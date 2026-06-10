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
SEH64_PROC_FRAME _mul_1x1,PRIVATE

	sub	rsp,128+8
SEH64_ALLOCSTACK 136
	; cfa_rsp=-144
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=144 cfa_rsp=-144
	mov	r9,-1
	lea	rsi,[rax*1+rax]
	shr	r9,3
	lea	rdi,[rax*4]
	and	r9,rax
	lea	r12,[rax*8]
	sar	rax,63
	lea	r10,[r9*1+r9]
	sar	rsi,63
	lea	r11,[r9*4]
	and	rax,rbp
	sar	rdi,63
	mov	rdx,rax
	shl	rax,63
	and	rsi,rbp
	shr	rdx,1
	mov	rcx,rsi
	shl	rsi,62
	and	rdi,rbp
	shr	rcx,2
	xor	rax,rsi
	mov	rbx,rdi
	shl	rdi,61
	xor	rdx,rcx
	shr	rbx,3
	xor	rax,rdi
	xor	rdx,rbx

	mov	r13,r9
	mov	QWORD[rsp],0
	xor	r13,r10
	mov	QWORD[8+rsp],r9
	mov	r14,r11
	mov	QWORD[16+rsp],r10
	xor	r14,r12
	mov	QWORD[24+rsp],r13

	xor	r9,r11
	mov	QWORD[32+rsp],r11
	xor	r10,r11
	mov	QWORD[40+rsp],r9
	xor	r13,r11
	mov	QWORD[48+rsp],r10
	xor	r9,r14
	mov	QWORD[56+rsp],r13
	xor	r10,r14

	mov	QWORD[64+rsp],r12
	xor	r13,r14
	mov	QWORD[72+rsp],r9
	xor	r9,r11
	mov	QWORD[80+rsp],r10
	xor	r10,r11
	mov	QWORD[88+rsp],r13

	xor	r13,r11
	mov	QWORD[96+rsp],r14
	mov	rsi,r8
	mov	QWORD[104+rsp],r9
	and	rsi,rbp
	mov	QWORD[112+rsp],r10
	shr	rbp,4
	mov	QWORD[120+rsp],r13
	mov	rdi,r8
	and	rdi,rbp
	shr	rbp,4

	movq	xmm0,QWORD[rsi*8+rsp]
	mov	rsi,r8
	and	rsi,rbp
	shr	rbp,4
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rdi,r8
	mov	rbx,rcx
	shl	rcx,4
	and	rdi,rbp
	movq	xmm1,QWORD[rsi*8+rsp]
	shr	rbx,60
	xor	rax,rcx
	pslldq	xmm1,1
	mov	rsi,r8
	shr	rbp,4
	xor	rdx,rbx
	and	rsi,rbp
	shr	rbp,4
	pxor	xmm0,xmm1
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rdi,r8
	mov	rbx,rcx
	shl	rcx,12
	and	rdi,rbp
	movq	xmm1,QWORD[rsi*8+rsp]
	shr	rbx,52
	xor	rax,rcx
	pslldq	xmm1,2
	mov	rsi,r8
	shr	rbp,4
	xor	rdx,rbx
	and	rsi,rbp
	shr	rbp,4
	pxor	xmm0,xmm1
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rdi,r8
	mov	rbx,rcx
	shl	rcx,20
	and	rdi,rbp
	movq	xmm1,QWORD[rsi*8+rsp]
	shr	rbx,44
	xor	rax,rcx
	pslldq	xmm1,3
	mov	rsi,r8
	shr	rbp,4
	xor	rdx,rbx
	and	rsi,rbp
	shr	rbp,4
	pxor	xmm0,xmm1
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rdi,r8
	mov	rbx,rcx
	shl	rcx,28
	and	rdi,rbp
	movq	xmm1,QWORD[rsi*8+rsp]
	shr	rbx,36
	xor	rax,rcx
	pslldq	xmm1,4
	mov	rsi,r8
	shr	rbp,4
	xor	rdx,rbx
	and	rsi,rbp
	shr	rbp,4
	pxor	xmm0,xmm1
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rdi,r8
	mov	rbx,rcx
	shl	rcx,36
	and	rdi,rbp
	movq	xmm1,QWORD[rsi*8+rsp]
	shr	rbx,28
	xor	rax,rcx
	pslldq	xmm1,5
	mov	rsi,r8
	shr	rbp,4
	xor	rdx,rbx
	and	rsi,rbp
	shr	rbp,4
	pxor	xmm0,xmm1
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rdi,r8
	mov	rbx,rcx
	shl	rcx,44
	and	rdi,rbp
	movq	xmm1,QWORD[rsi*8+rsp]
	shr	rbx,20
	xor	rax,rcx
	pslldq	xmm1,6
	mov	rsi,r8
	shr	rbp,4
	xor	rdx,rbx
	and	rsi,rbp
	shr	rbp,4
	pxor	xmm0,xmm1
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rdi,r8
	mov	rbx,rcx
	shl	rcx,52
	and	rdi,rbp
	movq	xmm1,QWORD[rsi*8+rsp]
	shr	rbx,12
	xor	rax,rcx
	pslldq	xmm1,7
	mov	rsi,r8
	shr	rbp,4
	xor	rdx,rbx
	and	rsi,rbp
	shr	rbp,4
	pxor	xmm0,xmm1
	mov	rcx,QWORD[rdi*8+rsp]
	mov	rbx,rcx
	shl	rcx,60
