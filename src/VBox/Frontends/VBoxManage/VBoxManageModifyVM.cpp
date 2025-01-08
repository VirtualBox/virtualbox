/* $Id: VBoxManageModifyVM.cpp 107610 2025-01-08 21:14:58Z jack.doherty@oracle.com $ */
/** @file
 * VBoxManage - Implementation of modifyvm command.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/cidr.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/getopt.h>
#include <VBox/log.h>
#include "VBoxManage.h"
#include "VBoxManageUtils.h"

DECLARE_TRANSLATION_CONTEXT(ModifyVM);

using namespace com;
/** @todo refine this after HDD changes; MSC 8.0/64 has trouble with handleModifyVM.  */
#if defined(_MSC_VER)
# pragma optimize("g", off)
# if _MSC_VER < RT_MSC_VER_VC120
#  pragma warning(disable:4748)
# endif
#endif

enum
{
    MODIFYVM_NAME = 1000,
    MODIFYVM_GROUPS,
    MODIFYVM_DESCRIPTION,
    MODIFYVM_OSTYPE,
    MODIFYVM_ICONFILE,
    MODIFYVM_MEMORY,
    MODIFYVM_PAGEFUSION,
    MODIFYVM_VRAM,
    MODIFYVM_FIRMWARE,
    MODIFYVM_ACPI,
    MODIFYVM_IOAPIC,
    MODIFYVM_CPUID_PORTABILITY,
    MODIFYVM_PARAVIRTPROVIDER,
    MODIFYVM_PARAVIRTDEBUG,
    MODIFYVM_CPUS,
    MODIFYVM_CPUHOTPLUG,
    MODIFYVM_CPU_PROFILE,
    MODIFYVM_PLUGCPU,
    MODIFYVM_UNPLUGCPU,
    MODIFYVM_GRAPHICSCONTROLLER,
    MODIFYVM_MONITORCOUNT,
    MODIFYVM_ACCELERATE3D,
#ifdef VBOX_WITH_VIDEOHWACCEL
    MODIFYVM_ACCELERATE2DVIDEO,
#endif
    /*
     * Firmware-specific stuff.
     */
    MODIFYVM_FWLOGOFADEIN,
    MODIFYVM_FWLOGOFADEOUT,
    MODIFYVM_FWLOGODISPLAYTIME,
    MODIFYVM_FWLOGOIMAGEPATH,
    MODIFYVM_FWBOOTMENU,
    MODIFYVM_FWAPIC,
    MODIFYVM_FWSYSTEMTIMEOFFSET,
    MODIFYVM_FWPXEDEBUG,
    MODIFYVM_SYSTEMUUIDLE,
    MODIFYVM_FWAUTOSERIALNUMGEN,
    MODIFYVM_BOOT,
    MODIFYVM_HDA,                // deprecated
    MODIFYVM_HDB,                // deprecated
    MODIFYVM_HDD,                // deprecated
    MODIFYVM_IDECONTROLLER,      // deprecated
    MODIFYVM_SATAPORTCOUNT,      // deprecated
    MODIFYVM_SATAPORT,           // deprecated
    MODIFYVM_SATA,               // deprecated
    MODIFYVM_SCSIPORT,           // deprecated
    MODIFYVM_SCSITYPE,           // deprecated
    MODIFYVM_SCSI,               // deprecated
    MODIFYVM_DVDPASSTHROUGH,     // deprecated
    MODIFYVM_DVD,                // deprecated
    MODIFYVM_FLOPPY,             // deprecated
    MODIFYVM_NICTRACEFILE,
    MODIFYVM_NICTRACE,
    MODIFYVM_NICPROPERTY,
    MODIFYVM_NICTYPE,
    MODIFYVM_NICSPEED,
    MODIFYVM_NICBOOTPRIO,
    MODIFYVM_NICPROMISC,
    MODIFYVM_NICBWGROUP,
    MODIFYVM_NIC,
    MODIFYVM_CABLECONNECTED,
    MODIFYVM_BRIDGEADAPTER,
#ifdef VBOX_WITH_CLOUD_NET
    MODIFYVM_CLOUDNET,
#endif /* VBOX_WITH_CLOUD_NET */
    MODIFYVM_HOSTONLYADAPTER,
#ifdef VBOX_WITH_VMNET
    MODIFYVM_HOSTONLYNET,
#endif /* VBOX_WITH_VMNET */
    MODIFYVM_INTNET,
    MODIFYVM_GENERICDRV,
    MODIFYVM_NATNETWORKNAME,
    MODIFYVM_NATNET,
    MODIFYVM_NATBINDIP,
    MODIFYVM_NATSETTINGS,
    MODIFYVM_NATPF,
    MODIFYVM_NATALIASMODE,
    MODIFYVM_NATTFTPPREFIX,
    MODIFYVM_NATTFTPFILE,
    MODIFYVM_NATTFTPSERVER,
    MODIFYVM_NATDNSPASSDOMAIN,
    MODIFYVM_NATDNSPROXY,
    MODIFYVM_NATDNSHOSTRESOLVER,
    MODIFYVM_NATLOCALHOSTREACHABLE,
    MODIFYVM_NATFORWARDBROADCAST,
    MODIFYVM_MACADDRESS,
    MODIFYVM_HIDPTR,
    MODIFYVM_HIDKBD,
    MODIFYVM_UARTMODE,
    MODIFYVM_UARTTYPE,
    MODIFYVM_UART,
#if defined(RT_OS_LINUX) || defined(RT_OS_WINDOWS)
    MODIFYVM_LPTMODE,
    MODIFYVM_LPT,
#endif
    MODIFYVM_GUESTMEMORYBALLOON,
    MODIFYVM_AUDIOCONTROLLER,
    MODIFYVM_AUDIOCODEC,
    MODIFYVM_AUDIODRIVER,
    MODIFYVM_AUDIOENABLED,
    MODIFYVM_AUDIO,                   /* Deprecated; remove in the next major version. */
    MODIFYVM_AUDIOIN,
    MODIFYVM_AUDIOOUT,
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    MODIFYVM_CLIPBOARD_MODE,
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    MODIFYVM_CLIPBOARD_FILE_TRANSFERS,
# endif
#endif
    MODIFYVM_DRAGANDDROP,
    MODIFYVM_VRDPPORT,                /* VRDE: deprecated */
    MODIFYVM_VRDPADDRESS,             /* VRDE: deprecated */
    MODIFYVM_VRDPAUTHTYPE,            /* VRDE: deprecated */
    MODIFYVM_VRDPMULTICON,            /* VRDE: deprecated */
    MODIFYVM_VRDPREUSECON,            /* VRDE: deprecated */
    MODIFYVM_VRDPVIDEOCHANNEL,        /* VRDE: deprecated */
    MODIFYVM_VRDPVIDEOCHANNELQUALITY, /* VRDE: deprecated */
    MODIFYVM_VRDP,                    /* VRDE: deprecated */
    MODIFYVM_VRDEPROPERTY,
    MODIFYVM_VRDEPORT,
    MODIFYVM_VRDEADDRESS,
    MODIFYVM_VRDEAUTHTYPE,
    MODIFYVM_VRDEAUTHLIBRARY,
    MODIFYVM_VRDEMULTICON,
    MODIFYVM_VRDEREUSECON,
    MODIFYVM_VRDEVIDEOCHANNEL,
    MODIFYVM_VRDEVIDEOCHANNELQUALITY,
    MODIFYVM_VRDE_EXTPACK,
    MODIFYVM_VRDE,
    MODIFYVM_RTCUSEUTC,
    MODIFYVM_USBRENAME,
    MODIFYVM_USBXHCI,
    MODIFYVM_USBEHCI,
    MODIFYVM_USBOHCI,
    MODIFYVM_SNAPSHOTFOLDER,
    MODIFYVM_TELEPORTER_ENABLED,
    MODIFYVM_TELEPORTER_PORT,
    MODIFYVM_TELEPORTER_ADDRESS,
    MODIFYVM_TELEPORTER_PASSWORD,
    MODIFYVM_TELEPORTER_PASSWORD_FILE,
    MODIFYVM_TRACING_ENABLED,
    MODIFYVM_TRACING_CONFIG,
    MODIFYVM_TRACING_ALLOW_VM_ACCESS,
    MODIFYVM_HARDWARE_UUID,
    MODIFYVM_IOCACHE,
    MODIFYVM_IOCACHESIZE,
    MODIFYVM_CPU_EXECTUION_CAP,
    MODIFYVM_AUTOSTART_ENABLED,
    MODIFYVM_AUTOSTART_DELAY,
    MODIFYVM_AUTOSTOP_TYPE,
#ifdef VBOX_WITH_PCI_PASSTHROUGH
    MODIFYVM_ATTACH_PCI,
    MODIFYVM_DETACH_PCI,
#endif
#ifdef VBOX_WITH_USB_CARDREADER
    MODIFYVM_USBCARDREADER,
#endif
#ifdef VBOX_WITH_RECORDING
    MODIFYVM_RECORDING,
    MODIFYVM_RECORDING_FEATURES,
    MODIFYVM_RECORDING_SCREENS,
    MODIFYVM_RECORDING_FILENAME,
    MODIFYVM_RECORDING_VIDEO_WIDTH,
    MODIFYVM_RECORDING_VIDEO_HEIGHT,
    MODIFYVM_RECORDING_VIDEO_RES,
    MODIFYVM_RECORDING_VIDEO_RATE,
    MODIFYVM_RECORDING_VIDEO_FPS,
    MODIFYVM_RECORDING_MAXTIME,
    MODIFYVM_RECORDING_MAXSIZE,
    MODIFYVM_RECORDING_OPTIONS,
#endif
    MODIFYVM_CHIPSET,
#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    MODIFYVM_IOMMU,
#endif
#if defined(VBOX_WITH_TPM)
    MODIFYVM_TPM_LOCATION,
    MODIFYVM_TPM_TYPE,
#endif
    MODIFYVM_DEFAULTFRONTEND,
    MODIFYVM_VMPROC_PRIORITY,
    MODIFYVM_VMEXEC_ENGINE,
    MODIFYVM_TESTING_ENABLED,
    MODIFYVM_TESTING_MMIO,
    MODIFYVM_TESTING_CFG_DWORD,
    MODIFYVM_GUEST_DEBUG_PROVIDER,
    MODIFYVM_GUEST_DEBUG_IO_PROVIDER,
    MODIFYVM_GUEST_DEBUG_ADDRESS,
    MODIFYVM_GUEST_DEBUG_PORT,
    /*
     * x86-specific stuff.
     */
    MODIFYVM_X86_APIC,
    MODIFYVM_X86_DELCPUID,
    MODIFYVM_X86_DELCPUID_OLD,      // legacy, different syntax from MODIFYVM_DELCPUID
    MODIFYVM_X86_DELALLCPUID,
    MODIFYVM_X86_HPET,
    MODIFYVM_X86_HWVIRTEX,
    MODIFYVM_X86_IBPB_ON_VM_ENTRY,
    MODIFYVM_X86_IBPB_ON_VM_EXIT,
    MODIFYVM_X86_L1D_FLUSH_ON_SCHED,
    MODIFYVM_X86_L1D_FLUSH_ON_VM_ENTRY,
    MODIFYVM_X86_LARGEPAGES,
    MODIFYVM_X86_LONGMODE,
    MODIFYVM_X86_MDS_CLEAR_ON_SCHED,
    MODIFYVM_X86_MDS_CLEAR_ON_VM_ENTRY,
    MODIFYVM_X86_NESTED_HW_VIRT,
    MODIFYVM_X86_NESTEDPAGING,
    MODIFYVM_X86_PAE,
    MODIFYVM_X86_SETCPUID,
    MODIFYVM_X86_SPEC_CTRL,
    MODIFYVM_X86_TFRESET,
    MODIFYVM_X86_VIRT_VMSAVE_VMLOAD,
    MODIFYVM_X86_VTXUX,
    MODIFYVM_X86_VTXVPID,
    MODIFYVM_X86_X2APIC
};

static const RTGETOPTDEF g_aModifyVMOptions[] =
{
    OPT1("--name",                                                      MODIFYVM_NAME,                      RTGETOPT_REQ_STRING),
    OPT1("--groups",                                                    MODIFYVM_GROUPS,                    RTGETOPT_REQ_STRING),
    OPT1("--description",                                               MODIFYVM_DESCRIPTION,               RTGETOPT_REQ_STRING),
    OPT2("--os-type",                       "--ostype",                 MODIFYVM_OSTYPE,                    RTGETOPT_REQ_STRING),
    OPT2("--icon-file",                     "--iconfile",               MODIFYVM_ICONFILE,                  RTGETOPT_REQ_STRING),
    OPT1("--memory",                                                    MODIFYVM_MEMORY,                    RTGETOPT_REQ_UINT32),
    OPT2("--page-fusion",                   "--pagefusion",             MODIFYVM_PAGEFUSION,                RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--vram",                                                      MODIFYVM_VRAM,                      RTGETOPT_REQ_UINT32),
    OPT1("--firmware",                                                  MODIFYVM_FIRMWARE,                  RTGETOPT_REQ_STRING),
    OPT1("--acpi",                                                      MODIFYVM_ACPI,                      RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--ioapic",                                                    MODIFYVM_IOAPIC,                    RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--cpuid-portability-level",                                   MODIFYVM_CPUID_PORTABILITY,         RTGETOPT_REQ_UINT32),
    OPT2("--paravirt-provider",             "--paravirtprovider",       MODIFYVM_PARAVIRTPROVIDER,          RTGETOPT_REQ_STRING),
    OPT2("--paravirt-debug",                "--paravirtdebug",          MODIFYVM_PARAVIRTDEBUG,             RTGETOPT_REQ_STRING),
    OPT1("--cpus",                                                      MODIFYVM_CPUS,                      RTGETOPT_REQ_UINT32),
    OPT2("--cpu-hotplug",                   "--cpuhotplug",             MODIFYVM_CPUHOTPLUG,                RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--cpu-profile",                                               MODIFYVM_CPU_PROFILE,               RTGETOPT_REQ_STRING),
    OPT2("--plug-cpu",                      "--plugcpu",                MODIFYVM_PLUGCPU,                   RTGETOPT_REQ_UINT32),
    OPT2("--unplug-cpu",                    "--unplugcpu",              MODIFYVM_UNPLUGCPU,                 RTGETOPT_REQ_UINT32),
    OPT2("--cpu-execution-cap",             "--cpuexecutioncap",        MODIFYVM_CPU_EXECTUION_CAP,         RTGETOPT_REQ_UINT32),
    OPT2("--rtc-use-utc",                   "--rtcuseutc",              MODIFYVM_RTCUSEUTC,                 RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--graphicscontroller",            "--graphicscontroller",     MODIFYVM_GRAPHICSCONTROLLER,        RTGETOPT_REQ_STRING),
    OPT2("--monitor-count",                 "--monitorcount",           MODIFYVM_MONITORCOUNT,              RTGETOPT_REQ_UINT32),
    OPT2("--accelerate-3d",                 "--accelerate3d",           MODIFYVM_ACCELERATE3D,              RTGETOPT_REQ_BOOL_ONOFF),
#ifdef VBOX_WITH_VIDEOHWACCEL
    OPT2("--accelerate-2d-video",           "--accelerate2dvideo",      MODIFYVM_ACCELERATE2DVIDEO,         RTGETOPT_REQ_BOOL_ONOFF),
#endif
    OPT1("--firmware-logo-fade-in",                                     MODIFYVM_FWLOGOFADEIN,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--firmware-logo-fade-out",                                    MODIFYVM_FWLOGOFADEOUT,             RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--firmware-logo-image-path",                                  MODIFYVM_FWLOGOIMAGEPATH,           RTGETOPT_REQ_STRING),
    OPT1("--firmware-logo-display-time",                                MODIFYVM_FWLOGODISPLAYTIME,         RTGETOPT_REQ_UINT32),
    OPT1("--firmware-boot-menu",                                        MODIFYVM_FWBOOTMENU,                RTGETOPT_REQ_STRING),
    OPT1("--firmware-system-time-offset",                               MODIFYVM_FWSYSTEMTIMEOFFSET,        RTGETOPT_REQ_INT64),
    OPT1("--firmware-apic",                                             MODIFYVM_FWAPIC,                    RTGETOPT_REQ_STRING),
    OPT1("--firmware-pxe-debug",                                        MODIFYVM_FWPXEDEBUG,                RTGETOPT_REQ_BOOL_ONOFF),
    /* { Kept for backwards-compatibility */
    OPT2("--bios-logo-fade-in",             "--bioslogofadein",         MODIFYVM_FWLOGOFADEIN,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--firmware-fade-out",                                         MODIFYVM_FWLOGOFADEOUT,             RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--bios-logo-fade-out",            "--bioslogofadeout",        MODIFYVM_FWLOGOFADEOUT,             RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--bios-logo-display-time",        "--bioslogodisplaytime",    MODIFYVM_FWLOGODISPLAYTIME,         RTGETOPT_REQ_UINT32),
    OPT2("--bios-logo-image-path",          "--bioslogoimagepath",      MODIFYVM_FWLOGOIMAGEPATH,           RTGETOPT_REQ_STRING),
    OPT2("--bios-boot-menu",                "--biosbootmenu",           MODIFYVM_FWBOOTMENU,                RTGETOPT_REQ_STRING),
    OPT2("--bios-system-time-offset",       "--biossystemtimeoffset",   MODIFYVM_FWSYSTEMTIMEOFFSET,        RTGETOPT_REQ_INT64),
    OPT2("--bios-apic",                     "--biosapic",               MODIFYVM_FWAPIC,                    RTGETOPT_REQ_STRING),
    OPT2("--bios-pxe-debug",                "--biospxedebug",           MODIFYVM_FWPXEDEBUG,                RTGETOPT_REQ_BOOL_ONOFF),
    /* } */
    OPT2("--system-uuid-le",                "--system-uuid-le",         MODIFYVM_SYSTEMUUIDLE,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--bios-auto-serial-num-gen",      "--biosautoserialnumgen",   MODIFYVM_FWAUTOSERIALNUMGEN,        RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--boot",                                                      MODIFYVM_BOOT,                      RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT1("--hda",                                                       MODIFYVM_HDA,                       RTGETOPT_REQ_STRING), /* deprecated */
    OPT1("--hdb",                                                       MODIFYVM_HDB,                       RTGETOPT_REQ_STRING), /* deprecated */
    OPT1("--hdd",                                                       MODIFYVM_HDD,                       RTGETOPT_REQ_STRING), /* deprecated */
    OPT2("--idec-ontroller",                "--idecontroller",          MODIFYVM_IDECONTROLLER,             RTGETOPT_REQ_STRING), /* deprecated */
    OPT2("--sata-port-count",               "--sataportcount",          MODIFYVM_SATAPORTCOUNT,             RTGETOPT_REQ_UINT32), /* deprecated */
    OPT2("--sata-port",                     "--sataport",               MODIFYVM_SATAPORT,                  RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX), /* deprecated */
    OPT1("--sata",                                                      MODIFYVM_SATA,                      RTGETOPT_REQ_STRING), /* deprecated */
    OPT2("--scsi-port",                     "--scsiport",               MODIFYVM_SCSIPORT,                  RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX), /* deprecated */
    OPT2("--scsi-type",                     "--scsitype",               MODIFYVM_SCSITYPE,                  RTGETOPT_REQ_STRING), /* deprecated */
    OPT1("--scsi",                                                      MODIFYVM_SCSI,                      RTGETOPT_REQ_STRING), /* deprecated */
    OPT2("--dvd-pass-through",              "--dvdpassthrough",         MODIFYVM_DVDPASSTHROUGH,            RTGETOPT_REQ_STRING), /* deprecated */
    OPT1("--dvd",                                                       MODIFYVM_DVD,                       RTGETOPT_REQ_STRING), /* deprecated */
    OPT1("--floppy",                                                    MODIFYVM_FLOPPY,                    RTGETOPT_REQ_STRING), /* deprecated */
    OPT2("--nic-trace-file",                "--nictracefile",           MODIFYVM_NICTRACEFILE,              RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-trace",                     "--nictrace",               MODIFYVM_NICTRACE,                  RTGETOPT_REQ_BOOL_ONOFF | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-property",                  "--nicproperty",            MODIFYVM_NICPROPERTY,               RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-type",                      "--nictype",                MODIFYVM_NICTYPE,                   RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-speed",                     "--nicspeed",               MODIFYVM_NICSPEED,                  RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-boot-prio",                 "--nicbootprio",            MODIFYVM_NICBOOTPRIO,               RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-promisc",                   "--nicpromisc",             MODIFYVM_NICPROMISC,                RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-bandwidth-group",           "--nicbandwidthgroup",      MODIFYVM_NICBWGROUP,                RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT1("--nic",                                                       MODIFYVM_NIC,                       RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--cable-connected",               "--cableconnected",         MODIFYVM_CABLECONNECTED,            RTGETOPT_REQ_BOOL_ONOFF | RTGETOPT_FLAG_INDEX),
    OPT2("--bridge-adapter",                "--bridgeadapter",          MODIFYVM_BRIDGEADAPTER,             RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
#ifdef VBOX_WITH_CLOUD_NET
    OPT2("--cloud-network",                 "--cloudnetwork",           MODIFYVM_CLOUDNET,                  RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
#endif /* VBOX_WITH_CLOUD_NET */
    OPT2("--host-only-adapter",             "--hostonlyadapter",        MODIFYVM_HOSTONLYADAPTER,           RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
#ifdef VBOX_WITH_VMNET
    OPT2("--host-only-net",                 "--hostonlynet",            MODIFYVM_HOSTONLYNET,               RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
#endif
    OPT1("--intnet",                                                    MODIFYVM_INTNET,                    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nic-generic-drv",               "--nicgenericdrv",          MODIFYVM_GENERICDRV,                RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-network",                   "--natnetwork",             MODIFYVM_NATNETWORKNAME,            RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-net",                       "--natnet",                 MODIFYVM_NATNET,                    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-bind-ip",                   "--natbindip",              MODIFYVM_NATBINDIP,                 RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-settings",                  "--natsettings",            MODIFYVM_NATSETTINGS,               RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-pf",                        "--natpf",                  MODIFYVM_NATPF,                     RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-alias-mode",                "--nataliasmode",           MODIFYVM_NATALIASMODE,              RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-tftp-prefix",               "--nattftpprefix",          MODIFYVM_NATTFTPPREFIX,             RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-tftp-file",                 "--nattftpfile",            MODIFYVM_NATTFTPFILE,               RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-tftp-server",               "--nattftpserver",          MODIFYVM_NATTFTPSERVER,             RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-dns-pass-domain",           "--natdnspassdomain",       MODIFYVM_NATDNSPASSDOMAIN,          RTGETOPT_REQ_BOOL_ONOFF | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-dns-proxy",                 "--natdnsproxy",            MODIFYVM_NATDNSPROXY,               RTGETOPT_REQ_BOOL_ONOFF | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-dns-host-resolver",         "--natdnshostresolver",     MODIFYVM_NATDNSHOSTRESOLVER,        RTGETOPT_REQ_BOOL_ONOFF | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-localhostreachable",        "--natlocalhostreachable",  MODIFYVM_NATLOCALHOSTREACHABLE,     RTGETOPT_REQ_BOOL_ONOFF | RTGETOPT_FLAG_INDEX),
    OPT2("--nat-forward-broadcast",         "--natforwardbroadcast",    MODIFYVM_NATFORWARDBROADCAST,       RTGETOPT_REQ_BOOL_ONOFF | RTGETOPT_FLAG_INDEX),
    OPT2("--mac-address",                   "--macaddress",             MODIFYVM_MACADDRESS,                RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT1("--mouse",                                                     MODIFYVM_HIDPTR,                    RTGETOPT_REQ_STRING),
    OPT1("--keyboard",                                                  MODIFYVM_HIDKBD,                    RTGETOPT_REQ_STRING),
    OPT2("--uart-mode",                     "--uartmode",               MODIFYVM_UARTMODE,                  RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT2("--uart-type",                     "--uarttype",               MODIFYVM_UARTTYPE,                  RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT1("--uart",                                                      MODIFYVM_UART,                      RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
#if defined(RT_OS_LINUX) || defined(RT_OS_WINDOWS)
    OPT2("--lpt-mode",                      "--lptmode",                MODIFYVM_LPTMODE,                   RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
    OPT1("--lpt",                                                       MODIFYVM_LPT,                       RTGETOPT_REQ_STRING | RTGETOPT_FLAG_INDEX),
#endif
    OPT2("--guest-memory-balloon",          "--guestmemoryballoon",     MODIFYVM_GUESTMEMORYBALLOON,        RTGETOPT_REQ_UINT32),
    OPT2("--audio-controller",              "--audiocontroller",        MODIFYVM_AUDIOCONTROLLER,           RTGETOPT_REQ_STRING),
    OPT2("--audio-codec",                   "--audiocodec",             MODIFYVM_AUDIOCODEC,                RTGETOPT_REQ_STRING),
    OPT1("--audio",                                                     MODIFYVM_AUDIO,                     RTGETOPT_REQ_STRING),
    OPT2("--audio-driver",                  "--audiodriver",            MODIFYVM_AUDIODRIVER,               RTGETOPT_REQ_STRING),
    OPT2("--audio-enabled",                 "--audioenabled",           MODIFYVM_AUDIOENABLED,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--audio-in",                      "--audioin",                MODIFYVM_AUDIOIN,                   RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--audio-out",                     "--audioout",               MODIFYVM_AUDIOOUT,                  RTGETOPT_REQ_BOOL_ONOFF),
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    OPT1("--clipboard-mode",                                            MODIFYVM_CLIPBOARD_MODE,            RTGETOPT_REQ_STRING),
    OPT1("--clipboard",                                                 MODIFYVM_CLIPBOARD_MODE,            RTGETOPT_REQ_STRING),     /* deprecated */
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    OPT1("--clipboard-file-transfers",                                  MODIFYVM_CLIPBOARD_FILE_TRANSFERS,  RTGETOPT_REQ_STRING),
# endif
#endif
    OPT2("--drag-and-drop",                 "--draganddrop",            MODIFYVM_DRAGANDDROP,               RTGETOPT_REQ_STRING),
    OPT2("--vrdp-port",                     "--vrdpport",               MODIFYVM_VRDPPORT,                  RTGETOPT_REQ_STRING),     /* deprecated */
    OPT2("--vrdp-address",                  "--vrdpaddress",            MODIFYVM_VRDPADDRESS,               RTGETOPT_REQ_STRING),     /* deprecated */
    OPT2("--vrdp-auth-type",                "--vrdpauthtype",           MODIFYVM_VRDPAUTHTYPE,              RTGETOPT_REQ_STRING),     /* deprecated */
    OPT2("--vrdp-multi-con",                "--vrdpmulticon",           MODIFYVM_VRDPMULTICON,              RTGETOPT_REQ_BOOL_ONOFF), /* deprecated */
    OPT2("--vrdp-reuse-con",                "--vrdpreusecon",           MODIFYVM_VRDPREUSECON,              RTGETOPT_REQ_BOOL_ONOFF), /* deprecated */
    OPT2("--vrdp-video-channel",            "--vrdpvideochannel",       MODIFYVM_VRDPVIDEOCHANNEL,          RTGETOPT_REQ_BOOL_ONOFF), /* deprecated */
    OPT2("--vrdp-video-channel-quality",    "--vrdpvideochannelquality",MODIFYVM_VRDPVIDEOCHANNELQUALITY,   RTGETOPT_REQ_STRING),     /* deprecated */
    OPT1("--vrdp",                                                      MODIFYVM_VRDP,                      RTGETOPT_REQ_BOOL_ONOFF), /* deprecated */
    OPT2("--vrde-property",                 "--vrdeproperty",           MODIFYVM_VRDEPROPERTY,              RTGETOPT_REQ_STRING),
    OPT2("--vrde-port",                     "--vrdeport",               MODIFYVM_VRDEPORT,                  RTGETOPT_REQ_STRING),
    OPT2("--vrde-address",                  "--vrdeaddress",            MODIFYVM_VRDEADDRESS,               RTGETOPT_REQ_STRING),
    OPT2("--vrde-auth-type",                "--vrdeauthtype",           MODIFYVM_VRDEAUTHTYPE,              RTGETOPT_REQ_STRING),
    OPT2("--vrde-auth-library",             "--vrdeauthlibrary",        MODIFYVM_VRDEAUTHLIBRARY,           RTGETOPT_REQ_STRING),
    OPT2("--vrde-multi-con",                "--vrdemulticon",           MODIFYVM_VRDEMULTICON,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--vrde-reuse-con",                "--vrdereusecon",           MODIFYVM_VRDEREUSECON,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--vrde-video-channel",            "--vrdevideochannel",       MODIFYVM_VRDEVIDEOCHANNEL,          RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--vrde-video-channel-quality",    "--vrdevideochannelquality",MODIFYVM_VRDEVIDEOCHANNELQUALITY,   RTGETOPT_REQ_STRING),
    OPT2("--vrde-extpack",                  "--vrdeextpack",            MODIFYVM_VRDE_EXTPACK,              RTGETOPT_REQ_STRING),
    OPT1("--vrde",                                                      MODIFYVM_VRDE,                      RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--usb-rename",                    "--usbrename",              MODIFYVM_USBRENAME,                 RTGETOPT_REQ_STRING),
    OPT2("--usb-xhci",                      "--usbxhci",                MODIFYVM_USBXHCI,                   RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--usb-ehci",                      "--usbehci",                MODIFYVM_USBEHCI,                   RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--usb-ohci",                      "--usbohci",                MODIFYVM_USBOHCI,                   RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--usb",                                                       MODIFYVM_USBOHCI,                   RTGETOPT_REQ_BOOL_ONOFF), /* deprecated */
    OPT2("--snapshot-folder",               "--snapshotfolder",         MODIFYVM_SNAPSHOTFOLDER,            RTGETOPT_REQ_STRING),
    OPT1("--teleporter",                                                MODIFYVM_TELEPORTER_ENABLED,        RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--teleporter-enabled",            "--teleporterenabled",      MODIFYVM_TELEPORTER_ENABLED,        RTGETOPT_REQ_BOOL_ONOFF), /* deprecated */
    OPT2("--teleporter-port",               "--teleporterport",         MODIFYVM_TELEPORTER_PORT,           RTGETOPT_REQ_UINT32),
    OPT2("--teleporter-address",            "--teleporteraddress",      MODIFYVM_TELEPORTER_ADDRESS,        RTGETOPT_REQ_STRING),
    OPT2("--teleporter-password",           "--teleporterpassword",     MODIFYVM_TELEPORTER_PASSWORD,       RTGETOPT_REQ_STRING),
    OPT2("--teleporter-password-file",      "--teleporterpasswordfile", MODIFYVM_TELEPORTER_PASSWORD_FILE,  RTGETOPT_REQ_STRING),
    OPT1("--tracing-enabled",                                           MODIFYVM_TRACING_ENABLED,           RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--tracing-config",                                            MODIFYVM_TRACING_CONFIG,            RTGETOPT_REQ_STRING),
    OPT1("--tracing-allow-vm-access",                                   MODIFYVM_TRACING_ALLOW_VM_ACCESS,   RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--hardware-uuid",                 "--hardwareuuid",                            MODIFYVM_HARDWARE_UUID,             RTGETOPT_REQ_STRING),
    OPT1("--iocache",                                                   MODIFYVM_IOCACHE,                   RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--iocache-size",                  "--iocachesize",            MODIFYVM_IOCACHESIZE,               RTGETOPT_REQ_UINT32),
    OPT1("--chipset",                                                   MODIFYVM_CHIPSET,                   RTGETOPT_REQ_STRING),
#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    OPT1("--iommu",                                                     MODIFYVM_IOMMU,                     RTGETOPT_REQ_STRING),
#endif
#if defined(VBOX_WITH_TPM)
    OPT1("--tpm-type",                                                  MODIFYVM_TPM_TYPE,                  RTGETOPT_REQ_STRING),
    OPT1("--tpm-location",                                              MODIFYVM_TPM_LOCATION,              RTGETOPT_REQ_STRING),
#endif
#ifdef VBOX_WITH_RECORDING
    OPT1("--recording",                                                 MODIFYVM_RECORDING,                 RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--recording-screens",             "--recordingscreens",       MODIFYVM_RECORDING_SCREENS,         RTGETOPT_REQ_STRING),
    OPT2("--recording-file",                "--recordingfile",          MODIFYVM_RECORDING_FILENAME,        RTGETOPT_REQ_STRING),
    OPT2("--recording-max-time",            "--recordingmaxtime",       MODIFYVM_RECORDING_MAXTIME,         RTGETOPT_REQ_INT32),
    OPT2("--recording-max-size",            "--recordingmaxsize",       MODIFYVM_RECORDING_MAXSIZE,         RTGETOPT_REQ_INT32),
    OPT2("--recording-opts",                "--recordingopts",          MODIFYVM_RECORDING_OPTIONS,         RTGETOPT_REQ_STRING),
    OPT2("--recording-options",             "--recordingoptions",       MODIFYVM_RECORDING_OPTIONS,         RTGETOPT_REQ_STRING),
    OPT2("--recording-video-res",           "--recordingvideores",      MODIFYVM_RECORDING_VIDEO_RES,       RTGETOPT_REQ_STRING),
    OPT2("--recording-video-resolution",    "--recordingvideoresolution",MODIFYVM_RECORDING_VIDEO_RES,      RTGETOPT_REQ_STRING),
    OPT2("--recording-video-rate",          "--recordingvideorate",     MODIFYVM_RECORDING_VIDEO_RATE,      RTGETOPT_REQ_UINT32),
    OPT2("--recording-video-fps",           "--recordingvideofps",      MODIFYVM_RECORDING_VIDEO_FPS,       RTGETOPT_REQ_UINT32),
#endif
    OPT1("--autostart-enabled",                                         MODIFYVM_AUTOSTART_ENABLED,         RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--autostart-delay",                                           MODIFYVM_AUTOSTART_DELAY,           RTGETOPT_REQ_UINT32),
    OPT1("--autostop-type",                                             MODIFYVM_AUTOSTOP_TYPE,             RTGETOPT_REQ_STRING),
#ifdef VBOX_WITH_PCI_PASSTHROUGH
    OPT2("--pci-attach",                    "--pciattach",              MODIFYVM_ATTACH_PCI,                RTGETOPT_REQ_STRING),
    OPT2("--pci-detach",                    "--pcidetach",              MODIFYVM_DETACH_PCI,                RTGETOPT_REQ_STRING),
#endif
#ifdef VBOX_WITH_USB_CARDREADER
    OPT2("--usb-card-reader",               "--usbcardreader",          MODIFYVM_USBCARDREADER,             RTGETOPT_REQ_BOOL_ONOFF),
#endif
    OPT2("--default-frontend",              "--defaultfrontend",        MODIFYVM_DEFAULTFRONTEND,           RTGETOPT_REQ_STRING),
    OPT1("--vm-process-priority",                                       MODIFYVM_VMPROC_PRIORITY,           RTGETOPT_REQ_STRING),
    OPT1("--vm-execution-engine",                                       MODIFYVM_VMEXEC_ENGINE,             RTGETOPT_REQ_STRING),
    OPT1("--testing-enabled",                                           MODIFYVM_TESTING_ENABLED,           RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--testing-mmio",                                              MODIFYVM_TESTING_MMIO,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--testing-cfg-dword",                                         MODIFYVM_TESTING_CFG_DWORD,         RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_INDEX),
    OPT1("--guest-debug-provider",                                      MODIFYVM_GUEST_DEBUG_PROVIDER,      RTGETOPT_REQ_STRING),
    OPT1("--guest-debug-io-provider",                                   MODIFYVM_GUEST_DEBUG_IO_PROVIDER,   RTGETOPT_REQ_STRING),
    OPT1("--guest-debug-address",                                       MODIFYVM_GUEST_DEBUG_ADDRESS,       RTGETOPT_REQ_STRING),
    OPT1("--guest-debug-port",                                          MODIFYVM_GUEST_DEBUG_PORT,          RTGETOPT_REQ_UINT32),

    /*
     * x86-only stuff.
     *
     * Note: The non-prefixed options (e.g. "hpet" vs. "x86-hpet") are being kept
     *       to maintain backwards compatibility, at least for a while. Remove them before going on pension.
     *
     * Sorted after their MODIFYVM_X86_XXX values.
     */
    OPT1("--x86-apic",                                                  MODIFYVM_X86_APIC,                  RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--apic",                                                      MODIFYVM_X86_APIC,                  RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-cpuid-remove",                                          MODIFYVM_X86_DELCPUID,              RTGETOPT_REQ_UINT32_OPTIONAL_PAIR | RTGETOPT_FLAG_HEX),
    OPT1("--cpuid-remove",                                              MODIFYVM_X86_DELCPUID,              RTGETOPT_REQ_UINT32_OPTIONAL_PAIR | RTGETOPT_FLAG_HEX),
    OPT1("--cpuidremove",                                               MODIFYVM_X86_DELCPUID_OLD,          RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_HEX), /* legacy - syntax differs */
    OPT1("--x86-cpuid-remove-all",                                      MODIFYVM_X86_DELALLCPUID,           RTGETOPT_REQ_NOTHING),
    OPT2("--cpuid-remove-all",              "--cpuidremoveall",         MODIFYVM_X86_DELALLCPUID,           RTGETOPT_REQ_NOTHING),
    OPT1("--x86-hpet",                                                  MODIFYVM_X86_HPET,                  RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--hpet",                                                      MODIFYVM_X86_HPET,                  RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-hwvirtex",                                              MODIFYVM_X86_HWVIRTEX,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--hwvirtex",                                                  MODIFYVM_X86_HWVIRTEX,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-ibpb-on-vm-entry",                                      MODIFYVM_X86_IBPB_ON_VM_ENTRY,      RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--ibpb-on-vm-entry",                                          MODIFYVM_X86_IBPB_ON_VM_ENTRY,      RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-ibpb-on-vm-exit",                                       MODIFYVM_X86_IBPB_ON_VM_EXIT,       RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--ibpb-on-vm-exit",                                           MODIFYVM_X86_IBPB_ON_VM_EXIT,       RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-l1d-flush-on-sched",                                    MODIFYVM_X86_L1D_FLUSH_ON_SCHED,    RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--l1d-flush-on-sched",                                        MODIFYVM_X86_L1D_FLUSH_ON_SCHED,    RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-l1d-flush-on-vm-entry",                                 MODIFYVM_X86_L1D_FLUSH_ON_VM_ENTRY, RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--l1d-flush-on-vm-entry",                                     MODIFYVM_X86_L1D_FLUSH_ON_VM_ENTRY, RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-large-pages",                                           MODIFYVM_X86_LARGEPAGES,            RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--large-pages",                   "--largepages",             MODIFYVM_X86_LARGEPAGES,            RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-long-mode",                                             MODIFYVM_X86_LONGMODE,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--long-mode",                     "--longmode",               MODIFYVM_X86_LONGMODE,              RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-mds-clear-on-sched",                                    MODIFYVM_X86_MDS_CLEAR_ON_SCHED,    RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--mds-clear-on-sched",                                        MODIFYVM_X86_MDS_CLEAR_ON_SCHED,    RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-mds-clear-on-vm-entry",                                 MODIFYVM_X86_MDS_CLEAR_ON_VM_ENTRY, RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--mds-clear-on-vm-entry",                                     MODIFYVM_X86_MDS_CLEAR_ON_VM_ENTRY, RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-nested-hw-virt",                                        MODIFYVM_X86_NESTED_HW_VIRT,        RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--nested-hw-virt",                                            MODIFYVM_X86_NESTED_HW_VIRT,        RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-nested-paging",                                         MODIFYVM_X86_NESTEDPAGING,          RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--nested-paging",                 "--nestedpaging",           MODIFYVM_X86_NESTEDPAGING,          RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-pae",                                                   MODIFYVM_X86_PAE,                   RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--pae",                                                       MODIFYVM_X86_PAE,                   RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-cpuid-set",                                             MODIFYVM_X86_SETCPUID,              RTGETOPT_REQ_UINT32_OPTIONAL_PAIR | RTGETOPT_FLAG_HEX),
    OPT2("--cpuid-set",                     "--cpuidset",               MODIFYVM_X86_SETCPUID,              RTGETOPT_REQ_UINT32_OPTIONAL_PAIR | RTGETOPT_FLAG_HEX),
    OPT1("--x86-spec-ctrl",                                             MODIFYVM_X86_SPEC_CTRL,             RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--spec-ctrl",                                                 MODIFYVM_X86_SPEC_CTRL,             RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-triple-fault-reset",                                    MODIFYVM_X86_TFRESET,               RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--triple-fault-reset",            "--triplefaultreset",       MODIFYVM_X86_TFRESET,               RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--virt-vmsave-vmload",                                        MODIFYVM_X86_VIRT_VMSAVE_VMLOAD,    RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-virt-vmsave-vmload",                                    MODIFYVM_X86_VIRT_VMSAVE_VMLOAD,    RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-vtx-ux",                                                MODIFYVM_X86_VTXUX,                 RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--vtx-ux",                        "--vtxux",                  MODIFYVM_X86_VTXUX,                 RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-vtx-vpid",                                              MODIFYVM_X86_VTXVPID,               RTGETOPT_REQ_BOOL_ONOFF),
    OPT2("--vtx-vpid",                      "--vtxvpid",                MODIFYVM_X86_VTXVPID,               RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x86-x2apic",                                                MODIFYVM_X86_X2APIC,                RTGETOPT_REQ_BOOL_ONOFF),
    OPT1("--x2apic",                                                    MODIFYVM_X86_X2APIC,                RTGETOPT_REQ_BOOL_ONOFF),
};

static void vrdeWarningDeprecatedOption(const char *pszOption)
{
    RTStrmPrintf(g_pStdErr, ModifyVM::tr("Warning: '--vrdp%s' is deprecated. Use '--vrde%s'.\n"), pszOption, pszOption);
}


/**
 * Wrapper around IMachine::SetExtraData that does the error reporting.
 *
 * @returns COM result code.
 * @param   rSessionMachine The IMachine.
 * @param   pszVariable     The variable to set.
 * @param   pszValue        The value to set.  To delete pass empty string, not
 *                          NULL.
 */
static HRESULT setExtraData(ComPtr<IMachine> &rSessionMachine, const char *pszVariable, const char *pszValue)
{
    HRESULT hrc = rSessionMachine->SetExtraData(Bstr(pszVariable).raw(), Bstr(pszValue).raw());
    if (FAILED(hrc))
    {
        char *pszContext = RTStrAPrintf2("IMachine::SetExtraData('%s', '%s')", pszVariable, pszValue);
        com::GlueHandleComError(rSessionMachine, pszContext, hrc, __FILE__, __LINE__);
        RTStrFree(pszContext);
    }
    return hrc;
}


#ifdef VBOX_WITH_PCI_PASSTHROUGH
/** Parse PCI address in format 01:02.03 and convert it to the numeric representation. */
static int32_t parsePci(const char* szPciAddr)
{
    uint8_t aVals[3] = {0, 0, 0};

    char *pszNext;
    int vrc = RTStrToUInt8Ex(pszNext, &pszNext, 16, &aVals[0]);
    if (RT_FAILURE(vrc) || pszNext == NULL || *pszNext != ':')
        return -1;

    vrc = RTStrToUInt8Ex(pszNext+1, &pszNext, 16, &aVals[1]);
    if (RT_FAILURE(vrc) || pszNext == NULL || *pszNext != '.')
        return -1;

    vrc = RTStrToUInt8Ex(pszNext+1, &pszNext, 16, &aVals[2]);
    if (RT_FAILURE(vrc) || pszNext == NULL)
        return -1;

    return (aVals[0] << 8) | (aVals[1] << 3) | (aVals[2] << 0);
}
#endif

void parseGroups(const char *pcszGroups, com::SafeArray<BSTR> *pGroups)
{
    while (pcszGroups)
    {
        char *pComma = RTStrStr(pcszGroups, ",");
        if (pComma)
        {
            Bstr(pcszGroups, pComma - pcszGroups).detachTo(pGroups->appendedRaw());
            pcszGroups = pComma + 1;
        }
        else
        {
            Bstr(pcszGroups).detachTo(pGroups->appendedRaw());
            pcszGroups = NULL;
        }
    }
}

#ifdef VBOX_WITH_RECORDING
int parseScreens(const char *pcszScreens, com::SafeArray<BOOL> *pScreens)
{
    if (!RTStrICmp(pcszScreens, "all"))
    {
        for (uint32_t i = 0; i < pScreens->size(); i++)
            (*pScreens)[i] = TRUE;
        return VINF_SUCCESS;
    }
    if (!RTStrICmp(pcszScreens, "none"))
    {
        for (uint32_t i = 0; i < pScreens->size(); i++)
            (*pScreens)[i] = FALSE;
        return VINF_SUCCESS;
    }
    while (pcszScreens && *pcszScreens)
    {
        char *pszNext;
        uint32_t iScreen;
        int vrc = RTStrToUInt32Ex(pcszScreens, &pszNext, 0, &iScreen);
        if (RT_FAILURE(vrc))
            return VERR_PARSE_ERROR;
        if (iScreen >= pScreens->size())
            return VERR_PARSE_ERROR;
        if (pszNext && *pszNext)
        {
            pszNext = RTStrStripL(pszNext);
            if (*pszNext != ',')
                return VERR_PARSE_ERROR;
            pszNext++;
        }
        (*pScreens)[iScreen] = true;
        pcszScreens = pszNext;
    }
    return VINF_SUCCESS;
}
#endif

static int parseNum(uint32_t uIndex, unsigned cMaxIndex, const char *pszName)
{
    if (   uIndex >= 1
        && uIndex <= cMaxIndex)
        return uIndex;
    errorArgument(ModifyVM::tr("Invalid %s number %u"), pszName, uIndex);
    return 0;
}

VMProcPriority_T nameToVMProcPriority(const char *pszName)
{
    if (!RTStrICmp(pszName, "default"))
        return VMProcPriority_Default;
    if (!RTStrICmp(pszName, "flat"))
        return VMProcPriority_Flat;
    if (!RTStrICmp(pszName, "low"))
        return VMProcPriority_Low;
    if (!RTStrICmp(pszName, "normal"))
        return VMProcPriority_Normal;
    if (!RTStrICmp(pszName, "high"))
        return VMProcPriority_High;

    return VMProcPriority_Invalid;
}

static VMExecutionEngine_T nameToVMExecEngine(const char *pszName)
{
    if (!RTStrICmp(pszName, "default"))
        return VMExecutionEngine_Default;
    if (   !RTStrICmp(pszName, "hwvirt")
        || !RTStrICmp(pszName, "hm"))
        return VMExecutionEngine_HwVirt;
    if (   !RTStrICmp(pszName, "nem")
        || !RTStrICmp(pszName, "native-api"))
        return VMExecutionEngine_NativeApi;
    if (!RTStrICmp(pszName, "interpreter"))
        return VMExecutionEngine_Interpreter;
    if (   !RTStrICmp(pszName, "recompiler"))
        return VMExecutionEngine_Recompiler;

    return VMExecutionEngine_NotSet;
}

/**
 * Handles the x86-specific modifyvm options.
 *
 * @returns HRESULT
 * @retval  E_INVALIDARG if handed-in option was not being handled.
 * @param   pGetOptState        Pointer to GetOpt state to use.
 * @param   c                   Current GetOpt value (short form).
 * @param   pValueUnion         Pointer to current value union.
 * @param   sessionMachine      Session machine to use.
 * @param   platformX86         x86-specific platform object to use.
 */
static HRESULT handleModifyVM_x86(PRTGETOPTSTATE pGetOptState, int c, PRTGETOPTUNION pValueUnion,
                                  ComPtr<IMachine> &sessionMachine, ComPtr<IPlatformX86> &platformX86)
{
    RT_NOREF(sessionMachine);

    HRESULT hrc = S_OK;

    switch (c)
    {
        case MODIFYVM_X86_APIC:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_APIC, pValueUnion->f));
            break;

        case MODIFYVM_X86_DELCPUID:
            CHECK_ERROR(platformX86, RemoveCPUIDLeaf(pValueUnion->PairU32.uFirst, pValueUnion->PairU32.uSecond));
            break;

        case MODIFYVM_X86_DELCPUID_OLD:
            CHECK_ERROR(platformX86, RemoveCPUIDLeaf(pValueUnion->u32, UINT32_MAX));
            break;

        case MODIFYVM_X86_DELALLCPUID:
            CHECK_ERROR(platformX86, RemoveAllCPUIDLeaves());
            break;

        case MODIFYVM_X86_HPET:
            CHECK_ERROR(platformX86, COMSETTER(HPETEnabled)(pValueUnion->f));
            break;

        case MODIFYVM_X86_TFRESET:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_TripleFaultReset, pValueUnion->f));
            break;

        case MODIFYVM_X86_PAE:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_PAE, pValueUnion->f));
            break;

        case MODIFYVM_X86_LONGMODE:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_LongMode, pValueUnion->f));
            break;

        case MODIFYVM_X86_X2APIC:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_X2APIC, pValueUnion->f));
            break;

        case MODIFYVM_X86_NESTEDPAGING:
            CHECK_ERROR(platformX86, SetHWVirtExProperty(HWVirtExPropertyType_NestedPaging, pValueUnion->f));
            break;

        case MODIFYVM_X86_LARGEPAGES:
            CHECK_ERROR(platformX86, SetHWVirtExProperty(HWVirtExPropertyType_LargePages, pValueUnion->f));
            break;

        case MODIFYVM_X86_VTXVPID:
            CHECK_ERROR(platformX86, SetHWVirtExProperty(HWVirtExPropertyType_VPID, pValueUnion->f));
            break;

        case MODIFYVM_X86_VTXUX:
            CHECK_ERROR(platformX86, SetHWVirtExProperty(HWVirtExPropertyType_UnrestrictedExecution, pValueUnion->f));
            break;

        case MODIFYVM_X86_VIRT_VMSAVE_VMLOAD:
            CHECK_ERROR(platformX86, SetHWVirtExProperty(HWVirtExPropertyType_VirtVmsaveVmload, pValueUnion->f));
            break;

        case MODIFYVM_X86_IBPB_ON_VM_EXIT:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_IBPBOnVMExit, pValueUnion->f));
            break;

        case MODIFYVM_X86_IBPB_ON_VM_ENTRY:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_IBPBOnVMEntry, pValueUnion->f));
            break;

        case MODIFYVM_X86_SPEC_CTRL:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_SpecCtrl, pValueUnion->f));
            break;

        case MODIFYVM_X86_L1D_FLUSH_ON_SCHED:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_L1DFlushOnEMTScheduling, pValueUnion->f));
            break;

        case MODIFYVM_X86_L1D_FLUSH_ON_VM_ENTRY:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_L1DFlushOnVMEntry, pValueUnion->f));
            break;

        case MODIFYVM_X86_MDS_CLEAR_ON_SCHED:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_MDSClearOnEMTScheduling, pValueUnion->f));
            break;

        case MODIFYVM_X86_MDS_CLEAR_ON_VM_ENTRY:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_MDSClearOnVMEntry, pValueUnion->f));
            break;

        case MODIFYVM_X86_NESTED_HW_VIRT:
            CHECK_ERROR(platformX86, SetCPUProperty(CPUPropertyTypeX86_HWVirt, pValueUnion->f));
            break;

        case MODIFYVM_X86_HWVIRTEX:
            CHECK_ERROR(platformX86, SetHWVirtExProperty(HWVirtExPropertyType_Enabled, pValueUnion->f));
            break;

        case MODIFYVM_X86_SETCPUID:
        {
            uint32_t const idx    = c == MODIFYVM_X86_SETCPUID ?  pValueUnion->PairU32.uFirst  : pValueUnion->u32;
            uint32_t const idxSub = c == MODIFYVM_X86_SETCPUID ?  pValueUnion->PairU32.uSecond : UINT32_MAX;
            uint32_t aValue[4];
            for (unsigned i = 0; i < 4; i++)
            {
                int vrc = RTGetOptFetchValue(pGetOptState, pValueUnion, RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_HEX);
                if (RT_FAILURE(vrc))
                    return errorSyntax(ModifyVM::tr("Missing or invalid argument to '%s'"),
                                       pGetOptState->pDef->pszLong);
                aValue[i] = pValueUnion->u32;
            }
            CHECK_ERROR(platformX86, SetCPUIDLeaf(idx, idxSub, aValue[0], aValue[1], aValue[2], aValue[3]));
            break;
        }

        default:
            hrc = E_INVALIDARG;
            break;
    }

    return hrc;
}

