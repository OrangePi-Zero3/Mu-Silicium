#include "DisplayDxe.h"
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/DevicePath.h>
#include <Configuration/BootDevices.h>
#include <Library/UefiBootManagerLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextIn.h>

#include "Clock.h"
#include "Phy.h"
#include "Gop.h"

//
// Global Variables
//
GOP_PRIVATE  *mPrivate = NULL;

//
// Initialises DisplayEngine
//
STATIC
VOID
H618DeInit (
  IN EFI_PHYSICAL_ADDRESS       FrameBufferBase,
  IN CONST DISPLAY_MODE  *Mode
  )
{
  UINT32 Width  = Mode->HActive;
  UINT32 Height = Mode->VActive;

  UINT32 Size = ((Width - 1) & 0x1FFF) | (((Height - 1) & 0x1FFF) << 16);

  MmioWrite32(SYSCON_SRAM_CTRL_REG1, MmioRead32(SYSCON_SRAM_CTRL_REG1) & ~SYSCON_SRAM_C_MAP_DE_BIT);

  MmioWrite32 (DE33_INIT0_REG, 0x00000000);
  MmioWrite32 (DE33_INIT1_REG, DE33_INIT1_MAGIC);
  MmioWrite32 (DE33_DIV_REG,   0x00000000);
  MmioWrite32 (DE33_BUS_GATE_REG, DE33_MIXER0_BIT);
  MmioWrite32 (DE33_MOD_GATE_REG, DE33_MIXER0_BIT);
  MmioWrite32 (DE33_RESET_REG,    DE33_MIXER0_BIT);
  MicroSecondDelay (10);

  MmioWrite32 (DE_MIXER_GLOBAL_CTL_REG, DE_MIXER_GLOBAL_RT_EN);
  MmioWrite32 (DE_MIXER_GLOBAL_CLK_REG, 0x1);
  MmioWrite32 (DE_MIXER_GLOBAL_SIZE_REG, Size);
  MicroSecondDelay (10);

  MmioWrite32 (DE_BLD_BASE + DE_BLD_EN_CTL, DE_BLD_PIPE_FCOLOR_EN (0) | DE_BLD_PIPE_EN (0));
  MmioBitFieldWrite32 (DE_BLD_BASE + DE_BLD_ROUT_CTL, 0, 3, DE_UI_CHN_LOGICAL);
  MmioWrite32 (DE_BLD_BASE + DE_BLD_PREMUL_CTL, 0x0);
  MmioWrite32 (DE_BLD_BASE + DE_BLD_BG_COLOR, 0xFF000000);
  MmioWrite32 (DE_BLD_BASE + DE_BLD_OUT_SIZE,
    ((Width - 1) & 0x1FFF) | (((Height - 1) & 0x1FFF) << 16));
  MmioWrite32 (DE_BLD_BASE + DE_BLD_BLEND_CTL (0), DE_BLD_MODE_SRCOVER);

  MmioWrite32 (DE_BLD_BASE + DE_BLD_PIPE_ATTR (0) + DE_BLD_PIPE_IN_SIZE,
    ((Width - 1) & 0x1FFF) | (((Height - 1) & 0x1FFF) << 16));
  MmioWrite32 (DE_BLD_BASE + DE_BLD_PIPE_ATTR (0) + DE_BLD_PIPE_IN_COORD, 0x0);

  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_LAY_ATTCTL,
    DE_OVL_UI_ATTCTL_EN |
    (0x1 << DE_OVL_UI_ATTCTL_ALPHA_MODE_SHIFT) |
    (DE_OVL_FMT_XRGB_8888 << DE_OVL_UI_ATTCTL_FMT_SHIFT) |
    (0xFF << DE_OVL_UI_ATTCTL_GLB_ALPHA_SHIFT));

  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_LAY_MBSIZE,
    ((Width - 1) & 0x1FFF) | (((Height - 1) & 0x1FFF) << 16));
  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_LAY_MBCOOR, 0x0);
  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_LAY_PITCH, Width * 4);
  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_LAY_TOP_LADDR, (UINT32)FrameBufferBase);
  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_LAY_BOT_LADDR, 0x0);
  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_LAY_FCOLOR, 0xFF000000);

  MmioAnd32 (DE_UI_OVL_BASE + DE_OVL_UI_TOP_HADDR, ~(UINT32)0xFF);

  MmioWrite32 (DE_UI_OVL_BASE + DE_OVL_UI_WIN_SIZE,
    ((Width - 1) & 0x1FFF) | (((Height - 1) & 0x1FFF) << 16));
}

