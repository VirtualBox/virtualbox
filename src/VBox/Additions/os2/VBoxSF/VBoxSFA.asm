; $Id: VBoxSFA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxSF - OS/2 Shared Folders, all assembly code (16 -> 32 thunking mostly).
;

;
; Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
;
; Permission is hereby granted, free of charge, to any person
; obtaining a copy of this software and associated documentation
; files (the "Software"), to deal in the Software without
; restriction, including without limitation the rights to use,
; copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the
; Software is furnished to do so, subject to the following
; conditions:
;
; The above copyright notice and this permission notice shall be
; included in all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
; OTHER DEALINGS IN THE SOFTWARE.
;


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%define RT_INCL_16BIT_SEGMENTS
%include "iprt/asmdefs.mac"
%include "iprt/err.mac"
%include "iprt/x86.mac"
%include "iprt/formats/dwarf.mac"
%include "VBox/VBoxGuest.mac"


;*********************************************************************************************************************************
;*  Dwarf constants and macros                                                                                                   *
;*********************************************************************************************************************************
;; enable dwarf debug info
%define WITH_DWARF 1

;; Emits a LEB128 (signed) constant (%1) - limited range.
%macro DWARF_LEB128 1
%if %1 >= 0
 %if %1 < 64
        db  %1
 %else
        db  (%1 & 0x7f) | 0x80
        db  (%1 >> 7)   & 0x7f
 %endif
%else
 %if %1 > -64
        db  (%1 & 0x3f) | 0x40
 %else
        db  (%1 & 0x7f) | 0x80
        db  ((%1 >> 7)  & 0x7f) | 0x40
 %endif
%endif
%endmacro

;; Emits a ULEB128 (unsigned) constant (%1) - limited range.
%macro DWARF_ULEB128 1
%if %1 < 0x80
        db  %1
%elif %1 < 0x4000
        db  (%1 & 0x7f) | 0x80
        db  (%1 >> 7)
%elif %1 < 0x200000
        db  ((%1)       & 0x7f) | 0x80
        db  ((%1 >> 7)  & 0x7f) | 0x80
        db  ((%1 >> 14))
%else
 %error out of range: %1
%endif
%endmacro

;; Emits a pair of ULEB128 constants.  Useful for .debug_abbrev.
%macro DWARF_ULEB128_PAIR 2
        DWARF_ULEB128 %1
        DWARF_ULEB128 %2
%endmacro


;; defines a CFA offset by register (%1) + unsigned offset (%2).
%macro CFA_DEF_CFA 2
        db      DW_CFA_def_cfa
        DWARF_ULEB128 %1
        DWARF_ULEB128 %2
%endmacro

;; defines the register (%1) value as CFA + unsigned offset (%2) * data_alignment_factor.
%macro CFA_VAL_OFFSET 2
        db      DW_CFA_val_offset
        DWARF_ULEB128 %1
        DWARF_ULEB128 %2
%endmacro

;; defines the register (%1) save location as CFA + unsigned offset (%2) * data_alignment_factor.
%macro CFA_OFFSET 2
%if %1 < 0x40
        db      DW_CFA_offset | %1
%else
        db      DW_CFA_offset_extended
        DWARF_ULEB128 %1
%endif
        DWARF_ULEB128 %2
%endmacro

%define MY_ABBREV_CODE_CU       2
%define MY_ABBREV_CODE_LABEL    3


;; Emits a debug info for a label in CODE16.
;; @param %1    symbol
%macro DWARF_LABEL_CODE16 1
%ifdef WITH_DWARF
segment _debug_info
        DWARF_ULEB128       MY_ABBREV_CODE_LABEL
        dd                  %1 wrt CODE16
        db                  2 ; Hardcoded CODE16 number.
%defstr tmp_str_conversion %1
        db                  tmp_str_conversion, 0
%endif
segment CODE16
%endmacro


;; Emits a debug info for a label in CODE32.
;; @param %1    symbol
%macro DWARF_LABEL_TEXT32 1
%ifdef WITH_DWARF
segment _debug_info
        DWARF_ULEB128       MY_ABBREV_CODE_LABEL
        dd                  %1 wrt TEXT32
        db                  3 ; Hardcoded TEXT32 number.
%defstr tmp_str_conversion %1
        db                  tmp_str_conversion, 0
%endif
segment TEXT32
%endmacro



;*********************************************************************************************************************************
;*  Additional Segment definitions.                                                                                                         *
;*********************************************************************************************************************************
%ifdef WITH_DWARF       ; We need to use '_debug_xxx' + dotseg.exe here rather than '.debug_xxx' because some nasm crap.
segment _debug_frame       public CLASS=DWARF align=4 use32
g_cie_thunk_back:
        dd      (g_cie_thunk_end - g_cie_thunk_back - 4)    ; Length
        dd      0xffffffff                      ; I'm a CIE.
        db      4                               ; DwARF v4
        db      0                               ; Augmentation.
        db      4                               ; Address size.
        db      4                               ; Segment size.
        DWARF_LEB128  1                         ; Code alignment factor.
        DWARF_LEB128 -1                         ; Data alignment factor.
        DWARF_ULEB128   DWREG_X86_RA            ; Return register column.
        CFA_DEF_CFA     DWREG_X86_EBP, 8        ; cfa = EBP + 8
        CFA_OFFSET      DWREG_X86_EBP, 8        ; EBP = [CFA - 8]
        CFA_OFFSET      DWREG_X86_ESP, 8+10     ; SS  = [CFA - 8 - 10]
        CFA_OFFSET      DWREG_X86_SS,  8+6      ; SS  = [CFA - 8 - 6]
        CFA_OFFSET      DWREG_X86_ES,  8+4      ; ES  = [CFA - 8 - 4]
        CFA_OFFSET      DWREG_X86_DS,  8+2      ; DS  = [CFA - 8 - 2]
        CFA_OFFSET      DWREG_X86_CS,  2        ; CS  = [CFA - 2]
;        CFA_OFFSET      DWREG_X86_RA,  4        ; RetAddr = [CFA - 4]
        align   4, db DW_CFA_nop
g_cie_thunk_end:


segment _debug_abbrev       public CLASS=DWARF align=1 use32
g_abbrev_compile_unit:
        DWARF_ULEB128       MY_ABBREV_CODE_CU
        DWARF_ULEB128_PAIR  DW_TAG_compile_unit, DW_CHILDREN_yes
        DWARF_ULEB128_PAIR  DW_AT_name, DW_FORM_string
        db                  0, 0 ; the end.
g_abbrev_label:
        db                  MY_ABBREV_CODE_LABEL
        DWARF_ULEB128_PAIR  DW_TAG_label, DW_CHILDREN_no
        DWARF_ULEB128_PAIR  DW_AT_low_pc, DW_FORM_addr
        DWARF_ULEB128_PAIR  DW_AT_segment, DW_FORM_data1
        DWARF_ULEB128_PAIR  DW_AT_name, DW_FORM_string
        db                  0, 0 ; the end.


segment _debug_info         public CLASS=DWARF align=1 use32
g_dwarf_compile_unit_header:
        dd                  g_dwarf_compile_unit_end - g_dwarf_compile_unit_header - 4
        dw                  2           ; DWARF v2
        dd                  g_abbrev_compile_unit wrt _debug_abbrev
        db                  4           ; address_size
.compile_unit_die:
        db                  MY_ABBREV_CODE_CU
        db                  __FILE__, 0

segment TEXT32
%endif ; WITH_DWARF



;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************
%define ERROR_NOT_SUPPORTED         50
%define ERROR_INVALID_PARAMETER     87
%define DevHlp_AttachDD             2ah


;;
; Prints a string to the VBox log port.
%macro DEBUG_STR16 1
%ifdef DEBUG
segment DATA16
%%my_dbg_str: db %1, 0ah, 0
segment CODE16
        push    ax
        mov     ax, %%my_dbg_str
        call    NAME(dbgstr16)
        pop     ax
%endif
%endmacro

;%define RT_STR_QUOTE    "
;%define RT_STR(a_Label)       RT_STR_QUOTE a_Label RT_STR_QUOTE

%macro VBOXSF_EP16_BEGIN 2
DWARF_LABEL_CODE16 %1
global %1
%1:
        ;DEBUG_STR16 {'VBoxSF: ', %2}

%endmacro

%macro VBOXSF_EP16_END 1
global %1_EndProc
%1_EndProc:
%endmacro

;;
; Used in a 16-bit entrypoint for taking us to 32-bit and reserving a parameter frame.
;
; @param    %1      The function name
; @param    %2      The number of bytes to reserve
;
%macro VBOXSF_TO_32 2
        ; prologue
%ifdef DEBUG
 %ifndef WITH_DWARF
        inc     ebp
 %endif
%endif
        push    ebp
        mov     ebp, esp                    ; bp
        push    ds                          ; bp - 2
        push    es                          ; bp - 4
%ifdef WITH_DWARF
        push    ss                          ; bp - 6
        lea     eax, [esp + 3*2 + 4 + 4]    ; bp - 10: return esp (16-bit)
        push    eax
%endif

        ; Reserve the 32-bit parameter and align the stack on a 16 byte
        ; boundary to make GCC really happy.
        sub     sp, %2
        and     sp, 0fff0h

        ;jmp far dword NAME(%i %+ _32) wrt FLAT
        db      066h
        db      0eah
        dd      NAME(%1 %+ _32) ;wrt FLAT
        dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME %1 %+ _32
DWARF_LABEL_TEXT32 NAME(%1 %+ _32)
        mov     ax, DATA32 wrt FLAT
        mov     ds, ax
        mov     es, ax

        call    KernThunkStackTo32
.vboxsf_to_32_end:

%endmacro ; VBOXSF_TO_32

