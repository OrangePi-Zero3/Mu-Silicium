#ifndef __SPI_DXE_H__
#define __SPI_DXE_H__

// SPI base and registers
#define SPI0_BASE               0x05010000

#define SPI_GCR                 0x04
#define SPI_TCR                 0x08
#define SPI_FIFO_STA            0x1C
#define SPI_MBC                 0x30
#define SPI_MTC                 0x34
#define SPI_BCC                 0x38
#define SPI_TXD                 0x200
#define SPI_RXD                 0x300

// GCR control bits
#define SPI_GCR_ENABLE          BIT0
#define SPI_GCR_MASTER          BIT1
#define SPI_GCR_SRST            BIT31

#define SPI_TCR_XCH             BIT31

// FIFO RX Count Mask
#define SPI_FIFO_RXCNT_MASK     0x7F

// FIFO Depth
#define SPI_FIFO_DEPTH          64

#endif // __SPI_DXE_H__