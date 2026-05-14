/** @file
  SunxiPcieDxe - PCIe / NVMe state-replay for Allwinner A733 (sun60iw2).

  Strategy
  --------
  The A733 PCIe controller is a Synopsys DesignWare Root Complex. Its
  DBI register window at 0x06000000 and its emulated root-port config
  space at 0x22200000 both read back as all-0xFF from EDK2 (and even
  from a running BSP Linux kernel via /dev/mem) — meaning DBI access
  is gated by an Allwinner-specific unlock register that we have not
  yet identified. Without DBI we cannot drive iATU windows and cannot
  enumerate config space ourselves.

  However: the BSP boot chain (BOOT0 + TF-A + U-Boot 2018.07) already
  brings the link UP, programs iATU, and assigns BARs **before** it
  hands off to BL33 (us). The endpoint NVMe SSD's BAR0 at
  PA 0x22100000 reads valid NVMe controller registers from EDK2:

      +0x0000 CAP    = 0x0a013fff
      +0x0008 VS     = 0x00010400  (NVMe 1.4)

  So instead of re-enumerating, we simply **adopt** the live MMIO
  region: register it as a NonDiscoverable NVMe device and let the
  generic EDK2 NvmExpressDxe driver bind to the PciIoProtocol that
  NonDiscoverablePciDeviceDxe synthesises.

  This is the same pattern SunxiUsbDxe uses for EHCI.

  Live values cross-checked against:
    research/sun60iw2-pcie-dbi-snapshot.txt   (NVMe BAR0 head dump)
    research/sun60iw2-iomem.txt               (Linux /proc/iomem)
        06000000-0647ffff : 6000000.pcie dbi
        22000000-27ffffff : pcie@6000000
          22100000-221fffff : PCI Bus 0000:01
            22100000-22103fff : 0000:01:00.0
              22100000-22103fff : nvme

  Copyright (c) 2026, carpi-os contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>

//
// NVMe controller MMIO (BAR0 already programmed by BSP U-Boot).
// 16 KB matches /proc/iomem on a running BSP Linux kernel.
//
#define A733_NVME_BAR0_BASE   0x22100000ULL
#define A733_NVME_BAR0_SIZE   0x00004000ULL

//
// NVMe controller register offsets (NVMe 1.4 spec, base spec section 3.1)
//
#define NVME_REG_CAP_LO       0x00   // Controller Capabilities, low 32 bits
#define NVME_REG_VS           0x08   // Version

EFI_STATUS
EFIAPI
SunxiPcieDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT32      CapLo;
  UINT32      Vers;

  //
  // Sanity-probe the NVMe BAR before claiming it. If BSP U-Boot has
  // not actually brought the link up (e.g. SSD removed / cold M.2 slot)
  // the read will return 0xFFFFFFFF and we should *not* register the
  // device — otherwise NvmExpressDxe will spin forever waiting for CSTS.RDY.
  //
  CapLo = MmioRead32 (A733_NVME_BAR0_BASE + NVME_REG_CAP_LO);
  Vers  = MmioRead32 (A733_NVME_BAR0_BASE + NVME_REG_VS);

  DEBUG ((
    DEBUG_ERROR,
    "SunxiPcieDxe: NVMe @ 0x%lx CAP_LO=0x%08x VS=0x%08x\n",
    A733_NVME_BAR0_BASE,
    CapLo,
    Vers
    ));

  if ((CapLo == 0xFFFFFFFF) || (CapLo == 0x00000000)) {
    DEBUG ((
      DEBUG_ERROR,
      "SunxiPcieDxe: NVMe BAR0 reads invalid (no link / no device) - skipping\n"
      ));
    return EFI_SUCCESS;
  }

  //
  // Hand the live MMIO range to NonDiscoverablePciDeviceDxe, which will
  // publish a PciIoProtocol against it. NvmExpressDxe will then bind via
  // its PCI-class match (Class=0x01 Mass Storage / SubClass=0x08 NVMe /
  // ProgIf=0x02 NVMe).
  //
  // DMA type: the BSP DT marks PCIe coherent (`dma-coherent`), and the
  // ARM CCI snoops PCIe traffic on this SoC, so coherent DMA is the
  // correct choice. (USB EHCI on the same SoC is non-coherent because
  // those controllers sit behind a different bus master.)
  //
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeNvme,
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,                          // InitFunc
             NULL,                          // OutHandle
             1,                             // NumMmioResources
             A733_NVME_BAR0_BASE,
             A733_NVME_BAR0_SIZE
             );

  DEBUG ((
    DEBUG_ERROR,
    "SunxiPcieDxe: NVMe register: %r\n",
    Status
    ));

  return Status;
}
