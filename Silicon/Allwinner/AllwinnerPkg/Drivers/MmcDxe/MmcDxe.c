#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/ClockLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/Gpio.h>

#include "MmcDxe.h"

//
// Global Variables
//
STATIC GPIO_PROTOCOL *mGpioProtocol = NULL;

//
// Update MMC Clock
//
STATIC
EFI_STATUS
MmcUpdateClock (
  VOID)
{
  UINTN  Elapsed;

  MmioWrite32 (MMC0_BASE + MMC_CMD, CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRE_OVER);

  for (Elapsed = 0; Elapsed < 2000000; Elapsed++)
  {
    if ((MmioRead32 (MMC0_BASE + MMC_CMD) & CMD_START) == 0)
    {
      MmioWrite32 (MMC0_BASE + MMC_RINT, MmioRead32 (MMC0_BASE + MMC_RINT));
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  DEBUG ((EFI_D_ERROR, "MmcDxe: clock update timed out\n"));
  return EFI_TIMEOUT;
}

//
// Set MMC clock
//
STATIC
EFI_STATUS
MmcSetClock (
  IN UINT32  Hz)
{
  EFI_STATUS  Status;
  UINT32      Div;
  UINT32      N;
  UINT32      Clkcr;

  ASSERT (Hz <= 24000000);

  Div = 24000000 / Hz;
  if ((24000000 % Hz) != 0)
    Div++;

  N = 0;
  while (Div > 16)
  {
    N++;
    Div = (Div + 1) / 2;
  }

  if (N > 3)
    return EFI_INVALID_PARAMETER;

  // Stop the card clock while the source is retuned.
  Clkcr = MmioRead32 (MMC0_BASE + MMC_CLKCR) & ~CLK_ENABLE;
  MmioWrite32 (MMC0_BASE + MMC_CLKCR, Clkcr);

  Status = MmcUpdateClock ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CcmClkConfigure (CLK_MMC0, (0 << 24), Div - 1, N);

  Clkcr &= ~CLK_DIVIDER_MASK;
  MmioWrite32 (MMC0_BASE + MMC_CLKCR, Clkcr);
  MmioWrite32 (MMC0_BASE + MMC_SAMP_DL, SAMP_DL_SW_EN);

  Clkcr |= CLK_ENABLE;
  MmioWrite32 (MMC0_BASE + MMC_CLKCR, Clkcr);

  return MmcUpdateClock ();
}

//
// Wait for interrupt bit, or return on error.
//
STATIC
EFI_STATUS
MmcWaitRint (
  IN UINT32  DoneBit,
  IN UINTN   TimeoutUs)
{
  UINT32  Rint;
  UINTN   Elapsed;

  for (Elapsed = 0; Elapsed < TimeoutUs; Elapsed++)
  {
    Rint = MmioRead32 (MMC0_BASE + MMC_RINT);

    if ((Rint & RINT_ERROR_MASK) != 0)
    {
      DEBUG ((EFI_D_ERROR, "MmcDxe: rint error 0x%08x\n", Rint));
      return EFI_DEVICE_ERROR;
    }

    if ((Rint & DoneBit) != 0)
      return EFI_SUCCESS;

    MicroSecondDelay (1);
  }

  return EFI_TIMEOUT;
}

//
// Transfer data through PIO
//
STATIC
EFI_STATUS
MmcTransferPio (
  IN OUT UINT32   *Buffer,
  IN     UINTN     WordCount,
  IN     BOOLEAN   Reading)
{
  UINT32  StatusBit;
  UINT32  Status;
  UINTN   Index;
  UINTN   InFifo;
  UINTN   Elapsed;

  StatusBit = Reading ? STATUS_FIFO_EMPTY : STATUS_FIFO_FULL;

  MmioWrite32 (MMC0_BASE + MMC_GCTRL, MmioRead32 (MMC0_BASE + MMC_GCTRL) | GCTRL_ACCESS_BY_AHB);

  Index = 0;
  while (Index < WordCount)
  {
    for (Elapsed = 0; ; Elapsed++) {
      Status = MmioRead32 (MMC0_BASE + MMC_STATUS);
      if ((Status & StatusBit) == 0)
        break;

      if (Elapsed >= 2000000)
      {
        DEBUG ((EFI_D_ERROR, "MmcDxe: FIFO stalled at word %lu\n", (UINT64)Index));
        return EFI_TIMEOUT;
      }

      MicroSecondDelay (1);
    }

    if (!Reading)
    {
      MmioWrite32 (MMC0_BASE + MMC_FIFO, Buffer[Index++]);
      continue;
    }

    InFifo = STATUS_FIFO_LEVEL (Status);
    if (InFifo == 0)
      InFifo = 1;

    while (InFifo > 0 && Index < WordCount)
    {
      Buffer[Index++] = MmioRead32 (MMC0_BASE + MMC_FIFO);
      InFifo--;
    }
  }

  return EFI_SUCCESS;
}

//
// Wait for card DAT0 to be high, or return on timeout.
//
STATIC
VOID
MmcWaitNotBusy (
  VOID)
{
  UINTN  Elapsed;

  for (Elapsed = 0; Elapsed < 500000; Elapsed++)
  {
    if ((MmioRead32 (MMC0_BASE + MMC_STATUS) & STATUS_CARD_DATA_BUSY) == 0)
      return;

    MicroSecondDelay (1);
  }

  DEBUG ((EFI_D_WARN, "MmcDxe: card still busy after 500ms\n"));
}

//
// Send an MMC/SD command to host controller
//
STATIC
EFI_STATUS
MmcSendCommand (
  IN     UINT32   Index,
  IN     UINT32   Argument,
  IN     UINT32   RespType,
  IN OUT VOID    *Buffer      OPTIONAL,
  IN     UINTN    Blocks,
  IN     BOOLEAN  Write,
  OUT    UINT32  *Response    OPTIONAL)
{
  EFI_STATUS  Status;
  UINT32      CmdVal;
  UINTN       Elapsed;

  MmcWaitNotBusy ();

  MmioWrite32 (MMC0_BASE + MMC_RINT, 0xFFFFFFFF);

  CmdVal = CMD_START | Index;

  if (Index == CMD_GO_IDLE_STATE)
    CmdVal |= CMD_SEND_INIT_SEQ;

  if (RespType != RESP_NONE)
    CmdVal |= CMD_RESP_EXPIRE;

  if (RespType == RESP_R2)
    CmdVal |= CMD_LONG_RESPONSE;

  if (RespType != RESP_NONE && RespType != RESP_R3)
    CmdVal |= CMD_CHK_RESPONSE_CRC;

  if (Buffer != NULL) {
    CmdVal |= CMD_DATA_EXPIRE | CMD_WAIT_PRE_OVER;

    if (Write)
      CmdVal |= CMD_WRITE;

    if (Blocks > 1)
      CmdVal |= CMD_AUTO_STOP;

    MmioWrite32 (MMC0_BASE + MMC_BLKSZ, MMC_BLOCK_SIZE);
    MmioWrite32 (MMC0_BASE + MMC_BYTECNT, (UINT32)(Blocks * MMC_BLOCK_SIZE));
  }

  MmioWrite32 (MMC0_BASE + MMC_ARG, Argument);
  MmioWrite32 (MMC0_BASE + MMC_CMD, CmdVal);

  if (Buffer != NULL) {
    Status = MmcTransferPio ((UINT32 *)Buffer, (Blocks * MMC_BLOCK_SIZE) / sizeof (UINT32), !Write);
    if (EFI_ERROR (Status))
    {
      goto Fail;
    }
  }

  Status = MmcWaitRint (RINT_COMMAND_DONE, 1000000);
  if (EFI_ERROR (Status))
  {
    goto Fail;
  }

  if (Buffer != NULL)
  {
    Status = MmcWaitRint ((Blocks > 1) ? RINT_AUTO_COMMAND_DONE : RINT_DATA_OVER, 2000000);
    if (EFI_ERROR (Status))
    {
      goto Fail;
    }
  }

  if (RespType == RESP_R1B)
  {
    for (Elapsed = 0; ; Elapsed++)
    {
      if ((MmioRead32 (MMC0_BASE + MMC_STATUS) & STATUS_CARD_DATA_BUSY) == 0)
      {
        break;
      }

      if (Elapsed >= 2000000)
      {
        Status = EFI_TIMEOUT;
        goto Fail;
      }

      MicroSecondDelay (1);
    }
  }

  if (Response != NULL)
  {
    if (RespType == RESP_R2)
    {
      Response[0] = MmioRead32 (MMC0_BASE + MMC_RESP3);
      Response[1] = MmioRead32 (MMC0_BASE + MMC_RESP2);
      Response[2] = MmioRead32 (MMC0_BASE + MMC_RESP1);
      Response[3] = MmioRead32 (MMC0_BASE + MMC_RESP0);
    }
    else
    {
      Response[0] = MmioRead32 (MMC0_BASE + MMC_RESP0);
    }
  }

  MmioWrite32 (MMC0_BASE + MMC_RINT, 0xFFFFFFFF);
  MmioWrite32 (MMC0_BASE + MMC_GCTRL, MmioRead32 (MMC0_BASE + MMC_GCTRL) | GCTRL_FIFO_RESET);
  return EFI_SUCCESS;

Fail:
  DEBUG ((EFI_D_ERROR, "CMD%u failed (%r) rint 0x%08x status 0x%08x\n", Index, Status, MmioRead32 (MMC0_BASE + MMC_RINT), MmioRead32 (MMC0_BASE + MMC_STATUS)));

  MmioWrite32 (MMC0_BASE + MMC_GCTRL, GCTRL_RESET);

  for (Elapsed = 0; Elapsed < 100000; Elapsed++)
  {
    if ((MmioRead32 (MMC0_BASE + MMC_GCTRL) & GCTRL_RESET) == 0)
    {
      break;
    }

    MicroSecondDelay (1);
  }

  MmioWrite32 (MMC0_BASE + MMC_RINT, 0xFFFFFFFF);
  MmcUpdateClock ();
  return Status;
}

//
// Initialise MMC gpios, clocks and controller.
//
STATIC
EFI_STATUS
MmcControllerInit (
  VOID)
{
  for (UINT32 Pin = 0; Pin <= 5; Pin++)
  {
    mGpioProtocol->ConfigurePin (SUNXI_GPF(Pin), MUX_2);
    mGpioProtocol->SetPinPull (SUNXI_GPF(Pin), SUNXI_GPIO_PULL_UP);
  }

  CcmBusEnable (CLK_BUS_MMC0, RST_BUS_MMC0);
  MicroSecondDelay (100);

  MmioWrite32 (MMC0_BASE + MMC_GCTRL, GCTRL_RESET);
  MicroSecondDelay (1000);

  if ((MmioRead32 (MMC0_BASE + MMC_GCTRL) & GCTRL_RESET) != 0)
  {
    DEBUG ((EFI_D_ERROR, "Failed to reset MMC controller\n"));
    return EFI_DEVICE_ERROR;
  }

  MmioWrite32 (MMC0_BASE + MMC_RINT, 0xFFFFFFFF);
  MmioWrite32 (MMC0_BASE + MMC_WIDTH, 0);

  return MmcSetClock (400000);
}

//
// Card identification, super simplified though.
//
STATIC
EFI_STATUS
MmcCardInit (
  VOID)
{
  EFI_STATUS  Status;
  UINT32      Response[4];
  UINT32      Ocr = 0x00FF8000;
  UINT32      Csd[4];
  UINT32      CsdStructure;
  UINT64      Blocks;
  UINTN       Retry;
  BOOLEAN     Version2;

  Status = MmcSendCommand (CMD_GO_IDLE_STATE, 0, RESP_NONE, NULL, 0, FALSE, NULL);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "CMD_GO_IDLE_STATE failed: %r\n", Status));
    return Status;
  }

  MicroSecondDelay (2000);

  Version2 = FALSE;
  Status = MmcSendCommand (CMD_SEND_IF_COND, 0x1AA, RESP_R7, NULL, 0, FALSE, Response);
  if (!EFI_ERROR (Status) && ((Response[0] & 0xFF) == 0xAA))
  {
    Version2 = TRUE;
  }

  if (Version2)
  {
    Ocr |= BIT30;
  }

  for (Retry = 0; Retry < 1000; Retry++)
  {
    Status = MmcSendCommand (CMD_APP_CMD, 0, RESP_R1, NULL, 0, FALSE, NULL);
    if (EFI_ERROR (Status))
    {
      DEBUG ((EFI_D_ERROR, "CMD_APP_CMD failed: %r\n", Status));
      return Status;
    }

    Status = MmcSendCommand (ACMD_SD_SEND_OP_COND, Ocr, RESP_R3, NULL, 0, FALSE, Response);
    if (EFI_ERROR (Status))
    {
      DEBUG ((EFI_D_ERROR, "ACMD_SD_SEND_OP_COND failed: %r\n", Status));
      return Status;
    }

    if ((Response[0] & BIT31) != 0)
      break;

    MicroSecondDelay (1000);
  }

  if (Retry >= 1000)
  {
    DEBUG ((EFI_D_ERROR, "Card never left power-up state\n"));
    return EFI_TIMEOUT;
  }

  mPrivate->HighCapacity = (BOOLEAN)((Response[0] & BIT30) != 0);

  Status = MmcSendCommand (CMD_ALL_SEND_CID, 0, RESP_R2, NULL, 0, FALSE, Response);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "CMD_ALL_SEND_CID failed: %r\n", Status));
    return Status;
  }

  Status = MmcSendCommand (CMD_SEND_RELATIVE_ADDR, 0, RESP_R6, NULL, 0, FALSE, Response);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "CMD_SEND_RELATIVE_ADDR failed: %r\n", Status));
    return Status;
  }

  mPrivate->Rca = Response[0] & 0xFFFF0000;

  Status = MmcSendCommand (CMD_SEND_CSD, mPrivate->Rca, RESP_R2, NULL, 0, FALSE, Csd);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "CMD_SEND_CSD failed: %r\n", Status));
    return Status;
  }

  CsdStructure = (Csd[0] >> 30) & 0x3;

  if (CsdStructure == 1)
  {
    UINT32 CSize = ((Csd[1] & 0x3F) << 16) | ((Csd[2] & 0xFFFF0000) >> 16);
    Blocks = ((UINT64)CSize + 1) * 1024;
  }
  else
  {
    UINT32  CSize     = ((Csd[1] & 0x3FF) << 2) | ((Csd[2] & 0xC0000000) >> 30);
    UINT32  CSizeMult = (Csd[2] >> 15) & 0x7;
    UINT32  ReadBlLen = (Csd[1] >> 16) & 0xF;

    Blocks = ((UINT64)CSize + 1) * (1ULL << (CSizeMult + 2));
    Blocks = (Blocks << ReadBlLen) / MMC_BLOCK_SIZE;
  }

  Status = MmcSendCommand (CMD_SELECT_CARD, mPrivate->Rca, RESP_R1B, NULL, 0, FALSE, NULL);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "CMD_SELECT_CARD failed: %r\n", Status));
    return Status;
  }

  Status = MmcSendCommand (CMD_APP_CMD, mPrivate->Rca, RESP_R1, NULL, 0, FALSE, NULL);
  if (!EFI_ERROR (Status))
  {
    Status = MmcSendCommand (ACMD_SET_BUS_WIDTH, 2, RESP_R1, NULL, 0, FALSE, NULL);
    if (!EFI_ERROR (Status))
    {
      MmioWrite32 (MMC0_BASE + MMC_WIDTH, 1);
    }
  }

  Status = MmcSendCommand (CMD_SET_BLOCKLEN, MMC_BLOCK_SIZE, RESP_R1, NULL, 0, FALSE, NULL);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "CMD_SET_BLOCKLEN failed: %r\n", Status));
    return Status;
  }

  Status = MmcSetClock (24000000);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "Failed to set clock: %r\n", Status));
    return Status;
  }

  mPrivate->Media.LastBlock = Blocks - 1;

  DEBUG ((EFI_D_WARN, "%a card, %lu MiB, RCA 0x%04x\n", mPrivate->HighCapacity ? "SDHC/SDXC" : "SDSC", (UINT64)(Blocks / 2048), mPrivate->Rca >> 16));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MmcBlockReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                 ExtendedVerification)
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MmcReadWriteBlocks (
  IN     EFI_LBA   Lba,
  IN     UINTN     BufferSize,
  IN OUT VOID     *Buffer,
  IN     BOOLEAN   Write)
{
  EFI_STATUS  Status;
  UINTN       Blocks;
  UINT32      Address;
  UINT32      Command;

  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;

  if (BufferSize == 0)
    return EFI_SUCCESS;

  if ((BufferSize % MMC_BLOCK_SIZE) != 0)
    return EFI_BAD_BUFFER_SIZE;


  Blocks = BufferSize / MMC_BLOCK_SIZE;

  if (Lba + Blocks - 1 > mPrivate->Media.LastBlock)
    return EFI_INVALID_PARAMETER;

  if (((UINTN)Buffer & 0x3) != 0)
    return EFI_INVALID_PARAMETER;

  Address = mPrivate->HighCapacity ? (UINT32)Lba : (UINT32)(Lba * MMC_BLOCK_SIZE);

  if (Write)
    Command = (Blocks > 1) ? CMD_WRITE_MULTIPLE_BLOCK : CMD_WRITE_SINGLE_BLOCK;
  else
    Command = (Blocks > 1) ? CMD_READ_MULTIPLE_BLOCK : CMD_READ_SINGLE_BLOCK;

  Status = MmcSendCommand (Command, Address, RESP_R1, Buffer, Blocks, Write, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG((EFI_D_ERROR, "Failed to %a %lu blocks at LBA %lu: %r\n", Write ? "write" : "read", (UINT64)Blocks, (UINT64)Lba, Status));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MmcBlockRead (
  IN  EFI_BLOCK_IO_PROTOCOL  *This,
  IN  UINT32                  MediaId,
  IN  EFI_LBA                 Lba,
  IN  UINTN                   BufferSize,
  OUT VOID                   *Buffer)
{
  if (MediaId != mPrivate->Media.MediaId)
    return EFI_MEDIA_CHANGED;

  return MmcReadWriteBlocks (Lba, BufferSize, Buffer, FALSE);
}

STATIC
EFI_STATUS
EFIAPI
MmcBlockWrite (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                  MediaId,
  IN EFI_LBA                 Lba,
  IN UINTN                   BufferSize,
  IN VOID                   *Buffer)
{
  if (MediaId != mPrivate->Media.MediaId)
    return EFI_MEDIA_CHANGED;

  if (mPrivate->Media.ReadOnly)
    return EFI_WRITE_PROTECTED;

  return MmcReadWriteBlocks (Lba, BufferSize, Buffer, TRUE);
}

STATIC
EFI_STATUS
EFIAPI
MmcBlockFlush (
  IN EFI_BLOCK_IO_PROTOCOL  *This)
{
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MmcDxeEntry (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable)
{
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (&gGpioProtocolGuid, NULL, (VOID *)&mGpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to Locate GPIO Protocol\n"));
    return EFI_NOT_FOUND;
  }

  mPrivate = AllocateZeroPool (sizeof (MMC_PRIVATE));
  if (mPrivate == NULL)
    return EFI_OUT_OF_RESOURCES;

  mPrivate->Signature = MMC_SIGNATURE;

  Status = MmcControllerInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "MMC controller init failed: %r\n", Status));
    goto Fail;
  }

  Status = MmcCardInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "No usable SD card: %r\n", Status));
    goto Fail;
  }

  mPrivate->Media.MediaId          = 1;
  mPrivate->Media.RemovableMedia   = TRUE;
  mPrivate->Media.MediaPresent     = TRUE;
  mPrivate->Media.LogicalPartition = FALSE;
  mPrivate->Media.ReadOnly         = FALSE;
  mPrivate->Media.WriteCaching     = FALSE;
  mPrivate->Media.BlockSize        = MMC_BLOCK_SIZE;
  mPrivate->Media.IoAlign          = 4;

  mPrivate->BlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mPrivate->BlockIo.Media       = &mPrivate->Media;
  mPrivate->BlockIo.Reset       = MmcBlockReset;
  mPrivate->BlockIo.ReadBlocks  = MmcBlockRead;
  mPrivate->BlockIo.WriteBlocks = MmcBlockWrite;
  mPrivate->BlockIo.FlushBlocks = MmcBlockFlush;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mPrivate->Handle,
                  &gEfiDevicePathProtocolGuid, &mDevicePath,
                  &gEfiBlockIoProtocolGuid, &mPrivate->BlockIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to install protocols: %r\n", Status));
    goto Fail;
  }

  return EFI_SUCCESS;

Fail:
  FreePool (mPrivate);
  mPrivate = NULL;
  return EFI_SUCCESS;
}
