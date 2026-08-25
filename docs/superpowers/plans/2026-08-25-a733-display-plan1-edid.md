# A733 Display Plan 1 — EDID and Mode Enumeration

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make EDK2 read the attached display's EDID over DDC, parse it correctly, and report the modes it supports — with 1920x1080 correctly identified.

**Architecture:** A pure-logic parsing library (`A733EdidLib`) with host-based unit tests, a hardware transport library (`A733HdmiDdcLib`) that reads EDID bytes over the DesignWare HDMI I2C master, and a small DXE driver that ties them together and logs results to serial. No display programming happens in this plan — nothing here can change what is on screen, which makes every task safe to run on a working board.

**Tech Stack:** EDK2 (edk2-stable202605), C, `UnitTestFrameworkPkg` host tests built with GCC for X64, AARCH64 firmware build via `~/edk2/build_edk2.sh`.

**Spec:** `docs/superpowers/specs/2026-08-25-a733-display-design.md`

## Global Constraints

- **License:** every new file carries `SPDX-License-Identifier: BSD-2-Clause-Patent`. No vendor BSP source may be copied into the repo; it is hardware documentation only.
- **Package location:** all new code lives under `Platform/OrangePi/OrangePi4ProPkg/`. Only that directory is symlinked into `~/edk2`, so files placed elsewhere will not build.
- **Build dir:** firmware builds run from `~/edk2`, not `~/edk2-a733`.
- **No behaviour change to the display in this plan.** Nothing here writes a display register.
- **Never filter an explicitly-advertised DTD or CEA VIC using the range-limits descriptor.** The bench sink declares a 140 MHz ceiling and then supplies a 148.5 MHz 1080p60 timing. Range limits are advisory and frequently wrong.
- **EDID reads must use repeated-START I2C transactions.** Separate write-then-read returns block 0 twice on this sink and looks like a 128-byte EEPROM.
- **Verify access width before concluding a register block is dead.** The DWC HDMI register file is 8-bit; 32-bit reads return `21212121` and look like broken hardware.

---

## File Structure

| Path | Responsibility |
|---|---|
| `Include/Library/A733EdidLib.h` | Public types and API for EDID parsing |
| `Library/A733EdidLib/A733EdidLib.inf` | Library module definition |
| `Library/A733EdidLib/EdidBlock.c` | Header/checksum validation, block walking |
| `Library/A733EdidLib/EdidDtd.c` | Detailed Timing Descriptor decode |
| `Library/A733EdidLib/EdidBase.c` | Base block: descriptors, standard/established timings |
| `Library/A733EdidLib/EdidCea.c` | CEA-861 extension: VICs and extension DTDs |
| `Library/A733EdidLib/EdidModes.c` | Mode list assembly and boot-mode selection policy |
| `Library/A733EdidLib/EdidInternal.h` | Internal helpers shared by the above |
| `Include/Library/A733HdmiDdcLib.h` | Public API for reading EDID over DDC |
| `Library/A733HdmiDdcLib/A733HdmiDdcLib.inf` | Library module definition |
| `Library/A733HdmiDdcLib/HdmiDdc.c` | DWC HDMI I2C master, retry/backoff |
| `Library/A733HdmiDdcLib/DwHdmiI2cm.h` | DWC I2CM register definitions |
| `Drivers/A733DisplayInfoDxe/A733DisplayInfoDxe.inf` | Diagnostic driver module |
| `Drivers/A733DisplayInfoDxe/DisplayInfo.c` | Reads EDID, parses, logs to serial |
| `Test/OrangePi4ProPkgHostTest.dsc` | Host-test platform description |
| `Test/UnitTest/A733EdidLib/A733EdidLibHostTest.inf` | Host test module |
| `Test/UnitTest/A733EdidLib/TestEdid.c` | The unit tests |
| `Test/UnitTest/A733EdidLib/TestData.c` | Captured real EDID fixtures |
| `Test/UnitTest/A733EdidLib/TestData.h` | Fixture declarations |

---

### Task 1: EDID block validation and host test harness

Stands up the host test infrastructure and the first parsing function. This task is the gate: if host tests do not build and run here, nothing later in the plan is testable.

**Files:**
- Create: `Platform/OrangePi/OrangePi4ProPkg/Include/Library/A733EdidLib.h`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/A733EdidLib.inf`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/EdidBlock.c`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/A733EdidLibHostTest.inf`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/TestData.h`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/TestData.c`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/TestEdid.c`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `BOOLEAN A733EdidBlockIsValid (CONST UINT8 *Block)` — TRUE if the 128-byte block's checksum sums to zero mod 256.
  - `BOOLEAN A733EdidHeaderIsValid (CONST UINT8 *Edid)` — TRUE if bytes 0-7 are `00 FF FF FF FF FF FF 00`.
  - `UINT8 A733EdidExtensionCount (CONST UINT8 *Edid)` — byte 126.
  - Fixtures `gEdidMpi7010` (256 bytes) and `gEdidMpi7010Size`.

- [ ] **Step 1: Create the public header**

`Include/Library/A733EdidLib.h`:

```c
/** @file
  EDID parsing for the Allwinner A733 display stack.

  Pure logic: no register access, no protocol dependencies, so it can be
  built and tested on a development host.

  Copyright (c) 2026, carpi-os contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef A733_EDID_LIB_H_
#define A733_EDID_LIB_H_

#include <Uefi.h>

#define A733_EDID_BLOCK_SIZE   128
#define A733_EDID_MAX_MODES    32

///
/// One display timing, in the units the TCON ultimately wants.
///
typedef struct {
  UINT32     PixelClockHz;
  UINT16     HActive;
  UINT16     HBlank;
  UINT16     HSyncOffset;
  UINT16     HSyncWidth;
  UINT16     VActive;
  UINT16     VBlank;
  UINT16     VSyncOffset;
  UINT16     VSyncWidth;
  BOOLEAN    Interlaced;
  BOOLEAN    HSyncPositive;
  BOOLEAN    VSyncPositive;
} A733_DISPLAY_TIMING;

BOOLEAN
EFIAPI
A733EdidHeaderIsValid (
  IN CONST UINT8  *Edid
  );

BOOLEAN
EFIAPI
A733EdidBlockIsValid (
  IN CONST UINT8  *Block
  );

UINT8
EFIAPI
A733EdidExtensionCount (
  IN CONST UINT8  *Edid
  );

#endif // A733_EDID_LIB_H_
```

- [ ] **Step 2: Create the fixture data from the real display**

`Test/UnitTest/A733EdidLib/TestData.h`:

```c
/** @file
  EDID fixtures captured from real hardware.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef A733_EDID_TEST_DATA_H_
#define A733_EDID_TEST_DATA_H_

#include <Uefi.h>

extern CONST UINT8  gEdidMpi7010[];
extern CONST UINTN  gEdidMpi7010Size;

#endif
```

`Test/UnitTest/A733EdidLib/TestData.c` — this is the exact 256 bytes read from
`/sys/class/drm/card0-HDMI-A-1/edid` on the board. Do not hand-edit it:

```c
/** @file
  EDID captured from the bench display (MPI7010) on 2026-08-25.

  Base block plus a valid CEA-861 rev 3 extension. Notable properties that make
  it a good fixture: the range-limits descriptor declares a 140 MHz ceiling
  while the extension supplies a 148.5 MHz 1080p60 timing, so anything that
  filters modes by range limits will visibly fail against this data.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "TestData.h"

CONST UINT8  gEdidMpi7010[] = {
  0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x36, 0x09, 0x10, 0x70,
  0x01, 0x00, 0x00, 0x00, 0x09, 0x22, 0x01, 0x03, 0x80, 0x29, 0x1A, 0x78,
  0x8A, 0xC3, 0x25, 0xA4, 0x56, 0x49, 0x97, 0x24, 0x13, 0x50, 0x54, 0x21,
  0x08, 0x00, 0xD1, 0xC0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x14, 0x00, 0x40, 0x41, 0x58,
  0x23, 0x20, 0xA0, 0x18, 0xC2, 0x00, 0x98, 0x56, 0x00, 0x00, 0x00, 0x1E,
  0x00, 0x00, 0x00, 0xFC, 0x00, 0x4D, 0x50, 0x49, 0x37, 0x30, 0x31, 0x30,
  0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x32,
  0x4C, 0x1E, 0x51, 0x0E, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x00, 0x00, 0x00, 0x10, 0x00, 0x31, 0x35, 0x27, 0x48, 0x44, 0x4D, 0x49,
  0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xB7, 0x02, 0x03, 0x17, 0xC1,
  0x44, 0x10, 0x05, 0x04, 0x03, 0x23, 0x09, 0x87, 0x07, 0x83, 0x01, 0x00,
  0x00, 0x65, 0x03, 0x0C, 0x00, 0x5E, 0x1C, 0x02, 0x3A, 0x80, 0x18, 0x71,
  0x38, 0x2D, 0x40, 0x58, 0x2C, 0x45, 0x00, 0x14, 0x2B, 0x21, 0x00, 0x00,
  0x1E, 0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20, 0x6E, 0x28, 0x55,
  0x00, 0x14, 0x2B, 0x21, 0x00, 0x00, 0x1E, 0x8C, 0x0A, 0xD0, 0x8A, 0x20,
  0xE0, 0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00, 0x14, 0x2B, 0x21, 0x00, 0x00,
  0x18, 0x01, 0x1D, 0x80, 0x18, 0x71, 0x1C, 0x16, 0x20, 0x58, 0x2C, 0x25,
  0x00, 0x14, 0x2B, 0x21, 0x00, 0x00, 0x9E, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x65
};

CONST UINTN  gEdidMpi7010Size = sizeof (gEdidMpi7010);
```

- [ ] **Step 3: Write the failing test**

`Test/UnitTest/A733EdidLib/TestEdid.c`:

```c
/** @file
  Host-based unit tests for A733EdidLib.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/UnitTestLib.h>
#include <Library/DebugLib.h>
#include <Library/A733EdidLib.h>
#include "TestData.h"

#define SUITE_NAME  "A733EdidLib"

STATIC
UNIT_TEST_STATUS
EFIAPI
TestHeaderValid (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  UINT8  Bad[A733_EDID_BLOCK_SIZE];

  UT_ASSERT_TRUE (A733EdidHeaderIsValid (gEdidMpi7010));

  CopyMem (Bad, gEdidMpi7010, sizeof (Bad));
  Bad[0] = 0x01;
  UT_ASSERT_FALSE (A733EdidHeaderIsValid (Bad));

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestChecksums (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  UINT8  Bad[A733_EDID_BLOCK_SIZE];

  // Both the base block and the CEA extension must validate.
  UT_ASSERT_TRUE (A733EdidBlockIsValid (gEdidMpi7010));
  UT_ASSERT_TRUE (A733EdidBlockIsValid (gEdidMpi7010 + A733_EDID_BLOCK_SIZE));

  CopyMem (Bad, gEdidMpi7010, sizeof (Bad));
  Bad[10] = (UINT8)(Bad[10] + 1);
  UT_ASSERT_FALSE (A733EdidBlockIsValid (Bad));

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestExtensionCount (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  UT_ASSERT_EQUAL (A733EdidExtensionCount (gEdidMpi7010), 1);
  return UNIT_TEST_PASSED;
}

INT32
main (
  INT32  Argc,
  CHAR8  *Argv[]
  )
{
  EFI_STATUS                  Status;
  UNIT_TEST_FRAMEWORK_HANDLE  Fw;
  UNIT_TEST_SUITE_HANDLE      Suite;

  Fw = NULL;
  Status = InitUnitTestFramework (&Fw, SUITE_NAME, gEfiCallerBaseName, "0.1");
  if (EFI_ERROR (Status)) {
    return 1;
  }

  Status = CreateUnitTestSuite (&Suite, Fw, "EDID block validation", "Edid.Block", NULL, NULL);
  if (EFI_ERROR (Status)) {
    return 1;
  }

  AddTestCase (Suite, "Header is recognised", "Header", TestHeaderValid, NULL, NULL, NULL);
  AddTestCase (Suite, "Both block checksums validate", "Checksum", TestChecksums, NULL, NULL, NULL);
  AddTestCase (Suite, "Extension count is 1", "ExtCount", TestExtensionCount, NULL, NULL, NULL);

  Status = RunAllTestSuites (Fw);
  FreeUnitTestFramework (Fw);
  return EFI_ERROR (Status) ? 1 : 0;
}
```

- [ ] **Step 4: Create the host test INF**

`Test/UnitTest/A733EdidLib/A733EdidLibHostTest.inf`:

```ini
## @file
#  Host-based unit tests for A733EdidLib.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  INF_VERSION    = 0x00010005
  BASE_NAME      = A733EdidLibHostTest
  FILE_GUID      = 7C4E1A90-3B62-4D18-9E55-2A7F0C31B846
  MODULE_TYPE    = HOST_APPLICATION
  VERSION_STRING = 1.0

[Sources]
  TestEdid.c
  TestData.c
  TestData.h

[Packages]
  MdePkg/MdePkg.dec
  Platform/OrangePi/OrangePi4ProPkg/OrangePi4ProPkg.dec

[LibraryClasses]
  BaseLib
  BaseMemoryLib
  DebugLib
  UnitTestLib
  A733EdidLib
```

- [ ] **Step 5: Create the host test DSC**

`Test/OrangePi4ProPkgHostTest.dsc`:

```ini
## @file
#  Builds host-based unit tests for the OrangePi 4 Pro package.
#
#  These run on the development machine, not the board. The EDID parser is pure
#  logic, so it can be proven correct here before it ever runs on silicon --
#  which matters because every on-board iteration costs a reboot.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME           = OrangePi4ProPkgHostTest
  PLATFORM_GUID           = 6F2A5D71-08C4-49B3-A1E6-95D7C0B2384F
  PLATFORM_VERSION        = 0.1
  DSC_SPECIFICATION       = 0x00010005
  OUTPUT_DIRECTORY        = Build/OrangePi4Pro/HostTest
  SUPPORTED_ARCHITECTURES = IA32|X64
  BUILD_TARGETS           = NOOPT
  SKUID_IDENTIFIER        = DEFAULT

!include UnitTestFrameworkPkg/UnitTestFrameworkPkgHost.dsc.inc

[LibraryClasses]
  A733EdidLib|Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/A733EdidLib.inf

[Components]
  Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/A733EdidLibHostTest.inf
```

- [ ] **Step 6: Declare the library class in the package DEC**

Add to `Platform/OrangePi/OrangePi4ProPkg/OrangePi4ProPkg.dec` under a
`[LibraryClasses]` section (create the section if absent):

```ini
[LibraryClasses]
  ##  @libraryclass  EDID parsing, pure logic, host-testable.
  A733EdidLib|Include/Library/A733EdidLib.h
```

- [ ] **Step 7: Run the test to verify it fails**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc \
        -a X64 -t GCC -b NOOPT
```

Expected: build FAILS — `A733EdidLib.inf` does not exist yet.

- [ ] **Step 8: Create the library INF and implementation**

`Library/A733EdidLib/A733EdidLib.inf`:

```ini
## @file
#  EDID parsing library for the A733 display stack.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  INF_VERSION    = 0x00010005
  BASE_NAME      = A733EdidLib
  FILE_GUID      = 1E9D3C08-57A4-42F6-B0D1-6C83A594E27B
  MODULE_TYPE    = BASE
  VERSION_STRING = 1.0
  LIBRARY_CLASS  = A733EdidLib

[Sources]
  EdidBlock.c
  EdidInternal.h

[Packages]
  MdePkg/MdePkg.dec
  Platform/OrangePi/OrangePi4ProPkg/OrangePi4ProPkg.dec

[LibraryClasses]
  BaseLib
  BaseMemoryLib
  DebugLib
