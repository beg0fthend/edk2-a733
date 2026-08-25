# A733 native display bring-up for EDK2

**Date:** 2026-08-25
**Status:** design approved, implementation not started
**Supersedes:** the `Phase 3 (TODO)` note in `SunxiSimpleFbGopDxe.c`

## Problem

EDK2 on this board has never had a display driver. It has a framebuffer
squatter. `SunxiSimpleFbGopDxe` writes three registers in DE mixer0 layer 0 —
size, pitch, scanout address — commits them, and publishes the result as a
`GRAPHICS_OUTPUT_PROTOCOL`. A grep of the whole platform package for TCON,
HDMI, EDID or video-PLL code returns zero matches. The driver's own header says
as much: *"Does not initialise the display engine itself."*

Everything visible during EDK2 is therefore inherited from BSP U-Boot. We never
read EDID, never select a mode, never program a timing. The signal leaving the
HDMI port is identical no matter what is plugged in, and the geometry is six
hardcoded constants in the DSC.

Two consequences, both currently visible:

- **Only one display works.** The 1024x600 panel on the bench works because it
  tolerates whatever U-Boot left running, not because it is supported. There is
  no negotiation for any other display to succeed at.
- **Nothing can be relied on.** The configuration we inherit is whatever the
  previous boot stage happened to do. That is not a foundation to hand testers.

## Goals

1. Read the sink's EDID in EDK2 and select a mode from it.
2. Program the full pipeline ourselves: video PLL, DE, TCON, HDMI TX, HDMI PHY.
3. Publish a real GOP mode list, with **1920x1080 supported as a first-class
   mode** — testers will default to Full HD and anything less will read as broken.
4. Never regress today's behaviour: if any stage fails, fall back to the
   inherited pipeline exactly as it works now.

## Non-goals

- Audio, CEC, HDCP, eDP, DSI, LVDS.
- Scaler, sharpness, deband, gamma, FCM, and the rest of the DE post-processing
  chain. One overlay layer through one blender is all a firmware console needs.
- The Linux hand-off. Our GOP working does not fix what the kernel does after
  `ExitBootServices`. Separate problem, separate spec.
- PCIe Gen3, and every other open item unrelated to display.

## Established hardware facts

Everything in this section was measured on the board, not assumed.

### Register map

Taken from vendor device-tree node names in dmesg, not guessed:

| Block | Base | Notes |
|---|---|---|
| DE | `0x05000000` | `5000000.de`; reads as zero even when live |
| mixer0 | `0x05100000` | GLB regs read as zero (write-only/commit) |
| mixer0 layer 0 | `0x05101000` | the overlay we already drive |
| tcon_top vo0 | `0x05500000` | |
| tcon_top vo1 | `0x05510000` | `+0x20 = 0x10100000` when live |
| HDMI0 | `0x05520000` | **byte-addressable, see below** |
| TCON3 | `0x05730000` | the HDMI path |
| TCON4 | `0x05731000` | unused, all zeros |

> **Access width matters.** Read as 32-bit words, HDMI0 returns
> `21212121 9f9f9f9f` and looks dead. It is not. The register file is 8-bit.
> This is the same mistake that produced the false "DBI is locked" theory during
> the PCIe work and cost weeks. Check access width before believing a dump.

### The HDMI controller is stock Synopsys DesignWare HDMI 2.0 TX

Read with 8-bit accesses:

```
+0x00 DESIGN_ID   = 0x21     +0x01 REVISION_ID = 0x2a
+0x02 PRODUCT_ID0 = 0xa0     +0x03 PRODUCT_ID1 = 0xc1
+0x04..07 CONFIG0..3 = 9f 62 fe 00
```

`0xA0`/`0xC1` is the canonical DWC HDMI TX signature. This is the most
consequential finding in the investigation: the controller is standard IP, so
the public Synopsys databook and mainline `dw-hdmi.c` apply directly. The
Allwinner-specific surface shrinks to essentially the **PHY** alone.

### Verified golden timing — 1024x600

Captured from TCON3 with the vendor pipeline live:

| Offset | Value | Decoded |
|---|---|---|
| `+0x00` | `80000000` | bit31 = TCON enable |
| `+0x9c` | `03ff0257` | active 1024 x 600 |
| `+0xa0` | `053f009f` | H total 1344, HBP 160 |
| `+0xa4` | `04f60016` | **V total in half-lines: 0x4f6 = 1270 = 2 x 635**, VBP 23 |
| `+0xa8` | `00170001` | HSW 24, VSW 2 |

Cross-check: 1344 x 635 x 60 = 51.21 MHz, against the EDID's declared
51.20 MHz. The timing model is confirmed, including the half-line encoding of
vertical total — a detail that would have cost a day to find by experiment.

### Mixer0 layer 0 when live

`CTRL ff008003`, `SIZE 025703ff`, `PITCH 00001000`, `ADDR ff800000` — which
validates all four of the constants currently hardcoded in the DSC.

