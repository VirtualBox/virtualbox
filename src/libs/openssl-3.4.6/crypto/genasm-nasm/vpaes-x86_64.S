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
SEH64_PROC_FRAME _vpaes_encrypt_core,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r9,rdx
	mov	r11,16
	mov	eax,DWORD[240+rdx]
	movdqa	xmm1,xmm9
	movdqa	xmm2,XMMWORD[$L$k_ipt]
	pandn	xmm1,xmm0
	movdqu	xmm5,XMMWORD[r9]
	psrld	xmm1,4
	pand	xmm0,xmm9
DB	102,15,56,0,208
	movdqa	xmm0,XMMWORD[(($L$k_ipt+16))]
DB	102,15,56,0,193
	pxor	xmm2,xmm5
	add	r9,16
	pxor	xmm0,xmm2
	lea	r10,[$L$k_mc_backward]
	jmp	NEAR $L$enc_entry

ALIGN	16
$L$enc_loop:

	movdqa	xmm4,xmm13
	movdqa	xmm0,xmm12
DB	102,15,56,0,226
DB	102,15,56,0,195
	pxor	xmm4,xmm5
	movdqa	xmm5,xmm15
	pxor	xmm0,xmm4
	movdqa	xmm1,XMMWORD[((-64))+r10*1+r11]
DB	102,15,56,0,234
	movdqa	xmm4,XMMWORD[r10*1+r11]
	movdqa	xmm2,xmm14
DB	102,15,56,0,211
	movdqa	xmm3,xmm0
	pxor	xmm2,xmm5
DB	102,15,56,0,193
	add	r9,16
	pxor	xmm0,xmm2
DB	102,15,56,0,220
	add	r11,16
	pxor	xmm3,xmm0
DB	102,15,56,0,193
	and	r11,0x30
	sub	rax,1
	pxor	xmm0,xmm3

$L$enc_entry:

	movdqa	xmm1,xmm9
	movdqa	xmm5,xmm11
	pandn	xmm1,xmm0
	psrld	xmm1,4
	pand	xmm0,xmm9
DB	102,15,56,0,232
	movdqa	xmm3,xmm10
	pxor	xmm0,xmm1
DB	102,15,56,0,217
	movdqa	xmm4,xmm10
	pxor	xmm3,xmm5
DB	102,15,56,0,224
	movdqa	xmm2,xmm10
	pxor	xmm4,xmm5
DB	102,15,56,0,211
	movdqa	xmm3,xmm10
	pxor	xmm2,xmm0
DB	102,15,56,0,220
	movdqu	xmm5,XMMWORD[r9]
	pxor	xmm3,xmm1
	jnz	NEAR $L$enc_loop


	movdqa	xmm4,XMMWORD[((-96))+r10]
	movdqa	xmm0,XMMWORD[((-80))+r10]
DB	102,15,56,0,226
	pxor	xmm4,xmm5
DB	102,15,56,0,195
	movdqa	xmm1,XMMWORD[64+r10*1+r11]
	pxor	xmm0,xmm4
DB	102,15,56,0,193
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_encrypt_core








ALIGN	16
SEH64_PROC_FRAME _vpaes_decrypt_core,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	mov	r9,rdx
	mov	eax,DWORD[240+rdx]
	movdqa	xmm1,xmm9
	movdqa	xmm2,XMMWORD[$L$k_dipt]
	pandn	xmm1,xmm0
	mov	r11,rax
	psrld	xmm1,4
	movdqu	xmm5,XMMWORD[r9]
	shl	r11,4
	pand	xmm0,xmm9
DB	102,15,56,0,208
	movdqa	xmm0,XMMWORD[(($L$k_dipt+16))]
	xor	r11,0x30
	lea	r10,[$L$k_dsbd]
DB	102,15,56,0,193
	and	r11,0x30
	pxor	xmm2,xmm5
	movdqa	xmm5,XMMWORD[(($L$k_mc_forward+48))]
	pxor	xmm0,xmm2
	add	r9,16
	add	r11,r10
	jmp	NEAR $L$dec_entry

