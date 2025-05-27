; $Id: VBoxVgaBiosAlternative286.asm 169020 2025-05-27 12:59:42Z vadim.galitsyn@oracle.com $ 
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
    call 038d9h                               ; e8 ef 37                    ; 0xc00e7 vgarom.asm:210
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
    xchg al, ah                               ; 86 e0                       ; 0xc039e vgarom.asm:726
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
    xchg ah, al                               ; 86 c4                       ; 0xc0570 vberom.asm:69
    out DX, AL                                ; ee                          ; 0xc0572 vberom.asm:70
    xchg ah, al                               ; 86 c4                       ; 0xc0573 vberom.asm:71
    out DX, AL                                ; ee                          ; 0xc0575 vberom.asm:72
    retn                                      ; c3                          ; 0xc0576 vberom.asm:73
  ; disGetNextSymbol 0xc0577 LB 0x383 -> off=0x0 cb=0000000000000040 uValue=00000000000c0577 'do_in_ax_dx'
do_in_ax_dx:                                 ; 0xc0577 LB 0x40
    in AL, DX                                 ; ec                          ; 0xc0577 vberom.asm:76
    xchg ah, al                               ; 86 c4                       ; 0xc0578 vberom.asm:77
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

section _TEXT progbits vstart=0x9f0 align=1 ; size=0x3b42 class=CODE group=AUTO
  ; disGetNextSymbol 0xc09f0 LB 0x3b42 -> off=0x0 cb=000000000000001b uValue=00000000000c09f0 'set_int_vector'
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
  ; disGetNextSymbol 0xc0a0b LB 0x3b27 -> off=0x0 cb=000000000000001c uValue=00000000000c0a0b 'init_vga_card'
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
  ; disGetNextSymbol 0xc0a27 LB 0x3b0b -> off=0x0 cb=000000000000003e uValue=00000000000c0a27 'init_bios_area'
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
    mov word [es:bx+000a8h], 05553h           ; 26 c7 87 a8 00 53 55        ; 0xc0a56 vgabios.c:242
    mov [es:bx+000aah], ds                    ; 26 8c 9f aa 00              ; 0xc0a5d
    pop bp                                    ; 5d                          ; 0xc0a62 vgabios.c:243
    pop bx                                    ; 5b                          ; 0xc0a63
    retn                                      ; c3                          ; 0xc0a64
  ; disGetNextSymbol 0xc0a65 LB 0x3acd -> off=0x0 cb=0000000000000031 uValue=00000000000c0a65 'vgabios_init_func'
vgabios_init_func:                           ; 0xc0a65 LB 0x31
    inc bp                                    ; 45                          ; 0xc0a65 vgabios.c:250
    push bp                                   ; 55                          ; 0xc0a66
    mov bp, sp                                ; 89 e5                       ; 0xc0a67
    call 00a0bh                               ; e8 9f ff                    ; 0xc0a69 vgabios.c:252
    call 00a27h                               ; e8 b8 ff                    ; 0xc0a6c vgabios.c:253
    call 03each                               ; e8 3a 34                    ; 0xc0a6f vgabios.c:255
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
  ; disGetNextSymbol 0xc0a96 LB 0x3a9c -> off=0x0 cb=0000000000000040 uValue=00000000000c0a96 'vga_get_cursor_pos'
vga_get_cursor_pos:                          ; 0xc0a96 LB 0x40
    push si                                   ; 56                          ; 0xc0a96 vgabios.c:356
    push di                                   ; 57                          ; 0xc0a97
    push bp                                   ; 55                          ; 0xc0a98
    mov bp, sp                                ; 89 e5                       ; 0xc0a99
    mov si, dx                                ; 89 d6                       ; 0xc0a9b
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc0a9d vgabios.c:358
    jbe short 00aafh                          ; 76 0e                       ; 0xc0a9f
    push SS                                   ; 16                          ; 0xc0aa1 vgabios.c:359
    pop ES                                    ; 07                          ; 0xc0aa2
    mov word [es:si], strict word 00000h      ; 26 c7 04 00 00              ; 0xc0aa3
    mov word [es:bx], strict word 00000h      ; 26 c7 07 00 00              ; 0xc0aa8 vgabios.c:360
    jmp short 00ad2h                          ; eb 23                       ; 0xc0aad vgabios.c:361
    mov di, strict word 00060h                ; bf 60 00                    ; 0xc0aaf vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0ab2
    mov es, dx                                ; 8e c2                       ; 0xc0ab5
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0ab7
    push SS                                   ; 16                          ; 0xc0aba vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0abb
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc0abc
    xor ah, ah                                ; 30 e4                       ; 0xc0abf vgabios.c:364
    mov si, ax                                ; 89 c6                       ; 0xc0ac1
    add si, ax                                ; 01 c6                       ; 0xc0ac3
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc0ac5
    mov es, dx                                ; 8e c2                       ; 0xc0ac8 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc0aca
    push SS                                   ; 16                          ; 0xc0acd vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0ace
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0acf
    pop bp                                    ; 5d                          ; 0xc0ad2 vgabios.c:366
    pop di                                    ; 5f                          ; 0xc0ad3
    pop si                                    ; 5e                          ; 0xc0ad4
    retn                                      ; c3                          ; 0xc0ad5
  ; disGetNextSymbol 0xc0ad6 LB 0x3a5c -> off=0x0 cb=000000000000005e uValue=00000000000c0ad6 'vga_find_glyph'
vga_find_glyph:                              ; 0xc0ad6 LB 0x5e
    push bp                                   ; 55                          ; 0xc0ad6 vgabios.c:369
    mov bp, sp                                ; 89 e5                       ; 0xc0ad7
    push si                                   ; 56                          ; 0xc0ad9
    push di                                   ; 57                          ; 0xc0ada
    push ax                                   ; 50                          ; 0xc0adb
    push ax                                   ; 50                          ; 0xc0adc
    push dx                                   ; 52                          ; 0xc0add
    push bx                                   ; 53                          ; 0xc0ade
    mov bl, cl                                ; 88 cb                       ; 0xc0adf
    mov word [bp-006h], strict word 00000h    ; c7 46 fa 00 00              ; 0xc0ae1 vgabios.c:371
    dec word [bp+004h]                        ; ff 4e 04                    ; 0xc0ae6 vgabios.c:373
    cmp word [bp+004h], strict byte 0ffffh    ; 83 7e 04 ff                 ; 0xc0ae9
    je short 00b28h                           ; 74 39                       ; 0xc0aed
    mov cl, byte [bp+006h]                    ; 8a 4e 06                    ; 0xc0aef vgabios.c:374
    xor ch, ch                                ; 30 ed                       ; 0xc0af2
    mov dx, ss                                ; 8c d2                       ; 0xc0af4
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc0af6
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc0af9
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc0afc
    push DS                                   ; 1e                          ; 0xc0aff
    mov ds, dx                                ; 8e da                       ; 0xc0b00
    rep cmpsb                                 ; f3 a6                       ; 0xc0b02
    pop DS                                    ; 1f                          ; 0xc0b04
    mov ax, strict word 00000h                ; b8 00 00                    ; 0xc0b05
    je short 00b0ch                           ; 74 02                       ; 0xc0b08
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc0b0a
    test ax, ax                               ; 85 c0                       ; 0xc0b0c
    jne short 00b1ch                          ; 75 0c                       ; 0xc0b0e
    mov al, bl                                ; 88 d8                       ; 0xc0b10 vgabios.c:375
    xor ah, ah                                ; 30 e4                       ; 0xc0b12
    or ah, 080h                               ; 80 cc 80                    ; 0xc0b14
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0b17
    jmp short 00b28h                          ; eb 0c                       ; 0xc0b1a vgabios.c:376
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc0b1c vgabios.c:378
    xor ah, ah                                ; 30 e4                       ; 0xc0b1f
    add word [bp-008h], ax                    ; 01 46 f8                    ; 0xc0b21
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc0b24 vgabios.c:379
    jmp short 00ae6h                          ; eb be                       ; 0xc0b26 vgabios.c:380
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc0b28 vgabios.c:382
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b2b
    pop di                                    ; 5f                          ; 0xc0b2e
    pop si                                    ; 5e                          ; 0xc0b2f
    pop bp                                    ; 5d                          ; 0xc0b30
    retn 00004h                               ; c2 04 00                    ; 0xc0b31
  ; disGetNextSymbol 0xc0b34 LB 0x39fe -> off=0x0 cb=0000000000000046 uValue=00000000000c0b34 'vga_read_glyph_planar'
vga_read_glyph_planar:                       ; 0xc0b34 LB 0x46
    push bp                                   ; 55                          ; 0xc0b34 vgabios.c:384
    mov bp, sp                                ; 89 e5                       ; 0xc0b35
    push si                                   ; 56                          ; 0xc0b37
    push di                                   ; 57                          ; 0xc0b38
    push ax                                   ; 50                          ; 0xc0b39
    push ax                                   ; 50                          ; 0xc0b3a
    mov si, ax                                ; 89 c6                       ; 0xc0b3b
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc0b3d
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc0b40
    mov bx, cx                                ; 89 cb                       ; 0xc0b43
    mov ax, 00805h                            ; b8 05 08                    ; 0xc0b45 vgabios.c:391
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b48
    out DX, ax                                ; ef                          ; 0xc0b4b
    dec byte [bp+004h]                        ; fe 4e 04                    ; 0xc0b4c vgabios.c:393
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc0b4f
    je short 00b6ah                           ; 74 15                       ; 0xc0b53
    mov es, [bp-006h]                         ; 8e 46 fa                    ; 0xc0b55 vgabios.c:394
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc0b58
    not al                                    ; f6 d0                       ; 0xc0b5b
    mov di, bx                                ; 89 df                       ; 0xc0b5d
    inc bx                                    ; 43                          ; 0xc0b5f
    push SS                                   ; 16                          ; 0xc0b60
    pop ES                                    ; 07                          ; 0xc0b61
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0b62
    add si, word [bp-008h]                    ; 03 76 f8                    ; 0xc0b65 vgabios.c:395
    jmp short 00b4ch                          ; eb e2                       ; 0xc0b68 vgabios.c:396
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0b6a vgabios.c:399
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b6d
    out DX, ax                                ; ef                          ; 0xc0b70
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b71 vgabios.c:400
    pop di                                    ; 5f                          ; 0xc0b74
    pop si                                    ; 5e                          ; 0xc0b75
    pop bp                                    ; 5d                          ; 0xc0b76
    retn 00002h                               ; c2 02 00                    ; 0xc0b77
  ; disGetNextSymbol 0xc0b7a LB 0x39b8 -> off=0x0 cb=000000000000002f uValue=00000000000c0b7a 'vga_char_ofs_planar'
vga_char_ofs_planar:                         ; 0xc0b7a LB 0x2f
    push si                                   ; 56                          ; 0xc0b7a vgabios.c:402
    push bp                                   ; 55                          ; 0xc0b7b
    mov bp, sp                                ; 89 e5                       ; 0xc0b7c
    mov ch, al                                ; 88 c5                       ; 0xc0b7e
    mov al, dl                                ; 88 d0                       ; 0xc0b80
    xor ah, ah                                ; 30 e4                       ; 0xc0b82 vgabios.c:406
    mul bx                                    ; f7 e3                       ; 0xc0b84
    mov bl, byte [bp+006h]                    ; 8a 5e 06                    ; 0xc0b86
    xor bh, bh                                ; 30 ff                       ; 0xc0b89
    mul bx                                    ; f7 e3                       ; 0xc0b8b
    mov bl, ch                                ; 88 eb                       ; 0xc0b8d
    add bx, ax                                ; 01 c3                       ; 0xc0b8f
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc0b91 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0b94
    mov es, ax                                ; 8e c0                       ; 0xc0b97
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc0b99
    mov al, cl                                ; 88 c8                       ; 0xc0b9c vgabios.c:58
    xor ah, ah                                ; 30 e4                       ; 0xc0b9e
    mul si                                    ; f7 e6                       ; 0xc0ba0
    add ax, bx                                ; 01 d8                       ; 0xc0ba2
    pop bp                                    ; 5d                          ; 0xc0ba4 vgabios.c:410
    pop si                                    ; 5e                          ; 0xc0ba5
    retn 00002h                               ; c2 02 00                    ; 0xc0ba6
  ; disGetNextSymbol 0xc0ba9 LB 0x3989 -> off=0x0 cb=0000000000000040 uValue=00000000000c0ba9 'vga_read_char_planar'
vga_read_char_planar:                        ; 0xc0ba9 LB 0x40
    push bp                                   ; 55                          ; 0xc0ba9 vgabios.c:412
    mov bp, sp                                ; 89 e5                       ; 0xc0baa
    push cx                                   ; 51                          ; 0xc0bac
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0bad
    mov byte [bp-004h], bl                    ; 88 5e fc                    ; 0xc0bb0 vgabios.c:416
    mov byte [bp-003h], 000h                  ; c6 46 fd 00                 ; 0xc0bb3
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0bb7
    lea cx, [bp-014h]                         ; 8d 4e ec                    ; 0xc0bba
    mov bx, ax                                ; 89 c3                       ; 0xc0bbd
    mov ax, dx                                ; 89 d0                       ; 0xc0bbf
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0bc1
    call 00b34h                               ; e8 6d ff                    ; 0xc0bc4
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0bc7 vgabios.c:419
    push 00100h                               ; 68 00 01                    ; 0xc0bca
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0bcd vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0bd0
    mov es, ax                                ; 8e c0                       ; 0xc0bd2
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0bd4
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0bd7
    xor cx, cx                                ; 31 c9                       ; 0xc0bdb vgabios.c:68
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc0bdd
    call 00ad6h                               ; e8 f3 fe                    ; 0xc0be0
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc0be3 vgabios.c:420
    pop cx                                    ; 59                          ; 0xc0be6
    pop bp                                    ; 5d                          ; 0xc0be7
    retn                                      ; c3                          ; 0xc0be8
  ; disGetNextSymbol 0xc0be9 LB 0x3949 -> off=0x0 cb=0000000000000024 uValue=00000000000c0be9 'vga_char_ofs_linear'
vga_char_ofs_linear:                         ; 0xc0be9 LB 0x24
    enter 00002h, 000h                        ; c8 02 00 00                 ; 0xc0be9 vgabios.c:422
    mov byte [bp-002h], al                    ; 88 46 fe                    ; 0xc0bed
    mov al, dl                                ; 88 d0                       ; 0xc0bf0 vgabios.c:426
    xor ah, ah                                ; 30 e4                       ; 0xc0bf2
    mul bx                                    ; f7 e3                       ; 0xc0bf4
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc0bf6
    xor dh, dh                                ; 30 f6                       ; 0xc0bf9
    mul dx                                    ; f7 e2                       ; 0xc0bfb
    mov dx, ax                                ; 89 c2                       ; 0xc0bfd
    mov al, byte [bp-002h]                    ; 8a 46 fe                    ; 0xc0bff
    xor ah, ah                                ; 30 e4                       ; 0xc0c02
    add ax, dx                                ; 01 d0                       ; 0xc0c04
    sal ax, 003h                              ; c1 e0 03                    ; 0xc0c06 vgabios.c:427
    leave                                     ; c9                          ; 0xc0c09 vgabios.c:429
    retn 00002h                               ; c2 02 00                    ; 0xc0c0a
  ; disGetNextSymbol 0xc0c0d LB 0x3925 -> off=0x0 cb=000000000000004b uValue=00000000000c0c0d 'vga_read_glyph_linear'
vga_read_glyph_linear:                       ; 0xc0c0d LB 0x4b
    push si                                   ; 56                          ; 0xc0c0d vgabios.c:431
    push di                                   ; 57                          ; 0xc0c0e
    enter 00004h, 000h                        ; c8 04 00 00                 ; 0xc0c0f
    mov si, ax                                ; 89 c6                       ; 0xc0c13
    mov word [bp-002h], dx                    ; 89 56 fe                    ; 0xc0c15
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc0c18
    mov bx, cx                                ; 89 cb                       ; 0xc0c1b
    dec byte [bp+008h]                        ; fe 4e 08                    ; 0xc0c1d vgabios.c:437
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc0c20
    je short 00c52h                           ; 74 2c                       ; 0xc0c24
    xor dh, dh                                ; 30 f6                       ; 0xc0c26 vgabios.c:438
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc0c28 vgabios.c:439
    xor ax, ax                                ; 31 c0                       ; 0xc0c2a vgabios.c:440
    jmp short 00c33h                          ; eb 05                       ; 0xc0c2c
    cmp ax, strict word 00008h                ; 3d 08 00                    ; 0xc0c2e
    jnl short 00c47h                          ; 7d 14                       ; 0xc0c31
    mov es, [bp-002h]                         ; 8e 46 fe                    ; 0xc0c33 vgabios.c:441
    mov di, si                                ; 89 f7                       ; 0xc0c36
    add di, ax                                ; 01 c7                       ; 0xc0c38
    cmp byte [es:di], 000h                    ; 26 80 3d 00                 ; 0xc0c3a
    je short 00c42h                           ; 74 02                       ; 0xc0c3e
    or dh, dl                                 ; 08 d6                       ; 0xc0c40 vgabios.c:442
    shr dl, 1                                 ; d0 ea                       ; 0xc0c42 vgabios.c:443
    inc ax                                    ; 40                          ; 0xc0c44 vgabios.c:444
    jmp short 00c2eh                          ; eb e7                       ; 0xc0c45
    mov di, bx                                ; 89 df                       ; 0xc0c47 vgabios.c:445
    inc bx                                    ; 43                          ; 0xc0c49
    mov byte [ss:di], dh                      ; 36 88 35                    ; 0xc0c4a
    add si, word [bp-004h]                    ; 03 76 fc                    ; 0xc0c4d vgabios.c:446
    jmp short 00c1dh                          ; eb cb                       ; 0xc0c50 vgabios.c:447
    leave                                     ; c9                          ; 0xc0c52 vgabios.c:448
    pop di                                    ; 5f                          ; 0xc0c53
    pop si                                    ; 5e                          ; 0xc0c54
    retn 00002h                               ; c2 02 00                    ; 0xc0c55
  ; disGetNextSymbol 0xc0c58 LB 0x38da -> off=0x0 cb=0000000000000045 uValue=00000000000c0c58 'vga_read_char_linear'
vga_read_char_linear:                        ; 0xc0c58 LB 0x45
    push bp                                   ; 55                          ; 0xc0c58 vgabios.c:450
    mov bp, sp                                ; 89 e5                       ; 0xc0c59
    push cx                                   ; 51                          ; 0xc0c5b
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0c5c
    mov cx, ax                                ; 89 c1                       ; 0xc0c5f
    mov ax, dx                                ; 89 d0                       ; 0xc0c61
    mov byte [bp-004h], bl                    ; 88 5e fc                    ; 0xc0c63 vgabios.c:454
    mov byte [bp-003h], 000h                  ; c6 46 fd 00                 ; 0xc0c66
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0c6a
    mov bx, cx                                ; 89 cb                       ; 0xc0c6d
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0c6f
    lea cx, [bp-014h]                         ; 8d 4e ec                    ; 0xc0c72
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0c75
    call 00c0dh                               ; e8 92 ff                    ; 0xc0c78
    push word [bp-004h]                       ; ff 76 fc                    ; 0xc0c7b vgabios.c:457
    push 00100h                               ; 68 00 01                    ; 0xc0c7e
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0c81 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0c84
    mov es, ax                                ; 8e c0                       ; 0xc0c86
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c88
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0c8b
    xor cx, cx                                ; 31 c9                       ; 0xc0c8f vgabios.c:68
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc0c91
    call 00ad6h                               ; e8 3f fe                    ; 0xc0c94
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc0c97 vgabios.c:458
    pop cx                                    ; 59                          ; 0xc0c9a
    pop bp                                    ; 5d                          ; 0xc0c9b
    retn                                      ; c3                          ; 0xc0c9c
  ; disGetNextSymbol 0xc0c9d LB 0x3895 -> off=0x0 cb=0000000000000035 uValue=00000000000c0c9d 'vga_read_2bpp_char'
vga_read_2bpp_char:                          ; 0xc0c9d LB 0x35
    push bp                                   ; 55                          ; 0xc0c9d vgabios.c:460
    mov bp, sp                                ; 89 e5                       ; 0xc0c9e
    push bx                                   ; 53                          ; 0xc0ca0
    push cx                                   ; 51                          ; 0xc0ca1
    mov bx, ax                                ; 89 c3                       ; 0xc0ca2
    mov es, dx                                ; 8e c2                       ; 0xc0ca4
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0ca6 vgabios.c:466
    mov DH, strict byte 080h                  ; b6 80                       ; 0xc0ca9 vgabios.c:467
    xor dl, dl                                ; 30 d2                       ; 0xc0cab vgabios.c:468
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0cad vgabios.c:469
    xchg ah, al                               ; 86 c4                       ; 0xc0cb0
    xor bx, bx                                ; 31 db                       ; 0xc0cb2 vgabios.c:471
    jmp short 00cbbh                          ; eb 05                       ; 0xc0cb4
    cmp bx, strict byte 00008h                ; 83 fb 08                    ; 0xc0cb6
    jnl short 00cc9h                          ; 7d 0e                       ; 0xc0cb9
    test ax, cx                               ; 85 c8                       ; 0xc0cbb vgabios.c:472
    je short 00cc1h                           ; 74 02                       ; 0xc0cbd
    or dl, dh                                 ; 08 f2                       ; 0xc0cbf vgabios.c:473
    shr dh, 1                                 ; d0 ee                       ; 0xc0cc1 vgabios.c:474
    shr cx, 002h                              ; c1 e9 02                    ; 0xc0cc3 vgabios.c:475
    inc bx                                    ; 43                          ; 0xc0cc6 vgabios.c:476
    jmp short 00cb6h                          ; eb ed                       ; 0xc0cc7
    mov al, dl                                ; 88 d0                       ; 0xc0cc9 vgabios.c:478
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0ccb
    pop cx                                    ; 59                          ; 0xc0cce
    pop bx                                    ; 5b                          ; 0xc0ccf
    pop bp                                    ; 5d                          ; 0xc0cd0
    retn                                      ; c3                          ; 0xc0cd1
  ; disGetNextSymbol 0xc0cd2 LB 0x3860 -> off=0x0 cb=0000000000000084 uValue=00000000000c0cd2 'vga_read_glyph_cga'
vga_read_glyph_cga:                          ; 0xc0cd2 LB 0x84
    push bp                                   ; 55                          ; 0xc0cd2 vgabios.c:480
    mov bp, sp                                ; 89 e5                       ; 0xc0cd3
    push cx                                   ; 51                          ; 0xc0cd5
    push si                                   ; 56                          ; 0xc0cd6
    push di                                   ; 57                          ; 0xc0cd7
    push ax                                   ; 50                          ; 0xc0cd8
    mov si, dx                                ; 89 d6                       ; 0xc0cd9
    cmp bl, 006h                              ; 80 fb 06                    ; 0xc0cdb vgabios.c:488
    je short 00d1ah                           ; 74 3a                       ; 0xc0cde
    mov bx, ax                                ; 89 c3                       ; 0xc0ce0 vgabios.c:490
    add bx, ax                                ; 01 c3                       ; 0xc0ce2
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0ce4
    xor cx, cx                                ; 31 c9                       ; 0xc0ce9 vgabios.c:492
    jmp short 00cf2h                          ; eb 05                       ; 0xc0ceb
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0ced
    jnl short 00d4eh                          ; 7d 5c                       ; 0xc0cf0
    mov ax, bx                                ; 89 d8                       ; 0xc0cf2 vgabios.c:493
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0cf4
    call 00c9dh                               ; e8 a3 ff                    ; 0xc0cf7
    mov di, si                                ; 89 f7                       ; 0xc0cfa
    inc si                                    ; 46                          ; 0xc0cfc
    push SS                                   ; 16                          ; 0xc0cfd
    pop ES                                    ; 07                          ; 0xc0cfe
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0cff
    lea ax, [bx+02000h]                       ; 8d 87 00 20                 ; 0xc0d02 vgabios.c:494
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0d06
    call 00c9dh                               ; e8 91 ff                    ; 0xc0d09
    mov di, si                                ; 89 f7                       ; 0xc0d0c
    inc si                                    ; 46                          ; 0xc0d0e
    push SS                                   ; 16                          ; 0xc0d0f
    pop ES                                    ; 07                          ; 0xc0d10
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d11
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d14 vgabios.c:495
    inc cx                                    ; 41                          ; 0xc0d17 vgabios.c:496
    jmp short 00cedh                          ; eb d3                       ; 0xc0d18
    mov bx, ax                                ; 89 c3                       ; 0xc0d1a vgabios.c:498
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0d1c
    xor cx, cx                                ; 31 c9                       ; 0xc0d21 vgabios.c:499
    jmp short 00d2ah                          ; eb 05                       ; 0xc0d23
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0d25
    jnl short 00d4eh                          ; 7d 24                       ; 0xc0d28
    mov di, si                                ; 89 f7                       ; 0xc0d2a vgabios.c:500
    inc si                                    ; 46                          ; 0xc0d2c
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d2d
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0d30
    push SS                                   ; 16                          ; 0xc0d33
    pop ES                                    ; 07                          ; 0xc0d34
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d35
    mov di, si                                ; 89 f7                       ; 0xc0d38 vgabios.c:501
    inc si                                    ; 46                          ; 0xc0d3a
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d3b
    mov al, byte [es:bx+02000h]               ; 26 8a 87 00 20              ; 0xc0d3e
    push SS                                   ; 16                          ; 0xc0d43
    pop ES                                    ; 07                          ; 0xc0d44
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d45
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d48 vgabios.c:502
    inc cx                                    ; 41                          ; 0xc0d4b vgabios.c:503
    jmp short 00d25h                          ; eb d7                       ; 0xc0d4c
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc0d4e vgabios.c:505
    pop di                                    ; 5f                          ; 0xc0d51
    pop si                                    ; 5e                          ; 0xc0d52
    pop cx                                    ; 59                          ; 0xc0d53
    pop bp                                    ; 5d                          ; 0xc0d54
    retn                                      ; c3                          ; 0xc0d55
  ; disGetNextSymbol 0xc0d56 LB 0x37dc -> off=0x0 cb=000000000000001a uValue=00000000000c0d56 'vga_char_ofs_cga'
vga_char_ofs_cga:                            ; 0xc0d56 LB 0x1a
    push cx                                   ; 51                          ; 0xc0d56 vgabios.c:507
    push bp                                   ; 55                          ; 0xc0d57
    mov bp, sp                                ; 89 e5                       ; 0xc0d58
    mov cl, al                                ; 88 c1                       ; 0xc0d5a
    mov al, dl                                ; 88 d0                       ; 0xc0d5c
    xor ah, ah                                ; 30 e4                       ; 0xc0d5e vgabios.c:512
    mul bx                                    ; f7 e3                       ; 0xc0d60
    mov bx, ax                                ; 89 c3                       ; 0xc0d62
    sal bx, 002h                              ; c1 e3 02                    ; 0xc0d64
    mov al, cl                                ; 88 c8                       ; 0xc0d67
    xor ah, ah                                ; 30 e4                       ; 0xc0d69
    add ax, bx                                ; 01 d8                       ; 0xc0d6b
    pop bp                                    ; 5d                          ; 0xc0d6d vgabios.c:513
    pop cx                                    ; 59                          ; 0xc0d6e
    retn                                      ; c3                          ; 0xc0d6f
  ; disGetNextSymbol 0xc0d70 LB 0x37c2 -> off=0x0 cb=0000000000000066 uValue=00000000000c0d70 'vga_read_char_cga'
vga_read_char_cga:                           ; 0xc0d70 LB 0x66
    push bp                                   ; 55                          ; 0xc0d70 vgabios.c:515
    mov bp, sp                                ; 89 e5                       ; 0xc0d71
    push bx                                   ; 53                          ; 0xc0d73
    push cx                                   ; 51                          ; 0xc0d74
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc0d75
    mov bl, dl                                ; 88 d3                       ; 0xc0d78 vgabios.c:521
    xor bh, bh                                ; 30 ff                       ; 0xc0d7a
    lea dx, [bp-00eh]                         ; 8d 56 f2                    ; 0xc0d7c
    call 00cd2h                               ; e8 50 ff                    ; 0xc0d7f
    push strict byte 00008h                   ; 6a 08                       ; 0xc0d82 vgabios.c:524
    push 00080h                               ; 68 80 00                    ; 0xc0d84
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0d87 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d8a
    mov es, ax                                ; 8e c0                       ; 0xc0d8c
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0d8e
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0d91
    xor cx, cx                                ; 31 c9                       ; 0xc0d95 vgabios.c:68
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0d97
    call 00ad6h                               ; e8 39 fd                    ; 0xc0d9a
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0d9d
    test ah, 080h                             ; f6 c4 80                    ; 0xc0da0 vgabios.c:526
    jne short 00dcch                          ; 75 27                       ; 0xc0da3
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0da5 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0da8
    mov es, ax                                ; 8e c0                       ; 0xc0daa
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0dac
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0daf
    test dx, dx                               ; 85 d2                       ; 0xc0db3 vgabios.c:530
    jne short 00dbbh                          ; 75 04                       ; 0xc0db5
    test ax, ax                               ; 85 c0                       ; 0xc0db7
    je short 00dcch                           ; 74 11                       ; 0xc0db9
    push strict byte 00008h                   ; 6a 08                       ; 0xc0dbb vgabios.c:531
    push 00080h                               ; 68 80 00                    ; 0xc0dbd
    mov cx, 00080h                            ; b9 80 00                    ; 0xc0dc0
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0dc3
    call 00ad6h                               ; e8 0d fd                    ; 0xc0dc6
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0dc9
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc0dcc vgabios.c:534
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0dcf
    pop cx                                    ; 59                          ; 0xc0dd2
    pop bx                                    ; 5b                          ; 0xc0dd3
    pop bp                                    ; 5d                          ; 0xc0dd4
    retn                                      ; c3                          ; 0xc0dd5
  ; disGetNextSymbol 0xc0dd6 LB 0x375c -> off=0x0 cb=0000000000000130 uValue=00000000000c0dd6 'vga_read_char_attr'
vga_read_char_attr:                          ; 0xc0dd6 LB 0x130
    push bp                                   ; 55                          ; 0xc0dd6 vgabios.c:536
    mov bp, sp                                ; 89 e5                       ; 0xc0dd7
    push bx                                   ; 53                          ; 0xc0dd9
    push cx                                   ; 51                          ; 0xc0dda
    push si                                   ; 56                          ; 0xc0ddb
    push di                                   ; 57                          ; 0xc0ddc
    sub sp, strict byte 00014h                ; 83 ec 14                    ; 0xc0ddd
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc0de0
    mov si, dx                                ; 89 d6                       ; 0xc0de3
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0de5 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0de8
    mov es, ax                                ; 8e c0                       ; 0xc0deb
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0ded
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc0df0 vgabios.c:48
    xor ah, ah                                ; 30 e4                       ; 0xc0df3 vgabios.c:544
    call 03819h                               ; e8 21 2a                    ; 0xc0df5
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc0df8
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0dfb vgabios.c:545
    jne short 00e02h                          ; 75 03                       ; 0xc0dfd
    jmp near 00efdh                           ; e9 fb 00                    ; 0xc0dff
    mov cl, byte [bp-00eh]                    ; 8a 4e f2                    ; 0xc0e02 vgabios.c:549
    xor ch, ch                                ; 30 ed                       ; 0xc0e05
    lea bx, [bp-01ch]                         ; 8d 5e e4                    ; 0xc0e07
    lea dx, [bp-01ah]                         ; 8d 56 e6                    ; 0xc0e0a
    mov ax, cx                                ; 89 c8                       ; 0xc0e0d
    call 00a96h                               ; e8 84 fc                    ; 0xc0e0f
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc0e12 vgabios.c:550
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0e15
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc0e18 vgabios.c:551
    xor al, al                                ; 30 c0                       ; 0xc0e1b
    shr ax, 008h                              ; c1 e8 08                    ; 0xc0e1d
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc0e20
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc0e23
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0e26 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0e29
    mov es, ax                                ; 8e c0                       ; 0xc0e2c
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0e2e
    xor ah, ah                                ; 30 e4                       ; 0xc0e31 vgabios.c:48
    inc ax                                    ; 40                          ; 0xc0e33
    mov word [bp-014h], ax                    ; 89 46 ec                    ; 0xc0e34
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc0e37 vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0e3a
    mov word [bp-018h], di                    ; 89 7e e8                    ; 0xc0e3d vgabios.c:58
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc0e40 vgabios.c:557
    xor bh, bh                                ; 30 ff                       ; 0xc0e43
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0e45
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc0e48
    jne short 00e7fh                          ; 75 30                       ; 0xc0e4d
    mov ax, di                                ; 89 f8                       ; 0xc0e4f vgabios.c:559
    mul word [bp-014h]                        ; f7 66 ec                    ; 0xc0e51
    add ax, ax                                ; 01 c0                       ; 0xc0e54
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc0e56
    inc ax                                    ; 40                          ; 0xc0e58
    mul cx                                    ; f7 e1                       ; 0xc0e59
    mov cx, ax                                ; 89 c1                       ; 0xc0e5b
    mov al, byte [bp-016h]                    ; 8a 46 ea                    ; 0xc0e5d
    xor ah, ah                                ; 30 e4                       ; 0xc0e60
    mul di                                    ; f7 e7                       ; 0xc0e62
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc0e64
    xor dh, dh                                ; 30 f6                       ; 0xc0e67
    mov di, ax                                ; 89 c7                       ; 0xc0e69
    add di, dx                                ; 01 d7                       ; 0xc0e6b
    add di, di                                ; 01 ff                       ; 0xc0e6d
    add di, cx                                ; 01 cf                       ; 0xc0e6f
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc0e71 vgabios.c:55
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc0e75
    push SS                                   ; 16                          ; 0xc0e78 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0e79
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0e7a
    jmp short 00dffh                          ; eb 80                       ; 0xc0e7d vgabios.c:561
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc0e7f vgabios.c:562
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc0e83
    je short 00ed6h                           ; 74 4e                       ; 0xc0e86
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc0e88
    jc short 00efdh                           ; 72 70                       ; 0xc0e8b
    jbe short 00e96h                          ; 76 07                       ; 0xc0e8d
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc0e8f
    jbe short 00eafh                          ; 76 1b                       ; 0xc0e92
    jmp short 00efdh                          ; eb 67                       ; 0xc0e94
    xor dh, dh                                ; 30 f6                       ; 0xc0e96 vgabios.c:565
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc0e98
    xor ah, ah                                ; 30 e4                       ; 0xc0e9b
    mov bx, word [bp-018h]                    ; 8b 5e e8                    ; 0xc0e9d
    call 00d56h                               ; e8 b3 fe                    ; 0xc0ea0
    mov dl, byte [bp-010h]                    ; 8a 56 f0                    ; 0xc0ea3 vgabios.c:566
    xor dh, dh                                ; 30 f6                       ; 0xc0ea6
    call 00d70h                               ; e8 c5 fe                    ; 0xc0ea8
    xor ah, ah                                ; 30 e4                       ; 0xc0eab
    jmp short 00e78h                          ; eb c9                       ; 0xc0ead
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0eaf vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0eb2
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc0eb5 vgabios.c:571
    mov byte [bp-011h], ch                    ; 88 6e ef                    ; 0xc0eb8
    push word [bp-012h]                       ; ff 76 ee                    ; 0xc0ebb
    xor dh, dh                                ; 30 f6                       ; 0xc0ebe
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc0ec0
    xor ah, ah                                ; 30 e4                       ; 0xc0ec3
    mov bx, di                                ; 89 fb                       ; 0xc0ec5
    call 00b7ah                               ; e8 b0 fc                    ; 0xc0ec7
    mov bx, word [bp-012h]                    ; 8b 5e ee                    ; 0xc0eca vgabios.c:572
    mov dx, ax                                ; 89 c2                       ; 0xc0ecd
    mov ax, di                                ; 89 f8                       ; 0xc0ecf
    call 00ba9h                               ; e8 d5 fc                    ; 0xc0ed1
    jmp short 00eabh                          ; eb d5                       ; 0xc0ed4
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0ed6 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0ed9
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc0edc vgabios.c:576
    mov byte [bp-011h], ch                    ; 88 6e ef                    ; 0xc0edf
    push word [bp-012h]                       ; ff 76 ee                    ; 0xc0ee2
    xor dh, dh                                ; 30 f6                       ; 0xc0ee5
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc0ee7
    xor ah, ah                                ; 30 e4                       ; 0xc0eea
    mov bx, di                                ; 89 fb                       ; 0xc0eec
    call 00be9h                               ; e8 f8 fc                    ; 0xc0eee
    mov bx, word [bp-012h]                    ; 8b 5e ee                    ; 0xc0ef1 vgabios.c:577
    mov dx, ax                                ; 89 c2                       ; 0xc0ef4
    mov ax, di                                ; 89 f8                       ; 0xc0ef6
    call 00c58h                               ; e8 5d fd                    ; 0xc0ef8
    jmp short 00eabh                          ; eb ae                       ; 0xc0efb
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc0efd vgabios.c:586
    pop di                                    ; 5f                          ; 0xc0f00
    pop si                                    ; 5e                          ; 0xc0f01
    pop cx                                    ; 59                          ; 0xc0f02
    pop bx                                    ; 5b                          ; 0xc0f03
    pop bp                                    ; 5d                          ; 0xc0f04
    retn                                      ; c3                          ; 0xc0f05
  ; disGetNextSymbol 0xc0f06 LB 0x362c -> off=0x10 cb=0000000000000083 uValue=00000000000c0f16 'vga_get_font_info'
    db  02dh, 00fh, 072h, 00fh, 077h, 00fh, 07eh, 00fh, 083h, 00fh, 088h, 00fh, 08dh, 00fh, 092h, 00fh
vga_get_font_info:                           ; 0xc0f16 LB 0x83
    push si                                   ; 56                          ; 0xc0f16 vgabios.c:588
    push di                                   ; 57                          ; 0xc0f17
    push bp                                   ; 55                          ; 0xc0f18
    mov bp, sp                                ; 89 e5                       ; 0xc0f19
    mov si, dx                                ; 89 d6                       ; 0xc0f1b
    mov di, bx                                ; 89 df                       ; 0xc0f1d
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc0f1f vgabios.c:593
    jnbe short 00f6ch                         ; 77 48                       ; 0xc0f22
    mov bx, ax                                ; 89 c3                       ; 0xc0f24
    add bx, ax                                ; 01 c3                       ; 0xc0f26
    jmp word [cs:bx+00f06h]                   ; 2e ff a7 06 0f              ; 0xc0f28
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0f2d vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0f30
    mov es, ax                                ; 8e c0                       ; 0xc0f32
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0f34
    mov ax, word [es:bx+002h]                 ; 26 8b 47 02                 ; 0xc0f37
    push SS                                   ; 16                          ; 0xc0f3b vgabios.c:596
    pop ES                                    ; 07                          ; 0xc0f3c
    mov word [es:di], dx                      ; 26 89 15                    ; 0xc0f3d
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0f40
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0f43
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f46
    mov es, ax                                ; 8e c0                       ; 0xc0f49
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f4b
    xor ah, ah                                ; 30 e4                       ; 0xc0f4e
    push SS                                   ; 16                          ; 0xc0f50
    pop ES                                    ; 07                          ; 0xc0f51
    mov bx, cx                                ; 89 cb                       ; 0xc0f52
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f54
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0f57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f5a
    mov es, ax                                ; 8e c0                       ; 0xc0f5d
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f5f
    xor ah, ah                                ; 30 e4                       ; 0xc0f62
    push SS                                   ; 16                          ; 0xc0f64
    pop ES                                    ; 07                          ; 0xc0f65
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc0f66
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f69
    pop bp                                    ; 5d                          ; 0xc0f6c
    pop di                                    ; 5f                          ; 0xc0f6d
    pop si                                    ; 5e                          ; 0xc0f6e
    retn 00002h                               ; c2 02 00                    ; 0xc0f6f
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0f72 vgabios.c:67
    jmp short 00f30h                          ; eb b9                       ; 0xc0f75
    mov dx, 05d6fh                            ; ba 6f 5d                    ; 0xc0f77 vgabios.c:601
    mov ax, ds                                ; 8c d8                       ; 0xc0f7a
    jmp short 00f3bh                          ; eb bd                       ; 0xc0f7c vgabios.c:602
    mov dx, 0556fh                            ; ba 6f 55                    ; 0xc0f7e vgabios.c:604
    jmp short 00f7ah                          ; eb f7                       ; 0xc0f81
    mov dx, 0596fh                            ; ba 6f 59                    ; 0xc0f83 vgabios.c:607
    jmp short 00f7ah                          ; eb f2                       ; 0xc0f86
    mov dx, 07b6fh                            ; ba 6f 7b                    ; 0xc0f88 vgabios.c:610
    jmp short 00f7ah                          ; eb ed                       ; 0xc0f8b
    mov dx, 06b6fh                            ; ba 6f 6b                    ; 0xc0f8d vgabios.c:613
    jmp short 00f7ah                          ; eb e8                       ; 0xc0f90
    mov dx, 07c9ch                            ; ba 9c 7c                    ; 0xc0f92 vgabios.c:616
    jmp short 00f7ah                          ; eb e3                       ; 0xc0f95
    jmp short 00f6ch                          ; eb d3                       ; 0xc0f97 vgabios.c:622
  ; disGetNextSymbol 0xc0f99 LB 0x3599 -> off=0x0 cb=0000000000000166 uValue=00000000000c0f99 'vga_read_pixel'
vga_read_pixel:                              ; 0xc0f99 LB 0x166
    push bp                                   ; 55                          ; 0xc0f99 vgabios.c:635
    mov bp, sp                                ; 89 e5                       ; 0xc0f9a
    push si                                   ; 56                          ; 0xc0f9c
    push di                                   ; 57                          ; 0xc0f9d
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc0f9e
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0fa1
    mov si, dx                                ; 89 d6                       ; 0xc0fa4
    mov dx, bx                                ; 89 da                       ; 0xc0fa6
    mov word [bp-00ch], cx                    ; 89 4e f4                    ; 0xc0fa8
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0fab vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0fae
    mov es, ax                                ; 8e c0                       ; 0xc0fb1
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0fb3
    xor ah, ah                                ; 30 e4                       ; 0xc0fb6 vgabios.c:642
    call 03819h                               ; e8 5e 28                    ; 0xc0fb8
    mov ah, al                                ; 88 c4                       ; 0xc0fbb
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0fbd vgabios.c:643
    je short 00fcfh                           ; 74 0e                       ; 0xc0fbf
    mov bl, al                                ; 88 c3                       ; 0xc0fc1 vgabios.c:645
    xor bh, bh                                ; 30 ff                       ; 0xc0fc3
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0fc5
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc0fc8
    jne short 00fd2h                          ; 75 03                       ; 0xc0fcd
    jmp near 010f8h                           ; e9 26 01                    ; 0xc0fcf vgabios.c:646
    mov ch, byte [bx+047b3h]                  ; 8a af b3 47                 ; 0xc0fd2 vgabios.c:649
    cmp ch, 003h                              ; 80 fd 03                    ; 0xc0fd6
    jc short 00feah                           ; 72 0f                       ; 0xc0fd9
    jbe short 00ff2h                          ; 76 15                       ; 0xc0fdb
    cmp ch, 005h                              ; 80 fd 05                    ; 0xc0fdd
    je short 01029h                           ; 74 47                       ; 0xc0fe0
    cmp ch, 004h                              ; 80 fd 04                    ; 0xc0fe2
    je short 00ff2h                           ; 74 0b                       ; 0xc0fe5
    jmp near 010eeh                           ; e9 04 01                    ; 0xc0fe7
    cmp ch, 002h                              ; 80 fd 02                    ; 0xc0fea
    je short 01060h                           ; 74 71                       ; 0xc0fed
    jmp near 010eeh                           ; e9 fc 00                    ; 0xc0fef
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc0ff2 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0ff5
    mov es, ax                                ; 8e c0                       ; 0xc0ff8
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc0ffa
    mov ax, dx                                ; 89 d0                       ; 0xc0ffd vgabios.c:58
    mul bx                                    ; f7 e3                       ; 0xc0fff
    mov bx, si                                ; 89 f3                       ; 0xc1001
    shr bx, 003h                              ; c1 eb 03                    ; 0xc1003
    add bx, ax                                ; 01 c3                       ; 0xc1006
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc1008 vgabios.c:57
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc100b
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc100e vgabios.c:58
    xor dh, dh                                ; 30 f6                       ; 0xc1011
    mul dx                                    ; f7 e2                       ; 0xc1013
    add bx, ax                                ; 01 c3                       ; 0xc1015
    mov cx, si                                ; 89 f1                       ; 0xc1017 vgabios.c:654
    and cx, strict byte 00007h                ; 83 e1 07                    ; 0xc1019
    mov ax, 00080h                            ; b8 80 00                    ; 0xc101c
    sar ax, CL                                ; d3 f8                       ; 0xc101f
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc1021
    mov byte [bp-006h], ch                    ; 88 6e fa                    ; 0xc1024 vgabios.c:656
    jmp short 01032h                          ; eb 09                       ; 0xc1027
    jmp near 010ceh                           ; e9 a2 00                    ; 0xc1029
    cmp byte [bp-006h], 004h                  ; 80 7e fa 04                 ; 0xc102c
    jnc short 0105dh                          ; 73 2b                       ; 0xc1030
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1032 vgabios.c:657
    xor ah, ah                                ; 30 e4                       ; 0xc1035
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1037
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc103a
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc103c
    out DX, ax                                ; ef                          ; 0xc103f
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc1040 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc1043
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1045
    and al, byte [bp-008h]                    ; 22 46 f8                    ; 0xc1048 vgabios.c:48
    test al, al                               ; 84 c0                       ; 0xc104b vgabios.c:659
    jbe short 01058h                          ; 76 09                       ; 0xc104d
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc104f vgabios.c:660
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc1052
    sal al, CL                                ; d2 e0                       ; 0xc1054
    or ch, al                                 ; 08 c5                       ; 0xc1056
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1058 vgabios.c:661
    jmp short 0102ch                          ; eb cf                       ; 0xc105b
    jmp near 010f0h                           ; e9 90 00                    ; 0xc105d
    mov cl, byte [bx+047b4h]                  ; 8a 8f b4 47                 ; 0xc1060 vgabios.c:664
    xor ch, ch                                ; 30 ed                       ; 0xc1064
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc1066
    sub bx, cx                                ; 29 cb                       ; 0xc1069
    mov cx, bx                                ; 89 d9                       ; 0xc106b
    mov bx, si                                ; 89 f3                       ; 0xc106d
    shr bx, CL                                ; d3 eb                       ; 0xc106f
    mov cx, bx                                ; 89 d9                       ; 0xc1071
    mov bx, dx                                ; 89 d3                       ; 0xc1073
    shr bx, 1                                 ; d1 eb                       ; 0xc1075
    imul bx, bx, strict byte 00050h           ; 6b db 50                    ; 0xc1077
    add bx, cx                                ; 01 cb                       ; 0xc107a
    test dl, 001h                             ; f6 c2 01                    ; 0xc107c vgabios.c:665
    je short 01084h                           ; 74 03                       ; 0xc107f
    add bh, 020h                              ; 80 c7 20                    ; 0xc1081 vgabios.c:666
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1084 vgabios.c:47
    mov es, dx                                ; 8e c2                       ; 0xc1087
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1089
    mov bl, ah                                ; 88 e3                       ; 0xc108c vgabios.c:668
    xor bh, bh                                ; 30 ff                       ; 0xc108e
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1090
    cmp byte [bx+047b4h], 002h                ; 80 bf b4 47 02              ; 0xc1093
    jne short 010b5h                          ; 75 1b                       ; 0xc1098
    mov cx, si                                ; 89 f1                       ; 0xc109a vgabios.c:669
    xor ch, ch                                ; 30 ed                       ; 0xc109c
    and cl, 003h                              ; 80 e1 03                    ; 0xc109e
    mov dx, strict word 00003h                ; ba 03 00                    ; 0xc10a1
    sub dx, cx                                ; 29 ca                       ; 0xc10a4
    mov cx, dx                                ; 89 d1                       ; 0xc10a6
    add cx, dx                                ; 01 d1                       ; 0xc10a8
    xor ah, ah                                ; 30 e4                       ; 0xc10aa
    sar ax, CL                                ; d3 f8                       ; 0xc10ac
    mov ch, al                                ; 88 c5                       ; 0xc10ae
    and ch, 003h                              ; 80 e5 03                    ; 0xc10b0
    jmp short 010f0h                          ; eb 3b                       ; 0xc10b3 vgabios.c:670
    mov cx, si                                ; 89 f1                       ; 0xc10b5 vgabios.c:671
    xor ch, ch                                ; 30 ed                       ; 0xc10b7
    and cl, 007h                              ; 80 e1 07                    ; 0xc10b9
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc10bc
    sub dx, cx                                ; 29 ca                       ; 0xc10bf
    mov cx, dx                                ; 89 d1                       ; 0xc10c1
    xor ah, ah                                ; 30 e4                       ; 0xc10c3
    sar ax, CL                                ; d3 f8                       ; 0xc10c5
    mov ch, al                                ; 88 c5                       ; 0xc10c7
    and ch, 001h                              ; 80 e5 01                    ; 0xc10c9
    jmp short 010f0h                          ; eb 22                       ; 0xc10cc vgabios.c:672
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc10ce vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc10d1
    mov es, ax                                ; 8e c0                       ; 0xc10d4
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc10d6
    sal bx, 003h                              ; c1 e3 03                    ; 0xc10d9 vgabios.c:58
    mov ax, dx                                ; 89 d0                       ; 0xc10dc
    mul bx                                    ; f7 e3                       ; 0xc10de
    mov bx, si                                ; 89 f3                       ; 0xc10e0
    add bx, ax                                ; 01 c3                       ; 0xc10e2
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc10e4 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc10e7
    mov ch, byte [es:bx]                      ; 26 8a 2f                    ; 0xc10e9
    jmp short 010f0h                          ; eb 02                       ; 0xc10ec vgabios.c:676
    xor ch, ch                                ; 30 ed                       ; 0xc10ee vgabios.c:681
    push SS                                   ; 16                          ; 0xc10f0 vgabios.c:683
    pop ES                                    ; 07                          ; 0xc10f1
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc10f2
    mov byte [es:bx], ch                      ; 26 88 2f                    ; 0xc10f5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc10f8 vgabios.c:684
    pop di                                    ; 5f                          ; 0xc10fb
    pop si                                    ; 5e                          ; 0xc10fc
    pop bp                                    ; 5d                          ; 0xc10fd
    retn                                      ; c3                          ; 0xc10fe
  ; disGetNextSymbol 0xc10ff LB 0x3433 -> off=0x0 cb=000000000000008d uValue=00000000000c10ff 'biosfn_perform_gray_scale_summing'
biosfn_perform_gray_scale_summing:           ; 0xc10ff LB 0x8d
    push bp                                   ; 55                          ; 0xc10ff vgabios.c:689
    mov bp, sp                                ; 89 e5                       ; 0xc1100
    push bx                                   ; 53                          ; 0xc1102
    push cx                                   ; 51                          ; 0xc1103
    push si                                   ; 56                          ; 0xc1104
    push di                                   ; 57                          ; 0xc1105
    push ax                                   ; 50                          ; 0xc1106
    push ax                                   ; 50                          ; 0xc1107
    mov bx, ax                                ; 89 c3                       ; 0xc1108
    mov di, dx                                ; 89 d7                       ; 0xc110a
    mov dx, 003dah                            ; ba da 03                    ; 0xc110c vgabios.c:694
    in AL, DX                                 ; ec                          ; 0xc110f
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1110
    xor al, al                                ; 30 c0                       ; 0xc1112 vgabios.c:695
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1114
    out DX, AL                                ; ee                          ; 0xc1117
    xor si, si                                ; 31 f6                       ; 0xc1118 vgabios.c:697
    cmp si, di                                ; 39 fe                       ; 0xc111a
    jnc short 01171h                          ; 73 53                       ; 0xc111c
    mov al, bl                                ; 88 d8                       ; 0xc111e vgabios.c:700
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc1120
    out DX, AL                                ; ee                          ; 0xc1123
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1124 vgabios.c:702
    in AL, DX                                 ; ec                          ; 0xc1127
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1128
    mov cx, ax                                ; 89 c1                       ; 0xc112a
    in AL, DX                                 ; ec                          ; 0xc112c vgabios.c:703
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc112d
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc112f
    in AL, DX                                 ; ec                          ; 0xc1132 vgabios.c:704
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1133
    xor ch, ch                                ; 30 ed                       ; 0xc1135 vgabios.c:707
    imul cx, cx, strict byte 0004dh           ; 6b c9 4d                    ; 0xc1137
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc113a
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc113d
    xor ch, ch                                ; 30 ed                       ; 0xc1140
    imul cx, cx, 00097h                       ; 69 c9 97 00                 ; 0xc1142
    add cx, word [bp-00ah]                    ; 03 4e f6                    ; 0xc1146
    xor ah, ah                                ; 30 e4                       ; 0xc1149
    imul ax, ax, strict byte 0001ch           ; 6b c0 1c                    ; 0xc114b
    add cx, ax                                ; 01 c1                       ; 0xc114e
    add cx, 00080h                            ; 81 c1 80 00                 ; 0xc1150
    sar cx, 008h                              ; c1 f9 08                    ; 0xc1154
    cmp cx, strict byte 0003fh                ; 83 f9 3f                    ; 0xc1157 vgabios.c:709
    jbe short 0115fh                          ; 76 03                       ; 0xc115a
    mov cx, strict word 0003fh                ; b9 3f 00                    ; 0xc115c
    mov al, bl                                ; 88 d8                       ; 0xc115f vgabios.c:712
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc1161
    out DX, AL                                ; ee                          ; 0xc1164
    mov al, cl                                ; 88 c8                       ; 0xc1165 vgabios.c:714
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1167
    out DX, AL                                ; ee                          ; 0xc116a
    out DX, AL                                ; ee                          ; 0xc116b vgabios.c:715
    out DX, AL                                ; ee                          ; 0xc116c vgabios.c:716
    inc bx                                    ; 43                          ; 0xc116d vgabios.c:717
    inc si                                    ; 46                          ; 0xc116e vgabios.c:718
    jmp short 0111ah                          ; eb a9                       ; 0xc116f
    mov dx, 003dah                            ; ba da 03                    ; 0xc1171 vgabios.c:719
    in AL, DX                                 ; ec                          ; 0xc1174
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1175
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc1177 vgabios.c:720
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1179
    out DX, AL                                ; ee                          ; 0xc117c
    mov dx, 003dah                            ; ba da 03                    ; 0xc117d vgabios.c:722
    in AL, DX                                 ; ec                          ; 0xc1180
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1181
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1183 vgabios.c:724
    pop di                                    ; 5f                          ; 0xc1186
    pop si                                    ; 5e                          ; 0xc1187
    pop cx                                    ; 59                          ; 0xc1188
    pop bx                                    ; 5b                          ; 0xc1189
    pop bp                                    ; 5d                          ; 0xc118a
    retn                                      ; c3                          ; 0xc118b
  ; disGetNextSymbol 0xc118c LB 0x33a6 -> off=0x0 cb=0000000000000107 uValue=00000000000c118c 'biosfn_set_cursor_shape'
biosfn_set_cursor_shape:                     ; 0xc118c LB 0x107
    push bp                                   ; 55                          ; 0xc118c vgabios.c:727
    mov bp, sp                                ; 89 e5                       ; 0xc118d
    push bx                                   ; 53                          ; 0xc118f
    push cx                                   ; 51                          ; 0xc1190
    push si                                   ; 56                          ; 0xc1191
    push ax                                   ; 50                          ; 0xc1192
    push ax                                   ; 50                          ; 0xc1193
    mov bl, al                                ; 88 c3                       ; 0xc1194
    mov ah, dl                                ; 88 d4                       ; 0xc1196
    mov dl, al                                ; 88 c2                       ; 0xc1198 vgabios.c:733
    xor dh, dh                                ; 30 f6                       ; 0xc119a
    mov cx, dx                                ; 89 d1                       ; 0xc119c
    sal cx, 008h                              ; c1 e1 08                    ; 0xc119e
    mov dl, ah                                ; 88 e2                       ; 0xc11a1
    add dx, cx                                ; 01 ca                       ; 0xc11a3
    mov si, strict word 00060h                ; be 60 00                    ; 0xc11a5 vgabios.c:62
    mov cx, strict word 00040h                ; b9 40 00                    ; 0xc11a8
    mov es, cx                                ; 8e c1                       ; 0xc11ab
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc11ad
    mov si, 00087h                            ; be 87 00                    ; 0xc11b0 vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc11b3
    test dl, 008h                             ; f6 c2 08                    ; 0xc11b6 vgabios.c:48
    jne short 011f8h                          ; 75 3d                       ; 0xc11b9
    mov dl, al                                ; 88 c2                       ; 0xc11bb vgabios.c:739
    and dl, 060h                              ; 80 e2 60                    ; 0xc11bd
    cmp dl, 020h                              ; 80 fa 20                    ; 0xc11c0
    jne short 011cbh                          ; 75 06                       ; 0xc11c3
    mov BL, strict byte 01eh                  ; b3 1e                       ; 0xc11c5 vgabios.c:741
    xor ah, ah                                ; 30 e4                       ; 0xc11c7 vgabios.c:742
    jmp short 011f8h                          ; eb 2d                       ; 0xc11c9 vgabios.c:743
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc11cb vgabios.c:47
    test dl, 001h                             ; f6 c2 01                    ; 0xc11ce vgabios.c:48
    jne short 0122dh                          ; 75 5a                       ; 0xc11d1
    cmp bl, 020h                              ; 80 fb 20                    ; 0xc11d3
    jnc short 0122dh                          ; 73 55                       ; 0xc11d6
    cmp ah, 020h                              ; 80 fc 20                    ; 0xc11d8
    jnc short 0122dh                          ; 73 50                       ; 0xc11db
    mov si, 00085h                            ; be 85 00                    ; 0xc11dd vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc11e0
    mov es, dx                                ; 8e c2                       ; 0xc11e3
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc11e5
    mov dx, cx                                ; 89 ca                       ; 0xc11e8 vgabios.c:58
    cmp ah, bl                                ; 38 dc                       ; 0xc11ea vgabios.c:754
    jnc short 011fah                          ; 73 0c                       ; 0xc11ec
    test ah, ah                               ; 84 e4                       ; 0xc11ee vgabios.c:756
    je short 0122dh                           ; 74 3b                       ; 0xc11f0
    xor bl, bl                                ; 30 db                       ; 0xc11f2 vgabios.c:757
    mov ah, cl                                ; 88 cc                       ; 0xc11f4 vgabios.c:758
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc11f6
    jmp short 0122dh                          ; eb 33                       ; 0xc11f8 vgabios.c:760
    mov byte [bp-008h], ah                    ; 88 66 f8                    ; 0xc11fa vgabios.c:761
    xor al, al                                ; 30 c0                       ; 0xc11fd
    mov byte [bp-007h], al                    ; 88 46 f9                    ; 0xc11ff
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1202
    mov byte [bp-009h], al                    ; 88 46 f7                    ; 0xc1205
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc1208
    or si, word [bp-00ah]                     ; 0b 76 f6                    ; 0xc120b
    cmp si, cx                                ; 39 ce                       ; 0xc120e
    jnc short 0122fh                          ; 73 1d                       ; 0xc1210
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc1212
    mov byte [bp-009h], al                    ; 88 46 f7                    ; 0xc1215
    mov si, cx                                ; 89 ce                       ; 0xc1218
    dec si                                    ; 4e                          ; 0xc121a
    cmp si, word [bp-00ah]                    ; 3b 76 f6                    ; 0xc121b
    je short 01269h                           ; 74 49                       ; 0xc121e
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc1220
    mov byte [bp-007h], al                    ; 88 46 f9                    ; 0xc1223
    dec cx                                    ; 49                          ; 0xc1226
    dec cx                                    ; 49                          ; 0xc1227
    cmp cx, word [bp-008h]                    ; 3b 4e f8                    ; 0xc1228
    jne short 0122fh                          ; 75 02                       ; 0xc122b
    jmp short 01269h                          ; eb 3a                       ; 0xc122d
    cmp ah, 003h                              ; 80 fc 03                    ; 0xc122f vgabios.c:763
    jbe short 01269h                          ; 76 35                       ; 0xc1232
    mov cl, bl                                ; 88 d9                       ; 0xc1234 vgabios.c:764
    xor ch, ch                                ; 30 ed                       ; 0xc1236
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc1238
    mov byte [bp-009h], ch                    ; 88 6e f7                    ; 0xc123b
    mov si, cx                                ; 89 ce                       ; 0xc123e
    inc si                                    ; 46                          ; 0xc1240
    inc si                                    ; 46                          ; 0xc1241
    mov cl, dl                                ; 88 d1                       ; 0xc1242
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc1244
    cmp si, word [bp-00ah]                    ; 3b 76 f6                    ; 0xc1246
    jl short 0125eh                           ; 7c 13                       ; 0xc1249
    sub bl, ah                                ; 28 e3                       ; 0xc124b vgabios.c:766
    add bl, dl                                ; 00 d3                       ; 0xc124d
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc124f
    mov ah, cl                                ; 88 cc                       ; 0xc1251 vgabios.c:767
    cmp dx, strict byte 0000eh                ; 83 fa 0e                    ; 0xc1253 vgabios.c:768
    jc short 01269h                           ; 72 11                       ; 0xc1256
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc1258 vgabios.c:770
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc125a vgabios.c:771
    jmp short 01269h                          ; eb 0b                       ; 0xc125c vgabios.c:773
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc125e
    jbe short 01267h                          ; 76 04                       ; 0xc1261
    shr dx, 1                                 ; d1 ea                       ; 0xc1263 vgabios.c:775
    mov bl, dl                                ; 88 d3                       ; 0xc1265
    mov ah, cl                                ; 88 cc                       ; 0xc1267 vgabios.c:779
    mov si, strict word 00063h                ; be 63 00                    ; 0xc1269 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc126c
    mov es, dx                                ; 8e c2                       ; 0xc126f
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc1271
    mov AL, strict byte 00ah                  ; b0 0a                       ; 0xc1274 vgabios.c:790
    mov dx, cx                                ; 89 ca                       ; 0xc1276
    out DX, AL                                ; ee                          ; 0xc1278
    mov si, cx                                ; 89 ce                       ; 0xc1279 vgabios.c:791
    inc si                                    ; 46                          ; 0xc127b
    mov al, bl                                ; 88 d8                       ; 0xc127c
    mov dx, si                                ; 89 f2                       ; 0xc127e
    out DX, AL                                ; ee                          ; 0xc1280
    mov AL, strict byte 00bh                  ; b0 0b                       ; 0xc1281 vgabios.c:792
    mov dx, cx                                ; 89 ca                       ; 0xc1283
    out DX, AL                                ; ee                          ; 0xc1285
    mov al, ah                                ; 88 e0                       ; 0xc1286 vgabios.c:793
    mov dx, si                                ; 89 f2                       ; 0xc1288
    out DX, AL                                ; ee                          ; 0xc128a
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc128b vgabios.c:794
    pop si                                    ; 5e                          ; 0xc128e
    pop cx                                    ; 59                          ; 0xc128f
    pop bx                                    ; 5b                          ; 0xc1290
    pop bp                                    ; 5d                          ; 0xc1291
    retn                                      ; c3                          ; 0xc1292
  ; disGetNextSymbol 0xc1293 LB 0x329f -> off=0x0 cb=000000000000008f uValue=00000000000c1293 'biosfn_set_cursor_pos'
biosfn_set_cursor_pos:                       ; 0xc1293 LB 0x8f
    push bp                                   ; 55                          ; 0xc1293 vgabios.c:797
    mov bp, sp                                ; 89 e5                       ; 0xc1294
    push bx                                   ; 53                          ; 0xc1296
    push cx                                   ; 51                          ; 0xc1297
    push si                                   ; 56                          ; 0xc1298
    push di                                   ; 57                          ; 0xc1299
    push ax                                   ; 50                          ; 0xc129a
    mov bl, al                                ; 88 c3                       ; 0xc129b
    mov cx, dx                                ; 89 d1                       ; 0xc129d
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc129f vgabios.c:803
    jnbe short 01319h                         ; 77 76                       ; 0xc12a1
    xor ah, ah                                ; 30 e4                       ; 0xc12a3 vgabios.c:806
    mov si, ax                                ; 89 c6                       ; 0xc12a5
    add si, ax                                ; 01 c6                       ; 0xc12a7
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc12a9
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc12ac vgabios.c:62
    mov es, ax                                ; 8e c0                       ; 0xc12af
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc12b1
    mov si, strict word 00062h                ; be 62 00                    ; 0xc12b4 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc12b7
    cmp bl, al                                ; 38 c3                       ; 0xc12ba vgabios.c:810
    jne short 01319h                          ; 75 5b                       ; 0xc12bc
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc12be vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc12c1
    mov si, 00084h                            ; be 84 00                    ; 0xc12c4 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc12c7
    xor ah, ah                                ; 30 e4                       ; 0xc12ca vgabios.c:48
    mov si, ax                                ; 89 c6                       ; 0xc12cc
    inc si                                    ; 46                          ; 0xc12ce
    mov ax, dx                                ; 89 d0                       ; 0xc12cf vgabios.c:816
    xor al, dl                                ; 30 d0                       ; 0xc12d1
    shr ax, 008h                              ; c1 e8 08                    ; 0xc12d3
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc12d6
    mov ax, di                                ; 89 f8                       ; 0xc12d9 vgabios.c:819
    mul si                                    ; f7 e6                       ; 0xc12db
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc12dd
    xor bh, bh                                ; 30 ff                       ; 0xc12df
    inc ax                                    ; 40                          ; 0xc12e1
    mul bx                                    ; f7 e3                       ; 0xc12e2
    mov bl, cl                                ; 88 cb                       ; 0xc12e4
    mov si, bx                                ; 89 de                       ; 0xc12e6
    add si, ax                                ; 01 c6                       ; 0xc12e8
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc12ea
    xor ah, ah                                ; 30 e4                       ; 0xc12ed
    mul di                                    ; f7 e7                       ; 0xc12ef
    add si, ax                                ; 01 c6                       ; 0xc12f1
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc12f3 vgabios.c:57
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc12f6
    mov AL, strict byte 00eh                  ; b0 0e                       ; 0xc12f9 vgabios.c:823
    mov dx, bx                                ; 89 da                       ; 0xc12fb
    out DX, AL                                ; ee                          ; 0xc12fd
    mov ax, si                                ; 89 f0                       ; 0xc12fe vgabios.c:824
    xor al, al                                ; 30 c0                       ; 0xc1300
    shr ax, 008h                              ; c1 e8 08                    ; 0xc1302
    lea cx, [bx+001h]                         ; 8d 4f 01                    ; 0xc1305
    mov dx, cx                                ; 89 ca                       ; 0xc1308
    out DX, AL                                ; ee                          ; 0xc130a
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc130b vgabios.c:825
    mov dx, bx                                ; 89 da                       ; 0xc130d
    out DX, AL                                ; ee                          ; 0xc130f
    and si, 000ffh                            ; 81 e6 ff 00                 ; 0xc1310 vgabios.c:826
    mov ax, si                                ; 89 f0                       ; 0xc1314
    mov dx, cx                                ; 89 ca                       ; 0xc1316
    out DX, AL                                ; ee                          ; 0xc1318
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1319 vgabios.c:828
    pop di                                    ; 5f                          ; 0xc131c
    pop si                                    ; 5e                          ; 0xc131d
    pop cx                                    ; 59                          ; 0xc131e
    pop bx                                    ; 5b                          ; 0xc131f
    pop bp                                    ; 5d                          ; 0xc1320
    retn                                      ; c3                          ; 0xc1321
  ; disGetNextSymbol 0xc1322 LB 0x3210 -> off=0x0 cb=00000000000000d8 uValue=00000000000c1322 'biosfn_set_active_page'
biosfn_set_active_page:                      ; 0xc1322 LB 0xd8
    push bp                                   ; 55                          ; 0xc1322 vgabios.c:831
    mov bp, sp                                ; 89 e5                       ; 0xc1323
    push bx                                   ; 53                          ; 0xc1325
    push cx                                   ; 51                          ; 0xc1326
    push dx                                   ; 52                          ; 0xc1327
    push si                                   ; 56                          ; 0xc1328
    push di                                   ; 57                          ; 0xc1329
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc132a
    mov cl, al                                ; 88 c1                       ; 0xc132d
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc132f vgabios.c:837
    jnbe short 01349h                         ; 77 16                       ; 0xc1331
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1333 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1336
    mov es, ax                                ; 8e c0                       ; 0xc1339
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc133b
    xor ah, ah                                ; 30 e4                       ; 0xc133e vgabios.c:841
    call 03819h                               ; e8 d6 24                    ; 0xc1340
    mov ch, al                                ; 88 c5                       ; 0xc1343
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1345 vgabios.c:842
    jne short 0134ch                          ; 75 03                       ; 0xc1347
    jmp near 013f0h                           ; e9 a4 00                    ; 0xc1349
    mov al, cl                                ; 88 c8                       ; 0xc134c vgabios.c:845
    xor ah, ah                                ; 30 e4                       ; 0xc134e
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc1350
    lea dx, [bp-010h]                         ; 8d 56 f0                    ; 0xc1353
    call 00a96h                               ; e8 3d f7                    ; 0xc1356
    mov bl, ch                                ; 88 eb                       ; 0xc1359 vgabios.c:847
    xor bh, bh                                ; 30 ff                       ; 0xc135b
    mov si, bx                                ; 89 de                       ; 0xc135d
    sal si, 003h                              ; c1 e6 03                    ; 0xc135f
    cmp byte [si+047b2h], 000h                ; 80 bc b2 47 00              ; 0xc1362
    jne short 013a8h                          ; 75 3f                       ; 0xc1367
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1369 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc136c
    mov es, ax                                ; 8e c0                       ; 0xc136f
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc1371
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1374 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1377
    xor ah, ah                                ; 30 e4                       ; 0xc137a vgabios.c:48
    mov bx, ax                                ; 89 c3                       ; 0xc137c
    inc bx                                    ; 43                          ; 0xc137e
    mov ax, dx                                ; 89 d0                       ; 0xc137f vgabios.c:854
    mul bx                                    ; f7 e3                       ; 0xc1381
    mov di, ax                                ; 89 c7                       ; 0xc1383
    add ax, ax                                ; 01 c0                       ; 0xc1385
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc1387
    mov byte [bp-00ch], cl                    ; 88 4e f4                    ; 0xc1389
    mov byte [bp-00bh], 000h                  ; c6 46 f5 00                 ; 0xc138c
    inc ax                                    ; 40                          ; 0xc1390
    mul word [bp-00ch]                        ; f7 66 f4                    ; 0xc1391
    mov bx, ax                                ; 89 c3                       ; 0xc1394
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc1396 vgabios.c:62
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc1399
    or di, 000ffh                             ; 81 cf ff 00                 ; 0xc139c vgabios.c:858
    lea ax, [di+001h]                         ; 8d 45 01                    ; 0xc13a0
    mul word [bp-00ch]                        ; f7 66 f4                    ; 0xc13a3
    jmp short 013b7h                          ; eb 0f                       ; 0xc13a6 vgabios.c:860
    mov bl, byte [bx+04831h]                  ; 8a 9f 31 48                 ; 0xc13a8 vgabios.c:862
    sal bx, 006h                              ; c1 e3 06                    ; 0xc13ac
    mov al, cl                                ; 88 c8                       ; 0xc13af
    xor ah, ah                                ; 30 e4                       ; 0xc13b1
    mul word [bx+04848h]                      ; f7 a7 48 48                 ; 0xc13b3
    mov bx, ax                                ; 89 c3                       ; 0xc13b7
    mov si, strict word 00063h                ; be 63 00                    ; 0xc13b9 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc13bc
    mov es, ax                                ; 8e c0                       ; 0xc13bf
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc13c1
    mov AL, strict byte 00ch                  ; b0 0c                       ; 0xc13c4 vgabios.c:867
    mov dx, si                                ; 89 f2                       ; 0xc13c6
    out DX, AL                                ; ee                          ; 0xc13c8
    mov ax, bx                                ; 89 d8                       ; 0xc13c9 vgabios.c:868
    xor al, bl                                ; 30 d8                       ; 0xc13cb
    shr ax, 008h                              ; c1 e8 08                    ; 0xc13cd
    lea di, [si+001h]                         ; 8d 7c 01                    ; 0xc13d0
    mov dx, di                                ; 89 fa                       ; 0xc13d3
    out DX, AL                                ; ee                          ; 0xc13d5
    mov AL, strict byte 00dh                  ; b0 0d                       ; 0xc13d6 vgabios.c:869
    mov dx, si                                ; 89 f2                       ; 0xc13d8
    out DX, AL                                ; ee                          ; 0xc13da
    xor bh, bh                                ; 30 ff                       ; 0xc13db vgabios.c:870
    mov ax, bx                                ; 89 d8                       ; 0xc13dd
    mov dx, di                                ; 89 fa                       ; 0xc13df
    out DX, AL                                ; ee                          ; 0xc13e1
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc13e2 vgabios.c:52
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc13e5
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc13e8 vgabios.c:880
    mov al, cl                                ; 88 c8                       ; 0xc13eb
    call 01293h                               ; e8 a3 fe                    ; 0xc13ed
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc13f0 vgabios.c:881
    pop di                                    ; 5f                          ; 0xc13f3
    pop si                                    ; 5e                          ; 0xc13f4
    pop dx                                    ; 5a                          ; 0xc13f5
    pop cx                                    ; 59                          ; 0xc13f6
    pop bx                                    ; 5b                          ; 0xc13f7
    pop bp                                    ; 5d                          ; 0xc13f8
    retn                                      ; c3                          ; 0xc13f9
  ; disGetNextSymbol 0xc13fa LB 0x3138 -> off=0x0 cb=0000000000000045 uValue=00000000000c13fa 'find_vpti'
find_vpti:                                   ; 0xc13fa LB 0x45
    push bx                                   ; 53                          ; 0xc13fa vgabios.c:916
    push si                                   ; 56                          ; 0xc13fb
    push bp                                   ; 55                          ; 0xc13fc
    mov bp, sp                                ; 89 e5                       ; 0xc13fd
    mov bl, al                                ; 88 c3                       ; 0xc13ff vgabios.c:921
    xor bh, bh                                ; 30 ff                       ; 0xc1401
    mov si, bx                                ; 89 de                       ; 0xc1403
    sal si, 003h                              ; c1 e6 03                    ; 0xc1405
    cmp byte [si+047b2h], 000h                ; 80 bc b2 47 00              ; 0xc1408
    jne short 01435h                          ; 75 26                       ; 0xc140d
    mov si, 00089h                            ; be 89 00                    ; 0xc140f vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1412
    mov es, ax                                ; 8e c0                       ; 0xc1415
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc1417
    test AL, strict byte 010h                 ; a8 10                       ; 0xc141a vgabios.c:923
    je short 01424h                           ; 74 06                       ; 0xc141c
    mov al, byte [bx+07df8h]                  ; 8a 87 f8 7d                 ; 0xc141e vgabios.c:924
    jmp short 01432h                          ; eb 0e                       ; 0xc1422 vgabios.c:925
    test AL, strict byte 080h                 ; a8 80                       ; 0xc1424
    je short 0142eh                           ; 74 06                       ; 0xc1426
    mov al, byte [bx+07de8h]                  ; 8a 87 e8 7d                 ; 0xc1428 vgabios.c:926
    jmp short 01432h                          ; eb 04                       ; 0xc142c vgabios.c:927
    mov al, byte [bx+07df0h]                  ; 8a 87 f0 7d                 ; 0xc142e vgabios.c:928
    cbw                                       ; 98                          ; 0xc1432
    jmp short 0143bh                          ; eb 06                       ; 0xc1433 vgabios.c:929
    mov al, byte [bx+04831h]                  ; 8a 87 31 48                 ; 0xc1435 vgabios.c:930
    xor ah, ah                                ; 30 e4                       ; 0xc1439
    pop bp                                    ; 5d                          ; 0xc143b vgabios.c:933
    pop si                                    ; 5e                          ; 0xc143c
    pop bx                                    ; 5b                          ; 0xc143d
    retn                                      ; c3                          ; 0xc143e
  ; disGetNextSymbol 0xc143f LB 0x30f3 -> off=0x0 cb=00000000000004d5 uValue=00000000000c143f 'biosfn_set_video_mode'
biosfn_set_video_mode:                       ; 0xc143f LB 0x4d5
    push bp                                   ; 55                          ; 0xc143f vgabios.c:938
    mov bp, sp                                ; 89 e5                       ; 0xc1440
    push bx                                   ; 53                          ; 0xc1442
    push cx                                   ; 51                          ; 0xc1443
    push dx                                   ; 52                          ; 0xc1444
    push si                                   ; 56                          ; 0xc1445
    push di                                   ; 57                          ; 0xc1446
    sub sp, strict byte 00018h                ; 83 ec 18                    ; 0xc1447
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc144a
    and AL, strict byte 080h                  ; 24 80                       ; 0xc144d vgabios.c:942
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc144f
    call 007bfh                               ; e8 6a f3                    ; 0xc1452 vgabios.c:952
    test ax, ax                               ; 85 c0                       ; 0xc1455
    je short 01465h                           ; 74 0c                       ; 0xc1457
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc1459 vgabios.c:954
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc145b
    out DX, AL                                ; ee                          ; 0xc145e
    xor al, al                                ; 30 c0                       ; 0xc145f vgabios.c:955
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc1461
    out DX, AL                                ; ee                          ; 0xc1464
    and byte [bp-010h], 07fh                  ; 80 66 f0 7f                 ; 0xc1465 vgabios.c:960
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1469 vgabios.c:966
    xor ah, ah                                ; 30 e4                       ; 0xc146c
    call 03819h                               ; e8 a8 23                    ; 0xc146e
    mov cl, al                                ; 88 c1                       ; 0xc1471
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc1473
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1476 vgabios.c:972
    je short 014e5h                           ; 74 6b                       ; 0xc1478
    mov bx, 000a8h                            ; bb a8 00                    ; 0xc147a vgabios.c:67
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc147d
    mov es, ax                                ; 8e c0                       ; 0xc1480
    mov di, word [es:bx]                      ; 26 8b 3f                    ; 0xc1482
    mov ax, word [es:bx+002h]                 ; 26 8b 47 02                 ; 0xc1485
    mov bx, di                                ; 89 fb                       ; 0xc1489 vgabios.c:68
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc148b
    xor ch, ch                                ; 30 ed                       ; 0xc148e vgabios.c:978
    mov ax, cx                                ; 89 c8                       ; 0xc1490
    call 013fah                               ; e8 65 ff                    ; 0xc1492
    mov es, [bp-018h]                         ; 8e 46 e8                    ; 0xc1495 vgabios.c:979
    mov si, word [es:di]                      ; 26 8b 35                    ; 0xc1498
    mov dx, word [es:di+002h]                 ; 26 8b 55 02                 ; 0xc149b
    mov word [bp-01eh], dx                    ; 89 56 e2                    ; 0xc149f
    xor ah, ah                                ; 30 e4                       ; 0xc14a2 vgabios.c:980
    sal ax, 006h                              ; c1 e0 06                    ; 0xc14a4
    add si, ax                                ; 01 c6                       ; 0xc14a7
    mov di, 00089h                            ; bf 89 00                    ; 0xc14a9 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc14ac
    mov es, ax                                ; 8e c0                       ; 0xc14af
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc14b1
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc14b4 vgabios.c:48
    test AL, strict byte 008h                 ; a8 08                       ; 0xc14b7 vgabios.c:997
    jne short 01501h                          ; 75 46                       ; 0xc14b9
    mov di, cx                                ; 89 cf                       ; 0xc14bb vgabios.c:999
    sal di, 003h                              ; c1 e7 03                    ; 0xc14bd
    mov al, byte [di+047b7h]                  ; 8a 85 b7 47                 ; 0xc14c0
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc14c4
    out DX, AL                                ; ee                          ; 0xc14c7
    xor al, al                                ; 30 c0                       ; 0xc14c8 vgabios.c:1002
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc14ca
    out DX, AL                                ; ee                          ; 0xc14cd
    mov cl, byte [di+047b8h]                  ; 8a 8d b8 47                 ; 0xc14ce vgabios.c:1005
    cmp cl, 001h                              ; 80 f9 01                    ; 0xc14d2
    jc short 014e8h                           ; 72 11                       ; 0xc14d5
    jbe short 014f3h                          ; 76 1a                       ; 0xc14d7
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc14d9
    je short 01504h                           ; 74 26                       ; 0xc14dc
    cmp cl, 002h                              ; 80 f9 02                    ; 0xc14de
    je short 014fah                           ; 74 17                       ; 0xc14e1
    jmp short 01509h                          ; eb 24                       ; 0xc14e3
    jmp near 0190ah                           ; e9 22 04                    ; 0xc14e5
    test cl, cl                               ; 84 c9                       ; 0xc14e8
    jne short 01509h                          ; 75 1d                       ; 0xc14ea
    mov word [bp-014h], 04fc5h                ; c7 46 ec c5 4f              ; 0xc14ec vgabios.c:1007
    jmp short 01509h                          ; eb 16                       ; 0xc14f1 vgabios.c:1008
    mov word [bp-014h], 05085h                ; c7 46 ec 85 50              ; 0xc14f3 vgabios.c:1010
    jmp short 01509h                          ; eb 0f                       ; 0xc14f8 vgabios.c:1011
    mov word [bp-014h], 05145h                ; c7 46 ec 45 51              ; 0xc14fa vgabios.c:1013
    jmp short 01509h                          ; eb 08                       ; 0xc14ff vgabios.c:1014
    jmp near 01578h                           ; e9 74 00                    ; 0xc1501
    mov word [bp-014h], 05205h                ; c7 46 ec 05 52              ; 0xc1504 vgabios.c:1016
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1509 vgabios.c:1020
    xor ah, ah                                ; 30 e4                       ; 0xc150c
    mov di, ax                                ; 89 c7                       ; 0xc150e
    sal di, 003h                              ; c1 e7 03                    ; 0xc1510
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc1513
    jne short 01529h                          ; 75 0f                       ; 0xc1518
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc151a vgabios.c:1022
    cmp byte [es:si+002h], 008h               ; 26 80 7c 02 08              ; 0xc151d
    jne short 01529h                          ; 75 05                       ; 0xc1522
    mov word [bp-014h], 05085h                ; c7 46 ec 85 50              ; 0xc1524 vgabios.c:1023
    xor cx, cx                                ; 31 c9                       ; 0xc1529 vgabios.c:1026
    jmp short 0153ch                          ; eb 0f                       ; 0xc152b
    xor al, al                                ; 30 c0                       ; 0xc152d vgabios.c:1033
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc152f
    out DX, AL                                ; ee                          ; 0xc1532
    out DX, AL                                ; ee                          ; 0xc1533 vgabios.c:1034
    out DX, AL                                ; ee                          ; 0xc1534 vgabios.c:1035
    inc cx                                    ; 41                          ; 0xc1535 vgabios.c:1037
    cmp cx, 00100h                            ; 81 f9 00 01                 ; 0xc1536
    jnc short 0156ah                          ; 73 2e                       ; 0xc153a
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc153c
    xor ah, ah                                ; 30 e4                       ; 0xc153f
    mov di, ax                                ; 89 c7                       ; 0xc1541
    sal di, 003h                              ; c1 e7 03                    ; 0xc1543
    mov al, byte [di+047b8h]                  ; 8a 85 b8 47                 ; 0xc1546
    mov di, ax                                ; 89 c7                       ; 0xc154a
    mov al, byte [di+04841h]                  ; 8a 85 41 48                 ; 0xc154c
    cmp cx, ax                                ; 39 c1                       ; 0xc1550
    jnbe short 0152dh                         ; 77 d9                       ; 0xc1552
    imul di, cx, strict byte 00003h           ; 6b f9 03                    ; 0xc1554
    add di, word [bp-014h]                    ; 03 7e ec                    ; 0xc1557
    mov al, byte [di]                         ; 8a 05                       ; 0xc155a
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc155c
    out DX, AL                                ; ee                          ; 0xc155f
    mov al, byte [di+001h]                    ; 8a 45 01                    ; 0xc1560
    out DX, AL                                ; ee                          ; 0xc1563
    mov al, byte [di+002h]                    ; 8a 45 02                    ; 0xc1564
    out DX, AL                                ; ee                          ; 0xc1567
    jmp short 01535h                          ; eb cb                       ; 0xc1568
    test byte [bp-00ch], 002h                 ; f6 46 f4 02                 ; 0xc156a vgabios.c:1038
    je short 01578h                           ; 74 08                       ; 0xc156e
    mov dx, 00100h                            ; ba 00 01                    ; 0xc1570 vgabios.c:1040
    xor ax, ax                                ; 31 c0                       ; 0xc1573
    call 010ffh                               ; e8 87 fb                    ; 0xc1575
    mov dx, 003dah                            ; ba da 03                    ; 0xc1578 vgabios.c:1045
    in AL, DX                                 ; ec                          ; 0xc157b
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc157c
    xor cx, cx                                ; 31 c9                       ; 0xc157e vgabios.c:1048
    jmp short 01587h                          ; eb 05                       ; 0xc1580
    cmp cx, strict byte 00013h                ; 83 f9 13                    ; 0xc1582
    jnbe short 0159ch                         ; 77 15                       ; 0xc1585
    mov al, cl                                ; 88 c8                       ; 0xc1587 vgabios.c:1049
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1589
    out DX, AL                                ; ee                          ; 0xc158c
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc158d vgabios.c:1050
    mov di, si                                ; 89 f7                       ; 0xc1590
    add di, cx                                ; 01 cf                       ; 0xc1592
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc1594
    out DX, AL                                ; ee                          ; 0xc1598
    inc cx                                    ; 41                          ; 0xc1599 vgabios.c:1051
    jmp short 01582h                          ; eb e6                       ; 0xc159a
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc159c vgabios.c:1052
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc159e
    out DX, AL                                ; ee                          ; 0xc15a1
    xor al, al                                ; 30 c0                       ; 0xc15a2 vgabios.c:1053
    out DX, AL                                ; ee                          ; 0xc15a4
    mov es, [bp-018h]                         ; 8e 46 e8                    ; 0xc15a5 vgabios.c:1056
    mov dx, word [es:bx+004h]                 ; 26 8b 57 04                 ; 0xc15a8
    mov ax, word [es:bx+006h]                 ; 26 8b 47 06                 ; 0xc15ac
    test ax, ax                               ; 85 c0                       ; 0xc15b0
    jne short 015b8h                          ; 75 04                       ; 0xc15b2
    test dx, dx                               ; 85 d2                       ; 0xc15b4
    je short 015f5h                           ; 74 3d                       ; 0xc15b6
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc15b8 vgabios.c:1060
    xor cx, cx                                ; 31 c9                       ; 0xc15bb vgabios.c:1061
    jmp short 015c4h                          ; eb 05                       ; 0xc15bd
    cmp cx, strict byte 00010h                ; 83 f9 10                    ; 0xc15bf
    jnc short 015e5h                          ; 73 21                       ; 0xc15c2
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc15c4 vgabios.c:1062
    mov di, si                                ; 89 f7                       ; 0xc15c7
    add di, cx                                ; 01 cf                       ; 0xc15c9
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc15cb
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc15ce
    mov ax, dx                                ; 89 d0                       ; 0xc15d1
    add ax, cx                                ; 01 c8                       ; 0xc15d3
    mov word [bp-022h], ax                    ; 89 46 de                    ; 0xc15d5
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc15d8
    les di, [bp-022h]                         ; c4 7e de                    ; 0xc15dc
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc15df
    inc cx                                    ; 41                          ; 0xc15e2
    jmp short 015bfh                          ; eb da                       ; 0xc15e3
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc15e5 vgabios.c:1063
    mov al, byte [es:si+034h]                 ; 26 8a 44 34                 ; 0xc15e8
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc15ec
    mov di, dx                                ; 89 d7                       ; 0xc15ef
    mov byte [es:di+010h], al                 ; 26 88 45 10                 ; 0xc15f1
    xor al, al                                ; 30 c0                       ; 0xc15f5 vgabios.c:1067
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc15f7
    out DX, AL                                ; ee                          ; 0xc15fa
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc15fb vgabios.c:1068
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc15fd
    out DX, AL                                ; ee                          ; 0xc1600
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc1601 vgabios.c:1069
    jmp short 0160bh                          ; eb 05                       ; 0xc1604
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc1606
    jnbe short 01623h                         ; 77 18                       ; 0xc1609
    mov al, cl                                ; 88 c8                       ; 0xc160b vgabios.c:1070
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc160d
    out DX, AL                                ; ee                          ; 0xc1610
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1611 vgabios.c:1071
    mov di, si                                ; 89 f7                       ; 0xc1614
    add di, cx                                ; 01 cf                       ; 0xc1616
    mov al, byte [es:di+004h]                 ; 26 8a 45 04                 ; 0xc1618
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc161c
    out DX, AL                                ; ee                          ; 0xc161f
    inc cx                                    ; 41                          ; 0xc1620 vgabios.c:1072
    jmp short 01606h                          ; eb e3                       ; 0xc1621
    xor cx, cx                                ; 31 c9                       ; 0xc1623 vgabios.c:1075
    jmp short 0162ch                          ; eb 05                       ; 0xc1625
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc1627
    jnbe short 01644h                         ; 77 18                       ; 0xc162a
    mov al, cl                                ; 88 c8                       ; 0xc162c vgabios.c:1076
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc162e
    out DX, AL                                ; ee                          ; 0xc1631
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1632 vgabios.c:1077
    mov di, si                                ; 89 f7                       ; 0xc1635
    add di, cx                                ; 01 cf                       ; 0xc1637
    mov al, byte [es:di+037h]                 ; 26 8a 45 37                 ; 0xc1639
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc163d
    out DX, AL                                ; ee                          ; 0xc1640
    inc cx                                    ; 41                          ; 0xc1641 vgabios.c:1078
    jmp short 01627h                          ; eb e3                       ; 0xc1642
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1644 vgabios.c:1081
    xor ah, ah                                ; 30 e4                       ; 0xc1647
    mov di, ax                                ; 89 c7                       ; 0xc1649
    sal di, 003h                              ; c1 e7 03                    ; 0xc164b
    cmp byte [di+047b3h], 001h                ; 80 bd b3 47 01              ; 0xc164e
    jne short 0165ah                          ; 75 05                       ; 0xc1653
    mov cx, 003b4h                            ; b9 b4 03                    ; 0xc1655
    jmp short 0165dh                          ; eb 03                       ; 0xc1658
    mov cx, 003d4h                            ; b9 d4 03                    ; 0xc165a
    mov word [bp-016h], cx                    ; 89 4e ea                    ; 0xc165d
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1660 vgabios.c:1084
    mov al, byte [es:si+009h]                 ; 26 8a 44 09                 ; 0xc1663
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc1667
    out DX, AL                                ; ee                          ; 0xc166a
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc166b vgabios.c:1087
    mov dx, cx                                ; 89 ca                       ; 0xc166e
    out DX, ax                                ; ef                          ; 0xc1670
    xor cx, cx                                ; 31 c9                       ; 0xc1671 vgabios.c:1089
    jmp short 0167ah                          ; eb 05                       ; 0xc1673
    cmp cx, strict byte 00018h                ; 83 f9 18                    ; 0xc1675
    jnbe short 01690h                         ; 77 16                       ; 0xc1678
    mov al, cl                                ; 88 c8                       ; 0xc167a vgabios.c:1090
    mov dx, word [bp-016h]                    ; 8b 56 ea                    ; 0xc167c
    out DX, AL                                ; ee                          ; 0xc167f
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1680 vgabios.c:1091
    mov di, si                                ; 89 f7                       ; 0xc1683
    add di, cx                                ; 01 cf                       ; 0xc1685
    inc dx                                    ; 42                          ; 0xc1687
    mov al, byte [es:di+00ah]                 ; 26 8a 45 0a                 ; 0xc1688
    out DX, AL                                ; ee                          ; 0xc168c
    inc cx                                    ; 41                          ; 0xc168d vgabios.c:1092
    jmp short 01675h                          ; eb e5                       ; 0xc168e
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc1690 vgabios.c:1095
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1692
    out DX, AL                                ; ee                          ; 0xc1695
    mov dx, word [bp-016h]                    ; 8b 56 ea                    ; 0xc1696 vgabios.c:1096
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc1699
    in AL, DX                                 ; ec                          ; 0xc169c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc169d
    cmp byte [bp-00eh], 000h                  ; 80 7e f2 00                 ; 0xc169f vgabios.c:1098
    jne short 01704h                          ; 75 5f                       ; 0xc16a3
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc16a5 vgabios.c:1100
    xor ah, ah                                ; 30 e4                       ; 0xc16a8
    mov di, ax                                ; 89 c7                       ; 0xc16aa
    sal di, 003h                              ; c1 e7 03                    ; 0xc16ac
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc16af
    jne short 016c8h                          ; 75 12                       ; 0xc16b4
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc16b6 vgabios.c:1102
    mov cx, 04000h                            ; b9 00 40                    ; 0xc16ba
    mov ax, 00720h                            ; b8 20 07                    ; 0xc16bd
    xor di, di                                ; 31 ff                       ; 0xc16c0
    jcxz 016c6h                               ; e3 02                       ; 0xc16c2
    rep stosw                                 ; f3 ab                       ; 0xc16c4
    jmp short 01704h                          ; eb 3c                       ; 0xc16c6 vgabios.c:1104
    cmp byte [bp-010h], 00dh                  ; 80 7e f0 0d                 ; 0xc16c8 vgabios.c:1106
    jnc short 016dfh                          ; 73 11                       ; 0xc16cc
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc16ce vgabios.c:1108
    mov cx, 04000h                            ; b9 00 40                    ; 0xc16d2
    xor al, al                                ; 30 c0                       ; 0xc16d5
    xor di, di                                ; 31 ff                       ; 0xc16d7
    jcxz 016ddh                               ; e3 02                       ; 0xc16d9
    rep stosw                                 ; f3 ab                       ; 0xc16db
    jmp short 01704h                          ; eb 25                       ; 0xc16dd vgabios.c:1110
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc16df vgabios.c:1112
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc16e1
    out DX, AL                                ; ee                          ; 0xc16e4
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc16e5 vgabios.c:1113
    in AL, DX                                 ; ec                          ; 0xc16e8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc16e9
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc16eb
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc16ee vgabios.c:1114
    out DX, AL                                ; ee                          ; 0xc16f0
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc16f1 vgabios.c:1115
    mov cx, 08000h                            ; b9 00 80                    ; 0xc16f5
    xor ax, ax                                ; 31 c0                       ; 0xc16f8
    xor di, di                                ; 31 ff                       ; 0xc16fa
    jcxz 01700h                               ; e3 02                       ; 0xc16fc
    rep stosw                                 ; f3 ab                       ; 0xc16fe
    mov al, byte [bp-020h]                    ; 8a 46 e0                    ; 0xc1700 vgabios.c:1116
    out DX, AL                                ; ee                          ; 0xc1703
    mov di, strict word 00049h                ; bf 49 00                    ; 0xc1704 vgabios.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1707
    mov es, ax                                ; 8e c0                       ; 0xc170a
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc170c
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc170f
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1712 vgabios.c:1123
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc1715
    xor ah, ah                                ; 30 e4                       ; 0xc1718
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc171a vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc171d
    mov es, dx                                ; 8e c2                       ; 0xc1720
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc1722
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1725 vgabios.c:60
    mov ax, word [es:si+003h]                 ; 26 8b 44 03                 ; 0xc1728
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc172c vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc172f
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc1731
    mov di, strict word 00063h                ; bf 63 00                    ; 0xc1734 vgabios.c:62
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc1737
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc173a
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc173d vgabios.c:50
    mov al, byte [es:si+001h]                 ; 26 8a 44 01                 ; 0xc1740
    mov di, 00084h                            ; bf 84 00                    ; 0xc1744 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc1747
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1749
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc174c vgabios.c:1127
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc174f
    xor ah, ah                                ; 30 e4                       ; 0xc1753
    mov di, 00085h                            ; bf 85 00                    ; 0xc1755 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc1758
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc175a
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc175d vgabios.c:1128
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc1760
    mov di, 00087h                            ; bf 87 00                    ; 0xc1762 vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1765
    mov di, 00088h                            ; bf 88 00                    ; 0xc1768 vgabios.c:52
    mov byte [es:di], 0f9h                    ; 26 c6 05 f9                 ; 0xc176b
    mov di, 0008ah                            ; bf 8a 00                    ; 0xc176f vgabios.c:52
    mov byte [es:di], 008h                    ; 26 c6 05 08                 ; 0xc1772
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1776 vgabios.c:1134
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc1779
    jnbe short 017a2h                         ; 77 25                       ; 0xc177b
    mov di, ax                                ; 89 c7                       ; 0xc177d vgabios.c:1136
    mov al, byte [di+07de0h]                  ; 8a 85 e0 7d                 ; 0xc177f
    mov di, strict word 00065h                ; bf 65 00                    ; 0xc1783 vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1786
    cmp byte [bp-010h], 006h                  ; 80 7e f0 06                 ; 0xc1789 vgabios.c:1137
    jne short 01794h                          ; 75 05                       ; 0xc178d
    mov ax, strict word 0003fh                ; b8 3f 00                    ; 0xc178f
    jmp short 01797h                          ; eb 03                       ; 0xc1792
    mov ax, strict word 00030h                ; b8 30 00                    ; 0xc1794
    mov di, strict word 00066h                ; bf 66 00                    ; 0xc1797 vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc179a
    mov es, dx                                ; 8e c2                       ; 0xc179d
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc179f
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc17a2 vgabios.c:1141
    xor ah, ah                                ; 30 e4                       ; 0xc17a5
    mov di, ax                                ; 89 c7                       ; 0xc17a7
    sal di, 003h                              ; c1 e7 03                    ; 0xc17a9
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc17ac
    jne short 017bch                          ; 75 09                       ; 0xc17b1
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc17b3 vgabios.c:1143
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc17b6
    call 0118ch                               ; e8 d0 f9                    ; 0xc17b9
    xor cx, cx                                ; 31 c9                       ; 0xc17bc vgabios.c:1148
    jmp short 017c5h                          ; eb 05                       ; 0xc17be
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc17c0
    jnc short 017d1h                          ; 73 0c                       ; 0xc17c3
    mov al, cl                                ; 88 c8                       ; 0xc17c5 vgabios.c:1149
    xor ah, ah                                ; 30 e4                       ; 0xc17c7
    xor dx, dx                                ; 31 d2                       ; 0xc17c9
    call 01293h                               ; e8 c5 fa                    ; 0xc17cb
    inc cx                                    ; 41                          ; 0xc17ce
    jmp short 017c0h                          ; eb ef                       ; 0xc17cf
    xor ax, ax                                ; 31 c0                       ; 0xc17d1 vgabios.c:1152
    call 01322h                               ; e8 4c fb                    ; 0xc17d3
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc17d6 vgabios.c:1155
    xor ah, ah                                ; 30 e4                       ; 0xc17d9
    mov di, ax                                ; 89 c7                       ; 0xc17db
    sal di, 003h                              ; c1 e7 03                    ; 0xc17dd
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc17e0
    jne short 01851h                          ; 75 6a                       ; 0xc17e5
    mov es, [bp-018h]                         ; 8e 46 e8                    ; 0xc17e7 vgabios.c:1157
    mov di, word [es:bx+008h]                 ; 26 8b 7f 08                 ; 0xc17ea
    mov ax, word [es:bx+00ah]                 ; 26 8b 47 0a                 ; 0xc17ee
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc17f2
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc17f5 vgabios.c:1159
    mov bl, byte [es:si+002h]                 ; 26 8a 5c 02                 ; 0xc17f8
    cmp bl, 00eh                              ; 80 fb 0e                    ; 0xc17fc
    je short 01824h                           ; 74 23                       ; 0xc17ff
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc1801
    jne short 01854h                          ; 75 4e                       ; 0xc1804
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc1806 vgabios.c:1161
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc1809
    xor ah, ah                                ; 30 e4                       ; 0xc180d
    push ax                                   ; 50                          ; 0xc180f
    push strict byte 00000h                   ; 6a 00                       ; 0xc1810
    push strict byte 00000h                   ; 6a 00                       ; 0xc1812
    mov cx, 00100h                            ; b9 00 01                    ; 0xc1814
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc1817
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc181a
    xor al, al                                ; 30 c0                       ; 0xc181d
    call 02e1ah                               ; e8 f8 15                    ; 0xc181f
    jmp short 01879h                          ; eb 55                       ; 0xc1822 vgabios.c:1162
    mov al, bl                                ; 88 d8                       ; 0xc1824 vgabios.c:1164
    xor ah, ah                                ; 30 e4                       ; 0xc1826
    push ax                                   ; 50                          ; 0xc1828
    push strict byte 00000h                   ; 6a 00                       ; 0xc1829
    push strict byte 00000h                   ; 6a 00                       ; 0xc182b
    mov cx, 00100h                            ; b9 00 01                    ; 0xc182d
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc1830
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc1833
    xor al, al                                ; 30 c0                       ; 0xc1836
    call 02e1ah                               ; e8 df 15                    ; 0xc1838
    cmp byte [bp-010h], 007h                  ; 80 7e f0 07                 ; 0xc183b vgabios.c:1165
    jne short 01879h                          ; 75 38                       ; 0xc183f
    mov cx, strict word 0000eh                ; b9 0e 00                    ; 0xc1841 vgabios.c:1166
    xor bx, bx                                ; 31 db                       ; 0xc1844
    mov dx, 07b6fh                            ; ba 6f 7b                    ; 0xc1846
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc1849
    call 02da5h                               ; e8 56 15                    ; 0xc184c
    jmp short 01879h                          ; eb 28                       ; 0xc184f vgabios.c:1167
    jmp near 018d5h                           ; e9 81 00                    ; 0xc1851
    mov al, bl                                ; 88 d8                       ; 0xc1854 vgabios.c:1169
    xor ah, ah                                ; 30 e4                       ; 0xc1856
    push ax                                   ; 50                          ; 0xc1858
    push strict byte 00000h                   ; 6a 00                       ; 0xc1859
    push strict byte 00000h                   ; 6a 00                       ; 0xc185b
    mov cx, 00100h                            ; b9 00 01                    ; 0xc185d
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc1860
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc1863
    xor al, al                                ; 30 c0                       ; 0xc1866
    call 02e1ah                               ; e8 af 15                    ; 0xc1868
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc186b vgabios.c:1170
    xor bx, bx                                ; 31 db                       ; 0xc186e
    mov dx, 07c9ch                            ; ba 9c 7c                    ; 0xc1870
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc1873
    call 02da5h                               ; e8 2c 15                    ; 0xc1876
    cmp word [bp-01ch], strict byte 00000h    ; 83 7e e4 00                 ; 0xc1879 vgabios.c:1172
    jne short 01883h                          ; 75 04                       ; 0xc187d
    test di, di                               ; 85 ff                       ; 0xc187f
    je short 018cdh                           ; 74 4a                       ; 0xc1881
    xor cx, cx                                ; 31 c9                       ; 0xc1883 vgabios.c:1177
    mov es, [bp-01ch]                         ; 8e 46 e4                    ; 0xc1885 vgabios.c:1179
    mov bx, di                                ; 89 fb                       ; 0xc1888
    add bx, cx                                ; 01 cb                       ; 0xc188a
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc188c
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1890
    je short 0189ch                           ; 74 08                       ; 0xc1892
    cmp al, byte [bp-010h]                    ; 3a 46 f0                    ; 0xc1894 vgabios.c:1181
    je short 0189ch                           ; 74 03                       ; 0xc1897
    inc cx                                    ; 41                          ; 0xc1899 vgabios.c:1183
    jmp short 01885h                          ; eb e9                       ; 0xc189a vgabios.c:1184
    mov es, [bp-01ch]                         ; 8e 46 e4                    ; 0xc189c vgabios.c:1186
    mov bx, di                                ; 89 fb                       ; 0xc189f
    add bx, cx                                ; 01 cb                       ; 0xc18a1
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc18a3
    cmp al, byte [bp-010h]                    ; 3a 46 f0                    ; 0xc18a7
    jne short 018cdh                          ; 75 21                       ; 0xc18aa
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc18ac vgabios.c:1191
    xor ah, ah                                ; 30 e4                       ; 0xc18af
    push ax                                   ; 50                          ; 0xc18b1
    mov al, byte [es:di+001h]                 ; 26 8a 45 01                 ; 0xc18b2
    push ax                                   ; 50                          ; 0xc18b6
    push word [es:di+004h]                    ; 26 ff 75 04                 ; 0xc18b7
    mov cx, word [es:di+002h]                 ; 26 8b 4d 02                 ; 0xc18bb
    mov bx, word [es:di+006h]                 ; 26 8b 5d 06                 ; 0xc18bf
    mov dx, word [es:di+008h]                 ; 26 8b 55 08                 ; 0xc18c3
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc18c7
    call 02e1ah                               ; e8 4d 15                    ; 0xc18ca
    xor bl, bl                                ; 30 db                       ; 0xc18cd vgabios.c:1195
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc18cf
    mov AH, strict byte 011h                  ; b4 11                       ; 0xc18d1
    int 06dh                                  ; cd 6d                       ; 0xc18d3
    mov bx, 0596fh                            ; bb 6f 59                    ; 0xc18d5 vgabios.c:1199
    mov cx, ds                                ; 8c d9                       ; 0xc18d8
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc18da
    call 009f0h                               ; e8 10 f1                    ; 0xc18dd
    mov es, [bp-01eh]                         ; 8e 46 e2                    ; 0xc18e0 vgabios.c:1201
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc18e3
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc18e7
    je short 01905h                           ; 74 1a                       ; 0xc18e9
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc18eb
    je short 01900h                           ; 74 11                       ; 0xc18ed
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc18ef
    jne short 0190ah                          ; 75 17                       ; 0xc18f1
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc18f3 vgabios.c:1203
    mov cx, ds                                ; 8c d9                       ; 0xc18f6
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc18f8
    call 009f0h                               ; e8 f2 f0                    ; 0xc18fb
    jmp short 0190ah                          ; eb 0a                       ; 0xc18fe vgabios.c:1204
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc1900 vgabios.c:1206
    jmp short 018f6h                          ; eb f1                       ; 0xc1903
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc1905 vgabios.c:1209
    jmp short 018f6h                          ; eb ec                       ; 0xc1908
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc190a vgabios.c:1212
    pop di                                    ; 5f                          ; 0xc190d
    pop si                                    ; 5e                          ; 0xc190e
    pop dx                                    ; 5a                          ; 0xc190f
    pop cx                                    ; 59                          ; 0xc1910
    pop bx                                    ; 5b                          ; 0xc1911
    pop bp                                    ; 5d                          ; 0xc1912
    retn                                      ; c3                          ; 0xc1913
  ; disGetNextSymbol 0xc1914 LB 0x2c1e -> off=0x0 cb=000000000000008e uValue=00000000000c1914 'vgamem_copy_pl4'
vgamem_copy_pl4:                             ; 0xc1914 LB 0x8e
    push bp                                   ; 55                          ; 0xc1914 vgabios.c:1215
    mov bp, sp                                ; 89 e5                       ; 0xc1915
    push si                                   ; 56                          ; 0xc1917
    push di                                   ; 57                          ; 0xc1918
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1919
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc191c
    mov al, dl                                ; 88 d0                       ; 0xc191f
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1921
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc1924
    xor ah, ah                                ; 30 e4                       ; 0xc1927 vgabios.c:1221
    mov dl, byte [bp+006h]                    ; 8a 56 06                    ; 0xc1929
    xor dh, dh                                ; 30 f6                       ; 0xc192c
    mov cx, dx                                ; 89 d1                       ; 0xc192e
    imul dx                                   ; f7 ea                       ; 0xc1930
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc1932
    xor dh, dh                                ; 30 f6                       ; 0xc1935
    mov si, dx                                ; 89 d6                       ; 0xc1937
    imul dx                                   ; f7 ea                       ; 0xc1939
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc193b
    xor dh, dh                                ; 30 f6                       ; 0xc193e
    mov bx, dx                                ; 89 d3                       ; 0xc1940
    add ax, dx                                ; 01 d0                       ; 0xc1942
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc1944
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1947 vgabios.c:1222
    xor ah, ah                                ; 30 e4                       ; 0xc194a
    imul cx                                   ; f7 e9                       ; 0xc194c
    imul si                                   ; f7 ee                       ; 0xc194e
    add ax, bx                                ; 01 d8                       ; 0xc1950
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc1952
    mov ax, 00105h                            ; b8 05 01                    ; 0xc1955 vgabios.c:1223
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1958
    out DX, ax                                ; ef                          ; 0xc195b
    xor bl, bl                                ; 30 db                       ; 0xc195c vgabios.c:1224
    cmp bl, byte [bp+006h]                    ; 3a 5e 06                    ; 0xc195e
    jnc short 01992h                          ; 73 2f                       ; 0xc1961
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1963 vgabios.c:1226
    xor ah, ah                                ; 30 e4                       ; 0xc1966
    mov cx, ax                                ; 89 c1                       ; 0xc1968
    mov al, bl                                ; 88 d8                       ; 0xc196a
    mov dx, ax                                ; 89 c2                       ; 0xc196c
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc196e
    mov si, ax                                ; 89 c6                       ; 0xc1971
    mov ax, dx                                ; 89 d0                       ; 0xc1973
    imul si                                   ; f7 ee                       ; 0xc1975
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc1977
    add si, ax                                ; 01 c6                       ; 0xc197a
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc197c
    add di, ax                                ; 01 c7                       ; 0xc197f
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1981
    mov es, dx                                ; 8e c2                       ; 0xc1984
    jcxz 0198eh                               ; e3 06                       ; 0xc1986
    push DS                                   ; 1e                          ; 0xc1988
    mov ds, dx                                ; 8e da                       ; 0xc1989
    rep movsb                                 ; f3 a4                       ; 0xc198b
    pop DS                                    ; 1f                          ; 0xc198d
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc198e vgabios.c:1227
    jmp short 0195eh                          ; eb cc                       ; 0xc1990
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1992 vgabios.c:1228
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1995
    out DX, ax                                ; ef                          ; 0xc1998
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1999 vgabios.c:1229
    pop di                                    ; 5f                          ; 0xc199c
    pop si                                    ; 5e                          ; 0xc199d
    pop bp                                    ; 5d                          ; 0xc199e
    retn 00004h                               ; c2 04 00                    ; 0xc199f
  ; disGetNextSymbol 0xc19a2 LB 0x2b90 -> off=0x0 cb=000000000000007b uValue=00000000000c19a2 'vgamem_fill_pl4'
vgamem_fill_pl4:                             ; 0xc19a2 LB 0x7b
    push bp                                   ; 55                          ; 0xc19a2 vgabios.c:1232
    mov bp, sp                                ; 89 e5                       ; 0xc19a3
    push si                                   ; 56                          ; 0xc19a5
    push di                                   ; 57                          ; 0xc19a6
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc19a7
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc19aa
    mov al, dl                                ; 88 d0                       ; 0xc19ad
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc19af
    mov bh, cl                                ; 88 cf                       ; 0xc19b2
    xor ah, ah                                ; 30 e4                       ; 0xc19b4 vgabios.c:1238
    mov dx, ax                                ; 89 c2                       ; 0xc19b6
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc19b8
    mov cx, ax                                ; 89 c1                       ; 0xc19bb
    mov ax, dx                                ; 89 d0                       ; 0xc19bd
    imul cx                                   ; f7 e9                       ; 0xc19bf
    mov dl, bh                                ; 88 fa                       ; 0xc19c1
    xor dh, dh                                ; 30 f6                       ; 0xc19c3
    imul dx                                   ; f7 ea                       ; 0xc19c5
    mov dx, ax                                ; 89 c2                       ; 0xc19c7
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc19c9
    xor ah, ah                                ; 30 e4                       ; 0xc19cc
    add dx, ax                                ; 01 c2                       ; 0xc19ce
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc19d0
    mov ax, 00205h                            ; b8 05 02                    ; 0xc19d3 vgabios.c:1239
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc19d6
    out DX, ax                                ; ef                          ; 0xc19d9
    xor bl, bl                                ; 30 db                       ; 0xc19da vgabios.c:1240
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc19dc
    jnc short 01a0dh                          ; 73 2c                       ; 0xc19df
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc19e1 vgabios.c:1242
    xor ch, ch                                ; 30 ed                       ; 0xc19e4
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc19e6
    xor ah, ah                                ; 30 e4                       ; 0xc19e9
    mov si, ax                                ; 89 c6                       ; 0xc19eb
    mov al, bl                                ; 88 d8                       ; 0xc19ed
    mov dx, ax                                ; 89 c2                       ; 0xc19ef
    mov al, bh                                ; 88 f8                       ; 0xc19f1
    mov di, ax                                ; 89 c7                       ; 0xc19f3
    mov ax, dx                                ; 89 d0                       ; 0xc19f5
    imul di                                   ; f7 ef                       ; 0xc19f7
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc19f9
    add di, ax                                ; 01 c7                       ; 0xc19fc
    mov ax, si                                ; 89 f0                       ; 0xc19fe
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1a00
    mov es, dx                                ; 8e c2                       ; 0xc1a03
    jcxz 01a09h                               ; e3 02                       ; 0xc1a05
    rep stosb                                 ; f3 aa                       ; 0xc1a07
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc1a09 vgabios.c:1243
    jmp short 019dch                          ; eb cf                       ; 0xc1a0b
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1a0d vgabios.c:1244
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1a10
    out DX, ax                                ; ef                          ; 0xc1a13
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1a14 vgabios.c:1245
    pop di                                    ; 5f                          ; 0xc1a17
    pop si                                    ; 5e                          ; 0xc1a18
    pop bp                                    ; 5d                          ; 0xc1a19
    retn 00004h                               ; c2 04 00                    ; 0xc1a1a
  ; disGetNextSymbol 0xc1a1d LB 0x2b15 -> off=0x0 cb=00000000000000b6 uValue=00000000000c1a1d 'vgamem_copy_cga'
vgamem_copy_cga:                             ; 0xc1a1d LB 0xb6
    push bp                                   ; 55                          ; 0xc1a1d vgabios.c:1248
    mov bp, sp                                ; 89 e5                       ; 0xc1a1e
    push si                                   ; 56                          ; 0xc1a20
    push di                                   ; 57                          ; 0xc1a21
    sub sp, strict byte 0000eh                ; 83 ec 0e                    ; 0xc1a22
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc1a25
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc1a28
    mov byte [bp-00ah], cl                    ; 88 4e f6                    ; 0xc1a2b
    mov al, dl                                ; 88 d0                       ; 0xc1a2e vgabios.c:1254
    xor ah, ah                                ; 30 e4                       ; 0xc1a30
    mov bx, ax                                ; 89 c3                       ; 0xc1a32
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1a34
    mov si, ax                                ; 89 c6                       ; 0xc1a37
    mov ax, bx                                ; 89 d8                       ; 0xc1a39
    imul si                                   ; f7 ee                       ; 0xc1a3b
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc1a3d
    mov di, bx                                ; 89 df                       ; 0xc1a40
    imul bx                                   ; f7 eb                       ; 0xc1a42
    mov dx, ax                                ; 89 c2                       ; 0xc1a44
    sar dx, 1                                 ; d1 fa                       ; 0xc1a46
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1a48
    xor ah, ah                                ; 30 e4                       ; 0xc1a4b
    mov bx, ax                                ; 89 c3                       ; 0xc1a4d
    add dx, ax                                ; 01 c2                       ; 0xc1a4f
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1a51
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1a54 vgabios.c:1255
    imul si                                   ; f7 ee                       ; 0xc1a57
    imul di                                   ; f7 ef                       ; 0xc1a59
    sar ax, 1                                 ; d1 f8                       ; 0xc1a5b
    add ax, bx                                ; 01 d8                       ; 0xc1a5d
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc1a5f
    mov byte [bp-006h], bh                    ; 88 7e fa                    ; 0xc1a62 vgabios.c:1256
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1a65
    xor ah, ah                                ; 30 e4                       ; 0xc1a68
    cwd                                       ; 99                          ; 0xc1a6a
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1a6b
    sar ax, 1                                 ; d1 f8                       ; 0xc1a6d
    mov bx, ax                                ; 89 c3                       ; 0xc1a6f
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1a71
    xor ah, ah                                ; 30 e4                       ; 0xc1a74
    cmp ax, bx                                ; 39 d8                       ; 0xc1a76
    jnl short 01acah                          ; 7d 50                       ; 0xc1a78
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc1a7a vgabios.c:1258
    xor bh, bh                                ; 30 ff                       ; 0xc1a7d
    mov word [bp-012h], bx                    ; 89 5e ee                    ; 0xc1a7f
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc1a82
    imul bx                                   ; f7 eb                       ; 0xc1a85
    mov bx, ax                                ; 89 c3                       ; 0xc1a87
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc1a89
    add si, ax                                ; 01 c6                       ; 0xc1a8c
    mov di, word [bp-010h]                    ; 8b 7e f0                    ; 0xc1a8e
    add di, ax                                ; 01 c7                       ; 0xc1a91
    mov cx, word [bp-012h]                    ; 8b 4e ee                    ; 0xc1a93
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1a96
    mov es, dx                                ; 8e c2                       ; 0xc1a99
    jcxz 01aa3h                               ; e3 06                       ; 0xc1a9b
    push DS                                   ; 1e                          ; 0xc1a9d
    mov ds, dx                                ; 8e da                       ; 0xc1a9e
    rep movsb                                 ; f3 a4                       ; 0xc1aa0
    pop DS                                    ; 1f                          ; 0xc1aa2
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc1aa3 vgabios.c:1259
    add si, 02000h                            ; 81 c6 00 20                 ; 0xc1aa6
    add si, bx                                ; 01 de                       ; 0xc1aaa
    mov di, word [bp-010h]                    ; 8b 7e f0                    ; 0xc1aac
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1aaf
    add di, bx                                ; 01 df                       ; 0xc1ab3
    mov cx, word [bp-012h]                    ; 8b 4e ee                    ; 0xc1ab5
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1ab8
    mov es, dx                                ; 8e c2                       ; 0xc1abb
    jcxz 01ac5h                               ; e3 06                       ; 0xc1abd
    push DS                                   ; 1e                          ; 0xc1abf
    mov ds, dx                                ; 8e da                       ; 0xc1ac0
    rep movsb                                 ; f3 a4                       ; 0xc1ac2
    pop DS                                    ; 1f                          ; 0xc1ac4
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1ac5 vgabios.c:1260
    jmp short 01a65h                          ; eb 9b                       ; 0xc1ac8
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1aca vgabios.c:1261
    pop di                                    ; 5f                          ; 0xc1acd
    pop si                                    ; 5e                          ; 0xc1ace
    pop bp                                    ; 5d                          ; 0xc1acf
    retn 00004h                               ; c2 04 00                    ; 0xc1ad0
  ; disGetNextSymbol 0xc1ad3 LB 0x2a5f -> off=0x0 cb=0000000000000094 uValue=00000000000c1ad3 'vgamem_fill_cga'
vgamem_fill_cga:                             ; 0xc1ad3 LB 0x94
    push bp                                   ; 55                          ; 0xc1ad3 vgabios.c:1264
    mov bp, sp                                ; 89 e5                       ; 0xc1ad4
    push si                                   ; 56                          ; 0xc1ad6
    push di                                   ; 57                          ; 0xc1ad7
    sub sp, strict byte 0000ch                ; 83 ec 0c                    ; 0xc1ad8
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1adb
    mov al, dl                                ; 88 d0                       ; 0xc1ade
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1ae0
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1ae3
    xor ah, ah                                ; 30 e4                       ; 0xc1ae6 vgabios.c:1270
    mov dx, ax                                ; 89 c2                       ; 0xc1ae8
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1aea
    mov bx, ax                                ; 89 c3                       ; 0xc1aed
    mov ax, dx                                ; 89 d0                       ; 0xc1aef
    imul bx                                   ; f7 eb                       ; 0xc1af1
    mov dl, cl                                ; 88 ca                       ; 0xc1af3
    xor dh, dh                                ; 30 f6                       ; 0xc1af5
    imul dx                                   ; f7 ea                       ; 0xc1af7
    mov dx, ax                                ; 89 c2                       ; 0xc1af9
    sar dx, 1                                 ; d1 fa                       ; 0xc1afb
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1afd
    xor ah, ah                                ; 30 e4                       ; 0xc1b00
    add dx, ax                                ; 01 c2                       ; 0xc1b02
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1b04
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1b07 vgabios.c:1271
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1b0a
    xor ah, ah                                ; 30 e4                       ; 0xc1b0d
    cwd                                       ; 99                          ; 0xc1b0f
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1b10
    sar ax, 1                                 ; d1 f8                       ; 0xc1b12
    mov dx, ax                                ; 89 c2                       ; 0xc1b14
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1b16
    xor ah, ah                                ; 30 e4                       ; 0xc1b19
    cmp ax, dx                                ; 39 d0                       ; 0xc1b1b
    jnl short 01b5eh                          ; 7d 3f                       ; 0xc1b1d
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc1b1f vgabios.c:1273
    xor bh, bh                                ; 30 ff                       ; 0xc1b22
    mov dl, byte [bp+006h]                    ; 8a 56 06                    ; 0xc1b24
    xor dh, dh                                ; 30 f6                       ; 0xc1b27
    mov si, dx                                ; 89 d6                       ; 0xc1b29
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1b2b
    imul dx                                   ; f7 ea                       ; 0xc1b2e
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc1b30
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1b33
    add di, ax                                ; 01 c7                       ; 0xc1b36
    mov cx, bx                                ; 89 d9                       ; 0xc1b38
    mov ax, si                                ; 89 f0                       ; 0xc1b3a
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1b3c
    mov es, dx                                ; 8e c2                       ; 0xc1b3f
    jcxz 01b45h                               ; e3 02                       ; 0xc1b41
    rep stosb                                 ; f3 aa                       ; 0xc1b43
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1b45 vgabios.c:1274
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1b48
    add di, word [bp-010h]                    ; 03 7e f0                    ; 0xc1b4c
    mov cx, bx                                ; 89 d9                       ; 0xc1b4f
    mov ax, si                                ; 89 f0                       ; 0xc1b51
    mov es, dx                                ; 8e c2                       ; 0xc1b53
    jcxz 01b59h                               ; e3 02                       ; 0xc1b55
    rep stosb                                 ; f3 aa                       ; 0xc1b57
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1b59 vgabios.c:1275
    jmp short 01b0ah                          ; eb ac                       ; 0xc1b5c
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1b5e vgabios.c:1276
    pop di                                    ; 5f                          ; 0xc1b61
    pop si                                    ; 5e                          ; 0xc1b62
    pop bp                                    ; 5d                          ; 0xc1b63
    retn 00004h                               ; c2 04 00                    ; 0xc1b64
  ; disGetNextSymbol 0xc1b67 LB 0x29cb -> off=0x0 cb=0000000000000081 uValue=00000000000c1b67 'vgamem_copy_linear'
vgamem_copy_linear:                          ; 0xc1b67 LB 0x81
    push bp                                   ; 55                          ; 0xc1b67 vgabios.c:1279
    mov bp, sp                                ; 89 e5                       ; 0xc1b68
    push si                                   ; 56                          ; 0xc1b6a
    push di                                   ; 57                          ; 0xc1b6b
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1b6c
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc1b6f
    mov al, dl                                ; 88 d0                       ; 0xc1b72
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1b74
    mov bx, cx                                ; 89 cb                       ; 0xc1b77
    xor ah, ah                                ; 30 e4                       ; 0xc1b79 vgabios.c:1285
    mov si, ax                                ; 89 c6                       ; 0xc1b7b
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1b7d
    mov di, ax                                ; 89 c7                       ; 0xc1b80
    mov ax, si                                ; 89 f0                       ; 0xc1b82
    imul di                                   ; f7 ef                       ; 0xc1b84
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1b86
    mov si, ax                                ; 89 c6                       ; 0xc1b89
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1b8b
    xor ah, ah                                ; 30 e4                       ; 0xc1b8e
    mov cx, ax                                ; 89 c1                       ; 0xc1b90
    add si, ax                                ; 01 c6                       ; 0xc1b92
    sal si, 003h                              ; c1 e6 03                    ; 0xc1b94
    mov word [bp-00ch], si                    ; 89 76 f4                    ; 0xc1b97
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1b9a vgabios.c:1286
    imul di                                   ; f7 ef                       ; 0xc1b9d
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1b9f
    add ax, cx                                ; 01 c8                       ; 0xc1ba2
    sal ax, 003h                              ; c1 e0 03                    ; 0xc1ba4
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc1ba7
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1baa vgabios.c:1287
    sal word [bp+004h], 003h                  ; c1 66 04 03                 ; 0xc1bad vgabios.c:1288
    mov byte [bp-006h], ch                    ; 88 6e fa                    ; 0xc1bb1 vgabios.c:1289
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1bb4
    cmp al, byte [bp+006h]                    ; 3a 46 06                    ; 0xc1bb7
    jnc short 01bdfh                          ; 73 23                       ; 0xc1bba
    xor ah, ah                                ; 30 e4                       ; 0xc1bbc vgabios.c:1291
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1bbe
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc1bc1
    add si, ax                                ; 01 c6                       ; 0xc1bc4
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1bc6
    add di, ax                                ; 01 c7                       ; 0xc1bc9
    mov cx, bx                                ; 89 d9                       ; 0xc1bcb
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1bcd
    mov es, dx                                ; 8e c2                       ; 0xc1bd0
    jcxz 01bdah                               ; e3 06                       ; 0xc1bd2
    push DS                                   ; 1e                          ; 0xc1bd4
    mov ds, dx                                ; 8e da                       ; 0xc1bd5
    rep movsb                                 ; f3 a4                       ; 0xc1bd7
    pop DS                                    ; 1f                          ; 0xc1bd9
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1bda vgabios.c:1292
    jmp short 01bb4h                          ; eb d5                       ; 0xc1bdd
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1bdf vgabios.c:1293
    pop di                                    ; 5f                          ; 0xc1be2
    pop si                                    ; 5e                          ; 0xc1be3
    pop bp                                    ; 5d                          ; 0xc1be4
    retn 00004h                               ; c2 04 00                    ; 0xc1be5
  ; disGetNextSymbol 0xc1be8 LB 0x294a -> off=0x0 cb=000000000000006d uValue=00000000000c1be8 'vgamem_fill_linear'
vgamem_fill_linear:                          ; 0xc1be8 LB 0x6d
    push bp                                   ; 55                          ; 0xc1be8 vgabios.c:1296
    mov bp, sp                                ; 89 e5                       ; 0xc1be9
    push si                                   ; 56                          ; 0xc1beb
    push di                                   ; 57                          ; 0xc1bec
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc1bed
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1bf0
    mov al, dl                                ; 88 d0                       ; 0xc1bf3
    mov si, cx                                ; 89 ce                       ; 0xc1bf5
    xor ah, ah                                ; 30 e4                       ; 0xc1bf7 vgabios.c:1302
    mov dx, ax                                ; 89 c2                       ; 0xc1bf9
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1bfb
    mov di, ax                                ; 89 c7                       ; 0xc1bfe
    mov ax, dx                                ; 89 d0                       ; 0xc1c00
    imul di                                   ; f7 ef                       ; 0xc1c02
    mul cx                                    ; f7 e1                       ; 0xc1c04
    mov dx, ax                                ; 89 c2                       ; 0xc1c06
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1c08
    xor ah, ah                                ; 30 e4                       ; 0xc1c0b
    add ax, dx                                ; 01 d0                       ; 0xc1c0d
    sal ax, 003h                              ; c1 e0 03                    ; 0xc1c0f
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc1c12
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1c15 vgabios.c:1303
    sal si, 003h                              ; c1 e6 03                    ; 0xc1c18 vgabios.c:1304
    mov byte [bp-008h], 000h                  ; c6 46 f8 00                 ; 0xc1c1b vgabios.c:1305
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1c1f
    cmp al, byte [bp+004h]                    ; 3a 46 04                    ; 0xc1c22
    jnc short 01c4ch                          ; 73 25                       ; 0xc1c25
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1c27 vgabios.c:1307
    xor ah, ah                                ; 30 e4                       ; 0xc1c2a
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc1c2c
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1c2f
    mul si                                    ; f7 e6                       ; 0xc1c32
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc1c34
    add di, ax                                ; 01 c7                       ; 0xc1c37
    mov cx, bx                                ; 89 d9                       ; 0xc1c39
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc1c3b
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1c3e
    mov es, dx                                ; 8e c2                       ; 0xc1c41
    jcxz 01c47h                               ; e3 02                       ; 0xc1c43
    rep stosb                                 ; f3 aa                       ; 0xc1c45
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc1c47 vgabios.c:1308
    jmp short 01c1fh                          ; eb d3                       ; 0xc1c4a
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1c4c vgabios.c:1309
    pop di                                    ; 5f                          ; 0xc1c4f
    pop si                                    ; 5e                          ; 0xc1c50
    pop bp                                    ; 5d                          ; 0xc1c51
    retn 00004h                               ; c2 04 00                    ; 0xc1c52
  ; disGetNextSymbol 0xc1c55 LB 0x28dd -> off=0x0 cb=0000000000000688 uValue=00000000000c1c55 'biosfn_scroll'
biosfn_scroll:                               ; 0xc1c55 LB 0x688
    push bp                                   ; 55                          ; 0xc1c55 vgabios.c:1312
    mov bp, sp                                ; 89 e5                       ; 0xc1c56
    push si                                   ; 56                          ; 0xc1c58
    push di                                   ; 57                          ; 0xc1c59
    sub sp, strict byte 0001eh                ; 83 ec 1e                    ; 0xc1c5a
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1c5d
    mov byte [bp-010h], dl                    ; 88 56 f0                    ; 0xc1c60
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1c63
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1c66
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc1c69 vgabios.c:1321
    jnbe short 01c8ah                         ; 77 1c                       ; 0xc1c6c
    cmp cl, byte [bp+006h]                    ; 3a 4e 06                    ; 0xc1c6e vgabios.c:1322
    jnbe short 01c8ah                         ; 77 17                       ; 0xc1c71
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1c73 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1c76
    mov es, ax                                ; 8e c0                       ; 0xc1c79
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1c7b
    xor ah, ah                                ; 30 e4                       ; 0xc1c7e vgabios.c:1326
    call 03819h                               ; e8 96 1b                    ; 0xc1c80
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc1c83
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1c86 vgabios.c:1327
    jne short 01c8dh                          ; 75 03                       ; 0xc1c88
    jmp near 022d4h                           ; e9 47 06                    ; 0xc1c8a
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1c8d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1c90
    mov es, ax                                ; 8e c0                       ; 0xc1c93
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1c95
    xor ah, ah                                ; 30 e4                       ; 0xc1c98 vgabios.c:48
    inc ax                                    ; 40                          ; 0xc1c9a
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc1c9b
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1c9e vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc1ca1
    mov word [bp-01eh], ax                    ; 89 46 e2                    ; 0xc1ca4 vgabios.c:58
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc1ca7 vgabios.c:1334
    jne short 01cb6h                          ; 75 09                       ; 0xc1cab
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc1cad vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1cb0
    mov byte [bp+008h], al                    ; 88 46 08                    ; 0xc1cb3 vgabios.c:48
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1cb6 vgabios.c:1337
    xor ah, ah                                ; 30 e4                       ; 0xc1cb9
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1cbb
    jc short 01cc8h                           ; 72 08                       ; 0xc1cbe
    mov al, byte [bp-016h]                    ; 8a 46 ea                    ; 0xc1cc0
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc1cc3
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc1cc5
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1cc8 vgabios.c:1338
    xor ah, ah                                ; 30 e4                       ; 0xc1ccb
    cmp ax, word [bp-01eh]                    ; 3b 46 e2                    ; 0xc1ccd
    jc short 01cdah                           ; 72 08                       ; 0xc1cd0
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc1cd2
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc1cd5
    mov byte [bp+006h], al                    ; 88 46 06                    ; 0xc1cd7
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1cda vgabios.c:1339
    xor ah, ah                                ; 30 e4                       ; 0xc1cdd
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1cdf
    jbe short 01ce7h                          ; 76 03                       ; 0xc1ce2
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1ce4
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1ce7 vgabios.c:1340
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc1cea
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc1ced
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1cef
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1cf2 vgabios.c:1342
    mov byte [bp-01ah], al                    ; 88 46 e6                    ; 0xc1cf5
    mov byte [bp-019h], 000h                  ; c6 46 e7 00                 ; 0xc1cf8
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1cfc
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1cff
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc1d02
    dec ax                                    ; 48                          ; 0xc1d05
    mov word [bp-022h], ax                    ; 89 46 de                    ; 0xc1d06
    mov di, word [bp-016h]                    ; 8b 7e ea                    ; 0xc1d09
    dec di                                    ; 4f                          ; 0xc1d0c
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc1d0d
    mul word [bp-016h]                        ; f7 66 ea                    ; 0xc1d10
    mov cx, ax                                ; 89 c1                       ; 0xc1d13
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc1d15
    jne short 01d65h                          ; 75 49                       ; 0xc1d1a
    add ax, ax                                ; 01 c0                       ; 0xc1d1c vgabios.c:1345
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc1d1e
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc1d20
    xor dh, dh                                ; 30 f6                       ; 0xc1d23
    inc ax                                    ; 40                          ; 0xc1d25
    mul dx                                    ; f7 e2                       ; 0xc1d26
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc1d28
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1d2b vgabios.c:1350
    jne short 01d68h                          ; 75 37                       ; 0xc1d2f
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1d31
    jne short 01d68h                          ; 75 31                       ; 0xc1d35
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1d37
    jne short 01d68h                          ; 75 2b                       ; 0xc1d3b
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1d3d
    xor ah, ah                                ; 30 e4                       ; 0xc1d40
    cmp ax, di                                ; 39 f8                       ; 0xc1d42
    jne short 01d68h                          ; 75 22                       ; 0xc1d44
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1d46
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc1d49
    jne short 01d68h                          ; 75 1a                       ; 0xc1d4c
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1d4e vgabios.c:1352
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1d51
    add ax, strict word 00020h                ; 05 20 00                    ; 0xc1d54
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1d57
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1d5b
    jcxz 01d62h                               ; e3 02                       ; 0xc1d5e
    rep stosw                                 ; f3 ab                       ; 0xc1d60
    jmp near 022d4h                           ; e9 6f 05                    ; 0xc1d62 vgabios.c:1354
    jmp near 01ed8h                           ; e9 70 01                    ; 0xc1d65
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1d68 vgabios.c:1356
    jne short 01dceh                          ; 75 60                       ; 0xc1d6c
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1d6e vgabios.c:1357
    xor ah, ah                                ; 30 e4                       ; 0xc1d71
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1d73
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc1d76
    xor dh, dh                                ; 30 f6                       ; 0xc1d79
    cmp dx, word [bp-01ch]                    ; 3b 56 e4                    ; 0xc1d7b
    jc short 01dd0h                           ; 72 50                       ; 0xc1d7e
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1d80 vgabios.c:1359
    xor ah, ah                                ; 30 e4                       ; 0xc1d83
    add ax, word [bp-01ch]                    ; 03 46 e4                    ; 0xc1d85
    cmp ax, dx                                ; 39 d0                       ; 0xc1d88
    jnbe short 01d92h                         ; 77 06                       ; 0xc1d8a
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1d8c
    jne short 01dd3h                          ; 75 41                       ; 0xc1d90
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1d92 vgabios.c:1360
    xor ch, ch                                ; 30 ed                       ; 0xc1d95
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1d97
    xor ah, ah                                ; 30 e4                       ; 0xc1d9a
    mov si, ax                                ; 89 c6                       ; 0xc1d9c
    sal si, 008h                              ; c1 e6 08                    ; 0xc1d9e
    add si, strict byte 00020h                ; 83 c6 20                    ; 0xc1da1
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1da4
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1da7
    mov dx, ax                                ; 89 c2                       ; 0xc1daa
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1dac
    xor ah, ah                                ; 30 e4                       ; 0xc1daf
    mov di, ax                                ; 89 c7                       ; 0xc1db1
    add di, dx                                ; 01 d7                       ; 0xc1db3
    add di, di                                ; 01 ff                       ; 0xc1db5
    add di, word [bp-020h]                    ; 03 7e e0                    ; 0xc1db7
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1dba
    xor bh, bh                                ; 30 ff                       ; 0xc1dbd
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1dbf
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1dc2
    mov ax, si                                ; 89 f0                       ; 0xc1dc6
    jcxz 01dcch                               ; e3 02                       ; 0xc1dc8
    rep stosw                                 ; f3 ab                       ; 0xc1dca
    jmp short 01e13h                          ; eb 45                       ; 0xc1dcc vgabios.c:1361
    jmp short 01e19h                          ; eb 49                       ; 0xc1dce
    jmp near 022d4h                           ; e9 01 05                    ; 0xc1dd0
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1dd3 vgabios.c:1362
    xor ch, ch                                ; 30 ed                       ; 0xc1dd6
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1dd8
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1ddb
    mov byte [bp-018h], dl                    ; 88 56 e8                    ; 0xc1dde
    mov byte [bp-017h], ch                    ; 88 6e e9                    ; 0xc1de1
    mov si, ax                                ; 89 c6                       ; 0xc1de4
    add si, word [bp-018h]                    ; 03 76 e8                    ; 0xc1de6
    add si, si                                ; 01 f6                       ; 0xc1de9
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1deb
    xor bh, bh                                ; 30 ff                       ; 0xc1dee
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1df0
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc1df3
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1df7
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1dfa
    add ax, word [bp-018h]                    ; 03 46 e8                    ; 0xc1dfd
    add ax, ax                                ; 01 c0                       ; 0xc1e00
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1e02
    add di, ax                                ; 01 c7                       ; 0xc1e05
    mov dx, bx                                ; 89 da                       ; 0xc1e07
    mov es, bx                                ; 8e c3                       ; 0xc1e09
    jcxz 01e13h                               ; e3 06                       ; 0xc1e0b
    push DS                                   ; 1e                          ; 0xc1e0d
    mov ds, dx                                ; 8e da                       ; 0xc1e0e
    rep movsw                                 ; f3 a5                       ; 0xc1e10
    pop DS                                    ; 1f                          ; 0xc1e12
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc1e13 vgabios.c:1363
    jmp near 01d76h                           ; e9 5d ff                    ; 0xc1e16
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1e19 vgabios.c:1366
    xor ah, ah                                ; 30 e4                       ; 0xc1e1c
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1e1e
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1e21
    xor ah, ah                                ; 30 e4                       ; 0xc1e24
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1e26
    jnbe short 01dd0h                         ; 77 a5                       ; 0xc1e29
    mov dl, al                                ; 88 c2                       ; 0xc1e2b vgabios.c:1368
    xor dh, dh                                ; 30 f6                       ; 0xc1e2d
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1e2f
    add ax, dx                                ; 01 d0                       ; 0xc1e32
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1e34
    jnbe short 01e3fh                         ; 77 06                       ; 0xc1e37
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1e39
    jne short 01e7bh                          ; 75 3c                       ; 0xc1e3d
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1e3f vgabios.c:1369
    xor ch, ch                                ; 30 ed                       ; 0xc1e42
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1e44
    xor ah, ah                                ; 30 e4                       ; 0xc1e47
    mov si, ax                                ; 89 c6                       ; 0xc1e49
    sal si, 008h                              ; c1 e6 08                    ; 0xc1e4b
    add si, strict byte 00020h                ; 83 c6 20                    ; 0xc1e4e
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1e51
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1e54
    mov dx, ax                                ; 89 c2                       ; 0xc1e57
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1e59
    xor ah, ah                                ; 30 e4                       ; 0xc1e5c
    add ax, dx                                ; 01 d0                       ; 0xc1e5e
    add ax, ax                                ; 01 c0                       ; 0xc1e60
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1e62
    add di, ax                                ; 01 c7                       ; 0xc1e65
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1e67
    xor bh, bh                                ; 30 ff                       ; 0xc1e6a
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1e6c
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1e6f
    mov ax, si                                ; 89 f0                       ; 0xc1e73
    jcxz 01e79h                               ; e3 02                       ; 0xc1e75
    rep stosw                                 ; f3 ab                       ; 0xc1e77
    jmp short 01ec8h                          ; eb 4d                       ; 0xc1e79 vgabios.c:1370
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1e7b vgabios.c:1371
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc1e7e
    mov byte [bp-017h], dh                    ; 88 76 e9                    ; 0xc1e81
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1e84
    xor ah, ah                                ; 30 e4                       ; 0xc1e87
    mov dx, word [bp-01ch]                    ; 8b 56 e4                    ; 0xc1e89
    sub dx, ax                                ; 29 c2                       ; 0xc1e8c
    mov ax, dx                                ; 89 d0                       ; 0xc1e8e
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1e90
    mov cl, byte [bp-008h]                    ; 8a 4e f8                    ; 0xc1e93
    xor ch, ch                                ; 30 ed                       ; 0xc1e96
    mov si, ax                                ; 89 c6                       ; 0xc1e98
    add si, cx                                ; 01 ce                       ; 0xc1e9a
    add si, si                                ; 01 f6                       ; 0xc1e9c
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1e9e
    xor bh, bh                                ; 30 ff                       ; 0xc1ea1
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1ea3
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc1ea6
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc1eaa
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1ead
    add ax, cx                                ; 01 c8                       ; 0xc1eb0
    add ax, ax                                ; 01 c0                       ; 0xc1eb2
    mov di, word [bp-020h]                    ; 8b 7e e0                    ; 0xc1eb4
    add di, ax                                ; 01 c7                       ; 0xc1eb7
    mov cx, word [bp-018h]                    ; 8b 4e e8                    ; 0xc1eb9
    mov dx, bx                                ; 89 da                       ; 0xc1ebc
    mov es, bx                                ; 8e c3                       ; 0xc1ebe
    jcxz 01ec8h                               ; e3 06                       ; 0xc1ec0
    push DS                                   ; 1e                          ; 0xc1ec2
    mov ds, dx                                ; 8e da                       ; 0xc1ec3
    rep movsw                                 ; f3 a5                       ; 0xc1ec5
    pop DS                                    ; 1f                          ; 0xc1ec7
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1ec8 vgabios.c:1372
    xor ah, ah                                ; 30 e4                       ; 0xc1ecb
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1ecd
    jc short 01f05h                           ; 72 33                       ; 0xc1ed0
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc1ed2 vgabios.c:1373
    jmp near 01e21h                           ; e9 49 ff                    ; 0xc1ed5
    mov si, word [bp-01ah]                    ; 8b 76 e6                    ; 0xc1ed8 vgabios.c:1379
    mov al, byte [si+04831h]                  ; 8a 84 31 48                 ; 0xc1edb
    xor ah, ah                                ; 30 e4                       ; 0xc1edf
    mov si, ax                                ; 89 c6                       ; 0xc1ee1
    sal si, 006h                              ; c1 e6 06                    ; 0xc1ee3
    mov al, byte [si+04847h]                  ; 8a 84 47 48                 ; 0xc1ee6
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc1eea
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc1eed vgabios.c:1380
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc1ef1
    jc short 01f01h                           ; 72 0c                       ; 0xc1ef3
    jbe short 01f08h                          ; 76 11                       ; 0xc1ef5
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc1ef7
    je short 01f36h                           ; 74 3b                       ; 0xc1ef9
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc1efb
    je short 01f08h                           ; 74 09                       ; 0xc1efd
    jmp short 01f05h                          ; eb 04                       ; 0xc1eff
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc1f01
    je short 01f39h                           ; 74 34                       ; 0xc1f03
    jmp near 022d4h                           ; e9 cc 03                    ; 0xc1f05
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1f08 vgabios.c:1384
    jne short 01f34h                          ; 75 26                       ; 0xc1f0c
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1f0e
    jne short 01f76h                          ; 75 62                       ; 0xc1f12
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1f14
    jne short 01f76h                          ; 75 5c                       ; 0xc1f18
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1f1a
    xor ah, ah                                ; 30 e4                       ; 0xc1f1d
    mov dx, word [bp-016h]                    ; 8b 56 ea                    ; 0xc1f1f
    dec dx                                    ; 4a                          ; 0xc1f22
    cmp ax, dx                                ; 39 d0                       ; 0xc1f23
    jne short 01f76h                          ; 75 4f                       ; 0xc1f25
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1f27
    xor ah, dh                                ; 30 f4                       ; 0xc1f2a
    mov dx, word [bp-01eh]                    ; 8b 56 e2                    ; 0xc1f2c
    dec dx                                    ; 4a                          ; 0xc1f2f
    cmp ax, dx                                ; 39 d0                       ; 0xc1f30
    je short 01f3ch                           ; 74 08                       ; 0xc1f32
    jmp short 01f76h                          ; eb 40                       ; 0xc1f34
    jmp near 021ach                           ; e9 73 02                    ; 0xc1f36
    jmp near 02066h                           ; e9 2a 01                    ; 0xc1f39
    mov ax, 00205h                            ; b8 05 02                    ; 0xc1f3c vgabios.c:1386
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1f3f
    out DX, ax                                ; ef                          ; 0xc1f42
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc1f43 vgabios.c:1387
    mul word [bp-01eh]                        ; f7 66 e2                    ; 0xc1f46
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc1f49
    xor dh, dh                                ; 30 f6                       ; 0xc1f4c
    mul dx                                    ; f7 e2                       ; 0xc1f4e
    mov dl, byte [bp-010h]                    ; 8a 56 f0                    ; 0xc1f50
    xor dh, dh                                ; 30 f6                       ; 0xc1f53
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1f55
    xor bh, bh                                ; 30 ff                       ; 0xc1f58
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1f5a
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc1f5d
    mov cx, ax                                ; 89 c1                       ; 0xc1f61
    mov ax, dx                                ; 89 d0                       ; 0xc1f63
    xor di, di                                ; 31 ff                       ; 0xc1f65
    mov es, bx                                ; 8e c3                       ; 0xc1f67
    jcxz 01f6dh                               ; e3 02                       ; 0xc1f69
    rep stosb                                 ; f3 aa                       ; 0xc1f6b
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1f6d vgabios.c:1388
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1f70
    out DX, ax                                ; ef                          ; 0xc1f73
    jmp short 01f05h                          ; eb 8f                       ; 0xc1f74 vgabios.c:1390
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1f76 vgabios.c:1392
    jne short 01ff1h                          ; 75 75                       ; 0xc1f7a
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1f7c vgabios.c:1393
    xor ah, ah                                ; 30 e4                       ; 0xc1f7f
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1f81
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1f84
    xor ah, ah                                ; 30 e4                       ; 0xc1f87
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1f89
    jc short 01feeh                           ; 72 60                       ; 0xc1f8c
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc1f8e vgabios.c:1395
    xor dh, dh                                ; 30 f6                       ; 0xc1f91
    add dx, word [bp-01ch]                    ; 03 56 e4                    ; 0xc1f93
    cmp dx, ax                                ; 39 c2                       ; 0xc1f96
    jnbe short 01fa0h                         ; 77 06                       ; 0xc1f98
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1f9a
    jne short 01fc1h                          ; 75 21                       ; 0xc1f9e
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1fa0 vgabios.c:1396
    xor ah, ah                                ; 30 e4                       ; 0xc1fa3
    push ax                                   ; 50                          ; 0xc1fa5
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc1fa6
    push ax                                   ; 50                          ; 0xc1fa9
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc1faa
    xor ch, ch                                ; 30 ed                       ; 0xc1fad
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc1faf
    xor bh, bh                                ; 30 ff                       ; 0xc1fb2
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc1fb4
    xor dh, dh                                ; 30 f6                       ; 0xc1fb7
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1fb9
    call 019a2h                               ; e8 e3 f9                    ; 0xc1fbc
    jmp short 01fe9h                          ; eb 28                       ; 0xc1fbf vgabios.c:1397
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc1fc1 vgabios.c:1398
    push ax                                   ; 50                          ; 0xc1fc4
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc1fc5
    push ax                                   ; 50                          ; 0xc1fc8
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc1fc9
    xor ch, ch                                ; 30 ed                       ; 0xc1fcc
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc1fce
    xor bh, bh                                ; 30 ff                       ; 0xc1fd1
    mov dl, bl                                ; 88 da                       ; 0xc1fd3
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc1fd5
    xor dh, dh                                ; 30 f6                       ; 0xc1fd8
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1fda
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc1fdd
    mov byte [bp-017h], ah                    ; 88 66 e9                    ; 0xc1fe0
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc1fe3
    call 01914h                               ; e8 2b f9                    ; 0xc1fe6
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc1fe9 vgabios.c:1399
    jmp short 01f84h                          ; eb 96                       ; 0xc1fec
    jmp near 022d4h                           ; e9 e3 02                    ; 0xc1fee
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1ff1 vgabios.c:1402
    xor ah, ah                                ; 30 e4                       ; 0xc1ff4
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1ff6
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1ff9
    xor ah, ah                                ; 30 e4                       ; 0xc1ffc
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc1ffe
    jnbe short 01feeh                         ; 77 eb                       ; 0xc2001
    mov dl, al                                ; 88 c2                       ; 0xc2003 vgabios.c:1404
    xor dh, dh                                ; 30 f6                       ; 0xc2005
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2007
    add ax, dx                                ; 01 d0                       ; 0xc200a
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc200c
    jnbe short 02017h                         ; 77 06                       ; 0xc200f
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2011
    jne short 02038h                          ; 75 21                       ; 0xc2015
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2017 vgabios.c:1405
    xor ah, ah                                ; 30 e4                       ; 0xc201a
    push ax                                   ; 50                          ; 0xc201c
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc201d
    push ax                                   ; 50                          ; 0xc2020
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc2021
    xor ch, ch                                ; 30 ed                       ; 0xc2024
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2026
    xor bh, bh                                ; 30 ff                       ; 0xc2029
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc202b
    xor dh, dh                                ; 30 f6                       ; 0xc202e
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2030
    call 019a2h                               ; e8 6c f9                    ; 0xc2033
    jmp short 02057h                          ; eb 1f                       ; 0xc2036 vgabios.c:1406
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2038 vgabios.c:1407
    xor ah, ah                                ; 30 e4                       ; 0xc203b
    push ax                                   ; 50                          ; 0xc203d
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc203e
    push ax                                   ; 50                          ; 0xc2041
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2042
    xor ch, ch                                ; 30 ed                       ; 0xc2045
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc2047
    xor bh, bh                                ; 30 ff                       ; 0xc204a
    mov dl, bl                                ; 88 da                       ; 0xc204c
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc204e
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2051
    call 01914h                               ; e8 bd f8                    ; 0xc2054
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2057 vgabios.c:1408
    xor ah, ah                                ; 30 e4                       ; 0xc205a
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc205c
    jc short 020afh                           ; 72 4e                       ; 0xc205f
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc2061 vgabios.c:1409
    jmp short 01ff9h                          ; eb 93                       ; 0xc2064
    mov al, byte [bx+047b4h]                  ; 8a 87 b4 47                 ; 0xc2066 vgabios.c:1414
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc206a
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc206d vgabios.c:1415
    jne short 020b2h                          ; 75 3f                       ; 0xc2071
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc2073
    jne short 020b2h                          ; 75 39                       ; 0xc2077
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc2079
    jne short 020b2h                          ; 75 33                       ; 0xc207d
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc207f
    cmp ax, di                                ; 39 f8                       ; 0xc2082
    jne short 020b2h                          ; 75 2c                       ; 0xc2084
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2086
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc2089
    jne short 020b2h                          ; 75 24                       ; 0xc208c
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc208e vgabios.c:1417
    xor dh, dh                                ; 30 f6                       ; 0xc2091
    mov ax, cx                                ; 89 c8                       ; 0xc2093
    mul dx                                    ; f7 e2                       ; 0xc2095
    mov dl, byte [bp-014h]                    ; 8a 56 ec                    ; 0xc2097
    xor dh, dh                                ; 30 f6                       ; 0xc209a
    mul dx                                    ; f7 e2                       ; 0xc209c
    mov cx, ax                                ; 89 c1                       ; 0xc209e
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc20a0
    xor ah, ah                                ; 30 e4                       ; 0xc20a3
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc20a5
    xor di, di                                ; 31 ff                       ; 0xc20a9
    jcxz 020afh                               ; e3 02                       ; 0xc20ab
    rep stosb                                 ; f3 aa                       ; 0xc20ad
    jmp near 022d4h                           ; e9 22 02                    ; 0xc20af vgabios.c:1419
    cmp byte [bp-014h], 002h                  ; 80 7e ec 02                 ; 0xc20b2 vgabios.c:1421
    jne short 020c1h                          ; 75 09                       ; 0xc20b6
    sal byte [bp-008h], 1                     ; d0 66 f8                    ; 0xc20b8 vgabios.c:1423
    sal byte [bp-00ah], 1                     ; d0 66 f6                    ; 0xc20bb vgabios.c:1424
    sal word [bp-01eh], 1                     ; d1 66 e2                    ; 0xc20be vgabios.c:1425
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc20c1 vgabios.c:1428
    jne short 02130h                          ; 75 69                       ; 0xc20c5
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc20c7 vgabios.c:1429
    xor ah, ah                                ; 30 e4                       ; 0xc20ca
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc20cc
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc20cf
    xor ah, ah                                ; 30 e4                       ; 0xc20d2
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc20d4
    jc short 020afh                           ; 72 d6                       ; 0xc20d7
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc20d9 vgabios.c:1431
    xor dh, dh                                ; 30 f6                       ; 0xc20dc
    add dx, word [bp-01ch]                    ; 03 56 e4                    ; 0xc20de
    cmp dx, ax                                ; 39 c2                       ; 0xc20e1
    jnbe short 020ebh                         ; 77 06                       ; 0xc20e3
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc20e5
    jne short 0210ch                          ; 75 21                       ; 0xc20e9
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc20eb vgabios.c:1432
    xor ah, ah                                ; 30 e4                       ; 0xc20ee
    push ax                                   ; 50                          ; 0xc20f0
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc20f1
    push ax                                   ; 50                          ; 0xc20f4
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc20f5
    xor ch, ch                                ; 30 ed                       ; 0xc20f8
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc20fa
    xor bh, bh                                ; 30 ff                       ; 0xc20fd
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc20ff
    xor dh, dh                                ; 30 f6                       ; 0xc2102
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2104
    call 01ad3h                               ; e8 c9 f9                    ; 0xc2107
    jmp short 0212bh                          ; eb 1f                       ; 0xc210a vgabios.c:1433
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc210c vgabios.c:1434
    push ax                                   ; 50                          ; 0xc210f
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc2110
    push ax                                   ; 50                          ; 0xc2113
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2114
    xor ch, ch                                ; 30 ed                       ; 0xc2117
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc2119
    xor bh, bh                                ; 30 ff                       ; 0xc211c
    mov dl, bl                                ; 88 da                       ; 0xc211e
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc2120
    xor dh, dh                                ; 30 f6                       ; 0xc2123
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2125
    call 01a1dh                               ; e8 f2 f8                    ; 0xc2128
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc212b vgabios.c:1435
    jmp short 020cfh                          ; eb 9f                       ; 0xc212e
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2130 vgabios.c:1438
    xor ah, ah                                ; 30 e4                       ; 0xc2133
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc2135
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2138
    xor ah, ah                                ; 30 e4                       ; 0xc213b
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc213d
    jnbe short 021aah                         ; 77 68                       ; 0xc2140
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2142 vgabios.c:1440
    xor dh, dh                                ; 30 f6                       ; 0xc2145
    add ax, dx                                ; 01 d0                       ; 0xc2147
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc2149
    jnbe short 02152h                         ; 77 04                       ; 0xc214c
    test dl, dl                               ; 84 d2                       ; 0xc214e
    jne short 0217ch                          ; 75 2a                       ; 0xc2150
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2152 vgabios.c:1441
    xor ah, ah                                ; 30 e4                       ; 0xc2155
    push ax                                   ; 50                          ; 0xc2157
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2158
    push ax                                   ; 50                          ; 0xc215b
    mov cl, byte [bp-01eh]                    ; 8a 4e e2                    ; 0xc215c
    xor ch, ch                                ; 30 ed                       ; 0xc215f
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2161
    xor bh, bh                                ; 30 ff                       ; 0xc2164
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc2166
    xor dh, dh                                ; 30 f6                       ; 0xc2169
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc216b
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc216e
    mov byte [bp-017h], ah                    ; 88 66 e9                    ; 0xc2171
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc2174
    call 01ad3h                               ; e8 59 f9                    ; 0xc2177
    jmp short 0219bh                          ; eb 1f                       ; 0xc217a vgabios.c:1442
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc217c vgabios.c:1443
    xor ah, ah                                ; 30 e4                       ; 0xc217f
    push ax                                   ; 50                          ; 0xc2181
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc2182
    push ax                                   ; 50                          ; 0xc2185
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2186
    xor ch, ch                                ; 30 ed                       ; 0xc2189
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc218b
    xor bh, bh                                ; 30 ff                       ; 0xc218e
    mov dl, bl                                ; 88 da                       ; 0xc2190
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc2192
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2195
    call 01a1dh                               ; e8 82 f8                    ; 0xc2198
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc219b vgabios.c:1444
    xor ah, ah                                ; 30 e4                       ; 0xc219e
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc21a0
    jc short 021eah                           ; 72 45                       ; 0xc21a3
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc21a5 vgabios.c:1445
    jmp short 02138h                          ; eb 8e                       ; 0xc21a8
    jmp short 021eah                          ; eb 3e                       ; 0xc21aa
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc21ac vgabios.c:1450
    jne short 021edh                          ; 75 3b                       ; 0xc21b0
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc21b2
    jne short 021edh                          ; 75 35                       ; 0xc21b6
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc21b8
    jne short 021edh                          ; 75 2f                       ; 0xc21bc
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc21be
    cmp ax, di                                ; 39 f8                       ; 0xc21c1
    jne short 021edh                          ; 75 28                       ; 0xc21c3
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc21c5
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc21c8
    jne short 021edh                          ; 75 20                       ; 0xc21cb
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc21cd vgabios.c:1452
    xor dh, dh                                ; 30 f6                       ; 0xc21d0
    mov ax, cx                                ; 89 c8                       ; 0xc21d2
    mul dx                                    ; f7 e2                       ; 0xc21d4
    mov cx, ax                                ; 89 c1                       ; 0xc21d6
    sal cx, 003h                              ; c1 e1 03                    ; 0xc21d8
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc21db
    xor ah, ah                                ; 30 e4                       ; 0xc21de
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc21e0
    xor di, di                                ; 31 ff                       ; 0xc21e4
    jcxz 021eah                               ; e3 02                       ; 0xc21e6
    rep stosb                                 ; f3 aa                       ; 0xc21e8
    jmp near 022d4h                           ; e9 e7 00                    ; 0xc21ea vgabios.c:1454
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc21ed vgabios.c:1457
    jne short 02262h                          ; 75 6f                       ; 0xc21f1
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc21f3 vgabios.c:1458
    xor ah, ah                                ; 30 e4                       ; 0xc21f6
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc21f8
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc21fb
    xor ah, ah                                ; 30 e4                       ; 0xc21fe
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc2200
    jc short 021eah                           ; 72 e5                       ; 0xc2203
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2205 vgabios.c:1460
    xor dh, dh                                ; 30 f6                       ; 0xc2208
    add dx, word [bp-01ch]                    ; 03 56 e4                    ; 0xc220a
    cmp dx, ax                                ; 39 c2                       ; 0xc220d
    jnbe short 02217h                         ; 77 06                       ; 0xc220f
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2211
    jne short 02236h                          ; 75 1f                       ; 0xc2215
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2217 vgabios.c:1461
    xor ah, ah                                ; 30 e4                       ; 0xc221a
    push ax                                   ; 50                          ; 0xc221c
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc221d
    push ax                                   ; 50                          ; 0xc2220
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2221
    xor bh, bh                                ; 30 ff                       ; 0xc2224
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc2226
    xor dh, dh                                ; 30 f6                       ; 0xc2229
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc222b
    mov cx, word [bp-01eh]                    ; 8b 4e e2                    ; 0xc222e
    call 01be8h                               ; e8 b4 f9                    ; 0xc2231
    jmp short 0225dh                          ; eb 27                       ; 0xc2234 vgabios.c:1462
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2236 vgabios.c:1463
    push ax                                   ; 50                          ; 0xc2239
    push word [bp-01eh]                       ; ff 76 e2                    ; 0xc223a
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc223d
    xor ch, ch                                ; 30 ed                       ; 0xc2240
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc2242
    xor bh, bh                                ; 30 ff                       ; 0xc2245
    mov dl, bl                                ; 88 da                       ; 0xc2247
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc2249
    xor dh, dh                                ; 30 f6                       ; 0xc224c
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc224e
    mov byte [bp-018h], al                    ; 88 46 e8                    ; 0xc2251
    mov byte [bp-017h], ah                    ; 88 66 e9                    ; 0xc2254
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc2257
    call 01b67h                               ; e8 0a f9                    ; 0xc225a
    inc word [bp-01ch]                        ; ff 46 e4                    ; 0xc225d vgabios.c:1464
    jmp short 021fbh                          ; eb 99                       ; 0xc2260
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2262 vgabios.c:1467
    xor ah, ah                                ; 30 e4                       ; 0xc2265
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc2267
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc226a
    xor ah, ah                                ; 30 e4                       ; 0xc226d
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc226f
    jnbe short 022d4h                         ; 77 60                       ; 0xc2272
    mov dl, al                                ; 88 c2                       ; 0xc2274 vgabios.c:1469
    xor dh, dh                                ; 30 f6                       ; 0xc2276
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2278
    add ax, dx                                ; 01 d0                       ; 0xc227b
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc227d
    jnbe short 02288h                         ; 77 06                       ; 0xc2280
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2282
    jne short 022a7h                          ; 75 1f                       ; 0xc2286
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2288 vgabios.c:1470
    xor ah, ah                                ; 30 e4                       ; 0xc228b
    push ax                                   ; 50                          ; 0xc228d
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc228e
    push ax                                   ; 50                          ; 0xc2291
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2292
    xor bh, bh                                ; 30 ff                       ; 0xc2295
    mov dl, byte [bp-01ch]                    ; 8a 56 e4                    ; 0xc2297
    xor dh, dh                                ; 30 f6                       ; 0xc229a
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc229c
    mov cx, word [bp-01eh]                    ; 8b 4e e2                    ; 0xc229f
    call 01be8h                               ; e8 43 f9                    ; 0xc22a2
    jmp short 022c5h                          ; eb 1e                       ; 0xc22a5 vgabios.c:1471
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc22a7 vgabios.c:1472
    xor ah, ah                                ; 30 e4                       ; 0xc22aa
    push ax                                   ; 50                          ; 0xc22ac
    push word [bp-01eh]                       ; ff 76 e2                    ; 0xc22ad
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc22b0
    xor ch, ch                                ; 30 ed                       ; 0xc22b3
    mov bl, byte [bp-01ch]                    ; 8a 5e e4                    ; 0xc22b5
    xor bh, bh                                ; 30 ff                       ; 0xc22b8
    mov dl, bl                                ; 88 da                       ; 0xc22ba
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc22bc
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc22bf
    call 01b67h                               ; e8 a2 f8                    ; 0xc22c2
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc22c5 vgabios.c:1473
    xor ah, ah                                ; 30 e4                       ; 0xc22c8
    cmp ax, word [bp-01ch]                    ; 3b 46 e4                    ; 0xc22ca
    jc short 022d4h                           ; 72 05                       ; 0xc22cd
    dec word [bp-01ch]                        ; ff 4e e4                    ; 0xc22cf vgabios.c:1474
    jmp short 0226ah                          ; eb 96                       ; 0xc22d2
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc22d4 vgabios.c:1485
    pop di                                    ; 5f                          ; 0xc22d7
    pop si                                    ; 5e                          ; 0xc22d8
    pop bp                                    ; 5d                          ; 0xc22d9
    retn 00008h                               ; c2 08 00                    ; 0xc22da
  ; disGetNextSymbol 0xc22dd LB 0x2255 -> off=0x0 cb=0000000000000111 uValue=00000000000c22dd 'write_gfx_char_pl4'
write_gfx_char_pl4:                          ; 0xc22dd LB 0x111
    push bp                                   ; 55                          ; 0xc22dd vgabios.c:1488
    mov bp, sp                                ; 89 e5                       ; 0xc22de
    push si                                   ; 56                          ; 0xc22e0
    push di                                   ; 57                          ; 0xc22e1
    sub sp, strict byte 0000eh                ; 83 ec 0e                    ; 0xc22e2
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc22e5
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc22e8
    mov ch, bl                                ; 88 dd                       ; 0xc22eb
    mov al, cl                                ; 88 c8                       ; 0xc22ed
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc22ef vgabios.c:67
    xor dx, dx                                ; 31 d2                       ; 0xc22f2
    mov es, dx                                ; 8e c2                       ; 0xc22f4
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc22f6
    mov bx, word [es:bx+002h]                 ; 26 8b 5f 02                 ; 0xc22f9
    mov word [bp-012h], dx                    ; 89 56 ee                    ; 0xc22fd vgabios.c:68
    mov word [bp-00ch], bx                    ; 89 5e f4                    ; 0xc2300
    xor ah, ah                                ; 30 e4                       ; 0xc2303 vgabios.c:1497
    mov bl, byte [bp+006h]                    ; 8a 5e 06                    ; 0xc2305
    xor bh, bh                                ; 30 ff                       ; 0xc2308
    imul bx                                   ; f7 eb                       ; 0xc230a
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc230c
    xor dh, dh                                ; 30 f6                       ; 0xc230f
    imul dx                                   ; f7 ea                       ; 0xc2311
    mov si, ax                                ; 89 c6                       ; 0xc2313
    mov al, ch                                ; 88 e8                       ; 0xc2315
    xor ah, ah                                ; 30 e4                       ; 0xc2317
    add si, ax                                ; 01 c6                       ; 0xc2319
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc231b vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc231e
    mov es, ax                                ; 8e c0                       ; 0xc2321
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc2323
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc2326 vgabios.c:58
    xor dh, dh                                ; 30 f6                       ; 0xc2329
    mul dx                                    ; f7 e2                       ; 0xc232b
    add si, ax                                ; 01 c6                       ; 0xc232d
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc232f vgabios.c:1499
    xor ah, ah                                ; 30 e4                       ; 0xc2332
    imul bx                                   ; f7 eb                       ; 0xc2334
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc2336
    mov ax, 00f02h                            ; b8 02 0f                    ; 0xc2339 vgabios.c:1500
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc233c
    out DX, ax                                ; ef                          ; 0xc233f
    mov ax, 00205h                            ; b8 05 02                    ; 0xc2340 vgabios.c:1501
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2343
    out DX, ax                                ; ef                          ; 0xc2346
    test byte [bp-008h], 080h                 ; f6 46 f8 80                 ; 0xc2347 vgabios.c:1502
    je short 02353h                           ; 74 06                       ; 0xc234b
    mov ax, 01803h                            ; b8 03 18                    ; 0xc234d vgabios.c:1504
    out DX, ax                                ; ef                          ; 0xc2350
    jmp short 02357h                          ; eb 04                       ; 0xc2351 vgabios.c:1506
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc2353 vgabios.c:1508
    out DX, ax                                ; ef                          ; 0xc2356
    xor ch, ch                                ; 30 ed                       ; 0xc2357 vgabios.c:1510
    cmp ch, byte [bp+006h]                    ; 3a 6e 06                    ; 0xc2359
    jnc short 023d0h                          ; 73 72                       ; 0xc235c
    mov al, ch                                ; 88 e8                       ; 0xc235e vgabios.c:1512
    xor ah, ah                                ; 30 e4                       ; 0xc2360
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc2362
    xor bh, bh                                ; 30 ff                       ; 0xc2365
    imul bx                                   ; f7 eb                       ; 0xc2367
    mov bx, si                                ; 89 f3                       ; 0xc2369
    add bx, ax                                ; 01 c3                       ; 0xc236b
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc236d vgabios.c:1513
    jmp short 02385h                          ; eb 12                       ; 0xc2371
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2373 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2376
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc2378
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc237c vgabios.c:1526
    cmp byte [bp-006h], 008h                  ; 80 7e fa 08                 ; 0xc237f
    jnc short 023d2h                          ; 73 4d                       ; 0xc2383
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc2385
    mov ax, 00080h                            ; b8 80 00                    ; 0xc2388
    sar ax, CL                                ; d3 f8                       ; 0xc238b
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc238d
    mov byte [bp-00dh], 000h                  ; c6 46 f3 00                 ; 0xc2390
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2394
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2397
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc239a
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc239c
    out DX, ax                                ; ef                          ; 0xc239f
    mov dx, bx                                ; 89 da                       ; 0xc23a0
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc23a2
    call 03841h                               ; e8 99 14                    ; 0xc23a5
    mov al, ch                                ; 88 e8                       ; 0xc23a8
    xor ah, ah                                ; 30 e4                       ; 0xc23aa
    add ax, word [bp-010h]                    ; 03 46 f0                    ; 0xc23ac
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc23af
    mov di, word [bp-012h]                    ; 8b 7e ee                    ; 0xc23b2
    add di, ax                                ; 01 c7                       ; 0xc23b5
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc23b7
    xor ah, ah                                ; 30 e4                       ; 0xc23ba
    test word [bp-00eh], ax                   ; 85 46 f2                    ; 0xc23bc
    je short 02373h                           ; 74 b2                       ; 0xc23bf
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc23c1
    and AL, strict byte 00fh                  ; 24 0f                       ; 0xc23c4
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc23c6
    mov es, dx                                ; 8e c2                       ; 0xc23c9
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc23cb
    jmp short 0237ch                          ; eb ac                       ; 0xc23ce
    jmp short 023d6h                          ; eb 04                       ; 0xc23d0
    db  0feh, 0c5h
    ; inc ch                                    ; fe c5                     ; 0xc23d2 vgabios.c:1527
    jmp short 02359h                          ; eb 83                       ; 0xc23d4
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc23d6 vgabios.c:1528
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc23d9
    out DX, ax                                ; ef                          ; 0xc23dc
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc23dd vgabios.c:1529
    out DX, ax                                ; ef                          ; 0xc23e0
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc23e1 vgabios.c:1530
    out DX, ax                                ; ef                          ; 0xc23e4
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc23e5 vgabios.c:1531
    pop di                                    ; 5f                          ; 0xc23e8
    pop si                                    ; 5e                          ; 0xc23e9
    pop bp                                    ; 5d                          ; 0xc23ea
    retn 00006h                               ; c2 06 00                    ; 0xc23eb
  ; disGetNextSymbol 0xc23ee LB 0x2144 -> off=0x0 cb=0000000000000112 uValue=00000000000c23ee 'write_gfx_char_cga'
write_gfx_char_cga:                          ; 0xc23ee LB 0x112
    push si                                   ; 56                          ; 0xc23ee vgabios.c:1534
    push di                                   ; 57                          ; 0xc23ef
    enter 0000ch, 000h                        ; c8 0c 00 00                 ; 0xc23f0
    mov bh, al                                ; 88 c7                       ; 0xc23f4
    mov ch, dl                                ; 88 d5                       ; 0xc23f6
    mov al, bl                                ; 88 d8                       ; 0xc23f8
    mov di, 0556fh                            ; bf 6f 55                    ; 0xc23fa vgabios.c:1541
    xor ah, ah                                ; 30 e4                       ; 0xc23fd vgabios.c:1542
    mov dl, byte [bp+00ah]                    ; 8a 56 0a                    ; 0xc23ff
    xor dh, dh                                ; 30 f6                       ; 0xc2402
    imul dx                                   ; f7 ea                       ; 0xc2404
    mov dl, cl                                ; 88 ca                       ; 0xc2406
    xor dh, dh                                ; 30 f6                       ; 0xc2408
    imul dx, dx, 00140h                       ; 69 d2 40 01                 ; 0xc240a
    add ax, dx                                ; 01 d0                       ; 0xc240e
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc2410
    mov al, bh                                ; 88 f8                       ; 0xc2413 vgabios.c:1543
    xor ah, ah                                ; 30 e4                       ; 0xc2415
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2417
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc241a
    xor ah, ah                                ; 30 e4                       ; 0xc241d vgabios.c:1544
    jmp near 0243eh                           ; e9 1c 00                    ; 0xc241f
    mov dl, ah                                ; 88 e2                       ; 0xc2422 vgabios.c:1559
    xor dh, dh                                ; 30 f6                       ; 0xc2424
    add dx, word [bp-00ch]                    ; 03 56 f4                    ; 0xc2426
    mov si, di                                ; 89 fe                       ; 0xc2429
    add si, dx                                ; 01 d6                       ; 0xc242b
    mov al, byte [si]                         ; 8a 04                       ; 0xc242d
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc242f vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc2432
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2434
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc2437 vgabios.c:1563
    cmp ah, 008h                              ; 80 fc 08                    ; 0xc2439
    jnc short 02495h                          ; 73 57                       ; 0xc243c
    mov dl, ah                                ; 88 e2                       ; 0xc243e
    xor dh, dh                                ; 30 f6                       ; 0xc2440
    sar dx, 1                                 ; d1 fa                       ; 0xc2442
    imul dx, dx, strict byte 00050h           ; 6b d2 50                    ; 0xc2444
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2447
    add bx, dx                                ; 01 d3                       ; 0xc244a
    test ah, 001h                             ; f6 c4 01                    ; 0xc244c
    je short 02454h                           ; 74 03                       ; 0xc244f
    add bh, 020h                              ; 80 c7 20                    ; 0xc2451
    mov byte [bp-002h], 080h                  ; c6 46 fe 80                 ; 0xc2454
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc2458
    jne short 0247ah                          ; 75 1c                       ; 0xc245c
    test ch, 080h                             ; f6 c5 80                    ; 0xc245e
    je short 02422h                           ; 74 bf                       ; 0xc2461
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc2463
    mov es, dx                                ; 8e c2                       ; 0xc2466
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2468
    mov dl, ah                                ; 88 e2                       ; 0xc246b
    xor dh, dh                                ; 30 f6                       ; 0xc246d
    add dx, word [bp-00ch]                    ; 03 56 f4                    ; 0xc246f
    mov si, di                                ; 89 fe                       ; 0xc2472
    add si, dx                                ; 01 d6                       ; 0xc2474
    xor al, byte [si]                         ; 32 04                       ; 0xc2476
    jmp short 0242fh                          ; eb b5                       ; 0xc2478
    cmp byte [bp-002h], 000h                  ; 80 7e fe 00                 ; 0xc247a vgabios.c:1565
    jbe short 02437h                          ; 76 b7                       ; 0xc247e
    test ch, 080h                             ; f6 c5 80                    ; 0xc2480 vgabios.c:1567
    je short 0248fh                           ; 74 0a                       ; 0xc2483
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc2485 vgabios.c:47
    mov es, dx                                ; 8e c2                       ; 0xc2488
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc248a
    jmp short 02491h                          ; eb 02                       ; 0xc248d vgabios.c:1571
    xor al, al                                ; 30 c0                       ; 0xc248f vgabios.c:1573
    xor dl, dl                                ; 30 d2                       ; 0xc2491 vgabios.c:1575
    jmp short 0249ch                          ; eb 07                       ; 0xc2493
    jmp short 024fah                          ; eb 63                       ; 0xc2495
    cmp dl, 004h                              ; 80 fa 04                    ; 0xc2497
    jnc short 024efh                          ; 73 53                       ; 0xc249a
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc249c vgabios.c:1577
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc249f
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc24a3
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc24a6
    add si, di                                ; 01 fe                       ; 0xc24a9
    mov dh, byte [si]                         ; 8a 34                       ; 0xc24ab
    mov byte [bp-006h], dh                    ; 88 76 fa                    ; 0xc24ad
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc24b0
    mov dh, byte [bp-002h]                    ; 8a 76 fe                    ; 0xc24b4
    mov byte [bp-00ah], dh                    ; 88 76 f6                    ; 0xc24b7
    mov byte [bp-009h], 000h                  ; c6 46 f7 00                 ; 0xc24ba
    mov si, word [bp-006h]                    ; 8b 76 fa                    ; 0xc24be
    test word [bp-00ah], si                   ; 85 76 f6                    ; 0xc24c1
    je short 024e8h                           ; 74 22                       ; 0xc24c4
    mov DH, strict byte 003h                  ; b6 03                       ; 0xc24c6 vgabios.c:1578
    sub dh, dl                                ; 28 d6                       ; 0xc24c8
    mov cl, ch                                ; 88 e9                       ; 0xc24ca
    and cl, 003h                              ; 80 e1 03                    ; 0xc24cc
    mov byte [bp-004h], cl                    ; 88 4e fc                    ; 0xc24cf
    mov cl, dh                                ; 88 f1                       ; 0xc24d2
    add cl, dh                                ; 00 f1                       ; 0xc24d4
    mov dh, byte [bp-004h]                    ; 8a 76 fc                    ; 0xc24d6
    sal dh, CL                                ; d2 e6                       ; 0xc24d9
    mov cl, dh                                ; 88 f1                       ; 0xc24db
    test ch, 080h                             ; f6 c5 80                    ; 0xc24dd vgabios.c:1579
    je short 024e6h                           ; 74 04                       ; 0xc24e0
    xor al, dh                                ; 30 f0                       ; 0xc24e2 vgabios.c:1581
    jmp short 024e8h                          ; eb 02                       ; 0xc24e4 vgabios.c:1583
    or al, dh                                 ; 08 f0                       ; 0xc24e6 vgabios.c:1585
    shr byte [bp-002h], 1                     ; d0 6e fe                    ; 0xc24e8 vgabios.c:1588
    db  0feh, 0c2h
    ; inc dl                                    ; fe c2                     ; 0xc24eb vgabios.c:1589
    jmp short 02497h                          ; eb a8                       ; 0xc24ed
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc24ef vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc24f2
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc24f4
    inc bx                                    ; 43                          ; 0xc24f7 vgabios.c:1591
    jmp short 0247ah                          ; eb 80                       ; 0xc24f8 vgabios.c:1592
    leave                                     ; c9                          ; 0xc24fa vgabios.c:1595
    pop di                                    ; 5f                          ; 0xc24fb
    pop si                                    ; 5e                          ; 0xc24fc
    retn 00004h                               ; c2 04 00                    ; 0xc24fd
  ; disGetNextSymbol 0xc2500 LB 0x2032 -> off=0x0 cb=000000000000009b uValue=00000000000c2500 'write_gfx_char_lin'
write_gfx_char_lin:                          ; 0xc2500 LB 0x9b
    push si                                   ; 56                          ; 0xc2500 vgabios.c:1598
    push di                                   ; 57                          ; 0xc2501
    enter 00008h, 000h                        ; c8 08 00 00                 ; 0xc2502
    mov bh, al                                ; 88 c7                       ; 0xc2506
    mov ch, dl                                ; 88 d5                       ; 0xc2508
    mov al, cl                                ; 88 c8                       ; 0xc250a
    mov di, 0556fh                            ; bf 6f 55                    ; 0xc250c vgabios.c:1605
    xor ah, ah                                ; 30 e4                       ; 0xc250f vgabios.c:1606
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc2511
    xor dh, dh                                ; 30 f6                       ; 0xc2514
    imul dx                                   ; f7 ea                       ; 0xc2516
    mov dx, ax                                ; 89 c2                       ; 0xc2518
    sal dx, 006h                              ; c1 e2 06                    ; 0xc251a
    mov al, bl                                ; 88 d8                       ; 0xc251d
    xor ah, ah                                ; 30 e4                       ; 0xc251f
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2521
    add ax, dx                                ; 01 d0                       ; 0xc2524
    mov word [bp-002h], ax                    ; 89 46 fe                    ; 0xc2526
    mov al, bh                                ; 88 f8                       ; 0xc2529 vgabios.c:1607
    xor ah, ah                                ; 30 e4                       ; 0xc252b
    sal ax, 003h                              ; c1 e0 03                    ; 0xc252d
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc2530
    xor bl, bl                                ; 30 db                       ; 0xc2533 vgabios.c:1608
    jmp short 02579h                          ; eb 42                       ; 0xc2535
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc2537 vgabios.c:1612
    jnc short 02572h                          ; 73 37                       ; 0xc2539
    xor bh, bh                                ; 30 ff                       ; 0xc253b vgabios.c:1614
    mov dl, bl                                ; 88 da                       ; 0xc253d vgabios.c:1615
    xor dh, dh                                ; 30 f6                       ; 0xc253f
    add dx, word [bp-006h]                    ; 03 56 fa                    ; 0xc2541
    mov si, di                                ; 89 fe                       ; 0xc2544
    add si, dx                                ; 01 d6                       ; 0xc2546
    mov dl, byte [si]                         ; 8a 14                       ; 0xc2548
    mov byte [bp-004h], dl                    ; 88 56 fc                    ; 0xc254a
    mov byte [bp-003h], bh                    ; 88 7e fd                    ; 0xc254d
    mov dl, ah                                ; 88 e2                       ; 0xc2550
    xor dh, dh                                ; 30 f6                       ; 0xc2552
    test word [bp-004h], dx                   ; 85 56 fc                    ; 0xc2554
    je short 0255bh                           ; 74 02                       ; 0xc2557
    mov bh, ch                                ; 88 ef                       ; 0xc2559 vgabios.c:1617
    mov dl, al                                ; 88 c2                       ; 0xc255b vgabios.c:1619
    xor dh, dh                                ; 30 f6                       ; 0xc255d
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc255f
    add si, dx                                ; 01 d6                       ; 0xc2562
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc2564 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc2567
    mov byte [es:si], bh                      ; 26 88 3c                    ; 0xc2569
    shr ah, 1                                 ; d0 ec                       ; 0xc256c vgabios.c:1620
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc256e vgabios.c:1621
    jmp short 02537h                          ; eb c5                       ; 0xc2570
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc2572 vgabios.c:1622
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc2574
    jnc short 02595h                          ; 73 1c                       ; 0xc2577
    mov al, bl                                ; 88 d8                       ; 0xc2579
    xor ah, ah                                ; 30 e4                       ; 0xc257b
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc257d
    xor dh, dh                                ; 30 f6                       ; 0xc2580
    imul dx                                   ; f7 ea                       ; 0xc2582
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2584
    mov dx, word [bp-002h]                    ; 8b 56 fe                    ; 0xc2587
    add dx, ax                                ; 01 c2                       ; 0xc258a
    mov word [bp-008h], dx                    ; 89 56 f8                    ; 0xc258c
    mov AH, strict byte 080h                  ; b4 80                       ; 0xc258f
    xor al, al                                ; 30 c0                       ; 0xc2591
    jmp short 0253bh                          ; eb a6                       ; 0xc2593
    leave                                     ; c9                          ; 0xc2595 vgabios.c:1623
    pop di                                    ; 5f                          ; 0xc2596
    pop si                                    ; 5e                          ; 0xc2597
    retn 00002h                               ; c2 02 00                    ; 0xc2598
  ; disGetNextSymbol 0xc259b LB 0x1f97 -> off=0x0 cb=0000000000000187 uValue=00000000000c259b 'biosfn_write_char_attr'
biosfn_write_char_attr:                      ; 0xc259b LB 0x187
    push bp                                   ; 55                          ; 0xc259b vgabios.c:1626
    mov bp, sp                                ; 89 e5                       ; 0xc259c
    push si                                   ; 56                          ; 0xc259e
    push di                                   ; 57                          ; 0xc259f
    sub sp, strict byte 0001ch                ; 83 ec 1c                    ; 0xc25a0
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc25a3
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc25a6
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc25a9
    mov si, cx                                ; 89 ce                       ; 0xc25ac
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc25ae vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc25b1
    mov es, ax                                ; 8e c0                       ; 0xc25b4
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc25b6
    xor ah, ah                                ; 30 e4                       ; 0xc25b9 vgabios.c:1634
    call 03819h                               ; e8 5b 12                    ; 0xc25bb
    mov cl, al                                ; 88 c1                       ; 0xc25be
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc25c0
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc25c3 vgabios.c:1635
    jne short 025cah                          ; 75 03                       ; 0xc25c5
    jmp near 0271bh                           ; e9 51 01                    ; 0xc25c7
    mov al, dl                                ; 88 d0                       ; 0xc25ca vgabios.c:1638
    xor ah, ah                                ; 30 e4                       ; 0xc25cc
    lea bx, [bp-01eh]                         ; 8d 5e e2                    ; 0xc25ce
    lea dx, [bp-020h]                         ; 8d 56 e0                    ; 0xc25d1
    call 00a96h                               ; e8 bf e4                    ; 0xc25d4
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc25d7 vgabios.c:1639
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc25da
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc25dd
    xor al, al                                ; 30 c0                       ; 0xc25e0
    shr ax, 008h                              ; c1 e8 08                    ; 0xc25e2
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc25e5
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc25e8
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc25eb
    mov bx, 00084h                            ; bb 84 00                    ; 0xc25ee vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc25f1
    mov es, ax                                ; 8e c0                       ; 0xc25f4
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc25f6
    xor ah, ah                                ; 30 e4                       ; 0xc25f9 vgabios.c:48
    mov dx, ax                                ; 89 c2                       ; 0xc25fb
    inc dx                                    ; 42                          ; 0xc25fd
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc25fe vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc2601
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc2604
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc2607 vgabios.c:58
    mov bl, cl                                ; 88 cb                       ; 0xc260a vgabios.c:1645
    xor bh, bh                                ; 30 ff                       ; 0xc260c
    mov di, bx                                ; 89 df                       ; 0xc260e
    sal di, 003h                              ; c1 e7 03                    ; 0xc2610
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc2613
    jne short 02663h                          ; 75 49                       ; 0xc2618
    mul dx                                    ; f7 e2                       ; 0xc261a vgabios.c:1648
    add ax, ax                                ; 01 c0                       ; 0xc261c
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc261e
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc2620
    xor dh, dh                                ; 30 f6                       ; 0xc2623
    inc ax                                    ; 40                          ; 0xc2625
    mul dx                                    ; f7 e2                       ; 0xc2626
    mov bx, ax                                ; 89 c3                       ; 0xc2628
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc262a
    xor ah, ah                                ; 30 e4                       ; 0xc262d
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc262f
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2632
    xor dh, dh                                ; 30 f6                       ; 0xc2635
    add ax, dx                                ; 01 d0                       ; 0xc2637
    add ax, ax                                ; 01 c0                       ; 0xc2639
    mov dx, bx                                ; 89 da                       ; 0xc263b
    add dx, ax                                ; 01 c2                       ; 0xc263d
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc263f vgabios.c:1650
    xor ah, ah                                ; 30 e4                       ; 0xc2642
    mov bx, ax                                ; 89 c3                       ; 0xc2644
    sal bx, 008h                              ; c1 e3 08                    ; 0xc2646
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2649
    add bx, ax                                ; 01 c3                       ; 0xc264c
    mov word [bp-020h], bx                    ; 89 5e e0                    ; 0xc264e
    mov ax, word [bp-020h]                    ; 8b 46 e0                    ; 0xc2651 vgabios.c:1651
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc2654
    mov cx, si                                ; 89 f1                       ; 0xc2658
    mov di, dx                                ; 89 d7                       ; 0xc265a
    jcxz 02660h                               ; e3 02                       ; 0xc265c
    rep stosw                                 ; f3 ab                       ; 0xc265e
    jmp near 0271bh                           ; e9 b8 00                    ; 0xc2660 vgabios.c:1653
    mov bl, byte [bx+04831h]                  ; 8a 9f 31 48                 ; 0xc2663 vgabios.c:1656
    sal bx, 006h                              ; c1 e3 06                    ; 0xc2667
    mov al, byte [bx+04847h]                  ; 8a 87 47 48                 ; 0xc266a
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc266e
    mov al, byte [di+047b4h]                  ; 8a 85 b4 47                 ; 0xc2671 vgabios.c:1657
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc2675
    dec si                                    ; 4e                          ; 0xc2678 vgabios.c:1658
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc2679
    je short 026ceh                           ; 74 50                       ; 0xc267c
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc267e vgabios.c:1660
    xor bh, bh                                ; 30 ff                       ; 0xc2681
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2683
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc2686
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc268a
    jc short 0269eh                           ; 72 0f                       ; 0xc268d
    jbe short 026a5h                          ; 76 14                       ; 0xc268f
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2691
    je short 026fah                           ; 74 64                       ; 0xc2694
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2696
    je short 026a9h                           ; 74 0e                       ; 0xc2699
    jmp near 02715h                           ; e9 77 00                    ; 0xc269b
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc269e
    je short 026d0h                           ; 74 2d                       ; 0xc26a1
    jmp short 02715h                          ; eb 70                       ; 0xc26a3
    or byte [bp-006h], 001h                   ; 80 4e fa 01                 ; 0xc26a5 vgabios.c:1663
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc26a9 vgabios.c:1665
    xor ah, ah                                ; 30 e4                       ; 0xc26ac
    push ax                                   ; 50                          ; 0xc26ae
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc26af
    push ax                                   ; 50                          ; 0xc26b2
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc26b3
    push ax                                   ; 50                          ; 0xc26b6
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc26b7
    xor ch, ch                                ; 30 ed                       ; 0xc26ba
    mov bl, byte [bp-008h]                    ; 8a 5e f8                    ; 0xc26bc
    xor bh, bh                                ; 30 ff                       ; 0xc26bf
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc26c1
    xor dh, dh                                ; 30 f6                       ; 0xc26c4
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc26c6
    call 022ddh                               ; e8 11 fc                    ; 0xc26c9
    jmp short 02715h                          ; eb 47                       ; 0xc26cc vgabios.c:1666
    jmp short 0271bh                          ; eb 4b                       ; 0xc26ce
    mov al, byte [bp-014h]                    ; 8a 46 ec                    ; 0xc26d0 vgabios.c:1668
    xor ah, ah                                ; 30 e4                       ; 0xc26d3
    push ax                                   ; 50                          ; 0xc26d5
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc26d6
    push ax                                   ; 50                          ; 0xc26d9
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc26da
    xor ch, ch                                ; 30 ed                       ; 0xc26dd
    mov bl, byte [bp-008h]                    ; 8a 5e f8                    ; 0xc26df
    xor bh, bh                                ; 30 ff                       ; 0xc26e2
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc26e4
    xor dh, dh                                ; 30 f6                       ; 0xc26e7
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc26e9
    mov byte [bp-016h], al                    ; 88 46 ea                    ; 0xc26ec
    mov byte [bp-015h], ah                    ; 88 66 eb                    ; 0xc26ef
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc26f2
    call 023eeh                               ; e8 f6 fc                    ; 0xc26f5
    jmp short 02715h                          ; eb 1b                       ; 0xc26f8 vgabios.c:1669
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc26fa vgabios.c:1671
    xor ah, ah                                ; 30 e4                       ; 0xc26fd
    push ax                                   ; 50                          ; 0xc26ff
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc2700
    xor ch, ch                                ; 30 ed                       ; 0xc2703
    mov bl, byte [bp-008h]                    ; 8a 5e f8                    ; 0xc2705
    xor bh, bh                                ; 30 ff                       ; 0xc2708
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc270a
    xor dh, dh                                ; 30 f6                       ; 0xc270d
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc270f
    call 02500h                               ; e8 eb fd                    ; 0xc2712
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc2715 vgabios.c:1678
    jmp near 02678h                           ; e9 5d ff                    ; 0xc2718 vgabios.c:1679
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc271b vgabios.c:1681
    pop di                                    ; 5f                          ; 0xc271e
    pop si                                    ; 5e                          ; 0xc271f
    pop bp                                    ; 5d                          ; 0xc2720
    retn                                      ; c3                          ; 0xc2721
  ; disGetNextSymbol 0xc2722 LB 0x1e10 -> off=0x0 cb=0000000000000181 uValue=00000000000c2722 'biosfn_write_char_only'
biosfn_write_char_only:                      ; 0xc2722 LB 0x181
    push bp                                   ; 55                          ; 0xc2722 vgabios.c:1684
    mov bp, sp                                ; 89 e5                       ; 0xc2723
    push si                                   ; 56                          ; 0xc2725
    push di                                   ; 57                          ; 0xc2726
    sub sp, strict byte 0001ch                ; 83 ec 1c                    ; 0xc2727
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc272a
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc272d
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc2730
    mov si, cx                                ; 89 ce                       ; 0xc2733
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc2735 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2738
    mov es, ax                                ; 8e c0                       ; 0xc273b
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc273d
    xor ah, ah                                ; 30 e4                       ; 0xc2740 vgabios.c:1692
    call 03819h                               ; e8 d4 10                    ; 0xc2742
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc2745
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc2748
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc274b vgabios.c:1693
    jne short 02752h                          ; 75 03                       ; 0xc274d
    jmp near 0289ch                           ; e9 4a 01                    ; 0xc274f
    mov al, dl                                ; 88 d0                       ; 0xc2752 vgabios.c:1696
    xor ah, ah                                ; 30 e4                       ; 0xc2754
    lea bx, [bp-01eh]                         ; 8d 5e e2                    ; 0xc2756
    lea dx, [bp-020h]                         ; 8d 56 e0                    ; 0xc2759
    call 00a96h                               ; e8 37 e3                    ; 0xc275c
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc275f vgabios.c:1697
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2762
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc2765
    xor al, al                                ; 30 c0                       ; 0xc2768
    shr ax, 008h                              ; c1 e8 08                    ; 0xc276a
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc276d
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc2770
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2773
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2776 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2779
    mov es, ax                                ; 8e c0                       ; 0xc277c
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc277e
    xor ah, ah                                ; 30 e4                       ; 0xc2781 vgabios.c:48
    mov dx, ax                                ; 89 c2                       ; 0xc2783
    inc dx                                    ; 42                          ; 0xc2785
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2786 vgabios.c:57
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc2789
    mov word [bp-01ch], cx                    ; 89 4e e4                    ; 0xc278c vgabios.c:58
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc278f vgabios.c:1703
    mov bx, ax                                ; 89 c3                       ; 0xc2792
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2794
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc2797
    jne short 027e0h                          ; 75 42                       ; 0xc279c
    mov ax, cx                                ; 89 c8                       ; 0xc279e vgabios.c:1706
    mul dx                                    ; f7 e2                       ; 0xc27a0
    add ax, ax                                ; 01 c0                       ; 0xc27a2
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc27a4
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc27a6
    xor dh, dh                                ; 30 f6                       ; 0xc27a9
    inc ax                                    ; 40                          ; 0xc27ab
    mul dx                                    ; f7 e2                       ; 0xc27ac
    mov bx, ax                                ; 89 c3                       ; 0xc27ae
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc27b0
    xor ah, ah                                ; 30 e4                       ; 0xc27b3
    mul cx                                    ; f7 e1                       ; 0xc27b5
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc27b7
    xor dh, dh                                ; 30 f6                       ; 0xc27ba
    add ax, dx                                ; 01 d0                       ; 0xc27bc
    add ax, ax                                ; 01 c0                       ; 0xc27be
    add bx, ax                                ; 01 c3                       ; 0xc27c0
    dec si                                    ; 4e                          ; 0xc27c2 vgabios.c:1708
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc27c3
    je short 0274fh                           ; 74 87                       ; 0xc27c6
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc27c8 vgabios.c:1709
    xor ah, ah                                ; 30 e4                       ; 0xc27cb
    mov di, ax                                ; 89 c7                       ; 0xc27cd
    sal di, 003h                              ; c1 e7 03                    ; 0xc27cf
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc27d2 vgabios.c:50
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc27d6 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc27d9
    inc bx                                    ; 43                          ; 0xc27dc vgabios.c:1710
    inc bx                                    ; 43                          ; 0xc27dd
    jmp short 027c2h                          ; eb e2                       ; 0xc27de vgabios.c:1711
    mov di, ax                                ; 89 c7                       ; 0xc27e0 vgabios.c:1716
    mov al, byte [di+04831h]                  ; 8a 85 31 48                 ; 0xc27e2
    mov di, ax                                ; 89 c7                       ; 0xc27e6
    sal di, 006h                              ; c1 e7 06                    ; 0xc27e8
    mov al, byte [di+04847h]                  ; 8a 85 47 48                 ; 0xc27eb
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc27ef
    mov al, byte [bx+047b4h]                  ; 8a 87 b4 47                 ; 0xc27f2 vgabios.c:1717
    mov byte [bp-016h], al                    ; 88 46 ea                    ; 0xc27f6
    dec si                                    ; 4e                          ; 0xc27f9 vgabios.c:1718
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc27fa
    je short 0284fh                           ; 74 50                       ; 0xc27fd
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc27ff vgabios.c:1720
    xor bh, bh                                ; 30 ff                       ; 0xc2802
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2804
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc2807
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc280b
    jc short 0281fh                           ; 72 0f                       ; 0xc280e
    jbe short 02826h                          ; 76 14                       ; 0xc2810
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2812
    je short 0287bh                           ; 74 64                       ; 0xc2815
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2817
    je short 0282ah                           ; 74 0e                       ; 0xc281a
    jmp near 02896h                           ; e9 77 00                    ; 0xc281c
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc281f
    je short 02851h                           ; 74 2d                       ; 0xc2822
    jmp short 02896h                          ; eb 70                       ; 0xc2824
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc2826 vgabios.c:1723
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc282a vgabios.c:1725
    xor ah, ah                                ; 30 e4                       ; 0xc282d
    push ax                                   ; 50                          ; 0xc282f
    mov al, byte [bp-014h]                    ; 8a 46 ec                    ; 0xc2830
    push ax                                   ; 50                          ; 0xc2833
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc2834
    push ax                                   ; 50                          ; 0xc2837
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc2838
    xor ch, ch                                ; 30 ed                       ; 0xc283b
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc283d
    xor bh, bh                                ; 30 ff                       ; 0xc2840
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2842
    xor dh, dh                                ; 30 f6                       ; 0xc2845
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2847
    call 022ddh                               ; e8 90 fa                    ; 0xc284a
    jmp short 02896h                          ; eb 47                       ; 0xc284d vgabios.c:1726
    jmp short 0289ch                          ; eb 4b                       ; 0xc284f
    mov al, byte [bp-016h]                    ; 8a 46 ea                    ; 0xc2851 vgabios.c:1728
    xor ah, ah                                ; 30 e4                       ; 0xc2854
    push ax                                   ; 50                          ; 0xc2856
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc2857
    push ax                                   ; 50                          ; 0xc285a
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc285b
    xor ch, ch                                ; 30 ed                       ; 0xc285e
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2860
    xor bh, bh                                ; 30 ff                       ; 0xc2863
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2865
    xor dh, dh                                ; 30 f6                       ; 0xc2868
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc286a
    mov byte [bp-01ah], al                    ; 88 46 e6                    ; 0xc286d
    mov byte [bp-019h], ah                    ; 88 66 e7                    ; 0xc2870
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc2873
    call 023eeh                               ; e8 75 fb                    ; 0xc2876
    jmp short 02896h                          ; eb 1b                       ; 0xc2879 vgabios.c:1729
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc287b vgabios.c:1731
    xor ah, ah                                ; 30 e4                       ; 0xc287e
    push ax                                   ; 50                          ; 0xc2880
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc2881
    xor ch, ch                                ; 30 ed                       ; 0xc2884
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2886
    xor bh, bh                                ; 30 ff                       ; 0xc2889
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc288b
    xor dh, dh                                ; 30 f6                       ; 0xc288e
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2890
    call 02500h                               ; e8 6a fc                    ; 0xc2893
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc2896 vgabios.c:1738
    jmp near 027f9h                           ; e9 5d ff                    ; 0xc2899 vgabios.c:1739
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc289c vgabios.c:1741
    pop di                                    ; 5f                          ; 0xc289f
    pop si                                    ; 5e                          ; 0xc28a0
    pop bp                                    ; 5d                          ; 0xc28a1
    retn                                      ; c3                          ; 0xc28a2
  ; disGetNextSymbol 0xc28a3 LB 0x1c8f -> off=0x0 cb=0000000000000173 uValue=00000000000c28a3 'biosfn_write_pixel'
biosfn_write_pixel:                          ; 0xc28a3 LB 0x173
    push bp                                   ; 55                          ; 0xc28a3 vgabios.c:1744
    mov bp, sp                                ; 89 e5                       ; 0xc28a4
    push si                                   ; 56                          ; 0xc28a6
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc28a7
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc28aa
    mov byte [bp-004h], dl                    ; 88 56 fc                    ; 0xc28ad
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc28b0
    mov dx, cx                                ; 89 ca                       ; 0xc28b3
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc28b5 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc28b8
    mov es, ax                                ; 8e c0                       ; 0xc28bb
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc28bd
    xor ah, ah                                ; 30 e4                       ; 0xc28c0 vgabios.c:1751
    call 03819h                               ; e8 54 0f                    ; 0xc28c2
    mov cl, al                                ; 88 c1                       ; 0xc28c5
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc28c7 vgabios.c:1752
    je short 028f1h                           ; 74 26                       ; 0xc28c9
    mov bl, al                                ; 88 c3                       ; 0xc28cb vgabios.c:1753
    xor bh, bh                                ; 30 ff                       ; 0xc28cd
    sal bx, 003h                              ; c1 e3 03                    ; 0xc28cf
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc28d2
    je short 028f1h                           ; 74 18                       ; 0xc28d7
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc28d9 vgabios.c:1755
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc28dd
    jc short 028edh                           ; 72 0c                       ; 0xc28df
    jbe short 028f7h                          ; 76 14                       ; 0xc28e1
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc28e3
    je short 028f4h                           ; 74 0d                       ; 0xc28e5
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc28e7
    je short 028f7h                           ; 74 0c                       ; 0xc28e9
    jmp short 028f1h                          ; eb 04                       ; 0xc28eb
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc28ed
    je short 02968h                           ; 74 77                       ; 0xc28ef
    jmp near 02a10h                           ; e9 1c 01                    ; 0xc28f1
    jmp near 029eeh                           ; e9 f7 00                    ; 0xc28f4
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc28f7 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc28fa
    mov es, ax                                ; 8e c0                       ; 0xc28fd
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc28ff
    mov ax, dx                                ; 89 d0                       ; 0xc2902 vgabios.c:58
    mul bx                                    ; f7 e3                       ; 0xc2904
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2906
    shr bx, 003h                              ; c1 eb 03                    ; 0xc2909
    add bx, ax                                ; 01 c3                       ; 0xc290c
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc290e vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc2911
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2914 vgabios.c:58
    xor dh, dh                                ; 30 f6                       ; 0xc2917
    mul dx                                    ; f7 e2                       ; 0xc2919
    add bx, ax                                ; 01 c3                       ; 0xc291b
    mov cx, word [bp-008h]                    ; 8b 4e f8                    ; 0xc291d vgabios.c:1761
    and cl, 007h                              ; 80 e1 07                    ; 0xc2920
    mov ax, 00080h                            ; b8 80 00                    ; 0xc2923
    sar ax, CL                                ; d3 f8                       ; 0xc2926
    xor ah, ah                                ; 30 e4                       ; 0xc2928 vgabios.c:1762
    sal ax, 008h                              ; c1 e0 08                    ; 0xc292a
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc292d
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc292f
    out DX, ax                                ; ef                          ; 0xc2932
    mov ax, 00205h                            ; b8 05 02                    ; 0xc2933 vgabios.c:1763
    out DX, ax                                ; ef                          ; 0xc2936
    mov dx, bx                                ; 89 da                       ; 0xc2937 vgabios.c:1764
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2939
    call 03841h                               ; e8 02 0f                    ; 0xc293c
    test byte [bp-004h], 080h                 ; f6 46 fc 80                 ; 0xc293f vgabios.c:1765
    je short 0294ch                           ; 74 07                       ; 0xc2943
    mov ax, 01803h                            ; b8 03 18                    ; 0xc2945 vgabios.c:1767
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2948
    out DX, ax                                ; ef                          ; 0xc294b
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc294c vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc294f
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2951
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2954
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc2957 vgabios.c:1770
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc295a
    out DX, ax                                ; ef                          ; 0xc295d
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc295e vgabios.c:1771
    out DX, ax                                ; ef                          ; 0xc2961
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc2962 vgabios.c:1772
    out DX, ax                                ; ef                          ; 0xc2965
    jmp short 028f1h                          ; eb 89                       ; 0xc2966 vgabios.c:1773
    mov ax, dx                                ; 89 d0                       ; 0xc2968 vgabios.c:1775
    shr ax, 1                                 ; d1 e8                       ; 0xc296a
    imul ax, ax, strict byte 00050h           ; 6b c0 50                    ; 0xc296c
    cmp byte [bx+047b4h], 002h                ; 80 bf b4 47 02              ; 0xc296f
    jne short 0297eh                          ; 75 08                       ; 0xc2974
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2976 vgabios.c:1777
    shr bx, 002h                              ; c1 eb 02                    ; 0xc2979
    jmp short 02984h                          ; eb 06                       ; 0xc297c vgabios.c:1779
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc297e vgabios.c:1781
    shr bx, 003h                              ; c1 eb 03                    ; 0xc2981
    add bx, ax                                ; 01 c3                       ; 0xc2984
    test dl, 001h                             ; f6 c2 01                    ; 0xc2986 vgabios.c:1783
    je short 0298eh                           ; 74 03                       ; 0xc2989
    add bh, 020h                              ; 80 c7 20                    ; 0xc298b
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc298e vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc2991
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc2993
    mov al, cl                                ; 88 c8                       ; 0xc2996 vgabios.c:1785
    xor ah, ah                                ; 30 e4                       ; 0xc2998
    mov si, ax                                ; 89 c6                       ; 0xc299a
    sal si, 003h                              ; c1 e6 03                    ; 0xc299c
    cmp byte [si+047b4h], 002h                ; 80 bc b4 47 02              ; 0xc299f
    jne short 029bfh                          ; 75 19                       ; 0xc29a4
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc29a6 vgabios.c:1787
    and AL, strict byte 003h                  ; 24 03                       ; 0xc29a9
    mov AH, strict byte 003h                  ; b4 03                       ; 0xc29ab
    sub ah, al                                ; 28 c4                       ; 0xc29ad
    mov cl, ah                                ; 88 e1                       ; 0xc29af
    add cl, ah                                ; 00 e1                       ; 0xc29b1
    mov dh, byte [bp-004h]                    ; 8a 76 fc                    ; 0xc29b3
    and dh, 003h                              ; 80 e6 03                    ; 0xc29b6
    sal dh, CL                                ; d2 e6                       ; 0xc29b9
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc29bb vgabios.c:1788
    jmp short 029d2h                          ; eb 13                       ; 0xc29bd vgabios.c:1790
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc29bf vgabios.c:1792
    and AL, strict byte 007h                  ; 24 07                       ; 0xc29c2
    mov CL, strict byte 007h                  ; b1 07                       ; 0xc29c4
    sub cl, al                                ; 28 c1                       ; 0xc29c6
    mov dh, byte [bp-004h]                    ; 8a 76 fc                    ; 0xc29c8
    and dh, 001h                              ; 80 e6 01                    ; 0xc29cb
    sal dh, CL                                ; d2 e6                       ; 0xc29ce
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc29d0 vgabios.c:1793
    sal al, CL                                ; d2 e0                       ; 0xc29d2
    test byte [bp-004h], 080h                 ; f6 46 fc 80                 ; 0xc29d4 vgabios.c:1795
    je short 029deh                           ; 74 04                       ; 0xc29d8
    xor dl, dh                                ; 30 f2                       ; 0xc29da vgabios.c:1797
    jmp short 029e4h                          ; eb 06                       ; 0xc29dc vgabios.c:1799
    not al                                    ; f6 d0                       ; 0xc29de vgabios.c:1801
    and dl, al                                ; 20 c2                       ; 0xc29e0
    or dl, dh                                 ; 08 f2                       ; 0xc29e2 vgabios.c:1802
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc29e4 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc29e7
    mov byte [es:bx], dl                      ; 26 88 17                    ; 0xc29e9
    jmp short 02a10h                          ; eb 22                       ; 0xc29ec vgabios.c:1805
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc29ee vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc29f1
    mov es, ax                                ; 8e c0                       ; 0xc29f4
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc29f6
    sal bx, 003h                              ; c1 e3 03                    ; 0xc29f9 vgabios.c:58
    mov ax, dx                                ; 89 d0                       ; 0xc29fc
    mul bx                                    ; f7 e3                       ; 0xc29fe
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2a00
    add bx, ax                                ; 01 c3                       ; 0xc2a03
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2a05 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2a08
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2a0a
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2a0d
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2a10 vgabios.c:1815
    pop si                                    ; 5e                          ; 0xc2a13
    pop bp                                    ; 5d                          ; 0xc2a14
    retn                                      ; c3                          ; 0xc2a15
  ; disGetNextSymbol 0xc2a16 LB 0x1b1c -> off=0x0 cb=0000000000000258 uValue=00000000000c2a16 'biosfn_write_teletype'
biosfn_write_teletype:                       ; 0xc2a16 LB 0x258
    push bp                                   ; 55                          ; 0xc2a16 vgabios.c:1818
    mov bp, sp                                ; 89 e5                       ; 0xc2a17
    push si                                   ; 56                          ; 0xc2a19
    sub sp, strict byte 00014h                ; 83 ec 14                    ; 0xc2a1a
    mov ch, al                                ; 88 c5                       ; 0xc2a1d
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc2a1f
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc2a22
    cmp dl, 0ffh                              ; 80 fa ff                    ; 0xc2a25 vgabios.c:1826
    jne short 02a38h                          ; 75 0e                       ; 0xc2a28
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc2a2a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2a2d
    mov es, ax                                ; 8e c0                       ; 0xc2a30
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2a32
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2a35 vgabios.c:48
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc2a38 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2a3b
    mov es, ax                                ; 8e c0                       ; 0xc2a3e
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2a40
    xor ah, ah                                ; 30 e4                       ; 0xc2a43 vgabios.c:1831
    call 03819h                               ; e8 d1 0d                    ; 0xc2a45
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2a48
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc2a4b vgabios.c:1832
    je short 02ab5h                           ; 74 66                       ; 0xc2a4d
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2a4f vgabios.c:1835
    xor ah, ah                                ; 30 e4                       ; 0xc2a52
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc2a54
    lea dx, [bp-016h]                         ; 8d 56 ea                    ; 0xc2a57
    call 00a96h                               ; e8 39 e0                    ; 0xc2a5a
    mov al, byte [bp-014h]                    ; 8a 46 ec                    ; 0xc2a5d vgabios.c:1836
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc2a60
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc2a63
    xor al, al                                ; 30 c0                       ; 0xc2a66
    shr ax, 008h                              ; c1 e8 08                    ; 0xc2a68
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2a6b
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2a6e vgabios.c:47
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2a71
    mov es, dx                                ; 8e c2                       ; 0xc2a74
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc2a76
    xor dh, dh                                ; 30 f6                       ; 0xc2a79 vgabios.c:48
    inc dx                                    ; 42                          ; 0xc2a7b
    mov word [bp-012h], dx                    ; 89 56 ee                    ; 0xc2a7c
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2a7f vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc2a82
    mov word [bp-010h], dx                    ; 89 56 f0                    ; 0xc2a85 vgabios.c:58
    cmp ch, 008h                              ; 80 fd 08                    ; 0xc2a88 vgabios.c:1842
    jc short 02a9bh                           ; 72 0e                       ; 0xc2a8b
    jbe short 02aa3h                          ; 76 14                       ; 0xc2a8d
    cmp ch, 00dh                              ; 80 fd 0d                    ; 0xc2a8f
    je short 02ab8h                           ; 74 24                       ; 0xc2a92
    cmp ch, 00ah                              ; 80 fd 0a                    ; 0xc2a94
    je short 02aaeh                           ; 74 15                       ; 0xc2a97
    jmp short 02abfh                          ; eb 24                       ; 0xc2a99
    cmp ch, 007h                              ; 80 fd 07                    ; 0xc2a9b
    jne short 02abfh                          ; 75 1f                       ; 0xc2a9e
    jmp near 02bc5h                           ; e9 22 01                    ; 0xc2aa0
    cmp byte [bp-004h], 000h                  ; 80 7e fc 00                 ; 0xc2aa3 vgabios.c:1849
    jbe short 02abch                          ; 76 13                       ; 0xc2aa7
    dec byte [bp-004h]                        ; fe 4e fc                    ; 0xc2aa9
    jmp short 02abch                          ; eb 0e                       ; 0xc2aac vgabios.c:1850
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc2aae vgabios.c:1853
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2ab0
    jmp short 02abch                          ; eb 07                       ; 0xc2ab3 vgabios.c:1854
    jmp near 02c68h                           ; e9 b0 01                    ; 0xc2ab5
    mov byte [bp-004h], 000h                  ; c6 46 fc 00                 ; 0xc2ab8 vgabios.c:1857
    jmp near 02bc5h                           ; e9 06 01                    ; 0xc2abc vgabios.c:1858
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2abf vgabios.c:1862
    xor ah, ah                                ; 30 e4                       ; 0xc2ac2
    mov bx, ax                                ; 89 c3                       ; 0xc2ac4
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2ac6
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc2ac9
    jne short 02b12h                          ; 75 42                       ; 0xc2ace
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc2ad0 vgabios.c:1865
    mul word [bp-012h]                        ; f7 66 ee                    ; 0xc2ad3
    add ax, ax                                ; 01 c0                       ; 0xc2ad6
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2ad8
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2ada
    xor dh, dh                                ; 30 f6                       ; 0xc2add
    inc ax                                    ; 40                          ; 0xc2adf
    mul dx                                    ; f7 e2                       ; 0xc2ae0
    mov si, ax                                ; 89 c6                       ; 0xc2ae2
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2ae4
    xor ah, ah                                ; 30 e4                       ; 0xc2ae7
    mul word [bp-010h]                        ; f7 66 f0                    ; 0xc2ae9
    mov dx, ax                                ; 89 c2                       ; 0xc2aec
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2aee
    xor ah, ah                                ; 30 e4                       ; 0xc2af1
    add ax, dx                                ; 01 d0                       ; 0xc2af3
    add ax, ax                                ; 01 c0                       ; 0xc2af5
    add si, ax                                ; 01 c6                       ; 0xc2af7
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2af9 vgabios.c:50
    mov byte [es:si], ch                      ; 26 88 2c                    ; 0xc2afd vgabios.c:52
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc2b00 vgabios.c:1870
    jne short 02b41h                          ; 75 3c                       ; 0xc2b03
    inc si                                    ; 46                          ; 0xc2b05 vgabios.c:1871
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2b06 vgabios.c:50
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2b0a
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2b0d
    jmp short 02b41h                          ; eb 2f                       ; 0xc2b10 vgabios.c:1873
    mov si, ax                                ; 89 c6                       ; 0xc2b12 vgabios.c:1876
    mov al, byte [si+04831h]                  ; 8a 84 31 48                 ; 0xc2b14
    mov si, ax                                ; 89 c6                       ; 0xc2b18
    sal si, 006h                              ; c1 e6 06                    ; 0xc2b1a
    mov dl, byte [si+04847h]                  ; 8a 94 47 48                 ; 0xc2b1d
    mov al, byte [bx+047b4h]                  ; 8a 87 b4 47                 ; 0xc2b21 vgabios.c:1877
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc2b25 vgabios.c:1878
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc2b29
    jc short 02b3ch                           ; 72 0e                       ; 0xc2b2c
    jbe short 02b43h                          ; 76 13                       ; 0xc2b2e
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2b30
    je short 02b93h                           ; 74 5e                       ; 0xc2b33
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2b35
    je short 02b47h                           ; 74 0d                       ; 0xc2b38
    jmp short 02bb2h                          ; eb 76                       ; 0xc2b3a
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2b3c
    je short 02b71h                           ; 74 30                       ; 0xc2b3f
    jmp short 02bb2h                          ; eb 6f                       ; 0xc2b41
    or byte [bp-00ah], 001h                   ; 80 4e f6 01                 ; 0xc2b43 vgabios.c:1881
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2b47 vgabios.c:1883
    xor ah, ah                                ; 30 e4                       ; 0xc2b4a
    push ax                                   ; 50                          ; 0xc2b4c
    mov al, dl                                ; 88 d0                       ; 0xc2b4d
    push ax                                   ; 50                          ; 0xc2b4f
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2b50
    push ax                                   ; 50                          ; 0xc2b53
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2b54
    mov bl, byte [bp-004h]                    ; 8a 5e fc                    ; 0xc2b57
    xor bh, bh                                ; 30 ff                       ; 0xc2b5a
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2b5c
    xor dh, dh                                ; 30 f6                       ; 0xc2b5f
    mov byte [bp-00eh], ch                    ; 88 6e f2                    ; 0xc2b61
    mov byte [bp-00dh], ah                    ; 88 66 f3                    ; 0xc2b64
    mov cx, ax                                ; 89 c1                       ; 0xc2b67
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2b69
    call 022ddh                               ; e8 6e f7                    ; 0xc2b6c
    jmp short 02bb2h                          ; eb 41                       ; 0xc2b6f vgabios.c:1884
    push ax                                   ; 50                          ; 0xc2b71 vgabios.c:1886
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2b72
    push ax                                   ; 50                          ; 0xc2b75
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2b76
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc2b79
    mov byte [bp-00dh], ah                    ; 88 66 f3                    ; 0xc2b7c
    mov bl, byte [bp-004h]                    ; 8a 5e fc                    ; 0xc2b7f
    xor bh, bh                                ; 30 ff                       ; 0xc2b82
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2b84
    xor dh, dh                                ; 30 f6                       ; 0xc2b87
    mov al, ch                                ; 88 e8                       ; 0xc2b89
    mov cx, word [bp-00eh]                    ; 8b 4e f2                    ; 0xc2b8b
    call 023eeh                               ; e8 5d f8                    ; 0xc2b8e
    jmp short 02bb2h                          ; eb 1f                       ; 0xc2b91 vgabios.c:1887
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2b93 vgabios.c:1889
    push ax                                   ; 50                          ; 0xc2b96
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2b97
    mov bl, byte [bp-004h]                    ; 8a 5e fc                    ; 0xc2b9a
    xor bh, bh                                ; 30 ff                       ; 0xc2b9d
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2b9f
    xor dh, dh                                ; 30 f6                       ; 0xc2ba2
    mov byte [bp-00eh], ch                    ; 88 6e f2                    ; 0xc2ba4
    mov byte [bp-00dh], ah                    ; 88 66 f3                    ; 0xc2ba7
    mov cx, ax                                ; 89 c1                       ; 0xc2baa
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2bac
    call 02500h                               ; e8 4e f9                    ; 0xc2baf
    inc byte [bp-004h]                        ; fe 46 fc                    ; 0xc2bb2 vgabios.c:1897
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2bb5 vgabios.c:1899
    xor ah, ah                                ; 30 e4                       ; 0xc2bb8
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc2bba
    jne short 02bc5h                          ; 75 06                       ; 0xc2bbd
    mov byte [bp-004h], ah                    ; 88 66 fc                    ; 0xc2bbf vgabios.c:1900
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc2bc2 vgabios.c:1901
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2bc5 vgabios.c:1906
    xor ah, ah                                ; 30 e4                       ; 0xc2bc8
    cmp ax, word [bp-012h]                    ; 3b 46 ee                    ; 0xc2bca
    jne short 02c30h                          ; 75 61                       ; 0xc2bcd
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc2bcf vgabios.c:1908
    xor bh, bh                                ; 30 ff                       ; 0xc2bd2
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2bd4
    mov ch, byte [bp-012h]                    ; 8a 6e ee                    ; 0xc2bd7
    db  0feh, 0cdh
    ; dec ch                                    ; fe cd                     ; 0xc2bda
    mov cl, byte [bp-010h]                    ; 8a 4e f0                    ; 0xc2bdc
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc2bdf
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc2be1
    jne short 02c32h                          ; 75 4a                       ; 0xc2be6
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc2be8 vgabios.c:1910
    mul word [bp-012h]                        ; f7 66 ee                    ; 0xc2beb
    add ax, ax                                ; 01 c0                       ; 0xc2bee
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2bf0
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2bf2
    xor dh, dh                                ; 30 f6                       ; 0xc2bf5
    inc ax                                    ; 40                          ; 0xc2bf7
    mul dx                                    ; f7 e2                       ; 0xc2bf8
    mov si, ax                                ; 89 c6                       ; 0xc2bfa
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2bfc
    xor ah, ah                                ; 30 e4                       ; 0xc2bff
    dec ax                                    ; 48                          ; 0xc2c01
    mul word [bp-010h]                        ; f7 66 f0                    ; 0xc2c02
    mov dx, ax                                ; 89 c2                       ; 0xc2c05
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2c07
    xor ah, ah                                ; 30 e4                       ; 0xc2c0a
    add ax, dx                                ; 01 d0                       ; 0xc2c0c
    add ax, ax                                ; 01 c0                       ; 0xc2c0e
    add si, ax                                ; 01 c6                       ; 0xc2c10
    inc si                                    ; 46                          ; 0xc2c12 vgabios.c:1911
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2c13 vgabios.c:45
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc2c17
    push strict byte 00001h                   ; 6a 01                       ; 0xc2c1a vgabios.c:1912
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c1c
    xor ah, ah                                ; 30 e4                       ; 0xc2c1f
    push ax                                   ; 50                          ; 0xc2c21
    mov al, cl                                ; 88 c8                       ; 0xc2c22
    push ax                                   ; 50                          ; 0xc2c24
    mov al, ch                                ; 88 e8                       ; 0xc2c25
    push ax                                   ; 50                          ; 0xc2c27
    xor dh, dh                                ; 30 f6                       ; 0xc2c28
    xor cx, cx                                ; 31 c9                       ; 0xc2c2a
    xor bx, bx                                ; 31 db                       ; 0xc2c2c
    jmp short 02c44h                          ; eb 14                       ; 0xc2c2e vgabios.c:1914
    jmp short 02c4dh                          ; eb 1b                       ; 0xc2c30
    push strict byte 00001h                   ; 6a 01                       ; 0xc2c32 vgabios.c:1916
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c34
    push ax                                   ; 50                          ; 0xc2c37
    mov al, cl                                ; 88 c8                       ; 0xc2c38
    push ax                                   ; 50                          ; 0xc2c3a
    mov al, ch                                ; 88 e8                       ; 0xc2c3b
    push ax                                   ; 50                          ; 0xc2c3d
    xor cx, cx                                ; 31 c9                       ; 0xc2c3e
    xor bx, bx                                ; 31 db                       ; 0xc2c40
    xor dx, dx                                ; 31 d2                       ; 0xc2c42
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc2c44
    call 01c55h                               ; e8 0b f0                    ; 0xc2c47
    dec byte [bp-006h]                        ; fe 4e fa                    ; 0xc2c4a vgabios.c:1918
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2c4d vgabios.c:1922
    xor ah, ah                                ; 30 e4                       ; 0xc2c50
    mov word [bp-014h], ax                    ; 89 46 ec                    ; 0xc2c52
    sal word [bp-014h], 008h                  ; c1 66 ec 08                 ; 0xc2c55
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2c59
    add word [bp-014h], ax                    ; 01 46 ec                    ; 0xc2c5c
    mov dx, word [bp-014h]                    ; 8b 56 ec                    ; 0xc2c5f vgabios.c:1923
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c62
    call 01293h                               ; e8 2b e6                    ; 0xc2c65
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2c68 vgabios.c:1924
    pop si                                    ; 5e                          ; 0xc2c6b
    pop bp                                    ; 5d                          ; 0xc2c6c
    retn                                      ; c3                          ; 0xc2c6d
  ; disGetNextSymbol 0xc2c6e LB 0x18c4 -> off=0x0 cb=0000000000000033 uValue=00000000000c2c6e 'get_font_access'
get_font_access:                             ; 0xc2c6e LB 0x33
    push bp                                   ; 55                          ; 0xc2c6e vgabios.c:1927
    mov bp, sp                                ; 89 e5                       ; 0xc2c6f
    push dx                                   ; 52                          ; 0xc2c71
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2c72 vgabios.c:1929
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2c75
    out DX, ax                                ; ef                          ; 0xc2c78
    mov AL, strict byte 006h                  ; b0 06                       ; 0xc2c79 vgabios.c:1930
    out DX, AL                                ; ee                          ; 0xc2c7b
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc2c7c vgabios.c:1931
    in AL, DX                                 ; ec                          ; 0xc2c7f
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2c80
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2c82
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc2c85
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2c87
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2c8a
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2c8c
    out DX, ax                                ; ef                          ; 0xc2c8f
    mov ax, 00402h                            ; b8 02 04                    ; 0xc2c90 vgabios.c:1932
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2c93
    out DX, ax                                ; ef                          ; 0xc2c96
    mov ax, 00604h                            ; b8 04 06                    ; 0xc2c97 vgabios.c:1933
    out DX, ax                                ; ef                          ; 0xc2c9a
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2c9b vgabios.c:1934
    pop dx                                    ; 5a                          ; 0xc2c9e
    pop bp                                    ; 5d                          ; 0xc2c9f
    retn                                      ; c3                          ; 0xc2ca0
  ; disGetNextSymbol 0xc2ca1 LB 0x1891 -> off=0x0 cb=0000000000000030 uValue=00000000000c2ca1 'release_font_access'
release_font_access:                         ; 0xc2ca1 LB 0x30
    push bp                                   ; 55                          ; 0xc2ca1 vgabios.c:1936
    mov bp, sp                                ; 89 e5                       ; 0xc2ca2
    push dx                                   ; 52                          ; 0xc2ca4
    mov dx, 003cch                            ; ba cc 03                    ; 0xc2ca5 vgabios.c:1938
    in AL, DX                                 ; ec                          ; 0xc2ca8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2ca9
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2cab
    sal ax, 002h                              ; c1 e0 02                    ; 0xc2cae
    or AL, strict byte 00ah                   ; 0c 0a                       ; 0xc2cb1
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2cb3
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2cb6
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2cb8
    out DX, ax                                ; ef                          ; 0xc2cbb
    mov ax, 01005h                            ; b8 05 10                    ; 0xc2cbc vgabios.c:1939
    out DX, ax                                ; ef                          ; 0xc2cbf
    mov ax, 00302h                            ; b8 02 03                    ; 0xc2cc0 vgabios.c:1940
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2cc3
    out DX, ax                                ; ef                          ; 0xc2cc6
    mov ax, 00204h                            ; b8 04 02                    ; 0xc2cc7 vgabios.c:1941
    out DX, ax                                ; ef                          ; 0xc2cca
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2ccb vgabios.c:1942
    pop dx                                    ; 5a                          ; 0xc2cce
    pop bp                                    ; 5d                          ; 0xc2ccf
    retn                                      ; c3                          ; 0xc2cd0
  ; disGetNextSymbol 0xc2cd1 LB 0x1861 -> off=0x0 cb=00000000000000b1 uValue=00000000000c2cd1 'set_scan_lines'
set_scan_lines:                              ; 0xc2cd1 LB 0xb1
    push bp                                   ; 55                          ; 0xc2cd1 vgabios.c:1944
    mov bp, sp                                ; 89 e5                       ; 0xc2cd2
    push bx                                   ; 53                          ; 0xc2cd4
    push cx                                   ; 51                          ; 0xc2cd5
    push dx                                   ; 52                          ; 0xc2cd6
    push si                                   ; 56                          ; 0xc2cd7
    push di                                   ; 57                          ; 0xc2cd8
    mov bl, al                                ; 88 c3                       ; 0xc2cd9
    mov si, strict word 00063h                ; be 63 00                    ; 0xc2cdb vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2cde
    mov es, ax                                ; 8e c0                       ; 0xc2ce1
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc2ce3
    mov cx, si                                ; 89 f1                       ; 0xc2ce6 vgabios.c:58
    mov AL, strict byte 009h                  ; b0 09                       ; 0xc2ce8 vgabios.c:1950
    mov dx, si                                ; 89 f2                       ; 0xc2cea
    out DX, AL                                ; ee                          ; 0xc2cec
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc2ced vgabios.c:1951
    in AL, DX                                 ; ec                          ; 0xc2cf0
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2cf1
    mov ah, al                                ; 88 c4                       ; 0xc2cf3 vgabios.c:1952
    and ah, 0e0h                              ; 80 e4 e0                    ; 0xc2cf5
    mov al, bl                                ; 88 d8                       ; 0xc2cf8
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc2cfa
    or al, ah                                 ; 08 e0                       ; 0xc2cfc
    out DX, AL                                ; ee                          ; 0xc2cfe vgabios.c:1953
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc2cff vgabios.c:1954
    jne short 02d0ch                          ; 75 08                       ; 0xc2d02
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc2d04 vgabios.c:1956
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc2d07
    jmp short 02d19h                          ; eb 0d                       ; 0xc2d0a vgabios.c:1958
    mov dl, bl                                ; 88 da                       ; 0xc2d0c vgabios.c:1960
    sub dl, 003h                              ; 80 ea 03                    ; 0xc2d0e
    xor dh, dh                                ; 30 f6                       ; 0xc2d11
    mov al, bl                                ; 88 d8                       ; 0xc2d13
    sub AL, strict byte 004h                  ; 2c 04                       ; 0xc2d15
    xor ah, ah                                ; 30 e4                       ; 0xc2d17
    call 0118ch                               ; e8 70 e4                    ; 0xc2d19
    xor bh, bh                                ; 30 ff                       ; 0xc2d1c vgabios.c:1962
    mov si, 00085h                            ; be 85 00                    ; 0xc2d1e vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2d21
    mov es, ax                                ; 8e c0                       ; 0xc2d24
    mov word [es:si], bx                      ; 26 89 1c                    ; 0xc2d26
    mov AL, strict byte 012h                  ; b0 12                       ; 0xc2d29 vgabios.c:1963
    mov dx, cx                                ; 89 ca                       ; 0xc2d2b
    out DX, AL                                ; ee                          ; 0xc2d2d
    mov si, cx                                ; 89 ce                       ; 0xc2d2e vgabios.c:1964
    inc si                                    ; 46                          ; 0xc2d30
    mov dx, si                                ; 89 f2                       ; 0xc2d31
    in AL, DX                                 ; ec                          ; 0xc2d33
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2d34
    mov di, ax                                ; 89 c7                       ; 0xc2d36
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc2d38 vgabios.c:1965
    mov dx, cx                                ; 89 ca                       ; 0xc2d3a
    out DX, AL                                ; ee                          ; 0xc2d3c
    mov dx, si                                ; 89 f2                       ; 0xc2d3d vgabios.c:1966
    in AL, DX                                 ; ec                          ; 0xc2d3f
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2d40
    mov dl, al                                ; 88 c2                       ; 0xc2d42 vgabios.c:1967
    and dl, 002h                              ; 80 e2 02                    ; 0xc2d44
    xor dh, dh                                ; 30 f6                       ; 0xc2d47
    sal dx, 007h                              ; c1 e2 07                    ; 0xc2d49
    and AL, strict byte 040h                  ; 24 40                       ; 0xc2d4c
    xor ah, ah                                ; 30 e4                       ; 0xc2d4e
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2d50
    add ax, dx                                ; 01 d0                       ; 0xc2d53
    inc ax                                    ; 40                          ; 0xc2d55
    add ax, di                                ; 01 f8                       ; 0xc2d56
    xor dx, dx                                ; 31 d2                       ; 0xc2d58 vgabios.c:1968
    div bx                                    ; f7 f3                       ; 0xc2d5a
    mov dl, al                                ; 88 c2                       ; 0xc2d5c vgabios.c:1969
    db  0feh, 0cah
    ; dec dl                                    ; fe ca                     ; 0xc2d5e
    mov si, 00084h                            ; be 84 00                    ; 0xc2d60 vgabios.c:52
    mov byte [es:si], dl                      ; 26 88 14                    ; 0xc2d63
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc2d66 vgabios.c:57
    mov dx, word [es:si]                      ; 26 8b 14                    ; 0xc2d69
    xor ah, ah                                ; 30 e4                       ; 0xc2d6c vgabios.c:1971
    mul dx                                    ; f7 e2                       ; 0xc2d6e
    add ax, ax                                ; 01 c0                       ; 0xc2d70
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc2d72 vgabios.c:62
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc2d75
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc2d78 vgabios.c:1972
    pop di                                    ; 5f                          ; 0xc2d7b
    pop si                                    ; 5e                          ; 0xc2d7c
    pop dx                                    ; 5a                          ; 0xc2d7d
    pop cx                                    ; 59                          ; 0xc2d7e
    pop bx                                    ; 5b                          ; 0xc2d7f
    pop bp                                    ; 5d                          ; 0xc2d80
    retn                                      ; c3                          ; 0xc2d81
  ; disGetNextSymbol 0xc2d82 LB 0x17b0 -> off=0x0 cb=0000000000000023 uValue=00000000000c2d82 'biosfn_set_font_block'
biosfn_set_font_block:                       ; 0xc2d82 LB 0x23
    push bp                                   ; 55                          ; 0xc2d82 vgabios.c:1974
    mov bp, sp                                ; 89 e5                       ; 0xc2d83
    push bx                                   ; 53                          ; 0xc2d85
    push dx                                   ; 52                          ; 0xc2d86
    mov bl, al                                ; 88 c3                       ; 0xc2d87
    mov ax, 00100h                            ; b8 00 01                    ; 0xc2d89 vgabios.c:1976
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2d8c
    out DX, ax                                ; ef                          ; 0xc2d8f
    mov al, bl                                ; 88 d8                       ; 0xc2d90 vgabios.c:1977
    xor ah, ah                                ; 30 e4                       ; 0xc2d92
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2d94
    or AL, strict byte 003h                   ; 0c 03                       ; 0xc2d97
    out DX, ax                                ; ef                          ; 0xc2d99
    mov ax, 00300h                            ; b8 00 03                    ; 0xc2d9a vgabios.c:1978
    out DX, ax                                ; ef                          ; 0xc2d9d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2d9e vgabios.c:1979
    pop dx                                    ; 5a                          ; 0xc2da1
    pop bx                                    ; 5b                          ; 0xc2da2
    pop bp                                    ; 5d                          ; 0xc2da3
    retn                                      ; c3                          ; 0xc2da4
  ; disGetNextSymbol 0xc2da5 LB 0x178d -> off=0x0 cb=0000000000000075 uValue=00000000000c2da5 'load_text_patch'
load_text_patch:                             ; 0xc2da5 LB 0x75
    push bp                                   ; 55                          ; 0xc2da5 vgabios.c:1981
    mov bp, sp                                ; 89 e5                       ; 0xc2da6
    push si                                   ; 56                          ; 0xc2da8
    push di                                   ; 57                          ; 0xc2da9
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc2daa
    push ax                                   ; 50                          ; 0xc2dad
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc2dae
    call 02c6eh                               ; e8 ba fe                    ; 0xc2db1 vgabios.c:1986
    mov al, bl                                ; 88 d8                       ; 0xc2db4 vgabios.c:1988
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2db6
    xor ah, ah                                ; 30 e4                       ; 0xc2db8
    mov cx, ax                                ; 89 c1                       ; 0xc2dba
    sal cx, 00eh                              ; c1 e1 0e                    ; 0xc2dbc
    mov al, bl                                ; 88 d8                       ; 0xc2dbf
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2dc1
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2dc3
    add cx, ax                                ; 01 c1                       ; 0xc2dc6
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc2dc8
    mov bx, dx                                ; 89 d3                       ; 0xc2dcb vgabios.c:1989
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2dcd
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc2dd0
    inc dx                                    ; 42                          ; 0xc2dd3 vgabios.c:1990
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc2dd4
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc2dd7 vgabios.c:1991
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2dda
    test al, al                               ; 84 c0                       ; 0xc2ddd
    je short 02e10h                           ; 74 2f                       ; 0xc2ddf
    xor ah, ah                                ; 30 e4                       ; 0xc2de1 vgabios.c:1992
    sal ax, 005h                              ; c1 e0 05                    ; 0xc2de3
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc2de6
    add di, ax                                ; 01 c7                       ; 0xc2de9
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc2deb vgabios.c:1993
    xor ch, ch                                ; 30 ed                       ; 0xc2dee
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc2df0
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2df3
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2df6
    mov es, ax                                ; 8e c0                       ; 0xc2df9
    jcxz 02e03h                               ; e3 06                       ; 0xc2dfb
    push DS                                   ; 1e                          ; 0xc2dfd
    mov ds, dx                                ; 8e da                       ; 0xc2dfe
    rep movsb                                 ; f3 a4                       ; 0xc2e00
    pop DS                                    ; 1f                          ; 0xc2e02
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2e03 vgabios.c:1994
    xor ah, ah                                ; 30 e4                       ; 0xc2e06
    inc ax                                    ; 40                          ; 0xc2e08
    add word [bp-00ch], ax                    ; 01 46 f4                    ; 0xc2e09
    add bx, ax                                ; 01 c3                       ; 0xc2e0c vgabios.c:1995
    jmp short 02dd7h                          ; eb c7                       ; 0xc2e0e vgabios.c:1996
    call 02ca1h                               ; e8 8e fe                    ; 0xc2e10 vgabios.c:1998
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2e13 vgabios.c:1999
    pop di                                    ; 5f                          ; 0xc2e16
    pop si                                    ; 5e                          ; 0xc2e17
    pop bp                                    ; 5d                          ; 0xc2e18
    retn                                      ; c3                          ; 0xc2e19
  ; disGetNextSymbol 0xc2e1a LB 0x1718 -> off=0x0 cb=000000000000007f uValue=00000000000c2e1a 'biosfn_load_text_user_pat'
biosfn_load_text_user_pat:                   ; 0xc2e1a LB 0x7f
    push bp                                   ; 55                          ; 0xc2e1a vgabios.c:2001
    mov bp, sp                                ; 89 e5                       ; 0xc2e1b
    push si                                   ; 56                          ; 0xc2e1d
    push di                                   ; 57                          ; 0xc2e1e
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2e1f
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2e22
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc2e25
    mov word [bp-00ah], bx                    ; 89 5e f6                    ; 0xc2e28
    mov word [bp-00eh], cx                    ; 89 4e f2                    ; 0xc2e2b
    call 02c6eh                               ; e8 3d fe                    ; 0xc2e2e vgabios.c:2006
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2e31 vgabios.c:2007
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2e34
    xor ah, ah                                ; 30 e4                       ; 0xc2e36
    mov bx, ax                                ; 89 c3                       ; 0xc2e38
    sal bx, 00eh                              ; c1 e3 0e                    ; 0xc2e3a
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2e3d
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2e40
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2e42
    add bx, ax                                ; 01 c3                       ; 0xc2e45
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc2e47
    xor bx, bx                                ; 31 db                       ; 0xc2e4a vgabios.c:2008
    cmp bx, word [bp-00eh]                    ; 3b 5e f2                    ; 0xc2e4c
    jnc short 02e7fh                          ; 73 2e                       ; 0xc2e4f
    mov cl, byte [bp+008h]                    ; 8a 4e 08                    ; 0xc2e51 vgabios.c:2010
    xor ch, ch                                ; 30 ed                       ; 0xc2e54
    mov ax, bx                                ; 89 d8                       ; 0xc2e56
    mul cx                                    ; f7 e1                       ; 0xc2e58
    mov si, word [bp-00ah]                    ; 8b 76 f6                    ; 0xc2e5a
    add si, ax                                ; 01 c6                       ; 0xc2e5d
    mov ax, word [bp+004h]                    ; 8b 46 04                    ; 0xc2e5f vgabios.c:2011
    add ax, bx                                ; 01 d8                       ; 0xc2e62
    sal ax, 005h                              ; c1 e0 05                    ; 0xc2e64
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc2e67
    add di, ax                                ; 01 c7                       ; 0xc2e6a
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc2e6c vgabios.c:2012
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2e6f
    mov es, ax                                ; 8e c0                       ; 0xc2e72
    jcxz 02e7ch                               ; e3 06                       ; 0xc2e74
    push DS                                   ; 1e                          ; 0xc2e76
    mov ds, dx                                ; 8e da                       ; 0xc2e77
    rep movsb                                 ; f3 a4                       ; 0xc2e79
    pop DS                                    ; 1f                          ; 0xc2e7b
    inc bx                                    ; 43                          ; 0xc2e7c vgabios.c:2013
    jmp short 02e4ch                          ; eb cd                       ; 0xc2e7d
    call 02ca1h                               ; e8 1f fe                    ; 0xc2e7f vgabios.c:2014
    cmp byte [bp-006h], 010h                  ; 80 7e fa 10                 ; 0xc2e82 vgabios.c:2015
    jc short 02e90h                           ; 72 08                       ; 0xc2e86
    mov al, byte [bp+008h]                    ; 8a 46 08                    ; 0xc2e88 vgabios.c:2017
    xor ah, ah                                ; 30 e4                       ; 0xc2e8b
    call 02cd1h                               ; e8 41 fe                    ; 0xc2e8d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2e90 vgabios.c:2019
    pop di                                    ; 5f                          ; 0xc2e93
    pop si                                    ; 5e                          ; 0xc2e94
    pop bp                                    ; 5d                          ; 0xc2e95
    retn 00006h                               ; c2 06 00                    ; 0xc2e96
  ; disGetNextSymbol 0xc2e99 LB 0x1699 -> off=0x0 cb=0000000000000016 uValue=00000000000c2e99 'biosfn_load_gfx_8_8_chars'
biosfn_load_gfx_8_8_chars:                   ; 0xc2e99 LB 0x16
    push bp                                   ; 55                          ; 0xc2e99 vgabios.c:2021
    mov bp, sp                                ; 89 e5                       ; 0xc2e9a
    push bx                                   ; 53                          ; 0xc2e9c
    push cx                                   ; 51                          ; 0xc2e9d
    mov bx, dx                                ; 89 d3                       ; 0xc2e9e vgabios.c:2023
    mov cx, ax                                ; 89 c1                       ; 0xc2ea0
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc2ea2
    call 009f0h                               ; e8 48 db                    ; 0xc2ea5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2ea8 vgabios.c:2024
    pop cx                                    ; 59                          ; 0xc2eab
    pop bx                                    ; 5b                          ; 0xc2eac
    pop bp                                    ; 5d                          ; 0xc2ead
    retn                                      ; c3                          ; 0xc2eae
  ; disGetNextSymbol 0xc2eaf LB 0x1683 -> off=0x0 cb=000000000000004d uValue=00000000000c2eaf 'set_gfx_font'
set_gfx_font:                                ; 0xc2eaf LB 0x4d
    push bp                                   ; 55                          ; 0xc2eaf vgabios.c:2026
    mov bp, sp                                ; 89 e5                       ; 0xc2eb0
    push si                                   ; 56                          ; 0xc2eb2
    push di                                   ; 57                          ; 0xc2eb3
    mov si, ax                                ; 89 c6                       ; 0xc2eb4
    mov ax, dx                                ; 89 d0                       ; 0xc2eb6
    mov di, bx                                ; 89 df                       ; 0xc2eb8
    mov dl, cl                                ; 88 ca                       ; 0xc2eba
    mov bx, si                                ; 89 f3                       ; 0xc2ebc vgabios.c:2030
    mov cx, ax                                ; 89 c1                       ; 0xc2ebe
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc2ec0
    call 009f0h                               ; e8 2a db                    ; 0xc2ec3
    test dl, dl                               ; 84 d2                       ; 0xc2ec6 vgabios.c:2031
    je short 02edch                           ; 74 12                       ; 0xc2ec8
    cmp dl, 003h                              ; 80 fa 03                    ; 0xc2eca vgabios.c:2032
    jbe short 02ed1h                          ; 76 02                       ; 0xc2ecd
    mov DL, strict byte 002h                  ; b2 02                       ; 0xc2ecf vgabios.c:2033
    mov bl, dl                                ; 88 d3                       ; 0xc2ed1 vgabios.c:2034
    xor bh, bh                                ; 30 ff                       ; 0xc2ed3
    mov al, byte [bx+07e00h]                  ; 8a 87 00 7e                 ; 0xc2ed5
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2ed9
    mov bx, 00085h                            ; bb 85 00                    ; 0xc2edc vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2edf
    mov es, ax                                ; 8e c0                       ; 0xc2ee2
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc2ee4
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2ee7 vgabios.c:2039
    xor ah, ah                                ; 30 e4                       ; 0xc2eea
    dec ax                                    ; 48                          ; 0xc2eec
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2eed vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc2ef0
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2ef3 vgabios.c:2040
    pop di                                    ; 5f                          ; 0xc2ef6
    pop si                                    ; 5e                          ; 0xc2ef7
    pop bp                                    ; 5d                          ; 0xc2ef8
    retn 00002h                               ; c2 02 00                    ; 0xc2ef9
  ; disGetNextSymbol 0xc2efc LB 0x1636 -> off=0x0 cb=000000000000001d uValue=00000000000c2efc 'biosfn_load_gfx_user_chars'
biosfn_load_gfx_user_chars:                  ; 0xc2efc LB 0x1d
    push bp                                   ; 55                          ; 0xc2efc vgabios.c:2042
    mov bp, sp                                ; 89 e5                       ; 0xc2efd
    push si                                   ; 56                          ; 0xc2eff
    mov si, ax                                ; 89 c6                       ; 0xc2f00
    mov ax, dx                                ; 89 d0                       ; 0xc2f02
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc2f04 vgabios.c:2045
    xor dh, dh                                ; 30 f6                       ; 0xc2f07
    push dx                                   ; 52                          ; 0xc2f09
    xor ch, ch                                ; 30 ed                       ; 0xc2f0a
    mov dx, si                                ; 89 f2                       ; 0xc2f0c
    call 02eafh                               ; e8 9e ff                    ; 0xc2f0e
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2f11 vgabios.c:2046
    pop si                                    ; 5e                          ; 0xc2f14
    pop bp                                    ; 5d                          ; 0xc2f15
    retn 00002h                               ; c2 02 00                    ; 0xc2f16
  ; disGetNextSymbol 0xc2f19 LB 0x1619 -> off=0x0 cb=0000000000000022 uValue=00000000000c2f19 'biosfn_load_gfx_8_14_chars'
biosfn_load_gfx_8_14_chars:                  ; 0xc2f19 LB 0x22
    push bp                                   ; 55                          ; 0xc2f19 vgabios.c:2051
    mov bp, sp                                ; 89 e5                       ; 0xc2f1a
    push bx                                   ; 53                          ; 0xc2f1c
    push cx                                   ; 51                          ; 0xc2f1d
    mov bl, al                                ; 88 c3                       ; 0xc2f1e
    mov al, dl                                ; 88 d0                       ; 0xc2f20
    xor ah, ah                                ; 30 e4                       ; 0xc2f22 vgabios.c:2053
    push ax                                   ; 50                          ; 0xc2f24
    mov al, bl                                ; 88 d8                       ; 0xc2f25
    mov cx, ax                                ; 89 c1                       ; 0xc2f27
    mov bx, strict word 0000eh                ; bb 0e 00                    ; 0xc2f29
    mov ax, 05d6fh                            ; b8 6f 5d                    ; 0xc2f2c
    mov dx, ds                                ; 8c da                       ; 0xc2f2f
    call 02eafh                               ; e8 7b ff                    ; 0xc2f31
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f34 vgabios.c:2054
    pop cx                                    ; 59                          ; 0xc2f37
    pop bx                                    ; 5b                          ; 0xc2f38
    pop bp                                    ; 5d                          ; 0xc2f39
    retn                                      ; c3                          ; 0xc2f3a
  ; disGetNextSymbol 0xc2f3b LB 0x15f7 -> off=0x0 cb=0000000000000022 uValue=00000000000c2f3b 'biosfn_load_gfx_8_8_dd_chars'
biosfn_load_gfx_8_8_dd_chars:                ; 0xc2f3b LB 0x22
    push bp                                   ; 55                          ; 0xc2f3b vgabios.c:2055
    mov bp, sp                                ; 89 e5                       ; 0xc2f3c
    push bx                                   ; 53                          ; 0xc2f3e
    push cx                                   ; 51                          ; 0xc2f3f
    mov bl, al                                ; 88 c3                       ; 0xc2f40
    mov al, dl                                ; 88 d0                       ; 0xc2f42
    xor ah, ah                                ; 30 e4                       ; 0xc2f44 vgabios.c:2057
    push ax                                   ; 50                          ; 0xc2f46
    mov al, bl                                ; 88 d8                       ; 0xc2f47
    mov cx, ax                                ; 89 c1                       ; 0xc2f49
    mov bx, strict word 00008h                ; bb 08 00                    ; 0xc2f4b
    mov ax, 0556fh                            ; b8 6f 55                    ; 0xc2f4e
    mov dx, ds                                ; 8c da                       ; 0xc2f51
    call 02eafh                               ; e8 59 ff                    ; 0xc2f53
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f56 vgabios.c:2058
    pop cx                                    ; 59                          ; 0xc2f59
    pop bx                                    ; 5b                          ; 0xc2f5a
    pop bp                                    ; 5d                          ; 0xc2f5b
    retn                                      ; c3                          ; 0xc2f5c
  ; disGetNextSymbol 0xc2f5d LB 0x15d5 -> off=0x0 cb=0000000000000022 uValue=00000000000c2f5d 'biosfn_load_gfx_8_16_chars'
biosfn_load_gfx_8_16_chars:                  ; 0xc2f5d LB 0x22
    push bp                                   ; 55                          ; 0xc2f5d vgabios.c:2059
    mov bp, sp                                ; 89 e5                       ; 0xc2f5e
    push bx                                   ; 53                          ; 0xc2f60
    push cx                                   ; 51                          ; 0xc2f61
    mov bl, al                                ; 88 c3                       ; 0xc2f62
    mov al, dl                                ; 88 d0                       ; 0xc2f64
    xor ah, ah                                ; 30 e4                       ; 0xc2f66 vgabios.c:2061
    push ax                                   ; 50                          ; 0xc2f68
    mov al, bl                                ; 88 d8                       ; 0xc2f69
    mov cx, ax                                ; 89 c1                       ; 0xc2f6b
    mov bx, strict word 00010h                ; bb 10 00                    ; 0xc2f6d
    mov ax, 06b6fh                            ; b8 6f 6b                    ; 0xc2f70
    mov dx, ds                                ; 8c da                       ; 0xc2f73
    call 02eafh                               ; e8 37 ff                    ; 0xc2f75
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f78 vgabios.c:2062
    pop cx                                    ; 59                          ; 0xc2f7b
    pop bx                                    ; 5b                          ; 0xc2f7c
    pop bp                                    ; 5d                          ; 0xc2f7d
    retn                                      ; c3                          ; 0xc2f7e
  ; disGetNextSymbol 0xc2f7f LB 0x15b3 -> off=0x0 cb=0000000000000005 uValue=00000000000c2f7f 'biosfn_alternate_prtsc'
biosfn_alternate_prtsc:                      ; 0xc2f7f LB 0x5
    push bp                                   ; 55                          ; 0xc2f7f vgabios.c:2064
    mov bp, sp                                ; 89 e5                       ; 0xc2f80
    pop bp                                    ; 5d                          ; 0xc2f82 vgabios.c:2069
    retn                                      ; c3                          ; 0xc2f83
  ; disGetNextSymbol 0xc2f84 LB 0x15ae -> off=0x0 cb=0000000000000032 uValue=00000000000c2f84 'biosfn_set_txt_lines'
biosfn_set_txt_lines:                        ; 0xc2f84 LB 0x32
    push bx                                   ; 53                          ; 0xc2f84 vgabios.c:2071
    push si                                   ; 56                          ; 0xc2f85
    push bp                                   ; 55                          ; 0xc2f86
    mov bp, sp                                ; 89 e5                       ; 0xc2f87
    mov bl, al                                ; 88 c3                       ; 0xc2f89
    mov si, 00089h                            ; be 89 00                    ; 0xc2f8b vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2f8e
    mov es, ax                                ; 8e c0                       ; 0xc2f91
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2f93
    and AL, strict byte 06fh                  ; 24 6f                       ; 0xc2f96 vgabios.c:2077
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2f98 vgabios.c:2079
    je short 02fa5h                           ; 74 08                       ; 0xc2f9b
    test bl, bl                               ; 84 db                       ; 0xc2f9d
    jne short 02fa7h                          ; 75 06                       ; 0xc2f9f
    or AL, strict byte 080h                   ; 0c 80                       ; 0xc2fa1 vgabios.c:2082
    jmp short 02fa7h                          ; eb 02                       ; 0xc2fa3 vgabios.c:2083
    or AL, strict byte 010h                   ; 0c 10                       ; 0xc2fa5 vgabios.c:2085
    mov bx, 00089h                            ; bb 89 00                    ; 0xc2fa7 vgabios.c:52
    mov si, strict word 00040h                ; be 40 00                    ; 0xc2faa
    mov es, si                                ; 8e c6                       ; 0xc2fad
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2faf
    pop bp                                    ; 5d                          ; 0xc2fb2 vgabios.c:2089
    pop si                                    ; 5e                          ; 0xc2fb3
    pop bx                                    ; 5b                          ; 0xc2fb4
    retn                                      ; c3                          ; 0xc2fb5
  ; disGetNextSymbol 0xc2fb6 LB 0x157c -> off=0x0 cb=0000000000000005 uValue=00000000000c2fb6 'biosfn_switch_video_interface'
biosfn_switch_video_interface:               ; 0xc2fb6 LB 0x5
    push bp                                   ; 55                          ; 0xc2fb6 vgabios.c:2092
    mov bp, sp                                ; 89 e5                       ; 0xc2fb7
    pop bp                                    ; 5d                          ; 0xc2fb9 vgabios.c:2097
    retn                                      ; c3                          ; 0xc2fba
  ; disGetNextSymbol 0xc2fbb LB 0x1577 -> off=0x0 cb=0000000000000005 uValue=00000000000c2fbb 'biosfn_enable_video_refresh_control'
biosfn_enable_video_refresh_control:         ; 0xc2fbb LB 0x5
    push bp                                   ; 55                          ; 0xc2fbb vgabios.c:2098
    mov bp, sp                                ; 89 e5                       ; 0xc2fbc
    pop bp                                    ; 5d                          ; 0xc2fbe vgabios.c:2103
    retn                                      ; c3                          ; 0xc2fbf
  ; disGetNextSymbol 0xc2fc0 LB 0x1572 -> off=0x0 cb=000000000000009d uValue=00000000000c2fc0 'biosfn_write_string'
biosfn_write_string:                         ; 0xc2fc0 LB 0x9d
    push bp                                   ; 55                          ; 0xc2fc0 vgabios.c:2106
    mov bp, sp                                ; 89 e5                       ; 0xc2fc1
    push si                                   ; 56                          ; 0xc2fc3
    push di                                   ; 57                          ; 0xc2fc4
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2fc5
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2fc8
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc2fcb
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc2fce
    mov si, cx                                ; 89 ce                       ; 0xc2fd1
    mov di, word [bp+00ah]                    ; 8b 7e 0a                    ; 0xc2fd3
    mov al, dl                                ; 88 d0                       ; 0xc2fd6 vgabios.c:2113
    xor ah, ah                                ; 30 e4                       ; 0xc2fd8
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc2fda
    lea dx, [bp-00ch]                         ; 8d 56 f4                    ; 0xc2fdd
    call 00a96h                               ; e8 b3 da                    ; 0xc2fe0
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc2fe3 vgabios.c:2116
    jne short 02ffah                          ; 75 11                       ; 0xc2fe7
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2fe9 vgabios.c:2117
    mov byte [bp+006h], al                    ; 88 46 06                    ; 0xc2fec
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2fef vgabios.c:2118
    xor al, al                                ; 30 c0                       ; 0xc2ff2
    shr ax, 008h                              ; c1 e8 08                    ; 0xc2ff4
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2ff7
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc2ffa vgabios.c:2121
    xor dh, dh                                ; 30 f6                       ; 0xc2ffd
    sal dx, 008h                              ; c1 e2 08                    ; 0xc2fff
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc3002
    xor ah, ah                                ; 30 e4                       ; 0xc3005
    add dx, ax                                ; 01 c2                       ; 0xc3007
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3009 vgabios.c:2122
    call 01293h                               ; e8 84 e2                    ; 0xc300c
    dec si                                    ; 4e                          ; 0xc300f vgabios.c:2124
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc3010
    je short 03043h                           ; 74 2e                       ; 0xc3013
    mov bx, di                                ; 89 fb                       ; 0xc3015 vgabios.c:2126
    inc di                                    ; 47                          ; 0xc3017
    mov es, [bp+008h]                         ; 8e 46 08                    ; 0xc3018 vgabios.c:47
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc301b
    test byte [bp-006h], 002h                 ; f6 46 fa 02                 ; 0xc301e vgabios.c:2127
    je short 0302dh                           ; 74 09                       ; 0xc3022
    mov bx, di                                ; 89 fb                       ; 0xc3024 vgabios.c:2128
    inc di                                    ; 47                          ; 0xc3026
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3027 vgabios.c:47
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc302a vgabios.c:48
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc302d vgabios.c:2130
    xor bh, bh                                ; 30 ff                       ; 0xc3030
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc3032
    xor dh, dh                                ; 30 f6                       ; 0xc3035
    mov al, ah                                ; 88 e0                       ; 0xc3037
    xor ah, ah                                ; 30 e4                       ; 0xc3039
    mov cx, strict word 00003h                ; b9 03 00                    ; 0xc303b
    call 02a16h                               ; e8 d5 f9                    ; 0xc303e
    jmp short 0300fh                          ; eb cc                       ; 0xc3041 vgabios.c:2131
    test byte [bp-006h], 001h                 ; f6 46 fa 01                 ; 0xc3043 vgabios.c:2134
    jne short 03054h                          ; 75 0b                       ; 0xc3047
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc3049 vgabios.c:2135
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc304c
    xor ah, ah                                ; 30 e4                       ; 0xc304f
    call 01293h                               ; e8 3f e2                    ; 0xc3051
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3054 vgabios.c:2136
    pop di                                    ; 5f                          ; 0xc3057
    pop si                                    ; 5e                          ; 0xc3058
    pop bp                                    ; 5d                          ; 0xc3059
    retn 00008h                               ; c2 08 00                    ; 0xc305a
  ; disGetNextSymbol 0xc305d LB 0x14d5 -> off=0x0 cb=00000000000001ef uValue=00000000000c305d 'biosfn_read_state_info'
biosfn_read_state_info:                      ; 0xc305d LB 0x1ef
    push bp                                   ; 55                          ; 0xc305d vgabios.c:2139
    mov bp, sp                                ; 89 e5                       ; 0xc305e
    push cx                                   ; 51                          ; 0xc3060
    push si                                   ; 56                          ; 0xc3061
    push di                                   ; 57                          ; 0xc3062
    push ax                                   ; 50                          ; 0xc3063
    push ax                                   ; 50                          ; 0xc3064
    push dx                                   ; 52                          ; 0xc3065
    mov si, strict word 00049h                ; be 49 00                    ; 0xc3066 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3069
    mov es, ax                                ; 8e c0                       ; 0xc306c
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc306e
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc3071 vgabios.c:48
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc3074 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3077
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc307a vgabios.c:58
    mov ax, ds                                ; 8c d8                       ; 0xc307d vgabios.c:2150
    mov es, dx                                ; 8e c2                       ; 0xc307f vgabios.c:72
    mov word [es:bx], 05505h                  ; 26 c7 07 05 55              ; 0xc3081
    mov [es:bx+002h], ds                      ; 26 8c 5f 02                 ; 0xc3086
    lea di, [bx+004h]                         ; 8d 7f 04                    ; 0xc308a vgabios.c:2155
    mov cx, strict word 0001eh                ; b9 1e 00                    ; 0xc308d
    mov si, strict word 00049h                ; be 49 00                    ; 0xc3090
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3093
    jcxz 0309eh                               ; e3 06                       ; 0xc3096
    push DS                                   ; 1e                          ; 0xc3098
    mov ds, dx                                ; 8e da                       ; 0xc3099
    rep movsb                                 ; f3 a4                       ; 0xc309b
    pop DS                                    ; 1f                          ; 0xc309d
    mov si, 00084h                            ; be 84 00                    ; 0xc309e vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc30a1
    mov es, ax                                ; 8e c0                       ; 0xc30a4
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc30a6
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc30a9 vgabios.c:48
    lea si, [bx+022h]                         ; 8d 77 22                    ; 0xc30ab
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc30ae vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc30b1
    lea di, [bx+023h]                         ; 8d 7f 23                    ; 0xc30b4 vgabios.c:2157
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc30b7
    mov si, 00085h                            ; be 85 00                    ; 0xc30ba
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc30bd
    jcxz 030c8h                               ; e3 06                       ; 0xc30c0
    push DS                                   ; 1e                          ; 0xc30c2
    mov ds, dx                                ; 8e da                       ; 0xc30c3
    rep movsb                                 ; f3 a4                       ; 0xc30c5
    pop DS                                    ; 1f                          ; 0xc30c7
    mov si, 0008ah                            ; be 8a 00                    ; 0xc30c8 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc30cb
    mov es, ax                                ; 8e c0                       ; 0xc30ce
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc30d0
    lea si, [bx+025h]                         ; 8d 77 25                    ; 0xc30d3 vgabios.c:48
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc30d6 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc30d9
    lea si, [bx+026h]                         ; 8d 77 26                    ; 0xc30dc vgabios.c:2160
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc30df vgabios.c:52
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc30e3 vgabios.c:2161
    mov word [es:si], strict word 00010h      ; 26 c7 04 10 00              ; 0xc30e6 vgabios.c:62
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc30eb vgabios.c:2162
    mov byte [es:si], 008h                    ; 26 c6 04 08                 ; 0xc30ee vgabios.c:52
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc30f2 vgabios.c:2163
    mov byte [es:si], 002h                    ; 26 c6 04 02                 ; 0xc30f5 vgabios.c:52
    lea si, [bx+02bh]                         ; 8d 77 2b                    ; 0xc30f9 vgabios.c:2164
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc30fc vgabios.c:52
    lea si, [bx+02ch]                         ; 8d 77 2c                    ; 0xc3100 vgabios.c:2165
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc3103 vgabios.c:52
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc3107 vgabios.c:2166
    mov byte [es:si], 021h                    ; 26 c6 04 21                 ; 0xc310a vgabios.c:52
    lea si, [bx+031h]                         ; 8d 77 31                    ; 0xc310e vgabios.c:2167
    mov byte [es:si], 003h                    ; 26 c6 04 03                 ; 0xc3111 vgabios.c:52
    lea si, [bx+032h]                         ; 8d 77 32                    ; 0xc3115 vgabios.c:2168
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc3118 vgabios.c:52
    mov si, 00089h                            ; be 89 00                    ; 0xc311c vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc311f
    mov es, ax                                ; 8e c0                       ; 0xc3122
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3124
    mov dl, al                                ; 88 c2                       ; 0xc3127 vgabios.c:2173
    and dl, 080h                              ; 80 e2 80                    ; 0xc3129
    xor dh, dh                                ; 30 f6                       ; 0xc312c
    sar dx, 006h                              ; c1 fa 06                    ; 0xc312e
    and AL, strict byte 010h                  ; 24 10                       ; 0xc3131
    xor ah, ah                                ; 30 e4                       ; 0xc3133
    sar ax, 004h                              ; c1 f8 04                    ; 0xc3135
    or ax, dx                                 ; 09 d0                       ; 0xc3138
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc313a vgabios.c:2174
    je short 03150h                           ; 74 11                       ; 0xc313d
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc313f
    je short 0314ch                           ; 74 08                       ; 0xc3142
    test ax, ax                               ; 85 c0                       ; 0xc3144
    jne short 03150h                          ; 75 08                       ; 0xc3146
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc3148 vgabios.c:2175
    jmp short 03152h                          ; eb 06                       ; 0xc314a
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc314c vgabios.c:2176
    jmp short 03152h                          ; eb 02                       ; 0xc314e
    xor al, al                                ; 30 c0                       ; 0xc3150 vgabios.c:2178
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc3152 vgabios.c:2180
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3155 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3158
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc315b vgabios.c:2183
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc315e
    jc short 03181h                           ; 72 1f                       ; 0xc3160
    cmp AL, strict byte 012h                  ; 3c 12                       ; 0xc3162
    jnbe short 03181h                         ; 77 1b                       ; 0xc3164
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3166 vgabios.c:2184
    test ax, ax                               ; 85 c0                       ; 0xc3169
    je short 031c3h                           ; 74 56                       ; 0xc316b
    mov si, ax                                ; 89 c6                       ; 0xc316d vgabios.c:2185
    shr si, 002h                              ; c1 ee 02                    ; 0xc316f
    mov ax, 04000h                            ; b8 00 40                    ; 0xc3172
    xor dx, dx                                ; 31 d2                       ; 0xc3175
    div si                                    ; f7 f6                       ; 0xc3177
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc3179
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc317c vgabios.c:52
    jmp short 031c3h                          ; eb 42                       ; 0xc317f vgabios.c:2186
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc3181
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3184
    cmp AL, strict byte 013h                  ; 3c 13                       ; 0xc3187
    jne short 0319ch                          ; 75 11                       ; 0xc3189
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc318b vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc318e
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc3192 vgabios.c:2188
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc3195 vgabios.c:62
    jmp short 031c3h                          ; eb 27                       ; 0xc319a vgabios.c:2189
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc319c
    jc short 031c3h                           ; 72 23                       ; 0xc319e
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc31a0
    jnbe short 031c3h                         ; 77 1f                       ; 0xc31a2
    cmp word [bp-00ah], strict byte 00000h    ; 83 7e f6 00                 ; 0xc31a4 vgabios.c:2191
    je short 031b8h                           ; 74 0e                       ; 0xc31a8
    mov ax, 04000h                            ; b8 00 40                    ; 0xc31aa vgabios.c:2192
    xor dx, dx                                ; 31 d2                       ; 0xc31ad
    div word [bp-00ah]                        ; f7 76 f6                    ; 0xc31af
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31b2 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc31b5
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc31b8 vgabios.c:2193
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31bb vgabios.c:62
    mov word [es:si], strict word 00004h      ; 26 c7 04 04 00              ; 0xc31be
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc31c3 vgabios.c:2195
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc31c6
    je short 031ceh                           ; 74 04                       ; 0xc31c8
    cmp AL, strict byte 011h                  ; 3c 11                       ; 0xc31ca
    jne short 031d9h                          ; 75 0b                       ; 0xc31cc
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc31ce vgabios.c:2196
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31d1 vgabios.c:62
    mov word [es:si], strict word 00002h      ; 26 c7 04 02 00              ; 0xc31d4
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc31d9 vgabios.c:2198
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc31dc
    jc short 03235h                           ; 72 55                       ; 0xc31de
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc31e0
    je short 03235h                           ; 74 51                       ; 0xc31e2
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc31e4 vgabios.c:2199
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31e7 vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc31ea
    mov si, 00084h                            ; be 84 00                    ; 0xc31ee vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31f1
    mov es, ax                                ; 8e c0                       ; 0xc31f4
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc31f6
    xor ah, ah                                ; 30 e4                       ; 0xc31f9 vgabios.c:48
    inc ax                                    ; 40                          ; 0xc31fb
    mov si, 00085h                            ; be 85 00                    ; 0xc31fc vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc31ff
    xor dh, dh                                ; 30 f6                       ; 0xc3202 vgabios.c:48
    imul dx                                   ; f7 ea                       ; 0xc3204
    cmp ax, 0015eh                            ; 3d 5e 01                    ; 0xc3206 vgabios.c:2201
    jc short 03219h                           ; 72 0e                       ; 0xc3209
    jbe short 03222h                          ; 76 15                       ; 0xc320b
    cmp ax, 001e0h                            ; 3d e0 01                    ; 0xc320d
    je short 0322ah                           ; 74 18                       ; 0xc3210
    cmp ax, 00190h                            ; 3d 90 01                    ; 0xc3212
    je short 03226h                           ; 74 0f                       ; 0xc3215
    jmp short 0322ah                          ; eb 11                       ; 0xc3217
    cmp ax, 000c8h                            ; 3d c8 00                    ; 0xc3219
    jne short 0322ah                          ; 75 0c                       ; 0xc321c
    xor al, al                                ; 30 c0                       ; 0xc321e vgabios.c:2202
    jmp short 0322ch                          ; eb 0a                       ; 0xc3220
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc3222 vgabios.c:2203
    jmp short 0322ch                          ; eb 06                       ; 0xc3224
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc3226 vgabios.c:2204
    jmp short 0322ch                          ; eb 02                       ; 0xc3228
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc322a vgabios.c:2206
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc322c vgabios.c:2208
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc322f vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3232
    lea di, [bx+033h]                         ; 8d 7f 33                    ; 0xc3235 vgabios.c:2211
    mov cx, strict word 0000dh                ; b9 0d 00                    ; 0xc3238
    xor ax, ax                                ; 31 c0                       ; 0xc323b
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc323d
    jcxz 03244h                               ; e3 02                       ; 0xc3240
    rep stosb                                 ; f3 aa                       ; 0xc3242
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3244 vgabios.c:2212
    pop di                                    ; 5f                          ; 0xc3247
    pop si                                    ; 5e                          ; 0xc3248
    pop cx                                    ; 59                          ; 0xc3249
    pop bp                                    ; 5d                          ; 0xc324a
    retn                                      ; c3                          ; 0xc324b
  ; disGetNextSymbol 0xc324c LB 0x12e6 -> off=0x0 cb=0000000000000023 uValue=00000000000c324c 'biosfn_read_video_state_size2'
biosfn_read_video_state_size2:               ; 0xc324c LB 0x23
    push dx                                   ; 52                          ; 0xc324c vgabios.c:2215
    push bp                                   ; 55                          ; 0xc324d
    mov bp, sp                                ; 89 e5                       ; 0xc324e
    mov dx, ax                                ; 89 c2                       ; 0xc3250
    xor ax, ax                                ; 31 c0                       ; 0xc3252 vgabios.c:2219
    test dl, 001h                             ; f6 c2 01                    ; 0xc3254 vgabios.c:2220
    je short 0325ch                           ; 74 03                       ; 0xc3257
    mov ax, strict word 00046h                ; b8 46 00                    ; 0xc3259 vgabios.c:2221
    test dl, 002h                             ; f6 c2 02                    ; 0xc325c vgabios.c:2223
    je short 03264h                           ; 74 03                       ; 0xc325f
    add ax, strict word 0002ah                ; 05 2a 00                    ; 0xc3261 vgabios.c:2224
    test dl, 004h                             ; f6 c2 04                    ; 0xc3264 vgabios.c:2226
    je short 0326ch                           ; 74 03                       ; 0xc3267
    add ax, 00304h                            ; 05 04 03                    ; 0xc3269 vgabios.c:2227
    pop bp                                    ; 5d                          ; 0xc326c vgabios.c:2230
    pop dx                                    ; 5a                          ; 0xc326d
    retn                                      ; c3                          ; 0xc326e
  ; disGetNextSymbol 0xc326f LB 0x12c3 -> off=0x0 cb=0000000000000018 uValue=00000000000c326f 'vga_get_video_state_size'
vga_get_video_state_size:                    ; 0xc326f LB 0x18
    push bp                                   ; 55                          ; 0xc326f vgabios.c:2232
    mov bp, sp                                ; 89 e5                       ; 0xc3270
    push bx                                   ; 53                          ; 0xc3272
    mov bx, dx                                ; 89 d3                       ; 0xc3273
    call 0324ch                               ; e8 d4 ff                    ; 0xc3275 vgabios.c:2235
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc3278
    shr ax, 006h                              ; c1 e8 06                    ; 0xc327b
    mov word [ss:bx], ax                      ; 36 89 07                    ; 0xc327e
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3281 vgabios.c:2236
    pop bx                                    ; 5b                          ; 0xc3284
    pop bp                                    ; 5d                          ; 0xc3285
    retn                                      ; c3                          ; 0xc3286
  ; disGetNextSymbol 0xc3287 LB 0x12ab -> off=0x0 cb=00000000000002d8 uValue=00000000000c3287 'biosfn_save_video_state'
biosfn_save_video_state:                     ; 0xc3287 LB 0x2d8
    push bp                                   ; 55                          ; 0xc3287 vgabios.c:2238
    mov bp, sp                                ; 89 e5                       ; 0xc3288
    push cx                                   ; 51                          ; 0xc328a
    push si                                   ; 56                          ; 0xc328b
    push di                                   ; 57                          ; 0xc328c
    push ax                                   ; 50                          ; 0xc328d
    push ax                                   ; 50                          ; 0xc328e
    push ax                                   ; 50                          ; 0xc328f
    mov cx, dx                                ; 89 d1                       ; 0xc3290
    mov si, strict word 00063h                ; be 63 00                    ; 0xc3292 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3295
    mov es, ax                                ; 8e c0                       ; 0xc3298
    mov di, word [es:si]                      ; 26 8b 3c                    ; 0xc329a
    mov si, di                                ; 89 fe                       ; 0xc329d vgabios.c:58
    test byte [bp-00ch], 001h                 ; f6 46 f4 01                 ; 0xc329f vgabios.c:2243
    je short 0330bh                           ; 74 66                       ; 0xc32a3
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc32a5 vgabios.c:2244
    in AL, DX                                 ; ec                          ; 0xc32a8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32a9
    mov es, cx                                ; 8e c1                       ; 0xc32ab vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32ad
    inc bx                                    ; 43                          ; 0xc32b0 vgabios.c:2244
    mov dx, di                                ; 89 fa                       ; 0xc32b1
    in AL, DX                                 ; ec                          ; 0xc32b3
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32b4
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32b6 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc32b9 vgabios.c:2245
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc32ba
    in AL, DX                                 ; ec                          ; 0xc32bd
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32be
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32c0 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc32c3 vgabios.c:2246
    mov dx, 003dah                            ; ba da 03                    ; 0xc32c4
    in AL, DX                                 ; ec                          ; 0xc32c7
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32c8
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc32ca vgabios.c:2248
    in AL, DX                                 ; ec                          ; 0xc32cd
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32ce
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc32d0
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc32d3 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32d6
    inc bx                                    ; 43                          ; 0xc32d9 vgabios.c:2249
    mov dx, 003cah                            ; ba ca 03                    ; 0xc32da
    in AL, DX                                 ; ec                          ; 0xc32dd
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32de
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32e0 vgabios.c:52
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc32e3 vgabios.c:2252
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc32e6
    add bx, ax                                ; 01 c3                       ; 0xc32e9 vgabios.c:2250
    jmp short 032f3h                          ; eb 06                       ; 0xc32eb
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc32ed
    jnbe short 0330eh                         ; 77 1b                       ; 0xc32f1
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc32f3 vgabios.c:2253
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc32f6
    out DX, AL                                ; ee                          ; 0xc32f9
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc32fa vgabios.c:2254
    in AL, DX                                 ; ec                          ; 0xc32fd
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32fe
    mov es, cx                                ; 8e c1                       ; 0xc3300 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3302
    inc bx                                    ; 43                          ; 0xc3305 vgabios.c:2254
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3306 vgabios.c:2255
    jmp short 032edh                          ; eb e2                       ; 0xc3309
    jmp near 033bbh                           ; e9 ad 00                    ; 0xc330b
    xor al, al                                ; 30 c0                       ; 0xc330e vgabios.c:2256
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3310
    out DX, AL                                ; ee                          ; 0xc3313
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3314 vgabios.c:2257
    in AL, DX                                 ; ec                          ; 0xc3317
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3318
    mov es, cx                                ; 8e c1                       ; 0xc331a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc331c
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc331f vgabios.c:2259
    inc bx                                    ; 43                          ; 0xc3324 vgabios.c:2257
    jmp short 0332dh                          ; eb 06                       ; 0xc3325
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc3327
    jnbe short 03344h                         ; 77 17                       ; 0xc332b
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc332d vgabios.c:2260
    mov dx, si                                ; 89 f2                       ; 0xc3330
    out DX, AL                                ; ee                          ; 0xc3332
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc3333 vgabios.c:2261
    in AL, DX                                 ; ec                          ; 0xc3336
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3337
    mov es, cx                                ; 8e c1                       ; 0xc3339 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc333b
    inc bx                                    ; 43                          ; 0xc333e vgabios.c:2261
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc333f vgabios.c:2262
    jmp short 03327h                          ; eb e3                       ; 0xc3342
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3344 vgabios.c:2264
    jmp short 03351h                          ; eb 06                       ; 0xc3349
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc334b
    jnbe short 03375h                         ; 77 24                       ; 0xc334f
    mov dx, 003dah                            ; ba da 03                    ; 0xc3351 vgabios.c:2265
    in AL, DX                                 ; ec                          ; 0xc3354
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3355
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3357 vgabios.c:2266
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc335a
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc335d
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3360
    out DX, AL                                ; ee                          ; 0xc3363
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc3364 vgabios.c:2267
    in AL, DX                                 ; ec                          ; 0xc3367
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3368
    mov es, cx                                ; 8e c1                       ; 0xc336a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc336c
    inc bx                                    ; 43                          ; 0xc336f vgabios.c:2267
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3370 vgabios.c:2268
    jmp short 0334bh                          ; eb d6                       ; 0xc3373
    mov dx, 003dah                            ; ba da 03                    ; 0xc3375 vgabios.c:2269
    in AL, DX                                 ; ec                          ; 0xc3378
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3379
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc337b vgabios.c:2271
    jmp short 03388h                          ; eb 06                       ; 0xc3380
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc3382
    jnbe short 033a0h                         ; 77 18                       ; 0xc3386
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3388 vgabios.c:2272
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc338b
    out DX, AL                                ; ee                          ; 0xc338e
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc338f vgabios.c:2273
    in AL, DX                                 ; ec                          ; 0xc3392
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3393
    mov es, cx                                ; 8e c1                       ; 0xc3395 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3397
    inc bx                                    ; 43                          ; 0xc339a vgabios.c:2273
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc339b vgabios.c:2274
    jmp short 03382h                          ; eb e2                       ; 0xc339e
    mov es, cx                                ; 8e c1                       ; 0xc33a0 vgabios.c:62
    mov word [es:bx], si                      ; 26 89 37                    ; 0xc33a2
    inc bx                                    ; 43                          ; 0xc33a5 vgabios.c:2276
    inc bx                                    ; 43                          ; 0xc33a6
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33a7 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33ab vgabios.c:2279
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33ac vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33b0 vgabios.c:2280
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33b1 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33b5 vgabios.c:2281
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc33b6 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc33ba vgabios.c:2282
    test byte [bp-00ch], 002h                 ; f6 46 f4 02                 ; 0xc33bb vgabios.c:2284
    jne short 033c4h                          ; 75 03                       ; 0xc33bf
    jmp near 03503h                           ; e9 3f 01                    ; 0xc33c1
    mov si, strict word 00049h                ; be 49 00                    ; 0xc33c4 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc33c7
    mov es, ax                                ; 8e c0                       ; 0xc33ca
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc33cc
    mov es, cx                                ; 8e c1                       ; 0xc33cf vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc33d1
    inc bx                                    ; 43                          ; 0xc33d4 vgabios.c:2285
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc33d5 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc33d8
    mov es, ax                                ; 8e c0                       ; 0xc33db
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc33dd
    mov es, cx                                ; 8e c1                       ; 0xc33e0 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc33e2
    inc bx                                    ; 43                          ; 0xc33e5 vgabios.c:2286
    inc bx                                    ; 43                          ; 0xc33e6
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc33e7 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc33ea
    mov es, ax                                ; 8e c0                       ; 0xc33ed
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc33ef
    mov es, cx                                ; 8e c1                       ; 0xc33f2 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc33f4
    inc bx                                    ; 43                          ; 0xc33f7 vgabios.c:2287
    inc bx                                    ; 43                          ; 0xc33f8
    mov si, strict word 00063h                ; be 63 00                    ; 0xc33f9 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc33fc
    mov es, ax                                ; 8e c0                       ; 0xc33ff
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3401
    mov es, cx                                ; 8e c1                       ; 0xc3404 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3406
    inc bx                                    ; 43                          ; 0xc3409 vgabios.c:2288
    inc bx                                    ; 43                          ; 0xc340a
    mov si, 00084h                            ; be 84 00                    ; 0xc340b vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc340e
    mov es, ax                                ; 8e c0                       ; 0xc3411
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3413
    mov es, cx                                ; 8e c1                       ; 0xc3416 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3418
    inc bx                                    ; 43                          ; 0xc341b vgabios.c:2289
    mov si, 00085h                            ; be 85 00                    ; 0xc341c vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc341f
    mov es, ax                                ; 8e c0                       ; 0xc3422
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3424
    mov es, cx                                ; 8e c1                       ; 0xc3427 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3429
    inc bx                                    ; 43                          ; 0xc342c vgabios.c:2290
    inc bx                                    ; 43                          ; 0xc342d
    mov si, 00087h                            ; be 87 00                    ; 0xc342e vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3431
    mov es, ax                                ; 8e c0                       ; 0xc3434
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3436
    mov es, cx                                ; 8e c1                       ; 0xc3439 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc343b
    inc bx                                    ; 43                          ; 0xc343e vgabios.c:2291
    mov si, 00088h                            ; be 88 00                    ; 0xc343f vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3442
    mov es, ax                                ; 8e c0                       ; 0xc3445
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3447
    mov es, cx                                ; 8e c1                       ; 0xc344a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc344c
    inc bx                                    ; 43                          ; 0xc344f vgabios.c:2292
    mov si, 00089h                            ; be 89 00                    ; 0xc3450 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3453
    mov es, ax                                ; 8e c0                       ; 0xc3456
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3458
    mov es, cx                                ; 8e c1                       ; 0xc345b vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc345d
    inc bx                                    ; 43                          ; 0xc3460 vgabios.c:2293
    mov si, strict word 00060h                ; be 60 00                    ; 0xc3461 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3464
    mov es, ax                                ; 8e c0                       ; 0xc3467
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3469
    mov es, cx                                ; 8e c1                       ; 0xc346c vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc346e
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3471 vgabios.c:2295
    inc bx                                    ; 43                          ; 0xc3476 vgabios.c:2294
    inc bx                                    ; 43                          ; 0xc3477
    jmp short 03480h                          ; eb 06                       ; 0xc3478
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc347a
    jnc short 0349ch                          ; 73 1c                       ; 0xc347e
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc3480 vgabios.c:2296
    add si, si                                ; 01 f6                       ; 0xc3483
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc3485
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3488 vgabios.c:57
    mov es, ax                                ; 8e c0                       ; 0xc348b
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc348d
    mov es, cx                                ; 8e c1                       ; 0xc3490 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3492
    inc bx                                    ; 43                          ; 0xc3495 vgabios.c:2297
    inc bx                                    ; 43                          ; 0xc3496
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3497 vgabios.c:2298
    jmp short 0347ah                          ; eb de                       ; 0xc349a
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc349c vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc349f
    mov es, ax                                ; 8e c0                       ; 0xc34a2
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34a4
    mov es, cx                                ; 8e c1                       ; 0xc34a7 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34a9
    inc bx                                    ; 43                          ; 0xc34ac vgabios.c:2299
    inc bx                                    ; 43                          ; 0xc34ad
    mov si, strict word 00062h                ; be 62 00                    ; 0xc34ae vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34b1
    mov es, ax                                ; 8e c0                       ; 0xc34b4
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc34b6
    mov es, cx                                ; 8e c1                       ; 0xc34b9 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc34bb
    inc bx                                    ; 43                          ; 0xc34be vgabios.c:2300
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc34bf vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc34c2
    mov es, ax                                ; 8e c0                       ; 0xc34c4
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34c6
    mov es, cx                                ; 8e c1                       ; 0xc34c9 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34cb
    inc bx                                    ; 43                          ; 0xc34ce vgabios.c:2302
    inc bx                                    ; 43                          ; 0xc34cf
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc34d0 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc34d3
    mov es, ax                                ; 8e c0                       ; 0xc34d5
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34d7
    mov es, cx                                ; 8e c1                       ; 0xc34da vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34dc
    inc bx                                    ; 43                          ; 0xc34df vgabios.c:2303
    inc bx                                    ; 43                          ; 0xc34e0
    mov si, 0010ch                            ; be 0c 01                    ; 0xc34e1 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc34e4
    mov es, ax                                ; 8e c0                       ; 0xc34e6
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34e8
    mov es, cx                                ; 8e c1                       ; 0xc34eb vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34ed
    inc bx                                    ; 43                          ; 0xc34f0 vgabios.c:2304
    inc bx                                    ; 43                          ; 0xc34f1
    mov si, 0010eh                            ; be 0e 01                    ; 0xc34f2 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc34f5
    mov es, ax                                ; 8e c0                       ; 0xc34f7
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34f9
    mov es, cx                                ; 8e c1                       ; 0xc34fc vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34fe
    inc bx                                    ; 43                          ; 0xc3501 vgabios.c:2305
    inc bx                                    ; 43                          ; 0xc3502
    test byte [bp-00ch], 004h                 ; f6 46 f4 04                 ; 0xc3503 vgabios.c:2307
    je short 03555h                           ; 74 4c                       ; 0xc3507
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc3509 vgabios.c:2309
    in AL, DX                                 ; ec                          ; 0xc350c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc350d
    mov es, cx                                ; 8e c1                       ; 0xc350f vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3511
    inc bx                                    ; 43                          ; 0xc3514 vgabios.c:2309
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc3515
    in AL, DX                                 ; ec                          ; 0xc3518
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3519
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc351b vgabios.c:52
    inc bx                                    ; 43                          ; 0xc351e vgabios.c:2310
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc351f
    in AL, DX                                 ; ec                          ; 0xc3522
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3523
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3525 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3528 vgabios.c:2311
    xor al, al                                ; 30 c0                       ; 0xc3529
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc352b
    out DX, AL                                ; ee                          ; 0xc352e
    xor ah, ah                                ; 30 e4                       ; 0xc352f vgabios.c:2314
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3531
    jmp short 0353dh                          ; eb 07                       ; 0xc3534
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc3536
    jnc short 0354eh                          ; 73 11                       ; 0xc353b
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc353d vgabios.c:2315
    in AL, DX                                 ; ec                          ; 0xc3540
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3541
    mov es, cx                                ; 8e c1                       ; 0xc3543 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3545
    inc bx                                    ; 43                          ; 0xc3548 vgabios.c:2315
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3549 vgabios.c:2316
    jmp short 03536h                          ; eb e8                       ; 0xc354c
    mov es, cx                                ; 8e c1                       ; 0xc354e vgabios.c:52
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3550
    inc bx                                    ; 43                          ; 0xc3554 vgabios.c:2317
    mov ax, bx                                ; 89 d8                       ; 0xc3555 vgabios.c:2320
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3557
    pop di                                    ; 5f                          ; 0xc355a
    pop si                                    ; 5e                          ; 0xc355b
    pop cx                                    ; 59                          ; 0xc355c
    pop bp                                    ; 5d                          ; 0xc355d
    retn                                      ; c3                          ; 0xc355e
  ; disGetNextSymbol 0xc355f LB 0xfd3 -> off=0x0 cb=00000000000002ba uValue=00000000000c355f 'biosfn_restore_video_state'
biosfn_restore_video_state:                  ; 0xc355f LB 0x2ba
    push bp                                   ; 55                          ; 0xc355f vgabios.c:2322
    mov bp, sp                                ; 89 e5                       ; 0xc3560
    push cx                                   ; 51                          ; 0xc3562
    push si                                   ; 56                          ; 0xc3563
    push di                                   ; 57                          ; 0xc3564
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc3565
    push ax                                   ; 50                          ; 0xc3568
    mov cx, dx                                ; 89 d1                       ; 0xc3569
    test byte [bp-010h], 001h                 ; f6 46 f0 01                 ; 0xc356b vgabios.c:2326
    je short 035e5h                           ; 74 74                       ; 0xc356f
    mov dx, 003dah                            ; ba da 03                    ; 0xc3571 vgabios.c:2328
    in AL, DX                                 ; ec                          ; 0xc3574
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3575
    lea si, [bx+040h]                         ; 8d 77 40                    ; 0xc3577 vgabios.c:2330
    mov es, cx                                ; 8e c1                       ; 0xc357a vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc357c
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc357f vgabios.c:58
    mov si, bx                                ; 89 de                       ; 0xc3582 vgabios.c:2331
    mov word [bp-008h], strict word 00001h    ; c7 46 f8 01 00              ; 0xc3584 vgabios.c:2334
    add bx, strict byte 00005h                ; 83 c3 05                    ; 0xc3589 vgabios.c:2332
    jmp short 03594h                          ; eb 06                       ; 0xc358c
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc358e
    jnbe short 035aah                         ; 77 16                       ; 0xc3592
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3594 vgabios.c:2335
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3597
    out DX, AL                                ; ee                          ; 0xc359a
    mov es, cx                                ; 8e c1                       ; 0xc359b vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc359d
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc35a0 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35a3
    inc bx                                    ; 43                          ; 0xc35a4 vgabios.c:2336
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc35a5 vgabios.c:2337
    jmp short 0358eh                          ; eb e4                       ; 0xc35a8
    xor al, al                                ; 30 c0                       ; 0xc35aa vgabios.c:2338
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc35ac
    out DX, AL                                ; ee                          ; 0xc35af
    mov es, cx                                ; 8e c1                       ; 0xc35b0 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35b2
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc35b5 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35b8
    inc bx                                    ; 43                          ; 0xc35b9 vgabios.c:2339
    mov dx, 003cch                            ; ba cc 03                    ; 0xc35ba
    in AL, DX                                 ; ec                          ; 0xc35bd
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc35be
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc35c0
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc35c2
    cmp word [bp-00ch], 003d4h                ; 81 7e f4 d4 03              ; 0xc35c5 vgabios.c:2343
    jne short 035d0h                          ; 75 04                       ; 0xc35ca
    or byte [bp-00eh], 001h                   ; 80 4e f2 01                 ; 0xc35cc vgabios.c:2344
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc35d0 vgabios.c:2345
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc35d3
    out DX, AL                                ; ee                          ; 0xc35d6
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc35d7 vgabios.c:2348
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc35da
    out DX, ax                                ; ef                          ; 0xc35dd
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc35de vgabios.c:2350
    jmp short 035eeh                          ; eb 09                       ; 0xc35e3
    jmp near 036a8h                           ; e9 c0 00                    ; 0xc35e5
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc35e8
    jnbe short 03608h                         ; 77 1a                       ; 0xc35ec
    cmp word [bp-008h], strict byte 00011h    ; 83 7e f8 11                 ; 0xc35ee vgabios.c:2351
    je short 03602h                           ; 74 0e                       ; 0xc35f2
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc35f4 vgabios.c:2352
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc35f7
    out DX, AL                                ; ee                          ; 0xc35fa
    mov es, cx                                ; 8e c1                       ; 0xc35fb vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35fd
    inc dx                                    ; 42                          ; 0xc3600 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3601
    inc bx                                    ; 43                          ; 0xc3602 vgabios.c:2355
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3603 vgabios.c:2356
    jmp short 035e8h                          ; eb e0                       ; 0xc3606
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc3608 vgabios.c:2358
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc360a
    out DX, AL                                ; ee                          ; 0xc360d
    lea di, [word bx-00007h]                  ; 8d bf f9 ff                 ; 0xc360e vgabios.c:2359
    mov es, cx                                ; 8e c1                       ; 0xc3612 vgabios.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc3614
    inc dx                                    ; 42                          ; 0xc3617 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3618
    lea di, [si+003h]                         ; 8d 7c 03                    ; 0xc3619 vgabios.c:2362
    mov dl, byte [es:di]                      ; 26 8a 15                    ; 0xc361c vgabios.c:47
    xor dh, dh                                ; 30 f6                       ; 0xc361f vgabios.c:48
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3621
    mov dx, 003dah                            ; ba da 03                    ; 0xc3624 vgabios.c:2363
    in AL, DX                                 ; ec                          ; 0xc3627
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3628
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc362a vgabios.c:2364
    jmp short 03637h                          ; eb 06                       ; 0xc362f
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc3631
    jnbe short 03650h                         ; 77 19                       ; 0xc3635
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3637 vgabios.c:2365
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc363a
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc363d
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3640
    out DX, AL                                ; ee                          ; 0xc3643
    mov es, cx                                ; 8e c1                       ; 0xc3644 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3646
    out DX, AL                                ; ee                          ; 0xc3649 vgabios.c:48
    inc bx                                    ; 43                          ; 0xc364a vgabios.c:2366
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc364b vgabios.c:2367
    jmp short 03631h                          ; eb e1                       ; 0xc364e
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc3650 vgabios.c:2368
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3653
    out DX, AL                                ; ee                          ; 0xc3656
    mov dx, 003dah                            ; ba da 03                    ; 0xc3657 vgabios.c:2369
    in AL, DX                                 ; ec                          ; 0xc365a
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc365b
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc365d vgabios.c:2371
    jmp short 0366ah                          ; eb 06                       ; 0xc3662
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc3664
    jnbe short 03680h                         ; 77 16                       ; 0xc3668
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc366a vgabios.c:2372
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc366d
    out DX, AL                                ; ee                          ; 0xc3670
    mov es, cx                                ; 8e c1                       ; 0xc3671 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3673
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc3676 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3679
    inc bx                                    ; 43                          ; 0xc367a vgabios.c:2373
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc367b vgabios.c:2374
    jmp short 03664h                          ; eb e4                       ; 0xc367e
    add bx, strict byte 00006h                ; 83 c3 06                    ; 0xc3680 vgabios.c:2375
    mov es, cx                                ; 8e c1                       ; 0xc3683 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3685
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3688 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc368b
    inc si                                    ; 46                          ; 0xc368c vgabios.c:2378
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc368d vgabios.c:47
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc3690 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3693
    inc si                                    ; 46                          ; 0xc3694 vgabios.c:2379
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3695 vgabios.c:47
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc3698 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc369b
    inc si                                    ; 46                          ; 0xc369c vgabios.c:2380
    inc si                                    ; 46                          ; 0xc369d
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc369e vgabios.c:47
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc36a1 vgabios.c:48
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc36a4
    out DX, AL                                ; ee                          ; 0xc36a7
    test byte [bp-010h], 002h                 ; f6 46 f0 02                 ; 0xc36a8 vgabios.c:2384
    jne short 036b1h                          ; 75 03                       ; 0xc36ac
    jmp near 037cch                           ; e9 1b 01                    ; 0xc36ae
    mov es, cx                                ; 8e c1                       ; 0xc36b1 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc36b3
    mov si, strict word 00049h                ; be 49 00                    ; 0xc36b6 vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc36b9
    mov es, dx                                ; 8e c2                       ; 0xc36bc
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc36be
    inc bx                                    ; 43                          ; 0xc36c1 vgabios.c:2385
    mov es, cx                                ; 8e c1                       ; 0xc36c2 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc36c4
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc36c7 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc36ca
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc36cc
    inc bx                                    ; 43                          ; 0xc36cf vgabios.c:2386
    inc bx                                    ; 43                          ; 0xc36d0
    mov es, cx                                ; 8e c1                       ; 0xc36d1 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc36d3
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc36d6 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc36d9
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc36db
    inc bx                                    ; 43                          ; 0xc36de vgabios.c:2387
    inc bx                                    ; 43                          ; 0xc36df
    mov es, cx                                ; 8e c1                       ; 0xc36e0 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc36e2
    mov si, strict word 00063h                ; be 63 00                    ; 0xc36e5 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc36e8
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc36ea
    inc bx                                    ; 43                          ; 0xc36ed vgabios.c:2388
    inc bx                                    ; 43                          ; 0xc36ee
    mov es, cx                                ; 8e c1                       ; 0xc36ef vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc36f1
    mov si, 00084h                            ; be 84 00                    ; 0xc36f4 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc36f7
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc36f9
    inc bx                                    ; 43                          ; 0xc36fc vgabios.c:2389
    mov es, cx                                ; 8e c1                       ; 0xc36fd vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc36ff
    mov si, 00085h                            ; be 85 00                    ; 0xc3702 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3705
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3707
    inc bx                                    ; 43                          ; 0xc370a vgabios.c:2390
    inc bx                                    ; 43                          ; 0xc370b
    mov es, cx                                ; 8e c1                       ; 0xc370c vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc370e
    mov si, 00087h                            ; be 87 00                    ; 0xc3711 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3714
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3716
    inc bx                                    ; 43                          ; 0xc3719 vgabios.c:2391
    mov es, cx                                ; 8e c1                       ; 0xc371a vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc371c
    mov si, 00088h                            ; be 88 00                    ; 0xc371f vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3722
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3724
    inc bx                                    ; 43                          ; 0xc3727 vgabios.c:2392
    mov es, cx                                ; 8e c1                       ; 0xc3728 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc372a
    mov si, 00089h                            ; be 89 00                    ; 0xc372d vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3730
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3732
    inc bx                                    ; 43                          ; 0xc3735 vgabios.c:2393
    mov es, cx                                ; 8e c1                       ; 0xc3736 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3738
    mov si, strict word 00060h                ; be 60 00                    ; 0xc373b vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc373e
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3740
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3743 vgabios.c:2395
    inc bx                                    ; 43                          ; 0xc3748 vgabios.c:2394
    inc bx                                    ; 43                          ; 0xc3749
    jmp short 03752h                          ; eb 06                       ; 0xc374a
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc374c
    jnc short 0376eh                          ; 73 1c                       ; 0xc3750
    mov es, cx                                ; 8e c1                       ; 0xc3752 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3754
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc3757 vgabios.c:58
    add si, si                                ; 01 f6                       ; 0xc375a
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc375c
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc375f vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3762
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3764
    inc bx                                    ; 43                          ; 0xc3767 vgabios.c:2397
    inc bx                                    ; 43                          ; 0xc3768
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3769 vgabios.c:2398
    jmp short 0374ch                          ; eb de                       ; 0xc376c
    mov es, cx                                ; 8e c1                       ; 0xc376e vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3770
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc3773 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3776
    mov es, dx                                ; 8e c2                       ; 0xc3779
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc377b
    inc bx                                    ; 43                          ; 0xc377e vgabios.c:2399
    inc bx                                    ; 43                          ; 0xc377f
    mov es, cx                                ; 8e c1                       ; 0xc3780 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3782
    mov si, strict word 00062h                ; be 62 00                    ; 0xc3785 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3788
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc378a
    inc bx                                    ; 43                          ; 0xc378d vgabios.c:2400
    mov es, cx                                ; 8e c1                       ; 0xc378e vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3790
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc3793 vgabios.c:62
    xor dx, dx                                ; 31 d2                       ; 0xc3796
    mov es, dx                                ; 8e c2                       ; 0xc3798
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc379a
    inc bx                                    ; 43                          ; 0xc379d vgabios.c:2402
    inc bx                                    ; 43                          ; 0xc379e
    mov es, cx                                ; 8e c1                       ; 0xc379f vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37a1
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc37a4 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37a7
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37a9
    inc bx                                    ; 43                          ; 0xc37ac vgabios.c:2403
    inc bx                                    ; 43                          ; 0xc37ad
    mov es, cx                                ; 8e c1                       ; 0xc37ae vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37b0
    mov si, 0010ch                            ; be 0c 01                    ; 0xc37b3 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37b6
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37b8
    inc bx                                    ; 43                          ; 0xc37bb vgabios.c:2404
    inc bx                                    ; 43                          ; 0xc37bc
    mov es, cx                                ; 8e c1                       ; 0xc37bd vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37bf
    mov si, 0010eh                            ; be 0e 01                    ; 0xc37c2 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37c5
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37c7
    inc bx                                    ; 43                          ; 0xc37ca vgabios.c:2405
    inc bx                                    ; 43                          ; 0xc37cb
    test byte [bp-010h], 004h                 ; f6 46 f0 04                 ; 0xc37cc vgabios.c:2407
    je short 0380fh                           ; 74 3d                       ; 0xc37d0
    inc bx                                    ; 43                          ; 0xc37d2 vgabios.c:2408
    mov es, cx                                ; 8e c1                       ; 0xc37d3 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc37d5
    xor ah, ah                                ; 30 e4                       ; 0xc37d8 vgabios.c:48
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc37da
    inc bx                                    ; 43                          ; 0xc37dd vgabios.c:2409
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc37de vgabios.c:47
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc37e1 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc37e4
    inc bx                                    ; 43                          ; 0xc37e5 vgabios.c:2410
    xor al, al                                ; 30 c0                       ; 0xc37e6
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc37e8
    out DX, AL                                ; ee                          ; 0xc37eb
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc37ec vgabios.c:2413
    jmp short 037f8h                          ; eb 07                       ; 0xc37ef
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc37f1
    jnc short 03807h                          ; 73 0f                       ; 0xc37f6
    mov es, cx                                ; 8e c1                       ; 0xc37f8 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc37fa
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc37fd vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3800
    inc bx                                    ; 43                          ; 0xc3801 vgabios.c:2414
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3802 vgabios.c:2415
    jmp short 037f1h                          ; eb ea                       ; 0xc3805
    inc bx                                    ; 43                          ; 0xc3807 vgabios.c:2416
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc3808
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc380b
    out DX, AL                                ; ee                          ; 0xc380e
    mov ax, bx                                ; 89 d8                       ; 0xc380f vgabios.c:2420
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3811
    pop di                                    ; 5f                          ; 0xc3814
    pop si                                    ; 5e                          ; 0xc3815
    pop cx                                    ; 59                          ; 0xc3816
    pop bp                                    ; 5d                          ; 0xc3817
    retn                                      ; c3                          ; 0xc3818
  ; disGetNextSymbol 0xc3819 LB 0xd19 -> off=0x0 cb=0000000000000028 uValue=00000000000c3819 'find_vga_entry'
find_vga_entry:                              ; 0xc3819 LB 0x28
    push bx                                   ; 53                          ; 0xc3819 vgabios.c:2429
    push dx                                   ; 52                          ; 0xc381a
    push bp                                   ; 55                          ; 0xc381b
    mov bp, sp                                ; 89 e5                       ; 0xc381c
    mov dl, al                                ; 88 c2                       ; 0xc381e
    mov AH, strict byte 0ffh                  ; b4 ff                       ; 0xc3820 vgabios.c:2431
    xor al, al                                ; 30 c0                       ; 0xc3822 vgabios.c:2432
    jmp short 0382ch                          ; eb 06                       ; 0xc3824
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc3826 vgabios.c:2433
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc3828
    jnbe short 0383bh                         ; 77 0f                       ; 0xc382a
    mov bl, al                                ; 88 c3                       ; 0xc382c
    xor bh, bh                                ; 30 ff                       ; 0xc382e
    sal bx, 003h                              ; c1 e3 03                    ; 0xc3830
    cmp dl, byte [bx+047b1h]                  ; 3a 97 b1 47                 ; 0xc3833
    jne short 03826h                          ; 75 ed                       ; 0xc3837
    mov ah, al                                ; 88 c4                       ; 0xc3839
    mov al, ah                                ; 88 e0                       ; 0xc383b vgabios.c:2438
    pop bp                                    ; 5d                          ; 0xc383d
    pop dx                                    ; 5a                          ; 0xc383e
    pop bx                                    ; 5b                          ; 0xc383f
    retn                                      ; c3                          ; 0xc3840
  ; disGetNextSymbol 0xc3841 LB 0xcf1 -> off=0x0 cb=000000000000000e uValue=00000000000c3841 'readx_byte'
readx_byte:                                  ; 0xc3841 LB 0xe
    push bx                                   ; 53                          ; 0xc3841 vgabios.c:2450
    push bp                                   ; 55                          ; 0xc3842
    mov bp, sp                                ; 89 e5                       ; 0xc3843
    mov bx, dx                                ; 89 d3                       ; 0xc3845
    mov es, ax                                ; 8e c0                       ; 0xc3847 vgabios.c:2452
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3849
    pop bp                                    ; 5d                          ; 0xc384c vgabios.c:2453
    pop bx                                    ; 5b                          ; 0xc384d
    retn                                      ; c3                          ; 0xc384e
  ; disGetNextSymbol 0xc384f LB 0xce3 -> off=0x8a cb=000000000000049f uValue=00000000000c38d9 'int10_func'
    db  056h, 04fh, 01ch, 01bh, 013h, 012h, 011h, 010h, 00eh, 00dh, 00ch, 00ah, 009h, 008h, 007h, 006h
    db  005h, 004h, 003h, 002h, 001h, 000h, 071h, 03dh, 002h, 039h, 03fh, 039h, 054h, 039h, 064h, 039h
    db  077h, 039h, 087h, 039h, 091h, 039h, 0d3h, 039h, 007h, 03ah, 018h, 03ah, 03eh, 03ah, 059h, 03ah
    db  078h, 03ah, 095h, 03ah, 0abh, 03ah, 0b7h, 03ah, 0b0h, 03bh, 034h, 03ch, 061h, 03ch, 076h, 03ch
    db  0b8h, 03ch, 043h, 03dh, 030h, 024h, 023h, 022h, 021h, 020h, 014h, 012h, 011h, 010h, 004h, 003h
    db  002h, 001h, 000h, 071h, 03dh, 0d6h, 03ah, 0f4h, 03ah, 00fh, 03bh, 024h, 03bh, 02fh, 03bh, 0d6h
    db  03ah, 0f4h, 03ah, 00fh, 03bh, 02fh, 03bh, 044h, 03bh, 04fh, 03bh, 06ah, 03bh, 079h, 03bh, 088h
    db  03bh, 097h, 03bh, 00ah, 009h, 006h, 004h, 002h, 001h, 000h, 035h, 03dh, 0deh, 03ch, 0ech, 03ch
    db  0fdh, 03ch, 00dh, 03dh, 022h, 03dh, 035h, 03dh, 035h, 03dh
int10_func:                                  ; 0xc38d9 LB 0x49f
    push bp                                   ; 55                          ; 0xc38d9 vgabios.c:2531
    mov bp, sp                                ; 89 e5                       ; 0xc38da
    push si                                   ; 56                          ; 0xc38dc
    push di                                   ; 57                          ; 0xc38dd
    push ax                                   ; 50                          ; 0xc38de
    mov si, word [bp+004h]                    ; 8b 76 04                    ; 0xc38df
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc38e2 vgabios.c:2536
    shr ax, 008h                              ; c1 e8 08                    ; 0xc38e5
    cmp ax, strict word 00056h                ; 3d 56 00                    ; 0xc38e8
    jnbe short 03951h                         ; 77 64                       ; 0xc38eb
    push CS                                   ; 0e                          ; 0xc38ed
    pop ES                                    ; 07                          ; 0xc38ee
    mov cx, strict word 00017h                ; b9 17 00                    ; 0xc38ef
    mov di, 0384fh                            ; bf 4f 38                    ; 0xc38f2
    repne scasb                               ; f2 ae                       ; 0xc38f5
    sal cx, 1                                 ; d1 e1                       ; 0xc38f7
    mov di, cx                                ; 89 cf                       ; 0xc38f9
    mov ax, word [cs:di+03865h]               ; 2e 8b 85 65 38              ; 0xc38fb
    jmp ax                                    ; ff e0                       ; 0xc3900
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3902 vgabios.c:2539
    xor ah, ah                                ; 30 e4                       ; 0xc3905
    call 0143fh                               ; e8 35 db                    ; 0xc3907
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc390a vgabios.c:2540
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc390d
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc3910
    je short 0392ah                           ; 74 15                       ; 0xc3913
    cmp ax, strict word 00006h                ; 3d 06 00                    ; 0xc3915
    je short 03921h                           ; 74 07                       ; 0xc3918
    cmp ax, strict word 00005h                ; 3d 05 00                    ; 0xc391a
    jbe short 0392ah                          ; 76 0b                       ; 0xc391d
    jmp short 03933h                          ; eb 12                       ; 0xc391f
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3921 vgabios.c:2542
    xor al, al                                ; 30 c0                       ; 0xc3924
    or AL, strict byte 03fh                   ; 0c 3f                       ; 0xc3926
    jmp short 0393ah                          ; eb 10                       ; 0xc3928 vgabios.c:2543
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc392a vgabios.c:2551
    xor al, al                                ; 30 c0                       ; 0xc392d
    or AL, strict byte 030h                   ; 0c 30                       ; 0xc392f
    jmp short 0393ah                          ; eb 07                       ; 0xc3931
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3933 vgabios.c:2554
    xor al, al                                ; 30 c0                       ; 0xc3936
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc3938
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc393a
    jmp short 03951h                          ; eb 12                       ; 0xc393d vgabios.c:2556
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc393f vgabios.c:2558
    xor ah, ah                                ; 30 e4                       ; 0xc3942
    mov dx, ax                                ; 89 c2                       ; 0xc3944
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3946
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3949
    xor ah, ah                                ; 30 e4                       ; 0xc394c
    call 0118ch                               ; e8 3b d8                    ; 0xc394e
    jmp near 03d71h                           ; e9 1d 04                    ; 0xc3951 vgabios.c:2559
    mov dx, word [bp+00eh]                    ; 8b 56 0e                    ; 0xc3954 vgabios.c:2561
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3957
    shr ax, 008h                              ; c1 e8 08                    ; 0xc395a
    xor ah, ah                                ; 30 e4                       ; 0xc395d
    call 01293h                               ; e8 31 d9                    ; 0xc395f
    jmp short 03951h                          ; eb ed                       ; 0xc3962 vgabios.c:2562
    lea bx, [bp+00eh]                         ; 8d 5e 0e                    ; 0xc3964 vgabios.c:2564
    lea dx, [bp+010h]                         ; 8d 56 10                    ; 0xc3967
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc396a
    shr ax, 008h                              ; c1 e8 08                    ; 0xc396d
    xor ah, ah                                ; 30 e4                       ; 0xc3970
    call 00a96h                               ; e8 21 d1                    ; 0xc3972
    jmp short 03951h                          ; eb da                       ; 0xc3975 vgabios.c:2565
    xor ax, ax                                ; 31 c0                       ; 0xc3977 vgabios.c:2571
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc3979
    mov word [bp+00ch], ax                    ; 89 46 0c                    ; 0xc397c vgabios.c:2572
    mov word [bp+010h], ax                    ; 89 46 10                    ; 0xc397f vgabios.c:2573
    mov word [bp+00eh], ax                    ; 89 46 0e                    ; 0xc3982 vgabios.c:2574
    jmp short 03951h                          ; eb ca                       ; 0xc3985 vgabios.c:2575
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3987 vgabios.c:2577
    xor ah, ah                                ; 30 e4                       ; 0xc398a
    call 01322h                               ; e8 93 d9                    ; 0xc398c
    jmp short 03951h                          ; eb c0                       ; 0xc398f vgabios.c:2578
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3991 vgabios.c:2580
    push ax                                   ; 50                          ; 0xc3994
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc3995
    push ax                                   ; 50                          ; 0xc3998
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3999
    xor ah, ah                                ; 30 e4                       ; 0xc399c
    push ax                                   ; 50                          ; 0xc399e
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc399f
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39a2
    xor ah, ah                                ; 30 e4                       ; 0xc39a5
    push ax                                   ; 50                          ; 0xc39a7
    mov cl, byte [bp+010h]                    ; 8a 4e 10                    ; 0xc39a8
    xor ch, ch                                ; 30 ed                       ; 0xc39ab
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc39ad
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39b0
    xor ah, ah                                ; 30 e4                       ; 0xc39b3
    mov bx, ax                                ; 89 c3                       ; 0xc39b5
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc39b7
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39ba
    xor ah, ah                                ; 30 e4                       ; 0xc39bd
    mov dx, ax                                ; 89 c2                       ; 0xc39bf
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc39c1
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc39c4
    mov byte [bp-005h], ch                    ; 88 6e fb                    ; 0xc39c7
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc39ca
    call 01c55h                               ; e8 85 e2                    ; 0xc39cd
    jmp near 03d71h                           ; e9 9e 03                    ; 0xc39d0 vgabios.c:2581
    xor ax, ax                                ; 31 c0                       ; 0xc39d3 vgabios.c:2583
    push ax                                   ; 50                          ; 0xc39d5
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc39d6
    push ax                                   ; 50                          ; 0xc39d9
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc39da
    xor ah, ah                                ; 30 e4                       ; 0xc39dd
    push ax                                   ; 50                          ; 0xc39df
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc39e0
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39e3
    xor ah, ah                                ; 30 e4                       ; 0xc39e6
    push ax                                   ; 50                          ; 0xc39e8
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc39e9
    mov cx, ax                                ; 89 c1                       ; 0xc39ec
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc39ee
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39f1
    xor ah, ah                                ; 30 e4                       ; 0xc39f4
    mov bx, ax                                ; 89 c3                       ; 0xc39f6
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc39f8
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39fb
    xor ah, ah                                ; 30 e4                       ; 0xc39fe
    mov dx, ax                                ; 89 c2                       ; 0xc3a00
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a02
    jmp short 039cdh                          ; eb c6                       ; 0xc3a05
    lea dx, [bp+012h]                         ; 8d 56 12                    ; 0xc3a07 vgabios.c:2586
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a0a
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a0d
    xor ah, ah                                ; 30 e4                       ; 0xc3a10
    call 00dd6h                               ; e8 c1 d3                    ; 0xc3a12
    jmp near 03d71h                           ; e9 59 03                    ; 0xc3a15 vgabios.c:2587
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a18 vgabios.c:2589
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3a1b
    xor ah, ah                                ; 30 e4                       ; 0xc3a1e
    mov bx, ax                                ; 89 c3                       ; 0xc3a20
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a22
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a25
    xor ah, ah                                ; 30 e4                       ; 0xc3a28
    mov dx, ax                                ; 89 c2                       ; 0xc3a2a
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a2c
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc3a2f
    mov byte [bp-005h], bh                    ; 88 7e fb                    ; 0xc3a32
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc3a35
    call 0259bh                               ; e8 60 eb                    ; 0xc3a38
    jmp near 03d71h                           ; e9 33 03                    ; 0xc3a3b vgabios.c:2590
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a3e vgabios.c:2592
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3a41
    xor ah, ah                                ; 30 e4                       ; 0xc3a44
    mov bx, ax                                ; 89 c3                       ; 0xc3a46
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3a48
    shr dx, 008h                              ; c1 ea 08                    ; 0xc3a4b
    xor dh, dh                                ; 30 f6                       ; 0xc3a4e
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a50
    call 02722h                               ; e8 cc ec                    ; 0xc3a53
    jmp near 03d71h                           ; e9 18 03                    ; 0xc3a56 vgabios.c:2593
    mov cx, word [bp+00eh]                    ; 8b 4e 0e                    ; 0xc3a59 vgabios.c:2595
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc3a5c
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a5f
    xor ah, ah                                ; 30 e4                       ; 0xc3a62
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3a64
    shr dx, 008h                              ; c1 ea 08                    ; 0xc3a67
    xor dh, dh                                ; 30 f6                       ; 0xc3a6a
    mov si, dx                                ; 89 d6                       ; 0xc3a6c
    mov dx, ax                                ; 89 c2                       ; 0xc3a6e
    mov ax, si                                ; 89 f0                       ; 0xc3a70
    call 028a3h                               ; e8 2e ee                    ; 0xc3a72
    jmp near 03d71h                           ; e9 f9 02                    ; 0xc3a75 vgabios.c:2596
    lea cx, [bp+012h]                         ; 8d 4e 12                    ; 0xc3a78 vgabios.c:2598
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3a7b
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3a7e
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a81
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3a84
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc3a87
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3a8a
    xor ah, ah                                ; 30 e4                       ; 0xc3a8d
    call 00f99h                               ; e8 07 d5                    ; 0xc3a8f
    jmp near 03d71h                           ; e9 dc 02                    ; 0xc3a92 vgabios.c:2599
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc3a95 vgabios.c:2607
    mov bl, byte [bp+00ch]                    ; 8a 5e 0c                    ; 0xc3a98
    xor bh, bh                                ; 30 ff                       ; 0xc3a9b
    mov dx, 000ffh                            ; ba ff 00                    ; 0xc3a9d
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3aa0
    xor ah, ah                                ; 30 e4                       ; 0xc3aa3
    call 02a16h                               ; e8 6e ef                    ; 0xc3aa5
    jmp near 03d71h                           ; e9 c6 02                    ; 0xc3aa8 vgabios.c:2608
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3aab vgabios.c:2611
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3aae
    call 010ffh                               ; e8 4b d6                    ; 0xc3ab1
    jmp near 03d71h                           ; e9 ba 02                    ; 0xc3ab4 vgabios.c:2612
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3ab7 vgabios.c:2614
    xor ah, ah                                ; 30 e4                       ; 0xc3aba
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3abc
    jnbe short 03b2ch                         ; 77 6b                       ; 0xc3abf
    push CS                                   ; 0e                          ; 0xc3ac1
    pop ES                                    ; 07                          ; 0xc3ac2
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc3ac3
    mov di, 03893h                            ; bf 93 38                    ; 0xc3ac6
    repne scasb                               ; f2 ae                       ; 0xc3ac9
    sal cx, 1                                 ; d1 e1                       ; 0xc3acb
    mov di, cx                                ; 89 cf                       ; 0xc3acd
    mov ax, word [cs:di+038a2h]               ; 2e 8b 85 a2 38              ; 0xc3acf
    jmp ax                                    ; ff e0                       ; 0xc3ad4
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3ad6 vgabios.c:2618
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3ad9
    xor ah, ah                                ; 30 e4                       ; 0xc3adc
    push ax                                   ; 50                          ; 0xc3ade
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3adf
    push ax                                   ; 50                          ; 0xc3ae2
    push word [bp+00eh]                       ; ff 76 0e                    ; 0xc3ae3
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3ae6
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3ae9
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc3aec
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3aef
    jmp short 03b0ah                          ; eb 16                       ; 0xc3af2
    push strict byte 0000eh                   ; 6a 0e                       ; 0xc3af4 vgabios.c:2622
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3af6
    xor ah, ah                                ; 30 e4                       ; 0xc3af9
    push ax                                   ; 50                          ; 0xc3afb
    push strict byte 00000h                   ; 6a 00                       ; 0xc3afc
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3afe
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b01
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc3b04
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc3b07
    call 02e1ah                               ; e8 0d f3                    ; 0xc3b0a
    jmp short 03b2ch                          ; eb 1d                       ; 0xc3b0d
    push strict byte 00008h                   ; 6a 08                       ; 0xc3b0f vgabios.c:2626
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b11
    xor ah, ah                                ; 30 e4                       ; 0xc3b14
    push ax                                   ; 50                          ; 0xc3b16
    push strict byte 00000h                   ; 6a 00                       ; 0xc3b17
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b19
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b1c
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc3b1f
    jmp short 03b07h                          ; eb e3                       ; 0xc3b22
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b24 vgabios.c:2629
    xor ah, ah                                ; 30 e4                       ; 0xc3b27
    call 02d82h                               ; e8 56 f2                    ; 0xc3b29
    jmp near 03d71h                           ; e9 42 02                    ; 0xc3b2c vgabios.c:2630
    push strict byte 00010h                   ; 6a 10                       ; 0xc3b2f vgabios.c:2633
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b31
    xor ah, ah                                ; 30 e4                       ; 0xc3b34
    push ax                                   ; 50                          ; 0xc3b36
    push strict byte 00000h                   ; 6a 00                       ; 0xc3b37
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b39
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b3c
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc3b3f
    jmp short 03b07h                          ; eb c3                       ; 0xc3b42
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc3b44 vgabios.c:2636
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc3b47
    call 02e99h                               ; e8 4c f3                    ; 0xc3b4a
    jmp short 03b2ch                          ; eb dd                       ; 0xc3b4d vgabios.c:2637
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b4f vgabios.c:2639
    xor ah, ah                                ; 30 e4                       ; 0xc3b52
    push ax                                   ; 50                          ; 0xc3b54
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b55
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc3b58
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc3b5b
    mov si, word [bp+016h]                    ; 8b 76 16                    ; 0xc3b5e
    mov cx, ax                                ; 89 c1                       ; 0xc3b61
    mov ax, si                                ; 89 f0                       ; 0xc3b63
    call 02efch                               ; e8 94 f3                    ; 0xc3b65
    jmp short 03b2ch                          ; eb c2                       ; 0xc3b68 vgabios.c:2640
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b6a vgabios.c:2642
    xor ah, ah                                ; 30 e4                       ; 0xc3b6d
    mov dx, ax                                ; 89 c2                       ; 0xc3b6f
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b71
    call 02f19h                               ; e8 a2 f3                    ; 0xc3b74
    jmp short 03b2ch                          ; eb b3                       ; 0xc3b77 vgabios.c:2643
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b79 vgabios.c:2645
    xor ah, ah                                ; 30 e4                       ; 0xc3b7c
    mov dx, ax                                ; 89 c2                       ; 0xc3b7e
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b80
    call 02f3bh                               ; e8 b5 f3                    ; 0xc3b83
    jmp short 03b2ch                          ; eb a4                       ; 0xc3b86 vgabios.c:2646
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b88 vgabios.c:2648
    xor ah, ah                                ; 30 e4                       ; 0xc3b8b
    mov dx, ax                                ; 89 c2                       ; 0xc3b8d
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b8f
    call 02f5dh                               ; e8 c8 f3                    ; 0xc3b92
    jmp short 03b2ch                          ; eb 95                       ; 0xc3b95 vgabios.c:2649
    lea ax, [bp+00eh]                         ; 8d 46 0e                    ; 0xc3b97 vgabios.c:2651
    push ax                                   ; 50                          ; 0xc3b9a
    lea cx, [bp+010h]                         ; 8d 4e 10                    ; 0xc3b9b
    lea bx, [bp+008h]                         ; 8d 5e 08                    ; 0xc3b9e
    lea dx, [bp+016h]                         ; 8d 56 16                    ; 0xc3ba1
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3ba4
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3ba7
    call 00f16h                               ; e8 69 d3                    ; 0xc3baa
    jmp near 03d71h                           ; e9 c1 01                    ; 0xc3bad vgabios.c:2659
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3bb0 vgabios.c:2661
    xor ah, ah                                ; 30 e4                       ; 0xc3bb3
    cmp ax, strict word 00034h                ; 3d 34 00                    ; 0xc3bb5
    jc short 03bc9h                           ; 72 0f                       ; 0xc3bb8
    jbe short 03bf4h                          ; 76 38                       ; 0xc3bba
    cmp ax, strict word 00036h                ; 3d 36 00                    ; 0xc3bbc
    je short 03c1ch                           ; 74 5b                       ; 0xc3bbf
    cmp ax, strict word 00035h                ; 3d 35 00                    ; 0xc3bc1
    je short 03c1eh                           ; 74 58                       ; 0xc3bc4
    jmp near 03d71h                           ; e9 a8 01                    ; 0xc3bc6
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3bc9
    je short 03bd8h                           ; 74 0a                       ; 0xc3bcc
    cmp ax, strict word 00020h                ; 3d 20 00                    ; 0xc3bce
    jne short 03c19h                          ; 75 46                       ; 0xc3bd1
    call 02f7fh                               ; e8 a9 f3                    ; 0xc3bd3 vgabios.c:2664
    jmp short 03c19h                          ; eb 41                       ; 0xc3bd6 vgabios.c:2665
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3bd8 vgabios.c:2667
    xor ah, ah                                ; 30 e4                       ; 0xc3bdb
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3bdd
    jnbe short 03c19h                         ; 77 37                       ; 0xc3be0
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3be2 vgabios.c:2668
    call 02f84h                               ; e8 9c f3                    ; 0xc3be5
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3be8 vgabios.c:2669
    xor al, al                                ; 30 c0                       ; 0xc3beb
    or AL, strict byte 012h                   ; 0c 12                       ; 0xc3bed
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc3bef
    jmp short 03c19h                          ; eb 25                       ; 0xc3bf2 vgabios.c:2671
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3bf4 vgabios.c:2673
    xor ah, ah                                ; 30 e4                       ; 0xc3bf7
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3bf9
    jnc short 03c16h                          ; 73 18                       ; 0xc3bfc
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3bfe vgabios.c:45
    mov es, ax                                ; 8e c0                       ; 0xc3c01
    mov si, 00087h                            ; be 87 00                    ; 0xc3c03
    mov ah, byte [es:si]                      ; 26 8a 24                    ; 0xc3c06 vgabios.c:47
    and ah, 0feh                              ; 80 e4 fe                    ; 0xc3c09 vgabios.c:48
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c0c
    or al, ah                                 ; 08 e0                       ; 0xc3c0f
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3c11 vgabios.c:52
    jmp short 03be8h                          ; eb d2                       ; 0xc3c14
    mov byte [bp+012h], ah                    ; 88 66 12                    ; 0xc3c16 vgabios.c:2679
    jmp near 03d71h                           ; e9 55 01                    ; 0xc3c19 vgabios.c:2680
    jmp short 03c2ch                          ; eb 0e                       ; 0xc3c1c
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c1e vgabios.c:2682
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3c21
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3c24
    call 02fb6h                               ; e8 8c f3                    ; 0xc3c27
    jmp short 03be8h                          ; eb bc                       ; 0xc3c2a
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c2c vgabios.c:2686
    call 02fbbh                               ; e8 89 f3                    ; 0xc3c2f
    jmp short 03be8h                          ; eb b4                       ; 0xc3c32
    push word [bp+008h]                       ; ff 76 08                    ; 0xc3c34 vgabios.c:2696
    push word [bp+016h]                       ; ff 76 16                    ; 0xc3c37
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3c3a
    xor ah, ah                                ; 30 e4                       ; 0xc3c3d
    push ax                                   ; 50                          ; 0xc3c3f
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc3c40
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3c43
    xor ah, ah                                ; 30 e4                       ; 0xc3c46
    push ax                                   ; 50                          ; 0xc3c48
    mov bl, byte [bp+00ch]                    ; 8a 5e 0c                    ; 0xc3c49
    xor bh, bh                                ; 30 ff                       ; 0xc3c4c
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3c4e
    shr dx, 008h                              ; c1 ea 08                    ; 0xc3c51
    xor dh, dh                                ; 30 f6                       ; 0xc3c54
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c56
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3c59
    call 02fc0h                               ; e8 61 f3                    ; 0xc3c5c
    jmp short 03c19h                          ; eb b8                       ; 0xc3c5f vgabios.c:2697
    mov bx, si                                ; 89 f3                       ; 0xc3c61 vgabios.c:2699
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3c63
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3c66
    call 0305dh                               ; e8 f1 f3                    ; 0xc3c69
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c6c vgabios.c:2700
    xor al, al                                ; 30 c0                       ; 0xc3c6f
    or AL, strict byte 01bh                   ; 0c 1b                       ; 0xc3c71
    jmp near 03befh                           ; e9 79 ff                    ; 0xc3c73
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c76 vgabios.c:2703
    xor ah, ah                                ; 30 e4                       ; 0xc3c79
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3c7b
    je short 03ca2h                           ; 74 22                       ; 0xc3c7e
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc3c80
    je short 03c94h                           ; 74 0f                       ; 0xc3c83
    test ax, ax                               ; 85 c0                       ; 0xc3c85
    jne short 03caeh                          ; 75 25                       ; 0xc3c87
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3c89 vgabios.c:2706
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3c8c
    call 0326fh                               ; e8 dd f5                    ; 0xc3c8f
    jmp short 03caeh                          ; eb 1a                       ; 0xc3c92 vgabios.c:2707
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3c94 vgabios.c:2709
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3c97
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3c9a
    call 03287h                               ; e8 e7 f5                    ; 0xc3c9d
    jmp short 03caeh                          ; eb 0c                       ; 0xc3ca0 vgabios.c:2710
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3ca2 vgabios.c:2712
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3ca5
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3ca8
    call 0355fh                               ; e8 b1 f8                    ; 0xc3cab
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3cae vgabios.c:2719
    xor al, al                                ; 30 c0                       ; 0xc3cb1
    or AL, strict byte 01ch                   ; 0c 1c                       ; 0xc3cb3
    jmp near 03befh                           ; e9 37 ff                    ; 0xc3cb5
    call 007bfh                               ; e8 04 cb                    ; 0xc3cb8 vgabios.c:2724
    test ax, ax                               ; 85 c0                       ; 0xc3cbb
    je short 03d33h                           ; 74 74                       ; 0xc3cbd
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3cbf vgabios.c:2725
    xor ah, ah                                ; 30 e4                       ; 0xc3cc2
    cmp ax, strict word 0000ah                ; 3d 0a 00                    ; 0xc3cc4
    jnbe short 03d35h                         ; 77 6c                       ; 0xc3cc7
    push CS                                   ; 0e                          ; 0xc3cc9
    pop ES                                    ; 07                          ; 0xc3cca
    mov cx, strict word 00008h                ; b9 08 00                    ; 0xc3ccb
    mov di, 038c2h                            ; bf c2 38                    ; 0xc3cce
    repne scasb                               ; f2 ae                       ; 0xc3cd1
    sal cx, 1                                 ; d1 e1                       ; 0xc3cd3
    mov di, cx                                ; 89 cf                       ; 0xc3cd5
    mov ax, word [cs:di+038c9h]               ; 2e 8b 85 c9 38              ; 0xc3cd7
    jmp ax                                    ; ff e0                       ; 0xc3cdc
    mov bx, si                                ; 89 f3                       ; 0xc3cde vgabios.c:2728
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3ce0
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3ce3
    call 03f42h                               ; e8 59 02                    ; 0xc3ce6
    jmp near 03d71h                           ; e9 85 00                    ; 0xc3ce9 vgabios.c:2729
    mov cx, si                                ; 89 f1                       ; 0xc3cec vgabios.c:2731
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3cee
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3cf1
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3cf4
    call 0406dh                               ; e8 73 03                    ; 0xc3cf7
    jmp near 03d71h                           ; e9 74 00                    ; 0xc3cfa vgabios.c:2732
    mov cx, si                                ; 89 f1                       ; 0xc3cfd vgabios.c:2734
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3cff
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3d02
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d05
    call 0410ch                               ; e8 01 04                    ; 0xc3d08
    jmp short 03d71h                          ; eb 64                       ; 0xc3d0b vgabios.c:2735
    lea ax, [bp+00ch]                         ; 8d 46 0c                    ; 0xc3d0d vgabios.c:2737
    push ax                                   ; 50                          ; 0xc3d10
    mov cx, word [bp+016h]                    ; 8b 4e 16                    ; 0xc3d11
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3d14
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3d17
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d1a
    call 042d5h                               ; e8 b5 05                    ; 0xc3d1d
    jmp short 03d71h                          ; eb 4f                       ; 0xc3d20 vgabios.c:2738
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3d22 vgabios.c:2740
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3d25
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3d28
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d2b
    call 04361h                               ; e8 30 06                    ; 0xc3d2e
    jmp short 03d71h                          ; eb 3e                       ; 0xc3d31 vgabios.c:2741
    jmp short 03d3ch                          ; eb 07                       ; 0xc3d33
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d35 vgabios.c:2763
    jmp short 03d71h                          ; eb 35                       ; 0xc3d3a vgabios.c:2766
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d3c vgabios.c:2768
    jmp short 03d71h                          ; eb 2e                       ; 0xc3d41 vgabios.c:2770
    call 007bfh                               ; e8 79 ca                    ; 0xc3d43 vgabios.c:2772
    test ax, ax                               ; 85 c0                       ; 0xc3d46
    je short 03d6ch                           ; 74 22                       ; 0xc3d48
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3d4a vgabios.c:2773
    xor ah, ah                                ; 30 e4                       ; 0xc3d4d
    cmp ax, strict word 00042h                ; 3d 42 00                    ; 0xc3d4f
    jne short 03d65h                          ; 75 11                       ; 0xc3d52
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3d54 vgabios.c:2776
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3d57
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3d5a
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d5d
    call 04440h                               ; e8 dd 06                    ; 0xc3d60
    jmp short 03d71h                          ; eb 0c                       ; 0xc3d63 vgabios.c:2777
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d65 vgabios.c:2779
    jmp short 03d71h                          ; eb 05                       ; 0xc3d6a vgabios.c:2782
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d6c vgabios.c:2784
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3d71 vgabios.c:2794
    pop di                                    ; 5f                          ; 0xc3d74
    pop si                                    ; 5e                          ; 0xc3d75
    pop bp                                    ; 5d                          ; 0xc3d76
    retn                                      ; c3                          ; 0xc3d77
  ; disGetNextSymbol 0xc3d78 LB 0x7ba -> off=0x0 cb=000000000000001f uValue=00000000000c3d78 'dispi_set_xres'
dispi_set_xres:                              ; 0xc3d78 LB 0x1f
    push bp                                   ; 55                          ; 0xc3d78 vbe.c:100
    mov bp, sp                                ; 89 e5                       ; 0xc3d79
    push bx                                   ; 53                          ; 0xc3d7b
    push dx                                   ; 52                          ; 0xc3d7c
    mov bx, ax                                ; 89 c3                       ; 0xc3d7d
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3d7f vbe.c:105
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3d82
    call 00570h                               ; e8 e8 c7                    ; 0xc3d85
    mov ax, bx                                ; 89 d8                       ; 0xc3d88 vbe.c:106
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3d8a
    call 00570h                               ; e8 e0 c7                    ; 0xc3d8d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3d90 vbe.c:107
    pop dx                                    ; 5a                          ; 0xc3d93
    pop bx                                    ; 5b                          ; 0xc3d94
    pop bp                                    ; 5d                          ; 0xc3d95
    retn                                      ; c3                          ; 0xc3d96
  ; disGetNextSymbol 0xc3d97 LB 0x79b -> off=0x0 cb=000000000000001f uValue=00000000000c3d97 'dispi_set_yres'
dispi_set_yres:                              ; 0xc3d97 LB 0x1f
    push bp                                   ; 55                          ; 0xc3d97 vbe.c:109
    mov bp, sp                                ; 89 e5                       ; 0xc3d98
    push bx                                   ; 53                          ; 0xc3d9a
    push dx                                   ; 52                          ; 0xc3d9b
    mov bx, ax                                ; 89 c3                       ; 0xc3d9c
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3d9e vbe.c:114
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3da1
    call 00570h                               ; e8 c9 c7                    ; 0xc3da4
    mov ax, bx                                ; 89 d8                       ; 0xc3da7 vbe.c:115
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3da9
    call 00570h                               ; e8 c1 c7                    ; 0xc3dac
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3daf vbe.c:116
    pop dx                                    ; 5a                          ; 0xc3db2
    pop bx                                    ; 5b                          ; 0xc3db3
    pop bp                                    ; 5d                          ; 0xc3db4
    retn                                      ; c3                          ; 0xc3db5
  ; disGetNextSymbol 0xc3db6 LB 0x77c -> off=0x0 cb=0000000000000019 uValue=00000000000c3db6 'dispi_get_yres'
dispi_get_yres:                              ; 0xc3db6 LB 0x19
    push bp                                   ; 55                          ; 0xc3db6 vbe.c:118
    mov bp, sp                                ; 89 e5                       ; 0xc3db7
    push dx                                   ; 52                          ; 0xc3db9
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3dba vbe.c:120
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3dbd
    call 00570h                               ; e8 ad c7                    ; 0xc3dc0
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3dc3 vbe.c:121
    call 00577h                               ; e8 ae c7                    ; 0xc3dc6
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3dc9 vbe.c:122
    pop dx                                    ; 5a                          ; 0xc3dcc
    pop bp                                    ; 5d                          ; 0xc3dcd
    retn                                      ; c3                          ; 0xc3dce
  ; disGetNextSymbol 0xc3dcf LB 0x763 -> off=0x0 cb=000000000000001f uValue=00000000000c3dcf 'dispi_set_bpp'
dispi_set_bpp:                               ; 0xc3dcf LB 0x1f
    push bp                                   ; 55                          ; 0xc3dcf vbe.c:124
    mov bp, sp                                ; 89 e5                       ; 0xc3dd0
    push bx                                   ; 53                          ; 0xc3dd2
    push dx                                   ; 52                          ; 0xc3dd3
    mov bx, ax                                ; 89 c3                       ; 0xc3dd4
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3dd6 vbe.c:129
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3dd9
    call 00570h                               ; e8 91 c7                    ; 0xc3ddc
    mov ax, bx                                ; 89 d8                       ; 0xc3ddf vbe.c:130
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3de1
    call 00570h                               ; e8 89 c7                    ; 0xc3de4
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3de7 vbe.c:131
    pop dx                                    ; 5a                          ; 0xc3dea
    pop bx                                    ; 5b                          ; 0xc3deb
    pop bp                                    ; 5d                          ; 0xc3dec
    retn                                      ; c3                          ; 0xc3ded
  ; disGetNextSymbol 0xc3dee LB 0x744 -> off=0x0 cb=0000000000000019 uValue=00000000000c3dee 'dispi_get_bpp'
dispi_get_bpp:                               ; 0xc3dee LB 0x19
    push bp                                   ; 55                          ; 0xc3dee vbe.c:133
    mov bp, sp                                ; 89 e5                       ; 0xc3def
    push dx                                   ; 52                          ; 0xc3df1
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3df2 vbe.c:135
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3df5
    call 00570h                               ; e8 75 c7                    ; 0xc3df8
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3dfb vbe.c:136
    call 00577h                               ; e8 76 c7                    ; 0xc3dfe
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e01 vbe.c:137
    pop dx                                    ; 5a                          ; 0xc3e04
    pop bp                                    ; 5d                          ; 0xc3e05
    retn                                      ; c3                          ; 0xc3e06
  ; disGetNextSymbol 0xc3e07 LB 0x72b -> off=0x0 cb=000000000000001f uValue=00000000000c3e07 'dispi_set_virt_width'
dispi_set_virt_width:                        ; 0xc3e07 LB 0x1f
    push bp                                   ; 55                          ; 0xc3e07 vbe.c:139
    mov bp, sp                                ; 89 e5                       ; 0xc3e08
    push bx                                   ; 53                          ; 0xc3e0a
    push dx                                   ; 52                          ; 0xc3e0b
    mov bx, ax                                ; 89 c3                       ; 0xc3e0c
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3e0e vbe.c:144
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e11
    call 00570h                               ; e8 59 c7                    ; 0xc3e14
    mov ax, bx                                ; 89 d8                       ; 0xc3e17 vbe.c:145
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e19
    call 00570h                               ; e8 51 c7                    ; 0xc3e1c
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3e1f vbe.c:146
    pop dx                                    ; 5a                          ; 0xc3e22
    pop bx                                    ; 5b                          ; 0xc3e23
    pop bp                                    ; 5d                          ; 0xc3e24
    retn                                      ; c3                          ; 0xc3e25
  ; disGetNextSymbol 0xc3e26 LB 0x70c -> off=0x0 cb=0000000000000019 uValue=00000000000c3e26 'dispi_get_virt_width'
dispi_get_virt_width:                        ; 0xc3e26 LB 0x19
    push bp                                   ; 55                          ; 0xc3e26 vbe.c:148
    mov bp, sp                                ; 89 e5                       ; 0xc3e27
    push dx                                   ; 52                          ; 0xc3e29
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3e2a vbe.c:150
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e2d
    call 00570h                               ; e8 3d c7                    ; 0xc3e30
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e33 vbe.c:151
    call 00577h                               ; e8 3e c7                    ; 0xc3e36
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e39 vbe.c:152
    pop dx                                    ; 5a                          ; 0xc3e3c
    pop bp                                    ; 5d                          ; 0xc3e3d
    retn                                      ; c3                          ; 0xc3e3e
  ; disGetNextSymbol 0xc3e3f LB 0x6f3 -> off=0x0 cb=0000000000000019 uValue=00000000000c3e3f 'dispi_get_virt_height'
dispi_get_virt_height:                       ; 0xc3e3f LB 0x19
    push bp                                   ; 55                          ; 0xc3e3f vbe.c:154
    mov bp, sp                                ; 89 e5                       ; 0xc3e40
    push dx                                   ; 52                          ; 0xc3e42
    mov ax, strict word 00007h                ; b8 07 00                    ; 0xc3e43 vbe.c:156
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e46
    call 00570h                               ; e8 24 c7                    ; 0xc3e49
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e4c vbe.c:157
    call 00577h                               ; e8 25 c7                    ; 0xc3e4f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e52 vbe.c:158
    pop dx                                    ; 5a                          ; 0xc3e55
    pop bp                                    ; 5d                          ; 0xc3e56
    retn                                      ; c3                          ; 0xc3e57
  ; disGetNextSymbol 0xc3e58 LB 0x6da -> off=0x0 cb=0000000000000012 uValue=00000000000c3e58 'in_word'
in_word:                                     ; 0xc3e58 LB 0x12
    push bp                                   ; 55                          ; 0xc3e58 vbe.c:160
    mov bp, sp                                ; 89 e5                       ; 0xc3e59
    push bx                                   ; 53                          ; 0xc3e5b
    mov bx, ax                                ; 89 c3                       ; 0xc3e5c
    mov ax, dx                                ; 89 d0                       ; 0xc3e5e
    mov dx, bx                                ; 89 da                       ; 0xc3e60 vbe.c:162
    out DX, ax                                ; ef                          ; 0xc3e62
    in ax, DX                                 ; ed                          ; 0xc3e63 vbe.c:163
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e64 vbe.c:164
    pop bx                                    ; 5b                          ; 0xc3e67
    pop bp                                    ; 5d                          ; 0xc3e68
    retn                                      ; c3                          ; 0xc3e69
  ; disGetNextSymbol 0xc3e6a LB 0x6c8 -> off=0x0 cb=0000000000000014 uValue=00000000000c3e6a 'in_byte'
in_byte:                                     ; 0xc3e6a LB 0x14
    push bp                                   ; 55                          ; 0xc3e6a vbe.c:166
    mov bp, sp                                ; 89 e5                       ; 0xc3e6b
    push bx                                   ; 53                          ; 0xc3e6d
    mov bx, ax                                ; 89 c3                       ; 0xc3e6e
    mov ax, dx                                ; 89 d0                       ; 0xc3e70
    mov dx, bx                                ; 89 da                       ; 0xc3e72 vbe.c:168
    out DX, ax                                ; ef                          ; 0xc3e74
    in AL, DX                                 ; ec                          ; 0xc3e75 vbe.c:169
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3e76
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e78 vbe.c:170
    pop bx                                    ; 5b                          ; 0xc3e7b
    pop bp                                    ; 5d                          ; 0xc3e7c
    retn                                      ; c3                          ; 0xc3e7d
  ; disGetNextSymbol 0xc3e7e LB 0x6b4 -> off=0x0 cb=0000000000000014 uValue=00000000000c3e7e 'dispi_get_id'
dispi_get_id:                                ; 0xc3e7e LB 0x14
    push bp                                   ; 55                          ; 0xc3e7e vbe.c:173
    mov bp, sp                                ; 89 e5                       ; 0xc3e7f
    push dx                                   ; 52                          ; 0xc3e81
    xor ax, ax                                ; 31 c0                       ; 0xc3e82 vbe.c:175
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e84
    out DX, ax                                ; ef                          ; 0xc3e87
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e88 vbe.c:176
    in ax, DX                                 ; ed                          ; 0xc3e8b
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e8c vbe.c:177
    pop dx                                    ; 5a                          ; 0xc3e8f
    pop bp                                    ; 5d                          ; 0xc3e90
    retn                                      ; c3                          ; 0xc3e91
  ; disGetNextSymbol 0xc3e92 LB 0x6a0 -> off=0x0 cb=000000000000001a uValue=00000000000c3e92 'dispi_set_id'
dispi_set_id:                                ; 0xc3e92 LB 0x1a
    push bp                                   ; 55                          ; 0xc3e92 vbe.c:179
    mov bp, sp                                ; 89 e5                       ; 0xc3e93
    push bx                                   ; 53                          ; 0xc3e95
    push dx                                   ; 52                          ; 0xc3e96
    mov bx, ax                                ; 89 c3                       ; 0xc3e97
    xor ax, ax                                ; 31 c0                       ; 0xc3e99 vbe.c:181
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e9b
    out DX, ax                                ; ef                          ; 0xc3e9e
    mov ax, bx                                ; 89 d8                       ; 0xc3e9f vbe.c:182
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3ea1
    out DX, ax                                ; ef                          ; 0xc3ea4
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3ea5 vbe.c:183
    pop dx                                    ; 5a                          ; 0xc3ea8
    pop bx                                    ; 5b                          ; 0xc3ea9
    pop bp                                    ; 5d                          ; 0xc3eaa
    retn                                      ; c3                          ; 0xc3eab
  ; disGetNextSymbol 0xc3eac LB 0x686 -> off=0x0 cb=000000000000002a uValue=00000000000c3eac 'vbe_init'
vbe_init:                                    ; 0xc3eac LB 0x2a
    push bp                                   ; 55                          ; 0xc3eac vbe.c:188
    mov bp, sp                                ; 89 e5                       ; 0xc3ead
    push bx                                   ; 53                          ; 0xc3eaf
    mov ax, 0b0c0h                            ; b8 c0 b0                    ; 0xc3eb0 vbe.c:190
    call 03e92h                               ; e8 dc ff                    ; 0xc3eb3
    call 03e7eh                               ; e8 c5 ff                    ; 0xc3eb6 vbe.c:191
    cmp ax, 0b0c0h                            ; 3d c0 b0                    ; 0xc3eb9
    jne short 03ed0h                          ; 75 12                       ; 0xc3ebc
    mov bx, 000b9h                            ; bb b9 00                    ; 0xc3ebe vbe.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3ec1
    mov es, ax                                ; 8e c0                       ; 0xc3ec4
    mov byte [es:bx], 001h                    ; 26 c6 07 01                 ; 0xc3ec6
    mov ax, 0b0c4h                            ; b8 c4 b0                    ; 0xc3eca vbe.c:194
    call 03e92h                               ; e8 c2 ff                    ; 0xc3ecd
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3ed0 vbe.c:199
    pop bx                                    ; 5b                          ; 0xc3ed3
    pop bp                                    ; 5d                          ; 0xc3ed4
    retn                                      ; c3                          ; 0xc3ed5
  ; disGetNextSymbol 0xc3ed6 LB 0x65c -> off=0x0 cb=000000000000006c uValue=00000000000c3ed6 'mode_info_find_mode'
mode_info_find_mode:                         ; 0xc3ed6 LB 0x6c
    push bp                                   ; 55                          ; 0xc3ed6 vbe.c:202
    mov bp, sp                                ; 89 e5                       ; 0xc3ed7
    push bx                                   ; 53                          ; 0xc3ed9
    push cx                                   ; 51                          ; 0xc3eda
    push si                                   ; 56                          ; 0xc3edb
    push di                                   ; 57                          ; 0xc3edc
    mov di, ax                                ; 89 c7                       ; 0xc3edd
    mov si, dx                                ; 89 d6                       ; 0xc3edf
    xor dx, dx                                ; 31 d2                       ; 0xc3ee1 vbe.c:208
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3ee3
    call 03e58h                               ; e8 6f ff                    ; 0xc3ee6
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3ee9 vbe.c:209
    jne short 03f37h                          ; 75 49                       ; 0xc3eec
    test si, si                               ; 85 f6                       ; 0xc3eee vbe.c:213
    je short 03f05h                           ; 74 13                       ; 0xc3ef0
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc3ef2 vbe.c:220
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3ef5
    call 00570h                               ; e8 75 c6                    ; 0xc3ef8
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3efb vbe.c:221
    call 00577h                               ; e8 76 c6                    ; 0xc3efe
    test ax, ax                               ; 85 c0                       ; 0xc3f01 vbe.c:222
    je short 03f39h                           ; 74 34                       ; 0xc3f03
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc3f05 vbe.c:226
    mov dx, bx                                ; 89 da                       ; 0xc3f08 vbe.c:232
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f0a
    call 03e58h                               ; e8 48 ff                    ; 0xc3f0d
    mov cx, ax                                ; 89 c1                       ; 0xc3f10
    cmp cx, strict byte 0ffffh                ; 83 f9 ff                    ; 0xc3f12 vbe.c:233
    je short 03f37h                           ; 74 20                       ; 0xc3f15
    lea dx, [bx+002h]                         ; 8d 57 02                    ; 0xc3f17 vbe.c:235
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f1a
    call 03e58h                               ; e8 38 ff                    ; 0xc3f1d
    lea dx, [bx+044h]                         ; 8d 57 44                    ; 0xc3f20
    cmp cx, di                                ; 39 f9                       ; 0xc3f23 vbe.c:237
    jne short 03f33h                          ; 75 0c                       ; 0xc3f25
    test si, si                               ; 85 f6                       ; 0xc3f27 vbe.c:239
    jne short 03f2fh                          ; 75 04                       ; 0xc3f29
    mov ax, bx                                ; 89 d8                       ; 0xc3f2b vbe.c:240
    jmp short 03f39h                          ; eb 0a                       ; 0xc3f2d
    test AL, strict byte 080h                 ; a8 80                       ; 0xc3f2f vbe.c:241
    jne short 03f2bh                          ; 75 f8                       ; 0xc3f31
    mov bx, dx                                ; 89 d3                       ; 0xc3f33 vbe.c:244
    jmp short 03f0ah                          ; eb d3                       ; 0xc3f35 vbe.c:249
    xor ax, ax                                ; 31 c0                       ; 0xc3f37 vbe.c:252
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc3f39 vbe.c:253
    pop di                                    ; 5f                          ; 0xc3f3c
    pop si                                    ; 5e                          ; 0xc3f3d
    pop cx                                    ; 59                          ; 0xc3f3e
    pop bx                                    ; 5b                          ; 0xc3f3f
    pop bp                                    ; 5d                          ; 0xc3f40
    retn                                      ; c3                          ; 0xc3f41
  ; disGetNextSymbol 0xc3f42 LB 0x5f0 -> off=0x0 cb=000000000000012b uValue=00000000000c3f42 'vbe_biosfn_return_controller_information'
vbe_biosfn_return_controller_information: ; 0xc3f42 LB 0x12b
    push bp                                   ; 55                          ; 0xc3f42 vbe.c:284
    mov bp, sp                                ; 89 e5                       ; 0xc3f43
    push cx                                   ; 51                          ; 0xc3f45
    push si                                   ; 56                          ; 0xc3f46
    push di                                   ; 57                          ; 0xc3f47
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc3f48
    mov si, ax                                ; 89 c6                       ; 0xc3f4b
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3f4d
    mov di, bx                                ; 89 df                       ; 0xc3f50
    mov word [bp-00ch], strict word 00022h    ; c7 46 f4 22 00              ; 0xc3f52 vbe.c:289
    call 005b7h                               ; e8 5d c6                    ; 0xc3f57 vbe.c:292
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc3f5a
    mov bx, di                                ; 89 fb                       ; 0xc3f5d vbe.c:295
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3f5f
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3f62
    xor dx, dx                                ; 31 d2                       ; 0xc3f65 vbe.c:298
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f67
    call 03e58h                               ; e8 eb fe                    ; 0xc3f6a
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3f6d vbe.c:299
    je short 03f7ch                           ; 74 0a                       ; 0xc3f70
    push SS                                   ; 16                          ; 0xc3f72 vbe.c:301
    pop ES                                    ; 07                          ; 0xc3f73
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc3f74
    jmp near 04065h                           ; e9 e9 00                    ; 0xc3f79 vbe.c:305
    mov cx, strict word 00004h                ; b9 04 00                    ; 0xc3f7c vbe.c:307
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3f7f vbe.c:314
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3f84 vbe.c:322
    cmp word [es:bx+002h], 03245h             ; 26 81 7f 02 45 32           ; 0xc3f87
    jne short 03f96h                          ; 75 07                       ; 0xc3f8d
    cmp word [es:bx], 04256h                  ; 26 81 3f 56 42              ; 0xc3f8f
    je short 03fa5h                           ; 74 0f                       ; 0xc3f94
    cmp word [es:bx+002h], 04153h             ; 26 81 7f 02 53 41           ; 0xc3f96
    jne short 03faah                          ; 75 0c                       ; 0xc3f9c
    cmp word [es:bx], 04556h                  ; 26 81 3f 56 45              ; 0xc3f9e
    jne short 03faah                          ; 75 05                       ; 0xc3fa3
    mov word [bp-00eh], strict word 00001h    ; c7 46 f2 01 00              ; 0xc3fa5 vbe.c:324
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3faa vbe.c:332
    mov word [es:bx], 04556h                  ; 26 c7 07 56 45              ; 0xc3fad
    mov word [es:bx+002h], 04153h             ; 26 c7 47 02 53 41           ; 0xc3fb2 vbe.c:334
    mov word [es:bx+004h], 00200h             ; 26 c7 47 04 00 02           ; 0xc3fb8 vbe.c:338
    mov word [es:bx+006h], 07e04h             ; 26 c7 47 06 04 7e           ; 0xc3fbe vbe.c:341
    mov [es:bx+008h], ds                      ; 26 8c 5f 08                 ; 0xc3fc4
    mov word [es:bx+00ah], strict word 00001h ; 26 c7 47 0a 01 00           ; 0xc3fc8 vbe.c:344
    mov word [es:bx+00ch], strict word 00000h ; 26 c7 47 0c 00 00           ; 0xc3fce vbe.c:346
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3fd4 vbe.c:350
    mov word [es:bx+010h], ax                 ; 26 89 47 10                 ; 0xc3fd7
    lea ax, [di+022h]                         ; 8d 45 22                    ; 0xc3fdb vbe.c:351
    mov word [es:bx+00eh], ax                 ; 26 89 47 0e                 ; 0xc3fde
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc3fe2 vbe.c:354
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3fe5
    call 03e58h                               ; e8 6d fe                    ; 0xc3fe8
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3feb
    mov word [es:bx+012h], ax                 ; 26 89 47 12                 ; 0xc3fee
    cmp word [bp-00eh], strict byte 00000h    ; 83 7e f2 00                 ; 0xc3ff2 vbe.c:356
    je short 0401ch                           ; 74 24                       ; 0xc3ff6
    mov word [es:bx+014h], strict word 00003h ; 26 c7 47 14 03 00           ; 0xc3ff8 vbe.c:359
    mov word [es:bx+016h], 07e19h             ; 26 c7 47 16 19 7e           ; 0xc3ffe vbe.c:360
    mov [es:bx+018h], ds                      ; 26 8c 5f 18                 ; 0xc4004
    mov word [es:bx+01ah], 07e36h             ; 26 c7 47 1a 36 7e           ; 0xc4008 vbe.c:361
    mov [es:bx+01ch], ds                      ; 26 8c 5f 1c                 ; 0xc400e
    mov word [es:bx+01eh], 07e54h             ; 26 c7 47 1e 54 7e           ; 0xc4012 vbe.c:362
    mov [es:bx+020h], ds                      ; 26 8c 5f 20                 ; 0xc4018
    mov dx, cx                                ; 89 ca                       ; 0xc401c vbe.c:369
    add dx, strict byte 0001bh                ; 83 c2 1b                    ; 0xc401e
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4021
    call 03e6ah                               ; e8 43 fe                    ; 0xc4024
    xor ah, ah                                ; 30 e4                       ; 0xc4027 vbe.c:370
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc4029
    jnbe short 04045h                         ; 77 17                       ; 0xc402c
    mov dx, cx                                ; 89 ca                       ; 0xc402e vbe.c:372
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4030
    call 03e58h                               ; e8 22 fe                    ; 0xc4033
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc4036 vbe.c:376
    add bx, di                                ; 01 fb                       ; 0xc4039
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc403b vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc403e
    add word [bp-00ch], strict byte 00002h    ; 83 46 f4 02                 ; 0xc4041 vbe.c:378
    add cx, strict byte 00044h                ; 83 c1 44                    ; 0xc4045 vbe.c:380
    mov dx, cx                                ; 89 ca                       ; 0xc4048 vbe.c:381
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc404a
    call 03e58h                               ; e8 08 fe                    ; 0xc404d
    cmp ax, strict word 0ffffh                ; 3d ff ff                    ; 0xc4050 vbe.c:382
    jne short 0401ch                          ; 75 c7                       ; 0xc4053
    add di, word [bp-00ch]                    ; 03 7e f4                    ; 0xc4055 vbe.c:385
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc4058 vbe.c:62
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc405b
    push SS                                   ; 16                          ; 0xc405e vbe.c:386
    pop ES                                    ; 07                          ; 0xc405f
    mov word [es:si], strict word 0004fh      ; 26 c7 04 4f 00              ; 0xc4060
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc4065 vbe.c:387
    pop di                                    ; 5f                          ; 0xc4068
    pop si                                    ; 5e                          ; 0xc4069
    pop cx                                    ; 59                          ; 0xc406a
    pop bp                                    ; 5d                          ; 0xc406b
    retn                                      ; c3                          ; 0xc406c
  ; disGetNextSymbol 0xc406d LB 0x4c5 -> off=0x0 cb=000000000000009f uValue=00000000000c406d 'vbe_biosfn_return_mode_information'
vbe_biosfn_return_mode_information:          ; 0xc406d LB 0x9f
    push bp                                   ; 55                          ; 0xc406d vbe.c:399
    mov bp, sp                                ; 89 e5                       ; 0xc406e
    push si                                   ; 56                          ; 0xc4070
    push di                                   ; 57                          ; 0xc4071
    push ax                                   ; 50                          ; 0xc4072
    push ax                                   ; 50                          ; 0xc4073
    mov ax, dx                                ; 89 d0                       ; 0xc4074
    mov si, bx                                ; 89 de                       ; 0xc4076
    mov bx, cx                                ; 89 cb                       ; 0xc4078
    test dh, 040h                             ; f6 c6 40                    ; 0xc407a vbe.c:410
    je short 04084h                           ; 74 05                       ; 0xc407d
    mov dx, strict word 00001h                ; ba 01 00                    ; 0xc407f
    jmp short 04086h                          ; eb 02                       ; 0xc4082
    xor dx, dx                                ; 31 d2                       ; 0xc4084
    and ah, 001h                              ; 80 e4 01                    ; 0xc4086 vbe.c:411
    call 03ed6h                               ; e8 4a fe                    ; 0xc4089 vbe.c:413
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc408c
    test ax, ax                               ; 85 c0                       ; 0xc408f vbe.c:415
    je short 040fah                           ; 74 67                       ; 0xc4091
    mov cx, 00100h                            ; b9 00 01                    ; 0xc4093 vbe.c:420
    xor ax, ax                                ; 31 c0                       ; 0xc4096
    mov di, bx                                ; 89 df                       ; 0xc4098
    mov es, si                                ; 8e c6                       ; 0xc409a
    jcxz 040a0h                               ; e3 02                       ; 0xc409c
    rep stosb                                 ; f3 aa                       ; 0xc409e
    xor cx, cx                                ; 31 c9                       ; 0xc40a0 vbe.c:421
    jmp short 040a9h                          ; eb 05                       ; 0xc40a2
    cmp cx, strict byte 00042h                ; 83 f9 42                    ; 0xc40a4
    jnc short 040c2h                          ; 73 19                       ; 0xc40a7
    mov dx, word [bp-006h]                    ; 8b 56 fa                    ; 0xc40a9 vbe.c:424
    inc dx                                    ; 42                          ; 0xc40ac
    inc dx                                    ; 42                          ; 0xc40ad
    add dx, cx                                ; 01 ca                       ; 0xc40ae
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc40b0
    call 03e6ah                               ; e8 b4 fd                    ; 0xc40b3
    mov di, bx                                ; 89 df                       ; 0xc40b6 vbe.c:425
    add di, cx                                ; 01 cf                       ; 0xc40b8
    mov es, si                                ; 8e c6                       ; 0xc40ba vbe.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc40bc
    inc cx                                    ; 41                          ; 0xc40bf vbe.c:426
    jmp short 040a4h                          ; eb e2                       ; 0xc40c0
    lea di, [bx+002h]                         ; 8d 7f 02                    ; 0xc40c2 vbe.c:427
    mov es, si                                ; 8e c6                       ; 0xc40c5 vbe.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc40c7
    test AL, strict byte 001h                 ; a8 01                       ; 0xc40ca vbe.c:428
    je short 040deh                           ; 74 10                       ; 0xc40cc
    lea di, [bx+00ch]                         ; 8d 7f 0c                    ; 0xc40ce vbe.c:429
    mov word [es:di], 00629h                  ; 26 c7 05 29 06              ; 0xc40d1 vbe.c:62
    lea di, [bx+00eh]                         ; 8d 7f 0e                    ; 0xc40d6 vbe.c:431
    mov word [es:di], 0c000h                  ; 26 c7 05 00 c0              ; 0xc40d9 vbe.c:62
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc40de vbe.c:434
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc40e1
    call 00570h                               ; e8 89 c4                    ; 0xc40e4
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc40e7 vbe.c:435
    call 00577h                               ; e8 8a c4                    ; 0xc40ea
    add bx, strict byte 0002ah                ; 83 c3 2a                    ; 0xc40ed
    mov es, si                                ; 8e c6                       ; 0xc40f0 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc40f2
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc40f5 vbe.c:437
    jmp short 040fdh                          ; eb 03                       ; 0xc40f8 vbe.c:438
    mov ax, 00100h                            ; b8 00 01                    ; 0xc40fa vbe.c:442
    push SS                                   ; 16                          ; 0xc40fd vbe.c:445
    pop ES                                    ; 07                          ; 0xc40fe
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc40ff
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4102
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4105 vbe.c:446
    pop di                                    ; 5f                          ; 0xc4108
    pop si                                    ; 5e                          ; 0xc4109
    pop bp                                    ; 5d                          ; 0xc410a
    retn                                      ; c3                          ; 0xc410b
  ; disGetNextSymbol 0xc410c LB 0x426 -> off=0x0 cb=00000000000000e7 uValue=00000000000c410c 'vbe_biosfn_set_mode'
vbe_biosfn_set_mode:                         ; 0xc410c LB 0xe7
    push bp                                   ; 55                          ; 0xc410c vbe.c:458
    mov bp, sp                                ; 89 e5                       ; 0xc410d
    push si                                   ; 56                          ; 0xc410f
    push di                                   ; 57                          ; 0xc4110
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc4111
    mov si, ax                                ; 89 c6                       ; 0xc4114
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc4116
    test byte [bp-009h], 040h                 ; f6 46 f7 40                 ; 0xc4119 vbe.c:466
    je short 04124h                           ; 74 05                       ; 0xc411d
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc411f
    jmp short 04126h                          ; eb 02                       ; 0xc4122
    xor ax, ax                                ; 31 c0                       ; 0xc4124
    mov dx, ax                                ; 89 c2                       ; 0xc4126
    test ax, ax                               ; 85 c0                       ; 0xc4128 vbe.c:467
    je short 0412fh                           ; 74 03                       ; 0xc412a
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc412c
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc412f
    test byte [bp-009h], 080h                 ; f6 46 f7 80                 ; 0xc4132 vbe.c:468
    je short 0413dh                           ; 74 05                       ; 0xc4136
    mov ax, 00080h                            ; b8 80 00                    ; 0xc4138
    jmp short 0413fh                          ; eb 02                       ; 0xc413b
    xor ax, ax                                ; 31 c0                       ; 0xc413d
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc413f
    and byte [bp-009h], 001h                  ; 80 66 f7 01                 ; 0xc4142 vbe.c:470
    cmp word [bp-00ah], 00100h                ; 81 7e f6 00 01              ; 0xc4146 vbe.c:473
    jnc short 04160h                          ; 73 13                       ; 0xc414b
    xor ax, ax                                ; 31 c0                       ; 0xc414d vbe.c:477
    call 005ddh                               ; e8 8b c4                    ; 0xc414f
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc4152 vbe.c:481
    xor ah, ah                                ; 30 e4                       ; 0xc4155
    call 0143fh                               ; e8 e5 d2                    ; 0xc4157
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc415a vbe.c:482
    jmp near 041e7h                           ; e9 87 00                    ; 0xc415d vbe.c:483
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc4160 vbe.c:486
    call 03ed6h                               ; e8 70 fd                    ; 0xc4163
    mov bx, ax                                ; 89 c3                       ; 0xc4166
    test ax, ax                               ; 85 c0                       ; 0xc4168 vbe.c:488
    je short 041e4h                           ; 74 78                       ; 0xc416a
    lea dx, [bx+014h]                         ; 8d 57 14                    ; 0xc416c vbe.c:493
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc416f
    call 03e58h                               ; e8 e3 fc                    ; 0xc4172
    mov cx, ax                                ; 89 c1                       ; 0xc4175
    lea dx, [bx+016h]                         ; 8d 57 16                    ; 0xc4177 vbe.c:494
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc417a
    call 03e58h                               ; e8 d8 fc                    ; 0xc417d
    mov di, ax                                ; 89 c7                       ; 0xc4180
    lea dx, [bx+01bh]                         ; 8d 57 1b                    ; 0xc4182 vbe.c:495
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4185
    call 03e6ah                               ; e8 df fc                    ; 0xc4188
    mov bl, al                                ; 88 c3                       ; 0xc418b
    mov dl, al                                ; 88 c2                       ; 0xc418d
    xor ax, ax                                ; 31 c0                       ; 0xc418f vbe.c:503
    call 005ddh                               ; e8 49 c4                    ; 0xc4191
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc4194 vbe.c:505
    jne short 0419fh                          ; 75 06                       ; 0xc4197
    mov ax, strict word 0006ah                ; b8 6a 00                    ; 0xc4199 vbe.c:507
    call 0143fh                               ; e8 a0 d2                    ; 0xc419c
    mov al, dl                                ; 88 d0                       ; 0xc419f vbe.c:510
    xor ah, ah                                ; 30 e4                       ; 0xc41a1
    call 03dcfh                               ; e8 29 fc                    ; 0xc41a3
    mov ax, cx                                ; 89 c8                       ; 0xc41a6 vbe.c:511
    call 03d78h                               ; e8 cd fb                    ; 0xc41a8
    mov ax, di                                ; 89 f8                       ; 0xc41ab vbe.c:512
    call 03d97h                               ; e8 e7 fb                    ; 0xc41ad
    xor ax, ax                                ; 31 c0                       ; 0xc41b0 vbe.c:513
    call 00603h                               ; e8 4e c4                    ; 0xc41b2
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc41b5 vbe.c:514
    or dl, 001h                               ; 80 ca 01                    ; 0xc41b8
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc41bb
    xor ah, ah                                ; 30 e4                       ; 0xc41be
    or al, dl                                 ; 08 d0                       ; 0xc41c0
    call 005ddh                               ; e8 18 c4                    ; 0xc41c2
    call 006d2h                               ; e8 0a c5                    ; 0xc41c5 vbe.c:515
    mov bx, 000bah                            ; bb ba 00                    ; 0xc41c8 vbe.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc41cb
    mov es, ax                                ; 8e c0                       ; 0xc41ce
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc41d0
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc41d3
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc41d6 vbe.c:518
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc41d9
    mov bx, 00087h                            ; bb 87 00                    ; 0xc41db vbe.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc41de
    jmp near 0415ah                           ; e9 76 ff                    ; 0xc41e1
    mov ax, 00100h                            ; b8 00 01                    ; 0xc41e4 vbe.c:527
    push SS                                   ; 16                          ; 0xc41e7 vbe.c:531
    pop ES                                    ; 07                          ; 0xc41e8
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc41e9
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc41ec vbe.c:532
    pop di                                    ; 5f                          ; 0xc41ef
    pop si                                    ; 5e                          ; 0xc41f0
    pop bp                                    ; 5d                          ; 0xc41f1
    retn                                      ; c3                          ; 0xc41f2
  ; disGetNextSymbol 0xc41f3 LB 0x33f -> off=0x0 cb=0000000000000008 uValue=00000000000c41f3 'vbe_biosfn_read_video_state_size'
vbe_biosfn_read_video_state_size:            ; 0xc41f3 LB 0x8
    push bp                                   ; 55                          ; 0xc41f3 vbe.c:534
    mov bp, sp                                ; 89 e5                       ; 0xc41f4
    mov ax, strict word 00012h                ; b8 12 00                    ; 0xc41f6 vbe.c:537
    pop bp                                    ; 5d                          ; 0xc41f9
    retn                                      ; c3                          ; 0xc41fa
  ; disGetNextSymbol 0xc41fb LB 0x337 -> off=0x0 cb=000000000000004b uValue=00000000000c41fb 'vbe_biosfn_save_video_state'
vbe_biosfn_save_video_state:                 ; 0xc41fb LB 0x4b
    push bp                                   ; 55                          ; 0xc41fb vbe.c:539
    mov bp, sp                                ; 89 e5                       ; 0xc41fc
    push bx                                   ; 53                          ; 0xc41fe
    push cx                                   ; 51                          ; 0xc41ff
    push si                                   ; 56                          ; 0xc4200
    mov si, ax                                ; 89 c6                       ; 0xc4201
    mov bx, dx                                ; 89 d3                       ; 0xc4203
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc4205 vbe.c:543
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4208
    out DX, ax                                ; ef                          ; 0xc420b
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc420c vbe.c:544
    in ax, DX                                 ; ed                          ; 0xc420f
    mov es, si                                ; 8e c6                       ; 0xc4210 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4212
    inc bx                                    ; 43                          ; 0xc4215 vbe.c:546
    inc bx                                    ; 43                          ; 0xc4216
    test AL, strict byte 001h                 ; a8 01                       ; 0xc4217 vbe.c:547
    je short 0423eh                           ; 74 23                       ; 0xc4219
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc421b vbe.c:549
    jmp short 04225h                          ; eb 05                       ; 0xc421e
    cmp cx, strict byte 00009h                ; 83 f9 09                    ; 0xc4220
    jnbe short 0423eh                         ; 77 19                       ; 0xc4223
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc4225 vbe.c:550
    je short 0423bh                           ; 74 11                       ; 0xc4228
    mov ax, cx                                ; 89 c8                       ; 0xc422a vbe.c:551
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc422c
    out DX, ax                                ; ef                          ; 0xc422f
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4230 vbe.c:552
    in ax, DX                                 ; ed                          ; 0xc4233
    mov es, si                                ; 8e c6                       ; 0xc4234 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4236
    inc bx                                    ; 43                          ; 0xc4239 vbe.c:553
    inc bx                                    ; 43                          ; 0xc423a
    inc cx                                    ; 41                          ; 0xc423b vbe.c:555
    jmp short 04220h                          ; eb e2                       ; 0xc423c
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc423e vbe.c:556
    pop si                                    ; 5e                          ; 0xc4241
    pop cx                                    ; 59                          ; 0xc4242
    pop bx                                    ; 5b                          ; 0xc4243
    pop bp                                    ; 5d                          ; 0xc4244
    retn                                      ; c3                          ; 0xc4245
  ; disGetNextSymbol 0xc4246 LB 0x2ec -> off=0x0 cb=000000000000008f uValue=00000000000c4246 'vbe_biosfn_restore_video_state'
vbe_biosfn_restore_video_state:              ; 0xc4246 LB 0x8f
    push bp                                   ; 55                          ; 0xc4246 vbe.c:559
    mov bp, sp                                ; 89 e5                       ; 0xc4247
    push bx                                   ; 53                          ; 0xc4249
    push cx                                   ; 51                          ; 0xc424a
    push si                                   ; 56                          ; 0xc424b
    push ax                                   ; 50                          ; 0xc424c
    mov cx, ax                                ; 89 c1                       ; 0xc424d
    mov bx, dx                                ; 89 d3                       ; 0xc424f
    mov es, ax                                ; 8e c0                       ; 0xc4251 vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4253
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc4256
    inc bx                                    ; 43                          ; 0xc4259 vbe.c:564
    inc bx                                    ; 43                          ; 0xc425a
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc425b vbe.c:566
    jne short 04271h                          ; 75 10                       ; 0xc425f
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc4261 vbe.c:567
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4264
    out DX, ax                                ; ef                          ; 0xc4267
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc4268 vbe.c:568
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc426b
    out DX, ax                                ; ef                          ; 0xc426e
    jmp short 042cdh                          ; eb 5c                       ; 0xc426f vbe.c:569
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc4271 vbe.c:570
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4274
    out DX, ax                                ; ef                          ; 0xc4277
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4278 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc427b vbe.c:58
    out DX, ax                                ; ef                          ; 0xc427e
    inc bx                                    ; 43                          ; 0xc427f vbe.c:572
    inc bx                                    ; 43                          ; 0xc4280
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc4281
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4284
    out DX, ax                                ; ef                          ; 0xc4287
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4288 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc428b vbe.c:58
    out DX, ax                                ; ef                          ; 0xc428e
    inc bx                                    ; 43                          ; 0xc428f vbe.c:575
    inc bx                                    ; 43                          ; 0xc4290
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc4291
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4294
    out DX, ax                                ; ef                          ; 0xc4297
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4298 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc429b vbe.c:58
    out DX, ax                                ; ef                          ; 0xc429e
    inc bx                                    ; 43                          ; 0xc429f vbe.c:578
    inc bx                                    ; 43                          ; 0xc42a0
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc42a1
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42a4
    out DX, ax                                ; ef                          ; 0xc42a7
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc42a8 vbe.c:580
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42ab
    out DX, ax                                ; ef                          ; 0xc42ae
    mov si, strict word 00005h                ; be 05 00                    ; 0xc42af vbe.c:582
    jmp short 042b9h                          ; eb 05                       ; 0xc42b2
    cmp si, strict byte 00009h                ; 83 fe 09                    ; 0xc42b4
    jnbe short 042cdh                         ; 77 14                       ; 0xc42b7
    mov ax, si                                ; 89 f0                       ; 0xc42b9 vbe.c:583
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42bb
    out DX, ax                                ; ef                          ; 0xc42be
    mov es, cx                                ; 8e c1                       ; 0xc42bf vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc42c1
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42c4 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc42c7
    inc bx                                    ; 43                          ; 0xc42c8 vbe.c:585
    inc bx                                    ; 43                          ; 0xc42c9
    inc si                                    ; 46                          ; 0xc42ca vbe.c:586
    jmp short 042b4h                          ; eb e7                       ; 0xc42cb
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc42cd vbe.c:588
    pop si                                    ; 5e                          ; 0xc42d0
    pop cx                                    ; 59                          ; 0xc42d1
    pop bx                                    ; 5b                          ; 0xc42d2
    pop bp                                    ; 5d                          ; 0xc42d3
    retn                                      ; c3                          ; 0xc42d4
  ; disGetNextSymbol 0xc42d5 LB 0x25d -> off=0x0 cb=000000000000008c uValue=00000000000c42d5 'vbe_biosfn_save_restore_state'
vbe_biosfn_save_restore_state:               ; 0xc42d5 LB 0x8c
    push bp                                   ; 55                          ; 0xc42d5 vbe.c:604
    mov bp, sp                                ; 89 e5                       ; 0xc42d6
    push si                                   ; 56                          ; 0xc42d8
    push di                                   ; 57                          ; 0xc42d9
    push ax                                   ; 50                          ; 0xc42da
    mov si, ax                                ; 89 c6                       ; 0xc42db
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc42dd
    mov ax, bx                                ; 89 d8                       ; 0xc42e0
    mov bx, word [bp+004h]                    ; 8b 5e 04                    ; 0xc42e2
    mov di, strict word 0004fh                ; bf 4f 00                    ; 0xc42e5 vbe.c:609
    xor ah, ah                                ; 30 e4                       ; 0xc42e8 vbe.c:610
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc42ea
    je short 04334h                           ; 74 45                       ; 0xc42ed
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc42ef
    je short 04318h                           ; 74 24                       ; 0xc42f2
    test ax, ax                               ; 85 c0                       ; 0xc42f4
    jne short 04350h                          ; 75 58                       ; 0xc42f6
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc42f8 vbe.c:612
    call 0324ch                               ; e8 4e ef                    ; 0xc42fb
    mov cx, ax                                ; 89 c1                       ; 0xc42fe
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc4300 vbe.c:616
    je short 0430bh                           ; 74 05                       ; 0xc4304
    call 041f3h                               ; e8 ea fe                    ; 0xc4306 vbe.c:617
    add ax, cx                                ; 01 c8                       ; 0xc4309
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc430b vbe.c:618
    shr ax, 006h                              ; c1 e8 06                    ; 0xc430e
    push SS                                   ; 16                          ; 0xc4311
    pop ES                                    ; 07                          ; 0xc4312
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4313
    jmp short 04353h                          ; eb 3b                       ; 0xc4316 vbe.c:619
    push SS                                   ; 16                          ; 0xc4318 vbe.c:621
    pop ES                                    ; 07                          ; 0xc4319
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc431a
    mov dx, cx                                ; 89 ca                       ; 0xc431d vbe.c:622
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc431f
    call 03287h                               ; e8 62 ef                    ; 0xc4322
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc4325 vbe.c:626
    je short 04353h                           ; 74 28                       ; 0xc4329
    mov dx, ax                                ; 89 c2                       ; 0xc432b vbe.c:627
    mov ax, cx                                ; 89 c8                       ; 0xc432d
    call 041fbh                               ; e8 c9 fe                    ; 0xc432f
    jmp short 04353h                          ; eb 1f                       ; 0xc4332 vbe.c:628
    push SS                                   ; 16                          ; 0xc4334 vbe.c:630
    pop ES                                    ; 07                          ; 0xc4335
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc4336
    mov dx, cx                                ; 89 ca                       ; 0xc4339 vbe.c:631
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc433b
    call 0355fh                               ; e8 1e f2                    ; 0xc433e
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc4341 vbe.c:635
    je short 04353h                           ; 74 0c                       ; 0xc4345
    mov dx, ax                                ; 89 c2                       ; 0xc4347 vbe.c:636
    mov ax, cx                                ; 89 c8                       ; 0xc4349
    call 04246h                               ; e8 f8 fe                    ; 0xc434b
    jmp short 04353h                          ; eb 03                       ; 0xc434e vbe.c:637
    mov di, 00100h                            ; bf 00 01                    ; 0xc4350 vbe.c:640
    push SS                                   ; 16                          ; 0xc4353 vbe.c:643
    pop ES                                    ; 07                          ; 0xc4354
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc4355
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4358 vbe.c:644
    pop di                                    ; 5f                          ; 0xc435b
    pop si                                    ; 5e                          ; 0xc435c
    pop bp                                    ; 5d                          ; 0xc435d
    retn 00002h                               ; c2 02 00                    ; 0xc435e
  ; disGetNextSymbol 0xc4361 LB 0x1d1 -> off=0x0 cb=00000000000000df uValue=00000000000c4361 'vbe_biosfn_get_set_scanline_length'
vbe_biosfn_get_set_scanline_length:          ; 0xc4361 LB 0xdf
    push bp                                   ; 55                          ; 0xc4361 vbe.c:665
    mov bp, sp                                ; 89 e5                       ; 0xc4362
    push si                                   ; 56                          ; 0xc4364
    push di                                   ; 57                          ; 0xc4365
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc4366
    push ax                                   ; 50                          ; 0xc4369
    mov di, dx                                ; 89 d7                       ; 0xc436a
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc436c
    mov si, cx                                ; 89 ce                       ; 0xc436f
    call 03deeh                               ; e8 7a fa                    ; 0xc4371 vbe.c:674
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc4374 vbe.c:675
    jne short 0437dh                          ; 75 05                       ; 0xc4376
    mov bx, strict word 00010h                ; bb 10 00                    ; 0xc4378
    jmp short 04381h                          ; eb 04                       ; 0xc437b
    xor ah, ah                                ; 30 e4                       ; 0xc437d
    mov bx, ax                                ; 89 c3                       ; 0xc437f
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc4381
    call 03e26h                               ; e8 9f fa                    ; 0xc4384 vbe.c:676
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc4387
    mov word [bp-00ch], strict word 0004fh    ; c7 46 f4 4f 00              ; 0xc438a vbe.c:677
    push SS                                   ; 16                          ; 0xc438f vbe.c:678
    pop ES                                    ; 07                          ; 0xc4390
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc4391
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc4394
    mov cl, byte [es:di]                      ; 26 8a 0d                    ; 0xc4397 vbe.c:679
    cmp cl, 002h                              ; 80 f9 02                    ; 0xc439a vbe.c:683
    je short 043abh                           ; 74 0c                       ; 0xc439d
    cmp cl, 001h                              ; 80 f9 01                    ; 0xc439f
    je short 043d1h                           ; 74 2d                       ; 0xc43a2
    test cl, cl                               ; 84 c9                       ; 0xc43a4
    je short 043cch                           ; 74 24                       ; 0xc43a6
    jmp near 04429h                           ; e9 7e 00                    ; 0xc43a8
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc43ab vbe.c:685
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc43ae
    jne short 043b7h                          ; 75 05                       ; 0xc43b0
    sal bx, 003h                              ; c1 e3 03                    ; 0xc43b2 vbe.c:686
    jmp short 043cch                          ; eb 15                       ; 0xc43b5 vbe.c:687
    xor ah, ah                                ; 30 e4                       ; 0xc43b7 vbe.c:688
    cwd                                       ; 99                          ; 0xc43b9
    sal dx, 003h                              ; c1 e2 03                    ; 0xc43ba
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc43bd
    sar ax, 003h                              ; c1 f8 03                    ; 0xc43bf
    mov cx, ax                                ; 89 c1                       ; 0xc43c2
    mov ax, bx                                ; 89 d8                       ; 0xc43c4
    xor dx, dx                                ; 31 d2                       ; 0xc43c6
    div cx                                    ; f7 f1                       ; 0xc43c8
    mov bx, ax                                ; 89 c3                       ; 0xc43ca
    mov ax, bx                                ; 89 d8                       ; 0xc43cc vbe.c:691
    call 03e07h                               ; e8 36 fa                    ; 0xc43ce
    call 03e26h                               ; e8 52 fa                    ; 0xc43d1 vbe.c:694
    mov cx, ax                                ; 89 c1                       ; 0xc43d4
    push SS                                   ; 16                          ; 0xc43d6 vbe.c:695
    pop ES                                    ; 07                          ; 0xc43d7
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc43d8
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc43db
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc43de vbe.c:696
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc43e1
    jne short 043ech                          ; 75 07                       ; 0xc43e3
    mov bx, cx                                ; 89 cb                       ; 0xc43e5 vbe.c:697
    shr bx, 003h                              ; c1 eb 03                    ; 0xc43e7
    jmp short 043ffh                          ; eb 13                       ; 0xc43ea vbe.c:698
    xor ah, ah                                ; 30 e4                       ; 0xc43ec vbe.c:699
    cwd                                       ; 99                          ; 0xc43ee
    sal dx, 003h                              ; c1 e2 03                    ; 0xc43ef
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc43f2
    sar ax, 003h                              ; c1 f8 03                    ; 0xc43f4
    mov bx, ax                                ; 89 c3                       ; 0xc43f7
    mov ax, cx                                ; 89 c8                       ; 0xc43f9
    mul bx                                    ; f7 e3                       ; 0xc43fb
    mov bx, ax                                ; 89 c3                       ; 0xc43fd
    add bx, strict byte 00003h                ; 83 c3 03                    ; 0xc43ff vbe.c:700
    and bl, 0fch                              ; 80 e3 fc                    ; 0xc4402
    push SS                                   ; 16                          ; 0xc4405 vbe.c:701
    pop ES                                    ; 07                          ; 0xc4406
    mov word [es:di], bx                      ; 26 89 1d                    ; 0xc4407
    call 03e3fh                               ; e8 32 fa                    ; 0xc440a vbe.c:702
    push SS                                   ; 16                          ; 0xc440d
    pop ES                                    ; 07                          ; 0xc440e
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc440f
    call 03db6h                               ; e8 a1 f9                    ; 0xc4412 vbe.c:703
    push SS                                   ; 16                          ; 0xc4415
    pop ES                                    ; 07                          ; 0xc4416
    cmp ax, word [es:si]                      ; 26 3b 04                    ; 0xc4417
    jbe short 0442eh                          ; 76 12                       ; 0xc441a
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc441c vbe.c:704
    call 03e07h                               ; e8 e5 f9                    ; 0xc441f
    mov word [bp-00ch], 00200h                ; c7 46 f4 00 02              ; 0xc4422 vbe.c:705
    jmp short 0442eh                          ; eb 05                       ; 0xc4427 vbe.c:707
    mov word [bp-00ch], 00100h                ; c7 46 f4 00 01              ; 0xc4429 vbe.c:710
    push SS                                   ; 16                          ; 0xc442e vbe.c:713
    pop ES                                    ; 07                          ; 0xc442f
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc4430
    mov bx, word [bp-00eh]                    ; 8b 5e f2                    ; 0xc4433
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4436
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4439 vbe.c:714
    pop di                                    ; 5f                          ; 0xc443c
    pop si                                    ; 5e                          ; 0xc443d
    pop bp                                    ; 5d                          ; 0xc443e
    retn                                      ; c3                          ; 0xc443f
  ; disGetNextSymbol 0xc4440 LB 0xf2 -> off=0x0 cb=00000000000000f2 uValue=00000000000c4440 'private_biosfn_custom_mode'
private_biosfn_custom_mode:                  ; 0xc4440 LB 0xf2
    push bp                                   ; 55                          ; 0xc4440 vbe.c:740
    mov bp, sp                                ; 89 e5                       ; 0xc4441
    push si                                   ; 56                          ; 0xc4443
    push di                                   ; 57                          ; 0xc4444
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc4445
    mov di, ax                                ; 89 c7                       ; 0xc4448
    mov si, dx                                ; 89 d6                       ; 0xc444a
    mov dx, cx                                ; 89 ca                       ; 0xc444c
    mov word [bp-00ah], strict word 0004fh    ; c7 46 f6 4f 00              ; 0xc444e vbe.c:753
    push SS                                   ; 16                          ; 0xc4453 vbe.c:754
    pop ES                                    ; 07                          ; 0xc4454
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc4455
    test al, al                               ; 84 c0                       ; 0xc4458 vbe.c:755
    jne short 0447eh                          ; 75 22                       ; 0xc445a
    push SS                                   ; 16                          ; 0xc445c vbe.c:757
    pop ES                                    ; 07                          ; 0xc445d
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc445e
    mov bx, dx                                ; 89 d3                       ; 0xc4461 vbe.c:758
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc4463
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc4466 vbe.c:759
    shr ax, 008h                              ; c1 e8 08                    ; 0xc4469
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc446c
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc446f
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc4472 vbe.c:764
    je short 04486h                           ; 74 10                       ; 0xc4474
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc4476
    je short 04486h                           ; 74 0c                       ; 0xc4478
    cmp AL, strict byte 020h                  ; 3c 20                       ; 0xc447a
    je short 04486h                           ; 74 08                       ; 0xc447c
    mov word [bp-00ah], 00100h                ; c7 46 f6 00 01              ; 0xc447e vbe.c:765
    jmp near 04523h                           ; e9 9d 00                    ; 0xc4483 vbe.c:766
    push SS                                   ; 16                          ; 0xc4486 vbe.c:770
    pop ES                                    ; 07                          ; 0xc4487
    test byte [es:si+001h], 080h              ; 26 f6 44 01 80              ; 0xc4488
    je short 04494h                           ; 74 05                       ; 0xc448d
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc448f
    jmp short 04496h                          ; eb 02                       ; 0xc4492
    xor ax, ax                                ; 31 c0                       ; 0xc4494
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc4496
    cmp cx, 00280h                            ; 81 f9 80 02                 ; 0xc4499 vbe.c:773
    jnc short 044a4h                          ; 73 05                       ; 0xc449d
    mov cx, 00280h                            ; b9 80 02                    ; 0xc449f vbe.c:774
    jmp short 044adh                          ; eb 09                       ; 0xc44a2 vbe.c:775
    cmp cx, 00a00h                            ; 81 f9 00 0a                 ; 0xc44a4
    jbe short 044adh                          ; 76 03                       ; 0xc44a8
    mov cx, 00a00h                            ; b9 00 0a                    ; 0xc44aa vbe.c:776
    cmp bx, 001e0h                            ; 81 fb e0 01                 ; 0xc44ad vbe.c:777
    jnc short 044b8h                          ; 73 05                       ; 0xc44b1
    mov bx, 001e0h                            ; bb e0 01                    ; 0xc44b3 vbe.c:778
    jmp short 044c1h                          ; eb 09                       ; 0xc44b6 vbe.c:779
    cmp bx, 00780h                            ; 81 fb 80 07                 ; 0xc44b8
    jbe short 044c1h                          ; 76 03                       ; 0xc44bc
    mov bx, 00780h                            ; bb 80 07                    ; 0xc44be vbe.c:780
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc44c1 vbe.c:786
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc44c4
    call 03e58h                               ; e8 8e f9                    ; 0xc44c7
    mov si, ax                                ; 89 c6                       ; 0xc44ca
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc44cc vbe.c:789
    xor ah, ah                                ; 30 e4                       ; 0xc44cf
    cwd                                       ; 99                          ; 0xc44d1
    sal dx, 003h                              ; c1 e2 03                    ; 0xc44d2
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc44d5
    sar ax, 003h                              ; c1 f8 03                    ; 0xc44d7
    mov dx, ax                                ; 89 c2                       ; 0xc44da
    mov ax, cx                                ; 89 c8                       ; 0xc44dc
    mul dx                                    ; f7 e2                       ; 0xc44de
    add ax, strict word 00003h                ; 05 03 00                    ; 0xc44e0 vbe.c:790
    and AL, strict byte 0fch                  ; 24 fc                       ; 0xc44e3
    mov dx, bx                                ; 89 da                       ; 0xc44e5 vbe.c:792
    mul dx                                    ; f7 e2                       ; 0xc44e7
    cmp dx, si                                ; 39 f2                       ; 0xc44e9 vbe.c:794
    jnbe short 044f3h                         ; 77 06                       ; 0xc44eb
    jne short 044fah                          ; 75 0b                       ; 0xc44ed
    test ax, ax                               ; 85 c0                       ; 0xc44ef
    jbe short 044fah                          ; 76 07                       ; 0xc44f1
    mov word [bp-00ah], 00200h                ; c7 46 f6 00 02              ; 0xc44f3 vbe.c:796
    jmp short 04523h                          ; eb 29                       ; 0xc44f8 vbe.c:797
    xor ax, ax                                ; 31 c0                       ; 0xc44fa vbe.c:801
    call 005ddh                               ; e8 de c0                    ; 0xc44fc
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc44ff vbe.c:802
    xor ah, ah                                ; 30 e4                       ; 0xc4502
    call 03dcfh                               ; e8 c8 f8                    ; 0xc4504
    mov ax, cx                                ; 89 c8                       ; 0xc4507 vbe.c:803
    call 03d78h                               ; e8 6c f8                    ; 0xc4509
    mov ax, bx                                ; 89 d8                       ; 0xc450c vbe.c:804
    call 03d97h                               ; e8 86 f8                    ; 0xc450e
    xor ax, ax                                ; 31 c0                       ; 0xc4511 vbe.c:805
    call 00603h                               ; e8 ed c0                    ; 0xc4513
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc4516 vbe.c:806
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc4519
    xor ah, ah                                ; 30 e4                       ; 0xc451b
    call 005ddh                               ; e8 bd c0                    ; 0xc451d
    call 006d2h                               ; e8 af c1                    ; 0xc4520 vbe.c:807
    push SS                                   ; 16                          ; 0xc4523 vbe.c:815
    pop ES                                    ; 07                          ; 0xc4524
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc4525
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc4528
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc452b vbe.c:816
    pop di                                    ; 5f                          ; 0xc452e
    pop si                                    ; 5e                          ; 0xc452f
    pop bp                                    ; 5d                          ; 0xc4530
    retn                                      ; c3                          ; 0xc4531

  ; Padding 0x10e bytes at 0xc4532
  times 270 db 0

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

section _DATA progbits vstart=0x4780 align=1 ; size=0x374e class=DATA group=DGROUP
  ; disGetNextSymbol 0xc4780 LB 0x374e -> off=0x0 cb=0000000000000031 uValue=00000000000c0000 '_msg_vga_init'
_msg_vga_init:                               ; 0xc4780 LB 0x31
    db  'Oracle VirtualBox Version 7.2.0_BETA1 VGA BIOS', 00dh, 00ah, 000h
  ; disGetNextSymbol 0xc47b1 LB 0x371d -> off=0x0 cb=0000000000000080 uValue=00000000000c0031 'vga_modes'
vga_modes:                                   ; 0xc47b1 LB 0x80
    db  000h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h, 001h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h
    db  002h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h, 003h, 000h, 000h, 004h, 000h, 0b8h, 0ffh, 002h
    db  004h, 001h, 002h, 002h, 000h, 0b8h, 0ffh, 001h, 005h, 001h, 002h, 002h, 000h, 0b8h, 0ffh, 001h
    db  006h, 001h, 002h, 001h, 000h, 0b8h, 0ffh, 001h, 007h, 000h, 001h, 004h, 000h, 0b0h, 0ffh, 000h
    db  00dh, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 001h, 00eh, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 001h
    db  00fh, 001h, 003h, 001h, 000h, 0a0h, 0ffh, 000h, 010h, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 002h
    db  011h, 001h, 003h, 001h, 000h, 0a0h, 0ffh, 002h, 012h, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 002h
    db  013h, 001h, 005h, 008h, 000h, 0a0h, 0ffh, 003h, 06ah, 001h, 004h, 004h, 000h, 0a0h, 0ffh, 002h
  ; disGetNextSymbol 0xc4831 LB 0x369d -> off=0x0 cb=0000000000000010 uValue=00000000000c00b1 'line_to_vpti'
line_to_vpti:                                ; 0xc4831 LB 0x10
    db  017h, 017h, 018h, 018h, 004h, 005h, 006h, 007h, 00dh, 00eh, 011h, 012h, 01ah, 01bh, 01ch, 01dh
  ; disGetNextSymbol 0xc4841 LB 0x368d -> off=0x0 cb=0000000000000004 uValue=00000000000c00c1 'dac_regs'
dac_regs:                                    ; 0xc4841 LB 0x4
    dd  0ff3f3f3fh
  ; disGetNextSymbol 0xc4845 LB 0x3689 -> off=0x0 cb=0000000000000780 uValue=00000000000c00c5 'video_param_table'
video_param_table:                           ; 0xc4845 LB 0x780
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
  ; disGetNextSymbol 0xc4fc5 LB 0x2f09 -> off=0x0 cb=00000000000000c0 uValue=00000000000c0845 'palette0'
palette0:                                    ; 0xc4fc5 LB 0xc0
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
  ; disGetNextSymbol 0xc5085 LB 0x2e49 -> off=0x0 cb=00000000000000c0 uValue=00000000000c0905 'palette1'
palette1:                                    ; 0xc5085 LB 0xc0
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
  ; disGetNextSymbol 0xc5145 LB 0x2d89 -> off=0x0 cb=00000000000000c0 uValue=00000000000c09c5 'palette2'
palette2:                                    ; 0xc5145 LB 0xc0
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
  ; disGetNextSymbol 0xc5205 LB 0x2cc9 -> off=0x0 cb=0000000000000300 uValue=00000000000c0a85 'palette3'
palette3:                                    ; 0xc5205 LB 0x300
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
  ; disGetNextSymbol 0xc5505 LB 0x29c9 -> off=0x0 cb=0000000000000010 uValue=00000000000c0d85 'static_functionality'
static_functionality:                        ; 0xc5505 LB 0x10
    db  0ffh, 0e0h, 00fh, 000h, 000h, 000h, 000h, 007h, 002h, 008h, 0e7h, 00ch, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc5515 LB 0x29b9 -> off=0x0 cb=0000000000000024 uValue=00000000000c0d95 '_dcc_table'
_dcc_table:                                  ; 0xc5515 LB 0x24
    db  010h, 001h, 007h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc5539 LB 0x2995 -> off=0x0 cb=000000000000001a uValue=00000000000c0db9 '_secondary_save_area'
_secondary_save_area:                        ; 0xc5539 LB 0x1a
    db  01ah, 000h, 015h, 055h, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc5553 LB 0x297b -> off=0x0 cb=000000000000001c uValue=00000000000c0dd3 '_video_save_pointer_table'
_video_save_pointer_table:                   ; 0xc5553 LB 0x1c
    db  045h, 048h, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  039h, 055h, 000h, 0c0h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
  ; disGetNextSymbol 0xc556f LB 0x295f -> off=0x0 cb=0000000000000800 uValue=00000000000c0def 'vgafont8'
vgafont8:                                    ; 0xc556f LB 0x800
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
  ; disGetNextSymbol 0xc5d6f LB 0x215f -> off=0x0 cb=0000000000000e00 uValue=00000000000c15ef 'vgafont14'
vgafont14:                                   ; 0xc5d6f LB 0xe00
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
  ; disGetNextSymbol 0xc6b6f LB 0x135f -> off=0x0 cb=0000000000001000 uValue=00000000000c23ef 'vgafont16'
vgafont16:                                   ; 0xc6b6f LB 0x1000
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
  ; disGetNextSymbol 0xc7b6f LB 0x35f -> off=0x0 cb=000000000000012d uValue=00000000000c33ef 'vgafont14alt'
vgafont14alt:                                ; 0xc7b6f LB 0x12d
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
  ; disGetNextSymbol 0xc7c9c LB 0x232 -> off=0x0 cb=0000000000000144 uValue=00000000000c351c 'vgafont16alt'
vgafont16alt:                                ; 0xc7c9c LB 0x144
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
  ; disGetNextSymbol 0xc7de0 LB 0xee -> off=0x0 cb=0000000000000008 uValue=00000000000c3660 '_cga_msr'
_cga_msr:                                    ; 0xc7de0 LB 0x8
    db  02ch, 028h, 02dh, 029h, 02ah, 02eh, 01eh, 029h
  ; disGetNextSymbol 0xc7de8 LB 0xe6 -> off=0x0 cb=0000000000000008 uValue=00000000000c3668 'line_to_vpti_200'
line_to_vpti_200:                            ; 0xc7de8 LB 0x8
    db  000h, 001h, 002h, 003h, 0ffh, 0ffh, 0ffh, 007h
  ; disGetNextSymbol 0xc7df0 LB 0xde -> off=0x0 cb=0000000000000008 uValue=00000000000c3670 'line_to_vpti_350'
line_to_vpti_350:                            ; 0xc7df0 LB 0x8
    db  013h, 014h, 015h, 016h, 0ffh, 0ffh, 0ffh, 007h
  ; disGetNextSymbol 0xc7df8 LB 0xd6 -> off=0x0 cb=0000000000000008 uValue=00000000000c3678 'line_to_vpti_400'
line_to_vpti_400:                            ; 0xc7df8 LB 0x8
    db  017h, 017h, 018h, 018h, 0ffh, 0ffh, 0ffh, 019h
  ; disGetNextSymbol 0xc7e00 LB 0xce -> off=0x0 cb=0000000000000004 uValue=00000000000c3680 'row_tbl'
row_tbl:                                     ; 0xc7e00 LB 0x4
    dd  02b190e00h
  ; disGetNextSymbol 0xc7e04 LB 0xca -> off=0x0 cb=0000000000000015 uValue=00000000000c3684 '_vbebios_copyright'
_vbebios_copyright:                          ; 0xc7e04 LB 0x15
    db  'VirtualBox VESA BIOS', 000h
  ; disGetNextSymbol 0xc7e19 LB 0xb5 -> off=0x0 cb=000000000000001d uValue=00000000000c3699 '_vbebios_vendor_name'
_vbebios_vendor_name:                        ; 0xc7e19 LB 0x1d
    db  'Oracle and/or its affiliates', 000h
  ; disGetNextSymbol 0xc7e36 LB 0x98 -> off=0x0 cb=000000000000001e uValue=00000000000c36b6 '_vbebios_product_name'
_vbebios_product_name:                       ; 0xc7e36 LB 0x1e
    db  'Oracle VirtualBox VBE Adapter', 000h
  ; disGetNextSymbol 0xc7e54 LB 0x7a -> off=0x0 cb=0000000000000026 uValue=00000000000c36d4 '_vbebios_product_revision'
_vbebios_product_revision:                   ; 0xc7e54 LB 0x26
    db  'Oracle VirtualBox Version 7.2.0_BETA1', 000h
  ; disGetNextSymbol 0xc7e7a LB 0x54 -> off=0x0 cb=000000000000002b uValue=00000000000c36fa '_vbebios_info_string'
_vbebios_info_string:                        ; 0xc7e7a LB 0x2b
    db  'VirtualBox VBE Display Adapter enabled', 00dh, 00ah, 00dh, 00ah, 000h
  ; disGetNextSymbol 0xc7ea5 LB 0x29 -> off=0x0 cb=0000000000000029 uValue=00000000000c3725 '_no_vbebios_info_string'
_no_vbebios_info_string:                     ; 0xc7ea5 LB 0x29
    db  'No VirtualBox VBE support available!', 00dh, 00ah, 00dh, 00ah, 000h

section CONST progbits vstart=0x7ece align=1 ; size=0x0 class=DATA group=DGROUP

section CONST2 progbits vstart=0x7ece align=1 ; size=0x0 class=DATA group=DGROUP

  ; Padding 0x132 bytes at 0xc7ece
    db  001h, 000h, 000h, 000h, 000h, 001h, 000h, 000h, 000h, 000h, 000h, 000h, 02fh, 068h, 06fh, 06dh
    db  065h, 02fh, 067h, 061h, 06ch, 069h, 074h, 073h, 079h, 06eh, 02fh, 063h, 06fh, 06dh, 070h, 069h
    db  06ch, 065h, 02dh, 063h, 061h, 063h, 068h, 065h, 02fh, 076h, 062h, 06fh, 078h, 02dh, 063h, 06ch
    db  065h, 061h, 06eh, 02fh, 074h, 072h, 075h, 06eh, 06bh, 02fh, 06fh, 075h, 074h, 02fh, 06ch, 069h
    db  06eh, 075h, 078h, 02eh, 061h, 06dh, 064h, 036h, 034h, 02fh, 072h, 065h, 06ch, 065h, 061h, 073h
    db  065h, 02fh, 06fh, 062h, 06ah, 02fh, 056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh
    db  073h, 032h, 038h, 036h, 02fh, 056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh, 073h
    db  032h, 038h, 036h, 02eh, 073h, 079h, 06dh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
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
    db  000h, 033h
