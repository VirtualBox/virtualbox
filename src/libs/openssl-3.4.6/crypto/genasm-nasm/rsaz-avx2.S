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


global	rsaz_1024_sqr_avx2

ALIGN	64
SEH64_PROC_FRAME rsaz_1024_sqr_avx2,PUBLIC
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



	lea	rax,[rsp]

	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -16,8 cfa_reg=%rax
	push	rbp
SEH64_PUSHREG	rbp	; cfa_rsp,cfa_reg_offset => -24,8 cfa_reg=%rax
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -32,8 cfa_reg=%rax
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -40,8 cfa_reg=%rax
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -48,8 cfa_reg=%rax
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -56,8 cfa_reg=%rax
	vzeroupper
	lea	rsp,[((-168))+rsp]
SEH64_ALLOCSTACK 168
	vmovaps	XMMWORD[(-216)+rax],xmm6
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -0xd8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-200)+rax],xmm7
SEH64_SAVEXMM128 xmm7, 16	; .cfi_offset xmm7, -0xc8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-184)+rax],xmm8
SEH64_SAVEXMM128 xmm8, 32	; .cfi_offset xmm8, -0xb8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-168)+rax],xmm9
SEH64_SAVEXMM128 xmm9, 48	; .cfi_offset xmm9, -0xa8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-152)+rax],xmm10
SEH64_SAVEXMM128 xmm10, 64	; .cfi_offset xmm10, -0x98-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-136)+rax],xmm11
SEH64_SAVEXMM128 xmm11, 80	; .cfi_offset xmm11, -0x88-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-120)+rax],xmm12
SEH64_SAVEXMM128 xmm12, 96	; .cfi_offset xmm12, -0x78-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-104)+rax],xmm13
SEH64_SAVEXMM128 xmm13, 112	; .cfi_offset xmm13, -0x68-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-88)+rax],xmm14
SEH64_SAVEXMM128 xmm14, 128	; .cfi_offset xmm14, -0x58-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-72)+rax],xmm15
SEH64_SAVEXMM128 xmm15, 144	; .cfi_offset xmm15, -0x48-8 cfa_reg_offset=8 cfa_rsp=-224
	mov	rbp,rax

SEH64_ALLOCSTACK 8
SEH64_SETFRAME rbp, 224
SEH64_ENDPROLOG	; cfa_reg=%rbp cfa_reg_offset=8 cfa_rsp=-224
	mov	r13,rdx
	sub	rsp,832
	mov	r15,r13
	sub	rdi,-128
	sub	rsi,-128
	sub	r13,-128

	and	r15,4095
	add	r15,32*10
	shr	r15,12
	vpxor	ymm9,ymm9,ymm9
	jz	NEAR $L$sqr_1024_no_n_copy





	sub	rsp,32*10
	vmovdqu	ymm0,YMMWORD[((0-128))+r13]
	and	rsp,-2048
	vmovdqu	ymm1,YMMWORD[((32-128))+r13]
	vmovdqu	ymm2,YMMWORD[((64-128))+r13]
	vmovdqu	ymm3,YMMWORD[((96-128))+r13]
	vmovdqu	ymm4,YMMWORD[((128-128))+r13]
	vmovdqu	ymm5,YMMWORD[((160-128))+r13]
	vmovdqu	ymm6,YMMWORD[((192-128))+r13]
	vmovdqu	ymm7,YMMWORD[((224-128))+r13]
	vmovdqu	ymm8,YMMWORD[((256-128))+r13]
	lea	r13,[((832+128))+rsp]
	vmovdqu	YMMWORD[(0-128)+r13],ymm0
	vmovdqu	YMMWORD[(32-128)+r13],ymm1
	vmovdqu	YMMWORD[(64-128)+r13],ymm2
	vmovdqu	YMMWORD[(96-128)+r13],ymm3
	vmovdqu	YMMWORD[(128-128)+r13],ymm4
	vmovdqu	YMMWORD[(160-128)+r13],ymm5
	vmovdqu	YMMWORD[(192-128)+r13],ymm6
	vmovdqu	YMMWORD[(224-128)+r13],ymm7
	vmovdqu	YMMWORD[(256-128)+r13],ymm8
	vmovdqu	YMMWORD[(288-128)+r13],ymm9

$L$sqr_1024_no_n_copy:
	and	rsp,-1024

	vmovdqu	ymm1,YMMWORD[((32-128))+rsi]
	vmovdqu	ymm2,YMMWORD[((64-128))+rsi]
	vmovdqu	ymm3,YMMWORD[((96-128))+rsi]
	vmovdqu	ymm4,YMMWORD[((128-128))+rsi]
	vmovdqu	ymm5,YMMWORD[((160-128))+rsi]
	vmovdqu	ymm6,YMMWORD[((192-128))+rsi]
	vmovdqu	ymm7,YMMWORD[((224-128))+rsi]
	vmovdqu	ymm8,YMMWORD[((256-128))+rsi]

	lea	rbx,[192+rsp]
	vmovdqu	ymm15,YMMWORD[$L$and_mask]
	jmp	NEAR $L$OOP_GRANDE_SQR_1024

ALIGN	32
$L$OOP_GRANDE_SQR_1024:
	lea	r9,[((576+128))+rsp]
	lea	r12,[448+rsp]




	vpaddq	ymm1,ymm1,ymm1
	vpbroadcastq	ymm10,QWORD[((0-128))+rsi]
	vpaddq	ymm2,ymm2,ymm2
	vmovdqa	YMMWORD[(0-128)+r9],ymm1
	vpaddq	ymm3,ymm3,ymm3
	vmovdqa	YMMWORD[(32-128)+r9],ymm2
	vpaddq	ymm4,ymm4,ymm4
	vmovdqa	YMMWORD[(64-128)+r9],ymm3
	vpaddq	ymm5,ymm5,ymm5
	vmovdqa	YMMWORD[(96-128)+r9],ymm4
	vpaddq	ymm6,ymm6,ymm6
	vmovdqa	YMMWORD[(128-128)+r9],ymm5
	vpaddq	ymm7,ymm7,ymm7
	vmovdqa	YMMWORD[(160-128)+r9],ymm6
	vpaddq	ymm8,ymm8,ymm8
	vmovdqa	YMMWORD[(192-128)+r9],ymm7
	vpxor	ymm9,ymm9,ymm9
	vmovdqa	YMMWORD[(224-128)+r9],ymm8

	vpmuludq	ymm0,ymm10,YMMWORD[((0-128))+rsi]
	vpbroadcastq	ymm11,QWORD[((32-128))+rsi]
	vmovdqu	YMMWORD[(288-192)+rbx],ymm9
	vpmuludq	ymm1,ymm1,ymm10
	vmovdqu	YMMWORD[(320-448)+r12],ymm9
	vpmuludq	ymm2,ymm2,ymm10
	vmovdqu	YMMWORD[(352-448)+r12],ymm9
	vpmuludq	ymm3,ymm3,ymm10
	vmovdqu	YMMWORD[(384-448)+r12],ymm9
	vpmuludq	ymm4,ymm4,ymm10
	vmovdqu	YMMWORD[(416-448)+r12],ymm9
	vpmuludq	ymm5,ymm5,ymm10
	vmovdqu	YMMWORD[(448-448)+r12],ymm9
	vpmuludq	ymm6,ymm6,ymm10
	vmovdqu	YMMWORD[(480-448)+r12],ymm9
	vpmuludq	ymm7,ymm7,ymm10
	vmovdqu	YMMWORD[(512-448)+r12],ymm9
	vpmuludq	ymm8,ymm8,ymm10
	vpbroadcastq	ymm10,QWORD[((64-128))+rsi]
	vmovdqu	YMMWORD[(544-448)+r12],ymm9

	mov	r15,rsi
	mov	r14d,4
	jmp	NEAR $L$sqr_entry_1024
ALIGN	32
$L$OOP_SQR_1024:
	vpbroadcastq	ymm11,QWORD[((32-128))+r15]
	vpmuludq	ymm0,ymm10,YMMWORD[((0-128))+rsi]
	vpaddq	ymm0,ymm0,YMMWORD[((0-192))+rbx]
	vpmuludq	ymm1,ymm10,YMMWORD[((0-128))+r9]
	vpaddq	ymm1,ymm1,YMMWORD[((32-192))+rbx]
	vpmuludq	ymm2,ymm10,YMMWORD[((32-128))+r9]
	vpaddq	ymm2,ymm2,YMMWORD[((64-192))+rbx]
	vpmuludq	ymm3,ymm10,YMMWORD[((64-128))+r9]
	vpaddq	ymm3,ymm3,YMMWORD[((96-192))+rbx]
	vpmuludq	ymm4,ymm10,YMMWORD[((96-128))+r9]
	vpaddq	ymm4,ymm4,YMMWORD[((128-192))+rbx]
	vpmuludq	ymm5,ymm10,YMMWORD[((128-128))+r9]
	vpaddq	ymm5,ymm5,YMMWORD[((160-192))+rbx]
	vpmuludq	ymm6,ymm10,YMMWORD[((160-128))+r9]
	vpaddq	ymm6,ymm6,YMMWORD[((192-192))+rbx]
	vpmuludq	ymm7,ymm10,YMMWORD[((192-128))+r9]
	vpaddq	ymm7,ymm7,YMMWORD[((224-192))+rbx]
	vpmuludq	ymm8,ymm10,YMMWORD[((224-128))+r9]
	vpbroadcastq	ymm10,QWORD[((64-128))+r15]
	vpaddq	ymm8,ymm8,YMMWORD[((256-192))+rbx]
$L$sqr_entry_1024:
	vmovdqu	YMMWORD[(0-192)+rbx],ymm0
	vmovdqu	YMMWORD[(32-192)+rbx],ymm1

	vpmuludq	ymm12,ymm11,YMMWORD[((32-128))+rsi]
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm14,ymm11,YMMWORD[((32-128))+r9]
	vpaddq	ymm3,ymm3,ymm14
	vpmuludq	ymm13,ymm11,YMMWORD[((64-128))+r9]
	vpaddq	ymm4,ymm4,ymm13
	vpmuludq	ymm12,ymm11,YMMWORD[((96-128))+r9]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm14,ymm11,YMMWORD[((128-128))+r9]
	vpaddq	ymm6,ymm6,ymm14
	vpmuludq	ymm13,ymm11,YMMWORD[((160-128))+r9]
	vpaddq	ymm7,ymm7,ymm13
	vpmuludq	ymm12,ymm11,YMMWORD[((192-128))+r9]
	vpaddq	ymm8,ymm8,ymm12
	vpmuludq	ymm0,ymm11,YMMWORD[((224-128))+r9]
	vpbroadcastq	ymm11,QWORD[((96-128))+r15]
	vpaddq	ymm0,ymm0,YMMWORD[((288-192))+rbx]

	vmovdqu	YMMWORD[(64-192)+rbx],ymm2
	vmovdqu	YMMWORD[(96-192)+rbx],ymm3

	vpmuludq	ymm13,ymm10,YMMWORD[((64-128))+rsi]
	vpaddq	ymm4,ymm4,ymm13
	vpmuludq	ymm12,ymm10,YMMWORD[((64-128))+r9]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm14,ymm10,YMMWORD[((96-128))+r9]
	vpaddq	ymm6,ymm6,ymm14
	vpmuludq	ymm13,ymm10,YMMWORD[((128-128))+r9]
	vpaddq	ymm7,ymm7,ymm13
	vpmuludq	ymm12,ymm10,YMMWORD[((160-128))+r9]
	vpaddq	ymm8,ymm8,ymm12
	vpmuludq	ymm14,ymm10,YMMWORD[((192-128))+r9]
	vpaddq	ymm0,ymm0,ymm14
	vpmuludq	ymm1,ymm10,YMMWORD[((224-128))+r9]
	vpbroadcastq	ymm10,QWORD[((128-128))+r15]
	vpaddq	ymm1,ymm1,YMMWORD[((320-448))+r12]

	vmovdqu	YMMWORD[(128-192)+rbx],ymm4
	vmovdqu	YMMWORD[(160-192)+rbx],ymm5

	vpmuludq	ymm12,ymm11,YMMWORD[((96-128))+rsi]
	vpaddq	ymm6,ymm6,ymm12
	vpmuludq	ymm14,ymm11,YMMWORD[((96-128))+r9]
	vpaddq	ymm7,ymm7,ymm14
	vpmuludq	ymm13,ymm11,YMMWORD[((128-128))+r9]
	vpaddq	ymm8,ymm8,ymm13
	vpmuludq	ymm12,ymm11,YMMWORD[((160-128))+r9]
	vpaddq	ymm0,ymm0,ymm12
	vpmuludq	ymm14,ymm11,YMMWORD[((192-128))+r9]
	vpaddq	ymm1,ymm1,ymm14
	vpmuludq	ymm2,ymm11,YMMWORD[((224-128))+r9]
	vpbroadcastq	ymm11,QWORD[((160-128))+r15]
	vpaddq	ymm2,ymm2,YMMWORD[((352-448))+r12]

	vmovdqu	YMMWORD[(192-192)+rbx],ymm6
	vmovdqu	YMMWORD[(224-192)+rbx],ymm7

	vpmuludq	ymm12,ymm10,YMMWORD[((128-128))+rsi]
	vpaddq	ymm8,ymm8,ymm12
	vpmuludq	ymm14,ymm10,YMMWORD[((128-128))+r9]
	vpaddq	ymm0,ymm0,ymm14
	vpmuludq	ymm13,ymm10,YMMWORD[((160-128))+r9]
	vpaddq	ymm1,ymm1,ymm13
	vpmuludq	ymm12,ymm10,YMMWORD[((192-128))+r9]
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm3,ymm10,YMMWORD[((224-128))+r9]
	vpbroadcastq	ymm10,QWORD[((192-128))+r15]
	vpaddq	ymm3,ymm3,YMMWORD[((384-448))+r12]

	vmovdqu	YMMWORD[(256-192)+rbx],ymm8
	vmovdqu	YMMWORD[(288-192)+rbx],ymm0
	lea	rbx,[8+rbx]

	vpmuludq	ymm13,ymm11,YMMWORD[((160-128))+rsi]
	vpaddq	ymm1,ymm1,ymm13
	vpmuludq	ymm12,ymm11,YMMWORD[((160-128))+r9]
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm14,ymm11,YMMWORD[((192-128))+r9]
	vpaddq	ymm3,ymm3,ymm14
	vpmuludq	ymm4,ymm11,YMMWORD[((224-128))+r9]
	vpbroadcastq	ymm11,QWORD[((224-128))+r15]
	vpaddq	ymm4,ymm4,YMMWORD[((416-448))+r12]

	vmovdqu	YMMWORD[(320-448)+r12],ymm1
	vmovdqu	YMMWORD[(352-448)+r12],ymm2

	vpmuludq	ymm12,ymm10,YMMWORD[((192-128))+rsi]
	vpaddq	ymm3,ymm3,ymm12
	vpmuludq	ymm14,ymm10,YMMWORD[((192-128))+r9]
	vpbroadcastq	ymm0,QWORD[((256-128))+r15]
	vpaddq	ymm4,ymm4,ymm14
	vpmuludq	ymm5,ymm10,YMMWORD[((224-128))+r9]
	vpbroadcastq	ymm10,QWORD[((0+8-128))+r15]
	vpaddq	ymm5,ymm5,YMMWORD[((448-448))+r12]

	vmovdqu	YMMWORD[(384-448)+r12],ymm3
	vmovdqu	YMMWORD[(416-448)+r12],ymm4
	lea	r15,[8+r15]

	vpmuludq	ymm12,ymm11,YMMWORD[((224-128))+rsi]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm6,ymm11,YMMWORD[((224-128))+r9]
	vpaddq	ymm6,ymm6,YMMWORD[((480-448))+r12]

	vpmuludq	ymm7,ymm0,YMMWORD[((256-128))+rsi]
	vmovdqu	YMMWORD[(448-448)+r12],ymm5
	vpaddq	ymm7,ymm7,YMMWORD[((512-448))+r12]
	vmovdqu	YMMWORD[(480-448)+r12],ymm6
	vmovdqu	YMMWORD[(512-448)+r12],ymm7
	lea	r12,[8+r12]

	dec	r14d
	jnz	NEAR $L$OOP_SQR_1024

	vmovdqu	ymm8,YMMWORD[256+rsp]
	vmovdqu	ymm1,YMMWORD[288+rsp]
	vmovdqu	ymm2,YMMWORD[320+rsp]
	lea	rbx,[192+rsp]

	vpsrlq	ymm14,ymm8,29
	vpand	ymm8,ymm8,ymm15
	vpsrlq	ymm11,ymm1,29
	vpand	ymm1,ymm1,ymm15

	vpermq	ymm14,ymm14,0x93
	vpxor	ymm9,ymm9,ymm9
	vpermq	ymm11,ymm11,0x93

	vpblendd	ymm10,ymm14,ymm9,3
	vpblendd	ymm14,ymm11,ymm14,3
	vpaddq	ymm8,ymm8,ymm10
	vpblendd	ymm11,ymm9,ymm11,3
	vpaddq	ymm1,ymm1,ymm14
	vpaddq	ymm2,ymm2,ymm11
	vmovdqu	YMMWORD[(288-192)+rbx],ymm1
	vmovdqu	YMMWORD[(320-192)+rbx],ymm2

	mov	rax,QWORD[rsp]
	mov	r10,QWORD[8+rsp]
	mov	r11,QWORD[16+rsp]
	mov	r12,QWORD[24+rsp]
	vmovdqu	ymm1,YMMWORD[32+rsp]
	vmovdqu	ymm2,YMMWORD[((64-192))+rbx]
	vmovdqu	ymm3,YMMWORD[((96-192))+rbx]
	vmovdqu	ymm4,YMMWORD[((128-192))+rbx]
	vmovdqu	ymm5,YMMWORD[((160-192))+rbx]
	vmovdqu	ymm6,YMMWORD[((192-192))+rbx]
	vmovdqu	ymm7,YMMWORD[((224-192))+rbx]

	mov	r9,rax
	imul	eax,ecx
	and	eax,0x1fffffff
	vmovd	xmm12,eax

	mov	rdx,rax
	imul	rax,QWORD[((-128))+r13]
	vpbroadcastq	ymm12,xmm12
	add	r9,rax
	mov	rax,rdx
	imul	rax,QWORD[((8-128))+r13]
	shr	r9,29
	add	r10,rax
	mov	rax,rdx
	imul	rax,QWORD[((16-128))+r13]
	add	r10,r9
	add	r11,rax
	imul	rdx,QWORD[((24-128))+r13]
	add	r12,rdx

	mov	rax,r10
	imul	eax,ecx
	and	eax,0x1fffffff

	mov	r14d,9
	jmp	NEAR $L$OOP_REDUCE_1024

ALIGN	32
$L$OOP_REDUCE_1024:
	vmovd	xmm13,eax
	vpbroadcastq	ymm13,xmm13

	vpmuludq	ymm10,ymm12,YMMWORD[((32-128))+r13]
	mov	rdx,rax
	imul	rax,QWORD[((-128))+r13]
	vpaddq	ymm1,ymm1,ymm10
	add	r10,rax
	vpmuludq	ymm14,ymm12,YMMWORD[((64-128))+r13]
	mov	rax,rdx
	imul	rax,QWORD[((8-128))+r13]
	vpaddq	ymm2,ymm2,ymm14
	vpmuludq	ymm11,ymm12,YMMWORD[((96-128))+r13]
DB	0x67
	add	r11,rax
DB	0x67
	mov	rax,rdx
	imul	rax,QWORD[((16-128))+r13]
	shr	r10,29
	vpaddq	ymm3,ymm3,ymm11
	vpmuludq	ymm10,ymm12,YMMWORD[((128-128))+r13]
	add	r12,rax
	add	r11,r10
	vpaddq	ymm4,ymm4,ymm10
	vpmuludq	ymm14,ymm12,YMMWORD[((160-128))+r13]
	mov	rax,r11
	imul	eax,ecx
	vpaddq	ymm5,ymm5,ymm14
	vpmuludq	ymm11,ymm12,YMMWORD[((192-128))+r13]
	and	eax,0x1fffffff
	vpaddq	ymm6,ymm6,ymm11
	vpmuludq	ymm10,ymm12,YMMWORD[((224-128))+r13]
	vpaddq	ymm7,ymm7,ymm10
	vpmuludq	ymm14,ymm12,YMMWORD[((256-128))+r13]
	vmovd	xmm12,eax

	vpaddq	ymm8,ymm8,ymm14

	vpbroadcastq	ymm12,xmm12

	vpmuludq	ymm11,ymm13,YMMWORD[((32-8-128))+r13]
	vmovdqu	ymm14,YMMWORD[((96-8-128))+r13]
	mov	rdx,rax
	imul	rax,QWORD[((-128))+r13]
	vpaddq	ymm1,ymm1,ymm11
	vpmuludq	ymm10,ymm13,YMMWORD[((64-8-128))+r13]
	vmovdqu	ymm11,YMMWORD[((128-8-128))+r13]
	add	r11,rax
	mov	rax,rdx
	imul	rax,QWORD[((8-128))+r13]
	vpaddq	ymm2,ymm2,ymm10
	add	rax,r12
	shr	r11,29
	vpmuludq	ymm14,ymm14,ymm13
	vmovdqu	ymm10,YMMWORD[((160-8-128))+r13]
	add	rax,r11
	vpaddq	ymm3,ymm3,ymm14
	vpmuludq	ymm11,ymm11,ymm13
	vmovdqu	ymm14,YMMWORD[((192-8-128))+r13]
DB	0x67
	mov	r12,rax
	imul	eax,ecx
	vpaddq	ymm4,ymm4,ymm11
	vpmuludq	ymm10,ymm10,ymm13
DB	0xc4,0x41,0x7e,0x6f,0x9d,0x58,0x00,0x00,0x00
	and	eax,0x1fffffff
	vpaddq	ymm5,ymm5,ymm10
	vpmuludq	ymm14,ymm14,ymm13
	vmovdqu	ymm10,YMMWORD[((256-8-128))+r13]
	vpaddq	ymm6,ymm6,ymm14
	vpmuludq	ymm11,ymm11,ymm13
	vmovdqu	ymm9,YMMWORD[((288-8-128))+r13]
	vmovd	xmm0,eax
	imul	rax,QWORD[((-128))+r13]
	vpaddq	ymm7,ymm7,ymm11
	vpmuludq	ymm10,ymm10,ymm13
	vmovdqu	ymm14,YMMWORD[((32-16-128))+r13]
	vpbroadcastq	ymm0,xmm0
	vpaddq	ymm8,ymm8,ymm10
	vpmuludq	ymm9,ymm9,ymm13
	vmovdqu	ymm11,YMMWORD[((64-16-128))+r13]
	add	r12,rax

	vmovdqu	ymm13,YMMWORD[((32-24-128))+r13]
	vpmuludq	ymm14,ymm14,ymm12
	vmovdqu	ymm10,YMMWORD[((96-16-128))+r13]
	vpaddq	ymm1,ymm1,ymm14
	vpmuludq	ymm13,ymm13,ymm0
	vpmuludq	ymm11,ymm11,ymm12
DB	0xc4,0x41,0x7e,0x6f,0xb5,0xf0,0xff,0xff,0xff
	vpaddq	ymm13,ymm13,ymm1
	vpaddq	ymm2,ymm2,ymm11
	vpmuludq	ymm10,ymm10,ymm12
	vmovdqu	ymm11,YMMWORD[((160-16-128))+r13]
DB	0x67
	vmovq	rax,xmm13
	vmovdqu	YMMWORD[rsp],ymm13
	vpaddq	ymm3,ymm3,ymm10
	vpmuludq	ymm14,ymm14,ymm12
	vmovdqu	ymm10,YMMWORD[((192-16-128))+r13]
	vpaddq	ymm4,ymm4,ymm14
	vpmuludq	ymm11,ymm11,ymm12
	vmovdqu	ymm14,YMMWORD[((224-16-128))+r13]
	vpaddq	ymm5,ymm5,ymm11
	vpmuludq	ymm10,ymm10,ymm12
	vmovdqu	ymm11,YMMWORD[((256-16-128))+r13]
	vpaddq	ymm6,ymm6,ymm10
	vpmuludq	ymm14,ymm14,ymm12
	shr	r12,29
	vmovdqu	ymm10,YMMWORD[((288-16-128))+r13]
	add	rax,r12
	vpaddq	ymm7,ymm7,ymm14
	vpmuludq	ymm11,ymm11,ymm12

	mov	r9,rax
	imul	eax,ecx
	vpaddq	ymm8,ymm8,ymm11
	vpmuludq	ymm10,ymm10,ymm12
	and	eax,0x1fffffff
	vmovd	xmm12,eax
	vmovdqu	ymm11,YMMWORD[((96-24-128))+r13]
DB	0x67
	vpaddq	ymm9,ymm9,ymm10
	vpbroadcastq	ymm12,xmm12

	vpmuludq	ymm14,ymm0,YMMWORD[((64-24-128))+r13]
	vmovdqu	ymm10,YMMWORD[((128-24-128))+r13]
	mov	rdx,rax
	imul	rax,QWORD[((-128))+r13]
	mov	r10,QWORD[8+rsp]
	vpaddq	ymm1,ymm2,ymm14
	vpmuludq	ymm11,ymm11,ymm0
	vmovdqu	ymm14,YMMWORD[((160-24-128))+r13]
	add	r9,rax
	mov	rax,rdx
	imul	rax,QWORD[((8-128))+r13]
DB	0x67
	shr	r9,29
	mov	r11,QWORD[16+rsp]
	vpaddq	ymm2,ymm3,ymm11
	vpmuludq	ymm10,ymm10,ymm0
	vmovdqu	ymm11,YMMWORD[((192-24-128))+r13]
	add	r10,rax
	mov	rax,rdx
	imul	rax,QWORD[((16-128))+r13]
	vpaddq	ymm3,ymm4,ymm10
	vpmuludq	ymm14,ymm14,ymm0
	vmovdqu	ymm10,YMMWORD[((224-24-128))+r13]
	imul	rdx,QWORD[((24-128))+r13]
	add	r11,rax
	lea	rax,[r10*1+r9]
	vpaddq	ymm4,ymm5,ymm14
	vpmuludq	ymm11,ymm11,ymm0
	vmovdqu	ymm14,YMMWORD[((256-24-128))+r13]
	mov	r10,rax
	imul	eax,ecx
	vpmuludq	ymm10,ymm10,ymm0
	vpaddq	ymm5,ymm6,ymm11
	vmovdqu	ymm11,YMMWORD[((288-24-128))+r13]
	and	eax,0x1fffffff
	vpaddq	ymm6,ymm7,ymm10
	vpmuludq	ymm14,ymm14,ymm0
	add	rdx,QWORD[24+rsp]
	vpaddq	ymm7,ymm8,ymm14
	vpmuludq	ymm11,ymm11,ymm0
	vpaddq	ymm8,ymm9,ymm11
	vmovq	xmm9,r12
	mov	r12,rdx

	dec	r14d
	jnz	NEAR $L$OOP_REDUCE_1024
	lea	r12,[448+rsp]
	vpaddq	ymm0,ymm13,ymm9
	vpxor	ymm9,ymm9,ymm9

	vpaddq	ymm0,ymm0,YMMWORD[((288-192))+rbx]
	vpaddq	ymm1,ymm1,YMMWORD[((320-448))+r12]
	vpaddq	ymm2,ymm2,YMMWORD[((352-448))+r12]
	vpaddq	ymm3,ymm3,YMMWORD[((384-448))+r12]
	vpaddq	ymm4,ymm4,YMMWORD[((416-448))+r12]
	vpaddq	ymm5,ymm5,YMMWORD[((448-448))+r12]
	vpaddq	ymm6,ymm6,YMMWORD[((480-448))+r12]
	vpaddq	ymm7,ymm7,YMMWORD[((512-448))+r12]
	vpaddq	ymm8,ymm8,YMMWORD[((544-448))+r12]

	vpsrlq	ymm14,ymm0,29
	vpand	ymm0,ymm0,ymm15
	vpsrlq	ymm11,ymm1,29
	vpand	ymm1,ymm1,ymm15
	vpsrlq	ymm12,ymm2,29
	vpermq	ymm14,ymm14,0x93
	vpand	ymm2,ymm2,ymm15
	vpsrlq	ymm13,ymm3,29
	vpermq	ymm11,ymm11,0x93
	vpand	ymm3,ymm3,ymm15
	vpermq	ymm12,ymm12,0x93

	vpblendd	ymm10,ymm14,ymm9,3
	vpermq	ymm13,ymm13,0x93
	vpblendd	ymm14,ymm11,ymm14,3
	vpaddq	ymm0,ymm0,ymm10
	vpblendd	ymm11,ymm12,ymm11,3
	vpaddq	ymm1,ymm1,ymm14
	vpblendd	ymm12,ymm13,ymm12,3
	vpaddq	ymm2,ymm2,ymm11
	vpblendd	ymm13,ymm9,ymm13,3
	vpaddq	ymm3,ymm3,ymm12
	vpaddq	ymm4,ymm4,ymm13

	vpsrlq	ymm14,ymm0,29
	vpand	ymm0,ymm0,ymm15
	vpsrlq	ymm11,ymm1,29
	vpand	ymm1,ymm1,ymm15
	vpsrlq	ymm12,ymm2,29
	vpermq	ymm14,ymm14,0x93
	vpand	ymm2,ymm2,ymm15
	vpsrlq	ymm13,ymm3,29
	vpermq	ymm11,ymm11,0x93
	vpand	ymm3,ymm3,ymm15
	vpermq	ymm12,ymm12,0x93

	vpblendd	ymm10,ymm14,ymm9,3
	vpermq	ymm13,ymm13,0x93
	vpblendd	ymm14,ymm11,ymm14,3
	vpaddq	ymm0,ymm0,ymm10
	vpblendd	ymm11,ymm12,ymm11,3
	vpaddq	ymm1,ymm1,ymm14
	vmovdqu	YMMWORD[(0-128)+rdi],ymm0
	vpblendd	ymm12,ymm13,ymm12,3
	vpaddq	ymm2,ymm2,ymm11
	vmovdqu	YMMWORD[(32-128)+rdi],ymm1
	vpblendd	ymm13,ymm9,ymm13,3
	vpaddq	ymm3,ymm3,ymm12
	vmovdqu	YMMWORD[(64-128)+rdi],ymm2
	vpaddq	ymm4,ymm4,ymm13
	vmovdqu	YMMWORD[(96-128)+rdi],ymm3
	vpsrlq	ymm14,ymm4,29
	vpand	ymm4,ymm4,ymm15
	vpsrlq	ymm11,ymm5,29
	vpand	ymm5,ymm5,ymm15
	vpsrlq	ymm12,ymm6,29
	vpermq	ymm14,ymm14,0x93
	vpand	ymm6,ymm6,ymm15
	vpsrlq	ymm13,ymm7,29
	vpermq	ymm11,ymm11,0x93
	vpand	ymm7,ymm7,ymm15
	vpsrlq	ymm0,ymm8,29
	vpermq	ymm12,ymm12,0x93
	vpand	ymm8,ymm8,ymm15
	vpermq	ymm13,ymm13,0x93

	vpblendd	ymm10,ymm14,ymm9,3
	vpermq	ymm0,ymm0,0x93
	vpblendd	ymm14,ymm11,ymm14,3
	vpaddq	ymm4,ymm4,ymm10
	vpblendd	ymm11,ymm12,ymm11,3
	vpaddq	ymm5,ymm5,ymm14
	vpblendd	ymm12,ymm13,ymm12,3
	vpaddq	ymm6,ymm6,ymm11
	vpblendd	ymm13,ymm0,ymm13,3
	vpaddq	ymm7,ymm7,ymm12
	vpaddq	ymm8,ymm8,ymm13

	vpsrlq	ymm14,ymm4,29
	vpand	ymm4,ymm4,ymm15
	vpsrlq	ymm11,ymm5,29
	vpand	ymm5,ymm5,ymm15
	vpsrlq	ymm12,ymm6,29
	vpermq	ymm14,ymm14,0x93
	vpand	ymm6,ymm6,ymm15
	vpsrlq	ymm13,ymm7,29
	vpermq	ymm11,ymm11,0x93
	vpand	ymm7,ymm7,ymm15
	vpsrlq	ymm0,ymm8,29
	vpermq	ymm12,ymm12,0x93
	vpand	ymm8,ymm8,ymm15
	vpermq	ymm13,ymm13,0x93

	vpblendd	ymm10,ymm14,ymm9,3
	vpermq	ymm0,ymm0,0x93
	vpblendd	ymm14,ymm11,ymm14,3
	vpaddq	ymm4,ymm4,ymm10
	vpblendd	ymm11,ymm12,ymm11,3
	vpaddq	ymm5,ymm5,ymm14
	vmovdqu	YMMWORD[(128-128)+rdi],ymm4
	vpblendd	ymm12,ymm13,ymm12,3
	vpaddq	ymm6,ymm6,ymm11
	vmovdqu	YMMWORD[(160-128)+rdi],ymm5
	vpblendd	ymm13,ymm0,ymm13,3
	vpaddq	ymm7,ymm7,ymm12
	vmovdqu	YMMWORD[(192-128)+rdi],ymm6
	vpaddq	ymm8,ymm8,ymm13
	vmovdqu	YMMWORD[(224-128)+rdi],ymm7
	vmovdqu	YMMWORD[(256-128)+rdi],ymm8

	mov	rsi,rdi
	dec	r8d
	jne	NEAR $L$OOP_GRANDE_SQR_1024

	vzeroall
	mov	rax,rbp

$L$sqr_1024_in_tail:
	movaps	xmm6,XMMWORD[((-216))+rax]
	movaps	xmm7,XMMWORD[((-200))+rax]
	movaps	xmm8,XMMWORD[((-184))+rax]
	movaps	xmm9,XMMWORD[((-168))+rax]
	movaps	xmm10,XMMWORD[((-152))+rax]
	movaps	xmm11,XMMWORD[((-136))+rax]
	movaps	xmm12,XMMWORD[((-120))+rax]
	movaps	xmm13,XMMWORD[((-104))+rax]
	movaps	xmm14,XMMWORD[((-88))+rax]
	movaps	xmm15,XMMWORD[((-72))+rax]
	mov	r15,QWORD[((-48))+rax]

	mov	r14,QWORD[((-40))+rax]

	mov	r13,QWORD[((-32))+rax]

	mov	r12,QWORD[((-24))+rax]

	mov	rbp,QWORD[((-16))+rax]

	mov	rbx,QWORD[((-8))+rax]

	lea	rsp,[rax]

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_1024_sqr_avx2
$L$SEH_end_rsaz_1024_sqr_avx2:
global	rsaz_1024_mul_avx2

ALIGN	64
SEH64_PROC_FRAME rsaz_1024_mul_avx2,PUBLIC
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



	lea	rax,[rsp]

	push	rbx
SEH64_PUSHREG	rbx	; cfa_rsp,cfa_reg_offset => -16,8 cfa_reg=%rax
	push	rbp
SEH64_PUSHREG	rbp	; cfa_rsp,cfa_reg_offset => -24,8 cfa_reg=%rax
	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -32,8 cfa_reg=%rax
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -40,8 cfa_reg=%rax
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -48,8 cfa_reg=%rax
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -56,8 cfa_reg=%rax
	vzeroupper
	lea	rsp,[((-168))+rsp]
SEH64_ALLOCSTACK 168
	vmovaps	XMMWORD[(-216)+rax],xmm6
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -0xd8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-200)+rax],xmm7
SEH64_SAVEXMM128 xmm7, 16	; .cfi_offset xmm7, -0xc8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-184)+rax],xmm8
SEH64_SAVEXMM128 xmm8, 32	; .cfi_offset xmm8, -0xb8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-168)+rax],xmm9
SEH64_SAVEXMM128 xmm9, 48	; .cfi_offset xmm9, -0xa8-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-152)+rax],xmm10
SEH64_SAVEXMM128 xmm10, 64	; .cfi_offset xmm10, -0x98-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-136)+rax],xmm11
SEH64_SAVEXMM128 xmm11, 80	; .cfi_offset xmm11, -0x88-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-120)+rax],xmm12
SEH64_SAVEXMM128 xmm12, 96	; .cfi_offset xmm12, -0x78-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-104)+rax],xmm13
SEH64_SAVEXMM128 xmm13, 112	; .cfi_offset xmm13, -0x68-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-88)+rax],xmm14
SEH64_SAVEXMM128 xmm14, 128	; .cfi_offset xmm14, -0x58-8 cfa_reg_offset=8 cfa_rsp=-224
	vmovaps	XMMWORD[(-72)+rax],xmm15
SEH64_SAVEXMM128 xmm15, 144	; .cfi_offset xmm15, -0x48-8 cfa_reg_offset=8 cfa_rsp=-224
	mov	rbp,rax

SEH64_ALLOCSTACK 8
SEH64_SETFRAME rbp, 224
SEH64_ENDPROLOG	; cfa_reg=%rbp cfa_reg_offset=8 cfa_rsp=-224
	vzeroall
	mov	r13,rdx
	sub	rsp,64






DB	0x67,0x67
	mov	r15,rsi
	and	r15,4095
	add	r15,32*10
	shr	r15,12
	mov	r15,rsi
	cmovnz	rsi,r13
	cmovnz	r13,r15

	mov	r15,rcx
	sub	rsi,-128
	sub	rcx,-128
	sub	rdi,-128

	and	r15,4095
	add	r15,32*10
DB	0x67,0x67
	shr	r15,12
	jz	NEAR $L$mul_1024_no_n_copy





	sub	rsp,32*10
	vmovdqu	ymm0,YMMWORD[((0-128))+rcx]
	and	rsp,-512
	vmovdqu	ymm1,YMMWORD[((32-128))+rcx]
	vmovdqu	ymm2,YMMWORD[((64-128))+rcx]
	vmovdqu	ymm3,YMMWORD[((96-128))+rcx]
	vmovdqu	ymm4,YMMWORD[((128-128))+rcx]
	vmovdqu	ymm5,YMMWORD[((160-128))+rcx]
	vmovdqu	ymm6,YMMWORD[((192-128))+rcx]
	vmovdqu	ymm7,YMMWORD[((224-128))+rcx]
	vmovdqu	ymm8,YMMWORD[((256-128))+rcx]
	lea	rcx,[((64+128))+rsp]
	vmovdqu	YMMWORD[(0-128)+rcx],ymm0
	vpxor	ymm0,ymm0,ymm0
	vmovdqu	YMMWORD[(32-128)+rcx],ymm1
	vpxor	ymm1,ymm1,ymm1
	vmovdqu	YMMWORD[(64-128)+rcx],ymm2
	vpxor	ymm2,ymm2,ymm2
	vmovdqu	YMMWORD[(96-128)+rcx],ymm3
	vpxor	ymm3,ymm3,ymm3
	vmovdqu	YMMWORD[(128-128)+rcx],ymm4
	vpxor	ymm4,ymm4,ymm4
	vmovdqu	YMMWORD[(160-128)+rcx],ymm5
	vpxor	ymm5,ymm5,ymm5
	vmovdqu	YMMWORD[(192-128)+rcx],ymm6
	vpxor	ymm6,ymm6,ymm6
	vmovdqu	YMMWORD[(224-128)+rcx],ymm7
	vpxor	ymm7,ymm7,ymm7
	vmovdqu	YMMWORD[(256-128)+rcx],ymm8
	vmovdqa	ymm8,ymm0
	vmovdqu	YMMWORD[(288-128)+rcx],ymm9
$L$mul_1024_no_n_copy:
	and	rsp,-64

	mov	rbx,QWORD[r13]
	vpbroadcastq	ymm10,QWORD[r13]
	vmovdqu	YMMWORD[rsp],ymm0
	xor	r9,r9
DB	0x67
	xor	r10,r10
	xor	r11,r11
	xor	r12,r12

	vmovdqu	ymm15,YMMWORD[$L$and_mask]
	mov	r14d,9
	vmovdqu	YMMWORD[(288-128)+rdi],ymm9
	jmp	NEAR $L$oop_mul_1024

ALIGN	32
$L$oop_mul_1024:
	vpsrlq	ymm9,ymm3,29
	mov	rax,rbx
	imul	rax,QWORD[((-128))+rsi]
	add	rax,r9
	mov	r10,rbx
	imul	r10,QWORD[((8-128))+rsi]
	add	r10,QWORD[8+rsp]

	mov	r9,rax
	imul	eax,r8d
	and	eax,0x1fffffff

	mov	r11,rbx
	imul	r11,QWORD[((16-128))+rsi]
	add	r11,QWORD[16+rsp]

	mov	r12,rbx
	imul	r12,QWORD[((24-128))+rsi]
	add	r12,QWORD[24+rsp]
	vpmuludq	ymm0,ymm10,YMMWORD[((32-128))+rsi]
	vmovd	xmm11,eax
	vpaddq	ymm1,ymm1,ymm0
	vpmuludq	ymm12,ymm10,YMMWORD[((64-128))+rsi]
	vpbroadcastq	ymm11,xmm11
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm13,ymm10,YMMWORD[((96-128))+rsi]
	vpand	ymm3,ymm3,ymm15
	vpaddq	ymm3,ymm3,ymm13
	vpmuludq	ymm0,ymm10,YMMWORD[((128-128))+rsi]
	vpaddq	ymm4,ymm4,ymm0
	vpmuludq	ymm12,ymm10,YMMWORD[((160-128))+rsi]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm13,ymm10,YMMWORD[((192-128))+rsi]
	vpaddq	ymm6,ymm6,ymm13
	vpmuludq	ymm0,ymm10,YMMWORD[((224-128))+rsi]
	vpermq	ymm9,ymm9,0x93
	vpaddq	ymm7,ymm7,ymm0
	vpmuludq	ymm12,ymm10,YMMWORD[((256-128))+rsi]
	vpbroadcastq	ymm10,QWORD[8+r13]
	vpaddq	ymm8,ymm8,ymm12

	mov	rdx,rax
	imul	rax,QWORD[((-128))+rcx]
	add	r9,rax
	mov	rax,rdx
	imul	rax,QWORD[((8-128))+rcx]
	add	r10,rax
	mov	rax,rdx
	imul	rax,QWORD[((16-128))+rcx]
	add	r11,rax
	shr	r9,29
	imul	rdx,QWORD[((24-128))+rcx]
	add	r12,rdx
	add	r10,r9

	vpmuludq	ymm13,ymm11,YMMWORD[((32-128))+rcx]
	vmovq	rbx,xmm10
	vpaddq	ymm1,ymm1,ymm13
	vpmuludq	ymm0,ymm11,YMMWORD[((64-128))+rcx]
	vpaddq	ymm2,ymm2,ymm0
	vpmuludq	ymm12,ymm11,YMMWORD[((96-128))+rcx]
	vpaddq	ymm3,ymm3,ymm12
	vpmuludq	ymm13,ymm11,YMMWORD[((128-128))+rcx]
	vpaddq	ymm4,ymm4,ymm13
	vpmuludq	ymm0,ymm11,YMMWORD[((160-128))+rcx]
	vpaddq	ymm5,ymm5,ymm0
	vpmuludq	ymm12,ymm11,YMMWORD[((192-128))+rcx]
	vpaddq	ymm6,ymm6,ymm12
	vpmuludq	ymm13,ymm11,YMMWORD[((224-128))+rcx]
	vpblendd	ymm12,ymm9,ymm14,3
	vpaddq	ymm7,ymm7,ymm13
	vpmuludq	ymm0,ymm11,YMMWORD[((256-128))+rcx]
	vpaddq	ymm3,ymm3,ymm12
	vpaddq	ymm8,ymm8,ymm0

	mov	rax,rbx
	imul	rax,QWORD[((-128))+rsi]
	add	r10,rax
	vmovdqu	ymm12,YMMWORD[((-8+32-128))+rsi]
	mov	rax,rbx
	imul	rax,QWORD[((8-128))+rsi]
	add	r11,rax
	vmovdqu	ymm13,YMMWORD[((-8+64-128))+rsi]

	mov	rax,r10
	vpblendd	ymm9,ymm9,ymm14,0xfc
	imul	eax,r8d
	vpaddq	ymm4,ymm4,ymm9
	and	eax,0x1fffffff

	imul	rbx,QWORD[((16-128))+rsi]
	add	r12,rbx
	vpmuludq	ymm12,ymm12,ymm10
	vmovd	xmm11,eax
	vmovdqu	ymm0,YMMWORD[((-8+96-128))+rsi]
	vpaddq	ymm1,ymm1,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vpbroadcastq	ymm11,xmm11
	vmovdqu	ymm12,YMMWORD[((-8+128-128))+rsi]
	vpaddq	ymm2,ymm2,ymm13
	vpmuludq	ymm0,ymm0,ymm10
	vmovdqu	ymm13,YMMWORD[((-8+160-128))+rsi]
	vpaddq	ymm3,ymm3,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vmovdqu	ymm0,YMMWORD[((-8+192-128))+rsi]
	vpaddq	ymm4,ymm4,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vmovdqu	ymm12,YMMWORD[((-8+224-128))+rsi]
	vpaddq	ymm5,ymm5,ymm13
	vpmuludq	ymm0,ymm0,ymm10
	vmovdqu	ymm13,YMMWORD[((-8+256-128))+rsi]
	vpaddq	ymm6,ymm6,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vmovdqu	ymm9,YMMWORD[((-8+288-128))+rsi]
	vpaddq	ymm7,ymm7,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vpaddq	ymm8,ymm8,ymm13
	vpmuludq	ymm9,ymm9,ymm10
	vpbroadcastq	ymm10,QWORD[16+r13]

	mov	rdx,rax
	imul	rax,QWORD[((-128))+rcx]
	add	r10,rax
	vmovdqu	ymm0,YMMWORD[((-8+32-128))+rcx]
	mov	rax,rdx
	imul	rax,QWORD[((8-128))+rcx]
	add	r11,rax
	vmovdqu	ymm12,YMMWORD[((-8+64-128))+rcx]
	shr	r10,29
	imul	rdx,QWORD[((16-128))+rcx]
	add	r12,rdx
	add	r11,r10

	vpmuludq	ymm0,ymm0,ymm11
	vmovq	rbx,xmm10
	vmovdqu	ymm13,YMMWORD[((-8+96-128))+rcx]
	vpaddq	ymm1,ymm1,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vmovdqu	ymm0,YMMWORD[((-8+128-128))+rcx]
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vmovdqu	ymm12,YMMWORD[((-8+160-128))+rcx]
	vpaddq	ymm3,ymm3,ymm13
	vpmuludq	ymm0,ymm0,ymm11
	vmovdqu	ymm13,YMMWORD[((-8+192-128))+rcx]
	vpaddq	ymm4,ymm4,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vmovdqu	ymm0,YMMWORD[((-8+224-128))+rcx]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vmovdqu	ymm12,YMMWORD[((-8+256-128))+rcx]
	vpaddq	ymm6,ymm6,ymm13
	vpmuludq	ymm0,ymm0,ymm11
	vmovdqu	ymm13,YMMWORD[((-8+288-128))+rcx]
	vpaddq	ymm7,ymm7,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vpaddq	ymm8,ymm8,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vpaddq	ymm9,ymm9,ymm13

	vmovdqu	ymm0,YMMWORD[((-16+32-128))+rsi]
	mov	rax,rbx
	imul	rax,QWORD[((-128))+rsi]
	add	rax,r11

	vmovdqu	ymm12,YMMWORD[((-16+64-128))+rsi]
	mov	r11,rax
	imul	eax,r8d
	and	eax,0x1fffffff

	imul	rbx,QWORD[((8-128))+rsi]
	add	r12,rbx
	vpmuludq	ymm0,ymm0,ymm10
	vmovd	xmm11,eax
	vmovdqu	ymm13,YMMWORD[((-16+96-128))+rsi]
	vpaddq	ymm1,ymm1,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vpbroadcastq	ymm11,xmm11
	vmovdqu	ymm0,YMMWORD[((-16+128-128))+rsi]
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vmovdqu	ymm12,YMMWORD[((-16+160-128))+rsi]
	vpaddq	ymm3,ymm3,ymm13
	vpmuludq	ymm0,ymm0,ymm10
	vmovdqu	ymm13,YMMWORD[((-16+192-128))+rsi]
	vpaddq	ymm4,ymm4,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vmovdqu	ymm0,YMMWORD[((-16+224-128))+rsi]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vmovdqu	ymm12,YMMWORD[((-16+256-128))+rsi]
	vpaddq	ymm6,ymm6,ymm13
	vpmuludq	ymm0,ymm0,ymm10
	vmovdqu	ymm13,YMMWORD[((-16+288-128))+rsi]
	vpaddq	ymm7,ymm7,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vpaddq	ymm8,ymm8,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vpbroadcastq	ymm10,QWORD[24+r13]
	vpaddq	ymm9,ymm9,ymm13

	vmovdqu	ymm0,YMMWORD[((-16+32-128))+rcx]
	mov	rdx,rax
	imul	rax,QWORD[((-128))+rcx]
	add	r11,rax
	vmovdqu	ymm12,YMMWORD[((-16+64-128))+rcx]
	imul	rdx,QWORD[((8-128))+rcx]
	add	r12,rdx
	shr	r11,29

	vpmuludq	ymm0,ymm0,ymm11
	vmovq	rbx,xmm10
	vmovdqu	ymm13,YMMWORD[((-16+96-128))+rcx]
	vpaddq	ymm1,ymm1,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vmovdqu	ymm0,YMMWORD[((-16+128-128))+rcx]
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vmovdqu	ymm12,YMMWORD[((-16+160-128))+rcx]
	vpaddq	ymm3,ymm3,ymm13
	vpmuludq	ymm0,ymm0,ymm11
	vmovdqu	ymm13,YMMWORD[((-16+192-128))+rcx]
	vpaddq	ymm4,ymm4,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vmovdqu	ymm0,YMMWORD[((-16+224-128))+rcx]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vmovdqu	ymm12,YMMWORD[((-16+256-128))+rcx]
	vpaddq	ymm6,ymm6,ymm13
	vpmuludq	ymm0,ymm0,ymm11
	vmovdqu	ymm13,YMMWORD[((-16+288-128))+rcx]
	vpaddq	ymm7,ymm7,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vmovdqu	ymm0,YMMWORD[((-24+32-128))+rsi]
	vpaddq	ymm8,ymm8,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vmovdqu	ymm12,YMMWORD[((-24+64-128))+rsi]
	vpaddq	ymm9,ymm9,ymm13

	add	r12,r11
	imul	rbx,QWORD[((-128))+rsi]
	add	r12,rbx

	mov	rax,r12
	imul	eax,r8d
	and	eax,0x1fffffff

	vpmuludq	ymm0,ymm0,ymm10
	vmovd	xmm11,eax
	vmovdqu	ymm13,YMMWORD[((-24+96-128))+rsi]
	vpaddq	ymm1,ymm1,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vpbroadcastq	ymm11,xmm11
	vmovdqu	ymm0,YMMWORD[((-24+128-128))+rsi]
	vpaddq	ymm2,ymm2,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vmovdqu	ymm12,YMMWORD[((-24+160-128))+rsi]
	vpaddq	ymm3,ymm3,ymm13
	vpmuludq	ymm0,ymm0,ymm10
	vmovdqu	ymm13,YMMWORD[((-24+192-128))+rsi]
	vpaddq	ymm4,ymm4,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vmovdqu	ymm0,YMMWORD[((-24+224-128))+rsi]
	vpaddq	ymm5,ymm5,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vmovdqu	ymm12,YMMWORD[((-24+256-128))+rsi]
	vpaddq	ymm6,ymm6,ymm13
	vpmuludq	ymm0,ymm0,ymm10
	vmovdqu	ymm13,YMMWORD[((-24+288-128))+rsi]
	vpaddq	ymm7,ymm7,ymm0
	vpmuludq	ymm12,ymm12,ymm10
	vpaddq	ymm8,ymm8,ymm12
	vpmuludq	ymm13,ymm13,ymm10
	vpbroadcastq	ymm10,QWORD[32+r13]
	vpaddq	ymm9,ymm9,ymm13
	add	r13,32

	vmovdqu	ymm0,YMMWORD[((-24+32-128))+rcx]
	imul	rax,QWORD[((-128))+rcx]
	add	r12,rax
	shr	r12,29

	vmovdqu	ymm12,YMMWORD[((-24+64-128))+rcx]
	vpmuludq	ymm0,ymm0,ymm11
	vmovq	rbx,xmm10
	vmovdqu	ymm13,YMMWORD[((-24+96-128))+rcx]
	vpaddq	ymm0,ymm1,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vmovdqu	YMMWORD[rsp],ymm0
	vpaddq	ymm1,ymm2,ymm12
	vmovdqu	ymm0,YMMWORD[((-24+128-128))+rcx]
	vpmuludq	ymm13,ymm13,ymm11
	vmovdqu	ymm12,YMMWORD[((-24+160-128))+rcx]
	vpaddq	ymm2,ymm3,ymm13
	vpmuludq	ymm0,ymm0,ymm11
	vmovdqu	ymm13,YMMWORD[((-24+192-128))+rcx]
	vpaddq	ymm3,ymm4,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	vmovdqu	ymm0,YMMWORD[((-24+224-128))+rcx]
	vpaddq	ymm4,ymm5,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vmovdqu	ymm12,YMMWORD[((-24+256-128))+rcx]
	vpaddq	ymm5,ymm6,ymm13
	vpmuludq	ymm0,ymm0,ymm11
	vmovdqu	ymm13,YMMWORD[((-24+288-128))+rcx]
	mov	r9,r12
	vpaddq	ymm6,ymm7,ymm0
	vpmuludq	ymm12,ymm12,ymm11
	add	r9,QWORD[rsp]
	vpaddq	ymm7,ymm8,ymm12
	vpmuludq	ymm13,ymm13,ymm11
	vmovq	xmm12,r12
	vpaddq	ymm8,ymm9,ymm13

	dec	r14d
	jnz	NEAR $L$oop_mul_1024
	vpaddq	ymm0,ymm12,YMMWORD[rsp]

	vpsrlq	ymm12,ymm0,29
	vpand	ymm0,ymm0,ymm15
	vpsrlq	ymm13,ymm1,29
	vpand	ymm1,ymm1,ymm15
	vpsrlq	ymm10,ymm2,29
	vpermq	ymm12,ymm12,0x93
	vpand	ymm2,ymm2,ymm15
	vpsrlq	ymm11,ymm3,29
	vpermq	ymm13,ymm13,0x93
	vpand	ymm3,ymm3,ymm15

	vpblendd	ymm9,ymm12,ymm14,3
	vpermq	ymm10,ymm10,0x93
	vpblendd	ymm12,ymm13,ymm12,3
	vpermq	ymm11,ymm11,0x93
	vpaddq	ymm0,ymm0,ymm9
	vpblendd	ymm13,ymm10,ymm13,3
	vpaddq	ymm1,ymm1,ymm12
	vpblendd	ymm10,ymm11,ymm10,3
	vpaddq	ymm2,ymm2,ymm13
	vpblendd	ymm11,ymm14,ymm11,3
	vpaddq	ymm3,ymm3,ymm10
	vpaddq	ymm4,ymm4,ymm11

	vpsrlq	ymm12,ymm0,29
	vpand	ymm0,ymm0,ymm15
	vpsrlq	ymm13,ymm1,29
	vpand	ymm1,ymm1,ymm15
	vpsrlq	ymm10,ymm2,29
	vpermq	ymm12,ymm12,0x93
	vpand	ymm2,ymm2,ymm15
	vpsrlq	ymm11,ymm3,29
	vpermq	ymm13,ymm13,0x93
	vpand	ymm3,ymm3,ymm15
	vpermq	ymm10,ymm10,0x93

	vpblendd	ymm9,ymm12,ymm14,3
	vpermq	ymm11,ymm11,0x93
	vpblendd	ymm12,ymm13,ymm12,3
	vpaddq	ymm0,ymm0,ymm9
	vpblendd	ymm13,ymm10,ymm13,3
	vpaddq	ymm1,ymm1,ymm12
	vpblendd	ymm10,ymm11,ymm10,3
	vpaddq	ymm2,ymm2,ymm13
	vpblendd	ymm11,ymm14,ymm11,3
	vpaddq	ymm3,ymm3,ymm10
	vpaddq	ymm4,ymm4,ymm11

	vmovdqu	YMMWORD[(0-128)+rdi],ymm0
	vmovdqu	YMMWORD[(32-128)+rdi],ymm1
	vmovdqu	YMMWORD[(64-128)+rdi],ymm2
	vmovdqu	YMMWORD[(96-128)+rdi],ymm3
	vpsrlq	ymm12,ymm4,29
	vpand	ymm4,ymm4,ymm15
	vpsrlq	ymm13,ymm5,29
	vpand	ymm5,ymm5,ymm15
	vpsrlq	ymm10,ymm6,29
	vpermq	ymm12,ymm12,0x93
	vpand	ymm6,ymm6,ymm15
	vpsrlq	ymm11,ymm7,29
	vpermq	ymm13,ymm13,0x93
	vpand	ymm7,ymm7,ymm15
	vpsrlq	ymm0,ymm8,29
	vpermq	ymm10,ymm10,0x93
	vpand	ymm8,ymm8,ymm15
	vpermq	ymm11,ymm11,0x93

	vpblendd	ymm9,ymm12,ymm14,3
	vpermq	ymm0,ymm0,0x93
	vpblendd	ymm12,ymm13,ymm12,3
	vpaddq	ymm4,ymm4,ymm9
	vpblendd	ymm13,ymm10,ymm13,3
	vpaddq	ymm5,ymm5,ymm12
	vpblendd	ymm10,ymm11,ymm10,3
	vpaddq	ymm6,ymm6,ymm13
	vpblendd	ymm11,ymm0,ymm11,3
	vpaddq	ymm7,ymm7,ymm10
	vpaddq	ymm8,ymm8,ymm11

	vpsrlq	ymm12,ymm4,29
	vpand	ymm4,ymm4,ymm15
	vpsrlq	ymm13,ymm5,29
	vpand	ymm5,ymm5,ymm15
	vpsrlq	ymm10,ymm6,29
	vpermq	ymm12,ymm12,0x93
	vpand	ymm6,ymm6,ymm15
	vpsrlq	ymm11,ymm7,29
	vpermq	ymm13,ymm13,0x93
	vpand	ymm7,ymm7,ymm15
	vpsrlq	ymm0,ymm8,29
	vpermq	ymm10,ymm10,0x93
	vpand	ymm8,ymm8,ymm15
	vpermq	ymm11,ymm11,0x93

	vpblendd	ymm9,ymm12,ymm14,3
	vpermq	ymm0,ymm0,0x93
	vpblendd	ymm12,ymm13,ymm12,3
	vpaddq	ymm4,ymm4,ymm9
	vpblendd	ymm13,ymm10,ymm13,3
	vpaddq	ymm5,ymm5,ymm12
	vpblendd	ymm10,ymm11,ymm10,3
	vpaddq	ymm6,ymm6,ymm13
	vpblendd	ymm11,ymm0,ymm11,3
	vpaddq	ymm7,ymm7,ymm10
	vpaddq	ymm8,ymm8,ymm11

	vmovdqu	YMMWORD[(128-128)+rdi],ymm4
	vmovdqu	YMMWORD[(160-128)+rdi],ymm5
	vmovdqu	YMMWORD[(192-128)+rdi],ymm6
	vmovdqu	YMMWORD[(224-128)+rdi],ymm7
	vmovdqu	YMMWORD[(256-128)+rdi],ymm8
	vzeroupper

	mov	rax,rbp

