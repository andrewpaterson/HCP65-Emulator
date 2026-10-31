#include <string.h>
#include "HCP65Lib/HCP65.h"
#include "HCP65Lib/HCP65Audio.h"
#include "TestLib/Assert.h"


#define TEST_HCP65_CLOCK_RATE	10000000


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
static void TestHCP65Put(uint8* puiMemory, uint32 uiAddress, const uint8* puiBytes, size uiLength)
{
	memcpy(&puiMemory[uiAddress], puiBytes, uiLength);
}


//////////////////////////////////////////////////////////////////////////
//
// A program in ROM and the same in RAM at its address (as the start up's
// copy would leave it), started from $8000 by the reset vector.
//
//////////////////////////////////////////////////////////////////////////
static void TestHCP65Program(CHCP65* pcHCP65, const uint8* puiProgram, size uiLength)
{
	uint8*	puiROM;
	uint8*	puiRAM;

	puiROM = pcHCP65->GetMPUROM();
	puiRAM = pcHCP65->GetMPURAM();
	TestHCP65Put(puiROM, 0x8000, puiProgram, uiLength);
	TestHCP65Put(puiRAM, 0x8000, puiProgram, uiLength);
	puiROM[0xFFFC] = 0x00;
	puiROM[0xFFFD] = 0x80;
}