//
// Initialises TCON TOP
//
STATIC
VOID
H618TconTopInit (
  VOID
  )
{
  MmioWrite32 (H618_TCON_TOP_BASE + TCON_TOP_DE_PERH_REG_OFFSET, 0);
  MmioWrite32 (H618_TCON_TOP_BASE + TCON_TOP_CLK_GATE_REG_OFFSET, 0);

  MmioWrite32 (H618_TCON_TOP_BASE + TCON_TOP_DE_PERH_REG_OFFSET,
    (H618_TCON_TV0_INDEX << TCON_TOP_DE_PORT0_PERH_SHIFT) |
    (H618_TCON_TV0_INDEX << TCON_TOP_DE_PORT1_PERH_SHIFT));

  MmioOr32(H618_TCON_TOP_BASE + TCON_TOP_CLK_GATE_REG_OFFSET, TCON_TOP_TV0_CLK_GATE);
  MmioBitFieldWrite32 (H618_TCON_TOP_BASE + TCON_TOP_CLK_GATE_REG_OFFSET, TCON_TOP_HDMI_SRC_SHIFT, TCON_TOP_HDMI_SRC_SHIFT + 1, TCON_TOP_HDMI_SRC_TV0);
}

//
// Initialises TCON TV0
//
STATIC
VOID
H618TconTvInit (
  IN CONST DISPLAY_MODE  *Mode
  )
{
  UINT32 HTotal = Mode->HActive + Mode->HFrontPorch + Mode->HSyncWidth + Mode->HBackPorch;
  UINT32 VTotal = Mode->VActive + Mode->VFrontPorch + Mode->VSyncWidth + Mode->VBackPorch;
  UINT32 StartDelay;

  MmioAndThenOr32 (H618_TCON_TV0_BASE + TCON_GCTL_REG_OFFSET, ~TCON_GCTL_IOMAP_TCON1, TCON_GCTL_EN);
  
  MmioWrite32 (H618_TCON_TV0_BASE + TCON_GINT0_REG_OFFSET, 0x0);

  MmioWrite32 (H618_TCON_TV0_BASE + TCON_TV_BASIC0_REG_OFFSET,
    (((Mode->HActive - 1) & 0xFFF) << TCON_TV_BASIC0_X_SHIFT) |
    (((Mode->VActive - 1) & 0xFFF) << TCON_TV_BASIC0_Y_SHIFT));

  MmioWrite32 (H618_TCON_TV0_BASE + TCON_TV_BASIC1_REG_OFFSET,
    (((Mode->HActive - 1) & 0xFFF) << 16) |
    ((Mode->VActive - 1) & 0xFFF));
  MmioWrite32 (H618_TCON_TV0_BASE + TCON_TV_BASIC2_REG_OFFSET,
    (((Mode->HActive - 1) & 0xFFF) << 16) |
    ((Mode->VActive - 1) & 0xFFF));

  MmioWrite32 (H618_TCON_TV0_BASE + TCON_TV_BASIC3_REG_OFFSET,
    (((HTotal - 1) & 0x1FFF) << TCON_TV_BASIC3_HT_SHIFT) |
    (((Mode->HSyncWidth + Mode->HBackPorch - 1) & 0xFFF) << TCON_TV_BASIC3_HBP_SHIFT));

  MmioWrite32 (H618_TCON_TV0_BASE + TCON_TV_BASIC4_REG_OFFSET,
    (((VTotal * 2) & 0x1FFF) << TCON_TV_BASIC4_VT_SHIFT) |
    (((Mode->VSyncWidth + Mode->VBackPorch - 1) & 0xFFF) << TCON_TV_BASIC4_VBP_SHIFT));

  MmioWrite32 (H618_TCON_TV0_BASE + TCON_TV_BASIC5_REG_OFFSET,
    (((Mode->HSyncWidth - 1) & 0x3FF) << TCON_TV_BASIC5_HSPW_SHIFT) |
    (((Mode->VSyncWidth - 1) & 0x3FF) << TCON_TV_BASIC5_VSPW_SHIFT));

  MmioWrite32 (H618_TCON_TV0_BASE + TCON0_IO_POL_REG_OFFSET,
    (Mode->HSyncPositive ? TCON0_IO_POL_HSYNC_POSITIVE : 0) |
    (Mode->VSyncPositive ? TCON0_IO_POL_VSYNC_POSITIVE : 0));

  MmioAndThenOr32 (H618_TCON_TV0_BASE + TCON0_IO_TRI_REG_OFFSET, ~(UINT32)(TCON0_IO_TRI_IO0_EN | TCON0_IO_TRI_IO1_EN), TCON0_IO_TRI_IO2_EN | TCON0_IO_TRI_IO3_EN | TCON0_IO_TRI_DATA_MASK);

  StartDelay = VTotal - Mode->VActive;
  StartDelay = (StartDelay > 5) ? (StartDelay - 5) : 0;
  if (StartDelay > 31) {
    StartDelay = 31;
  }

  MmioBitFieldWrite32 (H618_TCON_TV0_BASE + TCON_TV_CTL_REG_OFFSET, TCON_TV_CTL_SRC_SEL_SHIFT, TCON_TV_CTL_SRC_SEL_SHIFT + 1, 0);
  MmioBitFieldWrite32 (H618_TCON_TV0_BASE + TCON_TV_CTL_REG_OFFSET, TCON_TV_CTL_START_DELAY_SHIFT, TCON_TV_CTL_START_DELAY_SHIFT + 4, StartDelay);
  
  MmioAnd32 (H618_TCON_TV0_BASE + TCON_TV_CTL_REG_OFFSET, ~(UINT32)TCON_TV_CTL_INTERLACE_EN);
  MmioOr32  (H618_TCON_TV0_BASE + TCON_TV_CTL_REG_OFFSET, TCON_TV_CTL_EN);
}

