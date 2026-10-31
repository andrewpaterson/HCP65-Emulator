; The kernal copies the math tables to main memory at $400000 and maps them into every process at the same address
; (see HCP65Lib/Kernal/MathTables.asm), so a program's multiplies and divides reach them there and its executable
; doesn't carry them.

MathMult = 0x400000
MathInvert = 0x420000
		.global MathMult
		.global MathInvert
