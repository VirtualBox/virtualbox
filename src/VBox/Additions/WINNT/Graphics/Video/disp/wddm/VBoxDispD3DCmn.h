/* $Id: VBoxDispD3DCmn.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User mode dll
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3DCmn_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3DCmn_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxDispD3DBase.h"

#include <iprt/asm.h>
#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/mem.h>

#include <VBox/Log.h>

#include <VBox/VBoxGuestLib.h>

#include "VBoxDispDbg.h"
#include "VBoxDispD3DIf.h"
#include "../../common/wddm/VBoxMPIf.h"
#include "VBoxDispMpInternal.h"
#include <VBoxDispKmt.h>
#include "VBoxDispD3D.h"
#include "VBoxD3DIf.h"

# ifdef VBOXWDDMDISP
#  define VBOXWDDMDISP_DECL(_type) DECLEXPORT(_type)
# else
#  define VBOXWDDMDISP_DECL(_type) DECLIMPORT(_type)
# endif

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3DCmn_h */
