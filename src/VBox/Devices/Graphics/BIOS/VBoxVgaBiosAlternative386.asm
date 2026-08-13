; $Id: VBoxVgaBiosAlternative386.asm 115036 2026-08-13 17:41:02Z aleksey.ilyushin@oracle.com $
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





section VGAROM progbits vstart=0x0 align=1 ; size=0x907 class=CODE group=AUTO
  ; disGetNextSymbol 0xc0000 LB 0x907 -> off=0x28 cb=0000000000000548 uValue=00000000000c0028 'vgabios_int10_handler'
    db  055h, 0aah, 040h, 0ebh, 01dh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 049h, 042h
    db  04dh, 000h, 00eh, 01fh, 0fch, 0e9h, 03ch, 00ah
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
    call 008f3h                               ; e8 14 08                    ; 0xc00dc vgarom.asm:197
    jmp short 000edh                          ; eb 0c                       ; 0xc00df vgarom.asm:198
    push ES                                   ; 06                          ; 0xc00e1 vgarom.asm:202
    push DS                                   ; 1e                          ; 0xc00e2 vgarom.asm:203
    pushaw                                    ; 60                          ; 0xc00e3 vgarom.asm:107
    push CS                                   ; 0e                          ; 0xc00e4 vgarom.asm:207
    pop DS                                    ; 1f                          ; 0xc00e5 vgarom.asm:208
    cld                                       ; fc                          ; 0xc00e6 vgarom.asm:209
    call 036b6h                               ; e8 cc 35                    ; 0xc00e7 vgarom.asm:210
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
  ; disGetNextSymbol 0xc0570 LB 0x397 -> off=0x0 cb=0000000000000007 uValue=00000000000c0570 'do_out_dx_ax'
do_out_dx_ax:                                ; 0xc0570 LB 0x7
    db  086h, 0c4h
    ; xchg ah, al                               ; 86 c4                     ; 0xc0570 vberom.asm:69
    out DX, AL                                ; ee                          ; 0xc0572 vberom.asm:70
    db  086h, 0c4h
    ; xchg ah, al                               ; 86 c4                     ; 0xc0573 vberom.asm:71
    out DX, AL                                ; ee                          ; 0xc0575 vberom.asm:72
    retn                                      ; c3                          ; 0xc0576 vberom.asm:73
  ; disGetNextSymbol 0xc0577 LB 0x390 -> off=0x0 cb=0000000000000040 uValue=00000000000c0577 'do_in_ax_dx'
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
  ; disGetNextSymbol 0xc05b7 LB 0x350 -> off=0x0 cb=0000000000000026 uValue=00000000000c05b7 '_dispi_get_max_bpp'
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
  ; disGetNextSymbol 0xc05dd LB 0x32a -> off=0x0 cb=0000000000000026 uValue=00000000000c05dd 'dispi_set_enable_'
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
  ; disGetNextSymbol 0xc0603 LB 0x304 -> off=0x0 cb=0000000000000026 uValue=00000000000c0603 'dispi_set_bank_'
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
  ; disGetNextSymbol 0xc0629 LB 0x2de -> off=0x0 cb=00000000000000a9 uValue=00000000000c0629 '_dispi_set_bank_farcall'
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
  ; disGetNextSymbol 0xc06d2 LB 0x235 -> off=0x0 cb=00000000000000ed uValue=00000000000c06d2 '_vga_compat_setup'
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
  ; disGetNextSymbol 0xc07bf LB 0x148 -> off=0x0 cb=0000000000000013 uValue=00000000000c07bf '_vbe_has_vbe_display'
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
  ; disGetNextSymbol 0xc07d2 LB 0x135 -> off=0x0 cb=0000000000000025 uValue=00000000000c07d2 'vbe_biosfn_return_current_mode'
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
  ; disGetNextSymbol 0xc07f7 LB 0x110 -> off=0x0 cb=000000000000002d uValue=00000000000c07f7 'vbe_biosfn_display_window_control'
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
  ; disGetNextSymbol 0xc0824 LB 0xe3 -> off=0x0 cb=0000000000000034 uValue=00000000000c0824 'vbe_biosfn_set_get_display_start'
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
  ; disGetNextSymbol 0xc0858 LB 0xaf -> off=0x0 cb=0000000000000037 uValue=00000000000c0858 'vbe_biosfn_set_get_dac_palette_format'
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
  ; disGetNextSymbol 0xc088f LB 0x78 -> off=0x0 cb=0000000000000064 uValue=00000000000c088f 'vbe_biosfn_set_get_palette_data'
vbe_biosfn_set_get_palette_data:             ; 0xc088f LB 0x64
    test bl, bl                               ; 84 db                       ; 0xc088f vberom.asm:683
    je short 008a2h                           ; 74 0f                       ; 0xc0891 vberom.asm:684
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc0893 vberom.asm:685
    je short 008cah                           ; 74 32                       ; 0xc0896 vberom.asm:686
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc0898 vberom.asm:687
    jbe short 008efh                          ; 76 52                       ; 0xc089b vberom.asm:688
    cmp bl, 080h                              ; 80 fb 80                    ; 0xc089d vberom.asm:689
    jne short 008ebh                          ; 75 49                       ; 0xc08a0 vberom.asm:690
    pushad                                    ; 66 60                       ; 0xc08a2 vberom.asm:141
    push DS                                   ; 1e                          ; 0xc08a4 vberom.asm:696
    push ES                                   ; 06                          ; 0xc08a5 vberom.asm:697
    pop DS                                    ; 1f                          ; 0xc08a6 vberom.asm:698
    db  08ah, 0c2h
    ; mov al, dl                                ; 8a c2                     ; 0xc08a7 vberom.asm:699
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc08a9 vberom.asm:700
    out DX, AL                                ; ee                          ; 0xc08ac vberom.asm:701
    inc dx                                    ; 42                          ; 0xc08ad vberom.asm:702
    db  08bh, 0f7h
    ; mov si, di                                ; 8b f7                     ; 0xc08ae vberom.asm:703
    lodsd                                     ; 66 ad                       ; 0xc08b0 vberom.asm:706
    ror eax, 010h                             ; 66 c1 c8 10                 ; 0xc08b2 vberom.asm:707
    out DX, AL                                ; ee                          ; 0xc08b6 vberom.asm:708
    rol eax, 008h                             ; 66 c1 c0 08                 ; 0xc08b7 vberom.asm:709
    out DX, AL                                ; ee                          ; 0xc08bb vberom.asm:710
    rol eax, 008h                             ; 66 c1 c0 08                 ; 0xc08bc vberom.asm:711
    out DX, AL                                ; ee                          ; 0xc08c0 vberom.asm:712
    loop 008b0h                               ; e2 ed                       ; 0xc08c1 vberom.asm:723
    pop DS                                    ; 1f                          ; 0xc08c3 vberom.asm:724
    popad                                     ; 66 61                       ; 0xc08c4 vberom.asm:160
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc08c6 vberom.asm:727
    retn                                      ; c3                          ; 0xc08c9 vberom.asm:728
    pushad                                    ; 66 60                       ; 0xc08ca vberom.asm:141
    db  08ah, 0c2h
    ; mov al, dl                                ; 8a c2                     ; 0xc08cc vberom.asm:732
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc08ce vberom.asm:733
    out DX, AL                                ; ee                          ; 0xc08d1 vberom.asm:734
    add dl, 002h                              ; 80 c2 02                    ; 0xc08d2 vberom.asm:735
    db  066h, 033h, 0c0h
    ; xor eax, eax                              ; 66 33 c0                  ; 0xc08d5 vberom.asm:738
    in AL, DX                                 ; ec                          ; 0xc08d8 vberom.asm:739
    sal eax, 008h                             ; 66 c1 e0 08                 ; 0xc08d9 vberom.asm:740
    in AL, DX                                 ; ec                          ; 0xc08dd vberom.asm:741
    sal eax, 008h                             ; 66 c1 e0 08                 ; 0xc08de vberom.asm:742
    in AL, DX                                 ; ec                          ; 0xc08e2 vberom.asm:743
    stosd                                     ; 66 ab                       ; 0xc08e3 vberom.asm:744
    loop 008d5h                               ; e2 ee                       ; 0xc08e5 vberom.asm:757
    popad                                     ; 66 61                       ; 0xc08e7 vberom.asm:160
    jmp short 008c6h                          ; eb db                       ; 0xc08e9 vberom.asm:759
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc08eb vberom.asm:762
    retn                                      ; c3                          ; 0xc08ee vberom.asm:763
    mov ax, 0024fh                            ; b8 4f 02                    ; 0xc08ef vberom.asm:765
    retn                                      ; c3                          ; 0xc08f2 vberom.asm:766
  ; disGetNextSymbol 0xc08f3 LB 0x14 -> off=0x0 cb=0000000000000014 uValue=00000000000c08f3 'vbe_biosfn_return_protected_mode_interface'
vbe_biosfn_return_protected_mode_interface: ; 0xc08f3 LB 0x14
    test bl, bl                               ; 84 db                       ; 0xc08f3 vberom.asm:780
    jne short 00903h                          ; 75 0c                       ; 0xc08f5 vberom.asm:781
    push CS                                   ; 0e                          ; 0xc08f7 vberom.asm:782
    pop ES                                    ; 07                          ; 0xc08f8 vberom.asm:783
    mov di, 04640h                            ; bf 40 46                    ; 0xc08f9 vberom.asm:784
    mov cx, 00115h                            ; b9 15 01                    ; 0xc08fc vberom.asm:785
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc08ff vberom.asm:786
    retn                                      ; c3                          ; 0xc0902 vberom.asm:787
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc0903 vberom.asm:789
    retn                                      ; c3                          ; 0xc0906 vberom.asm:790

  ; Padding 0xe9 bytes at 0xc0907
  times 233 db 0

section _TEXT progbits vstart=0x9f0 align=1 ; size=0x38d3 class=CODE group=AUTO
  ; disGetNextSymbol 0xc09f0 LB 0x38d3 -> off=0x0 cb=000000000000001a uValue=00000000000c09f0 'set_int_vector'
set_int_vector:                              ; 0xc09f0 LB 0x1a
    push dx                                   ; 52                          ; 0xc09f0 vgabios.c:87
    push bp                                   ; 55                          ; 0xc09f1
    mov bp, sp                                ; 89 e5                       ; 0xc09f2
    mov dx, bx                                ; 89 da                       ; 0xc09f4
    movzx bx, al                              ; 0f b6 d8                    ; 0xc09f6 vgabios.c:91
    sal bx, 002h                              ; c1 e3 02                    ; 0xc09f9
    xor ax, ax                                ; 31 c0                       ; 0xc09fc
    mov es, ax                                ; 8e c0                       ; 0xc09fe
    mov word [es:bx], dx                      ; 26 89 17                    ; 0xc0a00
    mov word [es:bx+002h], cx                 ; 26 89 4f 02                 ; 0xc0a03
    pop bp                                    ; 5d                          ; 0xc0a07 vgabios.c:92
    pop dx                                    ; 5a                          ; 0xc0a08
    retn                                      ; c3                          ; 0xc0a09
  ; disGetNextSymbol 0xc0a0a LB 0x38b9 -> off=0x0 cb=000000000000001c uValue=00000000000c0a0a 'init_vga_card'
init_vga_card:                               ; 0xc0a0a LB 0x1c
    push bp                                   ; 55                          ; 0xc0a0a vgabios.c:143
    mov bp, sp                                ; 89 e5                       ; 0xc0a0b
    push dx                                   ; 52                          ; 0xc0a0d
    mov AL, strict byte 0c3h                  ; b0 c3                       ; 0xc0a0e vgabios.c:146
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc0a10
    out DX, AL                                ; ee                          ; 0xc0a13
    mov AL, strict byte 004h                  ; b0 04                       ; 0xc0a14 vgabios.c:149
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc0a16
    out DX, AL                                ; ee                          ; 0xc0a19
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc0a1a vgabios.c:150
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc0a1c
    out DX, AL                                ; ee                          ; 0xc0a1f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc0a20 vgabios.c:155
    pop dx                                    ; 5a                          ; 0xc0a23
    pop bp                                    ; 5d                          ; 0xc0a24
    retn                                      ; c3                          ; 0xc0a25
  ; disGetNextSymbol 0xc0a26 LB 0x389d -> off=0x0 cb=000000000000003e uValue=00000000000c0a26 'init_bios_area'
init_bios_area:                              ; 0xc0a26 LB 0x3e
    push bx                                   ; 53                          ; 0xc0a26 vgabios.c:221
    push bp                                   ; 55                          ; 0xc0a27
    mov bp, sp                                ; 89 e5                       ; 0xc0a28
    xor bx, bx                                ; 31 db                       ; 0xc0a2a vgabios.c:225
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0a2c
    mov es, ax                                ; 8e c0                       ; 0xc0a2f
    mov al, byte [es:bx+010h]                 ; 26 8a 47 10                 ; 0xc0a31 vgabios.c:228
    and AL, strict byte 0cfh                  ; 24 cf                       ; 0xc0a35
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc0a37
    mov byte [es:bx+010h], al                 ; 26 88 47 10                 ; 0xc0a39
    mov byte [es:bx+00085h], 010h             ; 26 c6 87 85 00 10           ; 0xc0a3d vgabios.c:232
    mov word [es:bx+00087h], 0f960h           ; 26 c7 87 87 00 60 f9        ; 0xc0a43 vgabios.c:234
    mov byte [es:bx+00089h], 051h             ; 26 c6 87 89 00 51           ; 0xc0a4a vgabios.c:238
    mov byte [es:bx+065h], 009h               ; 26 c6 47 65 09              ; 0xc0a50 vgabios.c:240
    mov word [es:bx+000a8h], 0554eh           ; 26 c7 87 a8 00 4e 55        ; 0xc0a55 vgabios.c:242
    mov [es:bx+000aah], ds                    ; 26 8c 9f aa 00              ; 0xc0a5c
    pop bp                                    ; 5d                          ; 0xc0a61 vgabios.c:243
    pop bx                                    ; 5b                          ; 0xc0a62
    retn                                      ; c3                          ; 0xc0a63
  ; disGetNextSymbol 0xc0a64 LB 0x385f -> off=0x0 cb=000000000000002f uValue=00000000000c0a64 'vgabios_init_func'
vgabios_init_func:                           ; 0xc0a64 LB 0x2f
    push bp                                   ; 55                          ; 0xc0a64 vgabios.c:250
    mov bp, sp                                ; 89 e5                       ; 0xc0a65
    call 00a0ah                               ; e8 a0 ff                    ; 0xc0a67 vgabios.c:252
    call 00a26h                               ; e8 b9 ff                    ; 0xc0a6a vgabios.c:253
    call 03c66h                               ; e8 f6 31                    ; 0xc0a6d vgabios.c:255
    mov bx, strict word 00028h                ; bb 28 00                    ; 0xc0a70 vgabios.c:257
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0a73
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc0a76
    call 009f0h                               ; e8 74 ff                    ; 0xc0a79
    mov bx, strict word 00028h                ; bb 28 00                    ; 0xc0a7c vgabios.c:258
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0a7f
    mov ax, strict word 0006dh                ; b8 6d 00                    ; 0xc0a82
    call 009f0h                               ; e8 68 ff                    ; 0xc0a85
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc0a88 vgabios.c:284
    db  032h, 0e4h
    ; xor ah, ah                                ; 32 e4                     ; 0xc0a8b
    int 010h                                  ; cd 10                       ; 0xc0a8d
    mov sp, bp                                ; 89 ec                       ; 0xc0a8f vgabios.c:287
    pop bp                                    ; 5d                          ; 0xc0a91
    retf                                      ; cb                          ; 0xc0a92
  ; disGetNextSymbol 0xc0a93 LB 0x3830 -> off=0x0 cb=000000000000002d uValue=00000000000c0a93 'vga_get_cursor_pos'
vga_get_cursor_pos:                          ; 0xc0a93 LB 0x2d
    push si                                   ; 56                          ; 0xc0a93 vgabios.c:356
    push di                                   ; 57                          ; 0xc0a94
    push bp                                   ; 55                          ; 0xc0a95
    mov bp, sp                                ; 89 e5                       ; 0xc0a96
    mov si, dx                                ; 89 d6                       ; 0xc0a98
    mov di, strict word 00060h                ; bf 60 00                    ; 0xc0a9a vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0a9d
    mov es, dx                                ; 8e c2                       ; 0xc0aa0
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0aa2
    push SS                                   ; 16                          ; 0xc0aa5 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0aa6
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc0aa7
    movzx si, al                              ; 0f b6 f0                    ; 0xc0aaa vgabios.c:360
    add si, si                                ; 01 f6                       ; 0xc0aad
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc0aaf
    mov es, dx                                ; 8e c2                       ; 0xc0ab2 vgabios.c:57
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc0ab4
    push SS                                   ; 16                          ; 0xc0ab7 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0ab8
    mov word [es:bx], si                      ; 26 89 37                    ; 0xc0ab9
    pop bp                                    ; 5d                          ; 0xc0abc vgabios.c:361
    pop di                                    ; 5f                          ; 0xc0abd
    pop si                                    ; 5e                          ; 0xc0abe
    retn                                      ; c3                          ; 0xc0abf
  ; disGetNextSymbol 0xc0ac0 LB 0x3803 -> off=0x0 cb=000000000000005d uValue=00000000000c0ac0 'vga_find_glyph'
vga_find_glyph:                              ; 0xc0ac0 LB 0x5d
    push bp                                   ; 55                          ; 0xc0ac0 vgabios.c:364
    mov bp, sp                                ; 89 e5                       ; 0xc0ac1
    push si                                   ; 56                          ; 0xc0ac3
    push di                                   ; 57                          ; 0xc0ac4
    push ax                                   ; 50                          ; 0xc0ac5
    push ax                                   ; 50                          ; 0xc0ac6
    push dx                                   ; 52                          ; 0xc0ac7
    push bx                                   ; 53                          ; 0xc0ac8
    mov bl, cl                                ; 88 cb                       ; 0xc0ac9
    mov word [bp-006h], strict word 00000h    ; c7 46 fa 00 00              ; 0xc0acb vgabios.c:366
    dec word [bp+004h]                        ; ff 4e 04                    ; 0xc0ad0 vgabios.c:368
    cmp word [bp+004h], strict byte 0ffffh    ; 83 7e 04 ff                 ; 0xc0ad3
    je short 00b11h                           ; 74 38                       ; 0xc0ad7
    movzx cx, byte [bp+006h]                  ; 0f b6 4e 06                 ; 0xc0ad9 vgabios.c:369
    mov dx, ss                                ; 8c d2                       ; 0xc0add
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc0adf
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc0ae2
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc0ae5
    push DS                                   ; 1e                          ; 0xc0ae8
    mov ds, dx                                ; 8e da                       ; 0xc0ae9
    rep cmpsb                                 ; f3 a6                       ; 0xc0aeb
    pop DS                                    ; 1f                          ; 0xc0aed
    mov ax, strict word 00000h                ; b8 00 00                    ; 0xc0aee
    je near 00af7h                            ; 0f 84 02 00                 ; 0xc0af1
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc0af5
    test ax, ax                               ; 85 c0                       ; 0xc0af7
    jne short 00b06h                          ; 75 0b                       ; 0xc0af9
    movzx ax, bl                              ; 0f b6 c3                    ; 0xc0afb vgabios.c:370
    or ah, 080h                               ; 80 cc 80                    ; 0xc0afe
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0b01
    jmp short 00b11h                          ; eb 0b                       ; 0xc0b04 vgabios.c:371
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc0b06 vgabios.c:373
    add word [bp-008h], ax                    ; 01 46 f8                    ; 0xc0b0a
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc0b0d vgabios.c:374
    jmp short 00ad0h                          ; eb bf                       ; 0xc0b0f vgabios.c:375
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc0b11 vgabios.c:377
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b14
    pop di                                    ; 5f                          ; 0xc0b17
    pop si                                    ; 5e                          ; 0xc0b18
    pop bp                                    ; 5d                          ; 0xc0b19
    retn 00004h                               ; c2 04 00                    ; 0xc0b1a
  ; disGetNextSymbol 0xc0b1d LB 0x37a6 -> off=0x0 cb=0000000000000046 uValue=00000000000c0b1d 'vga_read_glyph_planar'
vga_read_glyph_planar:                       ; 0xc0b1d LB 0x46
    push bp                                   ; 55                          ; 0xc0b1d vgabios.c:379
    mov bp, sp                                ; 89 e5                       ; 0xc0b1e
    push si                                   ; 56                          ; 0xc0b20
    push di                                   ; 57                          ; 0xc0b21
    push ax                                   ; 50                          ; 0xc0b22
    push ax                                   ; 50                          ; 0xc0b23
    mov si, ax                                ; 89 c6                       ; 0xc0b24
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc0b26
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc0b29
    mov bx, cx                                ; 89 cb                       ; 0xc0b2c
    mov ax, 00805h                            ; b8 05 08                    ; 0xc0b2e vgabios.c:386
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b31
    out DX, ax                                ; ef                          ; 0xc0b34
    dec byte [bp+004h]                        ; fe 4e 04                    ; 0xc0b35 vgabios.c:388
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc0b38
    je short 00b53h                           ; 74 15                       ; 0xc0b3c
    mov es, [bp-006h]                         ; 8e 46 fa                    ; 0xc0b3e vgabios.c:389
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc0b41
    not al                                    ; f6 d0                       ; 0xc0b44
    mov di, bx                                ; 89 df                       ; 0xc0b46
    inc bx                                    ; 43                          ; 0xc0b48
    push SS                                   ; 16                          ; 0xc0b49
    pop ES                                    ; 07                          ; 0xc0b4a
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0b4b
    add si, word [bp-008h]                    ; 03 76 f8                    ; 0xc0b4e vgabios.c:390
    jmp short 00b35h                          ; eb e2                       ; 0xc0b51 vgabios.c:391
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0b53 vgabios.c:394
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b56
    out DX, ax                                ; ef                          ; 0xc0b59
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b5a vgabios.c:395
    pop di                                    ; 5f                          ; 0xc0b5d
    pop si                                    ; 5e                          ; 0xc0b5e
    pop bp                                    ; 5d                          ; 0xc0b5f
    retn 00002h                               ; c2 02 00                    ; 0xc0b60
  ; disGetNextSymbol 0xc0b63 LB 0x3760 -> off=0x0 cb=000000000000002a uValue=00000000000c0b63 'vga_char_ofs_planar'
vga_char_ofs_planar:                         ; 0xc0b63 LB 0x2a
    push bp                                   ; 55                          ; 0xc0b63 vgabios.c:397
    mov bp, sp                                ; 89 e5                       ; 0xc0b64
    xor dh, dh                                ; 30 f6                       ; 0xc0b66 vgabios.c:401
    imul bx, dx                               ; 0f af da                    ; 0xc0b68
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc0b6b
    imul bx, dx                               ; 0f af da                    ; 0xc0b6f
    xor ah, ah                                ; 30 e4                       ; 0xc0b72
    add ax, bx                                ; 01 d8                       ; 0xc0b74
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc0b76 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0b79
    mov es, dx                                ; 8e c2                       ; 0xc0b7c
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0b7e
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc0b81 vgabios.c:58
    imul dx, bx                               ; 0f af d3                    ; 0xc0b84
    add ax, dx                                ; 01 d0                       ; 0xc0b87
    pop bp                                    ; 5d                          ; 0xc0b89 vgabios.c:405
    retn 00002h                               ; c2 02 00                    ; 0xc0b8a
  ; disGetNextSymbol 0xc0b8d LB 0x3736 -> off=0x0 cb=000000000000003e uValue=00000000000c0b8d 'vga_read_char_planar'
vga_read_char_planar:                        ; 0xc0b8d LB 0x3e
    push bp                                   ; 55                          ; 0xc0b8d vgabios.c:407
    mov bp, sp                                ; 89 e5                       ; 0xc0b8e
    push cx                                   ; 51                          ; 0xc0b90
    push si                                   ; 56                          ; 0xc0b91
    push di                                   ; 57                          ; 0xc0b92
    sub sp, strict byte 00010h                ; 83 ec 10                    ; 0xc0b93
    mov si, ax                                ; 89 c6                       ; 0xc0b96
    mov ax, dx                                ; 89 d0                       ; 0xc0b98
    movzx di, bl                              ; 0f b6 fb                    ; 0xc0b9a vgabios.c:411
    push di                                   ; 57                          ; 0xc0b9d
    lea cx, [bp-016h]                         ; 8d 4e ea                    ; 0xc0b9e
    mov bx, si                                ; 89 f3                       ; 0xc0ba1
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0ba3
    call 00b1dh                               ; e8 74 ff                    ; 0xc0ba6
    push di                                   ; 57                          ; 0xc0ba9 vgabios.c:414
    push 00100h                               ; 68 00 01                    ; 0xc0baa
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0bad vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0bb0
    mov es, ax                                ; 8e c0                       ; 0xc0bb2
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0bb4
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0bb7
    xor cx, cx                                ; 31 c9                       ; 0xc0bbb vgabios.c:68
    lea bx, [bp-016h]                         ; 8d 5e ea                    ; 0xc0bbd
    call 00ac0h                               ; e8 fd fe                    ; 0xc0bc0
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc0bc3 vgabios.c:415
    pop di                                    ; 5f                          ; 0xc0bc6
    pop si                                    ; 5e                          ; 0xc0bc7
    pop cx                                    ; 59                          ; 0xc0bc8
    pop bp                                    ; 5d                          ; 0xc0bc9
    retn                                      ; c3                          ; 0xc0bca
  ; disGetNextSymbol 0xc0bcb LB 0x36f8 -> off=0x0 cb=000000000000001a uValue=00000000000c0bcb 'vga_char_ofs_linear'
vga_char_ofs_linear:                         ; 0xc0bcb LB 0x1a
    push bp                                   ; 55                          ; 0xc0bcb vgabios.c:417
    mov bp, sp                                ; 89 e5                       ; 0xc0bcc
    xor dh, dh                                ; 30 f6                       ; 0xc0bce vgabios.c:421
    imul dx, bx                               ; 0f af d3                    ; 0xc0bd0
    movzx bx, byte [bp+004h]                  ; 0f b6 5e 04                 ; 0xc0bd3
    imul bx, dx                               ; 0f af da                    ; 0xc0bd7
    xor ah, ah                                ; 30 e4                       ; 0xc0bda
    add ax, bx                                ; 01 d8                       ; 0xc0bdc
    sal ax, 003h                              ; c1 e0 03                    ; 0xc0bde vgabios.c:422
    pop bp                                    ; 5d                          ; 0xc0be1 vgabios.c:424
    retn 00002h                               ; c2 02 00                    ; 0xc0be2
  ; disGetNextSymbol 0xc0be5 LB 0x36de -> off=0x0 cb=000000000000004b uValue=00000000000c0be5 'vga_read_glyph_linear'
vga_read_glyph_linear:                       ; 0xc0be5 LB 0x4b
    push si                                   ; 56                          ; 0xc0be5 vgabios.c:426
    push di                                   ; 57                          ; 0xc0be6
    enter 00004h, 000h                        ; c8 04 00 00                 ; 0xc0be7
    mov si, ax                                ; 89 c6                       ; 0xc0beb
    mov word [bp-002h], dx                    ; 89 56 fe                    ; 0xc0bed
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc0bf0
    mov bx, cx                                ; 89 cb                       ; 0xc0bf3
    dec byte [bp+008h]                        ; fe 4e 08                    ; 0xc0bf5 vgabios.c:432
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc0bf8
    je short 00c2ah                           ; 74 2c                       ; 0xc0bfc
    xor dh, dh                                ; 30 f6                       ; 0xc0bfe vgabios.c:433
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc0c00 vgabios.c:434
    xor ax, ax                                ; 31 c0                       ; 0xc0c02 vgabios.c:435
    jmp short 00c0bh                          ; eb 05                       ; 0xc0c04
    cmp ax, strict word 00008h                ; 3d 08 00                    ; 0xc0c06
    jnl short 00c1fh                          ; 7d 14                       ; 0xc0c09
    mov es, [bp-002h]                         ; 8e 46 fe                    ; 0xc0c0b vgabios.c:436
    mov di, si                                ; 89 f7                       ; 0xc0c0e
    add di, ax                                ; 01 c7                       ; 0xc0c10
    cmp byte [es:di], 000h                    ; 26 80 3d 00                 ; 0xc0c12
    je short 00c1ah                           ; 74 02                       ; 0xc0c16
    or dh, dl                                 ; 08 d6                       ; 0xc0c18 vgabios.c:437
    shr dl, 1                                 ; d0 ea                       ; 0xc0c1a vgabios.c:438
    inc ax                                    ; 40                          ; 0xc0c1c vgabios.c:439
    jmp short 00c06h                          ; eb e7                       ; 0xc0c1d
    mov di, bx                                ; 89 df                       ; 0xc0c1f vgabios.c:440
    inc bx                                    ; 43                          ; 0xc0c21
    mov byte [ss:di], dh                      ; 36 88 35                    ; 0xc0c22
    add si, word [bp-004h]                    ; 03 76 fc                    ; 0xc0c25 vgabios.c:441
    jmp short 00bf5h                          ; eb cb                       ; 0xc0c28 vgabios.c:442
    leave                                     ; c9                          ; 0xc0c2a vgabios.c:443
    pop di                                    ; 5f                          ; 0xc0c2b
    pop si                                    ; 5e                          ; 0xc0c2c
    retn 00002h                               ; c2 02 00                    ; 0xc0c2d
  ; disGetNextSymbol 0xc0c30 LB 0x3693 -> off=0x0 cb=000000000000003f uValue=00000000000c0c30 'vga_read_char_linear'
vga_read_char_linear:                        ; 0xc0c30 LB 0x3f
    push bp                                   ; 55                          ; 0xc0c30 vgabios.c:445
    mov bp, sp                                ; 89 e5                       ; 0xc0c31
    push cx                                   ; 51                          ; 0xc0c33
    push si                                   ; 56                          ; 0xc0c34
    sub sp, strict byte 00010h                ; 83 ec 10                    ; 0xc0c35
    mov cx, ax                                ; 89 c1                       ; 0xc0c38
    mov ax, dx                                ; 89 d0                       ; 0xc0c3a
    movzx si, bl                              ; 0f b6 f3                    ; 0xc0c3c vgabios.c:449
    push si                                   ; 56                          ; 0xc0c3f
    mov bx, cx                                ; 89 cb                       ; 0xc0c40
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0c42
    lea cx, [bp-014h]                         ; 8d 4e ec                    ; 0xc0c45
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0c48
    call 00be5h                               ; e8 97 ff                    ; 0xc0c4b
    push si                                   ; 56                          ; 0xc0c4e vgabios.c:452
    push 00100h                               ; 68 00 01                    ; 0xc0c4f
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0c52 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0c55
    mov es, ax                                ; 8e c0                       ; 0xc0c57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c59
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0c5c
    xor cx, cx                                ; 31 c9                       ; 0xc0c60 vgabios.c:68
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc0c62
    call 00ac0h                               ; e8 58 fe                    ; 0xc0c65
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0c68 vgabios.c:453
    pop si                                    ; 5e                          ; 0xc0c6b
    pop cx                                    ; 59                          ; 0xc0c6c
    pop bp                                    ; 5d                          ; 0xc0c6d
    retn                                      ; c3                          ; 0xc0c6e
  ; disGetNextSymbol 0xc0c6f LB 0x3654 -> off=0x0 cb=0000000000000035 uValue=00000000000c0c6f 'vga_read_2bpp_char'
vga_read_2bpp_char:                          ; 0xc0c6f LB 0x35
    push bp                                   ; 55                          ; 0xc0c6f vgabios.c:455
    mov bp, sp                                ; 89 e5                       ; 0xc0c70
    push bx                                   ; 53                          ; 0xc0c72
    push cx                                   ; 51                          ; 0xc0c73
    mov bx, ax                                ; 89 c3                       ; 0xc0c74
    mov es, dx                                ; 8e c2                       ; 0xc0c76
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0c78 vgabios.c:461
    mov DH, strict byte 080h                  ; b6 80                       ; 0xc0c7b vgabios.c:462
    xor dl, dl                                ; 30 d2                       ; 0xc0c7d vgabios.c:463
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c7f vgabios.c:464
    db  086h, 0c4h
    ; xchg ah, al                               ; 86 c4                     ; 0xc0c82
    xor bx, bx                                ; 31 db                       ; 0xc0c84 vgabios.c:466
    jmp short 00c8dh                          ; eb 05                       ; 0xc0c86
    cmp bx, strict byte 00008h                ; 83 fb 08                    ; 0xc0c88
    jnl short 00c9bh                          ; 7d 0e                       ; 0xc0c8b
    test ax, cx                               ; 85 c8                       ; 0xc0c8d vgabios.c:467
    je short 00c93h                           ; 74 02                       ; 0xc0c8f
    or dl, dh                                 ; 08 f2                       ; 0xc0c91 vgabios.c:468
    shr dh, 1                                 ; d0 ee                       ; 0xc0c93 vgabios.c:469
    shr cx, 002h                              ; c1 e9 02                    ; 0xc0c95 vgabios.c:470
    inc bx                                    ; 43                          ; 0xc0c98 vgabios.c:471
    jmp short 00c88h                          ; eb ed                       ; 0xc0c99
    mov al, dl                                ; 88 d0                       ; 0xc0c9b vgabios.c:473
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0c9d
    pop cx                                    ; 59                          ; 0xc0ca0
    pop bx                                    ; 5b                          ; 0xc0ca1
    pop bp                                    ; 5d                          ; 0xc0ca2
    retn                                      ; c3                          ; 0xc0ca3
  ; disGetNextSymbol 0xc0ca4 LB 0x361f -> off=0x0 cb=0000000000000084 uValue=00000000000c0ca4 'vga_read_glyph_cga'
vga_read_glyph_cga:                          ; 0xc0ca4 LB 0x84
    push bp                                   ; 55                          ; 0xc0ca4 vgabios.c:475
    mov bp, sp                                ; 89 e5                       ; 0xc0ca5
    push cx                                   ; 51                          ; 0xc0ca7
    push si                                   ; 56                          ; 0xc0ca8
    push di                                   ; 57                          ; 0xc0ca9
    push ax                                   ; 50                          ; 0xc0caa
    mov si, dx                                ; 89 d6                       ; 0xc0cab
    cmp bl, 006h                              ; 80 fb 06                    ; 0xc0cad vgabios.c:483
    je short 00cech                           ; 74 3a                       ; 0xc0cb0
    mov bx, ax                                ; 89 c3                       ; 0xc0cb2 vgabios.c:485
    add bx, ax                                ; 01 c3                       ; 0xc0cb4
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0cb6
    xor cx, cx                                ; 31 c9                       ; 0xc0cbb vgabios.c:487
    jmp short 00cc4h                          ; eb 05                       ; 0xc0cbd
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0cbf
    jnl short 00d20h                          ; 7d 5c                       ; 0xc0cc2
    mov ax, bx                                ; 89 d8                       ; 0xc0cc4 vgabios.c:488
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0cc6
    call 00c6fh                               ; e8 a3 ff                    ; 0xc0cc9
    mov di, si                                ; 89 f7                       ; 0xc0ccc
    inc si                                    ; 46                          ; 0xc0cce
    push SS                                   ; 16                          ; 0xc0ccf
    pop ES                                    ; 07                          ; 0xc0cd0
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0cd1
    lea ax, [bx+02000h]                       ; 8d 87 00 20                 ; 0xc0cd4 vgabios.c:489
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0cd8
    call 00c6fh                               ; e8 91 ff                    ; 0xc0cdb
    mov di, si                                ; 89 f7                       ; 0xc0cde
    inc si                                    ; 46                          ; 0xc0ce0
    push SS                                   ; 16                          ; 0xc0ce1
    pop ES                                    ; 07                          ; 0xc0ce2
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0ce3
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0ce6 vgabios.c:490
    inc cx                                    ; 41                          ; 0xc0ce9 vgabios.c:491
    jmp short 00cbfh                          ; eb d3                       ; 0xc0cea
    mov bx, ax                                ; 89 c3                       ; 0xc0cec vgabios.c:493
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0cee
    xor cx, cx                                ; 31 c9                       ; 0xc0cf3 vgabios.c:494
    jmp short 00cfch                          ; eb 05                       ; 0xc0cf5
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0cf7
    jnl short 00d20h                          ; 7d 24                       ; 0xc0cfa
    mov di, si                                ; 89 f7                       ; 0xc0cfc vgabios.c:495
    inc si                                    ; 46                          ; 0xc0cfe
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0cff
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0d02
    push SS                                   ; 16                          ; 0xc0d05
    pop ES                                    ; 07                          ; 0xc0d06
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d07
    mov di, si                                ; 89 f7                       ; 0xc0d0a vgabios.c:496
    inc si                                    ; 46                          ; 0xc0d0c
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d0d
    mov al, byte [es:bx+02000h]               ; 26 8a 87 00 20              ; 0xc0d10
    push SS                                   ; 16                          ; 0xc0d15
    pop ES                                    ; 07                          ; 0xc0d16
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d17
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d1a vgabios.c:497
    inc cx                                    ; 41                          ; 0xc0d1d vgabios.c:498
    jmp short 00cf7h                          ; eb d7                       ; 0xc0d1e
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc0d20 vgabios.c:500
    pop di                                    ; 5f                          ; 0xc0d23
    pop si                                    ; 5e                          ; 0xc0d24
    pop cx                                    ; 59                          ; 0xc0d25
    pop bp                                    ; 5d                          ; 0xc0d26
    retn                                      ; c3                          ; 0xc0d27
  ; disGetNextSymbol 0xc0d28 LB 0x359b -> off=0x0 cb=0000000000000011 uValue=00000000000c0d28 'vga_char_ofs_cga'
vga_char_ofs_cga:                            ; 0xc0d28 LB 0x11
    push bp                                   ; 55                          ; 0xc0d28 vgabios.c:502
    mov bp, sp                                ; 89 e5                       ; 0xc0d29
    xor dh, dh                                ; 30 f6                       ; 0xc0d2b vgabios.c:507
    imul dx, bx                               ; 0f af d3                    ; 0xc0d2d
    sal dx, 002h                              ; c1 e2 02                    ; 0xc0d30
    xor ah, ah                                ; 30 e4                       ; 0xc0d33
    add ax, dx                                ; 01 d0                       ; 0xc0d35
    pop bp                                    ; 5d                          ; 0xc0d37 vgabios.c:508
    retn                                      ; c3                          ; 0xc0d38
  ; disGetNextSymbol 0xc0d39 LB 0x358a -> off=0x0 cb=0000000000000065 uValue=00000000000c0d39 'vga_read_char_cga'
vga_read_char_cga:                           ; 0xc0d39 LB 0x65
    push bp                                   ; 55                          ; 0xc0d39 vgabios.c:510
    mov bp, sp                                ; 89 e5                       ; 0xc0d3a
    push bx                                   ; 53                          ; 0xc0d3c
    push cx                                   ; 51                          ; 0xc0d3d
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc0d3e
    movzx bx, dl                              ; 0f b6 da                    ; 0xc0d41 vgabios.c:516
    lea dx, [bp-00eh]                         ; 8d 56 f2                    ; 0xc0d44
    call 00ca4h                               ; e8 5a ff                    ; 0xc0d47
    push strict byte 00008h                   ; 6a 08                       ; 0xc0d4a vgabios.c:519
    push 00080h                               ; 68 80 00                    ; 0xc0d4c
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0d4f vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d52
    mov es, ax                                ; 8e c0                       ; 0xc0d54
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0d56
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0d59
    xor cx, cx                                ; 31 c9                       ; 0xc0d5d vgabios.c:68
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0d5f
    call 00ac0h                               ; e8 5b fd                    ; 0xc0d62
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0d65
    test ah, 080h                             ; f6 c4 80                    ; 0xc0d68 vgabios.c:521
    jne short 00d94h                          ; 75 27                       ; 0xc0d6b
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0d6d vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d70
    mov es, ax                                ; 8e c0                       ; 0xc0d72
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0d74
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0d77
    test dx, dx                               ; 85 d2                       ; 0xc0d7b vgabios.c:525
    jne short 00d83h                          ; 75 04                       ; 0xc0d7d
    test ax, ax                               ; 85 c0                       ; 0xc0d7f
    je short 00d94h                           ; 74 11                       ; 0xc0d81
    push strict byte 00008h                   ; 6a 08                       ; 0xc0d83 vgabios.c:526
    push 00080h                               ; 68 80 00                    ; 0xc0d85
    mov cx, 00080h                            ; b9 80 00                    ; 0xc0d88
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0d8b
    call 00ac0h                               ; e8 2f fd                    ; 0xc0d8e
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0d91
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc0d94 vgabios.c:529
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0d97
    pop cx                                    ; 59                          ; 0xc0d9a
    pop bx                                    ; 5b                          ; 0xc0d9b
    pop bp                                    ; 5d                          ; 0xc0d9c
    retn                                      ; c3                          ; 0xc0d9d
  ; disGetNextSymbol 0xc0d9e LB 0x3525 -> off=0x0 cb=0000000000000127 uValue=00000000000c0d9e 'vga_read_char_attr'
vga_read_char_attr:                          ; 0xc0d9e LB 0x127
    push bp                                   ; 55                          ; 0xc0d9e vgabios.c:531
    mov bp, sp                                ; 89 e5                       ; 0xc0d9f
    push bx                                   ; 53                          ; 0xc0da1
    push cx                                   ; 51                          ; 0xc0da2
    push si                                   ; 56                          ; 0xc0da3
    push di                                   ; 57                          ; 0xc0da4
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0da5
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0da8
    mov si, dx                                ; 89 d6                       ; 0xc0dab
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0dad vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0db0
    mov es, ax                                ; 8e c0                       ; 0xc0db3
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0db5
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc0db8 vgabios.c:48
    xor ah, ah                                ; 30 e4                       ; 0xc0dbb vgabios.c:539
    call 035f7h                               ; e8 37 28                    ; 0xc0dbd
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc0dc0
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0dc3 vgabios.c:540
    je near 00ebch                            ; 0f 84 f3 00                 ; 0xc0dc5
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc0dc9 vgabios.c:544
    lea bx, [bp-018h]                         ; 8d 5e e8                    ; 0xc0dcd
    lea dx, [bp-01ah]                         ; 8d 56 e6                    ; 0xc0dd0
    mov ax, cx                                ; 89 c8                       ; 0xc0dd3
    call 00a93h                               ; e8 bb fc                    ; 0xc0dd5
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc0dd8 vgabios.c:545
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc0ddb
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc0dde vgabios.c:546
    xor al, al                                ; 30 c0                       ; 0xc0de1
    shr ax, 008h                              ; c1 e8 08                    ; 0xc0de3
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc0de6
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0de9 vgabios.c:47
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0dec
    mov es, dx                                ; 8e c2                       ; 0xc0def
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc0df1
    xor dh, dh                                ; 30 f6                       ; 0xc0df4 vgabios.c:48
    inc dx                                    ; 42                          ; 0xc0df6
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc0df7 vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0dfa
    mov word [bp-014h], di                    ; 89 7e ec                    ; 0xc0dfd vgabios.c:58
    movzx bx, byte [bp-012h]                  ; 0f b6 5e ee                 ; 0xc0e00 vgabios.c:552
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0e04
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc0e07
    jne short 00e44h                          ; 75 36                       ; 0xc0e0c
    imul dx, di                               ; 0f af d7                    ; 0xc0e0e vgabios.c:554
    add dx, dx                                ; 01 d2                       ; 0xc0e11
    or dl, 0ffh                               ; 80 ca ff                    ; 0xc0e13
    mov word [bp-016h], dx                    ; 89 56 ea                    ; 0xc0e16
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc0e19
    mov cx, word [bp-016h]                    ; 8b 4e ea                    ; 0xc0e1d
    inc cx                                    ; 41                          ; 0xc0e20
    imul dx, cx                               ; 0f af d1                    ; 0xc0e21
    xor ah, ah                                ; 30 e4                       ; 0xc0e24
    imul di, ax                               ; 0f af f8                    ; 0xc0e26
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0e29
    add ax, di                                ; 01 f8                       ; 0xc0e2d
    add ax, ax                                ; 01 c0                       ; 0xc0e2f
    mov di, dx                                ; 89 d7                       ; 0xc0e31
    add di, ax                                ; 01 c7                       ; 0xc0e33
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc0e35 vgabios.c:55
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc0e39
    push SS                                   ; 16                          ; 0xc0e3c vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0e3d
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0e3e
    jmp near 00ebch                           ; e9 78 00                    ; 0xc0e41 vgabios.c:556
    mov bl, byte [bx+047aeh]                  ; 8a 9f ae 47                 ; 0xc0e44 vgabios.c:557
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc0e48
    je short 00e98h                           ; 74 4b                       ; 0xc0e4b
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc0e4d
    jc short 00ebch                           ; 72 6a                       ; 0xc0e50
    jbe short 00e5bh                          ; 76 07                       ; 0xc0e52
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc0e54
    jbe short 00e74h                          ; 76 1b                       ; 0xc0e57
    jmp short 00ebch                          ; eb 61                       ; 0xc0e59
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc0e5b vgabios.c:560
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0e5f
    mov bx, word [bp-014h]                    ; 8b 5e ec                    ; 0xc0e63
    call 00d28h                               ; e8 bf fe                    ; 0xc0e66
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc0e69 vgabios.c:561
    call 00d39h                               ; e8 c9 fe                    ; 0xc0e6d
    xor ah, ah                                ; 30 e4                       ; 0xc0e70
    jmp short 00e3ch                          ; eb c8                       ; 0xc0e72
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0e74 vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0e77
    xor dh, dh                                ; 30 f6                       ; 0xc0e7a vgabios.c:566
    mov word [bp-016h], dx                    ; 89 56 ea                    ; 0xc0e7c
    push dx                                   ; 52                          ; 0xc0e7f
    movzx dx, al                              ; 0f b6 d0                    ; 0xc0e80
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0e83
    mov bx, di                                ; 89 fb                       ; 0xc0e87
    call 00b63h                               ; e8 d7 fc                    ; 0xc0e89
    mov bx, word [bp-016h]                    ; 8b 5e ea                    ; 0xc0e8c vgabios.c:567
    mov dx, ax                                ; 89 c2                       ; 0xc0e8f
    mov ax, di                                ; 89 f8                       ; 0xc0e91
    call 00b8dh                               ; e8 f7 fc                    ; 0xc0e93
    jmp short 00e70h                          ; eb d8                       ; 0xc0e96
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0e98 vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0e9b
    xor dh, dh                                ; 30 f6                       ; 0xc0e9e vgabios.c:571
    mov word [bp-016h], dx                    ; 89 56 ea                    ; 0xc0ea0
    push dx                                   ; 52                          ; 0xc0ea3
    movzx dx, al                              ; 0f b6 d0                    ; 0xc0ea4
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0ea7
    mov bx, di                                ; 89 fb                       ; 0xc0eab
    call 00bcbh                               ; e8 1b fd                    ; 0xc0ead
    mov bx, word [bp-016h]                    ; 8b 5e ea                    ; 0xc0eb0 vgabios.c:572
    mov dx, ax                                ; 89 c2                       ; 0xc0eb3
    mov ax, di                                ; 89 f8                       ; 0xc0eb5
    call 00c30h                               ; e8 76 fd                    ; 0xc0eb7
    jmp short 00e70h                          ; eb b4                       ; 0xc0eba
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc0ebc vgabios.c:581
    pop di                                    ; 5f                          ; 0xc0ebf
    pop si                                    ; 5e                          ; 0xc0ec0
    pop cx                                    ; 59                          ; 0xc0ec1
    pop bx                                    ; 5b                          ; 0xc0ec2
    pop bp                                    ; 5d                          ; 0xc0ec3
    retn                                      ; c3                          ; 0xc0ec4
  ; disGetNextSymbol 0xc0ec5 LB 0x33fe -> off=0x10 cb=0000000000000083 uValue=00000000000c0ed5 'vga_get_font_info'
    db  0ech, 00eh, 031h, 00fh, 036h, 00fh, 03dh, 00fh, 042h, 00fh, 047h, 00fh, 04ch, 00fh, 051h, 00fh
vga_get_font_info:                           ; 0xc0ed5 LB 0x83
    push si                                   ; 56                          ; 0xc0ed5 vgabios.c:583
    push di                                   ; 57                          ; 0xc0ed6
    push bp                                   ; 55                          ; 0xc0ed7
    mov bp, sp                                ; 89 e5                       ; 0xc0ed8
    mov di, dx                                ; 89 d7                       ; 0xc0eda
    mov si, bx                                ; 89 de                       ; 0xc0edc
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc0ede vgabios.c:588
    jnbe short 00f2bh                         ; 77 48                       ; 0xc0ee1
    mov bx, ax                                ; 89 c3                       ; 0xc0ee3
    add bx, ax                                ; 01 c3                       ; 0xc0ee5
    jmp word [cs:bx+00ec5h]                   ; 2e ff a7 c5 0e              ; 0xc0ee7
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0eec vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0eef
    mov es, ax                                ; 8e c0                       ; 0xc0ef1
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0ef3
    mov ax, word [es:bx+002h]                 ; 26 8b 47 02                 ; 0xc0ef6
    push SS                                   ; 16                          ; 0xc0efa vgabios.c:591
    pop ES                                    ; 07                          ; 0xc0efb
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc0efc
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc0eff
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0f02
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f05
    mov es, ax                                ; 8e c0                       ; 0xc0f08
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f0a
    xor ah, ah                                ; 30 e4                       ; 0xc0f0d
    push SS                                   ; 16                          ; 0xc0f0f
    pop ES                                    ; 07                          ; 0xc0f10
    mov bx, cx                                ; 89 cb                       ; 0xc0f11
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f13
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0f16
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f19
    mov es, ax                                ; 8e c0                       ; 0xc0f1c
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f1e
    xor ah, ah                                ; 30 e4                       ; 0xc0f21
    push SS                                   ; 16                          ; 0xc0f23
    pop ES                                    ; 07                          ; 0xc0f24
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc0f25
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f28
    pop bp                                    ; 5d                          ; 0xc0f2b
    pop di                                    ; 5f                          ; 0xc0f2c
    pop si                                    ; 5e                          ; 0xc0f2d
    retn 00002h                               ; c2 02 00                    ; 0xc0f2e
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0f31 vgabios.c:67
    jmp short 00eefh                          ; eb b9                       ; 0xc0f34
    mov dx, 05d6ah                            ; ba 6a 5d                    ; 0xc0f36 vgabios.c:596
    mov ax, ds                                ; 8c d8                       ; 0xc0f39
    jmp short 00efah                          ; eb bd                       ; 0xc0f3b vgabios.c:597
    mov dx, 0556ah                            ; ba 6a 55                    ; 0xc0f3d vgabios.c:599
    jmp short 00f39h                          ; eb f7                       ; 0xc0f40
    mov dx, 0596ah                            ; ba 6a 59                    ; 0xc0f42 vgabios.c:602
    jmp short 00f39h                          ; eb f2                       ; 0xc0f45
    mov dx, 07b6ah                            ; ba 6a 7b                    ; 0xc0f47 vgabios.c:605
    jmp short 00f39h                          ; eb ed                       ; 0xc0f4a
    mov dx, 06b6ah                            ; ba 6a 6b                    ; 0xc0f4c vgabios.c:608
    jmp short 00f39h                          ; eb e8                       ; 0xc0f4f
    mov dx, 07c97h                            ; ba 97 7c                    ; 0xc0f51 vgabios.c:611
    jmp short 00f39h                          ; eb e3                       ; 0xc0f54
    jmp short 00f2bh                          ; eb d3                       ; 0xc0f56 vgabios.c:617
  ; disGetNextSymbol 0xc0f58 LB 0x336b -> off=0x0 cb=0000000000000156 uValue=00000000000c0f58 'vga_read_pixel'
vga_read_pixel:                              ; 0xc0f58 LB 0x156
    push bp                                   ; 55                          ; 0xc0f58 vgabios.c:630
    mov bp, sp                                ; 89 e5                       ; 0xc0f59
    push si                                   ; 56                          ; 0xc0f5b
    push di                                   ; 57                          ; 0xc0f5c
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc0f5d
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0f60
    mov word [bp-00ch], bx                    ; 89 5e f4                    ; 0xc0f63
    mov si, cx                                ; 89 ce                       ; 0xc0f66
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0f68 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f6b
    mov es, ax                                ; 8e c0                       ; 0xc0f6e
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f70
    xor ah, ah                                ; 30 e4                       ; 0xc0f73 vgabios.c:637
    call 035f7h                               ; e8 7f 26                    ; 0xc0f75
    mov ah, al                                ; 88 c4                       ; 0xc0f78
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0f7a vgabios.c:638
    je near 010a7h                            ; 0f 84 27 01                 ; 0xc0f7c
    movzx bx, al                              ; 0f b6 d8                    ; 0xc0f80 vgabios.c:640
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0f83
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc0f86
    je near 010a7h                            ; 0f 84 18 01                 ; 0xc0f8b
    mov ch, byte [bx+047aeh]                  ; 8a af ae 47                 ; 0xc0f8f vgabios.c:644
    cmp ch, 003h                              ; 80 fd 03                    ; 0xc0f93
    jc short 00fa9h                           ; 72 11                       ; 0xc0f96
    jbe short 00fb1h                          ; 76 17                       ; 0xc0f98
    cmp ch, 005h                              ; 80 fd 05                    ; 0xc0f9a
    je near 01080h                            ; 0f 84 df 00                 ; 0xc0f9d
    cmp ch, 004h                              ; 80 fd 04                    ; 0xc0fa1
    je short 00fb1h                           ; 74 0b                       ; 0xc0fa4
    jmp near 010a0h                           ; e9 f7 00                    ; 0xc0fa6
    cmp ch, 002h                              ; 80 fd 02                    ; 0xc0fa9
    je short 0101ch                           ; 74 6e                       ; 0xc0fac
    jmp near 010a0h                           ; e9 ef 00                    ; 0xc0fae
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc0fb1 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0fb4
    mov es, ax                                ; 8e c0                       ; 0xc0fb7
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0fb9
    imul ax, word [bp-00ch]                   ; 0f af 46 f4                 ; 0xc0fbc vgabios.c:58
    mov bx, dx                                ; 89 d3                       ; 0xc0fc0
    shr bx, 003h                              ; c1 eb 03                    ; 0xc0fc2
    add bx, ax                                ; 01 c3                       ; 0xc0fc5
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc0fc7 vgabios.c:57
    mov cx, word [es:di]                      ; 26 8b 0d                    ; 0xc0fca
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc0fcd vgabios.c:58
    imul ax, cx                               ; 0f af c1                    ; 0xc0fd1
    add bx, ax                                ; 01 c3                       ; 0xc0fd4
    mov cl, dl                                ; 88 d1                       ; 0xc0fd6 vgabios.c:649
    and cl, 007h                              ; 80 e1 07                    ; 0xc0fd8
    mov ax, 00080h                            ; b8 80 00                    ; 0xc0fdb
    sar ax, CL                                ; d3 f8                       ; 0xc0fde
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc0fe0
    xor ch, ch                                ; 30 ed                       ; 0xc0fe3 vgabios.c:650
    mov byte [bp-006h], ch                    ; 88 6e fa                    ; 0xc0fe5 vgabios.c:651
    jmp short 00ff2h                          ; eb 08                       ; 0xc0fe8
    cmp byte [bp-006h], 004h                  ; 80 7e fa 04                 ; 0xc0fea
    jnc near 010a2h                           ; 0f 83 b0 00                 ; 0xc0fee
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc0ff2 vgabios.c:652
    sal ax, 008h                              ; c1 e0 08                    ; 0xc0ff6
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc0ff9
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0ffb
    out DX, ax                                ; ef                          ; 0xc0ffe
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc0fff vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc1002
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1004
    and al, byte [bp-008h]                    ; 22 46 f8                    ; 0xc1007 vgabios.c:48
    test al, al                               ; 84 c0                       ; 0xc100a vgabios.c:654
    jbe short 01017h                          ; 76 09                       ; 0xc100c
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc100e vgabios.c:655
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc1011
    sal al, CL                                ; d2 e0                       ; 0xc1013
    or ch, al                                 ; 08 c5                       ; 0xc1015
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1017 vgabios.c:656
    jmp short 00feah                          ; eb ce                       ; 0xc101a
    movzx cx, byte [bx+047afh]                ; 0f b6 8f af 47              ; 0xc101c vgabios.c:659
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc1021
    sub bx, cx                                ; 29 cb                       ; 0xc1024
    mov cx, bx                                ; 89 d9                       ; 0xc1026
    mov bx, dx                                ; 89 d3                       ; 0xc1028
    shr bx, CL                                ; d3 eb                       ; 0xc102a
    mov cx, bx                                ; 89 d9                       ; 0xc102c
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc102e
    shr bx, 1                                 ; d1 eb                       ; 0xc1031
    imul bx, bx, strict byte 00050h           ; 6b db 50                    ; 0xc1033
    add bx, cx                                ; 01 cb                       ; 0xc1036
    test byte [bp-00ch], 001h                 ; f6 46 f4 01                 ; 0xc1038 vgabios.c:660
    je short 01041h                           ; 74 03                       ; 0xc103c
    add bh, 020h                              ; 80 c7 20                    ; 0xc103e vgabios.c:661
    mov cx, 0b800h                            ; b9 00 b8                    ; 0xc1041 vgabios.c:47
    mov es, cx                                ; 8e c1                       ; 0xc1044
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1046
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc1049 vgabios.c:663
    sal bx, 003h                              ; c1 e3 03                    ; 0xc104c
    cmp byte [bx+047afh], 002h                ; 80 bf af 47 02              ; 0xc104f
    jne short 0106bh                          ; 75 15                       ; 0xc1054
    and dx, strict byte 00003h                ; 83 e2 03                    ; 0xc1056 vgabios.c:664
    mov cx, strict word 00003h                ; b9 03 00                    ; 0xc1059
    sub cx, dx                                ; 29 d1                       ; 0xc105c
    add cx, cx                                ; 01 c9                       ; 0xc105e
    xor ah, ah                                ; 30 e4                       ; 0xc1060
    sar ax, CL                                ; d3 f8                       ; 0xc1062
    mov ch, al                                ; 88 c5                       ; 0xc1064
    and ch, 003h                              ; 80 e5 03                    ; 0xc1066
    jmp short 010a2h                          ; eb 37                       ; 0xc1069 vgabios.c:665
    xor dh, dh                                ; 30 f6                       ; 0xc106b vgabios.c:666
    and dl, 007h                              ; 80 e2 07                    ; 0xc106d
    mov cx, strict word 00007h                ; b9 07 00                    ; 0xc1070
    sub cx, dx                                ; 29 d1                       ; 0xc1073
    xor ah, ah                                ; 30 e4                       ; 0xc1075
    sar ax, CL                                ; d3 f8                       ; 0xc1077
    mov ch, al                                ; 88 c5                       ; 0xc1079
    and ch, 001h                              ; 80 e5 01                    ; 0xc107b
    jmp short 010a2h                          ; eb 22                       ; 0xc107e vgabios.c:667
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1080 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1083
    mov es, ax                                ; 8e c0                       ; 0xc1086
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc1088
    sal ax, 003h                              ; c1 e0 03                    ; 0xc108b vgabios.c:58
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc108e
    imul bx, ax                               ; 0f af d8                    ; 0xc1091
    add bx, dx                                ; 01 d3                       ; 0xc1094
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc1096 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc1099
    mov ch, byte [es:bx]                      ; 26 8a 2f                    ; 0xc109b
    jmp short 010a2h                          ; eb 02                       ; 0xc109e vgabios.c:671
    xor ch, ch                                ; 30 ed                       ; 0xc10a0 vgabios.c:676
    push SS                                   ; 16                          ; 0xc10a2 vgabios.c:678
    pop ES                                    ; 07                          ; 0xc10a3
    mov byte [es:si], ch                      ; 26 88 2c                    ; 0xc10a4
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc10a7 vgabios.c:679
    pop di                                    ; 5f                          ; 0xc10aa
    pop si                                    ; 5e                          ; 0xc10ab
    pop bp                                    ; 5d                          ; 0xc10ac
    retn                                      ; c3                          ; 0xc10ad
  ; disGetNextSymbol 0xc10ae LB 0x3215 -> off=0x0 cb=000000000000008c uValue=00000000000c10ae 'biosfn_perform_gray_scale_summing'
biosfn_perform_gray_scale_summing:           ; 0xc10ae LB 0x8c
    push bp                                   ; 55                          ; 0xc10ae vgabios.c:684
    mov bp, sp                                ; 89 e5                       ; 0xc10af
    push bx                                   ; 53                          ; 0xc10b1
    push cx                                   ; 51                          ; 0xc10b2
    push si                                   ; 56                          ; 0xc10b3
    push di                                   ; 57                          ; 0xc10b4
    push ax                                   ; 50                          ; 0xc10b5
    push ax                                   ; 50                          ; 0xc10b6
    mov bx, ax                                ; 89 c3                       ; 0xc10b7
    mov di, dx                                ; 89 d7                       ; 0xc10b9
    mov dx, 003dah                            ; ba da 03                    ; 0xc10bb vgabios.c:689
    in AL, DX                                 ; ec                          ; 0xc10be
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10bf
    xor al, al                                ; 30 c0                       ; 0xc10c1 vgabios.c:690
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc10c3
    out DX, AL                                ; ee                          ; 0xc10c6
    xor si, si                                ; 31 f6                       ; 0xc10c7 vgabios.c:692
    cmp si, di                                ; 39 fe                       ; 0xc10c9
    jnc short 0111fh                          ; 73 52                       ; 0xc10cb
    mov al, bl                                ; 88 d8                       ; 0xc10cd vgabios.c:695
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc10cf
    out DX, AL                                ; ee                          ; 0xc10d2
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc10d3 vgabios.c:697
    in AL, DX                                 ; ec                          ; 0xc10d6
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10d7
    mov cx, ax                                ; 89 c1                       ; 0xc10d9
    in AL, DX                                 ; ec                          ; 0xc10db vgabios.c:698
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10dc
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc10de
    in AL, DX                                 ; ec                          ; 0xc10e1 vgabios.c:699
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10e2
    xor ch, ch                                ; 30 ed                       ; 0xc10e4 vgabios.c:702
    imul cx, cx, strict byte 0004dh           ; 6b c9 4d                    ; 0xc10e6
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc10e9
    movzx cx, byte [bp-00ch]                  ; 0f b6 4e f4                 ; 0xc10ec
    imul cx, cx, 00097h                       ; 69 c9 97 00                 ; 0xc10f0
    add cx, word [bp-00ah]                    ; 03 4e f6                    ; 0xc10f4
    xor ah, ah                                ; 30 e4                       ; 0xc10f7
    imul ax, ax, strict byte 0001ch           ; 6b c0 1c                    ; 0xc10f9
    add cx, ax                                ; 01 c1                       ; 0xc10fc
    add cx, 00080h                            ; 81 c1 80 00                 ; 0xc10fe
    sar cx, 008h                              ; c1 f9 08                    ; 0xc1102
    cmp cx, strict byte 0003fh                ; 83 f9 3f                    ; 0xc1105 vgabios.c:704
    jbe short 0110dh                          ; 76 03                       ; 0xc1108
    mov cx, strict word 0003fh                ; b9 3f 00                    ; 0xc110a
    mov al, bl                                ; 88 d8                       ; 0xc110d vgabios.c:707
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc110f
    out DX, AL                                ; ee                          ; 0xc1112
    mov al, cl                                ; 88 c8                       ; 0xc1113 vgabios.c:709
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1115
    out DX, AL                                ; ee                          ; 0xc1118
    out DX, AL                                ; ee                          ; 0xc1119 vgabios.c:710
    out DX, AL                                ; ee                          ; 0xc111a vgabios.c:711
    inc bx                                    ; 43                          ; 0xc111b vgabios.c:712
    inc si                                    ; 46                          ; 0xc111c vgabios.c:713
    jmp short 010c9h                          ; eb aa                       ; 0xc111d
    mov dx, 003dah                            ; ba da 03                    ; 0xc111f vgabios.c:714
    in AL, DX                                 ; ec                          ; 0xc1122
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1123
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc1125 vgabios.c:715
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1127
    out DX, AL                                ; ee                          ; 0xc112a
    mov dx, 003dah                            ; ba da 03                    ; 0xc112b vgabios.c:717
    in AL, DX                                 ; ec                          ; 0xc112e
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc112f
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1131 vgabios.c:719
    pop di                                    ; 5f                          ; 0xc1134
    pop si                                    ; 5e                          ; 0xc1135
    pop cx                                    ; 59                          ; 0xc1136
    pop bx                                    ; 5b                          ; 0xc1137
    pop bp                                    ; 5d                          ; 0xc1138
    retn                                      ; c3                          ; 0xc1139
  ; disGetNextSymbol 0xc113a LB 0x3189 -> off=0x0 cb=00000000000000f6 uValue=00000000000c113a 'biosfn_set_cursor_shape'
biosfn_set_cursor_shape:                     ; 0xc113a LB 0xf6
    push bp                                   ; 55                          ; 0xc113a vgabios.c:722
    mov bp, sp                                ; 89 e5                       ; 0xc113b
    push bx                                   ; 53                          ; 0xc113d
    push cx                                   ; 51                          ; 0xc113e
    push si                                   ; 56                          ; 0xc113f
    push di                                   ; 57                          ; 0xc1140
    push ax                                   ; 50                          ; 0xc1141
    mov bl, al                                ; 88 c3                       ; 0xc1142
    mov ah, dl                                ; 88 d4                       ; 0xc1144
    movzx cx, al                              ; 0f b6 c8                    ; 0xc1146 vgabios.c:728
    sal cx, 008h                              ; c1 e1 08                    ; 0xc1149
    movzx dx, ah                              ; 0f b6 d4                    ; 0xc114c
    add dx, cx                                ; 01 ca                       ; 0xc114f
    mov si, strict word 00060h                ; be 60 00                    ; 0xc1151 vgabios.c:62
    mov cx, strict word 00040h                ; b9 40 00                    ; 0xc1154
    mov es, cx                                ; 8e c1                       ; 0xc1157
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc1159
    mov si, 00087h                            ; be 87 00                    ; 0xc115c vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc115f
    test dl, 008h                             ; f6 c2 08                    ; 0xc1162 vgabios.c:48
    jne near 01205h                           ; 0f 85 9c 00                 ; 0xc1165
    mov dl, al                                ; 88 c2                       ; 0xc1169 vgabios.c:734
    and dl, 060h                              ; 80 e2 60                    ; 0xc116b
    cmp dl, 020h                              ; 80 fa 20                    ; 0xc116e
    jne short 0117ah                          ; 75 07                       ; 0xc1171
    mov BL, strict byte 01eh                  ; b3 1e                       ; 0xc1173 vgabios.c:736
    xor ah, ah                                ; 30 e4                       ; 0xc1175 vgabios.c:737
    jmp near 01205h                           ; e9 8b 00                    ; 0xc1177 vgabios.c:738
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc117a vgabios.c:47
    test dl, 001h                             ; f6 c2 01                    ; 0xc117d vgabios.c:48
    jne near 01205h                           ; 0f 85 81 00                 ; 0xc1180
    cmp bl, 020h                              ; 80 fb 20                    ; 0xc1184
    jnc near 01205h                           ; 0f 83 7a 00                 ; 0xc1187
    cmp ah, 020h                              ; 80 fc 20                    ; 0xc118b
    jnc near 01205h                           ; 0f 83 73 00                 ; 0xc118e
    mov si, 00085h                            ; be 85 00                    ; 0xc1192 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc1195
    mov es, dx                                ; 8e c2                       ; 0xc1198
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc119a
    mov dx, cx                                ; 89 ca                       ; 0xc119d vgabios.c:58
    cmp ah, bl                                ; 38 dc                       ; 0xc119f vgabios.c:749
    jnc short 011afh                          ; 73 0c                       ; 0xc11a1
    test ah, ah                               ; 84 e4                       ; 0xc11a3 vgabios.c:751
    je short 01205h                           ; 74 5e                       ; 0xc11a5
    xor bl, bl                                ; 30 db                       ; 0xc11a7 vgabios.c:752
    mov ah, cl                                ; 88 cc                       ; 0xc11a9 vgabios.c:753
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc11ab
    jmp short 01205h                          ; eb 56                       ; 0xc11ad vgabios.c:755
    movzx si, ah                              ; 0f b6 f4                    ; 0xc11af vgabios.c:756
    mov word [bp-00ah], si                    ; 89 76 f6                    ; 0xc11b2
    movzx si, bl                              ; 0f b6 f3                    ; 0xc11b5
    or si, word [bp-00ah]                     ; 0b 76 f6                    ; 0xc11b8
    cmp si, cx                                ; 39 ce                       ; 0xc11bb
    jnc short 011d2h                          ; 73 13                       ; 0xc11bd
    movzx di, ah                              ; 0f b6 fc                    ; 0xc11bf
    mov si, cx                                ; 89 ce                       ; 0xc11c2
    dec si                                    ; 4e                          ; 0xc11c4
    cmp di, si                                ; 39 f7                       ; 0xc11c5
    je short 01205h                           ; 74 3c                       ; 0xc11c7
    movzx si, bl                              ; 0f b6 f3                    ; 0xc11c9
    dec cx                                    ; 49                          ; 0xc11cc
    dec cx                                    ; 49                          ; 0xc11cd
    cmp si, cx                                ; 39 ce                       ; 0xc11ce
    je short 01205h                           ; 74 33                       ; 0xc11d0
    cmp ah, 003h                              ; 80 fc 03                    ; 0xc11d2 vgabios.c:758
    jbe short 01205h                          ; 76 2e                       ; 0xc11d5
    movzx si, bl                              ; 0f b6 f3                    ; 0xc11d7 vgabios.c:759
    movzx di, ah                              ; 0f b6 fc                    ; 0xc11da
    inc si                                    ; 46                          ; 0xc11dd
    inc si                                    ; 46                          ; 0xc11de
    mov cl, dl                                ; 88 d1                       ; 0xc11df
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc11e1
    cmp di, si                                ; 39 f7                       ; 0xc11e3
    jnle short 011fah                         ; 7f 13                       ; 0xc11e5
    sub bl, ah                                ; 28 e3                       ; 0xc11e7 vgabios.c:761
    add bl, dl                                ; 00 d3                       ; 0xc11e9
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc11eb
    mov ah, cl                                ; 88 cc                       ; 0xc11ed vgabios.c:762
    cmp dx, strict byte 0000eh                ; 83 fa 0e                    ; 0xc11ef vgabios.c:763
    jc short 01205h                           ; 72 11                       ; 0xc11f2
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc11f4 vgabios.c:765
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc11f6 vgabios.c:766
    jmp short 01205h                          ; eb 0b                       ; 0xc11f8 vgabios.c:768
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc11fa
    jbe short 01203h                          ; 76 04                       ; 0xc11fd
    shr dx, 1                                 ; d1 ea                       ; 0xc11ff vgabios.c:770
    mov bl, dl                                ; 88 d3                       ; 0xc1201
    mov ah, cl                                ; 88 cc                       ; 0xc1203 vgabios.c:774
    mov si, strict word 00063h                ; be 63 00                    ; 0xc1205 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc1208
    mov es, dx                                ; 8e c2                       ; 0xc120b
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc120d
    mov AL, strict byte 00ah                  ; b0 0a                       ; 0xc1210 vgabios.c:785
    mov dx, cx                                ; 89 ca                       ; 0xc1212
    out DX, AL                                ; ee                          ; 0xc1214
    mov si, cx                                ; 89 ce                       ; 0xc1215 vgabios.c:786
    inc si                                    ; 46                          ; 0xc1217
    mov al, bl                                ; 88 d8                       ; 0xc1218
    mov dx, si                                ; 89 f2                       ; 0xc121a
    out DX, AL                                ; ee                          ; 0xc121c
    mov AL, strict byte 00bh                  ; b0 0b                       ; 0xc121d vgabios.c:787
    mov dx, cx                                ; 89 ca                       ; 0xc121f
    out DX, AL                                ; ee                          ; 0xc1221
    mov al, ah                                ; 88 e0                       ; 0xc1222 vgabios.c:788
    mov dx, si                                ; 89 f2                       ; 0xc1224
    out DX, AL                                ; ee                          ; 0xc1226
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1227 vgabios.c:789
    pop di                                    ; 5f                          ; 0xc122a
    pop si                                    ; 5e                          ; 0xc122b
    pop cx                                    ; 59                          ; 0xc122c
    pop bx                                    ; 5b                          ; 0xc122d
    pop bp                                    ; 5d                          ; 0xc122e
    retn                                      ; c3                          ; 0xc122f
  ; disGetNextSymbol 0xc1230 LB 0x3093 -> off=0x0 cb=0000000000000089 uValue=00000000000c1230 'biosfn_set_cursor_pos'
biosfn_set_cursor_pos:                       ; 0xc1230 LB 0x89
    push bp                                   ; 55                          ; 0xc1230 vgabios.c:792
    mov bp, sp                                ; 89 e5                       ; 0xc1231
    push bx                                   ; 53                          ; 0xc1233
    push cx                                   ; 51                          ; 0xc1234
    push si                                   ; 56                          ; 0xc1235
    push ax                                   ; 50                          ; 0xc1236
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc1237 vgabios.c:798
    jnbe short 012b1h                         ; 77 76                       ; 0xc1239
    movzx bx, al                              ; 0f b6 d8                    ; 0xc123b vgabios.c:801
    add bx, bx                                ; 01 db                       ; 0xc123e
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc1240
    mov cx, strict word 00040h                ; b9 40 00                    ; 0xc1243 vgabios.c:62
    mov es, cx                                ; 8e c1                       ; 0xc1246
    mov word [es:bx], dx                      ; 26 89 17                    ; 0xc1248
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc124b vgabios.c:47
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc124e
    cmp al, ah                                ; 38 e0                       ; 0xc1251 vgabios.c:805
    jne short 012b1h                          ; 75 5c                       ; 0xc1253
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1255 vgabios.c:57
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc1258
    mov bx, 00084h                            ; bb 84 00                    ; 0xc125b vgabios.c:47
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc125e
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc1261 vgabios.c:48
    inc bx                                    ; 43                          ; 0xc1264
    mov si, dx                                ; 89 d6                       ; 0xc1265 vgabios.c:811
    and si, 0ff00h                            ; 81 e6 00 ff                 ; 0xc1267
    shr si, 008h                              ; c1 ee 08                    ; 0xc126b
    mov word [bp-008h], si                    ; 89 76 f8                    ; 0xc126e
    imul bx, cx                               ; 0f af d9                    ; 0xc1271 vgabios.c:814
    or bl, 0ffh                               ; 80 cb ff                    ; 0xc1274
    xor ah, ah                                ; 30 e4                       ; 0xc1277
    inc bx                                    ; 43                          ; 0xc1279
    imul ax, bx                               ; 0f af c3                    ; 0xc127a
    movzx si, dl                              ; 0f b6 f2                    ; 0xc127d
    add si, ax                                ; 01 c6                       ; 0xc1280
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1282
    imul ax, cx                               ; 0f af c1                    ; 0xc1286
    add si, ax                                ; 01 c6                       ; 0xc1289
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc128b vgabios.c:57
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc128e
    mov AL, strict byte 00eh                  ; b0 0e                       ; 0xc1291 vgabios.c:818
    mov dx, bx                                ; 89 da                       ; 0xc1293
    out DX, AL                                ; ee                          ; 0xc1295
    mov ax, si                                ; 89 f0                       ; 0xc1296 vgabios.c:819
    xor al, al                                ; 30 c0                       ; 0xc1298
    shr ax, 008h                              ; c1 e8 08                    ; 0xc129a
    lea cx, [bx+001h]                         ; 8d 4f 01                    ; 0xc129d
    mov dx, cx                                ; 89 ca                       ; 0xc12a0
    out DX, AL                                ; ee                          ; 0xc12a2
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc12a3 vgabios.c:820
    mov dx, bx                                ; 89 da                       ; 0xc12a5
    out DX, AL                                ; ee                          ; 0xc12a7
    and si, 000ffh                            ; 81 e6 ff 00                 ; 0xc12a8 vgabios.c:821
    mov ax, si                                ; 89 f0                       ; 0xc12ac
    mov dx, cx                                ; 89 ca                       ; 0xc12ae
    out DX, AL                                ; ee                          ; 0xc12b0
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc12b1 vgabios.c:823
    pop si                                    ; 5e                          ; 0xc12b4
    pop cx                                    ; 59                          ; 0xc12b5
    pop bx                                    ; 5b                          ; 0xc12b6
    pop bp                                    ; 5d                          ; 0xc12b7
    retn                                      ; c3                          ; 0xc12b8
  ; disGetNextSymbol 0xc12b9 LB 0x300a -> off=0x0 cb=00000000000000cd uValue=00000000000c12b9 'biosfn_set_active_page'
biosfn_set_active_page:                      ; 0xc12b9 LB 0xcd
    push bp                                   ; 55                          ; 0xc12b9 vgabios.c:826
    mov bp, sp                                ; 89 e5                       ; 0xc12ba
    push bx                                   ; 53                          ; 0xc12bc
    push cx                                   ; 51                          ; 0xc12bd
    push dx                                   ; 52                          ; 0xc12be
    push si                                   ; 56                          ; 0xc12bf
    push di                                   ; 57                          ; 0xc12c0
    push ax                                   ; 50                          ; 0xc12c1
    push ax                                   ; 50                          ; 0xc12c2
    mov cl, al                                ; 88 c1                       ; 0xc12c3
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc12c5 vgabios.c:832
    jnbe near 0137ch                          ; 0f 87 b1 00                 ; 0xc12c7
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc12cb vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc12ce
    mov es, ax                                ; 8e c0                       ; 0xc12d1
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc12d3
    xor ah, ah                                ; 30 e4                       ; 0xc12d6 vgabios.c:836
    call 035f7h                               ; e8 1c 23                    ; 0xc12d8
    mov ch, al                                ; 88 c5                       ; 0xc12db
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc12dd vgabios.c:837
    je near 0137ch                            ; 0f 84 99 00                 ; 0xc12df
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc12e3 vgabios.c:840
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc12e6
    lea dx, [bp-00ch]                         ; 8d 56 f4                    ; 0xc12e9
    call 00a93h                               ; e8 a4 f7                    ; 0xc12ec
    movzx bx, ch                              ; 0f b6 dd                    ; 0xc12ef vgabios.c:842
    mov si, bx                                ; 89 de                       ; 0xc12f2
    sal si, 003h                              ; c1 e6 03                    ; 0xc12f4
    cmp byte [si+047adh], 000h                ; 80 bc ad 47 00              ; 0xc12f7
    jne short 01332h                          ; 75 34                       ; 0xc12fc
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc12fe vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1301
    mov es, ax                                ; 8e c0                       ; 0xc1304
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc1306
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1309 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc130c
    xor ah, ah                                ; 30 e4                       ; 0xc130f vgabios.c:48
    inc ax                                    ; 40                          ; 0xc1311
    imul dx, ax                               ; 0f af d0                    ; 0xc1312 vgabios.c:849
    mov ax, dx                                ; 89 d0                       ; 0xc1315
    add ax, dx                                ; 01 d0                       ; 0xc1317
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc1319
    mov bx, ax                                ; 89 c3                       ; 0xc131b
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc131d
    inc bx                                    ; 43                          ; 0xc1320
    imul bx, ax                               ; 0f af d8                    ; 0xc1321
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc1324 vgabios.c:62
    mov word [es:si], bx                      ; 26 89 1c                    ; 0xc1327
    or dl, 0ffh                               ; 80 ca ff                    ; 0xc132a vgabios.c:853
    mov bx, dx                                ; 89 d3                       ; 0xc132d
    inc bx                                    ; 43                          ; 0xc132f
    jmp short 01341h                          ; eb 0f                       ; 0xc1330 vgabios.c:855
    movzx bx, byte [bx+0482ch]                ; 0f b6 9f 2c 48              ; 0xc1332 vgabios.c:857
    sal bx, 006h                              ; c1 e3 06                    ; 0xc1337
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc133a
    mov bx, word [bx+04843h]                  ; 8b 9f 43 48                 ; 0xc133d
    imul bx, ax                               ; 0f af d8                    ; 0xc1341
    mov si, strict word 00063h                ; be 63 00                    ; 0xc1344 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1347
    mov es, ax                                ; 8e c0                       ; 0xc134a
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc134c
    mov AL, strict byte 00ch                  ; b0 0c                       ; 0xc134f vgabios.c:862
    mov dx, si                                ; 89 f2                       ; 0xc1351
    out DX, AL                                ; ee                          ; 0xc1353
    mov ax, bx                                ; 89 d8                       ; 0xc1354 vgabios.c:863
    xor al, bl                                ; 30 d8                       ; 0xc1356
    shr ax, 008h                              ; c1 e8 08                    ; 0xc1358
    lea di, [si+001h]                         ; 8d 7c 01                    ; 0xc135b
    mov dx, di                                ; 89 fa                       ; 0xc135e
    out DX, AL                                ; ee                          ; 0xc1360
    mov AL, strict byte 00dh                  ; b0 0d                       ; 0xc1361 vgabios.c:864
    mov dx, si                                ; 89 f2                       ; 0xc1363
    out DX, AL                                ; ee                          ; 0xc1365
    xor bh, bh                                ; 30 ff                       ; 0xc1366 vgabios.c:865
    mov ax, bx                                ; 89 d8                       ; 0xc1368
    mov dx, di                                ; 89 fa                       ; 0xc136a
    out DX, AL                                ; ee                          ; 0xc136c
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc136d vgabios.c:52
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc1370
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc1373 vgabios.c:875
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc1376
    call 01230h                               ; e8 b4 fe                    ; 0xc1379
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc137c vgabios.c:876
    pop di                                    ; 5f                          ; 0xc137f
    pop si                                    ; 5e                          ; 0xc1380
    pop dx                                    ; 5a                          ; 0xc1381
    pop cx                                    ; 59                          ; 0xc1382
    pop bx                                    ; 5b                          ; 0xc1383
    pop bp                                    ; 5d                          ; 0xc1384
    retn                                      ; c3                          ; 0xc1385
  ; disGetNextSymbol 0xc1386 LB 0x2f3d -> off=0x0 cb=0000000000000045 uValue=00000000000c1386 'find_vpti'
find_vpti:                                   ; 0xc1386 LB 0x45
    push bx                                   ; 53                          ; 0xc1386 vgabios.c:911
    push si                                   ; 56                          ; 0xc1387
    push bp                                   ; 55                          ; 0xc1388
    mov bp, sp                                ; 89 e5                       ; 0xc1389
    movzx bx, al                              ; 0f b6 d8                    ; 0xc138b vgabios.c:916
    mov si, bx                                ; 89 de                       ; 0xc138e
    sal si, 003h                              ; c1 e6 03                    ; 0xc1390
    cmp byte [si+047adh], 000h                ; 80 bc ad 47 00              ; 0xc1393
    jne short 013c2h                          ; 75 28                       ; 0xc1398
    mov si, 00089h                            ; be 89 00                    ; 0xc139a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc139d
    mov es, ax                                ; 8e c0                       ; 0xc13a0
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc13a2
    test AL, strict byte 010h                 ; a8 10                       ; 0xc13a5 vgabios.c:918
    je short 013b0h                           ; 74 07                       ; 0xc13a7
    movsx ax, byte [bx+07df3h]                ; 0f be 87 f3 7d              ; 0xc13a9 vgabios.c:919
    jmp short 013c7h                          ; eb 17                       ; 0xc13ae vgabios.c:920
    test AL, strict byte 080h                 ; a8 80                       ; 0xc13b0
    je short 013bbh                           ; 74 07                       ; 0xc13b2
    movsx ax, byte [bx+07de3h]                ; 0f be 87 e3 7d              ; 0xc13b4 vgabios.c:921
    jmp short 013c7h                          ; eb 0c                       ; 0xc13b9 vgabios.c:922
    movsx ax, byte [bx+07debh]                ; 0f be 87 eb 7d              ; 0xc13bb vgabios.c:923
    jmp short 013c7h                          ; eb 05                       ; 0xc13c0 vgabios.c:924
    movzx ax, byte [bx+0482ch]                ; 0f b6 87 2c 48              ; 0xc13c2 vgabios.c:925
    pop bp                                    ; 5d                          ; 0xc13c7 vgabios.c:928
    pop si                                    ; 5e                          ; 0xc13c8
    pop bx                                    ; 5b                          ; 0xc13c9
    retn                                      ; c3                          ; 0xc13ca
  ; disGetNextSymbol 0xc13cb LB 0x2ef8 -> off=0x0 cb=00000000000004bb uValue=00000000000c13cb 'biosfn_set_video_mode'
biosfn_set_video_mode:                       ; 0xc13cb LB 0x4bb
    push bp                                   ; 55                          ; 0xc13cb vgabios.c:933
    mov bp, sp                                ; 89 e5                       ; 0xc13cc
    push bx                                   ; 53                          ; 0xc13ce
    push cx                                   ; 51                          ; 0xc13cf
    push dx                                   ; 52                          ; 0xc13d0
    push si                                   ; 56                          ; 0xc13d1
    push di                                   ; 57                          ; 0xc13d2
    sub sp, strict byte 00016h                ; 83 ec 16                    ; 0xc13d3
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc13d6
    and AL, strict byte 080h                  ; 24 80                       ; 0xc13d9 vgabios.c:937
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc13db
    call 007bfh                               ; e8 de f3                    ; 0xc13de vgabios.c:947
    test ax, ax                               ; 85 c0                       ; 0xc13e1
    je short 013f1h                           ; 74 0c                       ; 0xc13e3
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc13e5 vgabios.c:949
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc13e7
    out DX, AL                                ; ee                          ; 0xc13ea
    xor al, al                                ; 30 c0                       ; 0xc13eb vgabios.c:950
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc13ed
    out DX, AL                                ; ee                          ; 0xc13f0
    and byte [bp-010h], 07fh                  ; 80 66 f0 7f                 ; 0xc13f1 vgabios.c:955
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc13f5 vgabios.c:961
    call 035f7h                               ; e8 fb 21                    ; 0xc13f9
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc13fc
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc13ff vgabios.c:967
    je near 0187ch                            ; 0f 84 77 04                 ; 0xc1401
    mov bx, 000a8h                            ; bb a8 00                    ; 0xc1405 vgabios.c:67
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc1408
    mov es, dx                                ; 8e c2                       ; 0xc140b
    mov di, word [es:bx]                      ; 26 8b 3f                    ; 0xc140d
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc1410
    mov bx, di                                ; 89 fb                       ; 0xc1414 vgabios.c:68
    mov word [bp-01ch], dx                    ; 89 56 e4                    ; 0xc1416
    movzx cx, al                              ; 0f b6 c8                    ; 0xc1419 vgabios.c:973
    mov ax, cx                                ; 89 c8                       ; 0xc141c
    call 01386h                               ; e8 65 ff                    ; 0xc141e
    mov es, dx                                ; 8e c2                       ; 0xc1421 vgabios.c:974
    mov si, word [es:di]                      ; 26 8b 35                    ; 0xc1423
    mov dx, word [es:di+002h]                 ; 26 8b 55 02                 ; 0xc1426
    mov word [bp-012h], dx                    ; 89 56 ee                    ; 0xc142a
    xor ah, ah                                ; 30 e4                       ; 0xc142d vgabios.c:975
    sal ax, 006h                              ; c1 e0 06                    ; 0xc142f
    add si, ax                                ; 01 c6                       ; 0xc1432
    mov di, 00089h                            ; bf 89 00                    ; 0xc1434 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1437
    mov es, ax                                ; 8e c0                       ; 0xc143a
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc143c
    mov ah, al                                ; 88 c4                       ; 0xc143f vgabios.c:48
    test AL, strict byte 008h                 ; a8 08                       ; 0xc1441 vgabios.c:992
    jne near 014f7h                           ; 0f 85 b0 00                 ; 0xc1443
    mov di, cx                                ; 89 cf                       ; 0xc1447 vgabios.c:994
    sal di, 003h                              ; c1 e7 03                    ; 0xc1449
    mov al, byte [di+047b2h]                  ; 8a 85 b2 47                 ; 0xc144c
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc1450
    out DX, AL                                ; ee                          ; 0xc1453
    xor al, al                                ; 30 c0                       ; 0xc1454 vgabios.c:997
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc1456
    out DX, AL                                ; ee                          ; 0xc1459
    mov cl, byte [di+047b3h]                  ; 8a 8d b3 47                 ; 0xc145a vgabios.c:1000
    cmp cl, 001h                              ; 80 f9 01                    ; 0xc145e
    jc short 01471h                           ; 72 0e                       ; 0xc1461
    jbe short 0147ch                          ; 76 17                       ; 0xc1463
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc1465
    je short 0148ah                           ; 74 20                       ; 0xc1468
    cmp cl, 002h                              ; 80 f9 02                    ; 0xc146a
    je short 01483h                           ; 74 14                       ; 0xc146d
    jmp short 0148fh                          ; eb 1e                       ; 0xc146f
    test cl, cl                               ; 84 c9                       ; 0xc1471
    jne short 0148fh                          ; 75 1a                       ; 0xc1473
    mov word [bp-016h], 04fc0h                ; c7 46 ea c0 4f              ; 0xc1475 vgabios.c:1002
    jmp short 0148fh                          ; eb 13                       ; 0xc147a vgabios.c:1003
    mov word [bp-016h], 05080h                ; c7 46 ea 80 50              ; 0xc147c vgabios.c:1005
    jmp short 0148fh                          ; eb 0c                       ; 0xc1481 vgabios.c:1006
    mov word [bp-016h], 05140h                ; c7 46 ea 40 51              ; 0xc1483 vgabios.c:1008
    jmp short 0148fh                          ; eb 05                       ; 0xc1488 vgabios.c:1009
    mov word [bp-016h], 05200h                ; c7 46 ea 00 52              ; 0xc148a vgabios.c:1011
    movzx di, byte [bp-00eh]                  ; 0f b6 7e f2                 ; 0xc148f vgabios.c:1015
    sal di, 003h                              ; c1 e7 03                    ; 0xc1493
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc1496
    jne short 014ach                          ; 75 0f                       ; 0xc149b
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc149d vgabios.c:1017
    cmp byte [es:si+002h], 008h               ; 26 80 7c 02 08              ; 0xc14a0
    jne short 014ach                          ; 75 05                       ; 0xc14a5
    mov word [bp-016h], 05080h                ; c7 46 ea 80 50              ; 0xc14a7 vgabios.c:1018
    xor cx, cx                                ; 31 c9                       ; 0xc14ac vgabios.c:1021
    jmp short 014bfh                          ; eb 0f                       ; 0xc14ae
    xor al, al                                ; 30 c0                       ; 0xc14b0 vgabios.c:1028
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc14b2
    out DX, AL                                ; ee                          ; 0xc14b5
    out DX, AL                                ; ee                          ; 0xc14b6 vgabios.c:1029
    out DX, AL                                ; ee                          ; 0xc14b7 vgabios.c:1030
    inc cx                                    ; 41                          ; 0xc14b8 vgabios.c:1032
    cmp cx, 00100h                            ; 81 f9 00 01                 ; 0xc14b9
    jnc short 014eah                          ; 73 2b                       ; 0xc14bd
    movzx di, byte [bp-00eh]                  ; 0f b6 7e f2                 ; 0xc14bf
    sal di, 003h                              ; c1 e7 03                    ; 0xc14c3
    movzx di, byte [di+047b3h]                ; 0f b6 bd b3 47              ; 0xc14c6
    movzx dx, byte [di+0483ch]                ; 0f b6 95 3c 48              ; 0xc14cb
    cmp cx, dx                                ; 39 d1                       ; 0xc14d0
    jnbe short 014b0h                         ; 77 dc                       ; 0xc14d2
    imul di, cx, strict byte 00003h           ; 6b f9 03                    ; 0xc14d4
    add di, word [bp-016h]                    ; 03 7e ea                    ; 0xc14d7
    mov al, byte [di]                         ; 8a 05                       ; 0xc14da
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc14dc
    out DX, AL                                ; ee                          ; 0xc14df
    mov al, byte [di+001h]                    ; 8a 45 01                    ; 0xc14e0
    out DX, AL                                ; ee                          ; 0xc14e3
    mov al, byte [di+002h]                    ; 8a 45 02                    ; 0xc14e4
    out DX, AL                                ; ee                          ; 0xc14e7
    jmp short 014b8h                          ; eb ce                       ; 0xc14e8
    test ah, 002h                             ; f6 c4 02                    ; 0xc14ea vgabios.c:1033
    je short 014f7h                           ; 74 08                       ; 0xc14ed
    mov dx, 00100h                            ; ba 00 01                    ; 0xc14ef vgabios.c:1035
    xor ax, ax                                ; 31 c0                       ; 0xc14f2
    call 010aeh                               ; e8 b7 fb                    ; 0xc14f4
    mov dx, 003dah                            ; ba da 03                    ; 0xc14f7 vgabios.c:1040
    in AL, DX                                 ; ec                          ; 0xc14fa
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc14fb
    xor cx, cx                                ; 31 c9                       ; 0xc14fd vgabios.c:1043
    jmp short 01506h                          ; eb 05                       ; 0xc14ff
    cmp cx, strict byte 00013h                ; 83 f9 13                    ; 0xc1501
    jnbe short 0151bh                         ; 77 15                       ; 0xc1504
    mov al, cl                                ; 88 c8                       ; 0xc1506 vgabios.c:1044
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1508
    out DX, AL                                ; ee                          ; 0xc150b
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc150c vgabios.c:1045
    mov di, si                                ; 89 f7                       ; 0xc150f
    add di, cx                                ; 01 cf                       ; 0xc1511
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc1513
    out DX, AL                                ; ee                          ; 0xc1517
    inc cx                                    ; 41                          ; 0xc1518 vgabios.c:1046
    jmp short 01501h                          ; eb e6                       ; 0xc1519
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc151b vgabios.c:1047
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc151d
    out DX, AL                                ; ee                          ; 0xc1520
    xor al, al                                ; 30 c0                       ; 0xc1521 vgabios.c:1048
    out DX, AL                                ; ee                          ; 0xc1523
    mov es, [bp-01ch]                         ; 8e 46 e4                    ; 0xc1524 vgabios.c:1051
    mov ax, word [es:bx+004h]                 ; 26 8b 47 04                 ; 0xc1527
    mov dx, word [es:bx+006h]                 ; 26 8b 57 06                 ; 0xc152b
    test dx, dx                               ; 85 d2                       ; 0xc152f
    jne short 01537h                          ; 75 04                       ; 0xc1531
    test ax, ax                               ; 85 c0                       ; 0xc1533
    je short 0157ah                           ; 74 43                       ; 0xc1535
    mov dx, ax                                ; 89 c2                       ; 0xc1537 vgabios.c:1055
    mov ax, word [es:bx+006h]                 ; 26 8b 47 06                 ; 0xc1539
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc153d
    xor cx, cx                                ; 31 c9                       ; 0xc1540 vgabios.c:1056
    jmp short 01549h                          ; eb 05                       ; 0xc1542
    cmp cx, strict byte 00010h                ; 83 f9 10                    ; 0xc1544
    jnc short 0156ah                          ; 73 21                       ; 0xc1547
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1549 vgabios.c:1057
    mov di, si                                ; 89 f7                       ; 0xc154c
    add di, cx                                ; 01 cf                       ; 0xc154e
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc1550
    mov word [bp-01eh], ax                    ; 89 46 e2                    ; 0xc1553
    mov ax, dx                                ; 89 d0                       ; 0xc1556
    add ax, cx                                ; 01 c8                       ; 0xc1558
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc155a
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc155d
    les di, [bp-020h]                         ; c4 7e e0                    ; 0xc1561
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1564
    inc cx                                    ; 41                          ; 0xc1567
    jmp short 01544h                          ; eb da                       ; 0xc1568
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc156a vgabios.c:1058
    mov al, byte [es:si+034h]                 ; 26 8a 44 34                 ; 0xc156d
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1571
    mov di, dx                                ; 89 d7                       ; 0xc1574
    mov byte [es:di+010h], al                 ; 26 88 45 10                 ; 0xc1576
    xor al, al                                ; 30 c0                       ; 0xc157a vgabios.c:1062
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc157c
    out DX, AL                                ; ee                          ; 0xc157f
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc1580 vgabios.c:1063
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc1582
    out DX, AL                                ; ee                          ; 0xc1585
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc1586 vgabios.c:1064
    jmp short 01590h                          ; eb 05                       ; 0xc1589
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc158b
    jnbe short 015a8h                         ; 77 18                       ; 0xc158e
    mov al, cl                                ; 88 c8                       ; 0xc1590 vgabios.c:1065
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc1592
    out DX, AL                                ; ee                          ; 0xc1595
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1596 vgabios.c:1066
    mov di, si                                ; 89 f7                       ; 0xc1599
    add di, cx                                ; 01 cf                       ; 0xc159b
    mov al, byte [es:di+004h]                 ; 26 8a 45 04                 ; 0xc159d
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc15a1
    out DX, AL                                ; ee                          ; 0xc15a4
    inc cx                                    ; 41                          ; 0xc15a5 vgabios.c:1067
    jmp short 0158bh                          ; eb e3                       ; 0xc15a6
    xor cx, cx                                ; 31 c9                       ; 0xc15a8 vgabios.c:1070
    jmp short 015b1h                          ; eb 05                       ; 0xc15aa
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc15ac
    jnbe short 015c9h                         ; 77 18                       ; 0xc15af
    mov al, cl                                ; 88 c8                       ; 0xc15b1 vgabios.c:1071
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc15b3
    out DX, AL                                ; ee                          ; 0xc15b6
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc15b7 vgabios.c:1072
    mov di, si                                ; 89 f7                       ; 0xc15ba
    add di, cx                                ; 01 cf                       ; 0xc15bc
    mov al, byte [es:di+037h]                 ; 26 8a 45 37                 ; 0xc15be
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc15c2
    out DX, AL                                ; ee                          ; 0xc15c5
    inc cx                                    ; 41                          ; 0xc15c6 vgabios.c:1073
    jmp short 015ach                          ; eb e3                       ; 0xc15c7
    movzx di, byte [bp-00eh]                  ; 0f b6 7e f2                 ; 0xc15c9 vgabios.c:1076
    sal di, 003h                              ; c1 e7 03                    ; 0xc15cd
    cmp byte [di+047aeh], 001h                ; 80 bd ae 47 01              ; 0xc15d0
    jne short 015dch                          ; 75 05                       ; 0xc15d5
    mov cx, 003b4h                            ; b9 b4 03                    ; 0xc15d7
    jmp short 015dfh                          ; eb 03                       ; 0xc15da
    mov cx, 003d4h                            ; b9 d4 03                    ; 0xc15dc
    mov word [bp-018h], cx                    ; 89 4e e8                    ; 0xc15df
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc15e2 vgabios.c:1079
    mov al, byte [es:si+009h]                 ; 26 8a 44 09                 ; 0xc15e5
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc15e9
    out DX, AL                                ; ee                          ; 0xc15ec
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc15ed vgabios.c:1082
    mov dx, cx                                ; 89 ca                       ; 0xc15f0
    out DX, ax                                ; ef                          ; 0xc15f2
    xor cx, cx                                ; 31 c9                       ; 0xc15f3 vgabios.c:1084
    jmp short 015fch                          ; eb 05                       ; 0xc15f5
    cmp cx, strict byte 00018h                ; 83 f9 18                    ; 0xc15f7
    jnbe short 01612h                         ; 77 16                       ; 0xc15fa
    mov al, cl                                ; 88 c8                       ; 0xc15fc vgabios.c:1085
    mov dx, word [bp-018h]                    ; 8b 56 e8                    ; 0xc15fe
    out DX, AL                                ; ee                          ; 0xc1601
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1602 vgabios.c:1086
    mov di, si                                ; 89 f7                       ; 0xc1605
    add di, cx                                ; 01 cf                       ; 0xc1607
    inc dx                                    ; 42                          ; 0xc1609
    mov al, byte [es:di+00ah]                 ; 26 8a 45 0a                 ; 0xc160a
    out DX, AL                                ; ee                          ; 0xc160e
    inc cx                                    ; 41                          ; 0xc160f vgabios.c:1087
    jmp short 015f7h                          ; eb e5                       ; 0xc1610
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc1612 vgabios.c:1090
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1614
    out DX, AL                                ; ee                          ; 0xc1617
    mov dx, word [bp-018h]                    ; 8b 56 e8                    ; 0xc1618 vgabios.c:1091
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc161b
    in AL, DX                                 ; ec                          ; 0xc161e
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc161f
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1621 vgabios.c:1093
    jne short 01683h                          ; 75 5c                       ; 0xc1625
    movzx di, byte [bp-00eh]                  ; 0f b6 7e f2                 ; 0xc1627 vgabios.c:1095
    sal di, 003h                              ; c1 e7 03                    ; 0xc162b
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc162e
    jne short 01647h                          ; 75 12                       ; 0xc1633
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc1635 vgabios.c:1097
    mov cx, 04000h                            ; b9 00 40                    ; 0xc1639
    mov ax, 00720h                            ; b8 20 07                    ; 0xc163c
    xor di, di                                ; 31 ff                       ; 0xc163f
    jcxz 01645h                               ; e3 02                       ; 0xc1641
    rep stosw                                 ; f3 ab                       ; 0xc1643
    jmp short 01683h                          ; eb 3c                       ; 0xc1645 vgabios.c:1099
    cmp byte [bp-010h], 00dh                  ; 80 7e f0 0d                 ; 0xc1647 vgabios.c:1101
    jnc short 0165eh                          ; 73 11                       ; 0xc164b
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc164d vgabios.c:1103
    mov cx, 04000h                            ; b9 00 40                    ; 0xc1651
    xor ax, ax                                ; 31 c0                       ; 0xc1654
    xor di, di                                ; 31 ff                       ; 0xc1656
    jcxz 0165ch                               ; e3 02                       ; 0xc1658
    rep stosw                                 ; f3 ab                       ; 0xc165a
    jmp short 01683h                          ; eb 25                       ; 0xc165c vgabios.c:1105
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc165e vgabios.c:1107
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc1660
    out DX, AL                                ; ee                          ; 0xc1663
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc1664 vgabios.c:1108
    in AL, DX                                 ; ec                          ; 0xc1667
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1668
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc166a
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc166d vgabios.c:1109
    out DX, AL                                ; ee                          ; 0xc166f
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc1670 vgabios.c:1110
    mov cx, 08000h                            ; b9 00 80                    ; 0xc1674
    xor ax, ax                                ; 31 c0                       ; 0xc1677
    xor di, di                                ; 31 ff                       ; 0xc1679
    jcxz 0167fh                               ; e3 02                       ; 0xc167b
    rep stosw                                 ; f3 ab                       ; 0xc167d
    mov al, byte [bp-020h]                    ; 8a 46 e0                    ; 0xc167f vgabios.c:1111
    out DX, AL                                ; ee                          ; 0xc1682
    mov di, strict word 00049h                ; bf 49 00                    ; 0xc1683 vgabios.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1686
    mov es, ax                                ; 8e c0                       ; 0xc1689
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc168b
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc168e
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1691 vgabios.c:1118
    movzx ax, byte [es:si]                    ; 26 0f b6 04                 ; 0xc1694
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc1698 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc169b
    mov es, dx                                ; 8e c2                       ; 0xc169e
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16a0
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc16a3 vgabios.c:60
    mov ax, word [es:si+003h]                 ; 26 8b 44 03                 ; 0xc16a6
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc16aa vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc16ad
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16af
    mov di, strict word 00063h                ; bf 63 00                    ; 0xc16b2 vgabios.c:62
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc16b5
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16b8
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc16bb vgabios.c:50
    mov al, byte [es:si+001h]                 ; 26 8a 44 01                 ; 0xc16be
    mov di, 00084h                            ; bf 84 00                    ; 0xc16c2 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc16c5
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc16c7
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc16ca vgabios.c:1122
    movzx ax, byte [es:si+002h]               ; 26 0f b6 44 02              ; 0xc16cd
    mov di, 00085h                            ; bf 85 00                    ; 0xc16d2 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc16d5
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16d7
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc16da vgabios.c:1123
    movzx ax, byte [es:si+014h]               ; 26 0f b6 44 14              ; 0xc16dd
    sal ax, 008h                              ; c1 e0 08                    ; 0xc16e2
    movzx dx, byte [es:si+015h]               ; 26 0f b6 54 15              ; 0xc16e5
    or ax, dx                                 ; 09 d0                       ; 0xc16ea
    mov di, strict word 00060h                ; bf 60 00                    ; 0xc16ec vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc16ef
    mov es, dx                                ; 8e c2                       ; 0xc16f2
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16f4
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc16f7 vgabios.c:1124
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc16fa
    mov di, 00087h                            ; bf 87 00                    ; 0xc16fc vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc16ff
    mov di, 00088h                            ; bf 88 00                    ; 0xc1702 vgabios.c:52
    mov byte [es:di], 0f9h                    ; 26 c6 05 f9                 ; 0xc1705
    mov di, 0008ah                            ; bf 8a 00                    ; 0xc1709 vgabios.c:52
    mov byte [es:di], 008h                    ; 26 c6 05 08                 ; 0xc170c
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1710 vgabios.c:1130
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc1713
    jnbe short 0173dh                         ; 77 26                       ; 0xc1715
    movzx di, al                              ; 0f b6 f8                    ; 0xc1717 vgabios.c:1132
    mov al, byte [di+07ddbh]                  ; 8a 85 db 7d                 ; 0xc171a vgabios.c:50
    mov di, strict word 00065h                ; bf 65 00                    ; 0xc171e vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1721
    cmp byte [bp-010h], 006h                  ; 80 7e f0 06                 ; 0xc1724 vgabios.c:1133
    jne short 0172fh                          ; 75 05                       ; 0xc1728
    mov dx, strict word 0003fh                ; ba 3f 00                    ; 0xc172a
    jmp short 01732h                          ; eb 03                       ; 0xc172d
    mov dx, strict word 00030h                ; ba 30 00                    ; 0xc172f
    mov di, strict word 00066h                ; bf 66 00                    ; 0xc1732 vgabios.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1735
    mov es, ax                                ; 8e c0                       ; 0xc1738
    mov byte [es:di], dl                      ; 26 88 15                    ; 0xc173a
    xor cx, cx                                ; 31 c9                       ; 0xc173d vgabios.c:1138
    jmp short 01746h                          ; eb 05                       ; 0xc173f
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc1741
    jnc short 01751h                          ; 73 0b                       ; 0xc1744
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc1746 vgabios.c:1139
    xor dx, dx                                ; 31 d2                       ; 0xc1749
    call 01230h                               ; e8 e2 fa                    ; 0xc174b
    inc cx                                    ; 41                          ; 0xc174e
    jmp short 01741h                          ; eb f0                       ; 0xc174f
    xor ax, ax                                ; 31 c0                       ; 0xc1751 vgabios.c:1142
    call 012b9h                               ; e8 63 fb                    ; 0xc1753
    movzx di, byte [bp-00eh]                  ; 0f b6 7e f2                 ; 0xc1756 vgabios.c:1145
    sal di, 003h                              ; c1 e7 03                    ; 0xc175a
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc175d
    jne near 01847h                           ; 0f 85 e1 00                 ; 0xc1762
    mov es, [bp-01ch]                         ; 8e 46 e4                    ; 0xc1766 vgabios.c:1147
    mov di, word [es:bx+008h]                 ; 26 8b 7f 08                 ; 0xc1769
    mov ax, word [es:bx+00ah]                 ; 26 8b 47 0a                 ; 0xc176d
    mov word [bp-014h], ax                    ; 89 46 ec                    ; 0xc1771
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1774 vgabios.c:1149
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc1777
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc177b
    je short 0179fh                           ; 74 20                       ; 0xc177d
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc177f
    jne short 017c9h                          ; 75 46                       ; 0xc1781
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1783 vgabios.c:1151
    movzx ax, byte [es:si+002h]               ; 26 0f b6 44 02              ; 0xc1786
    push ax                                   ; 50                          ; 0xc178b
    push dword 000000000h                     ; 66 6a 00                    ; 0xc178c
    mov cx, 00100h                            ; b9 00 01                    ; 0xc178f
    mov bx, 0556ah                            ; bb 6a 55                    ; 0xc1792
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc1795
    xor ax, ax                                ; 31 c0                       ; 0xc1798
    call 02c14h                               ; e8 77 14                    ; 0xc179a
    jmp short 017ebh                          ; eb 4c                       ; 0xc179d vgabios.c:1152
    xor ah, ah                                ; 30 e4                       ; 0xc179f vgabios.c:1154
    push ax                                   ; 50                          ; 0xc17a1
    push dword 000000000h                     ; 66 6a 00                    ; 0xc17a2
    mov cx, 00100h                            ; b9 00 01                    ; 0xc17a5
    mov bx, 05d6ah                            ; bb 6a 5d                    ; 0xc17a8
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc17ab
    xor al, al                                ; 30 c0                       ; 0xc17ae
    call 02c14h                               ; e8 61 14                    ; 0xc17b0
    cmp byte [bp-010h], 007h                  ; 80 7e f0 07                 ; 0xc17b3 vgabios.c:1155
    jne short 017ebh                          ; 75 32                       ; 0xc17b7
    mov cx, strict word 0000eh                ; b9 0e 00                    ; 0xc17b9 vgabios.c:1156
    xor bx, bx                                ; 31 db                       ; 0xc17bc
    mov dx, 07b6ah                            ; ba 6a 7b                    ; 0xc17be
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc17c1
    call 02b9fh                               ; e8 d8 13                    ; 0xc17c4
    jmp short 017ebh                          ; eb 22                       ; 0xc17c7 vgabios.c:1157
    xor ah, ah                                ; 30 e4                       ; 0xc17c9 vgabios.c:1159
    push ax                                   ; 50                          ; 0xc17cb
    push dword 000000000h                     ; 66 6a 00                    ; 0xc17cc
    mov cx, 00100h                            ; b9 00 01                    ; 0xc17cf
    mov bx, 06b6ah                            ; bb 6a 6b                    ; 0xc17d2
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc17d5
    xor al, al                                ; 30 c0                       ; 0xc17d8
    call 02c14h                               ; e8 37 14                    ; 0xc17da
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc17dd vgabios.c:1160
    xor bx, bx                                ; 31 db                       ; 0xc17e0
    mov dx, 07c97h                            ; ba 97 7c                    ; 0xc17e2
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc17e5
    call 02b9fh                               ; e8 b4 13                    ; 0xc17e8
    cmp word [bp-014h], strict byte 00000h    ; 83 7e ec 00                 ; 0xc17eb vgabios.c:1162
    jne short 017f5h                          ; 75 04                       ; 0xc17ef
    test di, di                               ; 85 ff                       ; 0xc17f1
    je short 0183fh                           ; 74 4a                       ; 0xc17f3
    xor cx, cx                                ; 31 c9                       ; 0xc17f5 vgabios.c:1167
    mov es, [bp-014h]                         ; 8e 46 ec                    ; 0xc17f7 vgabios.c:1169
    mov bx, di                                ; 89 fb                       ; 0xc17fa
    add bx, cx                                ; 01 cb                       ; 0xc17fc
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc17fe
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1802
    je short 0180eh                           ; 74 08                       ; 0xc1804
    cmp al, byte [bp-010h]                    ; 3a 46 f0                    ; 0xc1806 vgabios.c:1171
    je short 0180eh                           ; 74 03                       ; 0xc1809
    inc cx                                    ; 41                          ; 0xc180b vgabios.c:1173
    jmp short 017f7h                          ; eb e9                       ; 0xc180c vgabios.c:1174
    mov es, [bp-014h]                         ; 8e 46 ec                    ; 0xc180e vgabios.c:1176
    mov bx, di                                ; 89 fb                       ; 0xc1811
    add bx, cx                                ; 01 cb                       ; 0xc1813
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc1815
    cmp al, byte [bp-010h]                    ; 3a 46 f0                    ; 0xc1819
    jne short 0183fh                          ; 75 21                       ; 0xc181c
    movzx ax, byte [es:di]                    ; 26 0f b6 05                 ; 0xc181e vgabios.c:1181
    push ax                                   ; 50                          ; 0xc1822
    movzx ax, byte [es:di+001h]               ; 26 0f b6 45 01              ; 0xc1823
    push ax                                   ; 50                          ; 0xc1828
    push word [es:di+004h]                    ; 26 ff 75 04                 ; 0xc1829
    mov cx, word [es:di+002h]                 ; 26 8b 4d 02                 ; 0xc182d
    mov bx, word [es:di+006h]                 ; 26 8b 5d 06                 ; 0xc1831
    mov dx, word [es:di+008h]                 ; 26 8b 55 08                 ; 0xc1835
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc1839
    call 02c14h                               ; e8 d5 13                    ; 0xc183c
    xor bl, bl                                ; 30 db                       ; 0xc183f vgabios.c:1185
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc1841
    mov AH, strict byte 011h                  ; b4 11                       ; 0xc1843
    int 06dh                                  ; cd 6d                       ; 0xc1845
    mov bx, 0596ah                            ; bb 6a 59                    ; 0xc1847 vgabios.c:1189
    mov cx, ds                                ; 8c d9                       ; 0xc184a
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc184c
    call 009f0h                               ; e8 9e f1                    ; 0xc184f
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1852 vgabios.c:1191
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc1855
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc1859
    je short 01877h                           ; 74 1a                       ; 0xc185b
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc185d
    je short 01872h                           ; 74 11                       ; 0xc185f
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc1861
    jne short 0187ch                          ; 75 17                       ; 0xc1863
    mov bx, 0556ah                            ; bb 6a 55                    ; 0xc1865 vgabios.c:1193
    mov cx, ds                                ; 8c d9                       ; 0xc1868
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc186a
    call 009f0h                               ; e8 80 f1                    ; 0xc186d
    jmp short 0187ch                          ; eb 0a                       ; 0xc1870 vgabios.c:1194
    mov bx, 05d6ah                            ; bb 6a 5d                    ; 0xc1872 vgabios.c:1196
    jmp short 01868h                          ; eb f1                       ; 0xc1875
    mov bx, 06b6ah                            ; bb 6a 6b                    ; 0xc1877 vgabios.c:1199
    jmp short 01868h                          ; eb ec                       ; 0xc187a
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc187c vgabios.c:1202
    pop di                                    ; 5f                          ; 0xc187f
    pop si                                    ; 5e                          ; 0xc1880
    pop dx                                    ; 5a                          ; 0xc1881
    pop cx                                    ; 59                          ; 0xc1882
    pop bx                                    ; 5b                          ; 0xc1883
    pop bp                                    ; 5d                          ; 0xc1884
    retn                                      ; c3                          ; 0xc1885
  ; disGetNextSymbol 0xc1886 LB 0x2a3d -> off=0x0 cb=0000000000000075 uValue=00000000000c1886 'vgamem_copy_pl4'
vgamem_copy_pl4:                             ; 0xc1886 LB 0x75
    push bp                                   ; 55                          ; 0xc1886 vgabios.c:1205
    mov bp, sp                                ; 89 e5                       ; 0xc1887
    push si                                   ; 56                          ; 0xc1889
    push di                                   ; 57                          ; 0xc188a
    push ax                                   ; 50                          ; 0xc188b
    push ax                                   ; 50                          ; 0xc188c
    mov bh, cl                                ; 88 cf                       ; 0xc188d
    movzx di, dl                              ; 0f b6 fa                    ; 0xc188f vgabios.c:1211
    movzx cx, byte [bp+006h]                  ; 0f b6 4e 06                 ; 0xc1892
    imul di, cx                               ; 0f af f9                    ; 0xc1896
    movzx si, byte [bp+004h]                  ; 0f b6 76 04                 ; 0xc1899
    imul di, si                               ; 0f af fe                    ; 0xc189d
    xor ah, ah                                ; 30 e4                       ; 0xc18a0
    add di, ax                                ; 01 c7                       ; 0xc18a2
    mov word [bp-008h], di                    ; 89 7e f8                    ; 0xc18a4
    movzx di, bl                              ; 0f b6 fb                    ; 0xc18a7 vgabios.c:1212
    imul cx, di                               ; 0f af cf                    ; 0xc18aa
    imul cx, si                               ; 0f af ce                    ; 0xc18ad
    add cx, ax                                ; 01 c1                       ; 0xc18b0
    mov word [bp-006h], cx                    ; 89 4e fa                    ; 0xc18b2
    mov ax, 00105h                            ; b8 05 01                    ; 0xc18b5 vgabios.c:1213
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc18b8
    out DX, ax                                ; ef                          ; 0xc18bb
    xor bl, bl                                ; 30 db                       ; 0xc18bc vgabios.c:1214
    cmp bl, byte [bp+006h]                    ; 3a 5e 06                    ; 0xc18be
    jnc short 018ebh                          ; 73 28                       ; 0xc18c1
    movzx cx, bh                              ; 0f b6 cf                    ; 0xc18c3 vgabios.c:1216
    movzx si, bl                              ; 0f b6 f3                    ; 0xc18c6
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc18c9
    imul ax, si                               ; 0f af c6                    ; 0xc18cd
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc18d0
    add si, ax                                ; 01 c6                       ; 0xc18d3
    mov di, word [bp-006h]                    ; 8b 7e fa                    ; 0xc18d5
    add di, ax                                ; 01 c7                       ; 0xc18d8
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc18da
    mov es, dx                                ; 8e c2                       ; 0xc18dd
    jcxz 018e7h                               ; e3 06                       ; 0xc18df
    push DS                                   ; 1e                          ; 0xc18e1
    mov ds, dx                                ; 8e da                       ; 0xc18e2
    rep movsb                                 ; f3 a4                       ; 0xc18e4
    pop DS                                    ; 1f                          ; 0xc18e6
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc18e7 vgabios.c:1217
    jmp short 018beh                          ; eb d3                       ; 0xc18e9
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc18eb vgabios.c:1218
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc18ee
    out DX, ax                                ; ef                          ; 0xc18f1
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc18f2 vgabios.c:1219
    pop di                                    ; 5f                          ; 0xc18f5
    pop si                                    ; 5e                          ; 0xc18f6
    pop bp                                    ; 5d                          ; 0xc18f7
    retn 00004h                               ; c2 04 00                    ; 0xc18f8
  ; disGetNextSymbol 0xc18fb LB 0x29c8 -> off=0x0 cb=0000000000000060 uValue=00000000000c18fb 'vgamem_fill_pl4'
vgamem_fill_pl4:                             ; 0xc18fb LB 0x60
    push bp                                   ; 55                          ; 0xc18fb vgabios.c:1222
    mov bp, sp                                ; 89 e5                       ; 0xc18fc
    push di                                   ; 57                          ; 0xc18fe
    push ax                                   ; 50                          ; 0xc18ff
    push ax                                   ; 50                          ; 0xc1900
    mov byte [bp-004h], bl                    ; 88 5e fc                    ; 0xc1901
    mov bh, cl                                ; 88 cf                       ; 0xc1904
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc1906 vgabios.c:1228
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc1909
    imul cx, dx                               ; 0f af ca                    ; 0xc190d
    movzx dx, bh                              ; 0f b6 d7                    ; 0xc1910
    imul dx, cx                               ; 0f af d1                    ; 0xc1913
    xor ah, ah                                ; 30 e4                       ; 0xc1916
    add dx, ax                                ; 01 c2                       ; 0xc1918
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc191a
    mov ax, 00205h                            ; b8 05 02                    ; 0xc191d vgabios.c:1229
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1920
    out DX, ax                                ; ef                          ; 0xc1923
    xor bl, bl                                ; 30 db                       ; 0xc1924 vgabios.c:1230
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc1926
    jnc short 0194ch                          ; 73 21                       ; 0xc1929
    movzx cx, byte [bp-004h]                  ; 0f b6 4e fc                 ; 0xc192b vgabios.c:1232
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc192f
    movzx dx, bl                              ; 0f b6 d3                    ; 0xc1933
    movzx di, bh                              ; 0f b6 ff                    ; 0xc1936
    imul di, dx                               ; 0f af fa                    ; 0xc1939
    add di, word [bp-006h]                    ; 03 7e fa                    ; 0xc193c
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc193f
    mov es, dx                                ; 8e c2                       ; 0xc1942
    jcxz 01948h                               ; e3 02                       ; 0xc1944
    rep stosb                                 ; f3 aa                       ; 0xc1946
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc1948 vgabios.c:1233
    jmp short 01926h                          ; eb da                       ; 0xc194a
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc194c vgabios.c:1234
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc194f
    out DX, ax                                ; ef                          ; 0xc1952
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc1953 vgabios.c:1235
    pop di                                    ; 5f                          ; 0xc1956
    pop bp                                    ; 5d                          ; 0xc1957
    retn 00004h                               ; c2 04 00                    ; 0xc1958
  ; disGetNextSymbol 0xc195b LB 0x2968 -> off=0x0 cb=00000000000000a3 uValue=00000000000c195b 'vgamem_copy_cga'
vgamem_copy_cga:                             ; 0xc195b LB 0xa3
    push bp                                   ; 55                          ; 0xc195b vgabios.c:1238
    mov bp, sp                                ; 89 e5                       ; 0xc195c
    push si                                   ; 56                          ; 0xc195e
    push di                                   ; 57                          ; 0xc195f
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1960
    mov dh, bl                                ; 88 de                       ; 0xc1963
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc1965
    movzx di, dl                              ; 0f b6 fa                    ; 0xc1968 vgabios.c:1244
    movzx si, byte [bp+006h]                  ; 0f b6 76 06                 ; 0xc196b
    imul di, si                               ; 0f af fe                    ; 0xc196f
    movzx bx, byte [bp+004h]                  ; 0f b6 5e 04                 ; 0xc1972
    imul di, bx                               ; 0f af fb                    ; 0xc1976
    sar di, 1                                 ; d1 ff                       ; 0xc1979
    xor ah, ah                                ; 30 e4                       ; 0xc197b
    add di, ax                                ; 01 c7                       ; 0xc197d
    mov word [bp-00ch], di                    ; 89 7e f4                    ; 0xc197f
    movzx dx, dh                              ; 0f b6 d6                    ; 0xc1982 vgabios.c:1245
    imul dx, si                               ; 0f af d6                    ; 0xc1985
    imul dx, bx                               ; 0f af d3                    ; 0xc1988
    sar dx, 1                                 ; d1 fa                       ; 0xc198b
    add dx, ax                                ; 01 c2                       ; 0xc198d
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc198f
    mov byte [bp-008h], ah                    ; 88 66 f8                    ; 0xc1992 vgabios.c:1246
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc1995
    cwd                                       ; 99                          ; 0xc1999
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc199a
    sar ax, 1                                 ; d1 f8                       ; 0xc199c
    movzx bx, byte [bp-008h]                  ; 0f b6 5e f8                 ; 0xc199e
    cmp bx, ax                                ; 39 c3                       ; 0xc19a2
    jnl short 019f5h                          ; 7d 4f                       ; 0xc19a4
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc19a6 vgabios.c:1248
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc19aa
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc19ad
    imul bx, ax                               ; 0f af d8                    ; 0xc19b1
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc19b4
    add si, bx                                ; 01 de                       ; 0xc19b7
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc19b9
    add di, bx                                ; 01 df                       ; 0xc19bc
    mov cx, word [bp-00eh]                    ; 8b 4e f2                    ; 0xc19be
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc19c1
    mov es, dx                                ; 8e c2                       ; 0xc19c4
    jcxz 019ceh                               ; e3 06                       ; 0xc19c6
    push DS                                   ; 1e                          ; 0xc19c8
    mov ds, dx                                ; 8e da                       ; 0xc19c9
    rep movsb                                 ; f3 a4                       ; 0xc19cb
    pop DS                                    ; 1f                          ; 0xc19cd
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc19ce vgabios.c:1249
    add si, 02000h                            ; 81 c6 00 20                 ; 0xc19d1
    add si, bx                                ; 01 de                       ; 0xc19d5
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc19d7
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc19da
    add di, bx                                ; 01 df                       ; 0xc19de
    mov cx, word [bp-00eh]                    ; 8b 4e f2                    ; 0xc19e0
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc19e3
    mov es, dx                                ; 8e c2                       ; 0xc19e6
    jcxz 019f0h                               ; e3 06                       ; 0xc19e8
    push DS                                   ; 1e                          ; 0xc19ea
    mov ds, dx                                ; 8e da                       ; 0xc19eb
    rep movsb                                 ; f3 a4                       ; 0xc19ed
    pop DS                                    ; 1f                          ; 0xc19ef
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc19f0 vgabios.c:1250
    jmp short 01995h                          ; eb a0                       ; 0xc19f3
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc19f5 vgabios.c:1251
    pop di                                    ; 5f                          ; 0xc19f8
    pop si                                    ; 5e                          ; 0xc19f9
    pop bp                                    ; 5d                          ; 0xc19fa
    retn 00004h                               ; c2 04 00                    ; 0xc19fb
  ; disGetNextSymbol 0xc19fe LB 0x28c5 -> off=0x0 cb=0000000000000081 uValue=00000000000c19fe 'vgamem_fill_cga'
vgamem_fill_cga:                             ; 0xc19fe LB 0x81
    push bp                                   ; 55                          ; 0xc19fe vgabios.c:1254
    mov bp, sp                                ; 89 e5                       ; 0xc19ff
    push si                                   ; 56                          ; 0xc1a01
    push di                                   ; 57                          ; 0xc1a02
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1a03
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1a06
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1a09
    movzx bx, dl                              ; 0f b6 da                    ; 0xc1a0c vgabios.c:1260
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc1a0f
    imul bx, dx                               ; 0f af da                    ; 0xc1a13
    movzx dx, cl                              ; 0f b6 d1                    ; 0xc1a16
    imul dx, bx                               ; 0f af d3                    ; 0xc1a19
    sar dx, 1                                 ; d1 fa                       ; 0xc1a1c
    xor ah, ah                                ; 30 e4                       ; 0xc1a1e
    add dx, ax                                ; 01 c2                       ; 0xc1a20
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc1a22
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1a25 vgabios.c:1261
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1a28
    cwd                                       ; 99                          ; 0xc1a2c
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1a2d
    sar ax, 1                                 ; d1 f8                       ; 0xc1a2f
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc1a31
    cmp dx, ax                                ; 39 c2                       ; 0xc1a35
    jnl short 01a76h                          ; 7d 3d                       ; 0xc1a37
    movzx si, byte [bp-00ah]                  ; 0f b6 76 f6                 ; 0xc1a39 vgabios.c:1263
    movzx bx, byte [bp+006h]                  ; 0f b6 5e 06                 ; 0xc1a3d
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1a41
    imul dx, ax                               ; 0f af d0                    ; 0xc1a45
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1a48
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc1a4b
    add di, dx                                ; 01 d7                       ; 0xc1a4e
    mov cx, si                                ; 89 f1                       ; 0xc1a50
    mov ax, bx                                ; 89 d8                       ; 0xc1a52
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1a54
    mov es, dx                                ; 8e c2                       ; 0xc1a57
    jcxz 01a5dh                               ; e3 02                       ; 0xc1a59
    rep stosb                                 ; f3 aa                       ; 0xc1a5b
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc1a5d vgabios.c:1264
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1a60
    add di, word [bp-00eh]                    ; 03 7e f2                    ; 0xc1a64
    mov cx, si                                ; 89 f1                       ; 0xc1a67
    mov ax, bx                                ; 89 d8                       ; 0xc1a69
    mov es, dx                                ; 8e c2                       ; 0xc1a6b
    jcxz 01a71h                               ; e3 02                       ; 0xc1a6d
    rep stosb                                 ; f3 aa                       ; 0xc1a6f
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1a71 vgabios.c:1265
    jmp short 01a28h                          ; eb b2                       ; 0xc1a74
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1a76 vgabios.c:1266
    pop di                                    ; 5f                          ; 0xc1a79
    pop si                                    ; 5e                          ; 0xc1a7a
    pop bp                                    ; 5d                          ; 0xc1a7b
    retn 00004h                               ; c2 04 00                    ; 0xc1a7c
  ; disGetNextSymbol 0xc1a7f LB 0x2844 -> off=0x0 cb=0000000000000079 uValue=00000000000c1a7f 'vgamem_copy_linear'
vgamem_copy_linear:                          ; 0xc1a7f LB 0x79
    push bp                                   ; 55                          ; 0xc1a7f vgabios.c:1269
    mov bp, sp                                ; 89 e5                       ; 0xc1a80
    push si                                   ; 56                          ; 0xc1a82
    push di                                   ; 57                          ; 0xc1a83
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc1a84
    mov ah, al                                ; 88 c4                       ; 0xc1a87
    mov al, bl                                ; 88 d8                       ; 0xc1a89
    mov bx, cx                                ; 89 cb                       ; 0xc1a8b
    xor dh, dh                                ; 30 f6                       ; 0xc1a8d vgabios.c:1275
    movzx di, byte [bp+006h]                  ; 0f b6 7e 06                 ; 0xc1a8f
    imul dx, di                               ; 0f af d7                    ; 0xc1a93
    imul dx, word [bp+004h]                   ; 0f af 56 04                 ; 0xc1a96
    movzx si, ah                              ; 0f b6 f4                    ; 0xc1a9a
    add dx, si                                ; 01 f2                       ; 0xc1a9d
    sal dx, 003h                              ; c1 e2 03                    ; 0xc1a9f
    mov word [bp-008h], dx                    ; 89 56 f8                    ; 0xc1aa2
    xor ah, ah                                ; 30 e4                       ; 0xc1aa5 vgabios.c:1276
    imul ax, di                               ; 0f af c7                    ; 0xc1aa7
    imul ax, word [bp+004h]                   ; 0f af 46 04                 ; 0xc1aaa
    add si, ax                                ; 01 c6                       ; 0xc1aae
    sal si, 003h                              ; c1 e6 03                    ; 0xc1ab0
    mov word [bp-00ah], si                    ; 89 76 f6                    ; 0xc1ab3
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1ab6 vgabios.c:1277
    sal word [bp+004h], 003h                  ; c1 66 04 03                 ; 0xc1ab9 vgabios.c:1278
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc1abd vgabios.c:1279
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1ac1
    cmp al, byte [bp+006h]                    ; 3a 46 06                    ; 0xc1ac4
    jnc short 01aefh                          ; 73 26                       ; 0xc1ac7
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc1ac9 vgabios.c:1281
    imul ax, word [bp+004h]                   ; 0f af 46 04                 ; 0xc1acd
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc1ad1
    add si, ax                                ; 01 c6                       ; 0xc1ad4
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc1ad6
    add di, ax                                ; 01 c7                       ; 0xc1ad9
    mov cx, bx                                ; 89 d9                       ; 0xc1adb
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1add
    mov es, dx                                ; 8e c2                       ; 0xc1ae0
    jcxz 01aeah                               ; e3 06                       ; 0xc1ae2
    push DS                                   ; 1e                          ; 0xc1ae4
    mov ds, dx                                ; 8e da                       ; 0xc1ae5
    rep movsb                                 ; f3 a4                       ; 0xc1ae7
    pop DS                                    ; 1f                          ; 0xc1ae9
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1aea vgabios.c:1282
    jmp short 01ac1h                          ; eb d2                       ; 0xc1aed
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1aef vgabios.c:1283
    pop di                                    ; 5f                          ; 0xc1af2
    pop si                                    ; 5e                          ; 0xc1af3
    pop bp                                    ; 5d                          ; 0xc1af4
    retn 00004h                               ; c2 04 00                    ; 0xc1af5
  ; disGetNextSymbol 0xc1af8 LB 0x27cb -> off=0x0 cb=000000000000005c uValue=00000000000c1af8 'vgamem_fill_linear'
vgamem_fill_linear:                          ; 0xc1af8 LB 0x5c
    push bp                                   ; 55                          ; 0xc1af8 vgabios.c:1286
    mov bp, sp                                ; 89 e5                       ; 0xc1af9
    push si                                   ; 56                          ; 0xc1afb
    push di                                   ; 57                          ; 0xc1afc
    push ax                                   ; 50                          ; 0xc1afd
    push ax                                   ; 50                          ; 0xc1afe
    mov si, bx                                ; 89 de                       ; 0xc1aff
    mov bx, cx                                ; 89 cb                       ; 0xc1b01
    xor dh, dh                                ; 30 f6                       ; 0xc1b03 vgabios.c:1292
    movzx di, byte [bp+004h]                  ; 0f b6 7e 04                 ; 0xc1b05
    imul dx, di                               ; 0f af d7                    ; 0xc1b09
    imul dx, cx                               ; 0f af d1                    ; 0xc1b0c
    xor ah, ah                                ; 30 e4                       ; 0xc1b0f
    add ax, dx                                ; 01 d0                       ; 0xc1b11
    sal ax, 003h                              ; c1 e0 03                    ; 0xc1b13
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc1b16
    sal si, 003h                              ; c1 e6 03                    ; 0xc1b19 vgabios.c:1293
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1b1c vgabios.c:1294
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc1b1f vgabios.c:1295
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1b23
    cmp al, byte [bp+004h]                    ; 3a 46 04                    ; 0xc1b26
    jnc short 01b4bh                          ; 73 20                       ; 0xc1b29
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc1b2b vgabios.c:1297
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc1b2f
    imul dx, bx                               ; 0f af d3                    ; 0xc1b33
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc1b36
    add di, dx                                ; 01 d7                       ; 0xc1b39
    mov cx, si                                ; 89 f1                       ; 0xc1b3b
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1b3d
    mov es, dx                                ; 8e c2                       ; 0xc1b40
    jcxz 01b46h                               ; e3 02                       ; 0xc1b42
    rep stosb                                 ; f3 aa                       ; 0xc1b44
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1b46 vgabios.c:1298
    jmp short 01b23h                          ; eb d8                       ; 0xc1b49
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1b4b vgabios.c:1299
    pop di                                    ; 5f                          ; 0xc1b4e
    pop si                                    ; 5e                          ; 0xc1b4f
    pop bp                                    ; 5d                          ; 0xc1b50
    retn 00004h                               ; c2 04 00                    ; 0xc1b51
  ; disGetNextSymbol 0xc1b54 LB 0x276f -> off=0x0 cb=0000000000000628 uValue=00000000000c1b54 'biosfn_scroll'
biosfn_scroll:                               ; 0xc1b54 LB 0x628
    push bp                                   ; 55                          ; 0xc1b54 vgabios.c:1302
    mov bp, sp                                ; 89 e5                       ; 0xc1b55
    push si                                   ; 56                          ; 0xc1b57
    push di                                   ; 57                          ; 0xc1b58
    sub sp, strict byte 00018h                ; 83 ec 18                    ; 0xc1b59
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc1b5c
    mov byte [bp-012h], dl                    ; 88 56 ee                    ; 0xc1b5f
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1b62
    mov byte [bp-010h], cl                    ; 88 4e f0                    ; 0xc1b65
    mov dh, byte [bp+006h]                    ; 8a 76 06                    ; 0xc1b68
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc1b6b vgabios.c:1311
    jnbe near 02173h                          ; 0f 87 01 06                 ; 0xc1b6e
    cmp dh, cl                                ; 38 ce                       ; 0xc1b72 vgabios.c:1312
    jc near 02173h                            ; 0f 82 fb 05                 ; 0xc1b74
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1b78 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1b7b
    mov es, ax                                ; 8e c0                       ; 0xc1b7e
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1b80
    xor ah, ah                                ; 30 e4                       ; 0xc1b83 vgabios.c:1316
    call 035f7h                               ; e8 6f 1a                    ; 0xc1b85
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1b88
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1b8b vgabios.c:1317
    je near 02173h                            ; 0f 84 e2 05                 ; 0xc1b8d
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1b91 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1b94
    mov es, ax                                ; 8e c0                       ; 0xc1b97
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1b99
    movzx cx, al                              ; 0f b6 c8                    ; 0xc1b9c vgabios.c:48
    inc cx                                    ; 41                          ; 0xc1b9f
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1ba0 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc1ba3
    mov word [bp-014h], ax                    ; 89 46 ec                    ; 0xc1ba6 vgabios.c:58
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc1ba9 vgabios.c:1324
    jne short 01bb8h                          ; 75 09                       ; 0xc1bad
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc1baf vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1bb2
    mov byte [bp+008h], al                    ; 88 46 08                    ; 0xc1bb5 vgabios.c:48
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1bb8 vgabios.c:1327
    cmp ax, cx                                ; 39 c8                       ; 0xc1bbc
    jc short 01bc7h                           ; 72 07                       ; 0xc1bbe
    mov al, cl                                ; 88 c8                       ; 0xc1bc0
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc1bc2
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc1bc4
    movzx ax, dh                              ; 0f b6 c6                    ; 0xc1bc7 vgabios.c:1328
    cmp ax, word [bp-014h]                    ; 3b 46 ec                    ; 0xc1bca
    jc short 01bd4h                           ; 72 05                       ; 0xc1bcd
    mov dh, byte [bp-014h]                    ; 8a 76 ec                    ; 0xc1bcf
    db  0feh, 0ceh
    ; dec dh                                    ; fe ce                     ; 0xc1bd2
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1bd4 vgabios.c:1329
    cmp ax, cx                                ; 39 c8                       ; 0xc1bd8
    jbe short 01be0h                          ; 76 04                       ; 0xc1bda
    mov byte [bp-008h], 000h                  ; c6 46 f8 00                 ; 0xc1bdc
    mov al, dh                                ; 88 f0                       ; 0xc1be0 vgabios.c:1330
    sub al, byte [bp-010h]                    ; 2a 46 f0                    ; 0xc1be2
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc1be5
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1be7
    movzx di, byte [bp-006h]                  ; 0f b6 7e fa                 ; 0xc1bea vgabios.c:1332
    mov bx, di                                ; 89 fb                       ; 0xc1bee
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1bf0
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc1bf3
    dec ax                                    ; 48                          ; 0xc1bf6
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc1bf7
    mov ax, cx                                ; 89 c8                       ; 0xc1bfa
    dec ax                                    ; 48                          ; 0xc1bfc
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc1bfd
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc1c00
    imul ax, cx                               ; 0f af c1                    ; 0xc1c03
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc1c06
    jne near 01daah                           ; 0f 85 9b 01                 ; 0xc1c0b
    mov cx, ax                                ; 89 c1                       ; 0xc1c0f vgabios.c:1335
    add cx, ax                                ; 01 c1                       ; 0xc1c11
    or cl, 0ffh                               ; 80 c9 ff                    ; 0xc1c13
    movzx si, byte [bp+008h]                  ; 0f b6 76 08                 ; 0xc1c16
    inc cx                                    ; 41                          ; 0xc1c1a
    imul cx, si                               ; 0f af ce                    ; 0xc1c1b
    mov word [bp-01ch], cx                    ; 89 4e e4                    ; 0xc1c1e
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1c21 vgabios.c:1340
    jne short 01c62h                          ; 75 3b                       ; 0xc1c25
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1c27
    jne short 01c62h                          ; 75 35                       ; 0xc1c2b
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc1c2d
    jne short 01c62h                          ; 75 2f                       ; 0xc1c31
    movzx cx, byte [bp+004h]                  ; 0f b6 4e 04                 ; 0xc1c33
    cmp cx, word [bp-016h]                    ; 3b 4e ea                    ; 0xc1c37
    jne short 01c62h                          ; 75 26                       ; 0xc1c3a
    movzx dx, dh                              ; 0f b6 d6                    ; 0xc1c3c
    cmp dx, word [bp-018h]                    ; 3b 56 e8                    ; 0xc1c3f
    jne short 01c62h                          ; 75 1e                       ; 0xc1c42
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc1c44 vgabios.c:1342
    sal dx, 008h                              ; c1 e2 08                    ; 0xc1c48
    add dx, strict byte 00020h                ; 83 c2 20                    ; 0xc1c4b
    mov bx, word [bx+047b0h]                  ; 8b 9f b0 47                 ; 0xc1c4e
    mov cx, ax                                ; 89 c1                       ; 0xc1c52
    mov ax, dx                                ; 89 d0                       ; 0xc1c54
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1c56
    mov es, bx                                ; 8e c3                       ; 0xc1c59
    jcxz 01c5fh                               ; e3 02                       ; 0xc1c5b
    rep stosw                                 ; f3 ab                       ; 0xc1c5d
    jmp near 02173h                           ; e9 11 05                    ; 0xc1c5f vgabios.c:1344
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1c62 vgabios.c:1346
    jne near 01cffh                           ; 0f 85 95 00                 ; 0xc1c66
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1c6a vgabios.c:1347
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1c6e
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc1c71
    cmp dx, word [bp-01ah]                    ; 3b 56 e6                    ; 0xc1c75
    jc near 02173h                            ; 0f 82 f7 04                 ; 0xc1c78
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1c7c vgabios.c:1349
    add ax, word [bp-01ah]                    ; 03 46 e6                    ; 0xc1c80
    cmp ax, dx                                ; 39 d0                       ; 0xc1c83
    jnbe short 01c8dh                         ; 77 06                       ; 0xc1c85
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1c87
    jne short 01cc0h                          ; 75 33                       ; 0xc1c8b
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1c8d vgabios.c:1350
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1c91
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1c95
    add ax, strict word 00020h                ; 05 20 00                    ; 0xc1c98
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1c9b
    imul bx, word [bp-014h]                   ; 0f af 5e ec                 ; 0xc1c9e
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc1ca2
    add dx, bx                                ; 01 da                       ; 0xc1ca6
    add dx, dx                                ; 01 d2                       ; 0xc1ca8
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1caa
    add di, dx                                ; 01 d7                       ; 0xc1cad
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1caf
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1cb3
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc1cb6
    jcxz 01cbeh                               ; e3 02                       ; 0xc1cba
    rep stosw                                 ; f3 ab                       ; 0xc1cbc
    jmp short 01cf9h                          ; eb 39                       ; 0xc1cbe vgabios.c:1351
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1cc0 vgabios.c:1352
    mov si, ax                                ; 89 c6                       ; 0xc1cc4
    imul si, word [bp-014h]                   ; 0f af 76 ec                 ; 0xc1cc6
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc1cca
    add si, dx                                ; 01 d6                       ; 0xc1cce
    add si, si                                ; 01 f6                       ; 0xc1cd0
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1cd2
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1cd6
    mov ax, word [bx+047b0h]                  ; 8b 87 b0 47                 ; 0xc1cd9
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1cdd
    imul bx, word [bp-014h]                   ; 0f af 5e ec                 ; 0xc1ce0
    mov di, dx                                ; 89 d7                       ; 0xc1ce4
    add di, bx                                ; 01 df                       ; 0xc1ce6
    add di, di                                ; 01 ff                       ; 0xc1ce8
    add di, word [bp-01ch]                    ; 03 7e e4                    ; 0xc1cea
    mov dx, ax                                ; 89 c2                       ; 0xc1ced
    mov es, ax                                ; 8e c0                       ; 0xc1cef
    jcxz 01cf9h                               ; e3 06                       ; 0xc1cf1
    push DS                                   ; 1e                          ; 0xc1cf3
    mov ds, dx                                ; 8e da                       ; 0xc1cf4
    rep movsw                                 ; f3 a5                       ; 0xc1cf6
    pop DS                                    ; 1f                          ; 0xc1cf8
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc1cf9 vgabios.c:1353
    jmp near 01c71h                           ; e9 72 ff                    ; 0xc1cfc
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1cff vgabios.c:1356
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1d03
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1d06
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1d0a
    jnbe near 02173h                          ; 0f 87 62 04                 ; 0xc1d0d
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1d11 vgabios.c:1358
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc1d15
    add ax, dx                                ; 01 d0                       ; 0xc1d19
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1d1b
    jnbe short 01d26h                         ; 77 06                       ; 0xc1d1e
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1d20
    jne short 01d59h                          ; 75 33                       ; 0xc1d24
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1d26 vgabios.c:1359
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1d2a
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1d2e
    add ax, strict word 00020h                ; 05 20 00                    ; 0xc1d31
    mov dx, word [bp-01ah]                    ; 8b 56 e6                    ; 0xc1d34
    imul dx, word [bp-014h]                   ; 0f af 56 ec                 ; 0xc1d37
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc1d3b
    add dx, bx                                ; 01 da                       ; 0xc1d3f
    add dx, dx                                ; 01 d2                       ; 0xc1d41
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1d43
    add di, dx                                ; 01 d7                       ; 0xc1d46
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1d48
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1d4c
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc1d4f
    jcxz 01d57h                               ; e3 02                       ; 0xc1d53
    rep stosw                                 ; f3 ab                       ; 0xc1d55
    jmp short 01d99h                          ; eb 40                       ; 0xc1d57 vgabios.c:1360
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1d59 vgabios.c:1361
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1d5d
    mov si, word [bp-01ah]                    ; 8b 76 e6                    ; 0xc1d61
    sub si, ax                                ; 29 c6                       ; 0xc1d64
    imul si, word [bp-014h]                   ; 0f af 76 ec                 ; 0xc1d66
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc1d6a
    add si, dx                                ; 01 d6                       ; 0xc1d6e
    add si, si                                ; 01 f6                       ; 0xc1d70
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1d72
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1d76
    mov ax, word [bx+047b0h]                  ; 8b 87 b0 47                 ; 0xc1d79
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1d7d
    imul bx, word [bp-014h]                   ; 0f af 5e ec                 ; 0xc1d80
    add dx, bx                                ; 01 da                       ; 0xc1d84
    add dx, dx                                ; 01 d2                       ; 0xc1d86
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1d88
    add di, dx                                ; 01 d7                       ; 0xc1d8b
    mov dx, ax                                ; 89 c2                       ; 0xc1d8d
    mov es, ax                                ; 8e c0                       ; 0xc1d8f
    jcxz 01d99h                               ; e3 06                       ; 0xc1d91
    push DS                                   ; 1e                          ; 0xc1d93
    mov ds, dx                                ; 8e da                       ; 0xc1d94
    rep movsw                                 ; f3 a5                       ; 0xc1d96
    pop DS                                    ; 1f                          ; 0xc1d98
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1d99 vgabios.c:1362
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1d9d
    jc near 02173h                            ; 0f 82 cf 03                 ; 0xc1da0
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc1da4 vgabios.c:1363
    jmp near 01d06h                           ; e9 5c ff                    ; 0xc1da7
    movzx di, byte [di+0482ch]                ; 0f b6 bd 2c 48              ; 0xc1daa vgabios.c:1369
    sal di, 006h                              ; c1 e7 06                    ; 0xc1daf
    mov dl, byte [di+04842h]                  ; 8a 95 42 48                 ; 0xc1db2
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc1db6
    mov dl, byte [bx+047aeh]                  ; 8a 97 ae 47                 ; 0xc1db9 vgabios.c:1370
    cmp dl, 003h                              ; 80 fa 03                    ; 0xc1dbd
    jc short 01dd3h                           ; 72 11                       ; 0xc1dc0
    jbe short 01dddh                          ; 76 19                       ; 0xc1dc2
    cmp dl, 005h                              ; 80 fa 05                    ; 0xc1dc4
    je near 02056h                            ; 0f 84 8b 02                 ; 0xc1dc7
    cmp dl, 004h                              ; 80 fa 04                    ; 0xc1dcb
    je short 01dddh                           ; 74 0d                       ; 0xc1dce
    jmp near 02173h                           ; e9 a0 03                    ; 0xc1dd0
    cmp dl, 002h                              ; 80 fa 02                    ; 0xc1dd3
    je near 01f1ch                            ; 0f 84 42 01                 ; 0xc1dd6
    jmp near 02173h                           ; e9 96 03                    ; 0xc1dda
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1ddd vgabios.c:1374
    jne short 01e35h                          ; 75 52                       ; 0xc1de1
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1de3
    jne short 01e35h                          ; 75 4c                       ; 0xc1de7
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc1de9
    jne short 01e35h                          ; 75 46                       ; 0xc1ded
    movzx bx, byte [bp+004h]                  ; 0f b6 5e 04                 ; 0xc1def
    mov ax, cx                                ; 89 c8                       ; 0xc1df3
    dec ax                                    ; 48                          ; 0xc1df5
    cmp bx, ax                                ; 39 c3                       ; 0xc1df6
    jne short 01e35h                          ; 75 3b                       ; 0xc1df8
    movzx ax, dh                              ; 0f b6 c6                    ; 0xc1dfa
    mov dx, word [bp-014h]                    ; 8b 56 ec                    ; 0xc1dfd
    dec dx                                    ; 4a                          ; 0xc1e00
    cmp ax, dx                                ; 39 d0                       ; 0xc1e01
    jne short 01e35h                          ; 75 30                       ; 0xc1e03
    mov ax, 00205h                            ; b8 05 02                    ; 0xc1e05 vgabios.c:1376
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1e08
    out DX, ax                                ; ef                          ; 0xc1e0b
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc1e0c vgabios.c:1377
    imul ax, cx                               ; 0f af c1                    ; 0xc1e0f
    movzx cx, byte [bp-00eh]                  ; 0f b6 4e f2                 ; 0xc1e12
    imul cx, ax                               ; 0f af c8                    ; 0xc1e16
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1e19
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1e1d
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1e21
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc1e24
    xor di, di                                ; 31 ff                       ; 0xc1e28
    jcxz 01e2eh                               ; e3 02                       ; 0xc1e2a
    rep stosb                                 ; f3 aa                       ; 0xc1e2c
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1e2e vgabios.c:1378
    out DX, ax                                ; ef                          ; 0xc1e31
    jmp near 02173h                           ; e9 3e 03                    ; 0xc1e32 vgabios.c:1380
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1e35 vgabios.c:1382
    jne short 01ea4h                          ; 75 69                       ; 0xc1e39
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1e3b vgabios.c:1383
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1e3f
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1e42
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1e46
    jc near 02173h                            ; 0f 82 26 03                 ; 0xc1e49
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc1e4d vgabios.c:1385
    add dx, word [bp-01ah]                    ; 03 56 e6                    ; 0xc1e51
    cmp dx, ax                                ; 39 c2                       ; 0xc1e54
    jnbe short 01e5eh                         ; 77 06                       ; 0xc1e56
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1e58
    jne short 01e7dh                          ; 75 1f                       ; 0xc1e5c
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1e5e vgabios.c:1386
    push ax                                   ; 50                          ; 0xc1e62
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1e63
    push ax                                   ; 50                          ; 0xc1e67
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc1e68
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc1e6c
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc1e70
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1e74
    call 018fbh                               ; e8 80 fa                    ; 0xc1e78
    jmp short 01e9fh                          ; eb 22                       ; 0xc1e7b vgabios.c:1387
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1e7d vgabios.c:1388
    push ax                                   ; 50                          ; 0xc1e81
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc1e82
    push ax                                   ; 50                          ; 0xc1e86
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1e87
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc1e8b
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc1e8f
    add al, byte [bp-008h]                    ; 02 46 f8                    ; 0xc1e92
    movzx dx, al                              ; 0f b6 d0                    ; 0xc1e95
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1e98
    call 01886h                               ; e8 e7 f9                    ; 0xc1e9c
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc1e9f vgabios.c:1389
    jmp short 01e42h                          ; eb 9e                       ; 0xc1ea2
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1ea4 vgabios.c:1392
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1ea8
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1eab
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1eaf
    jnbe near 02173h                          ; 0f 87 bd 02                 ; 0xc1eb2
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc1eb6 vgabios.c:1394
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1eba
    add ax, dx                                ; 01 d0                       ; 0xc1ebe
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1ec0
    jnbe short 01ecbh                         ; 77 06                       ; 0xc1ec3
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1ec5
    jne short 01eeah                          ; 75 1f                       ; 0xc1ec9
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1ecb vgabios.c:1395
    push ax                                   ; 50                          ; 0xc1ecf
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1ed0
    push ax                                   ; 50                          ; 0xc1ed4
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc1ed5
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc1ed9
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc1edd
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1ee1
    call 018fbh                               ; e8 13 fa                    ; 0xc1ee5
    jmp short 01f0ch                          ; eb 22                       ; 0xc1ee8 vgabios.c:1396
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1eea vgabios.c:1397
    push ax                                   ; 50                          ; 0xc1eee
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc1eef
    push ax                                   ; 50                          ; 0xc1ef3
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1ef4
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc1ef8
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc1efc
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc1eff
    movzx dx, al                              ; 0f b6 d0                    ; 0xc1f02
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1f05
    call 01886h                               ; e8 7a f9                    ; 0xc1f09
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1f0c vgabios.c:1398
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1f10
    jc near 02173h                            ; 0f 82 5c 02                 ; 0xc1f13
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc1f17 vgabios.c:1399
    jmp short 01eabh                          ; eb 8f                       ; 0xc1f1a
    mov dl, byte [bx+047afh]                  ; 8a 97 af 47                 ; 0xc1f1c vgabios.c:1404
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1f20 vgabios.c:1405
    jne short 01f61h                          ; 75 3b                       ; 0xc1f24
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1f26
    jne short 01f61h                          ; 75 35                       ; 0xc1f2a
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc1f2c
    jne short 01f61h                          ; 75 2f                       ; 0xc1f30
    movzx cx, byte [bp+004h]                  ; 0f b6 4e 04                 ; 0xc1f32
    cmp cx, word [bp-016h]                    ; 3b 4e ea                    ; 0xc1f36
    jne short 01f61h                          ; 75 26                       ; 0xc1f39
    movzx cx, dh                              ; 0f b6 ce                    ; 0xc1f3b
    cmp cx, word [bp-018h]                    ; 3b 4e e8                    ; 0xc1f3e
    jne short 01f61h                          ; 75 1e                       ; 0xc1f41
    movzx cx, byte [bp-00eh]                  ; 0f b6 4e f2                 ; 0xc1f43 vgabios.c:1407
    imul ax, cx                               ; 0f af c1                    ; 0xc1f47
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc1f4a
    imul cx, ax                               ; 0f af c8                    ; 0xc1f4d
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1f50
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc1f54
    xor di, di                                ; 31 ff                       ; 0xc1f58
    jcxz 01f5eh                               ; e3 02                       ; 0xc1f5a
    rep stosb                                 ; f3 aa                       ; 0xc1f5c
    jmp near 02173h                           ; e9 12 02                    ; 0xc1f5e vgabios.c:1409
    cmp dl, 002h                              ; 80 fa 02                    ; 0xc1f61 vgabios.c:1411
    jne short 01f6fh                          ; 75 09                       ; 0xc1f64
    sal byte [bp-010h], 1                     ; d0 66 f0                    ; 0xc1f66 vgabios.c:1413
    sal byte [bp-00ah], 1                     ; d0 66 f6                    ; 0xc1f69 vgabios.c:1414
    sal word [bp-014h], 1                     ; d1 66 ec                    ; 0xc1f6c vgabios.c:1415
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1f6f vgabios.c:1418
    jne short 01fdeh                          ; 75 69                       ; 0xc1f73
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1f75 vgabios.c:1419
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1f79
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1f7c
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1f80
    jc near 02173h                            ; 0f 82 ec 01                 ; 0xc1f83
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc1f87 vgabios.c:1421
    add dx, word [bp-01ah]                    ; 03 56 e6                    ; 0xc1f8b
    cmp dx, ax                                ; 39 c2                       ; 0xc1f8e
    jnbe short 01f98h                         ; 77 06                       ; 0xc1f90
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1f92
    jne short 01fb7h                          ; 75 1f                       ; 0xc1f96
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1f98 vgabios.c:1422
    push ax                                   ; 50                          ; 0xc1f9c
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1f9d
    push ax                                   ; 50                          ; 0xc1fa1
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc1fa2
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc1fa6
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc1faa
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1fae
    call 019feh                               ; e8 49 fa                    ; 0xc1fb2
    jmp short 01fd9h                          ; eb 22                       ; 0xc1fb5 vgabios.c:1423
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1fb7 vgabios.c:1424
    push ax                                   ; 50                          ; 0xc1fbb
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc1fbc
    push ax                                   ; 50                          ; 0xc1fc0
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1fc1
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc1fc5
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc1fc9
    add al, byte [bp-008h]                    ; 02 46 f8                    ; 0xc1fcc
    movzx dx, al                              ; 0f b6 d0                    ; 0xc1fcf
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1fd2
    call 0195bh                               ; e8 82 f9                    ; 0xc1fd6
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc1fd9 vgabios.c:1425
    jmp short 01f7ch                          ; eb 9e                       ; 0xc1fdc
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1fde vgabios.c:1428
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1fe2
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1fe5
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1fe9
    jnbe near 02173h                          ; 0f 87 83 01                 ; 0xc1fec
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc1ff0 vgabios.c:1430
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1ff4
    add ax, dx                                ; 01 d0                       ; 0xc1ff8
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1ffa
    jnbe short 02005h                         ; 77 06                       ; 0xc1ffd
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1fff
    jne short 02024h                          ; 75 1f                       ; 0xc2003
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc2005 vgabios.c:1431
    push ax                                   ; 50                          ; 0xc2009
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc200a
    push ax                                   ; 50                          ; 0xc200e
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc200f
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc2013
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc2017
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc201b
    call 019feh                               ; e8 dc f9                    ; 0xc201f
    jmp short 02046h                          ; eb 22                       ; 0xc2022 vgabios.c:1432
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc2024 vgabios.c:1433
    push ax                                   ; 50                          ; 0xc2028
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc2029
    push ax                                   ; 50                          ; 0xc202d
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc202e
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc2032
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc2036
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc2039
    movzx dx, al                              ; 0f b6 d0                    ; 0xc203c
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc203f
    call 0195bh                               ; e8 15 f9                    ; 0xc2043
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc2046 vgabios.c:1434
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc204a
    jc near 02173h                            ; 0f 82 22 01                 ; 0xc204d
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc2051 vgabios.c:1435
    jmp short 01fe5h                          ; eb 8f                       ; 0xc2054
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc2056 vgabios.c:1440
    jne short 02096h                          ; 75 3a                       ; 0xc205a
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc205c
    jne short 02096h                          ; 75 34                       ; 0xc2060
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc2062
    jne short 02096h                          ; 75 2e                       ; 0xc2066
    movzx cx, byte [bp+004h]                  ; 0f b6 4e 04                 ; 0xc2068
    cmp cx, word [bp-016h]                    ; 3b 4e ea                    ; 0xc206c
    jne short 02096h                          ; 75 25                       ; 0xc206f
    movzx dx, dh                              ; 0f b6 d6                    ; 0xc2071
    cmp dx, word [bp-018h]                    ; 3b 56 e8                    ; 0xc2074
    jne short 02096h                          ; 75 1d                       ; 0xc2077
    movzx dx, byte [bp-00eh]                  ; 0f b6 56 f2                 ; 0xc2079 vgabios.c:1442
    mov cx, ax                                ; 89 c1                       ; 0xc207d
    imul cx, dx                               ; 0f af ca                    ; 0xc207f
    sal cx, 003h                              ; c1 e1 03                    ; 0xc2082
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc2085
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc2089
    xor di, di                                ; 31 ff                       ; 0xc208d
    jcxz 02093h                               ; e3 02                       ; 0xc208f
    rep stosb                                 ; f3 aa                       ; 0xc2091
    jmp near 02173h                           ; e9 dd 00                    ; 0xc2093 vgabios.c:1444
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc2096 vgabios.c:1447
    jne short 02102h                          ; 75 66                       ; 0xc209a
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc209c vgabios.c:1448
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc20a0
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc20a3
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc20a7
    jc near 02173h                            ; 0f 82 c5 00                 ; 0xc20aa
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc20ae vgabios.c:1450
    add dx, word [bp-01ah]                    ; 03 56 e6                    ; 0xc20b2
    cmp dx, ax                                ; 39 c2                       ; 0xc20b5
    jnbe short 020bfh                         ; 77 06                       ; 0xc20b7
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc20b9
    jne short 020ddh                          ; 75 1e                       ; 0xc20bd
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc20bf vgabios.c:1451
    push ax                                   ; 50                          ; 0xc20c3
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc20c4
    push ax                                   ; 50                          ; 0xc20c8
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc20c9
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc20cd
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc20d1
    mov cx, word [bp-014h]                    ; 8b 4e ec                    ; 0xc20d5
    call 01af8h                               ; e8 1d fa                    ; 0xc20d8
    jmp short 020fdh                          ; eb 20                       ; 0xc20db vgabios.c:1452
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc20dd vgabios.c:1453
    push ax                                   ; 50                          ; 0xc20e1
    push word [bp-014h]                       ; ff 76 ec                    ; 0xc20e2
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc20e5
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc20e9
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc20ed
    add al, byte [bp-008h]                    ; 02 46 f8                    ; 0xc20f0
    movzx dx, al                              ; 0f b6 d0                    ; 0xc20f3
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc20f6
    call 01a7fh                               ; e8 82 f9                    ; 0xc20fa
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc20fd vgabios.c:1454
    jmp short 020a3h                          ; eb a1                       ; 0xc2100
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc2102 vgabios.c:1457
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc2106
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc2109
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc210d
    jnbe short 02173h                         ; 77 61                       ; 0xc2110
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc2112 vgabios.c:1459
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2116
    add ax, dx                                ; 01 d0                       ; 0xc211a
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc211c
    jnbe short 02127h                         ; 77 06                       ; 0xc211f
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc2121
    jne short 02145h                          ; 75 1e                       ; 0xc2125
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc2127 vgabios.c:1460
    push ax                                   ; 50                          ; 0xc212b
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc212c
    push ax                                   ; 50                          ; 0xc2130
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc2131
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc2135
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2139
    mov cx, word [bp-014h]                    ; 8b 4e ec                    ; 0xc213d
    call 01af8h                               ; e8 b5 f9                    ; 0xc2140
    jmp short 02165h                          ; eb 20                       ; 0xc2143 vgabios.c:1461
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc2145 vgabios.c:1462
    push ax                                   ; 50                          ; 0xc2149
    push word [bp-014h]                       ; ff 76 ec                    ; 0xc214a
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc214d
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc2151
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc2155
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc2158
    movzx dx, al                              ; 0f b6 d0                    ; 0xc215b
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc215e
    call 01a7fh                               ; e8 1a f9                    ; 0xc2162
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc2165 vgabios.c:1463
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc2169
    jc short 02173h                           ; 72 05                       ; 0xc216c
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc216e vgabios.c:1464
    jmp short 02109h                          ; eb 96                       ; 0xc2171
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2173 vgabios.c:1475
    pop di                                    ; 5f                          ; 0xc2176
    pop si                                    ; 5e                          ; 0xc2177
    pop bp                                    ; 5d                          ; 0xc2178
    retn 00008h                               ; c2 08 00                    ; 0xc2179
  ; disGetNextSymbol 0xc217c LB 0x2147 -> off=0x0 cb=00000000000000ff uValue=00000000000c217c 'write_gfx_char_pl4'
write_gfx_char_pl4:                          ; 0xc217c LB 0xff
    push bp                                   ; 55                          ; 0xc217c vgabios.c:1478
    mov bp, sp                                ; 89 e5                       ; 0xc217d
    push si                                   ; 56                          ; 0xc217f
    push di                                   ; 57                          ; 0xc2180
    sub sp, strict byte 0000ch                ; 83 ec 0c                    ; 0xc2181
    mov ah, al                                ; 88 c4                       ; 0xc2184
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc2186
    mov al, bl                                ; 88 d8                       ; 0xc2189
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc218b vgabios.c:67
    xor si, si                                ; 31 f6                       ; 0xc218e
    mov es, si                                ; 8e c6                       ; 0xc2190
    mov si, word [es:bx]                      ; 26 8b 37                    ; 0xc2192
    mov bx, word [es:bx+002h]                 ; 26 8b 5f 02                 ; 0xc2195
    mov word [bp-00ch], si                    ; 89 76 f4                    ; 0xc2199 vgabios.c:68
    mov word [bp-00ah], bx                    ; 89 5e f6                    ; 0xc219c
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc219f vgabios.c:1487
    movzx cx, byte [bp+006h]                  ; 0f b6 4e 06                 ; 0xc21a2
    imul bx, cx                               ; 0f af d9                    ; 0xc21a6
    movzx si, byte [bp+004h]                  ; 0f b6 76 04                 ; 0xc21a9
    imul si, bx                               ; 0f af f3                    ; 0xc21ad
    movzx bx, al                              ; 0f b6 d8                    ; 0xc21b0
    add si, bx                                ; 01 de                       ; 0xc21b3
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc21b5 vgabios.c:57
    mov di, strict word 00040h                ; bf 40 00                    ; 0xc21b8
    mov es, di                                ; 8e c7                       ; 0xc21bb
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc21bd
    movzx di, byte [bp+008h]                  ; 0f b6 7e 08                 ; 0xc21c0 vgabios.c:58
    imul bx, di                               ; 0f af df                    ; 0xc21c4
    add si, bx                                ; 01 de                       ; 0xc21c7
    movzx ax, ah                              ; 0f b6 c4                    ; 0xc21c9 vgabios.c:1489
    imul ax, cx                               ; 0f af c1                    ; 0xc21cc
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc21cf
    mov ax, 00f02h                            ; b8 02 0f                    ; 0xc21d2 vgabios.c:1490
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc21d5
    out DX, ax                                ; ef                          ; 0xc21d8
    mov ax, 00205h                            ; b8 05 02                    ; 0xc21d9 vgabios.c:1491
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc21dc
    out DX, ax                                ; ef                          ; 0xc21df
    test byte [bp-008h], 080h                 ; f6 46 f8 80                 ; 0xc21e0 vgabios.c:1492
    je short 021ech                           ; 74 06                       ; 0xc21e4
    mov ax, 01803h                            ; b8 03 18                    ; 0xc21e6 vgabios.c:1494
    out DX, ax                                ; ef                          ; 0xc21e9
    jmp short 021f0h                          ; eb 04                       ; 0xc21ea vgabios.c:1496
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc21ec vgabios.c:1498
    out DX, ax                                ; ef                          ; 0xc21ef
    xor ch, ch                                ; 30 ed                       ; 0xc21f0 vgabios.c:1500
    cmp ch, byte [bp+006h]                    ; 3a 6e 06                    ; 0xc21f2
    jnc short 02263h                          ; 73 6c                       ; 0xc21f5
    movzx bx, ch                              ; 0f b6 dd                    ; 0xc21f7 vgabios.c:1502
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc21fa
    imul bx, ax                               ; 0f af d8                    ; 0xc21fe
    add bx, si                                ; 01 f3                       ; 0xc2201
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc2203 vgabios.c:1503
    jmp short 0221bh                          ; eb 12                       ; 0xc2207
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2209 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc220c
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc220e
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc2212 vgabios.c:1516
    cmp byte [bp-006h], 008h                  ; 80 7e fa 08                 ; 0xc2215
    jnc short 0225fh                          ; 73 44                       ; 0xc2219
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc221b
    mov cl, al                                ; 88 c1                       ; 0xc221f
    mov ax, 00080h                            ; b8 80 00                    ; 0xc2221
    sar ax, CL                                ; d3 f8                       ; 0xc2224
    xor ah, ah                                ; 30 e4                       ; 0xc2226
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc2228
    sal ax, 008h                              ; c1 e0 08                    ; 0xc222b
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc222e
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2230
    out DX, ax                                ; ef                          ; 0xc2233
    mov dx, bx                                ; 89 da                       ; 0xc2234
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2236
    call 0361eh                               ; e8 e2 13                    ; 0xc2239
    movzx ax, ch                              ; 0f b6 c5                    ; 0xc223c
    add ax, word [bp-00eh]                    ; 03 46 f2                    ; 0xc223f
    les di, [bp-00ch]                         ; c4 7e f4                    ; 0xc2242
    add di, ax                                ; 01 c7                       ; 0xc2245
    movzx ax, byte [es:di]                    ; 26 0f b6 05                 ; 0xc2247
    test word [bp-010h], ax                   ; 85 46 f0                    ; 0xc224b
    je short 02209h                           ; 74 b9                       ; 0xc224e
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2250
    and AL, strict byte 00fh                  ; 24 0f                       ; 0xc2253
    mov di, 0a000h                            ; bf 00 a0                    ; 0xc2255
    mov es, di                                ; 8e c7                       ; 0xc2258
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc225a
    jmp short 02212h                          ; eb b3                       ; 0xc225d
    db  0feh, 0c5h
    ; inc ch                                    ; fe c5                     ; 0xc225f vgabios.c:1517
    jmp short 021f2h                          ; eb 8f                       ; 0xc2261
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc2263 vgabios.c:1518
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2266
    out DX, ax                                ; ef                          ; 0xc2269
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc226a vgabios.c:1519
    out DX, ax                                ; ef                          ; 0xc226d
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc226e vgabios.c:1520
    out DX, ax                                ; ef                          ; 0xc2271
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2272 vgabios.c:1521
    pop di                                    ; 5f                          ; 0xc2275
    pop si                                    ; 5e                          ; 0xc2276
    pop bp                                    ; 5d                          ; 0xc2277
    retn 00006h                               ; c2 06 00                    ; 0xc2278
  ; disGetNextSymbol 0xc227b LB 0x2048 -> off=0x0 cb=00000000000000dd uValue=00000000000c227b 'write_gfx_char_cga'
write_gfx_char_cga:                          ; 0xc227b LB 0xdd
    push si                                   ; 56                          ; 0xc227b vgabios.c:1524
    push di                                   ; 57                          ; 0xc227c
    enter 00006h, 000h                        ; c8 06 00 00                 ; 0xc227d
    mov di, 0556ah                            ; bf 6a 55                    ; 0xc2281 vgabios.c:1531
    xor bh, bh                                ; 30 ff                       ; 0xc2284 vgabios.c:1532
    movzx si, byte [bp+00ah]                  ; 0f b6 76 0a                 ; 0xc2286
    imul si, bx                               ; 0f af f3                    ; 0xc228a
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc228d
    imul bx, bx, 00140h                       ; 69 db 40 01                 ; 0xc2290
    add si, bx                                ; 01 de                       ; 0xc2294
    mov word [bp-004h], si                    ; 89 76 fc                    ; 0xc2296
    xor ah, ah                                ; 30 e4                       ; 0xc2299 vgabios.c:1533
    sal ax, 003h                              ; c1 e0 03                    ; 0xc229b
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc229e
    xor ah, ah                                ; 30 e4                       ; 0xc22a1 vgabios.c:1534
    jmp near 022c1h                           ; e9 1b 00                    ; 0xc22a3
    movzx si, ah                              ; 0f b6 f4                    ; 0xc22a6 vgabios.c:1549
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc22a9
    add si, di                                ; 01 fe                       ; 0xc22ac
    mov al, byte [si]                         ; 8a 04                       ; 0xc22ae
    mov si, 0b800h                            ; be 00 b8                    ; 0xc22b0 vgabios.c:52
    mov es, si                                ; 8e c6                       ; 0xc22b3
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc22b5
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc22b8 vgabios.c:1553
    cmp ah, 008h                              ; 80 fc 08                    ; 0xc22ba
    jnc near 02352h                           ; 0f 83 91 00                 ; 0xc22bd
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc22c1
    sar bx, 1                                 ; d1 fb                       ; 0xc22c4
    imul bx, bx, strict byte 00050h           ; 6b db 50                    ; 0xc22c6
    add bx, word [bp-004h]                    ; 03 5e fc                    ; 0xc22c9
    test ah, 001h                             ; f6 c4 01                    ; 0xc22cc
    je short 022d4h                           ; 74 03                       ; 0xc22cf
    add bh, 020h                              ; 80 c7 20                    ; 0xc22d1
    mov DH, strict byte 080h                  ; b6 80                       ; 0xc22d4
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc22d6
    jne short 022f4h                          ; 75 18                       ; 0xc22da
    test dl, dh                               ; 84 f2                       ; 0xc22dc
    je short 022a6h                           ; 74 c6                       ; 0xc22de
    mov si, 0b800h                            ; be 00 b8                    ; 0xc22e0
    mov es, si                                ; 8e c6                       ; 0xc22e3
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc22e5
    movzx si, ah                              ; 0f b6 f4                    ; 0xc22e8
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc22eb
    add si, di                                ; 01 fe                       ; 0xc22ee
    xor al, byte [si]                         ; 32 04                       ; 0xc22f0
    jmp short 022b0h                          ; eb bc                       ; 0xc22f2
    test dh, dh                               ; 84 f6                       ; 0xc22f4 vgabios.c:1555
    jbe short 022b8h                          ; 76 c0                       ; 0xc22f6
    test dl, 080h                             ; f6 c2 80                    ; 0xc22f8 vgabios.c:1557
    je short 02307h                           ; 74 0a                       ; 0xc22fb
    mov si, 0b800h                            ; be 00 b8                    ; 0xc22fd vgabios.c:47
    mov es, si                                ; 8e c6                       ; 0xc2300
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2302
    jmp short 02309h                          ; eb 02                       ; 0xc2305 vgabios.c:1561
    xor al, al                                ; 30 c0                       ; 0xc2307 vgabios.c:1563
    mov byte [bp-002h], 000h                  ; c6 46 fe 00                 ; 0xc2309 vgabios.c:1565
    jmp short 0231ch                          ; eb 0d                       ; 0xc230d
    or al, ch                                 ; 08 e8                       ; 0xc230f vgabios.c:1575
    shr dh, 1                                 ; d0 ee                       ; 0xc2311 vgabios.c:1578
    inc byte [bp-002h]                        ; fe 46 fe                    ; 0xc2313 vgabios.c:1579
    cmp byte [bp-002h], 004h                  ; 80 7e fe 04                 ; 0xc2316
    jnc short 02347h                          ; 73 2b                       ; 0xc231a
    movzx si, ah                              ; 0f b6 f4                    ; 0xc231c
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc231f
    add si, di                                ; 01 fe                       ; 0xc2322
    movzx si, byte [si]                       ; 0f b6 34                    ; 0xc2324
    movzx cx, dh                              ; 0f b6 ce                    ; 0xc2327
    test si, cx                               ; 85 ce                       ; 0xc232a
    je short 02311h                           ; 74 e3                       ; 0xc232c
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc232e
    sub cl, byte [bp-002h]                    ; 2a 4e fe                    ; 0xc2330
    mov ch, dl                                ; 88 d5                       ; 0xc2333
    and ch, 003h                              ; 80 e5 03                    ; 0xc2335
    add cl, cl                                ; 00 c9                       ; 0xc2338
    sal ch, CL                                ; d2 e5                       ; 0xc233a
    mov cl, ch                                ; 88 e9                       ; 0xc233c
    test dl, 080h                             ; f6 c2 80                    ; 0xc233e
    je short 0230fh                           ; 74 cc                       ; 0xc2341
    xor al, ch                                ; 30 e8                       ; 0xc2343
    jmp short 02311h                          ; eb ca                       ; 0xc2345
    mov cx, 0b800h                            ; b9 00 b8                    ; 0xc2347 vgabios.c:52
    mov es, cx                                ; 8e c1                       ; 0xc234a
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc234c
    inc bx                                    ; 43                          ; 0xc234f vgabios.c:1581
    jmp short 022f4h                          ; eb a2                       ; 0xc2350 vgabios.c:1582
    leave                                     ; c9                          ; 0xc2352 vgabios.c:1585
    pop di                                    ; 5f                          ; 0xc2353
    pop si                                    ; 5e                          ; 0xc2354
    retn 00004h                               ; c2 04 00                    ; 0xc2355
  ; disGetNextSymbol 0xc2358 LB 0x1f6b -> off=0x0 cb=0000000000000085 uValue=00000000000c2358 'write_gfx_char_lin'
write_gfx_char_lin:                          ; 0xc2358 LB 0x85
    push si                                   ; 56                          ; 0xc2358 vgabios.c:1588
    push di                                   ; 57                          ; 0xc2359
    enter 00006h, 000h                        ; c8 06 00 00                 ; 0xc235a
    mov dh, dl                                ; 88 d6                       ; 0xc235e
    mov word [bp-002h], 0556ah                ; c7 46 fe 6a 55              ; 0xc2360 vgabios.c:1595
    movzx si, cl                              ; 0f b6 f1                    ; 0xc2365 vgabios.c:1596
    movzx cx, byte [bp+008h]                  ; 0f b6 4e 08                 ; 0xc2368
    imul cx, si                               ; 0f af ce                    ; 0xc236c
    sal cx, 006h                              ; c1 e1 06                    ; 0xc236f
    xor bh, bh                                ; 30 ff                       ; 0xc2372
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2374
    add bx, cx                                ; 01 cb                       ; 0xc2377
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc2379
    xor ah, ah                                ; 30 e4                       ; 0xc237c vgabios.c:1597
    mov si, ax                                ; 89 c6                       ; 0xc237e
    sal si, 003h                              ; c1 e6 03                    ; 0xc2380
    xor al, al                                ; 30 c0                       ; 0xc2383 vgabios.c:1598
    jmp short 023bch                          ; eb 35                       ; 0xc2385
    cmp ah, 008h                              ; 80 fc 08                    ; 0xc2387 vgabios.c:1602
    jnc short 023b6h                          ; 73 2a                       ; 0xc238a
    xor cl, cl                                ; 30 c9                       ; 0xc238c vgabios.c:1604
    movzx bx, al                              ; 0f b6 d8                    ; 0xc238e vgabios.c:1605
    add bx, si                                ; 01 f3                       ; 0xc2391
    add bx, word [bp-002h]                    ; 03 5e fe                    ; 0xc2393
    movzx bx, byte [bx]                       ; 0f b6 1f                    ; 0xc2396
    movzx di, dl                              ; 0f b6 fa                    ; 0xc2399
    test bx, di                               ; 85 fb                       ; 0xc239c
    je short 023a2h                           ; 74 02                       ; 0xc239e
    mov cl, dh                                ; 88 f1                       ; 0xc23a0 vgabios.c:1607
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc23a2 vgabios.c:1609
    add bx, word [bp-006h]                    ; 03 5e fa                    ; 0xc23a5
    mov di, 0a000h                            ; bf 00 a0                    ; 0xc23a8 vgabios.c:52
    mov es, di                                ; 8e c7                       ; 0xc23ab
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc23ad
    shr dl, 1                                 ; d0 ea                       ; 0xc23b0 vgabios.c:1610
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc23b2 vgabios.c:1611
    jmp short 02387h                          ; eb d1                       ; 0xc23b4
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc23b6 vgabios.c:1612
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc23b8
    jnc short 023d7h                          ; 73 1b                       ; 0xc23ba
    movzx cx, al                              ; 0f b6 c8                    ; 0xc23bc
    movzx bx, byte [bp+008h]                  ; 0f b6 5e 08                 ; 0xc23bf
    imul bx, cx                               ; 0f af d9                    ; 0xc23c3
    sal bx, 003h                              ; c1 e3 03                    ; 0xc23c6
    mov cx, word [bp-004h]                    ; 8b 4e fc                    ; 0xc23c9
    add cx, bx                                ; 01 d9                       ; 0xc23cc
    mov word [bp-006h], cx                    ; 89 4e fa                    ; 0xc23ce
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc23d1
    xor ah, ah                                ; 30 e4                       ; 0xc23d3
    jmp short 0238ch                          ; eb b5                       ; 0xc23d5
    leave                                     ; c9                          ; 0xc23d7 vgabios.c:1613
    pop di                                    ; 5f                          ; 0xc23d8
    pop si                                    ; 5e                          ; 0xc23d9
    retn 00002h                               ; c2 02 00                    ; 0xc23da
  ; disGetNextSymbol 0xc23dd LB 0x1ee6 -> off=0x0 cb=0000000000000165 uValue=00000000000c23dd 'biosfn_write_char_attr'
biosfn_write_char_attr:                      ; 0xc23dd LB 0x165
    push bp                                   ; 55                          ; 0xc23dd vgabios.c:1616
    mov bp, sp                                ; 89 e5                       ; 0xc23de
    push si                                   ; 56                          ; 0xc23e0
    push di                                   ; 57                          ; 0xc23e1
    sub sp, strict byte 00018h                ; 83 ec 18                    ; 0xc23e2
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc23e5
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc23e8
    mov byte [bp-012h], bl                    ; 88 5e ee                    ; 0xc23eb
    mov si, cx                                ; 89 ce                       ; 0xc23ee
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc23f0 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc23f3
    mov es, ax                                ; 8e c0                       ; 0xc23f6
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc23f8
    xor ah, ah                                ; 30 e4                       ; 0xc23fb vgabios.c:1624
    call 035f7h                               ; e8 f7 11                    ; 0xc23fd
    mov cl, al                                ; 88 c1                       ; 0xc2400
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2402
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc2405 vgabios.c:1625
    je near 0253bh                            ; 0f 84 30 01                 ; 0xc2407
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc240b vgabios.c:1628
    lea bx, [bp-01ch]                         ; 8d 5e e4                    ; 0xc240e
    lea dx, [bp-01ah]                         ; 8d 56 e6                    ; 0xc2411
    call 00a93h                               ; e8 7c e6                    ; 0xc2414
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc2417 vgabios.c:1629
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc241a
    mov dx, word [bp-01ch]                    ; 8b 56 e4                    ; 0xc241d
    xor dl, dl                                ; 30 d2                       ; 0xc2420
    shr dx, 008h                              ; c1 ea 08                    ; 0xc2422
    mov byte [bp-014h], dl                    ; 88 56 ec                    ; 0xc2425
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2428 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc242b
    mov es, ax                                ; 8e c0                       ; 0xc242e
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2430
    xor ah, ah                                ; 30 e4                       ; 0xc2433 vgabios.c:48
    inc ax                                    ; 40                          ; 0xc2435
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc2436
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2439 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc243c
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc243f vgabios.c:58
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc2442 vgabios.c:1635
    mov di, bx                                ; 89 df                       ; 0xc2445
    sal di, 003h                              ; c1 e7 03                    ; 0xc2447
    cmp byte [di+047adh], 000h                ; 80 bd ad 47 00              ; 0xc244a
    jne short 02497h                          ; 75 46                       ; 0xc244f
    mov bx, word [bp-018h]                    ; 8b 5e e8                    ; 0xc2451 vgabios.c:1638
    imul bx, ax                               ; 0f af d8                    ; 0xc2454
    add bx, bx                                ; 01 db                       ; 0xc2457
    or bl, 0ffh                               ; 80 cb ff                    ; 0xc2459
    movzx cx, byte [bp-00eh]                  ; 0f b6 4e f2                 ; 0xc245c
    inc bx                                    ; 43                          ; 0xc2460
    imul bx, cx                               ; 0f af d9                    ; 0xc2461
    xor dh, dh                                ; 30 f6                       ; 0xc2464
    imul ax, dx                               ; 0f af c2                    ; 0xc2466
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc2469
    add ax, dx                                ; 01 d0                       ; 0xc246d
    add ax, ax                                ; 01 c0                       ; 0xc246f
    mov dx, bx                                ; 89 da                       ; 0xc2471
    add dx, ax                                ; 01 c2                       ; 0xc2473
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc2475 vgabios.c:1640
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2479
    movzx bx, byte [bp-008h]                  ; 0f b6 5e f8                 ; 0xc247c
    add ax, bx                                ; 01 d8                       ; 0xc2480
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc2482
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc2485 vgabios.c:1641
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc2488
    mov cx, si                                ; 89 f1                       ; 0xc248c
    mov di, dx                                ; 89 d7                       ; 0xc248e
    jcxz 02494h                               ; e3 02                       ; 0xc2490
    rep stosw                                 ; f3 ab                       ; 0xc2492
    jmp near 0253bh                           ; e9 a4 00                    ; 0xc2494 vgabios.c:1643
    movzx bx, byte [bx+0482ch]                ; 0f b6 9f 2c 48              ; 0xc2497 vgabios.c:1646
    sal bx, 006h                              ; c1 e3 06                    ; 0xc249c
    mov al, byte [bx+04842h]                  ; 8a 87 42 48                 ; 0xc249f
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc24a3
    mov al, byte [di+047afh]                  ; 8a 85 af 47                 ; 0xc24a6 vgabios.c:1647
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc24aa
    dec si                                    ; 4e                          ; 0xc24ad vgabios.c:1648
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc24ae
    je near 0253bh                            ; 0f 84 86 00                 ; 0xc24b1
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc24b5 vgabios.c:1650
    sal bx, 003h                              ; c1 e3 03                    ; 0xc24b9
    mov al, byte [bx+047aeh]                  ; 8a 87 ae 47                 ; 0xc24bc
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc24c0
    jc short 024d0h                           ; 72 0c                       ; 0xc24c2
    jbe short 024d6h                          ; 76 10                       ; 0xc24c4
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc24c6
    je short 0251dh                           ; 74 53                       ; 0xc24c8
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc24ca
    je short 024dah                           ; 74 0c                       ; 0xc24cc
    jmp short 02535h                          ; eb 65                       ; 0xc24ce
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc24d0
    je short 024feh                           ; 74 2a                       ; 0xc24d2
    jmp short 02535h                          ; eb 5f                       ; 0xc24d4
    or byte [bp-012h], 001h                   ; 80 4e ee 01                 ; 0xc24d6 vgabios.c:1653
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc24da vgabios.c:1655
    push ax                                   ; 50                          ; 0xc24de
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc24df
    push ax                                   ; 50                          ; 0xc24e3
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc24e4
    push ax                                   ; 50                          ; 0xc24e8
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc24e9
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc24ed
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc24f1
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc24f5
    call 0217ch                               ; e8 80 fc                    ; 0xc24f9
    jmp short 02535h                          ; eb 37                       ; 0xc24fc vgabios.c:1656
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc24fe vgabios.c:1658
    push ax                                   ; 50                          ; 0xc2502
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc2503
    push ax                                   ; 50                          ; 0xc2507
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc2508
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc250c
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc2510
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2514
    call 0227bh                               ; e8 60 fd                    ; 0xc2518
    jmp short 02535h                          ; eb 18                       ; 0xc251b vgabios.c:1659
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc251d vgabios.c:1661
    push ax                                   ; 50                          ; 0xc2521
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc2522
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc2526
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc252a
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc252e
    call 02358h                               ; e8 23 fe                    ; 0xc2532
    inc byte [bp-010h]                        ; fe 46 f0                    ; 0xc2535 vgabios.c:1668
    jmp near 024adh                           ; e9 72 ff                    ; 0xc2538 vgabios.c:1669
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc253b vgabios.c:1671
    pop di                                    ; 5f                          ; 0xc253e
    pop si                                    ; 5e                          ; 0xc253f
    pop bp                                    ; 5d                          ; 0xc2540
    retn                                      ; c3                          ; 0xc2541
  ; disGetNextSymbol 0xc2542 LB 0x1d81 -> off=0x0 cb=0000000000000162 uValue=00000000000c2542 'biosfn_write_char_only'
biosfn_write_char_only:                      ; 0xc2542 LB 0x162
    push bp                                   ; 55                          ; 0xc2542 vgabios.c:1674
    mov bp, sp                                ; 89 e5                       ; 0xc2543
    push si                                   ; 56                          ; 0xc2545
    push di                                   ; 57                          ; 0xc2546
    sub sp, strict byte 00016h                ; 83 ec 16                    ; 0xc2547
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc254a
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc254d
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc2550
    mov si, cx                                ; 89 ce                       ; 0xc2553
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc2555 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2558
    mov es, ax                                ; 8e c0                       ; 0xc255b
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc255d
    xor ah, ah                                ; 30 e4                       ; 0xc2560 vgabios.c:1682
    call 035f7h                               ; e8 92 10                    ; 0xc2562
    mov cl, al                                ; 88 c1                       ; 0xc2565
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc2567
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc256a vgabios.c:1683
    je near 0269dh                            ; 0f 84 2d 01                 ; 0xc256c
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc2570 vgabios.c:1686
    lea bx, [bp-01ah]                         ; 8d 5e e6                    ; 0xc2573
    lea dx, [bp-018h]                         ; 8d 56 e8                    ; 0xc2576
    call 00a93h                               ; e8 17 e5                    ; 0xc2579
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc257c vgabios.c:1687
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc257f
    mov dx, word [bp-01ah]                    ; 8b 56 e6                    ; 0xc2582
    xor dl, dl                                ; 30 d2                       ; 0xc2585
    shr dx, 008h                              ; c1 ea 08                    ; 0xc2587
    mov byte [bp-00ah], dl                    ; 88 56 f6                    ; 0xc258a
    mov bx, 00084h                            ; bb 84 00                    ; 0xc258d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2590
    mov es, ax                                ; 8e c0                       ; 0xc2593
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2595
    xor ah, ah                                ; 30 e4                       ; 0xc2598 vgabios.c:48
    mov di, ax                                ; 89 c7                       ; 0xc259a
    inc di                                    ; 47                          ; 0xc259c
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc259d vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc25a0
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc25a3 vgabios.c:58
    xor ch, ch                                ; 30 ed                       ; 0xc25a6 vgabios.c:1693
    mov bx, cx                                ; 89 cb                       ; 0xc25a8
    sal bx, 003h                              ; c1 e3 03                    ; 0xc25aa
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc25ad
    jne short 025f1h                          ; 75 3d                       ; 0xc25b2
    imul di, ax                               ; 0f af f8                    ; 0xc25b4 vgabios.c:1696
    add di, di                                ; 01 ff                       ; 0xc25b7
    or di, 000ffh                             ; 81 cf ff 00                 ; 0xc25b9
    movzx bx, byte [bp-00eh]                  ; 0f b6 5e f2                 ; 0xc25bd
    inc di                                    ; 47                          ; 0xc25c1
    imul bx, di                               ; 0f af df                    ; 0xc25c2
    xor dh, dh                                ; 30 f6                       ; 0xc25c5
    imul ax, dx                               ; 0f af c2                    ; 0xc25c7
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc25ca
    add ax, dx                                ; 01 d0                       ; 0xc25ce
    add ax, ax                                ; 01 c0                       ; 0xc25d0
    add bx, ax                                ; 01 c3                       ; 0xc25d2
    dec si                                    ; 4e                          ; 0xc25d4 vgabios.c:1698
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc25d5
    je near 0269dh                            ; 0f 84 c1 00                 ; 0xc25d8
    movzx di, byte [bp-012h]                  ; 0f b6 7e ee                 ; 0xc25dc vgabios.c:1699
    sal di, 003h                              ; c1 e7 03                    ; 0xc25e0
    mov es, [di+047b0h]                       ; 8e 85 b0 47                 ; 0xc25e3 vgabios.c:50
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc25e7
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc25ea
    inc bx                                    ; 43                          ; 0xc25ed vgabios.c:1700
    inc bx                                    ; 43                          ; 0xc25ee
    jmp short 025d4h                          ; eb e3                       ; 0xc25ef vgabios.c:1701
    mov di, cx                                ; 89 cf                       ; 0xc25f1 vgabios.c:1706
    movzx ax, byte [di+0482ch]                ; 0f b6 85 2c 48              ; 0xc25f3
    mov di, ax                                ; 89 c7                       ; 0xc25f8
    sal di, 006h                              ; c1 e7 06                    ; 0xc25fa
    mov al, byte [di+04842h]                  ; 8a 85 42 48                 ; 0xc25fd
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2601
    mov al, byte [bx+047afh]                  ; 8a 87 af 47                 ; 0xc2604 vgabios.c:1707
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc2608
    dec si                                    ; 4e                          ; 0xc260b vgabios.c:1708
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc260c
    je near 0269dh                            ; 0f 84 8a 00                 ; 0xc260f
    movzx bx, byte [bp-012h]                  ; 0f b6 5e ee                 ; 0xc2613 vgabios.c:1710
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2617
    mov bl, byte [bx+047aeh]                  ; 8a 9f ae 47                 ; 0xc261a
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc261e
    jc short 02631h                           ; 72 0e                       ; 0xc2621
    jbe short 02638h                          ; 76 13                       ; 0xc2623
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2625
    je short 0267fh                           ; 74 55                       ; 0xc2628
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc262a
    je short 0263ch                           ; 74 0d                       ; 0xc262d
    jmp short 02697h                          ; eb 66                       ; 0xc262f
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2631
    je short 02660h                           ; 74 2a                       ; 0xc2634
    jmp short 02697h                          ; eb 5f                       ; 0xc2636
    or byte [bp-006h], 001h                   ; 80 4e fa 01                 ; 0xc2638 vgabios.c:1713
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc263c vgabios.c:1715
    push ax                                   ; 50                          ; 0xc2640
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc2641
    push ax                                   ; 50                          ; 0xc2645
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc2646
    push ax                                   ; 50                          ; 0xc264a
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc264b
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc264f
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc2653
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2657
    call 0217ch                               ; e8 1e fb                    ; 0xc265b
    jmp short 02697h                          ; eb 37                       ; 0xc265e vgabios.c:1716
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc2660 vgabios.c:1718
    push ax                                   ; 50                          ; 0xc2664
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc2665
    push ax                                   ; 50                          ; 0xc2669
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc266a
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc266e
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc2672
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2676
    call 0227bh                               ; e8 fe fb                    ; 0xc267a
    jmp short 02697h                          ; eb 18                       ; 0xc267d vgabios.c:1719
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc267f vgabios.c:1721
    push ax                                   ; 50                          ; 0xc2683
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc2684
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc2688
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc268c
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2690
    call 02358h                               ; e8 c1 fc                    ; 0xc2694
    inc byte [bp-010h]                        ; fe 46 f0                    ; 0xc2697 vgabios.c:1728
    jmp near 0260bh                           ; e9 6e ff                    ; 0xc269a vgabios.c:1729
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc269d vgabios.c:1731
    pop di                                    ; 5f                          ; 0xc26a0
    pop si                                    ; 5e                          ; 0xc26a1
    pop bp                                    ; 5d                          ; 0xc26a2
    retn                                      ; c3                          ; 0xc26a3
  ; disGetNextSymbol 0xc26a4 LB 0x1c1f -> off=0x0 cb=0000000000000165 uValue=00000000000c26a4 'biosfn_write_pixel'
biosfn_write_pixel:                          ; 0xc26a4 LB 0x165
    push bp                                   ; 55                          ; 0xc26a4 vgabios.c:1734
    mov bp, sp                                ; 89 e5                       ; 0xc26a5
    push si                                   ; 56                          ; 0xc26a7
    push ax                                   ; 50                          ; 0xc26a8
    push ax                                   ; 50                          ; 0xc26a9
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc26aa
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc26ad
    mov dx, bx                                ; 89 da                       ; 0xc26b0
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc26b2 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc26b5
    mov es, ax                                ; 8e c0                       ; 0xc26b8
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc26ba
    xor ah, ah                                ; 30 e4                       ; 0xc26bd vgabios.c:1741
    call 035f7h                               ; e8 35 0f                    ; 0xc26bf
    mov ah, al                                ; 88 c4                       ; 0xc26c2
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc26c4 vgabios.c:1742
    je near 027e4h                            ; 0f 84 1a 01                 ; 0xc26c6
    movzx bx, al                              ; 0f b6 d8                    ; 0xc26ca vgabios.c:1743
    sal bx, 003h                              ; c1 e3 03                    ; 0xc26cd
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc26d0
    je near 027e4h                            ; 0f 84 0b 01                 ; 0xc26d5
    mov al, byte [bx+047aeh]                  ; 8a 87 ae 47                 ; 0xc26d9 vgabios.c:1745
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc26dd
    jc short 026f0h                           ; 72 0f                       ; 0xc26df
    jbe short 026f7h                          ; 76 14                       ; 0xc26e1
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc26e3
    je near 027eah                            ; 0f 84 01 01                 ; 0xc26e5
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc26e9
    je short 026f7h                           ; 74 0a                       ; 0xc26eb
    jmp near 027e4h                           ; e9 f4 00                    ; 0xc26ed
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc26f0
    je short 02766h                           ; 74 72                       ; 0xc26f2
    jmp near 027e4h                           ; e9 ed 00                    ; 0xc26f4
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc26f7 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc26fa
    mov es, ax                                ; 8e c0                       ; 0xc26fd
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc26ff
    imul ax, cx                               ; 0f af c1                    ; 0xc2702 vgabios.c:58
    mov bx, dx                                ; 89 d3                       ; 0xc2705
    shr bx, 003h                              ; c1 eb 03                    ; 0xc2707
    add bx, ax                                ; 01 c3                       ; 0xc270a
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc270c vgabios.c:57
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc270f
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc2712 vgabios.c:58
    imul ax, cx                               ; 0f af c1                    ; 0xc2716
    add bx, ax                                ; 01 c3                       ; 0xc2719
    mov cl, dl                                ; 88 d1                       ; 0xc271b vgabios.c:1751
    and cl, 007h                              ; 80 e1 07                    ; 0xc271d
    mov ax, 00080h                            ; b8 80 00                    ; 0xc2720
    sar ax, CL                                ; d3 f8                       ; 0xc2723
    xor ah, ah                                ; 30 e4                       ; 0xc2725 vgabios.c:1752
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2727
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc272a
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc272c
    out DX, ax                                ; ef                          ; 0xc272f
    mov ax, 00205h                            ; b8 05 02                    ; 0xc2730 vgabios.c:1753
    out DX, ax                                ; ef                          ; 0xc2733
    mov dx, bx                                ; 89 da                       ; 0xc2734 vgabios.c:1754
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2736
    call 0361eh                               ; e8 e2 0e                    ; 0xc2739
    test byte [bp-006h], 080h                 ; f6 46 fa 80                 ; 0xc273c vgabios.c:1755
    je short 02749h                           ; 74 07                       ; 0xc2740
    mov ax, 01803h                            ; b8 03 18                    ; 0xc2742 vgabios.c:1757
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2745
    out DX, ax                                ; ef                          ; 0xc2748
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2749 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc274c
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc274e
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2751
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc2754 vgabios.c:1760
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2757
    out DX, ax                                ; ef                          ; 0xc275a
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc275b vgabios.c:1761
    out DX, ax                                ; ef                          ; 0xc275e
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc275f vgabios.c:1762
    out DX, ax                                ; ef                          ; 0xc2762
    jmp near 027e4h                           ; e9 7e 00                    ; 0xc2763 vgabios.c:1763
    mov si, cx                                ; 89 ce                       ; 0xc2766 vgabios.c:1765
    shr si, 1                                 ; d1 ee                       ; 0xc2768
    imul si, si, strict byte 00050h           ; 6b f6 50                    ; 0xc276a
    cmp al, byte [bx+047afh]                  ; 3a 87 af 47                 ; 0xc276d
    jne short 0277ah                          ; 75 07                       ; 0xc2771
    mov bx, dx                                ; 89 d3                       ; 0xc2773 vgabios.c:1767
    shr bx, 002h                              ; c1 eb 02                    ; 0xc2775
    jmp short 0277fh                          ; eb 05                       ; 0xc2778 vgabios.c:1769
    mov bx, dx                                ; 89 d3                       ; 0xc277a vgabios.c:1771
    shr bx, 003h                              ; c1 eb 03                    ; 0xc277c
    add bx, si                                ; 01 f3                       ; 0xc277f
    test cl, 001h                             ; f6 c1 01                    ; 0xc2781 vgabios.c:1773
    je short 02789h                           ; 74 03                       ; 0xc2784
    add bh, 020h                              ; 80 c7 20                    ; 0xc2786
    mov cx, 0b800h                            ; b9 00 b8                    ; 0xc2789 vgabios.c:47
    mov es, cx                                ; 8e c1                       ; 0xc278c
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc278e
    movzx si, ah                              ; 0f b6 f4                    ; 0xc2791 vgabios.c:1775
    sal si, 003h                              ; c1 e6 03                    ; 0xc2794
    cmp byte [si+047afh], 002h                ; 80 bc af 47 02              ; 0xc2797
    jne short 027b5h                          ; 75 17                       ; 0xc279c
    mov ah, dl                                ; 88 d4                       ; 0xc279e vgabios.c:1777
    and ah, 003h                              ; 80 e4 03                    ; 0xc27a0
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc27a3
    sub cl, ah                                ; 28 e1                       ; 0xc27a5
    add cl, cl                                ; 00 c9                       ; 0xc27a7
    mov dh, byte [bp-006h]                    ; 8a 76 fa                    ; 0xc27a9
    and dh, 003h                              ; 80 e6 03                    ; 0xc27ac
    sal dh, CL                                ; d2 e6                       ; 0xc27af
    mov DL, strict byte 003h                  ; b2 03                       ; 0xc27b1 vgabios.c:1778
    jmp short 027c8h                          ; eb 13                       ; 0xc27b3 vgabios.c:1780
    mov ah, dl                                ; 88 d4                       ; 0xc27b5 vgabios.c:1782
    and ah, 007h                              ; 80 e4 07                    ; 0xc27b7
    mov CL, strict byte 007h                  ; b1 07                       ; 0xc27ba
    sub cl, ah                                ; 28 e1                       ; 0xc27bc
    mov dh, byte [bp-006h]                    ; 8a 76 fa                    ; 0xc27be
    and dh, 001h                              ; 80 e6 01                    ; 0xc27c1
    sal dh, CL                                ; d2 e6                       ; 0xc27c4
    mov DL, strict byte 001h                  ; b2 01                       ; 0xc27c6 vgabios.c:1783
    sal dl, CL                                ; d2 e2                       ; 0xc27c8
    test byte [bp-006h], 080h                 ; f6 46 fa 80                 ; 0xc27ca vgabios.c:1785
    je short 027d4h                           ; 74 04                       ; 0xc27ce
    xor al, dh                                ; 30 f0                       ; 0xc27d0 vgabios.c:1787
    jmp short 027dch                          ; eb 08                       ; 0xc27d2 vgabios.c:1789
    mov ah, dl                                ; 88 d4                       ; 0xc27d4 vgabios.c:1791
    not ah                                    ; f6 d4                       ; 0xc27d6
    and al, ah                                ; 20 e0                       ; 0xc27d8
    or al, dh                                 ; 08 f0                       ; 0xc27da vgabios.c:1792
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc27dc vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc27df
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc27e1
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc27e4 vgabios.c:1795
    pop si                                    ; 5e                          ; 0xc27e7
    pop bp                                    ; 5d                          ; 0xc27e8
    retn                                      ; c3                          ; 0xc27e9
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc27ea vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc27ed
    mov es, ax                                ; 8e c0                       ; 0xc27f0
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc27f2
    sal ax, 003h                              ; c1 e0 03                    ; 0xc27f5 vgabios.c:58
    imul ax, cx                               ; 0f af c1                    ; 0xc27f8
    mov bx, dx                                ; 89 d3                       ; 0xc27fb
    add bx, ax                                ; 01 c3                       ; 0xc27fd
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc27ff vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2802
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2804
    jmp short 027e1h                          ; eb d8                       ; 0xc2807
  ; disGetNextSymbol 0xc2809 LB 0x1aba -> off=0x0 cb=000000000000024a uValue=00000000000c2809 'biosfn_write_teletype'
biosfn_write_teletype:                       ; 0xc2809 LB 0x24a
    push bp                                   ; 55                          ; 0xc2809 vgabios.c:1808
    mov bp, sp                                ; 89 e5                       ; 0xc280a
    push si                                   ; 56                          ; 0xc280c
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc280d
    mov ch, al                                ; 88 c5                       ; 0xc2810
    mov byte [bp-00ah], dl                    ; 88 56 f6                    ; 0xc2812
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc2815
    cmp dl, 0ffh                              ; 80 fa ff                    ; 0xc2818 vgabios.c:1816
    jne short 0282bh                          ; 75 0e                       ; 0xc281b
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc281d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2820
    mov es, ax                                ; 8e c0                       ; 0xc2823
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2825
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2828 vgabios.c:48
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc282b vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc282e
    mov es, ax                                ; 8e c0                       ; 0xc2831
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2833
    xor ah, ah                                ; 30 e4                       ; 0xc2836 vgabios.c:1821
    call 035f7h                               ; e8 bc 0d                    ; 0xc2838
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc283b
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc283e vgabios.c:1822
    je near 02a4dh                            ; 0f 84 09 02                 ; 0xc2840
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc2844 vgabios.c:1825
    lea bx, [bp-012h]                         ; 8d 5e ee                    ; 0xc2848
    lea dx, [bp-014h]                         ; 8d 56 ec                    ; 0xc284b
    call 00a93h                               ; e8 42 e2                    ; 0xc284e
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc2851 vgabios.c:1826
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2854
    mov ax, word [bp-012h]                    ; 8b 46 ee                    ; 0xc2857
    xor al, al                                ; 30 c0                       ; 0xc285a
    shr ax, 008h                              ; c1 e8 08                    ; 0xc285c
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc285f
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2862 vgabios.c:47
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2865
    mov es, dx                                ; 8e c2                       ; 0xc2868
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc286a
    xor dh, dh                                ; 30 f6                       ; 0xc286d vgabios.c:48
    inc dx                                    ; 42                          ; 0xc286f
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc2870
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2873 vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc2876
    mov word [bp-010h], dx                    ; 89 56 f0                    ; 0xc2879 vgabios.c:58
    cmp ch, 008h                              ; 80 fd 08                    ; 0xc287c vgabios.c:1832
    jc short 0288fh                           ; 72 0e                       ; 0xc287f
    jbe short 02898h                          ; 76 15                       ; 0xc2881
    cmp ch, 00dh                              ; 80 fd 0d                    ; 0xc2883
    je short 028aeh                           ; 74 26                       ; 0xc2886
    cmp ch, 00ah                              ; 80 fd 0a                    ; 0xc2888
    je short 028a6h                           ; 74 19                       ; 0xc288b
    jmp short 028b5h                          ; eb 26                       ; 0xc288d
    cmp ch, 007h                              ; 80 fd 07                    ; 0xc288f
    je near 029a9h                            ; 0f 84 13 01                 ; 0xc2892
    jmp short 028b5h                          ; eb 1d                       ; 0xc2896
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2898 vgabios.c:1839
    jbe near 029a9h                           ; 0f 86 09 01                 ; 0xc289c
    dec byte [bp-006h]                        ; fe 4e fa                    ; 0xc28a0
    jmp near 029a9h                           ; e9 03 01                    ; 0xc28a3 vgabios.c:1840
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc28a6 vgabios.c:1843
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc28a8
    jmp near 029a9h                           ; e9 fb 00                    ; 0xc28ab vgabios.c:1844
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc28ae vgabios.c:1847
    jmp near 029a9h                           ; e9 f4 00                    ; 0xc28b2 vgabios.c:1848
    movzx si, byte [bp-00ch]                  ; 0f b6 76 f4                 ; 0xc28b5 vgabios.c:1852
    mov bx, si                                ; 89 f3                       ; 0xc28b9
    sal bx, 003h                              ; c1 e3 03                    ; 0xc28bb
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc28be
    jne short 02908h                          ; 75 43                       ; 0xc28c3
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc28c5 vgabios.c:1855
    imul ax, word [bp-00eh]                   ; 0f af 46 f2                 ; 0xc28c8
    add ax, ax                                ; 01 c0                       ; 0xc28cc
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc28ce
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc28d0
    mov si, ax                                ; 89 c6                       ; 0xc28d4
    inc si                                    ; 46                          ; 0xc28d6
    imul si, dx                               ; 0f af f2                    ; 0xc28d7
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc28da
    imul ax, word [bp-010h]                   ; 0f af 46 f0                 ; 0xc28de
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc28e2
    add ax, dx                                ; 01 d0                       ; 0xc28e6
    add ax, ax                                ; 01 c0                       ; 0xc28e8
    add si, ax                                ; 01 c6                       ; 0xc28ea
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc28ec vgabios.c:50
    mov byte [es:si], ch                      ; 26 88 2c                    ; 0xc28f0
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc28f3 vgabios.c:1860
    jne near 02996h                           ; 0f 85 9c 00                 ; 0xc28f6
    inc si                                    ; 46                          ; 0xc28fa vgabios.c:1861
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc28fb vgabios.c:50
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc28ff
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2902
    jmp near 02996h                           ; e9 8e 00                    ; 0xc2905 vgabios.c:1863
    movzx si, byte [si+0482ch]                ; 0f b6 b4 2c 48              ; 0xc2908 vgabios.c:1866
    sal si, 006h                              ; c1 e6 06                    ; 0xc290d
    mov ah, byte [si+04842h]                  ; 8a a4 42 48                 ; 0xc2910
    mov dl, byte [bx+047afh]                  ; 8a 97 af 47                 ; 0xc2914 vgabios.c:1867
    mov al, byte [bx+047aeh]                  ; 8a 87 ae 47                 ; 0xc2918 vgabios.c:1868
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc291c
    jc short 0292ch                           ; 72 0c                       ; 0xc291e
    jbe short 02932h                          ; 76 10                       ; 0xc2920
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc2922
    je short 0297dh                           ; 74 57                       ; 0xc2924
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc2926
    je short 02936h                           ; 74 0c                       ; 0xc2928
    jmp short 02996h                          ; eb 6a                       ; 0xc292a
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc292c
    je short 0295ch                           ; 74 2c                       ; 0xc292e
    jmp short 02996h                          ; eb 64                       ; 0xc2930
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc2932 vgabios.c:1871
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc2936 vgabios.c:1873
    push dx                                   ; 52                          ; 0xc293a
    movzx ax, ah                              ; 0f b6 c4                    ; 0xc293b
    push ax                                   ; 50                          ; 0xc293e
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc293f
    push ax                                   ; 50                          ; 0xc2943
    movzx bx, byte [bp-004h]                  ; 0f b6 5e fc                 ; 0xc2944
    movzx si, byte [bp-006h]                  ; 0f b6 76 fa                 ; 0xc2948
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc294c
    movzx ax, ch                              ; 0f b6 c5                    ; 0xc2950
    mov cx, bx                                ; 89 d9                       ; 0xc2953
    mov bx, si                                ; 89 f3                       ; 0xc2955
    call 0217ch                               ; e8 22 f8                    ; 0xc2957
    jmp short 02996h                          ; eb 3a                       ; 0xc295a vgabios.c:1874
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc295c vgabios.c:1876
    push ax                                   ; 50                          ; 0xc295f
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2960
    push ax                                   ; 50                          ; 0xc2964
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc2965
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc2969
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc296d
    movzx si, ch                              ; 0f b6 f5                    ; 0xc2971
    mov cx, ax                                ; 89 c1                       ; 0xc2974
    mov ax, si                                ; 89 f0                       ; 0xc2976
    call 0227bh                               ; e8 00 f9                    ; 0xc2978
    jmp short 02996h                          ; eb 19                       ; 0xc297b vgabios.c:1877
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc297d vgabios.c:1879
    push ax                                   ; 50                          ; 0xc2981
    movzx si, byte [bp-004h]                  ; 0f b6 76 fc                 ; 0xc2982
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc2986
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc298a
    movzx ax, ch                              ; 0f b6 c5                    ; 0xc298e
    mov cx, si                                ; 89 f1                       ; 0xc2991
    call 02358h                               ; e8 c2 f9                    ; 0xc2993
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc2996 vgabios.c:1887
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2999 vgabios.c:1889
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc299d
    jne short 029a9h                          ; 75 07                       ; 0xc29a0
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc29a2 vgabios.c:1890
    inc byte [bp-004h]                        ; fe 46 fc                    ; 0xc29a6 vgabios.c:1891
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc29a9 vgabios.c:1896
    cmp ax, word [bp-00eh]                    ; 3b 46 f2                    ; 0xc29ad
    jne near 02a31h                           ; 0f 85 7d 00                 ; 0xc29b0
    movzx bx, byte [bp-00ch]                  ; 0f b6 5e f4                 ; 0xc29b4 vgabios.c:1898
    sal bx, 003h                              ; c1 e3 03                    ; 0xc29b8
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc29bb
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc29be
    mov ah, byte [bp-010h]                    ; 8a 66 f0                    ; 0xc29c0
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc29c3
    cmp byte [bx+047adh], 000h                ; 80 bf ad 47 00              ; 0xc29c5
    jne short 02a14h                          ; 75 48                       ; 0xc29ca
    mov dx, word [bp-010h]                    ; 8b 56 f0                    ; 0xc29cc vgabios.c:1900
    imul dx, word [bp-00eh]                   ; 0f af 56 f2                 ; 0xc29cf
    add dx, dx                                ; 01 d2                       ; 0xc29d3
    or dl, 0ffh                               ; 80 ca ff                    ; 0xc29d5
    movzx si, byte [bp-00ah]                  ; 0f b6 76 f6                 ; 0xc29d8
    inc dx                                    ; 42                          ; 0xc29dc
    imul si, dx                               ; 0f af f2                    ; 0xc29dd
    movzx dx, byte [bp-004h]                  ; 0f b6 56 fc                 ; 0xc29e0
    dec dx                                    ; 4a                          ; 0xc29e4
    mov cx, word [bp-010h]                    ; 8b 4e f0                    ; 0xc29e5
    imul cx, dx                               ; 0f af ca                    ; 0xc29e8
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc29eb
    add dx, cx                                ; 01 ca                       ; 0xc29ef
    add dx, dx                                ; 01 d2                       ; 0xc29f1
    add si, dx                                ; 01 d6                       ; 0xc29f3
    inc si                                    ; 46                          ; 0xc29f5 vgabios.c:1901
    mov es, [bx+047b0h]                       ; 8e 87 b0 47                 ; 0xc29f6 vgabios.c:45
    mov bl, byte [es:si]                      ; 26 8a 1c                    ; 0xc29fa
    push strict byte 00001h                   ; 6a 01                       ; 0xc29fd vgabios.c:1902
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc29ff
    push dx                                   ; 52                          ; 0xc2a03
    movzx dx, ah                              ; 0f b6 d4                    ; 0xc2a04
    push dx                                   ; 52                          ; 0xc2a07
    xor ah, ah                                ; 30 e4                       ; 0xc2a08
    push ax                                   ; 50                          ; 0xc2a0a
    movzx dx, bl                              ; 0f b6 d3                    ; 0xc2a0b
    xor cx, cx                                ; 31 c9                       ; 0xc2a0e
    xor bx, bx                                ; 31 db                       ; 0xc2a10
    jmp short 02a28h                          ; eb 14                       ; 0xc2a12 vgabios.c:1904
    push strict byte 00001h                   ; 6a 01                       ; 0xc2a14 vgabios.c:1906
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc2a16
    push dx                                   ; 52                          ; 0xc2a1a
    movzx dx, ah                              ; 0f b6 d4                    ; 0xc2a1b
    push dx                                   ; 52                          ; 0xc2a1e
    xor ah, ah                                ; 30 e4                       ; 0xc2a1f
    push ax                                   ; 50                          ; 0xc2a21
    xor cx, cx                                ; 31 c9                       ; 0xc2a22
    xor bx, bx                                ; 31 db                       ; 0xc2a24
    xor dx, dx                                ; 31 d2                       ; 0xc2a26
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc2a28
    call 01b54h                               ; e8 26 f1                    ; 0xc2a2b
    dec byte [bp-004h]                        ; fe 4e fc                    ; 0xc2a2e vgabios.c:1908
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc2a31 vgabios.c:1912
    mov word [bp-012h], ax                    ; 89 46 ee                    ; 0xc2a35
    sal word [bp-012h], 008h                  ; c1 66 ee 08                 ; 0xc2a38
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2a3c
    add word [bp-012h], ax                    ; 01 46 ee                    ; 0xc2a40
    mov dx, word [bp-012h]                    ; 8b 56 ee                    ; 0xc2a43 vgabios.c:1913
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc2a46
    call 01230h                               ; e8 e3 e7                    ; 0xc2a4a
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2a4d vgabios.c:1914
    pop si                                    ; 5e                          ; 0xc2a50
    pop bp                                    ; 5d                          ; 0xc2a51
    retn                                      ; c3                          ; 0xc2a52
  ; disGetNextSymbol 0xc2a53 LB 0x1870 -> off=0x0 cb=0000000000000033 uValue=00000000000c2a53 'get_font_access'
get_font_access:                             ; 0xc2a53 LB 0x33
    push bp                                   ; 55                          ; 0xc2a53 vgabios.c:1917
    mov bp, sp                                ; 89 e5                       ; 0xc2a54
    push dx                                   ; 52                          ; 0xc2a56
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2a57 vgabios.c:1919
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2a5a
    out DX, ax                                ; ef                          ; 0xc2a5d
    mov AL, strict byte 006h                  ; b0 06                       ; 0xc2a5e vgabios.c:1920
    out DX, AL                                ; ee                          ; 0xc2a60
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc2a61 vgabios.c:1921
    in AL, DX                                 ; ec                          ; 0xc2a64
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2a65
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2a67
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc2a6a
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2a6c
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2a6f
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2a71
    out DX, ax                                ; ef                          ; 0xc2a74
    mov ax, 00402h                            ; b8 02 04                    ; 0xc2a75 vgabios.c:1922
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2a78
    out DX, ax                                ; ef                          ; 0xc2a7b
    mov ax, 00604h                            ; b8 04 06                    ; 0xc2a7c vgabios.c:1923
    out DX, ax                                ; ef                          ; 0xc2a7f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2a80 vgabios.c:1924
    pop dx                                    ; 5a                          ; 0xc2a83
    pop bp                                    ; 5d                          ; 0xc2a84
    retn                                      ; c3                          ; 0xc2a85
  ; disGetNextSymbol 0xc2a86 LB 0x183d -> off=0x0 cb=0000000000000030 uValue=00000000000c2a86 'release_font_access'
release_font_access:                         ; 0xc2a86 LB 0x30
    push bp                                   ; 55                          ; 0xc2a86 vgabios.c:1926
    mov bp, sp                                ; 89 e5                       ; 0xc2a87
    push dx                                   ; 52                          ; 0xc2a89
    mov dx, 003cch                            ; ba cc 03                    ; 0xc2a8a vgabios.c:1928
    in AL, DX                                 ; ec                          ; 0xc2a8d
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2a8e
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2a90
    sal ax, 002h                              ; c1 e0 02                    ; 0xc2a93
    or AL, strict byte 00ah                   ; 0c 0a                       ; 0xc2a96
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2a98
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2a9b
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2a9d
    out DX, ax                                ; ef                          ; 0xc2aa0
    mov ax, 01005h                            ; b8 05 10                    ; 0xc2aa1 vgabios.c:1929
    out DX, ax                                ; ef                          ; 0xc2aa4
    mov ax, 00302h                            ; b8 02 03                    ; 0xc2aa5 vgabios.c:1930
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2aa8
    out DX, ax                                ; ef                          ; 0xc2aab
    mov ax, 00204h                            ; b8 04 02                    ; 0xc2aac vgabios.c:1931
    out DX, ax                                ; ef                          ; 0xc2aaf
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2ab0 vgabios.c:1932
    pop dx                                    ; 5a                          ; 0xc2ab3
    pop bp                                    ; 5d                          ; 0xc2ab4
    retn                                      ; c3                          ; 0xc2ab5
  ; disGetNextSymbol 0xc2ab6 LB 0x180d -> off=0x0 cb=00000000000000c7 uValue=00000000000c2ab6 'set_scan_lines'
set_scan_lines:                              ; 0xc2ab6 LB 0xc7
    push bp                                   ; 55                          ; 0xc2ab6 vgabios.c:1934
    mov bp, sp                                ; 89 e5                       ; 0xc2ab7
    push bx                                   ; 53                          ; 0xc2ab9
    push cx                                   ; 51                          ; 0xc2aba
    push dx                                   ; 52                          ; 0xc2abb
    push si                                   ; 56                          ; 0xc2abc
    push di                                   ; 57                          ; 0xc2abd
    push ax                                   ; 50                          ; 0xc2abe
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2abf
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc2ac2 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2ac5
    mov es, ax                                ; 8e c0                       ; 0xc2ac8
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc2aca
    mov bx, cx                                ; 89 cb                       ; 0xc2acd vgabios.c:58
    mov AL, strict byte 009h                  ; b0 09                       ; 0xc2acf vgabios.c:1940
    mov dx, cx                                ; 89 ca                       ; 0xc2ad1
    out DX, AL                                ; ee                          ; 0xc2ad3
    lea dx, [bx+001h]                         ; 8d 57 01                    ; 0xc2ad4 vgabios.c:1941
    in AL, DX                                 ; ec                          ; 0xc2ad7
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2ad8
    and AL, strict byte 0e0h                  ; 24 e0                       ; 0xc2ada vgabios.c:1942
    mov ah, byte [bp-00ch]                    ; 8a 66 f4                    ; 0xc2adc
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc2adf
    or al, ah                                 ; 08 e0                       ; 0xc2ae1
    out DX, AL                                ; ee                          ; 0xc2ae3 vgabios.c:1943
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc2ae4 vgabios.c:1948
    mov cx, ax                                ; 89 c1                       ; 0xc2ae8
    sal cx, 008h                              ; c1 e1 08                    ; 0xc2aea
    dec ax                                    ; 48                          ; 0xc2aed
    sub cx, 00200h                            ; 81 e9 00 02                 ; 0xc2aee
    or cx, ax                                 ; 09 c1                       ; 0xc2af2
    cmp byte [bp-00ch], 00eh                  ; 80 7e f4 0e                 ; 0xc2af4 vgabios.c:1949
    jc short 02afeh                           ; 72 04                       ; 0xc2af8
    sub cx, 00101h                            ; 81 e9 01 01                 ; 0xc2afa vgabios.c:1950
    mov ax, cx                                ; 89 c8                       ; 0xc2afe vgabios.c:1952
    xor al, cl                                ; 30 c8                       ; 0xc2b00
    or AL, strict byte 00ah                   ; 0c 0a                       ; 0xc2b02
    mov dx, bx                                ; 89 da                       ; 0xc2b04
    out DX, ax                                ; ef                          ; 0xc2b06
    mov ax, cx                                ; 89 c8                       ; 0xc2b07 vgabios.c:1953
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2b09
    or AL, strict byte 00bh                   ; 0c 0b                       ; 0xc2b0c
    out DX, ax                                ; ef                          ; 0xc2b0e
    mov si, strict word 00060h                ; be 60 00                    ; 0xc2b0f vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2b12
    mov es, ax                                ; 8e c0                       ; 0xc2b15
    mov word [es:si], cx                      ; 26 89 0c                    ; 0xc2b17
    movzx di, byte [bp-00ch]                  ; 0f b6 7e f4                 ; 0xc2b1a vgabios.c:1956
    mov si, 00085h                            ; be 85 00                    ; 0xc2b1e vgabios.c:62
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc2b21
    mov AL, strict byte 012h                  ; b0 12                       ; 0xc2b24 vgabios.c:1957
    out DX, AL                                ; ee                          ; 0xc2b26
    lea cx, [bx+001h]                         ; 8d 4f 01                    ; 0xc2b27 vgabios.c:1958
    mov dx, cx                                ; 89 ca                       ; 0xc2b2a
    in AL, DX                                 ; ec                          ; 0xc2b2c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2b2d
    mov si, ax                                ; 89 c6                       ; 0xc2b2f
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc2b31 vgabios.c:1959
    mov dx, bx                                ; 89 da                       ; 0xc2b33
    out DX, AL                                ; ee                          ; 0xc2b35
    mov dx, cx                                ; 89 ca                       ; 0xc2b36 vgabios.c:1960
    in AL, DX                                 ; ec                          ; 0xc2b38
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2b39
    mov ah, al                                ; 88 c4                       ; 0xc2b3b vgabios.c:1961
    and ah, 002h                              ; 80 e4 02                    ; 0xc2b3d
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc2b40
    sal bx, 007h                              ; c1 e3 07                    ; 0xc2b43
    and AL, strict byte 040h                  ; 24 40                       ; 0xc2b46
    xor ah, ah                                ; 30 e4                       ; 0xc2b48
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2b4a
    add ax, bx                                ; 01 d8                       ; 0xc2b4d
    inc ax                                    ; 40                          ; 0xc2b4f
    add ax, si                                ; 01 f0                       ; 0xc2b50
    xor dx, cx                                ; 31 ca                       ; 0xc2b52 vgabios.c:1962
    div di                                    ; f7 f7                       ; 0xc2b54
    mov cl, al                                ; 88 c1                       ; 0xc2b56 vgabios.c:1963
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc2b58
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2b5a vgabios.c:52
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc2b5d
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2b60 vgabios.c:57
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc2b63
    xor ah, ah                                ; 30 e4                       ; 0xc2b66 vgabios.c:1965
    imul ax, bx                               ; 0f af c3                    ; 0xc2b68
    add ax, ax                                ; 01 c0                       ; 0xc2b6b
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc2b6d vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc2b70
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc2b73 vgabios.c:1966
    pop di                                    ; 5f                          ; 0xc2b76
    pop si                                    ; 5e                          ; 0xc2b77
    pop dx                                    ; 5a                          ; 0xc2b78
    pop cx                                    ; 59                          ; 0xc2b79
    pop bx                                    ; 5b                          ; 0xc2b7a
    pop bp                                    ; 5d                          ; 0xc2b7b
    retn                                      ; c3                          ; 0xc2b7c
  ; disGetNextSymbol 0xc2b7d LB 0x1746 -> off=0x0 cb=0000000000000022 uValue=00000000000c2b7d 'biosfn_set_font_block'
biosfn_set_font_block:                       ; 0xc2b7d LB 0x22
    push bp                                   ; 55                          ; 0xc2b7d vgabios.c:1968
    mov bp, sp                                ; 89 e5                       ; 0xc2b7e
    push bx                                   ; 53                          ; 0xc2b80
    push dx                                   ; 52                          ; 0xc2b81
    mov bl, al                                ; 88 c3                       ; 0xc2b82
    mov ax, 00100h                            ; b8 00 01                    ; 0xc2b84 vgabios.c:1970
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2b87
    out DX, ax                                ; ef                          ; 0xc2b8a
    movzx ax, bl                              ; 0f b6 c3                    ; 0xc2b8b vgabios.c:1971
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2b8e
    or AL, strict byte 003h                   ; 0c 03                       ; 0xc2b91
    out DX, ax                                ; ef                          ; 0xc2b93
    mov ax, 00300h                            ; b8 00 03                    ; 0xc2b94 vgabios.c:1972
    out DX, ax                                ; ef                          ; 0xc2b97
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2b98 vgabios.c:1973
    pop dx                                    ; 5a                          ; 0xc2b9b
    pop bx                                    ; 5b                          ; 0xc2b9c
    pop bp                                    ; 5d                          ; 0xc2b9d
    retn                                      ; c3                          ; 0xc2b9e
  ; disGetNextSymbol 0xc2b9f LB 0x1724 -> off=0x0 cb=0000000000000075 uValue=00000000000c2b9f 'load_text_patch'
load_text_patch:                             ; 0xc2b9f LB 0x75
    push bp                                   ; 55                          ; 0xc2b9f vgabios.c:1975
    mov bp, sp                                ; 89 e5                       ; 0xc2ba0
    push si                                   ; 56                          ; 0xc2ba2
    push di                                   ; 57                          ; 0xc2ba3
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc2ba4
    push ax                                   ; 50                          ; 0xc2ba7
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc2ba8
    call 02a53h                               ; e8 a5 fe                    ; 0xc2bab vgabios.c:1980
    mov al, bl                                ; 88 d8                       ; 0xc2bae vgabios.c:1982
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2bb0
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2bb2
    sal cx, 00eh                              ; c1 e1 0e                    ; 0xc2bb5
    mov al, bl                                ; 88 d8                       ; 0xc2bb8
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2bba
    xor ah, ah                                ; 30 e4                       ; 0xc2bbc
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2bbe
    add cx, ax                                ; 01 c1                       ; 0xc2bc1
    mov word [bp-00ch], cx                    ; 89 4e f4                    ; 0xc2bc3
    mov bx, dx                                ; 89 d3                       ; 0xc2bc6 vgabios.c:1983
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2bc8
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc2bcb
    inc dx                                    ; 42                          ; 0xc2bce vgabios.c:1984
    mov word [bp-008h], dx                    ; 89 56 f8                    ; 0xc2bcf
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc2bd2 vgabios.c:1985
    cmp byte [es:bx], 000h                    ; 26 80 3f 00                 ; 0xc2bd5
    je short 02c0ah                           ; 74 2f                       ; 0xc2bd9
    movzx ax, byte [es:bx]                    ; 26 0f b6 07                 ; 0xc2bdb vgabios.c:1986
    sal ax, 005h                              ; c1 e0 05                    ; 0xc2bdf
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc2be2
    add di, ax                                ; 01 c7                       ; 0xc2be5
    movzx cx, byte [bp-006h]                  ; 0f b6 4e fa                 ; 0xc2be7 vgabios.c:1987
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc2beb
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2bee
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2bf1
    mov es, ax                                ; 8e c0                       ; 0xc2bf4
    jcxz 02bfeh                               ; e3 06                       ; 0xc2bf6
    push DS                                   ; 1e                          ; 0xc2bf8
    mov ds, dx                                ; 8e da                       ; 0xc2bf9
    rep movsb                                 ; f3 a4                       ; 0xc2bfb
    pop DS                                    ; 1f                          ; 0xc2bfd
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2bfe vgabios.c:1988
    inc ax                                    ; 40                          ; 0xc2c02
    add word [bp-008h], ax                    ; 01 46 f8                    ; 0xc2c03
    add bx, ax                                ; 01 c3                       ; 0xc2c06 vgabios.c:1989
    jmp short 02bd2h                          ; eb c8                       ; 0xc2c08 vgabios.c:1990
    call 02a86h                               ; e8 79 fe                    ; 0xc2c0a vgabios.c:1992
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2c0d vgabios.c:1993
    pop di                                    ; 5f                          ; 0xc2c10
    pop si                                    ; 5e                          ; 0xc2c11
    pop bp                                    ; 5d                          ; 0xc2c12
    retn                                      ; c3                          ; 0xc2c13
  ; disGetNextSymbol 0xc2c14 LB 0x16af -> off=0x0 cb=000000000000007c uValue=00000000000c2c14 'biosfn_load_text_user_pat'
biosfn_load_text_user_pat:                   ; 0xc2c14 LB 0x7c
    push bp                                   ; 55                          ; 0xc2c14 vgabios.c:1995
    mov bp, sp                                ; 89 e5                       ; 0xc2c15
    push si                                   ; 56                          ; 0xc2c17
    push di                                   ; 57                          ; 0xc2c18
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2c19
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2c1c
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc2c1f
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc2c22
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc2c25
    call 02a53h                               ; e8 28 fe                    ; 0xc2c28 vgabios.c:2000
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2c2b vgabios.c:2001
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2c2e
    xor ah, ah                                ; 30 e4                       ; 0xc2c30
    mov bx, ax                                ; 89 c3                       ; 0xc2c32
    sal bx, 00eh                              ; c1 e3 0e                    ; 0xc2c34
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2c37
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2c3a
    xor ah, ah                                ; 30 e4                       ; 0xc2c3c
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2c3e
    add bx, ax                                ; 01 c3                       ; 0xc2c41
    mov word [bp-00eh], bx                    ; 89 5e f2                    ; 0xc2c43
    xor bx, bx                                ; 31 db                       ; 0xc2c46 vgabios.c:2002
    cmp bx, word [bp-00ah]                    ; 3b 5e f6                    ; 0xc2c48
    jnc short 02c77h                          ; 73 2a                       ; 0xc2c4b
    movzx cx, byte [bp+008h]                  ; 0f b6 4e 08                 ; 0xc2c4d vgabios.c:2004
    mov si, bx                                ; 89 de                       ; 0xc2c51
    imul si, cx                               ; 0f af f1                    ; 0xc2c53
    add si, word [bp-008h]                    ; 03 76 f8                    ; 0xc2c56
    mov di, word [bp+004h]                    ; 8b 7e 04                    ; 0xc2c59 vgabios.c:2005
    add di, bx                                ; 01 df                       ; 0xc2c5c
    sal di, 005h                              ; c1 e7 05                    ; 0xc2c5e
    add di, word [bp-00eh]                    ; 03 7e f2                    ; 0xc2c61
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc2c64 vgabios.c:2006
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2c67
    mov es, ax                                ; 8e c0                       ; 0xc2c6a
    jcxz 02c74h                               ; e3 06                       ; 0xc2c6c
    push DS                                   ; 1e                          ; 0xc2c6e
    mov ds, dx                                ; 8e da                       ; 0xc2c6f
    rep movsb                                 ; f3 a4                       ; 0xc2c71
    pop DS                                    ; 1f                          ; 0xc2c73
    inc bx                                    ; 43                          ; 0xc2c74 vgabios.c:2007
    jmp short 02c48h                          ; eb d1                       ; 0xc2c75
    call 02a86h                               ; e8 0c fe                    ; 0xc2c77 vgabios.c:2008
    cmp byte [bp-006h], 010h                  ; 80 7e fa 10                 ; 0xc2c7a vgabios.c:2009
    jc short 02c87h                           ; 72 07                       ; 0xc2c7e
    movzx ax, byte [bp+008h]                  ; 0f b6 46 08                 ; 0xc2c80 vgabios.c:2011
    call 02ab6h                               ; e8 2f fe                    ; 0xc2c84
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2c87 vgabios.c:2013
    pop di                                    ; 5f                          ; 0xc2c8a
    pop si                                    ; 5e                          ; 0xc2c8b
    pop bp                                    ; 5d                          ; 0xc2c8c
    retn 00006h                               ; c2 06 00                    ; 0xc2c8d
  ; disGetNextSymbol 0xc2c90 LB 0x1633 -> off=0x0 cb=0000000000000016 uValue=00000000000c2c90 'biosfn_load_gfx_8_8_chars'
biosfn_load_gfx_8_8_chars:                   ; 0xc2c90 LB 0x16
    push bp                                   ; 55                          ; 0xc2c90 vgabios.c:2015
    mov bp, sp                                ; 89 e5                       ; 0xc2c91
    push bx                                   ; 53                          ; 0xc2c93
    push cx                                   ; 51                          ; 0xc2c94
    mov bx, dx                                ; 89 d3                       ; 0xc2c95 vgabios.c:2017
    mov cx, ax                                ; 89 c1                       ; 0xc2c97
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc2c99
    call 009f0h                               ; e8 51 dd                    ; 0xc2c9c
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2c9f vgabios.c:2018
    pop cx                                    ; 59                          ; 0xc2ca2
    pop bx                                    ; 5b                          ; 0xc2ca3
    pop bp                                    ; 5d                          ; 0xc2ca4
    retn                                      ; c3                          ; 0xc2ca5
  ; disGetNextSymbol 0xc2ca6 LB 0x161d -> off=0x0 cb=0000000000000049 uValue=00000000000c2ca6 'set_gfx_font'
set_gfx_font:                                ; 0xc2ca6 LB 0x49
    push bp                                   ; 55                          ; 0xc2ca6 vgabios.c:2020
    mov bp, sp                                ; 89 e5                       ; 0xc2ca7
    push si                                   ; 56                          ; 0xc2ca9
    push di                                   ; 57                          ; 0xc2caa
    mov si, dx                                ; 89 d6                       ; 0xc2cab
    mov di, bx                                ; 89 df                       ; 0xc2cad
    mov dl, cl                                ; 88 ca                       ; 0xc2caf
    mov bx, ax                                ; 89 c3                       ; 0xc2cb1 vgabios.c:2024
    mov cx, si                                ; 89 f1                       ; 0xc2cb3
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc2cb5
    call 009f0h                               ; e8 35 dd                    ; 0xc2cb8
    test dl, dl                               ; 84 d2                       ; 0xc2cbb vgabios.c:2025
    je short 02cd0h                           ; 74 11                       ; 0xc2cbd
    cmp dl, 003h                              ; 80 fa 03                    ; 0xc2cbf vgabios.c:2026
    jbe short 02cc6h                          ; 76 02                       ; 0xc2cc2
    mov DL, strict byte 002h                  ; b2 02                       ; 0xc2cc4 vgabios.c:2027
    movzx bx, dl                              ; 0f b6 da                    ; 0xc2cc6 vgabios.c:2028
    mov al, byte [bx+07dfbh]                  ; 8a 87 fb 7d                 ; 0xc2cc9
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2ccd
    mov bx, 00085h                            ; bb 85 00                    ; 0xc2cd0 vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2cd3
    mov es, ax                                ; 8e c0                       ; 0xc2cd6
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc2cd8
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc2cdb vgabios.c:2033
    dec ax                                    ; 48                          ; 0xc2cdf
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2ce0 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc2ce3
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2ce6 vgabios.c:2034
    pop di                                    ; 5f                          ; 0xc2ce9
    pop si                                    ; 5e                          ; 0xc2cea
    pop bp                                    ; 5d                          ; 0xc2ceb
    retn 00002h                               ; c2 02 00                    ; 0xc2cec
  ; disGetNextSymbol 0xc2cef LB 0x15d4 -> off=0x0 cb=000000000000001c uValue=00000000000c2cef 'biosfn_load_gfx_user_chars'
biosfn_load_gfx_user_chars:                  ; 0xc2cef LB 0x1c
    push bp                                   ; 55                          ; 0xc2cef vgabios.c:2036
    mov bp, sp                                ; 89 e5                       ; 0xc2cf0
    push si                                   ; 56                          ; 0xc2cf2
    mov si, ax                                ; 89 c6                       ; 0xc2cf3
    mov ax, dx                                ; 89 d0                       ; 0xc2cf5
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc2cf7 vgabios.c:2039
    push dx                                   ; 52                          ; 0xc2cfb
    xor ch, ch                                ; 30 ed                       ; 0xc2cfc
    mov dx, si                                ; 89 f2                       ; 0xc2cfe
    call 02ca6h                               ; e8 a3 ff                    ; 0xc2d00
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2d03 vgabios.c:2040
    pop si                                    ; 5e                          ; 0xc2d06
    pop bp                                    ; 5d                          ; 0xc2d07
    retn 00002h                               ; c2 02 00                    ; 0xc2d08
  ; disGetNextSymbol 0xc2d0b LB 0x15b8 -> off=0x0 cb=000000000000001e uValue=00000000000c2d0b 'biosfn_load_gfx_8_14_chars'
biosfn_load_gfx_8_14_chars:                  ; 0xc2d0b LB 0x1e
    push bp                                   ; 55                          ; 0xc2d0b vgabios.c:2045
    mov bp, sp                                ; 89 e5                       ; 0xc2d0c
    push bx                                   ; 53                          ; 0xc2d0e
    push cx                                   ; 51                          ; 0xc2d0f
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc2d10 vgabios.c:2047
    push cx                                   ; 51                          ; 0xc2d13
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2d14
    mov bx, strict word 0000eh                ; bb 0e 00                    ; 0xc2d17
    mov ax, 05d6ah                            ; b8 6a 5d                    ; 0xc2d1a
    mov dx, ds                                ; 8c da                       ; 0xc2d1d
    call 02ca6h                               ; e8 84 ff                    ; 0xc2d1f
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2d22 vgabios.c:2048
    pop cx                                    ; 59                          ; 0xc2d25
    pop bx                                    ; 5b                          ; 0xc2d26
    pop bp                                    ; 5d                          ; 0xc2d27
    retn                                      ; c3                          ; 0xc2d28
  ; disGetNextSymbol 0xc2d29 LB 0x159a -> off=0x0 cb=000000000000001e uValue=00000000000c2d29 'biosfn_load_gfx_8_8_dd_chars'
biosfn_load_gfx_8_8_dd_chars:                ; 0xc2d29 LB 0x1e
    push bp                                   ; 55                          ; 0xc2d29 vgabios.c:2049
    mov bp, sp                                ; 89 e5                       ; 0xc2d2a
    push bx                                   ; 53                          ; 0xc2d2c
    push cx                                   ; 51                          ; 0xc2d2d
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc2d2e vgabios.c:2051
    push cx                                   ; 51                          ; 0xc2d31
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2d32
    mov bx, strict word 00008h                ; bb 08 00                    ; 0xc2d35
    mov ax, 0556ah                            ; b8 6a 55                    ; 0xc2d38
    mov dx, ds                                ; 8c da                       ; 0xc2d3b
    call 02ca6h                               ; e8 66 ff                    ; 0xc2d3d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2d40 vgabios.c:2052
    pop cx                                    ; 59                          ; 0xc2d43
    pop bx                                    ; 5b                          ; 0xc2d44
    pop bp                                    ; 5d                          ; 0xc2d45
    retn                                      ; c3                          ; 0xc2d46
  ; disGetNextSymbol 0xc2d47 LB 0x157c -> off=0x0 cb=000000000000001e uValue=00000000000c2d47 'biosfn_load_gfx_8_16_chars'
biosfn_load_gfx_8_16_chars:                  ; 0xc2d47 LB 0x1e
    push bp                                   ; 55                          ; 0xc2d47 vgabios.c:2053
    mov bp, sp                                ; 89 e5                       ; 0xc2d48
    push bx                                   ; 53                          ; 0xc2d4a
    push cx                                   ; 51                          ; 0xc2d4b
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc2d4c vgabios.c:2055
    push cx                                   ; 51                          ; 0xc2d4f
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2d50
    mov bx, strict word 00010h                ; bb 10 00                    ; 0xc2d53
    mov ax, 06b6ah                            ; b8 6a 6b                    ; 0xc2d56
    mov dx, ds                                ; 8c da                       ; 0xc2d59
    call 02ca6h                               ; e8 48 ff                    ; 0xc2d5b
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2d5e vgabios.c:2056
    pop cx                                    ; 59                          ; 0xc2d61
    pop bx                                    ; 5b                          ; 0xc2d62
    pop bp                                    ; 5d                          ; 0xc2d63
    retn                                      ; c3                          ; 0xc2d64
  ; disGetNextSymbol 0xc2d65 LB 0x155e -> off=0x0 cb=0000000000000005 uValue=00000000000c2d65 'biosfn_alternate_prtsc'
biosfn_alternate_prtsc:                      ; 0xc2d65 LB 0x5
    push bp                                   ; 55                          ; 0xc2d65 vgabios.c:2058
    mov bp, sp                                ; 89 e5                       ; 0xc2d66
    pop bp                                    ; 5d                          ; 0xc2d68 vgabios.c:2063
    retn                                      ; c3                          ; 0xc2d69
  ; disGetNextSymbol 0xc2d6a LB 0x1559 -> off=0x0 cb=0000000000000032 uValue=00000000000c2d6a 'biosfn_set_txt_lines'
biosfn_set_txt_lines:                        ; 0xc2d6a LB 0x32
    push bx                                   ; 53                          ; 0xc2d6a vgabios.c:2065
    push si                                   ; 56                          ; 0xc2d6b
    push bp                                   ; 55                          ; 0xc2d6c
    mov bp, sp                                ; 89 e5                       ; 0xc2d6d
    mov bl, al                                ; 88 c3                       ; 0xc2d6f
    mov si, 00089h                            ; be 89 00                    ; 0xc2d71 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2d74
    mov es, ax                                ; 8e c0                       ; 0xc2d77
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2d79
    and AL, strict byte 06fh                  ; 24 6f                       ; 0xc2d7c vgabios.c:2071
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2d7e vgabios.c:2073
    je short 02d8bh                           ; 74 08                       ; 0xc2d81
    test bl, bl                               ; 84 db                       ; 0xc2d83
    jne short 02d8dh                          ; 75 06                       ; 0xc2d85
    or AL, strict byte 080h                   ; 0c 80                       ; 0xc2d87 vgabios.c:2076
    jmp short 02d8dh                          ; eb 02                       ; 0xc2d89 vgabios.c:2077
    or AL, strict byte 010h                   ; 0c 10                       ; 0xc2d8b vgabios.c:2079
    mov bx, 00089h                            ; bb 89 00                    ; 0xc2d8d vgabios.c:52
    mov si, strict word 00040h                ; be 40 00                    ; 0xc2d90
    mov es, si                                ; 8e c6                       ; 0xc2d93
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2d95
    pop bp                                    ; 5d                          ; 0xc2d98 vgabios.c:2083
    pop si                                    ; 5e                          ; 0xc2d99
    pop bx                                    ; 5b                          ; 0xc2d9a
    retn                                      ; c3                          ; 0xc2d9b
  ; disGetNextSymbol 0xc2d9c LB 0x1527 -> off=0x0 cb=0000000000000005 uValue=00000000000c2d9c 'biosfn_switch_video_interface'
biosfn_switch_video_interface:               ; 0xc2d9c LB 0x5
    push bp                                   ; 55                          ; 0xc2d9c vgabios.c:2086
    mov bp, sp                                ; 89 e5                       ; 0xc2d9d
    pop bp                                    ; 5d                          ; 0xc2d9f vgabios.c:2091
    retn                                      ; c3                          ; 0xc2da0
  ; disGetNextSymbol 0xc2da1 LB 0x1522 -> off=0x0 cb=0000000000000005 uValue=00000000000c2da1 'biosfn_enable_video_refresh_control'
biosfn_enable_video_refresh_control:         ; 0xc2da1 LB 0x5
    push bp                                   ; 55                          ; 0xc2da1 vgabios.c:2092
    mov bp, sp                                ; 89 e5                       ; 0xc2da2
    pop bp                                    ; 5d                          ; 0xc2da4 vgabios.c:2097
    retn                                      ; c3                          ; 0xc2da5
  ; disGetNextSymbol 0xc2da6 LB 0x151d -> off=0x0 cb=0000000000000096 uValue=00000000000c2da6 'biosfn_write_string'
biosfn_write_string:                         ; 0xc2da6 LB 0x96
    push bp                                   ; 55                          ; 0xc2da6 vgabios.c:2100
    mov bp, sp                                ; 89 e5                       ; 0xc2da7
    push si                                   ; 56                          ; 0xc2da9
    push di                                   ; 57                          ; 0xc2daa
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2dab
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2dae
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc2db1
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc2db4
    mov si, cx                                ; 89 ce                       ; 0xc2db7
    mov di, word [bp+00ah]                    ; 8b 7e 0a                    ; 0xc2db9
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc2dbc vgabios.c:2107
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc2dbf
    lea dx, [bp-00ch]                         ; 8d 56 f4                    ; 0xc2dc2
    call 00a93h                               ; e8 cb dc                    ; 0xc2dc5
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc2dc8 vgabios.c:2110
    jne short 02ddfh                          ; 75 11                       ; 0xc2dcc
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2dce vgabios.c:2111
    mov byte [bp+006h], al                    ; 88 46 06                    ; 0xc2dd1
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2dd4 vgabios.c:2112
    xor al, al                                ; 30 c0                       ; 0xc2dd7
    shr ax, 008h                              ; c1 e8 08                    ; 0xc2dd9
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2ddc
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc2ddf vgabios.c:2115
    sal dx, 008h                              ; c1 e2 08                    ; 0xc2de3
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc2de6
    add dx, ax                                ; 01 c2                       ; 0xc2dea
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2dec vgabios.c:2116
    call 01230h                               ; e8 3d e4                    ; 0xc2df0
    dec si                                    ; 4e                          ; 0xc2df3 vgabios.c:2118
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc2df4
    je short 02e23h                           ; 74 2a                       ; 0xc2df7
    mov bx, di                                ; 89 fb                       ; 0xc2df9 vgabios.c:2120
    inc di                                    ; 47                          ; 0xc2dfb
    mov es, [bp+008h]                         ; 8e 46 08                    ; 0xc2dfc vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2dff
    test byte [bp-008h], 002h                 ; f6 46 f8 02                 ; 0xc2e02 vgabios.c:2121
    je short 02e11h                           ; 74 09                       ; 0xc2e06
    mov bx, di                                ; 89 fb                       ; 0xc2e08 vgabios.c:2122
    inc di                                    ; 47                          ; 0xc2e0a
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc2e0b vgabios.c:47
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc2e0e vgabios.c:48
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc2e11 vgabios.c:2124
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc2e15
    xor ah, ah                                ; 30 e4                       ; 0xc2e19
    mov cx, strict word 00003h                ; b9 03 00                    ; 0xc2e1b
    call 02809h                               ; e8 e8 f9                    ; 0xc2e1e
    jmp short 02df3h                          ; eb d0                       ; 0xc2e21 vgabios.c:2125
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc2e23 vgabios.c:2128
    jne short 02e33h                          ; 75 0a                       ; 0xc2e27
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2e29 vgabios.c:2129
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2e2c
    call 01230h                               ; e8 fd e3                    ; 0xc2e30
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2e33 vgabios.c:2130
    pop di                                    ; 5f                          ; 0xc2e36
    pop si                                    ; 5e                          ; 0xc2e37
    pop bp                                    ; 5d                          ; 0xc2e38
    retn 00008h                               ; c2 08 00                    ; 0xc2e39
  ; disGetNextSymbol 0xc2e3c LB 0x1487 -> off=0x0 cb=00000000000001f2 uValue=00000000000c2e3c 'biosfn_read_state_info'
biosfn_read_state_info:                      ; 0xc2e3c LB 0x1f2
    push bp                                   ; 55                          ; 0xc2e3c vgabios.c:2133
    mov bp, sp                                ; 89 e5                       ; 0xc2e3d
    push cx                                   ; 51                          ; 0xc2e3f
    push si                                   ; 56                          ; 0xc2e40
    push di                                   ; 57                          ; 0xc2e41
    push ax                                   ; 50                          ; 0xc2e42
    push ax                                   ; 50                          ; 0xc2e43
    push dx                                   ; 52                          ; 0xc2e44
    mov si, strict word 00049h                ; be 49 00                    ; 0xc2e45 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2e48
    mov es, ax                                ; 8e c0                       ; 0xc2e4b
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2e4d
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2e50 vgabios.c:48
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc2e53 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc2e56
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc2e59 vgabios.c:58
    mov ax, ds                                ; 8c d8                       ; 0xc2e5c vgabios.c:2144
    mov es, dx                                ; 8e c2                       ; 0xc2e5e vgabios.c:72
    mov word [es:bx], 05500h                  ; 26 c7 07 00 55              ; 0xc2e60
    mov [es:bx+002h], ds                      ; 26 8c 5f 02                 ; 0xc2e65
    lea di, [bx+004h]                         ; 8d 7f 04                    ; 0xc2e69 vgabios.c:2149
    mov cx, strict word 0001eh                ; b9 1e 00                    ; 0xc2e6c
    mov si, strict word 00049h                ; be 49 00                    ; 0xc2e6f
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2e72
    jcxz 02e7dh                               ; e3 06                       ; 0xc2e75
    push DS                                   ; 1e                          ; 0xc2e77
    mov ds, dx                                ; 8e da                       ; 0xc2e78
    rep movsb                                 ; f3 a4                       ; 0xc2e7a
    pop DS                                    ; 1f                          ; 0xc2e7c
    mov si, 00084h                            ; be 84 00                    ; 0xc2e7d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2e80
    mov es, ax                                ; 8e c0                       ; 0xc2e83
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2e85
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc2e88 vgabios.c:48
    lea si, [bx+022h]                         ; 8d 77 22                    ; 0xc2e8a
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2e8d vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2e90
    lea di, [bx+023h]                         ; 8d 7f 23                    ; 0xc2e93 vgabios.c:2151
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc2e96
    mov si, 00085h                            ; be 85 00                    ; 0xc2e99
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2e9c
    jcxz 02ea7h                               ; e3 06                       ; 0xc2e9f
    push DS                                   ; 1e                          ; 0xc2ea1
    mov ds, dx                                ; 8e da                       ; 0xc2ea2
    rep movsb                                 ; f3 a4                       ; 0xc2ea4
    pop DS                                    ; 1f                          ; 0xc2ea6
    mov si, 0008ah                            ; be 8a 00                    ; 0xc2ea7 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2eaa
    mov es, ax                                ; 8e c0                       ; 0xc2ead
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2eaf
    lea si, [bx+025h]                         ; 8d 77 25                    ; 0xc2eb2 vgabios.c:48
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2eb5 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2eb8
    lea si, [bx+026h]                         ; 8d 77 26                    ; 0xc2ebb vgabios.c:2154
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2ebe vgabios.c:52
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2ec2 vgabios.c:2155
    mov word [es:si], strict word 00010h      ; 26 c7 04 10 00              ; 0xc2ec5 vgabios.c:62
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc2eca vgabios.c:2156
    mov byte [es:si], 008h                    ; 26 c6 04 08                 ; 0xc2ecd vgabios.c:52
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc2ed1 vgabios.c:2157
    mov byte [es:si], 002h                    ; 26 c6 04 02                 ; 0xc2ed4 vgabios.c:52
    lea si, [bx+02bh]                         ; 8d 77 2b                    ; 0xc2ed8 vgabios.c:2158
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2edb vgabios.c:52
    lea si, [bx+02ch]                         ; 8d 77 2c                    ; 0xc2edf vgabios.c:2159
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2ee2 vgabios.c:52
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc2ee6 vgabios.c:2160
    mov byte [es:si], 021h                    ; 26 c6 04 21                 ; 0xc2ee9 vgabios.c:52
    lea si, [bx+031h]                         ; 8d 77 31                    ; 0xc2eed vgabios.c:2161
    mov byte [es:si], 003h                    ; 26 c6 04 03                 ; 0xc2ef0 vgabios.c:52
    lea si, [bx+032h]                         ; 8d 77 32                    ; 0xc2ef4 vgabios.c:2162
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2ef7 vgabios.c:52
    mov si, 00089h                            ; be 89 00                    ; 0xc2efb vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2efe
    mov es, ax                                ; 8e c0                       ; 0xc2f01
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2f03
    mov ah, al                                ; 88 c4                       ; 0xc2f06 vgabios.c:2167
    and ah, 080h                              ; 80 e4 80                    ; 0xc2f08
    movzx si, ah                              ; 0f b6 f4                    ; 0xc2f0b
    sar si, 006h                              ; c1 fe 06                    ; 0xc2f0e
    and AL, strict byte 010h                  ; 24 10                       ; 0xc2f11
    xor ah, ah                                ; 30 e4                       ; 0xc2f13
    sar ax, 004h                              ; c1 f8 04                    ; 0xc2f15
    or ax, si                                 ; 09 f0                       ; 0xc2f18
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc2f1a vgabios.c:2168
    je short 02f30h                           ; 74 11                       ; 0xc2f1d
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc2f1f
    je short 02f2ch                           ; 74 08                       ; 0xc2f22
    test ax, ax                               ; 85 c0                       ; 0xc2f24
    jne short 02f30h                          ; 75 08                       ; 0xc2f26
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc2f28 vgabios.c:2169
    jmp short 02f32h                          ; eb 06                       ; 0xc2f2a
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc2f2c vgabios.c:2170
    jmp short 02f32h                          ; eb 02                       ; 0xc2f2e
    xor al, al                                ; 30 c0                       ; 0xc2f30 vgabios.c:2172
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc2f32 vgabios.c:2174
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f35 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2f38
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2f3b vgabios.c:2177
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc2f3e
    jc short 02f61h                           ; 72 1f                       ; 0xc2f40
    cmp AL, strict byte 012h                  ; 3c 12                       ; 0xc2f42
    jnbe short 02f61h                         ; 77 1b                       ; 0xc2f44
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc2f46 vgabios.c:2178
    test ax, ax                               ; 85 c0                       ; 0xc2f49
    je short 02fa3h                           ; 74 56                       ; 0xc2f4b
    mov si, ax                                ; 89 c6                       ; 0xc2f4d vgabios.c:2179
    shr si, 002h                              ; c1 ee 02                    ; 0xc2f4f
    mov ax, 04000h                            ; b8 00 40                    ; 0xc2f52
    xor dx, dx                                ; 31 d2                       ; 0xc2f55
    div si                                    ; f7 f6                       ; 0xc2f57
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc2f59
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2f5c vgabios.c:52
    jmp short 02fa3h                          ; eb 42                       ; 0xc2f5f vgabios.c:2180
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc2f61
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2f64
    cmp AL, strict byte 013h                  ; 3c 13                       ; 0xc2f67
    jne short 02f7ch                          ; 75 11                       ; 0xc2f69
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f6b vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc2f6e
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2f72 vgabios.c:2182
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc2f75 vgabios.c:62
    jmp short 02fa3h                          ; eb 27                       ; 0xc2f7a vgabios.c:2183
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc2f7c
    jc short 02fa3h                           ; 72 23                       ; 0xc2f7e
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc2f80
    jnbe short 02fa3h                         ; 77 1f                       ; 0xc2f82
    cmp word [bp-00ah], strict byte 00000h    ; 83 7e f6 00                 ; 0xc2f84 vgabios.c:2185
    je short 02f98h                           ; 74 0e                       ; 0xc2f88
    mov ax, 04000h                            ; b8 00 40                    ; 0xc2f8a vgabios.c:2186
    xor dx, dx                                ; 31 d2                       ; 0xc2f8d
    div word [bp-00ah]                        ; f7 76 f6                    ; 0xc2f8f
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f92 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2f95
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2f98 vgabios.c:2187
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f9b vgabios.c:62
    mov word [es:si], strict word 00004h      ; 26 c7 04 04 00              ; 0xc2f9e
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2fa3 vgabios.c:2189
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc2fa6
    je short 02faeh                           ; 74 04                       ; 0xc2fa8
    cmp AL, strict byte 011h                  ; 3c 11                       ; 0xc2faa
    jne short 02fb9h                          ; 75 0b                       ; 0xc2fac
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2fae vgabios.c:2190
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2fb1 vgabios.c:62
    mov word [es:si], strict word 00002h      ; 26 c7 04 02 00              ; 0xc2fb4
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2fb9 vgabios.c:2192
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc2fbc
    jc short 03017h                           ; 72 57                       ; 0xc2fbe
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc2fc0
    je short 03017h                           ; 74 53                       ; 0xc2fc2
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc2fc4 vgabios.c:2193
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2fc7 vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc2fca
    mov si, 00084h                            ; be 84 00                    ; 0xc2fce vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2fd1
    mov es, ax                                ; 8e c0                       ; 0xc2fd4
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2fd6
    movzx di, al                              ; 0f b6 f8                    ; 0xc2fd9 vgabios.c:48
    inc di                                    ; 47                          ; 0xc2fdc
    mov si, 00085h                            ; be 85 00                    ; 0xc2fdd vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2fe0
    xor ah, ah                                ; 30 e4                       ; 0xc2fe3 vgabios.c:48
    imul ax, di                               ; 0f af c7                    ; 0xc2fe5
    cmp ax, 0015eh                            ; 3d 5e 01                    ; 0xc2fe8 vgabios.c:2195
    jc short 02ffbh                           ; 72 0e                       ; 0xc2feb
    jbe short 03004h                          ; 76 15                       ; 0xc2fed
    cmp ax, 001e0h                            ; 3d e0 01                    ; 0xc2fef
    je short 0300ch                           ; 74 18                       ; 0xc2ff2
    cmp ax, 00190h                            ; 3d 90 01                    ; 0xc2ff4
    je short 03008h                           ; 74 0f                       ; 0xc2ff7
    jmp short 0300ch                          ; eb 11                       ; 0xc2ff9
    cmp ax, 000c8h                            ; 3d c8 00                    ; 0xc2ffb
    jne short 0300ch                          ; 75 0c                       ; 0xc2ffe
    xor al, al                                ; 30 c0                       ; 0xc3000 vgabios.c:2196
    jmp short 0300eh                          ; eb 0a                       ; 0xc3002
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc3004 vgabios.c:2197
    jmp short 0300eh                          ; eb 06                       ; 0xc3006
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc3008 vgabios.c:2198
    jmp short 0300eh                          ; eb 02                       ; 0xc300a
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc300c vgabios.c:2200
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc300e vgabios.c:2202
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3011 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3014
    lea di, [bx+033h]                         ; 8d 7f 33                    ; 0xc3017 vgabios.c:2205
    mov cx, strict word 0000dh                ; b9 0d 00                    ; 0xc301a
    xor ax, ax                                ; 31 c0                       ; 0xc301d
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc301f
    jcxz 03026h                               ; e3 02                       ; 0xc3022
    rep stosb                                 ; f3 aa                       ; 0xc3024
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3026 vgabios.c:2206
    pop di                                    ; 5f                          ; 0xc3029
    pop si                                    ; 5e                          ; 0xc302a
    pop cx                                    ; 59                          ; 0xc302b
    pop bp                                    ; 5d                          ; 0xc302c
    retn                                      ; c3                          ; 0xc302d
  ; disGetNextSymbol 0xc302e LB 0x1295 -> off=0x0 cb=0000000000000023 uValue=00000000000c302e 'biosfn_read_video_state_size2'
biosfn_read_video_state_size2:               ; 0xc302e LB 0x23
    push dx                                   ; 52                          ; 0xc302e vgabios.c:2209
    push bp                                   ; 55                          ; 0xc302f
    mov bp, sp                                ; 89 e5                       ; 0xc3030
    mov dx, ax                                ; 89 c2                       ; 0xc3032
    xor ax, ax                                ; 31 c0                       ; 0xc3034 vgabios.c:2213
    test dl, 001h                             ; f6 c2 01                    ; 0xc3036 vgabios.c:2214
    je short 0303eh                           ; 74 03                       ; 0xc3039
    mov ax, strict word 00046h                ; b8 46 00                    ; 0xc303b vgabios.c:2215
    test dl, 002h                             ; f6 c2 02                    ; 0xc303e vgabios.c:2217
    je short 03046h                           ; 74 03                       ; 0xc3041
    add ax, strict word 0002ah                ; 05 2a 00                    ; 0xc3043 vgabios.c:2218
    test dl, 004h                             ; f6 c2 04                    ; 0xc3046 vgabios.c:2220
    je short 0304eh                           ; 74 03                       ; 0xc3049
    add ax, 00304h                            ; 05 04 03                    ; 0xc304b vgabios.c:2221
    pop bp                                    ; 5d                          ; 0xc304e vgabios.c:2224
    pop dx                                    ; 5a                          ; 0xc304f
    retn                                      ; c3                          ; 0xc3050
  ; disGetNextSymbol 0xc3051 LB 0x1272 -> off=0x0 cb=0000000000000018 uValue=00000000000c3051 'vga_get_video_state_size'
vga_get_video_state_size:                    ; 0xc3051 LB 0x18
    push bp                                   ; 55                          ; 0xc3051 vgabios.c:2226
    mov bp, sp                                ; 89 e5                       ; 0xc3052
    push bx                                   ; 53                          ; 0xc3054
    mov bx, dx                                ; 89 d3                       ; 0xc3055
    call 0302eh                               ; e8 d4 ff                    ; 0xc3057 vgabios.c:2229
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc305a
    shr ax, 006h                              ; c1 e8 06                    ; 0xc305d
    mov word [ss:bx], ax                      ; 36 89 07                    ; 0xc3060
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3063 vgabios.c:2230
    pop bx                                    ; 5b                          ; 0xc3066
    pop bp                                    ; 5d                          ; 0xc3067
    retn                                      ; c3                          ; 0xc3068
  ; disGetNextSymbol 0xc3069 LB 0x125a -> off=0x0 cb=00000000000002d6 uValue=00000000000c3069 'biosfn_save_video_state'
biosfn_save_video_state:                     ; 0xc3069 LB 0x2d6
    push bp                                   ; 55                          ; 0xc3069 vgabios.c:2232
    mov bp, sp                                ; 89 e5                       ; 0xc306a
    push cx                                   ; 51                          ; 0xc306c
    push si                                   ; 56                          ; 0xc306d
    push di                                   ; 57                          ; 0xc306e
    push ax                                   ; 50                          ; 0xc306f
    push ax                                   ; 50                          ; 0xc3070
    push ax                                   ; 50                          ; 0xc3071
    mov cx, dx                                ; 89 d1                       ; 0xc3072
    mov si, strict word 00063h                ; be 63 00                    ; 0xc3074 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3077
    mov es, ax                                ; 8e c0                       ; 0xc307a
    mov di, word [es:si]                      ; 26 8b 3c                    ; 0xc307c
    mov si, di                                ; 89 fe                       ; 0xc307f vgabios.c:58
    test byte [bp-00ch], 001h                 ; f6 46 f4 01                 ; 0xc3081 vgabios.c:2237
    je near 0319ch                            ; 0f 84 13 01                 ; 0xc3085
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3089 vgabios.c:2238
    in AL, DX                                 ; ec                          ; 0xc308c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc308d
    mov es, cx                                ; 8e c1                       ; 0xc308f vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3091
    inc bx                                    ; 43                          ; 0xc3094 vgabios.c:2238
    mov dx, di                                ; 89 fa                       ; 0xc3095
    in AL, DX                                 ; ec                          ; 0xc3097
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3098
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc309a vgabios.c:52
    inc bx                                    ; 43                          ; 0xc309d vgabios.c:2239
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc309e
    in AL, DX                                 ; ec                          ; 0xc30a1
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30a2
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30a4 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc30a7 vgabios.c:2240
    mov dx, 003dah                            ; ba da 03                    ; 0xc30a8
    in AL, DX                                 ; ec                          ; 0xc30ab
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30ac
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc30ae vgabios.c:2242
    in AL, DX                                 ; ec                          ; 0xc30b1
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30b2
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc30b4
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc30b7 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30ba
    inc bx                                    ; 43                          ; 0xc30bd vgabios.c:2243
    mov dx, 003cah                            ; ba ca 03                    ; 0xc30be
    in AL, DX                                 ; ec                          ; 0xc30c1
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30c2
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30c4 vgabios.c:52
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc30c7 vgabios.c:2246
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc30ca
    add bx, ax                                ; 01 c3                       ; 0xc30cd vgabios.c:2244
    jmp short 030d7h                          ; eb 06                       ; 0xc30cf
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc30d1
    jnbe short 030efh                         ; 77 18                       ; 0xc30d5
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc30d7 vgabios.c:2247
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc30da
    out DX, AL                                ; ee                          ; 0xc30dd
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc30de vgabios.c:2248
    in AL, DX                                 ; ec                          ; 0xc30e1
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30e2
    mov es, cx                                ; 8e c1                       ; 0xc30e4 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30e6
    inc bx                                    ; 43                          ; 0xc30e9 vgabios.c:2248
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc30ea vgabios.c:2249
    jmp short 030d1h                          ; eb e2                       ; 0xc30ed
    xor al, al                                ; 30 c0                       ; 0xc30ef vgabios.c:2250
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc30f1
    out DX, AL                                ; ee                          ; 0xc30f4
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc30f5 vgabios.c:2251
    in AL, DX                                 ; ec                          ; 0xc30f8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30f9
    mov es, cx                                ; 8e c1                       ; 0xc30fb vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30fd
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3100 vgabios.c:2253
    inc bx                                    ; 43                          ; 0xc3105 vgabios.c:2251
    jmp short 0310eh                          ; eb 06                       ; 0xc3106
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc3108
    jnbe short 03125h                         ; 77 17                       ; 0xc310c
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc310e vgabios.c:2254
    mov dx, si                                ; 89 f2                       ; 0xc3111
    out DX, AL                                ; ee                          ; 0xc3113
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc3114 vgabios.c:2255
    in AL, DX                                 ; ec                          ; 0xc3117
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3118
    mov es, cx                                ; 8e c1                       ; 0xc311a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc311c
    inc bx                                    ; 43                          ; 0xc311f vgabios.c:2255
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3120 vgabios.c:2256
    jmp short 03108h                          ; eb e3                       ; 0xc3123
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3125 vgabios.c:2258
    jmp short 03132h                          ; eb 06                       ; 0xc312a
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc312c
    jnbe short 03156h                         ; 77 24                       ; 0xc3130
    mov dx, 003dah                            ; ba da 03                    ; 0xc3132 vgabios.c:2259
    in AL, DX                                 ; ec                          ; 0xc3135
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3136
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3138 vgabios.c:2260
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc313b
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc313e
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3141
    out DX, AL                                ; ee                          ; 0xc3144
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc3145 vgabios.c:2261
    in AL, DX                                 ; ec                          ; 0xc3148
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3149
    mov es, cx                                ; 8e c1                       ; 0xc314b vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc314d
    inc bx                                    ; 43                          ; 0xc3150 vgabios.c:2261
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3151 vgabios.c:2262
    jmp short 0312ch                          ; eb d6                       ; 0xc3154
    mov dx, 003dah                            ; ba da 03                    ; 0xc3156 vgabios.c:2263
    in AL, DX                                 ; ec                          ; 0xc3159
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc315a
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc315c vgabios.c:2265
    jmp short 03169h                          ; eb 06                       ; 0xc3161
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc3163
    jnbe short 03181h                         ; 77 18                       ; 0xc3167
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3169 vgabios.c:2266
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc316c
    out DX, AL                                ; ee                          ; 0xc316f
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc3170 vgabios.c:2267
    in AL, DX                                 ; ec                          ; 0xc3173
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3174
    mov es, cx                                ; 8e c1                       ; 0xc3176 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3178
    inc bx                                    ; 43                          ; 0xc317b vgabios.c:2267
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc317c vgabios.c:2268
    jmp short 03163h                          ; eb e2                       ; 0xc317f
    mov es, cx                                ; 8e c1                       ; 0xc3181 vgabios.c:62
    mov word [es:bx], si                      ; 26 89 37                    ; 0xc3183
    inc bx                                    ; 43                          ; 0xc3186 vgabios.c:2270
    inc bx                                    ; 43                          ; 0xc3187
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3188 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc318c vgabios.c:2273
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc318d vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3191 vgabios.c:2274
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3192 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3196 vgabios.c:2275
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3197 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc319b vgabios.c:2276
    test byte [bp-00ch], 002h                 ; f6 46 f4 02                 ; 0xc319c vgabios.c:2278
    je near 032e3h                            ; 0f 84 3f 01                 ; 0xc31a0
    mov si, strict word 00049h                ; be 49 00                    ; 0xc31a4 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31a7
    mov es, ax                                ; 8e c0                       ; 0xc31aa
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc31ac
    mov es, cx                                ; 8e c1                       ; 0xc31af vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc31b1
    inc bx                                    ; 43                          ; 0xc31b4 vgabios.c:2279
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc31b5 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31b8
    mov es, ax                                ; 8e c0                       ; 0xc31bb
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc31bd
    mov es, cx                                ; 8e c1                       ; 0xc31c0 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc31c2
    inc bx                                    ; 43                          ; 0xc31c5 vgabios.c:2280
    inc bx                                    ; 43                          ; 0xc31c6
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc31c7 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31ca
    mov es, ax                                ; 8e c0                       ; 0xc31cd
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc31cf
    mov es, cx                                ; 8e c1                       ; 0xc31d2 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc31d4
    inc bx                                    ; 43                          ; 0xc31d7 vgabios.c:2281
    inc bx                                    ; 43                          ; 0xc31d8
    mov si, strict word 00063h                ; be 63 00                    ; 0xc31d9 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31dc
    mov es, ax                                ; 8e c0                       ; 0xc31df
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc31e1
    mov es, cx                                ; 8e c1                       ; 0xc31e4 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc31e6
    inc bx                                    ; 43                          ; 0xc31e9 vgabios.c:2282
    inc bx                                    ; 43                          ; 0xc31ea
    mov si, 00084h                            ; be 84 00                    ; 0xc31eb vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31ee
    mov es, ax                                ; 8e c0                       ; 0xc31f1
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc31f3
    mov es, cx                                ; 8e c1                       ; 0xc31f6 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc31f8
    inc bx                                    ; 43                          ; 0xc31fb vgabios.c:2283
    mov si, 00085h                            ; be 85 00                    ; 0xc31fc vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31ff
    mov es, ax                                ; 8e c0                       ; 0xc3202
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3204
    mov es, cx                                ; 8e c1                       ; 0xc3207 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3209
    inc bx                                    ; 43                          ; 0xc320c vgabios.c:2284
    inc bx                                    ; 43                          ; 0xc320d
    mov si, 00087h                            ; be 87 00                    ; 0xc320e vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3211
    mov es, ax                                ; 8e c0                       ; 0xc3214
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3216
    mov es, cx                                ; 8e c1                       ; 0xc3219 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc321b
    inc bx                                    ; 43                          ; 0xc321e vgabios.c:2285
    mov si, 00088h                            ; be 88 00                    ; 0xc321f vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3222
    mov es, ax                                ; 8e c0                       ; 0xc3225
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3227
    mov es, cx                                ; 8e c1                       ; 0xc322a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc322c
    inc bx                                    ; 43                          ; 0xc322f vgabios.c:2286
    mov si, 00089h                            ; be 89 00                    ; 0xc3230 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3233
    mov es, ax                                ; 8e c0                       ; 0xc3236
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3238
    mov es, cx                                ; 8e c1                       ; 0xc323b vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc323d
    inc bx                                    ; 43                          ; 0xc3240 vgabios.c:2287
    mov si, strict word 00060h                ; be 60 00                    ; 0xc3241 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3244
    mov es, ax                                ; 8e c0                       ; 0xc3247
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3249
    mov es, cx                                ; 8e c1                       ; 0xc324c vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc324e
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3251 vgabios.c:2289
    inc bx                                    ; 43                          ; 0xc3256 vgabios.c:2288
    inc bx                                    ; 43                          ; 0xc3257
    jmp short 03260h                          ; eb 06                       ; 0xc3258
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc325a
    jnc short 0327ch                          ; 73 1c                       ; 0xc325e
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc3260 vgabios.c:2290
    add si, si                                ; 01 f6                       ; 0xc3263
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc3265
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3268 vgabios.c:57
    mov es, ax                                ; 8e c0                       ; 0xc326b
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc326d
    mov es, cx                                ; 8e c1                       ; 0xc3270 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3272
    inc bx                                    ; 43                          ; 0xc3275 vgabios.c:2291
    inc bx                                    ; 43                          ; 0xc3276
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3277 vgabios.c:2292
    jmp short 0325ah                          ; eb de                       ; 0xc327a
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc327c vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc327f
    mov es, ax                                ; 8e c0                       ; 0xc3282
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3284
    mov es, cx                                ; 8e c1                       ; 0xc3287 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3289
    inc bx                                    ; 43                          ; 0xc328c vgabios.c:2293
    inc bx                                    ; 43                          ; 0xc328d
    mov si, strict word 00062h                ; be 62 00                    ; 0xc328e vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3291
    mov es, ax                                ; 8e c0                       ; 0xc3294
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3296
    mov es, cx                                ; 8e c1                       ; 0xc3299 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc329b
    inc bx                                    ; 43                          ; 0xc329e vgabios.c:2294
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc329f vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc32a2
    mov es, ax                                ; 8e c0                       ; 0xc32a4
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc32a6
    mov es, cx                                ; 8e c1                       ; 0xc32a9 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32ab
    inc bx                                    ; 43                          ; 0xc32ae vgabios.c:2296
    inc bx                                    ; 43                          ; 0xc32af
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc32b0 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc32b3
    mov es, ax                                ; 8e c0                       ; 0xc32b5
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc32b7
    mov es, cx                                ; 8e c1                       ; 0xc32ba vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32bc
    inc bx                                    ; 43                          ; 0xc32bf vgabios.c:2297
    inc bx                                    ; 43                          ; 0xc32c0
    mov si, 0010ch                            ; be 0c 01                    ; 0xc32c1 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc32c4
    mov es, ax                                ; 8e c0                       ; 0xc32c6
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc32c8
    mov es, cx                                ; 8e c1                       ; 0xc32cb vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32cd
    inc bx                                    ; 43                          ; 0xc32d0 vgabios.c:2298
    inc bx                                    ; 43                          ; 0xc32d1
    mov si, 0010eh                            ; be 0e 01                    ; 0xc32d2 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc32d5
    mov es, ax                                ; 8e c0                       ; 0xc32d7
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc32d9
    mov es, cx                                ; 8e c1                       ; 0xc32dc vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32de
    inc bx                                    ; 43                          ; 0xc32e1 vgabios.c:2299
    inc bx                                    ; 43                          ; 0xc32e2
    test byte [bp-00ch], 004h                 ; f6 46 f4 04                 ; 0xc32e3 vgabios.c:2301
    je short 03335h                           ; 74 4c                       ; 0xc32e7
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc32e9 vgabios.c:2303
    in AL, DX                                 ; ec                          ; 0xc32ec
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32ed
    mov es, cx                                ; 8e c1                       ; 0xc32ef vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32f1
    inc bx                                    ; 43                          ; 0xc32f4 vgabios.c:2303
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc32f5
    in AL, DX                                 ; ec                          ; 0xc32f8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32f9
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32fb vgabios.c:52
    inc bx                                    ; 43                          ; 0xc32fe vgabios.c:2304
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc32ff
    in AL, DX                                 ; ec                          ; 0xc3302
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3303
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3305 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3308 vgabios.c:2305
    xor al, al                                ; 30 c0                       ; 0xc3309
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc330b
    out DX, AL                                ; ee                          ; 0xc330e
    xor ah, ah                                ; 30 e4                       ; 0xc330f vgabios.c:2308
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3311
    jmp short 0331dh                          ; eb 07                       ; 0xc3314
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc3316
    jnc short 0332eh                          ; 73 11                       ; 0xc331b
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc331d vgabios.c:2309
    in AL, DX                                 ; ec                          ; 0xc3320
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3321
    mov es, cx                                ; 8e c1                       ; 0xc3323 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3325
    inc bx                                    ; 43                          ; 0xc3328 vgabios.c:2309
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3329 vgabios.c:2310
    jmp short 03316h                          ; eb e8                       ; 0xc332c
    mov es, cx                                ; 8e c1                       ; 0xc332e vgabios.c:52
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3330
    inc bx                                    ; 43                          ; 0xc3334 vgabios.c:2311
    mov ax, bx                                ; 89 d8                       ; 0xc3335 vgabios.c:2314
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3337
    pop di                                    ; 5f                          ; 0xc333a
    pop si                                    ; 5e                          ; 0xc333b
    pop cx                                    ; 59                          ; 0xc333c
    pop bp                                    ; 5d                          ; 0xc333d
    retn                                      ; c3                          ; 0xc333e
  ; disGetNextSymbol 0xc333f LB 0xf84 -> off=0x0 cb=00000000000002b8 uValue=00000000000c333f 'biosfn_restore_video_state'
biosfn_restore_video_state:                  ; 0xc333f LB 0x2b8
    push bp                                   ; 55                          ; 0xc333f vgabios.c:2316
    mov bp, sp                                ; 89 e5                       ; 0xc3340
    push cx                                   ; 51                          ; 0xc3342
    push si                                   ; 56                          ; 0xc3343
    push di                                   ; 57                          ; 0xc3344
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc3345
    push ax                                   ; 50                          ; 0xc3348
    mov cx, dx                                ; 89 d1                       ; 0xc3349
    test byte [bp-010h], 001h                 ; f6 46 f0 01                 ; 0xc334b vgabios.c:2320
    je near 03487h                            ; 0f 84 34 01                 ; 0xc334f
    mov dx, 003dah                            ; ba da 03                    ; 0xc3353 vgabios.c:2322
    in AL, DX                                 ; ec                          ; 0xc3356
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3357
    lea si, [bx+040h]                         ; 8d 77 40                    ; 0xc3359 vgabios.c:2324
    mov es, cx                                ; 8e c1                       ; 0xc335c vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc335e
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc3361 vgabios.c:58
    mov si, bx                                ; 89 de                       ; 0xc3364 vgabios.c:2325
    mov word [bp-00eh], strict word 00001h    ; c7 46 f2 01 00              ; 0xc3366 vgabios.c:2328
    add bx, strict byte 00005h                ; 83 c3 05                    ; 0xc336b vgabios.c:2326
    jmp short 03376h                          ; eb 06                       ; 0xc336e
    cmp word [bp-00eh], strict byte 00004h    ; 83 7e f2 04                 ; 0xc3370
    jnbe short 0338ch                         ; 77 16                       ; 0xc3374
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc3376 vgabios.c:2329
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3379
    out DX, AL                                ; ee                          ; 0xc337c
    mov es, cx                                ; 8e c1                       ; 0xc337d vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc337f
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3382 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3385
    inc bx                                    ; 43                          ; 0xc3386 vgabios.c:2330
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc3387 vgabios.c:2331
    jmp short 03370h                          ; eb e4                       ; 0xc338a
    xor al, al                                ; 30 c0                       ; 0xc338c vgabios.c:2332
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc338e
    out DX, AL                                ; ee                          ; 0xc3391
    mov es, cx                                ; 8e c1                       ; 0xc3392 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3394
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3397 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc339a
    inc bx                                    ; 43                          ; 0xc339b vgabios.c:2333
    mov dx, 003cch                            ; ba cc 03                    ; 0xc339c
    in AL, DX                                 ; ec                          ; 0xc339f
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc33a0
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc33a2
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc33a4
    cmp word [bp-00ah], 003d4h                ; 81 7e f6 d4 03              ; 0xc33a7 vgabios.c:2337
    jne short 033b2h                          ; 75 04                       ; 0xc33ac
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc33ae vgabios.c:2338
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc33b2 vgabios.c:2339
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc33b5
    out DX, AL                                ; ee                          ; 0xc33b8
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc33b9 vgabios.c:2342
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc33bc
    out DX, ax                                ; ef                          ; 0xc33bf
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc33c0 vgabios.c:2344
    jmp short 033cdh                          ; eb 06                       ; 0xc33c5
    cmp word [bp-00eh], strict byte 00018h    ; 83 7e f2 18                 ; 0xc33c7
    jnbe short 033e7h                         ; 77 1a                       ; 0xc33cb
    cmp word [bp-00eh], strict byte 00011h    ; 83 7e f2 11                 ; 0xc33cd vgabios.c:2345
    je short 033e1h                           ; 74 0e                       ; 0xc33d1
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc33d3 vgabios.c:2346
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc33d6
    out DX, AL                                ; ee                          ; 0xc33d9
    mov es, cx                                ; 8e c1                       ; 0xc33da vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc33dc
    inc dx                                    ; 42                          ; 0xc33df vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc33e0
    inc bx                                    ; 43                          ; 0xc33e1 vgabios.c:2349
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc33e2 vgabios.c:2350
    jmp short 033c7h                          ; eb e0                       ; 0xc33e5
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc33e7 vgabios.c:2352
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc33e9
    out DX, AL                                ; ee                          ; 0xc33ec
    lea di, [word bx-00007h]                  ; 8d bf f9 ff                 ; 0xc33ed vgabios.c:2353
    mov es, cx                                ; 8e c1                       ; 0xc33f1 vgabios.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc33f3
    inc dx                                    ; 42                          ; 0xc33f6 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc33f7
    lea di, [si+003h]                         ; 8d 7c 03                    ; 0xc33f8 vgabios.c:2356
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc33fb vgabios.c:47
    xor ah, ah                                ; 30 e4                       ; 0xc33fe vgabios.c:48
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc3400
    mov dx, 003dah                            ; ba da 03                    ; 0xc3403 vgabios.c:2357
    in AL, DX                                 ; ec                          ; 0xc3406
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3407
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3409 vgabios.c:2358
    jmp short 03416h                          ; eb 06                       ; 0xc340e
    cmp word [bp-00eh], strict byte 00013h    ; 83 7e f2 13                 ; 0xc3410
    jnbe short 0342fh                         ; 77 19                       ; 0xc3414
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc3416 vgabios.c:2359
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc3419
    or ax, word [bp-00eh]                     ; 0b 46 f2                    ; 0xc341c
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc341f
    out DX, AL                                ; ee                          ; 0xc3422
    mov es, cx                                ; 8e c1                       ; 0xc3423 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3425
    out DX, AL                                ; ee                          ; 0xc3428 vgabios.c:48
    inc bx                                    ; 43                          ; 0xc3429 vgabios.c:2360
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc342a vgabios.c:2361
    jmp short 03410h                          ; eb e1                       ; 0xc342d
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc342f vgabios.c:2362
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3432
    out DX, AL                                ; ee                          ; 0xc3435
    mov dx, 003dah                            ; ba da 03                    ; 0xc3436 vgabios.c:2363
    in AL, DX                                 ; ec                          ; 0xc3439
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc343a
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc343c vgabios.c:2365
    jmp short 03449h                          ; eb 06                       ; 0xc3441
    cmp word [bp-00eh], strict byte 00008h    ; 83 7e f2 08                 ; 0xc3443
    jnbe short 0345fh                         ; 77 16                       ; 0xc3447
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc3449 vgabios.c:2366
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc344c
    out DX, AL                                ; ee                          ; 0xc344f
    mov es, cx                                ; 8e c1                       ; 0xc3450 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3452
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc3455 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3458
    inc bx                                    ; 43                          ; 0xc3459 vgabios.c:2367
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc345a vgabios.c:2368
    jmp short 03443h                          ; eb e4                       ; 0xc345d
    add bx, strict byte 00006h                ; 83 c3 06                    ; 0xc345f vgabios.c:2369
    mov es, cx                                ; 8e c1                       ; 0xc3462 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3464
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3467 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc346a
    inc si                                    ; 46                          ; 0xc346b vgabios.c:2372
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc346c vgabios.c:47
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc346f vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3472
    inc si                                    ; 46                          ; 0xc3473 vgabios.c:2373
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3474 vgabios.c:47
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc3477 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc347a
    inc si                                    ; 46                          ; 0xc347b vgabios.c:2374
    inc si                                    ; 46                          ; 0xc347c
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc347d vgabios.c:47
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc3480 vgabios.c:48
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc3483
    out DX, AL                                ; ee                          ; 0xc3486
    test byte [bp-010h], 002h                 ; f6 46 f0 02                 ; 0xc3487 vgabios.c:2378
    je near 035aah                            ; 0f 84 1b 01                 ; 0xc348b
    mov es, cx                                ; 8e c1                       ; 0xc348f vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3491
    mov si, strict word 00049h                ; be 49 00                    ; 0xc3494 vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3497
    mov es, dx                                ; 8e c2                       ; 0xc349a
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc349c
    inc bx                                    ; 43                          ; 0xc349f vgabios.c:2379
    mov es, cx                                ; 8e c1                       ; 0xc34a0 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc34a2
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc34a5 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc34a8
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34aa
    inc bx                                    ; 43                          ; 0xc34ad vgabios.c:2380
    inc bx                                    ; 43                          ; 0xc34ae
    mov es, cx                                ; 8e c1                       ; 0xc34af vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc34b1
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc34b4 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc34b7
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34b9
    inc bx                                    ; 43                          ; 0xc34bc vgabios.c:2381
    inc bx                                    ; 43                          ; 0xc34bd
    mov es, cx                                ; 8e c1                       ; 0xc34be vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc34c0
    mov si, strict word 00063h                ; be 63 00                    ; 0xc34c3 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc34c6
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34c8
    inc bx                                    ; 43                          ; 0xc34cb vgabios.c:2382
    inc bx                                    ; 43                          ; 0xc34cc
    mov es, cx                                ; 8e c1                       ; 0xc34cd vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc34cf
    mov si, 00084h                            ; be 84 00                    ; 0xc34d2 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc34d5
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc34d7
    inc bx                                    ; 43                          ; 0xc34da vgabios.c:2383
    mov es, cx                                ; 8e c1                       ; 0xc34db vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc34dd
    mov si, 00085h                            ; be 85 00                    ; 0xc34e0 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc34e3
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34e5
    inc bx                                    ; 43                          ; 0xc34e8 vgabios.c:2384
    inc bx                                    ; 43                          ; 0xc34e9
    mov es, cx                                ; 8e c1                       ; 0xc34ea vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc34ec
    mov si, 00087h                            ; be 87 00                    ; 0xc34ef vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc34f2
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc34f4
    inc bx                                    ; 43                          ; 0xc34f7 vgabios.c:2385
    mov es, cx                                ; 8e c1                       ; 0xc34f8 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc34fa
    mov si, 00088h                            ; be 88 00                    ; 0xc34fd vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3500
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3502
    inc bx                                    ; 43                          ; 0xc3505 vgabios.c:2386
    mov es, cx                                ; 8e c1                       ; 0xc3506 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3508
    mov si, 00089h                            ; be 89 00                    ; 0xc350b vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc350e
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3510
    inc bx                                    ; 43                          ; 0xc3513 vgabios.c:2387
    mov es, cx                                ; 8e c1                       ; 0xc3514 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3516
    mov si, strict word 00060h                ; be 60 00                    ; 0xc3519 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc351c
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc351e
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3521 vgabios.c:2389
    inc bx                                    ; 43                          ; 0xc3526 vgabios.c:2388
    inc bx                                    ; 43                          ; 0xc3527
    jmp short 03530h                          ; eb 06                       ; 0xc3528
    cmp word [bp-00eh], strict byte 00008h    ; 83 7e f2 08                 ; 0xc352a
    jnc short 0354ch                          ; 73 1c                       ; 0xc352e
    mov es, cx                                ; 8e c1                       ; 0xc3530 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3532
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc3535 vgabios.c:58
    add si, si                                ; 01 f6                       ; 0xc3538
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc353a
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc353d vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3540
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3542
    inc bx                                    ; 43                          ; 0xc3545 vgabios.c:2391
    inc bx                                    ; 43                          ; 0xc3546
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc3547 vgabios.c:2392
    jmp short 0352ah                          ; eb de                       ; 0xc354a
    mov es, cx                                ; 8e c1                       ; 0xc354c vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc354e
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc3551 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3554
    mov es, dx                                ; 8e c2                       ; 0xc3557
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3559
    inc bx                                    ; 43                          ; 0xc355c vgabios.c:2393
    inc bx                                    ; 43                          ; 0xc355d
    mov es, cx                                ; 8e c1                       ; 0xc355e vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3560
    mov si, strict word 00062h                ; be 62 00                    ; 0xc3563 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3566
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3568
    inc bx                                    ; 43                          ; 0xc356b vgabios.c:2394
    mov es, cx                                ; 8e c1                       ; 0xc356c vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc356e
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc3571 vgabios.c:62
    xor dx, dx                                ; 31 d2                       ; 0xc3574
    mov es, dx                                ; 8e c2                       ; 0xc3576
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3578
    inc bx                                    ; 43                          ; 0xc357b vgabios.c:2396
    inc bx                                    ; 43                          ; 0xc357c
    mov es, cx                                ; 8e c1                       ; 0xc357d vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc357f
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc3582 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3585
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3587
    inc bx                                    ; 43                          ; 0xc358a vgabios.c:2397
    inc bx                                    ; 43                          ; 0xc358b
    mov es, cx                                ; 8e c1                       ; 0xc358c vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc358e
    mov si, 0010ch                            ; be 0c 01                    ; 0xc3591 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3594
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3596
    inc bx                                    ; 43                          ; 0xc3599 vgabios.c:2398
    inc bx                                    ; 43                          ; 0xc359a
    mov es, cx                                ; 8e c1                       ; 0xc359b vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc359d
    mov si, 0010eh                            ; be 0e 01                    ; 0xc35a0 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc35a3
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc35a5
    inc bx                                    ; 43                          ; 0xc35a8 vgabios.c:2399
    inc bx                                    ; 43                          ; 0xc35a9
    test byte [bp-010h], 004h                 ; f6 46 f0 04                 ; 0xc35aa vgabios.c:2401
    je short 035edh                           ; 74 3d                       ; 0xc35ae
    inc bx                                    ; 43                          ; 0xc35b0 vgabios.c:2402
    mov es, cx                                ; 8e c1                       ; 0xc35b1 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35b3
    xor ah, ah                                ; 30 e4                       ; 0xc35b6 vgabios.c:48
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc35b8
    inc bx                                    ; 43                          ; 0xc35bb vgabios.c:2403
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35bc vgabios.c:47
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc35bf vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35c2
    inc bx                                    ; 43                          ; 0xc35c3 vgabios.c:2404
    xor al, al                                ; 30 c0                       ; 0xc35c4
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc35c6
    out DX, AL                                ; ee                          ; 0xc35c9
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc35ca vgabios.c:2407
    jmp short 035d6h                          ; eb 07                       ; 0xc35cd
    cmp word [bp-00eh], 00300h                ; 81 7e f2 00 03              ; 0xc35cf
    jnc short 035e5h                          ; 73 0f                       ; 0xc35d4
    mov es, cx                                ; 8e c1                       ; 0xc35d6 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35d8
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc35db vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35de
    inc bx                                    ; 43                          ; 0xc35df vgabios.c:2408
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc35e0 vgabios.c:2409
    jmp short 035cfh                          ; eb ea                       ; 0xc35e3
    inc bx                                    ; 43                          ; 0xc35e5 vgabios.c:2410
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc35e6
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc35e9
    out DX, AL                                ; ee                          ; 0xc35ec
    mov ax, bx                                ; 89 d8                       ; 0xc35ed vgabios.c:2414
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc35ef
    pop di                                    ; 5f                          ; 0xc35f2
    pop si                                    ; 5e                          ; 0xc35f3
    pop cx                                    ; 59                          ; 0xc35f4
    pop bp                                    ; 5d                          ; 0xc35f5
    retn                                      ; c3                          ; 0xc35f6
  ; disGetNextSymbol 0xc35f7 LB 0xccc -> off=0x0 cb=0000000000000027 uValue=00000000000c35f7 'find_vga_entry'
find_vga_entry:                              ; 0xc35f7 LB 0x27
    push bx                                   ; 53                          ; 0xc35f7 vgabios.c:2423
    push dx                                   ; 52                          ; 0xc35f8
    push bp                                   ; 55                          ; 0xc35f9
    mov bp, sp                                ; 89 e5                       ; 0xc35fa
    mov dl, al                                ; 88 c2                       ; 0xc35fc
    mov AH, strict byte 0ffh                  ; b4 ff                       ; 0xc35fe vgabios.c:2425
    xor al, al                                ; 30 c0                       ; 0xc3600 vgabios.c:2426
    jmp short 0360ah                          ; eb 06                       ; 0xc3602
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc3604 vgabios.c:2427
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc3606
    jnbe short 03618h                         ; 77 0e                       ; 0xc3608
    movzx bx, al                              ; 0f b6 d8                    ; 0xc360a
    sal bx, 003h                              ; c1 e3 03                    ; 0xc360d
    cmp dl, byte [bx+047ach]                  ; 3a 97 ac 47                 ; 0xc3610
    jne short 03604h                          ; 75 ee                       ; 0xc3614
    mov ah, al                                ; 88 c4                       ; 0xc3616
    mov al, ah                                ; 88 e0                       ; 0xc3618 vgabios.c:2432
    pop bp                                    ; 5d                          ; 0xc361a
    pop dx                                    ; 5a                          ; 0xc361b
    pop bx                                    ; 5b                          ; 0xc361c
    retn                                      ; c3                          ; 0xc361d
  ; disGetNextSymbol 0xc361e LB 0xca5 -> off=0x0 cb=000000000000000e uValue=00000000000c361e 'readx_byte'
readx_byte:                                  ; 0xc361e LB 0xe
    push bx                                   ; 53                          ; 0xc361e vgabios.c:2444
    push bp                                   ; 55                          ; 0xc361f
    mov bp, sp                                ; 89 e5                       ; 0xc3620
    mov bx, dx                                ; 89 d3                       ; 0xc3622
    mov es, ax                                ; 8e c0                       ; 0xc3624 vgabios.c:2446
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3626
    pop bp                                    ; 5d                          ; 0xc3629 vgabios.c:2447
    pop bx                                    ; 5b                          ; 0xc362a
    retn                                      ; c3                          ; 0xc362b
  ; disGetNextSymbol 0xc362c LB 0xc97 -> off=0x8a cb=000000000000047c uValue=00000000000c36b6 'int10_func'
    db  056h, 04fh, 01ch, 01bh, 013h, 012h, 011h, 010h, 00eh, 00dh, 00ch, 00ah, 009h, 008h, 007h, 006h
    db  005h, 004h, 003h, 002h, 001h, 000h, 02bh, 03bh, 0e1h, 036h, 01eh, 037h, 032h, 037h, 043h, 037h
    db  057h, 037h, 068h, 037h, 073h, 037h, 0adh, 037h, 0b1h, 037h, 0c2h, 037h, 0dfh, 037h, 0fch, 037h
    db  01ch, 038h, 039h, 038h, 050h, 038h, 05ch, 038h, 061h, 039h, 0eeh, 039h, 01bh, 03ah, 030h, 03ah
    db  072h, 03ah, 0fdh, 03ah, 030h, 024h, 023h, 022h, 021h, 020h, 014h, 012h, 011h, 010h, 004h, 003h
    db  002h, 001h, 000h, 02bh, 03bh, 07dh, 038h, 09dh, 038h, 0b9h, 038h, 0ceh, 038h, 0d9h, 038h, 07dh
    db  038h, 09dh, 038h, 0b9h, 038h, 0d9h, 038h, 0eeh, 038h, 0fah, 038h, 015h, 039h, 026h, 039h, 037h
    db  039h, 048h, 039h, 00ah, 009h, 006h, 004h, 002h, 001h, 000h, 0efh, 03ah, 09ah, 03ah, 0a8h, 03ah
    db  0b9h, 03ah, 0c9h, 03ah, 0deh, 03ah, 0efh, 03ah, 0efh, 03ah
int10_func:                                  ; 0xc36b6 LB 0x47c
    push bp                                   ; 55                          ; 0xc36b6 vgabios.c:2525
    mov bp, sp                                ; 89 e5                       ; 0xc36b7
    push si                                   ; 56                          ; 0xc36b9
    push di                                   ; 57                          ; 0xc36ba
    push ax                                   ; 50                          ; 0xc36bb
    mov si, word [bp+004h]                    ; 8b 76 04                    ; 0xc36bc
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc36bf vgabios.c:2530
    shr ax, 008h                              ; c1 e8 08                    ; 0xc36c2
    cmp ax, strict word 00056h                ; 3d 56 00                    ; 0xc36c5
    jnbe near 03b2bh                          ; 0f 87 5f 04                 ; 0xc36c8
    push CS                                   ; 0e                          ; 0xc36cc
    pop ES                                    ; 07                          ; 0xc36cd
    mov cx, strict word 00017h                ; b9 17 00                    ; 0xc36ce
    mov di, 0362ch                            ; bf 2c 36                    ; 0xc36d1
    repne scasb                               ; f2 ae                       ; 0xc36d4
    sal cx, 1                                 ; d1 e1                       ; 0xc36d6
    mov di, cx                                ; 89 cf                       ; 0xc36d8
    mov ax, word [cs:di+03642h]               ; 2e 8b 85 42 36              ; 0xc36da
    jmp ax                                    ; ff e0                       ; 0xc36df
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc36e1 vgabios.c:2533
    call 013cbh                               ; e8 e3 dc                    ; 0xc36e5
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc36e8 vgabios.c:2534
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc36eb
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc36ee
    je short 03708h                           ; 74 15                       ; 0xc36f1
    cmp ax, strict word 00006h                ; 3d 06 00                    ; 0xc36f3
    je short 036ffh                           ; 74 07                       ; 0xc36f6
    cmp ax, strict word 00005h                ; 3d 05 00                    ; 0xc36f8
    jbe short 03708h                          ; 76 0b                       ; 0xc36fb
    jmp short 03711h                          ; eb 12                       ; 0xc36fd
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc36ff vgabios.c:2536
    xor al, al                                ; 30 c0                       ; 0xc3702
    or AL, strict byte 03fh                   ; 0c 3f                       ; 0xc3704
    jmp short 03718h                          ; eb 10                       ; 0xc3706 vgabios.c:2537
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3708 vgabios.c:2545
    xor al, al                                ; 30 c0                       ; 0xc370b
    or AL, strict byte 030h                   ; 0c 30                       ; 0xc370d
    jmp short 03718h                          ; eb 07                       ; 0xc370f
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3711 vgabios.c:2548
    xor al, al                                ; 30 c0                       ; 0xc3714
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc3716
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc3718
    jmp near 03b2bh                           ; e9 0d 04                    ; 0xc371b vgabios.c:2550
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc371e vgabios.c:2552
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3721
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3724
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3727
    xor ah, ah                                ; 30 e4                       ; 0xc372a
    call 0113ah                               ; e8 0b da                    ; 0xc372c
    jmp near 03b2bh                           ; e9 f9 03                    ; 0xc372f vgabios.c:2553
    mov dx, word [bp+00eh]                    ; 8b 56 0e                    ; 0xc3732 vgabios.c:2555
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3735
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3738
    xor ah, ah                                ; 30 e4                       ; 0xc373b
    call 01230h                               ; e8 f0 da                    ; 0xc373d
    jmp near 03b2bh                           ; e9 e8 03                    ; 0xc3740 vgabios.c:2556
    lea bx, [bp+00eh]                         ; 8d 5e 0e                    ; 0xc3743 vgabios.c:2558
    lea dx, [bp+010h]                         ; 8d 56 10                    ; 0xc3746
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3749
    shr ax, 008h                              ; c1 e8 08                    ; 0xc374c
    xor ah, ah                                ; 30 e4                       ; 0xc374f
    call 00a93h                               ; e8 3f d3                    ; 0xc3751
    jmp near 03b2bh                           ; e9 d4 03                    ; 0xc3754 vgabios.c:2559
    xor ax, ax                                ; 31 c0                       ; 0xc3757 vgabios.c:2565
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc3759
    mov word [bp+00ch], ax                    ; 89 46 0c                    ; 0xc375c vgabios.c:2566
    mov word [bp+010h], ax                    ; 89 46 10                    ; 0xc375f vgabios.c:2567
    mov word [bp+00eh], ax                    ; 89 46 0e                    ; 0xc3762 vgabios.c:2568
    jmp near 03b2bh                           ; e9 c3 03                    ; 0xc3765 vgabios.c:2569
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3768 vgabios.c:2571
    xor ah, ah                                ; 30 e4                       ; 0xc376b
    call 012b9h                               ; e8 49 db                    ; 0xc376d
    jmp near 03b2bh                           ; e9 b8 03                    ; 0xc3770 vgabios.c:2572
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3773 vgabios.c:2574
    push ax                                   ; 50                          ; 0xc3776
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc3777
    push ax                                   ; 50                          ; 0xc377a
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc377b
    xor ah, ah                                ; 30 e4                       ; 0xc377e
    push ax                                   ; 50                          ; 0xc3780
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc3781
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3784
    xor ah, ah                                ; 30 e4                       ; 0xc3787
    push ax                                   ; 50                          ; 0xc3789
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc378a
    movzx cx, al                              ; 0f b6 c8                    ; 0xc378d
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3790
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3793
    movzx bx, al                              ; 0f b6 d8                    ; 0xc3796
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3799
    shr ax, 008h                              ; c1 e8 08                    ; 0xc379c
    movzx dx, al                              ; 0f b6 d0                    ; 0xc379f
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc37a2
    xor ah, ah                                ; 30 e4                       ; 0xc37a5
    call 01b54h                               ; e8 aa e3                    ; 0xc37a7
    jmp near 03b2bh                           ; e9 7e 03                    ; 0xc37aa vgabios.c:2575
    xor ax, ax                                ; 31 c0                       ; 0xc37ad vgabios.c:2577
    jmp short 03776h                          ; eb c5                       ; 0xc37af
    lea dx, [bp+012h]                         ; 8d 56 12                    ; 0xc37b1 vgabios.c:2580
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc37b4
    shr ax, 008h                              ; c1 e8 08                    ; 0xc37b7
    xor ah, ah                                ; 30 e4                       ; 0xc37ba
    call 00d9eh                               ; e8 df d5                    ; 0xc37bc
    jmp near 03b2bh                           ; e9 69 03                    ; 0xc37bf vgabios.c:2581
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc37c2 vgabios.c:2583
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc37c5
    movzx bx, al                              ; 0f b6 d8                    ; 0xc37c8
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc37cb
    shr ax, 008h                              ; c1 e8 08                    ; 0xc37ce
    movzx dx, al                              ; 0f b6 d0                    ; 0xc37d1
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc37d4
    xor ah, ah                                ; 30 e4                       ; 0xc37d7
    call 023ddh                               ; e8 01 ec                    ; 0xc37d9
    jmp near 03b2bh                           ; e9 4c 03                    ; 0xc37dc vgabios.c:2584
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc37df vgabios.c:2586
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc37e2
    movzx bx, al                              ; 0f b6 d8                    ; 0xc37e5
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc37e8
    shr ax, 008h                              ; c1 e8 08                    ; 0xc37eb
    movzx dx, al                              ; 0f b6 d0                    ; 0xc37ee
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc37f1
    xor ah, ah                                ; 30 e4                       ; 0xc37f4
    call 02542h                               ; e8 49 ed                    ; 0xc37f6
    jmp near 03b2bh                           ; e9 2f 03                    ; 0xc37f9 vgabios.c:2587
    mov cx, word [bp+00eh]                    ; 8b 4e 0e                    ; 0xc37fc vgabios.c:2589
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc37ff
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3802
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3805
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3808
    shr ax, 008h                              ; c1 e8 08                    ; 0xc380b
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc380e
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3811
    xor ah, ah                                ; 30 e4                       ; 0xc3814
    call 026a4h                               ; e8 8b ee                    ; 0xc3816
    jmp near 03b2bh                           ; e9 0f 03                    ; 0xc3819 vgabios.c:2590
    lea cx, [bp+012h]                         ; 8d 4e 12                    ; 0xc381c vgabios.c:2592
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc381f
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3822
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3825
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3828
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc382b
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc382e
    xor ah, ah                                ; 30 e4                       ; 0xc3831
    call 00f58h                               ; e8 22 d7                    ; 0xc3833
    jmp near 03b2bh                           ; e9 f2 02                    ; 0xc3836 vgabios.c:2593
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc3839 vgabios.c:2601
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc383c
    movzx bx, al                              ; 0f b6 d8                    ; 0xc383f
    mov dx, 000ffh                            ; ba ff 00                    ; 0xc3842
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3845
    xor ah, ah                                ; 30 e4                       ; 0xc3848
    call 02809h                               ; e8 bc ef                    ; 0xc384a
    jmp near 03b2bh                           ; e9 db 02                    ; 0xc384d vgabios.c:2602
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3850 vgabios.c:2605
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3853
    call 010aeh                               ; e8 55 d8                    ; 0xc3856
    jmp near 03b2bh                           ; e9 cf 02                    ; 0xc3859 vgabios.c:2606
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc385c vgabios.c:2608
    xor ah, ah                                ; 30 e4                       ; 0xc385f
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3861
    jnbe near 03b2bh                          ; 0f 87 c3 02                 ; 0xc3864
    push CS                                   ; 0e                          ; 0xc3868
    pop ES                                    ; 07                          ; 0xc3869
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc386a
    mov di, 03670h                            ; bf 70 36                    ; 0xc386d
    repne scasb                               ; f2 ae                       ; 0xc3870
    sal cx, 1                                 ; d1 e1                       ; 0xc3872
    mov di, cx                                ; 89 cf                       ; 0xc3874
    mov ax, word [cs:di+0367fh]               ; 2e 8b 85 7f 36              ; 0xc3876
    jmp ax                                    ; ff e0                       ; 0xc387b
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc387d vgabios.c:2612
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3880
    xor ah, ah                                ; 30 e4                       ; 0xc3883
    push ax                                   ; 50                          ; 0xc3885
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc3886
    push ax                                   ; 50                          ; 0xc388a
    push word [bp+00eh]                       ; ff 76 0e                    ; 0xc388b
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc388e
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3892
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc3895
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3898
    jmp short 038b3h                          ; eb 16                       ; 0xc389b
    push strict byte 0000eh                   ; 6a 0e                       ; 0xc389d vgabios.c:2616
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc389f
    push ax                                   ; 50                          ; 0xc38a3
    push strict byte 00000h                   ; 6a 00                       ; 0xc38a4
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc38a6
    mov cx, 00100h                            ; b9 00 01                    ; 0xc38aa
    mov bx, 05d6ah                            ; bb 6a 5d                    ; 0xc38ad
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc38b0
    call 02c14h                               ; e8 5e f3                    ; 0xc38b3
    jmp near 03b2bh                           ; e9 72 02                    ; 0xc38b6
    push strict byte 00008h                   ; 6a 08                       ; 0xc38b9 vgabios.c:2620
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc38bb
    push ax                                   ; 50                          ; 0xc38bf
    push strict byte 00000h                   ; 6a 00                       ; 0xc38c0
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc38c2
    mov cx, 00100h                            ; b9 00 01                    ; 0xc38c6
    mov bx, 0556ah                            ; bb 6a 55                    ; 0xc38c9
    jmp short 038b0h                          ; eb e2                       ; 0xc38cc
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc38ce vgabios.c:2623
    xor ah, ah                                ; 30 e4                       ; 0xc38d1
    call 02b7dh                               ; e8 a7 f2                    ; 0xc38d3
    jmp near 03b2bh                           ; e9 52 02                    ; 0xc38d6 vgabios.c:2624
    push strict byte 00010h                   ; 6a 10                       ; 0xc38d9 vgabios.c:2627
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc38db
    push ax                                   ; 50                          ; 0xc38df
    push strict byte 00000h                   ; 6a 00                       ; 0xc38e0
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc38e2
    mov cx, 00100h                            ; b9 00 01                    ; 0xc38e6
    mov bx, 06b6ah                            ; bb 6a 6b                    ; 0xc38e9
    jmp short 038b0h                          ; eb c2                       ; 0xc38ec
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc38ee vgabios.c:2630
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc38f1
    call 02c90h                               ; e8 99 f3                    ; 0xc38f4
    jmp near 03b2bh                           ; e9 31 02                    ; 0xc38f7 vgabios.c:2631
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc38fa vgabios.c:2633
    xor ah, ah                                ; 30 e4                       ; 0xc38fd
    push ax                                   ; 50                          ; 0xc38ff
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3900
    movzx cx, al                              ; 0f b6 c8                    ; 0xc3903
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc3906
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc3909
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc390c
    call 02cefh                               ; e8 dd f3                    ; 0xc390f
    jmp near 03b2bh                           ; e9 16 02                    ; 0xc3912 vgabios.c:2634
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3915 vgabios.c:2636
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3918
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc391b
    xor ah, ah                                ; 30 e4                       ; 0xc391e
    call 02d0bh                               ; e8 e8 f3                    ; 0xc3920
    jmp near 03b2bh                           ; e9 05 02                    ; 0xc3923 vgabios.c:2637
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3926 vgabios.c:2639
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3929
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc392c
    xor ah, ah                                ; 30 e4                       ; 0xc392f
    call 02d29h                               ; e8 f5 f3                    ; 0xc3931
    jmp near 03b2bh                           ; e9 f4 01                    ; 0xc3934 vgabios.c:2640
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3937 vgabios.c:2642
    movzx dx, al                              ; 0f b6 d0                    ; 0xc393a
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc393d
    xor ah, ah                                ; 30 e4                       ; 0xc3940
    call 02d47h                               ; e8 02 f4                    ; 0xc3942
    jmp near 03b2bh                           ; e9 e3 01                    ; 0xc3945 vgabios.c:2643
    lea ax, [bp+00eh]                         ; 8d 46 0e                    ; 0xc3948 vgabios.c:2645
    push ax                                   ; 50                          ; 0xc394b
    lea cx, [bp+010h]                         ; 8d 4e 10                    ; 0xc394c
    lea bx, [bp+008h]                         ; 8d 5e 08                    ; 0xc394f
    lea dx, [bp+016h]                         ; 8d 56 16                    ; 0xc3952
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3955
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3958
    call 00ed5h                               ; e8 77 d5                    ; 0xc395b
    jmp near 03b2bh                           ; e9 ca 01                    ; 0xc395e vgabios.c:2653
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3961 vgabios.c:2655
    xor ah, ah                                ; 30 e4                       ; 0xc3964
    cmp ax, strict word 00034h                ; 3d 34 00                    ; 0xc3966
    jc short 0397ah                           ; 72 0f                       ; 0xc3969
    jbe short 039adh                          ; 76 40                       ; 0xc396b
    cmp ax, strict word 00036h                ; 3d 36 00                    ; 0xc396d
    je short 039e4h                           ; 74 72                       ; 0xc3970
    cmp ax, strict word 00035h                ; 3d 35 00                    ; 0xc3972
    je short 039d5h                           ; 74 5e                       ; 0xc3975
    jmp near 03b2bh                           ; e9 b1 01                    ; 0xc3977
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc397a
    je short 0398ch                           ; 74 0d                       ; 0xc397d
    cmp ax, strict word 00020h                ; 3d 20 00                    ; 0xc397f
    jne near 03b2bh                           ; 0f 85 a5 01                 ; 0xc3982
    call 02d65h                               ; e8 dc f3                    ; 0xc3986 vgabios.c:2658
    jmp near 03b2bh                           ; e9 9f 01                    ; 0xc3989 vgabios.c:2659
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc398c vgabios.c:2661
    xor ah, ah                                ; 30 e4                       ; 0xc398f
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3991
    jnbe near 03b2bh                          ; 0f 87 93 01                 ; 0xc3994
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3998 vgabios.c:2662
    xor ah, ah                                ; 30 e4                       ; 0xc399b
    call 02d6ah                               ; e8 ca f3                    ; 0xc399d
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc39a0 vgabios.c:2663
    xor al, al                                ; 30 c0                       ; 0xc39a3
    or AL, strict byte 012h                   ; 0c 12                       ; 0xc39a5
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc39a7
    jmp near 03b2bh                           ; e9 7e 01                    ; 0xc39aa vgabios.c:2665
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc39ad vgabios.c:2667
    xor ah, ah                                ; 30 e4                       ; 0xc39b0
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc39b2
    jnc short 039cfh                          ; 73 18                       ; 0xc39b5
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc39b7 vgabios.c:45
    mov si, 00087h                            ; be 87 00                    ; 0xc39ba
    mov es, ax                                ; 8e c0                       ; 0xc39bd vgabios.c:47
    mov ah, byte [es:si]                      ; 26 8a 24                    ; 0xc39bf
    and ah, 0feh                              ; 80 e4 fe                    ; 0xc39c2 vgabios.c:48
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc39c5
    or al, ah                                 ; 08 e0                       ; 0xc39c8
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc39ca vgabios.c:52
    jmp short 039a0h                          ; eb d1                       ; 0xc39cd
    mov byte [bp+012h], ah                    ; 88 66 12                    ; 0xc39cf vgabios.c:2673
    jmp near 03b2bh                           ; e9 56 01                    ; 0xc39d2 vgabios.c:2674
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc39d5 vgabios.c:2676
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc39d9
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc39dc
    call 02d9ch                               ; e8 ba f3                    ; 0xc39df
    jmp short 039a0h                          ; eb bc                       ; 0xc39e2
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc39e4 vgabios.c:2680
    xor ah, ah                                ; 30 e4                       ; 0xc39e7
    call 02da1h                               ; e8 b5 f3                    ; 0xc39e9
    jmp short 039a0h                          ; eb b2                       ; 0xc39ec
    push word [bp+008h]                       ; ff 76 08                    ; 0xc39ee vgabios.c:2690
    push word [bp+016h]                       ; ff 76 16                    ; 0xc39f1
    movzx ax, byte [bp+00eh]                  ; 0f b6 46 0e                 ; 0xc39f4
    push ax                                   ; 50                          ; 0xc39f8
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc39f9
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39fc
    xor ah, ah                                ; 30 e4                       ; 0xc39ff
    push ax                                   ; 50                          ; 0xc3a01
    movzx bx, byte [bp+00ch]                  ; 0f b6 5e 0c                 ; 0xc3a02
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3a06
    shr dx, 008h                              ; c1 ea 08                    ; 0xc3a09
    xor dh, dh                                ; 30 f6                       ; 0xc3a0c
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc3a0e
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a12
    call 02da6h                               ; e8 8e f3                    ; 0xc3a15
    jmp near 03b2bh                           ; e9 10 01                    ; 0xc3a18 vgabios.c:2691
    mov bx, si                                ; 89 f3                       ; 0xc3a1b vgabios.c:2693
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a1d
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a20
    call 02e3ch                               ; e8 16 f4                    ; 0xc3a23
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a26 vgabios.c:2694
    xor al, al                                ; 30 c0                       ; 0xc3a29
    or AL, strict byte 01bh                   ; 0c 1b                       ; 0xc3a2b
    jmp near 039a7h                           ; e9 77 ff                    ; 0xc3a2d
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a30 vgabios.c:2697
    xor ah, ah                                ; 30 e4                       ; 0xc3a33
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3a35
    je short 03a5ch                           ; 74 22                       ; 0xc3a38
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc3a3a
    je short 03a4eh                           ; 74 0f                       ; 0xc3a3d
    test ax, ax                               ; 85 c0                       ; 0xc3a3f
    jne short 03a68h                          ; 75 25                       ; 0xc3a41
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3a43 vgabios.c:2700
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3a46
    call 03051h                               ; e8 05 f6                    ; 0xc3a49
    jmp short 03a68h                          ; eb 1a                       ; 0xc3a4c vgabios.c:2701
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3a4e vgabios.c:2703
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a51
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3a54
    call 03069h                               ; e8 0f f6                    ; 0xc3a57
    jmp short 03a68h                          ; eb 0c                       ; 0xc3a5a vgabios.c:2704
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3a5c vgabios.c:2706
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a5f
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3a62
    call 0333fh                               ; e8 d7 f8                    ; 0xc3a65
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a68 vgabios.c:2713
    xor al, al                                ; 30 c0                       ; 0xc3a6b
    or AL, strict byte 01ch                   ; 0c 1c                       ; 0xc3a6d
    jmp near 039a7h                           ; e9 35 ff                    ; 0xc3a6f
    call 007bfh                               ; e8 4a cd                    ; 0xc3a72 vgabios.c:2718
    test ax, ax                               ; 85 c0                       ; 0xc3a75
    je near 03af6h                            ; 0f 84 7b 00                 ; 0xc3a77
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a7b vgabios.c:2719
    xor ah, ah                                ; 30 e4                       ; 0xc3a7e
    cmp ax, strict word 0000ah                ; 3d 0a 00                    ; 0xc3a80
    jnbe short 03aefh                         ; 77 6a                       ; 0xc3a83
    push CS                                   ; 0e                          ; 0xc3a85
    pop ES                                    ; 07                          ; 0xc3a86
    mov cx, strict word 00008h                ; b9 08 00                    ; 0xc3a87
    mov di, 0369fh                            ; bf 9f 36                    ; 0xc3a8a
    repne scasb                               ; f2 ae                       ; 0xc3a8d
    sal cx, 1                                 ; d1 e1                       ; 0xc3a8f
    mov di, cx                                ; 89 cf                       ; 0xc3a91
    mov ax, word [cs:di+036a6h]               ; 2e 8b 85 a6 36              ; 0xc3a93
    jmp ax                                    ; ff e0                       ; 0xc3a98
    mov bx, si                                ; 89 f3                       ; 0xc3a9a vgabios.c:2722
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a9c
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3a9f
    call 03cfch                               ; e8 57 02                    ; 0xc3aa2
    jmp near 03b2bh                           ; e9 83 00                    ; 0xc3aa5 vgabios.c:2723
    mov cx, si                                ; 89 f1                       ; 0xc3aa8 vgabios.c:2725
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3aaa
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3aad
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3ab0
    call 03e21h                               ; e8 6b 03                    ; 0xc3ab3
    jmp near 03b2bh                           ; e9 72 00                    ; 0xc3ab6 vgabios.c:2726
    mov cx, si                                ; 89 f1                       ; 0xc3ab9 vgabios.c:2728
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3abb
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3abe
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3ac1
    call 03ebch                               ; e8 f5 03                    ; 0xc3ac4
    jmp short 03b2bh                          ; eb 62                       ; 0xc3ac7 vgabios.c:2729
    lea ax, [bp+00ch]                         ; 8d 46 0c                    ; 0xc3ac9 vgabios.c:2731
    push ax                                   ; 50                          ; 0xc3acc
    mov cx, word [bp+016h]                    ; 8b 4e 16                    ; 0xc3acd
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3ad0
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3ad3
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3ad6
    call 04083h                               ; e8 a7 05                    ; 0xc3ad9
    jmp short 03b2bh                          ; eb 4d                       ; 0xc3adc vgabios.c:2732
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3ade vgabios.c:2734
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3ae1
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3ae4
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3ae7
    call 0410fh                               ; e8 22 06                    ; 0xc3aea
    jmp short 03b2bh                          ; eb 3c                       ; 0xc3aed vgabios.c:2735
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3aef vgabios.c:2757
    jmp short 03b2bh                          ; eb 35                       ; 0xc3af4 vgabios.c:2760
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3af6 vgabios.c:2762
    jmp short 03b2bh                          ; eb 2e                       ; 0xc3afb vgabios.c:2764
    call 007bfh                               ; e8 bf cc                    ; 0xc3afd vgabios.c:2766
    test ax, ax                               ; 85 c0                       ; 0xc3b00
    je short 03b26h                           ; 74 22                       ; 0xc3b02
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3b04 vgabios.c:2767
    xor ah, ah                                ; 30 e4                       ; 0xc3b07
    cmp ax, strict word 00042h                ; 3d 42 00                    ; 0xc3b09
    jne short 03b1fh                          ; 75 11                       ; 0xc3b0c
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3b0e vgabios.c:2770
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3b11
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3b14
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3b17
    call 041deh                               ; e8 c1 06                    ; 0xc3b1a
    jmp short 03b2bh                          ; eb 0c                       ; 0xc3b1d vgabios.c:2771
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3b1f vgabios.c:2773
    jmp short 03b2bh                          ; eb 05                       ; 0xc3b24 vgabios.c:2776
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3b26 vgabios.c:2778
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3b2b vgabios.c:2788
    pop di                                    ; 5f                          ; 0xc3b2e
    pop si                                    ; 5e                          ; 0xc3b2f
    pop bp                                    ; 5d                          ; 0xc3b30
    retn                                      ; c3                          ; 0xc3b31
  ; disGetNextSymbol 0xc3b32 LB 0x791 -> off=0x0 cb=000000000000001f uValue=00000000000c3b32 'dispi_set_xres'
dispi_set_xres:                              ; 0xc3b32 LB 0x1f
    push bp                                   ; 55                          ; 0xc3b32 vbe.c:100
    mov bp, sp                                ; 89 e5                       ; 0xc3b33
    push bx                                   ; 53                          ; 0xc3b35
    push dx                                   ; 52                          ; 0xc3b36
    mov bx, ax                                ; 89 c3                       ; 0xc3b37
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3b39 vbe.c:105
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b3c
    call 00570h                               ; e8 2e ca                    ; 0xc3b3f
    mov ax, bx                                ; 89 d8                       ; 0xc3b42 vbe.c:106
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b44
    call 00570h                               ; e8 26 ca                    ; 0xc3b47
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3b4a vbe.c:107
    pop dx                                    ; 5a                          ; 0xc3b4d
    pop bx                                    ; 5b                          ; 0xc3b4e
    pop bp                                    ; 5d                          ; 0xc3b4f
    retn                                      ; c3                          ; 0xc3b50
  ; disGetNextSymbol 0xc3b51 LB 0x772 -> off=0x0 cb=000000000000001f uValue=00000000000c3b51 'dispi_set_yres'
dispi_set_yres:                              ; 0xc3b51 LB 0x1f
    push bp                                   ; 55                          ; 0xc3b51 vbe.c:109
    mov bp, sp                                ; 89 e5                       ; 0xc3b52
    push bx                                   ; 53                          ; 0xc3b54
    push dx                                   ; 52                          ; 0xc3b55
    mov bx, ax                                ; 89 c3                       ; 0xc3b56
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3b58 vbe.c:114
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b5b
    call 00570h                               ; e8 0f ca                    ; 0xc3b5e
    mov ax, bx                                ; 89 d8                       ; 0xc3b61 vbe.c:115
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b63
    call 00570h                               ; e8 07 ca                    ; 0xc3b66
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3b69 vbe.c:116
    pop dx                                    ; 5a                          ; 0xc3b6c
    pop bx                                    ; 5b                          ; 0xc3b6d
    pop bp                                    ; 5d                          ; 0xc3b6e
    retn                                      ; c3                          ; 0xc3b6f
  ; disGetNextSymbol 0xc3b70 LB 0x753 -> off=0x0 cb=0000000000000019 uValue=00000000000c3b70 'dispi_get_yres'
dispi_get_yres:                              ; 0xc3b70 LB 0x19
    push bp                                   ; 55                          ; 0xc3b70 vbe.c:118
    mov bp, sp                                ; 89 e5                       ; 0xc3b71
    push dx                                   ; 52                          ; 0xc3b73
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3b74 vbe.c:120
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b77
    call 00570h                               ; e8 f3 c9                    ; 0xc3b7a
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b7d vbe.c:121
    call 00577h                               ; e8 f4 c9                    ; 0xc3b80
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3b83 vbe.c:122
    pop dx                                    ; 5a                          ; 0xc3b86
    pop bp                                    ; 5d                          ; 0xc3b87
    retn                                      ; c3                          ; 0xc3b88
  ; disGetNextSymbol 0xc3b89 LB 0x73a -> off=0x0 cb=000000000000001f uValue=00000000000c3b89 'dispi_set_bpp'
dispi_set_bpp:                               ; 0xc3b89 LB 0x1f
    push bp                                   ; 55                          ; 0xc3b89 vbe.c:124
    mov bp, sp                                ; 89 e5                       ; 0xc3b8a
    push bx                                   ; 53                          ; 0xc3b8c
    push dx                                   ; 52                          ; 0xc3b8d
    mov bx, ax                                ; 89 c3                       ; 0xc3b8e
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3b90 vbe.c:129
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b93
    call 00570h                               ; e8 d7 c9                    ; 0xc3b96
    mov ax, bx                                ; 89 d8                       ; 0xc3b99 vbe.c:130
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b9b
    call 00570h                               ; e8 cf c9                    ; 0xc3b9e
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3ba1 vbe.c:131
    pop dx                                    ; 5a                          ; 0xc3ba4
    pop bx                                    ; 5b                          ; 0xc3ba5
    pop bp                                    ; 5d                          ; 0xc3ba6
    retn                                      ; c3                          ; 0xc3ba7
  ; disGetNextSymbol 0xc3ba8 LB 0x71b -> off=0x0 cb=0000000000000019 uValue=00000000000c3ba8 'dispi_get_bpp'
dispi_get_bpp:                               ; 0xc3ba8 LB 0x19
    push bp                                   ; 55                          ; 0xc3ba8 vbe.c:133
    mov bp, sp                                ; 89 e5                       ; 0xc3ba9
    push dx                                   ; 52                          ; 0xc3bab
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3bac vbe.c:135
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3baf
    call 00570h                               ; e8 bb c9                    ; 0xc3bb2
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3bb5 vbe.c:136
    call 00577h                               ; e8 bc c9                    ; 0xc3bb8
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3bbb vbe.c:137
    pop dx                                    ; 5a                          ; 0xc3bbe
    pop bp                                    ; 5d                          ; 0xc3bbf
    retn                                      ; c3                          ; 0xc3bc0
  ; disGetNextSymbol 0xc3bc1 LB 0x702 -> off=0x0 cb=000000000000001f uValue=00000000000c3bc1 'dispi_set_virt_width'
dispi_set_virt_width:                        ; 0xc3bc1 LB 0x1f
    push bp                                   ; 55                          ; 0xc3bc1 vbe.c:139
    mov bp, sp                                ; 89 e5                       ; 0xc3bc2
    push bx                                   ; 53                          ; 0xc3bc4
    push dx                                   ; 52                          ; 0xc3bc5
    mov bx, ax                                ; 89 c3                       ; 0xc3bc6
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3bc8 vbe.c:144
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3bcb
    call 00570h                               ; e8 9f c9                    ; 0xc3bce
    mov ax, bx                                ; 89 d8                       ; 0xc3bd1 vbe.c:145
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3bd3
    call 00570h                               ; e8 97 c9                    ; 0xc3bd6
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3bd9 vbe.c:146
    pop dx                                    ; 5a                          ; 0xc3bdc
    pop bx                                    ; 5b                          ; 0xc3bdd
    pop bp                                    ; 5d                          ; 0xc3bde
    retn                                      ; c3                          ; 0xc3bdf
  ; disGetNextSymbol 0xc3be0 LB 0x6e3 -> off=0x0 cb=0000000000000019 uValue=00000000000c3be0 'dispi_get_virt_width'
dispi_get_virt_width:                        ; 0xc3be0 LB 0x19
    push bp                                   ; 55                          ; 0xc3be0 vbe.c:148
    mov bp, sp                                ; 89 e5                       ; 0xc3be1
    push dx                                   ; 52                          ; 0xc3be3
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3be4 vbe.c:150
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3be7
    call 00570h                               ; e8 83 c9                    ; 0xc3bea
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3bed vbe.c:151
    call 00577h                               ; e8 84 c9                    ; 0xc3bf0
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3bf3 vbe.c:152
    pop dx                                    ; 5a                          ; 0xc3bf6
    pop bp                                    ; 5d                          ; 0xc3bf7
    retn                                      ; c3                          ; 0xc3bf8
  ; disGetNextSymbol 0xc3bf9 LB 0x6ca -> off=0x0 cb=0000000000000019 uValue=00000000000c3bf9 'dispi_get_virt_height'
dispi_get_virt_height:                       ; 0xc3bf9 LB 0x19
    push bp                                   ; 55                          ; 0xc3bf9 vbe.c:154
    mov bp, sp                                ; 89 e5                       ; 0xc3bfa
    push dx                                   ; 52                          ; 0xc3bfc
    mov ax, strict word 00007h                ; b8 07 00                    ; 0xc3bfd vbe.c:156
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3c00
    call 00570h                               ; e8 6a c9                    ; 0xc3c03
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3c06 vbe.c:157
    call 00577h                               ; e8 6b c9                    ; 0xc3c09
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c0c vbe.c:158
    pop dx                                    ; 5a                          ; 0xc3c0f
    pop bp                                    ; 5d                          ; 0xc3c10
    retn                                      ; c3                          ; 0xc3c11
  ; disGetNextSymbol 0xc3c12 LB 0x6b1 -> off=0x0 cb=0000000000000012 uValue=00000000000c3c12 'in_word'
in_word:                                     ; 0xc3c12 LB 0x12
    push bp                                   ; 55                          ; 0xc3c12 vbe.c:160
    mov bp, sp                                ; 89 e5                       ; 0xc3c13
    push bx                                   ; 53                          ; 0xc3c15
    mov bx, ax                                ; 89 c3                       ; 0xc3c16
    mov ax, dx                                ; 89 d0                       ; 0xc3c18
    mov dx, bx                                ; 89 da                       ; 0xc3c1a vbe.c:162
    out DX, ax                                ; ef                          ; 0xc3c1c
    in ax, DX                                 ; ed                          ; 0xc3c1d vbe.c:163
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c1e vbe.c:164
    pop bx                                    ; 5b                          ; 0xc3c21
    pop bp                                    ; 5d                          ; 0xc3c22
    retn                                      ; c3                          ; 0xc3c23
  ; disGetNextSymbol 0xc3c24 LB 0x69f -> off=0x0 cb=0000000000000014 uValue=00000000000c3c24 'in_byte'
in_byte:                                     ; 0xc3c24 LB 0x14
    push bp                                   ; 55                          ; 0xc3c24 vbe.c:166
    mov bp, sp                                ; 89 e5                       ; 0xc3c25
    push bx                                   ; 53                          ; 0xc3c27
    mov bx, ax                                ; 89 c3                       ; 0xc3c28
    mov ax, dx                                ; 89 d0                       ; 0xc3c2a
    mov dx, bx                                ; 89 da                       ; 0xc3c2c vbe.c:168
    out DX, ax                                ; ef                          ; 0xc3c2e
    in AL, DX                                 ; ec                          ; 0xc3c2f vbe.c:169
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3c30
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c32 vbe.c:170
    pop bx                                    ; 5b                          ; 0xc3c35
    pop bp                                    ; 5d                          ; 0xc3c36
    retn                                      ; c3                          ; 0xc3c37
  ; disGetNextSymbol 0xc3c38 LB 0x68b -> off=0x0 cb=0000000000000014 uValue=00000000000c3c38 'dispi_get_id'
dispi_get_id:                                ; 0xc3c38 LB 0x14
    push bp                                   ; 55                          ; 0xc3c38 vbe.c:173
    mov bp, sp                                ; 89 e5                       ; 0xc3c39
    push dx                                   ; 52                          ; 0xc3c3b
    xor ax, ax                                ; 31 c0                       ; 0xc3c3c vbe.c:175
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3c3e
    out DX, ax                                ; ef                          ; 0xc3c41
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3c42 vbe.c:176
    in ax, DX                                 ; ed                          ; 0xc3c45
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c46 vbe.c:177
    pop dx                                    ; 5a                          ; 0xc3c49
    pop bp                                    ; 5d                          ; 0xc3c4a
    retn                                      ; c3                          ; 0xc3c4b
  ; disGetNextSymbol 0xc3c4c LB 0x677 -> off=0x0 cb=000000000000001a uValue=00000000000c3c4c 'dispi_set_id'
dispi_set_id:                                ; 0xc3c4c LB 0x1a
    push bp                                   ; 55                          ; 0xc3c4c vbe.c:179
    mov bp, sp                                ; 89 e5                       ; 0xc3c4d
    push bx                                   ; 53                          ; 0xc3c4f
    push dx                                   ; 52                          ; 0xc3c50
    mov bx, ax                                ; 89 c3                       ; 0xc3c51
    xor ax, ax                                ; 31 c0                       ; 0xc3c53 vbe.c:181
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3c55
    out DX, ax                                ; ef                          ; 0xc3c58
    mov ax, bx                                ; 89 d8                       ; 0xc3c59 vbe.c:182
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3c5b
    out DX, ax                                ; ef                          ; 0xc3c5e
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3c5f vbe.c:183
    pop dx                                    ; 5a                          ; 0xc3c62
    pop bx                                    ; 5b                          ; 0xc3c63
    pop bp                                    ; 5d                          ; 0xc3c64
    retn                                      ; c3                          ; 0xc3c65
  ; disGetNextSymbol 0xc3c66 LB 0x65d -> off=0x0 cb=000000000000002a uValue=00000000000c3c66 'vbe_init'
vbe_init:                                    ; 0xc3c66 LB 0x2a
    push bp                                   ; 55                          ; 0xc3c66 vbe.c:188
    mov bp, sp                                ; 89 e5                       ; 0xc3c67
    push bx                                   ; 53                          ; 0xc3c69
    mov ax, 0b0c0h                            ; b8 c0 b0                    ; 0xc3c6a vbe.c:190
    call 03c4ch                               ; e8 dc ff                    ; 0xc3c6d
    call 03c38h                               ; e8 c5 ff                    ; 0xc3c70 vbe.c:191
    cmp ax, 0b0c0h                            ; 3d c0 b0                    ; 0xc3c73
    jne short 03c8ah                          ; 75 12                       ; 0xc3c76
    mov bx, 000b9h                            ; bb b9 00                    ; 0xc3c78 vbe.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3c7b
    mov es, ax                                ; 8e c0                       ; 0xc3c7e
    mov byte [es:bx], 001h                    ; 26 c6 07 01                 ; 0xc3c80
    mov ax, 0b0c4h                            ; b8 c4 b0                    ; 0xc3c84 vbe.c:194
    call 03c4ch                               ; e8 c2 ff                    ; 0xc3c87
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c8a vbe.c:199
    pop bx                                    ; 5b                          ; 0xc3c8d
    pop bp                                    ; 5d                          ; 0xc3c8e
    retn                                      ; c3                          ; 0xc3c8f
  ; disGetNextSymbol 0xc3c90 LB 0x633 -> off=0x0 cb=000000000000006c uValue=00000000000c3c90 'mode_info_find_mode'
mode_info_find_mode:                         ; 0xc3c90 LB 0x6c
    push bp                                   ; 55                          ; 0xc3c90 vbe.c:202
    mov bp, sp                                ; 89 e5                       ; 0xc3c91
    push bx                                   ; 53                          ; 0xc3c93
    push cx                                   ; 51                          ; 0xc3c94
    push si                                   ; 56                          ; 0xc3c95
    push di                                   ; 57                          ; 0xc3c96
    mov di, ax                                ; 89 c7                       ; 0xc3c97
    mov si, dx                                ; 89 d6                       ; 0xc3c99
    xor dx, dx                                ; 31 d2                       ; 0xc3c9b vbe.c:208
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3c9d
    call 03c12h                               ; e8 6f ff                    ; 0xc3ca0
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3ca3 vbe.c:209
    jne short 03cf1h                          ; 75 49                       ; 0xc3ca6
    test si, si                               ; 85 f6                       ; 0xc3ca8 vbe.c:213
    je short 03cbfh                           ; 74 13                       ; 0xc3caa
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc3cac vbe.c:220
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3caf
    call 00570h                               ; e8 bb c8                    ; 0xc3cb2
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3cb5 vbe.c:221
    call 00577h                               ; e8 bc c8                    ; 0xc3cb8
    test ax, ax                               ; 85 c0                       ; 0xc3cbb vbe.c:222
    je short 03cf3h                           ; 74 34                       ; 0xc3cbd
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc3cbf vbe.c:226
    mov dx, bx                                ; 89 da                       ; 0xc3cc2 vbe.c:232
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3cc4
    call 03c12h                               ; e8 48 ff                    ; 0xc3cc7
    mov cx, ax                                ; 89 c1                       ; 0xc3cca
    cmp cx, strict byte 0ffffh                ; 83 f9 ff                    ; 0xc3ccc vbe.c:233
    je short 03cf1h                           ; 74 20                       ; 0xc3ccf
    lea dx, [bx+002h]                         ; 8d 57 02                    ; 0xc3cd1 vbe.c:235
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3cd4
    call 03c12h                               ; e8 38 ff                    ; 0xc3cd7
    lea dx, [bx+044h]                         ; 8d 57 44                    ; 0xc3cda
    cmp cx, di                                ; 39 f9                       ; 0xc3cdd vbe.c:237
    jne short 03cedh                          ; 75 0c                       ; 0xc3cdf
    test si, si                               ; 85 f6                       ; 0xc3ce1 vbe.c:239
    jne short 03ce9h                          ; 75 04                       ; 0xc3ce3
    mov ax, bx                                ; 89 d8                       ; 0xc3ce5 vbe.c:240
    jmp short 03cf3h                          ; eb 0a                       ; 0xc3ce7
    test AL, strict byte 080h                 ; a8 80                       ; 0xc3ce9 vbe.c:241
    jne short 03ce5h                          ; 75 f8                       ; 0xc3ceb
    mov bx, dx                                ; 89 d3                       ; 0xc3ced vbe.c:244
    jmp short 03cc4h                          ; eb d3                       ; 0xc3cef vbe.c:249
    xor ax, ax                                ; 31 c0                       ; 0xc3cf1 vbe.c:252
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc3cf3 vbe.c:253
    pop di                                    ; 5f                          ; 0xc3cf6
    pop si                                    ; 5e                          ; 0xc3cf7
    pop cx                                    ; 59                          ; 0xc3cf8
    pop bx                                    ; 5b                          ; 0xc3cf9
    pop bp                                    ; 5d                          ; 0xc3cfa
    retn                                      ; c3                          ; 0xc3cfb
  ; disGetNextSymbol 0xc3cfc LB 0x5c7 -> off=0x0 cb=0000000000000125 uValue=00000000000c3cfc 'vbe_biosfn_return_controller_information'
vbe_biosfn_return_controller_information: ; 0xc3cfc LB 0x125
    push bp                                   ; 55                          ; 0xc3cfc vbe.c:284
    mov bp, sp                                ; 89 e5                       ; 0xc3cfd
    push cx                                   ; 51                          ; 0xc3cff
    push si                                   ; 56                          ; 0xc3d00
    push di                                   ; 57                          ; 0xc3d01
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc3d02
    mov si, ax                                ; 89 c6                       ; 0xc3d05
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3d07
    mov di, bx                                ; 89 df                       ; 0xc3d0a
    mov word [bp-00ch], strict word 00022h    ; c7 46 f4 22 00              ; 0xc3d0c vbe.c:289
    call 005b7h                               ; e8 a3 c8                    ; 0xc3d11 vbe.c:292
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc3d14
    mov bx, di                                ; 89 fb                       ; 0xc3d17 vbe.c:295
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3d19
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3d1c
    xor dx, dx                                ; 31 d2                       ; 0xc3d1f vbe.c:298
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3d21
    call 03c12h                               ; e8 eb fe                    ; 0xc3d24
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3d27 vbe.c:299
    je short 03d36h                           ; 74 0a                       ; 0xc3d2a
    push SS                                   ; 16                          ; 0xc3d2c vbe.c:301
    pop ES                                    ; 07                          ; 0xc3d2d
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc3d2e
    jmp near 03e19h                           ; e9 e3 00                    ; 0xc3d33 vbe.c:305
    mov cx, strict word 00004h                ; b9 04 00                    ; 0xc3d36 vbe.c:307
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3d39 vbe.c:314
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3d3e vbe.c:322
    cmp word [es:bx+002h], 03245h             ; 26 81 7f 02 45 32           ; 0xc3d41
    jne short 03d50h                          ; 75 07                       ; 0xc3d47
    cmp word [es:bx], 04256h                  ; 26 81 3f 56 42              ; 0xc3d49
    je short 03d5fh                           ; 74 0f                       ; 0xc3d4e
    cmp word [es:bx+002h], 04153h             ; 26 81 7f 02 53 41           ; 0xc3d50
    jne short 03d64h                          ; 75 0c                       ; 0xc3d56
    cmp word [es:bx], 04556h                  ; 26 81 3f 56 45              ; 0xc3d58
    jne short 03d64h                          ; 75 05                       ; 0xc3d5d
    mov word [bp-00eh], strict word 00001h    ; c7 46 f2 01 00              ; 0xc3d5f vbe.c:324
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3d64 vbe.c:332
    db  066h, 026h, 0c7h, 007h, 056h, 045h, 053h, 041h
    ; mov dword [es:bx], strict dword 041534556h ; 66 26 c7 07 56 45 53 41  ; 0xc3d67
    mov word [es:bx+004h], 00200h             ; 26 c7 47 04 00 02           ; 0xc3d6f vbe.c:338
    mov word [es:bx+006h], 07e00h             ; 26 c7 47 06 00 7e           ; 0xc3d75 vbe.c:341
    mov [es:bx+008h], ds                      ; 26 8c 5f 08                 ; 0xc3d7b
    db  066h, 026h, 0c7h, 047h, 00ah, 001h, 000h, 000h, 000h
    ; mov dword [es:bx+00ah], strict dword 000000001h ; 66 26 c7 47 0a 01 00 00 00; 0xc3d7f vbe.c:344
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3d88 vbe.c:350
    mov word [es:bx+010h], ax                 ; 26 89 47 10                 ; 0xc3d8b
    lea ax, [di+022h]                         ; 8d 45 22                    ; 0xc3d8f vbe.c:351
    mov word [es:bx+00eh], ax                 ; 26 89 47 0e                 ; 0xc3d92
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc3d96 vbe.c:354
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3d99
    call 03c12h                               ; e8 73 fe                    ; 0xc3d9c
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3d9f
    mov word [es:bx+012h], ax                 ; 26 89 47 12                 ; 0xc3da2
    cmp word [bp-00eh], strict byte 00000h    ; 83 7e f2 00                 ; 0xc3da6 vbe.c:356
    je short 03dd0h                           ; 74 24                       ; 0xc3daa
    mov word [es:bx+014h], strict word 00003h ; 26 c7 47 14 03 00           ; 0xc3dac vbe.c:359
    mov word [es:bx+016h], 07e15h             ; 26 c7 47 16 15 7e           ; 0xc3db2 vbe.c:360
    mov [es:bx+018h], ds                      ; 26 8c 5f 18                 ; 0xc3db8
    mov word [es:bx+01ah], 07e32h             ; 26 c7 47 1a 32 7e           ; 0xc3dbc vbe.c:361
    mov [es:bx+01ch], ds                      ; 26 8c 5f 1c                 ; 0xc3dc2
    mov word [es:bx+01eh], 07e50h             ; 26 c7 47 1e 50 7e           ; 0xc3dc6 vbe.c:362
    mov [es:bx+020h], ds                      ; 26 8c 5f 20                 ; 0xc3dcc
    mov dx, cx                                ; 89 ca                       ; 0xc3dd0 vbe.c:369
    add dx, strict byte 0001bh                ; 83 c2 1b                    ; 0xc3dd2
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3dd5
    call 03c24h                               ; e8 49 fe                    ; 0xc3dd8
    xor ah, ah                                ; 30 e4                       ; 0xc3ddb vbe.c:370
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc3ddd
    jnbe short 03df9h                         ; 77 17                       ; 0xc3de0
    mov dx, cx                                ; 89 ca                       ; 0xc3de2 vbe.c:372
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3de4
    call 03c12h                               ; e8 28 fe                    ; 0xc3de7
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc3dea vbe.c:376
    add bx, di                                ; 01 fb                       ; 0xc3ded
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc3def vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3df2
    add word [bp-00ch], strict byte 00002h    ; 83 46 f4 02                 ; 0xc3df5 vbe.c:378
    add cx, strict byte 00044h                ; 83 c1 44                    ; 0xc3df9 vbe.c:380
    mov dx, cx                                ; 89 ca                       ; 0xc3dfc vbe.c:381
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3dfe
    call 03c12h                               ; e8 0e fe                    ; 0xc3e01
    cmp ax, strict word 0ffffh                ; 3d ff ff                    ; 0xc3e04 vbe.c:382
    jne short 03dd0h                          ; 75 c7                       ; 0xc3e07
    add di, word [bp-00ch]                    ; 03 7e f4                    ; 0xc3e09 vbe.c:385
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc3e0c vbe.c:62
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc3e0f
    push SS                                   ; 16                          ; 0xc3e12 vbe.c:386
    pop ES                                    ; 07                          ; 0xc3e13
    mov word [es:si], strict word 0004fh      ; 26 c7 04 4f 00              ; 0xc3e14
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3e19 vbe.c:387
    pop di                                    ; 5f                          ; 0xc3e1c
    pop si                                    ; 5e                          ; 0xc3e1d
    pop cx                                    ; 59                          ; 0xc3e1e
    pop bp                                    ; 5d                          ; 0xc3e1f
    retn                                      ; c3                          ; 0xc3e20
  ; disGetNextSymbol 0xc3e21 LB 0x4a2 -> off=0x0 cb=000000000000009b uValue=00000000000c3e21 'vbe_biosfn_return_mode_information'
vbe_biosfn_return_mode_information:          ; 0xc3e21 LB 0x9b
    push bp                                   ; 55                          ; 0xc3e21 vbe.c:399
    mov bp, sp                                ; 89 e5                       ; 0xc3e22
    push si                                   ; 56                          ; 0xc3e24
    push di                                   ; 57                          ; 0xc3e25
    push ax                                   ; 50                          ; 0xc3e26
    push ax                                   ; 50                          ; 0xc3e27
    mov ax, dx                                ; 89 d0                       ; 0xc3e28
    mov si, bx                                ; 89 de                       ; 0xc3e2a
    mov bx, cx                                ; 89 cb                       ; 0xc3e2c
    test dh, 040h                             ; f6 c6 40                    ; 0xc3e2e vbe.c:410
    db  00fh, 095h, 0c2h
    ; setne dl                                  ; 0f 95 c2                  ; 0xc3e31
    xor dh, dh                                ; 30 f6                       ; 0xc3e34
    and ah, 001h                              ; 80 e4 01                    ; 0xc3e36 vbe.c:411
    call 03c90h                               ; e8 54 fe                    ; 0xc3e39 vbe.c:413
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc3e3c
    test ax, ax                               ; 85 c0                       ; 0xc3e3f vbe.c:415
    je short 03eaah                           ; 74 67                       ; 0xc3e41
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3e43 vbe.c:420
    xor ax, ax                                ; 31 c0                       ; 0xc3e46
    mov di, bx                                ; 89 df                       ; 0xc3e48
    mov es, si                                ; 8e c6                       ; 0xc3e4a
    jcxz 03e50h                               ; e3 02                       ; 0xc3e4c
    rep stosb                                 ; f3 aa                       ; 0xc3e4e
    xor cx, cx                                ; 31 c9                       ; 0xc3e50 vbe.c:421
    jmp short 03e59h                          ; eb 05                       ; 0xc3e52
    cmp cx, strict byte 00042h                ; 83 f9 42                    ; 0xc3e54
    jnc short 03e72h                          ; 73 19                       ; 0xc3e57
    mov dx, word [bp-006h]                    ; 8b 56 fa                    ; 0xc3e59 vbe.c:424
    inc dx                                    ; 42                          ; 0xc3e5c
    inc dx                                    ; 42                          ; 0xc3e5d
    add dx, cx                                ; 01 ca                       ; 0xc3e5e
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3e60
    call 03c24h                               ; e8 be fd                    ; 0xc3e63
    mov di, bx                                ; 89 df                       ; 0xc3e66 vbe.c:425
    add di, cx                                ; 01 cf                       ; 0xc3e68
    mov es, si                                ; 8e c6                       ; 0xc3e6a vbe.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc3e6c
    inc cx                                    ; 41                          ; 0xc3e6f vbe.c:426
    jmp short 03e54h                          ; eb e2                       ; 0xc3e70
    lea di, [bx+002h]                         ; 8d 7f 02                    ; 0xc3e72 vbe.c:427
    mov es, si                                ; 8e c6                       ; 0xc3e75 vbe.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc3e77
    test AL, strict byte 001h                 ; a8 01                       ; 0xc3e7a vbe.c:428
    je short 03e8eh                           ; 74 10                       ; 0xc3e7c
    lea di, [bx+00ch]                         ; 8d 7f 0c                    ; 0xc3e7e vbe.c:429
    mov word [es:di], 00629h                  ; 26 c7 05 29 06              ; 0xc3e81 vbe.c:62
    lea di, [bx+00eh]                         ; 8d 7f 0e                    ; 0xc3e86 vbe.c:431
    mov word [es:di], 0c000h                  ; 26 c7 05 00 c0              ; 0xc3e89 vbe.c:62
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc3e8e vbe.c:434
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e91
    call 00570h                               ; e8 d9 c6                    ; 0xc3e94
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e97 vbe.c:435
    call 00577h                               ; e8 da c6                    ; 0xc3e9a
    add bx, strict byte 0002ah                ; 83 c3 2a                    ; 0xc3e9d
    mov es, si                                ; 8e c6                       ; 0xc3ea0 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3ea2
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc3ea5 vbe.c:437
    jmp short 03eadh                          ; eb 03                       ; 0xc3ea8 vbe.c:438
    mov ax, 00100h                            ; b8 00 01                    ; 0xc3eaa vbe.c:442
    push SS                                   ; 16                          ; 0xc3ead vbe.c:445
    pop ES                                    ; 07                          ; 0xc3eae
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc3eaf
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3eb2
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3eb5 vbe.c:446
    pop di                                    ; 5f                          ; 0xc3eb8
    pop si                                    ; 5e                          ; 0xc3eb9
    pop bp                                    ; 5d                          ; 0xc3eba
    retn                                      ; c3                          ; 0xc3ebb
  ; disGetNextSymbol 0xc3ebc LB 0x407 -> off=0x0 cb=00000000000000e5 uValue=00000000000c3ebc 'vbe_biosfn_set_mode'
vbe_biosfn_set_mode:                         ; 0xc3ebc LB 0xe5
    push bp                                   ; 55                          ; 0xc3ebc vbe.c:458
    mov bp, sp                                ; 89 e5                       ; 0xc3ebd
    push si                                   ; 56                          ; 0xc3ebf
    push di                                   ; 57                          ; 0xc3ec0
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc3ec1
    mov si, ax                                ; 89 c6                       ; 0xc3ec4
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3ec6
    test byte [bp-009h], 040h                 ; f6 46 f7 40                 ; 0xc3ec9 vbe.c:466
    db  00fh, 095h, 0c0h
    ; setne al                                  ; 0f 95 c0                  ; 0xc3ecd
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3ed0
    mov ax, dx                                ; 89 d0                       ; 0xc3ed3
    test dx, dx                               ; 85 d2                       ; 0xc3ed5 vbe.c:467
    je short 03edch                           ; 74 03                       ; 0xc3ed7
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3ed9
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc3edc
    test byte [bp-009h], 080h                 ; f6 46 f7 80                 ; 0xc3edf vbe.c:468
    je short 03eeah                           ; 74 05                       ; 0xc3ee3
    mov dx, 00080h                            ; ba 80 00                    ; 0xc3ee5
    jmp short 03eech                          ; eb 02                       ; 0xc3ee8
    xor dx, dx                                ; 31 d2                       ; 0xc3eea
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc3eec
    and byte [bp-009h], 001h                  ; 80 66 f7 01                 ; 0xc3eef vbe.c:470
    cmp word [bp-00ah], 00100h                ; 81 7e f6 00 01              ; 0xc3ef3 vbe.c:473
    jnc short 03f0ch                          ; 73 12                       ; 0xc3ef8
    xor ax, ax                                ; 31 c0                       ; 0xc3efa vbe.c:477
    call 005ddh                               ; e8 de c6                    ; 0xc3efc
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc3eff vbe.c:481
    call 013cbh                               ; e8 c5 d4                    ; 0xc3f03
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc3f06 vbe.c:482
    jmp near 03f95h                           ; e9 89 00                    ; 0xc3f09 vbe.c:483
    mov dx, ax                                ; 89 c2                       ; 0xc3f0c vbe.c:486
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3f0e
    call 03c90h                               ; e8 7c fd                    ; 0xc3f11
    mov bx, ax                                ; 89 c3                       ; 0xc3f14
    test ax, ax                               ; 85 c0                       ; 0xc3f16 vbe.c:488
    je short 03f92h                           ; 74 78                       ; 0xc3f18
    lea dx, [bx+014h]                         ; 8d 57 14                    ; 0xc3f1a vbe.c:493
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f1d
    call 03c12h                               ; e8 ef fc                    ; 0xc3f20
    mov cx, ax                                ; 89 c1                       ; 0xc3f23
    lea dx, [bx+016h]                         ; 8d 57 16                    ; 0xc3f25 vbe.c:494
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f28
    call 03c12h                               ; e8 e4 fc                    ; 0xc3f2b
    mov di, ax                                ; 89 c7                       ; 0xc3f2e
    lea dx, [bx+01bh]                         ; 8d 57 1b                    ; 0xc3f30 vbe.c:495
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f33
    call 03c24h                               ; e8 eb fc                    ; 0xc3f36
    mov bl, al                                ; 88 c3                       ; 0xc3f39
    mov dl, al                                ; 88 c2                       ; 0xc3f3b
    xor ax, ax                                ; 31 c0                       ; 0xc3f3d vbe.c:503
    call 005ddh                               ; e8 9b c6                    ; 0xc3f3f
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc3f42 vbe.c:505
    jne short 03f4dh                          ; 75 06                       ; 0xc3f45
    mov ax, strict word 0006ah                ; b8 6a 00                    ; 0xc3f47 vbe.c:507
    call 013cbh                               ; e8 7e d4                    ; 0xc3f4a
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc3f4d vbe.c:510
    call 03b89h                               ; e8 36 fc                    ; 0xc3f50
    mov ax, cx                                ; 89 c8                       ; 0xc3f53 vbe.c:511
    call 03b32h                               ; e8 da fb                    ; 0xc3f55
    mov ax, di                                ; 89 f8                       ; 0xc3f58 vbe.c:512
    call 03b51h                               ; e8 f4 fb                    ; 0xc3f5a
    xor ax, ax                                ; 31 c0                       ; 0xc3f5d vbe.c:513
    call 00603h                               ; e8 a1 c6                    ; 0xc3f5f
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3f62 vbe.c:514
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc3f65
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3f67
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc3f6a
    or ax, dx                                 ; 09 d0                       ; 0xc3f6e
    call 005ddh                               ; e8 6a c6                    ; 0xc3f70
    call 006d2h                               ; e8 5c c7                    ; 0xc3f73 vbe.c:515
    mov bx, 000bah                            ; bb ba 00                    ; 0xc3f76 vbe.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3f79
    mov es, ax                                ; 8e c0                       ; 0xc3f7c
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3f7e
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3f81
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3f84 vbe.c:518
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc3f87
    mov bx, 00087h                            ; bb 87 00                    ; 0xc3f89 vbe.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3f8c
    jmp near 03f06h                           ; e9 74 ff                    ; 0xc3f8f
    mov ax, 00100h                            ; b8 00 01                    ; 0xc3f92 vbe.c:527
    push SS                                   ; 16                          ; 0xc3f95 vbe.c:531
    pop ES                                    ; 07                          ; 0xc3f96
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3f97
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3f9a vbe.c:532
    pop di                                    ; 5f                          ; 0xc3f9d
    pop si                                    ; 5e                          ; 0xc3f9e
    pop bp                                    ; 5d                          ; 0xc3f9f
    retn                                      ; c3                          ; 0xc3fa0
  ; disGetNextSymbol 0xc3fa1 LB 0x322 -> off=0x0 cb=0000000000000008 uValue=00000000000c3fa1 'vbe_biosfn_read_video_state_size'
vbe_biosfn_read_video_state_size:            ; 0xc3fa1 LB 0x8
    push bp                                   ; 55                          ; 0xc3fa1 vbe.c:534
    mov bp, sp                                ; 89 e5                       ; 0xc3fa2
    mov ax, strict word 00012h                ; b8 12 00                    ; 0xc3fa4 vbe.c:537
    pop bp                                    ; 5d                          ; 0xc3fa7
    retn                                      ; c3                          ; 0xc3fa8
  ; disGetNextSymbol 0xc3fa9 LB 0x31a -> off=0x0 cb=000000000000004b uValue=00000000000c3fa9 'vbe_biosfn_save_video_state'
vbe_biosfn_save_video_state:                 ; 0xc3fa9 LB 0x4b
    push bp                                   ; 55                          ; 0xc3fa9 vbe.c:539
    mov bp, sp                                ; 89 e5                       ; 0xc3faa
    push bx                                   ; 53                          ; 0xc3fac
    push cx                                   ; 51                          ; 0xc3fad
    push si                                   ; 56                          ; 0xc3fae
    mov si, ax                                ; 89 c6                       ; 0xc3faf
    mov bx, dx                                ; 89 d3                       ; 0xc3fb1
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc3fb3 vbe.c:543
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3fb6
    out DX, ax                                ; ef                          ; 0xc3fb9
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3fba vbe.c:544
    in ax, DX                                 ; ed                          ; 0xc3fbd
    mov es, si                                ; 8e c6                       ; 0xc3fbe vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3fc0
    inc bx                                    ; 43                          ; 0xc3fc3 vbe.c:546
    inc bx                                    ; 43                          ; 0xc3fc4
    test AL, strict byte 001h                 ; a8 01                       ; 0xc3fc5 vbe.c:547
    je short 03fech                           ; 74 23                       ; 0xc3fc7
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc3fc9 vbe.c:549
    jmp short 03fd3h                          ; eb 05                       ; 0xc3fcc
    cmp cx, strict byte 00009h                ; 83 f9 09                    ; 0xc3fce
    jnbe short 03fech                         ; 77 19                       ; 0xc3fd1
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc3fd3 vbe.c:550
    je short 03fe9h                           ; 74 11                       ; 0xc3fd6
    mov ax, cx                                ; 89 c8                       ; 0xc3fd8 vbe.c:551
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3fda
    out DX, ax                                ; ef                          ; 0xc3fdd
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3fde vbe.c:552
    in ax, DX                                 ; ed                          ; 0xc3fe1
    mov es, si                                ; 8e c6                       ; 0xc3fe2 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3fe4
    inc bx                                    ; 43                          ; 0xc3fe7 vbe.c:553
    inc bx                                    ; 43                          ; 0xc3fe8
    inc cx                                    ; 41                          ; 0xc3fe9 vbe.c:555
    jmp short 03fceh                          ; eb e2                       ; 0xc3fea
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3fec vbe.c:556
    pop si                                    ; 5e                          ; 0xc3fef
    pop cx                                    ; 59                          ; 0xc3ff0
    pop bx                                    ; 5b                          ; 0xc3ff1
    pop bp                                    ; 5d                          ; 0xc3ff2
    retn                                      ; c3                          ; 0xc3ff3
  ; disGetNextSymbol 0xc3ff4 LB 0x2cf -> off=0x0 cb=000000000000008f uValue=00000000000c3ff4 'vbe_biosfn_restore_video_state'
vbe_biosfn_restore_video_state:              ; 0xc3ff4 LB 0x8f
    push bp                                   ; 55                          ; 0xc3ff4 vbe.c:559
    mov bp, sp                                ; 89 e5                       ; 0xc3ff5
    push bx                                   ; 53                          ; 0xc3ff7
    push cx                                   ; 51                          ; 0xc3ff8
    push si                                   ; 56                          ; 0xc3ff9
    push ax                                   ; 50                          ; 0xc3ffa
    mov cx, ax                                ; 89 c1                       ; 0xc3ffb
    mov bx, dx                                ; 89 d3                       ; 0xc3ffd
    mov es, ax                                ; 8e c0                       ; 0xc3fff vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4001
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc4004
    inc bx                                    ; 43                          ; 0xc4007 vbe.c:564
    inc bx                                    ; 43                          ; 0xc4008
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc4009 vbe.c:566
    jne short 0401fh                          ; 75 10                       ; 0xc400d
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc400f vbe.c:567
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4012
    out DX, ax                                ; ef                          ; 0xc4015
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc4016 vbe.c:568
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4019
    out DX, ax                                ; ef                          ; 0xc401c
    jmp short 0407bh                          ; eb 5c                       ; 0xc401d vbe.c:569
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc401f vbe.c:570
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4022
    out DX, ax                                ; ef                          ; 0xc4025
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4026 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4029 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc402c
    inc bx                                    ; 43                          ; 0xc402d vbe.c:572
    inc bx                                    ; 43                          ; 0xc402e
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc402f
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4032
    out DX, ax                                ; ef                          ; 0xc4035
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4036 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4039 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc403c
    inc bx                                    ; 43                          ; 0xc403d vbe.c:575
    inc bx                                    ; 43                          ; 0xc403e
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc403f
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4042
    out DX, ax                                ; ef                          ; 0xc4045
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4046 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4049 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc404c
    inc bx                                    ; 43                          ; 0xc404d vbe.c:578
    inc bx                                    ; 43                          ; 0xc404e
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc404f
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4052
    out DX, ax                                ; ef                          ; 0xc4055
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc4056 vbe.c:580
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4059
    out DX, ax                                ; ef                          ; 0xc405c
    mov si, strict word 00005h                ; be 05 00                    ; 0xc405d vbe.c:582
    jmp short 04067h                          ; eb 05                       ; 0xc4060
    cmp si, strict byte 00009h                ; 83 fe 09                    ; 0xc4062
    jnbe short 0407bh                         ; 77 14                       ; 0xc4065
    mov ax, si                                ; 89 f0                       ; 0xc4067 vbe.c:583
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4069
    out DX, ax                                ; ef                          ; 0xc406c
    mov es, cx                                ; 8e c1                       ; 0xc406d vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc406f
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4072 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc4075
    inc bx                                    ; 43                          ; 0xc4076 vbe.c:585
    inc bx                                    ; 43                          ; 0xc4077
    inc si                                    ; 46                          ; 0xc4078 vbe.c:586
    jmp short 04062h                          ; eb e7                       ; 0xc4079
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc407b vbe.c:588
    pop si                                    ; 5e                          ; 0xc407e
    pop cx                                    ; 59                          ; 0xc407f
    pop bx                                    ; 5b                          ; 0xc4080
    pop bp                                    ; 5d                          ; 0xc4081
    retn                                      ; c3                          ; 0xc4082
  ; disGetNextSymbol 0xc4083 LB 0x240 -> off=0x0 cb=000000000000008c uValue=00000000000c4083 'vbe_biosfn_save_restore_state'
vbe_biosfn_save_restore_state:               ; 0xc4083 LB 0x8c
    push bp                                   ; 55                          ; 0xc4083 vbe.c:604
    mov bp, sp                                ; 89 e5                       ; 0xc4084
    push si                                   ; 56                          ; 0xc4086
    push di                                   ; 57                          ; 0xc4087
    push ax                                   ; 50                          ; 0xc4088
    mov si, ax                                ; 89 c6                       ; 0xc4089
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc408b
    mov ax, bx                                ; 89 d8                       ; 0xc408e
    mov bx, word [bp+004h]                    ; 8b 5e 04                    ; 0xc4090
    mov di, strict word 0004fh                ; bf 4f 00                    ; 0xc4093 vbe.c:609
    xor ah, ah                                ; 30 e4                       ; 0xc4096 vbe.c:610
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc4098
    je short 040e2h                           ; 74 45                       ; 0xc409b
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc409d
    je short 040c6h                           ; 74 24                       ; 0xc40a0
    test ax, ax                               ; 85 c0                       ; 0xc40a2
    jne short 040feh                          ; 75 58                       ; 0xc40a4
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc40a6 vbe.c:612
    call 0302eh                               ; e8 82 ef                    ; 0xc40a9
    mov cx, ax                                ; 89 c1                       ; 0xc40ac
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc40ae vbe.c:616
    je short 040b9h                           ; 74 05                       ; 0xc40b2
    call 03fa1h                               ; e8 ea fe                    ; 0xc40b4 vbe.c:617
    add ax, cx                                ; 01 c8                       ; 0xc40b7
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc40b9 vbe.c:618
    shr ax, 006h                              ; c1 e8 06                    ; 0xc40bc
    push SS                                   ; 16                          ; 0xc40bf
    pop ES                                    ; 07                          ; 0xc40c0
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc40c1
    jmp short 04101h                          ; eb 3b                       ; 0xc40c4 vbe.c:619
    push SS                                   ; 16                          ; 0xc40c6 vbe.c:621
    pop ES                                    ; 07                          ; 0xc40c7
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc40c8
    mov dx, cx                                ; 89 ca                       ; 0xc40cb vbe.c:622
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc40cd
    call 03069h                               ; e8 96 ef                    ; 0xc40d0
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc40d3 vbe.c:626
    je short 04101h                           ; 74 28                       ; 0xc40d7
    mov dx, ax                                ; 89 c2                       ; 0xc40d9 vbe.c:627
    mov ax, cx                                ; 89 c8                       ; 0xc40db
    call 03fa9h                               ; e8 c9 fe                    ; 0xc40dd
    jmp short 04101h                          ; eb 1f                       ; 0xc40e0 vbe.c:628
    push SS                                   ; 16                          ; 0xc40e2 vbe.c:630
    pop ES                                    ; 07                          ; 0xc40e3
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc40e4
    mov dx, cx                                ; 89 ca                       ; 0xc40e7 vbe.c:631
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc40e9
    call 0333fh                               ; e8 50 f2                    ; 0xc40ec
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc40ef vbe.c:635
    je short 04101h                           ; 74 0c                       ; 0xc40f3
    mov dx, ax                                ; 89 c2                       ; 0xc40f5 vbe.c:636
    mov ax, cx                                ; 89 c8                       ; 0xc40f7
    call 03ff4h                               ; e8 f8 fe                    ; 0xc40f9
    jmp short 04101h                          ; eb 03                       ; 0xc40fc vbe.c:637
    mov di, 00100h                            ; bf 00 01                    ; 0xc40fe vbe.c:640
    push SS                                   ; 16                          ; 0xc4101 vbe.c:643
    pop ES                                    ; 07                          ; 0xc4102
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc4103
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4106 vbe.c:644
    pop di                                    ; 5f                          ; 0xc4109
    pop si                                    ; 5e                          ; 0xc410a
    pop bp                                    ; 5d                          ; 0xc410b
    retn 00002h                               ; c2 02 00                    ; 0xc410c
  ; disGetNextSymbol 0xc410f LB 0x1b4 -> off=0x0 cb=00000000000000cf uValue=00000000000c410f 'vbe_biosfn_get_set_scanline_length'
vbe_biosfn_get_set_scanline_length:          ; 0xc410f LB 0xcf
    push bp                                   ; 55                          ; 0xc410f vbe.c:665
    mov bp, sp                                ; 89 e5                       ; 0xc4110
    push si                                   ; 56                          ; 0xc4112
    push di                                   ; 57                          ; 0xc4113
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc4114
    push ax                                   ; 50                          ; 0xc4117
    mov di, dx                                ; 89 d7                       ; 0xc4118
    mov si, bx                                ; 89 de                       ; 0xc411a
    mov word [bp-008h], cx                    ; 89 4e f8                    ; 0xc411c
    call 03ba8h                               ; e8 86 fa                    ; 0xc411f vbe.c:674
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc4122 vbe.c:675
    jne short 0412bh                          ; 75 05                       ; 0xc4124
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc4126
    jmp short 0412eh                          ; eb 03                       ; 0xc4129
    movzx cx, al                              ; 0f b6 c8                    ; 0xc412b
    call 03be0h                               ; e8 af fa                    ; 0xc412e vbe.c:676
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc4131
    mov word [bp-006h], strict word 0004fh    ; c7 46 fa 4f 00              ; 0xc4134 vbe.c:677
    push SS                                   ; 16                          ; 0xc4139 vbe.c:678
    pop ES                                    ; 07                          ; 0xc413a
    mov bx, word [es:si]                      ; 26 8b 1c                    ; 0xc413b
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc413e vbe.c:679
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc4141 vbe.c:683
    je short 04150h                           ; 74 0b                       ; 0xc4143
    cmp AL, strict byte 001h                  ; 3c 01                       ; 0xc4145
    je short 04177h                           ; 74 2e                       ; 0xc4147
    test al, al                               ; 84 c0                       ; 0xc4149
    je short 04172h                           ; 74 25                       ; 0xc414b
    jmp near 041c7h                           ; e9 77 00                    ; 0xc414d
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc4150 vbe.c:685
    jne short 0415ah                          ; 75 05                       ; 0xc4153
    sal bx, 003h                              ; c1 e3 03                    ; 0xc4155 vbe.c:686
    jmp short 04172h                          ; eb 18                       ; 0xc4158 vbe.c:687
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc415a vbe.c:688
    cwd                                       ; 99                          ; 0xc415d
    sal dx, 003h                              ; c1 e2 03                    ; 0xc415e
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc4161
    sar ax, 003h                              ; c1 f8 03                    ; 0xc4163
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc4166
    mov ax, bx                                ; 89 d8                       ; 0xc4169
    xor dx, dx                                ; 31 d2                       ; 0xc416b
    div word [bp-00ch]                        ; f7 76 f4                    ; 0xc416d
    mov bx, ax                                ; 89 c3                       ; 0xc4170
    mov ax, bx                                ; 89 d8                       ; 0xc4172 vbe.c:691
    call 03bc1h                               ; e8 4a fa                    ; 0xc4174
    call 03be0h                               ; e8 66 fa                    ; 0xc4177 vbe.c:694
    mov bx, ax                                ; 89 c3                       ; 0xc417a
    push SS                                   ; 16                          ; 0xc417c vbe.c:695
    pop ES                                    ; 07                          ; 0xc417d
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc417e
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc4181 vbe.c:696
    jne short 0418bh                          ; 75 05                       ; 0xc4184
    shr bx, 003h                              ; c1 eb 03                    ; 0xc4186 vbe.c:697
    jmp short 0419ah                          ; eb 0f                       ; 0xc4189 vbe.c:698
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc418b vbe.c:699
    cwd                                       ; 99                          ; 0xc418e
    sal dx, 003h                              ; c1 e2 03                    ; 0xc418f
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc4192
    sar ax, 003h                              ; c1 f8 03                    ; 0xc4194
    imul bx, ax                               ; 0f af d8                    ; 0xc4197
    add bx, strict byte 00003h                ; 83 c3 03                    ; 0xc419a vbe.c:700
    and bl, 0fch                              ; 80 e3 fc                    ; 0xc419d
    push SS                                   ; 16                          ; 0xc41a0 vbe.c:701
    pop ES                                    ; 07                          ; 0xc41a1
    mov word [es:di], bx                      ; 26 89 1d                    ; 0xc41a2
    call 03bf9h                               ; e8 51 fa                    ; 0xc41a5 vbe.c:702
    push SS                                   ; 16                          ; 0xc41a8
    pop ES                                    ; 07                          ; 0xc41a9
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc41aa
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc41ad
    call 03b70h                               ; e8 bd f9                    ; 0xc41b0 vbe.c:703
    push SS                                   ; 16                          ; 0xc41b3
    pop ES                                    ; 07                          ; 0xc41b4
    cmp ax, word [es:bx]                      ; 26 3b 07                    ; 0xc41b5
    jbe short 041cch                          ; 76 12                       ; 0xc41b8
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc41ba vbe.c:704
    call 03bc1h                               ; e8 01 fa                    ; 0xc41bd
    mov word [bp-006h], 00200h                ; c7 46 fa 00 02              ; 0xc41c0 vbe.c:705
    jmp short 041cch                          ; eb 05                       ; 0xc41c5 vbe.c:707
    mov word [bp-006h], 00100h                ; c7 46 fa 00 01              ; 0xc41c7 vbe.c:710
    push SS                                   ; 16                          ; 0xc41cc vbe.c:713
    pop ES                                    ; 07                          ; 0xc41cd
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc41ce
    mov bx, word [bp-00eh]                    ; 8b 5e f2                    ; 0xc41d1
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc41d4
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc41d7 vbe.c:714
    pop di                                    ; 5f                          ; 0xc41da
    pop si                                    ; 5e                          ; 0xc41db
    pop bp                                    ; 5d                          ; 0xc41dc
    retn                                      ; c3                          ; 0xc41dd
  ; disGetNextSymbol 0xc41de LB 0xe5 -> off=0x0 cb=00000000000000e5 uValue=00000000000c41de 'private_biosfn_custom_mode'
private_biosfn_custom_mode:                  ; 0xc41de LB 0xe5
    push bp                                   ; 55                          ; 0xc41de vbe.c:740
    mov bp, sp                                ; 89 e5                       ; 0xc41df
    push si                                   ; 56                          ; 0xc41e1
    push di                                   ; 57                          ; 0xc41e2
    push ax                                   ; 50                          ; 0xc41e3
    push ax                                   ; 50                          ; 0xc41e4
    push ax                                   ; 50                          ; 0xc41e5
    mov si, dx                                ; 89 d6                       ; 0xc41e6
    mov dx, cx                                ; 89 ca                       ; 0xc41e8
    mov di, strict word 0004fh                ; bf 4f 00                    ; 0xc41ea vbe.c:753
    push SS                                   ; 16                          ; 0xc41ed vbe.c:754
    pop ES                                    ; 07                          ; 0xc41ee
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc41ef
    test al, al                               ; 84 c0                       ; 0xc41f2 vbe.c:755
    jne short 04218h                          ; 75 22                       ; 0xc41f4
    push SS                                   ; 16                          ; 0xc41f6 vbe.c:757
    pop ES                                    ; 07                          ; 0xc41f7
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc41f8
    mov bx, dx                                ; 89 d3                       ; 0xc41fb vbe.c:758
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc41fd
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc4200 vbe.c:759
    shr ax, 008h                              ; c1 e8 08                    ; 0xc4203
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc4206
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc4209
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc420c vbe.c:764
    je short 0421eh                           ; 74 0e                       ; 0xc420e
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc4210
    je short 0421eh                           ; 74 0a                       ; 0xc4212
    cmp AL, strict byte 020h                  ; 3c 20                       ; 0xc4214
    je short 0421eh                           ; 74 06                       ; 0xc4216
    mov di, 00100h                            ; bf 00 01                    ; 0xc4218 vbe.c:765
    jmp near 042b4h                           ; e9 96 00                    ; 0xc421b vbe.c:766
    push SS                                   ; 16                          ; 0xc421e vbe.c:770
    pop ES                                    ; 07                          ; 0xc421f
    test byte [es:si+001h], 080h              ; 26 f6 44 01 80              ; 0xc4220
    je short 0422ch                           ; 74 05                       ; 0xc4225
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc4227
    jmp short 0422eh                          ; eb 02                       ; 0xc422a
    xor ax, ax                                ; 31 c0                       ; 0xc422c
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc422e
    cmp cx, 00280h                            ; 81 f9 80 02                 ; 0xc4231 vbe.c:773
    jnc short 0423ch                          ; 73 05                       ; 0xc4235
    mov cx, 00280h                            ; b9 80 02                    ; 0xc4237 vbe.c:774
    jmp short 04245h                          ; eb 09                       ; 0xc423a vbe.c:775
    cmp cx, 00a00h                            ; 81 f9 00 0a                 ; 0xc423c
    jbe short 04245h                          ; 76 03                       ; 0xc4240
    mov cx, 00a00h                            ; b9 00 0a                    ; 0xc4242 vbe.c:776
    cmp bx, 001e0h                            ; 81 fb e0 01                 ; 0xc4245 vbe.c:777
    jnc short 04250h                          ; 73 05                       ; 0xc4249
    mov bx, 001e0h                            ; bb e0 01                    ; 0xc424b vbe.c:778
    jmp short 04259h                          ; eb 09                       ; 0xc424e vbe.c:779
    cmp bx, 00780h                            ; 81 fb 80 07                 ; 0xc4250
    jbe short 04259h                          ; 76 03                       ; 0xc4254
    mov bx, 00780h                            ; bb 80 07                    ; 0xc4256 vbe.c:780
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc4259 vbe.c:786
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc425c
    call 03c12h                               ; e8 b0 f9                    ; 0xc425f
    mov si, ax                                ; 89 c6                       ; 0xc4262
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc4264 vbe.c:789
    cwd                                       ; 99                          ; 0xc4268
    sal dx, 003h                              ; c1 e2 03                    ; 0xc4269
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc426c
    sar ax, 003h                              ; c1 f8 03                    ; 0xc426e
    imul ax, cx                               ; 0f af c1                    ; 0xc4271
    add ax, strict word 00003h                ; 05 03 00                    ; 0xc4274 vbe.c:790
    and AL, strict byte 0fch                  ; 24 fc                       ; 0xc4277
    mov dx, bx                                ; 89 da                       ; 0xc4279 vbe.c:792
    mul dx                                    ; f7 e2                       ; 0xc427b
    cmp dx, si                                ; 39 f2                       ; 0xc427d vbe.c:794
    jnbe short 04287h                         ; 77 06                       ; 0xc427f
    jne short 0428ch                          ; 75 09                       ; 0xc4281
    test ax, ax                               ; 85 c0                       ; 0xc4283
    jbe short 0428ch                          ; 76 05                       ; 0xc4285
    mov di, 00200h                            ; bf 00 02                    ; 0xc4287 vbe.c:796
    jmp short 042b4h                          ; eb 28                       ; 0xc428a vbe.c:797
    xor ax, ax                                ; 31 c0                       ; 0xc428c vbe.c:801
    call 005ddh                               ; e8 4c c3                    ; 0xc428e
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc4291 vbe.c:802
    call 03b89h                               ; e8 f1 f8                    ; 0xc4295
    mov ax, cx                                ; 89 c8                       ; 0xc4298 vbe.c:803
    call 03b32h                               ; e8 95 f8                    ; 0xc429a
    mov ax, bx                                ; 89 d8                       ; 0xc429d vbe.c:804
    call 03b51h                               ; e8 af f8                    ; 0xc429f
    xor ax, ax                                ; 31 c0                       ; 0xc42a2 vbe.c:805
    call 00603h                               ; e8 5c c3                    ; 0xc42a4
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc42a7 vbe.c:806
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc42aa
    xor ah, ah                                ; 30 e4                       ; 0xc42ac
    call 005ddh                               ; e8 2c c3                    ; 0xc42ae
    call 006d2h                               ; e8 1e c4                    ; 0xc42b1 vbe.c:807
    push SS                                   ; 16                          ; 0xc42b4 vbe.c:815
    pop ES                                    ; 07                          ; 0xc42b5
    mov bx, word [bp-00ah]                    ; 8b 5e f6                    ; 0xc42b6
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc42b9
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc42bc vbe.c:816
    pop di                                    ; 5f                          ; 0xc42bf
    pop si                                    ; 5e                          ; 0xc42c0
    pop bp                                    ; 5d                          ; 0xc42c1
    retn                                      ; c3                          ; 0xc42c2

  ; Padding 0x37d bytes at 0xc42c3
  times 893 db 0

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
    db  02fh, 056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh, 073h, 033h, 038h, 036h, 02fh
    db  056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh, 073h, 033h, 038h, 036h, 02eh, 073h
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
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 044h
