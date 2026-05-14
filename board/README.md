# board/ — on-device boot artifacts

These files live on the **NVMe `/boot/`** partition of the Orange Pi 4 Pro
(rootfs `/dev/nvme0n1p1`). They are **not** consumed by the EDK2 build —
they are tracked here so the repo captures the exact boot-script logic
that selects between EDK2 and stock Linux.

| File                        | What it is                                         |
| --------------------------- | -------------------------------------------------- |
| `nvme-boot.cmd`             | Current BSP U-Boot script with EDK2 chainload + `skip_edk2` flag check |
| `nvme-boot.cmd.pre-skip`    | Original BSP version (pre-flag), kept for revert   |

`boot.scr` (the `mkimage`-wrapped form U-Boot actually loads) is
regenerated on the device with:

```bash
sudo mkimage -C none -A arm -T script -d /boot/boot.cmd /boot/boot.scr
```

See the **Dev workflow — booting Linux without the SD card** section in
the top-level `README.md` for the full procedure.
