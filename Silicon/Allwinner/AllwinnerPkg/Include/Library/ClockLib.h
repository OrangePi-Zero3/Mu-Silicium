#ifndef __CLOCK_LIB_H__
#define __CLOCK_LIB_H__

#define H618_CCM_BASE             0x03001000

// PLL registers
#define CCM_PLL_VIDEO0_REG        (H618_CCM_BASE + 0x0040)
#define CCM_PLL_DE_REG            (H618_CCM_BASE + 0x0060)

// Module clock register fields
#define CCM_CLK_ENABLE_BIT        (1 << 31)
#define CCM_CLK_MUX_SHIFT         24
#define CCM_CLK_DIV_N_SHIFT       8
#define CCM_CLK_DIV_M_SHIFT       0
#define CCM_CLK_MUX_MASK          0x7
#define CCM_CLK_DIV_M_MASK        0xF
#define CCM_CLK_DIV_N_MASK        0x3

// PLL control bits
#define CCM_PLL_ENABLE_BIT        (1 << 31)
#define CCM_PLL_LDO_EN_BIT        (1 << 30)
#define CCM_PLL_LOCK_ENABLE_BIT   (1 << 29)
#define CCM_PLL_LOCK_STATUS_BIT   (1 << 28)
#define CCM_PLL_OUT_EN_BIT        (1 << 27)

// Clock IDs
#define CLK_PLL_PERIPH0     4
#define CLK_APB1            26
#define CLK_DE              29
#define CLK_BUS_DE          30
#define CLK_MBUS_NAND       54
#define CLK_NAND0           57
#define CLK_NAND1           58
#define CLK_BUS_NAND        59
#define CLK_MMC0            60
#define CLK_MMC1            61
#define CLK_MMC2            62
#define CLK_BUS_MMC0        63
#define CLK_BUS_MMC1        64
#define CLK_BUS_MMC2        65
#define CLK_BUS_UART0       66
#define CLK_BUS_UART1       67
#define CLK_BUS_UART2       68
#define CLK_BUS_UART3       69
#define CLK_BUS_UART4       70
#define CLK_BUS_UART5       71
#define CLK_BUS_I2C0        72
#define CLK_BUS_I2C1        73
#define CLK_BUS_I2C2        74
#define CLK_BUS_I2C3        75
#define CLK_BUS_I2C4        76
#define CLK_SPI0            77
#define CLK_SPI1            78
#define CLK_BUS_SPI0        79
#define CLK_BUS_SPI1        80
#define CLK_BUS_EMAC0       82
#define CLK_BUS_EMAC1       83
#define CLK_USB_OHCI0       96
#define CLK_USB_PHY0        97
#define CLK_USB_OHCI1       98
#define CLK_USB_PHY1        99
#define CLK_USB_OHCI2       100
#define CLK_USB_PHY2        101
#define CLK_USB_OHCI3       102
#define CLK_USB_PHY3        103
#define CLK_BUS_OHCI0       104
#define CLK_BUS_OHCI1       105
#define CLK_BUS_OHCI2       106
#define CLK_BUS_OHCI3       107
#define CLK_BUS_EHCI0       108
#define CLK_BUS_EHCI1       109
#define CLK_BUS_EHCI2       110
#define CLK_BUS_EHCI3       111
#define CLK_BUS_OTG         112
#define CLK_HDMI            114
#define CLK_HDMI_SLOW       115
#define CLK_HDMI_CEC        116
#define CLK_BUS_HDMI        117
#define CLK_BUS_TCON_TOP    118
#define CLK_TCON_TV0        119
#define CLK_TCON_TV1        120
#define CLK_BUS_TCON_TV0    121
#define CLK_BUS_TCON_TV1    122
 
// Reset IDs
#define RST_BUS_DE          1
#define RST_BUS_NAND        13
#define RST_BUS_MMC0        14
#define RST_BUS_MMC1        15
#define RST_BUS_MMC2        16
#define RST_BUS_UART0       17
#define RST_BUS_UART1       18
#define RST_BUS_UART2       19
#define RST_BUS_UART3       20
#define RST_BUS_UART4       21
#define RST_BUS_UART5       22
#define RST_BUS_I2C0        23
#define RST_BUS_I2C1        24
#define RST_BUS_I2C2        25
#define RST_BUS_I2C3        26
#define RST_BUS_I2C4        27
#define RST_BUS_SPI0        28
#define RST_BUS_SPI1        29
#define RST_BUS_EMAC0       30
#define RST_BUS_EMAC1       31
#define RST_USB_PHY0        38
#define RST_USB_PHY1        39
#define RST_USB_PHY2        40
#define RST_USB_PHY3        41
#define RST_BUS_OHCI0       42
#define RST_BUS_OHCI1       43
#define RST_BUS_OHCI2       44
#define RST_BUS_OHCI3       45
#define RST_BUS_EHCI0       46
#define RST_BUS_EHCI1       47
#define RST_BUS_EHCI2       48
#define RST_BUS_EHCI3       49
#define RST_BUS_OTG         50
#define RST_BUS_HDMI        51
#define RST_BUS_HDMI_SUB    52
#define RST_BUS_TCON_TOP    53
#define RST_BUS_TCON_TV0    54
#define RST_BUS_TCON_TV1    55

VOID CcmClkEnable  (IN UINT32 ClkId);
VOID CcmClkDisable (IN UINT32 ClkId);
VOID CcmClkConfigure (IN UINT32 ClkId, IN UINT32 Mux, IN UINT32 DivM, IN UINT32 DivN);
VOID CcmResetAssert   (IN UINT32 RstId);
VOID CcmResetDeassert (IN UINT32 RstId);
VOID CcmResetPulse    (IN UINT32 RstId);
VOID CcmBusEnable (IN UINT32 ClkId, IN UINT32 RstId);
VOID CcmBusUngate (IN UINT32 ClkId, IN UINT32 RstId);
BOOLEAN CcmPllEnableAndLock (IN UINTN Reg);

#endif // __CLOCK_LIB_H__