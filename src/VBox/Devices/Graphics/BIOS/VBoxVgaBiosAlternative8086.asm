; $Id: VBoxVgaBiosAlternative8086.asm 169020 2025-05-27 12:59:42Z vadim.galitsyn@oracle.com $ 
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





section VGAROM progbits vstart=0x0 align=1 ; size=0x94f class=CODE group=AUTO
  ; disGetNextSymbol 0xc0000 LB 0x94f -> off=0x28 cb=0000000000000578 uValue=00000000000c0028 'vgabios_int10_handler'
    db  055h, 0aah, 040h, 0ebh, 01dh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
    db  000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 049h, 042h
    db  04dh, 000h, 00eh, 01fh, 0fch, 0e9h, 03eh, 00ah
vgabios_int10_handler:                       ; 0xc0028 LB 0x578
    pushfw                                    ; 9c                          ; 0xc0028 vgarom.asm:91
    cmp ah, 00fh                              ; 80 fc 0f                    ; 0xc0029 vgarom.asm:104
    jne short 00034h                          ; 75 06                       ; 0xc002c vgarom.asm:105
    call 0018dh                               ; e8 5c 01                    ; 0xc002e vgarom.asm:106
    jmp near 000fdh                           ; e9 c9 00                    ; 0xc0031 vgarom.asm:107
    cmp ah, 01ah                              ; 80 fc 1a                    ; 0xc0034 vgarom.asm:109
    jne short 0003fh                          ; 75 06                       ; 0xc0037 vgarom.asm:110
    call 00560h                               ; e8 24 05                    ; 0xc0039 vgarom.asm:111
    jmp near 000fdh                           ; e9 be 00                    ; 0xc003c vgarom.asm:112
    cmp ah, 00bh                              ; 80 fc 0b                    ; 0xc003f vgarom.asm:114
    jne short 0004ah                          ; 75 06                       ; 0xc0042 vgarom.asm:115
    call 000ffh                               ; e8 b8 00                    ; 0xc0044 vgarom.asm:116
    jmp near 000fdh                           ; e9 b3 00                    ; 0xc0047 vgarom.asm:117
    cmp ax, 01103h                            ; 3d 03 11                    ; 0xc004a vgarom.asm:119
    jne short 00055h                          ; 75 06                       ; 0xc004d vgarom.asm:120
    call 00454h                               ; e8 02 04                    ; 0xc004f vgarom.asm:121
    jmp near 000fdh                           ; e9 a8 00                    ; 0xc0052 vgarom.asm:122
    cmp ah, 012h                              ; 80 fc 12                    ; 0xc0055 vgarom.asm:124
    jne short 00099h                          ; 75 3f                       ; 0xc0058 vgarom.asm:125
    cmp bl, 010h                              ; 80 fb 10                    ; 0xc005a vgarom.asm:126
    jne short 00065h                          ; 75 06                       ; 0xc005d vgarom.asm:127
    call 00461h                               ; e8 ff 03                    ; 0xc005f vgarom.asm:128
    jmp near 000fdh                           ; e9 98 00                    ; 0xc0062 vgarom.asm:129
    cmp bl, 030h                              ; 80 fb 30                    ; 0xc0065 vgarom.asm:131
    jne short 00070h                          ; 75 06                       ; 0xc0068 vgarom.asm:132
    call 00484h                               ; e8 17 04                    ; 0xc006a vgarom.asm:133
    jmp near 000fdh                           ; e9 8d 00                    ; 0xc006d vgarom.asm:134
    cmp bl, 031h                              ; 80 fb 31                    ; 0xc0070 vgarom.asm:136
    jne short 0007bh                          ; 75 06                       ; 0xc0073 vgarom.asm:137
    call 004d7h                               ; e8 5f 04                    ; 0xc0075 vgarom.asm:138
    jmp near 000fdh                           ; e9 82 00                    ; 0xc0078 vgarom.asm:139
    cmp bl, 032h                              ; 80 fb 32                    ; 0xc007b vgarom.asm:141
    jne short 00085h                          ; 75 05                       ; 0xc007e vgarom.asm:142
    call 004fch                               ; e8 79 04                    ; 0xc0080 vgarom.asm:143
    jmp short 000fdh                          ; eb 78                       ; 0xc0083 vgarom.asm:144
    cmp bl, 033h                              ; 80 fb 33                    ; 0xc0085 vgarom.asm:146
    jne short 0008fh                          ; 75 05                       ; 0xc0088 vgarom.asm:147
    call 0051ah                               ; e8 8d 04                    ; 0xc008a vgarom.asm:148
    jmp short 000fdh                          ; eb 6e                       ; 0xc008d vgarom.asm:149
    cmp bl, 034h                              ; 80 fb 34                    ; 0xc008f vgarom.asm:151
    jne short 000e3h                          ; 75 4f                       ; 0xc0092 vgarom.asm:152
    call 0053eh                               ; e8 a7 04                    ; 0xc0094 vgarom.asm:153
    jmp short 000fdh                          ; eb 64                       ; 0xc0097 vgarom.asm:154
    cmp ax, 0101bh                            ; 3d 1b 10                    ; 0xc0099 vgarom.asm:156
    je short 000e3h                           ; 74 45                       ; 0xc009c vgarom.asm:157
    cmp ah, 010h                              ; 80 fc 10                    ; 0xc009e vgarom.asm:158
    jne short 000a8h                          ; 75 05                       ; 0xc00a1 vgarom.asm:162
    call 001b4h                               ; e8 0e 01                    ; 0xc00a3 vgarom.asm:164
    jmp short 000fdh                          ; eb 55                       ; 0xc00a6 vgarom.asm:165
    cmp ah, 04fh                              ; 80 fc 4f                    ; 0xc00a8 vgarom.asm:168
    jne short 000e3h                          ; 75 36                       ; 0xc00ab vgarom.asm:169
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc00ad vgarom.asm:170
    jne short 000b6h                          ; 75 05                       ; 0xc00af vgarom.asm:171
    call 0080bh                               ; e8 57 07                    ; 0xc00b1 vgarom.asm:172
    jmp short 000fdh                          ; eb 47                       ; 0xc00b4 vgarom.asm:173
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc00b6 vgarom.asm:175
    jne short 000bfh                          ; 75 05                       ; 0xc00b8 vgarom.asm:176
    call 00830h                               ; e8 73 07                    ; 0xc00ba vgarom.asm:177
    jmp short 000fdh                          ; eb 3e                       ; 0xc00bd vgarom.asm:178
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc00bf vgarom.asm:180
    jne short 000c8h                          ; 75 05                       ; 0xc00c1 vgarom.asm:181
    call 0085dh                               ; e8 97 07                    ; 0xc00c3 vgarom.asm:182
    jmp short 000fdh                          ; eb 35                       ; 0xc00c6 vgarom.asm:183
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc00c8 vgarom.asm:185
    jne short 000d1h                          ; 75 05                       ; 0xc00ca vgarom.asm:186
    call 00891h                               ; e8 c2 07                    ; 0xc00cc vgarom.asm:187
    jmp short 000fdh                          ; eb 2c                       ; 0xc00cf vgarom.asm:188
    cmp AL, strict byte 009h                  ; 3c 09                       ; 0xc00d1 vgarom.asm:190
    jne short 000dah                          ; 75 05                       ; 0xc00d3 vgarom.asm:191
    call 008c8h                               ; e8 f0 07                    ; 0xc00d5 vgarom.asm:192
    jmp short 000fdh                          ; eb 23                       ; 0xc00d8 vgarom.asm:193
    cmp AL, strict byte 00ah                  ; 3c 0a                       ; 0xc00da vgarom.asm:195
    jne short 000e3h                          ; 75 05                       ; 0xc00dc vgarom.asm:196
    call 0093bh                               ; e8 5a 08                    ; 0xc00de vgarom.asm:197
    jmp short 000fdh                          ; eb 1a                       ; 0xc00e1 vgarom.asm:198
    push ES                                   ; 06                          ; 0xc00e3 vgarom.asm:202
    push DS                                   ; 1e                          ; 0xc00e4 vgarom.asm:203
    push ax                                   ; 50                          ; 0xc00e5 vgarom.asm:109
    push cx                                   ; 51                          ; 0xc00e6 vgarom.asm:110
    push dx                                   ; 52                          ; 0xc00e7 vgarom.asm:111
    push bx                                   ; 53                          ; 0xc00e8 vgarom.asm:112
    push sp                                   ; 54                          ; 0xc00e9 vgarom.asm:113
    push bp                                   ; 55                          ; 0xc00ea vgarom.asm:114
    push si                                   ; 56                          ; 0xc00eb vgarom.asm:115
    push di                                   ; 57                          ; 0xc00ec vgarom.asm:116
    push CS                                   ; 0e                          ; 0xc00ed vgarom.asm:207
    pop DS                                    ; 1f                          ; 0xc00ee vgarom.asm:208
    cld                                       ; fc                          ; 0xc00ef vgarom.asm:209
    call 0394bh                               ; e8 58 38                    ; 0xc00f0 vgarom.asm:210
    pop di                                    ; 5f                          ; 0xc00f3 vgarom.asm:126
    pop si                                    ; 5e                          ; 0xc00f4 vgarom.asm:127
    pop bp                                    ; 5d                          ; 0xc00f5 vgarom.asm:128
    pop bx                                    ; 5b                          ; 0xc00f6 vgarom.asm:129
    pop bx                                    ; 5b                          ; 0xc00f7 vgarom.asm:130
    pop dx                                    ; 5a                          ; 0xc00f8 vgarom.asm:131
    pop cx                                    ; 59                          ; 0xc00f9 vgarom.asm:132
    pop ax                                    ; 58                          ; 0xc00fa vgarom.asm:133
    pop DS                                    ; 1f                          ; 0xc00fb vgarom.asm:213
    pop ES                                    ; 07                          ; 0xc00fc vgarom.asm:214
    popfw                                     ; 9d                          ; 0xc00fd vgarom.asm:216
    iret                                      ; cf                          ; 0xc00fe vgarom.asm:217
    cmp bh, 000h                              ; 80 ff 00                    ; 0xc00ff vgarom.asm:222
    je short 0010ah                           ; 74 06                       ; 0xc0102 vgarom.asm:223
    cmp bh, 001h                              ; 80 ff 01                    ; 0xc0104 vgarom.asm:224
    je short 0015bh                           ; 74 52                       ; 0xc0107 vgarom.asm:225
    retn                                      ; c3                          ; 0xc0109 vgarom.asm:229
    push ax                                   ; 50                          ; 0xc010a vgarom.asm:231
    push bx                                   ; 53                          ; 0xc010b vgarom.asm:232
    push cx                                   ; 51                          ; 0xc010c vgarom.asm:233
    push dx                                   ; 52                          ; 0xc010d vgarom.asm:234
    push DS                                   ; 1e                          ; 0xc010e vgarom.asm:235
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc010f vgarom.asm:236
    mov ds, dx                                ; 8e da                       ; 0xc0112 vgarom.asm:237
    mov dx, 003dah                            ; ba da 03                    ; 0xc0114 vgarom.asm:238
    in AL, DX                                 ; ec                          ; 0xc0117 vgarom.asm:239
    cmp byte [word 00049h], 003h              ; 80 3e 49 00 03              ; 0xc0118 vgarom.asm:240
    jbe short 0014eh                          ; 76 2f                       ; 0xc011d vgarom.asm:241
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc011f vgarom.asm:242
    mov AL, strict byte 000h                  ; b0 00                       ; 0xc0122 vgarom.asm:243
    out DX, AL                                ; ee                          ; 0xc0124 vgarom.asm:244
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc0125 vgarom.asm:245
    and AL, strict byte 00fh                  ; 24 0f                       ; 0xc0127 vgarom.asm:246
    test AL, strict byte 008h                 ; a8 08                       ; 0xc0129 vgarom.asm:247
    je short 0012fh                           ; 74 02                       ; 0xc012b vgarom.asm:248
    add AL, strict byte 008h                  ; 04 08                       ; 0xc012d vgarom.asm:249
    out DX, AL                                ; ee                          ; 0xc012f vgarom.asm:251
    mov CL, strict byte 001h                  ; b1 01                       ; 0xc0130 vgarom.asm:252
    and bl, 010h                              ; 80 e3 10                    ; 0xc0132 vgarom.asm:253
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0135 vgarom.asm:255
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc0138 vgarom.asm:256
    out DX, AL                                ; ee                          ; 0xc013a vgarom.asm:257
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc013b vgarom.asm:258
    in AL, DX                                 ; ec                          ; 0xc013e vgarom.asm:259
    and AL, strict byte 0efh                  ; 24 ef                       ; 0xc013f vgarom.asm:260
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc0141 vgarom.asm:261
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0143 vgarom.asm:262
    out DX, AL                                ; ee                          ; 0xc0146 vgarom.asm:263
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc0147 vgarom.asm:264
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc0149 vgarom.asm:265
    jne short 00135h                          ; 75 e7                       ; 0xc014c vgarom.asm:266
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc014e vgarom.asm:268
    out DX, AL                                ; ee                          ; 0xc0150 vgarom.asm:269
    mov dx, 003dah                            ; ba da 03                    ; 0xc0151 vgarom.asm:271
    in AL, DX                                 ; ec                          ; 0xc0154 vgarom.asm:272
    pop DS                                    ; 1f                          ; 0xc0155 vgarom.asm:274
    pop dx                                    ; 5a                          ; 0xc0156 vgarom.asm:275
    pop cx                                    ; 59                          ; 0xc0157 vgarom.asm:276
    pop bx                                    ; 5b                          ; 0xc0158 vgarom.asm:277
    pop ax                                    ; 58                          ; 0xc0159 vgarom.asm:278
    retn                                      ; c3                          ; 0xc015a vgarom.asm:279
    push ax                                   ; 50                          ; 0xc015b vgarom.asm:281
    push bx                                   ; 53                          ; 0xc015c vgarom.asm:282
    push cx                                   ; 51                          ; 0xc015d vgarom.asm:283
    push dx                                   ; 52                          ; 0xc015e vgarom.asm:284
    mov dx, 003dah                            ; ba da 03                    ; 0xc015f vgarom.asm:285
    in AL, DX                                 ; ec                          ; 0xc0162 vgarom.asm:286
    mov CL, strict byte 001h                  ; b1 01                       ; 0xc0163 vgarom.asm:287
    and bl, 001h                              ; 80 e3 01                    ; 0xc0165 vgarom.asm:288
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0168 vgarom.asm:290
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc016b vgarom.asm:291
    out DX, AL                                ; ee                          ; 0xc016d vgarom.asm:292
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc016e vgarom.asm:293
    in AL, DX                                 ; ec                          ; 0xc0171 vgarom.asm:294
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc0172 vgarom.asm:295
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc0174 vgarom.asm:296
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0176 vgarom.asm:297
    out DX, AL                                ; ee                          ; 0xc0179 vgarom.asm:298
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc017a vgarom.asm:299
    cmp cl, 004h                              ; 80 f9 04                    ; 0xc017c vgarom.asm:300
    jne short 00168h                          ; 75 e7                       ; 0xc017f vgarom.asm:301
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0181 vgarom.asm:302
    out DX, AL                                ; ee                          ; 0xc0183 vgarom.asm:303
    mov dx, 003dah                            ; ba da 03                    ; 0xc0184 vgarom.asm:305
    in AL, DX                                 ; ec                          ; 0xc0187 vgarom.asm:306
    pop dx                                    ; 5a                          ; 0xc0188 vgarom.asm:308
    pop cx                                    ; 59                          ; 0xc0189 vgarom.asm:309
    pop bx                                    ; 5b                          ; 0xc018a vgarom.asm:310
    pop ax                                    ; 58                          ; 0xc018b vgarom.asm:311
    retn                                      ; c3                          ; 0xc018c vgarom.asm:312
    push DS                                   ; 1e                          ; 0xc018d vgarom.asm:317
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc018e vgarom.asm:318
    mov ds, ax                                ; 8e d8                       ; 0xc0191 vgarom.asm:319
    push bx                                   ; 53                          ; 0xc0193 vgarom.asm:320
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc0194 vgarom.asm:321
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0197 vgarom.asm:322
    pop bx                                    ; 5b                          ; 0xc0199 vgarom.asm:323
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc019a vgarom.asm:324
    push bx                                   ; 53                          ; 0xc019c vgarom.asm:325
    mov bx, 00087h                            ; bb 87 00                    ; 0xc019d vgarom.asm:326
    mov ah, byte [bx]                         ; 8a 27                       ; 0xc01a0 vgarom.asm:327
    and ah, 080h                              ; 80 e4 80                    ; 0xc01a2 vgarom.asm:328
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc01a5 vgarom.asm:329
    mov al, byte [bx]                         ; 8a 07                       ; 0xc01a8 vgarom.asm:330
    db  00ah, 0c4h
    ; or al, ah                                 ; 0a c4                     ; 0xc01aa vgarom.asm:331
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc01ac vgarom.asm:332
    mov ah, byte [bx]                         ; 8a 27                       ; 0xc01af vgarom.asm:333
    pop bx                                    ; 5b                          ; 0xc01b1 vgarom.asm:334
    pop DS                                    ; 1f                          ; 0xc01b2 vgarom.asm:335
    retn                                      ; c3                          ; 0xc01b3 vgarom.asm:336
    cmp AL, strict byte 000h                  ; 3c 00                       ; 0xc01b4 vgarom.asm:341
    jne short 001bah                          ; 75 02                       ; 0xc01b6 vgarom.asm:342
    jmp short 0021bh                          ; eb 61                       ; 0xc01b8 vgarom.asm:343
    cmp AL, strict byte 001h                  ; 3c 01                       ; 0xc01ba vgarom.asm:345
    jne short 001c0h                          ; 75 02                       ; 0xc01bc vgarom.asm:346
    jmp short 00239h                          ; eb 79                       ; 0xc01be vgarom.asm:347
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc01c0 vgarom.asm:349
    jne short 001c6h                          ; 75 02                       ; 0xc01c2 vgarom.asm:350
    jmp short 00241h                          ; eb 7b                       ; 0xc01c4 vgarom.asm:351
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc01c6 vgarom.asm:353
    jne short 001cdh                          ; 75 03                       ; 0xc01c8 vgarom.asm:354
    jmp near 00272h                           ; e9 a5 00                    ; 0xc01ca vgarom.asm:355
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc01cd vgarom.asm:357
    jne short 001d4h                          ; 75 03                       ; 0xc01cf vgarom.asm:358
    jmp near 0029fh                           ; e9 cb 00                    ; 0xc01d1 vgarom.asm:359
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc01d4 vgarom.asm:361
    jne short 001dbh                          ; 75 03                       ; 0xc01d6 vgarom.asm:362
    jmp near 002c7h                           ; e9 ec 00                    ; 0xc01d8 vgarom.asm:363
    cmp AL, strict byte 009h                  ; 3c 09                       ; 0xc01db vgarom.asm:365
    jne short 001e2h                          ; 75 03                       ; 0xc01dd vgarom.asm:366
    jmp near 002d5h                           ; e9 f3 00                    ; 0xc01df vgarom.asm:367
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc01e2 vgarom.asm:369
    jne short 001e9h                          ; 75 03                       ; 0xc01e4 vgarom.asm:370
    jmp near 0031ah                           ; e9 31 01                    ; 0xc01e6 vgarom.asm:371
    cmp AL, strict byte 012h                  ; 3c 12                       ; 0xc01e9 vgarom.asm:373
    jne short 001f0h                          ; 75 03                       ; 0xc01eb vgarom.asm:374
    jmp near 00333h                           ; e9 43 01                    ; 0xc01ed vgarom.asm:375
    cmp AL, strict byte 013h                  ; 3c 13                       ; 0xc01f0 vgarom.asm:377
    jne short 001f7h                          ; 75 03                       ; 0xc01f2 vgarom.asm:378
    jmp near 0035bh                           ; e9 64 01                    ; 0xc01f4 vgarom.asm:379
    cmp AL, strict byte 015h                  ; 3c 15                       ; 0xc01f7 vgarom.asm:381
    jne short 001feh                          ; 75 03                       ; 0xc01f9 vgarom.asm:382
    jmp near 003aeh                           ; e9 b0 01                    ; 0xc01fb vgarom.asm:383
    cmp AL, strict byte 017h                  ; 3c 17                       ; 0xc01fe vgarom.asm:385
    jne short 00205h                          ; 75 03                       ; 0xc0200 vgarom.asm:386
    jmp near 003c9h                           ; e9 c4 01                    ; 0xc0202 vgarom.asm:387
    cmp AL, strict byte 018h                  ; 3c 18                       ; 0xc0205 vgarom.asm:389
    jne short 0020ch                          ; 75 03                       ; 0xc0207 vgarom.asm:390
    jmp near 003f1h                           ; e9 e5 01                    ; 0xc0209 vgarom.asm:391
    cmp AL, strict byte 019h                  ; 3c 19                       ; 0xc020c vgarom.asm:393
    jne short 00213h                          ; 75 03                       ; 0xc020e vgarom.asm:394
    jmp near 003fch                           ; e9 e9 01                    ; 0xc0210 vgarom.asm:395
    cmp AL, strict byte 01ah                  ; 3c 1a                       ; 0xc0213 vgarom.asm:397
    jne short 0021ah                          ; 75 03                       ; 0xc0215 vgarom.asm:398
    jmp near 00407h                           ; e9 ed 01                    ; 0xc0217 vgarom.asm:399
    retn                                      ; c3                          ; 0xc021a vgarom.asm:404
    cmp bl, 014h                              ; 80 fb 14                    ; 0xc021b vgarom.asm:407
    jnbe short 00238h                         ; 77 18                       ; 0xc021e vgarom.asm:408
    push ax                                   ; 50                          ; 0xc0220 vgarom.asm:409
    push dx                                   ; 52                          ; 0xc0221 vgarom.asm:410
    mov dx, 003dah                            ; ba da 03                    ; 0xc0222 vgarom.asm:411
    in AL, DX                                 ; ec                          ; 0xc0225 vgarom.asm:412
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0226 vgarom.asm:413
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc0229 vgarom.asm:414
    out DX, AL                                ; ee                          ; 0xc022b vgarom.asm:415
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc022c vgarom.asm:416
    out DX, AL                                ; ee                          ; 0xc022e vgarom.asm:417
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc022f vgarom.asm:418
    out DX, AL                                ; ee                          ; 0xc0231 vgarom.asm:419
    mov dx, 003dah                            ; ba da 03                    ; 0xc0232 vgarom.asm:421
    in AL, DX                                 ; ec                          ; 0xc0235 vgarom.asm:422
    pop dx                                    ; 5a                          ; 0xc0236 vgarom.asm:424
    pop ax                                    ; 58                          ; 0xc0237 vgarom.asm:425
    retn                                      ; c3                          ; 0xc0238 vgarom.asm:427
    push bx                                   ; 53                          ; 0xc0239 vgarom.asm:432
    mov BL, strict byte 011h                  ; b3 11                       ; 0xc023a vgarom.asm:433
    call 0021bh                               ; e8 dc ff                    ; 0xc023c vgarom.asm:434
    pop bx                                    ; 5b                          ; 0xc023f vgarom.asm:435
    retn                                      ; c3                          ; 0xc0240 vgarom.asm:436
    push ax                                   ; 50                          ; 0xc0241 vgarom.asm:441
    push bx                                   ; 53                          ; 0xc0242 vgarom.asm:442
    push cx                                   ; 51                          ; 0xc0243 vgarom.asm:443
    push dx                                   ; 52                          ; 0xc0244 vgarom.asm:444
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc0245 vgarom.asm:445
    mov dx, 003dah                            ; ba da 03                    ; 0xc0247 vgarom.asm:446
    in AL, DX                                 ; ec                          ; 0xc024a vgarom.asm:447
    mov CL, strict byte 000h                  ; b1 00                       ; 0xc024b vgarom.asm:448
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc024d vgarom.asm:449
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc0250 vgarom.asm:451
    out DX, AL                                ; ee                          ; 0xc0252 vgarom.asm:452
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0253 vgarom.asm:453
    out DX, AL                                ; ee                          ; 0xc0256 vgarom.asm:454
    inc bx                                    ; 43                          ; 0xc0257 vgarom.asm:455
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc0258 vgarom.asm:456
    cmp cl, 010h                              ; 80 f9 10                    ; 0xc025a vgarom.asm:457
    jne short 00250h                          ; 75 f1                       ; 0xc025d vgarom.asm:458
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc025f vgarom.asm:459
    out DX, AL                                ; ee                          ; 0xc0261 vgarom.asm:460
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0262 vgarom.asm:461
    out DX, AL                                ; ee                          ; 0xc0265 vgarom.asm:462
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0266 vgarom.asm:463
    out DX, AL                                ; ee                          ; 0xc0268 vgarom.asm:464
    mov dx, 003dah                            ; ba da 03                    ; 0xc0269 vgarom.asm:466
    in AL, DX                                 ; ec                          ; 0xc026c vgarom.asm:467
    pop dx                                    ; 5a                          ; 0xc026d vgarom.asm:469
    pop cx                                    ; 59                          ; 0xc026e vgarom.asm:470
    pop bx                                    ; 5b                          ; 0xc026f vgarom.asm:471
    pop ax                                    ; 58                          ; 0xc0270 vgarom.asm:472
    retn                                      ; c3                          ; 0xc0271 vgarom.asm:473
    push ax                                   ; 50                          ; 0xc0272 vgarom.asm:478
    push bx                                   ; 53                          ; 0xc0273 vgarom.asm:479
    push dx                                   ; 52                          ; 0xc0274 vgarom.asm:480
    mov dx, 003dah                            ; ba da 03                    ; 0xc0275 vgarom.asm:481
    in AL, DX                                 ; ec                          ; 0xc0278 vgarom.asm:482
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0279 vgarom.asm:483
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc027c vgarom.asm:484
    out DX, AL                                ; ee                          ; 0xc027e vgarom.asm:485
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc027f vgarom.asm:486
    in AL, DX                                 ; ec                          ; 0xc0282 vgarom.asm:487
    and AL, strict byte 0f7h                  ; 24 f7                       ; 0xc0283 vgarom.asm:488
    and bl, 001h                              ; 80 e3 01                    ; 0xc0285 vgarom.asm:489
    sal bl, 1                                 ; d0 e3                       ; 0xc0288 vgarom.asm:493
    sal bl, 1                                 ; d0 e3                       ; 0xc028a vgarom.asm:494
    sal bl, 1                                 ; d0 e3                       ; 0xc028c vgarom.asm:495
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc028e vgarom.asm:497
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0290 vgarom.asm:498
    out DX, AL                                ; ee                          ; 0xc0293 vgarom.asm:499
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0294 vgarom.asm:500
    out DX, AL                                ; ee                          ; 0xc0296 vgarom.asm:501
    mov dx, 003dah                            ; ba da 03                    ; 0xc0297 vgarom.asm:503
    in AL, DX                                 ; ec                          ; 0xc029a vgarom.asm:504
    pop dx                                    ; 5a                          ; 0xc029b vgarom.asm:506
    pop bx                                    ; 5b                          ; 0xc029c vgarom.asm:507
    pop ax                                    ; 58                          ; 0xc029d vgarom.asm:508
    retn                                      ; c3                          ; 0xc029e vgarom.asm:509
    cmp bl, 014h                              ; 80 fb 14                    ; 0xc029f vgarom.asm:514
    jnbe short 002c6h                         ; 77 22                       ; 0xc02a2 vgarom.asm:515
    push ax                                   ; 50                          ; 0xc02a4 vgarom.asm:516
    push dx                                   ; 52                          ; 0xc02a5 vgarom.asm:517
    mov dx, 003dah                            ; ba da 03                    ; 0xc02a6 vgarom.asm:518
    in AL, DX                                 ; ec                          ; 0xc02a9 vgarom.asm:519
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02aa vgarom.asm:520
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc02ad vgarom.asm:521
    out DX, AL                                ; ee                          ; 0xc02af vgarom.asm:522
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc02b0 vgarom.asm:523
    in AL, DX                                 ; ec                          ; 0xc02b3 vgarom.asm:524
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc02b4 vgarom.asm:525
    mov dx, 003dah                            ; ba da 03                    ; 0xc02b6 vgarom.asm:526
    in AL, DX                                 ; ec                          ; 0xc02b9 vgarom.asm:527
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02ba vgarom.asm:528
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc02bd vgarom.asm:529
    out DX, AL                                ; ee                          ; 0xc02bf vgarom.asm:530
    mov dx, 003dah                            ; ba da 03                    ; 0xc02c0 vgarom.asm:532
    in AL, DX                                 ; ec                          ; 0xc02c3 vgarom.asm:533
    pop dx                                    ; 5a                          ; 0xc02c4 vgarom.asm:535
    pop ax                                    ; 58                          ; 0xc02c5 vgarom.asm:536
    retn                                      ; c3                          ; 0xc02c6 vgarom.asm:538
    push ax                                   ; 50                          ; 0xc02c7 vgarom.asm:543
    push bx                                   ; 53                          ; 0xc02c8 vgarom.asm:544
    mov BL, strict byte 011h                  ; b3 11                       ; 0xc02c9 vgarom.asm:545
    call 0029fh                               ; e8 d1 ff                    ; 0xc02cb vgarom.asm:546
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc02ce vgarom.asm:547
    pop bx                                    ; 5b                          ; 0xc02d0 vgarom.asm:548
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc02d1 vgarom.asm:549
    pop ax                                    ; 58                          ; 0xc02d3 vgarom.asm:550
    retn                                      ; c3                          ; 0xc02d4 vgarom.asm:551
    push ax                                   ; 50                          ; 0xc02d5 vgarom.asm:556
    push bx                                   ; 53                          ; 0xc02d6 vgarom.asm:557
    push cx                                   ; 51                          ; 0xc02d7 vgarom.asm:558
    push dx                                   ; 52                          ; 0xc02d8 vgarom.asm:559
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc02d9 vgarom.asm:560
    mov CL, strict byte 000h                  ; b1 00                       ; 0xc02db vgarom.asm:561
    mov dx, 003dah                            ; ba da 03                    ; 0xc02dd vgarom.asm:563
    in AL, DX                                 ; ec                          ; 0xc02e0 vgarom.asm:564
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02e1 vgarom.asm:565
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc02e4 vgarom.asm:566
    out DX, AL                                ; ee                          ; 0xc02e6 vgarom.asm:567
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc02e7 vgarom.asm:568
    in AL, DX                                 ; ec                          ; 0xc02ea vgarom.asm:569
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc02eb vgarom.asm:570
    inc bx                                    ; 43                          ; 0xc02ee vgarom.asm:571
    db  0feh, 0c1h
    ; inc cl                                    ; fe c1                     ; 0xc02ef vgarom.asm:572
    cmp cl, 010h                              ; 80 f9 10                    ; 0xc02f1 vgarom.asm:573
    jne short 002ddh                          ; 75 e7                       ; 0xc02f4 vgarom.asm:574
    mov dx, 003dah                            ; ba da 03                    ; 0xc02f6 vgarom.asm:575
    in AL, DX                                 ; ec                          ; 0xc02f9 vgarom.asm:576
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc02fa vgarom.asm:577
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc02fd vgarom.asm:578
    out DX, AL                                ; ee                          ; 0xc02ff vgarom.asm:579
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0300 vgarom.asm:580
    in AL, DX                                 ; ec                          ; 0xc0303 vgarom.asm:581
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc0304 vgarom.asm:582
    mov dx, 003dah                            ; ba da 03                    ; 0xc0307 vgarom.asm:583
    in AL, DX                                 ; ec                          ; 0xc030a vgarom.asm:584
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc030b vgarom.asm:585
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc030e vgarom.asm:586
    out DX, AL                                ; ee                          ; 0xc0310 vgarom.asm:587
    mov dx, 003dah                            ; ba da 03                    ; 0xc0311 vgarom.asm:589
    in AL, DX                                 ; ec                          ; 0xc0314 vgarom.asm:590
    pop dx                                    ; 5a                          ; 0xc0315 vgarom.asm:592
    pop cx                                    ; 59                          ; 0xc0316 vgarom.asm:593
    pop bx                                    ; 5b                          ; 0xc0317 vgarom.asm:594
    pop ax                                    ; 58                          ; 0xc0318 vgarom.asm:595
    retn                                      ; c3                          ; 0xc0319 vgarom.asm:596
    push ax                                   ; 50                          ; 0xc031a vgarom.asm:601
    push dx                                   ; 52                          ; 0xc031b vgarom.asm:602
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc031c vgarom.asm:603
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc031f vgarom.asm:604
    out DX, AL                                ; ee                          ; 0xc0321 vgarom.asm:605
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc0322 vgarom.asm:606
    pop ax                                    ; 58                          ; 0xc0325 vgarom.asm:607
    push ax                                   ; 50                          ; 0xc0326 vgarom.asm:608
    db  08ah, 0c4h
    ; mov al, ah                                ; 8a c4                     ; 0xc0327 vgarom.asm:609
    out DX, AL                                ; ee                          ; 0xc0329 vgarom.asm:610
    db  08ah, 0c5h
    ; mov al, ch                                ; 8a c5                     ; 0xc032a vgarom.asm:611
    out DX, AL                                ; ee                          ; 0xc032c vgarom.asm:612
    db  08ah, 0c1h
    ; mov al, cl                                ; 8a c1                     ; 0xc032d vgarom.asm:613
    out DX, AL                                ; ee                          ; 0xc032f vgarom.asm:614
    pop dx                                    ; 5a                          ; 0xc0330 vgarom.asm:615
    pop ax                                    ; 58                          ; 0xc0331 vgarom.asm:616
    retn                                      ; c3                          ; 0xc0332 vgarom.asm:617
    push ax                                   ; 50                          ; 0xc0333 vgarom.asm:622
    push bx                                   ; 53                          ; 0xc0334 vgarom.asm:623
    push cx                                   ; 51                          ; 0xc0335 vgarom.asm:624
    push dx                                   ; 52                          ; 0xc0336 vgarom.asm:625
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc0337 vgarom.asm:626
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc033a vgarom.asm:627
    out DX, AL                                ; ee                          ; 0xc033c vgarom.asm:628
    pop dx                                    ; 5a                          ; 0xc033d vgarom.asm:629
    push dx                                   ; 52                          ; 0xc033e vgarom.asm:630
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc033f vgarom.asm:631
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc0341 vgarom.asm:632
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0344 vgarom.asm:634
    out DX, AL                                ; ee                          ; 0xc0347 vgarom.asm:635
    inc bx                                    ; 43                          ; 0xc0348 vgarom.asm:636
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0349 vgarom.asm:637
    out DX, AL                                ; ee                          ; 0xc034c vgarom.asm:638
    inc bx                                    ; 43                          ; 0xc034d vgarom.asm:639
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc034e vgarom.asm:640
    out DX, AL                                ; ee                          ; 0xc0351 vgarom.asm:641
    inc bx                                    ; 43                          ; 0xc0352 vgarom.asm:642
    dec cx                                    ; 49                          ; 0xc0353 vgarom.asm:643
    jne short 00344h                          ; 75 ee                       ; 0xc0354 vgarom.asm:644
    pop dx                                    ; 5a                          ; 0xc0356 vgarom.asm:645
    pop cx                                    ; 59                          ; 0xc0357 vgarom.asm:646
    pop bx                                    ; 5b                          ; 0xc0358 vgarom.asm:647
    pop ax                                    ; 58                          ; 0xc0359 vgarom.asm:648
    retn                                      ; c3                          ; 0xc035a vgarom.asm:649
    push ax                                   ; 50                          ; 0xc035b vgarom.asm:654
    push bx                                   ; 53                          ; 0xc035c vgarom.asm:655
    push dx                                   ; 52                          ; 0xc035d vgarom.asm:656
    mov dx, 003dah                            ; ba da 03                    ; 0xc035e vgarom.asm:657
    in AL, DX                                 ; ec                          ; 0xc0361 vgarom.asm:658
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0362 vgarom.asm:659
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc0365 vgarom.asm:660
    out DX, AL                                ; ee                          ; 0xc0367 vgarom.asm:661
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0368 vgarom.asm:662
    in AL, DX                                 ; ec                          ; 0xc036b vgarom.asm:663
    and bl, 001h                              ; 80 e3 01                    ; 0xc036c vgarom.asm:664
    jne short 00389h                          ; 75 18                       ; 0xc036f vgarom.asm:665
    and AL, strict byte 07fh                  ; 24 7f                       ; 0xc0371 vgarom.asm:666
    sal bh, 1                                 ; d0 e7                       ; 0xc0373 vgarom.asm:670
    sal bh, 1                                 ; d0 e7                       ; 0xc0375 vgarom.asm:671
    sal bh, 1                                 ; d0 e7                       ; 0xc0377 vgarom.asm:672
    sal bh, 1                                 ; d0 e7                       ; 0xc0379 vgarom.asm:673
    sal bh, 1                                 ; d0 e7                       ; 0xc037b vgarom.asm:674
    sal bh, 1                                 ; d0 e7                       ; 0xc037d vgarom.asm:675
    sal bh, 1                                 ; d0 e7                       ; 0xc037f vgarom.asm:676
    db  00ah, 0c7h
    ; or al, bh                                 ; 0a c7                     ; 0xc0381 vgarom.asm:678
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0383 vgarom.asm:679
    out DX, AL                                ; ee                          ; 0xc0386 vgarom.asm:680
    jmp short 003a3h                          ; eb 1a                       ; 0xc0387 vgarom.asm:681
    push ax                                   ; 50                          ; 0xc0389 vgarom.asm:683
    mov dx, 003dah                            ; ba da 03                    ; 0xc038a vgarom.asm:684
    in AL, DX                                 ; ec                          ; 0xc038d vgarom.asm:685
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc038e vgarom.asm:686
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc0391 vgarom.asm:687
    out DX, AL                                ; ee                          ; 0xc0393 vgarom.asm:688
    pop ax                                    ; 58                          ; 0xc0394 vgarom.asm:689
    and AL, strict byte 080h                  ; 24 80                       ; 0xc0395 vgarom.asm:690
    jne short 0039dh                          ; 75 04                       ; 0xc0397 vgarom.asm:691
    sal bh, 1                                 ; d0 e7                       ; 0xc0399 vgarom.asm:695
    sal bh, 1                                 ; d0 e7                       ; 0xc039b vgarom.asm:696
    and bh, 00fh                              ; 80 e7 0f                    ; 0xc039d vgarom.asm:699
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc03a0 vgarom.asm:700
    out DX, AL                                ; ee                          ; 0xc03a2 vgarom.asm:701
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc03a3 vgarom.asm:703
    out DX, AL                                ; ee                          ; 0xc03a5 vgarom.asm:704
    mov dx, 003dah                            ; ba da 03                    ; 0xc03a6 vgarom.asm:706
    in AL, DX                                 ; ec                          ; 0xc03a9 vgarom.asm:707
    pop dx                                    ; 5a                          ; 0xc03aa vgarom.asm:709
    pop bx                                    ; 5b                          ; 0xc03ab vgarom.asm:710
    pop ax                                    ; 58                          ; 0xc03ac vgarom.asm:711
    retn                                      ; c3                          ; 0xc03ad vgarom.asm:712
    push ax                                   ; 50                          ; 0xc03ae vgarom.asm:717
    push dx                                   ; 52                          ; 0xc03af vgarom.asm:718
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc03b0 vgarom.asm:719
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc03b3 vgarom.asm:720
    out DX, AL                                ; ee                          ; 0xc03b5 vgarom.asm:721
    pop ax                                    ; 58                          ; 0xc03b6 vgarom.asm:722
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc03b7 vgarom.asm:723
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc03b9 vgarom.asm:724
    in AL, DX                                 ; ec                          ; 0xc03bc vgarom.asm:725
    xchg al, ah                               ; 86 e0                       ; 0xc03bd vgarom.asm:726
    push ax                                   ; 50                          ; 0xc03bf vgarom.asm:727
    in AL, DX                                 ; ec                          ; 0xc03c0 vgarom.asm:728
    db  08ah, 0e8h
    ; mov ch, al                                ; 8a e8                     ; 0xc03c1 vgarom.asm:729
    in AL, DX                                 ; ec                          ; 0xc03c3 vgarom.asm:730
    db  08ah, 0c8h
    ; mov cl, al                                ; 8a c8                     ; 0xc03c4 vgarom.asm:731
    pop dx                                    ; 5a                          ; 0xc03c6 vgarom.asm:732
    pop ax                                    ; 58                          ; 0xc03c7 vgarom.asm:733
    retn                                      ; c3                          ; 0xc03c8 vgarom.asm:734
    push ax                                   ; 50                          ; 0xc03c9 vgarom.asm:739
    push bx                                   ; 53                          ; 0xc03ca vgarom.asm:740
    push cx                                   ; 51                          ; 0xc03cb vgarom.asm:741
    push dx                                   ; 52                          ; 0xc03cc vgarom.asm:742
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc03cd vgarom.asm:743
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc03d0 vgarom.asm:744
    out DX, AL                                ; ee                          ; 0xc03d2 vgarom.asm:745
    pop dx                                    ; 5a                          ; 0xc03d3 vgarom.asm:746
    push dx                                   ; 52                          ; 0xc03d4 vgarom.asm:747
    db  08bh, 0dah
    ; mov bx, dx                                ; 8b da                     ; 0xc03d5 vgarom.asm:748
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc03d7 vgarom.asm:749
    in AL, DX                                 ; ec                          ; 0xc03da vgarom.asm:751
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc03db vgarom.asm:752
    inc bx                                    ; 43                          ; 0xc03de vgarom.asm:753
    in AL, DX                                 ; ec                          ; 0xc03df vgarom.asm:754
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc03e0 vgarom.asm:755
    inc bx                                    ; 43                          ; 0xc03e3 vgarom.asm:756
    in AL, DX                                 ; ec                          ; 0xc03e4 vgarom.asm:757
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc03e5 vgarom.asm:758
    inc bx                                    ; 43                          ; 0xc03e8 vgarom.asm:759
    dec cx                                    ; 49                          ; 0xc03e9 vgarom.asm:760
    jne short 003dah                          ; 75 ee                       ; 0xc03ea vgarom.asm:761
    pop dx                                    ; 5a                          ; 0xc03ec vgarom.asm:762
    pop cx                                    ; 59                          ; 0xc03ed vgarom.asm:763
    pop bx                                    ; 5b                          ; 0xc03ee vgarom.asm:764
    pop ax                                    ; 58                          ; 0xc03ef vgarom.asm:765
    retn                                      ; c3                          ; 0xc03f0 vgarom.asm:766
    push ax                                   ; 50                          ; 0xc03f1 vgarom.asm:771
    push dx                                   ; 52                          ; 0xc03f2 vgarom.asm:772
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc03f3 vgarom.asm:773
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc03f6 vgarom.asm:774
    out DX, AL                                ; ee                          ; 0xc03f8 vgarom.asm:775
    pop dx                                    ; 5a                          ; 0xc03f9 vgarom.asm:776
    pop ax                                    ; 58                          ; 0xc03fa vgarom.asm:777
    retn                                      ; c3                          ; 0xc03fb vgarom.asm:778
    push ax                                   ; 50                          ; 0xc03fc vgarom.asm:783
    push dx                                   ; 52                          ; 0xc03fd vgarom.asm:784
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc03fe vgarom.asm:785
    in AL, DX                                 ; ec                          ; 0xc0401 vgarom.asm:786
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc0402 vgarom.asm:787
    pop dx                                    ; 5a                          ; 0xc0404 vgarom.asm:788
    pop ax                                    ; 58                          ; 0xc0405 vgarom.asm:789
    retn                                      ; c3                          ; 0xc0406 vgarom.asm:790
    push ax                                   ; 50                          ; 0xc0407 vgarom.asm:795
    push dx                                   ; 52                          ; 0xc0408 vgarom.asm:796
    mov dx, 003dah                            ; ba da 03                    ; 0xc0409 vgarom.asm:797
    in AL, DX                                 ; ec                          ; 0xc040c vgarom.asm:798
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc040d vgarom.asm:799
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc0410 vgarom.asm:800
    out DX, AL                                ; ee                          ; 0xc0412 vgarom.asm:801
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0413 vgarom.asm:802
    in AL, DX                                 ; ec                          ; 0xc0416 vgarom.asm:803
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc0417 vgarom.asm:804
    shr bl, 1                                 ; d0 eb                       ; 0xc0419 vgarom.asm:808
    shr bl, 1                                 ; d0 eb                       ; 0xc041b vgarom.asm:809
    shr bl, 1                                 ; d0 eb                       ; 0xc041d vgarom.asm:810
    shr bl, 1                                 ; d0 eb                       ; 0xc041f vgarom.asm:811
    shr bl, 1                                 ; d0 eb                       ; 0xc0421 vgarom.asm:812
    shr bl, 1                                 ; d0 eb                       ; 0xc0423 vgarom.asm:813
    shr bl, 1                                 ; d0 eb                       ; 0xc0425 vgarom.asm:814
    mov dx, 003dah                            ; ba da 03                    ; 0xc0427 vgarom.asm:816
    in AL, DX                                 ; ec                          ; 0xc042a vgarom.asm:817
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc042b vgarom.asm:818
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc042e vgarom.asm:819
    out DX, AL                                ; ee                          ; 0xc0430 vgarom.asm:820
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0431 vgarom.asm:821
    in AL, DX                                 ; ec                          ; 0xc0434 vgarom.asm:822
    db  08ah, 0f8h
    ; mov bh, al                                ; 8a f8                     ; 0xc0435 vgarom.asm:823
    and bh, 00fh                              ; 80 e7 0f                    ; 0xc0437 vgarom.asm:824
    test bl, 001h                             ; f6 c3 01                    ; 0xc043a vgarom.asm:825
    jne short 00443h                          ; 75 04                       ; 0xc043d vgarom.asm:826
    shr bh, 1                                 ; d0 ef                       ; 0xc043f vgarom.asm:830
    shr bh, 1                                 ; d0 ef                       ; 0xc0441 vgarom.asm:831
    mov dx, 003dah                            ; ba da 03                    ; 0xc0443 vgarom.asm:834
    in AL, DX                                 ; ec                          ; 0xc0446 vgarom.asm:835
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0447 vgarom.asm:836
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc044a vgarom.asm:837
    out DX, AL                                ; ee                          ; 0xc044c vgarom.asm:838
    mov dx, 003dah                            ; ba da 03                    ; 0xc044d vgarom.asm:840
    in AL, DX                                 ; ec                          ; 0xc0450 vgarom.asm:841
    pop dx                                    ; 5a                          ; 0xc0451 vgarom.asm:843
    pop ax                                    ; 58                          ; 0xc0452 vgarom.asm:844
    retn                                      ; c3                          ; 0xc0453 vgarom.asm:845
    push ax                                   ; 50                          ; 0xc0454 vgarom.asm:850
    push dx                                   ; 52                          ; 0xc0455 vgarom.asm:851
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc0456 vgarom.asm:852
    db  08ah, 0e3h
    ; mov ah, bl                                ; 8a e3                     ; 0xc0459 vgarom.asm:853
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc045b vgarom.asm:854
    out DX, ax                                ; ef                          ; 0xc045d vgarom.asm:855
    pop dx                                    ; 5a                          ; 0xc045e vgarom.asm:856
    pop ax                                    ; 58                          ; 0xc045f vgarom.asm:857
    retn                                      ; c3                          ; 0xc0460 vgarom.asm:858
    push DS                                   ; 1e                          ; 0xc0461 vgarom.asm:863
    push ax                                   ; 50                          ; 0xc0462 vgarom.asm:864
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0463 vgarom.asm:865
    mov ds, ax                                ; 8e d8                       ; 0xc0466 vgarom.asm:866
    db  032h, 0edh
    ; xor ch, ch                                ; 32 ed                     ; 0xc0468 vgarom.asm:867
    mov bx, 00088h                            ; bb 88 00                    ; 0xc046a vgarom.asm:868
    mov cl, byte [bx]                         ; 8a 0f                       ; 0xc046d vgarom.asm:869
    and cl, 00fh                              ; 80 e1 0f                    ; 0xc046f vgarom.asm:870
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc0472 vgarom.asm:871
    mov ax, word [bx]                         ; 8b 07                       ; 0xc0475 vgarom.asm:872
    mov bx, strict word 00003h                ; bb 03 00                    ; 0xc0477 vgarom.asm:873
    cmp ax, 003b4h                            ; 3d b4 03                    ; 0xc047a vgarom.asm:874
    jne short 00481h                          ; 75 02                       ; 0xc047d vgarom.asm:875
    mov BH, strict byte 001h                  ; b7 01                       ; 0xc047f vgarom.asm:876
    pop ax                                    ; 58                          ; 0xc0481 vgarom.asm:878
    pop DS                                    ; 1f                          ; 0xc0482 vgarom.asm:879
    retn                                      ; c3                          ; 0xc0483 vgarom.asm:880
    push DS                                   ; 1e                          ; 0xc0484 vgarom.asm:888
    push bx                                   ; 53                          ; 0xc0485 vgarom.asm:889
    push dx                                   ; 52                          ; 0xc0486 vgarom.asm:890
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc0487 vgarom.asm:891
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0489 vgarom.asm:892
    mov ds, ax                                ; 8e d8                       ; 0xc048c vgarom.asm:893
    mov bx, 00089h                            ; bb 89 00                    ; 0xc048e vgarom.asm:894
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0491 vgarom.asm:895
    mov bx, 00088h                            ; bb 88 00                    ; 0xc0493 vgarom.asm:896
    mov ah, byte [bx]                         ; 8a 27                       ; 0xc0496 vgarom.asm:897
    cmp dl, 001h                              ; 80 fa 01                    ; 0xc0498 vgarom.asm:898
    je short 004b2h                           ; 74 15                       ; 0xc049b vgarom.asm:899
    jc short 004bch                           ; 72 1d                       ; 0xc049d vgarom.asm:900
    cmp dl, 002h                              ; 80 fa 02                    ; 0xc049f vgarom.asm:901
    je short 004a6h                           ; 74 02                       ; 0xc04a2 vgarom.asm:902
    jmp short 004d0h                          ; eb 2a                       ; 0xc04a4 vgarom.asm:912
    and AL, strict byte 07fh                  ; 24 7f                       ; 0xc04a6 vgarom.asm:918
    or AL, strict byte 010h                   ; 0c 10                       ; 0xc04a8 vgarom.asm:919
    and ah, 0f0h                              ; 80 e4 f0                    ; 0xc04aa vgarom.asm:920
    or ah, 009h                               ; 80 cc 09                    ; 0xc04ad vgarom.asm:921
    jne short 004c6h                          ; 75 14                       ; 0xc04b0 vgarom.asm:922
    and AL, strict byte 06fh                  ; 24 6f                       ; 0xc04b2 vgarom.asm:928
    and ah, 0f0h                              ; 80 e4 f0                    ; 0xc04b4 vgarom.asm:929
    or ah, 009h                               ; 80 cc 09                    ; 0xc04b7 vgarom.asm:930
    jne short 004c6h                          ; 75 0a                       ; 0xc04ba vgarom.asm:931
    and AL, strict byte 0efh                  ; 24 ef                       ; 0xc04bc vgarom.asm:937
    or AL, strict byte 080h                   ; 0c 80                       ; 0xc04be vgarom.asm:938
    and ah, 0f0h                              ; 80 e4 f0                    ; 0xc04c0 vgarom.asm:939
    or ah, 008h                               ; 80 cc 08                    ; 0xc04c3 vgarom.asm:940
    mov bx, 00089h                            ; bb 89 00                    ; 0xc04c6 vgarom.asm:942
    mov byte [bx], al                         ; 88 07                       ; 0xc04c9 vgarom.asm:943
    mov bx, 00088h                            ; bb 88 00                    ; 0xc04cb vgarom.asm:944
    mov byte [bx], ah                         ; 88 27                       ; 0xc04ce vgarom.asm:945
    mov ax, 01212h                            ; b8 12 12                    ; 0xc04d0 vgarom.asm:947
    pop dx                                    ; 5a                          ; 0xc04d3 vgarom.asm:948
    pop bx                                    ; 5b                          ; 0xc04d4 vgarom.asm:949
    pop DS                                    ; 1f                          ; 0xc04d5 vgarom.asm:950
    retn                                      ; c3                          ; 0xc04d6 vgarom.asm:951
    push DS                                   ; 1e                          ; 0xc04d7 vgarom.asm:960
    push bx                                   ; 53                          ; 0xc04d8 vgarom.asm:961
    push dx                                   ; 52                          ; 0xc04d9 vgarom.asm:962
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc04da vgarom.asm:963
    and dl, 001h                              ; 80 e2 01                    ; 0xc04dc vgarom.asm:964
    sal dl, 1                                 ; d0 e2                       ; 0xc04df vgarom.asm:968
    sal dl, 1                                 ; d0 e2                       ; 0xc04e1 vgarom.asm:969
    sal dl, 1                                 ; d0 e2                       ; 0xc04e3 vgarom.asm:970
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc04e5 vgarom.asm:972
    mov ds, ax                                ; 8e d8                       ; 0xc04e8 vgarom.asm:973
    mov bx, 00089h                            ; bb 89 00                    ; 0xc04ea vgarom.asm:974
    mov al, byte [bx]                         ; 8a 07                       ; 0xc04ed vgarom.asm:975
    and AL, strict byte 0f7h                  ; 24 f7                       ; 0xc04ef vgarom.asm:976
    db  00ah, 0c2h
    ; or al, dl                                 ; 0a c2                     ; 0xc04f1 vgarom.asm:977
    mov byte [bx], al                         ; 88 07                       ; 0xc04f3 vgarom.asm:978
    mov ax, 01212h                            ; b8 12 12                    ; 0xc04f5 vgarom.asm:979
    pop dx                                    ; 5a                          ; 0xc04f8 vgarom.asm:980
    pop bx                                    ; 5b                          ; 0xc04f9 vgarom.asm:981
    pop DS                                    ; 1f                          ; 0xc04fa vgarom.asm:982
    retn                                      ; c3                          ; 0xc04fb vgarom.asm:983
    push bx                                   ; 53                          ; 0xc04fc vgarom.asm:987
    push dx                                   ; 52                          ; 0xc04fd vgarom.asm:988
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc04fe vgarom.asm:989
    and bl, 001h                              ; 80 e3 01                    ; 0xc0500 vgarom.asm:990
    xor bl, 001h                              ; 80 f3 01                    ; 0xc0503 vgarom.asm:991
    sal bl, 1                                 ; d0 e3                       ; 0xc0506 vgarom.asm:992
    mov dx, 003cch                            ; ba cc 03                    ; 0xc0508 vgarom.asm:993
    in AL, DX                                 ; ec                          ; 0xc050b vgarom.asm:994
    and AL, strict byte 0fdh                  ; 24 fd                       ; 0xc050c vgarom.asm:995
    db  00ah, 0c3h
    ; or al, bl                                 ; 0a c3                     ; 0xc050e vgarom.asm:996
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc0510 vgarom.asm:997
    out DX, AL                                ; ee                          ; 0xc0513 vgarom.asm:998
    mov ax, 01212h                            ; b8 12 12                    ; 0xc0514 vgarom.asm:999
    pop dx                                    ; 5a                          ; 0xc0517 vgarom.asm:1000
    pop bx                                    ; 5b                          ; 0xc0518 vgarom.asm:1001
    retn                                      ; c3                          ; 0xc0519 vgarom.asm:1002
    push DS                                   ; 1e                          ; 0xc051a vgarom.asm:1006
    push bx                                   ; 53                          ; 0xc051b vgarom.asm:1007
    push dx                                   ; 52                          ; 0xc051c vgarom.asm:1008
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc051d vgarom.asm:1009
    and dl, 001h                              ; 80 e2 01                    ; 0xc051f vgarom.asm:1010
    xor dl, 001h                              ; 80 f2 01                    ; 0xc0522 vgarom.asm:1011
    sal dl, 1                                 ; d0 e2                       ; 0xc0525 vgarom.asm:1012
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0527 vgarom.asm:1013
    mov ds, ax                                ; 8e d8                       ; 0xc052a vgarom.asm:1014
    mov bx, 00089h                            ; bb 89 00                    ; 0xc052c vgarom.asm:1015
    mov al, byte [bx]                         ; 8a 07                       ; 0xc052f vgarom.asm:1016
    and AL, strict byte 0fdh                  ; 24 fd                       ; 0xc0531 vgarom.asm:1017
    db  00ah, 0c2h
    ; or al, dl                                 ; 0a c2                     ; 0xc0533 vgarom.asm:1018
    mov byte [bx], al                         ; 88 07                       ; 0xc0535 vgarom.asm:1019
    mov ax, 01212h                            ; b8 12 12                    ; 0xc0537 vgarom.asm:1020
    pop dx                                    ; 5a                          ; 0xc053a vgarom.asm:1021
    pop bx                                    ; 5b                          ; 0xc053b vgarom.asm:1022
    pop DS                                    ; 1f                          ; 0xc053c vgarom.asm:1023
    retn                                      ; c3                          ; 0xc053d vgarom.asm:1024
    push DS                                   ; 1e                          ; 0xc053e vgarom.asm:1028
    push bx                                   ; 53                          ; 0xc053f vgarom.asm:1029
    push dx                                   ; 52                          ; 0xc0540 vgarom.asm:1030
    db  08ah, 0d0h
    ; mov dl, al                                ; 8a d0                     ; 0xc0541 vgarom.asm:1031
    and dl, 001h                              ; 80 e2 01                    ; 0xc0543 vgarom.asm:1032
    xor dl, 001h                              ; 80 f2 01                    ; 0xc0546 vgarom.asm:1033
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0549 vgarom.asm:1034
    mov ds, ax                                ; 8e d8                       ; 0xc054c vgarom.asm:1035
    mov bx, 00089h                            ; bb 89 00                    ; 0xc054e vgarom.asm:1036
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0551 vgarom.asm:1037
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc0553 vgarom.asm:1038
    db  00ah, 0c2h
    ; or al, dl                                 ; 0a c2                     ; 0xc0555 vgarom.asm:1039
    mov byte [bx], al                         ; 88 07                       ; 0xc0557 vgarom.asm:1040
    mov ax, 01212h                            ; b8 12 12                    ; 0xc0559 vgarom.asm:1041
    pop dx                                    ; 5a                          ; 0xc055c vgarom.asm:1042
    pop bx                                    ; 5b                          ; 0xc055d vgarom.asm:1043
    pop DS                                    ; 1f                          ; 0xc055e vgarom.asm:1044
    retn                                      ; c3                          ; 0xc055f vgarom.asm:1045
    cmp AL, strict byte 000h                  ; 3c 00                       ; 0xc0560 vgarom.asm:1050
    je short 00569h                           ; 74 05                       ; 0xc0562 vgarom.asm:1051
    cmp AL, strict byte 001h                  ; 3c 01                       ; 0xc0564 vgarom.asm:1052
    je short 0057eh                           ; 74 16                       ; 0xc0566 vgarom.asm:1053
    retn                                      ; c3                          ; 0xc0568 vgarom.asm:1057
    push DS                                   ; 1e                          ; 0xc0569 vgarom.asm:1059
    push ax                                   ; 50                          ; 0xc056a vgarom.asm:1060
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc056b vgarom.asm:1061
    mov ds, ax                                ; 8e d8                       ; 0xc056e vgarom.asm:1062
    mov bx, 0008ah                            ; bb 8a 00                    ; 0xc0570 vgarom.asm:1063
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0573 vgarom.asm:1064
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc0575 vgarom.asm:1065
    db  032h, 0ffh
    ; xor bh, bh                                ; 32 ff                     ; 0xc0577 vgarom.asm:1066
    pop ax                                    ; 58                          ; 0xc0579 vgarom.asm:1067
    db  08ah, 0c4h
    ; mov al, ah                                ; 8a c4                     ; 0xc057a vgarom.asm:1068
    pop DS                                    ; 1f                          ; 0xc057c vgarom.asm:1069
    retn                                      ; c3                          ; 0xc057d vgarom.asm:1070
    push DS                                   ; 1e                          ; 0xc057e vgarom.asm:1072
    push ax                                   ; 50                          ; 0xc057f vgarom.asm:1073
    push bx                                   ; 53                          ; 0xc0580 vgarom.asm:1074
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0581 vgarom.asm:1075
    mov ds, ax                                ; 8e d8                       ; 0xc0584 vgarom.asm:1076
    db  08bh, 0c3h
    ; mov ax, bx                                ; 8b c3                     ; 0xc0586 vgarom.asm:1077
    mov bx, 0008ah                            ; bb 8a 00                    ; 0xc0588 vgarom.asm:1078
    mov byte [bx], al                         ; 88 07                       ; 0xc058b vgarom.asm:1079
    pop bx                                    ; 5b                          ; 0xc058d vgarom.asm:1089
    pop ax                                    ; 58                          ; 0xc058e vgarom.asm:1090
    db  08ah, 0c4h
    ; mov al, ah                                ; 8a c4                     ; 0xc058f vgarom.asm:1091
    pop DS                                    ; 1f                          ; 0xc0591 vgarom.asm:1092
    retn                                      ; c3                          ; 0xc0592 vgarom.asm:1093
    times 0xd db 0
  ; disGetNextSymbol 0xc05a0 LB 0x3af -> off=0x0 cb=0000000000000007 uValue=00000000000c05a0 'do_out_dx_ax'
do_out_dx_ax:                                ; 0xc05a0 LB 0x7
    xchg ah, al                               ; 86 c4                       ; 0xc05a0 vberom.asm:69
    out DX, AL                                ; ee                          ; 0xc05a2 vberom.asm:70
    xchg ah, al                               ; 86 c4                       ; 0xc05a3 vberom.asm:71
    out DX, AL                                ; ee                          ; 0xc05a5 vberom.asm:72
    retn                                      ; c3                          ; 0xc05a6 vberom.asm:73
  ; disGetNextSymbol 0xc05a7 LB 0x3a8 -> off=0x0 cb=0000000000000043 uValue=00000000000c05a7 'do_in_ax_dx'
do_in_ax_dx:                                 ; 0xc05a7 LB 0x43
    in AL, DX                                 ; ec                          ; 0xc05a7 vberom.asm:76
    xchg ah, al                               ; 86 c4                       ; 0xc05a8 vberom.asm:77
    in AL, DX                                 ; ec                          ; 0xc05aa vberom.asm:78
    retn                                      ; c3                          ; 0xc05ab vberom.asm:79
    push ax                                   ; 50                          ; 0xc05ac vberom.asm:90
    push dx                                   ; 52                          ; 0xc05ad vberom.asm:91
    mov dx, 003dah                            ; ba da 03                    ; 0xc05ae vberom.asm:92
    in AL, DX                                 ; ec                          ; 0xc05b1 vberom.asm:94
    test AL, strict byte 008h                 ; a8 08                       ; 0xc05b2 vberom.asm:95
    je short 005b1h                           ; 74 fb                       ; 0xc05b4 vberom.asm:96
    pop dx                                    ; 5a                          ; 0xc05b6 vberom.asm:97
    pop ax                                    ; 58                          ; 0xc05b7 vberom.asm:98
    retn                                      ; c3                          ; 0xc05b8 vberom.asm:99
    push ax                                   ; 50                          ; 0xc05b9 vberom.asm:102
    push dx                                   ; 52                          ; 0xc05ba vberom.asm:103
    mov dx, 003dah                            ; ba da 03                    ; 0xc05bb vberom.asm:104
    in AL, DX                                 ; ec                          ; 0xc05be vberom.asm:106
    test AL, strict byte 008h                 ; a8 08                       ; 0xc05bf vberom.asm:107
    jne short 005beh                          ; 75 fb                       ; 0xc05c1 vberom.asm:108
    pop dx                                    ; 5a                          ; 0xc05c3 vberom.asm:109
    pop ax                                    ; 58                          ; 0xc05c4 vberom.asm:110
    retn                                      ; c3                          ; 0xc05c5 vberom.asm:111
    push dx                                   ; 52                          ; 0xc05c6 vberom.asm:116
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc05c7 vberom.asm:117
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc05ca vberom.asm:118
    call 005a0h                               ; e8 d0 ff                    ; 0xc05cd vberom.asm:119
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc05d0 vberom.asm:120
    call 005a7h                               ; e8 d1 ff                    ; 0xc05d3 vberom.asm:121
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc05d6 vberom.asm:122
    jbe short 005e8h                          ; 76 0e                       ; 0xc05d8 vberom.asm:123
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc05da vberom.asm:124
    shr ah, 1                                 ; d0 ec                       ; 0xc05dc vberom.asm:128
    shr ah, 1                                 ; d0 ec                       ; 0xc05de vberom.asm:129
    shr ah, 1                                 ; d0 ec                       ; 0xc05e0 vberom.asm:130
    test AL, strict byte 007h                 ; a8 07                       ; 0xc05e2 vberom.asm:132
    je short 005e8h                           ; 74 02                       ; 0xc05e4 vberom.asm:133
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc05e6 vberom.asm:134
    pop dx                                    ; 5a                          ; 0xc05e8 vberom.asm:136
    retn                                      ; c3                          ; 0xc05e9 vberom.asm:137
  ; disGetNextSymbol 0xc05ea LB 0x365 -> off=0x0 cb=0000000000000026 uValue=00000000000c05ea '_dispi_get_max_bpp'
_dispi_get_max_bpp:                          ; 0xc05ea LB 0x26
    push dx                                   ; 52                          ; 0xc05ea vberom.asm:142
    push bx                                   ; 53                          ; 0xc05eb vberom.asm:143
    call 00624h                               ; e8 35 00                    ; 0xc05ec vberom.asm:144
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc05ef vberom.asm:145
    or ax, strict byte 00002h                 ; 83 c8 02                    ; 0xc05f1 vberom.asm:146
    call 00610h                               ; e8 19 00                    ; 0xc05f4 vberom.asm:147
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc05f7 vberom.asm:148
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc05fa vberom.asm:149
    call 005a0h                               ; e8 a0 ff                    ; 0xc05fd vberom.asm:150
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0600 vberom.asm:151
    call 005a7h                               ; e8 a1 ff                    ; 0xc0603 vberom.asm:152
    push ax                                   ; 50                          ; 0xc0606 vberom.asm:153
    db  08bh, 0c3h
    ; mov ax, bx                                ; 8b c3                     ; 0xc0607 vberom.asm:154
    call 00610h                               ; e8 04 00                    ; 0xc0609 vberom.asm:155
    pop ax                                    ; 58                          ; 0xc060c vberom.asm:156
    pop bx                                    ; 5b                          ; 0xc060d vberom.asm:157
    pop dx                                    ; 5a                          ; 0xc060e vberom.asm:158
    retn                                      ; c3                          ; 0xc060f vberom.asm:159
  ; disGetNextSymbol 0xc0610 LB 0x33f -> off=0x0 cb=0000000000000026 uValue=00000000000c0610 'dispi_set_enable_'
dispi_set_enable_:                           ; 0xc0610 LB 0x26
    push dx                                   ; 52                          ; 0xc0610 vberom.asm:162
    push ax                                   ; 50                          ; 0xc0611 vberom.asm:163
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0612 vberom.asm:164
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc0615 vberom.asm:165
    call 005a0h                               ; e8 85 ff                    ; 0xc0618 vberom.asm:166
    pop ax                                    ; 58                          ; 0xc061b vberom.asm:167
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc061c vberom.asm:168
    call 005a0h                               ; e8 7e ff                    ; 0xc061f vberom.asm:169
    pop dx                                    ; 5a                          ; 0xc0622 vberom.asm:170
    retn                                      ; c3                          ; 0xc0623 vberom.asm:171
    push dx                                   ; 52                          ; 0xc0624 vberom.asm:174
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0625 vberom.asm:175
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc0628 vberom.asm:176
    call 005a0h                               ; e8 72 ff                    ; 0xc062b vberom.asm:177
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc062e vberom.asm:178
    call 005a7h                               ; e8 73 ff                    ; 0xc0631 vberom.asm:179
    pop dx                                    ; 5a                          ; 0xc0634 vberom.asm:180
    retn                                      ; c3                          ; 0xc0635 vberom.asm:181
  ; disGetNextSymbol 0xc0636 LB 0x319 -> off=0x0 cb=0000000000000026 uValue=00000000000c0636 'dispi_set_bank_'
dispi_set_bank_:                             ; 0xc0636 LB 0x26
    push dx                                   ; 52                          ; 0xc0636 vberom.asm:184
    push ax                                   ; 50                          ; 0xc0637 vberom.asm:185
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0638 vberom.asm:186
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc063b vberom.asm:187
    call 005a0h                               ; e8 5f ff                    ; 0xc063e vberom.asm:188
    pop ax                                    ; 58                          ; 0xc0641 vberom.asm:189
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0642 vberom.asm:190
    call 005a0h                               ; e8 58 ff                    ; 0xc0645 vberom.asm:191
    pop dx                                    ; 5a                          ; 0xc0648 vberom.asm:192
    retn                                      ; c3                          ; 0xc0649 vberom.asm:193
    push dx                                   ; 52                          ; 0xc064a vberom.asm:196
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc064b vberom.asm:197
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc064e vberom.asm:198
    call 005a0h                               ; e8 4c ff                    ; 0xc0651 vberom.asm:199
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0654 vberom.asm:200
    call 005a7h                               ; e8 4d ff                    ; 0xc0657 vberom.asm:201
    pop dx                                    ; 5a                          ; 0xc065a vberom.asm:202
    retn                                      ; c3                          ; 0xc065b vberom.asm:203
  ; disGetNextSymbol 0xc065c LB 0x2f3 -> off=0x0 cb=00000000000000ac uValue=00000000000c065c '_dispi_set_bank_farcall'
_dispi_set_bank_farcall:                     ; 0xc065c LB 0xac
    cmp bx, 00100h                            ; 81 fb 00 01                 ; 0xc065c vberom.asm:206
    je short 00686h                           ; 74 24                       ; 0xc0660 vberom.asm:207
    db  00bh, 0dbh
    ; or bx, bx                                 ; 0b db                     ; 0xc0662 vberom.asm:208
    jne short 00698h                          ; 75 32                       ; 0xc0664 vberom.asm:209
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc0666 vberom.asm:210
    push dx                                   ; 52                          ; 0xc0668 vberom.asm:211
    push ax                                   ; 50                          ; 0xc0669 vberom.asm:212
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc066a vberom.asm:213
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc066d vberom.asm:214
    call 005a0h                               ; e8 2d ff                    ; 0xc0670 vberom.asm:215
    pop ax                                    ; 58                          ; 0xc0673 vberom.asm:216
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0674 vberom.asm:217
    call 005a0h                               ; e8 26 ff                    ; 0xc0677 vberom.asm:218
    call 005a7h                               ; e8 2a ff                    ; 0xc067a vberom.asm:219
    pop dx                                    ; 5a                          ; 0xc067d vberom.asm:220
    db  03bh, 0d0h
    ; cmp dx, ax                                ; 3b d0                     ; 0xc067e vberom.asm:221
    jne short 00698h                          ; 75 16                       ; 0xc0680 vberom.asm:222
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0682 vberom.asm:223
    retf                                      ; cb                          ; 0xc0685 vberom.asm:224
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0686 vberom.asm:226
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0689 vberom.asm:227
    call 005a0h                               ; e8 11 ff                    ; 0xc068c vberom.asm:228
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc068f vberom.asm:229
    call 005a7h                               ; e8 12 ff                    ; 0xc0692 vberom.asm:230
    db  08bh, 0d0h
    ; mov dx, ax                                ; 8b d0                     ; 0xc0695 vberom.asm:231
    retf                                      ; cb                          ; 0xc0697 vberom.asm:232
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc0698 vberom.asm:234
    retf                                      ; cb                          ; 0xc069b vberom.asm:235
    push dx                                   ; 52                          ; 0xc069c vberom.asm:238
    push ax                                   ; 50                          ; 0xc069d vberom.asm:239
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc069e vberom.asm:240
    mov ax, strict word 00008h                ; b8 08 00                    ; 0xc06a1 vberom.asm:241
    call 005a0h                               ; e8 f9 fe                    ; 0xc06a4 vberom.asm:242
    pop ax                                    ; 58                          ; 0xc06a7 vberom.asm:243
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc06a8 vberom.asm:244
    call 005a0h                               ; e8 f2 fe                    ; 0xc06ab vberom.asm:245
    pop dx                                    ; 5a                          ; 0xc06ae vberom.asm:246
    retn                                      ; c3                          ; 0xc06af vberom.asm:247
    push dx                                   ; 52                          ; 0xc06b0 vberom.asm:250
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc06b1 vberom.asm:251
    mov ax, strict word 00008h                ; b8 08 00                    ; 0xc06b4 vberom.asm:252
    call 005a0h                               ; e8 e6 fe                    ; 0xc06b7 vberom.asm:253
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc06ba vberom.asm:254
    call 005a7h                               ; e8 e7 fe                    ; 0xc06bd vberom.asm:255
    pop dx                                    ; 5a                          ; 0xc06c0 vberom.asm:256
    retn                                      ; c3                          ; 0xc06c1 vberom.asm:257
    push dx                                   ; 52                          ; 0xc06c2 vberom.asm:260
    push ax                                   ; 50                          ; 0xc06c3 vberom.asm:261
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc06c4 vberom.asm:262
    mov ax, strict word 00009h                ; b8 09 00                    ; 0xc06c7 vberom.asm:263
    call 005a0h                               ; e8 d3 fe                    ; 0xc06ca vberom.asm:264
    pop ax                                    ; 58                          ; 0xc06cd vberom.asm:265
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc06ce vberom.asm:266
    call 005a0h                               ; e8 cc fe                    ; 0xc06d1 vberom.asm:267
    pop dx                                    ; 5a                          ; 0xc06d4 vberom.asm:268
    retn                                      ; c3                          ; 0xc06d5 vberom.asm:269
    push dx                                   ; 52                          ; 0xc06d6 vberom.asm:272
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc06d7 vberom.asm:273
    mov ax, strict word 00009h                ; b8 09 00                    ; 0xc06da vberom.asm:274
    call 005a0h                               ; e8 c0 fe                    ; 0xc06dd vberom.asm:275
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc06e0 vberom.asm:276
    call 005a7h                               ; e8 c1 fe                    ; 0xc06e3 vberom.asm:277
    pop dx                                    ; 5a                          ; 0xc06e6 vberom.asm:278
    retn                                      ; c3                          ; 0xc06e7 vberom.asm:279
    push ax                                   ; 50                          ; 0xc06e8 vberom.asm:282
    push bx                                   ; 53                          ; 0xc06e9 vberom.asm:283
    push dx                                   ; 52                          ; 0xc06ea vberom.asm:284
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc06eb vberom.asm:285
    call 005c6h                               ; e8 d6 fe                    ; 0xc06ed vberom.asm:286
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc06f0 vberom.asm:287
    jnbe short 006f6h                         ; 77 02                       ; 0xc06f2 vberom.asm:288
    shr bx, 1                                 ; d1 eb                       ; 0xc06f4 vberom.asm:289
    shr bx, 1                                 ; d1 eb                       ; 0xc06f6 vberom.asm:294
    shr bx, 1                                 ; d1 eb                       ; 0xc06f8 vberom.asm:295
    shr bx, 1                                 ; d1 eb                       ; 0xc06fa vberom.asm:296
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc06fc vberom.asm:298
    db  08ah, 0e3h
    ; mov ah, bl                                ; 8a e3                     ; 0xc06ff vberom.asm:299
    mov AL, strict byte 013h                  ; b0 13                       ; 0xc0701 vberom.asm:300
    out DX, ax                                ; ef                          ; 0xc0703 vberom.asm:301
    pop dx                                    ; 5a                          ; 0xc0704 vberom.asm:302
    pop bx                                    ; 5b                          ; 0xc0705 vberom.asm:303
    pop ax                                    ; 58                          ; 0xc0706 vberom.asm:304
    retn                                      ; c3                          ; 0xc0707 vberom.asm:305
  ; disGetNextSymbol 0xc0708 LB 0x247 -> off=0x0 cb=00000000000000f0 uValue=00000000000c0708 '_vga_compat_setup'
_vga_compat_setup:                           ; 0xc0708 LB 0xf0
    push ax                                   ; 50                          ; 0xc0708 vberom.asm:308
    push dx                                   ; 52                          ; 0xc0709 vberom.asm:309
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc070a vberom.asm:312
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc070d vberom.asm:313
    call 005a0h                               ; e8 8d fe                    ; 0xc0710 vberom.asm:314
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc0713 vberom.asm:315
    call 005a7h                               ; e8 8e fe                    ; 0xc0716 vberom.asm:316
    push ax                                   ; 50                          ; 0xc0719 vberom.asm:317
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc071a vberom.asm:318
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc071d vberom.asm:319
    out DX, ax                                ; ef                          ; 0xc0720 vberom.asm:320
    pop ax                                    ; 58                          ; 0xc0721 vberom.asm:321
    push ax                                   ; 50                          ; 0xc0722 vberom.asm:322
    shr ax, 1                                 ; d1 e8                       ; 0xc0723 vberom.asm:326
    shr ax, 1                                 ; d1 e8                       ; 0xc0725 vberom.asm:327
    shr ax, 1                                 ; d1 e8                       ; 0xc0727 vberom.asm:328
    dec ax                                    ; 48                          ; 0xc0729 vberom.asm:330
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc072a vberom.asm:331
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc072c vberom.asm:332
    out DX, ax                                ; ef                          ; 0xc072e vberom.asm:333
    pop ax                                    ; 58                          ; 0xc072f vberom.asm:334
    call 006e8h                               ; e8 b5 ff                    ; 0xc0730 vberom.asm:335
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc0733 vberom.asm:338
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc0736 vberom.asm:339
    call 005a0h                               ; e8 64 fe                    ; 0xc0739 vberom.asm:340
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc073c vberom.asm:341
    call 005a7h                               ; e8 65 fe                    ; 0xc073f vberom.asm:342
    dec ax                                    ; 48                          ; 0xc0742 vberom.asm:343
    push ax                                   ; 50                          ; 0xc0743 vberom.asm:344
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc0744 vberom.asm:345
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc0747 vberom.asm:346
    mov AL, strict byte 012h                  ; b0 12                       ; 0xc0749 vberom.asm:347
    out DX, ax                                ; ef                          ; 0xc074b vberom.asm:348
    pop ax                                    ; 58                          ; 0xc074c vberom.asm:349
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc074d vberom.asm:350
    out DX, AL                                ; ee                          ; 0xc074f vberom.asm:351
    inc dx                                    ; 42                          ; 0xc0750 vberom.asm:352
    in AL, DX                                 ; ec                          ; 0xc0751 vberom.asm:353
    and AL, strict byte 0bdh                  ; 24 bd                       ; 0xc0752 vberom.asm:354
    test ah, 001h                             ; f6 c4 01                    ; 0xc0754 vberom.asm:355
    je short 0075bh                           ; 74 02                       ; 0xc0757 vberom.asm:356
    or AL, strict byte 002h                   ; 0c 02                       ; 0xc0759 vberom.asm:357
    test ah, 002h                             ; f6 c4 02                    ; 0xc075b vberom.asm:359
    je short 00762h                           ; 74 02                       ; 0xc075e vberom.asm:360
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc0760 vberom.asm:361
    out DX, AL                                ; ee                          ; 0xc0762 vberom.asm:363
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc0763 vberom.asm:366
    mov ax, strict word 00009h                ; b8 09 00                    ; 0xc0766 vberom.asm:367
    out DX, AL                                ; ee                          ; 0xc0769 vberom.asm:368
    mov dx, 003d5h                            ; ba d5 03                    ; 0xc076a vberom.asm:369
    in AL, DX                                 ; ec                          ; 0xc076d vberom.asm:370
    and AL, strict byte 060h                  ; 24 60                       ; 0xc076e vberom.asm:371
    out DX, AL                                ; ee                          ; 0xc0770 vberom.asm:372
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc0771 vberom.asm:373
    mov AL, strict byte 017h                  ; b0 17                       ; 0xc0774 vberom.asm:374
    out DX, AL                                ; ee                          ; 0xc0776 vberom.asm:375
    mov dx, 003d5h                            ; ba d5 03                    ; 0xc0777 vberom.asm:376
    in AL, DX                                 ; ec                          ; 0xc077a vberom.asm:377
    or AL, strict byte 003h                   ; 0c 03                       ; 0xc077b vberom.asm:378
    out DX, AL                                ; ee                          ; 0xc077d vberom.asm:379
    mov dx, 003dah                            ; ba da 03                    ; 0xc077e vberom.asm:380
    in AL, DX                                 ; ec                          ; 0xc0781 vberom.asm:381
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc0782 vberom.asm:382
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc0785 vberom.asm:383
    out DX, AL                                ; ee                          ; 0xc0787 vberom.asm:384
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc0788 vberom.asm:385
    in AL, DX                                 ; ec                          ; 0xc078b vberom.asm:386
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc078c vberom.asm:387
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc078e vberom.asm:388
    out DX, AL                                ; ee                          ; 0xc0791 vberom.asm:389
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc0792 vberom.asm:390
    out DX, AL                                ; ee                          ; 0xc0794 vberom.asm:391
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0795 vberom.asm:392
    mov ax, 00506h                            ; b8 06 05                    ; 0xc0798 vberom.asm:393
    out DX, ax                                ; ef                          ; 0xc079b vberom.asm:394
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc079c vberom.asm:395
    mov ax, 00f02h                            ; b8 02 0f                    ; 0xc079f vberom.asm:396
    out DX, ax                                ; ef                          ; 0xc07a2 vberom.asm:397
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc07a3 vberom.asm:400
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc07a6 vberom.asm:401
    call 005a0h                               ; e8 f4 fd                    ; 0xc07a9 vberom.asm:402
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc07ac vberom.asm:403
    call 005a7h                               ; e8 f5 fd                    ; 0xc07af vberom.asm:404
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc07b2 vberom.asm:405
    jc short 007f6h                           ; 72 40                       ; 0xc07b4 vberom.asm:406
    mov dx, 003d4h                            ; ba d4 03                    ; 0xc07b6 vberom.asm:407
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc07b9 vberom.asm:408
    out DX, AL                                ; ee                          ; 0xc07bb vberom.asm:409
    mov dx, 003d5h                            ; ba d5 03                    ; 0xc07bc vberom.asm:410
    in AL, DX                                 ; ec                          ; 0xc07bf vberom.asm:411
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc07c0 vberom.asm:412
    out DX, AL                                ; ee                          ; 0xc07c2 vberom.asm:413
    mov dx, 003dah                            ; ba da 03                    ; 0xc07c3 vberom.asm:414
    in AL, DX                                 ; ec                          ; 0xc07c6 vberom.asm:415
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc07c7 vberom.asm:416
    mov AL, strict byte 010h                  ; b0 10                       ; 0xc07ca vberom.asm:417
    out DX, AL                                ; ee                          ; 0xc07cc vberom.asm:418
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc07cd vberom.asm:419
    in AL, DX                                 ; ec                          ; 0xc07d0 vberom.asm:420
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc07d1 vberom.asm:421
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc07d3 vberom.asm:422
    out DX, AL                                ; ee                          ; 0xc07d6 vberom.asm:423
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc07d7 vberom.asm:424
    out DX, AL                                ; ee                          ; 0xc07d9 vberom.asm:425
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc07da vberom.asm:426
    mov AL, strict byte 004h                  ; b0 04                       ; 0xc07dd vberom.asm:427
    out DX, AL                                ; ee                          ; 0xc07df vberom.asm:428
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc07e0 vberom.asm:429
    in AL, DX                                 ; ec                          ; 0xc07e3 vberom.asm:430
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc07e4 vberom.asm:431
    out DX, AL                                ; ee                          ; 0xc07e6 vberom.asm:432
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc07e7 vberom.asm:433
    mov AL, strict byte 005h                  ; b0 05                       ; 0xc07ea vberom.asm:434
    out DX, AL                                ; ee                          ; 0xc07ec vberom.asm:435
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc07ed vberom.asm:436
    in AL, DX                                 ; ec                          ; 0xc07f0 vberom.asm:437
    and AL, strict byte 09fh                  ; 24 9f                       ; 0xc07f1 vberom.asm:438
    or AL, strict byte 040h                   ; 0c 40                       ; 0xc07f3 vberom.asm:439
    out DX, AL                                ; ee                          ; 0xc07f5 vberom.asm:440
    pop dx                                    ; 5a                          ; 0xc07f6 vberom.asm:443
    pop ax                                    ; 58                          ; 0xc07f7 vberom.asm:444
  ; disGetNextSymbol 0xc07f8 LB 0x157 -> off=0x0 cb=0000000000000013 uValue=00000000000c07f8 '_vbe_has_vbe_display'
_vbe_has_vbe_display:                        ; 0xc07f8 LB 0x13
    push DS                                   ; 1e                          ; 0xc07f8 vberom.asm:450
    push bx                                   ; 53                          ; 0xc07f9 vberom.asm:451
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc07fa vberom.asm:452
    mov ds, ax                                ; 8e d8                       ; 0xc07fd vberom.asm:453
    mov bx, 000b9h                            ; bb b9 00                    ; 0xc07ff vberom.asm:454
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0802 vberom.asm:455
    and AL, strict byte 001h                  ; 24 01                       ; 0xc0804 vberom.asm:456
    db  032h, 0e4h
    ; xor ah, ah                                ; 32 e4                     ; 0xc0806 vberom.asm:457
    pop bx                                    ; 5b                          ; 0xc0808 vberom.asm:458
    pop DS                                    ; 1f                          ; 0xc0809 vberom.asm:459
    retn                                      ; c3                          ; 0xc080a vberom.asm:460
  ; disGetNextSymbol 0xc080b LB 0x144 -> off=0x0 cb=0000000000000025 uValue=00000000000c080b 'vbe_biosfn_return_current_mode'
vbe_biosfn_return_current_mode:              ; 0xc080b LB 0x25
    push DS                                   ; 1e                          ; 0xc080b vberom.asm:473
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc080c vberom.asm:474
    mov ds, ax                                ; 8e d8                       ; 0xc080f vberom.asm:475
    call 00624h                               ; e8 10 fe                    ; 0xc0811 vberom.asm:476
    and ax, strict byte 00001h                ; 83 e0 01                    ; 0xc0814 vberom.asm:477
    je short 00822h                           ; 74 09                       ; 0xc0817 vberom.asm:478
    mov bx, 000bah                            ; bb ba 00                    ; 0xc0819 vberom.asm:479
    mov ax, word [bx]                         ; 8b 07                       ; 0xc081c vberom.asm:480
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc081e vberom.asm:481
    jne short 0082bh                          ; 75 09                       ; 0xc0820 vberom.asm:482
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0822 vberom.asm:484
    mov al, byte [bx]                         ; 8a 07                       ; 0xc0825 vberom.asm:485
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc0827 vberom.asm:486
    db  032h, 0ffh
    ; xor bh, bh                                ; 32 ff                     ; 0xc0829 vberom.asm:487
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc082b vberom.asm:489
    pop DS                                    ; 1f                          ; 0xc082e vberom.asm:490
    retn                                      ; c3                          ; 0xc082f vberom.asm:491
  ; disGetNextSymbol 0xc0830 LB 0x11f -> off=0x0 cb=000000000000002d uValue=00000000000c0830 'vbe_biosfn_display_window_control'
vbe_biosfn_display_window_control:           ; 0xc0830 LB 0x2d
    cmp bl, 000h                              ; 80 fb 00                    ; 0xc0830 vberom.asm:515
    jne short 00859h                          ; 75 24                       ; 0xc0833 vberom.asm:516
    cmp bh, 001h                              ; 80 ff 01                    ; 0xc0835 vberom.asm:517
    je short 00850h                           ; 74 16                       ; 0xc0838 vberom.asm:518
    jc short 00840h                           ; 72 04                       ; 0xc083a vberom.asm:519
    mov ax, 00100h                            ; b8 00 01                    ; 0xc083c vberom.asm:520
    retn                                      ; c3                          ; 0xc083f vberom.asm:521
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc0840 vberom.asm:523
    call 00636h                               ; e8 f1 fd                    ; 0xc0842 vberom.asm:524
    call 0064ah                               ; e8 02 fe                    ; 0xc0845 vberom.asm:525
    db  03bh, 0c2h
    ; cmp ax, dx                                ; 3b c2                     ; 0xc0848 vberom.asm:526
    jne short 00859h                          ; 75 0d                       ; 0xc084a vberom.asm:527
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc084c vberom.asm:528
    retn                                      ; c3                          ; 0xc084f vberom.asm:529
    call 0064ah                               ; e8 f7 fd                    ; 0xc0850 vberom.asm:531
    db  08bh, 0d0h
    ; mov dx, ax                                ; 8b d0                     ; 0xc0853 vberom.asm:532
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0855 vberom.asm:533
    retn                                      ; c3                          ; 0xc0858 vberom.asm:534
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc0859 vberom.asm:536
    retn                                      ; c3                          ; 0xc085c vberom.asm:537
  ; disGetNextSymbol 0xc085d LB 0xf2 -> off=0x0 cb=0000000000000034 uValue=00000000000c085d 'vbe_biosfn_set_get_display_start'
vbe_biosfn_set_get_display_start:            ; 0xc085d LB 0x34
    cmp bl, 080h                              ; 80 fb 80                    ; 0xc085d vberom.asm:577
    je short 0086dh                           ; 74 0b                       ; 0xc0860 vberom.asm:578
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc0862 vberom.asm:579
    je short 00881h                           ; 74 1a                       ; 0xc0865 vberom.asm:580
    jc short 00873h                           ; 72 0a                       ; 0xc0867 vberom.asm:581
    mov ax, 00100h                            ; b8 00 01                    ; 0xc0869 vberom.asm:582
    retn                                      ; c3                          ; 0xc086c vberom.asm:583
    call 005b9h                               ; e8 49 fd                    ; 0xc086d vberom.asm:585
    call 005ach                               ; e8 39 fd                    ; 0xc0870 vberom.asm:586
    db  08bh, 0c1h
    ; mov ax, cx                                ; 8b c1                     ; 0xc0873 vberom.asm:588
    call 0069ch                               ; e8 24 fe                    ; 0xc0875 vberom.asm:589
    db  08bh, 0c2h
    ; mov ax, dx                                ; 8b c2                     ; 0xc0878 vberom.asm:590
    call 006c2h                               ; e8 45 fe                    ; 0xc087a vberom.asm:591
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc087d vberom.asm:592
    retn                                      ; c3                          ; 0xc0880 vberom.asm:593
    call 006b0h                               ; e8 2c fe                    ; 0xc0881 vberom.asm:595
    db  08bh, 0c8h
    ; mov cx, ax                                ; 8b c8                     ; 0xc0884 vberom.asm:596
    call 006d6h                               ; e8 4d fe                    ; 0xc0886 vberom.asm:597
    db  08bh, 0d0h
    ; mov dx, ax                                ; 8b d0                     ; 0xc0889 vberom.asm:598
    db  032h, 0ffh
    ; xor bh, bh                                ; 32 ff                     ; 0xc088b vberom.asm:599
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc088d vberom.asm:600
    retn                                      ; c3                          ; 0xc0890 vberom.asm:601
  ; disGetNextSymbol 0xc0891 LB 0xbe -> off=0x0 cb=0000000000000037 uValue=00000000000c0891 'vbe_biosfn_set_get_dac_palette_format'
vbe_biosfn_set_get_dac_palette_format:       ; 0xc0891 LB 0x37
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc0891 vberom.asm:616
    je short 008b4h                           ; 74 1e                       ; 0xc0894 vberom.asm:617
    jc short 0089ch                           ; 72 04                       ; 0xc0896 vberom.asm:618
    mov ax, 00100h                            ; b8 00 01                    ; 0xc0898 vberom.asm:619
    retn                                      ; c3                          ; 0xc089b vberom.asm:620
    call 00624h                               ; e8 85 fd                    ; 0xc089c vberom.asm:622
    cmp bh, 006h                              ; 80 ff 06                    ; 0xc089f vberom.asm:623
    je short 008aeh                           ; 74 0a                       ; 0xc08a2 vberom.asm:624
    cmp bh, 008h                              ; 80 ff 08                    ; 0xc08a4 vberom.asm:625
    jne short 008c4h                          ; 75 1b                       ; 0xc08a7 vberom.asm:626
    or ax, strict byte 00020h                 ; 83 c8 20                    ; 0xc08a9 vberom.asm:627
    jne short 008b1h                          ; 75 03                       ; 0xc08ac vberom.asm:628
    and ax, strict byte 0ffdfh                ; 83 e0 df                    ; 0xc08ae vberom.asm:630
    call 00610h                               ; e8 5c fd                    ; 0xc08b1 vberom.asm:632
    mov BH, strict byte 006h                  ; b7 06                       ; 0xc08b4 vberom.asm:634
    call 00624h                               ; e8 6b fd                    ; 0xc08b6 vberom.asm:635
    and ax, strict byte 00020h                ; 83 e0 20                    ; 0xc08b9 vberom.asm:636
    je short 008c0h                           ; 74 02                       ; 0xc08bc vberom.asm:637
    mov BH, strict byte 008h                  ; b7 08                       ; 0xc08be vberom.asm:638
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc08c0 vberom.asm:640
    retn                                      ; c3                          ; 0xc08c3 vberom.asm:641
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc08c4 vberom.asm:643
    retn                                      ; c3                          ; 0xc08c7 vberom.asm:644
  ; disGetNextSymbol 0xc08c8 LB 0x87 -> off=0x0 cb=0000000000000073 uValue=00000000000c08c8 'vbe_biosfn_set_get_palette_data'
vbe_biosfn_set_get_palette_data:             ; 0xc08c8 LB 0x73
    test bl, bl                               ; 84 db                       ; 0xc08c8 vberom.asm:683
    je short 008dbh                           ; 74 0f                       ; 0xc08ca vberom.asm:684
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc08cc vberom.asm:685
    je short 00909h                           ; 74 38                       ; 0xc08cf vberom.asm:686
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc08d1 vberom.asm:687
    jbe short 00937h                          ; 76 61                       ; 0xc08d4 vberom.asm:688
    cmp bl, 080h                              ; 80 fb 80                    ; 0xc08d6 vberom.asm:689
    jne short 00933h                          ; 75 58                       ; 0xc08d9 vberom.asm:690
    push ax                                   ; 50                          ; 0xc08db vberom.asm:145
    push cx                                   ; 51                          ; 0xc08dc vberom.asm:146
    push dx                                   ; 52                          ; 0xc08dd vberom.asm:147
    push bx                                   ; 53                          ; 0xc08de vberom.asm:148
    push sp                                   ; 54                          ; 0xc08df vberom.asm:149
    push bp                                   ; 55                          ; 0xc08e0 vberom.asm:150
    push si                                   ; 56                          ; 0xc08e1 vberom.asm:151
    push di                                   ; 57                          ; 0xc08e2 vberom.asm:152
    push DS                                   ; 1e                          ; 0xc08e3 vberom.asm:696
    push ES                                   ; 06                          ; 0xc08e4 vberom.asm:697
    pop DS                                    ; 1f                          ; 0xc08e5 vberom.asm:698
    db  08ah, 0c2h
    ; mov al, dl                                ; 8a c2                     ; 0xc08e6 vberom.asm:699
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc08e8 vberom.asm:700
    out DX, AL                                ; ee                          ; 0xc08eb vberom.asm:701
    inc dx                                    ; 42                          ; 0xc08ec vberom.asm:702
    db  08bh, 0f7h
    ; mov si, di                                ; 8b f7                     ; 0xc08ed vberom.asm:703
    lodsw                                     ; ad                          ; 0xc08ef vberom.asm:714
    db  08bh, 0d8h
    ; mov bx, ax                                ; 8b d8                     ; 0xc08f0 vberom.asm:715
    lodsw                                     ; ad                          ; 0xc08f2 vberom.asm:716
    out DX, AL                                ; ee                          ; 0xc08f3 vberom.asm:717
    db  08ah, 0c7h
    ; mov al, bh                                ; 8a c7                     ; 0xc08f4 vberom.asm:718
    out DX, AL                                ; ee                          ; 0xc08f6 vberom.asm:719
    db  08ah, 0c3h
    ; mov al, bl                                ; 8a c3                     ; 0xc08f7 vberom.asm:720
    out DX, AL                                ; ee                          ; 0xc08f9 vberom.asm:721
    loop 008efh                               ; e2 f3                       ; 0xc08fa vberom.asm:723
    pop DS                                    ; 1f                          ; 0xc08fc vberom.asm:724
    pop di                                    ; 5f                          ; 0xc08fd vberom.asm:164
    pop si                                    ; 5e                          ; 0xc08fe vberom.asm:165
    pop bp                                    ; 5d                          ; 0xc08ff vberom.asm:166
    pop bx                                    ; 5b                          ; 0xc0900 vberom.asm:167
    pop bx                                    ; 5b                          ; 0xc0901 vberom.asm:168
    pop dx                                    ; 5a                          ; 0xc0902 vberom.asm:169
    pop cx                                    ; 59                          ; 0xc0903 vberom.asm:170
    pop ax                                    ; 58                          ; 0xc0904 vberom.asm:171
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0905 vberom.asm:727
    retn                                      ; c3                          ; 0xc0908 vberom.asm:728
    push ax                                   ; 50                          ; 0xc0909 vberom.asm:145
    push cx                                   ; 51                          ; 0xc090a vberom.asm:146
    push dx                                   ; 52                          ; 0xc090b vberom.asm:147
    push bx                                   ; 53                          ; 0xc090c vberom.asm:148
    push sp                                   ; 54                          ; 0xc090d vberom.asm:149
    push bp                                   ; 55                          ; 0xc090e vberom.asm:150
    push si                                   ; 56                          ; 0xc090f vberom.asm:151
    push di                                   ; 57                          ; 0xc0910 vberom.asm:152
    db  08ah, 0c2h
    ; mov al, dl                                ; 8a c2                     ; 0xc0911 vberom.asm:732
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc0913 vberom.asm:733
    out DX, AL                                ; ee                          ; 0xc0916 vberom.asm:734
    add dl, 002h                              ; 80 c2 02                    ; 0xc0917 vberom.asm:735
    db  033h, 0dbh
    ; xor bx, bx                                ; 33 db                     ; 0xc091a vberom.asm:746
    in AL, DX                                 ; ec                          ; 0xc091c vberom.asm:748
    db  08ah, 0d8h
    ; mov bl, al                                ; 8a d8                     ; 0xc091d vberom.asm:749
    in AL, DX                                 ; ec                          ; 0xc091f vberom.asm:750
    db  08ah, 0e0h
    ; mov ah, al                                ; 8a e0                     ; 0xc0920 vberom.asm:751
    in AL, DX                                 ; ec                          ; 0xc0922 vberom.asm:752
    stosw                                     ; ab                          ; 0xc0923 vberom.asm:753
    db  08bh, 0c3h
    ; mov ax, bx                                ; 8b c3                     ; 0xc0924 vberom.asm:754
    stosw                                     ; ab                          ; 0xc0926 vberom.asm:755
    loop 0091ch                               ; e2 f3                       ; 0xc0927 vberom.asm:757
    pop di                                    ; 5f                          ; 0xc0929 vberom.asm:164
    pop si                                    ; 5e                          ; 0xc092a vberom.asm:165
    pop bp                                    ; 5d                          ; 0xc092b vberom.asm:166
    pop bx                                    ; 5b                          ; 0xc092c vberom.asm:167
    pop bx                                    ; 5b                          ; 0xc092d vberom.asm:168
    pop dx                                    ; 5a                          ; 0xc092e vberom.asm:169
    pop cx                                    ; 59                          ; 0xc092f vberom.asm:170
    pop ax                                    ; 58                          ; 0xc0930 vberom.asm:171
    jmp short 00905h                          ; eb d2                       ; 0xc0931 vberom.asm:759
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc0933 vberom.asm:762
    retn                                      ; c3                          ; 0xc0936 vberom.asm:763
    mov ax, 0024fh                            ; b8 4f 02                    ; 0xc0937 vberom.asm:765
    retn                                      ; c3                          ; 0xc093a vberom.asm:766
  ; disGetNextSymbol 0xc093b LB 0x14 -> off=0x0 cb=0000000000000014 uValue=00000000000c093b 'vbe_biosfn_return_protected_mode_interface'
vbe_biosfn_return_protected_mode_interface: ; 0xc093b LB 0x14
    test bl, bl                               ; 84 db                       ; 0xc093b vberom.asm:780
    jne short 0094bh                          ; 75 0c                       ; 0xc093d vberom.asm:781
    push CS                                   ; 0e                          ; 0xc093f vberom.asm:782
    pop ES                                    ; 07                          ; 0xc0940 vberom.asm:783
    mov di, 04640h                            ; bf 40 46                    ; 0xc0941 vberom.asm:784
    mov cx, 00115h                            ; b9 15 01                    ; 0xc0944 vberom.asm:785
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc0947 vberom.asm:786
    retn                                      ; c3                          ; 0xc094a vberom.asm:787
    mov ax, 0014fh                            ; b8 4f 01                    ; 0xc094b vberom.asm:789
    retn                                      ; c3                          ; 0xc094e vberom.asm:790

  ; Padding 0xa1 bytes at 0xc094f
  times 161 db 0

section _TEXT progbits vstart=0x9f0 align=1 ; size=0x3b74 class=CODE group=AUTO
  ; disGetNextSymbol 0xc09f0 LB 0x3b74 -> off=0x0 cb=000000000000001c uValue=00000000000c09f0 'set_int_vector'
set_int_vector:                              ; 0xc09f0 LB 0x1c
    push dx                                   ; 52                          ; 0xc09f0 vgabios.c:87
    push bp                                   ; 55                          ; 0xc09f1
    mov bp, sp                                ; 89 e5                       ; 0xc09f2
    mov dx, bx                                ; 89 da                       ; 0xc09f4
    mov bl, al                                ; 88 c3                       ; 0xc09f6 vgabios.c:91
    xor bh, bh                                ; 30 ff                       ; 0xc09f8
    sal bx, 1                                 ; d1 e3                       ; 0xc09fa
    sal bx, 1                                 ; d1 e3                       ; 0xc09fc
    xor ax, ax                                ; 31 c0                       ; 0xc09fe
    mov es, ax                                ; 8e c0                       ; 0xc0a00
    mov word [es:bx], dx                      ; 26 89 17                    ; 0xc0a02
    mov word [es:bx+002h], cx                 ; 26 89 4f 02                 ; 0xc0a05
    pop bp                                    ; 5d                          ; 0xc0a09 vgabios.c:92
    pop dx                                    ; 5a                          ; 0xc0a0a
    retn                                      ; c3                          ; 0xc0a0b
  ; disGetNextSymbol 0xc0a0c LB 0x3b58 -> off=0x0 cb=000000000000001c uValue=00000000000c0a0c 'init_vga_card'
init_vga_card:                               ; 0xc0a0c LB 0x1c
    push bp                                   ; 55                          ; 0xc0a0c vgabios.c:143
    mov bp, sp                                ; 89 e5                       ; 0xc0a0d
    push dx                                   ; 52                          ; 0xc0a0f
    mov AL, strict byte 0c3h                  ; b0 c3                       ; 0xc0a10 vgabios.c:146
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc0a12
    out DX, AL                                ; ee                          ; 0xc0a15
    mov AL, strict byte 004h                  ; b0 04                       ; 0xc0a16 vgabios.c:149
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc0a18
    out DX, AL                                ; ee                          ; 0xc0a1b
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc0a1c vgabios.c:150
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc0a1e
    out DX, AL                                ; ee                          ; 0xc0a21
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc0a22 vgabios.c:155
    pop dx                                    ; 5a                          ; 0xc0a25
    pop bp                                    ; 5d                          ; 0xc0a26
    retn                                      ; c3                          ; 0xc0a27
  ; disGetNextSymbol 0xc0a28 LB 0x3b3c -> off=0x0 cb=000000000000003e uValue=00000000000c0a28 'init_bios_area'
init_bios_area:                              ; 0xc0a28 LB 0x3e
    push bx                                   ; 53                          ; 0xc0a28 vgabios.c:221
    push bp                                   ; 55                          ; 0xc0a29
    mov bp, sp                                ; 89 e5                       ; 0xc0a2a
    xor bx, bx                                ; 31 db                       ; 0xc0a2c vgabios.c:225
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0a2e
    mov es, ax                                ; 8e c0                       ; 0xc0a31
    mov al, byte [es:bx+010h]                 ; 26 8a 47 10                 ; 0xc0a33 vgabios.c:228
    and AL, strict byte 0cfh                  ; 24 cf                       ; 0xc0a37
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc0a39
    mov byte [es:bx+010h], al                 ; 26 88 47 10                 ; 0xc0a3b
    mov byte [es:bx+00085h], 010h             ; 26 c6 87 85 00 10           ; 0xc0a3f vgabios.c:232
    mov word [es:bx+00087h], 0f960h           ; 26 c7 87 87 00 60 f9        ; 0xc0a45 vgabios.c:234
    mov byte [es:bx+00089h], 051h             ; 26 c6 87 89 00 51           ; 0xc0a4c vgabios.c:238
    mov byte [es:bx+065h], 009h               ; 26 c6 47 65 09              ; 0xc0a52 vgabios.c:240
    mov word [es:bx+000a8h], 05553h           ; 26 c7 87 a8 00 53 55        ; 0xc0a57 vgabios.c:242
    mov [es:bx+000aah], ds                    ; 26 8c 9f aa 00              ; 0xc0a5e
    pop bp                                    ; 5d                          ; 0xc0a63 vgabios.c:243
    pop bx                                    ; 5b                          ; 0xc0a64
    retn                                      ; c3                          ; 0xc0a65
  ; disGetNextSymbol 0xc0a66 LB 0x3afe -> off=0x0 cb=0000000000000031 uValue=00000000000c0a66 'vgabios_init_func'
vgabios_init_func:                           ; 0xc0a66 LB 0x31
    inc bp                                    ; 45                          ; 0xc0a66 vgabios.c:250
    push bp                                   ; 55                          ; 0xc0a67
    mov bp, sp                                ; 89 e5                       ; 0xc0a68
    call 00a0ch                               ; e8 9f ff                    ; 0xc0a6a vgabios.c:252
    call 00a28h                               ; e8 b8 ff                    ; 0xc0a6d vgabios.c:253
    call 03ed5h                               ; e8 62 34                    ; 0xc0a70 vgabios.c:255
    mov bx, strict word 00028h                ; bb 28 00                    ; 0xc0a73 vgabios.c:257
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0a76
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc0a79
    call 009f0h                               ; e8 71 ff                    ; 0xc0a7c
    mov bx, strict word 00028h                ; bb 28 00                    ; 0xc0a7f vgabios.c:258
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0a82
    mov ax, strict word 0006dh                ; b8 6d 00                    ; 0xc0a85
    call 009f0h                               ; e8 65 ff                    ; 0xc0a88
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc0a8b vgabios.c:284
    db  032h, 0e4h
    ; xor ah, ah                                ; 32 e4                     ; 0xc0a8e
    int 010h                                  ; cd 10                       ; 0xc0a90
    mov sp, bp                                ; 89 ec                       ; 0xc0a92 vgabios.c:287
    pop bp                                    ; 5d                          ; 0xc0a94
    dec bp                                    ; 4d                          ; 0xc0a95
    retf                                      ; cb                          ; 0xc0a96
  ; disGetNextSymbol 0xc0a97 LB 0x3acd -> off=0x0 cb=0000000000000040 uValue=00000000000c0a97 'vga_get_cursor_pos'
vga_get_cursor_pos:                          ; 0xc0a97 LB 0x40
    push si                                   ; 56                          ; 0xc0a97 vgabios.c:356
    push di                                   ; 57                          ; 0xc0a98
    push bp                                   ; 55                          ; 0xc0a99
    mov bp, sp                                ; 89 e5                       ; 0xc0a9a
    mov si, dx                                ; 89 d6                       ; 0xc0a9c
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc0a9e vgabios.c:358
    jbe short 00ab0h                          ; 76 0e                       ; 0xc0aa0
    push SS                                   ; 16                          ; 0xc0aa2 vgabios.c:359
    pop ES                                    ; 07                          ; 0xc0aa3
    mov word [es:si], strict word 00000h      ; 26 c7 04 00 00              ; 0xc0aa4
    mov word [es:bx], strict word 00000h      ; 26 c7 07 00 00              ; 0xc0aa9 vgabios.c:360
    jmp short 00ad3h                          ; eb 23                       ; 0xc0aae vgabios.c:361
    mov di, strict word 00060h                ; bf 60 00                    ; 0xc0ab0 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc0ab3
    mov es, dx                                ; 8e c2                       ; 0xc0ab6
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0ab8
    push SS                                   ; 16                          ; 0xc0abb vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0abc
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc0abd
    xor ah, ah                                ; 30 e4                       ; 0xc0ac0 vgabios.c:364
    mov si, ax                                ; 89 c6                       ; 0xc0ac2
    sal si, 1                                 ; d1 e6                       ; 0xc0ac4
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc0ac6
    mov es, dx                                ; 8e c2                       ; 0xc0ac9 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc0acb
    push SS                                   ; 16                          ; 0xc0ace vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0acf
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0ad0
    pop bp                                    ; 5d                          ; 0xc0ad3 vgabios.c:366
    pop di                                    ; 5f                          ; 0xc0ad4
    pop si                                    ; 5e                          ; 0xc0ad5
    retn                                      ; c3                          ; 0xc0ad6
  ; disGetNextSymbol 0xc0ad7 LB 0x3a8d -> off=0x0 cb=000000000000005e uValue=00000000000c0ad7 'vga_find_glyph'
vga_find_glyph:                              ; 0xc0ad7 LB 0x5e
    push bp                                   ; 55                          ; 0xc0ad7 vgabios.c:369
    mov bp, sp                                ; 89 e5                       ; 0xc0ad8
    push si                                   ; 56                          ; 0xc0ada
    push di                                   ; 57                          ; 0xc0adb
    push ax                                   ; 50                          ; 0xc0adc
    push ax                                   ; 50                          ; 0xc0add
    push dx                                   ; 52                          ; 0xc0ade
    push bx                                   ; 53                          ; 0xc0adf
    mov bl, cl                                ; 88 cb                       ; 0xc0ae0
    mov word [bp-006h], strict word 00000h    ; c7 46 fa 00 00              ; 0xc0ae2 vgabios.c:371
    dec word [bp+004h]                        ; ff 4e 04                    ; 0xc0ae7 vgabios.c:373
    cmp word [bp+004h], strict byte 0ffffh    ; 83 7e 04 ff                 ; 0xc0aea
    je short 00b29h                           ; 74 39                       ; 0xc0aee
    mov cl, byte [bp+006h]                    ; 8a 4e 06                    ; 0xc0af0 vgabios.c:374
    xor ch, ch                                ; 30 ed                       ; 0xc0af3
    mov dx, ss                                ; 8c d2                       ; 0xc0af5
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc0af7
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc0afa
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc0afd
    push DS                                   ; 1e                          ; 0xc0b00
    mov ds, dx                                ; 8e da                       ; 0xc0b01
    rep cmpsb                                 ; f3 a6                       ; 0xc0b03
    pop DS                                    ; 1f                          ; 0xc0b05
    mov ax, strict word 00000h                ; b8 00 00                    ; 0xc0b06
    je short 00b0dh                           ; 74 02                       ; 0xc0b09
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc0b0b
    test ax, ax                               ; 85 c0                       ; 0xc0b0d
    jne short 00b1dh                          ; 75 0c                       ; 0xc0b0f
    mov al, bl                                ; 88 d8                       ; 0xc0b11 vgabios.c:375
    xor ah, ah                                ; 30 e4                       ; 0xc0b13
    or ah, 080h                               ; 80 cc 80                    ; 0xc0b15
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0b18
    jmp short 00b29h                          ; eb 0c                       ; 0xc0b1b vgabios.c:376
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc0b1d vgabios.c:378
    xor ah, ah                                ; 30 e4                       ; 0xc0b20
    add word [bp-008h], ax                    ; 01 46 f8                    ; 0xc0b22
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc0b25 vgabios.c:379
    jmp short 00ae7h                          ; eb be                       ; 0xc0b27 vgabios.c:380
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc0b29 vgabios.c:382
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b2c
    pop di                                    ; 5f                          ; 0xc0b2f
    pop si                                    ; 5e                          ; 0xc0b30
    pop bp                                    ; 5d                          ; 0xc0b31
    retn 00004h                               ; c2 04 00                    ; 0xc0b32
  ; disGetNextSymbol 0xc0b35 LB 0x3a2f -> off=0x0 cb=0000000000000046 uValue=00000000000c0b35 'vga_read_glyph_planar'
vga_read_glyph_planar:                       ; 0xc0b35 LB 0x46
    push bp                                   ; 55                          ; 0xc0b35 vgabios.c:384
    mov bp, sp                                ; 89 e5                       ; 0xc0b36
    push si                                   ; 56                          ; 0xc0b38
    push di                                   ; 57                          ; 0xc0b39
    push ax                                   ; 50                          ; 0xc0b3a
    push ax                                   ; 50                          ; 0xc0b3b
    mov si, ax                                ; 89 c6                       ; 0xc0b3c
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc0b3e
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc0b41
    mov bx, cx                                ; 89 cb                       ; 0xc0b44
    mov ax, 00805h                            ; b8 05 08                    ; 0xc0b46 vgabios.c:391
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b49
    out DX, ax                                ; ef                          ; 0xc0b4c
    dec byte [bp+004h]                        ; fe 4e 04                    ; 0xc0b4d vgabios.c:393
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc0b50
    je short 00b6bh                           ; 74 15                       ; 0xc0b54
    mov es, [bp-006h]                         ; 8e 46 fa                    ; 0xc0b56 vgabios.c:394
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc0b59
    not al                                    ; f6 d0                       ; 0xc0b5c
    mov di, bx                                ; 89 df                       ; 0xc0b5e
    inc bx                                    ; 43                          ; 0xc0b60
    push SS                                   ; 16                          ; 0xc0b61
    pop ES                                    ; 07                          ; 0xc0b62
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0b63
    add si, word [bp-008h]                    ; 03 76 f8                    ; 0xc0b66 vgabios.c:395
    jmp short 00b4dh                          ; eb e2                       ; 0xc0b69 vgabios.c:396
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc0b6b vgabios.c:399
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc0b6e
    out DX, ax                                ; ef                          ; 0xc0b71
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0b72 vgabios.c:400
    pop di                                    ; 5f                          ; 0xc0b75
    pop si                                    ; 5e                          ; 0xc0b76
    pop bp                                    ; 5d                          ; 0xc0b77
    retn 00002h                               ; c2 02 00                    ; 0xc0b78
  ; disGetNextSymbol 0xc0b7b LB 0x39e9 -> off=0x0 cb=000000000000002f uValue=00000000000c0b7b 'vga_char_ofs_planar'
vga_char_ofs_planar:                         ; 0xc0b7b LB 0x2f
    push si                                   ; 56                          ; 0xc0b7b vgabios.c:402
    push bp                                   ; 55                          ; 0xc0b7c
    mov bp, sp                                ; 89 e5                       ; 0xc0b7d
    mov ch, al                                ; 88 c5                       ; 0xc0b7f
    mov al, dl                                ; 88 d0                       ; 0xc0b81
    xor ah, ah                                ; 30 e4                       ; 0xc0b83 vgabios.c:406
    mul bx                                    ; f7 e3                       ; 0xc0b85
    mov bl, byte [bp+006h]                    ; 8a 5e 06                    ; 0xc0b87
    xor bh, bh                                ; 30 ff                       ; 0xc0b8a
    mul bx                                    ; f7 e3                       ; 0xc0b8c
    mov bl, ch                                ; 88 eb                       ; 0xc0b8e
    add bx, ax                                ; 01 c3                       ; 0xc0b90
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc0b92 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0b95
    mov es, ax                                ; 8e c0                       ; 0xc0b98
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc0b9a
    mov al, cl                                ; 88 c8                       ; 0xc0b9d vgabios.c:58
    xor ah, ah                                ; 30 e4                       ; 0xc0b9f
    mul si                                    ; f7 e6                       ; 0xc0ba1
    add ax, bx                                ; 01 d8                       ; 0xc0ba3
    pop bp                                    ; 5d                          ; 0xc0ba5 vgabios.c:410
    pop si                                    ; 5e                          ; 0xc0ba6
    retn 00002h                               ; c2 02 00                    ; 0xc0ba7
  ; disGetNextSymbol 0xc0baa LB 0x39ba -> off=0x0 cb=0000000000000045 uValue=00000000000c0baa 'vga_read_char_planar'
vga_read_char_planar:                        ; 0xc0baa LB 0x45
    push bp                                   ; 55                          ; 0xc0baa vgabios.c:412
    mov bp, sp                                ; 89 e5                       ; 0xc0bab
    push cx                                   ; 51                          ; 0xc0bad
    push si                                   ; 56                          ; 0xc0bae
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0baf
    mov si, ax                                ; 89 c6                       ; 0xc0bb2
    mov ax, dx                                ; 89 d0                       ; 0xc0bb4
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc0bb6 vgabios.c:416
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc0bb9
    push word [bp-006h]                       ; ff 76 fa                    ; 0xc0bbd
    lea cx, [bp-016h]                         ; 8d 4e ea                    ; 0xc0bc0
    mov bx, si                                ; 89 f3                       ; 0xc0bc3
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0bc5
    call 00b35h                               ; e8 6a ff                    ; 0xc0bc8
    push word [bp-006h]                       ; ff 76 fa                    ; 0xc0bcb vgabios.c:419
    mov ax, 00100h                            ; b8 00 01                    ; 0xc0bce
    push ax                                   ; 50                          ; 0xc0bd1
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0bd2 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0bd5
    mov es, ax                                ; 8e c0                       ; 0xc0bd7
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0bd9
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0bdc
    xor cx, cx                                ; 31 c9                       ; 0xc0be0 vgabios.c:68
    lea bx, [bp-016h]                         ; 8d 5e ea                    ; 0xc0be2
    call 00ad7h                               ; e8 ef fe                    ; 0xc0be5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0be8 vgabios.c:420
    pop si                                    ; 5e                          ; 0xc0beb
    pop cx                                    ; 59                          ; 0xc0bec
    pop bp                                    ; 5d                          ; 0xc0bed
    retn                                      ; c3                          ; 0xc0bee
  ; disGetNextSymbol 0xc0bef LB 0x3975 -> off=0x0 cb=0000000000000027 uValue=00000000000c0bef 'vga_char_ofs_linear'
vga_char_ofs_linear:                         ; 0xc0bef LB 0x27
    push bp                                   ; 55                          ; 0xc0bef vgabios.c:422
    mov bp, sp                                ; 89 e5                       ; 0xc0bf0
    push ax                                   ; 50                          ; 0xc0bf2
    mov byte [bp-002h], al                    ; 88 46 fe                    ; 0xc0bf3
    mov al, dl                                ; 88 d0                       ; 0xc0bf6 vgabios.c:426
    xor ah, ah                                ; 30 e4                       ; 0xc0bf8
    mul bx                                    ; f7 e3                       ; 0xc0bfa
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc0bfc
    xor dh, dh                                ; 30 f6                       ; 0xc0bff
    mul dx                                    ; f7 e2                       ; 0xc0c01
    mov dx, ax                                ; 89 c2                       ; 0xc0c03
    mov al, byte [bp-002h]                    ; 8a 46 fe                    ; 0xc0c05
    xor ah, ah                                ; 30 e4                       ; 0xc0c08
    add ax, dx                                ; 01 d0                       ; 0xc0c0a
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc0c0c vgabios.c:427
    sal ax, CL                                ; d3 e0                       ; 0xc0c0e
    mov sp, bp                                ; 89 ec                       ; 0xc0c10 vgabios.c:429
    pop bp                                    ; 5d                          ; 0xc0c12
    retn 00002h                               ; c2 02 00                    ; 0xc0c13
  ; disGetNextSymbol 0xc0c16 LB 0x394e -> off=0x0 cb=000000000000004e uValue=00000000000c0c16 'vga_read_glyph_linear'
vga_read_glyph_linear:                       ; 0xc0c16 LB 0x4e
    push si                                   ; 56                          ; 0xc0c16 vgabios.c:431
    push di                                   ; 57                          ; 0xc0c17
    push bp                                   ; 55                          ; 0xc0c18
    mov bp, sp                                ; 89 e5                       ; 0xc0c19
    push ax                                   ; 50                          ; 0xc0c1b
    push ax                                   ; 50                          ; 0xc0c1c
    mov si, ax                                ; 89 c6                       ; 0xc0c1d
    mov word [bp-002h], dx                    ; 89 56 fe                    ; 0xc0c1f
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc0c22
    mov bx, cx                                ; 89 cb                       ; 0xc0c25
    dec byte [bp+008h]                        ; fe 4e 08                    ; 0xc0c27 vgabios.c:437
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc0c2a
    je short 00c5ch                           ; 74 2c                       ; 0xc0c2e
    xor dh, dh                                ; 30 f6                       ; 0xc0c30 vgabios.c:438
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc0c32 vgabios.c:439
    xor ax, ax                                ; 31 c0                       ; 0xc0c34 vgabios.c:440
    jmp short 00c3dh                          ; eb 05                       ; 0xc0c36
    cmp ax, strict word 00008h                ; 3d 08 00                    ; 0xc0c38
    jnl short 00c51h                          ; 7d 14                       ; 0xc0c3b
    mov es, [bp-002h]                         ; 8e 46 fe                    ; 0xc0c3d vgabios.c:441
    mov di, si                                ; 89 f7                       ; 0xc0c40
    add di, ax                                ; 01 c7                       ; 0xc0c42
    cmp byte [es:di], 000h                    ; 26 80 3d 00                 ; 0xc0c44
    je short 00c4ch                           ; 74 02                       ; 0xc0c48
    or dh, dl                                 ; 08 d6                       ; 0xc0c4a vgabios.c:442
    shr dl, 1                                 ; d0 ea                       ; 0xc0c4c vgabios.c:443
    inc ax                                    ; 40                          ; 0xc0c4e vgabios.c:444
    jmp short 00c38h                          ; eb e7                       ; 0xc0c4f
    mov di, bx                                ; 89 df                       ; 0xc0c51 vgabios.c:445
    inc bx                                    ; 43                          ; 0xc0c53
    mov byte [ss:di], dh                      ; 36 88 35                    ; 0xc0c54
    add si, word [bp-004h]                    ; 03 76 fc                    ; 0xc0c57 vgabios.c:446
    jmp short 00c27h                          ; eb cb                       ; 0xc0c5a vgabios.c:447
    mov sp, bp                                ; 89 ec                       ; 0xc0c5c vgabios.c:448
    pop bp                                    ; 5d                          ; 0xc0c5e
    pop di                                    ; 5f                          ; 0xc0c5f
    pop si                                    ; 5e                          ; 0xc0c60
    retn 00002h                               ; c2 02 00                    ; 0xc0c61
  ; disGetNextSymbol 0xc0c64 LB 0x3900 -> off=0x0 cb=0000000000000049 uValue=00000000000c0c64 'vga_read_char_linear'
vga_read_char_linear:                        ; 0xc0c64 LB 0x49
    push bp                                   ; 55                          ; 0xc0c64 vgabios.c:450
    mov bp, sp                                ; 89 e5                       ; 0xc0c65
    push cx                                   ; 51                          ; 0xc0c67
    push si                                   ; 56                          ; 0xc0c68
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0c69
    mov si, ax                                ; 89 c6                       ; 0xc0c6c
    mov ax, dx                                ; 89 d0                       ; 0xc0c6e
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc0c70 vgabios.c:454
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc0c73
    push word [bp-006h]                       ; ff 76 fa                    ; 0xc0c77
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc0c7a
    mov bx, si                                ; 89 f3                       ; 0xc0c7c
    sal bx, CL                                ; d3 e3                       ; 0xc0c7e
    lea cx, [bp-016h]                         ; 8d 4e ea                    ; 0xc0c80
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc0c83
    call 00c16h                               ; e8 8d ff                    ; 0xc0c86
    push word [bp-006h]                       ; ff 76 fa                    ; 0xc0c89 vgabios.c:457
    mov ax, 00100h                            ; b8 00 01                    ; 0xc0c8c
    push ax                                   ; 50                          ; 0xc0c8f
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0c90 vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0c93
    mov es, ax                                ; 8e c0                       ; 0xc0c95
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0c97
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0c9a
    xor cx, cx                                ; 31 c9                       ; 0xc0c9e vgabios.c:68
    lea bx, [bp-016h]                         ; 8d 5e ea                    ; 0xc0ca0
    call 00ad7h                               ; e8 31 fe                    ; 0xc0ca3
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0ca6 vgabios.c:458
    pop si                                    ; 5e                          ; 0xc0ca9
    pop cx                                    ; 59                          ; 0xc0caa
    pop bp                                    ; 5d                          ; 0xc0cab
    retn                                      ; c3                          ; 0xc0cac
  ; disGetNextSymbol 0xc0cad LB 0x38b7 -> off=0x0 cb=0000000000000036 uValue=00000000000c0cad 'vga_read_2bpp_char'
vga_read_2bpp_char:                          ; 0xc0cad LB 0x36
    push bp                                   ; 55                          ; 0xc0cad vgabios.c:460
    mov bp, sp                                ; 89 e5                       ; 0xc0cae
    push bx                                   ; 53                          ; 0xc0cb0
    push cx                                   ; 51                          ; 0xc0cb1
    mov bx, ax                                ; 89 c3                       ; 0xc0cb2
    mov es, dx                                ; 8e c2                       ; 0xc0cb4
    mov cx, 0c000h                            ; b9 00 c0                    ; 0xc0cb6 vgabios.c:466
    mov DH, strict byte 080h                  ; b6 80                       ; 0xc0cb9 vgabios.c:467
    xor dl, dl                                ; 30 d2                       ; 0xc0cbb vgabios.c:468
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0cbd vgabios.c:469
    xchg ah, al                               ; 86 c4                       ; 0xc0cc0
    xor bx, bx                                ; 31 db                       ; 0xc0cc2 vgabios.c:471
    jmp short 00ccbh                          ; eb 05                       ; 0xc0cc4
    cmp bx, strict byte 00008h                ; 83 fb 08                    ; 0xc0cc6
    jnl short 00cdah                          ; 7d 0f                       ; 0xc0cc9
    test ax, cx                               ; 85 c8                       ; 0xc0ccb vgabios.c:472
    je short 00cd1h                           ; 74 02                       ; 0xc0ccd
    or dl, dh                                 ; 08 f2                       ; 0xc0ccf vgabios.c:473
    shr dh, 1                                 ; d0 ee                       ; 0xc0cd1 vgabios.c:474
    shr cx, 1                                 ; d1 e9                       ; 0xc0cd3 vgabios.c:475
    shr cx, 1                                 ; d1 e9                       ; 0xc0cd5
    inc bx                                    ; 43                          ; 0xc0cd7 vgabios.c:476
    jmp short 00cc6h                          ; eb ec                       ; 0xc0cd8
    mov al, dl                                ; 88 d0                       ; 0xc0cda vgabios.c:478
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0cdc
    pop cx                                    ; 59                          ; 0xc0cdf
    pop bx                                    ; 5b                          ; 0xc0ce0
    pop bp                                    ; 5d                          ; 0xc0ce1
    retn                                      ; c3                          ; 0xc0ce2
  ; disGetNextSymbol 0xc0ce3 LB 0x3881 -> off=0x0 cb=0000000000000084 uValue=00000000000c0ce3 'vga_read_glyph_cga'
vga_read_glyph_cga:                          ; 0xc0ce3 LB 0x84
    push bp                                   ; 55                          ; 0xc0ce3 vgabios.c:480
    mov bp, sp                                ; 89 e5                       ; 0xc0ce4
    push cx                                   ; 51                          ; 0xc0ce6
    push si                                   ; 56                          ; 0xc0ce7
    push di                                   ; 57                          ; 0xc0ce8
    push ax                                   ; 50                          ; 0xc0ce9
    mov si, dx                                ; 89 d6                       ; 0xc0cea
    cmp bl, 006h                              ; 80 fb 06                    ; 0xc0cec vgabios.c:488
    je short 00d2bh                           ; 74 3a                       ; 0xc0cef
    mov bx, ax                                ; 89 c3                       ; 0xc0cf1 vgabios.c:490
    sal bx, 1                                 ; d1 e3                       ; 0xc0cf3
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0cf5
    xor cx, cx                                ; 31 c9                       ; 0xc0cfa vgabios.c:492
    jmp short 00d03h                          ; eb 05                       ; 0xc0cfc
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0cfe
    jnl short 00d5fh                          ; 7d 5c                       ; 0xc0d01
    mov ax, bx                                ; 89 d8                       ; 0xc0d03 vgabios.c:493
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0d05
    call 00cadh                               ; e8 a2 ff                    ; 0xc0d08
    mov di, si                                ; 89 f7                       ; 0xc0d0b
    inc si                                    ; 46                          ; 0xc0d0d
    push SS                                   ; 16                          ; 0xc0d0e
    pop ES                                    ; 07                          ; 0xc0d0f
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d10
    lea ax, [bx+02000h]                       ; 8d 87 00 20                 ; 0xc0d13 vgabios.c:494
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc0d17
    call 00cadh                               ; e8 90 ff                    ; 0xc0d1a
    mov di, si                                ; 89 f7                       ; 0xc0d1d
    inc si                                    ; 46                          ; 0xc0d1f
    push SS                                   ; 16                          ; 0xc0d20
    pop ES                                    ; 07                          ; 0xc0d21
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d22
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d25 vgabios.c:495
    inc cx                                    ; 41                          ; 0xc0d28 vgabios.c:496
    jmp short 00cfeh                          ; eb d3                       ; 0xc0d29
    mov bx, ax                                ; 89 c3                       ; 0xc0d2b vgabios.c:498
    mov word [bp-008h], 0b800h                ; c7 46 f8 00 b8              ; 0xc0d2d
    xor cx, cx                                ; 31 c9                       ; 0xc0d32 vgabios.c:499
    jmp short 00d3bh                          ; eb 05                       ; 0xc0d34
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc0d36
    jnl short 00d5fh                          ; 7d 24                       ; 0xc0d39
    mov di, si                                ; 89 f7                       ; 0xc0d3b vgabios.c:500
    inc si                                    ; 46                          ; 0xc0d3d
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d3e
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0d41
    push SS                                   ; 16                          ; 0xc0d44
    pop ES                                    ; 07                          ; 0xc0d45
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d46
    mov di, si                                ; 89 f7                       ; 0xc0d49 vgabios.c:501
    inc si                                    ; 46                          ; 0xc0d4b
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc0d4c
    mov al, byte [es:bx+02000h]               ; 26 8a 87 00 20              ; 0xc0d4f
    push SS                                   ; 16                          ; 0xc0d54
    pop ES                                    ; 07                          ; 0xc0d55
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc0d56
    add bx, strict byte 00050h                ; 83 c3 50                    ; 0xc0d59 vgabios.c:502
    inc cx                                    ; 41                          ; 0xc0d5c vgabios.c:503
    jmp short 00d36h                          ; eb d7                       ; 0xc0d5d
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc0d5f vgabios.c:505
    pop di                                    ; 5f                          ; 0xc0d62
    pop si                                    ; 5e                          ; 0xc0d63
    pop cx                                    ; 59                          ; 0xc0d64
    pop bp                                    ; 5d                          ; 0xc0d65
    retn                                      ; c3                          ; 0xc0d66
  ; disGetNextSymbol 0xc0d67 LB 0x37fd -> off=0x0 cb=000000000000001b uValue=00000000000c0d67 'vga_char_ofs_cga'
vga_char_ofs_cga:                            ; 0xc0d67 LB 0x1b
    push cx                                   ; 51                          ; 0xc0d67 vgabios.c:507
    push bp                                   ; 55                          ; 0xc0d68
    mov bp, sp                                ; 89 e5                       ; 0xc0d69
    mov cl, al                                ; 88 c1                       ; 0xc0d6b
    mov al, dl                                ; 88 d0                       ; 0xc0d6d
    xor ah, ah                                ; 30 e4                       ; 0xc0d6f vgabios.c:512
    mul bx                                    ; f7 e3                       ; 0xc0d71
    mov bx, ax                                ; 89 c3                       ; 0xc0d73
    sal bx, 1                                 ; d1 e3                       ; 0xc0d75
    sal bx, 1                                 ; d1 e3                       ; 0xc0d77
    mov al, cl                                ; 88 c8                       ; 0xc0d79
    xor ah, ah                                ; 30 e4                       ; 0xc0d7b
    add ax, bx                                ; 01 d8                       ; 0xc0d7d
    pop bp                                    ; 5d                          ; 0xc0d7f vgabios.c:513
    pop cx                                    ; 59                          ; 0xc0d80
    retn                                      ; c3                          ; 0xc0d81
  ; disGetNextSymbol 0xc0d82 LB 0x37e2 -> off=0x0 cb=000000000000006b uValue=00000000000c0d82 'vga_read_char_cga'
vga_read_char_cga:                           ; 0xc0d82 LB 0x6b
    push bp                                   ; 55                          ; 0xc0d82 vgabios.c:515
    mov bp, sp                                ; 89 e5                       ; 0xc0d83
    push bx                                   ; 53                          ; 0xc0d85
    push cx                                   ; 51                          ; 0xc0d86
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc0d87
    mov bl, dl                                ; 88 d3                       ; 0xc0d8a vgabios.c:521
    xor bh, bh                                ; 30 ff                       ; 0xc0d8c
    lea dx, [bp-00eh]                         ; 8d 56 f2                    ; 0xc0d8e
    call 00ce3h                               ; e8 4f ff                    ; 0xc0d91
    mov ax, strict word 00008h                ; b8 08 00                    ; 0xc0d94 vgabios.c:524
    push ax                                   ; 50                          ; 0xc0d97
    mov ax, 00080h                            ; b8 80 00                    ; 0xc0d98
    push ax                                   ; 50                          ; 0xc0d9b
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0d9c vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0d9f
    mov es, ax                                ; 8e c0                       ; 0xc0da1
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0da3
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0da6
    xor cx, cx                                ; 31 c9                       ; 0xc0daa vgabios.c:68
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0dac
    call 00ad7h                               ; e8 25 fd                    ; 0xc0daf
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0db2
    test ah, 080h                             ; f6 c4 80                    ; 0xc0db5 vgabios.c:526
    jne short 00de3h                          ; 75 29                       ; 0xc0db8
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0dba vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0dbd
    mov es, ax                                ; 8e c0                       ; 0xc0dbf
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0dc1
    mov dx, word [es:bx+002h]                 ; 26 8b 57 02                 ; 0xc0dc4
    test dx, dx                               ; 85 d2                       ; 0xc0dc8 vgabios.c:530
    jne short 00dd0h                          ; 75 04                       ; 0xc0dca
    test ax, ax                               ; 85 c0                       ; 0xc0dcc
    je short 00de3h                           ; 74 13                       ; 0xc0dce
    mov bx, strict word 00008h                ; bb 08 00                    ; 0xc0dd0 vgabios.c:531
    push bx                                   ; 53                          ; 0xc0dd3
    mov bx, 00080h                            ; bb 80 00                    ; 0xc0dd4
    push bx                                   ; 53                          ; 0xc0dd7
    mov cx, bx                                ; 89 d9                       ; 0xc0dd8
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc0dda
    call 00ad7h                               ; e8 f7 fc                    ; 0xc0ddd
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc0de0
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc0de3 vgabios.c:534
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc0de6
    pop cx                                    ; 59                          ; 0xc0de9
    pop bx                                    ; 5b                          ; 0xc0dea
    pop bp                                    ; 5d                          ; 0xc0deb
    retn                                      ; c3                          ; 0xc0dec
  ; disGetNextSymbol 0xc0ded LB 0x3777 -> off=0x0 cb=0000000000000147 uValue=00000000000c0ded 'vga_read_char_attr'
vga_read_char_attr:                          ; 0xc0ded LB 0x147
    push bp                                   ; 55                          ; 0xc0ded vgabios.c:536
    mov bp, sp                                ; 89 e5                       ; 0xc0dee
    push bx                                   ; 53                          ; 0xc0df0
    push cx                                   ; 51                          ; 0xc0df1
    push si                                   ; 56                          ; 0xc0df2
    push di                                   ; 57                          ; 0xc0df3
    sub sp, strict byte 00012h                ; 83 ec 12                    ; 0xc0df4
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc0df7
    mov si, dx                                ; 89 d6                       ; 0xc0dfa
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0dfc vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0dff
    mov es, ax                                ; 8e c0                       ; 0xc0e02
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0e04
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc0e07 vgabios.c:48
    xor ah, ah                                ; 30 e4                       ; 0xc0e0a vgabios.c:544
    call 03888h                               ; e8 79 2a                    ; 0xc0e0c
    mov cl, al                                ; 88 c1                       ; 0xc0e0f
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0e11 vgabios.c:545
    jne short 00e18h                          ; 75 03                       ; 0xc0e13
    jmp near 00f2bh                           ; e9 13 01                    ; 0xc0e15
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc0e18 vgabios.c:549
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc0e1b
    mov byte [bp-013h], 000h                  ; c6 46 ed 00                 ; 0xc0e1e
    lea bx, [bp-01ah]                         ; 8d 5e e6                    ; 0xc0e22
    lea dx, [bp-018h]                         ; 8d 56 e8                    ; 0xc0e25
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc0e28
    call 00a97h                               ; e8 69 fc                    ; 0xc0e2b
    mov ch, byte [bp-01ah]                    ; 8a 6e e6                    ; 0xc0e2e vgabios.c:550
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc0e31 vgabios.c:551
    mov al, ah                                ; 88 e0                       ; 0xc0e34
    xor ah, ah                                ; 30 e4                       ; 0xc0e36
    mov word [bp-012h], ax                    ; 89 46 ee                    ; 0xc0e38
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc0e3b
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc0e3e
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0e41 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0e44
    mov es, ax                                ; 8e c0                       ; 0xc0e47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0e49
    xor ah, ah                                ; 30 e4                       ; 0xc0e4c vgabios.c:48
    mov dx, ax                                ; 89 c2                       ; 0xc0e4e
    inc dx                                    ; 42                          ; 0xc0e50
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc0e51 vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc0e54
    mov word [bp-016h], di                    ; 89 7e ea                    ; 0xc0e57 vgabios.c:58
    mov bl, cl                                ; 88 cb                       ; 0xc0e5a vgabios.c:557
    xor bh, bh                                ; 30 ff                       ; 0xc0e5c
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc0e5e
    sal bx, CL                                ; d3 e3                       ; 0xc0e60
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc0e62
    jne short 00e9fh                          ; 75 36                       ; 0xc0e67
    mov ax, di                                ; 89 f8                       ; 0xc0e69 vgabios.c:559
    mul dx                                    ; f7 e2                       ; 0xc0e6b
    sal ax, 1                                 ; d1 e0                       ; 0xc0e6d
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc0e6f
    mov dl, byte [bp-00ch]                    ; 8a 56 f4                    ; 0xc0e71
    xor dh, dh                                ; 30 f6                       ; 0xc0e74
    inc ax                                    ; 40                          ; 0xc0e76
    mul dx                                    ; f7 e2                       ; 0xc0e77
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc0e79
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc0e7c
    xor ah, ah                                ; 30 e4                       ; 0xc0e7f
    mul di                                    ; f7 e7                       ; 0xc0e81
    mov dl, ch                                ; 88 ea                       ; 0xc0e83
    xor dh, dh                                ; 30 f6                       ; 0xc0e85
    add ax, dx                                ; 01 d0                       ; 0xc0e87
    sal ax, 1                                 ; d1 e0                       ; 0xc0e89
    mov di, word [bp-010h]                    ; 8b 7e f0                    ; 0xc0e8b
    add di, ax                                ; 01 c7                       ; 0xc0e8e
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc0e90 vgabios.c:55
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc0e94
    push SS                                   ; 16                          ; 0xc0e97 vgabios.c:58
    pop ES                                    ; 07                          ; 0xc0e98
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0e99
    jmp near 00f2bh                           ; e9 8c 00                    ; 0xc0e9c vgabios.c:561
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc0e9f vgabios.c:562
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc0ea3
    je short 00efeh                           ; 74 56                       ; 0xc0ea6
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc0ea8
    jc short 00eb4h                           ; 72 07                       ; 0xc0eab
    jbe short 00eb6h                          ; 76 07                       ; 0xc0ead
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc0eaf
    jbe short 00ed1h                          ; 76 1d                       ; 0xc0eb2
    jmp short 00f2bh                          ; eb 75                       ; 0xc0eb4
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc0eb6 vgabios.c:565
    xor dh, dh                                ; 30 f6                       ; 0xc0eb9
    mov al, ch                                ; 88 e8                       ; 0xc0ebb
    xor ah, ah                                ; 30 e4                       ; 0xc0ebd
    mov bx, word [bp-016h]                    ; 8b 5e ea                    ; 0xc0ebf
    call 00d67h                               ; e8 a2 fe                    ; 0xc0ec2
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc0ec5 vgabios.c:566
    xor dh, dh                                ; 30 f6                       ; 0xc0ec8
    call 00d82h                               ; e8 b5 fe                    ; 0xc0eca
    xor ah, ah                                ; 30 e4                       ; 0xc0ecd
    jmp short 00e97h                          ; eb c6                       ; 0xc0ecf
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0ed1 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0ed4
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc0ed7 vgabios.c:571
    mov byte [bp-00fh], 000h                  ; c6 46 f1 00                 ; 0xc0eda
    push word [bp-010h]                       ; ff 76 f0                    ; 0xc0ede
    mov dl, byte [bp-012h]                    ; 8a 56 ee                    ; 0xc0ee1
    xor dh, dh                                ; 30 f6                       ; 0xc0ee4
    mov al, ch                                ; 88 e8                       ; 0xc0ee6
    xor ah, ah                                ; 30 e4                       ; 0xc0ee8
    mov cx, word [bp-014h]                    ; 8b 4e ec                    ; 0xc0eea
    mov bx, di                                ; 89 fb                       ; 0xc0eed
    call 00b7bh                               ; e8 89 fc                    ; 0xc0eef
    mov bx, word [bp-010h]                    ; 8b 5e f0                    ; 0xc0ef2 vgabios.c:572
    mov dx, ax                                ; 89 c2                       ; 0xc0ef5
    mov ax, di                                ; 89 f8                       ; 0xc0ef7
    call 00baah                               ; e8 ae fc                    ; 0xc0ef9
    jmp short 00ecdh                          ; eb cf                       ; 0xc0efc
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0efe vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc0f01
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc0f04 vgabios.c:576
    mov byte [bp-00fh], 000h                  ; c6 46 f1 00                 ; 0xc0f07
    push word [bp-010h]                       ; ff 76 f0                    ; 0xc0f0b
    mov dl, byte [bp-012h]                    ; 8a 56 ee                    ; 0xc0f0e
    xor dh, dh                                ; 30 f6                       ; 0xc0f11
    mov al, ch                                ; 88 e8                       ; 0xc0f13
    xor ah, ah                                ; 30 e4                       ; 0xc0f15
    mov cx, word [bp-014h]                    ; 8b 4e ec                    ; 0xc0f17
    mov bx, di                                ; 89 fb                       ; 0xc0f1a
    call 00befh                               ; e8 d0 fc                    ; 0xc0f1c
    mov bx, word [bp-010h]                    ; 8b 5e f0                    ; 0xc0f1f vgabios.c:577
    mov dx, ax                                ; 89 c2                       ; 0xc0f22
    mov ax, di                                ; 89 f8                       ; 0xc0f24
    call 00c64h                               ; e8 3b fd                    ; 0xc0f26
    jmp short 00ecdh                          ; eb a2                       ; 0xc0f29
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc0f2b vgabios.c:586
    pop di                                    ; 5f                          ; 0xc0f2e
    pop si                                    ; 5e                          ; 0xc0f2f
    pop cx                                    ; 59                          ; 0xc0f30
    pop bx                                    ; 5b                          ; 0xc0f31
    pop bp                                    ; 5d                          ; 0xc0f32
    retn                                      ; c3                          ; 0xc0f33
  ; disGetNextSymbol 0xc0f34 LB 0x3630 -> off=0x10 cb=0000000000000083 uValue=00000000000c0f44 'vga_get_font_info'
    db  05bh, 00fh, 0a0h, 00fh, 0a5h, 00fh, 0ach, 00fh, 0b1h, 00fh, 0b6h, 00fh, 0bbh, 00fh, 0c0h, 00fh
vga_get_font_info:                           ; 0xc0f44 LB 0x83
    push si                                   ; 56                          ; 0xc0f44 vgabios.c:588
    push di                                   ; 57                          ; 0xc0f45
    push bp                                   ; 55                          ; 0xc0f46
    mov bp, sp                                ; 89 e5                       ; 0xc0f47
    mov si, dx                                ; 89 d6                       ; 0xc0f49
    mov di, bx                                ; 89 df                       ; 0xc0f4b
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc0f4d vgabios.c:593
    jnbe short 00f9ah                         ; 77 48                       ; 0xc0f50
    mov bx, ax                                ; 89 c3                       ; 0xc0f52
    sal bx, 1                                 ; d1 e3                       ; 0xc0f54
    jmp word [cs:bx+00f34h]                   ; 2e ff a7 34 0f              ; 0xc0f56
    mov bx, strict word 0007ch                ; bb 7c 00                    ; 0xc0f5b vgabios.c:67
    xor ax, ax                                ; 31 c0                       ; 0xc0f5e
    mov es, ax                                ; 8e c0                       ; 0xc0f60
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc0f62
    mov ax, word [es:bx+002h]                 ; 26 8b 47 02                 ; 0xc0f65
    push SS                                   ; 16                          ; 0xc0f69 vgabios.c:596
    pop ES                                    ; 07                          ; 0xc0f6a
    mov word [es:di], dx                      ; 26 89 15                    ; 0xc0f6b
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc0f6e
    mov bx, 00085h                            ; bb 85 00                    ; 0xc0f71
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f74
    mov es, ax                                ; 8e c0                       ; 0xc0f77
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f79
    xor ah, ah                                ; 30 e4                       ; 0xc0f7c
    push SS                                   ; 16                          ; 0xc0f7e
    pop ES                                    ; 07                          ; 0xc0f7f
    mov bx, cx                                ; 89 cb                       ; 0xc0f80
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f82
    mov bx, 00084h                            ; bb 84 00                    ; 0xc0f85
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0f88
    mov es, ax                                ; 8e c0                       ; 0xc0f8b
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0f8d
    xor ah, ah                                ; 30 e4                       ; 0xc0f90
    push SS                                   ; 16                          ; 0xc0f92
    pop ES                                    ; 07                          ; 0xc0f93
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc0f94
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc0f97
    pop bp                                    ; 5d                          ; 0xc0f9a
    pop di                                    ; 5f                          ; 0xc0f9b
    pop si                                    ; 5e                          ; 0xc0f9c
    retn 00002h                               ; c2 02 00                    ; 0xc0f9d
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc0fa0 vgabios.c:67
    jmp short 00f5eh                          ; eb b9                       ; 0xc0fa3
    mov dx, 05d6fh                            ; ba 6f 5d                    ; 0xc0fa5 vgabios.c:601
    mov ax, ds                                ; 8c d8                       ; 0xc0fa8
    jmp short 00f69h                          ; eb bd                       ; 0xc0faa vgabios.c:602
    mov dx, 0556fh                            ; ba 6f 55                    ; 0xc0fac vgabios.c:604
    jmp short 00fa8h                          ; eb f7                       ; 0xc0faf
    mov dx, 0596fh                            ; ba 6f 59                    ; 0xc0fb1 vgabios.c:607
    jmp short 00fa8h                          ; eb f2                       ; 0xc0fb4
    mov dx, 07b6fh                            ; ba 6f 7b                    ; 0xc0fb6 vgabios.c:610
    jmp short 00fa8h                          ; eb ed                       ; 0xc0fb9
    mov dx, 06b6fh                            ; ba 6f 6b                    ; 0xc0fbb vgabios.c:613
    jmp short 00fa8h                          ; eb e8                       ; 0xc0fbe
    mov dx, 07c9ch                            ; ba 9c 7c                    ; 0xc0fc0 vgabios.c:616
    jmp short 00fa8h                          ; eb e3                       ; 0xc0fc3
    jmp short 00f9ah                          ; eb d3                       ; 0xc0fc5 vgabios.c:622
  ; disGetNextSymbol 0xc0fc7 LB 0x359d -> off=0x0 cb=000000000000016d uValue=00000000000c0fc7 'vga_read_pixel'
vga_read_pixel:                              ; 0xc0fc7 LB 0x16d
    push bp                                   ; 55                          ; 0xc0fc7 vgabios.c:635
    mov bp, sp                                ; 89 e5                       ; 0xc0fc8
    push si                                   ; 56                          ; 0xc0fca
    push di                                   ; 57                          ; 0xc0fcb
    sub sp, strict byte 0000ch                ; 83 ec 0c                    ; 0xc0fcc
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc0fcf
    mov si, dx                                ; 89 d6                       ; 0xc0fd2
    mov word [bp-010h], bx                    ; 89 5e f0                    ; 0xc0fd4
    mov word [bp-00eh], cx                    ; 89 4e f2                    ; 0xc0fd7
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc0fda vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc0fdd
    mov es, ax                                ; 8e c0                       ; 0xc0fe0
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc0fe2
    xor ah, ah                                ; 30 e4                       ; 0xc0fe5 vgabios.c:642
    call 03888h                               ; e8 9e 28                    ; 0xc0fe7
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc0fea
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc0fed vgabios.c:643
    je short 01000h                           ; 74 0f                       ; 0xc0fef
    mov bl, al                                ; 88 c3                       ; 0xc0ff1 vgabios.c:645
    xor bh, bh                                ; 30 ff                       ; 0xc0ff3
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc0ff5
    sal bx, CL                                ; d3 e3                       ; 0xc0ff7
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc0ff9
    jne short 01003h                          ; 75 03                       ; 0xc0ffe
    jmp near 0112dh                           ; e9 2a 01                    ; 0xc1000 vgabios.c:646
    mov ch, byte [bx+047b3h]                  ; 8a af b3 47                 ; 0xc1003 vgabios.c:649
    cmp ch, cl                                ; 38 cd                       ; 0xc1007
    jc short 0101ah                           ; 72 0f                       ; 0xc1009
    jbe short 01022h                          ; 76 15                       ; 0xc100b
    cmp ch, 005h                              ; 80 fd 05                    ; 0xc100d
    je short 0105bh                           ; 74 49                       ; 0xc1010
    cmp ch, 004h                              ; 80 fd 04                    ; 0xc1012
    je short 01022h                           ; 74 0b                       ; 0xc1015
    jmp near 01123h                           ; e9 09 01                    ; 0xc1017
    cmp ch, 002h                              ; 80 fd 02                    ; 0xc101a
    je short 0108fh                           ; 74 70                       ; 0xc101d
    jmp near 01123h                           ; e9 01 01                    ; 0xc101f
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1022 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1025
    mov es, ax                                ; 8e c0                       ; 0xc1028
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc102a
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc102d vgabios.c:58
    mul bx                                    ; f7 e3                       ; 0xc1030
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1032
    mov bx, si                                ; 89 f3                       ; 0xc1034
    shr bx, CL                                ; d3 eb                       ; 0xc1036
    add bx, ax                                ; 01 c3                       ; 0xc1038
    mov di, strict word 0004ch                ; bf 4c 00                    ; 0xc103a vgabios.c:57
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc103d
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc1040 vgabios.c:58
    xor ch, ch                                ; 30 ed                       ; 0xc1043
    mul cx                                    ; f7 e1                       ; 0xc1045
    add bx, ax                                ; 01 c3                       ; 0xc1047
    mov cx, si                                ; 89 f1                       ; 0xc1049 vgabios.c:654
    and cx, strict byte 00007h                ; 83 e1 07                    ; 0xc104b
    mov ax, 00080h                            ; b8 80 00                    ; 0xc104e
    sar ax, CL                                ; d3 f8                       ; 0xc1051
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1053
    mov byte [bp-008h], ch                    ; 88 6e f8                    ; 0xc1056 vgabios.c:656
    jmp short 01064h                          ; eb 09                       ; 0xc1059
    jmp near 01103h                           ; e9 a5 00                    ; 0xc105b
    cmp byte [bp-008h], 004h                  ; 80 7e f8 04                 ; 0xc105e
    jnc short 0108ch                          ; 73 28                       ; 0xc1062
    mov ah, byte [bp-008h]                    ; 8a 66 f8                    ; 0xc1064 vgabios.c:657
    xor al, al                                ; 30 c0                       ; 0xc1067
    or AL, strict byte 004h                   ; 0c 04                       ; 0xc1069
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc106b
    out DX, ax                                ; ef                          ; 0xc106e
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc106f vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc1072
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1074
    and al, byte [bp-00ah]                    ; 22 46 f6                    ; 0xc1077 vgabios.c:48
    test al, al                               ; 84 c0                       ; 0xc107a vgabios.c:659
    jbe short 01087h                          ; 76 09                       ; 0xc107c
    mov cl, byte [bp-008h]                    ; 8a 4e f8                    ; 0xc107e vgabios.c:660
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc1081
    sal al, CL                                ; d2 e0                       ; 0xc1083
    or ch, al                                 ; 08 c5                       ; 0xc1085
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc1087 vgabios.c:661
    jmp short 0105eh                          ; eb d2                       ; 0xc108a
    jmp near 01125h                           ; e9 96 00                    ; 0xc108c
    mov al, byte [bx+047b4h]                  ; 8a 87 b4 47                 ; 0xc108f vgabios.c:664
    xor ah, ah                                ; 30 e4                       ; 0xc1093
    mov cx, strict word 00004h                ; b9 04 00                    ; 0xc1095
    sub cx, ax                                ; 29 c1                       ; 0xc1098
    mov ax, dx                                ; 89 d0                       ; 0xc109a
    shr ax, CL                                ; d3 e8                       ; 0xc109c
    mov cx, ax                                ; 89 c1                       ; 0xc109e
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc10a0
    shr ax, 1                                 ; d1 e8                       ; 0xc10a3
    mov bx, strict word 00050h                ; bb 50 00                    ; 0xc10a5
    mul bx                                    ; f7 e3                       ; 0xc10a8
    mov bx, cx                                ; 89 cb                       ; 0xc10aa
    add bx, ax                                ; 01 c3                       ; 0xc10ac
    test byte [bp-010h], 001h                 ; f6 46 f0 01                 ; 0xc10ae vgabios.c:665
    je short 010b7h                           ; 74 03                       ; 0xc10b2
    add bh, 020h                              ; 80 c7 20                    ; 0xc10b4 vgabios.c:666
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc10b7 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc10ba
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc10bc
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc10bf vgabios.c:668
    xor bh, bh                                ; 30 ff                       ; 0xc10c2
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc10c4
    sal bx, CL                                ; d3 e3                       ; 0xc10c6
    cmp byte [bx+047b4h], 002h                ; 80 bf b4 47 02              ; 0xc10c8
    jne short 010eah                          ; 75 1b                       ; 0xc10cd
    mov cx, si                                ; 89 f1                       ; 0xc10cf vgabios.c:669
    xor ch, ch                                ; 30 ed                       ; 0xc10d1
    and cl, 003h                              ; 80 e1 03                    ; 0xc10d3
    mov bx, strict word 00003h                ; bb 03 00                    ; 0xc10d6
    sub bx, cx                                ; 29 cb                       ; 0xc10d9
    mov cx, bx                                ; 89 d9                       ; 0xc10db
    sal cx, 1                                 ; d1 e1                       ; 0xc10dd
    xor ah, ah                                ; 30 e4                       ; 0xc10df
    sar ax, CL                                ; d3 f8                       ; 0xc10e1
    mov ch, al                                ; 88 c5                       ; 0xc10e3
    and ch, 003h                              ; 80 e5 03                    ; 0xc10e5
    jmp short 01125h                          ; eb 3b                       ; 0xc10e8 vgabios.c:670
    mov cx, si                                ; 89 f1                       ; 0xc10ea vgabios.c:671
    xor ch, ch                                ; 30 ed                       ; 0xc10ec
    and cl, 007h                              ; 80 e1 07                    ; 0xc10ee
    mov bx, strict word 00007h                ; bb 07 00                    ; 0xc10f1
    sub bx, cx                                ; 29 cb                       ; 0xc10f4
    mov cx, bx                                ; 89 d9                       ; 0xc10f6
    xor ah, ah                                ; 30 e4                       ; 0xc10f8
    sar ax, CL                                ; d3 f8                       ; 0xc10fa
    mov ch, al                                ; 88 c5                       ; 0xc10fc
    and ch, 001h                              ; 80 e5 01                    ; 0xc10fe
    jmp short 01125h                          ; eb 22                       ; 0xc1101 vgabios.c:672
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1103 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1106
    mov es, ax                                ; 8e c0                       ; 0xc1109
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc110b
    sal bx, CL                                ; d3 e3                       ; 0xc110e vgabios.c:58
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc1110
    mul bx                                    ; f7 e3                       ; 0xc1113
    mov bx, si                                ; 89 f3                       ; 0xc1115
    add bx, ax                                ; 01 c3                       ; 0xc1117
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc1119 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc111c
    mov ch, byte [es:bx]                      ; 26 8a 2f                    ; 0xc111e
    jmp short 01125h                          ; eb 02                       ; 0xc1121 vgabios.c:676
    xor ch, ch                                ; 30 ed                       ; 0xc1123 vgabios.c:681
    push SS                                   ; 16                          ; 0xc1125 vgabios.c:683
    pop ES                                    ; 07                          ; 0xc1126
    mov bx, word [bp-00eh]                    ; 8b 5e f2                    ; 0xc1127
    mov byte [es:bx], ch                      ; 26 88 2f                    ; 0xc112a
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc112d vgabios.c:684
    pop di                                    ; 5f                          ; 0xc1130
    pop si                                    ; 5e                          ; 0xc1131
    pop bp                                    ; 5d                          ; 0xc1132
    retn                                      ; c3                          ; 0xc1133
  ; disGetNextSymbol 0xc1134 LB 0x3430 -> off=0x0 cb=000000000000009f uValue=00000000000c1134 'biosfn_perform_gray_scale_summing'
biosfn_perform_gray_scale_summing:           ; 0xc1134 LB 0x9f
    push bp                                   ; 55                          ; 0xc1134 vgabios.c:689
    mov bp, sp                                ; 89 e5                       ; 0xc1135
    push bx                                   ; 53                          ; 0xc1137
    push cx                                   ; 51                          ; 0xc1138
    push si                                   ; 56                          ; 0xc1139
    push di                                   ; 57                          ; 0xc113a
    push ax                                   ; 50                          ; 0xc113b
    push ax                                   ; 50                          ; 0xc113c
    mov bx, ax                                ; 89 c3                       ; 0xc113d
    mov di, dx                                ; 89 d7                       ; 0xc113f
    mov dx, 003dah                            ; ba da 03                    ; 0xc1141 vgabios.c:694
    in AL, DX                                 ; ec                          ; 0xc1144
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1145
    xor al, al                                ; 30 c0                       ; 0xc1147 vgabios.c:695
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc1149
    out DX, AL                                ; ee                          ; 0xc114c
    xor si, si                                ; 31 f6                       ; 0xc114d vgabios.c:697
    cmp si, di                                ; 39 fe                       ; 0xc114f
    jnc short 011b8h                          ; 73 65                       ; 0xc1151
    mov al, bl                                ; 88 d8                       ; 0xc1153 vgabios.c:700
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc1155
    out DX, AL                                ; ee                          ; 0xc1158
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1159 vgabios.c:702
    in AL, DX                                 ; ec                          ; 0xc115c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc115d
    mov cx, ax                                ; 89 c1                       ; 0xc115f
    in AL, DX                                 ; ec                          ; 0xc1161 vgabios.c:703
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1162
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc1164
    in AL, DX                                 ; ec                          ; 0xc1167 vgabios.c:704
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1168
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc116a
    mov al, cl                                ; 88 c8                       ; 0xc116d vgabios.c:707
    xor ah, ah                                ; 30 e4                       ; 0xc116f
    mov cx, strict word 0004dh                ; b9 4d 00                    ; 0xc1171
    imul cx                                   ; f7 e9                       ; 0xc1174
    mov cx, ax                                ; 89 c1                       ; 0xc1176
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1178
    xor ah, ah                                ; 30 e4                       ; 0xc117b
    mov dx, 00097h                            ; ba 97 00                    ; 0xc117d
    imul dx                                   ; f7 ea                       ; 0xc1180
    add cx, ax                                ; 01 c1                       ; 0xc1182
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc1184
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc1187
    xor ch, ch                                ; 30 ed                       ; 0xc118a
    mov ax, cx                                ; 89 c8                       ; 0xc118c
    mov dx, strict word 0001ch                ; ba 1c 00                    ; 0xc118e
    imul dx                                   ; f7 ea                       ; 0xc1191
    add ax, word [bp-00ah]                    ; 03 46 f6                    ; 0xc1193
    add ax, 00080h                            ; 05 80 00                    ; 0xc1196
    mov al, ah                                ; 88 e0                       ; 0xc1199
    cbw                                       ; 98                          ; 0xc119b
    mov cx, ax                                ; 89 c1                       ; 0xc119c
    cmp ax, strict word 0003fh                ; 3d 3f 00                    ; 0xc119e vgabios.c:709
    jbe short 011a6h                          ; 76 03                       ; 0xc11a1
    mov cx, strict word 0003fh                ; b9 3f 00                    ; 0xc11a3
    mov al, bl                                ; 88 d8                       ; 0xc11a6 vgabios.c:712
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc11a8
    out DX, AL                                ; ee                          ; 0xc11ab
    mov al, cl                                ; 88 c8                       ; 0xc11ac vgabios.c:714
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc11ae
    out DX, AL                                ; ee                          ; 0xc11b1
    out DX, AL                                ; ee                          ; 0xc11b2 vgabios.c:715
    out DX, AL                                ; ee                          ; 0xc11b3 vgabios.c:716
    inc bx                                    ; 43                          ; 0xc11b4 vgabios.c:717
    inc si                                    ; 46                          ; 0xc11b5 vgabios.c:718
    jmp short 0114fh                          ; eb 97                       ; 0xc11b6
    mov dx, 003dah                            ; ba da 03                    ; 0xc11b8 vgabios.c:719
    in AL, DX                                 ; ec                          ; 0xc11bb
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc11bc
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc11be vgabios.c:720
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc11c0
    out DX, AL                                ; ee                          ; 0xc11c3
    mov dx, 003dah                            ; ba da 03                    ; 0xc11c4 vgabios.c:722
    in AL, DX                                 ; ec                          ; 0xc11c7
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc11c8
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc11ca vgabios.c:724
    pop di                                    ; 5f                          ; 0xc11cd
    pop si                                    ; 5e                          ; 0xc11ce
    pop cx                                    ; 59                          ; 0xc11cf
    pop bx                                    ; 5b                          ; 0xc11d0
    pop bp                                    ; 5d                          ; 0xc11d1
    retn                                      ; c3                          ; 0xc11d2
  ; disGetNextSymbol 0xc11d3 LB 0x3391 -> off=0x0 cb=00000000000000fc uValue=00000000000c11d3 'biosfn_set_cursor_shape'
biosfn_set_cursor_shape:                     ; 0xc11d3 LB 0xfc
    push bp                                   ; 55                          ; 0xc11d3 vgabios.c:727
    mov bp, sp                                ; 89 e5                       ; 0xc11d4
    push bx                                   ; 53                          ; 0xc11d6
    push cx                                   ; 51                          ; 0xc11d7
    push si                                   ; 56                          ; 0xc11d8
    push ax                                   ; 50                          ; 0xc11d9
    push ax                                   ; 50                          ; 0xc11da
    mov ah, al                                ; 88 c4                       ; 0xc11db
    mov bl, dl                                ; 88 d3                       ; 0xc11dd
    mov dh, al                                ; 88 c6                       ; 0xc11df vgabios.c:733
    mov si, strict word 00060h                ; be 60 00                    ; 0xc11e1 vgabios.c:62
    mov cx, strict word 00040h                ; b9 40 00                    ; 0xc11e4
    mov es, cx                                ; 8e c1                       ; 0xc11e7
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc11e9
    mov si, 00087h                            ; be 87 00                    ; 0xc11ec vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc11ef
    test dl, 008h                             ; f6 c2 08                    ; 0xc11f2 vgabios.c:48
    jne short 01234h                          ; 75 3d                       ; 0xc11f5
    mov dl, al                                ; 88 c2                       ; 0xc11f7 vgabios.c:739
    and dl, 060h                              ; 80 e2 60                    ; 0xc11f9
    cmp dl, 020h                              ; 80 fa 20                    ; 0xc11fc
    jne short 01207h                          ; 75 06                       ; 0xc11ff
    mov AH, strict byte 01eh                  ; b4 1e                       ; 0xc1201 vgabios.c:741
    xor bl, bl                                ; 30 db                       ; 0xc1203 vgabios.c:742
    jmp short 01234h                          ; eb 2d                       ; 0xc1205 vgabios.c:743
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc1207 vgabios.c:47
    test dl, 001h                             ; f6 c2 01                    ; 0xc120a vgabios.c:48
    jne short 01269h                          ; 75 5a                       ; 0xc120d
    cmp ah, 020h                              ; 80 fc 20                    ; 0xc120f
    jnc short 01269h                          ; 73 55                       ; 0xc1212
    cmp bl, 020h                              ; 80 fb 20                    ; 0xc1214
    jnc short 01269h                          ; 73 50                       ; 0xc1217
    mov si, 00085h                            ; be 85 00                    ; 0xc1219 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc121c
    mov es, dx                                ; 8e c2                       ; 0xc121f
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc1221
    mov dx, cx                                ; 89 ca                       ; 0xc1224 vgabios.c:58
    cmp bl, ah                                ; 38 e3                       ; 0xc1226 vgabios.c:754
    jnc short 01236h                          ; 73 0c                       ; 0xc1228
    test bl, bl                               ; 84 db                       ; 0xc122a vgabios.c:756
    je short 01269h                           ; 74 3b                       ; 0xc122c
    xor ah, ah                                ; 30 e4                       ; 0xc122e vgabios.c:757
    mov bl, cl                                ; 88 cb                       ; 0xc1230 vgabios.c:758
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc1232
    jmp short 01269h                          ; eb 33                       ; 0xc1234 vgabios.c:760
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc1236 vgabios.c:761
    xor al, al                                ; 30 c0                       ; 0xc1239
    mov byte [bp-007h], al                    ; 88 46 f9                    ; 0xc123b
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc123e
    mov byte [bp-009h], al                    ; 88 46 f7                    ; 0xc1241
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc1244
    or si, word [bp-00ah]                     ; 0b 76 f6                    ; 0xc1247
    cmp si, cx                                ; 39 ce                       ; 0xc124a
    jnc short 0126bh                          ; 73 1d                       ; 0xc124c
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc124e
    mov byte [bp-007h], al                    ; 88 46 f9                    ; 0xc1251
    mov si, cx                                ; 89 ce                       ; 0xc1254
    dec si                                    ; 4e                          ; 0xc1256
    cmp si, word [bp-008h]                    ; 3b 76 f8                    ; 0xc1257
    je short 012a5h                           ; 74 49                       ; 0xc125a
    mov byte [bp-008h], ah                    ; 88 66 f8                    ; 0xc125c
    mov byte [bp-007h], al                    ; 88 46 f9                    ; 0xc125f
    dec cx                                    ; 49                          ; 0xc1262
    dec cx                                    ; 49                          ; 0xc1263
    cmp cx, word [bp-008h]                    ; 3b 4e f8                    ; 0xc1264
    jne short 0126bh                          ; 75 02                       ; 0xc1267
    jmp short 012a5h                          ; eb 3a                       ; 0xc1269
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc126b vgabios.c:763
    jbe short 012a5h                          ; 76 35                       ; 0xc126e
    mov cl, ah                                ; 88 e1                       ; 0xc1270 vgabios.c:764
    xor ch, ch                                ; 30 ed                       ; 0xc1272
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc1274
    mov byte [bp-007h], ch                    ; 88 6e f9                    ; 0xc1277
    mov si, cx                                ; 89 ce                       ; 0xc127a
    inc si                                    ; 46                          ; 0xc127c
    inc si                                    ; 46                          ; 0xc127d
    mov cl, dl                                ; 88 d1                       ; 0xc127e
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc1280
    cmp si, word [bp-008h]                    ; 3b 76 f8                    ; 0xc1282
    jl short 0129ah                           ; 7c 13                       ; 0xc1285
    sub ah, bl                                ; 28 dc                       ; 0xc1287 vgabios.c:766
    add ah, dl                                ; 00 d4                       ; 0xc1289
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc128b
    mov bl, cl                                ; 88 cb                       ; 0xc128d vgabios.c:767
    cmp dx, strict byte 0000eh                ; 83 fa 0e                    ; 0xc128f vgabios.c:768
    jc short 012a5h                           ; 72 11                       ; 0xc1292
    db  0feh, 0cbh
    ; dec bl                                    ; fe cb                     ; 0xc1294 vgabios.c:770
    db  0feh, 0cch
    ; dec ah                                    ; fe cc                     ; 0xc1296 vgabios.c:771
    jmp short 012a5h                          ; eb 0b                       ; 0xc1298 vgabios.c:773
    cmp ah, 002h                              ; 80 fc 02                    ; 0xc129a
    jbe short 012a3h                          ; 76 04                       ; 0xc129d
    shr dx, 1                                 ; d1 ea                       ; 0xc129f vgabios.c:775
    mov ah, dl                                ; 88 d4                       ; 0xc12a1
    mov bl, cl                                ; 88 cb                       ; 0xc12a3 vgabios.c:779
    mov si, strict word 00063h                ; be 63 00                    ; 0xc12a5 vgabios.c:57
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc12a8
    mov es, dx                                ; 8e c2                       ; 0xc12ab
    mov cx, word [es:si]                      ; 26 8b 0c                    ; 0xc12ad
    mov AL, strict byte 00ah                  ; b0 0a                       ; 0xc12b0 vgabios.c:790
    mov dx, cx                                ; 89 ca                       ; 0xc12b2
    out DX, AL                                ; ee                          ; 0xc12b4
    mov si, cx                                ; 89 ce                       ; 0xc12b5 vgabios.c:791
    inc si                                    ; 46                          ; 0xc12b7
    mov al, ah                                ; 88 e0                       ; 0xc12b8
    mov dx, si                                ; 89 f2                       ; 0xc12ba
    out DX, AL                                ; ee                          ; 0xc12bc
    mov AL, strict byte 00bh                  ; b0 0b                       ; 0xc12bd vgabios.c:792
    mov dx, cx                                ; 89 ca                       ; 0xc12bf
    out DX, AL                                ; ee                          ; 0xc12c1
    mov al, bl                                ; 88 d8                       ; 0xc12c2 vgabios.c:793
    mov dx, si                                ; 89 f2                       ; 0xc12c4
    out DX, AL                                ; ee                          ; 0xc12c6
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc12c7 vgabios.c:794
    pop si                                    ; 5e                          ; 0xc12ca
    pop cx                                    ; 59                          ; 0xc12cb
    pop bx                                    ; 5b                          ; 0xc12cc
    pop bp                                    ; 5d                          ; 0xc12cd
    retn                                      ; c3                          ; 0xc12ce
  ; disGetNextSymbol 0xc12cf LB 0x3295 -> off=0x0 cb=000000000000008d uValue=00000000000c12cf 'biosfn_set_cursor_pos'
biosfn_set_cursor_pos:                       ; 0xc12cf LB 0x8d
    push bp                                   ; 55                          ; 0xc12cf vgabios.c:797
    mov bp, sp                                ; 89 e5                       ; 0xc12d0
    push bx                                   ; 53                          ; 0xc12d2
    push cx                                   ; 51                          ; 0xc12d3
    push si                                   ; 56                          ; 0xc12d4
    push di                                   ; 57                          ; 0xc12d5
    push ax                                   ; 50                          ; 0xc12d6
    mov bl, al                                ; 88 c3                       ; 0xc12d7
    mov cx, dx                                ; 89 d1                       ; 0xc12d9
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc12db vgabios.c:803
    jnbe short 01353h                         ; 77 74                       ; 0xc12dd
    xor ah, ah                                ; 30 e4                       ; 0xc12df vgabios.c:806
    mov si, ax                                ; 89 c6                       ; 0xc12e1
    sal si, 1                                 ; d1 e6                       ; 0xc12e3
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc12e5
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc12e8 vgabios.c:62
    mov es, ax                                ; 8e c0                       ; 0xc12eb
    mov word [es:si], dx                      ; 26 89 14                    ; 0xc12ed
    mov si, strict word 00062h                ; be 62 00                    ; 0xc12f0 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc12f3
    cmp bl, al                                ; 38 c3                       ; 0xc12f6 vgabios.c:810
    jne short 01353h                          ; 75 59                       ; 0xc12f8
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc12fa vgabios.c:57
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc12fd
    mov di, 00084h                            ; bf 84 00                    ; 0xc1300 vgabios.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc1303
    xor ah, ah                                ; 30 e4                       ; 0xc1306 vgabios.c:48
    mov di, ax                                ; 89 c7                       ; 0xc1308
    inc di                                    ; 47                          ; 0xc130a
    mov ax, dx                                ; 89 d0                       ; 0xc130b vgabios.c:816
    mov al, dh                                ; 88 f0                       ; 0xc130d
    xor ah, dh                                ; 30 f4                       ; 0xc130f
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc1311
    mov ax, si                                ; 89 f0                       ; 0xc1314 vgabios.c:819
    mul di                                    ; f7 e7                       ; 0xc1316
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc1318
    xor bh, bh                                ; 30 ff                       ; 0xc131a
    inc ax                                    ; 40                          ; 0xc131c
    mul bx                                    ; f7 e3                       ; 0xc131d
    mov bx, ax                                ; 89 c3                       ; 0xc131f
    mov al, cl                                ; 88 c8                       ; 0xc1321
    xor ah, ah                                ; 30 e4                       ; 0xc1323
    add bx, ax                                ; 01 c3                       ; 0xc1325
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1327
    mul si                                    ; f7 e6                       ; 0xc132a
    mov si, bx                                ; 89 de                       ; 0xc132c
    add si, ax                                ; 01 c6                       ; 0xc132e
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc1330 vgabios.c:57
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc1333
    mov AL, strict byte 00eh                  ; b0 0e                       ; 0xc1336 vgabios.c:823
    mov dx, bx                                ; 89 da                       ; 0xc1338
    out DX, AL                                ; ee                          ; 0xc133a
    mov ax, si                                ; 89 f0                       ; 0xc133b vgabios.c:824
    mov al, ah                                ; 88 e0                       ; 0xc133d
    lea cx, [bx+001h]                         ; 8d 4f 01                    ; 0xc133f
    mov dx, cx                                ; 89 ca                       ; 0xc1342
    out DX, AL                                ; ee                          ; 0xc1344
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc1345 vgabios.c:825
    mov dx, bx                                ; 89 da                       ; 0xc1347
    out DX, AL                                ; ee                          ; 0xc1349
    and si, 000ffh                            ; 81 e6 ff 00                 ; 0xc134a vgabios.c:826
    mov ax, si                                ; 89 f0                       ; 0xc134e
    mov dx, cx                                ; 89 ca                       ; 0xc1350
    out DX, AL                                ; ee                          ; 0xc1352
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc1353 vgabios.c:828
    pop di                                    ; 5f                          ; 0xc1356
    pop si                                    ; 5e                          ; 0xc1357
    pop cx                                    ; 59                          ; 0xc1358
    pop bx                                    ; 5b                          ; 0xc1359
    pop bp                                    ; 5d                          ; 0xc135a
    retn                                      ; c3                          ; 0xc135b
  ; disGetNextSymbol 0xc135c LB 0x3208 -> off=0x0 cb=00000000000000d5 uValue=00000000000c135c 'biosfn_set_active_page'
biosfn_set_active_page:                      ; 0xc135c LB 0xd5
    push bp                                   ; 55                          ; 0xc135c vgabios.c:831
    mov bp, sp                                ; 89 e5                       ; 0xc135d
    push bx                                   ; 53                          ; 0xc135f
    push cx                                   ; 51                          ; 0xc1360
    push dx                                   ; 52                          ; 0xc1361
    push si                                   ; 56                          ; 0xc1362
    push di                                   ; 57                          ; 0xc1363
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc1364
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc1367
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc136a vgabios.c:837
    jnbe short 01384h                         ; 77 16                       ; 0xc136c
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc136e vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1371
    mov es, ax                                ; 8e c0                       ; 0xc1374
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1376
    xor ah, ah                                ; 30 e4                       ; 0xc1379 vgabios.c:841
    call 03888h                               ; e8 0a 25                    ; 0xc137b
    mov cl, al                                ; 88 c1                       ; 0xc137e
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1380 vgabios.c:842
    jne short 01387h                          ; 75 03                       ; 0xc1382
    jmp near 01427h                           ; e9 a0 00                    ; 0xc1384
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1387 vgabios.c:845
    xor ah, ah                                ; 30 e4                       ; 0xc138a
    lea bx, [bp-010h]                         ; 8d 5e f0                    ; 0xc138c
    lea dx, [bp-00eh]                         ; 8d 56 f2                    ; 0xc138f
    call 00a97h                               ; e8 02 f7                    ; 0xc1392
    mov bl, cl                                ; 88 cb                       ; 0xc1395 vgabios.c:847
    xor bh, bh                                ; 30 ff                       ; 0xc1397
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1399
    mov si, bx                                ; 89 de                       ; 0xc139b
    sal si, CL                                ; d3 e6                       ; 0xc139d
    cmp byte [si+047b2h], 000h                ; 80 bc b2 47 00              ; 0xc139f
    jne short 013e1h                          ; 75 3b                       ; 0xc13a4
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc13a6 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc13a9
    mov es, ax                                ; 8e c0                       ; 0xc13ac
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc13ae
    mov bx, 00084h                            ; bb 84 00                    ; 0xc13b1 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc13b4
    xor ah, ah                                ; 30 e4                       ; 0xc13b7 vgabios.c:48
    mov bx, ax                                ; 89 c3                       ; 0xc13b9
    inc bx                                    ; 43                          ; 0xc13bb
    mov ax, dx                                ; 89 d0                       ; 0xc13bc vgabios.c:854
    mul bx                                    ; f7 e3                       ; 0xc13be
    mov di, ax                                ; 89 c7                       ; 0xc13c0
    sal ax, 1                                 ; d1 e0                       ; 0xc13c2
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc13c4
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc13c6
    xor bh, bh                                ; 30 ff                       ; 0xc13c9
    inc ax                                    ; 40                          ; 0xc13cb
    mul bx                                    ; f7 e3                       ; 0xc13cc
    mov cx, ax                                ; 89 c1                       ; 0xc13ce
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc13d0 vgabios.c:62
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc13d3
    or di, 000ffh                             ; 81 cf ff 00                 ; 0xc13d6 vgabios.c:858
    lea ax, [di+001h]                         ; 8d 45 01                    ; 0xc13da
    mul bx                                    ; f7 e3                       ; 0xc13dd
    jmp short 013f2h                          ; eb 11                       ; 0xc13df vgabios.c:860
    mov bl, byte [bx+04831h]                  ; 8a 9f 31 48                 ; 0xc13e1 vgabios.c:862
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc13e5
    sal bx, CL                                ; d3 e3                       ; 0xc13e7
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc13e9
    xor ah, ah                                ; 30 e4                       ; 0xc13ec
    mul word [bx+04848h]                      ; f7 a7 48 48                 ; 0xc13ee
    mov cx, ax                                ; 89 c1                       ; 0xc13f2
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc13f4 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc13f7
    mov es, ax                                ; 8e c0                       ; 0xc13fa
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc13fc
    mov AL, strict byte 00ch                  ; b0 0c                       ; 0xc13ff vgabios.c:867
    mov dx, bx                                ; 89 da                       ; 0xc1401
    out DX, AL                                ; ee                          ; 0xc1403
    mov al, ch                                ; 88 e8                       ; 0xc1404 vgabios.c:868
    lea si, [bx+001h]                         ; 8d 77 01                    ; 0xc1406
    mov dx, si                                ; 89 f2                       ; 0xc1409
    out DX, AL                                ; ee                          ; 0xc140b
    mov AL, strict byte 00dh                  ; b0 0d                       ; 0xc140c vgabios.c:869
    mov dx, bx                                ; 89 da                       ; 0xc140e
    out DX, AL                                ; ee                          ; 0xc1410
    xor ch, ch                                ; 30 ed                       ; 0xc1411 vgabios.c:870
    mov ax, cx                                ; 89 c8                       ; 0xc1413
    mov dx, si                                ; 89 f2                       ; 0xc1415
    out DX, AL                                ; ee                          ; 0xc1417
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc1418 vgabios.c:52
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc141b
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc141e
    mov dx, word [bp-010h]                    ; 8b 56 f0                    ; 0xc1421 vgabios.c:880
    call 012cfh                               ; e8 a8 fe                    ; 0xc1424
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc1427 vgabios.c:881
    pop di                                    ; 5f                          ; 0xc142a
    pop si                                    ; 5e                          ; 0xc142b
    pop dx                                    ; 5a                          ; 0xc142c
    pop cx                                    ; 59                          ; 0xc142d
    pop bx                                    ; 5b                          ; 0xc142e
    pop bp                                    ; 5d                          ; 0xc142f
    retn                                      ; c3                          ; 0xc1430
  ; disGetNextSymbol 0xc1431 LB 0x3133 -> off=0x0 cb=0000000000000048 uValue=00000000000c1431 'find_vpti'
find_vpti:                                   ; 0xc1431 LB 0x48
    push bx                                   ; 53                          ; 0xc1431 vgabios.c:916
    push cx                                   ; 51                          ; 0xc1432
    push si                                   ; 56                          ; 0xc1433
    push bp                                   ; 55                          ; 0xc1434
    mov bp, sp                                ; 89 e5                       ; 0xc1435
    mov bl, al                                ; 88 c3                       ; 0xc1437 vgabios.c:921
    xor bh, bh                                ; 30 ff                       ; 0xc1439
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc143b
    mov si, bx                                ; 89 de                       ; 0xc143d
    sal si, CL                                ; d3 e6                       ; 0xc143f
    cmp byte [si+047b2h], 000h                ; 80 bc b2 47 00              ; 0xc1441
    jne short 0146eh                          ; 75 26                       ; 0xc1446
    mov si, 00089h                            ; be 89 00                    ; 0xc1448 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc144b
    mov es, ax                                ; 8e c0                       ; 0xc144e
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc1450
    test AL, strict byte 010h                 ; a8 10                       ; 0xc1453 vgabios.c:923
    je short 0145dh                           ; 74 06                       ; 0xc1455
    mov al, byte [bx+07df8h]                  ; 8a 87 f8 7d                 ; 0xc1457 vgabios.c:924
    jmp short 0146bh                          ; eb 0e                       ; 0xc145b vgabios.c:925
    test AL, strict byte 080h                 ; a8 80                       ; 0xc145d
    je short 01467h                           ; 74 06                       ; 0xc145f
    mov al, byte [bx+07de8h]                  ; 8a 87 e8 7d                 ; 0xc1461 vgabios.c:926
    jmp short 0146bh                          ; eb 04                       ; 0xc1465 vgabios.c:927
    mov al, byte [bx+07df0h]                  ; 8a 87 f0 7d                 ; 0xc1467 vgabios.c:928
    cbw                                       ; 98                          ; 0xc146b
    jmp short 01474h                          ; eb 06                       ; 0xc146c vgabios.c:929
    mov al, byte [bx+04831h]                  ; 8a 87 31 48                 ; 0xc146e vgabios.c:930
    xor ah, ah                                ; 30 e4                       ; 0xc1472
    pop bp                                    ; 5d                          ; 0xc1474 vgabios.c:933
    pop si                                    ; 5e                          ; 0xc1475
    pop cx                                    ; 59                          ; 0xc1476
    pop bx                                    ; 5b                          ; 0xc1477
    retn                                      ; c3                          ; 0xc1478
  ; disGetNextSymbol 0xc1479 LB 0x30eb -> off=0x0 cb=00000000000004e4 uValue=00000000000c1479 'biosfn_set_video_mode'
biosfn_set_video_mode:                       ; 0xc1479 LB 0x4e4
    push bp                                   ; 55                          ; 0xc1479 vgabios.c:938
    mov bp, sp                                ; 89 e5                       ; 0xc147a
    push bx                                   ; 53                          ; 0xc147c
    push cx                                   ; 51                          ; 0xc147d
    push dx                                   ; 52                          ; 0xc147e
    push si                                   ; 56                          ; 0xc147f
    push di                                   ; 57                          ; 0xc1480
    sub sp, strict byte 00018h                ; 83 ec 18                    ; 0xc1481
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc1484
    and AL, strict byte 080h                  ; 24 80                       ; 0xc1487 vgabios.c:942
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc1489
    call 007f8h                               ; e8 69 f3                    ; 0xc148c vgabios.c:952
    test ax, ax                               ; 85 c0                       ; 0xc148f
    je short 0149fh                           ; 74 0c                       ; 0xc1491
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc1493 vgabios.c:954
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc1495
    out DX, AL                                ; ee                          ; 0xc1498
    xor al, al                                ; 30 c0                       ; 0xc1499 vgabios.c:955
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc149b
    out DX, AL                                ; ee                          ; 0xc149e
    and byte [bp-00ch], 07fh                  ; 80 66 f4 7f                 ; 0xc149f vgabios.c:960
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc14a3 vgabios.c:966
    xor ah, ah                                ; 30 e4                       ; 0xc14a6
    call 03888h                               ; e8 dd 23                    ; 0xc14a8
    mov dl, al                                ; 88 c2                       ; 0xc14ab
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc14ad
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc14b0 vgabios.c:972
    je short 01520h                           ; 74 6c                       ; 0xc14b2
    mov si, 000a8h                            ; be a8 00                    ; 0xc14b4 vgabios.c:67
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc14b7
    mov es, ax                                ; 8e c0                       ; 0xc14ba
    mov bx, word [es:si]                      ; 26 8b 1c                    ; 0xc14bc
    mov ax, word [es:si+002h]                 ; 26 8b 44 02                 ; 0xc14bf
    mov word [bp-014h], bx                    ; 89 5e ec                    ; 0xc14c3 vgabios.c:68
    mov word [bp-012h], ax                    ; 89 46 ee                    ; 0xc14c6
    xor dh, dh                                ; 30 f6                       ; 0xc14c9 vgabios.c:978
    mov ax, dx                                ; 89 d0                       ; 0xc14cb
    call 01431h                               ; e8 61 ff                    ; 0xc14cd
    mov es, [bp-012h]                         ; 8e 46 ee                    ; 0xc14d0 vgabios.c:979
    mov si, word [es:bx]                      ; 26 8b 37                    ; 0xc14d3
    mov bx, word [es:bx+002h]                 ; 26 8b 5f 02                 ; 0xc14d6
    mov word [bp-01ah], bx                    ; 89 5e e6                    ; 0xc14da
    xor ah, ah                                ; 30 e4                       ; 0xc14dd vgabios.c:980
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc14df
    sal ax, CL                                ; d3 e0                       ; 0xc14e1
    add si, ax                                ; 01 c6                       ; 0xc14e3
    mov bx, 00089h                            ; bb 89 00                    ; 0xc14e5 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc14e8
    mov es, ax                                ; 8e c0                       ; 0xc14eb
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc14ed
    mov ch, al                                ; 88 c5                       ; 0xc14f0 vgabios.c:48
    test AL, strict byte 008h                 ; a8 08                       ; 0xc14f2 vgabios.c:997
    jne short 0153ch                          ; 75 46                       ; 0xc14f4
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc14f6 vgabios.c:999
    mov bx, dx                                ; 89 d3                       ; 0xc14f8
    sal bx, CL                                ; d3 e3                       ; 0xc14fa
    mov al, byte [bx+047b7h]                  ; 8a 87 b7 47                 ; 0xc14fc
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc1500
    out DX, AL                                ; ee                          ; 0xc1503
    xor al, al                                ; 30 c0                       ; 0xc1504 vgabios.c:1002
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc1506
    out DX, AL                                ; ee                          ; 0xc1509
    mov bl, byte [bx+047b8h]                  ; 8a 9f b8 47                 ; 0xc150a vgabios.c:1005
    cmp bl, 001h                              ; 80 fb 01                    ; 0xc150e
    jc short 01523h                           ; 72 10                       ; 0xc1511
    jbe short 0152eh                          ; 76 19                       ; 0xc1513
    cmp bl, cl                                ; 38 cb                       ; 0xc1515
    je short 0153fh                           ; 74 26                       ; 0xc1517
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc1519
    je short 01535h                           ; 74 17                       ; 0xc151c
    jmp short 01544h                          ; eb 24                       ; 0xc151e
    jmp near 01953h                           ; e9 30 04                    ; 0xc1520
    test bl, bl                               ; 84 db                       ; 0xc1523
    jne short 01544h                          ; 75 1d                       ; 0xc1525
    mov word [bp-016h], 04fc5h                ; c7 46 ea c5 4f              ; 0xc1527 vgabios.c:1007
    jmp short 01544h                          ; eb 16                       ; 0xc152c vgabios.c:1008
    mov word [bp-016h], 05085h                ; c7 46 ea 85 50              ; 0xc152e vgabios.c:1010
    jmp short 01544h                          ; eb 0f                       ; 0xc1533 vgabios.c:1011
    mov word [bp-016h], 05145h                ; c7 46 ea 45 51              ; 0xc1535 vgabios.c:1013
    jmp short 01544h                          ; eb 08                       ; 0xc153a vgabios.c:1014
    jmp near 015b8h                           ; e9 79 00                    ; 0xc153c
    mov word [bp-016h], 05205h                ; c7 46 ea 05 52              ; 0xc153f vgabios.c:1016
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc1544 vgabios.c:1020
    xor bh, bh                                ; 30 ff                       ; 0xc1547
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1549
    sal bx, CL                                ; d3 e3                       ; 0xc154b
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc154d
    jne short 01563h                          ; 75 0f                       ; 0xc1552
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1554 vgabios.c:1022
    cmp byte [es:si+002h], 008h               ; 26 80 7c 02 08              ; 0xc1557
    jne short 01563h                          ; 75 05                       ; 0xc155c
    mov word [bp-016h], 05085h                ; c7 46 ea 85 50              ; 0xc155e vgabios.c:1023
    xor bx, bx                                ; 31 db                       ; 0xc1563 vgabios.c:1026
    jmp short 01576h                          ; eb 0f                       ; 0xc1565
    xor al, al                                ; 30 c0                       ; 0xc1567 vgabios.c:1033
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc1569
    out DX, AL                                ; ee                          ; 0xc156c
    out DX, AL                                ; ee                          ; 0xc156d vgabios.c:1034
    out DX, AL                                ; ee                          ; 0xc156e vgabios.c:1035
    inc bx                                    ; 43                          ; 0xc156f vgabios.c:1037
    cmp bx, 00100h                            ; 81 fb 00 01                 ; 0xc1570
    jnc short 015abh                          ; 73 35                       ; 0xc1574
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1576
    xor ah, ah                                ; 30 e4                       ; 0xc1579
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc157b
    mov di, ax                                ; 89 c7                       ; 0xc157d
    sal di, CL                                ; d3 e7                       ; 0xc157f
    mov al, byte [di+047b8h]                  ; 8a 85 b8 47                 ; 0xc1581
    mov di, ax                                ; 89 c7                       ; 0xc1585
    mov al, byte [di+04841h]                  ; 8a 85 41 48                 ; 0xc1587
    cmp bx, ax                                ; 39 c3                       ; 0xc158b
    jnbe short 01567h                         ; 77 d8                       ; 0xc158d
    mov ax, bx                                ; 89 d8                       ; 0xc158f
    mov dx, strict word 00003h                ; ba 03 00                    ; 0xc1591
    mul dx                                    ; f7 e2                       ; 0xc1594
    mov di, word [bp-016h]                    ; 8b 7e ea                    ; 0xc1596
    add di, ax                                ; 01 c7                       ; 0xc1599
    mov al, byte [di]                         ; 8a 05                       ; 0xc159b
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc159d
    out DX, AL                                ; ee                          ; 0xc15a0
    mov al, byte [di+001h]                    ; 8a 45 01                    ; 0xc15a1
    out DX, AL                                ; ee                          ; 0xc15a4
    mov al, byte [di+002h]                    ; 8a 45 02                    ; 0xc15a5
    out DX, AL                                ; ee                          ; 0xc15a8
    jmp short 0156fh                          ; eb c4                       ; 0xc15a9
    test ch, 002h                             ; f6 c5 02                    ; 0xc15ab vgabios.c:1038
    je short 015b8h                           ; 74 08                       ; 0xc15ae
    mov dx, 00100h                            ; ba 00 01                    ; 0xc15b0 vgabios.c:1040
    xor ax, ax                                ; 31 c0                       ; 0xc15b3
    call 01134h                               ; e8 7c fb                    ; 0xc15b5
    mov dx, 003dah                            ; ba da 03                    ; 0xc15b8 vgabios.c:1045
    in AL, DX                                 ; ec                          ; 0xc15bb
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc15bc
    xor bx, bx                                ; 31 db                       ; 0xc15be vgabios.c:1048
    jmp short 015c7h                          ; eb 05                       ; 0xc15c0
    cmp bx, strict byte 00013h                ; 83 fb 13                    ; 0xc15c2
    jnbe short 015dch                         ; 77 15                       ; 0xc15c5
    mov al, bl                                ; 88 d8                       ; 0xc15c7 vgabios.c:1049
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc15c9
    out DX, AL                                ; ee                          ; 0xc15cc
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc15cd vgabios.c:1050
    mov di, si                                ; 89 f7                       ; 0xc15d0
    add di, bx                                ; 01 df                       ; 0xc15d2
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc15d4
    out DX, AL                                ; ee                          ; 0xc15d8
    inc bx                                    ; 43                          ; 0xc15d9 vgabios.c:1051
    jmp short 015c2h                          ; eb e6                       ; 0xc15da
    mov AL, strict byte 014h                  ; b0 14                       ; 0xc15dc vgabios.c:1052
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc15de
    out DX, AL                                ; ee                          ; 0xc15e1
    xor al, al                                ; 30 c0                       ; 0xc15e2 vgabios.c:1053
    out DX, AL                                ; ee                          ; 0xc15e4
    les bx, [bp-014h]                         ; c4 5e ec                    ; 0xc15e5 vgabios.c:1056
    mov dx, word [es:bx+004h]                 ; 26 8b 57 04                 ; 0xc15e8
    mov ax, word [es:bx+006h]                 ; 26 8b 47 06                 ; 0xc15ec
    test ax, ax                               ; 85 c0                       ; 0xc15f0
    jne short 015f8h                          ; 75 04                       ; 0xc15f2
    test dx, dx                               ; 85 d2                       ; 0xc15f4
    je short 01634h                           ; 74 3c                       ; 0xc15f6
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc15f8 vgabios.c:1060
    xor bx, bx                                ; 31 db                       ; 0xc15fb vgabios.c:1061
    jmp short 01604h                          ; eb 05                       ; 0xc15fd
    cmp bx, strict byte 00010h                ; 83 fb 10                    ; 0xc15ff
    jnc short 01624h                          ; 73 20                       ; 0xc1602
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1604 vgabios.c:1062
    mov di, si                                ; 89 f7                       ; 0xc1607
    add di, bx                                ; 01 df                       ; 0xc1609
    mov ax, word [bp-020h]                    ; 8b 46 e0                    ; 0xc160b
    mov word [bp-022h], ax                    ; 89 46 de                    ; 0xc160e
    mov cx, dx                                ; 89 d1                       ; 0xc1611
    add cx, bx                                ; 01 d9                       ; 0xc1613
    mov al, byte [es:di+023h]                 ; 26 8a 45 23                 ; 0xc1615
    mov es, [bp-022h]                         ; 8e 46 de                    ; 0xc1619
    mov di, cx                                ; 89 cf                       ; 0xc161c
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc161e
    inc bx                                    ; 43                          ; 0xc1621
    jmp short 015ffh                          ; eb db                       ; 0xc1622
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1624 vgabios.c:1063
    mov al, byte [es:si+034h]                 ; 26 8a 44 34                 ; 0xc1627
    mov es, [bp-020h]                         ; 8e 46 e0                    ; 0xc162b
    mov bx, dx                                ; 89 d3                       ; 0xc162e
    mov byte [es:bx+010h], al                 ; 26 88 47 10                 ; 0xc1630
    xor al, al                                ; 30 c0                       ; 0xc1634 vgabios.c:1067
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc1636
    out DX, AL                                ; ee                          ; 0xc1639
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc163a vgabios.c:1068
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc163c
    out DX, AL                                ; ee                          ; 0xc163f
    mov bx, strict word 00001h                ; bb 01 00                    ; 0xc1640 vgabios.c:1069
    jmp short 0164ah                          ; eb 05                       ; 0xc1643
    cmp bx, strict byte 00004h                ; 83 fb 04                    ; 0xc1645
    jnbe short 01662h                         ; 77 18                       ; 0xc1648
    mov al, bl                                ; 88 d8                       ; 0xc164a vgabios.c:1070
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc164c
    out DX, AL                                ; ee                          ; 0xc164f
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1650 vgabios.c:1071
    mov di, si                                ; 89 f7                       ; 0xc1653
    add di, bx                                ; 01 df                       ; 0xc1655
    mov al, byte [es:di+004h]                 ; 26 8a 45 04                 ; 0xc1657
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc165b
    out DX, AL                                ; ee                          ; 0xc165e
    inc bx                                    ; 43                          ; 0xc165f vgabios.c:1072
    jmp short 01645h                          ; eb e3                       ; 0xc1660
    xor bx, bx                                ; 31 db                       ; 0xc1662 vgabios.c:1075
    jmp short 0166bh                          ; eb 05                       ; 0xc1664
    cmp bx, strict byte 00008h                ; 83 fb 08                    ; 0xc1666
    jnbe short 01683h                         ; 77 18                       ; 0xc1669
    mov al, bl                                ; 88 d8                       ; 0xc166b vgabios.c:1076
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc166d
    out DX, AL                                ; ee                          ; 0xc1670
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1671 vgabios.c:1077
    mov di, si                                ; 89 f7                       ; 0xc1674
    add di, bx                                ; 01 df                       ; 0xc1676
    mov al, byte [es:di+037h]                 ; 26 8a 45 37                 ; 0xc1678
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc167c
    out DX, AL                                ; ee                          ; 0xc167f
    inc bx                                    ; 43                          ; 0xc1680 vgabios.c:1078
    jmp short 01666h                          ; eb e3                       ; 0xc1681
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc1683 vgabios.c:1081
    xor bh, bh                                ; 30 ff                       ; 0xc1686
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1688
    sal bx, CL                                ; d3 e3                       ; 0xc168a
    cmp byte [bx+047b3h], 001h                ; 80 bf b3 47 01              ; 0xc168c
    jne short 01698h                          ; 75 05                       ; 0xc1691
    mov bx, 003b4h                            ; bb b4 03                    ; 0xc1693
    jmp short 0169bh                          ; eb 03                       ; 0xc1696
    mov bx, 003d4h                            ; bb d4 03                    ; 0xc1698
    mov word [bp-018h], bx                    ; 89 5e e8                    ; 0xc169b
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc169e vgabios.c:1084
    mov al, byte [es:si+009h]                 ; 26 8a 44 09                 ; 0xc16a1
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc16a5
    out DX, AL                                ; ee                          ; 0xc16a8
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc16a9 vgabios.c:1087
    mov dx, bx                                ; 89 da                       ; 0xc16ac
    out DX, ax                                ; ef                          ; 0xc16ae
    xor bx, bx                                ; 31 db                       ; 0xc16af vgabios.c:1089
    jmp short 016b8h                          ; eb 05                       ; 0xc16b1
    cmp bx, strict byte 00018h                ; 83 fb 18                    ; 0xc16b3
    jnbe short 016ceh                         ; 77 16                       ; 0xc16b6
    mov al, bl                                ; 88 d8                       ; 0xc16b8 vgabios.c:1090
    mov dx, word [bp-018h]                    ; 8b 56 e8                    ; 0xc16ba
    out DX, AL                                ; ee                          ; 0xc16bd
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc16be vgabios.c:1091
    mov di, si                                ; 89 f7                       ; 0xc16c1
    add di, bx                                ; 01 df                       ; 0xc16c3
    inc dx                                    ; 42                          ; 0xc16c5
    mov al, byte [es:di+00ah]                 ; 26 8a 45 0a                 ; 0xc16c6
    out DX, AL                                ; ee                          ; 0xc16ca
    inc bx                                    ; 43                          ; 0xc16cb vgabios.c:1092
    jmp short 016b3h                          ; eb e5                       ; 0xc16cc
    mov AL, strict byte 020h                  ; b0 20                       ; 0xc16ce vgabios.c:1095
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc16d0
    out DX, AL                                ; ee                          ; 0xc16d3
    mov dx, word [bp-018h]                    ; 8b 56 e8                    ; 0xc16d4 vgabios.c:1096
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc16d7
    in AL, DX                                 ; ec                          ; 0xc16da
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc16db
    cmp byte [bp-00eh], 000h                  ; 80 7e f2 00                 ; 0xc16dd vgabios.c:1098
    jne short 01741h                          ; 75 5e                       ; 0xc16e1
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc16e3 vgabios.c:1100
    xor bh, bh                                ; 30 ff                       ; 0xc16e6
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc16e8
    sal bx, CL                                ; d3 e3                       ; 0xc16ea
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc16ec
    jne short 01705h                          ; 75 12                       ; 0xc16f1
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc16f3 vgabios.c:1102
    mov cx, 04000h                            ; b9 00 40                    ; 0xc16f7
    mov ax, 00720h                            ; b8 20 07                    ; 0xc16fa
    xor di, di                                ; 31 ff                       ; 0xc16fd
    jcxz 01703h                               ; e3 02                       ; 0xc16ff
    rep stosw                                 ; f3 ab                       ; 0xc1701
    jmp short 01741h                          ; eb 3c                       ; 0xc1703 vgabios.c:1104
    cmp byte [bp-00ch], 00dh                  ; 80 7e f4 0d                 ; 0xc1705 vgabios.c:1106
    jnc short 0171ch                          ; 73 11                       ; 0xc1709
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc170b vgabios.c:1108
    mov cx, 04000h                            ; b9 00 40                    ; 0xc170f
    xor ax, ax                                ; 31 c0                       ; 0xc1712
    xor di, di                                ; 31 ff                       ; 0xc1714
    jcxz 0171ah                               ; e3 02                       ; 0xc1716
    rep stosw                                 ; f3 ab                       ; 0xc1718
    jmp short 01741h                          ; eb 25                       ; 0xc171a vgabios.c:1110
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc171c vgabios.c:1112
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc171e
    out DX, AL                                ; ee                          ; 0xc1721
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc1722 vgabios.c:1113
    in AL, DX                                 ; ec                          ; 0xc1725
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc1726
    mov word [bp-022h], ax                    ; 89 46 de                    ; 0xc1728
    mov AL, strict byte 00fh                  ; b0 0f                       ; 0xc172b vgabios.c:1114
    out DX, AL                                ; ee                          ; 0xc172d
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc172e vgabios.c:1115
    mov cx, 08000h                            ; b9 00 80                    ; 0xc1732
    xor ax, ax                                ; 31 c0                       ; 0xc1735
    xor di, di                                ; 31 ff                       ; 0xc1737
    jcxz 0173dh                               ; e3 02                       ; 0xc1739
    rep stosw                                 ; f3 ab                       ; 0xc173b
    mov al, byte [bp-022h]                    ; 8a 46 de                    ; 0xc173d vgabios.c:1116
    out DX, AL                                ; ee                          ; 0xc1740
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1741 vgabios.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1744
    mov es, ax                                ; 8e c0                       ; 0xc1747
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1749
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc174c
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc174f vgabios.c:1123
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc1752
    xor ah, ah                                ; 30 e4                       ; 0xc1755
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1757 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc175a
    mov es, dx                                ; 8e c2                       ; 0xc175d
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc175f
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1762 vgabios.c:60
    mov ax, word [es:si+003h]                 ; 26 8b 44 03                 ; 0xc1765
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc1769 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc176c
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc176e
    mov bx, strict word 00063h                ; bb 63 00                    ; 0xc1771 vgabios.c:62
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc1774
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc1777
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc177a vgabios.c:50
    mov al, byte [es:si+001h]                 ; 26 8a 44 01                 ; 0xc177d
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1781 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc1784
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc1786
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1789 vgabios.c:1127
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc178c
    xor ah, ah                                ; 30 e4                       ; 0xc1790
    mov bx, 00085h                            ; bb 85 00                    ; 0xc1792 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc1795
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc1797
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc179a vgabios.c:1128
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc179d
    mov bx, 00087h                            ; bb 87 00                    ; 0xc179f vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc17a2
    mov bx, 00088h                            ; bb 88 00                    ; 0xc17a5 vgabios.c:52
    mov byte [es:bx], 0f9h                    ; 26 c6 07 f9                 ; 0xc17a8
    mov bx, 0008ah                            ; bb 8a 00                    ; 0xc17ac vgabios.c:52
    mov byte [es:bx], 008h                    ; 26 c6 07 08                 ; 0xc17af
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc17b3 vgabios.c:1134
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc17b6
    jnbe short 017e1h                         ; 77 27                       ; 0xc17b8
    mov bl, al                                ; 88 c3                       ; 0xc17ba vgabios.c:1136
    xor bh, bh                                ; 30 ff                       ; 0xc17bc
    mov al, byte [bx+07de0h]                  ; 8a 87 e0 7d                 ; 0xc17be vgabios.c:50
    mov bx, strict word 00065h                ; bb 65 00                    ; 0xc17c2 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc17c5
    cmp byte [bp-00ch], 006h                  ; 80 7e f4 06                 ; 0xc17c8 vgabios.c:1137
    jne short 017d3h                          ; 75 05                       ; 0xc17cc
    mov ax, strict word 0003fh                ; b8 3f 00                    ; 0xc17ce
    jmp short 017d6h                          ; eb 03                       ; 0xc17d1
    mov ax, strict word 00030h                ; b8 30 00                    ; 0xc17d3
    mov bx, strict word 00066h                ; bb 66 00                    ; 0xc17d6 vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc17d9
    mov es, dx                                ; 8e c2                       ; 0xc17dc
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc17de
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc17e1 vgabios.c:1141
    xor bh, bh                                ; 30 ff                       ; 0xc17e4
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc17e6
    sal bx, CL                                ; d3 e3                       ; 0xc17e8
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc17ea
    jne short 017fah                          ; 75 09                       ; 0xc17ef
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc17f1 vgabios.c:1143
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc17f4
    call 011d3h                               ; e8 d9 f9                    ; 0xc17f7
    xor bx, bx                                ; 31 db                       ; 0xc17fa vgabios.c:1148
    jmp short 01803h                          ; eb 05                       ; 0xc17fc
    cmp bx, strict byte 00008h                ; 83 fb 08                    ; 0xc17fe
    jnc short 0180fh                          ; 73 0c                       ; 0xc1801
    mov al, bl                                ; 88 d8                       ; 0xc1803 vgabios.c:1149
    xor ah, ah                                ; 30 e4                       ; 0xc1805
    xor dx, dx                                ; 31 d2                       ; 0xc1807
    call 012cfh                               ; e8 c3 fa                    ; 0xc1809
    inc bx                                    ; 43                          ; 0xc180c
    jmp short 017feh                          ; eb ef                       ; 0xc180d
    xor ax, ax                                ; 31 c0                       ; 0xc180f vgabios.c:1152
    call 0135ch                               ; e8 48 fb                    ; 0xc1811
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc1814 vgabios.c:1155
    xor bh, bh                                ; 30 ff                       ; 0xc1817
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1819
    sal bx, CL                                ; d3 e3                       ; 0xc181b
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc181d
    jne short 01892h                          ; 75 6e                       ; 0xc1822
    les bx, [bp-014h]                         ; c4 5e ec                    ; 0xc1824 vgabios.c:1157
    mov bx, word [es:bx+008h]                 ; 26 8b 5f 08                 ; 0xc1827
    mov word [bp-01eh], bx                    ; 89 5e e2                    ; 0xc182b
    mov bx, word [bp-014h]                    ; 8b 5e ec                    ; 0xc182e
    mov ax, word [es:bx+00ah]                 ; 26 8b 47 0a                 ; 0xc1831
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1835
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1838 vgabios.c:1159
    mov bl, byte [es:si+002h]                 ; 26 8a 5c 02                 ; 0xc183b
    cmp bl, 00eh                              ; 80 fb 0e                    ; 0xc183f
    je short 01865h                           ; 74 21                       ; 0xc1842
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc1844
    jne short 01895h                          ; 75 4c                       ; 0xc1847
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1849 vgabios.c:1161
    mov al, byte [es:si+002h]                 ; 26 8a 44 02                 ; 0xc184c
    xor ah, ah                                ; 30 e4                       ; 0xc1850
    push ax                                   ; 50                          ; 0xc1852
    xor al, al                                ; 30 c0                       ; 0xc1853
    push ax                                   ; 50                          ; 0xc1855
    push ax                                   ; 50                          ; 0xc1856
    mov cx, 00100h                            ; b9 00 01                    ; 0xc1857
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc185a
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc185d
    call 02e8ch                               ; e8 29 16                    ; 0xc1860
    jmp short 018bah                          ; eb 55                       ; 0xc1863 vgabios.c:1162
    mov al, bl                                ; 88 d8                       ; 0xc1865 vgabios.c:1164
    xor ah, ah                                ; 30 e4                       ; 0xc1867
    push ax                                   ; 50                          ; 0xc1869
    xor al, bl                                ; 30 d8                       ; 0xc186a
    push ax                                   ; 50                          ; 0xc186c
    xor al, al                                ; 30 c0                       ; 0xc186d
    push ax                                   ; 50                          ; 0xc186f
    mov cx, 00100h                            ; b9 00 01                    ; 0xc1870
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc1873
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc1876
    call 02e8ch                               ; e8 10 16                    ; 0xc1879
    cmp byte [bp-00ch], 007h                  ; 80 7e f4 07                 ; 0xc187c vgabios.c:1165
    jne short 018bah                          ; 75 38                       ; 0xc1880
    mov cx, strict word 0000eh                ; b9 0e 00                    ; 0xc1882 vgabios.c:1166
    xor bx, bx                                ; 31 db                       ; 0xc1885
    mov dx, 07b6fh                            ; ba 6f 7b                    ; 0xc1887
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc188a
    call 02e14h                               ; e8 84 15                    ; 0xc188d
    jmp short 018bah                          ; eb 28                       ; 0xc1890 vgabios.c:1167
    jmp near 0191bh                           ; e9 86 00                    ; 0xc1892
    mov al, bl                                ; 88 d8                       ; 0xc1895 vgabios.c:1169
    xor ah, ah                                ; 30 e4                       ; 0xc1897
    push ax                                   ; 50                          ; 0xc1899
    xor al, bl                                ; 30 d8                       ; 0xc189a
    push ax                                   ; 50                          ; 0xc189c
    xor al, al                                ; 30 c0                       ; 0xc189d
    push ax                                   ; 50                          ; 0xc189f
    mov cx, 00100h                            ; b9 00 01                    ; 0xc18a0
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc18a3
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc18a6
    call 02e8ch                               ; e8 e0 15                    ; 0xc18a9
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc18ac vgabios.c:1170
    xor bx, bx                                ; 31 db                       ; 0xc18af
    mov dx, 07c9ch                            ; ba 9c 7c                    ; 0xc18b1
    mov ax, 0c000h                            ; b8 00 c0                    ; 0xc18b4
    call 02e14h                               ; e8 5a 15                    ; 0xc18b7
    cmp word [bp-01ch], strict byte 00000h    ; 83 7e e4 00                 ; 0xc18ba vgabios.c:1172
    jne short 018c6h                          ; 75 06                       ; 0xc18be
    cmp word [bp-01eh], strict byte 00000h    ; 83 7e e2 00                 ; 0xc18c0
    je short 01913h                           ; 74 4d                       ; 0xc18c4
    xor bx, bx                                ; 31 db                       ; 0xc18c6 vgabios.c:1177
    les di, [bp-01eh]                         ; c4 7e e2                    ; 0xc18c8 vgabios.c:1179
    add di, bx                                ; 01 df                       ; 0xc18cb
    mov al, byte [es:di+00bh]                 ; 26 8a 45 0b                 ; 0xc18cd
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc18d1
    je short 018ddh                           ; 74 08                       ; 0xc18d3
    cmp al, byte [bp-00ch]                    ; 3a 46 f4                    ; 0xc18d5 vgabios.c:1181
    je short 018ddh                           ; 74 03                       ; 0xc18d8
    inc bx                                    ; 43                          ; 0xc18da vgabios.c:1183
    jmp short 018c8h                          ; eb eb                       ; 0xc18db vgabios.c:1184
    mov es, [bp-01ch]                         ; 8e 46 e4                    ; 0xc18dd vgabios.c:1186
    add bx, word [bp-01eh]                    ; 03 5e e2                    ; 0xc18e0
    mov al, byte [es:bx+00bh]                 ; 26 8a 47 0b                 ; 0xc18e3
    cmp al, byte [bp-00ch]                    ; 3a 46 f4                    ; 0xc18e7
    jne short 01913h                          ; 75 27                       ; 0xc18ea
    mov bx, word [bp-01eh]                    ; 8b 5e e2                    ; 0xc18ec vgabios.c:1191
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc18ef
    xor ah, ah                                ; 30 e4                       ; 0xc18f2
    push ax                                   ; 50                          ; 0xc18f4
    mov al, byte [es:bx+001h]                 ; 26 8a 47 01                 ; 0xc18f5
    push ax                                   ; 50                          ; 0xc18f9
    push word [es:bx+004h]                    ; 26 ff 77 04                 ; 0xc18fa
    mov cx, word [es:bx+002h]                 ; 26 8b 4f 02                 ; 0xc18fe
    mov bx, word [es:bx+006h]                 ; 26 8b 5f 06                 ; 0xc1902
    mov di, word [bp-01eh]                    ; 8b 7e e2                    ; 0xc1906
    mov dx, word [es:di+008h]                 ; 26 8b 55 08                 ; 0xc1909
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc190d
    call 02e8ch                               ; e8 79 15                    ; 0xc1910
    xor bl, bl                                ; 30 db                       ; 0xc1913 vgabios.c:1195
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc1915
    mov AH, strict byte 011h                  ; b4 11                       ; 0xc1917
    int 06dh                                  ; cd 6d                       ; 0xc1919
    mov bx, 0596fh                            ; bb 6f 59                    ; 0xc191b vgabios.c:1199
    mov cx, ds                                ; 8c d9                       ; 0xc191e
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc1920
    call 009f0h                               ; e8 ca f0                    ; 0xc1923
    mov es, [bp-01ah]                         ; 8e 46 e6                    ; 0xc1926 vgabios.c:1201
    mov dl, byte [es:si+002h]                 ; 26 8a 54 02                 ; 0xc1929
    cmp dl, 010h                              ; 80 fa 10                    ; 0xc192d
    je short 0194eh                           ; 74 1c                       ; 0xc1930
    cmp dl, 00eh                              ; 80 fa 0e                    ; 0xc1932
    je short 01949h                           ; 74 12                       ; 0xc1935
    cmp dl, 008h                              ; 80 fa 08                    ; 0xc1937
    jne short 01953h                          ; 75 17                       ; 0xc193a
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc193c vgabios.c:1203
    mov cx, ds                                ; 8c d9                       ; 0xc193f
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc1941
    call 009f0h                               ; e8 a9 f0                    ; 0xc1944
    jmp short 01953h                          ; eb 0a                       ; 0xc1947 vgabios.c:1204
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc1949 vgabios.c:1206
    jmp short 0193fh                          ; eb f1                       ; 0xc194c
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc194e vgabios.c:1209
    jmp short 0193fh                          ; eb ec                       ; 0xc1951
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc1953 vgabios.c:1212
    pop di                                    ; 5f                          ; 0xc1956
    pop si                                    ; 5e                          ; 0xc1957
    pop dx                                    ; 5a                          ; 0xc1958
    pop cx                                    ; 59                          ; 0xc1959
    pop bx                                    ; 5b                          ; 0xc195a
    pop bp                                    ; 5d                          ; 0xc195b
    retn                                      ; c3                          ; 0xc195c
  ; disGetNextSymbol 0xc195d LB 0x2c07 -> off=0x0 cb=000000000000008e uValue=00000000000c195d 'vgamem_copy_pl4'
vgamem_copy_pl4:                             ; 0xc195d LB 0x8e
    push bp                                   ; 55                          ; 0xc195d vgabios.c:1215
    mov bp, sp                                ; 89 e5                       ; 0xc195e
    push si                                   ; 56                          ; 0xc1960
    push di                                   ; 57                          ; 0xc1961
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc1962
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc1965
    mov al, dl                                ; 88 d0                       ; 0xc1968
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc196a
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc196d
    xor ah, ah                                ; 30 e4                       ; 0xc1970 vgabios.c:1221
    mov dl, byte [bp+006h]                    ; 8a 56 06                    ; 0xc1972
    xor dh, dh                                ; 30 f6                       ; 0xc1975
    mov cx, dx                                ; 89 d1                       ; 0xc1977
    imul dx                                   ; f7 ea                       ; 0xc1979
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc197b
    xor dh, dh                                ; 30 f6                       ; 0xc197e
    mov si, dx                                ; 89 d6                       ; 0xc1980
    imul dx                                   ; f7 ea                       ; 0xc1982
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1984
    xor dh, dh                                ; 30 f6                       ; 0xc1987
    mov bx, dx                                ; 89 d3                       ; 0xc1989
    add ax, dx                                ; 01 d0                       ; 0xc198b
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc198d
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1990 vgabios.c:1222
    xor ah, ah                                ; 30 e4                       ; 0xc1993
    imul cx                                   ; f7 e9                       ; 0xc1995
    imul si                                   ; f7 ee                       ; 0xc1997
    add ax, bx                                ; 01 d8                       ; 0xc1999
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc199b
    mov ax, 00105h                            ; b8 05 01                    ; 0xc199e vgabios.c:1223
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc19a1
    out DX, ax                                ; ef                          ; 0xc19a4
    xor bl, bl                                ; 30 db                       ; 0xc19a5 vgabios.c:1224
    cmp bl, byte [bp+006h]                    ; 3a 5e 06                    ; 0xc19a7
    jnc short 019dbh                          ; 73 2f                       ; 0xc19aa
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc19ac vgabios.c:1226
    xor ah, ah                                ; 30 e4                       ; 0xc19af
    mov cx, ax                                ; 89 c1                       ; 0xc19b1
    mov al, bl                                ; 88 d8                       ; 0xc19b3
    mov dx, ax                                ; 89 c2                       ; 0xc19b5
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc19b7
    mov si, ax                                ; 89 c6                       ; 0xc19ba
    mov ax, dx                                ; 89 d0                       ; 0xc19bc
    imul si                                   ; f7 ee                       ; 0xc19be
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc19c0
    add si, ax                                ; 01 c6                       ; 0xc19c3
    mov di, word [bp-00ch]                    ; 8b 7e f4                    ; 0xc19c5
    add di, ax                                ; 01 c7                       ; 0xc19c8
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc19ca
    mov es, dx                                ; 8e c2                       ; 0xc19cd
    jcxz 019d7h                               ; e3 06                       ; 0xc19cf
    push DS                                   ; 1e                          ; 0xc19d1
    mov ds, dx                                ; 8e da                       ; 0xc19d2
    rep movsb                                 ; f3 a4                       ; 0xc19d4
    pop DS                                    ; 1f                          ; 0xc19d6
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc19d7 vgabios.c:1227
    jmp short 019a7h                          ; eb cc                       ; 0xc19d9
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc19db vgabios.c:1228
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc19de
    out DX, ax                                ; ef                          ; 0xc19e1
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc19e2 vgabios.c:1229
    pop di                                    ; 5f                          ; 0xc19e5
    pop si                                    ; 5e                          ; 0xc19e6
    pop bp                                    ; 5d                          ; 0xc19e7
    retn 00004h                               ; c2 04 00                    ; 0xc19e8
  ; disGetNextSymbol 0xc19eb LB 0x2b79 -> off=0x0 cb=000000000000007b uValue=00000000000c19eb 'vgamem_fill_pl4'
vgamem_fill_pl4:                             ; 0xc19eb LB 0x7b
    push bp                                   ; 55                          ; 0xc19eb vgabios.c:1232
    mov bp, sp                                ; 89 e5                       ; 0xc19ec
    push si                                   ; 56                          ; 0xc19ee
    push di                                   ; 57                          ; 0xc19ef
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc19f0
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc19f3
    mov al, dl                                ; 88 d0                       ; 0xc19f6
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc19f8
    mov bh, cl                                ; 88 cf                       ; 0xc19fb
    xor ah, ah                                ; 30 e4                       ; 0xc19fd vgabios.c:1238
    mov dx, ax                                ; 89 c2                       ; 0xc19ff
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1a01
    mov cx, ax                                ; 89 c1                       ; 0xc1a04
    mov ax, dx                                ; 89 d0                       ; 0xc1a06
    imul cx                                   ; f7 e9                       ; 0xc1a08
    mov dl, bh                                ; 88 fa                       ; 0xc1a0a
    xor dh, dh                                ; 30 f6                       ; 0xc1a0c
    imul dx                                   ; f7 ea                       ; 0xc1a0e
    mov dx, ax                                ; 89 c2                       ; 0xc1a10
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1a12
    xor ah, ah                                ; 30 e4                       ; 0xc1a15
    add dx, ax                                ; 01 c2                       ; 0xc1a17
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc1a19
    mov ax, 00205h                            ; b8 05 02                    ; 0xc1a1c vgabios.c:1239
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1a1f
    out DX, ax                                ; ef                          ; 0xc1a22
    xor bl, bl                                ; 30 db                       ; 0xc1a23 vgabios.c:1240
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc1a25
    jnc short 01a56h                          ; 73 2c                       ; 0xc1a28
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc1a2a vgabios.c:1242
    xor ch, ch                                ; 30 ed                       ; 0xc1a2d
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1a2f
    xor ah, ah                                ; 30 e4                       ; 0xc1a32
    mov si, ax                                ; 89 c6                       ; 0xc1a34
    mov al, bl                                ; 88 d8                       ; 0xc1a36
    mov dx, ax                                ; 89 c2                       ; 0xc1a38
    mov al, bh                                ; 88 f8                       ; 0xc1a3a
    mov di, ax                                ; 89 c7                       ; 0xc1a3c
    mov ax, dx                                ; 89 d0                       ; 0xc1a3e
    imul di                                   ; f7 ef                       ; 0xc1a40
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc1a42
    add di, ax                                ; 01 c7                       ; 0xc1a45
    mov ax, si                                ; 89 f0                       ; 0xc1a47
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1a49
    mov es, dx                                ; 8e c2                       ; 0xc1a4c
    jcxz 01a52h                               ; e3 02                       ; 0xc1a4e
    rep stosb                                 ; f3 aa                       ; 0xc1a50
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc1a52 vgabios.c:1243
    jmp short 01a25h                          ; eb cf                       ; 0xc1a54
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1a56 vgabios.c:1244
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1a59
    out DX, ax                                ; ef                          ; 0xc1a5c
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1a5d vgabios.c:1245
    pop di                                    ; 5f                          ; 0xc1a60
    pop si                                    ; 5e                          ; 0xc1a61
    pop bp                                    ; 5d                          ; 0xc1a62
    retn 00004h                               ; c2 04 00                    ; 0xc1a63
  ; disGetNextSymbol 0xc1a66 LB 0x2afe -> off=0x0 cb=00000000000000b6 uValue=00000000000c1a66 'vgamem_copy_cga'
vgamem_copy_cga:                             ; 0xc1a66 LB 0xb6
    push bp                                   ; 55                          ; 0xc1a66 vgabios.c:1248
    mov bp, sp                                ; 89 e5                       ; 0xc1a67
    push si                                   ; 56                          ; 0xc1a69
    push di                                   ; 57                          ; 0xc1a6a
    sub sp, strict byte 0000eh                ; 83 ec 0e                    ; 0xc1a6b
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc1a6e
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc1a71
    mov byte [bp-00ah], cl                    ; 88 4e f6                    ; 0xc1a74
    mov al, dl                                ; 88 d0                       ; 0xc1a77 vgabios.c:1254
    xor ah, ah                                ; 30 e4                       ; 0xc1a79
    mov bx, ax                                ; 89 c3                       ; 0xc1a7b
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1a7d
    mov si, ax                                ; 89 c6                       ; 0xc1a80
    mov ax, bx                                ; 89 d8                       ; 0xc1a82
    imul si                                   ; f7 ee                       ; 0xc1a84
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc1a86
    mov di, bx                                ; 89 df                       ; 0xc1a89
    imul bx                                   ; f7 eb                       ; 0xc1a8b
    mov dx, ax                                ; 89 c2                       ; 0xc1a8d
    sar dx, 1                                 ; d1 fa                       ; 0xc1a8f
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1a91
    xor ah, ah                                ; 30 e4                       ; 0xc1a94
    mov bx, ax                                ; 89 c3                       ; 0xc1a96
    add dx, ax                                ; 01 c2                       ; 0xc1a98
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1a9a
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1a9d vgabios.c:1255
    imul si                                   ; f7 ee                       ; 0xc1aa0
    imul di                                   ; f7 ef                       ; 0xc1aa2
    sar ax, 1                                 ; d1 f8                       ; 0xc1aa4
    add ax, bx                                ; 01 d8                       ; 0xc1aa6
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc1aa8
    mov byte [bp-006h], bh                    ; 88 7e fa                    ; 0xc1aab vgabios.c:1256
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1aae
    xor ah, ah                                ; 30 e4                       ; 0xc1ab1
    cwd                                       ; 99                          ; 0xc1ab3
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1ab4
    sar ax, 1                                 ; d1 f8                       ; 0xc1ab6
    mov bx, ax                                ; 89 c3                       ; 0xc1ab8
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1aba
    xor ah, ah                                ; 30 e4                       ; 0xc1abd
    cmp ax, bx                                ; 39 d8                       ; 0xc1abf
    jnl short 01b13h                          ; 7d 50                       ; 0xc1ac1
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc1ac3 vgabios.c:1258
    xor bh, bh                                ; 30 ff                       ; 0xc1ac6
    mov word [bp-012h], bx                    ; 89 5e ee                    ; 0xc1ac8
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc1acb
    imul bx                                   ; f7 eb                       ; 0xc1ace
    mov bx, ax                                ; 89 c3                       ; 0xc1ad0
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc1ad2
    add si, ax                                ; 01 c6                       ; 0xc1ad5
    mov di, word [bp-010h]                    ; 8b 7e f0                    ; 0xc1ad7
    add di, ax                                ; 01 c7                       ; 0xc1ada
    mov cx, word [bp-012h]                    ; 8b 4e ee                    ; 0xc1adc
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1adf
    mov es, dx                                ; 8e c2                       ; 0xc1ae2
    jcxz 01aech                               ; e3 06                       ; 0xc1ae4
    push DS                                   ; 1e                          ; 0xc1ae6
    mov ds, dx                                ; 8e da                       ; 0xc1ae7
    rep movsb                                 ; f3 a4                       ; 0xc1ae9
    pop DS                                    ; 1f                          ; 0xc1aeb
    mov si, word [bp-00eh]                    ; 8b 76 f2                    ; 0xc1aec vgabios.c:1259
    add si, 02000h                            ; 81 c6 00 20                 ; 0xc1aef
    add si, bx                                ; 01 de                       ; 0xc1af3
    mov di, word [bp-010h]                    ; 8b 7e f0                    ; 0xc1af5
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1af8
    add di, bx                                ; 01 df                       ; 0xc1afc
    mov cx, word [bp-012h]                    ; 8b 4e ee                    ; 0xc1afe
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1b01
    mov es, dx                                ; 8e c2                       ; 0xc1b04
    jcxz 01b0eh                               ; e3 06                       ; 0xc1b06
    push DS                                   ; 1e                          ; 0xc1b08
    mov ds, dx                                ; 8e da                       ; 0xc1b09
    rep movsb                                 ; f3 a4                       ; 0xc1b0b
    pop DS                                    ; 1f                          ; 0xc1b0d
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1b0e vgabios.c:1260
    jmp short 01aaeh                          ; eb 9b                       ; 0xc1b11
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1b13 vgabios.c:1261
    pop di                                    ; 5f                          ; 0xc1b16
    pop si                                    ; 5e                          ; 0xc1b17
    pop bp                                    ; 5d                          ; 0xc1b18
    retn 00004h                               ; c2 04 00                    ; 0xc1b19
  ; disGetNextSymbol 0xc1b1c LB 0x2a48 -> off=0x0 cb=0000000000000094 uValue=00000000000c1b1c 'vgamem_fill_cga'
vgamem_fill_cga:                             ; 0xc1b1c LB 0x94
    push bp                                   ; 55                          ; 0xc1b1c vgabios.c:1264
    mov bp, sp                                ; 89 e5                       ; 0xc1b1d
    push si                                   ; 56                          ; 0xc1b1f
    push di                                   ; 57                          ; 0xc1b20
    sub sp, strict byte 0000ch                ; 83 ec 0c                    ; 0xc1b21
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1b24
    mov al, dl                                ; 88 d0                       ; 0xc1b27
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1b29
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1b2c
    xor ah, ah                                ; 30 e4                       ; 0xc1b2f vgabios.c:1270
    mov dx, ax                                ; 89 c2                       ; 0xc1b31
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1b33
    mov bx, ax                                ; 89 c3                       ; 0xc1b36
    mov ax, dx                                ; 89 d0                       ; 0xc1b38
    imul bx                                   ; f7 eb                       ; 0xc1b3a
    mov dl, cl                                ; 88 ca                       ; 0xc1b3c
    xor dh, dh                                ; 30 f6                       ; 0xc1b3e
    imul dx                                   ; f7 ea                       ; 0xc1b40
    mov dx, ax                                ; 89 c2                       ; 0xc1b42
    sar dx, 1                                 ; d1 fa                       ; 0xc1b44
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1b46
    xor ah, ah                                ; 30 e4                       ; 0xc1b49
    add dx, ax                                ; 01 c2                       ; 0xc1b4b
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc1b4d
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1b50 vgabios.c:1271
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1b53
    xor ah, ah                                ; 30 e4                       ; 0xc1b56
    cwd                                       ; 99                          ; 0xc1b58
    db  02bh, 0c2h
    ; sub ax, dx                                ; 2b c2                     ; 0xc1b59
    sar ax, 1                                 ; d1 f8                       ; 0xc1b5b
    mov dx, ax                                ; 89 c2                       ; 0xc1b5d
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1b5f
    xor ah, ah                                ; 30 e4                       ; 0xc1b62
    cmp ax, dx                                ; 39 d0                       ; 0xc1b64
    jnl short 01ba7h                          ; 7d 3f                       ; 0xc1b66
    mov bl, byte [bp-00ch]                    ; 8a 5e f4                    ; 0xc1b68 vgabios.c:1273
    xor bh, bh                                ; 30 ff                       ; 0xc1b6b
    mov dl, byte [bp+006h]                    ; 8a 56 06                    ; 0xc1b6d
    xor dh, dh                                ; 30 f6                       ; 0xc1b70
    mov si, dx                                ; 89 d6                       ; 0xc1b72
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1b74
    imul dx                                   ; f7 ea                       ; 0xc1b77
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc1b79
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1b7c
    add di, ax                                ; 01 c7                       ; 0xc1b7f
    mov cx, bx                                ; 89 d9                       ; 0xc1b81
    mov ax, si                                ; 89 f0                       ; 0xc1b83
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc1b85
    mov es, dx                                ; 8e c2                       ; 0xc1b88
    jcxz 01b8eh                               ; e3 02                       ; 0xc1b8a
    rep stosb                                 ; f3 aa                       ; 0xc1b8c
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1b8e vgabios.c:1274
    add di, 02000h                            ; 81 c7 00 20                 ; 0xc1b91
    add di, word [bp-010h]                    ; 03 7e f0                    ; 0xc1b95
    mov cx, bx                                ; 89 d9                       ; 0xc1b98
    mov ax, si                                ; 89 f0                       ; 0xc1b9a
    mov es, dx                                ; 8e c2                       ; 0xc1b9c
    jcxz 01ba2h                               ; e3 02                       ; 0xc1b9e
    rep stosb                                 ; f3 aa                       ; 0xc1ba0
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1ba2 vgabios.c:1275
    jmp short 01b53h                          ; eb ac                       ; 0xc1ba5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1ba7 vgabios.c:1276
    pop di                                    ; 5f                          ; 0xc1baa
    pop si                                    ; 5e                          ; 0xc1bab
    pop bp                                    ; 5d                          ; 0xc1bac
    retn 00004h                               ; c2 04 00                    ; 0xc1bad
  ; disGetNextSymbol 0xc1bb0 LB 0x29b4 -> off=0x0 cb=0000000000000083 uValue=00000000000c1bb0 'vgamem_copy_linear'
vgamem_copy_linear:                          ; 0xc1bb0 LB 0x83
    push bp                                   ; 55                          ; 0xc1bb0 vgabios.c:1279
    mov bp, sp                                ; 89 e5                       ; 0xc1bb1
    push si                                   ; 56                          ; 0xc1bb3
    push di                                   ; 57                          ; 0xc1bb4
    sub sp, strict byte 0000ch                ; 83 ec 0c                    ; 0xc1bb5
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc1bb8
    mov al, dl                                ; 88 d0                       ; 0xc1bbb
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc1bbd
    mov bx, cx                                ; 89 cb                       ; 0xc1bc0
    xor ah, ah                                ; 30 e4                       ; 0xc1bc2 vgabios.c:1285
    mov si, ax                                ; 89 c6                       ; 0xc1bc4
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1bc6
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc1bc9
    mov ax, si                                ; 89 f0                       ; 0xc1bcc
    imul word [bp-010h]                       ; f7 6e f0                    ; 0xc1bce
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1bd1
    mov si, ax                                ; 89 c6                       ; 0xc1bd4
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1bd6
    xor ah, ah                                ; 30 e4                       ; 0xc1bd9
    mov di, ax                                ; 89 c7                       ; 0xc1bdb
    add si, ax                                ; 01 c6                       ; 0xc1bdd
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1bdf
    sal si, CL                                ; d3 e6                       ; 0xc1be1
    mov word [bp-00ch], si                    ; 89 76 f4                    ; 0xc1be3
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1be6 vgabios.c:1286
    imul word [bp-010h]                       ; f7 6e f0                    ; 0xc1be9
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1bec
    add ax, di                                ; 01 f8                       ; 0xc1bef
    sal ax, CL                                ; d3 e0                       ; 0xc1bf1
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc1bf3
    sal bx, CL                                ; d3 e3                       ; 0xc1bf6 vgabios.c:1287
    sal word [bp+004h], CL                    ; d3 66 04                    ; 0xc1bf8 vgabios.c:1288
    mov byte [bp-006h], 000h                  ; c6 46 fa 00                 ; 0xc1bfb vgabios.c:1289
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1bff
    cmp al, byte [bp+006h]                    ; 3a 46 06                    ; 0xc1c02
    jnc short 01c2ah                          ; 73 23                       ; 0xc1c05
    xor ah, ah                                ; 30 e4                       ; 0xc1c07 vgabios.c:1291
    mul word [bp+004h]                        ; f7 66 04                    ; 0xc1c09
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc1c0c
    add si, ax                                ; 01 c6                       ; 0xc1c0f
    mov di, word [bp-00eh]                    ; 8b 7e f2                    ; 0xc1c11
    add di, ax                                ; 01 c7                       ; 0xc1c14
    mov cx, bx                                ; 89 d9                       ; 0xc1c16
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1c18
    mov es, dx                                ; 8e c2                       ; 0xc1c1b
    jcxz 01c25h                               ; e3 06                       ; 0xc1c1d
    push DS                                   ; 1e                          ; 0xc1c1f
    mov ds, dx                                ; 8e da                       ; 0xc1c20
    rep movsb                                 ; f3 a4                       ; 0xc1c22
    pop DS                                    ; 1f                          ; 0xc1c24
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc1c25 vgabios.c:1292
    jmp short 01bffh                          ; eb d5                       ; 0xc1c28
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1c2a vgabios.c:1293
    pop di                                    ; 5f                          ; 0xc1c2d
    pop si                                    ; 5e                          ; 0xc1c2e
    pop bp                                    ; 5d                          ; 0xc1c2f
    retn 00004h                               ; c2 04 00                    ; 0xc1c30
  ; disGetNextSymbol 0xc1c33 LB 0x2931 -> off=0x0 cb=000000000000006c uValue=00000000000c1c33 'vgamem_fill_linear'
vgamem_fill_linear:                          ; 0xc1c33 LB 0x6c
    push bp                                   ; 55                          ; 0xc1c33 vgabios.c:1296
    mov bp, sp                                ; 89 e5                       ; 0xc1c34
    push si                                   ; 56                          ; 0xc1c36
    push di                                   ; 57                          ; 0xc1c37
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc1c38
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1c3b
    mov al, dl                                ; 88 d0                       ; 0xc1c3e
    mov si, cx                                ; 89 ce                       ; 0xc1c40
    xor ah, ah                                ; 30 e4                       ; 0xc1c42 vgabios.c:1302
    mov dx, ax                                ; 89 c2                       ; 0xc1c44
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1c46
    mov di, ax                                ; 89 c7                       ; 0xc1c49
    mov ax, dx                                ; 89 d0                       ; 0xc1c4b
    imul di                                   ; f7 ef                       ; 0xc1c4d
    mul cx                                    ; f7 e1                       ; 0xc1c4f
    mov dx, ax                                ; 89 c2                       ; 0xc1c51
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1c53
    xor ah, ah                                ; 30 e4                       ; 0xc1c56
    add ax, dx                                ; 01 d0                       ; 0xc1c58
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1c5a
    sal ax, CL                                ; d3 e0                       ; 0xc1c5c
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc1c5e
    sal bx, CL                                ; d3 e3                       ; 0xc1c61 vgabios.c:1303
    sal si, CL                                ; d3 e6                       ; 0xc1c63 vgabios.c:1304
    mov byte [bp-008h], 000h                  ; c6 46 f8 00                 ; 0xc1c65 vgabios.c:1305
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1c69
    cmp al, byte [bp+004h]                    ; 3a 46 04                    ; 0xc1c6c
    jnc short 01c96h                          ; 73 25                       ; 0xc1c6f
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc1c71 vgabios.c:1307
    xor ah, ah                                ; 30 e4                       ; 0xc1c74
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc1c76
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1c79
    mul si                                    ; f7 e6                       ; 0xc1c7c
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc1c7e
    add di, ax                                ; 01 c7                       ; 0xc1c81
    mov cx, bx                                ; 89 d9                       ; 0xc1c83
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc1c85
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc1c88
    mov es, dx                                ; 8e c2                       ; 0xc1c8b
    jcxz 01c91h                               ; e3 02                       ; 0xc1c8d
    rep stosb                                 ; f3 aa                       ; 0xc1c8f
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc1c91 vgabios.c:1308
    jmp short 01c69h                          ; eb d3                       ; 0xc1c94
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc1c96 vgabios.c:1309
    pop di                                    ; 5f                          ; 0xc1c99
    pop si                                    ; 5e                          ; 0xc1c9a
    pop bp                                    ; 5d                          ; 0xc1c9b
    retn 00004h                               ; c2 04 00                    ; 0xc1c9c
  ; disGetNextSymbol 0xc1c9f LB 0x28c5 -> off=0x0 cb=00000000000006a3 uValue=00000000000c1c9f 'biosfn_scroll'
biosfn_scroll:                               ; 0xc1c9f LB 0x6a3
    push bp                                   ; 55                          ; 0xc1c9f vgabios.c:1312
    mov bp, sp                                ; 89 e5                       ; 0xc1ca0
    push si                                   ; 56                          ; 0xc1ca2
    push di                                   ; 57                          ; 0xc1ca3
    sub sp, strict byte 00020h                ; 83 ec 20                    ; 0xc1ca4
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc1ca7
    mov byte [bp-010h], dl                    ; 88 56 f0                    ; 0xc1caa
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc1cad
    mov byte [bp-008h], cl                    ; 88 4e f8                    ; 0xc1cb0
    mov ch, byte [bp+006h]                    ; 8a 6e 06                    ; 0xc1cb3
    cmp bl, byte [bp+004h]                    ; 3a 5e 04                    ; 0xc1cb6 vgabios.c:1321
    jnbe short 01cd6h                         ; 77 1b                       ; 0xc1cb9
    cmp ch, cl                                ; 38 cd                       ; 0xc1cbb vgabios.c:1322
    jc short 01cd6h                           ; 72 17                       ; 0xc1cbd
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc1cbf vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1cc2
    mov es, ax                                ; 8e c0                       ; 0xc1cc5
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1cc7
    xor ah, ah                                ; 30 e4                       ; 0xc1cca vgabios.c:1326
    call 03888h                               ; e8 b9 1b                    ; 0xc1ccc
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc1ccf
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc1cd2 vgabios.c:1327
    jne short 01cd9h                          ; 75 03                       ; 0xc1cd4
    jmp near 02339h                           ; e9 60 06                    ; 0xc1cd6
    mov bx, 00084h                            ; bb 84 00                    ; 0xc1cd9 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc1cdc
    mov es, ax                                ; 8e c0                       ; 0xc1cdf
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1ce1
    xor ah, ah                                ; 30 e4                       ; 0xc1ce4 vgabios.c:48
    inc ax                                    ; 40                          ; 0xc1ce6
    mov word [bp-024h], ax                    ; 89 46 dc                    ; 0xc1ce7
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc1cea vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc1ced
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc1cf0 vgabios.c:58
    cmp byte [bp+008h], 0ffh                  ; 80 7e 08 ff                 ; 0xc1cf3 vgabios.c:1334
    jne short 01d02h                          ; 75 09                       ; 0xc1cf7
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc1cf9 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc1cfc
    mov byte [bp+008h], al                    ; 88 46 08                    ; 0xc1cff vgabios.c:48
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1d02 vgabios.c:1337
    xor ah, ah                                ; 30 e4                       ; 0xc1d05
    cmp ax, word [bp-024h]                    ; 3b 46 dc                    ; 0xc1d07
    jc short 01d14h                           ; 72 08                       ; 0xc1d0a
    mov al, byte [bp-024h]                    ; 8a 46 dc                    ; 0xc1d0c
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc1d0f
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc1d11
    mov al, ch                                ; 88 e8                       ; 0xc1d14 vgabios.c:1338
    xor ah, ah                                ; 30 e4                       ; 0xc1d16
    cmp ax, word [bp-018h]                    ; 3b 46 e8                    ; 0xc1d18
    jc short 01d22h                           ; 72 05                       ; 0xc1d1b
    mov ch, byte [bp-018h]                    ; 8a 6e e8                    ; 0xc1d1d
    db  0feh, 0cdh
    ; dec ch                                    ; fe cd                     ; 0xc1d20
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1d22 vgabios.c:1339
    xor ah, ah                                ; 30 e4                       ; 0xc1d25
    cmp ax, word [bp-024h]                    ; 3b 46 dc                    ; 0xc1d27
    jbe short 01d2fh                          ; 76 03                       ; 0xc1d2a
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc1d2c
    mov al, ch                                ; 88 e8                       ; 0xc1d2f vgabios.c:1340
    sub al, byte [bp-008h]                    ; 2a 46 f8                    ; 0xc1d31
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc1d34
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc1d36
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1d39 vgabios.c:1342
    mov byte [bp-01eh], al                    ; 88 46 e2                    ; 0xc1d3c
    mov byte [bp-01dh], 000h                  ; c6 46 e3 00                 ; 0xc1d3f
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1d43
    mov bx, word [bp-01eh]                    ; 8b 5e e2                    ; 0xc1d45
    sal bx, CL                                ; d3 e3                       ; 0xc1d48
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc1d4a
    dec ax                                    ; 48                          ; 0xc1d4d
    mov word [bp-020h], ax                    ; 89 46 e0                    ; 0xc1d4e
    mov ax, word [bp-024h]                    ; 8b 46 dc                    ; 0xc1d51
    dec ax                                    ; 48                          ; 0xc1d54
    mov word [bp-022h], ax                    ; 89 46 de                    ; 0xc1d55
    mov ax, word [bp-018h]                    ; 8b 46 e8                    ; 0xc1d58
    mul word [bp-024h]                        ; f7 66 dc                    ; 0xc1d5b
    mov di, ax                                ; 89 c7                       ; 0xc1d5e
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc1d60
    jne short 01db1h                          ; 75 4a                       ; 0xc1d65
    sal ax, 1                                 ; d1 e0                       ; 0xc1d67 vgabios.c:1345
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc1d69
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc1d6b
    xor dh, dh                                ; 30 f6                       ; 0xc1d6e
    inc ax                                    ; 40                          ; 0xc1d70
    mul dx                                    ; f7 e2                       ; 0xc1d71
    mov word [bp-01ch], ax                    ; 89 46 e4                    ; 0xc1d73
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1d76 vgabios.c:1350
    jne short 01db4h                          ; 75 38                       ; 0xc1d7a
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1d7c
    jne short 01db4h                          ; 75 32                       ; 0xc1d80
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1d82
    jne short 01db4h                          ; 75 2c                       ; 0xc1d86
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1d88
    xor ah, ah                                ; 30 e4                       ; 0xc1d8b
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc1d8d
    jne short 01db4h                          ; 75 22                       ; 0xc1d90
    mov al, ch                                ; 88 e8                       ; 0xc1d92
    cmp ax, word [bp-020h]                    ; 3b 46 e0                    ; 0xc1d94
    jne short 01db4h                          ; 75 1b                       ; 0xc1d97
    mov ah, byte [bp-010h]                    ; 8a 66 f0                    ; 0xc1d99 vgabios.c:1352
    xor al, ch                                ; 30 e8                       ; 0xc1d9c
    add ax, strict word 00020h                ; 05 20 00                    ; 0xc1d9e
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1da1
    mov cx, di                                ; 89 f9                       ; 0xc1da5
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1da7
    jcxz 01daeh                               ; e3 02                       ; 0xc1daa
    rep stosw                                 ; f3 ab                       ; 0xc1dac
    jmp near 02339h                           ; e9 88 05                    ; 0xc1dae vgabios.c:1354
    jmp near 01f3eh                           ; e9 8a 01                    ; 0xc1db1
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1db4 vgabios.c:1356
    jne short 01e1fh                          ; 75 65                       ; 0xc1db8
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1dba vgabios.c:1357
    xor ah, ah                                ; 30 e4                       ; 0xc1dbd
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc1dbf
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc1dc2
    xor dh, dh                                ; 30 f6                       ; 0xc1dc5
    cmp dx, word [bp-016h]                    ; 3b 56 ea                    ; 0xc1dc7
    jc short 01e21h                           ; 72 55                       ; 0xc1dca
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1dcc vgabios.c:1359
    xor ah, ah                                ; 30 e4                       ; 0xc1dcf
    add ax, word [bp-016h]                    ; 03 46 ea                    ; 0xc1dd1
    cmp ax, dx                                ; 39 d0                       ; 0xc1dd4
    jnbe short 01ddeh                         ; 77 06                       ; 0xc1dd6
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1dd8
    jne short 01e24h                          ; 75 46                       ; 0xc1ddc
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1dde vgabios.c:1360
    mov byte [bp-01ah], al                    ; 88 46 e6                    ; 0xc1de1
    xor al, al                                ; 30 c0                       ; 0xc1de4
    mov byte [bp-019h], al                    ; 88 46 e7                    ; 0xc1de6
    mov ah, byte [bp-010h]                    ; 8a 66 f0                    ; 0xc1de9
    mov si, ax                                ; 89 c6                       ; 0xc1dec
    add si, strict byte 00020h                ; 83 c6 20                    ; 0xc1dee
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc1df1
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc1df4
    mov dx, ax                                ; 89 c2                       ; 0xc1df7
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1df9
    xor ah, ah                                ; 30 e4                       ; 0xc1dfc
    add ax, dx                                ; 01 d0                       ; 0xc1dfe
    sal ax, 1                                 ; d1 e0                       ; 0xc1e00
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1e02
    add di, ax                                ; 01 c7                       ; 0xc1e05
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1e07
    xor bh, bh                                ; 30 ff                       ; 0xc1e0a
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1e0c
    sal bx, CL                                ; d3 e3                       ; 0xc1e0e
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc1e10
    mov cx, word [bp-01ah]                    ; 8b 4e e6                    ; 0xc1e14
    mov ax, si                                ; 89 f0                       ; 0xc1e17
    jcxz 01e1dh                               ; e3 02                       ; 0xc1e19
    rep stosw                                 ; f3 ab                       ; 0xc1e1b
    jmp short 01e6dh                          ; eb 4e                       ; 0xc1e1d vgabios.c:1361
    jmp short 01e73h                          ; eb 52                       ; 0xc1e1f
    jmp near 02339h                           ; e9 15 05                    ; 0xc1e21
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc1e24 vgabios.c:1362
    mov byte [bp-014h], dl                    ; 88 56 ec                    ; 0xc1e27
    mov byte [bp-013h], dh                    ; 88 76 ed                    ; 0xc1e2a
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc1e2d
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1e30
    mov byte [bp-01ah], dl                    ; 88 56 e6                    ; 0xc1e33
    mov byte [bp-019h], 000h                  ; c6 46 e7 00                 ; 0xc1e36
    mov si, ax                                ; 89 c6                       ; 0xc1e3a
    add si, word [bp-01ah]                    ; 03 76 e6                    ; 0xc1e3c
    sal si, 1                                 ; d1 e6                       ; 0xc1e3f
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1e41
    xor bh, bh                                ; 30 ff                       ; 0xc1e44
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1e46
    sal bx, CL                                ; d3 e3                       ; 0xc1e48
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc1e4a
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc1e4e
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc1e51
    add ax, word [bp-01ah]                    ; 03 46 e6                    ; 0xc1e54
    sal ax, 1                                 ; d1 e0                       ; 0xc1e57
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1e59
    add di, ax                                ; 01 c7                       ; 0xc1e5c
    mov cx, word [bp-014h]                    ; 8b 4e ec                    ; 0xc1e5e
    mov dx, bx                                ; 89 da                       ; 0xc1e61
    mov es, bx                                ; 8e c3                       ; 0xc1e63
    jcxz 01e6dh                               ; e3 06                       ; 0xc1e65
    push DS                                   ; 1e                          ; 0xc1e67
    mov ds, dx                                ; 8e da                       ; 0xc1e68
    rep movsw                                 ; f3 a5                       ; 0xc1e6a
    pop DS                                    ; 1f                          ; 0xc1e6c
    inc word [bp-016h]                        ; ff 46 ea                    ; 0xc1e6d vgabios.c:1363
    jmp near 01dc2h                           ; e9 4f ff                    ; 0xc1e70
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1e73 vgabios.c:1366
    xor ah, ah                                ; 30 e4                       ; 0xc1e76
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc1e78
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1e7b
    xor ah, ah                                ; 30 e4                       ; 0xc1e7e
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1e80
    jnbe short 01e21h                         ; 77 9c                       ; 0xc1e83
    mov dl, al                                ; 88 c2                       ; 0xc1e85 vgabios.c:1368
    xor dh, dh                                ; 30 f6                       ; 0xc1e87
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1e89
    add ax, dx                                ; 01 d0                       ; 0xc1e8c
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1e8e
    jnbe short 01e99h                         ; 77 06                       ; 0xc1e91
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1e93
    jne short 01ed9h                          ; 75 40                       ; 0xc1e97
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc1e99 vgabios.c:1369
    xor bh, bh                                ; 30 ff                       ; 0xc1e9c
    mov ah, byte [bp-010h]                    ; 8a 66 f0                    ; 0xc1e9e
    xor al, al                                ; 30 c0                       ; 0xc1ea1
    mov si, ax                                ; 89 c6                       ; 0xc1ea3
    add si, strict byte 00020h                ; 83 c6 20                    ; 0xc1ea5
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc1ea8
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc1eab
    mov dx, ax                                ; 89 c2                       ; 0xc1eae
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc1eb0
    xor ah, ah                                ; 30 e4                       ; 0xc1eb3
    add ax, dx                                ; 01 d0                       ; 0xc1eb5
    sal ax, 1                                 ; d1 e0                       ; 0xc1eb7
    mov dx, word [bp-01ch]                    ; 8b 56 e4                    ; 0xc1eb9
    add dx, ax                                ; 01 c2                       ; 0xc1ebc
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc1ebe
    xor ah, ah                                ; 30 e4                       ; 0xc1ec1
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1ec3
    mov di, ax                                ; 89 c7                       ; 0xc1ec5
    sal di, CL                                ; d3 e7                       ; 0xc1ec7
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc1ec9
    mov cx, bx                                ; 89 d9                       ; 0xc1ecd
    mov ax, si                                ; 89 f0                       ; 0xc1ecf
    mov di, dx                                ; 89 d7                       ; 0xc1ed1
    jcxz 01ed7h                               ; e3 02                       ; 0xc1ed3
    rep stosw                                 ; f3 ab                       ; 0xc1ed5
    jmp short 01f2eh                          ; eb 55                       ; 0xc1ed7 vgabios.c:1370
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc1ed9 vgabios.c:1371
    mov byte [bp-01ah], al                    ; 88 46 e6                    ; 0xc1edc
    mov byte [bp-019h], dh                    ; 88 76 e7                    ; 0xc1edf
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc1ee2
    xor ah, ah                                ; 30 e4                       ; 0xc1ee5
    mov dx, word [bp-016h]                    ; 8b 56 ea                    ; 0xc1ee7
    sub dx, ax                                ; 29 c2                       ; 0xc1eea
    mov ax, dx                                ; 89 d0                       ; 0xc1eec
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc1eee
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc1ef1
    mov byte [bp-014h], dl                    ; 88 56 ec                    ; 0xc1ef4
    mov byte [bp-013h], 000h                  ; c6 46 ed 00                 ; 0xc1ef7
    mov si, ax                                ; 89 c6                       ; 0xc1efb
    add si, word [bp-014h]                    ; 03 76 ec                    ; 0xc1efd
    sal si, 1                                 ; d1 e6                       ; 0xc1f00
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1f02
    xor bh, bh                                ; 30 ff                       ; 0xc1f05
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1f07
    sal bx, CL                                ; d3 e3                       ; 0xc1f09
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc1f0b
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc1f0f
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc1f12
    add ax, word [bp-014h]                    ; 03 46 ec                    ; 0xc1f15
    sal ax, 1                                 ; d1 e0                       ; 0xc1f18
    mov di, word [bp-01ch]                    ; 8b 7e e4                    ; 0xc1f1a
    add di, ax                                ; 01 c7                       ; 0xc1f1d
    mov cx, word [bp-01ah]                    ; 8b 4e e6                    ; 0xc1f1f
    mov dx, bx                                ; 89 da                       ; 0xc1f22
    mov es, bx                                ; 8e c3                       ; 0xc1f24
    jcxz 01f2eh                               ; e3 06                       ; 0xc1f26
    push DS                                   ; 1e                          ; 0xc1f28
    mov ds, dx                                ; 8e da                       ; 0xc1f29
    rep movsw                                 ; f3 a5                       ; 0xc1f2b
    pop DS                                    ; 1f                          ; 0xc1f2d
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1f2e vgabios.c:1372
    xor ah, ah                                ; 30 e4                       ; 0xc1f31
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1f33
    jc short 01f6ch                           ; 72 34                       ; 0xc1f36
    dec word [bp-016h]                        ; ff 4e ea                    ; 0xc1f38 vgabios.c:1373
    jmp near 01e7bh                           ; e9 3d ff                    ; 0xc1f3b
    mov si, word [bp-01eh]                    ; 8b 76 e2                    ; 0xc1f3e vgabios.c:1379
    mov al, byte [si+04831h]                  ; 8a 84 31 48                 ; 0xc1f41
    xor ah, ah                                ; 30 e4                       ; 0xc1f45
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc1f47
    mov si, ax                                ; 89 c6                       ; 0xc1f49
    sal si, CL                                ; d3 e6                       ; 0xc1f4b
    mov al, byte [si+04847h]                  ; 8a 84 47 48                 ; 0xc1f4d
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc1f51
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc1f54 vgabios.c:1380
    cmp AL, strict byte 003h                  ; 3c 03                       ; 0xc1f58
    jc short 01f68h                           ; 72 0c                       ; 0xc1f5a
    jbe short 01f6fh                          ; 76 11                       ; 0xc1f5c
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc1f5e
    je short 01f9ch                           ; 74 3a                       ; 0xc1f60
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc1f62
    je short 01f6fh                           ; 74 09                       ; 0xc1f64
    jmp short 01f6ch                          ; eb 04                       ; 0xc1f66
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc1f68
    je short 01f9fh                           ; 74 33                       ; 0xc1f6a
    jmp near 02339h                           ; e9 ca 03                    ; 0xc1f6c
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc1f6f vgabios.c:1384
    jne short 01f9ah                          ; 75 25                       ; 0xc1f73
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc1f75
    jne short 01fddh                          ; 75 62                       ; 0xc1f79
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc1f7b
    jne short 01fddh                          ; 75 5c                       ; 0xc1f7f
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1f81
    xor ah, ah                                ; 30 e4                       ; 0xc1f84
    mov dx, word [bp-024h]                    ; 8b 56 dc                    ; 0xc1f86
    dec dx                                    ; 4a                          ; 0xc1f89
    cmp ax, dx                                ; 39 d0                       ; 0xc1f8a
    jne short 01fddh                          ; 75 4f                       ; 0xc1f8c
    mov al, ch                                ; 88 e8                       ; 0xc1f8e
    xor ah, dh                                ; 30 f4                       ; 0xc1f90
    mov dx, word [bp-018h]                    ; 8b 56 e8                    ; 0xc1f92
    dec dx                                    ; 4a                          ; 0xc1f95
    cmp ax, dx                                ; 39 d0                       ; 0xc1f96
    je short 01fa2h                           ; 74 08                       ; 0xc1f98
    jmp short 01fddh                          ; eb 41                       ; 0xc1f9a
    jmp near 0221dh                           ; e9 7e 02                    ; 0xc1f9c
    jmp near 020c9h                           ; e9 27 01                    ; 0xc1f9f
    mov ax, 00205h                            ; b8 05 02                    ; 0xc1fa2 vgabios.c:1386
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1fa5
    out DX, ax                                ; ef                          ; 0xc1fa8
    mov ax, word [bp-024h]                    ; 8b 46 dc                    ; 0xc1fa9 vgabios.c:1387
    mul word [bp-018h]                        ; f7 66 e8                    ; 0xc1fac
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc1faf
    xor dh, dh                                ; 30 f6                       ; 0xc1fb2
    mul dx                                    ; f7 e2                       ; 0xc1fb4
    mov dx, ax                                ; 89 c2                       ; 0xc1fb6
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc1fb8
    xor ah, ah                                ; 30 e4                       ; 0xc1fbb
    mov bl, byte [bp-012h]                    ; 8a 5e ee                    ; 0xc1fbd
    xor bh, bh                                ; 30 ff                       ; 0xc1fc0
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc1fc2
    sal bx, CL                                ; d3 e3                       ; 0xc1fc4
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc1fc6
    mov cx, dx                                ; 89 d1                       ; 0xc1fca
    xor di, di                                ; 31 ff                       ; 0xc1fcc
    mov es, bx                                ; 8e c3                       ; 0xc1fce
    jcxz 01fd4h                               ; e3 02                       ; 0xc1fd0
    rep stosb                                 ; f3 aa                       ; 0xc1fd2
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc1fd4 vgabios.c:1388
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc1fd7
    out DX, ax                                ; ef                          ; 0xc1fda
    jmp short 01f6ch                          ; eb 8f                       ; 0xc1fdb vgabios.c:1390
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc1fdd vgabios.c:1392
    jne short 0204fh                          ; 75 6c                       ; 0xc1fe1
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc1fe3 vgabios.c:1393
    xor ah, ah                                ; 30 e4                       ; 0xc1fe6
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc1fe8
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc1feb
    xor ah, ah                                ; 30 e4                       ; 0xc1fee
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc1ff0
    jc short 0204ch                           ; 72 57                       ; 0xc1ff3
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc1ff5 vgabios.c:1395
    xor dh, dh                                ; 30 f6                       ; 0xc1ff8
    add dx, word [bp-016h]                    ; 03 56 ea                    ; 0xc1ffa
    cmp dx, ax                                ; 39 c2                       ; 0xc1ffd
    jnbe short 02007h                         ; 77 06                       ; 0xc1fff
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2001
    jne short 02028h                          ; 75 21                       ; 0xc2005
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2007 vgabios.c:1396
    xor ah, ah                                ; 30 e4                       ; 0xc200a
    push ax                                   ; 50                          ; 0xc200c
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc200d
    push ax                                   ; 50                          ; 0xc2010
    mov cl, byte [bp-018h]                    ; 8a 4e e8                    ; 0xc2011
    xor ch, ch                                ; 30 ed                       ; 0xc2014
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2016
    xor bh, bh                                ; 30 ff                       ; 0xc2019
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc201b
    xor dh, dh                                ; 30 f6                       ; 0xc201e
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2020
    call 019ebh                               ; e8 c5 f9                    ; 0xc2023
    jmp short 02047h                          ; eb 1f                       ; 0xc2026 vgabios.c:1397
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2028 vgabios.c:1398
    push ax                                   ; 50                          ; 0xc202b
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc202c
    push ax                                   ; 50                          ; 0xc202f
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2030
    xor ch, ch                                ; 30 ed                       ; 0xc2033
    mov bl, byte [bp-016h]                    ; 8a 5e ea                    ; 0xc2035
    xor bh, bh                                ; 30 ff                       ; 0xc2038
    mov dl, bl                                ; 88 da                       ; 0xc203a
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc203c
    xor dh, dh                                ; 30 f6                       ; 0xc203f
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2041
    call 0195dh                               ; e8 16 f9                    ; 0xc2044
    inc word [bp-016h]                        ; ff 46 ea                    ; 0xc2047 vgabios.c:1399
    jmp short 01febh                          ; eb 9f                       ; 0xc204a
    jmp near 02339h                           ; e9 ea 02                    ; 0xc204c
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc204f vgabios.c:1402
    xor ah, ah                                ; 30 e4                       ; 0xc2052
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc2054
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2057
    xor ah, ah                                ; 30 e4                       ; 0xc205a
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc205c
    jnbe short 0204ch                         ; 77 eb                       ; 0xc205f
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2061 vgabios.c:1404
    xor dh, dh                                ; 30 f6                       ; 0xc2064
    add ax, dx                                ; 01 d0                       ; 0xc2066
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc2068
    jnbe short 02071h                         ; 77 04                       ; 0xc206b
    test dl, dl                               ; 84 d2                       ; 0xc206d
    jne short 02092h                          ; 75 21                       ; 0xc206f
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2071 vgabios.c:1405
    xor ah, ah                                ; 30 e4                       ; 0xc2074
    push ax                                   ; 50                          ; 0xc2076
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2077
    push ax                                   ; 50                          ; 0xc207a
    mov cl, byte [bp-018h]                    ; 8a 4e e8                    ; 0xc207b
    xor ch, ch                                ; 30 ed                       ; 0xc207e
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2080
    xor bh, bh                                ; 30 ff                       ; 0xc2083
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc2085
    xor dh, dh                                ; 30 f6                       ; 0xc2088
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc208a
    call 019ebh                               ; e8 5b f9                    ; 0xc208d
    jmp short 020bah                          ; eb 28                       ; 0xc2090 vgabios.c:1406
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2092 vgabios.c:1407
    xor ah, ah                                ; 30 e4                       ; 0xc2095
    push ax                                   ; 50                          ; 0xc2097
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc2098
    push ax                                   ; 50                          ; 0xc209b
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc209c
    xor ch, ch                                ; 30 ed                       ; 0xc209f
    mov bl, byte [bp-016h]                    ; 8a 5e ea                    ; 0xc20a1
    xor bh, bh                                ; 30 ff                       ; 0xc20a4
    mov dl, bl                                ; 88 da                       ; 0xc20a6
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc20a8
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc20ab
    mov byte [bp-01ah], al                    ; 88 46 e6                    ; 0xc20ae
    mov byte [bp-019h], dh                    ; 88 76 e7                    ; 0xc20b1
    mov ax, word [bp-01ah]                    ; 8b 46 e6                    ; 0xc20b4
    call 0195dh                               ; e8 a3 f8                    ; 0xc20b7
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc20ba vgabios.c:1408
    xor ah, ah                                ; 30 e4                       ; 0xc20bd
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc20bf
    jc short 02112h                           ; 72 4e                       ; 0xc20c2
    dec word [bp-016h]                        ; ff 4e ea                    ; 0xc20c4 vgabios.c:1409
    jmp short 02057h                          ; eb 8e                       ; 0xc20c7
    mov cl, byte [bx+047b4h]                  ; 8a 8f b4 47                 ; 0xc20c9 vgabios.c:1414
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc20cd vgabios.c:1415
    jne short 02115h                          ; 75 42                       ; 0xc20d1
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc20d3
    jne short 02115h                          ; 75 3c                       ; 0xc20d7
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc20d9
    jne short 02115h                          ; 75 36                       ; 0xc20dd
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc20df
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc20e2
    jne short 02115h                          ; 75 2e                       ; 0xc20e5
    mov al, ch                                ; 88 e8                       ; 0xc20e7
    cmp ax, word [bp-020h]                    ; 3b 46 e0                    ; 0xc20e9
    jne short 02115h                          ; 75 27                       ; 0xc20ec
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc20ee vgabios.c:1417
    xor dh, dh                                ; 30 f6                       ; 0xc20f1
    mov ax, di                                ; 89 f8                       ; 0xc20f3
    mul dx                                    ; f7 e2                       ; 0xc20f5
    mov dl, cl                                ; 88 ca                       ; 0xc20f7
    xor dh, dh                                ; 30 f6                       ; 0xc20f9
    mul dx                                    ; f7 e2                       ; 0xc20fb
    mov dl, byte [bp-010h]                    ; 8a 56 f0                    ; 0xc20fd
    xor dh, dh                                ; 30 f6                       ; 0xc2100
    mov bx, word [bx+047b5h]                  ; 8b 9f b5 47                 ; 0xc2102
    mov cx, ax                                ; 89 c1                       ; 0xc2106
    mov ax, dx                                ; 89 d0                       ; 0xc2108
    xor di, di                                ; 31 ff                       ; 0xc210a
    mov es, bx                                ; 8e c3                       ; 0xc210c
    jcxz 02112h                               ; e3 02                       ; 0xc210e
    rep stosb                                 ; f3 aa                       ; 0xc2110
    jmp near 02339h                           ; e9 24 02                    ; 0xc2112 vgabios.c:1419
    cmp cl, 002h                              ; 80 f9 02                    ; 0xc2115 vgabios.c:1421
    jne short 02123h                          ; 75 09                       ; 0xc2118
    sal byte [bp-008h], 1                     ; d0 66 f8                    ; 0xc211a vgabios.c:1423
    sal byte [bp-00ah], 1                     ; d0 66 f6                    ; 0xc211d vgabios.c:1424
    sal word [bp-018h], 1                     ; d1 66 e8                    ; 0xc2120 vgabios.c:1425
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc2123 vgabios.c:1428
    jne short 02192h                          ; 75 69                       ; 0xc2127
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2129 vgabios.c:1429
    xor ah, ah                                ; 30 e4                       ; 0xc212c
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc212e
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2131
    xor ah, ah                                ; 30 e4                       ; 0xc2134
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc2136
    jc short 02112h                           ; 72 d7                       ; 0xc2139
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc213b vgabios.c:1431
    xor dh, dh                                ; 30 f6                       ; 0xc213e
    add dx, word [bp-016h]                    ; 03 56 ea                    ; 0xc2140
    cmp dx, ax                                ; 39 c2                       ; 0xc2143
    jnbe short 0214dh                         ; 77 06                       ; 0xc2145
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2147
    jne short 0216eh                          ; 75 21                       ; 0xc214b
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc214d vgabios.c:1432
    xor ah, ah                                ; 30 e4                       ; 0xc2150
    push ax                                   ; 50                          ; 0xc2152
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2153
    push ax                                   ; 50                          ; 0xc2156
    mov cl, byte [bp-018h]                    ; 8a 4e e8                    ; 0xc2157
    xor ch, ch                                ; 30 ed                       ; 0xc215a
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc215c
    xor bh, bh                                ; 30 ff                       ; 0xc215f
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc2161
    xor dh, dh                                ; 30 f6                       ; 0xc2164
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2166
    call 01b1ch                               ; e8 b0 f9                    ; 0xc2169
    jmp short 0218dh                          ; eb 1f                       ; 0xc216c vgabios.c:1433
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc216e vgabios.c:1434
    push ax                                   ; 50                          ; 0xc2171
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc2172
    push ax                                   ; 50                          ; 0xc2175
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2176
    xor ch, ch                                ; 30 ed                       ; 0xc2179
    mov bl, byte [bp-016h]                    ; 8a 5e ea                    ; 0xc217b
    xor bh, bh                                ; 30 ff                       ; 0xc217e
    mov dl, bl                                ; 88 da                       ; 0xc2180
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc2182
    xor dh, dh                                ; 30 f6                       ; 0xc2185
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2187
    call 01a66h                               ; e8 d9 f8                    ; 0xc218a
    inc word [bp-016h]                        ; ff 46 ea                    ; 0xc218d vgabios.c:1435
    jmp short 02131h                          ; eb 9f                       ; 0xc2190
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2192 vgabios.c:1438
    xor ah, ah                                ; 30 e4                       ; 0xc2195
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc2197
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc219a
    xor ah, ah                                ; 30 e4                       ; 0xc219d
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc219f
    jnbe short 021e2h                         ; 77 3e                       ; 0xc21a2
    mov dl, al                                ; 88 c2                       ; 0xc21a4 vgabios.c:1440
    xor dh, dh                                ; 30 f6                       ; 0xc21a6
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc21a8
    add ax, dx                                ; 01 d0                       ; 0xc21ab
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc21ad
    jnbe short 021b8h                         ; 77 06                       ; 0xc21b0
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc21b2
    jne short 021e5h                          ; 75 2d                       ; 0xc21b6
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc21b8 vgabios.c:1441
    xor ah, ah                                ; 30 e4                       ; 0xc21bb
    push ax                                   ; 50                          ; 0xc21bd
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc21be
    push ax                                   ; 50                          ; 0xc21c1
    mov cl, byte [bp-018h]                    ; 8a 4e e8                    ; 0xc21c2
    xor ch, ch                                ; 30 ed                       ; 0xc21c5
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc21c7
    xor bh, bh                                ; 30 ff                       ; 0xc21ca
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc21cc
    xor dh, dh                                ; 30 f6                       ; 0xc21cf
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc21d1
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc21d4
    mov byte [bp-013h], ah                    ; 88 66 ed                    ; 0xc21d7
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc21da
    call 01b1ch                               ; e8 3c f9                    ; 0xc21dd
    jmp short 0220dh                          ; eb 2b                       ; 0xc21e0 vgabios.c:1442
    jmp near 02339h                           ; e9 54 01                    ; 0xc21e2
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc21e5 vgabios.c:1443
    xor ah, ah                                ; 30 e4                       ; 0xc21e8
    push ax                                   ; 50                          ; 0xc21ea
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc21eb
    push ax                                   ; 50                          ; 0xc21ee
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc21ef
    xor ch, ch                                ; 30 ed                       ; 0xc21f2
    mov bl, byte [bp-016h]                    ; 8a 5e ea                    ; 0xc21f4
    xor bh, bh                                ; 30 ff                       ; 0xc21f7
    mov dl, bl                                ; 88 da                       ; 0xc21f9
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc21fb
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc21fe
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc2201
    mov byte [bp-013h], dh                    ; 88 76 ed                    ; 0xc2204
    mov ax, word [bp-014h]                    ; 8b 46 ec                    ; 0xc2207
    call 01a66h                               ; e8 59 f8                    ; 0xc220a
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc220d vgabios.c:1444
    xor ah, ah                                ; 30 e4                       ; 0xc2210
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc2212
    jc short 0225ch                           ; 72 45                       ; 0xc2215
    dec word [bp-016h]                        ; ff 4e ea                    ; 0xc2217 vgabios.c:1445
    jmp near 0219ah                           ; e9 7d ff                    ; 0xc221a
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc221d vgabios.c:1450
    jne short 0225fh                          ; 75 3c                       ; 0xc2221
    cmp byte [bp-00ch], 000h                  ; 80 7e f4 00                 ; 0xc2223
    jne short 0225fh                          ; 75 36                       ; 0xc2227
    cmp byte [bp-008h], 000h                  ; 80 7e f8 00                 ; 0xc2229
    jne short 0225fh                          ; 75 30                       ; 0xc222d
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc222f
    cmp ax, word [bp-022h]                    ; 3b 46 de                    ; 0xc2232
    jne short 0225fh                          ; 75 28                       ; 0xc2235
    mov al, ch                                ; 88 e8                       ; 0xc2237
    cmp ax, word [bp-020h]                    ; 3b 46 e0                    ; 0xc2239
    jne short 0225fh                          ; 75 21                       ; 0xc223c
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc223e vgabios.c:1452
    xor dh, dh                                ; 30 f6                       ; 0xc2241
    mov ax, di                                ; 89 f8                       ; 0xc2243
    mul dx                                    ; f7 e2                       ; 0xc2245
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2247
    sal ax, CL                                ; d3 e0                       ; 0xc2249
    mov cx, ax                                ; 89 c1                       ; 0xc224b
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc224d
    xor ah, ah                                ; 30 e4                       ; 0xc2250
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2252
    xor di, di                                ; 31 ff                       ; 0xc2256
    jcxz 0225ch                               ; e3 02                       ; 0xc2258
    rep stosb                                 ; f3 aa                       ; 0xc225a
    jmp near 02339h                           ; e9 da 00                    ; 0xc225c vgabios.c:1454
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc225f vgabios.c:1457
    jne short 022cbh                          ; 75 66                       ; 0xc2263
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2265 vgabios.c:1458
    xor ah, ah                                ; 30 e4                       ; 0xc2268
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc226a
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc226d
    xor ah, ah                                ; 30 e4                       ; 0xc2270
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc2272
    jc short 0225ch                           ; 72 e5                       ; 0xc2275
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2277 vgabios.c:1460
    xor dh, dh                                ; 30 f6                       ; 0xc227a
    add dx, word [bp-016h]                    ; 03 56 ea                    ; 0xc227c
    cmp dx, ax                                ; 39 c2                       ; 0xc227f
    jnbe short 02289h                         ; 77 06                       ; 0xc2281
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2283
    jne short 022a8h                          ; 75 1f                       ; 0xc2287
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc2289 vgabios.c:1461
    xor ah, ah                                ; 30 e4                       ; 0xc228c
    push ax                                   ; 50                          ; 0xc228e
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc228f
    push ax                                   ; 50                          ; 0xc2292
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc2293
    xor bh, bh                                ; 30 ff                       ; 0xc2296
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc2298
    xor dh, dh                                ; 30 f6                       ; 0xc229b
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc229d
    mov cx, word [bp-018h]                    ; 8b 4e e8                    ; 0xc22a0
    call 01c33h                               ; e8 8d f9                    ; 0xc22a3
    jmp short 022c6h                          ; eb 1e                       ; 0xc22a6 vgabios.c:1462
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc22a8 vgabios.c:1463
    push ax                                   ; 50                          ; 0xc22ab
    push word [bp-018h]                       ; ff 76 e8                    ; 0xc22ac
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc22af
    xor ch, ch                                ; 30 ed                       ; 0xc22b2
    mov bl, byte [bp-016h]                    ; 8a 5e ea                    ; 0xc22b4
    xor bh, bh                                ; 30 ff                       ; 0xc22b7
    mov dl, bl                                ; 88 da                       ; 0xc22b9
    add dl, byte [bp-006h]                    ; 02 56 fa                    ; 0xc22bb
    xor dh, dh                                ; 30 f6                       ; 0xc22be
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc22c0
    call 01bb0h                               ; e8 ea f8                    ; 0xc22c3
    inc word [bp-016h]                        ; ff 46 ea                    ; 0xc22c6 vgabios.c:1464
    jmp short 0226dh                          ; eb a2                       ; 0xc22c9
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc22cb vgabios.c:1467
    xor ah, ah                                ; 30 e4                       ; 0xc22ce
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc22d0
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc22d3
    xor ah, ah                                ; 30 e4                       ; 0xc22d6
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc22d8
    jnbe short 02339h                         ; 77 5c                       ; 0xc22db
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc22dd vgabios.c:1469
    xor dh, dh                                ; 30 f6                       ; 0xc22e0
    add ax, dx                                ; 01 d0                       ; 0xc22e2
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc22e4
    jnbe short 022edh                         ; 77 04                       ; 0xc22e7
    test dl, dl                               ; 84 d2                       ; 0xc22e9
    jne short 0230ch                          ; 75 1f                       ; 0xc22eb
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc22ed vgabios.c:1470
    xor ah, ah                                ; 30 e4                       ; 0xc22f0
    push ax                                   ; 50                          ; 0xc22f2
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc22f3
    push ax                                   ; 50                          ; 0xc22f6
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc22f7
    xor bh, bh                                ; 30 ff                       ; 0xc22fa
    mov dl, byte [bp-016h]                    ; 8a 56 ea                    ; 0xc22fc
    xor dh, dh                                ; 30 f6                       ; 0xc22ff
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2301
    mov cx, word [bp-018h]                    ; 8b 4e e8                    ; 0xc2304
    call 01c33h                               ; e8 29 f9                    ; 0xc2307
    jmp short 0232ah                          ; eb 1e                       ; 0xc230a vgabios.c:1471
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc230c vgabios.c:1472
    xor ah, ah                                ; 30 e4                       ; 0xc230f
    push ax                                   ; 50                          ; 0xc2311
    push word [bp-018h]                       ; ff 76 e8                    ; 0xc2312
    mov cl, byte [bp-00ah]                    ; 8a 4e f6                    ; 0xc2315
    xor ch, ch                                ; 30 ed                       ; 0xc2318
    mov bl, byte [bp-016h]                    ; 8a 5e ea                    ; 0xc231a
    xor bh, bh                                ; 30 ff                       ; 0xc231d
    mov dl, bl                                ; 88 da                       ; 0xc231f
    sub dl, byte [bp-006h]                    ; 2a 56 fa                    ; 0xc2321
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2324
    call 01bb0h                               ; e8 86 f8                    ; 0xc2327
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc232a vgabios.c:1473
    xor ah, ah                                ; 30 e4                       ; 0xc232d
    cmp ax, word [bp-016h]                    ; 3b 46 ea                    ; 0xc232f
    jc short 02339h                           ; 72 05                       ; 0xc2332
    dec word [bp-016h]                        ; ff 4e ea                    ; 0xc2334 vgabios.c:1474
    jmp short 022d3h                          ; eb 9a                       ; 0xc2337
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2339 vgabios.c:1485
    pop di                                    ; 5f                          ; 0xc233c
    pop si                                    ; 5e                          ; 0xc233d
    pop bp                                    ; 5d                          ; 0xc233e
    retn 00008h                               ; c2 08 00                    ; 0xc233f
  ; disGetNextSymbol 0xc2342 LB 0x2222 -> off=0x0 cb=0000000000000112 uValue=00000000000c2342 'write_gfx_char_pl4'
write_gfx_char_pl4:                          ; 0xc2342 LB 0x112
    push bp                                   ; 55                          ; 0xc2342 vgabios.c:1488
    mov bp, sp                                ; 89 e5                       ; 0xc2343
    push si                                   ; 56                          ; 0xc2345
    push di                                   ; 57                          ; 0xc2346
    sub sp, strict byte 00010h                ; 83 ec 10                    ; 0xc2347
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc234a
    mov byte [bp-00ah], dl                    ; 88 56 f6                    ; 0xc234d
    mov byte [bp-006h], bl                    ; 88 5e fa                    ; 0xc2350
    mov al, cl                                ; 88 c8                       ; 0xc2353
    mov bx, 0010ch                            ; bb 0c 01                    ; 0xc2355 vgabios.c:67
    xor cx, cx                                ; 31 c9                       ; 0xc2358
    mov es, cx                                ; 8e c1                       ; 0xc235a
    mov cx, word [es:bx]                      ; 26 8b 0f                    ; 0xc235c
    mov bx, word [es:bx+002h]                 ; 26 8b 5f 02                 ; 0xc235f
    mov word [bp-014h], cx                    ; 89 4e ec                    ; 0xc2363 vgabios.c:68
    mov word [bp-010h], bx                    ; 89 5e f0                    ; 0xc2366
    xor ah, ah                                ; 30 e4                       ; 0xc2369 vgabios.c:1497
    mov cl, byte [bp+006h]                    ; 8a 4e 06                    ; 0xc236b
    xor ch, ch                                ; 30 ed                       ; 0xc236e
    imul cx                                   ; f7 e9                       ; 0xc2370
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc2372
    xor bh, bh                                ; 30 ff                       ; 0xc2375
    imul bx                                   ; f7 eb                       ; 0xc2377
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2379
    mov si, bx                                ; 89 de                       ; 0xc237c
    add si, ax                                ; 01 c6                       ; 0xc237e
    mov bx, strict word 0004ch                ; bb 4c 00                    ; 0xc2380 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2383
    mov es, ax                                ; 8e c0                       ; 0xc2386
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc2388
    mov bl, byte [bp+008h]                    ; 8a 5e 08                    ; 0xc238b vgabios.c:58
    xor bh, bh                                ; 30 ff                       ; 0xc238e
    mul bx                                    ; f7 e3                       ; 0xc2390
    add si, ax                                ; 01 c6                       ; 0xc2392
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2394 vgabios.c:1499
    xor ah, ah                                ; 30 e4                       ; 0xc2397
    imul cx                                   ; f7 e9                       ; 0xc2399
    mov word [bp-012h], ax                    ; 89 46 ee                    ; 0xc239b
    mov ax, 00f02h                            ; b8 02 0f                    ; 0xc239e vgabios.c:1500
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc23a1
    out DX, ax                                ; ef                          ; 0xc23a4
    mov ax, 00205h                            ; b8 05 02                    ; 0xc23a5 vgabios.c:1501
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc23a8
    out DX, ax                                ; ef                          ; 0xc23ab
    test byte [bp-00ah], 080h                 ; f6 46 f6 80                 ; 0xc23ac vgabios.c:1502
    je short 023b8h                           ; 74 06                       ; 0xc23b0
    mov ax, 01803h                            ; b8 03 18                    ; 0xc23b2 vgabios.c:1504
    out DX, ax                                ; ef                          ; 0xc23b5
    jmp short 023bch                          ; eb 04                       ; 0xc23b6 vgabios.c:1506
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc23b8 vgabios.c:1508
    out DX, ax                                ; ef                          ; 0xc23bb
    xor ch, ch                                ; 30 ed                       ; 0xc23bc vgabios.c:1510
    cmp ch, byte [bp+006h]                    ; 3a 6e 06                    ; 0xc23be
    jnc short 023d8h                          ; 73 15                       ; 0xc23c1
    mov al, ch                                ; 88 e8                       ; 0xc23c3 vgabios.c:1512
    xor ah, ah                                ; 30 e4                       ; 0xc23c5
    mov bl, byte [bp+004h]                    ; 8a 5e 04                    ; 0xc23c7
    xor bh, bh                                ; 30 ff                       ; 0xc23ca
    imul bx                                   ; f7 eb                       ; 0xc23cc
    mov bx, si                                ; 89 f3                       ; 0xc23ce
    add bx, ax                                ; 01 c3                       ; 0xc23d0
    mov byte [bp-008h], 000h                  ; c6 46 f8 00                 ; 0xc23d2 vgabios.c:1513
    jmp short 023ech                          ; eb 14                       ; 0xc23d6
    jmp short 0243ch                          ; eb 62                       ; 0xc23d8 vgabios.c:1522
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc23da vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc23dd
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc23df
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc23e3 vgabios.c:1526
    cmp byte [bp-008h], 008h                  ; 80 7e f8 08                 ; 0xc23e6
    jnc short 02438h                          ; 73 4c                       ; 0xc23ea
    mov cl, byte [bp-008h]                    ; 8a 4e f8                    ; 0xc23ec
    mov ax, 00080h                            ; b8 80 00                    ; 0xc23ef
    sar ax, CL                                ; d3 f8                       ; 0xc23f2
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc23f4
    mov byte [bp-00dh], 000h                  ; c6 46 f3 00                 ; 0xc23f7
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc23fb
    mov ah, al                                ; 88 c4                       ; 0xc23fe
    xor al, al                                ; 30 c0                       ; 0xc2400
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc2402
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2404
    out DX, ax                                ; ef                          ; 0xc2407
    mov dx, bx                                ; 89 da                       ; 0xc2408
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc240a
    call 038b3h                               ; e8 a3 14                    ; 0xc240d
    mov al, ch                                ; 88 e8                       ; 0xc2410
    xor ah, ah                                ; 30 e4                       ; 0xc2412
    add ax, word [bp-012h]                    ; 03 46 ee                    ; 0xc2414
    mov es, [bp-010h]                         ; 8e 46 f0                    ; 0xc2417
    mov di, word [bp-014h]                    ; 8b 7e ec                    ; 0xc241a
    add di, ax                                ; 01 c7                       ; 0xc241d
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc241f
    xor ah, ah                                ; 30 e4                       ; 0xc2422
    test word [bp-00eh], ax                   ; 85 46 f2                    ; 0xc2424
    je short 023dah                           ; 74 b1                       ; 0xc2427
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2429
    and AL, strict byte 00fh                  ; 24 0f                       ; 0xc242c
    mov di, 0a000h                            ; bf 00 a0                    ; 0xc242e
    mov es, di                                ; 8e c7                       ; 0xc2431
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2433
    jmp short 023e3h                          ; eb ab                       ; 0xc2436
    db  0feh, 0c5h
    ; inc ch                                    ; fe c5                     ; 0xc2438 vgabios.c:1527
    jmp short 023beh                          ; eb 82                       ; 0xc243a
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc243c vgabios.c:1528
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc243f
    out DX, ax                                ; ef                          ; 0xc2442
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2443 vgabios.c:1529
    out DX, ax                                ; ef                          ; 0xc2446
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc2447 vgabios.c:1530
    out DX, ax                                ; ef                          ; 0xc244a
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc244b vgabios.c:1531
    pop di                                    ; 5f                          ; 0xc244e
    pop si                                    ; 5e                          ; 0xc244f
    pop bp                                    ; 5d                          ; 0xc2450
    retn 00006h                               ; c2 06 00                    ; 0xc2451
  ; disGetNextSymbol 0xc2454 LB 0x2110 -> off=0x0 cb=0000000000000112 uValue=00000000000c2454 'write_gfx_char_cga'
write_gfx_char_cga:                          ; 0xc2454 LB 0x112
    push si                                   ; 56                          ; 0xc2454 vgabios.c:1534
    push di                                   ; 57                          ; 0xc2455
    push bp                                   ; 55                          ; 0xc2456
    mov bp, sp                                ; 89 e5                       ; 0xc2457
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2459
    mov ch, al                                ; 88 c5                       ; 0xc245c
    mov byte [bp-002h], dl                    ; 88 56 fe                    ; 0xc245e
    mov al, bl                                ; 88 d8                       ; 0xc2461
    mov si, 0556fh                            ; be 6f 55                    ; 0xc2463 vgabios.c:1541
    xor ah, ah                                ; 30 e4                       ; 0xc2466 vgabios.c:1542
    mov bl, byte [bp+00ah]                    ; 8a 5e 0a                    ; 0xc2468
    xor bh, bh                                ; 30 ff                       ; 0xc246b
    imul bx                                   ; f7 eb                       ; 0xc246d
    mov bx, ax                                ; 89 c3                       ; 0xc246f
    mov al, cl                                ; 88 c8                       ; 0xc2471
    xor ah, ah                                ; 30 e4                       ; 0xc2473
    mov di, 00140h                            ; bf 40 01                    ; 0xc2475
    imul di                                   ; f7 ef                       ; 0xc2478
    add bx, ax                                ; 01 c3                       ; 0xc247a
    mov word [bp-004h], bx                    ; 89 5e fc                    ; 0xc247c
    mov al, ch                                ; 88 e8                       ; 0xc247f vgabios.c:1543
    xor ah, ah                                ; 30 e4                       ; 0xc2481
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2483
    sal ax, CL                                ; d3 e0                       ; 0xc2485
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc2487
    xor ch, ch                                ; 30 ed                       ; 0xc248a vgabios.c:1544
    jmp near 024abh                           ; e9 1c 00                    ; 0xc248c
    mov al, ch                                ; 88 e8                       ; 0xc248f vgabios.c:1559
    xor ah, ah                                ; 30 e4                       ; 0xc2491
    add ax, word [bp-008h]                    ; 03 46 f8                    ; 0xc2493
    mov di, si                                ; 89 f7                       ; 0xc2496
    add di, ax                                ; 01 c7                       ; 0xc2498
    mov al, byte [di]                         ; 8a 05                       ; 0xc249a
    mov di, 0b800h                            ; bf 00 b8                    ; 0xc249c vgabios.c:52
    mov es, di                                ; 8e c7                       ; 0xc249f
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc24a1
    db  0feh, 0c5h
    ; inc ch                                    ; fe c5                     ; 0xc24a4 vgabios.c:1563
    cmp ch, 008h                              ; 80 fd 08                    ; 0xc24a6
    jnc short 02503h                          ; 73 58                       ; 0xc24a9
    mov al, ch                                ; 88 e8                       ; 0xc24ab
    xor ah, ah                                ; 30 e4                       ; 0xc24ad
    sar ax, 1                                 ; d1 f8                       ; 0xc24af
    mov bx, strict word 00050h                ; bb 50 00                    ; 0xc24b1
    imul bx                                   ; f7 eb                       ; 0xc24b4
    mov bx, word [bp-004h]                    ; 8b 5e fc                    ; 0xc24b6
    add bx, ax                                ; 01 c3                       ; 0xc24b9
    test ch, 001h                             ; f6 c5 01                    ; 0xc24bb
    je short 024c3h                           ; 74 03                       ; 0xc24be
    add bh, 020h                              ; 80 c7 20                    ; 0xc24c0
    mov DL, strict byte 080h                  ; b2 80                       ; 0xc24c3
    cmp byte [bp+00ah], 001h                  ; 80 7e 0a 01                 ; 0xc24c5
    jne short 024e9h                          ; 75 1e                       ; 0xc24c9
    test byte [bp-002h], dl                   ; 84 56 fe                    ; 0xc24cb
    je short 0248fh                           ; 74 bf                       ; 0xc24ce
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc24d0
    mov es, ax                                ; 8e c0                       ; 0xc24d3
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc24d5
    mov al, ch                                ; 88 e8                       ; 0xc24d8
    xor ah, ah                                ; 30 e4                       ; 0xc24da
    add ax, word [bp-008h]                    ; 03 46 f8                    ; 0xc24dc
    mov di, si                                ; 89 f7                       ; 0xc24df
    add di, ax                                ; 01 c7                       ; 0xc24e1
    mov al, byte [di]                         ; 8a 05                       ; 0xc24e3
    xor al, dl                                ; 30 d0                       ; 0xc24e5
    jmp short 0249ch                          ; eb b3                       ; 0xc24e7
    test dl, dl                               ; 84 d2                       ; 0xc24e9 vgabios.c:1565
    jbe short 024a4h                          ; 76 b7                       ; 0xc24eb
    test byte [bp-002h], 080h                 ; f6 46 fe 80                 ; 0xc24ed vgabios.c:1567
    je short 024fdh                           ; 74 0a                       ; 0xc24f1
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc24f3 vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc24f6
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc24f8
    jmp short 024ffh                          ; eb 02                       ; 0xc24fb vgabios.c:1571
    xor al, al                                ; 30 c0                       ; 0xc24fd vgabios.c:1573
    xor ah, ah                                ; 30 e4                       ; 0xc24ff vgabios.c:1575
    jmp short 0250ah                          ; eb 07                       ; 0xc2501
    jmp short 0255eh                          ; eb 59                       ; 0xc2503
    cmp ah, 004h                              ; 80 fc 04                    ; 0xc2505
    jnc short 02553h                          ; 73 49                       ; 0xc2508
    mov byte [bp-006h], ch                    ; 88 6e fa                    ; 0xc250a vgabios.c:1577
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc250d
    mov di, word [bp-008h]                    ; 8b 7e f8                    ; 0xc2511
    add di, word [bp-006h]                    ; 03 7e fa                    ; 0xc2514
    add di, si                                ; 01 f7                       ; 0xc2517
    mov cl, byte [di]                         ; 8a 0d                       ; 0xc2519
    mov byte [bp-00ah], cl                    ; 88 4e f6                    ; 0xc251b
    mov byte [bp-009h], 000h                  ; c6 46 f7 00                 ; 0xc251e
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc2522
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc2525
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc2529
    test word [bp-006h], di                   ; 85 7e fa                    ; 0xc252c
    je short 0254dh                           ; 74 1c                       ; 0xc252f
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2531 vgabios.c:1578
    sub cl, ah                                ; 28 e1                       ; 0xc2533
    mov dh, byte [bp-002h]                    ; 8a 76 fe                    ; 0xc2535
    and dh, 003h                              ; 80 e6 03                    ; 0xc2538
    sal cl, 1                                 ; d0 e1                       ; 0xc253b
    sal dh, CL                                ; d2 e6                       ; 0xc253d
    mov cl, dh                                ; 88 f1                       ; 0xc253f
    test byte [bp-002h], 080h                 ; f6 46 fe 80                 ; 0xc2541 vgabios.c:1579
    je short 0254bh                           ; 74 04                       ; 0xc2545
    xor al, dh                                ; 30 f0                       ; 0xc2547 vgabios.c:1581
    jmp short 0254dh                          ; eb 02                       ; 0xc2549 vgabios.c:1583
    or al, dh                                 ; 08 f0                       ; 0xc254b vgabios.c:1585
    shr dl, 1                                 ; d0 ea                       ; 0xc254d vgabios.c:1588
    db  0feh, 0c4h
    ; inc ah                                    ; fe c4                     ; 0xc254f vgabios.c:1589
    jmp short 02505h                          ; eb b2                       ; 0xc2551
    mov di, 0b800h                            ; bf 00 b8                    ; 0xc2553 vgabios.c:52
    mov es, di                                ; 8e c7                       ; 0xc2556
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2558
    inc bx                                    ; 43                          ; 0xc255b vgabios.c:1591
    jmp short 024e9h                          ; eb 8b                       ; 0xc255c vgabios.c:1592
    mov sp, bp                                ; 89 ec                       ; 0xc255e vgabios.c:1595
    pop bp                                    ; 5d                          ; 0xc2560
    pop di                                    ; 5f                          ; 0xc2561
    pop si                                    ; 5e                          ; 0xc2562
    retn 00004h                               ; c2 04 00                    ; 0xc2563
  ; disGetNextSymbol 0xc2566 LB 0x1ffe -> off=0x0 cb=00000000000000a1 uValue=00000000000c2566 'write_gfx_char_lin'
write_gfx_char_lin:                          ; 0xc2566 LB 0xa1
    push si                                   ; 56                          ; 0xc2566 vgabios.c:1598
    push di                                   ; 57                          ; 0xc2567
    push bp                                   ; 55                          ; 0xc2568
    mov bp, sp                                ; 89 e5                       ; 0xc2569
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc256b
    mov bh, al                                ; 88 c7                       ; 0xc256e
    mov ch, dl                                ; 88 d5                       ; 0xc2570
    mov al, cl                                ; 88 c8                       ; 0xc2572
    mov di, 0556fh                            ; bf 6f 55                    ; 0xc2574 vgabios.c:1605
    xor ah, ah                                ; 30 e4                       ; 0xc2577 vgabios.c:1606
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc2579
    xor dh, dh                                ; 30 f6                       ; 0xc257c
    imul dx                                   ; f7 ea                       ; 0xc257e
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc2580
    mov dx, ax                                ; 89 c2                       ; 0xc2582
    sal dx, CL                                ; d3 e2                       ; 0xc2584
    mov al, bl                                ; 88 d8                       ; 0xc2586
    xor ah, ah                                ; 30 e4                       ; 0xc2588
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc258a
    sal ax, CL                                ; d3 e0                       ; 0xc258c
    add ax, dx                                ; 01 d0                       ; 0xc258e
    mov word [bp-002h], ax                    ; 89 46 fe                    ; 0xc2590
    mov al, bh                                ; 88 f8                       ; 0xc2593 vgabios.c:1607
    xor ah, ah                                ; 30 e4                       ; 0xc2595
    sal ax, CL                                ; d3 e0                       ; 0xc2597
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc2599
    xor bl, bl                                ; 30 db                       ; 0xc259c vgabios.c:1608
    jmp short 025e2h                          ; eb 42                       ; 0xc259e
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc25a0 vgabios.c:1612
    jnc short 025dbh                          ; 73 37                       ; 0xc25a2
    xor bh, bh                                ; 30 ff                       ; 0xc25a4 vgabios.c:1614
    mov dl, bl                                ; 88 da                       ; 0xc25a6 vgabios.c:1615
    xor dh, dh                                ; 30 f6                       ; 0xc25a8
    add dx, word [bp-006h]                    ; 03 56 fa                    ; 0xc25aa
    mov si, di                                ; 89 fe                       ; 0xc25ad
    add si, dx                                ; 01 d6                       ; 0xc25af
    mov dl, byte [si]                         ; 8a 14                       ; 0xc25b1
    mov byte [bp-004h], dl                    ; 88 56 fc                    ; 0xc25b3
    mov byte [bp-003h], bh                    ; 88 7e fd                    ; 0xc25b6
    mov dl, ah                                ; 88 e2                       ; 0xc25b9
    xor dh, dh                                ; 30 f6                       ; 0xc25bb
    test word [bp-004h], dx                   ; 85 56 fc                    ; 0xc25bd
    je short 025c4h                           ; 74 02                       ; 0xc25c0
    mov bh, ch                                ; 88 ef                       ; 0xc25c2 vgabios.c:1617
    mov dl, al                                ; 88 c2                       ; 0xc25c4 vgabios.c:1619
    xor dh, dh                                ; 30 f6                       ; 0xc25c6
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc25c8
    add si, dx                                ; 01 d6                       ; 0xc25cb
    mov dx, 0a000h                            ; ba 00 a0                    ; 0xc25cd vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc25d0
    mov byte [es:si], bh                      ; 26 88 3c                    ; 0xc25d2
    shr ah, 1                                 ; d0 ec                       ; 0xc25d5 vgabios.c:1620
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc25d7 vgabios.c:1621
    jmp short 025a0h                          ; eb c5                       ; 0xc25d9
    db  0feh, 0c3h
    ; inc bl                                    ; fe c3                     ; 0xc25db vgabios.c:1622
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc25dd
    jnc short 025ffh                          ; 73 1d                       ; 0xc25e0
    mov al, bl                                ; 88 d8                       ; 0xc25e2
    xor ah, ah                                ; 30 e4                       ; 0xc25e4
    mov dl, byte [bp+008h]                    ; 8a 56 08                    ; 0xc25e6
    xor dh, dh                                ; 30 f6                       ; 0xc25e9
    imul dx                                   ; f7 ea                       ; 0xc25eb
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc25ed
    sal ax, CL                                ; d3 e0                       ; 0xc25ef
    mov dx, word [bp-002h]                    ; 8b 56 fe                    ; 0xc25f1
    add dx, ax                                ; 01 c2                       ; 0xc25f4
    mov word [bp-008h], dx                    ; 89 56 f8                    ; 0xc25f6
    mov AH, strict byte 080h                  ; b4 80                       ; 0xc25f9
    xor al, al                                ; 30 c0                       ; 0xc25fb
    jmp short 025a4h                          ; eb a5                       ; 0xc25fd
    mov sp, bp                                ; 89 ec                       ; 0xc25ff vgabios.c:1623
    pop bp                                    ; 5d                          ; 0xc2601
    pop di                                    ; 5f                          ; 0xc2602
    pop si                                    ; 5e                          ; 0xc2603
    retn 00002h                               ; c2 02 00                    ; 0xc2604
  ; disGetNextSymbol 0xc2607 LB 0x1f5d -> off=0x0 cb=0000000000000172 uValue=00000000000c2607 'biosfn_write_char_attr'
biosfn_write_char_attr:                      ; 0xc2607 LB 0x172
    push bp                                   ; 55                          ; 0xc2607 vgabios.c:1626
    mov bp, sp                                ; 89 e5                       ; 0xc2608
    push si                                   ; 56                          ; 0xc260a
    push di                                   ; 57                          ; 0xc260b
    sub sp, strict byte 0001ah                ; 83 ec 1a                    ; 0xc260c
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc260f
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc2612
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc2615
    mov si, cx                                ; 89 ce                       ; 0xc2618
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc261a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc261d
    mov es, ax                                ; 8e c0                       ; 0xc2620
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2622
    xor ah, ah                                ; 30 e4                       ; 0xc2625 vgabios.c:1634
    call 03888h                               ; e8 5e 12                    ; 0xc2627
    mov cl, al                                ; 88 c1                       ; 0xc262a
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc262c
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc262f vgabios.c:1635
    jne short 02636h                          ; 75 03                       ; 0xc2631
    jmp near 02772h                           ; e9 3c 01                    ; 0xc2633
    mov al, dl                                ; 88 d0                       ; 0xc2636 vgabios.c:1638
    xor ah, ah                                ; 30 e4                       ; 0xc2638
    lea bx, [bp-01ch]                         ; 8d 5e e4                    ; 0xc263a
    lea dx, [bp-01eh]                         ; 8d 56 e2                    ; 0xc263d
    call 00a97h                               ; e8 54 e4                    ; 0xc2640
    mov al, byte [bp-01ch]                    ; 8a 46 e4                    ; 0xc2643 vgabios.c:1639
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2646
    mov ax, word [bp-01ch]                    ; 8b 46 e4                    ; 0xc2649
    mov al, ah                                ; 88 e0                       ; 0xc264c
    xor ah, ah                                ; 30 e4                       ; 0xc264e
    mov word [bp-018h], ax                    ; 89 46 e8                    ; 0xc2650
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc2653
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2656
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2659 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc265c
    mov es, ax                                ; 8e c0                       ; 0xc265f
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2661
    xor ah, ah                                ; 30 e4                       ; 0xc2664 vgabios.c:48
    mov dx, ax                                ; 89 c2                       ; 0xc2666
    inc dx                                    ; 42                          ; 0xc2668
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2669 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc266c
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc266f
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc2672 vgabios.c:58
    mov bl, cl                                ; 88 cb                       ; 0xc2675 vgabios.c:1645
    xor bh, bh                                ; 30 ff                       ; 0xc2677
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2679
    mov di, bx                                ; 89 df                       ; 0xc267b
    sal di, CL                                ; d3 e7                       ; 0xc267d
    cmp byte [di+047b2h], 000h                ; 80 bd b2 47 00              ; 0xc267f
    jne short 026c6h                          ; 75 40                       ; 0xc2684
    mul dx                                    ; f7 e2                       ; 0xc2686 vgabios.c:1648
    sal ax, 1                                 ; d1 e0                       ; 0xc2688
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc268a
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc268c
    xor dh, dh                                ; 30 f6                       ; 0xc268f
    inc ax                                    ; 40                          ; 0xc2691
    mul dx                                    ; f7 e2                       ; 0xc2692
    mov bx, ax                                ; 89 c3                       ; 0xc2694
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc2696
    xor ah, ah                                ; 30 e4                       ; 0xc2699
    mul word [bp-016h]                        ; f7 66 ea                    ; 0xc269b
    mov dx, ax                                ; 89 c2                       ; 0xc269e
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc26a0
    xor ah, ah                                ; 30 e4                       ; 0xc26a3
    add ax, dx                                ; 01 d0                       ; 0xc26a5
    sal ax, 1                                 ; d1 e0                       ; 0xc26a7
    add bx, ax                                ; 01 c3                       ; 0xc26a9
    mov ah, byte [bp-008h]                    ; 8a 66 f8                    ; 0xc26ab vgabios.c:1650
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc26ae
    mov word [bp-01eh], ax                    ; 89 46 e2                    ; 0xc26b1
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc26b4 vgabios.c:1651
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc26b7
    mov cx, si                                ; 89 f1                       ; 0xc26bb
    mov di, bx                                ; 89 df                       ; 0xc26bd
    jcxz 026c3h                               ; e3 02                       ; 0xc26bf
    rep stosw                                 ; f3 ab                       ; 0xc26c1
    jmp near 02772h                           ; e9 ac 00                    ; 0xc26c3 vgabios.c:1653
    mov bl, byte [bx+04831h]                  ; 8a 9f 31 48                 ; 0xc26c6 vgabios.c:1656
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc26ca
    sal bx, CL                                ; d3 e3                       ; 0xc26cc
    mov al, byte [bx+04847h]                  ; 8a 87 47 48                 ; 0xc26ce
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc26d2
    mov al, byte [di+047b4h]                  ; 8a 85 b4 47                 ; 0xc26d5 vgabios.c:1657
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc26d9
    dec si                                    ; 4e                          ; 0xc26dc vgabios.c:1658
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc26dd
    je short 0272eh                           ; 74 4c                       ; 0xc26e0
    mov bl, byte [bp-014h]                    ; 8a 5e ec                    ; 0xc26e2 vgabios.c:1660
    xor bh, bh                                ; 30 ff                       ; 0xc26e5
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc26e7
    sal bx, CL                                ; d3 e3                       ; 0xc26e9
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc26eb
    cmp al, cl                                ; 38 c8                       ; 0xc26ef
    jc short 026ffh                           ; 72 0c                       ; 0xc26f1
    jbe short 02705h                          ; 76 10                       ; 0xc26f3
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc26f5
    je short 02751h                           ; 74 58                       ; 0xc26f7
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc26f9
    je short 02709h                           ; 74 0c                       ; 0xc26fb
    jmp short 0276ch                          ; eb 6d                       ; 0xc26fd
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc26ff
    je short 02730h                           ; 74 2d                       ; 0xc2701
    jmp short 0276ch                          ; eb 67                       ; 0xc2703
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc2705 vgabios.c:1663
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2709 vgabios.c:1665
    xor ah, ah                                ; 30 e4                       ; 0xc270c
    push ax                                   ; 50                          ; 0xc270e
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc270f
    push ax                                   ; 50                          ; 0xc2712
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc2713
    push ax                                   ; 50                          ; 0xc2716
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc2717
    xor ch, ch                                ; 30 ed                       ; 0xc271a
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc271c
    xor bh, bh                                ; 30 ff                       ; 0xc271f
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2721
    xor dh, dh                                ; 30 f6                       ; 0xc2724
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2726
    call 02342h                               ; e8 16 fc                    ; 0xc2729
    jmp short 0276ch                          ; eb 3e                       ; 0xc272c vgabios.c:1666
    jmp short 02772h                          ; eb 42                       ; 0xc272e
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc2730 vgabios.c:1668
    xor ah, ah                                ; 30 e4                       ; 0xc2733
    push ax                                   ; 50                          ; 0xc2735
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc2736
    push ax                                   ; 50                          ; 0xc2739
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc273a
    xor ch, ch                                ; 30 ed                       ; 0xc273d
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc273f
    xor bh, bh                                ; 30 ff                       ; 0xc2742
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2744
    xor dh, dh                                ; 30 f6                       ; 0xc2747
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2749
    call 02454h                               ; e8 05 fd                    ; 0xc274c
    jmp short 0276ch                          ; eb 1b                       ; 0xc274f vgabios.c:1669
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc2751 vgabios.c:1671
    xor ah, ah                                ; 30 e4                       ; 0xc2754
    push ax                                   ; 50                          ; 0xc2756
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc2757
    xor ch, ch                                ; 30 ed                       ; 0xc275a
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc275c
    xor bh, bh                                ; 30 ff                       ; 0xc275f
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc2761
    xor dh, dh                                ; 30 f6                       ; 0xc2764
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2766
    call 02566h                               ; e8 fa fd                    ; 0xc2769
    inc byte [bp-00ah]                        ; fe 46 f6                    ; 0xc276c vgabios.c:1678
    jmp near 026dch                           ; e9 6a ff                    ; 0xc276f vgabios.c:1679
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2772 vgabios.c:1681
    pop di                                    ; 5f                          ; 0xc2775
    pop si                                    ; 5e                          ; 0xc2776
    pop bp                                    ; 5d                          ; 0xc2777
    retn                                      ; c3                          ; 0xc2778
  ; disGetNextSymbol 0xc2779 LB 0x1deb -> off=0x0 cb=0000000000000183 uValue=00000000000c2779 'biosfn_write_char_only'
biosfn_write_char_only:                      ; 0xc2779 LB 0x183
    push bp                                   ; 55                          ; 0xc2779 vgabios.c:1684
    mov bp, sp                                ; 89 e5                       ; 0xc277a
    push si                                   ; 56                          ; 0xc277c
    push di                                   ; 57                          ; 0xc277d
    sub sp, strict byte 0001ah                ; 83 ec 1a                    ; 0xc277e
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2781
    mov byte [bp-00eh], dl                    ; 88 56 f2                    ; 0xc2784
    mov byte [bp-008h], bl                    ; 88 5e f8                    ; 0xc2787
    mov si, cx                                ; 89 ce                       ; 0xc278a
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc278c vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc278f
    mov es, ax                                ; 8e c0                       ; 0xc2792
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2794
    xor ah, ah                                ; 30 e4                       ; 0xc2797 vgabios.c:1692
    call 03888h                               ; e8 ec 10                    ; 0xc2799
    mov cl, al                                ; 88 c1                       ; 0xc279c
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc279e
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc27a1 vgabios.c:1693
    jne short 027a8h                          ; 75 03                       ; 0xc27a3
    jmp near 028f5h                           ; e9 4d 01                    ; 0xc27a5
    mov al, dl                                ; 88 d0                       ; 0xc27a8 vgabios.c:1696
    xor ah, ah                                ; 30 e4                       ; 0xc27aa
    lea bx, [bp-01eh]                         ; 8d 5e e2                    ; 0xc27ac
    lea dx, [bp-01ch]                         ; 8d 56 e4                    ; 0xc27af
    call 00a97h                               ; e8 e2 e2                    ; 0xc27b2
    mov al, byte [bp-01eh]                    ; 8a 46 e2                    ; 0xc27b5 vgabios.c:1697
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc27b8
    mov ax, word [bp-01eh]                    ; 8b 46 e2                    ; 0xc27bb
    mov al, ah                                ; 88 e0                       ; 0xc27be
    xor ah, ah                                ; 30 e4                       ; 0xc27c0
    mov word [bp-01ah], ax                    ; 89 46 e6                    ; 0xc27c2
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc27c5
    mov byte [bp-00ch], al                    ; 88 46 f4                    ; 0xc27c8
    mov bx, 00084h                            ; bb 84 00                    ; 0xc27cb vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc27ce
    mov es, ax                                ; 8e c0                       ; 0xc27d1
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc27d3
    xor ah, ah                                ; 30 e4                       ; 0xc27d6 vgabios.c:48
    mov dx, ax                                ; 89 c2                       ; 0xc27d8
    inc dx                                    ; 42                          ; 0xc27da
    mov di, strict word 0004ah                ; bf 4a 00                    ; 0xc27db vgabios.c:57
    mov di, word [es:di]                      ; 26 8b 3d                    ; 0xc27de
    mov word [bp-018h], di                    ; 89 7e e8                    ; 0xc27e1 vgabios.c:58
    mov al, cl                                ; 88 c8                       ; 0xc27e4 vgabios.c:1703
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc27e6
    mov bx, ax                                ; 89 c3                       ; 0xc27e8
    sal bx, CL                                ; d3 e3                       ; 0xc27ea
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc27ec
    jne short 02838h                          ; 75 45                       ; 0xc27f1
    mov ax, di                                ; 89 f8                       ; 0xc27f3 vgabios.c:1706
    mul dx                                    ; f7 e2                       ; 0xc27f5
    sal ax, 1                                 ; d1 e0                       ; 0xc27f7
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc27f9
    mov dl, byte [bp-00eh]                    ; 8a 56 f2                    ; 0xc27fb
    xor dh, dh                                ; 30 f6                       ; 0xc27fe
    inc ax                                    ; 40                          ; 0xc2800
    mul dx                                    ; f7 e2                       ; 0xc2801
    mov bx, ax                                ; 89 c3                       ; 0xc2803
    mov al, byte [bp-01ah]                    ; 8a 46 e6                    ; 0xc2805
    xor ah, ah                                ; 30 e4                       ; 0xc2808
    mul di                                    ; f7 e7                       ; 0xc280a
    mov dx, ax                                ; 89 c2                       ; 0xc280c
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc280e
    xor ah, ah                                ; 30 e4                       ; 0xc2811
    add ax, dx                                ; 01 d0                       ; 0xc2813
    sal ax, 1                                 ; d1 e0                       ; 0xc2815
    add bx, ax                                ; 01 c3                       ; 0xc2817
    dec si                                    ; 4e                          ; 0xc2819 vgabios.c:1708
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc281a
    je short 027a5h                           ; 74 86                       ; 0xc281d
    mov al, byte [bp-010h]                    ; 8a 46 f0                    ; 0xc281f vgabios.c:1709
    xor ah, ah                                ; 30 e4                       ; 0xc2822
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2824
    mov di, ax                                ; 89 c7                       ; 0xc2826
    sal di, CL                                ; d3 e7                       ; 0xc2828
    mov es, [di+047b5h]                       ; 8e 85 b5 47                 ; 0xc282a vgabios.c:50
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc282e vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2831
    inc bx                                    ; 43                          ; 0xc2834 vgabios.c:1710
    inc bx                                    ; 43                          ; 0xc2835
    jmp short 02819h                          ; eb e1                       ; 0xc2836 vgabios.c:1711
    mov di, ax                                ; 89 c7                       ; 0xc2838 vgabios.c:1716
    mov al, byte [di+04831h]                  ; 8a 85 31 48                 ; 0xc283a
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc283e
    mov di, ax                                ; 89 c7                       ; 0xc2840
    sal di, CL                                ; d3 e7                       ; 0xc2842
    mov al, byte [di+04847h]                  ; 8a 85 47 48                 ; 0xc2844
    mov byte [bp-012h], al                    ; 88 46 ee                    ; 0xc2848
    mov al, byte [bx+047b4h]                  ; 8a 87 b4 47                 ; 0xc284b vgabios.c:1717
    mov byte [bp-014h], al                    ; 88 46 ec                    ; 0xc284f
    dec si                                    ; 4e                          ; 0xc2852 vgabios.c:1718
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc2853
    je short 028a8h                           ; 74 50                       ; 0xc2856
    mov bl, byte [bp-010h]                    ; 8a 5e f0                    ; 0xc2858 vgabios.c:1720
    xor bh, bh                                ; 30 ff                       ; 0xc285b
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc285d
    sal bx, CL                                ; d3 e3                       ; 0xc285f
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc2861
    cmp bl, cl                                ; 38 cb                       ; 0xc2865
    jc short 02878h                           ; 72 0f                       ; 0xc2867
    jbe short 0287fh                          ; 76 14                       ; 0xc2869
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc286b
    je short 028d4h                           ; 74 64                       ; 0xc286e
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2870
    je short 02883h                           ; 74 0e                       ; 0xc2873
    jmp near 028efh                           ; e9 77 00                    ; 0xc2875
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2878
    je short 028aah                           ; 74 2d                       ; 0xc287b
    jmp short 028efh                          ; eb 70                       ; 0xc287d
    or byte [bp-008h], 001h                   ; 80 4e f8 01                 ; 0xc287f vgabios.c:1723
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2883 vgabios.c:1725
    xor ah, ah                                ; 30 e4                       ; 0xc2886
    push ax                                   ; 50                          ; 0xc2888
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc2889
    push ax                                   ; 50                          ; 0xc288c
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc288d
    push ax                                   ; 50                          ; 0xc2890
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc2891
    xor ch, ch                                ; 30 ed                       ; 0xc2894
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2896
    xor bh, bh                                ; 30 ff                       ; 0xc2899
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc289b
    xor dh, dh                                ; 30 f6                       ; 0xc289e
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc28a0
    call 02342h                               ; e8 9c fa                    ; 0xc28a3
    jmp short 028efh                          ; eb 47                       ; 0xc28a6 vgabios.c:1726
    jmp short 028f5h                          ; eb 4b                       ; 0xc28a8
    mov al, byte [bp-014h]                    ; 8a 46 ec                    ; 0xc28aa vgabios.c:1728
    xor ah, ah                                ; 30 e4                       ; 0xc28ad
    push ax                                   ; 50                          ; 0xc28af
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc28b0
    push ax                                   ; 50                          ; 0xc28b3
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc28b4
    xor ch, ch                                ; 30 ed                       ; 0xc28b7
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc28b9
    xor bh, bh                                ; 30 ff                       ; 0xc28bc
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc28be
    xor dh, dh                                ; 30 f6                       ; 0xc28c1
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc28c3
    mov byte [bp-016h], al                    ; 88 46 ea                    ; 0xc28c6
    mov byte [bp-015h], ah                    ; 88 66 eb                    ; 0xc28c9
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc28cc
    call 02454h                               ; e8 82 fb                    ; 0xc28cf
    jmp short 028efh                          ; eb 1b                       ; 0xc28d2 vgabios.c:1729
    mov al, byte [bp-018h]                    ; 8a 46 e8                    ; 0xc28d4 vgabios.c:1731
    xor ah, ah                                ; 30 e4                       ; 0xc28d7
    push ax                                   ; 50                          ; 0xc28d9
    mov cl, byte [bp-00ch]                    ; 8a 4e f4                    ; 0xc28da
    xor ch, ch                                ; 30 ed                       ; 0xc28dd
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc28df
    xor bh, bh                                ; 30 ff                       ; 0xc28e2
    mov dl, byte [bp-008h]                    ; 8a 56 f8                    ; 0xc28e4
    xor dh, dh                                ; 30 f6                       ; 0xc28e7
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc28e9
    call 02566h                               ; e8 77 fc                    ; 0xc28ec
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc28ef vgabios.c:1738
    jmp near 02852h                           ; e9 5d ff                    ; 0xc28f2 vgabios.c:1739
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc28f5 vgabios.c:1741
    pop di                                    ; 5f                          ; 0xc28f8
    pop si                                    ; 5e                          ; 0xc28f9
    pop bp                                    ; 5d                          ; 0xc28fa
    retn                                      ; c3                          ; 0xc28fb
  ; disGetNextSymbol 0xc28fc LB 0x1c68 -> off=0x0 cb=000000000000017a uValue=00000000000c28fc 'biosfn_write_pixel'
biosfn_write_pixel:                          ; 0xc28fc LB 0x17a
    push bp                                   ; 55                          ; 0xc28fc vgabios.c:1744
    mov bp, sp                                ; 89 e5                       ; 0xc28fd
    push si                                   ; 56                          ; 0xc28ff
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc2900
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2903
    mov byte [bp-004h], dl                    ; 88 56 fc                    ; 0xc2906
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc2909
    mov word [bp-00ah], cx                    ; 89 4e f6                    ; 0xc290c
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc290f vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2912
    mov es, ax                                ; 8e c0                       ; 0xc2915
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2917
    xor ah, ah                                ; 30 e4                       ; 0xc291a vgabios.c:1751
    call 03888h                               ; e8 69 0f                    ; 0xc291c
    mov ch, al                                ; 88 c5                       ; 0xc291f
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc2921 vgabios.c:1752
    je short 0294ch                           ; 74 27                       ; 0xc2923
    mov bl, al                                ; 88 c3                       ; 0xc2925 vgabios.c:1753
    xor bh, bh                                ; 30 ff                       ; 0xc2927
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2929
    sal bx, CL                                ; d3 e3                       ; 0xc292b
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc292d
    je short 0294ch                           ; 74 18                       ; 0xc2932
    mov al, byte [bx+047b3h]                  ; 8a 87 b3 47                 ; 0xc2934 vgabios.c:1755
    cmp al, cl                                ; 38 c8                       ; 0xc2938
    jc short 02948h                           ; 72 0c                       ; 0xc293a
    jbe short 02952h                          ; 76 14                       ; 0xc293c
    cmp AL, strict byte 005h                  ; 3c 05                       ; 0xc293e
    je short 0294fh                           ; 74 0d                       ; 0xc2940
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc2942
    je short 02952h                           ; 74 0c                       ; 0xc2944
    jmp short 0294ch                          ; eb 04                       ; 0xc2946
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc2948
    je short 029c4h                           ; 74 78                       ; 0xc294a
    jmp near 02a4fh                           ; e9 00 01                    ; 0xc294c
    jmp near 02a55h                           ; e9 03 01                    ; 0xc294f
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2952 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2955
    mov es, ax                                ; 8e c0                       ; 0xc2958
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc295a
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc295d vgabios.c:58
    mul dx                                    ; f7 e2                       ; 0xc2960
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2962
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2964
    shr bx, CL                                ; d3 eb                       ; 0xc2967
    add bx, ax                                ; 01 c3                       ; 0xc2969
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc296b vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc296e
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc2971 vgabios.c:58
    xor dh, dh                                ; 30 f6                       ; 0xc2974
    mul dx                                    ; f7 e2                       ; 0xc2976
    add bx, ax                                ; 01 c3                       ; 0xc2978
    mov cx, word [bp-008h]                    ; 8b 4e f8                    ; 0xc297a vgabios.c:1761
    and cl, 007h                              ; 80 e1 07                    ; 0xc297d
    mov ax, 00080h                            ; b8 80 00                    ; 0xc2980
    sar ax, CL                                ; d3 f8                       ; 0xc2983
    mov ah, al                                ; 88 c4                       ; 0xc2985 vgabios.c:1762
    xor al, al                                ; 30 c0                       ; 0xc2987
    or AL, strict byte 008h                   ; 0c 08                       ; 0xc2989
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc298b
    out DX, ax                                ; ef                          ; 0xc298e
    mov ax, 00205h                            ; b8 05 02                    ; 0xc298f vgabios.c:1763
    out DX, ax                                ; ef                          ; 0xc2992
    mov dx, bx                                ; 89 da                       ; 0xc2993 vgabios.c:1764
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2995
    call 038b3h                               ; e8 18 0f                    ; 0xc2998
    test byte [bp-004h], 080h                 ; f6 46 fc 80                 ; 0xc299b vgabios.c:1765
    je short 029a8h                           ; 74 07                       ; 0xc299f
    mov ax, 01803h                            ; b8 03 18                    ; 0xc29a1 vgabios.c:1767
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc29a4
    out DX, ax                                ; ef                          ; 0xc29a7
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc29a8 vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc29ab
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc29ad
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc29b0
    mov ax, 0ff08h                            ; b8 08 ff                    ; 0xc29b3 vgabios.c:1770
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc29b6
    out DX, ax                                ; ef                          ; 0xc29b9
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc29ba vgabios.c:1771
    out DX, ax                                ; ef                          ; 0xc29bd
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc29be vgabios.c:1772
    out DX, ax                                ; ef                          ; 0xc29c1
    jmp short 0294ch                          ; eb 88                       ; 0xc29c2 vgabios.c:1773
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc29c4 vgabios.c:1775
    shr ax, 1                                 ; d1 e8                       ; 0xc29c7
    mov dx, strict word 00050h                ; ba 50 00                    ; 0xc29c9
    mul dx                                    ; f7 e2                       ; 0xc29cc
    cmp byte [bx+047b4h], 002h                ; 80 bf b4 47 02              ; 0xc29ce
    jne short 029deh                          ; 75 09                       ; 0xc29d3
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc29d5 vgabios.c:1777
    shr bx, 1                                 ; d1 eb                       ; 0xc29d8
    shr bx, 1                                 ; d1 eb                       ; 0xc29da
    jmp short 029e3h                          ; eb 05                       ; 0xc29dc vgabios.c:1779
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc29de vgabios.c:1781
    shr bx, CL                                ; d3 eb                       ; 0xc29e1
    add bx, ax                                ; 01 c3                       ; 0xc29e3
    test byte [bp-00ah], 001h                 ; f6 46 f6 01                 ; 0xc29e5 vgabios.c:1783
    je short 029eeh                           ; 74 03                       ; 0xc29e9
    add bh, 020h                              ; 80 c7 20                    ; 0xc29eb
    mov ax, 0b800h                            ; b8 00 b8                    ; 0xc29ee vgabios.c:47
    mov es, ax                                ; 8e c0                       ; 0xc29f1
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc29f3
    mov dl, ch                                ; 88 ea                       ; 0xc29f6 vgabios.c:1785
    xor dh, dh                                ; 30 f6                       ; 0xc29f8
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc29fa
    mov si, dx                                ; 89 d6                       ; 0xc29fc
    sal si, CL                                ; d3 e6                       ; 0xc29fe
    cmp byte [si+047b4h], 002h                ; 80 bc b4 47 02              ; 0xc2a00
    jne short 02a21h                          ; 75 1a                       ; 0xc2a05
    mov ah, byte [bp-008h]                    ; 8a 66 f8                    ; 0xc2a07 vgabios.c:1787
    and ah, cl                                ; 20 cc                       ; 0xc2a0a
    mov dl, cl                                ; 88 ca                       ; 0xc2a0c
    sub dl, ah                                ; 28 e2                       ; 0xc2a0e
    mov ah, dl                                ; 88 d4                       ; 0xc2a10
    sal ah, 1                                 ; d0 e4                       ; 0xc2a12
    mov dl, byte [bp-004h]                    ; 8a 56 fc                    ; 0xc2a14
    and dl, cl                                ; 20 ca                       ; 0xc2a17
    mov cl, ah                                ; 88 e1                       ; 0xc2a19
    sal dl, CL                                ; d2 e2                       ; 0xc2a1b
    mov AH, strict byte 003h                  ; b4 03                       ; 0xc2a1d vgabios.c:1788
    jmp short 02a35h                          ; eb 14                       ; 0xc2a1f vgabios.c:1790
    mov ah, byte [bp-008h]                    ; 8a 66 f8                    ; 0xc2a21 vgabios.c:1792
    and ah, 007h                              ; 80 e4 07                    ; 0xc2a24
    mov CL, strict byte 007h                  ; b1 07                       ; 0xc2a27
    sub cl, ah                                ; 28 e1                       ; 0xc2a29
    mov dl, byte [bp-004h]                    ; 8a 56 fc                    ; 0xc2a2b
    and dl, 001h                              ; 80 e2 01                    ; 0xc2a2e
    sal dl, CL                                ; d2 e2                       ; 0xc2a31
    mov AH, strict byte 001h                  ; b4 01                       ; 0xc2a33 vgabios.c:1793
    sal ah, CL                                ; d2 e4                       ; 0xc2a35
    test byte [bp-004h], 080h                 ; f6 46 fc 80                 ; 0xc2a37 vgabios.c:1795
    je short 02a41h                           ; 74 04                       ; 0xc2a3b
    xor al, dl                                ; 30 d0                       ; 0xc2a3d vgabios.c:1797
    jmp short 02a47h                          ; eb 06                       ; 0xc2a3f vgabios.c:1799
    not ah                                    ; f6 d4                       ; 0xc2a41 vgabios.c:1801
    and al, ah                                ; 20 e0                       ; 0xc2a43
    or al, dl                                 ; 08 d0                       ; 0xc2a45 vgabios.c:1802
    mov dx, 0b800h                            ; ba 00 b8                    ; 0xc2a47 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc2a4a
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc2a4c
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2a4f vgabios.c:1805
    pop si                                    ; 5e                          ; 0xc2a52
    pop bp                                    ; 5d                          ; 0xc2a53
    retn                                      ; c3                          ; 0xc2a54
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2a55 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2a58
    mov es, ax                                ; 8e c0                       ; 0xc2a5b
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc2a5d
    sal dx, CL                                ; d3 e2                       ; 0xc2a60 vgabios.c:58
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc2a62
    mul dx                                    ; f7 e2                       ; 0xc2a65
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc2a67
    add bx, ax                                ; 01 c3                       ; 0xc2a6a
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2a6c vgabios.c:52
    mov es, ax                                ; 8e c0                       ; 0xc2a6f
    mov al, byte [bp-004h]                    ; 8a 46 fc                    ; 0xc2a71
    jmp short 02a4ch                          ; eb d6                       ; 0xc2a74
  ; disGetNextSymbol 0xc2a76 LB 0x1aee -> off=0x0 cb=0000000000000263 uValue=00000000000c2a76 'biosfn_write_teletype'
biosfn_write_teletype:                       ; 0xc2a76 LB 0x263
    push bp                                   ; 55                          ; 0xc2a76 vgabios.c:1818
    mov bp, sp                                ; 89 e5                       ; 0xc2a77
    push si                                   ; 56                          ; 0xc2a79
    sub sp, strict byte 00016h                ; 83 ec 16                    ; 0xc2a7a
    mov ch, al                                ; 88 c5                       ; 0xc2a7d
    mov byte [bp-00ah], dl                    ; 88 56 f6                    ; 0xc2a7f
    mov byte [bp-00ch], bl                    ; 88 5e f4                    ; 0xc2a82
    mov byte [bp-004h], cl                    ; 88 4e fc                    ; 0xc2a85
    cmp dl, 0ffh                              ; 80 fa ff                    ; 0xc2a88 vgabios.c:1826
    jne short 02a9bh                          ; 75 0e                       ; 0xc2a8b
    mov bx, strict word 00062h                ; bb 62 00                    ; 0xc2a8d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2a90
    mov es, ax                                ; 8e c0                       ; 0xc2a93
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2a95
    mov byte [bp-00ah], al                    ; 88 46 f6                    ; 0xc2a98 vgabios.c:48
    mov bx, strict word 00049h                ; bb 49 00                    ; 0xc2a9b vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2a9e
    mov es, ax                                ; 8e c0                       ; 0xc2aa1
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2aa3
    xor ah, ah                                ; 30 e4                       ; 0xc2aa6 vgabios.c:1831
    call 03888h                               ; e8 dd 0d                    ; 0xc2aa8
    mov byte [bp-00eh], al                    ; 88 46 f2                    ; 0xc2aab
    cmp AL, strict byte 0ffh                  ; 3c ff                       ; 0xc2aae vgabios.c:1832
    je short 02b17h                           ; 74 65                       ; 0xc2ab0
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2ab2 vgabios.c:1835
    xor ah, ah                                ; 30 e4                       ; 0xc2ab5
    lea bx, [bp-016h]                         ; 8d 5e ea                    ; 0xc2ab7
    lea dx, [bp-018h]                         ; 8d 56 e8                    ; 0xc2aba
    call 00a97h                               ; e8 d7 df                    ; 0xc2abd
    mov al, byte [bp-016h]                    ; 8a 46 ea                    ; 0xc2ac0 vgabios.c:1836
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2ac3
    mov ax, word [bp-016h]                    ; 8b 46 ea                    ; 0xc2ac6
    mov al, ah                                ; 88 e0                       ; 0xc2ac9
    xor ah, ah                                ; 30 e4                       ; 0xc2acb
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2acd
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2ad0 vgabios.c:47
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc2ad3
    mov es, dx                                ; 8e c2                       ; 0xc2ad6
    mov dl, byte [es:bx]                      ; 26 8a 17                    ; 0xc2ad8
    xor dh, dh                                ; 30 f6                       ; 0xc2adb vgabios.c:48
    inc dx                                    ; 42                          ; 0xc2add
    mov word [bp-014h], dx                    ; 89 56 ec                    ; 0xc2ade
    mov bx, strict word 0004ah                ; bb 4a 00                    ; 0xc2ae1 vgabios.c:57
    mov dx, word [es:bx]                      ; 26 8b 17                    ; 0xc2ae4
    mov word [bp-012h], dx                    ; 89 56 ee                    ; 0xc2ae7 vgabios.c:58
    cmp ch, 008h                              ; 80 fd 08                    ; 0xc2aea vgabios.c:1842
    jc short 02afdh                           ; 72 0e                       ; 0xc2aed
    jbe short 02b05h                          ; 76 14                       ; 0xc2aef
    cmp ch, 00dh                              ; 80 fd 0d                    ; 0xc2af1
    je short 02b1ah                           ; 74 24                       ; 0xc2af4
    cmp ch, 00ah                              ; 80 fd 0a                    ; 0xc2af6
    je short 02b10h                           ; 74 15                       ; 0xc2af9
    jmp short 02b20h                          ; eb 23                       ; 0xc2afb
    cmp ch, 007h                              ; 80 fd 07                    ; 0xc2afd
    jne short 02b20h                          ; 75 1e                       ; 0xc2b00
    jmp near 02c28h                           ; e9 23 01                    ; 0xc2b02
    cmp byte [bp-006h], 000h                  ; 80 7e fa 00                 ; 0xc2b05 vgabios.c:1849
    jbe short 02b1dh                          ; 76 12                       ; 0xc2b09
    dec byte [bp-006h]                        ; fe 4e fa                    ; 0xc2b0b
    jmp short 02b1dh                          ; eb 0d                       ; 0xc2b0e vgabios.c:1850
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc2b10 vgabios.c:1853
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc2b12
    jmp short 02b1dh                          ; eb 06                       ; 0xc2b15 vgabios.c:1854
    jmp near 02cd3h                           ; e9 b9 01                    ; 0xc2b17
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc2b1a vgabios.c:1857
    jmp near 02c28h                           ; e9 08 01                    ; 0xc2b1d vgabios.c:1858
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc2b20 vgabios.c:1862
    xor ah, ah                                ; 30 e4                       ; 0xc2b23
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2b25
    mov bx, ax                                ; 89 c3                       ; 0xc2b27
    sal bx, CL                                ; d3 e3                       ; 0xc2b29
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc2b2b
    jne short 02b74h                          ; 75 42                       ; 0xc2b30
    mov ax, word [bp-012h]                    ; 8b 46 ee                    ; 0xc2b32 vgabios.c:1865
    mul word [bp-014h]                        ; f7 66 ec                    ; 0xc2b35
    sal ax, 1                                 ; d1 e0                       ; 0xc2b38
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2b3a
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2b3c
    xor dh, dh                                ; 30 f6                       ; 0xc2b3f
    inc ax                                    ; 40                          ; 0xc2b41
    mul dx                                    ; f7 e2                       ; 0xc2b42
    mov si, ax                                ; 89 c6                       ; 0xc2b44
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2b46
    xor ah, ah                                ; 30 e4                       ; 0xc2b49
    mul word [bp-012h]                        ; f7 66 ee                    ; 0xc2b4b
    mov dx, ax                                ; 89 c2                       ; 0xc2b4e
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2b50
    xor ah, ah                                ; 30 e4                       ; 0xc2b53
    add ax, dx                                ; 01 d0                       ; 0xc2b55
    sal ax, 1                                 ; d1 e0                       ; 0xc2b57
    add si, ax                                ; 01 c6                       ; 0xc2b59
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2b5b vgabios.c:50
    mov byte [es:si], ch                      ; 26 88 2c                    ; 0xc2b5f vgabios.c:52
    cmp cl, byte [bp-004h]                    ; 3a 4e fc                    ; 0xc2b62 vgabios.c:1870
    jne short 02ba4h                          ; 75 3d                       ; 0xc2b65
    inc si                                    ; 46                          ; 0xc2b67 vgabios.c:1871
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2b68 vgabios.c:50
    mov al, byte [bp-00ch]                    ; 8a 46 f4                    ; 0xc2b6c
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc2b6f
    jmp short 02ba4h                          ; eb 30                       ; 0xc2b72 vgabios.c:1873
    mov si, ax                                ; 89 c6                       ; 0xc2b74 vgabios.c:1876
    mov al, byte [si+04831h]                  ; 8a 84 31 48                 ; 0xc2b76
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc2b7a
    mov si, ax                                ; 89 c6                       ; 0xc2b7c
    sal si, CL                                ; d3 e6                       ; 0xc2b7e
    mov dl, byte [si+04847h]                  ; 8a 94 47 48                 ; 0xc2b80
    mov al, byte [bx+047b4h]                  ; 8a 87 b4 47                 ; 0xc2b84 vgabios.c:1877
    mov bl, byte [bx+047b3h]                  ; 8a 9f b3 47                 ; 0xc2b88 vgabios.c:1878
    cmp bl, 003h                              ; 80 fb 03                    ; 0xc2b8c
    jc short 02b9fh                           ; 72 0e                       ; 0xc2b8f
    jbe short 02ba6h                          ; 76 13                       ; 0xc2b91
    cmp bl, 005h                              ; 80 fb 05                    ; 0xc2b93
    je short 02bf6h                           ; 74 5e                       ; 0xc2b96
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc2b98
    je short 02baah                           ; 74 0d                       ; 0xc2b9b
    jmp short 02c15h                          ; eb 76                       ; 0xc2b9d
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc2b9f
    je short 02bd4h                           ; 74 30                       ; 0xc2ba2
    jmp short 02c15h                          ; eb 6f                       ; 0xc2ba4
    or byte [bp-00ch], 001h                   ; 80 4e f4 01                 ; 0xc2ba6 vgabios.c:1881
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2baa vgabios.c:1883
    xor ah, ah                                ; 30 e4                       ; 0xc2bad
    push ax                                   ; 50                          ; 0xc2baf
    mov al, dl                                ; 88 d0                       ; 0xc2bb0
    push ax                                   ; 50                          ; 0xc2bb2
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc2bb3
    push ax                                   ; 50                          ; 0xc2bb6
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2bb7
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2bba
    xor bh, bh                                ; 30 ff                       ; 0xc2bbd
    mov dl, byte [bp-00ch]                    ; 8a 56 f4                    ; 0xc2bbf
    xor dh, dh                                ; 30 f6                       ; 0xc2bc2
    mov byte [bp-010h], ch                    ; 88 6e f0                    ; 0xc2bc4
    mov byte [bp-00fh], ah                    ; 88 66 f1                    ; 0xc2bc7
    mov cx, ax                                ; 89 c1                       ; 0xc2bca
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc2bcc
    call 02342h                               ; e8 70 f7                    ; 0xc2bcf
    jmp short 02c15h                          ; eb 41                       ; 0xc2bd2 vgabios.c:1884
    push ax                                   ; 50                          ; 0xc2bd4 vgabios.c:1886
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc2bd5
    push ax                                   ; 50                          ; 0xc2bd8
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2bd9
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2bdc
    xor bh, bh                                ; 30 ff                       ; 0xc2bdf
    mov dl, byte [bp-00ch]                    ; 8a 56 f4                    ; 0xc2be1
    xor dh, dh                                ; 30 f6                       ; 0xc2be4
    mov byte [bp-010h], ch                    ; 88 6e f0                    ; 0xc2be6
    mov byte [bp-00fh], ah                    ; 88 66 f1                    ; 0xc2be9
    mov cx, ax                                ; 89 c1                       ; 0xc2bec
    mov ax, word [bp-010h]                    ; 8b 46 f0                    ; 0xc2bee
    call 02454h                               ; e8 60 f8                    ; 0xc2bf1
    jmp short 02c15h                          ; eb 1f                       ; 0xc2bf4 vgabios.c:1887
    mov al, byte [bp-012h]                    ; 8a 46 ee                    ; 0xc2bf6 vgabios.c:1889
    push ax                                   ; 50                          ; 0xc2bf9
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2bfa
    mov byte [bp-010h], al                    ; 88 46 f0                    ; 0xc2bfd
    mov byte [bp-00fh], ah                    ; 88 66 f1                    ; 0xc2c00
    mov bl, byte [bp-006h]                    ; 8a 5e fa                    ; 0xc2c03
    xor bh, bh                                ; 30 ff                       ; 0xc2c06
    mov dl, byte [bp-00ch]                    ; 8a 56 f4                    ; 0xc2c08
    xor dh, dh                                ; 30 f6                       ; 0xc2c0b
    mov al, ch                                ; 88 e8                       ; 0xc2c0d
    mov cx, word [bp-010h]                    ; 8b 4e f0                    ; 0xc2c0f
    call 02566h                               ; e8 51 f9                    ; 0xc2c12
    inc byte [bp-006h]                        ; fe 46 fa                    ; 0xc2c15 vgabios.c:1897
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2c18 vgabios.c:1899
    xor ah, ah                                ; 30 e4                       ; 0xc2c1b
    cmp ax, word [bp-012h]                    ; 3b 46 ee                    ; 0xc2c1d
    jne short 02c28h                          ; 75 06                       ; 0xc2c20
    mov byte [bp-006h], ah                    ; 88 66 fa                    ; 0xc2c22 vgabios.c:1900
    inc byte [bp-008h]                        ; fe 46 f8                    ; 0xc2c25 vgabios.c:1901
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c28 vgabios.c:1906
    xor ah, ah                                ; 30 e4                       ; 0xc2c2b
    cmp ax, word [bp-014h]                    ; 3b 46 ec                    ; 0xc2c2d
    jne short 02c96h                          ; 75 64                       ; 0xc2c30
    mov bl, byte [bp-00eh]                    ; 8a 5e f2                    ; 0xc2c32 vgabios.c:1908
    xor bh, bh                                ; 30 ff                       ; 0xc2c35
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2c37
    sal bx, CL                                ; d3 e3                       ; 0xc2c39
    mov cl, byte [bp-014h]                    ; 8a 4e ec                    ; 0xc2c3b
    db  0feh, 0c9h
    ; dec cl                                    ; fe c9                     ; 0xc2c3e
    mov ch, byte [bp-012h]                    ; 8a 6e ee                    ; 0xc2c40
    db  0feh, 0cdh
    ; dec ch                                    ; fe cd                     ; 0xc2c43
    cmp byte [bx+047b2h], 000h                ; 80 bf b2 47 00              ; 0xc2c45
    jne short 02c98h                          ; 75 4c                       ; 0xc2c4a
    mov ax, word [bp-012h]                    ; 8b 46 ee                    ; 0xc2c4c vgabios.c:1910
    mul word [bp-014h]                        ; f7 66 ec                    ; 0xc2c4f
    sal ax, 1                                 ; d1 e0                       ; 0xc2c52
    or AL, strict byte 0ffh                   ; 0c ff                       ; 0xc2c54
    mov dl, byte [bp-00ah]                    ; 8a 56 f6                    ; 0xc2c56
    xor dh, dh                                ; 30 f6                       ; 0xc2c59
    inc ax                                    ; 40                          ; 0xc2c5b
    mul dx                                    ; f7 e2                       ; 0xc2c5c
    mov si, ax                                ; 89 c6                       ; 0xc2c5e
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2c60
    xor ah, ah                                ; 30 e4                       ; 0xc2c63
    dec ax                                    ; 48                          ; 0xc2c65
    mul word [bp-012h]                        ; f7 66 ee                    ; 0xc2c66
    mov dx, ax                                ; 89 c2                       ; 0xc2c69
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2c6b
    xor ah, ah                                ; 30 e4                       ; 0xc2c6e
    add ax, dx                                ; 01 d0                       ; 0xc2c70
    sal ax, 1                                 ; d1 e0                       ; 0xc2c72
    add si, ax                                ; 01 c6                       ; 0xc2c74
    inc si                                    ; 46                          ; 0xc2c76 vgabios.c:1911
    mov es, [bx+047b5h]                       ; 8e 87 b5 47                 ; 0xc2c77 vgabios.c:45
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc2c7b vgabios.c:47
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc2c7e vgabios.c:1912
    push ax                                   ; 50                          ; 0xc2c81
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2c82
    xor ah, ah                                ; 30 e4                       ; 0xc2c85
    push ax                                   ; 50                          ; 0xc2c87
    mov al, ch                                ; 88 e8                       ; 0xc2c88
    push ax                                   ; 50                          ; 0xc2c8a
    mov al, cl                                ; 88 c8                       ; 0xc2c8b
    push ax                                   ; 50                          ; 0xc2c8d
    xor dh, dh                                ; 30 f6                       ; 0xc2c8e
    xor cx, cx                                ; 31 c9                       ; 0xc2c90
    xor bx, bx                                ; 31 db                       ; 0xc2c92
    jmp short 02caeh                          ; eb 18                       ; 0xc2c94 vgabios.c:1914
    jmp short 02cb7h                          ; eb 1f                       ; 0xc2c96
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc2c98 vgabios.c:1916
    push ax                                   ; 50                          ; 0xc2c9b
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2c9c
    xor ah, ah                                ; 30 e4                       ; 0xc2c9f
    push ax                                   ; 50                          ; 0xc2ca1
    mov al, ch                                ; 88 e8                       ; 0xc2ca2
    push ax                                   ; 50                          ; 0xc2ca4
    mov al, cl                                ; 88 c8                       ; 0xc2ca5
    push ax                                   ; 50                          ; 0xc2ca7
    xor cx, cx                                ; 31 c9                       ; 0xc2ca8
    xor bx, bx                                ; 31 db                       ; 0xc2caa
    xor dx, dx                                ; 31 d2                       ; 0xc2cac
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc2cae
    call 01c9fh                               ; e8 eb ef                    ; 0xc2cb1
    dec byte [bp-008h]                        ; fe 4e f8                    ; 0xc2cb4 vgabios.c:1918
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc2cb7 vgabios.c:1922
    xor ah, ah                                ; 30 e4                       ; 0xc2cba
    mov word [bp-016h], ax                    ; 89 46 ea                    ; 0xc2cbc
    mov CL, strict byte 008h                  ; b1 08                       ; 0xc2cbf
    sal word [bp-016h], CL                    ; d3 66 ea                    ; 0xc2cc1
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2cc4
    add word [bp-016h], ax                    ; 01 46 ea                    ; 0xc2cc7
    mov dx, word [bp-016h]                    ; 8b 56 ea                    ; 0xc2cca vgabios.c:1923
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc2ccd
    call 012cfh                               ; e8 fc e5                    ; 0xc2cd0
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2cd3 vgabios.c:1924
    pop si                                    ; 5e                          ; 0xc2cd6
    pop bp                                    ; 5d                          ; 0xc2cd7
    retn                                      ; c3                          ; 0xc2cd8
  ; disGetNextSymbol 0xc2cd9 LB 0x188b -> off=0x0 cb=0000000000000035 uValue=00000000000c2cd9 'get_font_access'
get_font_access:                             ; 0xc2cd9 LB 0x35
    push bp                                   ; 55                          ; 0xc2cd9 vgabios.c:1927
    mov bp, sp                                ; 89 e5                       ; 0xc2cda
    push dx                                   ; 52                          ; 0xc2cdc
    mov ax, strict word 00005h                ; b8 05 00                    ; 0xc2cdd vgabios.c:1929
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2ce0
    out DX, ax                                ; ef                          ; 0xc2ce3
    mov AL, strict byte 006h                  ; b0 06                       ; 0xc2ce4 vgabios.c:1930
    out DX, AL                                ; ee                          ; 0xc2ce6
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc2ce7 vgabios.c:1931
    in AL, DX                                 ; ec                          ; 0xc2cea
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2ceb
    mov ah, al                                ; 88 c4                       ; 0xc2ced
    and ah, 001h                              ; 80 e4 01                    ; 0xc2cef
    or ah, 004h                               ; 80 cc 04                    ; 0xc2cf2
    xor al, al                                ; 30 c0                       ; 0xc2cf5
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2cf7
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2cf9
    out DX, ax                                ; ef                          ; 0xc2cfc
    mov ax, 00402h                            ; b8 02 04                    ; 0xc2cfd vgabios.c:1932
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2d00
    out DX, ax                                ; ef                          ; 0xc2d03
    mov ax, 00604h                            ; b8 04 06                    ; 0xc2d04 vgabios.c:1933
    out DX, ax                                ; ef                          ; 0xc2d07
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2d08 vgabios.c:1934
    pop dx                                    ; 5a                          ; 0xc2d0b
    pop bp                                    ; 5d                          ; 0xc2d0c
    retn                                      ; c3                          ; 0xc2d0d
  ; disGetNextSymbol 0xc2d0e LB 0x1856 -> off=0x0 cb=0000000000000033 uValue=00000000000c2d0e 'release_font_access'
release_font_access:                         ; 0xc2d0e LB 0x33
    push bp                                   ; 55                          ; 0xc2d0e vgabios.c:1936
    mov bp, sp                                ; 89 e5                       ; 0xc2d0f
    push dx                                   ; 52                          ; 0xc2d11
    mov dx, 003cch                            ; ba cc 03                    ; 0xc2d12 vgabios.c:1938
    in AL, DX                                 ; ec                          ; 0xc2d15
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2d16
    and ax, strict word 00001h                ; 25 01 00                    ; 0xc2d18
    sal ax, 1                                 ; d1 e0                       ; 0xc2d1b
    sal ax, 1                                 ; d1 e0                       ; 0xc2d1d
    mov ah, al                                ; 88 c4                       ; 0xc2d1f
    or ah, 00ah                               ; 80 cc 0a                    ; 0xc2d21
    xor al, al                                ; 30 c0                       ; 0xc2d24
    or AL, strict byte 006h                   ; 0c 06                       ; 0xc2d26
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc2d28
    out DX, ax                                ; ef                          ; 0xc2d2b
    mov ax, 01005h                            ; b8 05 10                    ; 0xc2d2c vgabios.c:1939
    out DX, ax                                ; ef                          ; 0xc2d2f
    mov ax, 00302h                            ; b8 02 03                    ; 0xc2d30 vgabios.c:1940
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2d33
    out DX, ax                                ; ef                          ; 0xc2d36
    mov ax, 00204h                            ; b8 04 02                    ; 0xc2d37 vgabios.c:1941
    out DX, ax                                ; ef                          ; 0xc2d3a
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2d3b vgabios.c:1942
    pop dx                                    ; 5a                          ; 0xc2d3e
    pop bp                                    ; 5d                          ; 0xc2d3f
    retn                                      ; c3                          ; 0xc2d40
  ; disGetNextSymbol 0xc2d41 LB 0x1823 -> off=0x0 cb=00000000000000b3 uValue=00000000000c2d41 'set_scan_lines'
set_scan_lines:                              ; 0xc2d41 LB 0xb3
    push bp                                   ; 55                          ; 0xc2d41 vgabios.c:1944
    mov bp, sp                                ; 89 e5                       ; 0xc2d42
    push bx                                   ; 53                          ; 0xc2d44
    push cx                                   ; 51                          ; 0xc2d45
    push dx                                   ; 52                          ; 0xc2d46
    push si                                   ; 56                          ; 0xc2d47
    push di                                   ; 57                          ; 0xc2d48
    mov bl, al                                ; 88 c3                       ; 0xc2d49
    mov si, strict word 00063h                ; be 63 00                    ; 0xc2d4b vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2d4e
    mov es, ax                                ; 8e c0                       ; 0xc2d51
    mov si, word [es:si]                      ; 26 8b 34                    ; 0xc2d53
    mov cx, si                                ; 89 f1                       ; 0xc2d56 vgabios.c:58
    mov AL, strict byte 009h                  ; b0 09                       ; 0xc2d58 vgabios.c:1950
    mov dx, si                                ; 89 f2                       ; 0xc2d5a
    out DX, AL                                ; ee                          ; 0xc2d5c
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc2d5d vgabios.c:1951
    in AL, DX                                 ; ec                          ; 0xc2d60
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2d61
    mov ah, al                                ; 88 c4                       ; 0xc2d63 vgabios.c:1952
    and ah, 0e0h                              ; 80 e4 e0                    ; 0xc2d65
    mov al, bl                                ; 88 d8                       ; 0xc2d68
    db  0feh, 0c8h
    ; dec al                                    ; fe c8                     ; 0xc2d6a
    or al, ah                                 ; 08 e0                       ; 0xc2d6c
    out DX, AL                                ; ee                          ; 0xc2d6e vgabios.c:1953
    cmp bl, 008h                              ; 80 fb 08                    ; 0xc2d6f vgabios.c:1954
    jne short 02d7ch                          ; 75 08                       ; 0xc2d72
    mov dx, strict word 00007h                ; ba 07 00                    ; 0xc2d74 vgabios.c:1956
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc2d77
    jmp short 02d89h                          ; eb 0d                       ; 0xc2d7a vgabios.c:1958
    mov dl, bl                                ; 88 da                       ; 0xc2d7c vgabios.c:1960
    sub dl, 003h                              ; 80 ea 03                    ; 0xc2d7e
    xor dh, dh                                ; 30 f6                       ; 0xc2d81
    mov al, bl                                ; 88 d8                       ; 0xc2d83
    sub AL, strict byte 004h                  ; 2c 04                       ; 0xc2d85
    xor ah, ah                                ; 30 e4                       ; 0xc2d87
    call 011d3h                               ; e8 47 e4                    ; 0xc2d89
    xor bh, bh                                ; 30 ff                       ; 0xc2d8c vgabios.c:1962
    mov si, 00085h                            ; be 85 00                    ; 0xc2d8e vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2d91
    mov es, ax                                ; 8e c0                       ; 0xc2d94
    mov word [es:si], bx                      ; 26 89 1c                    ; 0xc2d96
    mov AL, strict byte 012h                  ; b0 12                       ; 0xc2d99 vgabios.c:1963
    mov dx, cx                                ; 89 ca                       ; 0xc2d9b
    out DX, AL                                ; ee                          ; 0xc2d9d
    mov si, cx                                ; 89 ce                       ; 0xc2d9e vgabios.c:1964
    inc si                                    ; 46                          ; 0xc2da0
    mov dx, si                                ; 89 f2                       ; 0xc2da1
    in AL, DX                                 ; ec                          ; 0xc2da3
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2da4
    mov di, ax                                ; 89 c7                       ; 0xc2da6
    mov AL, strict byte 007h                  ; b0 07                       ; 0xc2da8 vgabios.c:1965
    mov dx, cx                                ; 89 ca                       ; 0xc2daa
    out DX, AL                                ; ee                          ; 0xc2dac
    mov dx, si                                ; 89 f2                       ; 0xc2dad vgabios.c:1966
    in AL, DX                                 ; ec                          ; 0xc2daf
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc2db0
    mov dl, al                                ; 88 c2                       ; 0xc2db2 vgabios.c:1967
    and dl, 002h                              ; 80 e2 02                    ; 0xc2db4
    xor dh, dh                                ; 30 f6                       ; 0xc2db7
    mov CL, strict byte 007h                  ; b1 07                       ; 0xc2db9
    sal dx, CL                                ; d3 e2                       ; 0xc2dbb
    and AL, strict byte 040h                  ; 24 40                       ; 0xc2dbd
    xor ah, ah                                ; 30 e4                       ; 0xc2dbf
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc2dc1
    sal ax, CL                                ; d3 e0                       ; 0xc2dc3
    add ax, dx                                ; 01 d0                       ; 0xc2dc5
    inc ax                                    ; 40                          ; 0xc2dc7
    add ax, di                                ; 01 f8                       ; 0xc2dc8
    xor dx, dx                                ; 31 d2                       ; 0xc2dca vgabios.c:1968
    div bx                                    ; f7 f3                       ; 0xc2dcc
    mov dl, al                                ; 88 c2                       ; 0xc2dce vgabios.c:1969
    db  0feh, 0cah
    ; dec dl                                    ; fe ca                     ; 0xc2dd0
    mov si, 00084h                            ; be 84 00                    ; 0xc2dd2 vgabios.c:52
    mov byte [es:si], dl                      ; 26 88 14                    ; 0xc2dd5
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc2dd8 vgabios.c:57
    mov dx, word [es:si]                      ; 26 8b 14                    ; 0xc2ddb
    xor ah, ah                                ; 30 e4                       ; 0xc2dde vgabios.c:1971
    mul dx                                    ; f7 e2                       ; 0xc2de0
    sal ax, 1                                 ; d1 e0                       ; 0xc2de2
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc2de4 vgabios.c:62
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc2de7
    lea sp, [bp-00ah]                         ; 8d 66 f6                    ; 0xc2dea vgabios.c:1972
    pop di                                    ; 5f                          ; 0xc2ded
    pop si                                    ; 5e                          ; 0xc2dee
    pop dx                                    ; 5a                          ; 0xc2def
    pop cx                                    ; 59                          ; 0xc2df0
    pop bx                                    ; 5b                          ; 0xc2df1
    pop bp                                    ; 5d                          ; 0xc2df2
    retn                                      ; c3                          ; 0xc2df3
  ; disGetNextSymbol 0xc2df4 LB 0x1770 -> off=0x0 cb=0000000000000020 uValue=00000000000c2df4 'biosfn_set_font_block'
biosfn_set_font_block:                       ; 0xc2df4 LB 0x20
    push bp                                   ; 55                          ; 0xc2df4 vgabios.c:1974
    mov bp, sp                                ; 89 e5                       ; 0xc2df5
    push bx                                   ; 53                          ; 0xc2df7
    push dx                                   ; 52                          ; 0xc2df8
    mov bl, al                                ; 88 c3                       ; 0xc2df9
    mov ax, 00100h                            ; b8 00 01                    ; 0xc2dfb vgabios.c:1976
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc2dfe
    out DX, ax                                ; ef                          ; 0xc2e01
    mov ah, bl                                ; 88 dc                       ; 0xc2e02 vgabios.c:1977
    xor al, al                                ; 30 c0                       ; 0xc2e04
    or AL, strict byte 003h                   ; 0c 03                       ; 0xc2e06
    out DX, ax                                ; ef                          ; 0xc2e08
    mov ax, 00300h                            ; b8 00 03                    ; 0xc2e09 vgabios.c:1978
    out DX, ax                                ; ef                          ; 0xc2e0c
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2e0d vgabios.c:1979
    pop dx                                    ; 5a                          ; 0xc2e10
    pop bx                                    ; 5b                          ; 0xc2e11
    pop bp                                    ; 5d                          ; 0xc2e12
    retn                                      ; c3                          ; 0xc2e13
  ; disGetNextSymbol 0xc2e14 LB 0x1750 -> off=0x0 cb=0000000000000078 uValue=00000000000c2e14 'load_text_patch'
load_text_patch:                             ; 0xc2e14 LB 0x78
    push bp                                   ; 55                          ; 0xc2e14 vgabios.c:1981
    mov bp, sp                                ; 89 e5                       ; 0xc2e15
    push si                                   ; 56                          ; 0xc2e17
    push di                                   ; 57                          ; 0xc2e18
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc2e19
    push ax                                   ; 50                          ; 0xc2e1c
    mov byte [bp-006h], cl                    ; 88 4e fa                    ; 0xc2e1d
    call 02cd9h                               ; e8 b6 fe                    ; 0xc2e20 vgabios.c:1986
    mov al, bl                                ; 88 d8                       ; 0xc2e23 vgabios.c:1988
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2e25
    xor ah, ah                                ; 30 e4                       ; 0xc2e27
    mov CL, strict byte 00eh                  ; b1 0e                       ; 0xc2e29
    mov di, ax                                ; 89 c7                       ; 0xc2e2b
    sal di, CL                                ; d3 e7                       ; 0xc2e2d
    mov al, bl                                ; 88 d8                       ; 0xc2e2f
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2e31
    mov CL, strict byte 00bh                  ; b1 0b                       ; 0xc2e33
    sal ax, CL                                ; d3 e0                       ; 0xc2e35
    add di, ax                                ; 01 c7                       ; 0xc2e37
    mov word [bp-00ah], di                    ; 89 7e f6                    ; 0xc2e39
    mov bx, dx                                ; 89 d3                       ; 0xc2e3c vgabios.c:1989
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc2e3e
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc2e41
    inc dx                                    ; 42                          ; 0xc2e44 vgabios.c:1990
    mov word [bp-00ch], dx                    ; 89 56 f4                    ; 0xc2e45
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc2e48 vgabios.c:1991
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc2e4b
    test al, al                               ; 84 c0                       ; 0xc2e4e
    je short 02e82h                           ; 74 30                       ; 0xc2e50
    xor ah, ah                                ; 30 e4                       ; 0xc2e52 vgabios.c:1992
    mov CL, strict byte 005h                  ; b1 05                       ; 0xc2e54
    sal ax, CL                                ; d3 e0                       ; 0xc2e56
    mov di, word [bp-00ah]                    ; 8b 7e f6                    ; 0xc2e58
    add di, ax                                ; 01 c7                       ; 0xc2e5b
    mov cl, byte [bp-006h]                    ; 8a 4e fa                    ; 0xc2e5d vgabios.c:1993
    xor ch, ch                                ; 30 ed                       ; 0xc2e60
    mov si, word [bp-00ch]                    ; 8b 76 f4                    ; 0xc2e62
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2e65
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2e68
    mov es, ax                                ; 8e c0                       ; 0xc2e6b
    jcxz 02e75h                               ; e3 06                       ; 0xc2e6d
    push DS                                   ; 1e                          ; 0xc2e6f
    mov ds, dx                                ; 8e da                       ; 0xc2e70
    rep movsb                                 ; f3 a4                       ; 0xc2e72
    pop DS                                    ; 1f                          ; 0xc2e74
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc2e75 vgabios.c:1994
    xor ah, ah                                ; 30 e4                       ; 0xc2e78
    inc ax                                    ; 40                          ; 0xc2e7a
    add word [bp-00ch], ax                    ; 01 46 f4                    ; 0xc2e7b
    add bx, ax                                ; 01 c3                       ; 0xc2e7e vgabios.c:1995
    jmp short 02e48h                          ; eb c6                       ; 0xc2e80 vgabios.c:1996
    call 02d0eh                               ; e8 89 fe                    ; 0xc2e82 vgabios.c:1998
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2e85 vgabios.c:1999
    pop di                                    ; 5f                          ; 0xc2e88
    pop si                                    ; 5e                          ; 0xc2e89
    pop bp                                    ; 5d                          ; 0xc2e8a
    retn                                      ; c3                          ; 0xc2e8b
  ; disGetNextSymbol 0xc2e8c LB 0x16d8 -> off=0x0 cb=0000000000000084 uValue=00000000000c2e8c 'biosfn_load_text_user_pat'
biosfn_load_text_user_pat:                   ; 0xc2e8c LB 0x84
    push bp                                   ; 55                          ; 0xc2e8c vgabios.c:2001
    mov bp, sp                                ; 89 e5                       ; 0xc2e8d
    push si                                   ; 56                          ; 0xc2e8f
    push di                                   ; 57                          ; 0xc2e90
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc2e91
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc2e94
    mov word [bp-00eh], dx                    ; 89 56 f2                    ; 0xc2e97
    mov word [bp-00ah], bx                    ; 89 5e f6                    ; 0xc2e9a
    mov word [bp-00ch], cx                    ; 89 4e f4                    ; 0xc2e9d
    call 02cd9h                               ; e8 36 fe                    ; 0xc2ea0 vgabios.c:2006
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2ea3 vgabios.c:2007
    and AL, strict byte 003h                  ; 24 03                       ; 0xc2ea6
    xor ah, ah                                ; 30 e4                       ; 0xc2ea8
    mov CL, strict byte 00eh                  ; b1 0e                       ; 0xc2eaa
    mov bx, ax                                ; 89 c3                       ; 0xc2eac
    sal bx, CL                                ; d3 e3                       ; 0xc2eae
    mov al, byte [bp+006h]                    ; 8a 46 06                    ; 0xc2eb0
    and AL, strict byte 004h                  ; 24 04                       ; 0xc2eb3
    mov CL, strict byte 00bh                  ; b1 0b                       ; 0xc2eb5
    sal ax, CL                                ; d3 e0                       ; 0xc2eb7
    add bx, ax                                ; 01 c3                       ; 0xc2eb9
    mov word [bp-008h], bx                    ; 89 5e f8                    ; 0xc2ebb
    xor bx, bx                                ; 31 db                       ; 0xc2ebe vgabios.c:2008
    cmp bx, word [bp-00ch]                    ; 3b 5e f4                    ; 0xc2ec0
    jnc short 02ef6h                          ; 73 31                       ; 0xc2ec3
    mov al, byte [bp+008h]                    ; 8a 46 08                    ; 0xc2ec5 vgabios.c:2010
    xor ah, ah                                ; 30 e4                       ; 0xc2ec8
    mov si, ax                                ; 89 c6                       ; 0xc2eca
    mov ax, bx                                ; 89 d8                       ; 0xc2ecc
    mul si                                    ; f7 e6                       ; 0xc2ece
    add ax, word [bp-00ah]                    ; 03 46 f6                    ; 0xc2ed0
    mov di, word [bp+004h]                    ; 8b 7e 04                    ; 0xc2ed3 vgabios.c:2011
    add di, bx                                ; 01 df                       ; 0xc2ed6
    mov CL, strict byte 005h                  ; b1 05                       ; 0xc2ed8
    sal di, CL                                ; d3 e7                       ; 0xc2eda
    add di, word [bp-008h]                    ; 03 7e f8                    ; 0xc2edc
    mov cx, si                                ; 89 f1                       ; 0xc2edf vgabios.c:2012
    mov si, ax                                ; 89 c6                       ; 0xc2ee1
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc2ee3
    mov ax, 0a000h                            ; b8 00 a0                    ; 0xc2ee6
    mov es, ax                                ; 8e c0                       ; 0xc2ee9
    jcxz 02ef3h                               ; e3 06                       ; 0xc2eeb
    push DS                                   ; 1e                          ; 0xc2eed
    mov ds, dx                                ; 8e da                       ; 0xc2eee
    rep movsb                                 ; f3 a4                       ; 0xc2ef0
    pop DS                                    ; 1f                          ; 0xc2ef2
    inc bx                                    ; 43                          ; 0xc2ef3 vgabios.c:2013
    jmp short 02ec0h                          ; eb ca                       ; 0xc2ef4
    call 02d0eh                               ; e8 15 fe                    ; 0xc2ef6 vgabios.c:2014
    cmp byte [bp-006h], 010h                  ; 80 7e fa 10                 ; 0xc2ef9 vgabios.c:2015
    jc short 02f07h                           ; 72 08                       ; 0xc2efd
    mov al, byte [bp+008h]                    ; 8a 46 08                    ; 0xc2eff vgabios.c:2017
    xor ah, ah                                ; 30 e4                       ; 0xc2f02
    call 02d41h                               ; e8 3a fe                    ; 0xc2f04
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f07 vgabios.c:2019
    pop di                                    ; 5f                          ; 0xc2f0a
    pop si                                    ; 5e                          ; 0xc2f0b
    pop bp                                    ; 5d                          ; 0xc2f0c
    retn 00006h                               ; c2 06 00                    ; 0xc2f0d
  ; disGetNextSymbol 0xc2f10 LB 0x1654 -> off=0x0 cb=0000000000000016 uValue=00000000000c2f10 'biosfn_load_gfx_8_8_chars'
biosfn_load_gfx_8_8_chars:                   ; 0xc2f10 LB 0x16
    push bp                                   ; 55                          ; 0xc2f10 vgabios.c:2021
    mov bp, sp                                ; 89 e5                       ; 0xc2f11
    push bx                                   ; 53                          ; 0xc2f13
    push cx                                   ; 51                          ; 0xc2f14
    mov bx, dx                                ; 89 d3                       ; 0xc2f15 vgabios.c:2023
    mov cx, ax                                ; 89 c1                       ; 0xc2f17
    mov ax, strict word 0001fh                ; b8 1f 00                    ; 0xc2f19
    call 009f0h                               ; e8 d1 da                    ; 0xc2f1c
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f1f vgabios.c:2024
    pop cx                                    ; 59                          ; 0xc2f22
    pop bx                                    ; 5b                          ; 0xc2f23
    pop bp                                    ; 5d                          ; 0xc2f24
    retn                                      ; c3                          ; 0xc2f25
  ; disGetNextSymbol 0xc2f26 LB 0x163e -> off=0x0 cb=000000000000004d uValue=00000000000c2f26 'set_gfx_font'
set_gfx_font:                                ; 0xc2f26 LB 0x4d
    push bp                                   ; 55                          ; 0xc2f26 vgabios.c:2026
    mov bp, sp                                ; 89 e5                       ; 0xc2f27
    push si                                   ; 56                          ; 0xc2f29
    push di                                   ; 57                          ; 0xc2f2a
    mov si, ax                                ; 89 c6                       ; 0xc2f2b
    mov ax, dx                                ; 89 d0                       ; 0xc2f2d
    mov di, bx                                ; 89 df                       ; 0xc2f2f
    mov dl, cl                                ; 88 ca                       ; 0xc2f31
    mov bx, si                                ; 89 f3                       ; 0xc2f33 vgabios.c:2030
    mov cx, ax                                ; 89 c1                       ; 0xc2f35
    mov ax, strict word 00043h                ; b8 43 00                    ; 0xc2f37
    call 009f0h                               ; e8 b3 da                    ; 0xc2f3a
    test dl, dl                               ; 84 d2                       ; 0xc2f3d vgabios.c:2031
    je short 02f53h                           ; 74 12                       ; 0xc2f3f
    cmp dl, 003h                              ; 80 fa 03                    ; 0xc2f41 vgabios.c:2032
    jbe short 02f48h                          ; 76 02                       ; 0xc2f44
    mov DL, strict byte 002h                  ; b2 02                       ; 0xc2f46 vgabios.c:2033
    mov bl, dl                                ; 88 d3                       ; 0xc2f48 vgabios.c:2034
    xor bh, bh                                ; 30 ff                       ; 0xc2f4a
    mov al, byte [bx+07e00h]                  ; 8a 87 00 7e                 ; 0xc2f4c
    mov byte [bp+004h], al                    ; 88 46 04                    ; 0xc2f50
    mov bx, 00085h                            ; bb 85 00                    ; 0xc2f53 vgabios.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc2f56
    mov es, ax                                ; 8e c0                       ; 0xc2f59
    mov word [es:bx], di                      ; 26 89 3f                    ; 0xc2f5b
    mov al, byte [bp+004h]                    ; 8a 46 04                    ; 0xc2f5e vgabios.c:2039
    xor ah, ah                                ; 30 e4                       ; 0xc2f61
    dec ax                                    ; 48                          ; 0xc2f63
    mov bx, 00084h                            ; bb 84 00                    ; 0xc2f64 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc2f67
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2f6a vgabios.c:2040
    pop di                                    ; 5f                          ; 0xc2f6d
    pop si                                    ; 5e                          ; 0xc2f6e
    pop bp                                    ; 5d                          ; 0xc2f6f
    retn 00002h                               ; c2 02 00                    ; 0xc2f70
  ; disGetNextSymbol 0xc2f73 LB 0x15f1 -> off=0x0 cb=000000000000001d uValue=00000000000c2f73 'biosfn_load_gfx_user_chars'
biosfn_load_gfx_user_chars:                  ; 0xc2f73 LB 0x1d
    push bp                                   ; 55                          ; 0xc2f73 vgabios.c:2042
    mov bp, sp                                ; 89 e5                       ; 0xc2f74
    push si                                   ; 56                          ; 0xc2f76
    mov si, ax                                ; 89 c6                       ; 0xc2f77
    mov ax, dx                                ; 89 d0                       ; 0xc2f79
    mov dl, byte [bp+004h]                    ; 8a 56 04                    ; 0xc2f7b vgabios.c:2045
    xor dh, dh                                ; 30 f6                       ; 0xc2f7e
    push dx                                   ; 52                          ; 0xc2f80
    xor ch, ch                                ; 30 ed                       ; 0xc2f81
    mov dx, si                                ; 89 f2                       ; 0xc2f83
    call 02f26h                               ; e8 9e ff                    ; 0xc2f85
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc2f88 vgabios.c:2046
    pop si                                    ; 5e                          ; 0xc2f8b
    pop bp                                    ; 5d                          ; 0xc2f8c
    retn 00002h                               ; c2 02 00                    ; 0xc2f8d
  ; disGetNextSymbol 0xc2f90 LB 0x15d4 -> off=0x0 cb=0000000000000022 uValue=00000000000c2f90 'biosfn_load_gfx_8_14_chars'
biosfn_load_gfx_8_14_chars:                  ; 0xc2f90 LB 0x22
    push bp                                   ; 55                          ; 0xc2f90 vgabios.c:2051
    mov bp, sp                                ; 89 e5                       ; 0xc2f91
    push bx                                   ; 53                          ; 0xc2f93
    push cx                                   ; 51                          ; 0xc2f94
    mov bl, al                                ; 88 c3                       ; 0xc2f95
    mov al, dl                                ; 88 d0                       ; 0xc2f97
    xor ah, ah                                ; 30 e4                       ; 0xc2f99 vgabios.c:2053
    push ax                                   ; 50                          ; 0xc2f9b
    mov al, bl                                ; 88 d8                       ; 0xc2f9c
    mov cx, ax                                ; 89 c1                       ; 0xc2f9e
    mov bx, strict word 0000eh                ; bb 0e 00                    ; 0xc2fa0
    mov ax, 05d6fh                            ; b8 6f 5d                    ; 0xc2fa3
    mov dx, ds                                ; 8c da                       ; 0xc2fa6
    call 02f26h                               ; e8 7b ff                    ; 0xc2fa8
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2fab vgabios.c:2054
    pop cx                                    ; 59                          ; 0xc2fae
    pop bx                                    ; 5b                          ; 0xc2faf
    pop bp                                    ; 5d                          ; 0xc2fb0
    retn                                      ; c3                          ; 0xc2fb1
  ; disGetNextSymbol 0xc2fb2 LB 0x15b2 -> off=0x0 cb=0000000000000022 uValue=00000000000c2fb2 'biosfn_load_gfx_8_8_dd_chars'
biosfn_load_gfx_8_8_dd_chars:                ; 0xc2fb2 LB 0x22
    push bp                                   ; 55                          ; 0xc2fb2 vgabios.c:2055
    mov bp, sp                                ; 89 e5                       ; 0xc2fb3
    push bx                                   ; 53                          ; 0xc2fb5
    push cx                                   ; 51                          ; 0xc2fb6
    mov bl, al                                ; 88 c3                       ; 0xc2fb7
    mov al, dl                                ; 88 d0                       ; 0xc2fb9
    xor ah, ah                                ; 30 e4                       ; 0xc2fbb vgabios.c:2057
    push ax                                   ; 50                          ; 0xc2fbd
    mov al, bl                                ; 88 d8                       ; 0xc2fbe
    mov cx, ax                                ; 89 c1                       ; 0xc2fc0
    mov bx, strict word 00008h                ; bb 08 00                    ; 0xc2fc2
    mov ax, 0556fh                            ; b8 6f 55                    ; 0xc2fc5
    mov dx, ds                                ; 8c da                       ; 0xc2fc8
    call 02f26h                               ; e8 59 ff                    ; 0xc2fca
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2fcd vgabios.c:2058
    pop cx                                    ; 59                          ; 0xc2fd0
    pop bx                                    ; 5b                          ; 0xc2fd1
    pop bp                                    ; 5d                          ; 0xc2fd2
    retn                                      ; c3                          ; 0xc2fd3
  ; disGetNextSymbol 0xc2fd4 LB 0x1590 -> off=0x0 cb=0000000000000022 uValue=00000000000c2fd4 'biosfn_load_gfx_8_16_chars'
biosfn_load_gfx_8_16_chars:                  ; 0xc2fd4 LB 0x22
    push bp                                   ; 55                          ; 0xc2fd4 vgabios.c:2059
    mov bp, sp                                ; 89 e5                       ; 0xc2fd5
    push bx                                   ; 53                          ; 0xc2fd7
    push cx                                   ; 51                          ; 0xc2fd8
    mov bl, al                                ; 88 c3                       ; 0xc2fd9
    mov al, dl                                ; 88 d0                       ; 0xc2fdb
    xor ah, ah                                ; 30 e4                       ; 0xc2fdd vgabios.c:2061
    push ax                                   ; 50                          ; 0xc2fdf
    mov al, bl                                ; 88 d8                       ; 0xc2fe0
    mov cx, ax                                ; 89 c1                       ; 0xc2fe2
    mov bx, strict word 00010h                ; bb 10 00                    ; 0xc2fe4
    mov ax, 06b6fh                            ; b8 6f 6b                    ; 0xc2fe7
    mov dx, ds                                ; 8c da                       ; 0xc2fea
    call 02f26h                               ; e8 37 ff                    ; 0xc2fec
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc2fef vgabios.c:2062
    pop cx                                    ; 59                          ; 0xc2ff2
    pop bx                                    ; 5b                          ; 0xc2ff3
    pop bp                                    ; 5d                          ; 0xc2ff4
    retn                                      ; c3                          ; 0xc2ff5
  ; disGetNextSymbol 0xc2ff6 LB 0x156e -> off=0x0 cb=0000000000000005 uValue=00000000000c2ff6 'biosfn_alternate_prtsc'
biosfn_alternate_prtsc:                      ; 0xc2ff6 LB 0x5
    push bp                                   ; 55                          ; 0xc2ff6 vgabios.c:2064
    mov bp, sp                                ; 89 e5                       ; 0xc2ff7
    pop bp                                    ; 5d                          ; 0xc2ff9 vgabios.c:2069
    retn                                      ; c3                          ; 0xc2ffa
  ; disGetNextSymbol 0xc2ffb LB 0x1569 -> off=0x0 cb=0000000000000032 uValue=00000000000c2ffb 'biosfn_set_txt_lines'
biosfn_set_txt_lines:                        ; 0xc2ffb LB 0x32
    push bx                                   ; 53                          ; 0xc2ffb vgabios.c:2071
    push si                                   ; 56                          ; 0xc2ffc
    push bp                                   ; 55                          ; 0xc2ffd
    mov bp, sp                                ; 89 e5                       ; 0xc2ffe
    mov bl, al                                ; 88 c3                       ; 0xc3000
    mov si, 00089h                            ; be 89 00                    ; 0xc3002 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3005
    mov es, ax                                ; 8e c0                       ; 0xc3008
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc300a
    and AL, strict byte 06fh                  ; 24 6f                       ; 0xc300d vgabios.c:2077
    cmp bl, 002h                              ; 80 fb 02                    ; 0xc300f vgabios.c:2079
    je short 0301ch                           ; 74 08                       ; 0xc3012
    test bl, bl                               ; 84 db                       ; 0xc3014
    jne short 0301eh                          ; 75 06                       ; 0xc3016
    or AL, strict byte 080h                   ; 0c 80                       ; 0xc3018 vgabios.c:2082
    jmp short 0301eh                          ; eb 02                       ; 0xc301a vgabios.c:2083
    or AL, strict byte 010h                   ; 0c 10                       ; 0xc301c vgabios.c:2085
    mov bx, 00089h                            ; bb 89 00                    ; 0xc301e vgabios.c:52
    mov si, strict word 00040h                ; be 40 00                    ; 0xc3021
    mov es, si                                ; 8e c6                       ; 0xc3024
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3026
    pop bp                                    ; 5d                          ; 0xc3029 vgabios.c:2089
    pop si                                    ; 5e                          ; 0xc302a
    pop bx                                    ; 5b                          ; 0xc302b
    retn                                      ; c3                          ; 0xc302c
  ; disGetNextSymbol 0xc302d LB 0x1537 -> off=0x0 cb=0000000000000005 uValue=00000000000c302d 'biosfn_switch_video_interface'
biosfn_switch_video_interface:               ; 0xc302d LB 0x5
    push bp                                   ; 55                          ; 0xc302d vgabios.c:2092
    mov bp, sp                                ; 89 e5                       ; 0xc302e
    pop bp                                    ; 5d                          ; 0xc3030 vgabios.c:2097
    retn                                      ; c3                          ; 0xc3031
  ; disGetNextSymbol 0xc3032 LB 0x1532 -> off=0x0 cb=0000000000000005 uValue=00000000000c3032 'biosfn_enable_video_refresh_control'
biosfn_enable_video_refresh_control:         ; 0xc3032 LB 0x5
    push bp                                   ; 55                          ; 0xc3032 vgabios.c:2098
    mov bp, sp                                ; 89 e5                       ; 0xc3033
    pop bp                                    ; 5d                          ; 0xc3035 vgabios.c:2103
    retn                                      ; c3                          ; 0xc3036
  ; disGetNextSymbol 0xc3037 LB 0x152d -> off=0x0 cb=000000000000008f uValue=00000000000c3037 'biosfn_write_string'
biosfn_write_string:                         ; 0xc3037 LB 0x8f
    push bp                                   ; 55                          ; 0xc3037 vgabios.c:2106
    mov bp, sp                                ; 89 e5                       ; 0xc3038
    push si                                   ; 56                          ; 0xc303a
    push di                                   ; 57                          ; 0xc303b
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc303c
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc303f
    mov byte [bp-006h], dl                    ; 88 56 fa                    ; 0xc3042
    mov byte [bp-00ah], bl                    ; 88 5e f6                    ; 0xc3045
    mov si, cx                                ; 89 ce                       ; 0xc3048
    mov di, word [bp+00ah]                    ; 8b 7e 0a                    ; 0xc304a
    mov al, dl                                ; 88 d0                       ; 0xc304d vgabios.c:2113
    xor ah, ah                                ; 30 e4                       ; 0xc304f
    lea bx, [bp-00eh]                         ; 8d 5e f2                    ; 0xc3051
    lea dx, [bp-00ch]                         ; 8d 56 f4                    ; 0xc3054
    call 00a97h                               ; e8 3d da                    ; 0xc3057
    cmp byte [bp+004h], 0ffh                  ; 80 7e 04 ff                 ; 0xc305a vgabios.c:2116
    jne short 0306ch                          ; 75 0c                       ; 0xc305e
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc3060 vgabios.c:2117
    mov byte [bp+006h], al                    ; 88 46 06                    ; 0xc3063
    mov ax, word [bp-00eh]                    ; 8b 46 f2                    ; 0xc3066 vgabios.c:2118
    mov byte [bp+004h], ah                    ; 88 66 04                    ; 0xc3069
    mov dh, byte [bp+004h]                    ; 8a 76 04                    ; 0xc306c vgabios.c:2121
    mov dl, byte [bp+006h]                    ; 8a 56 06                    ; 0xc306f
    xor ah, ah                                ; 30 e4                       ; 0xc3072
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3074 vgabios.c:2122
    call 012cfh                               ; e8 55 e2                    ; 0xc3077
    dec si                                    ; 4e                          ; 0xc307a vgabios.c:2124
    cmp si, strict byte 0ffffh                ; 83 fe ff                    ; 0xc307b
    je short 030ach                           ; 74 2c                       ; 0xc307e
    mov bx, di                                ; 89 fb                       ; 0xc3080 vgabios.c:2126
    inc di                                    ; 47                          ; 0xc3082
    mov es, [bp+008h]                         ; 8e 46 08                    ; 0xc3083 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3086
    test byte [bp-008h], 002h                 ; f6 46 f8 02                 ; 0xc3089 vgabios.c:2127
    je short 03098h                           ; 74 09                       ; 0xc308d
    mov bx, di                                ; 89 fb                       ; 0xc308f vgabios.c:2128
    inc di                                    ; 47                          ; 0xc3091
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc3092 vgabios.c:47
    mov byte [bp-00ah], ah                    ; 88 66 f6                    ; 0xc3095 vgabios.c:48
    mov bl, byte [bp-00ah]                    ; 8a 5e f6                    ; 0xc3098 vgabios.c:2130
    xor bh, bh                                ; 30 ff                       ; 0xc309b
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc309d
    xor dh, dh                                ; 30 f6                       ; 0xc30a0
    xor ah, ah                                ; 30 e4                       ; 0xc30a2
    mov cx, strict word 00003h                ; b9 03 00                    ; 0xc30a4
    call 02a76h                               ; e8 cc f9                    ; 0xc30a7
    jmp short 0307ah                          ; eb ce                       ; 0xc30aa vgabios.c:2131
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc30ac vgabios.c:2134
    jne short 030bdh                          ; 75 0b                       ; 0xc30b0
    mov dx, word [bp-00eh]                    ; 8b 56 f2                    ; 0xc30b2 vgabios.c:2135
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc30b5
    xor ah, ah                                ; 30 e4                       ; 0xc30b8
    call 012cfh                               ; e8 12 e2                    ; 0xc30ba
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc30bd vgabios.c:2136
    pop di                                    ; 5f                          ; 0xc30c0
    pop si                                    ; 5e                          ; 0xc30c1
    pop bp                                    ; 5d                          ; 0xc30c2
    retn 00008h                               ; c2 08 00                    ; 0xc30c3
  ; disGetNextSymbol 0xc30c6 LB 0x149e -> off=0x0 cb=00000000000001f2 uValue=00000000000c30c6 'biosfn_read_state_info'
biosfn_read_state_info:                      ; 0xc30c6 LB 0x1f2
    push bp                                   ; 55                          ; 0xc30c6 vgabios.c:2139
    mov bp, sp                                ; 89 e5                       ; 0xc30c7
    push cx                                   ; 51                          ; 0xc30c9
    push si                                   ; 56                          ; 0xc30ca
    push di                                   ; 57                          ; 0xc30cb
    push ax                                   ; 50                          ; 0xc30cc
    push ax                                   ; 50                          ; 0xc30cd
    push dx                                   ; 52                          ; 0xc30ce
    mov si, strict word 00049h                ; be 49 00                    ; 0xc30cf vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc30d2
    mov es, ax                                ; 8e c0                       ; 0xc30d5
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc30d7
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc30da vgabios.c:48
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc30dd vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc30e0
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc30e3 vgabios.c:58
    mov ax, ds                                ; 8c d8                       ; 0xc30e6 vgabios.c:2150
    mov es, dx                                ; 8e c2                       ; 0xc30e8 vgabios.c:72
    mov word [es:bx], 05505h                  ; 26 c7 07 05 55              ; 0xc30ea
    mov [es:bx+002h], ds                      ; 26 8c 5f 02                 ; 0xc30ef
    lea di, [bx+004h]                         ; 8d 7f 04                    ; 0xc30f3 vgabios.c:2155
    mov cx, strict word 0001eh                ; b9 1e 00                    ; 0xc30f6
    mov si, strict word 00049h                ; be 49 00                    ; 0xc30f9
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc30fc
    jcxz 03107h                               ; e3 06                       ; 0xc30ff
    push DS                                   ; 1e                          ; 0xc3101
    mov ds, dx                                ; 8e da                       ; 0xc3102
    rep movsb                                 ; f3 a4                       ; 0xc3104
    pop DS                                    ; 1f                          ; 0xc3106
    mov si, 00084h                            ; be 84 00                    ; 0xc3107 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc310a
    mov es, ax                                ; 8e c0                       ; 0xc310d
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc310f
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc3112 vgabios.c:48
    lea si, [bx+022h]                         ; 8d 77 22                    ; 0xc3114
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3117 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc311a
    lea di, [bx+023h]                         ; 8d 7f 23                    ; 0xc311d vgabios.c:2157
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc3120
    mov si, 00085h                            ; be 85 00                    ; 0xc3123
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3126
    jcxz 03131h                               ; e3 06                       ; 0xc3129
    push DS                                   ; 1e                          ; 0xc312b
    mov ds, dx                                ; 8e da                       ; 0xc312c
    rep movsb                                 ; f3 a4                       ; 0xc312e
    pop DS                                    ; 1f                          ; 0xc3130
    mov si, 0008ah                            ; be 8a 00                    ; 0xc3131 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3134
    mov es, ax                                ; 8e c0                       ; 0xc3137
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3139
    lea si, [bx+025h]                         ; 8d 77 25                    ; 0xc313c vgabios.c:48
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc313f vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3142
    lea si, [bx+026h]                         ; 8d 77 26                    ; 0xc3145 vgabios.c:2160
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc3148 vgabios.c:52
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc314c vgabios.c:2161
    mov word [es:si], strict word 00010h      ; 26 c7 04 10 00              ; 0xc314f vgabios.c:62
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc3154 vgabios.c:2162
    mov byte [es:si], 008h                    ; 26 c6 04 08                 ; 0xc3157 vgabios.c:52
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc315b vgabios.c:2163
    mov byte [es:si], 002h                    ; 26 c6 04 02                 ; 0xc315e vgabios.c:52
    lea si, [bx+02bh]                         ; 8d 77 2b                    ; 0xc3162 vgabios.c:2164
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc3165 vgabios.c:52
    lea si, [bx+02ch]                         ; 8d 77 2c                    ; 0xc3169 vgabios.c:2165
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc316c vgabios.c:52
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc3170 vgabios.c:2166
    mov byte [es:si], 021h                    ; 26 c6 04 21                 ; 0xc3173 vgabios.c:52
    lea si, [bx+031h]                         ; 8d 77 31                    ; 0xc3177 vgabios.c:2167
    mov byte [es:si], 003h                    ; 26 c6 04 03                 ; 0xc317a vgabios.c:52
    lea si, [bx+032h]                         ; 8d 77 32                    ; 0xc317e vgabios.c:2168
    mov byte [es:si], 000h                    ; 26 c6 04 00                 ; 0xc3181 vgabios.c:52
    mov si, 00089h                            ; be 89 00                    ; 0xc3185 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3188
    mov es, ax                                ; 8e c0                       ; 0xc318b
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc318d
    mov dl, al                                ; 88 c2                       ; 0xc3190 vgabios.c:2173
    and dl, 080h                              ; 80 e2 80                    ; 0xc3192
    xor dh, dh                                ; 30 f6                       ; 0xc3195
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc3197
    sar dx, CL                                ; d3 fa                       ; 0xc3199
    and AL, strict byte 010h                  ; 24 10                       ; 0xc319b
    xor ah, ah                                ; 30 e4                       ; 0xc319d
    mov CL, strict byte 004h                  ; b1 04                       ; 0xc319f
    sar ax, CL                                ; d3 f8                       ; 0xc31a1
    or ax, dx                                 ; 09 d0                       ; 0xc31a3
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc31a5 vgabios.c:2174
    je short 031bbh                           ; 74 11                       ; 0xc31a8
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc31aa
    je short 031b7h                           ; 74 08                       ; 0xc31ad
    test ax, ax                               ; 85 c0                       ; 0xc31af
    jne short 031bbh                          ; 75 08                       ; 0xc31b1
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc31b3 vgabios.c:2175
    jmp short 031bdh                          ; eb 06                       ; 0xc31b5
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc31b7 vgabios.c:2176
    jmp short 031bdh                          ; eb 02                       ; 0xc31b9
    xor al, al                                ; 30 c0                       ; 0xc31bb vgabios.c:2178
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc31bd vgabios.c:2180
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31c0 vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc31c3
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc31c6 vgabios.c:2183
    cmp AL, strict byte 00eh                  ; 3c 0e                       ; 0xc31c9
    jc short 031edh                           ; 72 20                       ; 0xc31cb
    cmp AL, strict byte 012h                  ; 3c 12                       ; 0xc31cd
    jnbe short 031edh                         ; 77 1c                       ; 0xc31cf
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc31d1 vgabios.c:2184
    test ax, ax                               ; 85 c0                       ; 0xc31d4
    je short 0322fh                           ; 74 57                       ; 0xc31d6
    mov si, ax                                ; 89 c6                       ; 0xc31d8 vgabios.c:2185
    shr si, 1                                 ; d1 ee                       ; 0xc31da
    shr si, 1                                 ; d1 ee                       ; 0xc31dc
    mov ax, 04000h                            ; b8 00 40                    ; 0xc31de
    xor dx, dx                                ; 31 d2                       ; 0xc31e1
    div si                                    ; f7 f6                       ; 0xc31e3
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc31e5
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc31e8 vgabios.c:52
    jmp short 0322fh                          ; eb 42                       ; 0xc31eb vgabios.c:2186
    lea si, [bx+029h]                         ; 8d 77 29                    ; 0xc31ed
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc31f0
    cmp AL, strict byte 013h                  ; 3c 13                       ; 0xc31f3
    jne short 03208h                          ; 75 11                       ; 0xc31f5
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc31f7 vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc31fa
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc31fe vgabios.c:2188
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc3201 vgabios.c:62
    jmp short 0322fh                          ; eb 27                       ; 0xc3206 vgabios.c:2189
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc3208
    jc short 0322fh                           ; 72 23                       ; 0xc320a
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc320c
    jnbe short 0322fh                         ; 77 1f                       ; 0xc320e
    cmp word [bp-00ah], strict byte 00000h    ; 83 7e f6 00                 ; 0xc3210 vgabios.c:2191
    je short 03224h                           ; 74 0e                       ; 0xc3214
    mov ax, 04000h                            ; b8 00 40                    ; 0xc3216 vgabios.c:2192
    xor dx, dx                                ; 31 d2                       ; 0xc3219
    div word [bp-00ah]                        ; f7 76 f6                    ; 0xc321b
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc321e vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3221
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc3224 vgabios.c:2193
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3227 vgabios.c:62
    mov word [es:si], strict word 00004h      ; 26 c7 04 04 00              ; 0xc322a
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc322f vgabios.c:2195
    cmp AL, strict byte 006h                  ; 3c 06                       ; 0xc3232
    je short 0323ah                           ; 74 04                       ; 0xc3234
    cmp AL, strict byte 011h                  ; 3c 11                       ; 0xc3236
    jne short 03245h                          ; 75 0b                       ; 0xc3238
    lea si, [bx+027h]                         ; 8d 77 27                    ; 0xc323a vgabios.c:2196
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc323d vgabios.c:62
    mov word [es:si], strict word 00002h      ; 26 c7 04 02 00              ; 0xc3240
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3245 vgabios.c:2198
    cmp AL, strict byte 004h                  ; 3c 04                       ; 0xc3248
    jc short 032a1h                           ; 72 55                       ; 0xc324a
    cmp AL, strict byte 007h                  ; 3c 07                       ; 0xc324c
    je short 032a1h                           ; 74 51                       ; 0xc324e
    lea si, [bx+02dh]                         ; 8d 77 2d                    ; 0xc3250 vgabios.c:2199
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc3253 vgabios.c:52
    mov byte [es:si], 001h                    ; 26 c6 04 01                 ; 0xc3256
    mov si, 00084h                            ; be 84 00                    ; 0xc325a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc325d
    mov es, ax                                ; 8e c0                       ; 0xc3260
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3262
    xor ah, ah                                ; 30 e4                       ; 0xc3265 vgabios.c:48
    inc ax                                    ; 40                          ; 0xc3267
    mov si, 00085h                            ; be 85 00                    ; 0xc3268 vgabios.c:47
    mov dl, byte [es:si]                      ; 26 8a 14                    ; 0xc326b
    xor dh, dh                                ; 30 f6                       ; 0xc326e vgabios.c:48
    imul dx                                   ; f7 ea                       ; 0xc3270
    cmp ax, 0015eh                            ; 3d 5e 01                    ; 0xc3272 vgabios.c:2201
    jc short 03285h                           ; 72 0e                       ; 0xc3275
    jbe short 0328eh                          ; 76 15                       ; 0xc3277
    cmp ax, 001e0h                            ; 3d e0 01                    ; 0xc3279
    je short 03296h                           ; 74 18                       ; 0xc327c
    cmp ax, 00190h                            ; 3d 90 01                    ; 0xc327e
    je short 03292h                           ; 74 0f                       ; 0xc3281
    jmp short 03296h                          ; eb 11                       ; 0xc3283
    cmp ax, 000c8h                            ; 3d c8 00                    ; 0xc3285
    jne short 03296h                          ; 75 0c                       ; 0xc3288
    xor al, al                                ; 30 c0                       ; 0xc328a vgabios.c:2202
    jmp short 03298h                          ; eb 0a                       ; 0xc328c
    mov AL, strict byte 001h                  ; b0 01                       ; 0xc328e vgabios.c:2203
    jmp short 03298h                          ; eb 06                       ; 0xc3290
    mov AL, strict byte 002h                  ; b0 02                       ; 0xc3292 vgabios.c:2204
    jmp short 03298h                          ; eb 02                       ; 0xc3294
    mov AL, strict byte 003h                  ; b0 03                       ; 0xc3296 vgabios.c:2206
    lea si, [bx+02ah]                         ; 8d 77 2a                    ; 0xc3298 vgabios.c:2208
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc329b vgabios.c:52
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc329e
    lea di, [bx+033h]                         ; 8d 7f 33                    ; 0xc32a1 vgabios.c:2211
    mov cx, strict word 0000dh                ; b9 0d 00                    ; 0xc32a4
    xor ax, ax                                ; 31 c0                       ; 0xc32a7
    mov es, [bp-00ch]                         ; 8e 46 f4                    ; 0xc32a9
    jcxz 032b0h                               ; e3 02                       ; 0xc32ac
    rep stosb                                 ; f3 aa                       ; 0xc32ae
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc32b0 vgabios.c:2212
    pop di                                    ; 5f                          ; 0xc32b3
    pop si                                    ; 5e                          ; 0xc32b4
    pop cx                                    ; 59                          ; 0xc32b5
    pop bp                                    ; 5d                          ; 0xc32b6
    retn                                      ; c3                          ; 0xc32b7
  ; disGetNextSymbol 0xc32b8 LB 0x12ac -> off=0x0 cb=0000000000000023 uValue=00000000000c32b8 'biosfn_read_video_state_size2'
biosfn_read_video_state_size2:               ; 0xc32b8 LB 0x23
    push dx                                   ; 52                          ; 0xc32b8 vgabios.c:2215
    push bp                                   ; 55                          ; 0xc32b9
    mov bp, sp                                ; 89 e5                       ; 0xc32ba
    mov dx, ax                                ; 89 c2                       ; 0xc32bc
    xor ax, ax                                ; 31 c0                       ; 0xc32be vgabios.c:2219
    test dl, 001h                             ; f6 c2 01                    ; 0xc32c0 vgabios.c:2220
    je short 032c8h                           ; 74 03                       ; 0xc32c3
    mov ax, strict word 00046h                ; b8 46 00                    ; 0xc32c5 vgabios.c:2221
    test dl, 002h                             ; f6 c2 02                    ; 0xc32c8 vgabios.c:2223
    je short 032d0h                           ; 74 03                       ; 0xc32cb
    add ax, strict word 0002ah                ; 05 2a 00                    ; 0xc32cd vgabios.c:2224
    test dl, 004h                             ; f6 c2 04                    ; 0xc32d0 vgabios.c:2226
    je short 032d8h                           ; 74 03                       ; 0xc32d3
    add ax, 00304h                            ; 05 04 03                    ; 0xc32d5 vgabios.c:2227
    pop bp                                    ; 5d                          ; 0xc32d8 vgabios.c:2230
    pop dx                                    ; 5a                          ; 0xc32d9
    retn                                      ; c3                          ; 0xc32da
  ; disGetNextSymbol 0xc32db LB 0x1289 -> off=0x0 cb=000000000000001b uValue=00000000000c32db 'vga_get_video_state_size'
vga_get_video_state_size:                    ; 0xc32db LB 0x1b
    push bp                                   ; 55                          ; 0xc32db vgabios.c:2232
    mov bp, sp                                ; 89 e5                       ; 0xc32dc
    push bx                                   ; 53                          ; 0xc32de
    push cx                                   ; 51                          ; 0xc32df
    mov bx, dx                                ; 89 d3                       ; 0xc32e0
    call 032b8h                               ; e8 d3 ff                    ; 0xc32e2 vgabios.c:2235
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc32e5
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc32e8
    shr ax, CL                                ; d3 e8                       ; 0xc32ea
    mov word [ss:bx], ax                      ; 36 89 07                    ; 0xc32ec
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc32ef vgabios.c:2236
    pop cx                                    ; 59                          ; 0xc32f2
    pop bx                                    ; 5b                          ; 0xc32f3
    pop bp                                    ; 5d                          ; 0xc32f4
    retn                                      ; c3                          ; 0xc32f5
  ; disGetNextSymbol 0xc32f6 LB 0x126e -> off=0x0 cb=00000000000002d8 uValue=00000000000c32f6 'biosfn_save_video_state'
biosfn_save_video_state:                     ; 0xc32f6 LB 0x2d8
    push bp                                   ; 55                          ; 0xc32f6 vgabios.c:2238
    mov bp, sp                                ; 89 e5                       ; 0xc32f7
    push cx                                   ; 51                          ; 0xc32f9
    push si                                   ; 56                          ; 0xc32fa
    push di                                   ; 57                          ; 0xc32fb
    push ax                                   ; 50                          ; 0xc32fc
    push ax                                   ; 50                          ; 0xc32fd
    push ax                                   ; 50                          ; 0xc32fe
    mov cx, dx                                ; 89 d1                       ; 0xc32ff
    mov si, strict word 00063h                ; be 63 00                    ; 0xc3301 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3304
    mov es, ax                                ; 8e c0                       ; 0xc3307
    mov di, word [es:si]                      ; 26 8b 3c                    ; 0xc3309
    mov si, di                                ; 89 fe                       ; 0xc330c vgabios.c:58
    test byte [bp-00ch], 001h                 ; f6 46 f4 01                 ; 0xc330e vgabios.c:2243
    je short 0337ah                           ; 74 66                       ; 0xc3312
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3314 vgabios.c:2244
    in AL, DX                                 ; ec                          ; 0xc3317
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3318
    mov es, cx                                ; 8e c1                       ; 0xc331a vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc331c
    inc bx                                    ; 43                          ; 0xc331f vgabios.c:2244
    mov dx, di                                ; 89 fa                       ; 0xc3320
    in AL, DX                                 ; ec                          ; 0xc3322
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3323
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3325 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3328 vgabios.c:2245
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc3329
    in AL, DX                                 ; ec                          ; 0xc332c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc332d
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc332f vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3332 vgabios.c:2246
    mov dx, 003dah                            ; ba da 03                    ; 0xc3333
    in AL, DX                                 ; ec                          ; 0xc3336
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3337
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc3339 vgabios.c:2248
    in AL, DX                                 ; ec                          ; 0xc333c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc333d
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc333f
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc3342 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3345
    inc bx                                    ; 43                          ; 0xc3348 vgabios.c:2249
    mov dx, 003cah                            ; ba ca 03                    ; 0xc3349
    in AL, DX                                 ; ec                          ; 0xc334c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc334d
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc334f vgabios.c:52
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3352 vgabios.c:2252
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3355
    add bx, ax                                ; 01 c3                       ; 0xc3358 vgabios.c:2250
    jmp short 03362h                          ; eb 06                       ; 0xc335a
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc335c
    jnbe short 0337dh                         ; 77 1b                       ; 0xc3360
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3362 vgabios.c:2253
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3365
    out DX, AL                                ; ee                          ; 0xc3368
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3369 vgabios.c:2254
    in AL, DX                                 ; ec                          ; 0xc336c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc336d
    mov es, cx                                ; 8e c1                       ; 0xc336f vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3371
    inc bx                                    ; 43                          ; 0xc3374 vgabios.c:2254
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3375 vgabios.c:2255
    jmp short 0335ch                          ; eb e2                       ; 0xc3378
    jmp near 0342ah                           ; e9 ad 00                    ; 0xc337a
    xor al, al                                ; 30 c0                       ; 0xc337d vgabios.c:2256
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc337f
    out DX, AL                                ; ee                          ; 0xc3382
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3383 vgabios.c:2257
    in AL, DX                                 ; ec                          ; 0xc3386
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3387
    mov es, cx                                ; 8e c1                       ; 0xc3389 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc338b
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc338e vgabios.c:2259
    inc bx                                    ; 43                          ; 0xc3393 vgabios.c:2257
    jmp short 0339ch                          ; eb 06                       ; 0xc3394
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc3396
    jnbe short 033b3h                         ; 77 17                       ; 0xc339a
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc339c vgabios.c:2260
    mov dx, si                                ; 89 f2                       ; 0xc339f
    out DX, AL                                ; ee                          ; 0xc33a1
    lea dx, [si+001h]                         ; 8d 54 01                    ; 0xc33a2 vgabios.c:2261
    in AL, DX                                 ; ec                          ; 0xc33a5
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc33a6
    mov es, cx                                ; 8e c1                       ; 0xc33a8 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc33aa
    inc bx                                    ; 43                          ; 0xc33ad vgabios.c:2261
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc33ae vgabios.c:2262
    jmp short 03396h                          ; eb e3                       ; 0xc33b1
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc33b3 vgabios.c:2264
    jmp short 033c0h                          ; eb 06                       ; 0xc33b8
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc33ba
    jnbe short 033e4h                         ; 77 24                       ; 0xc33be
    mov dx, 003dah                            ; ba da 03                    ; 0xc33c0 vgabios.c:2265
    in AL, DX                                 ; ec                          ; 0xc33c3
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc33c4
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc33c6 vgabios.c:2266
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc33c9
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc33cc
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc33cf
    out DX, AL                                ; ee                          ; 0xc33d2
    mov dx, 003c1h                            ; ba c1 03                    ; 0xc33d3 vgabios.c:2267
    in AL, DX                                 ; ec                          ; 0xc33d6
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc33d7
    mov es, cx                                ; 8e c1                       ; 0xc33d9 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc33db
    inc bx                                    ; 43                          ; 0xc33de vgabios.c:2267
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc33df vgabios.c:2268
    jmp short 033bah                          ; eb d6                       ; 0xc33e2
    mov dx, 003dah                            ; ba da 03                    ; 0xc33e4 vgabios.c:2269
    in AL, DX                                 ; ec                          ; 0xc33e7
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc33e8
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc33ea vgabios.c:2271
    jmp short 033f7h                          ; eb 06                       ; 0xc33ef
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc33f1
    jnbe short 0340fh                         ; 77 18                       ; 0xc33f5
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc33f7 vgabios.c:2272
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc33fa
    out DX, AL                                ; ee                          ; 0xc33fd
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc33fe vgabios.c:2273
    in AL, DX                                 ; ec                          ; 0xc3401
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3402
    mov es, cx                                ; 8e c1                       ; 0xc3404 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3406
    inc bx                                    ; 43                          ; 0xc3409 vgabios.c:2273
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc340a vgabios.c:2274
    jmp short 033f1h                          ; eb e2                       ; 0xc340d
    mov es, cx                                ; 8e c1                       ; 0xc340f vgabios.c:62
    mov word [es:bx], si                      ; 26 89 37                    ; 0xc3411
    inc bx                                    ; 43                          ; 0xc3414 vgabios.c:2276
    inc bx                                    ; 43                          ; 0xc3415
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3416 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc341a vgabios.c:2279
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc341b vgabios.c:52
    inc bx                                    ; 43                          ; 0xc341f vgabios.c:2280
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3420 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3424 vgabios.c:2281
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc3425 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3429 vgabios.c:2282
    test byte [bp-00ch], 002h                 ; f6 46 f4 02                 ; 0xc342a vgabios.c:2284
    jne short 03433h                          ; 75 03                       ; 0xc342e
    jmp near 03572h                           ; e9 3f 01                    ; 0xc3430
    mov si, strict word 00049h                ; be 49 00                    ; 0xc3433 vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3436
    mov es, ax                                ; 8e c0                       ; 0xc3439
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc343b
    mov es, cx                                ; 8e c1                       ; 0xc343e vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3440
    inc bx                                    ; 43                          ; 0xc3443 vgabios.c:2285
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc3444 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3447
    mov es, ax                                ; 8e c0                       ; 0xc344a
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc344c
    mov es, cx                                ; 8e c1                       ; 0xc344f vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3451
    inc bx                                    ; 43                          ; 0xc3454 vgabios.c:2286
    inc bx                                    ; 43                          ; 0xc3455
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc3456 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3459
    mov es, ax                                ; 8e c0                       ; 0xc345c
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc345e
    mov es, cx                                ; 8e c1                       ; 0xc3461 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3463
    inc bx                                    ; 43                          ; 0xc3466 vgabios.c:2287
    inc bx                                    ; 43                          ; 0xc3467
    mov si, strict word 00063h                ; be 63 00                    ; 0xc3468 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc346b
    mov es, ax                                ; 8e c0                       ; 0xc346e
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3470
    mov es, cx                                ; 8e c1                       ; 0xc3473 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3475
    inc bx                                    ; 43                          ; 0xc3478 vgabios.c:2288
    inc bx                                    ; 43                          ; 0xc3479
    mov si, 00084h                            ; be 84 00                    ; 0xc347a vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc347d
    mov es, ax                                ; 8e c0                       ; 0xc3480
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3482
    mov es, cx                                ; 8e c1                       ; 0xc3485 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3487
    inc bx                                    ; 43                          ; 0xc348a vgabios.c:2289
    mov si, 00085h                            ; be 85 00                    ; 0xc348b vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc348e
    mov es, ax                                ; 8e c0                       ; 0xc3491
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3493
    mov es, cx                                ; 8e c1                       ; 0xc3496 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3498
    inc bx                                    ; 43                          ; 0xc349b vgabios.c:2290
    inc bx                                    ; 43                          ; 0xc349c
    mov si, 00087h                            ; be 87 00                    ; 0xc349d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34a0
    mov es, ax                                ; 8e c0                       ; 0xc34a3
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc34a5
    mov es, cx                                ; 8e c1                       ; 0xc34a8 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc34aa
    inc bx                                    ; 43                          ; 0xc34ad vgabios.c:2291
    mov si, 00088h                            ; be 88 00                    ; 0xc34ae vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34b1
    mov es, ax                                ; 8e c0                       ; 0xc34b4
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc34b6
    mov es, cx                                ; 8e c1                       ; 0xc34b9 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc34bb
    inc bx                                    ; 43                          ; 0xc34be vgabios.c:2292
    mov si, 00089h                            ; be 89 00                    ; 0xc34bf vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34c2
    mov es, ax                                ; 8e c0                       ; 0xc34c5
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc34c7
    mov es, cx                                ; 8e c1                       ; 0xc34ca vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc34cc
    inc bx                                    ; 43                          ; 0xc34cf vgabios.c:2293
    mov si, strict word 00060h                ; be 60 00                    ; 0xc34d0 vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34d3
    mov es, ax                                ; 8e c0                       ; 0xc34d6
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34d8
    mov es, cx                                ; 8e c1                       ; 0xc34db vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc34dd
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc34e0 vgabios.c:2295
    inc bx                                    ; 43                          ; 0xc34e5 vgabios.c:2294
    inc bx                                    ; 43                          ; 0xc34e6
    jmp short 034efh                          ; eb 06                       ; 0xc34e7
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc34e9
    jnc short 0350bh                          ; 73 1c                       ; 0xc34ed
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc34ef vgabios.c:2296
    sal si, 1                                 ; d1 e6                       ; 0xc34f2
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc34f4
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc34f7 vgabios.c:57
    mov es, ax                                ; 8e c0                       ; 0xc34fa
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc34fc
    mov es, cx                                ; 8e c1                       ; 0xc34ff vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3501
    inc bx                                    ; 43                          ; 0xc3504 vgabios.c:2297
    inc bx                                    ; 43                          ; 0xc3505
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3506 vgabios.c:2298
    jmp short 034e9h                          ; eb de                       ; 0xc3509
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc350b vgabios.c:57
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc350e
    mov es, ax                                ; 8e c0                       ; 0xc3511
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3513
    mov es, cx                                ; 8e c1                       ; 0xc3516 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc3518
    inc bx                                    ; 43                          ; 0xc351b vgabios.c:2299
    inc bx                                    ; 43                          ; 0xc351c
    mov si, strict word 00062h                ; be 62 00                    ; 0xc351d vgabios.c:47
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3520
    mov es, ax                                ; 8e c0                       ; 0xc3523
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3525
    mov es, cx                                ; 8e c1                       ; 0xc3528 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc352a
    inc bx                                    ; 43                          ; 0xc352d vgabios.c:2300
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc352e vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc3531
    mov es, ax                                ; 8e c0                       ; 0xc3533
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3535
    mov es, cx                                ; 8e c1                       ; 0xc3538 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc353a
    inc bx                                    ; 43                          ; 0xc353d vgabios.c:2302
    inc bx                                    ; 43                          ; 0xc353e
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc353f vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc3542
    mov es, ax                                ; 8e c0                       ; 0xc3544
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3546
    mov es, cx                                ; 8e c1                       ; 0xc3549 vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc354b
    inc bx                                    ; 43                          ; 0xc354e vgabios.c:2303
    inc bx                                    ; 43                          ; 0xc354f
    mov si, 0010ch                            ; be 0c 01                    ; 0xc3550 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc3553
    mov es, ax                                ; 8e c0                       ; 0xc3555
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3557
    mov es, cx                                ; 8e c1                       ; 0xc355a vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc355c
    inc bx                                    ; 43                          ; 0xc355f vgabios.c:2304
    inc bx                                    ; 43                          ; 0xc3560
    mov si, 0010eh                            ; be 0e 01                    ; 0xc3561 vgabios.c:57
    xor ax, ax                                ; 31 c0                       ; 0xc3564
    mov es, ax                                ; 8e c0                       ; 0xc3566
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc3568
    mov es, cx                                ; 8e c1                       ; 0xc356b vgabios.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc356d
    inc bx                                    ; 43                          ; 0xc3570 vgabios.c:2305
    inc bx                                    ; 43                          ; 0xc3571
    test byte [bp-00ch], 004h                 ; f6 46 f4 04                 ; 0xc3572 vgabios.c:2307
    je short 035c4h                           ; 74 4c                       ; 0xc3576
    mov dx, 003c7h                            ; ba c7 03                    ; 0xc3578 vgabios.c:2309
    in AL, DX                                 ; ec                          ; 0xc357b
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc357c
    mov es, cx                                ; 8e c1                       ; 0xc357e vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3580
    inc bx                                    ; 43                          ; 0xc3583 vgabios.c:2309
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc3584
    in AL, DX                                 ; ec                          ; 0xc3587
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3588
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc358a vgabios.c:52
    inc bx                                    ; 43                          ; 0xc358d vgabios.c:2310
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc358e
    in AL, DX                                 ; ec                          ; 0xc3591
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3592
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc3594 vgabios.c:52
    inc bx                                    ; 43                          ; 0xc3597 vgabios.c:2311
    xor al, al                                ; 30 c0                       ; 0xc3598
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc359a
    out DX, AL                                ; ee                          ; 0xc359d
    xor ah, ah                                ; 30 e4                       ; 0xc359e vgabios.c:2314
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc35a0
    jmp short 035ach                          ; eb 07                       ; 0xc35a3
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc35a5
    jnc short 035bdh                          ; 73 11                       ; 0xc35aa
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc35ac vgabios.c:2315
    in AL, DX                                 ; ec                          ; 0xc35af
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc35b0
    mov es, cx                                ; 8e c1                       ; 0xc35b2 vgabios.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc35b4
    inc bx                                    ; 43                          ; 0xc35b7 vgabios.c:2315
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc35b8 vgabios.c:2316
    jmp short 035a5h                          ; eb e8                       ; 0xc35bb
    mov es, cx                                ; 8e c1                       ; 0xc35bd vgabios.c:52
    mov byte [es:bx], 000h                    ; 26 c6 07 00                 ; 0xc35bf
    inc bx                                    ; 43                          ; 0xc35c3 vgabios.c:2317
    mov ax, bx                                ; 89 d8                       ; 0xc35c4 vgabios.c:2320
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc35c6
    pop di                                    ; 5f                          ; 0xc35c9
    pop si                                    ; 5e                          ; 0xc35ca
    pop cx                                    ; 59                          ; 0xc35cb
    pop bp                                    ; 5d                          ; 0xc35cc
    retn                                      ; c3                          ; 0xc35cd
  ; disGetNextSymbol 0xc35ce LB 0xf96 -> off=0x0 cb=00000000000002ba uValue=00000000000c35ce 'biosfn_restore_video_state'
biosfn_restore_video_state:                  ; 0xc35ce LB 0x2ba
    push bp                                   ; 55                          ; 0xc35ce vgabios.c:2322
    mov bp, sp                                ; 89 e5                       ; 0xc35cf
    push cx                                   ; 51                          ; 0xc35d1
    push si                                   ; 56                          ; 0xc35d2
    push di                                   ; 57                          ; 0xc35d3
    sub sp, strict byte 00008h                ; 83 ec 08                    ; 0xc35d4
    push ax                                   ; 50                          ; 0xc35d7
    mov cx, dx                                ; 89 d1                       ; 0xc35d8
    test byte [bp-010h], 001h                 ; f6 46 f0 01                 ; 0xc35da vgabios.c:2326
    je short 03654h                           ; 74 74                       ; 0xc35de
    mov dx, 003dah                            ; ba da 03                    ; 0xc35e0 vgabios.c:2328
    in AL, DX                                 ; ec                          ; 0xc35e3
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc35e4
    lea si, [bx+040h]                         ; 8d 77 40                    ; 0xc35e6 vgabios.c:2330
    mov es, cx                                ; 8e c1                       ; 0xc35e9 vgabios.c:57
    mov ax, word [es:si]                      ; 26 8b 04                    ; 0xc35eb
    mov word [bp-00ch], ax                    ; 89 46 f4                    ; 0xc35ee vgabios.c:58
    mov si, bx                                ; 89 de                       ; 0xc35f1 vgabios.c:2331
    mov word [bp-008h], strict word 00001h    ; c7 46 f8 01 00              ; 0xc35f3 vgabios.c:2334
    add bx, strict byte 00005h                ; 83 c3 05                    ; 0xc35f8 vgabios.c:2332
    jmp short 03603h                          ; eb 06                       ; 0xc35fb
    cmp word [bp-008h], strict byte 00004h    ; 83 7e f8 04                 ; 0xc35fd
    jnbe short 03619h                         ; 77 16                       ; 0xc3601
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3603 vgabios.c:2335
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc3606
    out DX, AL                                ; ee                          ; 0xc3609
    mov es, cx                                ; 8e c1                       ; 0xc360a vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc360c
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc360f vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3612
    inc bx                                    ; 43                          ; 0xc3613 vgabios.c:2336
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3614 vgabios.c:2337
    jmp short 035fdh                          ; eb e4                       ; 0xc3617
    xor al, al                                ; 30 c0                       ; 0xc3619 vgabios.c:2338
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc361b
    out DX, AL                                ; ee                          ; 0xc361e
    mov es, cx                                ; 8e c1                       ; 0xc361f vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3621
    mov dx, 003c5h                            ; ba c5 03                    ; 0xc3624 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3627
    inc bx                                    ; 43                          ; 0xc3628 vgabios.c:2339
    mov dx, 003cch                            ; ba cc 03                    ; 0xc3629
    in AL, DX                                 ; ec                          ; 0xc362c
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc362d
    and AL, strict byte 0feh                  ; 24 fe                       ; 0xc362f
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc3631
    cmp word [bp-00ch], 003d4h                ; 81 7e f4 d4 03              ; 0xc3634 vgabios.c:2343
    jne short 0363fh                          ; 75 04                       ; 0xc3639
    or byte [bp-00eh], 001h                   ; 80 4e f2 01                 ; 0xc363b vgabios.c:2344
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc363f vgabios.c:2345
    mov dx, 003c2h                            ; ba c2 03                    ; 0xc3642
    out DX, AL                                ; ee                          ; 0xc3645
    mov ax, strict word 00011h                ; b8 11 00                    ; 0xc3646 vgabios.c:2348
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc3649
    out DX, ax                                ; ef                          ; 0xc364c
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc364d vgabios.c:2350
    jmp short 0365dh                          ; eb 09                       ; 0xc3652
    jmp near 03717h                           ; e9 c0 00                    ; 0xc3654
    cmp word [bp-008h], strict byte 00018h    ; 83 7e f8 18                 ; 0xc3657
    jnbe short 03677h                         ; 77 1a                       ; 0xc365b
    cmp word [bp-008h], strict byte 00011h    ; 83 7e f8 11                 ; 0xc365d vgabios.c:2351
    je short 03671h                           ; 74 0e                       ; 0xc3661
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc3663 vgabios.c:2352
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc3666
    out DX, AL                                ; ee                          ; 0xc3669
    mov es, cx                                ; 8e c1                       ; 0xc366a vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc366c
    inc dx                                    ; 42                          ; 0xc366f vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3670
    inc bx                                    ; 43                          ; 0xc3671 vgabios.c:2355
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3672 vgabios.c:2356
    jmp short 03657h                          ; eb e0                       ; 0xc3675
    mov AL, strict byte 011h                  ; b0 11                       ; 0xc3677 vgabios.c:2358
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc3679
    out DX, AL                                ; ee                          ; 0xc367c
    lea di, [word bx-00007h]                  ; 8d bf f9 ff                 ; 0xc367d vgabios.c:2359
    mov es, cx                                ; 8e c1                       ; 0xc3681 vgabios.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc3683
    inc dx                                    ; 42                          ; 0xc3686 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3687
    lea di, [si+003h]                         ; 8d 7c 03                    ; 0xc3688 vgabios.c:2362
    mov dl, byte [es:di]                      ; 26 8a 15                    ; 0xc368b vgabios.c:47
    xor dh, dh                                ; 30 f6                       ; 0xc368e vgabios.c:48
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3690
    mov dx, 003dah                            ; ba da 03                    ; 0xc3693 vgabios.c:2363
    in AL, DX                                 ; ec                          ; 0xc3696
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3697
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc3699 vgabios.c:2364
    jmp short 036a6h                          ; eb 06                       ; 0xc369e
    cmp word [bp-008h], strict byte 00013h    ; 83 7e f8 13                 ; 0xc36a0
    jnbe short 036bfh                         ; 77 19                       ; 0xc36a4
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc36a6 vgabios.c:2365
    and ax, strict word 00020h                ; 25 20 00                    ; 0xc36a9
    or ax, word [bp-008h]                     ; 0b 46 f8                    ; 0xc36ac
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc36af
    out DX, AL                                ; ee                          ; 0xc36b2
    mov es, cx                                ; 8e c1                       ; 0xc36b3 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc36b5
    out DX, AL                                ; ee                          ; 0xc36b8 vgabios.c:48
    inc bx                                    ; 43                          ; 0xc36b9 vgabios.c:2366
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc36ba vgabios.c:2367
    jmp short 036a0h                          ; eb e1                       ; 0xc36bd
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc36bf vgabios.c:2368
    mov dx, 003c0h                            ; ba c0 03                    ; 0xc36c2
    out DX, AL                                ; ee                          ; 0xc36c5
    mov dx, 003dah                            ; ba da 03                    ; 0xc36c6 vgabios.c:2369
    in AL, DX                                 ; ec                          ; 0xc36c9
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc36ca
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc36cc vgabios.c:2371
    jmp short 036d9h                          ; eb 06                       ; 0xc36d1
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc36d3
    jnbe short 036efh                         ; 77 16                       ; 0xc36d7
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc36d9 vgabios.c:2372
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc36dc
    out DX, AL                                ; ee                          ; 0xc36df
    mov es, cx                                ; 8e c1                       ; 0xc36e0 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc36e2
    mov dx, 003cfh                            ; ba cf 03                    ; 0xc36e5 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc36e8
    inc bx                                    ; 43                          ; 0xc36e9 vgabios.c:2373
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc36ea vgabios.c:2374
    jmp short 036d3h                          ; eb e4                       ; 0xc36ed
    add bx, strict byte 00006h                ; 83 c3 06                    ; 0xc36ef vgabios.c:2375
    mov es, cx                                ; 8e c1                       ; 0xc36f2 vgabios.c:47
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc36f4
    mov dx, 003c4h                            ; ba c4 03                    ; 0xc36f7 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc36fa
    inc si                                    ; 46                          ; 0xc36fb vgabios.c:2378
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc36fc vgabios.c:47
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc36ff vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3702
    inc si                                    ; 46                          ; 0xc3703 vgabios.c:2379
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc3704 vgabios.c:47
    mov dx, 003ceh                            ; ba ce 03                    ; 0xc3707 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc370a
    inc si                                    ; 46                          ; 0xc370b vgabios.c:2380
    inc si                                    ; 46                          ; 0xc370c
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc370d vgabios.c:47
    mov dx, word [bp-00ch]                    ; 8b 56 f4                    ; 0xc3710 vgabios.c:48
    add dx, strict byte 00006h                ; 83 c2 06                    ; 0xc3713
    out DX, AL                                ; ee                          ; 0xc3716
    test byte [bp-010h], 002h                 ; f6 46 f0 02                 ; 0xc3717 vgabios.c:2384
    jne short 03720h                          ; 75 03                       ; 0xc371b
    jmp near 0383bh                           ; e9 1b 01                    ; 0xc371d
    mov es, cx                                ; 8e c1                       ; 0xc3720 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3722
    mov si, strict word 00049h                ; be 49 00                    ; 0xc3725 vgabios.c:52
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc3728
    mov es, dx                                ; 8e c2                       ; 0xc372b
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc372d
    inc bx                                    ; 43                          ; 0xc3730 vgabios.c:2385
    mov es, cx                                ; 8e c1                       ; 0xc3731 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3733
    mov si, strict word 0004ah                ; be 4a 00                    ; 0xc3736 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3739
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc373b
    inc bx                                    ; 43                          ; 0xc373e vgabios.c:2386
    inc bx                                    ; 43                          ; 0xc373f
    mov es, cx                                ; 8e c1                       ; 0xc3740 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3742
    mov si, strict word 0004ch                ; be 4c 00                    ; 0xc3745 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3748
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc374a
    inc bx                                    ; 43                          ; 0xc374d vgabios.c:2387
    inc bx                                    ; 43                          ; 0xc374e
    mov es, cx                                ; 8e c1                       ; 0xc374f vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3751
    mov si, strict word 00063h                ; be 63 00                    ; 0xc3754 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3757
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3759
    inc bx                                    ; 43                          ; 0xc375c vgabios.c:2388
    inc bx                                    ; 43                          ; 0xc375d
    mov es, cx                                ; 8e c1                       ; 0xc375e vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3760
    mov si, 00084h                            ; be 84 00                    ; 0xc3763 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3766
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3768
    inc bx                                    ; 43                          ; 0xc376b vgabios.c:2389
    mov es, cx                                ; 8e c1                       ; 0xc376c vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc376e
    mov si, 00085h                            ; be 85 00                    ; 0xc3771 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3774
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3776
    inc bx                                    ; 43                          ; 0xc3779 vgabios.c:2390
    inc bx                                    ; 43                          ; 0xc377a
    mov es, cx                                ; 8e c1                       ; 0xc377b vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc377d
    mov si, 00087h                            ; be 87 00                    ; 0xc3780 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3783
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3785
    inc bx                                    ; 43                          ; 0xc3788 vgabios.c:2391
    mov es, cx                                ; 8e c1                       ; 0xc3789 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc378b
    mov si, 00088h                            ; be 88 00                    ; 0xc378e vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc3791
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3793
    inc bx                                    ; 43                          ; 0xc3796 vgabios.c:2392
    mov es, cx                                ; 8e c1                       ; 0xc3797 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3799
    mov si, 00089h                            ; be 89 00                    ; 0xc379c vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc379f
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc37a1
    inc bx                                    ; 43                          ; 0xc37a4 vgabios.c:2393
    mov es, cx                                ; 8e c1                       ; 0xc37a5 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37a7
    mov si, strict word 00060h                ; be 60 00                    ; 0xc37aa vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37ad
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37af
    mov word [bp-008h], strict word 00000h    ; c7 46 f8 00 00              ; 0xc37b2 vgabios.c:2395
    inc bx                                    ; 43                          ; 0xc37b7 vgabios.c:2394
    inc bx                                    ; 43                          ; 0xc37b8
    jmp short 037c1h                          ; eb 06                       ; 0xc37b9
    cmp word [bp-008h], strict byte 00008h    ; 83 7e f8 08                 ; 0xc37bb
    jnc short 037ddh                          ; 73 1c                       ; 0xc37bf
    mov es, cx                                ; 8e c1                       ; 0xc37c1 vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37c3
    mov si, word [bp-008h]                    ; 8b 76 f8                    ; 0xc37c6 vgabios.c:58
    sal si, 1                                 ; d1 e6                       ; 0xc37c9
    add si, strict byte 00050h                ; 83 c6 50                    ; 0xc37cb
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc37ce vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc37d1
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37d3
    inc bx                                    ; 43                          ; 0xc37d6 vgabios.c:2397
    inc bx                                    ; 43                          ; 0xc37d7
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc37d8 vgabios.c:2398
    jmp short 037bbh                          ; eb de                       ; 0xc37db
    mov es, cx                                ; 8e c1                       ; 0xc37dd vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37df
    mov si, strict word 0004eh                ; be 4e 00                    ; 0xc37e2 vgabios.c:62
    mov dx, strict word 00040h                ; ba 40 00                    ; 0xc37e5
    mov es, dx                                ; 8e c2                       ; 0xc37e8
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc37ea
    inc bx                                    ; 43                          ; 0xc37ed vgabios.c:2399
    inc bx                                    ; 43                          ; 0xc37ee
    mov es, cx                                ; 8e c1                       ; 0xc37ef vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc37f1
    mov si, strict word 00062h                ; be 62 00                    ; 0xc37f4 vgabios.c:52
    mov es, dx                                ; 8e c2                       ; 0xc37f7
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc37f9
    inc bx                                    ; 43                          ; 0xc37fc vgabios.c:2400
    mov es, cx                                ; 8e c1                       ; 0xc37fd vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc37ff
    mov si, strict word 0007ch                ; be 7c 00                    ; 0xc3802 vgabios.c:62
    xor dx, dx                                ; 31 d2                       ; 0xc3805
    mov es, dx                                ; 8e c2                       ; 0xc3807
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3809
    inc bx                                    ; 43                          ; 0xc380c vgabios.c:2402
    inc bx                                    ; 43                          ; 0xc380d
    mov es, cx                                ; 8e c1                       ; 0xc380e vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc3810
    mov si, strict word 0007eh                ; be 7e 00                    ; 0xc3813 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3816
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3818
    inc bx                                    ; 43                          ; 0xc381b vgabios.c:2403
    inc bx                                    ; 43                          ; 0xc381c
    mov es, cx                                ; 8e c1                       ; 0xc381d vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc381f
    mov si, 0010ch                            ; be 0c 01                    ; 0xc3822 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3825
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3827
    inc bx                                    ; 43                          ; 0xc382a vgabios.c:2404
    inc bx                                    ; 43                          ; 0xc382b
    mov es, cx                                ; 8e c1                       ; 0xc382c vgabios.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc382e
    mov si, 0010eh                            ; be 0e 01                    ; 0xc3831 vgabios.c:62
    mov es, dx                                ; 8e c2                       ; 0xc3834
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc3836
    inc bx                                    ; 43                          ; 0xc3839 vgabios.c:2405
    inc bx                                    ; 43                          ; 0xc383a
    test byte [bp-010h], 004h                 ; f6 46 f0 04                 ; 0xc383b vgabios.c:2407
    je short 0387eh                           ; 74 3d                       ; 0xc383f
    inc bx                                    ; 43                          ; 0xc3841 vgabios.c:2408
    mov es, cx                                ; 8e c1                       ; 0xc3842 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3844
    xor ah, ah                                ; 30 e4                       ; 0xc3847 vgabios.c:48
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc3849
    inc bx                                    ; 43                          ; 0xc384c vgabios.c:2409
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc384d vgabios.c:47
    mov dx, 003c6h                            ; ba c6 03                    ; 0xc3850 vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc3853
    inc bx                                    ; 43                          ; 0xc3854 vgabios.c:2410
    xor al, al                                ; 30 c0                       ; 0xc3855
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc3857
    out DX, AL                                ; ee                          ; 0xc385a
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc385b vgabios.c:2413
    jmp short 03867h                          ; eb 07                       ; 0xc385e
    cmp word [bp-008h], 00300h                ; 81 7e f8 00 03              ; 0xc3860
    jnc short 03876h                          ; 73 0f                       ; 0xc3865
    mov es, cx                                ; 8e c1                       ; 0xc3867 vgabios.c:47
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc3869
    mov dx, 003c9h                            ; ba c9 03                    ; 0xc386c vgabios.c:48
    out DX, AL                                ; ee                          ; 0xc386f
    inc bx                                    ; 43                          ; 0xc3870 vgabios.c:2414
    inc word [bp-008h]                        ; ff 46 f8                    ; 0xc3871 vgabios.c:2415
    jmp short 03860h                          ; eb ea                       ; 0xc3874
    inc bx                                    ; 43                          ; 0xc3876 vgabios.c:2416
    mov al, byte [bp-00eh]                    ; 8a 46 f2                    ; 0xc3877
    mov dx, 003c8h                            ; ba c8 03                    ; 0xc387a
    out DX, AL                                ; ee                          ; 0xc387d
    mov ax, bx                                ; 89 d8                       ; 0xc387e vgabios.c:2420
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc3880
    pop di                                    ; 5f                          ; 0xc3883
    pop si                                    ; 5e                          ; 0xc3884
    pop cx                                    ; 59                          ; 0xc3885
    pop bp                                    ; 5d                          ; 0xc3886
    retn                                      ; c3                          ; 0xc3887
  ; disGetNextSymbol 0xc3888 LB 0xcdc -> off=0x0 cb=000000000000002b uValue=00000000000c3888 'find_vga_entry'
find_vga_entry:                              ; 0xc3888 LB 0x2b
    push bx                                   ; 53                          ; 0xc3888 vgabios.c:2429
    push cx                                   ; 51                          ; 0xc3889
    push dx                                   ; 52                          ; 0xc388a
    push bp                                   ; 55                          ; 0xc388b
    mov bp, sp                                ; 89 e5                       ; 0xc388c
    mov dl, al                                ; 88 c2                       ; 0xc388e
    mov AH, strict byte 0ffh                  ; b4 ff                       ; 0xc3890 vgabios.c:2431
    xor al, al                                ; 30 c0                       ; 0xc3892 vgabios.c:2432
    jmp short 0389ch                          ; eb 06                       ; 0xc3894
    db  0feh, 0c0h
    ; inc al                                    ; fe c0                     ; 0xc3896 vgabios.c:2433
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc3898
    jnbe short 038ach                         ; 77 10                       ; 0xc389a
    mov bl, al                                ; 88 c3                       ; 0xc389c
    xor bh, bh                                ; 30 ff                       ; 0xc389e
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc38a0
    sal bx, CL                                ; d3 e3                       ; 0xc38a2
    cmp dl, byte [bx+047b1h]                  ; 3a 97 b1 47                 ; 0xc38a4
    jne short 03896h                          ; 75 ec                       ; 0xc38a8
    mov ah, al                                ; 88 c4                       ; 0xc38aa
    mov al, ah                                ; 88 e0                       ; 0xc38ac vgabios.c:2438
    pop bp                                    ; 5d                          ; 0xc38ae
    pop dx                                    ; 5a                          ; 0xc38af
    pop cx                                    ; 59                          ; 0xc38b0
    pop bx                                    ; 5b                          ; 0xc38b1
    retn                                      ; c3                          ; 0xc38b2
  ; disGetNextSymbol 0xc38b3 LB 0xcb1 -> off=0x0 cb=000000000000000e uValue=00000000000c38b3 'readx_byte'
readx_byte:                                  ; 0xc38b3 LB 0xe
    push bx                                   ; 53                          ; 0xc38b3 vgabios.c:2450
    push bp                                   ; 55                          ; 0xc38b4
    mov bp, sp                                ; 89 e5                       ; 0xc38b5
    mov bx, dx                                ; 89 d3                       ; 0xc38b7
    mov es, ax                                ; 8e c0                       ; 0xc38b9 vgabios.c:2452
    mov al, byte [es:bx]                      ; 26 8a 07                    ; 0xc38bb
    pop bp                                    ; 5d                          ; 0xc38be vgabios.c:2453
    pop bx                                    ; 5b                          ; 0xc38bf
    retn                                      ; c3                          ; 0xc38c0
  ; disGetNextSymbol 0xc38c1 LB 0xca3 -> off=0x8a cb=0000000000000456 uValue=00000000000c394b 'int10_func'
    db  056h, 04fh, 01ch, 01bh, 013h, 012h, 011h, 010h, 00eh, 00dh, 00ch, 00ah, 009h, 008h, 007h, 006h
    db  005h, 004h, 003h, 002h, 001h, 000h, 09ah, 03dh, 075h, 039h, 0b2h, 039h, 0c1h, 039h, 0cfh, 039h
    db  0dfh, 039h, 0efh, 039h, 0f9h, 039h, 022h, 03ah, 04bh, 03ah, 059h, 03ah, 06fh, 03ah, 087h, 03ah
    db  0aah, 03ah, 0beh, 03ah, 0d4h, 03ah, 0e0h, 03ah, 0e2h, 03bh, 068h, 03ch, 08bh, 03ch, 09fh, 03ch
    db  0e1h, 03ch, 06ch, 03dh, 030h, 024h, 023h, 022h, 021h, 020h, 014h, 012h, 011h, 010h, 004h, 003h
    db  002h, 001h, 000h, 09ah, 03dh, 0ffh, 03ah, 01ah, 03bh, 038h, 03bh, 050h, 03bh, 05bh, 03bh, 0ffh
    db  03ah, 01ah, 03bh, 038h, 03bh, 05bh, 03bh, 073h, 03bh, 07eh, 03bh, 097h, 03bh, 0a6h, 03bh, 0b5h
    db  03bh, 0c2h, 03bh, 00ah, 009h, 006h, 004h, 002h, 001h, 000h, 05eh, 03dh, 007h, 03dh, 015h, 03dh
    db  026h, 03dh, 036h, 03dh, 04bh, 03dh, 05eh, 03dh, 05eh, 03dh
int10_func:                                  ; 0xc394b LB 0x456
    push bp                                   ; 55                          ; 0xc394b vgabios.c:2531
    mov bp, sp                                ; 89 e5                       ; 0xc394c
    push si                                   ; 56                          ; 0xc394e
    push di                                   ; 57                          ; 0xc394f
    push ax                                   ; 50                          ; 0xc3950
    mov si, word [bp+004h]                    ; 8b 76 04                    ; 0xc3951
    mov al, byte [bp+013h]                    ; 8a 46 13                    ; 0xc3954 vgabios.c:2536
    xor ah, ah                                ; 30 e4                       ; 0xc3957
    mov dx, ax                                ; 89 c2                       ; 0xc3959
    cmp ax, strict word 00056h                ; 3d 56 00                    ; 0xc395b
    jnbe short 039cch                         ; 77 6c                       ; 0xc395e
    push CS                                   ; 0e                          ; 0xc3960
    pop ES                                    ; 07                          ; 0xc3961
    mov cx, strict word 00017h                ; b9 17 00                    ; 0xc3962
    mov di, 038c1h                            ; bf c1 38                    ; 0xc3965
    repne scasb                               ; f2 ae                       ; 0xc3968
    sal cx, 1                                 ; d1 e1                       ; 0xc396a
    mov di, cx                                ; 89 cf                       ; 0xc396c
    mov ax, word [cs:di+038d7h]               ; 2e 8b 85 d7 38              ; 0xc396e
    jmp ax                                    ; ff e0                       ; 0xc3973
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3975 vgabios.c:2539
    xor ah, ah                                ; 30 e4                       ; 0xc3978
    call 01479h                               ; e8 fc da                    ; 0xc397a
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc397d vgabios.c:2540
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc3980
    cmp ax, strict word 00007h                ; 3d 07 00                    ; 0xc3983
    je short 0399dh                           ; 74 15                       ; 0xc3986
    cmp ax, strict word 00006h                ; 3d 06 00                    ; 0xc3988
    je short 03994h                           ; 74 07                       ; 0xc398b
    cmp ax, strict word 00005h                ; 3d 05 00                    ; 0xc398d
    jbe short 0399dh                          ; 76 0b                       ; 0xc3990
    jmp short 039a6h                          ; eb 12                       ; 0xc3992
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3994 vgabios.c:2542
    xor al, al                                ; 30 c0                       ; 0xc3997
    or AL, strict byte 03fh                   ; 0c 3f                       ; 0xc3999
    jmp short 039adh                          ; eb 10                       ; 0xc399b vgabios.c:2543
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc399d vgabios.c:2551
    xor al, al                                ; 30 c0                       ; 0xc39a0
    or AL, strict byte 030h                   ; 0c 30                       ; 0xc39a2
    jmp short 039adh                          ; eb 07                       ; 0xc39a4
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc39a6 vgabios.c:2554
    xor al, al                                ; 30 c0                       ; 0xc39a9
    or AL, strict byte 020h                   ; 0c 20                       ; 0xc39ab
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc39ad
    jmp short 039cch                          ; eb 1a                       ; 0xc39b0 vgabios.c:2556
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc39b2 vgabios.c:2558
    xor ah, ah                                ; 30 e4                       ; 0xc39b5
    mov dx, ax                                ; 89 c2                       ; 0xc39b7
    mov al, byte [bp+011h]                    ; 8a 46 11                    ; 0xc39b9
    call 011d3h                               ; e8 14 d8                    ; 0xc39bc
    jmp short 039cch                          ; eb 0b                       ; 0xc39bf vgabios.c:2559
    mov dx, word [bp+00eh]                    ; 8b 56 0e                    ; 0xc39c1 vgabios.c:2561
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc39c4
    xor ah, ah                                ; 30 e4                       ; 0xc39c7
    call 012cfh                               ; e8 03 d9                    ; 0xc39c9
    jmp near 03d9ah                           ; e9 cb 03                    ; 0xc39cc vgabios.c:2562
    lea bx, [bp+00eh]                         ; 8d 5e 0e                    ; 0xc39cf vgabios.c:2564
    lea dx, [bp+010h]                         ; 8d 56 10                    ; 0xc39d2
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc39d5
    xor ah, ah                                ; 30 e4                       ; 0xc39d8
    call 00a97h                               ; e8 ba d0                    ; 0xc39da
    jmp short 039cch                          ; eb ed                       ; 0xc39dd vgabios.c:2565
    xor ax, ax                                ; 31 c0                       ; 0xc39df vgabios.c:2571
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc39e1
    mov word [bp+00ch], ax                    ; 89 46 0c                    ; 0xc39e4 vgabios.c:2572
    mov word [bp+010h], ax                    ; 89 46 10                    ; 0xc39e7 vgabios.c:2573
    mov word [bp+00eh], ax                    ; 89 46 0e                    ; 0xc39ea vgabios.c:2574
    jmp short 039cch                          ; eb dd                       ; 0xc39ed vgabios.c:2575
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc39ef vgabios.c:2577
    xor ah, ah                                ; 30 e4                       ; 0xc39f2
    call 0135ch                               ; e8 65 d9                    ; 0xc39f4
    jmp short 039cch                          ; eb d3                       ; 0xc39f7 vgabios.c:2578
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc39f9 vgabios.c:2580
    push ax                                   ; 50                          ; 0xc39fc
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc39fd
    push ax                                   ; 50                          ; 0xc3a00
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3a01
    xor ah, ah                                ; 30 e4                       ; 0xc3a04
    push ax                                   ; 50                          ; 0xc3a06
    mov al, byte [bp+00fh]                    ; 8a 46 0f                    ; 0xc3a07
    push ax                                   ; 50                          ; 0xc3a0a
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc3a0b
    mov cx, ax                                ; 89 c1                       ; 0xc3a0e
    mov bl, byte [bp+011h]                    ; 8a 5e 11                    ; 0xc3a10
    xor bh, bh                                ; 30 ff                       ; 0xc3a13
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3a15
    mov dx, ax                                ; 89 c2                       ; 0xc3a18
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a1a
    call 01c9fh                               ; e8 7f e2                    ; 0xc3a1d
    jmp short 039cch                          ; eb aa                       ; 0xc3a20 vgabios.c:2581
    xor ax, ax                                ; 31 c0                       ; 0xc3a22 vgabios.c:2583
    push ax                                   ; 50                          ; 0xc3a24
    mov ax, 000ffh                            ; b8 ff 00                    ; 0xc3a25
    push ax                                   ; 50                          ; 0xc3a28
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3a29
    xor ah, ah                                ; 30 e4                       ; 0xc3a2c
    push ax                                   ; 50                          ; 0xc3a2e
    mov al, byte [bp+00fh]                    ; 8a 46 0f                    ; 0xc3a2f
    push ax                                   ; 50                          ; 0xc3a32
    mov al, byte [bp+010h]                    ; 8a 46 10                    ; 0xc3a33
    mov cx, ax                                ; 89 c1                       ; 0xc3a36
    mov al, byte [bp+011h]                    ; 8a 46 11                    ; 0xc3a38
    mov bx, ax                                ; 89 c3                       ; 0xc3a3b
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3a3d
    mov dl, byte [bp+012h]                    ; 8a 56 12                    ; 0xc3a40
    mov si, dx                                ; 89 d6                       ; 0xc3a43
    mov dx, ax                                ; 89 c2                       ; 0xc3a45
    mov ax, si                                ; 89 f0                       ; 0xc3a47
    jmp short 03a1dh                          ; eb d2                       ; 0xc3a49
    lea dx, [bp+012h]                         ; 8d 56 12                    ; 0xc3a4b vgabios.c:2586
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3a4e
    xor ah, ah                                ; 30 e4                       ; 0xc3a51
    call 00dedh                               ; e8 97 d3                    ; 0xc3a53
    jmp near 03d9ah                           ; e9 41 03                    ; 0xc3a56 vgabios.c:2587
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a59 vgabios.c:2589
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3a5c
    xor ah, ah                                ; 30 e4                       ; 0xc3a5f
    mov bx, ax                                ; 89 c3                       ; 0xc3a61
    mov dl, byte [bp+00dh]                    ; 8a 56 0d                    ; 0xc3a63
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a66
    call 02607h                               ; e8 9b eb                    ; 0xc3a69
    jmp near 03d9ah                           ; e9 2b 03                    ; 0xc3a6c vgabios.c:2590
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3a6f vgabios.c:2592
    mov bl, byte [bp+00ch]                    ; 8a 5e 0c                    ; 0xc3a72
    xor bh, bh                                ; 30 ff                       ; 0xc3a75
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3a77
    xor ah, ah                                ; 30 e4                       ; 0xc3a7a
    mov dx, ax                                ; 89 c2                       ; 0xc3a7c
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3a7e
    call 02779h                               ; e8 f5 ec                    ; 0xc3a81
    jmp near 03d9ah                           ; e9 13 03                    ; 0xc3a84 vgabios.c:2593
    mov cx, word [bp+00eh]                    ; 8b 4e 0e                    ; 0xc3a87 vgabios.c:2595
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc3a8a
    mov dl, byte [bp+012h]                    ; 8a 56 12                    ; 0xc3a8d
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3a90
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc3a93
    mov byte [bp-005h], dh                    ; 88 76 fb                    ; 0xc3a96
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc3a99
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc3a9c
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc3a9f
    xor ah, ah                                ; 30 e4                       ; 0xc3aa2
    call 028fch                               ; e8 55 ee                    ; 0xc3aa4
    jmp near 03d9ah                           ; e9 f0 02                    ; 0xc3aa7 vgabios.c:2596
    lea cx, [bp+012h]                         ; 8d 4e 12                    ; 0xc3aaa vgabios.c:2598
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3aad
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3ab0
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3ab3
    xor ah, ah                                ; 30 e4                       ; 0xc3ab6
    call 00fc7h                               ; e8 0c d5                    ; 0xc3ab8
    jmp near 03d9ah                           ; e9 dc 02                    ; 0xc3abb vgabios.c:2599
    mov cx, strict word 00002h                ; b9 02 00                    ; 0xc3abe vgabios.c:2607
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3ac1
    xor ah, ah                                ; 30 e4                       ; 0xc3ac4
    mov bx, ax                                ; 89 c3                       ; 0xc3ac6
    mov dx, 000ffh                            ; ba ff 00                    ; 0xc3ac8
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3acb
    call 02a76h                               ; e8 a5 ef                    ; 0xc3ace
    jmp near 03d9ah                           ; e9 c6 02                    ; 0xc3ad1 vgabios.c:2608
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3ad4 vgabios.c:2611
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3ad7
    call 01134h                               ; e8 57 d6                    ; 0xc3ada
    jmp near 03d9ah                           ; e9 ba 02                    ; 0xc3add vgabios.c:2612
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3ae0 vgabios.c:2614
    xor ah, ah                                ; 30 e4                       ; 0xc3ae3
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3ae5
    jnbe short 03b58h                         ; 77 6e                       ; 0xc3ae8
    push CS                                   ; 0e                          ; 0xc3aea
    pop ES                                    ; 07                          ; 0xc3aeb
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc3aec
    mov di, 03905h                            ; bf 05 39                    ; 0xc3aef
    repne scasb                               ; f2 ae                       ; 0xc3af2
    sal cx, 1                                 ; d1 e1                       ; 0xc3af4
    mov di, cx                                ; 89 cf                       ; 0xc3af6
    mov ax, word [cs:di+03914h]               ; 2e 8b 85 14 39              ; 0xc3af8
    jmp ax                                    ; ff e0                       ; 0xc3afd
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3aff vgabios.c:2618
    xor ah, ah                                ; 30 e4                       ; 0xc3b02
    push ax                                   ; 50                          ; 0xc3b04
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b05
    push ax                                   ; 50                          ; 0xc3b08
    push word [bp+00eh]                       ; ff 76 0e                    ; 0xc3b09
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b0c
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3b0f
    mov bx, word [bp+008h]                    ; 8b 5e 08                    ; 0xc3b12
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3b15
    jmp short 03b33h                          ; eb 19                       ; 0xc3b18
    mov ax, strict word 0000eh                ; b8 0e 00                    ; 0xc3b1a vgabios.c:2622
    push ax                                   ; 50                          ; 0xc3b1d
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b1e
    xor ah, ah                                ; 30 e4                       ; 0xc3b21
    push ax                                   ; 50                          ; 0xc3b23
    xor al, al                                ; 30 c0                       ; 0xc3b24
    push ax                                   ; 50                          ; 0xc3b26
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b27
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b2a
    mov bx, 05d6fh                            ; bb 6f 5d                    ; 0xc3b2d
    mov dx, 0c000h                            ; ba 00 c0                    ; 0xc3b30
    call 02e8ch                               ; e8 56 f3                    ; 0xc3b33
    jmp short 03b58h                          ; eb 20                       ; 0xc3b36
    mov ax, strict word 00008h                ; b8 08 00                    ; 0xc3b38 vgabios.c:2626
    push ax                                   ; 50                          ; 0xc3b3b
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b3c
    xor ah, ah                                ; 30 e4                       ; 0xc3b3f
    push ax                                   ; 50                          ; 0xc3b41
    xor al, al                                ; 30 c0                       ; 0xc3b42
    push ax                                   ; 50                          ; 0xc3b44
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b45
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b48
    mov bx, 0556fh                            ; bb 6f 55                    ; 0xc3b4b
    jmp short 03b30h                          ; eb e0                       ; 0xc3b4e
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b50 vgabios.c:2629
    xor ah, ah                                ; 30 e4                       ; 0xc3b53
    call 02df4h                               ; e8 9c f2                    ; 0xc3b55
    jmp near 03d9ah                           ; e9 3f 02                    ; 0xc3b58 vgabios.c:2630
    mov ax, strict word 00010h                ; b8 10 00                    ; 0xc3b5b vgabios.c:2633
    push ax                                   ; 50                          ; 0xc3b5e
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b5f
    xor ah, ah                                ; 30 e4                       ; 0xc3b62
    push ax                                   ; 50                          ; 0xc3b64
    xor al, al                                ; 30 c0                       ; 0xc3b65
    push ax                                   ; 50                          ; 0xc3b67
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3b68
    mov cx, 00100h                            ; b9 00 01                    ; 0xc3b6b
    mov bx, 06b6fh                            ; bb 6f 6b                    ; 0xc3b6e
    jmp short 03b30h                          ; eb bd                       ; 0xc3b71
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc3b73 vgabios.c:2636
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc3b76
    call 02f10h                               ; e8 94 f3                    ; 0xc3b79
    jmp short 03b58h                          ; eb da                       ; 0xc3b7c vgabios.c:2637
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b7e vgabios.c:2639
    xor ah, ah                                ; 30 e4                       ; 0xc3b81
    push ax                                   ; 50                          ; 0xc3b83
    mov cl, byte [bp+00ch]                    ; 8a 4e 0c                    ; 0xc3b84
    xor ch, ch                                ; 30 ed                       ; 0xc3b87
    mov bx, word [bp+010h]                    ; 8b 5e 10                    ; 0xc3b89
    mov dx, word [bp+008h]                    ; 8b 56 08                    ; 0xc3b8c
    mov ax, word [bp+016h]                    ; 8b 46 16                    ; 0xc3b8f
    call 02f73h                               ; e8 de f3                    ; 0xc3b92
    jmp short 03b58h                          ; eb c1                       ; 0xc3b95 vgabios.c:2640
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3b97 vgabios.c:2642
    xor ah, ah                                ; 30 e4                       ; 0xc3b9a
    mov dx, ax                                ; 89 c2                       ; 0xc3b9c
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3b9e
    call 02f90h                               ; e8 ec f3                    ; 0xc3ba1
    jmp short 03b58h                          ; eb b2                       ; 0xc3ba4 vgabios.c:2643
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3ba6 vgabios.c:2645
    xor ah, ah                                ; 30 e4                       ; 0xc3ba9
    mov dx, ax                                ; 89 c2                       ; 0xc3bab
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3bad
    call 02fb2h                               ; e8 ff f3                    ; 0xc3bb0
    jmp short 03b58h                          ; eb a3                       ; 0xc3bb3 vgabios.c:2646
    mov dl, byte [bp+00eh]                    ; 8a 56 0e                    ; 0xc3bb5 vgabios.c:2648
    mov al, byte [bp+00ch]                    ; 8a 46 0c                    ; 0xc3bb8
    xor ah, ah                                ; 30 e4                       ; 0xc3bbb
    call 02fd4h                               ; e8 14 f4                    ; 0xc3bbd
    jmp short 03b58h                          ; eb 96                       ; 0xc3bc0 vgabios.c:2649
    lea ax, [bp+00eh]                         ; 8d 46 0e                    ; 0xc3bc2 vgabios.c:2651
    push ax                                   ; 50                          ; 0xc3bc5
    lea cx, [bp+010h]                         ; 8d 4e 10                    ; 0xc3bc6
    lea bx, [bp+008h]                         ; 8d 5e 08                    ; 0xc3bc9
    lea dx, [bp+016h]                         ; 8d 56 16                    ; 0xc3bcc
    mov al, byte [bp+00dh]                    ; 8a 46 0d                    ; 0xc3bcf
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc3bd2
    mov byte [bp-005h], 000h                  ; c6 46 fb 00                 ; 0xc3bd5
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc3bd9
    call 00f44h                               ; e8 65 d3                    ; 0xc3bdc
    jmp near 03d9ah                           ; e9 b8 01                    ; 0xc3bdf vgabios.c:2659
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3be2 vgabios.c:2661
    xor ah, ah                                ; 30 e4                       ; 0xc3be5
    cmp ax, strict word 00034h                ; 3d 34 00                    ; 0xc3be7
    jc short 03bfbh                           ; 72 0f                       ; 0xc3bea
    jbe short 03c26h                          ; 76 38                       ; 0xc3bec
    cmp ax, strict word 00036h                ; 3d 36 00                    ; 0xc3bee
    je short 03c50h                           ; 74 5d                       ; 0xc3bf1
    cmp ax, strict word 00035h                ; 3d 35 00                    ; 0xc3bf3
    je short 03c52h                           ; 74 5a                       ; 0xc3bf6
    jmp near 03d9ah                           ; e9 9f 01                    ; 0xc3bf8
    cmp ax, strict word 00030h                ; 3d 30 00                    ; 0xc3bfb
    je short 03c0ah                           ; 74 0a                       ; 0xc3bfe
    cmp ax, strict word 00020h                ; 3d 20 00                    ; 0xc3c00
    jne short 03c4dh                          ; 75 48                       ; 0xc3c03
    call 02ff6h                               ; e8 ee f3                    ; 0xc3c05 vgabios.c:2664
    jmp short 03c4dh                          ; eb 43                       ; 0xc3c08 vgabios.c:2665
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c0a vgabios.c:2667
    xor ah, ah                                ; 30 e4                       ; 0xc3c0d
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3c0f
    jnbe short 03c4dh                         ; 77 39                       ; 0xc3c12
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c14 vgabios.c:2668
    call 02ffbh                               ; e8 e1 f3                    ; 0xc3c17
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c1a vgabios.c:2669
    xor al, al                                ; 30 c0                       ; 0xc3c1d
    or AL, strict byte 012h                   ; 0c 12                       ; 0xc3c1f
    mov word [bp+012h], ax                    ; 89 46 12                    ; 0xc3c21
    jmp short 03c4dh                          ; eb 27                       ; 0xc3c24 vgabios.c:2671
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c26 vgabios.c:2673
    xor ah, ah                                ; 30 e4                       ; 0xc3c29
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3c2b
    jnc short 03c4ah                          ; 73 1a                       ; 0xc3c2e
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3c30 vgabios.c:45
    mov es, ax                                ; 8e c0                       ; 0xc3c33
    mov bx, 00087h                            ; bb 87 00                    ; 0xc3c35
    mov ah, byte [es:bx]                      ; 26 8a 27                    ; 0xc3c38 vgabios.c:47
    and ah, 0feh                              ; 80 e4 fe                    ; 0xc3c3b vgabios.c:48
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c3e
    or al, ah                                 ; 08 e0                       ; 0xc3c41
    mov si, bx                                ; 89 de                       ; 0xc3c43 vgabios.c:50
    mov byte [es:si], al                      ; 26 88 04                    ; 0xc3c45 vgabios.c:52
    jmp short 03c1ah                          ; eb d0                       ; 0xc3c48
    mov byte [bp+012h], ah                    ; 88 66 12                    ; 0xc3c4a vgabios.c:2679
    jmp near 03d9ah                           ; e9 4a 01                    ; 0xc3c4d vgabios.c:2680
    jmp short 03c60h                          ; eb 0e                       ; 0xc3c50
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c52 vgabios.c:2682
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3c55
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3c58
    call 0302dh                               ; e8 cf f3                    ; 0xc3c5b
    jmp short 03c1ah                          ; eb ba                       ; 0xc3c5e
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c60 vgabios.c:2686
    call 03032h                               ; e8 cc f3                    ; 0xc3c63
    jmp short 03c1ah                          ; eb b2                       ; 0xc3c66
    push word [bp+008h]                       ; ff 76 08                    ; 0xc3c68 vgabios.c:2696
    push word [bp+016h]                       ; ff 76 16                    ; 0xc3c6b
    mov al, byte [bp+00eh]                    ; 8a 46 0e                    ; 0xc3c6e
    xor ah, ah                                ; 30 e4                       ; 0xc3c71
    push ax                                   ; 50                          ; 0xc3c73
    mov al, byte [bp+00fh]                    ; 8a 46 0f                    ; 0xc3c74
    push ax                                   ; 50                          ; 0xc3c77
    mov bl, byte [bp+00ch]                    ; 8a 5e 0c                    ; 0xc3c78
    xor bh, bh                                ; 30 ff                       ; 0xc3c7b
    mov dl, byte [bp+00dh]                    ; 8a 56 0d                    ; 0xc3c7d
    mov al, byte [bp+012h]                    ; 8a 46 12                    ; 0xc3c80
    mov cx, word [bp+010h]                    ; 8b 4e 10                    ; 0xc3c83
    call 03037h                               ; e8 ae f3                    ; 0xc3c86
    jmp short 03c4dh                          ; eb c2                       ; 0xc3c89 vgabios.c:2697
    mov bx, si                                ; 89 f3                       ; 0xc3c8b vgabios.c:2699
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3c8d
    mov ax, word [bp+00ch]                    ; 8b 46 0c                    ; 0xc3c90
    call 030c6h                               ; e8 30 f4                    ; 0xc3c93
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c96 vgabios.c:2700
    xor al, al                                ; 30 c0                       ; 0xc3c99
    or AL, strict byte 01bh                   ; 0c 1b                       ; 0xc3c9b
    jmp short 03c21h                          ; eb 82                       ; 0xc3c9d
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3c9f vgabios.c:2703
    xor ah, ah                                ; 30 e4                       ; 0xc3ca2
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc3ca4
    je short 03ccbh                           ; 74 22                       ; 0xc3ca7
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc3ca9
    je short 03cbdh                           ; 74 0f                       ; 0xc3cac
    test ax, ax                               ; 85 c0                       ; 0xc3cae
    jne short 03cd7h                          ; 75 25                       ; 0xc3cb0
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3cb2 vgabios.c:2706
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3cb5
    call 032dbh                               ; e8 20 f6                    ; 0xc3cb8
    jmp short 03cd7h                          ; eb 1a                       ; 0xc3cbb vgabios.c:2707
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3cbd vgabios.c:2709
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3cc0
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3cc3
    call 032f6h                               ; e8 2d f6                    ; 0xc3cc6
    jmp short 03cd7h                          ; eb 0c                       ; 0xc3cc9 vgabios.c:2710
    mov bx, word [bp+00ch]                    ; 8b 5e 0c                    ; 0xc3ccb vgabios.c:2712
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3cce
    mov ax, word [bp+010h]                    ; 8b 46 10                    ; 0xc3cd1
    call 035ceh                               ; e8 f7 f8                    ; 0xc3cd4
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3cd7 vgabios.c:2719
    xor al, al                                ; 30 c0                       ; 0xc3cda
    or AL, strict byte 01ch                   ; 0c 1c                       ; 0xc3cdc
    jmp near 03c21h                           ; e9 40 ff                    ; 0xc3cde
    call 007f8h                               ; e8 14 cb                    ; 0xc3ce1 vgabios.c:2724
    test ax, ax                               ; 85 c0                       ; 0xc3ce4
    je short 03d5ch                           ; 74 74                       ; 0xc3ce6
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3ce8 vgabios.c:2725
    xor ah, ah                                ; 30 e4                       ; 0xc3ceb
    cmp ax, strict word 0000ah                ; 3d 0a 00                    ; 0xc3ced
    jnbe short 03d5eh                         ; 77 6c                       ; 0xc3cf0
    push CS                                   ; 0e                          ; 0xc3cf2
    pop ES                                    ; 07                          ; 0xc3cf3
    mov cx, strict word 00008h                ; b9 08 00                    ; 0xc3cf4
    mov di, 03934h                            ; bf 34 39                    ; 0xc3cf7
    repne scasb                               ; f2 ae                       ; 0xc3cfa
    sal cx, 1                                 ; d1 e1                       ; 0xc3cfc
    mov di, cx                                ; 89 cf                       ; 0xc3cfe
    mov ax, word [cs:di+0393bh]               ; 2e 8b 85 3b 39              ; 0xc3d00
    jmp ax                                    ; ff e0                       ; 0xc3d05
    mov bx, si                                ; 89 f3                       ; 0xc3d07 vgabios.c:2728
    mov dx, word [bp+016h]                    ; 8b 56 16                    ; 0xc3d09
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d0c
    call 03f6bh                               ; e8 59 02                    ; 0xc3d0f
    jmp near 03d9ah                           ; e9 85 00                    ; 0xc3d12 vgabios.c:2729
    mov cx, si                                ; 89 f1                       ; 0xc3d15 vgabios.c:2731
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3d17
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3d1a
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d1d
    call 04096h                               ; e8 73 03                    ; 0xc3d20
    jmp near 03d9ah                           ; e9 74 00                    ; 0xc3d23 vgabios.c:2732
    mov cx, si                                ; 89 f1                       ; 0xc3d26 vgabios.c:2734
    mov bx, word [bp+016h]                    ; 8b 5e 16                    ; 0xc3d28
    mov dx, word [bp+00ch]                    ; 8b 56 0c                    ; 0xc3d2b
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d2e
    call 04135h                               ; e8 01 04                    ; 0xc3d31
    jmp short 03d9ah                          ; eb 64                       ; 0xc3d34 vgabios.c:2735
    lea ax, [bp+00ch]                         ; 8d 46 0c                    ; 0xc3d36 vgabios.c:2737
    push ax                                   ; 50                          ; 0xc3d39
    mov cx, word [bp+016h]                    ; 8b 4e 16                    ; 0xc3d3a
    mov bx, word [bp+00eh]                    ; 8b 5e 0e                    ; 0xc3d3d
    mov dx, word [bp+010h]                    ; 8b 56 10                    ; 0xc3d40
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d43
    call 042feh                               ; e8 b5 05                    ; 0xc3d46
    jmp short 03d9ah                          ; eb 4f                       ; 0xc3d49 vgabios.c:2738
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3d4b vgabios.c:2740
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3d4e
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3d51
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d54
    call 0438bh                               ; e8 31 06                    ; 0xc3d57
    jmp short 03d9ah                          ; eb 3e                       ; 0xc3d5a vgabios.c:2741
    jmp short 03d65h                          ; eb 07                       ; 0xc3d5c
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d5e vgabios.c:2763
    jmp short 03d9ah                          ; eb 35                       ; 0xc3d63 vgabios.c:2766
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d65 vgabios.c:2768
    jmp short 03d9ah                          ; eb 2e                       ; 0xc3d6a vgabios.c:2770
    call 007f8h                               ; e8 89 ca                    ; 0xc3d6c vgabios.c:2772
    test ax, ax                               ; 85 c0                       ; 0xc3d6f
    je short 03d95h                           ; 74 22                       ; 0xc3d71
    mov ax, word [bp+012h]                    ; 8b 46 12                    ; 0xc3d73 vgabios.c:2773
    xor ah, ah                                ; 30 e4                       ; 0xc3d76
    cmp ax, strict word 00042h                ; 3d 42 00                    ; 0xc3d78
    jne short 03d8eh                          ; 75 11                       ; 0xc3d7b
    lea cx, [bp+00eh]                         ; 8d 4e 0e                    ; 0xc3d7d vgabios.c:2776
    lea bx, [bp+010h]                         ; 8d 5e 10                    ; 0xc3d80
    lea dx, [bp+00ch]                         ; 8d 56 0c                    ; 0xc3d83
    lea ax, [bp+012h]                         ; 8d 46 12                    ; 0xc3d86
    call 0446dh                               ; e8 e1 06                    ; 0xc3d89
    jmp short 03d9ah                          ; eb 0c                       ; 0xc3d8c vgabios.c:2777
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d8e vgabios.c:2779
    jmp short 03d9ah                          ; eb 05                       ; 0xc3d93 vgabios.c:2782
    mov word [bp+012h], 00100h                ; c7 46 12 00 01              ; 0xc3d95 vgabios.c:2784
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3d9a vgabios.c:2794
    pop di                                    ; 5f                          ; 0xc3d9d
    pop si                                    ; 5e                          ; 0xc3d9e
    pop bp                                    ; 5d                          ; 0xc3d9f
    retn                                      ; c3                          ; 0xc3da0
  ; disGetNextSymbol 0xc3da1 LB 0x7c3 -> off=0x0 cb=000000000000001f uValue=00000000000c3da1 'dispi_set_xres'
dispi_set_xres:                              ; 0xc3da1 LB 0x1f
    push bp                                   ; 55                          ; 0xc3da1 vbe.c:100
    mov bp, sp                                ; 89 e5                       ; 0xc3da2
    push bx                                   ; 53                          ; 0xc3da4
    push dx                                   ; 52                          ; 0xc3da5
    mov bx, ax                                ; 89 c3                       ; 0xc3da6
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc3da8 vbe.c:105
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3dab
    call 005a0h                               ; e8 ef c7                    ; 0xc3dae
    mov ax, bx                                ; 89 d8                       ; 0xc3db1 vbe.c:106
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3db3
    call 005a0h                               ; e8 e7 c7                    ; 0xc3db6
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3db9 vbe.c:107
    pop dx                                    ; 5a                          ; 0xc3dbc
    pop bx                                    ; 5b                          ; 0xc3dbd
    pop bp                                    ; 5d                          ; 0xc3dbe
    retn                                      ; c3                          ; 0xc3dbf
  ; disGetNextSymbol 0xc3dc0 LB 0x7a4 -> off=0x0 cb=000000000000001f uValue=00000000000c3dc0 'dispi_set_yres'
dispi_set_yres:                              ; 0xc3dc0 LB 0x1f
    push bp                                   ; 55                          ; 0xc3dc0 vbe.c:109
    mov bp, sp                                ; 89 e5                       ; 0xc3dc1
    push bx                                   ; 53                          ; 0xc3dc3
    push dx                                   ; 52                          ; 0xc3dc4
    mov bx, ax                                ; 89 c3                       ; 0xc3dc5
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3dc7 vbe.c:114
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3dca
    call 005a0h                               ; e8 d0 c7                    ; 0xc3dcd
    mov ax, bx                                ; 89 d8                       ; 0xc3dd0 vbe.c:115
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3dd2
    call 005a0h                               ; e8 c8 c7                    ; 0xc3dd5
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3dd8 vbe.c:116
    pop dx                                    ; 5a                          ; 0xc3ddb
    pop bx                                    ; 5b                          ; 0xc3ddc
    pop bp                                    ; 5d                          ; 0xc3ddd
    retn                                      ; c3                          ; 0xc3dde
  ; disGetNextSymbol 0xc3ddf LB 0x785 -> off=0x0 cb=0000000000000019 uValue=00000000000c3ddf 'dispi_get_yres'
dispi_get_yres:                              ; 0xc3ddf LB 0x19
    push bp                                   ; 55                          ; 0xc3ddf vbe.c:118
    mov bp, sp                                ; 89 e5                       ; 0xc3de0
    push dx                                   ; 52                          ; 0xc3de2
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc3de3 vbe.c:120
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3de6
    call 005a0h                               ; e8 b4 c7                    ; 0xc3de9
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3dec vbe.c:121
    call 005a7h                               ; e8 b5 c7                    ; 0xc3def
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3df2 vbe.c:122
    pop dx                                    ; 5a                          ; 0xc3df5
    pop bp                                    ; 5d                          ; 0xc3df6
    retn                                      ; c3                          ; 0xc3df7
  ; disGetNextSymbol 0xc3df8 LB 0x76c -> off=0x0 cb=000000000000001f uValue=00000000000c3df8 'dispi_set_bpp'
dispi_set_bpp:                               ; 0xc3df8 LB 0x1f
    push bp                                   ; 55                          ; 0xc3df8 vbe.c:124
    mov bp, sp                                ; 89 e5                       ; 0xc3df9
    push bx                                   ; 53                          ; 0xc3dfb
    push dx                                   ; 52                          ; 0xc3dfc
    mov bx, ax                                ; 89 c3                       ; 0xc3dfd
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3dff vbe.c:129
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e02
    call 005a0h                               ; e8 98 c7                    ; 0xc3e05
    mov ax, bx                                ; 89 d8                       ; 0xc3e08 vbe.c:130
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e0a
    call 005a0h                               ; e8 90 c7                    ; 0xc3e0d
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3e10 vbe.c:131
    pop dx                                    ; 5a                          ; 0xc3e13
    pop bx                                    ; 5b                          ; 0xc3e14
    pop bp                                    ; 5d                          ; 0xc3e15
    retn                                      ; c3                          ; 0xc3e16
  ; disGetNextSymbol 0xc3e17 LB 0x74d -> off=0x0 cb=0000000000000019 uValue=00000000000c3e17 'dispi_get_bpp'
dispi_get_bpp:                               ; 0xc3e17 LB 0x19
    push bp                                   ; 55                          ; 0xc3e17 vbe.c:133
    mov bp, sp                                ; 89 e5                       ; 0xc3e18
    push dx                                   ; 52                          ; 0xc3e1a
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc3e1b vbe.c:135
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e1e
    call 005a0h                               ; e8 7c c7                    ; 0xc3e21
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e24 vbe.c:136
    call 005a7h                               ; e8 7d c7                    ; 0xc3e27
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e2a vbe.c:137
    pop dx                                    ; 5a                          ; 0xc3e2d
    pop bp                                    ; 5d                          ; 0xc3e2e
    retn                                      ; c3                          ; 0xc3e2f
  ; disGetNextSymbol 0xc3e30 LB 0x734 -> off=0x0 cb=000000000000001f uValue=00000000000c3e30 'dispi_set_virt_width'
dispi_set_virt_width:                        ; 0xc3e30 LB 0x1f
    push bp                                   ; 55                          ; 0xc3e30 vbe.c:139
    mov bp, sp                                ; 89 e5                       ; 0xc3e31
    push bx                                   ; 53                          ; 0xc3e33
    push dx                                   ; 52                          ; 0xc3e34
    mov bx, ax                                ; 89 c3                       ; 0xc3e35
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3e37 vbe.c:144
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e3a
    call 005a0h                               ; e8 60 c7                    ; 0xc3e3d
    mov ax, bx                                ; 89 d8                       ; 0xc3e40 vbe.c:145
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e42
    call 005a0h                               ; e8 58 c7                    ; 0xc3e45
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3e48 vbe.c:146
    pop dx                                    ; 5a                          ; 0xc3e4b
    pop bx                                    ; 5b                          ; 0xc3e4c
    pop bp                                    ; 5d                          ; 0xc3e4d
    retn                                      ; c3                          ; 0xc3e4e
  ; disGetNextSymbol 0xc3e4f LB 0x715 -> off=0x0 cb=0000000000000019 uValue=00000000000c3e4f 'dispi_get_virt_width'
dispi_get_virt_width:                        ; 0xc3e4f LB 0x19
    push bp                                   ; 55                          ; 0xc3e4f vbe.c:148
    mov bp, sp                                ; 89 e5                       ; 0xc3e50
    push dx                                   ; 52                          ; 0xc3e52
    mov ax, strict word 00006h                ; b8 06 00                    ; 0xc3e53 vbe.c:150
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e56
    call 005a0h                               ; e8 44 c7                    ; 0xc3e59
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e5c vbe.c:151
    call 005a7h                               ; e8 45 c7                    ; 0xc3e5f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e62 vbe.c:152
    pop dx                                    ; 5a                          ; 0xc3e65
    pop bp                                    ; 5d                          ; 0xc3e66
    retn                                      ; c3                          ; 0xc3e67
  ; disGetNextSymbol 0xc3e68 LB 0x6fc -> off=0x0 cb=0000000000000019 uValue=00000000000c3e68 'dispi_get_virt_height'
dispi_get_virt_height:                       ; 0xc3e68 LB 0x19
    push bp                                   ; 55                          ; 0xc3e68 vbe.c:154
    mov bp, sp                                ; 89 e5                       ; 0xc3e69
    push dx                                   ; 52                          ; 0xc3e6b
    mov ax, strict word 00007h                ; b8 07 00                    ; 0xc3e6c vbe.c:156
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3e6f
    call 005a0h                               ; e8 2b c7                    ; 0xc3e72
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3e75 vbe.c:157
    call 005a7h                               ; e8 2c c7                    ; 0xc3e78
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e7b vbe.c:158
    pop dx                                    ; 5a                          ; 0xc3e7e
    pop bp                                    ; 5d                          ; 0xc3e7f
    retn                                      ; c3                          ; 0xc3e80
  ; disGetNextSymbol 0xc3e81 LB 0x6e3 -> off=0x0 cb=0000000000000012 uValue=00000000000c3e81 'in_word'
in_word:                                     ; 0xc3e81 LB 0x12
    push bp                                   ; 55                          ; 0xc3e81 vbe.c:160
    mov bp, sp                                ; 89 e5                       ; 0xc3e82
    push bx                                   ; 53                          ; 0xc3e84
    mov bx, ax                                ; 89 c3                       ; 0xc3e85
    mov ax, dx                                ; 89 d0                       ; 0xc3e87
    mov dx, bx                                ; 89 da                       ; 0xc3e89 vbe.c:162
    out DX, ax                                ; ef                          ; 0xc3e8b
    in ax, DX                                 ; ed                          ; 0xc3e8c vbe.c:163
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3e8d vbe.c:164
    pop bx                                    ; 5b                          ; 0xc3e90
    pop bp                                    ; 5d                          ; 0xc3e91
    retn                                      ; c3                          ; 0xc3e92
  ; disGetNextSymbol 0xc3e93 LB 0x6d1 -> off=0x0 cb=0000000000000014 uValue=00000000000c3e93 'in_byte'
in_byte:                                     ; 0xc3e93 LB 0x14
    push bp                                   ; 55                          ; 0xc3e93 vbe.c:166
    mov bp, sp                                ; 89 e5                       ; 0xc3e94
    push bx                                   ; 53                          ; 0xc3e96
    mov bx, ax                                ; 89 c3                       ; 0xc3e97
    mov ax, dx                                ; 89 d0                       ; 0xc3e99
    mov dx, bx                                ; 89 da                       ; 0xc3e9b vbe.c:168
    out DX, ax                                ; ef                          ; 0xc3e9d
    in AL, DX                                 ; ec                          ; 0xc3e9e vbe.c:169
    db  02ah, 0e4h
    ; sub ah, ah                                ; 2a e4                     ; 0xc3e9f
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3ea1 vbe.c:170
    pop bx                                    ; 5b                          ; 0xc3ea4
    pop bp                                    ; 5d                          ; 0xc3ea5
    retn                                      ; c3                          ; 0xc3ea6
  ; disGetNextSymbol 0xc3ea7 LB 0x6bd -> off=0x0 cb=0000000000000014 uValue=00000000000c3ea7 'dispi_get_id'
dispi_get_id:                                ; 0xc3ea7 LB 0x14
    push bp                                   ; 55                          ; 0xc3ea7 vbe.c:173
    mov bp, sp                                ; 89 e5                       ; 0xc3ea8
    push dx                                   ; 52                          ; 0xc3eaa
    xor ax, ax                                ; 31 c0                       ; 0xc3eab vbe.c:175
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3ead
    out DX, ax                                ; ef                          ; 0xc3eb0
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3eb1 vbe.c:176
    in ax, DX                                 ; ed                          ; 0xc3eb4
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3eb5 vbe.c:177
    pop dx                                    ; 5a                          ; 0xc3eb8
    pop bp                                    ; 5d                          ; 0xc3eb9
    retn                                      ; c3                          ; 0xc3eba
  ; disGetNextSymbol 0xc3ebb LB 0x6a9 -> off=0x0 cb=000000000000001a uValue=00000000000c3ebb 'dispi_set_id'
dispi_set_id:                                ; 0xc3ebb LB 0x1a
    push bp                                   ; 55                          ; 0xc3ebb vbe.c:179
    mov bp, sp                                ; 89 e5                       ; 0xc3ebc
    push bx                                   ; 53                          ; 0xc3ebe
    push dx                                   ; 52                          ; 0xc3ebf
    mov bx, ax                                ; 89 c3                       ; 0xc3ec0
    xor ax, ax                                ; 31 c0                       ; 0xc3ec2 vbe.c:181
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3ec4
    out DX, ax                                ; ef                          ; 0xc3ec7
    mov ax, bx                                ; 89 d8                       ; 0xc3ec8 vbe.c:182
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3eca
    out DX, ax                                ; ef                          ; 0xc3ecd
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc3ece vbe.c:183
    pop dx                                    ; 5a                          ; 0xc3ed1
    pop bx                                    ; 5b                          ; 0xc3ed2
    pop bp                                    ; 5d                          ; 0xc3ed3
    retn                                      ; c3                          ; 0xc3ed4
  ; disGetNextSymbol 0xc3ed5 LB 0x68f -> off=0x0 cb=000000000000002a uValue=00000000000c3ed5 'vbe_init'
vbe_init:                                    ; 0xc3ed5 LB 0x2a
    push bp                                   ; 55                          ; 0xc3ed5 vbe.c:188
    mov bp, sp                                ; 89 e5                       ; 0xc3ed6
    push bx                                   ; 53                          ; 0xc3ed8
    mov ax, 0b0c0h                            ; b8 c0 b0                    ; 0xc3ed9 vbe.c:190
    call 03ebbh                               ; e8 dc ff                    ; 0xc3edc
    call 03ea7h                               ; e8 c5 ff                    ; 0xc3edf vbe.c:191
    cmp ax, 0b0c0h                            ; 3d c0 b0                    ; 0xc3ee2
    jne short 03ef9h                          ; 75 12                       ; 0xc3ee5
    mov bx, 000b9h                            ; bb b9 00                    ; 0xc3ee7 vbe.c:52
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc3eea
    mov es, ax                                ; 8e c0                       ; 0xc3eed
    mov byte [es:bx], 001h                    ; 26 c6 07 01                 ; 0xc3eef
    mov ax, 0b0c4h                            ; b8 c4 b0                    ; 0xc3ef3 vbe.c:194
    call 03ebbh                               ; e8 c2 ff                    ; 0xc3ef6
    lea sp, [bp-002h]                         ; 8d 66 fe                    ; 0xc3ef9 vbe.c:199
    pop bx                                    ; 5b                          ; 0xc3efc
    pop bp                                    ; 5d                          ; 0xc3efd
    retn                                      ; c3                          ; 0xc3efe
  ; disGetNextSymbol 0xc3eff LB 0x665 -> off=0x0 cb=000000000000006c uValue=00000000000c3eff 'mode_info_find_mode'
mode_info_find_mode:                         ; 0xc3eff LB 0x6c
    push bp                                   ; 55                          ; 0xc3eff vbe.c:202
    mov bp, sp                                ; 89 e5                       ; 0xc3f00
    push bx                                   ; 53                          ; 0xc3f02
    push cx                                   ; 51                          ; 0xc3f03
    push si                                   ; 56                          ; 0xc3f04
    push di                                   ; 57                          ; 0xc3f05
    mov di, ax                                ; 89 c7                       ; 0xc3f06
    mov si, dx                                ; 89 d6                       ; 0xc3f08
    xor dx, dx                                ; 31 d2                       ; 0xc3f0a vbe.c:208
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f0c
    call 03e81h                               ; e8 6f ff                    ; 0xc3f0f
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3f12 vbe.c:209
    jne short 03f60h                          ; 75 49                       ; 0xc3f15
    test si, si                               ; 85 f6                       ; 0xc3f17 vbe.c:213
    je short 03f2eh                           ; 74 13                       ; 0xc3f19
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc3f1b vbe.c:220
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc3f1e
    call 005a0h                               ; e8 7c c6                    ; 0xc3f21
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc3f24 vbe.c:221
    call 005a7h                               ; e8 7d c6                    ; 0xc3f27
    test ax, ax                               ; 85 c0                       ; 0xc3f2a vbe.c:222
    je short 03f62h                           ; 74 34                       ; 0xc3f2c
    mov bx, strict word 00004h                ; bb 04 00                    ; 0xc3f2e vbe.c:226
    mov dx, bx                                ; 89 da                       ; 0xc3f31 vbe.c:232
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f33
    call 03e81h                               ; e8 48 ff                    ; 0xc3f36
    mov cx, ax                                ; 89 c1                       ; 0xc3f39
    cmp cx, strict byte 0ffffh                ; 83 f9 ff                    ; 0xc3f3b vbe.c:233
    je short 03f60h                           ; 74 20                       ; 0xc3f3e
    lea dx, [bx+002h]                         ; 8d 57 02                    ; 0xc3f40 vbe.c:235
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f43
    call 03e81h                               ; e8 38 ff                    ; 0xc3f46
    lea dx, [bx+044h]                         ; 8d 57 44                    ; 0xc3f49
    cmp cx, di                                ; 39 f9                       ; 0xc3f4c vbe.c:237
    jne short 03f5ch                          ; 75 0c                       ; 0xc3f4e
    test si, si                               ; 85 f6                       ; 0xc3f50 vbe.c:239
    jne short 03f58h                          ; 75 04                       ; 0xc3f52
    mov ax, bx                                ; 89 d8                       ; 0xc3f54 vbe.c:240
    jmp short 03f62h                          ; eb 0a                       ; 0xc3f56
    test AL, strict byte 080h                 ; a8 80                       ; 0xc3f58 vbe.c:241
    jne short 03f54h                          ; 75 f8                       ; 0xc3f5a
    mov bx, dx                                ; 89 d3                       ; 0xc3f5c vbe.c:244
    jmp short 03f33h                          ; eb d3                       ; 0xc3f5e vbe.c:249
    xor ax, ax                                ; 31 c0                       ; 0xc3f60 vbe.c:252
    lea sp, [bp-008h]                         ; 8d 66 f8                    ; 0xc3f62 vbe.c:253
    pop di                                    ; 5f                          ; 0xc3f65
    pop si                                    ; 5e                          ; 0xc3f66
    pop cx                                    ; 59                          ; 0xc3f67
    pop bx                                    ; 5b                          ; 0xc3f68
    pop bp                                    ; 5d                          ; 0xc3f69
    retn                                      ; c3                          ; 0xc3f6a
  ; disGetNextSymbol 0xc3f6b LB 0x5f9 -> off=0x0 cb=000000000000012b uValue=00000000000c3f6b 'vbe_biosfn_return_controller_information'
vbe_biosfn_return_controller_information: ; 0xc3f6b LB 0x12b
    push bp                                   ; 55                          ; 0xc3f6b vbe.c:284
    mov bp, sp                                ; 89 e5                       ; 0xc3f6c
    push cx                                   ; 51                          ; 0xc3f6e
    push si                                   ; 56                          ; 0xc3f6f
    push di                                   ; 57                          ; 0xc3f70
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc3f71
    mov si, ax                                ; 89 c6                       ; 0xc3f74
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc3f76
    mov di, bx                                ; 89 df                       ; 0xc3f79
    mov word [bp-00ch], strict word 00022h    ; c7 46 f4 22 00              ; 0xc3f7b vbe.c:289
    call 005eah                               ; e8 67 c6                    ; 0xc3f80 vbe.c:292
    mov word [bp-010h], ax                    ; 89 46 f0                    ; 0xc3f83
    mov bx, di                                ; 89 fb                       ; 0xc3f86 vbe.c:295
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3f88
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc3f8b
    xor dx, dx                                ; 31 d2                       ; 0xc3f8e vbe.c:298
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc3f90
    call 03e81h                               ; e8 eb fe                    ; 0xc3f93
    cmp ax, 077cch                            ; 3d cc 77                    ; 0xc3f96 vbe.c:299
    je short 03fa5h                           ; 74 0a                       ; 0xc3f99
    push SS                                   ; 16                          ; 0xc3f9b vbe.c:301
    pop ES                                    ; 07                          ; 0xc3f9c
    mov word [es:si], 00100h                  ; 26 c7 04 00 01              ; 0xc3f9d
    jmp near 0408eh                           ; e9 e9 00                    ; 0xc3fa2 vbe.c:305
    mov cx, strict word 00004h                ; b9 04 00                    ; 0xc3fa5 vbe.c:307
    mov word [bp-00eh], strict word 00000h    ; c7 46 f2 00 00              ; 0xc3fa8 vbe.c:314
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3fad vbe.c:322
    cmp word [es:bx+002h], 03245h             ; 26 81 7f 02 45 32           ; 0xc3fb0
    jne short 03fbfh                          ; 75 07                       ; 0xc3fb6
    cmp word [es:bx], 04256h                  ; 26 81 3f 56 42              ; 0xc3fb8
    je short 03fceh                           ; 74 0f                       ; 0xc3fbd
    cmp word [es:bx+002h], 04153h             ; 26 81 7f 02 53 41           ; 0xc3fbf
    jne short 03fd3h                          ; 75 0c                       ; 0xc3fc5
    cmp word [es:bx], 04556h                  ; 26 81 3f 56 45              ; 0xc3fc7
    jne short 03fd3h                          ; 75 05                       ; 0xc3fcc
    mov word [bp-00eh], strict word 00001h    ; c7 46 f2 01 00              ; 0xc3fce vbe.c:324
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc3fd3 vbe.c:332
    mov word [es:bx], 04556h                  ; 26 c7 07 56 45              ; 0xc3fd6
    mov word [es:bx+002h], 04153h             ; 26 c7 47 02 53 41           ; 0xc3fdb vbe.c:334
    mov word [es:bx+004h], 00200h             ; 26 c7 47 04 00 02           ; 0xc3fe1 vbe.c:338
    mov word [es:bx+006h], 07e04h             ; 26 c7 47 06 04 7e           ; 0xc3fe7 vbe.c:341
    mov [es:bx+008h], ds                      ; 26 8c 5f 08                 ; 0xc3fed
    mov word [es:bx+00ah], strict word 00001h ; 26 c7 47 0a 01 00           ; 0xc3ff1 vbe.c:344
    mov word [es:bx+00ch], strict word 00000h ; 26 c7 47 0c 00 00           ; 0xc3ff7 vbe.c:346
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc3ffd vbe.c:350
    mov word [es:bx+010h], ax                 ; 26 89 47 10                 ; 0xc4000
    lea ax, [di+022h]                         ; 8d 45 22                    ; 0xc4004 vbe.c:351
    mov word [es:bx+00eh], ax                 ; 26 89 47 0e                 ; 0xc4007
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc400b vbe.c:354
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc400e
    call 03e81h                               ; e8 6d fe                    ; 0xc4011
    mov es, [bp-008h]                         ; 8e 46 f8                    ; 0xc4014
    mov word [es:bx+012h], ax                 ; 26 89 47 12                 ; 0xc4017
    cmp word [bp-00eh], strict byte 00000h    ; 83 7e f2 00                 ; 0xc401b vbe.c:356
    je short 04045h                           ; 74 24                       ; 0xc401f
    mov word [es:bx+014h], strict word 00003h ; 26 c7 47 14 03 00           ; 0xc4021 vbe.c:359
    mov word [es:bx+016h], 07e19h             ; 26 c7 47 16 19 7e           ; 0xc4027 vbe.c:360
    mov [es:bx+018h], ds                      ; 26 8c 5f 18                 ; 0xc402d
    mov word [es:bx+01ah], 07e36h             ; 26 c7 47 1a 36 7e           ; 0xc4031 vbe.c:361
    mov [es:bx+01ch], ds                      ; 26 8c 5f 1c                 ; 0xc4037
    mov word [es:bx+01eh], 07e54h             ; 26 c7 47 1e 54 7e           ; 0xc403b vbe.c:362
    mov [es:bx+020h], ds                      ; 26 8c 5f 20                 ; 0xc4041
    mov dx, cx                                ; 89 ca                       ; 0xc4045 vbe.c:369
    add dx, strict byte 0001bh                ; 83 c2 1b                    ; 0xc4047
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc404a
    call 03e93h                               ; e8 43 fe                    ; 0xc404d
    xor ah, ah                                ; 30 e4                       ; 0xc4050 vbe.c:370
    cmp ax, word [bp-010h]                    ; 3b 46 f0                    ; 0xc4052
    jnbe short 0406eh                         ; 77 17                       ; 0xc4055
    mov dx, cx                                ; 89 ca                       ; 0xc4057 vbe.c:372
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4059
    call 03e81h                               ; e8 22 fe                    ; 0xc405c
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc405f vbe.c:376
    add bx, di                                ; 01 fb                       ; 0xc4062
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc4064 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4067
    add word [bp-00ch], strict byte 00002h    ; 83 46 f4 02                 ; 0xc406a vbe.c:378
    add cx, strict byte 00044h                ; 83 c1 44                    ; 0xc406e vbe.c:380
    mov dx, cx                                ; 89 ca                       ; 0xc4071 vbe.c:381
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4073
    call 03e81h                               ; e8 08 fe                    ; 0xc4076
    cmp ax, strict word 0ffffh                ; 3d ff ff                    ; 0xc4079 vbe.c:382
    jne short 04045h                          ; 75 c7                       ; 0xc407c
    add di, word [bp-00ch]                    ; 03 7e f4                    ; 0xc407e vbe.c:385
    mov es, [bp-00ah]                         ; 8e 46 f6                    ; 0xc4081 vbe.c:62
    mov word [es:di], ax                      ; 26 89 05                    ; 0xc4084
    push SS                                   ; 16                          ; 0xc4087 vbe.c:386
    pop ES                                    ; 07                          ; 0xc4088
    mov word [es:si], strict word 0004fh      ; 26 c7 04 4f 00              ; 0xc4089
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc408e vbe.c:387
    pop di                                    ; 5f                          ; 0xc4091
    pop si                                    ; 5e                          ; 0xc4092
    pop cx                                    ; 59                          ; 0xc4093
    pop bp                                    ; 5d                          ; 0xc4094
    retn                                      ; c3                          ; 0xc4095
  ; disGetNextSymbol 0xc4096 LB 0x4ce -> off=0x0 cb=000000000000009f uValue=00000000000c4096 'vbe_biosfn_return_mode_information'
vbe_biosfn_return_mode_information:          ; 0xc4096 LB 0x9f
    push bp                                   ; 55                          ; 0xc4096 vbe.c:399
    mov bp, sp                                ; 89 e5                       ; 0xc4097
    push si                                   ; 56                          ; 0xc4099
    push di                                   ; 57                          ; 0xc409a
    push ax                                   ; 50                          ; 0xc409b
    push ax                                   ; 50                          ; 0xc409c
    mov ax, dx                                ; 89 d0                       ; 0xc409d
    mov si, bx                                ; 89 de                       ; 0xc409f
    mov bx, cx                                ; 89 cb                       ; 0xc40a1
    test dh, 040h                             ; f6 c6 40                    ; 0xc40a3 vbe.c:410
    je short 040adh                           ; 74 05                       ; 0xc40a6
    mov dx, strict word 00001h                ; ba 01 00                    ; 0xc40a8
    jmp short 040afh                          ; eb 02                       ; 0xc40ab
    xor dx, dx                                ; 31 d2                       ; 0xc40ad
    and ah, 001h                              ; 80 e4 01                    ; 0xc40af vbe.c:411
    call 03effh                               ; e8 4a fe                    ; 0xc40b2 vbe.c:413
    mov word [bp-006h], ax                    ; 89 46 fa                    ; 0xc40b5
    test ax, ax                               ; 85 c0                       ; 0xc40b8 vbe.c:415
    je short 04123h                           ; 74 67                       ; 0xc40ba
    mov cx, 00100h                            ; b9 00 01                    ; 0xc40bc vbe.c:420
    xor ax, ax                                ; 31 c0                       ; 0xc40bf
    mov di, bx                                ; 89 df                       ; 0xc40c1
    mov es, si                                ; 8e c6                       ; 0xc40c3
    jcxz 040c9h                               ; e3 02                       ; 0xc40c5
    rep stosb                                 ; f3 aa                       ; 0xc40c7
    xor cx, cx                                ; 31 c9                       ; 0xc40c9 vbe.c:421
    jmp short 040d2h                          ; eb 05                       ; 0xc40cb
    cmp cx, strict byte 00042h                ; 83 f9 42                    ; 0xc40cd
    jnc short 040ebh                          ; 73 19                       ; 0xc40d0
    mov dx, word [bp-006h]                    ; 8b 56 fa                    ; 0xc40d2 vbe.c:424
    inc dx                                    ; 42                          ; 0xc40d5
    inc dx                                    ; 42                          ; 0xc40d6
    add dx, cx                                ; 01 ca                       ; 0xc40d7
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc40d9
    call 03e93h                               ; e8 b4 fd                    ; 0xc40dc
    mov di, bx                                ; 89 df                       ; 0xc40df vbe.c:425
    add di, cx                                ; 01 cf                       ; 0xc40e1
    mov es, si                                ; 8e c6                       ; 0xc40e3 vbe.c:52
    mov byte [es:di], al                      ; 26 88 05                    ; 0xc40e5
    inc cx                                    ; 41                          ; 0xc40e8 vbe.c:426
    jmp short 040cdh                          ; eb e2                       ; 0xc40e9
    lea di, [bx+002h]                         ; 8d 7f 02                    ; 0xc40eb vbe.c:427
    mov es, si                                ; 8e c6                       ; 0xc40ee vbe.c:47
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc40f0
    test AL, strict byte 001h                 ; a8 01                       ; 0xc40f3 vbe.c:428
    je short 04107h                           ; 74 10                       ; 0xc40f5
    lea di, [bx+00ch]                         ; 8d 7f 0c                    ; 0xc40f7 vbe.c:429
    mov word [es:di], 0065ch                  ; 26 c7 05 5c 06              ; 0xc40fa vbe.c:62
    lea di, [bx+00eh]                         ; 8d 7f 0e                    ; 0xc40ff vbe.c:431
    mov word [es:di], 0c000h                  ; 26 c7 05 00 c0              ; 0xc4102 vbe.c:62
    mov ax, strict word 0000bh                ; b8 0b 00                    ; 0xc4107 vbe.c:434
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc410a
    call 005a0h                               ; e8 90 c4                    ; 0xc410d
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4110 vbe.c:435
    call 005a7h                               ; e8 91 c4                    ; 0xc4113
    add bx, strict byte 0002ah                ; 83 c3 2a                    ; 0xc4116
    mov es, si                                ; 8e c6                       ; 0xc4119 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc411b
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc411e vbe.c:437
    jmp short 04126h                          ; eb 03                       ; 0xc4121 vbe.c:438
    mov ax, 00100h                            ; b8 00 01                    ; 0xc4123 vbe.c:442
    push SS                                   ; 16                          ; 0xc4126 vbe.c:445
    pop ES                                    ; 07                          ; 0xc4127
    mov bx, word [bp-008h]                    ; 8b 5e f8                    ; 0xc4128
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc412b
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc412e vbe.c:446
    pop di                                    ; 5f                          ; 0xc4131
    pop si                                    ; 5e                          ; 0xc4132
    pop bp                                    ; 5d                          ; 0xc4133
    retn                                      ; c3                          ; 0xc4134
  ; disGetNextSymbol 0xc4135 LB 0x42f -> off=0x0 cb=00000000000000e7 uValue=00000000000c4135 'vbe_biosfn_set_mode'
vbe_biosfn_set_mode:                         ; 0xc4135 LB 0xe7
    push bp                                   ; 55                          ; 0xc4135 vbe.c:458
    mov bp, sp                                ; 89 e5                       ; 0xc4136
    push si                                   ; 56                          ; 0xc4138
    push di                                   ; 57                          ; 0xc4139
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc413a
    mov si, ax                                ; 89 c6                       ; 0xc413d
    mov word [bp-00ah], dx                    ; 89 56 f6                    ; 0xc413f
    test byte [bp-009h], 040h                 ; f6 46 f7 40                 ; 0xc4142 vbe.c:466
    je short 0414dh                           ; 74 05                       ; 0xc4146
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc4148
    jmp short 0414fh                          ; eb 02                       ; 0xc414b
    xor ax, ax                                ; 31 c0                       ; 0xc414d
    mov dx, ax                                ; 89 c2                       ; 0xc414f
    test ax, ax                               ; 85 c0                       ; 0xc4151 vbe.c:467
    je short 04158h                           ; 74 03                       ; 0xc4153
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc4155
    mov byte [bp-008h], al                    ; 88 46 f8                    ; 0xc4158
    test byte [bp-009h], 080h                 ; f6 46 f7 80                 ; 0xc415b vbe.c:468
    je short 04166h                           ; 74 05                       ; 0xc415f
    mov ax, 00080h                            ; b8 80 00                    ; 0xc4161
    jmp short 04168h                          ; eb 02                       ; 0xc4164
    xor ax, ax                                ; 31 c0                       ; 0xc4166
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc4168
    and byte [bp-009h], 001h                  ; 80 66 f7 01                 ; 0xc416b vbe.c:470
    cmp word [bp-00ah], 00100h                ; 81 7e f6 00 01              ; 0xc416f vbe.c:473
    jnc short 04189h                          ; 73 13                       ; 0xc4174
    xor ax, ax                                ; 31 c0                       ; 0xc4176 vbe.c:477
    call 00610h                               ; e8 95 c4                    ; 0xc4178
    mov al, byte [bp-00ah]                    ; 8a 46 f6                    ; 0xc417b vbe.c:481
    xor ah, ah                                ; 30 e4                       ; 0xc417e
    call 01479h                               ; e8 f6 d2                    ; 0xc4180
    mov ax, strict word 0004fh                ; b8 4f 00                    ; 0xc4183 vbe.c:482
    jmp near 04210h                           ; e9 87 00                    ; 0xc4186 vbe.c:483
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc4189 vbe.c:486
    call 03effh                               ; e8 70 fd                    ; 0xc418c
    mov bx, ax                                ; 89 c3                       ; 0xc418f
    test ax, ax                               ; 85 c0                       ; 0xc4191 vbe.c:488
    je short 0420dh                           ; 74 78                       ; 0xc4193
    lea dx, [bx+014h]                         ; 8d 57 14                    ; 0xc4195 vbe.c:493
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc4198
    call 03e81h                               ; e8 e3 fc                    ; 0xc419b
    mov cx, ax                                ; 89 c1                       ; 0xc419e
    lea dx, [bx+016h]                         ; 8d 57 16                    ; 0xc41a0 vbe.c:494
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc41a3
    call 03e81h                               ; e8 d8 fc                    ; 0xc41a6
    mov di, ax                                ; 89 c7                       ; 0xc41a9
    lea dx, [bx+01bh]                         ; 8d 57 1b                    ; 0xc41ab vbe.c:495
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc41ae
    call 03e93h                               ; e8 df fc                    ; 0xc41b1
    mov bl, al                                ; 88 c3                       ; 0xc41b4
    mov dl, al                                ; 88 c2                       ; 0xc41b6
    xor ax, ax                                ; 31 c0                       ; 0xc41b8 vbe.c:503
    call 00610h                               ; e8 53 c4                    ; 0xc41ba
    cmp bl, 004h                              ; 80 fb 04                    ; 0xc41bd vbe.c:505
    jne short 041c8h                          ; 75 06                       ; 0xc41c0
    mov ax, strict word 0006ah                ; b8 6a 00                    ; 0xc41c2 vbe.c:507
    call 01479h                               ; e8 b1 d2                    ; 0xc41c5
    mov al, dl                                ; 88 d0                       ; 0xc41c8 vbe.c:510
    xor ah, ah                                ; 30 e4                       ; 0xc41ca
    call 03df8h                               ; e8 29 fc                    ; 0xc41cc
    mov ax, cx                                ; 89 c8                       ; 0xc41cf vbe.c:511
    call 03da1h                               ; e8 cd fb                    ; 0xc41d1
    mov ax, di                                ; 89 f8                       ; 0xc41d4 vbe.c:512
    call 03dc0h                               ; e8 e7 fb                    ; 0xc41d6
    xor ax, ax                                ; 31 c0                       ; 0xc41d9 vbe.c:513
    call 00636h                               ; e8 58 c4                    ; 0xc41db
    mov dl, byte [bp-006h]                    ; 8a 56 fa                    ; 0xc41de vbe.c:514
    or dl, 001h                               ; 80 ca 01                    ; 0xc41e1
    mov al, byte [bp-008h]                    ; 8a 46 f8                    ; 0xc41e4
    xor ah, ah                                ; 30 e4                       ; 0xc41e7
    or al, dl                                 ; 08 d0                       ; 0xc41e9
    call 00610h                               ; e8 22 c4                    ; 0xc41eb
    call 00708h                               ; e8 17 c5                    ; 0xc41ee vbe.c:515
    mov bx, 000bah                            ; bb ba 00                    ; 0xc41f1 vbe.c:62
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc41f4
    mov es, ax                                ; 8e c0                       ; 0xc41f7
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc41f9
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc41fc
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc41ff vbe.c:518
    or AL, strict byte 060h                   ; 0c 60                       ; 0xc4202
    mov bx, 00087h                            ; bb 87 00                    ; 0xc4204 vbe.c:52
    mov byte [es:bx], al                      ; 26 88 07                    ; 0xc4207
    jmp near 04183h                           ; e9 76 ff                    ; 0xc420a
    mov ax, 00100h                            ; b8 00 01                    ; 0xc420d vbe.c:527
    push SS                                   ; 16                          ; 0xc4210 vbe.c:531
    pop ES                                    ; 07                          ; 0xc4211
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc4212
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4215 vbe.c:532
    pop di                                    ; 5f                          ; 0xc4218
    pop si                                    ; 5e                          ; 0xc4219
    pop bp                                    ; 5d                          ; 0xc421a
    retn                                      ; c3                          ; 0xc421b
  ; disGetNextSymbol 0xc421c LB 0x348 -> off=0x0 cb=0000000000000008 uValue=00000000000c421c 'vbe_biosfn_read_video_state_size'
vbe_biosfn_read_video_state_size:            ; 0xc421c LB 0x8
    push bp                                   ; 55                          ; 0xc421c vbe.c:534
    mov bp, sp                                ; 89 e5                       ; 0xc421d
    mov ax, strict word 00012h                ; b8 12 00                    ; 0xc421f vbe.c:537
    pop bp                                    ; 5d                          ; 0xc4222
    retn                                      ; c3                          ; 0xc4223
  ; disGetNextSymbol 0xc4224 LB 0x340 -> off=0x0 cb=000000000000004b uValue=00000000000c4224 'vbe_biosfn_save_video_state'
vbe_biosfn_save_video_state:                 ; 0xc4224 LB 0x4b
    push bp                                   ; 55                          ; 0xc4224 vbe.c:539
    mov bp, sp                                ; 89 e5                       ; 0xc4225
    push bx                                   ; 53                          ; 0xc4227
    push cx                                   ; 51                          ; 0xc4228
    push si                                   ; 56                          ; 0xc4229
    mov si, ax                                ; 89 c6                       ; 0xc422a
    mov bx, dx                                ; 89 d3                       ; 0xc422c
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc422e vbe.c:543
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4231
    out DX, ax                                ; ef                          ; 0xc4234
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4235 vbe.c:544
    in ax, DX                                 ; ed                          ; 0xc4238
    mov es, si                                ; 8e c6                       ; 0xc4239 vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc423b
    inc bx                                    ; 43                          ; 0xc423e vbe.c:546
    inc bx                                    ; 43                          ; 0xc423f
    test AL, strict byte 001h                 ; a8 01                       ; 0xc4240 vbe.c:547
    je short 04267h                           ; 74 23                       ; 0xc4242
    mov cx, strict word 00001h                ; b9 01 00                    ; 0xc4244 vbe.c:549
    jmp short 0424eh                          ; eb 05                       ; 0xc4247
    cmp cx, strict byte 00009h                ; 83 f9 09                    ; 0xc4249
    jnbe short 04267h                         ; 77 19                       ; 0xc424c
    cmp cx, strict byte 00004h                ; 83 f9 04                    ; 0xc424e vbe.c:550
    je short 04264h                           ; 74 11                       ; 0xc4251
    mov ax, cx                                ; 89 c8                       ; 0xc4253 vbe.c:551
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc4255
    out DX, ax                                ; ef                          ; 0xc4258
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4259 vbe.c:552
    in ax, DX                                 ; ed                          ; 0xc425c
    mov es, si                                ; 8e c6                       ; 0xc425d vbe.c:62
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc425f
    inc bx                                    ; 43                          ; 0xc4262 vbe.c:553
    inc bx                                    ; 43                          ; 0xc4263
    inc cx                                    ; 41                          ; 0xc4264 vbe.c:555
    jmp short 04249h                          ; eb e2                       ; 0xc4265
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc4267 vbe.c:556
    pop si                                    ; 5e                          ; 0xc426a
    pop cx                                    ; 59                          ; 0xc426b
    pop bx                                    ; 5b                          ; 0xc426c
    pop bp                                    ; 5d                          ; 0xc426d
    retn                                      ; c3                          ; 0xc426e
  ; disGetNextSymbol 0xc426f LB 0x2f5 -> off=0x0 cb=000000000000008f uValue=00000000000c426f 'vbe_biosfn_restore_video_state'
vbe_biosfn_restore_video_state:              ; 0xc426f LB 0x8f
    push bp                                   ; 55                          ; 0xc426f vbe.c:559
    mov bp, sp                                ; 89 e5                       ; 0xc4270
    push bx                                   ; 53                          ; 0xc4272
    push cx                                   ; 51                          ; 0xc4273
    push si                                   ; 56                          ; 0xc4274
    push ax                                   ; 50                          ; 0xc4275
    mov cx, ax                                ; 89 c1                       ; 0xc4276
    mov bx, dx                                ; 89 d3                       ; 0xc4278
    mov es, ax                                ; 8e c0                       ; 0xc427a vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc427c
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc427f
    inc bx                                    ; 43                          ; 0xc4282 vbe.c:564
    inc bx                                    ; 43                          ; 0xc4283
    test byte [bp-008h], 001h                 ; f6 46 f8 01                 ; 0xc4284 vbe.c:566
    jne short 0429ah                          ; 75 10                       ; 0xc4288
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc428a vbe.c:567
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc428d
    out DX, ax                                ; ef                          ; 0xc4290
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc4291 vbe.c:568
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc4294
    out DX, ax                                ; ef                          ; 0xc4297
    jmp short 042f6h                          ; eb 5c                       ; 0xc4298 vbe.c:569
    mov ax, strict word 00001h                ; b8 01 00                    ; 0xc429a vbe.c:570
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc429d
    out DX, ax                                ; ef                          ; 0xc42a0
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc42a1 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42a4 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc42a7
    inc bx                                    ; 43                          ; 0xc42a8 vbe.c:572
    inc bx                                    ; 43                          ; 0xc42a9
    mov ax, strict word 00002h                ; b8 02 00                    ; 0xc42aa
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42ad
    out DX, ax                                ; ef                          ; 0xc42b0
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc42b1 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42b4 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc42b7
    inc bx                                    ; 43                          ; 0xc42b8 vbe.c:575
    inc bx                                    ; 43                          ; 0xc42b9
    mov ax, strict word 00003h                ; b8 03 00                    ; 0xc42ba
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42bd
    out DX, ax                                ; ef                          ; 0xc42c0
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc42c1 vbe.c:57
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42c4 vbe.c:58
    out DX, ax                                ; ef                          ; 0xc42c7
    inc bx                                    ; 43                          ; 0xc42c8 vbe.c:578
    inc bx                                    ; 43                          ; 0xc42c9
    mov ax, strict word 00004h                ; b8 04 00                    ; 0xc42ca
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42cd
    out DX, ax                                ; ef                          ; 0xc42d0
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc42d1 vbe.c:580
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42d4
    out DX, ax                                ; ef                          ; 0xc42d7
    mov si, strict word 00005h                ; be 05 00                    ; 0xc42d8 vbe.c:582
    jmp short 042e2h                          ; eb 05                       ; 0xc42db
    cmp si, strict byte 00009h                ; 83 fe 09                    ; 0xc42dd
    jnbe short 042f6h                         ; 77 14                       ; 0xc42e0
    mov ax, si                                ; 89 f0                       ; 0xc42e2 vbe.c:583
    mov dx, 001ceh                            ; ba ce 01                    ; 0xc42e4
    out DX, ax                                ; ef                          ; 0xc42e7
    mov es, cx                                ; 8e c1                       ; 0xc42e8 vbe.c:57
    mov ax, word [es:bx]                      ; 26 8b 07                    ; 0xc42ea
    mov dx, 001cfh                            ; ba cf 01                    ; 0xc42ed vbe.c:58
    out DX, ax                                ; ef                          ; 0xc42f0
    inc bx                                    ; 43                          ; 0xc42f1 vbe.c:585
    inc bx                                    ; 43                          ; 0xc42f2
    inc si                                    ; 46                          ; 0xc42f3 vbe.c:586
    jmp short 042ddh                          ; eb e7                       ; 0xc42f4
    lea sp, [bp-006h]                         ; 8d 66 fa                    ; 0xc42f6 vbe.c:588
    pop si                                    ; 5e                          ; 0xc42f9
    pop cx                                    ; 59                          ; 0xc42fa
    pop bx                                    ; 5b                          ; 0xc42fb
    pop bp                                    ; 5d                          ; 0xc42fc
    retn                                      ; c3                          ; 0xc42fd
  ; disGetNextSymbol 0xc42fe LB 0x266 -> off=0x0 cb=000000000000008d uValue=00000000000c42fe 'vbe_biosfn_save_restore_state'
vbe_biosfn_save_restore_state:               ; 0xc42fe LB 0x8d
    push bp                                   ; 55                          ; 0xc42fe vbe.c:604
    mov bp, sp                                ; 89 e5                       ; 0xc42ff
    push si                                   ; 56                          ; 0xc4301
    push di                                   ; 57                          ; 0xc4302
    push ax                                   ; 50                          ; 0xc4303
    mov si, ax                                ; 89 c6                       ; 0xc4304
    mov word [bp-006h], dx                    ; 89 56 fa                    ; 0xc4306
    mov ax, bx                                ; 89 d8                       ; 0xc4309
    mov bx, word [bp+004h]                    ; 8b 5e 04                    ; 0xc430b
    mov di, strict word 0004fh                ; bf 4f 00                    ; 0xc430e vbe.c:609
    xor ah, ah                                ; 30 e4                       ; 0xc4311 vbe.c:610
    cmp ax, strict word 00002h                ; 3d 02 00                    ; 0xc4313
    je short 0435eh                           ; 74 46                       ; 0xc4316
    cmp ax, strict word 00001h                ; 3d 01 00                    ; 0xc4318
    je short 04342h                           ; 74 25                       ; 0xc431b
    test ax, ax                               ; 85 c0                       ; 0xc431d
    jne short 0437ah                          ; 75 59                       ; 0xc431f
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc4321 vbe.c:612
    call 032b8h                               ; e8 91 ef                    ; 0xc4324
    mov cx, ax                                ; 89 c1                       ; 0xc4327
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc4329 vbe.c:616
    je short 04334h                           ; 74 05                       ; 0xc432d
    call 0421ch                               ; e8 ea fe                    ; 0xc432f vbe.c:617
    add ax, cx                                ; 01 c8                       ; 0xc4332
    add ax, strict word 0003fh                ; 05 3f 00                    ; 0xc4334 vbe.c:618
    mov CL, strict byte 006h                  ; b1 06                       ; 0xc4337
    shr ax, CL                                ; d3 e8                       ; 0xc4339
    push SS                                   ; 16                          ; 0xc433b
    pop ES                                    ; 07                          ; 0xc433c
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc433d
    jmp short 0437dh                          ; eb 3b                       ; 0xc4340 vbe.c:619
    push SS                                   ; 16                          ; 0xc4342 vbe.c:621
    pop ES                                    ; 07                          ; 0xc4343
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc4344
    mov dx, cx                                ; 89 ca                       ; 0xc4347 vbe.c:622
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc4349
    call 032f6h                               ; e8 a7 ef                    ; 0xc434c
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc434f vbe.c:626
    je short 0437dh                           ; 74 28                       ; 0xc4353
    mov dx, ax                                ; 89 c2                       ; 0xc4355 vbe.c:627
    mov ax, cx                                ; 89 c8                       ; 0xc4357
    call 04224h                               ; e8 c8 fe                    ; 0xc4359
    jmp short 0437dh                          ; eb 1f                       ; 0xc435c vbe.c:628
    push SS                                   ; 16                          ; 0xc435e vbe.c:630
    pop ES                                    ; 07                          ; 0xc435f
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc4360
    mov dx, cx                                ; 89 ca                       ; 0xc4363 vbe.c:631
    mov ax, word [bp-006h]                    ; 8b 46 fa                    ; 0xc4365
    call 035ceh                               ; e8 63 f2                    ; 0xc4368
    test byte [bp-006h], 008h                 ; f6 46 fa 08                 ; 0xc436b vbe.c:635
    je short 0437dh                           ; 74 0c                       ; 0xc436f
    mov dx, ax                                ; 89 c2                       ; 0xc4371 vbe.c:636
    mov ax, cx                                ; 89 c8                       ; 0xc4373
    call 0426fh                               ; e8 f7 fe                    ; 0xc4375
    jmp short 0437dh                          ; eb 03                       ; 0xc4378 vbe.c:637
    mov di, 00100h                            ; bf 00 01                    ; 0xc437a vbe.c:640
    push SS                                   ; 16                          ; 0xc437d vbe.c:643
    pop ES                                    ; 07                          ; 0xc437e
    mov word [es:si], di                      ; 26 89 3c                    ; 0xc437f
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4382 vbe.c:644
    pop di                                    ; 5f                          ; 0xc4385
    pop si                                    ; 5e                          ; 0xc4386
    pop bp                                    ; 5d                          ; 0xc4387
    retn 00002h                               ; c2 02 00                    ; 0xc4388
  ; disGetNextSymbol 0xc438b LB 0x1d9 -> off=0x0 cb=00000000000000e2 uValue=00000000000c438b 'vbe_biosfn_get_set_scanline_length'
vbe_biosfn_get_set_scanline_length:          ; 0xc438b LB 0xe2
    push bp                                   ; 55                          ; 0xc438b vbe.c:665
    mov bp, sp                                ; 89 e5                       ; 0xc438c
    push si                                   ; 56                          ; 0xc438e
    push di                                   ; 57                          ; 0xc438f
    sub sp, strict byte 0000ah                ; 83 ec 0a                    ; 0xc4390
    push ax                                   ; 50                          ; 0xc4393
    mov di, dx                                ; 89 d7                       ; 0xc4394
    mov word [bp-006h], bx                    ; 89 5e fa                    ; 0xc4396
    mov si, cx                                ; 89 ce                       ; 0xc4399
    call 03e17h                               ; e8 79 fa                    ; 0xc439b vbe.c:674
    cmp AL, strict byte 00fh                  ; 3c 0f                       ; 0xc439e vbe.c:675
    jne short 043a7h                          ; 75 05                       ; 0xc43a0
    mov cx, strict word 00010h                ; b9 10 00                    ; 0xc43a2
    jmp short 043abh                          ; eb 04                       ; 0xc43a5
    xor ah, ah                                ; 30 e4                       ; 0xc43a7
    mov cx, ax                                ; 89 c1                       ; 0xc43a9
    mov ch, cl                                ; 88 cd                       ; 0xc43ab
    call 03e4fh                               ; e8 9f fa                    ; 0xc43ad vbe.c:676
    mov word [bp-00ah], ax                    ; 89 46 f6                    ; 0xc43b0
    mov word [bp-00ch], strict word 0004fh    ; c7 46 f4 4f 00              ; 0xc43b3 vbe.c:677
    push SS                                   ; 16                          ; 0xc43b8 vbe.c:678
    pop ES                                    ; 07                          ; 0xc43b9
    mov bx, word [bp-006h]                    ; 8b 5e fa                    ; 0xc43ba
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc43bd
    mov al, byte [es:di]                      ; 26 8a 05                    ; 0xc43c0 vbe.c:679
    cmp AL, strict byte 002h                  ; 3c 02                       ; 0xc43c3 vbe.c:683
    je short 043d2h                           ; 74 0b                       ; 0xc43c5
    cmp AL, strict byte 001h                  ; 3c 01                       ; 0xc43c7
    je short 043fbh                           ; 74 30                       ; 0xc43c9
    test al, al                               ; 84 c0                       ; 0xc43cb
    je short 043f6h                           ; 74 27                       ; 0xc43cd
    jmp near 04456h                           ; e9 84 00                    ; 0xc43cf
    cmp ch, 004h                              ; 80 fd 04                    ; 0xc43d2 vbe.c:685
    jne short 043ddh                          ; 75 06                       ; 0xc43d5
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc43d7 vbe.c:686
    sal bx, CL                                ; d3 e3                       ; 0xc43d9
    jmp short 043f6h                          ; eb 19                       ; 0xc43db vbe.c:687
    mov al, ch                                ; 88 e8                       ; 0xc43dd vbe.c:688
    xor ah, ah                                ; 30 e4                       ; 0xc43df
    cwd                                       ; 99                          ; 0xc43e1
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc43e2
    sal dx, CL                                ; d3 e2                       ; 0xc43e4
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc43e6
    sar ax, CL                                ; d3 f8                       ; 0xc43e8
    mov word [bp-00eh], ax                    ; 89 46 f2                    ; 0xc43ea
    mov ax, bx                                ; 89 d8                       ; 0xc43ed
    xor dx, dx                                ; 31 d2                       ; 0xc43ef
    div word [bp-00eh]                        ; f7 76 f2                    ; 0xc43f1
    mov bx, ax                                ; 89 c3                       ; 0xc43f4
    mov ax, bx                                ; 89 d8                       ; 0xc43f6 vbe.c:691
    call 03e30h                               ; e8 35 fa                    ; 0xc43f8
    call 03e4fh                               ; e8 51 fa                    ; 0xc43fb vbe.c:694
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc43fe
    push SS                                   ; 16                          ; 0xc4401 vbe.c:695
    pop ES                                    ; 07                          ; 0xc4402
    mov bx, word [bp-006h]                    ; 8b 5e fa                    ; 0xc4403
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4406
    cmp ch, 004h                              ; 80 fd 04                    ; 0xc4409 vbe.c:696
    jne short 04416h                          ; 75 08                       ; 0xc440c
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc440e vbe.c:697
    mov bx, ax                                ; 89 c3                       ; 0xc4410
    shr bx, CL                                ; d3 eb                       ; 0xc4412
    jmp short 0442ch                          ; eb 16                       ; 0xc4414 vbe.c:698
    mov al, ch                                ; 88 e8                       ; 0xc4416 vbe.c:699
    xor ah, ah                                ; 30 e4                       ; 0xc4418
    cwd                                       ; 99                          ; 0xc441a
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc441b
    sal dx, CL                                ; d3 e2                       ; 0xc441d
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc441f
    sar ax, CL                                ; d3 f8                       ; 0xc4421
    mov bx, ax                                ; 89 c3                       ; 0xc4423
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc4425
    mul bx                                    ; f7 e3                       ; 0xc4428
    mov bx, ax                                ; 89 c3                       ; 0xc442a
    add bx, strict byte 00003h                ; 83 c3 03                    ; 0xc442c vbe.c:700
    and bl, 0fch                              ; 80 e3 fc                    ; 0xc442f
    push SS                                   ; 16                          ; 0xc4432 vbe.c:701
    pop ES                                    ; 07                          ; 0xc4433
    mov word [es:di], bx                      ; 26 89 1d                    ; 0xc4434
    call 03e68h                               ; e8 2e fa                    ; 0xc4437 vbe.c:702
    push SS                                   ; 16                          ; 0xc443a
    pop ES                                    ; 07                          ; 0xc443b
    mov word [es:si], ax                      ; 26 89 04                    ; 0xc443c
    call 03ddfh                               ; e8 9d f9                    ; 0xc443f vbe.c:703
    push SS                                   ; 16                          ; 0xc4442
    pop ES                                    ; 07                          ; 0xc4443
    cmp ax, word [es:si]                      ; 26 3b 04                    ; 0xc4444
    jbe short 0445bh                          ; 76 12                       ; 0xc4447
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc4449 vbe.c:704
    call 03e30h                               ; e8 e1 f9                    ; 0xc444c
    mov word [bp-00ch], 00200h                ; c7 46 f4 00 02              ; 0xc444f vbe.c:705
    jmp short 0445bh                          ; eb 05                       ; 0xc4454 vbe.c:707
    mov word [bp-00ch], 00100h                ; c7 46 f4 00 01              ; 0xc4456 vbe.c:710
    push SS                                   ; 16                          ; 0xc445b vbe.c:713
    pop ES                                    ; 07                          ; 0xc445c
    mov ax, word [bp-00ch]                    ; 8b 46 f4                    ; 0xc445d
    mov bx, word [bp-010h]                    ; 8b 5e f0                    ; 0xc4460
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc4463
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc4466 vbe.c:714
    pop di                                    ; 5f                          ; 0xc4469
    pop si                                    ; 5e                          ; 0xc446a
    pop bp                                    ; 5d                          ; 0xc446b
    retn                                      ; c3                          ; 0xc446c
  ; disGetNextSymbol 0xc446d LB 0xf7 -> off=0x0 cb=00000000000000f7 uValue=00000000000c446d 'private_biosfn_custom_mode'
private_biosfn_custom_mode:                  ; 0xc446d LB 0xf7
    push bp                                   ; 55                          ; 0xc446d vbe.c:740
    mov bp, sp                                ; 89 e5                       ; 0xc446e
    push si                                   ; 56                          ; 0xc4470
    push di                                   ; 57                          ; 0xc4471
    sub sp, strict byte 00006h                ; 83 ec 06                    ; 0xc4472
    push ax                                   ; 50                          ; 0xc4475
    mov si, dx                                ; 89 d6                       ; 0xc4476
    mov di, cx                                ; 89 cf                       ; 0xc4478
    mov word [bp-00ah], strict word 0004fh    ; c7 46 f6 4f 00              ; 0xc447a vbe.c:753
    push SS                                   ; 16                          ; 0xc447f vbe.c:754
    pop ES                                    ; 07                          ; 0xc4480
    mov al, byte [es:si]                      ; 26 8a 04                    ; 0xc4481
    test al, al                               ; 84 c0                       ; 0xc4484 vbe.c:755
    jne short 044a8h                          ; 75 20                       ; 0xc4486
    push SS                                   ; 16                          ; 0xc4488 vbe.c:757
    pop ES                                    ; 07                          ; 0xc4489
    mov bx, word [es:bx]                      ; 26 8b 1f                    ; 0xc448a
    mov ax, word [es:di]                      ; 26 8b 05                    ; 0xc448d vbe.c:758
    mov word [bp-008h], ax                    ; 89 46 f8                    ; 0xc4490
    mov al, byte [es:si+001h]                 ; 26 8a 44 01                 ; 0xc4493 vbe.c:759
    and ax, strict word 0007fh                ; 25 7f 00                    ; 0xc4497
    mov ch, al                                ; 88 c5                       ; 0xc449a
    cmp AL, strict byte 008h                  ; 3c 08                       ; 0xc449c vbe.c:764
    je short 044b0h                           ; 74 10                       ; 0xc449e
    cmp AL, strict byte 010h                  ; 3c 10                       ; 0xc44a0
    je short 044b0h                           ; 74 0c                       ; 0xc44a2
    cmp AL, strict byte 020h                  ; 3c 20                       ; 0xc44a4
    je short 044b0h                           ; 74 08                       ; 0xc44a6
    mov word [bp-00ah], 00100h                ; c7 46 f6 00 01              ; 0xc44a8 vbe.c:765
    jmp near 04552h                           ; e9 a2 00                    ; 0xc44ad vbe.c:766
    push SS                                   ; 16                          ; 0xc44b0 vbe.c:770
    pop ES                                    ; 07                          ; 0xc44b1
    test byte [es:si+001h], 080h              ; 26 f6 44 01 80              ; 0xc44b2
    je short 044beh                           ; 74 05                       ; 0xc44b7
    mov ax, strict word 00040h                ; b8 40 00                    ; 0xc44b9
    jmp short 044c0h                          ; eb 02                       ; 0xc44bc
    xor ax, ax                                ; 31 c0                       ; 0xc44be
    mov byte [bp-006h], al                    ; 88 46 fa                    ; 0xc44c0
    cmp bx, 00280h                            ; 81 fb 80 02                 ; 0xc44c3 vbe.c:773
    jnc short 044ceh                          ; 73 05                       ; 0xc44c7
    mov bx, 00280h                            ; bb 80 02                    ; 0xc44c9 vbe.c:774
    jmp short 044d7h                          ; eb 09                       ; 0xc44cc vbe.c:775
    cmp bx, 00a00h                            ; 81 fb 00 0a                 ; 0xc44ce
    jbe short 044d7h                          ; 76 03                       ; 0xc44d2
    mov bx, 00a00h                            ; bb 00 0a                    ; 0xc44d4 vbe.c:776
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc44d7 vbe.c:777
    cmp ax, 001e0h                            ; 3d e0 01                    ; 0xc44da
    jnc short 044e6h                          ; 73 07                       ; 0xc44dd
    mov word [bp-008h], 001e0h                ; c7 46 f8 e0 01              ; 0xc44df vbe.c:778
    jmp short 044f0h                          ; eb 0a                       ; 0xc44e4 vbe.c:779
    cmp ax, 00780h                            ; 3d 80 07                    ; 0xc44e6
    jbe short 044f0h                          ; 76 05                       ; 0xc44e9
    mov word [bp-008h], 00780h                ; c7 46 f8 80 07              ; 0xc44eb vbe.c:780
    mov dx, strict word 0ffffh                ; ba ff ff                    ; 0xc44f0 vbe.c:786
    mov ax, 003b6h                            ; b8 b6 03                    ; 0xc44f3
    call 03e81h                               ; e8 88 f9                    ; 0xc44f6
    mov si, ax                                ; 89 c6                       ; 0xc44f9
    mov al, ch                                ; 88 e8                       ; 0xc44fb vbe.c:789
    xor ah, ah                                ; 30 e4                       ; 0xc44fd
    cwd                                       ; 99                          ; 0xc44ff
    mov CL, strict byte 003h                  ; b1 03                       ; 0xc4500
    sal dx, CL                                ; d3 e2                       ; 0xc4502
    db  01bh, 0c2h
    ; sbb ax, dx                                ; 1b c2                     ; 0xc4504
    sar ax, CL                                ; d3 f8                       ; 0xc4506
    mov dx, ax                                ; 89 c2                       ; 0xc4508
    mov ax, bx                                ; 89 d8                       ; 0xc450a
    mul dx                                    ; f7 e2                       ; 0xc450c
    add ax, strict word 00003h                ; 05 03 00                    ; 0xc450e vbe.c:790
    and AL, strict byte 0fch                  ; 24 fc                       ; 0xc4511
    mov dx, word [bp-008h]                    ; 8b 56 f8                    ; 0xc4513 vbe.c:792
    mul dx                                    ; f7 e2                       ; 0xc4516
    cmp dx, si                                ; 39 f2                       ; 0xc4518 vbe.c:794
    jnbe short 04522h                         ; 77 06                       ; 0xc451a
    jne short 04529h                          ; 75 0b                       ; 0xc451c
    test ax, ax                               ; 85 c0                       ; 0xc451e
    jbe short 04529h                          ; 76 07                       ; 0xc4520
    mov word [bp-00ah], 00200h                ; c7 46 f6 00 02              ; 0xc4522 vbe.c:796
    jmp short 04552h                          ; eb 29                       ; 0xc4527 vbe.c:797
    xor ax, ax                                ; 31 c0                       ; 0xc4529 vbe.c:801
    call 00610h                               ; e8 e2 c0                    ; 0xc452b
    mov al, ch                                ; 88 e8                       ; 0xc452e vbe.c:802
    xor ah, ah                                ; 30 e4                       ; 0xc4530
    call 03df8h                               ; e8 c3 f8                    ; 0xc4532
    mov ax, bx                                ; 89 d8                       ; 0xc4535 vbe.c:803
    call 03da1h                               ; e8 67 f8                    ; 0xc4537
    mov ax, word [bp-008h]                    ; 8b 46 f8                    ; 0xc453a vbe.c:804
    call 03dc0h                               ; e8 80 f8                    ; 0xc453d
    xor ax, ax                                ; 31 c0                       ; 0xc4540 vbe.c:805
    call 00636h                               ; e8 f1 c0                    ; 0xc4542
    mov al, byte [bp-006h]                    ; 8a 46 fa                    ; 0xc4545 vbe.c:806
    or AL, strict byte 001h                   ; 0c 01                       ; 0xc4548
    xor ah, ah                                ; 30 e4                       ; 0xc454a
    call 00610h                               ; e8 c1 c0                    ; 0xc454c
    call 00708h                               ; e8 b6 c1                    ; 0xc454f vbe.c:807
    push SS                                   ; 16                          ; 0xc4552 vbe.c:815
    pop ES                                    ; 07                          ; 0xc4553
    mov ax, word [bp-00ah]                    ; 8b 46 f6                    ; 0xc4554
    mov bx, word [bp-00ch]                    ; 8b 5e f4                    ; 0xc4557
    mov word [es:bx], ax                      ; 26 89 07                    ; 0xc455a
    lea sp, [bp-004h]                         ; 8d 66 fc                    ; 0xc455d vbe.c:816
    pop di                                    ; 5f                          ; 0xc4560
    pop si                                    ; 5e                          ; 0xc4561
    pop bp                                    ; 5d                          ; 0xc4562
    retn                                      ; c3                          ; 0xc4563

  ; Padding 0xdc bytes at 0xc4564
  times 220 db 0

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
    db  073h, 038h, 030h, 038h, 036h, 02fh, 056h, 042h, 06fh, 078h, 056h, 067h, 061h, 042h, 069h, 06fh
    db  073h, 038h, 030h, 038h, 036h, 02eh, 073h, 079h, 06dh, 000h, 000h, 000h, 000h, 000h, 000h, 000h
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
    db  000h, 072h
