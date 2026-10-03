##
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2018 - 2019, Bingxing Wang. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

!include SiliciumPkg/SiliciumPkg.dsc.inc

[LibraryClasses]
  ClockLib|AllwinnerPkg/Library/ClockLib/ClockLib.inf
  RamManagerLib|AllwinnerPkg/Library/RamManagerLib/RamManagerLib.inf

[Components]
  #
  # Display Driver
  #
  AllwinnerPkg/Drivers/DisplayDxe/DisplayDxe.inf

  #
  # GPIO Driver
  #
  AllwinnerPkg/Drivers/GpioDxe/GpioDxe.inf

  #
  # "RNG" Driver
  #
  AllwinnerPkg/Drivers/RngDxe/RngDxe.inf

  #
  # Ethernet MAC
  #
  AllwinnerPkg/Drivers/EmacDxe/EmacDxe.inf

  #
  # USB host driver
  #
  AllwinnerPkg/Drivers/UsbHostDxe/UsbHostDxe.inf

  #
  # I2C Driver
  #
  AllwinnerPkg/Drivers/I2cDxe/I2cDxe.inf

  #
  # PMIC Driver
  #
  AllwinnerPkg/Drivers/PmicDxe/PmicDxe.inf

  #
  # Storage Drivers
  #
  AllwinnerPkg/Drivers/MmcDxe/MmcDxe.inf