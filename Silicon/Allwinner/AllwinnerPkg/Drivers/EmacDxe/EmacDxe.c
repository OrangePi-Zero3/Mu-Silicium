#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/ClockLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Guid/EventGroup.h>
#include <Protocol/DevicePath.h>
#include <Protocol/Gpio.h>

#include "EmacDxe.h"

STATIC EMAC_PRIVATE *mEmac;

STATIC VOID
EmacProgramStationAddress (
  IN EMAC_PRIVATE  *Private
  )
{
  UINT32  Low;
  UINT32  High;

  Low = (UINT32)Private->Mode.CurrentAddress.Addr[0] |
        ((UINT32)Private->Mode.CurrentAddress.Addr[1] << 8) |
        ((UINT32)Private->Mode.CurrentAddress.Addr[2] << 16) |
        ((UINT32)Private->Mode.CurrentAddress.Addr[3] << 24);

  High = (UINT32)Private->Mode.CurrentAddress.Addr[4] |
         ((UINT32)Private->Mode.CurrentAddress.Addr[5] << 8);

  MmioWrite32 (EMAC_BASE + EMAC_ADDR0_LOW, Low);
  MmioWrite32 (EMAC_BASE + EMAC_ADDR0_HIGH, High);
}

STATIC VOID
EmacConfigurePins (
  IN GPIO_PROTOCOL  *GpioProtocol
  )
{
  UINT32  Pin;

  for (Pin = 0; Pin <= 16; Pin++) {
    if (Pin == 6) {
      continue;
    }
    GpioProtocol->ConfigurePin (SUNXI_GPI (Pin), MUX_2);
    GpioProtocol->SetPinDrive (SUNXI_GPI (Pin), SUNXI_DRIVE_L3);
  }
}

STATIC EFI_STATUS
EmacMdioAt (
  IN  UINT8   PhyAddress,
  IN  UINT8   Register,
  IN  BOOLEAN Write,
  IN  UINT16  *Value
  )
{
  UINT32      Command;
  UINTN       Timeout;

  for (Timeout = 0; Timeout < 1000; Timeout++) {
    if ((MmioRead32 (EMAC_BASE + EMAC_MII_CMD) & EMAC_MDIO_BUSY) == 0) {
      break;
    }

    MicroSecondDelay (10);
  }

  if (Timeout == 1000) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: MDIO bus busy before %a reg %u\n", Write ? "write" : "read", Register));
    return EFI_TIMEOUT;
  }

  if (Write)
    MmioWrite32 (EMAC_BASE + EMAC_MII_DATA, *Value);

  Command = (Register << 4) |
            (PhyAddress << 12) | EMAC_MDIO_DIV_128 |
            (Write ? EMAC_MDIO_WRITE : 0) | EMAC_MDIO_BUSY;

  MmioWrite32 (EMAC_BASE + EMAC_MII_CMD, Command);

  for (Timeout = 0; Timeout < 1000; Timeout++) {
    if ((MmioRead32 (EMAC_BASE + EMAC_MII_CMD) & EMAC_MDIO_BUSY) == 0) {
      if (!Write)
        *Value = (UINT16)MmioRead32 (EMAC_BASE + EMAC_MII_DATA);

      return EFI_SUCCESS;
    }

    MicroSecondDelay (10);
  }

  DEBUG ((EFI_D_ERROR, "EmacDxe: MDIO %a reg %u timed out\n", Write ? "write" : "read", Register));
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
EmacMdio (
  IN  UINT8   Register,
  IN  BOOLEAN Write,
  IN  UINT16  *Value
  )
{
  return EmacMdioAt (mEmac->PhyAddress, Register, Write, Value);
}

STATIC EFI_STATUS
EmacYtExtendedRead (
  IN  UINT16  Register,
  OUT UINT16  *Value
  )
{
  UINT16      Address;
  EFI_STATUS  Status;

  Address = Register;
  Status = EmacMdio (EMAC_YT_EXT_ADDR, TRUE, &Address);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EmacMdio (EMAC_YT_EXT_DATA, FALSE, Value);
}

STATIC EFI_STATUS
EmacYtExtendedWrite (
  IN  UINT16  Register,
  IN  UINT16  Value
  )
{
  EFI_STATUS  Status;

  Status = EmacMdio (EMAC_YT_EXT_ADDR, TRUE, &Register);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EmacMdio (EMAC_YT_EXT_DATA, TRUE, &Value);
}

