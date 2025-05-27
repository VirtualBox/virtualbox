; $Id: VBoxVgaBiosAlternative386.asm 169020 2025-05-27 12:59:42Z vadim.galitsyn@oracle.com $ 
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
    call 036ach                               ; e8 c2 35                    ; 0xc00e7 vgarom.asm:210
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
  ; disGetNextSymbol 0xc0570 LB 0x397 -> off=0x0 cb=0000000000000007 uValue=00000000000c0570 'do_out_dx_ax'
do_out_dx_ax:                                ; 0xc0570 LB 0x7
    xchg ah, al                               ; 86 c4                       ; 0xc0570 vberom.asm:69
    out DX, AL                                ; ee                          ; 0xc0572 vberom.asm:70
    xchg ah, al                               ; 86 c4                       ; 0xc0573 vberom.asm:71
    out DX, AL                                ; ee                          ; 0xc0575 vberom.asm:72
    retn                                      ; c3                          ; 0xc0576 vberom.asm:73
  ; disGetNextSymbol 0xc0577 LB 0x390 -> off=0x0 cb=0000000000000040 uValue=00000000000c0577 'do_in_ax_dx'
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

section _TEXT progbits vstart=0x9f0 align=1 ; size=0x38c9 class=CODE group=AUTO
  ; disGetNextSymbol 0xc09f0 LB 0x38c9 -> off=0x0 cb=000000000000001a uValue=00000000000c09f0 'set_int_vector'
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
  ; disGetNextSymbol 0xc0a0a LB 0x38af -> off=0x0 cb=000000000000001c uValue=00000000000c0a0a 'init_vga_card'
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
  ; disGetNextSymbol 0xc0a26 LB 0x3893 -> off=0x0 cb=000000000000003e uValue=00000000000c0a26 'init_bios_area'
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
    mov word [es:bx+000a8h], 05553h           ; 26 c7 87 a8 00 53 55        ; 0xc0a55 vgabios.c:242
    mov [es:bx+000aah], ds                    ; 26 8c 9f aa 00              ; 0xc0a5c
    pop bp                                    ; 5d                          ; 0xc0a61 vgabios.c:243
    pop bx                                    ; 5b                          ; 0xc0a62
    retn                                      ; c3                          ; 0xc0a63
  ; disGetNextSymbol 0xc0a64 LB 0x3855 -> off=0x0 cb=000000000000002f uValue=00000000000c0a64 'vgabios_init_func'
vgabios_init_func:                           ; 0xc0a64 LB 0x2f
    push bp                                   ; 55                          ; 0xc0a64 vgabios.c:250
    mov bp, sp                                ; 89 e5                       ; 0xc0a65
    call 00a0ah                               ; e8 a0 ff                    ; 0xc0a67 vgabios.c:252
    call 00a26h                               ; e8 b9 ff                    ; 0xc0a6a vgabios.c:253
    call 03c5ch                               ; e8 ec 31                    ; 0xc0a6d vgabios.c:255
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
  ; disGetNextSymbol 0xc0a93 LB 0x3826 -> off=0x0 cb=000000000000003f uValue=00000000000c0a93 'vga_get_cursor_pos'
vga_get_cursor_pos:                          ; 0xc0a93 LB 0x3f
    push si                                   ; 56                          ; 0xc0a93 vgabios.c:356
    push di                                   ; 57                          ; 0xc0a94
    push bp                                   ; 55                          ; 0xc0a95
    mov bp, sp                                ; 89 e5                       ; 0xc0a96
    mov si, dx                                ; 89 d6                       ; 0xc0a98
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc0a9a vgabios.c:358
    jbe short 00aach                          ; 76 0e                       ; 0xc0a9c
    push SS                                   ; 16                          ; 0xc0a9e vgabios.c:359
    pop ES                                    ; 07                          ; 0xc0a9f
    mov word [es:si], strict word 00000h      ; 26 c7 04 00 00              ; 0xc0aa0
    mov word [es:bx], strict word 00000h      ; 26 c7 07 00 00              ; 0xc0aa5 vgabios.c:360
    jmp short 00aceh                          ; eb 22                       ; 0xc0aaa vgabios.c:361
    mov di, strict word 00060h                ; bf 60 00                    ; 0xc0aac vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0aaf
    mov es, dx                                ; 8e c2                       ; 0xc0ab2
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0ab4
    push SS                                   ; 16                          ; 0xc0ab7 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0ab8
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc0ab9
    movzx si, al                              ; 0f b6 f0                    ; 0xc0abc vgabios.c:364
    add si, si                                ; 01 f6                       ; 0xc0abf
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc0ac1
    mov es, dx                                ; 8e c2                       ; 0xc0ac4 vgabios.c:57
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc0ac6
    push SS                                   ; 16                          ; 0xc0ac9 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0aca
    mov word [es:bx], si                      ; 26 89 37                    ; 0xc0acb
    pop bp                                    ; 5d                          ; 0xc0ace vgabios.c:366
    pop di                                    ; 5f                          ; 0xc0acf
    pop si                                    ; 5e                          ; 0xc0ad0
    retn                                      ; c3                          ; 0xc0ad1
  ; disGetNextSymbol 0xc0ad2 LB 0x37e7 -> off=0x0 cb=000000000000005d uValue=00000000000c0ad2 'vga_find_glyph'
vga_find_glyph:                              ; 0xc0ad2 LB 0x5d
    push bp                                   ; 55                          ; 0xc0ad2 vgabios.c:369
    mov bp, sp                                ; 89 e5                       ; 0xc0ad3
    push si                                   ; 56                          ; 0xc0ad5
    push di                                   ; 57                          ; 0xc0ad6
    push ax                                   ; 50                          ; 0xc0ad7
    push ax                                   ; 50                          ; 0xc0ad8
    push dx                                   ; 52                          ; 0xc0ad9
    push bx                                   ; 53                          ; 0xc0ada
    mov bl, cl                                ; 88 cb                       ; 0xc0adb
    mov word [bp-006h], strict word 00000h    ; c7 46 fa 00 00              ; 0xc0add vgabios.c:371
    dec word [bp+004h]                        ; ff 4e 04                    ; 0xc0ae2 vgabios.c:373
    cmp word [bp+004h], strict byte 0ffffh    ; 83 7e 04 ff                 ; 0xc0ae5
    je short 00b23h                           ; 74 38                       ; 0xc0ae9
    movzx cx, byte [bp+006h]                  ; 0f b6 4e 06                 ; 0xc0aeb vgabios.c:374
    mov dx, ss                                ; 8c d2                       ; 0xc0aef
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc0af1
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc0af4
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc0af7
    push DS                                   ; 1e                          ; 0xc0afa
    mov ds, dx                                ; 8e da                       ; 0xc0afb
    rep cmpsb                                 ; f3 a6                       ; 0xc0afd
    pop DS                                    ; 1f                          ; 0xc0aff
    mov ax, strict word 00000h                ; b8 00 00                    ; 0xc0b00
    je near 00b09h                            ; 0f 84 02 00                 ; 0xc0b03
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc0b07
    test ax, ax                               ; 85 c0                       ; 0xc0b09
    jne short 00b18h                          ; 75 0b                       ; 0xc0b0b
    movzx ax, bl                              ; 0f b6 c3                    ; 0xc0b0d vgabios.c:375
    or ah, 080h                               ; 80 cc 80                    ; 0xc0b10
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0b13
    jmp short 00b23h                          ; eb 0b                       ; 0xc0b16 vgabios.c:376
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc0b18 vgabios.c:378
    add word [bp-008h], ax                    ; 01 46 f8                    ; 0xc0b1c
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc0b1f vgabios.c:379
    jmp short 00ae2h                          ; eb bf                       ; 0xc0b21 vgabios.c:380
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc0b23 vgabios.c:382
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b26
    pop di                                    ; 5f                          ; 0xc0b29
    pop si                                    ; 5e                          ; 0xc0b2a
    pop bp                                    ; 5d                          ; 0xc0b2b
    retn 00004h                               ; c2 04 00                    ; 0xc0b2c
  ; disGetNextSymbol 0xc0b2f LB 0x378a -> off=0x0 cb=0000000000000046 uValue=00000000000c0b2f 'vga_read_glyph_planar'
vga_read_glyph_planar:                       ; 0xc0b2f LB 0x46
    push bp                                   ; 55                          ; 0xc0b2f vgabios.c:384
    mov bp, sp                                ; 89 e5                       ; 0xc0b30
    push si                                   ; 56                          ; 0xc0b32
    push di                                   ; 57                          ; 0xc0b33
    push ax                                   ; 50                          ; 0xc0b34
    push ax                                   ; 50                          ; 0xc0b35
    mov si, ax                                ; 89 c6                       ; 0xc0b36
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc0b38
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc0b3b
    mov bx, cx                                ; 89 cb                       ; 0xc0b3e
    mov ax, 00805h                            ; b8 05 08                    ; 0xc0b40 vgabios.c:391
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b43
    out DX, ax                                ; ef                          ; 0xc0b46
    dec byte [bp+004h]                        ; fe 4e 04                    ; 0xc0b47 vgabios.c:393
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc0b4a
    je short 00b65h                           ; 74 15                       ; 0xc0b4e
    mov es, [bp-006h]                         ; 8e 46 fa                    ; 0xc0b50 vgabios.c:394
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc0b53
    not al                                    ; f6 d0                       ; 0xc0b56
    mov di, bx                                ; 89 df                       ; 0xc0b58
    inc bx                                    ; 43                          ; 0xc0b5a
    push SS                                   ; 16                          ; 0xc0b5b
    pop ES                                    ; 07                          ; 0xc0b5c
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0b5d
    add si, word [bp-008h]                    ; 03 76 f8                    ; 0xc0b60 vgabios.c:395
    jmp short 00b47h                          ; eb e2                       ; 0xc0b63 vgabios.c:396
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0b65 vgabios.c:399
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b68
    out DX, ax                                ; ef                          ; 0xc0b6b
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b6c vgabios.c:400
    pop di                                    ; 5f                          ; 0xc0b6f
    pop si                                    ; 5e                          ; 0xc0b70
    pop bp                                    ; 5d                          ; 0xc0b71
    retn 00002h                               ; c2 02 00                    ; 0xc0b72
  ; disGetNextSymbol 0xc0b75 LB 0x3744 -> off=0x0 cb=000000000000002a uValue=00000000000c0b75 'vga_char_ofs_planar'
vga_char_ofs_planar:                         ; 0xc0b75 LB 0x2a
    push bp                                   ; 55                          ; 0xc0b75 vgabios.c:402
    mov bp, sp                                ; 89 e5                       ; 0xc0b76
    xor dh, dh                                ; 30 f6                       ; 0xc0b78 vgabios.c:406
    imul bx, dx                               ; 0f af da                    ; 0xc0b7a
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc0b7d
    imul bx, dx                               ; 0f af da                    ; 0xc0b81
    xor ah, ah                                ; 30 e4                       ; 0xc0b84
    add ax, bx                                ; 01 d8                       ; 0xc0b86
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc0b88 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0b8b
    mov es, dx                                ; 8e c2                       ; 0xc0b8e
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0b90
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc0b93 vgabios.c:58
    imul dx, bx                               ; 0f af d3                    ; 0xc0b96
    add ax, dx                                ; 01 d0                       ; 0xc0b99
    pop bp                                    ; 5d                          ; 0xc0b9b vgabios.c:410
    retn 00002h                               ; c2 02 00                    ; 0xc0b9c
  ; disGetNextSymbol 0xc0b9f LB 0x371a -> off=0x0 cb=000000000000003e uValue=00000000000c0b9f 'vga_read_char_planar'
vga_read_char_planar:                        ; 0xc0b9f LB 0x3e
    push bp                                   ; 55                          ; 0xc0b9f vgabios.c:412
    mov bp, sp                                ; 89 e5                       ; 0xc0ba0
    push cx                                   ; 51                          ; 0xc0ba2
    push si                                   ; 56                          ; 0xc0ba3
    push di                                   ; 57                          ; 0xc0ba4
    sub sp, strict byte 00010h                ; 83 ec 10                    ; 0xc0ba5
    mov si, ax                                ; 89 c6                       ; 0xc0ba8
    mov ax, dx                                ; 89 d0                       ; 0xc0baa
    movzx di, bl                              ; 0f b6 fb                    ; 0xc0bac vgabios.c:416
    push di                                   ; 57                          ; 0xc0baf
    lea cx, [bp-016h]                         ; 8d 4e ea                    ; 0xc0bb0
    mov bx, si                                ; 89 f3                       ; 0xc0bb3
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0bb5
    call 00b2fh                               ; e8 74 ff                    ; 0xc0bb8
    push di                                   ; 57                          ; 0xc0bbb vgabios.c:419
    push 00100h                               ; 68 00 01                    ; 0xc0bbc
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0bbf vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0bc2
    mov es, ax                                ; 8e c0                       ; 0xc0bc4
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0bc6
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0bc9
    xor cx, cx                                ; 31 c9                       ; 0xc0bcd vgabios.c:68
    lea bx, [bp-016h]                         ; 8d 5e ea                    ; 0xc0bcf
    call 00ad2h                               ; e8 fd fe                    ; 0xc0bd2
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc0bd5 vgabios.c:420
    pop di                                    ; 5f                          ; 0xc0bd8
    pop si                                    ; 5e                          ; 0xc0bd9
    pop cx                                    ; 59                          ; 0xc0bda
    pop bp                                    ; 5d                          ; 0xc0bdb
    retn                                      ; c3                          ; 0xc0bdc
  ; disGetNextSymbol 0xc0bdd LB 0x36dc -> off=0x0 cb=000000000000001a uValue=00000000000c0bdd 'vga_char_ofs_linear'
vga_char_ofs_linear:                         ; 0xc0bdd LB 0x1a
    push bp                                   ; 55                          ; 0xc0bdd vgabios.c:422
    mov bp, sp                                ; 89 e5                       ; 0xc0bde
    xor dh, dh                                ; 30 f6                       ; 0xc0be0 vgabios.c:426
    imul dx, bx                               ; 0f af d3                    ; 0xc0be2
    movzx bx, byte [bp+004h]                  ; 0f b6 5e 04                 ; 0xc0be5
    imul bx, dx                               ; 0f af da                    ; 0xc0be9
    xor ah, ah                                ; 30 e4                       ; 0xc0bec
    add ax, bx                                ; 01 d8                       ; 0xc0bee
    sal ax, 003h                              ; c1 e0 03                    ; 0xc0bf0 vgabios.c:427
    pop bp                                    ; 5d                          ; 0xc0bf3 vgabios.c:429
    retn 00002h                               ; c2 02 00                    ; 0xc0bf4
  ; disGetNextSymbol 0xc0bf7 LB 0x36c2 -> off=0x0 cb=000000000000004b uValue=00000000000c0bf7 'vga_read_glyph_linear'
vga_read_glyph_linear:                       ; 0xc0bf7 LB 0x4b
    push si                                   ; 56                          ; 0xc0bf7 vgabios.c:431
    push di                                   ; 57                          ; 0xc0bf8
    enter 00004h, 000h                        ; c8 04 00 00                 ; 0xc0bf9
    mov si, ax                                ; 89 c6                       ; 0xc0bfd
    mov word [bp-002h], dx                    ; 89 56 fe                    ; 0xc0bff
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc0c02
    mov bx, cx                                ; 89 cb                       ; 0xc0c05
    dec byte [bp+008h]                        ; fe 4e 08                    ; 0xc0c07 vgabios.c:437
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc0c0a
    je short 00c3ch                           ; 74 2c                       ; 0xc0c0e
    xor dh, dh                                ; 30 f6                       ; 0xc0c10 vgabios.c:438
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc0c12 vgabios.c:439
    xor ax, ax                                ; 31 c0                       ; 0xc0c14 vgabios.c:440
    jmp short 00c1dh                          ; eb 05                       ; 0xc0c16
    cmp ax, strict word 00008h                ; 3d 08 00                    ; 0xc0c18
    jnl short 00c31h                          ; 7d 14                       ; 0xc0c1b
    mov es, [bp-002h]                         ; 8e 46 fe                    ; 0xc0c1d vgabios.c:441
    mov di, si                                ; 89 f7                       ; 0xc0c20
    add di, ax                                ; 01 c7                       ; 0xc0c22
    cmp byte [es:di], 000h                    ; 26 80 3d 00                 ; 0xc0c24
    je short 00c2ch                           ; 74 02                       ; 0xc0c28
    or dh, dl                                 ; 08 d6                       ; 0xc0c2a vgabios.c:442
    shr dl, 1                                 ; d0 ea                       ; 0xc0c2c vgabios.c:443
    inc ax                                    ; 40                          ; 0xc0c2e vgabios.c:444
    jmp short 00c18h                          ; eb e7                       ; 0xc0c2f
    mov di, bx                                ; 89 df                       ; 0xc0c31 vgabios.c:445
    inc bx                                    ; 43                          ; 0xc0c33
    mov byte [ss:di], dh                      ; 36 88 35                    ; 0xc0c34
    add si, word [bp-004h]                    ; 03 76 fc                    ; 0xc0c37 vgabios.c:446
    jmp short 00c07h                          ; eb cb                       ; 0xc0c3a vgabios.c:447
    leave                                     ; c9                          ; 0xc0c3c vgabios.c:448
    pop di                                    ; 5f                          ; 0xc0c3d
    pop si                                    ; 5e                          ; 0xc0c3e
    retn 00002h                               ; c2 02 00                    ; 0xc0c3f
  ; disGetNextSymbol 0xc0c42 LB 0x3677 -> off=0x0 cb=000000000000003f uValue=00000000000c0c42 'vga_read_char_linear'
vga_read_char_linear:                        ; 0xc0c42 LB 0x3f
    push bp                                   ; 55                          ; 0xc0c42 vgabios.c:450
    mov bp, sp                                ; 89 e5                       ; 0xc0c43
    push cx                                   ; 51                          ; 0xc0c45
    push si                                   ; 56                          ; 0xc0c46
    sub sp, strict byte 00010h                ; 83 ec 10                    ; 0xc0c47
    mov cx, ax                                ; 89 c1                       ; 0xc0c4a
    mov ax, dx                                ; 89 d0                       ; 0xc0c4c
    movzx si, bl                              ; 0f b6 f3                    ; 0xc0c4e vgabios.c:454
    push si                                   ; 56                          ; 0xc0c51
    mov bx, cx                                ; 89 cb                       ; 0xc0c52
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0c54
    lea cx, [bp-014h]                         ; 8d 4e ec                    ; 0xc0c57
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0c5a
    call 00bf7h                               ; e8 97 ff                    ; 0xc0c5d
    push si                                   ; 56                          ; 0xc0c60 vgabios.c:457
    push 00100h                               ; 68 00 01                    ; 0xc0c61
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0c64 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0c67
    mov es, ax                                ; 8e c0                       ; 0xc0c69
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c6b
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0c6e
    xor cx, cx                                ; 31 c9                       ; 0xc0c72 vgabios.c:68
    lea bx, [bp-014h]                         ; 8d 5e ec                    ; 0xc0c74
    call 00ad2h                               ; e8 58 fe                    ; 0xc0c77
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0c7a vgabios.c:458
    pop si                                    ; 5e                          ; 0xc0c7d
    pop cx                                    ; 59                          ; 0xc0c7e
    pop bp                                    ; 5d                          ; 0xc0c7f
    retn                                      ; c3                          ; 0xc0c80
  ; disGetNextSymbol 0xc0c81 LB 0x3638 -> off=0x0 cb=0000000000000035 uValue=00000000000c0c81 'vga_read_2bpp_char'
vga_read_2bpp_char:                          ; 0xc0c81 LB 0x35
    push bp                                   ; 55                          ; 0xc0c81 vgabios.c:460
    mov bp, sp                                ; 89 e5                       ; 0xc0c82
    push bx                                   ; 53                          ; 0xc0c84
    push cx                                   ; 51                          ; 0xc0c85
    mov bx, ax                                ; 89 c3                       ; 0xc0c86
    mov es, dx                                ; 8e c2                       ; 0xc0c88
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0c8a vgabios.c:466
    mov DH, strict byte 080h                  ; b6 80                       ; 0xc0c8d vgabios.c:467
    xor dl, dl                                ; 30 d2                       ; 0xc0c8f vgabios.c:468
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c91 vgabios.c:469
    xchg ah, al                               ; 86 c4                       ; 0xc0c94
    xor bx, bx                                ; 31 db                       ; 0xc0c96 vgabios.c:471
    jmp short 00c9fh                          ; eb 05                       ; 0xc0c98
    cmp bx, strict byte 00008h                ; 83 fb 08                    ; 0xc0c9a
    jnl short 00cadh                          ; 7d 0e                       ; 0xc0c9d
    test ax, cx                               ; 85 c8                       ; 0xc0c9f vgabios.c:472
    je short 00ca5h                           ; 74 02                       ; 0xc0ca1
    or dl, dh                                 ; 08 f2                       ; 0xc0ca3 vgabios.c:473
    shr dh, 1                                 ; d0 ee                       ; 0xc0ca5 vgabios.c:474
    shr cx, 002h                              ; c1 e9 02                    ; 0xc0ca7 vgabios.c:475
    inc bx                                    ; 43                          ; 0xc0caa vgabios.c:476
    jmp short 00c9ah                          ; eb ed                       ; 0xc0cab
    mov al, dl                                ; 88 d0                       ; 0xc0cad vgabios.c:478
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0caf
    pop cx                                    ; 59                          ; 0xc0cb2
    pop bx                                    ; 5b                          ; 0xc0cb3
    pop bp                                    ; 5d                          ; 0xc0cb4
    retn                                      ; c3                          ; 0xc0cb5
  ; disGetNextSymbol 0xc0cb6 LB 0x3603 -> off=0x0 cb=0000000000000084 uValue=00000000000c0cb6 'vga_read_glyph_cga'
vga_read_glyph_cga:                          ; 0xc0cb6 LB 0x84
    push bp                                   ; 55                          ; 0xc0cb6 vgabios.c:480
    mov bp, sp                                ; 89 e5                       ; 0xc0cb7
    push cx                                   ; 51                          ; 0xc0cb9
    push si                                   ; 56                          ; 0xc0cba
    push di                                   ; 57                          ; 0xc0cbb
    push ax                                   ; 50                          ; 0xc0cbc
    mov si, dx                                ; 89 d6                       ; 0xc0cbd
    cmp bl, 006h                              ; 80 fb 06                    ; 0xc0cbf vgabios.c:488
    je short 00cfeh                           ; 74 3a                       ; 0xc0cc2
    mov bx, ax                                ; 89 c3                       ; 0xc0cc4 vgabios.c:490
    add bx, ax                                ; 01 c3                       ; 0xc0cc6
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0cc8
    xor cx, cx                                ; 31 c9                       ; 0xc0ccd vgabios.c:492
    jmp short 00cd6h                          ; eb 05                       ; 0xc0ccf
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0cd1
    jnl short 00d32h                          ; 7d 5c                       ; 0xc0cd4
    mov ax, bx                                ; 89 d8                       ; 0xc0cd6 vgabios.c:493
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0cd8
    call 00c81h                               ; e8 a3 ff                    ; 0xc0cdb
    mov di, si                                ; 89 f7                       ; 0xc0cde
    inc si                                    ; 46                          ; 0xc0ce0
    push SS                                   ; 16                          ; 0xc0ce1
    pop ES                                    ; 07                          ; 0xc0ce2
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0ce3
    lea ax, [bx+02000h]                       ; 8d 87 00 20                 ; 0xc0ce6 vgabios.c:494
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0cea
    call 00c81h                               ; e8 91 ff                    ; 0xc0ced
    mov di, si                                ; 89 f7                       ; 0xc0cf0
    inc si                                    ; 46                          ; 0xc0cf2
    push SS                                   ; 16                          ; 0xc0cf3
    pop ES                                    ; 07                          ; 0xc0cf4
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0cf5
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0cf8 vgabios.c:495
    inc cx                                    ; 41                          ; 0xc0cfb vgabios.c:496
    jmp short 00cd1h                          ; eb d3                       ; 0xc0cfc
    mov bx, ax                                ; 89 c3                       ; 0xc0cfe vgabios.c:498
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0d00
    xor cx, cx                                ; 31 c9                       ; 0xc0d05 vgabios.c:499
    jmp short 00d0eh                          ; eb 05                       ; 0xc0d07
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0d09
    jnl short 00d32h                          ; 7d 24                       ; 0xc0d0c
    mov di, si                                ; 89 f7                       ; 0xc0d0e vgabios.c:500
    inc si                                    ; 46                          ; 0xc0d10
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d11
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0d14
    push SS                                   ; 16                          ; 0xc0d17
    pop ES                                    ; 07                          ; 0xc0d18
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d19
    mov di, si                                ; 89 f7                       ; 0xc0d1c vgabios.c:501
    inc si                                    ; 46                          ; 0xc0d1e
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d1f
    mov al, byte [es:bx+02000h]               ; 26 8a 87 00 20              ; 0xc0d22
    push SS                                   ; 16                          ; 0xc0d27
    pop ES                                    ; 07                          ; 0xc0d28
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d29
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d2c vgabios.c:502
    inc cx                                    ; 41                          ; 0xc0d2f vgabios.c:503
    jmp short 00d09h                          ; eb d7                       ; 0xc0d30
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc0d32 vgabios.c:505
    pop di                                    ; 5f                          ; 0xc0d35
    pop si                                    ; 5e                          ; 0xc0d36
    pop cx                                    ; 59                          ; 0xc0d37
    pop bp                                    ; 5d                          ; 0xc0d38
    retn                                      ; c3                          ; 0xc0d39
  ; disGetNextSymbol 0xc0d3a LB 0x357f -> off=0x0 cb=0000000000000011 uValue=00000000000c0d3a 'vga_char_ofs_cga'
vga_char_ofs_cga:                            ; 0xc0d3a LB 0x11
    push bp                                   ; 55                          ; 0xc0d3a vgabios.c:507
    mov bp, sp                                ; 89 e5                       ; 0xc0d3b
    xor dh, dh                                ; 30 f6                       ; 0xc0d3d vgabios.c:512
    imul dx, bx                               ; 0f af d3                    ; 0xc0d3f
    sal dx, 002h                              ; c1 e2 02                    ; 0xc0d42
    xor ah, ah                                ; 30 e4                       ; 0xc0d45
    add ax, dx                                ; 01 d0                       ; 0xc0d47
    pop bp                                    ; 5d                          ; 0xc0d49 vgabios.c:513
    retn                                      ; c3                          ; 0xc0d4a
  ; disGetNextSymbol 0xc0d4b LB 0x356e -> off=0x0 cb=0000000000000065 uValue=00000000000c0d4b 'vga_read_char_cga'
vga_read_char_cga:                           ; 0xc0d4b LB 0x65
    push bp                                   ; 55                          ; 0xc0d4b vgabios.c:515
    mov bp, sp                                ; 89 e5                       ; 0xc0d4c
    push bx                                   ; 53                          ; 0xc0d4e
    push cx                                   ; 51                          ; 0xc0d4f
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc0d50
    movzx bx, dl                              ; 0f b6 da                    ; 0xc0d53 vgabios.c:521
    lea dx, [bp-00eh]                         ; 8d 56 f2                    ; 0xc0d56
    call 00cb6h                               ; e8 5a ff                    ; 0xc0d59
    push strict byte 00008h                   ; 6a 08                       ; 0xc0d5c vgabios.c:524
    push 00080h                               ; 68 80 00                    ; 0xc0d5e
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0d61 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d64
    mov es, ax                                ; 8e c0                       ; 0xc0d66
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0d68
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0d6b
    xor cx, cx                                ; 31 c9                       ; 0xc0d6f vgabios.c:68
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0d71
    call 00ad2h                               ; e8 5b fd                    ; 0xc0d74
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0d77
    test ah, 080h                             ; f6 c4 80                    ; 0xc0d7a vgabios.c:526
    jne short 00da6h                          ; 75 27                       ; 0xc0d7d
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0d7f vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d82
    mov es, ax                                ; 8e c0                       ; 0xc0d84
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0d86
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0d89
    test dx, dx                               ; 85 d2                       ; 0xc0d8d vgabios.c:530
    jne short 00d95h                          ; 75 04                       ; 0xc0d8f
    test ax, ax                               ; 85 c0                       ; 0xc0d91
    je short 00da6h                           ; 74 11                       ; 0xc0d93
    push strict byte 00008h                   ; 6a 08                       ; 0xc0d95 vgabios.c:531
    push 00080h                               ; 68 80 00                    ; 0xc0d97
    mov cx, 00080h                            ; b9 80 00                    ; 0xc0d9a
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0d9d
    call 00ad2h                               ; e8 2f fd                    ; 0xc0da0
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0da3
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc0da6 vgabios.c:534
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0da9
    pop cx                                    ; 59                          ; 0xc0dac
    pop bx                                    ; 5b                          ; 0xc0dad
    pop bp                                    ; 5d                          ; 0xc0dae
    retn                                      ; c3                          ; 0xc0daf
  ; disGetNextSymbol 0xc0db0 LB 0x3509 -> off=0x0 cb=0000000000000127 uValue=00000000000c0db0 'vga_read_char_attr'
vga_read_char_attr:                          ; 0xc0db0 LB 0x127
    push bp                                   ; 55                          ; 0xc0db0 vgabios.c:536
    mov bp, sp                                ; 89 e5                       ; 0xc0db1
    push bx                                   ; 53                          ; 0xc0db3
    push cx                                   ; 51                          ; 0xc0db4
    push si                                   ; 56                          ; 0xc0db5
    push di                                   ; 57                          ; 0xc0db6
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0db7
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0dba
    mov si, dx                                ; 89 d6                       ; 0xc0dbd
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0dbf vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0dc2
    mov es, ax                                ; 8e c0                       ; 0xc0dc5
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0dc7
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc0dca vgabios.c:48
    xor ah, ah                                ; 30 e4                       ; 0xc0dcd vgabios.c:544
    call 035edh                               ; e8 1b 28                    ; 0xc0dcf
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc0dd2
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0dd5 vgabios.c:545
    je near 00eceh                            ; 0f 84 f3 00                 ; 0xc0dd7
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc0ddb vgabios.c:549
    lea bx, [bp-018h]                         ; 8d 5e e8                    ; 0xc0ddf
    lea dx, [bp-01ah]                         ; 8d 56 e6                    ; 0xc0de2
    mov ax, cx                                ; 89 c8                       ; 0xc0de5
    call 00a93h                               ; e8 a9 fc                    ; 0xc0de7
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc0dea vgabios.c:550
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc0ded
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc0df0 vgabios.c:551
    xor al, al                                ; 30 c0                       ; 0xc0df3
    shr ax, 008h                              ; c1 e8 08                    ; 0xc0df5
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc0df8
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0dfb vgabios.c:47
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0dfe
    mov es, dx                                ; 8e c2                       ; 0xc0e01
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc0e03
    xor dh, dh                                ; 30 f6                       ; 0xc0e06 vgabios.c:48
    inc dx                                    ; 42                          ; 0xc0e08
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc0e09 vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0e0c
    mov word [bp-014h], di                    ; 89 7e ec                    ; 0xc0e0f vgabios.c:58
    movzx bx, byte [bp-012h]                  ; 0f b6 5e ee                 ; 0xc0e12 vgabios.c:557
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0e16
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc0e19
    jne short 00e56h                          ; 75 36                       ; 0xc0e1e
    imul dx, di                               ; 0f af d7                    ; 0xc0e20 vgabios.c:559
    add dx, dx                                ; 01 d2                       ; 0xc0e23
    or dl, 0ffh                               ; 80 ca ff                    ; 0xc0e25
    mov word [bp-016h], dx                    ; 89 56 ea                    ; 0xc0e28
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc0e2b
    mov cx, word [bp-016h]                    ; 8b 4e ea                    ; 0xc0e2f
    inc cx                                    ; 41                          ; 0xc0e32
    imul dx, cx                               ; 0f af d1                    ; 0xc0e33
    xor ah, ah                                ; 30 e4                       ; 0xc0e36
    imul di, ax                               ; 0f af f8                    ; 0xc0e38
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0e3b
    add ax, di                                ; 01 f8                       ; 0xc0e3f
    add ax, ax                                ; 01 c0                       ; 0xc0e41
    mov di, dx                                ; 89 d7                       ; 0xc0e43
    add di, ax                                ; 01 c7                       ; 0xc0e45
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc0e47 vgabios.c:55
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc0e4b
    push SS                                   ; 16                          ; 0xc0e4e vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0e4f
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0e50
    jmp near 00eceh                           ; e9 78 00                    ; 0xc0e53 vgabios.c:561
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc0e56 vgabios.c:562
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc0e5a
    je short 00eaah                           ; 74 4b                       ; 0xc0e5d
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc0e5f
    jc short 00eceh                           ; 72 6a                       ; 0xc0e62
    jbe short 00e6dh                          ; 76 07                       ; 0xc0e64
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc0e66
    jbe short 00e86h                          ; 76 1b                       ; 0xc0e69
    jmp short 00eceh                          ; eb 61                       ; 0xc0e6b
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc0e6d vgabios.c:565
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0e71
    mov bx, word [bp-014h]                    ; 8b 5e ec                    ; 0xc0e75
    call 00d3ah                               ; e8 bf fe                    ; 0xc0e78
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc0e7b vgabios.c:566
    call 00d4bh                               ; e8 c9 fe                    ; 0xc0e7f
    xor ah, ah                                ; 30 e4                       ; 0xc0e82
    jmp short 00e4eh                          ; eb c8                       ; 0xc0e84
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0e86 vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0e89
    xor dh, dh                                ; 30 f6                       ; 0xc0e8c vgabios.c:571
    mov word [bp-016h], dx                    ; 89 56 ea                    ; 0xc0e8e
    push dx                                   ; 52                          ; 0xc0e91
    movzx dx, al                              ; 0f b6 d0                    ; 0xc0e92
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0e95
    mov bx, di                                ; 89 fb                       ; 0xc0e99
    call 00b75h                               ; e8 d7 fc                    ; 0xc0e9b
    mov bx, word [bp-016h]                    ; 8b 5e ea                    ; 0xc0e9e vgabios.c:572
    mov dx, ax                                ; 89 c2                       ; 0xc0ea1
    mov ax, di                                ; 89 f8                       ; 0xc0ea3
    call 00b9fh                               ; e8 f7 fc                    ; 0xc0ea5
    jmp short 00e82h                          ; eb d8                       ; 0xc0ea8
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0eaa vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0ead
    xor dh, dh                                ; 30 f6                       ; 0xc0eb0 vgabios.c:576
    mov word [bp-016h], dx                    ; 89 56 ea                    ; 0xc0eb2
    push dx                                   ; 52                          ; 0xc0eb5
    movzx dx, al                              ; 0f b6 d0                    ; 0xc0eb6
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc0eb9
    mov bx, di                                ; 89 fb                       ; 0xc0ebd
    call 00bddh                               ; e8 1b fd                    ; 0xc0ebf
    mov bx, word [bp-016h]                    ; 8b 5e ea                    ; 0xc0ec2 vgabios.c:577
    mov dx, ax                                ; 89 c2                       ; 0xc0ec5
    mov ax, di                                ; 89 f8                       ; 0xc0ec7
    call 00c42h                               ; e8 76 fd                    ; 0xc0ec9
    jmp short 00e82h                          ; eb b4                       ; 0xc0ecc
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc0ece vgabios.c:586
    pop di                                    ; 5f                          ; 0xc0ed1
    pop si                                    ; 5e                          ; 0xc0ed2
    pop cx                                    ; 59                          ; 0xc0ed3
    pop bx                                    ; 5b                          ; 0xc0ed4
    pop bp                                    ; 5d                          ; 0xc0ed5
    retn                                      ; c3                          ; 0xc0ed6
  ; disGetNextSymbol 0xc0ed7 LB 0x33e2 -> off=0x10 cb=0000000000000083 uValue=00000000000c0ee7 'vga_get_font_info'
    db  0feh, 00eh, 043h, 00fh, 048h, 00fh, 04fh, 00fh, 054h, 00fh, 059h, 00fh, 05eh, 00fh, 063h, 00fh
vga_get_font_info:                           ; 0xc0ee7 LB 0x83
    push si                                   ; 56                          ; 0xc0ee7 vgabios.c:588
    push di                                   ; 57                          ; 0xc0ee8
    push bp                                   ; 55                          ; 0xc0ee9
    mov bp, sp                                ; 89 e5                       ; 0xc0eea
    mov di, dx                                ; 89 d7                       ; 0xc0eec
    mov si, bx                                ; 89 de                       ; 0xc0eee
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc0ef0 vgabios.c:593
    jnbe short 00f3dh                         ; 77 48                       ; 0xc0ef3
    mov bx, ax                                ; 89 c3                       ; 0xc0ef5
    add bx, ax                                ; 01 c3                       ; 0xc0ef7
    jmp word [cs:bx+00ed7h]                   ; 2e ff a7 d7 0e              ; 0xc0ef9
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0efe vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0f01
    mov es, ax                                ; 8e c0                       ; 0xc0f03
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0f05
    mov ax, word [es:bx+002h]                 ; 26 8b 47 02                 ; 0xc0f08
    push SS                                   ; 16                          ; 0xc0f0c vgabios.c:596
    pop ES                                    ; 07                          ; 0xc0f0d
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc0f0e
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc0f11
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0f14
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f17
    mov es, ax                                ; 8e c0                       ; 0xc0f1a
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f1c
    xor ah, ah                                ; 30 e4                       ; 0xc0f1f
    push SS                                   ; 16                          ; 0xc0f21
    pop ES                                    ; 07                          ; 0xc0f22
    mov bx, cx                                ; 89 cb                       ; 0xc0f23
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f25
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0f28
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f2b
    mov es, ax                                ; 8e c0                       ; 0xc0f2e
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f30
    xor ah, ah                                ; 30 e4                       ; 0xc0f33
    push SS                                   ; 16                          ; 0xc0f35
    pop ES                                    ; 07                          ; 0xc0f36
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc0f37
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f3a
    pop bp                                    ; 5d                          ; 0xc0f3d
    pop di                                    ; 5f                          ; 0xc0f3e
    pop si                                    ; 5e                          ; 0xc0f3f
    retn 00002h                               ; c2 02 00                    ; 0xc0f40
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0f43 vgabios.c:67
    jmp short 00f01h                          ; eb b9                       ; 0xc0f46
    mov dx, 05d6fh                            ; ba 6f 5d                    ; 0xc0f48 vgabios.c:601
    mov ax, ds                                ; 8c d8                       ; 0xc0f4b
    jmp short 00f0ch                          ; eb bd                       ; 0xc0f4d vgabios.c:602
    mov dx, 0556fh                            ; ba 6f 55                    ; 0xc0f4f vgabios.c:604
    jmp short 00f4bh                          ; eb f7                       ; 0xc0f52
    mov dx, 0596fh                            ; ba 6f 59                    ; 0xc0f54 vgabios.c:607
    jmp short 00f4bh                          ; eb f2                       ; 0xc0f57
    mov dx, 07b6fh                            ; ba 6f 7b                    ; 0xc0f59 vgabios.c:610
    jmp short 00f4bh                          ; eb ed                       ; 0xc0f5c
    mov dx, 06b6fh                            ; ba 6f 6b                    ; 0xc0f5e vgabios.c:613
    jmp short 00f4bh                          ; eb e8                       ; 0xc0f61
    mov dx, 07c9ch                            ; ba 9c 7c                    ; 0xc0f63 vgabios.c:616
    jmp short 00f4bh                          ; eb e3                       ; 0xc0f66
    jmp short 00f3dh                          ; eb d3                       ; 0xc0f68 vgabios.c:622
  ; disGetNextSymbol 0xc0f6a LB 0x334f -> off=0x0 cb=0000000000000156 uValue=00000000000c0f6a 'vga_read_pixel'
vga_read_pixel:                              ; 0xc0f6a LB 0x156
    push bp                                   ; 55                          ; 0xc0f6a vgabios.c:635
    mov bp, sp                                ; 89 e5                       ; 0xc0f6b
    push si                                   ; 56                          ; 0xc0f6d
    push di                                   ; 57                          ; 0xc0f6e
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc0f6f
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0f72
    mov word [bp-00ch], bx                    ; 89 5e f4                    ; 0xc0f75
    mov si, cx                                ; 89 ce                       ; 0xc0f78
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0f7a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f7d
    mov es, ax                                ; 8e c0                       ; 0xc0f80
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f82
    xor ah, ah                                ; 30 e4                       ; 0xc0f85 vgabios.c:642
    call 035edh                               ; e8 63 26                    ; 0xc0f87
    mov ah, al                                ; 88 c4                       ; 0xc0f8a
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0f8c vgabios.c:643
    je near 010b9h                            ; 0f 84 27 01                 ; 0xc0f8e
    movzx bx, al                              ; 0f b6 d8                    ; 0xc0f92 vgabios.c:645
    sal bx, 003h                              ; c1 e3 03                    ; 0xc0f95
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc0f98
    je near 010b9h                            ; 0f 84 18 01                 ; 0xc0f9d
    mov ch, byte [bx+047b3h]                  ; 8a af b3 47                 ; 0xc0fa1 vgabios.c:649
    cmp ch, 003h                              ; 80 fd 03                    ; 0xc0fa5
    jc short 00fbbh                           ; 72 11                       ; 0xc0fa8
    jbe short 00fc3h                          ; 76 17                       ; 0xc0faa
    cmp ch, 005h                              ; 80 fd 05                    ; 0xc0fac
    je near 01092h                            ; 0f 84 df 00                 ; 0xc0faf
    cmp ch, 004h                              ; 80 fd 04                    ; 0xc0fb3
    je short 00fc3h                           ; 74 0b                       ; 0xc0fb6
    jmp near 010b2h                           ; e9 f7 00                    ; 0xc0fb8
    cmp ch, 002h                              ; 80 fd 02                    ; 0xc0fbb
    je short 0102eh                           ; 74 6e                       ; 0xc0fbe
    jmp near 010b2h                           ; e9 ef 00                    ; 0xc0fc0
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc0fc3 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0fc6
    mov es, ax                                ; 8e c0                       ; 0xc0fc9
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0fcb
    imul ax, word [bp-00ch]                   ; 0f af 46 f4                 ; 0xc0fce vgabios.c:58
    mov bx, dx                                ; 89 d3                       ; 0xc0fd2
    shr bx, 003h                              ; c1 eb 03                    ; 0xc0fd4
    add bx, ax                                ; 01 c3                       ; 0xc0fd7
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc0fd9 vgabios.c:57
    mov cx, word [es:di]                      ; 26 8b 0d                    ; 0xc0fdc
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc0fdf vgabios.c:58
    imul ax, cx                               ; 0f af c1                    ; 0xc0fe3
    add bx, ax                                ; 01 c3                       ; 0xc0fe6
    mov cl, dl                                ; 88 d1                       ; 0xc0fe8 vgabios.c:654
    and cl, 007h                              ; 80 e1 07                    ; 0xc0fea
    mov ax, 00080h                            ; b8 80 00                    ; 0xc0fed
    sar ax, CL                                ; d3 f8                       ; 0xc0ff0
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc0ff2
    xor ch, ch                                ; 30 ed                       ; 0xc0ff5 vgabios.c:655
    mov byte [bp-006h], ch                    ; 88 6e fa                    ; 0xc0ff7 vgabios.c:656
    jmp short 01004h                          ; eb 08                       ; 0xc0ffa
    cmp byte [bp-006h], 004h                  ; 80 7e fa 04                 ; 0xc0ffc
    jnc near 010b4h                           ; 0f 83 b0 00                 ; 0xc1000
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc1004 vgabios.c:657
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1008
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc100b
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc100d
    out DX, ax                                ; ef                          ; 0xc1010
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc1011 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc1014
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1016
    and al, byte [bp-008h]                    ; 22 46 f8                    ; 0xc1019 vgabios.c:48
    test al, al                               ; 84 c0                       ; 0xc101c vgabios.c:659
    jbe short 01029h                          ; 76 09                       ; 0xc101e
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc1020 vgabios.c:660
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc1023
    sal al, CL                                ; d2 e0                       ; 0xc1025
    or ch, al                                 ; 08 c5                       ; 0xc1027
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1029 vgabios.c:661
    jmp short 00ffch                          ; eb ce                       ; 0xc102c
    movzx cx, byte [bx+047b4h]                ; 0f b6 8f b4 47              ; 0xc102e vgabios.c:664
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc1033
    sub bx, cx                                ; 29 cb                       ; 0xc1036
    mov cx, bx                                ; 89 d9                       ; 0xc1038
    mov bx, dx                                ; 89 d3                       ; 0xc103a
    shr bx, CL                                ; d3 eb                       ; 0xc103c
    mov cx, bx                                ; 89 d9                       ; 0xc103e
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc1040
    shr bx, 1                                 ; d1 eb                       ; 0xc1043
    imul bx, bx, strict byte 00050h           ; 6b db 50                    ; 0xc1045
    add bx, cx                                ; 01 cb                       ; 0xc1048
    test byte [bp-00ch], 001h                 ; f6 46 f4 01                 ; 0xc104a vgabios.c:665
    je short 01053h                           ; 74 03                       ; 0xc104e
    add bh, 020h                              ; 80 c7 20                    ; 0xc1050 vgabios.c:666
    mov cx, 0b800h                            ; b9 00 b8                    ; 0xc1053 vgabios.c:47
    mov es, cx                                ; 8e c1                       ; 0xc1056
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1058
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc105b vgabios.c:668
    sal bx, 003h                              ; c1 e3 03                    ; 0xc105e
    cmp byte [bx+047b4h], 002h                ; 80 bf b4 47 02              ; 0xc1061
    jne short 0107dh                          ; 75 15                       ; 0xc1066
    and dx, strict byte 00003h                ; 83 e2 03                    ; 0xc1068 vgabios.c:669
    mov cx, strict word 00003h                ; b9 03 00                    ; 0xc106b
    sub cx, dx                                ; 29 d1                       ; 0xc106e
    add cx, cx                                ; 01 c9                       ; 0xc1070
    xor ah, ah                                ; 30 e4                       ; 0xc1072
    sar ax, CL                                ; d3 f8                       ; 0xc1074
    mov ch, al                                ; 88 c5                       ; 0xc1076
    and ch, 003h                              ; 80 e5 03                    ; 0xc1078
    jmp short 010b4h                          ; eb 37                       ; 0xc107b vgabios.c:670
    xor dh, dh                                ; 30 f6                       ; 0xc107d vgabios.c:671
    and dl, 007h                              ; 80 e2 07                    ; 0xc107f
    mov cx, strict word 00007h                ; b9 07 00                    ; 0xc1082
    sub cx, dx                                ; 29 d1                       ; 0xc1085
    xor ah, ah                                ; 30 e4                       ; 0xc1087
    sar ax, CL                                ; d3 f8                       ; 0xc1089
    mov ch, al                                ; 88 c5                       ; 0xc108b
    and ch, 001h                              ; 80 e5 01                    ; 0xc108d
    jmp short 010b4h                          ; eb 22                       ; 0xc1090 vgabios.c:672
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1092 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1095
    mov es, ax                                ; 8e c0                       ; 0xc1098
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc109a
    sal ax, 003h                              ; c1 e0 03                    ; 0xc109d vgabios.c:58
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc10a0
    imul bx, ax                               ; 0f af d8                    ; 0xc10a3
    add bx, dx                                ; 01 d3                       ; 0xc10a6
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc10a8 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc10ab
    mov ch, byte [es:bx]                      ; 26 8a 2f                    ; 0xc10ad
    jmp short 010b4h                          ; eb 02                       ; 0xc10b0 vgabios.c:676
    xor ch, ch                                ; 30 ed                       ; 0xc10b2 vgabios.c:681
    push SS                                   ; 16                          ; 0xc10b4 vgabios.c:683
    pop ES                                    ; 07                          ; 0xc10b5
    mov byte [es:si], ch                      ; 26 88 2c                    ; 0xc10b6
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc10b9 vgabios.c:684
    pop di                                    ; 5f                          ; 0xc10bc
    pop si                                    ; 5e                          ; 0xc10bd
    pop bp                                    ; 5d                          ; 0xc10be
    retn                                      ; c3                          ; 0xc10bf
  ; disGetNextSymbol 0xc10c0 LB 0x31f9 -> off=0x0 cb=000000000000008c uValue=00000000000c10c0 'biosfn_perform_gray_scale_summing'
biosfn_perform_gray_scale_summing:           ; 0xc10c0 LB 0x8c
    push bp                                   ; 55                          ; 0xc10c0 vgabios.c:689
    mov bp, sp                                ; 89 e5                       ; 0xc10c1
    push bx                                   ; 53                          ; 0xc10c3
    push cx                                   ; 51                          ; 0xc10c4
    push si                                   ; 56                          ; 0xc10c5
    push di                                   ; 57                          ; 0xc10c6
    push ax                                   ; 50                          ; 0xc10c7
    push ax                                   ; 50                          ; 0xc10c8
    mov bx, ax                                ; 89 c3                       ; 0xc10c9
    mov di, dx                                ; 89 d7                       ; 0xc10cb
    mov dx, 003dah                            ; ba da 03                    ; 0xc10cd vgabios.c:694
    in AL, DX                                 ; ec                          ; 0xc10d0
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10d1
    xor al, al                                ; 30 c0                       ; 0xc10d3 vgabios.c:695
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc10d5
    out DX, AL                                ; ee                          ; 0xc10d8
    xor si, si                                ; 31 f6                       ; 0xc10d9 vgabios.c:697
    cmp si, di                                ; 39 fe                       ; 0xc10db
    jnc short 01131h                          ; 73 52                       ; 0xc10dd
    mov al, bl                                ; 88 d8                       ; 0xc10df vgabios.c:700
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc10e1
    out DX, AL                                ; ee                          ; 0xc10e4
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc10e5 vgabios.c:702
    in AL, DX                                 ; ec                          ; 0xc10e8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10e9
    mov cx, ax                                ; 89 c1                       ; 0xc10eb
    in AL, DX                                 ; ec                          ; 0xc10ed vgabios.c:703
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10ee
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc10f0
    in AL, DX                                 ; ec                          ; 0xc10f3 vgabios.c:704
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc10f4
    xor ch, ch                                ; 30 ed                       ; 0xc10f6 vgabios.c:707
    imul cx, cx, strict byte 0004dh           ; 6b c9 4d                    ; 0xc10f8
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc10fb
    movzx cx, byte [bp-00ch]                  ; 0f b6 4e f4                 ; 0xc10fe
    imul cx, cx, 00097h                       ; 69 c9 97 00                 ; 0xc1102
    add cx, word [bp-00ah]                    ; 03 4e f6                    ; 0xc1106
    xor ah, ah                                ; 30 e4                       ; 0xc1109
    imul ax, ax, strict byte 0001ch           ; 6b c0 1c                    ; 0xc110b
    add cx, ax                                ; 01 c1                       ; 0xc110e
    add cx, 00080h                            ; 81 c1 80 00                 ; 0xc1110
    sar cx, 008h                              ; c1 f9 08                    ; 0xc1114
    cmp cx, strict byte 0003fh                ; 83 f9 3f                    ; 0xc1117 vgabios.c:709
    jbe short 0111fh                          ; 76 03                       ; 0xc111a
    mov cx, strict word 0003fh                ; b9 3f 00                    ; 0xc111c
    mov al, bl                                ; 88 d8                       ; 0xc111f vgabios.c:712
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc1121
    out DX, AL                                ; ee                          ; 0xc1124
    mov al, cl                                ; 88 c8                       ; 0xc1125 vgabios.c:714
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1127
    out DX, AL                                ; ee                          ; 0xc112a
    out DX, AL                                ; ee                          ; 0xc112b vgabios.c:715
    out DX, AL                                ; ee                          ; 0xc112c vgabios.c:716
    inc bx                                    ; 43                          ; 0xc112d vgabios.c:717
    inc si                                    ; 46                          ; 0xc112e vgabios.c:718
    jmp short 010dbh                          ; eb aa                       ; 0xc112f
    mov dx, 003dah                            ; ba da 03                    ; 0xc1131 vgabios.c:719
    in AL, DX                                 ; ec                          ; 0xc1134
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1135
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc1137 vgabios.c:720
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1139
    out DX, AL                                ; ee                          ; 0xc113c
    mov dx, 003dah                            ; ba da 03                    ; 0xc113d vgabios.c:722
    in AL, DX                                 ; ec                          ; 0xc1140
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1141
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1143 vgabios.c:724
    pop di                                    ; 5f                          ; 0xc1146
    pop si                                    ; 5e                          ; 0xc1147
    pop cx                                    ; 59                          ; 0xc1148
    pop bx                                    ; 5b                          ; 0xc1149
    pop bp                                    ; 5d                          ; 0xc114a
    retn                                      ; c3                          ; 0xc114b
  ; disGetNextSymbol 0xc114c LB 0x316d -> off=0x0 cb=00000000000000f6 uValue=00000000000c114c 'biosfn_set_cursor_shape'
biosfn_set_cursor_shape:                     ; 0xc114c LB 0xf6
    push bp                                   ; 55                          ; 0xc114c vgabios.c:727
    mov bp, sp                                ; 89 e5                       ; 0xc114d
    push bx                                   ; 53                          ; 0xc114f
    push cx                                   ; 51                          ; 0xc1150
    push si                                   ; 56                          ; 0xc1151
    push di                                   ; 57                          ; 0xc1152
    push ax                                   ; 50                          ; 0xc1153
    mov bl, al                                ; 88 c3                       ; 0xc1154
    mov ah, dl                                ; 88 d4                       ; 0xc1156
    movzx cx, al                              ; 0f b6 c8                    ; 0xc1158 vgabios.c:733
    sal cx, 008h                              ; c1 e1 08                    ; 0xc115b
    movzx dx, ah                              ; 0f b6 d4                    ; 0xc115e
    add dx, cx                                ; 01 ca                       ; 0xc1161
    mov si, strict word 00060h                ; be 60 00                    ; 0xc1163 vgabios.c:62
    mov cx, strict word 00040h                ; b9 40 00                    ; 0xc1166
    mov es, cx                                ; 8e c1                       ; 0xc1169
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc116b
    mov si, 00087h                            ; be 87 00                    ; 0xc116e vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc1171
    test dl, 008h                             ; f6 c2 08                    ; 0xc1174 vgabios.c:48
    jne near 01217h                           ; 0f 85 9c 00                 ; 0xc1177
    mov dl, al                                ; 88 c2                       ; 0xc117b vgabios.c:739
    and dl, 060h                              ; 80 e2 60                    ; 0xc117d
    cmp dl, 020h                              ; 80 fa 20                    ; 0xc1180
    jne short 0118ch                          ; 75 07                       ; 0xc1183
    mov BL, strict byte 01eh                  ; b3 1e                       ; 0xc1185 vgabios.c:741
    xor ah, ah                                ; 30 e4                       ; 0xc1187 vgabios.c:742
    jmp near 01217h                           ; e9 8b 00                    ; 0xc1189 vgabios.c:743
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc118c vgabios.c:47
    test dl, 001h                             ; f6 c2 01                    ; 0xc118f vgabios.c:48
    jne near 01217h                           ; 0f 85 81 00                 ; 0xc1192
    cmp bl, 020h                              ; 80 fb 20                    ; 0xc1196
    jnc near 01217h                           ; 0f 83 7a 00                 ; 0xc1199
    cmp ah, 020h                              ; 80 fc 20                    ; 0xc119d
    jnc near 01217h                           ; 0f 83 73 00                 ; 0xc11a0
    mov si, 00085h                            ; be 85 00                    ; 0xc11a4 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc11a7
    mov es, dx                                ; 8e c2                       ; 0xc11aa
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc11ac
    mov dx, cx                                ; 89 ca                       ; 0xc11af vgabios.c:58
    cmp ah, bl                                ; 38 dc                       ; 0xc11b1 vgabios.c:754
    jnc short 011c1h                          ; 73 0c                       ; 0xc11b3
    test ah, ah                               ; 84 e4                       ; 0xc11b5 vgabios.c:756
    je short 01217h                           ; 74 5e                       ; 0xc11b7
    xor bl, bl                                ; 30 db                       ; 0xc11b9 vgabios.c:757
    mov ah, cl                                ; 88 cc                       ; 0xc11bb vgabios.c:758
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc11bd
    jmp short 01217h                          ; eb 56                       ; 0xc11bf vgabios.c:760
    movzx si, ah                              ; 0f b6 f4                    ; 0xc11c1 vgabios.c:761
    mov word [bp-00ah], si                    ; 89 76 f6                    ; 0xc11c4
    movzx si, bl                              ; 0f b6 f3                    ; 0xc11c7
    or si, word [bp-00ah]                     ; 0b 76 f6                    ; 0xc11ca
    cmp si, cx                                ; 39 ce                       ; 0xc11cd
    jnc short 011e4h                          ; 73 13                       ; 0xc11cf
    movzx di, ah                              ; 0f b6 fc                    ; 0xc11d1
    mov si, cx                                ; 89 ce                       ; 0xc11d4
    dec si                                    ; 4e                          ; 0xc11d6
    cmp di, si                                ; 39 f7                       ; 0xc11d7
    je short 01217h                           ; 74 3c                       ; 0xc11d9
    movzx si, bl                              ; 0f b6 f3                    ; 0xc11db
    dec cx                                    ; 49                          ; 0xc11de
    dec cx                                    ; 49                          ; 0xc11df
    cmp si, cx                                ; 39 ce                       ; 0xc11e0
    je short 01217h                           ; 74 33                       ; 0xc11e2
    cmp ah, 003h                              ; 80 fc 03                    ; 0xc11e4 vgabios.c:763
    jbe short 01217h                          ; 76 2e                       ; 0xc11e7
    movzx si, bl                              ; 0f b6 f3                    ; 0xc11e9 vgabios.c:764
    movzx di, ah                              ; 0f b6 fc                    ; 0xc11ec
    inc si                                    ; 46                          ; 0xc11ef
    inc si                                    ; 46                          ; 0xc11f0
    mov cl, dl                                ; 88 d1                       ; 0xc11f1
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc11f3
    cmp di, si                                ; 39 f7                       ; 0xc11f5
    jnle short 0120ch                         ; 7f 13                       ; 0xc11f7
    sub bl, ah                                ; 28 e3                       ; 0xc11f9 vgabios.c:766
    add bl, dl                                ; 00 d3                       ; 0xc11fb
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc11fd
    mov ah, cl                                ; 88 cc                       ; 0xc11ff vgabios.c:767
    cmp dx, strict byte 0000eh                ; 83 fa 0e                    ; 0xc1201 vgabios.c:768
    jc short 01217h                           ; 72 11                       ; 0xc1204
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc1206 vgabios.c:770
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc1208 vgabios.c:771
    jmp short 01217h                          ; eb 0b                       ; 0xc120a vgabios.c:773
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc120c
    jbe short 01215h                          ; 76 04                       ; 0xc120f
    shr dx, 1                                 ; d1 ea                       ; 0xc1211 vgabios.c:775
    mov bl, dl                                ; 88 d3                       ; 0xc1213
    mov ah, cl                                ; 88 cc                       ; 0xc1215 vgabios.c:779
    mov si, strict word 00063h                ; be 63 00                    ; 0xc1217 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc121a
    mov es, dx                                ; 8e c2                       ; 0xc121d
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc121f
    mov AL, strict byte 00ah                  ; b0 0a                       ; 0xc1222 vgabios.c:790
    mov dx, cx                                ; 89 ca                       ; 0xc1224
    out DX, AL                                ; ee                          ; 0xc1226
    mov si, cx                                ; 89 ce                       ; 0xc1227 vgabios.c:791
    inc si                                    ; 46                          ; 0xc1229
    mov al, bl                                ; 88 d8                       ; 0xc122a
    mov dx, si                                ; 89 f2                       ; 0xc122c
    out DX, AL                                ; ee                          ; 0xc122e
    mov AL, strict byte 00bh                  ; b0 0b                       ; 0xc122f vgabios.c:792
    mov dx, cx                                ; 89 ca                       ; 0xc1231
    out DX, AL                                ; ee                          ; 0xc1233
    mov al, ah                                ; 88 e0                       ; 0xc1234 vgabios.c:793
    mov dx, si                                ; 89 f2                       ; 0xc1236
    out DX, AL                                ; ee                          ; 0xc1238
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1239 vgabios.c:794
    pop di                                    ; 5f                          ; 0xc123c
    pop si                                    ; 5e                          ; 0xc123d
    pop cx                                    ; 59                          ; 0xc123e
    pop bx                                    ; 5b                          ; 0xc123f
    pop bp                                    ; 5d                          ; 0xc1240
    retn                                      ; c3                          ; 0xc1241
  ; disGetNextSymbol 0xc1242 LB 0x3077 -> off=0x0 cb=0000000000000089 uValue=00000000000c1242 'biosfn_set_cursor_pos'
biosfn_set_cursor_pos:                       ; 0xc1242 LB 0x89
    push bp                                   ; 55                          ; 0xc1242 vgabios.c:797
    mov bp, sp                                ; 89 e5                       ; 0xc1243
    push bx                                   ; 53                          ; 0xc1245
    push cx                                   ; 51                          ; 0xc1246
    push si                                   ; 56                          ; 0xc1247
    push ax                                   ; 50                          ; 0xc1248
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc1249 vgabios.c:803
    jnbe short 012c3h                         ; 77 76                       ; 0xc124b
    movzx bx, al                              ; 0f b6 d8                    ; 0xc124d vgabios.c:806
    add bx, bx                                ; 01 db                       ; 0xc1250
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc1252
    mov cx, strict word 00040h                ; b9 40 00                    ; 0xc1255 vgabios.c:62
    mov es, cx                                ; 8e c1                       ; 0xc1258
    mov word [es:bx], dx                      ; 26 89 17                    ; 0xc125a
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc125d vgabios.c:47
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc1260
    cmp al, ah                                ; 38 e0                       ; 0xc1263 vgabios.c:810
    jne short 012c3h                          ; 75 5c                       ; 0xc1265
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1267 vgabios.c:57
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc126a
    mov bx, 00084h                            ; bb 84 00                    ; 0xc126d vgabios.c:47
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc1270
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc1273 vgabios.c:48
    inc bx                                    ; 43                          ; 0xc1276
    mov si, dx                                ; 89 d6                       ; 0xc1277 vgabios.c:816
    and si, 0ff00h                            ; 81 e6 00 ff                 ; 0xc1279
    shr si, 008h                              ; c1 ee 08                    ; 0xc127d
    mov word [bp-008h], si                    ; 89 76 f8                    ; 0xc1280
    imul bx, cx                               ; 0f af d9                    ; 0xc1283 vgabios.c:819
    or bl, 0ffh                               ; 80 cb ff                    ; 0xc1286
    xor ah, ah                                ; 30 e4                       ; 0xc1289
    inc bx                                    ; 43                          ; 0xc128b
    imul ax, bx                               ; 0f af c3                    ; 0xc128c
    movzx si, dl                              ; 0f b6 f2                    ; 0xc128f
    add si, ax                                ; 01 c6                       ; 0xc1292
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1294
    imul ax, cx                               ; 0f af c1                    ; 0xc1298
    add si, ax                                ; 01 c6                       ; 0xc129b
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc129d vgabios.c:57
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc12a0
    mov AL, strict byte 00eh                  ; b0 0e                       ; 0xc12a3 vgabios.c:823
    mov dx, bx                                ; 89 da                       ; 0xc12a5
    out DX, AL                                ; ee                          ; 0xc12a7
    mov ax, si                                ; 89 f0                       ; 0xc12a8 vgabios.c:824
    xor al, al                                ; 30 c0                       ; 0xc12aa
    shr ax, 008h                              ; c1 e8 08                    ; 0xc12ac
    lea cx, [bx+001h]                         ; 8d 4f 01                    ; 0xc12af
    mov dx, cx                                ; 89 ca                       ; 0xc12b2
    out DX, AL                                ; ee                          ; 0xc12b4
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc12b5 vgabios.c:825
    mov dx, bx                                ; 89 da                       ; 0xc12b7
    out DX, AL                                ; ee                          ; 0xc12b9
    and si, 000ffh                            ; 81 e6 ff 00                 ; 0xc12ba vgabios.c:826
    mov ax, si                                ; 89 f0                       ; 0xc12be
    mov dx, cx                                ; 89 ca                       ; 0xc12c0
    out DX, AL                                ; ee                          ; 0xc12c2
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc12c3 vgabios.c:828
    pop si                                    ; 5e                          ; 0xc12c6
    pop cx                                    ; 59                          ; 0xc12c7
    pop bx                                    ; 5b                          ; 0xc12c8
    pop bp                                    ; 5d                          ; 0xc12c9
    retn                                      ; c3                          ; 0xc12ca
  ; disGetNextSymbol 0xc12cb LB 0x2fee -> off=0x0 cb=00000000000000cd uValue=00000000000c12cb 'biosfn_set_active_page'
biosfn_set_active_page:                      ; 0xc12cb LB 0xcd
    push bp                                   ; 55                          ; 0xc12cb vgabios.c:831
    mov bp, sp                                ; 89 e5                       ; 0xc12cc
    push bx                                   ; 53                          ; 0xc12ce
    push cx                                   ; 51                          ; 0xc12cf
    push dx                                   ; 52                          ; 0xc12d0
    push si                                   ; 56                          ; 0xc12d1
    push di                                   ; 57                          ; 0xc12d2
    push ax                                   ; 50                          ; 0xc12d3
    push ax                                   ; 50                          ; 0xc12d4
    mov cl, al                                ; 88 c1                       ; 0xc12d5
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc12d7 vgabios.c:837
    jnbe near 0138eh                          ; 0f 87 b1 00                 ; 0xc12d9
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc12dd vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc12e0
    mov es, ax                                ; 8e c0                       ; 0xc12e3
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc12e5
    xor ah, ah                                ; 30 e4                       ; 0xc12e8 vgabios.c:841
    call 035edh                               ; e8 00 23                    ; 0xc12ea
    mov ch, al                                ; 88 c5                       ; 0xc12ed
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc12ef vgabios.c:842
    je near 0138eh                            ; 0f 84 99 00                 ; 0xc12f1
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc12f5 vgabios.c:845
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc12f8
    lea dx, [bp-00ch]                         ; 8d 56 f4                    ; 0xc12fb
    call 00a93h                               ; e8 92 f7                    ; 0xc12fe
    movzx bx, ch                              ; 0f b6 dd                    ; 0xc1301 vgabios.c:847
    mov si, bx                                ; 89 de                       ; 0xc1304
    sal si, 003h                              ; c1 e6 03                    ; 0xc1306
    cmp byte [si+047b2h], 000h                ; 80 bc b2 47 00              ; 0xc1309
    jne short 01344h                          ; 75 34                       ; 0xc130e
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1310 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1313
    mov es, ax                                ; 8e c0                       ; 0xc1316
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc1318
    mov bx, 00084h                            ; bb 84 00                    ; 0xc131b vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc131e
    xor ah, ah                                ; 30 e4                       ; 0xc1321 vgabios.c:48
    inc ax                                    ; 40                          ; 0xc1323
    imul dx, ax                               ; 0f af d0                    ; 0xc1324 vgabios.c:854
    mov ax, dx                                ; 89 d0                       ; 0xc1327
    add ax, dx                                ; 01 d0                       ; 0xc1329
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc132b
    mov bx, ax                                ; 89 c3                       ; 0xc132d
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc132f
    inc bx                                    ; 43                          ; 0xc1332
    imul bx, ax                               ; 0f af d8                    ; 0xc1333
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc1336 vgabios.c:62
    mov word [es:si], bx                      ; 26 89 1c                    ; 0xc1339
    or dl, 0ffh                               ; 80 ca ff                    ; 0xc133c vgabios.c:858
    mov bx, dx                                ; 89 d3                       ; 0xc133f
    inc bx                                    ; 43                          ; 0xc1341
    jmp short 01353h                          ; eb 0f                       ; 0xc1342 vgabios.c:860
    movzx bx, byte [bx+04831h]                ; 0f b6 9f 31 48              ; 0xc1344 vgabios.c:862
    sal bx, 006h                              ; c1 e3 06                    ; 0xc1349
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc134c
    mov bx, word [bx+04848h]                  ; 8b 9f 48 48                 ; 0xc134f
    imul bx, ax                               ; 0f af d8                    ; 0xc1353
    mov si, strict word 00063h                ; be 63 00                    ; 0xc1356 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1359
    mov es, ax                                ; 8e c0                       ; 0xc135c
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc135e
    mov AL, strict byte 00ch                  ; b0 0c                       ; 0xc1361 vgabios.c:867
    mov dx, si                                ; 89 f2                       ; 0xc1363
    out DX, AL                                ; ee                          ; 0xc1365
    mov ax, bx                                ; 89 d8                       ; 0xc1366 vgabios.c:868
    xor al, bl                                ; 30 d8                       ; 0xc1368
    shr ax, 008h                              ; c1 e8 08                    ; 0xc136a
    lea di, [si+001h]                         ; 8d 7c 01                    ; 0xc136d
    mov dx, di                                ; 89 fa                       ; 0xc1370
    out DX, AL                                ; ee                          ; 0xc1372
    mov AL, strict byte 00dh                  ; b0 0d                       ; 0xc1373 vgabios.c:869
    mov dx, si                                ; 89 f2                       ; 0xc1375
    out DX, AL                                ; ee                          ; 0xc1377
    xor bh, bh                                ; 30 ff                       ; 0xc1378 vgabios.c:870
    mov ax, bx                                ; 89 d8                       ; 0xc137a
    mov dx, di                                ; 89 fa                       ; 0xc137c
    out DX, AL                                ; ee                          ; 0xc137e
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc137f vgabios.c:52
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc1382
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc1385 vgabios.c:880
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc1388
    call 01242h                               ; e8 b4 fe                    ; 0xc138b
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc138e vgabios.c:881
    pop di                                    ; 5f                          ; 0xc1391
    pop si                                    ; 5e                          ; 0xc1392
    pop dx                                    ; 5a                          ; 0xc1393
    pop cx                                    ; 59                          ; 0xc1394
    pop bx                                    ; 5b                          ; 0xc1395
    pop bp                                    ; 5d                          ; 0xc1396
    retn                                      ; c3                          ; 0xc1397
  ; disGetNextSymbol 0xc1398 LB 0x2f21 -> off=0x0 cb=0000000000000045 uValue=00000000000c1398 'find_vpti'
find_vpti:                                   ; 0xc1398 LB 0x45
    push bx                                   ; 53                          ; 0xc1398 vgabios.c:916
    push si                                   ; 56                          ; 0xc1399
    push bp                                   ; 55                          ; 0xc139a
    mov bp, sp                                ; 89 e5                       ; 0xc139b
    movzx bx, al                              ; 0f b6 d8                    ; 0xc139d vgabios.c:921
    mov si, bx                                ; 89 de                       ; 0xc13a0
    sal si, 003h                              ; c1 e6 03                    ; 0xc13a2
    cmp byte [si+047b2h], 000h                ; 80 bc b2 47 00              ; 0xc13a5
    jne short 013d4h                          ; 75 28                       ; 0xc13aa
    mov si, 00089h                            ; be 89 00                    ; 0xc13ac vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc13af
    mov es, ax                                ; 8e c0                       ; 0xc13b2
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc13b4
    test AL, strict byte 010h                 ; a8 10                       ; 0xc13b7 vgabios.c:923
    je short 013c2h                           ; 74 07                       ; 0xc13b9
    movsx ax, byte [bx+07df8h]                ; 0f be 87 f8 7d              ; 0xc13bb vgabios.c:924
    jmp short 013d9h                          ; eb 17                       ; 0xc13c0 vgabios.c:925
    test AL, strict byte 080h                 ; a8 80                       ; 0xc13c2
    je short 013cdh                           ; 74 07                       ; 0xc13c4
    movsx ax, byte [bx+07de8h]                ; 0f be 87 e8 7d              ; 0xc13c6 vgabios.c:926
    jmp short 013d9h                          ; eb 0c                       ; 0xc13cb vgabios.c:927
    movsx ax, byte [bx+07df0h]                ; 0f be 87 f0 7d              ; 0xc13cd vgabios.c:928
    jmp short 013d9h                          ; eb 05                       ; 0xc13d2 vgabios.c:929
    movzx ax, byte [bx+04831h]                ; 0f b6 87 31 48              ; 0xc13d4 vgabios.c:930
    pop bp                                    ; 5d                          ; 0xc13d9 vgabios.c:933
    pop si                                    ; 5e                          ; 0xc13da
    pop bx                                    ; 5b                          ; 0xc13db
    retn                                      ; c3                          ; 0xc13dc
  ; disGetNextSymbol 0xc13dd LB 0x2edc -> off=0x0 cb=00000000000004b2 uValue=00000000000c13dd 'biosfn_set_video_mode'
biosfn_set_video_mode:                       ; 0xc13dd LB 0x4b2
    push bp                                   ; 55                          ; 0xc13dd vgabios.c:938
    mov bp, sp                                ; 89 e5                       ; 0xc13de
    push bx                                   ; 53                          ; 0xc13e0
    push cx                                   ; 51                          ; 0xc13e1
    push dx                                   ; 52                          ; 0xc13e2
    push si                                   ; 56                          ; 0xc13e3
    push di                                   ; 57                          ; 0xc13e4
    sub sp, strict byte 00016h                ; 83 ec 16                    ; 0xc13e5
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc13e8
    and AL, strict byte 080h                  ; 24 80                       ; 0xc13eb vgabios.c:942
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc13ed
    call 007bfh                               ; e8 cc f3                    ; 0xc13f0 vgabios.c:952
    test ax, ax                               ; 85 c0                       ; 0xc13f3
    je short 01403h                           ; 74 0c                       ; 0xc13f5
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc13f7 vgabios.c:954
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc13f9
    out DX, AL                                ; ee                          ; 0xc13fc
    xor al, al                                ; 30 c0                       ; 0xc13fd vgabios.c:955
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc13ff
    out DX, AL                                ; ee                          ; 0xc1402
    and byte [bp-00eh], 07fh                  ; 80 66 f2 7f                 ; 0xc1403 vgabios.c:960
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1407 vgabios.c:966
    call 035edh                               ; e8 df 21                    ; 0xc140b
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc140e
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1411 vgabios.c:972
    je near 01885h                            ; 0f 84 6e 04                 ; 0xc1413
    mov bx, 000a8h                            ; bb a8 00                    ; 0xc1417 vgabios.c:67
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc141a
    mov es, dx                                ; 8e c2                       ; 0xc141d
    mov di, word [es:bx]                      ; 26 8b 3f                    ; 0xc141f
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc1422
    mov bx, di                                ; 89 fb                       ; 0xc1426 vgabios.c:68
    mov word [bp-014h], dx                    ; 89 56 ec                    ; 0xc1428
    movzx cx, al                              ; 0f b6 c8                    ; 0xc142b vgabios.c:978
    mov ax, cx                                ; 89 c8                       ; 0xc142e
    call 01398h                               ; e8 65 ff                    ; 0xc1430
    mov es, dx                                ; 8e c2                       ; 0xc1433 vgabios.c:979
    mov si, word [es:di]                      ; 26 8b 35                    ; 0xc1435
    mov dx, word [es:di+002h]                 ; 26 8b 55 02                 ; 0xc1438
    mov word [bp-01ah], dx                    ; 89 56 e6                    ; 0xc143c
    xor ah, ah                                ; 30 e4                       ; 0xc143f vgabios.c:980
    sal ax, 006h                              ; c1 e0 06                    ; 0xc1441
    add si, ax                                ; 01 c6                       ; 0xc1444
    mov di, 00089h                            ; bf 89 00                    ; 0xc1446 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1449
    mov es, ax                                ; 8e c0                       ; 0xc144c
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc144e
    mov ah, al                                ; 88 c4                       ; 0xc1451 vgabios.c:48
    test AL, strict byte 008h                 ; a8 08                       ; 0xc1453 vgabios.c:997
    jne near 01509h                           ; 0f 85 b0 00                 ; 0xc1455
    mov di, cx                                ; 89 cf                       ; 0xc1459 vgabios.c:999
    sal di, 003h                              ; c1 e7 03                    ; 0xc145b
    mov al, byte [di+047b7h]                  ; 8a 85 b7 47                 ; 0xc145e
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc1462
    out DX, AL                                ; ee                          ; 0xc1465
    xor al, al                                ; 30 c0                       ; 0xc1466 vgabios.c:1002
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc1468
    out DX, AL                                ; ee                          ; 0xc146b
    mov cl, byte [di+047b8h]                  ; 8a 8d b8 47                 ; 0xc146c vgabios.c:1005
    cmp cl, 001h                              ; 80 f9 01                    ; 0xc1470
    jc short 01483h                           ; 72 0e                       ; 0xc1473
    jbe short 0148eh                          ; 76 17                       ; 0xc1475
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc1477
    je short 0149ch                           ; 74 20                       ; 0xc147a
    cmp cl, 002h                              ; 80 f9 02                    ; 0xc147c
    je short 01495h                           ; 74 14                       ; 0xc147f
    jmp short 014a1h                          ; eb 1e                       ; 0xc1481
    test cl, cl                               ; 84 c9                       ; 0xc1483
    jne short 014a1h                          ; 75 1a                       ; 0xc1485
    mov word [bp-016h], 04fc5h                ; c7 46 ea c5 4f              ; 0xc1487 vgabios.c:1007
    jmp short 014a1h                          ; eb 13                       ; 0xc148c vgabios.c:1008
    mov word [bp-016h], 05085h                ; c7 46 ea 85 50              ; 0xc148e vgabios.c:1010
    jmp short 014a1h                          ; eb 0c                       ; 0xc1493 vgabios.c:1011
    mov word [bp-016h], 05145h                ; c7 46 ea 45 51              ; 0xc1495 vgabios.c:1013
    jmp short 014a1h                          ; eb 05                       ; 0xc149a vgabios.c:1014
    mov word [bp-016h], 05205h                ; c7 46 ea 05 52              ; 0xc149c vgabios.c:1016
    movzx di, byte [bp-010h]                  ; 0f b6 7e f0                 ; 0xc14a1 vgabios.c:1020
    sal di, 003h                              ; c1 e7 03                    ; 0xc14a5
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc14a8
    jne short 014beh                          ; 75 0f                       ; 0xc14ad
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc14af vgabios.c:1022
    cmp byte [es:si+002h], 008h               ; 26 80 7c 02 08              ; 0xc14b2
    jne short 014beh                          ; 75 05                       ; 0xc14b7
    mov word [bp-016h], 05085h                ; c7 46 ea 85 50              ; 0xc14b9 vgabios.c:1023
    xor cx, cx                                ; 31 c9                       ; 0xc14be vgabios.c:1026
    jmp short 014d1h                          ; eb 0f                       ; 0xc14c0
    xor al, al                                ; 30 c0                       ; 0xc14c2 vgabios.c:1033
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc14c4
    out DX, AL                                ; ee                          ; 0xc14c7
    out DX, AL                                ; ee                          ; 0xc14c8 vgabios.c:1034
    out DX, AL                                ; ee                          ; 0xc14c9 vgabios.c:1035
    inc cx                                    ; 41                          ; 0xc14ca vgabios.c:1037
    cmp cx, 00100h                            ; 81 f9 00 01                 ; 0xc14cb
    jnc short 014fch                          ; 73 2b                       ; 0xc14cf
    movzx di, byte [bp-010h]                  ; 0f b6 7e f0                 ; 0xc14d1
    sal di, 003h                              ; c1 e7 03                    ; 0xc14d5
    movzx di, byte [di+047b8h]                ; 0f b6 bd b8 47              ; 0xc14d8
    movzx di, byte [di+04841h]                ; 0f b6 bd 41 48              ; 0xc14dd
    cmp cx, di                                ; 39 f9                       ; 0xc14e2
    jnbe short 014c2h                         ; 77 dc                       ; 0xc14e4
    imul di, cx, strict byte 00003h           ; 6b f9 03                    ; 0xc14e6
    add di, word [bp-016h]                    ; 03 7e ea                    ; 0xc14e9
    mov al, byte [di]                         ; 8a 05                       ; 0xc14ec
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc14ee
    out DX, AL                                ; ee                          ; 0xc14f1
    mov al, byte [di+001h]                    ; 8a 45 01                    ; 0xc14f2
    out DX, AL                                ; ee                          ; 0xc14f5
    mov al, byte [di+002h]                    ; 8a 45 02                    ; 0xc14f6
    out DX, AL                                ; ee                          ; 0xc14f9
    jmp short 014cah                          ; eb ce                       ; 0xc14fa
    test ah, 002h                             ; f6 c4 02                    ; 0xc14fc vgabios.c:1038
    je short 01509h                           ; 74 08                       ; 0xc14ff
    mov dx, 00100h                            ; ba 00 01                    ; 0xc1501 vgabios.c:1040
    xor ax, ax                                ; 31 c0                       ; 0xc1504
    call 010c0h                               ; e8 b7 fb                    ; 0xc1506
    mov dx, 003dah                            ; ba da 03                    ; 0xc1509 vgabios.c:1045
    in AL, DX                                 ; ec                          ; 0xc150c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc150d
    xor cx, cx                                ; 31 c9                       ; 0xc150f vgabios.c:1048
    jmp short 01518h                          ; eb 05                       ; 0xc1511
    cmp cx, strict byte 00013h                ; 83 f9 13                    ; 0xc1513
    jnbe short 0152dh                         ; 77 15                       ; 0xc1516
    mov al, cl                                ; 88 c8                       ; 0xc1518 vgabios.c:1049
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc151a
    out DX, AL                                ; ee                          ; 0xc151d
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc151e vgabios.c:1050
    mov di, si                                ; 89 f7                       ; 0xc1521
    add di, cx                                ; 01 cf                       ; 0xc1523
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc1525
    out DX, AL                                ; ee                          ; 0xc1529
    inc cx                                    ; 41                          ; 0xc152a vgabios.c:1051
    jmp short 01513h                          ; eb e6                       ; 0xc152b
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc152d vgabios.c:1052
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc152f
    out DX, AL                                ; ee                          ; 0xc1532
    xor al, al                                ; 30 c0                       ; 0xc1533 vgabios.c:1053
    out DX, AL                                ; ee                          ; 0xc1535
    mov es, [bp-014h]                         ; 8e 46 ec                    ; 0xc1536 vgabios.c:1056
    mov dx, word [es:bx+004h]                 ; 26 8b 57 04                 ; 0xc1539
    mov ax, word [es:bx+006h]                 ; 26 8b 47 06                 ; 0xc153d
    test ax, ax                               ; 85 c0                       ; 0xc1541
    jne short 01549h                          ; 75 04                       ; 0xc1543
    test dx, dx                               ; 85 d2                       ; 0xc1545
    je short 01589h                           ; 74 40                       ; 0xc1547
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1549 vgabios.c:1060
    xor cx, cx                                ; 31 c9                       ; 0xc154c vgabios.c:1061
    jmp short 01555h                          ; eb 05                       ; 0xc154e
    cmp cx, strict byte 00010h                ; 83 f9 10                    ; 0xc1550
    jnc short 01579h                          ; 73 24                       ; 0xc1553
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1555 vgabios.c:1062
    mov di, si                                ; 89 f7                       ; 0xc1558
    add di, cx                                ; 01 cf                       ; 0xc155a
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc155c
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc155f
    mov ax, dx                                ; 89 d0                       ; 0xc1562
    add ax, cx                                ; 01 c8                       ; 0xc1564
    mov word [bp-01eh], ax                    ; 89 46 e2                    ; 0xc1566
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc1569
    mov es, [bp-020h]                         ; 8e 46 e0                    ; 0xc156d
    mov di, word [bp-01eh]                    ; 8b 7e e2                    ; 0xc1570
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1573
    inc cx                                    ; 41                          ; 0xc1576
    jmp short 01550h                          ; eb d7                       ; 0xc1577
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1579 vgabios.c:1063
    mov al, byte [es:si+034h]                 ; 26 8a 44 34                 ; 0xc157c
    mov es, [bp-01ch]                         ; 8e 46 e4                    ; 0xc1580
    mov di, dx                                ; 89 d7                       ; 0xc1583
    mov byte [es:di+010h], al                 ; 26 88 45 10                 ; 0xc1585
    xor al, al                                ; 30 c0                       ; 0xc1589 vgabios.c:1067
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc158b
    out DX, AL                                ; ee                          ; 0xc158e
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc158f vgabios.c:1068
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc1591
    out DX, AL                                ; ee                          ; 0xc1594
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc1595 vgabios.c:1069
    jmp short 0159fh                          ; eb 05                       ; 0xc1598
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc159a
    jnbe short 015b7h                         ; 77 18                       ; 0xc159d
    mov al, cl                                ; 88 c8                       ; 0xc159f vgabios.c:1070
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc15a1
    out DX, AL                                ; ee                          ; 0xc15a4
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc15a5 vgabios.c:1071
    mov di, si                                ; 89 f7                       ; 0xc15a8
    add di, cx                                ; 01 cf                       ; 0xc15aa
    mov al, byte [es:di+004h]                 ; 26 8a 45 04                 ; 0xc15ac
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc15b0
    out DX, AL                                ; ee                          ; 0xc15b3
    inc cx                                    ; 41                          ; 0xc15b4 vgabios.c:1072
    jmp short 0159ah                          ; eb e3                       ; 0xc15b5
    xor cx, cx                                ; 31 c9                       ; 0xc15b7 vgabios.c:1075
    jmp short 015c0h                          ; eb 05                       ; 0xc15b9
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc15bb
    jnbe short 015d8h                         ; 77 18                       ; 0xc15be
    mov al, cl                                ; 88 c8                       ; 0xc15c0 vgabios.c:1076
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc15c2
    out DX, AL                                ; ee                          ; 0xc15c5
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc15c6 vgabios.c:1077
    mov di, si                                ; 89 f7                       ; 0xc15c9
    add di, cx                                ; 01 cf                       ; 0xc15cb
    mov al, byte [es:di+037h]                 ; 26 8a 45 37                 ; 0xc15cd
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc15d1
    out DX, AL                                ; ee                          ; 0xc15d4
    inc cx                                    ; 41                          ; 0xc15d5 vgabios.c:1078
    jmp short 015bbh                          ; eb e3                       ; 0xc15d6
    movzx di, byte [bp-010h]                  ; 0f b6 7e f0                 ; 0xc15d8 vgabios.c:1081
    sal di, 003h                              ; c1 e7 03                    ; 0xc15dc
    cmp byte [di+047b3h], 001h                ; 80 bd b3 47 01              ; 0xc15df
    jne short 015ebh                          ; 75 05                       ; 0xc15e4
    mov cx, 003b4h                            ; b9 b4 03                    ; 0xc15e6
    jmp short 015eeh                          ; eb 03                       ; 0xc15e9
    mov cx, 003d4h                            ; b9 d4 03                    ; 0xc15eb
    mov word [bp-018h], cx                    ; 89 4e e8                    ; 0xc15ee
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc15f1 vgabios.c:1084
    mov al, byte [es:si+009h]                 ; 26 8a 44 09                 ; 0xc15f4
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc15f8
    out DX, AL                                ; ee                          ; 0xc15fb
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc15fc vgabios.c:1087
    mov dx, cx                                ; 89 ca                       ; 0xc15ff
    out DX, ax                                ; ef                          ; 0xc1601
    xor cx, cx                                ; 31 c9                       ; 0xc1602 vgabios.c:1089
    jmp short 0160bh                          ; eb 05                       ; 0xc1604
    cmp cx, strict byte 00018h                ; 83 f9 18                    ; 0xc1606
    jnbe short 01621h                         ; 77 16                       ; 0xc1609
    mov al, cl                                ; 88 c8                       ; 0xc160b vgabios.c:1090
    mov dx, word [bp-018h]                    ; 8b 56 e8                    ; 0xc160d
    out DX, AL                                ; ee                          ; 0xc1610
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1611 vgabios.c:1091
    mov di, si                                ; 89 f7                       ; 0xc1614
    add di, cx                                ; 01 cf                       ; 0xc1616
    inc dx                                    ; 42                          ; 0xc1618
    mov al, byte [es:di+00ah]                 ; 26 8a 45 0a                 ; 0xc1619
    out DX, AL                                ; ee                          ; 0xc161d
    inc cx                                    ; 41                          ; 0xc161e vgabios.c:1092
    jmp short 01606h                          ; eb e5                       ; 0xc161f
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc1621 vgabios.c:1095
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1623
    out DX, AL                                ; ee                          ; 0xc1626
    mov dx, word [bp-018h]                    ; 8b 56 e8                    ; 0xc1627 vgabios.c:1096
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc162a
    in AL, DX                                 ; ec                          ; 0xc162d
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc162e
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1630 vgabios.c:1098
    jne short 01692h                          ; 75 5c                       ; 0xc1634
    movzx di, byte [bp-010h]                  ; 0f b6 7e f0                 ; 0xc1636 vgabios.c:1100
    sal di, 003h                              ; c1 e7 03                    ; 0xc163a
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc163d
    jne short 01656h                          ; 75 12                       ; 0xc1642
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc1644 vgabios.c:1102
    mov cx, 04000h                            ; b9 00 40                    ; 0xc1648
    mov ax, 00720h                            ; b8 20 07                    ; 0xc164b
    xor di, di                                ; 31 ff                       ; 0xc164e
    jcxz 01654h                               ; e3 02                       ; 0xc1650
    rep stosw                                 ; f3 ab                       ; 0xc1652
    jmp short 01692h                          ; eb 3c                       ; 0xc1654 vgabios.c:1104
    cmp byte [bp-00eh], 00dh                  ; 80 7e f2 0d                 ; 0xc1656 vgabios.c:1106
    jnc short 0166dh                          ; 73 11                       ; 0xc165a
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc165c vgabios.c:1108
    mov cx, 04000h                            ; b9 00 40                    ; 0xc1660
    xor ax, ax                                ; 31 c0                       ; 0xc1663
    xor di, di                                ; 31 ff                       ; 0xc1665
    jcxz 0166bh                               ; e3 02                       ; 0xc1667
    rep stosw                                 ; f3 ab                       ; 0xc1669
    jmp short 01692h                          ; eb 25                       ; 0xc166b vgabios.c:1110
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc166d vgabios.c:1112
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc166f
    out DX, AL                                ; ee                          ; 0xc1672
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc1673 vgabios.c:1113
    in AL, DX                                 ; ec                          ; 0xc1676
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1677
    mov word [bp-01eh], ax                    ; 89 46 e2                    ; 0xc1679
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc167c vgabios.c:1114
    out DX, AL                                ; ee                          ; 0xc167e
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc167f vgabios.c:1115
    mov cx, 08000h                            ; b9 00 80                    ; 0xc1683
    xor ax, ax                                ; 31 c0                       ; 0xc1686
    xor di, di                                ; 31 ff                       ; 0xc1688
    jcxz 0168eh                               ; e3 02                       ; 0xc168a
    rep stosw                                 ; f3 ab                       ; 0xc168c
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc168e vgabios.c:1116
    out DX, AL                                ; ee                          ; 0xc1691
    mov di, strict word 00049h                ; bf 49 00                    ; 0xc1692 vgabios.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1695
    mov es, ax                                ; 8e c0                       ; 0xc1698
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc169a
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc169d
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc16a0 vgabios.c:1123
    movzx ax, byte [es:si]                    ; 26 0f b6 04                 ; 0xc16a3
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc16a7 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc16aa
    mov es, dx                                ; 8e c2                       ; 0xc16ad
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16af
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc16b2 vgabios.c:60
    mov ax, word [es:si+003h]                 ; 26 8b 44 03                 ; 0xc16b5
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc16b9 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc16bc
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16be
    mov di, strict word 00063h                ; bf 63 00                    ; 0xc16c1 vgabios.c:62
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc16c4
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16c7
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc16ca vgabios.c:50
    mov al, byte [es:si+001h]                 ; 26 8a 44 01                 ; 0xc16cd
    mov di, 00084h                            ; bf 84 00                    ; 0xc16d1 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc16d4
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc16d6
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc16d9 vgabios.c:1127
    movzx ax, byte [es:si+002h]               ; 26 0f b6 44 02              ; 0xc16dc
    mov di, 00085h                            ; bf 85 00                    ; 0xc16e1 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc16e4
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc16e6
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc16e9 vgabios.c:1128
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc16ec
    mov di, 00087h                            ; bf 87 00                    ; 0xc16ee vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc16f1
    mov di, 00088h                            ; bf 88 00                    ; 0xc16f4 vgabios.c:52
    mov byte [es:di], 0f9h                    ; 26 c6 05 f9                 ; 0xc16f7
    mov di, 0008ah                            ; bf 8a 00                    ; 0xc16fb vgabios.c:52
    mov byte [es:di], 008h                    ; 26 c6 05 08                 ; 0xc16fe
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc1702 vgabios.c:1134
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc1705
    jnbe short 0172fh                         ; 77 26                       ; 0xc1707
    movzx di, al                              ; 0f b6 f8                    ; 0xc1709 vgabios.c:1136
    mov al, byte [di+07de0h]                  ; 8a 85 e0 7d                 ; 0xc170c vgabios.c:50
    mov di, strict word 00065h                ; bf 65 00                    ; 0xc1710 vgabios.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc1713
    cmp byte [bp-00eh], 006h                  ; 80 7e f2 06                 ; 0xc1716 vgabios.c:1137
    jne short 01721h                          ; 75 05                       ; 0xc171a
    mov dx, strict word 0003fh                ; ba 3f 00                    ; 0xc171c
    jmp short 01724h                          ; eb 03                       ; 0xc171f
    mov dx, strict word 00030h                ; ba 30 00                    ; 0xc1721
    mov di, strict word 00066h                ; bf 66 00                    ; 0xc1724 vgabios.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1727
    mov es, ax                                ; 8e c0                       ; 0xc172a
    mov byte [es:di], dl                      ; 26 88 15                    ; 0xc172c
    movzx di, byte [bp-010h]                  ; 0f b6 7e f0                 ; 0xc172f vgabios.c:1141
    sal di, 003h                              ; c1 e7 03                    ; 0xc1733
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc1736
    jne short 01746h                          ; 75 09                       ; 0xc173b
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc173d vgabios.c:1143
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc1740
    call 0114ch                               ; e8 06 fa                    ; 0xc1743
    xor cx, cx                                ; 31 c9                       ; 0xc1746 vgabios.c:1148
    jmp short 0174fh                          ; eb 05                       ; 0xc1748
    cmp cx, strict byte 00008h                ; 83 f9 08                    ; 0xc174a
    jnc short 0175ah                          ; 73 0b                       ; 0xc174d
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc174f vgabios.c:1149
    xor dx, dx                                ; 31 d2                       ; 0xc1752
    call 01242h                               ; e8 eb fa                    ; 0xc1754
    inc cx                                    ; 41                          ; 0xc1757
    jmp short 0174ah                          ; eb f0                       ; 0xc1758
    xor ax, ax                                ; 31 c0                       ; 0xc175a vgabios.c:1152
    call 012cbh                               ; e8 6c fb                    ; 0xc175c
    movzx di, byte [bp-010h]                  ; 0f b6 7e f0                 ; 0xc175f vgabios.c:1155
    sal di, 003h                              ; c1 e7 03                    ; 0xc1763
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc1766
    jne near 01850h                           ; 0f 85 e1 00                 ; 0xc176b
    mov es, [bp-014h]                         ; 8e 46 ec                    ; 0xc176f vgabios.c:1157
    mov di, word [es:bx+008h]                 ; 26 8b 7f 08                 ; 0xc1772
    mov ax, word [es:bx+00ah]                 ; 26 8b 47 0a                 ; 0xc1776
    mov word [bp-012h], ax                    ; 89 46 ee                    ; 0xc177a
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc177d vgabios.c:1159
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc1780
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc1784
    je short 017a8h                           ; 74 20                       ; 0xc1786
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc1788
    jne short 017d2h                          ; 75 46                       ; 0xc178a
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc178c vgabios.c:1161
    movzx ax, byte [es:si+002h]               ; 26 0f b6 44 02              ; 0xc178f
    push ax                                   ; 50                          ; 0xc1794
    push dword 000000000h                     ; 66 6a 00                    ; 0xc1795
    mov cx, 00100h                            ; b9 00 01                    ; 0xc1798
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc179b
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc179e
    xor ax, ax                                ; 31 c0                       ; 0xc17a1
    call 02c0ah                               ; e8 64 14                    ; 0xc17a3
    jmp short 017f4h                          ; eb 4c                       ; 0xc17a6 vgabios.c:1162
    xor ah, ah                                ; 30 e4                       ; 0xc17a8 vgabios.c:1164
    push ax                                   ; 50                          ; 0xc17aa
    push dword 000000000h                     ; 66 6a 00                    ; 0xc17ab
    mov cx, 00100h                            ; b9 00 01                    ; 0xc17ae
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc17b1
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc17b4
    xor al, al                                ; 30 c0                       ; 0xc17b7
    call 02c0ah                               ; e8 4e 14                    ; 0xc17b9
    cmp byte [bp-00eh], 007h                  ; 80 7e f2 07                 ; 0xc17bc vgabios.c:1165
    jne short 017f4h                          ; 75 32                       ; 0xc17c0
    mov cx, strict word 0000eh                ; b9 0e 00                    ; 0xc17c2 vgabios.c:1166
    xor bx, bx                                ; 31 db                       ; 0xc17c5
    mov dx, 07b6fh                            ; ba 6f 7b                    ; 0xc17c7
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc17ca
    call 02b95h                               ; e8 c5 13                    ; 0xc17cd
    jmp short 017f4h                          ; eb 22                       ; 0xc17d0 vgabios.c:1167
    xor ah, ah                                ; 30 e4                       ; 0xc17d2 vgabios.c:1169
    push ax                                   ; 50                          ; 0xc17d4
    push dword 000000000h                     ; 66 6a 00                    ; 0xc17d5
    mov cx, 00100h                            ; b9 00 01                    ; 0xc17d8
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc17db
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc17de
    xor al, al                                ; 30 c0                       ; 0xc17e1
    call 02c0ah                               ; e8 24 14                    ; 0xc17e3
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc17e6 vgabios.c:1170
    xor bx, bx                                ; 31 db                       ; 0xc17e9
    mov dx, 07c9ch                            ; ba 9c 7c                    ; 0xc17eb
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc17ee
    call 02b95h                               ; e8 a1 13                    ; 0xc17f1
    cmp word [bp-012h], strict byte 00000h    ; 83 7e ee 00                 ; 0xc17f4 vgabios.c:1172
    jne short 017feh                          ; 75 04                       ; 0xc17f8
    test di, di                               ; 85 ff                       ; 0xc17fa
    je short 01848h                           ; 74 4a                       ; 0xc17fc
    xor cx, cx                                ; 31 c9                       ; 0xc17fe vgabios.c:1177
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1800 vgabios.c:1179
    mov bx, di                                ; 89 fb                       ; 0xc1803
    add bx, cx                                ; 01 cb                       ; 0xc1805
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc1807
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc180b
    je short 01817h                           ; 74 08                       ; 0xc180d
    cmp al, byte [bp-00eh]                    ; 3a 46 f2                    ; 0xc180f vgabios.c:1181
    je short 01817h                           ; 74 03                       ; 0xc1812
    inc cx                                    ; 41                          ; 0xc1814 vgabios.c:1183
    jmp short 01800h                          ; eb e9                       ; 0xc1815 vgabios.c:1184
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc1817 vgabios.c:1186
    mov bx, di                                ; 89 fb                       ; 0xc181a
    add bx, cx                                ; 01 cb                       ; 0xc181c
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc181e
    cmp al, byte [bp-00eh]                    ; 3a 46 f2                    ; 0xc1822
    jne short 01848h                          ; 75 21                       ; 0xc1825
    movzx ax, byte [es:di]                    ; 26 0f b6 05                 ; 0xc1827 vgabios.c:1191
    push ax                                   ; 50                          ; 0xc182b
    movzx ax, byte [es:di+001h]               ; 26 0f b6 45 01              ; 0xc182c
    push ax                                   ; 50                          ; 0xc1831
    push word [es:di+004h]                    ; 26 ff 75 04                 ; 0xc1832
    mov cx, word [es:di+002h]                 ; 26 8b 4d 02                 ; 0xc1836
    mov bx, word [es:di+006h]                 ; 26 8b 5d 06                 ; 0xc183a
    mov dx, word [es:di+008h]                 ; 26 8b 55 08                 ; 0xc183e
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc1842
    call 02c0ah                               ; e8 c2 13                    ; 0xc1845
    xor bl, bl                                ; 30 db                       ; 0xc1848 vgabios.c:1195
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc184a
    mov AH, strict byte 011h                  ; b4 11                       ; 0xc184c
    int 06dh                                  ; cd 6d                       ; 0xc184e
    mov bx, 0596fh                            ; bb 6f 59                    ; 0xc1850 vgabios.c:1199
    mov cx, ds                                ; 8c d9                       ; 0xc1853
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc1855
    call 009f0h                               ; e8 95 f1                    ; 0xc1858
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc185b vgabios.c:1201
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc185e
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc1862
    je short 01880h                           ; 74 1a                       ; 0xc1864
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc1866
    je short 0187bh                           ; 74 11                       ; 0xc1868
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc186a
    jne short 01885h                          ; 75 17                       ; 0xc186c
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc186e vgabios.c:1203
    mov cx, ds                                ; 8c d9                       ; 0xc1871
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc1873
    call 009f0h                               ; e8 77 f1                    ; 0xc1876
    jmp short 01885h                          ; eb 0a                       ; 0xc1879 vgabios.c:1204
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc187b vgabios.c:1206
    jmp short 01871h                          ; eb f1                       ; 0xc187e
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc1880 vgabios.c:1209
    jmp short 01871h                          ; eb ec                       ; 0xc1883
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc1885 vgabios.c:1212
    pop di                                    ; 5f                          ; 0xc1888
    pop si                                    ; 5e                          ; 0xc1889
    pop dx                                    ; 5a                          ; 0xc188a
    pop cx                                    ; 59                          ; 0xc188b
    pop bx                                    ; 5b                          ; 0xc188c
    pop bp                                    ; 5d                          ; 0xc188d
    retn                                      ; c3                          ; 0xc188e
  ; disGetNextSymbol 0xc188f LB 0x2a2a -> off=0x0 cb=0000000000000075 uValue=00000000000c188f 'vgamem_copy_pl4'
vgamem_copy_pl4:                             ; 0xc188f LB 0x75
    push bp                                   ; 55                          ; 0xc188f vgabios.c:1215
    mov bp, sp                                ; 89 e5                       ; 0xc1890
    push si                                   ; 56                          ; 0xc1892
    push di                                   ; 57                          ; 0xc1893
    push ax                                   ; 50                          ; 0xc1894
    push ax                                   ; 50                          ; 0xc1895
    mov bh, cl                                ; 88 cf                       ; 0xc1896
    movzx di, dl                              ; 0f b6 fa                    ; 0xc1898 vgabios.c:1221
    movzx cx, byte [bp+006h]                  ; 0f b6 4e 06                 ; 0xc189b
    imul di, cx                               ; 0f af f9                    ; 0xc189f
    movzx si, byte [bp+004h]                  ; 0f b6 76 04                 ; 0xc18a2
    imul di, si                               ; 0f af fe                    ; 0xc18a6
    xor ah, ah                                ; 30 e4                       ; 0xc18a9
    add di, ax                                ; 01 c7                       ; 0xc18ab
    mov word [bp-008h], di                    ; 89 7e f8                    ; 0xc18ad
    movzx di, bl                              ; 0f b6 fb                    ; 0xc18b0 vgabios.c:1222
    imul cx, di                               ; 0f af cf                    ; 0xc18b3
    imul cx, si                               ; 0f af ce                    ; 0xc18b6
    add cx, ax                                ; 01 c1                       ; 0xc18b9
    mov word [bp-006h], cx                    ; 89 4e fa                    ; 0xc18bb
    mov ax, 00105h                            ; b8 05 01                    ; 0xc18be vgabios.c:1223
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc18c1
    out DX, ax                                ; ef                          ; 0xc18c4
    xor bl, bl                                ; 30 db                       ; 0xc18c5 vgabios.c:1224
    cmp bl, byte [bp+006h]                    ; 3a 5e 06                    ; 0xc18c7
    jnc short 018f4h                          ; 73 28                       ; 0xc18ca
    movzx cx, bh                              ; 0f b6 cf                    ; 0xc18cc vgabios.c:1226
    movzx si, bl                              ; 0f b6 f3                    ; 0xc18cf
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc18d2
    imul ax, si                               ; 0f af c6                    ; 0xc18d6
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc18d9
    add si, ax                                ; 01 c6                       ; 0xc18dc
    mov di, word [bp-006h]                    ; 8b 7e fa                    ; 0xc18de
    add di, ax                                ; 01 c7                       ; 0xc18e1
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc18e3
    mov es, dx                                ; 8e c2                       ; 0xc18e6
    jcxz 018f0h                               ; e3 06                       ; 0xc18e8
    push DS                                   ; 1e                          ; 0xc18ea
    mov ds, dx                                ; 8e da                       ; 0xc18eb
    rep movsb                                 ; f3 a4                       ; 0xc18ed
    pop DS                                    ; 1f                          ; 0xc18ef
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc18f0 vgabios.c:1227
    jmp short 018c7h                          ; eb d3                       ; 0xc18f2
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc18f4 vgabios.c:1228
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc18f7
    out DX, ax                                ; ef                          ; 0xc18fa
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc18fb vgabios.c:1229
    pop di                                    ; 5f                          ; 0xc18fe
    pop si                                    ; 5e                          ; 0xc18ff
    pop bp                                    ; 5d                          ; 0xc1900
    retn 00004h                               ; c2 04 00                    ; 0xc1901
  ; disGetNextSymbol 0xc1904 LB 0x29b5 -> off=0x0 cb=0000000000000060 uValue=00000000000c1904 'vgamem_fill_pl4'
vgamem_fill_pl4:                             ; 0xc1904 LB 0x60
    push bp                                   ; 55                          ; 0xc1904 vgabios.c:1232
    mov bp, sp                                ; 89 e5                       ; 0xc1905
    push di                                   ; 57                          ; 0xc1907
    push ax                                   ; 50                          ; 0xc1908
    push ax                                   ; 50                          ; 0xc1909
    mov byte [bp-004h], bl                    ; 88 5e fc                    ; 0xc190a
    mov bh, cl                                ; 88 cf                       ; 0xc190d
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc190f vgabios.c:1238
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc1912
    imul cx, dx                               ; 0f af ca                    ; 0xc1916
    movzx dx, bh                              ; 0f b6 d7                    ; 0xc1919
    imul dx, cx                               ; 0f af d1                    ; 0xc191c
    xor ah, ah                                ; 30 e4                       ; 0xc191f
    add dx, ax                                ; 01 c2                       ; 0xc1921
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc1923
    mov ax, 00205h                            ; b8 05 02                    ; 0xc1926 vgabios.c:1239
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1929
    out DX, ax                                ; ef                          ; 0xc192c
    xor bl, bl                                ; 30 db                       ; 0xc192d vgabios.c:1240
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc192f
    jnc short 01955h                          ; 73 21                       ; 0xc1932
    movzx cx, byte [bp-004h]                  ; 0f b6 4e fc                 ; 0xc1934 vgabios.c:1242
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc1938
    movzx dx, bl                              ; 0f b6 d3                    ; 0xc193c
    movzx di, bh                              ; 0f b6 ff                    ; 0xc193f
    imul di, dx                               ; 0f af fa                    ; 0xc1942
    add di, word [bp-006h]                    ; 03 7e fa                    ; 0xc1945
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1948
    mov es, dx                                ; 8e c2                       ; 0xc194b
    jcxz 01951h                               ; e3 02                       ; 0xc194d
    rep stosb                                 ; f3 aa                       ; 0xc194f
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc1951 vgabios.c:1243
    jmp short 0192fh                          ; eb da                       ; 0xc1953
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1955 vgabios.c:1244
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1958
    out DX, ax                                ; ef                          ; 0xc195b
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc195c vgabios.c:1245
    pop di                                    ; 5f                          ; 0xc195f
    pop bp                                    ; 5d                          ; 0xc1960
    retn 00004h                               ; c2 04 00                    ; 0xc1961
  ; disGetNextSymbol 0xc1964 LB 0x2955 -> off=0x0 cb=00000000000000a3 uValue=00000000000c1964 'vgamem_copy_cga'
vgamem_copy_cga:                             ; 0xc1964 LB 0xa3
    push bp                                   ; 55                          ; 0xc1964 vgabios.c:1248
    mov bp, sp                                ; 89 e5                       ; 0xc1965
    push si                                   ; 56                          ; 0xc1967
    push di                                   ; 57                          ; 0xc1968
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1969
    mov dh, bl                                ; 88 de                       ; 0xc196c
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc196e
    movzx di, dl                              ; 0f b6 fa                    ; 0xc1971 vgabios.c:1254
    movzx si, byte [bp+006h]                  ; 0f b6 76 06                 ; 0xc1974
    imul di, si                               ; 0f af fe                    ; 0xc1978
    movzx bx, byte [bp+004h]                  ; 0f b6 5e 04                 ; 0xc197b
    imul di, bx                               ; 0f af fb                    ; 0xc197f
    sar di, 1                                 ; d1 ff                       ; 0xc1982
    xor ah, ah                                ; 30 e4                       ; 0xc1984
    add di, ax                                ; 01 c7                       ; 0xc1986
    mov word [bp-00ch], di                    ; 89 7e f4                    ; 0xc1988
    movzx dx, dh                              ; 0f b6 d6                    ; 0xc198b vgabios.c:1255
    imul dx, si                               ; 0f af d6                    ; 0xc198e
    imul dx, bx                               ; 0f af d3                    ; 0xc1991
    sar dx, 1                                 ; d1 fa                       ; 0xc1994
    add dx, ax                                ; 01 c2                       ; 0xc1996
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc1998
    mov byte [bp-008h], ah                    ; 88 66 f8                    ; 0xc199b vgabios.c:1256
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc199e
    cwd                                       ; 99                          ; 0xc19a2
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc19a3
    sar ax, 1                                 ; d1 f8                       ; 0xc19a5
    movzx bx, byte [bp-008h]                  ; 0f b6 5e f8                 ; 0xc19a7
    cmp bx, ax                                ; 39 c3                       ; 0xc19ab
    jnl short 019feh                          ; 7d 4f                       ; 0xc19ad
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc19af vgabios.c:1258
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc19b3
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc19b6
    imul bx, ax                               ; 0f af d8                    ; 0xc19ba
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc19bd
    add si, bx                                ; 01 de                       ; 0xc19c0
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc19c2
    add di, bx                                ; 01 df                       ; 0xc19c5
    mov cx, word [bp-00eh]                    ; 8b 4e f2                    ; 0xc19c7
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc19ca
    mov es, dx                                ; 8e c2                       ; 0xc19cd
    jcxz 019d7h                               ; e3 06                       ; 0xc19cf
    push DS                                   ; 1e                          ; 0xc19d1
    mov ds, dx                                ; 8e da                       ; 0xc19d2
    rep movsb                                 ; f3 a4                       ; 0xc19d4
    pop DS                                    ; 1f                          ; 0xc19d6
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc19d7 vgabios.c:1259
    add si, 02000h                            ; 81 c6 00 20                 ; 0xc19da
    add si, bx                                ; 01 de                       ; 0xc19de
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc19e0
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc19e3
    add di, bx                                ; 01 df                       ; 0xc19e7
    mov cx, word [bp-00eh]                    ; 8b 4e f2                    ; 0xc19e9
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc19ec
    mov es, dx                                ; 8e c2                       ; 0xc19ef
    jcxz 019f9h                               ; e3 06                       ; 0xc19f1
    push DS                                   ; 1e                          ; 0xc19f3
    mov ds, dx                                ; 8e da                       ; 0xc19f4
    rep movsb                                 ; f3 a4                       ; 0xc19f6
    pop DS                                    ; 1f                          ; 0xc19f8
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc19f9 vgabios.c:1260
    jmp short 0199eh                          ; eb a0                       ; 0xc19fc
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc19fe vgabios.c:1261
    pop di                                    ; 5f                          ; 0xc1a01
    pop si                                    ; 5e                          ; 0xc1a02
    pop bp                                    ; 5d                          ; 0xc1a03
    retn 00004h                               ; c2 04 00                    ; 0xc1a04
  ; disGetNextSymbol 0xc1a07 LB 0x28b2 -> off=0x0 cb=0000000000000081 uValue=00000000000c1a07 'vgamem_fill_cga'
vgamem_fill_cga:                             ; 0xc1a07 LB 0x81
    push bp                                   ; 55                          ; 0xc1a07 vgabios.c:1264
    mov bp, sp                                ; 89 e5                       ; 0xc1a08
    push si                                   ; 56                          ; 0xc1a0a
    push di                                   ; 57                          ; 0xc1a0b
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1a0c
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1a0f
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1a12
    movzx bx, dl                              ; 0f b6 da                    ; 0xc1a15 vgabios.c:1270
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc1a18
    imul bx, dx                               ; 0f af da                    ; 0xc1a1c
    movzx dx, cl                              ; 0f b6 d1                    ; 0xc1a1f
    imul dx, bx                               ; 0f af d3                    ; 0xc1a22
    sar dx, 1                                 ; d1 fa                       ; 0xc1a25
    xor ah, ah                                ; 30 e4                       ; 0xc1a27
    add dx, ax                                ; 01 c2                       ; 0xc1a29
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc1a2b
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1a2e vgabios.c:1271
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1a31
    cwd                                       ; 99                          ; 0xc1a35
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1a36
    sar ax, 1                                 ; d1 f8                       ; 0xc1a38
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc1a3a
    cmp dx, ax                                ; 39 c2                       ; 0xc1a3e
    jnl short 01a7fh                          ; 7d 3d                       ; 0xc1a40
    movzx si, byte [bp-00ah]                  ; 0f b6 76 f6                 ; 0xc1a42 vgabios.c:1273
    movzx bx, byte [bp+006h]                  ; 0f b6 5e 06                 ; 0xc1a46
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1a4a
    imul dx, ax                               ; 0f af d0                    ; 0xc1a4e
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1a51
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc1a54
    add di, dx                                ; 01 d7                       ; 0xc1a57
    mov cx, si                                ; 89 f1                       ; 0xc1a59
    mov ax, bx                                ; 89 d8                       ; 0xc1a5b
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1a5d
    mov es, dx                                ; 8e c2                       ; 0xc1a60
    jcxz 01a66h                               ; e3 02                       ; 0xc1a62
    rep stosb                                 ; f3 aa                       ; 0xc1a64
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc1a66 vgabios.c:1274
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1a69
    add di, word [bp-00eh]                    ; 03 7e f2                    ; 0xc1a6d
    mov cx, si                                ; 89 f1                       ; 0xc1a70
    mov ax, bx                                ; 89 d8                       ; 0xc1a72
    mov es, dx                                ; 8e c2                       ; 0xc1a74
    jcxz 01a7ah                               ; e3 02                       ; 0xc1a76
    rep stosb                                 ; f3 aa                       ; 0xc1a78
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1a7a vgabios.c:1275
    jmp short 01a31h                          ; eb b2                       ; 0xc1a7d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1a7f vgabios.c:1276
    pop di                                    ; 5f                          ; 0xc1a82
    pop si                                    ; 5e                          ; 0xc1a83
    pop bp                                    ; 5d                          ; 0xc1a84
    retn 00004h                               ; c2 04 00                    ; 0xc1a85
  ; disGetNextSymbol 0xc1a88 LB 0x2831 -> off=0x0 cb=0000000000000079 uValue=00000000000c1a88 'vgamem_copy_linear'
vgamem_copy_linear:                          ; 0xc1a88 LB 0x79
    push bp                                   ; 55                          ; 0xc1a88 vgabios.c:1279
    mov bp, sp                                ; 89 e5                       ; 0xc1a89
    push si                                   ; 56                          ; 0xc1a8b
    push di                                   ; 57                          ; 0xc1a8c
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc1a8d
    mov ah, al                                ; 88 c4                       ; 0xc1a90
    mov al, bl                                ; 88 d8                       ; 0xc1a92
    mov bx, cx                                ; 89 cb                       ; 0xc1a94
    xor dh, dh                                ; 30 f6                       ; 0xc1a96 vgabios.c:1285
    movzx di, byte [bp+006h]                  ; 0f b6 7e 06                 ; 0xc1a98
    imul dx, di                               ; 0f af d7                    ; 0xc1a9c
    imul dx, word [bp+004h]                   ; 0f af 56 04                 ; 0xc1a9f
    movzx si, ah                              ; 0f b6 f4                    ; 0xc1aa3
    add dx, si                                ; 01 f2                       ; 0xc1aa6
    sal dx, 003h                              ; c1 e2 03                    ; 0xc1aa8
    mov word [bp-008h], dx                    ; 89 56 f8                    ; 0xc1aab
    xor ah, ah                                ; 30 e4                       ; 0xc1aae vgabios.c:1286
    imul ax, di                               ; 0f af c7                    ; 0xc1ab0
    imul ax, word [bp+004h]                   ; 0f af 46 04                 ; 0xc1ab3
    add si, ax                                ; 01 c6                       ; 0xc1ab7
    sal si, 003h                              ; c1 e6 03                    ; 0xc1ab9
    mov word [bp-00ah], si                    ; 89 76 f6                    ; 0xc1abc
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1abf vgabios.c:1287
    sal word [bp+004h], 003h                  ; c1 66 04 03                 ; 0xc1ac2 vgabios.c:1288
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc1ac6 vgabios.c:1289
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1aca
    cmp al, byte [bp+006h]                    ; 3a 46 06                    ; 0xc1acd
    jnc short 01af8h                          ; 73 26                       ; 0xc1ad0
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc1ad2 vgabios.c:1291
    imul ax, word [bp+004h]                   ; 0f af 46 04                 ; 0xc1ad6
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc1ada
    add si, ax                                ; 01 c6                       ; 0xc1add
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc1adf
    add di, ax                                ; 01 c7                       ; 0xc1ae2
    mov cx, bx                                ; 89 d9                       ; 0xc1ae4
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1ae6
    mov es, dx                                ; 8e c2                       ; 0xc1ae9
    jcxz 01af3h                               ; e3 06                       ; 0xc1aeb
    push DS                                   ; 1e                          ; 0xc1aed
    mov ds, dx                                ; 8e da                       ; 0xc1aee
    rep movsb                                 ; f3 a4                       ; 0xc1af0
    pop DS                                    ; 1f                          ; 0xc1af2
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1af3 vgabios.c:1292
    jmp short 01acah                          ; eb d2                       ; 0xc1af6
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1af8 vgabios.c:1293
    pop di                                    ; 5f                          ; 0xc1afb
    pop si                                    ; 5e                          ; 0xc1afc
    pop bp                                    ; 5d                          ; 0xc1afd
    retn 00004h                               ; c2 04 00                    ; 0xc1afe
  ; disGetNextSymbol 0xc1b01 LB 0x27b8 -> off=0x0 cb=000000000000005c uValue=00000000000c1b01 'vgamem_fill_linear'
vgamem_fill_linear:                          ; 0xc1b01 LB 0x5c
    push bp                                   ; 55                          ; 0xc1b01 vgabios.c:1296
    mov bp, sp                                ; 89 e5                       ; 0xc1b02
    push si                                   ; 56                          ; 0xc1b04
    push di                                   ; 57                          ; 0xc1b05
    push ax                                   ; 50                          ; 0xc1b06
    push ax                                   ; 50                          ; 0xc1b07
    mov si, bx                                ; 89 de                       ; 0xc1b08
    mov bx, cx                                ; 89 cb                       ; 0xc1b0a
    xor dh, dh                                ; 30 f6                       ; 0xc1b0c vgabios.c:1302
    movzx di, byte [bp+004h]                  ; 0f b6 7e 04                 ; 0xc1b0e
    imul dx, di                               ; 0f af d7                    ; 0xc1b12
    imul dx, cx                               ; 0f af d1                    ; 0xc1b15
    xor ah, ah                                ; 30 e4                       ; 0xc1b18
    add ax, dx                                ; 01 d0                       ; 0xc1b1a
    sal ax, 003h                              ; c1 e0 03                    ; 0xc1b1c
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc1b1f
    sal si, 003h                              ; c1 e6 03                    ; 0xc1b22 vgabios.c:1303
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1b25 vgabios.c:1304
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc1b28 vgabios.c:1305
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1b2c
    cmp al, byte [bp+004h]                    ; 3a 46 04                    ; 0xc1b2f
    jnc short 01b54h                          ; 73 20                       ; 0xc1b32
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc1b34 vgabios.c:1307
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc1b38
    imul dx, bx                               ; 0f af d3                    ; 0xc1b3c
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc1b3f
    add di, dx                                ; 01 d7                       ; 0xc1b42
    mov cx, si                                ; 89 f1                       ; 0xc1b44
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1b46
    mov es, dx                                ; 8e c2                       ; 0xc1b49
    jcxz 01b4fh                               ; e3 02                       ; 0xc1b4b
    rep stosb                                 ; f3 aa                       ; 0xc1b4d
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1b4f vgabios.c:1308
    jmp short 01b2ch                          ; eb d8                       ; 0xc1b52
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1b54 vgabios.c:1309
    pop di                                    ; 5f                          ; 0xc1b57
    pop si                                    ; 5e                          ; 0xc1b58
    pop bp                                    ; 5d                          ; 0xc1b59
    retn 00004h                               ; c2 04 00                    ; 0xc1b5a
  ; disGetNextSymbol 0xc1b5d LB 0x275c -> off=0x0 cb=0000000000000628 uValue=00000000000c1b5d 'biosfn_scroll'
biosfn_scroll:                               ; 0xc1b5d LB 0x628
    push bp                                   ; 55                          ; 0xc1b5d vgabios.c:1312
    mov bp, sp                                ; 89 e5                       ; 0xc1b5e
    push si                                   ; 56                          ; 0xc1b60
    push di                                   ; 57                          ; 0xc1b61
    sub sp, strict byte 00018h                ; 83 ec 18                    ; 0xc1b62
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc1b65
    mov byte [bp-012h], dl                    ; 88 56 ee                    ; 0xc1b68
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1b6b
    mov byte [bp-010h], cl                    ; 88 4e f0                    ; 0xc1b6e
    mov dh, byte [bp+006h]                    ; 8a 76 06                    ; 0xc1b71
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc1b74 vgabios.c:1321
    jnbe near 0217ch                          ; 0f 87 01 06                 ; 0xc1b77
    cmp dh, cl                                ; 38 ce                       ; 0xc1b7b vgabios.c:1322
    jc near 0217ch                            ; 0f 82 fb 05                 ; 0xc1b7d
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1b81 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1b84
    mov es, ax                                ; 8e c0                       ; 0xc1b87
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1b89
    xor ah, ah                                ; 30 e4                       ; 0xc1b8c vgabios.c:1326
    call 035edh                               ; e8 5c 1a                    ; 0xc1b8e
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1b91
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1b94 vgabios.c:1327
    je near 0217ch                            ; 0f 84 e2 05                 ; 0xc1b96
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1b9a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1b9d
    mov es, ax                                ; 8e c0                       ; 0xc1ba0
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1ba2
    movzx cx, al                              ; 0f b6 c8                    ; 0xc1ba5 vgabios.c:48
    inc cx                                    ; 41                          ; 0xc1ba8
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1ba9 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc1bac
    mov word [bp-014h], ax                    ; 89 46 ec                    ; 0xc1baf vgabios.c:58
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc1bb2 vgabios.c:1334
    jne short 01bc1h                          ; 75 09                       ; 0xc1bb6
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc1bb8 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1bbb
    mov byte [bp+008h], al                    ; 88 46 08                    ; 0xc1bbe vgabios.c:48
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1bc1 vgabios.c:1337
    cmp ax, cx                                ; 39 c8                       ; 0xc1bc5
    jc short 01bd0h                           ; 72 07                       ; 0xc1bc7
    mov al, cl                                ; 88 c8                       ; 0xc1bc9
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc1bcb
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc1bcd
    movzx ax, dh                              ; 0f b6 c6                    ; 0xc1bd0 vgabios.c:1338
    cmp ax, word [bp-014h]                    ; 3b 46 ec                    ; 0xc1bd3
    jc short 01bddh                           ; 72 05                       ; 0xc1bd6
    mov dh, byte [bp-014h]                    ; 8a 76 ec                    ; 0xc1bd8
    db  0feh, 0ceh
    ; dec dh                                    ; fe ce                     ; 0xc1bdb
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1bdd vgabios.c:1339
    cmp ax, cx                                ; 39 c8                       ; 0xc1be1
    jbe short 01be9h                          ; 76 04                       ; 0xc1be3
    mov byte [bp-008h], 000h                  ; c6 46 f8 00                 ; 0xc1be5
    mov al, dh                                ; 88 f0                       ; 0xc1be9 vgabios.c:1340
    sub al, byte [bp-010h]                    ; 2a 46 f0                    ; 0xc1beb
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc1bee
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1bf0
    movzx di, byte [bp-006h]                  ; 0f b6 7e fa                 ; 0xc1bf3 vgabios.c:1342
    mov bx, di                                ; 89 fb                       ; 0xc1bf7
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1bf9
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc1bfc
    dec ax                                    ; 48                          ; 0xc1bff
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc1c00
    mov ax, cx                                ; 89 c8                       ; 0xc1c03
    dec ax                                    ; 48                          ; 0xc1c05
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc1c06
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc1c09
    imul ax, cx                               ; 0f af c1                    ; 0xc1c0c
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc1c0f
    jne near 01db3h                           ; 0f 85 9b 01                 ; 0xc1c14
    mov cx, ax                                ; 89 c1                       ; 0xc1c18 vgabios.c:1345
    add cx, ax                                ; 01 c1                       ; 0xc1c1a
    or cl, 0ffh                               ; 80 c9 ff                    ; 0xc1c1c
    movzx si, byte [bp+008h]                  ; 0f b6 76 08                 ; 0xc1c1f
    inc cx                                    ; 41                          ; 0xc1c23
    imul cx, si                               ; 0f af ce                    ; 0xc1c24
    mov word [bp-01ch], cx                    ; 89 4e e4                    ; 0xc1c27
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1c2a vgabios.c:1350
    jne short 01c6bh                          ; 75 3b                       ; 0xc1c2e
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1c30
    jne short 01c6bh                          ; 75 35                       ; 0xc1c34
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc1c36
    jne short 01c6bh                          ; 75 2f                       ; 0xc1c3a
    movzx cx, byte [bp+004h]                  ; 0f b6 4e 04                 ; 0xc1c3c
    cmp cx, word [bp-016h]                    ; 3b 4e ea                    ; 0xc1c40
    jne short 01c6bh                          ; 75 26                       ; 0xc1c43
    movzx dx, dh                              ; 0f b6 d6                    ; 0xc1c45
    cmp dx, word [bp-018h]                    ; 3b 56 e8                    ; 0xc1c48
    jne short 01c6bh                          ; 75 1e                       ; 0xc1c4b
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc1c4d vgabios.c:1352
    sal dx, 008h                              ; c1 e2 08                    ; 0xc1c51
    add dx, strict byte 00020h                ; 83 c2 20                    ; 0xc1c54
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc1c57
    mov cx, ax                                ; 89 c1                       ; 0xc1c5b
    mov ax, dx                                ; 89 d0                       ; 0xc1c5d
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1c5f
    mov es, bx                                ; 8e c3                       ; 0xc1c62
    jcxz 01c68h                               ; e3 02                       ; 0xc1c64
    rep stosw                                 ; f3 ab                       ; 0xc1c66
    jmp near 0217ch                           ; e9 11 05                    ; 0xc1c68 vgabios.c:1354
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1c6b vgabios.c:1356
    jne near 01d08h                           ; 0f 85 95 00                 ; 0xc1c6f
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1c73 vgabios.c:1357
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1c77
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc1c7a
    cmp dx, word [bp-01ah]                    ; 3b 56 e6                    ; 0xc1c7e
    jc near 0217ch                            ; 0f 82 f7 04                 ; 0xc1c81
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1c85 vgabios.c:1359
    add ax, word [bp-01ah]                    ; 03 46 e6                    ; 0xc1c89
    cmp ax, dx                                ; 39 d0                       ; 0xc1c8c
    jnbe short 01c96h                         ; 77 06                       ; 0xc1c8e
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1c90
    jne short 01cc9h                          ; 75 33                       ; 0xc1c94
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1c96 vgabios.c:1360
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1c9a
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1c9e
    add ax, strict word 00020h                ; 05 20 00                    ; 0xc1ca1
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1ca4
    imul bx, word [bp-014h]                   ; 0f af 5e ec                 ; 0xc1ca7
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc1cab
    add dx, bx                                ; 01 da                       ; 0xc1caf
    add dx, dx                                ; 01 d2                       ; 0xc1cb1
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1cb3
    add di, dx                                ; 01 d7                       ; 0xc1cb6
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1cb8
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1cbc
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1cbf
    jcxz 01cc7h                               ; e3 02                       ; 0xc1cc3
    rep stosw                                 ; f3 ab                       ; 0xc1cc5
    jmp short 01d02h                          ; eb 39                       ; 0xc1cc7 vgabios.c:1361
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1cc9 vgabios.c:1362
    mov si, ax                                ; 89 c6                       ; 0xc1ccd
    imul si, word [bp-014h]                   ; 0f af 76 ec                 ; 0xc1ccf
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc1cd3
    add si, dx                                ; 01 d6                       ; 0xc1cd7
    add si, si                                ; 01 f6                       ; 0xc1cd9
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1cdb
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1cdf
    mov ax, word [bx+047b5h]                  ; 8b 87 b5 47                 ; 0xc1ce2
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1ce6
    imul bx, word [bp-014h]                   ; 0f af 5e ec                 ; 0xc1ce9
    mov di, dx                                ; 89 d7                       ; 0xc1ced
    add di, bx                                ; 01 df                       ; 0xc1cef
    add di, di                                ; 01 ff                       ; 0xc1cf1
    add di, word [bp-01ch]                    ; 03 7e e4                    ; 0xc1cf3
    mov dx, ax                                ; 89 c2                       ; 0xc1cf6
    mov es, ax                                ; 8e c0                       ; 0xc1cf8
    jcxz 01d02h                               ; e3 06                       ; 0xc1cfa
    push DS                                   ; 1e                          ; 0xc1cfc
    mov ds, dx                                ; 8e da                       ; 0xc1cfd
    rep movsw                                 ; f3 a5                       ; 0xc1cff
    pop DS                                    ; 1f                          ; 0xc1d01
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc1d02 vgabios.c:1363
    jmp near 01c7ah                           ; e9 72 ff                    ; 0xc1d05
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1d08 vgabios.c:1366
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1d0c
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1d0f
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1d13
    jnbe near 0217ch                          ; 0f 87 62 04                 ; 0xc1d16
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1d1a vgabios.c:1368
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc1d1e
    add ax, dx                                ; 01 d0                       ; 0xc1d22
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1d24
    jnbe short 01d2fh                         ; 77 06                       ; 0xc1d27
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1d29
    jne short 01d62h                          ; 75 33                       ; 0xc1d2d
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1d2f vgabios.c:1369
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1d33
    sal ax, 008h                              ; c1 e0 08                    ; 0xc1d37
    add ax, strict word 00020h                ; 05 20 00                    ; 0xc1d3a
    mov dx, word [bp-01ah]                    ; 8b 56 e6                    ; 0xc1d3d
    imul dx, word [bp-014h]                   ; 0f af 56 ec                 ; 0xc1d40
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc1d44
    add dx, bx                                ; 01 da                       ; 0xc1d48
    add dx, dx                                ; 01 d2                       ; 0xc1d4a
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1d4c
    add di, dx                                ; 01 d7                       ; 0xc1d4f
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1d51
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1d55
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1d58
    jcxz 01d60h                               ; e3 02                       ; 0xc1d5c
    rep stosw                                 ; f3 ab                       ; 0xc1d5e
    jmp short 01da2h                          ; eb 40                       ; 0xc1d60 vgabios.c:1370
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1d62 vgabios.c:1371
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1d66
    mov si, word [bp-01ah]                    ; 8b 76 e6                    ; 0xc1d6a
    sub si, ax                                ; 29 c6                       ; 0xc1d6d
    imul si, word [bp-014h]                   ; 0f af 76 ec                 ; 0xc1d6f
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc1d73
    add si, dx                                ; 01 d6                       ; 0xc1d77
    add si, si                                ; 01 f6                       ; 0xc1d79
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1d7b
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1d7f
    mov ax, word [bx+047b5h]                  ; 8b 87 b5 47                 ; 0xc1d82
    mov bx, word [bp-01ah]                    ; 8b 5e e6                    ; 0xc1d86
    imul bx, word [bp-014h]                   ; 0f af 5e ec                 ; 0xc1d89
    add dx, bx                                ; 01 da                       ; 0xc1d8d
    add dx, dx                                ; 01 d2                       ; 0xc1d8f
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1d91
    add di, dx                                ; 01 d7                       ; 0xc1d94
    mov dx, ax                                ; 89 c2                       ; 0xc1d96
    mov es, ax                                ; 8e c0                       ; 0xc1d98
    jcxz 01da2h                               ; e3 06                       ; 0xc1d9a
    push DS                                   ; 1e                          ; 0xc1d9c
    mov ds, dx                                ; 8e da                       ; 0xc1d9d
    rep movsw                                 ; f3 a5                       ; 0xc1d9f
    pop DS                                    ; 1f                          ; 0xc1da1
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1da2 vgabios.c:1372
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1da6
    jc near 0217ch                            ; 0f 82 cf 03                 ; 0xc1da9
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc1dad vgabios.c:1373
    jmp near 01d0fh                           ; e9 5c ff                    ; 0xc1db0
    movzx di, byte [di+04831h]                ; 0f b6 bd 31 48              ; 0xc1db3 vgabios.c:1379
    sal di, 006h                              ; c1 e7 06                    ; 0xc1db8
    mov dl, byte [di+04847h]                  ; 8a 95 47 48                 ; 0xc1dbb
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc1dbf
    mov dl, byte [bx+047b3h]                  ; 8a 97 b3 47                 ; 0xc1dc2 vgabios.c:1380
    cmp dl, 003h                              ; 80 fa 03                    ; 0xc1dc6
    jc short 01ddch                           ; 72 11                       ; 0xc1dc9
    jbe short 01de6h                          ; 76 19                       ; 0xc1dcb
    cmp dl, 005h                              ; 80 fa 05                    ; 0xc1dcd
    je near 0205fh                            ; 0f 84 8b 02                 ; 0xc1dd0
    cmp dl, 004h                              ; 80 fa 04                    ; 0xc1dd4
    je short 01de6h                           ; 74 0d                       ; 0xc1dd7
    jmp near 0217ch                           ; e9 a0 03                    ; 0xc1dd9
    cmp dl, 002h                              ; 80 fa 02                    ; 0xc1ddc
    je near 01f25h                            ; 0f 84 42 01                 ; 0xc1ddf
    jmp near 0217ch                           ; e9 96 03                    ; 0xc1de3
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1de6 vgabios.c:1384
    jne short 01e3eh                          ; 75 52                       ; 0xc1dea
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1dec
    jne short 01e3eh                          ; 75 4c                       ; 0xc1df0
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc1df2
    jne short 01e3eh                          ; 75 46                       ; 0xc1df6
    movzx bx, byte [bp+004h]                  ; 0f b6 5e 04                 ; 0xc1df8
    mov ax, cx                                ; 89 c8                       ; 0xc1dfc
    dec ax                                    ; 48                          ; 0xc1dfe
    cmp bx, ax                                ; 39 c3                       ; 0xc1dff
    jne short 01e3eh                          ; 75 3b                       ; 0xc1e01
    movzx ax, dh                              ; 0f b6 c6                    ; 0xc1e03
    mov dx, word [bp-014h]                    ; 8b 56 ec                    ; 0xc1e06
    dec dx                                    ; 4a                          ; 0xc1e09
    cmp ax, dx                                ; 39 d0                       ; 0xc1e0a
    jne short 01e3eh                          ; 75 30                       ; 0xc1e0c
    mov ax, 00205h                            ; b8 05 02                    ; 0xc1e0e vgabios.c:1386
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1e11
    out DX, ax                                ; ef                          ; 0xc1e14
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc1e15 vgabios.c:1387
    imul ax, cx                               ; 0f af c1                    ; 0xc1e18
    movzx cx, byte [bp-00eh]                  ; 0f b6 4e f2                 ; 0xc1e1b
    imul cx, ax                               ; 0f af c8                    ; 0xc1e1f
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1e22
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc1e26
    sal bx, 003h                              ; c1 e3 03                    ; 0xc1e2a
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1e2d
    xor di, di                                ; 31 ff                       ; 0xc1e31
    jcxz 01e37h                               ; e3 02                       ; 0xc1e33
    rep stosb                                 ; f3 aa                       ; 0xc1e35
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1e37 vgabios.c:1388
    out DX, ax                                ; ef                          ; 0xc1e3a
    jmp near 0217ch                           ; e9 3e 03                    ; 0xc1e3b vgabios.c:1390
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1e3e vgabios.c:1392
    jne short 01eadh                          ; 75 69                       ; 0xc1e42
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1e44 vgabios.c:1393
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1e48
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1e4b
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1e4f
    jc near 0217ch                            ; 0f 82 26 03                 ; 0xc1e52
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc1e56 vgabios.c:1395
    add dx, word [bp-01ah]                    ; 03 56 e6                    ; 0xc1e5a
    cmp dx, ax                                ; 39 c2                       ; 0xc1e5d
    jnbe short 01e67h                         ; 77 06                       ; 0xc1e5f
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1e61
    jne short 01e86h                          ; 75 1f                       ; 0xc1e65
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1e67 vgabios.c:1396
    push ax                                   ; 50                          ; 0xc1e6b
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1e6c
    push ax                                   ; 50                          ; 0xc1e70
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc1e71
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc1e75
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc1e79
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1e7d
    call 01904h                               ; e8 80 fa                    ; 0xc1e81
    jmp short 01ea8h                          ; eb 22                       ; 0xc1e84 vgabios.c:1397
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1e86 vgabios.c:1398
    push ax                                   ; 50                          ; 0xc1e8a
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc1e8b
    push ax                                   ; 50                          ; 0xc1e8f
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1e90
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc1e94
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc1e98
    add al, byte [bp-008h]                    ; 02 46 f8                    ; 0xc1e9b
    movzx dx, al                              ; 0f b6 d0                    ; 0xc1e9e
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1ea1
    call 0188fh                               ; e8 e7 f9                    ; 0xc1ea5
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc1ea8 vgabios.c:1399
    jmp short 01e4bh                          ; eb 9e                       ; 0xc1eab
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1ead vgabios.c:1402
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1eb1
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1eb4
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1eb8
    jnbe near 0217ch                          ; 0f 87 bd 02                 ; 0xc1ebb
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc1ebf vgabios.c:1404
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1ec3
    add ax, dx                                ; 01 d0                       ; 0xc1ec7
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1ec9
    jnbe short 01ed4h                         ; 77 06                       ; 0xc1ecc
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1ece
    jne short 01ef3h                          ; 75 1f                       ; 0xc1ed2
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1ed4 vgabios.c:1405
    push ax                                   ; 50                          ; 0xc1ed8
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1ed9
    push ax                                   ; 50                          ; 0xc1edd
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc1ede
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc1ee2
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc1ee6
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1eea
    call 01904h                               ; e8 13 fa                    ; 0xc1eee
    jmp short 01f15h                          ; eb 22                       ; 0xc1ef1 vgabios.c:1406
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1ef3 vgabios.c:1407
    push ax                                   ; 50                          ; 0xc1ef7
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc1ef8
    push ax                                   ; 50                          ; 0xc1efc
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1efd
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc1f01
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc1f05
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc1f08
    movzx dx, al                              ; 0f b6 d0                    ; 0xc1f0b
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1f0e
    call 0188fh                               ; e8 7a f9                    ; 0xc1f12
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1f15 vgabios.c:1408
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1f19
    jc near 0217ch                            ; 0f 82 5c 02                 ; 0xc1f1c
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc1f20 vgabios.c:1409
    jmp short 01eb4h                          ; eb 8f                       ; 0xc1f23
    mov dl, byte [bx+047b4h]                  ; 8a 97 b4 47                 ; 0xc1f25 vgabios.c:1414
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1f29 vgabios.c:1415
    jne short 01f6ah                          ; 75 3b                       ; 0xc1f2d
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1f2f
    jne short 01f6ah                          ; 75 35                       ; 0xc1f33
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc1f35
    jne short 01f6ah                          ; 75 2f                       ; 0xc1f39
    movzx cx, byte [bp+004h]                  ; 0f b6 4e 04                 ; 0xc1f3b
    cmp cx, word [bp-016h]                    ; 3b 4e ea                    ; 0xc1f3f
    jne short 01f6ah                          ; 75 26                       ; 0xc1f42
    movzx cx, dh                              ; 0f b6 ce                    ; 0xc1f44
    cmp cx, word [bp-018h]                    ; 3b 4e e8                    ; 0xc1f47
    jne short 01f6ah                          ; 75 1e                       ; 0xc1f4a
    movzx cx, byte [bp-00eh]                  ; 0f b6 4e f2                 ; 0xc1f4c vgabios.c:1417
    imul ax, cx                               ; 0f af c1                    ; 0xc1f50
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc1f53
    imul cx, ax                               ; 0f af c8                    ; 0xc1f56
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1f59
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1f5d
    xor di, di                                ; 31 ff                       ; 0xc1f61
    jcxz 01f67h                               ; e3 02                       ; 0xc1f63
    rep stosb                                 ; f3 aa                       ; 0xc1f65
    jmp near 0217ch                           ; e9 12 02                    ; 0xc1f67 vgabios.c:1419
    cmp dl, 002h                              ; 80 fa 02                    ; 0xc1f6a vgabios.c:1421
    jne short 01f78h                          ; 75 09                       ; 0xc1f6d
    sal byte [bp-010h], 1                     ; d0 66 f0                    ; 0xc1f6f vgabios.c:1423
    sal byte [bp-00ah], 1                     ; d0 66 f6                    ; 0xc1f72 vgabios.c:1424
    sal word [bp-014h], 1                     ; d1 66 ec                    ; 0xc1f75 vgabios.c:1425
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1f78 vgabios.c:1428
    jne short 01fe7h                          ; 75 69                       ; 0xc1f7c
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1f7e vgabios.c:1429
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1f82
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1f85
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1f89
    jc near 0217ch                            ; 0f 82 ec 01                 ; 0xc1f8c
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc1f90 vgabios.c:1431
    add dx, word [bp-01ah]                    ; 03 56 e6                    ; 0xc1f94
    cmp dx, ax                                ; 39 c2                       ; 0xc1f97
    jnbe short 01fa1h                         ; 77 06                       ; 0xc1f99
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1f9b
    jne short 01fc0h                          ; 75 1f                       ; 0xc1f9f
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc1fa1 vgabios.c:1432
    push ax                                   ; 50                          ; 0xc1fa5
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1fa6
    push ax                                   ; 50                          ; 0xc1faa
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc1fab
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc1faf
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc1fb3
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1fb7
    call 01a07h                               ; e8 49 fa                    ; 0xc1fbb
    jmp short 01fe2h                          ; eb 22                       ; 0xc1fbe vgabios.c:1433
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc1fc0 vgabios.c:1434
    push ax                                   ; 50                          ; 0xc1fc4
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc1fc5
    push ax                                   ; 50                          ; 0xc1fc9
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc1fca
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc1fce
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc1fd2
    add al, byte [bp-008h]                    ; 02 46 f8                    ; 0xc1fd5
    movzx dx, al                              ; 0f b6 d0                    ; 0xc1fd8
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc1fdb
    call 01964h                               ; e8 82 f9                    ; 0xc1fdf
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc1fe2 vgabios.c:1435
    jmp short 01f85h                          ; eb 9e                       ; 0xc1fe5
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc1fe7 vgabios.c:1438
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc1feb
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc1fee
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc1ff2
    jnbe near 0217ch                          ; 0f 87 83 01                 ; 0xc1ff5
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc1ff9 vgabios.c:1440
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc1ffd
    add ax, dx                                ; 01 d0                       ; 0xc2001
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc2003
    jnbe short 0200eh                         ; 77 06                       ; 0xc2006
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc2008
    jne short 0202dh                          ; 75 1f                       ; 0xc200c
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc200e vgabios.c:1441
    push ax                                   ; 50                          ; 0xc2012
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc2013
    push ax                                   ; 50                          ; 0xc2017
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc2018
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc201c
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc2020
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2024
    call 01a07h                               ; e8 dc f9                    ; 0xc2028
    jmp short 0204fh                          ; eb 22                       ; 0xc202b vgabios.c:1442
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc202d vgabios.c:1443
    push ax                                   ; 50                          ; 0xc2031
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc2032
    push ax                                   ; 50                          ; 0xc2036
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc2037
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc203b
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc203f
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc2042
    movzx dx, al                              ; 0f b6 d0                    ; 0xc2045
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2048
    call 01964h                               ; e8 15 f9                    ; 0xc204c
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc204f vgabios.c:1444
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc2053
    jc near 0217ch                            ; 0f 82 22 01                 ; 0xc2056
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc205a vgabios.c:1445
    jmp short 01feeh                          ; eb 8f                       ; 0xc205d
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc205f vgabios.c:1450
    jne short 0209fh                          ; 75 3a                       ; 0xc2063
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc2065
    jne short 0209fh                          ; 75 34                       ; 0xc2069
    cmp byte [bp-010h], 000h                  ; 80 7e f0 00                 ; 0xc206b
    jne short 0209fh                          ; 75 2e                       ; 0xc206f
    movzx cx, byte [bp+004h]                  ; 0f b6 4e 04                 ; 0xc2071
    cmp cx, word [bp-016h]                    ; 3b 4e ea                    ; 0xc2075
    jne short 0209fh                          ; 75 25                       ; 0xc2078
    movzx dx, dh                              ; 0f b6 d6                    ; 0xc207a
    cmp dx, word [bp-018h]                    ; 3b 56 e8                    ; 0xc207d
    jne short 0209fh                          ; 75 1d                       ; 0xc2080
    movzx dx, byte [bp-00eh]                  ; 0f b6 56 f2                 ; 0xc2082 vgabios.c:1452
    mov cx, ax                                ; 89 c1                       ; 0xc2086
    imul cx, dx                               ; 0f af ca                    ; 0xc2088
    sal cx, 003h                              ; c1 e1 03                    ; 0xc208b
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc208e
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2092
    xor di, di                                ; 31 ff                       ; 0xc2096
    jcxz 0209ch                               ; e3 02                       ; 0xc2098
    rep stosb                                 ; f3 aa                       ; 0xc209a
    jmp near 0217ch                           ; e9 dd 00                    ; 0xc209c vgabios.c:1454
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc209f vgabios.c:1457
    jne short 0210bh                          ; 75 66                       ; 0xc20a3
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc20a5 vgabios.c:1458
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc20a9
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc20ac
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc20b0
    jc near 0217ch                            ; 0f 82 c5 00                 ; 0xc20b3
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc20b7 vgabios.c:1460
    add dx, word [bp-01ah]                    ; 03 56 e6                    ; 0xc20bb
    cmp dx, ax                                ; 39 c2                       ; 0xc20be
    jnbe short 020c8h                         ; 77 06                       ; 0xc20c0
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc20c2
    jne short 020e6h                          ; 75 1e                       ; 0xc20c6
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc20c8 vgabios.c:1461
    push ax                                   ; 50                          ; 0xc20cc
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc20cd
    push ax                                   ; 50                          ; 0xc20d1
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc20d2
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc20d6
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc20da
    mov cx, word [bp-014h]                    ; 8b 4e ec                    ; 0xc20de
    call 01b01h                               ; e8 1d fa                    ; 0xc20e1
    jmp short 02106h                          ; eb 20                       ; 0xc20e4 vgabios.c:1462
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc20e6 vgabios.c:1463
    push ax                                   ; 50                          ; 0xc20ea
    push word [bp-014h]                       ; ff 76 ec                    ; 0xc20eb
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc20ee
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc20f2
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc20f6
    add al, byte [bp-008h]                    ; 02 46 f8                    ; 0xc20f9
    movzx dx, al                              ; 0f b6 d0                    ; 0xc20fc
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc20ff
    call 01a88h                               ; e8 82 f9                    ; 0xc2103
    inc word [bp-01ah]                        ; ff 46 e6                    ; 0xc2106 vgabios.c:1464
    jmp short 020ach                          ; eb a1                       ; 0xc2109
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc210b vgabios.c:1467
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc210f
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc2112
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc2116
    jnbe short 0217ch                         ; 77 61                       ; 0xc2119
    movzx dx, byte [bp-00ch]                  ; 0f b6 56 f4                 ; 0xc211b vgabios.c:1469
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc211f
    add ax, dx                                ; 01 d0                       ; 0xc2123
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc2125
    jnbe short 02130h                         ; 77 06                       ; 0xc2128
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc212a
    jne short 0214eh                          ; 75 1e                       ; 0xc212e
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc2130 vgabios.c:1470
    push ax                                   ; 50                          ; 0xc2134
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc2135
    push ax                                   ; 50                          ; 0xc2139
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc213a
    movzx dx, byte [bp-01ah]                  ; 0f b6 56 e6                 ; 0xc213e
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2142
    mov cx, word [bp-014h]                    ; 8b 4e ec                    ; 0xc2146
    call 01b01h                               ; e8 b5 f9                    ; 0xc2149
    jmp short 0216eh                          ; eb 20                       ; 0xc214c vgabios.c:1471
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc214e vgabios.c:1472
    push ax                                   ; 50                          ; 0xc2152
    push word [bp-014h]                       ; ff 76 ec                    ; 0xc2153
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc2156
    movzx bx, byte [bp-01ah]                  ; 0f b6 5e e6                 ; 0xc215a
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc215e
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc2161
    movzx dx, al                              ; 0f b6 d0                    ; 0xc2164
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2167
    call 01a88h                               ; e8 1a f9                    ; 0xc216b
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc216e vgabios.c:1473
    cmp ax, word [bp-01ah]                    ; 3b 46 e6                    ; 0xc2172
    jc short 0217ch                           ; 72 05                       ; 0xc2175
    dec word [bp-01ah]                        ; ff 4e e6                    ; 0xc2177 vgabios.c:1474
    jmp short 02112h                          ; eb 96                       ; 0xc217a
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc217c vgabios.c:1485
    pop di                                    ; 5f                          ; 0xc217f
    pop si                                    ; 5e                          ; 0xc2180
    pop bp                                    ; 5d                          ; 0xc2181
    retn 00008h                               ; c2 08 00                    ; 0xc2182
  ; disGetNextSymbol 0xc2185 LB 0x2134 -> off=0x0 cb=00000000000000ff uValue=00000000000c2185 'write_gfx_char_pl4'
write_gfx_char_pl4:                          ; 0xc2185 LB 0xff
    push bp                                   ; 55                          ; 0xc2185 vgabios.c:1488
    mov bp, sp                                ; 89 e5                       ; 0xc2186
    push si                                   ; 56                          ; 0xc2188
    push di                                   ; 57                          ; 0xc2189
    sub sp, strict byte 0000ch                ; 83 ec 0c                    ; 0xc218a
    mov ah, al                                ; 88 c4                       ; 0xc218d
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc218f
    mov al, bl                                ; 88 d8                       ; 0xc2192
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc2194 vgabios.c:67
    xor si, si                                ; 31 f6                       ; 0xc2197
    mov es, si                                ; 8e c6                       ; 0xc2199
    mov si, word [es:bx]                      ; 26 8b 37                    ; 0xc219b
    mov bx, word [es:bx+002h]                 ; 26 8b 5f 02                 ; 0xc219e
    mov word [bp-00ch], si                    ; 89 76 f4                    ; 0xc21a2 vgabios.c:68
    mov word [bp-00ah], bx                    ; 89 5e f6                    ; 0xc21a5
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc21a8 vgabios.c:1497
    movzx cx, byte [bp+006h]                  ; 0f b6 4e 06                 ; 0xc21ab
    imul bx, cx                               ; 0f af d9                    ; 0xc21af
    movzx si, byte [bp+004h]                  ; 0f b6 76 04                 ; 0xc21b2
    imul si, bx                               ; 0f af f3                    ; 0xc21b6
    movzx bx, al                              ; 0f b6 d8                    ; 0xc21b9
    add si, bx                                ; 01 de                       ; 0xc21bc
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc21be vgabios.c:57
    mov di, strict word 00040h                ; bf 40 00                    ; 0xc21c1
    mov es, di                                ; 8e c7                       ; 0xc21c4
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc21c6
    movzx di, byte [bp+008h]                  ; 0f b6 7e 08                 ; 0xc21c9 vgabios.c:58
    imul bx, di                               ; 0f af df                    ; 0xc21cd
    add si, bx                                ; 01 de                       ; 0xc21d0
    movzx ax, ah                              ; 0f b6 c4                    ; 0xc21d2 vgabios.c:1499
    imul ax, cx                               ; 0f af c1                    ; 0xc21d5
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc21d8
    mov ax, 00f02h                            ; b8 02 0f                    ; 0xc21db vgabios.c:1500
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc21de
    out DX, ax                                ; ef                          ; 0xc21e1
    mov ax, 00205h                            ; b8 05 02                    ; 0xc21e2 vgabios.c:1501
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc21e5
    out DX, ax                                ; ef                          ; 0xc21e8
    test byte [bp-008h], 080h                 ; f6 46 f8 80                 ; 0xc21e9 vgabios.c:1502
    je short 021f5h                           ; 74 06                       ; 0xc21ed
    mov ax, 01803h                            ; b8 03 18                    ; 0xc21ef vgabios.c:1504
    out DX, ax                                ; ef                          ; 0xc21f2
    jmp short 021f9h                          ; eb 04                       ; 0xc21f3 vgabios.c:1506
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc21f5 vgabios.c:1508
    out DX, ax                                ; ef                          ; 0xc21f8
    xor ch, ch                                ; 30 ed                       ; 0xc21f9 vgabios.c:1510
    cmp ch, byte [bp+006h]                    ; 3a 6e 06                    ; 0xc21fb
    jnc short 0226ch                          ; 73 6c                       ; 0xc21fe
    movzx bx, ch                              ; 0f b6 dd                    ; 0xc2200 vgabios.c:1512
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc2203
    imul bx, ax                               ; 0f af d8                    ; 0xc2207
    add bx, si                                ; 01 f3                       ; 0xc220a
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc220c vgabios.c:1513
    jmp short 02224h                          ; eb 12                       ; 0xc2210
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2212 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2215
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc2217
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc221b vgabios.c:1526
    cmp byte [bp-006h], 008h                  ; 80 7e fa 08                 ; 0xc221e
    jnc short 02268h                          ; 73 44                       ; 0xc2222
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2224
    mov cl, al                                ; 88 c1                       ; 0xc2228
    mov ax, 00080h                            ; b8 80 00                    ; 0xc222a
    sar ax, CL                                ; d3 f8                       ; 0xc222d
    xor ah, ah                                ; 30 e4                       ; 0xc222f
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc2231
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2234
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc2237
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2239
    out DX, ax                                ; ef                          ; 0xc223c
    mov dx, bx                                ; 89 da                       ; 0xc223d
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc223f
    call 03614h                               ; e8 cf 13                    ; 0xc2242
    movzx ax, ch                              ; 0f b6 c5                    ; 0xc2245
    add ax, word [bp-00eh]                    ; 03 46 f2                    ; 0xc2248
    les di, [bp-00ch]                         ; c4 7e f4                    ; 0xc224b
    add di, ax                                ; 01 c7                       ; 0xc224e
    movzx ax, byte [es:di]                    ; 26 0f b6 05                 ; 0xc2250
    test word [bp-010h], ax                   ; 85 46 f0                    ; 0xc2254
    je short 02212h                           ; 74 b9                       ; 0xc2257
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2259
    and AL, strict byte 00fh                  ; 24 0f                       ; 0xc225c
    mov di, 0a000h                            ; bf 00 a0                    ; 0xc225e
    mov es, di                                ; 8e c7                       ; 0xc2261
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2263
    jmp short 0221bh                          ; eb b3                       ; 0xc2266
    db  0feh, 0c5h
    ; inc ch                                    ; fe c5                     ; 0xc2268 vgabios.c:1527
    jmp short 021fbh                          ; eb 8f                       ; 0xc226a
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc226c vgabios.c:1528
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc226f
    out DX, ax                                ; ef                          ; 0xc2272
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2273 vgabios.c:1529
    out DX, ax                                ; ef                          ; 0xc2276
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc2277 vgabios.c:1530
    out DX, ax                                ; ef                          ; 0xc227a
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc227b vgabios.c:1531
    pop di                                    ; 5f                          ; 0xc227e
    pop si                                    ; 5e                          ; 0xc227f
    pop bp                                    ; 5d                          ; 0xc2280
    retn 00006h                               ; c2 06 00                    ; 0xc2281
  ; disGetNextSymbol 0xc2284 LB 0x2035 -> off=0x0 cb=00000000000000dd uValue=00000000000c2284 'write_gfx_char_cga'
write_gfx_char_cga:                          ; 0xc2284 LB 0xdd
    push si                                   ; 56                          ; 0xc2284 vgabios.c:1534
    push di                                   ; 57                          ; 0xc2285
    enter 00006h, 000h                        ; c8 06 00 00                 ; 0xc2286
    mov di, 0556fh                            ; bf 6f 55                    ; 0xc228a vgabios.c:1541
    xor bh, bh                                ; 30 ff                       ; 0xc228d vgabios.c:1542
    movzx si, byte [bp+00ah]                  ; 0f b6 76 0a                 ; 0xc228f
    imul si, bx                               ; 0f af f3                    ; 0xc2293
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc2296
    imul bx, bx, 00140h                       ; 69 db 40 01                 ; 0xc2299
    add si, bx                                ; 01 de                       ; 0xc229d
    mov word [bp-004h], si                    ; 89 76 fc                    ; 0xc229f
    xor ah, ah                                ; 30 e4                       ; 0xc22a2 vgabios.c:1543
    sal ax, 003h                              ; c1 e0 03                    ; 0xc22a4
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc22a7
    xor ah, ah                                ; 30 e4                       ; 0xc22aa vgabios.c:1544
    jmp near 022cah                           ; e9 1b 00                    ; 0xc22ac
    movzx si, ah                              ; 0f b6 f4                    ; 0xc22af vgabios.c:1559
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc22b2
    add si, di                                ; 01 fe                       ; 0xc22b5
    mov al, byte [si]                         ; 8a 04                       ; 0xc22b7
    mov si, 0b800h                            ; be 00 b8                    ; 0xc22b9 vgabios.c:52
    mov es, si                                ; 8e c6                       ; 0xc22bc
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc22be
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc22c1 vgabios.c:1563
    cmp ah, 008h                              ; 80 fc 08                    ; 0xc22c3
    jnc near 0235bh                           ; 0f 83 91 00                 ; 0xc22c6
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc22ca
    sar bx, 1                                 ; d1 fb                       ; 0xc22cd
    imul bx, bx, strict byte 00050h           ; 6b db 50                    ; 0xc22cf
    add bx, word [bp-004h]                    ; 03 5e fc                    ; 0xc22d2
    test ah, 001h                             ; f6 c4 01                    ; 0xc22d5
    je short 022ddh                           ; 74 03                       ; 0xc22d8
    add bh, 020h                              ; 80 c7 20                    ; 0xc22da
    mov DH, strict byte 080h                  ; b6 80                       ; 0xc22dd
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc22df
    jne short 022fdh                          ; 75 18                       ; 0xc22e3
    test dl, dh                               ; 84 f2                       ; 0xc22e5
    je short 022afh                           ; 74 c6                       ; 0xc22e7
    mov si, 0b800h                            ; be 00 b8                    ; 0xc22e9
    mov es, si                                ; 8e c6                       ; 0xc22ec
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc22ee
    movzx si, ah                              ; 0f b6 f4                    ; 0xc22f1
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc22f4
    add si, di                                ; 01 fe                       ; 0xc22f7
    xor al, byte [si]                         ; 32 04                       ; 0xc22f9
    jmp short 022b9h                          ; eb bc                       ; 0xc22fb
    test dh, dh                               ; 84 f6                       ; 0xc22fd vgabios.c:1565
    jbe short 022c1h                          ; 76 c0                       ; 0xc22ff
    test dl, 080h                             ; f6 c2 80                    ; 0xc2301 vgabios.c:1567
    je short 02310h                           ; 74 0a                       ; 0xc2304
    mov si, 0b800h                            ; be 00 b8                    ; 0xc2306 vgabios.c:47
    mov es, si                                ; 8e c6                       ; 0xc2309
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc230b
    jmp short 02312h                          ; eb 02                       ; 0xc230e vgabios.c:1571
    xor al, al                                ; 30 c0                       ; 0xc2310 vgabios.c:1573
    mov byte [bp-002h], 000h                  ; c6 46 fe 00                 ; 0xc2312 vgabios.c:1575
    jmp short 02325h                          ; eb 0d                       ; 0xc2316
    or al, ch                                 ; 08 e8                       ; 0xc2318 vgabios.c:1585
    shr dh, 1                                 ; d0 ee                       ; 0xc231a vgabios.c:1588
    inc byte [bp-002h]                        ; fe 46 fe                    ; 0xc231c vgabios.c:1589
    cmp byte [bp-002h], 004h                  ; 80 7e fe 04                 ; 0xc231f
    jnc short 02350h                          ; 73 2b                       ; 0xc2323
    movzx si, ah                              ; 0f b6 f4                    ; 0xc2325
    add si, word [bp-006h]                    ; 03 76 fa                    ; 0xc2328
    add si, di                                ; 01 fe                       ; 0xc232b
    movzx si, byte [si]                       ; 0f b6 34                    ; 0xc232d
    movzx cx, dh                              ; 0f b6 ce                    ; 0xc2330
    test si, cx                               ; 85 ce                       ; 0xc2333
    je short 0231ah                           ; 74 e3                       ; 0xc2335
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2337
    sub cl, byte [bp-002h]                    ; 2a 4e fe                    ; 0xc2339
    mov ch, dl                                ; 88 d5                       ; 0xc233c
    and ch, 003h                              ; 80 e5 03                    ; 0xc233e
    add cl, cl                                ; 00 c9                       ; 0xc2341
    sal ch, CL                                ; d2 e5                       ; 0xc2343
    mov cl, ch                                ; 88 e9                       ; 0xc2345
    test dl, 080h                             ; f6 c2 80                    ; 0xc2347
    je short 02318h                           ; 74 cc                       ; 0xc234a
    xor al, ch                                ; 30 e8                       ; 0xc234c
    jmp short 0231ah                          ; eb ca                       ; 0xc234e
    mov cx, 0b800h                            ; b9 00 b8                    ; 0xc2350 vgabios.c:52
    mov es, cx                                ; 8e c1                       ; 0xc2353
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2355
    inc bx                                    ; 43                          ; 0xc2358 vgabios.c:1591
    jmp short 022fdh                          ; eb a2                       ; 0xc2359 vgabios.c:1592
    leave                                     ; c9                          ; 0xc235b vgabios.c:1595
    pop di                                    ; 5f                          ; 0xc235c
    pop si                                    ; 5e                          ; 0xc235d
    retn 00004h                               ; c2 04 00                    ; 0xc235e
  ; disGetNextSymbol 0xc2361 LB 0x1f58 -> off=0x0 cb=0000000000000085 uValue=00000000000c2361 'write_gfx_char_lin'
write_gfx_char_lin:                          ; 0xc2361 LB 0x85
    push si                                   ; 56                          ; 0xc2361 vgabios.c:1598
    push di                                   ; 57                          ; 0xc2362
    enter 00006h, 000h                        ; c8 06 00 00                 ; 0xc2363
    mov dh, dl                                ; 88 d6                       ; 0xc2367
    mov word [bp-002h], 0556fh                ; c7 46 fe 6f 55              ; 0xc2369 vgabios.c:1605
    movzx si, cl                              ; 0f b6 f1                    ; 0xc236e vgabios.c:1606
    movzx cx, byte [bp+008h]                  ; 0f b6 4e 08                 ; 0xc2371
    imul cx, si                               ; 0f af ce                    ; 0xc2375
    sal cx, 006h                              ; c1 e1 06                    ; 0xc2378
    xor bh, bh                                ; 30 ff                       ; 0xc237b
    sal bx, 003h                              ; c1 e3 03                    ; 0xc237d
    add bx, cx                                ; 01 cb                       ; 0xc2380
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc2382
    xor ah, ah                                ; 30 e4                       ; 0xc2385 vgabios.c:1607
    mov si, ax                                ; 89 c6                       ; 0xc2387
    sal si, 003h                              ; c1 e6 03                    ; 0xc2389
    xor al, al                                ; 30 c0                       ; 0xc238c vgabios.c:1608
    jmp short 023c5h                          ; eb 35                       ; 0xc238e
    cmp ah, 008h                              ; 80 fc 08                    ; 0xc2390 vgabios.c:1612
    jnc short 023bfh                          ; 73 2a                       ; 0xc2393
    xor cl, cl                                ; 30 c9                       ; 0xc2395 vgabios.c:1614
    movzx bx, al                              ; 0f b6 d8                    ; 0xc2397 vgabios.c:1615
    add bx, si                                ; 01 f3                       ; 0xc239a
    add bx, word [bp-002h]                    ; 03 5e fe                    ; 0xc239c
    movzx bx, byte [bx]                       ; 0f b6 1f                    ; 0xc239f
    movzx di, dl                              ; 0f b6 fa                    ; 0xc23a2
    test bx, di                               ; 85 fb                       ; 0xc23a5
    je short 023abh                           ; 74 02                       ; 0xc23a7
    mov cl, dh                                ; 88 f1                       ; 0xc23a9 vgabios.c:1617
    movzx bx, ah                              ; 0f b6 dc                    ; 0xc23ab vgabios.c:1619
    add bx, word [bp-006h]                    ; 03 5e fa                    ; 0xc23ae
    mov di, 0a000h                            ; bf 00 a0                    ; 0xc23b1 vgabios.c:52
    mov es, di                                ; 8e c7                       ; 0xc23b4
    mov byte [es:bx], cl                      ; 26 88 0f                    ; 0xc23b6
    shr dl, 1                                 ; d0 ea                       ; 0xc23b9 vgabios.c:1620
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc23bb vgabios.c:1621
    jmp short 02390h                          ; eb d1                       ; 0xc23bd
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc23bf vgabios.c:1622
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc23c1
    jnc short 023e0h                          ; 73 1b                       ; 0xc23c3
    movzx cx, al                              ; 0f b6 c8                    ; 0xc23c5
    movzx bx, byte [bp+008h]                  ; 0f b6 5e 08                 ; 0xc23c8
    imul bx, cx                               ; 0f af d9                    ; 0xc23cc
    sal bx, 003h                              ; c1 e3 03                    ; 0xc23cf
    mov cx, word [bp-004h]                    ; 8b 4e fc                    ; 0xc23d2
    add cx, bx                                ; 01 d9                       ; 0xc23d5
    mov word [bp-006h], cx                    ; 89 4e fa                    ; 0xc23d7
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc23da
    xor ah, ah                                ; 30 e4                       ; 0xc23dc
    jmp short 02395h                          ; eb b5                       ; 0xc23de
    leave                                     ; c9                          ; 0xc23e0 vgabios.c:1623
    pop di                                    ; 5f                          ; 0xc23e1
    pop si                                    ; 5e                          ; 0xc23e2
    retn 00002h                               ; c2 02 00                    ; 0xc23e3
  ; disGetNextSymbol 0xc23e6 LB 0x1ed3 -> off=0x0 cb=0000000000000165 uValue=00000000000c23e6 'biosfn_write_char_attr'
biosfn_write_char_attr:                      ; 0xc23e6 LB 0x165
    push bp                                   ; 55                          ; 0xc23e6 vgabios.c:1626
    mov bp, sp                                ; 89 e5                       ; 0xc23e7
    push si                                   ; 56                          ; 0xc23e9
    push di                                   ; 57                          ; 0xc23ea
    sub sp, strict byte 00018h                ; 83 ec 18                    ; 0xc23eb
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc23ee
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc23f1
    mov byte [bp-012h], bl                    ; 88 5e ee                    ; 0xc23f4
    mov si, cx                                ; 89 ce                       ; 0xc23f7
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc23f9 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc23fc
    mov es, ax                                ; 8e c0                       ; 0xc23ff
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2401
    xor ah, ah                                ; 30 e4                       ; 0xc2404 vgabios.c:1634
    call 035edh                               ; e8 e4 11                    ; 0xc2406
    mov cl, al                                ; 88 c1                       ; 0xc2409
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc240b
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc240e vgabios.c:1635
    je near 02544h                            ; 0f 84 30 01                 ; 0xc2410
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc2414 vgabios.c:1638
    lea bx, [bp-01ch]                         ; 8d 5e e4                    ; 0xc2417
    lea dx, [bp-01ah]                         ; 8d 56 e6                    ; 0xc241a
    call 00a93h                               ; e8 73 e6                    ; 0xc241d
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc2420 vgabios.c:1639
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc2423
    mov dx, word [bp-01ch]                    ; 8b 56 e4                    ; 0xc2426
    xor dl, dl                                ; 30 d2                       ; 0xc2429
    shr dx, 008h                              ; c1 ea 08                    ; 0xc242b
    mov byte [bp-014h], dl                    ; 88 56 ec                    ; 0xc242e
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2431 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2434
    mov es, ax                                ; 8e c0                       ; 0xc2437
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2439
    xor ah, ah                                ; 30 e4                       ; 0xc243c vgabios.c:48
    inc ax                                    ; 40                          ; 0xc243e
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc243f
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2442 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc2445
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc2448 vgabios.c:58
    movzx bx, cl                              ; 0f b6 d9                    ; 0xc244b vgabios.c:1645
    mov di, bx                                ; 89 df                       ; 0xc244e
    sal di, 003h                              ; c1 e7 03                    ; 0xc2450
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc2453
    jne short 024a0h                          ; 75 46                       ; 0xc2458
    mov bx, word [bp-018h]                    ; 8b 5e e8                    ; 0xc245a vgabios.c:1648
    imul bx, ax                               ; 0f af d8                    ; 0xc245d
    add bx, bx                                ; 01 db                       ; 0xc2460
    or bl, 0ffh                               ; 80 cb ff                    ; 0xc2462
    movzx cx, byte [bp-00eh]                  ; 0f b6 4e f2                 ; 0xc2465
    inc bx                                    ; 43                          ; 0xc2469
    imul bx, cx                               ; 0f af d9                    ; 0xc246a
    xor dh, dh                                ; 30 f6                       ; 0xc246d
    imul ax, dx                               ; 0f af c2                    ; 0xc246f
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc2472
    add ax, dx                                ; 01 d0                       ; 0xc2476
    add ax, ax                                ; 01 c0                       ; 0xc2478
    mov dx, bx                                ; 89 da                       ; 0xc247a
    add dx, ax                                ; 01 c2                       ; 0xc247c
    movzx ax, byte [bp-012h]                  ; 0f b6 46 ee                 ; 0xc247e vgabios.c:1650
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2482
    movzx bx, byte [bp-008h]                  ; 0f b6 5e f8                 ; 0xc2485
    add ax, bx                                ; 01 d8                       ; 0xc2489
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc248b
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc248e vgabios.c:1651
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc2491
    mov cx, si                                ; 89 f1                       ; 0xc2495
    mov di, dx                                ; 89 d7                       ; 0xc2497
    jcxz 0249dh                               ; e3 02                       ; 0xc2499
    rep stosw                                 ; f3 ab                       ; 0xc249b
    jmp near 02544h                           ; e9 a4 00                    ; 0xc249d vgabios.c:1653
    movzx bx, byte [bx+04831h]                ; 0f b6 9f 31 48              ; 0xc24a0 vgabios.c:1656
    sal bx, 006h                              ; c1 e3 06                    ; 0xc24a5
    mov al, byte [bx+04847h]                  ; 8a 87 47 48                 ; 0xc24a8
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc24ac
    mov al, byte [di+047b4h]                  ; 8a 85 b4 47                 ; 0xc24af vgabios.c:1657
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc24b3
    dec si                                    ; 4e                          ; 0xc24b6 vgabios.c:1658
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc24b7
    je near 02544h                            ; 0f 84 86 00                 ; 0xc24ba
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc24be vgabios.c:1660
    sal bx, 003h                              ; c1 e3 03                    ; 0xc24c2
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc24c5
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc24c9
    jc short 024d9h                           ; 72 0c                       ; 0xc24cb
    jbe short 024dfh                          ; 76 10                       ; 0xc24cd
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc24cf
    je short 02526h                           ; 74 53                       ; 0xc24d1
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc24d3
    je short 024e3h                           ; 74 0c                       ; 0xc24d5
    jmp short 0253eh                          ; eb 65                       ; 0xc24d7
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc24d9
    je short 02507h                           ; 74 2a                       ; 0xc24db
    jmp short 0253eh                          ; eb 5f                       ; 0xc24dd
    or byte [bp-012h], 001h                   ; 80 4e ee 01                 ; 0xc24df vgabios.c:1663
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc24e3 vgabios.c:1665
    push ax                                   ; 50                          ; 0xc24e7
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc24e8
    push ax                                   ; 50                          ; 0xc24ec
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc24ed
    push ax                                   ; 50                          ; 0xc24f1
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc24f2
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc24f6
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc24fa
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc24fe
    call 02185h                               ; e8 80 fc                    ; 0xc2502
    jmp short 0253eh                          ; eb 37                       ; 0xc2505 vgabios.c:1666
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc2507 vgabios.c:1668
    push ax                                   ; 50                          ; 0xc250b
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc250c
    push ax                                   ; 50                          ; 0xc2510
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc2511
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc2515
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc2519
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc251d
    call 02284h                               ; e8 60 fd                    ; 0xc2521
    jmp short 0253eh                          ; eb 18                       ; 0xc2524 vgabios.c:1669
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc2526 vgabios.c:1671
    push ax                                   ; 50                          ; 0xc252a
    movzx cx, byte [bp-014h]                  ; 0f b6 4e ec                 ; 0xc252b
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc252f
    movzx dx, byte [bp-012h]                  ; 0f b6 56 ee                 ; 0xc2533
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2537
    call 02361h                               ; e8 23 fe                    ; 0xc253b
    inc byte [bp-010h]                        ; fe 46 f0                    ; 0xc253e vgabios.c:1678
    jmp near 024b6h                           ; e9 72 ff                    ; 0xc2541 vgabios.c:1679
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2544 vgabios.c:1681
    pop di                                    ; 5f                          ; 0xc2547
    pop si                                    ; 5e                          ; 0xc2548
    pop bp                                    ; 5d                          ; 0xc2549
    retn                                      ; c3                          ; 0xc254a
  ; disGetNextSymbol 0xc254b LB 0x1d6e -> off=0x0 cb=0000000000000162 uValue=00000000000c254b 'biosfn_write_char_only'
biosfn_write_char_only:                      ; 0xc254b LB 0x162
    push bp                                   ; 55                          ; 0xc254b vgabios.c:1684
    mov bp, sp                                ; 89 e5                       ; 0xc254c
    push si                                   ; 56                          ; 0xc254e
    push di                                   ; 57                          ; 0xc254f
    sub sp, strict byte 00016h                ; 83 ec 16                    ; 0xc2550
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2553
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc2556
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc2559
    mov si, cx                                ; 89 ce                       ; 0xc255c
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc255e vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2561
    mov es, ax                                ; 8e c0                       ; 0xc2564
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2566
    xor ah, ah                                ; 30 e4                       ; 0xc2569 vgabios.c:1692
    call 035edh                               ; e8 7f 10                    ; 0xc256b
    mov cl, al                                ; 88 c1                       ; 0xc256e
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc2570
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc2573 vgabios.c:1693
    je near 026a6h                            ; 0f 84 2d 01                 ; 0xc2575
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc2579 vgabios.c:1696
    lea bx, [bp-01ah]                         ; 8d 5e e6                    ; 0xc257c
    lea dx, [bp-018h]                         ; 8d 56 e8                    ; 0xc257f
    call 00a93h                               ; e8 0e e5                    ; 0xc2582
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc2585 vgabios.c:1697
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc2588
    mov dx, word [bp-01ah]                    ; 8b 56 e6                    ; 0xc258b
    xor dl, dl                                ; 30 d2                       ; 0xc258e
    shr dx, 008h                              ; c1 ea 08                    ; 0xc2590
    mov byte [bp-00ah], dl                    ; 88 56 f6                    ; 0xc2593
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2596 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2599
    mov es, ax                                ; 8e c0                       ; 0xc259c
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc259e
    xor ah, ah                                ; 30 e4                       ; 0xc25a1 vgabios.c:48
    mov di, ax                                ; 89 c7                       ; 0xc25a3
    inc di                                    ; 47                          ; 0xc25a5
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc25a6 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc25a9
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc25ac vgabios.c:58
    xor ch, ch                                ; 30 ed                       ; 0xc25af vgabios.c:1703
    mov bx, cx                                ; 89 cb                       ; 0xc25b1
    sal bx, 003h                              ; c1 e3 03                    ; 0xc25b3
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc25b6
    jne short 025fah                          ; 75 3d                       ; 0xc25bb
    imul di, ax                               ; 0f af f8                    ; 0xc25bd vgabios.c:1706
    add di, di                                ; 01 ff                       ; 0xc25c0
    or di, 000ffh                             ; 81 cf ff 00                 ; 0xc25c2
    movzx bx, byte [bp-00eh]                  ; 0f b6 5e f2                 ; 0xc25c6
    inc di                                    ; 47                          ; 0xc25ca
    imul bx, di                               ; 0f af df                    ; 0xc25cb
    xor dh, dh                                ; 30 f6                       ; 0xc25ce
    imul ax, dx                               ; 0f af c2                    ; 0xc25d0
    movzx dx, byte [bp-010h]                  ; 0f b6 56 f0                 ; 0xc25d3
    add ax, dx                                ; 01 d0                       ; 0xc25d7
    add ax, ax                                ; 01 c0                       ; 0xc25d9
    add bx, ax                                ; 01 c3                       ; 0xc25db
    dec si                                    ; 4e                          ; 0xc25dd vgabios.c:1708
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc25de
    je near 026a6h                            ; 0f 84 c1 00                 ; 0xc25e1
    movzx di, byte [bp-012h]                  ; 0f b6 7e ee                 ; 0xc25e5 vgabios.c:1709
    sal di, 003h                              ; c1 e7 03                    ; 0xc25e9
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc25ec vgabios.c:50
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc25f0
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc25f3
    inc bx                                    ; 43                          ; 0xc25f6 vgabios.c:1710
    inc bx                                    ; 43                          ; 0xc25f7
    jmp short 025ddh                          ; eb e3                       ; 0xc25f8 vgabios.c:1711
    mov di, cx                                ; 89 cf                       ; 0xc25fa vgabios.c:1716
    movzx ax, byte [di+04831h]                ; 0f b6 85 31 48              ; 0xc25fc
    mov di, ax                                ; 89 c7                       ; 0xc2601
    sal di, 006h                              ; c1 e7 06                    ; 0xc2603
    mov al, byte [di+04847h]                  ; 8a 85 47 48                 ; 0xc2606
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc260a
    mov al, byte [bx+047b4h]                  ; 8a 87 b4 47                 ; 0xc260d vgabios.c:1717
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc2611
    dec si                                    ; 4e                          ; 0xc2614 vgabios.c:1718
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc2615
    je near 026a6h                            ; 0f 84 8a 00                 ; 0xc2618
    movzx bx, byte [bp-012h]                  ; 0f b6 5e ee                 ; 0xc261c vgabios.c:1720
    sal bx, 003h                              ; c1 e3 03                    ; 0xc2620
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc2623
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc2627
    jc short 0263ah                           ; 72 0e                       ; 0xc262a
    jbe short 02641h                          ; 76 13                       ; 0xc262c
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc262e
    je short 02688h                           ; 74 55                       ; 0xc2631
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2633
    je short 02645h                           ; 74 0d                       ; 0xc2636
    jmp short 026a0h                          ; eb 66                       ; 0xc2638
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc263a
    je short 02669h                           ; 74 2a                       ; 0xc263d
    jmp short 026a0h                          ; eb 5f                       ; 0xc263f
    or byte [bp-006h], 001h                   ; 80 4e fa 01                 ; 0xc2641 vgabios.c:1723
    movzx ax, byte [bp-00eh]                  ; 0f b6 46 f2                 ; 0xc2645 vgabios.c:1725
    push ax                                   ; 50                          ; 0xc2649
    movzx ax, byte [bp-00ch]                  ; 0f b6 46 f4                 ; 0xc264a
    push ax                                   ; 50                          ; 0xc264e
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc264f
    push ax                                   ; 50                          ; 0xc2653
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc2654
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc2658
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc265c
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2660
    call 02185h                               ; e8 1e fb                    ; 0xc2664
    jmp short 026a0h                          ; eb 37                       ; 0xc2667 vgabios.c:1726
    movzx ax, byte [bp-014h]                  ; 0f b6 46 ec                 ; 0xc2669 vgabios.c:1728
    push ax                                   ; 50                          ; 0xc266d
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc266e
    push ax                                   ; 50                          ; 0xc2672
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc2673
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc2677
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc267b
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc267f
    call 02284h                               ; e8 fe fb                    ; 0xc2683
    jmp short 026a0h                          ; eb 18                       ; 0xc2686 vgabios.c:1729
    movzx ax, byte [bp-016h]                  ; 0f b6 46 ea                 ; 0xc2688 vgabios.c:1731
    push ax                                   ; 50                          ; 0xc268c
    movzx cx, byte [bp-00ah]                  ; 0f b6 4e f6                 ; 0xc268d
    movzx bx, byte [bp-010h]                  ; 0f b6 5e f0                 ; 0xc2691
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc2695
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc2699
    call 02361h                               ; e8 c1 fc                    ; 0xc269d
    inc byte [bp-010h]                        ; fe 46 f0                    ; 0xc26a0 vgabios.c:1738
    jmp near 02614h                           ; e9 6e ff                    ; 0xc26a3 vgabios.c:1739
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc26a6 vgabios.c:1741
    pop di                                    ; 5f                          ; 0xc26a9
    pop si                                    ; 5e                          ; 0xc26aa
    pop bp                                    ; 5d                          ; 0xc26ab
    retn                                      ; c3                          ; 0xc26ac
  ; disGetNextSymbol 0xc26ad LB 0x1c0c -> off=0x0 cb=0000000000000165 uValue=00000000000c26ad 'biosfn_write_pixel'
biosfn_write_pixel:                          ; 0xc26ad LB 0x165
    push bp                                   ; 55                          ; 0xc26ad vgabios.c:1744
    mov bp, sp                                ; 89 e5                       ; 0xc26ae
    push si                                   ; 56                          ; 0xc26b0
    push ax                                   ; 50                          ; 0xc26b1
    push ax                                   ; 50                          ; 0xc26b2
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc26b3
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc26b6
    mov dx, bx                                ; 89 da                       ; 0xc26b9
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc26bb vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc26be
    mov es, ax                                ; 8e c0                       ; 0xc26c1
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc26c3
    xor ah, ah                                ; 30 e4                       ; 0xc26c6 vgabios.c:1751
    call 035edh                               ; e8 22 0f                    ; 0xc26c8
    mov ah, al                                ; 88 c4                       ; 0xc26cb
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc26cd vgabios.c:1752
    je near 027edh                            ; 0f 84 1a 01                 ; 0xc26cf
    movzx bx, al                              ; 0f b6 d8                    ; 0xc26d3 vgabios.c:1753
    sal bx, 003h                              ; c1 e3 03                    ; 0xc26d6
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc26d9
    je near 027edh                            ; 0f 84 0b 01                 ; 0xc26de
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc26e2 vgabios.c:1755
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc26e6
    jc short 026f9h                           ; 72 0f                       ; 0xc26e8
    jbe short 02700h                          ; 76 14                       ; 0xc26ea
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc26ec
    je near 027f3h                            ; 0f 84 01 01                 ; 0xc26ee
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc26f2
    je short 02700h                           ; 74 0a                       ; 0xc26f4
    jmp near 027edh                           ; e9 f4 00                    ; 0xc26f6
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc26f9
    je short 0276fh                           ; 74 72                       ; 0xc26fb
    jmp near 027edh                           ; e9 ed 00                    ; 0xc26fd
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2700 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2703
    mov es, ax                                ; 8e c0                       ; 0xc2706
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc2708
    imul ax, cx                               ; 0f af c1                    ; 0xc270b vgabios.c:58
    mov bx, dx                                ; 89 d3                       ; 0xc270e
    shr bx, 003h                              ; c1 eb 03                    ; 0xc2710
    add bx, ax                                ; 01 c3                       ; 0xc2713
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc2715 vgabios.c:57
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc2718
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc271b vgabios.c:58
    imul ax, cx                               ; 0f af c1                    ; 0xc271f
    add bx, ax                                ; 01 c3                       ; 0xc2722
    mov cl, dl                                ; 88 d1                       ; 0xc2724 vgabios.c:1761
    and cl, 007h                              ; 80 e1 07                    ; 0xc2726
    mov ax, 00080h                            ; b8 80 00                    ; 0xc2729
    sar ax, CL                                ; d3 f8                       ; 0xc272c
    xor ah, ah                                ; 30 e4                       ; 0xc272e vgabios.c:1762
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2730
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc2733
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2735
    out DX, ax                                ; ef                          ; 0xc2738
    mov ax, 00205h                            ; b8 05 02                    ; 0xc2739 vgabios.c:1763
    out DX, ax                                ; ef                          ; 0xc273c
    mov dx, bx                                ; 89 da                       ; 0xc273d vgabios.c:1764
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc273f
    call 03614h                               ; e8 cf 0e                    ; 0xc2742
    test byte [bp-006h], 080h                 ; f6 46 fa 80                 ; 0xc2745 vgabios.c:1765
    je short 02752h                           ; 74 07                       ; 0xc2749
    mov ax, 01803h                            ; b8 03 18                    ; 0xc274b vgabios.c:1767
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc274e
    out DX, ax                                ; ef                          ; 0xc2751
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2752 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2755
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2757
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc275a
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc275d vgabios.c:1770
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2760
    out DX, ax                                ; ef                          ; 0xc2763
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2764 vgabios.c:1771
    out DX, ax                                ; ef                          ; 0xc2767
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc2768 vgabios.c:1772
    out DX, ax                                ; ef                          ; 0xc276b
    jmp near 027edh                           ; e9 7e 00                    ; 0xc276c vgabios.c:1773
    mov si, cx                                ; 89 ce                       ; 0xc276f vgabios.c:1775
    shr si, 1                                 ; d1 ee                       ; 0xc2771
    imul si, si, strict byte 00050h           ; 6b f6 50                    ; 0xc2773
    cmp al, byte [bx+047b4h]                  ; 3a 87 b4 47                 ; 0xc2776
    jne short 02783h                          ; 75 07                       ; 0xc277a
    mov bx, dx                                ; 89 d3                       ; 0xc277c vgabios.c:1777
    shr bx, 002h                              ; c1 eb 02                    ; 0xc277e
    jmp short 02788h                          ; eb 05                       ; 0xc2781 vgabios.c:1779
    mov bx, dx                                ; 89 d3                       ; 0xc2783 vgabios.c:1781
    shr bx, 003h                              ; c1 eb 03                    ; 0xc2785
    add bx, si                                ; 01 f3                       ; 0xc2788
    test cl, 001h                             ; f6 c1 01                    ; 0xc278a vgabios.c:1783
    je short 02792h                           ; 74 03                       ; 0xc278d
    add bh, 020h                              ; 80 c7 20                    ; 0xc278f
    mov cx, 0b800h                            ; b9 00 b8                    ; 0xc2792 vgabios.c:47
    mov es, cx                                ; 8e c1                       ; 0xc2795
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2797
    movzx si, ah                              ; 0f b6 f4                    ; 0xc279a vgabios.c:1785
    sal si, 003h                              ; c1 e6 03                    ; 0xc279d
    cmp byte [si+047b4h], 002h                ; 80 bc b4 47 02              ; 0xc27a0
    jne short 027beh                          ; 75 17                       ; 0xc27a5
    mov ah, dl                                ; 88 d4                       ; 0xc27a7 vgabios.c:1787
    and ah, 003h                              ; 80 e4 03                    ; 0xc27a9
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc27ac
    sub cl, ah                                ; 28 e1                       ; 0xc27ae
    add cl, cl                                ; 00 c9                       ; 0xc27b0
    mov dh, byte [bp-006h]                    ; 8a 76 fa                    ; 0xc27b2
    and dh, 003h                              ; 80 e6 03                    ; 0xc27b5
    sal dh, CL                                ; d2 e6                       ; 0xc27b8
    mov DL, strict byte 003h                  ; b2 03                       ; 0xc27ba vgabios.c:1788
    jmp short 027d1h                          ; eb 13                       ; 0xc27bc vgabios.c:1790
    mov ah, dl                                ; 88 d4                       ; 0xc27be vgabios.c:1792
    and ah, 007h                              ; 80 e4 07                    ; 0xc27c0
    mov CL, strict byte 007h                  ; b1 07                       ; 0xc27c3
    sub cl, ah                                ; 28 e1                       ; 0xc27c5
    mov dh, byte [bp-006h]                    ; 8a 76 fa                    ; 0xc27c7
    and dh, 001h                              ; 80 e6 01                    ; 0xc27ca
    sal dh, CL                                ; d2 e6                       ; 0xc27cd
    mov DL, strict byte 001h                  ; b2 01                       ; 0xc27cf vgabios.c:1793
    sal dl, CL                                ; d2 e2                       ; 0xc27d1
    test byte [bp-006h], 080h                 ; f6 46 fa 80                 ; 0xc27d3 vgabios.c:1795
    je short 027ddh                           ; 74 04                       ; 0xc27d7
    xor al, dh                                ; 30 f0                       ; 0xc27d9 vgabios.c:1797
    jmp short 027e5h                          ; eb 08                       ; 0xc27db vgabios.c:1799
    mov ah, dl                                ; 88 d4                       ; 0xc27dd vgabios.c:1801
    not ah                                    ; f6 d4                       ; 0xc27df
    and al, ah                                ; 20 e0                       ; 0xc27e1
    or al, dh                                 ; 08 f0                       ; 0xc27e3 vgabios.c:1802
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc27e5 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc27e8
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc27ea
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc27ed vgabios.c:1805
    pop si                                    ; 5e                          ; 0xc27f0
    pop bp                                    ; 5d                          ; 0xc27f1
    retn                                      ; c3                          ; 0xc27f2
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc27f3 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc27f6
    mov es, ax                                ; 8e c0                       ; 0xc27f9
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc27fb
    sal ax, 003h                              ; c1 e0 03                    ; 0xc27fe vgabios.c:58
    imul ax, cx                               ; 0f af c1                    ; 0xc2801
    mov bx, dx                                ; 89 d3                       ; 0xc2804
    add bx, ax                                ; 01 c3                       ; 0xc2806
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2808 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc280b
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc280d
    jmp short 027eah                          ; eb d8                       ; 0xc2810
  ; disGetNextSymbol 0xc2812 LB 0x1aa7 -> off=0x0 cb=000000000000024a uValue=00000000000c2812 'biosfn_write_teletype'
biosfn_write_teletype:                       ; 0xc2812 LB 0x24a
    push bp                                   ; 55                          ; 0xc2812 vgabios.c:1818
    mov bp, sp                                ; 89 e5                       ; 0xc2813
    push si                                   ; 56                          ; 0xc2815
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc2816
    mov ch, al                                ; 88 c5                       ; 0xc2819
    mov byte [bp-00ah], dl                    ; 88 56 f6                    ; 0xc281b
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc281e
    cmp dl, 0ffh                              ; 80 fa ff                    ; 0xc2821 vgabios.c:1826
    jne short 02834h                          ; 75 0e                       ; 0xc2824
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc2826 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2829
    mov es, ax                                ; 8e c0                       ; 0xc282c
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc282e
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2831 vgabios.c:48
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc2834 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2837
    mov es, ax                                ; 8e c0                       ; 0xc283a
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc283c
    xor ah, ah                                ; 30 e4                       ; 0xc283f vgabios.c:1831
    call 035edh                               ; e8 a9 0d                    ; 0xc2841
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc2844
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc2847 vgabios.c:1832
    je near 02a56h                            ; 0f 84 09 02                 ; 0xc2849
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc284d vgabios.c:1835
    lea bx, [bp-012h]                         ; 8d 5e ee                    ; 0xc2851
    lea dx, [bp-014h]                         ; 8d 56 ec                    ; 0xc2854
    call 00a93h                               ; e8 39 e2                    ; 0xc2857
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc285a vgabios.c:1836
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc285d
    mov ax, word [bp-012h]                    ; 8b 46 ee                    ; 0xc2860
    xor al, al                                ; 30 c0                       ; 0xc2863
    shr ax, 008h                              ; c1 e8 08                    ; 0xc2865
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc2868
    mov bx, 00084h                            ; bb 84 00                    ; 0xc286b vgabios.c:47
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc286e
    mov es, dx                                ; 8e c2                       ; 0xc2871
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc2873
    xor dh, dh                                ; 30 f6                       ; 0xc2876 vgabios.c:48
    inc dx                                    ; 42                          ; 0xc2878
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc2879
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc287c vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc287f
    mov word [bp-010h], dx                    ; 89 56 f0                    ; 0xc2882 vgabios.c:58
    cmp ch, 008h                              ; 80 fd 08                    ; 0xc2885 vgabios.c:1842
    jc short 02898h                           ; 72 0e                       ; 0xc2888
    jbe short 028a1h                          ; 76 15                       ; 0xc288a
    cmp ch, 00dh                              ; 80 fd 0d                    ; 0xc288c
    je short 028b7h                           ; 74 26                       ; 0xc288f
    cmp ch, 00ah                              ; 80 fd 0a                    ; 0xc2891
    je short 028afh                           ; 74 19                       ; 0xc2894
    jmp short 028beh                          ; eb 26                       ; 0xc2896
    cmp ch, 007h                              ; 80 fd 07                    ; 0xc2898
    je near 029b2h                            ; 0f 84 13 01                 ; 0xc289b
    jmp short 028beh                          ; eb 1d                       ; 0xc289f
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc28a1 vgabios.c:1849
    jbe near 029b2h                           ; 0f 86 09 01                 ; 0xc28a5
    dec byte [bp-006h]                        ; fe 4e fa                    ; 0xc28a9
    jmp near 029b2h                           ; e9 03 01                    ; 0xc28ac vgabios.c:1850
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc28af vgabios.c:1853
    mov byte [bp-004h], al                    ; 88 46 fc                    ; 0xc28b1
    jmp near 029b2h                           ; e9 fb 00                    ; 0xc28b4 vgabios.c:1854
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc28b7 vgabios.c:1857
    jmp near 029b2h                           ; e9 f4 00                    ; 0xc28bb vgabios.c:1858
    movzx si, byte [bp-00ch]                  ; 0f b6 76 f4                 ; 0xc28be vgabios.c:1862
    mov bx, si                                ; 89 f3                       ; 0xc28c2
    sal bx, 003h                              ; c1 e3 03                    ; 0xc28c4
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc28c7
    jne short 02911h                          ; 75 43                       ; 0xc28cc
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc28ce vgabios.c:1865
    imul ax, word [bp-00eh]                   ; 0f af 46 f2                 ; 0xc28d1
    add ax, ax                                ; 01 c0                       ; 0xc28d5
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc28d7
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc28d9
    mov si, ax                                ; 89 c6                       ; 0xc28dd
    inc si                                    ; 46                          ; 0xc28df
    imul si, dx                               ; 0f af f2                    ; 0xc28e0
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc28e3
    imul ax, word [bp-010h]                   ; 0f af 46 f0                 ; 0xc28e7
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc28eb
    add ax, dx                                ; 01 d0                       ; 0xc28ef
    add ax, ax                                ; 01 c0                       ; 0xc28f1
    add si, ax                                ; 01 c6                       ; 0xc28f3
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc28f5 vgabios.c:50
    mov byte [es:si], ch                      ; 26 88 2c                    ; 0xc28f9
    cmp cl, 003h                              ; 80 f9 03                    ; 0xc28fc vgabios.c:1870
    jne near 0299fh                           ; 0f 85 9c 00                 ; 0xc28ff
    inc si                                    ; 46                          ; 0xc2903 vgabios.c:1871
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2904 vgabios.c:50
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2908
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc290b
    jmp near 0299fh                           ; e9 8e 00                    ; 0xc290e vgabios.c:1873
    movzx si, byte [si+04831h]                ; 0f b6 b4 31 48              ; 0xc2911 vgabios.c:1876
    sal si, 006h                              ; c1 e6 06                    ; 0xc2916
    mov ah, byte [si+04847h]                  ; 8a a4 47 48                 ; 0xc2919
    mov dl, byte [bx+047b4h]                  ; 8a 97 b4 47                 ; 0xc291d vgabios.c:1877
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc2921 vgabios.c:1878
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc2925
    jc short 02935h                           ; 72 0c                       ; 0xc2927
    jbe short 0293bh                          ; 76 10                       ; 0xc2929
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc292b
    je short 02986h                           ; 74 57                       ; 0xc292d
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc292f
    je short 0293fh                           ; 74 0c                       ; 0xc2931
    jmp short 0299fh                          ; eb 6a                       ; 0xc2933
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc2935
    je short 02965h                           ; 74 2c                       ; 0xc2937
    jmp short 0299fh                          ; eb 64                       ; 0xc2939
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc293b vgabios.c:1881
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc293f vgabios.c:1883
    push dx                                   ; 52                          ; 0xc2943
    movzx ax, ah                              ; 0f b6 c4                    ; 0xc2944
    push ax                                   ; 50                          ; 0xc2947
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2948
    push ax                                   ; 50                          ; 0xc294c
    movzx bx, byte [bp-004h]                  ; 0f b6 5e fc                 ; 0xc294d
    movzx si, byte [bp-006h]                  ; 0f b6 76 fa                 ; 0xc2951
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc2955
    movzx ax, ch                              ; 0f b6 c5                    ; 0xc2959
    mov cx, bx                                ; 89 d9                       ; 0xc295c
    mov bx, si                                ; 89 f3                       ; 0xc295e
    call 02185h                               ; e8 22 f8                    ; 0xc2960
    jmp short 0299fh                          ; eb 3a                       ; 0xc2963 vgabios.c:1884
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc2965 vgabios.c:1886
    push ax                                   ; 50                          ; 0xc2968
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2969
    push ax                                   ; 50                          ; 0xc296d
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc296e
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc2972
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc2976
    movzx si, ch                              ; 0f b6 f5                    ; 0xc297a
    mov cx, ax                                ; 89 c1                       ; 0xc297d
    mov ax, si                                ; 89 f0                       ; 0xc297f
    call 02284h                               ; e8 00 f9                    ; 0xc2981
    jmp short 0299fh                          ; eb 19                       ; 0xc2984 vgabios.c:1887
    movzx ax, byte [bp-010h]                  ; 0f b6 46 f0                 ; 0xc2986 vgabios.c:1889
    push ax                                   ; 50                          ; 0xc298a
    movzx si, byte [bp-004h]                  ; 0f b6 76 fc                 ; 0xc298b
    movzx bx, byte [bp-006h]                  ; 0f b6 5e fa                 ; 0xc298f
    movzx dx, byte [bp-008h]                  ; 0f b6 56 f8                 ; 0xc2993
    movzx ax, ch                              ; 0f b6 c5                    ; 0xc2997
    mov cx, si                                ; 89 f1                       ; 0xc299a
    call 02361h                               ; e8 c2 f9                    ; 0xc299c
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc299f vgabios.c:1897
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc29a2 vgabios.c:1899
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc29a6
    jne short 029b2h                          ; 75 07                       ; 0xc29a9
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc29ab vgabios.c:1900
    inc byte [bp-004h]                        ; fe 46 fc                    ; 0xc29af vgabios.c:1901
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc29b2 vgabios.c:1906
    cmp ax, word [bp-00eh]                    ; 3b 46 f2                    ; 0xc29b6
    jne near 02a3ah                           ; 0f 85 7d 00                 ; 0xc29b9
    movzx bx, byte [bp-00ch]                  ; 0f b6 5e f4                 ; 0xc29bd vgabios.c:1908
    sal bx, 003h                              ; c1 e3 03                    ; 0xc29c1
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc29c4
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc29c7
    mov ah, byte [bp-010h]                    ; 8a 66 f0                    ; 0xc29c9
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc29cc
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc29ce
    jne short 02a1dh                          ; 75 48                       ; 0xc29d3
    mov dx, word [bp-010h]                    ; 8b 56 f0                    ; 0xc29d5 vgabios.c:1910
    imul dx, word [bp-00eh]                   ; 0f af 56 f2                 ; 0xc29d8
    add dx, dx                                ; 01 d2                       ; 0xc29dc
    or dl, 0ffh                               ; 80 ca ff                    ; 0xc29de
    movzx si, byte [bp-00ah]                  ; 0f b6 76 f6                 ; 0xc29e1
    inc dx                                    ; 42                          ; 0xc29e5
    imul si, dx                               ; 0f af f2                    ; 0xc29e6
    movzx dx, byte [bp-004h]                  ; 0f b6 56 fc                 ; 0xc29e9
    dec dx                                    ; 4a                          ; 0xc29ed
    mov cx, word [bp-010h]                    ; 8b 4e f0                    ; 0xc29ee
    imul cx, dx                               ; 0f af ca                    ; 0xc29f1
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc29f4
    add dx, cx                                ; 01 ca                       ; 0xc29f8
    add dx, dx                                ; 01 d2                       ; 0xc29fa
    add si, dx                                ; 01 d6                       ; 0xc29fc
    inc si                                    ; 46                          ; 0xc29fe vgabios.c:1911
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc29ff vgabios.c:45
    mov bl, byte [es:si]                      ; 26 8a 1c                    ; 0xc2a03
    push strict byte 00001h                   ; 6a 01                       ; 0xc2a06 vgabios.c:1912
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc2a08
    push dx                                   ; 52                          ; 0xc2a0c
    movzx dx, ah                              ; 0f b6 d4                    ; 0xc2a0d
    push dx                                   ; 52                          ; 0xc2a10
    xor ah, ah                                ; 30 e4                       ; 0xc2a11
    push ax                                   ; 50                          ; 0xc2a13
    movzx dx, bl                              ; 0f b6 d3                    ; 0xc2a14
    xor cx, cx                                ; 31 c9                       ; 0xc2a17
    xor bx, bx                                ; 31 db                       ; 0xc2a19
    jmp short 02a31h                          ; eb 14                       ; 0xc2a1b vgabios.c:1914
    push strict byte 00001h                   ; 6a 01                       ; 0xc2a1d vgabios.c:1916
    movzx dx, byte [bp-00ah]                  ; 0f b6 56 f6                 ; 0xc2a1f
    push dx                                   ; 52                          ; 0xc2a23
    movzx dx, ah                              ; 0f b6 d4                    ; 0xc2a24
    push dx                                   ; 52                          ; 0xc2a27
    xor ah, ah                                ; 30 e4                       ; 0xc2a28
    push ax                                   ; 50                          ; 0xc2a2a
    xor cx, cx                                ; 31 c9                       ; 0xc2a2b
    xor bx, bx                                ; 31 db                       ; 0xc2a2d
    xor dx, dx                                ; 31 d2                       ; 0xc2a2f
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc2a31
    call 01b5dh                               ; e8 26 f1                    ; 0xc2a34
    dec byte [bp-004h]                        ; fe 4e fc                    ; 0xc2a37 vgabios.c:1918
    movzx ax, byte [bp-004h]                  ; 0f b6 46 fc                 ; 0xc2a3a vgabios.c:1922
    mov word [bp-012h], ax                    ; 89 46 ee                    ; 0xc2a3e
    sal word [bp-012h], 008h                  ; c1 66 ee 08                 ; 0xc2a41
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2a45
    add word [bp-012h], ax                    ; 01 46 ee                    ; 0xc2a49
    mov dx, word [bp-012h]                    ; 8b 56 ee                    ; 0xc2a4c vgabios.c:1923
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc2a4f
    call 01242h                               ; e8 ec e7                    ; 0xc2a53
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2a56 vgabios.c:1924
    pop si                                    ; 5e                          ; 0xc2a59
    pop bp                                    ; 5d                          ; 0xc2a5a
    retn                                      ; c3                          ; 0xc2a5b
  ; disGetNextSymbol 0xc2a5c LB 0x185d -> off=0x0 cb=0000000000000033 uValue=00000000000c2a5c 'get_font_access'
get_font_access:                             ; 0xc2a5c LB 0x33
    push bp                                   ; 55                          ; 0xc2a5c vgabios.c:1927
    mov bp, sp                                ; 89 e5                       ; 0xc2a5d
    push dx                                   ; 52                          ; 0xc2a5f
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2a60 vgabios.c:1929
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2a63
    out DX, ax                                ; ef                          ; 0xc2a66
    mov AL, strict byte 006h                  ; b0 06                       ; 0xc2a67 vgabios.c:1930
    out DX, AL                                ; ee                          ; 0xc2a69
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc2a6a vgabios.c:1931
    in AL, DX                                 ; ec                          ; 0xc2a6d
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2a6e
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2a70
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc2a73
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2a75
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2a78
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2a7a
    out DX, ax                                ; ef                          ; 0xc2a7d
    mov ax, 00402h                            ; b8 02 04                    ; 0xc2a7e vgabios.c:1932
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2a81
    out DX, ax                                ; ef                          ; 0xc2a84
    mov ax, 00604h                            ; b8 04 06                    ; 0xc2a85 vgabios.c:1933
    out DX, ax                                ; ef                          ; 0xc2a88
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2a89 vgabios.c:1934
    pop dx                                    ; 5a                          ; 0xc2a8c
    pop bp                                    ; 5d                          ; 0xc2a8d
    retn                                      ; c3                          ; 0xc2a8e
  ; disGetNextSymbol 0xc2a8f LB 0x182a -> off=0x0 cb=0000000000000030 uValue=00000000000c2a8f 'release_font_access'
release_font_access:                         ; 0xc2a8f LB 0x30
    push bp                                   ; 55                          ; 0xc2a8f vgabios.c:1936
    mov bp, sp                                ; 89 e5                       ; 0xc2a90
    push dx                                   ; 52                          ; 0xc2a92
    mov dx, 003cch                            ; ba cc 03                    ; 0xc2a93 vgabios.c:1938
    in AL, DX                                 ; ec                          ; 0xc2a96
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2a97
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2a99
    sal ax, 002h                              ; c1 e0 02                    ; 0xc2a9c
    or AL, strict byte 00ah                   ; 0c 0a                       ; 0xc2a9f
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2aa1
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2aa4
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2aa6
    out DX, ax                                ; ef                          ; 0xc2aa9
    mov ax, 01005h                            ; b8 05 10                    ; 0xc2aaa vgabios.c:1939
    out DX, ax                                ; ef                          ; 0xc2aad
    mov ax, 00302h                            ; b8 02 03                    ; 0xc2aae vgabios.c:1940
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2ab1
    out DX, ax                                ; ef                          ; 0xc2ab4
    mov ax, 00204h                            ; b8 04 02                    ; 0xc2ab5 vgabios.c:1941
    out DX, ax                                ; ef                          ; 0xc2ab8
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2ab9 vgabios.c:1942
    pop dx                                    ; 5a                          ; 0xc2abc
    pop bp                                    ; 5d                          ; 0xc2abd
    retn                                      ; c3                          ; 0xc2abe
  ; disGetNextSymbol 0xc2abf LB 0x17fa -> off=0x0 cb=00000000000000b4 uValue=00000000000c2abf 'set_scan_lines'
set_scan_lines:                              ; 0xc2abf LB 0xb4
    push bp                                   ; 55                          ; 0xc2abf vgabios.c:1944
    mov bp, sp                                ; 89 e5                       ; 0xc2ac0
    push bx                                   ; 53                          ; 0xc2ac2
    push cx                                   ; 51                          ; 0xc2ac3
    push dx                                   ; 52                          ; 0xc2ac4
    push si                                   ; 56                          ; 0xc2ac5
    push di                                   ; 57                          ; 0xc2ac6
    mov bl, al                                ; 88 c3                       ; 0xc2ac7
    mov si, strict word 00063h                ; be 63 00                    ; 0xc2ac9 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2acc
    mov es, ax                                ; 8e c0                       ; 0xc2acf
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc2ad1
    mov cx, si                                ; 89 f1                       ; 0xc2ad4 vgabios.c:58
    mov AL, strict byte 009h                  ; b0 09                       ; 0xc2ad6 vgabios.c:1950
    mov dx, si                                ; 89 f2                       ; 0xc2ad8
    out DX, AL                                ; ee                          ; 0xc2ada
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc2adb vgabios.c:1951
    in AL, DX                                 ; ec                          ; 0xc2ade
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2adf
    mov ah, al                                ; 88 c4                       ; 0xc2ae1 vgabios.c:1952
    and ah, 0e0h                              ; 80 e4 e0                    ; 0xc2ae3
    mov al, bl                                ; 88 d8                       ; 0xc2ae6
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc2ae8
    or al, ah                                 ; 08 e0                       ; 0xc2aea
    out DX, AL                                ; ee                          ; 0xc2aec vgabios.c:1953
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc2aed vgabios.c:1954
    jne short 02afah                          ; 75 08                       ; 0xc2af0
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc2af2 vgabios.c:1956
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc2af5
    jmp short 02b07h                          ; eb 0d                       ; 0xc2af8 vgabios.c:1958
    mov al, bl                                ; 88 d8                       ; 0xc2afa vgabios.c:1960
    sub AL, strict byte 003h                  ; 2c 03                       ; 0xc2afc
    movzx dx, al                              ; 0f b6 d0                    ; 0xc2afe
    mov al, bl                                ; 88 d8                       ; 0xc2b01
    sub AL, strict byte 004h                  ; 2c 04                       ; 0xc2b03
    xor ah, ah                                ; 30 e4                       ; 0xc2b05
    call 0114ch                               ; e8 42 e6                    ; 0xc2b07
    movzx di, bl                              ; 0f b6 fb                    ; 0xc2b0a vgabios.c:1962
    mov bx, 00085h                            ; bb 85 00                    ; 0xc2b0d vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2b10
    mov es, ax                                ; 8e c0                       ; 0xc2b13
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc2b15
    mov AL, strict byte 012h                  ; b0 12                       ; 0xc2b18 vgabios.c:1963
    mov dx, cx                                ; 89 ca                       ; 0xc2b1a
    out DX, AL                                ; ee                          ; 0xc2b1c
    mov bx, cx                                ; 89 cb                       ; 0xc2b1d vgabios.c:1964
    inc bx                                    ; 43                          ; 0xc2b1f
    mov dx, bx                                ; 89 da                       ; 0xc2b20
    in AL, DX                                 ; ec                          ; 0xc2b22
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2b23
    mov si, ax                                ; 89 c6                       ; 0xc2b25
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc2b27 vgabios.c:1965
    mov dx, cx                                ; 89 ca                       ; 0xc2b29
    out DX, AL                                ; ee                          ; 0xc2b2b
    mov dx, bx                                ; 89 da                       ; 0xc2b2c vgabios.c:1966
    in AL, DX                                 ; ec                          ; 0xc2b2e
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2b2f
    mov ah, al                                ; 88 c4                       ; 0xc2b31 vgabios.c:1967
    and ah, 002h                              ; 80 e4 02                    ; 0xc2b33
    movzx dx, ah                              ; 0f b6 d4                    ; 0xc2b36
    sal dx, 007h                              ; c1 e2 07                    ; 0xc2b39
    and AL, strict byte 040h                  ; 24 40                       ; 0xc2b3c
    xor ah, ah                                ; 30 e4                       ; 0xc2b3e
    sal ax, 003h                              ; c1 e0 03                    ; 0xc2b40
    add ax, dx                                ; 01 d0                       ; 0xc2b43
    inc ax                                    ; 40                          ; 0xc2b45
    add ax, si                                ; 01 f0                       ; 0xc2b46
    xor dx, dx                                ; 31 d2                       ; 0xc2b48 vgabios.c:1968
    div di                                    ; f7 f7                       ; 0xc2b4a
    mov dl, al                                ; 88 c2                       ; 0xc2b4c vgabios.c:1969
    db  0feh, 0cah
    ; dec dl                                    ; fe ca                     ; 0xc2b4e
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2b50 vgabios.c:52
    mov byte [es:bx], dl                      ; 26 88 17                    ; 0xc2b53
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2b56 vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc2b59
    xor ah, ah                                ; 30 e4                       ; 0xc2b5c vgabios.c:1971
    imul dx, ax                               ; 0f af d0                    ; 0xc2b5e
    add dx, dx                                ; 01 d2                       ; 0xc2b61
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc2b63 vgabios.c:62
    mov word [es:bx], dx                      ; 26 89 17                    ; 0xc2b66
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc2b69 vgabios.c:1972
    pop di                                    ; 5f                          ; 0xc2b6c
    pop si                                    ; 5e                          ; 0xc2b6d
    pop dx                                    ; 5a                          ; 0xc2b6e
    pop cx                                    ; 59                          ; 0xc2b6f
    pop bx                                    ; 5b                          ; 0xc2b70
    pop bp                                    ; 5d                          ; 0xc2b71
    retn                                      ; c3                          ; 0xc2b72
  ; disGetNextSymbol 0xc2b73 LB 0x1746 -> off=0x0 cb=0000000000000022 uValue=00000000000c2b73 'biosfn_set_font_block'
biosfn_set_font_block:                       ; 0xc2b73 LB 0x22
    push bp                                   ; 55                          ; 0xc2b73 vgabios.c:1974
    mov bp, sp                                ; 89 e5                       ; 0xc2b74
    push bx                                   ; 53                          ; 0xc2b76
    push dx                                   ; 52                          ; 0xc2b77
    mov bl, al                                ; 88 c3                       ; 0xc2b78
    mov ax, 00100h                            ; b8 00 01                    ; 0xc2b7a vgabios.c:1976
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2b7d
    out DX, ax                                ; ef                          ; 0xc2b80
    movzx ax, bl                              ; 0f b6 c3                    ; 0xc2b81 vgabios.c:1977
    sal ax, 008h                              ; c1 e0 08                    ; 0xc2b84
    or AL, strict byte 003h                   ; 0c 03                       ; 0xc2b87
    out DX, ax                                ; ef                          ; 0xc2b89
    mov ax, 00300h                            ; b8 00 03                    ; 0xc2b8a vgabios.c:1978
    out DX, ax                                ; ef                          ; 0xc2b8d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2b8e vgabios.c:1979
    pop dx                                    ; 5a                          ; 0xc2b91
    pop bx                                    ; 5b                          ; 0xc2b92
    pop bp                                    ; 5d                          ; 0xc2b93
    retn                                      ; c3                          ; 0xc2b94
  ; disGetNextSymbol 0xc2b95 LB 0x1724 -> off=0x0 cb=0000000000000075 uValue=00000000000c2b95 'load_text_patch'
load_text_patch:                             ; 0xc2b95 LB 0x75
    push bp                                   ; 55                          ; 0xc2b95 vgabios.c:1981
    mov bp, sp                                ; 89 e5                       ; 0xc2b96
    push si                                   ; 56                          ; 0xc2b98
    push di                                   ; 57                          ; 0xc2b99
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc2b9a
    push ax                                   ; 50                          ; 0xc2b9d
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc2b9e
    call 02a5ch                               ; e8 b8 fe                    ; 0xc2ba1 vgabios.c:1986
    mov al, bl                                ; 88 d8                       ; 0xc2ba4 vgabios.c:1988
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2ba6
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2ba8
    sal cx, 00eh                              ; c1 e1 0e                    ; 0xc2bab
    mov al, bl                                ; 88 d8                       ; 0xc2bae
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2bb0
    xor ah, ah                                ; 30 e4                       ; 0xc2bb2
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2bb4
    add cx, ax                                ; 01 c1                       ; 0xc2bb7
    mov word [bp-00ch], cx                    ; 89 4e f4                    ; 0xc2bb9
    mov bx, dx                                ; 89 d3                       ; 0xc2bbc vgabios.c:1989
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2bbe
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc2bc1
    inc dx                                    ; 42                          ; 0xc2bc4 vgabios.c:1990
    mov word [bp-008h], dx                    ; 89 56 f8                    ; 0xc2bc5
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc2bc8 vgabios.c:1991
    cmp byte [es:bx], 000h                    ; 26 80 3f 00                 ; 0xc2bcb
    je short 02c00h                           ; 74 2f                       ; 0xc2bcf
    movzx ax, byte [es:bx]                    ; 26 0f b6 07                 ; 0xc2bd1 vgabios.c:1992
    sal ax, 005h                              ; c1 e0 05                    ; 0xc2bd5
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc2bd8
    add di, ax                                ; 01 c7                       ; 0xc2bdb
    movzx cx, byte [bp-006h]                  ; 0f b6 4e fa                 ; 0xc2bdd vgabios.c:1993
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc2be1
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2be4
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2be7
    mov es, ax                                ; 8e c0                       ; 0xc2bea
    jcxz 02bf4h                               ; e3 06                       ; 0xc2bec
    push DS                                   ; 1e                          ; 0xc2bee
    mov ds, dx                                ; 8e da                       ; 0xc2bef
    rep movsb                                 ; f3 a4                       ; 0xc2bf1
    pop DS                                    ; 1f                          ; 0xc2bf3
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2bf4 vgabios.c:1994
    inc ax                                    ; 40                          ; 0xc2bf8
    add word [bp-008h], ax                    ; 01 46 f8                    ; 0xc2bf9
    add bx, ax                                ; 01 c3                       ; 0xc2bfc vgabios.c:1995
    jmp short 02bc8h                          ; eb c8                       ; 0xc2bfe vgabios.c:1996
    call 02a8fh                               ; e8 8c fe                    ; 0xc2c00 vgabios.c:1998
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2c03 vgabios.c:1999
    pop di                                    ; 5f                          ; 0xc2c06
    pop si                                    ; 5e                          ; 0xc2c07
    pop bp                                    ; 5d                          ; 0xc2c08
    retn                                      ; c3                          ; 0xc2c09
  ; disGetNextSymbol 0xc2c0a LB 0x16af -> off=0x0 cb=000000000000007c uValue=00000000000c2c0a 'biosfn_load_text_user_pat'
biosfn_load_text_user_pat:                   ; 0xc2c0a LB 0x7c
    push bp                                   ; 55                          ; 0xc2c0a vgabios.c:2001
    mov bp, sp                                ; 89 e5                       ; 0xc2c0b
    push si                                   ; 56                          ; 0xc2c0d
    push di                                   ; 57                          ; 0xc2c0e
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2c0f
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2c12
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc2c15
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc2c18
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc2c1b
    call 02a5ch                               ; e8 3b fe                    ; 0xc2c1e vgabios.c:2006
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2c21 vgabios.c:2007
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2c24
    xor ah, ah                                ; 30 e4                       ; 0xc2c26
    mov bx, ax                                ; 89 c3                       ; 0xc2c28
    sal bx, 00eh                              ; c1 e3 0e                    ; 0xc2c2a
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2c2d
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2c30
    xor ah, ah                                ; 30 e4                       ; 0xc2c32
    sal ax, 00bh                              ; c1 e0 0b                    ; 0xc2c34
    add bx, ax                                ; 01 c3                       ; 0xc2c37
    mov word [bp-00eh], bx                    ; 89 5e f2                    ; 0xc2c39
    xor bx, bx                                ; 31 db                       ; 0xc2c3c vgabios.c:2008
    cmp bx, word [bp-00ah]                    ; 3b 5e f6                    ; 0xc2c3e
    jnc short 02c6dh                          ; 73 2a                       ; 0xc2c41
    movzx cx, byte [bp+008h]                  ; 0f b6 4e 08                 ; 0xc2c43 vgabios.c:2010
    mov si, bx                                ; 89 de                       ; 0xc2c47
    imul si, cx                               ; 0f af f1                    ; 0xc2c49
    add si, word [bp-008h]                    ; 03 76 f8                    ; 0xc2c4c
    mov di, word [bp+004h]                    ; 8b 7e 04                    ; 0xc2c4f vgabios.c:2011
    add di, bx                                ; 01 df                       ; 0xc2c52
    sal di, 005h                              ; c1 e7 05                    ; 0xc2c54
    add di, word [bp-00eh]                    ; 03 7e f2                    ; 0xc2c57
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc2c5a vgabios.c:2012
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2c5d
    mov es, ax                                ; 8e c0                       ; 0xc2c60
    jcxz 02c6ah                               ; e3 06                       ; 0xc2c62
    push DS                                   ; 1e                          ; 0xc2c64
    mov ds, dx                                ; 8e da                       ; 0xc2c65
    rep movsb                                 ; f3 a4                       ; 0xc2c67
    pop DS                                    ; 1f                          ; 0xc2c69
    inc bx                                    ; 43                          ; 0xc2c6a vgabios.c:2013
    jmp short 02c3eh                          ; eb d1                       ; 0xc2c6b
    call 02a8fh                               ; e8 1f fe                    ; 0xc2c6d vgabios.c:2014
    cmp byte [bp-006h], 010h                  ; 80 7e fa 10                 ; 0xc2c70 vgabios.c:2015
    jc short 02c7dh                           ; 72 07                       ; 0xc2c74
    movzx ax, byte [bp+008h]                  ; 0f b6 46 08                 ; 0xc2c76 vgabios.c:2017
    call 02abfh                               ; e8 42 fe                    ; 0xc2c7a
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2c7d vgabios.c:2019
    pop di                                    ; 5f                          ; 0xc2c80
    pop si                                    ; 5e                          ; 0xc2c81
    pop bp                                    ; 5d                          ; 0xc2c82
    retn 00006h                               ; c2 06 00                    ; 0xc2c83
  ; disGetNextSymbol 0xc2c86 LB 0x1633 -> off=0x0 cb=0000000000000016 uValue=00000000000c2c86 'biosfn_load_gfx_8_8_chars'
biosfn_load_gfx_8_8_chars:                   ; 0xc2c86 LB 0x16
    push bp                                   ; 55                          ; 0xc2c86 vgabios.c:2021
    mov bp, sp                                ; 89 e5                       ; 0xc2c87
    push bx                                   ; 53                          ; 0xc2c89
    push cx                                   ; 51                          ; 0xc2c8a
    mov bx, dx                                ; 89 d3                       ; 0xc2c8b vgabios.c:2023
    mov cx, ax                                ; 89 c1                       ; 0xc2c8d
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc2c8f
    call 009f0h                               ; e8 5b dd                    ; 0xc2c92
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2c95 vgabios.c:2024
    pop cx                                    ; 59                          ; 0xc2c98
    pop bx                                    ; 5b                          ; 0xc2c99
    pop bp                                    ; 5d                          ; 0xc2c9a
    retn                                      ; c3                          ; 0xc2c9b
  ; disGetNextSymbol 0xc2c9c LB 0x161d -> off=0x0 cb=0000000000000049 uValue=00000000000c2c9c 'set_gfx_font'
set_gfx_font:                                ; 0xc2c9c LB 0x49
    push bp                                   ; 55                          ; 0xc2c9c vgabios.c:2026
    mov bp, sp                                ; 89 e5                       ; 0xc2c9d
    push si                                   ; 56                          ; 0xc2c9f
    push di                                   ; 57                          ; 0xc2ca0
    mov si, dx                                ; 89 d6                       ; 0xc2ca1
    mov di, bx                                ; 89 df                       ; 0xc2ca3
    mov dl, cl                                ; 88 ca                       ; 0xc2ca5
    mov bx, ax                                ; 89 c3                       ; 0xc2ca7 vgabios.c:2030
    mov cx, si                                ; 89 f1                       ; 0xc2ca9
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc2cab
    call 009f0h                               ; e8 3f dd                    ; 0xc2cae
    test dl, dl                               ; 84 d2                       ; 0xc2cb1 vgabios.c:2031
    je short 02cc6h                           ; 74 11                       ; 0xc2cb3
    cmp dl, 003h                              ; 80 fa 03                    ; 0xc2cb5 vgabios.c:2032
    jbe short 02cbch                          ; 76 02                       ; 0xc2cb8
    mov DL, strict byte 002h                  ; b2 02                       ; 0xc2cba vgabios.c:2033
    movzx bx, dl                              ; 0f b6 da                    ; 0xc2cbc vgabios.c:2034
    mov al, byte [bx+07e00h]                  ; 8a 87 00 7e                 ; 0xc2cbf
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2cc3
    mov bx, 00085h                            ; bb 85 00                    ; 0xc2cc6 vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2cc9
    mov es, ax                                ; 8e c0                       ; 0xc2ccc
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc2cce
    movzx ax, byte [bp+004h]                  ; 0f b6 46 04                 ; 0xc2cd1 vgabios.c:2039
    dec ax                                    ; 48                          ; 0xc2cd5
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2cd6 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc2cd9
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2cdc vgabios.c:2040
    pop di                                    ; 5f                          ; 0xc2cdf
    pop si                                    ; 5e                          ; 0xc2ce0
    pop bp                                    ; 5d                          ; 0xc2ce1
    retn 00002h                               ; c2 02 00                    ; 0xc2ce2
  ; disGetNextSymbol 0xc2ce5 LB 0x15d4 -> off=0x0 cb=000000000000001c uValue=00000000000c2ce5 'biosfn_load_gfx_user_chars'
biosfn_load_gfx_user_chars:                  ; 0xc2ce5 LB 0x1c
    push bp                                   ; 55                          ; 0xc2ce5 vgabios.c:2042
    mov bp, sp                                ; 89 e5                       ; 0xc2ce6
    push si                                   ; 56                          ; 0xc2ce8
    mov si, ax                                ; 89 c6                       ; 0xc2ce9
    mov ax, dx                                ; 89 d0                       ; 0xc2ceb
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc2ced vgabios.c:2045
    push dx                                   ; 52                          ; 0xc2cf1
    xor ch, ch                                ; 30 ed                       ; 0xc2cf2
    mov dx, si                                ; 89 f2                       ; 0xc2cf4
    call 02c9ch                               ; e8 a3 ff                    ; 0xc2cf6
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2cf9 vgabios.c:2046
    pop si                                    ; 5e                          ; 0xc2cfc
    pop bp                                    ; 5d                          ; 0xc2cfd
    retn 00002h                               ; c2 02 00                    ; 0xc2cfe
  ; disGetNextSymbol 0xc2d01 LB 0x15b8 -> off=0x0 cb=000000000000001e uValue=00000000000c2d01 'biosfn_load_gfx_8_14_chars'
biosfn_load_gfx_8_14_chars:                  ; 0xc2d01 LB 0x1e
    push bp                                   ; 55                          ; 0xc2d01 vgabios.c:2051
    mov bp, sp                                ; 89 e5                       ; 0xc2d02
    push bx                                   ; 53                          ; 0xc2d04
    push cx                                   ; 51                          ; 0xc2d05
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc2d06 vgabios.c:2053
    push cx                                   ; 51                          ; 0xc2d09
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2d0a
    mov bx, strict word 0000eh                ; bb 0e 00                    ; 0xc2d0d
    mov ax, 05d6fh                            ; b8 6f 5d                    ; 0xc2d10
    mov dx, ds                                ; 8c da                       ; 0xc2d13
    call 02c9ch                               ; e8 84 ff                    ; 0xc2d15
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2d18 vgabios.c:2054
    pop cx                                    ; 59                          ; 0xc2d1b
    pop bx                                    ; 5b                          ; 0xc2d1c
    pop bp                                    ; 5d                          ; 0xc2d1d
    retn                                      ; c3                          ; 0xc2d1e
  ; disGetNextSymbol 0xc2d1f LB 0x159a -> off=0x0 cb=000000000000001e uValue=00000000000c2d1f 'biosfn_load_gfx_8_8_dd_chars'
biosfn_load_gfx_8_8_dd_chars:                ; 0xc2d1f LB 0x1e
    push bp                                   ; 55                          ; 0xc2d1f vgabios.c:2055
    mov bp, sp                                ; 89 e5                       ; 0xc2d20
    push bx                                   ; 53                          ; 0xc2d22
    push cx                                   ; 51                          ; 0xc2d23
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc2d24 vgabios.c:2057
    push cx                                   ; 51                          ; 0xc2d27
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2d28
    mov bx, strict word 00008h                ; bb 08 00                    ; 0xc2d2b
    mov ax, 0556fh                            ; b8 6f 55                    ; 0xc2d2e
    mov dx, ds                                ; 8c da                       ; 0xc2d31
    call 02c9ch                               ; e8 66 ff                    ; 0xc2d33
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2d36 vgabios.c:2058
    pop cx                                    ; 59                          ; 0xc2d39
    pop bx                                    ; 5b                          ; 0xc2d3a
    pop bp                                    ; 5d                          ; 0xc2d3b
    retn                                      ; c3                          ; 0xc2d3c
  ; disGetNextSymbol 0xc2d3d LB 0x157c -> off=0x0 cb=000000000000001e uValue=00000000000c2d3d 'biosfn_load_gfx_8_16_chars'
biosfn_load_gfx_8_16_chars:                  ; 0xc2d3d LB 0x1e
    push bp                                   ; 55                          ; 0xc2d3d vgabios.c:2059
    mov bp, sp                                ; 89 e5                       ; 0xc2d3e
    push bx                                   ; 53                          ; 0xc2d40
    push cx                                   ; 51                          ; 0xc2d41
    movzx cx, dl                              ; 0f b6 ca                    ; 0xc2d42 vgabios.c:2061
    push cx                                   ; 51                          ; 0xc2d45
    movzx cx, al                              ; 0f b6 c8                    ; 0xc2d46
    mov bx, strict word 00010h                ; bb 10 00                    ; 0xc2d49
    mov ax, 06b6fh                            ; b8 6f 6b                    ; 0xc2d4c
    mov dx, ds                                ; 8c da                       ; 0xc2d4f
    call 02c9ch                               ; e8 48 ff                    ; 0xc2d51
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2d54 vgabios.c:2062
    pop cx                                    ; 59                          ; 0xc2d57
    pop bx                                    ; 5b                          ; 0xc2d58
    pop bp                                    ; 5d                          ; 0xc2d59
    retn                                      ; c3                          ; 0xc2d5a
  ; disGetNextSymbol 0xc2d5b LB 0x155e -> off=0x0 cb=0000000000000005 uValue=00000000000c2d5b 'biosfn_alternate_prtsc'
biosfn_alternate_prtsc:                      ; 0xc2d5b LB 0x5
    push bp                                   ; 55                          ; 0xc2d5b vgabios.c:2064
    mov bp, sp                                ; 89 e5                       ; 0xc2d5c
    pop bp                                    ; 5d                          ; 0xc2d5e vgabios.c:2069
    retn                                      ; c3                          ; 0xc2d5f
  ; disGetNextSymbol 0xc2d60 LB 0x1559 -> off=0x0 cb=0000000000000032 uValue=00000000000c2d60 'biosfn_set_txt_lines'
biosfn_set_txt_lines:                        ; 0xc2d60 LB 0x32
    push bx                                   ; 53                          ; 0xc2d60 vgabios.c:2071
    push si                                   ; 56                          ; 0xc2d61
    push bp                                   ; 55                          ; 0xc2d62
    mov bp, sp                                ; 89 e5                       ; 0xc2d63
    mov bl, al                                ; 88 c3                       ; 0xc2d65
    mov si, 00089h                            ; be 89 00                    ; 0xc2d67 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2d6a
    mov es, ax                                ; 8e c0                       ; 0xc2d6d
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2d6f
    and AL, strict byte 06fh                  ; 24 6f                       ; 0xc2d72 vgabios.c:2077
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2d74 vgabios.c:2079
    je short 02d81h                           ; 74 08                       ; 0xc2d77
    test bl, bl                               ; 84 db                       ; 0xc2d79
    jne short 02d83h                          ; 75 06                       ; 0xc2d7b
    or AL, strict byte 080h                   ; 0c 80                       ; 0xc2d7d vgabios.c:2082
    jmp short 02d83h                          ; eb 02                       ; 0xc2d7f vgabios.c:2083
    or AL, strict byte 010h                   ; 0c 10                       ; 0xc2d81 vgabios.c:2085
    mov bx, 00089h                            ; bb 89 00                    ; 0xc2d83 vgabios.c:52
    mov si, strict word 00040h                ; be 40 00                    ; 0xc2d86
    mov es, si                                ; 8e c6                       ; 0xc2d89
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2d8b
    pop bp                                    ; 5d                          ; 0xc2d8e vgabios.c:2089
    pop si                                    ; 5e                          ; 0xc2d8f
    pop bx                                    ; 5b                          ; 0xc2d90
    retn                                      ; c3                          ; 0xc2d91
  ; disGetNextSymbol 0xc2d92 LB 0x1527 -> off=0x0 cb=0000000000000005 uValue=00000000000c2d92 'biosfn_switch_video_interface'
biosfn_switch_video_interface:               ; 0xc2d92 LB 0x5
    push bp                                   ; 55                          ; 0xc2d92 vgabios.c:2092
    mov bp, sp                                ; 89 e5                       ; 0xc2d93
    pop bp                                    ; 5d                          ; 0xc2d95 vgabios.c:2097
    retn                                      ; c3                          ; 0xc2d96
  ; disGetNextSymbol 0xc2d97 LB 0x1522 -> off=0x0 cb=0000000000000005 uValue=00000000000c2d97 'biosfn_enable_video_refresh_control'
biosfn_enable_video_refresh_control:         ; 0xc2d97 LB 0x5
    push bp                                   ; 55                          ; 0xc2d97 vgabios.c:2098
    mov bp, sp                                ; 89 e5                       ; 0xc2d98
    pop bp                                    ; 5d                          ; 0xc2d9a vgabios.c:2103
    retn                                      ; c3                          ; 0xc2d9b
  ; disGetNextSymbol 0xc2d9c LB 0x151d -> off=0x0 cb=0000000000000096 uValue=00000000000c2d9c 'biosfn_write_string'
biosfn_write_string:                         ; 0xc2d9c LB 0x96
    push bp                                   ; 55                          ; 0xc2d9c vgabios.c:2106
    mov bp, sp                                ; 89 e5                       ; 0xc2d9d
    push si                                   ; 56                          ; 0xc2d9f
    push di                                   ; 57                          ; 0xc2da0
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2da1
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2da4
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc2da7
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc2daa
    mov si, cx                                ; 89 ce                       ; 0xc2dad
    mov di, word [bp+00ah]                    ; 8b 7e 0a                    ; 0xc2daf
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc2db2 vgabios.c:2113
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc2db5
    lea dx, [bp-00ch]                         ; 8d 56 f4                    ; 0xc2db8
    call 00a93h                               ; e8 d5 dc                    ; 0xc2dbb
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc2dbe vgabios.c:2116
    jne short 02dd5h                          ; 75 11                       ; 0xc2dc2
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2dc4 vgabios.c:2117
    mov byte [bp+006h], al                    ; 88 46 06                    ; 0xc2dc7
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2dca vgabios.c:2118
    xor al, al                                ; 30 c0                       ; 0xc2dcd
    shr ax, 008h                              ; c1 e8 08                    ; 0xc2dcf
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2dd2
    movzx dx, byte [bp+004h]                  ; 0f b6 56 04                 ; 0xc2dd5 vgabios.c:2121
    sal dx, 008h                              ; c1 e2 08                    ; 0xc2dd9
    movzx ax, byte [bp+006h]                  ; 0f b6 46 06                 ; 0xc2ddc
    add dx, ax                                ; 01 c2                       ; 0xc2de0
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2de2 vgabios.c:2122
    call 01242h                               ; e8 59 e4                    ; 0xc2de6
    dec si                                    ; 4e                          ; 0xc2de9 vgabios.c:2124
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc2dea
    je short 02e19h                           ; 74 2a                       ; 0xc2ded
    mov bx, di                                ; 89 fb                       ; 0xc2def vgabios.c:2126
    inc di                                    ; 47                          ; 0xc2df1
    mov es, [bp+008h]                         ; 8e 46 08                    ; 0xc2df2 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2df5
    test byte [bp-008h], 002h                 ; f6 46 f8 02                 ; 0xc2df8 vgabios.c:2127
    je short 02e07h                           ; 74 09                       ; 0xc2dfc
    mov bx, di                                ; 89 fb                       ; 0xc2dfe vgabios.c:2128
    inc di                                    ; 47                          ; 0xc2e00
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc2e01 vgabios.c:47
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc2e04 vgabios.c:48
    movzx bx, byte [bp-00ah]                  ; 0f b6 5e f6                 ; 0xc2e07 vgabios.c:2130
    movzx dx, byte [bp-006h]                  ; 0f b6 56 fa                 ; 0xc2e0b
    xor ah, ah                                ; 30 e4                       ; 0xc2e0f
    mov cx, strict word 00003h                ; b9 03 00                    ; 0xc2e11
    call 02812h                               ; e8 fb f9                    ; 0xc2e14
    jmp short 02de9h                          ; eb d0                       ; 0xc2e17 vgabios.c:2131
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc2e19 vgabios.c:2134
    jne short 02e29h                          ; 75 0a                       ; 0xc2e1d
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2e1f vgabios.c:2135
    movzx ax, byte [bp-006h]                  ; 0f b6 46 fa                 ; 0xc2e22
    call 01242h                               ; e8 19 e4                    ; 0xc2e26
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2e29 vgabios.c:2136
    pop di                                    ; 5f                          ; 0xc2e2c
    pop si                                    ; 5e                          ; 0xc2e2d
    pop bp                                    ; 5d                          ; 0xc2e2e
    retn 00008h                               ; c2 08 00                    ; 0xc2e2f
  ; disGetNextSymbol 0xc2e32 LB 0x1487 -> off=0x0 cb=00000000000001f2 uValue=00000000000c2e32 'biosfn_read_state_info'
biosfn_read_state_info:                      ; 0xc2e32 LB 0x1f2
    push bp                                   ; 55                          ; 0xc2e32 vgabios.c:2139
    mov bp, sp                                ; 89 e5                       ; 0xc2e33
    push cx                                   ; 51                          ; 0xc2e35
    push si                                   ; 56                          ; 0xc2e36
    push di                                   ; 57                          ; 0xc2e37
    push ax                                   ; 50                          ; 0xc2e38
    push ax                                   ; 50                          ; 0xc2e39
    push dx                                   ; 52                          ; 0xc2e3a
    mov si, strict word 00049h                ; be 49 00                    ; 0xc2e3b vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2e3e
    mov es, ax                                ; 8e c0                       ; 0xc2e41
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2e43
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2e46 vgabios.c:48
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc2e49 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc2e4c
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc2e4f vgabios.c:58
    mov ax, ds                                ; 8c d8                       ; 0xc2e52 vgabios.c:2150
    mov es, dx                                ; 8e c2                       ; 0xc2e54 vgabios.c:72
    mov word [es:bx], 05505h                  ; 26 c7 07 05 55              ; 0xc2e56
    mov [es:bx+002h], ds                      ; 26 8c 5f 02                 ; 0xc2e5b
    lea di, [bx+004h]                         ; 8d 7f 04                    ; 0xc2e5f vgabios.c:2155
    mov cx, strict word 0001eh                ; b9 1e 00                    ; 0xc2e62
    mov si, strict word 00049h                ; be 49 00                    ; 0xc2e65
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2e68
    jcxz 02e73h                               ; e3 06                       ; 0xc2e6b
    push DS                                   ; 1e                          ; 0xc2e6d
    mov ds, dx                                ; 8e da                       ; 0xc2e6e
    rep movsb                                 ; f3 a4                       ; 0xc2e70
    pop DS                                    ; 1f                          ; 0xc2e72
    mov si, 00084h                            ; be 84 00                    ; 0xc2e73 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2e76
    mov es, ax                                ; 8e c0                       ; 0xc2e79
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2e7b
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc2e7e vgabios.c:48
    lea si, [bx+022h]                         ; 8d 77 22                    ; 0xc2e80
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2e83 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2e86
    lea di, [bx+023h]                         ; 8d 7f 23                    ; 0xc2e89 vgabios.c:2157
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc2e8c
    mov si, 00085h                            ; be 85 00                    ; 0xc2e8f
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2e92
    jcxz 02e9dh                               ; e3 06                       ; 0xc2e95
    push DS                                   ; 1e                          ; 0xc2e97
    mov ds, dx                                ; 8e da                       ; 0xc2e98
    rep movsb                                 ; f3 a4                       ; 0xc2e9a
    pop DS                                    ; 1f                          ; 0xc2e9c
    mov si, 0008ah                            ; be 8a 00                    ; 0xc2e9d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2ea0
    mov es, ax                                ; 8e c0                       ; 0xc2ea3
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2ea5
    lea si, [bx+025h]                         ; 8d 77 25                    ; 0xc2ea8 vgabios.c:48
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2eab vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2eae
    lea si, [bx+026h]                         ; 8d 77 26                    ; 0xc2eb1 vgabios.c:2160
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2eb4 vgabios.c:52
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2eb8 vgabios.c:2161
    mov word [es:si], strict word 00010h      ; 26 c7 04 10 00              ; 0xc2ebb vgabios.c:62
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc2ec0 vgabios.c:2162
    mov byte [es:si], 008h                    ; 26 c6 04 08                 ; 0xc2ec3 vgabios.c:52
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc2ec7 vgabios.c:2163
    mov byte [es:si], 002h                    ; 26 c6 04 02                 ; 0xc2eca vgabios.c:52
    lea si, [bx+02bh]                         ; 8d 77 2b                    ; 0xc2ece vgabios.c:2164
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2ed1 vgabios.c:52
    lea si, [bx+02ch]                         ; 8d 77 2c                    ; 0xc2ed5 vgabios.c:2165
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2ed8 vgabios.c:52
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc2edc vgabios.c:2166
    mov byte [es:si], 021h                    ; 26 c6 04 21                 ; 0xc2edf vgabios.c:52
    lea si, [bx+031h]                         ; 8d 77 31                    ; 0xc2ee3 vgabios.c:2167
    mov byte [es:si], 003h                    ; 26 c6 04 03                 ; 0xc2ee6 vgabios.c:52
    lea si, [bx+032h]                         ; 8d 77 32                    ; 0xc2eea vgabios.c:2168
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc2eed vgabios.c:52
    mov si, 00089h                            ; be 89 00                    ; 0xc2ef1 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2ef4
    mov es, ax                                ; 8e c0                       ; 0xc2ef7
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2ef9
    mov ah, al                                ; 88 c4                       ; 0xc2efc vgabios.c:2173
    and ah, 080h                              ; 80 e4 80                    ; 0xc2efe
    movzx si, ah                              ; 0f b6 f4                    ; 0xc2f01
    sar si, 006h                              ; c1 fe 06                    ; 0xc2f04
    and AL, strict byte 010h                  ; 24 10                       ; 0xc2f07
    xor ah, ah                                ; 30 e4                       ; 0xc2f09
    sar ax, 004h                              ; c1 f8 04                    ; 0xc2f0b
    or ax, si                                 ; 09 f0                       ; 0xc2f0e
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc2f10 vgabios.c:2174
    je short 02f26h                           ; 74 11                       ; 0xc2f13
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc2f15
    je short 02f22h                           ; 74 08                       ; 0xc2f18
    test ax, ax                               ; 85 c0                       ; 0xc2f1a
    jne short 02f26h                          ; 75 08                       ; 0xc2f1c
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc2f1e vgabios.c:2175
    jmp short 02f28h                          ; eb 06                       ; 0xc2f20
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc2f22 vgabios.c:2176
    jmp short 02f28h                          ; eb 02                       ; 0xc2f24
    xor al, al                                ; 30 c0                       ; 0xc2f26 vgabios.c:2178
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc2f28 vgabios.c:2180
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f2b vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2f2e
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2f31 vgabios.c:2183
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc2f34
    jc short 02f57h                           ; 72 1f                       ; 0xc2f36
    cmp AL, strict byte 012h                  ; 3c 12                       ; 0xc2f38
    jnbe short 02f57h                         ; 77 1b                       ; 0xc2f3a
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc2f3c vgabios.c:2184
    test ax, ax                               ; 85 c0                       ; 0xc2f3f
    je short 02f99h                           ; 74 56                       ; 0xc2f41
    mov si, ax                                ; 89 c6                       ; 0xc2f43 vgabios.c:2185
    shr si, 002h                              ; c1 ee 02                    ; 0xc2f45
    mov ax, 04000h                            ; b8 00 40                    ; 0xc2f48
    xor dx, dx                                ; 31 d2                       ; 0xc2f4b
    div si                                    ; f7 f6                       ; 0xc2f4d
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc2f4f
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2f52 vgabios.c:52
    jmp short 02f99h                          ; eb 42                       ; 0xc2f55 vgabios.c:2186
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc2f57
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2f5a
    cmp AL, strict byte 013h                  ; 3c 13                       ; 0xc2f5d
    jne short 02f72h                          ; 75 11                       ; 0xc2f5f
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f61 vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc2f64
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2f68 vgabios.c:2188
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc2f6b vgabios.c:62
    jmp short 02f99h                          ; eb 27                       ; 0xc2f70 vgabios.c:2189
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc2f72
    jc short 02f99h                           ; 72 23                       ; 0xc2f74
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc2f76
    jnbe short 02f99h                         ; 77 1f                       ; 0xc2f78
    cmp word [bp-00ah], strict byte 00000h    ; 83 7e f6 00                 ; 0xc2f7a vgabios.c:2191
    je short 02f8eh                           ; 74 0e                       ; 0xc2f7e
    mov ax, 04000h                            ; b8 00 40                    ; 0xc2f80 vgabios.c:2192
    xor dx, dx                                ; 31 d2                       ; 0xc2f83
    div word [bp-00ah]                        ; f7 76 f6                    ; 0xc2f85
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f88 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2f8b
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2f8e vgabios.c:2193
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2f91 vgabios.c:62
    mov word [es:si], strict word 00004h      ; 26 c7 04 04 00              ; 0xc2f94
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2f99 vgabios.c:2195
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc2f9c
    je short 02fa4h                           ; 74 04                       ; 0xc2f9e
    cmp AL, strict byte 011h                  ; 3c 11                       ; 0xc2fa0
    jne short 02fafh                          ; 75 0b                       ; 0xc2fa2
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc2fa4 vgabios.c:2196
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2fa7 vgabios.c:62
    mov word [es:si], strict word 00002h      ; 26 c7 04 02 00              ; 0xc2faa
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2faf vgabios.c:2198
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc2fb2
    jc short 0300dh                           ; 72 57                       ; 0xc2fb4
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc2fb6
    je short 0300dh                           ; 74 53                       ; 0xc2fb8
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc2fba vgabios.c:2199
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc2fbd vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc2fc0
    mov si, 00084h                            ; be 84 00                    ; 0xc2fc4 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2fc7
    mov es, ax                                ; 8e c0                       ; 0xc2fca
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2fcc
    movzx di, al                              ; 0f b6 f8                    ; 0xc2fcf vgabios.c:48
    inc di                                    ; 47                          ; 0xc2fd2
    mov si, 00085h                            ; be 85 00                    ; 0xc2fd3 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc2fd6
    xor ah, ah                                ; 30 e4                       ; 0xc2fd9 vgabios.c:48
    imul ax, di                               ; 0f af c7                    ; 0xc2fdb
    cmp ax, 0015eh                            ; 3d 5e 01                    ; 0xc2fde vgabios.c:2201
    jc short 02ff1h                           ; 72 0e                       ; 0xc2fe1
    jbe short 02ffah                          ; 76 15                       ; 0xc2fe3
    cmp ax, 001e0h                            ; 3d e0 01                    ; 0xc2fe5
    je short 03002h                           ; 74 18                       ; 0xc2fe8
    cmp ax, 00190h                            ; 3d 90 01                    ; 0xc2fea
    je short 02ffeh                           ; 74 0f                       ; 0xc2fed
    jmp short 03002h                          ; eb 11                       ; 0xc2fef
    cmp ax, 000c8h                            ; 3d c8 00                    ; 0xc2ff1
    jne short 03002h                          ; 75 0c                       ; 0xc2ff4
    xor al, al                                ; 30 c0                       ; 0xc2ff6 vgabios.c:2202
    jmp short 03004h                          ; eb 0a                       ; 0xc2ff8
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc2ffa vgabios.c:2203
    jmp short 03004h                          ; eb 06                       ; 0xc2ffc
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc2ffe vgabios.c:2204
    jmp short 03004h                          ; eb 02                       ; 0xc3000
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc3002 vgabios.c:2206
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc3004 vgabios.c:2208
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3007 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc300a
    lea di, [bx+033h]                         ; 8d 7f 33                    ; 0xc300d vgabios.c:2211
    mov cx, strict word 0000dh                ; b9 0d 00                    ; 0xc3010
    xor ax, ax                                ; 31 c0                       ; 0xc3013
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3015
    jcxz 0301ch                               ; e3 02                       ; 0xc3018
    rep stosb                                 ; f3 aa                       ; 0xc301a
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc301c vgabios.c:2212
    pop di                                    ; 5f                          ; 0xc301f
    pop si                                    ; 5e                          ; 0xc3020
    pop cx                                    ; 59                          ; 0xc3021
    pop bp                                    ; 5d                          ; 0xc3022
    retn                                      ; c3                          ; 0xc3023
  ; disGetNextSymbol 0xc3024 LB 0x1295 -> off=0x0 cb=0000000000000023 uValue=00000000000c3024 'biosfn_read_video_state_size2'
biosfn_read_video_state_size2:               ; 0xc3024 LB 0x23
    push dx                                   ; 52                          ; 0xc3024 vgabios.c:2215
    push bp                                   ; 55                          ; 0xc3025
    mov bp, sp                                ; 89 e5                       ; 0xc3026
    mov dx, ax                                ; 89 c2                       ; 0xc3028
    xor ax, ax                                ; 31 c0                       ; 0xc302a vgabios.c:2219
    test dl, 001h                             ; f6 c2 01                    ; 0xc302c vgabios.c:2220
    je short 03034h                           ; 74 03                       ; 0xc302f
    mov ax, strict word 00046h                ; b8 46 00                    ; 0xc3031 vgabios.c:2221
    test dl, 002h                             ; f6 c2 02                    ; 0xc3034 vgabios.c:2223
    je short 0303ch                           ; 74 03                       ; 0xc3037
    add ax, strict word 0002ah                ; 05 2a 00                    ; 0xc3039 vgabios.c:2224
    test dl, 004h                             ; f6 c2 04                    ; 0xc303c vgabios.c:2226
    je short 03044h                           ; 74 03                       ; 0xc303f
    add ax, 00304h                            ; 05 04 03                    ; 0xc3041 vgabios.c:2227
    pop bp                                    ; 5d                          ; 0xc3044 vgabios.c:2230
    pop dx                                    ; 5a                          ; 0xc3045
    retn                                      ; c3                          ; 0xc3046
  ; disGetNextSymbol 0xc3047 LB 0x1272 -> off=0x0 cb=0000000000000018 uValue=00000000000c3047 'vga_get_video_state_size'
vga_get_video_state_size:                    ; 0xc3047 LB 0x18
    push bp                                   ; 55                          ; 0xc3047 vgabios.c:2232
    mov bp, sp                                ; 89 e5                       ; 0xc3048
    push bx                                   ; 53                          ; 0xc304a
    mov bx, dx                                ; 89 d3                       ; 0xc304b
    call 03024h                               ; e8 d4 ff                    ; 0xc304d vgabios.c:2235
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc3050
    shr ax, 006h                              ; c1 e8 06                    ; 0xc3053
    mov word [ss:bx], ax                      ; 36 89 07                    ; 0xc3056
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3059 vgabios.c:2236
    pop bx                                    ; 5b                          ; 0xc305c
    pop bp                                    ; 5d                          ; 0xc305d
    retn                                      ; c3                          ; 0xc305e
  ; disGetNextSymbol 0xc305f LB 0x125a -> off=0x0 cb=00000000000002d6 uValue=00000000000c305f 'biosfn_save_video_state'
biosfn_save_video_state:                     ; 0xc305f LB 0x2d6
    push bp                                   ; 55                          ; 0xc305f vgabios.c:2238
    mov bp, sp                                ; 89 e5                       ; 0xc3060
    push cx                                   ; 51                          ; 0xc3062
    push si                                   ; 56                          ; 0xc3063
    push di                                   ; 57                          ; 0xc3064
    push ax                                   ; 50                          ; 0xc3065
    push ax                                   ; 50                          ; 0xc3066
    push ax                                   ; 50                          ; 0xc3067
    mov cx, dx                                ; 89 d1                       ; 0xc3068
    mov si, strict word 00063h                ; be 63 00                    ; 0xc306a vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc306d
    mov es, ax                                ; 8e c0                       ; 0xc3070
    mov di, word [es:si]                      ; 26 8b 3c                    ; 0xc3072
    mov si, di                                ; 89 fe                       ; 0xc3075 vgabios.c:58
    test byte [bp-00ch], 001h                 ; f6 46 f4 01                 ; 0xc3077 vgabios.c:2243
    je near 03192h                            ; 0f 84 13 01                 ; 0xc307b
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc307f vgabios.c:2244
    in AL, DX                                 ; ec                          ; 0xc3082
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3083
    mov es, cx                                ; 8e c1                       ; 0xc3085 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3087
    inc bx                                    ; 43                          ; 0xc308a vgabios.c:2244
    mov dx, di                                ; 89 fa                       ; 0xc308b
    in AL, DX                                 ; ec                          ; 0xc308d
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc308e
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3090 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3093 vgabios.c:2245
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc3094
    in AL, DX                                 ; ec                          ; 0xc3097
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3098
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc309a vgabios.c:52
    inc bx                                    ; 43                          ; 0xc309d vgabios.c:2246
    mov dx, 003dah                            ; ba da 03                    ; 0xc309e
    in AL, DX                                 ; ec                          ; 0xc30a1
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30a2
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc30a4 vgabios.c:2248
    in AL, DX                                 ; ec                          ; 0xc30a7
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30a8
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc30aa
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc30ad vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30b0
    inc bx                                    ; 43                          ; 0xc30b3 vgabios.c:2249
    mov dx, 003cah                            ; ba ca 03                    ; 0xc30b4
    in AL, DX                                 ; ec                          ; 0xc30b7
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30b8
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30ba vgabios.c:52
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc30bd vgabios.c:2252
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc30c0
    add bx, ax                                ; 01 c3                       ; 0xc30c3 vgabios.c:2250
    jmp short 030cdh                          ; eb 06                       ; 0xc30c5
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc30c7
    jnbe short 030e5h                         ; 77 18                       ; 0xc30cb
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc30cd vgabios.c:2253
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc30d0
    out DX, AL                                ; ee                          ; 0xc30d3
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc30d4 vgabios.c:2254
    in AL, DX                                 ; ec                          ; 0xc30d7
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30d8
    mov es, cx                                ; 8e c1                       ; 0xc30da vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30dc
    inc bx                                    ; 43                          ; 0xc30df vgabios.c:2254
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc30e0 vgabios.c:2255
    jmp short 030c7h                          ; eb e2                       ; 0xc30e3
    xor al, al                                ; 30 c0                       ; 0xc30e5 vgabios.c:2256
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc30e7
    out DX, AL                                ; ee                          ; 0xc30ea
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc30eb vgabios.c:2257
    in AL, DX                                 ; ec                          ; 0xc30ee
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc30ef
    mov es, cx                                ; 8e c1                       ; 0xc30f1 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc30f3
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc30f6 vgabios.c:2259
    inc bx                                    ; 43                          ; 0xc30fb vgabios.c:2257
    jmp short 03104h                          ; eb 06                       ; 0xc30fc
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc30fe
    jnbe short 0311bh                         ; 77 17                       ; 0xc3102
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3104 vgabios.c:2260
    mov dx, si                                ; 89 f2                       ; 0xc3107
    out DX, AL                                ; ee                          ; 0xc3109
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc310a vgabios.c:2261
    in AL, DX                                 ; ec                          ; 0xc310d
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc310e
    mov es, cx                                ; 8e c1                       ; 0xc3110 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3112
    inc bx                                    ; 43                          ; 0xc3115 vgabios.c:2261
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3116 vgabios.c:2262
    jmp short 030feh                          ; eb e3                       ; 0xc3119
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc311b vgabios.c:2264
    jmp short 03128h                          ; eb 06                       ; 0xc3120
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc3122
    jnbe short 0314ch                         ; 77 24                       ; 0xc3126
    mov dx, 003dah                            ; ba da 03                    ; 0xc3128 vgabios.c:2265
    in AL, DX                                 ; ec                          ; 0xc312b
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc312c
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc312e vgabios.c:2266
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc3131
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc3134
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3137
    out DX, AL                                ; ee                          ; 0xc313a
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc313b vgabios.c:2267
    in AL, DX                                 ; ec                          ; 0xc313e
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc313f
    mov es, cx                                ; 8e c1                       ; 0xc3141 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3143
    inc bx                                    ; 43                          ; 0xc3146 vgabios.c:2267
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3147 vgabios.c:2268
    jmp short 03122h                          ; eb d6                       ; 0xc314a
    mov dx, 003dah                            ; ba da 03                    ; 0xc314c vgabios.c:2269
    in AL, DX                                 ; ec                          ; 0xc314f
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3150
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3152 vgabios.c:2271
    jmp short 0315fh                          ; eb 06                       ; 0xc3157
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc3159
    jnbe short 03177h                         ; 77 18                       ; 0xc315d
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc315f vgabios.c:2272
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc3162
    out DX, AL                                ; ee                          ; 0xc3165
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc3166 vgabios.c:2273
    in AL, DX                                 ; ec                          ; 0xc3169
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc316a
    mov es, cx                                ; 8e c1                       ; 0xc316c vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc316e
    inc bx                                    ; 43                          ; 0xc3171 vgabios.c:2273
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3172 vgabios.c:2274
    jmp short 03159h                          ; eb e2                       ; 0xc3175
    mov es, cx                                ; 8e c1                       ; 0xc3177 vgabios.c:62
    mov word [es:bx], si                      ; 26 89 37                    ; 0xc3179
    inc bx                                    ; 43                          ; 0xc317c vgabios.c:2276
    inc bx                                    ; 43                          ; 0xc317d
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc317e vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3182 vgabios.c:2279
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3183 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3187 vgabios.c:2280
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3188 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc318c vgabios.c:2281
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc318d vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3191 vgabios.c:2282
    test byte [bp-00ch], 002h                 ; f6 46 f4 02                 ; 0xc3192 vgabios.c:2284
    je near 032d9h                            ; 0f 84 3f 01                 ; 0xc3196
    mov si, strict word 00049h                ; be 49 00                    ; 0xc319a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc319d
    mov es, ax                                ; 8e c0                       ; 0xc31a0
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc31a2
    mov es, cx                                ; 8e c1                       ; 0xc31a5 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc31a7
    inc bx                                    ; 43                          ; 0xc31aa vgabios.c:2285
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc31ab vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31ae
    mov es, ax                                ; 8e c0                       ; 0xc31b1
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc31b3
    mov es, cx                                ; 8e c1                       ; 0xc31b6 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc31b8
    inc bx                                    ; 43                          ; 0xc31bb vgabios.c:2286
    inc bx                                    ; 43                          ; 0xc31bc
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc31bd vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31c0
    mov es, ax                                ; 8e c0                       ; 0xc31c3
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc31c5
    mov es, cx                                ; 8e c1                       ; 0xc31c8 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc31ca
    inc bx                                    ; 43                          ; 0xc31cd vgabios.c:2287
    inc bx                                    ; 43                          ; 0xc31ce
    mov si, strict word 00063h                ; be 63 00                    ; 0xc31cf vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31d2
    mov es, ax                                ; 8e c0                       ; 0xc31d5
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc31d7
    mov es, cx                                ; 8e c1                       ; 0xc31da vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc31dc
    inc bx                                    ; 43                          ; 0xc31df vgabios.c:2288
    inc bx                                    ; 43                          ; 0xc31e0
    mov si, 00084h                            ; be 84 00                    ; 0xc31e1 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31e4
    mov es, ax                                ; 8e c0                       ; 0xc31e7
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc31e9
    mov es, cx                                ; 8e c1                       ; 0xc31ec vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc31ee
    inc bx                                    ; 43                          ; 0xc31f1 vgabios.c:2289
    mov si, 00085h                            ; be 85 00                    ; 0xc31f2 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc31f5
    mov es, ax                                ; 8e c0                       ; 0xc31f8
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc31fa
    mov es, cx                                ; 8e c1                       ; 0xc31fd vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc31ff
    inc bx                                    ; 43                          ; 0xc3202 vgabios.c:2290
    inc bx                                    ; 43                          ; 0xc3203
    mov si, 00087h                            ; be 87 00                    ; 0xc3204 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3207
    mov es, ax                                ; 8e c0                       ; 0xc320a
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc320c
    mov es, cx                                ; 8e c1                       ; 0xc320f vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3211
    inc bx                                    ; 43                          ; 0xc3214 vgabios.c:2291
    mov si, 00088h                            ; be 88 00                    ; 0xc3215 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3218
    mov es, ax                                ; 8e c0                       ; 0xc321b
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc321d
    mov es, cx                                ; 8e c1                       ; 0xc3220 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3222
    inc bx                                    ; 43                          ; 0xc3225 vgabios.c:2292
    mov si, 00089h                            ; be 89 00                    ; 0xc3226 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3229
    mov es, ax                                ; 8e c0                       ; 0xc322c
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc322e
    mov es, cx                                ; 8e c1                       ; 0xc3231 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3233
    inc bx                                    ; 43                          ; 0xc3236 vgabios.c:2293
    mov si, strict word 00060h                ; be 60 00                    ; 0xc3237 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc323a
    mov es, ax                                ; 8e c0                       ; 0xc323d
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc323f
    mov es, cx                                ; 8e c1                       ; 0xc3242 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3244
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3247 vgabios.c:2295
    inc bx                                    ; 43                          ; 0xc324c vgabios.c:2294
    inc bx                                    ; 43                          ; 0xc324d
    jmp short 03256h                          ; eb 06                       ; 0xc324e
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc3250
    jnc short 03272h                          ; 73 1c                       ; 0xc3254
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc3256 vgabios.c:2296
    add si, si                                ; 01 f6                       ; 0xc3259
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc325b
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc325e vgabios.c:57
    mov es, ax                                ; 8e c0                       ; 0xc3261
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3263
    mov es, cx                                ; 8e c1                       ; 0xc3266 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3268
    inc bx                                    ; 43                          ; 0xc326b vgabios.c:2297
    inc bx                                    ; 43                          ; 0xc326c
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc326d vgabios.c:2298
    jmp short 03250h                          ; eb de                       ; 0xc3270
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc3272 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3275
    mov es, ax                                ; 8e c0                       ; 0xc3278
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc327a
    mov es, cx                                ; 8e c1                       ; 0xc327d vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc327f
    inc bx                                    ; 43                          ; 0xc3282 vgabios.c:2299
    inc bx                                    ; 43                          ; 0xc3283
    mov si, strict word 00062h                ; be 62 00                    ; 0xc3284 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3287
    mov es, ax                                ; 8e c0                       ; 0xc328a
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc328c
    mov es, cx                                ; 8e c1                       ; 0xc328f vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3291
    inc bx                                    ; 43                          ; 0xc3294 vgabios.c:2300
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc3295 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc3298
    mov es, ax                                ; 8e c0                       ; 0xc329a
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc329c
    mov es, cx                                ; 8e c1                       ; 0xc329f vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32a1
    inc bx                                    ; 43                          ; 0xc32a4 vgabios.c:2302
    inc bx                                    ; 43                          ; 0xc32a5
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc32a6 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc32a9
    mov es, ax                                ; 8e c0                       ; 0xc32ab
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc32ad
    mov es, cx                                ; 8e c1                       ; 0xc32b0 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32b2
    inc bx                                    ; 43                          ; 0xc32b5 vgabios.c:2303
    inc bx                                    ; 43                          ; 0xc32b6
    mov si, 0010ch                            ; be 0c 01                    ; 0xc32b7 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc32ba
    mov es, ax                                ; 8e c0                       ; 0xc32bc
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc32be
    mov es, cx                                ; 8e c1                       ; 0xc32c1 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32c3
    inc bx                                    ; 43                          ; 0xc32c6 vgabios.c:2304
    inc bx                                    ; 43                          ; 0xc32c7
    mov si, 0010eh                            ; be 0e 01                    ; 0xc32c8 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc32cb
    mov es, ax                                ; 8e c0                       ; 0xc32cd
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc32cf
    mov es, cx                                ; 8e c1                       ; 0xc32d2 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc32d4
    inc bx                                    ; 43                          ; 0xc32d7 vgabios.c:2305
    inc bx                                    ; 43                          ; 0xc32d8
    test byte [bp-00ch], 004h                 ; f6 46 f4 04                 ; 0xc32d9 vgabios.c:2307
    je short 0332bh                           ; 74 4c                       ; 0xc32dd
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc32df vgabios.c:2309
    in AL, DX                                 ; ec                          ; 0xc32e2
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32e3
    mov es, cx                                ; 8e c1                       ; 0xc32e5 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32e7
    inc bx                                    ; 43                          ; 0xc32ea vgabios.c:2309
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc32eb
    in AL, DX                                 ; ec                          ; 0xc32ee
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32ef
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32f1 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc32f4 vgabios.c:2310
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc32f5
    in AL, DX                                 ; ec                          ; 0xc32f8
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc32f9
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc32fb vgabios.c:52
    inc bx                                    ; 43                          ; 0xc32fe vgabios.c:2311
    xor al, al                                ; 30 c0                       ; 0xc32ff
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc3301
    out DX, AL                                ; ee                          ; 0xc3304
    xor ah, ah                                ; 30 e4                       ; 0xc3305 vgabios.c:2314
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3307
    jmp short 03313h                          ; eb 07                       ; 0xc330a
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc330c
    jnc short 03324h                          ; 73 11                       ; 0xc3311
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc3313 vgabios.c:2315
    in AL, DX                                 ; ec                          ; 0xc3316
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3317
    mov es, cx                                ; 8e c1                       ; 0xc3319 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc331b
    inc bx                                    ; 43                          ; 0xc331e vgabios.c:2315
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc331f vgabios.c:2316
    jmp short 0330ch                          ; eb e8                       ; 0xc3322
    mov es, cx                                ; 8e c1                       ; 0xc3324 vgabios.c:52
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3326
    inc bx                                    ; 43                          ; 0xc332a vgabios.c:2317
    mov ax, bx                                ; 89 d8                       ; 0xc332b vgabios.c:2320
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc332d
    pop di                                    ; 5f                          ; 0xc3330
    pop si                                    ; 5e                          ; 0xc3331
    pop cx                                    ; 59                          ; 0xc3332
    pop bp                                    ; 5d                          ; 0xc3333
    retn                                      ; c3                          ; 0xc3334
  ; disGetNextSymbol 0xc3335 LB 0xf84 -> off=0x0 cb=00000000000002b8 uValue=00000000000c3335 'biosfn_restore_video_state'
biosfn_restore_video_state:                  ; 0xc3335 LB 0x2b8
    push bp                                   ; 55                          ; 0xc3335 vgabios.c:2322
    mov bp, sp                                ; 89 e5                       ; 0xc3336
    push cx                                   ; 51                          ; 0xc3338
    push si                                   ; 56                          ; 0xc3339
    push di                                   ; 57                          ; 0xc333a
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc333b
    push ax                                   ; 50                          ; 0xc333e
    mov cx, dx                                ; 89 d1                       ; 0xc333f
    test byte [bp-010h], 001h                 ; f6 46 f0 01                 ; 0xc3341 vgabios.c:2326
    je near 0347dh                            ; 0f 84 34 01                 ; 0xc3345
    mov dx, 003dah                            ; ba da 03                    ; 0xc3349 vgabios.c:2328
    in AL, DX                                 ; ec                          ; 0xc334c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc334d
    lea si, [bx+040h]                         ; 8d 77 40                    ; 0xc334f vgabios.c:2330
    mov es, cx                                ; 8e c1                       ; 0xc3352 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3354
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc3357 vgabios.c:58
    mov si, bx                                ; 89 de                       ; 0xc335a vgabios.c:2331
    mov word [bp-00eh], strict word 00001h    ; c7 46 f2 01 00              ; 0xc335c vgabios.c:2334
    add bx, strict byte 00005h                ; 83 c3 05                    ; 0xc3361 vgabios.c:2332
    jmp short 0336ch                          ; eb 06                       ; 0xc3364
    cmp word [bp-00eh], strict byte 00004h    ; 83 7e f2 04                 ; 0xc3366
    jnbe short 03382h                         ; 77 16                       ; 0xc336a
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc336c vgabios.c:2335
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc336f
    out DX, AL                                ; ee                          ; 0xc3372
    mov es, cx                                ; 8e c1                       ; 0xc3373 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3375
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3378 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc337b
    inc bx                                    ; 43                          ; 0xc337c vgabios.c:2336
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc337d vgabios.c:2337
    jmp short 03366h                          ; eb e4                       ; 0xc3380
    xor al, al                                ; 30 c0                       ; 0xc3382 vgabios.c:2338
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3384
    out DX, AL                                ; ee                          ; 0xc3387
    mov es, cx                                ; 8e c1                       ; 0xc3388 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc338a
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc338d vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3390
    inc bx                                    ; 43                          ; 0xc3391 vgabios.c:2339
    mov dx, 003cch                            ; ba cc 03                    ; 0xc3392
    in AL, DX                                 ; ec                          ; 0xc3395
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3396
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc3398
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc339a
    cmp word [bp-00ah], 003d4h                ; 81 7e f6 d4 03              ; 0xc339d vgabios.c:2343
    jne short 033a8h                          ; 75 04                       ; 0xc33a2
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc33a4 vgabios.c:2344
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc33a8 vgabios.c:2345
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc33ab
    out DX, AL                                ; ee                          ; 0xc33ae
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc33af vgabios.c:2348
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc33b2
    out DX, ax                                ; ef                          ; 0xc33b5
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc33b6 vgabios.c:2350
    jmp short 033c3h                          ; eb 06                       ; 0xc33bb
    cmp word [bp-00eh], strict byte 00018h    ; 83 7e f2 18                 ; 0xc33bd
    jnbe short 033ddh                         ; 77 1a                       ; 0xc33c1
    cmp word [bp-00eh], strict byte 00011h    ; 83 7e f2 11                 ; 0xc33c3 vgabios.c:2351
    je short 033d7h                           ; 74 0e                       ; 0xc33c7
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc33c9 vgabios.c:2352
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc33cc
    out DX, AL                                ; ee                          ; 0xc33cf
    mov es, cx                                ; 8e c1                       ; 0xc33d0 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc33d2
    inc dx                                    ; 42                          ; 0xc33d5 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc33d6
    inc bx                                    ; 43                          ; 0xc33d7 vgabios.c:2355
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc33d8 vgabios.c:2356
    jmp short 033bdh                          ; eb e0                       ; 0xc33db
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc33dd vgabios.c:2358
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc33df
    out DX, AL                                ; ee                          ; 0xc33e2
    lea di, [word bx-00007h]                  ; 8d bf f9 ff                 ; 0xc33e3 vgabios.c:2359
    mov es, cx                                ; 8e c1                       ; 0xc33e7 vgabios.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc33e9
    inc dx                                    ; 42                          ; 0xc33ec vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc33ed
    lea di, [si+003h]                         ; 8d 7c 03                    ; 0xc33ee vgabios.c:2362
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc33f1 vgabios.c:47
    xor ah, ah                                ; 30 e4                       ; 0xc33f4 vgabios.c:48
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc33f6
    mov dx, 003dah                            ; ba da 03                    ; 0xc33f9 vgabios.c:2363
    in AL, DX                                 ; ec                          ; 0xc33fc
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc33fd
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc33ff vgabios.c:2364
    jmp short 0340ch                          ; eb 06                       ; 0xc3404
    cmp word [bp-00eh], strict byte 00013h    ; 83 7e f2 13                 ; 0xc3406
    jnbe short 03425h                         ; 77 19                       ; 0xc340a
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc340c vgabios.c:2365
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc340f
    or ax, word [bp-00eh]                     ; 0b 46 f2                    ; 0xc3412
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3415
    out DX, AL                                ; ee                          ; 0xc3418
    mov es, cx                                ; 8e c1                       ; 0xc3419 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc341b
    out DX, AL                                ; ee                          ; 0xc341e vgabios.c:48
    inc bx                                    ; 43                          ; 0xc341f vgabios.c:2366
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc3420 vgabios.c:2367
    jmp short 03406h                          ; eb e1                       ; 0xc3423
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc3425 vgabios.c:2368
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3428
    out DX, AL                                ; ee                          ; 0xc342b
    mov dx, 003dah                            ; ba da 03                    ; 0xc342c vgabios.c:2369
    in AL, DX                                 ; ec                          ; 0xc342f
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3430
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3432 vgabios.c:2371
    jmp short 0343fh                          ; eb 06                       ; 0xc3437
    cmp word [bp-00eh], strict byte 00008h    ; 83 7e f2 08                 ; 0xc3439
    jnbe short 03455h                         ; 77 16                       ; 0xc343d
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc343f vgabios.c:2372
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc3442
    out DX, AL                                ; ee                          ; 0xc3445
    mov es, cx                                ; 8e c1                       ; 0xc3446 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3448
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc344b vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc344e
    inc bx                                    ; 43                          ; 0xc344f vgabios.c:2373
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc3450 vgabios.c:2374
    jmp short 03439h                          ; eb e4                       ; 0xc3453
    add bx, strict byte 00006h                ; 83 c3 06                    ; 0xc3455 vgabios.c:2375
    mov es, cx                                ; 8e c1                       ; 0xc3458 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc345a
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc345d vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3460
    inc si                                    ; 46                          ; 0xc3461 vgabios.c:2378
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3462 vgabios.c:47
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc3465 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3468
    inc si                                    ; 46                          ; 0xc3469 vgabios.c:2379
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc346a vgabios.c:47
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc346d vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3470
    inc si                                    ; 46                          ; 0xc3471 vgabios.c:2380
    inc si                                    ; 46                          ; 0xc3472
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3473 vgabios.c:47
    mov dx, word [bp-00ah]                    ; 8b 56 f6                    ; 0xc3476 vgabios.c:48
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc3479
    out DX, AL                                ; ee                          ; 0xc347c
    test byte [bp-010h], 002h                 ; f6 46 f0 02                 ; 0xc347d vgabios.c:2384
    je near 035a0h                            ; 0f 84 1b 01                 ; 0xc3481
    mov es, cx                                ; 8e c1                       ; 0xc3485 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3487
    mov si, strict word 00049h                ; be 49 00                    ; 0xc348a vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc348d
    mov es, dx                                ; 8e c2                       ; 0xc3490
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3492
    inc bx                                    ; 43                          ; 0xc3495 vgabios.c:2385
    mov es, cx                                ; 8e c1                       ; 0xc3496 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3498
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc349b vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc349e
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34a0
    inc bx                                    ; 43                          ; 0xc34a3 vgabios.c:2386
    inc bx                                    ; 43                          ; 0xc34a4
    mov es, cx                                ; 8e c1                       ; 0xc34a5 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc34a7
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc34aa vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc34ad
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34af
    inc bx                                    ; 43                          ; 0xc34b2 vgabios.c:2387
    inc bx                                    ; 43                          ; 0xc34b3
    mov es, cx                                ; 8e c1                       ; 0xc34b4 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc34b6
    mov si, strict word 00063h                ; be 63 00                    ; 0xc34b9 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc34bc
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34be
    inc bx                                    ; 43                          ; 0xc34c1 vgabios.c:2388
    inc bx                                    ; 43                          ; 0xc34c2
    mov es, cx                                ; 8e c1                       ; 0xc34c3 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc34c5
    mov si, 00084h                            ; be 84 00                    ; 0xc34c8 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc34cb
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc34cd
    inc bx                                    ; 43                          ; 0xc34d0 vgabios.c:2389
    mov es, cx                                ; 8e c1                       ; 0xc34d1 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc34d3
    mov si, 00085h                            ; be 85 00                    ; 0xc34d6 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc34d9
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc34db
    inc bx                                    ; 43                          ; 0xc34de vgabios.c:2390
    inc bx                                    ; 43                          ; 0xc34df
    mov es, cx                                ; 8e c1                       ; 0xc34e0 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc34e2
    mov si, 00087h                            ; be 87 00                    ; 0xc34e5 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc34e8
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc34ea
    inc bx                                    ; 43                          ; 0xc34ed vgabios.c:2391
    mov es, cx                                ; 8e c1                       ; 0xc34ee vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc34f0
    mov si, 00088h                            ; be 88 00                    ; 0xc34f3 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc34f6
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc34f8
    inc bx                                    ; 43                          ; 0xc34fb vgabios.c:2392
    mov es, cx                                ; 8e c1                       ; 0xc34fc vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc34fe
    mov si, 00089h                            ; be 89 00                    ; 0xc3501 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3504
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3506
    inc bx                                    ; 43                          ; 0xc3509 vgabios.c:2393
    mov es, cx                                ; 8e c1                       ; 0xc350a vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc350c
    mov si, strict word 00060h                ; be 60 00                    ; 0xc350f vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3512
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3514
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3517 vgabios.c:2395
    inc bx                                    ; 43                          ; 0xc351c vgabios.c:2394
    inc bx                                    ; 43                          ; 0xc351d
    jmp short 03526h                          ; eb 06                       ; 0xc351e
    cmp word [bp-00eh], strict byte 00008h    ; 83 7e f2 08                 ; 0xc3520
    jnc short 03542h                          ; 73 1c                       ; 0xc3524
    mov es, cx                                ; 8e c1                       ; 0xc3526 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3528
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc352b vgabios.c:58
    add si, si                                ; 01 f6                       ; 0xc352e
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc3530
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3533 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3536
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3538
    inc bx                                    ; 43                          ; 0xc353b vgabios.c:2397
    inc bx                                    ; 43                          ; 0xc353c
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc353d vgabios.c:2398
    jmp short 03520h                          ; eb de                       ; 0xc3540
    mov es, cx                                ; 8e c1                       ; 0xc3542 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3544
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc3547 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc354a
    mov es, dx                                ; 8e c2                       ; 0xc354d
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc354f
    inc bx                                    ; 43                          ; 0xc3552 vgabios.c:2399
    inc bx                                    ; 43                          ; 0xc3553
    mov es, cx                                ; 8e c1                       ; 0xc3554 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3556
    mov si, strict word 00062h                ; be 62 00                    ; 0xc3559 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc355c
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc355e
    inc bx                                    ; 43                          ; 0xc3561 vgabios.c:2400
    mov es, cx                                ; 8e c1                       ; 0xc3562 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3564
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc3567 vgabios.c:62
    xor dx, dx                                ; 31 d2                       ; 0xc356a
    mov es, dx                                ; 8e c2                       ; 0xc356c
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc356e
    inc bx                                    ; 43                          ; 0xc3571 vgabios.c:2402
    inc bx                                    ; 43                          ; 0xc3572
    mov es, cx                                ; 8e c1                       ; 0xc3573 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3575
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc3578 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc357b
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc357d
    inc bx                                    ; 43                          ; 0xc3580 vgabios.c:2403
    inc bx                                    ; 43                          ; 0xc3581
    mov es, cx                                ; 8e c1                       ; 0xc3582 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3584
    mov si, 0010ch                            ; be 0c 01                    ; 0xc3587 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc358a
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc358c
    inc bx                                    ; 43                          ; 0xc358f vgabios.c:2404
    inc bx                                    ; 43                          ; 0xc3590
    mov es, cx                                ; 8e c1                       ; 0xc3591 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3593
    mov si, 0010eh                            ; be 0e 01                    ; 0xc3596 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3599
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc359b
    inc bx                                    ; 43                          ; 0xc359e vgabios.c:2405
    inc bx                                    ; 43                          ; 0xc359f
    test byte [bp-010h], 004h                 ; f6 46 f0 04                 ; 0xc35a0 vgabios.c:2407
    je short 035e3h                           ; 74 3d                       ; 0xc35a4
    inc bx                                    ; 43                          ; 0xc35a6 vgabios.c:2408
    mov es, cx                                ; 8e c1                       ; 0xc35a7 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35a9
    xor ah, ah                                ; 30 e4                       ; 0xc35ac vgabios.c:48
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc35ae
    inc bx                                    ; 43                          ; 0xc35b1 vgabios.c:2409
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35b2 vgabios.c:47
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc35b5 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35b8
    inc bx                                    ; 43                          ; 0xc35b9 vgabios.c:2410
    xor al, al                                ; 30 c0                       ; 0xc35ba
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc35bc
    out DX, AL                                ; ee                          ; 0xc35bf
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc35c0 vgabios.c:2413
    jmp short 035cch                          ; eb 07                       ; 0xc35c3
    cmp word [bp-00eh], 00300h                ; 81 7e f2 00 03              ; 0xc35c5
    jnc short 035dbh                          ; 73 0f                       ; 0xc35ca
    mov es, cx                                ; 8e c1                       ; 0xc35cc vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc35ce
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc35d1 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc35d4
    inc bx                                    ; 43                          ; 0xc35d5 vgabios.c:2414
    inc word [bp-00eh]                        ; ff 46 f2                    ; 0xc35d6 vgabios.c:2415
    jmp short 035c5h                          ; eb ea                       ; 0xc35d9
    inc bx                                    ; 43                          ; 0xc35db vgabios.c:2416
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc35dc
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc35df
    out DX, AL                                ; ee                          ; 0xc35e2
    mov ax, bx                                ; 89 d8                       ; 0xc35e3 vgabios.c:2420
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc35e5
    pop di                                    ; 5f                          ; 0xc35e8
    pop si                                    ; 5e                          ; 0xc35e9
    pop cx                                    ; 59                          ; 0xc35ea
    pop bp                                    ; 5d                          ; 0xc35eb
    retn                                      ; c3                          ; 0xc35ec
  ; disGetNextSymbol 0xc35ed LB 0xccc -> off=0x0 cb=0000000000000027 uValue=00000000000c35ed 'find_vga_entry'
find_vga_entry:                              ; 0xc35ed LB 0x27
    push bx                                   ; 53                          ; 0xc35ed vgabios.c:2429
    push dx                                   ; 52                          ; 0xc35ee
    push bp                                   ; 55                          ; 0xc35ef
    mov bp, sp                                ; 89 e5                       ; 0xc35f0
    mov dl, al                                ; 88 c2                       ; 0xc35f2
    mov AH, strict byte 0ffh                  ; b4 ff                       ; 0xc35f4 vgabios.c:2431
    xor al, al                                ; 30 c0                       ; 0xc35f6 vgabios.c:2432
    jmp short 03600h                          ; eb 06                       ; 0xc35f8
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc35fa vgabios.c:2433
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc35fc
    jnbe short 0360eh                         ; 77 0e                       ; 0xc35fe
    movzx bx, al                              ; 0f b6 d8                    ; 0xc3600
    sal bx, 003h                              ; c1 e3 03                    ; 0xc3603
    cmp dl, byte [bx+047b1h]                  ; 3a 97 b1 47                 ; 0xc3606
    jne short 035fah                          ; 75 ee                       ; 0xc360a
    mov ah, al                                ; 88 c4                       ; 0xc360c
    mov al, ah                                ; 88 e0                       ; 0xc360e vgabios.c:2438
    pop bp                                    ; 5d                          ; 0xc3610
    pop dx                                    ; 5a                          ; 0xc3611
    pop bx                                    ; 5b                          ; 0xc3612
    retn                                      ; c3                          ; 0xc3613
  ; disGetNextSymbol 0xc3614 LB 0xca5 -> off=0x0 cb=000000000000000e uValue=00000000000c3614 'readx_byte'
readx_byte:                                  ; 0xc3614 LB 0xe
    push bx                                   ; 53                          ; 0xc3614 vgabios.c:2450
    push bp                                   ; 55                          ; 0xc3615
    mov bp, sp                                ; 89 e5                       ; 0xc3616
    mov bx, dx                                ; 89 d3                       ; 0xc3618
    mov es, ax                                ; 8e c0                       ; 0xc361a vgabios.c:2452
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc361c
    pop bp                                    ; 5d                          ; 0xc361f vgabios.c:2453
    pop bx                                    ; 5b                          ; 0xc3620
    retn                                      ; c3                          ; 0xc3621
  ; disGetNextSymbol 0xc3622 LB 0xc97 -> off=0x8a cb=000000000000047c uValue=00000000000c36ac 'int10_func'
    db  056h, 04fh, 01ch, 01bh, 013h, 012h, 011h, 010h, 00eh, 00dh, 00ch, 00ah, 009h, 008h, 007h, 006h
    db  005h, 004h, 003h, 002h, 001h, 000h, 021h, 03bh, 0d7h, 036h, 014h, 037h, 028h, 037h, 039h, 037h
    db  04dh, 037h, 05eh, 037h, 069h, 037h, 0a3h, 037h, 0a7h, 037h, 0b8h, 037h, 0d5h, 037h, 0f2h, 037h
    db  012h, 038h, 02fh, 038h, 046h, 038h, 052h, 038h, 057h, 039h, 0e4h, 039h, 011h, 03ah, 026h, 03ah
    db  068h, 03ah, 0f3h, 03ah, 030h, 024h, 023h, 022h, 021h, 020h, 014h, 012h, 011h, 010h, 004h, 003h
    db  002h, 001h, 000h, 021h, 03bh, 073h, 038h, 093h, 038h, 0afh, 038h, 0c4h, 038h, 0cfh, 038h, 073h
    db  038h, 093h, 038h, 0afh, 038h, 0cfh, 038h, 0e4h, 038h, 0f0h, 038h, 00bh, 039h, 01ch, 039h, 02dh
    db  039h, 03eh, 039h, 00ah, 009h, 006h, 004h, 002h, 001h, 000h, 0e5h, 03ah, 090h, 03ah, 09eh, 03ah
    db  0afh, 03ah, 0bfh, 03ah, 0d4h, 03ah, 0e5h, 03ah, 0e5h, 03ah
int10_func:                                  ; 0xc36ac LB 0x47c
    push bp                                   ; 55                          ; 0xc36ac vgabios.c:2531
    mov bp, sp                                ; 89 e5                       ; 0xc36ad
    push si                                   ; 56                          ; 0xc36af
    push di                                   ; 57                          ; 0xc36b0
    push ax                                   ; 50                          ; 0xc36b1
    mov si, word [bp+004h]                    ; 8b 76 04                    ; 0xc36b2
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc36b5 vgabios.c:2536
    shr ax, 008h                              ; c1 e8 08                    ; 0xc36b8
    cmp ax, strict word 00056h                ; 3d 56 00                    ; 0xc36bb
    jnbe near 03b21h                          ; 0f 87 5f 04                 ; 0xc36be
    push CS                                   ; 0e                          ; 0xc36c2
    pop ES                                    ; 07                          ; 0xc36c3
    mov cx, strict word 00017h                ; b9 17 00                    ; 0xc36c4
    mov di, 03622h                            ; bf 22 36                    ; 0xc36c7
    repne scasb                               ; f2 ae                       ; 0xc36ca
    sal cx, 1                                 ; d1 e1                       ; 0xc36cc
    mov di, cx                                ; 89 cf                       ; 0xc36ce
    mov ax, word [cs:di+03638h]               ; 2e 8b 85 38 36              ; 0xc36d0
    jmp ax                                    ; ff e0                       ; 0xc36d5
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc36d7 vgabios.c:2539
    call 013ddh                               ; e8 ff dc                    ; 0xc36db
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc36de vgabios.c:2540
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc36e1
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc36e4
    je short 036feh                           ; 74 15                       ; 0xc36e7
    cmp ax, strict word 00006h                ; 3d 06 00                    ; 0xc36e9
    je short 036f5h                           ; 74 07                       ; 0xc36ec
    cmp ax, strict word 00005h                ; 3d 05 00                    ; 0xc36ee
    jbe short 036feh                          ; 76 0b                       ; 0xc36f1
    jmp short 03707h                          ; eb 12                       ; 0xc36f3
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc36f5 vgabios.c:2542
    xor al, al                                ; 30 c0                       ; 0xc36f8
    or AL, strict byte 03fh                   ; 0c 3f                       ; 0xc36fa
    jmp short 0370eh                          ; eb 10                       ; 0xc36fc vgabios.c:2543
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc36fe vgabios.c:2551
    xor al, al                                ; 30 c0                       ; 0xc3701
    or AL, strict byte 030h                   ; 0c 30                       ; 0xc3703
    jmp short 0370eh                          ; eb 07                       ; 0xc3705
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3707 vgabios.c:2554
    xor al, al                                ; 30 c0                       ; 0xc370a
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc370c
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc370e
    jmp near 03b21h                           ; e9 0d 04                    ; 0xc3711 vgabios.c:2556
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc3714 vgabios.c:2558
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3717
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc371a
    shr ax, 008h                              ; c1 e8 08                    ; 0xc371d
    xor ah, ah                                ; 30 e4                       ; 0xc3720
    call 0114ch                               ; e8 27 da                    ; 0xc3722
    jmp near 03b21h                           ; e9 f9 03                    ; 0xc3725 vgabios.c:2559
    mov dx, word [bp+00eh]                    ; 8b 56 0e                    ; 0xc3728 vgabios.c:2561
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc372b
    shr ax, 008h                              ; c1 e8 08                    ; 0xc372e
    xor ah, ah                                ; 30 e4                       ; 0xc3731
    call 01242h                               ; e8 0c db                    ; 0xc3733
    jmp near 03b21h                           ; e9 e8 03                    ; 0xc3736 vgabios.c:2562
    lea bx, [bp+00eh]                         ; 8d 5e 0e                    ; 0xc3739 vgabios.c:2564
    lea dx, [bp+010h]                         ; 8d 56 10                    ; 0xc373c
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc373f
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3742
    xor ah, ah                                ; 30 e4                       ; 0xc3745
    call 00a93h                               ; e8 49 d3                    ; 0xc3747
    jmp near 03b21h                           ; e9 d4 03                    ; 0xc374a vgabios.c:2565
    xor ax, ax                                ; 31 c0                       ; 0xc374d vgabios.c:2571
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc374f
    mov word [bp+00ch], ax                    ; 89 46 0c                    ; 0xc3752 vgabios.c:2572
    mov word [bp+010h], ax                    ; 89 46 10                    ; 0xc3755 vgabios.c:2573
    mov word [bp+00eh], ax                    ; 89 46 0e                    ; 0xc3758 vgabios.c:2574
    jmp near 03b21h                           ; e9 c3 03                    ; 0xc375b vgabios.c:2575
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc375e vgabios.c:2577
    xor ah, ah                                ; 30 e4                       ; 0xc3761
    call 012cbh                               ; e8 65 db                    ; 0xc3763
    jmp near 03b21h                           ; e9 b8 03                    ; 0xc3766 vgabios.c:2578
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3769 vgabios.c:2580
    push ax                                   ; 50                          ; 0xc376c
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc376d
    push ax                                   ; 50                          ; 0xc3770
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3771
    xor ah, ah                                ; 30 e4                       ; 0xc3774
    push ax                                   ; 50                          ; 0xc3776
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc3777
    shr ax, 008h                              ; c1 e8 08                    ; 0xc377a
    xor ah, ah                                ; 30 e4                       ; 0xc377d
    push ax                                   ; 50                          ; 0xc377f
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc3780
    movzx cx, al                              ; 0f b6 c8                    ; 0xc3783
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3786
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3789
    movzx bx, al                              ; 0f b6 d8                    ; 0xc378c
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc378f
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3792
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3795
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3798
    xor ah, ah                                ; 30 e4                       ; 0xc379b
    call 01b5dh                               ; e8 bd e3                    ; 0xc379d
    jmp near 03b21h                           ; e9 7e 03                    ; 0xc37a0 vgabios.c:2581
    xor ax, ax                                ; 31 c0                       ; 0xc37a3 vgabios.c:2583
    jmp short 0376ch                          ; eb c5                       ; 0xc37a5
    lea dx, [bp+012h]                         ; 8d 56 12                    ; 0xc37a7 vgabios.c:2586
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc37aa
    shr ax, 008h                              ; c1 e8 08                    ; 0xc37ad
    xor ah, ah                                ; 30 e4                       ; 0xc37b0
    call 00db0h                               ; e8 fb d5                    ; 0xc37b2
    jmp near 03b21h                           ; e9 69 03                    ; 0xc37b5 vgabios.c:2587
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc37b8 vgabios.c:2589
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc37bb
    movzx bx, al                              ; 0f b6 d8                    ; 0xc37be
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc37c1
    shr ax, 008h                              ; c1 e8 08                    ; 0xc37c4
    movzx dx, al                              ; 0f b6 d0                    ; 0xc37c7
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc37ca
    xor ah, ah                                ; 30 e4                       ; 0xc37cd
    call 023e6h                               ; e8 14 ec                    ; 0xc37cf
    jmp near 03b21h                           ; e9 4c 03                    ; 0xc37d2 vgabios.c:2590
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc37d5 vgabios.c:2592
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc37d8
    movzx bx, al                              ; 0f b6 d8                    ; 0xc37db
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc37de
    shr ax, 008h                              ; c1 e8 08                    ; 0xc37e1
    movzx dx, al                              ; 0f b6 d0                    ; 0xc37e4
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc37e7
    xor ah, ah                                ; 30 e4                       ; 0xc37ea
    call 0254bh                               ; e8 5c ed                    ; 0xc37ec
    jmp near 03b21h                           ; e9 2f 03                    ; 0xc37ef vgabios.c:2593
    mov cx, word [bp+00eh]                    ; 8b 4e 0e                    ; 0xc37f2 vgabios.c:2595
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc37f5
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc37f8
    movzx dx, al                              ; 0f b6 d0                    ; 0xc37fb
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc37fe
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3801
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc3804
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3807
    xor ah, ah                                ; 30 e4                       ; 0xc380a
    call 026adh                               ; e8 9e ee                    ; 0xc380c
    jmp near 03b21h                           ; e9 0f 03                    ; 0xc380f vgabios.c:2596
    lea cx, [bp+012h]                         ; 8d 4e 12                    ; 0xc3812 vgabios.c:2598
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3815
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3818
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc381b
    shr ax, 008h                              ; c1 e8 08                    ; 0xc381e
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc3821
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3824
    xor ah, ah                                ; 30 e4                       ; 0xc3827
    call 00f6ah                               ; e8 3e d7                    ; 0xc3829
    jmp near 03b21h                           ; e9 f2 02                    ; 0xc382c vgabios.c:2599
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc382f vgabios.c:2607
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3832
    movzx bx, al                              ; 0f b6 d8                    ; 0xc3835
    mov dx, 000ffh                            ; ba ff 00                    ; 0xc3838
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc383b
    xor ah, ah                                ; 30 e4                       ; 0xc383e
    call 02812h                               ; e8 cf ef                    ; 0xc3840
    jmp near 03b21h                           ; e9 db 02                    ; 0xc3843 vgabios.c:2608
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3846 vgabios.c:2611
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3849
    call 010c0h                               ; e8 71 d8                    ; 0xc384c
    jmp near 03b21h                           ; e9 cf 02                    ; 0xc384f vgabios.c:2612
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3852 vgabios.c:2614
    xor ah, ah                                ; 30 e4                       ; 0xc3855
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3857
    jnbe near 03b21h                          ; 0f 87 c3 02                 ; 0xc385a
    push CS                                   ; 0e                          ; 0xc385e
    pop ES                                    ; 07                          ; 0xc385f
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc3860
    mov di, 03666h                            ; bf 66 36                    ; 0xc3863
    repne scasb                               ; f2 ae                       ; 0xc3866
    sal cx, 1                                 ; d1 e1                       ; 0xc3868
    mov di, cx                                ; 89 cf                       ; 0xc386a
    mov ax, word [cs:di+03675h]               ; 2e 8b 85 75 36              ; 0xc386c
    jmp ax                                    ; ff e0                       ; 0xc3871
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3873 vgabios.c:2618
    shr ax, 008h                              ; c1 e8 08                    ; 0xc3876
    xor ah, ah                                ; 30 e4                       ; 0xc3879
    push ax                                   ; 50                          ; 0xc387b
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc387c
    push ax                                   ; 50                          ; 0xc3880
    push word [bp+00eh]                       ; ff 76 0e                    ; 0xc3881
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc3884
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3888
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc388b
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc388e
    jmp short 038a9h                          ; eb 16                       ; 0xc3891
    push strict byte 0000eh                   ; 6a 0e                       ; 0xc3893 vgabios.c:2622
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc3895
    push ax                                   ; 50                          ; 0xc3899
    push strict byte 00000h                   ; 6a 00                       ; 0xc389a
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc389c
    mov cx, 00100h                            ; b9 00 01                    ; 0xc38a0
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc38a3
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc38a6
    call 02c0ah                               ; e8 5e f3                    ; 0xc38a9
    jmp near 03b21h                           ; e9 72 02                    ; 0xc38ac
    push strict byte 00008h                   ; 6a 08                       ; 0xc38af vgabios.c:2626
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc38b1
    push ax                                   ; 50                          ; 0xc38b5
    push strict byte 00000h                   ; 6a 00                       ; 0xc38b6
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc38b8
    mov cx, 00100h                            ; b9 00 01                    ; 0xc38bc
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc38bf
    jmp short 038a6h                          ; eb e2                       ; 0xc38c2
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc38c4 vgabios.c:2629
    xor ah, ah                                ; 30 e4                       ; 0xc38c7
    call 02b73h                               ; e8 a7 f2                    ; 0xc38c9
    jmp near 03b21h                           ; e9 52 02                    ; 0xc38cc vgabios.c:2630
    push strict byte 00010h                   ; 6a 10                       ; 0xc38cf vgabios.c:2633
    movzx ax, byte [bp+00ch]                  ; 0f b6 46 0c                 ; 0xc38d1
    push ax                                   ; 50                          ; 0xc38d5
    push strict byte 00000h                   ; 6a 00                       ; 0xc38d6
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc38d8
    mov cx, 00100h                            ; b9 00 01                    ; 0xc38dc
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc38df
    jmp short 038a6h                          ; eb c2                       ; 0xc38e2
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc38e4 vgabios.c:2636
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc38e7
    call 02c86h                               ; e8 99 f3                    ; 0xc38ea
    jmp near 03b21h                           ; e9 31 02                    ; 0xc38ed vgabios.c:2637
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc38f0 vgabios.c:2639
    xor ah, ah                                ; 30 e4                       ; 0xc38f3
    push ax                                   ; 50                          ; 0xc38f5
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc38f6
    movzx cx, al                              ; 0f b6 c8                    ; 0xc38f9
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc38fc
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc38ff
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc3902
    call 02ce5h                               ; e8 dd f3                    ; 0xc3905
    jmp near 03b21h                           ; e9 16 02                    ; 0xc3908 vgabios.c:2640
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc390b vgabios.c:2642
    movzx dx, al                              ; 0f b6 d0                    ; 0xc390e
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3911
    xor ah, ah                                ; 30 e4                       ; 0xc3914
    call 02d01h                               ; e8 e8 f3                    ; 0xc3916
    jmp near 03b21h                           ; e9 05 02                    ; 0xc3919 vgabios.c:2643
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc391c vgabios.c:2645
    movzx dx, al                              ; 0f b6 d0                    ; 0xc391f
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3922
    xor ah, ah                                ; 30 e4                       ; 0xc3925
    call 02d1fh                               ; e8 f5 f3                    ; 0xc3927
    jmp near 03b21h                           ; e9 f4 01                    ; 0xc392a vgabios.c:2646
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc392d vgabios.c:2648
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3930
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3933
    xor ah, ah                                ; 30 e4                       ; 0xc3936
    call 02d3dh                               ; e8 02 f4                    ; 0xc3938
    jmp near 03b21h                           ; e9 e3 01                    ; 0xc393b vgabios.c:2649
    lea ax, [bp+00eh]                         ; 8d 46 0e                    ; 0xc393e vgabios.c:2651
    push ax                                   ; 50                          ; 0xc3941
    lea cx, [bp+010h]                         ; 8d 4e 10                    ; 0xc3942
    lea bx, [bp+008h]                         ; 8d 5e 08                    ; 0xc3945
    lea dx, [bp+016h]                         ; 8d 56 16                    ; 0xc3948
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc394b
    shr ax, 008h                              ; c1 e8 08                    ; 0xc394e
    call 00ee7h                               ; e8 93 d5                    ; 0xc3951
    jmp near 03b21h                           ; e9 ca 01                    ; 0xc3954 vgabios.c:2659
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3957 vgabios.c:2661
    xor ah, ah                                ; 30 e4                       ; 0xc395a
    cmp ax, strict word 00034h                ; 3d 34 00                    ; 0xc395c
    jc short 03970h                           ; 72 0f                       ; 0xc395f
    jbe short 039a3h                          ; 76 40                       ; 0xc3961
    cmp ax, strict word 00036h                ; 3d 36 00                    ; 0xc3963
    je short 039dah                           ; 74 72                       ; 0xc3966
    cmp ax, strict word 00035h                ; 3d 35 00                    ; 0xc3968
    je short 039cbh                           ; 74 5e                       ; 0xc396b
    jmp near 03b21h                           ; e9 b1 01                    ; 0xc396d
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3970
    je short 03982h                           ; 74 0d                       ; 0xc3973
    cmp ax, strict word 00020h                ; 3d 20 00                    ; 0xc3975
    jne near 03b21h                           ; 0f 85 a5 01                 ; 0xc3978
    call 02d5bh                               ; e8 dc f3                    ; 0xc397c vgabios.c:2664
    jmp near 03b21h                           ; e9 9f 01                    ; 0xc397f vgabios.c:2665
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3982 vgabios.c:2667
    xor ah, ah                                ; 30 e4                       ; 0xc3985
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3987
    jnbe near 03b21h                          ; 0f 87 93 01                 ; 0xc398a
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc398e vgabios.c:2668
    xor ah, ah                                ; 30 e4                       ; 0xc3991
    call 02d60h                               ; e8 ca f3                    ; 0xc3993
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3996 vgabios.c:2669
    xor al, al                                ; 30 c0                       ; 0xc3999
    or AL, strict byte 012h                   ; 0c 12                       ; 0xc399b
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc399d
    jmp near 03b21h                           ; e9 7e 01                    ; 0xc39a0 vgabios.c:2671
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc39a3 vgabios.c:2673
    xor ah, ah                                ; 30 e4                       ; 0xc39a6
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc39a8
    jnc short 039c5h                          ; 73 18                       ; 0xc39ab
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc39ad vgabios.c:45
    mov si, 00087h                            ; be 87 00                    ; 0xc39b0
    mov es, ax                                ; 8e c0                       ; 0xc39b3 vgabios.c:47
    mov ah, byte [es:si]                      ; 26 8a 24                    ; 0xc39b5
    and ah, 0feh                              ; 80 e4 fe                    ; 0xc39b8 vgabios.c:48
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc39bb
    or al, ah                                 ; 08 e0                       ; 0xc39be
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc39c0 vgabios.c:52
    jmp short 03996h                          ; eb d1                       ; 0xc39c3
    mov byte [bp+012h], ah                    ; 88 66 12                    ; 0xc39c5 vgabios.c:2679
    jmp near 03b21h                           ; e9 56 01                    ; 0xc39c8 vgabios.c:2680
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc39cb vgabios.c:2682
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc39cf
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc39d2
    call 02d92h                               ; e8 ba f3                    ; 0xc39d5
    jmp short 03996h                          ; eb bc                       ; 0xc39d8
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc39da vgabios.c:2686
    xor ah, ah                                ; 30 e4                       ; 0xc39dd
    call 02d97h                               ; e8 b5 f3                    ; 0xc39df
    jmp short 03996h                          ; eb b2                       ; 0xc39e2
    push word [bp+008h]                       ; ff 76 08                    ; 0xc39e4 vgabios.c:2696
    push word [bp+016h]                       ; ff 76 16                    ; 0xc39e7
    movzx ax, byte [bp+00eh]                  ; 0f b6 46 0e                 ; 0xc39ea
    push ax                                   ; 50                          ; 0xc39ee
    mov ax, word [bp+00eh]                    ; 8b 46 0e                    ; 0xc39ef
    shr ax, 008h                              ; c1 e8 08                    ; 0xc39f2
    xor ah, ah                                ; 30 e4                       ; 0xc39f5
    push ax                                   ; 50                          ; 0xc39f7
    movzx bx, byte [bp+00ch]                  ; 0f b6 5e 0c                 ; 0xc39f8
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc39fc
    shr dx, 008h                              ; c1 ea 08                    ; 0xc39ff
    xor dh, dh                                ; 30 f6                       ; 0xc3a02
    movzx ax, byte [bp+012h]                  ; 0f b6 46 12                 ; 0xc3a04
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a08
    call 02d9ch                               ; e8 8e f3                    ; 0xc3a0b
    jmp near 03b21h                           ; e9 10 01                    ; 0xc3a0e vgabios.c:2697
    mov bx, si                                ; 89 f3                       ; 0xc3a11 vgabios.c:2699
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a13
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3a16
    call 02e32h                               ; e8 16 f4                    ; 0xc3a19
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a1c vgabios.c:2700
    xor al, al                                ; 30 c0                       ; 0xc3a1f
    or AL, strict byte 01bh                   ; 0c 1b                       ; 0xc3a21
    jmp near 0399dh                           ; e9 77 ff                    ; 0xc3a23
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a26 vgabios.c:2703
    xor ah, ah                                ; 30 e4                       ; 0xc3a29
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3a2b
    je short 03a52h                           ; 74 22                       ; 0xc3a2e
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc3a30
    je short 03a44h                           ; 74 0f                       ; 0xc3a33
    test ax, ax                               ; 85 c0                       ; 0xc3a35
    jne short 03a5eh                          ; 75 25                       ; 0xc3a37
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3a39 vgabios.c:2706
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3a3c
    call 03047h                               ; e8 05 f6                    ; 0xc3a3f
    jmp short 03a5eh                          ; eb 1a                       ; 0xc3a42 vgabios.c:2707
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3a44 vgabios.c:2709
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a47
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3a4a
    call 0305fh                               ; e8 0f f6                    ; 0xc3a4d
    jmp short 03a5eh                          ; eb 0c                       ; 0xc3a50 vgabios.c:2710
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3a52 vgabios.c:2712
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a55
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3a58
    call 03335h                               ; e8 d7 f8                    ; 0xc3a5b
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a5e vgabios.c:2719
    xor al, al                                ; 30 c0                       ; 0xc3a61
    or AL, strict byte 01ch                   ; 0c 1c                       ; 0xc3a63
    jmp near 0399dh                           ; e9 35 ff                    ; 0xc3a65
    call 007bfh                               ; e8 54 cd                    ; 0xc3a68 vgabios.c:2724
    test ax, ax                               ; 85 c0                       ; 0xc3a6b
    je near 03aech                            ; 0f 84 7b 00                 ; 0xc3a6d
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3a71 vgabios.c:2725
    xor ah, ah                                ; 30 e4                       ; 0xc3a74
    cmp ax, strict word 0000ah                ; 3d 0a 00                    ; 0xc3a76
    jnbe short 03ae5h                         ; 77 6a                       ; 0xc3a79
    push CS                                   ; 0e                          ; 0xc3a7b
    pop ES                                    ; 07                          ; 0xc3a7c
    mov cx, strict word 00008h                ; b9 08 00                    ; 0xc3a7d
    mov di, 03695h                            ; bf 95 36                    ; 0xc3a80
    repne scasb                               ; f2 ae                       ; 0xc3a83
    sal cx, 1                                 ; d1 e1                       ; 0xc3a85
    mov di, cx                                ; 89 cf                       ; 0xc3a87
    mov ax, word [cs:di+0369ch]               ; 2e 8b 85 9c 36              ; 0xc3a89
    jmp ax                                    ; ff e0                       ; 0xc3a8e
    mov bx, si                                ; 89 f3                       ; 0xc3a90 vgabios.c:2728
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3a92
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3a95
    call 03cf2h                               ; e8 57 02                    ; 0xc3a98
    jmp near 03b21h                           ; e9 83 00                    ; 0xc3a9b vgabios.c:2729
    mov cx, si                                ; 89 f1                       ; 0xc3a9e vgabios.c:2731
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3aa0
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3aa3
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3aa6
    call 03e17h                               ; e8 6b 03                    ; 0xc3aa9
    jmp near 03b21h                           ; e9 72 00                    ; 0xc3aac vgabios.c:2732
    mov cx, si                                ; 89 f1                       ; 0xc3aaf vgabios.c:2734
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3ab1
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3ab4
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3ab7
    call 03eb2h                               ; e8 f5 03                    ; 0xc3aba
    jmp short 03b21h                          ; eb 62                       ; 0xc3abd vgabios.c:2735
    lea ax, [bp+00ch]                         ; 8d 46 0c                    ; 0xc3abf vgabios.c:2737
    push ax                                   ; 50                          ; 0xc3ac2
    mov cx, word [bp+016h]                    ; 8b 4e 16                    ; 0xc3ac3
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3ac6
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3ac9
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3acc
    call 04079h                               ; e8 a7 05                    ; 0xc3acf
    jmp short 03b21h                          ; eb 4d                       ; 0xc3ad2 vgabios.c:2738
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3ad4 vgabios.c:2740
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3ad7
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3ada
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3add
    call 04105h                               ; e8 22 06                    ; 0xc3ae0
    jmp short 03b21h                          ; eb 3c                       ; 0xc3ae3 vgabios.c:2741
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3ae5 vgabios.c:2763
    jmp short 03b21h                          ; eb 35                       ; 0xc3aea vgabios.c:2766
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3aec vgabios.c:2768
    jmp short 03b21h                          ; eb 2e                       ; 0xc3af1 vgabios.c:2770
    call 007bfh                               ; e8 c9 cc                    ; 0xc3af3 vgabios.c:2772
    test ax, ax                               ; 85 c0                       ; 0xc3af6
    je short 03b1ch                           ; 74 22                       ; 0xc3af8
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3afa vgabios.c:2773
    xor ah, ah                                ; 30 e4                       ; 0xc3afd
    cmp ax, strict word 00042h                ; 3d 42 00                    ; 0xc3aff
    jne short 03b15h                          ; 75 11                       ; 0xc3b02
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3b04 vgabios.c:2776
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3b07
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3b0a
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3b0d
    call 041d4h                               ; e8 c1 06                    ; 0xc3b10
    jmp short 03b21h                          ; eb 0c                       ; 0xc3b13 vgabios.c:2777
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3b15 vgabios.c:2779
    jmp short 03b21h                          ; eb 05                       ; 0xc3b1a vgabios.c:2782
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3b1c vgabios.c:2784
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3b21 vgabios.c:2794
    pop di                                    ; 5f                          ; 0xc3b24
    pop si                                    ; 5e                          ; 0xc3b25
    pop bp                                    ; 5d                          ; 0xc3b26
    retn                                      ; c3                          ; 0xc3b27
  ; disGetNextSymbol 0xc3b28 LB 0x791 -> off=0x0 cb=000000000000001f uValue=00000000000c3b28 'dispi_set_xres'
dispi_set_xres:                              ; 0xc3b28 LB 0x1f
    push bp                                   ; 55                          ; 0xc3b28 vbe.c:100
    mov bp, sp                                ; 89 e5                       ; 0xc3b29
    push bx                                   ; 53                          ; 0xc3b2b
    push dx                                   ; 52                          ; 0xc3b2c
    mov bx, ax                                ; 89 c3                       ; 0xc3b2d
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3b2f vbe.c:105
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b32
    call 00570h                               ; e8 38 ca                    ; 0xc3b35
    mov ax, bx                                ; 89 d8                       ; 0xc3b38 vbe.c:106
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b3a
    call 00570h                               ; e8 30 ca                    ; 0xc3b3d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3b40 vbe.c:107
    pop dx                                    ; 5a                          ; 0xc3b43
    pop bx                                    ; 5b                          ; 0xc3b44
    pop bp                                    ; 5d                          ; 0xc3b45
    retn                                      ; c3                          ; 0xc3b46
  ; disGetNextSymbol 0xc3b47 LB 0x772 -> off=0x0 cb=000000000000001f uValue=00000000000c3b47 'dispi_set_yres'
dispi_set_yres:                              ; 0xc3b47 LB 0x1f
    push bp                                   ; 55                          ; 0xc3b47 vbe.c:109
    mov bp, sp                                ; 89 e5                       ; 0xc3b48
    push bx                                   ; 53                          ; 0xc3b4a
    push dx                                   ; 52                          ; 0xc3b4b
    mov bx, ax                                ; 89 c3                       ; 0xc3b4c
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3b4e vbe.c:114
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b51
    call 00570h                               ; e8 19 ca                    ; 0xc3b54
    mov ax, bx                                ; 89 d8                       ; 0xc3b57 vbe.c:115
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b59
    call 00570h                               ; e8 11 ca                    ; 0xc3b5c
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3b5f vbe.c:116
    pop dx                                    ; 5a                          ; 0xc3b62
    pop bx                                    ; 5b                          ; 0xc3b63
    pop bp                                    ; 5d                          ; 0xc3b64
    retn                                      ; c3                          ; 0xc3b65
  ; disGetNextSymbol 0xc3b66 LB 0x753 -> off=0x0 cb=0000000000000019 uValue=00000000000c3b66 'dispi_get_yres'
dispi_get_yres:                              ; 0xc3b66 LB 0x19
    push bp                                   ; 55                          ; 0xc3b66 vbe.c:118
    mov bp, sp                                ; 89 e5                       ; 0xc3b67
    push dx                                   ; 52                          ; 0xc3b69
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3b6a vbe.c:120
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b6d
    call 00570h                               ; e8 fd c9                    ; 0xc3b70
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b73 vbe.c:121
    call 00577h                               ; e8 fe c9                    ; 0xc3b76
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3b79 vbe.c:122
    pop dx                                    ; 5a                          ; 0xc3b7c
    pop bp                                    ; 5d                          ; 0xc3b7d
    retn                                      ; c3                          ; 0xc3b7e
  ; disGetNextSymbol 0xc3b7f LB 0x73a -> off=0x0 cb=000000000000001f uValue=00000000000c3b7f 'dispi_set_bpp'
dispi_set_bpp:                               ; 0xc3b7f LB 0x1f
    push bp                                   ; 55                          ; 0xc3b7f vbe.c:124
    mov bp, sp                                ; 89 e5                       ; 0xc3b80
    push bx                                   ; 53                          ; 0xc3b82
    push dx                                   ; 52                          ; 0xc3b83
    mov bx, ax                                ; 89 c3                       ; 0xc3b84
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3b86 vbe.c:129
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3b89
    call 00570h                               ; e8 e1 c9                    ; 0xc3b8c
    mov ax, bx                                ; 89 d8                       ; 0xc3b8f vbe.c:130
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3b91
    call 00570h                               ; e8 d9 c9                    ; 0xc3b94
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3b97 vbe.c:131
    pop dx                                    ; 5a                          ; 0xc3b9a
    pop bx                                    ; 5b                          ; 0xc3b9b
    pop bp                                    ; 5d                          ; 0xc3b9c
    retn                                      ; c3                          ; 0xc3b9d
  ; disGetNextSymbol 0xc3b9e LB 0x71b -> off=0x0 cb=0000000000000019 uValue=00000000000c3b9e 'dispi_get_bpp'
dispi_get_bpp:                               ; 0xc3b9e LB 0x19
    push bp                                   ; 55                          ; 0xc3b9e vbe.c:133
    mov bp, sp                                ; 89 e5                       ; 0xc3b9f
    push dx                                   ; 52                          ; 0xc3ba1
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3ba2 vbe.c:135
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3ba5
    call 00570h                               ; e8 c5 c9                    ; 0xc3ba8
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3bab vbe.c:136
    call 00577h                               ; e8 c6 c9                    ; 0xc3bae
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3bb1 vbe.c:137
    pop dx                                    ; 5a                          ; 0xc3bb4
    pop bp                                    ; 5d                          ; 0xc3bb5
    retn                                      ; c3                          ; 0xc3bb6
  ; disGetNextSymbol 0xc3bb7 LB 0x702 -> off=0x0 cb=000000000000001f uValue=00000000000c3bb7 'dispi_set_virt_width'
dispi_set_virt_width:                        ; 0xc3bb7 LB 0x1f
    push bp                                   ; 55                          ; 0xc3bb7 vbe.c:139
    mov bp, sp                                ; 89 e5                       ; 0xc3bb8
    push bx                                   ; 53                          ; 0xc3bba
    push dx                                   ; 52                          ; 0xc3bbb
    mov bx, ax                                ; 89 c3                       ; 0xc3bbc
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3bbe vbe.c:144
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3bc1
    call 00570h                               ; e8 a9 c9                    ; 0xc3bc4
    mov ax, bx                                ; 89 d8                       ; 0xc3bc7 vbe.c:145
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3bc9
    call 00570h                               ; e8 a1 c9                    ; 0xc3bcc
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3bcf vbe.c:146
    pop dx                                    ; 5a                          ; 0xc3bd2
    pop bx                                    ; 5b                          ; 0xc3bd3
    pop bp                                    ; 5d                          ; 0xc3bd4
    retn                                      ; c3                          ; 0xc3bd5
  ; disGetNextSymbol 0xc3bd6 LB 0x6e3 -> off=0x0 cb=0000000000000019 uValue=00000000000c3bd6 'dispi_get_virt_width'
dispi_get_virt_width:                        ; 0xc3bd6 LB 0x19
    push bp                                   ; 55                          ; 0xc3bd6 vbe.c:148
    mov bp, sp                                ; 89 e5                       ; 0xc3bd7
    push dx                                   ; 52                          ; 0xc3bd9
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3bda vbe.c:150
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3bdd
    call 00570h                               ; e8 8d c9                    ; 0xc3be0
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3be3 vbe.c:151
    call 00577h                               ; e8 8e c9                    ; 0xc3be6
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3be9 vbe.c:152
    pop dx                                    ; 5a                          ; 0xc3bec
    pop bp                                    ; 5d                          ; 0xc3bed
    retn                                      ; c3                          ; 0xc3bee
  ; disGetNextSymbol 0xc3bef LB 0x6ca -> off=0x0 cb=0000000000000019 uValue=00000000000c3bef 'dispi_get_virt_height'
dispi_get_virt_height:                       ; 0xc3bef LB 0x19
    push bp                                   ; 55                          ; 0xc3bef vbe.c:154
    mov bp, sp                                ; 89 e5                       ; 0xc3bf0
    push dx                                   ; 52                          ; 0xc3bf2
    mov ax, strict word 00007h                ; b8 07 00                    ; 0xc3bf3 vbe.c:156
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3bf6
    call 00570h                               ; e8 74 c9                    ; 0xc3bf9
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3bfc vbe.c:157
    call 00577h                               ; e8 75 c9                    ; 0xc3bff
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c02 vbe.c:158
    pop dx                                    ; 5a                          ; 0xc3c05
    pop bp                                    ; 5d                          ; 0xc3c06
    retn                                      ; c3                          ; 0xc3c07
  ; disGetNextSymbol 0xc3c08 LB 0x6b1 -> off=0x0 cb=0000000000000012 uValue=00000000000c3c08 'in_word'
in_word:                                     ; 0xc3c08 LB 0x12
    push bp                                   ; 55                          ; 0xc3c08 vbe.c:160
    mov bp, sp                                ; 89 e5                       ; 0xc3c09
    push bx                                   ; 53                          ; 0xc3c0b
    mov bx, ax                                ; 89 c3                       ; 0xc3c0c
    mov ax, dx                                ; 89 d0                       ; 0xc3c0e
    mov dx, bx                                ; 89 da                       ; 0xc3c10 vbe.c:162
    out DX, ax                                ; ef                          ; 0xc3c12
    in ax, DX                                 ; ed                          ; 0xc3c13 vbe.c:163
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c14 vbe.c:164
    pop bx                                    ; 5b                          ; 0xc3c17
    pop bp                                    ; 5d                          ; 0xc3c18
    retn                                      ; c3                          ; 0xc3c19
  ; disGetNextSymbol 0xc3c1a LB 0x69f -> off=0x0 cb=0000000000000014 uValue=00000000000c3c1a 'in_byte'
in_byte:                                     ; 0xc3c1a LB 0x14
    push bp                                   ; 55                          ; 0xc3c1a vbe.c:166
    mov bp, sp                                ; 89 e5                       ; 0xc3c1b
    push bx                                   ; 53                          ; 0xc3c1d
    mov bx, ax                                ; 89 c3                       ; 0xc3c1e
    mov ax, dx                                ; 89 d0                       ; 0xc3c20
    mov dx, bx                                ; 89 da                       ; 0xc3c22 vbe.c:168
    out DX, ax                                ; ef                          ; 0xc3c24
    in AL, DX                                 ; ec                          ; 0xc3c25 vbe.c:169
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3c26
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c28 vbe.c:170
    pop bx                                    ; 5b                          ; 0xc3c2b
    pop bp                                    ; 5d                          ; 0xc3c2c
    retn                                      ; c3                          ; 0xc3c2d
  ; disGetNextSymbol 0xc3c2e LB 0x68b -> off=0x0 cb=0000000000000014 uValue=00000000000c3c2e 'dispi_get_id'
dispi_get_id:                                ; 0xc3c2e LB 0x14
    push bp                                   ; 55                          ; 0xc3c2e vbe.c:173
    mov bp, sp                                ; 89 e5                       ; 0xc3c2f
    push dx                                   ; 52                          ; 0xc3c31
    xor ax, ax                                ; 31 c0                       ; 0xc3c32 vbe.c:175
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3c34
    out DX, ax                                ; ef                          ; 0xc3c37
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3c38 vbe.c:176
    in ax, DX                                 ; ed                          ; 0xc3c3b
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c3c vbe.c:177
    pop dx                                    ; 5a                          ; 0xc3c3f
    pop bp                                    ; 5d                          ; 0xc3c40
    retn                                      ; c3                          ; 0xc3c41
  ; disGetNextSymbol 0xc3c42 LB 0x677 -> off=0x0 cb=000000000000001a uValue=00000000000c3c42 'dispi_set_id'
dispi_set_id:                                ; 0xc3c42 LB 0x1a
    push bp                                   ; 55                          ; 0xc3c42 vbe.c:179
    mov bp, sp                                ; 89 e5                       ; 0xc3c43
    push bx                                   ; 53                          ; 0xc3c45
    push dx                                   ; 52                          ; 0xc3c46
    mov bx, ax                                ; 89 c3                       ; 0xc3c47
    xor ax, ax                                ; 31 c0                       ; 0xc3c49 vbe.c:181
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3c4b
    out DX, ax                                ; ef                          ; 0xc3c4e
    mov ax, bx                                ; 89 d8                       ; 0xc3c4f vbe.c:182
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3c51
    out DX, ax                                ; ef                          ; 0xc3c54
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3c55 vbe.c:183
    pop dx                                    ; 5a                          ; 0xc3c58
    pop bx                                    ; 5b                          ; 0xc3c59
    pop bp                                    ; 5d                          ; 0xc3c5a
    retn                                      ; c3                          ; 0xc3c5b
  ; disGetNextSymbol 0xc3c5c LB 0x65d -> off=0x0 cb=000000000000002a uValue=00000000000c3c5c 'vbe_init'
vbe_init:                                    ; 0xc3c5c LB 0x2a
    push bp                                   ; 55                          ; 0xc3c5c vbe.c:188
    mov bp, sp                                ; 89 e5                       ; 0xc3c5d
    push bx                                   ; 53                          ; 0xc3c5f
    mov ax, 0b0c0h                            ; b8 c0 b0                    ; 0xc3c60 vbe.c:190
    call 03c42h                               ; e8 dc ff                    ; 0xc3c63
    call 03c2eh                               ; e8 c5 ff                    ; 0xc3c66 vbe.c:191
    cmp ax, 0b0c0h                            ; 3d c0 b0                    ; 0xc3c69
    jne short 03c80h                          ; 75 12                       ; 0xc3c6c
    mov bx, 000b9h                            ; bb b9 00                    ; 0xc3c6e vbe.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3c71
    mov es, ax                                ; 8e c0                       ; 0xc3c74
    mov byte [es:bx], 001h                    ; 26 c6 07 01                 ; 0xc3c76
    mov ax, 0b0c4h                            ; b8 c4 b0                    ; 0xc3c7a vbe.c:194
    call 03c42h                               ; e8 c2 ff                    ; 0xc3c7d
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3c80 vbe.c:199
    pop bx                                    ; 5b                          ; 0xc3c83
    pop bp                                    ; 5d                          ; 0xc3c84
    retn                                      ; c3                          ; 0xc3c85
  ; disGetNextSymbol 0xc3c86 LB 0x633 -> off=0x0 cb=000000000000006c uValue=00000000000c3c86 'mode_info_find_mode'
mode_info_find_mode:                         ; 0xc3c86 LB 0x6c
    push bp                                   ; 55                          ; 0xc3c86 vbe.c:202
    mov bp, sp                                ; 89 e5                       ; 0xc3c87
    push bx                                   ; 53                          ; 0xc3c89
    push cx                                   ; 51                          ; 0xc3c8a
    push si                                   ; 56                          ; 0xc3c8b
    push di                                   ; 57                          ; 0xc3c8c
    mov di, ax                                ; 89 c7                       ; 0xc3c8d
    mov si, dx                                ; 89 d6                       ; 0xc3c8f
    xor dx, dx                                ; 31 d2                       ; 0xc3c91 vbe.c:208
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3c93
    call 03c08h                               ; e8 6f ff                    ; 0xc3c96
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3c99 vbe.c:209
    jne short 03ce7h                          ; 75 49                       ; 0xc3c9c
    test si, si                               ; 85 f6                       ; 0xc3c9e vbe.c:213
    je short 03cb5h                           ; 74 13                       ; 0xc3ca0
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc3ca2 vbe.c:220
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3ca5
    call 00570h                               ; e8 c5 c8                    ; 0xc3ca8
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3cab vbe.c:221
    call 00577h                               ; e8 c6 c8                    ; 0xc3cae
    test ax, ax                               ; 85 c0                       ; 0xc3cb1 vbe.c:222
    je short 03ce9h                           ; 74 34                       ; 0xc3cb3
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc3cb5 vbe.c:226
    mov dx, bx                                ; 89 da                       ; 0xc3cb8 vbe.c:232
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3cba
    call 03c08h                               ; e8 48 ff                    ; 0xc3cbd
    mov cx, ax                                ; 89 c1                       ; 0xc3cc0
    cmp cx, strict byte 0ffffh                ; 83 f9 ff                    ; 0xc3cc2 vbe.c:233
    je short 03ce7h                           ; 74 20                       ; 0xc3cc5
    lea dx, [bx+002h]                         ; 8d 57 02                    ; 0xc3cc7 vbe.c:235
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3cca
    call 03c08h                               ; e8 38 ff                    ; 0xc3ccd
    lea dx, [bx+044h]                         ; 8d 57 44                    ; 0xc3cd0
    cmp cx, di                                ; 39 f9                       ; 0xc3cd3 vbe.c:237
    jne short 03ce3h                          ; 75 0c                       ; 0xc3cd5
    test si, si                               ; 85 f6                       ; 0xc3cd7 vbe.c:239
    jne short 03cdfh                          ; 75 04                       ; 0xc3cd9
    mov ax, bx                                ; 89 d8                       ; 0xc3cdb vbe.c:240
    jmp short 03ce9h                          ; eb 0a                       ; 0xc3cdd
    test AL, strict byte 080h                 ; a8 80                       ; 0xc3cdf vbe.c:241
    jne short 03cdbh                          ; 75 f8                       ; 0xc3ce1
    mov bx, dx                                ; 89 d3                       ; 0xc3ce3 vbe.c:244
    jmp short 03cbah                          ; eb d3                       ; 0xc3ce5 vbe.c:249
    xor ax, ax                                ; 31 c0                       ; 0xc3ce7 vbe.c:252
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc3ce9 vbe.c:253
    pop di                                    ; 5f                          ; 0xc3cec
    pop si                                    ; 5e                          ; 0xc3ced
    pop cx                                    ; 59                          ; 0xc3cee
    pop bx                                    ; 5b                          ; 0xc3cef
    pop bp                                    ; 5d                          ; 0xc3cf0
    retn                                      ; c3                          ; 0xc3cf1
  ; disGetNextSymbol 0xc3cf2 LB 0x5c7 -> off=0x0 cb=0000000000000125 uValue=00000000000c3cf2 'vbe_biosfn_return_controller_information'
vbe_biosfn_return_controller_information: ; 0xc3cf2 LB 0x125
    push bp                                   ; 55                          ; 0xc3cf2 vbe.c:284
    mov bp, sp                                ; 89 e5                       ; 0xc3cf3
    push cx                                   ; 51                          ; 0xc3cf5
    push si                                   ; 56                          ; 0xc3cf6
    push di                                   ; 57                          ; 0xc3cf7
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc3cf8
    mov si, ax                                ; 89 c6                       ; 0xc3cfb
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3cfd
    mov di, bx                                ; 89 df                       ; 0xc3d00
    mov word [bp-00ch], strict word 00022h    ; c7 46 f4 22 00              ; 0xc3d02 vbe.c:289
    call 005b7h                               ; e8 ad c8                    ; 0xc3d07 vbe.c:292
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc3d0a
    mov bx, di                                ; 89 fb                       ; 0xc3d0d vbe.c:295
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3d0f
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3d12
    xor dx, dx                                ; 31 d2                       ; 0xc3d15 vbe.c:298
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3d17
    call 03c08h                               ; e8 eb fe                    ; 0xc3d1a
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3d1d vbe.c:299
    je short 03d2ch                           ; 74 0a                       ; 0xc3d20
    push SS                                   ; 16                          ; 0xc3d22 vbe.c:301
    pop ES                                    ; 07                          ; 0xc3d23
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc3d24
    jmp near 03e0fh                           ; e9 e3 00                    ; 0xc3d29 vbe.c:305
    mov cx, strict word 00004h                ; b9 04 00                    ; 0xc3d2c vbe.c:307
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3d2f vbe.c:314
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3d34 vbe.c:322
    cmp word [es:bx+002h], 03245h             ; 26 81 7f 02 45 32           ; 0xc3d37
    jne short 03d46h                          ; 75 07                       ; 0xc3d3d
    cmp word [es:bx], 04256h                  ; 26 81 3f 56 42              ; 0xc3d3f
    je short 03d55h                           ; 74 0f                       ; 0xc3d44
    cmp word [es:bx+002h], 04153h             ; 26 81 7f 02 53 41           ; 0xc3d46
    jne short 03d5ah                          ; 75 0c                       ; 0xc3d4c
    cmp word [es:bx], 04556h                  ; 26 81 3f 56 45              ; 0xc3d4e
    jne short 03d5ah                          ; 75 05                       ; 0xc3d53
    mov word [bp-00eh], strict word 00001h    ; c7 46 f2 01 00              ; 0xc3d55 vbe.c:324
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3d5a vbe.c:332
    db  066h, 026h, 0c7h, 007h, 056h, 045h, 053h, 041h
    ; mov dword [es:bx], strict dword 041534556h ; 66 26 c7 07 56 45 53 41  ; 0xc3d5d
    mov word [es:bx+004h], 00200h             ; 26 c7 47 04 00 02           ; 0xc3d65 vbe.c:338
    mov word [es:bx+006h], 07e04h             ; 26 c7 47 06 04 7e           ; 0xc3d6b vbe.c:341
    mov [es:bx+008h], ds                      ; 26 8c 5f 08                 ; 0xc3d71
    db  066h, 026h, 0c7h, 047h, 00ah, 001h, 000h, 000h, 000h
    ; mov dword [es:bx+00ah], strict dword 000000001h ; 66 26 c7 47 0a 01 00 00 00; 0xc3d75 vbe.c:344
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3d7e vbe.c:350
    mov word [es:bx+010h], ax                 ; 26 89 47 10                 ; 0xc3d81
    lea ax, [di+022h]                         ; 8d 45 22                    ; 0xc3d85 vbe.c:351
    mov word [es:bx+00eh], ax                 ; 26 89 47 0e                 ; 0xc3d88
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc3d8c vbe.c:354
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3d8f
    call 03c08h                               ; e8 73 fe                    ; 0xc3d92
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3d95
    mov word [es:bx+012h], ax                 ; 26 89 47 12                 ; 0xc3d98
    cmp word [bp-00eh], strict byte 00000h    ; 83 7e f2 00                 ; 0xc3d9c vbe.c:356
    je short 03dc6h                           ; 74 24                       ; 0xc3da0
    mov word [es:bx+014h], strict word 00003h ; 26 c7 47 14 03 00           ; 0xc3da2 vbe.c:359
    mov word [es:bx+016h], 07e19h             ; 26 c7 47 16 19 7e           ; 0xc3da8 vbe.c:360
    mov [es:bx+018h], ds                      ; 26 8c 5f 18                 ; 0xc3dae
    mov word [es:bx+01ah], 07e36h             ; 26 c7 47 1a 36 7e           ; 0xc3db2 vbe.c:361
    mov [es:bx+01ch], ds                      ; 26 8c 5f 1c                 ; 0xc3db8
    mov word [es:bx+01eh], 07e54h             ; 26 c7 47 1e 54 7e           ; 0xc3dbc vbe.c:362
    mov [es:bx+020h], ds                      ; 26 8c 5f 20                 ; 0xc3dc2
    mov dx, cx                                ; 89 ca                       ; 0xc3dc6 vbe.c:369
    add dx, strict byte 0001bh                ; 83 c2 1b                    ; 0xc3dc8
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3dcb
    call 03c1ah                               ; e8 49 fe                    ; 0xc3dce
    xor ah, ah                                ; 30 e4                       ; 0xc3dd1 vbe.c:370
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc3dd3
    jnbe short 03defh                         ; 77 17                       ; 0xc3dd6
    mov dx, cx                                ; 89 ca                       ; 0xc3dd8 vbe.c:372
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3dda
    call 03c08h                               ; e8 28 fe                    ; 0xc3ddd
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc3de0 vbe.c:376
    add bx, di                                ; 01 fb                       ; 0xc3de3
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc3de5 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3de8
    add word [bp-00ch], strict byte 00002h    ; 83 46 f4 02                 ; 0xc3deb vbe.c:378
    add cx, strict byte 00044h                ; 83 c1 44                    ; 0xc3def vbe.c:380
    mov dx, cx                                ; 89 ca                       ; 0xc3df2 vbe.c:381
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3df4
    call 03c08h                               ; e8 0e fe                    ; 0xc3df7
    cmp ax, strict word 0ffffh                ; 3d ff ff                    ; 0xc3dfa vbe.c:382
    jne short 03dc6h                          ; 75 c7                       ; 0xc3dfd
    add di, word [bp-00ch]                    ; 03 7e f4                    ; 0xc3dff vbe.c:385
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc3e02 vbe.c:62
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc3e05
    push SS                                   ; 16                          ; 0xc3e08 vbe.c:386
    pop ES                                    ; 07                          ; 0xc3e09
    mov word [es:si], strict word 0004fh      ; 26 c7 04 4f 00              ; 0xc3e0a
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3e0f vbe.c:387
    pop di                                    ; 5f                          ; 0xc3e12
    pop si                                    ; 5e                          ; 0xc3e13
    pop cx                                    ; 59                          ; 0xc3e14
    pop bp                                    ; 5d                          ; 0xc3e15
    retn                                      ; c3                          ; 0xc3e16
  ; disGetNextSymbol 0xc3e17 LB 0x4a2 -> off=0x0 cb=000000000000009b uValue=00000000000c3e17 'vbe_biosfn_return_mode_information'
vbe_biosfn_return_mode_information:          ; 0xc3e17 LB 0x9b
    push bp                                   ; 55                          ; 0xc3e17 vbe.c:399
    mov bp, sp                                ; 89 e5                       ; 0xc3e18
    push si                                   ; 56                          ; 0xc3e1a
    push di                                   ; 57                          ; 0xc3e1b
    push ax                                   ; 50                          ; 0xc3e1c
    push ax                                   ; 50                          ; 0xc3e1d
    mov ax, dx                                ; 89 d0                       ; 0xc3e1e
    mov si, bx                                ; 89 de                       ; 0xc3e20
    mov bx, cx                                ; 89 cb                       ; 0xc3e22
    test dh, 040h                             ; f6 c6 40                    ; 0xc3e24 vbe.c:410
    db  00fh, 095h, 0c2h
    ; setne dl                                  ; 0f 95 c2                  ; 0xc3e27
    xor dh, dh                                ; 30 f6                       ; 0xc3e2a
    and ah, 001h                              ; 80 e4 01                    ; 0xc3e2c vbe.c:411
    call 03c86h                               ; e8 54 fe                    ; 0xc3e2f vbe.c:413
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc3e32
    test ax, ax                               ; 85 c0                       ; 0xc3e35 vbe.c:415
    je short 03ea0h                           ; 74 67                       ; 0xc3e37
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3e39 vbe.c:420
    xor ax, ax                                ; 31 c0                       ; 0xc3e3c
    mov di, bx                                ; 89 df                       ; 0xc3e3e
    mov es, si                                ; 8e c6                       ; 0xc3e40
    jcxz 03e46h                               ; e3 02                       ; 0xc3e42
    rep stosb                                 ; f3 aa                       ; 0xc3e44
    xor cx, cx                                ; 31 c9                       ; 0xc3e46 vbe.c:421
    jmp short 03e4fh                          ; eb 05                       ; 0xc3e48
    cmp cx, strict byte 00042h                ; 83 f9 42                    ; 0xc3e4a
    jnc short 03e68h                          ; 73 19                       ; 0xc3e4d
    mov dx, word [bp-006h]                    ; 8b 56 fa                    ; 0xc3e4f vbe.c:424
    inc dx                                    ; 42                          ; 0xc3e52
    inc dx                                    ; 42                          ; 0xc3e53
    add dx, cx                                ; 01 ca                       ; 0xc3e54
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3e56
    call 03c1ah                               ; e8 be fd                    ; 0xc3e59
    mov di, bx                                ; 89 df                       ; 0xc3e5c vbe.c:425
    add di, cx                                ; 01 cf                       ; 0xc3e5e
    mov es, si                                ; 8e c6                       ; 0xc3e60 vbe.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc3e62
    inc cx                                    ; 41                          ; 0xc3e65 vbe.c:426
    jmp short 03e4ah                          ; eb e2                       ; 0xc3e66
    lea di, [bx+002h]                         ; 8d 7f 02                    ; 0xc3e68 vbe.c:427
    mov es, si                                ; 8e c6                       ; 0xc3e6b vbe.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc3e6d
    test AL, strict byte 001h                 ; a8 01                       ; 0xc3e70 vbe.c:428
    je short 03e84h                           ; 74 10                       ; 0xc3e72
    lea di, [bx+00ch]                         ; 8d 7f 0c                    ; 0xc3e74 vbe.c:429
    mov word [es:di], 00629h                  ; 26 c7 05 29 06              ; 0xc3e77 vbe.c:62
    lea di, [bx+00eh]                         ; 8d 7f 0e                    ; 0xc3e7c vbe.c:431
    mov word [es:di], 0c000h                  ; 26 c7 05 00 c0              ; 0xc3e7f vbe.c:62
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc3e84 vbe.c:434
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e87
    call 00570h                               ; e8 e3 c6                    ; 0xc3e8a
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e8d vbe.c:435
    call 00577h                               ; e8 e4 c6                    ; 0xc3e90
    add bx, strict byte 0002ah                ; 83 c3 2a                    ; 0xc3e93
    mov es, si                                ; 8e c6                       ; 0xc3e96 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3e98
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc3e9b vbe.c:437
    jmp short 03ea3h                          ; eb 03                       ; 0xc3e9e vbe.c:438
    mov ax, 00100h                            ; b8 00 01                    ; 0xc3ea0 vbe.c:442
    push SS                                   ; 16                          ; 0xc3ea3 vbe.c:445
    pop ES                                    ; 07                          ; 0xc3ea4
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc3ea5
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3ea8
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3eab vbe.c:446
    pop di                                    ; 5f                          ; 0xc3eae
    pop si                                    ; 5e                          ; 0xc3eaf
    pop bp                                    ; 5d                          ; 0xc3eb0
    retn                                      ; c3                          ; 0xc3eb1
  ; disGetNextSymbol 0xc3eb2 LB 0x407 -> off=0x0 cb=00000000000000e5 uValue=00000000000c3eb2 'vbe_biosfn_set_mode'
vbe_biosfn_set_mode:                         ; 0xc3eb2 LB 0xe5
    push bp                                   ; 55                          ; 0xc3eb2 vbe.c:458
    mov bp, sp                                ; 89 e5                       ; 0xc3eb3
    push si                                   ; 56                          ; 0xc3eb5
    push di                                   ; 57                          ; 0xc3eb6
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc3eb7
    mov si, ax                                ; 89 c6                       ; 0xc3eba
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3ebc
    test byte [bp-009h], 040h                 ; f6 46 f7 40                 ; 0xc3ebf vbe.c:466
    db  00fh, 095h, 0c0h
    ; setne al                                  ; 0f 95 c0                  ; 0xc3ec3
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3ec6
    mov ax, dx                                ; 89 d0                       ; 0xc3ec9
    test dx, dx                               ; 85 d2                       ; 0xc3ecb vbe.c:467
    je short 03ed2h                           ; 74 03                       ; 0xc3ecd
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3ecf
    mov byte [bp-008h], dl                    ; 88 56 f8                    ; 0xc3ed2
    test byte [bp-009h], 080h                 ; f6 46 f7 80                 ; 0xc3ed5 vbe.c:468
    je short 03ee0h                           ; 74 05                       ; 0xc3ed9
    mov dx, 00080h                            ; ba 80 00                    ; 0xc3edb
    jmp short 03ee2h                          ; eb 02                       ; 0xc3ede
    xor dx, dx                                ; 31 d2                       ; 0xc3ee0
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc3ee2
    and byte [bp-009h], 001h                  ; 80 66 f7 01                 ; 0xc3ee5 vbe.c:470
    cmp word [bp-00ah], 00100h                ; 81 7e f6 00 01              ; 0xc3ee9 vbe.c:473
    jnc short 03f02h                          ; 73 12                       ; 0xc3eee
    xor ax, ax                                ; 31 c0                       ; 0xc3ef0 vbe.c:477
    call 005ddh                               ; e8 e8 c6                    ; 0xc3ef2
    movzx ax, byte [bp-00ah]                  ; 0f b6 46 f6                 ; 0xc3ef5 vbe.c:481
    call 013ddh                               ; e8 e1 d4                    ; 0xc3ef9
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc3efc vbe.c:482
    jmp near 03f8bh                           ; e9 89 00                    ; 0xc3eff vbe.c:483
    mov dx, ax                                ; 89 c2                       ; 0xc3f02 vbe.c:486
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3f04
    call 03c86h                               ; e8 7c fd                    ; 0xc3f07
    mov bx, ax                                ; 89 c3                       ; 0xc3f0a
    test ax, ax                               ; 85 c0                       ; 0xc3f0c vbe.c:488
    je short 03f88h                           ; 74 78                       ; 0xc3f0e
    lea dx, [bx+014h]                         ; 8d 57 14                    ; 0xc3f10 vbe.c:493
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f13
    call 03c08h                               ; e8 ef fc                    ; 0xc3f16
    mov cx, ax                                ; 89 c1                       ; 0xc3f19
    lea dx, [bx+016h]                         ; 8d 57 16                    ; 0xc3f1b vbe.c:494
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f1e
    call 03c08h                               ; e8 e4 fc                    ; 0xc3f21
    mov di, ax                                ; 89 c7                       ; 0xc3f24
    lea dx, [bx+01bh]                         ; 8d 57 1b                    ; 0xc3f26 vbe.c:495
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f29
    call 03c1ah                               ; e8 eb fc                    ; 0xc3f2c
    mov bl, al                                ; 88 c3                       ; 0xc3f2f
    mov dl, al                                ; 88 c2                       ; 0xc3f31
    xor ax, ax                                ; 31 c0                       ; 0xc3f33 vbe.c:503
    call 005ddh                               ; e8 a5 c6                    ; 0xc3f35
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc3f38 vbe.c:505
    jne short 03f43h                          ; 75 06                       ; 0xc3f3b
    mov ax, strict word 0006ah                ; b8 6a 00                    ; 0xc3f3d vbe.c:507
    call 013ddh                               ; e8 9a d4                    ; 0xc3f40
    movzx ax, dl                              ; 0f b6 c2                    ; 0xc3f43 vbe.c:510
    call 03b7fh                               ; e8 36 fc                    ; 0xc3f46
    mov ax, cx                                ; 89 c8                       ; 0xc3f49 vbe.c:511
    call 03b28h                               ; e8 da fb                    ; 0xc3f4b
    mov ax, di                                ; 89 f8                       ; 0xc3f4e vbe.c:512
    call 03b47h                               ; e8 f4 fb                    ; 0xc3f50
    xor ax, ax                                ; 31 c0                       ; 0xc3f53 vbe.c:513
    call 00603h                               ; e8 ab c6                    ; 0xc3f55
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3f58 vbe.c:514
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc3f5b
    movzx dx, al                              ; 0f b6 d0                    ; 0xc3f5d
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc3f60
    or ax, dx                                 ; 09 d0                       ; 0xc3f64
    call 005ddh                               ; e8 74 c6                    ; 0xc3f66
    call 006d2h                               ; e8 66 c7                    ; 0xc3f69 vbe.c:515
    mov bx, 000bah                            ; bb ba 00                    ; 0xc3f6c vbe.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3f6f
    mov es, ax                                ; 8e c0                       ; 0xc3f72
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3f74
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3f77
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3f7a vbe.c:518
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc3f7d
    mov bx, 00087h                            ; bb 87 00                    ; 0xc3f7f vbe.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3f82
    jmp near 03efch                           ; e9 74 ff                    ; 0xc3f85
    mov ax, 00100h                            ; b8 00 01                    ; 0xc3f88 vbe.c:527
    push SS                                   ; 16                          ; 0xc3f8b vbe.c:531
    pop ES                                    ; 07                          ; 0xc3f8c
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3f8d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3f90 vbe.c:532
    pop di                                    ; 5f                          ; 0xc3f93
    pop si                                    ; 5e                          ; 0xc3f94
    pop bp                                    ; 5d                          ; 0xc3f95
    retn                                      ; c3                          ; 0xc3f96
  ; disGetNextSymbol 0xc3f97 LB 0x322 -> off=0x0 cb=0000000000000008 uValue=00000000000c3f97 'vbe_biosfn_read_video_state_size'
vbe_biosfn_read_video_state_size:            ; 0xc3f97 LB 0x8
    push bp                                   ; 55                          ; 0xc3f97 vbe.c:534
    mov bp, sp                                ; 89 e5                       ; 0xc3f98
    mov ax, strict word 00012h                ; b8 12 00                    ; 0xc3f9a vbe.c:537
    pop bp                                    ; 5d                          ; 0xc3f9d
    retn                                      ; c3                          ; 0xc3f9e
  ; disGetNextSymbol 0xc3f9f LB 0x31a -> off=0x0 cb=000000000000004b uValue=00000000000c3f9f 'vbe_biosfn_save_video_state'
vbe_biosfn_save_video_state:                 ; 0xc3f9f LB 0x4b
    push bp                                   ; 55                          ; 0xc3f9f vbe.c:539
    mov bp, sp                                ; 89 e5                       ; 0xc3fa0
    push bx                                   ; 53                          ; 0xc3fa2
    push cx                                   ; 51                          ; 0xc3fa3
    push si                                   ; 56                          ; 0xc3fa4
    mov si, ax                                ; 89 c6                       ; 0xc3fa5
    mov bx, dx                                ; 89 d3                       ; 0xc3fa7
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc3fa9 vbe.c:543
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3fac
    out DX, ax                                ; ef                          ; 0xc3faf
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3fb0 vbe.c:544
    in ax, DX                                 ; ed                          ; 0xc3fb3
    mov es, si                                ; 8e c6                       ; 0xc3fb4 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3fb6
    inc bx                                    ; 43                          ; 0xc3fb9 vbe.c:546
    inc bx                                    ; 43                          ; 0xc3fba
    test AL, strict byte 001h                 ; a8 01                       ; 0xc3fbb vbe.c:547
    je short 03fe2h                           ; 74 23                       ; 0xc3fbd
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc3fbf vbe.c:549
    jmp short 03fc9h                          ; eb 05                       ; 0xc3fc2
    cmp cx, strict byte 00009h                ; 83 f9 09                    ; 0xc3fc4
    jnbe short 03fe2h                         ; 77 19                       ; 0xc3fc7
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc3fc9 vbe.c:550
    je short 03fdfh                           ; 74 11                       ; 0xc3fcc
    mov ax, cx                                ; 89 c8                       ; 0xc3fce vbe.c:551
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3fd0
    out DX, ax                                ; ef                          ; 0xc3fd3
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3fd4 vbe.c:552
    in ax, DX                                 ; ed                          ; 0xc3fd7
    mov es, si                                ; 8e c6                       ; 0xc3fd8 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3fda
    inc bx                                    ; 43                          ; 0xc3fdd vbe.c:553
    inc bx                                    ; 43                          ; 0xc3fde
    inc cx                                    ; 41                          ; 0xc3fdf vbe.c:555
    jmp short 03fc4h                          ; eb e2                       ; 0xc3fe0
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3fe2 vbe.c:556
    pop si                                    ; 5e                          ; 0xc3fe5
    pop cx                                    ; 59                          ; 0xc3fe6
    pop bx                                    ; 5b                          ; 0xc3fe7
    pop bp                                    ; 5d                          ; 0xc3fe8
    retn                                      ; c3                          ; 0xc3fe9
  ; disGetNextSymbol 0xc3fea LB 0x2cf -> off=0x0 cb=000000000000008f uValue=00000000000c3fea 'vbe_biosfn_restore_video_state'
vbe_biosfn_restore_video_state:              ; 0xc3fea LB 0x8f
    push bp                                   ; 55                          ; 0xc3fea vbe.c:559
    mov bp, sp                                ; 89 e5                       ; 0xc3feb
    push bx                                   ; 53                          ; 0xc3fed
    push cx                                   ; 51                          ; 0xc3fee
    push si                                   ; 56                          ; 0xc3fef
    push ax                                   ; 50                          ; 0xc3ff0
    mov cx, ax                                ; 89 c1                       ; 0xc3ff1
    mov bx, dx                                ; 89 d3                       ; 0xc3ff3
    mov es, ax                                ; 8e c0                       ; 0xc3ff5 vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3ff7
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3ffa
    inc bx                                    ; 43                          ; 0xc3ffd vbe.c:564
    inc bx                                    ; 43                          ; 0xc3ffe
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc3fff vbe.c:566
    jne short 04015h                          ; 75 10                       ; 0xc4003
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc4005 vbe.c:567
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4008
    out DX, ax                                ; ef                          ; 0xc400b
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc400c vbe.c:568
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc400f
    out DX, ax                                ; ef                          ; 0xc4012
    jmp short 04071h                          ; eb 5c                       ; 0xc4013 vbe.c:569
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc4015 vbe.c:570
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4018
    out DX, ax                                ; ef                          ; 0xc401b
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc401c vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc401f vbe.c:58
    out DX, ax                                ; ef                          ; 0xc4022
    inc bx                                    ; 43                          ; 0xc4023 vbe.c:572
    inc bx                                    ; 43                          ; 0xc4024
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc4025
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4028
    out DX, ax                                ; ef                          ; 0xc402b
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc402c vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc402f vbe.c:58
    out DX, ax                                ; ef                          ; 0xc4032
    inc bx                                    ; 43                          ; 0xc4033 vbe.c:575
    inc bx                                    ; 43                          ; 0xc4034
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc4035
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4038
    out DX, ax                                ; ef                          ; 0xc403b
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc403c vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc403f vbe.c:58
    out DX, ax                                ; ef                          ; 0xc4042
    inc bx                                    ; 43                          ; 0xc4043 vbe.c:578
    inc bx                                    ; 43                          ; 0xc4044
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc4045
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4048
    out DX, ax                                ; ef                          ; 0xc404b
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc404c vbe.c:580
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc404f
    out DX, ax                                ; ef                          ; 0xc4052
    mov si, strict word 00005h                ; be 05 00                    ; 0xc4053 vbe.c:582
    jmp short 0405dh                          ; eb 05                       ; 0xc4056
    cmp si, strict byte 00009h                ; 83 fe 09                    ; 0xc4058
    jnbe short 04071h                         ; 77 14                       ; 0xc405b
    mov ax, si                                ; 89 f0                       ; 0xc405d vbe.c:583
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc405f
    out DX, ax                                ; ef                          ; 0xc4062
    mov es, cx                                ; 8e c1                       ; 0xc4063 vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc4065
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4068 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc406b
    inc bx                                    ; 43                          ; 0xc406c vbe.c:585
    inc bx                                    ; 43                          ; 0xc406d
    inc si                                    ; 46                          ; 0xc406e vbe.c:586
    jmp short 04058h                          ; eb e7                       ; 0xc406f
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc4071 vbe.c:588
    pop si                                    ; 5e                          ; 0xc4074
    pop cx                                    ; 59                          ; 0xc4075
    pop bx                                    ; 5b                          ; 0xc4076
    pop bp                                    ; 5d                          ; 0xc4077
    retn                                      ; c3                          ; 0xc4078
  ; disGetNextSymbol 0xc4079 LB 0x240 -> off=0x0 cb=000000000000008c uValue=00000000000c4079 'vbe_biosfn_save_restore_state'
vbe_biosfn_save_restore_state:               ; 0xc4079 LB 0x8c
    push bp                                   ; 55                          ; 0xc4079 vbe.c:604
    mov bp, sp                                ; 89 e5                       ; 0xc407a
    push si                                   ; 56                          ; 0xc407c
    push di                                   ; 57                          ; 0xc407d
    push ax                                   ; 50                          ; 0xc407e
    mov si, ax                                ; 89 c6                       ; 0xc407f
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc4081
    mov ax, bx                                ; 89 d8                       ; 0xc4084
    mov bx, word [bp+004h]                    ; 8b 5e 04                    ; 0xc4086
    mov di, strict word 0004fh                ; bf 4f 00                    ; 0xc4089 vbe.c:609
    xor ah, ah                                ; 30 e4                       ; 0xc408c vbe.c:610
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc408e
    je short 040d8h                           ; 74 45                       ; 0xc4091
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc4093
    je short 040bch                           ; 74 24                       ; 0xc4096
    test ax, ax                               ; 85 c0                       ; 0xc4098
    jne short 040f4h                          ; 75 58                       ; 0xc409a
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc409c vbe.c:612
    call 03024h                               ; e8 82 ef                    ; 0xc409f
    mov cx, ax                                ; 89 c1                       ; 0xc40a2
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc40a4 vbe.c:616
    je short 040afh                           ; 74 05                       ; 0xc40a8
    call 03f97h                               ; e8 ea fe                    ; 0xc40aa vbe.c:617
    add ax, cx                                ; 01 c8                       ; 0xc40ad
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc40af vbe.c:618
    shr ax, 006h                              ; c1 e8 06                    ; 0xc40b2
    push SS                                   ; 16                          ; 0xc40b5
    pop ES                                    ; 07                          ; 0xc40b6
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc40b7
    jmp short 040f7h                          ; eb 3b                       ; 0xc40ba vbe.c:619
    push SS                                   ; 16                          ; 0xc40bc vbe.c:621
    pop ES                                    ; 07                          ; 0xc40bd
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc40be
    mov dx, cx                                ; 89 ca                       ; 0xc40c1 vbe.c:622
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc40c3
    call 0305fh                               ; e8 96 ef                    ; 0xc40c6
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc40c9 vbe.c:626
    je short 040f7h                           ; 74 28                       ; 0xc40cd
    mov dx, ax                                ; 89 c2                       ; 0xc40cf vbe.c:627
    mov ax, cx                                ; 89 c8                       ; 0xc40d1
    call 03f9fh                               ; e8 c9 fe                    ; 0xc40d3
    jmp short 040f7h                          ; eb 1f                       ; 0xc40d6 vbe.c:628
    push SS                                   ; 16                          ; 0xc40d8 vbe.c:630
    pop ES                                    ; 07                          ; 0xc40d9
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc40da
    mov dx, cx                                ; 89 ca                       ; 0xc40dd vbe.c:631
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc40df
    call 03335h                               ; e8 50 f2                    ; 0xc40e2
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc40e5 vbe.c:635
    je short 040f7h                           ; 74 0c                       ; 0xc40e9
    mov dx, ax                                ; 89 c2                       ; 0xc40eb vbe.c:636
    mov ax, cx                                ; 89 c8                       ; 0xc40ed
    call 03feah                               ; e8 f8 fe                    ; 0xc40ef
    jmp short 040f7h                          ; eb 03                       ; 0xc40f2 vbe.c:637
    mov di, 00100h                            ; bf 00 01                    ; 0xc40f4 vbe.c:640
    push SS                                   ; 16                          ; 0xc40f7 vbe.c:643
    pop ES                                    ; 07                          ; 0xc40f8
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc40f9
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc40fc vbe.c:644
    pop di                                    ; 5f                          ; 0xc40ff
    pop si                                    ; 5e                          ; 0xc4100
    pop bp                                    ; 5d                          ; 0xc4101
    retn 00002h                               ; c2 02 00                    ; 0xc4102
  ; disGetNextSymbol 0xc4105 LB 0x1b4 -> off=0x0 cb=00000000000000cf uValue=00000000000c4105 'vbe_biosfn_get_set_scanline_length'
vbe_biosfn_get_set_scanline_length:          ; 0xc4105 LB 0xcf
    push bp                                   ; 55                          ; 0xc4105 vbe.c:665
    mov bp, sp                                ; 89 e5                       ; 0xc4106
    push si                                   ; 56                          ; 0xc4108
    push di                                   ; 57                          ; 0xc4109
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc410a
    push ax                                   ; 50                          ; 0xc410d
    mov di, dx                                ; 89 d7                       ; 0xc410e
    mov si, bx                                ; 89 de                       ; 0xc4110
    mov word [bp-008h], cx                    ; 89 4e f8                    ; 0xc4112
    call 03b9eh                               ; e8 86 fa                    ; 0xc4115 vbe.c:674
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc4118 vbe.c:675
    jne short 04121h                          ; 75 05                       ; 0xc411a
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc411c
    jmp short 04124h                          ; eb 03                       ; 0xc411f
    movzx cx, al                              ; 0f b6 c8                    ; 0xc4121
    call 03bd6h                               ; e8 af fa                    ; 0xc4124 vbe.c:676
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc4127
    mov word [bp-006h], strict word 0004fh    ; c7 46 fa 4f 00              ; 0xc412a vbe.c:677
    push SS                                   ; 16                          ; 0xc412f vbe.c:678
    pop ES                                    ; 07                          ; 0xc4130
    mov bx, word [es:si]                      ; 26 8b 1c                    ; 0xc4131
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc4134 vbe.c:679
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc4137 vbe.c:683
    je short 04146h                           ; 74 0b                       ; 0xc4139
    cmp AL, strict byte 001h                  ; 3c 01                       ; 0xc413b
    je short 0416dh                           ; 74 2e                       ; 0xc413d
    test al, al                               ; 84 c0                       ; 0xc413f
    je short 04168h                           ; 74 25                       ; 0xc4141
    jmp near 041bdh                           ; e9 77 00                    ; 0xc4143
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc4146 vbe.c:685
    jne short 04150h                          ; 75 05                       ; 0xc4149
    sal bx, 003h                              ; c1 e3 03                    ; 0xc414b vbe.c:686
    jmp short 04168h                          ; eb 18                       ; 0xc414e vbe.c:687
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc4150 vbe.c:688
    cwd                                       ; 99                          ; 0xc4153
    sal dx, 003h                              ; c1 e2 03                    ; 0xc4154
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc4157
    sar ax, 003h                              ; c1 f8 03                    ; 0xc4159
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc415c
    mov ax, bx                                ; 89 d8                       ; 0xc415f
    xor dx, dx                                ; 31 d2                       ; 0xc4161
    div word [bp-00ch]                        ; f7 76 f4                    ; 0xc4163
    mov bx, ax                                ; 89 c3                       ; 0xc4166
    mov ax, bx                                ; 89 d8                       ; 0xc4168 vbe.c:691
    call 03bb7h                               ; e8 4a fa                    ; 0xc416a
    call 03bd6h                               ; e8 66 fa                    ; 0xc416d vbe.c:694
    mov bx, ax                                ; 89 c3                       ; 0xc4170
    push SS                                   ; 16                          ; 0xc4172 vbe.c:695
    pop ES                                    ; 07                          ; 0xc4173
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc4174
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc4177 vbe.c:696
    jne short 04181h                          ; 75 05                       ; 0xc417a
    shr bx, 003h                              ; c1 eb 03                    ; 0xc417c vbe.c:697
    jmp short 04190h                          ; eb 0f                       ; 0xc417f vbe.c:698
    movzx ax, cl                              ; 0f b6 c1                    ; 0xc4181 vbe.c:699
    cwd                                       ; 99                          ; 0xc4184
    sal dx, 003h                              ; c1 e2 03                    ; 0xc4185
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc4188
    sar ax, 003h                              ; c1 f8 03                    ; 0xc418a
    imul bx, ax                               ; 0f af d8                    ; 0xc418d
    add bx, strict byte 00003h                ; 83 c3 03                    ; 0xc4190 vbe.c:700
    and bl, 0fch                              ; 80 e3 fc                    ; 0xc4193
    push SS                                   ; 16                          ; 0xc4196 vbe.c:701
    pop ES                                    ; 07                          ; 0xc4197
    mov word [es:di], bx                      ; 26 89 1d                    ; 0xc4198
    call 03befh                               ; e8 51 fa                    ; 0xc419b vbe.c:702
    push SS                                   ; 16                          ; 0xc419e
    pop ES                                    ; 07                          ; 0xc419f
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc41a0
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc41a3
    call 03b66h                               ; e8 bd f9                    ; 0xc41a6 vbe.c:703
    push SS                                   ; 16                          ; 0xc41a9
    pop ES                                    ; 07                          ; 0xc41aa
    cmp ax, word [es:bx]                      ; 26 3b 07                    ; 0xc41ab
    jbe short 041c2h                          ; 76 12                       ; 0xc41ae
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc41b0 vbe.c:704
    call 03bb7h                               ; e8 01 fa                    ; 0xc41b3
    mov word [bp-006h], 00200h                ; c7 46 fa 00 02              ; 0xc41b6 vbe.c:705
    jmp short 041c2h                          ; eb 05                       ; 0xc41bb vbe.c:707
    mov word [bp-006h], 00100h                ; c7 46 fa 00 01              ; 0xc41bd vbe.c:710
    push SS                                   ; 16                          ; 0xc41c2 vbe.c:713
    pop ES                                    ; 07                          ; 0xc41c3
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc41c4
    mov bx, word [bp-00eh]                    ; 8b 5e f2                    ; 0xc41c7
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc41ca
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc41cd vbe.c:714
    pop di                                    ; 5f                          ; 0xc41d0
    pop si                                    ; 5e                          ; 0xc41d1
    pop bp                                    ; 5d                          ; 0xc41d2
    retn                                      ; c3                          ; 0xc41d3
  ; disGetNextSymbol 0xc41d4 LB 0xe5 -> off=0x0 cb=00000000000000e5 uValue=00000000000c41d4 'private_biosfn_custom_mode'
private_biosfn_custom_mode:                  ; 0xc41d4 LB 0xe5
    push bp                                   ; 55                          ; 0xc41d4 vbe.c:740
    mov bp, sp                                ; 89 e5                       ; 0xc41d5
    push si                                   ; 56                          ; 0xc41d7
    push di                                   ; 57                          ; 0xc41d8
    push ax                                   ; 50                          ; 0xc41d9
    push ax                                   ; 50                          ; 0xc41da
    push ax                                   ; 50                          ; 0xc41db
    mov si, dx                                ; 89 d6                       ; 0xc41dc
    mov dx, cx                                ; 89 ca                       ; 0xc41de
    mov di, strict word 0004fh                ; bf 4f 00                    ; 0xc41e0 vbe.c:753
    push SS                                   ; 16                          ; 0xc41e3 vbe.c:754
    pop ES                                    ; 07                          ; 0xc41e4
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc41e5
    test al, al                               ; 84 c0                       ; 0xc41e8 vbe.c:755
    jne short 0420eh                          ; 75 22                       ; 0xc41ea
    push SS                                   ; 16                          ; 0xc41ec vbe.c:757
    pop ES                                    ; 07                          ; 0xc41ed
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc41ee
    mov bx, dx                                ; 89 d3                       ; 0xc41f1 vbe.c:758
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc41f3
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc41f6 vbe.c:759
    shr ax, 008h                              ; c1 e8 08                    ; 0xc41f9
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc41fc
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc41ff
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc4202 vbe.c:764
    je short 04214h                           ; 74 0e                       ; 0xc4204
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc4206
    je short 04214h                           ; 74 0a                       ; 0xc4208
    cmp AL, strict byte 020h                  ; 3c 20                       ; 0xc420a
    je short 04214h                           ; 74 06                       ; 0xc420c
    mov di, 00100h                            ; bf 00 01                    ; 0xc420e vbe.c:765
    jmp near 042aah                           ; e9 96 00                    ; 0xc4211 vbe.c:766
    push SS                                   ; 16                          ; 0xc4214 vbe.c:770
    pop ES                                    ; 07                          ; 0xc4215
    test byte [es:si+001h], 080h              ; 26 f6 44 01 80              ; 0xc4216
    je short 04222h                           ; 74 05                       ; 0xc421b
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc421d
    jmp short 04224h                          ; eb 02                       ; 0xc4220
    xor ax, ax                                ; 31 c0                       ; 0xc4222
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc4224
    cmp cx, 00280h                            ; 81 f9 80 02                 ; 0xc4227 vbe.c:773
    jnc short 04232h                          ; 73 05                       ; 0xc422b
    mov cx, 00280h                            ; b9 80 02                    ; 0xc422d vbe.c:774
    jmp short 0423bh                          ; eb 09                       ; 0xc4230 vbe.c:775
    cmp cx, 00a00h                            ; 81 f9 00 0a                 ; 0xc4232
    jbe short 0423bh                          ; 76 03                       ; 0xc4236
    mov cx, 00a00h                            ; b9 00 0a                    ; 0xc4238 vbe.c:776
    cmp bx, 001e0h                            ; 81 fb e0 01                 ; 0xc423b vbe.c:777
    jnc short 04246h                          ; 73 05                       ; 0xc423f
    mov bx, 001e0h                            ; bb e0 01                    ; 0xc4241 vbe.c:778
    jmp short 0424fh                          ; eb 09                       ; 0xc4244 vbe.c:779
    cmp bx, 00780h                            ; 81 fb 80 07                 ; 0xc4246
    jbe short 0424fh                          ; 76 03                       ; 0xc424a
    mov bx, 00780h                            ; bb 80 07                    ; 0xc424c vbe.c:780
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc424f vbe.c:786
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4252
    call 03c08h                               ; e8 b0 f9                    ; 0xc4255
    mov si, ax                                ; 89 c6                       ; 0xc4258
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc425a vbe.c:789
    cwd                                       ; 99                          ; 0xc425e
    sal dx, 003h                              ; c1 e2 03                    ; 0xc425f
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc4262
    sar ax, 003h                              ; c1 f8 03                    ; 0xc4264
    imul ax, cx                               ; 0f af c1                    ; 0xc4267
    add ax, strict word 00003h                ; 05 03 00                    ; 0xc426a vbe.c:790
    and AL, strict byte 0fch                  ; 24 fc                       ; 0xc426d
    mov dx, bx                                ; 89 da                       ; 0xc426f vbe.c:792
    mul dx                                    ; f7 e2                       ; 0xc4271
    cmp dx, si                                ; 39 f2                       ; 0xc4273 vbe.c:794
    jnbe short 0427dh                         ; 77 06                       ; 0xc4275
    jne short 04282h                          ; 75 09                       ; 0xc4277
    test ax, ax                               ; 85 c0                       ; 0xc4279
    jbe short 04282h                          ; 76 05                       ; 0xc427b
    mov di, 00200h                            ; bf 00 02                    ; 0xc427d vbe.c:796
    jmp short 042aah                          ; eb 28                       ; 0xc4280 vbe.c:797
    xor ax, ax                                ; 31 c0                       ; 0xc4282 vbe.c:801
    call 005ddh                               ; e8 56 c3                    ; 0xc4284
    movzx ax, byte [bp-008h]                  ; 0f b6 46 f8                 ; 0xc4287 vbe.c:802
    call 03b7fh                               ; e8 f1 f8                    ; 0xc428b
    mov ax, cx                                ; 89 c8                       ; 0xc428e vbe.c:803
    call 03b28h                               ; e8 95 f8                    ; 0xc4290
    mov ax, bx                                ; 89 d8                       ; 0xc4293 vbe.c:804
    call 03b47h                               ; e8 af f8                    ; 0xc4295
    xor ax, ax                                ; 31 c0                       ; 0xc4298 vbe.c:805
    call 00603h                               ; e8 66 c3                    ; 0xc429a
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc429d vbe.c:806
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc42a0
    xor ah, ah                                ; 30 e4                       ; 0xc42a2
    call 005ddh                               ; e8 36 c3                    ; 0xc42a4
    call 006d2h                               ; e8 28 c4                    ; 0xc42a7 vbe.c:807
    push SS                                   ; 16                          ; 0xc42aa vbe.c:815
    pop ES                                    ; 07                          ; 0xc42ab
    mov bx, word [bp-00ah]                    ; 8b 5e f6                    ; 0xc42ac
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc42af
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc42b2 vbe.c:816
    pop di                                    ; 5f                          ; 0xc42b5
    pop si                                    ; 5e                          ; 0xc42b6
    pop bp                                    ; 5d                          ; 0xc42b7
    retn                                      ; c3                          ; 0xc42b8

  ; Padding 0x387 bytes at 0xc42b9
  times 903 db 0

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
    db  073h, 033h, 038h, 036h, 02fh, 056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh, 073h
    db  033h, 038h, 036h, 02eh, 073h, 079h, 06dh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
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
    db  000h, 0a8h
