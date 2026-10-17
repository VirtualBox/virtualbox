/* $Id: PGMR3PhysRWTmpl.h 115384 2026-10-17 13:31:43Z aleksey.ilyushin@oracle.com $ */
/** @file
 * PGM - Page Manager and Monitor, Physical Memory Access Template.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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


/**
 * Read physical memory. (one byte/word/dword)
 *
 * This API respects access handlers and MMIO. Use PGMPhysSimpleReadGCPhys() if you
 * want to ignore those.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Physical address start reading from.
 * @param   fOrigin         Who is calling.
 */
VMMDECL(PGMPHYS_DATATYPE) PGMPHYSFN_READNAME(PVM pVM, RTGCPHYS GCPhys, PGMACCESSORIGIN fOrigin)
{
    Assert(VM_IS_EMT(pVM));
    PGMPHYS_DATATYPE val;
    VBOXSTRICTRC rcStrict = PGMPhysRead(pVM, GCPhys, &val, sizeof(val), fOrigin);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); NOREF(rcStrict);
    return val;
}


/**
 * Write to physical memory. (one byte/word/dword)
 *
 * This API respects access handlers and MMIO. Use PGMPhysSimpleReadGCPhys() if you
 * want to ignore those.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Physical address to write to.
 * @param   val             What to write.
 * @param   fOrigin       Who is calling.
 */
VMMDECL(void) PGMPHYSFN_WRITENAME(PVM pVM, RTGCPHYS GCPhys, PGMPHYS_DATATYPE val, PGMACCESSORIGIN fOrigin)
{
    Assert(VM_IS_EMT(pVM));
    VBOXSTRICTRC rcStrict = PGMPhysWrite(pVM, GCPhys, &val, sizeof(val), fOrigin);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); NOREF(rcStrict);
}

#undef PGMPHYSFN_READNAME
#undef PGMPHYSFN_WRITENAME
#undef PGMPHYS_DATATYPE
#undef PGMPHYS_DATASIZE

