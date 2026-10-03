#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include <Protocol/Gpio.h>

#include "GpioDxe.h"

VOID
ConfigurePin (
  UINT32 Pin,
  UINT32 PinConfiguration
  )
{
  UINT32 Configuration;
  UINT32 Bank    = GPIO_BANK(Pin);
  UINT32 Bit     = GPIO_CFG_BIT(Pin);
  UINTN  Address = GPIO_CFG0_BASE(Bank) + (GPIO_NUM(Pin) >> 3) * 4;

  ASSERT_PIN (Pin);

  Configuration = MmioRead32(Address);
  Configuration &= ~(0xF << Bit);
  Configuration |= ((PinConfiguration & 0xF) << Bit);

  MmioWrite32(Address, Configuration);
}

VOID
SetPinDrive (
  UINT32 Pin,
  UINT32 Level
  )
{
  UINT32 Configuration;
  UINT32 Bank    = GPIO_BANK(Pin);
  UINT32 Bit     = GPIO_MDR_BIT(Pin);
  UINTN  Address = GPIO_MDR0_BASE(Bank) + (GPIO_NUM(Pin) >> 4) * 4;

  ASSERT_PIN (Pin);

  Configuration = MmioRead32(Address);
  Configuration &= ~(0x3 << Bit);
  Configuration |= ((Level & 0x3) << Bit);

  MmioWrite32(Address, Configuration);
}

VOID
SetPinPull (
  UINT32 Pin,
  UINT32 Level
  )
{
  UINT32 Configuration;
  UINT32 Bank   = GPIO_BANK(Pin);
  UINT32 Offset = GPIO_PULL_OFFSET(Pin);
  UINTN  Address = GPIO_PULL_BASE(Bank) + GPIO_PULL_INDEX(Pin) * 4;

  ASSERT_PIN (Pin);

  Configuration = MmioRead32(Address);
  Configuration &= ~(0x3 << Offset);
  Configuration |= (Level << Offset);

  MmioWrite32(Address, Configuration);
}

VOID
SetPinState (
  UINT32 Pin,
  UINT32 Enable
  )
{
    UINT32 Configuration;
    UINT32 Bank = GPIO_BANK(Pin);
    UINT32 Number = GPIO_NUM(Pin);
    UINT32 Address = GPIO_DAT_BASE(Bank);

    ASSERT_PIN (Pin);

    Configuration = MmioRead32(Address);
    if (Enable)
      Configuration |= (1 << Number);
    else
      Configuration &= ~(1 << Number);

    MmioWrite32(Address, Configuration);
}

UINT32
GetPinState (
  UINT32 Pin
  )
{
    UINT32 Configuration;
    UINT32 Bank = GPIO_BANK(Pin);
    UINT32 Number = GPIO_NUM(Pin);
    UINT32 Address = GPIO_DAT_BASE(Bank);

    ASSERT_PIN (Pin);

    Configuration = MmioRead32(Address);
    return (Configuration >> Number) & 1;
}

STATIC GPIO_PROTOCOL mGpioProtocol = {
  ConfigurePin,
  SetPinPull,
  SetPinState,
  GetPinState,
  SetPinDrive
};

EFI_STATUS
EFIAPI
GpioDriverEntry (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable)
{
    EFI_STATUS  Status;
    EFI_HANDLE  Handle;
        
    Handle = NULL;
    Status = gBS->InstallMultipleProtocolInterfaces (&Handle, &gGpioProtocolGuid, &mGpioProtocol, NULL);
    if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "GpioDxe: failed to install protocol: %r\n", Status));
        return Status;
    }
    
    return EFI_SUCCESS;
}
