default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD
section	.text code align=64


global	x25519_fe51_mul

ALIGN	32
x25519_fe51_mul:
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
	mov	QWORD[16+rsp],rsi
	mov	rax,rsp
$L$SEH_begin_x25519_fe51_mul:
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



	push	rbp

	push	rbx

	push	r12

	push	r13

	push	r14

	push	r15

	lea	rsp,[((-40))+rsp]

$L$fe51_mul_body:

	mov	rax,QWORD[rsi]
	mov	r11,QWORD[rdx]
	mov	r12,QWORD[8+rdx]
	mov	r13,QWORD[16+rdx]
	mov	rbp,QWORD[24+rdx]
	mov	r14,QWORD[32+rdx]

	mov	QWORD[32+rsp],rdi
	mov	rdi,rax
	mul	r11
	mov	QWORD[rsp],r11
	mov	rbx,rax
	mov	rax,rdi
	mov	rcx,rdx
	mul	r12
	mov	QWORD[8+rsp],r12
	mov	r8,rax
	mov	rax,rdi
	lea	r15,[r14*8+r14]
	mov	r9,rdx
	mul	r13
	mov	QWORD[16+rsp],r13
	mov	r10,rax
	mov	rax,rdi
	lea	rdi,[r15*2+r14]
	mov	r11,rdx
	mul	rbp
	mov	r12,rax
	mov	rax,QWORD[rsi]
	mov	r13,rdx
	mul	r14
	mov	r14,rax
	mov	rax,QWORD[8+rsi]
	mov	r15,rdx

	mul	rdi
	add	rbx,rax
	mov	rax,QWORD[16+rsi]
	adc	rcx,rdx
	mul	rdi
	add	r8,rax
	mov	rax,QWORD[24+rsi]
	adc	r9,rdx
	mul	rdi
	add	r10,rax
	mov	rax,QWORD[32+rsi]
	adc	r11,rdx
	mul	rdi
	imul	rdi,rbp,19
	add	r12,rax
	mov	rax,QWORD[8+rsi]
	adc	r13,rdx
	mul	rbp
	mov	rbp,QWORD[16+rsp]
	add	r14,rax
	mov	rax,QWORD[16+rsi]
	adc	r15,rdx

	mul	rdi
	add	rbx,rax
	mov	rax,QWORD[24+rsi]
	adc	rcx,rdx
	mul	rdi
	add	r8,rax
	mov	rax,QWORD[32+rsi]
	adc	r9,rdx
	mul	rdi
	imul	rdi,rbp,19
	add	r10,rax
	mov	rax,QWORD[8+rsi]
	adc	r11,rdx
	mul	rbp
	add	r12,rax
	mov	rax,QWORD[16+rsi]
	adc	r13,rdx
	mul	rbp
	mov	rbp,QWORD[8+rsp]
	add	r14,rax
	mov	rax,QWORD[24+rsi]
	adc	r15,rdx

	mul	rdi
	add	rbx,rax
	mov	rax,QWORD[32+rsi]
	adc	rcx,rdx
	mul	rdi
	add	r8,rax
	mov	rax,QWORD[8+rsi]
	adc	r9,rdx
	mul	rbp
	imul	rdi,rbp,19
	add	r10,rax
	mov	rax,QWORD[16+rsi]
	adc	r11,rdx
	mul	rbp
	add	r12,rax
	mov	rax,QWORD[24+rsi]
	adc	r13,rdx
	mul	rbp
	mov	rbp,QWORD[rsp]
	add	r14,rax
	mov	rax,QWORD[32+rsi]
	adc	r15,rdx

	mul	rdi
	add	rbx,rax
	mov	rax,QWORD[8+rsi]
	adc	rcx,rdx
	mul	rbp
	add	r8,rax
	mov	rax,QWORD[16+rsi]
	adc	r9,rdx
	mul	rbp
	add	r10,rax
	mov	rax,QWORD[24+rsi]
	adc	r11,rdx
	mul	rbp
	add	r12,rax
	mov	rax,QWORD[32+rsi]
	adc	r13,rdx
	mul	rbp
	add	r14,rax
	adc	r15,rdx

	mov	rdi,QWORD[32+rsp]
	jmp	NEAR $L$reduce51
$L$fe51_mul_epilogue:

$L$SEH_end_x25519_fe51_mul:

global	x25519_fe51_sqr

ALIGN	32
x25519_fe51_sqr:
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
	mov	QWORD[16+rsp],rsi
	mov	rax,rsp
$L$SEH_begin_x25519_fe51_sqr:
	mov	rdi,rcx
	mov	rsi,rdx



	push	rbp

	push	rbx

	push	r12

	push	r13

	push	r14

	push	r15

	lea	rsp,[((-40))+rsp]

$L$fe51_sqr_body:

	mov	rax,QWORD[rsi]
	mov	r15,QWORD[16+rsi]
	mov	rbp,QWORD[32+rsi]

	mov	QWORD[32+rsp],rdi
	lea	r14,[rax*1+rax]
	mul	rax
	mov	rbx,rax
	mov	rax,QWORD[8+rsi]
	mov	rcx,rdx
	mul	r14
	mov	r8,rax
	mov	rax,r15
	mov	QWORD[rsp],r15
	mov	r9,rdx
	mul	r14
	mov	r10,rax
	mov	rax,QWORD[24+rsi]
	mov	r11,rdx
	imul	rdi,rbp,19
	mul	r14
	mov	r12,rax
	mov	rax,rbp
	mov	r13,rdx
	mul	r14
	mov	r14,rax
	mov	rax,rbp
	mov	r15,rdx

	mul	rdi
	add	r12,rax
	mov	rax,QWORD[8+rsi]
	adc	r13,rdx

	mov	rsi,QWORD[24+rsi]
	lea	rbp,[rax*1+rax]
	mul	rax
	add	r10,rax
	mov	rax,QWORD[rsp]
	adc	r11,rdx
	mul	rbp
	add	r12,rax
	mov	rax,rbp
	adc	r13,rdx
	mul	rsi
	add	r14,rax
	mov	rax,rbp
	adc	r15,rdx
	imul	rbp,rsi,19
	mul	rdi
	add	rbx,rax
	lea	rax,[rsi*1+rsi]
	adc	rcx,rdx

	mul	rdi
	add	r10,rax
	mov	rax,rsi
	adc	r11,rdx
	mul	rbp
	add	r8,rax
	mov	rax,QWORD[rsp]
	adc	r9,rdx

	lea	rsi,[rax*1+rax]
	mul	rax
	add	r14,rax
	mov	rax,rbp
	adc	r15,rdx
	mul	rsi
	add	rbx,rax
	mov	rax,rsi
	adc	rcx,rdx
	mul	rdi
	add	r8,rax
	adc	r9,rdx

	mov	rdi,QWORD[32+rsp]
	jmp	NEAR $L$reduce51

ALIGN	32
$L$reduce51:
	mov	rbp,0x7ffffffffffff

	mov	rdx,r10
	shr	r10,51
	shl	r11,13
	and	rdx,rbp
	or	r11,r10
	add	r12,r11
	adc	r13,0

	mov	rax,rbx
	shr	rbx,51
	shl	rcx,13
	and	rax,rbp
	or	rcx,rbx
	add	r8,rcx
	adc	r9,0

	mov	rbx,r12
	shr	r12,51
	shl	r13,13
	and	rbx,rbp
	or	r13,r12
	add	r14,r13
	adc	r15,0

	mov	rcx,r8
	shr	r8,51
	shl	r9,13
	and	rcx,rbp
	or	r9,r8
	add	rdx,r9

	mov	r10,r14
	shr	r14,51
	shl	r15,13
	and	r10,rbp
	or	r15,r14

	lea	r14,[r15*8+r15]
	lea	r15,[r14*2+r15]
	add	rax,r15

	mov	r8,rdx
	and	rdx,rbp
	shr	r8,51
	add	rbx,r8

	mov	r9,rax
	and	rax,rbp
	shr	r9,51
	add	rcx,r9

	mov	QWORD[rdi],rax
	mov	QWORD[8+rdi],rcx
	mov	QWORD[16+rdi],rdx
	mov	QWORD[24+rdi],rbx
	mov	QWORD[32+rdi],r10

	mov	r15,QWORD[40+rsp]

	mov	r14,QWORD[48+rsp]

	mov	r13,QWORD[56+rsp]

	mov	r12,QWORD[64+rsp]

	mov	rbx,QWORD[72+rsp]

	mov	rbp,QWORD[80+rsp]

	lea	rsp,[88+rsp]

