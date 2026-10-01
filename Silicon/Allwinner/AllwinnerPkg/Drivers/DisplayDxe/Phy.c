#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>

#include "Phy.h"

STATIC CONST H618_PHY_CONFIG  mPhyConfigs[] = {
  // kHz    oppll   currctrl gmp    txterm  vlev    cksymtx
  {  74250, 0x0072, 0x0013, 0x0001, 0x0006, 0x022D, 0x8009 },  // 720p60  1188/16
  { 108000, 0x0051, 0x001B, 0x0002, 0x0004, 0x0232, 0x8009 },  // SXGA60  1188/11
  { 118800, 0x0051, 0x001B, 0x0002, 0x0004, 0x0232, 0x8009 },  // 1188/10
  { 132000, 0x0051, 0x001B, 0x0002, 0x0004, 0x0232, 0x8009 },  // 1188/9
  { 148500, 0x0051, 0x0019, 0x0002, 0x0006, 0x0270, 0x8029 },  // 1080p60 1188/8
  { 198000, 0x0040, 0x0036, 0x0003, 0x0004, 0x0230, 0x8009 },  // 1188/6
  { 297000, 0x0040, 0x0019, 0x0003, 0x0005, 0x01AB, 0x8039 },  // 1188/4
};

CONST H618_PHY_CONFIG *
H618PhyGetConfig (
  IN UINT32  PixelClockKhz
  )
{
  UINTN i;

  for (i = 0; i < ARRAY_SIZE (mPhyConfigs); i++) {
    if (mPhyConfigs[i].PixelClockKhz == PixelClockKhz) {
      return &mPhyConfigs[i];
    }
  }
  return NULL;
}

STATIC
EFI_STATUS
H618PhyI2cWrite (
  IN UINT8   Address,
  IN UINT16  Data
  )
{
  UINTN  Timeout;
  UINT8  Status;

  MmioWrite8 (H618_HDMI_BASE + HDMI_PHY_I2CM_ADDRESS_REG, Address);
  MmioWrite8 (H618_HDMI_BASE + HDMI_PHY_I2CM_DATAO_1_REG, (UINT8)(Data >> 8));
  MmioWrite8 (H618_HDMI_BASE + HDMI_PHY_I2CM_DATAO_0_REG, (UINT8)(Data & 0xFF));
  MmioWrite8 (H618_HDMI_BASE + HDMI_PHY_I2CM_OPERATION_REG, HDMI_PHY_I2CM_OPERATION_WR);

  for (Timeout = 0; Timeout < 100; Timeout++) {
    MicroSecondDelay (10);
    Status = MmioRead8 (H618_HDMI_BASE + HDMI_IH_I2CMPHY_STAT0_REG) &
             (HDMI_IH_I2CMPHY_STAT0_ERROR | HDMI_IH_I2CMPHY_STAT0_DONE);
    if (Status != 0) {
      MmioWrite8 (H618_HDMI_BASE + HDMI_IH_I2CMPHY_STAT0_REG, Status);
      if ((Status & HDMI_IH_I2CMPHY_STAT0_ERROR) != 0) {
        DEBUG ((EFI_D_ERROR, "PHY-I2C write 0x%02x failed\n", Address));
        return EFI_DEVICE_ERROR;
      }
      return EFI_SUCCESS;
    }
  }

  DEBUG ((EFI_D_ERROR, "PHY-I2C write 0x%02x timed out\n", Address));
  return EFI_TIMEOUT;
}

EFI_STATUS
H618HdmiPhyInit (
  IN CONST DISPLAY_MODE  *Mode
  )
{
  CONST H618_PHY_CONFIG  *Config;
  UINTN                   Timeout;

  Config = H618PhyGetConfig (Mode->PixelClockKhz);
  if (Config == NULL) {
    DEBUG ((EFI_D_ERROR, "no PHY settings for %u kHz\n",
      Mode->PixelClockKhz));
    return EFI_UNSUPPORTED;
  }

  MmioWrite32 (H618_HDMI_PHY_BASE + PHY_REXT_CTRL_REG_OFFSET, PHY_REXT_CTRL_VALUE);

  MmioWrite8 (H618_HDMI_BASE + HDMI_MC_PHYRSTZ_REG, 0x01);
  MmioAnd8 (H618_HDMI_BASE + HDMI_PHY_CONF0_REG, (UINT8)~HDMI_PHY_CONF0_TXPWRON_MASK);
  MmioOr8  (H618_HDMI_BASE + HDMI_PHY_CONF0_REG, HDMI_PHY_CONF0_PDDQ_MASK);
  MmioAnd8 (H618_HDMI_BASE + HDMI_PHY_CONF0_REG, (UINT8)~HDMI_PHY_CONF0_SVSRET_MASK);
  MmioOr8  (H618_HDMI_BASE + HDMI_PHY_CONF0_REG, HDMI_PHY_CONF0_SVSRET_MASK);
  MmioWrite8 (H618_HDMI_BASE + HDMI_MC_PHYRSTZ_REG, 0x00);

  MmioWrite8 (H618_HDMI_BASE + HDMI_JTAG_PHY_CONFIG_REG, HDMI_JTAG_PHY_CONFIG_I2C_JTAGZ);
  MmioWrite8 (H618_HDMI_BASE + HDMI_PHY_I2CM_SLAVE_REG, HDMI_PHY_I2C_SLAVE_ADDR);

  H618PhyI2cWrite (PHY_I2C_OPMODE_PLLCFG, Config->OpModePllCfg);
  H618PhyI2cWrite (PHY_I2C_PLLCURRCTRL,   Config->PllCurrCtrl);
  H618PhyI2cWrite (PHY_I2C_PLLGMPCTRL,    Config->PllGmpCtrl);
  H618PhyI2cWrite (PHY_I2C_TXTERM,        Config->TxTerm);
  H618PhyI2cWrite (PHY_I2C_CKSYMTXCTRL,   Config->CkSymTxCtrl);
  H618PhyI2cWrite (PHY_I2C_VLEVCTRL,      Config->VlevCtrl);

  MmioAnd8 (H618_HDMI_BASE + HDMI_PHY_CONF0_REG, (UINT8)~HDMI_PHY_CONF0_PDDQ_MASK);
  MmioOr8  (H618_HDMI_BASE + HDMI_PHY_CONF0_REG, HDMI_PHY_CONF0_TXPWRON_MASK);

  for (Timeout = 0; Timeout < 100; Timeout++) {
    MicroSecondDelay (5);
    if ((MmioRead8 (H618_HDMI_BASE + HDMI_PHY_STAT0_REG) & HDMI_PHY_STAT0_TX_PHY_LOCK) != 0) {
      return EFI_SUCCESS;
    }
  }

  DEBUG ((EFI_D_ERROR, "PHY PLL did not lock at %u kHz (PHY_STAT0=0x%02x)\n",
    Mode->PixelClockKhz, MmioRead8 (H618_HDMI_BASE + HDMI_PHY_STAT0_REG)));
  return EFI_DEVICE_ERROR;
}