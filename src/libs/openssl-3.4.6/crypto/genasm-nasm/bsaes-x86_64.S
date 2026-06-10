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


EXTERN	asm_AES_encrypt
EXTERN	asm_AES_decrypt


ALIGN	64
SEH64_PROC_FRAME _bsaes_encrypt8,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	r11,[$L$BS0]

	movdqa	xmm8,XMMWORD[rax]
	lea	rax,[16+rax]
	movdqa	xmm7,XMMWORD[80+r11]
	pxor	xmm15,xmm8
	pxor	xmm0,xmm8
	pxor	xmm1,xmm8
	pxor	xmm2,xmm8
DB	102,68,15,56,0,255
DB	102,15,56,0,199
	pxor	xmm3,xmm8
	pxor	xmm4,xmm8
DB	102,15,56,0,207
DB	102,15,56,0,215
	pxor	xmm5,xmm8
	pxor	xmm6,xmm8
DB	102,15,56,0,223
DB	102,15,56,0,231
DB	102,15,56,0,239
DB	102,15,56,0,247
_bsaes_encrypt8_bitslice:
	movdqa	xmm7,XMMWORD[r11]
	movdqa	xmm8,XMMWORD[16+r11]
	movdqa	xmm9,xmm5
	psrlq	xmm5,1
	movdqa	xmm10,xmm3
	psrlq	xmm3,1
	pxor	xmm5,xmm6
	pxor	xmm3,xmm4
	pand	xmm5,xmm7
	pand	xmm3,xmm7
	pxor	xmm6,xmm5
	psllq	xmm5,1
	pxor	xmm4,xmm3
	psllq	xmm3,1
	pxor	xmm5,xmm9
	pxor	xmm3,xmm10
	movdqa	xmm9,xmm1
	psrlq	xmm1,1
	movdqa	xmm10,xmm15
	psrlq	xmm15,1
	pxor	xmm1,xmm2
	pxor	xmm15,xmm0
	pand	xmm1,xmm7
	pand	xmm15,xmm7
	pxor	xmm2,xmm1
	psllq	xmm1,1
	pxor	xmm0,xmm15
	psllq	xmm15,1
	pxor	xmm1,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm7,XMMWORD[32+r11]
	movdqa	xmm9,xmm4
	psrlq	xmm4,2
	movdqa	xmm10,xmm3
	psrlq	xmm3,2
	pxor	xmm4,xmm6
	pxor	xmm3,xmm5
	pand	xmm4,xmm8
	pand	xmm3,xmm8
	pxor	xmm6,xmm4
	psllq	xmm4,2
	pxor	xmm5,xmm3
	psllq	xmm3,2
	pxor	xmm4,xmm9
	pxor	xmm3,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,2
	movdqa	xmm10,xmm15
	psrlq	xmm15,2
	pxor	xmm0,xmm2
	pxor	xmm15,xmm1
	pand	xmm0,xmm8
	pand	xmm15,xmm8
	pxor	xmm2,xmm0
	psllq	xmm0,2
	pxor	xmm1,xmm15
	psllq	xmm15,2
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm9,xmm2
	psrlq	xmm2,4
	movdqa	xmm10,xmm1
	psrlq	xmm1,4
	pxor	xmm2,xmm6
	pxor	xmm1,xmm5
	pand	xmm2,xmm7
	pand	xmm1,xmm7
	pxor	xmm6,xmm2
	psllq	xmm2,4
	pxor	xmm5,xmm1
	psllq	xmm1,4
	pxor	xmm2,xmm9
	pxor	xmm1,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,4
	movdqa	xmm10,xmm15
	psrlq	xmm15,4
	pxor	xmm0,xmm4
	pxor	xmm15,xmm3
	pand	xmm0,xmm7
	pand	xmm15,xmm7
	pxor	xmm4,xmm0
	psllq	xmm0,4
	pxor	xmm3,xmm15
	psllq	xmm15,4
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	dec	r10d
	jmp	NEAR $L$enc_sbox
ALIGN	16
$L$enc_loop:
	pxor	xmm15,XMMWORD[rax]
	pxor	xmm0,XMMWORD[16+rax]
	pxor	xmm1,XMMWORD[32+rax]
	pxor	xmm2,XMMWORD[48+rax]
DB	102,68,15,56,0,255
DB	102,15,56,0,199
	pxor	xmm3,XMMWORD[64+rax]
	pxor	xmm4,XMMWORD[80+rax]
DB	102,15,56,0,207
DB	102,15,56,0,215
	pxor	xmm5,XMMWORD[96+rax]
	pxor	xmm6,XMMWORD[112+rax]
DB	102,15,56,0,223
DB	102,15,56,0,231
DB	102,15,56,0,239
DB	102,15,56,0,247
	lea	rax,[128+rax]
$L$enc_sbox:
	pxor	xmm4,xmm5
	pxor	xmm1,xmm0
	pxor	xmm2,xmm15
	pxor	xmm5,xmm1
	pxor	xmm4,xmm15

	pxor	xmm5,xmm2
	pxor	xmm2,xmm6
	pxor	xmm6,xmm4
	pxor	xmm2,xmm3
	pxor	xmm3,xmm4
	pxor	xmm2,xmm0

	pxor	xmm1,xmm6
	pxor	xmm0,xmm4
	movdqa	xmm10,xmm6
	movdqa	xmm9,xmm0
	movdqa	xmm8,xmm4
	movdqa	xmm12,xmm1
	movdqa	xmm11,xmm5

	pxor	xmm10,xmm3
	pxor	xmm9,xmm1
	pxor	xmm8,xmm2
	movdqa	xmm13,xmm10
	pxor	xmm12,xmm3
	movdqa	xmm7,xmm9
	pxor	xmm11,xmm15
	movdqa	xmm14,xmm10

	por	xmm9,xmm8
	por	xmm10,xmm11
	pxor	xmm14,xmm7
	pand	xmm13,xmm11
	pxor	xmm11,xmm8
	pand	xmm7,xmm8
	pand	xmm14,xmm11
	movdqa	xmm11,xmm2
	pxor	xmm11,xmm15
	pand	xmm12,xmm11
	pxor	xmm10,xmm12
	pxor	xmm9,xmm12
	movdqa	xmm12,xmm6
	movdqa	xmm11,xmm4
	pxor	xmm12,xmm0
	pxor	xmm11,xmm5
	movdqa	xmm8,xmm12
	pand	xmm12,xmm11
	por	xmm8,xmm11
	pxor	xmm7,xmm12
	pxor	xmm10,xmm14
	pxor	xmm9,xmm13
	pxor	xmm8,xmm14
	movdqa	xmm11,xmm1
	pxor	xmm7,xmm13
	movdqa	xmm12,xmm3
	pxor	xmm8,xmm13
	movdqa	xmm13,xmm0
	pand	xmm11,xmm2
	movdqa	xmm14,xmm6
	pand	xmm12,xmm15
	pand	xmm13,xmm4
	por	xmm14,xmm5
	pxor	xmm10,xmm11
	pxor	xmm9,xmm12
	pxor	xmm8,xmm13
	pxor	xmm7,xmm14





	movdqa	xmm11,xmm10
	pand	xmm10,xmm8
	pxor	xmm11,xmm9

	movdqa	xmm13,xmm7
	movdqa	xmm14,xmm11
	pxor	xmm13,xmm10
	pand	xmm14,xmm13

	movdqa	xmm12,xmm8
	pxor	xmm14,xmm9
	pxor	xmm12,xmm7

	pxor	xmm10,xmm9

	pand	xmm12,xmm10

	movdqa	xmm9,xmm13
	pxor	xmm12,xmm7

	pxor	xmm9,xmm12
	pxor	xmm8,xmm12

	pand	xmm9,xmm7

	pxor	xmm13,xmm9
	pxor	xmm8,xmm9

	pand	xmm13,xmm14

	pxor	xmm13,xmm11
	movdqa	xmm11,xmm5
	movdqa	xmm7,xmm4
	movdqa	xmm9,xmm14
	pxor	xmm9,xmm13
	pand	xmm9,xmm5
	pxor	xmm5,xmm4
	pand	xmm4,xmm14
	pand	xmm5,xmm13
	pxor	xmm5,xmm4
	pxor	xmm4,xmm9
	pxor	xmm11,xmm15
	pxor	xmm7,xmm2
	pxor	xmm14,xmm12
	pxor	xmm13,xmm8
	movdqa	xmm10,xmm14
	movdqa	xmm9,xmm12
	pxor	xmm10,xmm13
	pxor	xmm9,xmm8
	pand	xmm10,xmm11
	pand	xmm9,xmm15
	pxor	xmm11,xmm7
	pxor	xmm15,xmm2
	pand	xmm7,xmm14
	pand	xmm2,xmm12
	pand	xmm11,xmm13
	pand	xmm15,xmm8
	pxor	xmm7,xmm11
	pxor	xmm15,xmm2
	pxor	xmm11,xmm10
	pxor	xmm2,xmm9
	pxor	xmm5,xmm11
	pxor	xmm15,xmm11
	pxor	xmm4,xmm7
	pxor	xmm2,xmm7

	movdqa	xmm11,xmm6
	movdqa	xmm7,xmm0
	pxor	xmm11,xmm3
	pxor	xmm7,xmm1
	movdqa	xmm10,xmm14
	movdqa	xmm9,xmm12
	pxor	xmm10,xmm13
	pxor	xmm9,xmm8
	pand	xmm10,xmm11
	pand	xmm9,xmm3
	pxor	xmm11,xmm7
	pxor	xmm3,xmm1
	pand	xmm7,xmm14
	pand	xmm1,xmm12
	pand	xmm11,xmm13
	pand	xmm3,xmm8
	pxor	xmm7,xmm11
	pxor	xmm3,xmm1
	pxor	xmm11,xmm10
	pxor	xmm1,xmm9
	pxor	xmm14,xmm12
	pxor	xmm13,xmm8
	movdqa	xmm10,xmm14
	pxor	xmm10,xmm13
	pand	xmm10,xmm6
	pxor	xmm6,xmm0
	pand	xmm0,xmm14
	pand	xmm6,xmm13
	pxor	xmm6,xmm0
	pxor	xmm0,xmm10
	pxor	xmm6,xmm11
	pxor	xmm3,xmm11
	pxor	xmm0,xmm7
	pxor	xmm1,xmm7
	pxor	xmm6,xmm15
	pxor	xmm0,xmm5
	pxor	xmm3,xmm6
	pxor	xmm5,xmm15
	pxor	xmm15,xmm0

	pxor	xmm0,xmm4
	pxor	xmm4,xmm1
	pxor	xmm1,xmm2
	pxor	xmm2,xmm4
	pxor	xmm3,xmm4

	pxor	xmm5,xmm2
	dec	r10d
	jl	NEAR $L$enc_done
	pshufd	xmm7,xmm15,0x93
	pshufd	xmm8,xmm0,0x93
	pxor	xmm15,xmm7
	pshufd	xmm9,xmm3,0x93
	pxor	xmm0,xmm8
	pshufd	xmm10,xmm5,0x93
	pxor	xmm3,xmm9
	pshufd	xmm11,xmm2,0x93
	pxor	xmm5,xmm10
	pshufd	xmm12,xmm6,0x93
	pxor	xmm2,xmm11
	pshufd	xmm13,xmm1,0x93
	pxor	xmm6,xmm12
	pshufd	xmm14,xmm4,0x93
	pxor	xmm1,xmm13
	pxor	xmm4,xmm14

	pxor	xmm8,xmm15
	pxor	xmm7,xmm4
	pxor	xmm8,xmm4
	pshufd	xmm15,xmm15,0x4E
	pxor	xmm9,xmm0
	pshufd	xmm0,xmm0,0x4E
	pxor	xmm12,xmm2
	pxor	xmm15,xmm7
	pxor	xmm13,xmm6
	pxor	xmm0,xmm8
	pxor	xmm11,xmm5
	pshufd	xmm7,xmm2,0x4E
	pxor	xmm14,xmm1
	pshufd	xmm8,xmm6,0x4E
	pxor	xmm10,xmm3
	pshufd	xmm2,xmm5,0x4E
	pxor	xmm10,xmm4
	pshufd	xmm6,xmm4,0x4E
	pxor	xmm11,xmm4
	pshufd	xmm5,xmm1,0x4E
	pxor	xmm7,xmm11
	pshufd	xmm1,xmm3,0x4E
	pxor	xmm8,xmm12
	pxor	xmm2,xmm10
	pxor	xmm6,xmm14
	pxor	xmm5,xmm13
	movdqa	xmm3,xmm7
	pxor	xmm1,xmm9
	movdqa	xmm4,xmm8
	movdqa	xmm7,XMMWORD[48+r11]
	jnz	NEAR $L$enc_loop
	movdqa	xmm7,XMMWORD[64+r11]
	jmp	NEAR $L$enc_loop
