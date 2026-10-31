#ifndef __HCP65_EXE_BUILDER_H__
#define __HCP65_EXE_BUILDER_H__
#include "BaseLib/Chars.h"
#include "BaseLib/ArrayUint8.h"
#include "CppParserLib/CPP65Builder.h"


#define HCP65_EXE_ENTRY_SYMBOL		"UserStart"
#define HCP65_EXE_CODE_LIMIT		0x080000		//Code and data are in banks 1-7; Libc65's heap is banks 8-15.


//Builds a program for the HCP65 kernal to run: the user start up (HCP65Lib/User/UserStart.asm), the program's units
//(compiled for the W65C816) with BaseLib and Libc65, and the kernal calls.  The executable is a CAsm65Object (as
//CAsm65Object::Write writes it) with one section placed at each linked segment's address in the process's own banks,
//no relocations, and an absolute global UserStart, the entry point.
class CHCP65ExeBuilder
{
protected:
	CCPP65Builder	mcBuilder;
	CArrayUint8		macExecutable;
	CChars			mszErrors;

public:
	void			Init(void);
	void			Kill(void);

	bool			Build(const char* szCommonLibraries, const char* szUser, const char* szProgramDirectory, const char** aszUnits);

	uint8*			GetExecutable(void);
	size			GetExecutableSize(void);
	const char*		GetErrors(void);

protected:
	bool			MakeExecutable(void);
};


bool HCP65PutFileOnImage(const char* szImage, const char* szName, uint8* puiData, size uiSize);


#endif // __HCP65_EXE_BUILDER_H__
