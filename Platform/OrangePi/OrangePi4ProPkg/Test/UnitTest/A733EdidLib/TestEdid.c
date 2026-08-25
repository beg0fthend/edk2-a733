/** @file
  Host-based unit tests for A733EdidLib.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
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
