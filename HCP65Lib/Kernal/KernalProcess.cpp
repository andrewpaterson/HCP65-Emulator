#include <stdlib.h>
#include <string.h>
#include "BaseLib/MemoryFile.h"
#include "BaseLib/ChunkFile.h"
#include "BaseLib/FatFile.h"
#include "Assembler65Lib/Asm65Object.h"
#include "KernalDevices.h"
#include "Kernal.h"
#include "KernalProcess.h"


#define KERNAL_PROCESS_ENTRY_SYMBOL		"UserStart"
#define KERNAL_PROCESS_STATUS			0x34		//The process starts with 8 bit registers and interrupts off.

//Banks every process gets: 0 (its stack and direct page), Libc65's heap ($08-$0F) and $FF (where Libc65 reads its
//memory size, zero here so the heap is $080000-$0FFFFF).
static const uint8 gauiKernalProcessBanks[] = { 0x00, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF };


extern CFatVolume	gcKernalVolume;

SKernalCall			gsKernalCall;
static uint8		guiKernalNextBank;


//////////////////////////////////////////////////////////////////////////
//
// The process's page of the remap table: entry n is the physical bank its
// bank n is.
//
//////////////////////////////////////////////////////////////////////////
static uint8* KernalRemapPage(void)
{
	return (uint8*)(KERNAL_REMAP_TABLE + ((uint32)KERNAL_PROCESS_ID << 8));
}


//////////////////////////////////////////////////////////////////////////
//
// The physical main memory address of a process address.
//
//////////////////////////////////////////////////////////////////////////
static uint8* KernalUserAddress(uint32 uiAddress)
{
	uint8	uiBank;

	uiBank = KernalRemapPage()[(uiAddress >> 16) & 0xFF];
	return (uint8*)(((uint32)uiBank << 16) | (uiAddress & 0xFFFF));
}