RTEXITCODE handleModifyVM(HandlerArg *a)
{
    int c;
    HRESULT hrc;
    Bstr name;

    /* VM ID + at least one parameter. Parameter arguments are checked
     * individually. */
    if (a->argc < 2)
        return errorSyntax(ModifyVM::tr("Not enough parameters"));

    /* try to find the given sessionMachine */
    ComPtr<IMachine> machine;
    CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                               machine.asOutParam()), RTEXITCODE_FAILURE);


    /* Get the number of network adapters */
    ULONG NetworkAdapterCount = getMaxNics(machine);

    /* open a session for the VM */
    CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Write), RTEXITCODE_FAILURE);

    /* get the mutable session sessionMachine */
    ComPtr<IMachine> sessionMachine;
    CHECK_ERROR_RET(a->session, COMGETTER(Machine)(sessionMachine.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IFirmwareSettings> firmwareSettings;
    CHECK_ERROR_RET(sessionMachine, COMGETTER(FirmwareSettings)(firmwareSettings.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IPlatform> platform;
    CHECK_ERROR_RET(sessionMachine, COMGETTER(Platform)(platform.asOutParam()), RTEXITCODE_FAILURE);

    /* For the x86-based options we need the x86-specific platform object. */
    ComPtr<IPlatformX86> platformX86;
    platform->COMGETTER(X86)(platformX86.asOutParam());

    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    sessionMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());

    RTGETOPTSTATE GetOptState;
    RTGetOptInit(&GetOptState, a->argc, a->argv, g_aModifyVMOptions,
                 RT_ELEMENTS(g_aModifyVMOptions), 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);

    RTGETOPTUNION ValueUnion;
    while (   SUCCEEDED (hrc)
           && (c = RTGetOpt(&GetOptState, &ValueUnion)))
    {
        switch (c)
        {
            case MODIFYVM_NAME:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(Name)(Bstr(ValueUnion.psz).raw()));
                break;
            }
            case MODIFYVM_GROUPS:
            {
                com::SafeArray<BSTR> groups;
                parseGroups(ValueUnion.psz, &groups);
                CHECK_ERROR(sessionMachine, COMSETTER(Groups)(ComSafeArrayAsInParam(groups)));
                break;
            }
            case MODIFYVM_DESCRIPTION:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(Description)(Bstr(ValueUnion.psz).raw()));
                break;
            }
            case MODIFYVM_OSTYPE:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(OSTypeId)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_ICONFILE:
            {
                RTFILE iconFile;
                int vrc = RTFileOpen(&iconFile, ValueUnion.psz, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                if (RT_FAILURE(vrc))
                {
                    RTMsgError(ModifyVM::tr("Cannot open file \"%s\": %Rrc"), ValueUnion.psz, vrc);
                    hrc = E_FAIL;
                    break;
                }
                uint64_t cbSize;
                vrc = RTFileQuerySize(iconFile, &cbSize);
                if (RT_FAILURE(vrc))
                {
                    RTMsgError(ModifyVM::tr("Cannot get size of file \"%s\": %Rrc"), ValueUnion.psz, vrc);
                    hrc = E_FAIL;
                    break;
                }
                if (cbSize > _256K)
                {
                    RTMsgError(ModifyVM::tr("File \"%s\" is bigger than 256KByte"), ValueUnion.psz);
                    hrc = E_FAIL;
                    break;
                }
                SafeArray<BYTE> icon((size_t)cbSize);
                hrc = RTFileRead(iconFile, icon.raw(), (size_t)cbSize, NULL);
                if (RT_FAILURE(vrc))
                {
                    RTMsgError(ModifyVM::tr("Cannot read contents of file \"%s\": %Rrc"), ValueUnion.psz, vrc);
                    hrc = E_FAIL;
                    break;
                }
                RTFileClose(iconFile);
                CHECK_ERROR(sessionMachine, COMSETTER(Icon)(ComSafeArrayAsInParam(icon)));
                break;
            }

            case MODIFYVM_MEMORY:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(MemorySize)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_PAGEFUSION:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(PageFusionEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_VRAM:
            {
                CHECK_ERROR(pGraphicsAdapter, COMSETTER(VRAMSize)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_FIRMWARE:
            {
                if (!RTStrICmp(ValueUnion.psz, "efi"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(FirmwareType)(FirmwareType_EFI));
                }
                else if (!RTStrICmp(ValueUnion.psz, "efi32"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(FirmwareType)(FirmwareType_EFI32));
                }
                else if (!RTStrICmp(ValueUnion.psz, "efi64"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(FirmwareType)(FirmwareType_EFI64));
                }
                else if (!RTStrICmp(ValueUnion.psz, "efidual"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(FirmwareType)(FirmwareType_EFIDUAL));
                }
                else if (!RTStrICmp(ValueUnion.psz, "bios"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(FirmwareType)(FirmwareType_BIOS));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --firmware argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_ACPI:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(ACPIEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_IOAPIC:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(IOAPICEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_CPUID_PORTABILITY:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(CPUIDPortabilityLevel)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_PARAVIRTPROVIDER:
            {
                if (   !RTStrICmp(ValueUnion.psz, "none")
                    || !RTStrICmp(ValueUnion.psz, "disabled"))
                    CHECK_ERROR(sessionMachine, COMSETTER(ParavirtProvider)(ParavirtProvider_None));
                else if (!RTStrICmp(ValueUnion.psz, "default"))
                    CHECK_ERROR(sessionMachine, COMSETTER(ParavirtProvider)(ParavirtProvider_Default));
                else if (!RTStrICmp(ValueUnion.psz, "legacy"))
                    CHECK_ERROR(sessionMachine, COMSETTER(ParavirtProvider)(ParavirtProvider_Legacy));
                else if (!RTStrICmp(ValueUnion.psz, "minimal"))
                    CHECK_ERROR(sessionMachine, COMSETTER(ParavirtProvider)(ParavirtProvider_Minimal));
                else if (!RTStrICmp(ValueUnion.psz, "hyperv"))
                    CHECK_ERROR(sessionMachine, COMSETTER(ParavirtProvider)(ParavirtProvider_HyperV));
                else if (!RTStrICmp(ValueUnion.psz, "kvm"))
                    CHECK_ERROR(sessionMachine, COMSETTER(ParavirtProvider)(ParavirtProvider_KVM));
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --paravirtprovider argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_PARAVIRTDEBUG:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(ParavirtDebug)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_CPUS:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(CPUCount)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_RTCUSEUTC:
            {
                CHECK_ERROR(platform, COMSETTER(RTCUseUTC)(ValueUnion.f));
                break;
            }

            case MODIFYVM_CPUHOTPLUG:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(CPUHotPlugEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_CPU_PROFILE:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(CPUProfile)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_PLUGCPU:
            {
                CHECK_ERROR(sessionMachine, HotPlugCPU(ValueUnion.u32));
                break;
            }

            case MODIFYVM_UNPLUGCPU:
            {
                CHECK_ERROR(sessionMachine, HotUnplugCPU(ValueUnion.u32));
                break;
            }

            case MODIFYVM_CPU_EXECTUION_CAP:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(CPUExecutionCap)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_GRAPHICSCONTROLLER:
            {
                if (   !RTStrICmp(ValueUnion.psz, "none")
                    || !RTStrICmp(ValueUnion.psz, "disabled"))
                    CHECK_ERROR(pGraphicsAdapter, COMSETTER(GraphicsControllerType)(GraphicsControllerType_Null));
                else if (   !RTStrICmp(ValueUnion.psz, "vboxvga")
                         || !RTStrICmp(ValueUnion.psz, "vbox")
                         || !RTStrICmp(ValueUnion.psz, "vga")
                         || !RTStrICmp(ValueUnion.psz, "vesa"))
                    CHECK_ERROR(pGraphicsAdapter, COMSETTER(GraphicsControllerType)(GraphicsControllerType_VBoxVGA));
#ifdef VBOX_WITH_VMSVGA
                else if (   !RTStrICmp(ValueUnion.psz, "vmsvga")
                         || !RTStrICmp(ValueUnion.psz, "vmware"))
                    CHECK_ERROR(pGraphicsAdapter, COMSETTER(GraphicsControllerType)(GraphicsControllerType_VMSVGA));
                else if (   !RTStrICmp(ValueUnion.psz, "vboxsvga")
                         || !RTStrICmp(ValueUnion.psz, "svga"))
                    CHECK_ERROR(pGraphicsAdapter, COMSETTER(GraphicsControllerType)(GraphicsControllerType_VBoxSVGA));
#endif
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --graphicscontroller argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_MONITORCOUNT:
            {
                CHECK_ERROR(pGraphicsAdapter, COMSETTER(MonitorCount)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_ACCELERATE3D:
            {
                CHECK_ERROR(pGraphicsAdapter, SetFeature(GraphicsFeature_Acceleration3D, ValueUnion.f));
                break;
            }

#ifdef VBOX_WITH_VIDEOHWACCEL
            case MODIFYVM_ACCELERATE2DVIDEO:
            {
                CHECK_ERROR(pGraphicsAdapter, SetFeature(GraphicsFeature_Acceleration2DVideo, ValueUnion.f));
                break;
            }
#endif
            case MODIFYVM_FWLOGOFADEIN:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(LogoFadeIn)(ValueUnion.f));
                break;
            }

            case MODIFYVM_FWLOGOFADEOUT:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(LogoFadeOut)(ValueUnion.f));
                break;
            }

            case MODIFYVM_FWLOGODISPLAYTIME:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(LogoDisplayTime)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_FWLOGOIMAGEPATH:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(LogoImagePath)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_FWBOOTMENU:
            {
                if (!RTStrICmp(ValueUnion.psz, "disabled"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(BootMenuMode)(FirmwareBootMenuMode_Disabled));
                }
                else if (!RTStrICmp(ValueUnion.psz, "menuonly"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(BootMenuMode)(FirmwareBootMenuMode_MenuOnly));
                }
                else if (!RTStrICmp(ValueUnion.psz, "messageandmenu"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(BootMenuMode)(FirmwareBootMenuMode_MessageAndMenu));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --biosbootmenu argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_FWAPIC:
            {
                if (!RTStrICmp(ValueUnion.psz, "disabled"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(APICMode)(APICMode_Disabled));
                }
                else if (   !RTStrICmp(ValueUnion.psz, "apic")
                         || !RTStrICmp(ValueUnion.psz, "lapic")
                         || !RTStrICmp(ValueUnion.psz, "xapic"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(APICMode)(APICMode_APIC));
                }
                else if (!RTStrICmp(ValueUnion.psz, "x2apic"))
                {
                    CHECK_ERROR(firmwareSettings, COMSETTER(APICMode)(APICMode_X2APIC));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --biosapic argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_FWSYSTEMTIMEOFFSET:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(TimeOffset)(ValueUnion.i64));
                break;
            }

            case MODIFYVM_FWPXEDEBUG:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(PXEDebugEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_SYSTEMUUIDLE:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(SMBIOSUuidLittleEndian)(ValueUnion.f));
                break;
            }

            case MODIFYVM_FWAUTOSERIALNUMGEN:
            {
                CHECK_ERROR(firmwareSettings, COMSETTER(AutoSerialNumGen)(ValueUnion.f));
                break;
            }

            case MODIFYVM_BOOT:
            {
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(sessionMachine, SetBootOrder(GetOptState.uIndex, DeviceType_Null));
                }
                else if (!RTStrICmp(ValueUnion.psz, "floppy"))
                {
                    CHECK_ERROR(sessionMachine, SetBootOrder(GetOptState.uIndex, DeviceType_Floppy));
                }
                else if (!RTStrICmp(ValueUnion.psz, "dvd"))
                {
                    CHECK_ERROR(sessionMachine, SetBootOrder(GetOptState.uIndex, DeviceType_DVD));
                }
                else if (!RTStrICmp(ValueUnion.psz, "disk"))
                {
                    CHECK_ERROR(sessionMachine, SetBootOrder(GetOptState.uIndex, DeviceType_HardDisk));
                }
                else if (!RTStrICmp(ValueUnion.psz, "net"))
                {
                    CHECK_ERROR(sessionMachine, SetBootOrder(GetOptState.uIndex, DeviceType_Network));
                }
                else
                    return errorArgument(ModifyVM::tr("Invalid boot device '%s'"), ValueUnion.psz);
                break;
            }

            case MODIFYVM_HDA: // deprecated
            case MODIFYVM_HDB: // deprecated
            case MODIFYVM_HDD: // deprecated
            case MODIFYVM_SATAPORT: // deprecated
            {
                uint32_t u1 = 0, u2 = 0;
                Bstr bstrController = L"IDE Controller";

                switch (c)
                {
                    case MODIFYVM_HDA: // deprecated
                        u1 = 0;
                    break;

                    case MODIFYVM_HDB: // deprecated
                        u1 = 0;
                        u2 = 1;
                    break;

                    case MODIFYVM_HDD: // deprecated
                        u1 = 1;
                        u2 = 1;
                    break;

                    case MODIFYVM_SATAPORT: // deprecated
                        u1 = GetOptState.uIndex;
                        bstrController = L"SATA";
                    break;
                }

                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    sessionMachine->DetachDevice(bstrController.raw(), u1, u2);
                }
                else
                {
                    ComPtr<IMedium> hardDisk;
                    hrc = openMedium(a, ValueUnion.psz, DeviceType_HardDisk,
                                    AccessMode_ReadWrite, hardDisk,
                                    false /* fForceNewUuidOnOpen */,
                                    false /* fSilent */);
                    if (FAILED(hrc))
                        break;
                    if (hardDisk)
                    {
                        CHECK_ERROR(sessionMachine, AttachDevice(bstrController.raw(),
                                                          u1, u2,
                                                          DeviceType_HardDisk,
                                                          hardDisk));
                    }
                    else
                        hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_IDECONTROLLER: // deprecated
            {
                ComPtr<IStorageController> storageController;
                CHECK_ERROR(sessionMachine, GetStorageControllerByName(Bstr("IDE Controller").raw(),
                                                                 storageController.asOutParam()));

                if (!RTStrICmp(ValueUnion.psz, "PIIX3"))
                {
                    CHECK_ERROR(storageController, COMSETTER(ControllerType)(StorageControllerType_PIIX3));
                }
                else if (!RTStrICmp(ValueUnion.psz, "PIIX4"))
                {
                    CHECK_ERROR(storageController, COMSETTER(ControllerType)(StorageControllerType_PIIX4));
                }
                else if (!RTStrICmp(ValueUnion.psz, "ICH6"))
                {
                    CHECK_ERROR(storageController, COMSETTER(ControllerType)(StorageControllerType_ICH6));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --idecontroller argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_SATAPORTCOUNT: // deprecated
            {
                ComPtr<IStorageController> SataCtl;
                CHECK_ERROR(sessionMachine, GetStorageControllerByName(Bstr("SATA").raw(),
                                                                SataCtl.asOutParam()));

                if (SUCCEEDED(hrc) && ValueUnion.u32 > 0)
                    CHECK_ERROR(SataCtl, COMSETTER(PortCount)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_SATA: // deprecated
            {
                if (!RTStrICmp(ValueUnion.psz, "on") || !RTStrICmp(ValueUnion.psz, "enable"))
                {
                    ComPtr<IStorageController> ctl;
                    CHECK_ERROR(sessionMachine, AddStorageController(Bstr("SATA").raw(),
                                                              StorageBus_SATA,
                                                              ctl.asOutParam()));
                    CHECK_ERROR(ctl, COMSETTER(ControllerType)(StorageControllerType_IntelAhci));
                }
                else if (!RTStrICmp(ValueUnion.psz, "off") || !RTStrICmp(ValueUnion.psz, "disable"))
                    CHECK_ERROR(sessionMachine, RemoveStorageController(Bstr("SATA").raw()));
                else
                    return errorArgument(ModifyVM::tr("Invalid --usb argument '%s'"), ValueUnion.psz);
                break;
            }

            case MODIFYVM_SCSIPORT: // deprecated
            {
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    hrc = sessionMachine->DetachDevice(Bstr("LsiLogic").raw(),
                                               GetOptState.uIndex, 0);
                    if (FAILED(hrc))
                        CHECK_ERROR(sessionMachine, DetachDevice(Bstr("BusLogic").raw(),
                                                          GetOptState.uIndex, 0));
                }
                else
                {
                    ComPtr<IMedium> hardDisk;
                    hrc = openMedium(a, ValueUnion.psz, DeviceType_HardDisk,
                                    AccessMode_ReadWrite, hardDisk,
                                    false /* fForceNewUuidOnOpen */,
                                    false /* fSilent */);
                    if (FAILED(hrc))
                        break;
                    if (hardDisk)
                    {
                        hrc = sessionMachine->AttachDevice(Bstr("LsiLogic").raw(),
                                                   GetOptState.uIndex, 0,
                                                   DeviceType_HardDisk,
                                                   hardDisk);
                        if (FAILED(hrc))
                            CHECK_ERROR(sessionMachine,
                                        AttachDevice(Bstr("BusLogic").raw(),
                                                     GetOptState.uIndex, 0,
                                                     DeviceType_HardDisk,
                                                     hardDisk));
                    }
                    else
                        hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_SCSITYPE: // deprecated
            {
                ComPtr<IStorageController> ctl;

                if (!RTStrICmp(ValueUnion.psz, "LsiLogic"))
                {
                    hrc = sessionMachine->RemoveStorageController(Bstr("BusLogic").raw());
                    if (FAILED(hrc))
                        CHECK_ERROR(sessionMachine, RemoveStorageController(Bstr("LsiLogic").raw()));

                    CHECK_ERROR(sessionMachine,
                                 AddStorageController(Bstr("LsiLogic").raw(),
                                                      StorageBus_SCSI,
                                                      ctl.asOutParam()));

                    if (SUCCEEDED(hrc))
                        CHECK_ERROR(ctl, COMSETTER(ControllerType)(StorageControllerType_LsiLogic));
                }
                else if (!RTStrICmp(ValueUnion.psz, "BusLogic"))
                {
                    hrc = sessionMachine->RemoveStorageController(Bstr("LsiLogic").raw());
                    if (FAILED(hrc))
                        CHECK_ERROR(sessionMachine, RemoveStorageController(Bstr("BusLogic").raw()));

                    CHECK_ERROR(sessionMachine,
                                 AddStorageController(Bstr("BusLogic").raw(),
                                                      StorageBus_SCSI,
                                                      ctl.asOutParam()));

                    if (SUCCEEDED(hrc))
                        CHECK_ERROR(ctl, COMSETTER(ControllerType)(StorageControllerType_BusLogic));
                }
                else
                    return errorArgument(ModifyVM::tr("Invalid --scsitype argument '%s'"), ValueUnion.psz);
                break;
            }

            case MODIFYVM_SCSI: // deprecated
            {
                if (!RTStrICmp(ValueUnion.psz, "on") || !RTStrICmp(ValueUnion.psz, "enable"))
                {
                    ComPtr<IStorageController> ctl;

                    CHECK_ERROR(sessionMachine, AddStorageController(Bstr("BusLogic").raw(),
                                                              StorageBus_SCSI,
                                                              ctl.asOutParam()));
                    if (SUCCEEDED(hrc))
                        CHECK_ERROR(ctl, COMSETTER(ControllerType)(StorageControllerType_BusLogic));
                }
                else if (!RTStrICmp(ValueUnion.psz, "off") || !RTStrICmp(ValueUnion.psz, "disable"))
                {
                    hrc = sessionMachine->RemoveStorageController(Bstr("BusLogic").raw());
                    if (FAILED(hrc))
                        CHECK_ERROR(sessionMachine, RemoveStorageController(Bstr("LsiLogic").raw()));
                }
                break;
            }

            case MODIFYVM_DVDPASSTHROUGH: // deprecated
            {
                CHECK_ERROR(sessionMachine, PassthroughDevice(Bstr("IDE Controller").raw(),
                                                       1, 0,
                                                       !RTStrICmp(ValueUnion.psz, "on")));
                break;
            }

            case MODIFYVM_DVD: // deprecated
            {
                ComPtr<IMedium> dvdMedium;

                /* unmount? */
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    /* nothing to do, NULL object will cause unmount */
                }
                /* host drive? */
                else if (!RTStrNICmp(ValueUnion.psz, RT_STR_TUPLE("host:")))
                {
                    ComPtr<IHost> host;
                    CHECK_ERROR(a->virtualBox, COMGETTER(Host)(host.asOutParam()));
                    hrc = host->FindHostDVDDrive(Bstr(ValueUnion.psz + 5).raw(),
                                                dvdMedium.asOutParam());
                    if (!dvdMedium)
                    {
                        /* 2nd try: try with the real name, important on Linux+libhal */
                        char szPathReal[RTPATH_MAX];
                        if (RT_FAILURE(RTPathReal(ValueUnion.psz + 5, szPathReal, sizeof(szPathReal))))
                        {
                            errorArgument(ModifyVM::tr("Invalid host DVD drive name \"%s\""), ValueUnion.psz + 5);
                            hrc = E_FAIL;
                            break;
                        }
                        hrc = host->FindHostDVDDrive(Bstr(szPathReal).raw(),
                                                    dvdMedium.asOutParam());
                        if (!dvdMedium)
                        {
                            errorArgument(ModifyVM::tr("Invalid host DVD drive name \"%s\""), ValueUnion.psz + 5);
                            hrc = E_FAIL;
                            break;
                        }
                    }
                }
                else
                {
                    hrc = openMedium(a, ValueUnion.psz, DeviceType_DVD,
                                    AccessMode_ReadOnly, dvdMedium,
                                    false /* fForceNewUuidOnOpen */,
                                    false /* fSilent */);
                    if (FAILED(hrc))
                        break;
                    if (!dvdMedium)
                    {
                        hrc = E_FAIL;
                        break;
                    }
                }

                CHECK_ERROR(sessionMachine, MountMedium(Bstr("IDE Controller").raw(),
                                                 1, 0,
                                                 dvdMedium,
                                                 FALSE /* aForce */));
                break;
            }

            case MODIFYVM_FLOPPY: // deprecated
            {
                ComPtr<IMedium> floppyMedium;
                ComPtr<IMediumAttachment> floppyAttachment;
                sessionMachine->GetMediumAttachment(Bstr("Floppy Controller").raw(),
                                             0, 0, floppyAttachment.asOutParam());

                /* disable? */
                if (!RTStrICmp(ValueUnion.psz, "disabled"))
                {
                    /* disable the controller */
                    if (floppyAttachment)
                        CHECK_ERROR(sessionMachine, DetachDevice(Bstr("Floppy Controller").raw(),
                                                          0, 0));
                }
                else
                {
                    /* enable the controller */
                    if (!floppyAttachment)
                        CHECK_ERROR(sessionMachine, AttachDeviceWithoutMedium(Bstr("Floppy Controller").raw(),
                                                                            0, 0,
                                                                            DeviceType_Floppy));

                    /* unmount? */
                    if (    !RTStrICmp(ValueUnion.psz, "none")
                        ||  !RTStrICmp(ValueUnion.psz, "empty"))   // deprecated
                    {
                        /* nothing to do, NULL object will cause unmount */
                    }
                    /* host drive? */
                    else if (!RTStrNICmp(ValueUnion.psz, RT_STR_TUPLE("host:")))
                    {
                        ComPtr<IHost> host;
                        CHECK_ERROR(a->virtualBox, COMGETTER(Host)(host.asOutParam()));
                        hrc = host->FindHostFloppyDrive(Bstr(ValueUnion.psz + 5).raw(),
                                                       floppyMedium.asOutParam());
                        if (!floppyMedium)
                        {
                            errorArgument(ModifyVM::tr("Invalid host floppy drive name \"%s\""), ValueUnion.psz + 5);
                            hrc = E_FAIL;
                            break;
                        }
                    }
                    else
                    {
                        hrc = openMedium(a, ValueUnion.psz, DeviceType_Floppy,
                                        AccessMode_ReadWrite, floppyMedium,
                                        false /* fForceNewUuidOnOpen */,
                                        false /* fSilent */);
                        if (FAILED(hrc))
                            break;
                        if (!floppyMedium)
                        {
                            hrc = E_FAIL;
                            break;
                        }
                    }
                    CHECK_ERROR(sessionMachine, MountMedium(Bstr("Floppy Controller").raw(),
                                                     0, 0,
                                                     floppyMedium,
                                                     FALSE /* aForce */));
                }
                break;
            }

            case MODIFYVM_NICTRACEFILE:
            {

                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMSETTER(TraceFile)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_NICTRACE:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMSETTER(TraceEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_NICPROPERTY:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                if (nic)
                {
                    /* Parse 'name=value' */
                    char *pszProperty = RTStrDup(ValueUnion.psz);
                    if (pszProperty)
                    {
                        char *pDelimiter = strchr(pszProperty, '=');
                        if (pDelimiter)
                        {
                            *pDelimiter = '\0';

                            Bstr bstrName = pszProperty;
                            Bstr bstrValue = &pDelimiter[1];
                            CHECK_ERROR(nic, SetProperty(bstrName.raw(), bstrValue.raw()));
                        }
                        else
                        {
                            errorArgument(ModifyVM::tr("Invalid --nicproperty%d argument '%s'"), GetOptState.uIndex, ValueUnion.psz);
                            hrc = E_FAIL;
                        }
                        RTStrFree(pszProperty);
                    }
                    else
                    {
                        RTStrmPrintf(g_pStdErr, ModifyVM::tr("Error: Failed to allocate memory for --nicproperty%d '%s'\n"),
                                     GetOptState.uIndex, ValueUnion.psz);
                        hrc = E_FAIL;
                    }
                }
                break;
            }
            case MODIFYVM_NICTYPE:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                if (!RTStrICmp(ValueUnion.psz, "Am79C970A"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_Am79C970A));
                }
                else if (!RTStrICmp(ValueUnion.psz, "Am79C973"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_Am79C973));
                }
                else if (!RTStrICmp(ValueUnion.psz, "Am79C960"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_Am79C960));
                }
#ifdef VBOX_WITH_E1000
                else if (!RTStrICmp(ValueUnion.psz, "82540EM"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_I82540EM));
                }
                else if (!RTStrICmp(ValueUnion.psz, "82543GC"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_I82543GC));
                }
                else if (!RTStrICmp(ValueUnion.psz, "82545EM"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_I82545EM));
                }
#endif
#ifdef VBOX_WITH_VIRTIO
                else if (!RTStrICmp(ValueUnion.psz, "virtio"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_Virtio));
                }
#endif /* VBOX_WITH_VIRTIO */
                else if (!RTStrICmp(ValueUnion.psz, "NE1000"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_NE1000));
                }
                else if (!RTStrICmp(ValueUnion.psz, "NE2000"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_NE2000));
                }
                else if (!RTStrICmp(ValueUnion.psz, "WD8003"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_WD8003));
                }
                else if (!RTStrICmp(ValueUnion.psz, "WD8013"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_WD8013));
                }
                else if (!RTStrICmp(ValueUnion.psz, "3C503"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_ELNK2));
                }
                else if (!RTStrICmp(ValueUnion.psz, "3C501"))
                {
                    CHECK_ERROR(nic, COMSETTER(AdapterType)(NetworkAdapterType_ELNK1));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid NIC type '%s' specified for NIC %u"),
                                  ValueUnion.psz, GetOptState.uIndex);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_NICSPEED:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMSETTER(LineSpeed)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_NICBOOTPRIO:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /* Somewhat arbitrary limitation - we can pass a list of up to 4 PCI devices
                 * to the PXE ROM, hence only boot priorities 1-4 are allowed (in addition to
                 * 0 for the default lowest priority).
                 */
                if (ValueUnion.u32 > 4)
                {
                    errorArgument(ModifyVM::tr("Invalid boot priority '%u' specfied for NIC %u"), ValueUnion.u32, GetOptState.uIndex);
                    hrc = E_FAIL;
                }
                else
                {
                    CHECK_ERROR(nic, COMSETTER(BootPriority)(ValueUnion.u32));
                }
                break;
            }

            case MODIFYVM_NICPROMISC:
            {
                NetworkAdapterPromiscModePolicy_T enmPromiscModePolicy;
                if (!RTStrICmp(ValueUnion.psz, "deny"))
                    enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_Deny;
                else if (   !RTStrICmp(ValueUnion.psz, "allow-vms")
                         || !RTStrICmp(ValueUnion.psz, "allow-network"))
                    enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_AllowNetwork;
                else if (!RTStrICmp(ValueUnion.psz, "allow-all"))
                    enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_AllowAll;
                else
                {
                    errorArgument(ModifyVM::tr("Unknown promiscuous mode policy '%s'"), ValueUnion.psz);
                    hrc = E_INVALIDARG;
                    break;
                }

                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMSETTER(PromiscModePolicy)(enmPromiscModePolicy));
                break;
            }

            case MODIFYVM_NICBWGROUP:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    /* Just remove the bandwidth group. */
                    CHECK_ERROR(nic, COMSETTER(BandwidthGroup)(NULL));
                }
                else
                {
                    ComPtr<IBandwidthControl> bwCtrl;
                    ComPtr<IBandwidthGroup> bwGroup;

                    CHECK_ERROR(sessionMachine, COMGETTER(BandwidthControl)(bwCtrl.asOutParam()));

                    if (SUCCEEDED(hrc))
                    {
                        CHECK_ERROR(bwCtrl, GetBandwidthGroup(Bstr(ValueUnion.psz).raw(), bwGroup.asOutParam()));
                        if (SUCCEEDED(hrc))
                        {
                            CHECK_ERROR(nic, COMSETTER(BandwidthGroup)(bwGroup));
                        }
                    }
                }
                break;
            }

            case MODIFYVM_NIC:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /*
                 * Check if the NIC is already enabled.  Do not try to
                 * enable it if it already is.  That makes a
                 * difference for saved VMs for which you can change
                 * the NIC attachment, but can't change the NIC
                 * enabled status (yes, the setter also should not
                 * freak out about a no-op request).
                 */
                BOOL fEnabled;;
                CHECK_ERROR(nic, COMGETTER(Enabled)(&fEnabled));

                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    if (RT_BOOL(fEnabled))
                        CHECK_ERROR(nic, COMSETTER(Enabled)(FALSE));
                }
                else if (!RTStrICmp(ValueUnion.psz, "null"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_Null));
                }
                else if (!RTStrICmp(ValueUnion.psz, "nat"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_NAT));
                }
                else if (  !RTStrICmp(ValueUnion.psz, "bridged")
                        || !RTStrICmp(ValueUnion.psz, "hostif")) /* backward compatibility */
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_Bridged));
                }
                else if (!RTStrICmp(ValueUnion.psz, "intnet"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_Internal));
                }
                else if (!RTStrICmp(ValueUnion.psz, "hostonly"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_HostOnly));
                }
