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

BOOLEAN
EFIAPI
A733EdidParseDtd (
  IN  CONST UINT8          *Descriptor,
  OUT A733_DISPLAY_TIMING  *Timing
  );

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

#endif // A733_EDID_LIB_H_