//////////////////////////////////////////////////////////////////////////
//
// The reset vector and the code before the first xce come from ROM (ROM
// is at 0); xce swaps RAM to 0.  RTI from kernel mode enters user mode (the
// interrupt depth goes to 0): the pulls come from the user's stack, through
// the process's remap table.  BRK goes back to kernel mode, pushing on the
// user's stack.
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65BootAndUserMode(void)
{
	CHCP65	cHCP65;
	uint8*	puiROM;
	uint8*	puiRAM;
	uint8*	puiMain;

	static const uint8 auiKernal[] = {
		0x18, 0xFB,					//8000 clc  xce
		0xA9, 0x22,					//8002 lda #$22 (ROM's copy has #$11)
		0x8F, 0x00, 0x00, 0x20,		//8004 sta $200000
		0xA9, 0xFE,					//8008 lda #$FE
		0x8F, 0x08, 0x00, 0x10,		//800A sta $100008 (process ID)
		0xA2, 0xF0,					//800E ldx #$F0
		0x9A,						//8010 txs
		0x40 };						//8011 rti
	static const uint8 auiUser[] = {
		0xA9, 0x33,					//lda #$33
		0x8D, 0x34, 0x12,			//sta $1234
		0x00, 0x00 };				//brk

	cHCP65.Init(TEST_HCP65_CLOCK_RATE);
	puiROM = cHCP65.GetMPUROM();
	puiRAM = cHCP65.GetMPURAM();
	puiMain = cHCP65.GetMainMemory();
	TestHCP65Program(&cHCP65, auiKernal, sizeof(auiKernal));
	puiROM[0x8003] = 0x11;
	puiRAM[0x8100] = 0xDB;									//stp
	puiRAM[0xFFE6] = 0x00;									//Native BRK vector.
	puiRAM[0xFFE7] = 0x81;
	puiRAM[HCP65_REMAP_TABLE + 0xFE00 + 0] = 0x42;			//Process $FE's bank 0 is bank $42.

	TestHCP65Put(puiMain, 0x429000, auiUser, sizeof(auiUser));
	puiMain[0x4200F1] = 0x34;	//P
	puiMain[0x4200F2] = 0x00;	//PCL
	puiMain[0x4200F3] = 0x90;	//PCH
	puiMain[0x4200F4] = 0x00;	//PBR

	cHCP65.Reset();
	AssertTrue(cHCP65.IsKernelMode());
	AssertInt(1, cHCP65.GetInterruptDepth());
	AssertFalse(cHCP65.IsNativeLatched());

	cHCP65.Run(1000);
	AssertTrue(cHCP65.IsStopped());
	AssertTrue(cHCP65.IsNativeLatched());
	AssertInt(0x22, puiMain[0x200000]);
	AssertInt(0xFE, cHCP65.GetProcessID());
	AssertInt(0x33, puiMain[0x421234]);
	AssertInt(1, cHCP65.GetInterruptDepth());
	AssertTrue(cHCP65.IsKernelMode());
	AssertInt(0x00, puiMain[0x4200F4]);
	AssertInt(0x90, puiMain[0x4200F3]);
	AssertInt(0x07, puiMain[0x4200F2]);
	AssertInt(0x8101, cHCP65.GetMPU()->GetProgramCounter());
	cHCP65.Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// Native mode, interrupts on, then waits; uiIrqHandler and uiNmiHandler
// are the native vectors.
//
//////////////////////////////////////////////////////////////////////////
static uint8* TestHCP65WaitingKernal(CHCP65* pcHCP65, uint16 uiIrqHandler, uint16 uiNmiHandler)
{
	uint8*	puiRAM;

	static const uint8 auiProgram[] = {
		0x18, 0xFB,					//8000 clc  xce
		0x58,						//8002 cli
		0xCB,						//8003 wai
		0x80, 0xFD };				//8004 bra 8003

	TestHCP65Program(pcHCP65, auiProgram, sizeof(auiProgram));
	puiRAM = pcHCP65->GetMPURAM();
	puiRAM[0xFFEE] = (uint8)uiIrqHandler;
	puiRAM[0xFFEF] = (uint8)(uiIrqHandler >> 8);
	puiRAM[0xFFEA] = (uint8)uiNmiHandler;
	puiRAM[0xFFEB] = (uint8)(uiNmiHandler >> 8);
	return puiRAM;
}


//////////////////////////////////////////////////////////////////////////
//
// The sound board in a 128KB socket: its ROM, registers and sample RAMs,
// the samples it plays, and its interrupt on the sample tick that reaches
// the interrupt address.
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65AudioBoard(void)
{
	CHCP65		cHCP65;
	CHCP65Audio	cAudio;
	uint8*		puiRAM;
	uint8		auiFrames[64];
	uint32		uiFrames;
	uint64		uiEnableCycle;
	uint64		uiDue;
	int			i;

	static const uint8 auiIrq[] = {
		0xAF, 0x00, 0x04, 0x18,		//lda $180400 (flags)
		0x8F, 0x10, 0x00, 0x20,		//sta $200010
		0xAF, 0x02, 0x04, 0x18,		//lda $180402 (counter low)
		0x8F, 0x11, 0x00, 0x20,		//sta $200011
		0xDB };						//stp

	cHCP65.Init(TEST_HCP65_CLOCK_RATE);
	cAudio.Init();
	AssertFalse(cHCP65.SetDevice(3, &cAudio));
	AssertTrue(cHCP65.SetDevice(16, &cAudio));
	puiRAM = TestHCP65WaitingKernal(&cHCP65, 0x8200, 0x8300);
	TestHCP65Put(puiRAM, 0x8200, auiIrq, sizeof(auiIrq));
	for (i = 0; i < HCP65_AUDIO_RAM_SIZE; i++)
	{
		cAudio.GetRAMA()[i] = (uint8)i;
		cAudio.GetRAMB()[i] = (uint8)(255 - i);
	}

	cHCP65.Reset();
	AssertTrue(cAudio.IsHeldInReset());
	cHCP65.Write(0x100005, 0x01);
	AssertFalse(cAudio.IsHeldInReset());
	AssertInt(0x01, cHCP65.Read(0x100005));
	AssertInt('H', cHCP65.Read(0x180000));
	cHCP65.Write(0x180402, 0x10);
	cHCP65.Write(0x180403, 0x00);
	cHCP65.Write(0x180404, 0x13);
	cHCP65.Write(0x180405, 0x00);
	cHCP65.Write(0x180406, 0x12);
	cHCP65.Write(0x180407, 0x00);
	uiEnableCycle = cHCP65.NumCycles();
	cHCP65.Write(0x180400, 0xC0);
	AssertInt(0x40, cHCP65.Read(0x180420));
	AssertInt(32768, cAudio.GetSampleRate());

	cHCP65.Run(100000);
	AssertTrue(cHCP65.IsStopped());
	AssertInt(0xC0, cHCP65.GetMainMemory()[0x200010]);
	AssertInt(0x12, cHCP65.GetMainMemory()[0x200011]);
	uiDue = cHCP65.CycleOfCrystalTick(cHCP65.CrystalTick(uiEnableCycle) + 2);
	AssertTrue(cHCP65.NumCycles() >= uiDue);
	AssertTrue(cHCP65.NumCycles() < uiDue + 60);
	AssertInt(0, cHCP65.Read(0x100002) & 0x01);

	uiFrames = cAudio.ReadFrames(auiFrames, 32);
	AssertInt(2, uiFrames);
	AssertInt(0x10, auiFrames[0]);
	AssertInt(0xEF, auiFrames[1]);
	AssertInt(0x11, auiFrames[2]);
	AssertInt(0xEE, auiFrames[3]);
	cHCP65.Write(0x180401, 0);
	AssertFalse(cAudio.IsInterrupting());
	AssertInt(1, cHCP65.Read(0x100002) & 0x01);

	//The loop 10, 11, 12, 13, 10 ... at 8192Hz.
	cHCP65.Write(0x180400, 0x42);
	cAudio.CatchUp(cHCP65.NumCycles() + cHCP65.CycleOfCrystalTick(4 * 8) - 1);
	uiFrames = cAudio.ReadFrames(auiFrames, 32);
	AssertTrue((uiFrames >= 7) && (uiFrames <= 8));
	AssertInt(0x12, auiFrames[0]);
	AssertInt(0x13, auiFrames[2]);
	AssertInt(0x10, auiFrames[4]);
	AssertInt(0x11, auiFrames[6]);

	cHCP65.Kill();
	cAudio.Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// The program itself enables the sound board and its interrupt, inside one
// long Run: the interrupt comes on the 100th sample.
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65AudioFromProgram(void)
{
	CHCP65		cHCP65;
	CHCP65Audio	cAudio;
	uint8*		puiRAM;

	static const uint8 auiProgram[] = {
		0x18, 0xFB,								//clc  xce
		0xA9, 0x02, 0x8F, 0x05, 0x00, 0x10,		//lda #2  sta $100005 (enable device 17)
		0xA9, 0x00, 0x8F, 0x02, 0x04, 0x1A,		//lda #0  sta $1A0402 (start)
		0xA9, 0x64, 0x8F, 0x06, 0x04, 0x1A,		//lda #100  sta $1A0406 (interrupt)
		0xA9, 0xC3, 0x8F, 0x00, 0x04, 0x1A,		//lda #$C3  sta $1A0400 (enable, interrupt, 4096Hz)
		0x58,									//cli
		0xCB, 0x80, 0xFD };						//wai  bra -3

	cHCP65.Init(TEST_HCP65_CLOCK_RATE);
	cAudio.Init();
	cHCP65.SetDevice(17, &cAudio);
	TestHCP65Program(&cHCP65, auiProgram, sizeof(auiProgram));
	puiRAM = cHCP65.GetMPURAM();
	puiRAM[0xFFEE] = 0x00;
	puiRAM[0xFFEF] = 0x82;
	puiRAM[0x8200] = 0xDB;
	cHCP65.Reset();
	cHCP65.Run(10000000);
	AssertTrue(cHCP65.IsStopped());
	AssertInt(100, cAudio.GetCounter());
	AssertInt(100, cAudio.NumFrames());
	AssertTrue(cHCP65.NumCycles() > cHCP65.CycleOfCrystalTick(796));
	AssertTrue(cHCP65.NumCycles() < cHCP65.CycleOfCrystalTick(804));
	cHCP65.Kill();
	cAudio.Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// The NMI timer starts when the processor first goes native and latches
// its NMI source every 2^(21 - 2 * speed) crystal ticks.
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65Timer(void)
{
	CHCP65	cHCP65;
	uint8*	puiRAM;
	uint64	uiDue;

	cHCP65.Init(TEST_HCP65_CLOCK_RATE);
	puiRAM = TestHCP65WaitingKernal(&cHCP65, 0x8200, 0x8300);
	puiRAM[0x8300] = 0xDB;
	cHCP65.Reset();
	cHCP65.Write(0x100006, 7);
	cHCP65.Run(2000000);
	AssertTrue(cHCP65.IsStopped());
	AssertInt(0x11, cHCP65.Read(0x100007) & 0x11);
	uiDue = cHCP65.CycleOfCrystalTick(128);
	AssertTrue(cHCP65.NumCycles() >= uiDue);
	AssertTrue(cHCP65.NumCycles() < uiDue + 60);
	cHCP65.Write(0x100007, 0x01);
	AssertInt(0, cHCP65.Read(0x100007) & 0x01);
	cHCP65.Kill();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void TestHCP65Machine(void)
{
	BeginTests();

	TestHCP65BootAndUserMode();
	TestHCP65AudioBoard();
	TestHCP65AudioFromProgram();
	TestHCP65Timer();

	TestStatistics();
}
