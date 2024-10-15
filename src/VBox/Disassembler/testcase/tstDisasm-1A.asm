; $Id: tstDisasm-1A.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBox disassembler: Assembler test routines
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from https://www.virtualbox.org.
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation, in version 3 of the
; License.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses>.
;
; SPDX-License-Identifier: GPL-3.0-only
;

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "iprt/asmdefs.mac"
;%include "VBox/vmm/vm.mac"
;%include "VBox/err.mac"
;%include "VBox/vmm/stam.mac"
;%include "iprt/x86.mac"

BITS 32

%ifndef __YASM_VERSION_ID__
 %define __YASM_VERSION_ID__ 001010000h ; v1.2.0.0 for OS/2
%endif
%if __YASM_VERSION_ID__ >= 001020001h ; v1.2.0.1 and greater, make sure to exclude v1.2.0.0.
 %define pmulhrwa pmulhrw
%endif


BEGINCODE

align 16
BEGINPROC   TestProc32
        xor eax, eax
        mov al, 4
        lea edx, [4]
        mov edx, 4
        mov eax, 4
        shl eax, 4
        shl edx, 4
        shr edx, 4
        mov eax, edx
        mov eax, ecx
        mov edx, eax
        mov ecx, eax
        DB 0xF0, 0x0F, 0x22, 0xC0
        DB 0xF0, 0x0F, 0x20, 0xC0
        smsw  word [edx+16]
        ;    invept      eax, dqword [ecx]
        DB          0x66, 0x0F, 0x38, 0x80, 0x1
        ;    invept      eax, dqword [ecx]
        DB          0x66, 0x0F, 0x38, 0x81, 0x1
        mov   eax, dword [ecx]
        mov   word [edi], 0123ah
        movzx eax,byte  [edx]
        movzx eax,word  [edx]
        mov dword [es:ebx + 1234h], 0789h
        mov word  [fs:ebx + ecx], 0654h
        mov byte  [esi + eax*4], 054h
        mov bl, byte [ds:ebp + 1234h]
        mov al, [cs:1234h + ecx*8]
        mov al, [cs:1234h]
        mov ax, [cs:1234h]
        mov eax, [cs:1234h]
        lock cmpxchg [ecx], eax
        lock cmpxchg [ecx], ax
        lock cmpxchg [ecx], dl
        movzx ESI,word  [EAX]
        in al, dx
        in ax, dx
        in eax, dx
        mov ebx, [ecx + eax*4 + 17]
        mov ebx, [ebp + eax*4 + 4]
        mov ebx, [ebp + eax*4]
        int 80h
        in  al, 60h
        in  ax, dx
        out 64h, eax

        movss xmm0, xmm1
        movss xmm3, [eax]
        movss [eax], xmm4
        movsd xmm6, xmm1

        pause
        nop

        ; 3Dnow!
        pavgusb     mm1, mm0
        pf2id       mm5, mm4
        pf2iw       mm6, mm3
        pfacc       mm7, mm2
        pfadd       mm5, mm4
        pfcmpeq     mm6, mm3
        pfcmpge     mm2, mm7
        pfcmpgt     mm4, mm5
        pfmax       mm3, mm6
        pfmin       mm1, mm0
        pfmul       mm5, mm4
        pmulhrwa    mm3, mm6
        pfnacc      mm4, mm5
        pfpnacc     mm3, mm6
        pfrcp       mm0, mm1
        pfrcpit1    mm2, mm7
        pfrcpit2    mm4, mm5
        pfrsqrt     mm7, mm2
        pfrsqit1    mm1, mm0
        pfsub       mm6, mm3
        pfsubr      mm0, mm1
        pi2fd       mm7, mm2
        pi2fw       mm0, mm1
        pswapd      mm2, mm7

        pavgusb     mm1, qword [es:eax+000000010h]
        pf2id       mm5, qword [ds:esi+000101010h]
        pf2iw       mm6, qword [fs:esi+000101010h]
        pfacc       mm7, qword [gs:esi+000101010h]
        pfadd       mm5, qword [   esi+000101010h]
        pfcmpeq     mm6, qword [ edi*8+000101010h]
        pfcmpge     mm2, qword [es:esi+000100010h]
        pfcmpgt     mm4, qword [es:esi+000101010h]
        pfmax       mm3, qword [es:esi+000101010h]
        pfmin       mm1, qword [es:esi+000101010h]
        pfmul       mm5, qword [es:esi+000101000h]
%ifndef RT_OS_OS2 ; nasm objects to this
        pmulhrwa    mm3, qword [es:eax+0ffffffffh]
%endif
        pfnacc      mm4, qword [es:ebx+000101010h]
        pfpnacc     mm3, qword [es:edx+000102900h]
        pfrcp       mm0, qword [es:ecx+000101020h]
        pfrcpit1    mm2, qword [es:ebp+000101510h]
        pfrcpit2    mm4, qword [es:esp+000101310h]
        pfrsqrt     mm7, qword [es:esi+0f0106010h]
        pfrsqit1    mm1, qword [es:edi+0001f1010h]
        pfsub       mm6, qword [es:esi*2]
        pfsubr      mm0, qword [es:esi*3]
        pi2fd       mm7, qword [es:esi*4]
        pi2fw       mm0, qword [es:esi*5]
        pswapd      mm2, qword [es:esi*8]

        pmulhrwa    mm0, qword [ds:ebp+edi*8+00f000001h]

        ; MMX
        psubusb     mm1, mm3
        cvtpi2pd    xmm0, mm3
        paddd       mm1, mm3
        paddd       xmm1, xmm3

        lds eax, [ebx]
        les ebp, [esp]
%if __YASM_VERSION_ID__ >= 001030000h ; Old yasm doesn't support the instructions below
        adcx        eax, ebx
        adcx        eax, [edi]

        adox        eax, ebx
        adox        eax, [edi]
        adox        eax, [edi + 1000h]

        tzcnt      ax, bx
        tzcnt      eax, ebx
        tzcnt      ax, [edi]
        tzcnt      eax, [edi]
        tzcnt      eax, [edi + 1000h]
        vpmovsxbw  ymm0, xmm1
        vpmovzxbq  ymm1, [100h]
        vgatherqps xmm0,dword [eax+xmm0*2],xmm0
        vgatherqpd xmm0,qword [eax+xmm0*2],xmm0
        vpsrld      ymm5, ymm1, 009h
        vpslld      ymm0, ymm7, 01ah
%endif

        movbe       eax, [edi]
        movbe       ebx, [edi + 1000h]
        movbe       ax, [edi]
        movbe       [edi], eax

        crc32       eax, bl
        crc32       eax, bx
        crc32       eax, ebx
        crc32       eax, byte [edi]
        crc32       eax, word [edi]
        crc32       eax, dword [edi]

        popcnt      ax, bx
        popcnt      eax, ebx
        popcnt      ax, [edi]
        popcnt      eax, [edi]
        popcnt      eax, [edi + 1000h]

        lzcnt      ax, bx
        lzcnt      eax, ebx
        lzcnt      ax, [edi]
        lzcnt      eax, [edi]
        lzcnt      eax, [edi + 1000h]

        vmread     eax, ebx
        vmwrite    eax, ebx

        movd       mm0, [edi]
        movq       mm0, [edi]
        movq       mm0, mm1

        vmovups    xmm0, xmm1
        vmovaps    ymm0, ymm1
        vunpcklps  xmm0, xmm1, xmm2
        vunpcklps  ymm0, ymm1, ymm2

        lddqu      xmm1, [ds:ebp+edi*8+00f000001h]
        vlddqu     xmm1, [ds:ebp+edi*8+00f000001h]
        vlddqu      ymm1, [ds:ebp+edi*8+00f000001h]

        vpmovsxbw xmm0,qword [0x100]
        vbroadcastf128 ymm0,oword [0x100]

        palignr mm0, mm1, 1
        vpinsrb xmm0, xmm1, eax, 1
        vpinsrb xmm0, xmm1, [100h], 1
        vinsertps xmm0, xmm1, xmm2, 1
        vinsertps xmm0, xmm1, [100h], 1

        vblendvps xmm0, xmm1, xmm2, xmm3
        vblendvps ymm0, ymm1, ymm2, ymm3

        aesimc xmm0, xmm1

        pmovzxbq xmm0, xmm1
        pmovzxbq xmm1, [100h]

        vfmaddsub132pd ymm1, ymm2, ymm3

%ifndef RT_OS_OS2
        blsr eax, ebx
        blsi eax, [ebx]
%endif
        db 0c4h, 0e2h, 0f8h, 0f3h, 01bh ;  blsi rax, dword [ebx] - but VEX.W=1 is ignored, so same as previous
%ifndef RT_OS_OS2
        blsmsk eax, [ebx+edi*2]
        shlx eax, ebx, ecx
%endif

        pmovmskb eax, mm2
        pmovmskb eax, xmm3
        vpmovmskb eax, xmm3
%ifndef RT_OS_OS2
        vpmovmskb eax, ymm3
%endif

ENDPROC   TestProc32