//
// Initialises HDMI controller
//
STATIC
VOID
HdmiControllerInit (
  IN CONST DISPLAY_MODE  *Mode
  )
{
  MmioWrite8 (H618_HDMI_BASE + HDMI_MC_SWRSTZ_REG, 0x7D);
  MmioWrite8 (H618_HDMI_BASE + HDMI_MC_FLOWCTRL_REG, 0x00);
  MmioWrite8 (H618_HDMI_BASE + HDMI_MC_PHYRSTZ_REG, 0x01);

  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INVIDCONF_REG,
    (1 << 4) |
    (Mode->HSyncPositive ? (1 << 5) : 0) |
    (Mode->VSyncPositive ? (1 << 6) : 0));

  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INHACTV1_REG, (Mode->HActive >> 8) & 0xFF);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INHACTV0_REG, Mode->HActive & 0xFF);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INVACTV1_REG, (Mode->VActive >> 8) & 0xFF);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INVACTV0_REG, Mode->VActive & 0xFF);

  {
    UINT32 HBlank = Mode->HFrontPorch + Mode->HSyncWidth + Mode->HBackPorch;
    MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INHBLANK1_REG, (HBlank >> 8) & 0xFF);
    MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INHBLANK0_REG, HBlank & 0xFF);
  }
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_INVBLANK_REG,
    Mode->VFrontPorch + Mode->VSyncWidth + Mode->VBackPorch);

  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_HSYNCINDELAY1_REG, (Mode->HFrontPorch >> 8) & 0xFF);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_HSYNCINDELAY0_REG, Mode->HFrontPorch & 0xFF);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_HSYNCINWIDTH1_REG, (Mode->HSyncWidth >> 8) & 0xFF);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_HSYNCINWIDTH0_REG, Mode->HSyncWidth & 0xFF);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_VSYNCINDELAY_REG, Mode->VFrontPorch);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_VSYNCINWIDTH_REG, Mode->VSyncWidth);

  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_CTRLDUR_REG, 12);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_EXCTRLDUR_REG, 32);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_EXCTRLSPAC_REG, 1);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_CH0PREAM_REG, 11);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_CH1PREAM_REG, 22);
  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_CH2PREAM_REG, 33);

  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_PRCONF_REG, 1 << HDMI_FC_PRCONF_INCOMING_SHIFT);

  MmioWrite8 (H618_HDMI_BASE + HDMI_MC_CLKDIS_REG, 0x7F & ~HDMI_MC_CLKDIS_PIXELCLK);
  MmioWrite8 (H618_HDMI_BASE + HDMI_MC_CLKDIS_REG,
    0x7F & ~(HDMI_MC_CLKDIS_PIXELCLK | HDMI_MC_CLKDIS_TMDSCLK));

  MmioWrite8 (H618_HDMI_BASE + HDMI_VP_PR_CD_REG, 0x00);
  MmioWrite8 (H618_HDMI_BASE + HDMI_VP_STUFF_REG, 0x07);
  MmioWrite8 (H618_HDMI_BASE + HDMI_VP_REMAP_REG, 0x00);
  MmioWrite8 (H618_HDMI_BASE + HDMI_VP_CONF_REG, 0x47);
  MmioWrite8 (H618_HDMI_BASE + HDMI_TX_INVID0_REG, HDMI_TX_INVID0_MAP_RGB444_8BIT);
  MmioWrite8 (H618_HDMI_BASE + HDMI_TX_INSTUFFING_REG, 0x07);

  if (EFI_ERROR (H618HdmiPhyInit (Mode))) {
    DEBUG ((EFI_D_ERROR, "PHY bring-up failed; picture will be absent\n"));
  }

  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_GCP_REG, 0x00);

  MmioWrite8 (H618_HDMI_BASE + HDMI_FC_DBGFORCE_REG, 0x00);
  MmioWrite8 (H618_HDMI_BASE + HDMI_IH_MUTE_REG, 0x00);
}

