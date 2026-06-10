default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD
;; For concatenation.
%define SEH64_CONCAT(a,b) a %+ b

%ifndef __YASM_MAJOR__
 ;; @name Register numbers.
 ;; @{
 %define SEH64_PE_GREG_rax     0
 %define SEH64_PE_GREG_rcx     1
 %define SEH64_PE_GREG_rdx     2
 %define SEH64_PE_GREG_rbx     3
 %define SEH64_PE_GREG_rsp     4
 %define SEH64_PE_GREG_rbp     5
 %define SEH64_PE_GREG_rsi     6
 %define SEH64_PE_GREG_rdi     7
 %define SEH64_PE_GREG_r8      8
 %define SEH64_PE_GREG_r9      9
 %define SEH64_PE_GREG_r10     10
 %define SEH64_PE_GREG_r11     11
 %define SEH64_PE_GREG_r12     12
 %define SEH64_PE_GREG_r13     13
 %define SEH64_PE_GREG_r14     14
 %define SEH64_PE_GREG_r15     15

 %define SEH64_PE_XREG_xmm0     0
 %define SEH64_PE_XREG_xmm1     1
 %define SEH64_PE_XREG_xmm2     2
 %define SEH64_PE_XREG_xmm3     3
 %define SEH64_PE_XREG_xmm4     4
 %define SEH64_PE_XREG_xmm5     5
 %define SEH64_PE_XREG_xmm6     6
 %define SEH64_PE_XREG_xmm7     7
 %define SEH64_PE_XREG_xmm8     8
 %define SEH64_PE_XREG_xmm9     9
 %define SEH64_PE_XREG_xmm10    10
 %define SEH64_PE_XREG_xmm11    11
 %define SEH64_PE_XREG_xmm12    12
 %define SEH64_PE_XREG_xmm13    13
 %define SEH64_PE_XREG_xmm14    14
 %define SEH64_PE_XREG_xmm15    15
 ;; @}

 ;; @name PE unwind operations.
 ;; @{
 %define SEH64_PE_PUSH_NONVOL      0
 %define SEH64_PE_ALLOC_LARGE      1
 %define SEH64_PE_ALLOC_SMALL      2
 %define SEH64_PE_SET_FPREG        3
 %define SEH64_PE_SAVE_NONVOL      4
 %define SEH64_PE_SAVE_NONVOL_FAR  5
 %define SEH64_PE_SAVE_XMM128      8
 %define SEH64_PE_SAVE_XMM128_FAR  9
 ;; @}

 ;; We keep the unwind bytes in the seh64_slot_bytes (x)define, in reverse order as per spec.
 %macro SEH64_APPEND_SLOT_PAIR 2
  %ifdef seh64_slot_bytes
   %xdefine seh64_slot_bytes %1, %2, seh64_slot_bytes
  %else
   %xdefine seh64_slot_bytes %1, %2
  %endif
 %endmacro

 ;; For multi-slot unwind info.
 %macro SEH64_APPEND_SLOT_BYTES 2+
  %rep %0
   %rotate -1
   %ifdef seh64_slot_bytes
    %xdefine seh64_slot_bytes %1, seh64_slot_bytes
   %else
    %xdefine seh64_slot_bytes %1
   %endif
  %endrep
 %endmacro

 ;; For generating labels.
 %define SEH64_OP_LABEL(a_idx) asm_seh64_proc %+ .seh64_op_label %+ a_idx
%endif ; !__YASM__

;; For producing proper .labels despite .Lxxxx labels messing up the prefixing.
%define SEH64_DOT_LABEL(a_DotLabel) SEH64_CONCAT(asm_seh64_proc,a_DotLabel)

%macro SEH64_PROC_FRAME 2
 %define asm_seh64_proc %1
 %ifdef __YASM_MAJOR__
        proc_frame %1
 %else
  %assign seh64_idxOps   0
  ;%assign seh64_FrameReg SEH64_PE_GREG_rsp
  %assign seh64_FrameReg 0
  %assign seh64_offFrame 0
  %undef  seh64_slot_bytes