#ifdef VBOX_WITH_VMNET
                else if (!RTStrICmp(ValueUnion.psz, "hostonlynet"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_HostOnlyNetwork));
                }
#endif /* VBOX_WITH_VMNET */
                else if (!RTStrICmp(ValueUnion.psz, "generic"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_Generic));
                }
                else if (!RTStrICmp(ValueUnion.psz, "natnetwork"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_NATNetwork));
                }
#ifdef VBOX_WITH_CLOUD_NET
                else if (!RTStrICmp(ValueUnion.psz, "cloud"))
                {
                    if (!fEnabled)
                        CHECK_ERROR(nic, COMSETTER(Enabled)(TRUE));
                    CHECK_ERROR(nic, COMSETTER(AttachmentType)(NetworkAttachmentType_Cloud));
                }
#endif /* VBOX_WITH_CLOUD_NET */
                else
                {
                    errorArgument(ModifyVM::tr("Invalid type '%s' specfied for NIC %u"), ValueUnion.psz, GetOptState.uIndex);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_CABLECONNECTED:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMSETTER(CableConnected)(ValueUnion.f));
                break;
            }

            case MODIFYVM_BRIDGEADAPTER:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /* remove it? */
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(nic, COMSETTER(BridgedInterface)(Bstr().raw()));
                }
                else
                {
                    CHECK_ERROR(nic, COMSETTER(BridgedInterface)(Bstr(ValueUnion.psz).raw()));
                    verifyHostNetworkInterfaceName(a->virtualBox, ValueUnion.psz,
                                                   HostNetworkInterfaceType_Bridged);
                }
                break;
            }