ALIGN	16
$L$enc_done:
	movdqa	xmm7,XMMWORD[r11]
	movdqa	xmm8,XMMWORD[16+r11]
	movdqa	xmm9,xmm1
	psrlq	xmm1,1
	movdqa	xmm10,xmm2
	psrlq	xmm2,1
	pxor	xmm1,xmm4
	pxor	xmm2,xmm6
	pand	xmm1,xmm7
	pand	xmm2,xmm7
	pxor	xmm4,xmm1
	psllq	xmm1,1
	pxor	xmm6,xmm2
	psllq	xmm2,1
	pxor	xmm1,xmm9
	pxor	xmm2,xmm10
	movdqa	xmm9,xmm3
	psrlq	xmm3,1
	movdqa	xmm10,xmm15
	psrlq	xmm15,1
	pxor	xmm3,xmm5
	pxor	xmm15,xmm0
	pand	xmm3,xmm7
	pand	xmm15,xmm7
	pxor	xmm5,xmm3
	psllq	xmm3,1
	pxor	xmm0,xmm15
	psllq	xmm15,1
	pxor	xmm3,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm7,XMMWORD[32+r11]
	movdqa	xmm9,xmm6
	psrlq	xmm6,2
	movdqa	xmm10,xmm2
	psrlq	xmm2,2
	pxor	xmm6,xmm4
	pxor	xmm2,xmm1
	pand	xmm6,xmm8
	pand	xmm2,xmm8
	pxor	xmm4,xmm6
	psllq	xmm6,2
	pxor	xmm1,xmm2
	psllq	xmm2,2
	pxor	xmm6,xmm9
	pxor	xmm2,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,2
	movdqa	xmm10,xmm15
	psrlq	xmm15,2
	pxor	xmm0,xmm5
	pxor	xmm15,xmm3
	pand	xmm0,xmm8
	pand	xmm15,xmm8
	pxor	xmm5,xmm0
	psllq	xmm0,2
	pxor	xmm3,xmm15
	psllq	xmm15,2
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm9,xmm5
	psrlq	xmm5,4
	movdqa	xmm10,xmm3
	psrlq	xmm3,4
	pxor	xmm5,xmm4
	pxor	xmm3,xmm1
	pand	xmm5,xmm7
	pand	xmm3,xmm7
	pxor	xmm4,xmm5
	psllq	xmm5,4
	pxor	xmm1,xmm3
	psllq	xmm3,4
	pxor	xmm5,xmm9
	pxor	xmm3,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,4
	movdqa	xmm10,xmm15
	psrlq	xmm15,4
	pxor	xmm0,xmm6
	pxor	xmm15,xmm2
	pand	xmm0,xmm7
	pand	xmm15,xmm7
	pxor	xmm6,xmm0
	psllq	xmm0,4
	pxor	xmm2,xmm15
	psllq	xmm15,4
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm7,XMMWORD[rax]
	pxor	xmm3,xmm7
	pxor	xmm5,xmm7
	pxor	xmm2,xmm7
	pxor	xmm6,xmm7
	pxor	xmm1,xmm7
	pxor	xmm4,xmm7
	pxor	xmm15,xmm7
	pxor	xmm0,xmm7
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_bsaes_encrypt8



ALIGN	64
SEH64_PROC_FRAME _bsaes_decrypt8,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	r11,[$L$BS0]

	movdqa	xmm8,XMMWORD[rax]
	lea	rax,[16+rax]
	movdqa	xmm7,XMMWORD[((-48))+r11]
	pxor	xmm15,xmm8
	pxor	xmm0,xmm8
	pxor	xmm1,xmm8
	pxor	xmm2,xmm8
DB	102,68,15,56,0,255
DB	102,15,56,0,199
	pxor	xmm3,xmm8
	pxor	xmm4,xmm8
DB	102,15,56,0,207
DB	102,15,56,0,215
	pxor	xmm5,xmm8
	pxor	xmm6,xmm8
DB	102,15,56,0,223
DB	102,15,56,0,231
DB	102,15,56,0,239
DB	102,15,56,0,247
	movdqa	xmm7,XMMWORD[r11]
	movdqa	xmm8,XMMWORD[16+r11]
	movdqa	xmm9,xmm5
	psrlq	xmm5,1
	movdqa	xmm10,xmm3
	psrlq	xmm3,1
	pxor	xmm5,xmm6
	pxor	xmm3,xmm4
	pand	xmm5,xmm7
	pand	xmm3,xmm7
	pxor	xmm6,xmm5
	psllq	xmm5,1
	pxor	xmm4,xmm3
	psllq	xmm3,1
	pxor	xmm5,xmm9
	pxor	xmm3,xmm10
	movdqa	xmm9,xmm1
	psrlq	xmm1,1
	movdqa	xmm10,xmm15
	psrlq	xmm15,1
	pxor	xmm1,xmm2
	pxor	xmm15,xmm0
	pand	xmm1,xmm7
	pand	xmm15,xmm7
	pxor	xmm2,xmm1
	psllq	xmm1,1
	pxor	xmm0,xmm15
	psllq	xmm15,1
	pxor	xmm1,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm7,XMMWORD[32+r11]
	movdqa	xmm9,xmm4
	psrlq	xmm4,2
	movdqa	xmm10,xmm3
	psrlq	xmm3,2
	pxor	xmm4,xmm6
	pxor	xmm3,xmm5
	pand	xmm4,xmm8
	pand	xmm3,xmm8
	pxor	xmm6,xmm4
	psllq	xmm4,2
	pxor	xmm5,xmm3
	psllq	xmm3,2
	pxor	xmm4,xmm9
	pxor	xmm3,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,2
	movdqa	xmm10,xmm15
	psrlq	xmm15,2
	pxor	xmm0,xmm2
	pxor	xmm15,xmm1
	pand	xmm0,xmm8
	pand	xmm15,xmm8
	pxor	xmm2,xmm0
	psllq	xmm0,2
	pxor	xmm1,xmm15
	psllq	xmm15,2
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm9,xmm2
	psrlq	xmm2,4
	movdqa	xmm10,xmm1
	psrlq	xmm1,4
	pxor	xmm2,xmm6
	pxor	xmm1,xmm5
	pand	xmm2,xmm7
	pand	xmm1,xmm7
	pxor	xmm6,xmm2
	psllq	xmm2,4
	pxor	xmm5,xmm1
	psllq	xmm1,4
	pxor	xmm2,xmm9
	pxor	xmm1,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,4
	movdqa	xmm10,xmm15
	psrlq	xmm15,4
	pxor	xmm0,xmm4
	pxor	xmm15,xmm3
	pand	xmm0,xmm7
	pand	xmm15,xmm7
	pxor	xmm4,xmm0
	psllq	xmm0,4
	pxor	xmm3,xmm15
	psllq	xmm15,4
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	dec	r10d
	jmp	NEAR $L$dec_sbox
ALIGN	16
$L$dec_loop:
	pxor	xmm15,XMMWORD[rax]
	pxor	xmm0,XMMWORD[16+rax]
	pxor	xmm1,XMMWORD[32+rax]
	pxor	xmm2,XMMWORD[48+rax]
DB	102,68,15,56,0,255
DB	102,15,56,0,199
	pxor	xmm3,XMMWORD[64+rax]
	pxor	xmm4,XMMWORD[80+rax]
DB	102,15,56,0,207
DB	102,15,56,0,215
	pxor	xmm5,XMMWORD[96+rax]
	pxor	xmm6,XMMWORD[112+rax]
DB	102,15,56,0,223
DB	102,15,56,0,231
DB	102,15,56,0,239
DB	102,15,56,0,247
	lea	rax,[128+rax]
