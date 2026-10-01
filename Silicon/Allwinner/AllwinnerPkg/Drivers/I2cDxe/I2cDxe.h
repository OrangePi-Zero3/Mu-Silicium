#ifndef __I2C_DXE_H__
#define __I2C_DXE_H__

// I2C base and registers
#define R_TWI_BASE            0x07081400
#define TWI_SLAVE_ADDR        (R_TWI_BASE + 0x00)
#define TWI_XTND_SLAVE_ADDR   (R_TWI_BASE + 0x04)
#define TWI_DATA              (R_TWI_BASE + 0x08)
#define TWI_CONTROL           (R_TWI_BASE + 0x0C)
#define TWI_STATUS            (R_TWI_BASE + 0x10)
#define TWI_BAUDRATE          (R_TWI_BASE + 0x14)
#define TWI_SOFT_RESET        (R_TWI_BASE + 0x18)

// TWI control bits
#define TWI_CTRL_ACK          BIT2
#define TWI_CTRL_IFLG         BIT3
#define TWI_CTRL_STOP         BIT4
#define TWI_CTRL_START        BIT5
#define TWI_CTRL_TWSIEN       BIT6

#define TWI_CTRL_CLEAR_IFLG   BIT3

// TWI status codes
#define TWI_STAT_START        0x08
#define TWI_STAT_RSTART       0x10
#define TWI_STAT_ADDR_W_ACK   0x18
#define TWI_STAT_DATA_W_ACK   0x28
#define TWI_STAT_ADDR_R_ACK   0x40
#define TWI_STAT_DATA_R_NAK   0x58
#define TWI_STAT_IDLE         0xF8
#define TWI_STAT_MASK         0xF8

#define TWI_BAUD_400K         0x44

#define PRCM_TWI_GATE_RESET   0x0701019C
#define PRCM_TWI_GATE         BIT0
#define PRCM_TWI_RESET        BIT16

#define I2C_TIMEOUT_US        100000

#define R_PIO_BASE            0x07022000
#define R_PIO_PL_CFG0         (R_PIO_BASE + 0x00)
#define R_PIO_PL_MUX_R_TWI    3

#endif // __I2C_DXE_H__