#ifndef __GPIO_DXE_H__
#define __GPIO_DXE_H__

// GPIO base and registers
#define H6_PIO_BASE			0x0300B000
#define H6_PIO_BANK_SIZE	0x24
#define H6_PIO_DAT_OFF		0x10
#define H6_PIO_PULL_OFF		0x1C

// Helpers to decode banks and pin numbers
#define GPIO_BANK(pin)		((pin) >> 5)
#define GPIO_NUM(pin)		((pin) & 0x1F)

// GPIO register addresses and bit positions
#define GPIO_CFG0_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE))
#define GPIO_CFG1_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE + 0x4))
#define GPIO_CFG2_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE + 0x8))
#define GPIO_CFG3_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE + 0xC))
#define GPIO_CFG_BIT(pin)	    ((GPIO_NUM(pin) & 0x7) << 2)

#define GPIO_MDR0_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE + 0x14))
#define GPIO_MDR1_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE + 0x18))
#define GPIO_MDR_BIT(pin)	    (GPIO_NUM(pin) * 2)

#define GPIO_PULL_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE + H6_PIO_PULL_OFF))
#define GPIO_PULL_INDEX(pin)	(GPIO_NUM(pin) >> 4)
#define GPIO_PULL_OFFSET(pin)	((GPIO_NUM(pin) & 0xF) << 1)

#define GPIO_DAT_BASE(bank)	((H6_PIO_BASE + (bank) * H6_PIO_BANK_SIZE + H6_PIO_DAT_OFF))

#endif // __I2C_DXE_H__