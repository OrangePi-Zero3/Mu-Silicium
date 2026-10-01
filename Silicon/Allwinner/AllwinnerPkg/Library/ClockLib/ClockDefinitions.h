#ifndef _CLOCK_DEFINITIONS_H_
#define _CLOCK_DEFINITIONS_H_

#define GATE(o, m)   { (o), (m) }
#define RESET(o, m)  { (o), (m) }
#define GATE_DUMMY   { 0, 0 }

typedef struct {
  UINT32  Offset;   // from H618_CCM_BASE; 0 = no entry
  UINT32  Mask;
} CCM_REG_BIT;

STATIC CONST CCM_REG_BIT  mGates[] = {
  [CLK_PLL_PERIPH0]  = GATE (0x020, BIT31 | BIT27),
  [CLK_APB1]         = GATE_DUMMY,

  [CLK_DE]           = GATE (0x600, BIT31),
  [CLK_BUS_DE]       = GATE (0x60c, BIT0),

  [CLK_MBUS_NAND]    = GATE (0x804, BIT5),
  [CLK_NAND0]        = GATE (0x810, BIT31),
  [CLK_NAND1]        = GATE (0x814, BIT31),
  [CLK_BUS_NAND]     = GATE (0x82c, BIT0),

  [CLK_MMC0]         = GATE (0x830, BIT31),
  [CLK_MMC1]         = GATE (0x834, BIT31),
  [CLK_MMC2]         = GATE (0x838, BIT31),
  [CLK_BUS_MMC0]     = GATE (0x84c, BIT0),
  [CLK_BUS_MMC1]     = GATE (0x84c, BIT1),
  [CLK_BUS_MMC2]     = GATE (0x84c, BIT2),

  [CLK_BUS_UART0]    = GATE (0x90c, BIT0),
  [CLK_BUS_UART1]    = GATE (0x90c, BIT1),
  [CLK_BUS_UART2]    = GATE (0x90c, BIT2),
  [CLK_BUS_UART3]    = GATE (0x90c, BIT3),
  [CLK_BUS_UART4]    = GATE (0x90c, BIT4),
  [CLK_BUS_UART5]    = GATE (0x90c, BIT5),

  [CLK_BUS_I2C0]     = GATE (0x91c, BIT0),
  [CLK_BUS_I2C1]     = GATE (0x91c, BIT1),
  [CLK_BUS_I2C2]     = GATE (0x91c, BIT2),
  [CLK_BUS_I2C3]     = GATE (0x91c, BIT3),
  [CLK_BUS_I2C4]     = GATE (0x91c, BIT4),

  [CLK_SPI0]         = GATE (0x940, BIT31),
  [CLK_SPI1]         = GATE (0x944, BIT31),
  [CLK_BUS_SPI0]     = GATE (0x96c, BIT0),
  [CLK_BUS_SPI1]     = GATE (0x96c, BIT1),

  [CLK_BUS_EMAC0]    = GATE (0x97c, BIT0),
  [CLK_BUS_EMAC1]    = GATE (0x97c, BIT1),

  [CLK_USB_PHY0]     = GATE (0xa70, BIT29),
  [CLK_USB_OHCI0]    = GATE (0xa70, BIT31),
  [CLK_USB_PHY1]     = GATE (0xa74, BIT29),
  [CLK_USB_OHCI1]    = GATE (0xa74, BIT31),
  [CLK_USB_PHY2]     = GATE (0xa78, BIT29),
  [CLK_USB_OHCI2]    = GATE (0xa78, BIT31),
  [CLK_USB_PHY3]     = GATE (0xa7c, BIT29),
  [CLK_USB_OHCI3]    = GATE (0xa7c, BIT31),

  [CLK_BUS_OHCI0]    = GATE (0xa8c, BIT0),
  [CLK_BUS_OHCI1]    = GATE (0xa8c, BIT1),
  [CLK_BUS_OHCI2]    = GATE (0xa8c, BIT2),
  [CLK_BUS_OHCI3]    = GATE (0xa8c, BIT3),
  [CLK_BUS_EHCI0]    = GATE (0xa8c, BIT4),
  [CLK_BUS_EHCI1]    = GATE (0xa8c, BIT5),
  [CLK_BUS_EHCI2]    = GATE (0xa8c, BIT6),
  [CLK_BUS_EHCI3]    = GATE (0xa8c, BIT7),
  [CLK_BUS_OTG]      = GATE (0xa8c, BIT8),

  [CLK_HDMI]         = GATE (0xb00, BIT31),
  [CLK_HDMI_SLOW]    = GATE (0xb04, BIT31),
  [CLK_HDMI_CEC]     = GATE (0xb10, BIT31),
  [CLK_BUS_HDMI]     = GATE (0xb1c, BIT0),
  [CLK_BUS_TCON_TOP] = GATE (0xb5c, BIT0),
  [CLK_TCON_TV0]     = GATE (0xb80, BIT31),
  [CLK_TCON_TV1]     = GATE (0xb84, BIT31),
  [CLK_BUS_TCON_TV0] = GATE (0xb9c, BIT0),
  [CLK_BUS_TCON_TV1] = GATE (0xb9c, BIT1),
};

