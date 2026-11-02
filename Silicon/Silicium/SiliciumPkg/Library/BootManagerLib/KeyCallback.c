/**
  Copyright (C) Microsoft Corporation. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include <Protocol/SimpleTextInEx.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/Usb2HostController.h>

#include "BootManager.h"

//
// Global Variables
//
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *mSimpleInExProtocol  = NULL;
STATIC VOID                              *mNotifyHandle        = NULL;
STATIC BOOLEAN                            mEnterBootManager    = FALSE;

STATIC
VOID
ConnectUsbHostControllers (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (!EFI_ERROR (Status)) {
    for (UINTN Index = 0; Index < HandleCount; Index++) {
      Status = gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    }

    FreePool (Handles);
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiUsb2HcProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (UINTN Index = 0; Index < HandleCount; Index++) {
    Status = gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }

  FreePool (Handles);
}

EFI_STATUS
EFIAPI
KeyNotify (IN EFI_KEY_DATA *KeyData)
{
  // Toggle Boot Manager Flag
  mEnterBootManager = TRUE;

  return EFI_SUCCESS;
}

EFI_STATUS
RegisterKeyNotify ()
{
  EFI_STATUS   Status;
  EFI_KEY_DATA KeyData;

  // Set Key Data
  KeyData.KeyState.KeyToggleState = 0;
  KeyData.KeyState.KeyShiftState  = 0;
  KeyData.Key.UnicodeChar         = CHAR_NULL;
  KeyData.Key.ScanCode            = SCAN_F10;

  // Register F10 Key Notify
  Status = mSimpleInExProtocol->RegisterKeyNotify (mSimpleInExProtocol, &KeyData, &KeyNotify, &mNotifyHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Register F10 Key Notify! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
RegisterKeyCallback (IN EFI_DEVICE_PATH_PROTOCOL *DevicePath)
{
  EFI_STATUS Status;

  (VOID)DevicePath;

  // Connect USB controllers before registering the console notification.
  ConnectUsbHostControllers ();

  Status = gBS->HandleProtocol (
                  gST->ConsoleInHandle,
                  &gEfiSimpleTextInputExProtocolGuid,
                  (VOID **)&mSimpleInExProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to get console input protocol! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Register Key Notify
  Status = RegisterKeyNotify ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

VOID
UnregisterKeyCallback ()
{
  EFI_STATUS Status;

  // Verify STI Protocol Presense & Key Notify
  if (mSimpleInExProtocol == NULL || mNotifyHandle == NULL) {
    return;
  }

  // Unregister F10 Key Notify
  Status = mSimpleInExProtocol->UnregisterKeyNotify (mSimpleInExProtocol, mNotifyHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Unregister F10 Key Notify! Status = %r\n", __FUNCTION__, Status));
  }
}

BOOLEAN
EnterBootManager ()
{
  // Pass Boot Manager Flag
  return mEnterBootManager;
}