#ifdef VBOX_WITH_CLOUD_NET
            case MODIFYVM_CLOUDNET:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /* remove it? */
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(nic, COMSETTER(CloudNetwork)(Bstr().raw()));
                }
                else
                {
                    CHECK_ERROR(nic, COMSETTER(CloudNetwork)(Bstr(ValueUnion.psz).raw()));
                }
                break;
            }
#endif /* VBOX_WITH_CLOUD_NET */

            case MODIFYVM_HOSTONLYADAPTER:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /* remove it? */
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(nic, COMSETTER(HostOnlyInterface)(Bstr().raw()));
                }
                else
                {
                    CHECK_ERROR(nic, COMSETTER(HostOnlyInterface)(Bstr(ValueUnion.psz).raw()));
                    verifyHostNetworkInterfaceName(a->virtualBox, ValueUnion.psz,
                                                   HostNetworkInterfaceType_HostOnly);
                }
                break;
            }

#ifdef VBOX_WITH_VMNET
            case MODIFYVM_HOSTONLYNET:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /* remove it? */
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(nic, COMSETTER(HostOnlyNetwork)(Bstr().raw()));
                }
                else
                {
                    CHECK_ERROR(nic, COMSETTER(HostOnlyNetwork)(Bstr(ValueUnion.psz).raw()));
                }
                break;
            }
#endif /* VBOX_WITH_VMNET */

            case MODIFYVM_INTNET:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /* remove it? */
                if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(nic, COMSETTER(InternalNetwork)(Bstr().raw()));
                }
                else
                {
                    CHECK_ERROR(nic, COMSETTER(InternalNetwork)(Bstr(ValueUnion.psz).raw()));
                }
                break;
            }

            case MODIFYVM_GENERICDRV:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMSETTER(GenericDriver)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_NATNETWORKNAME:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMSETTER(NATNetwork)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_NATNET:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                const char *psz = ValueUnion.psz;
                if (!RTStrICmp("default", psz))
                    psz = "";

                CHECK_ERROR(engine, COMSETTER(Network)(Bstr(psz).raw()));
                break;
            }

            case MODIFYVM_NATBINDIP:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(HostIP)(Bstr(ValueUnion.psz).raw()));
                break;
            }