ALIGN	16
$L$dec_loop:



	movdqa	xmm4,XMMWORD[((-32))+r10]
	movdqa	xmm1,XMMWORD[((-16))+r10]
DB	102,15,56,0,226
DB	102,15,56,0,203
	pxor	xmm0,xmm4
	movdqa	xmm4,XMMWORD[r10]
	pxor	xmm0,xmm1
	movdqa	xmm1,XMMWORD[16+r10]

DB	102,15,56,0,226
DB	102,15,56,0,197
DB	102,15,56,0,203
	pxor	xmm0,xmm4
	movdqa	xmm4,XMMWORD[32+r10]
	pxor	xmm0,xmm1
	movdqa	xmm1,XMMWORD[48+r10]

DB	102,15,56,0,226
DB	102,15,56,0,197
DB	102,15,56,0,203
	pxor	xmm0,xmm4
	movdqa	xmm4,XMMWORD[64+r10]
	pxor	xmm0,xmm1
	movdqa	xmm1,XMMWORD[80+r10]

DB	102,15,56,0,226
DB	102,15,56,0,197
DB	102,15,56,0,203
	pxor	xmm0,xmm4
	add	r9,16
DB	102,15,58,15,237,12
	pxor	xmm0,xmm1
	sub	rax,1

$L$dec_entry:

	movdqa	xmm1,xmm9
	pandn	xmm1,xmm0
	movdqa	xmm2,xmm11
	psrld	xmm1,4
	pand	xmm0,xmm9
DB	102,15,56,0,208
	movdqa	xmm3,xmm10
	pxor	xmm0,xmm1
DB	102,15,56,0,217
	movdqa	xmm4,xmm10
	pxor	xmm3,xmm2
DB	102,15,56,0,224
	pxor	xmm4,xmm2
	movdqa	xmm2,xmm10
DB	102,15,56,0,211
	movdqa	xmm3,xmm10
	pxor	xmm2,xmm0
DB	102,15,56,0,220
	movdqu	xmm0,XMMWORD[r9]
	pxor	xmm3,xmm1
	jnz	NEAR $L$dec_loop


	movdqa	xmm4,XMMWORD[96+r10]
DB	102,15,56,0,226
	pxor	xmm4,xmm0
	movdqa	xmm0,XMMWORD[112+r10]
	movdqa	xmm2,XMMWORD[((-352))+r11]
DB	102,15,56,0,195
	pxor	xmm0,xmm4
DB	102,15,56,0,194
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_decrypt_core








ALIGN	16
SEH64_PROC_FRAME _vpaes_schedule_core,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8





	call	_vpaes_preheat
	movdqa	xmm8,XMMWORD[$L$k_rcon]
	movdqu	xmm0,XMMWORD[rdi]


	movdqa	xmm3,xmm0
	lea	r11,[$L$k_ipt]
	call	_vpaes_schedule_transform
	movdqa	xmm7,xmm0

	lea	r10,[$L$k_sr]
	test	rcx,rcx
	jnz	NEAR $L$schedule_am_decrypting


	movdqu	XMMWORD[rdx],xmm0
	jmp	NEAR $L$schedule_go

$L$schedule_am_decrypting:

	movdqa	xmm1,XMMWORD[r10*1+r8]
DB	102,15,56,0,217
	movdqu	XMMWORD[rdx],xmm3
	xor	r8,0x30

$L$schedule_go:
	cmp	esi,192
	ja	NEAR $L$schedule_256
	je	NEAR $L$schedule_192










$L$schedule_128:
	mov	esi,10

$L$oop_schedule_128:
	call	_vpaes_schedule_round
	dec	rsi
	jz	NEAR $L$schedule_mangle_last
	call	_vpaes_schedule_mangle
	jmp	NEAR $L$oop_schedule_128
















ALIGN	16
$L$schedule_192:
	movdqu	xmm0,XMMWORD[8+rdi]
	call	_vpaes_schedule_transform
	movdqa	xmm6,xmm0
	pxor	xmm4,xmm4
	movhlps	xmm6,xmm4
	mov	esi,4

$L$oop_schedule_192:
	call	_vpaes_schedule_round