```

`Library/A733EdidLib/EdidInternal.h`:

```c
/** @file
  Internal helpers shared across the EDID parser.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef A733_EDID_INTERNAL_H_
#define A733_EDID_INTERNAL_H_

#include <Library/A733EdidLib.h>

#define EDID_HEADER_LEN        8
#define EDID_OFF_EXT_COUNT     126
#define EDID_OFF_DESCRIPTORS   54
#define EDID_DESCRIPTOR_LEN    18
#define EDID_DESCRIPTOR_COUNT  4

#endif
```

`Library/A733EdidLib/EdidBlock.c`:

```c
/** @file
  EDID block-level validation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/BaseMemoryLib.h>
#include "EdidInternal.h"

STATIC CONST UINT8  mEdidHeader[EDID_HEADER_LEN] = {
  0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

BOOLEAN
EFIAPI
A733EdidHeaderIsValid (
  IN CONST UINT8  *Edid
  )
{
  if (Edid == NULL) {
    return FALSE;
  }

  return (BOOLEAN)(CompareMem (Edid, mEdidHeader, EDID_HEADER_LEN) == 0);
}

BOOLEAN
EFIAPI
A733EdidBlockIsValid (
  IN CONST UINT8  *Block
  )
{
  UINTN  Index;
  UINT8  Sum;

  if (Block == NULL) {
    return FALSE;
  }

  Sum = 0;
  for (Index = 0; Index < A733_EDID_BLOCK_SIZE; Index++) {
    Sum = (UINT8)(Sum + Block[Index]);
  }

  return (BOOLEAN)(Sum == 0);
}

UINT8
EFIAPI
A733EdidExtensionCount (
  IN CONST UINT8  *Edid
  )
{
  if (Edid == NULL) {
    return 0;
  }

  return Edid[EDID_OFF_EXT_COUNT];
}
```

- [ ] **Step 9: Run the test to verify it passes**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc \
        -a X64 -t GCC -b NOOPT && \
  ./Build/OrangePi4Pro/HostTest/NOOPT_GCC/X64/A733EdidLibHostTest
```

Expected: all three tests PASS.

- [ ] **Step 10: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg/Include/Library/A733EdidLib.h \
        Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib \
        Platform/OrangePi/OrangePi4ProPkg/Test \
        Platform/OrangePi/OrangePi4ProPkg/OrangePi4ProPkg.dec
git commit -m "feat(edid): block validation with host-based unit tests

First host-testable code in this tree. The EDID parser is pure logic, so it
can be proven correct on a development machine instead of costing a reboot
per iteration.

Fixture is the real 256-byte EDID from the bench display, captured from the
board rather than hand-written."
```

---

### Task 2: Detailed Timing Descriptor decode

**Files:**
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/EdidDtd.c`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/A733EdidLib.inf` (add source)
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Include/Library/A733EdidLib.h` (add prototype)
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/TestEdid.c`

**Interfaces:**
- Consumes: `A733_DISPLAY_TIMING` from Task 1.
- Produces: `BOOLEAN A733EdidParseDtd (CONST UINT8 *Descriptor, A733_DISPLAY_TIMING *Timing)` — FALSE when the descriptor is a display descriptor (pixel clock zero) rather than a timing.

- [ ] **Step 1: Write the failing test**

Add to `TestEdid.c`, and register with `AddTestCase` in `main` alongside the existing cases:

```c
STATIC
UNIT_TEST_STATUS
EFIAPI
TestParseDtdPreferred (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  A733_DISPLAY_TIMING  T;

  // Base block's first descriptor: the preferred 1024x600 timing.
  UT_ASSERT_TRUE (A733EdidParseDtd (&gEdidMpi7010[54], &T));

  UT_ASSERT_EQUAL (T.PixelClockHz, 51200000);
  UT_ASSERT_EQUAL (T.HActive, 1024);
  UT_ASSERT_EQUAL (T.HBlank, 320);
  UT_ASSERT_EQUAL (T.VActive, 600);
  UT_ASSERT_EQUAL (T.VBlank, 35);
  UT_ASSERT_EQUAL (T.HSyncOffset, 160);
  UT_ASSERT_EQUAL (T.HSyncWidth, 24);
  UT_ASSERT_EQUAL (T.VSyncOffset, 12);
  UT_ASSERT_EQUAL (T.VSyncWidth, 2);
  UT_ASSERT_FALSE (T.Interlaced);
  UT_ASSERT_TRUE (T.HSyncPositive);
  UT_ASSERT_TRUE (T.VSyncPositive);

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestParseDtd1080p (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  A733_DISPLAY_TIMING  T;

  // First detailed timing inside the CEA extension: standard 1080p60.
  // Extension starts at 128, its DTD offset byte (index 2) is 23.
  UT_ASSERT_TRUE (A733EdidParseDtd (&gEdidMpi7010[128 + 23], &T));

  UT_ASSERT_EQUAL (T.PixelClockHz, 148500000);
  UT_ASSERT_EQUAL (T.HActive, 1920);
  UT_ASSERT_EQUAL (T.HBlank, 280);      // htotal 2200
  UT_ASSERT_EQUAL (T.VActive, 1080);
  UT_ASSERT_EQUAL (T.VBlank, 45);       // vtotal 1125
  UT_ASSERT_EQUAL (T.HSyncOffset, 88);
  UT_ASSERT_EQUAL (T.HSyncWidth, 44);
  UT_ASSERT_EQUAL (T.VSyncOffset, 4);
  UT_ASSERT_EQUAL (T.VSyncWidth, 5);
  UT_ASSERT_FALSE (T.Interlaced);

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestParseDtdRejectsDisplayDescriptor (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  A733_DISPLAY_TIMING  T;

  // Byte 72 is the 0xFC monitor-name descriptor: pixel clock zero marks it as
  // NOT a timing. Treating it as one is what produced the vendor driver's
  // "pixel clock[0KHz] invalid!" noise.
  UT_ASSERT_FALSE (A733EdidParseDtd (&gEdidMpi7010[72], &T));

  return UNIT_TEST_PASSED;
}
```

Register them:

```c
  AddTestCase (Suite, "Preferred DTD decodes to 1024x600", "DtdPreferred", TestParseDtdPreferred, NULL, NULL, NULL);
  AddTestCase (Suite, "CEA DTD decodes to 1080p60", "Dtd1080p", TestParseDtd1080p, NULL, NULL, NULL);
  AddTestCase (Suite, "Display descriptor is not a timing", "DtdReject", TestParseDtdRejectsDisplayDescriptor, NULL, NULL, NULL);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc -a X64 -t GCC -b NOOPT
```

Expected: FAIL — `A733EdidParseDtd` undeclared.

- [ ] **Step 3: Add the prototype**

Add to `Include/Library/A733EdidLib.h` before the closing `#endif`:

```c
BOOLEAN
EFIAPI
A733EdidParseDtd (
  IN  CONST UINT8          *Descriptor,
  OUT A733_DISPLAY_TIMING  *Timing
  );
```

- [ ] **Step 4: Implement**

`Library/A733EdidLib/EdidDtd.c`:

```c
/** @file
  Detailed Timing Descriptor decode.

  An 18-byte descriptor whose first two bytes are zero is a *display*
  descriptor (monitor name, range limits, serial), not a timing. Pixel clock
  zero is the sentinel that says so. Parsing those as timings is exactly what
  makes a driver log "pixel clock 0 invalid" three times per EDID.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "EdidInternal.h"

BOOLEAN
EFIAPI
A733EdidParseDtd (
  IN  CONST UINT8          *Descriptor,
  OUT A733_DISPLAY_TIMING  *Timing
  )
{
  UINT16  Clock10kHz;

  if ((Descriptor == NULL) || (Timing == NULL)) {
    return FALSE;
  }

  Clock10kHz = (UINT16)(Descriptor[0] | (Descriptor[1] << 8));
  if (Clock10kHz == 0) {
    return FALSE;
  }

  Timing->PixelClockHz = (UINT32)Clock10kHz * 10000;

  Timing->HActive = (UINT16)(Descriptor[2] | ((Descriptor[4] >> 4) << 8));
  Timing->HBlank  = (UINT16)(Descriptor[3] | ((Descriptor[4] & 0x0F) << 8));
  Timing->VActive = (UINT16)(Descriptor[5] | ((Descriptor[7] >> 4) << 8));
  Timing->VBlank  = (UINT16)(Descriptor[6] | ((Descriptor[7] & 0x0F) << 8));

  Timing->HSyncOffset = (UINT16)(Descriptor[8] | (((Descriptor[11] >> 6) & 0x03) << 8));
  Timing->HSyncWidth  = (UINT16)(Descriptor[9] | (((Descriptor[11] >> 4) & 0x03) << 8));
  Timing->VSyncOffset = (UINT16)(((Descriptor[10] >> 4) & 0x0F) | (((Descriptor[11] >> 2) & 0x03) << 4));
  Timing->VSyncWidth  = (UINT16)((Descriptor[10] & 0x0F) | ((Descriptor[11] & 0x03) << 4));

  Timing->Interlaced    = (BOOLEAN)((Descriptor[17] & 0x80) != 0);
  Timing->VSyncPositive = (BOOLEAN)((Descriptor[17] & 0x04) != 0);
  Timing->HSyncPositive = (BOOLEAN)((Descriptor[17] & 0x02) != 0);

  return TRUE;
}
```

- [ ] **Step 5: Add the source to the library INF**

In `Library/A733EdidLib/A733EdidLib.inf`, `[Sources]` becomes:

```ini
[Sources]
  EdidBlock.c
  EdidDtd.c
  EdidInternal.h
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc -a X64 -t GCC -b NOOPT && \
  ./Build/OrangePi4Pro/HostTest/NOOPT_GCC/X64/A733EdidLibHostTest
```

Expected: six tests PASS.

- [ ] **Step 7: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg
git commit -m "feat(edid): decode detailed timing descriptors

Verified against the bench display's real timings: 1024x600 at 51.2 MHz from
the base block, and standard 1080p60 at 148.5 MHz from the CEA extension.

Display descriptors are correctly rejected rather than parsed as timings with
a zero pixel clock."
```

---

### Task 3: CEA-861 extension parsing

**Files:**
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/EdidCea.c`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/A733EdidLib.inf`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Include/Library/A733EdidLib.h`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/TestEdid.c`

**Interfaces:**
- Consumes: `A733EdidParseDtd` from Task 2.
- Produces:
  - `BOOLEAN A733EdidCeaIsValid (CONST UINT8 *Ext)` — TRUE if tag byte is 0x02 and checksum passes.
  - `UINTN A733EdidCeaGetVics (CONST UINT8 *Ext, UINT8 *Vics, UINTN MaxVics)` — returns count written.
  - `BOOLEAN A733EdidCeaHas1080p (CONST UINT8 *Ext)` — TRUE if VIC 16, 31, 32, 33 or 34 present.

- [ ] **Step 1: Write the failing test**

```c
STATIC
UNIT_TEST_STATUS
EFIAPI
TestCeaExtension (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  CONST UINT8  *Ext;
  UINT8        Vics[16];
  UINTN        Count;

  Ext = &gEdidMpi7010[128];

  UT_ASSERT_TRUE (A733EdidCeaIsValid (Ext));

  Count = A733EdidCeaGetVics (Ext, Vics, ARRAY_SIZE (Vics));
  UT_ASSERT_EQUAL (Count, 4);
  UT_ASSERT_EQUAL (Vics[0], 16);   // 1920x1080p60
  UT_ASSERT_EQUAL (Vics[1], 5);    // 1920x1080i60
  UT_ASSERT_EQUAL (Vics[2], 4);    // 1280x720p60
  UT_ASSERT_EQUAL (Vics[3], 3);    // 720x480p60

  UT_ASSERT_TRUE (A733EdidCeaHas1080p (Ext));

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestCeaRejectsNonCea (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  // The base block is not a CEA extension. A parser that accepts it will walk
  // garbage -- this is the failure mode that produced three bogus "invalid
  // pixel clock" errors when a mirrored block 0 was parsed as an extension.
  UT_ASSERT_FALSE (A733EdidCeaIsValid (gEdidMpi7010));
  return UNIT_TEST_PASSED;
}
```

Register:

```c
  AddTestCase (Suite, "CEA extension yields expected VICs", "Cea", TestCeaExtension, NULL, NULL, NULL);
  AddTestCase (Suite, "Non-CEA block is rejected", "CeaReject", TestCeaRejectsNonCea, NULL, NULL, NULL);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc -a X64 -t GCC -b NOOPT
```

Expected: FAIL — CEA functions undeclared.

- [ ] **Step 3: Add prototypes**

Add to `Include/Library/A733EdidLib.h`:

```c
BOOLEAN
EFIAPI
A733EdidCeaIsValid (
  IN CONST UINT8  *Ext
  );

UINTN
EFIAPI
A733EdidCeaGetVics (
  IN  CONST UINT8  *Ext,
  OUT UINT8        *Vics,
  IN  UINTN        MaxVics
  );

BOOLEAN
EFIAPI
A733EdidCeaHas1080p (
  IN CONST UINT8  *Ext
  );
```

- [ ] **Step 4: Implement**

`Library/A733EdidLib/EdidCea.c`:

```c
/** @file
  CEA-861 extension block parsing.

  The data block collection runs from byte 4 up to the DTD offset in byte 2.
  Each entry is a one-byte header: top three bits are the block tag, low five
  bits the payload length. Tag 2 is the video data block, whose payload is a
  list of VICs with bit 7 marking "native".

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "EdidInternal.h"

#define CEA_TAG_EXTENSION    0x02
#define CEA_BLOCK_VIDEO      0x02
#define CEA_OFF_TAG          0
#define CEA_OFF_DTD_START    2
#define CEA_DATA_BLOCK_START 4

BOOLEAN
EFIAPI
A733EdidCeaIsValid (
  IN CONST UINT8  *Ext
  )
{
  if (Ext == NULL) {
    return FALSE;
  }

  if (Ext[CEA_OFF_TAG] != CEA_TAG_EXTENSION) {
    return FALSE;
  }

  return A733EdidBlockIsValid (Ext);
}

UINTN
EFIAPI
A733EdidCeaGetVics (
  IN  CONST UINT8  *Ext,
  OUT UINT8        *Vics,
  IN  UINTN        MaxVics
  )
{
  UINTN  Index;
  UINTN  End;
  UINTN  Count;
  UINT8  Tag;
  UINT8  Length;
  UINTN  Payload;

  if ((Ext == NULL) || (Vics == NULL) || (MaxVics == 0)) {
    return 0;
  }

  if (!A733EdidCeaIsValid (Ext)) {
    return 0;
  }

  End = Ext[CEA_OFF_DTD_START];
  if ((End < CEA_DATA_BLOCK_START) || (End > A733_EDID_BLOCK_SIZE)) {
    return 0;
  }

  Count = 0;
  Index = CEA_DATA_BLOCK_START;

  while (Index < End) {
    Tag    = (UINT8)((Ext[Index] >> 5) & 0x07);
    Length = (UINT8)(Ext[Index] & 0x1F);

    if ((Index + 1 + Length) > End) {
      break;
    }

    if (Tag == CEA_BLOCK_VIDEO) {
      for (Payload = 0; Payload < Length; Payload++) {
        if (Count >= MaxVics) {
          return Count;
        }

        Vics[Count] = (UINT8)(Ext[Index + 1 + Payload] & 0x7F);
        Count++;
      }
    }

    Index += (UINTN)Length + 1;
  }

  return Count;
}

BOOLEAN
EFIAPI
A733EdidCeaHas1080p (
  IN CONST UINT8  *Ext
  )
{
  UINT8  Vics[32];
  UINTN  Count;
  UINTN  Index;

  Count = A733EdidCeaGetVics (Ext, Vics, ARRAY_SIZE (Vics));

  for (Index = 0; Index < Count; Index++) {
    switch (Vics[Index]) {
      case 16:  // 1920x1080p60
      case 31:  // 1920x1080p50
      case 32:  // 1920x1080p24
      case 33:  // 1920x1080p25
      case 34:  // 1920x1080p30
        return TRUE;
      default:
        break;
    }
  }

  return FALSE;
}
```

- [ ] **Step 5: Add the source to the INF**

`[Sources]` becomes:

```ini
[Sources]
  EdidBlock.c
  EdidCea.c
  EdidDtd.c
  EdidInternal.h
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc -a X64 -t GCC -b NOOPT && \
  ./Build/OrangePi4Pro/HostTest/NOOPT_GCC/X64/A733EdidLibHostTest
```

Expected: eight tests PASS.

- [ ] **Step 7: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg
git commit -m "feat(edid): parse CEA-861 extension and video data block

Extracts VICs 16, 5, 4 and 3 from the bench display, confirming it advertises
1080p60. A non-CEA block is rejected rather than walked as one."
```

---

### Task 4: Mode list assembly and boot-mode selection

This is where the spec's mode policy becomes code, including the rule that range limits must never veto an explicit timing.

**Files:**
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/EdidModes.c`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/A733EdidLib.inf`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Include/Library/A733EdidLib.h`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Test/UnitTest/A733EdidLib/TestEdid.c`

**Interfaces:**
- Consumes: `A733EdidParseDtd`, `A733EdidCeaIsValid`, `A733EdidBlockIsValid`, `A733EdidHeaderIsValid`.
- Produces:
  - `A733_EDID_INFO` struct.
  - `EFI_STATUS A733EdidParse (CONST UINT8 *Edid, UINTN Size, A733_EDID_INFO *Info)`
  - `CONST A733_DISPLAY_TIMING *A733EdidSelectBootMode (CONST A733_EDID_INFO *Info)`

- [ ] **Step 1: Add the info type to the public header**

Insert into `Include/Library/A733EdidLib.h` after `A733_DISPLAY_TIMING`:

```c
///
/// Everything the display stack needs from an EDID.
///
typedef struct {
  A733_DISPLAY_TIMING    Modes[A733_EDID_MAX_MODES];
  UINTN                  ModeCount;
  UINTN                  PreferredIndex;
  BOOLEAN                PreferredValid;
  UINT16                 ManufacturerId;
  UINT16                 ProductCode;
  CHAR8                  MonitorName[14];
  UINT32                 MaxPixelClockHz;   ///< 0 when not declared. ADVISORY ONLY.
  BOOLEAN                Has1080p;
} A733_EDID_INFO;

EFI_STATUS
EFIAPI
A733EdidParse (
  IN  CONST UINT8     *Edid,
  IN  UINTN           Size,
  OUT A733_EDID_INFO  *Info
  );

CONST A733_DISPLAY_TIMING *
EFIAPI
A733EdidSelectBootMode (
  IN CONST A733_EDID_INFO  *Info
  );
```

- [ ] **Step 2: Write the failing test**

```c
STATIC
UNIT_TEST_STATUS
EFIAPI
TestParseFull (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  A733_EDID_INFO  Info;

  UT_ASSERT_NOT_EFI_ERROR (A733EdidParse (gEdidMpi7010, gEdidMpi7010Size, &Info));

  UT_ASSERT_TRUE (Info.PreferredValid);
  UT_ASSERT_EQUAL (Info.Modes[Info.PreferredIndex].HActive, 1024);
  UT_ASSERT_EQUAL (Info.Modes[Info.PreferredIndex].VActive, 600);

  UT_ASSERT_TRUE (Info.Has1080p);
  UT_ASSERT_EQUAL (Info.MaxPixelClockHz, 140000000);   // declared, and wrong
  UT_ASSERT_EQUAL (AsciiStrCmp (Info.MonitorName, "MPI7010"), 0);

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestRangeLimitsDoNotVeto1080p (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  A733_EDID_INFO             Info;
  CONST A733_DISPLAY_TIMING  *Mode;
  UINTN                      Index;
  BOOLEAN                    Found;

  UT_ASSERT_NOT_EFI_ERROR (A733EdidParse (gEdidMpi7010, gEdidMpi7010Size, &Info));

  // This sink declares max 140 MHz and then supplies a 148.5 MHz 1080p60
  // timing. The 1080p mode must survive. Filtering by range limits here would
  // permanently exclude Full HD from a display that plainly supports it.
  Found = FALSE;
  for (Index = 0; Index < Info.ModeCount; Index++) {
    if ((Info.Modes[Index].HActive == 1920) && (Info.Modes[Index].VActive == 1080) &&
        !Info.Modes[Index].Interlaced) {
      Found = TRUE;
      UT_ASSERT_EQUAL (Info.Modes[Index].PixelClockHz, 148500000);
    }
  }

  UT_ASSERT_TRUE (Found);

  // And the boot mode must be 1080p, not the 1024x600 preferred timing.
  Mode = A733EdidSelectBootMode (&Info);
  UT_ASSERT_NOT_NULL (Mode);
  UT_ASSERT_EQUAL (Mode->HActive, 1920);
  UT_ASSERT_EQUAL (Mode->VActive, 1080);

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestBadExtensionKeepsBaseBlock (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  UINT8           Corrupt[256];
  A733_EDID_INFO  Info;

  CopyMem (Corrupt, gEdidMpi7010, sizeof (Corrupt));

  // Wreck the extension: wrong tag and a broken checksum.
  Corrupt[128] = 0xAA;
  Corrupt[255] = 0x00;

  // Base block modes must survive. Discarding them because the extension is
  // bad is the single most damaging thing an EDID parser can do.
  UT_ASSERT_NOT_EFI_ERROR (A733EdidParse (Corrupt, sizeof (Corrupt), &Info));
  UT_ASSERT_TRUE (Info.PreferredValid);
  UT_ASSERT_EQUAL (Info.Modes[Info.PreferredIndex].HActive, 1024);
  UT_ASSERT_TRUE (Info.ModeCount > 0);

  return UNIT_TEST_PASSED;
}

STATIC
UNIT_TEST_STATUS
EFIAPI
TestExtensionCountLiesAreHarmless (
  IN UNIT_TEST_CONTEXT  Context
  )
{
  UINT8           Truncated[128];
  A733_EDID_INFO  Info;

  // Byte 126 claims an extension, but only 128 bytes are supplied. Parsing
  // must not read past the buffer.
  CopyMem (Truncated, gEdidMpi7010, sizeof (Truncated));
  UT_ASSERT_EQUAL (Truncated[126], 1);

  UT_ASSERT_NOT_EFI_ERROR (A733EdidParse (Truncated, sizeof (Truncated), &Info));
  UT_ASSERT_TRUE (Info.PreferredValid);

  return UNIT_TEST_PASSED;
}
```

Register:

```c
  AddTestCase (Suite, "Full parse extracts identity and modes", "ParseFull", TestParseFull, NULL, NULL, NULL);
  AddTestCase (Suite, "Range limits never veto 1080p", "NoVeto", TestRangeLimitsDoNotVeto1080p, NULL, NULL, NULL);
  AddTestCase (Suite, "Bad extension keeps base block modes", "BadExt", TestBadExtensionKeepsBaseBlock, NULL, NULL, NULL);
  AddTestCase (Suite, "Lying extension count is safe", "ExtLie", TestExtensionCountLiesAreHarmless, NULL, NULL, NULL);
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc -a X64 -t GCC -b NOOPT
```

Expected: FAIL — `A733EdidParse` undeclared.

- [ ] **Step 4: Implement**

`Library/A733EdidLib/EdidModes.c`:

```c
/** @file
  Assemble a mode list from an EDID and choose a boot mode.

  Two rules here are load-bearing, and both were learned from the bench sink:

    1. A malformed extension never invalidates a good base block. Throwing away
       parsed modes because a later block is broken leaves a working display
       with nothing to show.

    2. The range-limits descriptor is advisory and is frequently wrong. This
       sink declares a 140 MHz ceiling and then supplies a 148.5 MHz 1080p60
       detailed timing. Filtering explicit timings by that field would exclude
       Full HD from a display that plainly supports it.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include "EdidInternal.h"

#define EDID_DESC_TAG_NAME    0xFC
#define EDID_DESC_TAG_RANGE   0xFD

STATIC
VOID
AddMode (
  IN OUT A733_EDID_INFO       *Info,
  IN     A733_DISPLAY_TIMING  *Timing
  )
{
  UINTN  Index;

  if (Info->ModeCount >= A733_EDID_MAX_MODES) {
    return;
  }

  // Skip exact duplicates; sinks routinely list the same timing twice.
  for (Index = 0; Index < Info->ModeCount; Index++) {
    if ((Info->Modes[Index].HActive == Timing->HActive) &&
        (Info->Modes[Index].VActive == Timing->VActive) &&
        (Info->Modes[Index].PixelClockHz == Timing->PixelClockHz) &&
        (Info->Modes[Index].Interlaced == Timing->Interlaced))
    {
      return;
    }
  }

  CopyMem (&Info->Modes[Info->ModeCount], Timing, sizeof (A733_DISPLAY_TIMING));
  Info->ModeCount++;
}

STATIC
VOID
ParseDescriptors (
  IN     CONST UINT8     *Block,
  IN OUT A733_EDID_INFO  *Info
  )
{
  UINTN                Index;
  CONST UINT8          *Desc;
  A733_DISPLAY_TIMING  Timing;
  UINTN                Length;

  for (Index = 0; Index < EDID_DESCRIPTOR_COUNT; Index++) {
    Desc = Block + EDID_OFF_DESCRIPTORS + (Index * EDID_DESCRIPTOR_LEN);

    if (A733EdidParseDtd (Desc, &Timing)) {
      if (!Info->PreferredValid) {
        Info->PreferredValid = TRUE;
        Info->PreferredIndex = Info->ModeCount;
      }

      AddMode (Info, &Timing);
      continue;
    }

    // Not a timing: it is a display descriptor. Byte 3 carries the tag.
    switch (Desc[3]) {
      case EDID_DESC_TAG_NAME:
        for (Length = 0; Length < 13; Length++) {
          if ((Desc[5 + Length] == '\n') || (Desc[5 + Length] == 0)) {
            break;
          }

          Info->MonitorName[Length] = (CHAR8)Desc[5 + Length];
        }

        Info->MonitorName[Length] = '\0';
        break;

      case EDID_DESC_TAG_RANGE:
        // Recorded for diagnostics only. Never used to filter a mode.
        Info->MaxPixelClockHz = (UINT32)Desc[9] * 10000000;
        break;

      default:
        break;
    }
  }
}

EFI_STATUS
EFIAPI
A733EdidParse (
  IN  CONST UINT8     *Edid,
  IN  UINTN           Size,
  OUT A733_EDID_INFO  *Info
  )
{
  CONST UINT8          *Ext;
  UINTN                 Offset;
  UINTN                 Blocks;
  UINTN                 Index;
  A733_DISPLAY_TIMING   Timing;

  if ((Edid == NULL) || (Info == NULL) || (Size < A733_EDID_BLOCK_SIZE)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!A733EdidHeaderIsValid (Edid) || !A733EdidBlockIsValid (Edid)) {
    return EFI_VOLUME_CORRUPTED;
  }

  ZeroMem (Info, sizeof (A733_EDID_INFO));

  Info->ManufacturerId = (UINT16)((Edid[8] << 8) | Edid[9]);
  Info->ProductCode    = (UINT16)(Edid[10] | (Edid[11] << 8));

  ParseDescriptors (Edid, Info);

  //
  // Walk however many extension blocks the buffer actually contains. Byte 126
  // is a claim, not a guarantee: this sink claims one extension and, read the
  // wrong way, supplies none.
  //
  Blocks = Size / A733_EDID_BLOCK_SIZE;

  for (Index = 1; Index < Blocks; Index++) {
    Ext = Edid + (Index * A733_EDID_BLOCK_SIZE);

    if (!A733EdidCeaIsValid (Ext)) {
      continue;                       // bad extension: keep what we already have
    }

    Info->Has1080p = (BOOLEAN)(Info->Has1080p || A733EdidCeaHas1080p (Ext));

    Offset = Ext[2];
    if ((Offset < 4) || (Offset >= A733_EDID_BLOCK_SIZE)) {
      continue;
    }

    while ((Offset + EDID_DESCRIPTOR_LEN) <= A733_EDID_BLOCK_SIZE) {
      if (!A733EdidParseDtd (Ext + Offset, &Timing)) {
        break;
      }

      AddMode (Info, &Timing);
      Offset += EDID_DESCRIPTOR_LEN;
    }
  }

  return (Info->ModeCount > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

CONST A733_DISPLAY_TIMING *
EFIAPI
A733EdidSelectBootMode (
  IN CONST A733_EDID_INFO  *Info
  )
{
  UINTN  Index;

  if ((Info == NULL) || (Info->ModeCount == 0)) {
    return NULL;
  }

  //
  // 1080p first: testers connect ordinary monitors and judge the firmware by
  // whether it produces a normal picture on them.
  //
  for (Index = 0; Index < Info->ModeCount; Index++) {
    if ((Info->Modes[Index].HActive == 1920) &&
        (Info->Modes[Index].VActive == 1080) &&
        !Info->Modes[Index].Interlaced)
    {
      return &Info->Modes[Index];
    }
  }

  if (Info->PreferredValid && (Info->PreferredIndex < Info->ModeCount)) {
    return &Info->Modes[Info->PreferredIndex];
  }

  return &Info->Modes[0];
}
```

- [ ] **Step 5: Add the source to the INF**

```ini
[Sources]
  EdidBlock.c
  EdidCea.c
  EdidDtd.c
  EdidModes.c
  EdidInternal.h
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/Test/OrangePi4ProPkgHostTest.dsc -a X64 -t GCC -b NOOPT && \
  ./Build/OrangePi4Pro/HostTest/NOOPT_GCC/X64/A733EdidLibHostTest
```

Expected: twelve tests PASS.

- [ ] **Step 7: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg
git commit -m "feat(edid): mode list assembly and 1080p-first boot selection

Range limits are recorded for diagnostics but never used to filter a mode, and
a malformed extension can no longer discard a valid base block. Both rules are
covered by tests written against the bench display's real EDID, which declares
a 140 MHz ceiling and then supplies a 148.5 MHz 1080p60 timing."
```

---

### Task 5: DDC transport over the DesignWare HDMI I2C master

First task that touches hardware. It only reads, so it cannot change what is on screen.

**Files:**
- Create: `Platform/OrangePi/OrangePi4ProPkg/Include/Library/A733HdmiDdcLib.h`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733HdmiDdcLib/A733HdmiDdcLib.inf`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733HdmiDdcLib/DwHdmiI2cm.h`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Library/A733HdmiDdcLib/HdmiDdc.c`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/OrangePi4ProPkg.dec`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `EFI_STATUS A733HdmiDdcReadEdid (OUT UINT8 *Buffer, IN UINTN BufferSize, OUT UINTN *BytesRead)`.

> **Register access note:** the DWC HDMI register file is **8-bit**. Use `MmioRead8`/`MmioWrite8` throughout. A 32-bit read of the ID registers returns `21212121` and makes a working controller look dead.

- [ ] **Step 1: Create the register header**

`Library/A733HdmiDdcLib/DwHdmiI2cm.h`:

```c
/** @file
  Synopsys DesignWare HDMI TX I2C master (DDC) registers.

  Confirmed on hardware by reading the identification registers with 8-bit
  accesses: DESIGN_ID 0x21, REVISION_ID 0x2a, PRODUCT_ID 0xa0/0xc1 -- the
  canonical DWC HDMI TX signature.

  The entire register file is byte-wide. Word accesses return the same byte
  replicated across all four lanes.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef DW_HDMI_I2CM_H_
#define DW_HDMI_I2CM_H_

#define A733_HDMI_BASE            0x05520000ULL

#define DW_HDMI_DESIGN_ID         0x0000
#define DW_HDMI_REVISION_ID       0x0001
#define DW_HDMI_PRODUCT_ID0       0x0002
#define DW_HDMI_PRODUCT_ID1       0x0003

#define DW_HDMI_DESIGN_ID_VALUE   0x21
#define DW_HDMI_PRODUCT_ID0_VALUE 0xA0
#define DW_HDMI_PRODUCT_ID1_VALUE 0xC1

#define DW_HDMI_I2CM_SLAVE        0x7E00
#define DW_HDMI_I2CM_ADDRESS      0x7E01
#define DW_HDMI_I2CM_DATAO        0x7E02
#define DW_HDMI_I2CM_DATAI        0x7E03
#define DW_HDMI_I2CM_OPERATION    0x7E04
#define DW_HDMI_I2CM_INT          0x7E05
#define DW_HDMI_I2CM_CTLINT       0x7E06
#define DW_HDMI_I2CM_DIV          0x7E07
#define DW_HDMI_I2CM_SEGADDR      0x7E08
#define DW_HDMI_I2CM_SOFTRSTZ     0x7E09
#define DW_HDMI_I2CM_SEGPTR       0x7E0A

#define DW_HDMI_IH_I2CM_STAT0     0x0105
#define DW_HDMI_IH_MUTE_I2CM_STAT0 0x0185

#define I2CM_OP_READ              BIT0
#define I2CM_OP_READ_EXT          BIT1
#define I2CM_OP_WRITE             BIT4

#define I2CM_STAT_ERROR           BIT0
#define I2CM_STAT_DONE            BIT1

#define EDID_I2C_ADDR             0x50
#define EDID_SEGMENT_ADDR         0x30

#endif // DW_HDMI_I2CM_H_
```

- [ ] **Step 2: Create the public header**

`Include/Library/A733HdmiDdcLib.h`:

```c
/** @file
  Read EDID from the display over the HDMI DDC channel.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef A733_HDMI_DDC_LIB_H_
#define A733_HDMI_DDC_LIB_H_

#include <Uefi.h>

/**
  Read up to BufferSize bytes of EDID from the attached sink.

  Retries with backoff: a sink is frequently not ready immediately after
  hotplug, and the failure observed on this board at boot was a timing problem
  rather than a parsing one.

  @retval EFI_SUCCESS        At least the base block was read.
  @retval EFI_NO_MEDIA       No sink responded after all retries.
  @retval EFI_DEVICE_ERROR   The HDMI controller did not identify correctly.
**/
EFI_STATUS
EFIAPI
A733HdmiDdcReadEdid (
  OUT UINT8  *Buffer,
  IN  UINTN  BufferSize,
  OUT UINTN  *BytesRead
  );

