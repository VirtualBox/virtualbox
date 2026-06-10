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
section	.text code align=64

EXTERN	OPENSSL_ia32cap_P

global	RC4

ALIGN	16
SEH64_PROC_FRAME RC4,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8
	mov	rcx,r9



DB	243,15,30,250
	or	rsi,rsi
	jne	NEAR $L$entry
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
$L$entry:
	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -16,16 cfa_reg=%rsp
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -24,24 cfa_reg=%rsp
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -32,32 cfa_reg=%rsp
	; cfa_rsp=-32
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=32 cfa_rsp=-32
	mov	r11,rsi
	mov	r12,rdx
	mov	r13,rcx
	xor	r10,r10
	xor	rcx,rcx

	lea	rdi,[8+rdi]
	mov	r10b,BYTE[((-8))+rdi]
	mov	cl,BYTE[((-4))+rdi]
	cmp	DWORD[256+rdi],-1
	je	NEAR $L$RC4_CHAR
	mov	r8d,DWORD[OPENSSL_ia32cap_P]
	xor	rbx,rbx
	inc	r10b
	sub	rbx,r10
	sub	r13,r12
	mov	eax,DWORD[r10*4+rdi]
	test	r11,-16
	jz	NEAR $L$loop1
	bt	r8d,30
	jc	NEAR $L$intel
	and	rbx,7
	lea	rsi,[1+r10]
	jz	NEAR $L$oop8
	sub	r11,rbx
$L$oop8_warmup:
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	mov	DWORD[r10*4+rdi],edx
	add	al,dl
	inc	r10b
	mov	edx,DWORD[rax*4+rdi]
	mov	eax,DWORD[r10*4+rdi]
	xor	dl,BYTE[r12]
	mov	BYTE[r13*1+r12],dl
	lea	r12,[1+r12]
	dec	rbx
	jnz	NEAR $L$oop8_warmup

	lea	rsi,[1+r10]
	jmp	NEAR $L$oop8
ALIGN	16
$L$oop8:
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	mov	ebx,DWORD[rsi*4+rdi]
	ror	r8,8
	mov	DWORD[r10*4+rdi],edx
	add	dl,al
	mov	r8b,BYTE[rdx*4+rdi]
	add	cl,bl
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	mov	eax,DWORD[4+rsi*4+rdi]
	ror	r8,8
	mov	DWORD[4+r10*4+rdi],edx
	add	dl,bl
	mov	r8b,BYTE[rdx*4+rdi]
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	mov	ebx,DWORD[8+rsi*4+rdi]
	ror	r8,8
	mov	DWORD[8+r10*4+rdi],edx
	add	dl,al
	mov	r8b,BYTE[rdx*4+rdi]
	add	cl,bl
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	mov	eax,DWORD[12+rsi*4+rdi]
	ror	r8,8
	mov	DWORD[12+r10*4+rdi],edx
	add	dl,bl
	mov	r8b,BYTE[rdx*4+rdi]
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	mov	ebx,DWORD[16+rsi*4+rdi]
	ror	r8,8
	mov	DWORD[16+r10*4+rdi],edx
	add	dl,al
	mov	r8b,BYTE[rdx*4+rdi]
	add	cl,bl
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	mov	eax,DWORD[20+rsi*4+rdi]
	ror	r8,8
	mov	DWORD[20+r10*4+rdi],edx
	add	dl,bl
	mov	r8b,BYTE[rdx*4+rdi]
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	mov	ebx,DWORD[24+rsi*4+rdi]
	ror	r8,8
	mov	DWORD[24+r10*4+rdi],edx
	add	dl,al
	mov	r8b,BYTE[rdx*4+rdi]
	add	sil,8
	add	cl,bl
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	mov	eax,DWORD[((-4))+rsi*4+rdi]
	ror	r8,8
	mov	DWORD[28+r10*4+rdi],edx
	add	dl,bl
	mov	r8b,BYTE[rdx*4+rdi]
	add	r10b,8
	ror	r8,8
	sub	r11,8

	xor	r8,QWORD[r12]
	mov	QWORD[r13*1+r12],r8
	lea	r12,[8+r12]

	test	r11,-8
	jnz	NEAR $L$oop8
	cmp	r11,0
	jne	NEAR $L$loop1
	jmp	NEAR $L$exit

ALIGN	16
$L$intel:
	test	r11,-32
	jz	NEAR $L$loop1
	and	rbx,15
	jz	NEAR $L$oop16_is_hot
	sub	r11,rbx
$L$oop16_warmup:
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	mov	DWORD[r10*4+rdi],edx
	add	al,dl
	inc	r10b
	mov	edx,DWORD[rax*4+rdi]
	mov	eax,DWORD[r10*4+rdi]
	xor	dl,BYTE[r12]
	mov	BYTE[r13*1+r12],dl
	lea	r12,[1+r12]
	dec	rbx
	jnz	NEAR $L$oop16_warmup

	mov	rbx,rcx
	xor	rcx,rcx
	mov	cl,bl

$L$oop16_is_hot:
	lea	rsi,[r10*4+rdi]
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	pxor	xmm0,xmm0
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[4+rsi]
	movzx	eax,al
	mov	DWORD[rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],0
	jmp	NEAR $L$oop16_enter
ALIGN	16
$L$oop16:
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	pxor	xmm2,xmm0
	psllq	xmm1,8
	pxor	xmm0,xmm0
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[4+rsi]
	movzx	eax,al
	mov	DWORD[rsi],edx
	pxor	xmm2,xmm1
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],0
	movdqu	XMMWORD[r13*1+r12],xmm2
	lea	r12,[16+r12]
$L$oop16_enter:
	mov	edx,DWORD[rcx*4+rdi]
	pxor	xmm1,xmm1
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	mov	eax,DWORD[8+rsi]
	movzx	ebx,bl
	mov	DWORD[4+rsi],edx
	add	cl,al
	pinsrw	xmm1,WORD[rbx*4+rdi],0
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[12+rsi]
	movzx	eax,al
	mov	DWORD[8+rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],1
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	mov	eax,DWORD[16+rsi]
	movzx	ebx,bl
	mov	DWORD[12+rsi],edx
	add	cl,al
	pinsrw	xmm1,WORD[rbx*4+rdi],1
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[20+rsi]
	movzx	eax,al
	mov	DWORD[16+rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],2
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	mov	eax,DWORD[24+rsi]
	movzx	ebx,bl
	mov	DWORD[20+rsi],edx
	add	cl,al
	pinsrw	xmm1,WORD[rbx*4+rdi],2
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[28+rsi]
	movzx	eax,al
	mov	DWORD[24+rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],3
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	mov	eax,DWORD[32+rsi]
	movzx	ebx,bl
	mov	DWORD[28+rsi],edx
	add	cl,al
	pinsrw	xmm1,WORD[rbx*4+rdi],3
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[36+rsi]
	movzx	eax,al
	mov	DWORD[32+rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],4
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	mov	eax,DWORD[40+rsi]
	movzx	ebx,bl
	mov	DWORD[36+rsi],edx
	add	cl,al
	pinsrw	xmm1,WORD[rbx*4+rdi],4
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[44+rsi]
	movzx	eax,al
	mov	DWORD[40+rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],5
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	mov	eax,DWORD[48+rsi]
	movzx	ebx,bl
	mov	DWORD[44+rsi],edx
	add	cl,al
	pinsrw	xmm1,WORD[rbx*4+rdi],5
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[52+rsi]
	movzx	eax,al
	mov	DWORD[48+rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],6
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	mov	eax,DWORD[56+rsi]
	movzx	ebx,bl
	mov	DWORD[52+rsi],edx
	add	cl,al
	pinsrw	xmm1,WORD[rbx*4+rdi],6
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	add	al,dl
	mov	ebx,DWORD[60+rsi]
	movzx	eax,al
	mov	DWORD[56+rsi],edx
	add	cl,bl
	pinsrw	xmm0,WORD[rax*4+rdi],7
	add	r10b,16
	movdqu	xmm2,XMMWORD[r12]
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],ebx
	add	bl,dl
	movzx	ebx,bl
	mov	DWORD[60+rsi],edx
	lea	rsi,[r10*4+rdi]
	pinsrw	xmm1,WORD[rbx*4+rdi],7
	mov	eax,DWORD[rsi]
	mov	rbx,rcx
	xor	rcx,rcx
	sub	r11,16
	mov	cl,bl
	test	r11,-16
	jnz	NEAR $L$oop16

	psllq	xmm1,8
	pxor	xmm2,xmm0
	pxor	xmm2,xmm1
	movdqu	XMMWORD[r13*1+r12],xmm2
	lea	r12,[16+r12]

	cmp	r11,0
	jne	NEAR $L$loop1
	jmp	NEAR $L$exit

ALIGN	16
$L$loop1:
	add	cl,al
	mov	edx,DWORD[rcx*4+rdi]
	mov	DWORD[rcx*4+rdi],eax
	mov	DWORD[r10*4+rdi],edx
	add	al,dl
	inc	r10b
	mov	edx,DWORD[rax*4+rdi]
	mov	eax,DWORD[r10*4+rdi]
	xor	dl,BYTE[r12]
	mov	BYTE[r13*1+r12],dl
	lea	r12,[1+r12]
	dec	r11
	jnz	NEAR $L$loop1
	jmp	NEAR $L$exit

