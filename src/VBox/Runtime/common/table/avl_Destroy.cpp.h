/* $Id: avl_Destroy.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * kAVLDestroy - Walk the tree calling a callback to destroy all the nodes.
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

#ifndef _kAVLDestroy_h_
#define _kAVLDestroy_h_


/**
 * Destroys the specified tree, starting with the root node and working our way down.
 *
 * @returns 0 on success.
 * @returns Return value from callback on failure. On failure, the tree will be in
 *          an unbalanced condition and only further calls to the Destroy should be
 *          made on it. Note that the node we fail on will be considered dead and
 *          no action is taken to link it back into the tree.
 * @param   ppTree          Pointer to the AVL-tree root node pointer.
 * @param   pfnCallBack     Pointer to callback function.
 * @param   pvUser          User parameter passed on to the callback function.
 */
KAVL_DECL(int) KAVL_FN(Destroy)(PPKAVLNODECORE ppTree, PKAVLCALLBACK pfnCallBack, void *pvUser)
{
    unsigned        cEntries;
    PKAVLNODECORE   apEntries[KAVL_MAX_STACK];
    int             rc;

    if (*ppTree == KAVL_NULL)
        return VINF_SUCCESS;

    cEntries = 1;
    apEntries[0] = KAVL_GET_POINTER(ppTree);
    while (cEntries > 0)
    {
        /*
         * Process the subtrees first.
         */
        PKAVLNODECORE pNode = apEntries[cEntries - 1];
        if (pNode->pLeft != KAVL_NULL)
            apEntries[cEntries++] = KAVL_GET_POINTER(&pNode->pLeft);
        else if (pNode->pRight != KAVL_NULL)
            apEntries[cEntries++] = KAVL_GET_POINTER(&pNode->pRight);
        else
        {
#ifdef KAVL_EQUAL_ALLOWED
            /*
             * Process nodes with the same key.
             */
            while (pNode->pList != KAVL_NULL)
            {
                PKAVLNODECORE pEqual = KAVL_GET_POINTER(&pNode->pList);
                KAVL_SET_POINTER(&pNode->pList, KAVL_GET_POINTER_NULL(&pEqual->pList));
                pEqual->pList = KAVL_NULL;

                rc = pfnCallBack(pEqual, pvUser);
                if (rc != VINF_SUCCESS)
                    return rc;
            }
#endif

            /*
             * Unlink the node.
             */
            if (--cEntries > 0)
            {
                PKAVLNODECORE pParent = apEntries[cEntries - 1];
                if (KAVL_GET_POINTER(&pParent->pLeft) == pNode)
                    pParent->pLeft = KAVL_NULL;
                else
                    pParent->pRight = KAVL_NULL;
            }
            else
                *ppTree = KAVL_NULL;

            kASSERT(pNode->pLeft == KAVL_NULL);
            kASSERT(pNode->pRight == KAVL_NULL);
            rc = pfnCallBack(pNode, pvUser);
            if (rc != VINF_SUCCESS)
                return rc;
        }
    } /* while */

    kASSERT(*ppTree == KAVL_NULL);

    return VINF_SUCCESS;
}

#endif

