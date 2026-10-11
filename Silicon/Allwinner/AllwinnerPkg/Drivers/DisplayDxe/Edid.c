#include "Edid.h"
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include "Clock.h"
#include "Phy.h"

STATIC
EFI_STATUS
H618HdmiDdcReadBurst (
  IN  UINT8  Offset,
  IN  UINT8  Block,
  OUT UINT8  *Data
  )
{
  UINTN  Timeout;
  UINTN  Index;
  UINT8  Status;

  MmioWrite8 (H618_HDMI_BASE + HDMI_IH_I2CM_STAT0_REG, HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_SEGADDR_REG, 0x30);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_SEGPTR_REG, Block >> 1);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_ADDRESS_REG, Offset);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_OPERATION_REG, ((Block >> 1) == 0) ? HDMI_I2CM_OPERATION_READ_SEQ : HDMI_I2CM_OPERATION_READ_SEQ_EXT);

  for (Timeout = 0; Timeout < 200; Timeout++) {
    MicroSecondDelay (10);
    Status = MmioRead8 (H618_HDMI_BASE + HDMI_IH_I2CM_STAT0_REG);

    if ((Status & (HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE)) != 0) {
      MmioWrite8 (H618_HDMI_BASE + HDMI_IH_I2CM_STAT0_REG, Status & (HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE));
      if ((Status & HDMI_IH_I2CM_STAT0_ERROR) != 0)
        return EFI_DEVICE_ERROR;

      for (Index = 0; Index < 8; Index++)
        Data[Index] = MmioRead8 (H618_HDMI_BASE + HDMI_I2CM_READ_BUFF0_REG + Index);

      return EFI_SUCCESS;
    }
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
H618HdmiReadEdidBlock (
  IN  UINT8  Block,
  OUT UINT8  *Edid
  )
{
  UINTN      Index;
  UINTN      Retry;
  UINT8      Burst[8];
  EFI_STATUS Status;

  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_DIV_REG, 0);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_SLAVE_REG, 0x50);

  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_SS_SCL_HCNT1_REG, 0);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_SS_SCL_HCNT0_REG, 221);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_SS_SCL_LCNT1_REG, 0);
  MmioWrite8 (H618_HDMI_BASE + HDMI_I2CM_SS_SCL_LCNT0_REG, 245);

  for (Index = 0; Index < HDMI_EDID_BLOCK_SIZE; Index += sizeof (Burst)) {
    for (Retry = 0; Retry < 3; Retry++) {
      Status = H618HdmiDdcReadBurst ((UINT8)(Index + ((Block & 1) * HDMI_EDID_BLOCK_SIZE)), Block, Burst);
      if (!EFI_ERROR (Status))
        break;
    }

    if (EFI_ERROR (Status))
      return Status;

    CopyMem (&Edid[Index], Burst, sizeof (Burst));
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
H618EdidBlockValid (
  IN CONST UINT8  *Edid,
  IN UINT8        Block
  )
{
  STATIC CONST UINT8  Header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
  UINTN               Index;
  UINT8               Checksum;

  if ((Block == 0) && (CompareMem (Edid, Header, sizeof (Header)) != 0))
    return FALSE;

  Checksum = 0;
  for (Index = 0; Index < HDMI_EDID_BLOCK_SIZE; Index++)
    Checksum = (UINT8)(Checksum + Edid[Index]);

  if (Checksum != 0)
    return FALSE;

  return TRUE;
}

STATIC
BOOLEAN
H618EdidGetSupportedDetailedTiming (
  IN  CONST UINT8  *Timing,
  OUT DISPLAY_MODE *Mode
  )
{
  UINT32  HActive;
  UINT32  HBlank;
  UINT32  VActive;
  UINT32  VBlank;
  UINT32  HSyncOffset;
  UINT32  HSyncWidth;
  UINT32  VSyncOffset;
  UINT32  VSyncWidth;
  UINT32  HSyncStart;
  UINT32  VSyncStart;
  UINT32  HSyncEnd;
  UINT32  VSyncEnd;
  UINT32  HTotal;
  UINT32  VTotal;
  UINT32  PixelClockKhz;

  PixelClockKhz = ((UINT32)Timing[0] | ((UINT32)Timing[1] << 8)) * 10;
  if ((PixelClockKhz == 0) || !H618DisplayClockSupported (PixelClockKhz))
    return FALSE;

  HActive      = Timing[2] | (((UINT32)Timing[4] & 0xF0) << 4);
  HBlank       = Timing[3] | (((UINT32)Timing[4] & 0x0F) << 8);
  VActive      = Timing[5] | (((UINT32)Timing[7] & 0xF0) << 4);
  VBlank       = Timing[6] | (((UINT32)Timing[7] & 0x0F) << 8);
  HSyncOffset  = Timing[8] | (((UINT32)Timing[11] & 0xC0) << 2);
  HSyncWidth   = Timing[9] | (((UINT32)Timing[11] & 0x30) << 4);
  VSyncOffset  = ((Timing[10] >> 4) & 0x0F) | (((UINT32)Timing[11] & 0x0C) << 2);
  VSyncWidth   = (Timing[10] & 0x0F) | (((UINT32)Timing[11] & 0x03) << 4);

  if ((HActive == 0) || (VActive == 0) || (HSyncWidth == 0) || (VSyncWidth == 0) || (HBlank <= HSyncOffset + HSyncWidth) || (VBlank <= VSyncOffset + VSyncWidth) || ((Timing[17] & 0x80) != 0) || ((Timing[17] & 0x18) != 0x18) || ((Timing[17] & 0x60) != 0))
    return FALSE;

  HSyncStart = HActive + HSyncOffset;
  HSyncEnd   = HSyncStart + HSyncWidth;
  HTotal     = HActive + HBlank;
  VSyncStart = VActive + VSyncOffset;
  VSyncEnd   = VSyncStart + VSyncWidth;
  VTotal     = VActive + VBlank;

  *Mode = (DISPLAY_MODE){
    .HActive       = HActive,
    .HFrontPorch   = HSyncOffset,
    .HSyncWidth    = HSyncWidth,
    .HBackPorch    = HTotal - HSyncEnd,
    .VActive       = VActive,
    .VFrontPorch   = VSyncOffset,
    .VSyncWidth    = VSyncWidth,
    .VBackPorch    = VTotal - VSyncEnd,
    .PixelClockKhz = PixelClockKhz,
    .HSyncPositive = (BOOLEAN)((Timing[17] & 0x02) != 0),
    .VSyncPositive = (BOOLEAN)((Timing[17] & 0x04) != 0)
  };
  return TRUE;
}

STATIC
BOOLEAN
H618EdidGetMode (
  IN  CONST UINT8  *Edid,
  IN  CONST UINT8  *CtaExtension OPTIONAL,
  OUT DISPLAY_MODE *Mode
  )
{
  UINTN  Index;
  UINTN  DtdEnd;

  for (Index = 54; Index < 126; Index += 18)
    if (((Edid[Index] != 0) || (Edid[Index + 1] != 0)) && H618EdidGetSupportedDetailedTiming (&Edid[Index], Mode))
      return TRUE;

  if ((CtaExtension != NULL) && (CtaExtension[0] == 0x02) && (CtaExtension[2] >= 4) && (CtaExtension[2] <= 127)) {
    DtdEnd = CtaExtension[2];
    for (Index = DtdEnd; (Index + 18) <= 127; Index += 18)
      if (((CtaExtension[Index] != 0) || (CtaExtension[Index + 1] != 0)) && H618EdidGetSupportedDetailedTiming (&CtaExtension[Index], Mode))
        return TRUE;
  }

  return FALSE;
}

BOOLEAN
H618EdidReadPreferredMode (
  OUT DISPLAY_MODE  *Mode
  )
{
  EFI_STATUS  Status;
  UINT8       Edid[HDMI_EDID_BLOCK_SIZE];
  UINT8       EdidExtension[HDMI_EDID_BLOCK_SIZE];
  CONST UINT8 *CtaExtension;

  CtaExtension = NULL;
  Status = H618HdmiReadEdidBlock (0, Edid);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EDID base block DDC read failed: %r\n", Status));
    return FALSE;
  }

  if (!H618EdidBlockValid (Edid, 0)) {
    DEBUG ((EFI_D_WARN, "EDID validation failed; using 1080p\n"));
    return FALSE;
  }

  if (Edid[126] > 0) {
    Status = H618HdmiReadEdidBlock (1, EdidExtension);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_WARN, "EDID extension block DDC read failed: %r\n", Status));
    } else if (H618EdidBlockValid (EdidExtension, 1) && (EdidExtension[0] == 0x02))
      CtaExtension = EdidExtension;
  }

  if (!H618EdidGetMode (Edid, CtaExtension, Mode)) {
    DEBUG ((EFI_D_WARN, "EDID has no supported detailed timing; using 1080p\n"));
    return FALSE;
  }

  DEBUG ((EFI_D_WARN, "EDID selected %ux%u at %u kHz\n", Mode->HActive, Mode->VActive, Mode->PixelClockKhz));
  return TRUE;
}
