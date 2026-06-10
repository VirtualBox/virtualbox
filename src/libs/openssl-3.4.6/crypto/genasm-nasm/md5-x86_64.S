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

global	ossl_md5_block_asm_data_order

SEH64_PROC_FRAME ossl_md5_block_asm_data_order,PUBLIC
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
	push	r14
SEH64_PUSHREG	r14	; cfa_rsp,cfa_reg_offset => -40,40 cfa_reg=%rsp
	push	r15
SEH64_PUSHREG	r15	; cfa_rsp,cfa_reg_offset => -48,48 cfa_reg=%rsp
	; cfa_rsp=-48
SEH64_ENDPROLOG	; cfa_reg=%rsp cfa_reg_offset=48 cfa_rsp=-48




	mov	rbp,rdi
	shl	rdx,6
	lea	rdi,[rdx*1+rsi]
	mov	eax,DWORD[rbp]
	mov	ebx,DWORD[4+rbp]
	mov	ecx,DWORD[8+rbp]
	mov	edx,DWORD[12+rbp]







	cmp	rsi,rdi
	je	NEAR $L$end


$L$loop:
	mov	r8d,eax
	mov	r9d,ebx
	mov	r14d,ecx
	mov	r15d,edx
	mov	r10d,DWORD[rsi]
	mov	r11d,edx
	xor	r11d,ecx
	lea	eax,[((-680876936))+r10*1+rax]
	and	r11d,ebx
	mov	r10d,DWORD[4+rsi]
	xor	r11d,edx
	add	eax,r11d
	rol	eax,7
	mov	r11d,ecx
	add	eax,ebx
	xor	r11d,ebx
	lea	edx,[((-389564586))+r10*1+rdx]
	and	r11d,eax
	mov	r10d,DWORD[8+rsi]
	xor	r11d,ecx
	add	edx,r11d
	rol	edx,12
	mov	r11d,ebx
	add	edx,eax
	xor	r11d,eax
	lea	ecx,[606105819+r10*1+rcx]
	and	r11d,edx
	mov	r10d,DWORD[12+rsi]
	xor	r11d,ebx
	add	ecx,r11d
	rol	ecx,17
	mov	r11d,eax
	add	ecx,edx
	xor	r11d,edx
	lea	ebx,[((-1044525330))+r10*1+rbx]
	and	r11d,ecx
	mov	r10d,DWORD[16+rsi]
	xor	r11d,eax
	add	ebx,r11d
	rol	ebx,22
	mov	r11d,edx
	add	ebx,ecx
	xor	r11d,ecx
	lea	eax,[((-176418897))+r10*1+rax]
	and	r11d,ebx
	mov	r10d,DWORD[20+rsi]
	xor	r11d,edx
	add	eax,r11d
	rol	eax,7
	mov	r11d,ecx
	add	eax,ebx
	xor	r11d,ebx
	lea	edx,[1200080426+r10*1+rdx]
	and	r11d,eax
	mov	r10d,DWORD[24+rsi]
	xor	r11d,ecx
	add	edx,r11d
	rol	edx,12
	mov	r11d,ebx
	add	edx,eax
	xor	r11d,eax
	lea	ecx,[((-1473231341))+r10*1+rcx]
	and	r11d,edx
	mov	r10d,DWORD[28+rsi]
	xor	r11d,ebx
	add	ecx,r11d
	rol	ecx,17
	mov	r11d,eax
	add	ecx,edx
	xor	r11d,edx
	lea	ebx,[((-45705983))+r10*1+rbx]
	and	r11d,ecx
	mov	r10d,DWORD[32+rsi]
	xor	r11d,eax
	add	ebx,r11d
	rol	ebx,22
	mov	r11d,edx
	add	ebx,ecx
	xor	r11d,ecx
	lea	eax,[1770035416+r10*1+rax]
	and	r11d,ebx
	mov	r10d,DWORD[36+rsi]
	xor	r11d,edx
	add	eax,r11d
	rol	eax,7
	mov	r11d,ecx
	add	eax,ebx
	xor	r11d,ebx
	lea	edx,[((-1958414417))+r10*1+rdx]
	and	r11d,eax
	mov	r10d,DWORD[40+rsi]
	xor	r11d,ecx
	add	edx,r11d
	rol	edx,12
	mov	r11d,ebx
	add	edx,eax
	xor	r11d,eax
	lea	ecx,[((-42063))+r10*1+rcx]
	and	r11d,edx
	mov	r10d,DWORD[44+rsi]
	xor	r11d,ebx
	add	ecx,r11d
	rol	ecx,17
	mov	r11d,eax
	add	ecx,edx
	xor	r11d,edx
	lea	ebx,[((-1990404162))+r10*1+rbx]
	and	r11d,ecx
	mov	r10d,DWORD[48+rsi]
	xor	r11d,eax
	add	ebx,r11d
	rol	ebx,22
	mov	r11d,edx
	add	ebx,ecx
	xor	r11d,ecx
	lea	eax,[1804603682+r10*1+rax]
	and	r11d,ebx
	mov	r10d,DWORD[52+rsi]
	xor	r11d,edx
	add	eax,r11d
	rol	eax,7
	mov	r11d,ecx
	add	eax,ebx
	xor	r11d,ebx
	lea	edx,[((-40341101))+r10*1+rdx]
	and	r11d,eax
	mov	r10d,DWORD[56+rsi]
	xor	r11d,ecx
	add	edx,r11d
	rol	edx,12
	mov	r11d,ebx
	add	edx,eax
	xor	r11d,eax
	lea	ecx,[((-1502002290))+r10*1+rcx]
	and	r11d,edx
	mov	r10d,DWORD[60+rsi]
	xor	r11d,ebx
	add	ecx,r11d
	rol	ecx,17
	mov	r11d,eax
	add	ecx,edx
	xor	r11d,edx
	lea	ebx,[1236535329+r10*1+rbx]
	and	r11d,ecx
	mov	r10d,DWORD[4+rsi]
	xor	r11d,eax
	add	ebx,r11d
	rol	ebx,22
	mov	r11d,edx
	add	ebx,ecx
	mov	r11d,edx
	mov	r12d,edx
	not	r11d
	and	r12d,ebx
	lea	eax,[((-165796510))+r10*1+rax]
	and	r11d,ecx
	mov	r10d,DWORD[24+rsi]
	or	r12d,r11d
	mov	r11d,ecx
	add	eax,r12d
	mov	r12d,ecx
	rol	eax,5
	add	eax,ebx
	not	r11d
	and	r12d,eax
	lea	edx,[((-1069501632))+r10*1+rdx]
	and	r11d,ebx
	mov	r10d,DWORD[44+rsi]
	or	r12d,r11d
	mov	r11d,ebx
	add	edx,r12d
	mov	r12d,ebx
	rol	edx,9
	add	edx,eax
	not	r11d
	and	r12d,edx
	lea	ecx,[643717713+r10*1+rcx]
	and	r11d,eax
	mov	r10d,DWORD[rsi]
	or	r12d,r11d
	mov	r11d,eax
	add	ecx,r12d
	mov	r12d,eax
	rol	ecx,14
	add	ecx,edx
	not	r11d
	and	r12d,ecx
	lea	ebx,[((-373897302))+r10*1+rbx]
	and	r11d,edx
	mov	r10d,DWORD[20+rsi]
	or	r12d,r11d
	mov	r11d,edx
	add	ebx,r12d
	mov	r12d,edx
	rol	ebx,20
	add	ebx,ecx
	not	r11d
	and	r12d,ebx
	lea	eax,[((-701558691))+r10*1+rax]
	and	r11d,ecx
	mov	r10d,DWORD[40+rsi]
	or	r12d,r11d
	mov	r11d,ecx
	add	eax,r12d
	mov	r12d,ecx
	rol	eax,5
	add	eax,ebx
	not	r11d
	and	r12d,eax
	lea	edx,[38016083+r10*1+rdx]
	and	r11d,ebx
	mov	r10d,DWORD[60+rsi]
	or	r12d,r11d
	mov	r11d,ebx
	add	edx,r12d
	mov	r12d,ebx
	rol	edx,9
	add	edx,eax
	not	r11d
	and	r12d,edx
	lea	ecx,[((-660478335))+r10*1+rcx]
	and	r11d,eax
	mov	r10d,DWORD[16+rsi]
	or	r12d,r11d
	mov	r11d,eax
	add	ecx,r12d
	mov	r12d,eax
	rol	ecx,14
	add	ecx,edx
	not	r11d
	and	r12d,ecx
	lea	ebx,[((-405537848))+r10*1+rbx]
	and	r11d,edx
	mov	r10d,DWORD[36+rsi]
	or	r12d,r11d
	mov	r11d,edx
	add	ebx,r12d
	mov	r12d,edx
	rol	ebx,20
	add	ebx,ecx
	not	r11d
	and	r12d,ebx
	lea	eax,[568446438+r10*1+rax]
	and	r11d,ecx
	mov	r10d,DWORD[56+rsi]
	or	r12d,r11d
	mov	r11d,ecx
	add	eax,r12d
	mov	r12d,ecx
	rol	eax,5
	add	eax,ebx
	not	r11d
	and	r12d,eax
	lea	edx,[((-1019803690))+r10*1+rdx]
	and	r11d,ebx
	mov	r10d,DWORD[12+rsi]
	or	r12d,r11d
	mov	r11d,ebx
	add	edx,r12d
	mov	r12d,ebx
	rol	edx,9
	add	edx,eax
	not	r11d
	and	r12d,edx
	lea	ecx,[((-187363961))+r10*1+rcx]
	and	r11d,eax
	mov	r10d,DWORD[32+rsi]
	or	r12d,r11d
	mov	r11d,eax
	add	ecx,r12d
	mov	r12d,eax
	rol	ecx,14
	add	ecx,edx
	not	r11d
	and	r12d,ecx
	lea	ebx,[1163531501+r10*1+rbx]
	and	r11d,edx
	mov	r10d,DWORD[52+rsi]
	or	r12d,r11d
	mov	r11d,edx
	add	ebx,r12d
	mov	r12d,edx
	rol	ebx,20
	add	ebx,ecx
	not	r11d
	and	r12d,ebx
	lea	eax,[((-1444681467))+r10*1+rax]
	and	r11d,ecx
	mov	r10d,DWORD[8+rsi]
	or	r12d,r11d
	mov	r11d,ecx
	add	eax,r12d
	mov	r12d,ecx
	rol	eax,5
	add	eax,ebx
	not	r11d
	and	r12d,eax
	lea	edx,[((-51403784))+r10*1+rdx]
	and	r11d,ebx
	mov	r10d,DWORD[28+rsi]
	or	r12d,r11d
	mov	r11d,ebx
	add	edx,r12d
	mov	r12d,ebx
	rol	edx,9
	add	edx,eax
	not	r11d
	and	r12d,edx
	lea	ecx,[1735328473+r10*1+rcx]
	and	r11d,eax
	mov	r10d,DWORD[48+rsi]
	or	r12d,r11d
	mov	r11d,eax
	add	ecx,r12d
	mov	r12d,eax
	rol	ecx,14
	add	ecx,edx
	not	r11d
	and	r12d,ecx
	lea	ebx,[((-1926607734))+r10*1+rbx]
	and	r11d,edx
	mov	r10d,DWORD[20+rsi]
	or	r12d,r11d
	mov	r11d,edx
	add	ebx,r12d
	mov	r12d,edx
	rol	ebx,20
	add	ebx,ecx
	mov	r11d,ecx
	lea	eax,[((-378558))+r10*1+rax]
	xor	r11d,edx
	mov	r10d,DWORD[32+rsi]
	xor	r11d,ebx
	add	eax,r11d
	mov	r11d,ebx
	rol	eax,4
	add	eax,ebx
	lea	edx,[((-2022574463))+r10*1+rdx]
	xor	r11d,ecx
	mov	r10d,DWORD[44+rsi]
	xor	r11d,eax
	add	edx,r11d
	rol	edx,11
	mov	r11d,eax
	add	edx,eax
	lea	ecx,[1839030562+r10*1+rcx]
	xor	r11d,ebx
	mov	r10d,DWORD[56+rsi]
	xor	r11d,edx
	add	ecx,r11d
	mov	r11d,edx
	rol	ecx,16
	add	ecx,edx
	lea	ebx,[((-35309556))+r10*1+rbx]
	xor	r11d,eax
	mov	r10d,DWORD[4+rsi]
	xor	r11d,ecx
	add	ebx,r11d
	rol	ebx,23
	mov	r11d,ecx
	add	ebx,ecx
	lea	eax,[((-1530992060))+r10*1+rax]
	xor	r11d,edx
	mov	r10d,DWORD[16+rsi]
	xor	r11d,ebx
	add	eax,r11d
	mov	r11d,ebx
	rol	eax,4
	add	eax,ebx
	lea	edx,[1272893353+r10*1+rdx]
	xor	r11d,ecx
	mov	r10d,DWORD[28+rsi]
	xor	r11d,eax
	add	edx,r11d
	rol	edx,11
	mov	r11d,eax
	add	edx,eax
	lea	ecx,[((-155497632))+r10*1+rcx]
	xor	r11d,ebx
	mov	r10d,DWORD[40+rsi]
	xor	r11d,edx
	add	ecx,r11d
	mov	r11d,edx
	rol	ecx,16
	add	ecx,edx
	lea	ebx,[((-1094730640))+r10*1+rbx]
	xor	r11d,eax
	mov	r10d,DWORD[52+rsi]
	xor	r11d,ecx
	add	ebx,r11d
	rol	ebx,23
	mov	r11d,ecx
	add	ebx,ecx
	lea	eax,[681279174+r10*1+rax]
	xor	r11d,edx
	mov	r10d,DWORD[rsi]
	xor	r11d,ebx
	add	eax,r11d
	mov	r11d,ebx
	rol	eax,4
	add	eax,ebx
	lea	edx,[((-358537222))+r10*1+rdx]
	xor	r11d,ecx
	mov	r10d,DWORD[12+rsi]
	xor	r11d,eax
	add	edx,r11d
	rol	edx,11
	mov	r11d,eax
	add	edx,eax
	lea	ecx,[((-722521979))+r10*1+rcx]
	xor	r11d,ebx
	mov	r10d,DWORD[24+rsi]
	xor	r11d,edx
	add	ecx,r11d
	mov	r11d,edx
	rol	ecx,16
	add	ecx,edx
	lea	ebx,[76029189+r10*1+rbx]
	xor	r11d,eax
	mov	r10d,DWORD[36+rsi]
	xor	r11d,ecx
	add	ebx,r11d
	rol	ebx,23
	mov	r11d,ecx
	add	ebx,ecx
	lea	eax,[((-640364487))+r10*1+rax]
	xor	r11d,edx
	mov	r10d,DWORD[48+rsi]
	xor	r11d,ebx
	add	eax,r11d
	mov	r11d,ebx
	rol	eax,4
	add	eax,ebx
	lea	edx,[((-421815835))+r10*1+rdx]
	xor	r11d,ecx
	mov	r10d,DWORD[60+rsi]
	xor	r11d,eax
	add	edx,r11d
	rol	edx,11
	mov	r11d,eax
	add	edx,eax
	lea	ecx,[530742520+r10*1+rcx]
	xor	r11d,ebx
	mov	r10d,DWORD[8+rsi]
	xor	r11d,edx
	add	ecx,r11d
	mov	r11d,edx
	rol	ecx,16
	add	ecx,edx
	lea	ebx,[((-995338651))+r10*1+rbx]
	xor	r11d,eax
	mov	r10d,DWORD[rsi]
	xor	r11d,ecx
	add	ebx,r11d
	rol	ebx,23
	mov	r11d,ecx
	add	ebx,ecx
	mov	r11d,0xffffffff
	xor	r11d,edx
	lea	eax,[((-198630844))+r10*1+rax]
	or	r11d,ebx
	mov	r10d,DWORD[28+rsi]
	xor	r11d,ecx
	add	eax,r11d
	mov	r11d,0xffffffff
	rol	eax,6
	xor	r11d,ecx
	add	eax,ebx
	lea	edx,[1126891415+r10*1+rdx]
	or	r11d,eax
	mov	r10d,DWORD[56+rsi]
	xor	r11d,ebx
	add	edx,r11d
	mov	r11d,0xffffffff
	rol	edx,10
	xor	r11d,ebx
	add	edx,eax
	lea	ecx,[((-1416354905))+r10*1+rcx]
	or	r11d,edx
	mov	r10d,DWORD[20+rsi]
	xor	r11d,eax
	add	ecx,r11d
	mov	r11d,0xffffffff
	rol	ecx,15
	xor	r11d,eax
	add	ecx,edx
	lea	ebx,[((-57434055))+r10*1+rbx]
	or	r11d,ecx
	mov	r10d,DWORD[48+rsi]
	xor	r11d,edx
	add	ebx,r11d
	mov	r11d,0xffffffff
	rol	ebx,21
	xor	r11d,edx
	add	ebx,ecx
	lea	eax,[1700485571+r10*1+rax]
	or	r11d,ebx
	mov	r10d,DWORD[12+rsi]
	xor	r11d,ecx
	add	eax,r11d
	mov	r11d,0xffffffff
	rol	eax,6
	xor	r11d,ecx
	add	eax,ebx
	lea	edx,[((-1894986606))+r10*1+rdx]
	or	r11d,eax
	mov	r10d,DWORD[40+rsi]
	xor	r11d,ebx
	add	edx,r11d
	mov	r11d,0xffffffff
	rol	edx,10
	xor	r11d,ebx
	add	edx,eax
	lea	ecx,[((-1051523))+r10*1+rcx]
	or	r11d,edx
	mov	r10d,DWORD[4+rsi]
	xor	r11d,eax
	add	ecx,r11d
	mov	r11d,0xffffffff
	rol	ecx,15
	xor	r11d,eax
	add	ecx,edx
	lea	ebx,[((-2054922799))+r10*1+rbx]
	or	r11d,ecx
	mov	r10d,DWORD[32+rsi]
	xor	r11d,edx
	add	ebx,r11d
	mov	r11d,0xffffffff
	rol	ebx,21
	xor	r11d,edx
	add	ebx,ecx
	lea	eax,[1873313359+r10*1+rax]
	or	r11d,ebx
	mov	r10d,DWORD[60+rsi]
	xor	r11d,ecx
	add	eax,r11d
	mov	r11d,0xffffffff
	rol	eax,6
	xor	r11d,ecx
	add	eax,ebx
	lea	edx,[((-30611744))+r10*1+rdx]
	or	r11d,eax
	mov	r10d,DWORD[24+rsi]
	xor	r11d,ebx
	add	edx,r11d
	mov	r11d,0xffffffff
	rol	edx,10
	xor	r11d,ebx
	add	edx,eax
	lea	ecx,[((-1560198380))+r10*1+rcx]
	or	r11d,edx
	mov	r10d,DWORD[52+rsi]
	xor	r11d,eax
	add	ecx,r11d
	mov	r11d,0xffffffff
	rol	ecx,15
	xor	r11d,eax
	add	ecx,edx
	lea	ebx,[1309151649+r10*1+rbx]
	or	r11d,ecx
	mov	r10d,DWORD[16+rsi]
	xor	r11d,edx
	add	ebx,r11d
	mov	r11d,0xffffffff
	rol	ebx,21
	xor	r11d,edx
	add	ebx,ecx
	lea	eax,[((-145523070))+r10*1+rax]
	or	r11d,ebx
	mov	r10d,DWORD[44+rsi]
	xor	r11d,ecx
	add	eax,r11d
	mov	r11d,0xffffffff
	rol	eax,6
	xor	r11d,ecx
	add	eax,ebx
	lea	edx,[((-1120210379))+r10*1+rdx]
	or	r11d,eax
	mov	r10d,DWORD[8+rsi]
	xor	r11d,ebx
	add	edx,r11d
	mov	r11d,0xffffffff
	rol	edx,10
	xor	r11d,ebx
	add	edx,eax
	lea	ecx,[718787259+r10*1+rcx]
	or	r11d,edx
	mov	r10d,DWORD[36+rsi]
	xor	r11d,eax
	add	ecx,r11d
	mov	r11d,0xffffffff
	rol	ecx,15
	xor	r11d,eax
	add	ecx,edx
	lea	ebx,[((-343485551))+r10*1+rbx]
	or	r11d,ecx
	mov	r10d,DWORD[rsi]
	xor	r11d,edx
	add	ebx,r11d
	mov	r11d,0xffffffff
	rol	ebx,21
	xor	r11d,edx
	add	ebx,ecx

	add	eax,r8d
	add	ebx,r9d
	add	ecx,r14d
	add	edx,r15d


	add	rsi,64
	cmp	rsi,rdi
	jb	NEAR $L$loop


$L$end:
	mov	DWORD[rbp],eax
	mov	DWORD[4+rbp],ebx
	mov	DWORD[8+rbp],ecx
	mov	DWORD[12+rbp],edx

	mov	r15,QWORD[rsp]

	mov	r14,QWORD[8+rsp]

	mov	r12,QWORD[16+rsp]

	mov	rbx,QWORD[24+rsp]

	mov	rbp,QWORD[32+rsp]

	add	rsp,40

	mov	rdi,QWORD[8+rsp]	;WIN64 epilogue
	mov	rsi,QWORD[16+rsp]
	DB	0F3h,0C3h		;repret
SEH64_ENDPROC_FRAME	ossl_md5_block_asm_data_order
$L$SEH_end_ossl_md5_block_asm_data_order:
