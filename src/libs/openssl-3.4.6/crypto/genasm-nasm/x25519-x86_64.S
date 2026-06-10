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


global	x25519_fe51_mul

ALIGN	32
SEH64_PROC_FRAME x25519_fe51_mul,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



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
	lea	rsp,[((-40))+rsp]
SEH64_ALLOCSTACK 40
	; cfa_rsp=-96
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=96 cfa_rsp=-96

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
SEH64_ENDPROC_FRAME	x25519_fe51_mul
$L$SEH_end_x25519_fe51_mul:

global	x25519_fe51_sqr

ALIGN	32
SEH64_PROC_FRAME x25519_fe51_sqr,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx



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
	lea	rsp,[((-40))+rsp]
SEH64_ALLOCSTACK 40
	; cfa_rsp=-96
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=96 cfa_rsp=-96

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

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	x25519_fe51_sqr
$L$SEH_end_x25519_fe51_sqr:

global	x25519_fe51_mul121666

ALIGN	32
SEH64_PROC_FRAME x25519_fe51_mul121666,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx



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
	lea	rsp,[((-40))+rsp]
SEH64_ALLOCSTACK 40
	; cfa_rsp=-96
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=96 cfa_rsp=-96
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
SEH64_ENDPROC_FRAME	x25519_fe51_mul121666
$L$SEH_end_x25519_fe51_mul121666:
EXTERN	OPENSSL_ia32cap_P
global	x25519_fe64_eligible

ALIGN	32
SEH64_PROC_FRAME x25519_fe64_eligible,PUBLIC

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	ecx,DWORD[((OPENSSL_ia32cap_P+8))]
	xor	eax,eax
	and	ecx,0x80100
	cmp	ecx,0x80100
	cmove	eax,ecx
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	x25519_fe64_eligible


global	x25519_fe64_mul

ALIGN	32
SEH64_PROC_FRAME x25519_fe64_mul,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



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
	push	rdi
SEH64_PUSHREG	rdi	; cfa_rsp,cfa_reg_offset => -64,64 cfa_reg=%rsp
	lea	rsp,[((-16))+rsp]
SEH64_ALLOCSTACK 16
	; cfa_rsp=-80
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=80 cfa_rsp=-80

	mov	rax,rdx
	mov	rbp,QWORD[rdx]
	mov	rdx,QWORD[rsi]
	mov	rcx,QWORD[8+rax]
	mov	r14,QWORD[16+rax]
	mov	r15,QWORD[24+rax]

	mulx	rax,r8,rbp
	xor	edi,edi
	mulx	rbx,r9,rcx
	adcx	r9,rax
	mulx	rax,r10,r14
	adcx	r10,rbx
	mulx	r12,r11,r15
	mov	rdx,QWORD[8+rsi]
	adcx	r11,rax
	mov	QWORD[rsp],r14
	adcx	r12,rdi

	mulx	rbx,rax,rbp
	adox	r9,rax
	adcx	r10,rbx
	mulx	rbx,rax,rcx
	adox	r10,rax
	adcx	r11,rbx
	mulx	rbx,rax,r14
	adox	r11,rax
	adcx	r12,rbx
	mulx	r13,rax,r15
	mov	rdx,QWORD[16+rsi]
	adox	r12,rax
	adcx	r13,rdi
	adox	r13,rdi

	mulx	rbx,rax,rbp
	adcx	r10,rax
	adox	r11,rbx
	mulx	rbx,rax,rcx
	adcx	r11,rax
	adox	r12,rbx
	mulx	rbx,rax,r14
	adcx	r12,rax
	adox	r13,rbx
	mulx	r14,rax,r15
	mov	rdx,QWORD[24+rsi]
	adcx	r13,rax
	adox	r14,rdi
	adcx	r14,rdi

	mulx	rbx,rax,rbp
	adox	r11,rax
	adcx	r12,rbx
	mulx	rbx,rax,rcx
	adox	r12,rax
	adcx	r13,rbx
	mulx	rbx,rax,QWORD[rsp]
	adox	r13,rax
	adcx	r14,rbx
	mulx	r15,rax,r15
	mov	edx,38
	adox	r14,rax
	adcx	r15,rdi
	adox	r15,rdi

	jmp	NEAR $L$reduce64
SEH64_ENDPROC_FRAME	x25519_fe64_mul
$L$SEH_end_x25519_fe64_mul:

global	x25519_fe64_sqr

ALIGN	32
SEH64_PROC_FRAME x25519_fe64_sqr,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx



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
	push	rdi
SEH64_PUSHREG	rdi	; cfa_rsp,cfa_reg_offset => -64,64 cfa_reg=%rsp
	lea	rsp,[((-16))+rsp]
SEH64_ALLOCSTACK 16
	; cfa_rsp=-80
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=80 cfa_rsp=-80

	mov	rdx,QWORD[rsi]
	mov	rcx,QWORD[8+rsi]
	mov	rbp,QWORD[16+rsi]
	mov	rsi,QWORD[24+rsi]


	mulx	r15,r8,rdx
	mulx	rax,r9,rcx
	xor	edi,edi
	mulx	rbx,r10,rbp
	adcx	r10,rax
	mulx	r12,r11,rsi
	mov	rdx,rcx
	adcx	r11,rbx
	adcx	r12,rdi


	mulx	rbx,rax,rbp
	adox	r11,rax
	adcx	r12,rbx
	mulx	r13,rax,rsi
	mov	rdx,rbp
	adox	r12,rax
	adcx	r13,rdi


	mulx	r14,rax,rsi
	mov	rdx,rcx
	adox	r13,rax
	adcx	r14,rdi
	adox	r14,rdi

	adcx	r9,r9
	adox	r9,r15
	adcx	r10,r10
	mulx	rbx,rax,rdx
	mov	rdx,rbp
	adcx	r11,r11
	adox	r10,rax
	adcx	r12,r12
	adox	r11,rbx
	mulx	rbx,rax,rdx
	mov	rdx,rsi
	adcx	r13,r13
	adox	r12,rax
	adcx	r14,r14
	adox	r13,rbx
	mulx	r15,rax,rdx
	mov	edx,38
	adox	r14,rax
	adcx	r15,rdi
	adox	r15,rdi
	jmp	NEAR $L$reduce64

