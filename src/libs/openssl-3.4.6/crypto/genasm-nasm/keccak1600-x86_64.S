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



ALIGN	32
SEH64_PROC_FRAME __KeccakF1600,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	rax,QWORD[60+rdi]
	mov	rbx,QWORD[68+rdi]
	mov	rcx,QWORD[76+rdi]
	mov	rdx,QWORD[84+rdi]
	mov	rbp,QWORD[92+rdi]
	jmp	NEAR $L$oop

ALIGN	32
$L$oop:
	mov	r8,QWORD[((-100))+rdi]
	mov	r9,QWORD[((-52))+rdi]
	mov	r10,QWORD[((-4))+rdi]
	mov	r11,QWORD[44+rdi]

	xor	rcx,QWORD[((-84))+rdi]
	xor	rdx,QWORD[((-76))+rdi]
	xor	rax,r8
	xor	rbx,QWORD[((-92))+rdi]
	xor	rcx,QWORD[((-44))+rdi]
	xor	rax,QWORD[((-60))+rdi]
	mov	r12,rbp
	xor	rbp,QWORD[((-68))+rdi]

	xor	rcx,r10
	xor	rax,QWORD[((-20))+rdi]
	xor	rdx,QWORD[((-36))+rdi]
	xor	rbx,r9
	xor	rbp,QWORD[((-28))+rdi]

	xor	rcx,QWORD[36+rdi]
	xor	rax,QWORD[20+rdi]
	xor	rdx,QWORD[4+rdi]
	xor	rbx,QWORD[((-12))+rdi]
	xor	rbp,QWORD[12+rdi]

	mov	r13,rcx
	rol	rcx,1
	xor	rcx,rax
	xor	rdx,r11

	rol	rax,1
	xor	rax,rdx
	xor	rbx,QWORD[28+rdi]

	rol	rdx,1
	xor	rdx,rbx
	xor	rbp,QWORD[52+rdi]

	rol	rbx,1
	xor	rbx,rbp

	rol	rbp,1
	xor	rbp,r13
	xor	r9,rcx
	xor	r10,rdx
	rol	r9,44
	xor	r11,rbp
	xor	r12,rax
	rol	r10,43
	xor	r8,rbx
	mov	r13,r9
	rol	r11,21
	or	r9,r10
	xor	r9,r8
	rol	r12,14

	xor	r9,QWORD[r15]
	lea	r15,[8+r15]

	mov	r14,r12
	and	r12,r11
	mov	QWORD[((-100))+rsi],r9
	xor	r12,r10
	not	r10
	mov	QWORD[((-84))+rsi],r12

	or	r10,r11
	mov	r12,QWORD[76+rdi]
	xor	r10,r13
	mov	QWORD[((-92))+rsi],r10

	and	r13,r8
	mov	r9,QWORD[((-28))+rdi]
	xor	r13,r14
	mov	r10,QWORD[((-20))+rdi]
	mov	QWORD[((-68))+rsi],r13

	or	r14,r8
	mov	r8,QWORD[((-76))+rdi]
	xor	r14,r11
	mov	r11,QWORD[28+rdi]
	mov	QWORD[((-76))+rsi],r14


	xor	r8,rbp
	xor	r12,rdx
	rol	r8,28
	xor	r11,rcx
	xor	r9,rax
	rol	r12,61
	rol	r11,45
	xor	r10,rbx
	rol	r9,20
	mov	r13,r8
	or	r8,r12
	rol	r10,3

	xor	r8,r11
	mov	QWORD[((-36))+rsi],r8

	mov	r14,r9
	and	r9,r13
	mov	r8,QWORD[((-92))+rdi]
	xor	r9,r12
	not	r12
	mov	QWORD[((-28))+rsi],r9

	or	r12,r11
	mov	r9,QWORD[((-44))+rdi]
	xor	r12,r10
	mov	QWORD[((-44))+rsi],r12

	and	r11,r10
	mov	r12,QWORD[60+rdi]
	xor	r11,r14
	mov	QWORD[((-52))+rsi],r11

	or	r14,r10
	mov	r10,QWORD[4+rdi]
	xor	r14,r13
	mov	r11,QWORD[52+rdi]
	mov	QWORD[((-60))+rsi],r14


	xor	r10,rbp
	xor	r11,rax
	rol	r10,25
	xor	r9,rdx
	rol	r11,8
	xor	r12,rbx
	rol	r9,6
	xor	r8,rcx
	rol	r12,18
	mov	r13,r10
	and	r10,r11
	rol	r8,1

	not	r11
	xor	r10,r9
	mov	QWORD[((-12))+rsi],r10

	mov	r14,r12
	and	r12,r11
	mov	r10,QWORD[((-12))+rdi]
	xor	r12,r13
	mov	QWORD[((-4))+rsi],r12

	or	r13,r9
	mov	r12,QWORD[84+rdi]
	xor	r13,r8
	mov	QWORD[((-20))+rsi],r13

	and	r9,r8
	xor	r9,r14
	mov	QWORD[12+rsi],r9

	or	r14,r8
	mov	r9,QWORD[((-60))+rdi]
	xor	r14,r11
	mov	r11,QWORD[36+rdi]
	mov	QWORD[4+rsi],r14


	mov	r8,QWORD[((-68))+rdi]

	xor	r10,rcx
	xor	r11,rdx
	rol	r10,10
	xor	r9,rbx
	rol	r11,15
	xor	r12,rbp
	rol	r9,36
	xor	r8,rax
	rol	r12,56
	mov	r13,r10
	or	r10,r11
	rol	r8,27

	not	r11
	xor	r10,r9
	mov	QWORD[28+rsi],r10

	mov	r14,r12
	or	r12,r11
	xor	r12,r13
	mov	QWORD[36+rsi],r12

	and	r13,r9
	xor	r13,r8
	mov	QWORD[20+rsi],r13

	or	r9,r8
	xor	r9,r14
	mov	QWORD[52+rsi],r9

	and	r8,r14
	xor	r8,r11
	mov	QWORD[44+rsi],r8


	xor	rdx,QWORD[((-84))+rdi]
	xor	rbp,QWORD[((-36))+rdi]
	rol	rdx,62
	xor	rcx,QWORD[68+rdi]
	rol	rbp,55
	xor	rax,QWORD[12+rdi]
	rol	rcx,2
	xor	rbx,QWORD[20+rdi]
	xchg	rdi,rsi
	rol	rax,39
	rol	rbx,41
	mov	r13,rdx
	and	rdx,rbp
	not	rbp
	xor	rdx,rcx
	mov	QWORD[92+rdi],rdx

	mov	r14,rax
	and	rax,rbp
	xor	rax,r13
	mov	QWORD[60+rdi],rax

	or	r13,rcx
	xor	r13,rbx
	mov	QWORD[84+rdi],r13

	and	rcx,rbx
	xor	rcx,r14
	mov	QWORD[76+rdi],rcx

	or	rbx,r14
	xor	rbx,rbp
	mov	QWORD[68+rdi],rbx

	mov	rbp,rdx
	mov	rdx,r13

	test	r15,255
	jnz	NEAR $L$oop

	lea	r15,[((-192))+r15]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	__KeccakF1600



