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
global	sha256_block_data_order

ALIGN	16
SEH64_PROC_FRAME sha256_block_data_order,PUBLIC
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



	lea	r11,[OPENSSL_ia32cap_P]
	mov	r9d,DWORD[r11]
	mov	r10d,DWORD[4+r11]
	mov	r11d,DWORD[8+r11]
	test	r11d,536870912
	jnz	NEAR _shaext_shortcut
	and	r11d,296
	cmp	r11d,296
	je	NEAR $L$avx2_shortcut
	and	r9d,1073741824
	and	r10d,268435968
	or	r10d,r9d
	cmp	r10d,1342177792
	je	NEAR $L$avx_shortcut
	test	r10d,512
	jnz	NEAR $L$ssse3_shortcut
	mov	rax,rsp

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
	shl	rdx,4
	sub	rsp,16*4+4*8
	lea	rdx,[rdx*4+rsi]
	and	rsp,-64
	mov	QWORD[((64+0))+rsp],rdi
	mov	QWORD[((64+8))+rsp],rsi
	mov	QWORD[((64+16))+rsp],rdx
	mov	QWORD[88+rsp],rax

; Hack using rax to load saved CFA expression: 88(%rsp),deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ALLOCSTACK 88	; Can only deref by .PUSHREG, so adjust RSP by feigning stack alloc.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56

	mov	eax,DWORD[rdi]
	mov	ebx,DWORD[4+rdi]
	mov	ecx,DWORD[8+rdi]
	mov	edx,DWORD[12+rdi]
	mov	r8d,DWORD[16+rdi]
	mov	r9d,DWORD[20+rdi]
	mov	r10d,DWORD[24+rdi]
	mov	r11d,DWORD[28+rdi]
	jmp	NEAR $L$loop

ALIGN	16
$L$loop:
	mov	edi,ebx
	lea	rbp,[K256]
	xor	edi,ecx
	mov	r12d,DWORD[rsi]
	mov	r13d,r8d
	mov	r14d,eax
	bswap	r12d
	ror	r13d,14
	mov	r15d,r9d

	xor	r13d,r8d
	ror	r14d,9
	xor	r15d,r10d

	mov	DWORD[rsp],r12d
	xor	r14d,eax
	and	r15d,r8d

	ror	r13d,5
	add	r12d,r11d
	xor	r15d,r10d

	ror	r14d,11
	xor	r13d,r8d
	add	r12d,r15d

	mov	r15d,eax
	add	r12d,DWORD[rbp]
	xor	r14d,eax

	xor	r15d,ebx
	ror	r13d,6
	mov	r11d,ebx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r11d,edi
	add	edx,r12d
	add	r11d,r12d

	lea	rbp,[4+rbp]
	add	r11d,r14d
	mov	r12d,DWORD[4+rsi]
	mov	r13d,edx
	mov	r14d,r11d
	bswap	r12d
	ror	r13d,14
	mov	edi,r8d

	xor	r13d,edx
	ror	r14d,9
	xor	edi,r9d

	mov	DWORD[4+rsp],r12d
	xor	r14d,r11d
	and	edi,edx

	ror	r13d,5
	add	r12d,r10d
	xor	edi,r9d

	ror	r14d,11
	xor	r13d,edx
	add	r12d,edi

	mov	edi,r11d
	add	r12d,DWORD[rbp]
	xor	r14d,r11d

	xor	edi,eax
	ror	r13d,6
	mov	r10d,eax

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r10d,r15d
	add	ecx,r12d
	add	r10d,r12d

	lea	rbp,[4+rbp]
	add	r10d,r14d
	mov	r12d,DWORD[8+rsi]
	mov	r13d,ecx
	mov	r14d,r10d
	bswap	r12d
	ror	r13d,14
	mov	r15d,edx

	xor	r13d,ecx
	ror	r14d,9
	xor	r15d,r8d

	mov	DWORD[8+rsp],r12d
	xor	r14d,r10d
	and	r15d,ecx

	ror	r13d,5
	add	r12d,r9d
	xor	r15d,r8d

	ror	r14d,11
	xor	r13d,ecx
	add	r12d,r15d

	mov	r15d,r10d
	add	r12d,DWORD[rbp]
	xor	r14d,r10d

	xor	r15d,r11d
	ror	r13d,6
	mov	r9d,r11d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r9d,edi
	add	ebx,r12d
	add	r9d,r12d

	lea	rbp,[4+rbp]
	add	r9d,r14d
	mov	r12d,DWORD[12+rsi]
	mov	r13d,ebx
	mov	r14d,r9d
	bswap	r12d
	ror	r13d,14
	mov	edi,ecx

	xor	r13d,ebx
	ror	r14d,9
	xor	edi,edx

	mov	DWORD[12+rsp],r12d
	xor	r14d,r9d
	and	edi,ebx

	ror	r13d,5
	add	r12d,r8d
	xor	edi,edx

	ror	r14d,11
	xor	r13d,ebx
	add	r12d,edi

	mov	edi,r9d
	add	r12d,DWORD[rbp]
	xor	r14d,r9d

	xor	edi,r10d
	ror	r13d,6
	mov	r8d,r10d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r8d,r15d
	add	eax,r12d
	add	r8d,r12d

	lea	rbp,[20+rbp]
	add	r8d,r14d
	mov	r12d,DWORD[16+rsi]
	mov	r13d,eax
	mov	r14d,r8d
	bswap	r12d
	ror	r13d,14
	mov	r15d,ebx

	xor	r13d,eax
	ror	r14d,9
	xor	r15d,ecx

	mov	DWORD[16+rsp],r12d
	xor	r14d,r8d
	and	r15d,eax

	ror	r13d,5
	add	r12d,edx
	xor	r15d,ecx

	ror	r14d,11
	xor	r13d,eax
	add	r12d,r15d

	mov	r15d,r8d
	add	r12d,DWORD[rbp]
	xor	r14d,r8d

	xor	r15d,r9d
	ror	r13d,6
	mov	edx,r9d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	edx,edi
	add	r11d,r12d
	add	edx,r12d

	lea	rbp,[4+rbp]
	add	edx,r14d
	mov	r12d,DWORD[20+rsi]
	mov	r13d,r11d
	mov	r14d,edx
	bswap	r12d
	ror	r13d,14
	mov	edi,eax

	xor	r13d,r11d
	ror	r14d,9
	xor	edi,ebx

	mov	DWORD[20+rsp],r12d
	xor	r14d,edx
	and	edi,r11d

	ror	r13d,5
	add	r12d,ecx
	xor	edi,ebx

	ror	r14d,11
	xor	r13d,r11d
	add	r12d,edi

	mov	edi,edx
	add	r12d,DWORD[rbp]
	xor	r14d,edx

	xor	edi,r8d
	ror	r13d,6
	mov	ecx,r8d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	ecx,r15d
	add	r10d,r12d
	add	ecx,r12d

	lea	rbp,[4+rbp]
	add	ecx,r14d
	mov	r12d,DWORD[24+rsi]
	mov	r13d,r10d
	mov	r14d,ecx
	bswap	r12d
	ror	r13d,14
	mov	r15d,r11d

	xor	r13d,r10d
	ror	r14d,9
	xor	r15d,eax

	mov	DWORD[24+rsp],r12d
	xor	r14d,ecx
	and	r15d,r10d

	ror	r13d,5
	add	r12d,ebx
	xor	r15d,eax

	ror	r14d,11
	xor	r13d,r10d
	add	r12d,r15d

	mov	r15d,ecx
	add	r12d,DWORD[rbp]
	xor	r14d,ecx

	xor	r15d,edx
	ror	r13d,6
	mov	ebx,edx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	ebx,edi
	add	r9d,r12d
	add	ebx,r12d

	lea	rbp,[4+rbp]
	add	ebx,r14d
	mov	r12d,DWORD[28+rsi]
	mov	r13d,r9d
	mov	r14d,ebx
	bswap	r12d
	ror	r13d,14
	mov	edi,r10d

	xor	r13d,r9d
	ror	r14d,9
	xor	edi,r11d

	mov	DWORD[28+rsp],r12d
	xor	r14d,ebx
	and	edi,r9d

	ror	r13d,5
	add	r12d,eax
	xor	edi,r11d

	ror	r14d,11
	xor	r13d,r9d
	add	r12d,edi

	mov	edi,ebx
	add	r12d,DWORD[rbp]
	xor	r14d,ebx

	xor	edi,ecx
	ror	r13d,6
	mov	eax,ecx

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	eax,r15d
	add	r8d,r12d
	add	eax,r12d

	lea	rbp,[20+rbp]
	add	eax,r14d
	mov	r12d,DWORD[32+rsi]
	mov	r13d,r8d
	mov	r14d,eax
	bswap	r12d
	ror	r13d,14
	mov	r15d,r9d

	xor	r13d,r8d
	ror	r14d,9
	xor	r15d,r10d

	mov	DWORD[32+rsp],r12d
	xor	r14d,eax
	and	r15d,r8d

	ror	r13d,5
	add	r12d,r11d
	xor	r15d,r10d

	ror	r14d,11
	xor	r13d,r8d
	add	r12d,r15d

	mov	r15d,eax
	add	r12d,DWORD[rbp]
	xor	r14d,eax

	xor	r15d,ebx
	ror	r13d,6
	mov	r11d,ebx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r11d,edi
	add	edx,r12d
	add	r11d,r12d

	lea	rbp,[4+rbp]
	add	r11d,r14d
	mov	r12d,DWORD[36+rsi]
	mov	r13d,edx
	mov	r14d,r11d
	bswap	r12d
	ror	r13d,14
	mov	edi,r8d

	xor	r13d,edx
	ror	r14d,9
	xor	edi,r9d

	mov	DWORD[36+rsp],r12d
	xor	r14d,r11d
	and	edi,edx

	ror	r13d,5
	add	r12d,r10d
	xor	edi,r9d

	ror	r14d,11
	xor	r13d,edx
	add	r12d,edi

	mov	edi,r11d
	add	r12d,DWORD[rbp]
	xor	r14d,r11d

	xor	edi,eax
	ror	r13d,6
	mov	r10d,eax

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r10d,r15d
	add	ecx,r12d
	add	r10d,r12d

	lea	rbp,[4+rbp]
	add	r10d,r14d
	mov	r12d,DWORD[40+rsi]
	mov	r13d,ecx
	mov	r14d,r10d
	bswap	r12d
	ror	r13d,14
	mov	r15d,edx

	xor	r13d,ecx
	ror	r14d,9
	xor	r15d,r8d

	mov	DWORD[40+rsp],r12d
	xor	r14d,r10d
	and	r15d,ecx

	ror	r13d,5
	add	r12d,r9d
	xor	r15d,r8d

	ror	r14d,11
	xor	r13d,ecx
	add	r12d,r15d

	mov	r15d,r10d
	add	r12d,DWORD[rbp]
	xor	r14d,r10d

	xor	r15d,r11d
	ror	r13d,6
	mov	r9d,r11d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r9d,edi
	add	ebx,r12d
	add	r9d,r12d

	lea	rbp,[4+rbp]
	add	r9d,r14d
	mov	r12d,DWORD[44+rsi]
	mov	r13d,ebx
	mov	r14d,r9d
	bswap	r12d
	ror	r13d,14
	mov	edi,ecx

	xor	r13d,ebx
	ror	r14d,9
	xor	edi,edx

	mov	DWORD[44+rsp],r12d
	xor	r14d,r9d
	and	edi,ebx

	ror	r13d,5
	add	r12d,r8d
	xor	edi,edx

	ror	r14d,11
	xor	r13d,ebx
	add	r12d,edi

	mov	edi,r9d
	add	r12d,DWORD[rbp]
	xor	r14d,r9d

	xor	edi,r10d
	ror	r13d,6
	mov	r8d,r10d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r8d,r15d
	add	eax,r12d
	add	r8d,r12d

	lea	rbp,[20+rbp]
	add	r8d,r14d
	mov	r12d,DWORD[48+rsi]
	mov	r13d,eax
	mov	r14d,r8d
	bswap	r12d
	ror	r13d,14
	mov	r15d,ebx

	xor	r13d,eax
	ror	r14d,9
	xor	r15d,ecx

	mov	DWORD[48+rsp],r12d
	xor	r14d,r8d
	and	r15d,eax

	ror	r13d,5
	add	r12d,edx
	xor	r15d,ecx

	ror	r14d,11
	xor	r13d,eax
	add	r12d,r15d

	mov	r15d,r8d
	add	r12d,DWORD[rbp]
	xor	r14d,r8d

	xor	r15d,r9d
	ror	r13d,6
	mov	edx,r9d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	edx,edi
	add	r11d,r12d
	add	edx,r12d

	lea	rbp,[4+rbp]
	add	edx,r14d
	mov	r12d,DWORD[52+rsi]
	mov	r13d,r11d
	mov	r14d,edx
	bswap	r12d
	ror	r13d,14
	mov	edi,eax

	xor	r13d,r11d
	ror	r14d,9
	xor	edi,ebx

	mov	DWORD[52+rsp],r12d
	xor	r14d,edx
	and	edi,r11d

	ror	r13d,5
	add	r12d,ecx
	xor	edi,ebx

	ror	r14d,11
	xor	r13d,r11d
	add	r12d,edi

	mov	edi,edx
	add	r12d,DWORD[rbp]
	xor	r14d,edx

	xor	edi,r8d
	ror	r13d,6
	mov	ecx,r8d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	ecx,r15d
	add	r10d,r12d
	add	ecx,r12d

	lea	rbp,[4+rbp]
	add	ecx,r14d
	mov	r12d,DWORD[56+rsi]
	mov	r13d,r10d
	mov	r14d,ecx
	bswap	r12d
	ror	r13d,14
	mov	r15d,r11d

	xor	r13d,r10d
	ror	r14d,9
	xor	r15d,eax

	mov	DWORD[56+rsp],r12d
	xor	r14d,ecx
	and	r15d,r10d

	ror	r13d,5
	add	r12d,ebx
	xor	r15d,eax

	ror	r14d,11
	xor	r13d,r10d
	add	r12d,r15d

	mov	r15d,ecx
	add	r12d,DWORD[rbp]
	xor	r14d,ecx

	xor	r15d,edx
	ror	r13d,6
	mov	ebx,edx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	ebx,edi
	add	r9d,r12d
	add	ebx,r12d

	lea	rbp,[4+rbp]
	add	ebx,r14d
	mov	r12d,DWORD[60+rsi]
	mov	r13d,r9d
	mov	r14d,ebx
	bswap	r12d
	ror	r13d,14
	mov	edi,r10d

	xor	r13d,r9d
	ror	r14d,9
	xor	edi,r11d

	mov	DWORD[60+rsp],r12d
	xor	r14d,ebx
	and	edi,r9d

	ror	r13d,5
	add	r12d,eax
	xor	edi,r11d

	ror	r14d,11
	xor	r13d,r9d
	add	r12d,edi

	mov	edi,ebx
	add	r12d,DWORD[rbp]
	xor	r14d,ebx

	xor	edi,ecx
	ror	r13d,6
	mov	eax,ecx

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	eax,r15d
	add	r8d,r12d
	add	eax,r12d

	lea	rbp,[20+rbp]
	jmp	NEAR $L$rounds_16_xx