//////////////////////////////////////////////////////////////////////////
//
// Gives the process's bank uiBank a cleared physical bank, if it hasn't
// one.  False if there are none left.
//
//////////////////////////////////////////////////////////////////////////
static bool KernalMapBank(uint8 uiBank)
{
	uint8*	puiPage;
	uint8*	puiBank;

	puiPage = KernalRemapPage();
	if (puiPage[uiBank] != 0)
	{
		return true;
	}
	if (guiKernalNextBank > KERNAL_LAST_PROCESS_BANK)
	{
		return false;
	}
	puiPage[uiBank] = guiKernalNextBank;
	puiBank = (uint8*)((uint32)guiKernalNextBank << 16);
	memset(puiBank, 0, 0x10000);
	guiKernalNextBank++;
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
// Copies uiLength bytes to the process's memory at uiAddress, mapping the
// banks they go in.
//
//////////////////////////////////////////////////////////////////////////
static bool KernalLoadBytes(uint32 uiAddress, uint8* puiBytes, uint32 uiLength, uint32 uiSize)
{
	uint32	uiBank;
	uint32	i;

	if (uiSize == 0)
	{
		return true;
	}
	for (uiBank = uiAddress >> 16; uiBank <= ((uiAddress + uiSize - 1) >> 16); uiBank++)
	{
		if (!KernalMapBank((uint8)uiBank))
		{
			return false;
		}
	}
	for (i = 0; i < uiLength; i++)
	{
		*KernalUserAddress(uiAddress + i) = puiBytes[i];
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
// Reads an executable from the volume: a CAsm65Object whose sections are
// all placed (at an address in the process's own banks) and that has a
// UserStart symbol.  Maps and fills the process's banks, then runs it in
// user mode until it exits.  Returns KERNAL_PROCESS_EXITED with its exit
// code, or why it couldn't run.
//
//////////////////////////////////////////////////////////////////////////
uint16 KernalRunExecutable(const char* szPath, uint16* puiExitCode)
{
	CFatFile			cFile;
	CAsm65Object		cObject;
	CChunkFile			cChunkFile;
	SAsm65Section*		psSection;
	SAsm65ObjectSymbol*	psSymbol;
	uint8*				puiFile;
	uint8*				puiPage;
	uint8*				puiStack;
	lsize				uiRead;
	uint32				uiSize;
	uint32				uiEntry;
	int32				iSymbol;
	size				i;
	bool				bLoaded;

	cFile.Init(&gcKernalVolume);
	if (cFile.Open(szPath, FAT_FILE_ACCESS_READ) != FAT_SUCCESS)
	{
		return KERNAL_PROCESS_NOT_FOUND;
	}
	uiSize = cFile.Size();
	puiFile = (uint8*)malloc(uiSize);
	if (!puiFile)
	{
		cFile.Close();
		return KERNAL_PROCESS_NO_MEMORY;
	}
	bLoaded = (cFile.Read(puiFile, uiSize, &uiRead) == FAT_SUCCESS) && (uiRead == uiSize);
	gsKernalStatus.uiExitCode = (uint16)uiSize;
	cFile.Close();

	//Read without checking its hashes, so the kernal doesn't need MD5.
	cObject.Init("");
	if (bLoaded)
	{
		cChunkFile.InitUnhashed(MemoryFile(puiFile, uiSize));
		bLoaded = cChunkFile.ReadOpen();
		gsKernalStatus.uiUserValue = bLoaded ? 0x100 : 0x101;
		bLoaded = bLoaded && cObject.ReadChunks(&cChunkFile);
		gsKernalStatus.uiUserValue |= bLoaded ? 0x200 : 0x400;
		bLoaded &= cChunkFile.ReadClose();
		cChunkFile.Kill();
	}
	free(puiFile);
	if (!bLoaded)
	{
		cObject.Kill();
		return KERNAL_PROCESS_BAD_FILE;
	}

	iSymbol = cObject.FindSymbol(KERNAL_PROCESS_ENTRY_SYMBOL);
	if (iSymbol < 0)
	{
		cObject.Kill();
		return KERNAL_PROCESS_NO_ENTRY;
	}
	psSymbol = cObject.GetSymbol((size)iSymbol);
	uiEntry = (uint32)psSymbol->iValue;

	//A fresh page in the remap table: the banks every process has, the tables at their own address, then the sections.
	guiKernalNextBank = KERNAL_FIRST_PROCESS_BANK;
	puiPage = KernalRemapPage();
	memset(puiPage, 0, 256);
	for (i = 0; i < KERNAL_NUM_TABLE_BANKS; i++)
	{
		puiPage[KERNAL_TABLES_BANK + i] = (uint8)(KERNAL_TABLES_BANK + i);
	}
	for (i = 0; i < sizeof(gauiKernalProcessBanks); i++)
	{
		if (!KernalMapBank(gauiKernalProcessBanks[i]))
		{
			cObject.Kill();
			return KERNAL_PROCESS_NO_MEMORY;
		}
	}
	for (i = 0; i < cObject.NumSections(); i++)
	{
		psSection = cObject.GetSection(i);
		if (!psSection->bAt)
		{
			cObject.Kill();
			return KERNAL_PROCESS_BAD_FILE;
		}
		if (!KernalLoadBytes(psSection->uiAt, psSection->cBytes.GetData(), (uint32)psSection->cBytes.NumElements(), psSection->uiSize))
		{
			cObject.Kill();
			return KERNAL_PROCESS_NO_MEMORY;
		}
	}
	cObject.Kill();

	//The interrupt frame RTI pulls from the process's stack: P, the entry's PC and its bank.
	puiStack = KernalUserAddress(KERNAL_USER_STACK_TOP - 3);
	puiStack[0] = KERNAL_PROCESS_STATUS;
	puiStack[1] = (uint8)uiEntry;
	puiStack[2] = (uint8)(uiEntry >> 8);
	puiStack[3] = (uint8)(uiEntry >> 16);
	memset(&gsKernalCall, 0, sizeof(gsKernalCall));
	gsKernalCall.uiUserS = KERNAL_USER_STACK_TOP - 4;

	KernalWriteRegister(8, KERNAL_PROCESS_ID);
	KernalEnterUser();

	*puiExitCode = gsKernalCall.uiExitCode;
	return KERNAL_PROCESS_EXITED;
}


//////////////////////////////////////////////////////////////////////////
//
// A COP from the process.  Its signature byte (the byte before the return
// address it pushed on its stack) is the call.
//
//////////////////////////////////////////////////////////////////////////
extern "C" void KernalCopCall(void)
{
	uint8*	puiFrame;
	uint32	uiReturn;
	uint8	uiCall;

	//The frame: P, PC low, PC high, then the program bank.
	puiFrame = KernalUserAddress((uint32)gsKernalCall.uiUserS + 1);
	uiReturn = (uint32)puiFrame[1] | ((uint32)puiFrame[2] << 8) | ((uint32)puiFrame[3] << 16);
	uiCall = *KernalUserAddress(uiReturn - 1);

	switch (uiCall)
	{
	case KERNAL_CALL_EXIT:
		gsKernalCall.uiExitCode = gsKernalCall.uiA;
		gsKernalCall.uiExit = 1;
		break;

	case KERNAL_CALL_PUT_VALUE:
		gsKernalStatus.uiUserValue = gsKernalCall.uiA;
		break;

	default:
		gsKernalCall.uiA = 0xFFFF;
		break;
	}
}
