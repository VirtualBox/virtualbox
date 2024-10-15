/* $Id: bs3kit-mangling-data.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Symbol mangling.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*
 * First part is only applied once.  It concerns itself with data symbols.
 */

#ifndef BS3KIT_INCLUDED_bs3kit_mangling_data_h
#define BS3KIT_INCLUDED_bs3kit_mangling_data_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if 0 /* the object converter deals with this now  */
#if ARCH_BITS == 64

# define Bs3Gdt                                 BS3_DATA_NM(Bs3Gdt)
# define Bs3Gdt_Ldt                             BS3_DATA_NM(Bs3Gdt_Ldt)
# define Bs3Gdte_Tss16                          BS3_DATA_NM(Bs3Gdte_Tss16)
# define Bs3Gdte_Tss16DoubleFault               BS3_DATA_NM(Bs3Gdte_Tss16DoubleFault)
# define Bs3Gdte_Tss16Spare0                    BS3_DATA_NM(Bs3Gdte_Tss16Spare0)
# define Bs3Gdte_Tss16Spare1                    BS3_DATA_NM(Bs3Gdte_Tss16Spare1)
# define Bs3Gdte_Tss32                          BS3_DATA_NM(Bs3Gdte_Tss32)
# define Bs3Gdte_Tss32DoubleFault               BS3_DATA_NM(Bs3Gdte_Tss32DoubleFault)
# define Bs3Gdte_Tss32Spare0                    BS3_DATA_NM(Bs3Gdte_Tss32Spare0)
# define Bs3Gdte_Tss32Spare1                    BS3_DATA_NM(Bs3Gdte_Tss32Spare1)
# define Bs3Gdte_Tss32IobpIntRedirBm            BS3_DATA_NM(Bs3Gdte_Tss32IobpIntRedirBm)
# define Bs3Gdte_Tss32IntRedirBm                BS3_DATA_NM(Bs3Gdte_Tss32IntRedirBm)
# define Bs3Gdte_Tss64                          BS3_DATA_NM(Bs3Gdte_Tss64)
# define Bs3Gdte_Tss64Spare0                    BS3_DATA_NM(Bs3Gdte_Tss64Spare0)
# define Bs3Gdte_Tss64Spare1                    BS3_DATA_NM(Bs3Gdte_Tss64Spare1)
# define Bs3Gdte_Tss64Iobp                      BS3_DATA_NM(Bs3Gdte_Tss64Iobp)
# define Bs3Gdte_RMTEXT16_CS                    BS3_DATA_NM(Bs3Gdte_RMTEXT16_CS)
# define Bs3Gdte_X0TEXT16_CS                    BS3_DATA_NM(Bs3Gdte_X0TEXT16_CS)
# define Bs3Gdte_X1TEXT16_CS                    BS3_DATA_NM(Bs3Gdte_X1TEXT16_CS)
# define Bs3Gdte_R0_MMIO16                      BS3_DATA_NM(Bs3Gdte_R0_MMIO16)

# define Bs3Gdte_R0_First                       BS3_DATA_NM(Bs3Gdte_R0_First)
# define Bs3Gdte_R0_CS16                        BS3_DATA_NM(Bs3Gdte_R0_CS16)
# define Bs3Gdte_R0_DS16                        BS3_DATA_NM(Bs3Gdte_R0_DS16)
# define Bs3Gdte_R0_SS16                        BS3_DATA_NM(Bs3Gdte_R0_SS16)
# define Bs3Gdte_R0_CS32                        BS3_DATA_NM(Bs3Gdte_R0_CS32)
# define Bs3Gdte_R0_DS32                        BS3_DATA_NM(Bs3Gdte_R0_DS32)
# define Bs3Gdte_R0_SS32                        BS3_DATA_NM(Bs3Gdte_R0_SS32)
# define Bs3Gdte_R0_CS64                        BS3_DATA_NM(Bs3Gdte_R0_CS64)
# define Bs3Gdte_R0_DS64                        BS3_DATA_NM(Bs3Gdte_R0_DS64)
# define Bs3Gdte_R0_CS16_EO                     BS3_DATA_NM(Bs3Gdte_R0_CS16_EO)
# define Bs3Gdte_R0_CS16_CNF                    BS3_DATA_NM(Bs3Gdte_R0_CS16_CNF)
# define Bs3Gdte_R0_CS16_CND_EO                 BS3_DATA_NM(Bs3Gdte_R0_CS16_CND_EO)
# define Bs3Gdte_R0_CS32_EO                     BS3_DATA_NM(Bs3Gdte_R0_CS32_EO)
# define Bs3Gdte_R0_CS32_CNF                    BS3_DATA_NM(Bs3Gdte_R0_CS32_CNF)
# define Bs3Gdte_R0_CS32_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R0_CS32_CNF_EO)
# define Bs3Gdte_R0_CS64_EO                     BS3_DATA_NM(Bs3Gdte_R0_CS64_EO)
# define Bs3Gdte_R0_CS64_CNF                    BS3_DATA_NM(Bs3Gdte_R0_CS64_CNF)
# define Bs3Gdte_R0_CS64_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R0_CS64_CNF_EO)

