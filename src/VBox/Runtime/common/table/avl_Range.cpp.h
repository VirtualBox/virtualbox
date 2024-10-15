/* $Id: avl_Range.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * kAVLRange  - Range routines for AVL trees.
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

#ifndef _kAVLRange_h_
#define _kAVLRange_h_

/**
 * Finds the range containing the specified key.
 *
 * @returns   Pointer to the matching range.
 *
 * @param     ppTree  Pointer to Pointer to the tree root node.
 * @param     Key     The Key to find matching range for.
 */
KAVL_DECL(PKAVLNODECORE) KAVL_FN(RangeGet)(PPKAVLNODECORE ppTree, KAVLKEY Key)
{
    PKAVLNODECORE  pNode = KAVL_GET_POINTER_NULL(ppTree);
    if (pNode)
    {
        for (;;)
        {
            if (KAVL_R_IS_IN_RANGE(pNode->Key, pNode->KeyLast, Key))
                return pNode;
            if (KAVL_G(pNode->Key, Key))
            {
                if (pNode->pLeft != KAVL_NULL)
                    pNode = KAVL_GET_POINTER(&pNode->pLeft);
                else
                    return NULL;
            }
            else
            {
                if (pNode->pRight != KAVL_NULL)
                    pNode = KAVL_GET_POINTER(&pNode->pRight);
                else
                    return NULL;
            }
        }
    }

    return NULL;
}


/**
 * Removes the range containing the specified key.
 *
 * @returns   Pointer to the matching range.
 *
 * @param     ppTree  Pointer to Pointer to the tree root node.
 * @param     Key     The Key to remove matching range for.
 */
RTDECL(PKAVLNODECORE) KAVL_FN(RangeRemove)(PPKAVLNODECORE ppTree, KAVLKEY Key)
{
    PKAVLNODECORE pNode = KAVL_FN(RangeGet)(ppTree, Key);
    if (pNode)
        return KAVL_FN(Remove)(ppTree, pNode->Key);
    return NULL;
}

#endif
