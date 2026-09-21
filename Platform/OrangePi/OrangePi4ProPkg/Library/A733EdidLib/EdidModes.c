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
