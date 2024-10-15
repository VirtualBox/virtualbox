/* $Id: Docs-Intro.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * This file contains the introduction to Main for developers.
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

/** @page pg_main       Main API
 *
 * First of all, check out the "Technical background" chapter in the manual, pay
 * attention to the "VirtualBox executables and components" chapter.  It lists
 * three processes, (1) VBoxSVC, (2) VirtualBox in manager mode and (3)
 * VirtualBox in VM mode.  This will be referred to as (1) server, (2) client
 * and (3) VM process, respectively.
 *
 *
 * @section sec_main_walk_thru_suspend  IConsole::Pause Walkthru
 *
 * The instigator can be a client (VirtualBox in manager mode, VBoxManage
 * controlvm, web services, ++) or the VM process it self (i.e. you select
 * pause via the menu or the host key short cut).
 *
 * We will not cover the case where the guest triggers a suspend.
 *
 * Approximate sequence of events:
 *  - Client calls IConsole::Pause.
 *  - The COM/XPCOM routes this to the VM process, invoking Console::Pause() in
 *    ConsoleImpl.cpp. (The IConsole::Pause method in the client process is a
 *    COM/XPCOM stub method which does marshalling+IPC.)
 *  - Console::Pause validates the Console object state, the VM state and the VM
 *    handle.
 *  - Console::Pause calls VMR3Suspend to do the actual suspending.
 *  - VMR3Suspend() in VMM/VMMR3/VM.cpp calls VMMR3EmtRendezvous() to change the
 *    VM state synchronously on all EMTs (threads performing as virtual CPUs).
 *  - VMMR3EmtRendezvous() will first detect that the caller isn't an EMT and
 *    use VMR3ReqCallWait() to forward the call to an EMT.
 *  - When VMMR3EmtRendezvous() is called again on an EMT, it will signal the
 *    other EMTs by raising a force action flag (VM_FF_EMT_RENDEZVOUS) and then
 *    poke them via VMR3NotifyGlobalFFU(). Then wait for them all to arrive.
 *  - The other EMTs will call VMMR3EmtRendezvousFF as soon as they can.
 *  - When all EMTs are there, the calling back of vmR3Suspend() on each CPU in
 *    decending order will start.
 *  - When the CPU with the higest ID calls vmR3Suspend() the VM state is
 *    changed to VMSTATE_SUSPENDING or VMSTATE_SUSPENDING_EXT_LS.
 *  - When the CPU with ID 0 calls vmR3Suspend() the virtual device emulations
 *    and drivers get notified via PDMR3Suspend().
 *  - PDMR3Suspend() in VMM/VMMR3/PDM.cpp will iterate thru all device
 *    emulations and notify them that the VM is suspending by calling their
 *    PDMDEVREG::pfnSuspend / PDMUSBREG::pfnSuspend entry point (can be NULL).
 *    For each device it will iterate the chains of drivers and call their
 *    PDMDRVREG::pfnSuspend entry point as well.
 *  - Should a worker thread in a PDM device or PDM driver be busy and need some
 *    extra time to finish up / notice the pending suspend, the device or driver
 *    will ask for more time via PDMDevHlpSetAsyncNotification(),
 *    PDMDrvHlpSetAsyncNotification() or PDMUsbHlpSetAsyncNotification().
 *    PDMR3Suspend will then poll these devices and drivers frequently until all
 *    are done.
 *  - PDMR3Suspend() will return to vmR3Suspend() once all PDM devices and PDM
 *    drivers has responded to the pfnSuspend callback.
 *  - The virtual CPU with ID 0 returns from vmR3Suspend() to the rendezvous
 *    code and the EMTs are released.
 *  - The inner VMMR3EmtRendezvous() call returns and this in turn triggers the
 *    VMR3ReqCallWait() call to return (with the status code of the inner call).
 *  - The outer VMMR3EmtRendezvous() returns to VMR3Suspend().
 *  - VMR3Suspend() returns to Console::Pause().
 *  - Console::Pause() checks the result and flags provides error details on
 *    failure.
 *  - Console::Pause() returns to the COM/XPCOM marshalling/IPC stuff.
 *  - Switch back to client process.
 *  - The IConsole::Pause() call returns. The end.
 *
 * Summary of above: Client process calls into the VM process, VM process does a
 * bunch of inter thread calls with all the EMT, EMT0 suspends the PDM devices
 * and drivers.
 *
 * The EMTs will return to the outer execution loop, vmR3EmulationThreadWithId()
 * in VMM/VMMR3/VMEmt.cpp, where they will mostly do sleep.  They will not
 * execute any guest code until VMR3Resume() is called.
 *
 */