ALIGN	32
SEH64_PROC_FRAME KeccakF1600,PRIVATE

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

	lea	rdi,[100+rdi]
	sub	rsp,200
SEH64_ALLOCSTACK 200
	; cfa_rsp=-256
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=256 cfa_rsp=-256

	not	QWORD[((-92))+rdi]
	not	QWORD[((-84))+rdi]
	not	QWORD[((-36))+rdi]
	not	QWORD[((-4))+rdi]
	not	QWORD[36+rdi]
	not	QWORD[60+rdi]

	lea	r15,[iotas]
	lea	rsi,[100+rsp]

	call	__KeccakF1600

	not	QWORD[((-92))+rdi]
	not	QWORD[((-84))+rdi]
	not	QWORD[((-36))+rdi]
	not	QWORD[((-4))+rdi]
	not	QWORD[36+rdi]
	not	QWORD[60+rdi]
	lea	rdi,[((-100))+rdi]

	add	rsp,200


	pop	r15

	pop	r14

	pop	r13

	pop	r12

	pop	rbp

	pop	rbx

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	KeccakF1600

global	SHA3_absorb

ALIGN	32
SEH64_PROC_FRAME SHA3_absorb,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8
	mov	rcx,r9



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

	lea	rdi,[100+rdi]
	sub	rsp,232
SEH64_ALLOCSTACK 232
	; cfa_rsp=-288
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=288 cfa_rsp=-288

	mov	r9,rsi
	lea	rsi,[100+rsp]

	not	QWORD[((-92))+rdi]
	not	QWORD[((-84))+rdi]
	not	QWORD[((-36))+rdi]
	not	QWORD[((-4))+rdi]
	not	QWORD[36+rdi]
	not	QWORD[60+rdi]
	lea	r15,[iotas]

	mov	QWORD[((216-100))+rsi],rcx