%1:
.start_of_prologue:
 %endif
%endmacro

%macro SEH64_ENDPROC_FRAME 1
SEH64_DOT_LABEL(.end_proc):
 %ifdef __YASM_MAJOR__
	[endproc_frame]
 %else
        ; Emit the RUNTIME_FUNCTION entry.  The linker is picky here, no label.
  %ifndef ASM_DEFINED_PDATA_SECTION
   %define ASM_DEFINED_PDATA_SECTION
        section .pdata rdata align=4
  %else
        section .pdata
  %endif
        dd      %1                              wrt ..imagebase
        dd      SEH64_DOT_LABEL(.end_proc)      wrt ..imagebase
        dd      SEH64_DOT_LABEL(.unwind_info)   wrt ..imagebase

        ; Restore code section.
        section .text
 %endif
%endmacro

%macro SEH64_ENDPROLOG 0
SEH64_DOT_LABEL(.end_of_prologue):
 %ifdef __YASM_MAJOR__
        [endprolog]
 %else
        ; Emit the unwind info now.
  %ifndef ASM_DEFINED_XDATA_SECTION
   %define ASM_DEFINED_XDATA_SECTION
        section .xdata rdata align=4
  %else
        section .xdata
        align   4, db 0
  %endif
SEH64_DOT_LABEL(.unwind_info):
        db      1                       ; version 1 (3 bit), no flags (5 bits)
        db      SEH64_DOT_LABEL(.end_of_prologue) - SEH64_DOT_LABEL(.start_of_prologue)

        db      (SEH64_DOT_LABEL(.unwind_info_array_end) - SEH64_DOT_LABEL(.unwind_info_array)) / 2
        db      seh64_FrameReg | (seh64_offFrame & 0xf0) ; framereg and offset/16.
SEH64_DOT_LABEL(.unwind_info_array):
   %ifdef seh64_slot_bytes
        db      seh64_slot_bytes
    %undef seh64_slot_bytes
   %endif
SEH64_DOT_LABEL(.unwind_info_array_end):

        ; Reset the segment
        section .text
 %endif
%endmacro

%macro SEH64_ALLOCSTACK 1
 %ifdef __YASM_MAJOR__
        [allocstack %1]
 %else
SEH64_OP_LABEL(seh64_idxOps):
  %if (%1) & 7
   %error "SEH64_ALLOCSTACK must be a multiple of 8"
  %endif
  %if (%1) < 8
   %error "SEH64_ALLOCSTACK must have an argument that's 8 or higher."
  %elif (%1) <= 128
   SEH64_APPEND_SLOT_PAIR \
	SEH64_OP_LABEL(seh64_idxOps) - SEH64_DOT_LABEL(.start_of_prologue), \
        SEH64_PE_ALLOC_SMALL | ((((%1) / 8) - 1) << 4)
  %elif (%1) < 512
   SEH64_APPEND_SLOT_BYTES \
	SEH64_OP_LABEL(seh64_idxOps) - SEH64_DOT_LABEL(.start_of_prologue), \
	SEH64_PE_ALLOC_LARGE | 0, \
	((%1) / 8) & 0xff, ((%1) / 8) >> 8
  %else
   SEH64_APPEND_SLOT_BYTES \
	SEH64_OP_LABEL(seh64_idxOps) - SEH64_DOT_LABEL(.start_of_prologue), \
	SEH64_PE_ALLOC_LARGE | 1, \
	(%1) & 0xff, ((%1) >> 8) & 0xff, ((%1) >> 16) & 0xff, ((%1) >> 24) & 0xff
  %endif
  %assign seh64_idxOps seh64_idxOps + 1
 %endif
%endmacro

