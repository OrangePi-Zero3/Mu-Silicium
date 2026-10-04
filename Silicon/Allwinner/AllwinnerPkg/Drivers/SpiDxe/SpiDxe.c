#include <Uefi.h>

#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include <Protocol/Spi.h>
#include <Protocol/Gpio.h>

#include "SpiDxe.h"

//
// Global Variables
//
STATIC GPIO_PROTOCOL *mGpioProtocol = NULL;

STATIC
CONST
UINT32
SpiPins[4] =
{
  SUNXI_GPC(0), SUNXI_GPC(2), SUNXI_GPC(3), SUNXI_GPC(4),
};

//
// Init SPI
//
STATIC
VOID
SpiInit (VOID)
{
  for (INTN i = 0; i < 4; i++)
    mGpioProtocol->ConfigurePin(SpiPins[i], MUX_4);

  MmioWrite32(SPI0_BASE + SPI_GCR, MmioRead32(SPI0_BASE + SPI_GCR) | SPI_GCR_MASTER | SPI_GCR_ENABLE | SPI_GCR_SRST);

  while (MmioRead32(SPI0_BASE + SPI_GCR) & SPI_GCR_SRST)
    ;

  DEBUG ((EFI_D_WARN, "SPI initialized\n"));
}

STATIC
EFI_STATUS
EFIAPI
SpiTransfer (
  IN  CONST UINT8  *Tx,
  IN  UINT32        TxLen,
  OUT UINT8        *Rx,
  IN  UINT32        RxLen
  )
{
  UINT32 i;

  if ((TxLen + RxLen) > SPI_FIFO_DEPTH) {
    return EFI_INVALID_PARAMETER;
  }

  if (((TxLen != 0) && (Tx == NULL)) || ((RxLen != 0) && (Rx == NULL))) {
    return EFI_INVALID_PARAMETER;
  }

  MmioWrite32(SPI0_BASE + SPI_MBC, TxLen + RxLen);
  MmioWrite32(SPI0_BASE + SPI_MTC, TxLen);
  MmioWrite32(SPI0_BASE + SPI_BCC, TxLen);

  for (i = 0; i < TxLen; i++)
    MmioWrite8(SPI0_BASE + SPI_TXD, Tx[i]);

  MmioWrite32(SPI0_BASE + SPI_TCR, MmioRead32(SPI0_BASE + SPI_TCR) | SPI_TCR_XCH);
  while ((MmioRead32(SPI0_BASE + SPI_FIFO_STA) & SPI_FIFO_RXCNT_MASK) < TxLen + RxLen)
    ;

  for (i = 0; i < TxLen; i++)
    MmioRead8(SPI0_BASE + SPI_RXD);

  for (i = 0; i < RxLen; i++)
    Rx[i] = MmioRead8(SPI0_BASE + SPI_RXD);

  return EFI_SUCCESS;
}

//
// Global Variables
//
STATIC SPI_PROTOCOL mSpiProtocol = {
  SpiTransfer
};

EFI_STATUS
EFIAPI
SpiDriverEntry (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable)
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Status = gBS->LocateProtocol (&gGpioProtocolGuid, NULL, (VOID *)&mGpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Locate GPIO Protocol\n"));
    return EFI_NOT_FOUND;
  }

  SpiInit ();

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (&Handle, &gSpiProtocolGuid, &mSpiProtocol, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SpiDxe: failed to install protocol: %r\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}