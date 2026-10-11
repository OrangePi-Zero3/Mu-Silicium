#include <Uefi.h>

#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <IndustryStandard/Acpi50.h>

#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Guid/FileInfo.h>
#include <Protocol/AwSpiNorFlash.h>

#define IMAGE_NAME  L"spl-bootable.img"
#define PROGRESS_BAR_HEIGHT  12
#define FLASH_READ_CHUNK     4096

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
STATIC UINTN mProgressX;
STATIC UINTN mProgressY;
STATIC UINTN mProgressWidth;
STATIC UINTN mLastProgressPercent = MAX_UINTN;

STATIC
VOID
DrawProgress (
  IN UINTN Percent
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;
  UINTN                          InnerWidth;
  UINTN                          FillWidth;

  if ((mGop == NULL) || (mProgressWidth == 0)) {
    return;
  }

  if (Percent > 100) {
    Percent = 100;
  }

  Color.Blue = 0xB0;
  Color.Green = 0xB0;
  Color.Red = 0xB0;
  Color.Reserved = 0;
  mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, mProgressX, mProgressY,
             mProgressWidth, PROGRESS_BAR_HEIGHT, 0);

  InnerWidth = mProgressWidth > 4 ? mProgressWidth - 4 : 0;
  Color.Blue = 0x20;
  Color.Green = 0x20;
  Color.Red = 0x20;
  mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, mProgressX + 2,
             mProgressY + 2, InnerWidth, PROGRESS_BAR_HEIGHT - 4, 0);

  FillWidth = (InnerWidth * Percent) / 100;
  if (FillWidth != 0) {
    Color.Blue = 0xD0;
    Color.Green = 0xA0;
    Color.Red = 0x30;
    mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, mProgressX + 2,
               mProgressY + 2, FillWidth, PROGRESS_BAR_HEIGHT - 4, 0);
  }
}

STATIC
VOID
UpdateProgress (
  IN UINT64 Completed,
  IN UINT64 Total,
  IN UINTN  StartPercent,
  IN UINTN  EndPercent
  )
{
  UINTN Percent;

  if ((Total == 0) || (EndPercent < StartPercent)) {
    return;
  }

  if (Completed > Total) {
    Completed = Total;
  }

  Percent = StartPercent + (UINTN)(((UINT64)(EndPercent - StartPercent) * Completed) / Total);
  if (Percent != mLastProgressPercent) {
    DrawProgress (Percent);
    mLastProgressPercent = Percent;
  }
}