%macro SEH64_PUSHREG 1
 %ifdef __YASM_MAJOR__
        [pushreg %1]
 %else
SEH64_OP_LABEL(seh64_idxOps):
  SEH64_APPEND_SLOT_PAIR \
	SEH64_OP_LABEL(seh64_idxOps) - SEH64_DOT_LABEL(.start_of_prologue), \
	SEH64_PE_PUSH_NONVOL | (SEH64_PE_GREG_ %+ %1 << 4)
  %assign seh64_idxOps seh64_idxOps + 1
 %endif
%endmacro

%macro SEH64_SAVEREG 2
 %ifdef __YASM_MAJOR__
        [savereg %1, %2]
 %else
  %if (%2) & 7
   %error "SEH64_SAVEREG offset must be a multiple of 8"
  %endif
  %if (%2) <= (65535*8)
SEH64_OP_LABEL(seh64_idxOps):
   SEH64_APPEND_SLOT_BYTES \
	SEH64_OP_LABEL(seh64_idxOps) - SEH64_DOT_LABEL(.start_of_prologue), \
	SEH64_PE_SAVE_NONVOL | (SEH64_PE_GREG_ %+ %1 << 4), \
	((%2) / 8) & 0xff, \
	((%2) / 8) >> 8
  %else
   %error "SEH64_SAVEREG implement far offsets"
  %endif
  %assign seh64_idxOps seh64_idxOps + 1
 %endif
%endmacro

%macro SEH64_SAVEXMM128 2
 %ifdef __YASM_MAJOR__
        [savexmm128 %1, %2]
 %else
SEH64_OP_LABEL(seh64_idxOps):
  %if (%2) & 15
   %error "SEH64_SAVE_XMM128 offset must be a multiple of 16"
  %endif
  %if (%2) <= (65535*16)
   SEH64_APPEND_SLOT_BYTES \
	SEH64_OP_LABEL(seh64_idxOps) - SEH64_DOT_LABEL(.start_of_prologue), \
        SEH64_PE_SAVE_XMM128 | (SEH64_PE_XREG_ %+ %1 << 4), \
        ((%2) / 16) & 0xff, \
        ((%2) / 16) >> 8
  %else
   %error "SEH64_SAVE_XMM128 implement far offsets"
  %endif
  %assign seh64_idxOps seh64_idxOps + 1
 %endif
%endmacro

%macro SEH64_SETFRAME 2
 %ifdef __YASM_MAJOR__
        [setframe %1, %2]
 %else
SEH64_OP_LABEL(seh64_idxOps):
  SEH64_APPEND_SLOT_PAIR \
	SEH64_OP_LABEL(seh64_idxOps) - SEH64_DOT_LABEL(.start_of_prologue), \
        SEH64_PE_SET_FPREG | 0
  %assign seh64_FrameReg SEH64_PE_GREG_ %+ %1
  %assign seh64_offFrame %2
  %assign seh64_idxOps seh64_idxOps + 1
 %endif
%endmacro
EXTERN	OPENSSL_cpuid_setup

section	.CRT$XCU rdata align=8
		DQ	OPENSSL_cpuid_setup


common	OPENSSL_ia32cap_P 16

section	.text code align=64


global	OPENSSL_atomic_add

ALIGN	16
SEH64_PROC_FRAME OPENSSL_atomic_add,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	eax,DWORD[rcx]
$L$spin:	lea	r8,[rax*1+rdx]
DB	0xf0
	cmpxchg	DWORD[rcx],r8d
	jne	NEAR $L$spin
	mov	eax,r8d
DB	0x48,0x98
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_atomic_add


global	OPENSSL_rdtsc

ALIGN	16
SEH64_PROC_FRAME OPENSSL_rdtsc,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	rdtsc
	shl	rdx,32
	or	rax,rdx
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_rdtsc


global	OPENSSL_ia32_cpuid

ALIGN	16
SEH64_PROC_FRAME OPENSSL_ia32_cpuid,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx



DB	243,15,30,250
	mov	r8,rbx
	mov	QWORD[24+rsp],rbx
SEH64_SAVEREG rbx, 24	; .cfi_offset rbx, 24-8 cfa_reg_offset=8 cfa_rsp=-8
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8

	xor	eax,eax
	mov	QWORD[8+rdi],rax
	cpuid
	mov	r11d,eax

	xor	eax,eax
	cmp	ebx,0x756e6547
	setne	al
	mov	r9d,eax
	cmp	edx,0x49656e69
	setne	al
	or	r9d,eax
	cmp	ecx,0x6c65746e
	setne	al
	or	r9d,eax
	jz	NEAR $L$intel

	cmp	ebx,0x68747541
	setne	al
	mov	r10d,eax
	cmp	edx,0x69746E65
	setne	al
	or	r10d,eax
	cmp	ecx,0x444D4163
	setne	al
	or	r10d,eax
	jnz	NEAR $L$intel


	mov	eax,0x80000000
	cpuid
	cmp	eax,0x80000001
	jb	NEAR $L$intel
	mov	r10d,eax
	mov	eax,0x80000001
	cpuid
	or	r9d,ecx
	and	r9d,0x00000801

	cmp	r10d,0x80000008
	jb	NEAR $L$intel

	mov	eax,0x80000008
	cpuid
	movzx	r10,cl
	inc	r10

	mov	eax,1
	cpuid
	bt	edx,28
	jnc	NEAR $L$generic
	shr	ebx,16
	cmp	bl,r10b
	ja	NEAR $L$generic
	and	edx,0xefffffff
	jmp	NEAR $L$generic

$L$intel:
	cmp	r11d,4
	mov	r10d,-1
	jb	NEAR $L$nocacheinfo

	mov	eax,4
	mov	ecx,0
	cpuid
	mov	r10d,eax
	shr	r10d,14
	and	r10d,0xfff

$L$nocacheinfo:
	mov	eax,1
	cpuid
	movd	xmm0,eax
	and	edx,0xbfefffff
	cmp	r9d,0
	jne	NEAR $L$notintel
	or	edx,0x40000000
	and	ah,15
	cmp	ah,15
	jne	NEAR $L$notP4
	or	edx,0x00100000
$L$notP4:
	cmp	ah,6
	jne	NEAR $L$notintel
	and	eax,0x0fff0ff0
	cmp	eax,0x00050670
	je	NEAR $L$knights
	cmp	eax,0x00080650
	jne	NEAR $L$notintel
$L$knights:
	and	ecx,0xfbffffff

$L$notintel:
	bt	edx,28
	jnc	NEAR $L$generic
	and	edx,0xefffffff
	cmp	r10d,0
	je	NEAR $L$generic

	or	edx,0x10000000
	shr	ebx,16
	cmp	bl,1
	ja	NEAR $L$generic
	and	edx,0xefffffff
$L$generic:
	and	r9d,0x00000800
	and	ecx,0xfffff7ff
	or	r9d,ecx

	mov	r10d,edx

	cmp	r11d,7
	jb	NEAR $L$no_extended_info
	mov	eax,7
	xor	ecx,ecx
	cpuid
	bt	r9d,26
	jc	NEAR $L$notknights
	and	ebx,0xfff7ffff
$L$notknights:
	movd	eax,xmm0
	and	eax,0x0fff0ff0
	cmp	eax,0x00050650
	jne	NEAR $L$notskylakex
	and	ebx,0xfffeffff

$L$notskylakex:
	mov	DWORD[8+rdi],ebx
	mov	DWORD[12+rdi],ecx
$L$no_extended_info:

	bt	r9d,27
	jnc	NEAR $L$clear_avx
	xor	ecx,ecx