# define Bs3Gdte_R1_First                       BS3_DATA_NM(Bs3Gdte_R1_First)
# define Bs3Gdte_R1_CS16                        BS3_DATA_NM(Bs3Gdte_R1_CS16)
# define Bs3Gdte_R1_DS16                        BS3_DATA_NM(Bs3Gdte_R1_DS16)
# define Bs3Gdte_R1_SS16                        BS3_DATA_NM(Bs3Gdte_R1_SS16)
# define Bs3Gdte_R1_CS32                        BS3_DATA_NM(Bs3Gdte_R1_CS32)
# define Bs3Gdte_R1_DS32                        BS3_DATA_NM(Bs3Gdte_R1_DS32)
# define Bs3Gdte_R1_SS32                        BS3_DATA_NM(Bs3Gdte_R1_SS32)
# define Bs3Gdte_R1_CS64                        BS3_DATA_NM(Bs3Gdte_R1_CS64)
# define Bs3Gdte_R1_DS64                        BS3_DATA_NM(Bs3Gdte_R1_DS64)
# define Bs3Gdte_R1_CS16_EO                     BS3_DATA_NM(Bs3Gdte_R1_CS16_EO)
# define Bs3Gdte_R1_CS16_CNF                    BS3_DATA_NM(Bs3Gdte_R1_CS16_CNF)
# define Bs3Gdte_R1_CS16_CND_EO                 BS3_DATA_NM(Bs3Gdte_R1_CS16_CND_EO)
# define Bs3Gdte_R1_CS32_EO                     BS3_DATA_NM(Bs3Gdte_R1_CS32_EO)
# define Bs3Gdte_R1_CS32_CNF                    BS3_DATA_NM(Bs3Gdte_R1_CS32_CNF)
# define Bs3Gdte_R1_CS32_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R1_CS32_CNF_EO)
# define Bs3Gdte_R1_CS64_EO                     BS3_DATA_NM(Bs3Gdte_R1_CS64_EO)
# define Bs3Gdte_R1_CS64_CNF                    BS3_DATA_NM(Bs3Gdte_R1_CS64_CNF)
# define Bs3Gdte_R1_CS64_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R1_CS64_CNF_EO)

# define Bs3Gdte_R2_First                       BS3_DATA_NM(Bs3Gdte_R2_First)
# define Bs3Gdte_R2_CS16                        BS3_DATA_NM(Bs3Gdte_R2_CS16)
# define Bs3Gdte_R2_DS16                        BS3_DATA_NM(Bs3Gdte_R2_DS16)
# define Bs3Gdte_R2_SS16                        BS3_DATA_NM(Bs3Gdte_R2_SS16)
# define Bs3Gdte_R2_CS32                        BS3_DATA_NM(Bs3Gdte_R2_CS32)
# define Bs3Gdte_R2_DS32                        BS3_DATA_NM(Bs3Gdte_R2_DS32)
# define Bs3Gdte_R2_SS32                        BS3_DATA_NM(Bs3Gdte_R2_SS32)
# define Bs3Gdte_R2_CS64                        BS3_DATA_NM(Bs3Gdte_R2_CS64)
# define Bs3Gdte_R2_DS64                        BS3_DATA_NM(Bs3Gdte_R2_DS64)
# define Bs3Gdte_R2_CS16_EO                     BS3_DATA_NM(Bs3Gdte_R2_CS16_EO)
# define Bs3Gdte_R2_CS16_CNF                    BS3_DATA_NM(Bs3Gdte_R2_CS16_CNF)
# define Bs3Gdte_R2_CS16_CND_EO                 BS3_DATA_NM(Bs3Gdte_R2_CS16_CND_EO)
# define Bs3Gdte_R2_CS32_EO                     BS3_DATA_NM(Bs3Gdte_R2_CS32_EO)
# define Bs3Gdte_R2_CS32_CNF                    BS3_DATA_NM(Bs3Gdte_R2_CS32_CNF)
# define Bs3Gdte_R2_CS32_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R2_CS32_CNF_EO)
# define Bs3Gdte_R2_CS64_EO                     BS3_DATA_NM(Bs3Gdte_R2_CS64_EO)
# define Bs3Gdte_R2_CS64_CNF                    BS3_DATA_NM(Bs3Gdte_R2_CS64_CNF)
# define Bs3Gdte_R2_CS64_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R2_CS64_CNF_EO)

# define Bs3Gdte_R3_First                       BS3_DATA_NM(Bs3Gdte_R3_First)
# define Bs3Gdte_R3_CS16                        BS3_DATA_NM(Bs3Gdte_R3_CS16)
# define Bs3Gdte_R3_DS16                        BS3_DATA_NM(Bs3Gdte_R3_DS16)
# define Bs3Gdte_R3_SS16                        BS3_DATA_NM(Bs3Gdte_R3_SS16)
# define Bs3Gdte_R3_CS32                        BS3_DATA_NM(Bs3Gdte_R3_CS32)
# define Bs3Gdte_R3_DS32                        BS3_DATA_NM(Bs3Gdte_R3_DS32)
# define Bs3Gdte_R3_SS32                        BS3_DATA_NM(Bs3Gdte_R3_SS32)
# define Bs3Gdte_R3_CS64                        BS3_DATA_NM(Bs3Gdte_R3_CS64)
# define Bs3Gdte_R3_DS64                        BS3_DATA_NM(Bs3Gdte_R3_DS64)
# define Bs3Gdte_R3_CS16_EO                     BS3_DATA_NM(Bs3Gdte_R3_CS16_EO)
# define Bs3Gdte_R3_CS16_CNF                    BS3_DATA_NM(Bs3Gdte_R3_CS16_CNF)
# define Bs3Gdte_R3_CS16_CND_EO                 BS3_DATA_NM(Bs3Gdte_R3_CS16_CND_EO)
# define Bs3Gdte_R3_CS32_EO                     BS3_DATA_NM(Bs3Gdte_R3_CS32_EO)
# define Bs3Gdte_R3_CS32_CNF                    BS3_DATA_NM(Bs3Gdte_R3_CS32_CNF)
# define Bs3Gdte_R3_CS32_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R3_CS32_CNF_EO)
# define Bs3Gdte_R3_CS64_EO                     BS3_DATA_NM(Bs3Gdte_R3_CS64_EO)
# define Bs3Gdte_R3_CS64_CNF                    BS3_DATA_NM(Bs3Gdte_R3_CS64_CNF)
# define Bs3Gdte_R3_CS64_CNF_EO                 BS3_DATA_NM(Bs3Gdte_R3_CS64_CNF_EO)