#define ITERATE_TO_NEXT_TERM(ch)                                                         \
    do {                                                                                 \
        while (*ch != ',')                                                               \
        {                                                                                \
            if (*ch == 0)                                                                \
            {                                                                            \
                return errorSyntax(ModifyVM::tr("Missing or invalid argument to '%s'"),  \
                                   GetOptState.pDef->pszLong);                           \
            }                                                                            \
            ch++;                                                                        \
        }                                                                                \
        *ch = '\0';                                                                      \
        ch++;                                                                            \
    } while(0)

            case MODIFYVM_NATSETTINGS:
            {
                ComPtr<INetworkAdapter> nic;
                ComPtr<INATEngine> engine;
                char *strMtu;
                char *strSockSnd;
                char *strSockRcv;
                char *strTcpSnd;
                char *strTcpRcv;
                char *strRaw = RTStrDup(ValueUnion.psz);
                char *ch = strRaw;
                strMtu = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strSockSnd = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strSockRcv = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strTcpSnd = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strTcpRcv = RTStrStrip(ch);

                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));
                CHECK_ERROR(engine, SetNetworkSettings(RTStrToUInt32(strMtu), RTStrToUInt32(strSockSnd), RTStrToUInt32(strSockRcv),
                                    RTStrToUInt32(strTcpSnd), RTStrToUInt32(strTcpRcv)));
                break;
            }


            case MODIFYVM_NATPF:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                /* format name:proto:hostip:hostport:guestip:guestport*/
                if (RTStrCmp(ValueUnion.psz, "delete") != 0)
                {
                    char *strName;
                    char *strProto;
                    char *strHostIp;
                    char *strHostPort;
                    char *strGuestIp;
                    char *strGuestPort;
                    char *strRaw = RTStrDup(ValueUnion.psz);
                    char *ch = strRaw;
                    strName = RTStrStrip(ch);
                    ITERATE_TO_NEXT_TERM(ch);
                    strProto = RTStrStrip(ch);
                    ITERATE_TO_NEXT_TERM(ch);
                    strHostIp = RTStrStrip(ch);
                    ITERATE_TO_NEXT_TERM(ch);
                    strHostPort = RTStrStrip(ch);
                    ITERATE_TO_NEXT_TERM(ch);
                    strGuestIp = RTStrStrip(ch);
                    ITERATE_TO_NEXT_TERM(ch);
                    strGuestPort = RTStrStrip(ch);
                    NATProtocol_T proto;
                    if (RTStrICmp(strProto, "udp") == 0)
                        proto = NATProtocol_UDP;
                    else if (RTStrICmp(strProto, "tcp") == 0)
                        proto = NATProtocol_TCP;
                    else
                    {
                        errorArgument(ModifyVM::tr("Invalid proto '%s' specfied for NIC %u"), ValueUnion.psz, GetOptState.uIndex);
                        hrc = E_FAIL;
                        break;
                    }
                    CHECK_ERROR(engine, AddRedirect(Bstr(strName).raw(), proto,
                                        Bstr(strHostIp).raw(),
                                        RTStrToUInt16(strHostPort),
                                        Bstr(strGuestIp).raw(),
                                        RTStrToUInt16(strGuestPort)));
                }
                else
                {
                    /* delete NAT Rule operation */
                    int vrc;
                    vrc = RTGetOptFetchValue(&GetOptState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_FAILURE(vrc))
                        return errorSyntax(ModifyVM::tr("Not enough parameters"));
                    CHECK_ERROR(engine, RemoveRedirect(Bstr(ValueUnion.psz).raw()));
                }
                break;
            }
            #undef ITERATE_TO_NEXT_TERM
            case MODIFYVM_NATALIASMODE:
            {
                ComPtr<INetworkAdapter> nic;
                ComPtr<INATEngine> engine;
                uint32_t aliasMode = 0;

                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));
                if (RTStrCmp(ValueUnion.psz, "default") == 0)
                    aliasMode = 0;
                else
                {
                    char *token = (char *)ValueUnion.psz;
                    while (token)
                    {
                        if (RTStrNCmp(token, RT_STR_TUPLE("log")) == 0)
                            aliasMode |= NATAliasMode_AliasLog;
                        else if (RTStrNCmp(token, RT_STR_TUPLE("proxyonly")) == 0)
                            aliasMode |= NATAliasMode_AliasProxyOnly;
                        else if (RTStrNCmp(token, RT_STR_TUPLE("sameports")) == 0)
                            aliasMode |= NATAliasMode_AliasUseSamePorts;
                        token = RTStrStr(token, ",");
                        if (token == NULL)
                            break;
                        token++;
                    }
                }
                CHECK_ERROR(engine, COMSETTER(AliasMode)(aliasMode));
                break;
            }

            case MODIFYVM_NATTFTPPREFIX:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(TFTPPrefix)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_NATTFTPFILE:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(TFTPBootFile)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_NATTFTPSERVER:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(TFTPNextServer)(Bstr(ValueUnion.psz).raw()));
                break;
            }
            case MODIFYVM_NATDNSPASSDOMAIN:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(DNSPassDomain)(ValueUnion.f));
                break;
            }

            case MODIFYVM_NATDNSPROXY:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(DNSProxy)(ValueUnion.f));
                break;
            }

            case MODIFYVM_NATDNSHOSTRESOLVER:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(DNSUseHostResolver)(ValueUnion.f));
                break;
            }

            case MODIFYVM_NATLOCALHOSTREACHABLE:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(LocalhostReachable)(ValueUnion.f));
                break;
            }

            case MODIFYVM_NATFORWARDBROADCAST:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                ComPtr<INATEngine> engine;
                CHECK_ERROR(nic, COMGETTER(NATEngine)(engine.asOutParam()));

                CHECK_ERROR(engine, COMSETTER(ForwardBroadcast)(ValueUnion.f));
                break;
            }

            case MODIFYVM_MACADDRESS:
            {
                if (!parseNum(GetOptState.uIndex, NetworkAdapterCount, "NIC"))
                    break;

                ComPtr<INetworkAdapter> nic;
                CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(GetOptState.uIndex - 1, nic.asOutParam()));
                ASSERT(nic);

                /* generate one? */
                if (!RTStrICmp(ValueUnion.psz, "auto"))
                {
                    CHECK_ERROR(nic, COMSETTER(MACAddress)(Bstr().raw()));
                }
                else
                {
                    CHECK_ERROR(nic, COMSETTER(MACAddress)(Bstr(ValueUnion.psz).raw()));
                }
                break;
            }

            case MODIFYVM_HIDPTR:
            {
                bool fEnableUsb = false;
                if (!RTStrICmp(ValueUnion.psz, "ps2"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(PointingHIDType)(PointingHIDType_PS2Mouse));
                }
                else if (!RTStrICmp(ValueUnion.psz, "usb"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(PointingHIDType)(PointingHIDType_USBMouse));
                    if (SUCCEEDED(hrc))
                        fEnableUsb = true;
                }
                else if (!RTStrICmp(ValueUnion.psz, "usbtablet"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(PointingHIDType)(PointingHIDType_USBTablet));
                    if (SUCCEEDED(hrc))
                        fEnableUsb = true;
                }
                else if (!RTStrICmp(ValueUnion.psz, "usbmultitouch"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(PointingHIDType)(PointingHIDType_USBMultiTouch));
                    if (SUCCEEDED(hrc))
                        fEnableUsb = true;
                }
                else if (!RTStrICmp(ValueUnion.psz, "usbmtscreenpluspad"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(PointingHIDType)(PointingHIDType_USBMultiTouchScreenPlusPad));
                    if (SUCCEEDED(hrc))
                        fEnableUsb = true;
                }
                else if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(PointingHIDType)(PointingHIDType_None));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid type '%s' specfied for pointing device"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                if (fEnableUsb)
                {
                    /* Make sure either the OHCI or xHCI controller is enabled. */
                    ULONG cOhciCtrls = 0;
                    ULONG cXhciCtrls = 0;
                    hrc = sessionMachine->GetUSBControllerCountByType(USBControllerType_OHCI, &cOhciCtrls);
                    if (SUCCEEDED(hrc)) {
                        hrc = sessionMachine->GetUSBControllerCountByType(USBControllerType_XHCI, &cXhciCtrls);
                        if (   SUCCEEDED(hrc)
                            && cOhciCtrls + cXhciCtrls == 0)
                        {
                            /* If there's nothing, enable OHCI (always available). */
                            ComPtr<IUSBController> UsbCtl;
                            CHECK_ERROR(sessionMachine, AddUSBController(Bstr("OHCI").raw(), USBControllerType_OHCI,
                                                                  UsbCtl.asOutParam()));
                        }
                    }
                }
                break;
            }

            case MODIFYVM_HIDKBD:
            {
                bool fEnableUsb = false;
                if (!RTStrICmp(ValueUnion.psz, "ps2"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(KeyboardHIDType)(KeyboardHIDType_PS2Keyboard));
                }
                else if (!RTStrICmp(ValueUnion.psz, "usb"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(KeyboardHIDType)(KeyboardHIDType_USBKeyboard));
                    if (SUCCEEDED(hrc))
                        fEnableUsb = true;
                }
                else if (!RTStrICmp(ValueUnion.psz, "none"))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(KeyboardHIDType)(KeyboardHIDType_None));
                    if (SUCCEEDED(hrc))
                        fEnableUsb = true;
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid type '%s' specfied for keyboard"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                if (fEnableUsb)
                {
                    /* Make sure either the OHCI or xHCI controller is enabled. */
                    ULONG cOhciCtrls = 0;
                    ULONG cXhciCtrls = 0;
                    hrc = sessionMachine->GetUSBControllerCountByType(USBControllerType_OHCI, &cOhciCtrls);
                    if (SUCCEEDED(hrc)) {
                        hrc = sessionMachine->GetUSBControllerCountByType(USBControllerType_XHCI, &cXhciCtrls);
                        if (   SUCCEEDED(hrc)
                            && cOhciCtrls + cXhciCtrls == 0)
                        {
                            /* If there's nothing, enable OHCI (always available). */
                            ComPtr<IUSBController> UsbCtl;
                            CHECK_ERROR(sessionMachine, AddUSBController(Bstr("OHCI").raw(), USBControllerType_OHCI,
                                                                  UsbCtl.asOutParam()));
                        }
                    }
                }
                break;
            }

            case MODIFYVM_UARTMODE:
            {
                ComPtr<ISerialPort> uart;

                CHECK_ERROR_BREAK(sessionMachine, GetSerialPort(GetOptState.uIndex - 1, uart.asOutParam()));
                ASSERT(uart);

                if (!RTStrICmp(ValueUnion.psz, "disconnected"))
                {
                    CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_Disconnected));
                }
                else if (   !RTStrICmp(ValueUnion.psz, "server")
                         || !RTStrICmp(ValueUnion.psz, "client")
                         || !RTStrICmp(ValueUnion.psz, "tcpserver")
                         || !RTStrICmp(ValueUnion.psz, "tcpclient")
                         || !RTStrICmp(ValueUnion.psz, "file"))
                {
                    const char *pszMode = ValueUnion.psz;

                    int vrc = RTGetOptFetchValue(&GetOptState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_FAILURE(vrc))
                        return errorSyntax(ModifyVM::tr("Missing or invalid argument to '%s'"),
                                           GetOptState.pDef->pszLong);

                    CHECK_ERROR(uart, COMSETTER(Path)(Bstr(ValueUnion.psz).raw()));

                    if (!RTStrICmp(pszMode, "server"))
                    {
                        CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_HostPipe));
                        CHECK_ERROR(uart, COMSETTER(Server)(TRUE));
                    }
                    else if (!RTStrICmp(pszMode, "client"))
                    {
                        CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_HostPipe));
                        CHECK_ERROR(uart, COMSETTER(Server)(FALSE));
                    }
                    else if (!RTStrICmp(pszMode, "tcpserver"))
                    {
                        CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_TCP));
                        CHECK_ERROR(uart, COMSETTER(Server)(TRUE));
                    }
                    else if (!RTStrICmp(pszMode, "tcpclient"))
                    {
                        CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_TCP));
                        CHECK_ERROR(uart, COMSETTER(Server)(FALSE));
                    }
                    else if (!RTStrICmp(pszMode, "file"))
                    {
                        CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_RawFile));
                    }
                }
                else
                {
                    CHECK_ERROR(uart, COMSETTER(Path)(Bstr(ValueUnion.psz).raw()));
                    CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_HostDevice));
                }
                break;
            }

            case MODIFYVM_UARTTYPE:
            {
                ComPtr<ISerialPort> uart;

                CHECK_ERROR_BREAK(sessionMachine, GetSerialPort(GetOptState.uIndex - 1, uart.asOutParam()));
                ASSERT(uart);

                if (!RTStrICmp(ValueUnion.psz, "16450"))
                {
                    CHECK_ERROR(uart, COMSETTER(UartType)(UartType_U16450));
                }
                else if (!RTStrICmp(ValueUnion.psz, "16550A"))
                {
                    CHECK_ERROR(uart, COMSETTER(UartType)(UartType_U16550A));
                }
                else if (!RTStrICmp(ValueUnion.psz, "16750"))
                {
                    CHECK_ERROR(uart, COMSETTER(UartType)(UartType_U16750));
                }
                else
                    return errorSyntax(ModifyVM::tr("Invalid argument to '%s'"),
                                       GetOptState.pDef->pszLong);
                break;
            }

            case MODIFYVM_UART:
            {
                ComPtr<ISerialPort> uart;

                CHECK_ERROR_BREAK(sessionMachine, GetSerialPort(GetOptState.uIndex - 1, uart.asOutParam()));
                ASSERT(uart);

                if (!RTStrICmp(ValueUnion.psz, "off") || !RTStrICmp(ValueUnion.psz, "disable"))
                    CHECK_ERROR(uart, COMSETTER(Enabled)(FALSE));
                else
                {
                    const char *pszIOBase = ValueUnion.psz;
                    uint32_t uVal = 0;

                    int vrc = RTGetOptFetchValue(&GetOptState, &ValueUnion, RTGETOPT_REQ_UINT32) != MODIFYVM_UART;
                    if (RT_FAILURE(vrc))
                        return errorSyntax(ModifyVM::tr("Missing or invalid argument to '%s'"),
                                           GetOptState.pDef->pszLong);

                    CHECK_ERROR(uart, COMSETTER(IRQ)(ValueUnion.u32));

                    vrc = RTStrToUInt32Ex(pszIOBase, NULL, 0, &uVal);
                    if (vrc != VINF_SUCCESS || uVal == 0)
                        return errorArgument(ModifyVM::tr("Error parsing UART I/O base '%s'"), pszIOBase);
                    CHECK_ERROR(uart, COMSETTER(IOAddress)(uVal));

                    CHECK_ERROR(uart, COMSETTER(Enabled)(TRUE));
                }
                break;
            }

#if defined(RT_OS_LINUX) || defined(RT_OS_WINDOWS)
            case MODIFYVM_LPTMODE:
            {
                ComPtr<IParallelPort> lpt;

                CHECK_ERROR_BREAK(sessionMachine, GetParallelPort(GetOptState.uIndex - 1, lpt.asOutParam()));
                ASSERT(lpt);

                CHECK_ERROR(lpt, COMSETTER(Path)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_LPT:
            {
                ComPtr<IParallelPort> lpt;

                CHECK_ERROR_BREAK(sessionMachine, GetParallelPort(GetOptState.uIndex - 1, lpt.asOutParam()));
                ASSERT(lpt);

                if (!RTStrICmp(ValueUnion.psz, "off") || !RTStrICmp(ValueUnion.psz, "disable"))
                    CHECK_ERROR(lpt, COMSETTER(Enabled)(FALSE));
                else
                {
                    const char *pszIOBase = ValueUnion.psz;
                    uint32_t uVal = 0;

                    int vrc = RTGetOptFetchValue(&GetOptState, &ValueUnion, RTGETOPT_REQ_UINT32) != MODIFYVM_LPT;
                    if (RT_FAILURE(vrc))
                        return errorSyntax(ModifyVM::tr("Missing or invalid argument to '%s'"),
                                           GetOptState.pDef->pszLong);

                    CHECK_ERROR(lpt, COMSETTER(IRQ)(ValueUnion.u32));

                    vrc = RTStrToUInt32Ex(pszIOBase, NULL, 0, &uVal);
                    if (vrc != VINF_SUCCESS || uVal == 0)
                        return errorArgument(ModifyVM::tr("Error parsing LPT I/O base '%s'"), pszIOBase);
                    CHECK_ERROR(lpt, COMSETTER(IOBase)(uVal));

                    CHECK_ERROR(lpt, COMSETTER(Enabled)(TRUE));
                }
                break;
            }
#endif

            case MODIFYVM_GUESTMEMORYBALLOON:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(MemoryBalloonSize)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_AUDIOCONTROLLER:
            {
                ComPtr<IAudioSettings> audioSettings;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
                ComPtr<IAudioAdapter> audioAdapter;
                CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(audioAdapter.asOutParam()));
                ASSERT(audioAdapter);

                if (!RTStrICmp(ValueUnion.psz, "sb16"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioController)(AudioControllerType_SB16));
                else if (!RTStrICmp(ValueUnion.psz, "ac97"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioController)(AudioControllerType_AC97));
                else if (!RTStrICmp(ValueUnion.psz, "hda"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioController)(AudioControllerType_HDA));
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --audiocontroller argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_AUDIOCODEC:
            {
                ComPtr<IAudioSettings> audioSettings;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
                ComPtr<IAudioAdapter> audioAdapter;
                CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(audioAdapter.asOutParam()));
                ASSERT(audioAdapter);

                if (!RTStrICmp(ValueUnion.psz, "sb16"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioCodec)(AudioCodecType_SB16));
                else if (!RTStrICmp(ValueUnion.psz, "stac9700"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioCodec)(AudioCodecType_STAC9700));
                else if (!RTStrICmp(ValueUnion.psz, "ad1980"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioCodec)(AudioCodecType_AD1980));
                else if (!RTStrICmp(ValueUnion.psz, "stac9221"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioCodec)(AudioCodecType_STAC9221));
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --audiocodec argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_AUDIODRIVER:
                RT_FALL_THROUGH();
            case MODIFYVM_AUDIO: /** @todo Deprecated; remove. */
            {
                if (c == MODIFYVM_AUDIO)
                    RTStrmPrintf(g_pStdErr,
                                 ModifyVM::tr("Warning: --audio is deprecated and will be removed soon. Use --audio-driver instead!\n"));

                ComPtr<IAudioSettings> audioSettings;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
                ComPtr<IAudioAdapter> audioAdapter;
                CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(audioAdapter.asOutParam()));
                ASSERT(audioAdapter);
                /* disable? */
                if (   !RTStrICmp(ValueUnion.psz, "none")
                    || !RTStrICmp(ValueUnion.psz, "null"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_Null));
                else if (!RTStrICmp(ValueUnion.psz, "default"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_Default));
#ifdef RT_OS_WINDOWS
# ifdef VBOX_WITH_WINMM
                else if (!RTStrICmp(ValueUnion.psz, "winmm"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_WinMM));
# endif
                else if (!RTStrICmp(ValueUnion.psz, "dsound"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_DirectSound));
                else if (!RTStrICmp(ValueUnion.psz, "was"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_WAS));
#endif /* RT_OS_WINDOWS */
#ifdef VBOX_WITH_AUDIO_OSS
                else if (!RTStrICmp(ValueUnion.psz, "oss"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_OSS));
#endif
#ifdef VBOX_WITH_AUDIO_ALSA
                else if (!RTStrICmp(ValueUnion.psz, "alsa"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_ALSA));
#endif
#ifdef VBOX_WITH_AUDIO_PULSE
                else if (!RTStrICmp(ValueUnion.psz, "pulse"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_Pulse));
#endif
#ifdef RT_OS_DARWIN
                else if (!RTStrICmp(ValueUnion.psz, "coreaudio"))
                    CHECK_ERROR(audioAdapter, COMSETTER(AudioDriver)(AudioDriverType_CoreAudio));
#endif /* !RT_OS_DARWIN */
                else
                {
                    errorArgument(ModifyVM::tr("Invalid %s argument '%s'"),
                                  c == MODIFYVM_AUDIO ? "--audio" : "--audio-driver", ValueUnion.psz);
                    hrc = E_FAIL;
                }

                if (   SUCCEEDED(hrc)
                    && c == MODIFYVM_AUDIO) /* To keep the original behavior until we remove the command. */
                    CHECK_ERROR(audioAdapter, COMSETTER(Enabled)(RTStrICmp(ValueUnion.psz, "none") == false ? false : true));

                break;
            }

            case MODIFYVM_AUDIOENABLED:
            {
                ComPtr<IAudioSettings> audioSettings;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
                ComPtr<IAudioAdapter> audioAdapter;
                CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(audioAdapter.asOutParam()));
                ASSERT(audioAdapter);

                CHECK_ERROR(audioAdapter, COMSETTER(Enabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_AUDIOIN:
            {
                ComPtr<IAudioSettings> audioSettings;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
                ComPtr<IAudioAdapter> audioAdapter;
                CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(audioAdapter.asOutParam()));
                ASSERT(audioAdapter);

                CHECK_ERROR(audioAdapter, COMSETTER(EnabledIn)(ValueUnion.f));
                break;
            }

            case MODIFYVM_AUDIOOUT:
            {
                ComPtr<IAudioSettings> audioSettings;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
                ComPtr<IAudioAdapter> audioAdapter;
                CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(audioAdapter.asOutParam()));
                ASSERT(audioAdapter);

                CHECK_ERROR(audioAdapter, COMSETTER(EnabledOut)(ValueUnion.f));
                break;
            }

#ifdef VBOX_WITH_SHARED_CLIPBOARD
            case MODIFYVM_CLIPBOARD_MODE:
            {
                ClipboardMode_T mode = ClipboardMode_Disabled; /* Shut up MSC */
                if (!RTStrICmp(ValueUnion.psz, "disabled"))
                    mode = ClipboardMode_Disabled;
                else if (!RTStrICmp(ValueUnion.psz, "hosttoguest"))
                    mode = ClipboardMode_HostToGuest;
                else if (!RTStrICmp(ValueUnion.psz, "guesttohost"))
                    mode = ClipboardMode_GuestToHost;
                else if (!RTStrICmp(ValueUnion.psz, "bidirectional"))
                    mode = ClipboardMode_Bidirectional;
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --clipboard-mode argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                if (SUCCEEDED(hrc))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(ClipboardMode)(mode));
                }
                break;
            }

# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            case MODIFYVM_CLIPBOARD_FILE_TRANSFERS:
            {
                BOOL fEnabled = false; /* Shut up MSC */
                if (!RTStrICmp(ValueUnion.psz, "enabled"))
                    fEnabled = true;
                else if (!RTStrICmp(ValueUnion.psz, "disabled"))
                    fEnabled = false;
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --clipboard-file-transfers argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                if (SUCCEEDED(hrc))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(ClipboardFileTransfersEnabled)(fEnabled));
                }
                break;
            }
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
#endif /* VBOX_WITH_SHARED_CLIPBOARD */

            case MODIFYVM_DRAGANDDROP:
            {
                DnDMode_T mode = DnDMode_Disabled; /* Shut up MSC */
                if (!RTStrICmp(ValueUnion.psz, "disabled"))
                    mode = DnDMode_Disabled;
                else if (!RTStrICmp(ValueUnion.psz, "hosttoguest"))
                    mode = DnDMode_HostToGuest;
                else if (!RTStrICmp(ValueUnion.psz, "guesttohost"))
                    mode = DnDMode_GuestToHost;
                else if (!RTStrICmp(ValueUnion.psz, "bidirectional"))
                    mode = DnDMode_Bidirectional;
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --draganddrop argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                if (SUCCEEDED(hrc))
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(DnDMode)(mode));
                }
                break;
            }

            case MODIFYVM_VRDE_EXTPACK:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                if (vrdeServer)
                {
                    if (RTStrICmp(ValueUnion.psz, "default") != 0)
                    {
                        Bstr bstr(ValueUnion.psz);
                        CHECK_ERROR(vrdeServer, COMSETTER(VRDEExtPack)(bstr.raw()));
                    }
                    else
                        CHECK_ERROR(vrdeServer, COMSETTER(VRDEExtPack)(Bstr().raw()));
                }
                break;
            }

            case MODIFYVM_VRDEPROPERTY:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                if (vrdeServer)
                {
                    /* Parse 'name=value' */
                    char *pszProperty = RTStrDup(ValueUnion.psz);
                    if (pszProperty)
                    {
                        char *pDelimiter = strchr(pszProperty, '=');
                        if (pDelimiter)
                        {
                            *pDelimiter = '\0';

                            Bstr bstrName = pszProperty;
                            Bstr bstrValue = &pDelimiter[1];
                            CHECK_ERROR(vrdeServer, SetVRDEProperty(bstrName.raw(), bstrValue.raw()));
                        }
                        else
                        {
                            RTStrFree(pszProperty);

                            errorArgument(ModifyVM::tr("Invalid --vrdeproperty argument '%s'"), ValueUnion.psz);
                            hrc = E_FAIL;
                            break;
                        }
                        RTStrFree(pszProperty);
                    }
                    else
                    {
                        RTStrmPrintf(g_pStdErr, ModifyVM::tr("Error: Failed to allocate memory for VRDE property '%s'\n"),
                                     ValueUnion.psz);
                        hrc = E_FAIL;
                    }
                }
                break;
            }

            case MODIFYVM_VRDPPORT:
                vrdeWarningDeprecatedOption("port");
                RT_FALL_THRU();

            case MODIFYVM_VRDEPORT:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                if (!RTStrICmp(ValueUnion.psz, "default"))
                    CHECK_ERROR(vrdeServer, SetVRDEProperty(Bstr("TCP/Ports").raw(), Bstr("0").raw()));
                else
                    CHECK_ERROR(vrdeServer, SetVRDEProperty(Bstr("TCP/Ports").raw(), Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_VRDPADDRESS:
                vrdeWarningDeprecatedOption("address");
                RT_FALL_THRU();

            case MODIFYVM_VRDEADDRESS:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                CHECK_ERROR(vrdeServer, SetVRDEProperty(Bstr("TCP/Address").raw(), Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_VRDPAUTHTYPE:
                vrdeWarningDeprecatedOption("authtype");
                RT_FALL_THRU();
            case MODIFYVM_VRDEAUTHTYPE:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                if (!RTStrICmp(ValueUnion.psz, "null"))
                {
                    CHECK_ERROR(vrdeServer, COMSETTER(AuthType)(AuthType_Null));
                }
                else if (!RTStrICmp(ValueUnion.psz, "external"))
                {
                    CHECK_ERROR(vrdeServer, COMSETTER(AuthType)(AuthType_External));
                }
                else if (!RTStrICmp(ValueUnion.psz, "guest"))
                {
                    CHECK_ERROR(vrdeServer, COMSETTER(AuthType)(AuthType_Guest));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --vrdeauthtype argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_VRDEAUTHLIBRARY:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                if (vrdeServer)
                {
                    if (RTStrICmp(ValueUnion.psz, "default") != 0)
                    {
                        Bstr bstr(ValueUnion.psz);
                        CHECK_ERROR(vrdeServer, COMSETTER(AuthLibrary)(bstr.raw()));
                    }
                    else
                        CHECK_ERROR(vrdeServer, COMSETTER(AuthLibrary)(Bstr().raw()));
                }
                break;
            }

            case MODIFYVM_VRDPMULTICON:
                vrdeWarningDeprecatedOption("multicon");
                RT_FALL_THRU();
            case MODIFYVM_VRDEMULTICON:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                CHECK_ERROR(vrdeServer, COMSETTER(AllowMultiConnection)(ValueUnion.f));
                break;
            }

            case MODIFYVM_VRDPREUSECON:
                vrdeWarningDeprecatedOption("reusecon");
                RT_FALL_THRU();
            case MODIFYVM_VRDEREUSECON:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                CHECK_ERROR(vrdeServer, COMSETTER(ReuseSingleConnection)(ValueUnion.f));
                break;
            }

            case MODIFYVM_VRDPVIDEOCHANNEL:
                vrdeWarningDeprecatedOption("videochannel");
                RT_FALL_THRU();
            case MODIFYVM_VRDEVIDEOCHANNEL:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                CHECK_ERROR(vrdeServer, SetVRDEProperty(Bstr("VideoChannel/Enabled").raw(),
                                                        ValueUnion.f? Bstr("true").raw():  Bstr("false").raw()));
                break;
            }

            case MODIFYVM_VRDPVIDEOCHANNELQUALITY:
                vrdeWarningDeprecatedOption("videochannelquality");
                RT_FALL_THRU();
            case MODIFYVM_VRDEVIDEOCHANNELQUALITY:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                CHECK_ERROR(vrdeServer, SetVRDEProperty(Bstr("VideoChannel/Quality").raw(),
                                                        Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_VRDP:
                vrdeWarningDeprecatedOption("");
                RT_FALL_THRU();
            case MODIFYVM_VRDE:
            {
                ComPtr<IVRDEServer> vrdeServer;
                sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                ASSERT(vrdeServer);

                CHECK_ERROR(vrdeServer, COMSETTER(Enabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_USBRENAME:
            {
                const char *pszName = ValueUnion.psz;
                int vrc = RTGetOptFetchValue(&GetOptState, &ValueUnion, RTGETOPT_REQ_STRING);
                if (RT_FAILURE(vrc))
                    return errorSyntax(ModifyVM::tr("Missing or invalid argument to '%s'"),
                                       GetOptState.pDef->pszLong);
                const char *pszNewName = ValueUnion.psz;

                SafeIfaceArray<IUSBController> ctrls;
                CHECK_ERROR(sessionMachine, COMGETTER(USBControllers)(ComSafeArrayAsOutParam(ctrls)));
                bool fRenamed = false;
                for (size_t i = 0; i < ctrls.size(); i++)
                {
                    ComPtr<IUSBController> pCtrl = ctrls[i];
                    Bstr bstrName;
                    CHECK_ERROR(pCtrl, COMGETTER(Name)(bstrName.asOutParam()));
                    if (bstrName == pszName)
                    {
                        bstrName = pszNewName;
                        CHECK_ERROR(pCtrl, COMSETTER(Name)(bstrName.raw()));
                        fRenamed = true;
                    }
                }
                if (!fRenamed)
                {
                    errorArgument(ModifyVM::tr("Invalid --usbrename parameters, nothing renamed"));
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_USBXHCI:
            {
                ULONG cXhciCtrls = 0;
                hrc = sessionMachine->GetUSBControllerCountByType(USBControllerType_XHCI, &cXhciCtrls);
                if (SUCCEEDED(hrc))
                {
                    if (!cXhciCtrls && ValueUnion.f)
                    {
                        ComPtr<IUSBController> UsbCtl;
                        CHECK_ERROR(sessionMachine, AddUSBController(Bstr("xHCI").raw(), USBControllerType_XHCI,
                                                              UsbCtl.asOutParam()));
                    }
                    else if (cXhciCtrls && !ValueUnion.f)
                    {
                        SafeIfaceArray<IUSBController> ctrls;
                        CHECK_ERROR(sessionMachine, COMGETTER(USBControllers)(ComSafeArrayAsOutParam(ctrls)));
                        for (size_t i = 0; i < ctrls.size(); i++)
                        {
                            ComPtr<IUSBController> pCtrl = ctrls[i];
                            USBControllerType_T enmType;
                            CHECK_ERROR(pCtrl, COMGETTER(Type)(&enmType));
                            if (enmType == USBControllerType_XHCI)
                            {
                                Bstr ctrlName;
                                CHECK_ERROR(pCtrl, COMGETTER(Name)(ctrlName.asOutParam()));
                                CHECK_ERROR(sessionMachine, RemoveUSBController(ctrlName.raw()));
                            }
                        }
                    }
                }
                break;
            }

            case MODIFYVM_USBEHCI:
            {
                ULONG cEhciCtrls = 0;
                hrc = sessionMachine->GetUSBControllerCountByType(USBControllerType_EHCI, &cEhciCtrls);
                if (SUCCEEDED(hrc))
                {
                    if (!cEhciCtrls && ValueUnion.f)
                    {
                        ComPtr<IUSBController> UsbCtl;
                        CHECK_ERROR(sessionMachine, AddUSBController(Bstr("EHCI").raw(), USBControllerType_EHCI,
                                                              UsbCtl.asOutParam()));
                    }
                    else if (cEhciCtrls && !ValueUnion.f)
                    {
                        SafeIfaceArray<IUSBController> ctrls;
                        CHECK_ERROR(sessionMachine, COMGETTER(USBControllers)(ComSafeArrayAsOutParam(ctrls)));
                        for (size_t i = 0; i < ctrls.size(); i++)
                        {
                            ComPtr<IUSBController> pCtrl = ctrls[i];
                            USBControllerType_T enmType;
                            CHECK_ERROR(pCtrl, COMGETTER(Type)(&enmType));
                            if (enmType == USBControllerType_EHCI)
                            {
                                Bstr ctrlName;
                                CHECK_ERROR(pCtrl, COMGETTER(Name)(ctrlName.asOutParam()));
                                CHECK_ERROR(sessionMachine, RemoveUSBController(ctrlName.raw()));
                            }
                        }
                    }
                }
                break;
            }

            case MODIFYVM_USBOHCI:
            {
                ULONG cOhciCtrls = 0;
                hrc = sessionMachine->GetUSBControllerCountByType(USBControllerType_OHCI, &cOhciCtrls);
                if (SUCCEEDED(hrc))
                {
                    if (!cOhciCtrls && ValueUnion.f)
                    {
                        ComPtr<IUSBController> UsbCtl;
                        CHECK_ERROR(sessionMachine, AddUSBController(Bstr("OHCI").raw(), USBControllerType_OHCI,
                                                              UsbCtl.asOutParam()));
                    }
                    else if (cOhciCtrls && !ValueUnion.f)
                    {
                        SafeIfaceArray<IUSBController> ctrls;
                        CHECK_ERROR(sessionMachine, COMGETTER(USBControllers)(ComSafeArrayAsOutParam(ctrls)));
                        for (size_t i = 0; i < ctrls.size(); i++)
                        {
                            ComPtr<IUSBController> pCtrl = ctrls[i];
                            USBControllerType_T enmType;
                            CHECK_ERROR(pCtrl, COMGETTER(Type)(&enmType));
                            if (enmType == USBControllerType_OHCI)
                            {
                                Bstr ctrlName;
                                CHECK_ERROR(pCtrl, COMGETTER(Name)(ctrlName.asOutParam()));
                                CHECK_ERROR(sessionMachine, RemoveUSBController(ctrlName.raw()));
                            }
                        }
                    }
                }
                break;
            }

            case MODIFYVM_SNAPSHOTFOLDER:
            {
                if (!RTStrICmp(ValueUnion.psz, "default"))
                    CHECK_ERROR(sessionMachine, COMSETTER(SnapshotFolder)(Bstr().raw()));
                else
                    CHECK_ERROR(sessionMachine, COMSETTER(SnapshotFolder)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_TELEPORTER_ENABLED:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(TeleporterEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_TELEPORTER_PORT:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(TeleporterPort)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_TELEPORTER_ADDRESS:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(TeleporterAddress)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_TELEPORTER_PASSWORD:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(TeleporterPassword)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_TELEPORTER_PASSWORD_FILE:
            {
                Utf8Str password;
                RTEXITCODE rcExit = readPasswordFile(ValueUnion.psz, &password);
                if (rcExit != RTEXITCODE_SUCCESS)
                    hrc = E_FAIL;
                else
                    CHECK_ERROR(sessionMachine, COMSETTER(TeleporterPassword)(Bstr(password).raw()));
                break;
            }

            case MODIFYVM_TRACING_ENABLED:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(TracingEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_TRACING_CONFIG:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(TracingConfig)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_TRACING_ALLOW_VM_ACCESS:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(AllowTracingToAccessVM)(ValueUnion.f));
                break;
            }

            case MODIFYVM_HARDWARE_UUID:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(HardwareUUID)(Bstr(ValueUnion.psz).raw()));
                break;
            }

            case MODIFYVM_IOCACHE:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(IOCacheEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_IOCACHESIZE:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(IOCacheSize)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_CHIPSET:
            {
                if (!RTStrICmp(ValueUnion.psz, "piix3"))
                {
                    CHECK_ERROR(platform, COMSETTER(ChipsetType)(ChipsetType_PIIX3));
                }
                else if (!RTStrICmp(ValueUnion.psz, "ich9"))
                {
                    CHECK_ERROR(platform, COMSETTER(ChipsetType)(ChipsetType_ICH9));
                    BOOL fIoApic = FALSE;
                    CHECK_ERROR(firmwareSettings, COMGETTER(IOAPICEnabled)(&fIoApic));
                    if (!fIoApic)
                    {
                        RTStrmPrintf(g_pStdErr, ModifyVM::tr("*** I/O APIC must be enabled for ICH9, enabling. ***\n"));
                        CHECK_ERROR(firmwareSettings, COMSETTER(IOAPICEnabled)(TRUE));
                    }
                }
                else if (   !RTStrICmp(ValueUnion.psz, "armv8")
                         || !RTStrICmp(ValueUnion.psz, "armv8virtual"))
                {
                    CHECK_ERROR(platform, COMSETTER(ChipsetType)(ChipsetType_ARMv8Virtual));
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --chipset argument '%s' (valid: piix3,ich9,armv8virtual)"),
                                  ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }
#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
            case MODIFYVM_IOMMU:
            {
                if (   !RTStrICmp(ValueUnion.psz, "none")
                    || !RTStrICmp(ValueUnion.psz, "disabled"))
                    CHECK_ERROR(platform, COMSETTER(IommuType)(IommuType_None));
                else if (!RTStrICmp(ValueUnion.psz, "amd"))
                    CHECK_ERROR(platform, COMSETTER(IommuType)(IommuType_AMD));
                else if (!RTStrICmp(ValueUnion.psz, "intel"))
                {
#ifdef VBOX_WITH_IOMMU_INTEL
                    CHECK_ERROR(platform, COMSETTER(IommuType)(IommuType_Intel));
#else
                    errorArgument(ModifyVM::tr("Invalid --iommu argument '%s' (valid: none,amd,automatic)"), ValueUnion.psz);
                    hrc = E_FAIL;
#endif
                }
                else if (!RTStrICmp(ValueUnion.psz, "automatic"))
                {
                    CHECK_ERROR(platform, COMSETTER(IommuType)(IommuType_Automatic));
#ifndef VBOX_WITH_IOMMU_INTEL
                    RTStrmPrintf(g_pStdErr,
                                 ModifyVM::tr("Warning: On Intel hosts, 'automatic' will not enable an IOMMU since the Intel IOMMU device is not supported yet.\n"));
#endif
                }
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --iommu argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }
#endif
#if defined(VBOX_WITH_TPM)
            case MODIFYVM_TPM_TYPE:
            {
                ComPtr<ITrustedPlatformModule> tpm;
                sessionMachine->COMGETTER(TrustedPlatformModule)(tpm.asOutParam());

                if (   !RTStrICmp(ValueUnion.psz, "none")
                    || !RTStrICmp(ValueUnion.psz, "disabled"))
                    CHECK_ERROR(tpm, COMSETTER(Type)(TpmType_None));
                else if (!RTStrICmp(ValueUnion.psz, "1.2"))
                    CHECK_ERROR(tpm, COMSETTER(Type)(TpmType_v1_2));
                else if (!RTStrICmp(ValueUnion.psz, "2.0"))
                    CHECK_ERROR(tpm, COMSETTER(Type)(TpmType_v2_0));
                else if (!RTStrICmp(ValueUnion.psz, "host"))
                    CHECK_ERROR(tpm, COMSETTER(Type)(TpmType_Host));
                else if (!RTStrICmp(ValueUnion.psz, "swtpm"))
                    CHECK_ERROR(tpm, COMSETTER(Type)(TpmType_Swtpm));
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --tpm-type argument '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case MODIFYVM_TPM_LOCATION:
            {
                ComPtr<ITrustedPlatformModule> tpm;
                sessionMachine->COMGETTER(TrustedPlatformModule)(tpm.asOutParam());

                CHECK_ERROR(tpm, COMSETTER(Location)(Bstr(ValueUnion.psz).raw()));
                break;
            }
#endif
#ifdef VBOX_WITH_RECORDING
            case MODIFYVM_RECORDING:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_SCREENS:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_FILENAME:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_VIDEO_WIDTH:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_VIDEO_HEIGHT:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_VIDEO_RES:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_VIDEO_RATE:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_VIDEO_FPS:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_MAXTIME:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_MAXSIZE:
                RT_FALL_THROUGH();
            case MODIFYVM_RECORDING_OPTIONS:
            {
                ComPtr<IRecordingSettings> recordingSettings;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(RecordingSettings)(recordingSettings.asOutParam()));
                SafeIfaceArray <IRecordingScreenSettings> saRecordingScreenScreens;
                CHECK_ERROR_BREAK(recordingSettings, COMGETTER(Screens)(ComSafeArrayAsOutParam(saRecordingScreenScreens)));

                switch (c)
                {
                    case MODIFYVM_RECORDING:
                    {
                        CHECK_ERROR(recordingSettings, COMSETTER(Enabled)(ValueUnion.f));
                        break;
                    }
                    case MODIFYVM_RECORDING_SCREENS:
                    {
                        ULONG cMonitors = 64;
                        CHECK_ERROR(pGraphicsAdapter, COMGETTER(MonitorCount)(&cMonitors));
                        com::SafeArray<BOOL> screens(cMonitors);
                        if (RT_FAILURE(parseScreens(ValueUnion.psz, &screens)))
                        {
                            errorArgument(ModifyVM::tr("Invalid list of screens specified\n"));
                            hrc = E_FAIL;
                            break;
                        }

                        if (cMonitors > saRecordingScreenScreens.size()) /* Paranoia. */
                            cMonitors = (ULONG)saRecordingScreenScreens.size();

                        for (size_t i = 0; i < cMonitors; ++i)
                            CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(Enabled)(screens[i]));
                        break;
                    }
                    case MODIFYVM_RECORDING_FILENAME:
                    {
                        Bstr bstr;
                        /* empty string will fall through, leaving bstr empty */
                        if (*ValueUnion.psz)
                        {
                            char szVCFileAbs[RTPATH_MAX] = "";
                            int vrc = RTPathAbs(ValueUnion.psz, szVCFileAbs, sizeof(szVCFileAbs));
                            if (RT_FAILURE(vrc))
                            {
                                errorArgument(ModifyVM::tr("Cannot convert filename \"%s\" to absolute path\n"), ValueUnion.psz);
                                hrc = E_FAIL;
                                break;
                            }
                            bstr = szVCFileAbs;
                        }

                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(Filename)(bstr.raw()));
                        break;
                    }
                    case MODIFYVM_RECORDING_VIDEO_WIDTH:
                    {
                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(VideoWidth)(ValueUnion.u32));
                        break;
                    }
                    case MODIFYVM_RECORDING_VIDEO_HEIGHT:
                    {
                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(VideoHeight)(ValueUnion.u32));
                        break;
                    }
                    case MODIFYVM_RECORDING_VIDEO_RES:
                    {
                        uint32_t uWidth = 0;
                        char *pszNext;
                        int vrc = RTStrToUInt32Ex(ValueUnion.psz, &pszNext, 0, &uWidth);
                        if (RT_FAILURE(vrc) || vrc != VWRN_TRAILING_CHARS || !pszNext || *pszNext != 'x')
                        {
                            errorArgument(ModifyVM::tr("Error parsing video resolution '%s' (expected <width>x<height>)"),
                                          ValueUnion.psz);
                            hrc = E_FAIL;
                            break;
                        }
                        uint32_t uHeight = 0;
                        vrc = RTStrToUInt32Ex(pszNext+1, NULL, 0, &uHeight);
                        if (vrc != VINF_SUCCESS)
                        {
                            errorArgument(ModifyVM::tr("Error parsing video resolution '%s' (expected <width>x<height>)"),
                                          ValueUnion.psz);
                            hrc = E_FAIL;
                            break;
                        }

                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                        {
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(VideoWidth)(uWidth));
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(VideoHeight)(uHeight));
                        }
                        break;
                    }
                    case MODIFYVM_RECORDING_VIDEO_RATE:
                    {
                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(VideoRate)(ValueUnion.u32));
                        break;
                    }
                    case MODIFYVM_RECORDING_VIDEO_FPS:
                    {
                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(VideoFPS)(ValueUnion.u32));
                        break;
                    }
                    case MODIFYVM_RECORDING_MAXTIME:
                    {
                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(MaxTime)(ValueUnion.u32));
                        break;
                    }
                    case MODIFYVM_RECORDING_MAXSIZE:
                    {
                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(MaxFileSize)(ValueUnion.u32));
                        break;
                    }
                    case MODIFYVM_RECORDING_OPTIONS:
                    {
                        Bstr bstr(ValueUnion.psz);
                        for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                            CHECK_ERROR(saRecordingScreenScreens[i], COMSETTER(Options)(bstr.raw()));
                        break;
                    }
                }

                break;
            }
