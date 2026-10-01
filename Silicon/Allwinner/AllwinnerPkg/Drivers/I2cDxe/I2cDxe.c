#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include <Protocol/I2c.h>

#include "I2cDxe.h"

//
// Wait for IFLG
//
STATIC
EFI_STATUS
TwsiWaitIflg (VOID)
{
  UINTN  Elapsed;

  for (Elapsed = 0; Elapsed < I2C_TIMEOUT_US; Elapsed++) {
    if ((MmioRead32 (TWI_CONTROL) & TWI_CTRL_IFLG) != 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  return EFI_TIMEOUT;
}

//
// Do a bus step
//
STATIC
EFI_STATUS
TwsiStep (
  IN UINT32  Control,
  IN UINT32  ExpectedStatus)
{
  EFI_STATUS  Status;
  UINT32      Actual;

  MmioWrite32 (TWI_CONTROL, Control);

  Status = TwsiWaitIflg ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "I2C timeout waiting for IFLG (ctrl 0x%02x)\n", Control));
    return Status;
  }

  Actual = MmioRead32 (TWI_STATUS) & TWI_STAT_MASK;
  if (Actual != ExpectedStatus) {
    DEBUG ((
      EFI_D_ERROR,
      "I2C bad state, expected 0x%02x got 0x%02x\n",
      ExpectedStatus,
      Actual
      ));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

//
// Issue STOP and let the bus settle.
//
STATIC
VOID
TwsiStop (VOID)
{
  UINTN  Elapsed;

  MmioWrite32 (TWI_CONTROL, TWI_CTRL_TWSIEN | TWI_CTRL_STOP | TWI_CTRL_CLEAR_IFLG);

  for (Elapsed = 0; Elapsed < 1000; Elapsed++) {
    if ((MmioRead32 (TWI_CONTROL) & TWI_CTRL_STOP) == 0) {
      return;
    }

    MicroSecondDelay (1);
  }

  DEBUG ((EFI_D_WARN, "I2C did not return to idle after STOP\n"));
}

//
// Do a byte write to I2C
//
EFI_STATUS
I2cWrite (
  IN UINT8  Address,
  IN UINT8  Register,
  IN UINT8  Value)
{
  EFI_STATUS  Status;

  Status = TwsiStep (
             TWI_CTRL_TWSIEN | TWI_CTRL_START | TWI_CTRL_CLEAR_IFLG,
             TWI_STAT_START
             );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  MmioWrite32 (TWI_DATA, (Address << 1) | 0);
  Status = TwsiStep (TWI_CTRL_TWSIEN | TWI_CTRL_CLEAR_IFLG, TWI_STAT_ADDR_W_ACK);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  MmioWrite32 (TWI_DATA, Register);
  Status = TwsiStep (TWI_CTRL_TWSIEN | TWI_CTRL_CLEAR_IFLG, TWI_STAT_DATA_W_ACK);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  MmioWrite32 (TWI_DATA, Value);
  Status = TwsiStep (TWI_CTRL_TWSIEN | TWI_CTRL_CLEAR_IFLG, TWI_STAT_DATA_W_ACK);

Done:
  TwsiStop ();
  return Status;
}

//
// Do a byte read from I2C
//
EFI_STATUS
I2cRead (
  IN  UINT8   Address,
  IN  UINT8  Register,
  OUT UINT8  *Value)
{
  EFI_STATUS  Status;

  Status = TwsiStep (
             TWI_CTRL_TWSIEN | TWI_CTRL_START | TWI_CTRL_CLEAR_IFLG,
             TWI_STAT_START
             );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  MmioWrite32 (TWI_DATA, (Address << 1) | 0);
  Status = TwsiStep (TWI_CTRL_TWSIEN | TWI_CTRL_CLEAR_IFLG, TWI_STAT_ADDR_W_ACK);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  MmioWrite32 (TWI_DATA, Register);
  Status = TwsiStep (TWI_CTRL_TWSIEN | TWI_CTRL_CLEAR_IFLG, TWI_STAT_DATA_W_ACK);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = TwsiStep (
             TWI_CTRL_TWSIEN | TWI_CTRL_START | TWI_CTRL_CLEAR_IFLG,
             TWI_STAT_RSTART
             );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  MmioWrite32 (TWI_DATA, (Address << 1) | 1);
  Status = TwsiStep (TWI_CTRL_TWSIEN | TWI_CTRL_CLEAR_IFLG, TWI_STAT_ADDR_R_ACK);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = TwsiStep (TWI_CTRL_TWSIEN | TWI_CTRL_CLEAR_IFLG, TWI_STAT_DATA_R_NAK);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  *Value = (UINT8)MmioRead32 (TWI_DATA);

Done:
  TwsiStop ();
  return Status;
}

//
// Init I2C
//
STATIC
VOID
I2cInit (VOID)
{
  UINT32  Val;

  MmioOr32 (PRCM_TWI_GATE_RESET, PRCM_TWI_GATE | PRCM_TWI_RESET);
  MicroSecondDelay (10);

  // not adding this to the gpio driver, this is in a whole other register space.
  Val  = MmioRead32 (R_PIO_PL_CFG0);
  Val &= ~(0xFU << 0 | 0xFU << 4);
  Val |= (R_PIO_PL_MUX_R_TWI << 0) | (R_PIO_PL_MUX_R_TWI << 4);
  MmioWrite32 (R_PIO_PL_CFG0, Val);

  MmioWrite32 (TWI_SOFT_RESET, 0);
  MicroSecondDelay (10);

  MmioWrite32 (TWI_BAUDRATE, TWI_BAUD_400K);
  MmioWrite32 (TWI_SLAVE_ADDR, 0);
  MmioWrite32 (TWI_XTND_SLAVE_ADDR, 0);
  MmioWrite32 (TWI_CONTROL, TWI_CTRL_TWSIEN);
  MicroSecondDelay (10);
}

//
// Global Variables
//
STATIC I2C_PROTOCOL mI2cProtocol = {
  I2cRead,
  I2cWrite
};

EFI_STATUS
EFIAPI
I2cDriverEntry (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable)
{
    EFI_STATUS  Status;
    EFI_HANDLE  Handle;
    
    I2cInit ();
    
    Handle = NULL;
    Status = gBS->InstallMultipleProtocolInterfaces (&Handle, &gI2cProtocolGuid, &mI2cProtocol, NULL);
    if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "I2cDxe: failed to install protocol: %r\n", Status));
        return Status;
    }
    
    return EFI_SUCCESS;
}
