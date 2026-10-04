#ifndef __I2C_PROTOCOL_H__
#define __I2C_PROTOCOL_H__

#define I2C_PROTOCOL_GUID \
  { 0x7e3b91c4, 0x2a6d, 0x4f18, { 0xa3, 0x5c, 0xe9, 0x12, 0x74, 0xbf, 0x68, 0xd0 } }

typedef struct _I2C_PROTOCOL I2C_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *I2C_READ) (
  IN UINT8         Address,
  IN UINT8         Register,
  OUT UINT8        *Value
  );

typedef
EFI_STATUS
(EFIAPI *I2C_WRITE) (
  IN UINT8         Address,
  IN UINT8         Register,
  IN UINT8         Value
  );

struct _I2C_PROTOCOL {
  I2C_READ   Read;
  I2C_WRITE  Write;
};

extern EFI_GUID gI2cProtocolGuid;

#endif // __I2C_PROTOCOL_H_
