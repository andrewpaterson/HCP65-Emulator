#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BaseLib/FileUtil.h"
#include "BaseLib/MemoryFile.h"
#include "BaseLib/MemoryDrive.h"
#include "BaseLib/FatVolume.h"
#include "BaseLib/FatFile.h"
#include "BaseLib/PointerFunctions.h"
#include "Assembler65Lib/Asm65Object.h"
#include "HCP65ExeBuilder.h"


//Libc65's units for the W65C816 (its own assembly for string and soft float).
static const char* gaszHCP65ExeLibc65[] = { "string.cpp", "stdlib.cpp", "ctype.cpp", "time.cpp", "math.cpp", "softfloat.cpp", "new.cpp", "signal.cpp", "stdio.cpp", "host.cpp", "string816.asm", "string816_16.asm", "softfloat816.asm", NULL };


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65ExeBuilder::Init(void)
{
	mcBuilder.Init();
	macExecutable.Init();
	mszErrors.Init();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65ExeBuilder::Kill(void)
{
	mszErrors.Kill();
	macExecutable.Kill();
	mcBuilder.Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// aszUnits (NULL terminated) are the program's files in
// szProgramDirectory; one of them has main.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65ExeBuilder::Build(const char* szCommonLibraries, const char* szUser, const char* szProgramDirectory, const char** aszUnits)
{
	CFileUtil	cFileUtil;
	CChars		szCommon;
	CChars		szUserDirectory;
	CChars		szProgram;
	CChars		szPath;
	bool		bResult;
	size		i;

	szCommon.Init(szCommonLibraries);
	cFileUtil.FullPath(&szCommon);
	szUserDirectory.Init(szUser);
	cFileUtil.FullPath(&szUserDirectory);
	szProgram.Init(szProgramDirectory);
	cFileUtil.FullPath(&szProgram);

	mcBuilder.SetTarget(CPPT_W65C816);
	mcBuilder.SetRemoveUnused(true);
	szPath.Init(szCommon);
	cFileUtil.AppendToPath(&szPath, "Libc65");
	mcBuilder.AddIncludeDirectory(szPath.Text(), false, true);
	szPath.Kill();
	mcBuilder.AddIncludeDirectory(szCommon.Text(), true, false);
	mcBuilder.AddIncludeDirectory(szUserDirectory.Text(), false, false);
	mcBuilder.AddIncludeDirectory(szProgram.Text(), false, false);

	mcBuilder.AddUnit("User", szUserDirectory.Text(), "UserStart.asm");
	mcBuilder.AddUnit("User", szUserDirectory.Text(), "UserCalls.asm", true);
	mcBuilder.AddUnit("User", szUserDirectory.Text(), "UserMathTables.asm", true);
	for (i = 0; aszUnits[i]; i++)
	{
		mcBuilder.AddUnit("Program", szProgram.Text(), aszUnits[i]);
	}

	szPath.Init(szCommon);
	cFileUtil.AppendToPath(&szPath, "BaseLib");
	cFileUtil.AppendToPath(&szPath, "BaseLib.vcxproj");
	bResult = mcBuilder.AddVisualStudioProject("BaseLib", szPath.Text(), true);
	szPath.Kill();

	szPath.Init(szCommon);
	cFileUtil.AppendToPath(&szPath, "Libc65");
	for (i = 0; gaszHCP65ExeLibc65[i]; i++)
	{
		mcBuilder.AddUnit("Libc65", szPath.Text(), gaszHCP65ExeLibc65[i], true);
	}
	szPath.Kill();

	mcBuilder.SetLimit(HCP65_EXE_CODE_LIMIT);
	bResult &= mcBuilder.Link();
	mszErrors.Append(mcBuilder.GetErrors());
	szProgram.Kill();
	szUserDirectory.Kill();
	szCommon.Kill();
	if (!bResult)
	{
		return false;
	}
	return MakeExecutable();
}


//////////////////////////////////////////////////////////////////////////
//
// The linked segments as placed sections of an object, and the entry.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65ExeBuilder::MakeExecutable(void)
{
	CAsm65Object		cObject;
	CAsm65Output*		pcOutput;
	SAsm65Segment*		psSegment;
	SAsm65Section*		psSection;
	SAsm65ObjectSymbol*	psSymbol;
	CMemoryFile*		pcMemoryFile;
	CChars				szName;
	int64				lliEntry;
	int64				lliBssStart;
	int64				lliBssEnd;
	uint32				uiSection;
	uint32				uiSymbol;
	size				i;
	bool				bResult;

	if (!mcBuilder.GetSymbol(HCP65_EXE_ENTRY_SYMBOL, &lliEntry))
	{
		mszErrors.Append("No " HCP65_EXE_ENTRY_SYMBOL ".\n");
		return false;
	}

	cObject.Init("Executable");
	pcOutput = mcBuilder.GetOutput();
	for (i = 0; i < pcOutput->NumSegments(); i++)
	{
		psSegment = pcOutput->GetSegment(i);
		szName.Init("segment");
		szName.Append((int)i);
		uiSection = cObject.AddSection(szName.Text(), "", ASK65_Data);
		szName.Kill();
		psSection = cObject.GetSection(uiSection);
		psSection->bAt = true;
		psSection->uiAt = psSegment->uiAddress;
		psSection->bKeep = true;
		psSection->uiSize = (uint32)psSegment->cBytes.NumElements();
		psSection->cBytes.Resize(psSegment->cBytes.NumElements());
		memcpy(psSection->cBytes.GetData(), psSegment->cBytes.GetData(), psSegment->cBytes.NumElements());
	}
	//bss has no bytes: a section of its size and no bytes, which the kernal maps (its banks are cleared).
	if (mcBuilder.GetSymbol("__bss_start", &lliBssStart) && mcBuilder.GetSymbol("__bss_end", &lliBssEnd) && (lliBssEnd > lliBssStart))
	{
		uiSection = cObject.AddSection("bss", "", ASK65_Bss);
		psSection = cObject.GetSection(uiSection);
		psSection->bAt = true;
		psSection->uiAt = (uint32)lliBssStart;
		psSection->bKeep = true;
		psSection->uiSize = (uint32)(lliBssEnd - lliBssStart);
	}

	uiSymbol = cObject.PutSymbol(HCP65_EXE_ENTRY_SYMBOL);
	psSymbol = cObject.GetSymbol(uiSymbol);
	psSymbol->iSection = -1;
	psSymbol->iValue = lliEntry;
	psSymbol->eBinding = AB65_Global;

	pcMemoryFile = MemoryFile();
	pcMemoryFile->mbBasicFileMustFree = false;
	bResult = cObject.Write(pcMemoryFile);
	if (bResult)
	{
		macExecutable.Resize((size)pcMemoryFile->GetBufferSize());
		memcpy(macExecutable.GetData(), pcMemoryFile->GetBufferPointer(), (size)pcMemoryFile->GetBufferSize());
	}
	SafeKill(pcMemoryFile);
	cObject.Kill();
	return bResult;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8* CHCP65ExeBuilder::GetExecutable(void)
{
	return macExecutable.GetData();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
size CHCP65ExeBuilder::GetExecutableSize(void)
{
	return macExecutable.NumElements();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
const char* CHCP65ExeBuilder::GetErrors(void)
{
	return mszErrors.Text();
}


//////////////////////////////////////////////////////////////////////////
//
// Writes uiSize bytes as szName (a FAT path, \boot.exe) on the FAT volume in
// the image file szImage, replacing it if it is there.
//
//////////////////////////////////////////////////////////////////////////
bool HCP65PutFileOnImage(const char* szImage, const char* szName, uint8* puiData, size uiSize)
{
	CMemoryDrive	cDrive;
	CFatVolume		cVolume;
	CFatFile		cFile;
	FILE*			pcFile;
	long			iSize;
	bool			bResult;

	pcFile = fopen(szImage, "rb");
	if (!pcFile)
	{
		return false;
	}
	fseek(pcFile, 0, SEEK_END);
	iSize = ftell(pcFile);
	fseek(pcFile, 0, SEEK_SET);
	cDrive.Init((uint64)iSize, 512);
	bResult = fread(cDrive.GetMemory(), 1, (size_t)iSize, pcFile) == (size_t)iSize;
	fclose(pcFile);

	bResult = bResult && (cVolume.Mount(&cDrive) == FAT_SUCCESS);
	if (bResult)
	{
		cFile.Init(&cVolume);
		bResult = cFile.Open(szName, FAT_FILE_ACCESS_CREATE | FAT_FILE_ACCESS_OVERWRITE | FAT_FILE_ACCESS_WRITE) == FAT_SUCCESS;
		bResult = bResult && (cFile.Write(puiData, uiSize) == FAT_SUCCESS);
		bResult &= cFile.Close() == FAT_SUCCESS;
		bResult &= cVolume.Unmount() == FAT_SUCCESS;
	}

	if (bResult)
	{
		pcFile = fopen(szImage, "wb");
		bResult = pcFile != NULL;
		if (pcFile)
		{
			bResult = fwrite(cDrive.GetMemory(), 1, (size_t)iSize, pcFile) == (size_t)iSize;
			fclose(pcFile);
		}
	}
	cDrive.Kill();
	return bResult;
}
