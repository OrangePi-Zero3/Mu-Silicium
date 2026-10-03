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
  if (EmacPacketAvailable ((EMAC_PRIVATE *)Context))
    gBS->SignalEvent (Event);
}

STATIC BOOLEAN
EmacStopDma (
  VOID
  )
{
  UINTN  Timeout;

  MmioAnd32 (EMAC_BASE + EMAC_RX_CTL0, ~(UINT32)EMAC_RX_CTL0_RX_EN);
  MmioAnd32 (EMAC_BASE + EMAC_TX_CTL0, ~(UINT32)EMAC_TX_CTL0_TX_EN);
  MmioAnd32 (EMAC_BASE + EMAC_TX_CTL1, ~(UINT32)EMAC_TX_CTL1_TX_DMA_EN);
  MmioAnd32 (EMAC_BASE + EMAC_RX_CTL1, ~(UINT32)EMAC_RX_CTL1_RX_DMA_EN);

  MmioWrite32 (EMAC_BASE + EMAC_CTL1, EMAC_CTL1_SOFT_RST);
  for (Timeout = 0; Timeout < EMAC_RESET_TIMEOUT_US / 10; Timeout++) {
    if ((MmioRead32 (EMAC_BASE + EMAC_CTL1) & EMAC_CTL1_SOFT_RST) == 0) {
      return TRUE;
    }

    MicroSecondDelay (10);
  }

  DEBUG ((EFI_D_WARN, "EmacDxe: MAC soft reset timed out\n"));
  return FALSE;
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
  MmioWrite32 (EMAC_BASE + EMAC_TX_DMA_DESC, 0);
  MmioWrite32 (EMAC_BASE + EMAC_RX_DMA_DESC, 0);

  CcmResetAssert (RST_BUS_EMAC0);
  CcmClkDisable (CLK_BUS_EMAC0);

  Private->Mode.State = EfiSimpleNetworkStopped;
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

  if (Private->Mode.State == EfiSimpleNetworkInitialized) {
    EmacStopDma ();
  }

  Private->Mode.State = EfiSimpleNetworkStopped;

  DEBUG ((EFI_D_WARN, "EmacDxe: interface stopped\n"));

  return EFI_SUCCESS;
}

STATIC VOID
EmacRxDescsInit (
  IN EMAC_PRIVATE  *Private
  )
{
  UINTN  Index;

  InvalidateDataCacheRange (Private->RxBuffer, EMAC_PACKET_SIZE * EMAC_RING_SIZE);

  for (Index = 0; Index < EMAC_RING_SIZE; Index++) {
    EMAC_DESCRIPTOR  *Desc = &Private->RxDescriptors[Index];

    Desc->BufferAddress = (UINT32)(UINTN)(Private->RxBuffer + Index * EMAC_PACKET_SIZE);
    Desc->Next          = (UINT32)(UINTN)&Private->RxDescriptors[(Index + 1) % EMAC_RING_SIZE];
    Desc->ControlSize   = EMAC_RX_BUFFER_SIZE;
    Desc->Status        = EMAC_DESC_OWN;
  }

  WriteBackInvalidateDataCacheRange (Private->RxDescriptors, sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE);
  Private->RxIndex = 0;
}

STATIC VOID
EmacTxDescsInit (
  IN EMAC_PRIVATE  *Private
  )
{
  UINTN  Index;

  for (Index = 0; Index < EMAC_RING_SIZE; Index++) {
    EMAC_DESCRIPTOR  *Desc = &Private->TxDescriptors[Index];

    Desc->BufferAddress = (UINT32)(UINTN)(Private->TxBuffer + Index * EMAC_PACKET_SIZE);
    Desc->Next          = (UINT32)(UINTN)&Private->TxDescriptors[(Index + 1) % EMAC_RING_SIZE];
    Desc->ControlSize   = 0;
    Desc->Status        = 0;
    Private->TxUserBuffer[Index] = NULL;
  }

  WriteBackInvalidateDataCacheRange (Private->TxDescriptors, sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE);
  Private->TxIndex      = 0;
  Private->TxCleanIndex = 0;
}

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

