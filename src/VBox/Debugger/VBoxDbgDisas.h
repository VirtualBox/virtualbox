/* $Id: VBoxDbgDisas.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Debugger GUI - Disassembly View.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef DEBUGGER_INCLUDED_SRC_VBoxDbgDisas_h
#define DEBUGGER_INCLUDED_SRC_VBoxDbgDisas_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/**
 * Feature list:
 *  - Combobox (or some entry field with history similar to the command) for
 *    entering the address. Should support registers and other symbols, and
 *    possibly also grok various operators like the debugger command line.
 *    => Needs to make the DBGC evaluator available somehow.
 *  - Refresh manual/interval (for EIP or other non-fixed address expression).
 *  - Scrollable - down is not an issue, up is a bit more difficult.
 *  - Hide/Unhide PATM patches (jumps/int3/whatever) button in the guest disas.
 *  - Drop down for selecting mixed original guest disas and PATM/REM disas
 *    (Guest Only, Guest+PATM, Guest+REM).
 *
 */
class VBoxDbgDisas
{

};

#endif /* !DEBUGGER_INCLUDED_SRC_VBoxDbgDisas_h */

