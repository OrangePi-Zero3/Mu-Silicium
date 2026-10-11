#ifndef __EDID_H__
#define __EDID_H__

#include "DisplayDxe.h"

#define HDMI_EDID_BLOCK_SIZE  128

BOOLEAN
H618EdidReadPreferredMode (
  OUT DISPLAY_MODE  *Mode
  );

#endif
