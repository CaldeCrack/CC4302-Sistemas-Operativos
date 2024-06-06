# int swapInt(int *psl, int val);
# x0 = psl
# x1 = val
	.text
	.align	8
	.globl	swapInt
	.globl	storeInt
swapInt:
	ldxr	w2, [x0]
	stxr	w3, w1, [x0]
	cbnz	w3, swapInt
	dmb	sy
	mov	w0, w2
	ret

storeInt:
	dmb	sy
	str	w1, [x0]
	ret
	.section        .note.GNU-stack,"",@progbits
