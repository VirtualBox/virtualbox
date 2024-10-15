/* $Id: UIManagerDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxManager definitions.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIManagerDefs_h
#define FEQT_INCLUDED_SRC_manager_UIManagerDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** Virtual machine item types. */
enum UIVirtualMachineItemType
{
    UIVirtualMachineItemType_Invalid,
    UIVirtualMachineItemType_Local,
    UIVirtualMachineItemType_CloudFake,
    UIVirtualMachineItemType_CloudReal
};


/** Fake cloud virtual machine item states. */
enum UIFakeCloudVirtualMachineItemState
{
    UIFakeCloudVirtualMachineItemState_NotApplicable,
    UIFakeCloudVirtualMachineItemState_Loading,
    UIFakeCloudVirtualMachineItemState_Done
};


#endif /* !FEQT_INCLUDED_SRC_manager_UIManagerDefs_h */
