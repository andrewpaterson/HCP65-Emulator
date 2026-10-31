; The HCP65 kernal's start up.
;
; At reset MPU ROM is at $000000-$07FFFF and MPU RAM at $080000-$0FFFFF.  The first xce into native mode swaps
; them for good (the Native Latch): RAM at $000000, ROM at $080000.  Before the swap bank 0 is ROM, so there is no
; stack and no direct page, and emulation mode's 8 bit index registers can't copy 512KB.  So the start up:
;
;   1. In emulation mode, copies the 4 byte jml at StartSwapped into RAM where StartSwapped will be once RAM is at 0.
;   2. Goes native.  The next instruction is fetched from RAM: that jml, which goes on in ROM at StartFromROM + $80000.
;   3. Copies ROM banks 0-3 (the kernal's code and data) to RAM with mvn, and ROM banks 4-7 (the math tables, see
;      MathTables.asm) to main memory at $400000, then clears the top 1KB of RAM bank 7: the user mode remap table
;      (process IDs $FC-$FF).  The rest of RAM banks 4-7 is free.
;      (On the real machine the main memory channel must map the 4MB memory card to 4MB-8MB before the tables are
;      copied; not done yet.)
;   4. Goes on in RAM at StartInRAM: sets the stack and the direct page, calls the init table's routines (each
;      global's R_Initialise, in link order) and main, then waits for interrupts.
;
; Bank 0 holds only the stack, the direct page, the vectors and a jml stub per vector; the code is in bank 1 onwards
; and runs at the address it is linked at once it is in RAM.


KERNAL_STACK_TOP	= 0xEFFF
KERNAL_ROM_OFFSET	= 0x80000		; Where an address's ROM byte is once ROM and RAM have swapped.
KERNAL_REMAP_TABLE	= 0x07FC00		; Process $FC's page of the remap table.


		.section rodata.NativeVectors, at 0xFFE4, keep
		.word CopStub				; $FFE4 COP
		.word BrkStub				; $FFE6 BRK
		.word AbortStub				; $FFE8 ABORT
		.word NmiStub				; $FFEA NMI
		.word 0						; $FFEC
		.word IrqStub				; $FFEE IRQ

		.section rodata.EmulationVectors, at 0xFFF4, keep
		.word CopStub				; $FFF4 COP
		.word 0						; $FFF6
		.word AbortStub				; $FFF8 ABORT
		.word NmiStub				; $FFFA NMI
		.word ResetStub				; $FFFC RESET
		.word IrqStub				; $FFFE IRQ and BRK

		.section text.Stubs, at 0xFF00, keep
ResetStub:
		jml Start
CopStub:
		jml KernalCop
BrkStub:
		jml KernalBrk
AbortStub:
		jml KernalAbort
NmiStub:
		jml KernalNmi
IrqStub:
		jml KernalIrq


		.section text, keep
Start:
		.width (A, X), 8
		sei
		cld
		lda [StartSwapped]:24
		sta [StartSwapped + KERNAL_ROM_OFFSET]:24
		lda [StartSwapped + 1]:24
		sta [StartSwapped + KERNAL_ROM_OFFSET + 1]:24
		lda [StartSwapped + 2]:24
		sta [StartSwapped + KERNAL_ROM_OFFSET + 2]:24
		lda [StartSwapped + 3]:24
		sta [StartSwapped + KERNAL_ROM_OFFSET + 3]:24
		clc
		xce
StartSwapped:
		jml StartFromROM + KERNAL_ROM_OFFSET

		; Running from ROM at $08xxxx.  Each mvn copies 64KB and leaves C = $FFFF and X = Y = 0 for the next: the
		; kernal to MPU RAM banks 0-3, then the tables to main memory banks $40-$43.
StartFromROM:
		rep (m, x)
		lda 0xFFFF
		ldx 0
		ldy 0
		mvn 0x08, 0x00
		mvn 0x09, 0x01
		mvn 0x0A, 0x02
		mvn 0x0B, 0x03
		mvn 0x0C, 0x40
		mvn 0x0D, 0x41
		mvn 0x0E, 0x42
		mvn 0x0F, 0x43

		; Clear the remap table: a zero word, then copy it up through the table.
		.width (A, X), 16
		lda 0
		sta [KERNAL_REMAP_TABLE]:24
		lda 0x400 - 3
		ldx KERNAL_REMAP_TABLE & 0xFFFF
		ldy (KERNAL_REMAP_TABLE & 0xFFFF) + 2
		mvn 0x07, 0x07
		jml StartInRAM

StartInRAM:
		.width (A, X), 16
		lda KERNAL_STACK_TOP
		tcs
		lda 0
		tcd
		phk
		plb

		; Libc65's heap reads the memory size from $FF0300 (the test machines' register).  On the HCP65 that's plain
		; main memory, so say 16MB: the heap is then main memory from $200000 to $FEFFFF, which the SD Card Board's
		; reads can reach.
		lda 0
		sta [0xFF0300]:24
		lda 0x0100
		sta [0xFF0302]:24

		lda __init_start & 0xFFFF
		sta [D + 0]
		lda __init_start >> 16
		sta [D + 2]
StartInitialise:
		lda [D + 0]
		cmp __init_end & 0xFFFF
		bne StartInitialiseCall
		lda [D + 2]
		cmp __init_end >> 16
		beq StartMain
StartInitialiseCall:
		jsr StartCallEntry
		clc
		lda [D + 0]
		adc 4
		sta [D + 0]
		lda [D + 2]
		adc 0
		sta [D + 2]
		bra StartInitialise
StartMain:
		jsl _main
		.width (A, X), 16
		.global KernalIdle
KernalIdle:
		wai
		bra KernalIdle

		; Calls the routine the init table entry [D + 0] points at: the return address and the routine - 1 are pushed,
		; and rtl goes to it.
StartCallEntry:
		.width (A, X), 16
		ldy 0
		lda [[D + 0]:24 + Y]
		sta [D + 4]
		ldy 2
		lda [[D + 0]:24 + Y]
		sta [D + 6]
		phk
		per StartCallReturn - 1
		sep m
		lda [D + 6]
		pha
		rep m
		lda [D + 4]
		dec A
		pha
		rtl
StartCallReturn:
		rts

		; C's _Exit(int): the kernal has nowhere to exit to, so it idles.
		.global __Exit
__Exit:
		.width (A, X), 16
		jml KernalIdle


		; Interrupts are not handled yet: they return at once.
KernalCop:
KernalBrk:
KernalAbort:
KernalNmi:
KernalIrq:
		rti
