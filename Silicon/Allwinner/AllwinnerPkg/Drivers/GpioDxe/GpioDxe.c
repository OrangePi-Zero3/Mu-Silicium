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
  UINT32 Bank = GPIO_BANK(Pin);
  UINT32 Number = GPIO_NUM(Pin);
  UINT32 Bit = GPIO_CFG_BIT(Pin);
  UINT32 Address = 0;
  
  switch (Bank) {
    case SUNXI_GPIO_C:
      switch (Number) {
        case 0 ... 7:
          Address = GPIO_CFG0_BASE(Bank);
          break;
        case 8 ... 15:
          Address = GPIO_CFG1_BASE(Bank);
          break;
        case 16:
          Address = GPIO_CFG2_BASE(Bank);
          break;
        default:
          break;
      }
    break;
    case SUNXI_GPIO_F:
      switch (Number) {
        case 0 ... 6:
          Address = GPIO_CFG0_BASE(Bank);
          break;
        default:
          break;
      }
      break;
    case SUNXI_GPIO_G:
      switch (Number) {
        case 0 ... 7:
          Address = GPIO_CFG0_BASE(Bank);
          break;
        case 8 ... 15:
          Address = GPIO_CFG1_BASE(Bank);
          break;
        case 16 ... 19:
          Address = GPIO_CFG2_BASE(Bank);
          break;
        default:
          break;
      }
      break;
    case SUNXI_GPIO_H:
      switch (Number) {
        case 0 ... 7:
          Address = GPIO_CFG0_BASE(Bank);
          break;
        case 8 ... 10:
          Address = GPIO_CFG1_BASE(Bank);
          break;
        default:
          break;
      }
      break;
    case SUNXI_GPIO_I:
        switch (Number) {
          case 0 ... 7:
            Address = GPIO_CFG0_BASE(Bank);
            break;
          case 8 ... 15:
            Address = GPIO_CFG1_BASE(Bank);
            break;
          case 16 ... 19:
            Address = GPIO_CFG2_BASE(Bank);
            break;
          default:
              break;
        }
      break;
    default:
      break;
  }
  
  if (!Address)
    return;
  
  Configuration = MmioRead32(Address);
  Configuration &= ~(0x7 << Bit);
  Configuration |= (PinConfiguration << Bit);
  
  MmioWrite32(Address, Configuration);
}

VOID
SetPinDrive (
  UINT32 Pin,
  UINT32 Level
  )
{
  UINT32 Configuration;
  UINT32 Bank = GPIO_BANK(Pin);
  UINT32 Number = GPIO_NUM(Pin);
  UINT32 Bit = GPIO_MDR_BIT(Pin);
  UINT32 Address = 0;

  switch (Bank) {
    case SUNXI_GPIO_C:
      switch (Number) {
          case 0 ... 15:
            Address = GPIO_MDR0_BASE(Bank);
            break;
          case 16:
            Address = GPIO_MDR1_BASE(Bank);
            break;
          default:
            break;
      }
      break;
    case SUNXI_GPIO_F:
      switch (Number) {
          case 0 ... 6:
            Address = GPIO_MDR0_BASE(Bank);
            break;
          default:
            break;
      }
      break;
    case SUNXI_GPIO_G:
      switch (Number) {
          case 0 ... 15:
            Address = GPIO_MDR0_BASE(Bank);
            break;
          case 16 ... 19:
            Address = GPIO_MDR1_BASE(Bank);
            break;
          default:
            break;
      }
      break;
    case SUNXI_GPIO_H:
      switch (Number) {
          case 0 ... 10:
            Address = GPIO_MDR0_BASE(Bank);
            break;
          default:
            break;
      }
      break;
    case SUNXI_GPIO_I:
      switch (Number) {
        case 0 ... 15:
          Address = GPIO_MDR0_BASE(Bank);
          break;
        case 16:
          Address = GPIO_MDR1_BASE(Bank);
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  if (!Address)
    return;

  Configuration = MmioRead32(Address);
  Configuration &= ~(0x3 << Bit);
  Configuration |= (Level << Bit);

  MmioWrite32(Address, Configuration);
}

VOID
SetPinPull(
  UINT32 Pin,
  UINT32 Level
)
{
  UINT32 Configuration;
  UINT32 Bank = GPIO_BANK(Pin);
  UINT32 Number = GPIO_NUM(Pin);
  UINT32 Index = GPIO_PULL_INDEX(Pin);
  UINT32 Offset = GPIO_PULL_OFFSET(Pin);
  UINT32 Address = GPIO_PULL_BASE(Bank) + Index;

  switch (Bank) {
    case SUNXI_GPIO_C:
      switch (Number) {
        case 0 ... 15:
          Address = GPIO_MDR0_BASE(Bank);
          break;
        case 16:
          Address = GPIO_MDR1_BASE(Bank);
          break;
        default:
          break;
      }
      break;
    case SUNXI_GPIO_F:
      switch (Number) {
        case 0 ... 6:
          Address = GPIO_MDR0_BASE(Bank);
          break;
        default:
          break;
      }
      break;
    case SUNXI_GPIO_G:
      switch (Number) {
          case 0 ... 15:
            Address = GPIO_MDR0_BASE(Bank);
            break;
          case 16 ... 19:
            Address = GPIO_MDR1_BASE(Bank);
            break;
          default:
            break;
      }
      break;
    case SUNXI_GPIO_H:
      switch (Number) {
          case 0 ... 10:
            Address = GPIO_MDR0_BASE(Bank);
            break;
          default:
            break;
      }
      break;
    case SUNXI_GPIO_I:
      switch (Number) {
          case 0 ... 15:
            Address = GPIO_MDR0_BASE(Bank);
            break;
          case 16:
            Address = GPIO_MDR1_BASE(Bank);
            break;
          default:
            break;
      }
      break;
    default:
      break;
  }

  if (!Address)
    return;

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

    Configuration = MmioRead32(Address);
    return (Configuration >> Number) & 1;
}

STATIC GPIO_PROTOCOL mGpioProtocol = {
  ConfigurePin,
  SetPinPull,
  SetPinState,
  GetPinState
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
