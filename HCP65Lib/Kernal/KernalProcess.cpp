#include <stdlib.h>
#include <string.h>
#include "BaseLib/MemoryFile.h"
#include "BaseLib/ChunkFile.h"
#include "BaseLib/FatFile.h"
#include "BaseLib/FatAbstractFile.h"
#include "Assembler65Lib/Asm65Object.h"
#include "KernalDevices.h"
#include "Kernal.h"
#include "KernalProcess.h"


#define KERNAL_PROCESS_ENTRY_SYMBOL		"UserStart"
#define KERNAL_PROCESS_MEMORY_SYMBOL	"UserMemorySize"
#define KERNAL_PROCESS_STATUS			0x34		//The process starts with 8 bit registers and interrupts off.
#define KERNAL_MAX_PATH					128
#define KERNAL_COPY_CHUNK				0x1000		//A file is read to a process through a buffer this big.

//Sound board (HCP65Lib/HCP65Audio.h): its registers and its two sample RAMs, offsets in its slot.
#define KERNAL_AUDIO_REGISTERS			0x400
#define KERNAL_AUDIO_RAM_A				0x10000
#define KERNAL_AUDIO_RAM_B				0x18000
#define KERNAL_AUDIO_RAM_SIZE			0x8000

//Banks every process gets: 0 (its stack and direct page) and $FF (where Libc65 reads its memory size).  Without a
//memory size its heap is banks $08-$0F.
static const uint8 gauiKernalProcessBanks[] = { 0x00, 0xFF };
static const uint8 gauiKernalLowHeapBanks[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };


extern CFatVolume	gcKernalVolume;

//The sound board driver (KernalAudio.asm).
extern "C" void		KernalAudioCopy(uint8* puiDest, uint8* puiSource, uint16 uiCount);
extern "C" void		KernalAudioSetRegister(uint8* puiRegisters, uint16 uiRegister, uint16 uiValue);
extern "C" uint16	KernalAudioGetCounter(uint8* puiRegisters);

SKernalCall			gsKernalCall;
static uint8		guiKernalProcess;							//The process running (or calling), 0 for none.
static uint16		gauiKernalArguments[256];					//Each process ID's argument.
static uint8		gauiKernalBankOwners[KERNAL_LAST_PROCESS_BANK + 1];	//The process ID a physical bank is given to.
static SKernalDevice*	gpsKernalSoundBoard;
static uint8*			gpuiKernalArguments;					//The call's arguments, on the process's stack in its bank 0.


//////////////////////////////////////////////////////////////////////////
//
// A process's page of the remap table: entry n is the physical bank its
// bank n is.
//
//////////////////////////////////////////////////////////////////////////
static uint8* KernalRemapPage(uint8 uiProcess)
{
	return (uint8*)(KERNAL_REMAP_TABLE + ((uint32)uiProcess << 8));
}