STATIC CONST CCM_REG_BIT  mResets[] = {
  [RST_BUS_DE]       = RESET (0x60c, BIT16),
  [RST_BUS_NAND]     = RESET (0x82c, BIT16),

  [RST_BUS_MMC0]     = RESET (0x84c, BIT16),
  [RST_BUS_MMC1]     = RESET (0x84c, BIT17),
  [RST_BUS_MMC2]     = RESET (0x84c, BIT18),

  [RST_BUS_UART0]    = RESET (0x90c, BIT16),
  [RST_BUS_UART1]    = RESET (0x90c, BIT17),
  [RST_BUS_UART2]    = RESET (0x90c, BIT18),
  [RST_BUS_UART3]    = RESET (0x90c, BIT19),
  [RST_BUS_UART4]    = RESET (0x90c, BIT20),
  [RST_BUS_UART5]    = RESET (0x90c, BIT21),

  [RST_BUS_I2C0]     = RESET (0x91c, BIT16),
  [RST_BUS_I2C1]     = RESET (0x91c, BIT17),
  [RST_BUS_I2C2]     = RESET (0x91c, BIT18),
  [RST_BUS_I2C3]     = RESET (0x91c, BIT19),
  [RST_BUS_I2C4]     = RESET (0x91c, BIT20),

  [RST_BUS_SPI0]     = RESET (0x96c, BIT16),
  [RST_BUS_SPI1]     = RESET (0x96c, BIT17),

  [RST_BUS_EMAC0]    = RESET (0x97c, BIT16),
  [RST_BUS_EMAC1]    = RESET (0x97c, BIT17),

  [RST_USB_PHY0]     = RESET (0xa70, BIT30),
  [RST_USB_PHY1]     = RESET (0xa74, BIT30),
  [RST_USB_PHY2]     = RESET (0xa78, BIT30),
  [RST_USB_PHY3]     = RESET (0xa7c, BIT30),

  [RST_BUS_OHCI0]    = RESET (0xa8c, BIT16),
  [RST_BUS_OHCI1]    = RESET (0xa8c, BIT17),
  [RST_BUS_OHCI2]    = RESET (0xa8c, BIT18),
  [RST_BUS_OHCI3]    = RESET (0xa8c, BIT19),
  [RST_BUS_EHCI0]    = RESET (0xa8c, BIT20),
  [RST_BUS_EHCI1]    = RESET (0xa8c, BIT21),
  [RST_BUS_EHCI2]    = RESET (0xa8c, BIT22),
  [RST_BUS_EHCI3]    = RESET (0xa8c, BIT23),
  [RST_BUS_OTG]      = RESET (0xa8c, BIT24),

  [RST_BUS_HDMI]     = RESET (0xb1c, BIT16),
  [RST_BUS_HDMI_SUB] = RESET (0xb1c, BIT17),
  [RST_BUS_TCON_TOP] = RESET (0xb5c, BIT16),
  [RST_BUS_TCON_TV0] = RESET (0xb9c, BIT16),
  [RST_BUS_TCON_TV1] = RESET (0xb9c, BIT17),
};

#endif // _CLOCK_DEFINITIONS_H_