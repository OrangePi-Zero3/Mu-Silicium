#include "DisplayDxe.h"
#include "Phy.h"

#define H618_PLL_VIDEO0X4_KHZ  1188000

#define CCM_HDMI_MUX_PLL_VIDEO0X4    1
#define CCM_TCON_TV_MUX_PLL_VIDEO0X4 1
#define CCM_DE_MUX_PLL_PERIPH0X2  1

VOID
H618ClocksInit (
  IN CONST DISPLAY_MODE  *Mode
 );