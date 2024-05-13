	.arch armv8-a
	.file   "nStack-aarch64.c"
	.text
	.align  2
	.globl  _CallInNewStack
	.type	_CallInNewStack, %function
	.globl  _ChangeToStack
	.type	_ChangeToStack, %function

# void* CallInNewStack(int **psp, int *newsp, void (*proc)(), void *ptr)
# x0 = psp
# x1 = newsp
# x2 = proc
# x3 = ptr
_CallInNewStack:
	add		x4, sp, #0	/* can't STP from sp */
	stp		x19, x20,	[sp, #-192]!
	stp		x21, x22,	[sp, #16]
	stp		x23, x24,	[sp, #32]
	stp		x25, x26,	[sp, #48]
	stp		x27, x28,	[sp, #64]
	stp		x29, lr,	[sp, #80]
	stp		fp, x4,		[sp, #96]
	stp		d8, d9,		[sp, #112]
	stp		d10, d11,	[sp, #128]
	stp		d12, d13,	[sp, #144]
	stp		d14, d15,	[sp, #160]
	add		x4, sp, #0
        str		x4, [x0]
	mov		sp, x1
	mov		fp, x1
	mov		x0, x3
	blr		x2
	ret

# void *ChangeToStack(int **psp, int **pspnew)
# X0 : psp
# X1 : pspnew
# rdx : pret
# no sirve guardarlos...

_ChangeToStack:
	add		x4, sp, #0	/* can't STP from sp */
	stp		x19, x20,	[sp, #-192]!
	stp		x21, x22,	[sp, #16]
	stp		x23, x24,	[sp, #32]
	stp		x25, x26,	[sp, #48]
	stp		x27, x28,	[sp, #64]
	stp		x29, lr,	[sp, #80]
	stp		fp, x4,		[sp, #96]
	stp		d8, d9,		[sp, #112]
	stp		d10, d11,	[sp, #128]
	stp		d12, d13,	[sp, #144]
	stp		d14, d15,	[sp, #160]
	add		x4, sp, #0
        str		x4, [x0]
	ldr		x4, [x1]
	add		sp, x4, #0
	ldp		x21, x22,	[sp, #16]
	ldp		x23, x24,	[sp, #32]
	ldp		x25, x26,	[sp, #48]
	ldp		x27, x28,	[sp, #64]
	ldp		x29, lr,	[sp, #80]
	ldp		fp, x2,		[sp, #96]
	ldp		d8, d9,		[sp, #112]
	ldp		d10, d11,	[sp, #128]
	ldp		d12, d13,	[sp, #144]
	ldp		d14, d15,	[sp, #160]
	ldp		x19, x20,	[sp], #192
	ret

