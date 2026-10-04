#ifndef __SPINORFLASH_DXE_H__
#define __SPINORFLASH_DXE_H__

#define NOR_READ_CHUNK_MAX      (64 - 4)
#define NOR_WRITE_CHUNK_MAX     (64 - 4)
 
// CMDs
#define NOR_CMD_READ            0x03
#define NOR_CMD_READ_ID         0x9F
#define NOR_CMD_WREN            0x06
#define NOR_CMD_RDSR            0x05
#define NOR_CMD_PP              0x02
#define NOR_CMD_SE              0x20

// Status Register
#define NOR_SR_WIP              0x01

// Flash sizes
#define NOR_PAGE_SIZE           256
#define NOR_SECTOR_SIZE         4096

#endif // __SPINORFLASH_DXE_H__