STATIC EFI_STATUS
EmacConfigureYt8531 (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      IdHigh;
  UINT16      IdLow;
  UINT16      ChipConfig;
  UINT16      RgmiiConfig;
  UINT16      SynceConfig;

  Status = EmacMdio (2, FALSE, &IdHigh);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EmacMdio (3, FALSE, &IdLow);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((EFI_D_WARN, "EmacDxe: PHY @%u ID %04x:%04x\n", mEmac->PhyAddress, IdHigh, IdLow));

  if ((((UINT32)IdHigh << 16) | IdLow) != EMAC_YT8531_PHY_ID) {
    DEBUG ((EFI_D_ERROR, "Expected YT8531 PHY ID %08x, got %04x:%04x\n", EMAC_YT8531_PHY_ID, IdHigh, IdLow));
    return EFI_DEVICE_ERROR;
  }

  Status = EmacYtExtendedRead (EMAC_YT_CHIP_CONFIG, &ChipConfig);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EmacYtExtendedRead (EMAC_YT_RGMII_CONFIG1, &RgmiiConfig);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EmacYtExtendedRead (EMAC_YT_SYNCE_CONFIG, &SynceConfig);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ChipConfig &= (UINT16)~EMAC_YT_RXC_DELAY_ENABLE;
  Status = EmacYtExtendedWrite (EMAC_YT_CHIP_CONFIG, ChipConfig);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  RgmiiConfig = (UINT16)((RgmiiConfig & (UINT16)~EMAC_YT_RX_DELAY_MASK) | EMAC_YT_RX_DELAY_1950_PS);
  Status = EmacYtExtendedWrite (EMAC_YT_RGMII_CONFIG1, RgmiiConfig);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  SynceConfig = (UINT16)((SynceConfig & (UINT16)~(EMAC_YT_SYNCE_ENABLE | EMAC_YT_CLK_125MHZ | EMAC_YT_CLK_SOURCE_MASK)) | EMAC_YT_SYNCE_ENABLE | EMAC_YT_CLK_125MHZ);
  Status = EmacYtExtendedWrite (EMAC_YT_SYNCE_CONFIG, SynceConfig);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((EFI_D_WARN, "EmacDxe: YT8531 RGMII RX delay and 125 MHz clock configured\n"));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EmacReadNegotiatedMode (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      PhyStatus;

  Status = EmacMdio (EMAC_MII_PHY_SPEC_STATUS, FALSE, &PhyStatus);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (PhyStatus & EMAC_YT_SPEED_MASK) {
    case EMAC_YT_SPEED_10:
      mEmac->LinkSpeed = 10;
      break;
    case EMAC_YT_SPEED_100:
      mEmac->LinkSpeed = 100;
      break;
    case EMAC_YT_SPEED_1000:
      mEmac->LinkSpeed = 1000;
      break;
    case EMAC_YT_SPEED_2500:
      DEBUG ((EFI_D_ERROR, "EmacDxe: YT8531 negotiated unsupported 2.5G link\n"));
      return EFI_UNSUPPORTED;
    default:
      DEBUG ((EFI_D_ERROR, "EmacDxe: invalid YT8531 link status 0x%04x\n", PhyStatus));
      return EFI_DEVICE_ERROR;
  }

  mEmac->FullDuplex = (PhyStatus & EMAC_YT_DUPLEX) != 0;
  DEBUG ((EFI_D_WARN, "EmacDxe: negotiated %u Mbps %a duplex\n", mEmac->LinkSpeed, mEmac->FullDuplex ? "full" : "half"));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EmacPhyStartAutoneg (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      Value;
  UINT16      Bmsr;
  UINTN       Index;

  Status = EmacMdio (EMAC_MII_BMSR, FALSE, &Bmsr);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EmacMdio (EMAC_MII_BMSR, FALSE, &Bmsr);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Bmsr & (EMAC_BMSR_LSTATUS | EMAC_BMSR_ANEGCOMPLETE)) == (EMAC_BMSR_LSTATUS | EMAC_BMSR_ANEGCOMPLETE)) {
    DEBUG ((EFI_D_WARN, "EmacDxe: PHY link already up\n"));
    Status = EmacConfigureYt8531 ();
    if (EFI_ERROR (Status)) {
      return Status;
    }

    return EmacReadNegotiatedMode ();
  }

  Value = EMAC_BMCR_RESET;
  Status = EmacMdio (EMAC_MII_BMCR, TRUE, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: PHY reset write failed: %r\n", Status));
    return Status;
  }

  for (Index = 0; Index < 500; Index++) {
    Status = EmacMdio (EMAC_MII_BMCR, FALSE, &Value);
    if (!EFI_ERROR (Status) && ((Value & EMAC_BMCR_RESET) == 0)) {
      break;
    }
    MicroSecondDelay (1000);
  }

  if (Index == 500) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: PHY reset did not complete\n"));
    return EFI_TIMEOUT;
  }

  Status = EmacConfigureYt8531 ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: YT8531 configuration failed: %r\n", Status));
    return Status;
  }

  Value = EMAC_ADV_CSMA | EMAC_ADV_10HALF | EMAC_ADV_10FULL |
          EMAC_ADV_100HALF | EMAC_ADV_100FULL;
  Status = EmacMdio (EMAC_MII_ADVERTISE, TRUE, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Value = EMAC_ADV_1000HALF | EMAC_ADV_1000FULL;
  Status = EmacMdio (EMAC_MII_CTRL1000, TRUE, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Value = EMAC_BMCR_ANENABLE | EMAC_BMCR_ANRESTART;
  Status = EmacMdio (EMAC_MII_BMCR, TRUE, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((EFI_D_WARN, "EmacDxe: PHY auto-negotiation started\n"));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EmacWaitForLink (
  VOID
  )
{
  EFI_STATUS  MdioStatus;
  UINT16      Bmsr;
  UINTN       Index;

  for (Index = 0; Index < 5000; Index++) {
    MdioStatus = EmacMdio (EMAC_MII_BMSR, FALSE, &Bmsr);
    if (!EFI_ERROR (MdioStatus)) {
      MdioStatus = EmacMdio (EMAC_MII_BMSR, FALSE, &Bmsr);
      if (!EFI_ERROR (MdioStatus)) {
        if((Bmsr & (EMAC_BMSR_LSTATUS | EMAC_BMSR_ANEGCOMPLETE)) == (EMAC_BMSR_LSTATUS | EMAC_BMSR_ANEGCOMPLETE)) {
            DEBUG ((EFI_D_WARN, "EmacDxe: PHY link up\n"));
            return EmacReadNegotiatedMode ();
        }
      }
    }

    MicroSecondDelay (1000);
  }

  DEBUG ((EFI_D_WARN, "EmacDxe: PHY link start failed\n"));
  return EFI_NO_MEDIA;
}

STATIC BOOLEAN
EmacPacketAvailable (
  IN EMAC_PRIVATE  *Private
  )
{
  EMAC_DESCRIPTOR  *Descriptor;

  if ((Private->Mode.State != EfiSimpleNetworkInitialized) || (Private->RxDescriptors == NULL))
    return FALSE;

  Descriptor = &Private->RxDescriptors[Private->RxIndex];
  InvalidateDataCacheRange (Descriptor, sizeof (*Descriptor));

  return (Descriptor->Status & EMAC_DESC_OWN) == 0;
}

STATIC VOID
EFIAPI
EmacWaitForPacketNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EMAC_PRIVATE  *Private;

  Private = Context;

  if (EmacPacketAvailable (Private))
    gBS->SignalEvent (Event);
}

STATIC VOID
EmacPoll (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EMAC_PRIVATE  *Private;

  Private = Context;
  if ((Private->Snp.WaitForPacket != NULL) && EmacPacketAvailable (Private))
    gBS->SignalEvent (Private->Snp.WaitForPacket);
}

STATIC VOID
EmacCloseWaitForPacket (
  IN EMAC_PRIVATE  *Private
  )
{
  if (Private->Snp.WaitForPacket != NULL) {
    gBS->CloseEvent (Private->Snp.WaitForPacket);
    Private->Snp.WaitForPacket = NULL;
  }
}

STATIC VOID
EmacStopDma (
  VOID
  )
{
  MmioWrite32 (EMAC_BASE + EMAC_RX_CTL0,
               MmioRead32 (EMAC_BASE + EMAC_RX_CTL0) & ~EMAC_RX_CTL0_RX_EN);
  MmioWrite32 (EMAC_BASE + EMAC_TX_CTL0,
               MmioRead32 (EMAC_BASE + EMAC_TX_CTL0) & ~EMAC_TX_CTL0_TX_EN);
  MmioWrite32 (EMAC_BASE + EMAC_TX_CTL1,
               MmioRead32 (EMAC_BASE + EMAC_TX_CTL1) & ~EMAC_TX_CTL1_TX_DMA_EN);
  MmioWrite32 (EMAC_BASE + EMAC_RX_CTL1,
               MmioRead32 (EMAC_BASE + EMAC_RX_CTL1) & ~EMAC_RX_CTL1_RX_DMA_EN);
}

STATIC VOID *
EmacFreeDmaPages (
  IN VOID   *Buffer,
  IN UINTN  NumberOfPages
  )
{
  EFI_STATUS  Status;

  if (Buffer == NULL) {
    return NULL;
  }

  Status = gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)Buffer, NumberOfPages);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: failed to release reserved DMA memory: %r\n", Status));
    return Buffer;
  }

  return NULL;
}

