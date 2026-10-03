#ifndef __GPIO_PROTOCOL_H__
#define __GPIO_PROTOCOL_H__

#define GPIO_PROTOCOL_GUID \
  { 0xac669f62, 0x88c9, 0x4f64, { 0xbe, 0xff, 0xc7, 0x80, 0xbf, 0x37, 0x1f, 0x53 } }

// GPIO banks
#define SUNXI_GPIO_A    0 // Unused
#define SUNXI_GPIO_B    1 // Unused
#define SUNXI_GPIO_C    2
#define SUNXI_GPIO_D    3 // Unused
#define SUNXI_GPIO_E    4 // Unused
#define SUNXI_GPIO_F    5
#define SUNXI_GPIO_G    6
#define SUNXI_GPIO_H    7
#define SUNXI_GPIO_I    8

// GPIO bank sizes
#define SUNXI_GPIO_A_NR    (32) // Unused
#define SUNXI_GPIO_B_NR    (32) // Unused
#define SUNXI_GPIO_C_NR    (32)
#define SUNXI_GPIO_D_NR    (32) // Unused
#define SUNXI_GPIO_E_NR    (32) // Unused
#define SUNXI_GPIO_F_NR    (32)
#define SUNXI_GPIO_G_NR    (32)
#define SUNXI_GPIO_H_NR    (32)
#define SUNXI_GPIO_I_NR    (32)

#define SUNXI_GPIO_NEXT(__gpio) ((__gpio##_START) + (__gpio##_NR) + 0)

enum sunxi_gpio_number {
	SUNXI_GPIO_A_START = 0, // Unused
	SUNXI_GPIO_B_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_A), // Unused
	SUNXI_GPIO_C_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_B), // Unused
	SUNXI_GPIO_D_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_C),
	SUNXI_GPIO_E_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_D), // Unused
	SUNXI_GPIO_F_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_E), // Unused
	SUNXI_GPIO_G_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_F),
	SUNXI_GPIO_H_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_G),
	SUNXI_GPIO_I_START = SUNXI_GPIO_NEXT(SUNXI_GPIO_H),
};

// SUNXI GPIO number definitions
#define SUNXI_GPC(_nr)          (SUNXI_GPIO_C_START + (_nr))
#define SUNXI_GPF(_nr)          (SUNXI_GPIO_F_START + (_nr))
#define SUNXI_GPG(_nr)          (SUNXI_GPIO_G_START + (_nr))
#define SUNXI_GPH(_nr)          (SUNXI_GPIO_H_START + (_nr))
#define SUNXI_GPI(_nr)          (SUNXI_GPIO_I_START + (_nr))

// GPIO pin function config
#define SUNXI_GPIO_INPUT        (0)
#define SUNXI_GPIO_OUTPUT       (1)
#define MUX_2					(2)
#define MUX_3					(3)
#define MUX_4					(4)
#define MUX_5					(5)
#define MUX_6					(6)

// GPIO Multi Drive Select
#define SUNXI_DRIVE_L0		(0)
#define SUNXI_DRIVE_L1		(1)
#define SUNXI_DRIVE_L2		(2)
#define SUNXI_DRIVE_L3		(3)

// GPIO pin pull-up/down config
#define SUNXI_GPIO_PULL_DISABLE (0)
#define SUNXI_GPIO_PULL_UP      (1)
#define SUNXI_GPIO_PULL_DOWN    (2)

typedef struct _GPIO_PROTOCOL GPIO_PROTOCOL;

typedef
VOID
(EFIAPI *GPIO_CONFIGURE_PIN) (
  IN UINT32  Pin,
  IN UINT32  PinConfiguration
  );

typedef
VOID
(EFIAPI *GPIO_SET_PIN_DRIVE) (
  IN UINT32  Pin,
  IN UINT32  Drive
  );

typedef
VOID
(EFIAPI *GPIO_SET_PIN_PULL) (
  IN UINT32  Pin,
  IN UINT32  Pull
  );

typedef
VOID
(EFIAPI *GPIO_SET_PIN_STATE) (
  IN UINT32  Pin,
  IN UINT32  Enable
  );

typedef
UINT32
(EFIAPI *GPIO_GET_PIN_STATE) (
  IN UINT32  Pin
  );

struct _GPIO_PROTOCOL {
  GPIO_CONFIGURE_PIN  ConfigurePin;
  GPIO_SET_PIN_PULL   SetPinPull;
  GPIO_SET_PIN_STATE  SetPinState;
  GPIO_GET_PIN_STATE  GetPinState;
  GPIO_SET_PIN_DRIVE  SetPinDrive;
};

extern EFI_GUID gGpioProtocolGuid;

#endif // __GPIO_PROTOCOL_H__