#endif // A733_HDMI_DDC_LIB_H_
```

- [ ] **Step 3: Implement**

`Library/A733HdmiDdcLib/HdmiDdc.c`:

```c
/** @file
  EDID transport over the DesignWare HDMI I2C master.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/A733HdmiDdcLib.h>
#include "DwHdmiI2cm.h"

#define DDC_RETRIES          5
#define DDC_BYTE_TIMEOUT_US  10000

STATIC
UINT8
HdmiRead8 (
  IN UINTN  Offset
  )
{
  return MmioRead8 ((UINTN)(A733_HDMI_BASE + Offset));
}

STATIC
VOID
HdmiWrite8 (
  IN UINTN  Offset,
  IN UINT8  Value
  )
{
  MmioWrite8 ((UINTN)(A733_HDMI_BASE + Offset), Value);
}

STATIC
BOOLEAN
ControllerIsPresent (
  VOID
  )
{
  UINT8  Design;
  UINT8  Product0;
  UINT8  Product1;

  Design   = HdmiRead8 (DW_HDMI_DESIGN_ID);
  Product0 = HdmiRead8 (DW_HDMI_PRODUCT_ID0);
  Product1 = HdmiRead8 (DW_HDMI_PRODUCT_ID1);

  DEBUG ((
    DEBUG_INFO,
    "A733HdmiDdc: DESIGN_ID=0x%02x PRODUCT_ID=0x%02x/0x%02x\n",
    Design,
    Product0,
    Product1
    ));

  return (BOOLEAN)((Design == DW_HDMI_DESIGN_ID_VALUE) &&
                   (Product0 == DW_HDMI_PRODUCT_ID0_VALUE) &&
                   (Product1 == DW_HDMI_PRODUCT_ID1_VALUE));
}

STATIC
EFI_STATUS
DdcReadByte (
  IN  UINT8  Segment,
  IN  UINT8  Offset,
  OUT UINT8  *Value
  )
{
  UINTN  Elapsed;
  UINT8  Status;

  // Clear any latched status before starting.
  HdmiWrite8 (DW_HDMI_IH_I2CM_STAT0, (UINT8)(I2CM_STAT_DONE | I2CM_STAT_ERROR));

  HdmiWrite8 (DW_HDMI_I2CM_SLAVE, EDID_I2C_ADDR);
  HdmiWrite8 (DW_HDMI_I2CM_SEGADDR, EDID_SEGMENT_ADDR);
  HdmiWrite8 (DW_HDMI_I2CM_SEGPTR, Segment);
  HdmiWrite8 (DW_HDMI_I2CM_ADDRESS, Offset);

  //
  // The controller issues the repeated START itself. Driving the address and
  // the read as two separate transactions is what makes an EDID EEPROM reset
  // its address pointer and hand back block 0 forever.
  //
  HdmiWrite8 (
    DW_HDMI_I2CM_OPERATION,
    (UINT8)((Segment != 0) ? I2CM_OP_READ_EXT : I2CM_OP_READ)
    );

  for (Elapsed = 0; Elapsed < DDC_BYTE_TIMEOUT_US; Elapsed += 10) {
    Status = HdmiRead8 (DW_HDMI_IH_I2CM_STAT0);

    if ((Status & I2CM_STAT_ERROR) != 0) {
      HdmiWrite8 (DW_HDMI_IH_I2CM_STAT0, I2CM_STAT_ERROR);
      return EFI_DEVICE_ERROR;
    }

    if ((Status & I2CM_STAT_DONE) != 0) {
      HdmiWrite8 (DW_HDMI_IH_I2CM_STAT0, I2CM_STAT_DONE);
      *Value = HdmiRead8 (DW_HDMI_I2CM_DATAI);
      return EFI_SUCCESS;
    }

    MicroSecondDelay (10);
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
ReadBlock (
  IN  UINTN  BlockIndex,
  OUT UINT8  *Block
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  UINT8       Segment;
  UINT8       Offset;

  Segment = (UINT8)(BlockIndex / 2);

  for (Index = 0; Index < A733_EDID_BLOCK_SIZE_LOCAL; Index++) {
    Offset = (UINT8)(((BlockIndex % 2) * A733_EDID_BLOCK_SIZE_LOCAL) + Index);

    Status = DdcReadByte (Segment, Offset, &Block[Index]);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
A733HdmiDdcReadEdid (
  OUT UINT8  *Buffer,
  IN  UINTN  BufferSize,
  OUT UINTN  *BytesRead
  )
{
  EFI_STATUS  Status;
  UINTN       Attempt;
  UINTN       Blocks;
  UINTN       Index;
  UINT32      DelayUs;

  if ((Buffer == NULL) || (BytesRead == NULL) ||
      (BufferSize < A733_EDID_BLOCK_SIZE_LOCAL))
  {
    return EFI_INVALID_PARAMETER;
  }

  *BytesRead = 0;

  if (!ControllerIsPresent ()) {
    DEBUG ((DEBUG_ERROR, "A733HdmiDdc: DWC HDMI TX not identified\n"));
    return EFI_DEVICE_ERROR;
  }

  // Reset the I2C master and select standard-speed division.
  HdmiWrite8 (DW_HDMI_I2CM_SOFTRSTZ, 0x00);
  MicroSecondDelay (1000);
  HdmiWrite8 (DW_HDMI_I2CM_DIV, 0x00);
  HdmiWrite8 (DW_HDMI_IH_MUTE_I2CM_STAT0, 0x00);

  DelayUs = 20000;

  for (Attempt = 0; Attempt < DDC_RETRIES; Attempt++) {
    Status = ReadBlock (0, Buffer);

    if (!EFI_ERROR (Status)) {
      *BytesRead = A733_EDID_BLOCK_SIZE_LOCAL;

      Blocks = (UINTN)Buffer[126] + 1;
      if (Blocks > (BufferSize / A733_EDID_BLOCK_SIZE_LOCAL)) {
        Blocks = BufferSize / A733_EDID_BLOCK_SIZE_LOCAL;
      }

      //
      // Extension reads are best-effort. Byte 126 is a claim: if the sink
      // cannot actually supply the block, keep the base block rather than
      // failing the whole read.
      //
      for (Index = 1; Index < Blocks; Index++) {
        if (EFI_ERROR (ReadBlock (Index, Buffer + (Index * A733_EDID_BLOCK_SIZE_LOCAL)))) {
          DEBUG ((DEBUG_WARN, "A733HdmiDdc: extension %u unreadable, keeping base block\n", (UINT32)Index));
          break;
        }

        *BytesRead += A733_EDID_BLOCK_SIZE_LOCAL;
      }

      return EFI_SUCCESS;
    }

    DEBUG ((
      DEBUG_WARN,
      "A733HdmiDdc: attempt %u failed (%r), retrying in %u ms\n",
      (UINT32)(Attempt + 1),
      Status,
      DelayUs / 1000
      ));

    MicroSecondDelay (DelayUs);
    DelayUs *= 2;
  }

  return EFI_NO_MEDIA;
}
```

Add near the top of `HdmiDdc.c`, after the includes:

```c
#define A733_EDID_BLOCK_SIZE_LOCAL  128
```

- [ ] **Step 4: Create the library INF**

`Library/A733HdmiDdcLib/A733HdmiDdcLib.inf`:

```ini
## @file
#  EDID transport over the DesignWare HDMI I2C master.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  INF_VERSION    = 0x00010005
  BASE_NAME      = A733HdmiDdcLib
  FILE_GUID      = 2B7C4E19-6D03-4A85-BF12-70E9A3C5D648
  MODULE_TYPE    = BASE
  VERSION_STRING = 1.0
  LIBRARY_CLASS  = A733HdmiDdcLib

[Sources]
  HdmiDdc.c
  DwHdmiI2cm.h

[Packages]
  MdePkg/MdePkg.dec
  Platform/OrangePi/OrangePi4ProPkg/OrangePi4ProPkg.dec

[LibraryClasses]
  BaseLib
  BaseMemoryLib
  DebugLib
  IoLib
  TimerLib
```

- [ ] **Step 5: Declare the library class**

Add to `OrangePi4ProPkg.dec` under `[LibraryClasses]`:

```ini
  ##  @libraryclass  EDID transport over the HDMI DDC channel.
  A733HdmiDdcLib|Include/Library/A733HdmiDdcLib.h
```

- [ ] **Step 6: Verify it compiles for the target**

```bash
cd ~/edk2 && . edksetup.sh BaseTools && \
  build -p Platform/OrangePi/OrangePi4ProPkg/OrangePi4Pro.dsc -a AARCH64 -t GCC -b DEBUG
```

Expected: build SUCCEEDS. The library is not yet referenced by any component, so this only proves it parses and the tree is intact.

- [ ] **Step 7: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg
git commit -m "feat(hdmi): EDID transport over the DesignWare I2C master

Uses 8-bit register accesses throughout -- the DWC register file is
byte-addressable, and word reads return 21212121 and make a live controller
look dead.

Segment pointer and repeated-START are handled by the controller, and reads
retry with backoff because the observed boot failure on this board was the
sink not being ready rather than anything malformed."
```

---

### Task 6: Diagnostic driver — report what the display supports

The visible deliverable: a boot-time serial report of the attached display.

**Files:**
- Create: `Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/A733DisplayInfoDxe.inf`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/DisplayInfo.c`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/OrangePi4Pro.dsc`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/OrangePi4Pro.fdf`

**Interfaces:**
- Consumes: `A733HdmiDdcReadEdid` (Task 5), `A733EdidParse` and `A733EdidSelectBootMode` (Task 4).
- Produces: serial output only. No protocol.

- [ ] **Step 1: Write the driver**

`Drivers/A733DisplayInfoDxe/DisplayInfo.c`:

```c
/** @file
  Report the attached display's EDID and supported modes over serial.

  Diagnostic only: reads nothing but the DDC channel and writes no display
  register, so it cannot disturb a working screen. It exists because until now
  there has been no way to find out what a board thinks is attached without
  booting all the way into Linux.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/A733EdidLib.h>
#include <Library/A733HdmiDdcLib.h>

#define EDID_BUFFER_SIZE  256

STATIC
VOID
DecodeManufacturer (
  IN  UINT16  Id,
  OUT CHAR8   *Out
  )
{
  Out[0] = (CHAR8)(((Id >> 10) & 0x1F) + 'A' - 1);
  Out[1] = (CHAR8)(((Id >> 5) & 0x1F) + 'A' - 1);
  Out[2] = (CHAR8)((Id & 0x1F) + 'A' - 1);
  Out[3] = '\0';
}

EFI_STATUS
EFIAPI
A733DisplayInfoEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                 Status;
  UINT8                      Edid[EDID_BUFFER_SIZE];
  UINTN                      BytesRead;
  A733_EDID_INFO             Info;
  CONST A733_DISPLAY_TIMING  *Boot;
  CHAR8                      Mfr[4];
  UINTN                      Index;

  ZeroMem (Edid, sizeof (Edid));

  Status = A733HdmiDdcReadEdid (Edid, sizeof (Edid), &BytesRead);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "A733DisplayInfo: EDID read failed: %r\n", Status));
    return EFI_SUCCESS;          // diagnostic only; never fail the boot
  }

  DEBUG ((DEBUG_INFO, "A733DisplayInfo: read %u EDID bytes\n", (UINT32)BytesRead));

  Status = A733EdidParse (Edid, BytesRead, &Info);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "A733DisplayInfo: EDID parse failed: %r\n", Status));
    return EFI_SUCCESS;
  }

  DecodeManufacturer (Info.ManufacturerId, Mfr);

  DEBUG ((
    DEBUG_INFO,
    "A733DisplayInfo: %a %a (0x%04x), %u modes, 1080p=%a\n",
    Mfr,
    Info.MonitorName,
    Info.ProductCode,
    (UINT32)Info.ModeCount,
    Info.Has1080p ? "yes" : "no"
    ));

  if (Info.MaxPixelClockHz != 0) {
    DEBUG ((
      DEBUG_INFO,
      "A733DisplayInfo: declared max pixel clock %u MHz (advisory only)\n",
      Info.MaxPixelClockHz / 1000000
      ));
  }

  for (Index = 0; Index < Info.ModeCount; Index++) {
    DEBUG ((
      DEBUG_INFO,
      "A733DisplayInfo:   %4ux%-4u %s %u.%02u MHz  htotal=%u vtotal=%u\n",
      Info.Modes[Index].HActive,
      Info.Modes[Index].VActive,
      Info.Modes[Index].Interlaced ? L"i" : L"p",
      Info.Modes[Index].PixelClockHz / 1000000,
      (Info.Modes[Index].PixelClockHz % 1000000) / 10000,
      Info.Modes[Index].HActive + Info.Modes[Index].HBlank,
      Info.Modes[Index].VActive + Info.Modes[Index].VBlank
      ));
  }

  Boot = A733EdidSelectBootMode (&Info);
  if (Boot != NULL) {
    DEBUG ((
      DEBUG_INFO,
      "A733DisplayInfo: would select %ux%u @ %u.%02u MHz\n",
      Boot->HActive,
      Boot->VActive,
      Boot->PixelClockHz / 1000000,
      (Boot->PixelClockHz % 1000000) / 10000
      ));
  }

  return EFI_SUCCESS;
}
```

- [ ] **Step 2: Create the driver INF**

`Drivers/A733DisplayInfoDxe/A733DisplayInfoDxe.inf`:

```ini
## @file
#  Diagnostic: report the attached display's EDID over serial.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  INF_VERSION    = 0x00010005
  BASE_NAME      = A733DisplayInfoDxe
  FILE_GUID      = 9A34D712-C58E-4F60-B7A2-1D0E64835CF9
  MODULE_TYPE    = DXE_DRIVER
  VERSION_STRING = 1.0
  ENTRY_POINT    = A733DisplayInfoEntry

[Sources]
  DisplayInfo.c

[Packages]
  MdePkg/MdePkg.dec
  Platform/OrangePi/OrangePi4ProPkg/OrangePi4ProPkg.dec

[LibraryClasses]
  A733EdidLib
  A733HdmiDdcLib
  BaseLib
  BaseMemoryLib
  DebugLib
  UefiBootServicesTableLib
  UefiDriverEntryPoint

[Depex]
  TRUE
```

- [ ] **Step 3: Wire into the platform DSC**

In `OrangePi4Pro.dsc`, add to `[LibraryClasses.common]`:

```ini
  A733EdidLib|Platform/OrangePi/OrangePi4ProPkg/Library/A733EdidLib/A733EdidLib.inf
  A733HdmiDdcLib|Platform/OrangePi/OrangePi4ProPkg/Library/A733HdmiDdcLib/A733HdmiDdcLib.inf
```

and add to `[Components.common]`, next to the existing `SunxiSimpleFbGopDxe.inf` line:

```ini
  Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/A733DisplayInfoDxe.inf
```

- [ ] **Step 4: Wire into the FDF**

In `OrangePi4Pro.fdf`, next to the existing `SunxiSimpleFbGopDxe.inf` entry:

```ini
  INF Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/A733DisplayInfoDxe.inf
```

- [ ] **Step 5: Build**

```bash
cd ~/edk2 && ./build_edk2.sh
```

Expected: build SUCCEEDS.

- [ ] **Step 6: Verify UART before deploying**

The board's UART7 is `/dev/ttyS7`; the host adapter is **COM20** (not COM25 — older scripts have this wrong). Confirm the link carries traffic before relying on it:

```bash
sshpass -p orangepi ssh -o StrictHostKeyChecking=no orangepi@192.168.0.201 \
  'bash /home/orangepi/uarttest.sh'
```

While that runs, read COM20 on the host. Expect `UART7-LOOPBACK-OK-<n>` markers.
If nothing arrives, stop — do not deploy and guess. The adapter has been observed
enumerating, working, then dropping off the USB bus with no error logged.

- [ ] **Step 7: Deploy and capture the boot log**

```bash
cd ~/edk2 && ./build_edk2.sh --deploy
```

Then reboot into EDK2 and capture COM20. Expected serial output:

```
A733HdmiDdc: DESIGN_ID=0x21 PRODUCT_ID=0xa0/0xc1
A733DisplayInfo: read 256 EDID bytes
A733DisplayInfo: MPI MPI7010 (0x7010), N modes, 1080p=yes
A733DisplayInfo: declared max pixel clock 140 MHz (advisory only)
A733DisplayInfo:   1024x600  p 51.20 MHz  htotal=1344 vtotal=635
A733DisplayInfo:   1920x1080 p 148.50 MHz  htotal=2200 vtotal=1125
A733DisplayInfo:   1280x720  p 74.25 MHz  htotal=1650 vtotal=750
A733DisplayInfo:    720x480  p 27.00 MHz  htotal=858 vtotal=525
A733DisplayInfo: would select 1920x1080 @ 148.50 MHz
```

**This is the acceptance test for the whole plan.** If the firmware reports the
same timings that Linux reports over DDC, the parser and the transport are both
correct and Plan 2 can build on them.

If the EDID read fails, capture the retry lines and check whether the sink was
simply not ready — that was the boot-time failure mode observed under Linux.

- [ ] **Step 8: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg
git commit -m "feat(display): report attached display's EDID and modes at boot

First time this firmware can say what monitor is attached and what it supports,
without booting into Linux to find out.

Reads only the DDC channel and writes no display register, so it cannot disturb
a working screen."
```

---

### Task 7: Fix the boot logo so it never silently fails to draw

The logo is 1024x600, which does not fit the 640x480 fallback mode. Today that would fail silently.

**Files:**
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Library/PlatformBootManagerLib/PlatformBootManagerLib.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: nothing consumed later.

- [ ] **Step 1: Find the call site**

```bash
grep -n "BootLogoEnableLogo" \
  ~/edk2-a733/Platform/OrangePi/OrangePi4ProPkg/Library/PlatformBootManagerLib/PlatformBootManagerLib.c
```

- [ ] **Step 2: Guard the call**

Replace the bare `BootLogoEnableLogo ();` call with a version that checks the
logo fits the active mode first:

```c
  //
  // Draw the boot logo, but only if it fits the mode that is actually active.
  //
  // The logo is 1024x600. On the 640x480 fallback it does not fit, and
  // BootLogoEnableLogo would fail without saying why -- leaving a blank screen
  // at exactly the moment someone most needs to see that the firmware is alive.
  //
  {
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
    EFI_STATUS                    LogoStatus;

    Gop        = NULL;
    LogoStatus = gBS->HandleProtocol (
                        gST->ConsoleOutHandle,
                        &gEfiGraphicsOutputProtocolGuid,
                        (VOID **)&Gop
                        );

    if (!EFI_ERROR (LogoStatus) && (Gop != NULL) && (Gop->Mode != NULL) &&
        (Gop->Mode->Info != NULL))
    {
      if ((Gop->Mode->Info->HorizontalResolution >= 1024) &&
          (Gop->Mode->Info->VerticalResolution >= 600))
      {
        BootLogoEnableLogo ();
      } else {
        DEBUG ((
          DEBUG_INFO,
          "PlatformBds: skipping boot logo, mode %ux%u is smaller than the "
          "1024x600 artwork\n",
          Gop->Mode->Info->HorizontalResolution,
          Gop->Mode->Info->VerticalResolution
          ));
      }
    } else {
      // No GOP to interrogate: try anyway rather than never drawing it.
      BootLogoEnableLogo ();
    }
  }
```

- [ ] **Step 3: Build**

```bash
cd ~/edk2 && ./build_edk2.sh
```

Expected: SUCCEEDS.

- [ ] **Step 4: Verify the logo still draws at 1024x600**

Deploy, boot, and confirm the logo appears as before. The active mode is
1024x600, which passes the guard, so behaviour must be unchanged.

- [ ] **Step 5: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg/Library/PlatformBootManagerLib/PlatformBootManagerLib.c
git commit -m "fix(bds): skip the boot logo when it does not fit the active mode

The artwork is 1024x600 and will not draw on the 640x480 fallback. Previously
that failed silently, leaving a blank screen precisely when someone most needs
to see the firmware is alive."
```

---

---

### Task 8: Display Information page in Setup

Puts what the firmware detected on screen, under Device Manager, where it can be read without a serial adapter.

This matters more than a convenience feature. Task 6 reports the same information over UART, and the serial adapter on this bench cycles — works for minutes, drops, recovers. A tester with no adapter at all, which is most of them, currently has no way to find out what the firmware thinks is attached. This page is that way.

Read-only in this task. A mode *selector* needs the working modeset from Plan 2 and a persistent variable store from the SPI NOR work, so it is deliberately out of scope here.

Note that the Boot Maintenance Manager's console-configuration form already enumerates modes from `EFI_GRAPHICS_OUTPUT_PROTOCOL`, and will populate itself once Plan 2 publishes a real multi-mode GOP. No work is needed for that; it is empty today only because the GOP reports `MaxMode = 1`.

**Files:**
- Create: `Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/DisplayInfoHii.vfr`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/DisplayInfoStrings.uni`
- Create: `Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/DisplayInfoHii.h`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/DisplayInfo.c`
- Modify: `Platform/OrangePi/OrangePi4ProPkg/Drivers/A733DisplayInfoDxe/A733DisplayInfoDxe.inf`

**Interfaces:**
- Consumes: `A733_EDID_INFO`, `A733EdidParse`, `A733EdidSelectBootMode` (Task 4); `A733HdmiDdcReadEdid` (Task 5); the serial-reporting entry point from Task 6.
- Produces: an HII formset visible under Device Manager. Nothing later consumes it.

- [ ] **Step 1: Create the header with the GUIDs and string-id budget**

`Drivers/A733DisplayInfoDxe/DisplayInfoHii.h`:

```c
/** @file
  HII identifiers for the Display Information page.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef DISPLAY_INFO_HII_H_
#define DISPLAY_INFO_HII_H_

#define DISPLAY_INFO_FORMSET_GUID \
  { 0x4E8A1B37, 0x92D6, 0x4C05, { 0xB1, 0x7F, 0x3A, 0x62, 0xC9, 0x80, 0x5D, 0x14 } }

#define DISPLAY_INFO_FORM_ID  0x0001

//
// Eight mode rows is enough for every sink we have measured and keeps the form
// on one screen. Sinks advertising more get the first eight plus a count.
//
#define DISPLAY_INFO_MAX_MODE_ROWS  8

#endif // DISPLAY_INFO_HII_H_
```

- [ ] **Step 2: Create the string package**

`Drivers/A733DisplayInfoDxe/DisplayInfoStrings.uni`:

```
// SPDX-License-Identifier: BSD-2-Clause-Patent
/=#

#langdef en-US "English"

#string STR_FORM_TITLE          #language en-US "Display Information"
#string STR_FORM_HELP           #language en-US "What the firmware detected on the HDMI output."

#string STR_STATUS_PROMPT       #language en-US "Status"
#string STR_STATUS_VALUE        #language en-US "unknown"

#string STR_MONITOR_PROMPT      #language en-US "Monitor"
#string STR_MONITOR_VALUE       #language en-US "unknown"

#string STR_EDID_PROMPT         #language en-US "EDID"
#string STR_EDID_VALUE          #language en-US "unknown"

#string STR_MAXCLK_PROMPT       #language en-US "Declared max pixel clock"
#string STR_MAXCLK_VALUE        #language en-US "unknown"

#string STR_SELECTED_PROMPT     #language en-US "Mode that would be selected"
#string STR_SELECTED_VALUE      #language en-US "unknown"

#string STR_MODES_PROMPT        #language en-US "Supported modes"
#string STR_MODE_0              #language en-US ""
#string STR_MODE_1              #language en-US ""
#string STR_MODE_2              #language en-US ""
#string STR_MODE_3              #language en-US ""
#string STR_MODE_4              #language en-US ""
#string STR_MODE_5              #language en-US ""
#string STR_MODE_6              #language en-US ""
#string STR_MODE_7              #language en-US ""

#string STR_EMPTY               #language en-US ""
```

- [ ] **Step 3: Create the form**

`Drivers/A733DisplayInfoDxe/DisplayInfoHii.vfr`:

```c
// SPDX-License-Identifier: BSD-2-Clause-Patent

#include "DisplayInfoHii.h"

formset
  guid      = DISPLAY_INFO_FORMSET_GUID,
  title     = STRING_TOKEN(STR_FORM_TITLE),
  help      = STRING_TOKEN(STR_FORM_HELP),
  classguid = gEfiHiiPlatformSetupFormsetGuid,

  form formid = DISPLAY_INFO_FORM_ID,
       title  = STRING_TOKEN(STR_FORM_TITLE);

    text
      help   = STRING_TOKEN(STR_EMPTY),
      text   = STRING_TOKEN(STR_STATUS_PROMPT),
      text   = STRING_TOKEN(STR_STATUS_VALUE);

    text
      help   = STRING_TOKEN(STR_EMPTY),
      text   = STRING_TOKEN(STR_MONITOR_PROMPT),
      text   = STRING_TOKEN(STR_MONITOR_VALUE);

    text
      help   = STRING_TOKEN(STR_EMPTY),
      text   = STRING_TOKEN(STR_EDID_PROMPT),
      text   = STRING_TOKEN(STR_EDID_VALUE);

    text
      help   = STRING_TOKEN(STR_EMPTY),
      text   = STRING_TOKEN(STR_MAXCLK_PROMPT),
      text   = STRING_TOKEN(STR_MAXCLK_VALUE);

    text
      help   = STRING_TOKEN(STR_EMPTY),
      text   = STRING_TOKEN(STR_SELECTED_PROMPT),
      text   = STRING_TOKEN(STR_SELECTED_VALUE);

    subtitle text = STRING_TOKEN(STR_EMPTY);
    subtitle text = STRING_TOKEN(STR_MODES_PROMPT);

    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_0);
    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_1);
    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_2);
    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_3);
    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_4);
    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_5);
    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_6);
    text help = STRING_TOKEN(STR_EMPTY), text = STRING_TOKEN(STR_MODE_7);

  endform;

endformset;
```

- [ ] **Step 4: Publish the package and fill it in**

Add to `DisplayInfo.c`, and call `PublishDisplayInfoForm (&Info, Status)` from the entry point after parsing. Include `DisplayInfoHii.h`, `<Library/HiiLib.h>`, `<Library/PrintLib.h>`, `<Library/MemoryAllocationLib.h>`, and `<Guid/MdeModuleHii.h>`:

```c
extern UINT8  DisplayInfoHiiBin[];
extern UINT8  A733DisplayInfoDxeStrings[];

STATIC EFI_HII_HANDLE  mHiiHandle = NULL;

STATIC CONST EFI_GUID  mFormsetGuid = DISPLAY_INFO_FORMSET_GUID;

/**
  Replace one string token's contents at runtime.

  The form is static; the text in it is not. Every value on the page is a
  placeholder token that gets overwritten here once the EDID has been read.
**/
STATIC
VOID
SetStr (
  IN EFI_STRING_ID  Token,
  IN CONST CHAR16   *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR16   Buffer[128];

  VA_START (Marker, Format);
  UnicodeVSPrint (Buffer, sizeof (Buffer), Format, Marker);
  VA_END (Marker);

  HiiSetString (mHiiHandle, Token, Buffer, NULL);
}