%ifndef RT_OS_OS2
BITS 64
align 16
BEGINPROC TestProc64
        mov cr8, rax
        mov cr8, rbx
        mov [0xfffe0080], rax
        mov [0xfffe0080], rbx
        mov rax, cr8
        mov rbx, cr8
        mov rax, [0xfffe0080]
        mov rbx, [0xfffe0080]
        divsd xmm1, xmm0
        ;    invept      rdi, dqword [rsi]
        DB          0x66, 0x0F, 0x38, 0x80, 0x3E
        ;    invept      rcx, dqword [rdx]
        DB          0x66, 0x0F, 0x38, 0x80, 0xA
        ;invvpid     rdi, dqword [rsi]
        DB          0x66, 0x0F, 0x38, 0x81, 0x3E
        ;    invvpid     rcx, dqword [rdx]
        DB          0x66, 0x0F, 0x38, 0x81, 0xA
        mov   rdi, [rsi]
        mov   rcx, [rdx]
        db 48h
        db 0c7h
        db 42h
        db 18h
        db 20h
        db 3eh
        db 23h
        db 80h
        call qword [r8+10h]
        ; test
        db 48h
        db 8bh
        db 44h
        db 0ah
        db 0f8h
        ;incorrectly assembled by yasm; REX.W should not be added!
        ;test rax, dword 0cc90cc90h
        db 8bh
        db 04h
        db 8dh
        db 00h
        db 00h
        db 0feh
        db 0ffh
        mov   qword [rcx+rdx], 0
        mov   dword [rcx+rdx], 0
        and   [r15], rax
        movzx rcx, sil
        and   sil, 3
        movzx ecx, ah
        and   ah, 3

        sub rcx, 1234h
        mov rax, qword [0cc90cc90h]
        mov rax, qword [00c90cc90h]
        mov rax, dword 0cc90cc90h
        mov rax, qword 0ffffcc90cc90h

        movzx rax,byte  [edx]
        movzx rax,word  [edx]
        movzx rax,byte  [rdx]
        lock cmpxchg [rcx], rax
        lock cmpxchg [rcx], ax
        lock cmpxchg [r15], dl
        movzx RSI, word [R8]
        in al, dx
        in ax, dx
        in eax, dx
        mov rbx, [rcx + rax*4 + 17]
        mov rbx, [rbp + rax*4 + 4]
        mov rbx, [rbp + rax*4]
        mov rbx, [ebp + eax*4]
        int 80h
        in  al, 60h
        in  ax, dx
        out 64h, eax

        movss xmm0, xmm14
        movsd xmm6, xmm1

        movbe   eax, [rdi]
        movbe   ax, [rdi]
        movbe   rax, [rdi]

        crc32       eax, bl
        crc32       eax, bx
        crc32       eax, ebx
        crc32       eax, byte [edi]
        crc32       eax, word [edi]
        crc32       eax, dword [edi]

        crc32       rax, bl
        crc32       rax, byte [rdi]
        crc32       rax, qword [rdi]

%if __YASM_VERSION_ID__ >= 001030000h ; Old yasm doesn't support the instructions below

        adcx    eax, ebx
        adcx    rax, rbx
        adcx    r8, r11
        adcx    r8d, edx

        adox    eax, ebx
        adox    eax, [edi]
        adox    eax, [edi + 1000h]

        adox    rax, rbx
        adox    rax, [rdi]
        adox    rax, [rdi + 1000h]
        adox    rax, [edi + 1000h]

        tzcnt      ax, bx
        tzcnt      eax, ebx
        tzcnt      rax, rbx
        tzcnt      ax, [edi]
        tzcnt      eax, [edi]
        tzcnt      eax, [edi + 1000h]

        vpunpcklbw ymm1, ymm2, ymm3
        vpmovsxbw  ymm4,[0x100]
        vgatherqpd xmm0,qword [rbx+xmm11*2],xmm2

        vpsrld      ymm5, ymm1, 009h
        vpsrld      ymm8, ymm12, 011h
        vpslld      ymm0, ymm7, 01ah