DB	102,72,15,126,198
	shr	rbx,4
	xor	rax,rcx
	psrldq	xmm0,8
	xor	rdx,rbx
DB	102,72,15,126,199
	xor	rax,rsi
	xor	rdx,rdi

	add	rsp,128+8

	DB	0F3h,0C3h		;repret
$L$end_mul_1x1:
SEH64_ENDPROC_FRAME	_mul_1x1

EXTERN	OPENSSL_ia32cap_P
global	bn_GF2m_mul_2x2

ALIGN	16
SEH64_PROC_FRAME bn_GF2m_mul_2x2,PUBLIC

	mov	rax,rsp
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r10,QWORD[OPENSSL_ia32cap_P]
	bt	r10,33
	jnc	NEAR _bn_GF2m_mul_2x2_vanilla_shortcut

DB	102,72,15,110,194
DB	102,73,15,110,201
DB	102,73,15,110,208
	movq	xmm3,QWORD[40+rsp]
	movdqa	xmm4,xmm0
	movdqa	xmm5,xmm1
DB	102,15,58,68,193,0
	pxor	xmm4,xmm2
	pxor	xmm5,xmm3
DB	102,15,58,68,211,0
DB	102,15,58,68,229,0
	xorps	xmm4,xmm0
	xorps	xmm4,xmm2
	movdqa	xmm5,xmm4
	pslldq	xmm4,8
	psrldq	xmm5,8
	pxor	xmm2,xmm4
	pxor	xmm0,xmm5
	movdqu	XMMWORD[rcx],xmm2
	movdqu	XMMWORD[16+rcx],xmm0
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	bn_GF2m_mul_2x2



ALIGN	16
SEH64_PROC_FRAME _bn_GF2m_mul_2x2_vanilla,PRIVATE

_bn_GF2m_mul_2x2_vanilla_shortcut:
	lea	rsp,[((-136))+rsp]
SEH64_ALLOCSTACK 136
	mov	r10,QWORD[176+rsp]
SEH64_ALLOCSTACK 176
	mov	QWORD[120+rsp],rdi
SEH64_SAVEREG rdi, 120	; .cfi_offset rdi, -200 cfa_reg_offset=320 cfa_rsp=-320
	mov	QWORD[128+rsp],rsi
SEH64_SAVEREG rsi, 128	; .cfi_offset rsi, -192 cfa_reg_offset=320 cfa_rsp=-320
	mov	QWORD[80+rsp],r14
SEH64_SAVEREG r14, 80	; .cfi_offset r14, -240 cfa_reg_offset=320 cfa_rsp=-320
	mov	QWORD[88+rsp],r13
SEH64_SAVEREG r13, 88	; .cfi_offset r13, -232 cfa_reg_offset=320 cfa_rsp=-320
	mov	QWORD[96+rsp],r12
SEH64_SAVEREG r12, 96	; .cfi_offset r12, -224 cfa_reg_offset=320 cfa_rsp=-320
	mov	QWORD[104+rsp],rbp
SEH64_SAVEREG rbp, 104	; .cfi_offset rbp, -216 cfa_reg_offset=320 cfa_rsp=-320
	mov	QWORD[112+rsp],rbx
SEH64_SAVEREG rbx, 112	; .cfi_offset rbx, -208 cfa_reg_offset=320 cfa_rsp=-320
	; cfa_rsp=-320
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=320 cfa_rsp=-320
	mov	QWORD[32+rsp],rcx
	mov	QWORD[40+rsp],rdx
	mov	QWORD[48+rsp],r8
	mov	QWORD[56+rsp],r9
	mov	QWORD[64+rsp],r10

	mov	r8,0xf
	mov	rax,rdx
	mov	rbp,r9
	call	_mul_1x1
	mov	QWORD[16+rsp],rax
	mov	QWORD[24+rsp],rdx

	mov	rax,QWORD[48+rsp]
	mov	rbp,QWORD[64+rsp]
	call	_mul_1x1
	mov	QWORD[rsp],rax
	mov	QWORD[8+rsp],rdx

	mov	rax,QWORD[40+rsp]
	mov	rbp,QWORD[56+rsp]
	xor	rax,QWORD[48+rsp]
	xor	rbp,QWORD[64+rsp]
	call	_mul_1x1
	mov	rbx,QWORD[rsp]
	mov	rcx,QWORD[8+rsp]
	mov	rdi,QWORD[16+rsp]
	mov	rsi,QWORD[24+rsp]
	mov	rbp,QWORD[32+rsp]

	xor	rax,rdx
	xor	rdx,rcx
	xor	rax,rbx
	mov	QWORD[rbp],rbx
	xor	rdx,rdi
	mov	QWORD[24+rbp],rsi
	xor	rax,rsi
	xor	rdx,rsi
	xor	rax,rdx
	mov	QWORD[16+rbp],rdx
	mov	QWORD[8+rbp],rax

	mov	r14,QWORD[80+rsp]

	mov	r13,QWORD[88+rsp]

	mov	r12,QWORD[96+rsp]

	mov	rbp,QWORD[104+rsp]

	mov	rbx,QWORD[112+rsp]

	mov	rdi,QWORD[120+rsp]
	mov	rsi,QWORD[128+rsp]
	lea	rsp,[136+rsp]

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_bn_GF2m_mul_2x2_vanilla

DB	71,70,40,50,94,109,41,32,77,117,108,116,105,112,108,105
DB	99,97,116,105,111,110,32,102,111,114,32,120,56,54,95,54
DB	52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121
DB	32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46
DB	111,114,103,62,0
ALIGN	16