$L$oop_absorb:
	cmp	rdx,rcx
	jc	NEAR $L$done_absorb

	shr	rcx,3
	lea	r8,[((-100))+rdi]

$L$block_absorb:
	mov	rax,QWORD[r9]
	lea	r9,[8+r9]
	xor	rax,QWORD[r8]
	lea	r8,[8+r8]
	sub	rdx,8
	mov	QWORD[((-8))+r8],rax
	sub	rcx,1
	jnz	NEAR $L$block_absorb

	mov	QWORD[((200-100))+rsi],r9
	mov	QWORD[((208-100))+rsi],rdx
	call	__KeccakF1600
	mov	r9,QWORD[((200-100))+rsi]
	mov	rdx,QWORD[((208-100))+rsi]
	mov	rcx,QWORD[((216-100))+rsi]
	jmp	NEAR $L$oop_absorb

ALIGN	32
$L$done_absorb:
	mov	rax,rdx

	not	QWORD[((-92))+rdi]
	not	QWORD[((-84))+rdi]
	not	QWORD[((-36))+rdi]
	not	QWORD[((-4))+rdi]
	not	QWORD[36+rdi]
	not	QWORD[60+rdi]

	add	rsp,232


	pop	r15

	pop	r14

	pop	r13

	pop	r12

	pop	rbp

	pop	rbx

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	SHA3_absorb
$L$SEH_end_SHA3_absorb:
global	SHA3_squeeze

ALIGN	32
SEH64_PROC_FRAME SHA3_squeeze,PUBLIC
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



	push	r12
SEH64_PUSHREG	r12	; cfa_rsp,cfa_reg_offset => -16,16 cfa_reg=%rsp
	push	r13
SEH64_PUSHREG	r13	; cfa_rsp,cfa_reg_offset => -24,24 cfa_reg=%rsp
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -32,32 cfa_reg=%rsp
	; cfa_rsp=-32
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=32 cfa_rsp=-32

	shr	rcx,3
	mov	r9,rdi
	mov	r12,rsi
	mov	r13,rdx
	mov	r14,rcx
	bt	r8d,0
	jc	NEAR $L$next_block
	jmp	NEAR $L$oop_squeeze

ALIGN	32
$L$oop_squeeze:
	cmp	r13,8
	jb	NEAR $L$tail_squeeze

	mov	rax,QWORD[r9]
	lea	r9,[8+r9]
	mov	QWORD[r12],rax
	lea	r12,[8+r12]
	sub	r13,8
	jz	NEAR $L$done_squeeze

	sub	rcx,1
	jnz	NEAR $L$oop_squeeze
$L$next_block:
	call	KeccakF1600
	mov	r9,rdi
	mov	rcx,r14
	jmp	NEAR $L$oop_squeeze

$L$tail_squeeze:
	mov	rsi,r9
	mov	rdi,r12
	mov	rcx,r13
DB	0xf3,0xa4

$L$done_squeeze:
	pop	r14

	pop	r13

	pop	r12

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	SHA3_squeeze
$L$SEH_end_SHA3_squeeze:
section	.rdata rdata align=256
ALIGN	256
	DQ	0,0,0,0,0,0,0,0

iotas:
	DQ	0x0000000000000001
	DQ	0x0000000000008082
	DQ	0x800000000000808a
	DQ	0x8000000080008000
	DQ	0x000000000000808b
	DQ	0x0000000080000001
	DQ	0x8000000080008081
	DQ	0x8000000000008009
	DQ	0x000000000000008a
	DQ	0x0000000000000088
	DQ	0x0000000080008009
	DQ	0x000000008000000a
	DQ	0x000000008000808b
	DQ	0x800000000000008b
	DQ	0x8000000000008089
	DQ	0x8000000000008003
	DQ	0x8000000000008002
	DQ	0x8000000000000080
	DQ	0x000000000000800a
	DQ	0x800000008000000a
	DQ	0x8000000080008081
	DQ	0x8000000000008080
	DQ	0x0000000080000001
	DQ	0x8000000080008008

DB	75,101,99,99,97,107,45,49,54,48,48,32,97,98,115,111
DB	114,98,32,97,110,100,32,115,113,117,101,101,122,101,32,102
DB	111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84
DB	79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64
DB	111,112,101,110,115,115,108,46,111,114,103,62,0
