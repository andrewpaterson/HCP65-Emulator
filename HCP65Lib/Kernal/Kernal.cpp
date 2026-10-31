#include "BaseLib/FatVolume.h"
#include "KernalDevices.h"
#include "KernalSDCard.h"
#include "Kernal.h"


SKernalStatus	gsKernalStatus = { KERNAL_STATE_STARTING, 0, 0, 0, 0, 0, 0 };
CKernalSDCard	gcKernalSDCard;
CFatVolume		gcKernalVolume;


//////////////////////////////////////////////////////////////////////////
//
// Lets the device in uiSlot run (the processor board holds every device in
// reset until it is enabled).
//
//////////////////////////////////////////////////////////////////////////
static void KernalEnableDevice(uint16 uiSlot)
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


//////////////////////////////////////////////////////////////////////////
//
// Called by the start up once MPU RAM holds the kernal and its globals are
// initialised.  Running in kernel mode, native, with interrupts disabled.
// Finds the devices, brings up the SD card and mounts its FAT32 volume.
//
//////////////////////////////////////////////////////////////////////////
int main(void)
{
	SKernalDevice*	psSDCardBoard;
	EFatCode		eResult;
	uint16			uiSlot;

	KernalScanDevices();
	gsKernalStatus.uiState = KERNAL_STATE_DEVICES_SCANNED;

	psSDCardBoard = KernalFindDevice(KERNAL_SD_CARD_NAME);
	if (!psSDCardBoard)
	{
		gsKernalStatus.uiState = KERNAL_STATE_NO_SD_CARD_BOARD;
		return 1;
	}
	uiSlot = (uint16)(psSDCardBoard - gasKernalDevices);
	KernalEnableDevice(uiSlot);

	gsKernalStatus.uiSDError = gcKernalSDCard.Init(psSDCardBoard);
	if (gsKernalStatus.uiSDError != KERNAL_SD_SUCCESS)
	{
		gsKernalStatus.uiState = KERNAL_STATE_SD_CARD_FAILED;
		return 1;
	}
	gsKernalStatus.uiNumSectors = (uint32)(gcKernalSDCard.GetTotalSectors() + 1);

	eResult = gcKernalVolume.Mount(&gcKernalSDCard);
	gsKernalStatus.uiFatCode = (uint16)eResult;
	if (eResult != FAT_SUCCESS)
	{
		gsKernalStatus.uiSDError = gcKernalSDCard.GetError();
		gsKernalStatus.uiState = KERNAL_STATE_MOUNT_FAILED;
		return 1;
	}
	gsKernalStatus.uiFileSystem = (uint16)gcKernalVolume.GetFileSystemType();
	gsKernalStatus.uiNumClusters = gcKernalVolume.GetNumClusters();
	gsKernalStatus.uiFreeClusters = gcKernalVolume.GetTotalFreeClusters();
	gsKernalStatus.uiState = KERNAL_STATE_MOUNTED;
	return 0;
}
