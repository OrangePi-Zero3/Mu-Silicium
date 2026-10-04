#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include <Protocol/Spi.h>
#include <Protocol/AwSpiNorFlash.h>

#include "AwSpiNorFlashDxe.h"

//
// Global Variables
//
STATIC SPI_PROTOCOL *mSpiProtocol     = NULL;
STATIC UINT64        mSpiNorFlashSize = 0;

STATIC
CONST CHAR8 *
GetFlashManufacturer (
  IN UINT8 ManufacturerId
  )
{
  switch (ManufacturerId) {
    case 0xEF:
      return "Winbond";
    case 0xC2:
      return "Macronix";
    case 0x20:
      return "Micron";
    case 0x1C:
      return "EON";
    case 0x1F:
      return "Atmel";
    case 0x5E:
      return "Zbit";
    default:
      return "Unknown";
  }
}

//
// Low level helpers
//
STATIC
VOID
SpiNorReadChunk (
  IN UINT32  Address,
  IN UINT8  *Buffer,
  IN UINT32  Length
  )
{
  UINT8 Cmd[4];

  Cmd[0] = NOR_CMD_READ;
  Cmd[1] = (UINT8)((Address >> 16) & 0xFF);
  Cmd[2] = (UINT8)((Address >> 8) & 0xFF);
  Cmd[3] = (UINT8)(Address & 0xFF);

  mSpiProtocol->Transfer (Cmd, sizeof (Cmd), Buffer, Length);
}

STATIC
VOID
SpiNorWriteEnable (
  VOID
  )
{
  UINT8 Cmd = NOR_CMD_WREN;

  mSpiProtocol->Transfer (&Cmd, 1, NULL, 0);
}

