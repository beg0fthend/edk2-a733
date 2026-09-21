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
