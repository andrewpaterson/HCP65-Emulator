; Kernal calls for programs (see HCP65User.h): COP n with A the address of the call's arguments on the stack (in bank 0,
; where the caller pushed them: the first at S + 4 after the jsl).  The kernal reads them through the process's remap
; page and returns a result in A, or A (low) and X (high).  COP 0 is exit (UserStart.asm).

		.section text.KernalPutValue
		.global _KernalPutValue
_KernalPutValue:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 1
		rtl

		.section text.KernalExecute
		.global _KernalExecute
_KernalExecute:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 2
		rtl

		.section text.KernalGetArgument
		.global _KernalGetArgument
_KernalGetArgument:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 3
		rtl

		.section text.KernalFileSize
		.global _KernalFileSize
_KernalFileSize:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 4
		rtl

		.section text.KernalFileLoad
		.global _KernalFileLoad
_KernalFileLoad:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 5
		rtl

		.section text.KernalAudioOpen
		.global _KernalAudioOpen
_KernalAudioOpen:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 6
		rtl

		.section text.KernalAudioWrite
		.global _KernalAudioWrite
_KernalAudioWrite:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 7
		rtl

		.section text.KernalAudioSetRegister
		.global _KernalAudioSetRegister
_KernalAudioSetRegister:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 8
		rtl

		.section text.KernalAudioGetCounter
		.global _KernalAudioGetCounter
_KernalAudioGetCounter:
		.width (A, X), 16
		tsc
		clc
		adc 4
		cop 9
		rtl