;;
; The counter part to VBOXSF_TO_32
;
; @param    %1      The function name
;
%macro VBOXSF_TO_16 1
.vboxsf_to_16_start:
        push    eax
        call    KernThunkStackTo16
        pop     eax

        ;jmp far dword NAME(%1 %+ _16) wrt CODE16
        db      066h
        db      0eah
        dw      NAME(%1 %+ _16) wrt CODE16
        dw      CODE16
.vboxsf_to_16_done_32:
%ifdef WITH_DWARF
segment _debug_frame
.fde_start:
        dd      (.fde_end - .fde_start) - 4
        dd      g_cie_thunk_back wrt _debug_frame
        dd      2 ; TEXT32 idx
        dd      NAME(%1 %+ _32) wrt TEXT32
        dd      .vboxsf_to_16_done_32 - NAME(%1 %+ _32)
        db      DW_CFA_advance_loc | 4
        db      DW_CFA_advance_loc | 2
        db      DW_CFA_advance_loc | 2
        db      DW_CFA_advance_loc | 5
        db      DW_CFA_advance_loc2                         ; Hack to easily cover the parameter conversion code.
        dw      .vboxsf_to_16_start - .vboxsf_to_32_end
        db      DW_CFA_advance_loc | 1
        db      DW_CFA_advance_loc | 5
        db      DW_CFA_advance_loc | 1
        db      DW_CFA_advance_loc | 6
        align 4, db DW_CFA_nop
.fde_end:
 %endif ; WITH_DWARF
segment CODE16
GLOBALNAME %1 %+ _16
DWARF_LABEL_CODE16 NAME(%1 %+ _16)

        ; Epilogue
        lea     sp, [bp - 4h]
        pop     es
        pop     ds
        mov     esp, ebp
        pop     ebp
%ifdef DEBUG
 %ifndef WITH_DWARF
        dec     ebp
 %endif
%endif
%endmacro

;;
; Thunks the given 16:16 pointer to a flat pointer, NULL is returned as NULL.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, edx, ecx
;
%macro VBOXSF_FARPTR_2_FLAT 2
        push    dword [ebp + (%1)]
        call    KernSelToFlat
        add     esp, 4h
        mov     [esp + (%2)], eax
%endmacro

;;
; Thunks the given 16:16 struct sffsd pointer to a flat pointer.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, ecx
;
%macro VBOXSF_PSFFSD_2_FLAT 2
%if 1 ; optimize later if we can.
        VBOXSF_FARPTR_2_FLAT %1, %2
%else
        lds     cx, [ebp + (%1)]
        and     ecx, 0ffffh
        mov     eax, dword [ecx]
        mov     cx, DATA32 wrt FLAT
        mov     [esp + (%2)], eax
        mov     ds, cx
%endif
%endmacro


;;
; Thunks the given 16:16 struct cdfsd pointer to a flat pointer.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, ecx
;
%macro VBOXSF_PCDFSD_2_FLAT 2
%if 1 ; optimize later if possible.
        VBOXSF_FARPTR_2_FLAT %1, %2
%else
        lds     cx, [ebp + (%1)]
        and     ecx, 0ffffh
        mov     eax, dword [ecx]
        mov     cx, DATA32 wrt FLAT
        mov     [esp + (%2)], eax
        mov     ds, cx
%endif
%endmacro

;;
; Thunks the given 16:16 struct fsfsd pointer to a flat pointer.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, ecx
;
%macro VBOXSF_PFSFSD_2_FLAT 2
%if 1 ; optimize later if possible.
        VBOXSF_FARPTR_2_FLAT %1, %2
%else
        lds     cx, [ebp + (%1)]
        and     ecx, 0ffffh
        mov     eax, dword [ecx]
        mov     cx, DATA32 wrt FLAT
        mov     [esp + (%2)], eax
        mov     ds, cx
%endif
%endmacro


;;
; Used for taking us from 32-bit and reserving a parameter frame.
;
; @param    %1      The function name
; @param    %2      The number of bytes to reserve
;
%macro VBOXSF_FROM_32 2
        ; prologue
        push    ebp
        mov     ebp, esp                    ; ebp
        push    ds                          ; ebp - 4
        push    es                          ; ebp - 8
        push    ebx                         ; ebp - 0ch
        push    esi                         ; ebp - 10h
        push    edi                         ; ebp - 14h

        ; Reserve the 32-bit parameter
        sub     esp, %2

        call    KernThunkStackTo16

        ;jmp far dword NAME(%1 %+ _16) wrt CODE16
        db      066h
        db      0eah
        dw      NAME(%1 %+ _16) wrt CODE16
        dw      CODE16
.vboxsf_from_32_end:

segment CODE16
GLOBALNAME %1 %+ _16
DWARF_LABEL_CODE16 NAME(%1 %+ _16)

%endmacro


;;
; Partially countering VBOXSF_FROM_32:
; Take us back to 32-bit mode, but don't do the epilogue stuff.
;
; @param    %1      The function name
;
%macro VBOXSF_FROM_16_SWITCH 1
.vboxsf_from_16_start:
        ;jmp far dword NAME(%i %+ _32) wrt FLAT
        db      066h
        db      0eah
        dd      NAME(%1 %+ _32) ;wrt FLAT
        dw      TEXT32 wrt FLAT
.vboxsf_from_16_done_16:

segment TEXT32
GLOBALNAME %1 %+ _32
DWARF_LABEL_TEXT32 NAME(%1 %+ _32)

        push    eax
        call    KernThunkStackTo32
        mov     ax, DATA32 wrt FLAT
        mov     ds, eax
        mov     es, eax
        pop     eax
%endmacro


;;
; Does the remaining recovery after VBOXSF_FROM_32.
;
%macro VBOXSF_FROM_16_EPILOGUE 0
        ; Epilogue
        lea     esp, [ebp - 14h]
        pop     edi
        pop     esi
        pop     ebx
        pop     es
        pop     ds
        cld
        mov     esp, ebp
        pop     ebp
%endmacro




;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
segment CODE32
extern KernThunkStackTo32
extern KernThunkStackTo16
extern KernSelToFlat
extern KernStrToUcs
extern KernStrFromUcs
segment CODE16
extern FSH_FORCENOSWAP
extern FSH_GETVOLPARM
extern DOS16WRITE

segment CODE32
extern NAME(FS32_ALLOCATEPAGESPACE)
extern NAME(FS32_ATTACH)
extern NAME(FS32_CANCELLOCKREQUEST)
extern NAME(FS32_CANCELLOCKREQUESTL)
extern NAME(FS32_CHDIR)
extern FS32_CHGFILEPTRL
extern NAME(FS32_CLOSE)
extern NAME(FS32_COMMIT)
extern NAME(FS32_COPY)
extern NAME(FS32_DELETE)
extern NAME(FS32_DOPAGEIO)
extern NAME(FS32_EXIT)
extern NAME(FS32_FILEATTRIBUTE)
extern NAME(FS32_FILEINFO)
extern NAME(FS32_FILEIO)
extern NAME(FS32_FILELOCKS)
extern NAME(FS32_FILELOCKSL)
extern NAME(FS32_FINDCLOSE)
extern NAME(FS32_FINDFIRST)
extern NAME(FS32_FINDFROMNAME)
extern NAME(FS32_FINDNEXT)
extern NAME(FS32_FINDNOTIFYCLOSE)
extern NAME(FS32_FINDNOTIFYFIRST)
extern NAME(FS32_FINDNOTIFYNEXT)
extern NAME(FS32_FLUSHBUF)
extern NAME(FS32_FSCTL)
extern NAME(FS32_FSINFO)
extern NAME(FS32_IOCTL)
extern NAME(FS32_MKDIR)
extern NAME(FS32_MOUNT)
extern NAME(FS32_MOVE)
extern NAME(FS32_NEWSIZEL)
extern NAME(FS32_NMPIPE)
extern NAME(FS32_OPENCREATE)
extern NAME(FS32_OPENPAGEFILE)
extern NAME(FS32_PATHINFO)
extern NAME(FS32_PROCESSNAME)
extern FS32_READ
extern NAME(FS32_RMDIR)
extern NAME(FS32_SETSWAP)
extern NAME(FS32_SHUTDOWN)
extern NAME(FS32_VERIFYUNCNAME)
extern FS32_WRITE

extern NAME(VBoxSFR0Init)



;*******************************************************************************
;*  Global Variables                                                           *
;*******************************************************************************
segment DATA16

;;
; The file system name.
global FS_NAME
FS_NAME:
        db 'VBOXSF',0

;;
; File system attributes
; The 32-bit version is only used to indicate that this is a 32-bit file system.
;
%define FSA_REMOTE      0001h           ; remote file system.
%define FSA_UNC         0002h           ; implements UNC.
%define FSA_LOCK        0004h           ; needs lock notification.
%define FSA_LVL7        0008h           ; accept level 7 (case preserving path request).
%define FSA_PSVR        0010h           ; (named) pipe server.
%define FSA_LARGEFILE   0020h           ; large file support.
align 16
global FS_ATTRIBUTE
global FS32_ATTRIBUTE
FS_ATTRIBUTE:
FS32_ATTRIBUTE:
        dd FSA_REMOTE + FSA_LARGEFILE + FSA_UNC + FSA_LVL7 ;+ FSA_LOCK

;; 64-bit mask.
; bit 0 - don't get the ring-0 spinlock.
; bit 6 - don't get the subsystem ring-0 spinlock.
global FS_MPSAFEFLAGS2
FS_MPSAFEFLAGS2:
        dd  0 ;1 | (1<<6) - not getting the ring-0 spinlock causes trouble, so dropping both for now.
        dd  0

;;
; Set after VBoxSFR0Init16Bit has been called.
GLOBALNAME g_fDoneRing0
        db 0

align 4
;;
; The device helper (IPRT expects this name).
; (This is set by FS_INIT.)
GLOBALNAME g_fpfnDevHlp
        dd 0

;;
; Whether initialization should be verbose or quiet.
GLOBALNAME g_fVerbose
        db 1

;; DEBUGGING DEBUGGING
GLOBALNAME g_u32Info
        dd 0

