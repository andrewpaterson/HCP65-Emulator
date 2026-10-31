#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BaseLib/FileUtil.h"
#include "BaseLib/ArrayUint8.h"
#include "BaseLib/GlobalDataTypesIO.h"
#include "StandardLib/Objects.h"
#include "SupportLib/MODReader.h"
#include "SupportLib/ModPlayer.h"
#include "HCP65Lib/HCP65.h"
#include "HCP65Lib/HCP65Audio.h"
#include "HCP65Lib/HCP65SDCard.h"
#include "TestLib/Assert.h"
#include "HCP65KernalBuilder.h"
#include "HCP65ExeBuilder.h"
#include "Input/ModPlay/ModPlaySamples.h"


//Relative to the test's directory (TestHCP65Lib/TestHCP65Lib), as Visual Studio runs it.
#define TEST_HCP65_COMMON			".." _FS_ ".." _FS_ ".." _FS_ "commonlibraries"
#define TEST_HCP65_KERNAL			".." _FS_ ".." _FS_ "HCP65Lib" _FS_ "Kernal"
#define TEST_HCP65_INPUT			".." _FS_ ".." _FS_ "HCP65Emulator" _FS_ "Input"
#define TEST_HCP65_SD_CARD_IMAGE	TEST_HCP65_INPUT _FS_ "SDCard.img"
#define TEST_HCP65_KERNAL_ROM		TEST_HCP65_INPUT _FS_ "Kernal.rom"
#define TEST_HCP65_USER				".." _FS_ ".." _FS_ "HCP65Lib" _FS_ "User"
#define TEST_HCP65_BOOT_PROGRAM		"Input" _FS_ "Boot"
#define TEST_HCP65_MODPLAY_PROGRAM	"Input" _FS_ "ModPlay"
#define TEST_HCP65_SONG				TEST_HCP65_INPUT _FS_ "SpaceDebris.mod"

//The test's machine is fast enough that modplay.exe never runs out of samples, so what it plays is exactly what the host
//renders.
#define TEST_HCP65_CLOCK_RATE		4000000000ULL
#define TEST_HCP65_RUN_CYCLES		20000000
#define TEST_HCP65_MAX_RUNS			2000
#define TEST_MODPLAY_SECONDS		1			//boot.exe's argument, patched into the test's copy of the ROM.
#define TEST_HCP65_SD_SOCKET		2			//Socket 1 is the main memory channel.
#define TEST_HCP65_AUDIO_SOCKET		16

//The kernal's states (Kernal/Kernal.h, which is W65C816 code).
#define TEST_KERNAL_STATE_DEVICES_SCANNED	2
#define TEST_KERNAL_STATE_MOUNTED			6
#define TEST_KERNAL_STATE_BOOT_FAILED		7
#define TEST_KERNAL_STATE_BOOT_EXITED		8
#define TEST_KERNAL_PROCESS_EXITED			5
#define TEST_KERNAL_FAT32					3


//The kernal's SKernalStatus as it is laid out in MPU RAM (16 bit words, 32 bit longs, little endian).
struct STestKernalStatus
{
	uint16	uiState;
	uint16	uiSDError;
	uint16	uiFatCode;
	uint16	uiFileSystem;
	uint32	uiNumSectors;
	uint32	uiNumClusters;
	uint32	uiFreeClusters;
	uint16	uiBootResult;
	uint16	uiExitCode;
	uint16	uiUserValue;
	uint16	uiBootArgument;
};


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
static void TestKernalReadStatus(uint8* puiRAM, uint32 uiAddress, STestKernalStatus* psStatus)
{
	uint8*	p;

	p = &puiRAM[uiAddress];
	psStatus->uiState = (uint16)(p[0] | (p[1] << 8));
	psStatus->uiSDError = (uint16)(p[2] | (p[3] << 8));
	psStatus->uiFatCode = (uint16)(p[4] | (p[5] << 8));
	psStatus->uiFileSystem = (uint16)(p[6] | (p[7] << 8));
	psStatus->uiNumSectors = (uint32)p[8] | ((uint32)p[9] << 8) | ((uint32)p[10] << 16) | ((uint32)p[11] << 24);
	psStatus->uiNumClusters = (uint32)p[12] | ((uint32)p[13] << 8) | ((uint32)p[14] << 16) | ((uint32)p[15] << 24);
	psStatus->uiFreeClusters = (uint32)p[16] | ((uint32)p[17] << 8) | ((uint32)p[18] << 16) | ((uint32)p[19] << 24);
	psStatus->uiBootResult = (uint16)(p[20] | (p[21] << 8));
	psStatus->uiExitCode = (uint16)(p[22] | (p[23] << 8));
	psStatus->uiUserValue = (uint16)(p[24] | (p[25] << 8));
	psStatus->uiBootArgument = (uint16)(p[26] | (p[27] << 8));
}