DB	0x0f,0x01,0xd0
	and	eax,0xe6
	cmp	eax,0xe6
	je	NEAR $L$done
	and	DWORD[8+rdi],0x3fdeffff




	and	eax,6
	cmp	eax,6
	je	NEAR $L$done
$L$clear_avx:
	mov	eax,0xefffe7ff
	and	r9d,eax
	mov	eax,0x3fdeffdf
	and	DWORD[8+rdi],eax
$L$done:
	shl	r9,32
	mov	eax,r10d
	mov	rbx,r8

	or	rax,r9
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_ia32_cpuid
$L$SEH_end_OPENSSL_ia32_cpuid:

global	OPENSSL_cleanse

ALIGN	16
SEH64_PROC_FRAME OPENSSL_cleanse,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	xor	rax,rax
	cmp	rdx,15
	jae	NEAR $L$ot
	cmp	rdx,0
	je	NEAR $L$ret
$L$ittle:
	mov	BYTE[rcx],al
	sub	rdx,1
	lea	rcx,[1+rcx]
	jnz	NEAR $L$ittle
$L$ret:
	DB	0F3h,0C3h		;repret
ALIGN	16
$L$ot:
	test	rcx,7
	jz	NEAR $L$aligned
	mov	BYTE[rcx],al
	lea	rdx,[((-1))+rdx]
	lea	rcx,[1+rcx]
	jmp	NEAR $L$ot
$L$aligned:
	mov	QWORD[rcx],rax
	lea	rdx,[((-8))+rdx]
	test	rdx,-8
	lea	rcx,[8+rcx]
	jnz	NEAR $L$aligned
	cmp	rdx,0
	jne	NEAR $L$ittle
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_cleanse


global	CRYPTO_memcmp

ALIGN	16
SEH64_PROC_FRAME CRYPTO_memcmp,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	xor	rax,rax
	xor	r10,r10
	cmp	r8,0
	je	NEAR $L$no_data
	cmp	r8,16
	jne	NEAR $L$oop_cmp
	mov	r10,QWORD[rcx]
	mov	r11,QWORD[8+rcx]
	mov	r8,1
	xor	r10,QWORD[rdx]
	xor	r11,QWORD[8+rdx]
	or	r10,r11
	cmovnz	rax,r8
	DB	0F3h,0C3h		;repret

ALIGN	16
$L$oop_cmp:
	mov	r10b,BYTE[rcx]
	lea	rcx,[1+rcx]
	xor	r10b,BYTE[rdx]
	lea	rdx,[1+rdx]
	or	al,r10b
	dec	r8
	jnz	NEAR $L$oop_cmp
	neg	rax
	shr	rax,63
$L$no_data:
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	CRYPTO_memcmp

global	OPENSSL_wipe_cpu