EFI_STATUS
H618DisplayHardwareInit (
  IN  EFI_PHYSICAL_ADDRESS       FrameBufferBase,
  IN  CONST DISPLAY_MODE  *Mode
  )
{
  H618ClocksInit (Mode);
  H618DeInit (FrameBufferBase, Mode);
  H618TconTopInit ();
  H618TconTvInit (Mode);
  HdmiControllerInit (Mode);

  MmioWrite32 (DE_MIXER_GLOBAL_CTL_REG, DE_MIXER_GLOBAL_RT_EN);
  MmioWrite32 (DE_MIXER_GLOBAL_CLK_REG, 0x1);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DisplayDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                 Status;
  EFI_HANDLE                 Handle = NULL;
  CONST DISPLAY_MODE         *Mode;

  mPrivate = AllocateZeroPool (sizeof (GOP_PRIVATE));
  if (mPrivate == NULL)
  {
    return EFI_OUT_OF_RESOURCES;
  }

  mPrivate->ActiveMode = (DISPLAY_MODE){
    .HActive       = 1920,
    .HFrontPorch   = 88,
    .HSyncWidth    = 44,
    .HBackPorch    = 148,
    .VActive       = 1080,
    .VFrontPorch   = 4,
    .VSyncWidth    = 5,
    .VBackPorch    = 36,
    .PixelClockKhz = 148500,
    .HSyncPositive = TRUE,
    .VSyncPositive = TRUE
  };

  Mode = &mPrivate->ActiveMode;

  mPrivate->FrameBufferSize = Mode->HActive * Mode->VActive * 4;

  Status = gBS->AllocatePages (AllocateAnyPages, EfiReservedMemoryType, EFI_SIZE_TO_PAGES (mPrivate->FrameBufferSize), &mPrivate->FrameBufferBase);
  if (EFI_ERROR (Status))
    return Status;

  ZeroMem ((VOID *)(UINTN)mPrivate->FrameBufferBase, mPrivate->FrameBufferSize);

  Status = gDS->SetMemorySpaceAttributes (mPrivate->FrameBufferBase, mPrivate->FrameBufferSize, EFI_MEMORY_WC);
  if (EFI_ERROR (Status))
    Status = gDS->SetMemorySpaceAttributes (mPrivate->FrameBufferBase, mPrivate->FrameBufferSize, EFI_MEMORY_UC);

  if (EFI_ERROR (Status))
  {
    DEBUG((EFI_D_ERROR, "Unable to set framebuffer memory attributes: %r\n", Status));
    return Status;
  }

  WriteBackDataCacheRange ((VOID *)(UINTN)mPrivate->FrameBufferBase, mPrivate->FrameBufferSize);

  mPrivate->Info.Version              = 0;
  mPrivate->Info.HorizontalResolution = Mode->HActive;
  mPrivate->Info.VerticalResolution   = Mode->VActive;
  mPrivate->Info.PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mPrivate->Info.PixelsPerScanLine    = Mode->HActive;

  mPrivate->Mode.MaxMode              = 1;
  mPrivate->Mode.Mode                 = 0;
  mPrivate->Mode.Info                 = &mPrivate->Info;
  mPrivate->Mode.SizeOfInfo           = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  mPrivate->Mode.FrameBufferBase      = mPrivate->FrameBufferBase;
  mPrivate->Mode.FrameBufferSize      = mPrivate->FrameBufferSize;

  mPrivate->Gop.QueryMode = GopQueryMode;
  mPrivate->Gop.SetMode   = GopSetMode;
  mPrivate->Gop.Blt       = GopBlt;
  mPrivate->Gop.Mode      = &mPrivate->Mode;

  Status = H618DisplayHardwareInit (mPrivate->FrameBufferBase, Mode);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "hardware init failed: %r\n", Status));
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid, &DisplayDevicePath,
                  &gEfiGraphicsOutputProtocolGuid, &mPrivate->Gop,
                  NULL
                  );
  return Status;
}
