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

/** Set DLDO1 voltage and enable the rail. */
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_SET_DLDO1_MILLIVOLT) (
  IN ALLWINNER_PMIC_PROTOCOL  *This,
  IN UINT32                    MilliVolt
  );

/** Read back the DLDO1 voltage from hardware. */
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_GET_DLDO1_MILLIVOLT) (
  IN  ALLWINNER_PMIC_PROTOCOL  *This,
  OUT UINT32                   *MilliVolt
  );

/** Enable or disable DLDO1 without changing its programmed voltage. */
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_SET_DLDO1_ENABLED) (
  IN ALLWINNER_PMIC_PROTOCOL  *This,
  IN BOOLEAN                   Enabled
  );

/** Set ALDO1 voltage and enable the rail. */
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_SET_ALDO1_MILLIVOLT) (
  IN ALLWINNER_PMIC_PROTOCOL  *This,
  IN UINT32                    MilliVolt
  );

/** Read back the ALDO1 voltage from hardware. */
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_GET_ALDO1_MILLIVOLT) (
  IN  ALLWINNER_PMIC_PROTOCOL  *This,
  OUT UINT32                   *MilliVolt
  );

/** Enable or disable ALDO1 without changing its programmed voltage. */
typedef
EFI_STATUS
(EFIAPI *ALLWINNER_PMIC_SET_ALDO1_ENABLED) (
  IN ALLWINNER_PMIC_PROTOCOL  *This,
  IN BOOLEAN                   Enabled
  );

struct _ALLWINNER_PMIC_PROTOCOL {
  ALLWINNER_PMIC_SET_DCDC_MILLIVOLT  SetDcdcMilliVolt;
  ALLWINNER_PMIC_GET_DCDC_MILLIVOLT  GetDcdcMilliVolt;
  ALLWINNER_PMIC_SET_DLDO1_MILLIVOLT SetDldo1MilliVolt;
  ALLWINNER_PMIC_GET_DLDO1_MILLIVOLT GetDldo1MilliVolt;
  ALLWINNER_PMIC_SET_DLDO1_ENABLED   SetDldo1Enabled;
  ALLWINNER_PMIC_SET_ALDO1_MILLIVOLT SetAldo1MilliVolt;
  ALLWINNER_PMIC_GET_ALDO1_MILLIVOLT GetAldo1MilliVolt;
  ALLWINNER_PMIC_SET_ALDO1_ENABLED   SetAldo1Enabled;
};

extern EFI_GUID gAllwinnerPmicProtocolGuid;

#endif // ALLWINNER_PMIC_H_