%endif

        popcnt      ax, bx
        popcnt      eax, ebx
        popcnt      rax, rbx
        popcnt      ax, [edi]
        popcnt      eax, [edi]
        popcnt      eax, [edi + 1000h]
        popcnt      rax, [rdi + 1000h]

        lzcnt      ax, bx
        lzcnt      eax, ebx
        lzcnt      rax, rbx
        lzcnt      ax, [edi]
        lzcnt      eax, [edi]
        lzcnt      eax, [edi + 1000h]
        lzcnt      eax, [rdi]
        lzcnt      ax, [rdi]
        lzcnt      rax, [rdi]
        lzcnt      r8d, [rdi]

        vmread     rax, rbx
        vmwrite    rax, rbx

        getsec

        movd       mm0, [rdi]
        movq       mm0, [edi]
        movq       mm0, mm1

        vmovups    xmm0, xmm1
        vmovaps    ymm0, ymm1
        vunpcklps  xmm0, xmm1, xmm2
        vunpcklps  ymm0, ymm1, ymm2
        vunpcklps  ymm0, ymm10, ymm2

        vmovups    xmm5, xmm9

        vcmpps     xmm1, xmm2, xmm3, 12

        lddqu      xmm1, [ebp+edi*8+00f000001h]
        vlddqu     xmm1, [rbp+rdi*8+00f000001h]
        vlddqu     ymm1, [rbp+rdi*8+00f000001h]

        vbroadcastf128 ymm0,oword [0x100]
        vmovlps   xmm0, xmm1, [100h]
        vmovlps   xmm0, xmm1, [eax + ebx]
        vmovlps   xmm0, xmm1, [rax + rbx]
        vmovlps   xmm10, xmm1, [rax]

        vblendvpd xmm0, xmm1, [100h], xmm3

        dpps xmm0, xmm1, 1

        extractps eax, xmm2, 3
        vzeroupper
        vzeroall

        movlps xmm0, [100h]
        movlps xmm0, [eax + ebx]
        movlps xmm10, [rax + rbx]
        movhlps xmm0, xmm1

        blsr eax, ebx
        blsr rax, rbx
        blsi eax, [rbx]
        blsi rax, [rbx]
        db 0c4h, 0e2h, 0f8h | 4, 0f3h, 01bh ; blsi rax, [rbx] with VEX.L=1 - should be invalid
        blsmsk eax, [rbx+rdi*2]
        blsmsk rax, [rbx+rdi*2]
        blsmsk r8, [rbx+rdi*2]

        shlx   eax, ebx, ecx
        shlx   r8, rax, r15

        pmovmskb eax, mm2
        pmovmskb r9, mm2
        pmovmskb eax, xmm3
        pmovmskb r10, xmm3
        vpmovmskb eax, xmm3
        vpmovmskb rax, xmm3
        vpmovmskb r11, ymm9

        sha1nexte xmm0, xmm1
        sha1msg1  xmm0, xmm1
        sha1msg2  xmm0, xmm1
        sha1rnds4 xmm1, xmm2, 0
        sha1rnds4 xmm1, xmm2, 1
        sha256msg1 xmm0, xmm1
        sha256msg2 xmm0, xmm1
        sha256rnds2 xmm0, xmm1

        vpsrldq xmm1, xmm2, 3
        db 066h
        vpsrldq xmm1, xmm2, 3
        vpsllq xmm15, xmm8, 99
        vcmpps xmm1, xmm2, xmm3, 1
        vcmpps ymm1, ymm2, ymm3, 1
        vcmppd xmm1, xmm2, xmm3, 1
        vcmppd ymm1, ymm2, ymm3, 1
        vcmpss xmm1, xmm2, xmm3, 1
        vcmpsd xmm1, xmm2, xmm3, 1

        vcvtsi2ss xmm0, xmm1, ecx
        vcvtsi2ss xmm0, xmm1, rcx
        vcvtsi2sd xmm0, xmm1, ecx
        vcvtsi2sd xmm0, xmm1, rcx

        vcvttss2si ecx, xmm0
        vcvttss2si ecx, [rax]
        vcvttss2si ecx, [eax]
        vcvttss2si rcx, xmm0
        vcvttss2si rcx, [rax]
        vcvttss2si rcx, [eax]
        vcvtss2si  ecx, xmm0
        vcvtss2si  ecx, [rax]
        vcvtss2si  ecx, [eax]
        vcvtss2si  rcx, xmm0
        vcvtss2si  rcx, [rax]
        vcvtss2si  rcx, [eax]
        vcvttsd2si ecx, xmm0
        vcvttsd2si ecx, [rax]
        vcvttsd2si ecx, [eax]
        vcvttsd2si rcx, xmm0
        vcvttsd2si rcx, [rax]
        vcvttsd2si rcx, [eax]
        vcvtsd2si  ecx, xmm0
        vcvtsd2si  ecx, [rax]
        vcvtsd2si  ecx, [eax]
        vcvtsd2si  rcx, xmm0
        vcvtsd2si  rcx, [rax]
        vcvtsd2si  rcx, [eax]

        ; group 7 stuff.
        vmcall
        vmlaunch
        vmresume
        vmxoff
        monitor
        mwait
        clac
        stac
        encls
        xgetbv
        xsetbv
        vmfunc
        xend
        xtest
        enclu
        swpgs
        rdtscp
        rdfsbase rax
        wrgsbase r15
        rdrand rax
        vmxon [rax]

        cmpxchg8b [rdi]
        lock cmpxchg8b [rdi]
        cmpxchg16b [rdi]
        lock cmpxchg16b [rdi]
        vmptrst [rsi + 1]

        ret
ENDPROC   TestProc64
%endif ; !OS2

