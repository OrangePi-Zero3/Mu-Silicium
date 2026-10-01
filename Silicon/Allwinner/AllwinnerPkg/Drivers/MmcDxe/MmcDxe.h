#ifndef MMC_DXE_H_
#define MMC_DXE_H_

//
// Controller base and registers
//
#define MMC0_BASE                     0x04020000

#define MMC_GCTRL                     0x00
#define MMC_CLKCR                     0x04
#define MMC_TIMEOUT                   0x08
#define MMC_WIDTH                     0x0C
#define MMC_BLKSZ                     0x10
#define MMC_BYTECNT                   0x14
#define MMC_CMD                       0x18
#define MMC_ARG                       0x1C
#define MMC_RESP0                     0x20
#define MMC_RESP1                     0x24
#define MMC_RESP2                     0x28
#define MMC_RESP3                     0x2C
#define MMC_IMASK                     0x30
#define MMC_MINT                      0x34
#define MMC_RINT                      0x38
#define MMC_STATUS                    0x3C
#define MMC_FTRGLEVEL                 0x40
#define MMC_NTSR                      0x5C
#define MMC_SAMP_DL                   0x144   // H6 family only
#define MMC_FIFO                      0x200   // 0x100 on pre-H6 parts

#define GCTRL_SOFT_RESET              BIT0
#define GCTRL_FIFO_RESET              BIT1
#define GCTRL_DMA_RESET               BIT2
#define GCTRL_RESET                   (GCTRL_SOFT_RESET | GCTRL_FIFO_RESET | GCTRL_DMA_RESET)
#define GCTRL_DMA_ENABLE              BIT5
#define GCTRL_ACCESS_BY_AHB           BIT31

#define CLK_POWERSAVE                 BIT17
#define CLK_ENABLE                    BIT16
#define CLK_DIVIDER_MASK              0xFF

#define CMD_RESP_EXPIRE               BIT6
#define CMD_LONG_RESPONSE             BIT7
#define CMD_CHK_RESPONSE_CRC          BIT8
#define CMD_DATA_EXPIRE               BIT9
#define CMD_WRITE                     BIT10
#define CMD_AUTO_STOP                 BIT12
#define CMD_WAIT_PRE_OVER             BIT13
#define CMD_SEND_INIT_SEQ             BIT15
#define CMD_UPCLK_ONLY                BIT21
#define CMD_START                     BIT31

#define RINT_RESP_ERROR               BIT1
#define RINT_COMMAND_DONE             BIT2
#define RINT_DATA_OVER                BIT3
#define RINT_RESP_CRC_ERROR           BIT6
#define RINT_DATA_CRC_ERROR           BIT7
#define RINT_RESP_TIMEOUT             BIT8
#define RINT_DATA_TIMEOUT             BIT9
#define RINT_VOLTAGE_CHANGE_DONE      BIT10
#define RINT_FIFO_RUN_ERROR           BIT11
#define RINT_HARD_WARE_LOCKED         BIT12
#define RINT_START_BIT_ERROR          BIT13
#define RINT_AUTO_COMMAND_DONE        BIT14
#define RINT_END_BIT_ERROR            BIT15

#define RINT_ERROR_MASK               (RINT_RESP_ERROR | RINT_RESP_CRC_ERROR | \
                                       RINT_DATA_CRC_ERROR | RINT_RESP_TIMEOUT | \
                                       RINT_DATA_TIMEOUT | RINT_VOLTAGE_CHANGE_DONE | \
                                       RINT_FIFO_RUN_ERROR | RINT_HARD_WARE_LOCKED | \
                                       RINT_START_BIT_ERROR | RINT_END_BIT_ERROR)

#define STATUS_RXWL_FLAG              BIT0
#define STATUS_TXWL_FLAG              BIT1
#define STATUS_FIFO_EMPTY             BIT2
#define STATUS_FIFO_FULL              BIT3
#define STATUS_CARD_PRESENT           BIT8
#define STATUS_CARD_DATA_BUSY         BIT9
#define STATUS_FIFO_LEVEL(x)          (((x) >> 17) & 0x3FFF)

#define SAMP_DL_SW_EN                 BIT7

//
// CMDs
//
#define CMD_GO_IDLE_STATE             0
#define CMD_ALL_SEND_CID              2
#define CMD_SEND_RELATIVE_ADDR        3
#define CMD_SWITCH_FUNC               6
#define CMD_SELECT_CARD               7
#define CMD_SEND_IF_COND              8
#define CMD_SEND_CSD                  9
#define CMD_STOP_TRANSMISSION         12
#define CMD_SET_BLOCKLEN              16
#define CMD_READ_SINGLE_BLOCK         17
#define CMD_READ_MULTIPLE_BLOCK       18
#define CMD_WRITE_SINGLE_BLOCK        24
#define CMD_WRITE_MULTIPLE_BLOCK      25
#define CMD_APP_CMD                   55

#define ACMD_SET_BUS_WIDTH            6
#define ACMD_SD_SEND_OP_COND          41

//
// Response classes, mapped onto the controller's CMD register bits.
//
#define RESP_NONE                     0x00
#define RESP_R1                       0x01
#define RESP_R1B                      0x02
#define RESP_R2                       0x04
#define RESP_R3                       0x08
#define RESP_R6                       0x10
#define RESP_R7                       0x20

#define MMC_BLOCK_SIZE                512

#define MMC_SIGNATURE  SIGNATURE_32 ('a', 'w', 'm', 'c')

typedef struct
{
  UINT32                     Signature;
  EFI_HANDLE                 Handle;
  EFI_BLOCK_IO_PROTOCOL      BlockIo;
  EFI_BLOCK_IO_MEDIA         Media;
  UINT32                     Rca;
  BOOLEAN                    HighCapacity;
} MMC_PRIVATE;

STATIC MMC_PRIVATE  *mPrivate;

#pragma pack(1)
typedef struct
{
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} MMC_DEVICE_PATH;
#pragma pack()

STATIC MMC_DEVICE_PATH  mDevicePath =
{
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP,
      { (UINT8)sizeof (VENDOR_DEVICE_PATH), (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8) } },
      { 0x6b2f4a91, 0x3d7c, 0x4e58, { 0xa1, 0x0c, 0x92, 0xe4, 0x77, 0x35, 0xbd, 0x16 } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

#endif // MMC_DXE_H_