# define Bs3GdteSpare00                         BS3_DATA_NM(Bs3GdteSpare00)
# define Bs3GdteSpare01                         BS3_DATA_NM(Bs3GdteSpare01)
# define Bs3GdteSpare02                         BS3_DATA_NM(Bs3GdteSpare02)
# define Bs3GdteSpare03                         BS3_DATA_NM(Bs3GdteSpare03)
# define Bs3GdteSpare04                         BS3_DATA_NM(Bs3GdteSpare04)
# define Bs3GdteSpare05                         BS3_DATA_NM(Bs3GdteSpare05)
# define Bs3GdteSpare06                         BS3_DATA_NM(Bs3GdteSpare06)
# define Bs3GdteSpare07                         BS3_DATA_NM(Bs3GdteSpare07)
# define Bs3GdteSpare08                         BS3_DATA_NM(Bs3GdteSpare08)
# define Bs3GdteSpare09                         BS3_DATA_NM(Bs3GdteSpare09)
# define Bs3GdteSpare0a                         BS3_DATA_NM(Bs3GdteSpare0a)
# define Bs3GdteSpare0b                         BS3_DATA_NM(Bs3GdteSpare0b)
# define Bs3GdteSpare0c                         BS3_DATA_NM(Bs3GdteSpare0c)
# define Bs3GdteSpare0d                         BS3_DATA_NM(Bs3GdteSpare0d)
# define Bs3GdteSpare0e                         BS3_DATA_NM(Bs3GdteSpare0e)
# define Bs3GdteSpare0f                         BS3_DATA_NM(Bs3GdteSpare0f)
# define Bs3GdteSpare10                         BS3_DATA_NM(Bs3GdteSpare10)
# define Bs3GdteSpare11                         BS3_DATA_NM(Bs3GdteSpare11)
# define Bs3GdteSpare12                         BS3_DATA_NM(Bs3GdteSpare12)
# define Bs3GdteSpare13                         BS3_DATA_NM(Bs3GdteSpare13)
# define Bs3GdteSpare14                         BS3_DATA_NM(Bs3GdteSpare14)
# define Bs3GdteSpare15                         BS3_DATA_NM(Bs3GdteSpare15)
# define Bs3GdteSpare16                         BS3_DATA_NM(Bs3GdteSpare16)
# define Bs3GdteSpare17                         BS3_DATA_NM(Bs3GdteSpare17)
# define Bs3GdteSpare18                         BS3_DATA_NM(Bs3GdteSpare18)
# define Bs3GdteSpare19                         BS3_DATA_NM(Bs3GdteSpare19)
# define Bs3GdteSpare1a                         BS3_DATA_NM(Bs3GdteSpare1a)
# define Bs3GdteSpare1b                         BS3_DATA_NM(Bs3GdteSpare1b)
# define Bs3GdteSpare1c                         BS3_DATA_NM(Bs3GdteSpare1c)
# define Bs3GdteSpare1d                         BS3_DATA_NM(Bs3GdteSpare1d)
# define Bs3GdteSpare1e                         BS3_DATA_NM(Bs3GdteSpare1e)
# define Bs3GdteSpare1f                         BS3_DATA_NM(Bs3GdteSpare1f)

# define Bs3GdteTiled                           BS3_DATA_NM(Bs3GdteTiled)
# define Bs3GdteFreePart1                       BS3_DATA_NM(Bs3GdteFreePart1)
# define Bs3Gdte_CODE16                         BS3_DATA_NM(Bs3Gdte_CODE16)
# define Bs3GdteFreePart2                       BS3_DATA_NM(Bs3GdteFreePart2)
# define Bs3Gdte_SYSTEM16                       BS3_DATA_NM(Bs3Gdte_SYSTEM16)
# define Bs3GdteFreePart3                       BS3_DATA_NM(Bs3GdteFreePart3)
# define Bs3Gdte_DATA16                         BS3_DATA_NM(Bs3Gdte_DATA16)

# define Bs3GdteFreePart4                       BS3_DATA_NM(Bs3GdteFreePart4)
# define Bs3GdtePreTestPage08                   BS3_DATA_NM(Bs3GdtePreTestPage08)
# define Bs3GdtePreTestPage07                   BS3_DATA_NM(Bs3GdtePreTestPage07)
# define Bs3GdtePreTestPage06                   BS3_DATA_NM(Bs3GdtePreTestPage06)
# define Bs3GdtePreTestPage05                   BS3_DATA_NM(Bs3GdtePreTestPage05)
# define Bs3GdtePreTestPage04                   BS3_DATA_NM(Bs3GdtePreTestPage04)
# define Bs3GdtePreTestPage03                   BS3_DATA_NM(Bs3GdtePreTestPage03)
# define Bs3GdtePreTestPage02                   BS3_DATA_NM(Bs3GdtePreTestPage02)
# define Bs3GdtePreTestPage01                   BS3_DATA_NM(Bs3GdtePreTestPage01)
# define Bs3GdteTestPage                        BS3_DATA_NM(Bs3GdteTestPage)
# define Bs3GdteTestPage00                      BS3_DATA_NM(Bs3GdteTestPage00)
# define Bs3GdteTestPage01                      BS3_DATA_NM(Bs3GdteTestPage01)
# define Bs3GdteTestPage02                      BS3_DATA_NM(Bs3GdteTestPage02)
# define Bs3GdteTestPage03                      BS3_DATA_NM(Bs3GdteTestPage03)
# define Bs3GdteTestPage04                      BS3_DATA_NM(Bs3GdteTestPage04)
# define Bs3GdteTestPage05                      BS3_DATA_NM(Bs3GdteTestPage05)
# define Bs3GdteTestPage06                      BS3_DATA_NM(Bs3GdteTestPage06)
# define Bs3GdteTestPage07                      BS3_DATA_NM(Bs3GdteTestPage07)

