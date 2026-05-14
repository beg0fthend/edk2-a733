# USB Bring-up Status — A733 (Orange Pi 4 Pro)

Last updated: build #14 (`8221bfcd`)

## ✅ Working

- USB2 power domain on (PCK600 + R-CCU R_PPU gate)
- All USB CCU clocks/resets gated to live-Linux values
- SYSCFG resistance calibration for ports 0 + 1
- EHCI internal PHY woken (SIDDQ cleared at HCI+0x810)
- AHB passby + INCR4/8/16 + ULPI bypass at HCI+0x800 (`0x00008B01`)
- PHY tune programmed (`0x063338C6` at HCI+0x818)
- EHCI0 (0x04101000) and EHCI1 (0x04200000) registered as
  NonDiscoverable EHCI; EhciDxe attaches to both
- EHCI controller reaches RUN state (USBSTS HALT bit clears)

## ❌ Blocked

1. **EHCI schedule status bits never echo.** USBCMD PSE/ASE both write
   and read back set, but USBSTS PSS/ASS stay 0 — controller is
   running but scheduler isn't ticking. Most likely DMA cache
   coherency: EhciDxe allocates frame list via `BusMasterCommonBuffer`
   on a `NonDiscoverableDeviceDmaTypeNonCoherent` device, but our MMU
   table maps DRAM as cached/writeback. Controller fetches stale data
   and refuses to schedule.

2. **PORTSC stays 0x1000 (no CONNECT) on both EHCIs.** Even though
   live Linux shows a hub on Bus 03 (EHCI0) carrying both the keyboard
   and Ubuntu USB stick, our firmware's port never reports a connect
   after `CONFIGFLAG=1`. Possibly tied to (1).

3. **No OhciDxe in EDK2 MdeModulePkg.** Razer keyboard alone enumerates
   on OHCI in Linux. We can't drive low/full-speed devices directly
   without an OHCI driver — but the hub on EHCI fronts the keyboard, so
   if (1)+(2) are fixed we get keyboard + Ubuntu USB anyway.

4. **PIO writes silently dropped.** Live `/dev/mem` read of PIO bank PB
   shows MUX0/DAT all-zero on Linux too — VBUS isn't actually wired to
   PB7 on this board (DTS comments suggest hard-wired 5V or AXP-managed).
   The PB7 GPIO toggle code in `SunxiUsbPhyInit` is harmless dead-code
   and can stay or be removed.

## Next-Session Investigation Order

1. **Fix DMA coherency**: either map a chunk of DRAM as
   `EFI_MEMORY_UC` for USB DMA buffers, or wire `dma_alloc` through
   `NonCoherentPciIoAllocateBuffer` correctly. Check if
   `NonDiscoverablePciDeviceDxe`'s coherent buffer support is actually
   enabled on this build.
2. Add cache maintenance (`WriteBackInvalidateDataCacheRange`) around
   the frame list and async list before kicking schedules.
3. Try a manual `EhcReadOpReg(EHC_FRAME_BASE_OFFSET)` after
   `EhcInitSched` to confirm the address is in valid DRAM, not garbage.
4. Once schedule echoes, port-power + reset cycle should make a hi-speed
   hub appear; the rest of UsbBus enumeration should follow.

## Live Reference (working Linux state)

```
EHCI0 (Bus 03): hub at port 1
  └── HID @ p1 (12M, full-speed = keyboard)
  └── Mass Storage @ p3 (480M, hi-speed = Ubuntu USB)
EHCI1 (Bus 05): root hub only
OHCI0 (Bus 04): root hub only
OHCI1 (Bus 06): HID with 3 interfaces (Razer Ornata V3 X)
xHCI2 (Bus 01/02): root hubs only (USB3 ComboPHY not yet inited)

Linux EHCI0 op-regs (idle): USBCMD=0x00010004 USBSTS=0x00001008 (HALT=1)
Linux EHCI1 op-regs (idle): USBCMD=0x00010015 USBSTS=0x00004008 (PSS=1,HALT=1)
PHY/passby: +0x800=0x8B01 +0x808=0 +0x810=0x34810 +0x818=0x63338C6 (matches our firmware)
```

## Files Touched

- `Platform/OrangePi/OrangePi4ProPkg/Drivers/SunxiUsbDxe/SunxiUsbDxe.c`
- `Platform/OrangePi/OrangePi4ProPkg/Drivers/SunxiUsbDxe/SunxiUsbDxe.inf`
- `Platform/OrangePi/OrangePi4ProPkg/OrangePi4Pro.fdf` (added SunxiUsbDxe + EhciDxe + UsbBusDxe + UsbKbDxe + UsbMassStorageDxe)
- `Platform/OrangePi/OrangePi4ProPkg/OrangePi4Pro.dsc` (matching library declarations)
- `Platform/OrangePi/OrangePi4ProPkg/Library/PlatformBootManagerLib/PlatformBootManagerLib.c` (banner trim)
