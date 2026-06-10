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

global	rsaz_512_sqr

ALIGN	32
SEH64_PROC_FRAME rsaz_512_sqr,PUBLIC
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

	sub	rsp,128+24
SEH64_ALLOCSTACK 152
	; cfa_rsp=-208
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=208 cfa_rsp=-208
DB	102,72,15,110,202
	mov	rdx,QWORD[rsi]
	mov	rax,QWORD[8+rsi]
	mov	QWORD[128+rsp],rcx
	mov	r11d,0x80100
	and	r11d,DWORD[((OPENSSL_ia32cap_P+8))]
	cmp	r11d,0x80100
	je	NEAR $L$oop_sqrx
	jmp	NEAR $L$oop_sqr

ALIGN	32
$L$oop_sqr:
	mov	DWORD[((128+8))+rsp],r8d

	mov	rbx,rdx
	mov	rbp,rax
	mul	rdx
	mov	r8,rax
	mov	rax,QWORD[16+rsi]
	mov	r9,rdx

	mul	rbx
	add	r9,rax
	mov	rax,QWORD[24+rsi]
	mov	r10,rdx
	adc	r10,0

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[32+rsi]
	mov	r11,rdx
	adc	r11,0

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[40+rsi]
	mov	r12,rdx
	adc	r12,0

	mul	rbx
	add	r12,rax
	mov	rax,QWORD[48+rsi]
	mov	r13,rdx
	adc	r13,0

	mul	rbx
	add	r13,rax
	mov	rax,QWORD[56+rsi]
	mov	r14,rdx
	adc	r14,0

	mul	rbx
	add	r14,rax
	mov	rax,rbx
	adc	rdx,0

	xor	rcx,rcx
	add	r8,r8
	mov	r15,rdx
	adc	rcx,0

	mul	rax
	add	rdx,r8
	adc	rcx,0

	mov	QWORD[rsp],rax
	mov	QWORD[8+rsp],rdx


	mov	rax,QWORD[16+rsi]
	mul	rbp
	add	r10,rax
	mov	rax,QWORD[24+rsi]
	mov	rbx,rdx
	adc	rbx,0

	mul	rbp
	add	r11,rax
	mov	rax,QWORD[32+rsi]
	adc	rdx,0
	add	r11,rbx
	mov	rbx,rdx
	adc	rbx,0

	mul	rbp
	add	r12,rax
	mov	rax,QWORD[40+rsi]
	adc	rdx,0
	add	r12,rbx
	mov	rbx,rdx
	adc	rbx,0

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[48+rsi]
	adc	rdx,0
	add	r13,rbx
	mov	rbx,rdx
	adc	rbx,0

	mul	rbp
	add	r14,rax
	mov	rax,QWORD[56+rsi]
	adc	rdx,0
	add	r14,rbx
	mov	rbx,rdx
	adc	rbx,0

	mul	rbp
	add	r15,rax
	mov	rax,rbp
	adc	rdx,0
	add	r15,rbx
	adc	rdx,0

	xor	rbx,rbx
	add	r9,r9
	mov	r8,rdx
	adc	r10,r10
	adc	rbx,0

	mul	rax

	add	rax,rcx
	mov	rbp,QWORD[16+rsi]
	add	r9,rax
	mov	rax,QWORD[24+rsi]
	adc	r10,rdx
	adc	rbx,0

	mov	QWORD[16+rsp],r9
	mov	QWORD[24+rsp],r10


	mul	rbp
	add	r12,rax
	mov	rax,QWORD[32+rsi]
	mov	rcx,rdx
	adc	rcx,0

	mul	rbp
	add	r13,rax
	mov	rax,QWORD[40+rsi]
	adc	rdx,0
	add	r13,rcx
	mov	rcx,rdx
	adc	rcx,0

	mul	rbp
	add	r14,rax
	mov	rax,QWORD[48+rsi]
	adc	rdx,0
	add	r14,rcx
	mov	rcx,rdx
	adc	rcx,0

	mul	rbp
	add	r15,rax
	mov	rax,QWORD[56+rsi]
	adc	rdx,0
	add	r15,rcx
	mov	rcx,rdx
	adc	rcx,0

	mul	rbp
	add	r8,rax
	mov	rax,rbp
	adc	rdx,0
	add	r8,rcx
	adc	rdx,0

	xor	rcx,rcx
	add	r11,r11
	mov	r9,rdx
	adc	r12,r12
	adc	rcx,0

	mul	rax

	add	rax,rbx
	mov	r10,QWORD[24+rsi]
	add	r11,rax
	mov	rax,QWORD[32+rsi]
	adc	r12,rdx
	adc	rcx,0

	mov	QWORD[32+rsp],r11
	mov	QWORD[40+rsp],r12


	mov	r11,rax
	mul	r10
	add	r14,rax
	mov	rax,QWORD[40+rsi]
	mov	rbx,rdx
	adc	rbx,0

	mov	r12,rax
	mul	r10
	add	r15,rax
	mov	rax,QWORD[48+rsi]
	adc	rdx,0
	add	r15,rbx
	mov	rbx,rdx
	adc	rbx,0

	mov	rbp,rax
	mul	r10
	add	r8,rax
	mov	rax,QWORD[56+rsi]
	adc	rdx,0
	add	r8,rbx
	mov	rbx,rdx
	adc	rbx,0

	mul	r10
	add	r9,rax
	mov	rax,r10
	adc	rdx,0
	add	r9,rbx
	adc	rdx,0

	xor	rbx,rbx
	add	r13,r13
	mov	r10,rdx
	adc	r14,r14
	adc	rbx,0

	mul	rax

	add	rax,rcx
	add	r13,rax
	mov	rax,r12
	adc	r14,rdx
	adc	rbx,0

	mov	QWORD[48+rsp],r13
	mov	QWORD[56+rsp],r14


	mul	r11
	add	r8,rax
	mov	rax,rbp
	mov	rcx,rdx
	adc	rcx,0

	mul	r11
	add	r9,rax
	mov	rax,QWORD[56+rsi]
	adc	rdx,0
	add	r9,rcx
	mov	rcx,rdx
	adc	rcx,0

	mov	r14,rax
	mul	r11
	add	r10,rax
	mov	rax,r11
	adc	rdx,0
	add	r10,rcx
	adc	rdx,0

	xor	rcx,rcx
	add	r15,r15
	mov	r11,rdx
	adc	r8,r8
	adc	rcx,0

	mul	rax

	add	rax,rbx
	add	r15,rax
	mov	rax,rbp
	adc	r8,rdx
	adc	rcx,0

	mov	QWORD[64+rsp],r15
	mov	QWORD[72+rsp],r8


	mul	r12
	add	r10,rax
	mov	rax,r14
	mov	rbx,rdx
	adc	rbx,0

	mul	r12
	add	r11,rax
	mov	rax,r12
	adc	rdx,0
	add	r11,rbx
	adc	rdx,0

	xor	rbx,rbx
	add	r9,r9
	mov	r12,rdx
	adc	r10,r10
	adc	rbx,0

	mul	rax

	add	rax,rcx
	add	r9,rax
	mov	rax,r14
	adc	r10,rdx
	adc	rbx,0

	mov	QWORD[80+rsp],r9
	mov	QWORD[88+rsp],r10


	mul	rbp
	add	r12,rax
	mov	rax,rbp
	adc	rdx,0

	xor	rcx,rcx
	add	r11,r11
	mov	r13,rdx
	adc	r12,r12
	adc	rcx,0

	mul	rax

	add	rax,rbx
	add	r11,rax
	mov	rax,r14
	adc	r12,rdx
	adc	rcx,0

	mov	QWORD[96+rsp],r11
	mov	QWORD[104+rsp],r12


	xor	rbx,rbx
	add	r13,r13
	adc	rbx,0

	mul	rax

	add	rax,rcx
	add	rax,r13
	adc	rdx,rbx

	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]
