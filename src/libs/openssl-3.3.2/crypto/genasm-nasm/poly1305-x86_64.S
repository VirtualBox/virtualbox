default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD
section	.text code align=64


EXTERN	OPENSSL_ia32cap_P

global	poly1305_init

global	poly1305_blocks

global	poly1305_emit



ALIGN	32
poly1305_init:
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
	mov	QWORD[16+rsp],rsi
	mov	rax,rsp
$L$SEH_begin_poly1305_init:
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



	xor	rax,rax
	mov	QWORD[rdi],rax
	mov	QWORD[8+rdi],rax
	mov	QWORD[16+rdi],rax

	cmp	rsi,0
	je	NEAR $L$no_key

	lea	r10,[poly1305_blocks]
	lea	r11,[poly1305_emit]
	mov	rax,0x0ffffffc0fffffff
	mov	rcx,0x0ffffffc0ffffffc
	and	rax,QWORD[rsi]
	and	rcx,QWORD[8+rsi]
	mov	QWORD[24+rdi],rax
	mov	QWORD[32+rdi],rcx
	mov	QWORD[rdx],r10
	mov	QWORD[8+rdx],r11
	mov	eax,1
$L$no_key:
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret

$L$SEH_end_poly1305_init:


ALIGN	32
poly1305_blocks:
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
	mov	QWORD[16+rsp],rsi
	mov	rax,rsp
$L$SEH_begin_poly1305_blocks:
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8
	mov	rcx,r9



$L$blocks:
	shr	rdx,4
	jz	NEAR $L$no_data

	push	rbx

	push	rbp

	push	r12

	push	r13

	push	r14

	push	r15

$L$blocks_body:

	mov	r15,rdx

	mov	r11,QWORD[24+rdi]
	mov	r13,QWORD[32+rdi]

	mov	r14,QWORD[rdi]
	mov	rbx,QWORD[8+rdi]
	mov	rbp,QWORD[16+rdi]

	mov	r12,r13
	shr	r13,2
	mov	rax,r12
	add	r13,r12
	jmp	NEAR $L$oop

ALIGN	32
$L$oop:
	add	r14,QWORD[rsi]
	adc	rbx,QWORD[8+rsi]
	lea	rsi,[16+rsi]
	adc	rbp,rcx
	mul	r14
	mov	r9,rax
	mov	rax,r11
	mov	r10,rdx

	mul	r14
	mov	r14,rax
	mov	rax,r11
	mov	r8,rdx

	mul	rbx
	add	r9,rax
	mov	rax,r13
	adc	r10,rdx

	mul	rbx
	mov	rbx,rbp
	add	r14,rax
	adc	r8,rdx

	imul	rbx,r13
	add	r9,rbx
	mov	rbx,r8
	adc	r10,0

	imul	rbp,r11
	add	rbx,r9
	mov	rax,-4
	adc	r10,rbp

	and	rax,r10
	mov	rbp,r10
	shr	r10,2
	and	rbp,3
	add	rax,r10
	add	r14,rax
	adc	rbx,0
	adc	rbp,0
	mov	rax,r12
	dec	r15
	jnz	NEAR $L$oop

	mov	QWORD[rdi],r14
	mov	QWORD[8+rdi],rbx
	mov	QWORD[16+rdi],rbp

	mov	r15,QWORD[rsp]

	mov	r14,QWORD[8+rsp]

	mov	r13,QWORD[16+rsp]

	mov	r12,QWORD[24+rsp]

	mov	rbp,QWORD[32+rsp]

	mov	rbx,QWORD[40+rsp]

	lea	rsp,[48+rsp]

$L$no_data:
$L$blocks_epilogue:
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret

$L$SEH_end_poly1305_blocks:


ALIGN	32
poly1305_emit:
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
	mov	QWORD[16+rsp],rsi
	mov	rax,rsp
$L$SEH_begin_poly1305_emit:
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



$L$emit:
	mov	r8,QWORD[rdi]
	mov	r9,QWORD[8+rdi]
	mov	r10,QWORD[16+rdi]

	mov	rax,r8
	add	r8,5
	mov	rcx,r9
	adc	r9,0
	adc	r10,0
	shr	r10,2
	cmovnz	rax,r8
	cmovnz	rcx,r9

	add	rax,QWORD[rdx]
	adc	rcx,QWORD[8+rdx]
	mov	QWORD[rsi],rax
	mov	QWORD[8+rsi],rcx

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret

$L$SEH_end_poly1305_emit:
DB	80,111,108,121,49,51,48,53,32,102,111,114,32,120,56,54
DB	95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32
DB	98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115
DB	108,46,111,114,103,62,0
ALIGN	16
global	xor128_encrypt_n_pad

ALIGN	16
xor128_encrypt_n_pad:

	sub	rdx,r8
	sub	rcx,r8
	mov	r10,r9
	shr	r9,4
	jz	NEAR $L$tail_enc
	nop
$L$oop_enc_xmm:
	movdqu	xmm0,XMMWORD[r8*1+rdx]
	pxor	xmm0,XMMWORD[r8]
	movdqu	XMMWORD[r8*1+rcx],xmm0
	movdqa	XMMWORD[r8],xmm0
	lea	r8,[16+r8]
	dec	r9
	jnz	NEAR $L$oop_enc_xmm

	and	r10,15
	jz	NEAR $L$done_enc

$L$tail_enc:
	mov	r9,16
	sub	r9,r10
	xor	eax,eax
$L$oop_enc_byte:
	mov	al,BYTE[r8*1+rdx]
	xor	al,BYTE[r8]
	mov	BYTE[r8*1+rcx],al
	mov	BYTE[r8],al
	lea	r8,[1+r8]
	dec	r10
	jnz	NEAR $L$oop_enc_byte

	xor	eax,eax
$L$oop_enc_pad:
	mov	BYTE[r8],al
	lea	r8,[1+r8]
	dec	r9
	jnz	NEAR $L$oop_enc_pad

$L$done_enc:
	mov	rax,r8
	DB	0F3h,0C3h		;repret



global	xor128_decrypt_n_pad

ALIGN	16
xor128_decrypt_n_pad:

	sub	rdx,r8
	sub	rcx,r8
	mov	r10,r9
	shr	r9,4
	jz	NEAR $L$tail_dec
	nop
$L$oop_dec_xmm:
	movdqu	xmm0,XMMWORD[r8*1+rdx]
	movdqa	xmm1,XMMWORD[r8]
	pxor	xmm1,xmm0
	movdqu	XMMWORD[r8*1+rcx],xmm1
	movdqa	XMMWORD[r8],xmm0
	lea	r8,[16+r8]
	dec	r9
	jnz	NEAR $L$oop_dec_xmm

	pxor	xmm1,xmm1
	and	r10,15
	jz	NEAR $L$done_dec

$L$tail_dec:
	mov	r9,16
	sub	r9,r10
	xor	eax,eax
	xor	r11,r11
$L$oop_dec_byte:
	mov	r11b,BYTE[r8*1+rdx]
	mov	al,BYTE[r8]
	xor	al,r11b
	mov	BYTE[r8*1+rcx],al
	mov	BYTE[r8],r11b
	lea	r8,[1+r8]
	dec	r10
	jnz	NEAR $L$oop_dec_byte

	xor	eax,eax
$L$oop_dec_pad:
	mov	BYTE[r8],al
	lea	r8,[1+r8]
	dec	r9
	jnz	NEAR $L$oop_dec_pad

$L$done_dec:
	mov	rax,r8
	DB	0F3h,0C3h		;repret


EXTERN	__imp_RtlVirtualUnwind

ALIGN	16
se_handler:
	push	rsi
	push	rdi
	push	rbx
	push	rbp
	push	r12
	push	r13
	push	r14
	push	r15
	pushfq
	sub	rsp,64

	mov	rax,QWORD[120+r8]
	mov	rbx,QWORD[248+r8]

	mov	rsi,QWORD[8+r9]
	mov	r11,QWORD[56+r9]

	mov	r10d,DWORD[r11]
	lea	r10,[r10*1+rsi]
	cmp	rbx,r10
	jb	NEAR $L$common_seh_tail

	mov	rax,QWORD[152+r8]

	mov	r10d,DWORD[4+r11]
	lea	r10,[r10*1+rsi]
	cmp	rbx,r10
	jae	NEAR $L$common_seh_tail

	lea	rax,[48+rax]

	mov	rbx,QWORD[((-8))+rax]
	mov	rbp,QWORD[((-16))+rax]
	mov	r12,QWORD[((-24))+rax]
	mov	r13,QWORD[((-32))+rax]
	mov	r14,QWORD[((-40))+rax]
	mov	r15,QWORD[((-48))+rax]
	mov	QWORD[144+r8],rbx
	mov	QWORD[160+r8],rbp
	mov	QWORD[216+r8],r12
	mov	QWORD[224+r8],r13
	mov	QWORD[232+r8],r14
	mov	QWORD[240+r8],r15

	jmp	NEAR $L$common_seh_tail



