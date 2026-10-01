#ifndef __GOP_H__
#define __GOP_H__

EFI_STATUS
EFIAPI
GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  );

EFI_STATUS
EFIAPI
GopSetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN  UINT32                        ModeNumber
  );

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
  );

#endif // __GOP_H__