STATIC
VOID
PublishDisplayInfoForm (
  IN CONST A733_EDID_INFO  *Info,
  IN EFI_STATUS            ReadStatus
  )
{
  UINTN                      Index;
  CHAR16                     Mfr[4];
  CONST A733_DISPLAY_TIMING  *Boot;
  EFI_STRING_ID              ModeTokens[DISPLAY_INFO_MAX_MODE_ROWS];

  ModeTokens[0] = STRING_TOKEN (STR_MODE_0);
  ModeTokens[1] = STRING_TOKEN (STR_MODE_1);
  ModeTokens[2] = STRING_TOKEN (STR_MODE_2);
  ModeTokens[3] = STRING_TOKEN (STR_MODE_3);
  ModeTokens[4] = STRING_TOKEN (STR_MODE_4);
  ModeTokens[5] = STRING_TOKEN (STR_MODE_5);
  ModeTokens[6] = STRING_TOKEN (STR_MODE_6);
  ModeTokens[7] = STRING_TOKEN (STR_MODE_7);

  mHiiHandle = HiiAddPackages (
                 &mFormsetGuid,
                 gImageHandle,
                 A733DisplayInfoDxeStrings,
                 DisplayInfoHiiBin,
                 NULL
                 );
  if (mHiiHandle == NULL) {
    DEBUG ((DEBUG_ERROR, "A733DisplayInfo: HiiAddPackages failed\n"));
    return;
  }

  if (EFI_ERROR (ReadStatus)) {
    //
    // Say why, on screen. "No display detected" with a reason is diagnosable;
    // a blank page is not.
    //
    SetStr (STRING_TOKEN (STR_STATUS_VALUE), L"no EDID (%r)", ReadStatus);
    return;
  }

  SetStr (STRING_TOKEN (STR_STATUS_VALUE), L"display detected");

  Mfr[0] = (CHAR16)(((Info->ManufacturerId >> 10) & 0x1F) + L'A' - 1);
  Mfr[1] = (CHAR16)(((Info->ManufacturerId >> 5) & 0x1F) + L'A' - 1);
  Mfr[2] = (CHAR16)((Info->ManufacturerId & 0x1F) + L'A' - 1);
  Mfr[3] = L'\0';

  SetStr (
    STRING_TOKEN (STR_MONITOR_VALUE),
    L"%s %a (0x%04x)",
    Mfr,
    Info->MonitorName,
    Info->ProductCode
    );

  SetStr (STRING_TOKEN (STR_EDID_VALUE), L"%u modes parsed", (UINT32)Info->ModeCount);

  if (Info->MaxPixelClockHz != 0) {
    //
    // Labelled advisory on purpose: this sink declares 140 MHz and then
    // supplies a 148.5 MHz 1080p timing. Someone reading the page should know
    // the number is not a limit we honour.
    //
    SetStr (
      STRING_TOKEN (STR_MAXCLK_VALUE),
      L"%u MHz (advisory, not enforced)",
      Info->MaxPixelClockHz / 1000000
      );
  } else {
    SetStr (STRING_TOKEN (STR_MAXCLK_VALUE), L"not declared");
  }

  Boot = A733EdidSelectBootMode (Info);
  if (Boot != NULL) {
    SetStr (
      STRING_TOKEN (STR_SELECTED_VALUE),
      L"%ux%u @ %u.%02u MHz",
      Boot->HActive,
      Boot->VActive,
      Boot->PixelClockHz / 1000000,
      (Boot->PixelClockHz % 1000000) / 10000
      );
  }

  for (Index = 0; Index < DISPLAY_INFO_MAX_MODE_ROWS; Index++) {
    if (Index >= Info->ModeCount) {
      SetStr (ModeTokens[Index], L"");
      continue;
    }

    SetStr (
      ModeTokens[Index],
      L"  %ux%u%s  %u.%02u MHz",
      Info->Modes[Index].HActive,
      Info->Modes[Index].VActive,
      Info->Modes[Index].Interlaced ? L"i" : L"p",
      Info->Modes[Index].PixelClockHz / 1000000,
      (Info->Modes[Index].PixelClockHz % 1000000) / 10000
      );
  }

  if (Info->ModeCount > DISPLAY_INFO_MAX_MODE_ROWS) {
    SetStr (
      ModeTokens[DISPLAY_INFO_MAX_MODE_ROWS - 1],
      L"  ... and %u more",
      (UINT32)(Info->ModeCount - DISPLAY_INFO_MAX_MODE_ROWS + 1)
      );
  }
}
```

Also change the two early returns in `A733DisplayInfoEntry` so a failed read still publishes the form with its reason, rather than returning silently:

```c
  Status = A733HdmiDdcReadEdid (Edid, sizeof (Edid), &BytesRead);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "A733DisplayInfo: EDID read failed: %r\n", Status));
    ZeroMem (&Info, sizeof (Info));
    PublishDisplayInfoForm (&Info, Status);
    return EFI_SUCCESS;
  }
