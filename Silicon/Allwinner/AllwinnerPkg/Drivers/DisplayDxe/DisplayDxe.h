#ifndef DISPLAY_DXE_H_
#define DISPLAY_DXE_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

// Syscon registers
#define H618_SYSCON_BASE           0x03000000
#define SYSCON_SRAM_CTRL_REG1      (H618_SYSCON_BASE + 0x0004)
#define SYSCON_SRAM_C_MAP_DE_BIT   (1 << 24)

// Display engine base and regs
#define H618_DE_BASE               0x01000000
#define DE33_CLK_BASE              (H618_DE_BASE + 0x8000)
#define DE33_MOD_GATE_REG          (DE33_CLK_BASE + 0x00)
#define DE33_BUS_GATE_REG          (DE33_CLK_BASE + 0x04)
#define DE33_RESET_REG             (DE33_CLK_BASE + 0x08)
#define DE33_DIV_REG               (DE33_CLK_BASE + 0x0C)
#define DE33_INIT0_REG             (DE33_CLK_BASE + 0x24)
#define DE33_INIT1_REG             (DE33_CLK_BASE + 0x28)
#define DE33_MIXER0_BIT            (1 << 0)
#define DE33_INIT1_MAGIC           0x0000A980

// Display enginer mixer regs
#define DE_MIXER_TOP_BASE          (H618_DE_BASE + 0x8100)
#define DE_MIXER_GLOBAL_CTL_REG    (DE_MIXER_TOP_BASE + 0x00)
#define DE_MIXER_GLOBAL_STATUS_REG (DE_MIXER_TOP_BASE + 0x04)
#define DE_MIXER_GLOBAL_SIZE_REG   (DE_MIXER_TOP_BASE + 0x08)
#define DE_MIXER_GLOBAL_CLK_REG    (DE_MIXER_TOP_BASE + 0x0C)
#define DE_MIXER_GLOBAL_RT_EN      (1 << 0)

// Display engine channel/overlay regs
#define DE_CHN_BASE(n)             (H618_DE_BASE + 0x100000 + (n) * 0x20000)
#define DE_CHN_OVL_OFFSET          0x1000

#define DE_OVL_UI_LAY_STRIDE       0x20
#define DE_OVL_UI_LAY_ATTCTL       0x00
#define DE_OVL_UI_LAY_MBSIZE       0x04
#define DE_OVL_UI_LAY_MBCOOR       0x08
#define DE_OVL_UI_LAY_PITCH        0x0C
#define DE_OVL_UI_LAY_TOP_LADDR    0x10
#define DE_OVL_UI_LAY_BOT_LADDR    0x14
#define DE_OVL_UI_LAY_FCOLOR       0x18
#define DE_OVL_UI_TOP_HADDR        0x80
#define DE_OVL_UI_BOT_HADDR        0x84
#define DE_OVL_UI_WIN_SIZE         0x88

#define DE_OVL_UI_ATTCTL_EN        (1 << 0)
#define DE_OVL_UI_ATTCTL_ALPHA_MODE_SHIFT 1
#define DE_OVL_UI_ATTCTL_FCOLOR_EN (1 << 4)
#define DE_OVL_UI_ATTCTL_FMT_SHIFT 8
#define DE_OVL_UI_ATTCTL_GLB_ALPHA_SHIFT 24
#define DE_OVL_FMT_XRGB_8888       0x04

#define DE_UI_CHN_LOGICAL          1
#define DE_UI_CHN_PHYS             6
#define DE_UI_OVL_BASE             (DE_CHN_BASE (DE_UI_CHN_PHYS) + DE_CHN_OVL_OFFSET)

// Display engine blender regs
#define DE_DISP_BASE(n)            (H618_DE_BASE + 0x280000 + (n) * 0x20000)
#define DE_DISP_BLD_OFFSET         0x1000
#define DE_BLD_BASE                (DE_DISP_BASE (0) + DE_DISP_BLD_OFFSET)

#define DE_BLD_EN_CTL              0x0000
#define DE_BLD_PIPE_ATTR(p)        (0x0000 + (p) * 0x10)
#define DE_BLD_PIPE_FCOLOR         0x04
#define DE_BLD_PIPE_IN_SIZE        0x08
#define DE_BLD_PIPE_IN_COORD       0x0C
#define DE_BLD_ROUT_CTL            0x0080
#define DE_BLD_PREMUL_CTL          0x0084
#define DE_BLD_BG_COLOR            0x0088
#define DE_BLD_OUT_SIZE            0x008C
#define DE_BLD_BLEND_CTL(n)        (0x0090 + (n) * 0x4)
#define DE_BLD_OUT_CTL             0x00FC

#define DE_BLD_PIPE_FCOLOR_EN(p)   (1 << (p))
#define DE_BLD_PIPE_EN(p)          (1 << ((p) + 8))
#define DE_BLD_MODE_SRCOVER        0x03010301

