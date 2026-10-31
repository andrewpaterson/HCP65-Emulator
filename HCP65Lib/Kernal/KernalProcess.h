#ifndef __KERNAL_PROCESS_H__
#define __KERNAL_PROCESS_H__
#include "BaseLib/PrimitiveTypes.h"


#define KERNAL_FIRST_PROCESS_ID		0xFF		//boot.exe's.  A process run by another gets the next ID down.
#define KERNAL_LAST_PROCESS_ID		0xFC
#define KERNAL_REMAP_TABLE			0x070000	//MPU RAM: a 256 byte page of physical banks per process ID.
#define KERNAL_USER_STACK_TOP		0xEFFF		//In the process's bank 0.
#define KERNAL_FIRST_PROCESS_BANK	0x50		//Physical main memory banks given to processes: the 4MB card's banks
#define KERNAL_LAST_PROCESS_BANK	0x7F		//after the tables ($40-$43) and the kernal's heap ($44-$4F).
#define KERNAL_TABLES_BANK			0x40
#define KERNAL_NUM_TABLE_BANKS		4
#define KERNAL_USER_MEMORY_SIZE		0xFF0300	//Where Libc65 reads the memory size (a process address).
#define KERNAL_USER_HIGH_MEMORY		0x200000	//Libc65's heap starts here when the memory size is more.

//COP n: A is the address of the call's arguments on the process's stack (UserCalls.asm and HCP65User.h).
#define KERNAL_CALL_EXIT			0			//A is the exit code.  Doesn't return.
#define KERNAL_CALL_PUT_VALUE		1
#define KERNAL_CALL_EXECUTE			2
#define KERNAL_CALL_GET_ARGUMENT	3
#define KERNAL_CALL_FILE_SIZE		4
#define KERNAL_CALL_FILE_LOAD		5
#define KERNAL_CALL_AUDIO_OPEN		6
#define KERNAL_CALL_AUDIO_WRITE		7
#define KERNAL_CALL_AUDIO_SET		8
#define KERNAL_CALL_AUDIO_COUNTER	9

//Call results (KERNAL_RESULT_* in HCP65User.h).
#define KERNAL_PROCESS_NONE				0
#define KERNAL_PROCESS_NOT_FOUND		1
#define KERNAL_PROCESS_BAD_FILE			2
#define KERNAL_PROCESS_NO_MEMORY		3
#define KERNAL_PROCESS_NO_ENTRY			4
#define KERNAL_PROCESS_EXITED			5
#define KERNAL_PROCESS_NO_PROCESS		6


//The user registers a COP saved and what the kernal call asks for; Start.asm uses its layout (all 16 bit words).
struct SKernalCall
{
	uint16	uiA;			//+0
	uint16	uiX;			//+2
	uint16	uiY;			//+4
	uint16	uiUserS;		//+6
	uint16	uiUserD;		//+8
	uint16	uiUserDBR;		//+10
	uint16	uiKernalS;		//+12  The kernal's stack while the process runs (KernalEnterUser saves it); its calls run below it.
	uint16	uiExit;			//+14  Set by the exit call: COP returns to whoever ran the process.
	uint16	uiExitCode;		//+16
};


extern "C" SKernalCall	gsKernalCall;

extern "C" void		KernalEnterUser(void);		//Start.asm: RTI into the process; returns when it exits.
extern "C" void		KernalCopCall(void);		//Called by Start.asm's COP handler.

uint16	KernalRunExecutable(const char* szPath, uint16 uiArgument, uint16* puiExitCode);


#endif // __KERNAL_PROCESS_H__