DB	102,15,58,15,198,8
	call	_vpaes_schedule_mangle
	call	_vpaes_schedule_192_smear
	call	_vpaes_schedule_mangle
	call	_vpaes_schedule_round
	dec	rsi
	jz	NEAR $L$schedule_mangle_last
	call	_vpaes_schedule_mangle
	call	_vpaes_schedule_192_smear
	jmp	NEAR $L$oop_schedule_192











ALIGN	16
$L$schedule_256:
	movdqu	xmm0,XMMWORD[16+rdi]
	call	_vpaes_schedule_transform
	mov	esi,7

$L$oop_schedule_256:
	call	_vpaes_schedule_mangle
	movdqa	xmm6,xmm0


	call	_vpaes_schedule_round
	dec	rsi
	jz	NEAR $L$schedule_mangle_last
	call	_vpaes_schedule_mangle


	pshufd	xmm0,xmm0,0xFF
	movdqa	xmm5,xmm7
	movdqa	xmm7,xmm6
	call	_vpaes_schedule_low_round
	movdqa	xmm7,xmm5

	jmp	NEAR $L$oop_schedule_256












ALIGN	16
$L$schedule_mangle_last:

	lea	r11,[$L$k_deskew]
	test	rcx,rcx
	jnz	NEAR $L$schedule_mangle_last_dec


	movdqa	xmm1,XMMWORD[r10*1+r8]
DB	102,15,56,0,193
	lea	r11,[$L$k_opt]
	add	rdx,32

$L$schedule_mangle_last_dec:
	add	rdx,-16
	pxor	xmm0,XMMWORD[$L$k_s63]
	call	_vpaes_schedule_transform
	movdqu	XMMWORD[rdx],xmm0


	pxor	xmm0,xmm0
	pxor	xmm1,xmm1
	pxor	xmm2,xmm2
	pxor	xmm3,xmm3
	pxor	xmm4,xmm4
	pxor	xmm5,xmm5
	pxor	xmm6,xmm6
	pxor	xmm7,xmm7
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_schedule_core

















ALIGN	16
SEH64_PROC_FRAME _vpaes_schedule_192_smear,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	pshufd	xmm1,xmm6,0x80
	pshufd	xmm0,xmm7,0xFE
	pxor	xmm6,xmm1
	pxor	xmm1,xmm1
	pxor	xmm6,xmm0
	movdqa	xmm0,xmm6
	movhlps	xmm6,xmm1
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_schedule_192_smear





















ALIGN	16
SEH64_PROC_FRAME _vpaes_schedule_round,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8

	pxor	xmm1,xmm1
DB	102,65,15,58,15,200,15
DB	102,69,15,58,15,192,15
	pxor	xmm7,xmm1


	pshufd	xmm0,xmm0,0xFF
DB	102,15,58,15,192,1




_vpaes_schedule_low_round:

	movdqa	xmm1,xmm7
	pslldq	xmm7,4
	pxor	xmm7,xmm1
	movdqa	xmm1,xmm7
	pslldq	xmm7,8
	pxor	xmm7,xmm1
	pxor	xmm7,XMMWORD[$L$k_s63]


	movdqa	xmm1,xmm9
	pandn	xmm1,xmm0
	psrld	xmm1,4
	pand	xmm0,xmm9
	movdqa	xmm2,xmm11
DB	102,15,56,0,208
	pxor	xmm0,xmm1
	movdqa	xmm3,xmm10
DB	102,15,56,0,217
	pxor	xmm3,xmm2
	movdqa	xmm4,xmm10
DB	102,15,56,0,224
	pxor	xmm4,xmm2
	movdqa	xmm2,xmm10
DB	102,15,56,0,211
	pxor	xmm2,xmm0
	movdqa	xmm3,xmm10
DB	102,15,56,0,220
	pxor	xmm3,xmm1
	movdqa	xmm4,xmm13
DB	102,15,56,0,226
	movdqa	xmm0,xmm12
DB	102,15,56,0,195
	pxor	xmm0,xmm4


	pxor	xmm0,xmm7
	movdqa	xmm7,xmm0
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_schedule_round












