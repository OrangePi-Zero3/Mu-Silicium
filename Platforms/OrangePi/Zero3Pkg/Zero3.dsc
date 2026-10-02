##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = Zero3
  PLATFORM_GUID                  = FFFA8AF8-0C7A-4240-A68F-4F3611DB3547
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/Zero3Pkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Zero3Pkg/Zero3.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 1

!include H616Pkg/H616Pkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x4A0E8000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"OrangePi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Zero3"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"Zero3"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"OrangePi_Zero3_Zero3"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Zero3"

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|Zero3Pkg/Library/MemoryMapLib/MemoryMapLib.inf
