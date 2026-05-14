# TF-A (ARM Trusted Firmware) Integration for Allwinner A733

EDK2 runs as **BL33** under TF-A BL31.  You need to build both projects and
combine them into a single boot image accepted by the Allwinner BROM.

---

## 1. Boot Chain

```
BROM (on-chip ROM)
  └─▶ SPL (U-Boot or sunxi-fel)      — DRAM init, loads ATF+EDK2
        └─▶ BL31 (TF-A, EL3)        — secure world, PSCI, SMC dispatcher
              └─▶ BL33 (EDK2, EL2)  — UEFI firmware
                    └─▶ UEFI OS loader (grub / systemd-boot / Windows bootmgr)
```

---

## 2. TF-A Platform Port

TF-A does not yet have an upstream A733 platform.  The closest reference is
**`plat/allwinner`** which covers A64, H6, H616, and A100.  Use it as a base.

```bash
git clone https://git.trustedfirmware.org/TF-A/trusted-firmware-a.git
cd trusted-firmware-a
```

### 2.1 Create the platform directory

```
plat/allwinner/a733/
├── a733_def.h          # SoC-specific constants (see below)
├── platform.mk         # Makefile fragment
└── include/
    └── platform_def.h  # Memory map, BL addresses
```

**Minimal `platform_def.h` sketch:**

```c
// TODO: verify all addresses against A733 TRM.
#define PLAT_SUNXI_DRAM_BASE        0x40000000ULL
#define PLAT_SUNXI_DRAM_SIZE        0x180000000ULL   // 6 GB default

// BL31 lives in SRAM A2 (typical for sunxi)
#define BL31_BASE                   0x00044000
#define BL31_LIMIT                  0x0005BFFF

// BL33 = EDK2 loaded into DRAM
#define PRELOADED_BL33_BASE         0x44000000       // must match PcdFdBaseAddress
#define PLAT_ARM_BL33_SIZE          0x04000000       // 64 MB

// GICv3
#define PLAT_ARM_GICD_BASE          0x03400000
#define PLAT_ARM_GICR_BASE          0x03460000

// UART for TF-A console (NS16550)
#define PLAT_SUNXI_UART_BASE        0x02500000
#define PLAT_SUNXI_UART_CLOCK_IN_HZ 24000000
#define PLAT_SUNXI_UART_BAUDRATE    115200

// PSCI
#define PLAT_ARM_PSCI_PERFORMANCE_STATE_ID  0
```

**Minimal `platform.mk`:**

```makefile
PLAT_INCLUDES   += -Iplat/allwinner/a733/include
BL31_SOURCES    += plat/allwinner/common/sunxi_bl31_setup.c \
                   plat/allwinner/common/sunxi_cpu_ops.c    \
                   plat/allwinner/common/sunxi_pm.c         \
                   plat/allwinner/a733/a733_def.c
```

### 2.2 Build TF-A for A733

```bash
make PLAT=a733            \
     ARCH=aarch64         \
     ARM_ARCH_MAJOR=8     \
     ARM_ARCH_MINOR=2     \
     BL33=../edk2-build/ORANGEPI4PRO_EFI.fd \
     PRELOADED_BL33_BASE=0x44000000         \
     all fip
```

Output: `build/a733/release/fip.bin` — the Firmware Image Package containing
BL31 + EDK2 (BL33).

---

## 3. Allwinner BROM + SPL Flow

The A733 BROM loads an **eGON** or **TOC1** image from SD card / eMMC.  The
standard sunxi SPL (from U-Boot) understands both formats.

### 3.1 Build U-Boot SPL only (no U-Boot proper)

```bash
git clone https://github.com/u-boot/u-boot.git
cd u-boot

# Use the Orange Pi 4 Pro defconfig as a base.
# TODO: create orangepi_4pro_defconfig if it does not yet exist;
#       copy from the closest board (e.g. orangepi_5_plus if A523-based).
make orangepi_4pro_defconfig
make CROSS_COMPILE=aarch64-linux-gnu- spl/sunxi-spl.bin
```

### 3.2 Combine SPL + FIP into a flashable image

```bash
# Allwinner layout (SD card offsets in 512-byte sectors):
#   sector 16  (8 KB)  — SPL / boot0
#   sector 256 (128 KB)— FIP (BL31 + BL33/EDK2)

dd if=/dev/zero         of=orangepi4pro-uefi.img bs=1M count=128
dd if=spl/sunxi-spl.bin of=orangepi4pro-uefi.img bs=512 seek=16   conv=notrunc
dd if=fip.bin           of=orangepi4pro-uefi.img bs=512 seek=256  conv=notrunc
```

### 3.3 Flash to microSD

```bash
# Replace sdX with your SD card device — verify with lsblk BEFORE running.
sudo dd if=orangepi4pro-uefi.img of=/dev/sdX bs=1M status=progress
sync
```

---

## 4. PSCI — CPU On/Off

EDK2's `ArmPkg/Drivers/CpuDxe` issues `CPU_ON` SMC calls to TF-A for SMP.
TF-A's sunxi platform handles these via `sunxi_cpu_on()`.

  A733 = 6×A55 (cluster 0, MPIDR Aff1=0x0, Aff0=0..5) +
          2×A76 (cluster 1, MPIDR Aff1=0x1, Aff0=0..1)  [confirmed]

---

## 5. UEFI → Linux handoff (ACPI or DeviceTree)

| Method     | Notes |
|------------|-------|
| **ACPI**   | Preferred for UEFI-class boot. Requires writing DSDT/SSDT tables for A733 peripherals. Complex but enables vanilla distro kernels. |
| **DeviceTree** | Pass the DTB as `EFI_CONFIGURATION_TABLE` with `DEVICE_TREE_GUID`. Much less work; the existing mainline A733/OrangePi DTS can be reused. |

For initial bringup, use the **DeviceTree** path.  Add to your DSC:
```ini
[PcdsFixedAtBuild]
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuMemorySize|32
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|0
```
And include `EmbeddedPkg/Library/FdtLib/FdtLib.inf` + a DTB FV section.

---

## 6. Key TODOs Summary

| # | Item | Status | Where |
|---|------|--------|-------|
| 1 | UART0 base 0x02500000, PB0/PB1 pinmux | **CONFIRMED** | `A733.h`, DSC |
| 2 | GICv3 GICD=0x03400000, GICR=0x03460000 | **CONFIRMED** | `A733.h`, DSC |
| 3 | MPIDR: A55 cluster 0 (0x0-0x5), A76 cluster 1 (0x100-0x101) | **CONFIRMED** | `OrangePi4ProLib.c` |
| 4 | MMC0 SD base 0x04020000 | **CONFIRMED** | `A733.h` |
| 5 | Confirm `PRELOADED_BL33_BASE=0x44000000` matches TF-A + EDK2 PCDs | TODO | Both |
| 6 | Write Allwinner eMMC (SMHC) DXE driver | TODO | New driver |
| 7 | Write/port USB3 xHCI driver for A733 | TODO | New driver |
| 8 | Add DTB or ACPI tables | TODO | Platform FDF |
| 9 | CPUCFG base address for TF-A PSCI CPU_ON | TODO | TF-A `sunxi_cpu_ops.c` |