### Clocks

`pll-de` 1200 MHz → `pll-de-3x` 600 MHz → `de0` 600 MHz, enabled. Bus gates
`de0-gate`, `tcontv0` (= TCON3), `hdmi`, `hdmi-sfr` all enabled.

**`clk_summary` cannot be trusted for enable state here.** It reports `hdmi-tv`
and all three `pll-video*` as disabled while the pipeline is demonstrably
running, which means the vendor driver programs those CCU registers directly and
bypasses the clock framework. Consistent with the key-protected CCU behaviour
found during the ethernet investigation.

### The bench sink's EDID

256 bytes, base block plus a valid CEA-861 rev 3 extension. Both checksums good.
Manufacturer `MPI`, product `0x7010`, name `MPI7010`.

Base block:

- Preferred DTD **1024x600 @ 51.20 MHz**, htotal 1344, vtotal 635
- Standard timing 1920x1080 16:9 @ 60 Hz
- Established 640x480@60, 800x600@60, 1024x768@60
- Range limits 50–76 Hz, 30–81 kHz, max pixel clock 140 MHz

CEA extension — VICs 16 (1080p60), 5 (1080i60), 4 (720p60), 3 (480p60), plus
detailed timings:

| Mode | Pixel clock | htotal | vtotal |
|---|---|---|---|
| 1920x1080p60 | 148.50 MHz | 2200 | 1125 |
| 1280x720p60 | 74.25 MHz | 1650 | 750 |
| 720x480p60 | 27.00 MHz | 858 | 525 |
| 1024x600p60 | 51.20 MHz | 1344 | 635 |

Three requirements follow, each learned the hard way:

- **Never filter an explicit DTD or CEA VIC using the range-limits descriptor.**
  This sink declares a 140 MHz ceiling and then supplies a 148.5 MHz 1080p60
  timing three bytes later. Range limits are advisory and frequently wrong;
  explicit timings are authoritative. Trusting that field would permanently
  exclude 1080p from a display that plainly supports it.
- **A driver-synthesised mode list is not evidence.** The vendor driver reported
  21 modes; the sink advertises far fewer. Build the list from what the sink
  actually said.
- **Read EDID with repeated-START transactions.** An initial investigation using
  separate write and read transactions returned block 0 twice and produced a
  confident but entirely false "the EEPROM aliases" conclusion. EDID EEPROMs
  reset their address pointer on the intervening STOP. The extension block was
  there the whole time.

> Access method has now produced three false results in this project: 16-byte
> DBI reads fabricated "PCIe DBI is locked", 32-bit reads made a live HDMI
> controller look dead, and split I2C transactions invented a missing EDID
> extension. **Before concluding a block is broken, absent or locked, verify the
> access width and transaction shape.**

Those four timings span 27 to 148.5 MHz, which is unusually good PLL and PHY
coverage for validation. A driver that hits all four is unlikely to be
accidentally correct.

## Architecture

Four units, plus the existing driver retained as a fallback.

### 1. `EdidParserLib` — pure logic, no hardware

Bytes in, mode list out. No register access, no protocol dependencies.

Responsibilities: validate header and checksum; parse detailed timings,
standard timings and established timings; parse range limits; tolerate a
malformed, absent or mirrored extension block; **never discard a valid block 0
because the extension is bad**.

Because it touches no hardware, this is unit-testable on a development host
against the captured EDID bytes, with no board and no reboot cycle. It is the
one component we can prove correct before it ever runs on silicon, so it should
be built first and tested hardest.

### 2. `A733HdmiDdc` — EDID transport

The DWC `I2CM` master. Reads EDID blocks with **retry and exponential backoff**.

This is a separate unit for a specific reason: the boot-time failure observed on
this board was not a parsing bug, it was a *timing* bug — the sink was not ready
when the kernel probed at boot+6s, and a later re-detect succeeded. Retry policy
is load-bearing and deserves to be reasoned about in isolation.

### 3. `A733DisplayDxe` — the driver

Split along hardware seams so each file corresponds to one block and one section
of documentation:

| File | Owns | Primary source of truth |
|---|---|---|
| `Ccu.c` | video PLL, dividers, gates | live CCU register reads |
| `De.c` | mixer0, overlay, blender | golden trace |
| `Tcon.c` | TCON3 timing generation | golden trace (1024x600 verified) |
| `HdmiTx.c` | DWC controller | Synopsys databook / mainline |
| `HdmiPhy.c` | Allwinner PHY | hardware traces — the only custom part |

### 4. `SunxiSimpleFbGopDxe` — retained as fallback

Unchanged, demoted. Not deleted.

## Control flow

```
read EDID -> select mode -> PLL -> DE -> TCON -> HDMI TX -> PHY -> publish GOP
     |            |          |      |      |         |        |
     +------------+----------+------+------+---------+--------+--> any failure
                                                                   |
                                                    tear down cleanly, fall
                                                    back to SimpleFb
```

**Architectural rule: a failed stage must never leave the pipeline half
programmed.** Each stage records what it touched and unwinds on failure. The
fallback then inherits U-Boot's configuration exactly as today.

Serial console is on UART7 (`/dev/ttyS7`, MMIO `0x7080000`) and is unaffected by
any display failure, so a bad modeset costs a boot, never a brick. Note that the
vendor kernel's console is on ttyS0 — silence on UART7 during a vendor boot is
expected and is not evidence of a hang.

## Mode policy

The GOP advertises the sink's modes **filtered by what we can actually
generate**, not everything the sink claims. `SetMode` must never be offered a
mode the PLL cannot hit.

Selection order at boot:

1. **1920x1080 @ 60** if the sink advertises it — standard CEA timing,
   148.50 MHz, htotal 2200, vtotal 1125.
2. The sink's EDID-preferred timing.
3. 1280x720 @ 60 — 74.25 MHz, htotal 1650, vtotal 750.
4. 1024x600 @ 60 — 51.20 MHz, htotal 1344, vtotal 635.
5. 640x480 @ 60 — last resort.

CVT reduced blanking is **not** required. It was considered when the sink's
range-limits descriptor appeared to cap pixel clock at 140 MHz, but the sink
supplies a standard 148.5 MHz 1080p60 detailed timing and that field is simply
wrong.

1080p is placed first deliberately. Testers will connect ordinary monitors and
judge the firmware by whether it produces a normal picture on them.

## Error handling

- EDID read failure → retry with backoff, then fall back to a built-in safe mode
  list rather than failing the boot.
- Malformed EDID → use whatever parsed cleanly; a bad extension never
  invalidates a good base block.
- Self-contradictory EDID (as on the bench sink) → prefer explicit timings,
  respect range limits when choosing, try reduced blanking on conflict.
- Any pipeline stage failure → unwind, log to serial, fall back to SimpleFb.

## Testing

| Layer | Method | Needs board? |
|---|---|---|
| `EdidParserLib` | host unit tests against captured EDID, plus synthetic malformed cases | no |
| Register sequences | diff against golden traces | yes, SSH only |
| Signal correctness | capture card / HDMI analyser | yes |
| Real-world modes | 1080p monitor and several others | yes |

The capture card is what separates "we emitted a wrong signal" from "the monitor
rejected a correct one", which is otherwise the most expensive ambiguity in
display bring-up.

The bench chain advertises 1080p60 correctly, so Full HD can be validated on
the existing setup without re-cabling. Still worth confirming against a second,
independent 1080p monitor before calling it done — one sink's EDID is one data
point, and this one has already been shown to contain at least one wrong field.

## Open items

- **The CCU pixel-clock path is not yet established.** `clk_summary` is
  unreliable here, so the real parentage and divider chain for the TCON pixel
  clock must be read out of the CCU directly. This is a prerequisite for
  `Ccu.c`; it is listed as work, not as a known quantity.
- Whether the DWC PHY on this part is the Allwinner PHY (`phy_aw`) or the
  Innosilicon one (`phy_inno`); the vendor tree ships both.

## Licensing

The vendor BSP is `GPL-2.0-or-later`. Our tree is uniformly
`BSD-2-Clause-Patent`. **The tree stays BSD.**

Rationale: testers must be able to redistribute builds without inheriting source
obligations; upstreaming to TianoCore stays possible; and staying clean is cheap
here because the hardware can be observed directly.

Working policy:

- Register offsets, names and bit layouts — transcribed freely. These are
  datasheet facts.
- Initialisation sequences — derived from live register diffs. Vendor source
  read to understand *why* an ordering exists, then written fresh.
- PHY calibration constants — taken from hardware traces, not lifted from
  `phy_aw.c`.
- Allwinner credited in comments as the hardware-documentation source.
- **No vendor source file enters the repository.**

This is not "clean-room" in the strict two-team sense and should not be
described as such. It is original code informed by public documentation and
observed hardware behaviour — the ordinary way independent drivers are written.
This is an engineering judgement about risk, not legal advice.

## Build sequence

1. `EdidParserLib` with host tests — no hardware needed.
2. `A733HdmiDdc`, read-only. Cannot disturb the working display.
3. Golden traces at 1080p and 720p — two modes, so mode-dependent values
   separate from fixed ones.
4. `De.c` + `Tcon.c`, validated against trace, PHY left as U-Boot set it.
5. `HdmiTx.c` + `HdmiPhy.c` last, verified with the capture card.

Reference source, for documentation purposes only:
`orangepi-xunlong/linux-orangepi` @ `orange-pi-5.15-sun60iw2`, path
`bsp/drivers/drm/`. Note Allwinner uses a top-level `bsp/` tree, which is why
the conventional `drivers/gpu/drm/sunxi` paths do not exist.