ALIGN	16
SEH64_PROC_FRAME _vpaes_schedule_transform,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	movdqa	xmm1,xmm9
	pandn	xmm1,xmm0
	psrld	xmm1,4
	pand	xmm0,xmm9
	movdqa	xmm2,XMMWORD[r11]
DB	102,15,56,0,208
	movdqa	xmm0,XMMWORD[16+r11]
DB	102,15,56,0,193
	pxor	xmm0,xmm2
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_schedule_transform


























ALIGN	16
SEH64_PROC_FRAME _vpaes_schedule_mangle,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	movdqa	xmm4,xmm0
	movdqa	xmm5,XMMWORD[$L$k_mc_forward]
	test	rcx,rcx
	jnz	NEAR $L$schedule_mangle_dec


	add	rdx,16
	pxor	xmm4,XMMWORD[$L$k_s63]
DB	102,15,56,0,229
	movdqa	xmm3,xmm4
DB	102,15,56,0,229
	pxor	xmm3,xmm4
DB	102,15,56,0,229
	pxor	xmm3,xmm4

	jmp	NEAR $L$schedule_mangle_both
ALIGN	16
$L$schedule_mangle_dec:

	lea	r11,[$L$k_dksd]
	movdqa	xmm1,xmm9
	pandn	xmm1,xmm4
	psrld	xmm1,4
	pand	xmm4,xmm9

	movdqa	xmm2,XMMWORD[r11]
DB	102,15,56,0,212
	movdqa	xmm3,XMMWORD[16+r11]
DB	102,15,56,0,217
	pxor	xmm3,xmm2
DB	102,15,56,0,221

	movdqa	xmm2,XMMWORD[32+r11]
DB	102,15,56,0,212
	pxor	xmm2,xmm3
	movdqa	xmm3,XMMWORD[48+r11]
DB	102,15,56,0,217
	pxor	xmm3,xmm2
DB	102,15,56,0,221

	movdqa	xmm2,XMMWORD[64+r11]
DB	102,15,56,0,212
	pxor	xmm2,xmm3
	movdqa	xmm3,XMMWORD[80+r11]
DB	102,15,56,0,217
	pxor	xmm3,xmm2
DB	102,15,56,0,221

	movdqa	xmm2,XMMWORD[96+r11]
DB	102,15,56,0,212
	pxor	xmm2,xmm3
	movdqa	xmm3,XMMWORD[112+r11]
DB	102,15,56,0,217
	pxor	xmm3,xmm2

	add	rdx,-16

$L$schedule_mangle_both:
	movdqa	xmm1,XMMWORD[r10*1+r8]
DB	102,15,56,0,217
	add	r8,-16
	and	r8,0x30
	movdqu	XMMWORD[rdx],xmm3
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_schedule_mangle





global	vpaes_set_encrypt_key

ALIGN	16
SEH64_PROC_FRAME vpaes_set_encrypt_key,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



DB	243,15,30,250
	lea	rsp,[((-184))+rsp]
