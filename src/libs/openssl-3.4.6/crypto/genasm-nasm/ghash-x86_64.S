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

global	gcm_gmult_4bit

ALIGN	16
SEH64_PROC_FRAME gcm_gmult_4bit,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx



DB	243,15,30,250
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
	sub	rsp,280
SEH64_ALLOCSTACK 280
	; cfa_rsp=-336
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=336 cfa_rsp=-336

	movzx	r8,BYTE[15+rdi]
	lea	r11,[$L$rem_4bit]
	xor	rax,rax
	xor	rbx,rbx
	mov	al,r8b
	mov	bl,r8b
	shl	al,4
	mov	rcx,14
	mov	r8,QWORD[8+rax*1+rsi]
	mov	r9,QWORD[rax*1+rsi]
	and	bl,0xf0
	mov	rdx,r8
	jmp	NEAR $L$oop1

ALIGN	16
$L$oop1:
	shr	r8,4
	and	rdx,0xf
	mov	r10,r9
	mov	al,BYTE[rcx*1+rdi]
	shr	r9,4
	xor	r8,QWORD[8+rbx*1+rsi]
	shl	r10,60
	xor	r9,QWORD[rbx*1+rsi]
	mov	bl,al
	xor	r9,QWORD[rdx*8+r11]
	mov	rdx,r8
	shl	al,4
	xor	r8,r10
	dec	rcx
	js	NEAR $L$break1

	shr	r8,4
	and	rdx,0xf
	mov	r10,r9
	shr	r9,4
	xor	r8,QWORD[8+rax*1+rsi]
	shl	r10,60
	xor	r9,QWORD[rax*1+rsi]
	and	bl,0xf0
	xor	r9,QWORD[rdx*8+r11]
	mov	rdx,r8
	xor	r8,r10
	jmp	NEAR $L$oop1

ALIGN	16
$L$break1:
	shr	r8,4
	and	rdx,0xf
	mov	r10,r9
	shr	r9,4
	xor	r8,QWORD[8+rax*1+rsi]
	shl	r10,60
	xor	r9,QWORD[rax*1+rsi]
	and	bl,0xf0
	xor	r9,QWORD[rdx*8+r11]
	mov	rdx,r8
	xor	r8,r10

	shr	r8,4
	and	rdx,0xf
	mov	r10,r9
	shr	r9,4
	xor	r8,QWORD[8+rbx*1+rsi]
	shl	r10,60
	xor	r9,QWORD[rbx*1+rsi]
	xor	r8,r10
	xor	r9,QWORD[rdx*8+r11]

	bswap	r8
	bswap	r9
	mov	QWORD[8+rdi],r8
	mov	QWORD[rdi],r9

	lea	rsi,[((280+48))+rsp]

	mov	rbx,QWORD[((-8))+rsi]

	lea	rsp,[rsi]

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	gcm_gmult_4bit
$L$SEH_end_gcm_gmult_4bit:
global	gcm_ghash_4bit

ALIGN	16
SEH64_PROC_FRAME gcm_ghash_4bit,PUBLIC
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
	sub	rsp,280
