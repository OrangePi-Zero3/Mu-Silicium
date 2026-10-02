#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/RamManagerLib.h>
#include <Library/IoLib.h>

#include "RamManager.h"

//
// Global Variables
//
STATIC EFI_PHYSICAL_ADDRESS MemoryStart;
STATIC EFI_PHYSICAL_ADDRESS MemoryEnd;

EFI_STATUS
GetUsableMemoryRanges (
  OUT EFI_MEMORY_RANGE **Range,
  OUT UINT8             *RangeCount)
{
  // Set Inital Values
  EFI_PHYSICAL_ADDRESS  CurrentMemoryStart = MemoryStart;
  EFI_MEMORY_RANGE     *LocalRange         = NULL;
  UINT8                 LocalRangeCount    = 1;

  // Allocate Memory
  LocalRange = AllocateZeroPool (sizeof (EFI_MEMORY_RANGE) * LocalRangeCount);
  if (LocalRange == NULL) {
    DEBUG ((EFI_D_ERROR, "Failed to Allocate Memory for Memory Ranges!\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  // Add Remaining Memory
  if (MemoryEnd > CurrentMemoryStart) {
    LocalRange[LocalRangeCount - 1].Address = CurrentMemoryStart;
    LocalRange[LocalRangeCount - 1].Length  = MemoryEnd - CurrentMemoryStart;
  }

  // Pass Data
  *Range      = LocalRange;
  *RangeCount = LocalRangeCount;

  return EFI_SUCCESS;
}

EFI_STATUS
GetMemorySpace ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR SramRegion;
  UINT64                       MemorySize;

  // Locate "NS_IRAM" Memory Region
  Status = LocateMemoryRegionByName ("SRAM", &SramRegion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate \"SRAM\" Memory Region!\n"));
    return Status;
  }

  struct BootFileHead *BootHeader = (struct BootFileHead *)(UINTN)SramRegion.Address;
  if (BootHeader->SplSignature[3] != SPL_DRAM_HEADER_VERSION) {
    DEBUG ((EFI_D_ERROR, "Invalid SPL Signature, expected %d, got %d!\n", SPL_DRAM_HEADER_VERSION, BootHeader->SplSignature[3]));
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((EFI_D_WARN, "Detected %u MiB of System Memory!\n", BootHeader->DramSize));

  // Get System Memory Size
  MemorySize = BootHeader->DramSize << 20;

  // Set Memory Address
  MemoryStart = FixedPcdGet64 (PcdSystemMemoryBase);

  // Set Memory End Address
  MemoryEnd = MemoryStart + MemorySize;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GetMemoryData (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // Get Memory Start & End
  Status = GetMemorySpace ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