SEH64_ALLOCSTACK 184
	movaps	XMMWORD[16+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 16	; .cfi_offset xmm6, -176 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[32+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 32	; .cfi_offset xmm7, -160 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[48+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 48	; .cfi_offset xmm8, -144 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[64+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 64	; .cfi_offset xmm9, -128 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[80+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 80	; .cfi_offset xmm10, -112 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[96+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 96	; .cfi_offset xmm11, -96 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[112+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 112	; .cfi_offset xmm12, -80 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[128+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 128	; .cfi_offset xmm13, -64 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[144+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 144	; .cfi_offset xmm14, -48 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[160+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 160	; .cfi_offset xmm15, -32 cfa_reg_offset=192 cfa_rsp=-192
	; cfa_rsp=-192
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=192 cfa_rsp=-192
	mov	eax,esi
	shr	eax,5
	add	eax,5
	mov	DWORD[240+rdx],eax

	mov	ecx,0
	mov	r8d,0x30
	call	_vpaes_schedule_core
	movaps	xmm6,XMMWORD[16+rsp]
	movaps	xmm7,XMMWORD[32+rsp]
	movaps	xmm8,XMMWORD[48+rsp]
	movaps	xmm9,XMMWORD[64+rsp]
	movaps	xmm10,XMMWORD[80+rsp]
	movaps	xmm11,XMMWORD[96+rsp]
	movaps	xmm12,XMMWORD[112+rsp]
	movaps	xmm13,XMMWORD[128+rsp]
	movaps	xmm14,XMMWORD[144+rsp]
	movaps	xmm15,XMMWORD[160+rsp]
	lea	rsp,[184+rsp]
	xor	eax,eax
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	vpaes_set_encrypt_key
$L$SEH_end_vpaes_set_encrypt_key:

global	vpaes_set_decrypt_key

ALIGN	16
SEH64_PROC_FRAME vpaes_set_decrypt_key,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



DB	243,15,30,250
	lea	rsp,[((-184))+rsp]
SEH64_ALLOCSTACK 184
	movaps	XMMWORD[16+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 16	; .cfi_offset xmm6, -176 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[32+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 32	; .cfi_offset xmm7, -160 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[48+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 48	; .cfi_offset xmm8, -144 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[64+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 64	; .cfi_offset xmm9, -128 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[80+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 80	; .cfi_offset xmm10, -112 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[96+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 96	; .cfi_offset xmm11, -96 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[112+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 112	; .cfi_offset xmm12, -80 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[128+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 128	; .cfi_offset xmm13, -64 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[144+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 144	; .cfi_offset xmm14, -48 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[160+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 160	; .cfi_offset xmm15, -32 cfa_reg_offset=192 cfa_rsp=-192
	; cfa_rsp=-192
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=192 cfa_rsp=-192
	mov	eax,esi
	shr	eax,5
	add	eax,5
	mov	DWORD[240+rdx],eax
	shl	eax,4
	lea	rdx,[16+rax*1+rdx]

	mov	ecx,1
	mov	r8d,esi
	shr	r8d,1
	and	r8d,32
	xor	r8d,32
	call	_vpaes_schedule_core
	movaps	xmm6,XMMWORD[16+rsp]
	movaps	xmm7,XMMWORD[32+rsp]
	movaps	xmm8,XMMWORD[48+rsp]
	movaps	xmm9,XMMWORD[64+rsp]
	movaps	xmm10,XMMWORD[80+rsp]
	movaps	xmm11,XMMWORD[96+rsp]
	movaps	xmm12,XMMWORD[112+rsp]
	movaps	xmm13,XMMWORD[128+rsp]
	movaps	xmm14,XMMWORD[144+rsp]
	movaps	xmm15,XMMWORD[160+rsp]
	lea	rsp,[184+rsp]
	xor	eax,eax
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	vpaes_set_decrypt_key
$L$SEH_end_vpaes_set_decrypt_key:

global	vpaes_encrypt

ALIGN	16
SEH64_PROC_FRAME vpaes_encrypt,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



DB	243,15,30,250
	lea	rsp,[((-184))+rsp]
SEH64_ALLOCSTACK 184
	movaps	XMMWORD[16+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 16	; .cfi_offset xmm6, -176 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[32+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 32	; .cfi_offset xmm7, -160 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[48+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 48	; .cfi_offset xmm8, -144 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[64+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 64	; .cfi_offset xmm9, -128 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[80+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 80	; .cfi_offset xmm10, -112 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[96+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 96	; .cfi_offset xmm11, -96 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[112+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 112	; .cfi_offset xmm12, -80 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[128+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 128	; .cfi_offset xmm13, -64 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[144+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 144	; .cfi_offset xmm14, -48 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[160+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 160	; .cfi_offset xmm15, -32 cfa_reg_offset=192 cfa_rsp=-192
	; cfa_rsp=-192
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=192 cfa_rsp=-192
	movdqu	xmm0,XMMWORD[rdi]
	call	_vpaes_preheat
	call	_vpaes_encrypt_core
	movdqu	XMMWORD[rsi],xmm0
	movaps	xmm6,XMMWORD[16+rsp]
	movaps	xmm7,XMMWORD[32+rsp]
	movaps	xmm8,XMMWORD[48+rsp]
	movaps	xmm9,XMMWORD[64+rsp]
	movaps	xmm10,XMMWORD[80+rsp]
	movaps	xmm11,XMMWORD[96+rsp]
	movaps	xmm12,XMMWORD[112+rsp]
	movaps	xmm13,XMMWORD[128+rsp]
	movaps	xmm14,XMMWORD[144+rsp]
	movaps	xmm15,XMMWORD[160+rsp]
	lea	rsp,[184+rsp]
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	vpaes_encrypt
$L$SEH_end_vpaes_encrypt:

global	vpaes_decrypt

ALIGN	16
SEH64_PROC_FRAME vpaes_decrypt,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



DB	243,15,30,250
	lea	rsp,[((-184))+rsp]
SEH64_ALLOCSTACK 184
	movaps	XMMWORD[16+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 16	; .cfi_offset xmm6, -176 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[32+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 32	; .cfi_offset xmm7, -160 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[48+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 48	; .cfi_offset xmm8, -144 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[64+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 64	; .cfi_offset xmm9, -128 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[80+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 80	; .cfi_offset xmm10, -112 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[96+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 96	; .cfi_offset xmm11, -96 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[112+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 112	; .cfi_offset xmm12, -80 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[128+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 128	; .cfi_offset xmm13, -64 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[144+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 144	; .cfi_offset xmm14, -48 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[160+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 160	; .cfi_offset xmm15, -32 cfa_reg_offset=192 cfa_rsp=-192
	; cfa_rsp=-192
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=192 cfa_rsp=-192
	movdqu	xmm0,XMMWORD[rdi]
	call	_vpaes_preheat
	call	_vpaes_decrypt_core
	movdqu	XMMWORD[rsi],xmm0
	movaps	xmm6,XMMWORD[16+rsp]
	movaps	xmm7,XMMWORD[32+rsp]
	movaps	xmm8,XMMWORD[48+rsp]
	movaps	xmm9,XMMWORD[64+rsp]
	movaps	xmm10,XMMWORD[80+rsp]
	movaps	xmm11,XMMWORD[96+rsp]
	movaps	xmm12,XMMWORD[112+rsp]
	movaps	xmm13,XMMWORD[128+rsp]
	movaps	xmm14,XMMWORD[144+rsp]
	movaps	xmm15,XMMWORD[160+rsp]
	lea	rsp,[184+rsp]
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	vpaes_decrypt
$L$SEH_end_vpaes_decrypt:
global	vpaes_cbc_encrypt

ALIGN	16
SEH64_PROC_FRAME vpaes_cbc_encrypt,PUBLIC
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



DB	243,15,30,250
	xchg	rdx,rcx
	sub	rcx,16
	jc	NEAR $L$cbc_abort
	lea	rsp,[((-184))+rsp]
SEH64_ALLOCSTACK 184
	movaps	XMMWORD[16+rsp],xmm6
SEH64_SAVEXMM128 xmm6, 16	; .cfi_offset xmm6, -176 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[32+rsp],xmm7
SEH64_SAVEXMM128 xmm7, 32	; .cfi_offset xmm7, -160 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[48+rsp],xmm8
SEH64_SAVEXMM128 xmm8, 48	; .cfi_offset xmm8, -144 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[64+rsp],xmm9
SEH64_SAVEXMM128 xmm9, 64	; .cfi_offset xmm9, -128 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[80+rsp],xmm10
SEH64_SAVEXMM128 xmm10, 80	; .cfi_offset xmm10, -112 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[96+rsp],xmm11
SEH64_SAVEXMM128 xmm11, 96	; .cfi_offset xmm11, -96 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[112+rsp],xmm12
SEH64_SAVEXMM128 xmm12, 112	; .cfi_offset xmm12, -80 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[128+rsp],xmm13
SEH64_SAVEXMM128 xmm13, 128	; .cfi_offset xmm13, -64 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[144+rsp],xmm14
SEH64_SAVEXMM128 xmm14, 144	; .cfi_offset xmm14, -48 cfa_reg_offset=192 cfa_rsp=-192
	movaps	XMMWORD[160+rsp],xmm15
SEH64_SAVEXMM128 xmm15, 160	; .cfi_offset xmm15, -32 cfa_reg_offset=192 cfa_rsp=-192
	; cfa_rsp=-192
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=192 cfa_rsp=-192
	movdqu	xmm6,XMMWORD[r8]
	sub	rsi,rdi
	call	_vpaes_preheat
	cmp	r9d,0
	je	NEAR $L$cbc_dec_loop
	jmp	NEAR $L$cbc_enc_loop
ALIGN	16
$L$cbc_enc_loop:
	movdqu	xmm0,XMMWORD[rdi]
	pxor	xmm0,xmm6
	call	_vpaes_encrypt_core
	movdqa	xmm6,xmm0
	movdqu	XMMWORD[rdi*1+rsi],xmm0
	lea	rdi,[16+rdi]
	sub	rcx,16
	jnc	NEAR $L$cbc_enc_loop
	jmp	NEAR $L$cbc_done
ALIGN	16
$L$cbc_dec_loop:
	movdqu	xmm0,XMMWORD[rdi]
	movdqa	xmm7,xmm0
	call	_vpaes_decrypt_core
	pxor	xmm0,xmm6
	movdqa	xmm6,xmm7
	movdqu	XMMWORD[rdi*1+rsi],xmm0
	lea	rdi,[16+rdi]
	sub	rcx,16
	jnc	NEAR $L$cbc_dec_loop
$L$cbc_done:
	movdqu	XMMWORD[r8],xmm6
	movaps	xmm6,XMMWORD[16+rsp]
	movaps	xmm7,XMMWORD[32+rsp]
	movaps	xmm8,XMMWORD[48+rsp]
	movaps	xmm9,XMMWORD[64+rsp]
	movaps	xmm10,XMMWORD[80+rsp]
	movaps	xmm11,XMMWORD[96+rsp]
	movaps	xmm12,XMMWORD[112+rsp]
	movaps	xmm13,XMMWORD[128+rsp]
	movaps	xmm14,XMMWORD[144+rsp]
	movaps	xmm15,XMMWORD[160+rsp]
	lea	rsp,[184+rsp]
$L$cbc_abort:
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	vpaes_cbc_encrypt
$L$SEH_end_vpaes_cbc_encrypt:







ALIGN	16
SEH64_PROC_FRAME _vpaes_preheat,PRIVATE

	; cfa_rsp=-8
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=8 cfa_rsp=-8
	lea	r10,[$L$k_s0F]
	movdqa	xmm10,XMMWORD[((-32))+r10]
	movdqa	xmm11,XMMWORD[((-16))+r10]
	movdqa	xmm9,XMMWORD[r10]
	movdqa	xmm13,XMMWORD[48+r10]
	movdqa	xmm12,XMMWORD[64+r10]
	movdqa	xmm15,XMMWORD[80+r10]
	movdqa	xmm14,XMMWORD[96+r10]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	_vpaes_preheat







section	.rdata rdata align=64
ALIGN	64
_vpaes_consts:
$L$k_inv:
	DQ	0x0E05060F0D080180,0x040703090A0B0C02
	DQ	0x01040A060F0B0780,0x030D0E0C02050809

$L$k_s0F:
	DQ	0x0F0F0F0F0F0F0F0F,0x0F0F0F0F0F0F0F0F

$L$k_ipt:
	DQ	0xC2B2E8985A2A7000,0xCABAE09052227808
	DQ	0x4C01307D317C4D00,0xCD80B1FCB0FDCC81

$L$k_sb1:
	DQ	0xB19BE18FCB503E00,0xA5DF7A6E142AF544
	DQ	0x3618D415FAE22300,0x3BF7CCC10D2ED9EF
$L$k_sb2:
	DQ	0xE27A93C60B712400,0x5EB7E955BC982FCD
	DQ	0x69EB88400AE12900,0xC2A163C8AB82234A
$L$k_sbo:
	DQ	0xD0D26D176FBDC700,0x15AABF7AC502A878
	DQ	0xCFE474A55FBB6A00,0x8E1E90D1412B35FA

$L$k_mc_forward:
	DQ	0x0407060500030201,0x0C0F0E0D080B0A09
	DQ	0x080B0A0904070605,0x000302010C0F0E0D
	DQ	0x0C0F0E0D080B0A09,0x0407060500030201
	DQ	0x000302010C0F0E0D,0x080B0A0904070605

$L$k_mc_backward:
	DQ	0x0605040702010003,0x0E0D0C0F0A09080B
	DQ	0x020100030E0D0C0F,0x0A09080B06050407
	DQ	0x0E0D0C0F0A09080B,0x0605040702010003
	DQ	0x0A09080B06050407,0x020100030E0D0C0F

$L$k_sr:
	DQ	0x0706050403020100,0x0F0E0D0C0B0A0908
	DQ	0x030E09040F0A0500,0x0B06010C07020D08
	DQ	0x0F060D040B020900,0x070E050C030A0108
	DQ	0x0B0E0104070A0D00,0x0306090C0F020508

$L$k_rcon:
	DQ	0x1F8391B9AF9DEEB6,0x702A98084D7C7D81

$L$k_s63:
	DQ	0x5B5B5B5B5B5B5B5B,0x5B5B5B5B5B5B5B5B

$L$k_opt:
	DQ	0xFF9F4929D6B66000,0xF7974121DEBE6808
	DQ	0x01EDBD5150BCEC00,0xE10D5DB1B05C0CE0

$L$k_deskew:
	DQ	0x07E4A34047A4E300,0x1DFEB95A5DBEF91A
	DQ	0x5F36B5DC83EA6900,0x2841C2ABF49D1E77





$L$k_dksd:
	DQ	0xFEB91A5DA3E44700,0x0740E3A45A1DBEF9
	DQ	0x41C277F4B5368300,0x5FDC69EAAB289D1E
$L$k_dksb:
	DQ	0x9A4FCA1F8550D500,0x03D653861CC94C99
	DQ	0x115BEDA7B6FC4A00,0xD993256F7E3482C8
$L$k_dkse:
	DQ	0xD5031CCA1FC9D600,0x53859A4C994F5086
	DQ	0xA23196054FDC7BE8,0xCD5EF96A20B31487
$L$k_dks9:
	DQ	0xB6116FC87ED9A700,0x4AED933482255BFC
	DQ	0x4576516227143300,0x8BB89FACE9DAFDCE





$L$k_dipt:
	DQ	0x0F505B040B545F00,0x154A411E114E451A
	DQ	0x86E383E660056500,0x12771772F491F194

$L$k_dsb9:
	DQ	0x851C03539A86D600,0xCAD51F504F994CC9
	DQ	0xC03B1789ECD74900,0x725E2C9EB2FBA565
$L$k_dsbd:
	DQ	0x7D57CCDFE6B1A200,0xF56E9B13882A4439
	DQ	0x3CE2FAF724C6CB00,0x2931180D15DEEFD3
$L$k_dsbb:
	DQ	0xD022649296B44200,0x602646F6B0F2D404
	DQ	0xC19498A6CD596700,0xF3FF0C3E3255AA6B
$L$k_dsbe:
	DQ	0x46F2929626D4D000,0x2242600464B4F6B0
	DQ	0x0C55A6CDFFAAC100,0x9467F36B98593E32
$L$k_dsbo:
	DQ	0x1387EA537EF94000,0xC7AA6DB9D4943E2D
	DQ	0x12D7560F93441D00,0xCA4B8159D8C58E9C
ALIGN	64

DB	86,101,99,116,111,114,32,80,101,114,109,117,116,97,116,105
DB	111,110,32,65,69,83,32,102,111,114,32,120,56,54,95,54
DB	52,47,83,83,83,69,51,44,32,77,105,107,101,32,72,97
DB	109,98,117,114,103,32,40,83,116,97,110,102,111,114,100,32
DB	85,110,105,118,101,114,115,105,116,121,41,0
