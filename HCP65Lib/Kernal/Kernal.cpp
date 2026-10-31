#include "BaseLib/FatVolume.h"
#include "BaseLib/TrueMallocator.h"
#include "BaseLib/GlobalMemory.h"
#include "KernalDevices.h"
#include "KernalSDCard.h"
#include "KernalProcess.h"
#include "Kernal.h"


SKernalStatus	gsKernalStatus = { KERNAL_STATE_STARTING, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
CKernalSDCard	gcKernalSDCard;
CFatVolume		gcKernalVolume;


//Libc65's heap.
extern CTrueMallocator	gcHeap;
extern bool				gbHeap;


//////////////////////////////////////////////////////////////////////////
//
// Called by the start up before the init table: the kernal's heap is main
// memory on the 4MB card (which is always there), where the SD Card Board
// can reach it too.
//
//////////////////////////////////////////////////////////////////////////
extern "C" void KernalInitHeap(void)
{
	gcHeap.Init((void*)KERNAL_HEAP_START, KERNAL_HEAP_END - KERNAL_HEAP_START);
	gbHeap = true;
}


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
// Finds the devices, brings up the SD card, mounts its FAT32 volume and
// runs boot.exe from it.
//
//////////////////////////////////////////////////////////////////////////
int main(void)
{
	SKernalDevice*	psSDCardBoard;
	EFatCode		eResult;
	uint16			uiSlot;

	//BaseLib's memory: the allocators a chunk file (an executable) names are looked up there.
	MemoryInit();

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

	gsKernalStatus.uiBootResult = KernalRunExecutable(KERNAL_BOOT_FILE, &gsKernalStatus.uiExitCode);
	gsKernalStatus.uiState = (gsKernalStatus.uiBootResult == KERNAL_PROCESS_EXITED) ? KERNAL_STATE_BOOT_EXITED : KERNAL_STATE_BOOT_FAILED;
	return 0;
}
