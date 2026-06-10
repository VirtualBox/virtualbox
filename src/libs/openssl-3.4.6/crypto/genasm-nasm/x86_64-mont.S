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

global	bn_mul_mont

ALIGN	16
SEH64_PROC_FRAME bn_mul_mont,PUBLIC
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



	mov	r9d,r9d
	mov	rax,rsp

	test	r9d,3
	jnz	NEAR $L$mul_enter
	cmp	r9d,8
	jb	NEAR $L$mul_enter
	mov	r11d,DWORD[((OPENSSL_ia32cap_P+8))]
	cmp	rdx,rsi
	jne	NEAR $L$mul4x_enter
	test	r9d,7
	jz	NEAR $L$sqr8x_enter
	jmp	NEAR $L$mul4x_enter

ALIGN	16
$L$mul_enter:
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




	neg	r9
	mov	r11,rsp
	lea	r10,[((-32))+r9*8+rsp]
	neg	r9
	and	r10,-1024









	sub	r11,r10
	and	r11,-4096
	lea	rsp,[r11*1+r10]
	mov	r11,QWORD[rsp]
	cmp	rsp,r10
	ja	NEAR $L$mul_page_walk
	jmp	NEAR $L$mul_page_walk_done

ALIGN	16
$L$mul_page_walk:
	lea	rsp,[((-4096))+rsp]
	mov	r11,QWORD[rsp]
	cmp	rsp,r10
	ja	NEAR $L$mul_page_walk
$L$mul_page_walk_done:

	mov	QWORD[rsp],rax

; Hack using rax to load saved CFA expression: %rsp+0,deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56
	mov	r12,rdx
	mov	r8,QWORD[r8]
	mov	rbx,QWORD[r12]
	mov	rax,QWORD[rsi]

	xor	r14,r14
	xor	r15,r15

	mov	rbp,r8
	mul	rbx
	mov	r10,rax
	mov	rax,QWORD[rcx]

	imul	rbp,r10
	mov	r11,rdx

	mul	rbp
	add	r10,rax
	mov	rax,QWORD[8+rsi]
	adc	rdx,0
	mov	r13,rdx

	lea	r15,[1+r15]
	jmp	NEAR $L$1st_enter

ALIGN	16
$L$1st:
	add	r13,rax
	mov	rax,QWORD[r15*8+rsi]
	adc	rdx,0
	add	r13,r11
	mov	r11,r10
	adc	rdx,0
	mov	QWORD[r15*8+rsp],r13
	mov	r13,rdx

$L$1st_enter:
	mul	rbx
	add	r11,rax
	mov	rax,QWORD[r15*8+rcx]
	adc	rdx,0
	lea	r15,[1+r15]
	mov	r10,rdx

	mul	rbp
	cmp	r15,r9
	jne	NEAR $L$1st

	add	r13,rax
	mov	rax,QWORD[rsi]
	adc	rdx,0
	add	r13,r11
	adc	rdx,0
	mov	QWORD[r15*8+rsp],r13
	mov	r13,rdx
	mov	r11,r10

	xor	rdx,rdx
	add	r13,r11
	adc	rdx,0
	mov	QWORD[8+r9*8+rsp],r13
	mov	QWORD[16+r9*8+rsp],rdx

	lea	r14,[1+r14]
	jmp	NEAR $L$outer
ALIGN	16
$L$outer:
	mov	rbx,QWORD[r14*8+r12]
	xor	r15,r15
	mov	rbp,r8
	mov	r10,QWORD[16+rsp]
	mul	rbx
	add	r10,rax
	mov	rax,QWORD[rcx]
	adc	rdx,0

	imul	rbp,r10
	mov	r11,rdx

	mul	rbp
	add	r10,rax
	mov	rax,QWORD[8+rsi]
	adc	rdx,0
	mov	r10,QWORD[24+rsp]
	mov	r13,rdx

	lea	r15,[1+r15]
	jmp	NEAR $L$inner_enter

ALIGN	16
$L$inner:
	add	r13,rax
	mov	rax,QWORD[r15*8+rsi]
	adc	rdx,0
	add	r13,r10
	mov	r10,QWORD[16+r15*8+rsp]
	adc	rdx,0
	mov	QWORD[r15*8+rsp],r13
	mov	r13,rdx