STATIC
EFI_STATUS
SpiNorWaitReady (
  IN UINT32 TimeoutMs
  )
{
  UINT8  Cmd = NOR_CMD_RDSR;
  UINT8  Sr;
  UINT32 Elapsed;

  for (Elapsed = 0; Elapsed < TimeoutMs; Elapsed++) {
    mSpiProtocol->Transfer (&Cmd, 1, &Sr, 1);
    if ((Sr & NOR_SR_WIP) == 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1000);
  }

  DEBUG ((EFI_D_ERROR, "SpiNor: timeout waiting for WIP to clear\n"));
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
SpiNorPageProgram (
  IN UINT32        Address,
  IN CONST UINT8  *Buffer,
  IN UINT32        Length
  )
{
  UINT8  Tx[4 + NOR_WRITE_CHUNK_MAX];
  UINT32 I;

  if (Length > NOR_WRITE_CHUNK_MAX) {
    return EFI_INVALID_PARAMETER;
  }

  Tx[0] = NOR_CMD_PP;
  Tx[1] = (UINT8)((Address >> 16) & 0xFF);
  Tx[2] = (UINT8)((Address >> 8) & 0xFF);
  Tx[3] = (UINT8)(Address & 0xFF);
  for (I = 0; I < Length; I++) {
    Tx[4 + I] = Buffer[I];
  }

  SpiNorWriteEnable ();
  mSpiProtocol->Transfer (Tx, 4 + Length, NULL, 0);
  return SpiNorWaitReady (10);
}

STATIC
EFI_STATUS
SpiNorEraseSector (
  IN UINT32 Address
  )
{
  UINT8 Cmd[4];

  Cmd[0] = NOR_CMD_SE;
  Cmd[1] = (UINT8)((Address >> 16) & 0xFF);
  Cmd[2] = (UINT8)((Address >> 8) & 0xFF);
  Cmd[3] = (UINT8)(Address & 0xFF);

  SpiNorWriteEnable ();
  mSpiProtocol->Transfer (Cmd, sizeof (Cmd), NULL, 0);
  return SpiNorWaitReady (1000);
}

//
// Protocol functions
//
STATIC
EFI_STATUS
EFIAPI
SpiNorFlashRead (
  IN  AW_SPI_NOR_FLASH_PROTOCOL *This,
  IN  UINT32                   Address,
  IN  UINT32                   Length,
  OUT UINT8                   *Buffer
  )
{
  if ((This == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((UINT64)Address + Length > This->FlashSize) {
    DEBUG ((EFI_D_ERROR, "SpiNorFlashRead: read beyond flash size\n"));
    return EFI_INVALID_PARAMETER;
  }

  while (Length > 0) {
    UINT32 Chunk = Length > NOR_READ_CHUNK_MAX ? NOR_READ_CHUNK_MAX : Length;

    SpiNorReadChunk (Address, Buffer, Chunk);

    Address += Chunk;
    Buffer  += Chunk;
    Length  -= Chunk;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SpiNorFlashWrite (
  IN  AW_SPI_NOR_FLASH_PROTOCOL *This,
  IN  UINT32                   Address,
  IN  UINT32                   Length,
  IN  CONST UINT8             *Buffer
  )
{
  EFI_STATUS Status;

  if ((This == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((UINT64)Address + Length > This->FlashSize) {
    DEBUG ((EFI_D_ERROR, "SpiNorFlashWrite: write beyond flash size\n"));
    return EFI_INVALID_PARAMETER;
  }

  while (Length > 0) {
    UINT32 Chunk = NOR_PAGE_SIZE - (Address % NOR_PAGE_SIZE);

    if (Chunk > Length) {
      Chunk = Length;
    }

    if (Chunk > NOR_WRITE_CHUNK_MAX) {
      Chunk = NOR_WRITE_CHUNK_MAX;
    }

    Status = SpiNorPageProgram (Address, Buffer, Chunk);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Address += Chunk;
    Buffer  += Chunk;
    Length  -= Chunk;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SpiNorFlashErase (
  IN  AW_SPI_NOR_FLASH_PROTOCOL *This,
  IN  UINT32                   Address,
  IN  UINT32                   Length
  )
{
  EFI_STATUS Status;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (((Address % NOR_SECTOR_SIZE) != 0) ||
      ((Length % NOR_SECTOR_SIZE) != 0) ||
      ((UINT64)Address + Length > This->FlashSize))
  {
    DEBUG ((EFI_D_ERROR, "SpiNorFlashErase: bad range or alignment\n"));
    return EFI_INVALID_PARAMETER;
  }

  while (Length > 0) {
    Status = SpiNorEraseSector (Address);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Address += NOR_SECTOR_SIZE;
    Length  -= NOR_SECTOR_SIZE;
  }

  return EFI_SUCCESS;
}

//
// Identify
//
STATIC
EFI_STATUS
SpiNorIdentify (
  VOID
  )
{
  UINT8 Cmd = NOR_CMD_READ_ID;
  UINT8 Id[3];

  mSpiProtocol->Transfer (&Cmd, sizeof (Cmd), Id, sizeof (Id));

  if (((Id[0] == 0x00) && (Id[1] == 0x00) && (Id[2] == 0x00)) ||
      ((Id[0] == 0xFF) && (Id[1] == 0xFF) && (Id[2] == 0xFF)))
  {
    DEBUG ((EFI_D_ERROR, "SpiNor: no flash detected (ID %02X%02X%02X)\n", Id[0], Id[1], Id[2]));
    return EFI_NOT_FOUND;
  }

  mSpiNorFlashSize = LShiftU64 (1, Id[2] & 0x1F);

  DEBUG ((
    EFI_D_WARN,
    "NOR Flash Manufacturer: %a, Device ID: %02X%02X, Size: %lu bytes\n",
    GetFlashManufacturer (Id[0]),
    Id[1],
    Id[2],
    mSpiNorFlashSize
    ));

  if (mSpiNorFlashSize > SIZE_16MB) {
    DEBUG ((EFI_D_WARN, "SpiNor: flash > 16MB, 3-byte addressing will only reach the first 16MB\n"));
  }

  return EFI_SUCCESS;
}

//
// Protocol instance
//
STATIC AW_SPI_NOR_FLASH_PROTOCOL  mAwSpiNorFlashProtocol = {
  SpiNorFlashRead,
  SpiNorFlashWrite,
  SpiNorFlashErase,
  0,
  NOR_PAGE_SIZE,
  NOR_SECTOR_SIZE
};

EFI_STATUS
EFIAPI
SpiNorFlashDriverEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Status = gBS->LocateProtocol (&gSpiProtocolGuid, NULL, (VOID **)&mSpiProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate SPI Protocol\n"));
    return EFI_NOT_FOUND;
  }

  Status = SpiNorIdentify ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mAwSpiNorFlashProtocol.FlashSize = mSpiNorFlashSize;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gAwSpiNorFlashProtocolGuid,
                  &mAwSpiNorFlashProtocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SpiNorFlashDxe: failed to install protocol: %r\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}