# define Bs3GdtEnd                              BS3_DATA_NM(Bs3GdtEnd)

# define Bs3Tss16                               BS3_DATA_NM(Bs3Tss16)
# define Bs3Tss16DoubleFault                    BS3_DATA_NM(Bs3Tss16DoubleFault)
# define Bs3Tss16Spare0                         BS3_DATA_NM(Bs3Tss16Spare0)
# define Bs3Tss16Spare1                         BS3_DATA_NM(Bs3Tss16Spare1)
# define Bs3Tss32                               BS3_DATA_NM(Bs3Tss32)
# define Bs3Tss32DoubleFault                    BS3_DATA_NM(Bs3Tss32DoubleFault)
# define Bs3Tss32Spare0                         BS3_DATA_NM(Bs3Tss32Spare0)
# define Bs3Tss32Spare1                         BS3_DATA_NM(Bs3Tss32Spare1)
# define Bs3Tss64                               BS3_DATA_NM(Bs3Tss64)
# define Bs3Tss64Spare0                         BS3_DATA_NM(Bs3Tss64Spare0)
# define Bs3Tss64Spare1                         BS3_DATA_NM(Bs3Tss64Spare1)
# define Bs3Tss64WithIopb                       BS3_DATA_NM(Bs3Tss64WithIopb)
# define Bs3Tss32WithIopb                       BS3_DATA_NM(Bs3Tss32WithIopb)
# define Bs3SharedIntRedirBm                    BS3_DATA_NM(Bs3SharedIntRedirBm)
# define Bs3SharedIobp                          BS3_DATA_NM(Bs3SharedIobp)
# define Bs3SharedIobpEnd                       BS3_DATA_NM(Bs3SharedIobpEnd)
# define Bs3Idt16                               BS3_DATA_NM(Bs3Idt16)
# define Bs3Idt32                               BS3_DATA_NM(Bs3Idt32)
# define Bs3Idt64                               BS3_DATA_NM(Bs3Idt64)
# define Bs3Lidt_Idt16                          BS3_DATA_NM(Bs3Lidt_Idt16)
# define Bs3Lidt_Idt32                          BS3_DATA_NM(Bs3Lidt_Idt32)
# define Bs3Lidt_Idt64                          BS3_DATA_NM(Bs3Lidt_Idt64)
# define Bs3Lidt_Ivt                            BS3_DATA_NM(Bs3Lidt_Ivt)
# define Bs3Lgdt_Gdt                            BS3_DATA_NM(Bs3Lgdt_Gdt)
# define Bs3Ldt                                 BS3_DATA_NM(Bs3Ldt)
# define Bs3LdtEnd                              BS3_DATA_NM(Bs3LdtEnd)

# define Bs3Text16_StartOfSegment               BS3_DATA_NM(Bs3Text16_StartOfSegment)
# define Bs3Text16_EndOfSegment                 BS3_DATA_NM(Bs3Text16_EndOfSegment)
# define Bs3Text16_Size                         BS3_DATA_NM(Bs3Text16_Size)

# define Bs3System16_StartOfSegment             BS3_DATA_NM(Bs3System16_StartOfSegment)
# define Bs3System16_EndOfSegment               BS3_DATA_NM(Bs3System16_EndOfSegment)

