; The kernal's sound board driver (HCP65Lib/HCP65Audio.h has the board): its calls (KernalProcess.cpp) find the board
; and the process's memory; these talk to the board.  C functions with the W65C816 compiler's calling convention:
; arguments from S + 4 after the jsl (pointers 4 bytes, 16 bit values 2), m and x 16 bit, D kept.

AUDIO_COUNTER_LOW	= 2			; Reading it snapshots the counter.
AUDIO_COUNTER_HIGH	= 3


		.section text.KernalAudio


; extern "C" void KernalAudioCopy(uint8* puiDest, uint8* puiSource, uint16 uiCount): samples into the board's RAM A or
; RAM B, a word at a time (the board and main memory are different banks: mvn would need its banks in the
; instruction).
		.global _KernalAudioCopy
_KernalAudioCopy:
		.width (A, X), 16
		.relax
		phd
		tsc
		tcd
		ldy 0
		lda [D + 14]
		lsr A
		tax
		beq AudioCopyByte
AudioCopyWord:
		lda [[D + 10]:24 + Y]
		sta [[D + 6]:24 + Y]
		iny
		iny
		dex
		bne AudioCopyWord
AudioCopyByte:
		lda [D + 14]
		and 1
		beq AudioCopyDone
		sep m
		lda [[D + 10]:24 + Y]
		sta [[D + 6]:24 + Y]
		rep m
AudioCopyDone:
		pld
		rtl


; extern "C" void KernalAudioSetRegister(uint8* puiRegisters, uint16 uiRegister, uint16 uiValue)
		.global _KernalAudioSetRegister
_KernalAudioSetRegister:
		.width (A, X), 16
		phd
		tsc
		tcd
		lda [D + 10]
		and 7
		tay
		lda [D + 12]
		sep m
		sta [[D + 6]:24 + Y]
		rep m
		pld
		rtl


; extern "C" uint16 KernalAudioGetCounter(uint8* puiRegisters): the address of the samples the board plays next.  The
; low byte is read first: it snapshots the counter, so the high byte read after goes with it.
		.global _KernalAudioGetCounter
_KernalAudioGetCounter:
		.width (A, X), 16
		phd
		tsc
		tcd
		sep m
		ldy AUDIO_COUNTER_LOW
		lda [[D + 6]:24 + Y]
		pha
		ldy AUDIO_COUNTER_HIGH
		lda [[D + 6]:24 + Y]
		xba
		pla
		rep m
		pld
		rtl