;; Far pointer to DOS16WRITE (corrected set before called).
; Just a 'temporary' hack to work around a wlink/nasm issue.
GLOBALNAME g_fpfnDos16Write
        dw  DOS16WRITE
        dw  seg DOS16WRITE

;;
; The attach dd data.
GLOBALNAME g_VBoxGuestAttachDD
        dd 0
        dw 0
        dd 0
        dw 0
;;
; The AttachDD name of the VBoxGuest.sys driver.
GLOBALNAME g_szVBoxGuestName
        db VBOXGUEST_DEVICE_NAME_SHORT, 0
;;
; The VBoxGuest IDC connection data.
GLOBALNAME g_VBoxGuestIDC
        times VBGLOS2ATTACHDD_size db 0

;;
; This must be present, we've got fixups against it.
segment DATA32
g_pfnDos16Write:
        dd  DOS16WRITE  ; flat







;
;
;  16-bit entry point thunking.
;  16-bit entry point thunking.
;  16-bit entry point thunking.
;
;
segment CODE16


;;
; @cproto int FS_ALLOCATEPAGESPACE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cb, USHORT cbWantContig)
VBOXSF_EP16_BEGIN   FS_ALLOCATEPAGESPACE, 'FS_ALLOCATEPAGESPACE'
VBOXSF_TO_32        FS_ALLOCATEPAGESPACE, 4*4
        movzx   ecx, word [ebp + 08h]       ; cbWantContig
        mov     [esp + 3*4], ecx
        mov     edx, [ebp + 0ah]            ; cb
        mov     [esp + 2*4], edx
        VBOXSF_PSFFSD_2_FLAT  0eh, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  12h, 0*4      ; psffsi
        call    NAME(FS32_ALLOCATEPAGESPACE)
VBOXSF_TO_16        FS_ALLOCATEPAGESPACE
        retf    0eh
VBOXSF_EP16_END     FS_ALLOCATEPAGESPACE

;;
; @cproto int FS_ATTACH(USHORT flag, PCSZ pszDev, PVPFSD pvpfsd, PCDFSD pcdfsd, PBYTE pszParm, PUSHORT pcbParm)
;
VBOXSF_EP16_BEGIN   FS_ATTACH, 'FS_ATTACH'
        ;
        ; Initialized ring-0 yet? (this is a likely first entry point)
        ;
        push    ds
        mov     ax, DATA16
        mov     ds, ax
        test    byte [NAME(g_fDoneRing0)], 1
        jnz     .DoneRing0
        call    NAME(VBoxSFR0Init16Bit)
.DoneRing0:
        pop     ds

VBOXSF_TO_32        FS_ATTACH, 6*4
        VBOXSF_FARPTR_2_FLAT  08h, 5*4      ; pcbParm
        VBOXSF_FARPTR_2_FLAT  0ch, 4*4      ; pszParm
        VBOXSF_FARPTR_2_FLAT  10h, 3*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  14h, 2*4      ; pvpfsd
        VBOXSF_FARPTR_2_FLAT  18h, 1*4      ; pszDev
        movzx   ecx, word [ebp + 1ch]       ; fFlag
        mov     [esp], ecx
        call    NAME(FS32_ATTACH)
VBOXSF_TO_16        FS_ATTACH
        retf    16h
VBOXSF_EP16_END     FS_ATTACH


;;
; @cproto int FS_CANCELLOCKREQUEST(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock far *pLockRange)
VBOXSF_EP16_BEGIN   FS_CANCELLOCKREQUEST, 'FS_CANCELLOCKREQUEST'
VBOXSF_TO_32        FS_CANCELLOCKREQUEST, 3*4
        VBOXSF_FARPTR_2_FLAT  08h, 2*4      ; pLockRange
        VBOXSF_PSFFSD_2_FLAT  0ch, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  10h, 0*4      ; psffsi
        call    NAME(FS32_CANCELLOCKREQUEST)
VBOXSF_TO_16        FS_CANCELLOCKREQUEST
        retf    0ch
VBOXSF_EP16_END     FS_CANCELLOCKREQUEST


;;
; @cproto int FS_CANCELLOCKREQUESTL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl far *pLockRange)
VBOXSF_EP16_BEGIN   FS_CANCELLOCKREQUESTL, 'FS_CANCELLOCKREQUESTL'
VBOXSF_TO_32        FS_CANCELLOCKREQUESTL, 3*4
        VBOXSF_FARPTR_2_FLAT  08h, 2*4      ; pLockRange
        VBOXSF_PSFFSD_2_FLAT  0ch, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  10h, 0*4      ; psffsi
        call    NAME(FS32_CANCELLOCKREQUESTL)
VBOXSF_TO_16        FS_CANCELLOCKREQUESTL
        retf    0ch
VBOXSF_EP16_END     FS_CANCELLOCKREQUESTL


;;
; @cproto int FS_CHDIR(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszDir, USHORT iCurDirEnd)
VBOXSF_EP16_BEGIN   FS_CHDIR, 'FS_CHDIR'
VBOXSF_TO_32        FS_CHDIR, 5*4
        movsx   ecx, word [ebp + 08h]       ; iCurDirEnd
        mov     [esp + 4*4], ecx
        VBOXSF_FARPTR_2_FLAT  0ah, 3*4      ; pszDir
        VBOXSF_FARPTR_2_FLAT  0eh, 2*4      ; pcdfsd (use slow thunk here, see flag)
        VBOXSF_FARPTR_2_FLAT  12h, 1*4      ; pcdfsi
        movzx   eax, word [ebp + 16h]       ; flag
        mov     [esp], eax
        call    NAME(FS32_CHDIR)
VBOXSF_TO_16        FS_CHDIR
        retf    10h
VBOXSF_EP16_END     FS_CHDIR


; @cproto int FS_CHGFILEPTR(PSFFSI psffsi, PVBOXSFFSD psffsd, LONG off, USHORT usMethod, USHORT IOflag)
VBOXSF_EP16_BEGIN   FS_CHGFILEPTR, 'FS_CHGFILEPTR'
VBOXSF_TO_32        FS_CHGFILEPTR, 6*4
        movzx   ecx, word [ebp + 08h]       ; IOflag
        mov     [esp + 5*4], ecx
        movzx   edx, word [ebp + 0ah]       ; usMethod
        mov     [esp + 4*4], edx
        mov     eax, [ebp + 0ch]            ; off
        mov     [esp + 2*4], eax
        rol     eax, 1                      ; high dword - is there a better way than this?
        and     eax, 1
        mov     edx, 0ffffffffh
        mul     edx
        mov     [esp + 3*4], eax
        VBOXSF_PSFFSD_2_FLAT  10h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  14h, 0*4      ; psffsi
        call    FS32_CHGFILEPTRL
VBOXSF_TO_16        FS_CHGFILEPTR
        retf    10h
VBOXSF_EP16_END     FS_CHGFILEPTR


;;
; @cproto int FS_CLOSE(USHORT type, USHORT IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
;
VBOXSF_EP16_BEGIN   FS_CLOSE, 'FS_CLOSE'
VBOXSF_TO_32        FS_CLOSE, 4*4
        VBOXSF_PSFFSD_2_FLAT  08h, 3*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  0ch, 2*4      ; psffsi
        movzx   ecx, word [ebp + 10h]       ; IOflag
        mov     [esp + 1*4], ecx
        movzx   edx, word [ebp + 12h]       ; type
        mov     [esp], edx
        call    NAME(FS32_CLOSE)
VBOXSF_TO_16        FS_CLOSE
        retf    0ch
VBOXSF_EP16_END     FS_CLOSE


;;
; @cproto int FS_COMMIT(USHORT type, USHORT IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
;
VBOXSF_EP16_BEGIN   FS_COMMIT, 'FS_COMMIT'
VBOXSF_TO_32        FS_COMMIT, 4*4
        VBOXSF_PSFFSD_2_FLAT  08h, 3*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  0ch, 2*4      ; psffsi
        movzx   ecx, word [ebp + 10h]       ; IOflag
        mov     [esp + 1*4], ecx
        movzx   edx, word [ebp + 12h]       ; type
        mov     [esp], edx
        call    NAME(FS32_COMMIT)
VBOXSF_TO_16        FS_COMMIT
        retf    0ch
VBOXSF_EP16_END     FS_COMMIT

;;
; @cproto int FS_COPY(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd
;                     PCSZ pszDst, USHORT iDstCurDirEnd, USHORT nameType);
VBOXSF_EP16_BEGIN   FS_COPY, 'FS_COPY'
VBOXSF_TO_32        FS_COPY, 8*4
        movzx   ecx, word [ebp + 08h]       ; flag
        mov     [esp + 7*4], ecx
        movsx   edx, word [ebp + 0ah]       ; iDstCurDirEnd
        mov     [esp + 6*4], edx
        VBOXSF_FARPTR_2_FLAT  0ch, 5*4      ; pszDst
        movsx   eax, word [ebp + 10h]       ; iSrcCurDirEnd
        mov     [esp + 4*4], eax
        VBOXSF_FARPTR_2_FLAT  12h, 3*4      ; pszSrc
        VBOXSF_PCDFSD_2_FLAT  16h, 2*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  1ah, 1*4      ; psffsi
        movzx   ecx, word [ebp + 1eh]       ; flag
        mov     [esp], ecx
        call    NAME(FS32_COPY)
VBOXSF_TO_16        FS_COPY
        retf    18h
VBOXSF_EP16_END     FS_COPY


;;
; @cproto int FS_DELETE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszFile, USHORT iCurDirEnd);
VBOXSF_EP16_BEGIN   FS_DELETE, 'FS_DELETE'
VBOXSF_TO_32        FS_DELETE, 4*4
        movsx   ecx, word [ebp + 08h]       ; iCurDirEnd
        mov     [esp + 3*4], ecx
        VBOXSF_FARPTR_2_FLAT  0ah, 2*4      ; pszFile
        VBOXSF_PCDFSD_2_FLAT  0eh, 1*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  12h, 0*4      ; pcdfsi
        call    NAME(FS32_DELETE)
VBOXSF_TO_16        FS_DELETE
        retf    0eh
VBOXSF_EP16_END FS_DELETE


;;
; @cproto int FS_DOPAGEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, struct PageCmdHeader far *pList)
VBOXSF_EP16_BEGIN   FS_DOPAGEIO, 'FS_DOPAGEIO'
VBOXSF_TO_32        FS_DOPAGEIO, 3*4
        VBOXSF_FARPTR_2_FLAT  08h, 2*4      ; pList
        VBOXSF_PSFFSD_2_FLAT  0ch, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  10h, 0*4      ; psffsi
        call    NAME(FS32_DOPAGEIO)