$L$inner_enter:
	mul	rbx
	add	r11,rax
	mov	rax,QWORD[r15*8+rcx]
	adc	rdx,0
	add	r10,r11
	mov	r11,rdx
	adc	r11,0
	lea	r15,[1+r15]

	mul	rbp
	cmp	r15,r9
	jne	NEAR $L$inner

	add	r13,rax
	mov	rax,QWORD[rsi]
	adc	rdx,0
	add	r13,r10
	mov	r10,QWORD[16+r15*8+rsp]
	adc	rdx,0
	mov	QWORD[r15*8+rsp],r13
	mov	r13,rdx

	xor	rdx,rdx
	add	r13,r11
	adc	rdx,0
	add	r13,r10
	adc	rdx,0
	mov	QWORD[8+r9*8+rsp],r13
	mov	QWORD[16+r9*8+rsp],rdx

	lea	r14,[1+r14]
	cmp	r14,r9
	jb	NEAR $L$outer

	xor	r14,r14
	mov	rax,QWORD[16+rsp]
	mov	r15,r9

ALIGN	16
$L$sub:	sbb	rax,QWORD[r14*8+rcx]
	mov	QWORD[r14*8+rdi],rax
	mov	rax,QWORD[24+r14*8+rsp]
	lea	r14,[1+r14]
	dec	r15
	jnz	NEAR $L$sub

	sbb	rax,0
	mov	rbx,-1
	xor	rbx,rax
	xor	r14,r14
	mov	r15,r9

$L$copy:
	mov	rcx,QWORD[r14*8+rdi]
	mov	rdx,QWORD[16+r14*8+rsp]
	and	rcx,rbx
	and	rdx,rax
	mov	QWORD[16+r14*8+rsp],r9
	or	rdx,rcx
	mov	QWORD[r14*8+rdi],rdx
	lea	r14,[1+r14]
	sub	r15,1
	jnz	NEAR $L$copy

	mov	rsi,QWORD[rsp]

	mov	rax,1
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
SEH64_ENDPROC_FRAME	bn_mul_mont
$L$SEH_end_bn_mul_mont:

ALIGN	16
SEH64_PROC_FRAME bn_mul4x_mont,PRIVATE
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



	mov	r9d,r9d
	mov	rax,rsp

$L$mul4x_enter:
	and	r11d,0x80100
	cmp	r11d,0x80100
	je	NEAR $L$mulx4x_enter
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




	neg	r9
	mov	r11,rsp
	lea	r10,[((-48))+r9*8+rsp]
	neg	r9
	and	r10,-1024

	sub	r11,r10
	and	r11,-4096
	lea	rsp,[r11*1+r10]
	mov	r11,QWORD[rsp]
	cmp	rsp,r10
	ja	NEAR $L$mul4x_page_walk
	jmp	NEAR $L$mul4x_page_walk_done

$L$mul4x_page_walk:
	lea	rsp,[((-4096))+rsp]
	mov	r11,QWORD[rsp]
	cmp	rsp,r10
	ja	NEAR $L$mul4x_page_walk
$L$mul4x_page_walk_done:

	mov	QWORD[rsp],rax

; Hack using rax to load saved CFA expression: %rsp+0,deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56
	mov	QWORD[32+r9*8+rsp],rdi
	mov	r12,rdx
	mov	r8,QWORD[r8]
	mov	rbx,QWORD[r12]
	mov	rax,QWORD[rsi]

	xor	r14,r14
	xor	r15,r15

	mov	rbp,r8
	mul	rbx
	mov	r10,rax
	mov	rax,QWORD[rcx]

	imul	rbp,r10
	mov	r11,rdx

	mul	rbp
	add	r10,rax
	mov	rax,QWORD[8+rsi]
	adc	rdx,0
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[8+rcx]
	adc	rdx,0
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[16+rsi]
	adc	rdx,0
	add	rdi,r11
	lea	r15,[4+r15]
	adc	rdx,0
	mov	QWORD[16+rsp],rdi
	mov	r13,rdx
	jmp	NEAR $L$1st4x
