#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BaseLib/FileUtil.h"
#include "BaseLib/PointerFunctions.h"
#include "HCP65KernalBuilder.h"


//Libc65's units for the W65C816 (its own assembly for string and soft float).
static const char* gaszHCP65Libc65[] = { "string.cpp", "stdlib.cpp", "ctype.cpp", "time.cpp", "math.cpp", "softfloat.cpp", "new.cpp", "signal.cpp", "stdio.cpp", "host.cpp", "string816.asm", "string816_16.asm", "softfloat816.asm", NULL };

//The kernal's units, linked only when used.  Start.asm and Kernal.cpp (main) are always linked.
static const char* gaszHCP65KernalUnits[] = { "KernalDevices.cpp", "KernalSDCard.cpp", NULL };


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65KernalBuilder::Init(void)
{
	mcBuilder.Init();
	mpuiROM = NULL;
	muiCodeEnd = 0;
	mszErrors.Init();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65KernalBuilder::Kill(void)
{
	mszErrors.Kill();
	SafeFree(mpuiROM);
	mcBuilder.Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// szCommonLibraries and szKernal are directories (relative ones are made
// full).  False with GetErrors if it didn't compile, link or fit.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65KernalBuilder::Build(const char* szCommonLibraries, const char* szKernal)
{
	CFileUtil	cFileUtil;
	CChars		szCommon;
	CChars		szKernalDirectory;
	CChars		szPath;
	bool		bResult;
	size		i;

	szCommon.Init(szCommonLibraries);
	cFileUtil.FullPath(&szCommon);
	szKernalDirectory.Init(szKernal);
	cFileUtil.FullPath(&szKernalDirectory);

	mcBuilder.SetTarget(CPPT_W65C816);
	mcBuilder.SetRemoveUnused(true);
	szPath.Init(szCommon);
	cFileUtil.AppendToPath(&szPath, "Libc65");
	mcBuilder.AddIncludeDirectory(szPath.Text(), false, true);
	szPath.Kill();
	mcBuilder.AddIncludeDirectory(szCommon.Text(), true, false);
	mcBuilder.AddIncludeDirectory(szKernalDirectory.Text(), false, false);

	mcBuilder.AddUnit("Kernal", szKernalDirectory.Text(), "Start.asm");
	mcBuilder.AddUnit("Kernal", szKernalDirectory.Text(), "Kernal.cpp");
	mcBuilder.AddUnit("Kernal", szKernalDirectory.Text(), "MathTables.asm");
	for (i = 0; gaszHCP65KernalUnits[i]; i++)
	{
		mcBuilder.AddUnit("Kernal", szKernalDirectory.Text(), gaszHCP65KernalUnits[i], true);
	}

	szPath.Init(szCommon);
	cFileUtil.AppendToPath(&szPath, "BaseLib");
	cFileUtil.AppendToPath(&szPath, "BaseLib.vcxproj");
	bResult = mcBuilder.AddVisualStudioProject("BaseLib", szPath.Text(), true);
	szPath.Kill();

	szPath.Init(szCommon);
	cFileUtil.AppendToPath(&szPath, "Libc65");
	for (i = 0; gaszHCP65Libc65[i]; i++)
	{
		mcBuilder.AddUnit("Libc65", szPath.Text(), gaszHCP65Libc65[i], true);
	}
	szPath.Kill();

	//Code and data flow from bank 1 and must stay below the tables' ROM banks.
	mcBuilder.SetLimit(HCP65_ROM_CODE_END);
	bResult &= mcBuilder.Link();
	mszErrors.Append(mcBuilder.GetErrors());
	szKernalDirectory.Kill();
	szCommon.Kill();
	if (!bResult)
	{
		return false;
	}
	return MakeImage();
}


//////////////////////////////////////////////////////////////////////////
//
// The linked segments in the ROM's layout.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65KernalBuilder::MakeImage(void)
{
	CAsm65Output*	pcOutput;
	SAsm65Segment*	psSegment;
	size			i;
	uint32			uiStart;
	uint32			uiEnd;
	uint32			uiROM;

	SafeFree(mpuiROM);
	mpuiROM = (uint8*)malloc(HCP65_ROM_SIZE);
	memset(mpuiROM, 0xFF, HCP65_ROM_SIZE);
	muiCodeEnd = 0;

	pcOutput = mcBuilder.GetOutput();
	for (i = 0; i < pcOutput->NumSegments(); i++)
	{
		psSegment = pcOutput->GetSegment(i);
		uiStart = psSegment->uiAddress;
		uiEnd = uiStart + (uint32)psSegment->cBytes.NumElements();
		if ((uiStart >= HCP65_TABLES_START) && (uiEnd <= HCP65_TABLES_END))
		{
			uiROM = uiStart - HCP65_TABLES_START + HCP65_ROM_CODE_END;
		}
		else if (uiEnd <= HCP65_ROM_CODE_END)
		{
			uiROM = uiStart;
			if ((uiStart >= 0x10000) && (uiEnd > muiCodeEnd))
			{
				muiCodeEnd = uiEnd;
			}
		}
		else
		{
			mszErrors.Append("A segment at 0x");
			mszErrors.Append(uiStart, 16);
			mszErrors.Append(" is outside the ROM's layout.\n");
			return false;
		}
		memcpy(&mpuiROM[uiROM], psSegment->cBytes.GetData(), psSegment->cBytes.NumElements());
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65KernalBuilder::Write(const char* szFilename)
{
	FILE*	pcFile;
	bool	bResult;

	if (!mpuiROM)
	{
		return false;
	}
	pcFile = fopen(szFilename, "wb");
	if (!pcFile)
	{
		return false;
	}
	bResult = fwrite(mpuiROM, 1, HCP65_ROM_SIZE, pcFile) == HCP65_ROM_SIZE;
	fclose(pcFile);
	return bResult;
}


//////////////////////////////////////////////////////////////////////////
//
// A global's address, with the compiler's underscore (C++ guiState is
// _guiState).
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65KernalBuilder::GetSymbol(const char* szName, uint32* puiAddress)
{
	int64	lliValue;

	if (!mcBuilder.GetSymbol(szName, &lliValue))
	{
		return false;
	}
	*puiAddress = (uint32)lliValue;
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8* CHCP65KernalBuilder::GetROM(void)
{
	return mpuiROM;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65KernalBuilder::GetCodeEnd(void)
{
	return muiCodeEnd;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
const char* CHCP65KernalBuilder::GetErrors(void)
{
	return mszErrors.Text();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
CCPP65Builder* CHCP65KernalBuilder::GetBuilder(void)
{
	return &mcBuilder;
}
