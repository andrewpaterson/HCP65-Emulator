#include <string.h>
#include "KernalDevices.h"


SKernalDevice	gasKernalDevices[KERNAL_NUM_DEVICE_SLOTS];


//////////////////////////////////////////////////////////////////////////
//
// Kernel mode addresses: devices 1 to 7 are 32KB from $108000, devices 16
// to 19 are 128KB from $180000.
//
//////////////////////////////////////////////////////////////////////////
static void KernalSlotAddress(uint16 uiSlot, uint32* puiAddress, uint32* puiSize)
{
	if ((uiSlot >= 1) && (uiSlot <= 7))
	{
		*puiAddress = 0x100000 + ((uint32)uiSlot << 15);
		*puiSize = 0x8000;
	}
	else if ((uiSlot >= 16) && (uiSlot <= 19))
	{
		*puiAddress = 0x180000 + ((uint32)(uiSlot - 16) << 17);
		*puiSize = 0x20000;
	}
	else
	{
		*puiAddress = 0;
		*puiSize = 0;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// Reads each socket's identifier: the zero terminated string at the start
// of its ROM.  A socket is empty if its first byte reads $FF (nothing
// drives the bus) or 0.
//
//////////////////////////////////////////////////////////////////////////
void KernalScanDevices(void)
{
	SKernalDevice*		psDevice;
	volatile uint8*		puiROM;
	uint16				uiSlot;
	uint16				i;
	uint8				c;

	for (uiSlot = 0; uiSlot < KERNAL_NUM_DEVICE_SLOTS; uiSlot++)
	{
		psDevice = &gasKernalDevices[uiSlot];
		KernalSlotAddress(uiSlot, &psDevice->uiAddress, &psDevice->uiSize);
		psDevice->bPresent = false;
		psDevice->szName[0] = 0;
		if (psDevice->uiAddress == 0)
		{
			continue;
		}

		puiROM = (volatile uint8*)psDevice->uiAddress;
		for (i = 0; i < KERNAL_DEVICE_NAME_LENGTH - 1; i++)
		{
			c = puiROM[i];
			if ((c == 0) || (c == 0xFF))
			{
				break;
			}
			psDevice->szName[i] = (char)c;
		}
		psDevice->szName[i] = 0;
		psDevice->bPresent = i > 0;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// The first present device whose identifier is szName, or NULL.
//
//////////////////////////////////////////////////////////////////////////
SKernalDevice* KernalFindDevice(const char* szName)
{
	uint16	uiSlot;

	for (uiSlot = 0; uiSlot < KERNAL_NUM_DEVICE_SLOTS; uiSlot++)
	{
		if (gasKernalDevices[uiSlot].bPresent && (strcmp(gasKernalDevices[uiSlot].szName, szName) == 0))
		{
			return &gasKernalDevices[uiSlot];
		}
	}
	return NULL;
}


//////////////////////////////////////////////////////////////////////////
//
// The processor board's registers (device slot 0).
//
//////////////////////////////////////////////////////////////////////////
uint8 KernalReadRegister(uint8 uiRegister)
{
	return ((volatile uint8*)KERNAL_REGISTERS)[uiRegister];
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void KernalWriteRegister(uint8 uiRegister, uint8 uiValue)
{
	((volatile uint8*)KERNAL_REGISTERS)[uiRegister] = uiValue;
}


//////////////////////////////////////////////////////////////////////////
//
// Lets the device in uiSlot run (the processor board holds every device in
// reset until it is enabled).
//
//////////////////////////////////////////////////////////////////////////
void KernalEnableDevice(uint16 uiSlot)
{
	uint8	uiRegister;
	uint8	uiBit;

	if (uiSlot >= 16)
	{
		uiRegister = 5;
		uiBit = (uint8)(1 << (uiSlot - 16));
	}
	else
	{
		uiRegister = 3;
		uiBit = (uint8)(1 << uiSlot);
	}
	KernalWriteRegister(uiRegister, (uint8)(KernalReadRegister(uiRegister) | uiBit));
}