ALIGN	16
$L$1st4x:
	mul	rbx
	add	r10,rax
	mov	rax,QWORD[((-16))+r15*8+rcx]
	adc	rdx,0
	mov	r11,rdx

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[((-8))+r15*8+rsi]
	adc	rdx,0
	add	r13,r10
	adc	rdx,0
	mov	QWORD[((-8))+r15*8+rsp],r13
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[((-8))+r15*8+rcx]
	adc	rdx,0
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[r15*8+rsi]
	adc	rdx,0
	add	rdi,r11
	adc	rdx,0
	mov	QWORD[r15*8+rsp],rdi
	mov	r13,rdx

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[r15*8+rcx]
	adc	rdx,0
	mov	r11,rdx

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[8+r15*8+rsi]
	adc	rdx,0
	add	r13,r10
	adc	rdx,0
	mov	QWORD[8+r15*8+rsp],r13
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[8+r15*8+rcx]
	adc	rdx,0
	lea	r15,[4+r15]
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[((-16))+r15*8+rsi]
	adc	rdx,0
	add	rdi,r11
	adc	rdx,0
	mov	QWORD[((-16))+r15*8+rsp],rdi
	mov	r13,rdx
	cmp	r15,r9
	jb	NEAR $L$1st4x

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[((-16))+r15*8+rcx]
	adc	rdx,0
	mov	r11,rdx

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[((-8))+r15*8+rsi]
	adc	rdx,0
	add	r13,r10
	adc	rdx,0
	mov	QWORD[((-8))+r15*8+rsp],r13
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[((-8))+r15*8+rcx]
	adc	rdx,0
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[rsi]
	adc	rdx,0
	add	rdi,r11
	adc	rdx,0
	mov	QWORD[r15*8+rsp],rdi
	mov	r13,rdx

	xor	rdi,rdi
	add	r13,r10
	adc	rdi,0
	mov	QWORD[8+r15*8+rsp],r13
	mov	QWORD[16+r15*8+rsp],rdi

	lea	r14,[1+r14]
ALIGN	4
$L$outer4x:
	mov	rbx,QWORD[r14*8+r12]
	xor	r15,r15
	mov	r10,QWORD[16+rsp]
	mov	rbp,r8
	mul	rbx
	add	r10,rax
	mov	rax,QWORD[rcx]
	adc	rdx,0

	imul	rbp,r10
	mov	r11,rdx

	mul	rbp
	add	r10,rax
	mov	rax,QWORD[8+rsi]
	adc	rdx,0
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[8+rcx]
	adc	rdx,0
	add	r11,QWORD[24+rsp]
	adc	rdx,0
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[16+rsi]
	adc	rdx,0
	add	rdi,r11
	lea	r15,[4+r15]
	adc	rdx,0
	mov	QWORD[16+rsp],rdi
	mov	r13,rdx
	jmp	NEAR $L$inner4x
ALIGN	16
$L$inner4x:
	mul	rbx
	add	r10,rax
	mov	rax,QWORD[((-16))+r15*8+rcx]
	adc	rdx,0
	add	r10,QWORD[r15*8+rsp]
	adc	rdx,0
	mov	r11,rdx

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[((-8))+r15*8+rsi]
	adc	rdx,0
	add	r13,r10
	adc	rdx,0
	mov	QWORD[((-8))+r15*8+rsp],r13
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[((-8))+r15*8+rcx]
	adc	rdx,0
	add	r11,QWORD[8+r15*8+rsp]
	adc	rdx,0
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[r15*8+rsi]
	adc	rdx,0
	add	rdi,r11
	adc	rdx,0
	mov	QWORD[r15*8+rsp],rdi
	mov	r13,rdx

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[r15*8+rcx]
	adc	rdx,0
	add	r10,QWORD[16+r15*8+rsp]
	adc	rdx,0
	mov	r11,rdx

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[8+r15*8+rsi]
	adc	rdx,0
	add	r13,r10
	adc	rdx,0
	mov	QWORD[8+r15*8+rsp],r13
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[8+r15*8+rcx]
	adc	rdx,0
	add	r11,QWORD[24+r15*8+rsp]
	adc	rdx,0
	lea	r15,[4+r15]
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[((-16))+r15*8+rsi]
	adc	rdx,0
	add	rdi,r11
	adc	rdx,0
	mov	QWORD[((-16))+r15*8+rsp],rdi
	mov	r13,rdx
	cmp	r15,r9
	jb	NEAR $L$inner4x

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[((-16))+r15*8+rcx]
	adc	rdx,0
	add	r10,QWORD[r15*8+rsp]
	adc	rdx,0
	mov	r11,rdx

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[((-8))+r15*8+rsi]
	adc	rdx,0
	add	r13,r10
	adc	rdx,0
	mov	QWORD[((-8))+r15*8+rsp],r13
	mov	rdi,rdx

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[((-8))+r15*8+rcx]
	adc	rdx,0
	add	r11,QWORD[8+r15*8+rsp]
	adc	rdx,0
	lea	r14,[1+r14]
	mov	r10,rdx

	mul	rbp
	add	rdi,rax
	mov	rax,QWORD[rsi]
	adc	rdx,0
	add	rdi,r11
	adc	rdx,0
	mov	QWORD[r15*8+rsp],rdi
	mov	r13,rdx

	xor	rdi,rdi
	add	r13,r10
	adc	rdi,0
	add	r13,QWORD[16+r9*8+rsp]
	adc	rdi,0
	mov	QWORD[8+r15*8+rsp],r13
	mov	QWORD[16+r15*8+rsp],rdi

	cmp	r14,r9
	jb	NEAR $L$outer4x
	mov	rdi,QWORD[32+r9*8+rsp]
	lea	r15,[((-4))+r9]
	mov	rax,QWORD[16+rsp]
	mov	rdx,QWORD[24+rsp]
	shr	r15,2
	lea	rsi,[16+rsp]
	xor	r14,r14

	sub	rax,QWORD[rcx]
	mov	rbx,QWORD[16+rsi]
	mov	rbp,QWORD[24+rsi]
	sbb	rdx,QWORD[8+rcx]

