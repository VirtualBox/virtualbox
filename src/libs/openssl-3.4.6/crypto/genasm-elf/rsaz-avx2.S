.text	

.globl	rsaz_avx2_eligible
.type	rsaz_avx2_eligible,@function
rsaz_avx2_eligible:
	xorl	%eax,%eax
	.byte	0xf3,0xc3
.size	rsaz_avx2_eligible,.-rsaz_avx2_eligible

.globl	rsaz_1024_sqr_avx2
.globl	rsaz_1024_mul_avx2
.globl	rsaz_1024_norm2red_avx2
.globl	rsaz_1024_red2norm_avx2
.globl	rsaz_1024_scatter5_avx2
.globl	rsaz_1024_gather5_avx2
.type	rsaz_1024_sqr_avx2,@function
rsaz_1024_sqr_avx2:
rsaz_1024_mul_avx2:
rsaz_1024_norm2red_avx2:
rsaz_1024_red2norm_avx2:
rsaz_1024_scatter5_avx2:
rsaz_1024_gather5_avx2:
.byte	0x0f,0x0b
	.byte	0xf3,0xc3
.size	rsaz_1024_sqr_avx2,.-rsaz_1024_sqr_avx2
	.section ".note.gnu.property", "a"
	.p2align 3
	.long 1f - 0f
	.long 4f - 1f
	.long 5
0:
	# "GNU" encoded with .byte, since .asciz isn't supported
	# on Solaris.
	.byte 0x47
	.byte 0x4e
	.byte 0x55
	.byte 0
1:
	.p2align 3
	.long 0xc0000002
	.long 3f - 2f
2:
	.long 3
3:
	.p2align 3
4:
