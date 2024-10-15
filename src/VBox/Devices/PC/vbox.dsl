/* $Id: vbox.dsl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox ACPI
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

DefinitionBlock ("DSDT.aml", "DSDT", 2, "VBOX  ", "VBOXBIOS", 2)
{
    // Declare debugging ports withing SystemIO
    OperationRegion(DBG0, SystemIO, 0x3000, 4)

    // Writes to this field Will dump hex char
    Field (DBG0, ByteAcc, NoLock, Preserve)
    {
        DHE1, 8,
    }

    // Writes to this field Will dump hex word
    Field (DBG0, WordAcc, NoLock, Preserve)
    {
        DHE2, 16,
    }

    // Writes to this field Will dump hex double word
    Field (DBG0, DWordAcc, NoLock, Preserve)
    {
        DHE4, 32,
    }

    // Writes to this field will dump ascii char
    Field (DBG0, ByteAcc, NoLock, Preserve)
    {
        Offset (1),
        DCHR, 8
    }

    // Shortcuts
    Method(HEX, 1)
    {
        Store (Arg0, DHE1)
    }

    Method(HEX2, 1)
    {
        Store (Arg0, DHE2)
    }

    Method(HEX4, 1)
    {
        Store (Arg0, DHE4)
    }

    // Code from Microsoft sample
    // http://www.microsoft.com/whdc/system/pnppwr/powermgmt/_OSI-method.mspx

    //
    // SLEN(Str) - Returns the length of Str (excluding NULL).
    //
    Method(SLEN, 1)
    {
        //
        // Note: The caller must make sure that the argument is a string object.
        //
        Store(Arg0, Local0)
        Return(Sizeof(Local0))
    }


    //
    // S2BF(Str) - Convert a string object into a buffer object.
    //
    Method(S2BF, 1, Serialized)
    {
        //
        // Note: The caller must make sure that the argument is a string object.
        //
        // Local0 contains length of string + NULL.
        //
        Store(Arg0, Local0)
        Add(SLEN(Local0), One, Local0)
        //
        // Convert the string object into a buffer object.
        //
        Name(BUFF, Buffer(Local0) {})
        Store(Arg0, BUFF)
        Return(BUFF)
    }

    //
    // MIN(Int1, Int2) - Returns the minimum of Int1 or Int2.
    //
    //
    Method(MIN, 2)
    {
        //
        // Note: The caller must make sure that both arguments are integer objects.
        //
        If (LLess(Arg0, Arg1))
        {
            Return(Arg0)
        }
        Else
        {
            Return(Arg1)
        }
    }

    //
    // SCMP(Str1, Str2) - Compare Str1 and Str2.
    //                    Returns One if Str1 > Str2
    //                    Returns Zero if Str1 == Str2
    //                    Returns Ones if Str1 < Str2
    //
    Method(SCMP, 2)
    {
        //
        // Note: The caller must make sure that both arguments are string objects.
        //
        // Local0 is a buffer of Str1.
        // Local1 is a buffer of Str2.
        // Local2 is the indexed byte of Str1.
        // Local3 is the indexed byte of Str2.
        // Local4 is the index to both Str1 and Str2.
        // Local5 is the length of Str1.
        // Local6 is the length of Str2.
        // Local7 is the minimum of Str1 or Str2 length.
        //

        Store(Arg0, Local0)
        Store(S2BF(Local0), Local0)

        Store(S2BF(Arg1), Local1)
        Store(Zero, Local4)

        Store(SLEN(Arg0), Local5)
        Store(SLEN(Arg1), Local6)
        Store(MIN(Local5, Local6), Local7)

        While (LLess(Local4, Local7))
        {
            Store(Derefof(Index(Local0, Local4)), Local2)
            Store(Derefof(Index(Local1, Local4)), Local3)
            If (LGreater(Local2, Local3))
            {
                Return(One)
            }
            Else
            {
                If (LLess(Local2, Local3))
                {
                    Return(Ones)
                }
            }

            Increment(Local4)
        }

        If (LLess(Local4, Local5))
        {
            Return(One)
        }
        Else
        {
            If (LLess(Local4, Local6))
            {
                Return(Ones)
            }
            Else
            {
                Return(Zero)
            }
        }
    }

    // Return one if strings match, zero otherwise. Wrapper around SCMP
    Method (MTCH, 2)
    {
        Store(Arg0, Local0)
        Store(Arg1, Local1)
        Store(SCMP(Local0, Local1), Local2)
        Return(LNot(Local2))
    }

    // Convert ASCII string to buffer and store it's contents (char by
    // char) into DCHR (thus possibly writing the string to console)
    Method (\DBG, 1, NotSerialized)
    {
        Store(Arg0, Local0)
        Store(S2BF (Local0), Local1)
        Store(SizeOf (Local1), Local0)
        Decrement (Local0)
        Store(Zero, Local2)
        While (Local0)
        {
            Decrement (Local0)
            Store (DerefOf (Index (Local1, Local2)), DCHR)
            Increment (Local2)
        }
    }

    // Microsoft Windows version indicator
    Name(MSWV, Ones)

    //
    // Return Windows version. Detect non-Microsoft OSes.
    //
    //  0 : Not Windows OS
    //  2 : Windows Me
    //  3 : Windows 2000 (NT pre-XP)
    //  4 : Windows XP
    //  5 : Windows Server 2003
    //  6 : Windows Vista
    //  7 : Windows 7
    //  8 : Windows 8
    //  9 : Windows 8.1
    // 10 : Windows 10
    Method(MSWN, 0, NotSerialized)
    {
        If (LNotEqual(MSWV, Ones))
        {
            Return(MSWV)
        }

        Store(0x00, MSWV)
        DBG("_OS: ")
        DBG(_OS)
        DBG("\n")

        // Does OS provide the _OSI method?
        If (CondRefOf(_OSI))
        {
            DBG("_OSI exists\n")
            // OS returns non-zero value in response to _OSI query if it
            // supports the interface. Newer Windows releases support older
            // versions of the ACPI interface.
            If (_OSI("Windows 2001"))
            {
                Store(4, MSWV)  // XP
            }
            If (_OSI("Windows 2001.1"))
            {
                Store(5, MSWV)  // Server 2003
            }
            If (_OSI("Windows 2006"))
            {
                Store(6, MSWV)  // Vista
            }
            If (_OSI("Windows 2009"))
            {
                Store(7, MSWV)  // Windows 7
            }
            If (_OSI("Windows 2012"))
            {
                Store(8, MSWV)  // Windows 8
            }
            If (_OSI("Windows 2013"))
            {
                Store(9, MSWV)  // Windows 8.1
            }
            If (_OSI("Windows 2015"))
            {
                Store(10, MSWV) // Windows 10
            }

            // This must come last and is a trap. No version of Windows
            // reports this!
            If (_OSI("Windows 2006 SP2"))
            {
                DBG("Windows 2006 SP2 supported\n")
                // Not a Microsoft OS
                Store(0, MSWV)
            }
        }
        Else
        {
            // No _OSI, could be older NT or Windows 9x
            If (MTCH(_OS, "Microsoft Windows NT"))
            {
                Store(3, MSWV)
            }
            If (MTCH(_OS, "Microsoft WindowsME: Millennium Edition"))
            {
                Store(2, MSWV)
            }
        }

        // Does OS provide the _REV method?
        If (CondRefOf(_REV))
        {
            DBG("_REV: ")
            HEX4(_REV)

            // Defeat most Linuxes and other non-Microsoft OSes. Microsoft Windows
            // up to Server 2003 reports ACPI 1.0 support, Vista up to Windows 10
            // reports ACPI 2.0 support. Anything pretending to be a Windows OS
            // with higher ACPI revision support is a fake.
            If (LAnd(LGreater(MSWV, 0),LGreater(_REV, 2)))
            {
                If (LLess(MSWV,8))
                {
                    DBG("ACPI rev mismatch, not a Microsoft OS\n")
                    Store(0, MSWV)
                }
            }
        }

        DBG("Determined MSWV: ")
        HEX4(MSWV)

        Return(MSWV)
    }

    Name(PICM, 0)
    Method(_PIC, 1)
    {
        DBG ("Pic mode: ")
        HEX4 (Arg0)
        Store (Arg0, PICM)
    }

    // Declare indexed registers used for reading configuration information
    OperationRegion (SYSI, SystemIO, 0x4048, 0x08)
    Field (SYSI, DwordAcc, NoLock, Preserve)
    {
       IDX0, 32,
       DAT0, 32,
    }

    IndexField (IDX0, DAT0, DwordAcc, NoLock, Preserve)
    {
        MEML,  32, // low-memory length (64KB units)
        UIOA,  32, // if IO APIC enabled
        UHPT,  32, // if HPET enabled
        USMC,  32, // if SMC enabled
        UFDC,  32, // if floppy controller enabled
        SL2B,  32, // Serial2 base IO address
        SL2I,  32, // Serial2 IRQ
        SL3B,  32, // Serial3 base IO address
        SL3I,  32, // Serial3 IRQ
        PMNN,  32, // start of 64-bit prefetch window (64KB units)
        URTC,  32, // if RTC shown in tables
        CPUL,  32, // flag of CPU lock state
        CPUC,  32, // CPU to check lock status
        CPET,  32, // type of CPU hotplug event
        CPEV,  32, // id of CPU event targets
        NICA,  32, // Primary NIC PCI address
        HDAA,  32, // HDA PCI address
        PWRS,  32, // power states
        IOCA,  32, // southbridge IO controller PCI address
        HBCA,  32, // host bus controller address
        PCIB,  32, // PCI MCFG base start
        PCIL,  32, // PCI MCFG length
        SL0B,  32, // Serial0 base IO address
        SL0I,  32, // Serial0 IRQ
        SL1B,  32, // Serial1 base IO address
        SL1I,  32, // Serial1 IRQ
        PP0B,  32, // Parallel0 base IO address
        PP0I,  32, // Parallel0 IRQ
        PP1B,  32, // Parallel1 base IO address
        PP1I,  32, // Parallel1 IRQ
        PMNX,  32, // limit of 64-bit prefetch window (64KB units)
        NVMA,  32, // Primary NVMe controller PCI address
        IOMA,  32, // AMD IOMMU
        SIOA,  32, // Southbridge IO APIC (when AMD IOMMU is present)
        Offset (0x200),
        VAIN, 32,
    }

    Scope (\_SB)
    {
        Method (_INI, 0, NotSerialized)
        {
            Store (0xbadc0de, VAIN)
            DBG ("MEML: ")
            HEX4 (MEML)
            DBG ("UIOA: ")
            HEX4 (UIOA)
            DBG ("UHPT: ")
            HEX4 (UHPT)
            DBG ("USMC: ")
            HEX4 (USMC)
            DBG ("UFDC: ")
            HEX4 (UFDC)
            DBG ("PMNN: ")
            HEX4 (PMNN)
        }

        // PCI PIC IRQ Routing table
        // Must match pci.c:pci_slot_get_pirq
        Name (PR00, Package ()
        {
/** @todo add devices 0/1 to be complete */
            Package (0x04) {0x0002FFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x0002FFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x0002FFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x0002FFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x0003FFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x0003FFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x0003FFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x0003FFFF, 0x03, LNKB, 0x00,},

            Package (0x04) {0x0004FFFF, 0x00, LNKD, 0x00,},
            Package (0x04) {0x0004FFFF, 0x01, LNKA, 0x00,},
            Package (0x04) {0x0004FFFF, 0x02, LNKB, 0x00,},
            Package (0x04) {0x0004FFFF, 0x03, LNKC, 0x00,},

            Package (0x04) {0x0005FFFF, 0x00, LNKA, 0x00,},
            Package (0x04) {0x0005FFFF, 0x01, LNKB, 0x00,},
            Package (0x04) {0x0005FFFF, 0x02, LNKC, 0x00,},
            Package (0x04) {0x0005FFFF, 0x03, LNKD, 0x00,},

            Package (0x04) {0x0006FFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x0006FFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x0006FFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x0006FFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x0007FFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x0007FFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x0007FFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x0007FFFF, 0x03, LNKB, 0x00,},

            Package (0x04) {0x0008FFFF, 0x00, LNKD, 0x00,},
            Package (0x04) {0x0008FFFF, 0x01, LNKA, 0x00,},
            Package (0x04) {0x0008FFFF, 0x02, LNKB, 0x00,},
            Package (0x04) {0x0008FFFF, 0x03, LNKC, 0x00,},

            Package (0x04) {0x0009FFFF, 0x00, LNKA, 0x00,},
            Package (0x04) {0x0009FFFF, 0x01, LNKB, 0x00,},
            Package (0x04) {0x0009FFFF, 0x02, LNKC, 0x00,},
            Package (0x04) {0x0009FFFF, 0x03, LNKD, 0x00,},

            Package (0x04) {0x000AFFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x000AFFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x000AFFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x000AFFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x000BFFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x000BFFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x000BFFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x000BFFFF, 0x03, LNKB, 0x00,},

            Package (0x04) {0x000CFFFF, 0x00, LNKD, 0x00,},
            Package (0x04) {0x000CFFFF, 0x01, LNKA, 0x00,},
            Package (0x04) {0x000CFFFF, 0x02, LNKB, 0x00,},
            Package (0x04) {0x000CFFFF, 0x03, LNKC, 0x00,},

            Package (0x04) {0x000DFFFF, 0x00, LNKA, 0x00,},
            Package (0x04) {0x000DFFFF, 0x01, LNKB, 0x00,},
            Package (0x04) {0x000DFFFF, 0x02, LNKC, 0x00,},
            Package (0x04) {0x000DFFFF, 0x03, LNKD, 0x00,},

            Package (0x04) {0x000EFFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x000EFFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x000EFFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x000EFFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x000FFFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x000FFFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x000FFFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x000FFFFF, 0x03, LNKB, 0x00,},

            Package (0x04) {0x0010FFFF, 0x00, LNKD, 0x00,},
            Package (0x04) {0x0010FFFF, 0x01, LNKA, 0x00,},
            Package (0x04) {0x0010FFFF, 0x02, LNKB, 0x00,},
            Package (0x04) {0x0010FFFF, 0x03, LNKC, 0x00,},

            Package (0x04) {0x0011FFFF, 0x00, LNKA, 0x00,},
            Package (0x04) {0x0011FFFF, 0x01, LNKB, 0x00,},
            Package (0x04) {0x0011FFFF, 0x02, LNKC, 0x00,},
            Package (0x04) {0x0011FFFF, 0x03, LNKD, 0x00,},

            Package (0x04) {0x0012FFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x0012FFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x0012FFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x0012FFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x0013FFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x0013FFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x0013FFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x0013FFFF, 0x03, LNKB, 0x00,},

            Package (0x04) {0x0014FFFF, 0x00, LNKD, 0x00,},
            Package (0x04) {0x0014FFFF, 0x01, LNKA, 0x00,},
            Package (0x04) {0x0014FFFF, 0x02, LNKB, 0x00,},
            Package (0x04) {0x0014FFFF, 0x03, LNKC, 0x00,},

            Package (0x04) {0x0015FFFF, 0x00, LNKA, 0x00,},
            Package (0x04) {0x0015FFFF, 0x01, LNKB, 0x00,},
            Package (0x04) {0x0015FFFF, 0x02, LNKC, 0x00,},
            Package (0x04) {0x0015FFFF, 0x03, LNKD, 0x00,},

            Package (0x04) {0x0016FFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x0016FFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x0016FFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x0016FFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x0017FFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x0017FFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x0017FFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x0017FFFF, 0x03, LNKB, 0x00,},

            Package (0x04) {0x0018FFFF, 0x00, LNKD, 0x00,},
            Package (0x04) {0x0018FFFF, 0x01, LNKA, 0x00,},
            Package (0x04) {0x0018FFFF, 0x02, LNKB, 0x00,},
            Package (0x04) {0x0018FFFF, 0x03, LNKC, 0x00,},

            Package (0x04) {0x0019FFFF, 0x00, LNKA, 0x00,},
            Package (0x04) {0x0019FFFF, 0x01, LNKB, 0x00,},
            Package (0x04) {0x0019FFFF, 0x02, LNKC, 0x00,},
            Package (0x04) {0x0019FFFF, 0x03, LNKD, 0x00,},

            Package (0x04) {0x001AFFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x001AFFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x001AFFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x001AFFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x001BFFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x001BFFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x001BFFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x001BFFFF, 0x03, LNKB, 0x00,},

            Package (0x04) {0x001CFFFF, 0x00, LNKD, 0x00,},
            Package (0x04) {0x001CFFFF, 0x01, LNKA, 0x00,},
            Package (0x04) {0x001CFFFF, 0x02, LNKB, 0x00,},
            Package (0x04) {0x001CFFFF, 0x03, LNKC, 0x00,},

            Package (0x04) {0x001DFFFF, 0x00, LNKA, 0x00,},
            Package (0x04) {0x001DFFFF, 0x01, LNKB, 0x00,},
            Package (0x04) {0x001DFFFF, 0x02, LNKC, 0x00,},
            Package (0x04) {0x001DFFFF, 0x03, LNKD, 0x00,},

            Package (0x04) {0x001EFFFF, 0x00, LNKB, 0x00,},
            Package (0x04) {0x001EFFFF, 0x01, LNKC, 0x00,},
            Package (0x04) {0x001EFFFF, 0x02, LNKD, 0x00,},
            Package (0x04) {0x001EFFFF, 0x03, LNKA, 0x00,},

            Package (0x04) {0x001FFFFF, 0x00, LNKC, 0x00,},
            Package (0x04) {0x001FFFFF, 0x01, LNKD, 0x00,},
            Package (0x04) {0x001FFFFF, 0x02, LNKA, 0x00,},
            Package (0x04) {0x001FFFFF, 0x03, LNKB, 0x00,}
        })

        // PCI I/O APIC IRQ Routing table
        // Must match pci.c:pci_slot_get_acpi_pirq
        Name (PR01, Package ()
        {
/** @todo add devices 0/1 to be complete */
            Package (0x04) {0x0002FFFF, 0x00, 0x00, 0x12,},
            Package (0x04) {0x0002FFFF, 0x01, 0x00, 0x13,},
            Package (0x04) {0x0002FFFF, 0x02, 0x00, 0x14,},
            Package (0x04) {0x0002FFFF, 0x03, 0x00, 0x15,},

            Package (0x04) {0x0003FFFF, 0x00, 0x00, 0x13,},
            Package (0x04) {0x0003FFFF, 0x01, 0x00, 0x14,},
            Package (0x04) {0x0003FFFF, 0x02, 0x00, 0x15,},
            Package (0x04) {0x0003FFFF, 0x03, 0x00, 0x16,},

            Package (0x04) {0x0004FFFF, 0x00, 0x00, 0x14,},
            Package (0x04) {0x0004FFFF, 0x01, 0x00, 0x15,},
            Package (0x04) {0x0004FFFF, 0x02, 0x00, 0x16,},
            Package (0x04) {0x0004FFFF, 0x03, 0x00, 0x17,},

            Package (0x04) {0x0005FFFF, 0x00, 0x00, 0x15,},
            Package (0x04) {0x0005FFFF, 0x01, 0x00, 0x16,},
            Package (0x04) {0x0005FFFF, 0x02, 0x00, 0x17,},
            Package (0x04) {0x0005FFFF, 0x03, 0x00, 0x10,},

            Package (0x04) {0x0006FFFF, 0x00, 0x00, 0x16,},
            Package (0x04) {0x0006FFFF, 0x01, 0x00, 0x17,},
            Package (0x04) {0x0006FFFF, 0x02, 0x00, 0x10,},
            Package (0x04) {0x0006FFFF, 0x03, 0x00, 0x11,},

            Package (0x04) {0x0007FFFF, 0x00, 0x00, 0x17,},
            Package (0x04) {0x0007FFFF, 0x01, 0x00, 0x10,},
            Package (0x04) {0x0007FFFF, 0x02, 0x00, 0x11,},
            Package (0x04) {0x0007FFFF, 0x03, 0x00, 0x12,},

            Package (0x04) {0x0008FFFF, 0x00, 0x00, 0x10,},
            Package (0x04) {0x0008FFFF, 0x01, 0x00, 0x11,},
            Package (0x04) {0x0008FFFF, 0x02, 0x00, 0x12,},
            Package (0x04) {0x0008FFFF, 0x03, 0x00, 0x13,},

            Package (0x04) {0x0009FFFF, 0x00, 0x00, 0x11,},
            Package (0x04) {0x0009FFFF, 0x01, 0x00, 0x12,},
            Package (0x04) {0x0009FFFF, 0x02, 0x00, 0x13,},
            Package (0x04) {0x0009FFFF, 0x03, 0x00, 0x14,},

            Package (0x04) {0x000AFFFF, 0x00, 0x00, 0x12,},
            Package (0x04) {0x000AFFFF, 0x01, 0x00, 0x13,},
            Package (0x04) {0x000AFFFF, 0x02, 0x00, 0x14,},
            Package (0x04) {0x000AFFFF, 0x03, 0x00, 0x15,},

            Package (0x04) {0x000BFFFF, 0x00, 0x00, 0x13,},
            Package (0x04) {0x000BFFFF, 0x01, 0x00, 0x14,},
            Package (0x04) {0x000BFFFF, 0x02, 0x00, 0x15,},
            Package (0x04) {0x000BFFFF, 0x03, 0x00, 0x16,},

            Package (0x04) {0x000CFFFF, 0x00, 0x00, 0x14,},
            Package (0x04) {0x000CFFFF, 0x01, 0x00, 0x15,},
            Package (0x04) {0x000CFFFF, 0x02, 0x00, 0x16,},
            Package (0x04) {0x000CFFFF, 0x03, 0x00, 0x17,},

            Package (0x04) {0x000DFFFF, 0x00, 0x00, 0x15,},
            Package (0x04) {0x000DFFFF, 0x01, 0x00, 0x16,},
            Package (0x04) {0x000DFFFF, 0x02, 0x00, 0x17,},
            Package (0x04) {0x000DFFFF, 0x03, 0x00, 0x10,},

            Package (0x04) {0x000EFFFF, 0x00, 0x00, 0x16,},
            Package (0x04) {0x000EFFFF, 0x01, 0x00, 0x17,},
            Package (0x04) {0x000EFFFF, 0x02, 0x00, 0x10,},
            Package (0x04) {0x000EFFFF, 0x03, 0x00, 0x11,},

            Package (0x04) {0x000FFFFF, 0x00, 0x00, 0x17,},
            Package (0x04) {0x000FFFFF, 0x01, 0x00, 0x10,},
            Package (0x04) {0x000FFFFF, 0x02, 0x00, 0x11,},
            Package (0x04) {0x000FFFFF, 0x03, 0x00, 0x12,},

            Package (0x04) {0x0010FFFF, 0x00, 0x00, 0x10,},
            Package (0x04) {0x0010FFFF, 0x01, 0x00, 0x11,},
            Package (0x04) {0x0010FFFF, 0x02, 0x00, 0x12,},
            Package (0x04) {0x0010FFFF, 0x03, 0x00, 0x13,},

            Package (0x04) {0x0011FFFF, 0x00, 0x00, 0x11,},
            Package (0x04) {0x0011FFFF, 0x01, 0x00, 0x12,},
            Package (0x04) {0x0011FFFF, 0x02, 0x00, 0x13,},
            Package (0x04) {0x0011FFFF, 0x03, 0x00, 0x14,},

            Package (0x04) {0x0012FFFF, 0x00, 0x00, 0x12,},
            Package (0x04) {0x0012FFFF, 0x01, 0x00, 0x13,},
            Package (0x04) {0x0012FFFF, 0x02, 0x00, 0x14,},
            Package (0x04) {0x0012FFFF, 0x03, 0x00, 0x15,},

            Package (0x04) {0x0013FFFF, 0x00, 0x00, 0x13,},
            Package (0x04) {0x0013FFFF, 0x01, 0x00, 0x14,},
            Package (0x04) {0x0013FFFF, 0x02, 0x00, 0x15,},
            Package (0x04) {0x0013FFFF, 0x03, 0x00, 0x16,},

            Package (0x04) {0x0014FFFF, 0x00, 0x00, 0x14,},
            Package (0x04) {0x0014FFFF, 0x01, 0x00, 0x15,},
            Package (0x04) {0x0014FFFF, 0x02, 0x00, 0x16,},
            Package (0x04) {0x0014FFFF, 0x03, 0x00, 0x17,},

            Package (0x04) {0x0015FFFF, 0x00, 0x00, 0x15,},
            Package (0x04) {0x0015FFFF, 0x01, 0x00, 0x16,},
            Package (0x04) {0x0015FFFF, 0x02, 0x00, 0x17,},
            Package (0x04) {0x0015FFFF, 0x03, 0x00, 0x10,},

            Package (0x04) {0x0016FFFF, 0x00, 0x00, 0x16,},
            Package (0x04) {0x0016FFFF, 0x01, 0x00, 0x17,},
            Package (0x04) {0x0016FFFF, 0x02, 0x00, 0x10,},
            Package (0x04) {0x0016FFFF, 0x03, 0x00, 0x11,},

            Package (0x04) {0x0017FFFF, 0x00, 0x00, 0x17,},
            Package (0x04) {0x0017FFFF, 0x01, 0x00, 0x10,},
            Package (0x04) {0x0017FFFF, 0x02, 0x00, 0x11,},
            Package (0x04) {0x0017FFFF, 0x03, 0x00, 0x12,},

            Package (0x04) {0x0018FFFF, 0x00, 0x00, 0x10,},
            Package (0x04) {0x0018FFFF, 0x01, 0x00, 0x11,},
            Package (0x04) {0x0018FFFF, 0x02, 0x00, 0x12,},
            Package (0x04) {0x0018FFFF, 0x03, 0x00, 0x13,},

            Package (0x04) {0x0019FFFF, 0x00, 0x00, 0x11,},
            Package (0x04) {0x0019FFFF, 0x01, 0x00, 0x12,},
            Package (0x04) {0x0019FFFF, 0x02, 0x00, 0x13,},
            Package (0x04) {0x0019FFFF, 0x03, 0x00, 0x14,},

            Package (0x04) {0x001AFFFF, 0x00, 0x00, 0x12,},
            Package (0x04) {0x001AFFFF, 0x01, 0x00, 0x13,},
            Package (0x04) {0x001AFFFF, 0x02, 0x00, 0x14,},
            Package (0x04) {0x001AFFFF, 0x03, 0x00, 0x15,},

            Package (0x04) {0x001BFFFF, 0x00, 0x00, 0x13,},
            Package (0x04) {0x001BFFFF, 0x01, 0x00, 0x14,},
            Package (0x04) {0x001BFFFF, 0x02, 0x00, 0x15,},
            Package (0x04) {0x001BFFFF, 0x03, 0x00, 0x16,},

            Package (0x04) {0x001CFFFF, 0x00, 0x00, 0x14,},
            Package (0x04) {0x001CFFFF, 0x01, 0x00, 0x15,},
            Package (0x04) {0x001CFFFF, 0x02, 0x00, 0x16,},
            Package (0x04) {0x001CFFFF, 0x03, 0x00, 0x17,},

            Package (0x04) {0x001DFFFF, 0x00, 0x00, 0x15,},
            Package (0x04) {0x001DFFFF, 0x01, 0x00, 0x16,},
            Package (0x04) {0x001DFFFF, 0x02, 0x00, 0x17,},
            Package (0x04) {0x001DFFFF, 0x03, 0x00, 0x10,},

            Package (0x04) {0x001EFFFF, 0x00, 0x00, 0x16,},
            Package (0x04) {0x001EFFFF, 0x01, 0x00, 0x17,},
            Package (0x04) {0x001EFFFF, 0x02, 0x00, 0x10,},
            Package (0x04) {0x001EFFFF, 0x03, 0x00, 0x11,},

            Package (0x04) {0x001FFFFF, 0x00, 0x00, 0x17,},
            Package (0x04) {0x001FFFFF, 0x01, 0x00, 0x10,},
            Package (0x04) {0x001FFFFF, 0x02, 0x00, 0x11,},
            Package (0x04) {0x001FFFFF, 0x03, 0x00, 0x12,}
        })

        // Possible resource settings for PCI link A
        Name (PRSA, ResourceTemplate ()
        {
            IRQ (Level, ActiveLow, Shared) {5,9,10,11}
        })

        // Possible resource settings for PCI link B
        Name (PRSB, ResourceTemplate ()
        {
            IRQ (Level, ActiveLow, Shared) {5,9,10,11}
        })

        // Possible resource settings for PCI link C
        Name (PRSC, ResourceTemplate ()
        {
            IRQ (Level, ActiveLow, Shared) {5,9,10,11}
        })

        // Possible resource settings for PCI link D
        Name (PRSD, ResourceTemplate ()
        {
            IRQ (Level, ActiveLow, Shared) {5,9,10,11}
        })

        // PCI bus 0
        Device (PCI0)
        {

            Name (_HID, EisaId ("PNP0A03")) // PCI bus PNP id
            Method(_ADR, 0, NotSerialized)  // PCI address
            {
                 Return (HBCA)
            }
            Name (_BBN, 0x00) // base bus address (bus number)
            Name (_UID, 0x00)

            // Method that returns routing table; also opens PCI to I/O APIC
            // interrupt routing backdoor by writing 0xdead 0xbeef signature
            // to ISA bridge config space. See DevPCI.cpp/pciSetIrqInternal().
            Method (_PRT, 0, NotSerialized)
            {
                if (LEqual (LAnd (PICM, UIOA), Zero)) {
                    DBG ("RETURNING PIC\n")
                    Store (0x00, \_SB.PCI0.SBRG.APDE)
                    Store (0x00, \_SB.PCI0.SBRG.APAD)
                    Return (PR00)
                }
                else {
                    DBG ("RETURNING APIC\n")
                    Store (0xbe, \_SB.PCI0.SBRG.APDE)
                    Store (0xef, \_SB.PCI0.SBRG.APAD)
                    Return (PR01)
                }
            }

            Device (SBRG)
            {
                // Address of the southbridge device (PIIX or ICH9)
                Method(_ADR, 0, NotSerialized)
                {
                     Return (IOCA)
                }
                OperationRegion (PCIC, PCI_Config, 0x00, 0xff)

                Field (PCIC, ByteAcc, NoLock, Preserve)
                {
                    Offset (0xad),
                    APAD,   8,
                    Offset (0xde),
                    APDE,   8,
                }

                // PCI MCFG MMIO ranges
                Device (^PCIE)
                {
                    Name (_HID, EisaId ("PNP0C02"))
                    Name (_UID, 0x11)
                    Name (CRS, ResourceTemplate ()
                    {
                        Memory32Fixed (ReadOnly,
                            0xdc000000,        // Address Base
                            0x4000000,         // Address Length
                            _Y13)
                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                        CreateDWordField (CRS, \_SB.PCI0.PCIE._Y13._BAS, BAS1)
                        CreateDWordField (CRS, \_SB.PCI0.PCIE._Y13._LEN, LEN1)
                        Store (PCIB, BAS1)
                        Store (PCIL, LEN1)
                        Return (CRS)
                    }
                    Method (_STA, 0, NotSerialized)
                    {
                     if (LEqual (PCIB, Zero)) {
                        Return (0x00)
                     }
                     else {
                        Return (0x0F)
                     }
                    }
                }

                // Keyboard device
                Device (PS2K)
                {
                    Name (_HID, EisaId ("PNP0303"))
                    Method (_STA, 0, NotSerialized)
                    {
                        Return (0x0F)
                    }

                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x0060, 0x0060, 0x00, 0x01)
                        IO (Decode16, 0x0064, 0x0064, 0x00, 0x01)
                        IRQNoFlags () {1}
                    })
                }

                // DMA Controller
                Device (DMAC)
                {
                    Name (_HID, EisaId ("PNP0200"))
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x0000, 0x0000, 0x01, 0x10)
                        IO (Decode16, 0x0080, 0x0080, 0x01, 0x10)
                        IO (Decode16, 0x00C0, 0x00C0, 0x01, 0x20)
                        DMA (Compatibility, BusMaster, Transfer8_16) {4}
                    })
                }

                // Floppy disk controller
                Device (FDC0)
                {
                    Name (_HID, EisaId ("PNP0700"))

                    Method (_STA, 0, NotSerialized)
                    {
                        Return (UFDC)
                    }

                    // Current resource settings
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x03F0, 0x03F0, 0x01, 0x06)
                        IO (Decode16, 0x03F7, 0x03F7, 0x01, 0x01)
                        IRQNoFlags () {6}
                        DMA (Compatibility, NotBusMaster, Transfer8) {2}
                    })

                    // Possible resource settings
                    Name (_PRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x03F0, 0x03F0, 0x01, 0x06)
                        IO (Decode16, 0x03F7, 0x03F7, 0x01, 0x01)
                        IRQNoFlags () {6}
                        DMA (Compatibility, NotBusMaster, Transfer8) {2}
                    })

                }

                // Mouse device
                Device (PS2M)
                {
                    Name (_HID, EisaId ("PNP0F03"))
                    Method (_STA, 0, NotSerialized)
                    {
                        Return (0x0F)
                    }

                    Name (_CRS, ResourceTemplate ()
                    {
                        IRQNoFlags () {12}
                    })
                }

                // Parallel port 0
                Device (^LPT0)
                {
                    Name (_HID, EisaId ("PNP0400"))
                    Name (_UID, 0x01)
                    Method (_STA, 0, NotSerialized)
                    {
                        If (LEqual (PP0B, Zero))
                        {
                            Return (0x00)
                        }
                        Else
                        {
                            Return (0x0F)
                        }
                    }
                    Name (CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x0378, 0x0378, 0x08, 0x08, _Y18)
                        IRQNoFlags (_Y19) {7}
                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                        CreateWordField (CRS, \_SB.PCI0.LPT0._Y18._MIN, PMI0)
                        CreateWordField (CRS, \_SB.PCI0.LPT0._Y18._MAX, PMA0)
                        CreateWordField (CRS, \_SB.PCI0.LPT0._Y18._ALN, PAL0)
                        CreateWordField (CRS, \_SB.PCI0.LPT0._Y18._LEN, PLE0)
                        CreateWordField (CRS, \_SB.PCI0.LPT0._Y19._INT, PIQ0)
                        Store (PP0B, PMI0)
                        Store (PP0B, PMA0)
                        If (LEqual (0x3BC, PP0B)) {
                            Store (0x04, PAL0)
                            Store (0x04, PLE0)
                        }
                        ShiftLeft (0x01, PP0I, PIQ0)
                        Return (CRS)
                    }
                }

                // Parallel port 1
                Device (^LPT1)
                {
                    Name (_HID, EisaId ("PNP0400"))
                    Name (_UID, 0x02)
                    Method (_STA, 0, NotSerialized)
                    {
                        If (LEqual (PP1B, Zero))
                        {
                            Return (0x00)
                        }
                        Else
                        {
                            Return (0x0F)
                        }
                    }
                    Name (CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x0278, 0x0278, 0x08, 0x08, _Y20)
                        IRQNoFlags (_Y21) {5}
                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                        CreateWordField (CRS, \_SB.PCI0.LPT1._Y20._MIN, PMI1)
                        CreateWordField (CRS, \_SB.PCI0.LPT1._Y20._MAX, PMA1)
                        CreateWordField (CRS, \_SB.PCI0.LPT1._Y20._ALN, PAL1)
                        CreateWordField (CRS, \_SB.PCI0.LPT1._Y20._LEN, PLE1)
                        CreateWordField (CRS, \_SB.PCI0.LPT1._Y21._INT, PIQ1)
                        Store (PP1B, PMI1)
                        Store (PP1B, PMA1)
                        If (LEqual (0x3BC, PP1B)) {
                            Store (0x04, PAL1)
                            Store (0x04, PLE1)
                        }
                        ShiftLeft (0x01, PP1I, PIQ1)
                        Return (CRS)
                    }
                }


                // Serial port 0
                Device (^SRL0)
                {
                    Name (_HID, EisaId ("PNP0501"))
                    Name (_UID, 0x01)
                    Method (_STA, 0, NotSerialized)
                    {
                        If (LEqual (SL0B, Zero))
                        {
                            Return (0x00)
                        }
                        Else
                        {
                            Return (0x0F)
                        }
                    }
                    Name (CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x03F8, 0x03F8, 0x01, 0x08, _Y14)
                        IRQNoFlags (_Y15) {4}
                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                        CreateWordField (CRS, \_SB.PCI0.SRL0._Y14._MIN, MIN0)
                        CreateWordField (CRS, \_SB.PCI0.SRL0._Y14._MAX, MAX0)
                        CreateWordField (CRS, \_SB.PCI0.SRL0._Y15._INT, IRQ0)
                        Store (SL0B, MIN0)
                        Store (SL0B, MAX0)
                        ShiftLeft (0x01, SL0I, IRQ0)
                        Return (CRS)
                    }
                }

                // Serial port 1
                Device (^SRL1)
                {
                    Name (_HID, EisaId ("PNP0501"))
                    Name (_UID, 0x02)
                    Method (_STA, 0, NotSerialized)
                    {
                        If (LEqual (SL1B, Zero))
                        {
                            Return (0x00)
                        }
                        Else
                        {
                            Return (0x0F)
                        }
                    }
                    Name (CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x02F8, 0x02F8, 0x01, 0x08, _Y16)
                        IRQNoFlags (_Y17) {3}
                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                        CreateWordField (CRS, \_SB.PCI0.SRL1._Y16._MIN, MIN1)
                        CreateWordField (CRS, \_SB.PCI0.SRL1._Y16._MAX, MAX1)
                        CreateWordField (CRS, \_SB.PCI0.SRL1._Y17._INT, IRQ1)
                        Store (SL1B, MIN1)
                        Store (SL1B, MAX1)
                        ShiftLeft (0x01, SL1I, IRQ1)
                        Return (CRS)
                    }
                }

                // Serial port 2
                Device (^SRL2)
                {
                    Name (_HID, EisaId ("PNP0501"))
                    Name (_UID, 0x03)
                    Method (_STA, 0, NotSerialized)
                    {
                        If (LEqual (SL2B, Zero))
                        {
                            Return (0x00)
                        }
                        Else
                        {
                            Return (0x0F)
                        }
                    }
                    Name (CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x03E8, 0x03E8, 0x01, 0x08, _Y22)
                        IRQNoFlags (_Y23) {3}
                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                        CreateWordField (CRS, \_SB.PCI0.SRL2._Y22._MIN, MIN1)
                        CreateWordField (CRS, \_SB.PCI0.SRL2._Y22._MAX, MAX1)
                        CreateWordField (CRS, \_SB.PCI0.SRL2._Y23._INT, IRQ1)
                        Store (SL2B, MIN1)
                        Store (SL2B, MAX1)
                        ShiftLeft (0x01, SL2I, IRQ1)
                        Return (CRS)
                    }
                }

                // Serial port 3
                Device (^SRL3)
                {
                    Name (_HID, EisaId ("PNP0501"))
                    Name (_UID, 0x04)
                    Method (_STA, 0, NotSerialized)
                    {
                        If (LEqual (SL3B, Zero))
                        {
                            Return (0x00)
                        }
                        Else
                        {
                            Return (0x0F)
                        }
                    }
                    Name (CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x02E8, 0x02E8, 0x01, 0x08, _Y24)
                        IRQNoFlags (_Y25) {3}
                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                        CreateWordField (CRS, \_SB.PCI0.SRL3._Y24._MIN, MIN1)
                        CreateWordField (CRS, \_SB.PCI0.SRL3._Y24._MAX, MAX1)
                        CreateWordField (CRS, \_SB.PCI0.SRL3._Y25._INT, IRQ1)
                        Store (SL3B, MIN1)
                        Store (SL3B, MAX1)
                        ShiftLeft (0x01, SL3I, IRQ1)
                        Return (CRS)
                    }
                }

                // Programmable Interval Timer (i8254)
                Device (TIMR)
                {
                    Name (_HID, EisaId ("PNP0100"))
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16,
                            0x0040,             // Range Minimum
                            0x0040,             // Range Maximum
                            0x00,               // Alignment
                            0x04,               // Length
                            )
                        IO (Decode16,
                            0x0050,             // Range Minimum
                            0x0050,             // Range Maximum
                            0x10,               // Alignment
                            0x04,               // Length
                            )
                    })
                }

                // Programmable Interrupt Controller (i8259)
                Device (PIC)
                {
                    Name (_HID, EisaId ("PNP0000"))
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16,
                            0x0020,             // Range Minimum
                            0x0020,             // Range Maximum
                            0x00,               // Alignment
                            0x02,               // Length
                            )
                        IO (Decode16,
                            0x00A0,             // Range Minimum
                            0x00A0,             // Range Maximum
                            0x00,               // Alignment
                            0x02,               // Length
                            )
                        // because in APIC configs PIC connected to pin 0,
                        // and ISA IRQ0 rerouted to pin 2
                        IRQNoFlags ()
                            {2}
                    })
                }


                // Real Time Clock and CMOS (MC146818)
                Device (RTC)
                {
                    Name (_HID, EisaId ("PNP0B00"))
                    Name (_CRS, ResourceTemplate ()
                    {
                      IO (Decode16,
                          0x0070,             // Range Minimum
                          0x0070,             // Range Maximum
                          0x01,               // Alignment
                          0x02,               // Length
                      )
                    })
                    Method (_STA, 0, NotSerialized)
                    {
                       Return (URTC)
                    }
                }

                // High Precision Event Timer
                Device(HPET)
                {
                  Name (_HID,  EISAID("PNP0103"))
                  Name (_CID, EISAID("PNP0C01"))
                  Name(_UID, 0)

                  Method (_STA, 0, NotSerialized)
                  {
                       Return(UHPT)
                  }

                  Name(CRS, ResourceTemplate()
                  {
                      IRQNoFlags ()
                            {0}
                      IRQNoFlags ()
                            {8}
                      Memory32Fixed (ReadWrite,
                            0xFED00000,         // Address Base
                            0x00000400         // Address Length
                            )
                  })

                  Method (_CRS, 0, NotSerialized)
                  {
                     Return (CRS)
                  }
                }

                // AMD IOMMU (AMD-Vi), I/O Virtualization Reporting Structure
                Device (IVRS)
                {
                    Method(_ADR, 0, NotSerialized)
                    {
                        Return (IOMA)
                    }
                    Method (_STA, 0, NotSerialized)
                    {
                        if (LEqual (IOMA, Zero)) {
                            Return (0x00)
                        }
                        else {
                            Return (0x0F)
                        }
                    }
                }

                // System Management Controller
                Device (SMC)
                {
                    Name (_HID, EisaId ("APP0001"))
                    Name (_CID, "smc-napa")

                    Method (_STA, 0, NotSerialized)
                    {
                       Return (USMC)
                    }
                    Name (CRS, ResourceTemplate ()
                    {
                       IO (Decode16,
                           0x0300,             // Range Minimum
                           0x0300,             // Range Maximum
                           0x01,               // Alignment
                           0x20)               // Length
                    IRQNoFlags ()
                            {6}

                    })
                    Method (_CRS, 0, NotSerialized)
                    {
                       Return (CRS)
                    }
                 }
             }

            // NVMe controller. Required to convince OS X that
            // the controller is an internal (built-in) device.
            Device (SSD0)
            {
                Method(_ADR, 0, NotSerialized)
                {
                     Return (NVMA)
                }
                Method (_STA, 0, NotSerialized)
                {
                    if (LEqual (NVMA, Zero)) {
                        Return (0x00)
                    }
                    else {
                        Return (0x0F)
                    }
                }
                // Port 0
                Device (PRT0)
                {
                    Name (_ADR, 0xffff)
                }
            }

            // NIC
            Device (GIGE)
            {
                /**
                 * Generic NIC, according to
                 * http://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/devids.txt
                 * Needed by some Windows guests.
                 */
                Name (_HID, EisaId ("PNP8390"))

                Method(_ADR, 0, NotSerialized)
                {
                     Return (NICA)
                }
                /* Name (_PRW, Package (0x02)
                   {
                       0x09,
                       0x04
                    }) */

                 /* Wake up on LAN? */
                 /* Method (EWOL, 1, NotSerialized)
                 {
                    Return (0x00)
                 } */

                 Method (_STA, 0, NotSerialized)
                 {
                    if (LEqual (NICA, Zero)) {
                        Return (0x00)
                    }
                    else {
                        Return (0x0F)
                    }
                 }
            }

            // Graphics device
            Device (GFX0)
            {
                Name (_ADR, 0x00020000)

                // Windows releases older than Windows 8 (starting with Windows 2000)
                // get confused by this and lose the monitor device node. One of
                // the consequences is that color management is not available.
                // For Windows 2000 - Windows 7, disable this device (while keeping
                // it enabled for non-Microsoft OSes).
                Method (_STA, 0, NotSerialized)
                {
                    If (LAnd (LGreater (MSWN(), 0x00), LLess (MSWN(), 0x08)))
                    {
                        Return(0x00)
                    }
                    Else
                    {
                        Return(0x0F)
                    }
                }

                Scope (\_GPE)
                {
                    // GPE bit 2 handler
                    // GPE.2 must be set and SCI raised when
                    // display information changes.
                    Method (_L02, 0, NotSerialized)
                    {
                            Notify (\_SB.PCI0.GFX0, 0x81)
                    }
                }

                Method (_DOS, 1) { }

                Method (_DOD, 0, NotSerialized)
                {
                    Return (Package()
                    {
                        0x80000100
                    })
                }

                Device (VGA)
                {
                    Method (_ADR, 0, Serialized)
                    {
                        Return (0x0100)
                    }
                }
            }

            // HDA Audio card
            Device (HDEF)
            {
                Method(_DSM, 4, NotSerialized)
                {
                    Store (Package (0x04)
                    {
                        "layout-id",
                        Buffer (0x04)
                        {
                            /* 04 */    0x04, 0x00, 0x00, 0x00
                        },

                        "PinConfigurations",
                        Buffer (Zero) {}
                    }, Local0)
                    if (LEqual (Arg0, ToUUID("a0b5b7c6-1318-441c-b0c9-fe695eaf949b")))
                    {
                        If (LEqual (Arg1, One))
                        {
                            if (LEqual(Arg2, Zero))
                            {
                                    Store (Buffer (0x01)
                                        {
                                            0x03
                                        }
                                    , Local0)
                                    Return (Local0)
                            }
                            if (LEqual(Arg2, One))
                            {
                                    Return (Local0)
                            }
                        }
                    }
                    Store (Buffer (0x01)
                        {
                            0x0
                        }
                    , Local0)
                    Return (Local0)
                }

                Method(_ADR, 0, NotSerialized)
                {
                     Return (HDAA)
                }

                Method (_STA, 0, NotSerialized)
                {
                 if (LEqual (HDAA, Zero)) {
                        Return (0x00)
                    }
                    else {
                        Return (0x0F)
                    }
                 }
            }


            // Control method battery
            Device (BAT0)
            {
                Name (_HID, EisaId ("PNP0C0A"))
                Name (_UID, 0x00)

                Scope (\_GPE)
                {
                    // GPE bit 0 handler
                    // GPE.0 must be set and SCI raised when battery info
                    // changed. Do NOT re-evaluate _BIF (battery info, never
                    // changes) but DO re-evaluate _BST (dynamic state). Also
                    // re-evaluate the AC adapter status.
                    Method (_L00, 0, NotSerialized)
                    {
                        // _BST must be re-evaluated (battery state)
                        Notify (\_SB.PCI0.BAT0, 0x80)
                        // _PSR must be re-evaluated (AC adapter status)
                        Notify (\_SB.PCI0.AC, 0x80)
                    }
                }

                OperationRegion (CBAT, SystemIO, 0x4040, 0x08)
                Field (CBAT, DwordAcc, NoLock, Preserve)
                {
                    IDX0, 32,
                    DAT0, 32,
                }

                IndexField (IDX0, DAT0, DwordAcc, NoLock, Preserve)
                {
                    STAT, 32,
                    PRAT, 32,
                    RCAP, 32,
                    PVOL, 32,

                    UNIT, 32,
                    DCAP, 32,
                    LFCP, 32,
                    BTEC, 32,
                    DVOL, 32,
                    DWRN, 32,
                    DLOW, 32,
                    GRN1, 32,
                    GRN2, 32,

                    BSTA, 32,
                    APSR, 32,
                }

                Method (_STA, 0, NotSerialized)
                {
                    return (BSTA)
                }

                Name (PBIF, Package ()
                {
                    0x01,       // Power unit, 1 - mA
                    0x7fffffff, // Design capacity
                    0x7fffffff, // Last full charge capacity
                    0x00,       // Battery technology
                    0xffffffff, // Design voltage
                    0x00,       // Design capacity of Warning
                    0x00,       // Design capacity of Low
                    0x04,       // Battery capacity granularity 1
                    0x04,       // Battery capacity granularity 2
                    "1",        // Model number
                    "0",        // Serial number
                    "VBOX",     // Battery type
                    "innotek"   // OEM Information
                })

                Name (PBST, Package () {
                    0,          // Battery state
                    0x7fffffff, // Battery present rate
                    0x7fffffff, // Battery remaining capacity
                    0x7fffffff  // Battery present voltage
                })

                // Battery information
                Method (_BIF, 0, NotSerialized)
                {
                    Store (UNIT, Index (PBIF, 0,))
                    Store (DCAP, Index (PBIF, 1,))
                    Store (LFCP, Index (PBIF, 2,))
                    Store (BTEC, Index (PBIF, 3,))
                    Store (DVOL, Index (PBIF, 4,))
                    Store (DWRN, Index (PBIF, 5,))
                    Store (DLOW, Index (PBIF, 6,))
                    Store (GRN1, Index (PBIF, 7,))
                    Store (GRN2, Index (PBIF, 8,))

                    DBG ("_BIF:\n")
                    HEX4 (DerefOf (Index (PBIF, 0,)))
                    HEX4 (DerefOf (Index (PBIF, 1,)))
                    HEX4 (DerefOf (Index (PBIF, 2,)))
                    HEX4 (DerefOf (Index (PBIF, 3,)))
                    HEX4 (DerefOf (Index (PBIF, 4,)))
                    HEX4 (DerefOf (Index (PBIF, 5,)))
                    HEX4 (DerefOf (Index (PBIF, 6,)))
                    HEX4 (DerefOf (Index (PBIF, 7,)))
                    HEX4 (DerefOf (Index (PBIF, 8,)))

                    return (PBIF)
                }

                // Battery status
                Method (_BST, 0, NotSerialized)
                {
                    Store (STAT, Index (PBST, 0,))
                    Store (PRAT, Index (PBST, 1,))
                    Store (RCAP, Index (PBST, 2,))
                    Store (PVOL, Index (PBST, 3,))
/*
                    DBG ("_BST:\n")
                    HEX4 (DerefOf (Index (PBST, 0,)))
                    HEX4 (DerefOf (Index (PBST, 1,)))
                    HEX4 (DerefOf (Index (PBST, 2,)))
                    HEX4 (DerefOf (Index (PBST, 3,)))
*/
                    return (PBST)
                }
            }

            Device (AC)
            {
                Name (_HID, "ACPI0003")
                Name (_UID, 0x00)
                Name (_PCL, Package (0x01)
                {
                    \_SB
                })

                Method (_PSR, 0, NotSerialized)
                {
                    // DBG ("_PSR:\n")
                    // HEX4 (\_SB.PCI0.BAT0.APSR)
                    return (\_SB.PCI0.BAT0.APSR)
                }

                Method (_STA, 0, NotSerialized)
                {
                    return (0x0f)
                }
            }
        }
    }

    Scope (\_SB)
    {
        Scope (PCI0)
        {
            // PCI0 current resource settings
            Name (CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                               0x0000,
                               0x0000,
                               0x00FF,
                               0x0000,
                               0x0100)
                IO (Decode16, 0x0CF8, 0x0CF8, 0x01, 0x08)
                WordIO (ResourceProducer, MinFixed, MaxFixed,
                        PosDecode, EntireRange,
                        0x0000,
                        0x0000,
                        0x0CF7,
                        0x0000,
                        0x0CF8)
                WordIO (ResourceProducer, MinFixed, MaxFixed,
                        PosDecode, EntireRange,
                        0x0000,
                        0x0D00,
                        0xFFFF,
                        0x0000,
                        0xF300)

                /* Taken from ACPI faq (with some modifications) */
                DwordMemory( // descriptor for video RAM behind ISA bus
                     ResourceProducer,        // bit 0 of general flags is 0
                     PosDecode,
                     MinFixed,                // Range is fixed
                     MaxFixed,                // Range is Fixed
                     Cacheable,
                     ReadWrite,
                     0x00000000,              // Granularity
                     0x000a0000,              // Min
                     0x000bffff,              // Max
                     0x00000000,              // Translation
                     0x00020000               // Range Length
                     )

                DwordMemory( // Consumed-and-produced resource
                             // (all of low memory space)
                     ResourceProducer,        // bit 0 of general flags is 0
                     PosDecode,               // positive Decode
                     MinFixed,                // Range is fixed
                     MaxFixed,                // Range is fixed
                     Cacheable,
                     ReadWrite,
                     0x00000000,              // Granularity
                     0xe0000000,              // Min (calculated dynamically)

                     0xfdffffff,              // Max = 4GB - 32MB
                     0x00000000,              // Translation
                     0x1e000000,              // Range Length (calculated
                                              // dynamically)
                     ,                        // Optional field left blank
                     ,                        // Optional field left blank
                     MEM3                     // Name declaration for this
                                              //  descriptor
                     )
            })

            Name (TOM, ResourceTemplate ()
            {
                QwordMemory(
                    ResourceProducer,         // bit 0 of general flags is 0
                    PosDecode,                // positive Decode
                    MinFixed,                 // Range is fixed
                    MaxFixed,                 // Range is fixed
                    Prefetchable,
                    ReadWrite,
                    0x0000000000000000,       // _GRA: Granularity.
                    0x0000000100000000,       // _MIN: Min address, def. 4GB, will be overwritten.
                    0x0000000fffffffff,       // _MAX: Max address, def. 64GB-1, will be overwritten.
                    0x0000000000000000,       // _TRA: Translation
                    0x0000000f00000000,       // _LEN: Range length (_MAX-_MIN+1)
                    ,                         // ResourceSourceIndex: Optional field left blank
                    ,                         // ResourceSource:      Optional field left blank
                    MEM4                      // Name declaration for this descriptor.
                    )
            })

            Method (_CRS, 0, NotSerialized)
            {
                CreateDwordField (CRS, \_SB.PCI0.MEM3._MIN, RAMT)
                CreateDwordField (CRS, \_SB.PCI0.MEM3._LEN, RAMR)

                Store (MEML, RAMT)
                Subtract (0xfe000000, RAMT, RAMR)

                if (LNotEqual (PMNN, 0x00000000))
                {
                    // Not for Windows < 7!
                    If (LOr (LLess (MSWN(), 0x01), LGreater (MSWN(), 0x06)))
                    {
                        CreateQwordField (TOM, \_SB.PCI0.MEM4._MIN, TM4N)
                        CreateQwordField (TOM, \_SB.PCI0.MEM4._MAX, TM4X)
                        CreateQwordField (TOM, \_SB.PCI0.MEM4._LEN, TM4L)

                        Multiply (PMNN, 0x10000, TM4N)       // PMNN in units of 64KB
                        Subtract (Multiply (PMNX, 0x10000), 1, TM4X) // PMNX in units of 64KB
                        Add (Subtract (TM4X, TM4N), 1, TM4L) // determine LEN, MAX is already there

                        ConcatenateResTemplate (CRS, TOM, Local2)

                        Return (Local2)
                    }
                }

                Return (CRS)
            }

            /* Defined in PCI Firmware Specification 3.0 and ACPI 3.0, with both specs
             * referencing each other. The _OSC method must be present to make Linux happy,
             * but needs to prevent the OS from taking much control so as to not upset Windows.
             * NB: The first DWORD is defined in the ACPI spec but not the PCI FW spec.
             */
            Method (_OSC, 4)
            {
                Name(SUPP, 0)   // Support field value
                Name(CTRL, 0)   // Control field value

                // Break down the input capabilities buffer into individual DWORDs
                CreateDWordField(Arg3, 0, CDW1)
                CreateDWordField(Arg3, 4, CDW2)
                CreateDWordField(Arg3, 8, CDW3)

                If (LEqual (Arg0, ToUUID("33db4d5b-1ff7-401c-9657-7441c03dd766")))
                {
                    // Stash the Support and Control fields
                    Store(CDW2, SUPP)
                    Store(CDW3, CTRL)

                    DBG("_OSC: SUPP=")
                    HEX4(SUPP)
                    DBG(" CTRL=")
                    HEX4(CTRL)
                    DBG("\n")

                    // Mask off the PCI Express Capability Structure control
                    // Not emulated well enough to satisfy Windows (Vista and later)
                    And(CTRL, 0x0F, CTRL)

                    // If capabilities were masked, set the Capabilities Masked flag (bit 4)
                    If (LNotEqual(CDW3, CTRL))
                    {
                        Or(CDW1, 0x10, CDW1)
                    }

                    // Update the Control field and return
                    Store(CTRL, CDW3)
                    Return(Arg3)
                }
                Else
                {
                    // UUID not known, set Unrecognized UUID flag (bit 2)
                    Or(CDW1, 0x04, CDW1)
                    Return(Arg3)
                }
            }
        }
    }

    Scope (\_SB)
    {
        // Fields within PIIX3 configuration[0x60..0x63] with
        // IRQ mappings
        Field (\_SB.PCI0.SBRG.PCIC, ByteAcc, NoLock, Preserve)
        {
            Offset (0x60),
            PIRA,   8,
            PIRB,   8,
            PIRC,   8,
            PIRD,   8
        }

        Name (BUFA, ResourceTemplate ()
        {
            IRQ (Level, ActiveLow, Shared) {15}
        })
        CreateWordField (BUFA, 0x01, ICRS)

        // Generic status of IRQ routing entry
        Method (LSTA, 1, NotSerialized)
        {
            And (Arg0, 0x80, Local0)
//            DBG ("LSTA: ")
//            HEX (Arg0)
            If (Local0)
            {
                Return (0x09)
            }
            Else
            {
                Return (0x0B)
            }
        }

        // Generic "current resource settings" for routing entry
        Method (LCRS, 1, NotSerialized)
        {
            And (Arg0, 0x0F, Local0)
            ShiftLeft (0x01, Local0, ICRS)
//            DBG ("LCRS: ")
//            HEX (ICRS)
            Return (BUFA)
        }

        // Generic "set resource settings" for routing entry
        Method (LSRS, 1, NotSerialized)
        {
            CreateWordField (Arg0, 0x01, ISRS)
            FindSetRightBit (ISRS, Local0)
            Return (Decrement (Local0))
        }

        // Generic "disable" for routing entry
        Method (LDIS, 1, NotSerialized)
        {
            Return (Or (Arg0, 0x80))
        }

        // Link A
        Device (LNKA)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x01)

            // Status
            Method (_STA, 0, NotSerialized)
            {
                DBG ("LNKA._STA\n")
                Return (LSTA (PIRA))
            }

            // Possible resource settings
            Method (_PRS, 0, NotSerialized)
            {
                DBG ("LNKA._PRS\n")
                Return (PRSA)
            }

            // Disable
            Method (_DIS, 0, NotSerialized)
            {
                DBG ("LNKA._DIS\n")
                Store (LDIS (PIRA), PIRA)
            }

            // Current resource settings
            Method (_CRS, 0, NotSerialized)
            {
                DBG ("LNKA._CRS\n")
                Return (LCRS (PIRA))
            }

            // Set resource settings
            Method (_SRS, 1, NotSerialized)
            {
                DBG ("LNKA._SRS: ")
                HEX (LSRS (Arg0))
                Store (LSRS (Arg0), PIRA)
            }
        }

        // Link B
        Device (LNKB)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x02)
            Method (_STA, 0, NotSerialized)
            {
                // DBG ("LNKB._STA\n")
                Return (LSTA (PIRB))
            }

            Method (_PRS, 0, NotSerialized)
            {
                // DBG ("LNKB._PRS\n")
                Return (PRSB)
            }

            Method (_DIS, 0, NotSerialized)
            {
                // DBG ("LNKB._DIS\n")
                Store (LDIS (PIRB), PIRB)
            }

            Method (_CRS, 0, NotSerialized)
            {
                // DBG ("LNKB._CRS\n")
                Return (LCRS (PIRB))
            }

            Method (_SRS, 1, NotSerialized)
            {
                DBG ("LNKB._SRS: ")
                HEX (LSRS (Arg0))
                Store (LSRS (Arg0), PIRB)
            }
        }

        // Link C
        Device (LNKC)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x03)
            Method (_STA, 0, NotSerialized)
            {
                // DBG ("LNKC._STA\n")
                Return (LSTA (PIRC))
            }

            Method (_PRS, 0, NotSerialized)
            {
                // DBG ("LNKC._PRS\n")
                Return (PRSC)
            }

            Method (_DIS, 0, NotSerialized)
            {
                // DBG ("LNKC._DIS\n")
                Store (LDIS (PIRC), PIRC)
            }

            Method (_CRS, 0, NotSerialized)
            {
                // DBG ("LNKC._CRS\n")
                Return (LCRS (PIRC))
            }

            Method (_SRS, 1, NotSerialized)
            {
                DBG ("LNKC._SRS: ")
                HEX (LSRS (Arg0))
                Store (LSRS (Arg0), PIRC)
            }
        }

        // Link D
        Device (LNKD)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x04)
            Method (_STA, 0, NotSerialized)
            {
                // DBG ("LNKD._STA\n")
                Return (LSTA (PIRD))
            }

            Method (_PRS, 0, NotSerialized)
            {
                // DBG ("LNKD._PRS\n")
                Return (PRSD)
            }

            Method (_DIS, 0, NotSerialized)
            {
                // DBG ("LNKD._DIS\n")
                Store (LDIS (PIRA), PIRD)
            }

            Method (_CRS, 0, NotSerialized)
            {
                // DBG ("LNKD._CRS\n")
                Return (LCRS (PIRD))
            }

            Method (_SRS, 1, NotSerialized)
            {
                DBG ("LNKD._SRS: ")
                HEX (LSRS (Arg0))
                Store (LSRS (Arg0), PIRD)
            }
        }
    }

    // Sx states
    Name (_S0, Package (2) {
        0x00,
        0x00,
    })

    // Shift one by the power state number
    If (And(PWRS, ShiftLeft(One,1))) {
        Name (_S1, Package (2) {
            0x01,
            0x01,
        })
    }

    If (And(PWRS, ShiftLeft(One,4))) {
        Name (_S4, Package (2) {
            0x05,
            0x05,
        })
    }

    Name (_S5, Package (2) {
        0x05,
        0x05,
    })

    Method (_PTS, 1, NotSerialized)
    {
        DBG ("Prepare to sleep: ")
        HEX (Arg0)
    }
}

/*
 * Local Variables:
 * comment-start: "//"
 * End:
 */
