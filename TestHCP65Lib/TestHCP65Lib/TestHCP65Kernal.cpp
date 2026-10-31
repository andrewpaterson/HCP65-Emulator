#include <string.h>
#include "BaseLib/FileUtil.h"
#include "HCP65Lib/HCP65.h"
#include "HCP65Lib/HCP65Audio.h"
#include "HCP65Lib/HCP65SDCard.h"
#include "TestLib/Assert.h"
#include "HCP65KernalBuilder.h"


//Relative to the test's directory (TestHCP65Lib/TestHCP65Lib), as Visual Studio runs it.
#define TEST_HCP65_COMMON			".." _FS_ ".." _FS_ ".." _FS_ "commonlibraries"
#define TEST_HCP65_KERNAL			".." _FS_ ".." _FS_ "HCP65Lib" _FS_ "Kernal"
#define TEST_HCP65_INPUT			".." _FS_ ".." _FS_ "HCP65Emulator" _FS_ "Input"
#define TEST_HCP65_SD_CARD_IMAGE	TEST_HCP65_INPUT _FS_ "SDCard.img"
#define TEST_HCP65_KERNAL_ROM		TEST_HCP65_INPUT _FS_ "Kernal.rom"

#define TEST_HCP65_CLOCK_RATE		10000000
#define TEST_HCP65_SD_SOCKET		2			//Socket 1 is the main memory channel.
#define TEST_HCP65_AUDIO_SOCKET		16

//The kernal's states (Kernal/Kernal.h, which is W65C816 code).
#define TEST_KERNAL_STATE_DEVICES_SCANNED	2
#define TEST_KERNAL_STATE_MOUNTED			6
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
}


//////////////////////////////////////////////////////////////////////////
//
// Builds the kernal ROM, boots it with the sound board and the SD Card
// Board (holding Input/SDCard.img, not written back) and checks that it
// found both, copied the math tables to main memory and mounted the FAT32
// volume.  The ROM image is written to Input/Kernal.rom for the emulator.
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65KernalBoot(void)
{
	CHCP65KernalBuilder	cBuilder;
	CHCP65				cHCP65;
	CHCP65Audio			cAudio;
	CHCP65SDCard		cSDCard;
	STestKernalStatus	sStatus;
	uint32				uiStatus;
	uint8*				puiRAM;
	uint8*				puiMain;
	uint8*				puiROM;
	int					i;
	bool				bBuilt;

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
	puiROM = cBuilder.GetROM();

	cHCP65.Init(TEST_HCP65_CLOCK_RATE);
	cAudio.Init();
	cSDCard.Init();
	AssertTrue(cSDCard.InsertCard(TEST_HCP65_SD_CARD_IMAGE, false));
	AssertTrue(cHCP65.SetDevice(TEST_HCP65_AUDIO_SOCKET, &cAudio));
	AssertTrue(cHCP65.SetDevice(TEST_HCP65_SD_SOCKET, &cSDCard));
	AssertTrue(cHCP65.LoadROM(puiROM, HCP65_ROM_SIZE));
	cHCP65.Reset();

	puiRAM = cHCP65.GetMPURAM();
	for (i = 0; i < 20; i++)
	{
		cHCP65.Run(5000000);
		TestKernalReadStatus(puiRAM, uiStatus, &sStatus);
		if ((sStatus.uiState > TEST_KERNAL_STATE_DEVICES_SCANNED) || cHCP65.IsStopped())
		{
			break;
		}
	}

	AssertFalse(cHCP65.IsStopped());
	AssertTrue(cHCP65.IsNativeLatched());
	AssertTrue(cHCP65.IsKernelMode());
	AssertInt(TEST_KERNAL_STATE_MOUNTED, sStatus.uiState);
	AssertInt(0, sStatus.uiSDError);
	AssertInt(0, sStatus.uiFatCode);
	AssertInt(TEST_KERNAL_FAT32, sStatus.uiFileSystem);
	AssertInt(cSDCard.NumSectors(), sStatus.uiNumSectors);
	AssertTrue(sStatus.uiNumClusters >= 65525);
	AssertTrue(sStatus.uiFreeClusters <= sStatus.uiNumClusters);
	AssertTrue(cSDCard.NumSectorsRead() > 0);
	AssertInt(0, cSDCard.NumSectorsWritten());

	//The SD Card Board was enabled and the sound board left in reset.
	AssertInt(1 << TEST_HCP65_SD_SOCKET, cHCP65.GetDeviceEnables());

	//The math tables were copied from ROM banks 4-7 to main memory at $400000, and the kernal from ROM banks 0-3 to
	//MPU RAM; the remap table is clear.
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
