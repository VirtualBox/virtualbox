/* $Id: MachineImplCloneVM.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Definition of MachineCloneVM
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

#ifndef MAIN_INCLUDED_MachineImplCloneVM_h
#define MAIN_INCLUDED_MachineImplCloneVM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "MachineImpl.h"
#include "ProgressImpl.h"

/* Forward declaration of the d-pointer. */
struct MachineCloneVMPrivate;

class MachineCloneVM
{
public:
    DECLARE_TRANSLATE_METHODS(MachineCloneVM)

    MachineCloneVM(ComObjPtr<Machine> pSrcMachine, ComObjPtr<Machine> pTrgMachine, CloneMode_T mode, const RTCList<CloneOptions_T> &opts);
    ~MachineCloneVM();

    HRESULT start(IProgress **pProgress);

protected:
    HRESULT run();
    void destroy();

    /* d-pointer */
    MachineCloneVM(MachineCloneVMPrivate &d);
    MachineCloneVMPrivate *d_ptr;

    friend struct MachineCloneVMPrivate;
};

#endif /* !MAIN_INCLUDED_MachineImplCloneVM_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */

