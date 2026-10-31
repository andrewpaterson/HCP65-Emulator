; Kernal calls for programs (see HCP65User.h): COP n, with the call's argument in A.

		.section text.KernalPutValue
		.global _KernalPutValue
_KernalPutValue:
		.width (A, X), 16
		lda [S + 4]
		cop 1
		rtl
