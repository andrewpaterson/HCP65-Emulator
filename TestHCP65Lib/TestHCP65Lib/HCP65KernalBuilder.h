#ifndef __HCP65_KERNAL_BUILDER_H__
#define __HCP65_KERNAL_BUILDER_H__
#include "BaseLib/Chars.h"
#include "CppParserLib/CPP65Builder.h"


#define HCP65_ROM_SIZE				0x80000		//512KB of flash.
#define HCP65_ROM_CODE_END			0x40000		//The kernal's code and data: ROM banks 0-3, copied to MPU RAM.
#define HCP65_RAM_END				0x07FC00	//MPU RAM below the remap table: the kernal's code, data and bss.
#define HCP65_TABLES_START			0x400000	//The math tables in main memory: ROM banks 4-7 copied there.
#define HCP65_TABLES_END			0x440000


//Builds the HCP65 kernal's ROM image from its source in HCP65Lib/Kernal: the start up (Start.asm), the kernal's C++
//(compiled for the W65C816) with BaseLib and Libc65, and the math tables (MathTables.asm).  The image is 512KB of
//ROM, $FF where nothing is (erased flash): banks 0-3 hold what is linked at $000000-$03FFFF (the vectors and the
//kernal), banks 4-7 what is linked at $400000-$43FFFF (the tables).  See Start.asm for how it starts.
class CHCP65KernalBuilder
{
protected:
	CCPP65Builder	mcBuilder;
	uint8*			mpuiROM;
	uint32			muiCodeEnd;		//The end of the kernal's code and data.
	CChars			mszErrors;

public:
	void			Init(void);
	void			Kill(void);

	bool			Build(const char* szCommonLibraries, const char* szKernal);
	bool			Write(const char* szFilename);

	uint8*			GetROM(void);
	uint32			GetCodeEnd(void);
	bool			GetSymbol(const char* szName, uint32* puiAddress);
	const char*		GetErrors(void);
	CCPP65Builder*	GetBuilder(void);

protected:
	bool			MakeImage(void);
};


#endif // __HCP65_KERNAL_BUILDER_H__