ALIGN	16
SEH64_PROC_FRAME OPENSSL_wipe_cpu,PUBLIC

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	pxor	xmm0,xmm0
	pxor	xmm1,xmm1
	pxor	xmm2,xmm2
	pxor	xmm3,xmm3
	pxor	xmm4,xmm4
	pxor	xmm5,xmm5
	xor	rcx,rcx
	xor	rdx,rdx
	xor	r8,r8
	xor	r9,r9
	xor	r10,r10
	xor	r11,r11
	lea	rax,[8+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_wipe_cpu

global	OPENSSL_instrument_bus

ALIGN	16
SEH64_PROC_FRAME OPENSSL_instrument_bus,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r10,rcx
	mov	rcx,rdx
	mov	r11,rdx

	rdtsc
	mov	r8d,eax
	mov	r9d,0
	clflush	[r10]
DB	0xf0
	add	DWORD[r10],r9d
	jmp	NEAR $L$oop
ALIGN	16
$L$oop:	rdtsc
	mov	edx,eax
	sub	eax,r8d
	mov	r8d,edx
	mov	r9d,eax
	clflush	[r10]
DB	0xf0
	add	DWORD[r10],eax
	lea	r10,[4+r10]
	sub	rcx,1
	jnz	NEAR $L$oop

	mov	rax,r11
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_instrument_bus


global	OPENSSL_instrument_bus2

ALIGN	16
SEH64_PROC_FRAME OPENSSL_instrument_bus2,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r10,rcx
	mov	rcx,rdx
	mov	r11,r8
	mov	QWORD[8+rsp],rcx

	rdtsc
	mov	r8d,eax
	mov	r9d,0

	clflush	[r10]
DB	0xf0
	add	DWORD[r10],r9d

	rdtsc
	mov	edx,eax
	sub	eax,r8d
	mov	r8d,edx
	mov	r9d,eax
$L$oop2:
	clflush	[r10]
DB	0xf0
	add	DWORD[r10],eax

	sub	r11,1
	jz	NEAR $L$done2

	rdtsc
	mov	edx,eax
	sub	eax,r8d
	mov	r8d,edx
	cmp	eax,r9d
	mov	r9d,eax
	mov	edx,0
	setne	dl
	sub	rcx,rdx
	lea	r10,[rdx*4+r10]
	jnz	NEAR $L$oop2

$L$done2:
	mov	rax,QWORD[8+rsp]
	sub	rax,rcx
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_instrument_bus2

global	OPENSSL_ia32_rdrand_bytes

ALIGN	16
SEH64_PROC_FRAME OPENSSL_ia32_rdrand_bytes,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	xor	rax,rax
	cmp	rdx,0
	je	NEAR $L$done_rdrand_bytes

	mov	r11,8
$L$oop_rdrand_bytes:
DB	73,15,199,242
	jc	NEAR $L$break_rdrand_bytes
	dec	r11
	jnz	NEAR $L$oop_rdrand_bytes
	jmp	NEAR $L$done_rdrand_bytes

ALIGN	16
$L$break_rdrand_bytes:
	cmp	rdx,8
	jb	NEAR $L$tail_rdrand_bytes
	mov	QWORD[rcx],r10
	lea	rcx,[8+rcx]
	add	rax,8
	sub	rdx,8
	jz	NEAR $L$done_rdrand_bytes
	mov	r11,8
	jmp	NEAR $L$oop_rdrand_bytes

ALIGN	16
$L$tail_rdrand_bytes:
	mov	BYTE[rcx],r10b
	lea	rcx,[1+rcx]
	inc	rax
	shr	r10,8
	dec	rdx
	jnz	NEAR $L$tail_rdrand_bytes

$L$done_rdrand_bytes:
	xor	r10,r10
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_ia32_rdrand_bytes

global	OPENSSL_ia32_rdseed_bytes

ALIGN	16
SEH64_PROC_FRAME OPENSSL_ia32_rdseed_bytes,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	xor	rax,rax
	cmp	rdx,0
	je	NEAR $L$done_rdseed_bytes

	mov	r11,8
$L$oop_rdseed_bytes:
DB	73,15,199,250
	jc	NEAR $L$break_rdseed_bytes
	dec	r11
	jnz	NEAR $L$oop_rdseed_bytes
	jmp	NEAR $L$done_rdseed_bytes

ALIGN	16
$L$break_rdseed_bytes:
	cmp	rdx,8
	jb	NEAR $L$tail_rdseed_bytes
	mov	QWORD[rcx],r10
	lea	rcx,[8+rcx]
	add	rax,8
	sub	rdx,8
	jz	NEAR $L$done_rdseed_bytes
	mov	r11,8
	jmp	NEAR $L$oop_rdseed_bytes

ALIGN	16
$L$tail_rdseed_bytes:
	mov	BYTE[rcx],r10b
	lea	rcx,[1+rcx]
	inc	rax
	shr	r10,8
	dec	rdx
	jnz	NEAR $L$tail_rdseed_bytes

$L$done_rdseed_bytes:
	xor	r10,r10
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	OPENSSL_ia32_rdseed_bytes

