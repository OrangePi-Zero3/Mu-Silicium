##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

!include AllwinnerPkg/AllwinnerPkg.dsc.inc

[Defines]
  #
  # Timer
  #
  USE_PHYSICAL_TIMER                         = 0

  #
  # Skip shipping UFP
  #
  SKIP_UFP = 1

[BuildOptions]
  *_*_AARCH64_CC_FLAGS = -DSKIP_UFP=1

[PcdsFixedAtBuild]
  #
  # CPU
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|4
  gArmPlatformTokenSpaceGuid.PcdClusterCount|1

  #
  # GIC
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x03021000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x03022000

  #
  # Timer
  #
  gSiliciumPkgTokenSpaceGuid.PcdTimerFreqOverwrite|24000000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"Allwinner (TM) H616 @ 1.51 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"AWH616"

[LibraryClasses]
  #
  # SEC Libraries
  #
  PlatformSecLib|H616Pkg/Library/PlatformSecLib/PlatformSecLib.inf

  #
  # IO Libraries
  #
  SerialPortLib|H616Pkg/Library/SerialPortLib/SerialPortLib.inf