$L$sub4x:
	mov	QWORD[r14*8+rdi],rax
	mov	QWORD[8+r14*8+rdi],rdx
	sbb	rbx,QWORD[16+r14*8+rcx]
	mov	rax,QWORD[32+r14*8+rsi]
	mov	rdx,QWORD[40+r14*8+rsi]
	sbb	rbp,QWORD[24+r14*8+rcx]
	mov	QWORD[16+r14*8+rdi],rbx
	mov	QWORD[24+r14*8+rdi],rbp
	sbb	rax,QWORD[32+r14*8+rcx]
	mov	rbx,QWORD[48+r14*8+rsi]
	mov	rbp,QWORD[56+r14*8+rsi]
	sbb	rdx,QWORD[40+r14*8+rcx]
	lea	r14,[4+r14]
	dec	r15
	jnz	NEAR $L$sub4x

	mov	QWORD[r14*8+rdi],rax
	mov	rax,QWORD[32+r14*8+rsi]
	sbb	rbx,QWORD[16+r14*8+rcx]
	mov	QWORD[8+r14*8+rdi],rdx
	sbb	rbp,QWORD[24+r14*8+rcx]
	mov	QWORD[16+r14*8+rdi],rbx

	sbb	rax,0
	mov	QWORD[24+r14*8+rdi],rbp
	pxor	xmm0,xmm0
DB	102,72,15,110,224
	pcmpeqd	xmm5,xmm5
	pshufd	xmm4,xmm4,0
	mov	r15,r9
	pxor	xmm5,xmm4
	shr	r15,2
	xor	eax,eax

	jmp	NEAR $L$copy4x
ALIGN	16
$L$copy4x:
	movdqa	xmm1,XMMWORD[16+rax*1+rsp]
	movdqu	xmm2,XMMWORD[rax*1+rdi]
	pand	xmm1,xmm4
	pand	xmm2,xmm5
	movdqa	xmm3,XMMWORD[32+rax*1+rsp]
	movdqa	XMMWORD[16+rax*1+rsp],xmm0
	por	xmm1,xmm2
	movdqu	xmm2,XMMWORD[16+rax*1+rdi]
	movdqu	XMMWORD[rax*1+rdi],xmm1
	pand	xmm3,xmm4
	pand	xmm2,xmm5
	movdqa	XMMWORD[32+rax*1+rsp],xmm0
	por	xmm3,xmm2
	movdqu	XMMWORD[16+rax*1+rdi],xmm3
	lea	rax,[32+rax]
	dec	r15
	jnz	NEAR $L$copy4x
	mov	rsi,QWORD[rsp]

	mov	rax,1
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
SEH64_ENDPROC_FRAME	bn_mul4x_mont
$L$SEH_end_bn_mul4x_mont:
EXTERN	bn_sqrx8x_internal
EXTERN	bn_sqr8x_internal