```

and likewise for the parse failure, passing the parse status.

- [ ] **Step 5: Update the driver INF**

`[Sources]` gains the VFR and the string file; `[LibraryClasses]` gains the HII and print libraries; a `[Guids]` and `[Depex]` entry are required:

```ini
[Sources]
  DisplayInfo.c
  DisplayInfoHii.h
  DisplayInfoHii.vfr
  DisplayInfoStrings.uni

[LibraryClasses]
  A733EdidLib
  A733HdmiDdcLib
  BaseLib
  BaseMemoryLib
  DebugLib
  HiiLib
  MemoryAllocationLib
  PrintLib
  UefiBootServicesTableLib
  UefiDriverEntryPoint
  UefiHiiServicesLib

[Guids]
  gEfiIfrTianoGuid                              ## CONSUMES ## HII

[Depex]
  gEfiHiiDatabaseProtocolGuid AND gEfiHiiStringProtocolGuid
```

The `[Depex]` change matters: the driver now needs the HII database to exist before it runs. With `TRUE` it could load first and `HiiAddPackages` would fail.

- [ ] **Step 6: Add the required library mappings to the platform DSC**

Confirm these are present in `[LibraryClasses.common]` of `OrangePi4Pro.dsc`, adding any that are missing:

```ini
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
```

- [ ] **Step 7: Build**

```bash
cd ~/edk2 && ./build_edk2.sh
```

Expected: SUCCEEDS. A VFR compile error naming an unknown string token means a token used in the `.vfr` is missing from the `.uni`; they must match exactly.

- [ ] **Step 8: Deploy and verify on screen**

```bash
cd ~/edk2 && ./build_edk2.sh --deploy
```

Boot into EDK2, enter Setup, and go to **Device Manager**. Expect a **Display Information** entry. Open it and confirm it shows the monitor identity, the parsed mode count, the advisory pixel-clock note, the selected mode, and the mode list.

**This step needs no serial adapter** — that is the entire point of the task. Verify it by reading the screen.

Cross-check the values against what Linux reports for the same display:

```bash
sshpass -p orangepi ssh -o StrictHostKeyChecking=no orangepi@192.168.0.201 \
  'cat /sys/class/drm/card0-HDMI-A-1/modes'
