/* $Id: VBox-doc.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Top Level Documentation File.
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


/** @mainpage   VirtualBox
 *
 * (add introduction here)
 *
 * @section pg_main_comp    Components
 *
 *  - VM / @ref pg_vmm "VMM" / GVM / @ref pg_gvmm "GVMM" - Virtual Machine
 *    Monitor.
 *      - @ref pg_cfgm
 *      - @ref pg_cpum
 *      - @ref pg_dbgf
 *          - @ref pg_dbgf_addr_space
 *          - @ref pg_dbgf_vmcore
 *          - @ref pg_dbgf_module
 *          - @ref pg_dbgc
 *          - VBoxDbg - Debugger GUI (Qt).
 *      - @ref grp_dis
 *      - @ref pg_em
 *      - @ref pg_gim
 *      - @ref pg_hm
 *      - @ref pg_iem
 *      - @ref pg_nem
 *      - @ref pg_gmm
 *          - @ref pg_mm
 *          - @ref pg_pgm
 *              - @ref pg_pgm_phys
 *              - @ref pg_pgm_pool
 *          - @ref pg_selm
 *      - @ref pg_iom
 *      - @ref pg_pdm
 *          - Devices / USB Devices, Drivers and their public interfaces.
 *          - Async I/O Completion API.
 *          - Async Task API.
 *          - Critical Section API.
 *          - Queue API.
 *          - Thread API.
 *          - @ref pg_pdm_block_cache
 *      - @ref pg_ssm
 *      - @ref pg_stam
 *      - @ref pg_tm
 *      - @ref pg_trpm
 *      - VMM docs:
 *          - @ref pg_vmm_guideline
 *          - @ref pg_raw
 *  - Pluggable Components (via PDM).
 *      - DevPCArch - PC Architecture Device (chipset, legacy ++).
 *      - DevPCBios - Basic Input Output System.
 *      - DevDMAC - DMA Controller.
 *      - DevPIC - Programmable Interrupt Controller.
 *      - DevPIT - Programmable Interval Timer (i8254).
 *      - DevRTC - Real Time Clock.
 *      - DevVGA - Video Graphic Array.
 *      - DevPCI - Peripheral Component Interface (Bus).
 *      - VBoxDev - Special PCI Device which serves as an interface between
 *                  the VMM and the guest OS for the additions.
 *      - @ref pg_pdm_audio "Audio":
 *          - DevHda - Intel High Definition Audio Device Emulation.
 *          - DevIchAc97 - ICH AC'97 Device Emulation.
 *          - DevSB16 - SoundBlaster 16 Device Emulation.
 *          - DrvAudio - Intermediate driver.
 *          - DrvHostAudioAlsa - ALSA Host Audio Driver (Linux).
 *          - DrvHostAudioCoreAudio - Core Audio Host Audio Driver (macOS).
 *          - DrvHostAudioDebug - Debug Backend Driver.
 *          - DrvHostAudioDSound - DirectSound Host Audio Driver (Windows).
 *          - DrvHostAudioNull - NULL Backend Driver.
 *          - DrvHostAudioOss - Open Sound System Host Audio Driver (Linux,
 *            Solaris, ++).
 *          - DrvHostAudioPulseAudio - PulseAudio Host Audio Driver (Linux).
 *          - DrvHostAudioValidationKit - Validation Kit Test Driver.
 *          - DrvHostAudioWasApi - Windows Audio Session API Host Audio Driver.
 *      - Networking:
 *          - DevPCNet - AMD PCNet Device Emulation.
 *          - DevE1000 - Intel E1000 Device Emulation.
 *          - DevEEPROM - Intel E1000 EPROM Device Emulation.
 *          - SrvINetNetR0 - Internal Networking Ring-0 Service.
 *          - DevINIP - IP Stack Service for the internal networking.
 *          - DrvIntNet - Internal Networking Driver.
 *          - DrvNetSniffer - Wireshark Compatible Sniffer Driver (pass thru).
 *          - DrvNAT - Network Address Translation Driver.
 *          - DrvTAP - Host Interface Networking Driver.
 *      - Storage:
 *          - DevATA - ATA ((E)IDE) Device Emulation.
 *          - @ref pg_dev_ahci
 *          - DevFDC - Floppy Controller Device Emulation.
 *          - DrvBlock - Intermediate block driver.
 *          - DrvHostBase - Common code for the host drivers.
 *          - DrvHostDVD - Host DVD drive driver.
 *          - DrvHostFloppy - Host floppy drive driver.
 *          - DrvHostRawDisk - Host raw disk drive driver.
 *          - DrvMediaISO - ISO media driver.
 *          - DrvRawImage - Raw image driver (floppy images etc).
 *          - DrvVD - Intermediate Virtual Drive (Media) driver.
 *          - DrvVDI - VirtualBox Drive Image Container Driver.
 *          - DrvVmdk - VMDK Drive Image Container Driver.
 *      - USB:
 *          - @ref pg_dev_ohci
 *          - @ref pg_dev_ehci
 *          - @ref pg_dev_vusb
 *          - @ref pg_dev_vusb_old
 *  - Host Drivers.
 *      - SUPDRV - The Support driver (aka VBoxDrv).
 *          - @ref pg_sup
 *      - @ref pg_netflt
 *      - @ref pg_netadp
 *      - VBoxUSB - The USB support driver.
 *      - @ref pg_netflt
 *      - @ref pg_rawpci
 *  - Host Services.
 *      - @ref pg_hostclip
 *      - Shared Folders.
 *      - @ref pg_svc_guest_properties
 *      - @ref pg_svc_guest_control
 *  - Guest Additions.
 *      - VBoxGuest.
 *          - @ref pg_guest_lib
 *      - @ref pg_vgsvc
 *          - @ref pg_vgsvc_timesync
 *          - @ref pg_vgsvc_vminfo
 *          - @ref pg_vgsvc_vmstats
 *          - @ref pg_vgsvc_gstctrl
 *          - @ref pg_vgsvc_pagesharing
 *          - @ref pg_vgsvc_memballoon
 *          - @ref pg_vgsvc_cpuhotplug
 *          - @ref pg_vgsvc_automount
 *          - @ref pg_vgsvc_clipboard
 *      - VBoxControl.
 *      - Linux, Solaris and FreeBSD specific guest services and drivers.
 *          - @ref pg_vboxdrmcliet (Linux only).
 *          - VBoxClient.
 *          - VBoxVideo.
 *      - Windows Guests.
 *          - VBoxTray.
 *      - crOpenGL.
 *      - pam.
 *      - ...
 *  - Network Services:
 *      - @ref pg_net_dhcp
 *      - NAT
 *  - @ref pg_main
 *      - @ref pg_main_events
 *      - @ref pg_vrdb_usb
 *  - Frontends:
 *      - VirtualBox - The default Qt-based GUI.
 *      - VBoxHeadless - The headless frontend.
 *      - VBoxManage - The CLI.
 *      - VBoxShell - An interactive shell written in python.
 *      - VBoxSDL - A very simple GUI.
 *      - VBoxBFE - A bare metal edition which does not use COM/XPCOM (barely
 *        maintained atm).
 *  - IPRT - Runtime Library for hiding host OS differences.
 *  - Validation Kit:
 *      - @ref pg_validationkit_guideline
 *      - @ref pg_bs3kit
 *  - @ref pg_vbox_guideline
 *
 * @todo Make links to the components.
 *
 *
 *
 * @section Execution Contexts
 *
 * VirtualBox defines a number of different execution context, this can be
 * confusing at first.  So, to start with take a look at this diagram:
 *
 * @image html VMMContexts.png
 *
 * Context definitions:
 *      - Host context (HC) - This is the context where the host OS runs and
 *        runs VirtualBox within it.  The absense of IN_RC and IN_GUEST
 *        indicates that we're in HC.  IN_RING0 indicates ring-0 (kernel) and
 *        IN_RING3 indicates ring-3.
 *      - Raw-mode Context (RC) - This is the special VMM context where we
 *        execute the guest code directly on the CPU.  Kernel code is patched
 *        and execute in ring-1 instead of ring-0 (ring compression).  Ring-3
 *        code execute unmodified.  Only VMMs use ring-1, so we don't need to
 *        worry about that (it's guarded against in the scheduler (EM)).  We can
 *        in theory run ring-2 there, but since practially only only OS/2 uses
 *        ring-2, it is of little importance.  The macro IN_RC indicates that
 *        we're compiling something for RC.
 *        Note! This used to be called GC (see below) earlier, so a bunch of RC
 *        things are using GC markers.
 *      - Guest Context (GC) - This is where the guest code is executed.  When
 *        compiling, IN_GUEST indicates that it's for GC.  IN_RING0 and
 *        IN_RING3 are also set when applicable, these are accompanied by
 *        IN_GUEST_R0 and IN_GUEST_R3 respecitively.
 *      - Intermediate context - This is a special memory context used within
 *        the world switchers (HC -> RC and back), it features some identity
 *        mapped code pages so we can switch to real mode if necessary.
 *
 */