ALIGN	32
SEH64_PROC_FRAME bn_sqr8x_mont,PRIVATE
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



	mov	rax,rsp

$L$sqr8x_enter:
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

	mov	r10d,r9d
	shl	r9d,3
	shl	r10,3+2
	neg	r9






	lea	r11,[((-64))+r9*2+rsp]
	mov	rbp,rsp
	mov	r8,QWORD[r8]
	sub	r11,rsi
	and	r11,4095
	cmp	r10,r11
	jb	NEAR $L$sqr8x_sp_alt
	sub	rbp,r11
	lea	rbp,[((-64))+r9*2+rbp]
	jmp	NEAR $L$sqr8x_sp_done

ALIGN	32
$L$sqr8x_sp_alt:
	lea	r10,[((4096-64))+r9*2]
	lea	rbp,[((-64))+r9*2+rbp]
	sub	r11,r10
	mov	r10,0
	cmovc	r11,r10
	sub	rbp,r11
$L$sqr8x_sp_done:
	and	rbp,-64
	mov	r11,rsp
	sub	r11,rbp
	and	r11,-4096
	lea	rsp,[rbp*1+r11]
	mov	r10,QWORD[rsp]
	cmp	rsp,rbp
	ja	NEAR $L$sqr8x_page_walk
	jmp	NEAR $L$sqr8x_page_walk_done

ALIGN	16
$L$sqr8x_page_walk:
	lea	rsp,[((-4096))+rsp]
	mov	r10,QWORD[rsp]
	cmp	rsp,rbp
	ja	NEAR $L$sqr8x_page_walk
$L$sqr8x_page_walk_done:

	mov	r10,r9
	neg	r9

	mov	QWORD[32+rsp],r8
	mov	QWORD[40+rsp],rax

; Hack using rax to load saved CFA expression: %rsp+40,deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ALLOCSTACK 40	; Can only deref by .PUSHREG, so adjust RSP by feigning stack alloc.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56

DB	102,72,15,110,209
	pxor	xmm0,xmm0
DB	102,72,15,110,207
DB	102,73,15,110,218
	mov	eax,DWORD[((OPENSSL_ia32cap_P+8))]
	and	eax,0x80100
	cmp	eax,0x80100
	jne	NEAR $L$sqr8x_nox

	call	bn_sqrx8x_internal




	lea	rbx,[rcx*1+r8]
	mov	r9,rcx
	mov	rdx,rcx
DB	102,72,15,126,207
	sar	rcx,3+2
	jmp	NEAR $L$sqr8x_sub

ALIGN	32
$L$sqr8x_nox:
	call	bn_sqr8x_internal




	lea	rbx,[r9*1+rdi]
	mov	rcx,r9
	mov	rdx,r9
DB	102,72,15,126,207
	sar	rcx,3+2
	jmp	NEAR $L$sqr8x_sub

ALIGN	32
$L$sqr8x_sub:
	mov	r12,QWORD[rbx]
	mov	r13,QWORD[8+rbx]
	mov	r14,QWORD[16+rbx]
	mov	r15,QWORD[24+rbx]
	lea	rbx,[32+rbx]
	sbb	r12,QWORD[rbp]
	sbb	r13,QWORD[8+rbp]
	sbb	r14,QWORD[16+rbp]
	sbb	r15,QWORD[24+rbp]
	lea	rbp,[32+rbp]
	mov	QWORD[rdi],r12
	mov	QWORD[8+rdi],r13
	mov	QWORD[16+rdi],r14
	mov	QWORD[24+rdi],r15
	lea	rdi,[32+rdi]
	inc	rcx
	jnz	NEAR $L$sqr8x_sub

	sbb	rax,0
	lea	rbx,[r9*1+rbx]
	lea	rdi,[r9*1+rdi]

DB	102,72,15,110,200
	pxor	xmm0,xmm0
	pshufd	xmm1,xmm1,0
	mov	rsi,QWORD[40+rsp]

	jmp	NEAR $L$sqr8x_cond_copy