ALIGN	32
$L$reduce64:
	mulx	rbx,rax,r12
	adcx	r8,rax
	adox	r9,rbx
	mulx	rbx,rax,r13
	adcx	r9,rax
	adox	r10,rbx
	mulx	rbx,rax,r14
	adcx	r10,rax
	adox	r11,rbx
	mulx	r12,rax,r15
	adcx	r11,rax
	adox	r12,rdi
	adcx	r12,rdi

	mov	rdi,QWORD[16+rsp]
	imul	r12,rdx

	add	r8,r12
	adc	r9,0
	adc	r10,0
	adc	r11,0

	sbb	rax,rax
	and	rax,38

	add	r8,rax
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11
	mov	QWORD[rdi],r8

	mov	r15,QWORD[24+rsp]

	mov	r14,QWORD[32+rsp]

	mov	r13,QWORD[40+rsp]

	mov	r12,QWORD[48+rsp]

	mov	rbx,QWORD[56+rsp]

	mov	rbp,QWORD[64+rsp]

	lea	rsp,[72+rsp]

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	x25519_fe64_sqr
$L$SEH_end_x25519_fe64_sqr:

global	x25519_fe64_mul121666

ALIGN	32
SEH64_PROC_FRAME x25519_fe64_mul121666,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx



	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	edx,121666
	mulx	rcx,r8,QWORD[rsi]
	mulx	rax,r9,QWORD[8+rsi]
	add	r9,rcx
	mulx	rcx,r10,QWORD[16+rsi]
	adc	r10,rax
	mulx	rax,r11,QWORD[24+rsi]
	adc	r11,rcx
	adc	rax,0

	imul	rax,rax,38

	add	r8,rax
	adc	r9,0
	adc	r10,0
	adc	r11,0

	sbb	rax,rax
	and	rax,38

	add	r8,rax
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11
	mov	QWORD[rdi],r8

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	x25519_fe64_mul121666
$L$SEH_end_x25519_fe64_mul121666:

global	x25519_fe64_add

ALIGN	32
SEH64_PROC_FRAME x25519_fe64_add,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r8,QWORD[rsi]
	mov	r9,QWORD[8+rsi]
	mov	r10,QWORD[16+rsi]
	mov	r11,QWORD[24+rsi]

	add	r8,QWORD[rdx]
	adc	r9,QWORD[8+rdx]
	adc	r10,QWORD[16+rdx]
	adc	r11,QWORD[24+rdx]

	sbb	rax,rax
	and	rax,38

	add	r8,rax
	adc	r9,0
	adc	r10,0
	mov	QWORD[8+rdi],r9
	adc	r11,0
	mov	QWORD[16+rdi],r10
	sbb	rax,rax
	mov	QWORD[24+rdi],r11
	and	rax,38

	add	r8,rax
	mov	QWORD[rdi],r8

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	x25519_fe64_add
$L$SEH_end_x25519_fe64_add:

global	x25519_fe64_sub

ALIGN	32
SEH64_PROC_FRAME x25519_fe64_sub,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r8,QWORD[rsi]
	mov	r9,QWORD[8+rsi]
	mov	r10,QWORD[16+rsi]
	mov	r11,QWORD[24+rsi]

	sub	r8,QWORD[rdx]
	sbb	r9,QWORD[8+rdx]
	sbb	r10,QWORD[16+rdx]
	sbb	r11,QWORD[24+rdx]

	sbb	rax,rax
	and	rax,38

	sub	r8,rax
	sbb	r9,0
	sbb	r10,0
	mov	QWORD[8+rdi],r9
	sbb	r11,0
	mov	QWORD[16+rdi],r10
	sbb	rax,rax
	mov	QWORD[24+rdi],r11
	and	rax,38

	sub	r8,rax
	mov	QWORD[rdi],r8

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	x25519_fe64_sub
$L$SEH_end_x25519_fe64_sub:

global	x25519_fe64_tobytes

ALIGN	32
SEH64_PROC_FRAME x25519_fe64_tobytes,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx



	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r8,QWORD[rsi]
	mov	r9,QWORD[8+rsi]
	mov	r10,QWORD[16+rsi]
	mov	r11,QWORD[24+rsi]


	lea	rax,[r11*1+r11]
	sar	r11,63
	shr	rax,1
	and	r11,19
	add	r11,19

	add	r8,r11
	adc	r9,0
	adc	r10,0
	adc	rax,0

	lea	r11,[rax*1+rax]
	sar	rax,63
	shr	r11,1
	not	rax
	and	rax,19

	sub	r8,rax
	sbb	r9,0
	sbb	r10,0
	sbb	r11,0

	mov	QWORD[rdi],r8
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	x25519_fe64_tobytes
$L$SEH_end_x25519_fe64_tobytes:
DB	88,50,53,53,49,57,32,112,114,105,109,105,116,105,118,101
DB	115,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82
DB	89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112
DB	114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
