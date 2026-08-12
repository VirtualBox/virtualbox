; $Id: HMR0UtilA-x86.asm 115016 2026-08-12 23:40:45Z knut.osmundsen@oracle.com $
;; @file
; HM - Ring-0 VMX & SVM Helpers.
;

;
; Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "VBox/asmdefs.mac"
%include "VBox/err.mac"
%include "VBox/vmm/hm_vmx.mac"
%include "iprt/x86.mac"



BEGINCODE

;;
; Executes VMWRITE, 64-bit value.
;
; @returns VBox status code.
; @param   idxField   x86: [ebp + 08h]  msc: rcx  gcc: rdi   VMCS index.
; @param   u64Data    x86: [ebp + 0ch]  msc: rdx  gcc: rsi   VM field value.
;
ALIGNCODE(16)
BEGINPROC VMXWriteVmcs64
%ifdef ASM_CALL64_GCC
    and         edi, 0ffffffffh
    xor         rax, rax
    vmwrite     rdi, rsi
%else
    and         ecx, 0ffffffffh
    xor         rax, rax
    vmwrite     rcx, rdx
%endif

    jnc         .valid_vmcs
    mov         eax, VERR_VMX_INVALID_VMCS_PTR
    ret
    int3
.valid_vmcs:
    jnz         .the_end
    mov         eax, VERR_VMX_INVALID_VMCS_FIELD
.the_end:
    ret
ENDPROC VMXWriteVmcs64


;;
; Executes VMREAD, 64-bit value.
;
; @returns VBox status code.
; @param   idxField        VMCS index.
; @param   pData           Where to store VM field value.
;
;DECLASM(int) VMXReadVmcs64(uint32_t idxField, uint64_t *pData);
ALIGNCODE(16)
BEGINPROC VMXReadVmcs64
%ifdef ASM_CALL64_GCC
    and         edi, 0ffffffffh
    xor         rax, rax
    vmread      [rsi], rdi
%else
    and         ecx, 0ffffffffh
    xor         rax, rax
    vmread      [rdx], rcx
%endif

    jnc         .valid_vmcs
    mov         eax, VERR_VMX_INVALID_VMCS_PTR
    ret
    int3
.valid_vmcs:
    jnz         .the_end
    mov         eax, VERR_VMX_INVALID_VMCS_FIELD
.the_end:
    ret
ENDPROC VMXReadVmcs64


;;
; Executes VMREAD, 32-bit value.
;
; @returns VBox status code.
; @param   idxField        VMCS index.
; @param   pu32Data        Where to store VM field value.
;
;DECLASM(int) VMXReadVmcs32(uint32_t idxField, uint32_t *pu32Data);
ALIGNCODE(16)
BEGINPROC VMXReadVmcs32
%ifdef ASM_CALL64_GCC
    and     edi, 0ffffffffh
    xor     rax, rax
    vmread  r10, rdi
    mov     [rsi], r10d
%else
    and     ecx, 0ffffffffh
    xor     rax, rax
    vmread  r10, rcx
    mov     [rdx], r10d
%endif

    jnc     .valid_vmcs
    mov     eax, VERR_VMX_INVALID_VMCS_PTR
    ret
    int3
.valid_vmcs:
    jnz     .the_end
    mov     eax, VERR_VMX_INVALID_VMCS_FIELD
.the_end:
    ret
ENDPROC VMXReadVmcs32


;;
; Executes VMWRITE, 32-bit value.
;
; @returns VBox status code.
; @param   idxField        VMCS index.
; @param   u32Data         Where to store VM field value.
;
;DECLASM(int) VMXWriteVmcs32(uint32_t idxField, uint32_t u32Data);
ALIGNCODE(16)
BEGINPROC VMXWriteVmcs32
%ifdef ASM_CALL64_GCC
    and     edi, 0ffffffffh
    and     esi, 0ffffffffh
    xor     rax, rax
    vmwrite rdi, rsi
%else
    and     ecx, 0ffffffffh
    and     edx, 0ffffffffh
    xor     rax, rax
    vmwrite rcx, rdx
%endif

    jnc     .valid_vmcs
    mov     eax, VERR_VMX_INVALID_VMCS_PTR
    ret
    int3
.valid_vmcs:
    jnz     .the_end
    mov     eax, VERR_VMX_INVALID_VMCS_FIELD
.the_end:
    ret
ENDPROC VMXWriteVmcs32


;;
; Executes VMXON.
;
; @returns VBox status code.
; @param   HCPhysVMXOn      Physical address of VMXON structure.
;
;DECLASM(int) VMXEnable(RTHCPHYS HCPhysVMXOn);
BEGINPROC VMXEnable
    xor     rax, rax
%ifdef ASM_CALL64_GCC
    push    rdi
%else
    push    rcx
%endif
    vmxon   [rsp]

    jnc     .good
    mov     eax, VERR_VMX_INVALID_VMXON_PTR
    jmp     .the_end
    int3

.good:
    jnz     .the_end
    mov     eax, VERR_VMX_VMXON_FAILED

.the_end:
    add     rsp, 8
    ret
ENDPROC VMXEnable


;;
; Executes VMXOFF.
;
;DECLASM(void) VMXDisable(void);
BEGINPROC VMXDisable
    vmxoff
.the_end:
    ret
