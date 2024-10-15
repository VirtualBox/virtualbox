/* $Id: Docs-RawMode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * This file contains the documentation of the raw-mode execution.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */



/** @page pg_raw                Raw-mode Code Execution
 *
 * VirtualBox 0.0 thru 6.0 implemented a mode of guest code execution that
 * allowed executing mostly raw guest code directly the host CPU but without any
 * support from VT-x or AMD-V.  It was implemented for AMD64, AMD-V and VT-x
 * were available (former) or even specified (latter two).  This mode was
 * removed in 6.1 (code ripped out) as it was mostly unused by that point and
 * not worth the effort of maintaining.
 *
 * A future VirtualBox version may reintroduce a new kind of raw-mode for
 * emulating non-x86 architectures, making use of the host MMU to efficiently
 * emulate the target MMU.  This is just a wild idea at this point.
 *
 *
 * @section sec_old_rawmode     Old Raw-mode
 *
 * Running guest code unmodified on the host CPU is reasonably unproblematic for
 * ring-3 code when it runs without IOPL=3.  There will be some information
 * leaks thru CPUID, a bunch of 286 area unprivileged instructions revealing
 * privileged information (like SGDT, SIDT, SLDT, STR, SMSW), and hypervisor
 * selectors can probably be identified using VERR, VERW and such instructions.
 * However, it generally works fine for half friendly software when the CPUID
 * difference between the target and host isn't too big.
 *
 * Kernel code can be executed on the host CPU too, however it needs to be
 * pushed up a ring (guest ring-0 to ring-1, guest ring-1 to ring2) to let the
 * hypervisor (VMMRC.rc) be in charge of ring-0.   Ring compression causes
 * issues when CS or SS are pushed and inspected by the guest, since the values
 * will have bit 0 set whereas the guest expects that bit to be cleared.  In
 * addition there are problematic instructions like POPF and IRET that the guest
 * code uses to restore/modify EFLAGS.IF state, however the CPU just silently
 * ignores EFLAGS.IF when it isn't running in ring-0 (or with an appropriate
 * IOPL), which causes major headache.  The SIDT, SGDT, STR, SLDT and SMSW
 * instructions also causes problems since they will return information about
 * the hypervisor rather than the guest state and cannot be trapped.
 *
 * So, guest kernel code needed to be scanned (by CSAM) and problematic
 * instructions or sequences patched or recompiled (by PATM).
 *
 * The raw-mode execution operates in a slightly modified guest memory context,
 * so memory accesses can be done directly without any checking or masking.  The
 * modification was to insert the hypervisor in an unused portion of the the
 * page tables, making it float around and require it to be relocated when the
 * guest mapped code into the area it was occupying.
 *
 * The old raw-mode code was 32-bit only because its inception predates the
 * availability of the AMD64 architecture and the promise of AMD-V and VT-x made
 * it unnecessary to do a 64-bit version of the mode.  (A long-mode port of the
 * raw-mode execution hypvisor could in theory have been used for both 32-bit
 * and 64-bit guest, making the relocating unnecessary for 32-bit guests,
 * however v8086 mode does not work when the CPU is operating in long-mode made
 * it a little less attractive.)
 *
 *
 * @section sec_rawmode_v2      Raw-mode v2
 *
 * The vision for the reinvention of raw-mode execution is to put it inside
 * VT-x/AMD-V and run non-native instruction sets via a recompiler.
 *
 * The main motivation is TLB emulation using the host MMU.  An added benefit is
 * would be that the non-native instruction sets would be add-ons put on top of
 * the existing x86/AMD64 virtualization product and therefore not require a
 * complete separate product build.
 *
 *
 * Outline:
 *
 *  - Plug-in based, so the target architecture specific stuff is mostly in
 *    separate modules (ring-3, ring-0 (optional) and raw-mode images).
 *
 *  - Only 64-bit mode code (no problem since VirtualBox requires a 64-bit host
 *    since 6.0).  So, not reintroducing structure alignment pain from old RC.
 *
 *  - Map the RC-hypervisor modules as ROM, using the shadowing feature for the
 *    data sections.
 *
 *  - Use MMIO2-like regions for all the memory that the RC-hypervisor needs,
 *    all shared with the associated host side plug-in components.
 *
 *  - The ROM and MMIO2 regions does not directly end up in the saved state, the
 *    state is instead saved by the ring-3 architecture module.
 *
 *  - Device access thru MMIO mappings could be done transparently thru to the
 *    x86/AMD64 core VMM.  It would however be possible to reintroduce the RC
 *    side device handling, as that will not be removed in the old-RC cleanup.
 *
 *  - Virtual memory managed by the RC-hypervisor, optionally with help of the
 *    ring-3 and/or ring-0 architecture modules.
 *
 *  - The mapping of the RC modules and memory will probably have to runtime
 *    relocatable again, like it was in the old RC.  Though initially and for
 *    32-bit target architectures, we will probably use a fixed mapping.
 *
 *  - Memory accesses must unfortunately be range checked before being issued,
 *    in order to prevent the guest code from accessing the hypervisor.  The
 *    recompiled code must be able to run, modify state, call ROM code, update
 *    statistics and such, so we cannot use page table stuff protect the
 *    hypervisor code & data.  (If long mode implement segment limits, we
 *    could've used that, but it doesn't.)
 *
 *  - The RC-hypervisor will make hypercalls to communicate with the ring-0 and
 *    ring-3 host code.
 *
 *  - The host side should be able to dig out the current guest state from
 *    information (think AMD64 unwinding) stored in translation blocks.
 *
 *  - Non-atomic state updates outside TBs could be flagged so the host know
 *    how to roll the back.
 *
 *  - SMP must be taken into account early on.
 *
 *  - As must existing IEM-based recompiler ideas, preferrably sharing code
 *    (basically compiling IEM targetting the other architecture).
 *
 * The actual implementation will depend a lot on which architectures are
 * targeted and how they can be mapped onto AMD64/x86.  It is possible that
 * there are some significan roadblocks preventing us from using the host MMU
 * efficiently even.  AMD64 is for instance rather low on virtual address space
 * compared to several other 64-bit architectures, which means we'll generate a
 * lot of \#GPs when the guest tries to access spaced reserved on AMD64.  The
 * proposed 5-level page tables will help with this, of course, but that need to
 * get into silicon and into user computers for it to be really helpful.
 *
 * One thing that helps a lot is that we don't have to consider 32-bit x86 any
 * more, meaning that the recompiler only need to generate 64-bit code and can
 * assume having 15-16 GPRs at its disposal.
 *
 */

