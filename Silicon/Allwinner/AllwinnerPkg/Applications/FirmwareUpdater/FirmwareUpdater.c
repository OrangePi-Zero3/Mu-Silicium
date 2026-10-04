#include <Uefi.h>

#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Protocol/AwSpiNorFlash.h>

#define IMAGE_NAME  L"spl-bootable.img"

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

  EraseLen = (ImageSize + Nor->EraseSize - 1) / Nor->EraseSize * Nor->EraseSize;
  if ((UINT64)EraseLen > Nor->FlashSize) {
    Print (L"Image (%u bytes) does not fit in flash (%lu bytes)\n", ImageSize, Nor->FlashSize);
    DEBUG ((EFI_D_ERROR, "Image (%u bytes) does not fit in flash (%lu bytes)\n", ImageSize, Nor->FlashSize));
    Status = EFI_BAD_BUFFER_SIZE;
    goto Out;
  }

  if (CompareMem(Image + 4, "eGON.BT0", 8) != 0) {
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

  Status = Nor->Read (Nor, 0, ImageSize, Current);
  if (EFI_ERROR (Status)) {
    Print (L"Flash read failed: %r\n", Status);
    DEBUG ((EFI_D_ERROR, "Flash read failed: %r\n", Status));
    goto Out;
  }

  if (CompareMem (Image, Current, ImageSize) == 0) {
    Print (L"Flash already contains this image, nothing to do\n");
    DEBUG ((EFI_D_WARN, "Flash already contains this image, nothing to do\n"));
    Status = EFI_SUCCESS;
    goto Out;
  }

  Print (L"Flash differs, updating (erase %u bytes)... do not power off\n", EraseLen);
  DEBUG ((EFI_D_WARN, "Flash differs, updating (erase %u bytes)... do not power off\n", EraseLen));

  Status = Nor->Erase (Nor, 0, EraseLen);
  if (EFI_ERROR (Status)) {
    Print (L"Erase failed: %r\n", Status);
    DEBUG ((EFI_D_ERROR, "Erase failed: %r\n", Status));
    goto Out;
  }

  Status = Nor->Write (Nor, 0, ImageSize, Image);
  if (EFI_ERROR (Status)) {
    Print (L"Write failed: %r\n", Status);
    DEBUG ((EFI_D_ERROR, "Write failed: %r\n", Status));
    goto Out;
  }

  ZeroMem (Current, ImageSize);
  Status = Nor->Read (Nor, 0, ImageSize, Current);
  if (EFI_ERROR (Status) || (CompareMem (Image, Current, ImageSize) != 0)) {
    Print (L"VERIFY FAILED! Please program an SD card with UEFI, insert into system and reboot!\n");
    DEBUG ((EFI_D_ERROR, "VERIFY FAILED! Please program an SD card with UEFI, insert into system and reboot!\n"));
    Status = EFI_DEVICE_ERROR;
    goto Out;
  }

  Print (L"Flash updated and verified, rebooting...\n");
  DEBUG ((EFI_D_WARN, "Flash updated and verified, rebooting...\n"));
  gBS->Stall (2000000);
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