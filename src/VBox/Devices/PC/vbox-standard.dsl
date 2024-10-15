/* $Id: vbox-standard.dsl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

DefinitionBlock ("SSDT.aml", "SSDT", 1, "VBOX  ", "VBOXCPUT", 2)
{
    // Processor object
    // #1463: Showing the CPU can make the guest do bad things on it like SpeedStep.
    // In this case, XP SP2 contains this buggy Intelppm.sys driver which wants to mess
    // with SpeedStep if it finds a CPU object and when it finds out that it can't, it
    // tries to unload and crashes (MS probably never tested this code path).
    // So we enable this ACPI object only for certain guests, which do need it,
    // if by accident Windows guest seen enabled CPU object, just boot from latest
    // known good configuration, as it remembers state, even if ACPI object gets disabled.
    Scope (\_PR)
    {
        Processor (CPU0, /* Name */
                    0x00, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }

        Processor (CPU1, /* Name */
                    0x01, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU2, /* Name */
                    0x02, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU3, /* Name */
                    0x03, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU4, /* Name */
                    0x04, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU5, /* Name */
                    0x05, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU6, /* Name */
                    0x06, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU7, /* Name */
                    0x07, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU8, /* Name */
                    0x08, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPU9, /* Name */
                    0x09, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUA, /* Name */
                    0x0a, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUB, /* Name */
                    0x0b, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUC, /* Name */
                    0x0c, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUD, /* Name */
                    0x0d, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUE, /* Name */
                    0x0e, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUF, /* Name */
                    0x0f, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUG, /* Name */
                    0x10, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUH, /* Name */
                    0x11, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUI, /* Name */
                    0x12, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUJ, /* Name */
                    0x13, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUK, /* Name */
                    0x14, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUL, /* Name */
                    0x15, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUM, /* Name */
                    0x16, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUN, /* Name */
                    0x17, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUO, /* Name */
                    0x18, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUP, /* Name */
                    0x19, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUQ, /* Name */
                    0x1a, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUR, /* Name */
                    0x1b, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUS, /* Name */
                    0x1c, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUT, /* Name */
                    0x1d, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUU, /* Name */
                    0x1e, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPUV, /* Name */
                    0x1f, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV0, /* Name */
                    0x20, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }

        Processor (CPV1, /* Name */
                    0x21, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV2, /* Name */
                    0x22, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV3, /* Name */
                    0x23, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV4, /* Name */
                    0x24, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV5, /* Name */
                    0x25, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV6, /* Name */
                    0x26, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV7, /* Name */
                    0x27, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV8, /* Name */
                    0x28, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPV9, /* Name */
                    0x29, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVA, /* Name */
                    0x2a, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVB, /* Name */
                    0x2b, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVC, /* Name */
                    0x2c, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVD, /* Name */
                    0x2d, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVE, /* Name */
                    0x2e, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVF, /* Name */
                    0x2f, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVG, /* Name */
                    0x30, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVH, /* Name */
                    0x31, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVI, /* Name */
                    0x32, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVJ, /* Name */
                    0x33, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVK, /* Name */
                    0x34, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVL, /* Name */
                    0x35, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVM, /* Name */
                    0x36, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVN, /* Name */
                    0x37, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVO, /* Name */
                    0x38, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVP, /* Name */
                    0x39, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVQ, /* Name */
                    0x3a, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVR, /* Name */
                    0x3b, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVS, /* Name */
                    0x3c, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVT, /* Name */
                    0x3d, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVU, /* Name */
                    0x3e, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }
        Processor (CPVV, /* Name */
                    0x3f, /* Id */
                    0x0,  /* Processor IO ports range start */
                    0x0   /* Processor IO ports range length */
                    )
        {
        }

    }
}

/*
 * Local Variables:
 * comment-start: "//"
 * End:
 */

