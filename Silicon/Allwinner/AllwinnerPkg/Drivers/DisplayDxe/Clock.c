//
// ---- CCM: PLLs, module clocks, gates and resets --------------------------
//
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/ClockLib.h>

#include "Clock.h"
#include "DisplayDxe.h"

STATIC
BOOLEAN
H618ClockSolve (
  IN  UINT32  PixelClockKhz,
  OUT UINT32  *DivN,
  OUT UINT32  *DivM
  )
{
  UINT32 Total;

  if (PixelClockKhz == 0) {
    return FALSE;
  }

  if ((H618_PLL_VIDEO0X4_KHZ % PixelClockKhz) != 0) {
    return FALSE;
  }

  Total = H618_PLL_VIDEO0X4_KHZ / PixelClockKhz;

  if ((Total < 1) || (Total > 16)) {
    return FALSE;
  }

  *DivN = 0;
  *DivM = Total - 1;
  return TRUE;
}

BOOLEAN
H618DisplayClockSupported (
  IN UINT32  PixelClockKhz
  )
{
  UINT32 N, M;

  return (BOOLEAN)(H618ClockSolve (PixelClockKhz, &N, &M) &&
                   (H618PhyGetConfig (PixelClockKhz) != NULL));
}

VOID
H618ClocksInit (
  IN CONST DISPLAY_MODE  *Mode
  )
{
  UINT32 DivN = 0;
  UINT32 DivM = 0;
 
  CcmPllEnableAndLock (CCM_PLL_VIDEO0_REG);
  CcmPllEnableAndLock (CCM_PLL_DE_REG);
 
  CcmBusEnable (CLK_BUS_DE, RST_BUS_DE);
  CcmClkConfigure (CLK_DE, CCM_DE_MUX_PLL_PERIPH0X2, 1, 0);
  MicroSecondDelay (100);
 
  CcmBusEnable (CLK_BUS_TCON_TOP, RST_BUS_TCON_TOP);
  CcmBusEnable (CLK_BUS_TCON_TV0, RST_BUS_TCON_TV0);

  CcmBusEnable (CLK_BUS_HDMI, RST_BUS_HDMI);
  CcmResetPulse (RST_BUS_HDMI_SUB);
 
  CcmClkEnable (CLK_HDMI_SLOW);
  MicroSecondDelay (20);
 
  CcmClkConfigure (CLK_HDMI, CCM_HDMI_MUX_PLL_VIDEO0X4, 15, 0);
  MicroSecondDelay (50);
 
  if (!H618ClockSolve (Mode->PixelClockKhz, &DivN, &DivM)) {
    DEBUG ((DEBUG_ERROR,
      "no exact divider for %u kHz, forcing 74250 kHz\n",
      Mode->PixelClockKhz));
    DivN = 0;
    DivM = 15;
  }
 
  CcmClkConfigure (CLK_TCON_TV0, CCM_TCON_TV_MUX_PLL_VIDEO0X4, DivM, DivN);
 
  CcmClkConfigure (CLK_HDMI, CCM_HDMI_MUX_PLL_VIDEO0X4,
    (((DivM + 1) << DivN) - 1), 0);
 
  MmioWrite32 (H618_HDMI_PHY_BASE + PHY_REXT_CTRL_REG_OFFSET, PHY_REXT_CTRL_VALUE);
}