// TCON-TOP and TCON-TV0 regs
#define H618_TCON_TOP_BASE         0x06510000
#define H618_TCON_LCD0_BASE        0x06511000
#define H618_TCON_TV0_BASE         0x06515000
#define H618_TCON_TV0_INDEX        2

#define TCON_TOP_TV_SETUP_REG_OFFSET  0x0000
#define TCON_TOP_DE_PERH_REG_OFFSET   0x001C
#define TCON_TOP_CLK_GATE_REG_OFFSET  0x0020

#define TCON_GCTL_REG_OFFSET       0x0000
#define TCON_GINT0_REG_OFFSET      0x0004
#define TCON0_IO_POL_REG_OFFSET    0x0088
#define TCON0_IO_TRI_REG_OFFSET    0x008C
#define TCON_TV_CTL_REG_OFFSET     0x0090
#define TCON_TV_BASIC0_REG_OFFSET  0x0094
#define TCON_TV_BASIC1_REG_OFFSET  0x0098
#define TCON_TV_BASIC2_REG_OFFSET  0x009C
#define TCON_TV_BASIC3_REG_OFFSET  0x00A0
#define TCON_TV_BASIC4_REG_OFFSET  0x00A4
#define TCON_TV_BASIC5_REG_OFFSET  0x00A8
#define TCON_TV_IO_POL_REG_OFFSET  0x00F0
#define TCON_TV_IO_TRI_REG_OFFSET  0x00F4

#define TCON_TOP_DE_PORT0_PERH_SHIFT  0
#define TCON_TOP_DE_PORT1_PERH_SHIFT  4
#define TCON_TOP_TV0_CLK_GATE         (1 << 20)
#define TCON_TOP_TV1_CLK_GATE         (1 << 21)
#define TCON_TOP_HDMI_SRC_SHIFT       28
#define TCON_TOP_HDMI_SRC_TV0         1

#define TCON_GCTL_EN               (1 << 31)
#define TCON_GCTL_IOMAP_TCON1      (1 << 0)
#define TCON_TV_CTL_EN             (1 << 31)
#define TCON_TV_CTL_INTERLACE_EN   (1 << 20)
#define TCON_TV_CTL_START_DELAY_SHIFT 4
#define TCON_TV_CTL_SRC_SEL_SHIFT  0

#define TCON_TV_BASIC0_X_SHIFT     16
#define TCON_TV_BASIC0_Y_SHIFT     0
#define TCON_TV_BASIC3_HT_SHIFT    16
#define TCON_TV_BASIC3_HBP_SHIFT   0
#define TCON_TV_BASIC4_VT_SHIFT    16
#define TCON_TV_BASIC4_VBP_SHIFT   0
#define TCON_TV_BASIC5_HSPW_SHIFT  16
#define TCON_TV_BASIC5_VSPW_SHIFT  0

#define TCON0_IO_POL_HSYNC_POSITIVE (1 << 24)
#define TCON0_IO_POL_VSYNC_POSITIVE (1 << 25)
#define TCON0_IO_POL_CLK_INV        (1 << 26)

#define TCON0_IO_TRI_IO0_EN        (1 << 24)
#define TCON0_IO_TRI_IO1_EN        (1 << 25)
#define TCON0_IO_TRI_IO2_EN        (1 << 26)
#define TCON0_IO_TRI_IO3_EN        (1 << 27)
#define TCON0_IO_TRI_DATA_MASK     0x00FFFFFF

typedef struct {
  UINT32   HActive;
  UINT32   HFrontPorch;
  UINT32   HSyncWidth;
  UINT32   HBackPorch;
  UINT32   VActive;
  UINT32   VFrontPorch;
  UINT32   VSyncWidth;
  UINT32   VBackPorch;
  UINT32   PixelClockKhz;
  BOOLEAN  HSyncPositive;
  BOOLEAN  VSyncPositive;
} DISPLAY_MODE;

#define MODE_H_TOTAL(m)  ((m)->HActive + (m)->HFrontPorch + (m)->HSyncWidth + (m)->HBackPorch)
#define MODE_V_TOTAL(m)  ((m)->VActive + (m)->VFrontPorch + (m)->VSyncWidth + (m)->VBackPorch)

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL          Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  Info;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     Mode;
  EFI_PHYSICAL_ADDRESS                  FrameBufferBase;
  UINTN                                 FrameBufferSize;
  DISPLAY_MODE                          ActiveMode;
} GOP_PRIVATE;

EFI_STATUS
H618DisplayHardwareInit (
  IN  EFI_PHYSICAL_ADDRESS       FrameBufferBase,
  IN  CONST DISPLAY_MODE  *Mode
  );

extern GOP_PRIVATE  *mPrivate;

#endif // DISPLAY_DXE_H_
