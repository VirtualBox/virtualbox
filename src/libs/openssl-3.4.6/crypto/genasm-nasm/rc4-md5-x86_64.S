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

ALIGN	16

global	rc4_md5_enc

SEH64_PROC_FRAME rc4_md5_enc,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8
	mov	rcx,r9
	mov	r8,QWORD[40+rsp]
	mov	r9,QWORD[48+rsp]



	cmp	r9,0
	je	NEAR $L$abort
	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -16,16 cfa_reg=%rsp
	push	rbp
SEH64_PUSHREG	rbp	; cfa_rsp,cfa_reg_offset => -24,24 cfa_reg=%rsp
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -32,32 cfa_reg=%rsp
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -40,40 cfa_reg=%rsp
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -48,48 cfa_reg=%rsp
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -56,56 cfa_reg=%rsp
	sub	rsp,40
SEH64_ALLOCSTACK 40
	; cfa_rsp=-96
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=96 cfa_rsp=-96
	mov	r11,rcx
	mov	r12,r9
	mov	r13,rsi
	mov	r14,rdx
	mov	r15,r8
	xor	rbp,rbp
	xor	rcx,rcx

	lea	rdi,[8+rdi]
	mov	bpl,BYTE[((-8))+rdi]
	mov	cl,BYTE[((-4))+rdi]

	inc	bpl
	sub	r14,r13
	mov	eax,DWORD[rbp*4+rdi]
	add	cl,al
	lea	rsi,[rbp*4+rdi]
	shl	r12,6
	add	r12,r15
	mov	QWORD[16+rsp],r12

	mov	QWORD[24+rsp],r11
	mov	r8d,DWORD[r11]
	mov	r9d,DWORD[4+r11]
	mov	r10d,DWORD[8+r11]
	mov	r11d,DWORD[12+r11]
	jmp	NEAR $L$oop

