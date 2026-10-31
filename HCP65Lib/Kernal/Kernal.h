#ifndef __KERNAL_H__
#define __KERNAL_H__
#include "BaseLib/PrimitiveTypes.h"


#define KERNAL_STATE_STARTING			0x0001
#define KERNAL_STATE_DEVICES_SCANNED	0x0002
#define KERNAL_STATE_NO_SD_CARD_BOARD	0x0003
#define KERNAL_STATE_SD_CARD_FAILED		0x0004
#define KERNAL_STATE_MOUNT_FAILED		0x0005
#define KERNAL_STATE_MOUNTED			0x0006


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
};


extern SKernalStatus	gsKernalStatus;


#endif // __KERNAL_H__