SEH64_ALLOCSTACK 280
	; cfa_rsp=-336
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=336 cfa_rsp=-336
	mov	r14,rdx
	mov	r15,rcx
	sub	rsi,-128
	lea	rbp,[((16+128))+rsp]
	xor	edx,edx
	mov	r8,QWORD[((0+0-128))+rsi]
	mov	rax,QWORD[((0+8-128))+rsi]
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	r9,QWORD[((16+0-128))+rsi]
	shl	dl,4
	mov	rbx,QWORD[((16+8-128))+rsi]
	shl	r10,60
	mov	BYTE[rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[rbp],r8
	mov	r8,QWORD[((32+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((0-128))+rbp],rax
	mov	rax,QWORD[((32+8-128))+rsi]
	shl	r10,60
	mov	BYTE[1+rsp],dl
	or	rbx,r10
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	QWORD[8+rbp],r9
	mov	r9,QWORD[((48+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((8-128))+rbp],rbx
	mov	rbx,QWORD[((48+8-128))+rsi]
	shl	r10,60
	mov	BYTE[2+rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[16+rbp],r8
	mov	r8,QWORD[((64+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((16-128))+rbp],rax
	mov	rax,QWORD[((64+8-128))+rsi]
	shl	r10,60
	mov	BYTE[3+rsp],dl
	or	rbx,r10
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	QWORD[24+rbp],r9
	mov	r9,QWORD[((80+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((24-128))+rbp],rbx
	mov	rbx,QWORD[((80+8-128))+rsi]
	shl	r10,60
	mov	BYTE[4+rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[32+rbp],r8
	mov	r8,QWORD[((96+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((32-128))+rbp],rax
	mov	rax,QWORD[((96+8-128))+rsi]
	shl	r10,60
	mov	BYTE[5+rsp],dl
	or	rbx,r10
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	QWORD[40+rbp],r9
	mov	r9,QWORD[((112+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((40-128))+rbp],rbx
	mov	rbx,QWORD[((112+8-128))+rsi]
	shl	r10,60
	mov	BYTE[6+rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[48+rbp],r8
	mov	r8,QWORD[((128+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((48-128))+rbp],rax
	mov	rax,QWORD[((128+8-128))+rsi]
	shl	r10,60
	mov	BYTE[7+rsp],dl
	or	rbx,r10
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	QWORD[56+rbp],r9
	mov	r9,QWORD[((144+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((56-128))+rbp],rbx
	mov	rbx,QWORD[((144+8-128))+rsi]
	shl	r10,60
	mov	BYTE[8+rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[64+rbp],r8
	mov	r8,QWORD[((160+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((64-128))+rbp],rax
	mov	rax,QWORD[((160+8-128))+rsi]
	shl	r10,60
	mov	BYTE[9+rsp],dl
	or	rbx,r10
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	QWORD[72+rbp],r9
	mov	r9,QWORD[((176+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((72-128))+rbp],rbx
	mov	rbx,QWORD[((176+8-128))+rsi]
	shl	r10,60
	mov	BYTE[10+rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[80+rbp],r8
	mov	r8,QWORD[((192+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((80-128))+rbp],rax
	mov	rax,QWORD[((192+8-128))+rsi]
	shl	r10,60
	mov	BYTE[11+rsp],dl
	or	rbx,r10
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	QWORD[88+rbp],r9
	mov	r9,QWORD[((208+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((88-128))+rbp],rbx
	mov	rbx,QWORD[((208+8-128))+rsi]
	shl	r10,60
	mov	BYTE[12+rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[96+rbp],r8
	mov	r8,QWORD[((224+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((96-128))+rbp],rax
	mov	rax,QWORD[((224+8-128))+rsi]
	shl	r10,60
	mov	BYTE[13+rsp],dl
	or	rbx,r10
	mov	dl,al
	shr	rax,4
	mov	r10,r8
	shr	r8,4
	mov	QWORD[104+rbp],r9
	mov	r9,QWORD[((240+0-128))+rsi]
	shl	dl,4
	mov	QWORD[((104-128))+rbp],rbx
	mov	rbx,QWORD[((240+8-128))+rsi]
	shl	r10,60
	mov	BYTE[14+rsp],dl
	or	rax,r10
	mov	dl,bl
	shr	rbx,4
	mov	r10,r9
	shr	r9,4
	mov	QWORD[112+rbp],r8
	shl	dl,4
	mov	QWORD[((112-128))+rbp],rax
	shl	r10,60
	mov	BYTE[15+rsp],dl
	or	rbx,r10
	mov	QWORD[120+rbp],r9
	mov	QWORD[((120-128))+rbp],rbx
	add	rsi,-128
	mov	r8,QWORD[8+rdi]
	mov	r9,QWORD[rdi]
	add	r15,r14
	lea	r11,[$L$rem_8bit]
	jmp	NEAR $L$outer_loop
ALIGN	16
$L$outer_loop:
	xor	r9,QWORD[r14]
	mov	rdx,QWORD[8+r14]
	lea	r14,[16+r14]
	xor	rdx,r8
	mov	QWORD[rdi],r9
	mov	QWORD[8+rdi],rdx
	shr	rdx,32
	xor	rax,rax
	rol	edx,8
	mov	al,dl
	movzx	ebx,dl
	shl	al,4
	shr	ebx,4
	rol	edx,8
	mov	r8,QWORD[8+rax*1+rsi]
	mov	r9,QWORD[rax*1+rsi]
	mov	al,dl
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	shr	ecx,4
	xor	r12,r8
	mov	r10,r9
	shr	r8,8
	movzx	r12,r12b
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r12,WORD[r12*2+r11]
	movzx	ebx,dl
	shl	al,4
	movzx	r13,BYTE[rcx*1+rsp]
	shr	ebx,4
	shl	r12,48
	xor	r13,r8
	mov	r10,r9
	xor	r9,r12
	shr	r8,8
	movzx	r13,r13b
	shr	r9,8
	xor	r8,QWORD[((-128))+rcx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rcx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r13,WORD[r13*2+r11]
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	shr	ecx,4
	shl	r13,48
	xor	r12,r8
	mov	r10,r9
	xor	r9,r13
	shr	r8,8
	movzx	r12,r12b
	mov	edx,DWORD[8+rdi]
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r12,WORD[r12*2+r11]
	movzx	ebx,dl
	shl	al,4
	movzx	r13,BYTE[rcx*1+rsp]
	shr	ebx,4
	shl	r12,48
	xor	r13,r8
	mov	r10,r9
	xor	r9,r12
	shr	r8,8
	movzx	r13,r13b
	shr	r9,8
	xor	r8,QWORD[((-128))+rcx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rcx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r13,WORD[r13*2+r11]
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	shr	ecx,4
	shl	r13,48
	xor	r12,r8
	mov	r10,r9
	xor	r9,r13
	shr	r8,8
	movzx	r12,r12b
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r12,WORD[r12*2+r11]
	movzx	ebx,dl
	shl	al,4
	movzx	r13,BYTE[rcx*1+rsp]
	shr	ebx,4
	shl	r12,48
	xor	r13,r8
	mov	r10,r9
	xor	r9,r12
	shr	r8,8
	movzx	r13,r13b
	shr	r9,8
	xor	r8,QWORD[((-128))+rcx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rcx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r13,WORD[r13*2+r11]
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	shr	ecx,4
	shl	r13,48
	xor	r12,r8
	mov	r10,r9
	xor	r9,r13
	shr	r8,8
	movzx	r12,r12b
	mov	edx,DWORD[4+rdi]
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r12,WORD[r12*2+r11]
	movzx	ebx,dl
	shl	al,4
	movzx	r13,BYTE[rcx*1+rsp]
	shr	ebx,4
	shl	r12,48
	xor	r13,r8
	mov	r10,r9
	xor	r9,r12
	shr	r8,8
	movzx	r13,r13b
	shr	r9,8
	xor	r8,QWORD[((-128))+rcx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rcx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r13,WORD[r13*2+r11]
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	shr	ecx,4
	shl	r13,48
	xor	r12,r8
	mov	r10,r9
	xor	r9,r13
	shr	r8,8
	movzx	r12,r12b
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r12,WORD[r12*2+r11]
	movzx	ebx,dl
	shl	al,4
	movzx	r13,BYTE[rcx*1+rsp]
	shr	ebx,4
	shl	r12,48
	xor	r13,r8
	mov	r10,r9
	xor	r9,r12
	shr	r8,8
	movzx	r13,r13b
	shr	r9,8
	xor	r8,QWORD[((-128))+rcx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rcx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r13,WORD[r13*2+r11]
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	shr	ecx,4
	shl	r13,48
	xor	r12,r8
	mov	r10,r9
	xor	r9,r13
	shr	r8,8
	movzx	r12,r12b
	mov	edx,DWORD[rdi]
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r12,WORD[r12*2+r11]
	movzx	ebx,dl
	shl	al,4
	movzx	r13,BYTE[rcx*1+rsp]
	shr	ebx,4
	shl	r12,48
	xor	r13,r8
	mov	r10,r9
	xor	r9,r12
	shr	r8,8
	movzx	r13,r13b
	shr	r9,8
	xor	r8,QWORD[((-128))+rcx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rcx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r13,WORD[r13*2+r11]
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	shr	ecx,4
	shl	r13,48
	xor	r12,r8
	mov	r10,r9
	xor	r9,r13
	shr	r8,8
	movzx	r12,r12b
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r12,WORD[r12*2+r11]
	movzx	ebx,dl
	shl	al,4
	movzx	r13,BYTE[rcx*1+rsp]
	shr	ebx,4
	shl	r12,48
	xor	r13,r8
	mov	r10,r9
	xor	r9,r12
	shr	r8,8
	movzx	r13,r13b
	shr	r9,8
	xor	r8,QWORD[((-128))+rcx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rcx*8+rbp]
	rol	edx,8
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	mov	al,dl
	xor	r8,r10
	movzx	r13,WORD[r13*2+r11]
	movzx	ecx,dl
	shl	al,4
	movzx	r12,BYTE[rbx*1+rsp]
	and	ecx,240
	shl	r13,48
	xor	r12,r8
	mov	r10,r9
	xor	r9,r13
	shr	r8,8
	movzx	r12,r12b
	mov	edx,DWORD[((-4))+rdi]
	shr	r9,8
	xor	r8,QWORD[((-128))+rbx*8+rbp]
	shl	r10,56
	xor	r9,QWORD[rbx*8+rbp]
	movzx	r12,WORD[r12*2+r11]
	xor	r8,QWORD[8+rax*1+rsi]
	xor	r9,QWORD[rax*1+rsi]
	shl	r12,48
	xor	r8,r10
	xor	r9,r12
	movzx	r13,r8b
	shr	r8,4
	mov	r10,r9
	shl	r13b,4
	shr	r9,4
	xor	r8,QWORD[8+rcx*1+rsi]
	movzx	r13,WORD[r13*2+r11]
	shl	r10,60
	xor	r9,QWORD[rcx*1+rsi]
	xor	r8,r10
	shl	r13,48
	bswap	r8
	xor	r9,r13
	bswap	r9
	cmp	r14,r15
	jb	NEAR $L$outer_loop
	mov	QWORD[8+rdi],r8
	mov	QWORD[rdi],r9

	lea	rsi,[((280+48))+rsp]

	mov	r15,QWORD[((-48))+rsi]

	mov	r14,QWORD[((-40))+rsi]

	mov	r13,QWORD[((-32))+rsi]

	mov	r12,QWORD[((-24))+rsi]

	mov	rbp,QWORD[((-16))+rsi]

	mov	rbx,QWORD[((-8))+rsi]

	lea	rsp,[rsi]

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	gcm_ghash_4bit
$L$SEH_end_gcm_ghash_4bit:
global	gcm_init_clmul

ALIGN	16
SEH64_PROC_FRAME gcm_init_clmul,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
$L$_init_clmul:

DB	0x48,0x83,0xec,0x18
DB	0x0f,0x29,0x34,0x24
	movdqu	xmm2,XMMWORD[rdx]
	pshufd	xmm2,xmm2,78


	pshufd	xmm4,xmm2,255
	movdqa	xmm3,xmm2
	psllq	xmm2,1
	pxor	xmm5,xmm5
	psrlq	xmm3,63
	pcmpgtd	xmm5,xmm4
	pslldq	xmm3,8
	por	xmm2,xmm3


	pand	xmm5,XMMWORD[$L$0x1c2_polynomial]
	pxor	xmm2,xmm5


	pshufd	xmm6,xmm2,78
	movdqa	xmm0,xmm2
	pxor	xmm6,xmm2
	movdqa	xmm1,xmm0
	pshufd	xmm3,xmm0,78
	pxor	xmm3,xmm0
DB	102,15,58,68,194,0
DB	102,15,58,68,202,17
DB	102,15,58,68,222,0
	pxor	xmm3,xmm0
	pxor	xmm3,xmm1

	movdqa	xmm4,xmm3
	psrldq	xmm3,8
	pslldq	xmm4,8
	pxor	xmm1,xmm3
	pxor	xmm0,xmm4

	movdqa	xmm4,xmm0
	movdqa	xmm3,xmm0
	psllq	xmm0,5
	pxor	xmm3,xmm0
	psllq	xmm0,1
	pxor	xmm0,xmm3
	psllq	xmm0,57
	movdqa	xmm3,xmm0
	pslldq	xmm0,8
	psrldq	xmm3,8
	pxor	xmm0,xmm4
	pxor	xmm1,xmm3


	movdqa	xmm4,xmm0
	psrlq	xmm0,1
	pxor	xmm1,xmm4
	pxor	xmm4,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm4
	psrlq	xmm0,1
	pxor	xmm0,xmm1
	pshufd	xmm3,xmm2,78
	pshufd	xmm4,xmm0,78
	pxor	xmm3,xmm2
	movdqu	XMMWORD[rcx],xmm2
	pxor	xmm4,xmm0
	movdqu	XMMWORD[16+rcx],xmm0
DB	102,15,58,15,227,8
	movdqu	XMMWORD[32+rcx],xmm4
	movdqa	xmm1,xmm0
	pshufd	xmm3,xmm0,78
	pxor	xmm3,xmm0
DB	102,15,58,68,194,0
DB	102,15,58,68,202,17
DB	102,15,58,68,222,0
	pxor	xmm3,xmm0
	pxor	xmm3,xmm1

	movdqa	xmm4,xmm3
	psrldq	xmm3,8
	pslldq	xmm4,8
	pxor	xmm1,xmm3
	pxor	xmm0,xmm4

	movdqa	xmm4,xmm0
	movdqa	xmm3,xmm0
	psllq	xmm0,5
	pxor	xmm3,xmm0
	psllq	xmm0,1
	pxor	xmm0,xmm3
	psllq	xmm0,57
	movdqa	xmm3,xmm0
	pslldq	xmm0,8
	psrldq	xmm3,8
	pxor	xmm0,xmm4
	pxor	xmm1,xmm3


	movdqa	xmm4,xmm0
	psrlq	xmm0,1
	pxor	xmm1,xmm4
	pxor	xmm4,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm4
	psrlq	xmm0,1
	pxor	xmm0,xmm1
	movdqa	xmm5,xmm0
	movdqa	xmm1,xmm0
	pshufd	xmm3,xmm0,78
	pxor	xmm3,xmm0
DB	102,15,58,68,194,0
DB	102,15,58,68,202,17
DB	102,15,58,68,222,0
	pxor	xmm3,xmm0
	pxor	xmm3,xmm1

	movdqa	xmm4,xmm3
	psrldq	xmm3,8
	pslldq	xmm4,8
	pxor	xmm1,xmm3
	pxor	xmm0,xmm4

	movdqa	xmm4,xmm0
	movdqa	xmm3,xmm0
	psllq	xmm0,5
	pxor	xmm3,xmm0
	psllq	xmm0,1
	pxor	xmm0,xmm3
	psllq	xmm0,57
	movdqa	xmm3,xmm0
	pslldq	xmm0,8
	psrldq	xmm3,8
	pxor	xmm0,xmm4
	pxor	xmm1,xmm3


	movdqa	xmm4,xmm0
	psrlq	xmm0,1
	pxor	xmm1,xmm4
	pxor	xmm4,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm4
	psrlq	xmm0,1
	pxor	xmm0,xmm1
	pshufd	xmm3,xmm5,78
	pshufd	xmm4,xmm0,78
	pxor	xmm3,xmm5
	movdqu	XMMWORD[48+rcx],xmm5
	pxor	xmm4,xmm0
	movdqu	XMMWORD[64+rcx],xmm0
DB	102,15,58,15,227,8
	movdqu	XMMWORD[80+rcx],xmm4
	movaps	xmm6,XMMWORD[rsp]
	lea	rsp,[24+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	gcm_init_clmul

global	gcm_gmult_clmul

ALIGN	16
SEH64_PROC_FRAME gcm_gmult_clmul,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
$L$_gmult_clmul:
	movdqu	xmm0,XMMWORD[rcx]
	movdqa	xmm5,XMMWORD[$L$bswap_mask]
	movdqu	xmm2,XMMWORD[rdx]
	movdqu	xmm4,XMMWORD[32+rdx]
DB	102,15,56,0,197
	movdqa	xmm1,xmm0
	pshufd	xmm3,xmm0,78
	pxor	xmm3,xmm0
DB	102,15,58,68,194,0
DB	102,15,58,68,202,17
DB	102,15,58,68,220,0
	pxor	xmm3,xmm0
	pxor	xmm3,xmm1

	movdqa	xmm4,xmm3
	psrldq	xmm3,8
	pslldq	xmm4,8
	pxor	xmm1,xmm3
	pxor	xmm0,xmm4

	movdqa	xmm4,xmm0
	movdqa	xmm3,xmm0
	psllq	xmm0,5
	pxor	xmm3,xmm0
	psllq	xmm0,1
	pxor	xmm0,xmm3
	psllq	xmm0,57
	movdqa	xmm3,xmm0
	pslldq	xmm0,8
	psrldq	xmm3,8
	pxor	xmm0,xmm4
	pxor	xmm1,xmm3


	movdqa	xmm4,xmm0
	psrlq	xmm0,1
	pxor	xmm1,xmm4
	pxor	xmm4,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm4
	psrlq	xmm0,1
	pxor	xmm0,xmm1
DB	102,15,56,0,197
	movdqu	XMMWORD[rcx],xmm0
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	gcm_gmult_clmul

global	gcm_ghash_clmul

ALIGN	32
SEH64_PROC_FRAME gcm_ghash_clmul,PUBLIC

DB	243,15,30,250
$L$_ghash_clmul:
	lea	rax,[((-136))+rsp]

DB	0x48,0x8d,0x60,0xe0
SEH64_ALLOCSTACK 168
DB	0x0f,0x29,0x70,0xe0
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -176 cfa_reg_offset=176 cfa_rsp=-176
DB	0x0f,0x29,0x78,0xf0
SEH64_SAVEXMM128 xmm7, 16	; .cfi_offset xmm7, -160 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x00
SEH64_SAVEXMM128 xmm8, 32	; .cfi_offset xmm8, -144 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x48,0x10
SEH64_SAVEXMM128 xmm9, 48	; .cfi_offset xmm9, -128 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x50,0x20
SEH64_SAVEXMM128 xmm10, 64	; .cfi_offset xmm10, -112 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x58,0x30
SEH64_SAVEXMM128 xmm11, 80	; .cfi_offset xmm11, -96 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x60,0x40
SEH64_SAVEXMM128 xmm12, 96	; .cfi_offset xmm12, -80 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x68,0x50
SEH64_SAVEXMM128 xmm13, 112	; .cfi_offset xmm13, -64 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x70,0x60
SEH64_SAVEXMM128 xmm14, 128	; .cfi_offset xmm14, -48 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x78,0x70
SEH64_SAVEXMM128 xmm15, 144	; .cfi_offset xmm15, -32 cfa_reg_offset=176 cfa_rsp=-176
	; cfa_rsp=-176
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=176 cfa_rsp=-176
	movdqa	xmm10,XMMWORD[$L$bswap_mask]

	movdqu	xmm0,XMMWORD[rcx]
	movdqu	xmm2,XMMWORD[rdx]
	movdqu	xmm7,XMMWORD[32+rdx]
DB	102,65,15,56,0,194

	sub	r9,0x10
	jz	NEAR $L$odd_tail

	movdqu	xmm6,XMMWORD[16+rdx]
	mov	eax,DWORD[((OPENSSL_ia32cap_P+4))]
	cmp	r9,0x30
	jb	NEAR $L$skip4x

	and	eax,71303168
	cmp	eax,4194304
	je	NEAR $L$skip4x

	sub	r9,0x30
	mov	rax,0xA040608020C0E000
	movdqu	xmm14,XMMWORD[48+rdx]
	movdqu	xmm15,XMMWORD[64+rdx]




	movdqu	xmm3,XMMWORD[48+r8]
	movdqu	xmm11,XMMWORD[32+r8]
DB	102,65,15,56,0,218
DB	102,69,15,56,0,218
	movdqa	xmm5,xmm3
	pshufd	xmm4,xmm3,78
	pxor	xmm4,xmm3
DB	102,15,58,68,218,0
DB	102,15,58,68,234,17
DB	102,15,58,68,231,0

	movdqa	xmm13,xmm11
	pshufd	xmm12,xmm11,78
	pxor	xmm12,xmm11
DB	102,68,15,58,68,222,0
DB	102,68,15,58,68,238,17
DB	102,68,15,58,68,231,16
	xorps	xmm3,xmm11
	xorps	xmm5,xmm13
	movups	xmm7,XMMWORD[80+rdx]
	xorps	xmm4,xmm12

	movdqu	xmm11,XMMWORD[16+r8]
	movdqu	xmm8,XMMWORD[r8]
DB	102,69,15,56,0,218
DB	102,69,15,56,0,194
	movdqa	xmm13,xmm11
	pshufd	xmm12,xmm11,78
	pxor	xmm0,xmm8
	pxor	xmm12,xmm11
DB	102,69,15,58,68,222,0
	movdqa	xmm1,xmm0
	pshufd	xmm8,xmm0,78
	pxor	xmm8,xmm0
DB	102,69,15,58,68,238,17
DB	102,68,15,58,68,231,0
	xorps	xmm3,xmm11
	xorps	xmm5,xmm13

	lea	r8,[64+r8]
	sub	r9,0x40
	jc	NEAR $L$tail4x

	jmp	NEAR $L$mod4_loop
ALIGN	32
$L$mod4_loop:
DB	102,65,15,58,68,199,0
	xorps	xmm4,xmm12
	movdqu	xmm11,XMMWORD[48+r8]
DB	102,69,15,56,0,218
DB	102,65,15,58,68,207,17
	xorps	xmm0,xmm3
	movdqu	xmm3,XMMWORD[32+r8]
	movdqa	xmm13,xmm11
DB	102,68,15,58,68,199,16
	pshufd	xmm12,xmm11,78
	xorps	xmm1,xmm5
	pxor	xmm12,xmm11
DB	102,65,15,56,0,218
	movups	xmm7,XMMWORD[32+rdx]
	xorps	xmm8,xmm4
DB	102,68,15,58,68,218,0
	pshufd	xmm4,xmm3,78

	pxor	xmm8,xmm0
	movdqa	xmm5,xmm3
	pxor	xmm8,xmm1
	pxor	xmm4,xmm3
	movdqa	xmm9,xmm8
DB	102,68,15,58,68,234,17
	pslldq	xmm8,8
	psrldq	xmm9,8
	pxor	xmm0,xmm8
	movdqa	xmm8,XMMWORD[$L$7_mask]
	pxor	xmm1,xmm9
DB	102,76,15,110,200

	pand	xmm8,xmm0
DB	102,69,15,56,0,200
	pxor	xmm9,xmm0
DB	102,68,15,58,68,231,0
	psllq	xmm9,57
	movdqa	xmm8,xmm9
	pslldq	xmm9,8
DB	102,15,58,68,222,0
	psrldq	xmm8,8
	pxor	xmm0,xmm9
	pxor	xmm1,xmm8
	movdqu	xmm8,XMMWORD[r8]

	movdqa	xmm9,xmm0
	psrlq	xmm0,1
DB	102,15,58,68,238,17
	xorps	xmm3,xmm11
	movdqu	xmm11,XMMWORD[16+r8]
DB	102,69,15,56,0,218
DB	102,15,58,68,231,16
	xorps	xmm5,xmm13
	movups	xmm7,XMMWORD[80+rdx]
DB	102,69,15,56,0,194
	pxor	xmm1,xmm9
	pxor	xmm9,xmm0
	psrlq	xmm0,5

	movdqa	xmm13,xmm11
	pxor	xmm4,xmm12
	pshufd	xmm12,xmm11,78
	pxor	xmm0,xmm9
	pxor	xmm1,xmm8
	pxor	xmm12,xmm11
DB	102,69,15,58,68,222,0
	psrlq	xmm0,1
	pxor	xmm0,xmm1
	movdqa	xmm1,xmm0
DB	102,69,15,58,68,238,17
	xorps	xmm3,xmm11
	pshufd	xmm8,xmm0,78
	pxor	xmm8,xmm0

DB	102,68,15,58,68,231,0
	xorps	xmm5,xmm13

	lea	r8,[64+r8]
	sub	r9,0x40
	jnc	NEAR $L$mod4_loop

$L$tail4x:
DB	102,65,15,58,68,199,0
DB	102,65,15,58,68,207,17
DB	102,68,15,58,68,199,16
	xorps	xmm4,xmm12
	xorps	xmm0,xmm3
	xorps	xmm1,xmm5
	pxor	xmm1,xmm0
	pxor	xmm8,xmm4

	pxor	xmm8,xmm1
	pxor	xmm1,xmm0

	movdqa	xmm9,xmm8
	psrldq	xmm8,8
	pslldq	xmm9,8
	pxor	xmm1,xmm8
	pxor	xmm0,xmm9

	movdqa	xmm4,xmm0
	movdqa	xmm3,xmm0
	psllq	xmm0,5
	pxor	xmm3,xmm0
	psllq	xmm0,1
	pxor	xmm0,xmm3
	psllq	xmm0,57
	movdqa	xmm3,xmm0
	pslldq	xmm0,8
	psrldq	xmm3,8
	pxor	xmm0,xmm4
	pxor	xmm1,xmm3


	movdqa	xmm4,xmm0
	psrlq	xmm0,1
	pxor	xmm1,xmm4
	pxor	xmm4,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm4
	psrlq	xmm0,1
	pxor	xmm0,xmm1
	add	r9,0x40
	jz	NEAR $L$done
	movdqu	xmm7,XMMWORD[32+rdx]
	sub	r9,0x10
	jz	NEAR $L$odd_tail
$L$skip4x:





	movdqu	xmm8,XMMWORD[r8]
	movdqu	xmm3,XMMWORD[16+r8]
DB	102,69,15,56,0,194
DB	102,65,15,56,0,218
	pxor	xmm0,xmm8

	movdqa	xmm5,xmm3
	pshufd	xmm4,xmm3,78
	pxor	xmm4,xmm3
DB	102,15,58,68,218,0
DB	102,15,58,68,234,17
DB	102,15,58,68,231,0

	lea	r8,[32+r8]
	nop
	sub	r9,0x20
	jbe	NEAR $L$even_tail
	nop
	jmp	NEAR $L$mod_loop

ALIGN	32
$L$mod_loop:
	movdqa	xmm1,xmm0
	movdqa	xmm8,xmm4
	pshufd	xmm4,xmm0,78
	pxor	xmm4,xmm0

DB	102,15,58,68,198,0
DB	102,15,58,68,206,17
DB	102,15,58,68,231,16

	pxor	xmm0,xmm3
	pxor	xmm1,xmm5
	movdqu	xmm9,XMMWORD[r8]
	pxor	xmm8,xmm0
DB	102,69,15,56,0,202
	movdqu	xmm3,XMMWORD[16+r8]

	pxor	xmm8,xmm1
	pxor	xmm1,xmm9
	pxor	xmm4,xmm8
DB	102,65,15,56,0,218
	movdqa	xmm8,xmm4
	psrldq	xmm8,8
	pslldq	xmm4,8
	pxor	xmm1,xmm8
	pxor	xmm0,xmm4

	movdqa	xmm5,xmm3

	movdqa	xmm9,xmm0
	movdqa	xmm8,xmm0
	psllq	xmm0,5
	pxor	xmm8,xmm0
DB	102,15,58,68,218,0
	psllq	xmm0,1
	pxor	xmm0,xmm8
	psllq	xmm0,57
	movdqa	xmm8,xmm0
	pslldq	xmm0,8
	psrldq	xmm8,8
	pxor	xmm0,xmm9
	pshufd	xmm4,xmm5,78
	pxor	xmm1,xmm8
	pxor	xmm4,xmm5

	movdqa	xmm9,xmm0
	psrlq	xmm0,1
DB	102,15,58,68,234,17
	pxor	xmm1,xmm9
	pxor	xmm9,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm9
	lea	r8,[32+r8]
	psrlq	xmm0,1
DB	102,15,58,68,231,0
	pxor	xmm0,xmm1

	sub	r9,0x20
	ja	NEAR $L$mod_loop

$L$even_tail:
	movdqa	xmm1,xmm0
	movdqa	xmm8,xmm4
	pshufd	xmm4,xmm0,78
	pxor	xmm4,xmm0

DB	102,15,58,68,198,0
DB	102,15,58,68,206,17
DB	102,15,58,68,231,16

	pxor	xmm0,xmm3
	pxor	xmm1,xmm5
	pxor	xmm8,xmm0
	pxor	xmm8,xmm1
	pxor	xmm4,xmm8
	movdqa	xmm8,xmm4
	psrldq	xmm8,8
	pslldq	xmm4,8
	pxor	xmm1,xmm8
	pxor	xmm0,xmm4

	movdqa	xmm4,xmm0
	movdqa	xmm3,xmm0
	psllq	xmm0,5
	pxor	xmm3,xmm0
	psllq	xmm0,1
	pxor	xmm0,xmm3
	psllq	xmm0,57
	movdqa	xmm3,xmm0
	pslldq	xmm0,8
	psrldq	xmm3,8
	pxor	xmm0,xmm4
	pxor	xmm1,xmm3


	movdqa	xmm4,xmm0
	psrlq	xmm0,1
	pxor	xmm1,xmm4
	pxor	xmm4,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm4
	psrlq	xmm0,1
	pxor	xmm0,xmm1
	test	r9,r9
	jnz	NEAR $L$done

$L$odd_tail:
	movdqu	xmm8,XMMWORD[r8]
DB	102,69,15,56,0,194
	pxor	xmm0,xmm8
	movdqa	xmm1,xmm0
	pshufd	xmm3,xmm0,78
	pxor	xmm3,xmm0
DB	102,15,58,68,194,0
DB	102,15,58,68,202,17
DB	102,15,58,68,223,0
	pxor	xmm3,xmm0
	pxor	xmm3,xmm1

	movdqa	xmm4,xmm3
	psrldq	xmm3,8
	pslldq	xmm4,8
	pxor	xmm1,xmm3
	pxor	xmm0,xmm4

	movdqa	xmm4,xmm0
	movdqa	xmm3,xmm0
	psllq	xmm0,5
	pxor	xmm3,xmm0
	psllq	xmm0,1
	pxor	xmm0,xmm3
	psllq	xmm0,57
	movdqa	xmm3,xmm0
	pslldq	xmm0,8
	psrldq	xmm3,8
	pxor	xmm0,xmm4
	pxor	xmm1,xmm3


	movdqa	xmm4,xmm0
	psrlq	xmm0,1
	pxor	xmm1,xmm4
	pxor	xmm4,xmm0
	psrlq	xmm0,5
	pxor	xmm0,xmm4
	psrlq	xmm0,1
	pxor	xmm0,xmm1
$L$done:
DB	102,65,15,56,0,194
	movdqu	XMMWORD[rcx],xmm0
	movaps	xmm6,XMMWORD[rsp]
	movaps	xmm7,XMMWORD[16+rsp]
	movaps	xmm8,XMMWORD[32+rsp]
	movaps	xmm9,XMMWORD[48+rsp]
	movaps	xmm10,XMMWORD[64+rsp]
	movaps	xmm11,XMMWORD[80+rsp]
	movaps	xmm12,XMMWORD[96+rsp]
	movaps	xmm13,XMMWORD[112+rsp]
	movaps	xmm14,XMMWORD[128+rsp]
	movaps	xmm15,XMMWORD[144+rsp]
	lea	rsp,[168+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	gcm_ghash_clmul

global	gcm_init_avx

ALIGN	32
SEH64_PROC_FRAME gcm_init_avx,PUBLIC

DB	243,15,30,250

DB	0x48,0x83,0xec,0x18
SEH64_ALLOCSTACK 24
DB	0x0f,0x29,0x34,0x24
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -32 cfa_reg_offset=32 cfa_rsp=-32
	; cfa_rsp=-32
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=32 cfa_rsp=-32
	vzeroupper

	vmovdqu	xmm2,XMMWORD[rdx]
	vpshufd	xmm2,xmm2,78


	vpshufd	xmm4,xmm2,255
	vpsrlq	xmm3,xmm2,63
	vpsllq	xmm2,xmm2,1
	vpxor	xmm5,xmm5,xmm5
	vpcmpgtd	xmm5,xmm5,xmm4
	vpslldq	xmm3,xmm3,8
	vpor	xmm2,xmm2,xmm3


	vpand	xmm5,xmm5,XMMWORD[$L$0x1c2_polynomial]
	vpxor	xmm2,xmm2,xmm5

	vpunpckhqdq	xmm6,xmm2,xmm2
	vmovdqa	xmm0,xmm2
	vpxor	xmm6,xmm6,xmm2
	mov	r10,4
	jmp	NEAR $L$init_start_avx
ALIGN	32
$L$init_loop_avx:
	vpalignr	xmm5,xmm4,xmm3,8
	vmovdqu	XMMWORD[(-16)+rcx],xmm5
	vpunpckhqdq	xmm3,xmm0,xmm0
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm1,xmm0,xmm2,0x11
	vpclmulqdq	xmm0,xmm0,xmm2,0x00
	vpclmulqdq	xmm3,xmm3,xmm6,0x00
	vpxor	xmm4,xmm1,xmm0
	vpxor	xmm3,xmm3,xmm4

	vpslldq	xmm4,xmm3,8
	vpsrldq	xmm3,xmm3,8
	vpxor	xmm0,xmm0,xmm4
	vpxor	xmm1,xmm1,xmm3
	vpsllq	xmm3,xmm0,57
	vpsllq	xmm4,xmm0,62
	vpxor	xmm4,xmm4,xmm3
	vpsllq	xmm3,xmm0,63
	vpxor	xmm4,xmm4,xmm3
	vpslldq	xmm3,xmm4,8
	vpsrldq	xmm4,xmm4,8
	vpxor	xmm0,xmm0,xmm3
	vpxor	xmm1,xmm1,xmm4

	vpsrlq	xmm4,xmm0,1
	vpxor	xmm1,xmm1,xmm0
	vpxor	xmm0,xmm0,xmm4
	vpsrlq	xmm4,xmm4,5
	vpxor	xmm0,xmm0,xmm4
	vpsrlq	xmm0,xmm0,1
	vpxor	xmm0,xmm0,xmm1
$L$init_start_avx:
	vmovdqa	xmm5,xmm0
	vpunpckhqdq	xmm3,xmm0,xmm0
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm1,xmm0,xmm2,0x11
	vpclmulqdq	xmm0,xmm0,xmm2,0x00
	vpclmulqdq	xmm3,xmm3,xmm6,0x00
	vpxor	xmm4,xmm1,xmm0
	vpxor	xmm3,xmm3,xmm4

	vpslldq	xmm4,xmm3,8
	vpsrldq	xmm3,xmm3,8
	vpxor	xmm0,xmm0,xmm4
	vpxor	xmm1,xmm1,xmm3
	vpsllq	xmm3,xmm0,57
	vpsllq	xmm4,xmm0,62
	vpxor	xmm4,xmm4,xmm3
	vpsllq	xmm3,xmm0,63
	vpxor	xmm4,xmm4,xmm3
	vpslldq	xmm3,xmm4,8
	vpsrldq	xmm4,xmm4,8
	vpxor	xmm0,xmm0,xmm3
	vpxor	xmm1,xmm1,xmm4

	vpsrlq	xmm4,xmm0,1
	vpxor	xmm1,xmm1,xmm0
	vpxor	xmm0,xmm0,xmm4
	vpsrlq	xmm4,xmm4,5
	vpxor	xmm0,xmm0,xmm4
	vpsrlq	xmm0,xmm0,1
	vpxor	xmm0,xmm0,xmm1
	vpshufd	xmm3,xmm5,78
	vpshufd	xmm4,xmm0,78
	vpxor	xmm3,xmm3,xmm5
	vmovdqu	XMMWORD[rcx],xmm5
	vpxor	xmm4,xmm4,xmm0
	vmovdqu	XMMWORD[16+rcx],xmm0
	lea	rcx,[48+rcx]
	sub	r10,1
	jnz	NEAR $L$init_loop_avx

	vpalignr	xmm5,xmm3,xmm4,8
	vmovdqu	XMMWORD[(-16)+rcx],xmm5

	vzeroupper
	movaps	xmm6,XMMWORD[rsp]
	lea	rsp,[24+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	gcm_init_avx

global	gcm_gmult_avx

ALIGN	32
SEH64_PROC_FRAME gcm_gmult_avx,PUBLIC

DB	243,15,30,250
	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	jmp	NEAR $L$_gmult_clmul
SEH64_ENDPROC_FRAME	gcm_gmult_avx

global	gcm_ghash_avx

ALIGN	32
SEH64_PROC_FRAME gcm_ghash_avx,PUBLIC

DB	243,15,30,250
	lea	rax,[((-136))+rsp]

DB	0x48,0x8d,0x60,0xe0
SEH64_ALLOCSTACK 168
DB	0x0f,0x29,0x70,0xe0
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -176 cfa_reg_offset=176 cfa_rsp=-176
DB	0x0f,0x29,0x78,0xf0
SEH64_SAVEXMM128 xmm7, 16	; .cfi_offset xmm7, -160 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x00
SEH64_SAVEXMM128 xmm8, 32	; .cfi_offset xmm8, -144 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x48,0x10
SEH64_SAVEXMM128 xmm9, 48	; .cfi_offset xmm9, -128 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x50,0x20
SEH64_SAVEXMM128 xmm10, 64	; .cfi_offset xmm10, -112 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x58,0x30
SEH64_SAVEXMM128 xmm11, 80	; .cfi_offset xmm11, -96 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x60,0x40
SEH64_SAVEXMM128 xmm12, 96	; .cfi_offset xmm12, -80 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x68,0x50
SEH64_SAVEXMM128 xmm13, 112	; .cfi_offset xmm13, -64 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x70,0x60
SEH64_SAVEXMM128 xmm14, 128	; .cfi_offset xmm14, -48 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x78,0x70
SEH64_SAVEXMM128 xmm15, 144	; .cfi_offset xmm15, -32 cfa_reg_offset=176 cfa_rsp=-176
	; cfa_rsp=-176
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=176 cfa_rsp=-176
	vzeroupper

	vmovdqu	xmm10,XMMWORD[rcx]
	lea	r10,[$L$0x1c2_polynomial]
	lea	rdx,[64+rdx]
	vmovdqu	xmm13,XMMWORD[$L$bswap_mask]
	vpshufb	xmm10,xmm10,xmm13
	cmp	r9,0x80
	jb	NEAR $L$short_avx
	sub	r9,0x80

	vmovdqu	xmm14,XMMWORD[112+r8]
	vmovdqu	xmm6,XMMWORD[((0-64))+rdx]
	vpshufb	xmm14,xmm14,xmm13
	vmovdqu	xmm7,XMMWORD[((32-64))+rdx]

	vpunpckhqdq	xmm9,xmm14,xmm14
	vmovdqu	xmm15,XMMWORD[96+r8]
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpxor	xmm9,xmm9,xmm14
	vpshufb	xmm15,xmm15,xmm13
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((16-64))+rdx]
	vpunpckhqdq	xmm8,xmm15,xmm15
	vmovdqu	xmm14,XMMWORD[80+r8]
	vpclmulqdq	xmm2,xmm9,xmm7,0x00
	vpxor	xmm8,xmm8,xmm15

	vpshufb	xmm14,xmm14,xmm13
	vpclmulqdq	xmm3,xmm15,xmm6,0x00
	vpunpckhqdq	xmm9,xmm14,xmm14
	vpclmulqdq	xmm4,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((48-64))+rdx]
	vpxor	xmm9,xmm9,xmm14
	vmovdqu	xmm15,XMMWORD[64+r8]
	vpclmulqdq	xmm5,xmm8,xmm7,0x10
	vmovdqu	xmm7,XMMWORD[((80-64))+rdx]

	vpshufb	xmm15,xmm15,xmm13
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpxor	xmm4,xmm4,xmm1
	vpunpckhqdq	xmm8,xmm15,xmm15
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((64-64))+rdx]
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm9,xmm7,0x00
	vpxor	xmm8,xmm8,xmm15

	vmovdqu	xmm14,XMMWORD[48+r8]
	vpxor	xmm0,xmm0,xmm3
	vpclmulqdq	xmm3,xmm15,xmm6,0x00
	vpxor	xmm1,xmm1,xmm4
	vpshufb	xmm14,xmm14,xmm13
	vpclmulqdq	xmm4,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((96-64))+rdx]
	vpxor	xmm2,xmm2,xmm5
	vpunpckhqdq	xmm9,xmm14,xmm14
	vpclmulqdq	xmm5,xmm8,xmm7,0x10
	vmovdqu	xmm7,XMMWORD[((128-64))+rdx]
	vpxor	xmm9,xmm9,xmm14

	vmovdqu	xmm15,XMMWORD[32+r8]
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpxor	xmm4,xmm4,xmm1
	vpshufb	xmm15,xmm15,xmm13
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((112-64))+rdx]
	vpxor	xmm5,xmm5,xmm2
	vpunpckhqdq	xmm8,xmm15,xmm15
	vpclmulqdq	xmm2,xmm9,xmm7,0x00
	vpxor	xmm8,xmm8,xmm15

	vmovdqu	xmm14,XMMWORD[16+r8]
	vpxor	xmm0,xmm0,xmm3
	vpclmulqdq	xmm3,xmm15,xmm6,0x00
	vpxor	xmm1,xmm1,xmm4
	vpshufb	xmm14,xmm14,xmm13
	vpclmulqdq	xmm4,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((144-64))+rdx]
	vpxor	xmm2,xmm2,xmm5
	vpunpckhqdq	xmm9,xmm14,xmm14
	vpclmulqdq	xmm5,xmm8,xmm7,0x10
	vmovdqu	xmm7,XMMWORD[((176-64))+rdx]
	vpxor	xmm9,xmm9,xmm14

	vmovdqu	xmm15,XMMWORD[r8]
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpxor	xmm4,xmm4,xmm1
	vpshufb	xmm15,xmm15,xmm13
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((160-64))+rdx]
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm9,xmm7,0x10

	lea	r8,[128+r8]
	cmp	r9,0x80
	jb	NEAR $L$tail_avx

	vpxor	xmm15,xmm15,xmm10
	sub	r9,0x80
	jmp	NEAR $L$oop8x_avx

ALIGN	32
$L$oop8x_avx:
	vpunpckhqdq	xmm8,xmm15,xmm15
	vmovdqu	xmm14,XMMWORD[112+r8]
	vpxor	xmm3,xmm3,xmm0
	vpxor	xmm8,xmm8,xmm15
	vpclmulqdq	xmm10,xmm15,xmm6,0x00
	vpshufb	xmm14,xmm14,xmm13
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm11,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((0-64))+rdx]
	vpunpckhqdq	xmm9,xmm14,xmm14
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm12,xmm8,xmm7,0x00
	vmovdqu	xmm7,XMMWORD[((32-64))+rdx]
	vpxor	xmm9,xmm9,xmm14

	vmovdqu	xmm15,XMMWORD[96+r8]
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpxor	xmm10,xmm10,xmm3
	vpshufb	xmm15,xmm15,xmm13
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vxorps	xmm11,xmm11,xmm4
	vmovdqu	xmm6,XMMWORD[((16-64))+rdx]
	vpunpckhqdq	xmm8,xmm15,xmm15
	vpclmulqdq	xmm2,xmm9,xmm7,0x00
	vpxor	xmm12,xmm12,xmm5
	vxorps	xmm8,xmm8,xmm15

	vmovdqu	xmm14,XMMWORD[80+r8]
	vpxor	xmm12,xmm12,xmm10
	vpclmulqdq	xmm3,xmm15,xmm6,0x00
	vpxor	xmm12,xmm12,xmm11
	vpslldq	xmm9,xmm12,8
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm4,xmm15,xmm6,0x11
	vpsrldq	xmm12,xmm12,8
	vpxor	xmm10,xmm10,xmm9
	vmovdqu	xmm6,XMMWORD[((48-64))+rdx]
	vpshufb	xmm14,xmm14,xmm13
	vxorps	xmm11,xmm11,xmm12
	vpxor	xmm4,xmm4,xmm1
	vpunpckhqdq	xmm9,xmm14,xmm14
	vpclmulqdq	xmm5,xmm8,xmm7,0x10
	vmovdqu	xmm7,XMMWORD[((80-64))+rdx]
	vpxor	xmm9,xmm9,xmm14
	vpxor	xmm5,xmm5,xmm2

	vmovdqu	xmm15,XMMWORD[64+r8]
	vpalignr	xmm12,xmm10,xmm10,8
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpshufb	xmm15,xmm15,xmm13
	vpxor	xmm0,xmm0,xmm3
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((64-64))+rdx]
	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm1,xmm1,xmm4
	vpclmulqdq	xmm2,xmm9,xmm7,0x00
	vxorps	xmm8,xmm8,xmm15
	vpxor	xmm2,xmm2,xmm5

	vmovdqu	xmm14,XMMWORD[48+r8]
	vpclmulqdq	xmm10,xmm10,XMMWORD[r10],0x10
	vpclmulqdq	xmm3,xmm15,xmm6,0x00
	vpshufb	xmm14,xmm14,xmm13
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm4,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((96-64))+rdx]
	vpunpckhqdq	xmm9,xmm14,xmm14
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm5,xmm8,xmm7,0x10
	vmovdqu	xmm7,XMMWORD[((128-64))+rdx]
	vpxor	xmm9,xmm9,xmm14
	vpxor	xmm5,xmm5,xmm2

	vmovdqu	xmm15,XMMWORD[32+r8]
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpshufb	xmm15,xmm15,xmm13
	vpxor	xmm0,xmm0,xmm3
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((112-64))+rdx]
	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm1,xmm1,xmm4
	vpclmulqdq	xmm2,xmm9,xmm7,0x00
	vpxor	xmm8,xmm8,xmm15
	vpxor	xmm2,xmm2,xmm5
	vxorps	xmm10,xmm10,xmm12

	vmovdqu	xmm14,XMMWORD[16+r8]
	vpalignr	xmm12,xmm10,xmm10,8
	vpclmulqdq	xmm3,xmm15,xmm6,0x00
	vpshufb	xmm14,xmm14,xmm13
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm4,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((144-64))+rdx]
	vpclmulqdq	xmm10,xmm10,XMMWORD[r10],0x10
	vxorps	xmm12,xmm12,xmm11
	vpunpckhqdq	xmm9,xmm14,xmm14
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm5,xmm8,xmm7,0x10
	vmovdqu	xmm7,XMMWORD[((176-64))+rdx]
	vpxor	xmm9,xmm9,xmm14
	vpxor	xmm5,xmm5,xmm2

	vmovdqu	xmm15,XMMWORD[r8]
	vpclmulqdq	xmm0,xmm14,xmm6,0x00
	vpshufb	xmm15,xmm15,xmm13
	vpclmulqdq	xmm1,xmm14,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((160-64))+rdx]
	vpxor	xmm15,xmm15,xmm12
	vpclmulqdq	xmm2,xmm9,xmm7,0x10
	vpxor	xmm15,xmm15,xmm10

	lea	r8,[128+r8]
	sub	r9,0x80
	jnc	NEAR $L$oop8x_avx

	add	r9,0x80
	jmp	NEAR $L$tail_no_xor_avx

ALIGN	32
$L$short_avx:
	vmovdqu	xmm14,XMMWORD[((-16))+r9*1+r8]
	lea	r8,[r9*1+r8]
	vmovdqu	xmm6,XMMWORD[((0-64))+rdx]
	vmovdqu	xmm7,XMMWORD[((32-64))+rdx]
	vpshufb	xmm15,xmm14,xmm13

	vmovdqa	xmm3,xmm0
	vmovdqa	xmm4,xmm1
	vmovdqa	xmm5,xmm2
	sub	r9,0x10
	jz	NEAR $L$tail_avx

	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm15,xmm6,0x00
	vpxor	xmm8,xmm8,xmm15
	vmovdqu	xmm14,XMMWORD[((-32))+r8]
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm1,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((16-64))+rdx]
	vpshufb	xmm15,xmm14,xmm13
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm8,xmm7,0x00
	vpsrldq	xmm7,xmm7,8
	sub	r9,0x10
	jz	NEAR $L$tail_avx

	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm15,xmm6,0x00
	vpxor	xmm8,xmm8,xmm15
	vmovdqu	xmm14,XMMWORD[((-48))+r8]
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm1,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((48-64))+rdx]
	vpshufb	xmm15,xmm14,xmm13
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm8,xmm7,0x00
	vmovdqu	xmm7,XMMWORD[((80-64))+rdx]
	sub	r9,0x10
	jz	NEAR $L$tail_avx

	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm15,xmm6,0x00
	vpxor	xmm8,xmm8,xmm15
	vmovdqu	xmm14,XMMWORD[((-64))+r8]
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm1,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((64-64))+rdx]
	vpshufb	xmm15,xmm14,xmm13
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm8,xmm7,0x00
	vpsrldq	xmm7,xmm7,8
	sub	r9,0x10
	jz	NEAR $L$tail_avx

	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm15,xmm6,0x00
	vpxor	xmm8,xmm8,xmm15
	vmovdqu	xmm14,XMMWORD[((-80))+r8]
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm1,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((96-64))+rdx]
	vpshufb	xmm15,xmm14,xmm13
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm8,xmm7,0x00
	vmovdqu	xmm7,XMMWORD[((128-64))+rdx]
	sub	r9,0x10
	jz	NEAR $L$tail_avx

	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm15,xmm6,0x00
	vpxor	xmm8,xmm8,xmm15
	vmovdqu	xmm14,XMMWORD[((-96))+r8]
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm1,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((112-64))+rdx]
	vpshufb	xmm15,xmm14,xmm13
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm8,xmm7,0x00
	vpsrldq	xmm7,xmm7,8
	sub	r9,0x10
	jz	NEAR $L$tail_avx

	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm15,xmm6,0x00
	vpxor	xmm8,xmm8,xmm15
	vmovdqu	xmm14,XMMWORD[((-112))+r8]
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm1,xmm15,xmm6,0x11
	vmovdqu	xmm6,XMMWORD[((144-64))+rdx]
	vpshufb	xmm15,xmm14,xmm13
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm8,xmm7,0x00
	vmovq	xmm7,QWORD[((184-64))+rdx]
	sub	r9,0x10
	jmp	NEAR $L$tail_avx

ALIGN	32
$L$tail_avx:
	vpxor	xmm15,xmm15,xmm10
$L$tail_no_xor_avx:
	vpunpckhqdq	xmm8,xmm15,xmm15
	vpxor	xmm3,xmm3,xmm0
	vpclmulqdq	xmm0,xmm15,xmm6,0x00
	vpxor	xmm8,xmm8,xmm15
	vpxor	xmm4,xmm4,xmm1
	vpclmulqdq	xmm1,xmm15,xmm6,0x11
	vpxor	xmm5,xmm5,xmm2
	vpclmulqdq	xmm2,xmm8,xmm7,0x00

	vmovdqu	xmm12,XMMWORD[r10]

	vpxor	xmm10,xmm3,xmm0
	vpxor	xmm11,xmm4,xmm1
	vpxor	xmm5,xmm5,xmm2

	vpxor	xmm5,xmm5,xmm10
	vpxor	xmm5,xmm5,xmm11
	vpslldq	xmm9,xmm5,8
	vpsrldq	xmm5,xmm5,8
	vpxor	xmm10,xmm10,xmm9
	vpxor	xmm11,xmm11,xmm5

	vpclmulqdq	xmm9,xmm10,xmm12,0x10
	vpalignr	xmm10,xmm10,xmm10,8
	vpxor	xmm10,xmm10,xmm9

	vpclmulqdq	xmm9,xmm10,xmm12,0x10
	vpalignr	xmm10,xmm10,xmm10,8
	vpxor	xmm10,xmm10,xmm11
	vpxor	xmm10,xmm10,xmm9

	cmp	r9,0
	jne	NEAR $L$short_avx

	vpshufb	xmm10,xmm10,xmm13
	vmovdqu	XMMWORD[rcx],xmm10
	vzeroupper
	movaps	xmm6,XMMWORD[rsp]
	movaps	xmm7,XMMWORD[16+rsp]
	movaps	xmm8,XMMWORD[32+rsp]
	movaps	xmm9,XMMWORD[48+rsp]
	movaps	xmm10,XMMWORD[64+rsp]
	movaps	xmm11,XMMWORD[80+rsp]
	movaps	xmm12,XMMWORD[96+rsp]
	movaps	xmm13,XMMWORD[112+rsp]
	movaps	xmm14,XMMWORD[128+rsp]
	movaps	xmm15,XMMWORD[144+rsp]
	lea	rsp,[168+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	gcm_ghash_avx

section	.rdata rdata align=64
ALIGN	64
$L$bswap_mask:
DB	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
$L$0x1c2_polynomial:
DB	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2
$L$7_mask:
	DD	7,0,7,0
$L$7_mask_poly:
	DD	7,0,450,0
ALIGN	64

$L$rem_4bit:
	DD	0,0,0,471859200,0,943718400,0,610271232
	DD	0,1887436800,0,1822425088,0,1220542464,0,1423966208
	DD	0,3774873600,0,4246732800,0,3644850176,0,3311403008
	DD	0,2441084928,0,2376073216,0,2847932416,0,3051356160

$L$rem_8bit:
	DW	0x0000,0x01C2,0x0384,0x0246,0x0708,0x06CA,0x048C,0x054E
	DW	0x0E10,0x0FD2,0x0D94,0x0C56,0x0918,0x08DA,0x0A9C,0x0B5E
	DW	0x1C20,0x1DE2,0x1FA4,0x1E66,0x1B28,0x1AEA,0x18AC,0x196E
	DW	0x1230,0x13F2,0x11B4,0x1076,0x1538,0x14FA,0x16BC,0x177E
	DW	0x3840,0x3982,0x3BC4,0x3A06,0x3F48,0x3E8A,0x3CCC,0x3D0E
	DW	0x3650,0x3792,0x35D4,0x3416,0x3158,0x309A,0x32DC,0x331E
	DW	0x2460,0x25A2,0x27E4,0x2626,0x2368,0x22AA,0x20EC,0x212E
	DW	0x2A70,0x2BB2,0x29F4,0x2836,0x2D78,0x2CBA,0x2EFC,0x2F3E
	DW	0x7080,0x7142,0x7304,0x72C6,0x7788,0x764A,0x740C,0x75CE
	DW	0x7E90,0x7F52,0x7D14,0x7CD6,0x7998,0x785A,0x7A1C,0x7BDE
	DW	0x6CA0,0x6D62,0x6F24,0x6EE6,0x6BA8,0x6A6A,0x682C,0x69EE
	DW	0x62B0,0x6372,0x6134,0x60F6,0x65B8,0x647A,0x663C,0x67FE
	DW	0x48C0,0x4902,0x4B44,0x4A86,0x4FC8,0x4E0A,0x4C4C,0x4D8E
	DW	0x46D0,0x4712,0x4554,0x4496,0x41D8,0x401A,0x425C,0x439E
	DW	0x54E0,0x5522,0x5764,0x56A6,0x53E8,0x522A,0x506C,0x51AE
	DW	0x5AF0,0x5B32,0x5974,0x58B6,0x5DF8,0x5C3A,0x5E7C,0x5FBE
	DW	0xE100,0xE0C2,0xE284,0xE346,0xE608,0xE7CA,0xE58C,0xE44E
	DW	0xEF10,0xEED2,0xEC94,0xED56,0xE818,0xE9DA,0xEB9C,0xEA5E
	DW	0xFD20,0xFCE2,0xFEA4,0xFF66,0xFA28,0xFBEA,0xF9AC,0xF86E
	DW	0xF330,0xF2F2,0xF0B4,0xF176,0xF438,0xF5FA,0xF7BC,0xF67E
	DW	0xD940,0xD882,0xDAC4,0xDB06,0xDE48,0xDF8A,0xDDCC,0xDC0E
	DW	0xD750,0xD692,0xD4D4,0xD516,0xD058,0xD19A,0xD3DC,0xD21E
	DW	0xC560,0xC4A2,0xC6E4,0xC726,0xC268,0xC3AA,0xC1EC,0xC02E
	DW	0xCB70,0xCAB2,0xC8F4,0xC936,0xCC78,0xCDBA,0xCFFC,0xCE3E
	DW	0x9180,0x9042,0x9204,0x93C6,0x9688,0x974A,0x950C,0x94CE
	DW	0x9F90,0x9E52,0x9C14,0x9DD6,0x9898,0x995A,0x9B1C,0x9ADE
	DW	0x8DA0,0x8C62,0x8E24,0x8FE6,0x8AA8,0x8B6A,0x892C,0x88EE
	DW	0x83B0,0x8272,0x8034,0x81F6,0x84B8,0x857A,0x873C,0x86FE
	DW	0xA9C0,0xA802,0xAA44,0xAB86,0xAEC8,0xAF0A,0xAD4C,0xAC8E
	DW	0xA7D0,0xA612,0xA454,0xA596,0xA0D8,0xA11A,0xA35C,0xA29E
	DW	0xB5E0,0xB422,0xB664,0xB7A6,0xB2E8,0xB32A,0xB16C,0xB0AE
	DW	0xBBF0,0xBA32,0xB874,0xB9B6,0xBCF8,0xBD3A,0xBF7C,0xBEBE

DB	71,72,65,83,72,32,102,111,114,32,120,56,54,95,54,52
DB	44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32
DB	60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111
DB	114,103,62,0
ALIGN	64
section .text
