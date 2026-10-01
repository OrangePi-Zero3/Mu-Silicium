/** @file
  Protocol for setting regulator voltages on the board PMIC.

  Exists so the CPU clock driver can raise the core voltage before raising the
  CPU frequency, and can refuse to clock up at all if the PMIC is not there.
  The dependency is expressed as a real depex on this protocol, so the ordering
  cannot silently invert.

  Copyright (c) 2026, Community EDK2 Orange Pi Zero3 port.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef ALLWINNER_PMIC_H_
#define ALLWINNER_PMIC_H_

#define ALLWINNER_PMIC_PROTOCOL_GUID \
  { 0xac669a62, 0x88c9, 0x4f64, { 0xbe, 0xff, 0xc7, 0x80, 0xbf, 0x37, 0x1f, 0x53 } }

typedef struct _ALLWINNER_PMIC_PROTOCOL ALLWINNER_PMIC_PROTOCOL;

/**
  Set a DC-DC converter output voltage.

  @param[in] This        This protocol instance.
  @param[in] Dcdc        Converter number, 1-based (DCDC2 is vdd-cpu).
  @param[in] MilliVolt   Requested voltage in mV.

  @retval EFI_SUCCESS            Voltage programmed and the rail enabled.
  @retval EFI_INVALID_PARAMETER  Converter number or voltage out of range.
  @retval EFI_DEVICE_ERROR       The I2C transfer failed.
**/
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_SET_DCDC_MILLIVOLT) (
  IN ALLWINNER_PMIC_PROTOCOL  *This,
  IN UINT8                     Dcdc,
  IN UINT32                    MilliVolt
  );

/**
  Read back a DC-DC converter output voltage.

  Reads the hardware rather than a cached value, so it can be used to confirm a
  write actually took effect before acting on it.
**/
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_GET_DCDC_MILLIVOLT) (
  IN  ALLWINNER_PMIC_PROTOCOL  *This,
  IN  UINT8                     Dcdc,
  OUT UINT32                   *MilliVolt
  );

struct _ALLWINNER_PMIC_PROTOCOL {
  ALLWINNER_PMIC_SET_DCDC_MILLIVOLT  SetDcdcMilliVolt;
  ALLWINNER_PMIC_GET_DCDC_MILLIVOLT  GetDcdcMilliVolt;
};

extern EFI_GUID gAllwinnerPmicProtocolGuid;

#endif // ALLWINNER_PMIC_H_