DB	102,72,15,126,205

	mov	QWORD[112+rsp],rax
	mov	QWORD[120+rsp],rdx

	call	__rsaz_512_reduce

	add	r8,QWORD[64+rsp]
	adc	r9,QWORD[72+rsp]
	adc	r10,QWORD[80+rsp]
	adc	r11,QWORD[88+rsp]
	adc	r12,QWORD[96+rsp]
	adc	r13,QWORD[104+rsp]
	adc	r14,QWORD[112+rsp]
	adc	r15,QWORD[120+rsp]
	sbb	rcx,rcx

	call	__rsaz_512_subtract

	mov	rdx,r8
	mov	rax,r9
	mov	r8d,DWORD[((128+8))+rsp]
	mov	rsi,rdi

	dec	r8d
	jnz	NEAR $L$oop_sqr
	jmp	NEAR $L$sqr_tail

ALIGN	32
$L$oop_sqrx:
	mov	DWORD[((128+8))+rsp],r8d
DB	102,72,15,110,199

	mulx	r9,r8,rax
	mov	rbx,rax

	mulx	r10,rcx,QWORD[16+rsi]
	xor	rbp,rbp

	mulx	r11,rax,QWORD[24+rsi]
	adcx	r9,rcx

DB	0xc4,0x62,0xf3,0xf6,0xa6,0x20,0x00,0x00,0x00
	adcx	r10,rax

DB	0xc4,0x62,0xfb,0xf6,0xae,0x28,0x00,0x00,0x00
	adcx	r11,rcx

	mulx	r14,rcx,QWORD[48+rsi]
	adcx	r12,rax
	adcx	r13,rcx

	mulx	r15,rax,QWORD[56+rsi]
	adcx	r14,rax
	adcx	r15,rbp

	mulx	rdi,rax,rdx
	mov	rdx,rbx
	xor	rcx,rcx
	adox	r8,r8
	adcx	r8,rdi
	adox	rcx,rbp
	adcx	rcx,rbp

	mov	QWORD[rsp],rax
	mov	QWORD[8+rsp],r8


DB	0xc4,0xe2,0xfb,0xf6,0x9e,0x10,0x00,0x00,0x00
	adox	r10,rax
	adcx	r11,rbx

	mulx	r8,rdi,QWORD[24+rsi]
	adox	r11,rdi
DB	0x66
	adcx	r12,r8

	mulx	rbx,rax,QWORD[32+rsi]
	adox	r12,rax
	adcx	r13,rbx

	mulx	r8,rdi,QWORD[40+rsi]
	adox	r13,rdi
	adcx	r14,r8

DB	0xc4,0xe2,0xfb,0xf6,0x9e,0x30,0x00,0x00,0x00
	adox	r14,rax
	adcx	r15,rbx

DB	0xc4,0x62,0xc3,0xf6,0x86,0x38,0x00,0x00,0x00
	adox	r15,rdi
	adcx	r8,rbp
	mulx	rdi,rax,rdx
	adox	r8,rbp
DB	0x48,0x8b,0x96,0x10,0x00,0x00,0x00

	xor	rbx,rbx
	adox	r9,r9

	adcx	rax,rcx
	adox	r10,r10
	adcx	r9,rax
	adox	rbx,rbp
	adcx	r10,rdi
	adcx	rbx,rbp

	mov	QWORD[16+rsp],r9
DB	0x4c,0x89,0x94,0x24,0x18,0x00,0x00,0x00


	mulx	r9,rdi,QWORD[24+rsi]
	adox	r12,rdi
	adcx	r13,r9

	mulx	rcx,rax,QWORD[32+rsi]
	adox	r13,rax
	adcx	r14,rcx

DB	0xc4,0x62,0xc3,0xf6,0x8e,0x28,0x00,0x00,0x00
	adox	r14,rdi
	adcx	r15,r9

DB	0xc4,0xe2,0xfb,0xf6,0x8e,0x30,0x00,0x00,0x00
	adox	r15,rax
	adcx	r8,rcx

	mulx	r9,rdi,QWORD[56+rsi]
	adox	r8,rdi
	adcx	r9,rbp
	mulx	rdi,rax,rdx
	adox	r9,rbp
	mov	rdx,QWORD[24+rsi]

	xor	rcx,rcx
	adox	r11,r11

	adcx	rax,rbx
	adox	r12,r12
	adcx	r11,rax
	adox	rcx,rbp
	adcx	r12,rdi
	adcx	rcx,rbp

	mov	QWORD[32+rsp],r11
	mov	QWORD[40+rsp],r12


	mulx	rbx,rax,QWORD[32+rsi]
	adox	r14,rax
	adcx	r15,rbx

	mulx	r10,rdi,QWORD[40+rsi]
	adox	r15,rdi
	adcx	r8,r10

	mulx	rbx,rax,QWORD[48+rsi]
	adox	r8,rax
	adcx	r9,rbx

	mulx	r10,rdi,QWORD[56+rsi]
	adox	r9,rdi
	adcx	r10,rbp
	mulx	rdi,rax,rdx
	adox	r10,rbp
	mov	rdx,QWORD[32+rsi]

	xor	rbx,rbx
	adox	r13,r13

	adcx	rax,rcx
	adox	r14,r14
	adcx	r13,rax
	adox	rbx,rbp
	adcx	r14,rdi
	adcx	rbx,rbp

	mov	QWORD[48+rsp],r13
	mov	QWORD[56+rsp],r14


	mulx	r11,rdi,QWORD[40+rsi]
	adox	r8,rdi
	adcx	r9,r11

	mulx	rcx,rax,QWORD[48+rsi]
	adox	r9,rax
	adcx	r10,rcx

	mulx	r11,rdi,QWORD[56+rsi]
	adox	r10,rdi
	adcx	r11,rbp
	mulx	rdi,rax,rdx
	mov	rdx,QWORD[40+rsi]
	adox	r11,rbp

	xor	rcx,rcx
	adox	r15,r15

	adcx	rax,rbx
	adox	r8,r8
	adcx	r15,rax
	adox	rcx,rbp
	adcx	r8,rdi
	adcx	rcx,rbp

	mov	QWORD[64+rsp],r15
	mov	QWORD[72+rsp],r8