```

- [ ] **Step 9: Commit**

```bash
cd ~/edk2-a733
git add Platform/OrangePi/OrangePi4ProPkg
git commit -m "feat(display): show detected display and modes in Setup

Adds a Display Information page under Device Manager reporting the attached
monitor, the modes parsed from its EDID, and the mode that would be selected.

Task 6 reports the same information over serial, which is no help to a tester
without a UART adapter -- which is most of them, and is the likeliest reason
this port has drawn few contributors. This page needs only a screen.

Read-only for now. A mode selector needs the modeset work and a persistent
variable store, neither of which exists yet."
```

## Self-Review

**Spec coverage.** Plan 1 implements the spec's `EdidParserLib` (Tasks 1-4), `A733HdmiDdc` (Task 5), and the testing strategy's host-test layer (Task 1). The mode policy is implemented in Task 4 with 1080p first. Deliberately **not** covered here, and deferred to later plans: `Ccu.c`, `De.c`, `Tcon.c`, `HdmiTx.c`, `HdmiPhy.c`, the GOP producer, and the fallback-on-failure control flow — all of which depend on the CCU pixel-clock path, which the spec lists as an open item. The logo fix (Task 7) is not in the spec; it was found while measuring the artwork and is included because it is small, related, and currently a latent silent failure.

**Placeholder scan.** No TBD/TODO markers. Every code step carries complete code. Every test step names the exact command and the expected result.

**Type consistency.** `A733_DISPLAY_TIMING` is defined once in Task 1 and used unchanged in Tasks 2, 4, 5 and 6. `A733_EDID_INFO` is defined in Task 4 and consumed in Task 6. `A733EdidBlockIsValid` is defined in Task 1 and called from `EdidCea.c` in Task 3 and `EdidModes.c` in Task 4 — both include `EdidInternal.h`, which includes the public header, so the declaration is visible. `A733_EDID_BLOCK_SIZE` is public; `A733HdmiDdcLib` deliberately defines its own `A733_EDID_BLOCK_SIZE_LOCAL` rather than depending on the parser library.

## Known risks

- **Task 5 is the first speculative code in this plan.** The DWC I2CM register offsets follow the standard Synopsys map, and the identification registers are confirmed on hardware, but the exact `I2CM` offsets on this integration are unverified. If `ControllerIsPresent` passes and the reads time out, dump the `0x7E00` region with 8-bit accesses from Linux and compare against the assumed map before changing the driver.
- **UART reliability gates Task 6.** The adapter has been observed dropping off the USB bus with nothing logged. Step 6 of Task 6 exists to catch that before it is mistaken for a firmware fault.