//////////////////////////////////////////////////////////////////////////
//
// The physical main memory address of the running process's address, and
// how many bytes from it are in the same bank.  NULL if the process has no
// such bank.
//
//////////////////////////////////////////////////////////////////////////
static uint8* KernalUserPointer(uint32 uiAddress, uint32* puiInBank)
{
	uint8	uiBank;

	uiBank = KernalRemapPage(guiKernalProcess)[(uiAddress >> 16) & 0xFF];
	if (uiBank == 0)
	{
		return NULL;
	}
	if (puiInBank)
	{
		*puiInBank = 0x10000 - (uiAddress & 0xFFFF);
	}
	return (uint8*)(((uint32)uiBank << 16) | (uiAddress & 0xFFFF));
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
static uint8* KernalUserAddress(uint32 uiAddress)
{
	return KernalUserPointer(uiAddress, NULL);
}


//////////////////////////////////////////////////////////////////////////
//
// Copies between kernal memory and the running process's.  False if the
// process hasn't all of the banks.
//
//////////////////////////////////////////////////////////////////////////
static bool KernalCopyToUser(uint32 uiAddress, const uint8* puiSource, uint32 uiLength)
{
	uint8*	puiDest;
	uint32	uiInBank;

	while (uiLength > 0)
	{
		puiDest = KernalUserPointer(uiAddress, &uiInBank);
		if (!puiDest)
		{
			return false;
		}
		uiInBank = (uiInBank < uiLength) ? uiInBank : uiLength;
		memcpy(puiDest, puiSource, uiInBank);
		uiAddress += uiInBank;
		puiSource += uiInBank;
		uiLength -= uiInBank;
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
static bool KernalCopyFromUser(uint8* puiDest, uint32 uiAddress, uint32 uiLength)
{
	uint8*	puiSource;
	uint32	uiInBank;

	while (uiLength > 0)
	{
		puiSource = KernalUserPointer(uiAddress, &uiInBank);
		if (!puiSource)
		{
			return false;
		}
		uiInBank = (uiInBank < uiLength) ? uiInBank : uiLength;
		memcpy(puiDest, puiSource, uiInBank);
		uiAddress += uiInBank;
		puiDest += uiInBank;
		uiLength -= uiInBank;
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
// The running process's call arguments: the words on its stack from
// uiOffset.
//
//////////////////////////////////////////////////////////////////////////
static uint16 KernalArgumentWord(uint16 uiOffset)
{
	return (uint16)(gpuiKernalArguments[uiOffset] | (gpuiKernalArguments[uiOffset + 1] << 8));
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
static uint32 KernalArgumentLong(uint16 uiOffset)
{
	return (uint32)KernalArgumentWord(uiOffset) | ((uint32)KernalArgumentWord((uint16)(uiOffset + 2)) << 16);
}


//////////////////////////////////////////////////////////////////////////
//
// A zero terminated string from the running process.  False if it is too
// long or not in its memory.
//
//////////////////////////////////////////////////////////////////////////
static bool KernalUserString(uint32 uiAddress, char* szDest, uint16 uiMax)
{
	uint8*	puiChar;
	uint16	i;

	for (i = 0; i < uiMax; i++)
	{
		puiChar = KernalUserAddress(uiAddress + i);
		if (!puiChar)
		{
			return false;
		}
		szDest[i] = (char)*puiChar;
		if (szDest[i] == '\0')
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
//
// Gives the process's bank uiBank a cleared physical bank, if it hasn't
// one.  False if there are none left.
//
//////////////////////////////////////////////////////////////////////////
static bool KernalMapBank(uint8 uiProcess, uint8 uiBank)
{
	uint8*	puiPage;
	uint16	uiPhysical;

	puiPage = KernalRemapPage(uiProcess);
	if (puiPage[uiBank] != 0)
	{
		return true;
	}
	for (uiPhysical = KERNAL_FIRST_PROCESS_BANK; uiPhysical <= KERNAL_LAST_PROCESS_BANK; uiPhysical++)
	{
		if (gauiKernalBankOwners[uiPhysical] == 0)
		{
			gauiKernalBankOwners[uiPhysical] = uiProcess;
			puiPage[uiBank] = (uint8)uiPhysical;
			memset((uint8*)((uint32)uiPhysical << 16), 0, 0x10000);
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
//
// Takes the process's banks back and clears its page of the remap table.
//
//////////////////////////////////////////////////////////////////////////
static void KernalFreeProcess(uint8 uiProcess)
{
	uint16	uiPhysical;

	for (uiPhysical = KERNAL_FIRST_PROCESS_BANK; uiPhysical <= KERNAL_LAST_PROCESS_BANK; uiPhysical++)
	{
		if (gauiKernalBankOwners[uiPhysical] == uiProcess)
		{
			gauiKernalBankOwners[uiPhysical] = 0;
		}
	}
	memset(KernalRemapPage(uiProcess), 0, 256);
}


//////////////////////////////////////////////////////////////////////////
//
// Maps the banks uiAddress to uiAddress + uiSize - 1 are in.
//
//////////////////////////////////////////////////////////////////////////
static bool KernalMapRange(uint8 uiProcess, uint32 uiAddress, uint32 uiSize)
{
	uint32	uiBank;

	if (uiSize == 0)
	{
		return true;
	}
	for (uiBank = uiAddress >> 16; uiBank <= ((uiAddress + uiSize - 1) >> 16); uiBank++)
	{
		if (!KernalMapBank(uiProcess, (uint8)uiBank))
		{
			return false;
		}
	}
	return true;
}


static uint16 KernalLoadExecutableObject(CAsm65Object* pcObject, CFatFile* pcFile, uint8 uiProcess, bool bLoaded);


//////////////////////////////////////////////////////////////////////////
//
// Reads an executable from the volume into a new process's banks: a
// CAsm65Object whose sections are all placed (at an address in the
// process's own banks), with a UserStart symbol and optionally a
// UserMemorySize (the memory size Libc65 reads: its heap's banks from
// $200000 are mapped).  Puts the interrupt frame RTI starts it with on its
// stack.
//
//////////////////////////////////////////////////////////////////////////
static uint16 KernalLoadExecutable(const char* szPath, uint8 uiProcess)
{
	CFatFile			cFile;
	CFatAbstractFile	cFatAbstractFile;
	CAsm65Object		cObject;
	CChunkFile			cChunkFile;
	bool				bLoaded;
	uint16				uiResult;

	cFile.Init(&gcKernalVolume);
	if (cFile.Open(szPath, FAT_FILE_ACCESS_READ) != FAT_SUCCESS)
	{
		return KERNAL_PROCESS_NOT_FOUND;
	}

	//Read straight from the volume without checking its hashes (the kernal has no MD5).  The sections' bytes are
	//left in the file and read into the process's banks once they are mapped: an executable can be bigger than the
	//kernal's heap.
	cFatAbstractFile.Init(&cFile);
	cObject.Init("");
	cObject.SetSkipSectionBytes(true);
	cChunkFile.InitUnhashed(&cFatAbstractFile);
	bLoaded = cChunkFile.ReadOpen();
	bLoaded = bLoaded && cObject.ReadChunks(&cChunkFile);
	uiResult = KernalLoadExecutableObject(&cObject, &cFile, uiProcess, bLoaded);
	if (bLoaded)
	{
		cChunkFile.ReadClose();
	}
	cChunkFile.Kill();
	cObject.Kill();
	cFile.Close();
	return uiResult;
}


//////////////////////////////////////////////////////////////////////////
//
// KernalLoadExecutable once the object is read (bLoaded): maps the
// process's banks and reads the sections' bytes from the file into them.
//
//////////////////////////////////////////////////////////////////////////
static uint16 KernalLoadExecutableObject(CAsm65Object* pcObject, CFatFile* pcFile, uint8 uiProcess, bool bLoaded)
{
	SAsm65Section*		psSection;
	SAsm65ObjectSymbol*	psSymbol;
	uint8*				puiPage;
	uint8*				puiStack;
	uint8*				puiBuffer;
	uint32				uiEntry;
	uint32				uiMemorySize;
	uint32				uiAddress;
	uint32				uiLeft;
	uint32				uiChunk;
	lsize				uiRead;
	int32				iSymbol;
	size				i;

	if (!bLoaded)
	{
		return KERNAL_PROCESS_BAD_FILE;
	}
	iSymbol = pcObject->FindSymbol(KERNAL_PROCESS_ENTRY_SYMBOL);
	if (iSymbol < 0)
	{
		return KERNAL_PROCESS_NO_ENTRY;
	}
	psSymbol = pcObject->GetSymbol((size)iSymbol);
	uiEntry = (uint32)psSymbol->iValue;
	uiMemorySize = 0;
	iSymbol = pcObject->FindSymbol(KERNAL_PROCESS_MEMORY_SYMBOL);
	if (iSymbol >= 0)
	{
		uiMemorySize = (uint32)pcObject->GetSymbol((size)iSymbol)->iValue;
	}

	//The process's page in the remap table: the banks every process has, its heap, the tables at their own address,
	//then the sections.
	puiPage = KernalRemapPage(uiProcess);
	for (i = 0; i < KERNAL_NUM_TABLE_BANKS; i++)
	{
		puiPage[KERNAL_TABLES_BANK + i] = (uint8)(KERNAL_TABLES_BANK + i);
	}
	bLoaded = true;
	for (i = 0; i < sizeof(gauiKernalProcessBanks); i++)
	{
		bLoaded &= KernalMapBank(uiProcess, gauiKernalProcessBanks[i]);
	}
	if (uiMemorySize > KERNAL_USER_HIGH_MEMORY)
	{
		bLoaded &= KernalMapRange(uiProcess, KERNAL_USER_HIGH_MEMORY, uiMemorySize - KERNAL_USER_HIGH_MEMORY);
	}
	else
	{
		uiMemorySize = 0;
		for (i = 0; i < sizeof(gauiKernalLowHeapBanks); i++)
		{
			bLoaded &= KernalMapBank(uiProcess, gauiKernalLowHeapBanks[i]);
		}
	}
	for (i = 0; (i < pcObject->NumSections()) && bLoaded; i++)
	{
		psSection = pcObject->GetSection(i);
		if (!psSection->bAt)
		{
			return KERNAL_PROCESS_BAD_FILE;
		}
		bLoaded = KernalMapRange(uiProcess, psSection->uiAt, psSection->uiSize);
	}
	if (!bLoaded)
	{
		return KERNAL_PROCESS_NO_MEMORY;
	}

	//The sections' bytes, from the file to the process's banks through a buffer.
	puiBuffer = (uint8*)malloc(KERNAL_COPY_CHUNK);
	if (!puiBuffer)
	{
		return KERNAL_PROCESS_NO_MEMORY;
	}
	for (i = 0; (i < pcObject->NumSections()) && bLoaded; i++)
	{
		psSection = pcObject->GetSection(i);
		uiAddress = psSection->uiAt;
		uiLeft = psSection->uiNumBytes;
		bLoaded = (uiLeft == 0) || (pcFile->Seek((uint32)psSection->iBytesPosition, FAT_SEEK_START) == FAT_SUCCESS);
		while ((uiLeft > 0) && bLoaded)
		{
			uiChunk = (uiLeft < KERNAL_COPY_CHUNK) ? uiLeft : KERNAL_COPY_CHUNK;
			bLoaded = (pcFile->Read(puiBuffer, uiChunk, &uiRead) == FAT_SUCCESS) && (uiRead == uiChunk) && KernalCopyToUser(uiAddress, puiBuffer, uiChunk);
			uiAddress += uiChunk;
			uiLeft -= uiChunk;
		}
	}
	free(puiBuffer);
	if (!bLoaded)
	{
		return KERNAL_PROCESS_BAD_FILE;
	}

	*(uint32*)KernalUserAddress(KERNAL_USER_MEMORY_SIZE) = uiMemorySize;

	//The interrupt frame RTI pulls from the process's stack: P, the entry's PC and its bank.
	puiStack = KernalUserAddress(KERNAL_USER_STACK_TOP - 3);
	puiStack[0] = KERNAL_PROCESS_STATUS;
	puiStack[1] = (uint8)uiEntry;
	puiStack[2] = (uint8)(uiEntry >> 8);
	puiStack[3] = (uint8)(uiEntry >> 16);
	return KERNAL_PROCESS_NONE;
}


//////////////////////////////////////////////////////////////////////////
//
// Runs an executable (see KernalLoadExecutable) as a new process in user
// mode until it exits.  boot.exe is process $FF; a process run by another
// (during the other's execute call) gets the next ID down and its own
// banks, and the other carries on once it exits.  Returns
// KERNAL_PROCESS_EXITED with its exit code, or why it couldn't run.
//
//////////////////////////////////////////////////////////////////////////
uint16 KernalRunExecutable(const char* szPath, uint16 uiArgument, uint16* puiExitCode)
{
	SKernalCall		sCaller;
	uint8			uiCaller;
	uint8			uiProcess;
	uint16			uiResult;

	uiCaller = guiKernalProcess;
	uiProcess = (uiCaller == 0) ? KERNAL_FIRST_PROCESS_ID : (uint8)(uiCaller - 1);
	if ((uiProcess < KERNAL_LAST_PROCESS_ID) || (uiCaller == KERNAL_LAST_PROCESS_ID))
	{
		return KERNAL_PROCESS_NO_PROCESS;
	}

	//The process's memory is read and written through guiKernalProcess's remap page.
	guiKernalProcess = uiProcess;
	KernalFreeProcess(uiProcess);
	uiResult = KernalLoadExecutable(szPath, uiProcess);
	if (uiResult != KERNAL_PROCESS_NONE)
	{
		KernalFreeProcess(uiProcess);
		guiKernalProcess = uiCaller;
		return uiResult;
	}

	//The caller's registers are in gsKernalCall until its call returns: the new process's calls use it too.
	memcpy(&sCaller, &gsKernalCall, sizeof(SKernalCall));
	memset(&gsKernalCall, 0, sizeof(gsKernalCall));
	gsKernalCall.uiUserS = KERNAL_USER_STACK_TOP - 4;
	gauiKernalArguments[uiProcess] = uiArgument;

	KernalWriteRegister(8, uiProcess);
	KernalEnterUser();
	*puiExitCode = gsKernalCall.uiExitCode;

	KernalFreeProcess(uiProcess);
	memcpy(&gsKernalCall, &sCaller, sizeof(SKernalCall));
	guiKernalProcess = uiCaller;
	if (uiCaller != 0)
	{
		KernalWriteRegister(8, uiCaller);
	}
	return KERNAL_PROCESS_EXITED;
}


//////////////////////////////////////////////////////////////////////////
//
// The execute call: the path, the argument and where the exit code goes.
//
//////////////////////////////////////////////////////////////////////////
static uint16 KernalCallExecute(void)
{
	char		szPath[KERNAL_MAX_PATH];
	uint32		uiExitCodeAddress;
	uint16		uiArgument;
	uint16		uiExitCode;
	uint16		uiResult;
	uint8		auiExitCode[2];

	if (!KernalUserString(KernalArgumentLong(0), szPath, KERNAL_MAX_PATH))
	{
		return KERNAL_PROCESS_NOT_FOUND;
	}
	uiArgument = KernalArgumentWord(4);
	uiExitCodeAddress = KernalArgumentLong(6);

	uiExitCode = 0;
	uiResult = KernalRunExecutable(szPath, uiArgument, &uiExitCode);
	auiExitCode[0] = (uint8)uiExitCode;
	auiExitCode[1] = (uint8)(uiExitCode >> 8);
	KernalCopyToUser(uiExitCodeAddress, auiExitCode, 2);
	return uiResult;
}


//////////////////////////////////////////////////////////////////////////
//
// The file size call: the size in A (low) and X (high), $FFFFFFFF if the
// file isn't there.
//
//////////////////////////////////////////////////////////////////////////
static void KernalCallFileSize(void)
{
	char		szPath[KERNAL_MAX_PATH];
	CFatFile	cFile;
	uint32		uiSize;

	uiSize = 0xFFFFFFFF;
	if (KernalUserString(KernalArgumentLong(0), szPath, KERNAL_MAX_PATH))
	{
		cFile.Init(&gcKernalVolume);
		if (cFile.Open(szPath, FAT_FILE_ACCESS_READ) == FAT_SUCCESS)
		{
			uiSize = cFile.Size();
			cFile.Close();
		}
	}
	gsKernalCall.uiA = (uint16)uiSize;
	gsKernalCall.uiX = (uint16)(uiSize >> 16);
}


//////////////////////////////////////////////////////////////////////////
//
// The file load call: the path, the destination and the most to read.
//
//////////////////////////////////////////////////////////////////////////
static uint16 KernalCallFileLoad(void)
{
	char		szPath[KERNAL_MAX_PATH];
	CFatFile	cFile;
	uint32		uiDest;
	uint32		uiMax;
	uint32		uiSize;
	uint32		uiChunk;
	lsize		uiRead;
	uint8*		puiBuffer;
	uint16		uiResult;

	if (!KernalUserString(KernalArgumentLong(0), szPath, KERNAL_MAX_PATH))
	{
		return KERNAL_PROCESS_NOT_FOUND;
	}
	uiDest = KernalArgumentLong(4);
	uiMax = KernalArgumentLong(8);

	cFile.Init(&gcKernalVolume);
	if (cFile.Open(szPath, FAT_FILE_ACCESS_READ) != FAT_SUCCESS)
	{
		return KERNAL_PROCESS_NOT_FOUND;
	}
	puiBuffer = (uint8*)malloc(KERNAL_COPY_CHUNK);
	if (!puiBuffer)
	{
		cFile.Close();
		return KERNAL_PROCESS_NO_MEMORY;
	}

	uiSize = cFile.Size();
	uiResult = (uiSize <= uiMax) ? KERNAL_PROCESS_NONE : KERNAL_PROCESS_NO_MEMORY;
	uiSize = (uiSize < uiMax) ? uiSize : uiMax;
	while ((uiSize > 0) && (uiResult == KERNAL_PROCESS_NONE))
	{
		uiChunk = (uiSize < KERNAL_COPY_CHUNK) ? uiSize : KERNAL_COPY_CHUNK;
		if ((cFile.Read(puiBuffer, uiChunk, &uiRead) != FAT_SUCCESS) || (uiRead != uiChunk))
		{
			uiResult = KERNAL_PROCESS_BAD_FILE;
		}
		else if (!KernalCopyToUser(uiDest, puiBuffer, uiChunk))
		{
			uiResult = KERNAL_PROCESS_NO_MEMORY;
		}
		uiDest += uiChunk;
		uiSize -= uiChunk;
	}
	free(puiBuffer);
	cFile.Close();
	return uiResult;
}


//////////////////////////////////////////////////////////////////////////
//
// The audio open call: finds the sound board and takes it out of reset.
// It starts disabled.
//
//////////////////////////////////////////////////////////////////////////
static uint16 KernalCallAudioOpen(void)
{
	if (!gpsKernalSoundBoard)
	{
		gpsKernalSoundBoard = KernalFindDevice(KERNAL_SOUND_BOARD_NAME);
		if (!gpsKernalSoundBoard)
		{
			return KERNAL_PROCESS_NOT_FOUND;
		}
		KernalEnableDevice((uint16)(gpsKernalSoundBoard - gasKernalDevices));
	}
	return KERNAL_PROCESS_NONE;
}


//////////////////////////////////////////////////////////////////////////
//
// Copies uiLength samples from the running process's memory at uiAddress
// to the sound board (KernalAudioCopy), a run in one of its banks at a
// time.
//
//////////////////////////////////////////////////////////////////////////
static void KernalCopyToAudio(uint8* puiDest, uint32 uiAddress, uint16 uiLength)
{
	uint8*	puiSource;
	uint32	uiInBank;

	while (uiLength > 0)
	{
		puiSource = KernalUserPointer(uiAddress, &uiInBank);
		if (!puiSource)
		{
			return;
		}
		uiInBank = (uiInBank < uiLength) ? uiInBank : uiLength;
		KernalAudioCopy(puiDest, puiSource, (uint16)uiInBank);
		uiAddress += uiInBank;
		puiDest += uiInBank;
		uiLength = (uint16)(uiLength - uiInBank);
	}
}


//////////////////////////////////////////////////////////////////////////
//
// The audio write call: the offset, the left and right samples and how
// many.  Copied to RAM A and RAM B, wrapping at their end.
//
//////////////////////////////////////////////////////////////////////////
static void KernalCallAudioWrite(void)
{
	uint8*	puiBoard;
	uint32	uiLeft;
	uint32	uiRight;
	uint16	uiOffset;
	uint16	uiCount;
	uint16	uiPart;

	if (!gpsKernalSoundBoard)
	{
		return;
	}
	puiBoard = (uint8*)gpsKernalSoundBoard->uiAddress;
	uiOffset = (uint16)(KernalArgumentWord(0) & (KERNAL_AUDIO_RAM_SIZE - 1));
	uiLeft = KernalArgumentLong(2);
	uiRight = KernalArgumentLong(6);
	uiCount = KernalArgumentWord(10);
	while (uiCount > 0)
	{
		uiPart = (uint16)(KERNAL_AUDIO_RAM_SIZE - uiOffset);
		uiPart = (uiPart < uiCount) ? uiPart : uiCount;
		KernalCopyToAudio(&puiBoard[KERNAL_AUDIO_RAM_A + uiOffset], uiLeft, uiPart);
		KernalCopyToAudio(&puiBoard[KERNAL_AUDIO_RAM_B + uiOffset], uiRight, uiPart);
		uiLeft += uiPart;
		uiRight += uiPart;
		uiCount -= uiPart;
		uiOffset = 0;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// A COP from the process.  Its signature byte (the byte before the return
// address it pushed on its stack) is the call.
//
//////////////////////////////////////////////////////////////////////////
extern "C" void KernalCopCall(void)
{
	uint8*			puiFrame;
	uint32			uiReturn;
	uint8			uiCall;

	//The frame: P, PC low, PC high, then the program bank.
	puiFrame = KernalUserAddress((uint32)gsKernalCall.uiUserS + 1);
	uiReturn = (uint32)puiFrame[1] | ((uint32)puiFrame[2] << 8) | ((uint32)puiFrame[3] << 16);
	uiCall = *KernalUserAddress(uiReturn - 1);
	gpuiKernalArguments = KernalUserAddress(gsKernalCall.uiA);

	switch (uiCall)
	{
	case KERNAL_CALL_EXIT:
		gsKernalCall.uiExitCode = gsKernalCall.uiA;
		gsKernalCall.uiExit = 1;
		break;

	case KERNAL_CALL_PUT_VALUE:
		gsKernalStatus.uiUserValue = KernalArgumentWord(0);
		break;

	case KERNAL_CALL_EXECUTE:
		gsKernalCall.uiA = KernalCallExecute();
		break;

	case KERNAL_CALL_GET_ARGUMENT:
		gsKernalCall.uiA = gauiKernalArguments[guiKernalProcess];
		break;

	case KERNAL_CALL_FILE_SIZE:
		KernalCallFileSize();
		break;

	case KERNAL_CALL_FILE_LOAD:
		gsKernalCall.uiA = KernalCallFileLoad();
		break;

	case KERNAL_CALL_AUDIO_OPEN:
		gsKernalCall.uiA = KernalCallAudioOpen();
		break;

	case KERNAL_CALL_AUDIO_WRITE:
		KernalCallAudioWrite();
		break;

	case KERNAL_CALL_AUDIO_SET:
		if (gpsKernalSoundBoard)
		{
			KernalAudioSetRegister((uint8*)(gpsKernalSoundBoard->uiAddress + KERNAL_AUDIO_REGISTERS), KernalArgumentWord(0), KernalArgumentWord(2));
		}
		break;

	case KERNAL_CALL_AUDIO_COUNTER:
		gsKernalCall.uiA = 0;
		if (gpsKernalSoundBoard)
		{
			gsKernalCall.uiA = KernalAudioGetCounter((uint8*)(gpsKernalSoundBoard->uiAddress + KERNAL_AUDIO_REGISTERS));
		}
		break;

	default:
		gsKernalCall.uiA = 0xFFFF;
		break;
	}
}