DB	0xc4,0xe2,0xfb,0xf6,0x9e,0x30,0x00,0x00,0x00
	adox	r10,rax
	adcx	r11,rbx

DB	0xc4,0x62,0xc3,0xf6,0xa6,0x38,0x00,0x00,0x00
	adox	r11,rdi
	adcx	r12,rbp
	mulx	rdi,rax,rdx
	adox	r12,rbp
	mov	rdx,QWORD[48+rsi]

	xor	rbx,rbx
	adox	r9,r9

	adcx	rax,rcx
	adox	r10,r10
	adcx	r9,rax
	adcx	r10,rdi
	adox	rbx,rbp
	adcx	rbx,rbp

	mov	QWORD[80+rsp],r9
	mov	QWORD[88+rsp],r10


DB	0xc4,0x62,0xfb,0xf6,0xae,0x38,0x00,0x00,0x00
	adox	r12,rax
	adox	r13,rbp

	mulx	rdi,rax,rdx
	xor	rcx,rcx
	mov	rdx,QWORD[56+rsi]
	adox	r11,r11

	adcx	rax,rbx
	adox	r12,r12
	adcx	r11,rax
	adox	rcx,rbp
	adcx	r12,rdi
	adcx	rcx,rbp

DB	0x4c,0x89,0x9c,0x24,0x60,0x00,0x00,0x00
DB	0x4c,0x89,0xa4,0x24,0x68,0x00,0x00,0x00


	mulx	rdx,rax,rdx
	xor	rbx,rbx
	adox	r13,r13

	adcx	rax,rcx
	adox	rbx,rbp
	adcx	rax,r13
	adcx	rbx,rdx

DB	102,72,15,126,199
DB	102,72,15,126,205

	mov	rdx,QWORD[128+rsp]
	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]

	mov	QWORD[112+rsp],rax
	mov	QWORD[120+rsp],rbx

	call	__rsaz_512_reducex

	add	r8,QWORD[64+rsp]
	adc	r9,QWORD[72+rsp]
	adc	r10,QWORD[80+rsp]
	adc	r11,QWORD[88+rsp]
	adc	r12,QWORD[96+rsp]
	adc	r13,QWORD[104+rsp]
	adc	r14,QWORD[112+rsp]
	adc	r15,QWORD[120+rsp]
	sbb	rcx,rcx

	call	__rsaz_512_subtract

	mov	rdx,r8
	mov	rax,r9
	mov	r8d,DWORD[((128+8))+rsp]
	mov	rsi,rdi

	dec	r8d
	jnz	NEAR $L$oop_sqrx

$L$sqr_tail:

	lea	rax,[((128+24+48))+rsp]

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
SEH64_ENDPROC_FRAME	rsaz_512_sqr
$L$SEH_end_rsaz_512_sqr:
global	rsaz_512_mul

ALIGN	32
SEH64_PROC_FRAME rsaz_512_mul,PUBLIC
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

	sub	rsp,128+24
SEH64_ALLOCSTACK 152
	; cfa_rsp=-208
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=208 cfa_rsp=-208
DB	102,72,15,110,199
DB	102,72,15,110,201
	mov	QWORD[128+rsp],r8
	mov	r11d,0x80100
	and	r11d,DWORD[((OPENSSL_ia32cap_P+8))]
	cmp	r11d,0x80100
	je	NEAR $L$mulx
	mov	rbx,QWORD[rdx]
	mov	rbp,rdx
	call	__rsaz_512_mul

DB	102,72,15,126,199
DB	102,72,15,126,205

	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]

	call	__rsaz_512_reduce
	jmp	NEAR $L$mul_tail

ALIGN	32
$L$mulx:
	mov	rbp,rdx
	mov	rdx,QWORD[rdx]
	call	__rsaz_512_mulx

DB	102,72,15,126,199
DB	102,72,15,126,205

	mov	rdx,QWORD[128+rsp]
	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]

	call	__rsaz_512_reducex
$L$mul_tail:
	add	r8,QWORD[64+rsp]
	adc	r9,QWORD[72+rsp]
	adc	r10,QWORD[80+rsp]
	adc	r11,QWORD[88+rsp]
	adc	r12,QWORD[96+rsp]
	adc	r13,QWORD[104+rsp]
	adc	r14,QWORD[112+rsp]
	adc	r15,QWORD[120+rsp]
	sbb	rcx,rcx

	call	__rsaz_512_subtract

	lea	rax,[((128+24+48))+rsp]

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
SEH64_ENDPROC_FRAME	rsaz_512_mul
$L$SEH_end_rsaz_512_mul:
global	rsaz_512_mul_gather4

ALIGN	32
SEH64_PROC_FRAME rsaz_512_mul_gather4,PUBLIC
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

	sub	rsp,328
