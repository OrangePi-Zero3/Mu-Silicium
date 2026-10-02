#ifndef _RAM_MANAGER_H_
#define _RAM_MANAGER_H_

#define SPL_MAJOR_BITS		3
#define SPL_MINOR_BITS		5
#define SPL_VERSION(maj, min)						\
	((((maj) & ((1U << SPL_MAJOR_BITS) - 1)) << SPL_MINOR_BITS) | \
	((min) & ((1U << SPL_MINOR_BITS) - 1)))

#define SPL_DRAM_HEADER_VERSION	SPL_VERSION(0, 3)

struct BootFileHead {
	UINT32 JumpInstruction;
	UINT8 Magic[8];
	UINT32 CheckSum;
	UINT32 Length;
	union {
		UINT32 PubHeadSize;
		UINT8 SplSignature[4];
	};
	UINT32 FelScriptAddress;
	UINT32 FelUEnvLength;
	UINT32 DtNameAddress;
	UINT32 DramSize;
	UINT32 BootMedia;
	UINT32 StringPool[13];
};

#endif /* _RAM_MANAGER_H_ */