$L$fe51_sqr_epilogue:
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret

$L$SEH_end_x25519_fe51_sqr:

global	x25519_fe51_mul121666

ALIGN	32
x25519_fe51_mul121666:
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
	mov	QWORD[16+rsp],rsi
	mov	rax,rsp
$L$SEH_begin_x25519_fe51_mul121666:
	mov	rdi,rcx
	mov	rsi,rdx



	push	rbp

	push	rbx

	push	r12

	push	r13

	push	r14

	push	r15

	lea	rsp,[((-40))+rsp]

$L$fe51_mul121666_body:
	mov	eax,121666

	mul	QWORD[rsi]
	mov	rbx,rax
	mov	eax,121666
	mov	rcx,rdx
	mul	QWORD[8+rsi]
	mov	r8,rax
	mov	eax,121666
	mov	r9,rdx
	mul	QWORD[16+rsi]
	mov	r10,rax
	mov	eax,121666
	mov	r11,rdx
	mul	QWORD[24+rsi]
	mov	r12,rax
	mov	eax,121666
	mov	r13,rdx
	mul	QWORD[32+rsi]
	mov	r14,rax
	mov	r15,rdx

	jmp	NEAR $L$reduce51
$L$fe51_mul121666_epilogue:

$L$SEH_end_x25519_fe51_mul121666:
global	x25519_fe64_eligible

ALIGN	32
x25519_fe64_eligible:

	xor	eax,eax
	DB	0F3h,0C3h		;repret



global	x25519_fe64_mul

global	x25519_fe64_sqr
global	x25519_fe64_mul121666
global	x25519_fe64_add
global	x25519_fe64_sub
global	x25519_fe64_tobytes
x25519_fe64_mul:
x25519_fe64_sqr:
x25519_fe64_mul121666:
x25519_fe64_add:
x25519_fe64_sub:
x25519_fe64_tobytes:

DB	0x0f,0x0b
	DB	0F3h,0C3h		;repret


DB	88,50,53,53,49,57,32,112,114,105,109,105,116,105,118,101
DB	115,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82
DB	89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112
DB	114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
EXTERN	__imp_RtlVirtualUnwind


ALIGN	16
short_handler:
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
	jmp	NEAR $L$common_seh_tail



ALIGN	16
full_handler:
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

	mov	r10d,DWORD[8+r11]
	lea	rax,[r10*1+rax]

	mov	rbp,QWORD[((-8))+rax]
	mov	rbx,QWORD[((-16))+rax]
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
	DD	$L$SEH_begin_x25519_fe51_mul wrt ..imagebase
	DD	$L$SEH_end_x25519_fe51_mul wrt ..imagebase
	DD	$L$SEH_info_x25519_fe51_mul wrt ..imagebase

	DD	$L$SEH_begin_x25519_fe51_sqr wrt ..imagebase
	DD	$L$SEH_end_x25519_fe51_sqr wrt ..imagebase
	DD	$L$SEH_info_x25519_fe51_sqr wrt ..imagebase

	DD	$L$SEH_begin_x25519_fe51_mul121666 wrt ..imagebase
	DD	$L$SEH_end_x25519_fe51_mul121666 wrt ..imagebase
	DD	$L$SEH_info_x25519_fe51_mul121666 wrt ..imagebase
section	.xdata rdata align=8
ALIGN	8
$L$SEH_info_x25519_fe51_mul:
DB	9,0,0,0
	DD	full_handler wrt ..imagebase
	DD	$L$fe51_mul_body wrt ..imagebase,$L$fe51_mul_epilogue wrt ..imagebase
	DD	88,0
$L$SEH_info_x25519_fe51_sqr:
DB	9,0,0,0
	DD	full_handler wrt ..imagebase
	DD	$L$fe51_sqr_body wrt ..imagebase,$L$fe51_sqr_epilogue wrt ..imagebase
	DD	88,0
$L$SEH_info_x25519_fe51_mul121666:
DB	9,0,0,0
	DD	full_handler wrt ..imagebase
	DD	$L$fe51_mul121666_body wrt ..imagebase,$L$fe51_mul121666_epilogue wrt ..imagebase
	DD	88,0
