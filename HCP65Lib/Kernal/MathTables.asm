; Garth Wilson's multiply table and the 128KB slice of his reciprocal table (see commonlibraries/Libc65/MathTbls,
; MathMult.asm and MathInvert.asm for their layouts and how the 816 runtime uses them), for the kernal.
;
; They live in main memory at $400000-$43FFFF, the first four banks of the 4MB memory card that is always plugged in
; (mapped at 4MB to 8MB): there user processes can reach them too, not just the kernal.  The ROM image keeps them in
; ROM banks 4-7 and the start up copies them there.  Each table is two 64KB sections, one per bank, because a
; section can't cross a bank.  They are kept even if nothing uses them.

; The .inchex paths are relative to this file (HCP65Lib/Kernal).

		.section rodata.MathMultLow, at 0x400000, keep
		.global MathMult
MathMult:
		.inchex "../../../commonlibraries/Libc65/MathTbls/MULT.HEX", 0xE0000, 0x10000

		.section rodata.MathMultHigh, at 0x410000, keep
		.inchex "../../../commonlibraries/Libc65/MathTbls/MULT.HEX", 0xF0000, 0x10000

		.section rodata.MathInvertLow, at 0x420000, keep
		.global MathInvert
MathInvert:
		.inchex "../../../commonlibraries/Libc65/MathTbls/INVERT.HEX", 0x60000, 0x10000

		.section rodata.MathInvertHigh, at 0x430000, keep
		.inchex "../../../commonlibraries/Libc65/MathTbls/INVERT.HEX", 0x70000, 0x10000
