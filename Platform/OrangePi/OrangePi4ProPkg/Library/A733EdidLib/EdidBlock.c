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