STATIC EFI_STATUS
EFIAPI
EmacInitialize (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        ExtraRx,
  IN UINTN                        ExtraTx
  )
{
  EMAC_PRIVATE  *Private;
  UINT32        Value;

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

  if (!EmacStopDma ()) {
    return EFI_TIMEOUT;
  }

  EmacRxDescsInit (Private);
  EmacTxDescsInit (Private);

  EmacProgramStationAddress (Private);

  MmioOr32 (EMAC_BASE + EMAC_TX_CTL1, EMAC_TX_CTL1_TX_MD);
  MmioOr32 (EMAC_BASE + EMAC_RX_CTL1, EMAC_RX_CTL1_RX_MD);
  MmioWrite32 (EMAC_BASE + EMAC_CTL1, 8 << EMAC_CTL1_BURST_SHIFT);

  MmioWrite32 (EMAC_BASE + EMAC_RX_DMA_DESC, (UINT32)(UINTN)Private->RxDescriptors);
  MmioWrite32 (EMAC_BASE + EMAC_TX_DMA_DESC, (UINT32)(UINTN)Private->TxDescriptors);

  Value = (Private->FullDuplex ? EMAC_MAC_FULL_DUPLEX : 0) |
          (Private->LinkSpeed == 10 ? (2 << 2) :
           (Private->LinkSpeed == 100 ? EMAC_MAC_SPEED_100 : EMAC_MAC_SPEED_1000));
  MmioWrite32 (EMAC_BASE + EMAC_CTL0, Value);

  MmioOr32 (EMAC_BASE + EMAC_RX_CTL1,
            EMAC_RX_CTL1_RX_DMA_EN | EMAC_RX_CTL1_RX_ERR_FRM | EMAC_RX_CTL1_RX_RUNT_FRM);
  MmioOr32 (EMAC_BASE + EMAC_TX_CTL1, EMAC_TX_CTL1_TX_DMA_EN);

  MmioOr32 (EMAC_BASE + EMAC_RX_CTL0, EMAC_RX_CTL0_RX_EN);
  MmioOr32 (EMAC_BASE + EMAC_TX_CTL0, EMAC_TX_CTL0_TX_EN);

  Private->Mode.MediaPresent = TRUE;
  Private->Mode.State        = EfiSimpleNetworkInitialized;

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
  UINT32        Setting;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped)
    return EFI_NOT_STARTED;

  if (((Enable | Disable) & ~Private->Mode.ReceiveFilterMask) != 0)
    return EFI_INVALID_PARAMETER;

  Setting = Reset ? (EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST)
                  : Private->Mode.ReceiveFilterSetting;
  Setting = (Setting | Enable) & ~Disable;

  Private->Mode.ReceiveFilterSetting = Setting;

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
  if ((Ip == NULL) || (Mac == NULL))
    return EFI_INVALID_PARAMETER;

  SetMem (Mac, sizeof (*Mac), 0);

  if (IPv6) {
    Mac->Addr[0] = 0x33;
    Mac->Addr[1] = 0x33;
    Mac->Addr[2] = Ip->v6.Addr[12];
    Mac->Addr[3] = Ip->v6.Addr[13];
    Mac->Addr[4] = Ip->v6.Addr[14];
    Mac->Addr[5] = Ip->v6.Addr[15];
  } else {
    // 01:00:5e + low 23 bits of the IPv4 address
    Mac->Addr[0] = 0x01;
    Mac->Addr[1] = 0x00;
    Mac->Addr[2] = 0x5e;
    Mac->Addr[3] = Ip->v4.Addr[1] & 0x7f;
    Mac->Addr[4] = Ip->v4.Addr[2];
    Mac->Addr[5] = Ip->v4.Addr[3];
  }

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
  EMAC_PRIVATE     *Private;
  EMAC_DESCRIPTOR  *Desc;

  Private = EMAC_PRIVATE_FROM_SNP (This);

  if (Private->Mode.State == EfiSimpleNetworkStopped)
    return EFI_NOT_STARTED;

  if (Private->Mode.State != EfiSimpleNetworkInitialized)
    return EFI_DEVICE_ERROR;

  if (InterruptStatus != NULL) {
    *InterruptStatus = EmacPacketAvailable (Private) ? EFI_SIMPLE_NETWORK_RECEIVE_INTERRUPT : 0;
  }

  if (TxBuf != NULL) {
    *TxBuf = NULL;

    if (Private->TxCleanIndex != Private->TxIndex) {
      Desc = &Private->TxDescriptors[Private->TxCleanIndex];
      InvalidateDataCacheRange (Desc, sizeof (*Desc));

      if ((Desc->Status & EMAC_DESC_OWN) == 0) {
        *TxBuf = Private->TxUserBuffer[Private->TxCleanIndex];
        Private->TxUserBuffer[Private->TxCleanIndex] = NULL;
        Private->TxCleanIndex = (Private->TxCleanIndex + 1) % EMAC_RING_SIZE;

        if ((InterruptStatus != NULL) && (*TxBuf != NULL))
          *InterruptStatus |= EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT;
      }
    }
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
  UINTN            Next;

  Private = EMAC_PRIVATE_FROM_SNP (This);

  if (Private->Mode.State != EfiSimpleNetworkInitialized)
    return EFI_NOT_STARTED;

  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;

  if (BufferSize < Private->Mode.MediaHeaderSize)
    return EFI_BUFFER_TOO_SMALL;

  if (BufferSize > (UINTN)Private->Mode.MaxPacketSize + Private->Mode.MediaHeaderSize)
    return EFI_INVALID_PARAMETER;

  if ((HeaderSize != 0) && ((HeaderSize != Private->Mode.MediaHeaderSize) || (Dest == NULL) || (Protocol == NULL)))
    return EFI_INVALID_PARAMETER;

  Next = (Private->TxIndex + 1) % EMAC_RING_SIZE;
  if (Next == Private->TxCleanIndex) {
    InvalidateDataCacheRange (&Private->TxDescriptors[Private->TxCleanIndex], sizeof (Private->TxDescriptors[Private->TxCleanIndex]));
    if ((Private->TxDescriptors[Private->TxCleanIndex].Status & EMAC_DESC_OWN) == 0) {
      Private->TxUserBuffer[Private->TxCleanIndex] = NULL;
      Private->TxCleanIndex = (Private->TxCleanIndex + 1) % EMAC_RING_SIZE;
    } else {
      return EFI_NOT_READY;
    }
  }

  Desc = &Private->TxDescriptors[Private->TxIndex];
  InvalidateDataCacheRange (Desc, sizeof (*Desc));
  if (Desc->Status & EMAC_DESC_OWN) {
    DEBUG ((EFI_D_WARN, "EmacDxe: TX ring full at descriptor %u\n", (UINT32)Private->TxIndex));
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
  WriteBackInvalidateDataCacheRange (TxPacket, (BufferSize + 127) & ~127);

  Desc->BufferAddress = (UINT32)(UINTN)TxPacket;
  Desc->ControlSize   = (UINT32)BufferSize | EMAC_DESC_CHAIN_SECOND | EMAC_DESC_LAST_DESC | EMAC_DESC_FIRST_DESC;
  Desc->Status        = EMAC_DESC_OWN;
  WriteBackInvalidateDataCacheRange (Desc, sizeof (*Desc));

  Private->TxUserBuffer[Private->TxIndex] = Buffer;
  Private->TxIndex = Next;

  MmioOr32 (EMAC_BASE + EMAC_TX_CTL1, EMAC_TX_CTL1_TX_DMA_START);

  Private->Statistics.TxTotalFrames++;
  Private->Statistics.TxGoodFrames++;

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
  UINT8            *RxPacket;
  UINT32           Status;
  UINTN            Length;
  UINTN            Scanned;

  Private = EMAC_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State != EfiSimpleNetworkInitialized)
    return EFI_NOT_STARTED;

  if ((BufferSize == NULL) || (Buffer == NULL))
    return EFI_INVALID_PARAMETER;

  for (Scanned = 0; Scanned < EMAC_RING_SIZE; Scanned++) {
    Desc = &Private->RxDescriptors[Private->RxIndex];
    InvalidateDataCacheRange (Desc, sizeof (*Desc));

    Status = Desc->Status;
    if ((Status & EMAC_DESC_OWN) != 0)
      return EFI_NOT_READY;

    RxPacket = Private->RxBuffer + Private->RxIndex * EMAC_PACKET_SIZE;
    Length   = (Status >> 16) & 0x3fff;

    if (((Status & EMAC_DESC_RX_ERROR_MASK) != 0) ||
        (Length < EMAC_MIN_RX_FRAME) ||
        (Length > EMAC_RX_BUFFER_SIZE))
    {
      Private->Statistics.RxDroppedFrames++;
      goto GiveBack;
    }

    Length -= EMAC_FCS_SIZE;

    if (*BufferSize < Length) {
      *BufferSize = Length;
      DEBUG ((EFI_D_WARN, "EmacDxe: RX buffer too small for %u-byte frame\n", (UINT32)Length));
      return EFI_BUFFER_TOO_SMALL;
    }

    InvalidateDataCacheRange (RxPacket, (Length + EMAC_FCS_SIZE + 127) & ~127);
    CopyMem (Buffer, RxPacket, Length);

    if (HeaderSize != NULL)
      *HeaderSize = EMAC_MEDIA_HEADER_SIZE;

    if (Dest != NULL)
      CopyMem (Dest, Buffer, 6);

    if (Src != NULL)
      CopyMem (Src, (UINT8 *)Buffer + 6, 6);

    if (Protocol != NULL)
      *Protocol = (UINT16)(((UINT8 *)Buffer)[12] << 8 | ((UINT8 *)Buffer)[13]);

    *BufferSize = Length;
    Private->Statistics.RxTotalFrames++;
    Private->Statistics.RxGoodFrames++;

    Desc->ControlSize = EMAC_RX_BUFFER_SIZE;
    Desc->Status      = EMAC_DESC_OWN;
    WriteBackInvalidateDataCacheRange (Desc, sizeof (*Desc));
    Private->RxIndex = (Private->RxIndex + 1) % EMAC_RING_SIZE;
    MmioOr32 (EMAC_BASE + EMAC_RX_CTL1, EMAC_RX_CTL1_RX_DMA_START);
    return EFI_SUCCESS;

GiveBack:
    Desc->ControlSize = EMAC_RX_BUFFER_SIZE;
    Desc->Status      = EMAC_DESC_OWN;
    WriteBackInvalidateDataCacheRange (Desc, sizeof (*Desc));
    Private->RxIndex = (Private->RxIndex + 1) % EMAC_RING_SIZE;
    MmioOr32 (EMAC_BASE + EMAC_RX_CTL1, EMAC_RX_CTL1_RX_DMA_START);
  }

  return EFI_NOT_READY;
}

