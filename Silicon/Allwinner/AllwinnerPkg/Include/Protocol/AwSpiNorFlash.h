#ifndef AW_SPI_NOR_FLASH_PROTOCOL_H_
#define AW_SPI_NOR_FLASH_PROTOCOL_H_

#define AW_SPI_NOR_FLASH_PROTOCOL_GUID \
  { 0x7e3b91c4, 0x2a6d, 0x4f18, { 0xa3, 0x5c, 0xe9, 0x12, 0x75, 0xbf, 0x68, 0xd1 } }

typedef struct _AW_SPI_NOR_FLASH_PROTOCOL AW_SPI_NOR_FLASH_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *AW_SPI_NOR_READ) (
  IN  AW_SPI_NOR_FLASH_PROTOCOL *This,
  IN  UINT32                  Address,
  IN  UINT32                  Length,
  OUT UINT8                  *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *AW_SPI_NOR_WRITE) (
  IN  AW_SPI_NOR_FLASH_PROTOCOL *This,
  IN  UINT32                  Address,
  IN  UINT32                  Length,
  IN  CONST UINT8            *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *AW_SPI_NOR_ERASE) (
  IN  AW_SPI_NOR_FLASH_PROTOCOL *This,
  IN  UINT32                  Address,   // must be sector aligned
  IN  UINT32                  Length     // must be multiple of EraseSize
  );

struct _AW_SPI_NOR_FLASH_PROTOCOL {
  AW_SPI_NOR_READ   Read;
  AW_SPI_NOR_WRITE  Write;
  AW_SPI_NOR_ERASE  Erase;
  UINT64         FlashSize;
  UINT32         PageSize;
  UINT32         EraseSize;
};

extern EFI_GUID gAwSpiNorFlashProtocolGuid;

#endif