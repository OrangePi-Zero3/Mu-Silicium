#ifndef __SPI_PROTOCOL_H__
#define __SPI_PROTOCOL_H__

#define SPI_PROTOCOL_GUID \
  { 0x7e3b91c4, 0x2a6d, 0x4f18, { 0xa3, 0x5c, 0xe9, 0x12, 0x74, 0xbf, 0x68, 0xd1 } }

//
// Hardware FIFO depth
//
#define SPI_FIFO_DEPTH  64

typedef struct _SPI_PROTOCOL SPI_PROTOCOL;

/**
  Run one chip-select-asserted SPI transaction: send Tx, then receive Rx.

  @param[in]  Tx     Bytes to send (may be NULL if TxLen is 0).
  @param[in]  TxLen  Number of bytes to send.
  @param[out] Rx     Receive buffer (may be NULL if RxLen is 0).
  @param[in]  RxLen  Number of bytes to receive.

  @retval EFI_SUCCESS            Transfer completed.
  @retval EFI_INVALID_PARAMETER  Bad buffer, or TxLen + RxLen > SPI_FIFO_DEPTH.
**/
typedef
EFI_STATUS
(EFIAPI *SPI_TRANSFER) (
  IN  CONST UINT8  *Tx,
  IN  UINT32        TxLen,
  OUT UINT8        *Rx,
  IN  UINT32        RxLen
  );

struct _SPI_PROTOCOL {
  SPI_TRANSFER Transfer;
};

extern EFI_GUID gSpiProtocolGuid;

#endif // __SPI_PROTOCOL_H__