SEH64_ALLOCSTACK 328
	movaps	XMMWORD[160+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 160	; .cfi_offset xmm6, -224 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[176+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 176	; .cfi_offset xmm7, -208 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[192+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 192	; .cfi_offset xmm8, -192 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[208+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 208	; .cfi_offset xmm9, -176 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[224+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 224	; .cfi_offset xmm10, -160 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[240+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 240	; .cfi_offset xmm11, -144 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[256+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 256	; .cfi_offset xmm12, -128 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[272+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 272	; .cfi_offset xmm13, -112 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[288+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 288	; .cfi_offset xmm14, -96 cfa_reg_offset=384 cfa_rsp=-384
	movaps	XMMWORD[304+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 320	; .cfi_offset xmm15, -64 cfa_reg_offset=384 cfa_rsp=-384
	; cfa_rsp=-384
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=384 cfa_rsp=-384
	movd	xmm8,r9d
	movdqa	xmm1,XMMWORD[(($L$inc+16))]
	movdqa	xmm0,XMMWORD[$L$inc]

	pshufd	xmm8,xmm8,0
	movdqa	xmm7,xmm1
	movdqa	xmm2,xmm1
	paddd	xmm1,xmm0
	pcmpeqd	xmm0,xmm8
	movdqa	xmm3,xmm7
	paddd	xmm2,xmm1
	pcmpeqd	xmm1,xmm8
	movdqa	xmm4,xmm7
	paddd	xmm3,xmm2
	pcmpeqd	xmm2,xmm8
	movdqa	xmm5,xmm7
	paddd	xmm4,xmm3
	pcmpeqd	xmm3,xmm8
	movdqa	xmm6,xmm7
	paddd	xmm5,xmm4
	pcmpeqd	xmm4,xmm8
	paddd	xmm6,xmm5
	pcmpeqd	xmm5,xmm8
	paddd	xmm7,xmm6
	pcmpeqd	xmm6,xmm8
	pcmpeqd	xmm7,xmm8

	movdqa	xmm8,XMMWORD[rdx]
	movdqa	xmm9,XMMWORD[16+rdx]
	movdqa	xmm10,XMMWORD[32+rdx]
	movdqa	xmm11,XMMWORD[48+rdx]
	pand	xmm8,xmm0
	movdqa	xmm12,XMMWORD[64+rdx]
	pand	xmm9,xmm1
	movdqa	xmm13,XMMWORD[80+rdx]
	pand	xmm10,xmm2
	movdqa	xmm14,XMMWORD[96+rdx]
	pand	xmm11,xmm3
	movdqa	xmm15,XMMWORD[112+rdx]
	lea	rbp,[128+rdx]
	pand	xmm12,xmm4
	pand	xmm13,xmm5
	pand	xmm14,xmm6
	pand	xmm15,xmm7
	por	xmm8,xmm10
	por	xmm9,xmm11
	por	xmm8,xmm12
	por	xmm9,xmm13
	por	xmm8,xmm14
	por	xmm9,xmm15

	por	xmm8,xmm9
	pshufd	xmm9,xmm8,0x4e
	por	xmm8,xmm9
	mov	r11d,0x80100
	and	r11d,DWORD[((OPENSSL_ia32cap_P+8))]
	cmp	r11d,0x80100
	je	NEAR $L$mulx_gather
DB	102,76,15,126,195

	mov	QWORD[128+rsp],r8
	mov	QWORD[((128+8))+rsp],rdi
	mov	QWORD[((128+16))+rsp],rcx

	mov	rax,QWORD[rsi]
	mov	rcx,QWORD[8+rsi]
	mul	rbx
	mov	QWORD[rsp],rax
	mov	rax,rcx
	mov	r8,rdx

	mul	rbx
	add	r8,rax
	mov	rax,QWORD[16+rsi]
	mov	r9,rdx
	adc	r9,0

	mul	rbx
	add	r9,rax
	mov	rax,QWORD[24+rsi]
	mov	r10,rdx
	adc	r10,0

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[32+rsi]
	mov	r11,rdx
	adc	r11,0

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[40+rsi]
	mov	r12,rdx
	adc	r12,0

	mul	rbx
	add	r12,rax
	mov	rax,QWORD[48+rsi]
	mov	r13,rdx
	adc	r13,0

	mul	rbx
	add	r13,rax
	mov	rax,QWORD[56+rsi]
	mov	r14,rdx
	adc	r14,0

	mul	rbx
	add	r14,rax
	mov	rax,QWORD[rsi]
	mov	r15,rdx
	adc	r15,0

	lea	rdi,[8+rsp]
	mov	ecx,7
	jmp	NEAR $L$oop_mul_gather

ALIGN	32
$L$oop_mul_gather:
	movdqa	xmm8,XMMWORD[rbp]
	movdqa	xmm9,XMMWORD[16+rbp]
	movdqa	xmm10,XMMWORD[32+rbp]
	movdqa	xmm11,XMMWORD[48+rbp]
	pand	xmm8,xmm0
	movdqa	xmm12,XMMWORD[64+rbp]
	pand	xmm9,xmm1
	movdqa	xmm13,XMMWORD[80+rbp]
	pand	xmm10,xmm2
	movdqa	xmm14,XMMWORD[96+rbp]
	pand	xmm11,xmm3
	movdqa	xmm15,XMMWORD[112+rbp]
	lea	rbp,[128+rbp]
	pand	xmm12,xmm4
	pand	xmm13,xmm5
	pand	xmm14,xmm6
	pand	xmm15,xmm7
	por	xmm8,xmm10
	por	xmm9,xmm11
	por	xmm8,xmm12
	por	xmm9,xmm13
	por	xmm8,xmm14
	por	xmm9,xmm15

	por	xmm8,xmm9
	pshufd	xmm9,xmm8,0x4e
	por	xmm8,xmm9
DB	102,76,15,126,195

	mul	rbx
	add	r8,rax
	mov	rax,QWORD[8+rsi]
	mov	QWORD[rdi],r8
	mov	r8,rdx
	adc	r8,0

	mul	rbx
	add	r9,rax
	mov	rax,QWORD[16+rsi]
	adc	rdx,0
	add	r8,r9
	mov	r9,rdx
	adc	r9,0

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[24+rsi]
	adc	rdx,0
	add	r9,r10
	mov	r10,rdx
	adc	r10,0

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[32+rsi]
	adc	rdx,0
	add	r10,r11
	mov	r11,rdx
	adc	r11,0

	mul	rbx
	add	r12,rax
	mov	rax,QWORD[40+rsi]
	adc	rdx,0
	add	r11,r12
	mov	r12,rdx
	adc	r12,0

	mul	rbx
	add	r13,rax
	mov	rax,QWORD[48+rsi]
	adc	rdx,0
	add	r12,r13
	mov	r13,rdx
	adc	r13,0

	mul	rbx
	add	r14,rax
	mov	rax,QWORD[56+rsi]
	adc	rdx,0
	add	r13,r14
	mov	r14,rdx
	adc	r14,0

	mul	rbx
	add	r15,rax
	mov	rax,QWORD[rsi]
	adc	rdx,0
	add	r14,r15
	mov	r15,rdx
	adc	r15,0

	lea	rdi,[8+rdi]

	dec	ecx
	jnz	NEAR $L$oop_mul_gather

	mov	QWORD[rdi],r8
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11
	mov	QWORD[32+rdi],r12
	mov	QWORD[40+rdi],r13
	mov	QWORD[48+rdi],r14
	mov	QWORD[56+rdi],r15

	mov	rdi,QWORD[((128+8))+rsp]
	mov	rbp,QWORD[((128+16))+rsp]

	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]

	call	__rsaz_512_reduce
	jmp	NEAR $L$mul_gather_tail

ALIGN	32
$L$mulx_gather:
DB	102,76,15,126,194

	mov	QWORD[128+rsp],r8
	mov	QWORD[((128+8))+rsp],rdi
	mov	QWORD[((128+16))+rsp],rcx

	mulx	r8,rbx,QWORD[rsi]
	mov	QWORD[rsp],rbx
	xor	edi,edi

	mulx	r9,rax,QWORD[8+rsi]

	mulx	r10,rbx,QWORD[16+rsi]
	adcx	r8,rax

	mulx	r11,rax,QWORD[24+rsi]
	adcx	r9,rbx

	mulx	r12,rbx,QWORD[32+rsi]
	adcx	r10,rax

	mulx	r13,rax,QWORD[40+rsi]
	adcx	r11,rbx

	mulx	r14,rbx,QWORD[48+rsi]
	adcx	r12,rax

	mulx	r15,rax,QWORD[56+rsi]
	adcx	r13,rbx
	adcx	r14,rax
DB	0x67
	mov	rbx,r8
	adcx	r15,rdi

	mov	rcx,-7
	jmp	NEAR $L$oop_mulx_gather

ALIGN	32
$L$oop_mulx_gather:
	movdqa	xmm8,XMMWORD[rbp]
	movdqa	xmm9,XMMWORD[16+rbp]
	movdqa	xmm10,XMMWORD[32+rbp]
	movdqa	xmm11,XMMWORD[48+rbp]
	pand	xmm8,xmm0
	movdqa	xmm12,XMMWORD[64+rbp]
	pand	xmm9,xmm1
	movdqa	xmm13,XMMWORD[80+rbp]
	pand	xmm10,xmm2
	movdqa	xmm14,XMMWORD[96+rbp]
	pand	xmm11,xmm3
	movdqa	xmm15,XMMWORD[112+rbp]
	lea	rbp,[128+rbp]
	pand	xmm12,xmm4
	pand	xmm13,xmm5
	pand	xmm14,xmm6
	pand	xmm15,xmm7
	por	xmm8,xmm10
	por	xmm9,xmm11
	por	xmm8,xmm12
	por	xmm9,xmm13
	por	xmm8,xmm14
	por	xmm9,xmm15

	por	xmm8,xmm9
	pshufd	xmm9,xmm8,0x4e
	por	xmm8,xmm9
DB	102,76,15,126,194

DB	0xc4,0x62,0xfb,0xf6,0x86,0x00,0x00,0x00,0x00
	adcx	rbx,rax
	adox	r8,r9

	mulx	r9,rax,QWORD[8+rsi]
	adcx	r8,rax
	adox	r9,r10

	mulx	r10,rax,QWORD[16+rsi]
	adcx	r9,rax
	adox	r10,r11

DB	0xc4,0x62,0xfb,0xf6,0x9e,0x18,0x00,0x00,0x00
	adcx	r10,rax
	adox	r11,r12

	mulx	r12,rax,QWORD[32+rsi]
	adcx	r11,rax
	adox	r12,r13

	mulx	r13,rax,QWORD[40+rsi]
	adcx	r12,rax
	adox	r13,r14

DB	0xc4,0x62,0xfb,0xf6,0xb6,0x30,0x00,0x00,0x00
	adcx	r13,rax
DB	0x67
	adox	r14,r15

	mulx	r15,rax,QWORD[56+rsi]
	mov	QWORD[64+rcx*8+rsp],rbx
	adcx	r14,rax
	adox	r15,rdi
	mov	rbx,r8
	adcx	r15,rdi

	inc	rcx
	jnz	NEAR $L$oop_mulx_gather

	mov	QWORD[64+rsp],r8
	mov	QWORD[((64+8))+rsp],r9
	mov	QWORD[((64+16))+rsp],r10
	mov	QWORD[((64+24))+rsp],r11
	mov	QWORD[((64+32))+rsp],r12
	mov	QWORD[((64+40))+rsp],r13
	mov	QWORD[((64+48))+rsp],r14
	mov	QWORD[((64+56))+rsp],r15

	mov	rdx,QWORD[128+rsp]
	mov	rdi,QWORD[((128+8))+rsp]
	mov	rbp,QWORD[((128+16))+rsp]

	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]

	call	__rsaz_512_reducex

$L$mul_gather_tail:
	add	r8,QWORD[64+rsp]
	adc	r9,QWORD[72+rsp]
	adc	r10,QWORD[80+rsp]
	adc	r11,QWORD[88+rsp]
	adc	r12,QWORD[96+rsp]
	adc	r13,QWORD[104+rsp]
	adc	r14,QWORD[112+rsp]
	adc	r15,QWORD[120+rsp]
	sbb	rcx,rcx

	call	__rsaz_512_subtract

	lea	rax,[((128+24+48))+rsp]
	movaps	xmm6,XMMWORD[((160-200))+rax]
	movaps	xmm7,XMMWORD[((176-200))+rax]
	movaps	xmm8,XMMWORD[((192-200))+rax]
	movaps	xmm9,XMMWORD[((208-200))+rax]
	movaps	xmm10,XMMWORD[((224-200))+rax]
	movaps	xmm11,XMMWORD[((240-200))+rax]
	movaps	xmm12,XMMWORD[((256-200))+rax]
	movaps	xmm13,XMMWORD[((272-200))+rax]
	movaps	xmm14,XMMWORD[((288-200))+rax]
	movaps	xmm15,XMMWORD[((304-200))+rax]
	lea	rax,[176+rax]

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
SEH64_ENDPROC_FRAME	rsaz_512_mul_gather4
$L$SEH_end_rsaz_512_mul_gather4:
global	rsaz_512_mul_scatter4

ALIGN	32
SEH64_PROC_FRAME rsaz_512_mul_scatter4,PUBLIC
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

	mov	r9d,r9d
	sub	rsp,128+24
SEH64_ALLOCSTACK 152
	; cfa_rsp=-208
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=208 cfa_rsp=-208
	lea	r8,[r9*8+r8]
DB	102,72,15,110,199
DB	102,72,15,110,202
DB	102,73,15,110,208
	mov	QWORD[128+rsp],rcx

	mov	rbp,rdi
	mov	r11d,0x80100
	and	r11d,DWORD[((OPENSSL_ia32cap_P+8))]
	cmp	r11d,0x80100
	je	NEAR $L$mulx_scatter
	mov	rbx,QWORD[rdi]
	call	__rsaz_512_mul

DB	102,72,15,126,199
DB	102,72,15,126,205

	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]

	call	__rsaz_512_reduce
	jmp	NEAR $L$mul_scatter_tail

ALIGN	32
$L$mulx_scatter:
	mov	rdx,QWORD[rdi]
	call	__rsaz_512_mulx

DB	102,72,15,126,199
DB	102,72,15,126,205

	mov	rdx,QWORD[128+rsp]
	mov	r8,QWORD[rsp]
	mov	r9,QWORD[8+rsp]
	mov	r10,QWORD[16+rsp]
	mov	r11,QWORD[24+rsp]
	mov	r12,QWORD[32+rsp]
	mov	r13,QWORD[40+rsp]
	mov	r14,QWORD[48+rsp]
	mov	r15,QWORD[56+rsp]

	call	__rsaz_512_reducex

$L$mul_scatter_tail:
	add	r8,QWORD[64+rsp]
	adc	r9,QWORD[72+rsp]
	adc	r10,QWORD[80+rsp]
	adc	r11,QWORD[88+rsp]
	adc	r12,QWORD[96+rsp]
	adc	r13,QWORD[104+rsp]
	adc	r14,QWORD[112+rsp]
	adc	r15,QWORD[120+rsp]
DB	102,72,15,126,214
	sbb	rcx,rcx

	call	__rsaz_512_subtract

	mov	QWORD[rsi],r8
	mov	QWORD[128+rsi],r9
	mov	QWORD[256+rsi],r10
	mov	QWORD[384+rsi],r11
	mov	QWORD[512+rsi],r12
	mov	QWORD[640+rsi],r13
	mov	QWORD[768+rsi],r14
	mov	QWORD[896+rsi],r15

	lea	rax,[((128+24+48))+rsp]

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
SEH64_ENDPROC_FRAME	rsaz_512_mul_scatter4
$L$SEH_end_rsaz_512_mul_scatter4:
global	rsaz_512_mul_by_one

ALIGN	32
SEH64_PROC_FRAME rsaz_512_mul_by_one,PUBLIC
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

	sub	rsp,128+24
SEH64_ALLOCSTACK 152
	; cfa_rsp=-208
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=208 cfa_rsp=-208
	mov	eax,DWORD[((OPENSSL_ia32cap_P+8))]
	mov	rbp,rdx
	mov	QWORD[128+rsp],rcx

	mov	r8,QWORD[rsi]
	pxor	xmm0,xmm0
	mov	r9,QWORD[8+rsi]
	mov	r10,QWORD[16+rsi]
	mov	r11,QWORD[24+rsi]
	mov	r12,QWORD[32+rsi]
	mov	r13,QWORD[40+rsi]
	mov	r14,QWORD[48+rsi]
	mov	r15,QWORD[56+rsi]

	movdqa	XMMWORD[rsp],xmm0
	movdqa	XMMWORD[16+rsp],xmm0
	movdqa	XMMWORD[32+rsp],xmm0
	movdqa	XMMWORD[48+rsp],xmm0
	movdqa	XMMWORD[64+rsp],xmm0
	movdqa	XMMWORD[80+rsp],xmm0
	movdqa	XMMWORD[96+rsp],xmm0
	and	eax,0x80100
	cmp	eax,0x80100
	je	NEAR $L$by_one_callx
	call	__rsaz_512_reduce
	jmp	NEAR $L$by_one_tail
ALIGN	32
$L$by_one_callx:
	mov	rdx,QWORD[128+rsp]
	call	__rsaz_512_reducex
$L$by_one_tail:
	mov	QWORD[rdi],r8
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11
	mov	QWORD[32+rdi],r12
	mov	QWORD[40+rdi],r13
	mov	QWORD[48+rdi],r14
	mov	QWORD[56+rdi],r15

	lea	rax,[((128+24+48))+rsp]

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
SEH64_ENDPROC_FRAME	rsaz_512_mul_by_one
$L$SEH_end_rsaz_512_mul_by_one:

ALIGN	32
SEH64_PROC_FRAME __rsaz_512_reduce,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	rbx,r8
	imul	rbx,QWORD[((128+8))+rsp]
	mov	rax,QWORD[rbp]
	mov	ecx,8
	jmp	NEAR $L$reduction_loop

ALIGN	32
$L$reduction_loop:
	mul	rbx
	mov	rax,QWORD[8+rbp]
	neg	r8
	mov	r8,rdx
	adc	r8,0

	mul	rbx
	add	r9,rax
	mov	rax,QWORD[16+rbp]
	adc	rdx,0
	add	r8,r9
	mov	r9,rdx
	adc	r9,0

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[24+rbp]
	adc	rdx,0
	add	r9,r10
	mov	r10,rdx
	adc	r10,0

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[32+rbp]
	adc	rdx,0
	add	r10,r11
	mov	rsi,QWORD[((128+8))+rsp]


	adc	rdx,0
	mov	r11,rdx

	mul	rbx
	add	r12,rax
	mov	rax,QWORD[40+rbp]
	adc	rdx,0
	imul	rsi,r8
	add	r11,r12
	mov	r12,rdx
	adc	r12,0

	mul	rbx
	add	r13,rax
	mov	rax,QWORD[48+rbp]
	adc	rdx,0
	add	r12,r13
	mov	r13,rdx
	adc	r13,0

	mul	rbx
	add	r14,rax
	mov	rax,QWORD[56+rbp]
	adc	rdx,0
	add	r13,r14
	mov	r14,rdx
	adc	r14,0

	mul	rbx
	mov	rbx,rsi
	add	r15,rax
	mov	rax,QWORD[rbp]
	adc	rdx,0
	add	r14,r15
	mov	r15,rdx
	adc	r15,0

	dec	ecx
	jne	NEAR $L$reduction_loop

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	__rsaz_512_reduce


ALIGN	32
SEH64_PROC_FRAME __rsaz_512_reducex,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8

	imul	rdx,r8
	xor	rsi,rsi
	mov	ecx,8
	jmp	NEAR $L$reduction_loopx

ALIGN	32
$L$reduction_loopx:
	mov	rbx,r8
	mulx	r8,rax,QWORD[rbp]
	adcx	rax,rbx
	adox	r8,r9

	mulx	r9,rax,QWORD[8+rbp]
	adcx	r8,rax
	adox	r9,r10

	mulx	r10,rbx,QWORD[16+rbp]
	adcx	r9,rbx
	adox	r10,r11

	mulx	r11,rbx,QWORD[24+rbp]
	adcx	r10,rbx
	adox	r11,r12

DB	0xc4,0x62,0xe3,0xf6,0xa5,0x20,0x00,0x00,0x00
	mov	rax,rdx
	mov	rdx,r8
	adcx	r11,rbx
	adox	r12,r13

	mulx	rdx,rbx,QWORD[((128+8))+rsp]
	mov	rdx,rax

	mulx	r13,rax,QWORD[40+rbp]
	adcx	r12,rax
	adox	r13,r14

DB	0xc4,0x62,0xfb,0xf6,0xb5,0x30,0x00,0x00,0x00
	adcx	r13,rax
	adox	r14,r15

	mulx	r15,rax,QWORD[56+rbp]
	mov	rdx,rbx
	adcx	r14,rax
	adox	r15,rsi
	adcx	r15,rsi

	dec	ecx
	jne	NEAR $L$reduction_loopx

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	__rsaz_512_reducex


ALIGN	32
SEH64_PROC_FRAME __rsaz_512_subtract,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	QWORD[rdi],r8
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11
	mov	QWORD[32+rdi],r12
	mov	QWORD[40+rdi],r13
	mov	QWORD[48+rdi],r14
	mov	QWORD[56+rdi],r15

	mov	r8,QWORD[rbp]
	mov	r9,QWORD[8+rbp]
	neg	r8
	not	r9
	and	r8,rcx
	mov	r10,QWORD[16+rbp]
	and	r9,rcx
	not	r10
	mov	r11,QWORD[24+rbp]
	and	r10,rcx
	not	r11
	mov	r12,QWORD[32+rbp]
	and	r11,rcx
	not	r12
	mov	r13,QWORD[40+rbp]
	and	r12,rcx
	not	r13
	mov	r14,QWORD[48+rbp]
	and	r13,rcx
	not	r14
	mov	r15,QWORD[56+rbp]
	and	r14,rcx
	not	r15
	and	r15,rcx

	add	r8,QWORD[rdi]
	adc	r9,QWORD[8+rdi]
	adc	r10,QWORD[16+rdi]
	adc	r11,QWORD[24+rdi]
	adc	r12,QWORD[32+rdi]
	adc	r13,QWORD[40+rdi]
	adc	r14,QWORD[48+rdi]
	adc	r15,QWORD[56+rdi]

	mov	QWORD[rdi],r8
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11
	mov	QWORD[32+rdi],r12
	mov	QWORD[40+rdi],r13
	mov	QWORD[48+rdi],r14
	mov	QWORD[56+rdi],r15

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	__rsaz_512_subtract


ALIGN	32
SEH64_PROC_FRAME __rsaz_512_mul,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	rdi,[8+rsp]

	mov	rax,QWORD[rsi]
	mul	rbx
	mov	QWORD[rdi],rax
	mov	rax,QWORD[8+rsi]
	mov	r8,rdx

	mul	rbx
	add	r8,rax
	mov	rax,QWORD[16+rsi]
	mov	r9,rdx
	adc	r9,0

	mul	rbx
	add	r9,rax
	mov	rax,QWORD[24+rsi]
	mov	r10,rdx
	adc	r10,0

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[32+rsi]
	mov	r11,rdx
	adc	r11,0

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[40+rsi]
	mov	r12,rdx
	adc	r12,0

	mul	rbx
	add	r12,rax
	mov	rax,QWORD[48+rsi]
	mov	r13,rdx
	adc	r13,0

	mul	rbx
	add	r13,rax
	mov	rax,QWORD[56+rsi]
	mov	r14,rdx
	adc	r14,0

	mul	rbx
	add	r14,rax
	mov	rax,QWORD[rsi]
	mov	r15,rdx
	adc	r15,0

	lea	rbp,[8+rbp]
	lea	rdi,[8+rdi]

	mov	ecx,7
	jmp	NEAR $L$oop_mul

ALIGN	32
$L$oop_mul:
	mov	rbx,QWORD[rbp]
	mul	rbx
	add	r8,rax
	mov	rax,QWORD[8+rsi]
	mov	QWORD[rdi],r8
	mov	r8,rdx
	adc	r8,0

	mul	rbx
	add	r9,rax
	mov	rax,QWORD[16+rsi]
	adc	rdx,0
	add	r8,r9
	mov	r9,rdx
	adc	r9,0

	mul	rbx
	add	r10,rax
	mov	rax,QWORD[24+rsi]
	adc	rdx,0
	add	r9,r10
	mov	r10,rdx
	adc	r10,0

	mul	rbx
	add	r11,rax
	mov	rax,QWORD[32+rsi]
	adc	rdx,0
	add	r10,r11
	mov	r11,rdx
	adc	r11,0

	mul	rbx
	add	r12,rax
	mov	rax,QWORD[40+rsi]
	adc	rdx,0
	add	r11,r12
	mov	r12,rdx
	adc	r12,0

	mul	rbx
	add	r13,rax
	mov	rax,QWORD[48+rsi]
	adc	rdx,0
	add	r12,r13
	mov	r13,rdx
	adc	r13,0

	mul	rbx
	add	r14,rax
	mov	rax,QWORD[56+rsi]
	adc	rdx,0
	add	r13,r14
	mov	r14,rdx
	lea	rbp,[8+rbp]
	adc	r14,0

	mul	rbx
	add	r15,rax
	mov	rax,QWORD[rsi]
	adc	rdx,0
	add	r14,r15
	mov	r15,rdx
	adc	r15,0

	lea	rdi,[8+rdi]

	dec	ecx
	jnz	NEAR $L$oop_mul

	mov	QWORD[rdi],r8
	mov	QWORD[8+rdi],r9
	mov	QWORD[16+rdi],r10
	mov	QWORD[24+rdi],r11
	mov	QWORD[32+rdi],r12
	mov	QWORD[40+rdi],r13
	mov	QWORD[48+rdi],r14
	mov	QWORD[56+rdi],r15

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	__rsaz_512_mul


ALIGN	32
SEH64_PROC_FRAME __rsaz_512_mulx,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mulx	r8,rbx,QWORD[rsi]
	mov	rcx,-6

	mulx	r9,rax,QWORD[8+rsi]
	mov	QWORD[8+rsp],rbx

	mulx	r10,rbx,QWORD[16+rsi]
	adc	r8,rax

	mulx	r11,rax,QWORD[24+rsi]
	adc	r9,rbx

	mulx	r12,rbx,QWORD[32+rsi]
	adc	r10,rax

	mulx	r13,rax,QWORD[40+rsi]
	adc	r11,rbx

	mulx	r14,rbx,QWORD[48+rsi]
	adc	r12,rax

	mulx	r15,rax,QWORD[56+rsi]
	mov	rdx,QWORD[8+rbp]
	adc	r13,rbx
	adc	r14,rax
	adc	r15,0

	xor	rdi,rdi
	jmp	NEAR $L$oop_mulx

ALIGN	32
$L$oop_mulx:
	mov	rbx,r8
	mulx	r8,rax,QWORD[rsi]
	adcx	rbx,rax
	adox	r8,r9

	mulx	r9,rax,QWORD[8+rsi]
	adcx	r8,rax
	adox	r9,r10

	mulx	r10,rax,QWORD[16+rsi]
	adcx	r9,rax
	adox	r10,r11

	mulx	r11,rax,QWORD[24+rsi]
	adcx	r10,rax
	adox	r11,r12

DB	0x3e,0xc4,0x62,0xfb,0xf6,0xa6,0x20,0x00,0x00,0x00
	adcx	r11,rax
	adox	r12,r13

	mulx	r13,rax,QWORD[40+rsi]
	adcx	r12,rax
	adox	r13,r14

	mulx	r14,rax,QWORD[48+rsi]
	adcx	r13,rax
	adox	r14,r15

	mulx	r15,rax,QWORD[56+rsi]
	mov	rdx,QWORD[64+rcx*8+rbp]
	mov	QWORD[((8+64-8))+rcx*8+rsp],rbx
	adcx	r14,rax
	adox	r15,rdi
	adcx	r15,rdi

	inc	rcx
	jnz	NEAR $L$oop_mulx

	mov	rbx,r8
	mulx	r8,rax,QWORD[rsi]
	adcx	rbx,rax
	adox	r8,r9

DB	0xc4,0x62,0xfb,0xf6,0x8e,0x08,0x00,0x00,0x00
	adcx	r8,rax
	adox	r9,r10

DB	0xc4,0x62,0xfb,0xf6,0x96,0x10,0x00,0x00,0x00
	adcx	r9,rax
	adox	r10,r11

	mulx	r11,rax,QWORD[24+rsi]
	adcx	r10,rax
	adox	r11,r12

	mulx	r12,rax,QWORD[32+rsi]
	adcx	r11,rax
	adox	r12,r13

	mulx	r13,rax,QWORD[40+rsi]
	adcx	r12,rax
	adox	r13,r14

DB	0xc4,0x62,0xfb,0xf6,0xb6,0x30,0x00,0x00,0x00
	adcx	r13,rax
	adox	r14,r15

DB	0xc4,0x62,0xfb,0xf6,0xbe,0x38,0x00,0x00,0x00
	adcx	r14,rax
	adox	r15,rdi
	adcx	r15,rdi

	mov	QWORD[((8+64-8))+rsp],rbx
	mov	QWORD[((8+64))+rsp],r8
	mov	QWORD[((8+64+8))+rsp],r9
	mov	QWORD[((8+64+16))+rsp],r10
	mov	QWORD[((8+64+24))+rsp],r11
	mov	QWORD[((8+64+32))+rsp],r12
	mov	QWORD[((8+64+40))+rsp],r13
	mov	QWORD[((8+64+48))+rsp],r14
	mov	QWORD[((8+64+56))+rsp],r15

	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	__rsaz_512_mulx

global	rsaz_512_scatter4

ALIGN	16
SEH64_PROC_FRAME rsaz_512_scatter4,PUBLIC

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	rcx,[r8*8+rcx]
	mov	r9d,8
	jmp	NEAR $L$oop_scatter
ALIGN	16
$L$oop_scatter:
	mov	rax,QWORD[rdx]
	lea	rdx,[8+rdx]
	mov	QWORD[rcx],rax
	lea	rcx,[128+rcx]
	dec	r9d
	jnz	NEAR $L$oop_scatter
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_512_scatter4


global	rsaz_512_gather4

ALIGN	16
SEH64_PROC_FRAME rsaz_512_gather4,PUBLIC

DB	0x48,0x81,0xec,0xa8,0x00,0x00,0x00
SEH64_ALLOCSTACK 168
DB	0x0f,0x29,0x34,0x24
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -176 cfa_reg_offset=176 cfa_rsp=-176
DB	0x0f,0x29,0x7c,0x24,0x10
SEH64_SAVEXMM128 xmm7, 16	; .cfi_offset xmm7, -160 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x44,0x24,0x20
SEH64_SAVEXMM128 xmm8, 32	; .cfi_offset xmm8, -144 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x4c,0x24,0x30
SEH64_SAVEXMM128 xmm9, 48	; .cfi_offset xmm9, -128 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x54,0x24,0x40
SEH64_SAVEXMM128 xmm10, 64	; .cfi_offset xmm10, -112 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x5c,0x24,0x50
SEH64_SAVEXMM128 xmm11, 80	; .cfi_offset xmm11, -96 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x64,0x24,0x60
SEH64_SAVEXMM128 xmm12, 96	; .cfi_offset xmm12, -80 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0x6c,0x24,0x70
SEH64_SAVEXMM128 xmm13, 112	; .cfi_offset xmm13, -64 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0xb4,0x24,0x80,0,0,0
SEH64_SAVEXMM128 xmm14, 128	; .cfi_offset xmm14, -48 cfa_reg_offset=176 cfa_rsp=-176
DB	0x44,0x0f,0x29,0xbc,0x24,0x90,0,0,0
SEH64_SAVEXMM128 xmm15, 144	; .cfi_offset xmm15, -32 cfa_reg_offset=176 cfa_rsp=-176
	; cfa_rsp=-176
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=176 cfa_rsp=-176
	movd	xmm8,r8d
	movdqa	xmm1,XMMWORD[(($L$inc+16))]
	movdqa	xmm0,XMMWORD[$L$inc]

	pshufd	xmm8,xmm8,0
	movdqa	xmm7,xmm1
	movdqa	xmm2,xmm1
	paddd	xmm1,xmm0
	pcmpeqd	xmm0,xmm8
	movdqa	xmm3,xmm7
	paddd	xmm2,xmm1
	pcmpeqd	xmm1,xmm8
	movdqa	xmm4,xmm7
	paddd	xmm3,xmm2
	pcmpeqd	xmm2,xmm8
	movdqa	xmm5,xmm7
	paddd	xmm4,xmm3
	pcmpeqd	xmm3,xmm8
	movdqa	xmm6,xmm7
	paddd	xmm5,xmm4
	pcmpeqd	xmm4,xmm8
	paddd	xmm6,xmm5
	pcmpeqd	xmm5,xmm8
	paddd	xmm7,xmm6
	pcmpeqd	xmm6,xmm8
	pcmpeqd	xmm7,xmm8
	mov	r9d,8
	jmp	NEAR $L$oop_gather
ALIGN	16
$L$oop_gather:
	movdqa	xmm8,XMMWORD[rdx]
	movdqa	xmm9,XMMWORD[16+rdx]
	movdqa	xmm10,XMMWORD[32+rdx]
	movdqa	xmm11,XMMWORD[48+rdx]
	pand	xmm8,xmm0
	movdqa	xmm12,XMMWORD[64+rdx]
	pand	xmm9,xmm1
	movdqa	xmm13,XMMWORD[80+rdx]
	pand	xmm10,xmm2
	movdqa	xmm14,XMMWORD[96+rdx]
	pand	xmm11,xmm3
	movdqa	xmm15,XMMWORD[112+rdx]
	lea	rdx,[128+rdx]
	pand	xmm12,xmm4
	pand	xmm13,xmm5
	pand	xmm14,xmm6
	pand	xmm15,xmm7
	por	xmm8,xmm10
	por	xmm9,xmm11
	por	xmm8,xmm12
	por	xmm9,xmm13
	por	xmm8,xmm14
	por	xmm9,xmm15

	por	xmm8,xmm9
	pshufd	xmm9,xmm8,0x4e
	por	xmm8,xmm9
	movq	QWORD[rcx],xmm8
	lea	rcx,[8+rcx]
	dec	r9d
	jnz	NEAR $L$oop_gather
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
	add	rsp,0xa8
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	rsaz_512_gather4


section	.rdata rdata align=64
ALIGN	64
$L$inc:
	DD	0,0,1,1
	DD	2,2,2,2
section .text