//////////////////////////////////////////////////////////////////////////
//
// Builds an executable from Input/szDirectory and puts it on the SD card
// image as szName.
//
//////////////////////////////////////////////////////////////////////////
static bool TestKernalPutExe(const char* szName, const char* szDirectory, const char** aszUnits, const char** aszLibraries, uint32 uiHeapSize)
{
	CHCP65ExeBuilder	cExe;
	bool				bBuilt;

	cExe.Init();
	cExe.SetHeapSize(uiHeapSize);
	bBuilt = cExe.Build(TEST_HCP65_COMMON, TEST_HCP65_USER, szDirectory, aszUnits, aszLibraries);
	AssertString("", cExe.GetErrors());
	AssertTrue(bBuilt);
	if (bBuilt)
	{
		AssertTrue(cExe.GetExecutableSize() > 0);
		if (getenv("TEST_HCP65_VERBOSE"))
		{
			printf("%s: %d bytes\n", szName, (int)cExe.GetExecutableSize());
		}
		bBuilt = HCP65PutFileOnImage(TEST_HCP65_SD_CARD_IMAGE, szName, cExe.GetExecutable(), cExe.GetExecutableSize());
		AssertTrue(bBuilt);
	}
	cExe.Kill();
	return bBuilt;
}


//////////////////////////////////////////////////////////////////////////
//
// Puts boot.exe (Input/Boot), modplay.exe (Input/ModPlay) and the song it
// plays on the SD card image.
//
//////////////////////////////////////////////////////////////////////////
static bool TestKernalPutFiles(void)
{
	static const char*	aszBootUnits[] = { "Boot.cpp", NULL };
	static const char*	aszModPlayUnits[] = { "ModPlay.cpp", NULL };
	static const char*	aszModPlayLibraries[] = { "StandardLib", "SupportLib", NULL };
	FILE*				pcFile;
	CArrayUint8			acSong;
	long				iSize;
	bool				bResult;

	if (!TestKernalPutExe("\\boot.exe", TEST_HCP65_BOOT_PROGRAM, aszBootUnits, NULL, HCP65_EXE_DEFAULT_HEAP))
	{
		return false;
	}
	if (!TestKernalPutExe("\\modplay.exe", TEST_HCP65_MODPLAY_PROGRAM, aszModPlayUnits, aszModPlayLibraries, MODPLAY_HEAP_SIZE))
	{
		return false;
	}

	pcFile = fopen(TEST_HCP65_SONG, "rb");
	AssertNotNull(pcFile);
	if (!pcFile)
	{
		return false;
	}
	fseek(pcFile, 0, SEEK_END);
	iSize = ftell(pcFile);
	fseek(pcFile, 0, SEEK_SET);
	acSong.Init();
	acSong.Resize((size)iSize);
	bResult = fread(acSong.GetData(), 1, (size_t)iSize, pcFile) == (size_t)iSize;
	fclose(pcFile);
	bResult = bResult && HCP65PutFileOnImage(TEST_HCP65_SD_CARD_IMAGE, MODPLAY_FILE, acSong.GetData(), acSong.NumElements());
	AssertTrue(bResult);
	acSong.Kill();
	return bResult;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
static void TestKernalAppend(CArrayUint8* pacDest, uint8* puiSource, uint32 uiLength)
{
	size	uiOld;

	uiOld = pacDest->NumElements();
	pacDest->Resize(uiOld + (size)uiLength);
	memcpy(pacDest->GetData() + uiOld, puiSource, uiLength);
}


//////////////////////////////////////////////////////////////////////////
//
// What modplay.exe should play in uiFrames frames: CModPlayer on the host
// with its settings, a block at a time, as left/right pairs.
//
//////////////////////////////////////////////////////////////////////////
static void TestKernalRenderSong(lsize uiFrames, CArrayUint8* pacFrames)
{
	CModPlayer		cPlayer;
	SModUnsigned8	sBlock;
	uint8			auiSamples[MODPLAY_BLOCK_FRAMES * 2];
	lsize			uiDone;
	lsize			uiBlock;
	lsize			i;

	DataIOInit();
	ObjectsInit();
	Ptr<CModSong> pcSong = LoadMOD((char*)TEST_HCP65_SONG);
	AssertTrue(pcSong.IsNotNull());
	if (pcSong.IsNotNull())
	{
		cPlayer.Init(pcSong, MODPLAY_SAMPLE_RATE, MI_Nearest, 1.0);
		for (uiDone = 0; uiDone < uiFrames; uiDone += uiBlock)
		{
			uiBlock = ((uiFrames - uiDone) < MODPLAY_BLOCK_FRAMES) ? (uiFrames - uiDone) : MODPLAY_BLOCK_FRAMES;
			uiBlock = cPlayer.Render8(&sBlock, uiBlock);
			if (uiBlock == 0)
			{
				break;
			}
			for (i = 0; i < uiBlock; i++)
			{
				auiSamples[i * 2] = sBlock.auiLeft[i];
				auiSamples[i * 2 + 1] = sBlock.auiRight[i];
			}
			TestKernalAppend(pacFrames, auiSamples, (uint32)(uiBlock * 2));
		}
		cPlayer.Kill();
	}
	pcSong = NULL;
	ObjectsKill(false);
	DataIOKill();
}


//////////////////////////////////////////////////////////////////////////
//
// Builds the kernal ROM, boots it with the sound board and the SD Card
// Board (holding Input/SDCard.img with the test's boot.exe, modplay.exe and
// Space Debris on it, not written back) and checks that it found both,
// copied the math tables to main memory, mounted the FAT32 volume and ran
// boot.exe in user mode, which ran modplay.exe as a second process: it
// played the first TEST_MODPLAY_SECONDS of the song through the sound
// board as CModPlayer does on the host.  The ROM image is written to
// Input/Kernal.rom for the emulator (where boot.exe's argument is zero:
// modplay.exe plays the whole song).
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65KernalBoot(void)
{
	CHCP65KernalBuilder	cBuilder;
	CHCP65				cHCP65;
	CHCP65Audio			cAudio;
	CHCP65SDCard		cSDCard;
	STestKernalStatus	sStatus;
	CArrayUint8			acPlayed;
	CArrayUint8			acExpected;
	uint8				auiFrames[0x1000 * 2];
	uint32				uiStatus;
	uint32				uiArgument;
	uint32				uiSeconds;
	uint64				uiRunCycles;
	uint32				uiFrames;
	uint32				uiFirstDifference;
	uint32				uiExpectedFrames;
	uint8*				puiRAM;
	uint8*				puiMain;
	uint8*				puiROM;
	uint8*				puiPlayed;
	int					i;
	bool				bBuilt;
	bool				bSilence;

	if (!TestKernalPutFiles())
	{
		return;
	}

	cBuilder.Init();
	bBuilt = cBuilder.Build(TEST_HCP65_COMMON, TEST_HCP65_KERNAL);
	AssertString("", cBuilder.GetErrors());
	AssertTrue(bBuilt);
	if (!bBuilt)
	{
		cBuilder.Kill();
		return;
	}
	AssertTrue(cBuilder.GetCodeEnd() > 0x10000);
	AssertTrue(cBuilder.GetCodeEnd() <= HCP65_ROM_CODE_END);
	AssertTrue(cBuilder.GetSymbol("_gsKernalStatus", &uiStatus));
	AssertTrue(cBuilder.Write(TEST_HCP65_KERNAL_ROM));

	//guiKernalBootArgument is image data: in the ROM at its MPU RAM address (the kernal's status is set by the start
	//up).  boot.exe gets the boot argument.
	puiROM = cBuilder.GetROM();
	AssertTrue(cBuilder.GetSymbol("_guiKernalBootArgument", &uiArgument));
	AssertInt(0xFFFF, (int32)(puiROM[uiArgument] | (puiROM[uiArgument + 1] << 8)));
	//TEST_MODPLAY_SECONDS, TEST_HCP65_RUN_CYCLES and TEST_HCP65_CLOCK (environment) override them, to time modplay.exe
	//(at the board's 25000000 it must keep the sound board fed, or what it plays won't match).
	uiSeconds = getenv("TEST_MODPLAY_SECONDS") ? (uint32)atoi(getenv("TEST_MODPLAY_SECONDS")) : TEST_MODPLAY_SECONDS;
	uiRunCycles = getenv("TEST_HCP65_RUN_CYCLES") ? (uint64)atoll(getenv("TEST_HCP65_RUN_CYCLES")) : TEST_HCP65_RUN_CYCLES;
	puiROM[uiArgument] = (uint8)uiSeconds;
	puiROM[uiArgument + 1] = (uint8)(uiSeconds >> 8);

	if (getenv("TEST_HCP65_VERBOSE"))
	{
		printf("status at $%06X, boot argument at $%06X\n", uiStatus, uiArgument);
	}
	cHCP65.Init(getenv("TEST_HCP65_CLOCK") ? (uint64)atoll(getenv("TEST_HCP65_CLOCK")) : TEST_HCP65_CLOCK_RATE);
	cAudio.Init();
	cSDCard.Init();
	AssertTrue(cSDCard.InsertCard(TEST_HCP65_SD_CARD_IMAGE, false));
	AssertTrue(cHCP65.SetDevice(TEST_HCP65_AUDIO_SOCKET, &cAudio));
	AssertTrue(cHCP65.SetDevice(TEST_HCP65_SD_SOCKET, &cSDCard));
	AssertTrue(cHCP65.LoadROM(puiROM, HCP65_ROM_SIZE));
	cHCP65.Reset();

	//The sound board's frames are taken as they are played: it keeps no more than 64K of them.
	acPlayed.Init();
	puiRAM = cHCP65.GetMPURAM();
	for (i = 0; i < (int)(TEST_HCP65_MAX_RUNS * (TEST_HCP65_RUN_CYCLES / uiRunCycles)); i++)
	{
		cHCP65.Run(uiRunCycles);
		do
		{
			uiFrames = cAudio.ReadFrames(auiFrames, 0x1000);
			TestKernalAppend(&acPlayed, auiFrames, uiFrames * 2);
		}
		while (uiFrames > 0);
		TestKernalReadStatus(puiRAM, uiStatus, &sStatus);
		if (getenv("TEST_HCP65_VERBOSE"))
		{
			printf("run %d: state %d (boot %d, exit %d, argument %d), process $%02X, depth %d, kernel %d, PC $%06X, sound %s, counter $%04X, frames %d\n", i, sStatus.uiState, sStatus.uiBootResult, sStatus.uiExitCode, sStatus.uiBootArgument, cHCP65.GetProcessID(), cHCP65.GetInterruptDepth(), cHCP65.IsKernelMode(), cHCP65.GetMPU()->GetProgramAddress(), cAudio.IsEnabled() ? "on" : "off", cAudio.GetCounter(), (int)(acPlayed.NumElements() / 2));
			fflush(stdout);
		}
		if ((sStatus.uiState >= TEST_KERNAL_STATE_BOOT_FAILED) || ((sStatus.uiState > TEST_KERNAL_STATE_DEVICES_SCANNED) && (sStatus.uiState < TEST_KERNAL_STATE_MOUNTED)) || cHCP65.IsStopped())
		{
			break;
		}
	}

	AssertFalse(cHCP65.IsStopped());
	AssertTrue(cHCP65.IsNativeLatched());
	AssertTrue(cHCP65.IsKernelMode());
	AssertInt(TEST_KERNAL_STATE_BOOT_EXITED, sStatus.uiState);
	AssertInt(0, sStatus.uiSDError);
	AssertInt(0, sStatus.uiFatCode);
	AssertInt(TEST_KERNAL_FAT32, sStatus.uiFileSystem);
	AssertInt(cSDCard.NumSectors(), sStatus.uiNumSectors);
	AssertTrue(sStatus.uiNumClusters >= 65525);
	AssertTrue(sStatus.uiFreeClusters <= sStatus.uiNumClusters);
	AssertTrue(cSDCard.NumSectorsRead() > 0);
	AssertInt(0, cSDCard.NumSectorsWritten());
	AssertInt(uiSeconds, (uint32)sStatus.uiBootArgument);

	//boot.exe ran in user mode (process $FF), multiplied and divided with the tables, gave the kernal 300 * 200 / 3
	//and ran modplay.exe (process $FE), whose exit code it exited with; the kernal is back in kernel mode.
	AssertInt(TEST_KERNAL_PROCESS_EXITED, sStatus.uiBootResult);
	AssertInt(MODPLAY_EXIT_SUCCESS, sStatus.uiExitCode);
	AssertInt(20000, sStatus.uiUserValue);
	AssertInt(0xFF, cHCP65.GetProcessID());
	AssertInt(1, cHCP65.GetInterruptDepth());

	//The SD Card Board was enabled at boot, the sound board by modplay.exe, which left it disabled.
	AssertInt((1 << TEST_HCP65_SD_SOCKET) | (1 << TEST_HCP65_AUDIO_SOCKET), cHCP65.GetDeviceEnables());
	AssertFalse(cAudio.IsEnabled());

	//The math tables were copied from ROM banks 4-7 to main memory at $400000, and the kernal from ROM banks 0-3 to
	//MPU RAM.  The processes' pages of the remap table were cleared when they exited.
	puiMain = cHCP65.GetMainMemory();
	AssertTrue(memcmp(&puiROM[HCP65_ROM_CODE_END], &puiMain[HCP65_TABLES_START], HCP65_TABLES_END - HCP65_TABLES_START) == 0);
	AssertInt(15, puiMain[HCP65_TABLES_START + ((3 << 8) | 5) * 2]);
	AssertTrue(memcmp(&puiROM[0x10000], &puiRAM[0x10000], 0x100) == 0);
	for (i = 0; i < 0x400; i++)
	{
		if (puiRAM[HCP65_REMAP_TABLE + 0xFC00 + i] != 0)
		{
			break;
		}
	}
	AssertInt(0x400, i);

	//What the sound board played: the song's first seconds as the host plays them, then silence until it was stopped.
	uiExpectedFrames = uiSeconds * MODPLAY_SAMPLE_RATE;
	acExpected.Init();
	TestKernalRenderSong(uiExpectedFrames, &acExpected);
	AssertInt(uiExpectedFrames * 2, (uint32)acExpected.NumElements());
	AssertTrue(acPlayed.NumElements() / 2 >= uiExpectedFrames);
	AssertTrue(acPlayed.NumElements() / 2 <= uiExpectedFrames + MODPLAY_BLOCK_FRAMES);
	if (acPlayed.NumElements() >= acExpected.NumElements())
	{
		puiPlayed = acPlayed.GetData();
		for (uiFirstDifference = 0; uiFirstDifference < acExpected.NumElements(); uiFirstDifference++)
		{
			if (puiPlayed[uiFirstDifference] != acExpected.GetData()[uiFirstDifference])
			{
				break;
			}
		}
		AssertInt((uint32)acExpected.NumElements(), uiFirstDifference);
		bSilence = true;
		for (i = (int)acExpected.NumElements(); i < (int)acPlayed.NumElements(); i++)
		{
			bSilence &= puiPlayed[i] == 0x80;
		}
		AssertTrue(bSilence);
	}
	AssertTrue(cAudio.NumDroppedFrames() == 0);
	acExpected.Kill();
	acPlayed.Kill();

	cHCP65.Kill();
	cSDCard.Kill();
	cAudio.Kill();
	cBuilder.Kill();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65Kernal(void)
{
	BeginTests();

	TestHCP65KernalBoot();

	TestStatistics();
}
