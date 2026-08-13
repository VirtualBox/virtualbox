; $Id: VBoxVgaBiosAlternative286.asm 115036 2026-08-13 17:41:02Z aleksey.ilyushin@oracle.com $
;; @file
; Auto Generated source file. Do not edit.
;

;
; Source file: vgarom.asm
;
;  ============================================================================================
;
;   Copyright (C) 2001,2002 the LGPL VGABios developers Team
;
;   This library is free software; you can redistribute it and/or
;   modify it under the terms of the GNU Lesser General Public
;   License as published by the Free Software Foundation; either
;   version 2 of the License, or (at your option) any later version.
;
;   This library is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;   Lesser General Public License for more details.
;
;   You should have received a copy of the GNU Lesser General Public
;   License along with this library; if not, write to the Free Software
;   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
;
;  ============================================================================================
;
;   This VGA Bios is specific to the plex86/bochs Emulated VGA card.
;   You can NOT drive any physical vga card with it.
;
;  ============================================================================================
;

;
; Source file: vberom.asm
;
;  ============================================================================================
;
;   Copyright (C) 2002 Jeroen Janssen
;
;   This library is free software; you can redistribute it and/or
;   modify it under the terms of the GNU Lesser General Public
;   License as published by the Free Software Foundation; either
;   version 2 of the License, or (at your option) any later version.
;
;   This library is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;   Lesser General Public License for more details.
;
;   You should have received a copy of the GNU Lesser General Public
;   License along with this library; if not, write to the Free Software
;   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
;
;  ============================================================================================
;
;   This VBE is part of the VGA Bios specific to the plex86/bochs Emulated VGA card.
;   You can NOT drive any physical vga card with it.
;
;  ============================================================================================
;
;   This VBE Bios is based on information taken from :
;    - VESA BIOS EXTENSION (VBE) Core Functions Standard Version 3.0 located at www.vesa.org
;
;  ============================================================================================

;
; Source file: vgabios.c
;
;  // ============================================================================================
;
;  vgabios.c
;
;  // ============================================================================================
;  //
;  //  Copyright (C) 2001,2002 the LGPL VGABios developers Team
;  //
;  //  This library is free software; you can redistribute it and/or
;  //  modify it under the terms of the GNU Lesser General Public
;  //  License as published by the Free Software Foundation; either
;  //  version 2 of the License, or (at your option) any later version.
;  //
;  //  This library is distributed in the hope that it will be useful,
;  //  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  //  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;  //  Lesser General Public License for more details.
;  //
;  //  You should have received a copy of the GNU Lesser General Public
;  //  License along with this library; if not, write to the Free Software
;  //  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
;  //
;  // ============================================================================================
;  //
;  //  This VGA Bios is specific to the plex86/bochs Emulated VGA card.
;  //  You can NOT drive any physical vga card with it.
;  //
;  // ============================================================================================
;  //
;  //  This file contains code ripped from :
;  //   - rombios.c of plex86
;  //
;  //  This VGA Bios contains fonts from :
;  //   - fntcol16.zip (c) by Joseph Gil avalable at :
;  //      ftp://ftp.simtel.net/pub/simtelnet/msdos/screen/fntcol16.zip
;  //     These fonts are public domain
;  //
;  //  This VGA Bios is based on information taken from :
;  //   - Kevin Lawton's vga card emulation for bochs/plex86
;  //   - Ralf Brown's interrupts list available at http://www.cs.cmu.edu/afs/cs/user/ralf/pub/WWW/files.html
;  //   - Finn Thogersons' VGADOC4b available at http://home.worldonline.dk/~finth/
;  //   - Michael Abrash's Graphics Programming Black Book
;  //   - Francois Gervais' book "programmation des cartes graphiques cga-ega-vga" edited by sybex
;  //   - DOSEMU 1.0.1 source code for several tables values and formulas
;  //
;  // Thanks for patches, comments and ideas to :
;  //   - techt@pikeonline.net
;  //
;  // ============================================================================================

;
; Source file: vbe.c
;
;  // ============================================================================================
;  //
;  //  Copyright (C) 2002 Jeroen Janssen
;  //
;  //  This library is free software; you can redistribute it and/or
;  //  modify it under the terms of the GNU Lesser General Public
;  //  License as published by the Free Software Foundation; either
;  //  version 2 of the License, or (at your option) any later version.
;  //
;  //  This library is distributed in the hope that it will be useful,
;  //  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  //  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;  //  Lesser General Public License for more details.
;  //
;  //  You should have received a copy of the GNU Lesser General Public
;  //  License along with this library; if not, write to the Free Software
;  //  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
;  //
;  // ============================================================================================
;  //
;  //  This VBE is part of the VGA Bios specific to the plex86/bochs Emulated VGA card.
;  //  You can NOT drive any physical vga card with it.
;  //
;  // ============================================================================================
;  //
;  //  This VBE Bios is based on information taken from :
;  //   - VESA BIOS EXTENSION (VBE) Core Functions Standard Version 3.0 located at www.vesa.org
;  //
;  // ============================================================================================

;
; Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
; other than GPL or LGPL is available it will apply instead, Oracle elects to use only
; the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
; a choice of LGPL license versions is made available with the language indicating
; that LGPLv2 or any later version may be used, or where a choice of which version
; of the LGPL is applied is otherwise unspecified.
;





section VGAROM progbits vstart=0x0 align=1 ; size=0x8fa class=CODE group=AUTO
  ; disGetNextSymbol 0xc0000 LB 0x8fa -> off=0x28 cb=0000000000000548 uValue=00000000000c0028 'vgabios_int10_handler'
    db  055h, 0aah, 040h, 0ebh, 01dh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 049h, 042h
    db  04dh, 000h, 00eh, 01fh, 0fch, 0e9h, 03dh, 00ah
vgabios_int10_handler:                       ; 0xc0028 LB 0x548
    pushfw                                    ; 9c                          ; 0xc0028 vgarom.asm:91
    cmp ah, 00fh                              ; 80 fc 0f                    ; 0xc0029 vgarom.asm:104
    jne short 00034h                          ; 75 06                       ; 0xc002c vgarom.asm:105
    call 0017dh                               ; e8 4c 01                    ; 0xc002e vgarom.asm:106
    jmp near 000edh                           ; e9 b9 00                    ; 0xc0031 vgarom.asm:107
    cmp ah, 01ah                              ; 80 fc 1a                    ; 0xc0034 vgarom.asm:109
    jne short 0003fh                          ; 75 06                       ; 0xc0037 vgarom.asm:110
    call 00532h                               ; e8 f6 04                    ; 0xc0039 vgarom.asm:111
    jmp near 000edh                           ; e9 ae 00                    ; 0xc003c vgarom.asm:112
    cmp ah, 00bh                              ; 80 fc 0b                    ; 0xc003f vgarom.asm:114
    jne short 0004ah                          ; 75 06                       ; 0xc0042 vgarom.asm:115
    call 000efh                               ; e8 a8 00                    ; 0xc0044 vgarom.asm:116
    jmp near 000edh                           ; e9 a3 00                    ; 0xc0047 vgarom.asm:117
    cmp ax, 01103h                            ; 3d 03 11                    ; 0xc004a vgarom.asm:119
    jne short 00055h                          ; 75 06                       ; 0xc004d vgarom.asm:120
    call 00429h                               ; e8 d7 03                    ; 0xc004f vgarom.asm:121
    jmp near 000edh                           ; e9 98 00                    ; 0xc0052 vgarom.asm:122
    cmp ah, 012h                              ; 80 fc 12                    ; 0xc0055 vgarom.asm:124
    jne short 00097h                          ; 75 3d                       ; 0xc0058 vgarom.asm:125
    cmp bl, 010h                              ; 80 fb 10                    ; 0xc005a vgarom.asm:126
    jne short 00065h                          ; 75 06                       ; 0xc005d vgarom.asm:127
    call 00436h                               ; e8 d4 03                    ; 0xc005f vgarom.asm:128
    jmp near 000edh                           ; e9 88 00                    ; 0xc0062 vgarom.asm:129
    cmp bl, 030h                              ; 80 fb 30                    ; 0xc0065 vgarom.asm:131
    jne short 0006fh                          ; 75 05                       ; 0xc0068 vgarom.asm:132
    call 00459h                               ; e8 ec 03                    ; 0xc006a vgarom.asm:133
    jmp short 000edh                          ; eb 7e                       ; 0xc006d vgarom.asm:134
    cmp bl, 031h                              ; 80 fb 31                    ; 0xc006f vgarom.asm:136
    jne short 00079h                          ; 75 05                       ; 0xc0072 vgarom.asm:137
    call 004ach                               ; e8 35 04                    ; 0xc0074 vgarom.asm:138
    jmp short 000edh                          ; eb 74                       ; 0xc0077 vgarom.asm:139
    cmp bl, 032h                              ; 80 fb 32                    ; 0xc0079 vgarom.asm:141
    jne short 00083h                          ; 75 05                       ; 0xc007c vgarom.asm:142
    call 004ceh                               ; e8 4d 04                    ; 0xc007e vgarom.asm:143
    jmp short 000edh                          ; eb 6a                       ; 0xc0081 vgarom.asm:144
    cmp bl, 033h                              ; 80 fb 33                    ; 0xc0083 vgarom.asm:146
    jne short 0008dh                          ; 75 05                       ; 0xc0086 vgarom.asm:147
    call 004ech                               ; e8 61 04                    ; 0xc0088 vgarom.asm:148
    jmp short 000edh                          ; eb 60                       ; 0xc008b vgarom.asm:149
    cmp bl, 034h                              ; 80 fb 34                    ; 0xc008d vgarom.asm:151
    jne short 000e1h                          ; 75 4f                       ; 0xc0090 vgarom.asm:152
    call 00510h                               ; e8 7b 04                    ; 0xc0092 vgarom.asm:153
    jmp short 000edh                          ; eb 56                       ; 0xc0095 vgarom.asm:154
    cmp ax, 0101bh                            ; 3d 1b 10                    ; 0xc0097 vgarom.asm:156
    je short 000e1h                           ; 74 45                       ; 0xc009a vgarom.asm:157
    cmp ah, 010h                              ; 80 fc 10                    ; 0xc009c vgarom.asm:158
    jne short 000a6h                          ; 75 05                       ; 0xc009f vgarom.asm:162
    call 001a4h                               ; e8 00 01                    ; 0xc00a1 vgarom.asm:164
    jmp short 000edh                          ; eb 47                       ; 0xc00a4 vgarom.asm:165
    cmp ah, 04fh                              ; 80 fc 4f                    ; 0xc00a6 vgarom.asm:168
    jne short 000e1h                          ; 75 36                       ; 0xc00a9 vgarom.asm:169
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc00ab vgarom.asm:170
    jne short 000b4h                          ; 75 05                       ; 0xc00ad vgarom.asm:171
    call 007d2h                               ; e8 20 07                    ; 0xc00af vgarom.asm:172
    jmp short 000edh                          ; eb 39                       ; 0xc00b2 vgarom.asm:173
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc00b4 vgarom.asm:175
    jne short 000bdh                          ; 75 05                       ; 0xc00b6 vgarom.asm:176
    call 007f7h                               ; e8 3c 07                    ; 0xc00b8 vgarom.asm:177
    jmp short 000edh                          ; eb 30                       ; 0xc00bb vgarom.asm:178
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc00bd vgarom.asm:180
    jne short 000c6h                          ; 75 05                       ; 0xc00bf vgarom.asm:181
    call 00824h                               ; e8 60 07                    ; 0xc00c1 vgarom.asm:182
    jmp short 000edh                          ; eb 27                       ; 0xc00c4 vgarom.asm:183
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc00c6 vgarom.asm:185
    jne short 000cfh                          ; 75 05                       ; 0xc00c8 vgarom.asm:186
    call 00858h                               ; e8 8b 07                    ; 0xc00ca vgarom.asm:187
    jmp short 000edh                          ; eb 1e                       ; 0xc00cd vgarom.asm:188
    cmp AL, strict byte 009h                  ; 3c 09                       ; 0xc00cf vgarom.asm:190
    jne short 000d8h                          ; 75 05                       ; 0xc00d1 vgarom.asm:191
    call 0088fh                               ; e8 b9 07                    ; 0xc00d3 vgarom.asm:192
    jmp short 000edh                          ; eb 15                       ; 0xc00d6 vgarom.asm:193
    cmp AL, strict byte 00ah                  ; 3c 0a                       ; 0xc00d8 vgarom.asm:195
    jne short 000e1h                          ; 75 05                       ; 0xc00da vgarom.asm:196
    call 008e6h                               ; e8 07 08                    ; 0xc00dc vgarom.asm:197
    jmp short 000edh                          ; eb 0c                       ; 0xc00df vgarom.asm:198
    push ES                                   ; 06                          ; 0xc00e1 vgarom.asm:202
    push DS                                   ; 1e                          ; 0xc00e2 vgarom.asm:203
    pushaw                                    ; 60                          ; 0xc00e3 vgarom.asm:107
    push CS                                   ; 0e                          ; 0xc00e4 vgarom.asm:207
    pop DS                                    ; 1f                          ; 0xc00e5 vgarom.asm:208
    cld                                       ; fc                          ; 0xc00e6 vgarom.asm:209
    call 038eah                               ; e8 00 38                    ; 0xc00e7 vgarom.asm:210
    popaw                                     ; 61                          ; 0xc00ea vgarom.asm:124
    pop DS                                    ; 1f                          ; 0xc00eb vgarom.asm:213
    pop ES                                    ; 07                          ; 0xc00ec vgarom.asm:214
    popfw                                     ; 9d                          ; 0xc00ed vgarom.asm:216
    iret                                      ; cf                          ; 0xc00ee vgarom.asm:217
    cmp bh, 000h                              ; 80 ff 00                    ; 0xc00ef vgarom.asm:222
    je short 000fah                           ; 74 06                       ; 0xc00f2 vgarom.asm:223
    cmp bh, 001h                              ; 80 ff 01                    ; 0xc00f4 vgarom.asm:224
    je short 0014bh                           ; 74 52                       ; 0xc00f7 vgarom.asm:225
    retn                                      ; c3                          ; 0xc00f9 vgarom.asm:229
    push ax                                   ; 50                          ; 0xc00fa vgarom.asm:231
    push bx                                   ; 53                          ; 0xc00fb vgarom.asm:232
    push cx                                   ; 51                          ; 0xc00fc vgarom.asm:233
    push dx                                   ; 52                          ; 0xc00fd vgarom.asm:234
    push DS                                   ; 1e                          ; 0xc00fe vgarom.asm:235
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc00ff vgarom.asm:236
    mov ds, dx                                ; 8e da                       ; 0xc0102 vgarom.asm:237
    mov dx, 003dah                            ; ba da 03                    ; 0xc0104 vgarom.asm:238
    in AL, DX                                 ; ec                          ; 0xc0107 vgarom.asm:239
    cmp byte [word 00049h], 003h              ; 80 3e 49 00 03              ; 0xc0108 vgarom.asm:240
    jbe short 0013eh                          ; 76 2f                       ; 0xc010d vgarom.asm:241
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc010f vgarom.asm:242
    mov AL, strict byte 000h                  ; b0 00                       ; 0xc0112 vgarom.asm:243
    out DX, AL                                ; ee                          ; 0xc0114 vgarom.asm:244
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc0115 vgarom.asm:245
    and AL, strict byte 00fh                  ; 24 0f                       ; 0xc0117 vgarom.asm:246
    test AL, strict byte 008h                 ; a8 08                       ; 0xc0119 vgarom.asm:247
    je short 0011fh                           ; 74 02                       ; 0xc011b vgarom.asm:248
    add AL, strict byte 008h                  ; 04 08                       ; 0xc011d vgarom.asm:249
    out DX, AL                                ; ee                          ; 0xc011f vgarom.asm:251
    mov CL, strict byte 001h                  ; b1 01                       ; 0xc0120 vgarom.asm:252
    and bl, 010h                              ; 80 e3 10                    ; 0xc0122 vgarom.asm:253
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0125 vgarom.asm:255
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc0128 vgarom.asm:256
    out DX, AL                                ; ee                          ; 0xc012a vgarom.asm:257
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc012b vgarom.asm:258
    in AL, DX                                 ; ec                          ; 0xc012e vgarom.asm:259
    and AL, strict byte 0efh                  ; 24 ef                       ; 0xc012f vgarom.asm:260
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc0131 vgarom.asm:261
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0133 vgarom.asm:262
    out DX, AL                                ; ee                          ; 0xc0136 vgarom.asm:263
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc0137 vgarom.asm:264
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc0139 vgarom.asm:265
    jne short 00125h                          ; 75 e7                       ; 0xc013c vgarom.asm:266
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc013e vgarom.asm:268
    out DX, AL                                ; ee                          ; 0xc0140 vgarom.asm:269
    mov dx, 003dah                            ; ba da 03                    ; 0xc0141 vgarom.asm:271
    in AL, DX                                 ; ec                          ; 0xc0144 vgarom.asm:272
    pop DS                                    ; 1f                          ; 0xc0145 vgarom.asm:274
    pop dx                                    ; 5a                          ; 0xc0146 vgarom.asm:275
    pop cx                                    ; 59                          ; 0xc0147 vgarom.asm:276
    pop bx                                    ; 5b                          ; 0xc0148 vgarom.asm:277
    pop ax                                    ; 58                          ; 0xc0149 vgarom.asm:278
    retn                                      ; c3                          ; 0xc014a vgarom.asm:279
    push ax                                   ; 50                          ; 0xc014b vgarom.asm:281
    push bx                                   ; 53                          ; 0xc014c vgarom.asm:282
    push cx                                   ; 51                          ; 0xc014d vgarom.asm:283
    push dx                                   ; 52                          ; 0xc014e vgarom.asm:284
    mov dx, 003dah                            ; ba da 03                    ; 0xc014f vgarom.asm:285
    in AL, DX                                 ; ec                          ; 0xc0152 vgarom.asm:286
    mov CL, strict byte 001h                  ; b1 01                       ; 0xc0153 vgarom.asm:287
    and bl, 001h                              ; 80 e3 01                    ; 0xc0155 vgarom.asm:288
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0158 vgarom.asm:290
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc015b vgarom.asm:291
    out DX, AL                                ; ee                          ; 0xc015d vgarom.asm:292
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc015e vgarom.asm:293
    in AL, DX                                 ; ec                          ; 0xc0161 vgarom.asm:294
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc0162 vgarom.asm:295
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc0164 vgarom.asm:296
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0166 vgarom.asm:297
    out DX, AL                                ; ee                          ; 0xc0169 vgarom.asm:298
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc016a vgarom.asm:299
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc016c vgarom.asm:300
    jne short 00158h                          ; 75 e7                       ; 0xc016f vgarom.asm:301
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0171 vgarom.asm:302
    out DX, AL                                ; ee                          ; 0xc0173 vgarom.asm:303
    mov dx, 003dah                            ; ba da 03                    ; 0xc0174 vgarom.asm:305
    in AL, DX                                 ; ec                          ; 0xc0177 vgarom.asm:306
    pop dx                                    ; 5a                          ; 0xc0178 vgarom.asm:308
    pop cx                                    ; 59                          ; 0xc0179 vgarom.asm:309
    pop bx                                    ; 5b                          ; 0xc017a vgarom.asm:310
    pop ax                                    ; 58                          ; 0xc017b vgarom.asm:311
    retn                                      ; c3                          ; 0xc017c vgarom.asm:312
    push DS                                   ; 1e                          ; 0xc017d vgarom.asm:317
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc017e vgarom.asm:318
    mov ds, ax                                ; 8e d8                       ; 0xc0181 vgarom.asm:319
    push bx                                   ; 53                          ; 0xc0183 vgarom.asm:320
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc0184 vgarom.asm:321
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0187 vgarom.asm:322
    pop bx                                    ; 5b                          ; 0xc0189 vgarom.asm:323
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc018a vgarom.asm:324
    push bx                                   ; 53                          ; 0xc018c vgarom.asm:325
    mov bx, 00087h                            ; bb 87 00                    ; 0xc018d vgarom.asm:326
    mov ah, byte [bx]                         ; 8a 27                       ; 0xc0190 vgarom.asm:327
    and ah, 080h                              ; 80 e4 80                    ; 0xc0192 vgarom.asm:328
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0195 vgarom.asm:329
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0198 vgarom.asm:330
    db  00ah, 0c4h
    ; or al, ah                                 ; 0a c4                     ; 0xc019a vgarom.asm:331
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc019c vgarom.asm:332
    mov ah, byte [bx]                         ; 8a 27                       ; 0xc019f vgarom.asm:333
    pop bx                                    ; 5b                          ; 0xc01a1 vgarom.asm:334
    pop DS                                    ; 1f                          ; 0xc01a2 vgarom.asm:335
    retn                                      ; c3                          ; 0xc01a3 vgarom.asm:336
    cmp AL, strict byte 000h                  ; 3c 00                       ; 0xc01a4 vgarom.asm:341
    jne short 001aah                          ; 75 02                       ; 0xc01a6 vgarom.asm:342
    jmp short 0020bh                          ; eb 61                       ; 0xc01a8 vgarom.asm:343
    cmp AL, strict byte 001h                  ; 3c 01                       ; 0xc01aa vgarom.asm:345
    jne short 001b0h                          ; 75 02                       ; 0xc01ac vgarom.asm:346
    jmp short 00229h                          ; eb 79                       ; 0xc01ae vgarom.asm:347
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc01b0 vgarom.asm:349
    jne short 001b6h                          ; 75 02                       ; 0xc01b2 vgarom.asm:350
    jmp short 00231h                          ; eb 7b                       ; 0xc01b4 vgarom.asm:351
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc01b6 vgarom.asm:353
    jne short 001bdh                          ; 75 03                       ; 0xc01b8 vgarom.asm:354
    jmp near 00262h                           ; e9 a5 00                    ; 0xc01ba vgarom.asm:355
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc01bd vgarom.asm:357
    jne short 001c4h                          ; 75 03                       ; 0xc01bf vgarom.asm:358
    jmp near 0028ch                           ; e9 c8 00                    ; 0xc01c1 vgarom.asm:359
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc01c4 vgarom.asm:361
    jne short 001cbh                          ; 75 03                       ; 0xc01c6 vgarom.asm:362
    jmp near 002b4h                           ; e9 e9 00                    ; 0xc01c8 vgarom.asm:363
    cmp AL, strict byte 009h                  ; 3c 09                       ; 0xc01cb vgarom.asm:365
    jne short 001d2h                          ; 75 03                       ; 0xc01cd vgarom.asm:366
    jmp near 002c2h                           ; e9 f0 00                    ; 0xc01cf vgarom.asm:367
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc01d2 vgarom.asm:369
    jne short 001d9h                          ; 75 03                       ; 0xc01d4 vgarom.asm:370
    jmp near 00307h                           ; e9 2e 01                    ; 0xc01d6 vgarom.asm:371
    cmp AL, strict byte 012h                  ; 3c 12                       ; 0xc01d9 vgarom.asm:373
    jne short 001e0h                          ; 75 03                       ; 0xc01db vgarom.asm:374
    jmp near 00320h                           ; e9 40 01                    ; 0xc01dd vgarom.asm:375
    cmp AL, strict byte 013h                  ; 3c 13                       ; 0xc01e0 vgarom.asm:377
    jne short 001e7h                          ; 75 03                       ; 0xc01e2 vgarom.asm:378
    jmp near 00348h                           ; e9 61 01                    ; 0xc01e4 vgarom.asm:379
    cmp AL, strict byte 015h                  ; 3c 15                       ; 0xc01e7 vgarom.asm:381
    jne short 001eeh                          ; 75 03                       ; 0xc01e9 vgarom.asm:382
    jmp near 0038fh                           ; e9 a1 01                    ; 0xc01eb vgarom.asm:383
    cmp AL, strict byte 017h                  ; 3c 17                       ; 0xc01ee vgarom.asm:385
    jne short 001f5h                          ; 75 03                       ; 0xc01f0 vgarom.asm:386
    jmp near 003aah                           ; e9 b5 01                    ; 0xc01f2 vgarom.asm:387
    cmp AL, strict byte 018h                  ; 3c 18                       ; 0xc01f5 vgarom.asm:389
    jne short 001fch                          ; 75 03                       ; 0xc01f7 vgarom.asm:390
    jmp near 003d2h                           ; e9 d6 01                    ; 0xc01f9 vgarom.asm:391
    cmp AL, strict byte 019h                  ; 3c 19                       ; 0xc01fc vgarom.asm:393
    jne short 00203h                          ; 75 03                       ; 0xc01fe vgarom.asm:394
    jmp near 003ddh                           ; e9 da 01                    ; 0xc0200 vgarom.asm:395
    cmp AL, strict byte 01ah                  ; 3c 1a                       ; 0xc0203 vgarom.asm:397
    jne short 0020ah                          ; 75 03                       ; 0xc0205 vgarom.asm:398
    jmp near 003e8h                           ; e9 de 01                    ; 0xc0207 vgarom.asm:399
    retn                                      ; c3                          ; 0xc020a vgarom.asm:404
    cmp bl, 014h                              ; 80 fb 14                    ; 0xc020b vgarom.asm:407
    jnbe short 00228h                         ; 77 18                       ; 0xc020e vgarom.asm:408
    push ax                                   ; 50                          ; 0xc0210 vgarom.asm:409
    push dx                                   ; 52                          ; 0xc0211 vgarom.asm:410
    mov dx, 003dah                            ; ba da 03                    ; 0xc0212 vgarom.asm:411
    in AL, DX                                 ; ec                          ; 0xc0215 vgarom.asm:412
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0216 vgarom.asm:413
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc0219 vgarom.asm:414
    out DX, AL                                ; ee                          ; 0xc021b vgarom.asm:415
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc021c vgarom.asm:416
    out DX, AL                                ; ee                          ; 0xc021e vgarom.asm:417
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc021f vgarom.asm:418
    out DX, AL                                ; ee                          ; 0xc0221 vgarom.asm:419
    mov dx, 003dah                            ; ba da 03                    ; 0xc0222 vgarom.asm:421
    in AL, DX                                 ; ec                          ; 0xc0225 vgarom.asm:422
    pop dx                                    ; 5a                          ; 0xc0226 vgarom.asm:424
    pop ax                                    ; 58                          ; 0xc0227 vgarom.asm:425
    retn                                      ; c3                          ; 0xc0228 vgarom.asm:427
    push bx                                   ; 53                          ; 0xc0229 vgarom.asm:432
    mov BL, strict byte 011h                  ; b3 11                       ; 0xc022a vgarom.asm:433
    call 0020bh                               ; e8 dc ff                    ; 0xc022c vgarom.asm:434
    pop bx                                    ; 5b                          ; 0xc022f vgarom.asm:435
    retn                                      ; c3                          ; 0xc0230 vgarom.asm:436
    push ax                                   ; 50                          ; 0xc0231 vgarom.asm:441
    push bx                                   ; 53                          ; 0xc0232 vgarom.asm:442
    push cx                                   ; 51                          ; 0xc0233 vgarom.asm:443
    push dx                                   ; 52                          ; 0xc0234 vgarom.asm:444
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc0235 vgarom.asm:445
    mov dx, 003dah                            ; ba da 03                    ; 0xc0237 vgarom.asm:446
    in AL, DX                                 ; ec                          ; 0xc023a vgarom.asm:447
    mov CL, strict byte 000h                  ; b1 00                       ; 0xc023b vgarom.asm:448
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc023d vgarom.asm:449
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc0240 vgarom.asm:451
    out DX, AL                                ; ee                          ; 0xc0242 vgarom.asm:452
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0243 vgarom.asm:453
    out DX, AL                                ; ee                          ; 0xc0246 vgarom.asm:454
    inc bx                                    ; 43                          ; 0xc0247 vgarom.asm:455
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc0248 vgarom.asm:456
    cmp cl, 010h                              ; 80 f9 10                    ; 0xc024a vgarom.asm:457
    jne short 00240h                          ; 75 f1                       ; 0xc024d vgarom.asm:458
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc024f vgarom.asm:459
    out DX, AL                                ; ee                          ; 0xc0251 vgarom.asm:460
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0252 vgarom.asm:461
    out DX, AL                                ; ee                          ; 0xc0255 vgarom.asm:462
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0256 vgarom.asm:463
    out DX, AL                                ; ee                          ; 0xc0258 vgarom.asm:464
    mov dx, 003dah                            ; ba da 03                    ; 0xc0259 vgarom.asm:466
    in AL, DX                                 ; ec                          ; 0xc025c vgarom.asm:467
    pop dx                                    ; 5a                          ; 0xc025d vgarom.asm:469
    pop cx                                    ; 59                          ; 0xc025e vgarom.asm:470
    pop bx                                    ; 5b                          ; 0xc025f vgarom.asm:471
    pop ax                                    ; 58                          ; 0xc0260 vgarom.asm:472
    retn                                      ; c3                          ; 0xc0261 vgarom.asm:473
    push ax                                   ; 50                          ; 0xc0262 vgarom.asm:478
    push bx                                   ; 53                          ; 0xc0263 vgarom.asm:479
    push dx                                   ; 52                          ; 0xc0264 vgarom.asm:480
    mov dx, 003dah                            ; ba da 03                    ; 0xc0265 vgarom.asm:481
    in AL, DX                                 ; ec                          ; 0xc0268 vgarom.asm:482
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0269 vgarom.asm:483
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc026c vgarom.asm:484
    out DX, AL                                ; ee                          ; 0xc026e vgarom.asm:485
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc026f vgarom.asm:486
    in AL, DX                                 ; ec                          ; 0xc0272 vgarom.asm:487
    and AL, strict byte 0f7h                  ; 24 f7                       ; 0xc0273 vgarom.asm:488
    and bl, 001h                              ; 80 e3 01                    ; 0xc0275 vgarom.asm:489
    sal bl, 003h                              ; c0 e3 03                    ; 0xc0278 vgarom.asm:491
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc027b vgarom.asm:497
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc027d vgarom.asm:498
    out DX, AL                                ; ee                          ; 0xc0280 vgarom.asm:499
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0281 vgarom.asm:500
    out DX, AL                                ; ee                          ; 0xc0283 vgarom.asm:501
    mov dx, 003dah                            ; ba da 03                    ; 0xc0284 vgarom.asm:503
    in AL, DX                                 ; ec                          ; 0xc0287 vgarom.asm:504
    pop dx                                    ; 5a                          ; 0xc0288 vgarom.asm:506
    pop bx                                    ; 5b                          ; 0xc0289 vgarom.asm:507
    pop ax                                    ; 58                          ; 0xc028a vgarom.asm:508
    retn                                      ; c3                          ; 0xc028b vgarom.asm:509
    cmp bl, 014h                              ; 80 fb 14                    ; 0xc028c vgarom.asm:514
    jnbe short 002b3h                         ; 77 22                       ; 0xc028f vgarom.asm:515
    push ax                                   ; 50                          ; 0xc0291 vgarom.asm:516
    push dx                                   ; 52                          ; 0xc0292 vgarom.asm:517
    mov dx, 003dah                            ; ba da 03                    ; 0xc0293 vgarom.asm:518
    in AL, DX                                 ; ec                          ; 0xc0296 vgarom.asm:519
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0297 vgarom.asm:520
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc029a vgarom.asm:521
    out DX, AL                                ; ee                          ; 0xc029c vgarom.asm:522
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc029d vgarom.asm:523
    in AL, DX                                 ; ec                          ; 0xc02a0 vgarom.asm:524
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc02a1 vgarom.asm:525
    mov dx, 003dah                            ; ba da 03                    ; 0xc02a3 vgarom.asm:526
    in AL, DX                                 ; ec                          ; 0xc02a6 vgarom.asm:527
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02a7 vgarom.asm:528
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc02aa vgarom.asm:529
    out DX, AL                                ; ee                          ; 0xc02ac vgarom.asm:530
    mov dx, 003dah                            ; ba da 03                    ; 0xc02ad vgarom.asm:532
    in AL, DX                                 ; ec                          ; 0xc02b0 vgarom.asm:533
    pop dx                                    ; 5a                          ; 0xc02b1 vgarom.asm:535
    pop ax                                    ; 58                          ; 0xc02b2 vgarom.asm:536
    retn                                      ; c3                          ; 0xc02b3 vgarom.asm:538
    push ax                                   ; 50                          ; 0xc02b4 vgarom.asm:543
    push bx                                   ; 53                          ; 0xc02b5 vgarom.asm:544
    mov BL, strict byte 011h                  ; b3 11                       ; 0xc02b6 vgarom.asm:545
    call 0028ch                               ; e8 d1 ff                    ; 0xc02b8 vgarom.asm:546
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc02bb vgarom.asm:547
    pop bx                                    ; 5b                          ; 0xc02bd vgarom.asm:548
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc02be vgarom.asm:549
    pop ax                                    ; 58                          ; 0xc02c0 vgarom.asm:550
    retn                                      ; c3                          ; 0xc02c1 vgarom.asm:551
    push ax                                   ; 50                          ; 0xc02c2 vgarom.asm:556
    push bx                                   ; 53                          ; 0xc02c3 vgarom.asm:557
    push cx                                   ; 51                          ; 0xc02c4 vgarom.asm:558
    push dx                                   ; 52                          ; 0xc02c5 vgarom.asm:559
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc02c6 vgarom.asm:560
    mov CL, strict byte 000h                  ; b1 00                       ; 0xc02c8 vgarom.asm:561
    mov dx, 003dah                            ; ba da 03                    ; 0xc02ca vgarom.asm:563
    in AL, DX                                 ; ec                          ; 0xc02cd vgarom.asm:564
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02ce vgarom.asm:565
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc02d1 vgarom.asm:566
    out DX, AL                                ; ee                          ; 0xc02d3 vgarom.asm:567
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc02d4 vgarom.asm:568
    in AL, DX                                 ; ec                          ; 0xc02d7 vgarom.asm:569
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc02d8 vgarom.asm:570
    inc bx                                    ; 43                          ; 0xc02db vgarom.asm:571
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc02dc vgarom.asm:572
    cmp cl, 010h                              ; 80 f9 10                    ; 0xc02de vgarom.asm:573
    jne short 002cah                          ; 75 e7                       ; 0xc02e1 vgarom.asm:574
    mov dx, 003dah                            ; ba da 03                    ; 0xc02e3 vgarom.asm:575
    in AL, DX                                 ; ec                          ; 0xc02e6 vgarom.asm:576
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02e7 vgarom.asm:577
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc02ea vgarom.asm:578
    out DX, AL                                ; ee                          ; 0xc02ec vgarom.asm:579
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc02ed vgarom.asm:580
    in AL, DX                                 ; ec                          ; 0xc02f0 vgarom.asm:581
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc02f1 vgarom.asm:582
    mov dx, 003dah                            ; ba da 03                    ; 0xc02f4 vgarom.asm:583
    in AL, DX                                 ; ec                          ; 0xc02f7 vgarom.asm:584
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02f8 vgarom.asm:585
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc02fb vgarom.asm:586
    out DX, AL                                ; ee                          ; 0xc02fd vgarom.asm:587
    mov dx, 003dah                            ; ba da 03                    ; 0xc02fe vgarom.asm:589
    in AL, DX                                 ; ec                          ; 0xc0301 vgarom.asm:590
    pop dx                                    ; 5a                          ; 0xc0302 vgarom.asm:592
    pop cx                                    ; 59                          ; 0xc0303 vgarom.asm:593
    pop bx                                    ; 5b                          ; 0xc0304 vgarom.asm:594
    pop ax                                    ; 58                          ; 0xc0305 vgarom.asm:595
    retn                                      ; c3                          ; 0xc0306 vgarom.asm:596
    push ax                                   ; 50                          ; 0xc0307 vgarom.asm:601
    push dx                                   ; 52                          ; 0xc0308 vgarom.asm:602
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc0309 vgarom.asm:603
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc030c vgarom.asm:604
    out DX, AL                                ; ee                          ; 0xc030e vgarom.asm:605
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc030f vgarom.asm:606
    pop ax                                    ; 58                          ; 0xc0312 vgarom.asm:607
    push ax                                   ; 50                          ; 0xc0313 vgarom.asm:608
    db  08ah, 0c4h
    ; mov al, ah                                ; 8a c4                     ; 0xc0314 vgarom.asm:609
    out DX, AL                                ; ee                          ; 0xc0316 vgarom.asm:610
    db  08ah, 0c5h
    ; mov al, ch                                ; 8a c5                     ; 0xc0317 vgarom.asm:611
    out DX, AL                                ; ee                          ; 0xc0319 vgarom.asm:612
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc031a vgarom.asm:613
    out DX, AL                                ; ee                          ; 0xc031c vgarom.asm:614
    pop dx                                    ; 5a                          ; 0xc031d vgarom.asm:615
    pop ax                                    ; 58                          ; 0xc031e vgarom.asm:616
    retn                                      ; c3                          ; 0xc031f vgarom.asm:617
    push ax                                   ; 50                          ; 0xc0320 vgarom.asm:622
    push bx                                   ; 53                          ; 0xc0321 vgarom.asm:623
    push cx                                   ; 51                          ; 0xc0322 vgarom.asm:624
    push dx                                   ; 52                          ; 0xc0323 vgarom.asm:625
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc0324 vgarom.asm:626
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc0327 vgarom.asm:627
    out DX, AL                                ; ee                          ; 0xc0329 vgarom.asm:628
    pop dx                                    ; 5a                          ; 0xc032a vgarom.asm:629
    push dx                                   ; 52                          ; 0xc032b vgarom.asm:630
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc032c vgarom.asm:631
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc032e vgarom.asm:632
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0331 vgarom.asm:634
    out DX, AL                                ; ee                          ; 0xc0334 vgarom.asm:635
    inc bx                                    ; 43                          ; 0xc0335 vgarom.asm:636
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0336 vgarom.asm:637
    out DX, AL                                ; ee                          ; 0xc0339 vgarom.asm:638
    inc bx                                    ; 43                          ; 0xc033a vgarom.asm:639
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc033b vgarom.asm:640
    out DX, AL                                ; ee                          ; 0xc033e vgarom.asm:641
    inc bx                                    ; 43                          ; 0xc033f vgarom.asm:642
    dec cx                                    ; 49                          ; 0xc0340 vgarom.asm:643
    jne short 00331h                          ; 75 ee                       ; 0xc0341 vgarom.asm:644
    pop dx                                    ; 5a                          ; 0xc0343 vgarom.asm:645
    pop cx                                    ; 59                          ; 0xc0344 vgarom.asm:646
    pop bx                                    ; 5b                          ; 0xc0345 vgarom.asm:647
    pop ax                                    ; 58                          ; 0xc0346 vgarom.asm:648
    retn                                      ; c3                          ; 0xc0347 vgarom.asm:649
    push ax                                   ; 50                          ; 0xc0348 vgarom.asm:654
    push bx                                   ; 53                          ; 0xc0349 vgarom.asm:655
    push dx                                   ; 52                          ; 0xc034a vgarom.asm:656
    mov dx, 003dah                            ; ba da 03                    ; 0xc034b vgarom.asm:657
    in AL, DX                                 ; ec                          ; 0xc034e vgarom.asm:658
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc034f vgarom.asm:659
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc0352 vgarom.asm:660
    out DX, AL                                ; ee                          ; 0xc0354 vgarom.asm:661
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0355 vgarom.asm:662
    in AL, DX                                 ; ec                          ; 0xc0358 vgarom.asm:663
    and bl, 001h                              ; 80 e3 01                    ; 0xc0359 vgarom.asm:664
    jne short 0036bh                          ; 75 0d                       ; 0xc035c vgarom.asm:665
    and AL, strict byte 07fh                  ; 24 7f                       ; 0xc035e vgarom.asm:666
    sal bh, 007h                              ; c0 e7 07                    ; 0xc0360 vgarom.asm:668
    db  00ah, 0c7h
    ; or al, bh                                 ; 0a c7                     ; 0xc0363 vgarom.asm:678
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0365 vgarom.asm:679
    out DX, AL                                ; ee                          ; 0xc0368 vgarom.asm:680
    jmp short 00384h                          ; eb 19                       ; 0xc0369 vgarom.asm:681
    push ax                                   ; 50                          ; 0xc036b vgarom.asm:683
    mov dx, 003dah                            ; ba da 03                    ; 0xc036c vgarom.asm:684
    in AL, DX                                 ; ec                          ; 0xc036f vgarom.asm:685
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0370 vgarom.asm:686
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc0373 vgarom.asm:687
    out DX, AL                                ; ee                          ; 0xc0375 vgarom.asm:688
    pop ax                                    ; 58                          ; 0xc0376 vgarom.asm:689
    and AL, strict byte 080h                  ; 24 80                       ; 0xc0377 vgarom.asm:690
    jne short 0037eh                          ; 75 03                       ; 0xc0379 vgarom.asm:691
    sal bh, 002h                              ; c0 e7 02                    ; 0xc037b vgarom.asm:693
    and bh, 00fh                              ; 80 e7 0f                    ; 0xc037e vgarom.asm:699
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc0381 vgarom.asm:700
    out DX, AL                                ; ee                          ; 0xc0383 vgarom.asm:701
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0384 vgarom.asm:703
    out DX, AL                                ; ee                          ; 0xc0386 vgarom.asm:704
    mov dx, 003dah                            ; ba da 03                    ; 0xc0387 vgarom.asm:706
    in AL, DX                                 ; ec                          ; 0xc038a vgarom.asm:707
    pop dx                                    ; 5a                          ; 0xc038b vgarom.asm:709
    pop bx                                    ; 5b                          ; 0xc038c vgarom.asm:710
    pop ax                                    ; 58                          ; 0xc038d vgarom.asm:711
    retn                                      ; c3                          ; 0xc038e vgarom.asm:712
    push ax                                   ; 50                          ; 0xc038f vgarom.asm:717
    push dx                                   ; 52                          ; 0xc0390 vgarom.asm:718
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc0391 vgarom.asm:719
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc0394 vgarom.asm:720
    out DX, AL                                ; ee                          ; 0xc0396 vgarom.asm:721
    pop ax                                    ; 58                          ; 0xc0397 vgarom.asm:722
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc0398 vgarom.asm:723
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc039a vgarom.asm:724
    in AL, DX                                 ; ec                          ; 0xc039d vgarom.asm:725
    db  086h, 0e0h
    ; xchg al, ah                               ; 86 e0                     ; 0xc039e vgarom.asm:726
    push ax                                   ; 50                          ; 0xc03a0 vgarom.asm:727
    in AL, DX                                 ; ec                          ; 0xc03a1 vgarom.asm:728
    db  08ah, 0e8h
    ; mov ch, al                                ; 8a e8                     ; 0xc03a2 vgarom.asm:729
    in AL, DX                                 ; ec                          ; 0xc03a4 vgarom.asm:730
    db  08ah, 0c8h
    ; mov cl, al                                ; 8a c8                     ; 0xc03a5 vgarom.asm:731
    pop dx                                    ; 5a                          ; 0xc03a7 vgarom.asm:732
    pop ax                                    ; 58                          ; 0xc03a8 vgarom.asm:733
    retn                                      ; c3                          ; 0xc03a9 vgarom.asm:734
    push ax                                   ; 50                          ; 0xc03aa vgarom.asm:739
    push bx                                   ; 53                          ; 0xc03ab vgarom.asm:740
    push cx                                   ; 51                          ; 0xc03ac vgarom.asm:741
    push dx                                   ; 52                          ; 0xc03ad vgarom.asm:742
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc03ae vgarom.asm:743
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc03b1 vgarom.asm:744
    out DX, AL                                ; ee                          ; 0xc03b3 vgarom.asm:745
    pop dx                                    ; 5a                          ; 0xc03b4 vgarom.asm:746
    push dx                                   ; 52                          ; 0xc03b5 vgarom.asm:747
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc03b6 vgarom.asm:748
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc03b8 vgarom.asm:749
    in AL, DX                                 ; ec                          ; 0xc03bb vgarom.asm:751
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc03bc vgarom.asm:752
    inc bx                                    ; 43                          ; 0xc03bf vgarom.asm:753
    in AL, DX                                 ; ec                          ; 0xc03c0 vgarom.asm:754
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc03c1 vgarom.asm:755
    inc bx                                    ; 43                          ; 0xc03c4 vgarom.asm:756
    in AL, DX                                 ; ec                          ; 0xc03c5 vgarom.asm:757
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc03c6 vgarom.asm:758
    inc bx                                    ; 43                          ; 0xc03c9 vgarom.asm:759
    dec cx                                    ; 49                          ; 0xc03ca vgarom.asm:760
    jne short 003bbh                          ; 75 ee                       ; 0xc03cb vgarom.asm:761
    pop dx                                    ; 5a                          ; 0xc03cd vgarom.asm:762
    pop cx                                    ; 59                          ; 0xc03ce vgarom.asm:763
    pop bx                                    ; 5b                          ; 0xc03cf vgarom.asm:764
    pop ax                                    ; 58                          ; 0xc03d0 vgarom.asm:765
    retn                                      ; c3                          ; 0xc03d1 vgarom.asm:766
    push ax                                   ; 50                          ; 0xc03d2 vgarom.asm:771
    push dx                                   ; 52                          ; 0xc03d3 vgarom.asm:772
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc03d4 vgarom.asm:773
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc03d7 vgarom.asm:774
    out DX, AL                                ; ee                          ; 0xc03d9 vgarom.asm:775
    pop dx                                    ; 5a                          ; 0xc03da vgarom.asm:776
    pop ax                                    ; 58                          ; 0xc03db vgarom.asm:777
    retn                                      ; c3                          ; 0xc03dc vgarom.asm:778
    push ax                                   ; 50                          ; 0xc03dd vgarom.asm:783
    push dx                                   ; 52                          ; 0xc03de vgarom.asm:784
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc03df vgarom.asm:785
    in AL, DX                                 ; ec                          ; 0xc03e2 vgarom.asm:786
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc03e3 vgarom.asm:787
    pop dx                                    ; 5a                          ; 0xc03e5 vgarom.asm:788
    pop ax                                    ; 58                          ; 0xc03e6 vgarom.asm:789
    retn                                      ; c3                          ; 0xc03e7 vgarom.asm:790
    push ax                                   ; 50                          ; 0xc03e8 vgarom.asm:795
    push dx                                   ; 52                          ; 0xc03e9 vgarom.asm:796
    mov dx, 003dah                            ; ba da 03                    ; 0xc03ea vgarom.asm:797
    in AL, DX                                 ; ec                          ; 0xc03ed vgarom.asm:798
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc03ee vgarom.asm:799
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc03f1 vgarom.asm:800
    out DX, AL                                ; ee                          ; 0xc03f3 vgarom.asm:801
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc03f4 vgarom.asm:802
    in AL, DX                                 ; ec                          ; 0xc03f7 vgarom.asm:803
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc03f8 vgarom.asm:804
    shr bl, 007h                              ; c0 eb 07                    ; 0xc03fa vgarom.asm:806
    mov dx, 003dah                            ; ba da 03                    ; 0xc03fd vgarom.asm:816
    in AL, DX                                 ; ec                          ; 0xc0400 vgarom.asm:817
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0401 vgarom.asm:818
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc0404 vgarom.asm:819
    out DX, AL                                ; ee                          ; 0xc0406 vgarom.asm:820
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0407 vgarom.asm:821
    in AL, DX                                 ; ec                          ; 0xc040a vgarom.asm:822
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc040b vgarom.asm:823
    and bh, 00fh                              ; 80 e7 0f                    ; 0xc040d vgarom.asm:824
    test bl, 001h                             ; f6 c3 01                    ; 0xc0410 vgarom.asm:825
    jne short 00418h                          ; 75 03                       ; 0xc0413 vgarom.asm:826
    shr bh, 002h                              ; c0 ef 02                    ; 0xc0415 vgarom.asm:828
    mov dx, 003dah                            ; ba da 03                    ; 0xc0418 vgarom.asm:834
    in AL, DX                                 ; ec                          ; 0xc041b vgarom.asm:835
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc041c vgarom.asm:836
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc041f vgarom.asm:837
    out DX, AL                                ; ee                          ; 0xc0421 vgarom.asm:838
    mov dx, 003dah                            ; ba da 03                    ; 0xc0422 vgarom.asm:840
    in AL, DX                                 ; ec                          ; 0xc0425 vgarom.asm:841
    pop dx                                    ; 5a                          ; 0xc0426 vgarom.asm:843
    pop ax                                    ; 58                          ; 0xc0427 vgarom.asm:844
    retn                                      ; c3                          ; 0xc0428 vgarom.asm:845
    push ax                                   ; 50                          ; 0xc0429 vgarom.asm:850
    push dx                                   ; 52                          ; 0xc042a vgarom.asm:851
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc042b vgarom.asm:852
    db  08ah, 0e3h
    ; mov ah, bl                                ; 8a e3                     ; 0xc042e vgarom.asm:853
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc0430 vgarom.asm:854
    out DX, ax                                ; ef                          ; 0xc0432 vgarom.asm:855
    pop dx                                    ; 5a                          ; 0xc0433 vgarom.asm:856
    pop ax                                    ; 58                          ; 0xc0434 vgarom.asm:857
    retn                                      ; c3                          ; 0xc0435 vgarom.asm:858
    push DS                                   ; 1e                          ; 0xc0436 vgarom.asm:863
    push ax                                   ; 50                          ; 0xc0437 vgarom.asm:864
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0438 vgarom.asm:865
    mov ds, ax                                ; 8e d8                       ; 0xc043b vgarom.asm:866
    db  032h, 0edh
    ; xor ch, ch                                ; 32 ed                     ; 0xc043d vgarom.asm:867
    mov bx, 00088h                            ; bb 88 00                    ; 0xc043f vgarom.asm:868
    mov cl, byte [bx]                         ; 8a 0f                       ; 0xc0442 vgarom.asm:869
    and cl, 00fh                              ; 80 e1 0f                    ; 0xc0444 vgarom.asm:870
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc0447 vgarom.asm:871
    mov ax, word [bx]                         ; 8b 07                       ; 0xc044a vgarom.asm:872
    mov bx, strict word 00003h                ; bb 03 00                    ; 0xc044c vgarom.asm:873
    cmp ax, 003b4h                            ; 3d b4 03                    ; 0xc044f vgarom.asm:874
    jne short 00456h                          ; 75 02                       ; 0xc0452 vgarom.asm:875
    mov BH, strict byte 001h                  ; b7 01                       ; 0xc0454 vgarom.asm:876
    pop ax                                    ; 58                          ; 0xc0456 vgarom.asm:878
    pop DS                                    ; 1f                          ; 0xc0457 vgarom.asm:879
    retn                                      ; c3                          ; 0xc0458 vgarom.asm:880
    push DS                                   ; 1e                          ; 0xc0459 vgarom.asm:888
    push bx                                   ; 53                          ; 0xc045a vgarom.asm:889
    push dx                                   ; 52                          ; 0xc045b vgarom.asm:890
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc045c vgarom.asm:891
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc045e vgarom.asm:892
    mov ds, ax                                ; 8e d8                       ; 0xc0461 vgarom.asm:893
    mov bx, 00089h                            ; bb 89 00                    ; 0xc0463 vgarom.asm:894
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0466 vgarom.asm:895
    mov bx, 00088h                            ; bb 88 00                    ; 0xc0468 vgarom.asm:896
    mov ah, byte [bx]                         ; 8a 27                       ; 0xc046b vgarom.asm:897
    cmp dl, 001h                              ; 80 fa 01                    ; 0xc046d vgarom.asm:898
    je short 00487h                           ; 74 15                       ; 0xc0470 vgarom.asm:899
    jc short 00491h                           ; 72 1d                       ; 0xc0472 vgarom.asm:900
    cmp dl, 002h                              ; 80 fa 02                    ; 0xc0474 vgarom.asm:901
    je short 0047bh                           ; 74 02                       ; 0xc0477 vgarom.asm:902
    jmp short 004a5h                          ; eb 2a                       ; 0xc0479 vgarom.asm:912
    and AL, strict byte 07fh                  ; 24 7f                       ; 0xc047b vgarom.asm:918
    or AL, strict byte 010h                   ; 0c 10                       ; 0xc047d vgarom.asm:919
    and ah, 0f0h                              ; 80 e4 f0                    ; 0xc047f vgarom.asm:920
    or ah, 009h                               ; 80 cc 09                    ; 0xc0482 vgarom.asm:921
    jne short 0049bh                          ; 75 14                       ; 0xc0485 vgarom.asm:922
    and AL, strict byte 06fh                  ; 24 6f                       ; 0xc0487 vgarom.asm:928
    and ah, 0f0h                              ; 80 e4 f0                    ; 0xc0489 vgarom.asm:929
    or ah, 009h                               ; 80 cc 09                    ; 0xc048c vgarom.asm:930
    jne short 0049bh                          ; 75 0a                       ; 0xc048f vgarom.asm:931
    and AL, strict byte 0efh                  ; 24 ef                       ; 0xc0491 vgarom.asm:937
    or AL, strict byte 080h                   ; 0c 80                       ; 0xc0493 vgarom.asm:938
    and ah, 0f0h                              ; 80 e4 f0                    ; 0xc0495 vgarom.asm:939
    or ah, 008h                               ; 80 cc 08                    ; 0xc0498 vgarom.asm:940
    mov bx, 00089h                            ; bb 89 00                    ; 0xc049b vgarom.asm:942
    mov byte [bx], al                         ; 88 07                       ; 0xc049e vgarom.asm:943
    mov bx, 00088h                            ; bb 88 00                    ; 0xc04a0 vgarom.asm:944
    mov byte [bx], ah                         ; 88 27                       ; 0xc04a3 vgarom.asm:945
    mov ax, 01212h                            ; b8 12 12                    ; 0xc04a5 vgarom.asm:947
    pop dx                                    ; 5a                          ; 0xc04a8 vgarom.asm:948
    pop bx                                    ; 5b                          ; 0xc04a9 vgarom.asm:949
    pop DS                                    ; 1f                          ; 0xc04aa vgarom.asm:950
    retn                                      ; c3                          ; 0xc04ab vgarom.asm:951
    push DS                                   ; 1e                          ; 0xc04ac vgarom.asm:960
    push bx                                   ; 53                          ; 0xc04ad vgarom.asm:961
    push dx                                   ; 52                          ; 0xc04ae vgarom.asm:962
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc04af vgarom.asm:963
    and dl, 001h                              ; 80 e2 01                    ; 0xc04b1 vgarom.asm:964
    sal dl, 003h                              ; c0 e2 03                    ; 0xc04b4 vgarom.asm:966
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc04b7 vgarom.asm:972
    mov ds, ax                                ; 8e d8                       ; 0xc04ba vgarom.asm:973
    mov bx, 00089h                            ; bb 89 00                    ; 0xc04bc vgarom.asm:974
    mov al, byte [bx]                         ; 8a 07                       ; 0xc04bf vgarom.asm:975
    and AL, strict byte 0f7h                  ; 24 f7                       ; 0xc04c1 vgarom.asm:976
    db  00ah, 0c2h
    ; or al, dl                                 ; 0a c2                     ; 0xc04c3 vgarom.asm:977
    mov byte [bx], al                         ; 88 07                       ; 0xc04c5 vgarom.asm:978
    mov ax, 01212h                            ; b8 12 12                    ; 0xc04c7 vgarom.asm:979
    pop dx                                    ; 5a                          ; 0xc04ca vgarom.asm:980
    pop bx                                    ; 5b                          ; 0xc04cb vgarom.asm:981
    pop DS                                    ; 1f                          ; 0xc04cc vgarom.asm:982
    retn                                      ; c3                          ; 0xc04cd vgarom.asm:983
    push bx                                   ; 53                          ; 0xc04ce vgarom.asm:987
    push dx                                   ; 52                          ; 0xc04cf vgarom.asm:988
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc04d0 vgarom.asm:989
    and bl, 001h                              ; 80 e3 01                    ; 0xc04d2 vgarom.asm:990
    xor bl, 001h                              ; 80 f3 01                    ; 0xc04d5 vgarom.asm:991
    sal bl, 1                                 ; d0 e3                       ; 0xc04d8 vgarom.asm:992
    mov dx, 003cch                            ; ba cc 03                    ; 0xc04da vgarom.asm:993
    in AL, DX                                 ; ec                          ; 0xc04dd vgarom.asm:994
    and AL, strict byte 0fdh                  ; 24 fd                       ; 0xc04de vgarom.asm:995
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc04e0 vgarom.asm:996
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc04e2 vgarom.asm:997
    out DX, AL                                ; ee                          ; 0xc04e5 vgarom.asm:998
    mov ax, 01212h                            ; b8 12 12                    ; 0xc04e6 vgarom.asm:999
    pop dx                                    ; 5a                          ; 0xc04e9 vgarom.asm:1000
    pop bx                                    ; 5b                          ; 0xc04ea vgarom.asm:1001
    retn                                      ; c3                          ; 0xc04eb vgarom.asm:1002
    push DS                                   ; 1e                          ; 0xc04ec vgarom.asm:1006
    push bx                                   ; 53                          ; 0xc04ed vgarom.asm:1007
    push dx                                   ; 52                          ; 0xc04ee vgarom.asm:1008
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc04ef vgarom.asm:1009
    and dl, 001h                              ; 80 e2 01                    ; 0xc04f1 vgarom.asm:1010
    xor dl, 001h                              ; 80 f2 01                    ; 0xc04f4 vgarom.asm:1011
    sal dl, 1                                 ; d0 e2                       ; 0xc04f7 vgarom.asm:1012
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc04f9 vgarom.asm:1013
    mov ds, ax                                ; 8e d8                       ; 0xc04fc vgarom.asm:1014
    mov bx, 00089h                            ; bb 89 00                    ; 0xc04fe vgarom.asm:1015
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0501 vgarom.asm:1016
    and AL, strict byte 0fdh                  ; 24 fd                       ; 0xc0503 vgarom.asm:1017
    db  00ah, 0c2h
    ; or al, dl                                 ; 0a c2                     ; 0xc0505 vgarom.asm:1018
    mov byte [bx], al                         ; 88 07                       ; 0xc0507 vgarom.asm:1019
    mov ax, 01212h                            ; b8 12 12                    ; 0xc0509 vgarom.asm:1020
    pop dx                                    ; 5a                          ; 0xc050c vgarom.asm:1021
    pop bx                                    ; 5b                          ; 0xc050d vgarom.asm:1022
    pop DS                                    ; 1f                          ; 0xc050e vgarom.asm:1023
    retn                                      ; c3                          ; 0xc050f vgarom.asm:1024
    push DS                                   ; 1e                          ; 0xc0510 vgarom.asm:1028
    push bx                                   ; 53                          ; 0xc0511 vgarom.asm:1029
    push dx                                   ; 52                          ; 0xc0512 vgarom.asm:1030
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc0513 vgarom.asm:1031
    and dl, 001h                              ; 80 e2 01                    ; 0xc0515 vgarom.asm:1032
    xor dl, 001h                              ; 80 f2 01                    ; 0xc0518 vgarom.asm:1033
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc051b vgarom.asm:1034
    mov ds, ax                                ; 8e d8                       ; 0xc051e vgarom.asm:1035
    mov bx, 00089h                            ; bb 89 00                    ; 0xc0520 vgarom.asm:1036
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0523 vgarom.asm:1037
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc0525 vgarom.asm:1038
    db  00ah, 0c2h
    ; or al, dl                                 ; 0a c2                     ; 0xc0527 vgarom.asm:1039
    mov byte [bx], al                         ; 88 07                       ; 0xc0529 vgarom.asm:1040
    mov ax, 01212h                            ; b8 12 12                    ; 0xc052b vgarom.asm:1041
    pop dx                                    ; 5a                          ; 0xc052e vgarom.asm:1042
    pop bx                                    ; 5b                          ; 0xc052f vgarom.asm:1043
    pop DS                                    ; 1f                          ; 0xc0530 vgarom.asm:1044
    retn                                      ; c3                          ; 0xc0531 vgarom.asm:1045
    cmp AL, strict byte 000h                  ; 3c 00                       ; 0xc0532 vgarom.asm:1050
    je short 0053bh                           ; 74 05                       ; 0xc0534 vgarom.asm:1051
    cmp AL, strict byte 001h                  ; 3c 01                       ; 0xc0536 vgarom.asm:1052
    je short 00550h                           ; 74 16                       ; 0xc0538 vgarom.asm:1053
    retn                                      ; c3                          ; 0xc053a vgarom.asm:1057
    push DS                                   ; 1e                          ; 0xc053b vgarom.asm:1059
    push ax                                   ; 50                          ; 0xc053c vgarom.asm:1060
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc053d vgarom.asm:1061
    mov ds, ax                                ; 8e d8                       ; 0xc0540 vgarom.asm:1062
    mov bx, 0008ah                            ; bb 8a 00                    ; 0xc0542 vgarom.asm:1063
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0545 vgarom.asm:1064
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc0547 vgarom.asm:1065
    db  032h, 0ffh
    ; xor bh, bh                                ; 32 ff                     ; 0xc0549 vgarom.asm:1066
    pop ax                                    ; 58                          ; 0xc054b vgarom.asm:1067
    db  08ah, 0c4h
    ; mov al, ah                                ; 8a c4                     ; 0xc054c vgarom.asm:1068
    pop DS                                    ; 1f                          ; 0xc054e vgarom.asm:1069
    retn                                      ; c3                          ; 0xc054f vgarom.asm:1070
    push DS                                   ; 1e                          ; 0xc0550 vgarom.asm:1072
    push ax                                   ; 50                          ; 0xc0551 vgarom.asm:1073
    push bx                                   ; 53                          ; 0xc0552 vgarom.asm:1074
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0553 vgarom.asm:1075
    mov ds, ax                                ; 8e d8                       ; 0xc0556 vgarom.asm:1076
    db  08bh, 0c3h
    ; mov ax, bx                                ; 8b c3                     ; 0xc0558 vgarom.asm:1077
    mov bx, 0008ah                            ; bb 8a 00                    ; 0xc055a vgarom.asm:1078
    mov byte [bx], al                         ; 88 07                       ; 0xc055d vgarom.asm:1079
    pop bx                                    ; 5b                          ; 0xc055f vgarom.asm:1089
    pop ax                                    ; 58                          ; 0xc0560 vgarom.asm:1090
    db  08ah, 0c4h
    ; mov al, ah                                ; 8a c4                     ; 0xc0561 vgarom.asm:1091
    pop DS                                    ; 1f                          ; 0xc0563 vgarom.asm:1092
    retn                                      ; c3                          ; 0xc0564 vgarom.asm:1093
    times 0xb db 0
  ; disGetNextSymbol 0xc0570 LB 0x38a -> off=0x0 cb=0000000000000007 uValue=00000000000c0570 'do_out_dx_ax'
do_out_dx_ax:                                ; 0xc0570 LB 0x7
    db  086h, 0c4h
    ; xchg ah, al                               ; 86 c4                     ; 0xc0570 vberom.asm:69
    out DX, AL                                ; ee                          ; 0xc0572 vberom.asm:70
    db  086h, 0c4h
    ; xchg ah, al                               ; 86 c4                     ; 0xc0573 vberom.asm:71
    out DX, AL                                ; ee                          ; 0xc0575 vberom.asm:72
    retn                                      ; c3                          ; 0xc0576 vberom.asm:73
  ; disGetNextSymbol 0xc0577 LB 0x383 -> off=0x0 cb=0000000000000040 uValue=00000000000c0577 'do_in_ax_dx'
do_in_ax_dx:                                 ; 0xc0577 LB 0x40
    in AL, DX                                 ; ec                          ; 0xc0577 vberom.asm:76
    db  086h, 0c4h
    ; xchg ah, al                               ; 86 c4                     ; 0xc0578 vberom.asm:77
    in AL, DX                                 ; ec                          ; 0xc057a vberom.asm:78
    retn                                      ; c3                          ; 0xc057b vberom.asm:79
    push ax                                   ; 50                          ; 0xc057c vberom.asm:90
    push dx                                   ; 52                          ; 0xc057d vberom.asm:91
    mov dx, 003dah                            ; ba da 03                    ; 0xc057e vberom.asm:92
    in AL, DX                                 ; ec                          ; 0xc0581 vberom.asm:94
    test AL, strict byte 008h                 ; a8 08                       ; 0xc0582 vberom.asm:95
    je short 00581h                           ; 74 fb                       ; 0xc0584 vberom.asm:96
    pop dx                                    ; 5a                          ; 0xc0586 vberom.asm:97
    pop ax                                    ; 58                          ; 0xc0587 vberom.asm:98
    retn                                      ; c3                          ; 0xc0588 vberom.asm:99
    push ax                                   ; 50                          ; 0xc0589 vberom.asm:102
    push dx                                   ; 52                          ; 0xc058a vberom.asm:103
    mov dx, 003dah                            ; ba da 03                    ; 0xc058b vberom.asm:104
    in AL, DX                                 ; ec                          ; 0xc058e vberom.asm:106
    test AL, strict byte 008h                 ; a8 08                       ; 0xc058f vberom.asm:107
    jne short 0058eh                          ; 75 fb                       ; 0xc0591 vberom.asm:108
    pop dx                                    ; 5a                          ; 0xc0593 vberom.asm:109
    pop ax                                    ; 58                          ; 0xc0594 vberom.asm:110
    retn                                      ; c3                          ; 0xc0595 vberom.asm:111
    push dx                                   ; 52                          ; 0xc0596 vberom.asm:116
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0597 vberom.asm:117
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc059a vberom.asm:118
    call 00570h                               ; e8 d0 ff                    ; 0xc059d vberom.asm:119
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc05a0 vberom.asm:120
    call 00577h                               ; e8 d1 ff                    ; 0xc05a3 vberom.asm:121
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc05a6 vberom.asm:122
    jbe short 005b5h                          ; 76 0b                       ; 0xc05a8 vberom.asm:123
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc05aa vberom.asm:124
    shr ah, 003h                              ; c0 ec 03                    ; 0xc05ac vberom.asm:126
    test AL, strict byte 007h                 ; a8 07                       ; 0xc05af vberom.asm:132
    je short 005b5h                           ; 74 02                       ; 0xc05b1 vberom.asm:133
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc05b3 vberom.asm:134
    pop dx                                    ; 5a                          ; 0xc05b5 vberom.asm:136
    retn                                      ; c3                          ; 0xc05b6 vberom.asm:137
  ; disGetNextSymbol 0xc05b7 LB 0x343 -> off=0x0 cb=0000000000000026 uValue=00000000000c05b7 '_dispi_get_max_bpp'
_dispi_get_max_bpp:                          ; 0xc05b7 LB 0x26
    push dx                                   ; 52                          ; 0xc05b7 vberom.asm:142
    push bx                                   ; 53                          ; 0xc05b8 vberom.asm:143
    call 005f1h                               ; e8 35 00                    ; 0xc05b9 vberom.asm:144
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc05bc vberom.asm:145
    or ax, strict byte 00002h                 ; 83 c8 02                    ; 0xc05be vberom.asm:146
    call 005ddh                               ; e8 19 00                    ; 0xc05c1 vberom.asm:147
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc05c4 vberom.asm:148
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc05c7 vberom.asm:149
    call 00570h                               ; e8 a3 ff                    ; 0xc05ca vberom.asm:150
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc05cd vberom.asm:151
    call 00577h                               ; e8 a4 ff                    ; 0xc05d0 vberom.asm:152
    push ax                                   ; 50                          ; 0xc05d3 vberom.asm:153
    db  08bh, 0c3h
    ; mov ax, bx                                ; 8b c3                     ; 0xc05d4 vberom.asm:154
    call 005ddh                               ; e8 04 00                    ; 0xc05d6 vberom.asm:155
    pop ax                                    ; 58                          ; 0xc05d9 vberom.asm:156
    pop bx                                    ; 5b                          ; 0xc05da vberom.asm:157
    pop dx                                    ; 5a                          ; 0xc05db vberom.asm:158
    retn                                      ; c3                          ; 0xc05dc vberom.asm:159
  ; disGetNextSymbol 0xc05dd LB 0x31d -> off=0x0 cb=0000000000000026 uValue=00000000000c05dd 'dispi_set_enable_'
dispi_set_enable_:                           ; 0xc05dd LB 0x26
    push dx                                   ; 52                          ; 0xc05dd vberom.asm:162
    push ax                                   ; 50                          ; 0xc05de vberom.asm:163
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc05df vberom.asm:164
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc05e2 vberom.asm:165
    call 00570h                               ; e8 88 ff                    ; 0xc05e5 vberom.asm:166
    pop ax                                    ; 58                          ; 0xc05e8 vberom.asm:167
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc05e9 vberom.asm:168
    call 00570h                               ; e8 81 ff                    ; 0xc05ec vberom.asm:169
    pop dx                                    ; 5a                          ; 0xc05ef vberom.asm:170
    retn                                      ; c3                          ; 0xc05f0 vberom.asm:171
    push dx                                   ; 52                          ; 0xc05f1 vberom.asm:174
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc05f2 vberom.asm:175
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc05f5 vberom.asm:176
    call 00570h                               ; e8 75 ff                    ; 0xc05f8 vberom.asm:177
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc05fb vberom.asm:178
    call 00577h                               ; e8 76 ff                    ; 0xc05fe vberom.asm:179
    pop dx                                    ; 5a                          ; 0xc0601 vberom.asm:180
    retn                                      ; c3                          ; 0xc0602 vberom.asm:181
  ; disGetNextSymbol 0xc0603 LB 0x2f7 -> off=0x0 cb=0000000000000026 uValue=00000000000c0603 'dispi_set_bank_'
dispi_set_bank_:                             ; 0xc0603 LB 0x26
    push dx                                   ; 52                          ; 0xc0603 vberom.asm:184
    push ax                                   ; 50                          ; 0xc0604 vberom.asm:185
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0605 vberom.asm:186
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0608 vberom.asm:187
    call 00570h                               ; e8 62 ff                    ; 0xc060b vberom.asm:188
    pop ax                                    ; 58                          ; 0xc060e vberom.asm:189
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc060f vberom.asm:190
    call 00570h                               ; e8 5b ff                    ; 0xc0612 vberom.asm:191
    pop dx                                    ; 5a                          ; 0xc0615 vberom.asm:192
    retn                                      ; c3                          ; 0xc0616 vberom.asm:193
    push dx                                   ; 52                          ; 0xc0617 vberom.asm:196
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0618 vberom.asm:197
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc061b vberom.asm:198
    call 00570h                               ; e8 4f ff                    ; 0xc061e vberom.asm:199
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0621 vberom.asm:200
    call 00577h                               ; e8 50 ff                    ; 0xc0624 vberom.asm:201
    pop dx                                    ; 5a                          ; 0xc0627 vberom.asm:202
    retn                                      ; c3                          ; 0xc0628 vberom.asm:203
  ; disGetNextSymbol 0xc0629 LB 0x2d1 -> off=0x0 cb=00000000000000a9 uValue=00000000000c0629 '_dispi_set_bank_farcall'
_dispi_set_bank_farcall:                     ; 0xc0629 LB 0xa9
    cmp bx, 00100h                            ; 81 fb 00 01                 ; 0xc0629 vberom.asm:206
    je short 00653h                           ; 74 24                       ; 0xc062d vberom.asm:207
    db  00bh, 0dbh
    ; or bx, bx                                 ; 0b db                     ; 0xc062f vberom.asm:208
    jne short 00665h                          ; 75 32                       ; 0xc0631 vberom.asm:209
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc0633 vberom.asm:210
    push dx                                   ; 52                          ; 0xc0635 vberom.asm:211
    push ax                                   ; 50                          ; 0xc0636 vberom.asm:212
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0637 vberom.asm:213
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc063a vberom.asm:214
    call 00570h                               ; e8 30 ff                    ; 0xc063d vberom.asm:215
    pop ax                                    ; 58                          ; 0xc0640 vberom.asm:216
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0641 vberom.asm:217
    call 00570h                               ; e8 29 ff                    ; 0xc0644 vberom.asm:218
    call 00577h                               ; e8 2d ff                    ; 0xc0647 vberom.asm:219
    pop dx                                    ; 5a                          ; 0xc064a vberom.asm:220
    db  03bh, 0d0h
    ; cmp dx, ax                                ; 3b d0                     ; 0xc064b vberom.asm:221
    jne short 00665h                          ; 75 16                       ; 0xc064d vberom.asm:222
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc064f vberom.asm:223
    retf                                      ; cb                          ; 0xc0652 vberom.asm:224
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0653 vberom.asm:226
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0656 vberom.asm:227
    call 00570h                               ; e8 14 ff                    ; 0xc0659 vberom.asm:228
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc065c vberom.asm:229
    call 00577h                               ; e8 15 ff                    ; 0xc065f vberom.asm:230
    db  08bh, 0d0h
    ; mov dx, ax                                ; 8b d0                     ; 0xc0662 vberom.asm:231
    retf                                      ; cb                          ; 0xc0664 vberom.asm:232
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc0665 vberom.asm:234
    retf                                      ; cb                          ; 0xc0668 vberom.asm:235
    push dx                                   ; 52                          ; 0xc0669 vberom.asm:238
    push ax                                   ; 50                          ; 0xc066a vberom.asm:239
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc066b vberom.asm:240
    mov ax, strict word 00008h                ; b8 08 00                    ; 0xc066e vberom.asm:241
    call 00570h                               ; e8 fc fe                    ; 0xc0671 vberom.asm:242
    pop ax                                    ; 58                          ; 0xc0674 vberom.asm:243
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0675 vberom.asm:244
    call 00570h                               ; e8 f5 fe                    ; 0xc0678 vberom.asm:245
    pop dx                                    ; 5a                          ; 0xc067b vberom.asm:246
    retn                                      ; c3                          ; 0xc067c vberom.asm:247
    push dx                                   ; 52                          ; 0xc067d vberom.asm:250
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc067e vberom.asm:251
    mov ax, strict word 00008h                ; b8 08 00                    ; 0xc0681 vberom.asm:252
    call 00570h                               ; e8 e9 fe                    ; 0xc0684 vberom.asm:253
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0687 vberom.asm:254
    call 00577h                               ; e8 ea fe                    ; 0xc068a vberom.asm:255
    pop dx                                    ; 5a                          ; 0xc068d vberom.asm:256
    retn                                      ; c3                          ; 0xc068e vberom.asm:257
    push dx                                   ; 52                          ; 0xc068f vberom.asm:260
    push ax                                   ; 50                          ; 0xc0690 vberom.asm:261
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0691 vberom.asm:262
    mov ax, strict word 00009h                ; b8 09 00                    ; 0xc0694 vberom.asm:263
    call 00570h                               ; e8 d6 fe                    ; 0xc0697 vberom.asm:264
    pop ax                                    ; 58                          ; 0xc069a vberom.asm:265
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc069b vberom.asm:266
    call 00570h                               ; e8 cf fe                    ; 0xc069e vberom.asm:267
    pop dx                                    ; 5a                          ; 0xc06a1 vberom.asm:268
    retn                                      ; c3                          ; 0xc06a2 vberom.asm:269
    push dx                                   ; 52                          ; 0xc06a3 vberom.asm:272
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc06a4 vberom.asm:273
    mov ax, strict word 00009h                ; b8 09 00                    ; 0xc06a7 vberom.asm:274
    call 00570h                               ; e8 c3 fe                    ; 0xc06aa vberom.asm:275
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc06ad vberom.asm:276
    call 00577h                               ; e8 c4 fe                    ; 0xc06b0 vberom.asm:277
    pop dx                                    ; 5a                          ; 0xc06b3 vberom.asm:278
    retn                                      ; c3                          ; 0xc06b4 vberom.asm:279
    push ax                                   ; 50                          ; 0xc06b5 vberom.asm:282
    push bx                                   ; 53                          ; 0xc06b6 vberom.asm:283
    push dx                                   ; 52                          ; 0xc06b7 vberom.asm:284
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc06b8 vberom.asm:285
    call 00596h                               ; e8 d9 fe                    ; 0xc06ba vberom.asm:286
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc06bd vberom.asm:287
    jnbe short 006c3h                         ; 77 02                       ; 0xc06bf vberom.asm:288
    shr bx, 1                                 ; d1 eb                       ; 0xc06c1 vberom.asm:289
    shr bx, 003h                              ; c1 eb 03                    ; 0xc06c3 vberom.asm:292
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc06c6 vberom.asm:298
    db  08ah, 0e3h
    ; mov ah, bl                                ; 8a e3                     ; 0xc06c9 vberom.asm:299
    mov AL, strict byte 013h                  ; b0 13                       ; 0xc06cb vberom.asm:300
    out DX, ax                                ; ef                          ; 0xc06cd vberom.asm:301
    pop dx                                    ; 5a                          ; 0xc06ce vberom.asm:302
    pop bx                                    ; 5b                          ; 0xc06cf vberom.asm:303
    pop ax                                    ; 58                          ; 0xc06d0 vberom.asm:304
    retn                                      ; c3                          ; 0xc06d1 vberom.asm:305
  ; disGetNextSymbol 0xc06d2 LB 0x228 -> off=0x0 cb=00000000000000ed uValue=00000000000c06d2 '_vga_compat_setup'
_vga_compat_setup:                           ; 0xc06d2 LB 0xed
    push ax                                   ; 50                          ; 0xc06d2 vberom.asm:308
    push dx                                   ; 52                          ; 0xc06d3 vberom.asm:309
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc06d4 vberom.asm:312
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc06d7 vberom.asm:313
    call 00570h                               ; e8 93 fe                    ; 0xc06da vberom.asm:314
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc06dd vberom.asm:315
    call 00577h                               ; e8 94 fe                    ; 0xc06e0 vberom.asm:316
    push ax                                   ; 50                          ; 0xc06e3 vberom.asm:317
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc06e4 vberom.asm:318
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc06e7 vberom.asm:319
    out DX, ax                                ; ef                          ; 0xc06ea vberom.asm:320
    pop ax                                    ; 58                          ; 0xc06eb vberom.asm:321
    push ax                                   ; 50                          ; 0xc06ec vberom.asm:322
    shr ax, 003h                              ; c1 e8 03                    ; 0xc06ed vberom.asm:324
    dec ax                                    ; 48                          ; 0xc06f0 vberom.asm:330
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc06f1 vberom.asm:331
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc06f3 vberom.asm:332
    out DX, ax                                ; ef                          ; 0xc06f5 vberom.asm:333
    pop ax                                    ; 58                          ; 0xc06f6 vberom.asm:334
    call 006b5h                               ; e8 bb ff                    ; 0xc06f7 vberom.asm:335
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc06fa vberom.asm:338
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc06fd vberom.asm:339
    call 00570h                               ; e8 6d fe                    ; 0xc0700 vberom.asm:340
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0703 vberom.asm:341
    call 00577h                               ; e8 6e fe                    ; 0xc0706 vberom.asm:342
    dec ax                                    ; 48                          ; 0xc0709 vberom.asm:343
    push ax                                   ; 50                          ; 0xc070a vberom.asm:344
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc070b vberom.asm:345
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc070e vberom.asm:346
    mov AL, strict byte 012h                  ; b0 12                       ; 0xc0710 vberom.asm:347
    out DX, ax                                ; ef                          ; 0xc0712 vberom.asm:348
    pop ax                                    ; 58                          ; 0xc0713 vberom.asm:349
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc0714 vberom.asm:350
    out DX, AL                                ; ee                          ; 0xc0716 vberom.asm:351
    inc dx                                    ; 42                          ; 0xc0717 vberom.asm:352
    in AL, DX                                 ; ec                          ; 0xc0718 vberom.asm:353
    and AL, strict byte 0bdh                  ; 24 bd                       ; 0xc0719 vberom.asm:354
    test ah, 001h                             ; f6 c4 01                    ; 0xc071b vberom.asm:355
    je short 00722h                           ; 74 02                       ; 0xc071e vberom.asm:356
    or AL, strict byte 002h                   ; 0c 02                       ; 0xc0720 vberom.asm:357
    test ah, 002h                             ; f6 c4 02                    ; 0xc0722 vberom.asm:359
    je short 00729h                           ; 74 02                       ; 0xc0725 vberom.asm:360
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc0727 vberom.asm:361
    out DX, AL                                ; ee                          ; 0xc0729 vberom.asm:363
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc072a vberom.asm:366
    mov ax, strict word 00009h                ; b8 09 00                    ; 0xc072d vberom.asm:367
    out DX, AL                                ; ee                          ; 0xc0730 vberom.asm:368
    mov dx, 003d5h                            ; ba d5 03                    ; 0xc0731 vberom.asm:369
    in AL, DX                                 ; ec                          ; 0xc0734 vberom.asm:370
    and AL, strict byte 060h                  ; 24 60                       ; 0xc0735 vberom.asm:371
    out DX, AL                                ; ee                          ; 0xc0737 vberom.asm:372
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc0738 vberom.asm:373
    mov AL, strict byte 017h                  ; b0 17                       ; 0xc073b vberom.asm:374
    out DX, AL                                ; ee                          ; 0xc073d vberom.asm:375
    mov dx, 003d5h                            ; ba d5 03                    ; 0xc073e vberom.asm:376
    in AL, DX                                 ; ec                          ; 0xc0741 vberom.asm:377
    or AL, strict byte 003h                   ; 0c 03                       ; 0xc0742 vberom.asm:378
    out DX, AL                                ; ee                          ; 0xc0744 vberom.asm:379
    mov dx, 003dah                            ; ba da 03                    ; 0xc0745 vberom.asm:380
    in AL, DX                                 ; ec                          ; 0xc0748 vberom.asm:381
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0749 vberom.asm:382
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc074c vberom.asm:383
    out DX, AL                                ; ee                          ; 0xc074e vberom.asm:384
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc074f vberom.asm:385
    in AL, DX                                 ; ec                          ; 0xc0752 vberom.asm:386
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc0753 vberom.asm:387
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0755 vberom.asm:388
    out DX, AL                                ; ee                          ; 0xc0758 vberom.asm:389
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0759 vberom.asm:390
    out DX, AL                                ; ee                          ; 0xc075b vberom.asm:391
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc075c vberom.asm:392
    mov ax, 00506h                            ; b8 06 05                    ; 0xc075f vberom.asm:393
    out DX, ax                                ; ef                          ; 0xc0762 vberom.asm:394
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc0763 vberom.asm:395
    mov ax, 00f02h                            ; b8 02 0f                    ; 0xc0766 vberom.asm:396
    out DX, ax                                ; ef                          ; 0xc0769 vberom.asm:397
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc076a vberom.asm:400
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc076d vberom.asm:401
    call 00570h                               ; e8 fd fd                    ; 0xc0770 vberom.asm:402
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0773 vberom.asm:403
    call 00577h                               ; e8 fe fd                    ; 0xc0776 vberom.asm:404
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc0779 vberom.asm:405
    jc short 007bdh                           ; 72 40                       ; 0xc077b vberom.asm:406
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc077d vberom.asm:407
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc0780 vberom.asm:408
    out DX, AL                                ; ee                          ; 0xc0782 vberom.asm:409
    mov dx, 003d5h                            ; ba d5 03                    ; 0xc0783 vberom.asm:410
    in AL, DX                                 ; ec                          ; 0xc0786 vberom.asm:411
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc0787 vberom.asm:412
    out DX, AL                                ; ee                          ; 0xc0789 vberom.asm:413
    mov dx, 003dah                            ; ba da 03                    ; 0xc078a vberom.asm:414
    in AL, DX                                 ; ec                          ; 0xc078d vberom.asm:415
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc078e vberom.asm:416
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc0791 vberom.asm:417
    out DX, AL                                ; ee                          ; 0xc0793 vberom.asm:418
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0794 vberom.asm:419
    in AL, DX                                 ; ec                          ; 0xc0797 vberom.asm:420
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc0798 vberom.asm:421
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc079a vberom.asm:422
    out DX, AL                                ; ee                          ; 0xc079d vberom.asm:423
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc079e vberom.asm:424
    out DX, AL                                ; ee                          ; 0xc07a0 vberom.asm:425
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc07a1 vberom.asm:426
    mov AL, strict byte 004h                  ; b0 04                       ; 0xc07a4 vberom.asm:427
    out DX, AL                                ; ee                          ; 0xc07a6 vberom.asm:428
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc07a7 vberom.asm:429
    in AL, DX                                 ; ec                          ; 0xc07aa vberom.asm:430
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc07ab vberom.asm:431
    out DX, AL                                ; ee                          ; 0xc07ad vberom.asm:432
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc07ae vberom.asm:433
    mov AL, strict byte 005h                  ; b0 05                       ; 0xc07b1 vberom.asm:434
    out DX, AL                                ; ee                          ; 0xc07b3 vberom.asm:435
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc07b4 vberom.asm:436
    in AL, DX                                 ; ec                          ; 0xc07b7 vberom.asm:437
    and AL, strict byte 09fh                  ; 24 9f                       ; 0xc07b8 vberom.asm:438
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc07ba vberom.asm:439
    out DX, AL                                ; ee                          ; 0xc07bc vberom.asm:440
    pop dx                                    ; 5a                          ; 0xc07bd vberom.asm:443
    pop ax                                    ; 58                          ; 0xc07be vberom.asm:444
  ; disGetNextSymbol 0xc07bf LB 0x13b -> off=0x0 cb=0000000000000013 uValue=00000000000c07bf '_vbe_has_vbe_display'
_vbe_has_vbe_display:                        ; 0xc07bf LB 0x13
    push DS                                   ; 1e                          ; 0xc07bf vberom.asm:450
    push bx                                   ; 53                          ; 0xc07c0 vberom.asm:451
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc07c1 vberom.asm:452
    mov ds, ax                                ; 8e d8                       ; 0xc07c4 vberom.asm:453
    mov bx, 000b9h                            ; bb b9 00                    ; 0xc07c6 vberom.asm:454
    mov al, byte [bx]                         ; 8a 07                       ; 0xc07c9 vberom.asm:455
    and AL, strict byte 001h                  ; 24 01                       ; 0xc07cb vberom.asm:456
    db  032h, 0e4h
    ; xor ah, ah                                ; 32 e4                     ; 0xc07cd vberom.asm:457
    pop bx                                    ; 5b                          ; 0xc07cf vberom.asm:458
    pop DS                                    ; 1f                          ; 0xc07d0 vberom.asm:459
    retn                                      ; c3                          ; 0xc07d1 vberom.asm:460
  ; disGetNextSymbol 0xc07d2 LB 0x128 -> off=0x0 cb=0000000000000025 uValue=00000000000c07d2 'vbe_biosfn_return_current_mode'
vbe_biosfn_return_current_mode:              ; 0xc07d2 LB 0x25
    push DS                                   ; 1e                          ; 0xc07d2 vberom.asm:473
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc07d3 vberom.asm:474
    mov ds, ax                                ; 8e d8                       ; 0xc07d6 vberom.asm:475
    call 005f1h                               ; e8 16 fe                    ; 0xc07d8 vberom.asm:476
    and ax, strict byte 00001h                ; 83 e0 01                    ; 0xc07db vberom.asm:477
    je short 007e9h                           ; 74 09                       ; 0xc07de vberom.asm:478
    mov bx, 000bah                            ; bb ba 00                    ; 0xc07e0 vberom.asm:479
    mov ax, word [bx]                         ; 8b 07                       ; 0xc07e3 vberom.asm:480
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc07e5 vberom.asm:481
    jne short 007f2h                          ; 75 09                       ; 0xc07e7 vberom.asm:482
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc07e9 vberom.asm:484
    mov al, byte [bx]                         ; 8a 07                       ; 0xc07ec vberom.asm:485
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc07ee vberom.asm:486
    db  032h, 0ffh
    ; xor bh, bh                                ; 32 ff                     ; 0xc07f0 vberom.asm:487
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc07f2 vberom.asm:489
    pop DS                                    ; 1f                          ; 0xc07f5 vberom.asm:490
    retn                                      ; c3                          ; 0xc07f6 vberom.asm:491
  ; disGetNextSymbol 0xc07f7 LB 0x103 -> off=0x0 cb=000000000000002d uValue=00000000000c07f7 'vbe_biosfn_display_window_control'
vbe_biosfn_display_window_control:           ; 0xc07f7 LB 0x2d
    cmp bl, 000h                              ; 80 fb 00                    ; 0xc07f7 vberom.asm:515
    jne short 00820h                          ; 75 24                       ; 0xc07fa vberom.asm:516
    cmp bh, 001h                              ; 80 ff 01                    ; 0xc07fc vberom.asm:517
    je short 00817h                           ; 74 16                       ; 0xc07ff vberom.asm:518
    jc short 00807h                           ; 72 04                       ; 0xc0801 vberom.asm:519
    mov ax, 00100h                            ; b8 00 01                    ; 0xc0803 vberom.asm:520
    retn                                      ; c3                          ; 0xc0806 vberom.asm:521
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc0807 vberom.asm:523
    call 00603h                               ; e8 f7 fd                    ; 0xc0809 vberom.asm:524
    call 00617h                               ; e8 08 fe                    ; 0xc080c vberom.asm:525
    db  03bh, 0c2h
    ; cmp ax, dx                                ; 3b c2                     ; 0xc080f vberom.asm:526
    jne short 00820h                          ; 75 0d                       ; 0xc0811 vberom.asm:527
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0813 vberom.asm:528
    retn                                      ; c3                          ; 0xc0816 vberom.asm:529
    call 00617h                               ; e8 fd fd                    ; 0xc0817 vberom.asm:531
    db  08bh, 0d0h
    ; mov dx, ax                                ; 8b d0                     ; 0xc081a vberom.asm:532
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc081c vberom.asm:533
    retn                                      ; c3                          ; 0xc081f vberom.asm:534
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc0820 vberom.asm:536
    retn                                      ; c3                          ; 0xc0823 vberom.asm:537
  ; disGetNextSymbol 0xc0824 LB 0xd6 -> off=0x0 cb=0000000000000034 uValue=00000000000c0824 'vbe_biosfn_set_get_display_start'
vbe_biosfn_set_get_display_start:            ; 0xc0824 LB 0x34
    cmp bl, 080h                              ; 80 fb 80                    ; 0xc0824 vberom.asm:577
    je short 00834h                           ; 74 0b                       ; 0xc0827 vberom.asm:578
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc0829 vberom.asm:579
    je short 00848h                           ; 74 1a                       ; 0xc082c vberom.asm:580
    jc short 0083ah                           ; 72 0a                       ; 0xc082e vberom.asm:581
    mov ax, 00100h                            ; b8 00 01                    ; 0xc0830 vberom.asm:582
    retn                                      ; c3                          ; 0xc0833 vberom.asm:583
    call 00589h                               ; e8 52 fd                    ; 0xc0834 vberom.asm:585
    call 0057ch                               ; e8 42 fd                    ; 0xc0837 vberom.asm:586
    db  08bh, 0c1h
    ; mov ax, cx                                ; 8b c1                     ; 0xc083a vberom.asm:588
    call 00669h                               ; e8 2a fe                    ; 0xc083c vberom.asm:589
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc083f vberom.asm:590
    call 0068fh                               ; e8 4b fe                    ; 0xc0841 vberom.asm:591
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0844 vberom.asm:592
    retn                                      ; c3                          ; 0xc0847 vberom.asm:593
    call 0067dh                               ; e8 32 fe                    ; 0xc0848 vberom.asm:595
    db  08bh, 0c8h
    ; mov cx, ax                                ; 8b c8                     ; 0xc084b vberom.asm:596
    call 006a3h                               ; e8 53 fe                    ; 0xc084d vberom.asm:597
    db  08bh, 0d0h
    ; mov dx, ax                                ; 8b d0                     ; 0xc0850 vberom.asm:598
    db  032h, 0ffh
    ; xor bh, bh                                ; 32 ff                     ; 0xc0852 vberom.asm:599
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0854 vberom.asm:600
    retn                                      ; c3                          ; 0xc0857 vberom.asm:601
  ; disGetNextSymbol 0xc0858 LB 0xa2 -> off=0x0 cb=0000000000000037 uValue=00000000000c0858 'vbe_biosfn_set_get_dac_palette_format'
vbe_biosfn_set_get_dac_palette_format:       ; 0xc0858 LB 0x37
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc0858 vberom.asm:616
    je short 0087bh                           ; 74 1e                       ; 0xc085b vberom.asm:617
    jc short 00863h                           ; 72 04                       ; 0xc085d vberom.asm:618
    mov ax, 00100h                            ; b8 00 01                    ; 0xc085f vberom.asm:619
    retn                                      ; c3                          ; 0xc0862 vberom.asm:620
    call 005f1h                               ; e8 8b fd                    ; 0xc0863 vberom.asm:622
    cmp bh, 006h                              ; 80 ff 06                    ; 0xc0866 vberom.asm:623
    je short 00875h                           ; 74 0a                       ; 0xc0869 vberom.asm:624
    cmp bh, 008h                              ; 80 ff 08                    ; 0xc086b vberom.asm:625
    jne short 0088bh                          ; 75 1b                       ; 0xc086e vberom.asm:626
    or ax, strict byte 00020h                 ; 83 c8 20                    ; 0xc0870 vberom.asm:627
    jne short 00878h                          ; 75 03                       ; 0xc0873 vberom.asm:628
    and ax, strict byte 0ffdfh                ; 83 e0 df                    ; 0xc0875 vberom.asm:630
    call 005ddh                               ; e8 62 fd                    ; 0xc0878 vberom.asm:632
    mov BH, strict byte 006h                  ; b7 06                       ; 0xc087b vberom.asm:634
    call 005f1h                               ; e8 71 fd                    ; 0xc087d vberom.asm:635
    and ax, strict byte 00020h                ; 83 e0 20                    ; 0xc0880 vberom.asm:636
    je short 00887h                           ; 74 02                       ; 0xc0883 vberom.asm:637
    mov BH, strict byte 008h                  ; b7 08                       ; 0xc0885 vberom.asm:638
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0887 vberom.asm:640
    retn                                      ; c3                          ; 0xc088a vberom.asm:641
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc088b vberom.asm:643
    retn                                      ; c3                          ; 0xc088e vberom.asm:644
  ; disGetNextSymbol 0xc088f LB 0x6b -> off=0x0 cb=0000000000000057 uValue=00000000000c088f 'vbe_biosfn_set_get_palette_data'
vbe_biosfn_set_get_palette_data:             ; 0xc088f LB 0x57
    test bl, bl                               ; 84 db                       ; 0xc088f vberom.asm:683
    je short 008a2h                           ; 74 0f                       ; 0xc0891 vberom.asm:684
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc0893 vberom.asm:685
    je short 008c2h                           ; 74 2a                       ; 0xc0896 vberom.asm:686
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc0898 vberom.asm:687
    jbe short 008e2h                          ; 76 45                       ; 0xc089b vberom.asm:688
    cmp bl, 080h                              ; 80 fb 80                    ; 0xc089d vberom.asm:689
    jne short 008deh                          ; 75 3c                       ; 0xc08a0 vberom.asm:690
    pushaw                                    ; 60                          ; 0xc08a2 vberom.asm:143
    push DS                                   ; 1e                          ; 0xc08a3 vberom.asm:696
    push ES                                   ; 06                          ; 0xc08a4 vberom.asm:697
    pop DS                                    ; 1f                          ; 0xc08a5 vberom.asm:698
    db  08ah, 0c2h
    ; mov al, dl                                ; 8a c2                     ; 0xc08a6 vberom.asm:699
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc08a8 vberom.asm:700
    out DX, AL                                ; ee                          ; 0xc08ab vberom.asm:701
    inc dx                                    ; 42                          ; 0xc08ac vberom.asm:702
    db  08bh, 0f7h
    ; mov si, di                                ; 8b f7                     ; 0xc08ad vberom.asm:703
    lodsw                                     ; ad                          ; 0xc08af vberom.asm:714
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc08b0 vberom.asm:715
    lodsw                                     ; ad                          ; 0xc08b2 vberom.asm:716
    out DX, AL                                ; ee                          ; 0xc08b3 vberom.asm:717
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc08b4 vberom.asm:718
    out DX, AL                                ; ee                          ; 0xc08b6 vberom.asm:719
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc08b7 vberom.asm:720
    out DX, AL                                ; ee                          ; 0xc08b9 vberom.asm:721
    loop 008afh                               ; e2 f3                       ; 0xc08ba vberom.asm:723
    pop DS                                    ; 1f                          ; 0xc08bc vberom.asm:724
    popaw                                     ; 61                          ; 0xc08bd vberom.asm:162
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc08be vberom.asm:727
    retn                                      ; c3                          ; 0xc08c1 vberom.asm:728
    pushaw                                    ; 60                          ; 0xc08c2 vberom.asm:143
    db  08ah, 0c2h
    ; mov al, dl                                ; 8a c2                     ; 0xc08c3 vberom.asm:732
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc08c5 vberom.asm:733
    out DX, AL                                ; ee                          ; 0xc08c8 vberom.asm:734
    add dl, 002h                              ; 80 c2 02                    ; 0xc08c9 vberom.asm:735
    db  033h, 0dbh
    ; xor bx, bx                                ; 33 db                     ; 0xc08cc vberom.asm:746
    in AL, DX                                 ; ec                          ; 0xc08ce vberom.asm:748
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc08cf vberom.asm:749
    in AL, DX                                 ; ec                          ; 0xc08d1 vberom.asm:750
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc08d2 vberom.asm:751
    in AL, DX                                 ; ec                          ; 0xc08d4 vberom.asm:752
    stosw                                     ; ab                          ; 0xc08d5 vberom.asm:753
    db  08bh, 0c3h
    ; mov ax, bx                                ; 8b c3                     ; 0xc08d6 vberom.asm:754
    stosw                                     ; ab                          ; 0xc08d8 vberom.asm:755
    loop 008ceh                               ; e2 f3                       ; 0xc08d9 vberom.asm:757
    popaw                                     ; 61                          ; 0xc08db vberom.asm:162
    jmp short 008beh                          ; eb e0                       ; 0xc08dc vberom.asm:759
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc08de vberom.asm:762
    retn                                      ; c3                          ; 0xc08e1 vberom.asm:763
    mov ax, 0024fh                            ; b8 4f 02                    ; 0xc08e2 vberom.asm:765
    retn                                      ; c3                          ; 0xc08e5 vberom.asm:766
  ; disGetNextSymbol 0xc08e6 LB 0x14 -> off=0x0 cb=0000000000000014 uValue=00000000000c08e6 'vbe_biosfn_return_protected_mode_interface'
vbe_biosfn_return_protected_mode_interface: ; 0xc08e6 LB 0x14
    test bl, bl                               ; 84 db                       ; 0xc08e6 vberom.asm:780
    jne short 008f6h                          ; 75 0c                       ; 0xc08e8 vberom.asm:781
    push CS                                   ; 0e                          ; 0xc08ea vberom.asm:782
    pop ES                                    ; 07                          ; 0xc08eb vberom.asm:783
    mov di, 04640h                            ; bf 40 46                    ; 0xc08ec vberom.asm:784
    mov cx, 00115h                            ; b9 15 01                    ; 0xc08ef vberom.asm:785
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc08f2 vberom.asm:786
    retn                                      ; c3                          ; 0xc08f5 vberom.asm:787
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc08f6 vberom.asm:789
    retn                                      ; c3                          ; 0xc08f9 vberom.asm:790

  ; Padding 0xf6 bytes at 0xc08fa
  times 246 db 0

section _TEXT progbits vstart=0x9f0 align=1 ; size=0x3b53 class=CODE group=AUTO
  ; disGetNextSymbol 0xc09f0 LB 0x3b53 -> off=0x0 cb=000000000000001b uValue=00000000000c09f0 'set_int_vector'
set_int_vector:                              ; 0xc09f0 LB 0x1b
    push dx                                   ; 52                          ; 0xc09f0 vgabios.c:87
    push bp                                   ; 55                          ; 0xc09f1
    mov bp, sp                                ; 89 e5                       ; 0xc09f2
    mov dx, bx                                ; 89 da                       ; 0xc09f4
    mov bl, al                                ; 88 c3                       ; 0xc09f6 vgabios.c:91
    xor bh, bh                                ; 30 ff                       ; 0xc09f8
    sal bx, 002h                              ; c1 e3 02                    ; 0xc09fa
    xor ax, ax                                ; 31 c0                       ; 0xc09fd
    mov es, ax                                ; 8e c0                       ; 0xc09ff
    mov word [es:bx], dx                      ; 26 89 17                    ; 0xc0a01
    mov word [es:bx+002h], cx                 ; 26 89 4f 02                 ; 0xc0a04
    pop bp                                    ; 5d                          ; 0xc0a08 vgabios.c:92
    pop dx                                    ; 5a                          ; 0xc0a09
    retn                                      ; c3                          ; 0xc0a0a
  ; disGetNextSymbol 0xc0a0b LB 0x3b38 -> off=0x0 cb=000000000000001c uValue=00000000000c0a0b 'init_vga_card'
init_vga_card:                               ; 0xc0a0b LB 0x1c
    push bp                                   ; 55                          ; 0xc0a0b vgabios.c:143
    mov bp, sp                                ; 89 e5                       ; 0xc0a0c
    push dx                                   ; 52                          ; 0xc0a0e
    mov AL, strict byte 0c3h                  ; b0 c3                       ; 0xc0a0f vgabios.c:146
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc0a11
    out DX, AL                                ; ee                          ; 0xc0a14
    mov AL, strict byte 004h                  ; b0 04                       ; 0xc0a15 vgabios.c:149
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc0a17
    out DX, AL                                ; ee                          ; 0xc0a1a
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc0a1b vgabios.c:150
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc0a1d
    out DX, AL                                ; ee                          ; 0xc0a20
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc0a21 vgabios.c:155
    pop dx                                    ; 5a                          ; 0xc0a24
    pop bp                                    ; 5d                          ; 0xc0a25
    retn                                      ; c3                          ; 0xc0a26
  ; disGetNextSymbol 0xc0a27 LB 0x3b1c -> off=0x0 cb=000000000000003e uValue=00000000000c0a27 'init_bios_area'
init_bios_area:                              ; 0xc0a27 LB 0x3e
    push bx                                   ; 53                          ; 0xc0a27 vgabios.c:221
    push bp                                   ; 55                          ; 0xc0a28
    mov bp, sp                                ; 89 e5                       ; 0xc0a29
    xor bx, bx                                ; 31 db                       ; 0xc0a2b vgabios.c:225
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0a2d
    mov es, ax                                ; 8e c0                       ; 0xc0a30
    mov al, byte [es:bx+010h]                 ; 26 8a 47 10                 ; 0xc0a32 vgabios.c:228
    and AL, strict byte 0cfh                  ; 24 cf                       ; 0xc0a36
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc0a38
    mov byte [es:bx+010h], al                 ; 26 88 47 10                 ; 0xc0a3a
    mov byte [es:bx+00085h], 010h             ; 26 c6 87 85 00 10           ; 0xc0a3e vgabios.c:232
    mov word [es:bx+00087h], 0f960h           ; 26 c7 87 87 00 60 f9        ; 0xc0a44 vgabios.c:234
    mov byte [es:bx+00089h], 051h             ; 26 c6 87 89 00 51           ; 0xc0a4b vgabios.c:238
    mov byte [es:bx+065h], 009h               ; 26 c6 47 65 09              ; 0xc0a51 vgabios.c:240
    mov word [es:bx+000a8h], 0554eh           ; 26 c7 87 a8 00 4e 55        ; 0xc0a56 vgabios.c:242
    mov [es:bx+000aah], ds                    ; 26 8c 9f aa 00              ; 0xc0a5d
    pop bp                                    ; 5d                          ; 0xc0a62 vgabios.c:243
    pop bx                                    ; 5b                          ; 0xc0a63
    retn                                      ; c3                          ; 0xc0a64
  ; disGetNextSymbol 0xc0a65 LB 0x3ade -> off=0x0 cb=0000000000000031 uValue=00000000000c0a65 'vgabios_init_func'
vgabios_init_func:                           ; 0xc0a65 LB 0x31
    inc bp                                    ; 45                          ; 0xc0a65 vgabios.c:250
    push bp                                   ; 55                          ; 0xc0a66
    mov bp, sp                                ; 89 e5                       ; 0xc0a67
    call 00a0bh                               ; e8 9f ff                    ; 0xc0a69 vgabios.c:252
    call 00a27h                               ; e8 b8 ff                    ; 0xc0a6c vgabios.c:253
    call 03ebdh                               ; e8 4b 34                    ; 0xc0a6f vgabios.c:255
    mov bx, strict word 00028h                ; bb 28 00                    ; 0xc0a72 vgabios.c:257
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0a75
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc0a78
    call 009f0h                               ; e8 72 ff                    ; 0xc0a7b
    mov bx, strict word 00028h                ; bb 28 00                    ; 0xc0a7e vgabios.c:258
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0a81
    mov ax, strict word 0006dh                ; b8 6d 00                    ; 0xc0a84
    call 009f0h                               ; e8 66 ff                    ; 0xc0a87
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc0a8a vgabios.c:284
    db  032h, 0e4h
    ; xor ah, ah                                ; 32 e4                     ; 0xc0a8d
    int 010h                                  ; cd 10                       ; 0xc0a8f
    mov sp, bp                                ; 89 ec                       ; 0xc0a91 vgabios.c:287
    pop bp                                    ; 5d                          ; 0xc0a93
    dec bp                                    ; 4d                          ; 0xc0a94
    retf                                      ; cb                          ; 0xc0a95
  ; disGetNextSymbol 0xc0a96 LB 0x3aad -> off=0x0 cb=000000000000002e uValue=00000000000c0a96 'vga_get_cursor_pos'
vga_get_cursor_pos:                          ; 0xc0a96 LB 0x2e
    push si                                   ; 56                          ; 0xc0a96 vgabios.c:356
    push di                                   ; 57                          ; 0xc0a97
    push bp                                   ; 55                          ; 0xc0a98
    mov bp, sp                                ; 89 e5                       ; 0xc0a99
    mov si, dx                                ; 89 d6                       ; 0xc0a9b
    mov di, strict word 00060h                ; bf 60 00                    ; 0xc0a9d vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0aa0
    mov es, dx                                ; 8e c2                       ; 0xc0aa3
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0aa5
    push SS                                   ; 16                          ; 0xc0aa8 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0aa9
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc0aaa
    xor ah, ah                                ; 30 e4                       ; 0xc0aad vgabios.c:360
    mov si, ax                                ; 89 c6                       ; 0xc0aaf
    add si, ax                                ; 01 c6                       ; 0xc0ab1
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc0ab3
    mov es, dx                                ; 8e c2                       ; 0xc0ab6 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc0ab8
    push SS                                   ; 16                          ; 0xc0abb vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0abc
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0abd
    pop bp                                    ; 5d                          ; 0xc0ac0 vgabios.c:361
    pop di                                    ; 5f                          ; 0xc0ac1
    pop si                                    ; 5e                          ; 0xc0ac2
    retn                                      ; c3                          ; 0xc0ac3
  ; disGetNextSymbol 0xc0ac4 LB 0x3a7f -> off=0x0 cb=000000000000005e uValue=00000000000c0ac4 'vga_find_glyph'
vga_find_glyph:                              ; 0xc0ac4 LB 0x5e
    push bp                                   ; 55                          ; 0xc0ac4 vgabios.c:364
    mov bp, sp                                ; 89 e5                       ; 0xc0ac5
    push si                                   ; 56                          ; 0xc0ac7
    push di                                   ; 57                          ; 0xc0ac8
    push ax                                   ; 50                          ; 0xc0ac9
    push ax                                   ; 50                          ; 0xc0aca
    push dx                                   ; 52                          ; 0xc0acb
    push bx                                   ; 53                          ; 0xc0acc
    mov bl, cl                                ; 88 cb                       ; 0xc0acd
    mov word [bp-006h], strict word 00000h    ; c7 46 fa 00 00              ; 0xc0acf vgabios.c:366
    dec word [bp+004h]                        ; ff 4e 04                    ; 0xc0ad4 vgabios.c:368
    cmp word [bp+004h], strict byte 0ffffh    ; 83 7e 04 ff                 ; 0xc0ad7
    je short 00b16h                           ; 74 39                       ; 0xc0adb
    mov cl, byte [bp+006h]                    ; 8a 4e 06                    ; 0xc0add vgabios.c:369
    xor ch, ch                                ; 30 ed                       ; 0xc0ae0
    mov dx, ss                                ; 8c d2                       ; 0xc0ae2
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc0ae4
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc0ae7
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc0aea
    push DS                                   ; 1e                          ; 0xc0aed
    mov ds, dx                                ; 8e da                       ; 0xc0aee
    rep cmpsb                                 ; f3 a6                       ; 0xc0af0
    pop DS                                    ; 1f                          ; 0xc0af2
    mov ax, strict word 00000h                ; b8 00 00                    ; 0xc0af3
    je short 00afah                           ; 74 02                       ; 0xc0af6
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc0af8
    test ax, ax                               ; 85 c0                       ; 0xc0afa
    jne short 00b0ah                          ; 75 0c                       ; 0xc0afc
    mov al, bl                                ; 88 d8                       ; 0xc0afe vgabios.c:370
    xor ah, ah                                ; 30 e4                       ; 0xc0b00
    or ah, 080h                               ; 80 cc 80                    ; 0xc0b02
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0b05
    jmp short 00b16h                          ; eb 0c                       ; 0xc0b08 vgabios.c:371
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc0b0a vgabios.c:373
    xor ah, ah                                ; 30 e4                       ; 0xc0b0d
    add word [bp-008h], ax                    ; 01 46 f8                    ; 0xc0b0f
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc0b12 vgabios.c:374
    jmp short 00ad4h                          ; eb be                       ; 0xc0b14 vgabios.c:375
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc0b16 vgabios.c:377
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b19
    pop di                                    ; 5f                          ; 0xc0b1c
    pop si                                    ; 5e                          ; 0xc0b1d
    pop bp                                    ; 5d                          ; 0xc0b1e
    retn 00004h                               ; c2 04 00                    ; 0xc0b1f
  ; disGetNextSymbol 0xc0b22 LB 0x3a21 -> off=0x0 cb=0000000000000046 uValue=00000000000c0b22 'vga_read_glyph_planar'
vga_read_glyph_planar:                       ; 0xc0b22 LB 0x46
    push bp                                   ; 55                          ; 0xc0b22 vgabios.c:379
    mov bp, sp                                ; 89 e5                       ; 0xc0b23
    push si                                   ; 56                          ; 0xc0b25
    push di                                   ; 57                          ; 0xc0b26
    push ax                                   ; 50                          ; 0xc0b27
    push ax                                   ; 50                          ; 0xc0b28
    mov si, ax                                ; 89 c6                       ; 0xc0b29
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc0b2b
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc0b2e
    mov bx, cx                                ; 89 cb                       ; 0xc0b31
    mov ax, 00805h                            ; b8 05 08                    ; 0xc0b33 vgabios.c:386
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b36
    out DX, ax                                ; ef                          ; 0xc0b39
    dec byte [bp+004h]                        ; fe 4e 04                    ; 0xc0b3a vgabios.c:388
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc0b3d
    je short 00b58h                           ; 74 15                       ; 0xc0b41
    mov es, [bp-006h]                         ; 8e 46 fa                    ; 0xc0b43 vgabios.c:389
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc0b46
    not al                                    ; f6 d0                       ; 0xc0b49
    mov di, bx                                ; 89 df                       ; 0xc0b4b
    inc bx                                    ; 43                          ; 0xc0b4d
    push SS                                   ; 16                          ; 0xc0b4e
    pop ES                                    ; 07                          ; 0xc0b4f
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0b50
    add si, word [bp-008h]                    ; 03 76 f8                    ; 0xc0b53 vgabios.c:390
    jmp short 00b3ah                          ; eb e2                       ; 0xc0b56 vgabios.c:391
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0b58 vgabios.c:394
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b5b
    out DX, ax                                ; ef                          ; 0xc0b5e
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b5f vgabios.c:395
    pop di                                    ; 5f                          ; 0xc0b62
    pop si                                    ; 5e                          ; 0xc0b63
    pop bp                                    ; 5d                          ; 0xc0b64
    retn 00002h                               ; c2 02 00                    ; 0xc0b65
  ; disGetNextSymbol 0xc0b68 LB 0x39db -> off=0x0 cb=000000000000002f uValue=00000000000c0b68 'vga_char_ofs_planar'
vga_char_ofs_planar:                         ; 0xc0b68 LB 0x2f
    push si                                   ; 56                          ; 0xc0b68 vgabios.c:397
    push bp                                   ; 55                          ; 0xc0b69
    mov bp, sp                                ; 89 e5                       ; 0xc0b6a
    mov ch, al                                ; 88 c5                       ; 0xc0b6c
    mov al, dl                                ; 88 d0                       ; 0xc0b6e
    xor ah, ah                                ; 30 e4                       ; 0xc0b70 vgabios.c:401
    mul bx                                    ; f7 e3                       ; 0xc0b72
    mov bl, byte [bp+006h]                    ; 8a 5e 06                    ; 0xc0b74
    xor bh, bh                                ; 30 ff                       ; 0xc0b77
    mul bx                                    ; f7 e3                       ; 0xc0b79
    mov bl, ch                                ; 88 eb                       ; 0xc0b7b
    add bx, ax                                ; 01 c3                       ; 0xc0b7d
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc0b7f vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0b82
    mov es, ax                                ; 8e c0                       ; 0xc0b85
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc0b87
    mov al, cl                                ; 88 c8                       ; 0xc0b8a vgabios.c:58
    xor ah, ah                                ; 30 e4                       ; 0xc0b8c
    mul si                                    ; f7 e6                       ; 0xc0b8e
    add ax, bx                                ; 01 d8                       ; 0xc0b90
    pop bp                                    ; 5d                          ; 0xc0b92 vgabios.c:405
    pop si                                    ; 5e                          ; 0xc0b93
    retn 00002h                               ; c2 02 00                    ; 0xc0b94
  ; disGetNextSymbol 0xc0b97 LB 0x39ac -> off=0x0 cb=0000000000000040 uValue=00000000000c0b97 'vga_read_char_planar'
vga_read_char_planar:                        ; 0xc0b97 LB 0x40
    push bp                                   ; 55                          ; 0xc0b97 vgabios.c:407
    mov bp, sp                                ; 89 e5                       ; 0xc0b98
    push cx                                   ; 51                          ; 0xc0b9a
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0b9b
    mov byte [bp-004h], bl                    ; 88 5e fc                    ; 0xc0b9e vgabios.c:411
    mov byte [bp-003h], 000h                  ; c6 46 fd 00                 ; 0xc0ba1
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0ba5
    lea cx, [bp-014h]                         ; 8d 4e ec                    ; 0xc0ba8
    mov bx, ax                                ; 89 c3                       ; 0xc0bab
    mov ax, dx                                ; 89 d0                       ; 0xc0bad
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0baf
    call 00b22h                               ; e8 6d ff                    ; 0xc0bb2
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0bb5 vgabios.c:414
    push 00100h                               ; 68 00 01                    ; 0xc0bb8
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0bbb vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0bbe
    mov es, ax                                ; 8e c0                       ; 0xc0bc0
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0bc2
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0bc5
    xor cx, cx                                ; 31 c9                       ; 0xc0bc9 vgabios.c:68
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc0bcb
    call 00ac4h                               ; e8 f3 fe                    ; 0xc0bce
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc0bd1 vgabios.c:415
    pop cx                                    ; 59                          ; 0xc0bd4
    pop bp                                    ; 5d                          ; 0xc0bd5
    retn                                      ; c3                          ; 0xc0bd6
  ; disGetNextSymbol 0xc0bd7 LB 0x396c -> off=0x0 cb=0000000000000024 uValue=00000000000c0bd7 'vga_char_ofs_linear'
vga_char_ofs_linear:                         ; 0xc0bd7 LB 0x24
    enter 00002h, 000h                        ; c8 02 00 00                 ; 0xc0bd7 vgabios.c:417
    mov byte [bp-002h], al                    ; 88 46 fe                    ; 0xc0bdb
    mov al, dl                                ; 88 d0                       ; 0xc0bde vgabios.c:421
    xor ah, ah                                ; 30 e4                       ; 0xc0be0
    mul bx                                    ; f7 e3                       ; 0xc0be2
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc0be4
    xor dh, dh                                ; 30 f6                       ; 0xc0be7
    mul dx                                    ; f7 e2                       ; 0xc0be9
    mov dx, ax                                ; 89 c2                       ; 0xc0beb
    mov al, byte [bp-002h]                    ; 8a 46 fe                    ; 0xc0bed
    xor ah, ah                                ; 30 e4                       ; 0xc0bf0
    add ax, dx                                ; 01 d0                       ; 0xc0bf2
    sal ax, 003h                              ; c1 e0 03                    ; 0xc0bf4 vgabios.c:422
    leave                                     ; c9                          ; 0xc0bf7 vgabios.c:424
    retn 00002h                               ; c2 02 00                    ; 0xc0bf8
  ; disGetNextSymbol 0xc0bfb LB 0x3948 -> off=0x0 cb=000000000000004b uValue=00000000000c0bfb 'vga_read_glyph_linear'
vga_read_glyph_linear:                       ; 0xc0bfb LB 0x4b
    push si                                   ; 56                          ; 0xc0bfb vgabios.c:426
    push di                                   ; 57                          ; 0xc0bfc
    enter 00004h, 000h                        ; c8 04 00 00                 ; 0xc0bfd
    mov si, ax                                ; 89 c6                       ; 0xc0c01
    mov word [bp-002h], dx                    ; 89 56 fe                    ; 0xc0c03
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc0c06
    mov bx, cx                                ; 89 cb                       ; 0xc0c09
    dec byte [bp+008h]                        ; fe 4e 08                    ; 0xc0c0b vgabios.c:432
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc0c0e
    je short 00c40h                           ; 74 2c                       ; 0xc0c12
    xor dh, dh                                ; 30 f6                       ; 0xc0c14 vgabios.c:433
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc0c16 vgabios.c:434
    xor ax, ax                                ; 31 c0                       ; 0xc0c18 vgabios.c:435
    jmp short 00c21h                          ; eb 05                       ; 0xc0c1a
    cmp ax, strict word 00008h                ; 3d 08 00                    ; 0xc0c1c
    jnl short 00c35h                          ; 7d 14                       ; 0xc0c1f
    mov es, [bp-002h]                         ; 8e 46 fe                    ; 0xc0c21 vgabios.c:436
    mov di, si                                ; 89 f7                       ; 0xc0c24
    add di, ax                                ; 01 c7                       ; 0xc0c26
    cmp byte [es:di], 000h                    ; 26 80 3d 00                 ; 0xc0c28
    je short 00c30h                           ; 74 02                       ; 0xc0c2c
    or dh, dl                                 ; 08 d6                       ; 0xc0c2e vgabios.c:437
    shr dl, 1                                 ; d0 ea                       ; 0xc0c30 vgabios.c:438
    inc ax                                    ; 40                          ; 0xc0c32 vgabios.c:439
    jmp short 00c1ch                          ; eb e7                       ; 0xc0c33
    mov di, bx                                ; 89 df                       ; 0xc0c35 vgabios.c:440
    inc bx                                    ; 43                          ; 0xc0c37
    mov byte [ss:di], dh                      ; 36 88 35                    ; 0xc0c38
    add si, word [bp-004h]                    ; 03 76 fc                    ; 0xc0c3b vgabios.c:441
    jmp short 00c0bh                          ; eb cb                       ; 0xc0c3e vgabios.c:442
    leave                                     ; c9                          ; 0xc0c40 vgabios.c:443
    pop di                                    ; 5f                          ; 0xc0c41
    pop si                                    ; 5e                          ; 0xc0c42
    retn 00002h                               ; c2 02 00                    ; 0xc0c43
  ; disGetNextSymbol 0xc0c46 LB 0x38fd -> off=0x0 cb=0000000000000045 uValue=00000000000c0c46 'vga_read_char_linear'
vga_read_char_linear:                        ; 0xc0c46 LB 0x45
    push bp                                   ; 55                          ; 0xc0c46 vgabios.c:445
    mov bp, sp                                ; 89 e5                       ; 0xc0c47
    push cx                                   ; 51                          ; 0xc0c49
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0c4a
    mov cx, ax                                ; 89 c1                       ; 0xc0c4d
    mov ax, dx                                ; 89 d0                       ; 0xc0c4f
    mov byte [bp-004h], bl                    ; 88 5e fc                    ; 0xc0c51 vgabios.c:449
    mov byte [bp-003h], 000h                  ; c6 46 fd 00                 ; 0xc0c54
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0c58
    mov bx, cx                                ; 89 cb                       ; 0xc0c5b
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0c5d
    lea cx, [bp-014h]                         ; 8d 4e ec                    ; 0xc0c60
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0c63
    call 00bfbh                               ; e8 92 ff                    ; 0xc0c66
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0c69 vgabios.c:452
    push 00100h                               ; 68 00 01                    ; 0xc0c6c
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0c6f vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0c72
    mov es, ax                                ; 8e c0                       ; 0xc0c74
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c76
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0c79
    xor cx, cx                                ; 31 c9                       ; 0xc0c7d vgabios.c:68
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc0c7f
    call 00ac4h                               ; e8 3f fe                    ; 0xc0c82
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc0c85 vgabios.c:453
    pop cx                                    ; 59                          ; 0xc0c88
    pop bp                                    ; 5d                          ; 0xc0c89
    retn                                      ; c3                          ; 0xc0c8a
  ; disGetNextSymbol 0xc0c8b LB 0x38b8 -> off=0x0 cb=0000000000000035 uValue=00000000000c0c8b 'vga_read_2bpp_char'
vga_read_2bpp_char:                          ; 0xc0c8b LB 0x35
    push bp                                   ; 55                          ; 0xc0c8b vgabios.c:455
    mov bp, sp                                ; 89 e5                       ; 0xc0c8c
    push bx                                   ; 53                          ; 0xc0c8e
    push cx                                   ; 51                          ; 0xc0c8f
    mov bx, ax                                ; 89 c3                       ; 0xc0c90
    mov es, dx                                ; 8e c2                       ; 0xc0c92
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0c94 vgabios.c:461
    mov DH, strict byte 080h                  ; b6 80                       ; 0xc0c97 vgabios.c:462
    xor dl, dl                                ; 30 d2                       ; 0xc0c99 vgabios.c:463
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c9b vgabios.c:464
    db  086h, 0c4h
    ; xchg ah, al                               ; 86 c4                     ; 0xc0c9e
    xor bx, bx                                ; 31 db                       ; 0xc0ca0 vgabios.c:466
    jmp short 00ca9h                          ; eb 05                       ; 0xc0ca2
    cmp bx, strict byte 00008h                ; 83 fb 08                    ; 0xc0ca4
    jnl short 00cb7h                          ; 7d 0e                       ; 0xc0ca7
    test ax, cx                               ; 85 c8                       ; 0xc0ca9 vgabios.c:467
    je short 00cafh                           ; 74 02                       ; 0xc0cab
    or dl, dh                                 ; 08 f2                       ; 0xc0cad vgabios.c:468
    shr dh, 1                                 ; d0 ee                       ; 0xc0caf vgabios.c:469
    shr cx, 002h                              ; c1 e9 02                    ; 0xc0cb1 vgabios.c:470
    inc bx                                    ; 43                          ; 0xc0cb4 vgabios.c:471
    jmp short 00ca4h                          ; eb ed                       ; 0xc0cb5
    mov al, dl                                ; 88 d0                       ; 0xc0cb7 vgabios.c:473
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0cb9
    pop cx                                    ; 59                          ; 0xc0cbc
    pop bx                                    ; 5b                          ; 0xc0cbd
    pop bp                                    ; 5d                          ; 0xc0cbe
    retn                                      ; c3                          ; 0xc0cbf
  ; disGetNextSymbol 0xc0cc0 LB 0x3883 -> off=0x0 cb=0000000000000084 uValue=00000000000c0cc0 'vga_read_glyph_cga'
vga_read_glyph_cga:                          ; 0xc0cc0 LB 0x84
    push bp                                   ; 55                          ; 0xc0cc0 vgabios.c:475
    mov bp, sp                                ; 89 e5                       ; 0xc0cc1
    push cx                                   ; 51                          ; 0xc0cc3
    push si                                   ; 56                          ; 0xc0cc4
    push di                                   ; 57                          ; 0xc0cc5
    push ax                                   ; 50                          ; 0xc0cc6
    mov si, dx                                ; 89 d6                       ; 0xc0cc7
    cmp bl, 006h                              ; 80 fb 06                    ; 0xc0cc9 vgabios.c:483
    je short 00d08h                           ; 74 3a                       ; 0xc0ccc
    mov bx, ax                                ; 89 c3                       ; 0xc0cce vgabios.c:485
    add bx, ax                                ; 01 c3                       ; 0xc0cd0
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0cd2
    xor cx, cx                                ; 31 c9                       ; 0xc0cd7 vgabios.c:487
    jmp short 00ce0h                          ; eb 05                       ; 0xc0cd9
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0cdb
    jnl short 00d3ch                          ; 7d 5c                       ; 0xc0cde
    mov ax, bx                                ; 89 d8                       ; 0xc0ce0 vgabios.c:488
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0ce2
    call 00c8bh                               ; e8 a3 ff                    ; 0xc0ce5
    mov di, si                                ; 89 f7                       ; 0xc0ce8
    inc si                                    ; 46                          ; 0xc0cea
    push SS                                   ; 16                          ; 0xc0ceb
    pop ES                                    ; 07                          ; 0xc0cec
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0ced
    lea ax, [bx+02000h]                       ; 8d 87 00 20                 ; 0xc0cf0 vgabios.c:489
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0cf4
    call 00c8bh                               ; e8 91 ff                    ; 0xc0cf7
    mov di, si                                ; 89 f7                       ; 0xc0cfa
    inc si                                    ; 46                          ; 0xc0cfc
    push SS                                   ; 16                          ; 0xc0cfd
    pop ES                                    ; 07                          ; 0xc0cfe
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0cff
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d02 vgabios.c:490
    inc cx                                    ; 41                          ; 0xc0d05 vgabios.c:491
    jmp short 00cdbh                          ; eb d3                       ; 0xc0d06
    mov bx, ax                                ; 89 c3                       ; 0xc0d08 vgabios.c:493
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0d0a
    xor cx, cx                                ; 31 c9                       ; 0xc0d0f vgabios.c:494
    jmp short 00d18h                          ; eb 05                       ; 0xc0d11
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0d13
    jnl short 00d3ch                          ; 7d 24                       ; 0xc0d16
    mov di, si                                ; 89 f7                       ; 0xc0d18 vgabios.c:495
    inc si                                    ; 46                          ; 0xc0d1a
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d1b
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0d1e
    push SS                                   ; 16                          ; 0xc0d21
    pop ES                                    ; 07                          ; 0xc0d22
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d23
    mov di, si                                ; 89 f7                       ; 0xc0d26 vgabios.c:496
    inc si                                    ; 46                          ; 0xc0d28
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d29
    mov al, byte [es:bx+02000h]               ; 26 8a 87 00 20              ; 0xc0d2c
    push SS                                   ; 16                          ; 0xc0d31
    pop ES                                    ; 07                          ; 0xc0d32
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d33
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d36 vgabios.c:497
    inc cx                                    ; 41                          ; 0xc0d39 vgabios.c:498
    jmp short 00d13h                          ; eb d7                       ; 0xc0d3a
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc0d3c vgabios.c:500
    pop di                                    ; 5f                          ; 0xc0d3f
    pop si                                    ; 5e                          ; 0xc0d40
    pop cx                                    ; 59                          ; 0xc0d41
    pop bp                                    ; 5d                          ; 0xc0d42
    retn                                      ; c3                          ; 0xc0d43
  ; disGetNextSymbol 0xc0d44 LB 0x37ff -> off=0x0 cb=000000000000001a uValue=00000000000c0d44 'vga_char_ofs_cga'
vga_char_ofs_cga:                            ; 0xc0d44 LB 0x1a
    push cx                                   ; 51                          ; 0xc0d44 vgabios.c:502
    push bp                                   ; 55                          ; 0xc0d45
    mov bp, sp                                ; 89 e5                       ; 0xc0d46
    mov cl, al                                ; 88 c1                       ; 0xc0d48
    mov al, dl                                ; 88 d0                       ; 0xc0d4a
    xor ah, ah                                ; 30 e4                       ; 0xc0d4c vgabios.c:507
    mul bx                                    ; f7 e3                       ; 0xc0d4e
    mov bx, ax                                ; 89 c3                       ; 0xc0d50
    sal bx, 002h                              ; c1 e3 02                    ; 0xc0d52
    mov al, cl                                ; 88 c8                       ; 0xc0d55
    xor ah, ah                                ; 30 e4                       ; 0xc0d57
    add ax, bx                                ; 01 d8                       ; 0xc0d59
    pop bp                                    ; 5d                          ; 0xc0d5b vgabios.c:508
    pop cx                                    ; 59                          ; 0xc0d5c
    retn                                      ; c3                          ; 0xc0d5d
  ; disGetNextSymbol 0xc0d5e LB 0x37e5 -> off=0x0 cb=0000000000000066 uValue=00000000000c0d5e 'vga_read_char_cga'
vga_read_char_cga:                           ; 0xc0d5e LB 0x66
    push bp                                   ; 55                          ; 0xc0d5e vgabios.c:510
    mov bp, sp                                ; 89 e5                       ; 0xc0d5f
    push bx                                   ; 53                          ; 0xc0d61
    push cx                                   ; 51                          ; 0xc0d62
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc0d63
    mov bl, dl                                ; 88 d3                       ; 0xc0d66 vgabios.c:516
    xor bh, bh                                ; 30 ff                       ; 0xc0d68
    lea dx, [bp-00eh]                         ; 8d 56 f2                    ; 0xc0d6a
    call 00cc0h                               ; e8 50 ff                    ; 0xc0d6d
    push strict byte 00008h                   ; 6a 08                       ; 0xc0d70 vgabios.c:519
    push 00080h                               ; 68 80 00                    ; 0xc0d72
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0d75 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d78
    mov es, ax                                ; 8e c0                       ; 0xc0d7a
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0d7c
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0d7f
    xor cx, cx                                ; 31 c9                       ; 0xc0d83 vgabios.c:68
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0d85
    call 00ac4h                               ; e8 39 fd                    ; 0xc0d88
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0d8b
    test ah, 080h                             ; f6 c4 80                    ; 0xc0d8e vgabios.c:521
    jne short 00dbah                          ; 75 27                       ; 0xc0d91
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0d93 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d96
    mov es, ax                                ; 8e c0                       ; 0xc0d98
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0d9a
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0d9d
    test dx, dx                               ; 85 d2                       ; 0xc0da1 vgabios.c:525
    jne short 00da9h                          ; 75 04                       ; 0xc0da3
    test ax, ax                               ; 85 c0                       ; 0xc0da5
    je short 00dbah                           ; 74 11                       ; 0xc0da7
    push strict byte 00008h                   ; 6a 08                       ; 0xc0da9 vgabios.c:526
    push 00080h                               ; 68 80 00                    ; 0xc0dab
    mov cx, 00080h                            ; b9 80 00                    ; 0xc0dae
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0db1
    call 00ac4h                               ; e8 0d fd                    ; 0xc0db4
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0db7
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc0dba vgabios.c:529
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0dbd
    pop cx                                    ; 59                          ; 0xc0dc0
    pop bx                                    ; 5b                          ; 0xc0dc1
    pop bp                                    ; 5d                          ; 0xc0dc2
    retn                                      ; c3                          ; 0xc0dc3
  ; disGetNextSymbol 0xc0dc4 LB 0x377f -> off=0x0 cb=0000000000000130 uValue=00000000000c0dc4 'vga_read_char_attr'
vga_read_char_attr:                          ; 0xc0dc4 LB 0x130
    push bp                                   ; 55                          ; 0xc0dc4 vgabios.c:531
    mov bp, sp                                ; 89 e5                       ; 0xc0dc5
    push bx                                   ; 53                          ; 0xc0dc7
    push cx                                   ; 51                          ; 0xc0dc8
    push si                                   ; 56                          ; 0xc0dc9
    push di                                   ; 57                          ; 0xc0dca
    sub sp, strict byte 00014h                ; 83 ec 14                    ; 0xc0dcb
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc0dce
    mov si, dx                                ; 89 d6                       ; 0xc0dd1
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0dd3 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0dd6
    mov es, ax                                ; 8e c0                       ; 0xc0dd9
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0ddb
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc0dde vgabios.c:48
    xor ah, ah                                ; 30 e4                       ; 0xc0de1 vgabios.c:539
    call 0382ah                               ; e8 44 2a                    ; 0xc0de3
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc0de6
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0de9 vgabios.c:540
    jne short 00df0h                          ; 75 03                       ; 0xc0deb
    jmp near 00eebh                           ; e9 fb 00                    ; 0xc0ded
    mov cl, byte [bp-00eh]                    ; 8a 4e f2                    ; 0xc0df0 vgabios.c:544
    xor ch, ch                                ; 30 ed                       ; 0xc0df3
    lea bx, [bp-01ch]                         ; 8d 5e e4                    ; 0xc0df5
    lea dx, [bp-01ah]                         ; 8d 56 e6                    ; 0xc0df8
    mov ax, cx                                ; 89 c8                       ; 0xc0dfb
    call 00a96h                               ; e8 96 fc                    ; 0xc0dfd
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc0e00 vgabios.c:545
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0e03
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc0e06 vgabios.c:546
    xor al, al                                ; 30 c0                       ; 0xc0e09
    shr ax, 008h                              ; c1 e8 08                    ; 0xc0e0b
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc0e0e
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc0e11
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0e14 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0e17
    mov es, ax                                ; 8e c0                       ; 0xc0e1a
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0e1c
    xor ah, ah                                ; 30 e4                       ; 0xc0e1f vgabios.c:48
    inc ax                                    ; 40                          ; 0xc0e21
    mov word [bp-014h], ax                    ; 89 46 ec                    ; 0xc0e22
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc0e25 vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0e28
    mov word [bp-018h], di                    ; 89 7e e8                    ; 0xc0e2b vgabios.c:58
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc0e2e vgabios.c:552
    xor bh, bh                                ; 30 ff                       ; 0xc0e31
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0e33
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc0e36
    jne short 00e6dh                          ; 75 30                       ; 0xc0e3b
    mov ax, di                                ; 89 f8                       ; 0xc0e3d vgabios.c:554
    mul word [bp-014h]                        ; f7 66 ec                    ; 0xc0e3f
    add ax, ax                                ; 01 c0                       ; 0xc0e42
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc0e44
    inc ax                                    ; 40                          ; 0xc0e46
    mul cx                                    ; f7 e1                       ; 0xc0e47
    mov cx, ax                                ; 89 c1                       ; 0xc0e49
    mov al, byte [bp-016h]                    ; 8a 46 ea                    ; 0xc0e4b
    xor ah, ah                                ; 30 e4                       ; 0xc0e4e
    mul di                                    ; f7 e7                       ; 0xc0e50
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc0e52
    xor dh, dh                                ; 30 f6                       ; 0xc0e55
    mov di, ax                                ; 89 c7                       ; 0xc0e57
    add di, dx                                ; 01 d7                       ; 0xc0e59
    add di, di                                ; 01 ff                       ; 0xc0e5b
    add di, cx                                ; 01 cf                       ; 0xc0e5d
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc0e5f vgabios.c:55
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc0e63
    push SS                                   ; 16                          ; 0xc0e66 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0e67
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0e68
    jmp short 00dedh                          ; eb 80                       ; 0xc0e6b vgabios.c:556
    mov bl, byte [bx+047aeh]                  ; 8a 9f ae 47                 ; 0xc0e6d vgabios.c:557
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc0e71
    je short 00ec4h                           ; 74 4e                       ; 0xc0e74
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc0e76
    jc short 00eebh                           ; 72 70                       ; 0xc0e79
    jbe short 00e84h                          ; 76 07                       ; 0xc0e7b
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc0e7d
    jbe short 00e9dh                          ; 76 1b                       ; 0xc0e80
    jmp short 00eebh                          ; eb 67                       ; 0xc0e82
    xor dh, dh                                ; 30 f6                       ; 0xc0e84 vgabios.c:560
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc0e86
    xor ah, ah                                ; 30 e4                       ; 0xc0e89
    mov bx, word [bp-018h]                    ; 8b 5e e8                    ; 0xc0e8b
    call 00d44h                               ; e8 b3 fe                    ; 0xc0e8e
    mov dl, byte [bp-010h]                    ; 8a 56 f0                    ; 0xc0e91 vgabios.c:561
    xor dh, dh                                ; 30 f6                       ; 0xc0e94
    call 00d5eh                               ; e8 c5 fe                    ; 0xc0e96
    xor ah, ah                                ; 30 e4                       ; 0xc0e99
    jmp short 00e66h                          ; eb c9                       ; 0xc0e9b
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0e9d vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0ea0
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc0ea3 vgabios.c:566
    mov byte [bp-011h], ch                    ; 88 6e ef                    ; 0xc0ea6
    push word [bp-012h]                       ; ff 76 ee                    ; 0xc0ea9
    xor dh, dh                                ; 30 f6                       ; 0xc0eac
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc0eae
    xor ah, ah                                ; 30 e4                       ; 0xc0eb1
    mov bx, di                                ; 89 fb                       ; 0xc0eb3
    call 00b68h                               ; e8 b0 fc                    ; 0xc0eb5
    mov bx, word [bp-012h]                    ; 8b 5e ee                    ; 0xc0eb8 vgabios.c:567
    mov dx, ax                                ; 89 c2                       ; 0xc0ebb
    mov ax, di                                ; 89 f8                       ; 0xc0ebd
    call 00b97h                               ; e8 d5 fc                    ; 0xc0ebf
    jmp short 00e99h                          ; eb d5                       ; 0xc0ec2
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0ec4 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0ec7
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc0eca vgabios.c:571
    mov byte [bp-011h], ch                    ; 88 6e ef                    ; 0xc0ecd
    push word [bp-012h]                       ; ff 76 ee                    ; 0xc0ed0
    xor dh, dh                                ; 30 f6                       ; 0xc0ed3
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc0ed5
    xor ah, ah                                ; 30 e4                       ; 0xc0ed8
    mov bx, di                                ; 89 fb                       ; 0xc0eda
    call 00bd7h                               ; e8 f8 fc                    ; 0xc0edc
    mov bx, word [bp-012h]                    ; 8b 5e ee                    ; 0xc0edf vgabios.c:572
    mov dx, ax                                ; 89 c2                       ; 0xc0ee2
    mov ax, di                                ; 89 f8                       ; 0xc0ee4
    call 00c46h                               ; e8 5d fd                    ; 0xc0ee6
    jmp short 00e99h                          ; eb ae                       ; 0xc0ee9
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc0eeb vgabios.c:581
    pop di                                    ; 5f                          ; 0xc0eee
    pop si                                    ; 5e                          ; 0xc0eef
    pop cx                                    ; 59                          ; 0xc0ef0
    pop bx                                    ; 5b                          ; 0xc0ef1
    pop bp                                    ; 5d                          ; 0xc0ef2
    retn                                      ; c3                          ; 0xc0ef3
  ; disGetNextSymbol 0xc0ef4 LB 0x364f -> off=0x10 cb=0000000000000083 uValue=00000000000c0f04 'vga_get_font_info'
    db  01bh, 00fh, 060h, 00fh, 065h, 00fh, 06ch, 00fh, 071h, 00fh, 076h, 00fh, 07bh, 00fh, 080h, 00fh
vga_get_font_info:                           ; 0xc0f04 LB 0x83
    push si                                   ; 56                          ; 0xc0f04 vgabios.c:583
    push di                                   ; 57                          ; 0xc0f05
    push bp                                   ; 55                          ; 0xc0f06
    mov bp, sp                                ; 89 e5                       ; 0xc0f07
    mov si, dx                                ; 89 d6                       ; 0xc0f09
    mov di, bx                                ; 89 df                       ; 0xc0f0b
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc0f0d vgabios.c:588
    jnbe short 00f5ah                         ; 77 48                       ; 0xc0f10
    mov bx, ax                                ; 89 c3                       ; 0xc0f12
    add bx, ax                                ; 01 c3                       ; 0xc0f14
    jmp word [cs:bx+00ef4h]                   ; 2e ff a7 f4 0e              ; 0xc0f16
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0f1b vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0f1e
    mov es, ax                                ; 8e c0                       ; 0xc0f20
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0f22
    mov ax, word [es:bx+002h]                 ; 26 8b 47 02                 ; 0xc0f25
    push SS                                   ; 16                          ; 0xc0f29 vgabios.c:591
    pop ES                                    ; 07                          ; 0xc0f2a
    mov word [es:di], dx                      ; 26 89 15                    ; 0xc0f2b
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0f2e
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0f31
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f34
    mov es, ax                                ; 8e c0                       ; 0xc0f37
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f39
    xor ah, ah                                ; 30 e4                       ; 0xc0f3c
    push SS                                   ; 16                          ; 0xc0f3e
    pop ES                                    ; 07                          ; 0xc0f3f
    mov bx, cx                                ; 89 cb                       ; 0xc0f40
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f42
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0f45
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f48
    mov es, ax                                ; 8e c0                       ; 0xc0f4b
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f4d
    xor ah, ah                                ; 30 e4                       ; 0xc0f50
    push SS                                   ; 16                          ; 0xc0f52
    pop ES                                    ; 07                          ; 0xc0f53
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc0f54
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f57
    pop bp                                    ; 5d                          ; 0xc0f5a
    pop di                                    ; 5f                          ; 0xc0f5b
    pop si                                    ; 5e                          ; 0xc0f5c
    retn 00002h                               ; c2 02 00                    ; 0xc0f5d
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0f60 vgabios.c:67
    jmp short 00f1eh                          ; eb b9                       ; 0xc0f63
    mov dx, 05d6ah                            ; ba 6a 5d                    ; 0xc0f65 vgabios.c:596
    mov ax, ds                                ; 8c d8                       ; 0xc0f68
    jmp short 00f29h                          ; eb bd                       ; 0xc0f6a vgabios.c:597
    mov dx, 0556ah                            ; ba 6a 55                    ; 0xc0f6c vgabios.c:599
    jmp short 00f68h                          ; eb f7                       ; 0xc0f6f
    mov dx, 0596ah                            ; ba 6a 59                    ; 0xc0f71 vgabios.c:602
    jmp short 00f68h                          ; eb f2                       ; 0xc0f74
    mov dx, 07b6ah                            ; ba 6a 7b                    ; 0xc0f76 vgabios.c:605
    jmp short 00f68h                          ; eb ed                       ; 0xc0f79
    mov dx, 06b6ah                            ; ba 6a 6b                    ; 0xc0f7b vgabios.c:608
    jmp short 00f68h                          ; eb e8                       ; 0xc0f7e
    mov dx, 07c97h                            ; ba 97 7c                    ; 0xc0f80 vgabios.c:611
    jmp short 00f68h                          ; eb e3                       ; 0xc0f83
    jmp short 00f5ah                          ; eb d3                       ; 0xc0f85 vgabios.c:617
  ; disGetNextSymbol 0xc0f87 LB 0x35bc -> off=0x0 cb=0000000000000166 uValue=00000000000c0f87 'vga_read_pixel'
vga_read_pixel:                              ; 0xc0f87 LB 0x166
    push bp                                   ; 55                          ; 0xc0f87 vgabios.c:630
    mov bp, sp                                ; 89 e5                       ; 0xc0f88
    push si                                   ; 56                          ; 0xc0f8a
    push di                                   ; 57                          ; 0xc0f8b
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc0f8c
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0f8f
    mov si, dx                                ; 89 d6                       ; 0xc0f92
    mov dx, bx                                ; 89 da                       ; 0xc0f94
    mov word [bp-00ch], cx                    ; 89 4e f4                    ; 0xc0f96
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0f99 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f9c
    mov es, ax                                ; 8e c0                       ; 0xc0f9f
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0fa1
    xor ah, ah                                ; 30 e4                       ; 0xc0fa4 vgabios.c:637
    call 0382ah                               ; e8 81 28                    ; 0xc0fa6
    mov ah, al                                ; 88 c4                       ; 0xc0fa9
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0fab vgabios.c:638
    je short 00fbdh                           ; 74 0e                       ; 0xc0fad
    mov bl, al                                ; 88 c3                       ; 0xc0faf vgabios.c:640
    xor bh, bh                                ; 30 ff                       ; 0xc0fb1
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0fb3
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc0fb6
    jne short 00fc0h                          ; 75 03                       ; 0xc0fbb
    jmp near 010e6h                           ; e9 26 01                    ; 0xc0fbd vgabios.c:641
    mov ch, byte [bx+047aeh]                  ; 8a af ae 47                 ; 0xc0fc0 vgabios.c:644
    cmp ch, 003h                              ; 80 fd 03                    ; 0xc0fc4
    jc short 00fd8h                           ; 72 0f                       ; 0xc0fc7
    jbe short 00fe0h                          ; 76 15                       ; 0xc0fc9
    cmp ch, 005h                              ; 80 fd 05                    ; 0xc0fcb
    je short 01017h                           ; 74 47                       ; 0xc0fce
    cmp ch, 004h                              ; 80 fd 04                    ; 0xc0fd0
    je short 00fe0h                           ; 74 0b                       ; 0xc0fd3
    jmp near 010dch                           ; e9 04 01                    ; 0xc0fd5
    cmp ch, 002h                              ; 80 fd 02                    ; 0xc0fd8
    je short 0104eh                           ; 74 71                       ; 0xc0fdb
    jmp near 010dch                           ; e9 fc 00                    ; 0xc0fdd
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc0fe0 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0fe3
    mov es, ax                                ; 8e c0                       ; 0xc0fe6
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc0fe8
    mov ax, dx                                ; 89 d0                       ; 0xc0feb vgabios.c:58
    mul bx                                    ; f7 e3                       ; 0xc0fed
    mov bx, si                                ; 89 f3                       ; 0xc0fef
    shr bx, 003h                              ; c1 eb 03                    ; 0xc0ff1
    add bx, ax                                ; 01 c3                       ; 0xc0ff4
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc0ff6 vgabios.c:57
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc0ff9
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc0ffc vgabios.c:58
    xor dh, dh                                ; 30 f6                       ; 0xc0fff
    mul dx                                    ; f7 e2                       ; 0xc1001
    add bx, ax                                ; 01 c3                       ; 0xc1003
    mov cx, si                                ; 89 f1                       ; 0xc1005 vgabios.c:649
    and cx, strict byte 00007h                ; 83 e1 07                    ; 0xc1007
    mov ax, 00080h                            ; b8 80 00                    ; 0xc100a
    sar ax, CL                                ; d3 f8                       ; 0xc100d
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc100f
    mov byte [bp-006h], ch                    ; 88 6e fa                    ; 0xc1012 vgabios.c:651
    jmp short 01020h                          ; eb 09                       ; 0xc1015
    jmp near 010bch                           ; e9 a2 00                    ; 0xc1017
    cmp byte [bp-006h], 004h                  ; 80 7e fa 04                 ; 0xc101a
    jnc short 0104bh                          ; 73 2b                       ; 0xc101e
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1020 vgabios.c:652
    xor ah, ah                                ; 30 e4                       ; 0xc1023
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1025
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc1028
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc102a
    out DX, ax                                ; ef                          ; 0xc102d
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc102e vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc1031
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1033
    and al, byte [bp-008h]                    ; 22 46 f8                    ; 0xc1036 vgabios.c:48
    test al, al                               ; 84 c0                       ; 0xc1039 vgabios.c:654
    jbe short 01046h                          ; 76 09                       ; 0xc103b
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc103d vgabios.c:655
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc1040
    sal al, CL                                ; d2 e0                       ; 0xc1042
    or ch, al                                 ; 08 c5                       ; 0xc1044
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1046 vgabios.c:656
    jmp short 0101ah                          ; eb cf                       ; 0xc1049
    jmp near 010deh                           ; e9 90 00                    ; 0xc104b
    mov cl, byte [bx+047afh]                  ; 8a 8f af 47                 ; 0xc104e vgabios.c:659
    xor ch, ch                                ; 30 ed                       ; 0xc1052
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc1054
    sub bx, cx                                ; 29 cb                       ; 0xc1057
    mov cx, bx                                ; 89 d9                       ; 0xc1059
    mov bx, si                                ; 89 f3                       ; 0xc105b
    shr bx, CL                                ; d3 eb                       ; 0xc105d
    mov cx, bx                                ; 89 d9                       ; 0xc105f
    mov bx, dx                                ; 89 d3                       ; 0xc1061
    shr bx, 1                                 ; d1 eb                       ; 0xc1063
    imul bx, bx, strict byte 00050h           ; 6b db 50                    ; 0xc1065
    add bx, cx                                ; 01 cb                       ; 0xc1068
    test dl, 001h                             ; f6 c2 01                    ; 0xc106a vgabios.c:660
    je short 01072h                           ; 74 03                       ; 0xc106d
    add bh, 020h                              ; 80 c7 20                    ; 0xc106f vgabios.c:661
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1072 vgabios.c:47
    mov es, dx                                ; 8e c2                       ; 0xc1075
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1077
    mov bl, ah                                ; 88 e3                       ; 0xc107a vgabios.c:663
    xor bh, bh                                ; 30 ff                       ; 0xc107c
    sal bx, 003h                              ; c1 e3 03                    ; 0xc107e
    cmp byte [bx+047afh], 002h                ; 80 bf af 47 02              ; 0xc1081
    jne short 010a3h                          ; 75 1b                       ; 0xc1086
    mov cx, si                                ; 89 f1                       ; 0xc1088 vgabios.c:664
    xor ch, ch                                ; 30 ed                       ; 0xc108a
    and cl, 003h                              ; 80 e1 03                    ; 0xc108c
    mov dx, strict word 00003h                ; ba 03 00                    ; 0xc108f
    sub dx, cx                                ; 29 ca                       ; 0xc1092
    mov cx, dx                                ; 89 d1                       ; 0xc1094
    add cx, dx                                ; 01 d1                       ; 0xc1096
    xor ah, ah                                ; 30 e4                       ; 0xc1098
    sar ax, CL                                ; d3 f8                       ; 0xc109a
    mov ch, al                                ; 88 c5                       ; 0xc109c
    and ch, 003h                              ; 80 e5 03                    ; 0xc109e
    jmp short 010deh                          ; eb 3b                       ; 0xc10a1 vgabios.c:665
    mov cx, si                                ; 89 f1                       ; 0xc10a3 vgabios.c:666
    xor ch, ch                                ; 30 ed                       ; 0xc10a5
    and cl, 007h                              ; 80 e1 07                    ; 0xc10a7
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc10aa
    sub dx, cx                                ; 29 ca                       ; 0xc10ad
    mov cx, dx                                ; 89 d1                       ; 0xc10af
    xor ah, ah                                ; 30 e4                       ; 0xc10b1
    sar ax, CL                                ; d3 f8                       ; 0xc10b3
    mov ch, al                                ; 88 c5                       ; 0xc10b5
    and ch, 001h                              ; 80 e5 01                    ; 0xc10b7
    jmp short 010deh                          ; eb 22                       ; 0xc10ba vgabios.c:667
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc10bc vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc10bf
    mov es, ax                                ; 8e c0                       ; 0xc10c2
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc10c4
    sal bx, 003h                              ; c1 e3 03                    ; 0xc10c7 vgabios.c:58
    mov ax, dx                                ; 89 d0                       ; 0xc10ca
    mul bx                                    ; f7 e3                       ; 0xc10cc
    mov bx, si                                ; 89 f3                       ; 0xc10ce
    add bx, ax                                ; 01 c3                       ; 0xc10d0
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc10d2 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc10d5
    mov ch, byte [es:bx]                      ; 26 8a 2f                    ; 0xc10d7
    jmp short 010deh                          ; eb 02                       ; 0xc10da vgabios.c:671
    xor ch, ch                                ; 30 ed                       ; 0xc10dc vgabios.c:676
    push SS                                   ; 16                          ; 0xc10de vgabios.c:678
    pop ES                                    ; 07                          ; 0xc10df
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc10e0
    mov byte [es:bx], ch                      ; 26 88 2f                    ; 0xc10e3
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc10e6 vgabios.c:679
    pop di                                    ; 5f                          ; 0xc10e9
    pop si                                    ; 5e                          ; 0xc10ea
    pop bp                                    ; 5d                          ; 0xc10eb
    retn                                      ; c3                          ; 0xc10ec
  ; disGetNextSymbol 0xc10ed LB 0x3456 -> off=0x0 cb=000000000000008d uValue=00000000000c10ed 'biosfn_perform_gray_scale_summing'
biosfn_perform_gray_scale_summing:           ; 0xc10ed LB 0x8d
    push bp                                   ; 55                          ; 0xc10ed vgabios.c:684
    mov bp, sp                                ; 89 e5                       ; 0xc10ee
    push bx                                   ; 53                          ; 0xc10f0
    push cx                                   ; 51                          ; 0xc10f1
    push si                                   ; 56                          ; 0xc10f2
    push di                                   ; 57                          ; 0xc10f3
    push ax                                   ; 50                          ; 0xc10f4
    push ax                                   ; 50                          ; 0xc10f5
    mov bx, ax                                ; 89 c3                       ; 0xc10f6
    mov di, dx                                ; 89 d7                       ; 0xc10f8
    mov dx, 003dah                            ; ba da 03                    ; 0xc10fa vgabios.c:689
    in AL, DX                                 ; ec                          ; 0xc10fd
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10fe
    xor al, al                                ; 30 c0                       ; 0xc1100 vgabios.c:690
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1102
    out DX, AL                                ; ee                          ; 0xc1105
    xor si, si                                ; 31 f6                       ; 0xc1106 vgabios.c:692
    cmp si, di                                ; 39 fe                       ; 0xc1108
    jnc short 0115fh                          ; 73 53                       ; 0xc110a
    mov al, bl                                ; 88 d8                       ; 0xc110c vgabios.c:695
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc110e
    out DX, AL                                ; ee                          ; 0xc1111
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1112 vgabios.c:697
    in AL, DX                                 ; ec                          ; 0xc1115
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1116
    mov cx, ax                                ; 89 c1                       ; 0xc1118
    in AL, DX                                 ; ec                          ; 0xc111a vgabios.c:698
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc111b
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc111d
    in AL, DX                                 ; ec                          ; 0xc1120 vgabios.c:699
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1121
    xor ch, ch                                ; 30 ed                       ; 0xc1123 vgabios.c:702
    imul cx, cx, strict byte 0004dh           ; 6b c9 4d                    ; 0xc1125
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc1128
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc112b
    xor ch, ch                                ; 30 ed                       ; 0xc112e
    imul cx, cx, 00097h                       ; 69 c9 97 00                 ; 0xc1130
    add cx, word [bp-00ah]                    ; 03 4e f6                    ; 0xc1134
    xor ah, ah                                ; 30 e4                       ; 0xc1137
    imul ax, ax, strict byte 0001ch           ; 6b c0 1c                    ; 0xc1139
    add cx, ax                                ; 01 c1                       ; 0xc113c
    add cx, 00080h                            ; 81 c1 80 00                 ; 0xc113e
    sar cx, 008h                              ; c1 f9 08                    ; 0xc1142
    cmp cx, strict byte 0003fh                ; 83 f9 3f                    ; 0xc1145 vgabios.c:704
    jbe short 0114dh                          ; 76 03                       ; 0xc1148
    mov cx, strict word 0003fh                ; b9 3f 00                    ; 0xc114a
    mov al, bl                                ; 88 d8                       ; 0xc114d vgabios.c:707
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc114f
    out DX, AL                                ; ee                          ; 0xc1152
    mov al, cl                                ; 88 c8                       ; 0xc1153 vgabios.c:709
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1155
    out DX, AL                                ; ee                          ; 0xc1158
    out DX, AL                                ; ee                          ; 0xc1159 vgabios.c:710
    out DX, AL                                ; ee                          ; 0xc115a vgabios.c:711
    inc bx                                    ; 43                          ; 0xc115b vgabios.c:712
    inc si                                    ; 46                          ; 0xc115c vgabios.c:713
    jmp short 01108h                          ; eb a9                       ; 0xc115d
    mov dx, 003dah                            ; ba da 03                    ; 0xc115f vgabios.c:714
    in AL, DX                                 ; ec                          ; 0xc1162
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1163
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc1165 vgabios.c:715
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1167
    out DX, AL                                ; ee                          ; 0xc116a
    mov dx, 003dah                            ; ba da 03                    ; 0xc116b vgabios.c:717
    in AL, DX                                 ; ec                          ; 0xc116e
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc116f
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1171 vgabios.c:719
    pop di                                    ; 5f                          ; 0xc1174
    pop si                                    ; 5e                          ; 0xc1175
    pop cx                                    ; 59                          ; 0xc1176
    pop bx                                    ; 5b                          ; 0xc1177
    pop bp                                    ; 5d                          ; 0xc1178
    retn                                      ; c3                          ; 0xc1179
  ; disGetNextSymbol 0xc117a LB 0x33c9 -> off=0x0 cb=0000000000000107 uValue=00000000000c117a 'biosfn_set_cursor_shape'
biosfn_set_cursor_shape:                     ; 0xc117a LB 0x107
    push bp                                   ; 55                          ; 0xc117a vgabios.c:722
    mov bp, sp                                ; 89 e5                       ; 0xc117b
    push bx                                   ; 53                          ; 0xc117d
    push cx                                   ; 51                          ; 0xc117e
    push si                                   ; 56                          ; 0xc117f
    push ax                                   ; 50                          ; 0xc1180
    push ax                                   ; 50                          ; 0xc1181
    mov bl, al                                ; 88 c3                       ; 0xc1182
    mov ah, dl                                ; 88 d4                       ; 0xc1184
    mov dl, al                                ; 88 c2                       ; 0xc1186 vgabios.c:728
    xor dh, dh                                ; 30 f6                       ; 0xc1188
    mov cx, dx                                ; 89 d1                       ; 0xc118a
    sal cx, 008h                              ; c1 e1 08                    ; 0xc118c
    mov dl, ah                                ; 88 e2                       ; 0xc118f
    add dx, cx                                ; 01 ca                       ; 0xc1191
    mov si, strict word 00060h                ; be 60 00                    ; 0xc1193 vgabios.c:62
    mov cx, strict word 00040h                ; b9 40 00                    ; 0xc1196
    mov es, cx                                ; 8e c1                       ; 0xc1199
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc119b
    mov si, 00087h                            ; be 87 00                    ; 0xc119e vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc11a1
    test dl, 008h                             ; f6 c2 08                    ; 0xc11a4 vgabios.c:48
    jne short 011e6h                          ; 75 3d                       ; 0xc11a7
    mov dl, al                                ; 88 c2                       ; 0xc11a9 vgabios.c:734
    and dl, 060h                              ; 80 e2 60                    ; 0xc11ab
    cmp dl, 020h                              ; 80 fa 20                    ; 0xc11ae
    jne short 011b9h                          ; 75 06                       ; 0xc11b1
    mov BL, strict byte 01eh                  ; b3 1e                       ; 0xc11b3 vgabios.c:736
    xor ah, ah                                ; 30 e4                       ; 0xc11b5 vgabios.c:737
    jmp short 011e6h                          ; eb 2d                       ; 0xc11b7 vgabios.c:738
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc11b9 vgabios.c:47
    test dl, 001h                             ; f6 c2 01                    ; 0xc11bc vgabios.c:48
    jne short 0121bh                          ; 75 5a                       ; 0xc11bf
    cmp bl, 020h                              ; 80 fb 20                    ; 0xc11c1
    jnc short 0121bh                          ; 73 55                       ; 0xc11c4
    cmp ah, 020h                              ; 80 fc 20                    ; 0xc11c6
    jnc short 0121bh                          ; 73 50                       ; 0xc11c9
    mov si, 00085h                            ; be 85 00                    ; 0xc11cb vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc11ce
    mov es, dx                                ; 8e c2                       ; 0xc11d1
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc11d3
    mov dx, cx                                ; 89 ca                       ; 0xc11d6 vgabios.c:58
    cmp ah, bl                                ; 38 dc                       ; 0xc11d8 vgabios.c:749
    jnc short 011e8h                          ; 73 0c                       ; 0xc11da
    test ah, ah                               ; 84 e4                       ; 0xc11dc vgabios.c:751
    je short 0121bh                           ; 74 3b                       ; 0xc11de
    xor bl, bl                                ; 30 db                       ; 0xc11e0 vgabios.c:752
    mov ah, cl                                ; 88 cc                       ; 0xc11e2 vgabios.c:753
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc11e4
    jmp short 0121bh                          ; eb 33                       ; 0xc11e6 vgabios.c:755
    mov byte [bp-008h], ah                    ; 88 66 f8                    ; 0xc11e8 vgabios.c:756
    xor al, al                                ; 30 c0                       ; 0xc11eb
    mov byte [bp-007h], al                    ; 88 46 f9                    ; 0xc11ed
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc11f0
    mov byte [bp-009h], al                    ; 88 46 f7                    ; 0xc11f3
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc11f6
    or si, word [bp-00ah]                     ; 0b 76 f6                    ; 0xc11f9
    cmp si, cx                                ; 39 ce                       ; 0xc11fc
    jnc short 0121dh                          ; 73 1d                       ; 0xc11fe
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc1200
    mov byte [bp-009h], al                    ; 88 46 f7                    ; 0xc1203
    mov si, cx                                ; 89 ce                       ; 0xc1206
    dec si                                    ; 4e                          ; 0xc1208
    cmp si, word [bp-00ah]                    ; 3b 76 f6                    ; 0xc1209
    je short 01257h                           ; 74 49                       ; 0xc120c
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc120e
    mov byte [bp-007h], al                    ; 88 46 f9                    ; 0xc1211
    dec cx                                    ; 49                          ; 0xc1214
    dec cx                                    ; 49                          ; 0xc1215
    cmp cx, word [bp-008h]                    ; 3b 4e f8                    ; 0xc1216
    jne short 0121dh                          ; 75 02                       ; 0xc1219
    jmp short 01257h                          ; eb 3a                       ; 0xc121b
    cmp ah, 003h                              ; 80 fc 03                    ; 0xc121d vgabios.c:758
    jbe short 01257h                          ; 76 35                       ; 0xc1220
    mov cl, bl                                ; 88 d9                       ; 0xc1222 vgabios.c:759
    xor ch, ch                                ; 30 ed                       ; 0xc1224
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc1226
    mov byte [bp-009h], ch                    ; 88 6e f7                    ; 0xc1229
    mov si, cx                                ; 89 ce                       ; 0xc122c
    inc si                                    ; 46                          ; 0xc122e
    inc si                                    ; 46                          ; 0xc122f
    mov cl, dl                                ; 88 d1                       ; 0xc1230
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc1232
    cmp si, word [bp-00ah]                    ; 3b 76 f6                    ; 0xc1234
    jl short 0124ch                           ; 7c 13                       ; 0xc1237
    sub bl, ah                                ; 28 e3                       ; 0xc1239 vgabios.c:761
    add bl, dl                                ; 00 d3                       ; 0xc123b
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc123d
    mov ah, cl                                ; 88 cc                       ; 0xc123f vgabios.c:762
    cmp dx, strict byte 0000eh                ; 83 fa 0e                    ; 0xc1241 vgabios.c:763
    jc short 01257h                           ; 72 11                       ; 0xc1244
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc1246 vgabios.c:765
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc1248 vgabios.c:766
    jmp short 01257h                          ; eb 0b                       ; 0xc124a vgabios.c:768
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc124c
    jbe short 01255h                          ; 76 04                       ; 0xc124f
    shr dx, 1                                 ; d1 ea                       ; 0xc1251 vgabios.c:770
    mov bl, dl                                ; 88 d3                       ; 0xc1253
    mov ah, cl                                ; 88 cc                       ; 0xc1255 vgabios.c:774
    mov si, strict word 00063h                ; be 63 00                    ; 0xc1257 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc125a
    mov es, dx                                ; 8e c2                       ; 0xc125d
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc125f
    mov AL, strict byte 00ah                  ; b0 0a                       ; 0xc1262 vgabios.c:785
    mov dx, cx                                ; 89 ca                       ; 0xc1264
    out DX, AL                                ; ee                          ; 0xc1266
    mov si, cx                                ; 89 ce                       ; 0xc1267 vgabios.c:786
    inc si                                    ; 46                          ; 0xc1269
    mov al, bl                                ; 88 d8                       ; 0xc126a
    mov dx, si                                ; 89 f2                       ; 0xc126c
    out DX, AL                                ; ee                          ; 0xc126e
    mov AL, strict byte 00bh                  ; b0 0b                       ; 0xc126f vgabios.c:787
    mov dx, cx                                ; 89 ca                       ; 0xc1271
    out DX, AL                                ; ee                          ; 0xc1273
    mov al, ah                                ; 88 e0                       ; 0xc1274 vgabios.c:788
    mov dx, si                                ; 89 f2                       ; 0xc1276
    out DX, AL                                ; ee                          ; 0xc1278
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc1279 vgabios.c:789
    pop si                                    ; 5e                          ; 0xc127c
    pop cx                                    ; 59                          ; 0xc127d
    pop bx                                    ; 5b                          ; 0xc127e
    pop bp                                    ; 5d                          ; 0xc127f
    retn                                      ; c3                          ; 0xc1280
  ; disGetNextSymbol 0xc1281 LB 0x32c2 -> off=0x0 cb=000000000000008f uValue=00000000000c1281 'biosfn_set_cursor_pos'
biosfn_set_cursor_pos:                       ; 0xc1281 LB 0x8f
    push bp                                   ; 55                          ; 0xc1281 vgabios.c:792
    mov bp, sp                                ; 89 e5                       ; 0xc1282
    push bx                                   ; 53                          ; 0xc1284
    push cx                                   ; 51                          ; 0xc1285
    push si                                   ; 56                          ; 0xc1286
    push di                                   ; 57                          ; 0xc1287
    push ax                                   ; 50                          ; 0xc1288
    mov bl, al                                ; 88 c3                       ; 0xc1289
    mov cx, dx                                ; 89 d1                       ; 0xc128b
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc128d vgabios.c:798
    jnbe short 01307h                         ; 77 76                       ; 0xc128f
    xor ah, ah                                ; 30 e4                       ; 0xc1291 vgabios.c:801
    mov si, ax                                ; 89 c6                       ; 0xc1293
    add si, ax                                ; 01 c6                       ; 0xc1295
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc1297
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc129a vgabios.c:62
    mov es, ax                                ; 8e c0                       ; 0xc129d
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc129f
    mov si, strict word 00062h                ; be 62 00                    ; 0xc12a2 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc12a5
    cmp bl, al                                ; 38 c3                       ; 0xc12a8 vgabios.c:805
    jne short 01307h                          ; 75 5b                       ; 0xc12aa
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc12ac vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc12af
    mov si, 00084h                            ; be 84 00                    ; 0xc12b2 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc12b5
    xor ah, ah                                ; 30 e4                       ; 0xc12b8 vgabios.c:48
    mov si, ax                                ; 89 c6                       ; 0xc12ba
    inc si                                    ; 46                          ; 0xc12bc
    mov ax, dx                                ; 89 d0                       ; 0xc12bd vgabios.c:811
    xor al, dl                                ; 30 d0                       ; 0xc12bf
    shr ax, 008h                              ; c1 e8 08                    ; 0xc12c1
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc12c4
    mov ax, di                                ; 89 f8                       ; 0xc12c7 vgabios.c:814
    mul si                                    ; f7 e6                       ; 0xc12c9
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc12cb
    xor bh, bh                                ; 30 ff                       ; 0xc12cd
    inc ax                                    ; 40                          ; 0xc12cf
    mul bx                                    ; f7 e3                       ; 0xc12d0
    mov bl, cl                                ; 88 cb                       ; 0xc12d2
    mov si, bx                                ; 89 de                       ; 0xc12d4
    add si, ax                                ; 01 c6                       ; 0xc12d6
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc12d8
    xor ah, ah                                ; 30 e4                       ; 0xc12db
    mul di                                    ; f7 e7                       ; 0xc12dd
    add si, ax                                ; 01 c6                       ; 0xc12df
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc12e1 vgabios.c:57
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc12e4
    mov AL, strict byte 00eh                  ; b0 0e                       ; 0xc12e7 vgabios.c:818
    mov dx, bx                                ; 89 da                       ; 0xc12e9
    out DX, AL                                ; ee                          ; 0xc12eb
    mov ax, si                                ; 89 f0                       ; 0xc12ec vgabios.c:819
    xor al, al                                ; 30 c0                       ; 0xc12ee
    shr ax, 008h                              ; c1 e8 08                    ; 0xc12f0
    lea cx, [bx+001h]                         ; 8d 4f 01                    ; 0xc12f3
    mov dx, cx                                ; 89 ca                       ; 0xc12f6
    out DX, AL                                ; ee                          ; 0xc12f8
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc12f9 vgabios.c:820
    mov dx, bx                                ; 89 da                       ; 0xc12fb
    out DX, AL                                ; ee                          ; 0xc12fd
    and si, 000ffh                            ; 81 e6 ff 00                 ; 0xc12fe vgabios.c:821
    mov ax, si                                ; 89 f0                       ; 0xc1302
    mov dx, cx                                ; 89 ca                       ; 0xc1304
    out DX, AL                                ; ee                          ; 0xc1306
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1307 vgabios.c:823
    pop di                                    ; 5f                          ; 0xc130a
    pop si                                    ; 5e                          ; 0xc130b
    pop cx                                    ; 59                          ; 0xc130c
    pop bx                                    ; 5b                          ; 0xc130d
    pop bp                                    ; 5d                          ; 0xc130e
    retn                                      ; c3                          ; 0xc130f
  ; disGetNextSymbol 0xc1310 LB 0x3233 -> off=0x0 cb=00000000000000d8 uValue=00000000000c1310 'biosfn_set_active_page'
biosfn_set_active_page:                      ; 0xc1310 LB 0xd8
    push bp                                   ; 55                          ; 0xc1310 vgabios.c:826
    mov bp, sp                                ; 89 e5                       ; 0xc1311
    push bx                                   ; 53                          ; 0xc1313
    push cx                                   ; 51                          ; 0xc1314
    push dx                                   ; 52                          ; 0xc1315
    push si                                   ; 56                          ; 0xc1316
    push di                                   ; 57                          ; 0xc1317
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc1318
    mov cl, al                                ; 88 c1                       ; 0xc131b
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc131d vgabios.c:832
    jnbe short 01337h                         ; 77 16                       ; 0xc131f
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1321 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1324
    mov es, ax                                ; 8e c0                       ; 0xc1327
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1329
    xor ah, ah                                ; 30 e4                       ; 0xc132c vgabios.c:836
    call 0382ah                               ; e8 f9 24                    ; 0xc132e
    mov ch, al                                ; 88 c5                       ; 0xc1331
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1333 vgabios.c:837
    jne short 0133ah                          ; 75 03                       ; 0xc1335
    jmp near 013deh                           ; e9 a4 00                    ; 0xc1337
    mov al, cl                                ; 88 c8                       ; 0xc133a vgabios.c:840
    xor ah, ah                                ; 30 e4                       ; 0xc133c
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc133e
    lea dx, [bp-010h]                         ; 8d 56 f0                    ; 0xc1341
    call 00a96h                               ; e8 4f f7                    ; 0xc1344
    mov bl, ch                                ; 88 eb                       ; 0xc1347 vgabios.c:842
    xor bh, bh                                ; 30 ff                       ; 0xc1349
    mov si, bx                                ; 89 de                       ; 0xc134b
    sal si, 003h                              ; c1 e6 03                    ; 0xc134d
    cmp byte [si+047adh], 000h                ; 80 bc ad 47 00              ; 0xc1350
    jne short 01396h                          ; 75 3f                       ; 0xc1355
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1357 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc135a
    mov es, ax                                ; 8e c0                       ; 0xc135d
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc135f
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1362 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1365
    xor ah, ah                                ; 30 e4                       ; 0xc1368 vgabios.c:48
    mov bx, ax                                ; 89 c3                       ; 0xc136a
    inc bx                                    ; 43                          ; 0xc136c
    mov ax, dx                                ; 89 d0                       ; 0xc136d vgabios.c:849
    mul bx                                    ; f7 e3                       ; 0xc136f
    mov di, ax                                ; 89 c7                       ; 0xc1371
    add ax, ax                                ; 01 c0                       ; 0xc1373
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc1375
    mov byte [bp-00ch], cl                    ; 88 4e f4                    ; 0xc1377
    mov byte [bp-00bh], 000h                  ; c6 46 f5 00                 ; 0xc137a
    inc ax                                    ; 40                          ; 0xc137e
    mul word [bp-00ch]                        ; f7 66 f4                    ; 0xc137f
    mov bx, ax                                ; 89 c3                       ; 0xc1382
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc1384 vgabios.c:62
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc1387
    or di, 000ffh                             ; 81 cf ff 00                 ; 0xc138a vgabios.c:853
    lea ax, [di+001h]                         ; 8d 45 01                    ; 0xc138e
    mul word [bp-00ch]                        ; f7 66 f4                    ; 0xc1391
    jmp short 013a5h                          ; eb 0f                       ; 0xc1394 vgabios.c:855
    mov bl, byte [bx+0482ch]                  ; 8a 9f 2c 48                 ; 0xc1396 vgabios.c:857
    sal bx, 006h                              ; c1 e3 06                    ; 0xc139a
    mov al, cl                                ; 88 c8                       ; 0xc139d
    xor ah, ah                                ; 30 e4                       ; 0xc139f
    mul word [bx+04843h]                      ; f7 a7 43 48                 ; 0xc13a1
    mov bx, ax                                ; 89 c3                       ; 0xc13a5
    mov si, strict word 00063h                ; be 63 00                    ; 0xc13a7 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc13aa
    mov es, ax                                ; 8e c0                       ; 0xc13ad
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc13af
    mov AL, strict byte 00ch                  ; b0 0c                       ; 0xc13b2 vgabios.c:862
    mov dx, si                                ; 89 f2                       ; 0xc13b4
    out DX, AL                                ; ee                          ; 0xc13b6
    mov ax, bx                                ; 89 d8                       ; 0xc13b7 vgabios.c:863
    xor al, bl                                ; 30 d8                       ; 0xc13b9
    shr ax, 008h                              ; c1 e8 08                    ; 0xc13bb
    lea di, [si+001h]                         ; 8d 7c 01                    ; 0xc13be
    mov dx, di                                ; 89 fa                       ; 0xc13c1
    out DX, AL                                ; ee                          ; 0xc13c3
    mov AL, strict byte 00dh                  ; b0 0d                       ; 0xc13c4 vgabios.c:864
    mov dx, si                                ; 89 f2                       ; 0xc13c6
    out DX, AL                                ; ee                          ; 0xc13c8
    xor bh, bh                                ; 30 ff                       ; 0xc13c9 vgabios.c:865
    mov ax, bx                                ; 89 d8                       ; 0xc13cb
    mov dx, di                                ; 89 fa                       ; 0xc13cd
    out DX, AL                                ; ee                          ; 0xc13cf
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc13d0 vgabios.c:52
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc13d3
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc13d6 vgabios.c:875
    mov al, cl                                ; 88 c8                       ; 0xc13d9
    call 01281h                               ; e8 a3 fe                    ; 0xc13db
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc13de vgabios.c:876
    pop di                                    ; 5f                          ; 0xc13e1
    pop si                                    ; 5e                          ; 0xc13e2
    pop dx                                    ; 5a                          ; 0xc13e3
    pop cx                                    ; 59                          ; 0xc13e4
    pop bx                                    ; 5b                          ; 0xc13e5
    pop bp                                    ; 5d                          ; 0xc13e6
    retn                                      ; c3                          ; 0xc13e7
  ; disGetNextSymbol 0xc13e8 LB 0x315b -> off=0x0 cb=0000000000000045 uValue=00000000000c13e8 'find_vpti'
find_vpti:                                   ; 0xc13e8 LB 0x45
    push bx                                   ; 53                          ; 0xc13e8 vgabios.c:911
    push si                                   ; 56                          ; 0xc13e9
    push bp                                   ; 55                          ; 0xc13ea
    mov bp, sp                                ; 89 e5                       ; 0xc13eb
    mov bl, al                                ; 88 c3                       ; 0xc13ed vgabios.c:916
    xor bh, bh                                ; 30 ff                       ; 0xc13ef
    mov si, bx                                ; 89 de                       ; 0xc13f1
    sal si, 003h                              ; c1 e6 03                    ; 0xc13f3
    cmp byte [si+047adh], 000h                ; 80 bc ad 47 00              ; 0xc13f6
    jne short 01423h                          ; 75 26                       ; 0xc13fb
    mov si, 00089h                            ; be 89 00                    ; 0xc13fd vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1400
    mov es, ax                                ; 8e c0                       ; 0xc1403
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc1405
    test AL, strict byte 010h                 ; a8 10                       ; 0xc1408 vgabios.c:918
    je short 01412h                           ; 74 06                       ; 0xc140a
    mov al, byte [bx+07df3h]                  ; 8a 87 f3 7d                 ; 0xc140c vgabios.c:919
    jmp short 01420h                          ; eb 0e                       ; 0xc1410 vgabios.c:920
    test AL, strict byte 080h                 ; a8 80                       ; 0xc1412
    je short 0141ch                           ; 74 06                       ; 0xc1414
    mov al, byte [bx+07de3h]                  ; 8a 87 e3 7d                 ; 0xc1416 vgabios.c:921
    jmp short 01420h                          ; eb 04                       ; 0xc141a vgabios.c:922
    mov al, byte [bx+07debh]                  ; 8a 87 eb 7d                 ; 0xc141c vgabios.c:923
    cbw                                       ; 98                          ; 0xc1420
    jmp short 01429h                          ; eb 06                       ; 0xc1421 vgabios.c:924
    mov al, byte [bx+0482ch]                  ; 8a 87 2c 48                 ; 0xc1423 vgabios.c:925
    xor ah, ah                                ; 30 e4                       ; 0xc1427
    pop bp                                    ; 5d                          ; 0xc1429 vgabios.c:928
    pop si                                    ; 5e                          ; 0xc142a
    pop bx                                    ; 5b                          ; 0xc142b
    retn                                      ; c3                          ; 0xc142c
  ; disGetNextSymbol 0xc142d LB 0x3116 -> off=0x0 cb=00000000000004da uValue=00000000000c142d 'biosfn_set_video_mode'
biosfn_set_video_mode:                       ; 0xc142d LB 0x4da
    push bp                                   ; 55                          ; 0xc142d vgabios.c:933
    mov bp, sp                                ; 89 e5                       ; 0xc142e
    push bx                                   ; 53                          ; 0xc1430
    push cx                                   ; 51                          ; 0xc1431
    push dx                                   ; 52                          ; 0xc1432
    push si                                   ; 56                          ; 0xc1433
    push di                                   ; 57                          ; 0xc1434
    sub sp, strict byte 00018h                ; 83 ec 18                    ; 0xc1435
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc1438
    and AL, strict byte 080h                  ; 24 80                       ; 0xc143b vgabios.c:937
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc143d
    call 007bfh                               ; e8 7c f3                    ; 0xc1440 vgabios.c:947
    test ax, ax                               ; 85 c0                       ; 0xc1443
    je short 01453h                           ; 74 0c                       ; 0xc1445
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc1447 vgabios.c:949
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc1449
    out DX, AL                                ; ee                          ; 0xc144c
    xor al, al                                ; 30 c0                       ; 0xc144d vgabios.c:950
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc144f
    out DX, AL                                ; ee                          ; 0xc1452
    and byte [bp-00ch], 07fh                  ; 80 66 f4 7f                 ; 0xc1453 vgabios.c:955
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1457 vgabios.c:961
    xor ah, ah                                ; 30 e4                       ; 0xc145a
    call 0382ah                               ; e8 cb 23                    ; 0xc145c
    mov cl, al                                ; 88 c1                       ; 0xc145f
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc1461
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1464 vgabios.c:967
    je short 014d3h                           ; 74 6b                       ; 0xc1466
    mov bx, 000a8h                            ; bb a8 00                    ; 0xc1468 vgabios.c:67
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc146b
    mov es, ax                                ; 8e c0                       ; 0xc146e
    mov di, word [es:bx]                      ; 26 8b 3f                    ; 0xc1470
    mov ax, word [es:bx+002h]                 ; 26 8b 47 02                 ; 0xc1473
    mov bx, di                                ; 89 fb                       ; 0xc1477 vgabios.c:68
    mov word [bp-01eh], ax                    ; 89 46 e2                    ; 0xc1479
    xor ch, ch                                ; 30 ed                       ; 0xc147c vgabios.c:973
    mov ax, cx                                ; 89 c8                       ; 0xc147e
    call 013e8h                               ; e8 65 ff                    ; 0xc1480
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1483 vgabios.c:974
    mov si, word [es:di]                      ; 26 8b 35                    ; 0xc1486
    mov dx, word [es:di+002h]                 ; 26 8b 55 02                 ; 0xc1489
    mov word [bp-016h], dx                    ; 89 56 ea                    ; 0xc148d
    xor ah, ah                                ; 30 e4                       ; 0xc1490 vgabios.c:975
    sal ax, 006h                              ; c1 e0 06                    ; 0xc1492
    add si, ax                                ; 01 c6                       ; 0xc1495
    mov di, 00089h                            ; bf 89 00                    ; 0xc1497 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc149a
    mov es, ax                                ; 8e c0                       ; 0xc149d
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc149f
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc14a2 vgabios.c:48
    test AL, strict byte 008h                 ; a8 08                       ; 0xc14a5 vgabios.c:992
    jne short 014efh                          ; 75 46                       ; 0xc14a7
    mov di, cx                                ; 89 cf                       ; 0xc14a9 vgabios.c:994
    sal di, 003h                              ; c1 e7 03                    ; 0xc14ab
    mov al, byte [di+047b2h]                  ; 8a 85 b2 47                 ; 0xc14ae
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc14b2
    out DX, AL                                ; ee                          ; 0xc14b5
    xor al, al                                ; 30 c0                       ; 0xc14b6 vgabios.c:997
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc14b8
    out DX, AL                                ; ee                          ; 0xc14bb
    mov cl, byte [di+047b3h]                  ; 8a 8d b3 47                 ; 0xc14bc vgabios.c:1000
    cmp cl, 001h                              ; 80 f9 01                    ; 0xc14c0
    jc short 014d6h                           ; 72 11                       ; 0xc14c3
    jbe short 014e1h                          ; 76 1a                       ; 0xc14c5
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc14c7
    je short 014f2h                           ; 74 26                       ; 0xc14ca
    cmp cl, 002h                              ; 80 f9 02                    ; 0xc14cc
    je short 014e8h                           ; 74 17                       ; 0xc14cf
    jmp short 014f7h                          ; eb 24                       ; 0xc14d1
    jmp near 018fdh                           ; e9 27 04                    ; 0xc14d3
    test cl, cl                               ; 84 c9                       ; 0xc14d6
    jne short 014f7h                          ; 75 1d                       ; 0xc14d8
    mov word [bp-01ah], 04fc0h                ; c7 46 e6 c0 4f              ; 0xc14da vgabios.c:1002
    jmp short 014f7h                          ; eb 16                       ; 0xc14df vgabios.c:1003
    mov word [bp-01ah], 05080h                ; c7 46 e6 80 50              ; 0xc14e1 vgabios.c:1005
    jmp short 014f7h                          ; eb 0f                       ; 0xc14e6 vgabios.c:1006
    mov word [bp-01ah], 05140h                ; c7 46 e6 40 51              ; 0xc14e8 vgabios.c:1008
    jmp short 014f7h                          ; eb 08                       ; 0xc14ed vgabios.c:1009
    jmp near 01566h                           ; e9 74 00                    ; 0xc14ef
    mov word [bp-01ah], 05200h                ; c7 46 e6 00 52              ; 0xc14f2 vgabios.c:1011
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc14f7 vgabios.c:1015
    xor ah, ah                                ; 30 e4                       ; 0xc14fa
    mov di, ax                                ; 89 c7                       ; 0xc14fc
    sal di, 003h                              ; c1 e7 03                    ; 0xc14fe
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc1501
    jne short 01517h                          ; 75 0f                       ; 0xc1506
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc1508 vgabios.c:1017
    cmp byte [es:si+002h], 008h               ; 26 80 7c 02 08              ; 0xc150b
    jne short 01517h                          ; 75 05                       ; 0xc1510
    mov word [bp-01ah], 05080h                ; c7 46 e6 80 50              ; 0xc1512 vgabios.c:1018
    xor cx, cx                                ; 31 c9                       ; 0xc1517 vgabios.c:1021
    jmp short 0152ah                          ; eb 0f                       ; 0xc1519
    xor al, al                                ; 30 c0                       ; 0xc151b vgabios.c:1028
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc151d
    out DX, AL                                ; ee                          ; 0xc1520
    out DX, AL                                ; ee                          ; 0xc1521 vgabios.c:1029
    out DX, AL                                ; ee                          ; 0xc1522 vgabios.c:1030
    inc cx                                    ; 41                          ; 0xc1523 vgabios.c:1032
    cmp cx, 00100h                            ; 81 f9 00 01                 ; 0xc1524
    jnc short 01558h                          ; 73 2e                       ; 0xc1528
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc152a
    xor ah, ah                                ; 30 e4                       ; 0xc152d
    mov di, ax                                ; 89 c7                       ; 0xc152f
    sal di, 003h                              ; c1 e7 03                    ; 0xc1531
    mov al, byte [di+047b3h]                  ; 8a 85 b3 47                 ; 0xc1534
    mov di, ax                                ; 89 c7                       ; 0xc1538
    mov al, byte [di+0483ch]                  ; 8a 85 3c 48                 ; 0xc153a
    cmp cx, ax                                ; 39 c1                       ; 0xc153e
    jnbe short 0151bh                         ; 77 d9                       ; 0xc1540
    imul di, cx, strict byte 00003h           ; 6b f9 03                    ; 0xc1542
    add di, word [bp-01ah]                    ; 03 7e e6                    ; 0xc1545
    mov al, byte [di]                         ; 8a 05                       ; 0xc1548
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc154a
    out DX, AL                                ; ee                          ; 0xc154d
    mov al, byte [di+001h]                    ; 8a 45 01                    ; 0xc154e
    out DX, AL                                ; ee                          ; 0xc1551
    mov al, byte [di+002h]                    ; 8a 45 02                    ; 0xc1552
    out DX, AL                                ; ee                          ; 0xc1555
    jmp short 01523h                          ; eb cb                       ; 0xc1556
    test byte [bp-010h], 002h                 ; f6 46 f0 02                 ; 0xc1558 vgabios.c:1033
    je short 01566h                           ; 74 08                       ; 0xc155c
    mov dx, 00100h                            ; ba 00 01                    ; 0xc155e vgabios.c:1035
    xor ax, ax                                ; 31 c0                       ; 0xc1561
    call 010edh                               ; e8 87 fb                    ; 0xc1563
    mov dx, 003dah                            ; ba da 03                    ; 0xc1566 vgabios.c:1040
    in AL, DX                                 ; ec                          ; 0xc1569
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc156a
    xor cx, cx                                ; 31 c9                       ; 0xc156c vgabios.c:1043
    jmp short 01575h                          ; eb 05                       ; 0xc156e
    cmp cx, strict byte 00013h                ; 83 f9 13                    ; 0xc1570
    jnbe short 0158ah                         ; 77 15                       ; 0xc1573
    mov al, cl                                ; 88 c8                       ; 0xc1575 vgabios.c:1044
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1577
    out DX, AL                                ; ee                          ; 0xc157a
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc157b vgabios.c:1045
    mov di, si                                ; 89 f7                       ; 0xc157e
    add di, cx                                ; 01 cf                       ; 0xc1580
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc1582
    out DX, AL                                ; ee                          ; 0xc1586
    inc cx                                    ; 41                          ; 0xc1587 vgabios.c:1046
    jmp short 01570h                          ; eb e6                       ; 0xc1588
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc158a vgabios.c:1047
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc158c
    out DX, AL                                ; ee                          ; 0xc158f
    xor al, al                                ; 30 c0                       ; 0xc1590 vgabios.c:1048
    out DX, AL                                ; ee                          ; 0xc1592
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1593 vgabios.c:1051
    mov dx, word [es:bx+004h]                 ; 26 8b 57 04                 ; 0xc1596
    mov ax, word [es:bx+006h]                 ; 26 8b 47 06                 ; 0xc159a
    test ax, ax                               ; 85 c0                       ; 0xc159e
    jne short 015a6h                          ; 75 04                       ; 0xc15a0
    test dx, dx                               ; 85 d2                       ; 0xc15a2
    je short 015e3h                           ; 74 3d                       ; 0xc15a4
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc15a6 vgabios.c:1055
    xor cx, cx                                ; 31 c9                       ; 0xc15a9 vgabios.c:1056
    jmp short 015b2h                          ; eb 05                       ; 0xc15ab
    cmp cx, strict byte 00010h                ; 83 f9 10                    ; 0xc15ad
    jnc short 015d3h                          ; 73 21                       ; 0xc15b0
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc15b2 vgabios.c:1057
    mov di, si                                ; 89 f7                       ; 0xc15b5
    add di, cx                                ; 01 cf                       ; 0xc15b7
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc15b9
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc15bc
    mov ax, dx                                ; 89 d0                       ; 0xc15bf
    add ax, cx                                ; 01 c8                       ; 0xc15c1
    mov word [bp-022h], ax                    ; 89 46 de                    ; 0xc15c3
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc15c6
    les di, [bp-022h]                         ; c4 7e de                    ; 0xc15ca
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc15cd
    inc cx                                    ; 41                          ; 0xc15d0
    jmp short 015adh                          ; eb da                       ; 0xc15d1
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc15d3 vgabios.c:1058
    mov al, byte [es:si+034h]                 ; 26 8a 44 34                 ; 0xc15d6
    mov es, [bp-01ch]                         ; 8e 46 e4                    ; 0xc15da
    mov di, dx                                ; 89 d7                       ; 0xc15dd
    mov byte [es:di+010h], al                 ; 26 88 45 10                 ; 0xc15df
    xor al, al                                ; 30 c0                       ; 0xc15e3 vgabios.c:1062
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc15e5
    out DX, AL                                ; ee                          ; 0xc15e8
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc15e9 vgabios.c:1063
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc15eb
    out DX, AL                                ; ee                          ; 0xc15ee
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc15ef vgabios.c:1064
    jmp short 015f9h                          ; eb 05                       ; 0xc15f2
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc15f4
    jnbe short 01611h                         ; 77 18                       ; 0xc15f7
    mov al, cl                                ; 88 c8                       ; 0xc15f9 vgabios.c:1065
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc15fb
    out DX, AL                                ; ee                          ; 0xc15fe
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc15ff vgabios.c:1066
    mov di, si                                ; 89 f7                       ; 0xc1602
    add di, cx                                ; 01 cf                       ; 0xc1604
    mov al, byte [es:di+004h]                 ; 26 8a 45 04                 ; 0xc1606
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc160a
    out DX, AL                                ; ee                          ; 0xc160d
    inc cx                                    ; 41                          ; 0xc160e vgabios.c:1067
    jmp short 015f4h                          ; eb e3                       ; 0xc160f
    xor cx, cx                                ; 31 c9                       ; 0xc1611 vgabios.c:1070
    jmp short 0161ah                          ; eb 05                       ; 0xc1613
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc1615
    jnbe short 01632h                         ; 77 18                       ; 0xc1618
    mov al, cl                                ; 88 c8                       ; 0xc161a vgabios.c:1071
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc161c
    out DX, AL                                ; ee                          ; 0xc161f
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc1620 vgabios.c:1072
    mov di, si                                ; 89 f7                       ; 0xc1623
    add di, cx                                ; 01 cf                       ; 0xc1625
    mov al, byte [es:di+037h]                 ; 26 8a 45 37                 ; 0xc1627
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc162b
    out DX, AL                                ; ee                          ; 0xc162e
    inc cx                                    ; 41                          ; 0xc162f vgabios.c:1073
    jmp short 01615h                          ; eb e3                       ; 0xc1630
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1632 vgabios.c:1076
    xor ah, ah                                ; 30 e4                       ; 0xc1635
    mov di, ax                                ; 89 c7                       ; 0xc1637
    sal di, 003h                              ; c1 e7 03                    ; 0xc1639
    cmp byte [di+047aeh], 001h                ; 80 bd ae 47 01              ; 0xc163c
    jne short 01648h                          ; 75 05                       ; 0xc1641
    mov cx, 003b4h                            ; b9 b4 03                    ; 0xc1643
    jmp short 0164bh                          ; eb 03                       ; 0xc1646
    mov cx, 003d4h                            ; b9 d4 03                    ; 0xc1648
    mov word [bp-014h], cx                    ; 89 4e ec                    ; 0xc164b
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc164e vgabios.c:1079
    mov al, byte [es:si+009h]                 ; 26 8a 44 09                 ; 0xc1651
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc1655
    out DX, AL                                ; ee                          ; 0xc1658
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc1659 vgabios.c:1082
    mov dx, cx                                ; 89 ca                       ; 0xc165c
    out DX, ax                                ; ef                          ; 0xc165e
    xor cx, cx                                ; 31 c9                       ; 0xc165f vgabios.c:1084
    jmp short 01668h                          ; eb 05                       ; 0xc1661
    cmp cx, strict byte 00018h                ; 83 f9 18                    ; 0xc1663
    jnbe short 0167eh                         ; 77 16                       ; 0xc1666
    mov al, cl                                ; 88 c8                       ; 0xc1668 vgabios.c:1085
    mov dx, word [bp-014h]                    ; 8b 56 ec                    ; 0xc166a
    out DX, AL                                ; ee                          ; 0xc166d
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc166e vgabios.c:1086
    mov di, si                                ; 89 f7                       ; 0xc1671
    add di, cx                                ; 01 cf                       ; 0xc1673
    inc dx                                    ; 42                          ; 0xc1675
    mov al, byte [es:di+00ah]                 ; 26 8a 45 0a                 ; 0xc1676
    out DX, AL                                ; ee                          ; 0xc167a
    inc cx                                    ; 41                          ; 0xc167b vgabios.c:1087
    jmp short 01663h                          ; eb e5                       ; 0xc167c
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc167e vgabios.c:1090
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1680
    out DX, AL                                ; ee                          ; 0xc1683
    mov dx, word [bp-014h]                    ; 8b 56 ec                    ; 0xc1684 vgabios.c:1091
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc1687
    in AL, DX                                 ; ec                          ; 0xc168a
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc168b
    cmp byte [bp-00eh], 000h                  ; 80 7e f2 00                 ; 0xc168d vgabios.c:1093
    jne short 016f2h                          ; 75 5f                       ; 0xc1691
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1693 vgabios.c:1095
    xor ah, ah                                ; 30 e4                       ; 0xc1696
    mov di, ax                                ; 89 c7                       ; 0xc1698
    sal di, 003h                              ; c1 e7 03                    ; 0xc169a
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc169d
    jne short 016b6h                          ; 75 12                       ; 0xc16a2
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc16a4 vgabios.c:1097
    mov cx, 04000h                            ; b9 00 40                    ; 0xc16a8
    mov ax, 00720h                            ; b8 20 07                    ; 0xc16ab
    xor di, di                                ; 31 ff                       ; 0xc16ae
    jcxz 016b4h                               ; e3 02                       ; 0xc16b0
    rep stosw                                 ; f3 ab                       ; 0xc16b2
    jmp short 016f2h                          ; eb 3c                       ; 0xc16b4 vgabios.c:1099
    cmp byte [bp-00ch], 00dh                  ; 80 7e f4 0d                 ; 0xc16b6 vgabios.c:1101
    jnc short 016cdh                          ; 73 11                       ; 0xc16ba
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc16bc vgabios.c:1103
    mov cx, 04000h                            ; b9 00 40                    ; 0xc16c0
    xor al, al                                ; 30 c0                       ; 0xc16c3
    xor di, di                                ; 31 ff                       ; 0xc16c5
    jcxz 016cbh                               ; e3 02                       ; 0xc16c7
    rep stosw                                 ; f3 ab                       ; 0xc16c9
    jmp short 016f2h                          ; eb 25                       ; 0xc16cb vgabios.c:1105
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc16cd vgabios.c:1107
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc16cf
    out DX, AL                                ; ee                          ; 0xc16d2
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc16d3 vgabios.c:1108
    in AL, DX                                 ; ec                          ; 0xc16d6
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc16d7
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc16d9
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc16dc vgabios.c:1109
    out DX, AL                                ; ee                          ; 0xc16de
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc16df vgabios.c:1110
    mov cx, 08000h                            ; b9 00 80                    ; 0xc16e3
    xor ax, ax                                ; 31 c0                       ; 0xc16e6
    xor di, di                                ; 31 ff                       ; 0xc16e8
    jcxz 016eeh                               ; e3 02                       ; 0xc16ea
    rep stosw                                 ; f3 ab                       ; 0xc16ec
    mov al, byte [bp-020h]                    ; 8a 46 e0                    ; 0xc16ee vgabios.c:1111
    out DX, AL                                ; ee                          ; 0xc16f1
    mov di, strict word 00049h                ; bf 49 00                    ; 0xc16f2 vgabios.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc16f5
    mov es, ax                                ; 8e c0                       ; 0xc16f8
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc16fa
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc16fd
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc1700 vgabios.c:1118
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc1703
    xor ah, ah                                ; 30 e4                       ; 0xc1706
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc1708 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc170b
    mov es, dx                                ; 8e c2                       ; 0xc170e
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc1710
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc1713 vgabios.c:60
    mov ax, word [es:si+003h]                 ; 26 8b 44 03                 ; 0xc1716
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc171a vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc171d
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc171f
    mov di, strict word 00063h                ; bf 63 00                    ; 0xc1722 vgabios.c:62
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc1725
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc1728
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc172b vgabios.c:50
    mov al, byte [es:si+001h]                 ; 26 8a 44 01                 ; 0xc172e
    mov di, 00084h                            ; bf 84 00                    ; 0xc1732 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc1735
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1737
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc173a vgabios.c:1122
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc173d
    xor ah, ah                                ; 30 e4                       ; 0xc1741
    mov di, 00085h                            ; bf 85 00                    ; 0xc1743 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc1746
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc1748
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc174b vgabios.c:1123
    mov al, byte [es:si+014h]                 ; 26 8a 44 14                 ; 0xc174e
    mov dx, ax                                ; 89 c2                       ; 0xc1752
    sal dx, 008h                              ; c1 e2 08                    ; 0xc1754
    mov al, byte [es:si+015h]                 ; 26 8a 44 15                 ; 0xc1757
    or ax, dx                                 ; 09 d0                       ; 0xc175b
    mov di, strict word 00060h                ; bf 60 00                    ; 0xc175d vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc1760
    mov es, dx                                ; 8e c2                       ; 0xc1763
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc1765
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc1768 vgabios.c:1124
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc176b
    mov di, 00087h                            ; bf 87 00                    ; 0xc176d vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1770
    mov di, 00088h                            ; bf 88 00                    ; 0xc1773 vgabios.c:52
    mov byte [es:di], 0f9h                    ; 26 c6 05 f9                 ; 0xc1776
    mov di, 0008ah                            ; bf 8a 00                    ; 0xc177a vgabios.c:52
    mov byte [es:di], 008h                    ; 26 c6 05 08                 ; 0xc177d
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1781 vgabios.c:1130
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc1784
    jnbe short 017afh                         ; 77 27                       ; 0xc1786
    xor ah, ah                                ; 30 e4                       ; 0xc1788 vgabios.c:1132
    mov di, ax                                ; 89 c7                       ; 0xc178a vgabios.c:50
    mov al, byte [di+07ddbh]                  ; 8a 85 db 7d                 ; 0xc178c
    mov di, strict word 00065h                ; bf 65 00                    ; 0xc1790 vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1793
    cmp byte [bp-00ch], 006h                  ; 80 7e f4 06                 ; 0xc1796 vgabios.c:1133
    jne short 017a1h                          ; 75 05                       ; 0xc179a
    mov ax, strict word 0003fh                ; b8 3f 00                    ; 0xc179c
    jmp short 017a4h                          ; eb 03                       ; 0xc179f
    mov ax, strict word 00030h                ; b8 30 00                    ; 0xc17a1
    mov di, strict word 00066h                ; bf 66 00                    ; 0xc17a4 vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc17a7
    mov es, dx                                ; 8e c2                       ; 0xc17aa
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc17ac
    xor cx, cx                                ; 31 c9                       ; 0xc17af vgabios.c:1138
    jmp short 017b8h                          ; eb 05                       ; 0xc17b1
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc17b3
    jnc short 017c4h                          ; 73 0c                       ; 0xc17b6
    mov al, cl                                ; 88 c8                       ; 0xc17b8 vgabios.c:1139
    xor ah, ah                                ; 30 e4                       ; 0xc17ba
    xor dx, dx                                ; 31 d2                       ; 0xc17bc
    call 01281h                               ; e8 c0 fa                    ; 0xc17be
    inc cx                                    ; 41                          ; 0xc17c1
    jmp short 017b3h                          ; eb ef                       ; 0xc17c2
    xor ax, ax                                ; 31 c0                       ; 0xc17c4 vgabios.c:1142
    call 01310h                               ; e8 47 fb                    ; 0xc17c6
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc17c9 vgabios.c:1145
    xor ah, ah                                ; 30 e4                       ; 0xc17cc
    mov di, ax                                ; 89 c7                       ; 0xc17ce
    sal di, 003h                              ; c1 e7 03                    ; 0xc17d0
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc17d3
    jne short 01844h                          ; 75 6a                       ; 0xc17d8
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc17da vgabios.c:1147
    mov di, word [es:bx+008h]                 ; 26 8b 7f 08                 ; 0xc17dd
    mov ax, word [es:bx+00ah]                 ; 26 8b 47 0a                 ; 0xc17e1
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc17e5
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc17e8 vgabios.c:1149
    mov bl, byte [es:si+002h]                 ; 26 8a 5c 02                 ; 0xc17eb
    cmp bl, 00eh                              ; 80 fb 0e                    ; 0xc17ef
    je short 01817h                           ; 74 23                       ; 0xc17f2
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc17f4
    jne short 01847h                          ; 75 4e                       ; 0xc17f7
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc17f9 vgabios.c:1151
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc17fc
    xor ah, ah                                ; 30 e4                       ; 0xc1800
    push ax                                   ; 50                          ; 0xc1802
    push strict byte 00000h                   ; 6a 00                       ; 0xc1803
    push strict byte 00000h                   ; 6a 00                       ; 0xc1805
    mov cx, 00100h                            ; b9 00 01                    ; 0xc1807
    mov bx, 0556ah                            ; bb 6a 55                    ; 0xc180a
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc180d
    xor al, al                                ; 30 c0                       ; 0xc1810
    call 02e2bh                               ; e8 16 16                    ; 0xc1812
    jmp short 0186ch                          ; eb 55                       ; 0xc1815 vgabios.c:1152
    mov al, bl                                ; 88 d8                       ; 0xc1817 vgabios.c:1154
    xor ah, ah                                ; 30 e4                       ; 0xc1819
    push ax                                   ; 50                          ; 0xc181b
    push strict byte 00000h                   ; 6a 00                       ; 0xc181c
    push strict byte 00000h                   ; 6a 00                       ; 0xc181e
    mov cx, 00100h                            ; b9 00 01                    ; 0xc1820
    mov bx, 05d6ah                            ; bb 6a 5d                    ; 0xc1823
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc1826
    xor al, al                                ; 30 c0                       ; 0xc1829
    call 02e2bh                               ; e8 fd 15                    ; 0xc182b
    cmp byte [bp-00ch], 007h                  ; 80 7e f4 07                 ; 0xc182e vgabios.c:1155
    jne short 0186ch                          ; 75 38                       ; 0xc1832
    mov cx, strict word 0000eh                ; b9 0e 00                    ; 0xc1834 vgabios.c:1156
    xor bx, bx                                ; 31 db                       ; 0xc1837
    mov dx, 07b6ah                            ; ba 6a 7b                    ; 0xc1839
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc183c
    call 02db6h                               ; e8 74 15                    ; 0xc183f
    jmp short 0186ch                          ; eb 28                       ; 0xc1842 vgabios.c:1157
    jmp near 018c8h                           ; e9 81 00                    ; 0xc1844
    mov al, bl                                ; 88 d8                       ; 0xc1847 vgabios.c:1159
    xor ah, ah                                ; 30 e4                       ; 0xc1849
    push ax                                   ; 50                          ; 0xc184b
    push strict byte 00000h                   ; 6a 00                       ; 0xc184c
    push strict byte 00000h                   ; 6a 00                       ; 0xc184e
    mov cx, 00100h                            ; b9 00 01                    ; 0xc1850
    mov bx, 06b6ah                            ; bb 6a 6b                    ; 0xc1853
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc1856
    xor al, al                                ; 30 c0                       ; 0xc1859
    call 02e2bh                               ; e8 cd 15                    ; 0xc185b
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc185e vgabios.c:1160
    xor bx, bx                                ; 31 db                       ; 0xc1861
    mov dx, 07c97h                            ; ba 97 7c                    ; 0xc1863
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc1866
    call 02db6h                               ; e8 4a 15                    ; 0xc1869
    cmp word [bp-018h], strict byte 00000h    ; 83 7e e8 00                 ; 0xc186c vgabios.c:1162
    jne short 01876h                          ; 75 04                       ; 0xc1870
    test di, di                               ; 85 ff                       ; 0xc1872
    je short 018c0h                           ; 74 4a                       ; 0xc1874
    xor cx, cx                                ; 31 c9                       ; 0xc1876 vgabios.c:1167
    mov es, [bp-018h]                         ; 8e 46 e8                    ; 0xc1878 vgabios.c:1169
    mov bx, di                                ; 89 fb                       ; 0xc187b
    add bx, cx                                ; 01 cb                       ; 0xc187d
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc187f
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1883
    je short 0188fh                           ; 74 08                       ; 0xc1885
    cmp al, byte [bp-00ch]                    ; 3a 46 f4                    ; 0xc1887 vgabios.c:1171
    je short 0188fh                           ; 74 03                       ; 0xc188a
    inc cx                                    ; 41                          ; 0xc188c vgabios.c:1173
    jmp short 01878h                          ; eb e9                       ; 0xc188d vgabios.c:1174
    mov es, [bp-018h]                         ; 8e 46 e8                    ; 0xc188f vgabios.c:1176
    mov bx, di                                ; 89 fb                       ; 0xc1892
    add bx, cx                                ; 01 cb                       ; 0xc1894
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc1896
    cmp al, byte [bp-00ch]                    ; 3a 46 f4                    ; 0xc189a
    jne short 018c0h                          ; 75 21                       ; 0xc189d
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc189f vgabios.c:1181
    xor ah, ah                                ; 30 e4                       ; 0xc18a2
    push ax                                   ; 50                          ; 0xc18a4
    mov al, byte [es:di+001h]                 ; 26 8a 45 01                 ; 0xc18a5
    push ax                                   ; 50                          ; 0xc18a9
    push word [es:di+004h]                    ; 26 ff 75 04                 ; 0xc18aa
    mov cx, word [es:di+002h]                 ; 26 8b 4d 02                 ; 0xc18ae
    mov bx, word [es:di+006h]                 ; 26 8b 5d 06                 ; 0xc18b2
    mov dx, word [es:di+008h]                 ; 26 8b 55 08                 ; 0xc18b6
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc18ba
    call 02e2bh                               ; e8 6b 15                    ; 0xc18bd
    xor bl, bl                                ; 30 db                       ; 0xc18c0 vgabios.c:1185
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc18c2
    mov AH, strict byte 011h                  ; b4 11                       ; 0xc18c4
    int 06dh                                  ; cd 6d                       ; 0xc18c6
    mov bx, 0596ah                            ; bb 6a 59                    ; 0xc18c8 vgabios.c:1189
    mov cx, ds                                ; 8c d9                       ; 0xc18cb
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc18cd
    call 009f0h                               ; e8 1d f1                    ; 0xc18d0
    mov es, [bp-016h]                         ; 8e 46 ea                    ; 0xc18d3 vgabios.c:1191
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc18d6
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc18da
    je short 018f8h                           ; 74 1a                       ; 0xc18dc
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc18de
    je short 018f3h                           ; 74 11                       ; 0xc18e0
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc18e2
    jne short 018fdh                          ; 75 17                       ; 0xc18e4
    mov bx, 0556ah                            ; bb 6a 55                    ; 0xc18e6 vgabios.c:1193
    mov cx, ds                                ; 8c d9                       ; 0xc18e9
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc18eb
    call 009f0h                               ; e8 ff f0                    ; 0xc18ee
    jmp short 018fdh                          ; eb 0a                       ; 0xc18f1 vgabios.c:1194
    mov bx, 05d6ah                            ; bb 6a 5d                    ; 0xc18f3 vgabios.c:1196
    jmp short 018e9h                          ; eb f1                       ; 0xc18f6
    mov bx, 06b6ah                            ; bb 6a 6b                    ; 0xc18f8 vgabios.c:1199
    jmp short 018e9h                          ; eb ec                       ; 0xc18fb
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc18fd vgabios.c:1202
    pop di                                    ; 5f                          ; 0xc1900
    pop si                                    ; 5e                          ; 0xc1901
    pop dx                                    ; 5a                          ; 0xc1902
    pop cx                                    ; 59                          ; 0xc1903
    pop bx                                    ; 5b                          ; 0xc1904
    pop bp                                    ; 5d                          ; 0xc1905
    retn                                      ; c3                          ; 0xc1906
  ; disGetNextSymbol 0xc1907 LB 0x2c3c -> off=0x0 cb=000000000000008e uValue=00000000000c1907 'vgamem_copy_pl4'
vgamem_copy_pl4:                             ; 0xc1907 LB 0x8e
    push bp                                   ; 55                          ; 0xc1907 vgabios.c:1205
    mov bp, sp                                ; 89 e5                       ; 0xc1908
    push si                                   ; 56                          ; 0xc190a
    push di                                   ; 57                          ; 0xc190b
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc190c
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc190f
    mov al, dl                                ; 88 d0                       ; 0xc1912
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1914
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc1917
    xor ah, ah                                ; 30 e4                       ; 0xc191a vgabios.c:1211
    mov dl, byte [bp+006h]                    ; 8a 56 06                    ; 0xc191c
    xor dh, dh                                ; 30 f6                       ; 0xc191f
    mov cx, dx                                ; 89 d1                       ; 0xc1921
    imul dx                                   ; f7 ea                       ; 0xc1923
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc1925
    xor dh, dh                                ; 30 f6                       ; 0xc1928
    mov si, dx                                ; 89 d6                       ; 0xc192a
    imul dx                                   ; f7 ea                       ; 0xc192c
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc192e
    xor dh, dh                                ; 30 f6                       ; 0xc1931
    mov bx, dx                                ; 89 d3                       ; 0xc1933
    add ax, dx                                ; 01 d0                       ; 0xc1935
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc1937
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc193a vgabios.c:1212
    xor ah, ah                                ; 30 e4                       ; 0xc193d
    imul cx                                   ; f7 e9                       ; 0xc193f
    imul si                                   ; f7 ee                       ; 0xc1941
    add ax, bx                                ; 01 d8                       ; 0xc1943
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc1945
    mov ax, 00105h                            ; b8 05 01                    ; 0xc1948 vgabios.c:1213
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc194b
    out DX, ax                                ; ef                          ; 0xc194e
    xor bl, bl                                ; 30 db                       ; 0xc194f vgabios.c:1214
    cmp bl, byte [bp+006h]                    ; 3a 5e 06                    ; 0xc1951
    jnc short 01985h                          ; 73 2f                       ; 0xc1954
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1956 vgabios.c:1216
    xor ah, ah                                ; 30 e4                       ; 0xc1959
    mov cx, ax                                ; 89 c1                       ; 0xc195b
    mov al, bl                                ; 88 d8                       ; 0xc195d
    mov dx, ax                                ; 89 c2                       ; 0xc195f
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1961
    mov si, ax                                ; 89 c6                       ; 0xc1964
    mov ax, dx                                ; 89 d0                       ; 0xc1966
    imul si                                   ; f7 ee                       ; 0xc1968
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc196a
    add si, ax                                ; 01 c6                       ; 0xc196d
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc196f
    add di, ax                                ; 01 c7                       ; 0xc1972
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1974
    mov es, dx                                ; 8e c2                       ; 0xc1977
    jcxz 01981h                               ; e3 06                       ; 0xc1979
    push DS                                   ; 1e                          ; 0xc197b
    mov ds, dx                                ; 8e da                       ; 0xc197c
    rep movsb                                 ; f3 a4                       ; 0xc197e
    pop DS                                    ; 1f                          ; 0xc1980
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc1981 vgabios.c:1217
    jmp short 01951h                          ; eb cc                       ; 0xc1983
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1985 vgabios.c:1218
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1988
    out DX, ax                                ; ef                          ; 0xc198b
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc198c vgabios.c:1219
    pop di                                    ; 5f                          ; 0xc198f
    pop si                                    ; 5e                          ; 0xc1990
    pop bp                                    ; 5d                          ; 0xc1991
    retn 00004h                               ; c2 04 00                    ; 0xc1992
  ; disGetNextSymbol 0xc1995 LB 0x2bae -> off=0x0 cb=000000000000007b uValue=00000000000c1995 'vgamem_fill_pl4'
vgamem_fill_pl4:                             ; 0xc1995 LB 0x7b
    push bp                                   ; 55                          ; 0xc1995 vgabios.c:1222
    mov bp, sp                                ; 89 e5                       ; 0xc1996
    push si                                   ; 56                          ; 0xc1998
    push di                                   ; 57                          ; 0xc1999
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc199a
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc199d
    mov al, dl                                ; 88 d0                       ; 0xc19a0
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc19a2
    mov bh, cl                                ; 88 cf                       ; 0xc19a5
    xor ah, ah                                ; 30 e4                       ; 0xc19a7 vgabios.c:1228
    mov dx, ax                                ; 89 c2                       ; 0xc19a9
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc19ab
    mov cx, ax                                ; 89 c1                       ; 0xc19ae
    mov ax, dx                                ; 89 d0                       ; 0xc19b0
    imul cx                                   ; f7 e9                       ; 0xc19b2
    mov dl, bh                                ; 88 fa                       ; 0xc19b4
    xor dh, dh                                ; 30 f6                       ; 0xc19b6
    imul dx                                   ; f7 ea                       ; 0xc19b8
    mov dx, ax                                ; 89 c2                       ; 0xc19ba
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc19bc
    xor ah, ah                                ; 30 e4                       ; 0xc19bf
    add dx, ax                                ; 01 c2                       ; 0xc19c1
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc19c3
    mov ax, 00205h                            ; b8 05 02                    ; 0xc19c6 vgabios.c:1229
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc19c9
    out DX, ax                                ; ef                          ; 0xc19cc
    xor bl, bl                                ; 30 db                       ; 0xc19cd vgabios.c:1230
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc19cf
    jnc short 01a00h                          ; 73 2c                       ; 0xc19d2
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc19d4 vgabios.c:1232
    xor ch, ch                                ; 30 ed                       ; 0xc19d7
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc19d9
    xor ah, ah                                ; 30 e4                       ; 0xc19dc
    mov si, ax                                ; 89 c6                       ; 0xc19de
    mov al, bl                                ; 88 d8                       ; 0xc19e0
    mov dx, ax                                ; 89 c2                       ; 0xc19e2
    mov al, bh                                ; 88 f8                       ; 0xc19e4
    mov di, ax                                ; 89 c7                       ; 0xc19e6
    mov ax, dx                                ; 89 d0                       ; 0xc19e8
    imul di                                   ; f7 ef                       ; 0xc19ea
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc19ec
    add di, ax                                ; 01 c7                       ; 0xc19ef
    mov ax, si                                ; 89 f0                       ; 0xc19f1
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc19f3
    mov es, dx                                ; 8e c2                       ; 0xc19f6
    jcxz 019fch                               ; e3 02                       ; 0xc19f8
    rep stosb                                 ; f3 aa                       ; 0xc19fa
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc19fc vgabios.c:1233
    jmp short 019cfh                          ; eb cf                       ; 0xc19fe
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1a00 vgabios.c:1234
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1a03
    out DX, ax                                ; ef                          ; 0xc1a06
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1a07 vgabios.c:1235
    pop di                                    ; 5f                          ; 0xc1a0a
    pop si                                    ; 5e                          ; 0xc1a0b
    pop bp                                    ; 5d                          ; 0xc1a0c
    retn 00004h                               ; c2 04 00                    ; 0xc1a0d
  ; disGetNextSymbol 0xc1a10 LB 0x2b33 -> off=0x0 cb=00000000000000b6 uValue=00000000000c1a10 'vgamem_copy_cga'
vgamem_copy_cga:                             ; 0xc1a10 LB 0xb6
    push bp                                   ; 55                          ; 0xc1a10 vgabios.c:1238
    mov bp, sp                                ; 89 e5                       ; 0xc1a11
    push si                                   ; 56                          ; 0xc1a13
    push di                                   ; 57                          ; 0xc1a14
    sub sp, strict byte 0000eh                ; 83 ec 0e                    ; 0xc1a15
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc1a18
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc1a1b
    mov byte [bp-00ah], cl                    ; 88 4e f6                    ; 0xc1a1e
    mov al, dl                                ; 88 d0                       ; 0xc1a21 vgabios.c:1244
    xor ah, ah                                ; 30 e4                       ; 0xc1a23
    mov bx, ax                                ; 89 c3                       ; 0xc1a25
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1a27
    mov si, ax                                ; 89 c6                       ; 0xc1a2a
    mov ax, bx                                ; 89 d8                       ; 0xc1a2c
    imul si                                   ; f7 ee                       ; 0xc1a2e
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc1a30
    mov di, bx                                ; 89 df                       ; 0xc1a33
    imul bx                                   ; f7 eb                       ; 0xc1a35
    mov dx, ax                                ; 89 c2                       ; 0xc1a37
    sar dx, 1                                 ; d1 fa                       ; 0xc1a39
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1a3b
    xor ah, ah                                ; 30 e4                       ; 0xc1a3e
    mov bx, ax                                ; 89 c3                       ; 0xc1a40
    add dx, ax                                ; 01 c2                       ; 0xc1a42
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1a44
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1a47 vgabios.c:1245
    imul si                                   ; f7 ee                       ; 0xc1a4a
    imul di                                   ; f7 ef                       ; 0xc1a4c
    sar ax, 1                                 ; d1 f8                       ; 0xc1a4e
    add ax, bx                                ; 01 d8                       ; 0xc1a50
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc1a52
    mov byte [bp-006h], bh                    ; 88 7e fa                    ; 0xc1a55 vgabios.c:1246
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1a58
    xor ah, ah                                ; 30 e4                       ; 0xc1a5b
    cwd                                       ; 99                          ; 0xc1a5d
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1a5e
    sar ax, 1                                 ; d1 f8                       ; 0xc1a60
    mov bx, ax                                ; 89 c3                       ; 0xc1a62
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1a64
    xor ah, ah                                ; 30 e4                       ; 0xc1a67
    cmp ax, bx                                ; 39 d8                       ; 0xc1a69
    jnl short 01abdh                          ; 7d 50                       ; 0xc1a6b
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc1a6d vgabios.c:1248
    xor bh, bh                                ; 30 ff                       ; 0xc1a70
    mov word [bp-012h], bx                    ; 89 5e ee                    ; 0xc1a72
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc1a75
    imul bx                                   ; f7 eb                       ; 0xc1a78
    mov bx, ax                                ; 89 c3                       ; 0xc1a7a
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc1a7c
    add si, ax                                ; 01 c6                       ; 0xc1a7f
    mov di, word [bp-010h]                    ; 8b 7e f0                    ; 0xc1a81
    add di, ax                                ; 01 c7                       ; 0xc1a84
    mov cx, word [bp-012h]                    ; 8b 4e ee                    ; 0xc1a86
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1a89
    mov es, dx                                ; 8e c2                       ; 0xc1a8c
    jcxz 01a96h                               ; e3 06                       ; 0xc1a8e
    push DS                                   ; 1e                          ; 0xc1a90
    mov ds, dx                                ; 8e da                       ; 0xc1a91
    rep movsb                                 ; f3 a4                       ; 0xc1a93
    pop DS                                    ; 1f                          ; 0xc1a95
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc1a96 vgabios.c:1249
    add si, 02000h                            ; 81 c6 00 20                 ; 0xc1a99
    add si, bx                                ; 01 de                       ; 0xc1a9d
    mov di, word [bp-010h]                    ; 8b 7e f0                    ; 0xc1a9f
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1aa2
    add di, bx                                ; 01 df                       ; 0xc1aa6
    mov cx, word [bp-012h]                    ; 8b 4e ee                    ; 0xc1aa8
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1aab
    mov es, dx                                ; 8e c2                       ; 0xc1aae
    jcxz 01ab8h                               ; e3 06                       ; 0xc1ab0
    push DS                                   ; 1e                          ; 0xc1ab2
    mov ds, dx                                ; 8e da                       ; 0xc1ab3
    rep movsb                                 ; f3 a4                       ; 0xc1ab5
    pop DS                                    ; 1f                          ; 0xc1ab7
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1ab8 vgabios.c:1250
    jmp short 01a58h                          ; eb 9b                       ; 0xc1abb
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1abd vgabios.c:1251
    pop di                                    ; 5f                          ; 0xc1ac0
    pop si                                    ; 5e                          ; 0xc1ac1
    pop bp                                    ; 5d                          ; 0xc1ac2
    retn 00004h                               ; c2 04 00                    ; 0xc1ac3
  ; disGetNextSymbol 0xc1ac6 LB 0x2a7d -> off=0x0 cb=0000000000000094 uValue=00000000000c1ac6 'vgamem_fill_cga'
vgamem_fill_cga:                             ; 0xc1ac6 LB 0x94
    push bp                                   ; 55                          ; 0xc1ac6 vgabios.c:1254
    mov bp, sp                                ; 89 e5                       ; 0xc1ac7
    push si                                   ; 56                          ; 0xc1ac9
    push di                                   ; 57                          ; 0xc1aca
    sub sp, strict byte 0000ch                ; 83 ec 0c                    ; 0xc1acb
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1ace
    mov al, dl                                ; 88 d0                       ; 0xc1ad1
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1ad3
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1ad6
    xor ah, ah                                ; 30 e4                       ; 0xc1ad9 vgabios.c:1260
    mov dx, ax                                ; 89 c2                       ; 0xc1adb
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1add
    mov bx, ax                                ; 89 c3                       ; 0xc1ae0
    mov ax, dx                                ; 89 d0                       ; 0xc1ae2
    imul bx                                   ; f7 eb                       ; 0xc1ae4
    mov dl, cl                                ; 88 ca                       ; 0xc1ae6
    xor dh, dh                                ; 30 f6                       ; 0xc1ae8
    imul dx                                   ; f7 ea                       ; 0xc1aea
    mov dx, ax                                ; 89 c2                       ; 0xc1aec
    sar dx, 1                                 ; d1 fa                       ; 0xc1aee
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1af0
    xor ah, ah                                ; 30 e4                       ; 0xc1af3
    add dx, ax                                ; 01 c2                       ; 0xc1af5
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1af7
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1afa vgabios.c:1261
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1afd
    xor ah, ah                                ; 30 e4                       ; 0xc1b00
    cwd                                       ; 99                          ; 0xc1b02
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1b03
    sar ax, 1                                 ; d1 f8                       ; 0xc1b05
    mov dx, ax                                ; 89 c2                       ; 0xc1b07
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1b09
    xor ah, ah                                ; 30 e4                       ; 0xc1b0c
    cmp ax, dx                                ; 39 d0                       ; 0xc1b0e
    jnl short 01b51h                          ; 7d 3f                       ; 0xc1b10
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc1b12 vgabios.c:1263
    xor bh, bh                                ; 30 ff                       ; 0xc1b15
    mov dl, byte [bp+006h]                    ; 8a 56 06                    ; 0xc1b17
    xor dh, dh                                ; 30 f6                       ; 0xc1b1a
    mov si, dx                                ; 89 d6                       ; 0xc1b1c
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1b1e
    imul dx                                   ; f7 ea                       ; 0xc1b21
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc1b23
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1b26
    add di, ax                                ; 01 c7                       ; 0xc1b29
    mov cx, bx                                ; 89 d9                       ; 0xc1b2b
    mov ax, si                                ; 89 f0                       ; 0xc1b2d
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1b2f
    mov es, dx                                ; 8e c2                       ; 0xc1b32
    jcxz 01b38h                               ; e3 02                       ; 0xc1b34
    rep stosb                                 ; f3 aa                       ; 0xc1b36
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1b38 vgabios.c:1264
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1b3b
    add di, word [bp-010h]                    ; 03 7e f0                    ; 0xc1b3f
    mov cx, bx                                ; 89 d9                       ; 0xc1b42
    mov ax, si                                ; 89 f0                       ; 0xc1b44
    mov es, dx                                ; 8e c2                       ; 0xc1b46
    jcxz 01b4ch                               ; e3 02                       ; 0xc1b48
    rep stosb                                 ; f3 aa                       ; 0xc1b4a
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1b4c vgabios.c:1265
    jmp short 01afdh                          ; eb ac                       ; 0xc1b4f
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1b51 vgabios.c:1266
    pop di                                    ; 5f                          ; 0xc1b54
    pop si                                    ; 5e                          ; 0xc1b55
    pop bp                                    ; 5d                          ; 0xc1b56
    retn 00004h                               ; c2 04 00                    ; 0xc1b57
  ; disGetNextSymbol 0xc1b5a LB 0x29e9 -> off=0x0 cb=0000000000000081 uValue=00000000000c1b5a 'vgamem_copy_linear'
vgamem_copy_linear:                          ; 0xc1b5a LB 0x81
    push bp                                   ; 55                          ; 0xc1b5a vgabios.c:1269
    mov bp, sp                                ; 89 e5                       ; 0xc1b5b
    push si                                   ; 56                          ; 0xc1b5d
    push di                                   ; 57                          ; 0xc1b5e
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1b5f
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc1b62
    mov al, dl                                ; 88 d0                       ; 0xc1b65
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1b67
    mov bx, cx                                ; 89 cb                       ; 0xc1b6a
    xor ah, ah                                ; 30 e4                       ; 0xc1b6c vgabios.c:1275
    mov si, ax                                ; 89 c6                       ; 0xc1b6e
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1b70
    mov di, ax                                ; 89 c7                       ; 0xc1b73
    mov ax, si                                ; 89 f0                       ; 0xc1b75
    imul di                                   ; f7 ef                       ; 0xc1b77
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1b79
    mov si, ax                                ; 89 c6                       ; 0xc1b7c
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1b7e
    xor ah, ah                                ; 30 e4                       ; 0xc1b81
    mov cx, ax                                ; 89 c1                       ; 0xc1b83
    add si, ax                                ; 01 c6                       ; 0xc1b85
    sal si, 003h                              ; c1 e6 03                    ; 0xc1b87
    mov word [bp-00ch], si                    ; 89 76 f4                    ; 0xc1b8a
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1b8d vgabios.c:1276
    imul di                                   ; f7 ef                       ; 0xc1b90
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1b92
    add ax, cx                                ; 01 c8                       ; 0xc1b95
    sal ax, 003h                              ; c1 e0 03                    ; 0xc1b97
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc1b9a
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1b9d vgabios.c:1277
    sal word [bp+004h], 003h                  ; c1 66 04 03                 ; 0xc1ba0 vgabios.c:1278
    mov byte [bp-006h], ch                    ; 88 6e fa                    ; 0xc1ba4 vgabios.c:1279
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1ba7
    cmp al, byte [bp+006h]                    ; 3a 46 06                    ; 0xc1baa
    jnc short 01bd2h                          ; 73 23                       ; 0xc1bad
    xor ah, ah                                ; 30 e4                       ; 0xc1baf vgabios.c:1281
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1bb1
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc1bb4
    add si, ax                                ; 01 c6                       ; 0xc1bb7
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1bb9
    add di, ax                                ; 01 c7                       ; 0xc1bbc
    mov cx, bx                                ; 89 d9                       ; 0xc1bbe
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1bc0
    mov es, dx                                ; 8e c2                       ; 0xc1bc3
    jcxz 01bcdh                               ; e3 06                       ; 0xc1bc5
    push DS                                   ; 1e                          ; 0xc1bc7
    mov ds, dx                                ; 8e da                       ; 0xc1bc8
    rep movsb                                 ; f3 a4                       ; 0xc1bca
    pop DS                                    ; 1f                          ; 0xc1bcc
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1bcd vgabios.c:1282
    jmp short 01ba7h                          ; eb d5                       ; 0xc1bd0
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1bd2 vgabios.c:1283
    pop di                                    ; 5f                          ; 0xc1bd5
    pop si                                    ; 5e                          ; 0xc1bd6
    pop bp                                    ; 5d                          ; 0xc1bd7
    retn 00004h                               ; c2 04 00                    ; 0xc1bd8
  ; disGetNextSymbol 0xc1bdb LB 0x2968 -> off=0x0 cb=000000000000006d uValue=00000000000c1bdb 'vgamem_fill_linear'
vgamem_fill_linear:                          ; 0xc1bdb LB 0x6d
    push bp                                   ; 55                          ; 0xc1bdb vgabios.c:1286
    mov bp, sp                                ; 89 e5                       ; 0xc1bdc
    push si                                   ; 56                          ; 0xc1bde
    push di                                   ; 57                          ; 0xc1bdf
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc1be0
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1be3
    mov al, dl                                ; 88 d0                       ; 0xc1be6
    mov si, cx                                ; 89 ce                       ; 0xc1be8
    xor ah, ah                                ; 30 e4                       ; 0xc1bea vgabios.c:1292
    mov dx, ax                                ; 89 c2                       ; 0xc1bec
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1bee
    mov di, ax                                ; 89 c7                       ; 0xc1bf1
    mov ax, dx                                ; 89 d0                       ; 0xc1bf3
    imul di                                   ; f7 ef                       ; 0xc1bf5
    mul cx                                    ; f7 e1                       ; 0xc1bf7
    mov dx, ax                                ; 89 c2                       ; 0xc1bf9
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1bfb
    xor ah, ah                                ; 30 e4                       ; 0xc1bfe
    add ax, dx                                ; 01 d0                       ; 0xc1c00
    sal ax, 003h                              ; c1 e0 03                    ; 0xc1c02
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc1c05
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1c08 vgabios.c:1293
    sal si, 003h                              ; c1 e6 03                    ; 0xc1c0b vgabios.c:1294
    mov byte [bp-008h], 000h                  ; c6 46 f8 00                 ; 0xc1c0e vgabios.c:1295
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1c12
    cmp al, byte [bp+004h]                    ; 3a 46 04                    ; 0xc1c15
    jnc short 01c3fh                          ; 73 25                       ; 0xc1c18
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1c1a vgabios.c:1297
    xor ah, ah                                ; 30 e4                       ; 0xc1c1d
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc1c1f
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1c22
    mul si                                    ; f7 e6                       ; 0xc1c25
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc1c27
    add di, ax                                ; 01 c7                       ; 0xc1c2a
    mov cx, bx                                ; 89 d9                       ; 0xc1c2c
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc1c2e
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1c31
    mov es, dx                                ; 8e c2                       ; 0xc1c34
    jcxz 01c3ah                               ; e3 02                       ; 0xc1c36
    rep stosb                                 ; f3 aa                       ; 0xc1c38
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc1c3a vgabios.c:1298
    jmp short 01c12h                          ; eb d3                       ; 0xc1c3d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1c3f vgabios.c:1299
    pop di                                    ; 5f                          ; 0xc1c42
    pop si                                    ; 5e                          ; 0xc1c43
    pop bp                                    ; 5d                          ; 0xc1c44
    retn 00004h                               ; c2 04 00                    ; 0xc1c45
  ; disGetNextSymbol 0xc1c48 LB 0x28fb -> off=0x0 cb=0000000000000688 uValue=00000000000c1c48 'biosfn_scroll'
biosfn_scroll:                               ; 0xc1c48 LB 0x688
    push bp                                   ; 55                          ; 0xc1c48 vgabios.c:1302
    mov bp, sp                                ; 89 e5                       ; 0xc1c49
    push si                                   ; 56                          ; 0xc1c4b
    push di                                   ; 57                          ; 0xc1c4c
    sub sp, strict byte 0001eh                ; 83 ec 1e                    ; 0xc1c4d
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1c50
    mov byte [bp-010h], dl                    ; 88 56 f0                    ; 0xc1c53
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1c56
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1c59
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc1c5c vgabios.c:1311
    jnbe short 01c7dh                         ; 77 1c                       ; 0xc1c5f
    cmp cl, byte [bp+006h]                    ; 3a 4e 06                    ; 0xc1c61 vgabios.c:1312
    jnbe short 01c7dh                         ; 77 17                       ; 0xc1c64
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1c66 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1c69
    mov es, ax                                ; 8e c0                       ; 0xc1c6c
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1c6e
    xor ah, ah                                ; 30 e4                       ; 0xc1c71 vgabios.c:1316
    call 0382ah                               ; e8 b4 1b                    ; 0xc1c73
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc1c76
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1c79 vgabios.c:1317
    jne short 01c80h                          ; 75 03                       ; 0xc1c7b
    jmp near 022c7h                           ; e9 47 06                    ; 0xc1c7d
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1c80 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1c83
    mov es, ax                                ; 8e c0                       ; 0xc1c86
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1c88
    xor ah, ah                                ; 30 e4                       ; 0xc1c8b vgabios.c:48
    inc ax                                    ; 40                          ; 0xc1c8d
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc1c8e
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1c91 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc1c94
    mov word [bp-01eh], ax                    ; 89 46 e2                    ; 0xc1c97 vgabios.c:58
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc1c9a vgabios.c:1324
    jne short 01ca9h                          ; 75 09                       ; 0xc1c9e
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc1ca0 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1ca3
    mov byte [bp+008h], al                    ; 88 46 08                    ; 0xc1ca6 vgabios.c:48
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1ca9 vgabios.c:1327
    xor ah, ah                                ; 30 e4                       ; 0xc1cac
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1cae
    jc short 01cbbh                           ; 72 08                       ; 0xc1cb1
    mov al, byte [bp-016h]                    ; 8a 46 ea                    ; 0xc1cb3
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc1cb6
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc1cb8
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1cbb vgabios.c:1328
    xor ah, ah                                ; 30 e4                       ; 0xc1cbe
    cmp ax, word [bp-01eh]                    ; 3b 46 e2                    ; 0xc1cc0
    jc short 01ccdh                           ; 72 08                       ; 0xc1cc3
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc1cc5
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc1cc8
    mov byte [bp+006h], al                    ; 88 46 06                    ; 0xc1cca
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1ccd vgabios.c:1329
    xor ah, ah                                ; 30 e4                       ; 0xc1cd0
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1cd2
    jbe short 01cdah                          ; 76 03                       ; 0xc1cd5
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1cd7
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1cda vgabios.c:1330
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc1cdd
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc1ce0
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1ce2
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1ce5 vgabios.c:1332
    mov byte [bp-01ah], al                    ; 88 46 e6                    ; 0xc1ce8
    mov byte [bp-019h], 000h                  ; c6 46 e7 00                 ; 0xc1ceb
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1cef
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1cf2
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc1cf5
    dec ax                                    ; 48                          ; 0xc1cf8
    mov word [bp-022h], ax                    ; 89 46 de                    ; 0xc1cf9
    mov di, word [bp-016h]                    ; 8b 7e ea                    ; 0xc1cfc
    dec di                                    ; 4f                          ; 0xc1cff
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc1d00
    mul word [bp-016h]                        ; f7 66 ea                    ; 0xc1d03
    mov cx, ax                                ; 89 c1                       ; 0xc1d06
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc1d08
    jne short 01d58h                          ; 75 49                       ; 0xc1d0d
    add ax, ax                                ; 01 c0                       ; 0xc1d0f vgabios.c:1335
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc1d11
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc1d13
    xor dh, dh                                ; 30 f6                       ; 0xc1d16
    inc ax                                    ; 40                          ; 0xc1d18
    mul dx                                    ; f7 e2                       ; 0xc1d19
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc1d1b
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1d1e vgabios.c:1340
    jne short 01d5bh                          ; 75 37                       ; 0xc1d22
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1d24
    jne short 01d5bh                          ; 75 31                       ; 0xc1d28
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1d2a
    jne short 01d5bh                          ; 75 2b                       ; 0xc1d2e
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1d30
    xor ah, ah                                ; 30 e4                       ; 0xc1d33
    cmp ax, di                                ; 39 f8                       ; 0xc1d35
    jne short 01d5bh                          ; 75 22                       ; 0xc1d37
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1d39
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc1d3c
    jne short 01d5bh                          ; 75 1a                       ; 0xc1d3f
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1d41 vgabios.c:1342
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1d44
    add ax, strict word 00020h                ; 05 20 00                    ; 0xc1d47
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc1d4a
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1d4e
    jcxz 01d55h                               ; e3 02                       ; 0xc1d51
    rep stosw                                 ; f3 ab                       ; 0xc1d53
    jmp near 022c7h                           ; e9 6f 05                    ; 0xc1d55 vgabios.c:1344
    jmp near 01ecbh                           ; e9 70 01                    ; 0xc1d58
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1d5b vgabios.c:1346
    jne short 01dc1h                          ; 75 60                       ; 0xc1d5f
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1d61 vgabios.c:1347
    xor ah, ah                                ; 30 e4                       ; 0xc1d64
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1d66
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc1d69
    xor dh, dh                                ; 30 f6                       ; 0xc1d6c
    cmp dx, word [bp-01ch]                    ; 3b 56 e4                    ; 0xc1d6e
    jc short 01dc3h                           ; 72 50                       ; 0xc1d71
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1d73 vgabios.c:1349
    xor ah, ah                                ; 30 e4                       ; 0xc1d76
    add ax, word [bp-01ch]                    ; 03 46 e4                    ; 0xc1d78
    cmp ax, dx                                ; 39 d0                       ; 0xc1d7b
    jnbe short 01d85h                         ; 77 06                       ; 0xc1d7d
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1d7f
    jne short 01dc6h                          ; 75 41                       ; 0xc1d83
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1d85 vgabios.c:1350
    xor ch, ch                                ; 30 ed                       ; 0xc1d88
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1d8a
    xor ah, ah                                ; 30 e4                       ; 0xc1d8d
    mov si, ax                                ; 89 c6                       ; 0xc1d8f
    sal si, 008h                              ; c1 e6 08                    ; 0xc1d91
    add si, strict byte 00020h                ; 83 c6 20                    ; 0xc1d94
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1d97
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1d9a
    mov dx, ax                                ; 89 c2                       ; 0xc1d9d
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1d9f
    xor ah, ah                                ; 30 e4                       ; 0xc1da2
    mov di, ax                                ; 89 c7                       ; 0xc1da4
    add di, dx                                ; 01 d7                       ; 0xc1da6
    add di, di                                ; 01 ff                       ; 0xc1da8
    add di, word [bp-020h]                    ; 03 7e e0                    ; 0xc1daa
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1dad
    xor bh, bh                                ; 30 ff                       ; 0xc1db0
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1db2
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc1db5
    mov ax, si                                ; 89 f0                       ; 0xc1db9
    jcxz 01dbfh                               ; e3 02                       ; 0xc1dbb
    rep stosw                                 ; f3 ab                       ; 0xc1dbd
    jmp short 01e06h                          ; eb 45                       ; 0xc1dbf vgabios.c:1351
    jmp short 01e0ch                          ; eb 49                       ; 0xc1dc1
    jmp near 022c7h                           ; e9 01 05                    ; 0xc1dc3
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1dc6 vgabios.c:1352
    xor ch, ch                                ; 30 ed                       ; 0xc1dc9
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1dcb
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1dce
    mov byte [bp-018h], dl                    ; 88 56 e8                    ; 0xc1dd1
    mov byte [bp-017h], ch                    ; 88 6e e9                    ; 0xc1dd4
    mov si, ax                                ; 89 c6                       ; 0xc1dd7
    add si, word [bp-018h]                    ; 03 76 e8                    ; 0xc1dd9
    add si, si                                ; 01 f6                       ; 0xc1ddc
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1dde
    xor bh, bh                                ; 30 ff                       ; 0xc1de1
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1de3
    mov bx, word [bx+047b0h]                  ; 8b 9f b0 47                 ; 0xc1de6
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1dea
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1ded
    add ax, word [bp-018h]                    ; 03 46 e8                    ; 0xc1df0
    add ax, ax                                ; 01 c0                       ; 0xc1df3
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1df5
    add di, ax                                ; 01 c7                       ; 0xc1df8
    mov dx, bx                                ; 89 da                       ; 0xc1dfa
    mov es, bx                                ; 8e c3                       ; 0xc1dfc
    jcxz 01e06h                               ; e3 06                       ; 0xc1dfe
    push DS                                   ; 1e                          ; 0xc1e00
    mov ds, dx                                ; 8e da                       ; 0xc1e01
    rep movsw                                 ; f3 a5                       ; 0xc1e03
    pop DS                                    ; 1f                          ; 0xc1e05
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc1e06 vgabios.c:1353
    jmp near 01d69h                           ; e9 5d ff                    ; 0xc1e09
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1e0c vgabios.c:1356
    xor ah, ah                                ; 30 e4                       ; 0xc1e0f
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1e11
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1e14
    xor ah, ah                                ; 30 e4                       ; 0xc1e17
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1e19
    jnbe short 01dc3h                         ; 77 a5                       ; 0xc1e1c
    mov dl, al                                ; 88 c2                       ; 0xc1e1e vgabios.c:1358
    xor dh, dh                                ; 30 f6                       ; 0xc1e20
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1e22
    add ax, dx                                ; 01 d0                       ; 0xc1e25
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1e27
    jnbe short 01e32h                         ; 77 06                       ; 0xc1e2a
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1e2c
    jne short 01e6eh                          ; 75 3c                       ; 0xc1e30
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1e32 vgabios.c:1359
    xor ch, ch                                ; 30 ed                       ; 0xc1e35
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1e37
    xor ah, ah                                ; 30 e4                       ; 0xc1e3a
    mov si, ax                                ; 89 c6                       ; 0xc1e3c
    sal si, 008h                              ; c1 e6 08                    ; 0xc1e3e
    add si, strict byte 00020h                ; 83 c6 20                    ; 0xc1e41
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1e44
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1e47
    mov dx, ax                                ; 89 c2                       ; 0xc1e4a
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1e4c
    xor ah, ah                                ; 30 e4                       ; 0xc1e4f
    add ax, dx                                ; 01 d0                       ; 0xc1e51
    add ax, ax                                ; 01 c0                       ; 0xc1e53
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1e55
    add di, ax                                ; 01 c7                       ; 0xc1e58
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1e5a
    xor bh, bh                                ; 30 ff                       ; 0xc1e5d
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1e5f
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc1e62
    mov ax, si                                ; 89 f0                       ; 0xc1e66
    jcxz 01e6ch                               ; e3 02                       ; 0xc1e68
    rep stosw                                 ; f3 ab                       ; 0xc1e6a
    jmp short 01ebbh                          ; eb 4d                       ; 0xc1e6c vgabios.c:1360
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1e6e vgabios.c:1361
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc1e71
    mov byte [bp-017h], dh                    ; 88 76 e9                    ; 0xc1e74
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1e77
    xor ah, ah                                ; 30 e4                       ; 0xc1e7a
    mov dx, word [bp-01ch]                    ; 8b 56 e4                    ; 0xc1e7c
    sub dx, ax                                ; 29 c2                       ; 0xc1e7f
    mov ax, dx                                ; 89 d0                       ; 0xc1e81
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1e83
    mov cl, byte [bp-008h]                    ; 8a 4e f8                    ; 0xc1e86
    xor ch, ch                                ; 30 ed                       ; 0xc1e89
    mov si, ax                                ; 89 c6                       ; 0xc1e8b
    add si, cx                                ; 01 ce                       ; 0xc1e8d
    add si, si                                ; 01 f6                       ; 0xc1e8f
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1e91
    xor bh, bh                                ; 30 ff                       ; 0xc1e94
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1e96
    mov bx, word [bx+047b0h]                  ; 8b 9f b0 47                 ; 0xc1e99
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1e9d
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1ea0
    add ax, cx                                ; 01 c8                       ; 0xc1ea3
    add ax, ax                                ; 01 c0                       ; 0xc1ea5
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1ea7
    add di, ax                                ; 01 c7                       ; 0xc1eaa
    mov cx, word [bp-018h]                    ; 8b 4e e8                    ; 0xc1eac
    mov dx, bx                                ; 89 da                       ; 0xc1eaf
    mov es, bx                                ; 8e c3                       ; 0xc1eb1
    jcxz 01ebbh                               ; e3 06                       ; 0xc1eb3
    push DS                                   ; 1e                          ; 0xc1eb5
    mov ds, dx                                ; 8e da                       ; 0xc1eb6
    rep movsw                                 ; f3 a5                       ; 0xc1eb8
    pop DS                                    ; 1f                          ; 0xc1eba
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1ebb vgabios.c:1362
    xor ah, ah                                ; 30 e4                       ; 0xc1ebe
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1ec0
    jc short 01ef8h                           ; 72 33                       ; 0xc1ec3
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc1ec5 vgabios.c:1363
    jmp near 01e14h                           ; e9 49 ff                    ; 0xc1ec8
    mov si, word [bp-01ah]                    ; 8b 76 e6                    ; 0xc1ecb vgabios.c:1369
    mov al, byte [si+0482ch]                  ; 8a 84 2c 48                 ; 0xc1ece
    xor ah, ah                                ; 30 e4                       ; 0xc1ed2
    mov si, ax                                ; 89 c6                       ; 0xc1ed4
    sal si, 006h                              ; c1 e6 06                    ; 0xc1ed6
    mov al, byte [si+04842h]                  ; 8a 84 42 48                 ; 0xc1ed9
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc1edd
    mov al, byte [bx+047aeh]                  ; 8a 87 ae 47                 ; 0xc1ee0 vgabios.c:1370
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc1ee4
    jc short 01ef4h                           ; 72 0c                       ; 0xc1ee6
    jbe short 01efbh                          ; 76 11                       ; 0xc1ee8
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc1eea
    je short 01f29h                           ; 74 3b                       ; 0xc1eec
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc1eee
    je short 01efbh                           ; 74 09                       ; 0xc1ef0
    jmp short 01ef8h                          ; eb 04                       ; 0xc1ef2
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc1ef4
    je short 01f2ch                           ; 74 34                       ; 0xc1ef6
    jmp near 022c7h                           ; e9 cc 03                    ; 0xc1ef8
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1efb vgabios.c:1374
    jne short 01f27h                          ; 75 26                       ; 0xc1eff
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1f01
    jne short 01f69h                          ; 75 62                       ; 0xc1f05
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1f07
    jne short 01f69h                          ; 75 5c                       ; 0xc1f0b
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1f0d
    xor ah, ah                                ; 30 e4                       ; 0xc1f10
    mov dx, word [bp-016h]                    ; 8b 56 ea                    ; 0xc1f12
    dec dx                                    ; 4a                          ; 0xc1f15
    cmp ax, dx                                ; 39 d0                       ; 0xc1f16
    jne short 01f69h                          ; 75 4f                       ; 0xc1f18
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1f1a
    xor ah, dh                                ; 30 f4                       ; 0xc1f1d
    mov dx, word [bp-01eh]                    ; 8b 56 e2                    ; 0xc1f1f
    dec dx                                    ; 4a                          ; 0xc1f22
    cmp ax, dx                                ; 39 d0                       ; 0xc1f23
    je short 01f2fh                           ; 74 08                       ; 0xc1f25
    jmp short 01f69h                          ; eb 40                       ; 0xc1f27
    jmp near 0219fh                           ; e9 73 02                    ; 0xc1f29
    jmp near 02059h                           ; e9 2a 01                    ; 0xc1f2c
    mov ax, 00205h                            ; b8 05 02                    ; 0xc1f2f vgabios.c:1376
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1f32
    out DX, ax                                ; ef                          ; 0xc1f35
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc1f36 vgabios.c:1377
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1f39
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc1f3c
    xor dh, dh                                ; 30 f6                       ; 0xc1f3f
    mul dx                                    ; f7 e2                       ; 0xc1f41
    mov dl, byte [bp-010h]                    ; 8a 56 f0                    ; 0xc1f43
    xor dh, dh                                ; 30 f6                       ; 0xc1f46
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1f48
    xor bh, bh                                ; 30 ff                       ; 0xc1f4b
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1f4d
    mov bx, word [bx+047b0h]                  ; 8b 9f b0 47                 ; 0xc1f50
    mov cx, ax                                ; 89 c1                       ; 0xc1f54
    mov ax, dx                                ; 89 d0                       ; 0xc1f56
    xor di, di                                ; 31 ff                       ; 0xc1f58
    mov es, bx                                ; 8e c3                       ; 0xc1f5a
    jcxz 01f60h                               ; e3 02                       ; 0xc1f5c
    rep stosb                                 ; f3 aa                       ; 0xc1f5e
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1f60 vgabios.c:1378
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1f63
    out DX, ax                                ; ef                          ; 0xc1f66
    jmp short 01ef8h                          ; eb 8f                       ; 0xc1f67 vgabios.c:1380
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1f69 vgabios.c:1382
    jne short 01fe4h                          ; 75 75                       ; 0xc1f6d
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1f6f vgabios.c:1383
    xor ah, ah                                ; 30 e4                       ; 0xc1f72
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1f74
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1f77
    xor ah, ah                                ; 30 e4                       ; 0xc1f7a
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1f7c
    jc short 01fe1h                           ; 72 60                       ; 0xc1f7f
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc1f81 vgabios.c:1385
    xor dh, dh                                ; 30 f6                       ; 0xc1f84
    add dx, word [bp-01ch]                    ; 03 56 e4                    ; 0xc1f86
    cmp dx, ax                                ; 39 c2                       ; 0xc1f89
    jnbe short 01f93h                         ; 77 06                       ; 0xc1f8b
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1f8d
    jne short 01fb4h                          ; 75 21                       ; 0xc1f91
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1f93 vgabios.c:1386
    xor ah, ah                                ; 30 e4                       ; 0xc1f96
    push ax                                   ; 50                          ; 0xc1f98
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc1f99
    push ax                                   ; 50                          ; 0xc1f9c
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc1f9d
    xor ch, ch                                ; 30 ed                       ; 0xc1fa0
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc1fa2
    xor bh, bh                                ; 30 ff                       ; 0xc1fa5
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc1fa7
    xor dh, dh                                ; 30 f6                       ; 0xc1faa
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1fac
    call 01995h                               ; e8 e3 f9                    ; 0xc1faf
    jmp short 01fdch                          ; eb 28                       ; 0xc1fb2 vgabios.c:1387
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc1fb4 vgabios.c:1388
    push ax                                   ; 50                          ; 0xc1fb7
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc1fb8
    push ax                                   ; 50                          ; 0xc1fbb
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1fbc
    xor ch, ch                                ; 30 ed                       ; 0xc1fbf
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc1fc1
    xor bh, bh                                ; 30 ff                       ; 0xc1fc4
    mov dl, bl                                ; 88 da                       ; 0xc1fc6
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc1fc8
    xor dh, dh                                ; 30 f6                       ; 0xc1fcb
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1fcd
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc1fd0
    mov byte [bp-017h], ah                    ; 88 66 e9                    ; 0xc1fd3
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc1fd6
    call 01907h                               ; e8 2b f9                    ; 0xc1fd9
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc1fdc vgabios.c:1389
    jmp short 01f77h                          ; eb 96                       ; 0xc1fdf
    jmp near 022c7h                           ; e9 e3 02                    ; 0xc1fe1
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1fe4 vgabios.c:1392
    xor ah, ah                                ; 30 e4                       ; 0xc1fe7
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1fe9
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1fec
    xor ah, ah                                ; 30 e4                       ; 0xc1fef
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1ff1
    jnbe short 01fe1h                         ; 77 eb                       ; 0xc1ff4
    mov dl, al                                ; 88 c2                       ; 0xc1ff6 vgabios.c:1394
    xor dh, dh                                ; 30 f6                       ; 0xc1ff8
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1ffa
    add ax, dx                                ; 01 d0                       ; 0xc1ffd
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1fff
    jnbe short 0200ah                         ; 77 06                       ; 0xc2002
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2004
    jne short 0202bh                          ; 75 21                       ; 0xc2008
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc200a vgabios.c:1395
    xor ah, ah                                ; 30 e4                       ; 0xc200d
    push ax                                   ; 50                          ; 0xc200f
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2010
    push ax                                   ; 50                          ; 0xc2013
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc2014
    xor ch, ch                                ; 30 ed                       ; 0xc2017
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2019
    xor bh, bh                                ; 30 ff                       ; 0xc201c
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc201e
    xor dh, dh                                ; 30 f6                       ; 0xc2021
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2023
    call 01995h                               ; e8 6c f9                    ; 0xc2026
    jmp short 0204ah                          ; eb 1f                       ; 0xc2029 vgabios.c:1396
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc202b vgabios.c:1397
    xor ah, ah                                ; 30 e4                       ; 0xc202e
    push ax                                   ; 50                          ; 0xc2030
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc2031
    push ax                                   ; 50                          ; 0xc2034
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2035
    xor ch, ch                                ; 30 ed                       ; 0xc2038
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc203a
    xor bh, bh                                ; 30 ff                       ; 0xc203d
    mov dl, bl                                ; 88 da                       ; 0xc203f
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc2041
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2044
    call 01907h                               ; e8 bd f8                    ; 0xc2047
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc204a vgabios.c:1398
    xor ah, ah                                ; 30 e4                       ; 0xc204d
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc204f
    jc short 020a2h                           ; 72 4e                       ; 0xc2052
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc2054 vgabios.c:1399
    jmp short 01fech                          ; eb 93                       ; 0xc2057
    mov al, byte [bx+047afh]                  ; 8a 87 af 47                 ; 0xc2059 vgabios.c:1404
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc205d
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2060 vgabios.c:1405
    jne short 020a5h                          ; 75 3f                       ; 0xc2064
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc2066
    jne short 020a5h                          ; 75 39                       ; 0xc206a
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc206c
    jne short 020a5h                          ; 75 33                       ; 0xc2070
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2072
    cmp ax, di                                ; 39 f8                       ; 0xc2075
    jne short 020a5h                          ; 75 2c                       ; 0xc2077
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2079
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc207c
    jne short 020a5h                          ; 75 24                       ; 0xc207f
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc2081 vgabios.c:1407
    xor dh, dh                                ; 30 f6                       ; 0xc2084
    mov ax, cx                                ; 89 c8                       ; 0xc2086
    mul dx                                    ; f7 e2                       ; 0xc2088
    mov dl, byte [bp-014h]                    ; 8a 56 ec                    ; 0xc208a
    xor dh, dh                                ; 30 f6                       ; 0xc208d
    mul dx                                    ; f7 e2                       ; 0xc208f
    mov cx, ax                                ; 89 c1                       ; 0xc2091
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2093
    xor ah, ah                                ; 30 e4                       ; 0xc2096
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc2098
    xor di, di                                ; 31 ff                       ; 0xc209c
    jcxz 020a2h                               ; e3 02                       ; 0xc209e
    rep stosb                                 ; f3 aa                       ; 0xc20a0
    jmp near 022c7h                           ; e9 22 02                    ; 0xc20a2 vgabios.c:1409
    cmp byte [bp-014h], 002h                  ; 80 7e ec 02                 ; 0xc20a5 vgabios.c:1411
    jne short 020b4h                          ; 75 09                       ; 0xc20a9
    sal byte [bp-008h], 1                     ; d0 66 f8                    ; 0xc20ab vgabios.c:1413
    sal byte [bp-00ah], 1                     ; d0 66 f6                    ; 0xc20ae vgabios.c:1414
    sal word [bp-01eh], 1                     ; d1 66 e2                    ; 0xc20b1 vgabios.c:1415
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc20b4 vgabios.c:1418
    jne short 02123h                          ; 75 69                       ; 0xc20b8
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc20ba vgabios.c:1419
    xor ah, ah                                ; 30 e4                       ; 0xc20bd
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc20bf
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc20c2
    xor ah, ah                                ; 30 e4                       ; 0xc20c5
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc20c7
    jc short 020a2h                           ; 72 d6                       ; 0xc20ca
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc20cc vgabios.c:1421
    xor dh, dh                                ; 30 f6                       ; 0xc20cf
    add dx, word [bp-01ch]                    ; 03 56 e4                    ; 0xc20d1
    cmp dx, ax                                ; 39 c2                       ; 0xc20d4
    jnbe short 020deh                         ; 77 06                       ; 0xc20d6
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc20d8
    jne short 020ffh                          ; 75 21                       ; 0xc20dc
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc20de vgabios.c:1422
    xor ah, ah                                ; 30 e4                       ; 0xc20e1
    push ax                                   ; 50                          ; 0xc20e3
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc20e4
    push ax                                   ; 50                          ; 0xc20e7
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc20e8
    xor ch, ch                                ; 30 ed                       ; 0xc20eb
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc20ed
    xor bh, bh                                ; 30 ff                       ; 0xc20f0
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc20f2
    xor dh, dh                                ; 30 f6                       ; 0xc20f5
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc20f7
    call 01ac6h                               ; e8 c9 f9                    ; 0xc20fa
    jmp short 0211eh                          ; eb 1f                       ; 0xc20fd vgabios.c:1423
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc20ff vgabios.c:1424
    push ax                                   ; 50                          ; 0xc2102
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc2103
    push ax                                   ; 50                          ; 0xc2106
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2107
    xor ch, ch                                ; 30 ed                       ; 0xc210a
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc210c
    xor bh, bh                                ; 30 ff                       ; 0xc210f
    mov dl, bl                                ; 88 da                       ; 0xc2111
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc2113
    xor dh, dh                                ; 30 f6                       ; 0xc2116
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2118
    call 01a10h                               ; e8 f2 f8                    ; 0xc211b
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc211e vgabios.c:1425
    jmp short 020c2h                          ; eb 9f                       ; 0xc2121
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2123 vgabios.c:1428
    xor ah, ah                                ; 30 e4                       ; 0xc2126
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc2128
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc212b
    xor ah, ah                                ; 30 e4                       ; 0xc212e
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc2130
    jnbe short 0219dh                         ; 77 68                       ; 0xc2133
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2135 vgabios.c:1430
    xor dh, dh                                ; 30 f6                       ; 0xc2138
    add ax, dx                                ; 01 d0                       ; 0xc213a
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc213c
    jnbe short 02145h                         ; 77 04                       ; 0xc213f
    test dl, dl                               ; 84 d2                       ; 0xc2141
    jne short 0216fh                          ; 75 2a                       ; 0xc2143
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2145 vgabios.c:1431
    xor ah, ah                                ; 30 e4                       ; 0xc2148
    push ax                                   ; 50                          ; 0xc214a
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc214b
    push ax                                   ; 50                          ; 0xc214e
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc214f
    xor ch, ch                                ; 30 ed                       ; 0xc2152
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2154
    xor bh, bh                                ; 30 ff                       ; 0xc2157
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc2159
    xor dh, dh                                ; 30 f6                       ; 0xc215c
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc215e
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc2161
    mov byte [bp-017h], ah                    ; 88 66 e9                    ; 0xc2164
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc2167
    call 01ac6h                               ; e8 59 f9                    ; 0xc216a
    jmp short 0218eh                          ; eb 1f                       ; 0xc216d vgabios.c:1432
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc216f vgabios.c:1433
    xor ah, ah                                ; 30 e4                       ; 0xc2172
    push ax                                   ; 50                          ; 0xc2174
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc2175
    push ax                                   ; 50                          ; 0xc2178
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2179
    xor ch, ch                                ; 30 ed                       ; 0xc217c
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc217e
    xor bh, bh                                ; 30 ff                       ; 0xc2181
    mov dl, bl                                ; 88 da                       ; 0xc2183
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc2185
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2188
    call 01a10h                               ; e8 82 f8                    ; 0xc218b
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc218e vgabios.c:1434
    xor ah, ah                                ; 30 e4                       ; 0xc2191
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc2193
    jc short 021ddh                           ; 72 45                       ; 0xc2196
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc2198 vgabios.c:1435
    jmp short 0212bh                          ; eb 8e                       ; 0xc219b
    jmp short 021ddh                          ; eb 3e                       ; 0xc219d
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc219f vgabios.c:1440
    jne short 021e0h                          ; 75 3b                       ; 0xc21a3
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc21a5
    jne short 021e0h                          ; 75 35                       ; 0xc21a9
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc21ab
    jne short 021e0h                          ; 75 2f                       ; 0xc21af
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc21b1
    cmp ax, di                                ; 39 f8                       ; 0xc21b4
    jne short 021e0h                          ; 75 28                       ; 0xc21b6
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc21b8
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc21bb
    jne short 021e0h                          ; 75 20                       ; 0xc21be
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc21c0 vgabios.c:1442
    xor dh, dh                                ; 30 f6                       ; 0xc21c3
    mov ax, cx                                ; 89 c8                       ; 0xc21c5
    mul dx                                    ; f7 e2                       ; 0xc21c7
    mov cx, ax                                ; 89 c1                       ; 0xc21c9
    sal cx, 003h                              ; c1 e1 03                    ; 0xc21cb
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc21ce
    xor ah, ah                                ; 30 e4                       ; 0xc21d1
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc21d3
    xor di, di                                ; 31 ff                       ; 0xc21d7
    jcxz 021ddh                               ; e3 02                       ; 0xc21d9
    rep stosb                                 ; f3 aa                       ; 0xc21db
    jmp near 022c7h                           ; e9 e7 00                    ; 0xc21dd vgabios.c:1444
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc21e0 vgabios.c:1447
    jne short 02255h                          ; 75 6f                       ; 0xc21e4
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc21e6 vgabios.c:1448
    xor ah, ah                                ; 30 e4                       ; 0xc21e9
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc21eb
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc21ee
    xor ah, ah                                ; 30 e4                       ; 0xc21f1
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc21f3
    jc short 021ddh                           ; 72 e5                       ; 0xc21f6
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc21f8 vgabios.c:1450
    xor dh, dh                                ; 30 f6                       ; 0xc21fb
    add dx, word [bp-01ch]                    ; 03 56 e4                    ; 0xc21fd
    cmp dx, ax                                ; 39 c2                       ; 0xc2200
    jnbe short 0220ah                         ; 77 06                       ; 0xc2202
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2204
    jne short 02229h                          ; 75 1f                       ; 0xc2208
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc220a vgabios.c:1451
    xor ah, ah                                ; 30 e4                       ; 0xc220d
    push ax                                   ; 50                          ; 0xc220f
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2210
    push ax                                   ; 50                          ; 0xc2213
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2214
    xor bh, bh                                ; 30 ff                       ; 0xc2217
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc2219
    xor dh, dh                                ; 30 f6                       ; 0xc221c
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc221e
    mov cx, word [bp-01eh]                    ; 8b 4e e2                    ; 0xc2221
    call 01bdbh                               ; e8 b4 f9                    ; 0xc2224
    jmp short 02250h                          ; eb 27                       ; 0xc2227 vgabios.c:1452
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2229 vgabios.c:1453
    push ax                                   ; 50                          ; 0xc222c
    push word [bp-01eh]                       ; ff 76 e2                    ; 0xc222d
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2230
    xor ch, ch                                ; 30 ed                       ; 0xc2233
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc2235
    xor bh, bh                                ; 30 ff                       ; 0xc2238
    mov dl, bl                                ; 88 da                       ; 0xc223a
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc223c
    xor dh, dh                                ; 30 f6                       ; 0xc223f
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2241
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc2244
    mov byte [bp-017h], ah                    ; 88 66 e9                    ; 0xc2247
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc224a
    call 01b5ah                               ; e8 0a f9                    ; 0xc224d
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc2250 vgabios.c:1454
    jmp short 021eeh                          ; eb 99                       ; 0xc2253
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2255 vgabios.c:1457
    xor ah, ah                                ; 30 e4                       ; 0xc2258
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc225a
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc225d
    xor ah, ah                                ; 30 e4                       ; 0xc2260
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc2262
    jnbe short 022c7h                         ; 77 60                       ; 0xc2265
    mov dl, al                                ; 88 c2                       ; 0xc2267 vgabios.c:1459
    xor dh, dh                                ; 30 f6                       ; 0xc2269
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc226b
    add ax, dx                                ; 01 d0                       ; 0xc226e
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc2270
    jnbe short 0227bh                         ; 77 06                       ; 0xc2273
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2275
    jne short 0229ah                          ; 75 1f                       ; 0xc2279
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc227b vgabios.c:1460
    xor ah, ah                                ; 30 e4                       ; 0xc227e
    push ax                                   ; 50                          ; 0xc2280
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2281
    push ax                                   ; 50                          ; 0xc2284
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2285
    xor bh, bh                                ; 30 ff                       ; 0xc2288
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc228a
    xor dh, dh                                ; 30 f6                       ; 0xc228d
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc228f
    mov cx, word [bp-01eh]                    ; 8b 4e e2                    ; 0xc2292
    call 01bdbh                               ; e8 43 f9                    ; 0xc2295
    jmp short 022b8h                          ; eb 1e                       ; 0xc2298 vgabios.c:1461
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc229a vgabios.c:1462
    xor ah, ah                                ; 30 e4                       ; 0xc229d
    push ax                                   ; 50                          ; 0xc229f
    push word [bp-01eh]                       ; ff 76 e2                    ; 0xc22a0
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc22a3
    xor ch, ch                                ; 30 ed                       ; 0xc22a6
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc22a8
    xor bh, bh                                ; 30 ff                       ; 0xc22ab
    mov dl, bl                                ; 88 da                       ; 0xc22ad
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc22af
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc22b2
    call 01b5ah                               ; e8 a2 f8                    ; 0xc22b5
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc22b8 vgabios.c:1463
    xor ah, ah                                ; 30 e4                       ; 0xc22bb
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc22bd
    jc short 022c7h                           ; 72 05                       ; 0xc22c0
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc22c2 vgabios.c:1464
    jmp short 0225dh                          ; eb 96                       ; 0xc22c5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc22c7 vgabios.c:1475
    pop di                                    ; 5f                          ; 0xc22ca
    pop si                                    ; 5e                          ; 0xc22cb
    pop bp                                    ; 5d                          ; 0xc22cc
    retn 00008h                               ; c2 08 00                    ; 0xc22cd
  ; disGetNextSymbol 0xc22d0 LB 0x2273 -> off=0x0 cb=0000000000000111 uValue=00000000000c22d0 'write_gfx_char_pl4'
write_gfx_char_pl4:                          ; 0xc22d0 LB 0x111
    push bp                                   ; 55                          ; 0xc22d0 vgabios.c:1478
    mov bp, sp                                ; 89 e5                       ; 0xc22d1
    push si                                   ; 56                          ; 0xc22d3
    push di                                   ; 57                          ; 0xc22d4
    sub sp, strict byte 0000eh                ; 83 ec 0e                    ; 0xc22d5
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc22d8
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc22db
    mov ch, bl                                ; 88 dd                       ; 0xc22de
    mov al, cl                                ; 88 c8                       ; 0xc22e0
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc22e2 vgabios.c:67
    xor dx, dx                                ; 31 d2                       ; 0xc22e5
    mov es, dx                                ; 8e c2                       ; 0xc22e7
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc22e9
    mov bx, word [es:bx+002h]                 ; 26 8b 5f 02                 ; 0xc22ec
    mov word [bp-012h], dx                    ; 89 56 ee                    ; 0xc22f0 vgabios.c:68
    mov word [bp-00ch], bx                    ; 89 5e f4                    ; 0xc22f3
    xor ah, ah                                ; 30 e4                       ; 0xc22f6 vgabios.c:1487
    mov bl, byte [bp+006h]                    ; 8a 5e 06                    ; 0xc22f8
    xor bh, bh                                ; 30 ff                       ; 0xc22fb
    imul bx                                   ; f7 eb                       ; 0xc22fd
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc22ff
    xor dh, dh                                ; 30 f6                       ; 0xc2302
    imul dx                                   ; f7 ea                       ; 0xc2304
    mov si, ax                                ; 89 c6                       ; 0xc2306
    mov al, ch                                ; 88 e8                       ; 0xc2308
    xor ah, ah                                ; 30 e4                       ; 0xc230a
    add si, ax                                ; 01 c6                       ; 0xc230c
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc230e vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2311
    mov es, ax                                ; 8e c0                       ; 0xc2314
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc2316
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc2319 vgabios.c:58
    xor dh, dh                                ; 30 f6                       ; 0xc231c
    mul dx                                    ; f7 e2                       ; 0xc231e
    add si, ax                                ; 01 c6                       ; 0xc2320
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2322 vgabios.c:1489
    xor ah, ah                                ; 30 e4                       ; 0xc2325
    imul bx                                   ; f7 eb                       ; 0xc2327
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc2329
    mov ax, 00f02h                            ; b8 02 0f                    ; 0xc232c vgabios.c:1490
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc232f
    out DX, ax                                ; ef                          ; 0xc2332
    mov ax, 00205h                            ; b8 05 02                    ; 0xc2333 vgabios.c:1491
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2336
    out DX, ax                                ; ef                          ; 0xc2339
    test byte [bp-008h], 080h                 ; f6 46 f8 80                 ; 0xc233a vgabios.c:1492
    je short 02346h                           ; 74 06                       ; 0xc233e
    mov ax, 01803h                            ; b8 03 18                    ; 0xc2340 vgabios.c:1494
    out DX, ax                                ; ef                          ; 0xc2343
    jmp short 0234ah                          ; eb 04                       ; 0xc2344 vgabios.c:1496
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc2346 vgabios.c:1498
    out DX, ax                                ; ef                          ; 0xc2349
    xor ch, ch                                ; 30 ed                       ; 0xc234a vgabios.c:1500
    cmp ch, byte [bp+006h]                    ; 3a 6e 06                    ; 0xc234c
    jnc short 023c3h                          ; 73 72                       ; 0xc234f
    mov al, ch                                ; 88 e8                       ; 0xc2351 vgabios.c:1502
    xor ah, ah                                ; 30 e4                       ; 0xc2353
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc2355
    xor bh, bh                                ; 30 ff                       ; 0xc2358
    imul bx                                   ; f7 eb                       ; 0xc235a
    mov bx, si                                ; 89 f3                       ; 0xc235c
    add bx, ax                                ; 01 c3                       ; 0xc235e
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc2360 vgabios.c:1503
    jmp short 02378h                          ; eb 12                       ; 0xc2364
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2366 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2369
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc236b
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc236f vgabios.c:1516
    cmp byte [bp-006h], 008h                  ; 80 7e fa 08                 ; 0xc2372
    jnc short 023c5h                          ; 73 4d                       ; 0xc2376
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc2378
    mov ax, 00080h                            ; b8 80 00                    ; 0xc237b
    sar ax, CL                                ; d3 f8                       ; 0xc237e
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc2380
    mov byte [bp-00dh], 000h                  ; c6 46 f3 00                 ; 0xc2383
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2387
    sal ax, 008h                              ; c1 e0 08                    ; 0xc238a
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc238d
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc238f
    out DX, ax                                ; ef                          ; 0xc2392
    mov dx, bx                                ; 89 da                       ; 0xc2393
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2395
    call 03852h                               ; e8 b7 14                    ; 0xc2398
    mov al, ch                                ; 88 e8                       ; 0xc239b
    xor ah, ah                                ; 30 e4                       ; 0xc239d
    add ax, word [bp-010h]                    ; 03 46 f0                    ; 0xc239f
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc23a2
    mov di, word [bp-012h]                    ; 8b 7e ee                    ; 0xc23a5
    add di, ax                                ; 01 c7                       ; 0xc23a8
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc23aa
    xor ah, ah                                ; 30 e4                       ; 0xc23ad
    test word [bp-00eh], ax                   ; 85 46 f2                    ; 0xc23af
    je short 02366h                           ; 74 b2                       ; 0xc23b2
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc23b4
    and AL, strict byte 00fh                  ; 24 0f                       ; 0xc23b7
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc23b9
    mov es, dx                                ; 8e c2                       ; 0xc23bc
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc23be
    jmp short 0236fh                          ; eb ac                       ; 0xc23c1
    jmp short 023c9h                          ; eb 04                       ; 0xc23c3
    db  0feh, 0c5h
    ; inc ch                                    ; fe c5                     ; 0xc23c5 vgabios.c:1517
    jmp short 0234ch                          ; eb 83                       ; 0xc23c7
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc23c9 vgabios.c:1518
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc23cc
    out DX, ax                                ; ef                          ; 0xc23cf
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc23d0 vgabios.c:1519
    out DX, ax                                ; ef                          ; 0xc23d3
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc23d4 vgabios.c:1520
    out DX, ax                                ; ef                          ; 0xc23d7
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc23d8 vgabios.c:1521
    pop di                                    ; 5f                          ; 0xc23db
    pop si                                    ; 5e                          ; 0xc23dc
    pop bp                                    ; 5d                          ; 0xc23dd
    retn 00006h                               ; c2 06 00                    ; 0xc23de
  ; disGetNextSymbol 0xc23e1 LB 0x2162 -> off=0x0 cb=0000000000000112 uValue=00000000000c23e1 'write_gfx_char_cga'
write_gfx_char_cga:                          ; 0xc23e1 LB 0x112
    push si                                   ; 56                          ; 0xc23e1 vgabios.c:1524
    push di                                   ; 57                          ; 0xc23e2
    enter 0000ch, 000h                        ; c8 0c 00 00                 ; 0xc23e3
    mov bh, al                                ; 88 c7                       ; 0xc23e7
    mov ch, dl                                ; 88 d5                       ; 0xc23e9
    mov al, bl                                ; 88 d8                       ; 0xc23eb
    mov di, 0556ah                            ; bf 6a 55                    ; 0xc23ed vgabios.c:1531
    xor ah, ah                                ; 30 e4                       ; 0xc23f0 vgabios.c:1532
    mov dl, byte [bp+00ah]                    ; 8a 56 0a                    ; 0xc23f2
    xor dh, dh                                ; 30 f6                       ; 0xc23f5
    imul dx                                   ; f7 ea                       ; 0xc23f7
    mov dl, cl                                ; 88 ca                       ; 0xc23f9
    xor dh, dh                                ; 30 f6                       ; 0xc23fb
    imul dx, dx, 00140h                       ; 69 d2 40 01                 ; 0xc23fd
    add ax, dx                                ; 01 d0                       ; 0xc2401
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc2403
    mov al, bh                                ; 88 f8                       ; 0xc2406 vgabios.c:1533
    xor ah, ah                                ; 30 e4                       ; 0xc2408
    sal ax, 003h                              ; c1 e0 03                    ; 0xc240a
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc240d
    xor ah, ah                                ; 30 e4                       ; 0xc2410 vgabios.c:1534
    jmp near 02431h                           ; e9 1c 00                    ; 0xc2412
    mov dl, ah                                ; 88 e2                       ; 0xc2415 vgabios.c:1549
    xor dh, dh                                ; 30 f6                       ; 0xc2417
    add dx, word [bp-00ch]                    ; 03 56 f4                    ; 0xc2419
    mov si, di                                ; 89 fe                       ; 0xc241c
    add si, dx                                ; 01 d6                       ; 0xc241e
    mov al, byte [si]                         ; 8a 04                       ; 0xc2420
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc2422 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc2425
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2427
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc242a vgabios.c:1553
    cmp ah, 008h                              ; 80 fc 08                    ; 0xc242c
    jnc short 02488h                          ; 73 57                       ; 0xc242f
    mov dl, ah                                ; 88 e2                       ; 0xc2431
    xor dh, dh                                ; 30 f6                       ; 0xc2433
    sar dx, 1                                 ; d1 fa                       ; 0xc2435
    imul dx, dx, strict byte 00050h           ; 6b d2 50                    ; 0xc2437
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc243a
    add bx, dx                                ; 01 d3                       ; 0xc243d
    test ah, 001h                             ; f6 c4 01                    ; 0xc243f
    je short 02447h                           ; 74 03                       ; 0xc2442
    add bh, 020h                              ; 80 c7 20                    ; 0xc2444
    mov byte [bp-002h], 080h                  ; c6 46 fe 80                 ; 0xc2447
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc244b
    jne short 0246dh                          ; 75 1c                       ; 0xc244f
    test ch, 080h                             ; f6 c5 80                    ; 0xc2451
    je short 02415h                           ; 74 bf                       ; 0xc2454
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc2456
    mov es, dx                                ; 8e c2                       ; 0xc2459
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc245b
    mov dl, ah                                ; 88 e2                       ; 0xc245e
    xor dh, dh                                ; 30 f6                       ; 0xc2460
    add dx, word [bp-00ch]                    ; 03 56 f4                    ; 0xc2462
    mov si, di                                ; 89 fe                       ; 0xc2465
    add si, dx                                ; 01 d6                       ; 0xc2467
    xor al, byte [si]                         ; 32 04                       ; 0xc2469
    jmp short 02422h                          ; eb b5                       ; 0xc246b
    cmp byte [bp-002h], 000h                  ; 80 7e fe 00                 ; 0xc246d vgabios.c:1555
    jbe short 0242ah                          ; 76 b7                       ; 0xc2471
    test ch, 080h                             ; f6 c5 80                    ; 0xc2473 vgabios.c:1557
    je short 02482h                           ; 74 0a                       ; 0xc2476
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc2478 vgabios.c:47
    mov es, dx                                ; 8e c2                       ; 0xc247b
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc247d
    jmp short 02484h                          ; eb 02                       ; 0xc2480 vgabios.c:1561
    xor al, al                                ; 30 c0                       ; 0xc2482 vgabios.c:1563
    xor dl, dl                                ; 30 d2                       ; 0xc2484 vgabios.c:1565
    jmp short 0248fh                          ; eb 07                       ; 0xc2486
    jmp short 024edh                          ; eb 63                       ; 0xc2488
    cmp dl, 004h                              ; 80 fa 04                    ; 0xc248a
    jnc short 024e2h                          ; 73 53                       ; 0xc248d
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc248f vgabios.c:1567
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc2492
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc2496
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc2499
    add si, di                                ; 01 fe                       ; 0xc249c
    mov dh, byte [si]                         ; 8a 34                       ; 0xc249e
    mov byte [bp-006h], dh                    ; 88 76 fa                    ; 0xc24a0
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc24a3
    mov dh, byte [bp-002h]                    ; 8a 76 fe                    ; 0xc24a7
    mov byte [bp-00ah], dh                    ; 88 76 f6                    ; 0xc24aa
    mov byte [bp-009h], 000h                  ; c6 46 f7 00                 ; 0xc24ad
    mov si, word [bp-006h]                    ; 8b 76 fa                    ; 0xc24b1
    test word [bp-00ah], si                   ; 85 76 f6                    ; 0xc24b4
    je short 024dbh                           ; 74 22                       ; 0xc24b7
    mov DH, strict byte 003h                  ; b6 03                       ; 0xc24b9 vgabios.c:1568
    sub dh, dl                                ; 28 d6                       ; 0xc24bb
    mov cl, ch                                ; 88 e9                       ; 0xc24bd
    and cl, 003h                              ; 80 e1 03                    ; 0xc24bf
    mov byte [bp-004h], cl                    ; 88 4e fc                    ; 0xc24c2
    mov cl, dh                                ; 88 f1                       ; 0xc24c5
    add cl, dh                                ; 00 f1                       ; 0xc24c7
    mov dh, byte [bp-004h]                    ; 8a 76 fc                    ; 0xc24c9
    sal dh, CL                                ; d2 e6                       ; 0xc24cc
    mov cl, dh                                ; 88 f1                       ; 0xc24ce
    test ch, 080h                             ; f6 c5 80                    ; 0xc24d0 vgabios.c:1569
    je short 024d9h                           ; 74 04                       ; 0xc24d3
    xor al, dh                                ; 30 f0                       ; 0xc24d5 vgabios.c:1571
    jmp short 024dbh                          ; eb 02                       ; 0xc24d7 vgabios.c:1573
    or al, dh                                 ; 08 f0                       ; 0xc24d9 vgabios.c:1575
    shr byte [bp-002h], 1                     ; d0 6e fe                    ; 0xc24db vgabios.c:1578
    db  0feh, 0c2h
    ; inc dl                                    ; fe c2                     ; 0xc24de vgabios.c:1579
    jmp short 0248ah                          ; eb a8                       ; 0xc24e0
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc24e2 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc24e5
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc24e7
    inc bx                                    ; 43                          ; 0xc24ea vgabios.c:1581
    jmp short 0246dh                          ; eb 80                       ; 0xc24eb vgabios.c:1582
    leave                                     ; c9                          ; 0xc24ed vgabios.c:1585
    pop di                                    ; 5f                          ; 0xc24ee
    pop si                                    ; 5e                          ; 0xc24ef
    retn 00004h                               ; c2 04 00                    ; 0xc24f0
  ; disGetNextSymbol 0xc24f3 LB 0x2050 -> off=0x0 cb=000000000000009b uValue=00000000000c24f3 'write_gfx_char_lin'
write_gfx_char_lin:                          ; 0xc24f3 LB 0x9b
    push si                                   ; 56                          ; 0xc24f3 vgabios.c:1588
    push di                                   ; 57                          ; 0xc24f4
    enter 00008h, 000h                        ; c8 08 00 00                 ; 0xc24f5
    mov bh, al                                ; 88 c7                       ; 0xc24f9
    mov ch, dl                                ; 88 d5                       ; 0xc24fb
    mov al, cl                                ; 88 c8                       ; 0xc24fd
    mov di, 0556ah                            ; bf 6a 55                    ; 0xc24ff vgabios.c:1595
    xor ah, ah                                ; 30 e4                       ; 0xc2502 vgabios.c:1596
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc2504
    xor dh, dh                                ; 30 f6                       ; 0xc2507
    imul dx                                   ; f7 ea                       ; 0xc2509
    mov dx, ax                                ; 89 c2                       ; 0xc250b
    sal dx, 006h                              ; c1 e2 06                    ; 0xc250d
    mov al, bl                                ; 88 d8                       ; 0xc2510
    xor ah, ah                                ; 30 e4                       ; 0xc2512
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2514
    add ax, dx                                ; 01 d0                       ; 0xc2517
    mov word [bp-002h], ax                    ; 89 46 fe                    ; 0xc2519
    mov al, bh                                ; 88 f8                       ; 0xc251c vgabios.c:1597
    xor ah, ah                                ; 30 e4                       ; 0xc251e
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2520
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc2523
    xor bl, bl                                ; 30 db                       ; 0xc2526 vgabios.c:1598
    jmp short 0256ch                          ; eb 42                       ; 0xc2528
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc252a vgabios.c:1602
    jnc short 02565h                          ; 73 37                       ; 0xc252c
    xor bh, bh                                ; 30 ff                       ; 0xc252e vgabios.c:1604
    mov dl, bl                                ; 88 da                       ; 0xc2530 vgabios.c:1605
    xor dh, dh                                ; 30 f6                       ; 0xc2532
    add dx, word [bp-006h]                    ; 03 56 fa                    ; 0xc2534
    mov si, di                                ; 89 fe                       ; 0xc2537
    add si, dx                                ; 01 d6                       ; 0xc2539
    mov dl, byte [si]                         ; 8a 14                       ; 0xc253b
    mov byte [bp-004h], dl                    ; 88 56 fc                    ; 0xc253d
    mov byte [bp-003h], bh                    ; 88 7e fd                    ; 0xc2540
    mov dl, ah                                ; 88 e2                       ; 0xc2543
    xor dh, dh                                ; 30 f6                       ; 0xc2545
    test word [bp-004h], dx                   ; 85 56 fc                    ; 0xc2547
    je short 0254eh                           ; 74 02                       ; 0xc254a
    mov bh, ch                                ; 88 ef                       ; 0xc254c vgabios.c:1607
    mov dl, al                                ; 88 c2                       ; 0xc254e vgabios.c:1609
    xor dh, dh                                ; 30 f6                       ; 0xc2550
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc2552
    add si, dx                                ; 01 d6                       ; 0xc2555
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc2557 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc255a
    mov byte [es:si], bh                      ; 26 88 3c                    ; 0xc255c
    shr ah, 1                                 ; d0 ec                       ; 0xc255f vgabios.c:1610
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc2561 vgabios.c:1611
    jmp short 0252ah                          ; eb c5                       ; 0xc2563
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc2565 vgabios.c:1612
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc2567
    jnc short 02588h                          ; 73 1c                       ; 0xc256a
    mov al, bl                                ; 88 d8                       ; 0xc256c
    xor ah, ah                                ; 30 e4                       ; 0xc256e
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc2570
    xor dh, dh                                ; 30 f6                       ; 0xc2573
    imul dx                                   ; f7 ea                       ; 0xc2575
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2577
    mov dx, word [bp-002h]                    ; 8b 56 fe                    ; 0xc257a
    add dx, ax                                ; 01 c2                       ; 0xc257d
    mov word [bp-008h], dx                    ; 89 56 f8                    ; 0xc257f
    mov AH, strict byte 080h                  ; b4 80                       ; 0xc2582
    xor al, al                                ; 30 c0                       ; 0xc2584
    jmp short 0252eh                          ; eb a6                       ; 0xc2586
    leave                                     ; c9                          ; 0xc2588 vgabios.c:1613
    pop di                                    ; 5f                          ; 0xc2589
    pop si                                    ; 5e                          ; 0xc258a
    retn 00002h                               ; c2 02 00                    ; 0xc258b
  ; disGetNextSymbol 0xc258e LB 0x1fb5 -> off=0x0 cb=0000000000000187 uValue=00000000000c258e 'biosfn_write_char_attr'
biosfn_write_char_attr:                      ; 0xc258e LB 0x187
    push bp                                   ; 55                          ; 0xc258e vgabios.c:1616
    mov bp, sp                                ; 89 e5                       ; 0xc258f
    push si                                   ; 56                          ; 0xc2591
    push di                                   ; 57                          ; 0xc2592
    sub sp, strict byte 0001ch                ; 83 ec 1c                    ; 0xc2593
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2596
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc2599
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc259c
    mov si, cx                                ; 89 ce                       ; 0xc259f
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc25a1 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc25a4
    mov es, ax                                ; 8e c0                       ; 0xc25a7
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc25a9
    xor ah, ah                                ; 30 e4                       ; 0xc25ac vgabios.c:1624
    call 0382ah                               ; e8 79 12                    ; 0xc25ae
    mov cl, al                                ; 88 c1                       ; 0xc25b1
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc25b3
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc25b6 vgabios.c:1625
    jne short 025bdh                          ; 75 03                       ; 0xc25b8
    jmp near 0270eh                           ; e9 51 01                    ; 0xc25ba
    mov al, dl                                ; 88 d0                       ; 0xc25bd vgabios.c:1628
    xor ah, ah                                ; 30 e4                       ; 0xc25bf
    lea bx, [bp-01eh]                         ; 8d 5e e2                    ; 0xc25c1
    lea dx, [bp-020h]                         ; 8d 56 e0                    ; 0xc25c4
    call 00a96h                               ; e8 cc e4                    ; 0xc25c7
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc25ca vgabios.c:1629
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc25cd
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc25d0
    xor al, al                                ; 30 c0                       ; 0xc25d3
    shr ax, 008h                              ; c1 e8 08                    ; 0xc25d5
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc25d8
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc25db
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc25de
    mov bx, 00084h                            ; bb 84 00                    ; 0xc25e1 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc25e4
    mov es, ax                                ; 8e c0                       ; 0xc25e7
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc25e9
    xor ah, ah                                ; 30 e4                       ; 0xc25ec vgabios.c:48
    mov dx, ax                                ; 89 c2                       ; 0xc25ee
    inc dx                                    ; 42                          ; 0xc25f0
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc25f1 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc25f4
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc25f7
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc25fa vgabios.c:58
    mov bl, cl                                ; 88 cb                       ; 0xc25fd vgabios.c:1635
    xor bh, bh                                ; 30 ff                       ; 0xc25ff
    mov di, bx                                ; 89 df                       ; 0xc2601
    sal di, 003h                              ; c1 e7 03                    ; 0xc2603
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc2606
    jne short 02656h                          ; 75 49                       ; 0xc260b
    mul dx                                    ; f7 e2                       ; 0xc260d vgabios.c:1638
    add ax, ax                                ; 01 c0                       ; 0xc260f
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2611
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc2613
    xor dh, dh                                ; 30 f6                       ; 0xc2616
    inc ax                                    ; 40                          ; 0xc2618
    mul dx                                    ; f7 e2                       ; 0xc2619
    mov bx, ax                                ; 89 c3                       ; 0xc261b
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc261d
    xor ah, ah                                ; 30 e4                       ; 0xc2620
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc2622
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2625
    xor dh, dh                                ; 30 f6                       ; 0xc2628
    add ax, dx                                ; 01 d0                       ; 0xc262a
    add ax, ax                                ; 01 c0                       ; 0xc262c
    mov dx, bx                                ; 89 da                       ; 0xc262e
    add dx, ax                                ; 01 c2                       ; 0xc2630
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2632 vgabios.c:1640
    xor ah, ah                                ; 30 e4                       ; 0xc2635
    mov bx, ax                                ; 89 c3                       ; 0xc2637
    sal bx, 008h                              ; c1 e3 08                    ; 0xc2639
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc263c
    add bx, ax                                ; 01 c3                       ; 0xc263f
    mov word [bp-020h], bx                    ; 89 5e e0                    ; 0xc2641
    mov ax, word [bp-020h]                    ; 8b 46 e0                    ; 0xc2644 vgabios.c:1641
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc2647
    mov cx, si                                ; 89 f1                       ; 0xc264b
    mov di, dx                                ; 89 d7                       ; 0xc264d
    jcxz 02653h                               ; e3 02                       ; 0xc264f
    rep stosw                                 ; f3 ab                       ; 0xc2651
    jmp near 0270eh                           ; e9 b8 00                    ; 0xc2653 vgabios.c:1643
    mov bl, byte [bx+0482ch]                  ; 8a 9f 2c 48                 ; 0xc2656 vgabios.c:1646
    sal bx, 006h                              ; c1 e3 06                    ; 0xc265a
    mov al, byte [bx+04842h]                  ; 8a 87 42 48                 ; 0xc265d
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc2661
    mov al, byte [di+047afh]                  ; 8a 85 af 47                 ; 0xc2664 vgabios.c:1647
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc2668
    dec si                                    ; 4e                          ; 0xc266b vgabios.c:1648
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc266c
    je short 026c1h                           ; 74 50                       ; 0xc266f
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc2671 vgabios.c:1650
    xor bh, bh                                ; 30 ff                       ; 0xc2674
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2676
    mov bl, byte [bx+047aeh]                  ; 8a 9f ae 47                 ; 0xc2679
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc267d
    jc short 02691h                           ; 72 0f                       ; 0xc2680
    jbe short 02698h                          ; 76 14                       ; 0xc2682
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2684
    je short 026edh                           ; 74 64                       ; 0xc2687
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2689
    je short 0269ch                           ; 74 0e                       ; 0xc268c
    jmp near 02708h                           ; e9 77 00                    ; 0xc268e
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2691
    je short 026c3h                           ; 74 2d                       ; 0xc2694
    jmp short 02708h                          ; eb 70                       ; 0xc2696
    or byte [bp-006h], 001h                   ; 80 4e fa 01                 ; 0xc2698 vgabios.c:1653
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc269c vgabios.c:1655
    xor ah, ah                                ; 30 e4                       ; 0xc269f
    push ax                                   ; 50                          ; 0xc26a1
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc26a2
    push ax                                   ; 50                          ; 0xc26a5
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc26a6
    push ax                                   ; 50                          ; 0xc26a9
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc26aa
    xor ch, ch                                ; 30 ed                       ; 0xc26ad
    mov bl, byte [bp-008h]                    ; 8a 5e f8                    ; 0xc26af
    xor bh, bh                                ; 30 ff                       ; 0xc26b2
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc26b4
    xor dh, dh                                ; 30 f6                       ; 0xc26b7
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc26b9
    call 022d0h                               ; e8 11 fc                    ; 0xc26bc
    jmp short 02708h                          ; eb 47                       ; 0xc26bf vgabios.c:1656
    jmp short 0270eh                          ; eb 4b                       ; 0xc26c1
    mov al, byte [bp-014h]                    ; 8a 46 ec                    ; 0xc26c3 vgabios.c:1658
    xor ah, ah                                ; 30 e4                       ; 0xc26c6
    push ax                                   ; 50                          ; 0xc26c8
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc26c9
    push ax                                   ; 50                          ; 0xc26cc
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc26cd
    xor ch, ch                                ; 30 ed                       ; 0xc26d0
    mov bl, byte [bp-008h]                    ; 8a 5e f8                    ; 0xc26d2
    xor bh, bh                                ; 30 ff                       ; 0xc26d5
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc26d7
    xor dh, dh                                ; 30 f6                       ; 0xc26da
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc26dc
    mov byte [bp-016h], al                    ; 88 46 ea                    ; 0xc26df
    mov byte [bp-015h], ah                    ; 88 66 eb                    ; 0xc26e2
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc26e5
    call 023e1h                               ; e8 f6 fc                    ; 0xc26e8
    jmp short 02708h                          ; eb 1b                       ; 0xc26eb vgabios.c:1659
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc26ed vgabios.c:1661
    xor ah, ah                                ; 30 e4                       ; 0xc26f0
    push ax                                   ; 50                          ; 0xc26f2
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc26f3
    xor ch, ch                                ; 30 ed                       ; 0xc26f6
    mov bl, byte [bp-008h]                    ; 8a 5e f8                    ; 0xc26f8
    xor bh, bh                                ; 30 ff                       ; 0xc26fb
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc26fd
    xor dh, dh                                ; 30 f6                       ; 0xc2700
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2702
    call 024f3h                               ; e8 eb fd                    ; 0xc2705
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc2708 vgabios.c:1668
    jmp near 0266bh                           ; e9 5d ff                    ; 0xc270b vgabios.c:1669
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc270e vgabios.c:1671
    pop di                                    ; 5f                          ; 0xc2711
    pop si                                    ; 5e                          ; 0xc2712
    pop bp                                    ; 5d                          ; 0xc2713
    retn                                      ; c3                          ; 0xc2714
  ; disGetNextSymbol 0xc2715 LB 0x1e2e -> off=0x0 cb=0000000000000181 uValue=00000000000c2715 'biosfn_write_char_only'
biosfn_write_char_only:                      ; 0xc2715 LB 0x181
    push bp                                   ; 55                          ; 0xc2715 vgabios.c:1674
    mov bp, sp                                ; 89 e5                       ; 0xc2716
    push si                                   ; 56                          ; 0xc2718
    push di                                   ; 57                          ; 0xc2719
    sub sp, strict byte 0001ch                ; 83 ec 1c                    ; 0xc271a
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc271d
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc2720
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc2723
    mov si, cx                                ; 89 ce                       ; 0xc2726
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc2728 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc272b
    mov es, ax                                ; 8e c0                       ; 0xc272e
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2730
    xor ah, ah                                ; 30 e4                       ; 0xc2733 vgabios.c:1682
    call 0382ah                               ; e8 f2 10                    ; 0xc2735
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc2738
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc273b
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc273e vgabios.c:1683
    jne short 02745h                          ; 75 03                       ; 0xc2740
    jmp near 0288fh                           ; e9 4a 01                    ; 0xc2742
    mov al, dl                                ; 88 d0                       ; 0xc2745 vgabios.c:1686
    xor ah, ah                                ; 30 e4                       ; 0xc2747
    lea bx, [bp-01eh]                         ; 8d 5e e2                    ; 0xc2749
    lea dx, [bp-020h]                         ; 8d 56 e0                    ; 0xc274c
    call 00a96h                               ; e8 44 e3                    ; 0xc274f
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc2752 vgabios.c:1687
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2755
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc2758
    xor al, al                                ; 30 c0                       ; 0xc275b
    shr ax, 008h                              ; c1 e8 08                    ; 0xc275d
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc2760
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc2763
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2766
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2769 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc276c
    mov es, ax                                ; 8e c0                       ; 0xc276f
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2771
    xor ah, ah                                ; 30 e4                       ; 0xc2774 vgabios.c:48
    mov dx, ax                                ; 89 c2                       ; 0xc2776
    inc dx                                    ; 42                          ; 0xc2778
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2779 vgabios.c:57
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc277c
    mov word [bp-01ch], cx                    ; 89 4e e4                    ; 0xc277f vgabios.c:58
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2782 vgabios.c:1693
    mov bx, ax                                ; 89 c3                       ; 0xc2785
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2787
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc278a
    jne short 027d3h                          ; 75 42                       ; 0xc278f
    mov ax, cx                                ; 89 c8                       ; 0xc2791 vgabios.c:1696
    mul dx                                    ; f7 e2                       ; 0xc2793
    add ax, ax                                ; 01 c0                       ; 0xc2795
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2797
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc2799
    xor dh, dh                                ; 30 f6                       ; 0xc279c
    inc ax                                    ; 40                          ; 0xc279e
    mul dx                                    ; f7 e2                       ; 0xc279f
    mov bx, ax                                ; 89 c3                       ; 0xc27a1
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc27a3
    xor ah, ah                                ; 30 e4                       ; 0xc27a6
    mul cx                                    ; f7 e1                       ; 0xc27a8
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc27aa
    xor dh, dh                                ; 30 f6                       ; 0xc27ad
    add ax, dx                                ; 01 d0                       ; 0xc27af
    add ax, ax                                ; 01 c0                       ; 0xc27b1
    add bx, ax                                ; 01 c3                       ; 0xc27b3
    dec si                                    ; 4e                          ; 0xc27b5 vgabios.c:1698
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc27b6
    je short 02742h                           ; 74 87                       ; 0xc27b9
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc27bb vgabios.c:1699
    xor ah, ah                                ; 30 e4                       ; 0xc27be
    mov di, ax                                ; 89 c7                       ; 0xc27c0
    sal di, 003h                              ; c1 e7 03                    ; 0xc27c2
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc27c5 vgabios.c:50
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc27c9 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc27cc
    inc bx                                    ; 43                          ; 0xc27cf vgabios.c:1700
    inc bx                                    ; 43                          ; 0xc27d0
    jmp short 027b5h                          ; eb e2                       ; 0xc27d1 vgabios.c:1701
    mov di, ax                                ; 89 c7                       ; 0xc27d3 vgabios.c:1706
    mov al, byte [di+0482ch]                  ; 8a 85 2c 48                 ; 0xc27d5
    mov di, ax                                ; 89 c7                       ; 0xc27d9
    sal di, 006h                              ; c1 e7 06                    ; 0xc27db
    mov al, byte [di+04842h]                  ; 8a 85 42 48                 ; 0xc27de
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc27e2
    mov al, byte [bx+047afh]                  ; 8a 87 af 47                 ; 0xc27e5 vgabios.c:1707
    mov byte [bp-016h], al                    ; 88 46 ea                    ; 0xc27e9
    dec si                                    ; 4e                          ; 0xc27ec vgabios.c:1708
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc27ed
    je short 02842h                           ; 74 50                       ; 0xc27f0
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc27f2 vgabios.c:1710
    xor bh, bh                                ; 30 ff                       ; 0xc27f5
    sal bx, 003h                              ; c1 e3 03                    ; 0xc27f7
    mov bl, byte [bx+047aeh]                  ; 8a 9f ae 47                 ; 0xc27fa
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc27fe
    jc short 02812h                           ; 72 0f                       ; 0xc2801
    jbe short 02819h                          ; 76 14                       ; 0xc2803
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2805
    je short 0286eh                           ; 74 64                       ; 0xc2808
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc280a
    je short 0281dh                           ; 74 0e                       ; 0xc280d
    jmp near 02889h                           ; e9 77 00                    ; 0xc280f
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2812
    je short 02844h                           ; 74 2d                       ; 0xc2815
    jmp short 02889h                          ; eb 70                       ; 0xc2817
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc2819 vgabios.c:1713
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc281d vgabios.c:1715
    xor ah, ah                                ; 30 e4                       ; 0xc2820
    push ax                                   ; 50                          ; 0xc2822
    mov al, byte [bp-014h]                    ; 8a 46 ec                    ; 0xc2823
    push ax                                   ; 50                          ; 0xc2826
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc2827
    push ax                                   ; 50                          ; 0xc282a
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc282b
    xor ch, ch                                ; 30 ed                       ; 0xc282e
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2830
    xor bh, bh                                ; 30 ff                       ; 0xc2833
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2835
    xor dh, dh                                ; 30 f6                       ; 0xc2838
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc283a
    call 022d0h                               ; e8 90 fa                    ; 0xc283d
    jmp short 02889h                          ; eb 47                       ; 0xc2840 vgabios.c:1716
    jmp short 0288fh                          ; eb 4b                       ; 0xc2842
    mov al, byte [bp-016h]                    ; 8a 46 ea                    ; 0xc2844 vgabios.c:1718
    xor ah, ah                                ; 30 e4                       ; 0xc2847
    push ax                                   ; 50                          ; 0xc2849
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc284a
    push ax                                   ; 50                          ; 0xc284d
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc284e
    xor ch, ch                                ; 30 ed                       ; 0xc2851
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2853
    xor bh, bh                                ; 30 ff                       ; 0xc2856
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2858
    xor dh, dh                                ; 30 f6                       ; 0xc285b
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc285d
    mov byte [bp-01ah], al                    ; 88 46 e6                    ; 0xc2860
    mov byte [bp-019h], ah                    ; 88 66 e7                    ; 0xc2863
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc2866
    call 023e1h                               ; e8 75 fb                    ; 0xc2869
    jmp short 02889h                          ; eb 1b                       ; 0xc286c vgabios.c:1719
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc286e vgabios.c:1721
    xor ah, ah                                ; 30 e4                       ; 0xc2871
    push ax                                   ; 50                          ; 0xc2873
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc2874
    xor ch, ch                                ; 30 ed                       ; 0xc2877
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2879
    xor bh, bh                                ; 30 ff                       ; 0xc287c
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc287e
    xor dh, dh                                ; 30 f6                       ; 0xc2881
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2883
    call 024f3h                               ; e8 6a fc                    ; 0xc2886
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc2889 vgabios.c:1728
    jmp near 027ech                           ; e9 5d ff                    ; 0xc288c vgabios.c:1729
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc288f vgabios.c:1731
    pop di                                    ; 5f                          ; 0xc2892
    pop si                                    ; 5e                          ; 0xc2893
    pop bp                                    ; 5d                          ; 0xc2894
    retn                                      ; c3                          ; 0xc2895
  ; disGetNextSymbol 0xc2896 LB 0x1cad -> off=0x0 cb=0000000000000173 uValue=00000000000c2896 'biosfn_write_pixel'
biosfn_write_pixel:                          ; 0xc2896 LB 0x173
    push bp                                   ; 55                          ; 0xc2896 vgabios.c:1734
    mov bp, sp                                ; 89 e5                       ; 0xc2897
    push si                                   ; 56                          ; 0xc2899
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc289a
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc289d
    mov byte [bp-004h], dl                    ; 88 56 fc                    ; 0xc28a0
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc28a3
    mov dx, cx                                ; 89 ca                       ; 0xc28a6
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc28a8 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc28ab
    mov es, ax                                ; 8e c0                       ; 0xc28ae
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc28b0
    xor ah, ah                                ; 30 e4                       ; 0xc28b3 vgabios.c:1741
    call 0382ah                               ; e8 72 0f                    ; 0xc28b5
    mov cl, al                                ; 88 c1                       ; 0xc28b8
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc28ba vgabios.c:1742
    je short 028e4h                           ; 74 26                       ; 0xc28bc
    mov bl, al                                ; 88 c3                       ; 0xc28be vgabios.c:1743
    xor bh, bh                                ; 30 ff                       ; 0xc28c0
    sal bx, 003h                              ; c1 e3 03                    ; 0xc28c2
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc28c5
    je short 028e4h                           ; 74 18                       ; 0xc28ca
    mov al, byte [bx+047aeh]                  ; 8a 87 ae 47                 ; 0xc28cc vgabios.c:1745
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc28d0
    jc short 028e0h                           ; 72 0c                       ; 0xc28d2
    jbe short 028eah                          ; 76 14                       ; 0xc28d4
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc28d6
    je short 028e7h                           ; 74 0d                       ; 0xc28d8
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc28da
    je short 028eah                           ; 74 0c                       ; 0xc28dc
    jmp short 028e4h                          ; eb 04                       ; 0xc28de
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc28e0
    je short 0295bh                           ; 74 77                       ; 0xc28e2
    jmp near 02a03h                           ; e9 1c 01                    ; 0xc28e4
    jmp near 029e1h                           ; e9 f7 00                    ; 0xc28e7
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc28ea vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc28ed
    mov es, ax                                ; 8e c0                       ; 0xc28f0
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc28f2
    mov ax, dx                                ; 89 d0                       ; 0xc28f5 vgabios.c:58
    mul bx                                    ; f7 e3                       ; 0xc28f7
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc28f9
    shr bx, 003h                              ; c1 eb 03                    ; 0xc28fc
    add bx, ax                                ; 01 c3                       ; 0xc28ff
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc2901 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc2904
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2907 vgabios.c:58
    xor dh, dh                                ; 30 f6                       ; 0xc290a
    mul dx                                    ; f7 e2                       ; 0xc290c
    add bx, ax                                ; 01 c3                       ; 0xc290e
    mov cx, word [bp-008h]                    ; 8b 4e f8                    ; 0xc2910 vgabios.c:1751
    and cl, 007h                              ; 80 e1 07                    ; 0xc2913
    mov ax, 00080h                            ; b8 80 00                    ; 0xc2916
    sar ax, CL                                ; d3 f8                       ; 0xc2919
    xor ah, ah                                ; 30 e4                       ; 0xc291b vgabios.c:1752
    sal ax, 008h                              ; c1 e0 08                    ; 0xc291d
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc2920
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2922
    out DX, ax                                ; ef                          ; 0xc2925
    mov ax, 00205h                            ; b8 05 02                    ; 0xc2926 vgabios.c:1753
    out DX, ax                                ; ef                          ; 0xc2929
    mov dx, bx                                ; 89 da                       ; 0xc292a vgabios.c:1754
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc292c
    call 03852h                               ; e8 20 0f                    ; 0xc292f
    test byte [bp-004h], 080h                 ; f6 46 fc 80                 ; 0xc2932 vgabios.c:1755
    je short 0293fh                           ; 74 07                       ; 0xc2936
    mov ax, 01803h                            ; b8 03 18                    ; 0xc2938 vgabios.c:1757
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc293b
    out DX, ax                                ; ef                          ; 0xc293e
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc293f vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2942
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2944
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2947
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc294a vgabios.c:1760
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc294d
    out DX, ax                                ; ef                          ; 0xc2950
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2951 vgabios.c:1761
    out DX, ax                                ; ef                          ; 0xc2954
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc2955 vgabios.c:1762
    out DX, ax                                ; ef                          ; 0xc2958
    jmp short 028e4h                          ; eb 89                       ; 0xc2959 vgabios.c:1763
    mov ax, dx                                ; 89 d0                       ; 0xc295b vgabios.c:1765
    shr ax, 1                                 ; d1 e8                       ; 0xc295d
    imul ax, ax, strict byte 00050h           ; 6b c0 50                    ; 0xc295f
    cmp byte [bx+047afh], 002h                ; 80 bf af 47 02              ; 0xc2962
    jne short 02971h                          ; 75 08                       ; 0xc2967
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2969 vgabios.c:1767
    shr bx, 002h                              ; c1 eb 02                    ; 0xc296c
    jmp short 02977h                          ; eb 06                       ; 0xc296f vgabios.c:1769
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2971 vgabios.c:1771
    shr bx, 003h                              ; c1 eb 03                    ; 0xc2974
    add bx, ax                                ; 01 c3                       ; 0xc2977
    test dl, 001h                             ; f6 c2 01                    ; 0xc2979 vgabios.c:1773
    je short 02981h                           ; 74 03                       ; 0xc297c
    add bh, 020h                              ; 80 c7 20                    ; 0xc297e
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc2981 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc2984
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc2986
    mov al, cl                                ; 88 c8                       ; 0xc2989 vgabios.c:1775
    xor ah, ah                                ; 30 e4                       ; 0xc298b
    mov si, ax                                ; 89 c6                       ; 0xc298d
    sal si, 003h                              ; c1 e6 03                    ; 0xc298f
    cmp byte [si+047afh], 002h                ; 80 bc af 47 02              ; 0xc2992
    jne short 029b2h                          ; 75 19                       ; 0xc2997
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2999 vgabios.c:1777
    and AL, strict byte 003h                  ; 24 03                       ; 0xc299c
    mov AH, strict byte 003h                  ; b4 03                       ; 0xc299e
    sub ah, al                                ; 28 c4                       ; 0xc29a0
    mov cl, ah                                ; 88 e1                       ; 0xc29a2
    add cl, ah                                ; 00 e1                       ; 0xc29a4
    mov dh, byte [bp-004h]                    ; 8a 76 fc                    ; 0xc29a6
    and dh, 003h                              ; 80 e6 03                    ; 0xc29a9
    sal dh, CL                                ; d2 e6                       ; 0xc29ac
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc29ae vgabios.c:1778
    jmp short 029c5h                          ; eb 13                       ; 0xc29b0 vgabios.c:1780
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc29b2 vgabios.c:1782
    and AL, strict byte 007h                  ; 24 07                       ; 0xc29b5
    mov CL, strict byte 007h                  ; b1 07                       ; 0xc29b7
    sub cl, al                                ; 28 c1                       ; 0xc29b9
    mov dh, byte [bp-004h]                    ; 8a 76 fc                    ; 0xc29bb
    and dh, 001h                              ; 80 e6 01                    ; 0xc29be
    sal dh, CL                                ; d2 e6                       ; 0xc29c1
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc29c3 vgabios.c:1783
    sal al, CL                                ; d2 e0                       ; 0xc29c5
    test byte [bp-004h], 080h                 ; f6 46 fc 80                 ; 0xc29c7 vgabios.c:1785
    je short 029d1h                           ; 74 04                       ; 0xc29cb
    xor dl, dh                                ; 30 f2                       ; 0xc29cd vgabios.c:1787
    jmp short 029d7h                          ; eb 06                       ; 0xc29cf vgabios.c:1789
    not al                                    ; f6 d0                       ; 0xc29d1 vgabios.c:1791
    and dl, al                                ; 20 c2                       ; 0xc29d3
    or dl, dh                                 ; 08 f2                       ; 0xc29d5 vgabios.c:1792
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc29d7 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc29da
    mov byte [es:bx], dl                      ; 26 88 17                    ; 0xc29dc
    jmp short 02a03h                          ; eb 22                       ; 0xc29df vgabios.c:1795
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc29e1 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc29e4
    mov es, ax                                ; 8e c0                       ; 0xc29e7
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc29e9
    sal bx, 003h                              ; c1 e3 03                    ; 0xc29ec vgabios.c:58
    mov ax, dx                                ; 89 d0                       ; 0xc29ef
    mul bx                                    ; f7 e3                       ; 0xc29f1
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc29f3
    add bx, ax                                ; 01 c3                       ; 0xc29f6
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc29f8 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc29fb
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc29fd
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2a00
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2a03 vgabios.c:1805
    pop si                                    ; 5e                          ; 0xc2a06
    pop bp                                    ; 5d                          ; 0xc2a07
    retn                                      ; c3                          ; 0xc2a08
  ; disGetNextSymbol 0xc2a09 LB 0x1b3a -> off=0x0 cb=0000000000000258 uValue=00000000000c2a09 'biosfn_write_teletype'
biosfn_write_teletype:                       ; 0xc2a09 LB 0x258
    push bp                                   ; 55                          ; 0xc2a09 vgabios.c:1808
    mov bp, sp                                ; 89 e5                       ; 0xc2a0a
    push si                                   ; 56                          ; 0xc2a0c
    sub sp, strict byte 00014h                ; 83 ec 14                    ; 0xc2a0d
    mov ch, al                                ; 88 c5                       ; 0xc2a10
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc2a12
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc2a15
    cmp dl, 0ffh                              ; 80 fa ff                    ; 0xc2a18 vgabios.c:1816
    jne short 02a2bh                          ; 75 0e                       ; 0xc2a1b
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc2a1d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2a20
    mov es, ax                                ; 8e c0                       ; 0xc2a23
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2a25
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2a28 vgabios.c:48
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc2a2b vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2a2e
    mov es, ax                                ; 8e c0                       ; 0xc2a31
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2a33
    xor ah, ah                                ; 30 e4                       ; 0xc2a36 vgabios.c:1821
    call 0382ah                               ; e8 ef 0d                    ; 0xc2a38
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2a3b
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc2a3e vgabios.c:1822
    je short 02aa8h                           ; 74 66                       ; 0xc2a40
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2a42 vgabios.c:1825
    xor ah, ah                                ; 30 e4                       ; 0xc2a45
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc2a47
    lea dx, [bp-016h]                         ; 8d 56 ea                    ; 0xc2a4a
    call 00a96h                               ; e8 46 e0                    ; 0xc2a4d
    mov al, byte [bp-014h]                    ; 8a 46 ec                    ; 0xc2a50 vgabios.c:1826
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc2a53
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc2a56
    xor al, al                                ; 30 c0                       ; 0xc2a59
    shr ax, 008h                              ; c1 e8 08                    ; 0xc2a5b
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2a5e
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2a61 vgabios.c:47
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2a64
    mov es, dx                                ; 8e c2                       ; 0xc2a67
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc2a69
    xor dh, dh                                ; 30 f6                       ; 0xc2a6c vgabios.c:48
    inc dx                                    ; 42                          ; 0xc2a6e
    mov word [bp-012h], dx                    ; 89 56 ee                    ; 0xc2a6f
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2a72 vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc2a75
    mov word [bp-010h], dx                    ; 89 56 f0                    ; 0xc2a78 vgabios.c:58
    cmp ch, 008h                              ; 80 fd 08                    ; 0xc2a7b vgabios.c:1832
    jc short 02a8eh                           ; 72 0e                       ; 0xc2a7e
    jbe short 02a96h                          ; 76 14                       ; 0xc2a80
    cmp ch, 00dh                              ; 80 fd 0d                    ; 0xc2a82
    je short 02aabh                           ; 74 24                       ; 0xc2a85
    cmp ch, 00ah                              ; 80 fd 0a                    ; 0xc2a87
    je short 02aa1h                           ; 74 15                       ; 0xc2a8a
    jmp short 02ab2h                          ; eb 24                       ; 0xc2a8c
    cmp ch, 007h                              ; 80 fd 07                    ; 0xc2a8e
    jne short 02ab2h                          ; 75 1f                       ; 0xc2a91
    jmp near 02bb8h                           ; e9 22 01                    ; 0xc2a93
    cmp byte [bp-004h], 000h                  ; 80 7e fc 00                 ; 0xc2a96 vgabios.c:1839
    jbe short 02aafh                          ; 76 13                       ; 0xc2a9a
    dec byte [bp-004h]                        ; fe 4e fc                    ; 0xc2a9c
    jmp short 02aafh                          ; eb 0e                       ; 0xc2a9f vgabios.c:1840
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc2aa1 vgabios.c:1843
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2aa3
    jmp short 02aafh                          ; eb 07                       ; 0xc2aa6 vgabios.c:1844
    jmp near 02c5bh                           ; e9 b0 01                    ; 0xc2aa8
    mov byte [bp-004h], 000h                  ; c6 46 fc 00                 ; 0xc2aab vgabios.c:1847
    jmp near 02bb8h                           ; e9 06 01                    ; 0xc2aaf vgabios.c:1848
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2ab2 vgabios.c:1852
    xor ah, ah                                ; 30 e4                       ; 0xc2ab5
    mov bx, ax                                ; 89 c3                       ; 0xc2ab7
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2ab9
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc2abc
    jne short 02b05h                          ; 75 42                       ; 0xc2ac1
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc2ac3 vgabios.c:1855
    mul word [bp-012h]                        ; f7 66 ee                    ; 0xc2ac6
    add ax, ax                                ; 01 c0                       ; 0xc2ac9
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2acb
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2acd
    xor dh, dh                                ; 30 f6                       ; 0xc2ad0
    inc ax                                    ; 40                          ; 0xc2ad2
    mul dx                                    ; f7 e2                       ; 0xc2ad3
    mov si, ax                                ; 89 c6                       ; 0xc2ad5
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2ad7
    xor ah, ah                                ; 30 e4                       ; 0xc2ada
    mul word [bp-010h]                        ; f7 66 f0                    ; 0xc2adc
    mov dx, ax                                ; 89 c2                       ; 0xc2adf
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2ae1
    xor ah, ah                                ; 30 e4                       ; 0xc2ae4
    add ax, dx                                ; 01 d0                       ; 0xc2ae6
    add ax, ax                                ; 01 c0                       ; 0xc2ae8
    add si, ax                                ; 01 c6                       ; 0xc2aea
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc2aec vgabios.c:50
    mov byte [es:si], ch                      ; 26 88 2c                    ; 0xc2af0 vgabios.c:52
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc2af3 vgabios.c:1860
    jne short 02b34h                          ; 75 3c                       ; 0xc2af6
    inc si                                    ; 46                          ; 0xc2af8 vgabios.c:1861
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc2af9 vgabios.c:50
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2afd
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2b00
    jmp short 02b34h                          ; eb 2f                       ; 0xc2b03 vgabios.c:1863
    mov si, ax                                ; 89 c6                       ; 0xc2b05 vgabios.c:1866
    mov al, byte [si+0482ch]                  ; 8a 84 2c 48                 ; 0xc2b07
    mov si, ax                                ; 89 c6                       ; 0xc2b0b
    sal si, 006h                              ; c1 e6 06                    ; 0xc2b0d
    mov dl, byte [si+04842h]                  ; 8a 94 42 48                 ; 0xc2b10
    mov al, byte [bx+047afh]                  ; 8a 87 af 47                 ; 0xc2b14 vgabios.c:1867
    mov bl, byte [bx+047aeh]                  ; 8a 9f ae 47                 ; 0xc2b18 vgabios.c:1868
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc2b1c
    jc short 02b2fh                           ; 72 0e                       ; 0xc2b1f
    jbe short 02b36h                          ; 76 13                       ; 0xc2b21
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2b23
    je short 02b86h                           ; 74 5e                       ; 0xc2b26
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2b28
    je short 02b3ah                           ; 74 0d                       ; 0xc2b2b
    jmp short 02ba5h                          ; eb 76                       ; 0xc2b2d
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2b2f
    je short 02b64h                           ; 74 30                       ; 0xc2b32
    jmp short 02ba5h                          ; eb 6f                       ; 0xc2b34
    or byte [bp-00ah], 001h                   ; 80 4e f6 01                 ; 0xc2b36 vgabios.c:1871
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2b3a vgabios.c:1873
    xor ah, ah                                ; 30 e4                       ; 0xc2b3d
    push ax                                   ; 50                          ; 0xc2b3f
    mov al, dl                                ; 88 d0                       ; 0xc2b40
    push ax                                   ; 50                          ; 0xc2b42
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2b43
    push ax                                   ; 50                          ; 0xc2b46
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2b47
    mov bl, byte [bp-004h]                    ; 8a 5e fc                    ; 0xc2b4a
    xor bh, bh                                ; 30 ff                       ; 0xc2b4d
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2b4f
    xor dh, dh                                ; 30 f6                       ; 0xc2b52
    mov byte [bp-00eh], ch                    ; 88 6e f2                    ; 0xc2b54
    mov byte [bp-00dh], ah                    ; 88 66 f3                    ; 0xc2b57
    mov cx, ax                                ; 89 c1                       ; 0xc2b5a
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2b5c
    call 022d0h                               ; e8 6e f7                    ; 0xc2b5f
    jmp short 02ba5h                          ; eb 41                       ; 0xc2b62 vgabios.c:1874
    push ax                                   ; 50                          ; 0xc2b64 vgabios.c:1876
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2b65
    push ax                                   ; 50                          ; 0xc2b68
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2b69
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc2b6c
    mov byte [bp-00dh], ah                    ; 88 66 f3                    ; 0xc2b6f
    mov bl, byte [bp-004h]                    ; 8a 5e fc                    ; 0xc2b72
    xor bh, bh                                ; 30 ff                       ; 0xc2b75
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2b77
    xor dh, dh                                ; 30 f6                       ; 0xc2b7a
    mov al, ch                                ; 88 e8                       ; 0xc2b7c
    mov cx, word [bp-00eh]                    ; 8b 4e f2                    ; 0xc2b7e
    call 023e1h                               ; e8 5d f8                    ; 0xc2b81
    jmp short 02ba5h                          ; eb 1f                       ; 0xc2b84 vgabios.c:1877
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2b86 vgabios.c:1879
    push ax                                   ; 50                          ; 0xc2b89
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2b8a
    mov bl, byte [bp-004h]                    ; 8a 5e fc                    ; 0xc2b8d
    xor bh, bh                                ; 30 ff                       ; 0xc2b90
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2b92
    xor dh, dh                                ; 30 f6                       ; 0xc2b95
    mov byte [bp-00eh], ch                    ; 88 6e f2                    ; 0xc2b97
    mov byte [bp-00dh], ah                    ; 88 66 f3                    ; 0xc2b9a
    mov cx, ax                                ; 89 c1                       ; 0xc2b9d
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2b9f
    call 024f3h                               ; e8 4e f9                    ; 0xc2ba2
    inc byte [bp-004h]                        ; fe 46 fc                    ; 0xc2ba5 vgabios.c:1887
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2ba8 vgabios.c:1889
    xor ah, ah                                ; 30 e4                       ; 0xc2bab
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc2bad
    jne short 02bb8h                          ; 75 06                       ; 0xc2bb0
    mov byte [bp-004h], ah                    ; 88 66 fc                    ; 0xc2bb2 vgabios.c:1890
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc2bb5 vgabios.c:1891
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2bb8 vgabios.c:1896
    xor ah, ah                                ; 30 e4                       ; 0xc2bbb
    cmp ax, word [bp-012h]                    ; 3b 46 ee                    ; 0xc2bbd
    jne short 02c23h                          ; 75 61                       ; 0xc2bc0
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc2bc2 vgabios.c:1898
    xor bh, bh                                ; 30 ff                       ; 0xc2bc5
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2bc7
    mov ch, byte [bp-012h]                    ; 8a 6e ee                    ; 0xc2bca
    db  0feh, 0cdh
    ; dec ch                                    ; fe cd                     ; 0xc2bcd
    mov cl, byte [bp-010h]                    ; 8a 4e f0                    ; 0xc2bcf
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc2bd2
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc2bd4
    jne short 02c25h                          ; 75 4a                       ; 0xc2bd9
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc2bdb vgabios.c:1900
    mul word [bp-012h]                        ; f7 66 ee                    ; 0xc2bde
    add ax, ax                                ; 01 c0                       ; 0xc2be1
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2be3
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2be5
    xor dh, dh                                ; 30 f6                       ; 0xc2be8
    inc ax                                    ; 40                          ; 0xc2bea
    mul dx                                    ; f7 e2                       ; 0xc2beb
    mov si, ax                                ; 89 c6                       ; 0xc2bed
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2bef
    xor ah, ah                                ; 30 e4                       ; 0xc2bf2
    dec ax                                    ; 48                          ; 0xc2bf4
    mul word [bp-010h]                        ; f7 66 f0                    ; 0xc2bf5
    mov dx, ax                                ; 89 c2                       ; 0xc2bf8
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2bfa
    xor ah, ah                                ; 30 e4                       ; 0xc2bfd
    add ax, dx                                ; 01 d0                       ; 0xc2bff
    add ax, ax                                ; 01 c0                       ; 0xc2c01
    add si, ax                                ; 01 c6                       ; 0xc2c03
    inc si                                    ; 46                          ; 0xc2c05 vgabios.c:1901
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc2c06 vgabios.c:45
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc2c0a
    push strict byte 00001h                   ; 6a 01                       ; 0xc2c0d vgabios.c:1902
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c0f
    xor ah, ah                                ; 30 e4                       ; 0xc2c12
    push ax                                   ; 50                          ; 0xc2c14
    mov al, cl                                ; 88 c8                       ; 0xc2c15
    push ax                                   ; 50                          ; 0xc2c17
    mov al, ch                                ; 88 e8                       ; 0xc2c18
    push ax                                   ; 50                          ; 0xc2c1a
    xor dh, dh                                ; 30 f6                       ; 0xc2c1b
    xor cx, cx                                ; 31 c9                       ; 0xc2c1d
    xor bx, bx                                ; 31 db                       ; 0xc2c1f
    jmp short 02c37h                          ; eb 14                       ; 0xc2c21 vgabios.c:1904
    jmp short 02c40h                          ; eb 1b                       ; 0xc2c23
    push strict byte 00001h                   ; 6a 01                       ; 0xc2c25 vgabios.c:1906
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c27
    push ax                                   ; 50                          ; 0xc2c2a
    mov al, cl                                ; 88 c8                       ; 0xc2c2b
    push ax                                   ; 50                          ; 0xc2c2d
    mov al, ch                                ; 88 e8                       ; 0xc2c2e
    push ax                                   ; 50                          ; 0xc2c30
    xor cx, cx                                ; 31 c9                       ; 0xc2c31
    xor bx, bx                                ; 31 db                       ; 0xc2c33
    xor dx, dx                                ; 31 d2                       ; 0xc2c35
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc2c37
    call 01c48h                               ; e8 0b f0                    ; 0xc2c3a
    dec byte [bp-006h]                        ; fe 4e fa                    ; 0xc2c3d vgabios.c:1908
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2c40 vgabios.c:1912
    xor ah, ah                                ; 30 e4                       ; 0xc2c43
    mov word [bp-014h], ax                    ; 89 46 ec                    ; 0xc2c45
    sal word [bp-014h], 008h                  ; c1 66 ec 08                 ; 0xc2c48
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2c4c
    add word [bp-014h], ax                    ; 01 46 ec                    ; 0xc2c4f
    mov dx, word [bp-014h]                    ; 8b 56 ec                    ; 0xc2c52 vgabios.c:1913
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c55
    call 01281h                               ; e8 26 e6                    ; 0xc2c58
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2c5b vgabios.c:1914
    pop si                                    ; 5e                          ; 0xc2c5e
    pop bp                                    ; 5d                          ; 0xc2c5f
    retn                                      ; c3                          ; 0xc2c60
  ; disGetNextSymbol 0xc2c61 LB 0x18e2 -> off=0x0 cb=0000000000000033 uValue=00000000000c2c61 'get_font_access'
get_font_access:                             ; 0xc2c61 LB 0x33
    push bp                                   ; 55                          ; 0xc2c61 vgabios.c:1917
    mov bp, sp                                ; 89 e5                       ; 0xc2c62
    push dx                                   ; 52                          ; 0xc2c64
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2c65 vgabios.c:1919
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2c68
    out DX, ax                                ; ef                          ; 0xc2c6b
    mov AL, strict byte 006h                  ; b0 06                       ; 0xc2c6c vgabios.c:1920
    out DX, AL                                ; ee                          ; 0xc2c6e
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc2c6f vgabios.c:1921
    in AL, DX                                 ; ec                          ; 0xc2c72
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2c73
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2c75
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc2c78
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2c7a
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2c7d
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2c7f
    out DX, ax                                ; ef                          ; 0xc2c82
    mov ax, 00402h                            ; b8 02 04                    ; 0xc2c83 vgabios.c:1922
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2c86
    out DX, ax                                ; ef                          ; 0xc2c89
    mov ax, 00604h                            ; b8 04 06                    ; 0xc2c8a vgabios.c:1923
    out DX, ax                                ; ef                          ; 0xc2c8d
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2c8e vgabios.c:1924
    pop dx                                    ; 5a                          ; 0xc2c91
    pop bp                                    ; 5d                          ; 0xc2c92
    retn                                      ; c3                          ; 0xc2c93
  ; disGetNextSymbol 0xc2c94 LB 0x18af -> off=0x0 cb=0000000000000030 uValue=00000000000c2c94 'release_font_access'
release_font_access:                         ; 0xc2c94 LB 0x30
    push bp                                   ; 55                          ; 0xc2c94 vgabios.c:1926
    mov bp, sp                                ; 89 e5                       ; 0xc2c95
    push dx                                   ; 52                          ; 0xc2c97
    mov dx, 003cch                            ; ba cc 03                    ; 0xc2c98 vgabios.c:1928
    in AL, DX                                 ; ec                          ; 0xc2c9b
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2c9c
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2c9e
    sal ax, 002h                              ; c1 e0 02                    ; 0xc2ca1
    or AL, strict byte 00ah                   ; 0c 0a                       ; 0xc2ca4
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2ca6
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2ca9
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2cab
    out DX, ax                                ; ef                          ; 0xc2cae
    mov ax, 01005h                            ; b8 05 10                    ; 0xc2caf vgabios.c:1929
    out DX, ax                                ; ef                          ; 0xc2cb2
    mov ax, 00302h                            ; b8 02 03                    ; 0xc2cb3 vgabios.c:1930
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2cb6
    out DX, ax                                ; ef                          ; 0xc2cb9
    mov ax, 00204h                            ; b8 04 02                    ; 0xc2cba vgabios.c:1931
    out DX, ax                                ; ef                          ; 0xc2cbd
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2cbe vgabios.c:1932
    pop dx                                    ; 5a                          ; 0xc2cc1
    pop bp                                    ; 5d                          ; 0xc2cc2
    retn                                      ; c3                          ; 0xc2cc3
  ; disGetNextSymbol 0xc2cc4 LB 0x187f -> off=0x0 cb=00000000000000cf uValue=00000000000c2cc4 'set_scan_lines'
set_scan_lines:                              ; 0xc2cc4 LB 0xcf
    push bp                                   ; 55                          ; 0xc2cc4 vgabios.c:1934
    mov bp, sp                                ; 89 e5                       ; 0xc2cc5
    push bx                                   ; 53                          ; 0xc2cc7
    push cx                                   ; 51                          ; 0xc2cc8
    push dx                                   ; 52                          ; 0xc2cc9
    push si                                   ; 56                          ; 0xc2cca
    push ax                                   ; 50                          ; 0xc2ccb
    push ax                                   ; 50                          ; 0xc2ccc
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2ccd
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc2cd0 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2cd3
    mov es, ax                                ; 8e c0                       ; 0xc2cd6
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc2cd8
    mov bx, cx                                ; 89 cb                       ; 0xc2cdb vgabios.c:58
    mov AL, strict byte 009h                  ; b0 09                       ; 0xc2cdd vgabios.c:1940
    mov dx, cx                                ; 89 ca                       ; 0xc2cdf
    out DX, AL                                ; ee                          ; 0xc2ce1
    lea dx, [bx+001h]                         ; 8d 57 01                    ; 0xc2ce2 vgabios.c:1941
    in AL, DX                                 ; ec                          ; 0xc2ce5
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2ce6
    and AL, strict byte 0e0h                  ; 24 e0                       ; 0xc2ce8 vgabios.c:1942
    mov ah, byte [bp-00ah]                    ; 8a 66 f6                    ; 0xc2cea
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc2ced
    or al, ah                                 ; 08 e0                       ; 0xc2cef
    out DX, AL                                ; ee                          ; 0xc2cf1 vgabios.c:1943
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2cf2 vgabios.c:1948
    xor ah, ah                                ; 30 e4                       ; 0xc2cf5
    mov cx, ax                                ; 89 c1                       ; 0xc2cf7
    sal cx, 008h                              ; c1 e1 08                    ; 0xc2cf9
    dec ax                                    ; 48                          ; 0xc2cfc
    sub cx, 00200h                            ; 81 e9 00 02                 ; 0xc2cfd
    or cx, ax                                 ; 09 c1                       ; 0xc2d01
    cmp byte [bp-00ah], 00eh                  ; 80 7e f6 0e                 ; 0xc2d03 vgabios.c:1949
    jc short 02d0dh                           ; 72 04                       ; 0xc2d07
    sub cx, 00101h                            ; 81 e9 01 01                 ; 0xc2d09 vgabios.c:1950
    mov ax, cx                                ; 89 c8                       ; 0xc2d0d vgabios.c:1952
    xor al, cl                                ; 30 c8                       ; 0xc2d0f
    or AL, strict byte 00ah                   ; 0c 0a                       ; 0xc2d11
    mov dx, bx                                ; 89 da                       ; 0xc2d13
    out DX, ax                                ; ef                          ; 0xc2d15
    mov ax, cx                                ; 89 c8                       ; 0xc2d16 vgabios.c:1953
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2d18
    or AL, strict byte 00bh                   ; 0c 0b                       ; 0xc2d1b
    out DX, ax                                ; ef                          ; 0xc2d1d
    mov si, strict word 00060h                ; be 60 00                    ; 0xc2d1e vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2d21
    mov es, ax                                ; 8e c0                       ; 0xc2d24
    mov word [es:si], cx                      ; 26 89 0c                    ; 0xc2d26
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2d29 vgabios.c:1956
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2d2c
    mov byte [bp-00bh], 000h                  ; c6 46 f5 00                 ; 0xc2d2f
    mov si, 00085h                            ; be 85 00                    ; 0xc2d33 vgabios.c:62
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc2d36
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc2d39
    mov AL, strict byte 012h                  ; b0 12                       ; 0xc2d3c vgabios.c:1957
    out DX, AL                                ; ee                          ; 0xc2d3e
    lea cx, [bx+001h]                         ; 8d 4f 01                    ; 0xc2d3f vgabios.c:1958
    mov dx, cx                                ; 89 ca                       ; 0xc2d42
    in AL, DX                                 ; ec                          ; 0xc2d44
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2d45
    mov si, ax                                ; 89 c6                       ; 0xc2d47
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc2d49 vgabios.c:1959
    mov dx, bx                                ; 89 da                       ; 0xc2d4b
    out DX, AL                                ; ee                          ; 0xc2d4d
    mov dx, cx                                ; 89 ca                       ; 0xc2d4e vgabios.c:1960
    in AL, DX                                 ; ec                          ; 0xc2d50
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2d51
    mov bl, al                                ; 88 c3                       ; 0xc2d53 vgabios.c:1961
    and bl, 002h                              ; 80 e3 02                    ; 0xc2d55
    xor bh, bh                                ; 30 ff                       ; 0xc2d58
    sal bx, 007h                              ; c1 e3 07                    ; 0xc2d5a
    and AL, strict byte 040h                  ; 24 40                       ; 0xc2d5d
    xor ah, ah                                ; 30 e4                       ; 0xc2d5f
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2d61
    add ax, bx                                ; 01 d8                       ; 0xc2d64
    inc ax                                    ; 40                          ; 0xc2d66
    add ax, si                                ; 01 f0                       ; 0xc2d67
    xor dx, cx                                ; 31 ca                       ; 0xc2d69 vgabios.c:1962
    div word [bp-00ch]                        ; f7 76 f4                    ; 0xc2d6b
    mov cl, al                                ; 88 c1                       ; 0xc2d6e vgabios.c:1963
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc2d70
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2d72 vgabios.c:52
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc2d75
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2d78 vgabios.c:57
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc2d7b
    xor ah, ah                                ; 30 e4                       ; 0xc2d7e vgabios.c:1965
    mul bx                                    ; f7 e3                       ; 0xc2d80
    add ax, ax                                ; 01 c0                       ; 0xc2d82
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc2d84 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc2d87
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc2d8a vgabios.c:1966
    pop si                                    ; 5e                          ; 0xc2d8d
    pop dx                                    ; 5a                          ; 0xc2d8e
    pop cx                                    ; 59                          ; 0xc2d8f
    pop bx                                    ; 5b                          ; 0xc2d90
    pop bp                                    ; 5d                          ; 0xc2d91
    retn                                      ; c3                          ; 0xc2d92
  ; disGetNextSymbol 0xc2d93 LB 0x17b0 -> off=0x0 cb=0000000000000023 uValue=00000000000c2d93 'biosfn_set_font_block'
biosfn_set_font_block:                       ; 0xc2d93 LB 0x23
    push bp                                   ; 55                          ; 0xc2d93 vgabios.c:1968
    mov bp, sp                                ; 89 e5                       ; 0xc2d94
    push bx                                   ; 53                          ; 0xc2d96
    push dx                                   ; 52                          ; 0xc2d97
    mov bl, al                                ; 88 c3                       ; 0xc2d98
    mov ax, 00100h                            ; b8 00 01                    ; 0xc2d9a vgabios.c:1970
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2d9d
    out DX, ax                                ; ef                          ; 0xc2da0
    mov al, bl                                ; 88 d8                       ; 0xc2da1 vgabios.c:1971
    xor ah, ah                                ; 30 e4                       ; 0xc2da3
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2da5
    or AL, strict byte 003h                   ; 0c 03                       ; 0xc2da8
    out DX, ax                                ; ef                          ; 0xc2daa
    mov ax, 00300h                            ; b8 00 03                    ; 0xc2dab vgabios.c:1972
    out DX, ax                                ; ef                          ; 0xc2dae
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2daf vgabios.c:1973
    pop dx                                    ; 5a                          ; 0xc2db2
    pop bx                                    ; 5b                          ; 0xc2db3
    pop bp                                    ; 5d                          ; 0xc2db4
    retn                                      ; c3                          ; 0xc2db5
  ; disGetNextSymbol 0xc2db6 LB 0x178d -> off=0x0 cb=0000000000000075 uValue=00000000000c2db6 'load_text_patch'
load_text_patch:                             ; 0xc2db6 LB 0x75
    push bp                                   ; 55                          ; 0xc2db6 vgabios.c:1975
    mov bp, sp                                ; 89 e5                       ; 0xc2db7
    push si                                   ; 56                          ; 0xc2db9
    push di                                   ; 57                          ; 0xc2dba
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc2dbb
    push ax                                   ; 50                          ; 0xc2dbe
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc2dbf
    call 02c61h                               ; e8 9c fe                    ; 0xc2dc2 vgabios.c:1980
    mov al, bl                                ; 88 d8                       ; 0xc2dc5 vgabios.c:1982
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2dc7
    xor ah, ah                                ; 30 e4                       ; 0xc2dc9
    mov cx, ax                                ; 89 c1                       ; 0xc2dcb
    sal cx, 00eh                              ; c1 e1 0e                    ; 0xc2dcd
    mov al, bl                                ; 88 d8                       ; 0xc2dd0
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2dd2
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2dd4
    add cx, ax                                ; 01 c1                       ; 0xc2dd7
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc2dd9
    mov bx, dx                                ; 89 d3                       ; 0xc2ddc vgabios.c:1983
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2dde
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc2de1
    inc dx                                    ; 42                          ; 0xc2de4 vgabios.c:1984
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc2de5
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc2de8 vgabios.c:1985
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2deb
    test al, al                               ; 84 c0                       ; 0xc2dee
    je short 02e21h                           ; 74 2f                       ; 0xc2df0
    xor ah, ah                                ; 30 e4                       ; 0xc2df2 vgabios.c:1986
    sal ax, 005h                              ; c1 e0 05                    ; 0xc2df4
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc2df7
    add di, ax                                ; 01 c7                       ; 0xc2dfa
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc2dfc vgabios.c:1987
    xor ch, ch                                ; 30 ed                       ; 0xc2dff
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc2e01
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2e04
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2e07
    mov es, ax                                ; 8e c0                       ; 0xc2e0a
    jcxz 02e14h                               ; e3 06                       ; 0xc2e0c
    push DS                                   ; 1e                          ; 0xc2e0e
    mov ds, dx                                ; 8e da                       ; 0xc2e0f
    rep movsb                                 ; f3 a4                       ; 0xc2e11
    pop DS                                    ; 1f                          ; 0xc2e13
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2e14 vgabios.c:1988
    xor ah, ah                                ; 30 e4                       ; 0xc2e17
    inc ax                                    ; 40                          ; 0xc2e19
    add word [bp-00ch], ax                    ; 01 46 f4                    ; 0xc2e1a
    add bx, ax                                ; 01 c3                       ; 0xc2e1d vgabios.c:1989
    jmp short 02de8h                          ; eb c7                       ; 0xc2e1f vgabios.c:1990
    call 02c94h                               ; e8 70 fe                    ; 0xc2e21 vgabios.c:1992
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2e24 vgabios.c:1993
    pop di                                    ; 5f                          ; 0xc2e27
    pop si                                    ; 5e                          ; 0xc2e28
    pop bp                                    ; 5d                          ; 0xc2e29
    retn                                      ; c3                          ; 0xc2e2a
  ; disGetNextSymbol 0xc2e2b LB 0x1718 -> off=0x0 cb=000000000000007f uValue=00000000000c2e2b 'biosfn_load_text_user_pat'
biosfn_load_text_user_pat:                   ; 0xc2e2b LB 0x7f
    push bp                                   ; 55                          ; 0xc2e2b vgabios.c:1995
    mov bp, sp                                ; 89 e5                       ; 0xc2e2c
    push si                                   ; 56                          ; 0xc2e2e
    push di                                   ; 57                          ; 0xc2e2f
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2e30
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2e33
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc2e36
    mov word [bp-00ah], bx                    ; 89 5e f6                    ; 0xc2e39
    mov word [bp-00eh], cx                    ; 89 4e f2                    ; 0xc2e3c
    call 02c61h                               ; e8 1f fe                    ; 0xc2e3f vgabios.c:2000
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2e42 vgabios.c:2001
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2e45
    xor ah, ah                                ; 30 e4                       ; 0xc2e47
    mov bx, ax                                ; 89 c3                       ; 0xc2e49
    sal bx, 00eh                              ; c1 e3 0e                    ; 0xc2e4b
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2e4e
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2e51
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2e53
    add bx, ax                                ; 01 c3                       ; 0xc2e56
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc2e58
    xor bx, bx                                ; 31 db                       ; 0xc2e5b vgabios.c:2002
    cmp bx, word [bp-00eh]                    ; 3b 5e f2                    ; 0xc2e5d
    jnc short 02e90h                          ; 73 2e                       ; 0xc2e60
    mov cl, byte [bp+008h]                    ; 8a 4e 08                    ; 0xc2e62 vgabios.c:2004
    xor ch, ch                                ; 30 ed                       ; 0xc2e65
    mov ax, bx                                ; 89 d8                       ; 0xc2e67
    mul cx                                    ; f7 e1                       ; 0xc2e69
    mov si, word [bp-00ah]                    ; 8b 76 f6                    ; 0xc2e6b
    add si, ax                                ; 01 c6                       ; 0xc2e6e
    mov ax, word [bp+004h]                    ; 8b 46 04                    ; 0xc2e70 vgabios.c:2005
    add ax, bx                                ; 01 d8                       ; 0xc2e73
    sal ax, 005h                              ; c1 e0 05                    ; 0xc2e75
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc2e78
    add di, ax                                ; 01 c7                       ; 0xc2e7b
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc2e7d vgabios.c:2006
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2e80
    mov es, ax                                ; 8e c0                       ; 0xc2e83
    jcxz 02e8dh                               ; e3 06                       ; 0xc2e85
    push DS                                   ; 1e                          ; 0xc2e87
    mov ds, dx                                ; 8e da                       ; 0xc2e88
    rep movsb                                 ; f3 a4                       ; 0xc2e8a
    pop DS                                    ; 1f                          ; 0xc2e8c
    inc bx                                    ; 43                          ; 0xc2e8d vgabios.c:2007
    jmp short 02e5dh                          ; eb cd                       ; 0xc2e8e
    call 02c94h                               ; e8 01 fe                    ; 0xc2e90 vgabios.c:2008
    cmp byte [bp-006h], 010h                  ; 80 7e fa 10                 ; 0xc2e93 vgabios.c:2009
    jc short 02ea1h                           ; 72 08                       ; 0xc2e97
    mov al, byte [bp+008h]                    ; 8a 46 08                    ; 0xc2e99 vgabios.c:2011
    xor ah, ah                                ; 30 e4                       ; 0xc2e9c
    call 02cc4h                               ; e8 23 fe                    ; 0xc2e9e
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2ea1 vgabios.c:2013
    pop di                                    ; 5f                          ; 0xc2ea4
    pop si                                    ; 5e                          ; 0xc2ea5
    pop bp                                    ; 5d                          ; 0xc2ea6
    retn 00006h                               ; c2 06 00                    ; 0xc2ea7
  ; disGetNextSymbol 0xc2eaa LB 0x1699 -> off=0x0 cb=0000000000000016 uValue=00000000000c2eaa 'biosfn_load_gfx_8_8_chars'
biosfn_load_gfx_8_8_chars:                   ; 0xc2eaa LB 0x16
    push bp                                   ; 55                          ; 0xc2eaa vgabios.c:2015
    mov bp, sp                                ; 89 e5                       ; 0xc2eab
    push bx                                   ; 53                          ; 0xc2ead
    push cx                                   ; 51                          ; 0xc2eae
    mov bx, dx                                ; 89 d3                       ; 0xc2eaf vgabios.c:2017
    mov cx, ax                                ; 89 c1                       ; 0xc2eb1
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc2eb3
    call 009f0h                               ; e8 37 db                    ; 0xc2eb6
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2eb9 vgabios.c:2018
    pop cx                                    ; 59                          ; 0xc2ebc
    pop bx                                    ; 5b                          ; 0xc2ebd
    pop bp                                    ; 5d                          ; 0xc2ebe
    retn                                      ; c3                          ; 0xc2ebf
  ; disGetNextSymbol 0xc2ec0 LB 0x1683 -> off=0x0 cb=000000000000004d uValue=00000000000c2ec0 'set_gfx_font'
set_gfx_font:                                ; 0xc2ec0 LB 0x4d
    push bp                                   ; 55                          ; 0xc2ec0 vgabios.c:2020
    mov bp, sp                                ; 89 e5                       ; 0xc2ec1
    push si                                   ; 56                          ; 0xc2ec3
    push di                                   ; 57                          ; 0xc2ec4
    mov si, ax                                ; 89 c6                       ; 0xc2ec5
    mov ax, dx                                ; 89 d0                       ; 0xc2ec7
    mov di, bx                                ; 89 df                       ; 0xc2ec9
    mov dl, cl                                ; 88 ca                       ; 0xc2ecb
    mov bx, si                                ; 89 f3                       ; 0xc2ecd vgabios.c:2024
    mov cx, ax                                ; 89 c1                       ; 0xc2ecf
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc2ed1
    call 009f0h                               ; e8 19 db                    ; 0xc2ed4
    test dl, dl                               ; 84 d2                       ; 0xc2ed7 vgabios.c:2025
    je short 02eedh                           ; 74 12                       ; 0xc2ed9
    cmp dl, 003h                              ; 80 fa 03                    ; 0xc2edb vgabios.c:2026
    jbe short 02ee2h                          ; 76 02                       ; 0xc2ede
    mov DL, strict byte 002h                  ; b2 02                       ; 0xc2ee0 vgabios.c:2027
    mov bl, dl                                ; 88 d3                       ; 0xc2ee2 vgabios.c:2028
    xor bh, bh                                ; 30 ff                       ; 0xc2ee4
    mov al, byte [bx+07dfbh]                  ; 8a 87 fb 7d                 ; 0xc2ee6
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2eea
    mov bx, 00085h                            ; bb 85 00                    ; 0xc2eed vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2ef0
    mov es, ax                                ; 8e c0                       ; 0xc2ef3
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc2ef5
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2ef8 vgabios.c:2033
    xor ah, ah                                ; 30 e4                       ; 0xc2efb
    dec ax                                    ; 48                          ; 0xc2efd
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2efe vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc2f01
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f04 vgabios.c:2034
    pop di                                    ; 5f                          ; 0xc2f07
    pop si                                    ; 5e                          ; 0xc2f08
    pop bp                                    ; 5d                          ; 0xc2f09
    retn 00002h                               ; c2 02 00                    ; 0xc2f0a
  ; disGetNextSymbol 0xc2f0d LB 0x1636 -> off=0x0 cb=000000000000001d uValue=00000000000c2f0d 'biosfn_load_gfx_user_chars'
biosfn_load_gfx_user_chars:                  ; 0xc2f0d LB 0x1d
    push bp                                   ; 55                          ; 0xc2f0d vgabios.c:2036
    mov bp, sp                                ; 89 e5                       ; 0xc2f0e
    push si                                   ; 56                          ; 0xc2f10
    mov si, ax                                ; 89 c6                       ; 0xc2f11
    mov ax, dx                                ; 89 d0                       ; 0xc2f13
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc2f15 vgabios.c:2039
    xor dh, dh                                ; 30 f6                       ; 0xc2f18
    push dx                                   ; 52                          ; 0xc2f1a
    xor ch, ch                                ; 30 ed                       ; 0xc2f1b
    mov dx, si                                ; 89 f2                       ; 0xc2f1d
    call 02ec0h                               ; e8 9e ff                    ; 0xc2f1f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2f22 vgabios.c:2040
    pop si                                    ; 5e                          ; 0xc2f25
    pop bp                                    ; 5d                          ; 0xc2f26
    retn 00002h                               ; c2 02 00                    ; 0xc2f27
  ; disGetNextSymbol 0xc2f2a LB 0x1619 -> off=0x0 cb=0000000000000022 uValue=00000000000c2f2a 'biosfn_load_gfx_8_14_chars'
biosfn_load_gfx_8_14_chars:                  ; 0xc2f2a LB 0x22
    push bp                                   ; 55                          ; 0xc2f2a vgabios.c:2045
    mov bp, sp                                ; 89 e5                       ; 0xc2f2b
    push bx                                   ; 53                          ; 0xc2f2d
    push cx                                   ; 51                          ; 0xc2f2e
    mov bl, al                                ; 88 c3                       ; 0xc2f2f
    mov al, dl                                ; 88 d0                       ; 0xc2f31
    xor ah, ah                                ; 30 e4                       ; 0xc2f33 vgabios.c:2047
    push ax                                   ; 50                          ; 0xc2f35
    mov al, bl                                ; 88 d8                       ; 0xc2f36
    mov cx, ax                                ; 89 c1                       ; 0xc2f38
    mov bx, strict word 0000eh                ; bb 0e 00                    ; 0xc2f3a
    mov ax, 05d6ah                            ; b8 6a 5d                    ; 0xc2f3d
    mov dx, ds                                ; 8c da                       ; 0xc2f40
    call 02ec0h                               ; e8 7b ff                    ; 0xc2f42
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f45 vgabios.c:2048
    pop cx                                    ; 59                          ; 0xc2f48
    pop bx                                    ; 5b                          ; 0xc2f49
    pop bp                                    ; 5d                          ; 0xc2f4a
    retn                                      ; c3                          ; 0xc2f4b
  ; disGetNextSymbol 0xc2f4c LB 0x15f7 -> off=0x0 cb=0000000000000022 uValue=00000000000c2f4c 'biosfn_load_gfx_8_8_dd_chars'
biosfn_load_gfx_8_8_dd_chars:                ; 0xc2f4c LB 0x22
    push bp                                   ; 55                          ; 0xc2f4c vgabios.c:2049
    mov bp, sp                                ; 89 e5                       ; 0xc2f4d
    push bx                                   ; 53                          ; 0xc2f4f
    push cx                                   ; 51                          ; 0xc2f50
    mov bl, al                                ; 88 c3                       ; 0xc2f51
    mov al, dl                                ; 88 d0                       ; 0xc2f53
    xor ah, ah                                ; 30 e4                       ; 0xc2f55 vgabios.c:2051
    push ax                                   ; 50                          ; 0xc2f57
    mov al, bl                                ; 88 d8                       ; 0xc2f58
    mov cx, ax                                ; 89 c1                       ; 0xc2f5a
    mov bx, strict word 00008h                ; bb 08 00                    ; 0xc2f5c
    mov ax, 0556ah                            ; b8 6a 55                    ; 0xc2f5f
    mov dx, ds                                ; 8c da                       ; 0xc2f62
    call 02ec0h                               ; e8 59 ff                    ; 0xc2f64
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f67 vgabios.c:2052
    pop cx                                    ; 59                          ; 0xc2f6a
    pop bx                                    ; 5b                          ; 0xc2f6b
    pop bp                                    ; 5d                          ; 0xc2f6c
    retn                                      ; c3                          ; 0xc2f6d
  ; disGetNextSymbol 0xc2f6e LB 0x15d5 -> off=0x0 cb=0000000000000022 uValue=00000000000c2f6e 'biosfn_load_gfx_8_16_chars'
biosfn_load_gfx_8_16_chars:                  ; 0xc2f6e LB 0x22
    push bp                                   ; 55                          ; 0xc2f6e vgabios.c:2053
    mov bp, sp                                ; 89 e5                       ; 0xc2f6f
    push bx                                   ; 53                          ; 0xc2f71
    push cx                                   ; 51                          ; 0xc2f72
    mov bl, al                                ; 88 c3                       ; 0xc2f73
    mov al, dl                                ; 88 d0                       ; 0xc2f75
    xor ah, ah                                ; 30 e4                       ; 0xc2f77 vgabios.c:2055
    push ax                                   ; 50                          ; 0xc2f79
    mov al, bl                                ; 88 d8                       ; 0xc2f7a
    mov cx, ax                                ; 89 c1                       ; 0xc2f7c
    mov bx, strict word 00010h                ; bb 10 00                    ; 0xc2f7e
    mov ax, 06b6ah                            ; b8 6a 6b                    ; 0xc2f81
    mov dx, ds                                ; 8c da                       ; 0xc2f84
    call 02ec0h                               ; e8 37 ff                    ; 0xc2f86
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f89 vgabios.c:2056
    pop cx                                    ; 59                          ; 0xc2f8c
    pop bx                                    ; 5b                          ; 0xc2f8d
    pop bp                                    ; 5d                          ; 0xc2f8e
    retn                                      ; c3                          ; 0xc2f8f
  ; disGetNextSymbol 0xc2f90 LB 0x15b3 -> off=0x0 cb=0000000000000005 uValue=00000000000c2f90 'biosfn_alternate_prtsc'
biosfn_alternate_prtsc:                      ; 0xc2f90 LB 0x5
    push bp                                   ; 55                          ; 0xc2f90 vgabios.c:2058
    mov bp, sp                                ; 89 e5                       ; 0xc2f91
    pop bp                                    ; 5d                          ; 0xc2f93 vgabios.c:2063
    retn                                      ; c3                          ; 0xc2f94
  ; disGetNextSymbol 0xc2f95 LB 0x15ae -> off=0x0 cb=0000000000000032 uValue=00000000000c2f95 'biosfn_set_txt_lines'
biosfn_set_txt_lines:                        ; 0xc2f95 LB 0x32
    push bx                                   ; 53                          ; 0xc2f95 vgabios.c:2065
    push si                                   ; 56                          ; 0xc2f96
    push bp                                   ; 55                          ; 0xc2f97
    mov bp, sp                                ; 89 e5                       ; 0xc2f98
    mov bl, al                                ; 88 c3                       ; 0xc2f9a
    mov si, 00089h                            ; be 89 00                    ; 0xc2f9c vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2f9f
    mov es, ax                                ; 8e c0                       ; 0xc2fa2
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2fa4
    and AL, strict byte 06fh                  ; 24 6f                       ; 0xc2fa7 vgabios.c:2071
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2fa9 vgabios.c:2073
    je short 02fb6h                           ; 74 08                       ; 0xc2fac
    test bl, bl                               ; 84 db                       ; 0xc2fae
    jne short 02fb8h                          ; 75 06                       ; 0xc2fb0
    or AL, strict byte 080h                   ; 0c 80                       ; 0xc2fb2 vgabios.c:2076
    jmp short 02fb8h                          ; eb 02                       ; 0xc2fb4 vgabios.c:2077
    or AL, strict byte 010h                   ; 0c 10                       ; 0xc2fb6 vgabios.c:2079
    mov bx, 00089h                            ; bb 89 00                    ; 0xc2fb8 vgabios.c:52
    mov si, strict word 00040h                ; be 40 00                    ; 0xc2fbb
    mov es, si                                ; 8e c6                       ; 0xc2fbe
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2fc0
    pop bp                                    ; 5d                          ; 0xc2fc3 vgabios.c:2083
    pop si                                    ; 5e                          ; 0xc2fc4
    pop bx                                    ; 5b                          ; 0xc2fc5
    retn                                      ; c3                          ; 0xc2fc6
  ; disGetNextSymbol 0xc2fc7 LB 0x157c -> off=0x0 cb=0000000000000005 uValue=00000000000c2fc7 'biosfn_switch_video_interface'
biosfn_switch_video_interface:               ; 0xc2fc7 LB 0x5
    push bp                                   ; 55                          ; 0xc2fc7 vgabios.c:2086
    mov bp, sp                                ; 89 e5                       ; 0xc2fc8
    pop bp                                    ; 5d                          ; 0xc2fca vgabios.c:2091
    retn                                      ; c3                          ; 0xc2fcb
  ; disGetNextSymbol 0xc2fcc LB 0x1577 -> off=0x0 cb=0000000000000005 uValue=00000000000c2fcc 'biosfn_enable_video_refresh_control'
biosfn_enable_video_refresh_control:         ; 0xc2fcc LB 0x5
    push bp                                   ; 55                          ; 0xc2fcc vgabios.c:2092
    mov bp, sp                                ; 89 e5                       ; 0xc2fcd
    pop bp                                    ; 5d                          ; 0xc2fcf vgabios.c:2097
    retn                                      ; c3                          ; 0xc2fd0
  ; disGetNextSymbol 0xc2fd1 LB 0x1572 -> off=0x0 cb=000000000000009d uValue=00000000000c2fd1 'biosfn_write_string'
biosfn_write_string:                         ; 0xc2fd1 LB 0x9d
    push bp                                   ; 55                          ; 0xc2fd1 vgabios.c:2100
    mov bp, sp                                ; 89 e5                       ; 0xc2fd2
    push si                                   ; 56                          ; 0xc2fd4
    push di                                   ; 57                          ; 0xc2fd5
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2fd6
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2fd9
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc2fdc
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc2fdf
    mov si, cx                                ; 89 ce                       ; 0xc2fe2
    mov di, word [bp+00ah]                    ; 8b 7e 0a                    ; 0xc2fe4
    mov al, dl                                ; 88 d0                       ; 0xc2fe7 vgabios.c:2107
    xor ah, ah                                ; 30 e4                       ; 0xc2fe9
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc2feb
    lea dx, [bp-00ch]                         ; 8d 56 f4                    ; 0xc2fee
    call 00a96h                               ; e8 a2 da                    ; 0xc2ff1
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc2ff4 vgabios.c:2110
    jne short 0300bh                          ; 75 11                       ; 0xc2ff8
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2ffa vgabios.c:2111
    mov byte [bp+006h], al                    ; 88 46 06                    ; 0xc2ffd
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc3000 vgabios.c:2112
    xor al, al                                ; 30 c0                       ; 0xc3003
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3005
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc3008
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc300b vgabios.c:2115
    xor dh, dh                                ; 30 f6                       ; 0xc300e
    sal dx, 008h                              ; c1 e2 08                    ; 0xc3010
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc3013
    xor ah, ah                                ; 30 e4                       ; 0xc3016
    add dx, ax                                ; 01 c2                       ; 0xc3018
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc301a vgabios.c:2116
    call 01281h                               ; e8 61 e2                    ; 0xc301d
    dec si                                    ; 4e                          ; 0xc3020 vgabios.c:2118
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc3021
    je short 03054h                           ; 74 2e                       ; 0xc3024
    mov bx, di                                ; 89 fb                       ; 0xc3026 vgabios.c:2120
    inc di                                    ; 47                          ; 0xc3028
    mov es, [bp+008h]                         ; 8e 46 08                    ; 0xc3029 vgabios.c:47
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc302c
    test byte [bp-006h], 002h                 ; f6 46 fa 02                 ; 0xc302f vgabios.c:2121
    je short 0303eh                           ; 74 09                       ; 0xc3033
    mov bx, di                                ; 89 fb                       ; 0xc3035 vgabios.c:2122
    inc di                                    ; 47                          ; 0xc3037
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3038 vgabios.c:47
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc303b vgabios.c:48
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc303e vgabios.c:2124
    xor bh, bh                                ; 30 ff                       ; 0xc3041
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc3043
    xor dh, dh                                ; 30 f6                       ; 0xc3046
    mov al, ah                                ; 88 e0                       ; 0xc3048
    xor ah, ah                                ; 30 e4                       ; 0xc304a
    mov cx, strict word 00003h                ; b9 03 00                    ; 0xc304c
    call 02a09h                               ; e8 b7 f9                    ; 0xc304f
    jmp short 03020h                          ; eb cc                       ; 0xc3052 vgabios.c:2125
    test byte [bp-006h], 001h                 ; f6 46 fa 01                 ; 0xc3054 vgabios.c:2128
    jne short 03065h                          ; 75 0b                       ; 0xc3058
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc305a vgabios.c:2129
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc305d
    xor ah, ah                                ; 30 e4                       ; 0xc3060
    call 01281h                               ; e8 1c e2                    ; 0xc3062
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3065 vgabios.c:2130
    pop di                                    ; 5f                          ; 0xc3068
    pop si                                    ; 5e                          ; 0xc3069
    pop bp                                    ; 5d                          ; 0xc306a
    retn 00008h                               ; c2 08 00                    ; 0xc306b
  ; disGetNextSymbol 0xc306e LB 0x14d5 -> off=0x0 cb=00000000000001ef uValue=00000000000c306e 'biosfn_read_state_info'
biosfn_read_state_info:                      ; 0xc306e LB 0x1ef
    push bp                                   ; 55                          ; 0xc306e vgabios.c:2133
    mov bp, sp                                ; 89 e5                       ; 0xc306f
    push cx                                   ; 51                          ; 0xc3071
    push si                                   ; 56                          ; 0xc3072
    push di                                   ; 57                          ; 0xc3073
    push ax                                   ; 50                          ; 0xc3074
    push ax                                   ; 50                          ; 0xc3075
    push dx                                   ; 52                          ; 0xc3076
    mov si, strict word 00049h                ; be 49 00                    ; 0xc3077 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc307a
    mov es, ax                                ; 8e c0                       ; 0xc307d
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc307f
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc3082 vgabios.c:48
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc3085 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3088
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc308b vgabios.c:58
    mov ax, ds                                ; 8c d8                       ; 0xc308e vgabios.c:2144
    mov es, dx                                ; 8e c2                       ; 0xc3090 vgabios.c:72
    mov word [es:bx], 05500h                  ; 26 c7 07 00 55              ; 0xc3092
    mov [es:bx+002h], ds                      ; 26 8c 5f 02                 ; 0xc3097
    lea di, [bx+004h]                         ; 8d 7f 04                    ; 0xc309b vgabios.c:2149
    mov cx, strict word 0001eh                ; b9 1e 00                    ; 0xc309e
    mov si, strict word 00049h                ; be 49 00                    ; 0xc30a1
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc30a4
    jcxz 030afh                               ; e3 06                       ; 0xc30a7
    push DS                                   ; 1e                          ; 0xc30a9
    mov ds, dx                                ; 8e da                       ; 0xc30aa
    rep movsb                                 ; f3 a4                       ; 0xc30ac
    pop DS                                    ; 1f                          ; 0xc30ae
    mov si, 00084h                            ; be 84 00                    ; 0xc30af vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc30b2
    mov es, ax                                ; 8e c0                       ; 0xc30b5
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc30b7
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc30ba vgabios.c:48
    lea si, [bx+022h]                         ; 8d 77 22                    ; 0xc30bc
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc30bf vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc30c2
    lea di, [bx+023h]                         ; 8d 7f 23                    ; 0xc30c5 vgabios.c:2151
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc30c8
    mov si, 00085h                            ; be 85 00                    ; 0xc30cb
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc30ce
    jcxz 030d9h                               ; e3 06                       ; 0xc30d1
    push DS                                   ; 1e                          ; 0xc30d3
    mov ds, dx                                ; 8e da                       ; 0xc30d4
    rep movsb                                 ; f3 a4                       ; 0xc30d6
    pop DS                                    ; 1f                          ; 0xc30d8
    mov si, 0008ah                            ; be 8a 00                    ; 0xc30d9 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc30dc
    mov es, ax                                ; 8e c0                       ; 0xc30df
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc30e1
    lea si, [bx+025h]                         ; 8d 77 25                    ; 0xc30e4 vgabios.c:48
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc30e7 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc30ea
    lea si, [bx+026h]                         ; 8d 77 26                    ; 0xc30ed vgabios.c:2154
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc30f0 vgabios.c:52
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc30f4 vgabios.c:2155
    mov word [es:si], strict word 00010h      ; 26 c7 04 10 00              ; 0xc30f7 vgabios.c:62
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc30fc vgabios.c:2156
    mov byte [es:si], 008h                    ; 26 c6 04 08                 ; 0xc30ff vgabios.c:52
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc3103 vgabios.c:2157
    mov byte [es:si], 002h                    ; 26 c6 04 02                 ; 0xc3106 vgabios.c:52
    lea si, [bx+02bh]                         ; 8d 77 2b                    ; 0xc310a vgabios.c:2158
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc310d vgabios.c:52
    lea si, [bx+02ch]                         ; 8d 77 2c                    ; 0xc3111 vgabios.c:2159
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc3114 vgabios.c:52
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc3118 vgabios.c:2160
    mov byte [es:si], 021h                    ; 26 c6 04 21                 ; 0xc311b vgabios.c:52
    lea si, [bx+031h]                         ; 8d 77 31                    ; 0xc311f vgabios.c:2161
    mov byte [es:si], 003h                    ; 26 c6 04 03                 ; 0xc3122 vgabios.c:52
    lea si, [bx+032h]                         ; 8d 77 32                    ; 0xc3126 vgabios.c:2162
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc3129 vgabios.c:52
    mov si, 00089h                            ; be 89 00                    ; 0xc312d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3130
    mov es, ax                                ; 8e c0                       ; 0xc3133
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3135
    mov dl, al                                ; 88 c2                       ; 0xc3138 vgabios.c:2167
    and dl, 080h                              ; 80 e2 80                    ; 0xc313a
    xor dh, dh                                ; 30 f6                       ; 0xc313d
    sar dx, 006h                              ; c1 fa 06                    ; 0xc313f
    and AL, strict byte 010h                  ; 24 10                       ; 0xc3142
    xor ah, ah                                ; 30 e4                       ; 0xc3144
    sar ax, 004h                              ; c1 f8 04                    ; 0xc3146
    or ax, dx                                 ; 09 d0                       ; 0xc3149
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc314b vgabios.c:2168
    je short 03161h                           ; 74 11                       ; 0xc314e
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc3150
    je short 0315dh                           ; 74 08                       ; 0xc3153
    test ax, ax                               ; 85 c0                       ; 0xc3155
    jne short 03161h                          ; 75 08                       ; 0xc3157
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc3159 vgabios.c:2169
    jmp short 03163h                          ; eb 06                       ; 0xc315b
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc315d vgabios.c:2170
    jmp short 03163h                          ; eb 02                       ; 0xc315f
    xor al, al                                ; 30 c0                       ; 0xc3161 vgabios.c:2172
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc3163 vgabios.c:2174
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3166 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3169
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc316c vgabios.c:2177
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc316f
    jc short 03192h                           ; 72 1f                       ; 0xc3171
    cmp AL, strict byte 012h                  ; 3c 12                       ; 0xc3173
    jnbe short 03192h                         ; 77 1b                       ; 0xc3175
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3177 vgabios.c:2178
    test ax, ax                               ; 85 c0                       ; 0xc317a
    je short 031d4h                           ; 74 56                       ; 0xc317c
    mov si, ax                                ; 89 c6                       ; 0xc317e vgabios.c:2179
    shr si, 002h                              ; c1 ee 02                    ; 0xc3180
    mov ax, 04000h                            ; b8 00 40                    ; 0xc3183
    xor dx, dx                                ; 31 d2                       ; 0xc3186
    div si                                    ; f7 f6                       ; 0xc3188
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc318a
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc318d vgabios.c:52
    jmp short 031d4h                          ; eb 42                       ; 0xc3190 vgabios.c:2180
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc3192
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3195
    cmp AL, strict byte 013h                  ; 3c 13                       ; 0xc3198
    jne short 031adh                          ; 75 11                       ; 0xc319a
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc319c vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc319f
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc31a3 vgabios.c:2182
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc31a6 vgabios.c:62
    jmp short 031d4h                          ; eb 27                       ; 0xc31ab vgabios.c:2183
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc31ad
    jc short 031d4h                           ; 72 23                       ; 0xc31af
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc31b1
    jnbe short 031d4h                         ; 77 1f                       ; 0xc31b3
    cmp word [bp-00ah], strict byte 00000h    ; 83 7e f6 00                 ; 0xc31b5 vgabios.c:2185
    je short 031c9h                           ; 74 0e                       ; 0xc31b9
    mov ax, 04000h                            ; b8 00 40                    ; 0xc31bb vgabios.c:2186
    xor dx, dx                                ; 31 d2                       ; 0xc31be
    div word [bp-00ah]                        ; f7 76 f6                    ; 0xc31c0
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31c3 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc31c6
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc31c9 vgabios.c:2187
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31cc vgabios.c:62
    mov word [es:si], strict word 00004h      ; 26 c7 04 04 00              ; 0xc31cf
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc31d4 vgabios.c:2189
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc31d7
    je short 031dfh                           ; 74 04                       ; 0xc31d9
    cmp AL, strict byte 011h                  ; 3c 11                       ; 0xc31db
    jne short 031eah                          ; 75 0b                       ; 0xc31dd
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc31df vgabios.c:2190
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31e2 vgabios.c:62
    mov word [es:si], strict word 00002h      ; 26 c7 04 02 00              ; 0xc31e5
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc31ea vgabios.c:2192
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc31ed
    jc short 03246h                           ; 72 55                       ; 0xc31ef
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc31f1
    je short 03246h                           ; 74 51                       ; 0xc31f3
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc31f5 vgabios.c:2193
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31f8 vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc31fb
    mov si, 00084h                            ; be 84 00                    ; 0xc31ff vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3202
    mov es, ax                                ; 8e c0                       ; 0xc3205
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3207
    xor ah, ah                                ; 30 e4                       ; 0xc320a vgabios.c:48
    inc ax                                    ; 40                          ; 0xc320c
    mov si, 00085h                            ; be 85 00                    ; 0xc320d vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc3210
    xor dh, dh                                ; 30 f6                       ; 0xc3213 vgabios.c:48
    imul dx                                   ; f7 ea                       ; 0xc3215
    cmp ax, 0015eh                            ; 3d 5e 01                    ; 0xc3217 vgabios.c:2195
    jc short 0322ah                           ; 72 0e                       ; 0xc321a
    jbe short 03233h                          ; 76 15                       ; 0xc321c
    cmp ax, 001e0h                            ; 3d e0 01                    ; 0xc321e
    je short 0323bh                           ; 74 18                       ; 0xc3221
    cmp ax, 00190h                            ; 3d 90 01                    ; 0xc3223
    je short 03237h                           ; 74 0f                       ; 0xc3226
    jmp short 0323bh                          ; eb 11                       ; 0xc3228
    cmp ax, 000c8h                            ; 3d c8 00                    ; 0xc322a
    jne short 0323bh                          ; 75 0c                       ; 0xc322d
    xor al, al                                ; 30 c0                       ; 0xc322f vgabios.c:2196
    jmp short 0323dh                          ; eb 0a                       ; 0xc3231
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc3233 vgabios.c:2197
    jmp short 0323dh                          ; eb 06                       ; 0xc3235
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc3237 vgabios.c:2198
    jmp short 0323dh                          ; eb 02                       ; 0xc3239
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc323b vgabios.c:2200
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc323d vgabios.c:2202
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3240 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3243
    lea di, [bx+033h]                         ; 8d 7f 33                    ; 0xc3246 vgabios.c:2205
    mov cx, strict word 0000dh                ; b9 0d 00                    ; 0xc3249
    xor ax, ax                                ; 31 c0                       ; 0xc324c
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc324e
    jcxz 03255h                               ; e3 02                       ; 0xc3251
    rep stosb                                 ; f3 aa                       ; 0xc3253
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3255 vgabios.c:2206
    pop di                                    ; 5f                          ; 0xc3258
    pop si                                    ; 5e                          ; 0xc3259
    pop cx                                    ; 59                          ; 0xc325a
    pop bp                                    ; 5d                          ; 0xc325b
    retn                                      ; c3                          ; 0xc325c
  ; disGetNextSymbol 0xc325d LB 0x12e6 -> off=0x0 cb=0000000000000023 uValue=00000000000c325d 'biosfn_read_video_state_size2'
biosfn_read_video_state_size2:               ; 0xc325d LB 0x23
    push dx                                   ; 52                          ; 0xc325d vgabios.c:2209
    push bp                                   ; 55                          ; 0xc325e
    mov bp, sp                                ; 89 e5                       ; 0xc325f
    mov dx, ax                                ; 89 c2                       ; 0xc3261
    xor ax, ax                                ; 31 c0                       ; 0xc3263 vgabios.c:2213
    test dl, 001h                             ; f6 c2 01                    ; 0xc3265 vgabios.c:2214
    je short 0326dh                           ; 74 03                       ; 0xc3268
    mov ax, strict word 00046h                ; b8 46 00                    ; 0xc326a vgabios.c:2215
    test dl, 002h                             ; f6 c2 02                    ; 0xc326d vgabios.c:2217
    je short 03275h                           ; 74 03                       ; 0xc3270
    add ax, strict word 0002ah                ; 05 2a 00                    ; 0xc3272 vgabios.c:2218
    test dl, 004h                             ; f6 c2 04                    ; 0xc3275 vgabios.c:2220
    je short 0327dh                           ; 74 03                       ; 0xc3278
    add ax, 00304h                            ; 05 04 03                    ; 0xc327a vgabios.c:2221
    pop bp                                    ; 5d                          ; 0xc327d vgabios.c:2224
    pop dx                                    ; 5a                          ; 0xc327e
    retn                                      ; c3                          ; 0xc327f
  ; disGetNextSymbol 0xc3280 LB 0x12c3 -> off=0x0 cb=0000000000000018 uValue=00000000000c3280 'vga_get_video_state_size'
vga_get_video_state_size:                    ; 0xc3280 LB 0x18
    push bp                                   ; 55                          ; 0xc3280 vgabios.c:2226
    mov bp, sp                                ; 89 e5                       ; 0xc3281
    push bx                                   ; 53                          ; 0xc3283
    mov bx, dx                                ; 89 d3                       ; 0xc3284
    call 0325dh                               ; e8 d4 ff                    ; 0xc3286 vgabios.c:2229
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc3289
    shr ax, 006h                              ; c1 e8 06                    ; 0xc328c
    mov word [ss:bx], ax                      ; 36 89 07                    ; 0xc328f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3292 vgabios.c:2230
    pop bx                                    ; 5b                          ; 0xc3295
    pop bp                                    ; 5d                          ; 0xc3296
    retn                                      ; c3                          ; 0xc3297
  ; disGetNextSymbol 0xc3298 LB 0x12ab -> off=0x0 cb=00000000000002d8 uValue=00000000000c3298 'biosfn_save_video_state'
biosfn_save_video_state:                     ; 0xc3298 LB 0x2d8
    push bp                                   ; 55                          ; 0xc3298 vgabios.c:2232
    mov bp, sp                                ; 89 e5                       ; 0xc3299
    push cx                                   ; 51                          ; 0xc329b
    push si                                   ; 56                          ; 0xc329c
    push di                                   ; 57                          ; 0xc329d
    push ax                                   ; 50                          ; 0xc329e
    push ax                                   ; 50                          ; 0xc329f
    push ax                                   ; 50                          ; 0xc32a0
    mov cx, dx                                ; 89 d1                       ; 0xc32a1
    mov si, strict word 00063h                ; be 63 00                    ; 0xc32a3 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc32a6
    mov es, ax                                ; 8e c0                       ; 0xc32a9
    mov di, word [es:si]                      ; 26 8b 3c                    ; 0xc32ab
    mov si, di                                ; 89 fe                       ; 0xc32ae vgabios.c:58
    test byte [bp-00ch], 001h                 ; f6 46 f4 01                 ; 0xc32b0 vgabios.c:2237
    je short 0331ch                           ; 74 66                       ; 0xc32b4
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc32b6 vgabios.c:2238
    in AL, DX                                 ; ec                          ; 0xc32b9
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32ba
    mov es, cx                                ; 8e c1                       ; 0xc32bc vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32be
    inc bx                                    ; 43                          ; 0xc32c1 vgabios.c:2238
    mov dx, di                                ; 89 fa                       ; 0xc32c2
    in AL, DX                                 ; ec                          ; 0xc32c4
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32c5
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32c7 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc32ca vgabios.c:2239
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc32cb
    in AL, DX                                 ; ec                          ; 0xc32ce
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32cf
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32d1 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc32d4 vgabios.c:2240
    mov dx, 003dah                            ; ba da 03                    ; 0xc32d5
    in AL, DX                                 ; ec                          ; 0xc32d8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32d9
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc32db vgabios.c:2242
    in AL, DX                                 ; ec                          ; 0xc32de
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32df
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc32e1
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc32e4 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32e7
    inc bx                                    ; 43                          ; 0xc32ea vgabios.c:2243
    mov dx, 003cah                            ; ba ca 03                    ; 0xc32eb
    in AL, DX                                 ; ec                          ; 0xc32ee
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32ef
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32f1 vgabios.c:52
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc32f4 vgabios.c:2246
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc32f7
    add bx, ax                                ; 01 c3                       ; 0xc32fa vgabios.c:2244
    jmp short 03304h                          ; eb 06                       ; 0xc32fc
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc32fe
    jnbe short 0331fh                         ; 77 1b                       ; 0xc3302
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3304 vgabios.c:2247
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3307
    out DX, AL                                ; ee                          ; 0xc330a
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc330b vgabios.c:2248
    in AL, DX                                 ; ec                          ; 0xc330e
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc330f
    mov es, cx                                ; 8e c1                       ; 0xc3311 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3313
    inc bx                                    ; 43                          ; 0xc3316 vgabios.c:2248
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3317 vgabios.c:2249
    jmp short 032feh                          ; eb e2                       ; 0xc331a
    jmp near 033cch                           ; e9 ad 00                    ; 0xc331c
    xor al, al                                ; 30 c0                       ; 0xc331f vgabios.c:2250
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3321
    out DX, AL                                ; ee                          ; 0xc3324
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3325 vgabios.c:2251
    in AL, DX                                 ; ec                          ; 0xc3328
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3329
    mov es, cx                                ; 8e c1                       ; 0xc332b vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc332d
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3330 vgabios.c:2253
    inc bx                                    ; 43                          ; 0xc3335 vgabios.c:2251
    jmp short 0333eh                          ; eb 06                       ; 0xc3336
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc3338
    jnbe short 03355h                         ; 77 17                       ; 0xc333c
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc333e vgabios.c:2254
    mov dx, si                                ; 89 f2                       ; 0xc3341
    out DX, AL                                ; ee                          ; 0xc3343
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc3344 vgabios.c:2255
    in AL, DX                                 ; ec                          ; 0xc3347
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3348
    mov es, cx                                ; 8e c1                       ; 0xc334a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc334c
    inc bx                                    ; 43                          ; 0xc334f vgabios.c:2255
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3350 vgabios.c:2256
    jmp short 03338h                          ; eb e3                       ; 0xc3353
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3355 vgabios.c:2258
    jmp short 03362h                          ; eb 06                       ; 0xc335a
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc335c
    jnbe short 03386h                         ; 77 24                       ; 0xc3360
    mov dx, 003dah                            ; ba da 03                    ; 0xc3362 vgabios.c:2259
    in AL, DX                                 ; ec                          ; 0xc3365
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3366
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3368 vgabios.c:2260
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc336b
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc336e
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3371
    out DX, AL                                ; ee                          ; 0xc3374
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc3375 vgabios.c:2261
    in AL, DX                                 ; ec                          ; 0xc3378
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3379
    mov es, cx                                ; 8e c1                       ; 0xc337b vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc337d
    inc bx                                    ; 43                          ; 0xc3380 vgabios.c:2261
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3381 vgabios.c:2262
    jmp short 0335ch                          ; eb d6                       ; 0xc3384
    mov dx, 003dah                            ; ba da 03                    ; 0xc3386 vgabios.c:2263
    in AL, DX                                 ; ec                          ; 0xc3389
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc338a
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc338c vgabios.c:2265
    jmp short 03399h                          ; eb 06                       ; 0xc3391
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc3393
    jnbe short 033b1h                         ; 77 18                       ; 0xc3397
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3399 vgabios.c:2266
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc339c
    out DX, AL                                ; ee                          ; 0xc339f
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc33a0 vgabios.c:2267
    in AL, DX                                 ; ec                          ; 0xc33a3
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc33a4
    mov es, cx                                ; 8e c1                       ; 0xc33a6 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc33a8
    inc bx                                    ; 43                          ; 0xc33ab vgabios.c:2267
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc33ac vgabios.c:2268
    jmp short 03393h                          ; eb e2                       ; 0xc33af
    mov es, cx                                ; 8e c1                       ; 0xc33b1 vgabios.c:62
    mov word [es:bx], si                      ; 26 89 37                    ; 0xc33b3
    inc bx                                    ; 43                          ; 0xc33b6 vgabios.c:2270
    inc bx                                    ; 43                          ; 0xc33b7
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33b8 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33bc vgabios.c:2273
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33bd vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33c1 vgabios.c:2274
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33c2 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33c6 vgabios.c:2275
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33c7 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33cb vgabios.c:2276
    test byte [bp-00ch], 002h                 ; f6 46 f4 02                 ; 0xc33cc vgabios.c:2278
    jne short 033d5h                          ; 75 03                       ; 0xc33d0
    jmp near 03514h                           ; e9 3f 01                    ; 0xc33d2
    mov si, strict word 00049h                ; be 49 00                    ; 0xc33d5 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc33d8
    mov es, ax                                ; 8e c0                       ; 0xc33db
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc33dd
    mov es, cx                                ; 8e c1                       ; 0xc33e0 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc33e2
    inc bx                                    ; 43                          ; 0xc33e5 vgabios.c:2279
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc33e6 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc33e9
    mov es, ax                                ; 8e c0                       ; 0xc33ec
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc33ee
    mov es, cx                                ; 8e c1                       ; 0xc33f1 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc33f3
    inc bx                                    ; 43                          ; 0xc33f6 vgabios.c:2280
    inc bx                                    ; 43                          ; 0xc33f7
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc33f8 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc33fb
    mov es, ax                                ; 8e c0                       ; 0xc33fe
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3400
    mov es, cx                                ; 8e c1                       ; 0xc3403 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3405
    inc bx                                    ; 43                          ; 0xc3408 vgabios.c:2281
    inc bx                                    ; 43                          ; 0xc3409
    mov si, strict word 00063h                ; be 63 00                    ; 0xc340a vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc340d
    mov es, ax                                ; 8e c0                       ; 0xc3410
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3412
    mov es, cx                                ; 8e c1                       ; 0xc3415 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3417
    inc bx                                    ; 43                          ; 0xc341a vgabios.c:2282
    inc bx                                    ; 43                          ; 0xc341b
    mov si, 00084h                            ; be 84 00                    ; 0xc341c vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc341f
    mov es, ax                                ; 8e c0                       ; 0xc3422
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3424
    mov es, cx                                ; 8e c1                       ; 0xc3427 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3429
    inc bx                                    ; 43                          ; 0xc342c vgabios.c:2283
    mov si, 00085h                            ; be 85 00                    ; 0xc342d vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3430
    mov es, ax                                ; 8e c0                       ; 0xc3433
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3435
    mov es, cx                                ; 8e c1                       ; 0xc3438 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc343a
    inc bx                                    ; 43                          ; 0xc343d vgabios.c:2284
    inc bx                                    ; 43                          ; 0xc343e
    mov si, 00087h                            ; be 87 00                    ; 0xc343f vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3442
    mov es, ax                                ; 8e c0                       ; 0xc3445
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3447
    mov es, cx                                ; 8e c1                       ; 0xc344a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc344c
    inc bx                                    ; 43                          ; 0xc344f vgabios.c:2285
    mov si, 00088h                            ; be 88 00                    ; 0xc3450 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3453
    mov es, ax                                ; 8e c0                       ; 0xc3456
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3458
    mov es, cx                                ; 8e c1                       ; 0xc345b vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc345d
    inc bx                                    ; 43                          ; 0xc3460 vgabios.c:2286
    mov si, 00089h                            ; be 89 00                    ; 0xc3461 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3464
    mov es, ax                                ; 8e c0                       ; 0xc3467
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3469
    mov es, cx                                ; 8e c1                       ; 0xc346c vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc346e
    inc bx                                    ; 43                          ; 0xc3471 vgabios.c:2287
    mov si, strict word 00060h                ; be 60 00                    ; 0xc3472 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3475
    mov es, ax                                ; 8e c0                       ; 0xc3478
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc347a
    mov es, cx                                ; 8e c1                       ; 0xc347d vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc347f
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3482 vgabios.c:2289
    inc bx                                    ; 43                          ; 0xc3487 vgabios.c:2288
    inc bx                                    ; 43                          ; 0xc3488
    jmp short 03491h                          ; eb 06                       ; 0xc3489
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc348b
    jnc short 034adh                          ; 73 1c                       ; 0xc348f
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc3491 vgabios.c:2290
    add si, si                                ; 01 f6                       ; 0xc3494
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc3496
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3499 vgabios.c:57
    mov es, ax                                ; 8e c0                       ; 0xc349c
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc349e
    mov es, cx                                ; 8e c1                       ; 0xc34a1 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34a3
    inc bx                                    ; 43                          ; 0xc34a6 vgabios.c:2291
    inc bx                                    ; 43                          ; 0xc34a7
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc34a8 vgabios.c:2292
    jmp short 0348bh                          ; eb de                       ; 0xc34ab
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc34ad vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34b0
    mov es, ax                                ; 8e c0                       ; 0xc34b3
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34b5
    mov es, cx                                ; 8e c1                       ; 0xc34b8 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34ba
    inc bx                                    ; 43                          ; 0xc34bd vgabios.c:2293
    inc bx                                    ; 43                          ; 0xc34be
    mov si, strict word 00062h                ; be 62 00                    ; 0xc34bf vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34c2
    mov es, ax                                ; 8e c0                       ; 0xc34c5
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc34c7
    mov es, cx                                ; 8e c1                       ; 0xc34ca vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc34cc
    inc bx                                    ; 43                          ; 0xc34cf vgabios.c:2294
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc34d0 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc34d3
    mov es, ax                                ; 8e c0                       ; 0xc34d5
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34d7
    mov es, cx                                ; 8e c1                       ; 0xc34da vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34dc
    inc bx                                    ; 43                          ; 0xc34df vgabios.c:2296
    inc bx                                    ; 43                          ; 0xc34e0
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc34e1 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc34e4
    mov es, ax                                ; 8e c0                       ; 0xc34e6
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34e8
    mov es, cx                                ; 8e c1                       ; 0xc34eb vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34ed
    inc bx                                    ; 43                          ; 0xc34f0 vgabios.c:2297
    inc bx                                    ; 43                          ; 0xc34f1
    mov si, 0010ch                            ; be 0c 01                    ; 0xc34f2 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc34f5
    mov es, ax                                ; 8e c0                       ; 0xc34f7
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34f9
    mov es, cx                                ; 8e c1                       ; 0xc34fc vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34fe
    inc bx                                    ; 43                          ; 0xc3501 vgabios.c:2298
    inc bx                                    ; 43                          ; 0xc3502
    mov si, 0010eh                            ; be 0e 01                    ; 0xc3503 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc3506
    mov es, ax                                ; 8e c0                       ; 0xc3508
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc350a
    mov es, cx                                ; 8e c1                       ; 0xc350d vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc350f
    inc bx                                    ; 43                          ; 0xc3512 vgabios.c:2299
    inc bx                                    ; 43                          ; 0xc3513
    test byte [bp-00ch], 004h                 ; f6 46 f4 04                 ; 0xc3514 vgabios.c:2301
    je short 03566h                           ; 74 4c                       ; 0xc3518
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc351a vgabios.c:2303
    in AL, DX                                 ; ec                          ; 0xc351d
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc351e
    mov es, cx                                ; 8e c1                       ; 0xc3520 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3522
    inc bx                                    ; 43                          ; 0xc3525 vgabios.c:2303
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc3526
    in AL, DX                                 ; ec                          ; 0xc3529
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc352a
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc352c vgabios.c:52
    inc bx                                    ; 43                          ; 0xc352f vgabios.c:2304
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc3530
    in AL, DX                                 ; ec                          ; 0xc3533
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3534
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3536 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3539 vgabios.c:2305
    xor al, al                                ; 30 c0                       ; 0xc353a
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc353c
    out DX, AL                                ; ee                          ; 0xc353f
    xor ah, ah                                ; 30 e4                       ; 0xc3540 vgabios.c:2308
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3542
    jmp short 0354eh                          ; eb 07                       ; 0xc3545
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc3547
    jnc short 0355fh                          ; 73 11                       ; 0xc354c
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc354e vgabios.c:2309
    in AL, DX                                 ; ec                          ; 0xc3551
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3552
    mov es, cx                                ; 8e c1                       ; 0xc3554 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3556
    inc bx                                    ; 43                          ; 0xc3559 vgabios.c:2309
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc355a vgabios.c:2310
    jmp short 03547h                          ; eb e8                       ; 0xc355d
    mov es, cx                                ; 8e c1                       ; 0xc355f vgabios.c:52
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3561
    inc bx                                    ; 43                          ; 0xc3565 vgabios.c:2311
    mov ax, bx                                ; 89 d8                       ; 0xc3566 vgabios.c:2314
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3568
    pop di                                    ; 5f                          ; 0xc356b
    pop si                                    ; 5e                          ; 0xc356c
    pop cx                                    ; 59                          ; 0xc356d
    pop bp                                    ; 5d                          ; 0xc356e
    retn                                      ; c3                          ; 0xc356f
  ; disGetNextSymbol 0xc3570 LB 0xfd3 -> off=0x0 cb=00000000000002ba uValue=00000000000c3570 'biosfn_restore_video_state'
biosfn_restore_video_state:                  ; 0xc3570 LB 0x2ba
    push bp                                   ; 55                          ; 0xc3570 vgabios.c:2316
    mov bp, sp                                ; 89 e5                       ; 0xc3571
    push cx                                   ; 51                          ; 0xc3573
    push si                                   ; 56                          ; 0xc3574
    push di                                   ; 57                          ; 0xc3575
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc3576
    push ax                                   ; 50                          ; 0xc3579
    mov cx, dx                                ; 89 d1                       ; 0xc357a
    test byte [bp-010h], 001h                 ; f6 46 f0 01                 ; 0xc357c vgabios.c:2320
    je short 035f6h                           ; 74 74                       ; 0xc3580
    mov dx, 003dah                            ; ba da 03                    ; 0xc3582 vgabios.c:2322
    in AL, DX                                 ; ec                          ; 0xc3585
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3586
    lea si, [bx+040h]                         ; 8d 77 40                    ; 0xc3588 vgabios.c:2324
    mov es, cx                                ; 8e c1                       ; 0xc358b vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc358d
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc3590 vgabios.c:58
    mov si, bx                                ; 89 de                       ; 0xc3593 vgabios.c:2325
    mov word [bp-008h], strict word 00001h    ; c7 46 f8 01 00              ; 0xc3595 vgabios.c:2328
    add bx, strict byte 00005h                ; 83 c3 05                    ; 0xc359a vgabios.c:2326
    jmp short 035a5h                          ; eb 06                       ; 0xc359d
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc359f
    jnbe short 035bbh                         ; 77 16                       ; 0xc35a3
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc35a5 vgabios.c:2329
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc35a8
    out DX, AL                                ; ee                          ; 0xc35ab
    mov es, cx                                ; 8e c1                       ; 0xc35ac vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35ae
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc35b1 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35b4
    inc bx                                    ; 43                          ; 0xc35b5 vgabios.c:2330
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc35b6 vgabios.c:2331
    jmp short 0359fh                          ; eb e4                       ; 0xc35b9
    xor al, al                                ; 30 c0                       ; 0xc35bb vgabios.c:2332
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc35bd
    out DX, AL                                ; ee                          ; 0xc35c0
    mov es, cx                                ; 8e c1                       ; 0xc35c1 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35c3
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc35c6 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35c9
    inc bx                                    ; 43                          ; 0xc35ca vgabios.c:2333
    mov dx, 003cch                            ; ba cc 03                    ; 0xc35cb
    in AL, DX                                 ; ec                          ; 0xc35ce
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc35cf
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc35d1
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc35d3
    cmp word [bp-00ch], 003d4h                ; 81 7e f4 d4 03              ; 0xc35d6 vgabios.c:2337
    jne short 035e1h                          ; 75 04                       ; 0xc35db
    or byte [bp-00eh], 001h                   ; 80 4e f2 01                 ; 0xc35dd vgabios.c:2338
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc35e1 vgabios.c:2339
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc35e4
    out DX, AL                                ; ee                          ; 0xc35e7
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc35e8 vgabios.c:2342
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc35eb
    out DX, ax                                ; ef                          ; 0xc35ee
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc35ef vgabios.c:2344
    jmp short 035ffh                          ; eb 09                       ; 0xc35f4
    jmp near 036b9h                           ; e9 c0 00                    ; 0xc35f6
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc35f9
    jnbe short 03619h                         ; 77 1a                       ; 0xc35fd
    cmp word [bp-008h], strict byte 00011h    ; 83 7e f8 11                 ; 0xc35ff vgabios.c:2345
    je short 03613h                           ; 74 0e                       ; 0xc3603
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3605 vgabios.c:2346
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc3608
    out DX, AL                                ; ee                          ; 0xc360b
    mov es, cx                                ; 8e c1                       ; 0xc360c vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc360e
    inc dx                                    ; 42                          ; 0xc3611 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3612
    inc bx                                    ; 43                          ; 0xc3613 vgabios.c:2349
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3614 vgabios.c:2350
    jmp short 035f9h                          ; eb e0                       ; 0xc3617
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc3619 vgabios.c:2352
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc361b
    out DX, AL                                ; ee                          ; 0xc361e
    lea di, [word bx-00007h]                  ; 8d bf f9 ff                 ; 0xc361f vgabios.c:2353
    mov es, cx                                ; 8e c1                       ; 0xc3623 vgabios.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc3625
    inc dx                                    ; 42                          ; 0xc3628 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3629
    lea di, [si+003h]                         ; 8d 7c 03                    ; 0xc362a vgabios.c:2356
    mov dl, byte [es:di]                      ; 26 8a 15                    ; 0xc362d vgabios.c:47
    xor dh, dh                                ; 30 f6                       ; 0xc3630 vgabios.c:48
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3632
    mov dx, 003dah                            ; ba da 03                    ; 0xc3635 vgabios.c:2357
    in AL, DX                                 ; ec                          ; 0xc3638
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3639
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc363b vgabios.c:2358
    jmp short 03648h                          ; eb 06                       ; 0xc3640
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc3642
    jnbe short 03661h                         ; 77 19                       ; 0xc3646
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3648 vgabios.c:2359
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc364b
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc364e
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3651
    out DX, AL                                ; ee                          ; 0xc3654
    mov es, cx                                ; 8e c1                       ; 0xc3655 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3657
    out DX, AL                                ; ee                          ; 0xc365a vgabios.c:48
    inc bx                                    ; 43                          ; 0xc365b vgabios.c:2360
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc365c vgabios.c:2361
    jmp short 03642h                          ; eb e1                       ; 0xc365f
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc3661 vgabios.c:2362
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3664
    out DX, AL                                ; ee                          ; 0xc3667
    mov dx, 003dah                            ; ba da 03                    ; 0xc3668 vgabios.c:2363
    in AL, DX                                 ; ec                          ; 0xc366b
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc366c
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc366e vgabios.c:2365
    jmp short 0367bh                          ; eb 06                       ; 0xc3673
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc3675
    jnbe short 03691h                         ; 77 16                       ; 0xc3679
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc367b vgabios.c:2366
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc367e
    out DX, AL                                ; ee                          ; 0xc3681
    mov es, cx                                ; 8e c1                       ; 0xc3682 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3684
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc3687 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc368a
    inc bx                                    ; 43                          ; 0xc368b vgabios.c:2367
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc368c vgabios.c:2368
    jmp short 03675h                          ; eb e4                       ; 0xc368f
    add bx, strict byte 00006h                ; 83 c3 06                    ; 0xc3691 vgabios.c:2369
    mov es, cx                                ; 8e c1                       ; 0xc3694 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3696
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3699 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc369c
    inc si                                    ; 46                          ; 0xc369d vgabios.c:2372
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc369e vgabios.c:47
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc36a1 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc36a4
    inc si                                    ; 46                          ; 0xc36a5 vgabios.c:2373
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc36a6 vgabios.c:47
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc36a9 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc36ac
    inc si                                    ; 46                          ; 0xc36ad vgabios.c:2374
    inc si                                    ; 46                          ; 0xc36ae
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc36af vgabios.c:47
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc36b2 vgabios.c:48
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc36b5
    out DX, AL                                ; ee                          ; 0xc36b8
    test byte [bp-010h], 002h                 ; f6 46 f0 02                 ; 0xc36b9 vgabios.c:2378
    jne short 036c2h                          ; 75 03                       ; 0xc36bd
    jmp near 037ddh                           ; e9 1b 01                    ; 0xc36bf
    mov es, cx                                ; 8e c1                       ; 0xc36c2 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc36c4
    mov si, strict word 00049h                ; be 49 00                    ; 0xc36c7 vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc36ca
    mov es, dx                                ; 8e c2                       ; 0xc36cd
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc36cf
    inc bx                                    ; 43                          ; 0xc36d2 vgabios.c:2379
    mov es, cx                                ; 8e c1                       ; 0xc36d3 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc36d5
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc36d8 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc36db
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc36dd
    inc bx                                    ; 43                          ; 0xc36e0 vgabios.c:2380
    inc bx                                    ; 43                          ; 0xc36e1
    mov es, cx                                ; 8e c1                       ; 0xc36e2 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc36e4
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc36e7 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc36ea
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc36ec
    inc bx                                    ; 43                          ; 0xc36ef vgabios.c:2381
    inc bx                                    ; 43                          ; 0xc36f0
    mov es, cx                                ; 8e c1                       ; 0xc36f1 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc36f3
    mov si, strict word 00063h                ; be 63 00                    ; 0xc36f6 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc36f9
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc36fb
    inc bx                                    ; 43                          ; 0xc36fe vgabios.c:2382
    inc bx                                    ; 43                          ; 0xc36ff
    mov es, cx                                ; 8e c1                       ; 0xc3700 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3702
    mov si, 00084h                            ; be 84 00                    ; 0xc3705 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3708
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc370a
    inc bx                                    ; 43                          ; 0xc370d vgabios.c:2383
    mov es, cx                                ; 8e c1                       ; 0xc370e vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3710
    mov si, 00085h                            ; be 85 00                    ; 0xc3713 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3716
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3718
    inc bx                                    ; 43                          ; 0xc371b vgabios.c:2384
    inc bx                                    ; 43                          ; 0xc371c
    mov es, cx                                ; 8e c1                       ; 0xc371d vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc371f
    mov si, 00087h                            ; be 87 00                    ; 0xc3722 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3725
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3727
    inc bx                                    ; 43                          ; 0xc372a vgabios.c:2385
    mov es, cx                                ; 8e c1                       ; 0xc372b vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc372d
    mov si, 00088h                            ; be 88 00                    ; 0xc3730 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3733
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3735
    inc bx                                    ; 43                          ; 0xc3738 vgabios.c:2386
    mov es, cx                                ; 8e c1                       ; 0xc3739 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc373b
    mov si, 00089h                            ; be 89 00                    ; 0xc373e vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3741
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3743
    inc bx                                    ; 43                          ; 0xc3746 vgabios.c:2387
    mov es, cx                                ; 8e c1                       ; 0xc3747 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3749
    mov si, strict word 00060h                ; be 60 00                    ; 0xc374c vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc374f
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3751
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3754 vgabios.c:2389
    inc bx                                    ; 43                          ; 0xc3759 vgabios.c:2388
    inc bx                                    ; 43                          ; 0xc375a
    jmp short 03763h                          ; eb 06                       ; 0xc375b
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc375d
    jnc short 0377fh                          ; 73 1c                       ; 0xc3761
    mov es, cx                                ; 8e c1                       ; 0xc3763 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3765
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc3768 vgabios.c:58
    add si, si                                ; 01 f6                       ; 0xc376b
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc376d
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3770 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3773
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3775
    inc bx                                    ; 43                          ; 0xc3778 vgabios.c:2391
    inc bx                                    ; 43                          ; 0xc3779
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc377a vgabios.c:2392
    jmp short 0375dh                          ; eb de                       ; 0xc377d
    mov es, cx                                ; 8e c1                       ; 0xc377f vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3781
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc3784 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3787
    mov es, dx                                ; 8e c2                       ; 0xc378a
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc378c
    inc bx                                    ; 43                          ; 0xc378f vgabios.c:2393
    inc bx                                    ; 43                          ; 0xc3790
    mov es, cx                                ; 8e c1                       ; 0xc3791 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3793
    mov si, strict word 00062h                ; be 62 00                    ; 0xc3796 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3799
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc379b
    inc bx                                    ; 43                          ; 0xc379e vgabios.c:2394
    mov es, cx                                ; 8e c1                       ; 0xc379f vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37a1
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc37a4 vgabios.c:62
    xor dx, dx                                ; 31 d2                       ; 0xc37a7
    mov es, dx                                ; 8e c2                       ; 0xc37a9
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37ab
    inc bx                                    ; 43                          ; 0xc37ae vgabios.c:2396
    inc bx                                    ; 43                          ; 0xc37af
    mov es, cx                                ; 8e c1                       ; 0xc37b0 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37b2
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc37b5 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37b8
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37ba
    inc bx                                    ; 43                          ; 0xc37bd vgabios.c:2397
    inc bx                                    ; 43                          ; 0xc37be
    mov es, cx                                ; 8e c1                       ; 0xc37bf vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37c1
    mov si, 0010ch                            ; be 0c 01                    ; 0xc37c4 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37c7
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37c9
    inc bx                                    ; 43                          ; 0xc37cc vgabios.c:2398
    inc bx                                    ; 43                          ; 0xc37cd
    mov es, cx                                ; 8e c1                       ; 0xc37ce vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37d0
    mov si, 0010eh                            ; be 0e 01                    ; 0xc37d3 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37d6
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37d8
    inc bx                                    ; 43                          ; 0xc37db vgabios.c:2399
    inc bx                                    ; 43                          ; 0xc37dc
    test byte [bp-010h], 004h                 ; f6 46 f0 04                 ; 0xc37dd vgabios.c:2401
    je short 03820h                           ; 74 3d                       ; 0xc37e1
    inc bx                                    ; 43                          ; 0xc37e3 vgabios.c:2402
    mov es, cx                                ; 8e c1                       ; 0xc37e4 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc37e6
    xor ah, ah                                ; 30 e4                       ; 0xc37e9 vgabios.c:48
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc37eb
    inc bx                                    ; 43                          ; 0xc37ee vgabios.c:2403
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc37ef vgabios.c:47
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc37f2 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc37f5
    inc bx                                    ; 43                          ; 0xc37f6 vgabios.c:2404
    xor al, al                                ; 30 c0                       ; 0xc37f7
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc37f9
    out DX, AL                                ; ee                          ; 0xc37fc
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc37fd vgabios.c:2407
    jmp short 03809h                          ; eb 07                       ; 0xc3800
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc3802
    jnc short 03818h                          ; 73 0f                       ; 0xc3807
    mov es, cx                                ; 8e c1                       ; 0xc3809 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc380b
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc380e vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3811
    inc bx                                    ; 43                          ; 0xc3812 vgabios.c:2408
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3813 vgabios.c:2409
    jmp short 03802h                          ; eb ea                       ; 0xc3816
    inc bx                                    ; 43                          ; 0xc3818 vgabios.c:2410
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc3819
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc381c
    out DX, AL                                ; ee                          ; 0xc381f
    mov ax, bx                                ; 89 d8                       ; 0xc3820 vgabios.c:2414
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3822
    pop di                                    ; 5f                          ; 0xc3825
    pop si                                    ; 5e                          ; 0xc3826
    pop cx                                    ; 59                          ; 0xc3827
    pop bp                                    ; 5d                          ; 0xc3828
    retn                                      ; c3                          ; 0xc3829
  ; disGetNextSymbol 0xc382a LB 0xd19 -> off=0x0 cb=0000000000000028 uValue=00000000000c382a 'find_vga_entry'
find_vga_entry:                              ; 0xc382a LB 0x28
    push bx                                   ; 53                          ; 0xc382a vgabios.c:2423
    push dx                                   ; 52                          ; 0xc382b
    push bp                                   ; 55                          ; 0xc382c
    mov bp, sp                                ; 89 e5                       ; 0xc382d
    mov dl, al                                ; 88 c2                       ; 0xc382f
    mov AH, strict byte 0ffh                  ; b4 ff                       ; 0xc3831 vgabios.c:2425
    xor al, al                                ; 30 c0                       ; 0xc3833 vgabios.c:2426
    jmp short 0383dh                          ; eb 06                       ; 0xc3835
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc3837 vgabios.c:2427
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc3839
    jnbe short 0384ch                         ; 77 0f                       ; 0xc383b
    mov bl, al                                ; 88 c3                       ; 0xc383d
    xor bh, bh                                ; 30 ff                       ; 0xc383f
    sal bx, 003h                              ; c1 e3 03                    ; 0xc3841
    cmp dl, byte [bx+047ach]                  ; 3a 97 ac 47                 ; 0xc3844
    jne short 03837h                          ; 75 ed                       ; 0xc3848
    mov ah, al                                ; 88 c4                       ; 0xc384a
    mov al, ah                                ; 88 e0                       ; 0xc384c vgabios.c:2432
    pop bp                                    ; 5d                          ; 0xc384e
    pop dx                                    ; 5a                          ; 0xc384f
    pop bx                                    ; 5b                          ; 0xc3850
    retn                                      ; c3                          ; 0xc3851
  ; disGetNextSymbol 0xc3852 LB 0xcf1 -> off=0x0 cb=000000000000000e uValue=00000000000c3852 'readx_byte'
readx_byte:                                  ; 0xc3852 LB 0xe
    push bx                                   ; 53                          ; 0xc3852 vgabios.c:2444
    push bp                                   ; 55                          ; 0xc3853
    mov bp, sp                                ; 89 e5                       ; 0xc3854
    mov bx, dx                                ; 89 d3                       ; 0xc3856
    mov es, ax                                ; 8e c0                       ; 0xc3858 vgabios.c:2446
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc385a
    pop bp                                    ; 5d                          ; 0xc385d vgabios.c:2447
    pop bx                                    ; 5b                          ; 0xc385e
    retn                                      ; c3                          ; 0xc385f
  ; disGetNextSymbol 0xc3860 LB 0xce3 -> off=0x8a cb=000000000000049f uValue=00000000000c38ea 'int10_func'
    db  056h, 04fh, 01ch, 01bh, 013h, 012h, 011h, 010h, 00eh, 00dh, 00ch, 00ah, 009h, 008h, 007h, 006h
    db  005h, 004h, 003h, 002h, 001h, 000h, 082h, 03dh, 013h, 039h, 050h, 039h, 065h, 039h, 075h, 039h
    db  088h, 039h, 098h, 039h, 0a2h, 039h, 0e4h, 039h, 018h, 03ah, 029h, 03ah, 04fh, 03ah, 06ah, 03ah
    db  089h, 03ah, 0a6h, 03ah, 0bch, 03ah, 0c8h, 03ah, 0c1h, 03bh, 045h, 03ch, 072h, 03ch, 087h, 03ch
    db  0c9h, 03ch, 054h, 03dh, 030h, 024h, 023h, 022h, 021h, 020h, 014h, 012h, 011h, 010h, 004h, 003h
    db  002h, 001h, 000h, 082h, 03dh, 0e7h, 03ah, 005h, 03bh, 020h, 03bh, 035h, 03bh, 040h, 03bh, 0e7h
    db  03ah, 005h, 03bh, 020h, 03bh, 040h, 03bh, 055h, 03bh, 060h, 03bh, 07bh, 03bh, 08ah, 03bh, 099h
    db  03bh, 0a8h, 03bh, 00ah, 009h, 006h, 004h, 002h, 001h, 000h, 046h, 03dh, 0efh, 03ch, 0fdh, 03ch
    db  00eh, 03dh, 01eh, 03dh, 033h, 03dh, 046h, 03dh, 046h, 03dh
int10_func:                                  ; 0xc38ea LB 0x49f
    push bp                                   ; 55                          ; 0xc38ea vgabios.c:2525
    mov bp, sp                                ; 89 e5                       ; 0xc38eb
    push si                                   ; 56                          ; 0xc38ed
    push di                                   ; 57                          ; 0xc38ee
    push ax                                   ; 50                          ; 0xc38ef
    mov si, word [bp+004h]                    ; 8b 76 04                    ; 0xc38f0
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc38f3 vgabios.c:2530
    shr ax, 008h                              ; c1 e8 08                    ; 0xc38f6
    cmp ax, strict word 00056h                ; 3d 56 00                    ; 0xc38f9
    jnbe short 03962h                         ; 77 64                       ; 0xc38fc
    push CS                                   ; 0e                          ; 0xc38fe
    pop ES                                    ; 07                          ; 0xc38ff
    mov cx, strict word 00017h                ; b9 17 00                    ; 0xc3900
    mov di, 03860h                            ; bf 60 38                    ; 0xc3903
    repne scasb                               ; f2 ae                       ; 0xc3906
    sal cx, 1                                 ; d1 e1                       ; 0xc3908
    mov di, cx                                ; 89 cf                       ; 0xc390a
    mov ax, word [cs:di+03876h]               ; 2e 8b 85 76 38              ; 0xc390c
    jmp ax                                    ; ff e0                       ; 0xc3911
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3913 vgabios.c:2533
    xor ah, ah                                ; 30 e4                       ; 0xc3916
    call 0142dh                               ; e8 12 db                    ; 0xc3918
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc391b vgabios.c:2534
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc391e
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc3921
    je short 0393bh                           ; 74 15                       ; 0xc3924
    cmp ax, strict word 00006h                ; 3d 06 00                    ; 0xc3926
    je short 03932h                           ; 74 07                       ; 0xc3929
    cmp ax, strict word 00005h                ; 3d 05 00                    ; 0xc392b
    jbe short 0393bh                          ; 76 0b                       ; 0xc392e
    jmp short 03944h                          ; eb 12                       ; 0xc3930
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3932 vgabios.c:2536
    xor al, al                                ; 30 c0                       ; 0xc3935
    or AL, strict byte 03fh                   ; 0c 3f                       ; 0xc3937
    jmp short 0394bh                          ; eb 10                       ; 0xc3939 vgabios.c:2537
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc393b vgabios.c:2545
    xor al, al                                ; 30 c0                       ; 0xc393e
    or AL, strict byte 030h                   ; 0c 30                       ; 0xc3940
    jmp short 0394bh                          ; eb 07                       ; 0xc3942
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3944 vgabios.c:2548
    xor al, al                                ; 30 c0                       ; 0xc3947
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc3949
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc394b
    jmp short 03962h                          ; eb 12                       ; 0xc394e vgabios.c:2550
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc3950 vgabios.c:2552
    xor ah, ah                                ; 30 e4                       ; 0xc3953
    mov dx, ax                                ; 89 c2                       ; 0xc3955
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3957
    shr ax, 008h                              ; c1 e8 08                    ; 0xc395a
    xor ah, ah                                ; 30 e4                       ; 0xc395d
    call 0117ah                               ; e8 18 d8                    ; 0xc395f
    jmp near 03d82h                           ; e9 1d 04                    ; 0xc3962 vgabios.c:2553
    mov dx, word [bp+00eh]                    ; 8b 56 0e                    ; 0xc3965 vgabios.c:2555
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3968
    shr ax, 008h                              ; c1 e8 08                    ; 0xc396b
    xor ah, ah                                ; 30 e4                       ; 0xc396e
    call 01281h                               ; e8 0e d9                    ; 0xc3970
    jmp short 03962h                          ; eb ed                       ; 0xc3973 vgabios.c:2556
    lea bx, [bp+00eh]                         ; 8d 5e 0e                    ; 0xc3975 vgabios.c:2558
    lea dx, [bp+010h]                         ; 8d 56 10                    ; 0xc3978
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc397b
    shr ax, 008h                              ; c1 e8 08                    ; 0xc397e
    xor ah, ah                                ; 30 e4                       ; 0xc3981
    call 00a96h                               ; e8 10 d1                    ; 0xc3983
    jmp short 03962h                          ; eb da                       ; 0xc3986 vgabios.c:2559
    xor ax, ax                                ; 31 c0                       ; 0xc3988 vgabios.c:2565
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc398a
    mov word [bp+00ch], ax                    ; 89 46 0c                    ; 0xc398d vgabios.c:2566
    mov word [bp+010h], ax                    ; 89 46 10                    ; 0xc3990 vgabios.c:2567
    mov word [bp+00eh], ax                    ; 89 46 0e                    ; 0xc3993 vgabios.c:2568
    jmp short 03962h                          ; eb ca                       ; 0xc3996 vgabios.c:2569
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3998 vgabios.c:2571
    xor ah, ah                                ; 30 e4                       ; 0xc399b
    call 01310h                               ; e8 70 d9                    ; 0xc399d
    jmp short 03962h                          ; eb c0                       ; 0xc39a0 vgabios.c:2572
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc39a2 vgabios.c:2574
    push ax                                   ; 50                          ; 0xc39a5
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc39a6
    push ax                                   ; 50                          ; 0xc39a9
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc39aa
    xor ah, ah                                ; 30 e4                       ; 0xc39ad
    push ax                                   ; 50                          ; 0xc39af
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc39b0
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39b3
    xor ah, ah                                ; 30 e4                       ; 0xc39b6
    push ax                                   ; 50                          ; 0xc39b8
    mov cl, byte [bp+010h]                    ; 8a 4e 10                    ; 0xc39b9
    xor ch, ch                                ; 30 ed                       ; 0xc39bc
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc39be
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39c1
    xor ah, ah                                ; 30 e4                       ; 0xc39c4
    mov bx, ax                                ; 89 c3                       ; 0xc39c6
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc39c8
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39cb
    xor ah, ah                                ; 30 e4                       ; 0xc39ce
    mov dx, ax                                ; 89 c2                       ; 0xc39d0
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc39d2
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc39d5
    mov byte [bp-005h], ch                    ; 88 6e fb                    ; 0xc39d8
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc39db
    call 01c48h                               ; e8 67 e2                    ; 0xc39de
    jmp near 03d82h                           ; e9 9e 03                    ; 0xc39e1 vgabios.c:2575
    xor ax, ax                                ; 31 c0                       ; 0xc39e4 vgabios.c:2577
    push ax                                   ; 50                          ; 0xc39e6
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc39e7
    push ax                                   ; 50                          ; 0xc39ea
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc39eb
    xor ah, ah                                ; 30 e4                       ; 0xc39ee
    push ax                                   ; 50                          ; 0xc39f0
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc39f1
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39f4
    xor ah, ah                                ; 30 e4                       ; 0xc39f7
    push ax                                   ; 50                          ; 0xc39f9
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc39fa
    mov cx, ax                                ; 89 c1                       ; 0xc39fd
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc39ff
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a02
    xor ah, ah                                ; 30 e4                       ; 0xc3a05
    mov bx, ax                                ; 89 c3                       ; 0xc3a07
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a09
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a0c
    xor ah, ah                                ; 30 e4                       ; 0xc3a0f
    mov dx, ax                                ; 89 c2                       ; 0xc3a11
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a13
    jmp short 039deh                          ; eb c6                       ; 0xc3a16
    lea dx, [bp+012h]                         ; 8d 56 12                    ; 0xc3a18 vgabios.c:2580
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a1b
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a1e
    xor ah, ah                                ; 30 e4                       ; 0xc3a21
    call 00dc4h                               ; e8 9e d3                    ; 0xc3a23
    jmp near 03d82h                           ; e9 59 03                    ; 0xc3a26 vgabios.c:2581
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a29 vgabios.c:2583
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3a2c
    xor ah, ah                                ; 30 e4                       ; 0xc3a2f
    mov bx, ax                                ; 89 c3                       ; 0xc3a31
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a33
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a36
    xor ah, ah                                ; 30 e4                       ; 0xc3a39
    mov dx, ax                                ; 89 c2                       ; 0xc3a3b
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a3d
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc3a40
    mov byte [bp-005h], bh                    ; 88 7e fb                    ; 0xc3a43
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc3a46
    call 0258eh                               ; e8 42 eb                    ; 0xc3a49
    jmp near 03d82h                           ; e9 33 03                    ; 0xc3a4c vgabios.c:2584
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a4f vgabios.c:2586
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3a52
    xor ah, ah                                ; 30 e4                       ; 0xc3a55
    mov bx, ax                                ; 89 c3                       ; 0xc3a57
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3a59
    shr dx, 008h                              ; c1 ea 08                    ; 0xc3a5c
    xor dh, dh                                ; 30 f6                       ; 0xc3a5f
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a61
    call 02715h                               ; e8 ae ec                    ; 0xc3a64
    jmp near 03d82h                           ; e9 18 03                    ; 0xc3a67 vgabios.c:2587
    mov cx, word [bp+00eh]                    ; 8b 4e 0e                    ; 0xc3a6a vgabios.c:2589
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc3a6d
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a70
    xor ah, ah                                ; 30 e4                       ; 0xc3a73
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3a75
    shr dx, 008h                              ; c1 ea 08                    ; 0xc3a78
    xor dh, dh                                ; 30 f6                       ; 0xc3a7b
    mov si, dx                                ; 89 d6                       ; 0xc3a7d
    mov dx, ax                                ; 89 c2                       ; 0xc3a7f
    mov ax, si                                ; 89 f0                       ; 0xc3a81
    call 02896h                               ; e8 10 ee                    ; 0xc3a83
    jmp near 03d82h                           ; e9 f9 02                    ; 0xc3a86 vgabios.c:2590
    lea cx, [bp+012h]                         ; 8d 4e 12                    ; 0xc3a89 vgabios.c:2592
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3a8c
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3a8f
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a92
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a95
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc3a98
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3a9b
    xor ah, ah                                ; 30 e4                       ; 0xc3a9e
    call 00f87h                               ; e8 e4 d4                    ; 0xc3aa0
    jmp near 03d82h                           ; e9 dc 02                    ; 0xc3aa3 vgabios.c:2593
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc3aa6 vgabios.c:2601
    mov bl, byte [bp+00ch]                    ; 8a 5e 0c                    ; 0xc3aa9
    xor bh, bh                                ; 30 ff                       ; 0xc3aac
    mov dx, 000ffh                            ; ba ff 00                    ; 0xc3aae
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3ab1
    xor ah, ah                                ; 30 e4                       ; 0xc3ab4
    call 02a09h                               ; e8 50 ef                    ; 0xc3ab6
    jmp near 03d82h                           ; e9 c6 02                    ; 0xc3ab9 vgabios.c:2602
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3abc vgabios.c:2605
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3abf
    call 010edh                               ; e8 28 d6                    ; 0xc3ac2
    jmp near 03d82h                           ; e9 ba 02                    ; 0xc3ac5 vgabios.c:2606
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3ac8 vgabios.c:2608
    xor ah, ah                                ; 30 e4                       ; 0xc3acb
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3acd
    jnbe short 03b3dh                         ; 77 6b                       ; 0xc3ad0
    push CS                                   ; 0e                          ; 0xc3ad2
    pop ES                                    ; 07                          ; 0xc3ad3
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc3ad4
    mov di, 038a4h                            ; bf a4 38                    ; 0xc3ad7
    repne scasb                               ; f2 ae                       ; 0xc3ada
    sal cx, 1                                 ; d1 e1                       ; 0xc3adc
    mov di, cx                                ; 89 cf                       ; 0xc3ade
    mov ax, word [cs:di+038b3h]               ; 2e 8b 85 b3 38              ; 0xc3ae0
    jmp ax                                    ; ff e0                       ; 0xc3ae5
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3ae7 vgabios.c:2612
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3aea
    xor ah, ah                                ; 30 e4                       ; 0xc3aed
    push ax                                   ; 50                          ; 0xc3aef
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3af0
    push ax                                   ; 50                          ; 0xc3af3
    push word [bp+00eh]                       ; ff 76 0e                    ; 0xc3af4
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3af7
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3afa
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc3afd
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3b00
    jmp short 03b1bh                          ; eb 16                       ; 0xc3b03
    push strict byte 0000eh                   ; 6a 0e                       ; 0xc3b05 vgabios.c:2616
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b07
    xor ah, ah                                ; 30 e4                       ; 0xc3b0a
    push ax                                   ; 50                          ; 0xc3b0c
    push strict byte 00000h                   ; 6a 00                       ; 0xc3b0d
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b0f
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b12
    mov bx, 05d6ah                            ; bb 6a 5d                    ; 0xc3b15
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc3b18
    call 02e2bh                               ; e8 0d f3                    ; 0xc3b1b
    jmp short 03b3dh                          ; eb 1d                       ; 0xc3b1e
    push strict byte 00008h                   ; 6a 08                       ; 0xc3b20 vgabios.c:2620
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b22
    xor ah, ah                                ; 30 e4                       ; 0xc3b25
    push ax                                   ; 50                          ; 0xc3b27
    push strict byte 00000h                   ; 6a 00                       ; 0xc3b28
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b2a
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b2d
    mov bx, 0556ah                            ; bb 6a 55                    ; 0xc3b30
    jmp short 03b18h                          ; eb e3                       ; 0xc3b33
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b35 vgabios.c:2623
    xor ah, ah                                ; 30 e4                       ; 0xc3b38
    call 02d93h                               ; e8 56 f2                    ; 0xc3b3a
    jmp near 03d82h                           ; e9 42 02                    ; 0xc3b3d vgabios.c:2624
    push strict byte 00010h                   ; 6a 10                       ; 0xc3b40 vgabios.c:2627
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b42
    xor ah, ah                                ; 30 e4                       ; 0xc3b45
    push ax                                   ; 50                          ; 0xc3b47
    push strict byte 00000h                   ; 6a 00                       ; 0xc3b48
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b4a
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b4d
    mov bx, 06b6ah                            ; bb 6a 6b                    ; 0xc3b50
    jmp short 03b18h                          ; eb c3                       ; 0xc3b53
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc3b55 vgabios.c:2630
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc3b58
    call 02eaah                               ; e8 4c f3                    ; 0xc3b5b
    jmp short 03b3dh                          ; eb dd                       ; 0xc3b5e vgabios.c:2631
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b60 vgabios.c:2633
    xor ah, ah                                ; 30 e4                       ; 0xc3b63
    push ax                                   ; 50                          ; 0xc3b65
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b66
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc3b69
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc3b6c
    mov si, word [bp+016h]                    ; 8b 76 16                    ; 0xc3b6f
    mov cx, ax                                ; 89 c1                       ; 0xc3b72
    mov ax, si                                ; 89 f0                       ; 0xc3b74
    call 02f0dh                               ; e8 94 f3                    ; 0xc3b76
    jmp short 03b3dh                          ; eb c2                       ; 0xc3b79 vgabios.c:2634
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b7b vgabios.c:2636
    xor ah, ah                                ; 30 e4                       ; 0xc3b7e
    mov dx, ax                                ; 89 c2                       ; 0xc3b80
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b82
    call 02f2ah                               ; e8 a2 f3                    ; 0xc3b85
    jmp short 03b3dh                          ; eb b3                       ; 0xc3b88 vgabios.c:2637
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b8a vgabios.c:2639
    xor ah, ah                                ; 30 e4                       ; 0xc3b8d
    mov dx, ax                                ; 89 c2                       ; 0xc3b8f
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b91
    call 02f4ch                               ; e8 b5 f3                    ; 0xc3b94
    jmp short 03b3dh                          ; eb a4                       ; 0xc3b97 vgabios.c:2640
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b99 vgabios.c:2642
    xor ah, ah                                ; 30 e4                       ; 0xc3b9c
    mov dx, ax                                ; 89 c2                       ; 0xc3b9e
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3ba0
    call 02f6eh                               ; e8 c8 f3                    ; 0xc3ba3
    jmp short 03b3dh                          ; eb 95                       ; 0xc3ba6 vgabios.c:2643
    lea ax, [bp+00eh]                         ; 8d 46 0e                    ; 0xc3ba8 vgabios.c:2645
    push ax                                   ; 50                          ; 0xc3bab
    lea cx, [bp+010h]                         ; 8d 4e 10                    ; 0xc3bac
    lea bx, [bp+008h]                         ; 8d 5e 08                    ; 0xc3baf
    lea dx, [bp+016h]                         ; 8d 56 16                    ; 0xc3bb2
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3bb5
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3bb8
    call 00f04h                               ; e8 46 d3                    ; 0xc3bbb
    jmp near 03d82h                           ; e9 c1 01                    ; 0xc3bbe vgabios.c:2653
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3bc1 vgabios.c:2655
    xor ah, ah                                ; 30 e4                       ; 0xc3bc4
    cmp ax, strict word 00034h                ; 3d 34 00                    ; 0xc3bc6
    jc short 03bdah                           ; 72 0f                       ; 0xc3bc9
    jbe short 03c05h                          ; 76 38                       ; 0xc3bcb
    cmp ax, strict word 00036h                ; 3d 36 00                    ; 0xc3bcd
    je short 03c2dh                           ; 74 5b                       ; 0xc3bd0
    cmp ax, strict word 00035h                ; 3d 35 00                    ; 0xc3bd2
    je short 03c2fh                           ; 74 58                       ; 0xc3bd5
    jmp near 03d82h                           ; e9 a8 01                    ; 0xc3bd7
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3bda
    je short 03be9h                           ; 74 0a                       ; 0xc3bdd
    cmp ax, strict word 00020h                ; 3d 20 00                    ; 0xc3bdf
    jne short 03c2ah                          ; 75 46                       ; 0xc3be2
    call 02f90h                               ; e8 a9 f3                    ; 0xc3be4 vgabios.c:2658
    jmp short 03c2ah                          ; eb 41                       ; 0xc3be7 vgabios.c:2659
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3be9 vgabios.c:2661
    xor ah, ah                                ; 30 e4                       ; 0xc3bec
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3bee
    jnbe short 03c2ah                         ; 77 37                       ; 0xc3bf1
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3bf3 vgabios.c:2662
    call 02f95h                               ; e8 9c f3                    ; 0xc3bf6
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3bf9 vgabios.c:2663
    xor al, al                                ; 30 c0                       ; 0xc3bfc
    or AL, strict byte 012h                   ; 0c 12                       ; 0xc3bfe
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc3c00
    jmp short 03c2ah                          ; eb 25                       ; 0xc3c03 vgabios.c:2665
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c05 vgabios.c:2667
    xor ah, ah                                ; 30 e4                       ; 0xc3c08
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3c0a
    jnc short 03c27h                          ; 73 18                       ; 0xc3c0d
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3c0f vgabios.c:45
    mov es, ax                                ; 8e c0                       ; 0xc3c12
    mov si, 00087h                            ; be 87 00                    ; 0xc3c14
    mov ah, byte [es:si]                      ; 26 8a 24                    ; 0xc3c17 vgabios.c:47
    and ah, 0feh                              ; 80 e4 fe                    ; 0xc3c1a vgabios.c:48
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c1d
    or al, ah                                 ; 08 e0                       ; 0xc3c20
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3c22 vgabios.c:52
    jmp short 03bf9h                          ; eb d2                       ; 0xc3c25
    mov byte [bp+012h], ah                    ; 88 66 12                    ; 0xc3c27 vgabios.c:2673
    jmp near 03d82h                           ; e9 55 01                    ; 0xc3c2a vgabios.c:2674
    jmp short 03c3dh                          ; eb 0e                       ; 0xc3c2d
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c2f vgabios.c:2676
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3c32
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3c35
    call 02fc7h                               ; e8 8c f3                    ; 0xc3c38
    jmp short 03bf9h                          ; eb bc                       ; 0xc3c3b
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c3d vgabios.c:2680
    call 02fcch                               ; e8 89 f3                    ; 0xc3c40
    jmp short 03bf9h                          ; eb b4                       ; 0xc3c43
    push word [bp+008h]                       ; ff 76 08                    ; 0xc3c45 vgabios.c:2690
    push word [bp+016h]                       ; ff 76 16                    ; 0xc3c48
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3c4b
    xor ah, ah                                ; 30 e4                       ; 0xc3c4e
    push ax                                   ; 50                          ; 0xc3c50
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc3c51
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3c54
    xor ah, ah                                ; 30 e4                       ; 0xc3c57
    push ax                                   ; 50                          ; 0xc3c59
    mov bl, byte [bp+00ch]                    ; 8a 5e 0c                    ; 0xc3c5a
    xor bh, bh                                ; 30 ff                       ; 0xc3c5d
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3c5f
    shr dx, 008h                              ; c1 ea 08                    ; 0xc3c62
    xor dh, dh                                ; 30 f6                       ; 0xc3c65
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c67
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3c6a
    call 02fd1h                               ; e8 61 f3                    ; 0xc3c6d
    jmp short 03c2ah                          ; eb b8                       ; 0xc3c70 vgabios.c:2691
    mov bx, si                                ; 89 f3                       ; 0xc3c72 vgabios.c:2693
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3c74
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3c77
    call 0306eh                               ; e8 f1 f3                    ; 0xc3c7a
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c7d vgabios.c:2694
    xor al, al                                ; 30 c0                       ; 0xc3c80
    or AL, strict byte 01bh                   ; 0c 1b                       ; 0xc3c82
    jmp near 03c00h                           ; e9 79 ff                    ; 0xc3c84
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c87 vgabios.c:2697
    xor ah, ah                                ; 30 e4                       ; 0xc3c8a
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3c8c
    je short 03cb3h                           ; 74 22                       ; 0xc3c8f
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc3c91
    je short 03ca5h                           ; 74 0f                       ; 0xc3c94
    test ax, ax                               ; 85 c0                       ; 0xc3c96
    jne short 03cbfh                          ; 75 25                       ; 0xc3c98
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3c9a vgabios.c:2700
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3c9d
    call 03280h                               ; e8 dd f5                    ; 0xc3ca0
    jmp short 03cbfh                          ; eb 1a                       ; 0xc3ca3 vgabios.c:2701
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3ca5 vgabios.c:2703
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3ca8
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3cab
    call 03298h                               ; e8 e7 f5                    ; 0xc3cae
    jmp short 03cbfh                          ; eb 0c                       ; 0xc3cb1 vgabios.c:2704
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3cb3 vgabios.c:2706
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3cb6
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3cb9
    call 03570h                               ; e8 b1 f8                    ; 0xc3cbc
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3cbf vgabios.c:2713
    xor al, al                                ; 30 c0                       ; 0xc3cc2
    or AL, strict byte 01ch                   ; 0c 1c                       ; 0xc3cc4
    jmp near 03c00h                           ; e9 37 ff                    ; 0xc3cc6
    call 007bfh                               ; e8 f3 ca                    ; 0xc3cc9 vgabios.c:2718
    test ax, ax                               ; 85 c0                       ; 0xc3ccc
    je short 03d44h                           ; 74 74                       ; 0xc3cce
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3cd0 vgabios.c:2719
    xor ah, ah                                ; 30 e4                       ; 0xc3cd3
    cmp ax, strict word 0000ah                ; 3d 0a 00                    ; 0xc3cd5
    jnbe short 03d46h                         ; 77 6c                       ; 0xc3cd8
    push CS                                   ; 0e                          ; 0xc3cda
    pop ES                                    ; 07                          ; 0xc3cdb
    mov cx, strict word 00008h                ; b9 08 00                    ; 0xc3cdc
    mov di, 038d3h                            ; bf d3 38                    ; 0xc3cdf
    repne scasb                               ; f2 ae                       ; 0xc3ce2
    sal cx, 1                                 ; d1 e1                       ; 0xc3ce4
    mov di, cx                                ; 89 cf                       ; 0xc3ce6
    mov ax, word [cs:di+038dah]               ; 2e 8b 85 da 38              ; 0xc3ce8
    jmp ax                                    ; ff e0                       ; 0xc3ced
    mov bx, si                                ; 89 f3                       ; 0xc3cef vgabios.c:2722
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3cf1
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3cf4
    call 03f53h                               ; e8 59 02                    ; 0xc3cf7
    jmp near 03d82h                           ; e9 85 00                    ; 0xc3cfa vgabios.c:2723
    mov cx, si                                ; 89 f1                       ; 0xc3cfd vgabios.c:2725
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3cff
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3d02
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d05
    call 0407eh                               ; e8 73 03                    ; 0xc3d08
    jmp near 03d82h                           ; e9 74 00                    ; 0xc3d0b vgabios.c:2726
    mov cx, si                                ; 89 f1                       ; 0xc3d0e vgabios.c:2728
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3d10
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3d13
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d16
    call 0411dh                               ; e8 01 04                    ; 0xc3d19
    jmp short 03d82h                          ; eb 64                       ; 0xc3d1c vgabios.c:2729
    lea ax, [bp+00ch]                         ; 8d 46 0c                    ; 0xc3d1e vgabios.c:2731
    push ax                                   ; 50                          ; 0xc3d21
    mov cx, word [bp+016h]                    ; 8b 4e 16                    ; 0xc3d22
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3d25
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3d28
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d2b
    call 042e6h                               ; e8 b5 05                    ; 0xc3d2e
    jmp short 03d82h                          ; eb 4f                       ; 0xc3d31 vgabios.c:2732
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3d33 vgabios.c:2734
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3d36
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3d39
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d3c
    call 04372h                               ; e8 30 06                    ; 0xc3d3f
    jmp short 03d82h                          ; eb 3e                       ; 0xc3d42 vgabios.c:2735
    jmp short 03d4dh                          ; eb 07                       ; 0xc3d44
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d46 vgabios.c:2757
    jmp short 03d82h                          ; eb 35                       ; 0xc3d4b vgabios.c:2760
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d4d vgabios.c:2762
    jmp short 03d82h                          ; eb 2e                       ; 0xc3d52 vgabios.c:2764
    call 007bfh                               ; e8 68 ca                    ; 0xc3d54 vgabios.c:2766
    test ax, ax                               ; 85 c0                       ; 0xc3d57
    je short 03d7dh                           ; 74 22                       ; 0xc3d59
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3d5b vgabios.c:2767
    xor ah, ah                                ; 30 e4                       ; 0xc3d5e
    cmp ax, strict word 00042h                ; 3d 42 00                    ; 0xc3d60
    jne short 03d76h                          ; 75 11                       ; 0xc3d63
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3d65 vgabios.c:2770
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3d68
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3d6b
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d6e
    call 04451h                               ; e8 dd 06                    ; 0xc3d71
    jmp short 03d82h                          ; eb 0c                       ; 0xc3d74 vgabios.c:2771
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d76 vgabios.c:2773
    jmp short 03d82h                          ; eb 05                       ; 0xc3d7b vgabios.c:2776
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d7d vgabios.c:2778
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3d82 vgabios.c:2788
    pop di                                    ; 5f                          ; 0xc3d85
    pop si                                    ; 5e                          ; 0xc3d86
    pop bp                                    ; 5d                          ; 0xc3d87
    retn                                      ; c3                          ; 0xc3d88
  ; disGetNextSymbol 0xc3d89 LB 0x7ba -> off=0x0 cb=000000000000001f uValue=00000000000c3d89 'dispi_set_xres'
dispi_set_xres:                              ; 0xc3d89 LB 0x1f
    push bp                                   ; 55                          ; 0xc3d89 vbe.c:100
    mov bp, sp                                ; 89 e5                       ; 0xc3d8a
    push bx                                   ; 53                          ; 0xc3d8c
    push dx                                   ; 52                          ; 0xc3d8d
    mov bx, ax                                ; 89 c3                       ; 0xc3d8e
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3d90 vbe.c:105
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3d93
    call 00570h                               ; e8 d7 c7                    ; 0xc3d96
    mov ax, bx                                ; 89 d8                       ; 0xc3d99 vbe.c:106
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3d9b
    call 00570h                               ; e8 cf c7                    ; 0xc3d9e
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3da1 vbe.c:107
    pop dx                                    ; 5a                          ; 0xc3da4
    pop bx                                    ; 5b                          ; 0xc3da5
    pop bp                                    ; 5d                          ; 0xc3da6
    retn                                      ; c3                          ; 0xc3da7
  ; disGetNextSymbol 0xc3da8 LB 0x79b -> off=0x0 cb=000000000000001f uValue=00000000000c3da8 'dispi_set_yres'
dispi_set_yres:                              ; 0xc3da8 LB 0x1f
    push bp                                   ; 55                          ; 0xc3da8 vbe.c:109
    mov bp, sp                                ; 89 e5                       ; 0xc3da9
    push bx                                   ; 53                          ; 0xc3dab
    push dx                                   ; 52                          ; 0xc3dac
    mov bx, ax                                ; 89 c3                       ; 0xc3dad
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3daf vbe.c:114
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3db2
    call 00570h                               ; e8 b8 c7                    ; 0xc3db5
    mov ax, bx                                ; 89 d8                       ; 0xc3db8 vbe.c:115
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3dba
    call 00570h                               ; e8 b0 c7                    ; 0xc3dbd
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3dc0 vbe.c:116
    pop dx                                    ; 5a                          ; 0xc3dc3
    pop bx                                    ; 5b                          ; 0xc3dc4
    pop bp                                    ; 5d                          ; 0xc3dc5
    retn                                      ; c3                          ; 0xc3dc6
  ; disGetNextSymbol 0xc3dc7 LB 0x77c -> off=0x0 cb=0000000000000019 uValue=00000000000c3dc7 'dispi_get_yres'
dispi_get_yres:                              ; 0xc3dc7 LB 0x19
    push bp                                   ; 55                          ; 0xc3dc7 vbe.c:118
    mov bp, sp                                ; 89 e5                       ; 0xc3dc8
    push dx                                   ; 52                          ; 0xc3dca
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3dcb vbe.c:120
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3dce
    call 00570h                               ; e8 9c c7                    ; 0xc3dd1
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3dd4 vbe.c:121
    call 00577h                               ; e8 9d c7                    ; 0xc3dd7
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3dda vbe.c:122
    pop dx                                    ; 5a                          ; 0xc3ddd
    pop bp                                    ; 5d                          ; 0xc3dde
    retn                                      ; c3                          ; 0xc3ddf
  ; disGetNextSymbol 0xc3de0 LB 0x763 -> off=0x0 cb=000000000000001f uValue=00000000000c3de0 'dispi_set_bpp'
dispi_set_bpp:                               ; 0xc3de0 LB 0x1f
    push bp                                   ; 55                          ; 0xc3de0 vbe.c:124
    mov bp, sp                                ; 89 e5                       ; 0xc3de1
    push bx                                   ; 53                          ; 0xc3de3
    push dx                                   ; 52                          ; 0xc3de4
    mov bx, ax                                ; 89 c3                       ; 0xc3de5
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3de7 vbe.c:129
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3dea
    call 00570h                               ; e8 80 c7                    ; 0xc3ded
    mov ax, bx                                ; 89 d8                       ; 0xc3df0 vbe.c:130
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3df2
    call 00570h                               ; e8 78 c7                    ; 0xc3df5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3df8 vbe.c:131
    pop dx                                    ; 5a                          ; 0xc3dfb
    pop bx                                    ; 5b                          ; 0xc3dfc
    pop bp                                    ; 5d                          ; 0xc3dfd
    retn                                      ; c3                          ; 0xc3dfe
  ; disGetNextSymbol 0xc3dff LB 0x744 -> off=0x0 cb=0000000000000019 uValue=00000000000c3dff 'dispi_get_bpp'
dispi_get_bpp:                               ; 0xc3dff LB 0x19
    push bp                                   ; 55                          ; 0xc3dff vbe.c:133
    mov bp, sp                                ; 89 e5                       ; 0xc3e00
    push dx                                   ; 52                          ; 0xc3e02
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3e03 vbe.c:135
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e06
    call 00570h                               ; e8 64 c7                    ; 0xc3e09
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e0c vbe.c:136
    call 00577h                               ; e8 65 c7                    ; 0xc3e0f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e12 vbe.c:137
    pop dx                                    ; 5a                          ; 0xc3e15
    pop bp                                    ; 5d                          ; 0xc3e16
    retn                                      ; c3                          ; 0xc3e17
  ; disGetNextSymbol 0xc3e18 LB 0x72b -> off=0x0 cb=000000000000001f uValue=00000000000c3e18 'dispi_set_virt_width'
dispi_set_virt_width:                        ; 0xc3e18 LB 0x1f
    push bp                                   ; 55                          ; 0xc3e18 vbe.c:139
    mov bp, sp                                ; 89 e5                       ; 0xc3e19
    push bx                                   ; 53                          ; 0xc3e1b
    push dx                                   ; 52                          ; 0xc3e1c
    mov bx, ax                                ; 89 c3                       ; 0xc3e1d
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3e1f vbe.c:144
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e22
    call 00570h                               ; e8 48 c7                    ; 0xc3e25
    mov ax, bx                                ; 89 d8                       ; 0xc3e28 vbe.c:145
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e2a
    call 00570h                               ; e8 40 c7                    ; 0xc3e2d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3e30 vbe.c:146
    pop dx                                    ; 5a                          ; 0xc3e33
    pop bx                                    ; 5b                          ; 0xc3e34
    pop bp                                    ; 5d                          ; 0xc3e35
    retn                                      ; c3                          ; 0xc3e36
  ; disGetNextSymbol 0xc3e37 LB 0x70c -> off=0x0 cb=0000000000000019 uValue=00000000000c3e37 'dispi_get_virt_width'
dispi_get_virt_width:                        ; 0xc3e37 LB 0x19
    push bp                                   ; 55                          ; 0xc3e37 vbe.c:148
    mov bp, sp                                ; 89 e5                       ; 0xc3e38
    push dx                                   ; 52                          ; 0xc3e3a
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3e3b vbe.c:150
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e3e
    call 00570h                               ; e8 2c c7                    ; 0xc3e41
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e44 vbe.c:151
    call 00577h                               ; e8 2d c7                    ; 0xc3e47
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e4a vbe.c:152
    pop dx                                    ; 5a                          ; 0xc3e4d
    pop bp                                    ; 5d                          ; 0xc3e4e
    retn                                      ; c3                          ; 0xc3e4f
  ; disGetNextSymbol 0xc3e50 LB 0x6f3 -> off=0x0 cb=0000000000000019 uValue=00000000000c3e50 'dispi_get_virt_height'
dispi_get_virt_height:                       ; 0xc3e50 LB 0x19
    push bp                                   ; 55                          ; 0xc3e50 vbe.c:154
    mov bp, sp                                ; 89 e5                       ; 0xc3e51
    push dx                                   ; 52                          ; 0xc3e53
    mov ax, strict word 00007h                ; b8 07 00                    ; 0xc3e54 vbe.c:156
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e57
    call 00570h                               ; e8 13 c7                    ; 0xc3e5a
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e5d vbe.c:157
    call 00577h                               ; e8 14 c7                    ; 0xc3e60
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e63 vbe.c:158
    pop dx                                    ; 5a                          ; 0xc3e66
    pop bp                                    ; 5d                          ; 0xc3e67
    retn                                      ; c3                          ; 0xc3e68
  ; disGetNextSymbol 0xc3e69 LB 0x6da -> off=0x0 cb=0000000000000012 uValue=00000000000c3e69 'in_word'
in_word:                                     ; 0xc3e69 LB 0x12
    push bp                                   ; 55                          ; 0xc3e69 vbe.c:160
    mov bp, sp                                ; 89 e5                       ; 0xc3e6a
    push bx                                   ; 53                          ; 0xc3e6c
    mov bx, ax                                ; 89 c3                       ; 0xc3e6d
    mov ax, dx                                ; 89 d0                       ; 0xc3e6f
    mov dx, bx                                ; 89 da                       ; 0xc3e71 vbe.c:162
    out DX, ax                                ; ef                          ; 0xc3e73
    in ax, DX                                 ; ed                          ; 0xc3e74 vbe.c:163
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e75 vbe.c:164
    pop bx                                    ; 5b                          ; 0xc3e78
    pop bp                                    ; 5d                          ; 0xc3e79
    retn                                      ; c3                          ; 0xc3e7a
  ; disGetNextSymbol 0xc3e7b LB 0x6c8 -> off=0x0 cb=0000000000000014 uValue=00000000000c3e7b 'in_byte'
in_byte:                                     ; 0xc3e7b LB 0x14
    push bp                                   ; 55                          ; 0xc3e7b vbe.c:166
    mov bp, sp                                ; 89 e5                       ; 0xc3e7c
    push bx                                   ; 53                          ; 0xc3e7e
    mov bx, ax                                ; 89 c3                       ; 0xc3e7f
    mov ax, dx                                ; 89 d0                       ; 0xc3e81
    mov dx, bx                                ; 89 da                       ; 0xc3e83 vbe.c:168
    out DX, ax                                ; ef                          ; 0xc3e85
    in AL, DX                                 ; ec                          ; 0xc3e86 vbe.c:169
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3e87
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e89 vbe.c:170
    pop bx                                    ; 5b                          ; 0xc3e8c
    pop bp                                    ; 5d                          ; 0xc3e8d
    retn                                      ; c3                          ; 0xc3e8e
  ; disGetNextSymbol 0xc3e8f LB 0x6b4 -> off=0x0 cb=0000000000000014 uValue=00000000000c3e8f 'dispi_get_id'
dispi_get_id:                                ; 0xc3e8f LB 0x14
    push bp                                   ; 55                          ; 0xc3e8f vbe.c:173
    mov bp, sp                                ; 89 e5                       ; 0xc3e90
    push dx                                   ; 52                          ; 0xc3e92
    xor ax, ax                                ; 31 c0                       ; 0xc3e93 vbe.c:175
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e95
    out DX, ax                                ; ef                          ; 0xc3e98
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e99 vbe.c:176
    in ax, DX                                 ; ed                          ; 0xc3e9c
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e9d vbe.c:177
    pop dx                                    ; 5a                          ; 0xc3ea0
    pop bp                                    ; 5d                          ; 0xc3ea1
    retn                                      ; c3                          ; 0xc3ea2
  ; disGetNextSymbol 0xc3ea3 LB 0x6a0 -> off=0x0 cb=000000000000001a uValue=00000000000c3ea3 'dispi_set_id'
dispi_set_id:                                ; 0xc3ea3 LB 0x1a
    push bp                                   ; 55                          ; 0xc3ea3 vbe.c:179
    mov bp, sp                                ; 89 e5                       ; 0xc3ea4
    push bx                                   ; 53                          ; 0xc3ea6
    push dx                                   ; 52                          ; 0xc3ea7
    mov bx, ax                                ; 89 c3                       ; 0xc3ea8
    xor ax, ax                                ; 31 c0                       ; 0xc3eaa vbe.c:181
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3eac
    out DX, ax                                ; ef                          ; 0xc3eaf
    mov ax, bx                                ; 89 d8                       ; 0xc3eb0 vbe.c:182
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3eb2
    out DX, ax                                ; ef                          ; 0xc3eb5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3eb6 vbe.c:183
    pop dx                                    ; 5a                          ; 0xc3eb9
    pop bx                                    ; 5b                          ; 0xc3eba
    pop bp                                    ; 5d                          ; 0xc3ebb
    retn                                      ; c3                          ; 0xc3ebc
  ; disGetNextSymbol 0xc3ebd LB 0x686 -> off=0x0 cb=000000000000002a uValue=00000000000c3ebd 'vbe_init'
vbe_init:                                    ; 0xc3ebd LB 0x2a
    push bp                                   ; 55                          ; 0xc3ebd vbe.c:188
    mov bp, sp                                ; 89 e5                       ; 0xc3ebe
    push bx                                   ; 53                          ; 0xc3ec0
    mov ax, 0b0c0h                            ; b8 c0 b0                    ; 0xc3ec1 vbe.c:190
    call 03ea3h                               ; e8 dc ff                    ; 0xc3ec4
    call 03e8fh                               ; e8 c5 ff                    ; 0xc3ec7 vbe.c:191
    cmp ax, 0b0c0h                            ; 3d c0 b0                    ; 0xc3eca
    jne short 03ee1h                          ; 75 12                       ; 0xc3ecd
    mov bx, 000b9h                            ; bb b9 00                    ; 0xc3ecf vbe.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3ed2
    mov es, ax                                ; 8e c0                       ; 0xc3ed5
    mov byte [es:bx], 001h                    ; 26 c6 07 01                 ; 0xc3ed7
    mov ax, 0b0c4h                            ; b8 c4 b0                    ; 0xc3edb vbe.c:194
    call 03ea3h                               ; e8 c2 ff                    ; 0xc3ede
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3ee1 vbe.c:199
    pop bx                                    ; 5b                          ; 0xc3ee4
    pop bp                                    ; 5d                          ; 0xc3ee5
    retn                                      ; c3                          ; 0xc3ee6
  ; disGetNextSymbol 0xc3ee7 LB 0x65c -> off=0x0 cb=000000000000006c uValue=00000000000c3ee7 'mode_info_find_mode'
mode_info_find_mode:                         ; 0xc3ee7 LB 0x6c
    push bp                                   ; 55                          ; 0xc3ee7 vbe.c:202
    mov bp, sp                                ; 89 e5                       ; 0xc3ee8
    push bx                                   ; 53                          ; 0xc3eea
    push cx                                   ; 51                          ; 0xc3eeb
    push si                                   ; 56                          ; 0xc3eec
    push di                                   ; 57                          ; 0xc3eed
    mov di, ax                                ; 89 c7                       ; 0xc3eee
    mov si, dx                                ; 89 d6                       ; 0xc3ef0
    xor dx, dx                                ; 31 d2                       ; 0xc3ef2 vbe.c:208
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3ef4
    call 03e69h                               ; e8 6f ff                    ; 0xc3ef7
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3efa vbe.c:209
    jne short 03f48h                          ; 75 49                       ; 0xc3efd
    test si, si                               ; 85 f6                       ; 0xc3eff vbe.c:213
    je short 03f16h                           ; 74 13                       ; 0xc3f01
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc3f03 vbe.c:220
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3f06
    call 00570h                               ; e8 64 c6                    ; 0xc3f09
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3f0c vbe.c:221
    call 00577h                               ; e8 65 c6                    ; 0xc3f0f
    test ax, ax                               ; 85 c0                       ; 0xc3f12 vbe.c:222
    je short 03f4ah                           ; 74 34                       ; 0xc3f14
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc3f16 vbe.c:226
    mov dx, bx                                ; 89 da                       ; 0xc3f19 vbe.c:232
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f1b
    call 03e69h                               ; e8 48 ff                    ; 0xc3f1e
    mov cx, ax                                ; 89 c1                       ; 0xc3f21
    cmp cx, strict byte 0ffffh                ; 83 f9 ff                    ; 0xc3f23 vbe.c:233
    je short 03f48h                           ; 74 20                       ; 0xc3f26
    lea dx, [bx+002h]                         ; 8d 57 02                    ; 0xc3f28 vbe.c:235
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f2b
    call 03e69h                               ; e8 38 ff                    ; 0xc3f2e
    lea dx, [bx+044h]                         ; 8d 57 44                    ; 0xc3f31
    cmp cx, di                                ; 39 f9                       ; 0xc3f34 vbe.c:237
    jne short 03f44h                          ; 75 0c                       ; 0xc3f36
    test si, si                               ; 85 f6                       ; 0xc3f38 vbe.c:239
    jne short 03f40h                          ; 75 04                       ; 0xc3f3a
    mov ax, bx                                ; 89 d8                       ; 0xc3f3c vbe.c:240
    jmp short 03f4ah                          ; eb 0a                       ; 0xc3f3e
    test AL, strict byte 080h                 ; a8 80                       ; 0xc3f40 vbe.c:241
    jne short 03f3ch                          ; 75 f8                       ; 0xc3f42
    mov bx, dx                                ; 89 d3                       ; 0xc3f44 vbe.c:244
    jmp short 03f1bh                          ; eb d3                       ; 0xc3f46 vbe.c:249
    xor ax, ax                                ; 31 c0                       ; 0xc3f48 vbe.c:252
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc3f4a vbe.c:253
    pop di                                    ; 5f                          ; 0xc3f4d
    pop si                                    ; 5e                          ; 0xc3f4e
    pop cx                                    ; 59                          ; 0xc3f4f
    pop bx                                    ; 5b                          ; 0xc3f50
    pop bp                                    ; 5d                          ; 0xc3f51
    retn                                      ; c3                          ; 0xc3f52
  ; disGetNextSymbol 0xc3f53 LB 0x5f0 -> off=0x0 cb=000000000000012b uValue=00000000000c3f53 'vbe_biosfn_return_controller_information'
vbe_biosfn_return_controller_information: ; 0xc3f53 LB 0x12b
    push bp                                   ; 55                          ; 0xc3f53 vbe.c:284
    mov bp, sp                                ; 89 e5                       ; 0xc3f54
    push cx                                   ; 51                          ; 0xc3f56
    push si                                   ; 56                          ; 0xc3f57
    push di                                   ; 57                          ; 0xc3f58
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc3f59
    mov si, ax                                ; 89 c6                       ; 0xc3f5c
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3f5e
    mov di, bx                                ; 89 df                       ; 0xc3f61
    mov word [bp-00ch], strict word 00022h    ; c7 46 f4 22 00              ; 0xc3f63 vbe.c:289
    call 005b7h                               ; e8 4c c6                    ; 0xc3f68 vbe.c:292
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc3f6b
    mov bx, di                                ; 89 fb                       ; 0xc3f6e vbe.c:295
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3f70
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3f73
    xor dx, dx                                ; 31 d2                       ; 0xc3f76 vbe.c:298
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f78
    call 03e69h                               ; e8 eb fe                    ; 0xc3f7b
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3f7e vbe.c:299
    je short 03f8dh                           ; 74 0a                       ; 0xc3f81
    push SS                                   ; 16                          ; 0xc3f83 vbe.c:301
    pop ES                                    ; 07                          ; 0xc3f84
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc3f85
    jmp near 04076h                           ; e9 e9 00                    ; 0xc3f8a vbe.c:305
    mov cx, strict word 00004h                ; b9 04 00                    ; 0xc3f8d vbe.c:307
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3f90 vbe.c:314
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3f95 vbe.c:322
    cmp word [es:bx+002h], 03245h             ; 26 81 7f 02 45 32           ; 0xc3f98
    jne short 03fa7h                          ; 75 07                       ; 0xc3f9e
    cmp word [es:bx], 04256h                  ; 26 81 3f 56 42              ; 0xc3fa0
    je short 03fb6h                           ; 74 0f                       ; 0xc3fa5
    cmp word [es:bx+002h], 04153h             ; 26 81 7f 02 53 41           ; 0xc3fa7
    jne short 03fbbh                          ; 75 0c                       ; 0xc3fad
    cmp word [es:bx], 04556h                  ; 26 81 3f 56 45              ; 0xc3faf
    jne short 03fbbh                          ; 75 05                       ; 0xc3fb4
    mov word [bp-00eh], strict word 00001h    ; c7 46 f2 01 00              ; 0xc3fb6 vbe.c:324
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3fbb vbe.c:332
    mov word [es:bx], 04556h                  ; 26 c7 07 56 45              ; 0xc3fbe
    mov word [es:bx+002h], 04153h             ; 26 c7 47 02 53 41           ; 0xc3fc3 vbe.c:334
    mov word [es:bx+004h], 00200h             ; 26 c7 47 04 00 02           ; 0xc3fc9 vbe.c:338
    mov word [es:bx+006h], 07e00h             ; 26 c7 47 06 00 7e           ; 0xc3fcf vbe.c:341
    mov [es:bx+008h], ds                      ; 26 8c 5f 08                 ; 0xc3fd5
    mov word [es:bx+00ah], strict word 00001h ; 26 c7 47 0a 01 00           ; 0xc3fd9 vbe.c:344
    mov word [es:bx+00ch], strict word 00000h ; 26 c7 47 0c 00 00           ; 0xc3fdf vbe.c:346
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3fe5 vbe.c:350
    mov word [es:bx+010h], ax                 ; 26 89 47 10                 ; 0xc3fe8
    lea ax, [di+022h]                         ; 8d 45 22                    ; 0xc3fec vbe.c:351
    mov word [es:bx+00eh], ax                 ; 26 89 47 0e                 ; 0xc3fef
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc3ff3 vbe.c:354
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3ff6
    call 03e69h                               ; e8 6d fe                    ; 0xc3ff9
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3ffc
    mov word [es:bx+012h], ax                 ; 26 89 47 12                 ; 0xc3fff
    cmp word [bp-00eh], strict byte 00000h    ; 83 7e f2 00                 ; 0xc4003 vbe.c:356
    je short 0402dh                           ; 74 24                       ; 0xc4007
    mov word [es:bx+014h], strict word 00003h ; 26 c7 47 14 03 00           ; 0xc4009 vbe.c:359
    mov word [es:bx+016h], 07e15h             ; 26 c7 47 16 15 7e           ; 0xc400f vbe.c:360
    mov [es:bx+018h], ds                      ; 26 8c 5f 18                 ; 0xc4015
    mov word [es:bx+01ah], 07e32h             ; 26 c7 47 1a 32 7e           ; 0xc4019 vbe.c:361
    mov [es:bx+01ch], ds                      ; 26 8c 5f 1c                 ; 0xc401f
    mov word [es:bx+01eh], 07e50h             ; 26 c7 47 1e 50 7e           ; 0xc4023 vbe.c:362
    mov [es:bx+020h], ds                      ; 26 8c 5f 20                 ; 0xc4029
    mov dx, cx                                ; 89 ca                       ; 0xc402d vbe.c:369
    add dx, strict byte 0001bh                ; 83 c2 1b                    ; 0xc402f
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4032
    call 03e7bh                               ; e8 43 fe                    ; 0xc4035
    xor ah, ah                                ; 30 e4                       ; 0xc4038 vbe.c:370
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc403a
    jnbe short 04056h                         ; 77 17                       ; 0xc403d
    mov dx, cx                                ; 89 ca                       ; 0xc403f vbe.c:372
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4041
    call 03e69h                               ; e8 22 fe                    ; 0xc4044
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc4047 vbe.c:376
    add bx, di                                ; 01 fb                       ; 0xc404a
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc404c vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc404f
    add word [bp-00ch], strict byte 00002h    ; 83 46 f4 02                 ; 0xc4052 vbe.c:378
    add cx, strict byte 00044h                ; 83 c1 44                    ; 0xc4056 vbe.c:380
    mov dx, cx                                ; 89 ca                       ; 0xc4059 vbe.c:381
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc405b
    call 03e69h                               ; e8 08 fe                    ; 0xc405e
    cmp ax, strict word 0ffffh                ; 3d ff ff                    ; 0xc4061 vbe.c:382
    jne short 0402dh                          ; 75 c7                       ; 0xc4064
    add di, word [bp-00ch]                    ; 03 7e f4                    ; 0xc4066 vbe.c:385
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc4069 vbe.c:62
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc406c
    push SS                                   ; 16                          ; 0xc406f vbe.c:386
    pop ES                                    ; 07                          ; 0xc4070
    mov word [es:si], strict word 0004fh      ; 26 c7 04 4f 00              ; 0xc4071
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc4076 vbe.c:387
    pop di                                    ; 5f                          ; 0xc4079
    pop si                                    ; 5e                          ; 0xc407a
    pop cx                                    ; 59                          ; 0xc407b
    pop bp                                    ; 5d                          ; 0xc407c
    retn                                      ; c3                          ; 0xc407d
  ; disGetNextSymbol 0xc407e LB 0x4c5 -> off=0x0 cb=000000000000009f uValue=00000000000c407e 'vbe_biosfn_return_mode_information'
vbe_biosfn_return_mode_information:          ; 0xc407e LB 0x9f
    push bp                                   ; 55                          ; 0xc407e vbe.c:399
    mov bp, sp                                ; 89 e5                       ; 0xc407f
    push si                                   ; 56                          ; 0xc4081
    push di                                   ; 57                          ; 0xc4082
    push ax                                   ; 50                          ; 0xc4083
    push ax                                   ; 50                          ; 0xc4084
    mov ax, dx                                ; 89 d0                       ; 0xc4085
    mov si, bx                                ; 89 de                       ; 0xc4087
    mov bx, cx                                ; 89 cb                       ; 0xc4089
    test dh, 040h                             ; f6 c6 40                    ; 0xc408b vbe.c:410
    je short 04095h                           ; 74 05                       ; 0xc408e
    mov dx, strict word 00001h                ; ba 01 00                    ; 0xc4090
    jmp short 04097h                          ; eb 02                       ; 0xc4093
    xor dx, dx                                ; 31 d2                       ; 0xc4095
    and ah, 001h                              ; 80 e4 01                    ; 0xc4097 vbe.c:411
    call 03ee7h                               ; e8 4a fe                    ; 0xc409a vbe.c:413
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc409d
    test ax, ax                               ; 85 c0                       ; 0xc40a0 vbe.c:415
    je short 0410bh                           ; 74 67                       ; 0xc40a2
    mov cx, 00100h                            ; b9 00 01                    ; 0xc40a4 vbe.c:420
    xor ax, ax                                ; 31 c0                       ; 0xc40a7
    mov di, bx                                ; 89 df                       ; 0xc40a9
    mov es, si                                ; 8e c6                       ; 0xc40ab
    jcxz 040b1h                               ; e3 02                       ; 0xc40ad
    rep stosb                                 ; f3 aa                       ; 0xc40af
    xor cx, cx                                ; 31 c9                       ; 0xc40b1 vbe.c:421
    jmp short 040bah                          ; eb 05                       ; 0xc40b3
    cmp cx, strict byte 00042h                ; 83 f9 42                    ; 0xc40b5
    jnc short 040d3h                          ; 73 19                       ; 0xc40b8
    mov dx, word [bp-006h]                    ; 8b 56 fa                    ; 0xc40ba vbe.c:424
    inc dx                                    ; 42                          ; 0xc40bd
    inc dx                                    ; 42                          ; 0xc40be
    add dx, cx                                ; 01 ca                       ; 0xc40bf
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc40c1
    call 03e7bh                               ; e8 b4 fd                    ; 0xc40c4
    mov di, bx                                ; 89 df                       ; 0xc40c7 vbe.c:425
    add di, cx                                ; 01 cf                       ; 0xc40c9
    mov es, si                                ; 8e c6                       ; 0xc40cb vbe.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc40cd
    inc cx                                    ; 41                          ; 0xc40d0 vbe.c:426
    jmp short 040b5h                          ; eb e2                       ; 0xc40d1
    lea di, [bx+002h]                         ; 8d 7f 02                    ; 0xc40d3 vbe.c:427
    mov es, si                                ; 8e c6                       ; 0xc40d6 vbe.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc40d8
    test AL, strict byte 001h                 ; a8 01                       ; 0xc40db vbe.c:428
    je short 040efh                           ; 74 10                       ; 0xc40dd
    lea di, [bx+00ch]                         ; 8d 7f 0c                    ; 0xc40df vbe.c:429
    mov word [es:di], 00629h                  ; 26 c7 05 29 06              ; 0xc40e2 vbe.c:62
    lea di, [bx+00eh]                         ; 8d 7f 0e                    ; 0xc40e7 vbe.c:431
    mov word [es:di], 0c000h                  ; 26 c7 05 00 c0              ; 0xc40ea vbe.c:62
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc40ef vbe.c:434
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc40f2
    call 00570h                               ; e8 78 c4                    ; 0xc40f5
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc40f8 vbe.c:435
    call 00577h                               ; e8 79 c4                    ; 0xc40fb
    add bx, strict byte 0002ah                ; 83 c3 2a                    ; 0xc40fe
    mov es, si                                ; 8e c6                       ; 0xc4101 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4103
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc4106 vbe.c:437
    jmp short 0410eh                          ; eb 03                       ; 0xc4109 vbe.c:438
    mov ax, 00100h                            ; b8 00 01                    ; 0xc410b vbe.c:442
    push SS                                   ; 16                          ; 0xc410e vbe.c:445
    pop ES                                    ; 07                          ; 0xc410f
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc4110
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4113
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4116 vbe.c:446
    pop di                                    ; 5f                          ; 0xc4119
    pop si                                    ; 5e                          ; 0xc411a
    pop bp                                    ; 5d                          ; 0xc411b
    retn                                      ; c3                          ; 0xc411c
  ; disGetNextSymbol 0xc411d LB 0x426 -> off=0x0 cb=00000000000000e7 uValue=00000000000c411d 'vbe_biosfn_set_mode'
vbe_biosfn_set_mode:                         ; 0xc411d LB 0xe7
    push bp                                   ; 55                          ; 0xc411d vbe.c:458
    mov bp, sp                                ; 89 e5                       ; 0xc411e
    push si                                   ; 56                          ; 0xc4120
    push di                                   ; 57                          ; 0xc4121
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc4122
    mov si, ax                                ; 89 c6                       ; 0xc4125
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc4127
    test byte [bp-009h], 040h                 ; f6 46 f7 40                 ; 0xc412a vbe.c:466
    je short 04135h                           ; 74 05                       ; 0xc412e
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc4130
    jmp short 04137h                          ; eb 02                       ; 0xc4133
    xor ax, ax                                ; 31 c0                       ; 0xc4135
    mov dx, ax                                ; 89 c2                       ; 0xc4137
    test ax, ax                               ; 85 c0                       ; 0xc4139 vbe.c:467
    je short 04140h                           ; 74 03                       ; 0xc413b
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc413d
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc4140
    test byte [bp-009h], 080h                 ; f6 46 f7 80                 ; 0xc4143 vbe.c:468
    je short 0414eh                           ; 74 05                       ; 0xc4147
    mov ax, 00080h                            ; b8 80 00                    ; 0xc4149
    jmp short 04150h                          ; eb 02                       ; 0xc414c
    xor ax, ax                                ; 31 c0                       ; 0xc414e
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc4150
    and byte [bp-009h], 001h                  ; 80 66 f7 01                 ; 0xc4153 vbe.c:470
    cmp word [bp-00ah], 00100h                ; 81 7e f6 00 01              ; 0xc4157 vbe.c:473
    jnc short 04171h                          ; 73 13                       ; 0xc415c
    xor ax, ax                                ; 31 c0                       ; 0xc415e vbe.c:477
    call 005ddh                               ; e8 7a c4                    ; 0xc4160
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc4163 vbe.c:481
    xor ah, ah                                ; 30 e4                       ; 0xc4166
    call 0142dh                               ; e8 c2 d2                    ; 0xc4168
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc416b vbe.c:482
    jmp near 041f8h                           ; e9 87 00                    ; 0xc416e vbe.c:483
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc4171 vbe.c:486
    call 03ee7h                               ; e8 70 fd                    ; 0xc4174
    mov bx, ax                                ; 89 c3                       ; 0xc4177
    test ax, ax                               ; 85 c0                       ; 0xc4179 vbe.c:488
    je short 041f5h                           ; 74 78                       ; 0xc417b
    lea dx, [bx+014h]                         ; 8d 57 14                    ; 0xc417d vbe.c:493
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4180
    call 03e69h                               ; e8 e3 fc                    ; 0xc4183
    mov cx, ax                                ; 89 c1                       ; 0xc4186
    lea dx, [bx+016h]                         ; 8d 57 16                    ; 0xc4188 vbe.c:494
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc418b
    call 03e69h                               ; e8 d8 fc                    ; 0xc418e
    mov di, ax                                ; 89 c7                       ; 0xc4191
    lea dx, [bx+01bh]                         ; 8d 57 1b                    ; 0xc4193 vbe.c:495
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4196
    call 03e7bh                               ; e8 df fc                    ; 0xc4199
    mov bl, al                                ; 88 c3                       ; 0xc419c
    mov dl, al                                ; 88 c2                       ; 0xc419e
    xor ax, ax                                ; 31 c0                       ; 0xc41a0 vbe.c:503
    call 005ddh                               ; e8 38 c4                    ; 0xc41a2
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc41a5 vbe.c:505
    jne short 041b0h                          ; 75 06                       ; 0xc41a8
    mov ax, strict word 0006ah                ; b8 6a 00                    ; 0xc41aa vbe.c:507
    call 0142dh                               ; e8 7d d2                    ; 0xc41ad
    mov al, dl                                ; 88 d0                       ; 0xc41b0 vbe.c:510
    xor ah, ah                                ; 30 e4                       ; 0xc41b2
    call 03de0h                               ; e8 29 fc                    ; 0xc41b4
    mov ax, cx                                ; 89 c8                       ; 0xc41b7 vbe.c:511
    call 03d89h                               ; e8 cd fb                    ; 0xc41b9
    mov ax, di                                ; 89 f8                       ; 0xc41bc vbe.c:512
    call 03da8h                               ; e8 e7 fb                    ; 0xc41be
    xor ax, ax                                ; 31 c0                       ; 0xc41c1 vbe.c:513
    call 00603h                               ; e8 3d c4                    ; 0xc41c3
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc41c6 vbe.c:514
    or dl, 001h                               ; 80 ca 01                    ; 0xc41c9
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc41cc
    xor ah, ah                                ; 30 e4                       ; 0xc41cf
    or al, dl                                 ; 08 d0                       ; 0xc41d1
    call 005ddh                               ; e8 07 c4                    ; 0xc41d3
    call 006d2h                               ; e8 f9 c4                    ; 0xc41d6 vbe.c:515
    mov bx, 000bah                            ; bb ba 00                    ; 0xc41d9 vbe.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc41dc
    mov es, ax                                ; 8e c0                       ; 0xc41df
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc41e1
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc41e4
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc41e7 vbe.c:518
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc41ea
    mov bx, 00087h                            ; bb 87 00                    ; 0xc41ec vbe.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc41ef
    jmp near 0416bh                           ; e9 76 ff                    ; 0xc41f2
    mov ax, 00100h                            ; b8 00 01                    ; 0xc41f5 vbe.c:527
    push SS                                   ; 16                          ; 0xc41f8 vbe.c:531
    pop ES                                    ; 07                          ; 0xc41f9
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc41fa
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc41fd vbe.c:532
    pop di                                    ; 5f                          ; 0xc4200
    pop si                                    ; 5e                          ; 0xc4201
    pop bp                                    ; 5d                          ; 0xc4202
    retn                                      ; c3                          ; 0xc4203
  ; disGetNextSymbol 0xc4204 LB 0x33f -> off=0x0 cb=0000000000000008 uValue=00000000000c4204 'vbe_biosfn_read_video_state_size'
vbe_biosfn_read_video_state_size:            ; 0xc4204 LB 0x8
    push bp                                   ; 55                          ; 0xc4204 vbe.c:534
    mov bp, sp                                ; 89 e5                       ; 0xc4205
    mov ax, strict word 00012h                ; b8 12 00                    ; 0xc4207 vbe.c:537
    pop bp                                    ; 5d                          ; 0xc420a
    retn                                      ; c3                          ; 0xc420b
  ; disGetNextSymbol 0xc420c LB 0x337 -> off=0x0 cb=000000000000004b uValue=00000000000c420c 'vbe_biosfn_save_video_state'
vbe_biosfn_save_video_state:                 ; 0xc420c LB 0x4b
    push bp                                   ; 55                          ; 0xc420c vbe.c:539
    mov bp, sp                                ; 89 e5                       ; 0xc420d
    push bx                                   ; 53                          ; 0xc420f
    push cx                                   ; 51                          ; 0xc4210
    push si                                   ; 56                          ; 0xc4211
    mov si, ax                                ; 89 c6                       ; 0xc4212
    mov bx, dx                                ; 89 d3                       ; 0xc4214
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc4216 vbe.c:543
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4219
    out DX, ax                                ; ef                          ; 0xc421c
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc421d vbe.c:544
    in ax, DX                                 ; ed                          ; 0xc4220
    mov es, si                                ; 8e c6                       ; 0xc4221 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4223
    inc bx                                    ; 43                          ; 0xc4226 vbe.c:546
    inc bx                                    ; 43                          ; 0xc4227
    test AL, strict byte 001h                 ; a8 01                       ; 0xc4228 vbe.c:547
    je short 0424fh                           ; 74 23                       ; 0xc422a
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc422c vbe.c:549
    jmp short 04236h                          ; eb 05                       ; 0xc422f
    cmp cx, strict byte 00009h                ; 83 f9 09                    ; 0xc4231
    jnbe short 0424fh                         ; 77 19                       ; 0xc4234
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc4236 vbe.c:550
    je short 0424ch                           ; 74 11                       ; 0xc4239
    mov ax, cx                                ; 89 c8                       ; 0xc423b vbe.c:551
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc423d
    out DX, ax                                ; ef                          ; 0xc4240
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4241 vbe.c:552
    in ax, DX                                 ; ed                          ; 0xc4244
    mov es, si                                ; 8e c6                       ; 0xc4245 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4247
    inc bx                                    ; 43                          ; 0xc424a vbe.c:553
    inc bx                                    ; 43                          ; 0xc424b
    inc cx                                    ; 41                          ; 0xc424c vbe.c:555
    jmp short 04231h                          ; eb e2                       ; 0xc424d
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc424f vbe.c:556
    pop si                                    ; 5e                          ; 0xc4252
    pop cx                                    ; 59                          ; 0xc4253
    pop bx                                    ; 5b                          ; 0xc4254
    pop bp                                    ; 5d                          ; 0xc4255
    retn                                      ; c3                          ; 0xc4256
  ; disGetNextSymbol 0xc4257 LB 0x2ec -> off=0x0 cb=000000000000008f uValue=00000000000c4257 'vbe_biosfn_restore_video_state'
vbe_biosfn_restore_video_state:              ; 0xc4257 LB 0x8f
    push bp                                   ; 55                          ; 0xc4257 vbe.c:559
    mov bp, sp                                ; 89 e5                       ; 0xc4258
    push bx                                   ; 53                          ; 0xc425a
    push cx                                   ; 51                          ; 0xc425b
    push si                                   ; 56                          ; 0xc425c
    push ax                                   ; 50                          ; 0xc425d
    mov cx, ax                                ; 89 c1                       ; 0xc425e
    mov bx, dx                                ; 89 d3                       ; 0xc4260
    mov es, ax                                ; 8e c0                       ; 0xc4262 vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4264
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc4267
    inc bx                                    ; 43                          ; 0xc426a vbe.c:564
    inc bx                                    ; 43                          ; 0xc426b
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc426c vbe.c:566
    jne short 04282h                          ; 75 10                       ; 0xc4270
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc4272 vbe.c:567
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4275
    out DX, ax                                ; ef                          ; 0xc4278
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc4279 vbe.c:568
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc427c
    out DX, ax                                ; ef                          ; 0xc427f
    jmp short 042deh                          ; eb 5c                       ; 0xc4280 vbe.c:569
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc4282 vbe.c:570
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4285
    out DX, ax                                ; ef                          ; 0xc4288
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4289 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc428c vbe.c:58
    out DX, ax                                ; ef                          ; 0xc428f
    inc bx                                    ; 43                          ; 0xc4290 vbe.c:572
    inc bx                                    ; 43                          ; 0xc4291
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc4292
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4295
    out DX, ax                                ; ef                          ; 0xc4298
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4299 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc429c vbe.c:58
    out DX, ax                                ; ef                          ; 0xc429f
    inc bx                                    ; 43                          ; 0xc42a0 vbe.c:575
    inc bx                                    ; 43                          ; 0xc42a1
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc42a2
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42a5
    out DX, ax                                ; ef                          ; 0xc42a8
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc42a9 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42ac vbe.c:58
    out DX, ax                                ; ef                          ; 0xc42af
    inc bx                                    ; 43                          ; 0xc42b0 vbe.c:578
    inc bx                                    ; 43                          ; 0xc42b1
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc42b2
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42b5
    out DX, ax                                ; ef                          ; 0xc42b8
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc42b9 vbe.c:580
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42bc
    out DX, ax                                ; ef                          ; 0xc42bf
    mov si, strict word 00005h                ; be 05 00                    ; 0xc42c0 vbe.c:582
    jmp short 042cah                          ; eb 05                       ; 0xc42c3
    cmp si, strict byte 00009h                ; 83 fe 09                    ; 0xc42c5
    jnbe short 042deh                         ; 77 14                       ; 0xc42c8
    mov ax, si                                ; 89 f0                       ; 0xc42ca vbe.c:583
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42cc
    out DX, ax                                ; ef                          ; 0xc42cf
    mov es, cx                                ; 8e c1                       ; 0xc42d0 vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc42d2
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42d5 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc42d8
    inc bx                                    ; 43                          ; 0xc42d9 vbe.c:585
    inc bx                                    ; 43                          ; 0xc42da
    inc si                                    ; 46                          ; 0xc42db vbe.c:586
    jmp short 042c5h                          ; eb e7                       ; 0xc42dc
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc42de vbe.c:588
    pop si                                    ; 5e                          ; 0xc42e1
    pop cx                                    ; 59                          ; 0xc42e2
    pop bx                                    ; 5b                          ; 0xc42e3
    pop bp                                    ; 5d                          ; 0xc42e4
    retn                                      ; c3                          ; 0xc42e5
  ; disGetNextSymbol 0xc42e6 LB 0x25d -> off=0x0 cb=000000000000008c uValue=00000000000c42e6 'vbe_biosfn_save_restore_state'
vbe_biosfn_save_restore_state:               ; 0xc42e6 LB 0x8c
    push bp                                   ; 55                          ; 0xc42e6 vbe.c:604
    mov bp, sp                                ; 89 e5                       ; 0xc42e7
    push si                                   ; 56                          ; 0xc42e9
    push di                                   ; 57                          ; 0xc42ea
    push ax                                   ; 50                          ; 0xc42eb
    mov si, ax                                ; 89 c6                       ; 0xc42ec
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc42ee
    mov ax, bx                                ; 89 d8                       ; 0xc42f1
    mov bx, word [bp+004h]                    ; 8b 5e 04                    ; 0xc42f3
    mov di, strict word 0004fh                ; bf 4f 00                    ; 0xc42f6 vbe.c:609
    xor ah, ah                                ; 30 e4                       ; 0xc42f9 vbe.c:610
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc42fb
    je short 04345h                           ; 74 45                       ; 0xc42fe
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc4300
    je short 04329h                           ; 74 24                       ; 0xc4303
    test ax, ax                               ; 85 c0                       ; 0xc4305
    jne short 04361h                          ; 75 58                       ; 0xc4307
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc4309 vbe.c:612
    call 0325dh                               ; e8 4e ef                    ; 0xc430c
    mov cx, ax                                ; 89 c1                       ; 0xc430f
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc4311 vbe.c:616
    je short 0431ch                           ; 74 05                       ; 0xc4315
    call 04204h                               ; e8 ea fe                    ; 0xc4317 vbe.c:617
    add ax, cx                                ; 01 c8                       ; 0xc431a
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc431c vbe.c:618
    shr ax, 006h                              ; c1 e8 06                    ; 0xc431f
    push SS                                   ; 16                          ; 0xc4322
    pop ES                                    ; 07                          ; 0xc4323
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4324
    jmp short 04364h                          ; eb 3b                       ; 0xc4327 vbe.c:619
    push SS                                   ; 16                          ; 0xc4329 vbe.c:621
    pop ES                                    ; 07                          ; 0xc432a
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc432b
    mov dx, cx                                ; 89 ca                       ; 0xc432e vbe.c:622
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc4330
    call 03298h                               ; e8 62 ef                    ; 0xc4333
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc4336 vbe.c:626
    je short 04364h                           ; 74 28                       ; 0xc433a
    mov dx, ax                                ; 89 c2                       ; 0xc433c vbe.c:627
    mov ax, cx                                ; 89 c8                       ; 0xc433e
    call 0420ch                               ; e8 c9 fe                    ; 0xc4340
    jmp short 04364h                          ; eb 1f                       ; 0xc4343 vbe.c:628
    push SS                                   ; 16                          ; 0xc4345 vbe.c:630
    pop ES                                    ; 07                          ; 0xc4346
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc4347
    mov dx, cx                                ; 89 ca                       ; 0xc434a vbe.c:631
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc434c
    call 03570h                               ; e8 1e f2                    ; 0xc434f
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc4352 vbe.c:635
    je short 04364h                           ; 74 0c                       ; 0xc4356
    mov dx, ax                                ; 89 c2                       ; 0xc4358 vbe.c:636
    mov ax, cx                                ; 89 c8                       ; 0xc435a
    call 04257h                               ; e8 f8 fe                    ; 0xc435c
    jmp short 04364h                          ; eb 03                       ; 0xc435f vbe.c:637
    mov di, 00100h                            ; bf 00 01                    ; 0xc4361 vbe.c:640
    push SS                                   ; 16                          ; 0xc4364 vbe.c:643
    pop ES                                    ; 07                          ; 0xc4365
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc4366
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4369 vbe.c:644
    pop di                                    ; 5f                          ; 0xc436c
    pop si                                    ; 5e                          ; 0xc436d
    pop bp                                    ; 5d                          ; 0xc436e
    retn 00002h                               ; c2 02 00                    ; 0xc436f
  ; disGetNextSymbol 0xc4372 LB 0x1d1 -> off=0x0 cb=00000000000000df uValue=00000000000c4372 'vbe_biosfn_get_set_scanline_length'
vbe_biosfn_get_set_scanline_length:          ; 0xc4372 LB 0xdf
    push bp                                   ; 55                          ; 0xc4372 vbe.c:665
    mov bp, sp                                ; 89 e5                       ; 0xc4373
    push si                                   ; 56                          ; 0xc4375
    push di                                   ; 57                          ; 0xc4376
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc4377
    push ax                                   ; 50                          ; 0xc437a
    mov di, dx                                ; 89 d7                       ; 0xc437b
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc437d
    mov si, cx                                ; 89 ce                       ; 0xc4380
    call 03dffh                               ; e8 7a fa                    ; 0xc4382 vbe.c:674
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc4385 vbe.c:675
    jne short 0438eh                          ; 75 05                       ; 0xc4387
    mov bx, strict word 00010h                ; bb 10 00                    ; 0xc4389
    jmp short 04392h                          ; eb 04                       ; 0xc438c
    xor ah, ah                                ; 30 e4                       ; 0xc438e
    mov bx, ax                                ; 89 c3                       ; 0xc4390
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc4392
    call 03e37h                               ; e8 9f fa                    ; 0xc4395 vbe.c:676
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc4398
    mov word [bp-00ch], strict word 0004fh    ; c7 46 f4 4f 00              ; 0xc439b vbe.c:677
    push SS                                   ; 16                          ; 0xc43a0 vbe.c:678
    pop ES                                    ; 07                          ; 0xc43a1
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc43a2
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc43a5
    mov cl, byte [es:di]                      ; 26 8a 0d                    ; 0xc43a8 vbe.c:679
    cmp cl, 002h                              ; 80 f9 02                    ; 0xc43ab vbe.c:683
    je short 043bch                           ; 74 0c                       ; 0xc43ae
    cmp cl, 001h                              ; 80 f9 01                    ; 0xc43b0
    je short 043e2h                           ; 74 2d                       ; 0xc43b3
    test cl, cl                               ; 84 c9                       ; 0xc43b5
    je short 043ddh                           ; 74 24                       ; 0xc43b7
    jmp near 0443ah                           ; e9 7e 00                    ; 0xc43b9
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc43bc vbe.c:685
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc43bf
    jne short 043c8h                          ; 75 05                       ; 0xc43c1
    sal bx, 003h                              ; c1 e3 03                    ; 0xc43c3 vbe.c:686
    jmp short 043ddh                          ; eb 15                       ; 0xc43c6 vbe.c:687
    xor ah, ah                                ; 30 e4                       ; 0xc43c8 vbe.c:688
    cwd                                       ; 99                          ; 0xc43ca
    sal dx, 003h                              ; c1 e2 03                    ; 0xc43cb
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc43ce
    sar ax, 003h                              ; c1 f8 03                    ; 0xc43d0
    mov cx, ax                                ; 89 c1                       ; 0xc43d3
    mov ax, bx                                ; 89 d8                       ; 0xc43d5
    xor dx, dx                                ; 31 d2                       ; 0xc43d7
    div cx                                    ; f7 f1                       ; 0xc43d9
    mov bx, ax                                ; 89 c3                       ; 0xc43db
    mov ax, bx                                ; 89 d8                       ; 0xc43dd vbe.c:691
    call 03e18h                               ; e8 36 fa                    ; 0xc43df
    call 03e37h                               ; e8 52 fa                    ; 0xc43e2 vbe.c:694
    mov cx, ax                                ; 89 c1                       ; 0xc43e5
    push SS                                   ; 16                          ; 0xc43e7 vbe.c:695
    pop ES                                    ; 07                          ; 0xc43e8
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc43e9
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc43ec
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc43ef vbe.c:696
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc43f2
    jne short 043fdh                          ; 75 07                       ; 0xc43f4
    mov bx, cx                                ; 89 cb                       ; 0xc43f6 vbe.c:697
    shr bx, 003h                              ; c1 eb 03                    ; 0xc43f8
    jmp short 04410h                          ; eb 13                       ; 0xc43fb vbe.c:698
    xor ah, ah                                ; 30 e4                       ; 0xc43fd vbe.c:699
    cwd                                       ; 99                          ; 0xc43ff
    sal dx, 003h                              ; c1 e2 03                    ; 0xc4400
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc4403
    sar ax, 003h                              ; c1 f8 03                    ; 0xc4405
    mov bx, ax                                ; 89 c3                       ; 0xc4408
    mov ax, cx                                ; 89 c8                       ; 0xc440a
    mul bx                                    ; f7 e3                       ; 0xc440c
    mov bx, ax                                ; 89 c3                       ; 0xc440e
    add bx, strict byte 00003h                ; 83 c3 03                    ; 0xc4410 vbe.c:700
    and bl, 0fch                              ; 80 e3 fc                    ; 0xc4413
    push SS                                   ; 16                          ; 0xc4416 vbe.c:701
    pop ES                                    ; 07                          ; 0xc4417
    mov word [es:di], bx                      ; 26 89 1d                    ; 0xc4418
    call 03e50h                               ; e8 32 fa                    ; 0xc441b vbe.c:702
    push SS                                   ; 16                          ; 0xc441e
    pop ES                                    ; 07                          ; 0xc441f
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc4420
    call 03dc7h                               ; e8 a1 f9                    ; 0xc4423 vbe.c:703
    push SS                                   ; 16                          ; 0xc4426
    pop ES                                    ; 07                          ; 0xc4427
    cmp ax, word [es:si]                      ; 26 3b 04                    ; 0xc4428
    jbe short 0443fh                          ; 76 12                       ; 0xc442b
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc442d vbe.c:704
    call 03e18h                               ; e8 e5 f9                    ; 0xc4430
    mov word [bp-00ch], 00200h                ; c7 46 f4 00 02              ; 0xc4433 vbe.c:705
    jmp short 0443fh                          ; eb 05                       ; 0xc4438 vbe.c:707
    mov word [bp-00ch], 00100h                ; c7 46 f4 00 01              ; 0xc443a vbe.c:710
    push SS                                   ; 16                          ; 0xc443f vbe.c:713
    pop ES                                    ; 07                          ; 0xc4440
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc4441
    mov bx, word [bp-00eh]                    ; 8b 5e f2                    ; 0xc4444
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4447
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc444a vbe.c:714
    pop di                                    ; 5f                          ; 0xc444d
    pop si                                    ; 5e                          ; 0xc444e
    pop bp                                    ; 5d                          ; 0xc444f
    retn                                      ; c3                          ; 0xc4450
  ; disGetNextSymbol 0xc4451 LB 0xf2 -> off=0x0 cb=00000000000000f2 uValue=00000000000c4451 'private_biosfn_custom_mode'
private_biosfn_custom_mode:                  ; 0xc4451 LB 0xf2
    push bp                                   ; 55                          ; 0xc4451 vbe.c:740
    mov bp, sp                                ; 89 e5                       ; 0xc4452
    push si                                   ; 56                          ; 0xc4454
    push di                                   ; 57                          ; 0xc4455
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc4456
    mov di, ax                                ; 89 c7                       ; 0xc4459
    mov si, dx                                ; 89 d6                       ; 0xc445b
    mov dx, cx                                ; 89 ca                       ; 0xc445d
    mov word [bp-00ah], strict word 0004fh    ; c7 46 f6 4f 00              ; 0xc445f vbe.c:753
    push SS                                   ; 16                          ; 0xc4464 vbe.c:754
    pop ES                                    ; 07                          ; 0xc4465
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc4466
    test al, al                               ; 84 c0                       ; 0xc4469 vbe.c:755
    jne short 0448fh                          ; 75 22                       ; 0xc446b
    push SS                                   ; 16                          ; 0xc446d vbe.c:757
    pop ES                                    ; 07                          ; 0xc446e
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc446f
    mov bx, dx                                ; 89 d3                       ; 0xc4472 vbe.c:758
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc4474
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc4477 vbe.c:759
    shr ax, 008h                              ; c1 e8 08                    ; 0xc447a
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc447d
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc4480
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc4483 vbe.c:764
    je short 04497h                           ; 74 10                       ; 0xc4485
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc4487
    je short 04497h                           ; 74 0c                       ; 0xc4489
    cmp AL, strict byte 020h                  ; 3c 20                       ; 0xc448b
    je short 04497h                           ; 74 08                       ; 0xc448d
    mov word [bp-00ah], 00100h                ; c7 46 f6 00 01              ; 0xc448f vbe.c:765
    jmp near 04534h                           ; e9 9d 00                    ; 0xc4494 vbe.c:766
    push SS                                   ; 16                          ; 0xc4497 vbe.c:770
    pop ES                                    ; 07                          ; 0xc4498
    test byte [es:si+001h], 080h              ; 26 f6 44 01 80              ; 0xc4499
    je short 044a5h                           ; 74 05                       ; 0xc449e
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc44a0
    jmp short 044a7h                          ; eb 02                       ; 0xc44a3
    xor ax, ax                                ; 31 c0                       ; 0xc44a5
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc44a7
    cmp cx, 00280h                            ; 81 f9 80 02                 ; 0xc44aa vbe.c:773
    jnc short 044b5h                          ; 73 05                       ; 0xc44ae
    mov cx, 00280h                            ; b9 80 02                    ; 0xc44b0 vbe.c:774
    jmp short 044beh                          ; eb 09                       ; 0xc44b3 vbe.c:775
    cmp cx, 00a00h                            ; 81 f9 00 0a                 ; 0xc44b5
    jbe short 044beh                          ; 76 03                       ; 0xc44b9
    mov cx, 00a00h                            ; b9 00 0a                    ; 0xc44bb vbe.c:776
    cmp bx, 001e0h                            ; 81 fb e0 01                 ; 0xc44be vbe.c:777
    jnc short 044c9h                          ; 73 05                       ; 0xc44c2
    mov bx, 001e0h                            ; bb e0 01                    ; 0xc44c4 vbe.c:778
    jmp short 044d2h                          ; eb 09                       ; 0xc44c7 vbe.c:779
    cmp bx, 00780h                            ; 81 fb 80 07                 ; 0xc44c9
    jbe short 044d2h                          ; 76 03                       ; 0xc44cd
    mov bx, 00780h                            ; bb 80 07                    ; 0xc44cf vbe.c:780
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc44d2 vbe.c:786
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc44d5
    call 03e69h                               ; e8 8e f9                    ; 0xc44d8
    mov si, ax                                ; 89 c6                       ; 0xc44db
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc44dd vbe.c:789
    xor ah, ah                                ; 30 e4                       ; 0xc44e0
    cwd                                       ; 99                          ; 0xc44e2
    sal dx, 003h                              ; c1 e2 03                    ; 0xc44e3
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc44e6
    sar ax, 003h                              ; c1 f8 03                    ; 0xc44e8
    mov dx, ax                                ; 89 c2                       ; 0xc44eb
    mov ax, cx                                ; 89 c8                       ; 0xc44ed
    mul dx                                    ; f7 e2                       ; 0xc44ef
    add ax, strict word 00003h                ; 05 03 00                    ; 0xc44f1 vbe.c:790
    and AL, strict byte 0fch                  ; 24 fc                       ; 0xc44f4
    mov dx, bx                                ; 89 da                       ; 0xc44f6 vbe.c:792
    mul dx                                    ; f7 e2                       ; 0xc44f8
    cmp dx, si                                ; 39 f2                       ; 0xc44fa vbe.c:794
    jnbe short 04504h                         ; 77 06                       ; 0xc44fc
    jne short 0450bh                          ; 75 0b                       ; 0xc44fe
    test ax, ax                               ; 85 c0                       ; 0xc4500
    jbe short 0450bh                          ; 76 07                       ; 0xc4502
    mov word [bp-00ah], 00200h                ; c7 46 f6 00 02              ; 0xc4504 vbe.c:796
    jmp short 04534h                          ; eb 29                       ; 0xc4509 vbe.c:797
    xor ax, ax                                ; 31 c0                       ; 0xc450b vbe.c:801
    call 005ddh                               ; e8 cd c0                    ; 0xc450d
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc4510 vbe.c:802
    xor ah, ah                                ; 30 e4                       ; 0xc4513
    call 03de0h                               ; e8 c8 f8                    ; 0xc4515
    mov ax, cx                                ; 89 c8                       ; 0xc4518 vbe.c:803
    call 03d89h                               ; e8 6c f8                    ; 0xc451a
    mov ax, bx                                ; 89 d8                       ; 0xc451d vbe.c:804
    call 03da8h                               ; e8 86 f8                    ; 0xc451f
    xor ax, ax                                ; 31 c0                       ; 0xc4522 vbe.c:805
    call 00603h                               ; e8 dc c0                    ; 0xc4524
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc4527 vbe.c:806
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc452a
    xor ah, ah                                ; 30 e4                       ; 0xc452c
    call 005ddh                               ; e8 ac c0                    ; 0xc452e
    call 006d2h                               ; e8 9e c1                    ; 0xc4531 vbe.c:807
    push SS                                   ; 16                          ; 0xc4534 vbe.c:815
    pop ES                                    ; 07                          ; 0xc4535
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc4536
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc4539
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc453c vbe.c:816
    pop di                                    ; 5f                          ; 0xc453f
    pop si                                    ; 5e                          ; 0xc4540
    pop bp                                    ; 5d                          ; 0xc4541
    retn                                      ; c3                          ; 0xc4542

  ; Padding 0xfd bytes at 0xc4543
  times 253 db 0

section VBE32 progbits vstart=0x4640 align=1 ; size=0x115 class=CODE group=AUTO
  ; disGetNextSymbol 0xc4640 LB 0x115 -> off=0x0 cb=0000000000000114 uValue=00000000000c0000 'vesa_pm_start'
vesa_pm_start:                               ; 0xc4640 LB 0x114
    sbb byte [bx+si], al                      ; 18 00                       ; 0xc4640
    dec di                                    ; 4f                          ; 0xc4642
    add byte [bx+si], dl                      ; 00 10                       ; 0xc4643
    add word [bx+si], cx                      ; 01 08                       ; 0xc4645
    add dh, cl                                ; 00 ce                       ; 0xc4647
    add di, cx                                ; 01 cf                       ; 0xc4649
    add di, cx                                ; 01 cf                       ; 0xc464b
    add ax, dx                                ; 01 d0                       ; 0xc464d
    add word [bp-048fdh], si                  ; 01 b6 03 b7                 ; 0xc464f
    db  003h, 0ffh
    ; add di, di                                ; 03 ff                     ; 0xc4653
    db  0ffh
    db  0ffh
    jmp word [bp-07dh]                        ; ff 66 83                    ; 0xc4657
    sti                                       ; fb                          ; 0xc465a
    add byte [si+005h], dh                    ; 00 74 05                    ; 0xc465b
    mov eax, strict dword 066c30100h          ; 66 b8 00 01 c3 66           ; 0xc465e vberom.asm:825
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc4664
    push edx                                  ; 66 52                       ; 0xc4666 vberom.asm:829
    push eax                                  ; 66 50                       ; 0xc4668 vberom.asm:830
    mov edx, strict dword 0b86601ceh          ; 66 ba ce 01 66 b8           ; 0xc466a vberom.asm:831
    add ax, 06600h                            ; 05 00 66                    ; 0xc4670
    out DX, ax                                ; ef                          ; 0xc4673
    pop eax                                   ; 66 58                       ; 0xc4674 vberom.asm:834
    mov edx, strict dword 0ef6601cfh          ; 66 ba cf 01 66 ef           ; 0xc4676 vberom.asm:835
    in eax, DX                                ; 66 ed                       ; 0xc467c vberom.asm:837
    pop edx                                   ; 66 5a                       ; 0xc467e vberom.asm:838
    db  066h, 03bh, 0d0h
    ; cmp edx, eax                              ; 66 3b d0                  ; 0xc4680 vberom.asm:839
    jne short 0468ah                          ; 75 05                       ; 0xc4683 vberom.asm:840
    mov eax, strict dword 066c3004fh          ; 66 b8 4f 00 c3 66           ; 0xc4685 vberom.asm:841
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc468b
    retn                                      ; c3                          ; 0xc468e vberom.asm:845
    cmp bl, 080h                              ; 80 fb 80                    ; 0xc468f vberom.asm:847
    je short 0469eh                           ; 74 0a                       ; 0xc4692 vberom.asm:848
    cmp bl, 000h                              ; 80 fb 00                    ; 0xc4694 vberom.asm:849
    je short 046aeh                           ; 74 15                       ; 0xc4697 vberom.asm:850
    mov eax, strict dword 052c30100h          ; 66 b8 00 01 c3 52           ; 0xc4699 vberom.asm:851
    mov edx, strict dword 0a8ec03dah          ; 66 ba da 03 ec a8           ; 0xc469f vberom.asm:855
    or byte [di-005h], dh                     ; 08 75 fb                    ; 0xc46a5
    in AL, DX                                 ; ec                          ; 0xc46a8 vberom.asm:861
    test AL, strict byte 008h                 ; a8 08                       ; 0xc46a9 vberom.asm:862
    je short 046a8h                           ; 74 fb                       ; 0xc46ab vberom.asm:863
    pop dx                                    ; 5a                          ; 0xc46ad vberom.asm:864
    push ax                                   ; 50                          ; 0xc46ae vberom.asm:868
    push cx                                   ; 51                          ; 0xc46af vberom.asm:869
    push dx                                   ; 52                          ; 0xc46b0 vberom.asm:870
    push si                                   ; 56                          ; 0xc46b1 vberom.asm:871
    push di                                   ; 57                          ; 0xc46b2 vberom.asm:872
    sal dx, 010h                              ; c1 e2 10                    ; 0xc46b3 vberom.asm:873
    and cx, strict word 0ffffh                ; 81 e1 ff ff                 ; 0xc46b6 vberom.asm:874
    add byte [bx+si], al                      ; 00 00                       ; 0xc46ba
    db  00bh, 0cah
    ; or cx, dx                                 ; 0b ca                     ; 0xc46bc vberom.asm:875
    sal cx, 002h                              ; c1 e1 02                    ; 0xc46be vberom.asm:876
    db  08bh, 0c1h
    ; mov ax, cx                                ; 8b c1                     ; 0xc46c1 vberom.asm:877
    push ax                                   ; 50                          ; 0xc46c3 vberom.asm:878
    mov edx, strict dword 0b86601ceh          ; 66 ba ce 01 66 b8           ; 0xc46c4 vberom.asm:879
    push ES                                   ; 06                          ; 0xc46ca
    add byte [bp-011h], ah                    ; 00 66 ef                    ; 0xc46cb
    mov edx, strict dword 0ed6601cfh          ; 66 ba cf 01 66 ed           ; 0xc46ce vberom.asm:882
    db  00fh, 0b7h, 0c8h
    ; movzx cx, ax                              ; 0f b7 c8                  ; 0xc46d4 vberom.asm:884
    mov edx, strict dword 0b86601ceh          ; 66 ba ce 01 66 b8           ; 0xc46d7 vberom.asm:885
    add ax, word [bx+si]                      ; 03 00                       ; 0xc46dd
    out DX, eax                               ; 66 ef                       ; 0xc46df vberom.asm:887
    mov edx, strict dword 0ed6601cfh          ; 66 ba cf 01 66 ed           ; 0xc46e1 vberom.asm:888
    db  00fh, 0b7h, 0f0h
    ; movzx si, ax                              ; 0f b7 f0                  ; 0xc46e7 vberom.asm:890
    pop ax                                    ; 58                          ; 0xc46ea vberom.asm:891
    cmp si, strict byte 00004h                ; 83 fe 04                    ; 0xc46eb vberom.asm:893
    je short 04707h                           ; 74 17                       ; 0xc46ee vberom.asm:894
    add si, strict byte 00007h                ; 83 c6 07                    ; 0xc46f0 vberom.asm:895
    shr si, 003h                              ; c1 ee 03                    ; 0xc46f3 vberom.asm:896
    imul cx, si                               ; 0f af ce                    ; 0xc46f6 vberom.asm:897
    db  033h, 0d2h
    ; xor dx, dx                                ; 33 d2                     ; 0xc46f9 vberom.asm:898
    div cx                                    ; f7 f1                       ; 0xc46fb vberom.asm:899
    db  08bh, 0f8h
    ; mov di, ax                                ; 8b f8                     ; 0xc46fd vberom.asm:900
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc46ff vberom.asm:901
    db  033h, 0d2h
    ; xor dx, dx                                ; 33 d2                     ; 0xc4701 vberom.asm:902
    div si                                    ; f7 f6                       ; 0xc4703 vberom.asm:903
    jmp short 04713h                          ; eb 0c                       ; 0xc4705 vberom.asm:904
    shr cx, 1                                 ; d1 e9                       ; 0xc4707 vberom.asm:907
    db  033h, 0d2h
    ; xor dx, dx                                ; 33 d2                     ; 0xc4709 vberom.asm:908
    div cx                                    ; f7 f1                       ; 0xc470b vberom.asm:909
    db  08bh, 0f8h
    ; mov di, ax                                ; 8b f8                     ; 0xc470d vberom.asm:910
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc470f vberom.asm:911
    sal ax, 1                                 ; d1 e0                       ; 0xc4711 vberom.asm:912
    push edx                                  ; 66 52                       ; 0xc4713 vberom.asm:915
    push eax                                  ; 66 50                       ; 0xc4715 vberom.asm:916
    mov edx, strict dword 0b86601ceh          ; 66 ba ce 01 66 b8           ; 0xc4717 vberom.asm:917
    or byte [bx+si], al                       ; 08 00                       ; 0xc471d
    out DX, eax                               ; 66 ef                       ; 0xc471f vberom.asm:919
    pop eax                                   ; 66 58                       ; 0xc4721 vberom.asm:920
    mov edx, strict dword 0ef6601cfh          ; 66 ba cf 01 66 ef           ; 0xc4723 vberom.asm:921
    pop edx                                   ; 66 5a                       ; 0xc4729 vberom.asm:923
    db  066h, 08bh, 0c7h
    ; mov eax, edi                              ; 66 8b c7                  ; 0xc472b vberom.asm:925
    push edx                                  ; 66 52                       ; 0xc472e vberom.asm:926
    push eax                                  ; 66 50                       ; 0xc4730 vberom.asm:927
    mov edx, strict dword 0b86601ceh          ; 66 ba ce 01 66 b8           ; 0xc4732 vberom.asm:928
    or word [bx+si], ax                       ; 09 00                       ; 0xc4738
    out DX, eax                               ; 66 ef                       ; 0xc473a vberom.asm:930
    pop eax                                   ; 66 58                       ; 0xc473c vberom.asm:931
    mov edx, strict dword 0ef6601cfh          ; 66 ba cf 01 66 ef           ; 0xc473e vberom.asm:932
    pop edx                                   ; 66 5a                       ; 0xc4744 vberom.asm:934
    pop di                                    ; 5f                          ; 0xc4746 vberom.asm:936
    pop si                                    ; 5e                          ; 0xc4747 vberom.asm:937
    pop dx                                    ; 5a                          ; 0xc4748 vberom.asm:938
    pop cx                                    ; 59                          ; 0xc4749 vberom.asm:939
    pop ax                                    ; 58                          ; 0xc474a vberom.asm:940
    mov eax, strict dword 066c3004fh          ; 66 b8 4f 00 c3 66           ; 0xc474b vberom.asm:941
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc4751
  ; disGetNextSymbol 0xc4754 LB 0x1 -> off=0x0 cb=0000000000000001 uValue=0000000000000114 'vesa_pm_end'
vesa_pm_end:                                 ; 0xc4754 LB 0x1
    retn                                      ; c3                          ; 0xc4754 vberom.asm:946

  ; Padding 0x2b bytes at 0xc4755
  times 43 db 0

section _DATA progbits vstart=0x4780 align=1 ; size=0x3745 class=DATA group=DGROUP
  ; disGetNextSymbol 0xc4780 LB 0x3745 -> off=0x0 cb=000000000000002c uValue=00000000000c0000 '_msg_vga_init'
_msg_vga_init:                               ; 0xc4780 LB 0x2c
    db  'Oracle VirtualBox Version 7.2.16 VGA BIOS', 00dh, 00ah, 000h
  ; disGetNextSymbol 0xc47ac LB 0x3719 -> off=0x0 cb=0000000000000080 uValue=00000000000c002c 'vga_modes'
vga_modes:                                   ; 0xc47ac LB 0x80
    db  000h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h, 001h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h
    db  002h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h, 003h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h
    db  004h, 001h, 002h, 002h, 000h, 0b8h, 0ffh, 001h, 005h, 001h, 002h, 002h, 000h, 0b8h, 0ffh, 001h
    db  006h, 001h, 002h, 001h, 000h, 0b8h, 0ffh, 001h, 007h, 000h, 001h, 004h, 000h, 0b0h, 0ffh, 000h
    db  00dh, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 001h, 00eh, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 001h
    db  00fh, 001h, 003h, 001h, 000h, 0a0h, 0ffh, 000h, 010h, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 002h
    db  011h, 001h, 003h, 001h, 000h, 0a0h, 0ffh, 002h, 012h, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 002h
    db  013h, 001h, 005h, 008h, 000h, 0a0h, 0ffh, 003h, 06ah, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 002h
  ; disGetNextSymbol 0xc482c LB 0x3699 -> off=0x0 cb=0000000000000010 uValue=00000000000c00ac 'line_to_vpti'
line_to_vpti:                                ; 0xc482c LB 0x10
    db  017h, 017h, 018h, 018h, 004h, 005h, 006h, 007h, 00dh, 00eh, 011h, 012h, 01ah, 01bh, 01ch, 01dh
  ; disGetNextSymbol 0xc483c LB 0x3689 -> off=0x0 cb=0000000000000004 uValue=00000000000c00bc 'dac_regs'
dac_regs:                                    ; 0xc483c LB 0x4
    dd  0ff3f3f3fh
  ; disGetNextSymbol 0xc4840 LB 0x3685 -> off=0x0 cb=0000000000000780 uValue=00000000000c00c0 'video_param_table'
video_param_table:                           ; 0xc4840 LB 0x780
    db  028h, 018h, 008h, 000h, 008h, 009h, 003h, 000h, 002h, 063h, 02dh, 027h, 028h, 090h, 02bh, 0a0h
    db  0bfh, 01fh, 000h, 0c7h, 006h, 007h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 014h, 01fh, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 008h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  028h, 018h, 008h, 000h, 008h, 009h, 003h, 000h, 002h, 063h, 02dh, 027h, 028h, 090h, 02bh, 0a0h
    db  0bfh, 01fh, 000h, 0c7h, 006h, 007h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 014h, 01fh, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 008h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  050h, 018h, 008h, 000h, 010h, 001h, 003h, 000h, 002h, 063h, 05fh, 04fh, 050h, 082h, 055h, 081h
    db  0bfh, 01fh, 000h, 0c7h, 006h, 007h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 028h, 01fh, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 008h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  050h, 018h, 008h, 000h, 010h, 001h, 003h, 000h, 002h, 063h, 05fh, 04fh, 050h, 082h, 055h, 081h
    db  0bfh, 01fh, 000h, 0c7h, 006h, 007h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 028h, 01fh, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 008h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  028h, 018h, 008h, 000h, 040h, 009h, 003h, 000h, 002h, 063h, 02dh, 027h, 028h, 090h, 02bh, 080h
    db  0bfh, 01fh, 000h, 0c1h, 000h, 000h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 014h, 000h, 096h
    db  0b9h, 0a2h, 0ffh, 000h, 013h, 015h, 017h, 002h, 004h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 001h, 000h, 003h, 000h, 000h, 000h, 000h, 000h, 000h, 030h, 00fh, 00fh, 0ffh
    db  028h, 018h, 008h, 000h, 040h, 009h, 003h, 000h, 002h, 063h, 02dh, 027h, 028h, 090h, 02bh, 080h
    db  0bfh, 01fh, 000h, 0c1h, 000h, 000h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 014h, 000h, 096h
    db  0b9h, 0a2h, 0ffh, 000h, 013h, 015h, 017h, 002h, 004h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 001h, 000h, 003h, 000h, 000h, 000h, 000h, 000h, 000h, 030h, 00fh, 00fh, 0ffh
    db  050h, 018h, 008h, 000h, 040h, 001h, 001h, 000h, 006h, 063h, 05fh, 04fh, 050h, 082h, 054h, 080h
    db  0bfh, 01fh, 000h, 0c1h, 000h, 000h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 028h, 000h, 096h
    db  0b9h, 0c2h, 0ffh, 000h, 017h, 017h, 017h, 017h, 017h, 017h, 017h, 017h, 017h, 017h, 017h, 017h
    db  017h, 017h, 017h, 001h, 000h, 001h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 00dh, 00fh, 0ffh
    db  050h, 018h, 00eh, 000h, 010h, 000h, 003h, 000h, 003h, 0a6h, 05fh, 04fh, 050h, 082h, 055h, 081h
    db  0bfh, 01fh, 000h, 04dh, 00bh, 00ch, 000h, 000h, 000h, 000h, 083h, 085h, 05dh, 028h, 00dh, 063h
    db  0bah, 0a3h, 0ffh, 000h, 008h, 008h, 008h, 008h, 008h, 008h, 008h, 010h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 00eh, 000h, 00fh, 008h, 000h, 000h, 000h, 000h, 000h, 010h, 00ah, 000h, 0ffh
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  028h, 018h, 008h, 000h, 020h, 009h, 00fh, 000h, 006h, 063h, 02dh, 027h, 028h, 090h, 02bh, 080h
    db  0bfh, 01fh, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 014h, 000h, 096h
    db  0b9h, 0e3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 001h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 005h, 00fh, 0ffh
    db  050h, 018h, 008h, 000h, 040h, 001h, 00fh, 000h, 006h, 063h, 05fh, 04fh, 050h, 082h, 054h, 080h
    db  0bfh, 01fh, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 028h, 000h, 096h
    db  0b9h, 0e3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 010h, 011h, 012h, 013h, 014h
    db  015h, 016h, 017h, 001h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 005h, 00fh, 0ffh
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  050h, 018h, 00eh, 000h, 080h, 001h, 00fh, 000h, 006h, 0a3h, 05fh, 04fh, 050h, 082h, 054h, 080h
    db  0bfh, 01fh, 000h, 040h, 000h, 000h, 000h, 000h, 000h, 000h, 083h, 085h, 05dh, 028h, 00fh, 063h
    db  0bah, 0e3h, 0ffh, 000h, 008h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 008h, 000h, 000h, 000h
    db  018h, 000h, 000h, 001h, 000h, 001h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 005h, 00fh, 0ffh
    db  050h, 018h, 00eh, 000h, 080h, 001h, 00fh, 000h, 006h, 0a3h, 05fh, 04fh, 050h, 082h, 054h, 080h
    db  0bfh, 01fh, 000h, 040h, 000h, 000h, 000h, 000h, 000h, 000h, 083h, 085h, 05dh, 028h, 00fh, 063h
    db  0bah, 0e3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 001h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 005h, 00fh, 0ffh
    db  028h, 018h, 00eh, 000h, 008h, 009h, 003h, 000h, 002h, 0a3h, 02dh, 027h, 028h, 090h, 02bh, 0a0h
    db  0bfh, 01fh, 000h, 04dh, 00bh, 00ch, 000h, 000h, 000h, 000h, 083h, 085h, 05dh, 014h, 01fh, 063h
    db  0bah, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 008h, 000h, 00fh, 008h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  028h, 018h, 00eh, 000h, 008h, 009h, 003h, 000h, 002h, 0a3h, 02dh, 027h, 028h, 090h, 02bh, 0a0h
    db  0bfh, 01fh, 000h, 04dh, 00bh, 00ch, 000h, 000h, 000h, 000h, 083h, 085h, 05dh, 014h, 01fh, 063h
    db  0bah, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 008h, 000h, 00fh, 008h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  050h, 018h, 00eh, 000h, 010h, 001h, 003h, 000h, 002h, 0a3h, 05fh, 04fh, 050h, 082h, 055h, 081h
    db  0bfh, 01fh, 000h, 04dh, 00bh, 00ch, 000h, 000h, 000h, 000h, 083h, 085h, 05dh, 028h, 01fh, 063h
    db  0bah, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 008h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  050h, 018h, 00eh, 000h, 010h, 001h, 003h, 000h, 002h, 0a3h, 05fh, 04fh, 050h, 082h, 055h, 081h
    db  0bfh, 01fh, 000h, 04dh, 00bh, 00ch, 000h, 000h, 000h, 000h, 083h, 085h, 05dh, 028h, 01fh, 063h
    db  0bah, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 008h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 000h, 0ffh
    db  028h, 018h, 010h, 000h, 008h, 008h, 003h, 000h, 002h, 067h, 02dh, 027h, 028h, 090h, 02bh, 0a0h
    db  0bfh, 01fh, 000h, 04fh, 00dh, 00eh, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 014h, 01fh, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 00ch, 000h, 00fh, 008h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 00fh, 0ffh
    db  050h, 018h, 010h, 000h, 010h, 000h, 003h, 000h, 002h, 067h, 05fh, 04fh, 050h, 082h, 055h, 081h
    db  0bfh, 01fh, 000h, 04fh, 00dh, 00eh, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 028h, 01fh, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 00ch, 000h, 00fh, 008h, 000h, 000h, 000h, 000h, 000h, 010h, 00eh, 00fh, 0ffh
    db  050h, 018h, 010h, 000h, 010h, 000h, 003h, 000h, 002h, 066h, 05fh, 04fh, 050h, 082h, 055h, 081h
    db  0bfh, 01fh, 000h, 04fh, 00dh, 00eh, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 028h, 00fh, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 008h, 008h, 008h, 008h, 008h, 008h, 008h, 010h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 00eh, 000h, 00fh, 008h, 000h, 000h, 000h, 000h, 000h, 010h, 00ah, 00fh, 0ffh
    db  050h, 01dh, 010h, 000h, 0a0h, 001h, 00fh, 000h, 006h, 0e3h, 05fh, 04fh, 050h, 082h, 054h, 080h
    db  00bh, 03eh, 000h, 040h, 000h, 000h, 000h, 000h, 000h, 000h, 0eah, 08ch, 0dfh, 028h, 000h, 0e7h
    db  004h, 0c3h, 0ffh, 000h, 03fh, 000h, 03fh, 000h, 03fh, 000h, 03fh, 000h, 03fh, 000h, 03fh, 000h
    db  03fh, 000h, 03fh, 001h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 005h, 00fh, 0ffh
    db  050h, 01dh, 010h, 000h, 0a0h, 001h, 00fh, 000h, 006h, 0e3h, 05fh, 04fh, 050h, 082h, 054h, 080h
    db  00bh, 03eh, 000h, 040h, 000h, 000h, 000h, 000h, 000h, 000h, 0eah, 08ch, 0dfh, 028h, 000h, 0e7h
    db  004h, 0e3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 001h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 005h, 00fh, 0ffh
    db  028h, 018h, 008h, 000h, 020h, 001h, 00fh, 000h, 00eh, 063h, 05fh, 04fh, 050h, 082h, 054h, 080h
    db  0bfh, 01fh, 000h, 041h, 000h, 000h, 000h, 000h, 000h, 000h, 09ch, 08eh, 08fh, 028h, 040h, 096h
    db  0b9h, 0a3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 008h, 009h, 00ah, 00bh, 00ch
    db  00dh, 00eh, 00fh, 041h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 040h, 005h, 00fh, 0ffh
    db  064h, 024h, 010h, 000h, 000h, 001h, 00fh, 000h, 006h, 0e3h, 07fh, 063h, 063h, 083h, 06bh, 01bh
    db  072h, 0f0h, 000h, 060h, 000h, 000h, 000h, 000h, 000h, 000h, 059h, 08dh, 057h, 032h, 000h, 057h
    db  073h, 0e3h, 0ffh, 000h, 001h, 002h, 003h, 004h, 005h, 014h, 007h, 038h, 039h, 03ah, 03bh, 03ch
    db  03dh, 03eh, 03fh, 001h, 000h, 00fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 005h, 00fh, 0ffh
  ; disGetNextSymbol 0xc4fc0 LB 0x2f05 -> off=0x0 cb=00000000000000c0 uValue=00000000000c0840 'palette0'
palette0:                                    ; 0xc4fc0 LB 0xc0
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah
    db  02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah
    db  02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah
    db  02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh
    db  03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah
    db  02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah
    db  02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah
    db  02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 02ah, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh
    db  03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh, 03fh
  ; disGetNextSymbol 0xc5080 LB 0x2e45 -> off=0x0 cb=00000000000000c0 uValue=00000000000c0900 'palette1'
palette1:                                    ; 0xc5080 LB 0xc0
    db  000h, 000h, 000h, 000h, 000h, 02ah, 000h, 02ah, 000h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 02ah
    db  000h, 02ah, 02ah, 015h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 000h, 000h, 000h, 02ah, 000h, 02ah
    db  000h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 02ah, 000h, 02ah, 02ah, 015h, 000h, 02ah, 02ah, 02ah
    db  015h, 015h, 015h, 015h, 015h, 03fh, 015h, 03fh, 015h, 015h, 03fh, 03fh, 03fh, 015h, 015h, 03fh
    db  015h, 03fh, 03fh, 03fh, 015h, 03fh, 03fh, 03fh, 015h, 015h, 015h, 015h, 015h, 03fh, 015h, 03fh
    db  015h, 015h, 03fh, 03fh, 03fh, 015h, 015h, 03fh, 015h, 03fh, 03fh, 03fh, 015h, 03fh, 03fh, 03fh
    db  000h, 000h, 000h, 000h, 000h, 02ah, 000h, 02ah, 000h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 02ah
    db  000h, 02ah, 02ah, 015h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 000h, 000h, 000h, 02ah, 000h, 02ah
    db  000h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 02ah, 000h, 02ah, 02ah, 015h, 000h, 02ah, 02ah, 02ah
    db  015h, 015h, 015h, 015h, 015h, 03fh, 015h, 03fh, 015h, 015h, 03fh, 03fh, 03fh, 015h, 015h, 03fh
    db  015h, 03fh, 03fh, 03fh, 015h, 03fh, 03fh, 03fh, 015h, 015h, 015h, 015h, 015h, 03fh, 015h, 03fh
    db  015h, 015h, 03fh, 03fh, 03fh, 015h, 015h, 03fh, 015h, 03fh, 03fh, 03fh, 015h, 03fh, 03fh, 03fh
  ; disGetNextSymbol 0xc5140 LB 0x2d85 -> off=0x0 cb=00000000000000c0 uValue=00000000000c09c0 'palette2'
palette2:                                    ; 0xc5140 LB 0xc0
    db  000h, 000h, 000h, 000h, 000h, 02ah, 000h, 02ah, 000h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 02ah
    db  000h, 02ah, 02ah, 02ah, 000h, 02ah, 02ah, 02ah, 000h, 000h, 015h, 000h, 000h, 03fh, 000h, 02ah
    db  015h, 000h, 02ah, 03fh, 02ah, 000h, 015h, 02ah, 000h, 03fh, 02ah, 02ah, 015h, 02ah, 02ah, 03fh
    db  000h, 015h, 000h, 000h, 015h, 02ah, 000h, 03fh, 000h, 000h, 03fh, 02ah, 02ah, 015h, 000h, 02ah
    db  015h, 02ah, 02ah, 03fh, 000h, 02ah, 03fh, 02ah, 000h, 015h, 015h, 000h, 015h, 03fh, 000h, 03fh
    db  015h, 000h, 03fh, 03fh, 02ah, 015h, 015h, 02ah, 015h, 03fh, 02ah, 03fh, 015h, 02ah, 03fh, 03fh
    db  015h, 000h, 000h, 015h, 000h, 02ah, 015h, 02ah, 000h, 015h, 02ah, 02ah, 03fh, 000h, 000h, 03fh
    db  000h, 02ah, 03fh, 02ah, 000h, 03fh, 02ah, 02ah, 015h, 000h, 015h, 015h, 000h, 03fh, 015h, 02ah
    db  015h, 015h, 02ah, 03fh, 03fh, 000h, 015h, 03fh, 000h, 03fh, 03fh, 02ah, 015h, 03fh, 02ah, 03fh
    db  015h, 015h, 000h, 015h, 015h, 02ah, 015h, 03fh, 000h, 015h, 03fh, 02ah, 03fh, 015h, 000h, 03fh
    db  015h, 02ah, 03fh, 03fh, 000h, 03fh, 03fh, 02ah, 015h, 015h, 015h, 015h, 015h, 03fh, 015h, 03fh
    db  015h, 015h, 03fh, 03fh, 03fh, 015h, 015h, 03fh, 015h, 03fh, 03fh, 03fh, 015h, 03fh, 03fh, 03fh
  ; disGetNextSymbol 0xc5200 LB 0x2cc5 -> off=0x0 cb=0000000000000300 uValue=00000000000c0a80 'palette3'
palette3:                                    ; 0xc5200 LB 0x300
    db  000h, 000h, 000h, 000h, 000h, 02ah, 000h, 02ah, 000h, 000h, 02ah, 02ah, 02ah, 000h, 000h, 02ah
    db  000h, 02ah, 02ah, 015h, 000h, 02ah, 02ah, 02ah, 015h, 015h, 015h, 015h, 015h, 03fh, 015h, 03fh
    db  015h, 015h, 03fh, 03fh, 03fh, 015h, 015h, 03fh, 015h, 03fh, 03fh, 03fh, 015h, 03fh, 03fh, 03fh
    db  000h, 000h, 000h, 005h, 005h, 005h, 008h, 008h, 008h, 00bh, 00bh, 00bh, 00eh, 00eh, 00eh, 011h
    db  011h, 011h, 014h, 014h, 014h, 018h, 018h, 018h, 01ch, 01ch, 01ch, 020h, 020h, 020h, 024h, 024h
    db  024h, 028h, 028h, 028h, 02dh, 02dh, 02dh, 032h, 032h, 032h, 038h, 038h, 038h, 03fh, 03fh, 03fh
    db  000h, 000h, 03fh, 010h, 000h, 03fh, 01fh, 000h, 03fh, 02fh, 000h, 03fh, 03fh, 000h, 03fh, 03fh
    db  000h, 02fh, 03fh, 000h, 01fh, 03fh, 000h, 010h, 03fh, 000h, 000h, 03fh, 010h, 000h, 03fh, 01fh
    db  000h, 03fh, 02fh, 000h, 03fh, 03fh, 000h, 02fh, 03fh, 000h, 01fh, 03fh, 000h, 010h, 03fh, 000h
    db  000h, 03fh, 000h, 000h, 03fh, 010h, 000h, 03fh, 01fh, 000h, 03fh, 02fh, 000h, 03fh, 03fh, 000h
    db  02fh, 03fh, 000h, 01fh, 03fh, 000h, 010h, 03fh, 01fh, 01fh, 03fh, 027h, 01fh, 03fh, 02fh, 01fh
    db  03fh, 037h, 01fh, 03fh, 03fh, 01fh, 03fh, 03fh, 01fh, 037h, 03fh, 01fh, 02fh, 03fh, 01fh, 027h
    db  03fh, 01fh, 01fh, 03fh, 027h, 01fh, 03fh, 02fh, 01fh, 03fh, 037h, 01fh, 03fh, 03fh, 01fh, 037h
    db  03fh, 01fh, 02fh, 03fh, 01fh, 027h, 03fh, 01fh, 01fh, 03fh, 01fh, 01fh, 03fh, 027h, 01fh, 03fh
    db  02fh, 01fh, 03fh, 037h, 01fh, 03fh, 03fh, 01fh, 037h, 03fh, 01fh, 02fh, 03fh, 01fh, 027h, 03fh
    db  02dh, 02dh, 03fh, 031h, 02dh, 03fh, 036h, 02dh, 03fh, 03ah, 02dh, 03fh, 03fh, 02dh, 03fh, 03fh
    db  02dh, 03ah, 03fh, 02dh, 036h, 03fh, 02dh, 031h, 03fh, 02dh, 02dh, 03fh, 031h, 02dh, 03fh, 036h
    db  02dh, 03fh, 03ah, 02dh, 03fh, 03fh, 02dh, 03ah, 03fh, 02dh, 036h, 03fh, 02dh, 031h, 03fh, 02dh
    db  02dh, 03fh, 02dh, 02dh, 03fh, 031h, 02dh, 03fh, 036h, 02dh, 03fh, 03ah, 02dh, 03fh, 03fh, 02dh
    db  03ah, 03fh, 02dh, 036h, 03fh, 02dh, 031h, 03fh, 000h, 000h, 01ch, 007h, 000h, 01ch, 00eh, 000h
    db  01ch, 015h, 000h, 01ch, 01ch, 000h, 01ch, 01ch, 000h, 015h, 01ch, 000h, 00eh, 01ch, 000h, 007h
    db  01ch, 000h, 000h, 01ch, 007h, 000h, 01ch, 00eh, 000h, 01ch, 015h, 000h, 01ch, 01ch, 000h, 015h
    db  01ch, 000h, 00eh, 01ch, 000h, 007h, 01ch, 000h, 000h, 01ch, 000h, 000h, 01ch, 007h, 000h, 01ch
    db  00eh, 000h, 01ch, 015h, 000h, 01ch, 01ch, 000h, 015h, 01ch, 000h, 00eh, 01ch, 000h, 007h, 01ch
    db  00eh, 00eh, 01ch, 011h, 00eh, 01ch, 015h, 00eh, 01ch, 018h, 00eh, 01ch, 01ch, 00eh, 01ch, 01ch
    db  00eh, 018h, 01ch, 00eh, 015h, 01ch, 00eh, 011h, 01ch, 00eh, 00eh, 01ch, 011h, 00eh, 01ch, 015h
    db  00eh, 01ch, 018h, 00eh, 01ch, 01ch, 00eh, 018h, 01ch, 00eh, 015h, 01ch, 00eh, 011h, 01ch, 00eh
    db  00eh, 01ch, 00eh, 00eh, 01ch, 011h, 00eh, 01ch, 015h, 00eh, 01ch, 018h, 00eh, 01ch, 01ch, 00eh
    db  018h, 01ch, 00eh, 015h, 01ch, 00eh, 011h, 01ch, 014h, 014h, 01ch, 016h, 014h, 01ch, 018h, 014h
    db  01ch, 01ah, 014h, 01ch, 01ch, 014h, 01ch, 01ch, 014h, 01ah, 01ch, 014h, 018h, 01ch, 014h, 016h
    db  01ch, 014h, 014h, 01ch, 016h, 014h, 01ch, 018h, 014h, 01ch, 01ah, 014h, 01ch, 01ch, 014h, 01ah
    db  01ch, 014h, 018h, 01ch, 014h, 016h, 01ch, 014h, 014h, 01ch, 014h, 014h, 01ch, 016h, 014h, 01ch
    db  018h, 014h, 01ch, 01ah, 014h, 01ch, 01ch, 014h, 01ah, 01ch, 014h, 018h, 01ch, 014h, 016h, 01ch
    db  000h, 000h, 010h, 004h, 000h, 010h, 008h, 000h, 010h, 00ch, 000h, 010h, 010h, 000h, 010h, 010h
    db  000h, 00ch, 010h, 000h, 008h, 010h, 000h, 004h, 010h, 000h, 000h, 010h, 004h, 000h, 010h, 008h
    db  000h, 010h, 00ch, 000h, 010h, 010h, 000h, 00ch, 010h, 000h, 008h, 010h, 000h, 004h, 010h, 000h
    db  000h, 010h, 000h, 000h, 010h, 004h, 000h, 010h, 008h, 000h, 010h, 00ch, 000h, 010h, 010h, 000h
    db  00ch, 010h, 000h, 008h, 010h, 000h, 004h, 010h, 008h, 008h, 010h, 00ah, 008h, 010h, 00ch, 008h
    db  010h, 00eh, 008h, 010h, 010h, 008h, 010h, 010h, 008h, 00eh, 010h, 008h, 00ch, 010h, 008h, 00ah
    db  010h, 008h, 008h, 010h, 00ah, 008h, 010h, 00ch, 008h, 010h, 00eh, 008h, 010h, 010h, 008h, 00eh
    db  010h, 008h, 00ch, 010h, 008h, 00ah, 010h, 008h, 008h, 010h, 008h, 008h, 010h, 00ah, 008h, 010h
    db  00ch, 008h, 010h, 00eh, 008h, 010h, 010h, 008h, 00eh, 010h, 008h, 00ch, 010h, 008h, 00ah, 010h
    db  00bh, 00bh, 010h, 00ch, 00bh, 010h, 00dh, 00bh, 010h, 00fh, 00bh, 010h, 010h, 00bh, 010h, 010h
    db  00bh, 00fh, 010h, 00bh, 00dh, 010h, 00bh, 00ch, 010h, 00bh, 00bh, 010h, 00ch, 00bh, 010h, 00dh
    db  00bh, 010h, 00fh, 00bh, 010h, 010h, 00bh, 00fh, 010h, 00bh, 00dh, 010h, 00bh, 00ch, 010h, 00bh
    db  00bh, 010h, 00bh, 00bh, 010h, 00ch, 00bh, 010h, 00dh, 00bh, 010h, 00fh, 00bh, 010h, 010h, 00bh
    db  00fh, 010h, 00bh, 00dh, 010h, 00bh, 00ch, 010h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc5500 LB 0x29c5 -> off=0x0 cb=0000000000000010 uValue=00000000000c0d80 'static_functionality'
static_functionality:                        ; 0xc5500 LB 0x10
    db  0ffh, 0e0h, 00fh, 000h, 000h, 000h, 000h, 007h, 002h, 008h, 0e7h, 00ch, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc5510 LB 0x29b5 -> off=0x0 cb=0000000000000024 uValue=00000000000c0d90 '_dcc_table'
_dcc_table:                                  ; 0xc5510 LB 0x24
    db  010h, 001h, 007h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc5534 LB 0x2991 -> off=0x0 cb=000000000000001a uValue=00000000000c0db4 '_secondary_save_area'
_secondary_save_area:                        ; 0xc5534 LB 0x1a
    db  01ah, 000h, 010h, 055h, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc554e LB 0x2977 -> off=0x0 cb=000000000000001c uValue=00000000000c0dce '_video_save_pointer_table'
_video_save_pointer_table:                   ; 0xc554e LB 0x1c
    db  040h, 048h, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  034h, 055h, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc556a LB 0x295b -> off=0x0 cb=0000000000000800 uValue=00000000000c0dea 'vgafont8'
vgafont8:                                    ; 0xc556a LB 0x800
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07eh, 081h, 0a5h, 081h, 0bdh, 099h, 081h, 07eh
    db  07eh, 0ffh, 0dbh, 0ffh, 0c3h, 0e7h, 0ffh, 07eh, 06ch, 0feh, 0feh, 0feh, 07ch, 038h, 010h, 000h
    db  010h, 038h, 07ch, 0feh, 07ch, 038h, 010h, 000h, 038h, 07ch, 038h, 0feh, 0feh, 07ch, 038h, 07ch
    db  010h, 010h, 038h, 07ch, 0feh, 07ch, 038h, 07ch, 000h, 000h, 018h, 03ch, 03ch, 018h, 000h, 000h
    db  0ffh, 0ffh, 0e7h, 0c3h, 0c3h, 0e7h, 0ffh, 0ffh, 000h, 03ch, 066h, 042h, 042h, 066h, 03ch, 000h
    db  0ffh, 0c3h, 099h, 0bdh, 0bdh, 099h, 0c3h, 0ffh, 00fh, 007h, 00fh, 07dh, 0cch, 0cch, 0cch, 078h
    db  03ch, 066h, 066h, 066h, 03ch, 018h, 07eh, 018h, 03fh, 033h, 03fh, 030h, 030h, 070h, 0f0h, 0e0h
    db  07fh, 063h, 07fh, 063h, 063h, 067h, 0e6h, 0c0h, 099h, 05ah, 03ch, 0e7h, 0e7h, 03ch, 05ah, 099h
    db  080h, 0e0h, 0f8h, 0feh, 0f8h, 0e0h, 080h, 000h, 002h, 00eh, 03eh, 0feh, 03eh, 00eh, 002h, 000h
    db  018h, 03ch, 07eh, 018h, 018h, 07eh, 03ch, 018h, 066h, 066h, 066h, 066h, 066h, 000h, 066h, 000h
    db  07fh, 0dbh, 0dbh, 07bh, 01bh, 01bh, 01bh, 000h, 03eh, 063h, 038h, 06ch, 06ch, 038h, 0cch, 078h
    db  000h, 000h, 000h, 000h, 07eh, 07eh, 07eh, 000h, 018h, 03ch, 07eh, 018h, 07eh, 03ch, 018h, 0ffh
    db  018h, 03ch, 07eh, 018h, 018h, 018h, 018h, 000h, 018h, 018h, 018h, 018h, 07eh, 03ch, 018h, 000h
    db  000h, 018h, 00ch, 0feh, 00ch, 018h, 000h, 000h, 000h, 030h, 060h, 0feh, 060h, 030h, 000h, 000h
    db  000h, 000h, 0c0h, 0c0h, 0c0h, 0feh, 000h, 000h, 000h, 024h, 066h, 0ffh, 066h, 024h, 000h, 000h
    db  000h, 018h, 03ch, 07eh, 0ffh, 0ffh, 000h, 000h, 000h, 0ffh, 0ffh, 07eh, 03ch, 018h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 030h, 078h, 078h, 030h, 030h, 000h, 030h, 000h
    db  06ch, 06ch, 06ch, 000h, 000h, 000h, 000h, 000h, 06ch, 06ch, 0feh, 06ch, 0feh, 06ch, 06ch, 000h
    db  030h, 07ch, 0c0h, 078h, 00ch, 0f8h, 030h, 000h, 000h, 0c6h, 0cch, 018h, 030h, 066h, 0c6h, 000h
    db  038h, 06ch, 038h, 076h, 0dch, 0cch, 076h, 000h, 060h, 060h, 0c0h, 000h, 000h, 000h, 000h, 000h
    db  018h, 030h, 060h, 060h, 060h, 030h, 018h, 000h, 060h, 030h, 018h, 018h, 018h, 030h, 060h, 000h
    db  000h, 066h, 03ch, 0ffh, 03ch, 066h, 000h, 000h, 000h, 030h, 030h, 0fch, 030h, 030h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 030h, 030h, 060h, 000h, 000h, 000h, 0fch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 030h, 030h, 000h, 006h, 00ch, 018h, 030h, 060h, 0c0h, 080h, 000h
    db  07ch, 0c6h, 0ceh, 0deh, 0f6h, 0e6h, 07ch, 000h, 030h, 070h, 030h, 030h, 030h, 030h, 0fch, 000h
    db  078h, 0cch, 00ch, 038h, 060h, 0cch, 0fch, 000h, 078h, 0cch, 00ch, 038h, 00ch, 0cch, 078h, 000h
    db  01ch, 03ch, 06ch, 0cch, 0feh, 00ch, 01eh, 000h, 0fch, 0c0h, 0f8h, 00ch, 00ch, 0cch, 078h, 000h
    db  038h, 060h, 0c0h, 0f8h, 0cch, 0cch, 078h, 000h, 0fch, 0cch, 00ch, 018h, 030h, 030h, 030h, 000h
    db  078h, 0cch, 0cch, 078h, 0cch, 0cch, 078h, 000h, 078h, 0cch, 0cch, 07ch, 00ch, 018h, 070h, 000h
    db  000h, 030h, 030h, 000h, 000h, 030h, 030h, 000h, 000h, 030h, 030h, 000h, 000h, 030h, 030h, 060h
    db  018h, 030h, 060h, 0c0h, 060h, 030h, 018h, 000h, 000h, 000h, 0fch, 000h, 000h, 0fch, 000h, 000h
    db  060h, 030h, 018h, 00ch, 018h, 030h, 060h, 000h, 078h, 0cch, 00ch, 018h, 030h, 000h, 030h, 000h
    db  07ch, 0c6h, 0deh, 0deh, 0deh, 0c0h, 078h, 000h, 030h, 078h, 0cch, 0cch, 0fch, 0cch, 0cch, 000h
    db  0fch, 066h, 066h, 07ch, 066h, 066h, 0fch, 000h, 03ch, 066h, 0c0h, 0c0h, 0c0h, 066h, 03ch, 000h
    db  0f8h, 06ch, 066h, 066h, 066h, 06ch, 0f8h, 000h, 0feh, 062h, 068h, 078h, 068h, 062h, 0feh, 000h
    db  0feh, 062h, 068h, 078h, 068h, 060h, 0f0h, 000h, 03ch, 066h, 0c0h, 0c0h, 0ceh, 066h, 03eh, 000h
    db  0cch, 0cch, 0cch, 0fch, 0cch, 0cch, 0cch, 000h, 078h, 030h, 030h, 030h, 030h, 030h, 078h, 000h
    db  01eh, 00ch, 00ch, 00ch, 0cch, 0cch, 078h, 000h, 0e6h, 066h, 06ch, 078h, 06ch, 066h, 0e6h, 000h
    db  0f0h, 060h, 060h, 060h, 062h, 066h, 0feh, 000h, 0c6h, 0eeh, 0feh, 0feh, 0d6h, 0c6h, 0c6h, 000h
    db  0c6h, 0e6h, 0f6h, 0deh, 0ceh, 0c6h, 0c6h, 000h, 038h, 06ch, 0c6h, 0c6h, 0c6h, 06ch, 038h, 000h
    db  0fch, 066h, 066h, 07ch, 060h, 060h, 0f0h, 000h, 078h, 0cch, 0cch, 0cch, 0dch, 078h, 01ch, 000h
    db  0fch, 066h, 066h, 07ch, 06ch, 066h, 0e6h, 000h, 078h, 0cch, 0e0h, 070h, 01ch, 0cch, 078h, 000h
    db  0fch, 0b4h, 030h, 030h, 030h, 030h, 078h, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 0cch, 0fch, 000h
    db  0cch, 0cch, 0cch, 0cch, 0cch, 078h, 030h, 000h, 0c6h, 0c6h, 0c6h, 0d6h, 0feh, 0eeh, 0c6h, 000h
    db  0c6h, 0c6h, 06ch, 038h, 038h, 06ch, 0c6h, 000h, 0cch, 0cch, 0cch, 078h, 030h, 030h, 078h, 000h
    db  0feh, 0c6h, 08ch, 018h, 032h, 066h, 0feh, 000h, 078h, 060h, 060h, 060h, 060h, 060h, 078h, 000h
    db  0c0h, 060h, 030h, 018h, 00ch, 006h, 002h, 000h, 078h, 018h, 018h, 018h, 018h, 018h, 078h, 000h
    db  010h, 038h, 06ch, 0c6h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh
    db  030h, 030h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 078h, 00ch, 07ch, 0cch, 076h, 000h
    db  0e0h, 060h, 060h, 07ch, 066h, 066h, 0dch, 000h, 000h, 000h, 078h, 0cch, 0c0h, 0cch, 078h, 000h
    db  01ch, 00ch, 00ch, 07ch, 0cch, 0cch, 076h, 000h, 000h, 000h, 078h, 0cch, 0fch, 0c0h, 078h, 000h
    db  038h, 06ch, 060h, 0f0h, 060h, 060h, 0f0h, 000h, 000h, 000h, 076h, 0cch, 0cch, 07ch, 00ch, 0f8h
    db  0e0h, 060h, 06ch, 076h, 066h, 066h, 0e6h, 000h, 030h, 000h, 070h, 030h, 030h, 030h, 078h, 000h
    db  00ch, 000h, 00ch, 00ch, 00ch, 0cch, 0cch, 078h, 0e0h, 060h, 066h, 06ch, 078h, 06ch, 0e6h, 000h
    db  070h, 030h, 030h, 030h, 030h, 030h, 078h, 000h, 000h, 000h, 0cch, 0feh, 0feh, 0d6h, 0c6h, 000h
    db  000h, 000h, 0f8h, 0cch, 0cch, 0cch, 0cch, 000h, 000h, 000h, 078h, 0cch, 0cch, 0cch, 078h, 000h
    db  000h, 000h, 0dch, 066h, 066h, 07ch, 060h, 0f0h, 000h, 000h, 076h, 0cch, 0cch, 07ch, 00ch, 01eh
    db  000h, 000h, 0dch, 076h, 066h, 060h, 0f0h, 000h, 000h, 000h, 07ch, 0c0h, 078h, 00ch, 0f8h, 000h
    db  010h, 030h, 07ch, 030h, 030h, 034h, 018h, 000h, 000h, 000h, 0cch, 0cch, 0cch, 0cch, 076h, 000h
    db  000h, 000h, 0cch, 0cch, 0cch, 078h, 030h, 000h, 000h, 000h, 0c6h, 0d6h, 0feh, 0feh, 06ch, 000h
    db  000h, 000h, 0c6h, 06ch, 038h, 06ch, 0c6h, 000h, 000h, 000h, 0cch, 0cch, 0cch, 07ch, 00ch, 0f8h
    db  000h, 000h, 0fch, 098h, 030h, 064h, 0fch, 000h, 01ch, 030h, 030h, 0e0h, 030h, 030h, 01ch, 000h
    db  018h, 018h, 018h, 000h, 018h, 018h, 018h, 000h, 0e0h, 030h, 030h, 01ch, 030h, 030h, 0e0h, 000h
    db  076h, 0dch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 038h, 06ch, 0c6h, 0c6h, 0feh, 000h
    db  078h, 0cch, 0c0h, 0cch, 078h, 018h, 00ch, 078h, 000h, 0cch, 000h, 0cch, 0cch, 0cch, 07eh, 000h
    db  01ch, 000h, 078h, 0cch, 0fch, 0c0h, 078h, 000h, 07eh, 0c3h, 03ch, 006h, 03eh, 066h, 03fh, 000h
    db  0cch, 000h, 078h, 00ch, 07ch, 0cch, 07eh, 000h, 0e0h, 000h, 078h, 00ch, 07ch, 0cch, 07eh, 000h
    db  030h, 030h, 078h, 00ch, 07ch, 0cch, 07eh, 000h, 000h, 000h, 078h, 0c0h, 0c0h, 078h, 00ch, 038h
    db  07eh, 0c3h, 03ch, 066h, 07eh, 060h, 03ch, 000h, 0cch, 000h, 078h, 0cch, 0fch, 0c0h, 078h, 000h
    db  0e0h, 000h, 078h, 0cch, 0fch, 0c0h, 078h, 000h, 0cch, 000h, 070h, 030h, 030h, 030h, 078h, 000h
    db  07ch, 0c6h, 038h, 018h, 018h, 018h, 03ch, 000h, 0e0h, 000h, 070h, 030h, 030h, 030h, 078h, 000h
    db  0c6h, 038h, 06ch, 0c6h, 0feh, 0c6h, 0c6h, 000h, 030h, 030h, 000h, 078h, 0cch, 0fch, 0cch, 000h
    db  01ch, 000h, 0fch, 060h, 078h, 060h, 0fch, 000h, 000h, 000h, 07fh, 00ch, 07fh, 0cch, 07fh, 000h
    db  03eh, 06ch, 0cch, 0feh, 0cch, 0cch, 0ceh, 000h, 078h, 0cch, 000h, 078h, 0cch, 0cch, 078h, 000h
    db  000h, 0cch, 000h, 078h, 0cch, 0cch, 078h, 000h, 000h, 0e0h, 000h, 078h, 0cch, 0cch, 078h, 000h
    db  078h, 0cch, 000h, 0cch, 0cch, 0cch, 07eh, 000h, 000h, 0e0h, 000h, 0cch, 0cch, 0cch, 07eh, 000h
    db  000h, 0cch, 000h, 0cch, 0cch, 07ch, 00ch, 0f8h, 0c3h, 018h, 03ch, 066h, 066h, 03ch, 018h, 000h
    db  0cch, 000h, 0cch, 0cch, 0cch, 0cch, 078h, 000h, 018h, 018h, 07eh, 0c0h, 0c0h, 07eh, 018h, 018h
    db  038h, 06ch, 064h, 0f0h, 060h, 0e6h, 0fch, 000h, 0cch, 0cch, 078h, 0fch, 030h, 0fch, 030h, 030h
    db  0f8h, 0cch, 0cch, 0fah, 0c6h, 0cfh, 0c6h, 0c7h, 00eh, 01bh, 018h, 03ch, 018h, 018h, 0d8h, 070h
    db  01ch, 000h, 078h, 00ch, 07ch, 0cch, 07eh, 000h, 038h, 000h, 070h, 030h, 030h, 030h, 078h, 000h
    db  000h, 01ch, 000h, 078h, 0cch, 0cch, 078h, 000h, 000h, 01ch, 000h, 0cch, 0cch, 0cch, 07eh, 000h
    db  000h, 0f8h, 000h, 0f8h, 0cch, 0cch, 0cch, 000h, 0fch, 000h, 0cch, 0ech, 0fch, 0dch, 0cch, 000h
    db  03ch, 06ch, 06ch, 03eh, 000h, 07eh, 000h, 000h, 038h, 06ch, 06ch, 038h, 000h, 07ch, 000h, 000h
    db  030h, 000h, 030h, 060h, 0c0h, 0cch, 078h, 000h, 000h, 000h, 000h, 0fch, 0c0h, 0c0h, 000h, 000h
    db  000h, 000h, 000h, 0fch, 00ch, 00ch, 000h, 000h, 0c3h, 0c6h, 0cch, 0deh, 033h, 066h, 0cch, 00fh
    db  0c3h, 0c6h, 0cch, 0dbh, 037h, 06fh, 0cfh, 003h, 018h, 018h, 000h, 018h, 018h, 018h, 018h, 000h
    db  000h, 033h, 066h, 0cch, 066h, 033h, 000h, 000h, 000h, 0cch, 066h, 033h, 066h, 0cch, 000h, 000h
    db  022h, 088h, 022h, 088h, 022h, 088h, 022h, 088h, 055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah
    db  0dbh, 077h, 0dbh, 0eeh, 0dbh, 077h, 0dbh, 0eeh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 0f8h, 018h, 018h, 018h, 018h, 018h, 0f8h, 018h, 0f8h, 018h, 018h, 018h
    db  036h, 036h, 036h, 036h, 0f6h, 036h, 036h, 036h, 000h, 000h, 000h, 000h, 0feh, 036h, 036h, 036h
    db  000h, 000h, 0f8h, 018h, 0f8h, 018h, 018h, 018h, 036h, 036h, 0f6h, 006h, 0f6h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 000h, 000h, 0feh, 006h, 0f6h, 036h, 036h, 036h
    db  036h, 036h, 0f6h, 006h, 0feh, 000h, 000h, 000h, 036h, 036h, 036h, 036h, 0feh, 000h, 000h, 000h
    db  018h, 018h, 0f8h, 018h, 0f8h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0f8h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 01fh, 000h, 000h, 000h, 018h, 018h, 018h, 018h, 0ffh, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 01fh, 018h, 018h, 018h
    db  000h, 000h, 000h, 000h, 0ffh, 000h, 000h, 000h, 018h, 018h, 018h, 018h, 0ffh, 018h, 018h, 018h
    db  018h, 018h, 01fh, 018h, 01fh, 018h, 018h, 018h, 036h, 036h, 036h, 036h, 037h, 036h, 036h, 036h
    db  036h, 036h, 037h, 030h, 03fh, 000h, 000h, 000h, 000h, 000h, 03fh, 030h, 037h, 036h, 036h, 036h
    db  036h, 036h, 0f7h, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 0f7h, 036h, 036h, 036h
    db  036h, 036h, 037h, 030h, 037h, 036h, 036h, 036h, 000h, 000h, 0ffh, 000h, 0ffh, 000h, 000h, 000h
    db  036h, 036h, 0f7h, 000h, 0f7h, 036h, 036h, 036h, 018h, 018h, 0ffh, 000h, 0ffh, 000h, 000h, 000h
    db  036h, 036h, 036h, 036h, 0ffh, 000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 0ffh, 018h, 018h, 018h
    db  000h, 000h, 000h, 000h, 0ffh, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 03fh, 000h, 000h, 000h
    db  018h, 018h, 01fh, 018h, 01fh, 000h, 000h, 000h, 000h, 000h, 01fh, 018h, 01fh, 018h, 018h, 018h
    db  000h, 000h, 000h, 000h, 03fh, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 0ffh, 036h, 036h, 036h
    db  018h, 018h, 0ffh, 018h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 0f8h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 01fh, 018h, 018h, 018h, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh
    db  000h, 000h, 000h, 000h, 0ffh, 0ffh, 0ffh, 0ffh, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h
    db  00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 0ffh, 0ffh, 0ffh, 0ffh, 000h, 000h, 000h, 000h
    db  000h, 000h, 076h, 0dch, 0c8h, 0dch, 076h, 000h, 000h, 078h, 0cch, 0f8h, 0cch, 0f8h, 0c0h, 0c0h
    db  000h, 0fch, 0cch, 0c0h, 0c0h, 0c0h, 0c0h, 000h, 000h, 0feh, 06ch, 06ch, 06ch, 06ch, 06ch, 000h
    db  0fch, 0cch, 060h, 030h, 060h, 0cch, 0fch, 000h, 000h, 000h, 07eh, 0d8h, 0d8h, 0d8h, 070h, 000h
    db  000h, 066h, 066h, 066h, 066h, 07ch, 060h, 0c0h, 000h, 076h, 0dch, 018h, 018h, 018h, 018h, 000h
    db  0fch, 030h, 078h, 0cch, 0cch, 078h, 030h, 0fch, 038h, 06ch, 0c6h, 0feh, 0c6h, 06ch, 038h, 000h
    db  038h, 06ch, 0c6h, 0c6h, 06ch, 06ch, 0eeh, 000h, 01ch, 030h, 018h, 07ch, 0cch, 0cch, 078h, 000h
    db  000h, 000h, 07eh, 0dbh, 0dbh, 07eh, 000h, 000h, 006h, 00ch, 07eh, 0dbh, 0dbh, 07eh, 060h, 0c0h
    db  038h, 060h, 0c0h, 0f8h, 0c0h, 060h, 038h, 000h, 078h, 0cch, 0cch, 0cch, 0cch, 0cch, 0cch, 000h
    db  000h, 0fch, 000h, 0fch, 000h, 0fch, 000h, 000h, 030h, 030h, 0fch, 030h, 030h, 000h, 0fch, 000h
    db  060h, 030h, 018h, 030h, 060h, 000h, 0fch, 000h, 018h, 030h, 060h, 030h, 018h, 000h, 0fch, 000h
    db  00eh, 01bh, 01bh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 0d8h, 0d8h, 070h
    db  030h, 030h, 000h, 0fch, 000h, 030h, 030h, 000h, 000h, 076h, 0dch, 000h, 076h, 0dch, 000h, 000h
    db  038h, 06ch, 06ch, 038h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 018h, 000h, 000h, 000h, 00fh, 00ch, 00ch, 00ch, 0ech, 06ch, 03ch, 01ch
    db  078h, 06ch, 06ch, 06ch, 06ch, 000h, 000h, 000h, 070h, 018h, 030h, 060h, 078h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 03ch, 03ch, 03ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc5d6a LB 0x215b -> off=0x0 cb=0000000000000e00 uValue=00000000000c15ea 'vgafont14'
vgafont14:                                   ; 0xc5d6a LB 0xe00
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  07eh, 081h, 0a5h, 081h, 081h, 0bdh, 099h, 081h, 07eh, 000h, 000h, 000h, 000h, 000h, 07eh, 0ffh
    db  0dbh, 0ffh, 0ffh, 0c3h, 0e7h, 0ffh, 07eh, 000h, 000h, 000h, 000h, 000h, 000h, 06ch, 0feh, 0feh
    db  0feh, 0feh, 07ch, 038h, 010h, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 038h, 07ch, 0feh, 07ch
    db  038h, 010h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 03ch, 03ch, 0e7h, 0e7h, 0e7h, 018h, 018h
    db  03ch, 000h, 000h, 000h, 000h, 000h, 018h, 03ch, 07eh, 0ffh, 0ffh, 07eh, 018h, 018h, 03ch, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 03ch, 03ch, 018h, 000h, 000h, 000h, 000h, 000h
    db  0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0e7h, 0c3h, 0c3h, 0e7h, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 000h, 000h
    db  000h, 000h, 03ch, 066h, 042h, 042h, 066h, 03ch, 000h, 000h, 000h, 000h, 0ffh, 0ffh, 0ffh, 0ffh
    db  0c3h, 099h, 0bdh, 0bdh, 099h, 0c3h, 0ffh, 0ffh, 0ffh, 0ffh, 000h, 000h, 01eh, 00eh, 01ah, 032h
    db  078h, 0cch, 0cch, 0cch, 078h, 000h, 000h, 000h, 000h, 000h, 03ch, 066h, 066h, 066h, 03ch, 018h
    db  07eh, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 03fh, 033h, 03fh, 030h, 030h, 030h, 070h, 0f0h
    db  0e0h, 000h, 000h, 000h, 000h, 000h, 07fh, 063h, 07fh, 063h, 063h, 063h, 067h, 0e7h, 0e6h, 0c0h
    db  000h, 000h, 000h, 000h, 018h, 018h, 0dbh, 03ch, 0e7h, 03ch, 0dbh, 018h, 018h, 000h, 000h, 000h
    db  000h, 000h, 080h, 0c0h, 0e0h, 0f8h, 0feh, 0f8h, 0e0h, 0c0h, 080h, 000h, 000h, 000h, 000h, 000h
    db  002h, 006h, 00eh, 03eh, 0feh, 03eh, 00eh, 006h, 002h, 000h, 000h, 000h, 000h, 000h, 018h, 03ch
    db  07eh, 018h, 018h, 018h, 07eh, 03ch, 018h, 000h, 000h, 000h, 000h, 000h, 066h, 066h, 066h, 066h
    db  066h, 066h, 000h, 066h, 066h, 000h, 000h, 000h, 000h, 000h, 07fh, 0dbh, 0dbh, 0dbh, 07bh, 01bh
    db  01bh, 01bh, 01bh, 000h, 000h, 000h, 000h, 07ch, 0c6h, 060h, 038h, 06ch, 0c6h, 0c6h, 06ch, 038h
    db  00ch, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0feh, 0feh, 0feh, 000h
    db  000h, 000h, 000h, 000h, 018h, 03ch, 07eh, 018h, 018h, 018h, 07eh, 03ch, 018h, 07eh, 000h, 000h
    db  000h, 000h, 018h, 03ch, 07eh, 018h, 018h, 018h, 018h, 018h, 018h, 000h, 000h, 000h, 000h, 000h
    db  018h, 018h, 018h, 018h, 018h, 018h, 07eh, 03ch, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  018h, 00ch, 0feh, 00ch, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 030h, 060h
    db  0feh, 060h, 030h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0c0h, 0c0h, 0c0h
    db  0feh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 028h, 06ch, 0feh, 06ch, 028h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 010h, 038h, 038h, 07ch, 07ch, 0feh, 0feh, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0feh, 0feh, 07ch, 07ch, 038h, 038h, 010h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  018h, 03ch, 03ch, 03ch, 018h, 018h, 000h, 018h, 018h, 000h, 000h, 000h, 000h, 066h, 066h, 066h
    db  024h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 06ch, 06ch, 0feh, 06ch
    db  06ch, 06ch, 0feh, 06ch, 06ch, 000h, 000h, 000h, 018h, 018h, 07ch, 0c6h, 0c2h, 0c0h, 07ch, 006h
    db  086h, 0c6h, 07ch, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 0c2h, 0c6h, 00ch, 018h, 030h, 066h
    db  0c6h, 000h, 000h, 000h, 000h, 000h, 038h, 06ch, 06ch, 038h, 076h, 0dch, 0cch, 0cch, 076h, 000h
    db  000h, 000h, 000h, 030h, 030h, 030h, 060h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 00ch, 018h, 030h, 030h, 030h, 030h, 030h, 018h, 00ch, 000h, 000h, 000h, 000h, 000h
    db  030h, 018h, 00ch, 00ch, 00ch, 00ch, 00ch, 018h, 030h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  066h, 03ch, 0ffh, 03ch, 066h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h
    db  07eh, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  018h, 018h, 018h, 030h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0feh, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 000h
    db  000h, 000h, 000h, 000h, 002h, 006h, 00ch, 018h, 030h, 060h, 0c0h, 080h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0ceh, 0deh, 0f6h, 0e6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h
    db  018h, 038h, 078h, 018h, 018h, 018h, 018h, 018h, 07eh, 000h, 000h, 000h, 000h, 000h, 07ch, 0c6h
    db  006h, 00ch, 018h, 030h, 060h, 0c6h, 0feh, 000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 006h, 006h
    db  03ch, 006h, 006h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h, 00ch, 01ch, 03ch, 06ch, 0cch, 0feh
    db  00ch, 00ch, 01eh, 000h, 000h, 000h, 000h, 000h, 0feh, 0c0h, 0c0h, 0c0h, 0fch, 006h, 006h, 0c6h
    db  07ch, 000h, 000h, 000h, 000h, 000h, 038h, 060h, 0c0h, 0c0h, 0fch, 0c6h, 0c6h, 0c6h, 07ch, 000h
    db  000h, 000h, 000h, 000h, 0feh, 0c6h, 006h, 00ch, 018h, 030h, 030h, 030h, 030h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 07ch, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h
    db  07ch, 0c6h, 0c6h, 0c6h, 07eh, 006h, 006h, 00ch, 078h, 000h, 000h, 000h, 000h, 000h, 000h, 018h
    db  018h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 000h
    db  000h, 000h, 018h, 018h, 030h, 000h, 000h, 000h, 000h, 000h, 006h, 00ch, 018h, 030h, 060h, 030h
    db  018h, 00ch, 006h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07eh, 000h, 000h, 07eh, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 060h, 030h, 018h, 00ch, 006h, 00ch, 018h, 030h, 060h, 000h
    db  000h, 000h, 000h, 000h, 07ch, 0c6h, 0c6h, 00ch, 018h, 018h, 000h, 018h, 018h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 0deh, 0deh, 0deh, 0dch, 0c0h, 07ch, 000h, 000h, 000h, 000h, 000h
    db  010h, 038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h, 000h, 0fch, 066h
    db  066h, 066h, 07ch, 066h, 066h, 066h, 0fch, 000h, 000h, 000h, 000h, 000h, 03ch, 066h, 0c2h, 0c0h
    db  0c0h, 0c0h, 0c2h, 066h, 03ch, 000h, 000h, 000h, 000h, 000h, 0f8h, 06ch, 066h, 066h, 066h, 066h
    db  066h, 06ch, 0f8h, 000h, 000h, 000h, 000h, 000h, 0feh, 066h, 062h, 068h, 078h, 068h, 062h, 066h
    db  0feh, 000h, 000h, 000h, 000h, 000h, 0feh, 066h, 062h, 068h, 078h, 068h, 060h, 060h, 0f0h, 000h
    db  000h, 000h, 000h, 000h, 03ch, 066h, 0c2h, 0c0h, 0c0h, 0deh, 0c6h, 066h, 03ah, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h, 000h
    db  03ch, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h, 000h, 01eh, 00ch
    db  00ch, 00ch, 00ch, 00ch, 0cch, 0cch, 078h, 000h, 000h, 000h, 000h, 000h, 0e6h, 066h, 06ch, 06ch
    db  078h, 06ch, 06ch, 066h, 0e6h, 000h, 000h, 000h, 000h, 000h, 0f0h, 060h, 060h, 060h, 060h, 060h
    db  062h, 066h, 0feh, 000h, 000h, 000h, 000h, 000h, 0c6h, 0eeh, 0feh, 0feh, 0d6h, 0c6h, 0c6h, 0c6h
    db  0c6h, 000h, 000h, 000h, 000h, 000h, 0c6h, 0e6h, 0f6h, 0feh, 0deh, 0ceh, 0c6h, 0c6h, 0c6h, 000h
    db  000h, 000h, 000h, 000h, 038h, 06ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 06ch, 038h, 000h, 000h, 000h
    db  000h, 000h, 0fch, 066h, 066h, 066h, 07ch, 060h, 060h, 060h, 0f0h, 000h, 000h, 000h, 000h, 000h
    db  07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0d6h, 0deh, 07ch, 00ch, 00eh, 000h, 000h, 000h, 000h, 0fch, 066h
    db  066h, 066h, 07ch, 06ch, 066h, 066h, 0e6h, 000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 0c6h, 060h
    db  038h, 00ch, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h, 07eh, 07eh, 05ah, 018h, 018h, 018h
    db  018h, 018h, 03ch, 000h, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h
    db  07ch, 000h, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 06ch, 038h, 010h, 000h
    db  000h, 000h, 000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0d6h, 0d6h, 0feh, 07ch, 06ch, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 06ch, 038h, 038h, 038h, 06ch, 0c6h, 0c6h, 000h, 000h, 000h, 000h, 000h
    db  066h, 066h, 066h, 066h, 03ch, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h, 000h, 0feh, 0c6h
    db  08ch, 018h, 030h, 060h, 0c2h, 0c6h, 0feh, 000h, 000h, 000h, 000h, 000h, 03ch, 030h, 030h, 030h
    db  030h, 030h, 030h, 030h, 03ch, 000h, 000h, 000h, 000h, 000h, 080h, 0c0h, 0e0h, 070h, 038h, 01ch
    db  00eh, 006h, 002h, 000h, 000h, 000h, 000h, 000h, 03ch, 00ch, 00ch, 00ch, 00ch, 00ch, 00ch, 00ch
    db  03ch, 000h, 000h, 000h, 010h, 038h, 06ch, 0c6h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh, 000h
    db  030h, 030h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h, 000h, 0e0h, 060h
    db  060h, 078h, 06ch, 066h, 066h, 066h, 07ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07ch
    db  0c6h, 0c0h, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h, 01ch, 00ch, 00ch, 03ch, 06ch, 0cch
    db  0cch, 0cch, 076h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c6h
    db  07ch, 000h, 000h, 000h, 000h, 000h, 038h, 06ch, 064h, 060h, 0f0h, 060h, 060h, 060h, 0f0h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 076h, 0cch, 0cch, 0cch, 07ch, 00ch, 0cch, 078h, 000h
    db  000h, 000h, 0e0h, 060h, 060h, 06ch, 076h, 066h, 066h, 066h, 0e6h, 000h, 000h, 000h, 000h, 000h
    db  018h, 018h, 000h, 038h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h, 000h, 006h, 006h
    db  000h, 00eh, 006h, 006h, 006h, 006h, 066h, 066h, 03ch, 000h, 000h, 000h, 0e0h, 060h, 060h, 066h
    db  06ch, 078h, 06ch, 066h, 0e6h, 000h, 000h, 000h, 000h, 000h, 038h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 03ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ech, 0feh, 0d6h, 0d6h, 0d6h
    db  0c6h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0dch, 066h, 066h, 066h, 066h, 066h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0dch, 066h, 066h, 066h, 07ch, 060h, 060h, 0f0h, 000h, 000h, 000h
    db  000h, 000h, 000h, 076h, 0cch, 0cch, 0cch, 07ch, 00ch, 00ch, 01eh, 000h, 000h, 000h, 000h, 000h
    db  000h, 0dch, 076h, 066h, 060h, 060h, 0f0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07ch
    db  0c6h, 070h, 01ch, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h, 010h, 030h, 030h, 0fch, 030h, 030h
    db  030h, 036h, 01ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0cch, 0cch, 0cch, 0cch, 0cch
    db  076h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 066h, 066h, 066h, 066h, 03ch, 018h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 0d6h, 0d6h, 0feh, 06ch, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0c6h, 06ch, 038h, 038h, 06ch, 0c6h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 07eh, 006h, 00ch, 0f8h, 000h, 000h, 000h, 000h, 000h
    db  000h, 0feh, 0cch, 018h, 030h, 066h, 0feh, 000h, 000h, 000h, 000h, 000h, 00eh, 018h, 018h, 018h
    db  070h, 018h, 018h, 018h, 00eh, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 018h, 018h, 000h, 018h
    db  018h, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 070h, 018h, 018h, 018h, 00eh, 018h, 018h, 018h
    db  070h, 000h, 000h, 000h, 000h, 000h, 076h, 0dch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 010h, 038h, 06ch, 0c6h, 0c6h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 066h, 0c2h, 0c0h, 0c0h, 0c2h, 066h, 03ch, 00ch, 006h, 07ch, 000h, 000h, 000h
    db  0cch, 0cch, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h, 00ch, 018h, 030h
    db  000h, 07ch, 0c6h, 0feh, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 010h, 038h, 06ch, 000h, 078h
    db  00ch, 07ch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h, 000h, 0cch, 0cch, 000h, 078h, 00ch, 07ch
    db  0cch, 0cch, 076h, 000h, 000h, 000h, 000h, 060h, 030h, 018h, 000h, 078h, 00ch, 07ch, 0cch, 0cch
    db  076h, 000h, 000h, 000h, 000h, 038h, 06ch, 038h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 076h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 03ch, 066h, 060h, 066h, 03ch, 00ch, 006h, 03ch, 000h, 000h
    db  000h, 010h, 038h, 06ch, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h
    db  0cch, 0cch, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 060h, 030h, 018h
    db  000h, 07ch, 0c6h, 0feh, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h, 066h, 066h, 000h, 038h
    db  018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h, 018h, 03ch, 066h, 000h, 038h, 018h, 018h
    db  018h, 018h, 03ch, 000h, 000h, 000h, 000h, 060h, 030h, 018h, 000h, 038h, 018h, 018h, 018h, 018h
    db  03ch, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 010h, 038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 000h
    db  000h, 000h, 038h, 06ch, 038h, 000h, 038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 000h, 000h, 000h
    db  018h, 030h, 060h, 000h, 0feh, 066h, 060h, 07ch, 060h, 066h, 0feh, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0cch, 076h, 036h, 07eh, 0d8h, 0d8h, 06eh, 000h, 000h, 000h, 000h, 000h, 03eh, 06ch
    db  0cch, 0cch, 0feh, 0cch, 0cch, 0cch, 0ceh, 000h, 000h, 000h, 000h, 010h, 038h, 06ch, 000h, 07ch
    db  0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 000h, 07ch, 0c6h, 0c6h
    db  0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 060h, 030h, 018h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h
    db  07ch, 000h, 000h, 000h, 000h, 030h, 078h, 0cch, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h
    db  000h, 000h, 000h, 060h, 030h, 018h, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 07eh, 006h, 00ch, 078h, 000h, 000h, 0c6h
    db  0c6h, 038h, 06ch, 0c6h, 0c6h, 0c6h, 0c6h, 06ch, 038h, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 000h
    db  0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 018h, 018h, 03ch, 066h, 060h
    db  060h, 066h, 03ch, 018h, 018h, 000h, 000h, 000h, 000h, 038h, 06ch, 064h, 060h, 0f0h, 060h, 060h
    db  060h, 0e6h, 0fch, 000h, 000h, 000h, 000h, 000h, 066h, 066h, 03ch, 018h, 07eh, 018h, 07eh, 018h
    db  018h, 000h, 000h, 000h, 000h, 0f8h, 0cch, 0cch, 0f8h, 0c4h, 0cch, 0deh, 0cch, 0cch, 0c6h, 000h
    db  000h, 000h, 000h, 00eh, 01bh, 018h, 018h, 018h, 07eh, 018h, 018h, 018h, 018h, 0d8h, 070h, 000h
    db  000h, 018h, 030h, 060h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h, 00ch
    db  018h, 030h, 000h, 038h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h, 018h, 030h, 060h
    db  000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 018h, 030h, 060h, 000h, 0cch
    db  0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h, 000h, 076h, 0dch, 000h, 0dch, 066h, 066h
    db  066h, 066h, 066h, 000h, 000h, 000h, 076h, 0dch, 000h, 0c6h, 0e6h, 0f6h, 0feh, 0deh, 0ceh, 0c6h
    db  0c6h, 000h, 000h, 000h, 000h, 03ch, 06ch, 06ch, 03eh, 000h, 07eh, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 038h, 06ch, 06ch, 038h, 000h, 07ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 030h, 030h, 000h, 030h, 030h, 060h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 0feh, 0c0h, 0c0h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0feh, 006h, 006h, 006h, 000h, 000h, 000h, 000h, 000h, 0c0h, 0c0h, 0c6h, 0cch, 0d8h
    db  030h, 060h, 0dch, 086h, 00ch, 018h, 03eh, 000h, 000h, 0c0h, 0c0h, 0c6h, 0cch, 0d8h, 030h, 066h
    db  0ceh, 09eh, 03eh, 006h, 006h, 000h, 000h, 000h, 018h, 018h, 000h, 018h, 018h, 03ch, 03ch, 03ch
    db  018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 036h, 06ch, 0d8h, 06ch, 036h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 0d8h, 06ch, 036h, 06ch, 0d8h, 000h, 000h, 000h, 000h, 000h
    db  011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 055h, 0aah
    db  055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah, 0ddh, 077h, 0ddh, 077h
    db  0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 0f8h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 0f8h, 018h, 0f8h, 018h, 018h
    db  018h, 018h, 018h, 018h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 0f6h, 036h, 036h, 036h, 036h
    db  036h, 036h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0feh, 036h, 036h, 036h, 036h, 036h, 036h
    db  000h, 000h, 000h, 000h, 000h, 0f8h, 018h, 0f8h, 018h, 018h, 018h, 018h, 018h, 018h, 036h, 036h
    db  036h, 036h, 036h, 0f6h, 006h, 0f6h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 000h, 000h, 000h, 000h, 000h, 0feh
    db  006h, 0f6h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 0f6h, 006h, 0feh
    db  000h, 000h, 000h, 000h, 000h, 000h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 0feh, 000h, 000h
    db  000h, 000h, 000h, 000h, 018h, 018h, 018h, 018h, 018h, 0f8h, 018h, 0f8h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0f8h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 01fh, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 01fh, 018h, 018h, 018h, 018h, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh
    db  000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 0ffh, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 01fh, 018h, 01fh, 018h, 018h, 018h, 018h
    db  018h, 018h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 037h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 037h, 030h, 03fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 03fh, 030h, 037h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 0f7h, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh
    db  000h, 0f7h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 037h, 030h, 037h
    db  036h, 036h, 036h, 036h, 036h, 036h, 000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 0ffh, 000h, 000h
    db  000h, 000h, 000h, 000h, 036h, 036h, 036h, 036h, 036h, 0f7h, 000h, 0f7h, 036h, 036h, 036h, 036h
    db  036h, 036h, 018h, 018h, 018h, 018h, 018h, 0ffh, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 0ffh, 000h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 0ffh, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 03fh, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 018h, 018h, 018h, 01fh, 018h, 01fh
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 01fh, 018h, 01fh, 018h, 018h
    db  018h, 018h, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 03fh, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 0ffh, 036h, 036h, 036h, 036h, 036h, 036h
    db  018h, 018h, 018h, 018h, 018h, 0ffh, 018h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 0f8h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 01fh, 018h, 018h, 018h, 018h, 018h, 018h, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh
    db  0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh
    db  0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h
    db  0f0h, 0f0h, 0f0h, 0f0h, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh
    db  00fh, 00fh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 076h, 0dch, 0d8h, 0d8h, 0dch, 076h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0fch, 0c6h, 0c6h, 0fch, 0c0h, 0c0h, 040h, 000h, 000h, 000h, 0feh, 0c6h
    db  0c6h, 0c0h, 0c0h, 0c0h, 0c0h, 0c0h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0feh, 06ch
    db  06ch, 06ch, 06ch, 06ch, 06ch, 000h, 000h, 000h, 000h, 000h, 0feh, 0c6h, 060h, 030h, 018h, 030h
    db  060h, 0c6h, 0feh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07eh, 0d8h, 0d8h, 0d8h, 0d8h
    db  070h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 066h, 066h, 066h, 066h, 07ch, 060h, 060h, 0c0h
    db  000h, 000h, 000h, 000h, 000h, 000h, 076h, 0dch, 018h, 018h, 018h, 018h, 018h, 000h, 000h, 000h
    db  000h, 000h, 07eh, 018h, 03ch, 066h, 066h, 066h, 03ch, 018h, 07eh, 000h, 000h, 000h, 000h, 000h
    db  038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 06ch, 038h, 000h, 000h, 000h, 000h, 000h, 038h, 06ch
    db  0c6h, 0c6h, 0c6h, 06ch, 06ch, 06ch, 0eeh, 000h, 000h, 000h, 000h, 000h, 01eh, 030h, 018h, 00ch
    db  03eh, 066h, 066h, 066h, 03ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07eh, 0dbh, 0dbh
    db  07eh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 003h, 006h, 07eh, 0dbh, 0dbh, 0f3h, 07eh, 060h
    db  0c0h, 000h, 000h, 000h, 000h, 000h, 01ch, 030h, 060h, 060h, 07ch, 060h, 060h, 030h, 01ch, 000h
    db  000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h
    db  000h, 000h, 000h, 0feh, 000h, 000h, 0feh, 000h, 000h, 0feh, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 018h, 018h, 07eh, 018h, 018h, 000h, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 030h, 018h
    db  00ch, 006h, 00ch, 018h, 030h, 000h, 07eh, 000h, 000h, 000h, 000h, 000h, 00ch, 018h, 030h, 060h
    db  030h, 018h, 00ch, 000h, 07eh, 000h, 000h, 000h, 000h, 000h, 00eh, 01bh, 01bh, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 0d8h, 0d8h
    db  070h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 000h, 07eh, 000h, 018h, 018h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 076h, 0dch, 000h, 076h, 0dch, 000h, 000h, 000h, 000h, 000h
    db  000h, 038h, 06ch, 06ch, 038h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 00fh, 00ch, 00ch, 00ch, 00ch
    db  00ch, 0ech, 06ch, 03ch, 01ch, 000h, 000h, 000h, 000h, 0d8h, 06ch, 06ch, 06ch, 06ch, 06ch, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 070h, 0d8h, 030h, 060h, 0c8h, 0f8h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 07ch, 07ch, 07ch, 07ch, 07ch, 07ch, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc6b6a LB 0x135b -> off=0x0 cb=0000000000001000 uValue=00000000000c23ea 'vgafont16'
vgafont16:                                   ; 0xc6b6a LB 0x1000
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07eh, 081h, 0a5h, 081h, 081h, 0bdh, 099h, 081h, 081h, 07eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 07eh, 0ffh, 0dbh, 0ffh, 0ffh, 0c3h, 0e7h, 0ffh, 0ffh, 07eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 06ch, 0feh, 0feh, 0feh, 0feh, 07ch, 038h, 010h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 010h, 038h, 07ch, 0feh, 07ch, 038h, 010h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 018h, 03ch, 03ch, 0e7h, 0e7h, 0e7h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 018h, 03ch, 07eh, 0ffh, 0ffh, 07eh, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 018h, 03ch, 03ch, 018h, 000h, 000h, 000h, 000h, 000h, 000h
    db  0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0e7h, 0c3h, 0c3h, 0e7h, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh
    db  000h, 000h, 000h, 000h, 000h, 03ch, 066h, 042h, 042h, 066h, 03ch, 000h, 000h, 000h, 000h, 000h
    db  0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0c3h, 099h, 0bdh, 0bdh, 099h, 0c3h, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh
    db  000h, 000h, 01eh, 00eh, 01ah, 032h, 078h, 0cch, 0cch, 0cch, 0cch, 078h, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 066h, 066h, 066h, 066h, 03ch, 018h, 07eh, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 03fh, 033h, 03fh, 030h, 030h, 030h, 030h, 070h, 0f0h, 0e0h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07fh, 063h, 07fh, 063h, 063h, 063h, 063h, 067h, 0e7h, 0e6h, 0c0h, 000h, 000h, 000h
    db  000h, 000h, 000h, 018h, 018h, 0dbh, 03ch, 0e7h, 03ch, 0dbh, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 080h, 0c0h, 0e0h, 0f0h, 0f8h, 0feh, 0f8h, 0f0h, 0e0h, 0c0h, 080h, 000h, 000h, 000h, 000h
    db  000h, 002h, 006h, 00eh, 01eh, 03eh, 0feh, 03eh, 01eh, 00eh, 006h, 002h, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 03ch, 07eh, 018h, 018h, 018h, 07eh, 03ch, 018h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 066h, 066h, 066h, 066h, 066h, 066h, 066h, 000h, 066h, 066h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07fh, 0dbh, 0dbh, 0dbh, 07bh, 01bh, 01bh, 01bh, 01bh, 01bh, 000h, 000h, 000h, 000h
    db  000h, 07ch, 0c6h, 060h, 038h, 06ch, 0c6h, 0c6h, 06ch, 038h, 00ch, 0c6h, 07ch, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0feh, 0feh, 0feh, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 03ch, 07eh, 018h, 018h, 018h, 07eh, 03ch, 018h, 07eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 03ch, 07eh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 07eh, 03ch, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 018h, 00ch, 0feh, 00ch, 018h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 030h, 060h, 0feh, 060h, 030h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 0c0h, 0c0h, 0c0h, 0feh, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 028h, 06ch, 0feh, 06ch, 028h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 010h, 038h, 038h, 07ch, 07ch, 0feh, 0feh, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 0feh, 0feh, 07ch, 07ch, 038h, 038h, 010h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 03ch, 03ch, 03ch, 018h, 018h, 018h, 000h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 066h, 066h, 066h, 024h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 06ch, 06ch, 0feh, 06ch, 06ch, 06ch, 0feh, 06ch, 06ch, 000h, 000h, 000h, 000h
    db  018h, 018h, 07ch, 0c6h, 0c2h, 0c0h, 07ch, 006h, 006h, 086h, 0c6h, 07ch, 018h, 018h, 000h, 000h
    db  000h, 000h, 000h, 000h, 0c2h, 0c6h, 00ch, 018h, 030h, 060h, 0c6h, 086h, 000h, 000h, 000h, 000h
    db  000h, 000h, 038h, 06ch, 06ch, 038h, 076h, 0dch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 030h, 030h, 030h, 060h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 00ch, 018h, 030h, 030h, 030h, 030h, 030h, 030h, 018h, 00ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 030h, 018h, 00ch, 00ch, 00ch, 00ch, 00ch, 00ch, 018h, 030h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 066h, 03ch, 0ffh, 03ch, 066h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 018h, 018h, 07eh, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 018h, 030h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0feh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 002h, 006h, 00ch, 018h, 030h, 060h, 0c0h, 080h, 000h, 000h, 000h, 000h
    db  000h, 000h, 038h, 06ch, 0c6h, 0c6h, 0d6h, 0d6h, 0c6h, 0c6h, 06ch, 038h, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 038h, 078h, 018h, 018h, 018h, 018h, 018h, 018h, 07eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 006h, 00ch, 018h, 030h, 060h, 0c0h, 0c6h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 006h, 006h, 03ch, 006h, 006h, 006h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 00ch, 01ch, 03ch, 06ch, 0cch, 0feh, 00ch, 00ch, 00ch, 01eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 0feh, 0c0h, 0c0h, 0c0h, 0fch, 006h, 006h, 006h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 038h, 060h, 0c0h, 0c0h, 0fch, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0feh, 0c6h, 006h, 006h, 00ch, 018h, 030h, 030h, 030h, 030h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 07eh, 006h, 006h, 006h, 00ch, 078h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 018h, 018h, 030h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 006h, 00ch, 018h, 030h, 060h, 030h, 018h, 00ch, 006h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 07eh, 000h, 000h, 07eh, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 060h, 030h, 018h, 00ch, 006h, 00ch, 018h, 030h, 060h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 00ch, 018h, 018h, 018h, 000h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 07ch, 0c6h, 0c6h, 0deh, 0deh, 0deh, 0dch, 0c0h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 010h, 038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0fch, 066h, 066h, 066h, 07ch, 066h, 066h, 066h, 066h, 0fch, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 066h, 0c2h, 0c0h, 0c0h, 0c0h, 0c0h, 0c2h, 066h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0f8h, 06ch, 066h, 066h, 066h, 066h, 066h, 066h, 06ch, 0f8h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0feh, 066h, 062h, 068h, 078h, 068h, 060h, 062h, 066h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 0feh, 066h, 062h, 068h, 078h, 068h, 060h, 060h, 060h, 0f0h, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 066h, 0c2h, 0c0h, 0c0h, 0deh, 0c6h, 0c6h, 066h, 03ah, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 01eh, 00ch, 00ch, 00ch, 00ch, 00ch, 0cch, 0cch, 0cch, 078h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0e6h, 066h, 066h, 06ch, 078h, 078h, 06ch, 066h, 066h, 0e6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0f0h, 060h, 060h, 060h, 060h, 060h, 060h, 062h, 066h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0eeh, 0feh, 0feh, 0d6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0e6h, 0f6h, 0feh, 0deh, 0ceh, 0c6h, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0fch, 066h, 066h, 066h, 07ch, 060h, 060h, 060h, 060h, 0f0h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0d6h, 0deh, 07ch, 00ch, 00eh, 000h, 000h
    db  000h, 000h, 0fch, 066h, 066h, 066h, 07ch, 06ch, 066h, 066h, 066h, 0e6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 07ch, 0c6h, 0c6h, 060h, 038h, 00ch, 006h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 07eh, 07eh, 05ah, 018h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 06ch, 038h, 010h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0d6h, 0d6h, 0d6h, 0feh, 0eeh, 06ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 0c6h, 06ch, 07ch, 038h, 038h, 07ch, 06ch, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 066h, 066h, 066h, 066h, 03ch, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0feh, 0c6h, 086h, 00ch, 018h, 030h, 060h, 0c2h, 0c6h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 030h, 030h, 030h, 030h, 030h, 030h, 030h, 030h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 080h, 0c0h, 0e0h, 070h, 038h, 01ch, 00eh, 006h, 002h, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 00ch, 00ch, 00ch, 00ch, 00ch, 00ch, 00ch, 00ch, 03ch, 000h, 000h, 000h, 000h
    db  010h, 038h, 06ch, 0c6h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 000h
    db  030h, 030h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0e0h, 060h, 060h, 078h, 06ch, 066h, 066h, 066h, 066h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 0c0h, 0c0h, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 01ch, 00ch, 00ch, 03ch, 06ch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 038h, 06ch, 064h, 060h, 0f0h, 060h, 060h, 060h, 060h, 0f0h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 076h, 0cch, 0cch, 0cch, 0cch, 0cch, 07ch, 00ch, 0cch, 078h, 000h
    db  000h, 000h, 0e0h, 060h, 060h, 06ch, 076h, 066h, 066h, 066h, 066h, 0e6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 018h, 000h, 038h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 006h, 006h, 000h, 00eh, 006h, 006h, 006h, 006h, 006h, 006h, 066h, 066h, 03ch, 000h
    db  000h, 000h, 0e0h, 060h, 060h, 066h, 06ch, 078h, 078h, 06ch, 066h, 0e6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 038h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0ech, 0feh, 0d6h, 0d6h, 0d6h, 0d6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0dch, 066h, 066h, 066h, 066h, 066h, 066h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0dch, 066h, 066h, 066h, 066h, 066h, 07ch, 060h, 060h, 0f0h, 000h
    db  000h, 000h, 000h, 000h, 000h, 076h, 0cch, 0cch, 0cch, 0cch, 0cch, 07ch, 00ch, 00ch, 01eh, 000h
    db  000h, 000h, 000h, 000h, 000h, 0dch, 076h, 066h, 060h, 060h, 060h, 0f0h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 07ch, 0c6h, 060h, 038h, 00ch, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 010h, 030h, 030h, 0fch, 030h, 030h, 030h, 030h, 036h, 01ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 066h, 066h, 066h, 066h, 066h, 03ch, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 0d6h, 0d6h, 0d6h, 0feh, 06ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0c6h, 06ch, 038h, 038h, 038h, 06ch, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07eh, 006h, 00ch, 0f8h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0feh, 0cch, 018h, 030h, 060h, 0c6h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 00eh, 018h, 018h, 018h, 070h, 018h, 018h, 018h, 018h, 00eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 018h, 018h, 018h, 018h, 000h, 018h, 018h, 018h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 070h, 018h, 018h, 018h, 00eh, 018h, 018h, 018h, 018h, 070h, 000h, 000h, 000h, 000h
    db  000h, 000h, 076h, 0dch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 010h, 038h, 06ch, 0c6h, 0c6h, 0c6h, 0feh, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 03ch, 066h, 0c2h, 0c0h, 0c0h, 0c0h, 0c2h, 066h, 03ch, 00ch, 006h, 07ch, 000h, 000h
    db  000h, 000h, 0cch, 000h, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 00ch, 018h, 030h, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 010h, 038h, 06ch, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0cch, 000h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 060h, 030h, 018h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 038h, 06ch, 038h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 03ch, 066h, 060h, 060h, 066h, 03ch, 00ch, 006h, 03ch, 000h, 000h, 000h
    db  000h, 010h, 038h, 06ch, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 000h, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 060h, 030h, 018h, 000h, 07ch, 0c6h, 0feh, 0c0h, 0c0h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 066h, 000h, 000h, 038h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 018h, 03ch, 066h, 000h, 038h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 060h, 030h, 018h, 000h, 038h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 0c6h, 000h, 010h, 038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  038h, 06ch, 038h, 000h, 038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  018h, 030h, 060h, 000h, 0feh, 066h, 060h, 07ch, 060h, 060h, 066h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0cch, 076h, 036h, 07eh, 0d8h, 0d8h, 06eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 03eh, 06ch, 0cch, 0cch, 0feh, 0cch, 0cch, 0cch, 0cch, 0ceh, 000h, 000h, 000h, 000h
    db  000h, 010h, 038h, 06ch, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 060h, 030h, 018h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 030h, 078h, 0cch, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 060h, 030h, 018h, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 0c6h, 000h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07eh, 006h, 00ch, 078h, 000h
    db  000h, 0c6h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 0c6h, 000h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 018h, 018h, 03ch, 066h, 060h, 060h, 060h, 066h, 03ch, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 038h, 06ch, 064h, 060h, 0f0h, 060h, 060h, 060h, 060h, 0e6h, 0fch, 000h, 000h, 000h, 000h
    db  000h, 000h, 066h, 066h, 03ch, 018h, 07eh, 018h, 07eh, 018h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 0f8h, 0cch, 0cch, 0f8h, 0c4h, 0cch, 0deh, 0cch, 0cch, 0cch, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 00eh, 01bh, 018h, 018h, 018h, 07eh, 018h, 018h, 018h, 018h, 018h, 0d8h, 070h, 000h, 000h
    db  000h, 018h, 030h, 060h, 000h, 078h, 00ch, 07ch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 00ch, 018h, 030h, 000h, 038h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 018h, 030h, 060h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 018h, 030h, 060h, 000h, 0cch, 0cch, 0cch, 0cch, 0cch, 0cch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 076h, 0dch, 000h, 0dch, 066h, 066h, 066h, 066h, 066h, 066h, 000h, 000h, 000h, 000h
    db  076h, 0dch, 000h, 0c6h, 0e6h, 0f6h, 0feh, 0deh, 0ceh, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 03ch, 06ch, 06ch, 03eh, 000h, 07eh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 038h, 06ch, 06ch, 038h, 000h, 07ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 030h, 030h, 000h, 030h, 030h, 060h, 0c0h, 0c6h, 0c6h, 07ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 0feh, 0c0h, 0c0h, 0c0h, 0c0h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 0feh, 006h, 006h, 006h, 006h, 000h, 000h, 000h, 000h, 000h
    db  000h, 0c0h, 0c0h, 0c2h, 0c6h, 0cch, 018h, 030h, 060h, 0dch, 086h, 00ch, 018h, 03eh, 000h, 000h
    db  000h, 0c0h, 0c0h, 0c2h, 0c6h, 0cch, 018h, 030h, 066h, 0ceh, 09eh, 03eh, 006h, 006h, 000h, 000h
    db  000h, 000h, 018h, 018h, 000h, 018h, 018h, 018h, 03ch, 03ch, 03ch, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 036h, 06ch, 0d8h, 06ch, 036h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0d8h, 06ch, 036h, 06ch, 0d8h, 000h, 000h, 000h, 000h, 000h, 000h
    db  011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h, 011h, 044h
    db  055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah, 055h, 0aah
    db  0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 0ddh, 077h, 0ddh, 077h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 0f8h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 0f8h, 018h, 0f8h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 0f6h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0feh, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  000h, 000h, 000h, 000h, 000h, 0f8h, 018h, 0f8h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  036h, 036h, 036h, 036h, 036h, 0f6h, 006h, 0f6h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  000h, 000h, 000h, 000h, 000h, 0feh, 006h, 0f6h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 0f6h, 006h, 0feh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 0feh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  018h, 018h, 018h, 018h, 018h, 0f8h, 018h, 0f8h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0f8h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 01fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 01fh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 01fh, 018h, 01fh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 037h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 037h, 030h, 03fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 03fh, 030h, 037h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 0f7h, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 0f7h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 037h, 030h, 037h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  036h, 036h, 036h, 036h, 036h, 0f7h, 000h, 0f7h, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  018h, 018h, 018h, 018h, 018h, 0ffh, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 0ffh, 000h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 03fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  018h, 018h, 018h, 018h, 018h, 01fh, 018h, 01fh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 01fh, 018h, 01fh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 03fh, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  036h, 036h, 036h, 036h, 036h, 036h, 036h, 0ffh, 036h, 036h, 036h, 036h, 036h, 036h, 036h, 036h
    db  018h, 018h, 018h, 018h, 018h, 0ffh, 018h, 0ffh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 0f8h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 01fh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh
    db  0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h, 0f0h
    db  00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh, 00fh
    db  0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 076h, 0dch, 0d8h, 0d8h, 0d8h, 0dch, 076h, 000h, 000h, 000h, 000h
    db  000h, 000h, 078h, 0cch, 0cch, 0cch, 0d8h, 0cch, 0c6h, 0c6h, 0c6h, 0cch, 000h, 000h, 000h, 000h
    db  000h, 000h, 0feh, 0c6h, 0c6h, 0c0h, 0c0h, 0c0h, 0c0h, 0c0h, 0c0h, 0c0h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 0feh, 06ch, 06ch, 06ch, 06ch, 06ch, 06ch, 06ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 0feh, 0c6h, 060h, 030h, 018h, 030h, 060h, 0c6h, 0feh, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 07eh, 0d8h, 0d8h, 0d8h, 0d8h, 0d8h, 070h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 066h, 066h, 066h, 066h, 066h, 07ch, 060h, 060h, 0c0h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 076h, 0dch, 018h, 018h, 018h, 018h, 018h, 018h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 07eh, 018h, 03ch, 066h, 066h, 066h, 03ch, 018h, 07eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 038h, 06ch, 0c6h, 0c6h, 0feh, 0c6h, 0c6h, 06ch, 038h, 000h, 000h, 000h, 000h
    db  000h, 000h, 038h, 06ch, 0c6h, 0c6h, 0c6h, 06ch, 06ch, 06ch, 06ch, 0eeh, 000h, 000h, 000h, 000h
    db  000h, 000h, 01eh, 030h, 018h, 00ch, 03eh, 066h, 066h, 066h, 066h, 03ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 07eh, 0dbh, 0dbh, 0dbh, 07eh, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 003h, 006h, 07eh, 0dbh, 0dbh, 0f3h, 07eh, 060h, 0c0h, 000h, 000h, 000h, 000h
    db  000h, 000h, 01ch, 030h, 060h, 060h, 07ch, 060h, 060h, 060h, 030h, 01ch, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 07ch, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 0c6h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 0feh, 000h, 000h, 0feh, 000h, 000h, 0feh, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 018h, 018h, 07eh, 018h, 018h, 000h, 000h, 0ffh, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 030h, 018h, 00ch, 006h, 00ch, 018h, 030h, 000h, 07eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 00ch, 018h, 030h, 060h, 030h, 018h, 00ch, 000h, 07eh, 000h, 000h, 000h, 000h
    db  000h, 000h, 00eh, 01bh, 01bh, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h
    db  018h, 018h, 018h, 018h, 018h, 018h, 018h, 018h, 0d8h, 0d8h, 0d8h, 070h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 018h, 018h, 000h, 07eh, 000h, 018h, 018h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 076h, 0dch, 000h, 076h, 0dch, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 038h, 06ch, 06ch, 038h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 018h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 00fh, 00ch, 00ch, 00ch, 00ch, 00ch, 0ech, 06ch, 06ch, 03ch, 01ch, 000h, 000h, 000h, 000h
    db  000h, 0d8h, 06ch, 06ch, 06ch, 06ch, 06ch, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 070h, 0d8h, 030h, 060h, 0c8h, 0f8h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 07ch, 07ch, 07ch, 07ch, 07ch, 07ch, 07ch, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc7b6a LB 0x35b -> off=0x0 cb=000000000000012d uValue=00000000000c33ea 'vgafont14alt'
vgafont14alt:                                ; 0xc7b6a LB 0x12d
    db  01dh, 000h, 000h, 000h, 000h, 024h, 066h, 0ffh, 066h, 024h, 000h, 000h, 000h, 000h, 000h, 022h
    db  000h, 063h, 063h, 063h, 022h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 02bh, 000h
    db  000h, 000h, 018h, 018h, 018h, 0ffh, 018h, 018h, 018h, 000h, 000h, 000h, 000h, 02dh, 000h, 000h
    db  000h, 000h, 000h, 000h, 0ffh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 04dh, 000h, 000h, 0c3h
    db  0e7h, 0ffh, 0dbh, 0c3h, 0c3h, 0c3h, 0c3h, 0c3h, 000h, 000h, 000h, 054h, 000h, 000h, 0ffh, 0dbh
    db  099h, 018h, 018h, 018h, 018h, 018h, 03ch, 000h, 000h, 000h, 056h, 000h, 000h, 0c3h, 0c3h, 0c3h
    db  0c3h, 0c3h, 0c3h, 066h, 03ch, 018h, 000h, 000h, 000h, 057h, 000h, 000h, 0c3h, 0c3h, 0c3h, 0c3h
    db  0dbh, 0dbh, 0ffh, 066h, 066h, 000h, 000h, 000h, 058h, 000h, 000h, 0c3h, 0c3h, 066h, 03ch, 018h
    db  03ch, 066h, 0c3h, 0c3h, 000h, 000h, 000h, 059h, 000h, 000h, 0c3h, 0c3h, 0c3h, 066h, 03ch, 018h
    db  018h, 018h, 03ch, 000h, 000h, 000h, 05ah, 000h, 000h, 0ffh, 0c3h, 086h, 00ch, 018h, 030h, 061h
    db  0c3h, 0ffh, 000h, 000h, 000h, 06dh, 000h, 000h, 000h, 000h, 000h, 0e6h, 0ffh, 0dbh, 0dbh, 0dbh
    db  0dbh, 000h, 000h, 000h, 076h, 000h, 000h, 000h, 000h, 000h, 0c3h, 0c3h, 0c3h, 066h, 03ch, 018h
    db  000h, 000h, 000h, 077h, 000h, 000h, 000h, 000h, 000h, 0c3h, 0c3h, 0dbh, 0dbh, 0ffh, 066h, 000h
    db  000h, 000h, 091h, 000h, 000h, 000h, 000h, 06eh, 03bh, 01bh, 07eh, 0d8h, 0dch, 077h, 000h, 000h
    db  000h, 09bh, 000h, 018h, 018h, 07eh, 0c3h, 0c0h, 0c0h, 0c3h, 07eh, 018h, 018h, 000h, 000h, 000h
    db  09dh, 000h, 000h, 0c3h, 066h, 03ch, 018h, 0ffh, 018h, 0ffh, 018h, 018h, 000h, 000h, 000h, 09eh
    db  000h, 0fch, 066h, 066h, 07ch, 062h, 066h, 06fh, 066h, 066h, 0f3h, 000h, 000h, 000h, 0f1h, 000h
    db  000h, 018h, 018h, 018h, 0ffh, 018h, 018h, 018h, 000h, 0ffh, 000h, 000h, 000h, 0f6h, 000h, 000h
    db  018h, 018h, 000h, 000h, 0ffh, 000h, 000h, 018h, 018h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc7c97 LB 0x22e -> off=0x0 cb=0000000000000144 uValue=00000000000c3517 'vgafont16alt'
vgafont16alt:                                ; 0xc7c97 LB 0x144
    db  01dh, 000h, 000h, 000h, 000h, 000h, 024h, 066h, 0ffh, 066h, 024h, 000h, 000h, 000h, 000h, 000h
    db  000h, 030h, 000h, 000h, 03ch, 066h, 0c3h, 0c3h, 0dbh, 0dbh, 0c3h, 0c3h, 066h, 03ch, 000h, 000h
    db  000h, 000h, 04dh, 000h, 000h, 0c3h, 0e7h, 0ffh, 0ffh, 0dbh, 0c3h, 0c3h, 0c3h, 0c3h, 0c3h, 000h
    db  000h, 000h, 000h, 054h, 000h, 000h, 0ffh, 0dbh, 099h, 018h, 018h, 018h, 018h, 018h, 018h, 03ch
    db  000h, 000h, 000h, 000h, 056h, 000h, 000h, 0c3h, 0c3h, 0c3h, 0c3h, 0c3h, 0c3h, 0c3h, 066h, 03ch
    db  018h, 000h, 000h, 000h, 000h, 057h, 000h, 000h, 0c3h, 0c3h, 0c3h, 0c3h, 0c3h, 0dbh, 0dbh, 0ffh
    db  066h, 066h, 000h, 000h, 000h, 000h, 058h, 000h, 000h, 0c3h, 0c3h, 066h, 03ch, 018h, 018h, 03ch
    db  066h, 0c3h, 0c3h, 000h, 000h, 000h, 000h, 059h, 000h, 000h, 0c3h, 0c3h, 0c3h, 066h, 03ch, 018h
    db  018h, 018h, 018h, 03ch, 000h, 000h, 000h, 000h, 05ah, 000h, 000h, 0ffh, 0c3h, 086h, 00ch, 018h
    db  030h, 060h, 0c1h, 0c3h, 0ffh, 000h, 000h, 000h, 000h, 06dh, 000h, 000h, 000h, 000h, 000h, 0e6h
    db  0ffh, 0dbh, 0dbh, 0dbh, 0dbh, 0dbh, 000h, 000h, 000h, 000h, 076h, 000h, 000h, 000h, 000h, 000h
    db  0c3h, 0c3h, 0c3h, 0c3h, 066h, 03ch, 018h, 000h, 000h, 000h, 000h, 077h, 000h, 000h, 000h, 000h
    db  000h, 0c3h, 0c3h, 0c3h, 0dbh, 0dbh, 0ffh, 066h, 000h, 000h, 000h, 000h, 078h, 000h, 000h, 000h
    db  000h, 000h, 0c3h, 066h, 03ch, 018h, 03ch, 066h, 0c3h, 000h, 000h, 000h, 000h, 091h, 000h, 000h
    db  000h, 000h, 000h, 06eh, 03bh, 01bh, 07eh, 0d8h, 0dch, 077h, 000h, 000h, 000h, 000h, 09bh, 000h
    db  018h, 018h, 07eh, 0c3h, 0c0h, 0c0h, 0c0h, 0c3h, 07eh, 018h, 018h, 000h, 000h, 000h, 000h, 09dh
    db  000h, 000h, 0c3h, 066h, 03ch, 018h, 0ffh, 018h, 0ffh, 018h, 018h, 018h, 000h, 000h, 000h, 000h
    db  09eh, 000h, 0fch, 066h, 066h, 07ch, 062h, 066h, 06fh, 066h, 066h, 066h, 0f3h, 000h, 000h, 000h
    db  000h, 0abh, 000h, 0c0h, 0c0h, 0c2h, 0c6h, 0cch, 018h, 030h, 060h, 0ceh, 09bh, 006h, 00ch, 01fh
    db  000h, 000h, 0ach, 000h, 0c0h, 0c0h, 0c2h, 0c6h, 0cch, 018h, 030h, 066h, 0ceh, 096h, 03eh, 006h
    db  006h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc7ddb LB 0xea -> off=0x0 cb=0000000000000008 uValue=00000000000c365b '_cga_msr'
_cga_msr:                                    ; 0xc7ddb LB 0x8
    db  02ch, 028h, 02dh, 029h, 02ah, 02eh, 01eh, 029h
  ; disGetNextSymbol 0xc7de3 LB 0xe2 -> off=0x0 cb=0000000000000008 uValue=00000000000c3663 'line_to_vpti_200'
line_to_vpti_200:                            ; 0xc7de3 LB 0x8
    db  000h, 001h, 002h, 003h, 0ffh, 0ffh, 0ffh, 007h
  ; disGetNextSymbol 0xc7deb LB 0xda -> off=0x0 cb=0000000000000008 uValue=00000000000c366b 'line_to_vpti_350'
line_to_vpti_350:                            ; 0xc7deb LB 0x8
    db  013h, 014h, 015h, 016h, 0ffh, 0ffh, 0ffh, 007h
  ; disGetNextSymbol 0xc7df3 LB 0xd2 -> off=0x0 cb=0000000000000008 uValue=00000000000c3673 'line_to_vpti_400'
line_to_vpti_400:                            ; 0xc7df3 LB 0x8
    db  017h, 017h, 018h, 018h, 0ffh, 0ffh, 0ffh, 019h
  ; disGetNextSymbol 0xc7dfb LB 0xca -> off=0x0 cb=0000000000000005 uValue=00000000000c367b 'row_tbl'
row_tbl:                                     ; 0xc7dfb LB 0x5
    db  000h, 00eh, 019h, 02bh, 000h
  ; disGetNextSymbol 0xc7e00 LB 0xc5 -> off=0x0 cb=0000000000000015 uValue=00000000000c3680 '_vbebios_copyright'
_vbebios_copyright:                          ; 0xc7e00 LB 0x15
    db  'VirtualBox VESA BIOS', 000h
  ; disGetNextSymbol 0xc7e15 LB 0xb0 -> off=0x0 cb=000000000000001d uValue=00000000000c3695 '_vbebios_vendor_name'
_vbebios_vendor_name:                        ; 0xc7e15 LB 0x1d
    db  'Oracle and/or its affiliates', 000h
  ; disGetNextSymbol 0xc7e32 LB 0x93 -> off=0x0 cb=000000000000001e uValue=00000000000c36b2 '_vbebios_product_name'
_vbebios_product_name:                       ; 0xc7e32 LB 0x1e
    db  'Oracle VirtualBox VBE Adapter', 000h
  ; disGetNextSymbol 0xc7e50 LB 0x75 -> off=0x0 cb=0000000000000021 uValue=00000000000c36d0 '_vbebios_product_revision'
_vbebios_product_revision:                   ; 0xc7e50 LB 0x21
    db  'Oracle VirtualBox Version 7.2.16', 000h
  ; disGetNextSymbol 0xc7e71 LB 0x54 -> off=0x0 cb=000000000000002b uValue=00000000000c36f1 '_vbebios_info_string'
_vbebios_info_string:                        ; 0xc7e71 LB 0x2b
    db  'VirtualBox VBE Display Adapter enabled', 00dh, 00ah, 00dh, 00ah, 000h
  ; disGetNextSymbol 0xc7e9c LB 0x29 -> off=0x0 cb=0000000000000029 uValue=00000000000c371c '_no_vbebios_info_string'
_no_vbebios_info_string:                     ; 0xc7e9c LB 0x29
    db  'No VirtualBox VBE support available!', 00dh, 00ah, 00dh, 00ah, 000h

  ; Padding 0x1 bytes at 0xc7ec5
    db  001h

section CONST progbits vstart=0x7ec6 align=1 ; size=0x0 class=DATA group=DGROUP

section CONST2 progbits vstart=0x7ec6 align=1 ; size=0x0 class=DATA group=DGROUP

  ; Padding 0x13a bytes at 0xc7ec6
    db  000h, 000h, 000h, 000h, 001h, 000h, 000h, 000h, 000h, 000h, 000h, 02fh, 068h, 06fh, 06dh, 065h
    db  02fh, 061h, 06ch, 065h, 06bh, 073h, 065h, 079h, 02fh, 077h, 06fh, 072h, 06bh, 02fh, 076h, 062h
    db  06fh, 078h, 02fh, 062h, 072h, 061h, 06eh, 063h, 068h, 065h, 073h, 02fh, 056h, 042h, 06fh, 078h
    db  02dh, 037h, 02eh, 032h, 02fh, 06fh, 075h, 074h, 02fh, 06ch, 069h, 06eh, 075h, 078h, 02eh, 061h
    db  06dh, 064h, 036h, 034h, 02fh, 072h, 065h, 06ch, 065h, 061h, 073h, 065h, 02fh, 06fh, 062h, 06ah
    db  02fh, 056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh, 073h, 032h, 038h, 036h, 02fh
    db  056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh, 073h, 032h, 038h, 036h, 02eh, 073h
    db  079h, 06dh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 015h