// Position the bar just below the BGRT bitmap, leaving the logo untouched.
STATIC
VOID
InitializeProgressBar (
  VOID
  )
{
  EFI_ACPI_SDT_PROTOCOL                     *AcpiSdt;
  EFI_ACPI_SDT_HEADER                       *AcpiTable;
  EFI_ACPI_5_0_BOOT_GRAPHICS_RESOURCE_TABLE  *Bgrt;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION      *Mode;
  UINTN                                      Index;
  UINTN                                      TableKey;
  UINTN                                      ScreenWidth;
  UINTN                                      ScreenHeight;
  UINTN                                      LogoBottom;
  UINT32                                     LogoWidth;
  UINT32                                     LogoHeight;
  UINT32                                     BmpWidth;
  UINT32                                     BmpHeight;
  EFI_ACPI_TABLE_VERSION                    Version;

  mGop = NULL;
  mProgressWidth = 0;
  if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop)) ||
      (mGop->Mode == NULL) || (mGop->Mode->Info == NULL)) {
    mGop = NULL;
    return;
  }

  Mode = mGop->Mode->Info;
  ScreenWidth = Mode->HorizontalResolution;
  ScreenHeight = Mode->VerticalResolution;
  mProgressWidth = MIN (ScreenWidth > 40 ? ScreenWidth - 40 : 0, 480);
  if (mProgressWidth < 80) {
    mProgressWidth = 0;
    return;
  }

  mProgressX = (ScreenWidth - mProgressWidth) / 2;
  LogoBottom = 0;
  LogoWidth = 0;
  LogoHeight = 0;
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&AcpiSdt))) {
    for (Index = 0; ; Index++) {
      if (EFI_ERROR (AcpiSdt->GetAcpiTable (Index, &AcpiTable, &Version, &TableKey))) {
        break;
      }

      if ((AcpiTable->Signature == EFI_ACPI_5_0_BOOT_GRAPHICS_RESOURCE_TABLE_SIGNATURE) &&
          (AcpiTable->Length >= sizeof (*Bgrt))) {
        Bgrt = (EFI_ACPI_5_0_BOOT_GRAPHICS_RESOURCE_TABLE *)AcpiTable;
        if (((Bgrt->Status & EFI_ACPI_5_0_BGRT_STATUS_VALID) != 0) &&
            (Bgrt->ImageType == EFI_ACPI_5_0_BGRT_IMAGE_TYPE_BMP) &&
            (Bgrt->ImageAddress != 0)) {
          // BITMAPINFOHEADER width/height are at byte offsets 18 and 22.
          // Use the absolute height because BMPs may be stored bottom-up.
          CopyMem (&BmpWidth, (VOID *)(UINTN)(Bgrt->ImageAddress + 18), sizeof (BmpWidth));
          CopyMem (&BmpHeight, (VOID *)(UINTN)(Bgrt->ImageAddress + 22), sizeof (BmpHeight));
          LogoWidth = BmpWidth;
          LogoHeight = BmpHeight;
          if ((BmpHeight & 0x80000000U) != 0) {
            LogoHeight = (UINT32)(-(INT32)BmpHeight);
          }
          if ((LogoWidth != 0) && (LogoHeight != 0) && (LogoWidth <= ScreenWidth) &&
              (LogoHeight <= ScreenHeight) && (Bgrt->ImageOffsetX <= ScreenWidth - LogoWidth) &&
              (Bgrt->ImageOffsetY <= ScreenHeight - LogoHeight)) {
            LogoBottom = Bgrt->ImageOffsetY + LogoHeight;
            mProgressWidth = MIN (mProgressWidth, LogoWidth);
            mProgressX = Bgrt->ImageOffsetX + (LogoWidth - mProgressWidth) / 2;
          }
        }
        break;
      }
    }
  }

  // Fall back to a lower-screen position on systems without a valid BGRT.
  mProgressY = (LogoBottom != 0) ? LogoBottom + 18 : (ScreenHeight * 3) / 4;
  if ((LogoBottom != 0) && (mProgressY + PROGRESS_BAR_HEIGHT > ScreenHeight)) {
    mProgressWidth = 0;
    return;
  }
  if (LogoBottom == 0) {
    mProgressX = (ScreenWidth - mProgressWidth) / 2;
  }

  // Avoid drawing the bar off-screen on small panels.
  if (mProgressY + PROGRESS_BAR_HEIGHT > ScreenHeight) {
    mProgressY = ScreenHeight - PROGRESS_BAR_HEIGHT - 8;
  }
  DrawProgress (0);
  mLastProgressPercent = 0;
}

STATIC
EFI_STATUS
ReadWholeFile (
  IN  EFI_FILE_PROTOCOL  *File,
  OUT UINT8             **Buffer,
  OUT UINT32             *Size
  )
{
  EFI_STATUS     Status;
  EFI_FILE_INFO  *Info;
  UINTN          InfoSize;
  UINT64         FileSize;
  UINTN          ReadSize;
  UINT8          *Buf;

  InfoSize = 0;
  Info     = NULL;
  Status   = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return EFI_DEVICE_ERROR;
  }

  Info = AllocatePool (InfoSize);
  if (Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    return Status;
  }

  FileSize = Info->FileSize;
  FreePool (Info);

  if ((FileSize == 0) || (FileSize > MAX_UINT32)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Buf = AllocatePool ((UINTN)FileSize);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ReadSize = (UINTN)FileSize;
  Status   = File->Read (File, &ReadSize, Buf);
  if (EFI_ERROR (Status) || (ReadSize != (UINTN)FileSize)) {
    FreePool (Buf);
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  *Buffer = Buf;
  *Size   = (UINT32)FileSize;
  return EFI_SUCCESS;
}

//
// Look at the root of every filesystem volume, take the first image found.
//
STATIC
EFI_STATUS
LoadImageFromVolumes (
  OUT UINT8   **Image,
  OUT UINT32   *ImageSize
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  EFI_STATUS  Result;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  Result = EFI_NOT_FOUND;

  for (Index = 0; Index < HandleCount; Index++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
    EFI_FILE_PROTOCOL                *Root;
    EFI_FILE_PROTOCOL                *File;

    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Fs->OpenVolume (Fs, &Root);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Root->Open (Root, &File, IMAGE_NAME, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR (Status)) {
      Root->Close (Root);
      continue;
    }

    Status = ReadWholeFile (File, Image, ImageSize);
    File->Close (File);
    Root->Close (Root);

    if (!EFI_ERROR (Status)) {
      Result = EFI_SUCCESS;
      break;
    }

    Print (L"Found %s on volume %lu but failed to read it: %r\n", IMAGE_NAME, (UINT64)Index, Status);
    DEBUG ((EFI_D_ERROR, "Failed to read %s on volume %lu: %r\n", IMAGE_NAME, (UINT64)Index, Status));
  }

  FreePool (Handles);
  return Result;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS              Status;
  AW_SPI_NOR_FLASH_PROTOCOL  *Nor;
  UINT8                   *Image;
  UINT8                   *Current;
  UINT32                  ImageSize;
  UINT32                  EraseLen;
  UINT32                  EraseOffset;
  UINT32                  WriteOffset;
  UINT32                  ChunkSize;
  UINT32                  ReadOffset;
  UINT64                  EraseLen64;
  UINTN                   Countdown;

  Image     = NULL;
  Current   = NULL;
  ImageSize = 0;

  Status = LoadImageFromVolumes (&Image, &ImageSize);
  if (Status == EFI_NOT_FOUND) {
    Print (L"No %s found on any volume, nothing to do\n", IMAGE_NAME);
    DEBUG ((EFI_D_WARN, "No %s found on any volume, nothing to do\n", IMAGE_NAME));
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    Print (L"Failed to load %s: %r\n", IMAGE_NAME, Status);
    DEBUG ((EFI_D_ERROR, "Failed to load %s: %r\n", IMAGE_NAME, Status));
    return Status;
  }

  Print (L"Found %s (%u bytes)\n", IMAGE_NAME, ImageSize);

  Status = gBS->LocateProtocol (&gAwSpiNorFlashProtocolGuid, NULL, (VOID **)&Nor);
  if (EFI_ERROR (Status)) {
    Print (L"SPI NOR protocol not available: %r\n", Status);
    DEBUG ((EFI_D_ERROR, "SPI NOR protocol not available: %r\n", Status));
    goto Out;
  }

  if ((Nor->EraseSize == 0) || (Nor->PageSize == 0)) {
    Print (L"SPI NOR reports invalid erase or page size\n");
    Status = EFI_DEVICE_ERROR;
    goto Out;
  }

  EraseLen64 = (((UINT64)ImageSize + Nor->EraseSize - 1) / Nor->EraseSize) * Nor->EraseSize;
  if ((EraseLen64 > MAX_UINT32) || (EraseLen64 > Nor->FlashSize)) {
    Print (L"Image (%u bytes) does not fit in flash (%lu bytes)\n", ImageSize, Nor->FlashSize);
    DEBUG ((EFI_D_ERROR, "Image (%u bytes) does not fit in flash (%lu bytes)\n", ImageSize, Nor->FlashSize));
    Status = EFI_BAD_BUFFER_SIZE;
    goto Out;
  }
  EraseLen = (UINT32)EraseLen64;

  if ((ImageSize < 12) || (CompareMem (Image + 4, "eGON.BT0", 8) != 0)) {
    Print (L"Image does not have valid SPL signature, aborting\n");
    DEBUG ((EFI_D_ERROR, "Image does not have valid SPL signature, aborting\n"));
    Status = EFI_ABORTED;
    goto Out;
  }

  Current = AllocatePool (ImageSize);
  if (Current == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Out;
  }

  InitializeProgressBar ();
  mLastProgressPercent = 0;
  Print (L"Reading flash...\n");
  for (ReadOffset = 0; ReadOffset < ImageSize; ReadOffset += ChunkSize) {
    ChunkSize = MIN (FLASH_READ_CHUNK, ImageSize - ReadOffset);
    Status = Nor->Read (Nor, ReadOffset, ChunkSize, Current + ReadOffset);
    if (EFI_ERROR (Status)) {
      Print (L"Flash read failed: %r\n", Status);
      DEBUG ((EFI_D_ERROR, "Flash read failed: %r\n", Status));
      goto Out;
    }

    UpdateProgress ((UINT64)ReadOffset + ChunkSize, ImageSize, 0, 10);
  }

  if (CompareMem (Image, Current, ImageSize) == 0) {
    DrawProgress (100);
    mLastProgressPercent = 100;
    Print (L"Flash already contains this image, nothing to do\n");
    DEBUG ((EFI_D_WARN, "Flash already contains this image, nothing to do\n"));
    Status = EFI_SUCCESS;
    goto Out;
  }

  Print (L"Flash differs, updating (erase %u bytes)... do not power off\n", EraseLen);
  DEBUG ((EFI_D_WARN, "Flash differs, updating (erase %u bytes)... do not power off\n", EraseLen));
  for (EraseOffset = 0; EraseOffset < EraseLen; EraseOffset += Nor->EraseSize) {
    Status = Nor->Erase (Nor, EraseOffset, Nor->EraseSize);
    if (EFI_ERROR (Status)) {
      Print (L"Flash erase failed at 0x%08x: %r\n", EraseOffset, Status);
      DEBUG ((EFI_D_ERROR, "Flash erase failed at 0x%08x: %r\n", EraseOffset, Status));
      goto Out;
    }

    UpdateProgress ((UINT64)EraseOffset + Nor->EraseSize, EraseLen, 10, 40);
  }

  for (WriteOffset = 0; WriteOffset < ImageSize; WriteOffset += ChunkSize) {
    ChunkSize = MIN (Nor->PageSize, ImageSize - WriteOffset);
    Status = Nor->Write (Nor, WriteOffset, ChunkSize, Image + WriteOffset);
    if (EFI_ERROR (Status)) {
      Print (L"Flash write failed at 0x%08x: %r\n", WriteOffset, Status);
      DEBUG ((EFI_D_ERROR, "Flash write failed at 0x%08x: %r\n", WriteOffset, Status));
      goto Out;
    }

    UpdateProgress ((UINT64)WriteOffset + ChunkSize, ImageSize, 40, 75);
  }

  Print (L"Verifying flash...\n");
  for (ReadOffset = 0; ReadOffset < ImageSize; ReadOffset += ChunkSize) {
    ChunkSize = MIN (FLASH_READ_CHUNK, ImageSize - ReadOffset);
    Status = Nor->Read (Nor, ReadOffset, ChunkSize, Current + ReadOffset);
    if (EFI_ERROR (Status)) {
      Print (L"Flash verify read failed at 0x%08x: %r\n", ReadOffset, Status);
      DEBUG ((EFI_D_ERROR, "Flash verify read failed at 0x%08x: %r\n", ReadOffset, Status));
      goto Out;
    }

    if (CompareMem (Image + ReadOffset, Current + ReadOffset, ChunkSize) != 0) {
      Print (L"VERIFY FAILED at flash offset 0x%08x! Please program an SD card with UEFI, insert into system and reboot!\n", ReadOffset);
      DEBUG ((EFI_D_ERROR, "VERIFY FAILED at flash offset 0x%08x!\n", ReadOffset));
      Status = EFI_DEVICE_ERROR;
      goto Out;
    }

    UpdateProgress ((UINT64)ReadOffset + ChunkSize, ImageSize, 75, 100);
  }

  DrawProgress (100);
  Print (L"Flash updated and verified. Rebooting in 5 seconds...\n");
  DEBUG ((EFI_D_WARN, "Flash updated and verified, rebooting...\n"));
  for (Countdown = 5; Countdown > 0; Countdown--) {
    Print (L"Rebooting in %u...\n", (UINT32)Countdown);
    gBS->Stall (1000000);
  }
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  Status = EFI_SUCCESS;

Out:
  if (Image != NULL) {
    FreePool (Image);
  }

  if (Current != NULL) {
    FreePool (Current);
  }

  return Status;
}
