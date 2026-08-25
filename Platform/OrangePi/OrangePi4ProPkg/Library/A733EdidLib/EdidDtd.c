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
