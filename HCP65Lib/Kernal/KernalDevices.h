#ifndef __KERNAL_DEVICES_H__
#define __KERNAL_DEVICES_H__
#include "BaseLib/PrimitiveTypes.h"


#define KERNAL_NUM_DEVICE_SLOTS		20		//0 is the processor board's registers; 8 to 15 don't exist.
#define KERNAL_DEVICE_NAME_LENGTH	32		//The identifier read from a device's ROM, with its terminating zero.
#define KERNAL_REGISTERS			0x100000


//A device socket and what was found in it.  An empty socket's bus floats high, so its ROM reads $FF.
struct SKernalDevice
{
	uint32	uiAddress;		//The slot's first address, 0 if the slot doesn't exist.
	uint32	uiSize;
	bool	bPresent;
	char	szName[KERNAL_DEVICE_NAME_LENGTH];
};


extern SKernalDevice	gasKernalDevices[KERNAL_NUM_DEVICE_SLOTS];


void			KernalScanDevices(void);
SKernalDevice*	KernalFindDevice(const char* szName);
uint8			KernalReadRegister(uint8 uiRegister);
void			KernalWriteRegister(uint8 uiRegister, uint8 uiValue);


#endif // __KERNAL_DEVICES_H__