ALIGN	16
$L$oop:
	mov	DWORD[rsp],r8d
	mov	DWORD[4+rsp],r9d
	mov	DWORD[8+rsp],r10d
	mov	r12d,r11d
	mov	DWORD[12+rsp],r11d
	pxor	xmm0,xmm0
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r8d,DWORD[r15]
	add	al,dl
	mov	ebx,DWORD[4+rsi]
	add	r8d,3614090360
	xor	r12d,r11d
	movzx	eax,al
	mov	DWORD[rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,7
	mov	r12d,r10d
	movd	xmm0,DWORD[rax*4+rdi]

	add	r8d,r9d
	pxor	xmm1,xmm1
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r11d,DWORD[4+r15]
	add	bl,dl
	mov	eax,DWORD[8+rsi]
	add	r11d,3905402710
	xor	r12d,r10d
	movzx	ebx,bl
	mov	DWORD[4+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,12
	mov	r12d,r9d
	movd	xmm1,DWORD[rbx*4+rdi]

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r10d,DWORD[8+r15]
	add	al,dl
	mov	ebx,DWORD[12+rsi]
	add	r10d,606105819
	xor	r12d,r9d
	movzx	eax,al
	mov	DWORD[8+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,17
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],1

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r9d,DWORD[12+r15]
	add	bl,dl
	mov	eax,DWORD[16+rsi]
	add	r9d,3250441966
	xor	r12d,r8d
	movzx	ebx,bl
	mov	DWORD[12+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,22
	mov	r12d,r11d
	pinsrw	xmm1,WORD[rbx*4+rdi],1

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r8d,DWORD[16+r15]
	add	al,dl
	mov	ebx,DWORD[20+rsi]
	add	r8d,4118548399
	xor	r12d,r11d
	movzx	eax,al
	mov	DWORD[16+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,7
	mov	r12d,r10d
	pinsrw	xmm0,WORD[rax*4+rdi],2

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r11d,DWORD[20+r15]
	add	bl,dl
	mov	eax,DWORD[24+rsi]
	add	r11d,1200080426
	xor	r12d,r10d
	movzx	ebx,bl
	mov	DWORD[20+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,12
	mov	r12d,r9d
	pinsrw	xmm1,WORD[rbx*4+rdi],2

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r10d,DWORD[24+r15]
	add	al,dl
	mov	ebx,DWORD[28+rsi]
	add	r10d,2821735955
	xor	r12d,r9d
	movzx	eax,al
	mov	DWORD[24+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,17
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],3

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r9d,DWORD[28+r15]
	add	bl,dl
	mov	eax,DWORD[32+rsi]
	add	r9d,4249261313
	xor	r12d,r8d
	movzx	ebx,bl
	mov	DWORD[28+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,22
	mov	r12d,r11d
	pinsrw	xmm1,WORD[rbx*4+rdi],3

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r8d,DWORD[32+r15]
	add	al,dl
	mov	ebx,DWORD[36+rsi]
	add	r8d,1770035416
	xor	r12d,r11d
	movzx	eax,al
	mov	DWORD[32+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,7
	mov	r12d,r10d
	pinsrw	xmm0,WORD[rax*4+rdi],4

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r11d,DWORD[36+r15]
	add	bl,dl
	mov	eax,DWORD[40+rsi]
	add	r11d,2336552879
	xor	r12d,r10d
	movzx	ebx,bl
	mov	DWORD[36+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,12
	mov	r12d,r9d
	pinsrw	xmm1,WORD[rbx*4+rdi],4

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r10d,DWORD[40+r15]
	add	al,dl
	mov	ebx,DWORD[44+rsi]
	add	r10d,4294925233
	xor	r12d,r9d
	movzx	eax,al
	mov	DWORD[40+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,17
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],5

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r9d,DWORD[44+r15]
	add	bl,dl
	mov	eax,DWORD[48+rsi]
	add	r9d,2304563134
	xor	r12d,r8d
	movzx	ebx,bl
	mov	DWORD[44+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,22
	mov	r12d,r11d
	pinsrw	xmm1,WORD[rbx*4+rdi],5

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r8d,DWORD[48+r15]
	add	al,dl
	mov	ebx,DWORD[52+rsi]
	add	r8d,1804603682
	xor	r12d,r11d
	movzx	eax,al
	mov	DWORD[48+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,7
	mov	r12d,r10d
	pinsrw	xmm0,WORD[rax*4+rdi],6

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r11d,DWORD[52+r15]
	add	bl,dl
	mov	eax,DWORD[56+rsi]
	add	r11d,4254626195
	xor	r12d,r10d
	movzx	ebx,bl
	mov	DWORD[52+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,12
	mov	r12d,r9d
	pinsrw	xmm1,WORD[rbx*4+rdi],6

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r10d,DWORD[56+r15]
	add	al,dl
	mov	ebx,DWORD[60+rsi]
	add	r10d,2792965006
	xor	r12d,r9d
	movzx	eax,al
	mov	DWORD[56+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,17
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],7

	add	r10d,r11d
	movdqu	xmm2,XMMWORD[r13]
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r9d,DWORD[60+r15]
	add	bl,dl
	mov	eax,DWORD[64+rsi]
	add	r9d,1236535329
	xor	r12d,r8d
	movzx	ebx,bl
	mov	DWORD[60+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,22
	mov	r12d,r10d
	pinsrw	xmm1,WORD[rbx*4+rdi],7

	add	r9d,r10d
	psllq	xmm1,8
	pxor	xmm2,xmm0
	pxor	xmm2,xmm1
	pxor	xmm0,xmm0
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r8d,DWORD[4+r15]
	add	al,dl
	mov	ebx,DWORD[68+rsi]
	add	r8d,4129170786
	xor	r12d,r10d
	movzx	eax,al
	mov	DWORD[64+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,5
	mov	r12d,r9d
	movd	xmm0,DWORD[rax*4+rdi]

	add	r8d,r9d
	pxor	xmm1,xmm1
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r11d,DWORD[24+r15]
	add	bl,dl
	mov	eax,DWORD[72+rsi]
	add	r11d,3225465664
	xor	r12d,r9d
	movzx	ebx,bl
	mov	DWORD[68+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,9
	mov	r12d,r8d
	movd	xmm1,DWORD[rbx*4+rdi]

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r10d,DWORD[44+r15]
	add	al,dl
	mov	ebx,DWORD[76+rsi]
	add	r10d,643717713
	xor	r12d,r8d
	movzx	eax,al
	mov	DWORD[72+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,14
	mov	r12d,r11d
	pinsrw	xmm0,WORD[rax*4+rdi],1

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r9d,DWORD[r15]
	add	bl,dl
	mov	eax,DWORD[80+rsi]
	add	r9d,3921069994
	xor	r12d,r11d
	movzx	ebx,bl
	mov	DWORD[76+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,20
	mov	r12d,r10d
	pinsrw	xmm1,WORD[rbx*4+rdi],1

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r8d,DWORD[20+r15]
	add	al,dl
	mov	ebx,DWORD[84+rsi]
	add	r8d,3593408605
	xor	r12d,r10d
	movzx	eax,al
	mov	DWORD[80+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,5
	mov	r12d,r9d
	pinsrw	xmm0,WORD[rax*4+rdi],2

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r11d,DWORD[40+r15]
	add	bl,dl
	mov	eax,DWORD[88+rsi]
	add	r11d,38016083
	xor	r12d,r9d
	movzx	ebx,bl
	mov	DWORD[84+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,9
	mov	r12d,r8d
	pinsrw	xmm1,WORD[rbx*4+rdi],2

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r10d,DWORD[60+r15]
	add	al,dl
	mov	ebx,DWORD[92+rsi]
	add	r10d,3634488961
	xor	r12d,r8d
	movzx	eax,al
	mov	DWORD[88+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,14
	mov	r12d,r11d
	pinsrw	xmm0,WORD[rax*4+rdi],3

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r9d,DWORD[16+r15]
	add	bl,dl
	mov	eax,DWORD[96+rsi]
	add	r9d,3889429448
	xor	r12d,r11d
	movzx	ebx,bl
	mov	DWORD[92+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,20
	mov	r12d,r10d
	pinsrw	xmm1,WORD[rbx*4+rdi],3

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r8d,DWORD[36+r15]
	add	al,dl
	mov	ebx,DWORD[100+rsi]
	add	r8d,568446438
	xor	r12d,r10d
	movzx	eax,al
	mov	DWORD[96+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,5
	mov	r12d,r9d
	pinsrw	xmm0,WORD[rax*4+rdi],4

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r11d,DWORD[56+r15]
	add	bl,dl
	mov	eax,DWORD[104+rsi]
	add	r11d,3275163606
	xor	r12d,r9d
	movzx	ebx,bl
	mov	DWORD[100+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,9
	mov	r12d,r8d
	pinsrw	xmm1,WORD[rbx*4+rdi],4

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r10d,DWORD[12+r15]
	add	al,dl
	mov	ebx,DWORD[108+rsi]
	add	r10d,4107603335
	xor	r12d,r8d
	movzx	eax,al
	mov	DWORD[104+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,14
	mov	r12d,r11d
	pinsrw	xmm0,WORD[rax*4+rdi],5

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r9d,DWORD[32+r15]
	add	bl,dl
	mov	eax,DWORD[112+rsi]
	add	r9d,1163531501
	xor	r12d,r11d
	movzx	ebx,bl
	mov	DWORD[108+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,20
	mov	r12d,r10d
	pinsrw	xmm1,WORD[rbx*4+rdi],5

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r11d
	add	r8d,DWORD[52+r15]
	add	al,dl
	mov	ebx,DWORD[116+rsi]
	add	r8d,2850285829
	xor	r12d,r10d
	movzx	eax,al
	mov	DWORD[112+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,5
	mov	r12d,r9d
	pinsrw	xmm0,WORD[rax*4+rdi],6

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r10d
	add	r11d,DWORD[8+r15]
	add	bl,dl
	mov	eax,DWORD[120+rsi]
	add	r11d,4243563512
	xor	r12d,r9d
	movzx	ebx,bl
	mov	DWORD[116+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,9
	mov	r12d,r8d
	pinsrw	xmm1,WORD[rbx*4+rdi],6

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	and	r12d,r9d
	add	r10d,DWORD[28+r15]
	add	al,dl
	mov	ebx,DWORD[124+rsi]
	add	r10d,1735328473
	xor	r12d,r8d
	movzx	eax,al
	mov	DWORD[120+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,14
	mov	r12d,r11d
	pinsrw	xmm0,WORD[rax*4+rdi],7

	add	r10d,r11d
	movdqu	xmm3,XMMWORD[16+r13]
	add	bpl,32
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	and	r12d,r8d
	add	r9d,DWORD[48+r15]
	add	bl,dl
	mov	eax,DWORD[rbp*4+rdi]
	add	r9d,2368359562
	xor	r12d,r11d
	movzx	ebx,bl
	mov	DWORD[124+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,20
	mov	r12d,r11d
	pinsrw	xmm1,WORD[rbx*4+rdi],7

	add	r9d,r10d
	mov	rsi,rcx
	xor	rcx,rcx
	mov	cl,sil
	lea	rsi,[rbp*4+rdi]
	psllq	xmm1,8
	pxor	xmm3,xmm0
	pxor	xmm3,xmm1
	pxor	xmm0,xmm0
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r9d
	add	r8d,DWORD[20+r15]
	add	al,dl
	mov	ebx,DWORD[4+rsi]
	add	r8d,4294588738
	movzx	eax,al
	add	r8d,r12d
	mov	DWORD[rsi],edx
	add	cl,bl
	rol	r8d,4
	mov	r12d,r10d
	movd	xmm0,DWORD[rax*4+rdi]

	add	r8d,r9d
	pxor	xmm1,xmm1
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r8d
	add	r11d,DWORD[32+r15]
	add	bl,dl
	mov	eax,DWORD[8+rsi]
	add	r11d,2272392833
	movzx	ebx,bl
	add	r11d,r12d
	mov	DWORD[4+rsi],edx
	add	cl,al
	rol	r11d,11
	mov	r12d,r9d
	movd	xmm1,DWORD[rbx*4+rdi]

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r11d
	add	r10d,DWORD[44+r15]
	add	al,dl
	mov	ebx,DWORD[12+rsi]
	add	r10d,1839030562
	movzx	eax,al
	add	r10d,r12d
	mov	DWORD[8+rsi],edx
	add	cl,bl
	rol	r10d,16
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],1

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r10d
	add	r9d,DWORD[56+r15]
	add	bl,dl
	mov	eax,DWORD[16+rsi]
	add	r9d,4259657740
	movzx	ebx,bl
	add	r9d,r12d
	mov	DWORD[12+rsi],edx
	add	cl,al
	rol	r9d,23
	mov	r12d,r11d
	pinsrw	xmm1,WORD[rbx*4+rdi],1

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r9d
	add	r8d,DWORD[4+r15]
	add	al,dl
	mov	ebx,DWORD[20+rsi]
	add	r8d,2763975236
	movzx	eax,al
	add	r8d,r12d
	mov	DWORD[16+rsi],edx
	add	cl,bl
	rol	r8d,4
	mov	r12d,r10d
	pinsrw	xmm0,WORD[rax*4+rdi],2

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r8d
	add	r11d,DWORD[16+r15]
	add	bl,dl
	mov	eax,DWORD[24+rsi]
	add	r11d,1272893353
	movzx	ebx,bl
	add	r11d,r12d
	mov	DWORD[20+rsi],edx
	add	cl,al
	rol	r11d,11
	mov	r12d,r9d
	pinsrw	xmm1,WORD[rbx*4+rdi],2

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r11d
	add	r10d,DWORD[28+r15]
	add	al,dl
	mov	ebx,DWORD[28+rsi]
	add	r10d,4139469664
	movzx	eax,al
	add	r10d,r12d
	mov	DWORD[24+rsi],edx
	add	cl,bl
	rol	r10d,16
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],3

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r10d
	add	r9d,DWORD[40+r15]
	add	bl,dl
	mov	eax,DWORD[32+rsi]
	add	r9d,3200236656
	movzx	ebx,bl
	add	r9d,r12d
	mov	DWORD[28+rsi],edx
	add	cl,al
	rol	r9d,23
	mov	r12d,r11d
	pinsrw	xmm1,WORD[rbx*4+rdi],3

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r9d
	add	r8d,DWORD[52+r15]
	add	al,dl
	mov	ebx,DWORD[36+rsi]
	add	r8d,681279174
	movzx	eax,al
	add	r8d,r12d
	mov	DWORD[32+rsi],edx
	add	cl,bl
	rol	r8d,4
	mov	r12d,r10d
	pinsrw	xmm0,WORD[rax*4+rdi],4

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r8d
	add	r11d,DWORD[r15]
	add	bl,dl
	mov	eax,DWORD[40+rsi]
	add	r11d,3936430074
	movzx	ebx,bl
	add	r11d,r12d
	mov	DWORD[36+rsi],edx
	add	cl,al
	rol	r11d,11
	mov	r12d,r9d
	pinsrw	xmm1,WORD[rbx*4+rdi],4

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r11d
	add	r10d,DWORD[12+r15]
	add	al,dl
	mov	ebx,DWORD[44+rsi]
	add	r10d,3572445317
	movzx	eax,al
	add	r10d,r12d
	mov	DWORD[40+rsi],edx
	add	cl,bl
	rol	r10d,16
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],5

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r10d
	add	r9d,DWORD[24+r15]
	add	bl,dl
	mov	eax,DWORD[48+rsi]
	add	r9d,76029189
	movzx	ebx,bl
	add	r9d,r12d
	mov	DWORD[44+rsi],edx
	add	cl,al
	rol	r9d,23
	mov	r12d,r11d
	pinsrw	xmm1,WORD[rbx*4+rdi],5

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r9d
	add	r8d,DWORD[36+r15]
	add	al,dl
	mov	ebx,DWORD[52+rsi]
	add	r8d,3654602809
	movzx	eax,al
	add	r8d,r12d
	mov	DWORD[48+rsi],edx
	add	cl,bl
	rol	r8d,4
	mov	r12d,r10d
	pinsrw	xmm0,WORD[rax*4+rdi],6

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r8d
	add	r11d,DWORD[48+r15]
	add	bl,dl
	mov	eax,DWORD[56+rsi]
	add	r11d,3873151461
	movzx	ebx,bl
	add	r11d,r12d
	mov	DWORD[52+rsi],edx
	add	cl,al
	rol	r11d,11
	mov	r12d,r9d
	pinsrw	xmm1,WORD[rbx*4+rdi],6

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],eax
	xor	r12d,r11d
	add	r10d,DWORD[60+r15]
	add	al,dl
	mov	ebx,DWORD[60+rsi]
	add	r10d,530742520
	movzx	eax,al
	add	r10d,r12d
	mov	DWORD[56+rsi],edx
	add	cl,bl
	rol	r10d,16
	mov	r12d,r8d
	pinsrw	xmm0,WORD[rax*4+rdi],7

	add	r10d,r11d
	movdqu	xmm4,XMMWORD[32+r13]
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],ebx
	xor	r12d,r10d
	add	r9d,DWORD[8+r15]
	add	bl,dl
	mov	eax,DWORD[64+rsi]
	add	r9d,3299628645
	movzx	ebx,bl
	add	r9d,r12d
	mov	DWORD[60+rsi],edx
	add	cl,al
	rol	r9d,23
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],7

	add	r9d,r10d
	psllq	xmm1,8
	pxor	xmm4,xmm0
	pxor	xmm4,xmm1
	pxor	xmm0,xmm0
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r9d
	add	r8d,DWORD[r15]
	add	al,dl
	mov	ebx,DWORD[68+rsi]
	add	r8d,4096336452
	movzx	eax,al
	xor	r12d,r10d
	mov	DWORD[64+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,6
	mov	r12d,-1
	movd	xmm0,DWORD[rax*4+rdi]

	add	r8d,r9d
	pxor	xmm1,xmm1
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r8d
	add	r11d,DWORD[28+r15]
	add	bl,dl
	mov	eax,DWORD[72+rsi]
	add	r11d,1126891415
	movzx	ebx,bl
	xor	r12d,r9d
	mov	DWORD[68+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,10
	mov	r12d,-1
	movd	xmm1,DWORD[rbx*4+rdi]

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r11d
	add	r10d,DWORD[56+r15]
	add	al,dl
	mov	ebx,DWORD[76+rsi]
	add	r10d,2878612391
	movzx	eax,al
	xor	r12d,r8d
	mov	DWORD[72+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,15
	mov	r12d,-1
	pinsrw	xmm0,WORD[rax*4+rdi],1

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r10d
	add	r9d,DWORD[20+r15]
	add	bl,dl
	mov	eax,DWORD[80+rsi]
	add	r9d,4237533241
	movzx	ebx,bl
	xor	r12d,r11d
	mov	DWORD[76+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,21
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],1

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r9d
	add	r8d,DWORD[48+r15]
	add	al,dl
	mov	ebx,DWORD[84+rsi]
	add	r8d,1700485571
	movzx	eax,al
	xor	r12d,r10d
	mov	DWORD[80+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,6
	mov	r12d,-1
	pinsrw	xmm0,WORD[rax*4+rdi],2

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r8d
	add	r11d,DWORD[12+r15]
	add	bl,dl
	mov	eax,DWORD[88+rsi]
	add	r11d,2399980690
	movzx	ebx,bl
	xor	r12d,r9d
	mov	DWORD[84+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,10
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],2

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r11d
	add	r10d,DWORD[40+r15]
	add	al,dl
	mov	ebx,DWORD[92+rsi]
	add	r10d,4293915773
	movzx	eax,al
	xor	r12d,r8d
	mov	DWORD[88+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,15
	mov	r12d,-1
	pinsrw	xmm0,WORD[rax*4+rdi],3

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r10d
	add	r9d,DWORD[4+r15]
	add	bl,dl
	mov	eax,DWORD[96+rsi]
	add	r9d,2240044497
	movzx	ebx,bl
	xor	r12d,r11d
	mov	DWORD[92+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,21
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],3

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r9d
	add	r8d,DWORD[32+r15]
	add	al,dl
	mov	ebx,DWORD[100+rsi]
	add	r8d,1873313359
	movzx	eax,al
	xor	r12d,r10d
	mov	DWORD[96+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,6
	mov	r12d,-1
	pinsrw	xmm0,WORD[rax*4+rdi],4

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r8d
	add	r11d,DWORD[60+r15]
	add	bl,dl
	mov	eax,DWORD[104+rsi]
	add	r11d,4264355552
	movzx	ebx,bl
	xor	r12d,r9d
	mov	DWORD[100+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,10
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],4

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r11d
	add	r10d,DWORD[24+r15]
	add	al,dl
	mov	ebx,DWORD[108+rsi]
	add	r10d,2734768916
	movzx	eax,al
	xor	r12d,r8d
	mov	DWORD[104+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,15
	mov	r12d,-1
	pinsrw	xmm0,WORD[rax*4+rdi],5

	add	r10d,r11d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r10d
	add	r9d,DWORD[52+r15]
	add	bl,dl
	mov	eax,DWORD[112+rsi]
	add	r9d,1309151649
	movzx	ebx,bl
	xor	r12d,r11d
	mov	DWORD[108+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,21
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],5

	add	r9d,r10d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r11d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r9d
	add	r8d,DWORD[16+r15]
	add	al,dl
	mov	ebx,DWORD[116+rsi]
	add	r8d,4149444226
	movzx	eax,al
	xor	r12d,r10d
	mov	DWORD[112+rsi],edx
	add	r8d,r12d
	add	cl,bl
	rol	r8d,6
	mov	r12d,-1
	pinsrw	xmm0,WORD[rax*4+rdi],6

	add	r8d,r9d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r10d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r8d
	add	r11d,DWORD[44+r15]
	add	bl,dl
	mov	eax,DWORD[120+rsi]
	add	r11d,3174756917
	movzx	ebx,bl
	xor	r12d,r9d
	mov	DWORD[116+rsi],edx
	add	r11d,r12d
	add	cl,al
	rol	r11d,10
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],6

	add	r11d,r8d
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r9d
	mov	DWORD[rcx*4+rdi],eax
	or	r12d,r11d
	add	r10d,DWORD[8+r15]
	add	al,dl
	mov	ebx,DWORD[124+rsi]
	add	r10d,718787259
	movzx	eax,al
	xor	r12d,r8d
	mov	DWORD[120+rsi],edx
	add	r10d,r12d
	add	cl,bl
	rol	r10d,15
	mov	r12d,-1
	pinsrw	xmm0,WORD[rax*4+rdi],7

	add	r10d,r11d
	movdqu	xmm5,XMMWORD[48+r13]
	add	bpl,32
	mov	edx,DWORD[rcx*4+rdi]
	xor	r12d,r8d
	mov	DWORD[rcx*4+rdi],ebx
	or	r12d,r10d
	add	r9d,DWORD[36+r15]
	add	bl,dl
	mov	eax,DWORD[rbp*4+rdi]
	add	r9d,3951481745
	movzx	ebx,bl
	xor	r12d,r11d
	mov	DWORD[124+rsi],edx
	add	r9d,r12d
	add	cl,al
	rol	r9d,21
	mov	r12d,-1
	pinsrw	xmm1,WORD[rbx*4+rdi],7

	add	r9d,r10d
	mov	rsi,rbp
	xor	rbp,rbp
	mov	bpl,sil
	mov	rsi,rcx
	xor	rcx,rcx
	mov	cl,sil
	lea	rsi,[rbp*4+rdi]
	psllq	xmm1,8
	pxor	xmm5,xmm0
	pxor	xmm5,xmm1
	add	r8d,DWORD[rsp]
	add	r9d,DWORD[4+rsp]
	add	r10d,DWORD[8+rsp]
	add	r11d,DWORD[12+rsp]

	movdqu	XMMWORD[r13*1+r14],xmm2
	movdqu	XMMWORD[16+r13*1+r14],xmm3
	movdqu	XMMWORD[32+r13*1+r14],xmm4
	movdqu	XMMWORD[48+r13*1+r14],xmm5
	lea	r15,[64+r15]
	lea	r13,[64+r13]
	cmp	r15,QWORD[16+rsp]
	jb	NEAR $L$oop

	mov	r12,QWORD[24+rsp]
	sub	cl,al
	mov	DWORD[r12],r8d
	mov	DWORD[4+r12],r9d
	mov	DWORD[8+r12],r10d
	mov	DWORD[12+r12],r11d
	sub	bpl,1
	mov	DWORD[((-8))+rdi],ebp
	mov	DWORD[((-4))+rdi],ecx

	mov	r15,QWORD[40+rsp]

	mov	r14,QWORD[48+rsp]

	mov	r13,QWORD[56+rsp]

	mov	r12,QWORD[64+rsp]

	mov	rbp,QWORD[72+rsp]

	mov	rbx,QWORD[80+rsp]

	lea	rsp,[88+rsp]

$L$abort:
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rc4_md5_enc
$L$SEH_end_rc4_md5_enc:
