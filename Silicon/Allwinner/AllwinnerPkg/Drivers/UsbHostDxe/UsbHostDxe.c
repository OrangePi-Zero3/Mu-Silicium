#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/ClockLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>

#include <Protocol/Gpio.h>

#include <Guid/EventGroup.h>

#include "UsbHost.h"

// Dont bring up PORT 0, it's USB-C
STATIC CONST UINT32  mUsbPorts[] = { 1, 2, 3 };

STATIC EFI_HANDLE  mEhciHandles[ARRAY_SIZE (mUsbPorts)];
STATIC UINTN       mEhciHandleCount = 0;

//
// Init USB port clock
//
STATIC
VOID
UsbPortClockInit (
  IN UINT32  Port
  )
{
  CONST USB_PORT_CLOCKS  *C;
 
  ASSERT (Port < ARRAY_SIZE (mUsbClocks));
  C = &mUsbClocks[Port];
 
  CcmClkEnable  (C->PhyClk);
  CcmClkDisable (C->OhciClk);
 
  CcmResetAssert (C->OhciRst);
  CcmClkDisable  (C->OhciBus);
  CcmClkEnable   (C->EhciBus);
  MicroSecondDelay (10);
 
  CcmResetDeassert (C->PhyRst);
 
  CcmResetAssert (C->EhciRst);
  MicroSecondDelay (10);
  CcmResetDeassert (C->EhciRst);
  MicroSecondDelay (50);
}

/**
  Take one port's PHY out of powerdown and route it to the HCI.
**/
STATIC
VOID
UsbPortPhyInit (
  IN UINT32  Port
  )
{
  UINTN   Pmu = USB_PMU_BASE (Port);
  UINT32  Val;

  Val  = MmioRead32 (Pmu + USB_HCI_PHY_CTL_OFFSET);
  Val &= ~(UINT32)PHY_CTL_SIDDQ;
  MmioWrite32 (Pmu + USB_HCI_PHY_CTL_OFFSET, Val);

  Val  = MmioRead32 (Pmu + USB_PMU_CTRL_OFFSET);
  Val |= PMU_AHB_ICHR8_EN | PMU_AHB_INCR4_BURST_EN |
         PMU_AHB_INCRX_ALIGN_EN | PMU_ULPI_BYPASS_EN;
  MmioWrite32 (Pmu + USB_PMU_CTRL_OFFSET, Val);
}

EFI_STATUS
EFIAPI
UsbHostDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  UINTN Index;
  UINT32 Port;

  // Must have PHY2 initialised, no usb port will work without it.
  UsbPortClockInit (2);
  UsbPortPhyInit (2);

  for (Index = 0; Index < ARRAY_SIZE (mUsbPorts); Index++)
  {
    Port = mUsbPorts[Index];

    if (Port != 2)
    {
      UsbPortClockInit (Port);
      UsbPortPhyInit (Port);
    }

    Status = RegisterNonDiscoverableMmioDevice (Port, NonDiscoverableDeviceTypeEhci, NonDiscoverableDeviceDmaTypeNonCoherent, NULL, &mEhciHandles[mEhciHandleCount], 1, (EFI_PHYSICAL_ADDRESS)mUsbEhciBase[Port], (UINTN)0x100);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "EHCI%u register failed: %r\n", Port, Status));
      continue;
    }

    DEBUG ((EFI_D_WARN, "EHCI%u @ 0x%lx registered\n", Port, (UINT64)mUsbEhciBase[Port]));
    mEhciHandleCount++;
  }

  return EFI_SUCCESS;
}
