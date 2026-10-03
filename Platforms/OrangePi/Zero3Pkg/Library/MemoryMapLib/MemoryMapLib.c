#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
  {"BL31 Reserved",      0x40000000, 0x00080000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"UEFI FD",            0x4A000000, 0x000E8000, AddMem, SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK},
  {"UEFI Stack",         0x4A0E8000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"DXE Heap",           0x4A128000, 0x03C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},

  // SRAM
  {"SRAM",               0x00020000, 0x00008000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},

  // Register Regions
  {"Display Engine",     0x01000000, 0x01400000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"System Control",     0x03000000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"CCU",                0x03001000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"SID",                0x03006000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"GIC Distributor",    0x03021000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"GIC Redistributors", 0x03022000, 0x00002000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"Pin Controller",     0x0300B000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"MMC0",               0x04020000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"UART",               0x05000000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"EMAC",               0x05020000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"USB0 & USB PHY",     0x05100000, 0x00002000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"USB1",               0x05200000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"USB2",               0x05310000, 0x00002000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"USB3",               0x05311000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"HDMI Controller",    0x06000000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"HDMI PHY",           0x06010000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"TCON TOP",           0x06510000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"TCON LCD0",          0x06511000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"TCON TV0",           0x06515000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"R_PRCM",             0x07010000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"R_PIO",              0x07022000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"R_TWI",              0x07081000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  // Pass Data
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