ALIGN	32
$L$sqr8x_cond_copy:
	movdqa	xmm2,XMMWORD[rbx]
	movdqa	xmm3,XMMWORD[16+rbx]
	lea	rbx,[32+rbx]
	movdqu	xmm4,XMMWORD[rdi]
	movdqu	xmm5,XMMWORD[16+rdi]
	lea	rdi,[32+rdi]
	movdqa	XMMWORD[(-32)+rbx],xmm0
	movdqa	XMMWORD[(-16)+rbx],xmm0
	movdqa	XMMWORD[(-32)+rdx*1+rbx],xmm0
	movdqa	XMMWORD[(-16)+rdx*1+rbx],xmm0
	pcmpeqd	xmm0,xmm1
	pand	xmm2,xmm1
	pand	xmm3,xmm1
	pand	xmm4,xmm0
	pand	xmm5,xmm0
	pxor	xmm0,xmm0
	por	xmm4,xmm2
	por	xmm5,xmm3
	movdqu	XMMWORD[(-32)+rdi],xmm4
	movdqu	XMMWORD[(-16)+rdi],xmm5
	add	r9,32
	jnz	NEAR $L$sqr8x_cond_copy

	mov	rax,1
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
SEH64_ENDPROC_FRAME	bn_sqr8x_mont
$L$SEH_end_bn_sqr8x_mont:

ALIGN	32
SEH64_PROC_FRAME bn_mulx4x_mont,PRIVATE
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



	mov	rax,rsp

$L$mulx4x_enter:
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

	shl	r9d,3
	xor	r10,r10
	sub	r10,r9
	mov	r8,QWORD[r8]
	lea	rbp,[((-72))+r10*1+rsp]
	and	rbp,-128
	mov	r11,rsp
	sub	r11,rbp
	and	r11,-4096
	lea	rsp,[rbp*1+r11]
	mov	r10,QWORD[rsp]
	cmp	rsp,rbp
	ja	NEAR $L$mulx4x_page_walk
	jmp	NEAR $L$mulx4x_page_walk_done

ALIGN	16
$L$mulx4x_page_walk:
	lea	rsp,[((-4096))+rsp]
	mov	r10,QWORD[rsp]
	cmp	rsp,rbp
	ja	NEAR $L$mulx4x_page_walk
$L$mulx4x_page_walk_done:

	lea	r10,[r9*1+rdx]












	mov	QWORD[rsp],r9
	shr	r9,5
	mov	QWORD[16+rsp],r10
	sub	r9,1
	mov	QWORD[24+rsp],r8
	mov	QWORD[32+rsp],rdi
	mov	QWORD[40+rsp],rax

; Hack using rax to load saved CFA expression: %rsp+40,deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ALLOCSTACK 40	; Can only deref by .PUSHREG, so adjust RSP by feigning stack alloc.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56
	mov	QWORD[48+rsp],r9
	jmp	NEAR $L$mulx4x_body

ALIGN	32
$L$mulx4x_body:
	lea	rdi,[8+rdx]
	mov	rdx,QWORD[rdx]
	lea	rbx,[((64+32))+rsp]
	mov	r9,rdx

	mulx	rax,r8,QWORD[rsi]
	mulx	r14,r11,QWORD[8+rsi]
	add	r11,rax
	mov	QWORD[8+rsp],rdi
	mulx	r13,r12,QWORD[16+rsi]
	adc	r12,r14
	adc	r13,0

	mov	rdi,r8
	imul	r8,QWORD[24+rsp]
	xor	rbp,rbp

	mulx	r14,rax,QWORD[24+rsi]
	mov	rdx,r8
	lea	rsi,[32+rsi]
	adcx	r13,rax
	adcx	r14,rbp

	mulx	r10,rax,QWORD[rcx]
	adcx	rdi,rax
	adox	r10,r11
	mulx	r11,rax,QWORD[8+rcx]
	adcx	r10,rax
	adox	r11,r12
DB	0xc4,0x62,0xfb,0xf6,0xa1,0x10,0x00,0x00,0x00
	mov	rdi,QWORD[48+rsp]
	mov	QWORD[((-32))+rbx],r10
	adcx	r11,rax
	adox	r12,r13
	mulx	r15,rax,QWORD[24+rcx]
	mov	rdx,r9
	mov	QWORD[((-24))+rbx],r11
	adcx	r12,rax
	adox	r15,rbp
	lea	rcx,[32+rcx]
	mov	QWORD[((-16))+rbx],r12

	jmp	NEAR $L$mulx4x_1st