$L$mul_1024_in_tail:
	movaps	xmm6,XMMWORD[((-216))+rax]
	movaps	xmm7,XMMWORD[((-200))+rax]
	movaps	xmm8,XMMWORD[((-184))+rax]
	movaps	xmm9,XMMWORD[((-168))+rax]
	movaps	xmm10,XMMWORD[((-152))+rax]
	movaps	xmm11,XMMWORD[((-136))+rax]
	movaps	xmm12,XMMWORD[((-120))+rax]
	movaps	xmm13,XMMWORD[((-104))+rax]
	movaps	xmm14,XMMWORD[((-88))+rax]
	movaps	xmm15,XMMWORD[((-72))+rax]
	mov	r15,QWORD[((-48))+rax]

	mov	r14,QWORD[((-40))+rax]

	mov	r13,QWORD[((-32))+rax]

	mov	r12,QWORD[((-24))+rax]

	mov	rbp,QWORD[((-16))+rax]

	mov	rbx,QWORD[((-8))+rax]

	lea	rsp,[rax]

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_1024_mul_avx2
$L$SEH_end_rsaz_1024_mul_avx2:
global	rsaz_1024_red2norm_avx2

ALIGN	32
SEH64_PROC_FRAME rsaz_1024_red2norm_avx2,PUBLIC

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	sub	rdx,-128
	xor	rax,rax
	mov	r8,QWORD[((-128))+rdx]
	mov	r9,QWORD[((-120))+rdx]
	mov	r10,QWORD[((-112))+rdx]
	shl	r8,0
	shl	r9,29
	mov	r11,r10
	shl	r10,58
	shr	r11,6
	add	rax,r8
	add	rax,r9
	add	rax,r10
	adc	r11,0
	mov	QWORD[rcx],rax
	mov	rax,r11
	mov	r8,QWORD[((-104))+rdx]
	mov	r9,QWORD[((-96))+rdx]
	shl	r8,23
	mov	r10,r9
	shl	r9,52
	shr	r10,12
	add	rax,r8
	add	rax,r9
	adc	r10,0
	mov	QWORD[8+rcx],rax
	mov	rax,r10
	mov	r11,QWORD[((-88))+rdx]
	mov	r8,QWORD[((-80))+rdx]
	shl	r11,17
	mov	r9,r8
	shl	r8,46
	shr	r9,18
	add	rax,r11
	add	rax,r8
	adc	r9,0
	mov	QWORD[16+rcx],rax
	mov	rax,r9
	mov	r10,QWORD[((-72))+rdx]
	mov	r11,QWORD[((-64))+rdx]
	shl	r10,11
	mov	r8,r11
	shl	r11,40
	shr	r8,24
	add	rax,r10
	add	rax,r11
	adc	r8,0
	mov	QWORD[24+rcx],rax
	mov	rax,r8
	mov	r9,QWORD[((-56))+rdx]
	mov	r10,QWORD[((-48))+rdx]
	mov	r11,QWORD[((-40))+rdx]
	shl	r9,5
	shl	r10,34
	mov	r8,r11
	shl	r11,63
	shr	r8,1
	add	rax,r9
	add	rax,r10
	add	rax,r11
	adc	r8,0
	mov	QWORD[32+rcx],rax
	mov	rax,r8
	mov	r9,QWORD[((-32))+rdx]
	mov	r10,QWORD[((-24))+rdx]
	shl	r9,28
	mov	r11,r10
	shl	r10,57
	shr	r11,7
	add	rax,r9
	add	rax,r10
	adc	r11,0
	mov	QWORD[40+rcx],rax
	mov	rax,r11
	mov	r8,QWORD[((-16))+rdx]
	mov	r9,QWORD[((-8))+rdx]
	shl	r8,22
	mov	r10,r9
	shl	r9,51
	shr	r10,13
	add	rax,r8
	add	rax,r9
	adc	r10,0
	mov	QWORD[48+rcx],rax
	mov	rax,r10
	mov	r11,QWORD[rdx]
	mov	r8,QWORD[8+rdx]
	shl	r11,16
	mov	r9,r8
	shl	r8,45
	shr	r9,19
	add	rax,r11
	add	rax,r8
	adc	r9,0
	mov	QWORD[56+rcx],rax
	mov	rax,r9
	mov	r10,QWORD[16+rdx]
	mov	r11,QWORD[24+rdx]
	shl	r10,10
	mov	r8,r11
	shl	r11,39
	shr	r8,25
	add	rax,r10
	add	rax,r11
	adc	r8,0
	mov	QWORD[64+rcx],rax
	mov	rax,r8
	mov	r9,QWORD[32+rdx]
	mov	r10,QWORD[40+rdx]
	mov	r11,QWORD[48+rdx]
	shl	r9,4
	shl	r10,33
	mov	r8,r11
	shl	r11,62
	shr	r8,2
	add	rax,r9
	add	rax,r10
	add	rax,r11
	adc	r8,0
	mov	QWORD[72+rcx],rax
	mov	rax,r8
	mov	r9,QWORD[56+rdx]
	mov	r10,QWORD[64+rdx]
	shl	r9,27
	mov	r11,r10
	shl	r10,56
	shr	r11,8
	add	rax,r9
	add	rax,r10
	adc	r11,0
	mov	QWORD[80+rcx],rax
	mov	rax,r11
	mov	r8,QWORD[72+rdx]
	mov	r9,QWORD[80+rdx]
	shl	r8,21
	mov	r10,r9
	shl	r9,50
	shr	r10,14
	add	rax,r8
	add	rax,r9
	adc	r10,0
	mov	QWORD[88+rcx],rax
	mov	rax,r10
	mov	r11,QWORD[88+rdx]
	mov	r8,QWORD[96+rdx]
	shl	r11,15
	mov	r9,r8
	shl	r8,44
	shr	r9,20
	add	rax,r11
	add	rax,r8
	adc	r9,0
	mov	QWORD[96+rcx],rax
	mov	rax,r9
	mov	r10,QWORD[104+rdx]
	mov	r11,QWORD[112+rdx]
	shl	r10,9
	mov	r8,r11
	shl	r11,38
	shr	r8,26
	add	rax,r10
	add	rax,r11
	adc	r8,0
	mov	QWORD[104+rcx],rax
	mov	rax,r8
	mov	r9,QWORD[120+rdx]
	mov	r10,QWORD[128+rdx]
	mov	r11,QWORD[136+rdx]
	shl	r9,3
	shl	r10,32
	mov	r8,r11
	shl	r11,61
	shr	r8,3
	add	rax,r9
	add	rax,r10
	add	rax,r11
	adc	r8,0
	mov	QWORD[112+rcx],rax
	mov	rax,r8
	mov	r9,QWORD[144+rdx]
	mov	r10,QWORD[152+rdx]
	shl	r9,26
	mov	r11,r10
	shl	r10,55
	shr	r11,9
	add	rax,r9
	add	rax,r10
	adc	r11,0
	mov	QWORD[120+rcx],rax
	mov	rax,r11
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_1024_red2norm_avx2


