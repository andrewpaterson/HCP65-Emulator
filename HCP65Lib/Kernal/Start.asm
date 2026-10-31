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
;   4. Goes on in RAM at StartInRAM: sets the stack and the direct page, clears bss, sets up the heap, calls the init
;      table's routines (each global's R_Initialise, in link order) and main, then waits for interrupts.
;
; Bank 0 holds only the stack, the direct page, the vectors and a jml stub per vector; the code is in bank 1 onwards
; and runs at the address it is linked at once it is in RAM.


KERNAL_STACK_TOP	= 0xBFFF		; The kernal's stack.
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

		; bss (globals with no initialiser) isn't in the image.  Up to 64KB is cleared a word at a time (a long address
		; plus X crosses banks; an odd size clears the byte after it too, where nothing is yet), more a byte at a time.
		lda __bss_end & 0xFFFF
		sec
		sbc __bss_start & 0xFFFF
		tay
		lda __bss_end >> 16
		sbc __bss_start >> 16
		bne StartClearLarge
		tya
		inc A
		lsr A
		beq StartCleared
		tay
		ldx 0
		lda 0
StartClearWord:
		sta [__bss_start + X]:24
		inx
		inx
		dey
		bne StartClearWord
		bra StartCleared
StartClearLarge:
		lda __bss_start & 0xFFFF
		sta [D + 0]
		lda __bss_start >> 16
		sta [D + 2]
StartClear:
		lda [D + 0]
		cmp __bss_end & 0xFFFF
		bne StartClearByte
		lda [D + 2]
		cmp __bss_end >> 16
		beq StartCleared
StartClearByte:
		sep m
		lda 0
		sta [[D + 0]:24]
		rep m
		inc [D + 0]
		bne StartClear
		inc [D + 2]
		bra StartClear
StartCleared:

		; The heap before anything can allocate.
		jsl _KernalInitHeap
		.width (A, X), 16

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


		; Interrupts are not handled yet: they return at once.  (An interrupt taken in user mode pushed its frame on
		; the process's stack before the vector pull raised the interrupt depth, and RTI drops the depth again before
		; it pulls, so the frame comes back from the same stack.)
KernalBrk:
KernalAbort:
KernalNmi:
KernalIrq:
		rti


		; A kernal call: COP n from a process.  Its frame is on the process's stack and S still points there (in MPU
		; RAM now), so the registers are saved without the stack, then the call runs on the kernal's stack below where
		; KernalEnterUser left it for this process (so a process run during another's execute call has its calls below
		; the other's).  KernalCopCall (C++) reads the signature byte through the remap table.  The exit call goes back
		; to whoever ran the process (KernalEnterUser's caller); every other call returns to the process.
KernalCop:
		rep (m, x)
		sta [_gsKernalCall + 0]:24
		txa
		sta [_gsKernalCall + 2]:24
		tya
		sta [_gsKernalCall + 4]:24
		tsc
		sta [_gsKernalCall + 6]:24
		tdc
		sta [_gsKernalCall + 8]:24
		lda [_gsKernalCall + 12]:24
		tcs
		lda 0
		sep m
		phb
		pla
		rep m
		sta [_gsKernalCall + 10]:24
		lda 0
		tcd
		phk
		plb
		jsl _KernalCopCall
		.width (A, X), 16
		lda [_gsKernalCall + 14]:24
		bne KernalCopExit

		sep m
		lda [_gsKernalCall + 10]:24
		pha
		plb
		rep m
		lda [_gsKernalCall + 8]:24
		tcd
		lda [_gsKernalCall + 6]:24
		tcs
		lda [_gsKernalCall + 4]:24
		tay
		lda [_gsKernalCall + 2]:24
		tax
		lda [_gsKernalCall + 0]:24
		rti

		; The process has exited: back on the kernal's stack, return from KernalEnterUser.  The COP's vector pull
		; left the interrupt depth where KernalEnterUser's RTI found it.
KernalCopExit:
		.width (A, X), 16
		lda [_gsKernalCall + 12]:24
		tcs
		pld
		plb
		rtl


		; extern "C" void KernalEnterUser(void): RTI into the process.  KernalRunExecutable put the frame (P, PC and
		; the program bank) at the top of the process's stack and its S in gsKernalCall.uiUserS.  The RTI drops the
		; interrupt depth to 0 (user mode) before it pulls, so it pulls from the process's stack.
		.global _KernalEnterUser
_KernalEnterUser:
		.width (A, X), 16
		phb
		phd
		tsc
		sta [_gsKernalCall + 12]:24
		lda [_gsKernalCall + 6]:24
		tcs
		rti