STATIC VOID
EmacFreeDmaMemory (
  IN OUT VOID  **Buffer,
  IN     UINTN  Pages
  )
{
  if (*Buffer != NULL) {
    gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)*Buffer, Pages);
    *Buffer = NULL;
  }
}

STATIC VOID *
EmacAllocateDmaPages (
  IN UINTN  Pages
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Address;

  Address = 0xFFFFFFFF;
  Status  = gBS->AllocatePages (AllocateMaxAddress, EfiReservedMemoryType, Pages, &Address);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  ZeroMem ((VOID *)(UINTN)Address, EFI_PAGES_TO_SIZE (Pages));
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Address, EFI_PAGES_TO_SIZE (Pages));

  return (VOID *)(UINTN)Address;
}

STATIC VOID
EmacDestroy (
  VOID
  )
{
  if (mEmac == NULL) {
    return;
  }

  if (mEmac->ExitBootServicesEvent != NULL)
    gBS->CloseEvent (mEmac->ExitBootServicesEvent);

  if (mEmac->Snp.WaitForPacket != NULL)
    gBS->CloseEvent (mEmac->Snp.WaitForPacket);

  if (mEmac->DevicePath != NULL)
    FreePool (mEmac->DevicePath);

  EmacFreeDmaMemory ((VOID **)&mEmac->TxDescriptors, EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE));
  EmacFreeDmaMemory ((VOID **)&mEmac->RxDescriptors, EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE));
  EmacFreeDmaMemory ((VOID **)&mEmac->TxBuffer, EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE));
  EmacFreeDmaMemory ((VOID **)&mEmac->RxBuffer, EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE));

  FreePool (mEmac);
  mEmac = NULL;
}

