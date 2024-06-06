# int swapInt(int *psl, int val);
# x0 = psl
# x1 = val
	.text
	.align	8
	.globl	swapInt
swapInt:
	ldaxr	w2, [x0]
	stlxr	w3, w1, [x0]
	cbnz	w3, swapInt
	mov	w0, w2
	ret
	.section        .note.GNU-stack,"",@progbits