ALIGN	16
$L$RC4_CHAR:
	add	r10b,1
	movzx	eax,BYTE[r10*1+rdi]
	test	r11,-8
	jz	NEAR $L$cloop1
	jmp	NEAR $L$cloop8
ALIGN	16
$L$cloop8:
	mov	r8d,DWORD[r12]
	mov	r9d,DWORD[4+r12]
	add	cl,al
	lea	rsi,[1+r10]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	esi,sil
	movzx	ebx,BYTE[rsi*1+rdi]
	mov	BYTE[rcx*1+rdi],al
	cmp	rcx,rsi
	mov	BYTE[r10*1+rdi],dl
	jne	NEAR $L$cmov0
	mov	rbx,rax
$L$cmov0:
	add	dl,al
	xor	r8b,BYTE[rdx*1+rdi]
	ror	r8d,8
	add	cl,bl
	lea	r10,[1+rsi]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	r10d,r10b
	movzx	eax,BYTE[r10*1+rdi]
	mov	BYTE[rcx*1+rdi],bl
	cmp	rcx,r10
	mov	BYTE[rsi*1+rdi],dl
	jne	NEAR $L$cmov1
	mov	rax,rbx
$L$cmov1:
	add	dl,bl
	xor	r8b,BYTE[rdx*1+rdi]
	ror	r8d,8
	add	cl,al
	lea	rsi,[1+r10]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	esi,sil
	movzx	ebx,BYTE[rsi*1+rdi]
	mov	BYTE[rcx*1+rdi],al
	cmp	rcx,rsi
	mov	BYTE[r10*1+rdi],dl
	jne	NEAR $L$cmov2
	mov	rbx,rax
$L$cmov2:
	add	dl,al
	xor	r8b,BYTE[rdx*1+rdi]
	ror	r8d,8
	add	cl,bl
	lea	r10,[1+rsi]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	r10d,r10b
	movzx	eax,BYTE[r10*1+rdi]
	mov	BYTE[rcx*1+rdi],bl
	cmp	rcx,r10
	mov	BYTE[rsi*1+rdi],dl
	jne	NEAR $L$cmov3
	mov	rax,rbx
$L$cmov3:
	add	dl,bl
	xor	r8b,BYTE[rdx*1+rdi]
	ror	r8d,8
	add	cl,al
	lea	rsi,[1+r10]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	esi,sil
	movzx	ebx,BYTE[rsi*1+rdi]
	mov	BYTE[rcx*1+rdi],al
	cmp	rcx,rsi
	mov	BYTE[r10*1+rdi],dl
	jne	NEAR $L$cmov4
	mov	rbx,rax
$L$cmov4:
	add	dl,al
	xor	r9b,BYTE[rdx*1+rdi]
	ror	r9d,8
	add	cl,bl
	lea	r10,[1+rsi]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	r10d,r10b
	movzx	eax,BYTE[r10*1+rdi]
	mov	BYTE[rcx*1+rdi],bl
	cmp	rcx,r10
	mov	BYTE[rsi*1+rdi],dl
	jne	NEAR $L$cmov5
	mov	rax,rbx
$L$cmov5:
	add	dl,bl
	xor	r9b,BYTE[rdx*1+rdi]
	ror	r9d,8
	add	cl,al
	lea	rsi,[1+r10]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	esi,sil
	movzx	ebx,BYTE[rsi*1+rdi]
	mov	BYTE[rcx*1+rdi],al
	cmp	rcx,rsi
	mov	BYTE[r10*1+rdi],dl
	jne	NEAR $L$cmov6
	mov	rbx,rax
$L$cmov6:
	add	dl,al
	xor	r9b,BYTE[rdx*1+rdi]
	ror	r9d,8
	add	cl,bl
	lea	r10,[1+rsi]
	movzx	edx,BYTE[rcx*1+rdi]
	movzx	r10d,r10b
	movzx	eax,BYTE[r10*1+rdi]
	mov	BYTE[rcx*1+rdi],bl
	cmp	rcx,r10
	mov	BYTE[rsi*1+rdi],dl
	jne	NEAR $L$cmov7
	mov	rax,rbx