global	rsaz_1024_norm2red_avx2

ALIGN	32
SEH64_PROC_FRAME rsaz_1024_norm2red_avx2,PUBLIC

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	sub	rcx,-128
	mov	r8,QWORD[rdx]
	mov	eax,0x1fffffff
	mov	r9,QWORD[8+rdx]
	mov	r11,r8
	shr	r11,0
	and	r11,rax
	mov	QWORD[((-128))+rcx],r11
	mov	r10,r8
	shr	r10,29
	and	r10,rax
	mov	QWORD[((-120))+rcx],r10
	shrd	r8,r9,58
	and	r8,rax
	mov	QWORD[((-112))+rcx],r8
	mov	r10,QWORD[16+rdx]
	mov	r8,r9
	shr	r8,23
	and	r8,rax
	mov	QWORD[((-104))+rcx],r8
	shrd	r9,r10,52
	and	r9,rax
	mov	QWORD[((-96))+rcx],r9
	mov	r11,QWORD[24+rdx]
	mov	r9,r10
	shr	r9,17
	and	r9,rax
	mov	QWORD[((-88))+rcx],r9
	shrd	r10,r11,46
	and	r10,rax
	mov	QWORD[((-80))+rcx],r10
	mov	r8,QWORD[32+rdx]
	mov	r10,r11
	shr	r10,11
	and	r10,rax
	mov	QWORD[((-72))+rcx],r10
	shrd	r11,r8,40
	and	r11,rax
	mov	QWORD[((-64))+rcx],r11
	mov	r9,QWORD[40+rdx]
	mov	r11,r8
	shr	r11,5
	and	r11,rax
	mov	QWORD[((-56))+rcx],r11
	mov	r10,r8
	shr	r10,34
	and	r10,rax
	mov	QWORD[((-48))+rcx],r10
	shrd	r8,r9,63
	and	r8,rax
	mov	QWORD[((-40))+rcx],r8
	mov	r10,QWORD[48+rdx]
	mov	r8,r9
	shr	r8,28
	and	r8,rax
	mov	QWORD[((-32))+rcx],r8
	shrd	r9,r10,57
	and	r9,rax
	mov	QWORD[((-24))+rcx],r9
	mov	r11,QWORD[56+rdx]
	mov	r9,r10
	shr	r9,22
	and	r9,rax
	mov	QWORD[((-16))+rcx],r9
	shrd	r10,r11,51
	and	r10,rax
	mov	QWORD[((-8))+rcx],r10
	mov	r8,QWORD[64+rdx]
	mov	r10,r11
	shr	r10,16
	and	r10,rax
	mov	QWORD[rcx],r10
	shrd	r11,r8,45
	and	r11,rax
	mov	QWORD[8+rcx],r11
	mov	r9,QWORD[72+rdx]
	mov	r11,r8
	shr	r11,10
	and	r11,rax
	mov	QWORD[16+rcx],r11
	shrd	r8,r9,39
	and	r8,rax
	mov	QWORD[24+rcx],r8
	mov	r10,QWORD[80+rdx]
	mov	r8,r9
	shr	r8,4
	and	r8,rax
	mov	QWORD[32+rcx],r8
	mov	r11,r9
	shr	r11,33
	and	r11,rax
	mov	QWORD[40+rcx],r11
	shrd	r9,r10,62
	and	r9,rax
	mov	QWORD[48+rcx],r9
	mov	r11,QWORD[88+rdx]
	mov	r9,r10
	shr	r9,27
	and	r9,rax
	mov	QWORD[56+rcx],r9
	shrd	r10,r11,56
	and	r10,rax
	mov	QWORD[64+rcx],r10
	mov	r8,QWORD[96+rdx]
	mov	r10,r11
	shr	r10,21
	and	r10,rax
	mov	QWORD[72+rcx],r10
	shrd	r11,r8,50
	and	r11,rax
	mov	QWORD[80+rcx],r11
	mov	r9,QWORD[104+rdx]
	mov	r11,r8
	shr	r11,15
	and	r11,rax
	mov	QWORD[88+rcx],r11
	shrd	r8,r9,44
	and	r8,rax
	mov	QWORD[96+rcx],r8
	mov	r10,QWORD[112+rdx]
	mov	r8,r9
	shr	r8,9
	and	r8,rax
	mov	QWORD[104+rcx],r8
	shrd	r9,r10,38
	and	r9,rax
	mov	QWORD[112+rcx],r9
	mov	r11,QWORD[120+rdx]
	mov	r9,r10
	shr	r9,3
	and	r9,rax
	mov	QWORD[120+rcx],r9
	mov	r8,r10
	shr	r8,32
	and	r8,rax
	mov	QWORD[128+rcx],r8
	shrd	r10,r11,61
	and	r10,rax
	mov	QWORD[136+rcx],r10
	xor	r8,r8
	mov	r10,r11
	shr	r10,26
	and	r10,rax
	mov	QWORD[144+rcx],r10
	shrd	r11,r8,55
	and	r11,rax
	mov	QWORD[152+rcx],r11
	mov	QWORD[160+rcx],r8
	mov	QWORD[168+rcx],r8
	mov	QWORD[176+rcx],r8
	mov	QWORD[184+rcx],r8
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_1024_norm2red_avx2

global	rsaz_1024_scatter5_avx2

ALIGN	32
SEH64_PROC_FRAME rsaz_1024_scatter5_avx2,PUBLIC

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	vzeroupper
	vmovdqu	ymm5,YMMWORD[$L$scatter_permd]
	shl	r8d,4
	lea	rcx,[r8*1+rcx]
	mov	eax,9
	jmp	NEAR $L$oop_scatter_1024

ALIGN	32
$L$oop_scatter_1024:
	vmovdqu	ymm0,YMMWORD[rdx]
	lea	rdx,[32+rdx]
	vpermd	ymm0,ymm5,ymm0
	vmovdqu	XMMWORD[rcx],xmm0
	lea	rcx,[512+rcx]
	dec	eax
	jnz	NEAR $L$oop_scatter_1024

	vzeroupper
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_1024_scatter5_avx2


global	rsaz_1024_gather5_avx2

ALIGN	32
SEH64_PROC_FRAME rsaz_1024_gather5_avx2,PUBLIC

	vzeroupper
	mov	r11,rsp

	lea	rax,[((-136))+rsp]

DB	0x48,0x8d,0x60,0xe0
SEH64_ALLOCSTACK 168
DB	0xc5,0xf8,0x29,0x70,0xe0
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -176 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0xf8,0x29,0x78,0xf0
SEH64_SAVEXMM128 xmm7, 16	; .cfi_offset xmm7, -160 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x40,0x00
SEH64_SAVEXMM128 xmm8, 32	; .cfi_offset xmm8, -144 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x48,0x10
SEH64_SAVEXMM128 xmm9, 48	; .cfi_offset xmm9, -128 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x50,0x20
SEH64_SAVEXMM128 xmm10, 64	; .cfi_offset xmm10, -112 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x58,0x30
SEH64_SAVEXMM128 xmm11, 80	; .cfi_offset xmm11, -96 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x60,0x40
SEH64_SAVEXMM128 xmm12, 96	; .cfi_offset xmm12, -80 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x68,0x50
SEH64_SAVEXMM128 xmm13, 112	; .cfi_offset xmm13, -64 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x70,0x60
SEH64_SAVEXMM128 xmm14, 128	; .cfi_offset xmm14, -48 cfa_reg_offset=8 cfa_rsp=-176
DB	0xc5,0x78,0x29,0x78,0x70
SEH64_SAVEXMM128 xmm15, 144	; .cfi_offset xmm15, -32 cfa_reg_offset=8 cfa_rsp=-176
SEH64_ALLOCSTACK 8
SEH64_SETFRAME r11, 176
SEH64_ENDPROLOG	; cfa_reg=%r11 cfa_reg_offset=8 cfa_rsp=-176
	lea	rsp,[((-256))+rsp]
	and	rsp,-32
	lea	r10,[$L$inc]
	lea	rax,[((-128))+rsp]

	vmovd	xmm4,r8d
	vmovdqa	ymm0,YMMWORD[r10]
	vmovdqa	ymm1,YMMWORD[32+r10]
	vmovdqa	ymm5,YMMWORD[64+r10]
	vpbroadcastd	ymm4,xmm4

	vpaddd	ymm2,ymm0,ymm5
	vpcmpeqd	ymm0,ymm0,ymm4
	vpaddd	ymm3,ymm1,ymm5
	vpcmpeqd	ymm1,ymm1,ymm4
	vmovdqa	YMMWORD[(0+128)+rax],ymm0
	vpaddd	ymm0,ymm2,ymm5
	vpcmpeqd	ymm2,ymm2,ymm4
	vmovdqa	YMMWORD[(32+128)+rax],ymm1
	vpaddd	ymm1,ymm3,ymm5
	vpcmpeqd	ymm3,ymm3,ymm4
	vmovdqa	YMMWORD[(64+128)+rax],ymm2
	vpaddd	ymm2,ymm0,ymm5
	vpcmpeqd	ymm0,ymm0,ymm4
	vmovdqa	YMMWORD[(96+128)+rax],ymm3
	vpaddd	ymm3,ymm1,ymm5
	vpcmpeqd	ymm1,ymm1,ymm4
	vmovdqa	YMMWORD[(128+128)+rax],ymm0
	vpaddd	ymm8,ymm2,ymm5
	vpcmpeqd	ymm2,ymm2,ymm4
	vmovdqa	YMMWORD[(160+128)+rax],ymm1
	vpaddd	ymm9,ymm3,ymm5
	vpcmpeqd	ymm3,ymm3,ymm4
	vmovdqa	YMMWORD[(192+128)+rax],ymm2
	vpaddd	ymm10,ymm8,ymm5
	vpcmpeqd	ymm8,ymm8,ymm4
	vmovdqa	YMMWORD[(224+128)+rax],ymm3
	vpaddd	ymm11,ymm9,ymm5
	vpcmpeqd	ymm9,ymm9,ymm4
	vpaddd	ymm12,ymm10,ymm5
	vpcmpeqd	ymm10,ymm10,ymm4
	vpaddd	ymm13,ymm11,ymm5
	vpcmpeqd	ymm11,ymm11,ymm4
	vpaddd	ymm14,ymm12,ymm5
	vpcmpeqd	ymm12,ymm12,ymm4
	vpaddd	ymm15,ymm13,ymm5
	vpcmpeqd	ymm13,ymm13,ymm4
	vpcmpeqd	ymm14,ymm14,ymm4
	vpcmpeqd	ymm15,ymm15,ymm4

	vmovdqa	ymm7,YMMWORD[((-32))+r10]
	lea	rdx,[128+rdx]
	mov	r8d,9

$L$oop_gather_1024:
	vmovdqa	ymm0,YMMWORD[((0-128))+rdx]
	vmovdqa	ymm1,YMMWORD[((32-128))+rdx]
	vmovdqa	ymm2,YMMWORD[((64-128))+rdx]
	vmovdqa	ymm3,YMMWORD[((96-128))+rdx]
	vpand	ymm0,ymm0,YMMWORD[((0+128))+rax]
	vpand	ymm1,ymm1,YMMWORD[((32+128))+rax]
	vpand	ymm2,ymm2,YMMWORD[((64+128))+rax]
	vpor	ymm4,ymm1,ymm0
	vpand	ymm3,ymm3,YMMWORD[((96+128))+rax]
	vmovdqa	ymm0,YMMWORD[((128-128))+rdx]
	vmovdqa	ymm1,YMMWORD[((160-128))+rdx]
	vpor	ymm5,ymm3,ymm2
	vmovdqa	ymm2,YMMWORD[((192-128))+rdx]
	vmovdqa	ymm3,YMMWORD[((224-128))+rdx]
	vpand	ymm0,ymm0,YMMWORD[((128+128))+rax]
	vpand	ymm1,ymm1,YMMWORD[((160+128))+rax]
	vpand	ymm2,ymm2,YMMWORD[((192+128))+rax]
	vpor	ymm4,ymm4,ymm0
	vpand	ymm3,ymm3,YMMWORD[((224+128))+rax]
	vpand	ymm0,ymm8,YMMWORD[((256-128))+rdx]
	vpor	ymm5,ymm5,ymm1
	vpand	ymm1,ymm9,YMMWORD[((288-128))+rdx]
	vpor	ymm4,ymm4,ymm2
	vpand	ymm2,ymm10,YMMWORD[((320-128))+rdx]
	vpor	ymm5,ymm5,ymm3
	vpand	ymm3,ymm11,YMMWORD[((352-128))+rdx]
	vpor	ymm4,ymm4,ymm0
	vpand	ymm0,ymm12,YMMWORD[((384-128))+rdx]
	vpor	ymm5,ymm5,ymm1
	vpand	ymm1,ymm13,YMMWORD[((416-128))+rdx]
	vpor	ymm4,ymm4,ymm2
	vpand	ymm2,ymm14,YMMWORD[((448-128))+rdx]
	vpor	ymm5,ymm5,ymm3
	vpand	ymm3,ymm15,YMMWORD[((480-128))+rdx]
	lea	rdx,[512+rdx]
	vpor	ymm4,ymm4,ymm0
	vpor	ymm5,ymm5,ymm1
	vpor	ymm4,ymm4,ymm2
	vpor	ymm5,ymm5,ymm3

	vpor	ymm4,ymm4,ymm5
	vextracti128	xmm5,ymm4,1
	vpor	xmm5,xmm5,xmm4
	vpermd	ymm5,ymm7,ymm5
	vmovdqu	YMMWORD[rcx],ymm5
	lea	rcx,[32+rcx]
	dec	r8d
	jnz	NEAR $L$oop_gather_1024

	vpxor	ymm0,ymm0,ymm0
	vmovdqu	YMMWORD[rcx],ymm0
	vzeroupper
	movaps	xmm6,XMMWORD[((-168))+r11]
	movaps	xmm7,XMMWORD[((-152))+r11]
	movaps	xmm8,XMMWORD[((-136))+r11]
	movaps	xmm9,XMMWORD[((-120))+r11]
	movaps	xmm10,XMMWORD[((-104))+r11]
	movaps	xmm11,XMMWORD[((-88))+r11]
	movaps	xmm12,XMMWORD[((-72))+r11]
	movaps	xmm13,XMMWORD[((-56))+r11]
	movaps	xmm14,XMMWORD[((-40))+r11]
	movaps	xmm15,XMMWORD[((-24))+r11]
	lea	rsp,[r11]

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_1024_gather5_avx2

EXTERN	OPENSSL_ia32cap_P
global	rsaz_avx2_eligible

ALIGN	32
SEH64_PROC_FRAME rsaz_avx2_eligible,PUBLIC

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	eax,DWORD[((OPENSSL_ia32cap_P+8))]
	mov	ecx,524544
	mov	edx,0
	and	ecx,eax
	cmp	ecx,524544
	cmove	eax,edx
	and	eax,32
	shr	eax,5
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_avx2_eligible


section	.rdata rdata align=64
ALIGN	64
$L$and_mask:
	DQ	0x1fffffff,0x1fffffff,0x1fffffff,0x1fffffff
$L$scatter_permd:
	DD	0,2,4,6,7,7,7,7
$L$gather_permd:
	DD	0,7,1,7,2,7,3,7
$L$inc:
	DD	0,0,0,0,1,1,1,1
	DD	2,2,2,2,3,3,3,3
	DD	4,4,4,4,4,4,4,4
section .text
ALIGN	64