ALIGN	32
$L$mulx4x_1st:
	adcx	r15,rbp
	mulx	rax,r10,QWORD[rsi]
	adcx	r10,r14
	mulx	r14,r11,QWORD[8+rsi]
	adcx	r11,rax
	mulx	rax,r12,QWORD[16+rsi]
	adcx	r12,r14
	mulx	r14,r13,QWORD[24+rsi]
DB	0x67,0x67
	mov	rdx,r8
	adcx	r13,rax
	adcx	r14,rbp
	lea	rsi,[32+rsi]
	lea	rbx,[32+rbx]

	adox	r10,r15
	mulx	r15,rax,QWORD[rcx]
	adcx	r10,rax
	adox	r11,r15
	mulx	r15,rax,QWORD[8+rcx]
	adcx	r11,rax
	adox	r12,r15
	mulx	r15,rax,QWORD[16+rcx]
	mov	QWORD[((-40))+rbx],r10
	adcx	r12,rax
	mov	QWORD[((-32))+rbx],r11
	adox	r13,r15
	mulx	r15,rax,QWORD[24+rcx]
	mov	rdx,r9
	mov	QWORD[((-24))+rbx],r12
	adcx	r13,rax
	adox	r15,rbp
	lea	rcx,[32+rcx]
	mov	QWORD[((-16))+rbx],r13

	dec	rdi
	jnz	NEAR $L$mulx4x_1st

	mov	rax,QWORD[rsp]
	mov	rdi,QWORD[8+rsp]
	adc	r15,rbp
	add	r14,r15
	sbb	r15,r15
	mov	QWORD[((-8))+rbx],r14
	jmp	NEAR $L$mulx4x_outer

ALIGN	32
$L$mulx4x_outer:
	mov	rdx,QWORD[rdi]
	lea	rdi,[8+rdi]
	sub	rsi,rax
	mov	QWORD[rbx],r15
	lea	rbx,[((64+32))+rsp]
	sub	rcx,rax

	mulx	r11,r8,QWORD[rsi]
	xor	ebp,ebp
	mov	r9,rdx
	mulx	r12,r14,QWORD[8+rsi]
	adox	r8,QWORD[((-32))+rbx]
	adcx	r11,r14
	mulx	r13,r15,QWORD[16+rsi]
	adox	r11,QWORD[((-24))+rbx]
	adcx	r12,r15
	adox	r12,QWORD[((-16))+rbx]
	adcx	r13,rbp
	adox	r13,rbp

	mov	QWORD[8+rsp],rdi
	mov	r15,r8
	imul	r8,QWORD[24+rsp]
	xor	ebp,ebp

	mulx	r14,rax,QWORD[24+rsi]
	mov	rdx,r8
	adcx	r13,rax
	adox	r13,QWORD[((-8))+rbx]
	adcx	r14,rbp
	lea	rsi,[32+rsi]
	adox	r14,rbp

	mulx	r10,rax,QWORD[rcx]
	adcx	r15,rax
	adox	r10,r11
	mulx	r11,rax,QWORD[8+rcx]
	adcx	r10,rax
	adox	r11,r12
	mulx	r12,rax,QWORD[16+rcx]
	mov	QWORD[((-32))+rbx],r10
	adcx	r11,rax
	adox	r12,r13
	mulx	r15,rax,QWORD[24+rcx]
	mov	rdx,r9
	mov	QWORD[((-24))+rbx],r11
	lea	rcx,[32+rcx]
	adcx	r12,rax
	adox	r15,rbp
	mov	rdi,QWORD[48+rsp]
	mov	QWORD[((-16))+rbx],r12

	jmp	NEAR $L$mulx4x_inner