#endif
            case MODIFYVM_AUTOSTART_ENABLED:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(AutostartEnabled)(ValueUnion.f));
                break;
            }

            case MODIFYVM_AUTOSTART_DELAY:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(AutostartDelay)(ValueUnion.u32));
                break;
            }

            case MODIFYVM_AUTOSTOP_TYPE:
            {
                AutostopType_T enmAutostopType = AutostopType_Disabled;

                if (!RTStrICmp(ValueUnion.psz, "disabled"))
                    enmAutostopType = AutostopType_Disabled;
                else if (!RTStrICmp(ValueUnion.psz, "savestate"))
                    enmAutostopType = AutostopType_SaveState;
                else if (!RTStrICmp(ValueUnion.psz, "poweroff"))
                    enmAutostopType = AutostopType_PowerOff;
                else if (!RTStrICmp(ValueUnion.psz, "acpishutdown"))
                    enmAutostopType = AutostopType_AcpiShutdown;
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --autostop-type argument '%s' (valid: disabled, savestate, poweroff, acpishutdown)"),
                                  ValueUnion.psz);
                    hrc = E_FAIL;
                }

                if (SUCCEEDED(hrc))
                    CHECK_ERROR(sessionMachine, COMSETTER(AutostopType)(enmAutostopType));
                break;
            }
#ifdef VBOX_WITH_PCI_PASSTHROUGH
            case MODIFYVM_ATTACH_PCI:
            {
                const char* pAt = strchr(ValueUnion.psz, '@');
                int32_t iHostAddr, iGuestAddr;

                iHostAddr = parsePci(ValueUnion.psz);
                iGuestAddr = pAt != NULL ? parsePci(pAt + 1) : iHostAddr;

                if (iHostAddr == -1 || iGuestAddr == -1)
                {
                    errorArgument(ModifyVM::tr("Invalid --pciattach argument '%s' (valid: 'HB:HD.HF@GB:GD.GF' or just 'HB:HD.HF')"),
                                  ValueUnion.psz);
                    hrc = E_FAIL;
                }
                else
                {
                    CHECK_ERROR(sessionMachine, AttachHostPCIDevice(iHostAddr, iGuestAddr, TRUE));
                }

                break;
            }
            case MODIFYVM_DETACH_PCI:
            {
                int32_t iHostAddr;

                iHostAddr = parsePci(ValueUnion.psz);
                if (iHostAddr == -1)
                {
                    errorArgument(ModifyVM::tr("Invalid --pcidetach argument '%s' (valid: 'HB:HD.HF')"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                else
                {
                    CHECK_ERROR(sessionMachine, DetachHostPCIDevice(iHostAddr));
                }

                break;
            }
#endif

#ifdef VBOX_WITH_USB_CARDREADER
            case MODIFYVM_USBCARDREADER:
            {
                CHECK_ERROR(sessionMachine, COMSETTER(EmulatedUSBCardReaderEnabled)(ValueUnion.f));
                break;
            }
#endif /* VBOX_WITH_USB_CARDREADER */

            case MODIFYVM_DEFAULTFRONTEND:
            {
                Bstr bstr(ValueUnion.psz);
                if (bstr == "default")
                    bstr = Bstr::Empty;
                CHECK_ERROR(sessionMachine, COMSETTER(DefaultFrontend)(bstr.raw()));
                break;
            }

            case MODIFYVM_VMPROC_PRIORITY:
            {
                VMProcPriority_T enmPriority = nameToVMProcPriority(ValueUnion.psz);
                if (enmPriority == VMProcPriority_Invalid)
                {
                    errorArgument(ModifyVM::tr("Invalid --vm-process-priority '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                else
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(VMProcessPriority)(enmPriority));
                }
                break;
            }

            case MODIFYVM_VMEXEC_ENGINE:
            {
                VMExecutionEngine_T enmExecEngine = nameToVMExecEngine(ValueUnion.psz);
                if (enmExecEngine == VMExecutionEngine_NotSet)
                {
                    errorArgument(ModifyVM::tr("Invalid --vm-execution-engine '%s'"), ValueUnion.psz);
                    hrc = E_FAIL;
                }
                else
                {
                    CHECK_ERROR(sessionMachine, COMSETTER(VMExecutionEngine)(enmExecEngine));
                }
                break;
            }

            case MODIFYVM_TESTING_ENABLED:
                hrc = setExtraData(sessionMachine, "VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled", ValueUnion.f ? "1" : "");
                break;

            case MODIFYVM_TESTING_MMIO:
                hrc = setExtraData(sessionMachine, "VBoxInternal/Devices/VMMDev/0/Config/TestingMMIO", ValueUnion.f ? "1" : "");
                break;

            case MODIFYVM_TESTING_CFG_DWORD:
                if (GetOptState.uIndex <= 9)
                {
                    char szVar[128];
                    RTStrPrintf(szVar, sizeof(szVar), "VBoxInternal/Devices/VMMDev/0/Config/TestingCfgDword%u",
                                GetOptState.uIndex);
                    char szValue[32];
                    RTStrPrintf(szValue, sizeof(szValue), "%u", ValueUnion.u32);
                    hrc = setExtraData(sessionMachine, szVar, szValue);
                }
                else
                    hrc = errorArgumentHr(ModifyVM::tr("--testing-cfg-dword index %u is out of range: 0 thru 9"),
                                         GetOptState.uIndex);
                break;

            case MODIFYVM_GUEST_DEBUG_PROVIDER:
            {
                ComPtr<IGuestDebugControl> gstDbgCtrl;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(GuestDebugControl)(gstDbgCtrl.asOutParam()));

                GuestDebugProvider_T enmDebugProvider = GuestDebugProvider_None;

                if (!RTStrICmp(ValueUnion.psz, "none"))
                    enmDebugProvider = GuestDebugProvider_None;
                else if (!RTStrICmp(ValueUnion.psz, "native"))
                    enmDebugProvider = GuestDebugProvider_Native;
                else if (!RTStrICmp(ValueUnion.psz, "gdb"))
                    enmDebugProvider = GuestDebugProvider_GDB;
                else if (!RTStrICmp(ValueUnion.psz, "kd"))
                    enmDebugProvider = GuestDebugProvider_KD;
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --guest-debug-provider '%s' (valid: none, native, gdb, kd)"),
                                  ValueUnion.psz);
                    hrc = E_FAIL;
                }

                if (SUCCEEDED(hrc))
                    CHECK_ERROR(gstDbgCtrl, COMSETTER(DebugProvider)(enmDebugProvider));
                break;
            }

            case MODIFYVM_GUEST_DEBUG_IO_PROVIDER:
            {
                ComPtr<IGuestDebugControl> gstDbgCtrl;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(GuestDebugControl)(gstDbgCtrl.asOutParam()));

                GuestDebugIoProvider_T enmDebugIoProvider = GuestDebugIoProvider_None;

                if (!RTStrICmp(ValueUnion.psz, "none"))
                    enmDebugIoProvider = GuestDebugIoProvider_None;
                else if (!RTStrICmp(ValueUnion.psz, "tcp"))
                    enmDebugIoProvider = GuestDebugIoProvider_TCP;
                else if (!RTStrICmp(ValueUnion.psz, "udp"))
                    enmDebugIoProvider = GuestDebugIoProvider_UDP;
                else if (!RTStrICmp(ValueUnion.psz, "ipc"))
                    enmDebugIoProvider = GuestDebugIoProvider_IPC;
                else
                {
                    errorArgument(ModifyVM::tr("Invalid --guest-debug-io-provider '%s' (valid: none, tcp, udp, ipc)"),
                                  ValueUnion.psz);
                    hrc = E_FAIL;
                }

                if (SUCCEEDED(hrc))
                    CHECK_ERROR(gstDbgCtrl, COMSETTER(DebugIoProvider)(enmDebugIoProvider));
                break;
            }

            case MODIFYVM_GUEST_DEBUG_ADDRESS:
            {
                ComPtr<IGuestDebugControl> gstDbgCtrl;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(GuestDebugControl)(gstDbgCtrl.asOutParam()));

                Bstr bstr(ValueUnion.psz);
                CHECK_ERROR(gstDbgCtrl, COMSETTER(DebugAddress)(bstr.raw()));
                break;
            }

            case MODIFYVM_GUEST_DEBUG_PORT:
            {
                ComPtr<IGuestDebugControl> gstDbgCtrl;
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(GuestDebugControl)(gstDbgCtrl.asOutParam()));
                CHECK_ERROR(gstDbgCtrl, COMSETTER(DebugPort)(ValueUnion.u32));
                break;
            }

            default:
            {
                hrc = handleModifyVM_x86(&GetOptState, c, &ValueUnion, sessionMachine, platformX86);
                if (FAILED(hrc))
                    errorGetOpt(c, &ValueUnion);
                break;
            }
        }
    }

    /* commit changes */
    if (SUCCEEDED(hrc))
        CHECK_ERROR(sessionMachine, SaveSettings());

    /* it's important to always close sessions */
    a->session->UnlockMachine();

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
