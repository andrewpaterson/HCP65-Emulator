#ifndef __KERNAL_H__
#define __KERNAL_H__
#include "BaseLib/PrimitiveTypes.h"


#define KERNAL_BOOT_FILE				"\\boot.exe"
#define KERNAL_HEAP_START				0x440000	//Main memory on the 4MB card, after the math tables.
#define KERNAL_HEAP_END					0x500000

#define KERNAL_STATE_STARTING			0x0001
#define KERNAL_STATE_DEVICES_SCANNED	0x0002
#define KERNAL_STATE_NO_SD_CARD_BOARD	0x0003
#define KERNAL_STATE_SD_CARD_FAILED		0x0004
#define KERNAL_STATE_MOUNT_FAILED		0x0005
#define KERNAL_STATE_MOUNTED			0x0006
#define KERNAL_STATE_BOOT_FAILED		0x0007
#define KERNAL_STATE_BOOT_EXITED		0x0008


//What the kernal found starting up, for tests and debuggers to read.
struct SKernalStatus
{
	uint16	uiState;		//KERNAL_STATE_*.
	uint16	uiSDError;		//KERNAL_SD_* when the card failed.
	uint16	uiFatCode;		//The EFatCode Mount returned.
	uint16	uiFileSystem;	//EFatFileSystemType.
	uint32	uiNumSectors;
	uint32	uiNumClusters;
	uint32	uiFreeClusters;
	uint16	uiBootResult;	//KERNAL_PROCESS_* for boot.exe.
	uint16	uiExitCode;		//boot.exe's exit code.
	uint16	uiUserValue;	//The last value a process gave the kernal (COP 1).
};


extern SKernalStatus	gsKernalStatus;


extern "C" void	KernalInitHeap(void);


#endif // __KERNAL_H__
