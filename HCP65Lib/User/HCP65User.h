#ifndef __HCP65_USER_H__
#define __HCP65_USER_H__
#include "BaseLib/PrimitiveTypes.h"


//Kernal calls for a program the HCP65 kernal runs (UserCalls.asm).  A program exits by returning from main (or by
//calling exit).  Paths are FAT paths on the SD card ("\\boot.exe").

#define KERNAL_RESULT_SUCCESS		0
#define KERNAL_RESULT_NOT_FOUND		1		//No such file (or no sound board).
#define KERNAL_RESULT_BAD_FILE		2		//Not an executable, or a file that couldn't be read.
#define KERNAL_RESULT_NO_MEMORY		3
#define KERNAL_RESULT_NO_ENTRY		4		//An executable without UserStart.
#define KERNAL_RESULT_EXITED		5		//KernalExecute: the program ran and exited.
#define KERNAL_RESULT_NO_PROCESS	6		//KernalExecute: all four process IDs are in use.

#define KERNAL_FILE_NOT_FOUND		0xFFFFFFFF


//Gives the kernal a value it keeps in its status (for tests).
extern "C" void		KernalPutValue(uint16 uiValue);

//Runs the executable szPath as a new process and waits for it to exit.  It gets uiArgument (KernalGetArgument).
//Returns KERNAL_RESULT_EXITED with its exit code in *puiExitCode, or why it couldn't run.
extern "C" uint16	KernalExecute(const char* szPath, uint16 uiArgument, uint16* puiExitCode);

//The argument the process was run with (boot.exe's is the kernal's boot argument, zero unless a test sets it).
extern "C" uint16	KernalGetArgument(void);

//The size of the file szPath, or KERNAL_FILE_NOT_FOUND.
extern "C" uint32	KernalFileSize(const char* szPath);

//Reads up to uiMaxSize bytes of the file szPath to pvDest.  KERNAL_RESULT_SUCCESS if it read the whole file.
extern "C" uint16	KernalFileLoad(const char* szPath, void* pvDest, uint32 uiMaxSize);

//Finds the sound board and takes it out of reset, disabled.  KERNAL_RESULT_SUCCESS, or KERNAL_RESULT_NOT_FOUND.
extern "C" uint16	KernalAudioOpen(void);

//Copies uiCount left and right samples (unsigned 8 bit) to the sound board's RAM A and RAM B from uiOffset (0 to
//$7FFF; the copy wraps there).
extern "C" void		KernalAudioWrite(uint16 uiOffset, const uint8* puiLeft, const uint8* puiRight, uint16 uiCount);

//Writes one of the sound board's registers (HCP65_AUDIO_FLAGS ... HCP65_AUDIO_INTERRUPT_HIGH: 0 to 7).
extern "C" void		KernalAudioSetRegister(uint16 uiRegister, uint16 uiValue);

//The sound board's counter: the address of the samples it plays next.
extern "C" uint16	KernalAudioGetCounter(void);


#endif // __HCP65_USER_H__
