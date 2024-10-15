/* $Id: avl_Enum.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Enumeration routines for AVL trees.
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

#ifndef _kAVLEnum_h_
#define _kAVLEnum_h_


/**
 * Gets the root node.
 *
 * @returns Pointer to the root node.
 * @returns NULL if the tree is empty.
 *
 * @param   ppTree      Pointer to pointer to the tree root node.
 */
KAVL_DECL(PKAVLNODECORE) KAVL_FN(GetRoot)(PPKAVLNODECORE ppTree)
{
    return KAVL_GET_POINTER_NULL(ppTree);
}


/**
 * Gets the right node.
 *
 * @returns Pointer to the right node.
 * @returns NULL if no right node.
 *
 * @param   pNode       The current node.
 */
KAVL_DECL(PKAVLNODECORE)    KAVL_FN(GetRight)(PKAVLNODECORE pNode)
{
    if (pNode)
        return KAVL_GET_POINTER_NULL(&pNode->pRight);
    return NULL;
}


/**
 * Gets the left node.
 *
 * @returns Pointer to the left node.
 * @returns NULL if no left node.
 *
 * @param   pNode       The current node.
 */
KAVL_DECL(PKAVLNODECORE) KAVL_FN(GetLeft)(PKAVLNODECORE pNode)
{
    if (pNode)
        return KAVL_GET_POINTER_NULL(&pNode->pLeft);
    return NULL;
}


# ifdef KAVL_EQUAL_ALLOWED
/**
 * Gets the next node with an equal (start) key.
 *
 * @returns Pointer to the next equal node.
 * @returns NULL if the current node was the last one with this key.
 *
 * @param   pNode       The current node.
 */
KAVL_DECL(PKAVLNODECORE) KAVL_FN(GetNextEqual)(PKAVLNODECORE pNode)
{
    if (pNode)
        return KAVL_GET_POINTER_NULL(&pNode->pList);
    return NULL;
}
# endif /* KAVL_EQUAL_ALLOWED */

#endif

