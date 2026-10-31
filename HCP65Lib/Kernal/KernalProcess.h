#ifndef __KERNAL_PROCESS_H__
#define __KERNAL_PROCESS_H__
#include "BaseLib/PrimitiveTypes.h"


#define KERNAL_PROCESS_ID			0xFF		//The one process there is for now (IDs $FC-$FF are allowed).
#define KERNAL_REMAP_TABLE			0x070000	//MPU RAM: a 256 byte page of physical banks per process ID.
#define KERNAL_USER_STACK_TOP		0xEFFF		//In the process's bank 0.
#define KERNAL_FIRST_PROCESS_BANK	0x50		//Physical main memory banks given to processes: the 4MB card's banks
#define KERNAL_LAST_PROCESS_BANK	0x7F		//after the tables ($40-$43) and the kernal's heap ($44-$4F).
#define KERNAL_TABLES_BANK			0x40
#define KERNAL_NUM_TABLE_BANKS		4

#define KERNAL_CALL_EXIT			0			//COP 0: A is the exit code.  Doesn't return.
#define KERNAL_CALL_PUT_VALUE		1			//COP 1: A is a value for the kernal's status (for tests).

#define KERNAL_PROCESS_NONE				0
#define KERNAL_PROCESS_NOT_FOUND		1
#define KERNAL_PROCESS_BAD_FILE			2
#define KERNAL_PROCESS_NO_MEMORY		3
#define KERNAL_PROCESS_NO_ENTRY			4
#define KERNAL_PROCESS_EXITED			5


//The user registers a COP saved and what the kernal call asks for; Start.asm uses its layout (all 16 bit words).
struct SKernalCall
{
	uint16	uiA;			//+0
	uint16	uiX;			//+2
	uint16	uiY;			//+4
	uint16	uiUserS;		//+6
	uint16	uiUserD;		//+8
	uint16	uiUserDBR;		//+10
	uint16	uiKernalS;		//+12  The kernal's stack while the process runs (KernalEnterUser saves it).
	uint16	uiExit;			//+14  Set by the exit call: COP returns to whoever ran the process.
	uint16	uiExitCode;		//+16
};


extern "C" SKernalCall	gsKernalCall;

extern "C" void		KernalEnterUser(void);		//Start.asm: RTI into the process; returns when it exits.
extern "C" void		KernalCopCall(void);		//Called by Start.asm's COP handler.

uint16	KernalRunExecutable(const char* szPath, uint16* puiExitCode);


#endif // __KERNAL_PROCESS_H__