$L$dec_sbox:
	pxor	xmm2,xmm3

	pxor	xmm3,xmm6
	pxor	xmm1,xmm6
	pxor	xmm5,xmm3
	pxor	xmm6,xmm5
	pxor	xmm0,xmm6

	pxor	xmm15,xmm0
	pxor	xmm1,xmm4
	pxor	xmm2,xmm15
	pxor	xmm4,xmm15
	pxor	xmm0,xmm2
	movdqa	xmm10,xmm2
	movdqa	xmm9,xmm6
	movdqa	xmm8,xmm0
	movdqa	xmm12,xmm3
	movdqa	xmm11,xmm4

	pxor	xmm10,xmm15
	pxor	xmm9,xmm3
	pxor	xmm8,xmm5
	movdqa	xmm13,xmm10
	pxor	xmm12,xmm15
	movdqa	xmm7,xmm9
	pxor	xmm11,xmm1
	movdqa	xmm14,xmm10

	por	xmm9,xmm8
	por	xmm10,xmm11
	pxor	xmm14,xmm7
	pand	xmm13,xmm11
	pxor	xmm11,xmm8
	pand	xmm7,xmm8
	pand	xmm14,xmm11
	movdqa	xmm11,xmm5
	pxor	xmm11,xmm1
	pand	xmm12,xmm11
	pxor	xmm10,xmm12
	pxor	xmm9,xmm12
	movdqa	xmm12,xmm2
	movdqa	xmm11,xmm0
	pxor	xmm12,xmm6
	pxor	xmm11,xmm4
	movdqa	xmm8,xmm12
	pand	xmm12,xmm11
	por	xmm8,xmm11
	pxor	xmm7,xmm12
	pxor	xmm10,xmm14
	pxor	xmm9,xmm13
	pxor	xmm8,xmm14
	movdqa	xmm11,xmm3
	pxor	xmm7,xmm13
	movdqa	xmm12,xmm15
	pxor	xmm8,xmm13
	movdqa	xmm13,xmm6
	pand	xmm11,xmm5
	movdqa	xmm14,xmm2
	pand	xmm12,xmm1
	pand	xmm13,xmm0
	por	xmm14,xmm4
	pxor	xmm10,xmm11
	pxor	xmm9,xmm12
	pxor	xmm8,xmm13
	pxor	xmm7,xmm14





	movdqa	xmm11,xmm10
	pand	xmm10,xmm8
	pxor	xmm11,xmm9

	movdqa	xmm13,xmm7
	movdqa	xmm14,xmm11
	pxor	xmm13,xmm10
	pand	xmm14,xmm13

	movdqa	xmm12,xmm8
	pxor	xmm14,xmm9
	pxor	xmm12,xmm7

	pxor	xmm10,xmm9

	pand	xmm12,xmm10

	movdqa	xmm9,xmm13
	pxor	xmm12,xmm7

	pxor	xmm9,xmm12
	pxor	xmm8,xmm12

	pand	xmm9,xmm7

	pxor	xmm13,xmm9
	pxor	xmm8,xmm9

	pand	xmm13,xmm14

	pxor	xmm13,xmm11
	movdqa	xmm11,xmm4
	movdqa	xmm7,xmm0
	movdqa	xmm9,xmm14
	pxor	xmm9,xmm13
	pand	xmm9,xmm4
	pxor	xmm4,xmm0
	pand	xmm0,xmm14
	pand	xmm4,xmm13
	pxor	xmm4,xmm0
	pxor	xmm0,xmm9
	pxor	xmm11,xmm1
	pxor	xmm7,xmm5
	pxor	xmm14,xmm12
	pxor	xmm13,xmm8
	movdqa	xmm10,xmm14
	movdqa	xmm9,xmm12
	pxor	xmm10,xmm13
	pxor	xmm9,xmm8
	pand	xmm10,xmm11
	pand	xmm9,xmm1
	pxor	xmm11,xmm7
	pxor	xmm1,xmm5
	pand	xmm7,xmm14
	pand	xmm5,xmm12
	pand	xmm11,xmm13
	pand	xmm1,xmm8
	pxor	xmm7,xmm11
	pxor	xmm1,xmm5
	pxor	xmm11,xmm10
	pxor	xmm5,xmm9
	pxor	xmm4,xmm11
	pxor	xmm1,xmm11
	pxor	xmm0,xmm7
	pxor	xmm5,xmm7

	movdqa	xmm11,xmm2
	movdqa	xmm7,xmm6
	pxor	xmm11,xmm15
	pxor	xmm7,xmm3
	movdqa	xmm10,xmm14
	movdqa	xmm9,xmm12
	pxor	xmm10,xmm13
	pxor	xmm9,xmm8
	pand	xmm10,xmm11
	pand	xmm9,xmm15
	pxor	xmm11,xmm7
	pxor	xmm15,xmm3
	pand	xmm7,xmm14
	pand	xmm3,xmm12
	pand	xmm11,xmm13
	pand	xmm15,xmm8
	pxor	xmm7,xmm11
	pxor	xmm15,xmm3
	pxor	xmm11,xmm10
	pxor	xmm3,xmm9
	pxor	xmm14,xmm12
	pxor	xmm13,xmm8
	movdqa	xmm10,xmm14
	pxor	xmm10,xmm13
	pand	xmm10,xmm2
	pxor	xmm2,xmm6
	pand	xmm6,xmm14
	pand	xmm2,xmm13
	pxor	xmm2,xmm6
	pxor	xmm6,xmm10
	pxor	xmm2,xmm11
	pxor	xmm15,xmm11
	pxor	xmm6,xmm7
	pxor	xmm3,xmm7
	pxor	xmm0,xmm6
	pxor	xmm5,xmm4

	pxor	xmm3,xmm0
	pxor	xmm1,xmm6
	pxor	xmm4,xmm6
	pxor	xmm3,xmm1
	pxor	xmm6,xmm15
	pxor	xmm3,xmm4
	pxor	xmm2,xmm5
	pxor	xmm5,xmm0
	pxor	xmm2,xmm3

	pxor	xmm3,xmm15
	pxor	xmm6,xmm2
	dec	r10d
	jl	NEAR $L$dec_done

	pshufd	xmm7,xmm15,0x4E
	pshufd	xmm13,xmm2,0x4E
	pxor	xmm7,xmm15
	pshufd	xmm14,xmm4,0x4E
	pxor	xmm13,xmm2
	pshufd	xmm8,xmm0,0x4E
	pxor	xmm14,xmm4
	pshufd	xmm9,xmm5,0x4E
	pxor	xmm8,xmm0
	pshufd	xmm10,xmm3,0x4E
	pxor	xmm9,xmm5
	pxor	xmm15,xmm13
	pxor	xmm0,xmm13
	pshufd	xmm11,xmm1,0x4E
	pxor	xmm10,xmm3
	pxor	xmm5,xmm7
	pxor	xmm3,xmm8
	pshufd	xmm12,xmm6,0x4E
	pxor	xmm11,xmm1
	pxor	xmm0,xmm14
	pxor	xmm1,xmm9
	pxor	xmm12,xmm6

	pxor	xmm5,xmm14
	pxor	xmm3,xmm13
	pxor	xmm1,xmm13
	pxor	xmm6,xmm10
	pxor	xmm2,xmm11
	pxor	xmm1,xmm14
	pxor	xmm6,xmm14
	pxor	xmm4,xmm12
	pshufd	xmm7,xmm15,0x93
	pshufd	xmm8,xmm0,0x93
	pxor	xmm15,xmm7
	pshufd	xmm9,xmm5,0x93
	pxor	xmm0,xmm8
	pshufd	xmm10,xmm3,0x93
	pxor	xmm5,xmm9
	pshufd	xmm11,xmm1,0x93
	pxor	xmm3,xmm10
	pshufd	xmm12,xmm6,0x93
	pxor	xmm1,xmm11
	pshufd	xmm13,xmm2,0x93
	pxor	xmm6,xmm12
	pshufd	xmm14,xmm4,0x93
	pxor	xmm2,xmm13
	pxor	xmm4,xmm14

	pxor	xmm8,xmm15
	pxor	xmm7,xmm4
	pxor	xmm8,xmm4
	pshufd	xmm15,xmm15,0x4E
	pxor	xmm9,xmm0
	pshufd	xmm0,xmm0,0x4E
	pxor	xmm12,xmm1
	pxor	xmm15,xmm7
	pxor	xmm13,xmm6
	pxor	xmm0,xmm8
	pxor	xmm11,xmm3
	pshufd	xmm7,xmm1,0x4E
	pxor	xmm14,xmm2
	pshufd	xmm8,xmm6,0x4E
	pxor	xmm10,xmm5
	pshufd	xmm1,xmm3,0x4E
	pxor	xmm10,xmm4
	pshufd	xmm6,xmm4,0x4E
	pxor	xmm11,xmm4
	pshufd	xmm3,xmm2,0x4E
	pxor	xmm7,xmm11
	pshufd	xmm2,xmm5,0x4E
	pxor	xmm8,xmm12
	pxor	xmm10,xmm1
	pxor	xmm6,xmm14
	pxor	xmm13,xmm3
	movdqa	xmm3,xmm7
	pxor	xmm2,xmm9
	movdqa	xmm5,xmm13
	movdqa	xmm4,xmm8
	movdqa	xmm1,xmm2
	movdqa	xmm2,xmm10
	movdqa	xmm7,XMMWORD[((-16))+r11]
	jnz	NEAR $L$dec_loop
	movdqa	xmm7,XMMWORD[((-32))+r11]
	jmp	NEAR $L$dec_loop