STATIC VOID
EFIAPI
EmacExitBootServicesNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EMAC_PRIVATE  *Private;

  Private = Context;
  EmacStopDma ();
  Private->Mode.State = EfiSimpleNetworkStopped;

  if (Private->PollEvent != NULL) {
    gBS->CloseEvent (Private->PollEvent);
    Private->PollEvent = NULL;
  }

  EmacCloseWaitForPacket (Private);

  Private->TxDescriptors = EmacFreeDmaPages (
                             Private->TxDescriptors,
                             EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE)
                             );
  Private->RxDescriptors = EmacFreeDmaPages (
                             Private->RxDescriptors,
                             EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE)
                             );
  Private->TxBuffer = EmacFreeDmaPages (
                        Private->TxBuffer,
                        EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE)
                        );
  Private->RxBuffer = EmacFreeDmaPages (
                        Private->RxBuffer,
                        EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE)
                        );

  if (Private->ExitBootServicesEvent != NULL) {
    gBS->CloseEvent (Private->ExitBootServicesEvent);
    Private->ExitBootServicesEvent = NULL;
  }
}

STATIC EFI_STATUS
EFIAPI
EmacStart (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  EMAC_PRIVATE  *Private;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State != EfiSimpleNetworkStopped) {
    return EFI_ALREADY_STARTED;
  }

  Private->Mode.State = EfiSimpleNetworkStarted;

  DEBUG ((EFI_D_WARN, "EmacDxe: interface started\n"));

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacStop (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  EMAC_PRIVATE  *Private;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  EmacStopDma ();

  EmacCloseWaitForPacket (Private);
  Private->Mode.State = EfiSimpleNetworkStopped;

  DEBUG ((EFI_D_WARN, "EmacDxe: interface stopped\n"));

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacInitialize (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        ExtraRx,
  IN UINTN                        ExtraTx
  )
{
  EMAC_PRIVATE  *Private;
  EFI_STATUS    Status;
  UINT32        Value;
  UINTN         Timeout = 1000;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: initialize requested before Start\n"));
    return EFI_NOT_STARTED;
  }
  if (Private->Mode.State == EfiSimpleNetworkInitialized) {
    return EFI_ALREADY_STARTED;
  }

  if (EFI_ERROR (EmacPhyStartAutoneg ())) {
    return EFI_DEVICE_ERROR;
  }
  if (EFI_ERROR (EmacWaitForLink ())) {
    return EFI_NO_MEDIA;
  }

  SetMem (Private->TxDescriptors, sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE, 0);
  SetMem (Private->RxDescriptors, sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE, 0);

  for (UINTN Index = 0; Index < EMAC_RING_SIZE; Index++) {
    Private->RxDescriptors[Index].BufferAddress = (UINT32)(UINTN)(Private->RxBuffer + Index * EMAC_PACKET_SIZE);
    Private->RxDescriptors[Index].Next = (UINT32)(UINTN)&Private->RxDescriptors[(Index + 1) % EMAC_RING_SIZE];
    Private->RxDescriptors[Index].ControlSize = EMAC_PACKET_SIZE - 4;
    Private->RxDescriptors[Index].Status = EMAC_DESC_OWN;
    Private->TxDescriptors[Index].BufferAddress = (UINT32)(UINTN)(Private->TxBuffer + Index * EMAC_PACKET_SIZE);
    Private->TxDescriptors[Index].Next = (UINT32)(UINTN)&Private->TxDescriptors[(Index + 1) % EMAC_RING_SIZE];
  }

  WriteBackInvalidateDataCacheRange (Private->TxDescriptors, sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE);
  WriteBackInvalidateDataCacheRange (Private->RxDescriptors, sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE);

  MmioWrite32 (EMAC_BASE + EMAC_CTL1, EMAC_CTL1_SOFT_RST);

  while ((MmioRead32 (EMAC_BASE + EMAC_CTL1) & EMAC_CTL1_SOFT_RST) != 0) {
    if (Timeout-- == 0) {
      DEBUG ((EFI_D_ERROR, "EmacDxe: MAC soft reset timed out\n"));
      return EFI_TIMEOUT;
    }
    MicroSecondDelay (10);
  }

  if (Private->Snp.WaitForPacket == NULL) {
    Status = gBS->CreateEvent (
                     EVT_NOTIFY_WAIT,
                     TPL_NOTIFY,
                     EmacWaitForPacketNotify,
                     Private,
                     &Private->Snp.WaitForPacket
                     );
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "EmacDxe: failed to create WaitForPacket event: %r\n", Status));
      return EFI_DEVICE_ERROR;
    }
  }

  EmacProgramStationAddress (Private);

  MmioWrite32 (EMAC_BASE + EMAC_CTL1, 8 << 24);
  MmioWrite32 (EMAC_BASE + EMAC_TX_CTL1, EMAC_TX_CTL1_TX_MD | EMAC_TX_CTL1_TX_DMA_EN);

  MmioWrite32 (EMAC_BASE + EMAC_RX_CTL1,
             EMAC_RX_CTL1_RX_MD | EMAC_RX_CTL1_RX_DMA_EN |
             EMAC_RX_CTL1_RX_ERR_FRM | EMAC_RX_CTL1_RX_RUNT_FRM);

  MmioWrite32 (EMAC_BASE + EMAC_TX_DMA_DESC, (UINT32)(UINTN)Private->TxDescriptors);
  MmioWrite32 (EMAC_BASE + EMAC_RX_DMA_DESC, (UINT32)(UINTN)Private->RxDescriptors);

  Value = (Private->FullDuplex ? EMAC_MAC_FULL_DUPLEX : 0) |
          (Private->LinkSpeed == 10 ? (2 << 2) :
           (Private->LinkSpeed == 100 ? EMAC_MAC_SPEED_100 : EMAC_MAC_SPEED_1000));

  MmioWrite32 (EMAC_BASE + EMAC_CTL0, Value);
  MmioWrite32 (EMAC_BASE + EMAC_RX_CTL0, EMAC_RX_CTL0_RX_EN);
  MmioWrite32 (EMAC_BASE + EMAC_TX_CTL0, EMAC_TX_CTL0_TX_EN);

  Private->Mode.State = EfiSimpleNetworkInitialized;

  DEBUG ((EFI_D_WARN, "EmacDxe: DMA initialized, link speed %u Mbps\n", Private->LinkSpeed));

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacReset (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      ExtendedVerification
  )
{
  EMAC_PRIVATE  *Private;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Private->Mode.State == EfiSimpleNetworkInitialized) {
    EmacStopDma ();

    Private->Mode.State = EfiSimpleNetworkStarted;
  }

  DEBUG ((EFI_D_WARN, "EmacDxe: resetting interface\n"));

  return EmacInitialize (This, 0, 0);
}

STATIC EFI_STATUS
EFIAPI
EmacShutdown (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  EMAC_PRIVATE  *Private;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  EmacStopDma ();

  EmacCloseWaitForPacket (Private);
  Private->Mode.State = EfiSimpleNetworkStarted;

  DEBUG ((EFI_D_WARN, "EmacDxe: interface shut down\n"));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacFilters (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINT32                       Enable,
  IN UINT32                       Disable,
  IN BOOLEAN                      Reset,
  IN UINTN                        MCastFilterCount,
  IN EFI_MAC_ADDRESS              *MCastFilter
  )
{
  EMAC_PRIVATE  *Private;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped)
    return EFI_NOT_STARTED;

  Private->Mode.ReceiveFilterSetting = Enable;

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacStation (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      Reset,
  IN EFI_MAC_ADDRESS              *New
  )
{
  EMAC_PRIVATE  *Private;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped)
    return EFI_NOT_STARTED;

  if (!Private->Mode.MacAddressChangeable)
    return EFI_UNSUPPORTED;

  if (Reset)
    CopyMem (&Private->Mode.CurrentAddress, &Private->Mode.PermanentAddress, sizeof (EFI_MAC_ADDRESS));
  else if (New != NULL)
    CopyMem (&Private->Mode.CurrentAddress, New, sizeof (EFI_MAC_ADDRESS));
  else
    return EFI_INVALID_PARAMETER;

  EmacProgramStationAddress (Private);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacStatistics (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      Reset,
  IN OUT UINTN                    *Size,
  OUT EFI_NETWORK_STATISTICS      *Stats
  )
{
  EMAC_PRIVATE  *Private;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Size == NULL)
    return EFI_INVALID_PARAMETER;

  if (*Size < sizeof (EFI_NETWORK_STATISTICS)) {
    *Size = sizeof (EFI_NETWORK_STATISTICS);
    return EFI_BUFFER_TOO_SMALL;
  }

  if (Stats == NULL)
    return EFI_INVALID_PARAMETER;

  if (Reset)
    SetMem (&Private->Statistics, sizeof (Private->Statistics), 0);

  CopyMem (Stats, &Private->Statistics, sizeof (*Stats));
  *Size = sizeof (*Stats);

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacMcast (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      IPv6,
  IN EFI_IP_ADDRESS               *Ip,
  OUT EFI_MAC_ADDRESS             *Mac
  )
{
  if ((Ip == NULL) || (Mac == NULL) || IPv6)
    return EFI_INVALID_PARAMETER;

  SetMem (Mac, sizeof (*Mac), 0);

  Mac->Addr[0] = 0x01;
  Mac->Addr[1] = 0x00;
  Mac->Addr[2] = 0x5e;

  Mac->Addr[3] = Ip->Addr[2] & 0x7f;
  Mac->Addr[4] = Ip->Addr[3];
  Mac->Addr[5] = Ip->Addr[4];

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacNvdata (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      ReadWrite,
  IN UINTN                        Offset,
  IN UINTN                        BufferSize,
  IN OUT VOID                     *Buffer
  )
{
  return EFI_UNSUPPORTED;
}

STATIC EFI_STATUS
EFIAPI
EmacStatus (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT UINT32                      *InterruptStatus,
  OUT VOID                        **TxBuf
  )
{
  if (InterruptStatus != NULL) {
    *InterruptStatus = 0;
  }

  if (TxBuf != NULL) {
    *TxBuf = NULL;
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacTransmit (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        HeaderSize,
  IN UINTN                        BufferSize,
  IN OUT VOID                     *Buffer,
  IN EFI_MAC_ADDRESS              *Src,
  IN EFI_MAC_ADDRESS              *Dest,
  IN UINT16                       *Protocol
  )
{
  EMAC_PRIVATE     *Private;
  EMAC_DESCRIPTOR  *Desc;
  UINT8            *TxPacket;
  EFI_MAC_ADDRESS  *Source;

  Private = EMAC_PRIVATE_FROM_SNP (This);

  if (Private->Mode.State != EfiSimpleNetworkInitialized)
    return EFI_NOT_STARTED;

  if ((BufferSize > EMAC_PACKET_SIZE) || (Buffer == NULL))
    return EFI_INVALID_PARAMETER;

  if (BufferSize < Private->Mode.MediaHeaderSize)
    return EFI_BUFFER_TOO_SMALL;

  if ((HeaderSize != 0) && ((HeaderSize != Private->Mode.MediaHeaderSize) || (Dest == NULL) || (Protocol == NULL)))
    return EFI_INVALID_PARAMETER;

  Desc = &Private->TxDescriptors[Private->TxIndex];

  if (Desc->Status & EMAC_DESC_OWN) {
    DEBUG ((EFI_D_WARN, "EmacDxe: TX ring full at descriptor %u\n", Private->TxIndex));
    return EFI_NOT_READY;
  }

  if (HeaderSize != 0) {
    Source = (Src != NULL) ? Src : &Private->Mode.CurrentAddress;

    CopyMem (Buffer, Dest->Addr, Private->Mode.HwAddressSize);
    CopyMem ((UINT8 *)Buffer + Private->Mode.HwAddressSize, Source->Addr, Private->Mode.HwAddressSize);

    ((UINT8 *)Buffer)[12] = (UINT8)(*Protocol >> 8);
    ((UINT8 *)Buffer)[13] = (UINT8)*Protocol;
  }

  TxPacket = Private->TxBuffer + Private->TxIndex * EMAC_PACKET_SIZE;

  CopyMem (TxPacket, Buffer, BufferSize);
  WriteBackInvalidateDataCacheRange (TxPacket, BufferSize);

  Desc->BufferAddress = (UINT32)(UINTN)TxPacket;
  Desc->ControlSize = (UINT32)BufferSize | EMAC_DESC_CHAIN_SECOND | EMAC_DESC_LAST_DESC | EMAC_DESC_FIRST_DESC;
  Desc->Status = EMAC_DESC_OWN;
  WriteBackInvalidateDataCacheRange (Desc, sizeof (*Desc));

  MmioWrite32 (EMAC_BASE + EMAC_TX_CTL1, MmioRead32 (EMAC_BASE + EMAC_TX_CTL1) | EMAC_TX_CTL1_TX_DMA_START);

  Private->TxIndex = (Private->TxIndex + 1) % EMAC_RING_SIZE;
  Private->Statistics.TxTotalFrames++;

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
EmacReceive (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT UINTN                       *HeaderSize,
  IN OUT UINTN                    *BufferSize,
  OUT VOID                        *Buffer,
  OUT EFI_MAC_ADDRESS             *Src,
  OUT EFI_MAC_ADDRESS             *Dest,
  OUT UINT16                      *Protocol
  )
{
  EMAC_PRIVATE     *Private;
  EMAC_DESCRIPTOR  *Desc;
  UINTN            Length;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State != EfiSimpleNetworkInitialized)
    return EFI_NOT_STARTED;

  if ((BufferSize == NULL) || (Buffer == NULL))
    return EFI_INVALID_PARAMETER;

  Desc = &Private->RxDescriptors[Private->RxIndex];
  InvalidateDataCacheRange (Desc, sizeof (*Desc));

  if ((Desc->Status & EMAC_DESC_OWN) != 0)
    return EFI_NOT_READY;

  Length = (Desc->Status >> 16) & 0x3fff;

  if (*BufferSize < Length) {
    *BufferSize = Length;
    DEBUG ((EFI_D_WARN, "EmacDxe: RX buffer too small for %u-byte frame\n", (UINT32)Length));
    return EFI_BUFFER_TOO_SMALL;
  }

  InvalidateDataCacheRange (Private->RxBuffer + Private->RxIndex * EMAC_PACKET_SIZE, Length);
  CopyMem (Buffer, Private->RxBuffer + Private->RxIndex * EMAC_PACKET_SIZE, Length);

  if (HeaderSize != NULL)
    *HeaderSize = 14;

  if (Src != NULL)
    CopyMem (Src, Buffer, 6);

  if (Dest != NULL)
    CopyMem (Dest, (UINT8 *)Buffer + 6, 6);

  if (Protocol != NULL)
    CopyMem (Protocol, (UINT8 *)Buffer + 12, sizeof (UINT16));

  *BufferSize = Length;
  Desc->Status = EMAC_DESC_OWN;
  WriteBackInvalidateDataCacheRange (Desc, sizeof (*Desc));

  Private->RxIndex = (Private->RxIndex + 1) % EMAC_RING_SIZE;
  Private->Statistics.RxTotalFrames++;

  return EFI_SUCCESS;
}

STATIC VOID
EmacDestroy (
  VOID
  )
{
  if (mEmac == NULL) {
    return;
  }

  if (mEmac->PollEvent != NULL)
    gBS->CloseEvent (mEmac->PollEvent);

  if (mEmac->ExitBootServicesEvent != NULL)
    gBS->CloseEvent (mEmac->ExitBootServicesEvent);

  EmacCloseWaitForPacket (mEmac);

  if (mEmac->DevicePath != NULL)
    FreePool (mEmac->DevicePath);

  if (mEmac->TxDescriptors != NULL)
    FreePages (mEmac->TxDescriptors,
               EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE));

  if (mEmac->RxDescriptors != NULL)
    FreePages (mEmac->RxDescriptors,
               EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE));

  if (mEmac->TxBuffer != NULL)
    FreePages (mEmac->TxBuffer,
               EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE));

  if (mEmac->RxBuffer != NULL)
    FreePages (mEmac->RxBuffer,
               EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE));

  FreePool (mEmac);
  mEmac = NULL;
}

EFI_STATUS
EFIAPI
EmacDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EMAC_DEVICE_PATH  *DevicePath;
  EMAC_PRIVATE  *Private;
  GPIO_PROTOCOL  *GpioProtocol;
  EFI_STATUS     Status;

  Status = gBS->LocateProtocol (&gGpioProtocolGuid, NULL, (VOID **)&GpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: failed to locate GPIO protocol: %r\n", Status));
    return Status;
  }

  Private = AllocateZeroPool (sizeof (*Private));
  if (Private == NULL) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: private context allocation failed\n"));
    return EFI_OUT_OF_RESOURCES;
  }
  Private->Signature = SIGNATURE_32 ('e', 'm', 'a', 'c');
  Private->PhyAddress = EMAC_PHY_ADDRESS;
  Private->FullDuplex = TRUE;
  mEmac = Private;

  Private->TxDescriptors = AllocateAlignedReservedPages (EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE), EFI_PAGE_SIZE);
  Private->RxDescriptors = AllocateAlignedReservedPages (EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE), EFI_PAGE_SIZE);
  Private->TxBuffer = AllocateAlignedReservedPages (EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE), EFI_PAGE_SIZE);
  Private->RxBuffer = AllocateAlignedReservedPages (EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE), EFI_PAGE_SIZE);

  if (!Private->TxDescriptors || !Private->RxDescriptors || !Private->TxBuffer || !Private->RxBuffer) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: DMA buffer allocation failed\n"));
    EmacDestroy ();
    return EFI_OUT_OF_RESOURCES;
  }

  CcmResetAssert (RST_BUS_EMAC0);
  CcmClkDisable (CLK_BUS_EMAC0);
  MicroSecondDelay (10);
  CcmClkEnable (CLK_BUS_EMAC0);
  CcmResetDeassert (RST_BUS_EMAC0);

  EmacConfigurePins (GpioProtocol);
  MmioWrite32 (EMAC_SYSCON, EMAC_SYSCON_EPHY_SHUTDOWN | EMAC_SYSCON_EPIT | EMAC_SYSCON_ETCS_INT_GMII | (7 << EMAC_SYSCON_ETXDC_SHIFT));

  Private->LinkSpeed = 1000;
  Private->PhyAddress = EMAC_PHY_ADDRESS;

  Private->Mode.CurrentAddress.Addr[0] = 0x02;
  Private->Mode.CurrentAddress.Addr[1] = 0x00;
  Private->Mode.CurrentAddress.Addr[2] = 0x00;
  Private->Mode.CurrentAddress.Addr[3] = 0x00;
  Private->Mode.CurrentAddress.Addr[4] = 0x00;
  Private->Mode.CurrentAddress.Addr[5] = 0x03;

  CopyMem (&Private->Mode.PermanentAddress, &Private->Mode.CurrentAddress, sizeof (EFI_MAC_ADDRESS));

  Private->Mode.State = EfiSimpleNetworkStopped;

  Private->Mode.HwAddressSize = 6;
  Private->Mode.MediaHeaderSize = 14;

  Private->Mode.MaxPacketSize = EMAC_MAX_FRAME_SIZE;

  Private->Mode.ReceiveFilterMask = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST | EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS;
  Private->Mode.ReceiveFilterSetting = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST;

  Private->Mode.MediaPresentSupported = TRUE;
  Private->Mode.MediaPresent = TRUE;

  Private->Mode.IfType = 1;

  SetMem (Private->Mode.BroadcastAddress.Addr, Private->Mode.HwAddressSize, 0xFF);

  Private->Snp.Revision = EFI_SIMPLE_NETWORK_PROTOCOL_REVISION;

  Private->Snp.Start = EmacStart;
  Private->Snp.Stop = EmacStop;
  Private->Snp.Initialize = EmacInitialize;
  Private->Snp.Reset = EmacReset;
  Private->Snp.Shutdown = EmacShutdown;
  Private->Snp.ReceiveFilters = EmacFilters;
  Private->Snp.StationAddress = EmacStation;
  Private->Snp.Statistics = EmacStatistics;
  Private->Snp.MCastIpToMac = EmacMcast;
  Private->Snp.NvData = EmacNvdata;
  Private->Snp.GetStatus = EmacStatus;
  Private->Snp.Transmit = EmacTransmit;
  Private->Snp.Receive = EmacReceive;
  Private->Snp.Mode = &Private->Mode;
  Private->DevicePath = AllocateZeroPool (sizeof (EMAC_DEVICE_PATH));

  if (Private->DevicePath == NULL) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: device path allocation failed\n"));
    EmacDestroy ();
    return EFI_OUT_OF_RESOURCES;
  }

  DevicePath = (EMAC_DEVICE_PATH *)Private->DevicePath;
  DevicePath->Vendor.Header.Type = HARDWARE_DEVICE_PATH;
  DevicePath->Vendor.Header.SubType = HW_VENDOR_DP;

  SetDevicePathNodeLength (&DevicePath->Vendor.Header, sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&DevicePath->Vendor.Guid, &mEmacDevicePathGuid);

  DevicePath->Mac.Header.Type = MESSAGING_DEVICE_PATH;
  DevicePath->Mac.Header.SubType = MSG_MAC_ADDR_DP;

  SetDevicePathNodeLength (&DevicePath->Mac.Header, sizeof (MAC_ADDR_DEVICE_PATH));
  CopyMem (&DevicePath->Mac.MacAddress, &Private->Mode.CurrentAddress, sizeof (EFI_MAC_ADDRESS));

  DevicePath->Mac.IfType = Private->Mode.IfType;

  SetDevicePathEndNode (&DevicePath->End);

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, EmacExitBootServicesNotify, Private, &gEfiEventBeforeExitBootServicesGuid, &Private->ExitBootServicesEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: failed to create ExitBootServices event: %r\n", Status));
    EmacDestroy ();
    return Status;
  }

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, EmacPoll, Private, &Private->PollEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: failed to create RX poll event: %r\n", Status));
    EmacDestroy ();
    return Status;
  }

  gBS->SetTimer (Private->PollEvent, TimerPeriodic, 10000);

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Private->Handle,
                  &gEfiSimpleNetworkProtocolGuid,
                  &Private->Snp,
                  &gEfiDevicePathProtocolGuid,
                  Private->DevicePath,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: failed to install SNP/device path: %r\n", Status));
    EmacDestroy ();
    return Status;
  }

  DEBUG ((EFI_D_WARN,
          "EmacDxe: EMAC0 SNP installed, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
          Private->Mode.CurrentAddress.Addr[0],
          Private->Mode.CurrentAddress.Addr[1],
          Private->Mode.CurrentAddress.Addr[2],
          Private->Mode.CurrentAddress.Addr[3],
          Private->Mode.CurrentAddress.Addr[4],
          Private->Mode.CurrentAddress.Addr[5]));
  return EFI_SUCCESS;
}
