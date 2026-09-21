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
  UT_ASSERT_NOT_NULL ((VOID *)Mode);
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
  AddTestCase (Suite, "Preferred DTD decodes to 1024x600", "DtdPreferred", TestParseDtdPreferred, NULL, NULL, NULL);
  AddTestCase (Suite, "CEA DTD decodes to 1080p60", "Dtd1080p", TestParseDtd1080p, NULL, NULL, NULL);
  AddTestCase (Suite, "Display descriptor is not a timing", "DtdReject", TestParseDtdRejectsDisplayDescriptor, NULL, NULL, NULL);
  AddTestCase (Suite, "CEA extension yields expected VICs", "Cea", TestCeaExtension, NULL, NULL, NULL);
  AddTestCase (Suite, "Non-CEA block is rejected", "CeaReject", TestCeaRejectsNonCea, NULL, NULL, NULL);
  AddTestCase (Suite, "Full parse extracts identity and modes", "ParseFull", TestParseFull, NULL, NULL, NULL);
  AddTestCase (Suite, "Range limits never veto 1080p", "NoVeto", TestRangeLimitsDoNotVeto1080p, NULL, NULL, NULL);
  AddTestCase (Suite, "Bad extension keeps base block modes", "BadExt", TestBadExtensionKeepsBaseBlock, NULL, NULL, NULL);
  AddTestCase (Suite, "Lying extension count is safe", "ExtLie", TestExtensionCountLiesAreHarmless, NULL, NULL, NULL);

  Status = RunAllTestSuites (Fw);
  FreeUnitTestFramework (Fw);
  return EFI_ERROR (Status) ? 1 : 0;
}