VBOXSF_TO_16        FS_DOPAGEIO
        retf    0ch
VBOXSF_EP16_END     FS_DOPAGEIO

;;
; @cproto void FS_EXIT(USHORT uid, USHORT pid, USHORT pdb)
VBOXSF_EP16_BEGIN   FS_EXIT, 'FS_EXIT'
        ;
        ; Initialized ring-0 yet? (this is a likely first entry point)
        ;
        push    ds
        mov     ax, DATA16
        mov     ds, ax
        test    byte [NAME(g_fDoneRing0)], 1
        jnz     .DoneRing0
        call    NAME(VBoxSFR0Init16Bit)
.DoneRing0:
        pop     ds

VBOXSF_TO_32        FS_EXIT, 3*4
        movzx   ecx, word [ebp + 08h]       ; pdb
        mov     [esp + 2*4], ecx
        movzx   edx, word [ebp + 0ah]       ; pib
        mov     [esp + 1*4], edx
        movzx   eax, word [ebp + 0ch]       ; uid
        mov     [esp], eax
        call    NAME(FS32_EXIT)
VBOXSF_TO_16        FS_EXIT
        retf    6h
VBOXSF_EP16_END     FS_EXIT


;;
; @cproto int FS_FILEATTRIBUTE(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, PUSHORT pAttr);
;
VBOXSF_EP16_BEGIN   FS_FILEATTRIBUTE, 'FS_FILEATTRIBUTE'
VBOXSF_TO_32        FS_FILEATTRIBUTE, 6*4
        VBOXSF_FARPTR_2_FLAT  08h, 5*4      ; pAttr
        movsx   ecx, word [ebp + 0ch]       ; iCurDirEnd - caller may pass 0xffff, so sign extend.
        mov     [esp + 4*4], ecx
        VBOXSF_FARPTR_2_FLAT  0eh, 3*4      ; pszName
        VBOXSF_PCDFSD_2_FLAT  12h, 2*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  16h, 1*4      ; pcdfsi
        movzx   edx, word [ebp + 1ah]       ; flag
        mov     [esp], edx
        call    NAME(FS32_FILEATTRIBUTE)
VBOXSF_TO_16        FS_FILEATTRIBUTE
        retf    14h
VBOXSF_EP16_END     FS_FILEATTRIBUTE


;;
; @cproto  int FS_FILEINFO(USHORT flag, PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT level,
;                          PBYTE pData, USHORT cbData, USHORT IOflag);
VBOXSF_EP16_BEGIN   FS_FILEINFO, 'FS_FILEINFO'
VBOXSF_TO_32        FS_FILEINFO, 7*4
        movzx   ecx, word [ebp + 08h]       ; IOflag
        mov     [esp + 6*4], ecx
        movzx   edx, word [ebp + 0ah]       ; cbData
        mov     [esp + 5*4], edx
        VBOXSF_FARPTR_2_FLAT  0ch, 4*4      ; pData
        movzx   eax, word [ebp + 10h]       ; level
        mov     [esp + 3*4], eax
        VBOXSF_PSFFSD_2_FLAT  12h, 2*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  16h, 1*4      ; psffsi
        movzx   ecx, word [ebp + 1ah]       ; flag
        mov     [esp], ecx
        call    NAME(FS32_FILEINFO)
VBOXSF_TO_16        FS_FILEINFO
        retf    14h
VBOXSF_EP16_END     FS_FILEINFO


;;
; @cproto  int FS_FILEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pCmdList, USHORT cbCmdList,
;                        PUSHORT poError, USHORT IOflag);
VBOXSF_EP16_BEGIN   FS_FILEIO, 'FS_FILEIO'
VBOXSF_TO_32        FS_FILEIO, 6*4
        movzx   ecx, word [ebp + 08h]       ; IOFlag
        mov     [esp + 5*4], ecx
        VBOXSF_FARPTR_2_FLAT  0ah, 4*4      ; poError
        movzx   edx, word [ebp + 0eh]       ; cbCmdList
        mov     [esp + 3*4], edx
        VBOXSF_FARPTR_2_FLAT  10h, 2*4      ; pCmdList
        VBOXSF_PSFFSD_2_FLAT  14h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  18h, 0*4      ; psffsi
        call    NAME(FS32_FILEIO)
VBOXSF_TO_16        FS_FILEIO
        retf    14h
VBOXSF_EP16_END     FS_FILEIO


;;
; @cproto  int FS_FILELOCKS(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock far *pUnLockRange
;                           struct filelock far *pLockRange, ULONG timeout, ULONG flags)
VBOXSF_EP16_BEGIN   FS_FILELOCKS, 'FS_FILELOCKS'
VBOXSF_TO_32        FS_FILELOCKS, 6*4
        mov     ecx, [ebp + 08h]            ; flags
        mov     [esp + 5*4], ecx
        mov     edx, [ebp + 0ch]            ; timeout
        mov     [esp + 4*4], edx
        VBOXSF_FARPTR_2_FLAT  10h, 3*4      ; pLockRange
        VBOXSF_FARPTR_2_FLAT  14h, 2*4      ; pUnLockRange
        VBOXSF_PSFFSD_2_FLAT  18h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  1ch, 0*4      ; psffsi
        call    NAME(FS32_FILELOCKS)
VBOXSF_TO_16        FS_FILELOCKS
        retf    18h
VBOXSF_EP16_END     FS_FILELOCKS


;;
; @cproto  int FS_FILELOCKSL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl far *pUnLockRange
;                            struct filelockl far *pLockRange, ULONG timeout, ULONG flags)
VBOXSF_EP16_BEGIN   FS_FILELOCKSL, 'FS_FILELOCKSL'
VBOXSF_TO_32        FS_FILELOCKSL, 6*4
        mov     ecx, [ebp + 08h]            ; flags
        mov     [esp + 5*4], ecx
        mov     edx, [ebp + 0ch]            ; timeout
        mov     [esp + 4*4], edx
        VBOXSF_FARPTR_2_FLAT  10h, 3*4      ; pLockRange
        VBOXSF_FARPTR_2_FLAT  14h, 2*4      ; pUnLockRange
        VBOXSF_PSFFSD_2_FLAT  18h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  1ch, 0*4      ; psffsi
        call    NAME(FS32_FILELOCKS)
VBOXSF_TO_16        FS_FILELOCKSL
        retf    18h
VBOXSF_EP16_END     FS_FILELOCKSL


;;
; @cproto int FS_FINDCLOSE(PFSFSI pfsfsi, PVBOXSFFS pfsfsd);
;
VBOXSF_EP16_BEGIN   FS_FINDCLOSE, 'FS_FINDCLOSE'
VBOXSF_TO_32        FS_FINDCLOSE, 2*4
        VBOXSF_PFSFSD_2_FLAT  08h, 1*4      ; pfsfsd
        VBOXSF_FARPTR_2_FLAT  0ch, 0*4      ; pfsfsi
        call    NAME(FS32_FINDCLOSE)
VBOXSF_TO_16        FS_FINDCLOSE
        retf    8h
VBOXSF_EP16_END     FS_FINDCLOSE


;;
; @cproto int FS_FINDFIRST(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, USHORT attr,
;                          PFSFSI pfsfsi, PVBOXSFFS pfsfsd, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                          USHORT level, USHORT flags);
;
VBOXSF_EP16_BEGIN   FS_FINDFIRST, 'FS_FINDFIRST'
VBOXSF_TO_32        FS_FINDFIRST, 12*4
        movzx   ecx, word [ebp + 08h]       ; flags
        mov     [esp + 11*4], ecx
        movzx   edx, word [ebp + 0ah]       ; level
        mov     [esp + 10*4], edx
        VBOXSF_FARPTR_2_FLAT  0ch, 9*4      ; pcMatch
        movzx   eax, word [ebp + 10h]       ; cbData
        mov     [esp + 8*4], eax
        VBOXSF_FARPTR_2_FLAT  12h, 7*4      ; pbData
        VBOXSF_FARPTR_2_FLAT  16h, 6*4      ; pfsfsd
        VBOXSF_FARPTR_2_FLAT  1ah, 5*4      ; pfsfsi
        movzx   ecx, word [ebp + 1eh]       ; attr
        mov     [esp + 4*4], ecx
        movsx   edx, word [ebp + 20h]       ; iCurDirEnd
        mov     [esp + 3*4], edx
        VBOXSF_FARPTR_2_FLAT  22h, 2*4      ; pszName
        VBOXSF_PCDFSD_2_FLAT  26h, 1*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  2ah, 0*4      ; pcdfsi
        call    NAME(FS32_FINDFIRST)
VBOXSF_TO_16        FS_FINDFIRST
        retf    26h
VBOXSF_EP16_END FS_FINDFIRST


;;
; @cproto int FS_FINDFROMNAME(PFSFSI pfsfsi, PVBOXSFFS pfsfsd, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                             USHORT level, ULONG position, PCSZ pszName, USHORT flag)
;
VBOXSF_EP16_BEGIN   FS_FINDFROMNAME, 'FS_FINDFROMNAME'
VBOXSF_TO_32        FS_FINDFROMNAME, 9*4
        movzx   ecx, word [ebp + 08h]       ; flags
        mov     [esp + 8*4], ecx
        VBOXSF_FARPTR_2_FLAT  0ah, 7*4      ; pszName
        mov     edx, [ebp + 0eh]            ; position
        mov     [esp + 6*4], edx
        movzx   eax, word [ebp + 12h]       ; level
        mov     [esp + 5*4], eax
        VBOXSF_FARPTR_2_FLAT  14h, 4*4      ; pcMatch
        movzx   eax, word [ebp + 18h]       ; cbData
        mov     [esp + 3*4], eax
        VBOXSF_FARPTR_2_FLAT  1ah, 2*4      ; pbData
        VBOXSF_PFSFSD_2_FLAT  1eh, 1*4      ; pfsfsd
        VBOXSF_FARPTR_2_FLAT  22h, 0*4      ; pfsfsi
        call    NAME(FS32_FINDFROMNAME)
VBOXSF_TO_16        FS_FINDFROMNAME
        retf    1eh
VBOXSF_EP16_END     FS_FINDFROMNAME


;;
; @cproto int FS_FINDNEXT(PFSFSI pfsfsi, PVBOXSFFS pfsfsd, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                         USHORT level, USHORT flag)
;
VBOXSF_EP16_BEGIN   FS_FINDNEXT, 'FS_FINDNEXT'
VBOXSF_TO_32        FS_FINDNEXT, 7*4
        movzx   ecx, word [ebp + 08h]       ; flags
        mov     [esp + 6*4], ecx
        movzx   eax, word [ebp + 0ah]       ; level
        mov     [esp + 5*4], eax
        VBOXSF_FARPTR_2_FLAT  0ch, 4*4      ; pcMatch
        movzx   eax, word [ebp + 10h]       ; cbData
        mov     [esp + 3*4], eax
        VBOXSF_FARPTR_2_FLAT  12h, 2*4      ; pbData
        VBOXSF_PFSFSD_2_FLAT  16h, 1*4      ; pfsfsd
        VBOXSF_FARPTR_2_FLAT  1ah, 0*4      ; pfsfsi
        call    NAME(FS32_FINDNEXT)
VBOXSF_TO_16        FS_FINDNEXT
        retf    16h
VBOXSF_EP16_END     FS_FINDNEXT


;;
; @cproto int FS_FINDNOTIFYCLOSE(USHORT handle);
;
VBOXSF_EP16_BEGIN   FS_FINDNOTIFYCLOSE, 'FS_FINDNOTIFYCLOSE'
VBOXSF_TO_32        FS_FINDNOTIFYCLOSE, 1*4
        movzx   ecx, word [ebp + 08h]       ; handle
        mov     [esp], ecx
        call    NAME(FS32_FINDNOTIFYCLOSE)
VBOXSF_TO_16        FS_FINDNOTIFYCLOSE
        retf    2h
VBOXSF_EP16_END     FS_FINDNOTIFYCLOSE


;;
; @cproto int FS_FINDNOTIFYFIRST(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, USHORT attr,
;                                PUSHORT pHandle, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                                USHORT level, USHORT flags);
;
VBOXSF_EP16_BEGIN   FS_FINDNOTIFYFIRST, 'FS_FINDNOTIFYFIRST'
VBOXSF_TO_32        FS_FINDNOTIFYFIRST, 11*4
        movzx   ecx, word [ebp + 08h]       ; flags
        mov     [esp + 10*4], ecx
        movzx   edx, word [ebp + 0ah]       ; level
        mov     [esp + 9*4], edx
        VBOXSF_FARPTR_2_FLAT  0ch, 8*4      ; pcMatch
        movzx   eax, word [ebp + 10h]       ; cbData
        mov     [esp + 7*4], eax
        VBOXSF_FARPTR_2_FLAT  12h, 6*4      ; pbData
        VBOXSF_FARPTR_2_FLAT  16h, 5*4      ; pHandle
        movzx   ecx, word [ebp + 1ah]       ; attr
        mov     [esp + 4*4], ecx
        movsx   edx, word [ebp + 1ch]       ; iCurDirEnd
        mov     [esp + 3*4], edx
        VBOXSF_FARPTR_2_FLAT  1eh, 2*4      ; pszName
        VBOXSF_PCDFSD_2_FLAT  22h, 1*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  26h, 0*4      ; pcdfsi
        call    NAME(FS32_FINDNOTIFYFIRST)
VBOXSF_TO_16        FS_FINDNOTIFYFIRST
        retf    22h
VBOXSF_EP16_END     FS_FINDNOTIFYFIRST


;;
; @cproto int FS_FINDNOTIFYNEXT(USHORT handle, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                               USHORT level, ULONG timeout)
;
VBOXSF_EP16_BEGIN FS_FINDNOTIFYNEXT, 'FS_FINDNOTIFYNEXT'
VBOXSF_TO_32        FS_FINDNOTIFYNEXT, 6*4
        mov     ecx, [ebp + 08h]            ; timeout
        mov     [esp + 5*4], ecx
        movzx   edx, word [ebp + 0ch]       ; level
        mov     [esp + 4*4], edx
        VBOXSF_FARPTR_2_FLAT  0eh, 3*4      ; pcMatch
        movzx   eax, word [ebp + 12h]       ; cbData
        mov     [esp + 2*4], eax
        VBOXSF_FARPTR_2_FLAT  14h, 1*4      ; pbData
        movzx   ecx, word [ebp + 18h]       ; handle
        mov     [esp], ecx
        call    NAME(FS32_FINDNOTIFYNEXT)
VBOXSF_TO_16        FS_FINDNOTIFYNEXT
        retf    12h
VBOXSF_EP16_END     FS_FINDNOTIFYNEXT


;; @cproto int FS_FLUSHBUF(USHORT hVPB, USHORT flag);
VBOXSF_EP16_BEGIN FS_FLUSHBUF, 'FS_FLUSHBUF'
VBOXSF_TO_32        FS_FLUSHBUF, 2*4
        movzx   edx, word [ebp + 08h]       ; flag
        mov     [esp + 1*4], edx
        movzx   eax, word [ebp + 0ch]       ; hVPB
        mov     [esp + 0*4], eax
        call    NAME(FS32_FLUSHBUF)
VBOXSF_TO_16        FS_FLUSHBUF
        retf    4h
VBOXSF_EP16_END FS_FLUSHBUF


;; @cproto int FS_FSCTL(union argdat far *pArgdat, USHORT iArgType, USHORT func,
;                       PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
;                       PVOID pData, USHORT lenData, PUSHORT plenDataIO);
VBOXSF_EP16_BEGIN FS_FSCTL, 'FS_FSCTL'
        ;
        ; Initialized ring-0 yet? (this is a likely first entry point)
        ;
        push    ds
        mov     ax, DATA16
        mov     ds, ax
        test    byte [NAME(g_fDoneRing0)], 1
        jnz     .DoneRing0
        call    NAME(VBoxSFR0Init16Bit)
.DoneRing0:
        pop     ds

VBOXSF_TO_32        FS_FSCTL, 9*4
        VBOXSF_FARPTR_2_FLAT  08h, 8*4      ; plenDataIO
        movzx   ecx, word [ebp + 0ch]       ; lenData
        mov     [esp + 7*4], ecx
        VBOXSF_FARPTR_2_FLAT  0eh, 6*4      ; pData
        VBOXSF_FARPTR_2_FLAT  12h, 5*4      ; plenDataIO
        movzx   ecx, word [ebp + 16h]       ; lenData
        mov     [esp + 4*4], ecx
        VBOXSF_FARPTR_2_FLAT  18h, 3*4      ; pData
        movzx   edx, word [ebp + 1ch]       ; func
        mov     [esp + 2*4], edx
        movzx   eax, word [ebp + 1eh]       ; iArgType
        mov     [esp + 1*4], eax
        VBOXSF_FARPTR_2_FLAT  20h, 0*4      ; pArgdat
        call    NAME(FS32_FSCTL)
VBOXSF_TO_16        FS_FSCTL
        retf    1ch
VBOXSF_EP16_END FS_FSCTL


;; @cproto int FS_FSINFO(USHORT flag, USHORT hVPB, PBYTE pbData, USHORT cbData, USHORT level)
VBOXSF_EP16_BEGIN FS_FSINFO, 'FS_FSINFO'
VBOXSF_TO_32        FS_FSINFO, 5*4
        movzx   ecx, word [ebp + 08h]       ; level
        mov     [esp + 10h], ecx
        movzx   edx, word [ebp + 0ah]       ; cbData
        mov     [esp + 0ch], edx
        VBOXSF_FARPTR_2_FLAT  0ch, 2*4      ; pbData
        movzx   edx, word [ebp + 10h]       ; hVPB
        mov     [esp + 4], edx
        movzx   eax, word [ebp + 12h]       ; flag
        mov     [esp], eax
        call    NAME(FS32_FSINFO)
VBOXSF_TO_16        FS_FSINFO
        retf    14h
VBOXSF_EP16_END     FS_FSINFO


;;
; @cproto int FS_IOCTL(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT cat, USHORT func,
;                      PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
;                      PVOID pData, USHORT lenData, PUSHORT plenDataIO);
VBOXSF_EP16_BEGIN   FS_IOCTL, 'FS_IOCTL'
VBOXSF_TO_32        FS_IOCTL, 10*4
        VBOXSF_FARPTR_2_FLAT  08h, 9*4      ; plenDataIO
        movzx   ecx, word [ebp + 0ch]       ; lenData
        mov     [esp + 8*4], ecx
        VBOXSF_FARPTR_2_FLAT  0eh, 7*4      ; pData
        VBOXSF_FARPTR_2_FLAT  12h, 6*4      ; plenDataIO
        movzx   ecx, word [ebp + 16h]       ; lenData
        mov     [esp + 5*4], ecx
        VBOXSF_FARPTR_2_FLAT  18h, 4*4      ; pData
        movzx   edx, word [ebp + 1ch]       ; cat
        mov     [esp + 3*4], edx
        movzx   eax, word [ebp + 1eh]       ; func
        mov     [esp + 2*4], eax
        VBOXSF_PSFFSD_2_FLAT  20h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  24h, 0*4      ; pData
        call    NAME(FS32_IOCTL)
