.text	

.globl	aesni_gcm_encrypt
.type	aesni_gcm_encrypt,@function
aesni_gcm_encrypt:
.cfi_startproc	
	xorl	%eax,%eax
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_gcm_encrypt,.-aesni_gcm_encrypt

.globl	aesni_gcm_decrypt
.type	aesni_gcm_decrypt,@function
aesni_gcm_decrypt:
.cfi_startproc	
	xorl	%eax,%eax
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_gcm_decrypt,.-aesni_gcm_decrypt
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