ALIGN	16
$L$dec_done:
	movdqa	xmm7,XMMWORD[r11]
	movdqa	xmm8,XMMWORD[16+r11]
	movdqa	xmm9,xmm2
	psrlq	xmm2,1
	movdqa	xmm10,xmm1
	psrlq	xmm1,1
	pxor	xmm2,xmm4
	pxor	xmm1,xmm6
	pand	xmm2,xmm7
	pand	xmm1,xmm7
	pxor	xmm4,xmm2
	psllq	xmm2,1
	pxor	xmm6,xmm1
	psllq	xmm1,1
	pxor	xmm2,xmm9
	pxor	xmm1,xmm10
	movdqa	xmm9,xmm5
	psrlq	xmm5,1
	movdqa	xmm10,xmm15
	psrlq	xmm15,1
	pxor	xmm5,xmm3
	pxor	xmm15,xmm0
	pand	xmm5,xmm7
	pand	xmm15,xmm7
	pxor	xmm3,xmm5
	psllq	xmm5,1
	pxor	xmm0,xmm15
	psllq	xmm15,1
	pxor	xmm5,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm7,XMMWORD[32+r11]
	movdqa	xmm9,xmm6
	psrlq	xmm6,2
	movdqa	xmm10,xmm1
	psrlq	xmm1,2
	pxor	xmm6,xmm4
	pxor	xmm1,xmm2
	pand	xmm6,xmm8
	pand	xmm1,xmm8
	pxor	xmm4,xmm6
	psllq	xmm6,2
	pxor	xmm2,xmm1
	psllq	xmm1,2
	pxor	xmm6,xmm9
	pxor	xmm1,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,2
	movdqa	xmm10,xmm15
	psrlq	xmm15,2
	pxor	xmm0,xmm3
	pxor	xmm15,xmm5
	pand	xmm0,xmm8
	pand	xmm15,xmm8
	pxor	xmm3,xmm0
	psllq	xmm0,2
	pxor	xmm5,xmm15
	psllq	xmm15,2
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm9,xmm3
	psrlq	xmm3,4
	movdqa	xmm10,xmm5
	psrlq	xmm5,4
	pxor	xmm3,xmm4
	pxor	xmm5,xmm2
	pand	xmm3,xmm7
	pand	xmm5,xmm7
	pxor	xmm4,xmm3
	psllq	xmm3,4
	pxor	xmm2,xmm5
	psllq	xmm5,4
	pxor	xmm3,xmm9
	pxor	xmm5,xmm10
	movdqa	xmm9,xmm0
	psrlq	xmm0,4
	movdqa	xmm10,xmm15
	psrlq	xmm15,4
	pxor	xmm0,xmm6
	pxor	xmm15,xmm1
	pand	xmm0,xmm7
	pand	xmm15,xmm7
	pxor	xmm6,xmm0
	psllq	xmm0,4
	pxor	xmm1,xmm15
	psllq	xmm15,4
	pxor	xmm0,xmm9
	pxor	xmm15,xmm10
	movdqa	xmm7,XMMWORD[rax]
	pxor	xmm5,xmm7
	pxor	xmm3,xmm7
	pxor	xmm1,xmm7
	pxor	xmm6,xmm7
	pxor	xmm2,xmm7
	pxor	xmm4,xmm7
	pxor	xmm15,xmm7
	pxor	xmm0,xmm7
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_bsaes_decrypt8


ALIGN	16
SEH64_PROC_FRAME _bsaes_key_convert,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	r11,[$L$masks]
	movdqu	xmm7,XMMWORD[rcx]
	lea	rcx,[16+rcx]
	movdqa	xmm0,XMMWORD[r11]
	movdqa	xmm1,XMMWORD[16+r11]
	movdqa	xmm2,XMMWORD[32+r11]
	movdqa	xmm3,XMMWORD[48+r11]
	movdqa	xmm4,XMMWORD[64+r11]
	pcmpeqd	xmm5,xmm5

	movdqu	xmm6,XMMWORD[rcx]
	movdqa	XMMWORD[rax],xmm7
	lea	rax,[16+rax]
	dec	r10d
	jmp	NEAR $L$key_loop
ALIGN	16
$L$key_loop:
DB	102,15,56,0,244

	movdqa	xmm8,xmm0
	movdqa	xmm9,xmm1

	pand	xmm8,xmm6
	pand	xmm9,xmm6
	movdqa	xmm10,xmm2
	pcmpeqb	xmm8,xmm0
	psllq	xmm0,4
	movdqa	xmm11,xmm3
	pcmpeqb	xmm9,xmm1
	psllq	xmm1,4

	pand	xmm10,xmm6
	pand	xmm11,xmm6
	movdqa	xmm12,xmm0
	pcmpeqb	xmm10,xmm2
	psllq	xmm2,4
	movdqa	xmm13,xmm1
	pcmpeqb	xmm11,xmm3
	psllq	xmm3,4

	movdqa	xmm14,xmm2
	movdqa	xmm15,xmm3
	pxor	xmm8,xmm5
	pxor	xmm9,xmm5

	pand	xmm12,xmm6
	pand	xmm13,xmm6
	movdqa	XMMWORD[rax],xmm8
	pcmpeqb	xmm12,xmm0
	psrlq	xmm0,4
	movdqa	XMMWORD[16+rax],xmm9
	pcmpeqb	xmm13,xmm1
	psrlq	xmm1,4
	lea	rcx,[16+rcx]

	pand	xmm14,xmm6
	pand	xmm15,xmm6
	movdqa	XMMWORD[32+rax],xmm10
	pcmpeqb	xmm14,xmm2
	psrlq	xmm2,4
	movdqa	XMMWORD[48+rax],xmm11
	pcmpeqb	xmm15,xmm3
	psrlq	xmm3,4
	movdqu	xmm6,XMMWORD[rcx]

	pxor	xmm13,xmm5
	pxor	xmm14,xmm5
	movdqa	XMMWORD[64+rax],xmm12
	movdqa	XMMWORD[80+rax],xmm13
	movdqa	XMMWORD[96+rax],xmm14
	movdqa	XMMWORD[112+rax],xmm15
	lea	rax,[128+rax]
	dec	r10d
	jnz	NEAR $L$key_loop

	movdqa	xmm7,XMMWORD[80+r11]

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_bsaes_key_convert

EXTERN	asm_AES_cbc_encrypt
global	ossl_bsaes_cbc_encrypt

ALIGN	16
SEH64_PROC_FRAME ossl_bsaes_cbc_encrypt,PUBLIC

DB	243,15,30,250
	mov	r11d,DWORD[48+rsp]
	cmp	r11d,0
	jne	NEAR asm_AES_cbc_encrypt
	cmp	r8,128
	jb	NEAR asm_AES_cbc_encrypt

	mov	rax,rsp
	push	rbp
SEH64_PUSHREG	rbp	; cfa_rsp,cfa_reg_offset => -16,16 cfa_reg=%rsp
	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -24,24 cfa_reg=%rsp
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -32,32 cfa_reg=%rsp
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -40,40 cfa_reg=%rsp
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -48,48 cfa_reg=%rsp
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -56,56 cfa_reg=%rsp
	lea	rsp,[((-72))+rsp]
SEH64_ALLOCSTACK 72
	mov	r10,QWORD[160+rsp]
	lea	rsp,[((-160))+rsp]
SEH64_ALLOCSTACK 160
	movaps	XMMWORD[64+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 64	; .cfi_offset xmm6, -224 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[80+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 80	; .cfi_offset xmm7, -208 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[96+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 96	; .cfi_offset xmm8, -192 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[112+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 112	; .cfi_offset xmm9, -176 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[128+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 128	; .cfi_offset xmm10, -160 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[144+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 144	; .cfi_offset xmm11, -144 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[160+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 160	; .cfi_offset xmm12, -128 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[176+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 176	; .cfi_offset xmm13, -112 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[192+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 192	; .cfi_offset xmm14, -96 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[208+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 208	; .cfi_offset xmm15, -80 cfa_reg_offset=288 cfa_rsp=-288
	mov	rbp,rsp

SEH64_SETFRAME rbp, 0
SEH64_ENDPROLOG	; cfa_reg=%rbp cfa_reg_offset=288 cfa_rsp=-288
	mov	eax,DWORD[240+r9]
	mov	r12,rcx
	mov	r13,rdx
	mov	r14,r8
	mov	r15,r9
	mov	rbx,r10
	shr	r14,4

	mov	edx,eax
	shl	rax,7
	sub	rax,96
	sub	rsp,rax

	mov	rax,rsp
	mov	rcx,r15
	mov	r10d,edx
	call	_bsaes_key_convert
	pxor	xmm7,XMMWORD[rsp]
	movdqa	XMMWORD[rax],xmm6
	movdqa	XMMWORD[rsp],xmm7

	movdqu	xmm14,XMMWORD[rbx]
	sub	r14,8
$L$cbc_dec_loop:
	movdqu	xmm15,XMMWORD[r12]
	movdqu	xmm0,XMMWORD[16+r12]
	movdqu	xmm1,XMMWORD[32+r12]
	movdqu	xmm2,XMMWORD[48+r12]
	movdqu	xmm3,XMMWORD[64+r12]
	movdqu	xmm4,XMMWORD[80+r12]
	mov	rax,rsp
	movdqu	xmm5,XMMWORD[96+r12]
	mov	r10d,edx
	movdqu	xmm6,XMMWORD[112+r12]
	movdqa	XMMWORD[32+rbp],xmm14

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[32+rbp]
	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm0,xmm7
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm5,xmm8
	movdqu	xmm10,XMMWORD[48+r12]
	pxor	xmm3,xmm9
	movdqu	xmm11,XMMWORD[64+r12]
	pxor	xmm1,xmm10
	movdqu	xmm12,XMMWORD[80+r12]
	pxor	xmm6,xmm11
	movdqu	xmm13,XMMWORD[96+r12]
	pxor	xmm2,xmm12
	movdqu	xmm14,XMMWORD[112+r12]
	pxor	xmm4,xmm13
	movdqu	XMMWORD[r13],xmm15
	lea	r12,[128+r12]
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm5
	movdqu	XMMWORD[48+r13],xmm3
	movdqu	XMMWORD[64+r13],xmm1
	movdqu	XMMWORD[80+r13],xmm6
	movdqu	XMMWORD[96+r13],xmm2
	movdqu	XMMWORD[112+r13],xmm4
	lea	r13,[128+r13]
	sub	r14,8
	jnc	NEAR $L$cbc_dec_loop

	add	r14,8
	jz	NEAR $L$cbc_dec_done

	movdqu	xmm15,XMMWORD[r12]
	mov	rax,rsp
	mov	r10d,edx
	cmp	r14,2
	jb	NEAR $L$cbc_dec_one
	movdqu	xmm0,XMMWORD[16+r12]
	je	NEAR $L$cbc_dec_two
	movdqu	xmm1,XMMWORD[32+r12]
	cmp	r14,4
	jb	NEAR $L$cbc_dec_three
	movdqu	xmm2,XMMWORD[48+r12]
	je	NEAR $L$cbc_dec_four
	movdqu	xmm3,XMMWORD[64+r12]
	cmp	r14,6
	jb	NEAR $L$cbc_dec_five
	movdqu	xmm4,XMMWORD[80+r12]
	je	NEAR $L$cbc_dec_six
	movdqu	xmm5,XMMWORD[96+r12]
	movdqa	XMMWORD[32+rbp],xmm14
	call	_bsaes_decrypt8
	pxor	xmm15,XMMWORD[32+rbp]
	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm0,xmm7
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm5,xmm8
	movdqu	xmm10,XMMWORD[48+r12]
	pxor	xmm3,xmm9
	movdqu	xmm11,XMMWORD[64+r12]
	pxor	xmm1,xmm10
	movdqu	xmm12,XMMWORD[80+r12]
	pxor	xmm6,xmm11
	movdqu	xmm14,XMMWORD[96+r12]
	pxor	xmm2,xmm12
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm5
	movdqu	XMMWORD[48+r13],xmm3
	movdqu	XMMWORD[64+r13],xmm1
	movdqu	XMMWORD[80+r13],xmm6
	movdqu	XMMWORD[96+r13],xmm2
	jmp	NEAR $L$cbc_dec_done
ALIGN	16
$L$cbc_dec_six:
	movdqa	XMMWORD[32+rbp],xmm14
	call	_bsaes_decrypt8
	pxor	xmm15,XMMWORD[32+rbp]
	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm0,xmm7
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm5,xmm8
	movdqu	xmm10,XMMWORD[48+r12]
	pxor	xmm3,xmm9
	movdqu	xmm11,XMMWORD[64+r12]
	pxor	xmm1,xmm10
	movdqu	xmm14,XMMWORD[80+r12]
	pxor	xmm6,xmm11
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm5
	movdqu	XMMWORD[48+r13],xmm3
	movdqu	XMMWORD[64+r13],xmm1
	movdqu	XMMWORD[80+r13],xmm6
	jmp	NEAR $L$cbc_dec_done
ALIGN	16
$L$cbc_dec_five:
	movdqa	XMMWORD[32+rbp],xmm14
	call	_bsaes_decrypt8
	pxor	xmm15,XMMWORD[32+rbp]
	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm0,xmm7
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm5,xmm8
	movdqu	xmm10,XMMWORD[48+r12]
	pxor	xmm3,xmm9
	movdqu	xmm14,XMMWORD[64+r12]
	pxor	xmm1,xmm10
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm5
	movdqu	XMMWORD[48+r13],xmm3
	movdqu	XMMWORD[64+r13],xmm1
	jmp	NEAR $L$cbc_dec_done
ALIGN	16
$L$cbc_dec_four:
	movdqa	XMMWORD[32+rbp],xmm14
	call	_bsaes_decrypt8
	pxor	xmm15,XMMWORD[32+rbp]
	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm0,xmm7
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm5,xmm8
	movdqu	xmm14,XMMWORD[48+r12]
	pxor	xmm3,xmm9
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm5
	movdqu	XMMWORD[48+r13],xmm3
	jmp	NEAR $L$cbc_dec_done
ALIGN	16
$L$cbc_dec_three:
	movdqa	XMMWORD[32+rbp],xmm14
	call	_bsaes_decrypt8
	pxor	xmm15,XMMWORD[32+rbp]
	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm0,xmm7
	movdqu	xmm14,XMMWORD[32+r12]
	pxor	xmm5,xmm8
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm5
	jmp	NEAR $L$cbc_dec_done
ALIGN	16
$L$cbc_dec_two:
	movdqa	XMMWORD[32+rbp],xmm14
	call	_bsaes_decrypt8
	pxor	xmm15,XMMWORD[32+rbp]
	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm14,XMMWORD[16+r12]
	pxor	xmm0,xmm7
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	jmp	NEAR $L$cbc_dec_done
ALIGN	16
$L$cbc_dec_one:
	lea	rcx,[r12]
	lea	rdx,[32+rbp]
	lea	r8,[r15]
	call	asm_AES_decrypt
	pxor	xmm14,XMMWORD[32+rbp]
	movdqu	XMMWORD[r13],xmm14
	movdqa	xmm14,xmm15

$L$cbc_dec_done:
	movdqu	XMMWORD[rbx],xmm14
	lea	rax,[rsp]
	pxor	xmm0,xmm0
$L$cbc_dec_bzero:
	movdqa	XMMWORD[rax],xmm0
	movdqa	XMMWORD[16+rax],xmm0
	lea	rax,[32+rax]
	cmp	rbp,rax
	ja	NEAR $L$cbc_dec_bzero

	lea	rax,[120+rbp]

	movaps	xmm6,XMMWORD[64+rbp]
	movaps	xmm7,XMMWORD[80+rbp]
	movaps	xmm8,XMMWORD[96+rbp]
	movaps	xmm9,XMMWORD[112+rbp]
	movaps	xmm10,XMMWORD[128+rbp]
	movaps	xmm11,XMMWORD[144+rbp]
	movaps	xmm12,XMMWORD[160+rbp]
	movaps	xmm13,XMMWORD[176+rbp]
	movaps	xmm14,XMMWORD[192+rbp]
	movaps	xmm15,XMMWORD[208+rbp]
	lea	rax,[160+rax]
$L$cbc_dec_tail:
	mov	r15,QWORD[((-48))+rax]

	mov	r14,QWORD[((-40))+rax]

	mov	r13,QWORD[((-32))+rax]

	mov	r12,QWORD[((-24))+rax]

	mov	rbx,QWORD[((-16))+rax]

	mov	rbp,QWORD[((-8))+rax]

	lea	rsp,[rax]

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	ossl_bsaes_cbc_encrypt


global	ossl_bsaes_ctr32_encrypt_blocks

ALIGN	16
SEH64_PROC_FRAME ossl_bsaes_ctr32_encrypt_blocks,PUBLIC

DB	243,15,30,250
	mov	rax,rsp
	push	rbp
SEH64_PUSHREG	rbp	; cfa_rsp,cfa_reg_offset => -16,16 cfa_reg=%rsp
	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -24,24 cfa_reg=%rsp
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -32,32 cfa_reg=%rsp
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -40,40 cfa_reg=%rsp
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -48,48 cfa_reg=%rsp
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -56,56 cfa_reg=%rsp
	lea	rsp,[((-72))+rsp]
SEH64_ALLOCSTACK 72
	mov	r10,QWORD[160+rsp]
	lea	rsp,[((-160))+rsp]
SEH64_ALLOCSTACK 160
	movaps	XMMWORD[64+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 64	; .cfi_offset xmm6, -224 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[80+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 80	; .cfi_offset xmm7, -208 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[96+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 96	; .cfi_offset xmm8, -192 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[112+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 112	; .cfi_offset xmm9, -176 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[128+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 128	; .cfi_offset xmm10, -160 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[144+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 144	; .cfi_offset xmm11, -144 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[160+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 160	; .cfi_offset xmm12, -128 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[176+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 176	; .cfi_offset xmm13, -112 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[192+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 192	; .cfi_offset xmm14, -96 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[208+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 208	; .cfi_offset xmm15, -80 cfa_reg_offset=288 cfa_rsp=-288
	mov	rbp,rsp

SEH64_SETFRAME rbp, 0
SEH64_ENDPROLOG	; cfa_reg=%rbp cfa_reg_offset=288 cfa_rsp=-288
	movdqu	xmm0,XMMWORD[r10]
	mov	eax,DWORD[240+r9]
	mov	r12,rcx
	mov	r13,rdx
	mov	r14,r8
	mov	r15,r9
	movdqa	XMMWORD[32+rbp],xmm0
	cmp	r8,8
	jb	NEAR $L$ctr_enc_short

	mov	ebx,eax
	shl	rax,7
	sub	rax,96
	sub	rsp,rax

	mov	rax,rsp
	mov	rcx,r15
	mov	r10d,ebx
	call	_bsaes_key_convert
	pxor	xmm7,xmm6
	movdqa	XMMWORD[rax],xmm7

	movdqa	xmm8,XMMWORD[rsp]
	lea	r11,[$L$ADD1]
	movdqa	xmm15,XMMWORD[32+rbp]
	movdqa	xmm7,XMMWORD[((-32))+r11]
DB	102,68,15,56,0,199
DB	102,68,15,56,0,255
	movdqa	XMMWORD[rsp],xmm8
	jmp	NEAR $L$ctr_enc_loop
ALIGN	16
$L$ctr_enc_loop:
	movdqa	XMMWORD[32+rbp],xmm15
	movdqa	xmm0,xmm15
	movdqa	xmm1,xmm15
	paddd	xmm0,XMMWORD[r11]
	movdqa	xmm2,xmm15
	paddd	xmm1,XMMWORD[16+r11]
	movdqa	xmm3,xmm15
	paddd	xmm2,XMMWORD[32+r11]
	movdqa	xmm4,xmm15
	paddd	xmm3,XMMWORD[48+r11]
	movdqa	xmm5,xmm15
	paddd	xmm4,XMMWORD[64+r11]
	movdqa	xmm6,xmm15
	paddd	xmm5,XMMWORD[80+r11]
	paddd	xmm6,XMMWORD[96+r11]



	movdqa	xmm8,XMMWORD[rsp]
	lea	rax,[16+rsp]
	movdqa	xmm7,XMMWORD[((-16))+r11]
	pxor	xmm15,xmm8
	pxor	xmm0,xmm8
	pxor	xmm1,xmm8
	pxor	xmm2,xmm8
DB	102,68,15,56,0,255
DB	102,15,56,0,199
	pxor	xmm3,xmm8
	pxor	xmm4,xmm8
DB	102,15,56,0,207
DB	102,15,56,0,215
	pxor	xmm5,xmm8
	pxor	xmm6,xmm8
DB	102,15,56,0,223
DB	102,15,56,0,231
DB	102,15,56,0,239
DB	102,15,56,0,247
	lea	r11,[$L$BS0]
	mov	r10d,ebx

	call	_bsaes_encrypt8_bitslice

	sub	r14,8
	jc	NEAR $L$ctr_enc_loop_done

	movdqu	xmm7,XMMWORD[r12]
	movdqu	xmm8,XMMWORD[16+r12]
	movdqu	xmm9,XMMWORD[32+r12]
	movdqu	xmm10,XMMWORD[48+r12]
	movdqu	xmm11,XMMWORD[64+r12]
	movdqu	xmm12,XMMWORD[80+r12]
	movdqu	xmm13,XMMWORD[96+r12]
	movdqu	xmm14,XMMWORD[112+r12]
	lea	r12,[128+r12]
	pxor	xmm7,xmm15
	movdqa	xmm15,XMMWORD[32+rbp]
	pxor	xmm0,xmm8
	movdqu	XMMWORD[r13],xmm7
	pxor	xmm3,xmm9
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm5,xmm10
	movdqu	XMMWORD[32+r13],xmm3
	pxor	xmm2,xmm11
	movdqu	XMMWORD[48+r13],xmm5
	pxor	xmm6,xmm12
	movdqu	XMMWORD[64+r13],xmm2
	pxor	xmm1,xmm13
	movdqu	XMMWORD[80+r13],xmm6
	pxor	xmm4,xmm14
	movdqu	XMMWORD[96+r13],xmm1
	lea	r11,[$L$ADD1]
	movdqu	XMMWORD[112+r13],xmm4
	lea	r13,[128+r13]
	paddd	xmm15,XMMWORD[112+r11]
	jnz	NEAR $L$ctr_enc_loop

	jmp	NEAR $L$ctr_enc_done
ALIGN	16
$L$ctr_enc_loop_done:
	add	r14,8
	movdqu	xmm7,XMMWORD[r12]
	pxor	xmm15,xmm7
	movdqu	XMMWORD[r13],xmm15
	cmp	r14,2
	jb	NEAR $L$ctr_enc_done
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm0,xmm8
	movdqu	XMMWORD[16+r13],xmm0
	je	NEAR $L$ctr_enc_done
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm3,xmm9
	movdqu	XMMWORD[32+r13],xmm3
	cmp	r14,4
	jb	NEAR $L$ctr_enc_done
	movdqu	xmm10,XMMWORD[48+r12]
	pxor	xmm5,xmm10
	movdqu	XMMWORD[48+r13],xmm5
	je	NEAR $L$ctr_enc_done
	movdqu	xmm11,XMMWORD[64+r12]
	pxor	xmm2,xmm11
	movdqu	XMMWORD[64+r13],xmm2
	cmp	r14,6
	jb	NEAR $L$ctr_enc_done
	movdqu	xmm12,XMMWORD[80+r12]
	pxor	xmm6,xmm12
	movdqu	XMMWORD[80+r13],xmm6
	je	NEAR $L$ctr_enc_done
	movdqu	xmm13,XMMWORD[96+r12]
	pxor	xmm1,xmm13
	movdqu	XMMWORD[96+r13],xmm1
	jmp	NEAR $L$ctr_enc_done

ALIGN	16
$L$ctr_enc_short:
	lea	rcx,[32+rbp]
	lea	rdx,[48+rbp]
	lea	r8,[r15]
	call	asm_AES_encrypt
	movdqu	xmm0,XMMWORD[r12]
	lea	r12,[16+r12]
	mov	eax,DWORD[44+rbp]
	bswap	eax
	pxor	xmm0,XMMWORD[48+rbp]
	inc	eax
	movdqu	XMMWORD[r13],xmm0
	bswap	eax
	lea	r13,[16+r13]
	mov	DWORD[44+rsp],eax
	dec	r14
	jnz	NEAR $L$ctr_enc_short

$L$ctr_enc_done:
	lea	rax,[rsp]
	pxor	xmm0,xmm0
$L$ctr_enc_bzero:
	movdqa	XMMWORD[rax],xmm0
	movdqa	XMMWORD[16+rax],xmm0
	lea	rax,[32+rax]
	cmp	rbp,rax
	ja	NEAR $L$ctr_enc_bzero

	lea	rax,[120+rbp]

	movaps	xmm6,XMMWORD[64+rbp]
	movaps	xmm7,XMMWORD[80+rbp]
	movaps	xmm8,XMMWORD[96+rbp]
	movaps	xmm9,XMMWORD[112+rbp]
	movaps	xmm10,XMMWORD[128+rbp]
	movaps	xmm11,XMMWORD[144+rbp]
	movaps	xmm12,XMMWORD[160+rbp]
	movaps	xmm13,XMMWORD[176+rbp]
	movaps	xmm14,XMMWORD[192+rbp]
	movaps	xmm15,XMMWORD[208+rbp]
	lea	rax,[160+rax]
$L$ctr_enc_tail:
	mov	r15,QWORD[((-48))+rax]

	mov	r14,QWORD[((-40))+rax]

	mov	r13,QWORD[((-32))+rax]

	mov	r12,QWORD[((-24))+rax]

	mov	rbx,QWORD[((-16))+rax]

	mov	rbp,QWORD[((-8))+rax]

	lea	rsp,[rax]

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	ossl_bsaes_ctr32_encrypt_blocks

global	ossl_bsaes_xts_encrypt

ALIGN	16
SEH64_PROC_FRAME ossl_bsaes_xts_encrypt,PUBLIC

DB	243,15,30,250
	mov	rax,rsp
	push	rbp
SEH64_PUSHREG	rbp	; cfa_rsp,cfa_reg_offset => -16,16 cfa_reg=%rsp
	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -24,24 cfa_reg=%rsp
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -32,32 cfa_reg=%rsp
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -40,40 cfa_reg=%rsp
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -48,48 cfa_reg=%rsp
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -56,56 cfa_reg=%rsp
	lea	rsp,[((-72))+rsp]
SEH64_ALLOCSTACK 72
	mov	r10,QWORD[160+rsp]
	mov	r11,QWORD[168+rsp]
	lea	rsp,[((-160))+rsp]
SEH64_ALLOCSTACK 160
	movaps	XMMWORD[64+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 64	; .cfi_offset xmm6, -224 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[80+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 80	; .cfi_offset xmm7, -208 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[96+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 96	; .cfi_offset xmm8, -192 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[112+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 112	; .cfi_offset xmm9, -176 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[128+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 128	; .cfi_offset xmm10, -160 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[144+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 144	; .cfi_offset xmm11, -144 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[160+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 160	; .cfi_offset xmm12, -128 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[176+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 176	; .cfi_offset xmm13, -112 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[192+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 192	; .cfi_offset xmm14, -96 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[208+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 208	; .cfi_offset xmm15, -80 cfa_reg_offset=288 cfa_rsp=-288
	mov	rbp,rsp

SEH64_SETFRAME rbp, 0
SEH64_ENDPROLOG	; cfa_reg=%rbp cfa_reg_offset=288 cfa_rsp=-288
	mov	r12,rcx
	mov	r13,rdx
	mov	r14,r8
	mov	r15,r9

	lea	rcx,[r11]
	lea	rdx,[32+rbp]
	lea	r8,[r10]
	call	asm_AES_encrypt

	mov	eax,DWORD[240+r15]
	mov	rbx,r14

	mov	edx,eax
	shl	rax,7
	sub	rax,96
	sub	rsp,rax

	mov	rax,rsp
	mov	rcx,r15
	mov	r10d,edx
	call	_bsaes_key_convert
	pxor	xmm7,xmm6
	movdqa	XMMWORD[rax],xmm7

	and	r14,-16
	sub	rsp,0x80
	movdqa	xmm6,XMMWORD[32+rbp]

	pxor	xmm14,xmm14
	movdqa	xmm12,XMMWORD[$L$xts_magic]
	pcmpgtd	xmm14,xmm6

	sub	r14,0x80
	jc	NEAR $L$xts_enc_short
	jmp	NEAR $L$xts_enc_loop

ALIGN	16
$L$xts_enc_loop:
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm15,xmm6
	movdqa	XMMWORD[rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm0,xmm6
	movdqa	XMMWORD[16+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm7,XMMWORD[r12]
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm1,xmm6
	movdqa	XMMWORD[32+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm15,xmm7
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm2,xmm6
	movdqa	XMMWORD[48+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm0,xmm8
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm3,xmm6
	movdqa	XMMWORD[64+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm10,XMMWORD[48+r12]
	pxor	xmm1,xmm9
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm4,xmm6
	movdqa	XMMWORD[80+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm11,XMMWORD[64+r12]
	pxor	xmm2,xmm10
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm5,xmm6
	movdqa	XMMWORD[96+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm12,XMMWORD[80+r12]
	pxor	xmm3,xmm11
	movdqu	xmm13,XMMWORD[96+r12]
	pxor	xmm4,xmm12
	movdqu	xmm14,XMMWORD[112+r12]
	lea	r12,[128+r12]
	movdqa	XMMWORD[112+rsp],xmm6
	pxor	xmm5,xmm13
	lea	rax,[128+rsp]
	pxor	xmm6,xmm14
	mov	r10d,edx

	call	_bsaes_encrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm3,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm5,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm3
	pxor	xmm2,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm5
	pxor	xmm6,XMMWORD[80+rsp]
	movdqu	XMMWORD[64+r13],xmm2
	pxor	xmm1,XMMWORD[96+rsp]
	movdqu	XMMWORD[80+r13],xmm6
	pxor	xmm4,XMMWORD[112+rsp]
	movdqu	XMMWORD[96+r13],xmm1
	movdqu	XMMWORD[112+r13],xmm4
	lea	r13,[128+r13]

	movdqa	xmm6,XMMWORD[112+rsp]
	pxor	xmm14,xmm14
	movdqa	xmm12,XMMWORD[$L$xts_magic]
	pcmpgtd	xmm14,xmm6
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13

	sub	r14,0x80
	jnc	NEAR $L$xts_enc_loop

$L$xts_enc_short:
	add	r14,0x80
	jz	NEAR $L$xts_enc_done
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm15,xmm6
	movdqa	XMMWORD[rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm0,xmm6
	movdqa	XMMWORD[16+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm7,XMMWORD[r12]
	cmp	r14,16
	je	NEAR $L$xts_enc_1
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm1,xmm6
	movdqa	XMMWORD[32+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm8,XMMWORD[16+r12]
	cmp	r14,32
	je	NEAR $L$xts_enc_2
	pxor	xmm15,xmm7
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm2,xmm6
	movdqa	XMMWORD[48+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm9,XMMWORD[32+r12]
	cmp	r14,48
	je	NEAR $L$xts_enc_3
	pxor	xmm0,xmm8
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm3,xmm6
	movdqa	XMMWORD[64+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm10,XMMWORD[48+r12]
	cmp	r14,64
	je	NEAR $L$xts_enc_4
	pxor	xmm1,xmm9
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm4,xmm6
	movdqa	XMMWORD[80+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm11,XMMWORD[64+r12]
	cmp	r14,80
	je	NEAR $L$xts_enc_5
	pxor	xmm2,xmm10
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm5,xmm6
	movdqa	XMMWORD[96+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm12,XMMWORD[80+r12]
	cmp	r14,96
	je	NEAR $L$xts_enc_6
	pxor	xmm3,xmm11
	movdqu	xmm13,XMMWORD[96+r12]
	pxor	xmm4,xmm12
	movdqa	XMMWORD[112+rsp],xmm6
	lea	r12,[112+r12]
	pxor	xmm5,xmm13
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_encrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm3,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm5,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm3
	pxor	xmm2,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm5
	pxor	xmm6,XMMWORD[80+rsp]
	movdqu	XMMWORD[64+r13],xmm2
	pxor	xmm1,XMMWORD[96+rsp]
	movdqu	XMMWORD[80+r13],xmm6
	movdqu	XMMWORD[96+r13],xmm1
	lea	r13,[112+r13]

	movdqa	xmm6,XMMWORD[112+rsp]
	jmp	NEAR $L$xts_enc_done
ALIGN	16
$L$xts_enc_6:
	pxor	xmm3,xmm11
	lea	r12,[96+r12]
	pxor	xmm4,xmm12
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_encrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm3,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm5,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm3
	pxor	xmm2,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm5
	pxor	xmm6,XMMWORD[80+rsp]
	movdqu	XMMWORD[64+r13],xmm2
	movdqu	XMMWORD[80+r13],xmm6
	lea	r13,[96+r13]

	movdqa	xmm6,XMMWORD[96+rsp]
	jmp	NEAR $L$xts_enc_done
ALIGN	16
$L$xts_enc_5:
	pxor	xmm2,xmm10
	lea	r12,[80+r12]
	pxor	xmm3,xmm11
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_encrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm3,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm5,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm3
	pxor	xmm2,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm5
	movdqu	XMMWORD[64+r13],xmm2
	lea	r13,[80+r13]

	movdqa	xmm6,XMMWORD[80+rsp]
	jmp	NEAR $L$xts_enc_done
ALIGN	16
$L$xts_enc_4:
	pxor	xmm1,xmm9
	lea	r12,[64+r12]
	pxor	xmm2,xmm10
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_encrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm3,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm5,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm3
	movdqu	XMMWORD[48+r13],xmm5
	lea	r13,[64+r13]

	movdqa	xmm6,XMMWORD[64+rsp]
	jmp	NEAR $L$xts_enc_done
ALIGN	16
$L$xts_enc_3:
	pxor	xmm0,xmm8
	lea	r12,[48+r12]
	pxor	xmm1,xmm9
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_encrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm3,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm3
	lea	r13,[48+r13]

	movdqa	xmm6,XMMWORD[48+rsp]
	jmp	NEAR $L$xts_enc_done
ALIGN	16
$L$xts_enc_2:
	pxor	xmm15,xmm7
	lea	r12,[32+r12]
	pxor	xmm0,xmm8
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_encrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	lea	r13,[32+r13]

	movdqa	xmm6,XMMWORD[32+rsp]
	jmp	NEAR $L$xts_enc_done
ALIGN	16
$L$xts_enc_1:
	pxor	xmm7,xmm15
	lea	r12,[16+r12]
	movdqa	XMMWORD[32+rbp],xmm7
	lea	rcx,[32+rbp]
	lea	rdx,[32+rbp]
	lea	r8,[r15]
	call	asm_AES_encrypt
	pxor	xmm15,XMMWORD[32+rbp]





	movdqu	XMMWORD[r13],xmm15
	lea	r13,[16+r13]

	movdqa	xmm6,XMMWORD[16+rsp]

$L$xts_enc_done:
	and	ebx,15
	jz	NEAR $L$xts_enc_ret
	mov	rdx,r13

$L$xts_enc_steal:
	movzx	eax,BYTE[r12]
	movzx	ecx,BYTE[((-16))+rdx]
	lea	r12,[1+r12]
	mov	BYTE[((-16))+rdx],al
	mov	BYTE[rdx],cl
	lea	rdx,[1+rdx]
	sub	ebx,1
	jnz	NEAR $L$xts_enc_steal

	movdqu	xmm15,XMMWORD[((-16))+r13]
	lea	rcx,[32+rbp]
	pxor	xmm15,xmm6
	lea	rdx,[32+rbp]
	movdqa	XMMWORD[32+rbp],xmm15
	lea	r8,[r15]
	call	asm_AES_encrypt
	pxor	xmm6,XMMWORD[32+rbp]
	movdqu	XMMWORD[(-16)+r13],xmm6

$L$xts_enc_ret:
	lea	rax,[rsp]
	pxor	xmm0,xmm0
$L$xts_enc_bzero:
	movdqa	XMMWORD[rax],xmm0
	movdqa	XMMWORD[16+rax],xmm0
	lea	rax,[32+rax]
	cmp	rbp,rax
	ja	NEAR $L$xts_enc_bzero

	lea	rax,[120+rbp]

	movaps	xmm6,XMMWORD[64+rbp]
	movaps	xmm7,XMMWORD[80+rbp]
	movaps	xmm8,XMMWORD[96+rbp]
	movaps	xmm9,XMMWORD[112+rbp]
	movaps	xmm10,XMMWORD[128+rbp]
	movaps	xmm11,XMMWORD[144+rbp]
	movaps	xmm12,XMMWORD[160+rbp]
	movaps	xmm13,XMMWORD[176+rbp]
	movaps	xmm14,XMMWORD[192+rbp]
	movaps	xmm15,XMMWORD[208+rbp]
	lea	rax,[160+rax]
$L$xts_enc_tail:
	mov	r15,QWORD[((-48))+rax]

	mov	r14,QWORD[((-40))+rax]

	mov	r13,QWORD[((-32))+rax]

	mov	r12,QWORD[((-24))+rax]

	mov	rbx,QWORD[((-16))+rax]

	mov	rbp,QWORD[((-8))+rax]

	lea	rsp,[rax]

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	ossl_bsaes_xts_encrypt


global	ossl_bsaes_xts_decrypt

ALIGN	16
SEH64_PROC_FRAME ossl_bsaes_xts_decrypt,PUBLIC

DB	243,15,30,250
	mov	rax,rsp
	push	rbp
SEH64_PUSHREG	rbp	; cfa_rsp,cfa_reg_offset => -16,16 cfa_reg=%rsp
	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -24,24 cfa_reg=%rsp
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -32,32 cfa_reg=%rsp
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -40,40 cfa_reg=%rsp
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -48,48 cfa_reg=%rsp
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -56,56 cfa_reg=%rsp
	lea	rsp,[((-72))+rsp]
SEH64_ALLOCSTACK 72
	mov	r10,QWORD[160+rsp]
	mov	r11,QWORD[168+rsp]
	lea	rsp,[((-160))+rsp]
SEH64_ALLOCSTACK 160
	movaps	XMMWORD[64+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 64	; .cfi_offset xmm6, -224 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[80+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 80	; .cfi_offset xmm7, -208 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[96+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 96	; .cfi_offset xmm8, -192 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[112+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 112	; .cfi_offset xmm9, -176 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[128+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 128	; .cfi_offset xmm10, -160 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[144+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 144	; .cfi_offset xmm11, -144 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[160+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 160	; .cfi_offset xmm12, -128 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[176+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 176	; .cfi_offset xmm13, -112 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[192+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 192	; .cfi_offset xmm14, -96 cfa_reg_offset=288 cfa_rsp=-288
	movaps	XMMWORD[208+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 208	; .cfi_offset xmm15, -80 cfa_reg_offset=288 cfa_rsp=-288
	; cfa_rsp=-288
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=288 cfa_rsp=-288
	mov	rbp,rsp
	mov	r12,rcx
	mov	r13,rdx
	mov	r14,r8
	mov	r15,r9

	lea	rcx,[r11]
	lea	rdx,[32+rbp]
	lea	r8,[r10]
	call	asm_AES_encrypt

	mov	eax,DWORD[240+r15]
	mov	rbx,r14

	mov	edx,eax
	shl	rax,7
	sub	rax,96
	sub	rsp,rax

	mov	rax,rsp
	mov	rcx,r15
	mov	r10d,edx
	call	_bsaes_key_convert
	pxor	xmm7,XMMWORD[rsp]
	movdqa	XMMWORD[rax],xmm6
	movdqa	XMMWORD[rsp],xmm7

	xor	eax,eax
	and	r14,-16
	test	ebx,15
	setnz	al
	shl	rax,4
	sub	r14,rax

	sub	rsp,0x80
	movdqa	xmm6,XMMWORD[32+rbp]

	pxor	xmm14,xmm14
	movdqa	xmm12,XMMWORD[$L$xts_magic]
	pcmpgtd	xmm14,xmm6

	sub	r14,0x80
	jc	NEAR $L$xts_dec_short
	jmp	NEAR $L$xts_dec_loop

ALIGN	16
$L$xts_dec_loop:
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm15,xmm6
	movdqa	XMMWORD[rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm0,xmm6
	movdqa	XMMWORD[16+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm7,XMMWORD[r12]
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm1,xmm6
	movdqa	XMMWORD[32+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm8,XMMWORD[16+r12]
	pxor	xmm15,xmm7
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm2,xmm6
	movdqa	XMMWORD[48+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm9,XMMWORD[32+r12]
	pxor	xmm0,xmm8
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm3,xmm6
	movdqa	XMMWORD[64+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm10,XMMWORD[48+r12]
	pxor	xmm1,xmm9
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm4,xmm6
	movdqa	XMMWORD[80+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm11,XMMWORD[64+r12]
	pxor	xmm2,xmm10
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm5,xmm6
	movdqa	XMMWORD[96+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm12,XMMWORD[80+r12]
	pxor	xmm3,xmm11
	movdqu	xmm13,XMMWORD[96+r12]
	pxor	xmm4,xmm12
	movdqu	xmm14,XMMWORD[112+r12]
	lea	r12,[128+r12]
	movdqa	XMMWORD[112+rsp],xmm6
	pxor	xmm5,xmm13
	lea	rax,[128+rsp]
	pxor	xmm6,xmm14
	mov	r10d,edx

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm5,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm3,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm5
	pxor	xmm1,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm3
	pxor	xmm6,XMMWORD[80+rsp]
	movdqu	XMMWORD[64+r13],xmm1
	pxor	xmm2,XMMWORD[96+rsp]
	movdqu	XMMWORD[80+r13],xmm6
	pxor	xmm4,XMMWORD[112+rsp]
	movdqu	XMMWORD[96+r13],xmm2
	movdqu	XMMWORD[112+r13],xmm4
	lea	r13,[128+r13]

	movdqa	xmm6,XMMWORD[112+rsp]
	pxor	xmm14,xmm14
	movdqa	xmm12,XMMWORD[$L$xts_magic]
	pcmpgtd	xmm14,xmm6
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13

	sub	r14,0x80
	jnc	NEAR $L$xts_dec_loop

$L$xts_dec_short:
	add	r14,0x80
	jz	NEAR $L$xts_dec_done
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm15,xmm6
	movdqa	XMMWORD[rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm0,xmm6
	movdqa	XMMWORD[16+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm7,XMMWORD[r12]
	cmp	r14,16
	je	NEAR $L$xts_dec_1
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm1,xmm6
	movdqa	XMMWORD[32+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm8,XMMWORD[16+r12]
	cmp	r14,32
	je	NEAR $L$xts_dec_2
	pxor	xmm15,xmm7
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm2,xmm6
	movdqa	XMMWORD[48+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm9,XMMWORD[32+r12]
	cmp	r14,48
	je	NEAR $L$xts_dec_3
	pxor	xmm0,xmm8
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm3,xmm6
	movdqa	XMMWORD[64+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm10,XMMWORD[48+r12]
	cmp	r14,64
	je	NEAR $L$xts_dec_4
	pxor	xmm1,xmm9
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm4,xmm6
	movdqa	XMMWORD[80+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm11,XMMWORD[64+r12]
	cmp	r14,80
	je	NEAR $L$xts_dec_5
	pxor	xmm2,xmm10
	pshufd	xmm13,xmm14,0x13
	pxor	xmm14,xmm14
	movdqa	xmm5,xmm6
	movdqa	XMMWORD[96+rsp],xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	pcmpgtd	xmm14,xmm6
	pxor	xmm6,xmm13
	movdqu	xmm12,XMMWORD[80+r12]
	cmp	r14,96
	je	NEAR $L$xts_dec_6
	pxor	xmm3,xmm11
	movdqu	xmm13,XMMWORD[96+r12]
	pxor	xmm4,xmm12
	movdqa	XMMWORD[112+rsp],xmm6
	lea	r12,[112+r12]
	pxor	xmm5,xmm13
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm5,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm3,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm5
	pxor	xmm1,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm3
	pxor	xmm6,XMMWORD[80+rsp]
	movdqu	XMMWORD[64+r13],xmm1
	pxor	xmm2,XMMWORD[96+rsp]
	movdqu	XMMWORD[80+r13],xmm6
	movdqu	XMMWORD[96+r13],xmm2
	lea	r13,[112+r13]

	movdqa	xmm6,XMMWORD[112+rsp]
	jmp	NEAR $L$xts_dec_done
ALIGN	16
$L$xts_dec_6:
	pxor	xmm3,xmm11
	lea	r12,[96+r12]
	pxor	xmm4,xmm12
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm5,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm3,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm5
	pxor	xmm1,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm3
	pxor	xmm6,XMMWORD[80+rsp]
	movdqu	XMMWORD[64+r13],xmm1
	movdqu	XMMWORD[80+r13],xmm6
	lea	r13,[96+r13]

	movdqa	xmm6,XMMWORD[96+rsp]
	jmp	NEAR $L$xts_dec_done
ALIGN	16
$L$xts_dec_5:
	pxor	xmm2,xmm10
	lea	r12,[80+r12]
	pxor	xmm3,xmm11
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm5,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm3,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm5
	pxor	xmm1,XMMWORD[64+rsp]
	movdqu	XMMWORD[48+r13],xmm3
	movdqu	XMMWORD[64+r13],xmm1
	lea	r13,[80+r13]

	movdqa	xmm6,XMMWORD[80+rsp]
	jmp	NEAR $L$xts_dec_done
ALIGN	16
$L$xts_dec_4:
	pxor	xmm1,xmm9
	lea	r12,[64+r12]
	pxor	xmm2,xmm10
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm5,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	pxor	xmm3,XMMWORD[48+rsp]
	movdqu	XMMWORD[32+r13],xmm5
	movdqu	XMMWORD[48+r13],xmm3
	lea	r13,[64+r13]

	movdqa	xmm6,XMMWORD[64+rsp]
	jmp	NEAR $L$xts_dec_done
ALIGN	16
$L$xts_dec_3:
	pxor	xmm0,xmm8
	lea	r12,[48+r12]
	pxor	xmm1,xmm9
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	pxor	xmm5,XMMWORD[32+rsp]
	movdqu	XMMWORD[16+r13],xmm0
	movdqu	XMMWORD[32+r13],xmm5
	lea	r13,[48+r13]

	movdqa	xmm6,XMMWORD[48+rsp]
	jmp	NEAR $L$xts_dec_done
ALIGN	16
$L$xts_dec_2:
	pxor	xmm15,xmm7
	lea	r12,[32+r12]
	pxor	xmm0,xmm8
	lea	rax,[128+rsp]
	mov	r10d,edx

	call	_bsaes_decrypt8

	pxor	xmm15,XMMWORD[rsp]
	pxor	xmm0,XMMWORD[16+rsp]
	movdqu	XMMWORD[r13],xmm15
	movdqu	XMMWORD[16+r13],xmm0
	lea	r13,[32+r13]

	movdqa	xmm6,XMMWORD[32+rsp]
	jmp	NEAR $L$xts_dec_done
ALIGN	16
$L$xts_dec_1:
	pxor	xmm7,xmm15
	lea	r12,[16+r12]
	movdqa	XMMWORD[32+rbp],xmm7
	lea	rcx,[32+rbp]
	lea	rdx,[32+rbp]
	lea	r8,[r15]
	call	asm_AES_decrypt
	pxor	xmm15,XMMWORD[32+rbp]





	movdqu	XMMWORD[r13],xmm15
	lea	r13,[16+r13]

	movdqa	xmm6,XMMWORD[16+rsp]

$L$xts_dec_done:
	and	ebx,15
	jz	NEAR $L$xts_dec_ret

	pxor	xmm14,xmm14
	movdqa	xmm12,XMMWORD[$L$xts_magic]
	pcmpgtd	xmm14,xmm6
	pshufd	xmm13,xmm14,0x13
	movdqa	xmm5,xmm6
	paddq	xmm6,xmm6
	pand	xmm13,xmm12
	movdqu	xmm15,XMMWORD[r12]
	pxor	xmm6,xmm13

	lea	rcx,[32+rbp]
	pxor	xmm15,xmm6
	lea	rdx,[32+rbp]
	movdqa	XMMWORD[32+rbp],xmm15
	lea	r8,[r15]
	call	asm_AES_decrypt
	pxor	xmm6,XMMWORD[32+rbp]
	mov	rdx,r13
	movdqu	XMMWORD[r13],xmm6

$L$xts_dec_steal:
	movzx	eax,BYTE[16+r12]
	movzx	ecx,BYTE[rdx]
	lea	r12,[1+r12]
	mov	BYTE[rdx],al
	mov	BYTE[16+rdx],cl
	lea	rdx,[1+rdx]
	sub	ebx,1
	jnz	NEAR $L$xts_dec_steal

	movdqu	xmm15,XMMWORD[r13]
	lea	rcx,[32+rbp]
	pxor	xmm15,xmm5
	lea	rdx,[32+rbp]
	movdqa	XMMWORD[32+rbp],xmm15
	lea	r8,[r15]
	call	asm_AES_decrypt
	pxor	xmm5,XMMWORD[32+rbp]
	movdqu	XMMWORD[r13],xmm5

$L$xts_dec_ret:
	lea	rax,[rsp]
	pxor	xmm0,xmm0
$L$xts_dec_bzero:
	movdqa	XMMWORD[rax],xmm0
	movdqa	XMMWORD[16+rax],xmm0
	lea	rax,[32+rax]
	cmp	rbp,rax
	ja	NEAR $L$xts_dec_bzero

	lea	rax,[120+rbp]

	movaps	xmm6,XMMWORD[64+rbp]
	movaps	xmm7,XMMWORD[80+rbp]
	movaps	xmm8,XMMWORD[96+rbp]
	movaps	xmm9,XMMWORD[112+rbp]
	movaps	xmm10,XMMWORD[128+rbp]
	movaps	xmm11,XMMWORD[144+rbp]
	movaps	xmm12,XMMWORD[160+rbp]
	movaps	xmm13,XMMWORD[176+rbp]
	movaps	xmm14,XMMWORD[192+rbp]
	movaps	xmm15,XMMWORD[208+rbp]
	lea	rax,[160+rax]
$L$xts_dec_tail:
	mov	r15,QWORD[((-48))+rax]

	mov	r14,QWORD[((-40))+rax]

	mov	r13,QWORD[((-32))+rax]

	mov	r12,QWORD[((-24))+rax]

	mov	rbx,QWORD[((-16))+rax]

	mov	rbp,QWORD[((-8))+rax]

	lea	rsp,[rax]

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	ossl_bsaes_xts_decrypt


section	.rdata rdata align=64
ALIGN	64
_bsaes_const:
$L$M0ISR:
	DQ	0x0a0e0206070b0f03,0x0004080c0d010509
$L$ISRM0:
	DQ	0x01040b0e0205080f,0x0306090c00070a0d
$L$ISR:
	DQ	0x0504070602010003,0x0f0e0d0c080b0a09
$L$BS0:
	DQ	0x5555555555555555,0x5555555555555555
$L$BS1:
	DQ	0x3333333333333333,0x3333333333333333
$L$BS2:
	DQ	0x0f0f0f0f0f0f0f0f,0x0f0f0f0f0f0f0f0f
$L$SR:
	DQ	0x0504070600030201,0x0f0e0d0c0a09080b
$L$SRM0:
	DQ	0x0304090e00050a0f,0x01060b0c0207080d
$L$M0SR:
	DQ	0x0a0e02060f03070b,0x0004080c05090d01
$L$SWPUP:
	DQ	0x0706050403020100,0x0c0d0e0f0b0a0908
$L$SWPUPM0SR:
	DQ	0x0a0d02060c03070b,0x0004080f05090e01
$L$ADD1:
	DQ	0x0000000000000000,0x0000000100000000
$L$ADD2:
	DQ	0x0000000000000000,0x0000000200000000
$L$ADD3:
	DQ	0x0000000000000000,0x0000000300000000
$L$ADD4:
	DQ	0x0000000000000000,0x0000000400000000
$L$ADD5:
	DQ	0x0000000000000000,0x0000000500000000
$L$ADD6:
	DQ	0x0000000000000000,0x0000000600000000
$L$ADD7:
	DQ	0x0000000000000000,0x0000000700000000
$L$ADD8:
	DQ	0x0000000000000000,0x0000000800000000
$L$xts_magic:
	DD	0x87,0,1,0
$L$masks:
	DQ	0x0101010101010101,0x0101010101010101
	DQ	0x0202020202020202,0x0202020202020202
	DQ	0x0404040404040404,0x0404040404040404
	DQ	0x0808080808080808,0x0808080808080808
$L$M0:
	DQ	0x02060a0e03070b0f,0x0004080c0105090d
$L$63:
	DQ	0x6363636363636363,0x6363636363636363
ALIGN	64

DB	66,105,116,45,115,108,105,99,101,100,32,65,69,83,32,102
DB	111,114,32,120,56,54,95,54,52,47,83,83,83,69,51,44
DB	32,69,109,105,108,105,97,32,75,195,164,115,112,101,114,44
DB	32,80,101,116,101,114,32,83,99,104,119,97,98,101,44,32
DB	65,110,100,121,32,80,111,108,121,97,107,111,118,0