STATIC VOID
EmacGenerateMac (
  OUT UINT8  *Mac
  )
{
  UINT32  Sid[4];
  UINTN   Index;

  for (Index = 0; Index < 4; Index++) {
    Sid[Index] = MmioRead32 (EMAC_SID_BASE + Index * 4);
  }

  if ((Sid[0] | Sid[1] | Sid[2] | Sid[3]) == 0) {
    Mac[0] = 0x02; Mac[1] = 0x00; Mac[2] = 0x00;
    Mac[3] = 0x00; Mac[4] = 0x00; Mac[5] = 0x03;
    return;
  }

  if ((Sid[3] & 0xFFFFFF) == 0) {
    Sid[3] |= 0x800000;
  }

  Mac[0] = 0x02;
  Mac[1] = (UINT8)(Sid[0] >> 0);
  Mac[2] = (UINT8)(Sid[3] >> 24);
  Mac[3] = (UINT8)(Sid[3] >> 16);
  Mac[4] = (UINT8)(Sid[3] >> 8);
  Mac[5] = (UINT8)(Sid[3] >> 0);
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

  STATIC_ASSERT (sizeof (EMAC_DESCRIPTOR) == EMAC_DESC_STRIDE, "EMAC descriptor must fill a cache line");

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

  CcmResetAssert (RST_BUS_EMAC0);
  CcmClkDisable (CLK_BUS_EMAC0);

  Private->TxDescriptors = EmacAllocateDmaPages (EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE));
  Private->RxDescriptors = EmacAllocateDmaPages (EFI_SIZE_TO_PAGES (sizeof (EMAC_DESCRIPTOR) * EMAC_RING_SIZE));
  Private->TxBuffer = EmacAllocateDmaPages (EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE));
  Private->RxBuffer = EmacAllocateDmaPages (EFI_SIZE_TO_PAGES (EMAC_PACKET_SIZE * EMAC_RING_SIZE));

  if (!Private->TxDescriptors || !Private->RxDescriptors || !Private->TxBuffer || !Private->RxBuffer) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: DMA buffer allocation failed\n"));
    EmacDestroy ();
    return EFI_OUT_OF_RESOURCES;
  }

  MicroSecondDelay (10);
  CcmClkEnable (CLK_BUS_EMAC0);
  CcmResetDeassert (RST_BUS_EMAC0);

  EmacConfigurePins (GpioProtocol);
  MmioWrite32 (EMAC_SYSCON, EMAC_SYSCON_EPHY_SHUTDOWN | EMAC_SYSCON_EPIT | EMAC_SYSCON_ETCS_INT_GMII | (7 << EMAC_SYSCON_ETXDC_SHIFT));

  EmacStopDma ();

  Private->LinkSpeed = 1000;
  Private->PhyAddress = EMAC_PHY_ADDRESS;

  EmacGenerateMac (Private->Mode.CurrentAddress.Addr);

  CopyMem (&Private->Mode.PermanentAddress, &Private->Mode.CurrentAddress, sizeof (EFI_MAC_ADDRESS));

  Private->Mode.State = EfiSimpleNetworkStopped;

  Private->Mode.HwAddressSize = 6;
  Private->Mode.MediaHeaderSize = EMAC_MEDIA_HEADER_SIZE;

  Private->Mode.MaxPacketSize = EMAC_MTU;

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

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, EmacWaitForPacketNotify, Private, &Private->Snp.WaitForPacket);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: failed to create WaitForPacket event: %r\n", Status));
    EmacDestroy ();
    return Status;
  }

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

  Status = gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, EmacExitBootServicesNotify, Private, &Private->ExitBootServicesEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EmacDxe: failed to create ExitBootServices event: %r\n", Status));
    EmacDestroy ();
    return Status;
  }

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