ENDPROC VMXDisable


;;
; Executes VMCLEAR.
;
; @returns VBox status code.
; @param   HCPhysVmcs     Physical address of VM control structure.
;
;DECLASM(int) VMXClearVmcs(RTHCPHYS HCPhysVmcs);
ALIGNCODE(16)
BEGINPROC VMXClearVmcs
    xor     rax, rax
%ifdef ASM_CALL64_GCC
    push    rdi
%else
    push    rcx
%endif
    vmclear [rsp]
    jnc     .the_end
    mov     eax, VERR_VMX_INVALID_VMCS_PTR
.the_end:
    add     rsp, 8
    ret
ENDPROC VMXClearVmcs


;;
; Executes VMPTRLD.
;
; @returns VBox status code.
; @param   HCPhysVmcs     Physical address of VMCS structure.
;
;DECLASM(int) VMXLoadVmcs(RTHCPHYS HCPhysVmcs);
ALIGNCODE(16)
BEGINPROC VMXLoadVmcs
    xor     rax, rax
%ifdef ASM_CALL64_GCC
    push    rdi
%else
    push    rcx
%endif
    vmptrld [rsp]

    jnc     .the_end
    mov     eax, VERR_VMX_INVALID_VMCS_PTR
.the_end:
    add     rsp, 8
    ret
ENDPROC VMXLoadVmcs


;;
; Executes VMPTRST.
;
; @returns VBox status code.
; @param    [esp + 04h]  gcc:rdi  msc:rcx   Param 1 - First parameter - Address that will receive the current pointer.
;
;DECLASM(int) VMXGetCurrentVmcs(RTHCPHYS *pVMCS);
BEGINPROC VMXGetCurrentVmcs
%ifdef RT_OS_OS2
    mov     eax, VERR_NOT_SUPPORTED
    ret
%else
 %ifdef ASM_CALL64_GCC
    vmptrst [rdi]
 %else
    vmptrst [rcx]
 %endif
    xor     eax, eax
.the_end:
    ret
%endif
ENDPROC VMXGetCurrentVmcs


;;
; Invalidate a page using INVEPT.
;
; @param   enmTlbFlush  msc:ecx  gcc:edi  x86:[esp+04]  Type of flush.
; @param   pDescriptor  msc:edx  gcc:esi  x86:[esp+08]  Descriptor pointer.
;
;DECLASM(int) VMXR0InvEPT(VMXTLBFLUSHEPT enmTlbFlush, uint64_t *pDescriptor);
BEGINPROC VMXR0InvEPT
%ifdef ASM_CALL64_GCC
    and         edi, 0ffffffffh
    xor         rax, rax
;    invept      rdi, qword [rsi]
    DB          0x66, 0x0F, 0x38, 0x80, 0x3E
%else
    and         ecx, 0ffffffffh
    xor         rax, rax
;    invept      rcx, qword [rdx]
    DB          0x66, 0x0F, 0x38, 0x80, 0xA
%endif

    jnc         .valid_vmcs
    mov         eax, VERR_VMX_INVALID_VMCS_PTR
    ret
    int3
.valid_vmcs:
    jnz         .the_end
    mov         eax, VERR_INVALID_PARAMETER
.the_end:
    ret
ENDPROC VMXR0InvEPT


;;
; Invalidate a page using INVVPID.
;
; @param   enmTlbFlush  msc:ecx  gcc:edi  x86:[esp+04]  Type of flush
; @param   pDescriptor  msc:edx  gcc:esi  x86:[esp+08]  Descriptor pointer
;
;DECLASM(int) VMXR0InvVPID(VMXTLBFLUSHVPID enmTlbFlush, uint64_t *pDescriptor);
BEGINPROC VMXR0InvVPID
%ifdef ASM_CALL64_GCC
    and         edi, 0ffffffffh
    xor         rax, rax
;    invvpid     rdi, qword [rsi]
    DB          0x66, 0x0F, 0x38, 0x81, 0x3E
%else
    and         ecx, 0ffffffffh
    xor         rax, rax
;    invvpid     rcx, qword [rdx]
    DB          0x66, 0x0F, 0x38, 0x81, 0xA
%endif

    jnc         .valid_vmcs
    mov         eax, VERR_VMX_INVALID_VMCS_PTR
    ret
    int3
.valid_vmcs:
    jnz         .the_end
    mov         eax, VERR_INVALID_PARAMETER
.the_end:
    ret
ENDPROC VMXR0InvVPID


;;
; Executes INVLPGA.
;
; @param   pPageGC  msc:rcx  gcc:rdi  x86:[esp+04]  Virtual page to invalidate
; @param   uASID    msc:rdx  gcc:rsi  x86:[esp+0C]  Tagged TLB id
;
;DECLASM(void) SVMR0InvlpgA(RTGCPTR pPageGC, uint32_t uASID);
BEGINPROC SVMR0InvlpgA
%ifdef ASM_CALL64_GCC
    mov     rax, rdi
    mov     rcx, rsi
%else
    mov     rax, rcx
    mov     rcx, rdx
%endif

%ifdef __YASM__
    invlpga [xAX], ecx
%else
    invlpga rax, ecx
%endif
    ret
ENDPROC SVMR0InvlpgA

MARK_OBJECT_RETPOLINE_SAFE