ALIGN	32
$L$mulx4x_inner:
	mulx	rax,r10,QWORD[rsi]
	adcx	r15,rbp
	adox	r10,r14
	mulx	r14,r11,QWORD[8+rsi]
	adcx	r10,QWORD[rbx]
	adox	r11,rax
	mulx	rax,r12,QWORD[16+rsi]
	adcx	r11,QWORD[8+rbx]
	adox	r12,r14
	mulx	r14,r13,QWORD[24+rsi]
	mov	rdx,r8
	adcx	r12,QWORD[16+rbx]
	adox	r13,rax
	adcx	r13,QWORD[24+rbx]
	adox	r14,rbp
	lea	rsi,[32+rsi]
	lea	rbx,[32+rbx]
	adcx	r14,rbp

	adox	r10,r15
	mulx	r15,rax,QWORD[rcx]
	adcx	r10,rax
	adox	r11,r15
	mulx	r15,rax,QWORD[8+rcx]
	adcx	r11,rax
	adox	r12,r15
	mulx	r15,rax,QWORD[16+rcx]
	mov	QWORD[((-40))+rbx],r10
	adcx	r12,rax
	adox	r13,r15
	mulx	r15,rax,QWORD[24+rcx]
	mov	rdx,r9
	mov	QWORD[((-32))+rbx],r11
	mov	QWORD[((-24))+rbx],r12
	adcx	r13,rax
	adox	r15,rbp
	lea	rcx,[32+rcx]
	mov	QWORD[((-16))+rbx],r13

	dec	rdi
	jnz	NEAR $L$mulx4x_inner

	mov	rax,QWORD[rsp]
	mov	rdi,QWORD[8+rsp]
	adc	r15,rbp
	sub	rbp,QWORD[rbx]
	adc	r14,r15
	sbb	r15,r15
	mov	QWORD[((-8))+rbx],r14

	cmp	rdi,QWORD[16+rsp]
	jne	NEAR $L$mulx4x_outer

	lea	rbx,[64+rsp]
	sub	rcx,rax
	neg	r15
	mov	rdx,rax
	shr	rax,3+2
	mov	rdi,QWORD[32+rsp]
	jmp	NEAR $L$mulx4x_sub

ALIGN	32
$L$mulx4x_sub:
	mov	r11,QWORD[rbx]
	mov	r12,QWORD[8+rbx]
	mov	r13,QWORD[16+rbx]
	mov	r14,QWORD[24+rbx]
	lea	rbx,[32+rbx]
	sbb	r11,QWORD[rcx]
	sbb	r12,QWORD[8+rcx]
	sbb	r13,QWORD[16+rcx]
	sbb	r14,QWORD[24+rcx]
	lea	rcx,[32+rcx]
	mov	QWORD[rdi],r11
	mov	QWORD[8+rdi],r12
	mov	QWORD[16+rdi],r13
	mov	QWORD[24+rdi],r14
	lea	rdi,[32+rdi]
	dec	rax
	jnz	NEAR $L$mulx4x_sub

	sbb	r15,0
	lea	rbx,[64+rsp]
	sub	rdi,rdx

DB	102,73,15,110,207
	pxor	xmm0,xmm0
	pshufd	xmm1,xmm1,0
	mov	rsi,QWORD[40+rsp]

	jmp	NEAR $L$mulx4x_cond_copy

ALIGN	32
$L$mulx4x_cond_copy:
	movdqa	xmm2,XMMWORD[rbx]
	movdqa	xmm3,XMMWORD[16+rbx]
	lea	rbx,[32+rbx]
	movdqu	xmm4,XMMWORD[rdi]
	movdqu	xmm5,XMMWORD[16+rdi]
	lea	rdi,[32+rdi]
	movdqa	XMMWORD[(-32)+rbx],xmm0
	movdqa	XMMWORD[(-16)+rbx],xmm0
	pcmpeqd	xmm0,xmm1
	pand	xmm2,xmm1
	pand	xmm3,xmm1
	pand	xmm4,xmm0
	pand	xmm5,xmm0
	pxor	xmm0,xmm0
	por	xmm4,xmm2
	por	xmm5,xmm3
	movdqu	XMMWORD[(-32)+rdi],xmm4
	movdqu	XMMWORD[(-16)+rdi],xmm5
	sub	rdx,32
	jnz	NEAR $L$mulx4x_cond_copy

	mov	QWORD[rbx],rdx

	mov	rax,1
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
SEH64_ENDPROC_FRAME	bn_mulx4x_mont
$L$SEH_end_bn_mulx4x_mont:
DB	77,111,110,116,103,111,109,101,114,121,32,77,117,108,116,105
DB	112,108,105,99,97,116,105,111,110,32,102,111,114,32,120,56
DB	54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83
DB	32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115
DB	115,108,46,111,114,103,62,0
ALIGN	16
