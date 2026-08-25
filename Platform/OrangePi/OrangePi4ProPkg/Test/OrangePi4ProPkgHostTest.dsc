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
