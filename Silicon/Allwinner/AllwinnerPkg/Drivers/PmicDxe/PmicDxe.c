#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>

#include <Protocol/AllwinnerPmic.h>
#include <Protocol/I2c.h>

#include "PmicDxe.h"

//
// Global Variables
//
STATIC ALLWINNER_PMIC_PROTOCOL  mPmicProtocol;
STATIC I2C_PROTOCOL             *mI2cProtocol = NULL;

STATIC
UINT8
MilliVoltToCode (
  IN UINT32  MilliVolt)
{
  if (MilliVolt > AXP313_SPLIT_MV) {
    return (UINT8)(AXP313_SPLIT_CODE + (MilliVolt - (AXP313_SPLIT_MV + 20)) / 20);
  }

  return (UINT8)((MilliVolt - 500) / 10);
}

STATIC
UINT32
CodeToMilliVolt (
  IN UINT8  Code)
{
  if (Code >= AXP313_SPLIT_CODE) {
    return (AXP313_SPLIT_MV + 20) + ((UINT32)Code - AXP313_SPLIT_CODE) * 20;
  }

  return 500 + (UINT32)Code * 10;
}

STATIC
EFI_STATUS
EFIAPI
PmicSetDcdcMilliVolt (
  IN ALLWINNER_PMIC_PROTOCOL  *This,
  IN UINT8                     Dcdc,
  IN UINT32                    MilliVolt)
{
  EFI_STATUS  Status;
  UINT32      MaxMilliVolt;
  UINT8       OutputCtrl;
  UINT8       EnableMask;

  switch (Dcdc) {
    case 1:
    case 2:
      MaxMilliVolt = AXP313_DCDC12_MAX_MV;
      break;
    case 3:
      MaxMilliVolt = AXP313_DCDC3_MAX_MV;
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  if (MilliVolt < 500 || MilliVolt > MaxMilliVolt) {
    return EFI_INVALID_PARAMETER;
  }

  Status = mI2cProtocol->Write (AXP313_I2C_ADDR, AXP313_DCDC1_CTRL + Dcdc - 1, MilliVoltToCode (MilliVolt));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = mI2cProtocol->Read (AXP313_I2C_ADDR, AXP313_OUTPUT_CTRL, &OutputCtrl);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  EnableMask = (UINT8)(1U << (Dcdc - 1));
  if ((OutputCtrl & EnableMask) != 0) {
    return EFI_SUCCESS;
  }

  return mI2cProtocol->Write (AXP313_I2C_ADDR, AXP313_OUTPUT_CTRL, OutputCtrl | EnableMask);
}

STATIC
EFI_STATUS
EFIAPI
PmicGetDcdcMilliVolt (
  IN  ALLWINNER_PMIC_PROTOCOL  *This,
  IN  UINT8                     Dcdc,
  OUT UINT32                   *MilliVolt)
{
  EFI_STATUS  Status;
  UINT8       Code;

  if (Dcdc < 1 || Dcdc > 3 || MilliVolt == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = mI2cProtocol->Read (AXP313_I2C_ADDR, AXP313_DCDC1_CTRL + Dcdc - 1, &Code);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *MilliVolt = CodeToMilliVolt (Code);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
PmicDxeEntry (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable)
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;
  UINT8       ChipId;
  UINT32      MilliVolt;

  // Look for I2C protocol, which is the only way to talk to the PMIC. If it is not present, the board is not ready to clock up the CPU and we should stay quiet.
  Status = gBS->LocateProtocol (&gI2cProtocolGuid, NULL, (VOID *)&mI2cProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Locate I2C Protocol\n"));
    return EFI_NOT_FOUND;
  }
  
  Status = mI2cProtocol->Read (AXP313_I2C_ADDR, AXP313_CHIP_VERSION, &ChipId);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PmicDxe: no response from PMIC at 0x%02x - %r\n", AXP313_I2C_ADDR, Status));
    return EFI_SUCCESS;
  }

  ChipId &= AXP313_VERSION_MASK;
  if (ChipId != AXP313_VERSION_313A &&
      ChipId != AXP313_VERSION_313B &&
      ChipId != AXP313_VERSION_1530) {
    DEBUG ((DEBUG_ERROR, "PmicDxe: This is not AXP313! (id 0x%02x)\n", ChipId));
    return EFI_SUCCESS;
  }

  Status = PmicGetDcdcMilliVolt (&mPmicProtocol, 2, &MilliVolt);
  DEBUG ((
    DEBUG_INFO,
    "PmicDxe: AXP313A found (id 0x%02x), vdd-cpu currently %u mV\n",
    ChipId,
    EFI_ERROR (Status) ? 0 : MilliVolt
    ));

  mPmicProtocol.SetDcdcMilliVolt = PmicSetDcdcMilliVolt;
  mPmicProtocol.GetDcdcMilliVolt = PmicGetDcdcMilliVolt;

  Handle = NULL;
  return gBS->InstallMultipleProtocolInterfaces (
                &Handle,
                &gAllwinnerPmicProtocolGuid, &mPmicProtocol,
                NULL
                );
}
