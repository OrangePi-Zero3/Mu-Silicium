#ifndef EMAC_DXE_H_
#define EMAC_DXE_H_

#include <Uefi.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/DevicePath.h>

#define EMAC_BASE                 0x05020000
#define EMAC_SYSCON               0x03000030
// EMAC PHY address
#define EMAC_PHY_ADDRESS          1

// EMAC descriptor ring size, frame size and packet size
#define EMAC_RING_SIZE            32
#define EMAC_PACKET_SIZE          2048
#define EMAC_MAX_FRAME_SIZE       1536

// EMAC registers
#define EMAC_CTL0                 0x000
#define EMAC_CTL1                 0x004
#define EMAC_TX_CTL0              0x010
#define EMAC_TX_CTL1              0x014
#define EMAC_TX_DMA_DESC          0x020
#define EMAC_RX_CTL0              0x024
#define EMAC_RX_CTL1              0x028
#define EMAC_RX_DMA_DESC          0x034
#define EMAC_MII_CMD              0x048
#define EMAC_MII_DATA             0x04C
#define EMAC_ADDR0_HIGH           0x050
#define EMAC_ADDR0_LOW            0x054

// CTL1 Bit definitions
#define EMAC_CTL1_SOFT_RST        BIT0

// TX_CTL Bit definitions
#define EMAC_TX_CTL0_TX_EN        BIT31
#define EMAC_TX_CTL1_TX_MD        BIT1
#define EMAC_TX_CTL1_TX_DMA_EN    BIT30
#define EMAC_TX_CTL1_TX_DMA_START BIT31

// RX_CTL Bit definitions
#define EMAC_RX_CTL0_RX_EN        BIT31
#define EMAC_RX_CTL1_RX_MD        BIT1
#define EMAC_RX_CTL1_RX_RUNT_FRM  BIT2
#define EMAC_RX_CTL1_RX_ERR_FRM   BIT3
#define EMAC_RX_CTL1_RX_DMA_EN    BIT30
#define EMAC_RX_CTL1_RX_DMA_START BIT31

// MAC Bit definitions
#define EMAC_MAC_FULL_DUPLEX      BIT0
#define EMAC_MAC_SPEED_100        (3 << 2)
#define EMAC_MAC_SPEED_1000       0

// Descriptor bit definitions
#define EMAC_DESC_OWN             BIT31
#define EMAC_DESC_LAST_DESC       BIT30
#define EMAC_DESC_FIRST_DESC      BIT29
#define EMAC_DESC_CHAIN_SECOND    BIT24
#define EMAC_DESC_RX_ERROR_MASK   0x400068DB
#define EMAC_MDIO_BUSY            BIT0
#define EMAC_MDIO_WRITE           BIT1

// MDIO command bit definitions
#define EMAC_MDIO_DIV_128         (3 << 20)
#define EMAC_MII_BMCR              0x00
#define EMAC_MII_BMSR              0x01
#define EMAC_MII_ADVERTISE         0x04
#define EMAC_MII_CTRL1000          0x09
#define EMAC_MII_PHY_SPEC_STATUS   0x11

// BMSR bit definitions
#define EMAC_BMSR_LSTATUS          BIT2
#define EMAC_BMSR_ANEGCOMPLETE     BIT5

// BMCR bit definitions
#define EMAC_BMCR_RESET            BIT15
#define EMAC_BMCR_ANENABLE         BIT12
#define EMAC_BMCR_ANRESTART        BIT9

// Advertise bit definitions
#define EMAC_ADV_10HALF            BIT5
#define EMAC_ADV_10FULL            BIT6
#define EMAC_ADV_100HALF           BIT7
#define EMAC_ADV_100FULL            BIT8
#define EMAC_ADV_CSMA              BIT0
#define EMAC_ADV_1000HALF          BIT8
#define EMAC_ADV_1000FULL          BIT9

// Syscon bit definitions
#define EMAC_SYSCON_EPHY_SHUTDOWN  BIT16
#define EMAC_SYSCON_EPIT           BIT2
#define EMAC_SYSCON_ETCS_INT_GMII  2
#define EMAC_SYSCON_ETXDC_SHIFT    10

//
// YT8531 PHY registers and bit definitions
//
#define EMAC_YT8531_PHY_ID         0x4F51E91B
#define EMAC_YT_EXT_ADDR           0x1E
#define EMAC_YT_EXT_DATA           0x1F
#define EMAC_YT_CHIP_CONFIG        0xA001
#define EMAC_YT_RGMII_CONFIG1      0xA003
#define EMAC_YT_SYNCE_CONFIG       0xA012

#define EMAC_YT_RXC_DELAY_ENABLE   BIT8
#define EMAC_YT_RX_DELAY_MASK      (0xFU << 10)
#define EMAC_YT_RX_DELAY_1950_PS   (13U << 10)

#define EMAC_YT_SYNCE_ENABLE       BIT6

#define EMAC_YT_CLK_125MHZ         BIT4
#define EMAC_YT_CLK_SOURCE_MASK    (7U << 1)

#define EMAC_YT_SPEED_MASK         ((3U << 14) | BIT9)
#define EMAC_YT_SPEED_10           (0U << 14)
#define EMAC_YT_SPEED_100          (1U << 14)
#define EMAC_YT_SPEED_1000         (2U << 14)

#define EMAC_YT_SPEED_2500         BIT9
#define EMAC_YT_DUPLEX             BIT13

STATIC CONST EFI_GUID mEmacDevicePathGuid = {
  0x7c1e5a30, 0x4b0d, 0x4f6e, { 0x9a, 0x21, 0x8d, 0x53, 0xe6, 0x0b, 0x77, 0xc4 }
};

typedef struct {
  UINT32  Status;
  UINT32  ControlSize;
  UINT32  BufferAddress;
  UINT32  Next;
} EMAC_DESCRIPTOR;

typedef struct {
  UINT32                         Signature;
  EFI_SIMPLE_NETWORK_PROTOCOL  Snp;
  EFI_SIMPLE_NETWORK_MODE      Mode;
  EFI_HANDLE                   Handle;
  EFI_EVENT                    PollEvent;
  EFI_EVENT                    ExitBootServicesEvent;
  EMAC_DESCRIPTOR              *TxDescriptors;
  EMAC_DESCRIPTOR              *RxDescriptors;
  UINT8                        *TxBuffer;
  UINT8                        *RxBuffer;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath;
  UINTN                        TxIndex;
  UINTN                        RxIndex;
  UINT8                        PhyAddress;
  UINT32                       LinkSpeed;
  BOOLEAN                      FullDuplex;
  EFI_NETWORK_STATISTICS       Statistics;
} EMAC_PRIVATE;

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH    Vendor;
  MAC_ADDR_DEVICE_PATH  Mac;
  EFI_DEVICE_PATH_PROTOCOL End;
} EMAC_DEVICE_PATH;
#pragma pack()

#define EMAC_PRIVATE_FROM_SNP(This) CR (This, EMAC_PRIVATE, Snp, SIGNATURE_32 ('e','m','a','c'))

#endif