ALIGN	16
avx_handler:
	push	rsi
	push	rdi
	push	rbx
	push	rbp
	push	r12
	push	r13
	push	r14
	push	r15
	pushfq
	sub	rsp,64

	mov	rax,QWORD[120+r8]
	mov	rbx,QWORD[248+r8]

	mov	rsi,QWORD[8+r9]
	mov	r11,QWORD[56+r9]

	mov	r10d,DWORD[r11]
	lea	r10,[r10*1+rsi]
	cmp	rbx,r10
	jb	NEAR $L$common_seh_tail

	mov	rax,QWORD[152+r8]

	mov	r10d,DWORD[4+r11]
	lea	r10,[r10*1+rsi]
	cmp	rbx,r10
	jae	NEAR $L$common_seh_tail

	mov	rax,QWORD[208+r8]

	lea	rsi,[80+rax]
	lea	rax,[248+rax]
	lea	rdi,[512+r8]
	mov	ecx,20
	DD	0xa548f3fc

$L$common_seh_tail:
	mov	rdi,QWORD[8+rax]
	mov	rsi,QWORD[16+rax]
	mov	QWORD[152+r8],rax
	mov	QWORD[168+r8],rsi
	mov	QWORD[176+r8],rdi

	mov	rdi,QWORD[40+r9]
	mov	rsi,r8
	mov	ecx,154
	DD	0xa548f3fc

	mov	rsi,r9
	xor	rcx,rcx
	mov	rdx,QWORD[8+rsi]
	mov	r8,QWORD[rsi]
	mov	r9,QWORD[16+rsi]
	mov	r10,QWORD[40+rsi]
	lea	r11,[56+rsi]
	lea	r12,[24+rsi]
	mov	QWORD[32+rsp],r10
	mov	QWORD[40+rsp],r11
	mov	QWORD[48+rsp],r12
	mov	QWORD[56+rsp],rcx
	call	QWORD[__imp_RtlVirtualUnwind]

	mov	eax,1
	add	rsp,64
	popfq
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbp
	pop	rbx
	pop	rdi
	pop	rsi
	DB	0F3h,0C3h		;repret


section	.pdata rdata align=4
ALIGN	4
	DD	$L$SEH_begin_poly1305_init wrt ..imagebase
	DD	$L$SEH_end_poly1305_init wrt ..imagebase
	DD	$L$SEH_info_poly1305_init wrt ..imagebase

	DD	$L$SEH_begin_poly1305_blocks wrt ..imagebase
	DD	$L$SEH_end_poly1305_blocks wrt ..imagebase
	DD	$L$SEH_info_poly1305_blocks wrt ..imagebase

	DD	$L$SEH_begin_poly1305_emit wrt ..imagebase
	DD	$L$SEH_end_poly1305_emit wrt ..imagebase
	DD	$L$SEH_info_poly1305_emit wrt ..imagebase
section	.xdata rdata align=8
ALIGN	8
$L$SEH_info_poly1305_init:
DB	9,0,0,0
	DD	se_handler wrt ..imagebase
	DD	$L$SEH_begin_poly1305_init wrt ..imagebase,$L$SEH_begin_poly1305_init wrt ..imagebase

$L$SEH_info_poly1305_blocks:
DB	9,0,0,0
	DD	se_handler wrt ..imagebase
	DD	$L$blocks_body wrt ..imagebase,$L$blocks_epilogue wrt ..imagebase

$L$SEH_info_poly1305_emit:
DB	9,0,0,0
	DD	se_handler wrt ..imagebase
	DD	$L$SEH_begin_poly1305_emit wrt ..imagebase,$L$SEH_begin_poly1305_emit wrt ..imagebase
