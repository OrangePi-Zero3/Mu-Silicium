#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include <Protocol/GraphicsOutput.h>

#include "DisplayDxe.h"

EFI_STATUS
EFIAPI
GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  if (ModeNumber != 0 || Info == NULL || SizeOfInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Info = AllocateCopyPool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION), &mPrivate->Info);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GopSetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN  UINT32                        ModeNumber
  )
{
  return (ModeNumber == 0) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
GopBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL             *This,
  IN  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL        *BltBuffer OPTIONAL,
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION        BltOperation,
  IN  UINTN                                    SourceX,
  IN  UINTN                                    SourceY,
  IN  UINTN                                    DestinationX,
  IN  UINTN                                    DestinationY,
  IN  UINTN                                    Width,
  IN  UINTN                                    Height,
  IN  UINTN                                    Delta OPTIONAL
  )
{
  UINT8  *Fb = (UINT8 *)(UINTN)mPrivate->FrameBufferBase;
  UINTN  Stride = mPrivate->Info.PixelsPerScanLine * 4;
  UINTN  Row;

  if (BltOperation == EfiBltVideoFill) {
    for (Row = 0; Row < Height; Row++) {
      UINT32 *Dst = (UINT32 *)(Fb + (DestinationY + Row) * Stride + DestinationX * 4);
      UINTN Col;
      for (Col = 0; Col < Width; Col++) {
        Dst[Col] = *(UINT32 *)BltBuffer;
      }
    }
    return EFI_SUCCESS;
  }

  if (BltOperation == EfiBltBufferToVideo) {
    UINTN SrcStride = (Delta == 0) ? Width * 4 : Delta;
    for (Row = 0; Row < Height; Row++) {
      CopyMem (
        Fb + (DestinationY + Row) * Stride + DestinationX * 4,
        (UINT8 *)BltBuffer + (SourceY + Row) * SrcStride + SourceX * 4,
        Width * 4
        );
    }
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}