$L$cmov7:
	add	dl,bl
	xor	r9b,BYTE[rdx*1+rdi]
	ror	r9d,8
	lea	r11,[((-8))+r11]
	mov	DWORD[r13],r8d
	lea	r12,[8+r12]
	mov	DWORD[4+r13],r9d
	lea	r13,[8+r13]

	test	r11,-8
	jnz	NEAR $L$cloop8
	cmp	r11,0
	jne	NEAR $L$cloop1
	jmp	NEAR $L$exit
ALIGN	16
$L$cloop1:
	add	cl,al
	movzx	ecx,cl
	movzx	edx,BYTE[rcx*1+rdi]
	mov	BYTE[rcx*1+rdi],al
	mov	BYTE[r10*1+rdi],dl
	add	dl,al
	add	r10b,1
	movzx	edx,dl
	movzx	r10d,r10b
	movzx	edx,BYTE[rdx*1+rdi]
	movzx	eax,BYTE[r10*1+rdi]
	xor	dl,BYTE[r12]
	lea	r12,[1+r12]
	mov	BYTE[r13],dl
	lea	r13,[1+r13]
	sub	r11,1
	jnz	NEAR $L$cloop1
	jmp	NEAR $L$exit

ALIGN	16
$L$exit:
	sub	r10b,1
	mov	DWORD[((-8))+rdi],r10d
	mov	DWORD[((-4))+rdi],ecx

	mov	r13,QWORD[rsp]

	mov	r12,QWORD[8+rsp]

	mov	rbx,QWORD[16+rsp]

	add	rsp,24

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	RC4
$L$SEH_end_RC4:
global	RC4_set_key

ALIGN	16
SEH64_PROC_FRAME RC4_set_key,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	rdi,[8+rdi]
	lea	rdx,[rsi*1+rdx]
	neg	rsi
	mov	rcx,rsi
	xor	eax,eax
	xor	r9,r9
	xor	r10,r10
	xor	r11,r11

	mov	r8d,DWORD[OPENSSL_ia32cap_P]
	bt	r8d,20
	jc	NEAR $L$c1stloop
	jmp	NEAR $L$w1stloop

ALIGN	16
$L$w1stloop:
	mov	DWORD[rax*4+rdi],eax
	add	al,1
	jnc	NEAR $L$w1stloop

	xor	r9,r9
	xor	r8,r8
ALIGN	16
$L$w2ndloop:
	mov	r10d,DWORD[r9*4+rdi]
	add	r8b,BYTE[rsi*1+rdx]
	add	r8b,r10b
	add	rsi,1
	mov	r11d,DWORD[r8*4+rdi]
	cmovz	rsi,rcx
	mov	DWORD[r8*4+rdi],r10d
	mov	DWORD[r9*4+rdi],r11d
	add	r9b,1
	jnc	NEAR $L$w2ndloop
	jmp	NEAR $L$exit_key

ALIGN	16
$L$c1stloop:
	mov	BYTE[rax*1+rdi],al
	add	al,1
	jnc	NEAR $L$c1stloop

	xor	r9,r9
	xor	r8,r8
ALIGN	16
$L$c2ndloop:
	mov	r10b,BYTE[r9*1+rdi]
	add	r8b,BYTE[rsi*1+rdx]
	add	r8b,r10b
	add	rsi,1
	mov	r11b,BYTE[r8*1+rdi]
	jnz	NEAR $L$cnowrap
	mov	rsi,rcx
$L$cnowrap:
	mov	BYTE[r8*1+rdi],r10b
	mov	BYTE[r9*1+rdi],r11b
	add	r9b,1
	jnc	NEAR $L$c2ndloop
	mov	DWORD[256+rdi],-1

ALIGN	16
$L$exit_key:
	xor	eax,eax
	mov	DWORD[((-8))+rdi],eax
	mov	DWORD[((-4))+rdi],eax
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	RC4_set_key
$L$SEH_end_RC4_set_key:

global	RC4_options

ALIGN	16
SEH64_PROC_FRAME RC4_options,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	rax,[$L$opts]
	mov	edx,DWORD[OPENSSL_ia32cap_P]
	bt	edx,20
	jc	NEAR $L$8xchar
	bt	edx,30
	jnc	NEAR $L$done
	add	rax,25
	DB	0F3h,0C3h		;repret
$L$8xchar:
	add	rax,12
$L$done:
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	RC4_options
ALIGN	64
$L$opts:
DB	114,99,52,40,56,120,44,105,110,116,41,0
DB	114,99,52,40,56,120,44,99,104,97,114,41,0
DB	114,99,52,40,49,54,120,44,105,110,116,41,0
DB	82,67,52,32,102,111,114,32,120,56,54,95,54,52,44,32
DB	67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97
DB	112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103
DB	62,0
ALIGN	64