VBOXSF_TO_16        FS_IOCTL
        retf    20h
VBOXSF_EP16_END     FS_IOCTL


;;
; @cproto int FS_MKDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
;                      PBYTE pEABuf, USHORT flag);
VBOXSF_EP16_BEGIN   FS_MKDIR, 'FS_MKDIR'
VBOXSF_TO_32        FS_MKDIR, 6*4
        movzx   ecx, word [ebp + 08h]       ; flag
        mov     [esp + 5*4], ecx
        VBOXSF_FARPTR_2_FLAT  0ah, 4*4      ; pEABuf
        movsx   edx, word [ebp + 0eh]       ; iCurDirEnd
        mov     [esp + 3*4], edx
        VBOXSF_FARPTR_2_FLAT  10h, 2*4      ; pszName
        VBOXSF_PCDFSD_2_FLAT  14h, 1*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  18h, 0*4      ; pcdfsi
        call    NAME(FS32_MKDIR)
VBOXSF_TO_16        FS_MKDIR
        retf    14h
VBOXSF_EP16_END     FS_MKDIR


;;
; @cproto int FS_MOUNT(USHORT flag, PVPFSI pvpfsi, PVBOXSFVP pvpfsd, USHORT hVPB, PCSZ pszBoot)
VBOXSF_EP16_BEGIN   FS_MOUNT, 'FS_MOUNT'
        ;
        ; Initialized ring-0 yet? (this is a likely first entry point)
        ;
        push    ds
        mov     ax, DATA16
        mov     ds, ax
        test    byte [NAME(g_fDoneRing0)], 1
        jnz     .DoneRing0
        call    NAME(VBoxSFR0Init16Bit)
.DoneRing0:
        pop     ds

VBOXSF_TO_32        FS_MOUNT, 5*4
        VBOXSF_FARPTR_2_FLAT  08h, 4*4      ; pszBoot
        movzx   ecx, word [ebp + 0ch]       ; hVPB
        mov     [esp + 3*4], ecx
        VBOXSF_FARPTR_2_FLAT  0eh, 2*4      ; pvpfsd
        VBOXSF_FARPTR_2_FLAT  12h, 1*4      ; pvpfsi
        movzx   ecx, word [ebp + 16h]       ; flag
        mov     [esp], ecx
        call    NAME(FS32_MOUNT)
VBOXSF_TO_16        FS_MOUNT
        retf    10h
VBOXSF_EP16_END     FS_MOUNT


;;
; @cproto int FS_MOVE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd
;                     PCSZ pszDst, USHORT iDstCurDirEnd, USHORT type)
VBOXSF_EP16_BEGIN   FS_MOVE, 'FS_MOVE'
VBOXSF_TO_32        FS_MOVE, 7*4
        movzx   ecx, word [ebp + 08h]       ; type
        mov     [esp + 6*4], ecx
        movzx   edx, word [ebp + 0ah]       ; iDstCurDirEnd
        mov     [esp + 5*4], edx
        VBOXSF_FARPTR_2_FLAT  0ch, 4*4      ; pszDst
        movzx   eax, word [ebp + 10h]       ; iSrcCurDirEnd
        mov     [esp + 3*4], eax
        VBOXSF_FARPTR_2_FLAT  12h, 2*4      ; pszSrc
        VBOXSF_PCDFSD_2_FLAT  16h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  1ah, 0*4      ; psffsi
        call    NAME(FS32_MOVE)
VBOXSF_TO_16        FS_MOVE
        retf    16h
VBOXSF_EP16_END     FS_MOVE


;;
; @cproto int FS_NEWSIZE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cbFile, USHORT IOflag);
VBOXSF_EP16_BEGIN   FS_NEWSIZE, 'FS_NEWSIZE'
VBOXSF_TO_32        FS_NEWSIZE, 5*4     ; thunking to longlong edition.
        movzx   ecx, word [ebp + 08h]       ; IOflag
        mov     [esp + 4*4], ecx
        mov     eax, [ebp + 0ah]            ; cbFile (ULONG -> LONGLONG)
        mov     dword [esp + 3*4], 0
        mov     [esp + 2*4], eax
        VBOXSF_PSFFSD_2_FLAT  0eh, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  12h, 0*4      ; psffsi
        call            NAME(FS32_NEWSIZEL)
VBOXSF_TO_16        FS_NEWSIZE
        retf    0eh
VBOXSF_EP16_END     FS_NEWSIZE


;;
; @cproto int FS_NEWSIZEL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG cbFile, USHORT IOflag);
VBOXSF_EP16_BEGIN FS_NEWSIZEL, 'FS_NEWSIZEL'
VBOXSF_TO_32        FS_NEWSIZEL, 5*4
        movzx   ecx, word [ebp + 08h]       ; IOflag
        mov     [esp + 4*4], ecx
        mov     eax, [ebp + 0ah]            ; cbFile
        mov     edx, [ebp + 0eh]
        mov     [esp + 3*4], edx
        mov     [esp + 2*4], eax
        VBOXSF_PSFFSD_2_FLAT  12h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  16h, 0*4      ; psffsi
        call            NAME(FS32_NEWSIZEL)
VBOXSF_TO_16        FS_NEWSIZEL
        retf    12h
VBOXSF_EP16_END FS_NEWSIZEL


;;
; @cproto int FS_NMPIPE(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT OpType, union npoper far *pOpRec,
;                       PBYTE pData, PCSZ pszName);
VBOXSF_EP16_BEGIN   FS_NMPIPE, 'FS_NMPIPE'
VBOXSF_TO_32        FS_NMPIPE, 6*4
        VBOXSF_FARPTR_2_FLAT  08h, 5*4      ; pszName
        VBOXSF_FARPTR_2_FLAT  0ch, 4*4      ; pData
        VBOXSF_FARPTR_2_FLAT  10h, 3*4      ; pOpRec
        movzx   ecx, word [ebp + 14h]       ; OpType
        mov     [esp + 2*4], ecx
        VBOXSF_FARPTR_2_FLAT  16h, 1*4      ; psffsd (take care...)
        VBOXSF_FARPTR_2_FLAT  1ah, 0*4      ; psffsi
        call            NAME(FS32_NMPIPE)
VBOXSF_TO_16        FS_NMPIPE
        retf    16h
VBOXSF_EP16_END     FS_NMPIPE


;;
; @cproto int FS_OPENCREATE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
;                           PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG ulOpenMode, USHORT usOpenFlag,
;                           PUSHORT pusAction, USHORT usAttr, PBYTE pcEABuf, PUSHORT pfgenflag);
VBOXSF_EP16_BEGIN   FS_OPENCREATE, 'FS_OPENCREATE'
VBOXSF_TO_32        FS_OPENCREATE, 12*4
        VBOXSF_FARPTR_2_FLAT  08h, 11*4     ; pfgenflag
        VBOXSF_FARPTR_2_FLAT  0ch, 10*4     ; pcEABuf
        movzx   ecx, word [ebp + 10h]       ; usAttr
        mov     [esp + 9*4], ecx
        VBOXSF_FARPTR_2_FLAT  12h, 8*4      ; pusAction
        movzx   edx, word [ebp + 16h]       ; usOpenFlag
        mov     [esp + 7*4], edx
        mov     eax, [ebp + 18h]            ; ulOpenMode
        mov     [esp + 6*4], eax
        VBOXSF_FARPTR_2_FLAT  1ch, 5*4      ; psffsd (new, no short cuts)
        VBOXSF_FARPTR_2_FLAT  20h, 4*4      ; psffsi
        movsx   ecx, word [ebp + 24h]       ; iCurDirEnd
        mov     [esp + 3*4], ecx
        VBOXSF_FARPTR_2_FLAT  26h, 2*4      ; pszName
        VBOXSF_PCDFSD_2_FLAT  2ah, 1*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  2eh, 0*4      ; pcdfsi
        call    NAME(FS32_OPENCREATE)
VBOXSF_TO_16        FS_OPENCREATE
        retf    42
VBOXSF_EP16_END FS_OPENCREATE


;;
; @cproto int FS_OPENPAGEFILE(PULONG pFlag, PULONG pcMaxReq, PCSZ pszName, PSFFSI psffsi, PVBOXSFFSD psffsd,
;                             USHORT ulOpenMode, USHORT usOpenFlag, USHORT usAttr, ULONG Reserved)
VBOXSF_EP16_BEGIN   FS_OPENPAGEFILE, 'FS_OPENPAGEFILE'
VBOXSF_TO_32        FS_OPENPAGEFILE, 9*4
        mov     ecx, [ebp + 08h]            ; Reserved
        mov     [esp + 8*4], ecx
        movzx   edx, word [ebp + 0ch]       ; usAttr
        mov     [esp + 7*4], edx
        movzx   eax, word [ebp + 0eh]       ; usOpenFlag
        mov     [esp + 6*4], eax
        movzx   ecx, word [ebp + 10h]       ; usOpenMode
        mov     [esp + 5*4], ecx
        VBOXSF_FARPTR_2_FLAT  12h, 4*4      ; psffsd (new, no short cuts)
        VBOXSF_FARPTR_2_FLAT  16h, 3*4      ; psffsi
        VBOXSF_FARPTR_2_FLAT  1ah, 2*4      ; pszName
        VBOXSF_FARPTR_2_FLAT  1eh, 1*4      ; pcMaxReq
        VBOXSF_FARPTR_2_FLAT  22h, 0*4      ; pFlag
        call    NAME(FS32_OPENPAGEFILE)
VBOXSF_TO_16        FS_OPENPAGEFILE
        retf    1eh
VBOXSF_EP16_END     FS_OPENPAGEFILE


;;
; @cproto int FS_PATHINFO(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnt,
;                         USHORT level, PBYTE pData, USHORT cbData);
VBOXSF_EP16_BEGIN   FS_PATHINFO, 'FS_PATHINFO'
VBOXSF_TO_32        FS_PATHINFO, 8*4
        movzx   ecx, word [ebp + 08h]       ; cbData
        mov     [esp + 7*4], ecx
        VBOXSF_FARPTR_2_FLAT  0ah, 6*4      ; pData
        movzx   edx, word [ebp + 0eh]       ; level
        mov     [esp + 5*4], edx
        movsx   eax, word [ebp + 10h]       ; iCurDirEnd
        mov     [esp + 4*4], eax
        VBOXSF_FARPTR_2_FLAT  12h, 3*4      ; pszName
        VBOXSF_PCDFSD_2_FLAT  16h, 2*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  1ah, 1*4      ; pcdfsi
        movzx   edx, word [ebp + 1eh]       ; flag
        mov     [esp], edx
        call    NAME(FS32_PATHINFO)
VBOXSF_TO_16        FS_PATHINFO
        retf    18h
VBOXSF_EP16_END     FS_PATHINFO


;; @cproto int FS_PROCESSNAME(PSZ pszName);
VBOXSF_EP16_BEGIN FS_PROCESSNAME, 'FS_PROCESSNAME'
VBOXSF_TO_32        FS_PROCESSNAME, 1*4
        VBOXSF_FARPTR_2_FLAT  08h, 0*4      ; pszName
        call    NAME(FS32_PROCESSNAME)
VBOXSF_TO_16        FS_PROCESSNAME
        retf    4h
VBOXSF_EP16_END FS_PROCESSNAME


;;
; @cproto int FS_READ(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pbData, PUSHORT pcbData, USHORT IOflag)
VBOXSF_EP16_BEGIN   FS_READ, 'FS_READ'
VBOXSF_TO_32        FS_READ, 6*4        ; extra local for ULONG cbDataTmp.
        movzx   ecx, word [ebp + 08h]       ; IOflag
        mov     [esp + 4*4], ecx
        les     dx, [ebp + 0ah]             ; cbDataTmp = *pcbData;
        movzx   edx, dx
        lea     ecx, [esp + 5*4]            ; pcbData = &cbDataTmp
        movzx   eax, word [es:edx]
        mov     [ecx], eax
        mov     [esp + 3*4], ecx
        mov     edx, DATA32
        mov     es, edx
        VBOXSF_FARPTR_2_FLAT  0eh, 2*4      ; pbData
        VBOXSF_PSFFSD_2_FLAT  12h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  16h, 0*4      ; psffsi
        call    FS32_READ

        les     dx, [ebp + 0ah]             ; *pcbData = cbDataTmp;
        movzx   edx, dx
        mov     cx, [esp + 5*4]
        mov     [es:edx], cx
        mov     edx, DATA32
        mov     es, edx

VBOXSF_TO_16        FS_READ
        retf    12h
VBOXSF_EP16_END     FS_READ


;;
; @cproto int FS_RMDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd);
;
VBOXSF_EP16_BEGIN   FS_RMDIR, 'FS_RMDIR'
VBOXSF_TO_32        FS_RMDIR, 4*4
        movsx   edx, word [ebp + 08h]       ; iCurDirEnd
        mov     [esp + 3*4], edx
        VBOXSF_FARPTR_2_FLAT  0ah, 2*4      ; pszName
        VBOXSF_PCDFSD_2_FLAT  0eh, 1*4      ; pcdfsd
        VBOXSF_FARPTR_2_FLAT  12h, 0*4      ; pcdfsi
        call    NAME(FS32_RMDIR)
VBOXSF_TO_16        FS_RMDIR
        retf    0eh
VBOXSF_EP16_END     FS_RMDIR


;;
; @cproto int FS_SETSWAP(PSFFSI psffsi, PVBOXSFFSD psffsd);
;
VBOXSF_EP16_BEGIN FS_SETSWAP, 'FS_SETSWAP'
VBOXSF_TO_32        FS_SETSWAP, 2*4
        VBOXSF_PSFFSD_2_FLAT  08h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  0ch, 0*4      ; psffsi
        call    NAME(FS32_SETSWAP)
VBOXSF_TO_16        FS_SETSWAP
        retf    8h
VBOXSF_EP16_END     FS_SETSWAP


;;
; @cproto int FS_SHUTDOWN(USHORT type, ULONG reserved);
;
VBOXSF_EP16_BEGIN   FS_SHUTDOWN, 'FS_SHUTDOWN'
VBOXSF_TO_32        FS_SHUTDOWN, 3*4
        mov     ecx, [ebp + 0ch]            ; type
        mov     [esp + 1*4], edx
        movzx   edx, word [ebp + 08h]       ; reserved
        mov     [esp], eax
        call    NAME(FS32_SHUTDOWN)
VBOXSF_TO_16        FS_SHUTDOWN
        retf    6h
VBOXSF_EP16_END     FS_SHUTDOWN


;;
; @cproto int FS_VERIFYUNCNAME(USHORT type, PCSZ pszName);
;
VBOXSF_EP16_BEGIN   FS_VERIFYUNCNAME, 'FS_VERIFYUNCNAME'
VBOXSF_TO_32        FS_VERIFYUNCNAME, 3*4
        VBOXSF_FARPTR_2_FLAT  08h, 1*4      ; pszDev
        movzx   ecx, word [ebp + 0ch]       ; fFlag
        mov     [esp], ecx
        call    NAME(FS32_VERIFYUNCNAME)
VBOXSF_TO_16        FS_VERIFYUNCNAME
        retf    6h
VBOXSF_EP16_END     FS_VERIFYUNCNAME


;;
; @cproto int FS_WRITE(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pbData, PUSHORT pcbData, USHORT IOflag)
VBOXSF_EP16_BEGIN   FS_WRITE, 'FS_WRITE'
VBOXSF_TO_32        FS_WRITE, 6*4       ; extra local for ULONG cbDataTmp.
        movzx   ecx, word [ebp + 08h]       ; IOflag
        mov     [esp + 4*4], ecx
        les     dx, [ebp + 0ah]             ; cbDataTmp = *pcbData;
        movzx   edx, dx
        lea     ecx, [esp + 5*4]            ; pcbData = &cbDataTmp
        movzx   eax, word [es:edx]
        mov     [ecx], eax
        mov     [esp + 3*4], ecx
        mov     edx, DATA32
        mov     es, edx
        VBOXSF_FARPTR_2_FLAT  0eh, 2*4      ; pbData
        VBOXSF_PSFFSD_2_FLAT  12h, 1*4      ; psffsd
        VBOXSF_FARPTR_2_FLAT  16h, 0*4      ; psffsi
        call    FS32_WRITE

        les     dx, [ebp + 0ah]             ; *pcbData = cbDataTmp;
        movzx   edx, dx
        mov     cx, [esp + 5*4]
        mov     [es:edx], cx
        mov     edx, DATA32
        mov     es, edx

VBOXSF_TO_16        FS_WRITE
        retf    12h
VBOXSF_EP16_END     FS_WRITE


;
;
; Calling 16-bit kernel code.
;
;

BEGINCODE

;;
; Wrapper around FSH_GETVOLPARM.
;
; @returns  VPBFSD.
; @param    hVbp            The volume handle to resolve.
; @param    ppVbpFsi
;
BEGINPROC       Fsh32GetVolParams
VBOXSF_FROM_32  Fsh32GetVolParams, 2*4
        mov     di, sp                  ; make the top of the stack addressable via di

        mov     [ss:di], eax            ; clear the return variables
        mov     [ss:di + 4], eax

        mov     ax, [bp + 8]            ; hVbp
        push    ax

        lea     ax, [ss:di]             ; &hvfsi
        push    ss
        push    ax

        lea     ax, [ss:di + 4]         ; &hvfsd
        push    ss
        push    ax

        call far FSH_GETVOLPARM

        mov     sp, di                  ; paranoia (pascal pops params)

VBOXSF_FROM_16_SWITCH Fsh32GetVolParams

        ; Convert vpfsi to flat and store it in return location.
        mov     ebx, [ebp + 0ch]
        test    ebx, ebx
        jz      .no_vpfsi
        call    KernSelToFlat
        mov     [ebx], eax
.no_vpfsi:
        add     esp, 4

        ; Convert vpfsd to flat and return it.
        call    KernSelToFlat

VBOXSF_FROM_16_EPILOGUE
        ret
ENDPROC     Fsh32GetVolParams



;
;
; Calling 32-bit kernel code.
;
;

BEGINCODE

;;
; Wraps APIRET APIENTRY KernStrToUcs(PUconvObj, UniChar *, char *, LONG, LONG),
; to preserve ES.  ES get trashed in some cases (probably conversion table init).
;
BEGINPROC   SafeKernStrToUcs
DWARF_LABEL_TEXT32 NAME(SafeKernStrToUcs)
        push    ebp
        mov     ebp, esp
        push    es
        push    ds

        push    dword [ebp + 18h]
        push    dword [ebp + 14h]
        push    dword [ebp + 10h]
        push    dword [ebp + 0ch]
        push    dword [ebp + 08h]
        call    KernStrToUcs

        lea     esp, [ebp - 8]
        pop     ds
        pop     es
        cld                             ; just to be on the safe side
        leave
        ret
ENDPROC     SafeKernStrToUcs


;;
; Wraps APIRET APIENTRY KernStrFromUcs(PUconvObj, char *, UniChar *, LONG, LONG),
; to preserve ES.  ES get trashed in some cases (probably conversion table init).
;
BEGINPROC   SafeKernStrFromUcs
DWARF_LABEL_TEXT32 NAME(SafeKernStrFromUcs)
        push    ebp
        mov     ebp, esp
        push    es
        push    ds

        push    dword [ebp + 18h]
        push    dword [ebp + 14h]
        push    dword [ebp + 10h]
        push    dword [ebp + 0ch]
        push    dword [ebp + 08h]
        call    KernStrFromUcs

        lea     esp, [ebp - 8]
        pop     ds
        pop     es
        cld                             ; just to be on the safe side
        leave
        ret
ENDPROC     SafeKernStrFromUcs



;
;
;  Init code starts here
;  Init code starts here
;  Init code starts here
;
;


;;
; Ring-3 Init (16-bit).
;
; @param    pMiniFS         [bp + 08h]      The mini-FSD. (NULL)
; @param    fpfnDevHlp      [bp + 0ch]      The address of the DevHlp router.
; @param    pszCmdLine      [bp + 10h]      The config.sys command line.
;
VBOXSF_EP16_BEGIN FS_INIT, 'FS_INIT'
;    DEBUG_STR16 'VBoxSF: FS_INIT - enter'
        push    ebp
        mov     ebp, esp
        push    ds                          ; bp - 02h
        push    es                          ; bp - 04h
        push    esi                         ; bp - 08h
        push    edi                         ; bp - 0ch

        mov     ax, DATA16
        mov     ds, ax
        mov     es, ax

        ;
        ; Save the device help entry point.
        ;
        mov     eax, [bp + 0ch]
        mov     [NAME(g_fpfnDevHlp)], eax

        ;
        ; Parse the command line.
        ; Doing this in assembly is kind of ugly...
        ;
        cmp     word [bp + 10h + 2], 3
        jbe near .no_command_line
        lds     si, [bp + 10h]              ; ds:si -> command line iterator.
.parse_next:

        ; skip leading blanks.
.parse_next_char:
        mov     di, si                      ; DI = start of argument.
        lodsb
        cmp     al, ' '
        je      .parse_next_char
        cmp     al, 9                       ; tab
        je      .parse_next_char
        cmp     al, 0
        je near .parse_done

        ; check for '/' or '-'
        cmp     al, '/'
        je      .parse_switch
        cmp     al, '-'
        je      .parse_switch
        jmp     .parse_error

        ; parse switches.
.parse_switch:
        lodsb
        cmp     al, 0
        je      .parse_error
        and     al, ~20h                    ; uppercase

        cmp     al, 'V'                     ; /V - verbose
        je      .parse_verbose
        cmp     al, 'Q'                     ; /Q - quiet.
        je      .parse_quiet
        jmp     .parse_error

.parse_verbose:
        mov     byte [es:NAME(g_fVerbose)], 1
        jmp     .parse_next

.parse_quiet:
        mov     byte [es:NAME(g_fVerbose)], 0
        jmp     .parse_next

.parse_error:
segment DATA16
.szSyntaxError:
        db 0dh, 0ah, 'VBoxSF.ifs: command line parse error at: ', 0
.szNewLine:
        db 0dh, 0ah, 0dh, 0ah, 0
segment CODE16
        mov     bx, .szSyntaxError
        call    NAME(FS_INIT_FPUTS)

        push    es
        push    ds
        pop     es
        mov     bx, di
        call    NAME(FS_INIT_FPUTS)
        pop     es

        mov     bx, .szNewLine
        call    NAME(FS_INIT_FPUTS)

        mov     ax, ERROR_INVALID_PARAMETER
        jmp     .done

.parse_done:
        mov     ax, DATA16
        mov     ds, ax
.no_command_line:

        ;
        ; Write our greeting to STDOUT.
        ; APIRET  _Pascal DosWrite(HFILE hf, PVOID pvBuf, USHORT cbBuf, PUSHORT pcbBytesWritten);
        ;
        cmp     byte [NAME(g_fVerbose)], 0
        je near .quiet
segment DATA16
.szMessage:
        db 'VirtualBox Guest Additions IFS for OS/2 version ', VBOX_VERSION_STRING, ' r', VBOX_SVN_REV_STR, 0dh, 0ah, 0
segment CODE16
        mov     bx, .szMessage
        call    NAME(FS_INIT_FPUTS)
.quiet:

        ; return success.
        xor     eax, eax
.done:
        lea     sp, [bp - 0ch]
        pop     edi
        pop     esi
        pop     es
        pop     ds
        mov     esp, ebp
        pop     ebp
        DEBUG_STR16 'VBoxSF: FS_INIT - leave'
        retf    0ch
VBOXSF_EP16_END FS_INIT


;;
; Dos16Write wrapper.
;
; @param    es:bx       String to print. (zero terminated)
; @uses     nothing.
GLOBALNAME FS_INIT_FPUTS
        push    bp
        mov     bp, sp
        push    es                          ; bp - 02h
        push    ds                          ; bp - 04h
        push    ax                          ; bp - 06h
        push    bx                          ; bp - 08h
        push    cx                          ; bp - 0ah
        push    dx                          ; bp - 0ch
        push    si                          ; bp - 0eh
        push    di                          ; bp - 10h

        ; cx = strlen(es:bx)
        xor     al, al
        mov     di, bx
        mov     cx, 0ffffh
        cld
        repne scasb
        not     cx
        dec     cx

        ; APIRET  _Pascal DosWrite(HFILE hf, PVOID pvBuf, USHORT cbBuf, PUSHORT pcbBytesWritten);
        push    cx
        mov     ax, sp                      ; cbBytesWritten
        push    1                           ; STDOUT
        push    es                          ; pvBuf
        push    bx
        push    cx                          ; cbBuf
        push    ss                          ; pcbBytesWritten
        push    ax
%if 0 ; wlink/nasm generates a non-aliased fixup here which results in 16-bit offset with the flat 32-bit selector.
        call far DOS16WRITE
%else
        ; convert flat pointer to a far pointer using the tiled algorithm.
        mov     ax, DATA32 wrt FLAT
        mov     ds, ax
        mov     eax, g_pfnDos16Write wrt FLAT
        movzx   eax, word [eax + 2]                     ; High word of the flat address (in DATA32).
        shl     ax, 3
        or      ax, 0007h
        mov     dx, DATA16
        mov     ds, dx
        mov     [NAME(g_fpfnDos16Write) + 2], ax        ; Update the selector (in DATA16).
        ; do the call
        call far [NAME(g_fpfnDos16Write)]
%endif

        lea     sp, [bp - 10h]
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     ds
        pop     es
        pop     bp
        ret
ENDPROC FS_INIT_FPUTS



;;
; 16-bit ring-0 init routine.
;
; Called from various entrypoints likely to be the first to be invoked.
;
GLOBALNAME VBoxSFR0Init16Bit
        DEBUG_STR16 'VBoxSF: VBoxSFR0Init16Bit - enter'
        push    ds
        push    es
        push    fs
        push    gs
        push    esi
        push    edi
        push    ebp
        mov     ebp, esp
        and     sp, 0fffch

        ;
        ; Only try once.
        ;
        mov     ax, DATA16
        mov     ds, ax
        mov     byte [NAME(g_fDoneRing0)], 1

        ;
        ; Try attach to the VBoxGuest driver.
        ;
        mov     bx, NAME(g_szVBoxGuestName)
        mov     di, NAME(g_VBoxGuestAttachDD)
        mov     dl, DevHlp_AttachDD
        call far [NAME(g_fpfnDevHlp)]
        jc      .attach_attempt_done

        push    seg NAME(g_VBoxGuestIDC)
        push    NAME(g_VBoxGuestIDC)
        call far [NAME(g_VBoxGuestAttachDD) + 6]
.attach_attempt_done:

%ifndef DONT_LOCK_SEGMENTS
        ;
        ; Lock the two 16-bit segments.
        ;
        push    DATA16
        call far FSH_FORCENOSWAP
        push    CODE16
        call far FSH_FORCENOSWAP
        ; Wonder if this'll work if wlink could mark the two segments as ALIASed...
        ;push DATA32
        ;call far FSH_FORCENOSWAP
        ;push TEXT32
        ;call far FSH_FORCENOSWAP
%endif

        ;
        ; Do 32-bit ring-0 init.
        ;
        ;jmp far dword NAME(VBoxSFR0Init16Bit_32) wrt FLAT
        db      066h
        db      0eah
        dd      NAME(VBoxSFR0Init16Bit_32) ;wrt FLAT
        dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME VBoxSFR0Init16Bit_32
        mov     ax, DATA32 wrt FLAT
        mov     ds, ax
        mov     es, ax

        call    KernThunkStackTo32
        call    NAME(VBoxSFR0Init)
        call    KernThunkStackTo16

        ;jmp far dword NAME(VBoxSFR0Init16Bit_16) wrt CODE16
        db      066h
        db      0eah
        dw      NAME(VBoxSFR0Init16Bit_16) wrt CODE16
        dw      CODE16
segment CODE16
GLOBALNAME VBoxSFR0Init16Bit_16

        mov     esp, ebp
        pop     ebp
        pop     edi
        pop     esi
        pop     gs
        pop     fs
        pop     es
        pop     ds
        DEBUG_STR16 'VBoxSF: VBoxSFR0Init16Bit - leave'
        ret
ENDPROC VBoxSFR0Init16Bit


%ifdef DEBUG
;;
; print the string which offset is in AX (it's in the data segment).
; @uses AX
;
GLOBALNAME dbgstr16
        push    ds
        push    ebx
        push    edx

        mov     bx, ax
        mov     dx, 0504h                   ; RTLOG_DEBUG_PORT
        mov     ax, DATA16
        mov     ds, ax

.next:
        mov     al, [bx]
        or      al, al
        jz      .done
        inc     bx
        out     dx, al
        jmp     .next

.done:
        pop     edx
        pop     ebx
        pop     ds
        ret
ENDPROC dbgstr16
%endif


%ifdef WITH_DWARF
;
; Close debug info
;
segment _debug_info
        db  0
g_dwarf_compile_unit_end:
%endif