# define Bs3Data16_StartOfSegment               BS3_DATA_NM(Bs3Data16_StartOfSegment)
# define Bs3Data16_EndOfSegment                 BS3_DATA_NM(Bs3Data16_EndOfSegment)

# define Bs3RmText16_StartOfSegment             BS3_DATA_NM(Bs3RmText16_StartOfSegment)
# define Bs3RmText16_EndOfSegment               BS3_DATA_NM(Bs3RmText16_EndOfSegment)
# define Bs3RmText16_Size                       BS3_DATA_NM(Bs3RmText16_Size)
# define Bs3RmText16_FlatAddr                   BS3_DATA_NM(Bs3RmText16_FlatAddr)

# define Bs3X0Text16_StartOfSegment             BS3_DATA_NM(Bs3X0Text16_StartOfSegment)
# define Bs3X0Text16_EndOfSegment               BS3_DATA_NM(Bs3X0Text16_EndOfSegment)
# define Bs3X0Text16_Size                       BS3_DATA_NM(Bs3X0Text16_Size)
# define Bs3X0Text16_FlatAddr                   BS3_DATA_NM(Bs3X0Text16_FlatAddr)

# define Bs3X1Text16_StartOfSegment             BS3_DATA_NM(Bs3X1Text16_StartOfSegment)
# define Bs3X1Text16_EndOfSegment               BS3_DATA_NM(Bs3X1Text16_EndOfSegment)
# define Bs3X1Text16_Size                       BS3_DATA_NM(Bs3X1Text16_Size)
# define Bs3X1Text16_FlatAddr                   BS3_DATA_NM(Bs3X1Text16_FlatAddr)

# define Bs3Text32_StartOfSegment               BS3_DATA_NM(Bs3Text32_StartOfSegment)
# define Bs3Text32_EndOfSegment                 BS3_DATA_NM(Bs3Text32_EndOfSegment)

# define Bs3Data32_StartOfSegment               BS3_DATA_NM(Bs3Data32_StartOfSegment)
# define Bs3Data32_EndOfSegment                 BS3_DATA_NM(Bs3Data32_EndOfSegment)

# define Bs3Text64_StartOfSegment               BS3_DATA_NM(Bs3Text64_StartOfSegment)
# define Bs3Text64_EndOfSegment                 BS3_DATA_NM(Bs3Text64_EndOfSegment)

# define Bs3Data64_StartOfSegment               BS3_DATA_NM(Bs3Data64_StartOfSegment)
# define Bs3Data64_EndOfSegment                 BS3_DATA_NM(Bs3Data64_EndOfSegment)

# define Bs3Data16Thru64Text32And64_TotalSize   BS3_DATA_NM(Bs3Data16Thru64Text32And64_TotalSize)
# define Bs3TotalImageSize                      BS3_DATA_NM(Bs3TotalImageSize)

# define g_achBs3HexDigits                      BS3_DATA_NM(g_achBs3HexDigits)
# define g_achBs3HexDigitsUpper                 BS3_DATA_NM(g_achBs3HexDigitsUpper)
# define g_bBs3CurrentMode                      BS3_DATA_NM(g_bBs3CurrentMode)
# define g_uBs3TrapEipHint                      BS3_DATA_NM(g_uBs3TrapEipHint)
# define g_aBs3RmIvtOriginal                    BS3_DATA_NM(g_aBs3RmIvtOriginal)

# define g_usBs3TestStep                        BS3_DATA_NM(g_usBs3TestStep)
# define g_usBs3TestStep                        BS3_DATA_NM(g_usBs3TestStep)
# define g_Bs3Trap16GenericEntriesFlatAddr      BS3_DATA_NM(g_Bs3Trap16GenericEntriesFlatAddr)
# define g_Bs3Trap32GenericEntriesFlatAddr      BS3_DATA_NM(g_Bs3Trap32GenericEntriesFlatAddr)
# define g_Bs3Trap64GenericEntriesFlatAddr      BS3_DATA_NM(g_Bs3Trap64GenericEntriesFlatAddr)

# define g_uBs3CpuDetected                      BS3_DATA_NM(g_uBs3CpuDetected)

#endif /* ARCH_BITS == 64 */
#endif /* not needed */

#endif /* !BS3KIT_INCLUDED_bs3kit_mangling_data_h */

