#ifndef __I2C_PROTOCOL_H__
#define __I2C_PROTOCOL_H__

#define I2C_PROTOCOL_GUID \
  { 0xac669a62, 0x88c9, 0x4f64, { 0xbe, 0xff, 0xc7, 0x80, 0xbf, 0x37, 0x1f, 0x53 } }

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
