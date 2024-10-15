/* $Id: IEMAllInst3DNow.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation, 3DNow!.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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


/** @name 3DNow! instructions (0x0f 0x0f)
 *
 * @{
 */

/** Opcode 0x0f 0x0f 0x0c. */
FNIEMOP_STUB(iemOp_3Dnow_pi2fw_Pq_Qq);

/** Opcode 0x0f 0x0f 0x0d. */
FNIEMOP_STUB(iemOp_3Dnow_pi2fd_Pq_Qq);

/** Opcode 0x0f 0x0f 0x1c. */
FNIEMOP_STUB(iemOp_3Dnow_pf2fw_Pq_Qq);

/** Opcode 0x0f 0x0f 0x1d. */
FNIEMOP_STUB(iemOp_3Dnow_pf2fd_Pq_Qq);

/** Opcode 0x0f 0x0f 0x8a. */
FNIEMOP_STUB(iemOp_3Dnow_pfnacc_Pq_Qq);

/** Opcode 0x0f 0x0f 0x8e. */
FNIEMOP_STUB(iemOp_3Dnow_pfpnacc_Pq_Qq);

/** Opcode 0x0f 0x0f 0x90. */
FNIEMOP_STUB(iemOp_3Dnow_pfcmpge_Pq_Qq);

/** Opcode 0x0f 0x0f 0x94. */
FNIEMOP_STUB(iemOp_3Dnow_pfmin_Pq_Qq);

/** Opcode 0x0f 0x0f 0x96. */
FNIEMOP_STUB(iemOp_3Dnow_pfrcp_Pq_Qq);

/** Opcode 0x0f 0x0f 0x97. */
FNIEMOP_STUB(iemOp_3Dnow_pfrsqrt_Pq_Qq);

/** Opcode 0x0f 0x0f 0x9a. */
FNIEMOP_STUB(iemOp_3Dnow_pfsub_Pq_Qq);

/** Opcode 0x0f 0x0f 0x9e. */
FNIEMOP_STUB(iemOp_3Dnow_pfadd_PQ_Qq);

/** Opcode 0x0f 0x0f 0xa0. */
FNIEMOP_STUB(iemOp_3Dnow_pfcmpgt_Pq_Qq);

/** Opcode 0x0f 0x0f 0xa4. */
FNIEMOP_STUB(iemOp_3Dnow_pfmax_Pq_Qq);

/** Opcode 0x0f 0x0f 0xa6. */
FNIEMOP_STUB(iemOp_3Dnow_pfrcpit1_Pq_Qq);

/** Opcode 0x0f 0x0f 0xa7. */
FNIEMOP_STUB(iemOp_3Dnow_pfrsqit1_Pq_Qq);

/** Opcode 0x0f 0x0f 0xaa. */
FNIEMOP_STUB(iemOp_3Dnow_pfsubr_Pq_Qq);

/** Opcode 0x0f 0x0f 0xae. */
FNIEMOP_STUB(iemOp_3Dnow_pfacc_PQ_Qq);

/** Opcode 0x0f 0x0f 0xb0. */
FNIEMOP_STUB(iemOp_3Dnow_pfcmpeq_Pq_Qq);

/** Opcode 0x0f 0x0f 0xb4. */
FNIEMOP_STUB(iemOp_3Dnow_pfmul_Pq_Qq);

/** Opcode 0x0f 0x0f 0xb6. */
FNIEMOP_STUB(iemOp_3Dnow_pfrcpit2_Pq_Qq);

/** Opcode 0x0f 0x0f 0xb7. */
FNIEMOP_STUB(iemOp_3Dnow_pmulhrw_Pq_Qq);

/** Opcode 0x0f 0x0f 0xbb. */
FNIEMOP_STUB(iemOp_3Dnow_pswapd_Pq_Qq);

/** Opcode 0x0f 0x0f 0xbf. */
FNIEMOP_STUB(iemOp_3Dnow_pavgusb_PQ_Qq);


/** Opcode 0x0f 0x0f. */
FNIEMOP_DEF_1(iemOp_3DNowDispatcher, uint8_t, b)
{
    /* This is pretty sparse, use switch instead of table. */
    switch (b)
    {
        case 0x0c: return FNIEMOP_CALL(iemOp_3Dnow_pi2fw_Pq_Qq);
        case 0x0d: return FNIEMOP_CALL(iemOp_3Dnow_pi2fd_Pq_Qq);
        case 0x1c: return FNIEMOP_CALL(iemOp_3Dnow_pf2fw_Pq_Qq);
        case 0x1d: return FNIEMOP_CALL(iemOp_3Dnow_pf2fd_Pq_Qq);
        case 0x8a: return FNIEMOP_CALL(iemOp_3Dnow_pfnacc_Pq_Qq);
        case 0x8e: return FNIEMOP_CALL(iemOp_3Dnow_pfpnacc_Pq_Qq);
        case 0x90: return FNIEMOP_CALL(iemOp_3Dnow_pfcmpge_Pq_Qq);
        case 0x94: return FNIEMOP_CALL(iemOp_3Dnow_pfmin_Pq_Qq);
        case 0x96: return FNIEMOP_CALL(iemOp_3Dnow_pfrcp_Pq_Qq);
        case 0x97: return FNIEMOP_CALL(iemOp_3Dnow_pfrsqrt_Pq_Qq);
        case 0x9a: return FNIEMOP_CALL(iemOp_3Dnow_pfsub_Pq_Qq);
        case 0x9e: return FNIEMOP_CALL(iemOp_3Dnow_pfadd_PQ_Qq);
        case 0xa0: return FNIEMOP_CALL(iemOp_3Dnow_pfcmpgt_Pq_Qq);
        case 0xa4: return FNIEMOP_CALL(iemOp_3Dnow_pfmax_Pq_Qq);
        case 0xa6: return FNIEMOP_CALL(iemOp_3Dnow_pfrcpit1_Pq_Qq);
        case 0xa7: return FNIEMOP_CALL(iemOp_3Dnow_pfrsqit1_Pq_Qq);
        case 0xaa: return FNIEMOP_CALL(iemOp_3Dnow_pfsubr_Pq_Qq);
        case 0xae: return FNIEMOP_CALL(iemOp_3Dnow_pfacc_PQ_Qq);
        case 0xb0: return FNIEMOP_CALL(iemOp_3Dnow_pfcmpeq_Pq_Qq);
        case 0xb4: return FNIEMOP_CALL(iemOp_3Dnow_pfmul_Pq_Qq);
        case 0xb6: return FNIEMOP_CALL(iemOp_3Dnow_pfrcpit2_Pq_Qq);
        case 0xb7: return FNIEMOP_CALL(iemOp_3Dnow_pmulhrw_Pq_Qq);
        case 0xbb: return FNIEMOP_CALL(iemOp_3Dnow_pswapd_Pq_Qq);
        case 0xbf: return FNIEMOP_CALL(iemOp_3Dnow_pavgusb_PQ_Qq);
        default:
            IEMOP_RAISE_INVALID_OPCODE_RET();
    }
}

/** @} */