ALIGN	16
$L$rounds_16_xx:
	mov	r13d,DWORD[4+rsp]
	mov	r15d,DWORD[56+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	eax,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[36+rsp]

	add	r12d,DWORD[rsp]
	mov	r13d,r8d
	add	r12d,r15d
	mov	r14d,eax
	ror	r13d,14
	mov	r15d,r9d

	xor	r13d,r8d
	ror	r14d,9
	xor	r15d,r10d

	mov	DWORD[rsp],r12d
	xor	r14d,eax
	and	r15d,r8d

	ror	r13d,5
	add	r12d,r11d
	xor	r15d,r10d

	ror	r14d,11
	xor	r13d,r8d
	add	r12d,r15d

	mov	r15d,eax
	add	r12d,DWORD[rbp]
	xor	r14d,eax

	xor	r15d,ebx
	ror	r13d,6
	mov	r11d,ebx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r11d,edi
	add	edx,r12d
	add	r11d,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[8+rsp]
	mov	edi,DWORD[60+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r11d,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[40+rsp]

	add	r12d,DWORD[4+rsp]
	mov	r13d,edx
	add	r12d,edi
	mov	r14d,r11d
	ror	r13d,14
	mov	edi,r8d

	xor	r13d,edx
	ror	r14d,9
	xor	edi,r9d

	mov	DWORD[4+rsp],r12d
	xor	r14d,r11d
	and	edi,edx

	ror	r13d,5
	add	r12d,r10d
	xor	edi,r9d

	ror	r14d,11
	xor	r13d,edx
	add	r12d,edi

	mov	edi,r11d
	add	r12d,DWORD[rbp]
	xor	r14d,r11d

	xor	edi,eax
	ror	r13d,6
	mov	r10d,eax

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r10d,r15d
	add	ecx,r12d
	add	r10d,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[12+rsp]
	mov	r15d,DWORD[rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r10d,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[44+rsp]

	add	r12d,DWORD[8+rsp]
	mov	r13d,ecx
	add	r12d,r15d
	mov	r14d,r10d
	ror	r13d,14
	mov	r15d,edx

	xor	r13d,ecx
	ror	r14d,9
	xor	r15d,r8d

	mov	DWORD[8+rsp],r12d
	xor	r14d,r10d
	and	r15d,ecx

	ror	r13d,5
	add	r12d,r9d
	xor	r15d,r8d

	ror	r14d,11
	xor	r13d,ecx
	add	r12d,r15d

	mov	r15d,r10d
	add	r12d,DWORD[rbp]
	xor	r14d,r10d

	xor	r15d,r11d
	ror	r13d,6
	mov	r9d,r11d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r9d,edi
	add	ebx,r12d
	add	r9d,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[16+rsp]
	mov	edi,DWORD[4+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r9d,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[48+rsp]

	add	r12d,DWORD[12+rsp]
	mov	r13d,ebx
	add	r12d,edi
	mov	r14d,r9d
	ror	r13d,14
	mov	edi,ecx

	xor	r13d,ebx
	ror	r14d,9
	xor	edi,edx

	mov	DWORD[12+rsp],r12d
	xor	r14d,r9d
	and	edi,ebx

	ror	r13d,5
	add	r12d,r8d
	xor	edi,edx

	ror	r14d,11
	xor	r13d,ebx
	add	r12d,edi

	mov	edi,r9d
	add	r12d,DWORD[rbp]
	xor	r14d,r9d

	xor	edi,r10d
	ror	r13d,6
	mov	r8d,r10d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r8d,r15d
	add	eax,r12d
	add	r8d,r12d

	lea	rbp,[20+rbp]
	mov	r13d,DWORD[20+rsp]
	mov	r15d,DWORD[8+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r8d,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[52+rsp]

	add	r12d,DWORD[16+rsp]
	mov	r13d,eax
	add	r12d,r15d
	mov	r14d,r8d
	ror	r13d,14
	mov	r15d,ebx

	xor	r13d,eax
	ror	r14d,9
	xor	r15d,ecx

	mov	DWORD[16+rsp],r12d
	xor	r14d,r8d
	and	r15d,eax

	ror	r13d,5
	add	r12d,edx
	xor	r15d,ecx

	ror	r14d,11
	xor	r13d,eax
	add	r12d,r15d

	mov	r15d,r8d
	add	r12d,DWORD[rbp]
	xor	r14d,r8d

	xor	r15d,r9d
	ror	r13d,6
	mov	edx,r9d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	edx,edi
	add	r11d,r12d
	add	edx,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[24+rsp]
	mov	edi,DWORD[12+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	edx,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[56+rsp]

	add	r12d,DWORD[20+rsp]
	mov	r13d,r11d
	add	r12d,edi
	mov	r14d,edx
	ror	r13d,14
	mov	edi,eax

	xor	r13d,r11d
	ror	r14d,9
	xor	edi,ebx

	mov	DWORD[20+rsp],r12d
	xor	r14d,edx
	and	edi,r11d

	ror	r13d,5
	add	r12d,ecx
	xor	edi,ebx

	ror	r14d,11
	xor	r13d,r11d
	add	r12d,edi

	mov	edi,edx
	add	r12d,DWORD[rbp]
	xor	r14d,edx

	xor	edi,r8d
	ror	r13d,6
	mov	ecx,r8d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	ecx,r15d
	add	r10d,r12d
	add	ecx,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[28+rsp]
	mov	r15d,DWORD[16+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	ecx,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[60+rsp]

	add	r12d,DWORD[24+rsp]
	mov	r13d,r10d
	add	r12d,r15d
	mov	r14d,ecx
	ror	r13d,14
	mov	r15d,r11d

	xor	r13d,r10d
	ror	r14d,9
	xor	r15d,eax

	mov	DWORD[24+rsp],r12d
	xor	r14d,ecx
	and	r15d,r10d

	ror	r13d,5
	add	r12d,ebx
	xor	r15d,eax

	ror	r14d,11
	xor	r13d,r10d
	add	r12d,r15d

	mov	r15d,ecx
	add	r12d,DWORD[rbp]
	xor	r14d,ecx

	xor	r15d,edx
	ror	r13d,6
	mov	ebx,edx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	ebx,edi
	add	r9d,r12d
	add	ebx,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[32+rsp]
	mov	edi,DWORD[20+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	ebx,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[rsp]

	add	r12d,DWORD[28+rsp]
	mov	r13d,r9d
	add	r12d,edi
	mov	r14d,ebx
	ror	r13d,14
	mov	edi,r10d

	xor	r13d,r9d
	ror	r14d,9
	xor	edi,r11d

	mov	DWORD[28+rsp],r12d
	xor	r14d,ebx
	and	edi,r9d

	ror	r13d,5
	add	r12d,eax
	xor	edi,r11d

	ror	r14d,11
	xor	r13d,r9d
	add	r12d,edi

	mov	edi,ebx
	add	r12d,DWORD[rbp]
	xor	r14d,ebx

	xor	edi,ecx
	ror	r13d,6
	mov	eax,ecx

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	eax,r15d
	add	r8d,r12d
	add	eax,r12d

	lea	rbp,[20+rbp]
	mov	r13d,DWORD[36+rsp]
	mov	r15d,DWORD[24+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	eax,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[4+rsp]

	add	r12d,DWORD[32+rsp]
	mov	r13d,r8d
	add	r12d,r15d
	mov	r14d,eax
	ror	r13d,14
	mov	r15d,r9d

	xor	r13d,r8d
	ror	r14d,9
	xor	r15d,r10d

	mov	DWORD[32+rsp],r12d
	xor	r14d,eax
	and	r15d,r8d

	ror	r13d,5
	add	r12d,r11d
	xor	r15d,r10d

	ror	r14d,11
	xor	r13d,r8d
	add	r12d,r15d

	mov	r15d,eax
	add	r12d,DWORD[rbp]
	xor	r14d,eax

	xor	r15d,ebx
	ror	r13d,6
	mov	r11d,ebx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r11d,edi
	add	edx,r12d
	add	r11d,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[40+rsp]
	mov	edi,DWORD[28+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r11d,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[8+rsp]

	add	r12d,DWORD[36+rsp]
	mov	r13d,edx
	add	r12d,edi
	mov	r14d,r11d
	ror	r13d,14
	mov	edi,r8d

	xor	r13d,edx
	ror	r14d,9
	xor	edi,r9d

	mov	DWORD[36+rsp],r12d
	xor	r14d,r11d
	and	edi,edx

	ror	r13d,5
	add	r12d,r10d
	xor	edi,r9d

	ror	r14d,11
	xor	r13d,edx
	add	r12d,edi

	mov	edi,r11d
	add	r12d,DWORD[rbp]
	xor	r14d,r11d

	xor	edi,eax
	ror	r13d,6
	mov	r10d,eax

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r10d,r15d
	add	ecx,r12d
	add	r10d,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[44+rsp]
	mov	r15d,DWORD[32+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r10d,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[12+rsp]

	add	r12d,DWORD[40+rsp]
	mov	r13d,ecx
	add	r12d,r15d
	mov	r14d,r10d
	ror	r13d,14
	mov	r15d,edx

	xor	r13d,ecx
	ror	r14d,9
	xor	r15d,r8d

	mov	DWORD[40+rsp],r12d
	xor	r14d,r10d
	and	r15d,ecx

	ror	r13d,5
	add	r12d,r9d
	xor	r15d,r8d

	ror	r14d,11
	xor	r13d,ecx
	add	r12d,r15d

	mov	r15d,r10d
	add	r12d,DWORD[rbp]
	xor	r14d,r10d

	xor	r15d,r11d
	ror	r13d,6
	mov	r9d,r11d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	r9d,edi
	add	ebx,r12d
	add	r9d,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[48+rsp]
	mov	edi,DWORD[36+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r9d,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[16+rsp]

	add	r12d,DWORD[44+rsp]
	mov	r13d,ebx
	add	r12d,edi
	mov	r14d,r9d
	ror	r13d,14
	mov	edi,ecx

	xor	r13d,ebx
	ror	r14d,9
	xor	edi,edx

	mov	DWORD[44+rsp],r12d
	xor	r14d,r9d
	and	edi,ebx

	ror	r13d,5
	add	r12d,r8d
	xor	edi,edx

	ror	r14d,11
	xor	r13d,ebx
	add	r12d,edi

	mov	edi,r9d
	add	r12d,DWORD[rbp]
	xor	r14d,r9d

	xor	edi,r10d
	ror	r13d,6
	mov	r8d,r10d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	r8d,r15d
	add	eax,r12d
	add	r8d,r12d

	lea	rbp,[20+rbp]
	mov	r13d,DWORD[52+rsp]
	mov	r15d,DWORD[40+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	r8d,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[20+rsp]

	add	r12d,DWORD[48+rsp]
	mov	r13d,eax
	add	r12d,r15d
	mov	r14d,r8d
	ror	r13d,14
	mov	r15d,ebx

	xor	r13d,eax
	ror	r14d,9
	xor	r15d,ecx

	mov	DWORD[48+rsp],r12d
	xor	r14d,r8d
	and	r15d,eax

	ror	r13d,5
	add	r12d,edx
	xor	r15d,ecx

	ror	r14d,11
	xor	r13d,eax
	add	r12d,r15d

	mov	r15d,r8d
	add	r12d,DWORD[rbp]
	xor	r14d,r8d

	xor	r15d,r9d
	ror	r13d,6
	mov	edx,r9d

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	edx,edi
	add	r11d,r12d
	add	edx,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[56+rsp]
	mov	edi,DWORD[44+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	edx,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[24+rsp]

	add	r12d,DWORD[52+rsp]
	mov	r13d,r11d
	add	r12d,edi
	mov	r14d,edx
	ror	r13d,14
	mov	edi,eax

	xor	r13d,r11d
	ror	r14d,9
	xor	edi,ebx

	mov	DWORD[52+rsp],r12d
	xor	r14d,edx
	and	edi,r11d

	ror	r13d,5
	add	r12d,ecx
	xor	edi,ebx

	ror	r14d,11
	xor	r13d,r11d
	add	r12d,edi

	mov	edi,edx
	add	r12d,DWORD[rbp]
	xor	r14d,edx

	xor	edi,r8d
	ror	r13d,6
	mov	ecx,r8d

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	ecx,r15d
	add	r10d,r12d
	add	ecx,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[60+rsp]
	mov	r15d,DWORD[48+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	ecx,r14d
	mov	r14d,r15d
	ror	r15d,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	r15d,r14d
	shr	r14d,10

	ror	r15d,17
	xor	r12d,r13d
	xor	r15d,r14d
	add	r12d,DWORD[28+rsp]

	add	r12d,DWORD[56+rsp]
	mov	r13d,r10d
	add	r12d,r15d
	mov	r14d,ecx
	ror	r13d,14
	mov	r15d,r11d

	xor	r13d,r10d
	ror	r14d,9
	xor	r15d,eax

	mov	DWORD[56+rsp],r12d
	xor	r14d,ecx
	and	r15d,r10d

	ror	r13d,5
	add	r12d,ebx
	xor	r15d,eax

	ror	r14d,11
	xor	r13d,r10d
	add	r12d,r15d

	mov	r15d,ecx
	add	r12d,DWORD[rbp]
	xor	r14d,ecx

	xor	r15d,edx
	ror	r13d,6
	mov	ebx,edx

	and	edi,r15d
	ror	r14d,2
	add	r12d,r13d

	xor	ebx,edi
	add	r9d,r12d
	add	ebx,r12d

	lea	rbp,[4+rbp]
	mov	r13d,DWORD[rsp]
	mov	edi,DWORD[52+rsp]

	mov	r12d,r13d
	ror	r13d,11
	add	ebx,r14d
	mov	r14d,edi
	ror	edi,2

	xor	r13d,r12d
	shr	r12d,3
	ror	r13d,7
	xor	edi,r14d
	shr	r14d,10

	ror	edi,17
	xor	r12d,r13d
	xor	edi,r14d
	add	r12d,DWORD[32+rsp]

	add	r12d,DWORD[60+rsp]
	mov	r13d,r9d
	add	r12d,edi
	mov	r14d,ebx
	ror	r13d,14
	mov	edi,r10d

	xor	r13d,r9d
	ror	r14d,9
	xor	edi,r11d

	mov	DWORD[60+rsp],r12d
	xor	r14d,ebx
	and	edi,r9d

	ror	r13d,5
	add	r12d,eax
	xor	edi,r11d

	ror	r14d,11
	xor	r13d,r9d
	add	r12d,edi

	mov	edi,ebx
	add	r12d,DWORD[rbp]
	xor	r14d,ebx

	xor	edi,ecx
	ror	r13d,6
	mov	eax,ecx

	and	r15d,edi
	ror	r14d,2
	add	r12d,r13d

	xor	eax,r15d
	add	r8d,r12d
	add	eax,r12d

	lea	rbp,[20+rbp]
	cmp	BYTE[3+rbp],0
	jnz	NEAR $L$rounds_16_xx

	mov	rdi,QWORD[((64+0))+rsp]
	add	eax,r14d
	lea	rsi,[64+rsi]

	add	eax,DWORD[rdi]
	add	ebx,DWORD[4+rdi]
	add	ecx,DWORD[8+rdi]
	add	edx,DWORD[12+rdi]
	add	r8d,DWORD[16+rdi]
	add	r9d,DWORD[20+rdi]
	add	r10d,DWORD[24+rdi]
	add	r11d,DWORD[28+rdi]

	cmp	rsi,QWORD[((64+16))+rsp]

	mov	DWORD[rdi],eax
	mov	DWORD[4+rdi],ebx
	mov	DWORD[8+rdi],ecx
	mov	DWORD[12+rdi],edx
	mov	DWORD[16+rdi],r8d
	mov	DWORD[20+rdi],r9d
	mov	DWORD[24+rdi],r10d
	mov	DWORD[28+rdi],r11d
	jb	NEAR $L$loop

	mov	rsi,QWORD[88+rsp]

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
SEH64_ENDPROC_FRAME	sha256_block_data_order
$L$SEH_end_sha256_block_data_order:
section	.rdata rdata align=64
ALIGN	64

K256:
	DD	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	DD	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	DD	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	DD	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	DD	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	DD	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	DD	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	DD	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	DD	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	DD	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	DD	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	DD	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	DD	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	DD	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	DD	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	DD	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	DD	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	DD	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	DD	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	DD	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	DD	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	DD	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	DD	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	DD	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	DD	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	DD	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	DD	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	DD	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	DD	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	DD	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	DD	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
	DD	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2

	DD	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
	DD	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
	DD	0x03020100,0x0b0a0908,0xffffffff,0xffffffff
	DD	0x03020100,0x0b0a0908,0xffffffff,0xffffffff
	DD	0xffffffff,0xffffffff,0x03020100,0x0b0a0908
	DD	0xffffffff,0xffffffff,0x03020100,0x0b0a0908
DB	83,72,65,50,53,54,32,98,108,111,99,107,32,116,114,97
DB	110,115,102,111,114,109,32,102,111,114,32,120,56,54,95,54
DB	52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121
DB	32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46
DB	111,114,103,62,0
section .text

ALIGN	64
SEH64_PROC_FRAME sha256_block_data_order_shaext,PRIVATE
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8


_shaext_shortcut:

	lea	rsp,[((-88))+rsp]
SEH64_ALLOCSTACK 88
	movaps	XMMWORD[(-8-80)+rax],xmm6
SEH64_SAVEXMM128 xmm6, 0	; .cfi_offset xmm6, -8-5*16-8 cfa_reg_offset=96 cfa_rsp=-96
	movaps	XMMWORD[(-8-64)+rax],xmm7
SEH64_SAVEXMM128 xmm7, 16	; .cfi_offset xmm7, -8-4*16-8 cfa_reg_offset=96 cfa_rsp=-96
	movaps	XMMWORD[(-8-48)+rax],xmm8
SEH64_SAVEXMM128 xmm8, 32	; .cfi_offset xmm8, -8-3*16-8 cfa_reg_offset=96 cfa_rsp=-96
	movaps	XMMWORD[(-8-32)+rax],xmm9
SEH64_SAVEXMM128 xmm9, 48	; .cfi_offset xmm9, -8-2*16-8 cfa_reg_offset=96 cfa_rsp=-96
	movaps	XMMWORD[(-8-16)+rax],xmm10
SEH64_SAVEXMM128 xmm10, 64	; .cfi_offset xmm10, -8-1*16-8 cfa_reg_offset=96 cfa_rsp=-96
	; cfa_rsp=-96
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=96 cfa_rsp=-96
	lea	rcx,[((K256+128))]
	movdqu	xmm1,XMMWORD[rdi]
	movdqu	xmm2,XMMWORD[16+rdi]
	movdqa	xmm7,XMMWORD[((512-128))+rcx]

	pshufd	xmm0,xmm1,0x1b
	pshufd	xmm1,xmm1,0xb1
	pshufd	xmm2,xmm2,0x1b
	movdqa	xmm8,xmm7
DB	102,15,58,15,202,8
	punpcklqdq	xmm2,xmm0
	jmp	NEAR $L$oop_shaext

ALIGN	16
$L$oop_shaext:
	movdqu	xmm3,XMMWORD[rsi]
	movdqu	xmm4,XMMWORD[16+rsi]
	movdqu	xmm5,XMMWORD[32+rsi]
DB	102,15,56,0,223
	movdqu	xmm6,XMMWORD[48+rsi]

	movdqa	xmm0,XMMWORD[((0-128))+rcx]
	paddd	xmm0,xmm3
DB	102,15,56,0,231
	movdqa	xmm10,xmm2
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	nop
	movdqa	xmm9,xmm1
DB	15,56,203,202

	movdqa	xmm0,XMMWORD[((32-128))+rcx]
	paddd	xmm0,xmm4
DB	102,15,56,0,239
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	lea	rsi,[64+rsi]
DB	15,56,204,220
DB	15,56,203,202

	movdqa	xmm0,XMMWORD[((64-128))+rcx]
	paddd	xmm0,xmm5
DB	102,15,56,0,247
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm6
DB	102,15,58,15,253,4
	nop
	paddd	xmm3,xmm7
DB	15,56,204,229
DB	15,56,203,202

	movdqa	xmm0,XMMWORD[((96-128))+rcx]
	paddd	xmm0,xmm6
DB	15,56,205,222
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm3
DB	102,15,58,15,254,4
	nop
	paddd	xmm4,xmm7
DB	15,56,204,238
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((128-128))+rcx]
	paddd	xmm0,xmm3
DB	15,56,205,227
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm4
DB	102,15,58,15,251,4
	nop
	paddd	xmm5,xmm7
DB	15,56,204,243
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((160-128))+rcx]
	paddd	xmm0,xmm4
DB	15,56,205,236
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm5
DB	102,15,58,15,252,4
	nop
	paddd	xmm6,xmm7
DB	15,56,204,220
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((192-128))+rcx]
	paddd	xmm0,xmm5
DB	15,56,205,245
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm6
DB	102,15,58,15,253,4
	nop
	paddd	xmm3,xmm7
DB	15,56,204,229
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((224-128))+rcx]
	paddd	xmm0,xmm6
DB	15,56,205,222
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm3
DB	102,15,58,15,254,4
	nop
	paddd	xmm4,xmm7
DB	15,56,204,238
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((256-128))+rcx]
	paddd	xmm0,xmm3
DB	15,56,205,227
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm4
DB	102,15,58,15,251,4
	nop
	paddd	xmm5,xmm7
DB	15,56,204,243
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((288-128))+rcx]
	paddd	xmm0,xmm4
DB	15,56,205,236
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm5
DB	102,15,58,15,252,4
	nop
	paddd	xmm6,xmm7
DB	15,56,204,220
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((320-128))+rcx]
	paddd	xmm0,xmm5
DB	15,56,205,245
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm6
DB	102,15,58,15,253,4
	nop
	paddd	xmm3,xmm7
DB	15,56,204,229
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((352-128))+rcx]
	paddd	xmm0,xmm6
DB	15,56,205,222
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm3
DB	102,15,58,15,254,4
	nop
	paddd	xmm4,xmm7
DB	15,56,204,238
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((384-128))+rcx]
	paddd	xmm0,xmm3
DB	15,56,205,227
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm4
DB	102,15,58,15,251,4
	nop
	paddd	xmm5,xmm7
DB	15,56,204,243
DB	15,56,203,202
	movdqa	xmm0,XMMWORD[((416-128))+rcx]
	paddd	xmm0,xmm4
DB	15,56,205,236
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	movdqa	xmm7,xmm5
DB	102,15,58,15,252,4
DB	15,56,203,202
	paddd	xmm6,xmm7

	movdqa	xmm0,XMMWORD[((448-128))+rcx]
	paddd	xmm0,xmm5
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
DB	15,56,205,245
	movdqa	xmm7,xmm8
DB	15,56,203,202

	movdqa	xmm0,XMMWORD[((480-128))+rcx]
	paddd	xmm0,xmm6
	nop
DB	15,56,203,209
	pshufd	xmm0,xmm0,0x0e
	dec	rdx
	nop
DB	15,56,203,202

	paddd	xmm2,xmm10
	paddd	xmm1,xmm9
	jnz	NEAR $L$oop_shaext

	pshufd	xmm2,xmm2,0xb1
	pshufd	xmm7,xmm1,0x1b
	pshufd	xmm1,xmm1,0xb1
	punpckhqdq	xmm1,xmm2
DB	102,15,58,15,215,8

	movdqu	XMMWORD[rdi],xmm1
	movdqu	XMMWORD[16+rdi],xmm2
	movaps	xmm6,XMMWORD[((-8-80))+rax]
	movaps	xmm7,XMMWORD[((-8-64))+rax]
	movaps	xmm8,XMMWORD[((-8-48))+rax]
	movaps	xmm9,XMMWORD[((-8-32))+rax]
	movaps	xmm10,XMMWORD[((-8-16))+rax]
	mov	rsp,rax
	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	sha256_block_data_order_shaext
$L$SEH_end_sha256_block_data_order_shaext:

ALIGN	64
SEH64_PROC_FRAME sha256_block_data_order_ssse3,PRIVATE
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



$L$ssse3_shortcut:
	mov	rax,rsp

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
	shl	rdx,4
	sub	rsp,160
	lea	rdx,[rdx*4+rsi]
	and	rsp,-64
	mov	QWORD[((64+0))+rsp],rdi
	mov	QWORD[((64+8))+rsp],rsi
	mov	QWORD[((64+16))+rsp],rdx
	mov	QWORD[88+rsp],rax

	movaps	XMMWORD[(64+32)+rsp],xmm6
	movaps	XMMWORD[(64+48)+rsp],xmm7
	movaps	XMMWORD[(64+64)+rsp],xmm8
	movaps	XMMWORD[(64+80)+rsp],xmm9
; Hack using rax to load saved CFA expression: 88(%rsp),deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ALLOCSTACK 88	; Can only deref by .PUSHREG, so adjust RSP by feigning stack alloc.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56

	mov	eax,DWORD[rdi]
	mov	ebx,DWORD[4+rdi]
	mov	ecx,DWORD[8+rdi]
	mov	edx,DWORD[12+rdi]
	mov	r8d,DWORD[16+rdi]
	mov	r9d,DWORD[20+rdi]
	mov	r10d,DWORD[24+rdi]
	mov	r11d,DWORD[28+rdi]


	jmp	NEAR $L$loop_ssse3
ALIGN	16
$L$loop_ssse3:
	movdqa	xmm7,XMMWORD[((K256+512))]
	movdqu	xmm0,XMMWORD[rsi]
	movdqu	xmm1,XMMWORD[16+rsi]
	movdqu	xmm2,XMMWORD[32+rsi]
DB	102,15,56,0,199
	movdqu	xmm3,XMMWORD[48+rsi]
	lea	rbp,[K256]
DB	102,15,56,0,207
	movdqa	xmm4,XMMWORD[rbp]
	movdqa	xmm5,XMMWORD[32+rbp]
DB	102,15,56,0,215
	paddd	xmm4,xmm0
	movdqa	xmm6,XMMWORD[64+rbp]
DB	102,15,56,0,223
	movdqa	xmm7,XMMWORD[96+rbp]
	paddd	xmm5,xmm1
	paddd	xmm6,xmm2
	paddd	xmm7,xmm3
	movdqa	XMMWORD[rsp],xmm4
	mov	r14d,eax
	movdqa	XMMWORD[16+rsp],xmm5
	mov	edi,ebx
	movdqa	XMMWORD[32+rsp],xmm6
	xor	edi,ecx
	movdqa	XMMWORD[48+rsp],xmm7
	mov	r13d,r8d
	jmp	NEAR $L$ssse3_00_47

ALIGN	16
$L$ssse3_00_47:
	sub	rbp,-128
	ror	r13d,14
	movdqa	xmm4,xmm1
	mov	eax,r14d
	mov	r12d,r9d
	movdqa	xmm7,xmm3
	ror	r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	ror	r13d,5
	xor	r14d,eax
DB	102,15,58,15,224,4
	and	r12d,r8d
	xor	r13d,r8d
DB	102,15,58,15,250,4
	add	r11d,DWORD[rsp]
	mov	r15d,eax
	xor	r12d,r10d
	ror	r14d,11
	movdqa	xmm5,xmm4
	xor	r15d,ebx
	add	r11d,r12d
	movdqa	xmm6,xmm4
	ror	r13d,6
	and	edi,r15d
	psrld	xmm4,3
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	paddd	xmm0,xmm7
	ror	r14d,2
	add	edx,r11d
	psrld	xmm6,7
	add	r11d,edi
	mov	r13d,edx
	pshufd	xmm7,xmm3,250
	add	r14d,r11d
	ror	r13d,14
	pslld	xmm5,14
	mov	r11d,r14d
	mov	r12d,r8d
	pxor	xmm4,xmm6
	ror	r14d,9
	xor	r13d,edx
	xor	r12d,r9d
	ror	r13d,5
	psrld	xmm6,11
	xor	r14d,r11d
	pxor	xmm4,xmm5
	and	r12d,edx
	xor	r13d,edx
	pslld	xmm5,11
	add	r10d,DWORD[4+rsp]
	mov	edi,r11d
	pxor	xmm4,xmm6
	xor	r12d,r9d
	ror	r14d,11
	movdqa	xmm6,xmm7
	xor	edi,eax
	add	r10d,r12d
	pxor	xmm4,xmm5
	ror	r13d,6
	and	r15d,edi
	xor	r14d,r11d
	psrld	xmm7,10
	add	r10d,r13d
	xor	r15d,eax
	paddd	xmm0,xmm4
	ror	r14d,2
	add	ecx,r10d
	psrlq	xmm6,17
	add	r10d,r15d
	mov	r13d,ecx
	add	r14d,r10d
	pxor	xmm7,xmm6
	ror	r13d,14
	mov	r10d,r14d
	mov	r12d,edx
	ror	r14d,9
	psrlq	xmm6,2
	xor	r13d,ecx
	xor	r12d,r8d
	pxor	xmm7,xmm6
	ror	r13d,5
	xor	r14d,r10d
	and	r12d,ecx
	pshufd	xmm7,xmm7,128
	xor	r13d,ecx
	add	r9d,DWORD[8+rsp]
	mov	r15d,r10d
	psrldq	xmm7,8
	xor	r12d,r8d
	ror	r14d,11
	xor	r15d,r11d
	add	r9d,r12d
	ror	r13d,6
	paddd	xmm0,xmm7
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	pshufd	xmm7,xmm0,80
	xor	edi,r11d
	ror	r14d,2
	add	ebx,r9d
	movdqa	xmm6,xmm7
	add	r9d,edi
	mov	r13d,ebx
	psrld	xmm7,10
	add	r14d,r9d
	ror	r13d,14
	psrlq	xmm6,17
	mov	r9d,r14d
	mov	r12d,ecx
	pxor	xmm7,xmm6
	ror	r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	ror	r13d,5
	xor	r14d,r9d
	psrlq	xmm6,2
	and	r12d,ebx
	xor	r13d,ebx
	add	r8d,DWORD[12+rsp]
	pxor	xmm7,xmm6
	mov	edi,r9d
	xor	r12d,edx
	ror	r14d,11
	pshufd	xmm7,xmm7,8
	xor	edi,r10d
	add	r8d,r12d
	movdqa	xmm6,XMMWORD[rbp]
	ror	r13d,6
	and	r15d,edi
	pslldq	xmm7,8
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	paddd	xmm0,xmm7
	ror	r14d,2
	add	eax,r8d
	add	r8d,r15d
	paddd	xmm6,xmm0
	mov	r13d,eax
	add	r14d,r8d
	movdqa	XMMWORD[rsp],xmm6
	ror	r13d,14
	movdqa	xmm4,xmm2
	mov	r8d,r14d
	mov	r12d,ebx
	movdqa	xmm7,xmm0
	ror	r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	ror	r13d,5
	xor	r14d,r8d
DB	102,15,58,15,225,4
	and	r12d,eax
	xor	r13d,eax
DB	102,15,58,15,251,4
	add	edx,DWORD[16+rsp]
	mov	r15d,r8d
	xor	r12d,ecx
	ror	r14d,11
	movdqa	xmm5,xmm4
	xor	r15d,r9d
	add	edx,r12d
	movdqa	xmm6,xmm4
	ror	r13d,6
	and	edi,r15d
	psrld	xmm4,3
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	paddd	xmm1,xmm7
	ror	r14d,2
	add	r11d,edx
	psrld	xmm6,7
	add	edx,edi
	mov	r13d,r11d
	pshufd	xmm7,xmm0,250
	add	r14d,edx
	ror	r13d,14
	pslld	xmm5,14
	mov	edx,r14d
	mov	r12d,eax
	pxor	xmm4,xmm6
	ror	r14d,9
	xor	r13d,r11d
	xor	r12d,ebx
	ror	r13d,5
	psrld	xmm6,11
	xor	r14d,edx
	pxor	xmm4,xmm5
	and	r12d,r11d
	xor	r13d,r11d
	pslld	xmm5,11
	add	ecx,DWORD[20+rsp]
	mov	edi,edx
	pxor	xmm4,xmm6
	xor	r12d,ebx
	ror	r14d,11
	movdqa	xmm6,xmm7
	xor	edi,r8d
	add	ecx,r12d
	pxor	xmm4,xmm5
	ror	r13d,6
	and	r15d,edi
	xor	r14d,edx
	psrld	xmm7,10
	add	ecx,r13d
	xor	r15d,r8d
	paddd	xmm1,xmm4
	ror	r14d,2
	add	r10d,ecx
	psrlq	xmm6,17
	add	ecx,r15d
	mov	r13d,r10d
	add	r14d,ecx
	pxor	xmm7,xmm6
	ror	r13d,14
	mov	ecx,r14d
	mov	r12d,r11d
	ror	r14d,9
	psrlq	xmm6,2
	xor	r13d,r10d
	xor	r12d,eax
	pxor	xmm7,xmm6
	ror	r13d,5
	xor	r14d,ecx
	and	r12d,r10d
	pshufd	xmm7,xmm7,128
	xor	r13d,r10d
	add	ebx,DWORD[24+rsp]
	mov	r15d,ecx
	psrldq	xmm7,8
	xor	r12d,eax
	ror	r14d,11
	xor	r15d,edx
	add	ebx,r12d
	ror	r13d,6
	paddd	xmm1,xmm7
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	pshufd	xmm7,xmm1,80
	xor	edi,edx
	ror	r14d,2
	add	r9d,ebx
	movdqa	xmm6,xmm7
	add	ebx,edi
	mov	r13d,r9d
	psrld	xmm7,10
	add	r14d,ebx
	ror	r13d,14
	psrlq	xmm6,17
	mov	ebx,r14d
	mov	r12d,r10d
	pxor	xmm7,xmm6
	ror	r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	ror	r13d,5
	xor	r14d,ebx
	psrlq	xmm6,2
	and	r12d,r9d
	xor	r13d,r9d
	add	eax,DWORD[28+rsp]
	pxor	xmm7,xmm6
	mov	edi,ebx
	xor	r12d,r11d
	ror	r14d,11
	pshufd	xmm7,xmm7,8
	xor	edi,ecx
	add	eax,r12d
	movdqa	xmm6,XMMWORD[32+rbp]
	ror	r13d,6
	and	r15d,edi
	pslldq	xmm7,8
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	paddd	xmm1,xmm7
	ror	r14d,2
	add	r8d,eax
	add	eax,r15d
	paddd	xmm6,xmm1
	mov	r13d,r8d
	add	r14d,eax
	movdqa	XMMWORD[16+rsp],xmm6
	ror	r13d,14
	movdqa	xmm4,xmm3
	mov	eax,r14d
	mov	r12d,r9d
	movdqa	xmm7,xmm1
	ror	r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	ror	r13d,5
	xor	r14d,eax
DB	102,15,58,15,226,4
	and	r12d,r8d
	xor	r13d,r8d
DB	102,15,58,15,248,4
	add	r11d,DWORD[32+rsp]
	mov	r15d,eax
	xor	r12d,r10d
	ror	r14d,11
	movdqa	xmm5,xmm4
	xor	r15d,ebx
	add	r11d,r12d
	movdqa	xmm6,xmm4
	ror	r13d,6
	and	edi,r15d
	psrld	xmm4,3
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	paddd	xmm2,xmm7
	ror	r14d,2
	add	edx,r11d
	psrld	xmm6,7
	add	r11d,edi
	mov	r13d,edx
	pshufd	xmm7,xmm1,250
	add	r14d,r11d
	ror	r13d,14
	pslld	xmm5,14
	mov	r11d,r14d
	mov	r12d,r8d
	pxor	xmm4,xmm6
	ror	r14d,9
	xor	r13d,edx
	xor	r12d,r9d
	ror	r13d,5
	psrld	xmm6,11
	xor	r14d,r11d
	pxor	xmm4,xmm5
	and	r12d,edx
	xor	r13d,edx
	pslld	xmm5,11
	add	r10d,DWORD[36+rsp]
	mov	edi,r11d
	pxor	xmm4,xmm6
	xor	r12d,r9d
	ror	r14d,11
	movdqa	xmm6,xmm7
	xor	edi,eax
	add	r10d,r12d
	pxor	xmm4,xmm5
	ror	r13d,6
	and	r15d,edi
	xor	r14d,r11d
	psrld	xmm7,10
	add	r10d,r13d
	xor	r15d,eax
	paddd	xmm2,xmm4
	ror	r14d,2
	add	ecx,r10d
	psrlq	xmm6,17
	add	r10d,r15d
	mov	r13d,ecx
	add	r14d,r10d
	pxor	xmm7,xmm6
	ror	r13d,14
	mov	r10d,r14d
	mov	r12d,edx
	ror	r14d,9
	psrlq	xmm6,2
	xor	r13d,ecx
	xor	r12d,r8d
	pxor	xmm7,xmm6
	ror	r13d,5
	xor	r14d,r10d
	and	r12d,ecx
	pshufd	xmm7,xmm7,128
	xor	r13d,ecx
	add	r9d,DWORD[40+rsp]
	mov	r15d,r10d
	psrldq	xmm7,8
	xor	r12d,r8d
	ror	r14d,11
	xor	r15d,r11d
	add	r9d,r12d
	ror	r13d,6
	paddd	xmm2,xmm7
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	pshufd	xmm7,xmm2,80
	xor	edi,r11d
	ror	r14d,2
	add	ebx,r9d
	movdqa	xmm6,xmm7
	add	r9d,edi
	mov	r13d,ebx
	psrld	xmm7,10
	add	r14d,r9d
	ror	r13d,14
	psrlq	xmm6,17
	mov	r9d,r14d
	mov	r12d,ecx
	pxor	xmm7,xmm6
	ror	r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	ror	r13d,5
	xor	r14d,r9d
	psrlq	xmm6,2
	and	r12d,ebx
	xor	r13d,ebx
	add	r8d,DWORD[44+rsp]
	pxor	xmm7,xmm6
	mov	edi,r9d
	xor	r12d,edx
	ror	r14d,11
	pshufd	xmm7,xmm7,8
	xor	edi,r10d
	add	r8d,r12d
	movdqa	xmm6,XMMWORD[64+rbp]
	ror	r13d,6
	and	r15d,edi
	pslldq	xmm7,8
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	paddd	xmm2,xmm7
	ror	r14d,2
	add	eax,r8d
	add	r8d,r15d
	paddd	xmm6,xmm2
	mov	r13d,eax
	add	r14d,r8d
	movdqa	XMMWORD[32+rsp],xmm6
	ror	r13d,14
	movdqa	xmm4,xmm0
	mov	r8d,r14d
	mov	r12d,ebx
	movdqa	xmm7,xmm2
	ror	r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	ror	r13d,5
	xor	r14d,r8d
DB	102,15,58,15,227,4
	and	r12d,eax
	xor	r13d,eax
DB	102,15,58,15,249,4
	add	edx,DWORD[48+rsp]
	mov	r15d,r8d
	xor	r12d,ecx
	ror	r14d,11
	movdqa	xmm5,xmm4
	xor	r15d,r9d
	add	edx,r12d
	movdqa	xmm6,xmm4
	ror	r13d,6
	and	edi,r15d
	psrld	xmm4,3
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	paddd	xmm3,xmm7
	ror	r14d,2
	add	r11d,edx
	psrld	xmm6,7
	add	edx,edi
	mov	r13d,r11d
	pshufd	xmm7,xmm2,250
	add	r14d,edx
	ror	r13d,14
	pslld	xmm5,14
	mov	edx,r14d
	mov	r12d,eax
	pxor	xmm4,xmm6
	ror	r14d,9
	xor	r13d,r11d
	xor	r12d,ebx
	ror	r13d,5
	psrld	xmm6,11
	xor	r14d,edx
	pxor	xmm4,xmm5
	and	r12d,r11d
	xor	r13d,r11d
	pslld	xmm5,11
	add	ecx,DWORD[52+rsp]
	mov	edi,edx
	pxor	xmm4,xmm6
	xor	r12d,ebx
	ror	r14d,11
	movdqa	xmm6,xmm7
	xor	edi,r8d
	add	ecx,r12d
	pxor	xmm4,xmm5
	ror	r13d,6
	and	r15d,edi
	xor	r14d,edx
	psrld	xmm7,10
	add	ecx,r13d
	xor	r15d,r8d
	paddd	xmm3,xmm4
	ror	r14d,2
	add	r10d,ecx
	psrlq	xmm6,17
	add	ecx,r15d
	mov	r13d,r10d
	add	r14d,ecx
	pxor	xmm7,xmm6
	ror	r13d,14
	mov	ecx,r14d
	mov	r12d,r11d
	ror	r14d,9
	psrlq	xmm6,2
	xor	r13d,r10d
	xor	r12d,eax
	pxor	xmm7,xmm6
	ror	r13d,5
	xor	r14d,ecx
	and	r12d,r10d
	pshufd	xmm7,xmm7,128
	xor	r13d,r10d
	add	ebx,DWORD[56+rsp]
	mov	r15d,ecx
	psrldq	xmm7,8
	xor	r12d,eax
	ror	r14d,11
	xor	r15d,edx
	add	ebx,r12d
	ror	r13d,6
	paddd	xmm3,xmm7
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	pshufd	xmm7,xmm3,80
	xor	edi,edx
	ror	r14d,2
	add	r9d,ebx
	movdqa	xmm6,xmm7
	add	ebx,edi
	mov	r13d,r9d
	psrld	xmm7,10
	add	r14d,ebx
	ror	r13d,14
	psrlq	xmm6,17
	mov	ebx,r14d
	mov	r12d,r10d
	pxor	xmm7,xmm6
	ror	r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	ror	r13d,5
	xor	r14d,ebx
	psrlq	xmm6,2
	and	r12d,r9d
	xor	r13d,r9d
	add	eax,DWORD[60+rsp]
	pxor	xmm7,xmm6
	mov	edi,ebx
	xor	r12d,r11d
	ror	r14d,11
	pshufd	xmm7,xmm7,8
	xor	edi,ecx
	add	eax,r12d
	movdqa	xmm6,XMMWORD[96+rbp]
	ror	r13d,6
	and	r15d,edi
	pslldq	xmm7,8
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	paddd	xmm3,xmm7
	ror	r14d,2
	add	r8d,eax
	add	eax,r15d
	paddd	xmm6,xmm3
	mov	r13d,r8d
	add	r14d,eax
	movdqa	XMMWORD[48+rsp],xmm6
	cmp	BYTE[131+rbp],0
	jne	NEAR $L$ssse3_00_47
	ror	r13d,14
	mov	eax,r14d
	mov	r12d,r9d
	ror	r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	ror	r13d,5
	xor	r14d,eax
	and	r12d,r8d
	xor	r13d,r8d
	add	r11d,DWORD[rsp]
	mov	r15d,eax
	xor	r12d,r10d
	ror	r14d,11
	xor	r15d,ebx
	add	r11d,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	ror	r14d,2
	add	edx,r11d
	add	r11d,edi
	mov	r13d,edx
	add	r14d,r11d
	ror	r13d,14
	mov	r11d,r14d
	mov	r12d,r8d
	ror	r14d,9
	xor	r13d,edx
	xor	r12d,r9d
	ror	r13d,5
	xor	r14d,r11d
	and	r12d,edx
	xor	r13d,edx
	add	r10d,DWORD[4+rsp]
	mov	edi,r11d
	xor	r12d,r9d
	ror	r14d,11
	xor	edi,eax
	add	r10d,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,r11d
	add	r10d,r13d
	xor	r15d,eax
	ror	r14d,2
	add	ecx,r10d
	add	r10d,r15d
	mov	r13d,ecx
	add	r14d,r10d
	ror	r13d,14
	mov	r10d,r14d
	mov	r12d,edx
	ror	r14d,9
	xor	r13d,ecx
	xor	r12d,r8d
	ror	r13d,5
	xor	r14d,r10d
	and	r12d,ecx
	xor	r13d,ecx
	add	r9d,DWORD[8+rsp]
	mov	r15d,r10d
	xor	r12d,r8d
	ror	r14d,11
	xor	r15d,r11d
	add	r9d,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	xor	edi,r11d
	ror	r14d,2
	add	ebx,r9d
	add	r9d,edi
	mov	r13d,ebx
	add	r14d,r9d
	ror	r13d,14
	mov	r9d,r14d
	mov	r12d,ecx
	ror	r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	ror	r13d,5
	xor	r14d,r9d
	and	r12d,ebx
	xor	r13d,ebx
	add	r8d,DWORD[12+rsp]
	mov	edi,r9d
	xor	r12d,edx
	ror	r14d,11
	xor	edi,r10d
	add	r8d,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	ror	r14d,2
	add	eax,r8d
	add	r8d,r15d
	mov	r13d,eax
	add	r14d,r8d
	ror	r13d,14
	mov	r8d,r14d
	mov	r12d,ebx
	ror	r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	ror	r13d,5
	xor	r14d,r8d
	and	r12d,eax
	xor	r13d,eax
	add	edx,DWORD[16+rsp]
	mov	r15d,r8d
	xor	r12d,ecx
	ror	r14d,11
	xor	r15d,r9d
	add	edx,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	ror	r14d,2
	add	r11d,edx
	add	edx,edi
	mov	r13d,r11d
	add	r14d,edx
	ror	r13d,14
	mov	edx,r14d
	mov	r12d,eax
	ror	r14d,9
	xor	r13d,r11d
	xor	r12d,ebx
	ror	r13d,5
	xor	r14d,edx
	and	r12d,r11d
	xor	r13d,r11d
	add	ecx,DWORD[20+rsp]
	mov	edi,edx
	xor	r12d,ebx
	ror	r14d,11
	xor	edi,r8d
	add	ecx,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,edx
	add	ecx,r13d
	xor	r15d,r8d
	ror	r14d,2
	add	r10d,ecx
	add	ecx,r15d
	mov	r13d,r10d
	add	r14d,ecx
	ror	r13d,14
	mov	ecx,r14d
	mov	r12d,r11d
	ror	r14d,9
	xor	r13d,r10d
	xor	r12d,eax
	ror	r13d,5
	xor	r14d,ecx
	and	r12d,r10d
	xor	r13d,r10d
	add	ebx,DWORD[24+rsp]
	mov	r15d,ecx
	xor	r12d,eax
	ror	r14d,11
	xor	r15d,edx
	add	ebx,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	xor	edi,edx
	ror	r14d,2
	add	r9d,ebx
	add	ebx,edi
	mov	r13d,r9d
	add	r14d,ebx
	ror	r13d,14
	mov	ebx,r14d
	mov	r12d,r10d
	ror	r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	ror	r13d,5
	xor	r14d,ebx
	and	r12d,r9d
	xor	r13d,r9d
	add	eax,DWORD[28+rsp]
	mov	edi,ebx
	xor	r12d,r11d
	ror	r14d,11
	xor	edi,ecx
	add	eax,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	ror	r14d,2
	add	r8d,eax
	add	eax,r15d
	mov	r13d,r8d
	add	r14d,eax
	ror	r13d,14
	mov	eax,r14d
	mov	r12d,r9d
	ror	r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	ror	r13d,5
	xor	r14d,eax
	and	r12d,r8d
	xor	r13d,r8d
	add	r11d,DWORD[32+rsp]
	mov	r15d,eax
	xor	r12d,r10d
	ror	r14d,11
	xor	r15d,ebx
	add	r11d,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	ror	r14d,2
	add	edx,r11d
	add	r11d,edi
	mov	r13d,edx
	add	r14d,r11d
	ror	r13d,14
	mov	r11d,r14d
	mov	r12d,r8d
	ror	r14d,9
	xor	r13d,edx
	xor	r12d,r9d
	ror	r13d,5
	xor	r14d,r11d
	and	r12d,edx
	xor	r13d,edx
	add	r10d,DWORD[36+rsp]
	mov	edi,r11d
	xor	r12d,r9d
	ror	r14d,11
	xor	edi,eax
	add	r10d,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,r11d
	add	r10d,r13d
	xor	r15d,eax
	ror	r14d,2
	add	ecx,r10d
	add	r10d,r15d
	mov	r13d,ecx
	add	r14d,r10d
	ror	r13d,14
	mov	r10d,r14d
	mov	r12d,edx
	ror	r14d,9
	xor	r13d,ecx
	xor	r12d,r8d
	ror	r13d,5
	xor	r14d,r10d
	and	r12d,ecx
	xor	r13d,ecx
	add	r9d,DWORD[40+rsp]
	mov	r15d,r10d
	xor	r12d,r8d
	ror	r14d,11
	xor	r15d,r11d
	add	r9d,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	xor	edi,r11d
	ror	r14d,2
	add	ebx,r9d
	add	r9d,edi
	mov	r13d,ebx
	add	r14d,r9d
	ror	r13d,14
	mov	r9d,r14d
	mov	r12d,ecx
	ror	r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	ror	r13d,5
	xor	r14d,r9d
	and	r12d,ebx
	xor	r13d,ebx
	add	r8d,DWORD[44+rsp]
	mov	edi,r9d
	xor	r12d,edx
	ror	r14d,11
	xor	edi,r10d
	add	r8d,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	ror	r14d,2
	add	eax,r8d
	add	r8d,r15d
	mov	r13d,eax
	add	r14d,r8d
	ror	r13d,14
	mov	r8d,r14d
	mov	r12d,ebx
	ror	r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	ror	r13d,5
	xor	r14d,r8d
	and	r12d,eax
	xor	r13d,eax
	add	edx,DWORD[48+rsp]
	mov	r15d,r8d
	xor	r12d,ecx
	ror	r14d,11
	xor	r15d,r9d
	add	edx,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	ror	r14d,2
	add	r11d,edx
	add	edx,edi
	mov	r13d,r11d
	add	r14d,edx
	ror	r13d,14
	mov	edx,r14d
	mov	r12d,eax
	ror	r14d,9
	xor	r13d,r11d
	xor	r12d,ebx
	ror	r13d,5
	xor	r14d,edx
	and	r12d,r11d
	xor	r13d,r11d
	add	ecx,DWORD[52+rsp]
	mov	edi,edx
	xor	r12d,ebx
	ror	r14d,11
	xor	edi,r8d
	add	ecx,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,edx
	add	ecx,r13d
	xor	r15d,r8d
	ror	r14d,2
	add	r10d,ecx
	add	ecx,r15d
	mov	r13d,r10d
	add	r14d,ecx
	ror	r13d,14
	mov	ecx,r14d
	mov	r12d,r11d
	ror	r14d,9
	xor	r13d,r10d
	xor	r12d,eax
	ror	r13d,5
	xor	r14d,ecx
	and	r12d,r10d
	xor	r13d,r10d
	add	ebx,DWORD[56+rsp]
	mov	r15d,ecx
	xor	r12d,eax
	ror	r14d,11
	xor	r15d,edx
	add	ebx,r12d
	ror	r13d,6
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	xor	edi,edx
	ror	r14d,2
	add	r9d,ebx
	add	ebx,edi
	mov	r13d,r9d
	add	r14d,ebx
	ror	r13d,14
	mov	ebx,r14d
	mov	r12d,r10d
	ror	r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	ror	r13d,5
	xor	r14d,ebx
	and	r12d,r9d
	xor	r13d,r9d
	add	eax,DWORD[60+rsp]
	mov	edi,ebx
	xor	r12d,r11d
	ror	r14d,11
	xor	edi,ecx
	add	eax,r12d
	ror	r13d,6
	and	r15d,edi
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	ror	r14d,2
	add	r8d,eax
	add	eax,r15d
	mov	r13d,r8d
	add	r14d,eax
	mov	rdi,QWORD[((64+0))+rsp]
	mov	eax,r14d

	add	eax,DWORD[rdi]
	lea	rsi,[64+rsi]
	add	ebx,DWORD[4+rdi]
	add	ecx,DWORD[8+rdi]
	add	edx,DWORD[12+rdi]
	add	r8d,DWORD[16+rdi]
	add	r9d,DWORD[20+rdi]
	add	r10d,DWORD[24+rdi]
	add	r11d,DWORD[28+rdi]

	cmp	rsi,QWORD[((64+16))+rsp]

	mov	DWORD[rdi],eax
	mov	DWORD[4+rdi],ebx
	mov	DWORD[8+rdi],ecx
	mov	DWORD[12+rdi],edx
	mov	DWORD[16+rdi],r8d
	mov	DWORD[20+rdi],r9d
	mov	DWORD[24+rdi],r10d
	mov	DWORD[28+rdi],r11d
	jb	NEAR $L$loop_ssse3

	mov	rsi,QWORD[88+rsp]

	movaps	xmm6,XMMWORD[((64+32))+rsp]
	movaps	xmm7,XMMWORD[((64+48))+rsp]
	movaps	xmm8,XMMWORD[((64+64))+rsp]
	movaps	xmm9,XMMWORD[((64+80))+rsp]
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
SEH64_ENDPROC_FRAME	sha256_block_data_order_ssse3
$L$SEH_end_sha256_block_data_order_ssse3:

ALIGN	64
SEH64_PROC_FRAME sha256_block_data_order_avx,PRIVATE
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



$L$avx_shortcut:
	mov	rax,rsp

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
	shl	rdx,4
	sub	rsp,160
	lea	rdx,[rdx*4+rsi]
	and	rsp,-64
	mov	QWORD[((64+0))+rsp],rdi
	mov	QWORD[((64+8))+rsp],rsi
	mov	QWORD[((64+16))+rsp],rdx
	mov	QWORD[88+rsp],rax

	movaps	XMMWORD[(64+32)+rsp],xmm6
	movaps	XMMWORD[(64+48)+rsp],xmm7
	movaps	XMMWORD[(64+64)+rsp],xmm8
	movaps	XMMWORD[(64+80)+rsp],xmm9
; Hack using rax to load saved CFA expression: 88(%rsp),deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ALLOCSTACK 88	; Can only deref by .PUSHREG, so adjust RSP by feigning stack alloc.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56

	vzeroupper
	mov	eax,DWORD[rdi]
	mov	ebx,DWORD[4+rdi]
	mov	ecx,DWORD[8+rdi]
	mov	edx,DWORD[12+rdi]
	mov	r8d,DWORD[16+rdi]
	mov	r9d,DWORD[20+rdi]
	mov	r10d,DWORD[24+rdi]
	mov	r11d,DWORD[28+rdi]
	vmovdqa	xmm8,XMMWORD[((K256+512+32))]
	vmovdqa	xmm9,XMMWORD[((K256+512+64))]
	jmp	NEAR $L$loop_avx
ALIGN	16
$L$loop_avx:
	vmovdqa	xmm7,XMMWORD[((K256+512))]
	vmovdqu	xmm0,XMMWORD[rsi]
	vmovdqu	xmm1,XMMWORD[16+rsi]
	vmovdqu	xmm2,XMMWORD[32+rsi]
	vmovdqu	xmm3,XMMWORD[48+rsi]
	vpshufb	xmm0,xmm0,xmm7
	lea	rbp,[K256]
	vpshufb	xmm1,xmm1,xmm7
	vpshufb	xmm2,xmm2,xmm7
	vpaddd	xmm4,xmm0,XMMWORD[rbp]
	vpshufb	xmm3,xmm3,xmm7
	vpaddd	xmm5,xmm1,XMMWORD[32+rbp]
	vpaddd	xmm6,xmm2,XMMWORD[64+rbp]
	vpaddd	xmm7,xmm3,XMMWORD[96+rbp]
	vmovdqa	XMMWORD[rsp],xmm4
	mov	r14d,eax
	vmovdqa	XMMWORD[16+rsp],xmm5
	mov	edi,ebx
	vmovdqa	XMMWORD[32+rsp],xmm6
	xor	edi,ecx
	vmovdqa	XMMWORD[48+rsp],xmm7
	mov	r13d,r8d
	jmp	NEAR $L$avx_00_47

ALIGN	16
$L$avx_00_47:
	sub	rbp,-128
	vpalignr	xmm4,xmm1,xmm0,4
	shrd	r13d,r13d,14
	mov	eax,r14d
	mov	r12d,r9d
	vpalignr	xmm7,xmm3,xmm2,4
	shrd	r14d,r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	vpsrld	xmm6,xmm4,7
	shrd	r13d,r13d,5
	xor	r14d,eax
	and	r12d,r8d
	vpaddd	xmm0,xmm0,xmm7
	xor	r13d,r8d
	add	r11d,DWORD[rsp]
	mov	r15d,eax
	vpsrld	xmm7,xmm4,3
	xor	r12d,r10d
	shrd	r14d,r14d,11
	xor	r15d,ebx
	vpslld	xmm5,xmm4,14
	add	r11d,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	vpxor	xmm4,xmm7,xmm6
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	vpshufd	xmm7,xmm3,250
	shrd	r14d,r14d,2
	add	edx,r11d
	add	r11d,edi
	vpsrld	xmm6,xmm6,11
	mov	r13d,edx
	add	r14d,r11d
	shrd	r13d,r13d,14
	vpxor	xmm4,xmm4,xmm5
	mov	r11d,r14d
	mov	r12d,r8d
	shrd	r14d,r14d,9
	vpslld	xmm5,xmm5,11
	xor	r13d,edx
	xor	r12d,r9d
	shrd	r13d,r13d,5
	vpxor	xmm4,xmm4,xmm6
	xor	r14d,r11d
	and	r12d,edx
	xor	r13d,edx
	vpsrld	xmm6,xmm7,10
	add	r10d,DWORD[4+rsp]
	mov	edi,r11d
	xor	r12d,r9d
	vpxor	xmm4,xmm4,xmm5
	shrd	r14d,r14d,11
	xor	edi,eax
	add	r10d,r12d
	vpsrlq	xmm7,xmm7,17
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r11d
	vpaddd	xmm0,xmm0,xmm4
	add	r10d,r13d
	xor	r15d,eax
	shrd	r14d,r14d,2
	vpxor	xmm6,xmm6,xmm7
	add	ecx,r10d
	add	r10d,r15d
	mov	r13d,ecx
	vpsrlq	xmm7,xmm7,2
	add	r14d,r10d
	shrd	r13d,r13d,14
	mov	r10d,r14d
	vpxor	xmm6,xmm6,xmm7
	mov	r12d,edx
	shrd	r14d,r14d,9
	xor	r13d,ecx
	vpshufb	xmm6,xmm6,xmm8
	xor	r12d,r8d
	shrd	r13d,r13d,5
	xor	r14d,r10d
	vpaddd	xmm0,xmm0,xmm6
	and	r12d,ecx
	xor	r13d,ecx
	add	r9d,DWORD[8+rsp]
	vpshufd	xmm7,xmm0,80
	mov	r15d,r10d
	xor	r12d,r8d
	shrd	r14d,r14d,11
	vpsrld	xmm6,xmm7,10
	xor	r15d,r11d
	add	r9d,r12d
	shrd	r13d,r13d,6
	vpsrlq	xmm7,xmm7,17
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	vpxor	xmm6,xmm6,xmm7
	xor	edi,r11d
	shrd	r14d,r14d,2
	add	ebx,r9d
	vpsrlq	xmm7,xmm7,2
	add	r9d,edi
	mov	r13d,ebx
	add	r14d,r9d
	vpxor	xmm6,xmm6,xmm7
	shrd	r13d,r13d,14
	mov	r9d,r14d
	mov	r12d,ecx
	vpshufb	xmm6,xmm6,xmm9
	shrd	r14d,r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	vpaddd	xmm0,xmm0,xmm6
	shrd	r13d,r13d,5
	xor	r14d,r9d
	and	r12d,ebx
	vpaddd	xmm6,xmm0,XMMWORD[rbp]
	xor	r13d,ebx
	add	r8d,DWORD[12+rsp]
	mov	edi,r9d
	xor	r12d,edx
	shrd	r14d,r14d,11
	xor	edi,r10d
	add	r8d,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	shrd	r14d,r14d,2
	add	eax,r8d
	add	r8d,r15d
	mov	r13d,eax
	add	r14d,r8d
	vmovdqa	XMMWORD[rsp],xmm6
	vpalignr	xmm4,xmm2,xmm1,4
	shrd	r13d,r13d,14
	mov	r8d,r14d
	mov	r12d,ebx
	vpalignr	xmm7,xmm0,xmm3,4
	shrd	r14d,r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	vpsrld	xmm6,xmm4,7
	shrd	r13d,r13d,5
	xor	r14d,r8d
	and	r12d,eax
	vpaddd	xmm1,xmm1,xmm7
	xor	r13d,eax
	add	edx,DWORD[16+rsp]
	mov	r15d,r8d
	vpsrld	xmm7,xmm4,3
	xor	r12d,ecx
	shrd	r14d,r14d,11
	xor	r15d,r9d
	vpslld	xmm5,xmm4,14
	add	edx,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	vpxor	xmm4,xmm7,xmm6
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	vpshufd	xmm7,xmm0,250
	shrd	r14d,r14d,2
	add	r11d,edx
	add	edx,edi
	vpsrld	xmm6,xmm6,11
	mov	r13d,r11d
	add	r14d,edx
	shrd	r13d,r13d,14
	vpxor	xmm4,xmm4,xmm5
	mov	edx,r14d
	mov	r12d,eax
	shrd	r14d,r14d,9
	vpslld	xmm5,xmm5,11
	xor	r13d,r11d
	xor	r12d,ebx
	shrd	r13d,r13d,5
	vpxor	xmm4,xmm4,xmm6
	xor	r14d,edx
	and	r12d,r11d
	xor	r13d,r11d
	vpsrld	xmm6,xmm7,10
	add	ecx,DWORD[20+rsp]
	mov	edi,edx
	xor	r12d,ebx
	vpxor	xmm4,xmm4,xmm5
	shrd	r14d,r14d,11
	xor	edi,r8d
	add	ecx,r12d
	vpsrlq	xmm7,xmm7,17
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,edx
	vpaddd	xmm1,xmm1,xmm4
	add	ecx,r13d
	xor	r15d,r8d
	shrd	r14d,r14d,2
	vpxor	xmm6,xmm6,xmm7
	add	r10d,ecx
	add	ecx,r15d
	mov	r13d,r10d
	vpsrlq	xmm7,xmm7,2
	add	r14d,ecx
	shrd	r13d,r13d,14
	mov	ecx,r14d
	vpxor	xmm6,xmm6,xmm7
	mov	r12d,r11d
	shrd	r14d,r14d,9
	xor	r13d,r10d
	vpshufb	xmm6,xmm6,xmm8
	xor	r12d,eax
	shrd	r13d,r13d,5
	xor	r14d,ecx
	vpaddd	xmm1,xmm1,xmm6
	and	r12d,r10d
	xor	r13d,r10d
	add	ebx,DWORD[24+rsp]
	vpshufd	xmm7,xmm1,80
	mov	r15d,ecx
	xor	r12d,eax
	shrd	r14d,r14d,11
	vpsrld	xmm6,xmm7,10
	xor	r15d,edx
	add	ebx,r12d
	shrd	r13d,r13d,6
	vpsrlq	xmm7,xmm7,17
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	vpxor	xmm6,xmm6,xmm7
	xor	edi,edx
	shrd	r14d,r14d,2
	add	r9d,ebx
	vpsrlq	xmm7,xmm7,2
	add	ebx,edi
	mov	r13d,r9d
	add	r14d,ebx
	vpxor	xmm6,xmm6,xmm7
	shrd	r13d,r13d,14
	mov	ebx,r14d
	mov	r12d,r10d
	vpshufb	xmm6,xmm6,xmm9
	shrd	r14d,r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	vpaddd	xmm1,xmm1,xmm6
	shrd	r13d,r13d,5
	xor	r14d,ebx
	and	r12d,r9d
	vpaddd	xmm6,xmm1,XMMWORD[32+rbp]
	xor	r13d,r9d
	add	eax,DWORD[28+rsp]
	mov	edi,ebx
	xor	r12d,r11d
	shrd	r14d,r14d,11
	xor	edi,ecx
	add	eax,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	shrd	r14d,r14d,2
	add	r8d,eax
	add	eax,r15d
	mov	r13d,r8d
	add	r14d,eax
	vmovdqa	XMMWORD[16+rsp],xmm6
	vpalignr	xmm4,xmm3,xmm2,4
	shrd	r13d,r13d,14
	mov	eax,r14d
	mov	r12d,r9d
	vpalignr	xmm7,xmm1,xmm0,4
	shrd	r14d,r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	vpsrld	xmm6,xmm4,7
	shrd	r13d,r13d,5
	xor	r14d,eax
	and	r12d,r8d
	vpaddd	xmm2,xmm2,xmm7
	xor	r13d,r8d
	add	r11d,DWORD[32+rsp]
	mov	r15d,eax
	vpsrld	xmm7,xmm4,3
	xor	r12d,r10d
	shrd	r14d,r14d,11
	xor	r15d,ebx
	vpslld	xmm5,xmm4,14
	add	r11d,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	vpxor	xmm4,xmm7,xmm6
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	vpshufd	xmm7,xmm1,250
	shrd	r14d,r14d,2
	add	edx,r11d
	add	r11d,edi
	vpsrld	xmm6,xmm6,11
	mov	r13d,edx
	add	r14d,r11d
	shrd	r13d,r13d,14
	vpxor	xmm4,xmm4,xmm5
	mov	r11d,r14d
	mov	r12d,r8d
	shrd	r14d,r14d,9
	vpslld	xmm5,xmm5,11
	xor	r13d,edx
	xor	r12d,r9d
	shrd	r13d,r13d,5
	vpxor	xmm4,xmm4,xmm6
	xor	r14d,r11d
	and	r12d,edx
	xor	r13d,edx
	vpsrld	xmm6,xmm7,10
	add	r10d,DWORD[36+rsp]
	mov	edi,r11d
	xor	r12d,r9d
	vpxor	xmm4,xmm4,xmm5
	shrd	r14d,r14d,11
	xor	edi,eax
	add	r10d,r12d
	vpsrlq	xmm7,xmm7,17
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r11d
	vpaddd	xmm2,xmm2,xmm4
	add	r10d,r13d
	xor	r15d,eax
	shrd	r14d,r14d,2
	vpxor	xmm6,xmm6,xmm7
	add	ecx,r10d
	add	r10d,r15d
	mov	r13d,ecx
	vpsrlq	xmm7,xmm7,2
	add	r14d,r10d
	shrd	r13d,r13d,14
	mov	r10d,r14d
	vpxor	xmm6,xmm6,xmm7
	mov	r12d,edx
	shrd	r14d,r14d,9
	xor	r13d,ecx
	vpshufb	xmm6,xmm6,xmm8
	xor	r12d,r8d
	shrd	r13d,r13d,5
	xor	r14d,r10d
	vpaddd	xmm2,xmm2,xmm6
	and	r12d,ecx
	xor	r13d,ecx
	add	r9d,DWORD[40+rsp]
	vpshufd	xmm7,xmm2,80
	mov	r15d,r10d
	xor	r12d,r8d
	shrd	r14d,r14d,11
	vpsrld	xmm6,xmm7,10
	xor	r15d,r11d
	add	r9d,r12d
	shrd	r13d,r13d,6
	vpsrlq	xmm7,xmm7,17
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	vpxor	xmm6,xmm6,xmm7
	xor	edi,r11d
	shrd	r14d,r14d,2
	add	ebx,r9d
	vpsrlq	xmm7,xmm7,2
	add	r9d,edi
	mov	r13d,ebx
	add	r14d,r9d
	vpxor	xmm6,xmm6,xmm7
	shrd	r13d,r13d,14
	mov	r9d,r14d
	mov	r12d,ecx
	vpshufb	xmm6,xmm6,xmm9
	shrd	r14d,r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	vpaddd	xmm2,xmm2,xmm6
	shrd	r13d,r13d,5
	xor	r14d,r9d
	and	r12d,ebx
	vpaddd	xmm6,xmm2,XMMWORD[64+rbp]
	xor	r13d,ebx
	add	r8d,DWORD[44+rsp]
	mov	edi,r9d
	xor	r12d,edx
	shrd	r14d,r14d,11
	xor	edi,r10d
	add	r8d,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	shrd	r14d,r14d,2
	add	eax,r8d
	add	r8d,r15d
	mov	r13d,eax
	add	r14d,r8d
	vmovdqa	XMMWORD[32+rsp],xmm6
	vpalignr	xmm4,xmm0,xmm3,4
	shrd	r13d,r13d,14
	mov	r8d,r14d
	mov	r12d,ebx
	vpalignr	xmm7,xmm2,xmm1,4
	shrd	r14d,r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	vpsrld	xmm6,xmm4,7
	shrd	r13d,r13d,5
	xor	r14d,r8d
	and	r12d,eax
	vpaddd	xmm3,xmm3,xmm7
	xor	r13d,eax
	add	edx,DWORD[48+rsp]
	mov	r15d,r8d
	vpsrld	xmm7,xmm4,3
	xor	r12d,ecx
	shrd	r14d,r14d,11
	xor	r15d,r9d
	vpslld	xmm5,xmm4,14
	add	edx,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	vpxor	xmm4,xmm7,xmm6
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	vpshufd	xmm7,xmm2,250
	shrd	r14d,r14d,2
	add	r11d,edx
	add	edx,edi
	vpsrld	xmm6,xmm6,11
	mov	r13d,r11d
	add	r14d,edx
	shrd	r13d,r13d,14
	vpxor	xmm4,xmm4,xmm5
	mov	edx,r14d
	mov	r12d,eax
	shrd	r14d,r14d,9
	vpslld	xmm5,xmm5,11
	xor	r13d,r11d
	xor	r12d,ebx
	shrd	r13d,r13d,5
	vpxor	xmm4,xmm4,xmm6
	xor	r14d,edx
	and	r12d,r11d
	xor	r13d,r11d
	vpsrld	xmm6,xmm7,10
	add	ecx,DWORD[52+rsp]
	mov	edi,edx
	xor	r12d,ebx
	vpxor	xmm4,xmm4,xmm5
	shrd	r14d,r14d,11
	xor	edi,r8d
	add	ecx,r12d
	vpsrlq	xmm7,xmm7,17
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,edx
	vpaddd	xmm3,xmm3,xmm4
	add	ecx,r13d
	xor	r15d,r8d
	shrd	r14d,r14d,2
	vpxor	xmm6,xmm6,xmm7
	add	r10d,ecx
	add	ecx,r15d
	mov	r13d,r10d
	vpsrlq	xmm7,xmm7,2
	add	r14d,ecx
	shrd	r13d,r13d,14
	mov	ecx,r14d
	vpxor	xmm6,xmm6,xmm7
	mov	r12d,r11d
	shrd	r14d,r14d,9
	xor	r13d,r10d
	vpshufb	xmm6,xmm6,xmm8
	xor	r12d,eax
	shrd	r13d,r13d,5
	xor	r14d,ecx
	vpaddd	xmm3,xmm3,xmm6
	and	r12d,r10d
	xor	r13d,r10d
	add	ebx,DWORD[56+rsp]
	vpshufd	xmm7,xmm3,80
	mov	r15d,ecx
	xor	r12d,eax
	shrd	r14d,r14d,11
	vpsrld	xmm6,xmm7,10
	xor	r15d,edx
	add	ebx,r12d
	shrd	r13d,r13d,6
	vpsrlq	xmm7,xmm7,17
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	vpxor	xmm6,xmm6,xmm7
	xor	edi,edx
	shrd	r14d,r14d,2
	add	r9d,ebx
	vpsrlq	xmm7,xmm7,2
	add	ebx,edi
	mov	r13d,r9d
	add	r14d,ebx
	vpxor	xmm6,xmm6,xmm7
	shrd	r13d,r13d,14
	mov	ebx,r14d
	mov	r12d,r10d
	vpshufb	xmm6,xmm6,xmm9
	shrd	r14d,r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	vpaddd	xmm3,xmm3,xmm6
	shrd	r13d,r13d,5
	xor	r14d,ebx
	and	r12d,r9d
	vpaddd	xmm6,xmm3,XMMWORD[96+rbp]
	xor	r13d,r9d
	add	eax,DWORD[60+rsp]
	mov	edi,ebx
	xor	r12d,r11d
	shrd	r14d,r14d,11
	xor	edi,ecx
	add	eax,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	shrd	r14d,r14d,2
	add	r8d,eax
	add	eax,r15d
	mov	r13d,r8d
	add	r14d,eax
	vmovdqa	XMMWORD[48+rsp],xmm6
	cmp	BYTE[131+rbp],0
	jne	NEAR $L$avx_00_47
	shrd	r13d,r13d,14
	mov	eax,r14d
	mov	r12d,r9d
	shrd	r14d,r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	shrd	r13d,r13d,5
	xor	r14d,eax
	and	r12d,r8d
	xor	r13d,r8d
	add	r11d,DWORD[rsp]
	mov	r15d,eax
	xor	r12d,r10d
	shrd	r14d,r14d,11
	xor	r15d,ebx
	add	r11d,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	shrd	r14d,r14d,2
	add	edx,r11d
	add	r11d,edi
	mov	r13d,edx
	add	r14d,r11d
	shrd	r13d,r13d,14
	mov	r11d,r14d
	mov	r12d,r8d
	shrd	r14d,r14d,9
	xor	r13d,edx
	xor	r12d,r9d
	shrd	r13d,r13d,5
	xor	r14d,r11d
	and	r12d,edx
	xor	r13d,edx
	add	r10d,DWORD[4+rsp]
	mov	edi,r11d
	xor	r12d,r9d
	shrd	r14d,r14d,11
	xor	edi,eax
	add	r10d,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r11d
	add	r10d,r13d
	xor	r15d,eax
	shrd	r14d,r14d,2
	add	ecx,r10d
	add	r10d,r15d
	mov	r13d,ecx
	add	r14d,r10d
	shrd	r13d,r13d,14
	mov	r10d,r14d
	mov	r12d,edx
	shrd	r14d,r14d,9
	xor	r13d,ecx
	xor	r12d,r8d
	shrd	r13d,r13d,5
	xor	r14d,r10d
	and	r12d,ecx
	xor	r13d,ecx
	add	r9d,DWORD[8+rsp]
	mov	r15d,r10d
	xor	r12d,r8d
	shrd	r14d,r14d,11
	xor	r15d,r11d
	add	r9d,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	xor	edi,r11d
	shrd	r14d,r14d,2
	add	ebx,r9d
	add	r9d,edi
	mov	r13d,ebx
	add	r14d,r9d
	shrd	r13d,r13d,14
	mov	r9d,r14d
	mov	r12d,ecx
	shrd	r14d,r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	shrd	r13d,r13d,5
	xor	r14d,r9d
	and	r12d,ebx
	xor	r13d,ebx
	add	r8d,DWORD[12+rsp]
	mov	edi,r9d
	xor	r12d,edx
	shrd	r14d,r14d,11
	xor	edi,r10d
	add	r8d,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	shrd	r14d,r14d,2
	add	eax,r8d
	add	r8d,r15d
	mov	r13d,eax
	add	r14d,r8d
	shrd	r13d,r13d,14
	mov	r8d,r14d
	mov	r12d,ebx
	shrd	r14d,r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	shrd	r13d,r13d,5
	xor	r14d,r8d
	and	r12d,eax
	xor	r13d,eax
	add	edx,DWORD[16+rsp]
	mov	r15d,r8d
	xor	r12d,ecx
	shrd	r14d,r14d,11
	xor	r15d,r9d
	add	edx,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	shrd	r14d,r14d,2
	add	r11d,edx
	add	edx,edi
	mov	r13d,r11d
	add	r14d,edx
	shrd	r13d,r13d,14
	mov	edx,r14d
	mov	r12d,eax
	shrd	r14d,r14d,9
	xor	r13d,r11d
	xor	r12d,ebx
	shrd	r13d,r13d,5
	xor	r14d,edx
	and	r12d,r11d
	xor	r13d,r11d
	add	ecx,DWORD[20+rsp]
	mov	edi,edx
	xor	r12d,ebx
	shrd	r14d,r14d,11
	xor	edi,r8d
	add	ecx,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,edx
	add	ecx,r13d
	xor	r15d,r8d
	shrd	r14d,r14d,2
	add	r10d,ecx
	add	ecx,r15d
	mov	r13d,r10d
	add	r14d,ecx
	shrd	r13d,r13d,14
	mov	ecx,r14d
	mov	r12d,r11d
	shrd	r14d,r14d,9
	xor	r13d,r10d
	xor	r12d,eax
	shrd	r13d,r13d,5
	xor	r14d,ecx
	and	r12d,r10d
	xor	r13d,r10d
	add	ebx,DWORD[24+rsp]
	mov	r15d,ecx
	xor	r12d,eax
	shrd	r14d,r14d,11
	xor	r15d,edx
	add	ebx,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	xor	edi,edx
	shrd	r14d,r14d,2
	add	r9d,ebx
	add	ebx,edi
	mov	r13d,r9d
	add	r14d,ebx
	shrd	r13d,r13d,14
	mov	ebx,r14d
	mov	r12d,r10d
	shrd	r14d,r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	shrd	r13d,r13d,5
	xor	r14d,ebx
	and	r12d,r9d
	xor	r13d,r9d
	add	eax,DWORD[28+rsp]
	mov	edi,ebx
	xor	r12d,r11d
	shrd	r14d,r14d,11
	xor	edi,ecx
	add	eax,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	shrd	r14d,r14d,2
	add	r8d,eax
	add	eax,r15d
	mov	r13d,r8d
	add	r14d,eax
	shrd	r13d,r13d,14
	mov	eax,r14d
	mov	r12d,r9d
	shrd	r14d,r14d,9
	xor	r13d,r8d
	xor	r12d,r10d
	shrd	r13d,r13d,5
	xor	r14d,eax
	and	r12d,r8d
	xor	r13d,r8d
	add	r11d,DWORD[32+rsp]
	mov	r15d,eax
	xor	r12d,r10d
	shrd	r14d,r14d,11
	xor	r15d,ebx
	add	r11d,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,eax
	add	r11d,r13d
	xor	edi,ebx
	shrd	r14d,r14d,2
	add	edx,r11d
	add	r11d,edi
	mov	r13d,edx
	add	r14d,r11d
	shrd	r13d,r13d,14
	mov	r11d,r14d
	mov	r12d,r8d
	shrd	r14d,r14d,9
	xor	r13d,edx
	xor	r12d,r9d
	shrd	r13d,r13d,5
	xor	r14d,r11d
	and	r12d,edx
	xor	r13d,edx
	add	r10d,DWORD[36+rsp]
	mov	edi,r11d
	xor	r12d,r9d
	shrd	r14d,r14d,11
	xor	edi,eax
	add	r10d,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r11d
	add	r10d,r13d
	xor	r15d,eax
	shrd	r14d,r14d,2
	add	ecx,r10d
	add	r10d,r15d
	mov	r13d,ecx
	add	r14d,r10d
	shrd	r13d,r13d,14
	mov	r10d,r14d
	mov	r12d,edx
	shrd	r14d,r14d,9
	xor	r13d,ecx
	xor	r12d,r8d
	shrd	r13d,r13d,5
	xor	r14d,r10d
	and	r12d,ecx
	xor	r13d,ecx
	add	r9d,DWORD[40+rsp]
	mov	r15d,r10d
	xor	r12d,r8d
	shrd	r14d,r14d,11
	xor	r15d,r11d
	add	r9d,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,r10d
	add	r9d,r13d
	xor	edi,r11d
	shrd	r14d,r14d,2
	add	ebx,r9d
	add	r9d,edi
	mov	r13d,ebx
	add	r14d,r9d
	shrd	r13d,r13d,14
	mov	r9d,r14d
	mov	r12d,ecx
	shrd	r14d,r14d,9
	xor	r13d,ebx
	xor	r12d,edx
	shrd	r13d,r13d,5
	xor	r14d,r9d
	and	r12d,ebx
	xor	r13d,ebx
	add	r8d,DWORD[44+rsp]
	mov	edi,r9d
	xor	r12d,edx
	shrd	r14d,r14d,11
	xor	edi,r10d
	add	r8d,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,r9d
	add	r8d,r13d
	xor	r15d,r10d
	shrd	r14d,r14d,2
	add	eax,r8d
	add	r8d,r15d
	mov	r13d,eax
	add	r14d,r8d
	shrd	r13d,r13d,14
	mov	r8d,r14d
	mov	r12d,ebx
	shrd	r14d,r14d,9
	xor	r13d,eax
	xor	r12d,ecx
	shrd	r13d,r13d,5
	xor	r14d,r8d
	and	r12d,eax
	xor	r13d,eax
	add	edx,DWORD[48+rsp]
	mov	r15d,r8d
	xor	r12d,ecx
	shrd	r14d,r14d,11
	xor	r15d,r9d
	add	edx,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,r8d
	add	edx,r13d
	xor	edi,r9d
	shrd	r14d,r14d,2
	add	r11d,edx
	add	edx,edi
	mov	r13d,r11d
	add	r14d,edx
	shrd	r13d,r13d,14
	mov	edx,r14d
	mov	r12d,eax
	shrd	r14d,r14d,9
	xor	r13d,r11d
	xor	r12d,ebx
	shrd	r13d,r13d,5
	xor	r14d,edx
	and	r12d,r11d
	xor	r13d,r11d
	add	ecx,DWORD[52+rsp]
	mov	edi,edx
	xor	r12d,ebx
	shrd	r14d,r14d,11
	xor	edi,r8d
	add	ecx,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,edx
	add	ecx,r13d
	xor	r15d,r8d
	shrd	r14d,r14d,2
	add	r10d,ecx
	add	ecx,r15d
	mov	r13d,r10d
	add	r14d,ecx
	shrd	r13d,r13d,14
	mov	ecx,r14d
	mov	r12d,r11d
	shrd	r14d,r14d,9
	xor	r13d,r10d
	xor	r12d,eax
	shrd	r13d,r13d,5
	xor	r14d,ecx
	and	r12d,r10d
	xor	r13d,r10d
	add	ebx,DWORD[56+rsp]
	mov	r15d,ecx
	xor	r12d,eax
	shrd	r14d,r14d,11
	xor	r15d,edx
	add	ebx,r12d
	shrd	r13d,r13d,6
	and	edi,r15d
	xor	r14d,ecx
	add	ebx,r13d
	xor	edi,edx
	shrd	r14d,r14d,2
	add	r9d,ebx
	add	ebx,edi
	mov	r13d,r9d
	add	r14d,ebx
	shrd	r13d,r13d,14
	mov	ebx,r14d
	mov	r12d,r10d
	shrd	r14d,r14d,9
	xor	r13d,r9d
	xor	r12d,r11d
	shrd	r13d,r13d,5
	xor	r14d,ebx
	and	r12d,r9d
	xor	r13d,r9d
	add	eax,DWORD[60+rsp]
	mov	edi,ebx
	xor	r12d,r11d
	shrd	r14d,r14d,11
	xor	edi,ecx
	add	eax,r12d
	shrd	r13d,r13d,6
	and	r15d,edi
	xor	r14d,ebx
	add	eax,r13d
	xor	r15d,ecx
	shrd	r14d,r14d,2
	add	r8d,eax
	add	eax,r15d
	mov	r13d,r8d
	add	r14d,eax
	mov	rdi,QWORD[((64+0))+rsp]
	mov	eax,r14d

	add	eax,DWORD[rdi]
	lea	rsi,[64+rsi]
	add	ebx,DWORD[4+rdi]
	add	ecx,DWORD[8+rdi]
	add	edx,DWORD[12+rdi]
	add	r8d,DWORD[16+rdi]
	add	r9d,DWORD[20+rdi]
	add	r10d,DWORD[24+rdi]
	add	r11d,DWORD[28+rdi]

	cmp	rsi,QWORD[((64+16))+rsp]

	mov	DWORD[rdi],eax
	mov	DWORD[4+rdi],ebx
	mov	DWORD[8+rdi],ecx
	mov	DWORD[12+rdi],edx
	mov	DWORD[16+rdi],r8d
	mov	DWORD[20+rdi],r9d
	mov	DWORD[24+rdi],r10d
	mov	DWORD[28+rdi],r11d
	jb	NEAR $L$loop_avx

	mov	rsi,QWORD[88+rsp]

	vzeroupper
	movaps	xmm6,XMMWORD[((64+32))+rsp]
	movaps	xmm7,XMMWORD[((64+48))+rsp]
	movaps	xmm8,XMMWORD[((64+64))+rsp]
	movaps	xmm9,XMMWORD[((64+80))+rsp]
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
SEH64_ENDPROC_FRAME	sha256_block_data_order_avx
$L$SEH_end_sha256_block_data_order_avx:

ALIGN	64
SEH64_PROC_FRAME sha256_block_data_order_avx2,PRIVATE
	mov	QWORD[8+rsp],rdi	;WIN64 prologue
SEH64_SAVEREG rdi, 8
	mov	QWORD[16+rsp],rsi
SEH64_SAVEREG rsi, 16
	mov	rax,rsp
	mov	rdi,rcx
	mov	rsi,rdx
	mov	rdx,r8



$L$avx2_shortcut:
	mov	rax,rsp

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
	sub	rsp,608
	shl	rdx,4
	and	rsp,-256*4
	lea	rdx,[rdx*4+rsi]
	add	rsp,448
	mov	QWORD[((64+0))+rsp],rdi
	mov	QWORD[((64+8))+rsp],rsi
	mov	QWORD[((64+16))+rsp],rdx
	mov	QWORD[88+rsp],rax

	movaps	XMMWORD[(64+32)+rsp],xmm6
	movaps	XMMWORD[(64+48)+rsp],xmm7
	movaps	XMMWORD[(64+64)+rsp],xmm8
	movaps	XMMWORD[(64+80)+rsp],xmm9
; Hack using rax to load saved CFA expression: 88(%rsp),deref,+8
SEH64_SETFRAME rax, 48	; Unwind basically does: rsp = rax - offset
SEH64_PUSHREG rax    	; Unwind will load deref'ed value into volatile reg for SET_FRAME above.
SEH64_ALLOCSTACK 88	; Can only deref by .PUSHREG, so adjust RSP by feigning stack alloc.
SEH64_ENDPROLOG	; cfa_reg=%rax cfa_reg_offset= cfa_rsp=-56

	vzeroupper
	sub	rsi,-16*4
	mov	eax,DWORD[rdi]
	mov	r12,rsi
	mov	ebx,DWORD[4+rdi]
	cmp	rsi,rdx
	mov	ecx,DWORD[8+rdi]
	cmove	r12,rsp
	mov	edx,DWORD[12+rdi]
	mov	r8d,DWORD[16+rdi]
	mov	r9d,DWORD[20+rdi]
	mov	r10d,DWORD[24+rdi]
	mov	r11d,DWORD[28+rdi]
	vmovdqa	ymm8,YMMWORD[((K256+512+32))]
	vmovdqa	ymm9,YMMWORD[((K256+512+64))]
	jmp	NEAR $L$oop_avx2
ALIGN	16
$L$oop_avx2:
	vmovdqa	ymm7,YMMWORD[((K256+512))]
	vmovdqu	xmm0,XMMWORD[((-64+0))+rsi]
	vmovdqu	xmm1,XMMWORD[((-64+16))+rsi]
	vmovdqu	xmm2,XMMWORD[((-64+32))+rsi]
	vmovdqu	xmm3,XMMWORD[((-64+48))+rsi]

	vinserti128	ymm0,ymm0,XMMWORD[r12],1
	vinserti128	ymm1,ymm1,XMMWORD[16+r12],1
	vpshufb	ymm0,ymm0,ymm7
	vinserti128	ymm2,ymm2,XMMWORD[32+r12],1
	vpshufb	ymm1,ymm1,ymm7
	vinserti128	ymm3,ymm3,XMMWORD[48+r12],1

	lea	rbp,[K256]
	vpshufb	ymm2,ymm2,ymm7
	vpaddd	ymm4,ymm0,YMMWORD[rbp]
	vpshufb	ymm3,ymm3,ymm7
	vpaddd	ymm5,ymm1,YMMWORD[32+rbp]
	vpaddd	ymm6,ymm2,YMMWORD[64+rbp]
	vpaddd	ymm7,ymm3,YMMWORD[96+rbp]
	vmovdqa	YMMWORD[rsp],ymm4
	xor	r14d,r14d
	vmovdqa	YMMWORD[32+rsp],ymm5
	lea	rsp,[((-64))+rsp]
	mov	edi,ebx
	vmovdqa	YMMWORD[rsp],ymm6
	xor	edi,ecx
	vmovdqa	YMMWORD[32+rsp],ymm7
	mov	r12d,r9d
	sub	rbp,-16*2*4
	jmp	NEAR $L$avx2_00_47

ALIGN	16
$L$avx2_00_47:
	lea	rsp,[((-64))+rsp]
	vpalignr	ymm4,ymm1,ymm0,4
	add	r11d,DWORD[((0+128))+rsp]
	and	r12d,r8d
	rorx	r13d,r8d,25
	vpalignr	ymm7,ymm3,ymm2,4
	rorx	r15d,r8d,11
	lea	eax,[r14*1+rax]
	lea	r11d,[r12*1+r11]
	vpsrld	ymm6,ymm4,7
	andn	r12d,r8d,r10d
	xor	r13d,r15d
	rorx	r14d,r8d,6
	vpaddd	ymm0,ymm0,ymm7
	lea	r11d,[r12*1+r11]
	xor	r13d,r14d
	mov	r15d,eax
	vpsrld	ymm7,ymm4,3
	rorx	r12d,eax,22
	lea	r11d,[r13*1+r11]
	xor	r15d,ebx
	vpslld	ymm5,ymm4,14
	rorx	r14d,eax,13
	rorx	r13d,eax,2
	lea	edx,[r11*1+rdx]
	vpxor	ymm4,ymm7,ymm6
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,ebx
	vpshufd	ymm7,ymm3,250
	xor	r14d,r13d
	lea	r11d,[rdi*1+r11]
	mov	r12d,r8d
	vpsrld	ymm6,ymm6,11
	add	r10d,DWORD[((4+128))+rsp]
	and	r12d,edx
	rorx	r13d,edx,25
	vpxor	ymm4,ymm4,ymm5
	rorx	edi,edx,11
	lea	r11d,[r14*1+r11]
	lea	r10d,[r12*1+r10]
	vpslld	ymm5,ymm5,11
	andn	r12d,edx,r9d
	xor	r13d,edi
	rorx	r14d,edx,6
	vpxor	ymm4,ymm4,ymm6
	lea	r10d,[r12*1+r10]
	xor	r13d,r14d
	mov	edi,r11d
	vpsrld	ymm6,ymm7,10
	rorx	r12d,r11d,22
	lea	r10d,[r13*1+r10]
	xor	edi,eax
	vpxor	ymm4,ymm4,ymm5
	rorx	r14d,r11d,13
	rorx	r13d,r11d,2
	lea	ecx,[r10*1+rcx]
	vpsrlq	ymm7,ymm7,17
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,eax
	vpaddd	ymm0,ymm0,ymm4
	xor	r14d,r13d
	lea	r10d,[r15*1+r10]
	mov	r12d,edx
	vpxor	ymm6,ymm6,ymm7
	add	r9d,DWORD[((8+128))+rsp]
	and	r12d,ecx
	rorx	r13d,ecx,25
	vpsrlq	ymm7,ymm7,2
	rorx	r15d,ecx,11
	lea	r10d,[r14*1+r10]
	lea	r9d,[r12*1+r9]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,ecx,r8d
	xor	r13d,r15d
	rorx	r14d,ecx,6
	vpshufb	ymm6,ymm6,ymm8
	lea	r9d,[r12*1+r9]
	xor	r13d,r14d
	mov	r15d,r10d
	vpaddd	ymm0,ymm0,ymm6
	rorx	r12d,r10d,22
	lea	r9d,[r13*1+r9]
	xor	r15d,r11d
	vpshufd	ymm7,ymm0,80
	rorx	r14d,r10d,13
	rorx	r13d,r10d,2
	lea	ebx,[r9*1+rbx]
	vpsrld	ymm6,ymm7,10
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r11d
	vpsrlq	ymm7,ymm7,17
	xor	r14d,r13d
	lea	r9d,[rdi*1+r9]
	mov	r12d,ecx
	vpxor	ymm6,ymm6,ymm7
	add	r8d,DWORD[((12+128))+rsp]
	and	r12d,ebx
	rorx	r13d,ebx,25
	vpsrlq	ymm7,ymm7,2
	rorx	edi,ebx,11
	lea	r9d,[r14*1+r9]
	lea	r8d,[r12*1+r8]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,ebx,edx
	xor	r13d,edi
	rorx	r14d,ebx,6
	vpshufb	ymm6,ymm6,ymm9
	lea	r8d,[r12*1+r8]
	xor	r13d,r14d
	mov	edi,r9d
	vpaddd	ymm0,ymm0,ymm6
	rorx	r12d,r9d,22
	lea	r8d,[r13*1+r8]
	xor	edi,r10d
	vpaddd	ymm6,ymm0,YMMWORD[rbp]
	rorx	r14d,r9d,13
	rorx	r13d,r9d,2
	lea	eax,[r8*1+rax]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r10d
	xor	r14d,r13d
	lea	r8d,[r15*1+r8]
	mov	r12d,ebx
	vmovdqa	YMMWORD[rsp],ymm6
	vpalignr	ymm4,ymm2,ymm1,4
	add	edx,DWORD[((32+128))+rsp]
	and	r12d,eax
	rorx	r13d,eax,25
	vpalignr	ymm7,ymm0,ymm3,4
	rorx	r15d,eax,11
	lea	r8d,[r14*1+r8]
	lea	edx,[r12*1+rdx]
	vpsrld	ymm6,ymm4,7
	andn	r12d,eax,ecx
	xor	r13d,r15d
	rorx	r14d,eax,6
	vpaddd	ymm1,ymm1,ymm7
	lea	edx,[r12*1+rdx]
	xor	r13d,r14d
	mov	r15d,r8d
	vpsrld	ymm7,ymm4,3
	rorx	r12d,r8d,22
	lea	edx,[r13*1+rdx]
	xor	r15d,r9d
	vpslld	ymm5,ymm4,14
	rorx	r14d,r8d,13
	rorx	r13d,r8d,2
	lea	r11d,[rdx*1+r11]
	vpxor	ymm4,ymm7,ymm6
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r9d
	vpshufd	ymm7,ymm0,250
	xor	r14d,r13d
	lea	edx,[rdi*1+rdx]
	mov	r12d,eax
	vpsrld	ymm6,ymm6,11
	add	ecx,DWORD[((36+128))+rsp]
	and	r12d,r11d
	rorx	r13d,r11d,25
	vpxor	ymm4,ymm4,ymm5
	rorx	edi,r11d,11
	lea	edx,[r14*1+rdx]
	lea	ecx,[r12*1+rcx]
	vpslld	ymm5,ymm5,11
	andn	r12d,r11d,ebx
	xor	r13d,edi
	rorx	r14d,r11d,6
	vpxor	ymm4,ymm4,ymm6
	lea	ecx,[r12*1+rcx]
	xor	r13d,r14d
	mov	edi,edx
	vpsrld	ymm6,ymm7,10
	rorx	r12d,edx,22
	lea	ecx,[r13*1+rcx]
	xor	edi,r8d
	vpxor	ymm4,ymm4,ymm5
	rorx	r14d,edx,13
	rorx	r13d,edx,2
	lea	r10d,[rcx*1+r10]
	vpsrlq	ymm7,ymm7,17
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r8d
	vpaddd	ymm1,ymm1,ymm4
	xor	r14d,r13d
	lea	ecx,[r15*1+rcx]
	mov	r12d,r11d
	vpxor	ymm6,ymm6,ymm7
	add	ebx,DWORD[((40+128))+rsp]
	and	r12d,r10d
	rorx	r13d,r10d,25
	vpsrlq	ymm7,ymm7,2
	rorx	r15d,r10d,11
	lea	ecx,[r14*1+rcx]
	lea	ebx,[r12*1+rbx]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,r10d,eax
	xor	r13d,r15d
	rorx	r14d,r10d,6
	vpshufb	ymm6,ymm6,ymm8
	lea	ebx,[r12*1+rbx]
	xor	r13d,r14d
	mov	r15d,ecx
	vpaddd	ymm1,ymm1,ymm6
	rorx	r12d,ecx,22
	lea	ebx,[r13*1+rbx]
	xor	r15d,edx
	vpshufd	ymm7,ymm1,80
	rorx	r14d,ecx,13
	rorx	r13d,ecx,2
	lea	r9d,[rbx*1+r9]
	vpsrld	ymm6,ymm7,10
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,edx
	vpsrlq	ymm7,ymm7,17
	xor	r14d,r13d
	lea	ebx,[rdi*1+rbx]
	mov	r12d,r10d
	vpxor	ymm6,ymm6,ymm7
	add	eax,DWORD[((44+128))+rsp]
	and	r12d,r9d
	rorx	r13d,r9d,25
	vpsrlq	ymm7,ymm7,2
	rorx	edi,r9d,11
	lea	ebx,[r14*1+rbx]
	lea	eax,[r12*1+rax]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,r9d,r11d
	xor	r13d,edi
	rorx	r14d,r9d,6
	vpshufb	ymm6,ymm6,ymm9
	lea	eax,[r12*1+rax]
	xor	r13d,r14d
	mov	edi,ebx
	vpaddd	ymm1,ymm1,ymm6
	rorx	r12d,ebx,22
	lea	eax,[r13*1+rax]
	xor	edi,ecx
	vpaddd	ymm6,ymm1,YMMWORD[32+rbp]
	rorx	r14d,ebx,13
	rorx	r13d,ebx,2
	lea	r8d,[rax*1+r8]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,ecx
	xor	r14d,r13d
	lea	eax,[r15*1+rax]
	mov	r12d,r9d
	vmovdqa	YMMWORD[32+rsp],ymm6
	lea	rsp,[((-64))+rsp]
	vpalignr	ymm4,ymm3,ymm2,4
	add	r11d,DWORD[((0+128))+rsp]
	and	r12d,r8d
	rorx	r13d,r8d,25
	vpalignr	ymm7,ymm1,ymm0,4
	rorx	r15d,r8d,11
	lea	eax,[r14*1+rax]
	lea	r11d,[r12*1+r11]
	vpsrld	ymm6,ymm4,7
	andn	r12d,r8d,r10d
	xor	r13d,r15d
	rorx	r14d,r8d,6
	vpaddd	ymm2,ymm2,ymm7
	lea	r11d,[r12*1+r11]
	xor	r13d,r14d
	mov	r15d,eax
	vpsrld	ymm7,ymm4,3
	rorx	r12d,eax,22
	lea	r11d,[r13*1+r11]
	xor	r15d,ebx
	vpslld	ymm5,ymm4,14
	rorx	r14d,eax,13
	rorx	r13d,eax,2
	lea	edx,[r11*1+rdx]
	vpxor	ymm4,ymm7,ymm6
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,ebx
	vpshufd	ymm7,ymm1,250
	xor	r14d,r13d
	lea	r11d,[rdi*1+r11]
	mov	r12d,r8d
	vpsrld	ymm6,ymm6,11
	add	r10d,DWORD[((4+128))+rsp]
	and	r12d,edx
	rorx	r13d,edx,25
	vpxor	ymm4,ymm4,ymm5
	rorx	edi,edx,11
	lea	r11d,[r14*1+r11]
	lea	r10d,[r12*1+r10]
	vpslld	ymm5,ymm5,11
	andn	r12d,edx,r9d
	xor	r13d,edi
	rorx	r14d,edx,6
	vpxor	ymm4,ymm4,ymm6
	lea	r10d,[r12*1+r10]
	xor	r13d,r14d
	mov	edi,r11d
	vpsrld	ymm6,ymm7,10
	rorx	r12d,r11d,22
	lea	r10d,[r13*1+r10]
	xor	edi,eax
	vpxor	ymm4,ymm4,ymm5
	rorx	r14d,r11d,13
	rorx	r13d,r11d,2
	lea	ecx,[r10*1+rcx]
	vpsrlq	ymm7,ymm7,17
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,eax
	vpaddd	ymm2,ymm2,ymm4
	xor	r14d,r13d
	lea	r10d,[r15*1+r10]
	mov	r12d,edx
	vpxor	ymm6,ymm6,ymm7
	add	r9d,DWORD[((8+128))+rsp]
	and	r12d,ecx
	rorx	r13d,ecx,25
	vpsrlq	ymm7,ymm7,2
	rorx	r15d,ecx,11
	lea	r10d,[r14*1+r10]
	lea	r9d,[r12*1+r9]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,ecx,r8d
	xor	r13d,r15d
	rorx	r14d,ecx,6
	vpshufb	ymm6,ymm6,ymm8
	lea	r9d,[r12*1+r9]
	xor	r13d,r14d
	mov	r15d,r10d
	vpaddd	ymm2,ymm2,ymm6
	rorx	r12d,r10d,22
	lea	r9d,[r13*1+r9]
	xor	r15d,r11d
	vpshufd	ymm7,ymm2,80
	rorx	r14d,r10d,13
	rorx	r13d,r10d,2
	lea	ebx,[r9*1+rbx]
	vpsrld	ymm6,ymm7,10
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r11d
	vpsrlq	ymm7,ymm7,17
	xor	r14d,r13d
	lea	r9d,[rdi*1+r9]
	mov	r12d,ecx
	vpxor	ymm6,ymm6,ymm7
	add	r8d,DWORD[((12+128))+rsp]
	and	r12d,ebx
	rorx	r13d,ebx,25
	vpsrlq	ymm7,ymm7,2
	rorx	edi,ebx,11
	lea	r9d,[r14*1+r9]
	lea	r8d,[r12*1+r8]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,ebx,edx
	xor	r13d,edi
	rorx	r14d,ebx,6
	vpshufb	ymm6,ymm6,ymm9
	lea	r8d,[r12*1+r8]
	xor	r13d,r14d
	mov	edi,r9d
	vpaddd	ymm2,ymm2,ymm6
	rorx	r12d,r9d,22
	lea	r8d,[r13*1+r8]
	xor	edi,r10d
	vpaddd	ymm6,ymm2,YMMWORD[64+rbp]
	rorx	r14d,r9d,13
	rorx	r13d,r9d,2
	lea	eax,[r8*1+rax]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r10d
	xor	r14d,r13d
	lea	r8d,[r15*1+r8]
	mov	r12d,ebx
	vmovdqa	YMMWORD[rsp],ymm6
	vpalignr	ymm4,ymm0,ymm3,4
	add	edx,DWORD[((32+128))+rsp]
	and	r12d,eax
	rorx	r13d,eax,25
	vpalignr	ymm7,ymm2,ymm1,4
	rorx	r15d,eax,11
	lea	r8d,[r14*1+r8]
	lea	edx,[r12*1+rdx]
	vpsrld	ymm6,ymm4,7
	andn	r12d,eax,ecx
	xor	r13d,r15d
	rorx	r14d,eax,6
	vpaddd	ymm3,ymm3,ymm7
	lea	edx,[r12*1+rdx]
	xor	r13d,r14d
	mov	r15d,r8d
	vpsrld	ymm7,ymm4,3
	rorx	r12d,r8d,22
	lea	edx,[r13*1+rdx]
	xor	r15d,r9d
	vpslld	ymm5,ymm4,14
	rorx	r14d,r8d,13
	rorx	r13d,r8d,2
	lea	r11d,[rdx*1+r11]
	vpxor	ymm4,ymm7,ymm6
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r9d
	vpshufd	ymm7,ymm2,250
	xor	r14d,r13d
	lea	edx,[rdi*1+rdx]
	mov	r12d,eax
	vpsrld	ymm6,ymm6,11
	add	ecx,DWORD[((36+128))+rsp]
	and	r12d,r11d
	rorx	r13d,r11d,25
	vpxor	ymm4,ymm4,ymm5
	rorx	edi,r11d,11
	lea	edx,[r14*1+rdx]
	lea	ecx,[r12*1+rcx]
	vpslld	ymm5,ymm5,11
	andn	r12d,r11d,ebx
	xor	r13d,edi
	rorx	r14d,r11d,6
	vpxor	ymm4,ymm4,ymm6
	lea	ecx,[r12*1+rcx]
	xor	r13d,r14d
	mov	edi,edx
	vpsrld	ymm6,ymm7,10
	rorx	r12d,edx,22
	lea	ecx,[r13*1+rcx]
	xor	edi,r8d
	vpxor	ymm4,ymm4,ymm5
	rorx	r14d,edx,13
	rorx	r13d,edx,2
	lea	r10d,[rcx*1+r10]
	vpsrlq	ymm7,ymm7,17
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r8d
	vpaddd	ymm3,ymm3,ymm4
	xor	r14d,r13d
	lea	ecx,[r15*1+rcx]
	mov	r12d,r11d
	vpxor	ymm6,ymm6,ymm7
	add	ebx,DWORD[((40+128))+rsp]
	and	r12d,r10d
	rorx	r13d,r10d,25
	vpsrlq	ymm7,ymm7,2
	rorx	r15d,r10d,11
	lea	ecx,[r14*1+rcx]
	lea	ebx,[r12*1+rbx]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,r10d,eax
	xor	r13d,r15d
	rorx	r14d,r10d,6
	vpshufb	ymm6,ymm6,ymm8
	lea	ebx,[r12*1+rbx]
	xor	r13d,r14d
	mov	r15d,ecx
	vpaddd	ymm3,ymm3,ymm6
	rorx	r12d,ecx,22
	lea	ebx,[r13*1+rbx]
	xor	r15d,edx
	vpshufd	ymm7,ymm3,80
	rorx	r14d,ecx,13
	rorx	r13d,ecx,2
	lea	r9d,[rbx*1+r9]
	vpsrld	ymm6,ymm7,10
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,edx
	vpsrlq	ymm7,ymm7,17
	xor	r14d,r13d
	lea	ebx,[rdi*1+rbx]
	mov	r12d,r10d
	vpxor	ymm6,ymm6,ymm7
	add	eax,DWORD[((44+128))+rsp]
	and	r12d,r9d
	rorx	r13d,r9d,25
	vpsrlq	ymm7,ymm7,2
	rorx	edi,r9d,11
	lea	ebx,[r14*1+rbx]
	lea	eax,[r12*1+rax]
	vpxor	ymm6,ymm6,ymm7
	andn	r12d,r9d,r11d
	xor	r13d,edi
	rorx	r14d,r9d,6
	vpshufb	ymm6,ymm6,ymm9
	lea	eax,[r12*1+rax]
	xor	r13d,r14d
	mov	edi,ebx
	vpaddd	ymm3,ymm3,ymm6
	rorx	r12d,ebx,22
	lea	eax,[r13*1+rax]
	xor	edi,ecx
	vpaddd	ymm6,ymm3,YMMWORD[96+rbp]
	rorx	r14d,ebx,13
	rorx	r13d,ebx,2
	lea	r8d,[rax*1+r8]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,ecx
	xor	r14d,r13d
	lea	eax,[r15*1+rax]
	mov	r12d,r9d
	vmovdqa	YMMWORD[32+rsp],ymm6
	lea	rbp,[128+rbp]
	cmp	BYTE[3+rbp],0
	jne	NEAR $L$avx2_00_47
	add	r11d,DWORD[((0+64))+rsp]
	and	r12d,r8d
	rorx	r13d,r8d,25
	rorx	r15d,r8d,11
	lea	eax,[r14*1+rax]
	lea	r11d,[r12*1+r11]
	andn	r12d,r8d,r10d
	xor	r13d,r15d
	rorx	r14d,r8d,6
	lea	r11d,[r12*1+r11]
	xor	r13d,r14d
	mov	r15d,eax
	rorx	r12d,eax,22
	lea	r11d,[r13*1+r11]
	xor	r15d,ebx
	rorx	r14d,eax,13
	rorx	r13d,eax,2
	lea	edx,[r11*1+rdx]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,ebx
	xor	r14d,r13d
	lea	r11d,[rdi*1+r11]
	mov	r12d,r8d
	add	r10d,DWORD[((4+64))+rsp]
	and	r12d,edx
	rorx	r13d,edx,25
	rorx	edi,edx,11
	lea	r11d,[r14*1+r11]
	lea	r10d,[r12*1+r10]
	andn	r12d,edx,r9d
	xor	r13d,edi
	rorx	r14d,edx,6
	lea	r10d,[r12*1+r10]
	xor	r13d,r14d
	mov	edi,r11d
	rorx	r12d,r11d,22
	lea	r10d,[r13*1+r10]
	xor	edi,eax
	rorx	r14d,r11d,13
	rorx	r13d,r11d,2
	lea	ecx,[r10*1+rcx]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,eax
	xor	r14d,r13d
	lea	r10d,[r15*1+r10]
	mov	r12d,edx
	add	r9d,DWORD[((8+64))+rsp]
	and	r12d,ecx
	rorx	r13d,ecx,25
	rorx	r15d,ecx,11
	lea	r10d,[r14*1+r10]
	lea	r9d,[r12*1+r9]
	andn	r12d,ecx,r8d
	xor	r13d,r15d
	rorx	r14d,ecx,6
	lea	r9d,[r12*1+r9]
	xor	r13d,r14d
	mov	r15d,r10d
	rorx	r12d,r10d,22
	lea	r9d,[r13*1+r9]
	xor	r15d,r11d
	rorx	r14d,r10d,13
	rorx	r13d,r10d,2
	lea	ebx,[r9*1+rbx]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r11d
	xor	r14d,r13d
	lea	r9d,[rdi*1+r9]
	mov	r12d,ecx
	add	r8d,DWORD[((12+64))+rsp]
	and	r12d,ebx
	rorx	r13d,ebx,25
	rorx	edi,ebx,11
	lea	r9d,[r14*1+r9]
	lea	r8d,[r12*1+r8]
	andn	r12d,ebx,edx
	xor	r13d,edi
	rorx	r14d,ebx,6
	lea	r8d,[r12*1+r8]
	xor	r13d,r14d
	mov	edi,r9d
	rorx	r12d,r9d,22
	lea	r8d,[r13*1+r8]
	xor	edi,r10d
	rorx	r14d,r9d,13
	rorx	r13d,r9d,2
	lea	eax,[r8*1+rax]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r10d
	xor	r14d,r13d
	lea	r8d,[r15*1+r8]
	mov	r12d,ebx
	add	edx,DWORD[((32+64))+rsp]
	and	r12d,eax
	rorx	r13d,eax,25
	rorx	r15d,eax,11
	lea	r8d,[r14*1+r8]
	lea	edx,[r12*1+rdx]
	andn	r12d,eax,ecx
	xor	r13d,r15d
	rorx	r14d,eax,6
	lea	edx,[r12*1+rdx]
	xor	r13d,r14d
	mov	r15d,r8d
	rorx	r12d,r8d,22
	lea	edx,[r13*1+rdx]
	xor	r15d,r9d
	rorx	r14d,r8d,13
	rorx	r13d,r8d,2
	lea	r11d,[rdx*1+r11]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r9d
	xor	r14d,r13d
	lea	edx,[rdi*1+rdx]
	mov	r12d,eax
	add	ecx,DWORD[((36+64))+rsp]
	and	r12d,r11d
	rorx	r13d,r11d,25
	rorx	edi,r11d,11
	lea	edx,[r14*1+rdx]
	lea	ecx,[r12*1+rcx]
	andn	r12d,r11d,ebx
	xor	r13d,edi
	rorx	r14d,r11d,6
	lea	ecx,[r12*1+rcx]
	xor	r13d,r14d
	mov	edi,edx
	rorx	r12d,edx,22
	lea	ecx,[r13*1+rcx]
	xor	edi,r8d
	rorx	r14d,edx,13
	rorx	r13d,edx,2
	lea	r10d,[rcx*1+r10]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r8d
	xor	r14d,r13d
	lea	ecx,[r15*1+rcx]
	mov	r12d,r11d
	add	ebx,DWORD[((40+64))+rsp]
	and	r12d,r10d
	rorx	r13d,r10d,25
	rorx	r15d,r10d,11
	lea	ecx,[r14*1+rcx]
	lea	ebx,[r12*1+rbx]
	andn	r12d,r10d,eax
	xor	r13d,r15d
	rorx	r14d,r10d,6
	lea	ebx,[r12*1+rbx]
	xor	r13d,r14d
	mov	r15d,ecx
	rorx	r12d,ecx,22
	lea	ebx,[r13*1+rbx]
	xor	r15d,edx
	rorx	r14d,ecx,13
	rorx	r13d,ecx,2
	lea	r9d,[rbx*1+r9]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,edx
	xor	r14d,r13d
	lea	ebx,[rdi*1+rbx]
	mov	r12d,r10d
	add	eax,DWORD[((44+64))+rsp]
	and	r12d,r9d
	rorx	r13d,r9d,25
	rorx	edi,r9d,11
	lea	ebx,[r14*1+rbx]
	lea	eax,[r12*1+rax]
	andn	r12d,r9d,r11d
	xor	r13d,edi
	rorx	r14d,r9d,6
	lea	eax,[r12*1+rax]
	xor	r13d,r14d
	mov	edi,ebx
	rorx	r12d,ebx,22
	lea	eax,[r13*1+rax]
	xor	edi,ecx
	rorx	r14d,ebx,13
	rorx	r13d,ebx,2
	lea	r8d,[rax*1+r8]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,ecx
	xor	r14d,r13d
	lea	eax,[r15*1+rax]
	mov	r12d,r9d
	add	r11d,DWORD[rsp]
	and	r12d,r8d
	rorx	r13d,r8d,25
	rorx	r15d,r8d,11
	lea	eax,[r14*1+rax]
	lea	r11d,[r12*1+r11]
	andn	r12d,r8d,r10d
	xor	r13d,r15d
	rorx	r14d,r8d,6
	lea	r11d,[r12*1+r11]
	xor	r13d,r14d
	mov	r15d,eax
	rorx	r12d,eax,22
	lea	r11d,[r13*1+r11]
	xor	r15d,ebx
	rorx	r14d,eax,13
	rorx	r13d,eax,2
	lea	edx,[r11*1+rdx]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,ebx
	xor	r14d,r13d
	lea	r11d,[rdi*1+r11]
	mov	r12d,r8d
	add	r10d,DWORD[4+rsp]
	and	r12d,edx
	rorx	r13d,edx,25
	rorx	edi,edx,11
	lea	r11d,[r14*1+r11]
	lea	r10d,[r12*1+r10]
	andn	r12d,edx,r9d
	xor	r13d,edi
	rorx	r14d,edx,6
	lea	r10d,[r12*1+r10]
	xor	r13d,r14d
	mov	edi,r11d
	rorx	r12d,r11d,22
	lea	r10d,[r13*1+r10]
	xor	edi,eax
	rorx	r14d,r11d,13
	rorx	r13d,r11d,2
	lea	ecx,[r10*1+rcx]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,eax
	xor	r14d,r13d
	lea	r10d,[r15*1+r10]
	mov	r12d,edx
	add	r9d,DWORD[8+rsp]
	and	r12d,ecx
	rorx	r13d,ecx,25
	rorx	r15d,ecx,11
	lea	r10d,[r14*1+r10]
	lea	r9d,[r12*1+r9]
	andn	r12d,ecx,r8d
	xor	r13d,r15d
	rorx	r14d,ecx,6
	lea	r9d,[r12*1+r9]
	xor	r13d,r14d
	mov	r15d,r10d
	rorx	r12d,r10d,22
	lea	r9d,[r13*1+r9]
	xor	r15d,r11d
	rorx	r14d,r10d,13
	rorx	r13d,r10d,2
	lea	ebx,[r9*1+rbx]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r11d
	xor	r14d,r13d
	lea	r9d,[rdi*1+r9]
	mov	r12d,ecx
	add	r8d,DWORD[12+rsp]
	and	r12d,ebx
	rorx	r13d,ebx,25
	rorx	edi,ebx,11
	lea	r9d,[r14*1+r9]
	lea	r8d,[r12*1+r8]
	andn	r12d,ebx,edx
	xor	r13d,edi
	rorx	r14d,ebx,6
	lea	r8d,[r12*1+r8]
	xor	r13d,r14d
	mov	edi,r9d
	rorx	r12d,r9d,22
	lea	r8d,[r13*1+r8]
	xor	edi,r10d
	rorx	r14d,r9d,13
	rorx	r13d,r9d,2
	lea	eax,[r8*1+rax]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r10d
	xor	r14d,r13d
	lea	r8d,[r15*1+r8]
	mov	r12d,ebx
	add	edx,DWORD[32+rsp]
	and	r12d,eax
	rorx	r13d,eax,25
	rorx	r15d,eax,11
	lea	r8d,[r14*1+r8]
	lea	edx,[r12*1+rdx]
	andn	r12d,eax,ecx
	xor	r13d,r15d
	rorx	r14d,eax,6
	lea	edx,[r12*1+rdx]
	xor	r13d,r14d
	mov	r15d,r8d
	rorx	r12d,r8d,22
	lea	edx,[r13*1+rdx]
	xor	r15d,r9d
	rorx	r14d,r8d,13
	rorx	r13d,r8d,2
	lea	r11d,[rdx*1+r11]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r9d
	xor	r14d,r13d
	lea	edx,[rdi*1+rdx]
	mov	r12d,eax
	add	ecx,DWORD[36+rsp]
	and	r12d,r11d
	rorx	r13d,r11d,25
	rorx	edi,r11d,11
	lea	edx,[r14*1+rdx]
	lea	ecx,[r12*1+rcx]
	andn	r12d,r11d,ebx
	xor	r13d,edi
	rorx	r14d,r11d,6
	lea	ecx,[r12*1+rcx]
	xor	r13d,r14d
	mov	edi,edx
	rorx	r12d,edx,22
	lea	ecx,[r13*1+rcx]
	xor	edi,r8d
	rorx	r14d,edx,13
	rorx	r13d,edx,2
	lea	r10d,[rcx*1+r10]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r8d
	xor	r14d,r13d
	lea	ecx,[r15*1+rcx]
	mov	r12d,r11d
	add	ebx,DWORD[40+rsp]
	and	r12d,r10d
	rorx	r13d,r10d,25
	rorx	r15d,r10d,11
	lea	ecx,[r14*1+rcx]
	lea	ebx,[r12*1+rbx]
	andn	r12d,r10d,eax
	xor	r13d,r15d
	rorx	r14d,r10d,6
	lea	ebx,[r12*1+rbx]
	xor	r13d,r14d
	mov	r15d,ecx
	rorx	r12d,ecx,22
	lea	ebx,[r13*1+rbx]
	xor	r15d,edx
	rorx	r14d,ecx,13
	rorx	r13d,ecx,2
	lea	r9d,[rbx*1+r9]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,edx
	xor	r14d,r13d
	lea	ebx,[rdi*1+rbx]
	mov	r12d,r10d
	add	eax,DWORD[44+rsp]
	and	r12d,r9d
	rorx	r13d,r9d,25
	rorx	edi,r9d,11
	lea	ebx,[r14*1+rbx]
	lea	eax,[r12*1+rax]
	andn	r12d,r9d,r11d
	xor	r13d,edi
	rorx	r14d,r9d,6
	lea	eax,[r12*1+rax]
	xor	r13d,r14d
	mov	edi,ebx
	rorx	r12d,ebx,22
	lea	eax,[r13*1+rax]
	xor	edi,ecx
	rorx	r14d,ebx,13
	rorx	r13d,ebx,2
	lea	r8d,[rax*1+r8]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,ecx
	xor	r14d,r13d
	lea	eax,[r15*1+rax]
	mov	r12d,r9d
	mov	rdi,QWORD[512+rsp]
	add	eax,r14d

	lea	rbp,[448+rsp]

	add	eax,DWORD[rdi]
	add	ebx,DWORD[4+rdi]
	add	ecx,DWORD[8+rdi]
	add	edx,DWORD[12+rdi]
	add	r8d,DWORD[16+rdi]
	add	r9d,DWORD[20+rdi]
	add	r10d,DWORD[24+rdi]
	add	r11d,DWORD[28+rdi]

	mov	DWORD[rdi],eax
	mov	DWORD[4+rdi],ebx
	mov	DWORD[8+rdi],ecx
	mov	DWORD[12+rdi],edx
	mov	DWORD[16+rdi],r8d
	mov	DWORD[20+rdi],r9d
	mov	DWORD[24+rdi],r10d
	mov	DWORD[28+rdi],r11d

	cmp	rsi,QWORD[80+rbp]
	je	NEAR $L$done_avx2

	xor	r14d,r14d
	mov	edi,ebx
	xor	edi,ecx
	mov	r12d,r9d
	jmp	NEAR $L$ower_avx2
ALIGN	16
$L$ower_avx2:
	add	r11d,DWORD[((0+16))+rbp]
	and	r12d,r8d
	rorx	r13d,r8d,25
	rorx	r15d,r8d,11
	lea	eax,[r14*1+rax]
	lea	r11d,[r12*1+r11]
	andn	r12d,r8d,r10d
	xor	r13d,r15d
	rorx	r14d,r8d,6
	lea	r11d,[r12*1+r11]
	xor	r13d,r14d
	mov	r15d,eax
	rorx	r12d,eax,22
	lea	r11d,[r13*1+r11]
	xor	r15d,ebx
	rorx	r14d,eax,13
	rorx	r13d,eax,2
	lea	edx,[r11*1+rdx]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,ebx
	xor	r14d,r13d
	lea	r11d,[rdi*1+r11]
	mov	r12d,r8d
	add	r10d,DWORD[((4+16))+rbp]
	and	r12d,edx
	rorx	r13d,edx,25
	rorx	edi,edx,11
	lea	r11d,[r14*1+r11]
	lea	r10d,[r12*1+r10]
	andn	r12d,edx,r9d
	xor	r13d,edi
	rorx	r14d,edx,6
	lea	r10d,[r12*1+r10]
	xor	r13d,r14d
	mov	edi,r11d
	rorx	r12d,r11d,22
	lea	r10d,[r13*1+r10]
	xor	edi,eax
	rorx	r14d,r11d,13
	rorx	r13d,r11d,2
	lea	ecx,[r10*1+rcx]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,eax
	xor	r14d,r13d
	lea	r10d,[r15*1+r10]
	mov	r12d,edx
	add	r9d,DWORD[((8+16))+rbp]
	and	r12d,ecx
	rorx	r13d,ecx,25
	rorx	r15d,ecx,11
	lea	r10d,[r14*1+r10]
	lea	r9d,[r12*1+r9]
	andn	r12d,ecx,r8d
	xor	r13d,r15d
	rorx	r14d,ecx,6
	lea	r9d,[r12*1+r9]
	xor	r13d,r14d
	mov	r15d,r10d
	rorx	r12d,r10d,22
	lea	r9d,[r13*1+r9]
	xor	r15d,r11d
	rorx	r14d,r10d,13
	rorx	r13d,r10d,2
	lea	ebx,[r9*1+rbx]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r11d
	xor	r14d,r13d
	lea	r9d,[rdi*1+r9]
	mov	r12d,ecx
	add	r8d,DWORD[((12+16))+rbp]
	and	r12d,ebx
	rorx	r13d,ebx,25
	rorx	edi,ebx,11
	lea	r9d,[r14*1+r9]
	lea	r8d,[r12*1+r8]
	andn	r12d,ebx,edx
	xor	r13d,edi
	rorx	r14d,ebx,6
	lea	r8d,[r12*1+r8]
	xor	r13d,r14d
	mov	edi,r9d
	rorx	r12d,r9d,22
	lea	r8d,[r13*1+r8]
	xor	edi,r10d
	rorx	r14d,r9d,13
	rorx	r13d,r9d,2
	lea	eax,[r8*1+rax]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r10d
	xor	r14d,r13d
	lea	r8d,[r15*1+r8]
	mov	r12d,ebx
	add	edx,DWORD[((32+16))+rbp]
	and	r12d,eax
	rorx	r13d,eax,25
	rorx	r15d,eax,11
	lea	r8d,[r14*1+r8]
	lea	edx,[r12*1+rdx]
	andn	r12d,eax,ecx
	xor	r13d,r15d
	rorx	r14d,eax,6
	lea	edx,[r12*1+rdx]
	xor	r13d,r14d
	mov	r15d,r8d
	rorx	r12d,r8d,22
	lea	edx,[r13*1+rdx]
	xor	r15d,r9d
	rorx	r14d,r8d,13
	rorx	r13d,r8d,2
	lea	r11d,[rdx*1+r11]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,r9d
	xor	r14d,r13d
	lea	edx,[rdi*1+rdx]
	mov	r12d,eax
	add	ecx,DWORD[((36+16))+rbp]
	and	r12d,r11d
	rorx	r13d,r11d,25
	rorx	edi,r11d,11
	lea	edx,[r14*1+rdx]
	lea	ecx,[r12*1+rcx]
	andn	r12d,r11d,ebx
	xor	r13d,edi
	rorx	r14d,r11d,6
	lea	ecx,[r12*1+rcx]
	xor	r13d,r14d
	mov	edi,edx
	rorx	r12d,edx,22
	lea	ecx,[r13*1+rcx]
	xor	edi,r8d
	rorx	r14d,edx,13
	rorx	r13d,edx,2
	lea	r10d,[rcx*1+r10]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,r8d
	xor	r14d,r13d
	lea	ecx,[r15*1+rcx]
	mov	r12d,r11d
	add	ebx,DWORD[((40+16))+rbp]
	and	r12d,r10d
	rorx	r13d,r10d,25
	rorx	r15d,r10d,11
	lea	ecx,[r14*1+rcx]
	lea	ebx,[r12*1+rbx]
	andn	r12d,r10d,eax
	xor	r13d,r15d
	rorx	r14d,r10d,6
	lea	ebx,[r12*1+rbx]
	xor	r13d,r14d
	mov	r15d,ecx
	rorx	r12d,ecx,22
	lea	ebx,[r13*1+rbx]
	xor	r15d,edx
	rorx	r14d,ecx,13
	rorx	r13d,ecx,2
	lea	r9d,[rbx*1+r9]
	and	edi,r15d
	xor	r14d,r12d
	xor	edi,edx
	xor	r14d,r13d
	lea	ebx,[rdi*1+rbx]
	mov	r12d,r10d
	add	eax,DWORD[((44+16))+rbp]
	and	r12d,r9d
	rorx	r13d,r9d,25
	rorx	edi,r9d,11
	lea	ebx,[r14*1+rbx]
	lea	eax,[r12*1+rax]
	andn	r12d,r9d,r11d
	xor	r13d,edi
	rorx	r14d,r9d,6
	lea	eax,[r12*1+rax]
	xor	r13d,r14d
	mov	edi,ebx
	rorx	r12d,ebx,22
	lea	eax,[r13*1+rax]
	xor	edi,ecx
	rorx	r14d,ebx,13
	rorx	r13d,ebx,2
	lea	r8d,[rax*1+r8]
	and	r15d,edi
	xor	r14d,r12d
	xor	r15d,ecx
	xor	r14d,r13d
	lea	eax,[r15*1+rax]
	mov	r12d,r9d
	lea	rbp,[((-64))+rbp]
	cmp	rbp,rsp
	jae	NEAR $L$ower_avx2

	mov	rdi,QWORD[512+rsp]
	add	eax,r14d

	lea	rsp,[448+rsp]



	add	eax,DWORD[rdi]
	add	ebx,DWORD[4+rdi]
	add	ecx,DWORD[8+rdi]
	add	edx,DWORD[12+rdi]
	add	r8d,DWORD[16+rdi]
	add	r9d,DWORD[20+rdi]
	lea	rsi,[128+rsi]
	add	r10d,DWORD[24+rdi]
	mov	r12,rsi
	add	r11d,DWORD[28+rdi]
	cmp	rsi,QWORD[((64+16))+rsp]

	mov	DWORD[rdi],eax
	cmove	r12,rsp
	mov	DWORD[4+rdi],ebx
	mov	DWORD[8+rdi],ecx
	mov	DWORD[12+rdi],edx
	mov	DWORD[16+rdi],r8d
	mov	DWORD[20+rdi],r9d
	mov	DWORD[24+rdi],r10d
	mov	DWORD[28+rdi],r11d

	jbe	NEAR $L$oop_avx2
	lea	rbp,[rsp]




$L$done_avx2:
	mov	rsi,QWORD[88+rbp]

	vzeroupper
	movaps	xmm6,XMMWORD[((64+32))+rbp]
	movaps	xmm7,XMMWORD[((64+48))+rbp]
	movaps	xmm8,XMMWORD[((64+64))+rbp]
	movaps	xmm9,XMMWORD[((64+80))+rbp]
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
SEH64_ENDPROC_FRAME	sha256_block_data_order_avx2
$L$SEH_end_sha256_block_data_order_avx2:
