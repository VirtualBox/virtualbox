/* $Id: ApplianceImpl.cpp 108607 2025-03-03 12:09:00Z brent.paulson@oracle.com $ */
/** @file
 * IAppliance and IVirtualSystem COM class implementations.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_APPLIANCE
#include <iprt/path.h>
#include <iprt/cpp/path.h>
#include <iprt/cpp/utils.h>
#include <VBox/com/array.h>
#include <map>

#include "ApplianceImpl.h"
#include "VFSExplorerImpl.h"
#include "VirtualBoxImpl.h"
#include "GuestOSTypeImpl.h"
#include "Global.h"
#include "ProgressImpl.h"
#include "MachineImpl.h"
#include "SystemPropertiesImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include "CertificateImpl.h"

#include "ApplianceImplPrivate.h"

using namespace std;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char * const   g_pszISOURI             = "http://www.ecma-international.org/publications/standards/Ecma-119.htm";
static const char * const   g_pszVMDKStreamURI      = "http://www.vmware.com/interfaces/specifications/vmdk.html#streamOptimized";
static const char * const   g_pszVMDKSparseURI      = "http://www.vmware.com/specifications/vmdk.html#sparse";
static const char * const   g_pszVMDKCompressedURI  = "http://www.vmware.com/specifications/vmdk.html#compressed";
static const char * const   g_pszVMDKCompressedURI2 = "http://www.vmware.com/interfaces/specifications/vmdk.html#compressed";
static const char * const   g_pszrVHDURI            = "http://go.microsoft.com/fwlink/?LinkId=137171";
static char                 g_szIsoBackend[128];
static char                 g_szVmdkBackend[128];
static char                 g_szVhdBackend[128];
/** Set after the g_szXxxxBackend variables has been initialized. */
static bool volatile        g_fInitializedBackendNames = false;

static struct
{
    const char *pszUri, *pszBackend;
} const g_aUriToBackend[] =
{
    { g_pszISOURI,              g_szIsoBackend },
    { g_pszVMDKStreamURI,       g_szVmdkBackend },
    { g_pszVMDKSparseURI,       g_szVmdkBackend },
    { g_pszVMDKCompressedURI,   g_szVmdkBackend },
    { g_pszVMDKCompressedURI2,  g_szVmdkBackend },
    { g_pszrVHDURI,             g_szVhdBackend },
};

static std::map<Utf8Str, Utf8Str> supportedStandardsURI;

static struct
{
    ovf::CIMOSType_T    cim;
    VBOXOSTYPE          osType;
} const g_aOsTypes[] =
{
    { ovf::CIMOSType_CIMOS_Unknown,                              VBOXOSTYPE_Unknown },
    { ovf::CIMOSType_CIMOS_OS2,                                  VBOXOSTYPE_OS2 },
    { ovf::CIMOSType_CIMOS_OS2,                                  VBOXOSTYPE_OS2Warp3 },
    { ovf::CIMOSType_CIMOS_OS2,                                  VBOXOSTYPE_OS2Warp4 },
    { ovf::CIMOSType_CIMOS_OS2,                                  VBOXOSTYPE_OS2Warp45 },
    { ovf::CIMOSType_CIMOS_OS2,                                  VBOXOSTYPE_OS21x },
    { ovf::CIMOSType_CIMOS_OS2,                                  VBOXOSTYPE_ECS },
    { ovf::CIMOSType_CIMOS_OS2,                                  VBOXOSTYPE_ArcaOS },
    { ovf::CIMOSType_CIMOS_MSDOS,                                VBOXOSTYPE_DOS },
    { ovf::CIMOSType_CIMOS_WIN3x,                                VBOXOSTYPE_Win31 },
    { ovf::CIMOSType_CIMOS_WIN95,                                VBOXOSTYPE_Win95 },
    { ovf::CIMOSType_CIMOS_WIN98,                                VBOXOSTYPE_Win98 },
    { ovf::CIMOSType_CIMOS_WINNT,                                VBOXOSTYPE_WinNT },
    { ovf::CIMOSType_CIMOS_WINNT,                                VBOXOSTYPE_WinNT4 },
    { ovf::CIMOSType_CIMOS_WINNT,                                VBOXOSTYPE_WinNT3x },
    { ovf::CIMOSType_CIMOS_NetWare,                              VBOXOSTYPE_Netware },
    { ovf::CIMOSType_CIMOS_NovellOES,                            VBOXOSTYPE_Netware },
    { ovf::CIMOSType_CIMOS_Solaris,                              VBOXOSTYPE_Solaris },
    { ovf::CIMOSType_CIMOS_Solaris_64,                           VBOXOSTYPE_Solaris_x64 },
    { ovf::CIMOSType_CIMOS_Solaris,                              VBOXOSTYPE_Solaris10U8_or_later },
    { ovf::CIMOSType_CIMOS_Solaris_64,                           VBOXOSTYPE_Solaris10U8_or_later_x64 },
    { ovf::CIMOSType_CIMOS_SunOS,                                VBOXOSTYPE_Solaris },
    { ovf::CIMOSType_CIMOS_FreeBSD,                              VBOXOSTYPE_FreeBSD },
    { ovf::CIMOSType_CIMOS_NetBSD,                               VBOXOSTYPE_NetBSD },
    { ovf::CIMOSType_CIMOS_QNX,                                  VBOXOSTYPE_QNX },
    { ovf::CIMOSType_CIMOS_Windows2000,                          VBOXOSTYPE_Win2k },
    { ovf::CIMOSType_CIMOS_WindowsMe,                            VBOXOSTYPE_WinMe },
    { ovf::CIMOSType_CIMOS_OpenBSD,                              VBOXOSTYPE_OpenBSD },
    { ovf::CIMOSType_CIMOS_WindowsXP,                            VBOXOSTYPE_WinXP },
    { ovf::CIMOSType_CIMOS_WindowsXPEmbedded,                    VBOXOSTYPE_WinXP },
    { ovf::CIMOSType_CIMOS_WindowsEmbeddedforPointofService,     VBOXOSTYPE_WinXP },
    { ovf::CIMOSType_CIMOS_MicrosoftWindowsServer2003,           VBOXOSTYPE_Win2k3 },
    { ovf::CIMOSType_CIMOS_MicrosoftWindowsServer2003_64,        VBOXOSTYPE_Win2k3_x64 },
    { ovf::CIMOSType_CIMOS_WindowsXP_64,                         VBOXOSTYPE_WinXP_x64 },
    { ovf::CIMOSType_CIMOS_WindowsVista,                         VBOXOSTYPE_WinVista },
    { ovf::CIMOSType_CIMOS_WindowsVista_64,                      VBOXOSTYPE_WinVista_x64 },
    { ovf::CIMOSType_CIMOS_MicrosoftWindowsServer2008,           VBOXOSTYPE_Win2k8 },
    { ovf::CIMOSType_CIMOS_MicrosoftWindowsServer2008_64,        VBOXOSTYPE_Win2k8_x64 },
    { ovf::CIMOSType_CIMOS_FreeBSD_64,                           VBOXOSTYPE_FreeBSD_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS_x64 }, // there is no CIM 64-bit type for this
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS106 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS106_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS107_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS108_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS109_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS1010_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS1011_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS1012_x64 },
    { ovf::CIMOSType_CIMOS_MACOS,                                VBOXOSTYPE_MacOS1013_x64 },

    // Linuxes
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux,                VBOXOSTYPE_RedHat },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat_x64 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux,                VBOXOSTYPE_RedHat3 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat3_x64 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux,                VBOXOSTYPE_RedHat4 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat4_x64 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux,                VBOXOSTYPE_RedHat5 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat5_x64 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux,                VBOXOSTYPE_RedHat6 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat6_x64 },
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat7_x64 }, // 64-bit only
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat8_x64 }, // 64-bit only
    { ovf::CIMOSType_CIMOS_RedHatEnterpriseLinux_64,             VBOXOSTYPE_RedHat9_x64 }, // 64-bit only
    { ovf::CIMOSType_CIMOS_SUSE,                                 VBOXOSTYPE_OpenSUSE },
    { ovf::CIMOSType_CIMOS_SLES,                                 VBOXOSTYPE_SUSE_LE },
    { ovf::CIMOSType_CIMOS_NovellLinuxDesktop,                   VBOXOSTYPE_OpenSUSE },
    { ovf::CIMOSType_CIMOS_SUSE_64,                              VBOXOSTYPE_OpenSUSE_x64 },
    { ovf::CIMOSType_CIMOS_SLES_64,                              VBOXOSTYPE_SUSE_LE_x64 },
    { ovf::CIMOSType_CIMOS_SUSE_64,                              VBOXOSTYPE_OpenSUSE_Leap_x64 }, // 64-bit only
    { ovf::CIMOSType_CIMOS_SUSE,                                 VBOXOSTYPE_OpenSUSE_Tumbleweed },
    { ovf::CIMOSType_CIMOS_SUSE_64,                              VBOXOSTYPE_OpenSUSE_Tumbleweed_x64 },
    { ovf::CIMOSType_CIMOS_LINUX,                                VBOXOSTYPE_Linux },
    { ovf::CIMOSType_CIMOS_LINUX,                                VBOXOSTYPE_Linux22 },
    { ovf::CIMOSType_CIMOS_SunJavaDesktopSystem,                 VBOXOSTYPE_Linux },
    { ovf::CIMOSType_CIMOS_TurboLinux,                           VBOXOSTYPE_Turbolinux },
    { ovf::CIMOSType_CIMOS_TurboLinux_64,                        VBOXOSTYPE_Turbolinux_x64 },
    { ovf::CIMOSType_CIMOS_Mandriva,                             VBOXOSTYPE_Mandriva },
    { ovf::CIMOSType_CIMOS_Mandriva_64,                          VBOXOSTYPE_Mandriva_x64 },
    { ovf::CIMOSType_CIMOS_Mandriva,                             VBOXOSTYPE_OpenMandriva_Lx },
    { ovf::CIMOSType_CIMOS_Mandriva_64,                          VBOXOSTYPE_OpenMandriva_Lx_x64 },
    { ovf::CIMOSType_CIMOS_Mandriva,                             VBOXOSTYPE_PCLinuxOS },
    { ovf::CIMOSType_CIMOS_Mandriva_64,                          VBOXOSTYPE_PCLinuxOS_x64 },
    { ovf::CIMOSType_CIMOS_Mandriva,                             VBOXOSTYPE_Mageia },
    { ovf::CIMOSType_CIMOS_Mandriva_64,                          VBOXOSTYPE_Mageia_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu10_LTS },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu10_LTS_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu10 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu10_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu11 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu11_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu12_LTS },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu12_LTS_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu12 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu12_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu13 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu13_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu14_LTS },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu14_LTS_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu14 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu14_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu15 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu15_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu16_LTS },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu16_LTS_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu16 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu16_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu17 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu17_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu18_LTS },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu18_LTS_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu18 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu18_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Ubuntu19 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu19_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu20_LTS_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu20_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu21_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu22_LTS_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu22_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Ubuntu23_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Lubuntu },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Lubuntu_x64 },
    { ovf::CIMOSType_CIMOS_Ubuntu,                               VBOXOSTYPE_Xubuntu },
    { ovf::CIMOSType_CIMOS_Ubuntu_64,                            VBOXOSTYPE_Xubuntu_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian31 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian4 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian4_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian5 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian5_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian6 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian6_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian7 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian7_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian8 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian8_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian9 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian9_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian10 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian10_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian11 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian11_x64 },
    { ovf::CIMOSType_CIMOS_Debian,                               VBOXOSTYPE_Debian12 },
    { ovf::CIMOSType_CIMOS_Debian_64,                            VBOXOSTYPE_Debian12_x64 },
    { ovf::CIMOSType_CIMOS_Linux_2_4_x,                          VBOXOSTYPE_Linux24 },
    { ovf::CIMOSType_CIMOS_Linux_2_4_x_64,                       VBOXOSTYPE_Linux24_x64 },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x,                          VBOXOSTYPE_Linux26 },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x_64,                       VBOXOSTYPE_Linux26_x64 },
    { ovf::CIMOSType_CIMOS_Linux_64,                             VBOXOSTYPE_Linux26_x64 },

    // types that we have support for but CIM doesn't
    { ovf::CIMOSType_CIMOS_Linux_2_6_x,                          VBOXOSTYPE_ArchLinux },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x_64,                       VBOXOSTYPE_ArchLinux_x64 },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x,                          VBOXOSTYPE_FedoraCore },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x_64,                       VBOXOSTYPE_FedoraCore_x64 },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x,                          VBOXOSTYPE_Gentoo },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x_64,                       VBOXOSTYPE_Gentoo_x64 },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x,                          VBOXOSTYPE_Xandros },
    { ovf::CIMOSType_CIMOS_Linux_2_6_x_64,                       VBOXOSTYPE_Xandros_x64 },
    { ovf::CIMOSType_CIMOS_Solaris,                              VBOXOSTYPE_OpenSolaris },
    { ovf::CIMOSType_CIMOS_Solaris_64,                           VBOXOSTYPE_OpenSolaris_x64 },

    // types added with CIM 2.25.0 follow:
    { ovf::CIMOSType_CIMOS_WindowsServer2008R2,                  VBOXOSTYPE_Win2k8 },           // duplicate, see above
//     { ovf::CIMOSType_CIMOS_VMwareESXi = 104,                                                 // we can't run ESX in a VM
    { ovf::CIMOSType_CIMOS_Windows7,                             VBOXOSTYPE_Win7 },
    { ovf::CIMOSType_CIMOS_Windows7,                             VBOXOSTYPE_Win7_x64 },         // there is no
                                                                                                // CIM 64-bit type for this
    { ovf::CIMOSType_CIMOS_CentOS,                               VBOXOSTYPE_RedHat },
    { ovf::CIMOSType_CIMOS_CentOS_64,                            VBOXOSTYPE_RedHat_x64 },
    { ovf::CIMOSType_CIMOS_OracleLinux,                          VBOXOSTYPE_Oracle },
    { ovf::CIMOSType_CIMOS_OracleLinux_64,                       VBOXOSTYPE_Oracle_x64 },
    { ovf::CIMOSType_CIMOS_OracleLinux,                          VBOXOSTYPE_Oracle4 },
    { ovf::CIMOSType_CIMOS_OracleLinux_64,                       VBOXOSTYPE_Oracle4_x64 },
    { ovf::CIMOSType_CIMOS_OracleLinux,                          VBOXOSTYPE_Oracle5 },
    { ovf::CIMOSType_CIMOS_OracleLinux_64,                       VBOXOSTYPE_Oracle5_x64 },
    { ovf::CIMOSType_CIMOS_OracleLinux,                          VBOXOSTYPE_Oracle6 },
    { ovf::CIMOSType_CIMOS_OracleLinux_64,                       VBOXOSTYPE_Oracle6_x64 },
    { ovf::CIMOSType_CIMOS_OracleLinux_64,                       VBOXOSTYPE_Oracle7_x64 }, // 64-bit only
    { ovf::CIMOSType_CIMOS_OracleLinux_64,                       VBOXOSTYPE_Oracle8_x64 }, // 64-bit only
    { ovf::CIMOSType_CIMOS_OracleLinux_64,                       VBOXOSTYPE_Oracle9_x64 }, // 64-bit only
    { ovf::CIMOSType_CIMOS_eComStation,                          VBOXOSTYPE_ECS },

    { ovf::CIMOSType_CIMOS_WindowsServer2011,                    VBOXOSTYPE_Win2k8_x64 },       // no 1:1 match on the VBox side
    { ovf::CIMOSType_CIMOS_WindowsServer2012,                    VBOXOSTYPE_Win2k12_x64 },
    { ovf::CIMOSType_CIMOS_Windows8,                             VBOXOSTYPE_Win8 },
    { ovf::CIMOSType_CIMOS_Windows8_64,                          VBOXOSTYPE_Win8_x64 },
    { ovf::CIMOSType_CIMOS_WindowsServer2012R2,                  VBOXOSTYPE_Win2k12_x64 },
    { ovf::CIMOSType_CIMOS_Windows8_1,                           VBOXOSTYPE_Win81 },
    { ovf::CIMOSType_CIMOS_Windows8_1_64,                        VBOXOSTYPE_Win81_x64 },
    { ovf::CIMOSType_CIMOS_WindowsServer2016,                    VBOXOSTYPE_Win2k16_x64 },
    { ovf::CIMOSType_CIMOS_Windows10,                            VBOXOSTYPE_Win10 },
    { ovf::CIMOSType_CIMOS_Windows10_64,                         VBOXOSTYPE_Win10_x64 },
    { ovf::CIMOSType_CIMOS_WindowsServer2016,                    VBOXOSTYPE_Win2k19_x64 },      // no CIM type for this yet
    { ovf::CIMOSType_CIMOS_WindowsServer2016,                    VBOXOSTYPE_Win2k22_x64 },      // no CIM type for this yet
    { ovf::CIMOSType_CIMOS_WindowsServer2016,                    VBOXOSTYPE_Win2k25_x64 },      // no CIM type for this yet

    // there are no CIM types for these, so these turn to "other" on export:
    //      VBOXOSTYPE_OpenBSD
    //      VBOXOSTYPE_OpenBSD_x64
    //      VBOXOSTYPE_NetBSD
    //      VBOXOSTYPE_NetBSD_x64

};

/* Pattern structure for matching the OS type description field */
struct osTypePattern
{
    const char *pcszPattern;
    VBOXOSTYPE osType;
};

/* These are the 32-Bit ones. They are sorted by priority. */
static const osTypePattern g_aOsTypesPattern[] =
{
    {"Windows NT",    VBOXOSTYPE_WinNT4},
    {"Windows XP",    VBOXOSTYPE_WinXP},
    {"Windows 2000",  VBOXOSTYPE_Win2k},
    {"Windows 2003",  VBOXOSTYPE_Win2k3},
    {"Windows Vista", VBOXOSTYPE_WinVista},
    {"Windows 2008",  VBOXOSTYPE_Win2k8},
    {"Windows 7",     VBOXOSTYPE_Win7},
    {"Windows 8.1",   VBOXOSTYPE_Win81},
    {"Windows 8",     VBOXOSTYPE_Win8},
    {"Windows 10",    VBOXOSTYPE_Win10},
    {"SUSE",          VBOXOSTYPE_OpenSUSE},
    {"Novell",        VBOXOSTYPE_OpenSUSE},
    {"Red Hat",       VBOXOSTYPE_RedHat},
    {"Mandriva",      VBOXOSTYPE_Mandriva},
    {"Ubuntu",        VBOXOSTYPE_Ubuntu},
    {"Debian",        VBOXOSTYPE_Debian},
    {"QNX",           VBOXOSTYPE_QNX},
    {"Linux 2.4",     VBOXOSTYPE_Linux24},
    {"Linux 2.6",     VBOXOSTYPE_Linux26},
    {"Linux",         VBOXOSTYPE_Linux},
    {"OpenSolaris",   VBOXOSTYPE_OpenSolaris},
    {"Solaris",       VBOXOSTYPE_OpenSolaris},
    {"FreeBSD",       VBOXOSTYPE_FreeBSD},
    {"NetBSD",        VBOXOSTYPE_NetBSD},
    {"Windows 95",    VBOXOSTYPE_Win95},
    {"Windows 98",    VBOXOSTYPE_Win98},
    {"Windows Me",    VBOXOSTYPE_WinMe},
    {"Windows 3.",    VBOXOSTYPE_Win31},
    {"DOS",           VBOXOSTYPE_DOS},
    {"OS2",           VBOXOSTYPE_OS2}
};

/* These are the 64-Bit ones. They are sorted by priority. */
static const osTypePattern g_aOsTypesPattern64[] =
{
    {"Windows XP",    VBOXOSTYPE_WinXP_x64},
    {"Windows 2003",  VBOXOSTYPE_Win2k3_x64},
    {"Windows Vista", VBOXOSTYPE_WinVista_x64},
    {"Windows 2008",  VBOXOSTYPE_Win2k8_x64},
    {"Windows 7",     VBOXOSTYPE_Win7_x64},
    {"Windows 8.1",   VBOXOSTYPE_Win81_x64},
    {"Windows 8",     VBOXOSTYPE_Win8_x64},
    {"Windows 2012",  VBOXOSTYPE_Win2k12_x64},
    {"Windows 10",    VBOXOSTYPE_Win10_x64},
    {"Windows 2016",  VBOXOSTYPE_Win2k16_x64},
    {"Windows 2019",  VBOXOSTYPE_Win2k19_x64},
    {"SUSE",          VBOXOSTYPE_OpenSUSE_x64},
    {"Novell",        VBOXOSTYPE_OpenSUSE_x64},
    {"Red Hat",       VBOXOSTYPE_RedHat_x64},
    {"Mandriva",      VBOXOSTYPE_Mandriva_x64},
    {"Ubuntu",        VBOXOSTYPE_Ubuntu_x64},
    {"Debian",        VBOXOSTYPE_Debian_x64},
    {"Linux 2.4",     VBOXOSTYPE_Linux24_x64},
    {"Linux 2.6",     VBOXOSTYPE_Linux26_x64},
    {"Linux",         VBOXOSTYPE_Linux26_x64},
    {"OpenSolaris",   VBOXOSTYPE_OpenSolaris_x64},
    {"Solaris",       VBOXOSTYPE_OpenSolaris_x64},
    {"FreeBSD",       VBOXOSTYPE_FreeBSD_x64},
};

/**
 * Private helper func that suggests a VirtualBox guest OS type
 * for the given OVF operating system type.
 */
void convertCIMOSType2VBoxOSType(Utf8Str &strType, ovf::CIMOSType_T c, const Utf8Str &cStr)
{
    /* First check if the type is other/other_64 */
    if (c == ovf::CIMOSType_CIMOS_Other)
    {
        for (size_t i = 0; i < RT_ELEMENTS(g_aOsTypesPattern); ++i)
            if (cStr.contains(g_aOsTypesPattern[i].pcszPattern, Utf8Str::CaseInsensitive))
            {
                strType = Global::OSTypeId(g_aOsTypesPattern[i].osType);
                return;
            }
    }
    else if (c == ovf::CIMOSType_CIMOS_Other_64)
    {
        for (size_t i = 0; i < RT_ELEMENTS(g_aOsTypesPattern64); ++i)
            if (cStr.contains(g_aOsTypesPattern64[i].pcszPattern, Utf8Str::CaseInsensitive))
            {
                strType = Global::OSTypeId(g_aOsTypesPattern64[i].osType);
                return;
            }
    }

    for (size_t i = 0; i < RT_ELEMENTS(g_aOsTypes); ++i)
    {
        if (c == g_aOsTypes[i].cim)
        {
            strType = Global::OSTypeId(g_aOsTypes[i].osType);
            return;
        }
    }

    if (c == ovf::CIMOSType_CIMOS_Other_64)
        strType = Global::OSTypeId(VBOXOSTYPE_Unknown_x64);
    else
        strType = Global::OSTypeId(VBOXOSTYPE_Unknown);
}

/**
 * Private helper func that suggests a VirtualBox guest OS type
 * for the given OVF operating system type.
 * @returns CIM OS type.
 * @param pcszVBox  Our guest OS type identifier string.
 * @param fLongMode Whether long mode is enabled and a 64-bit CIM type is
 *                  preferred even if the VBox guest type isn't 64-bit.
 */
ovf::CIMOSType_T convertVBoxOSType2CIMOSType(const char *pcszVBox, BOOL fLongMode)
{
    for (size_t i = 0; i < RT_ELEMENTS(g_aOsTypes); ++i)
    {
        if (!RTStrICmp(pcszVBox, Global::OSTypeId(g_aOsTypes[i].osType)))
        {
            if (fLongMode && !(g_aOsTypes[i].osType & VBOXOSTYPE_x64))
            {
                VBOXOSTYPE enmDesiredOsType = (VBOXOSTYPE)((int)g_aOsTypes[i].osType | (int)VBOXOSTYPE_x64);
                for (size_t j = i+1; j < RT_ELEMENTS(g_aOsTypes); j++)
                    if (g_aOsTypes[j].osType == enmDesiredOsType)
                        return g_aOsTypes[j].cim;
                if (i > 0)
                {
                    for (size_t j = i-1; j > 0; j--)
                        if (g_aOsTypes[j].osType == enmDesiredOsType)
                            return g_aOsTypes[j].cim;
                }
                /* Not all OSes have 64-bit versions, so just return the 32-bit variant. */
            }
            return g_aOsTypes[i].cim;
        }
    }

    return fLongMode ? ovf::CIMOSType_CIMOS_Other_64 : ovf::CIMOSType_CIMOS_Other;
}

Utf8Str convertNetworkAttachmentTypeToString(NetworkAttachmentType_T type)
{
    Utf8Str strType;
    switch (type)
    {
        case NetworkAttachmentType_NAT: strType = "NAT"; break;
        case NetworkAttachmentType_Bridged: strType = "Bridged"; break;
        case NetworkAttachmentType_Internal: strType = "Internal"; break;
        case NetworkAttachmentType_HostOnly: strType = "HostOnly"; break;
        case NetworkAttachmentType_HostOnlyNetwork: strType = "HostOnlyNetwork"; break;
        case NetworkAttachmentType_Generic: strType = "Generic"; break;
        case NetworkAttachmentType_NATNetwork: strType = "NATNetwork"; break;
        case NetworkAttachmentType_Null: strType = "Null"; break;
        case NetworkAttachmentType_Cloud: strType = "Cloud"; break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case NetworkAttachmentType_32BitHack: AssertFailedBreak(); /* (compiler warnings) */
#endif
    }
    return strType;
}


////////////////////////////////////////////////////////////////////////////////
//
// Appliance constructor / destructor
//
// ////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(VirtualSystemDescription)

HRESULT VirtualSystemDescription::FinalConstruct()
{
    return BaseFinalConstruct();
}

void VirtualSystemDescription::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

Appliance::Appliance()
    : mVirtualBox(NULL)
{
}

Appliance::~Appliance()
{
}


HRESULT Appliance::FinalConstruct()
{
    return BaseFinalConstruct();
}

void Appliance::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}


////////////////////////////////////////////////////////////////////////////////
//
// Internal helpers
//
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// IVirtualBox public methods
//
////////////////////////////////////////////////////////////////////////////////

// This code is here so we won't have to include the appliance headers in the
// IVirtualBox implementation.

/**
 * Implementation for IVirtualBox::createAppliance.
 *
 * @param aAppliance IAppliance object created if S_OK is returned.
 * @return S_OK or error.
 */
HRESULT VirtualBox::createAppliance(ComPtr<IAppliance> &aAppliance)
{
    ComObjPtr<Appliance> appliance;
    HRESULT hrc = appliance.createObject();
    if (SUCCEEDED(hrc))
    {
        hrc = appliance->init(this);
        if (SUCCEEDED(hrc))
            hrc = appliance.queryInterfaceTo(aAppliance.asOutParam());
    }
    return hrc;
}

/**
 * Appliance COM initializer.
 * @param   aVirtualBox     The VirtualBox object.
 */
HRESULT Appliance::init(VirtualBox *aVirtualBox)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Weak reference to a VirtualBox object */
    unconst(mVirtualBox) = aVirtualBox;

    // initialize data
    m = new Data;
    m->m_pSecretKeyStore = new SecretKeyStore(false /* fRequireNonPageable*/);
    AssertReturn(m->m_pSecretKeyStore, E_FAIL);

    HRESULT hrc = i_initBackendNames();

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return hrc;
}

/**
 * Appliance COM uninitializer.
 */
void Appliance::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    if (m->m_pSecretKeyStore)
        delete m->m_pSecretKeyStore;

    delete m;
    m = NULL;
}

////////////////////////////////////////////////////////////////////////////////
//
// IAppliance public methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Public method implementation.
 */
HRESULT Appliance::getPath(com::Utf8Str &aPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aPath = m->locInfo.strPath;

    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT Appliance::getDisks(std::vector<com::Utf8Str> &aDisks)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDisks.resize(0);

    if (m->pReader) // OVFReader instantiated?
    {
        aDisks.resize(m->pReader->m_mapDisks.size());

        ovf::DiskImagesMap::const_iterator it;
        size_t i = 0;
        for (it = m->pReader->m_mapDisks.begin();
             it != m->pReader->m_mapDisks.end();
             ++it, ++i)
        {
            // create a string representing this disk
            const ovf::DiskImage &d = it->second;
            char *psz = NULL;
            RTStrAPrintf(&psz,
                         "%s\t"
                         "%RI64\t"
                         "%RI64\t"
                         "%s\t"
                         "%s\t"
                         "%RI64\t"
                         "%RI64\t"
                         "%s",
                         d.strDiskId.c_str(),
                         d.iCapacity,
                         d.iPopulatedSize,
                         d.strFormat.c_str(),
                         d.strHref.c_str(),
                         d.iSize,
                         d.iChunkSize,
                         d.strCompression.c_str());
            Utf8Str utf(psz);
            aDisks[i] = utf;
            RTStrFree(psz);
        }
    }

    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT Appliance::getCertificate(ComPtr<ICertificate> &aCertificateInfo)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Can be NULL at this point, queryInterfaceto handles that. */
    m->ptrCertificateInfo.queryInterfaceTo(aCertificateInfo.asOutParam());
    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT Appliance::getVirtualSystemDescriptions(std::vector<ComPtr<IVirtualSystemDescription> > &aVirtualSystemDescriptions)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aVirtualSystemDescriptions.resize(m->virtualSystemDescriptions.size());
    std::list< ComObjPtr<VirtualSystemDescription> > vsds(m->virtualSystemDescriptions);
    size_t i = 0;
    for (std::list< ComObjPtr<VirtualSystemDescription> >::iterator it = vsds.begin(); it != vsds.end(); ++it, ++i)
    {
        (*it).queryInterfaceTo(aVirtualSystemDescriptions[i].asOutParam());
    }
    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT Appliance::getMachines(std::vector<com::Utf8Str> &aMachines)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aMachines.resize(m->llGuidsMachinesCreated.size());
    size_t i = 0;
    for (std::list<Guid>::const_iterator it = m->llGuidsMachinesCreated.begin();
         it != m->llGuidsMachinesCreated.end();
         ++it, ++i)
    {
        const Guid &uuid = *it;
        aMachines[i] = uuid.toUtf16();
    }
    return S_OK;
}

HRESULT Appliance::createVFSExplorer(const com::Utf8Str &aURI, ComPtr<IVFSExplorer> &aExplorer)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<VFSExplorer> explorer;
    HRESULT hrc;
    try
    {
        Utf8Str uri(aURI);
        /* Check which kind of export the user has requested */
        LocationInfo li;
        i_parseURI(aURI, li);
        /* Create the explorer object */
        explorer.createObject();
        hrc = explorer->init(li.storageType, li.strPath, li.strHostname, li.strUsername, li.strPassword, mVirtualBox);
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

    if (SUCCEEDED(hrc))
        /* Return explorer to the caller */
        explorer.queryInterfaceTo(aExplorer.asOutParam());

    return hrc;
}


/**
 * Public method implementation.
 * Add the "aRequested" numbers of new empty objects of VSD into the list
 * "virtualSystemDescriptions".
 * The parameter "aCreated" keeps the actual number of the added objects.
 * In case of exception all added objects are removed from the list.
 */
HRESULT Appliance::createVirtualSystemDescriptions(ULONG aRequested, ULONG *aCreated)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;
    uint32_t lQuantity = aRequested;
    uint32_t i=0;

    if (lQuantity < 1)
        return setError(E_FAIL, tr("The number of VirtualSystemDescription objects must be at least 1 or more."));
    try
    {
        for (; i<lQuantity; ++i)
        {
            ComObjPtr<VirtualSystemDescription> opVSD;
            hrc = opVSD.createObject();
            if (SUCCEEDED(hrc))
            {
                hrc = opVSD->init();
                if (SUCCEEDED(hrc))
                    m->virtualSystemDescriptions.push_back(opVSD);
                else
                    break;
            }
            else
                break;
        }

        if (i<lQuantity)
            LogRel(("Number of created VirtualSystemDescription objects is less than requested"
                    "(Requested %d, Created %d)",lQuantity, i));

        *aCreated = i;
    }
    catch (HRESULT hrcXcpt)
    {
        for (; i>0; --i)
        {
            if (!m->virtualSystemDescriptions.empty())
                m->virtualSystemDescriptions.pop_back();
            else
                break;
        }
        hrc = hrcXcpt;
    }

    return hrc;
}

/**
 * Public method implementation.
 */
HRESULT Appliance::getWarnings(std::vector<com::Utf8Str> &aWarnings)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aWarnings.resize(m->llWarnings.size());

    list<Utf8Str>::const_iterator it;
    size_t i = 0;
    for (it = m->llWarnings.begin();
         it != m->llWarnings.end();
         ++it, ++i)
    {
        aWarnings[i] = *it;
    }

    return S_OK;
}

HRESULT Appliance::getPasswordIds(std::vector<com::Utf8Str> &aIdentifiers)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aIdentifiers = m->m_vecPasswordIdentifiers;
    return S_OK;
}

HRESULT Appliance::getMediumIdsForPasswordId(const com::Utf8Str &aPasswordId, std::vector<com::Guid> &aIdentifiers)
{
    HRESULT hrc = S_OK;
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    std::map<com::Utf8Str, GUIDVEC>::const_iterator it = m->m_mapPwIdToMediumIds.find(aPasswordId);
    if (it != m->m_mapPwIdToMediumIds.end())
        aIdentifiers = it->second;
    else
        hrc = setError(E_FAIL, tr("The given password identifier is not associated with any medium"));

    return hrc;
}

HRESULT Appliance::addPasswords(const std::vector<com::Utf8Str> &aIdentifiers,
                                const std::vector<com::Utf8Str> &aPasswords)
{
    HRESULT hrc = S_OK;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Check that the IDs do not exist already before changing anything. */
    for (unsigned i = 0; i < aIdentifiers.size(); i++)
    {
        SecretKey *pKey = NULL;
        int vrc = m->m_pSecretKeyStore->retainSecretKey(aIdentifiers[i], &pKey);
        if (vrc != VERR_NOT_FOUND)
        {
            AssertPtr(pKey);
            if (pKey)
                pKey->release();
            return setError(VBOX_E_OBJECT_IN_USE, tr("A password with the given ID already exists"));
        }
    }

    for (unsigned i = 0; i < aIdentifiers.size() && SUCCEEDED(hrc); i++)
    {
        size_t cbKey = aPasswords[i].length() + 1; /* Include terminator */
        const uint8_t *pbKey = (const uint8_t *)aPasswords[i].c_str();

        int vrc = m->m_pSecretKeyStore->addSecretKey(aIdentifiers[i], pbKey, cbKey);
        if (RT_SUCCESS(vrc))
            m->m_cPwProvided++;
        else if (vrc == VERR_NO_MEMORY)
            hrc = setError(E_OUTOFMEMORY, tr("Failed to allocate enough secure memory for the key"));
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("Unknown error happened while adding a password (%Rrc)"), vrc);
    }

    return hrc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Appliance private methods
//
////////////////////////////////////////////////////////////////////////////////

HRESULT Appliance::i_initBackendNames()
{
    HRESULT hrc = S_OK;
    if (!g_fInitializedBackendNames)
    {
        /*
         * Use the system properties to translate file extensions into
         * storage backend names.
         */
        static struct
        {
            const char *pszExt;         /**< extension */
            char       *pszBackendName;
            size_t      cbBackendName;
        } const s_aFormats[] =
        {
            { "iso",   g_szIsoBackend,  sizeof(g_szIsoBackend)  },
            { "vmdk",  g_szVmdkBackend, sizeof(g_szVmdkBackend) },
            { "vhd",   g_szVhdBackend,  sizeof(g_szVhdBackend)  },
        };
        SystemProperties *pSysProps = mVirtualBox->i_getSystemProperties();
        for (unsigned i = 0; i < RT_ELEMENTS(s_aFormats); i++)
        {
            ComObjPtr<MediumFormat> trgFormat = pSysProps->i_mediumFormatFromExtension(s_aFormats[i].pszExt);
            if (trgFormat.isNotNull())
            {
                const char *pszName = trgFormat->i_getName().c_str();
                int vrc = RTStrCopy(s_aFormats[i].pszBackendName, s_aFormats[i].cbBackendName, pszName);
                AssertRCStmt(vrc, hrc = setError(E_FAIL, "Unexpected storage backend name copy error %Rrc for %s.", vrc, pszName));
            }
            else
                hrc = setError(E_FAIL, tr("Can't find appropriate medium format for ISO type of a virtual disk."));
        }

        if (SUCCEEDED(hrc))
            g_fInitializedBackendNames = true;
    }

    return hrc;
}

Utf8Str Appliance::i_typeOfVirtualDiskFormatFromURI(Utf8Str uri) const
{
    Assert(g_fInitializedBackendNames);

    unsigned i = RT_ELEMENTS(g_aUriToBackend);
    while (i-- > 0)
        if (RTStrICmp(g_aUriToBackend[i].pszUri, uri.c_str()) == 0)
            return Utf8Str(g_aUriToBackend[i].pszBackend);
    return Utf8Str();
}

#if 0 /* unused */
std::set<Utf8Str> Appliance::i_URIFromTypeOfVirtualDiskFormat(Utf8Str type)
{
    Assert(g_fInitializedBackendNames);

    std::set<Utf8Str> UriSet;
    unsigned i = RT_ELEMENTS(g_aUriToBackend);
    while (i-- > 0)
        if (RTStrICmp(g_aUriToBackend[i].pszBackend, type.c_str()) == 0)
            UriSet.insert(g_aUriToBackend[i].pszUri);
    return UriSet;
}
#endif

/**
 * Returns a medium format object corresponding to the given
 * disk image or null if no such format.
 *
 * @param di   Disk Image
 * @param mf   Medium Format
 *
 * @return ComObjPtr<MediumFormat>
 */
HRESULT Appliance::i_findMediumFormatFromDiskImage(const ovf::DiskImage &di, ComObjPtr<MediumFormat>& mf)
{
    HRESULT hrc = S_OK;

    /* Get the system properties. */
    SystemProperties *pSysProps = mVirtualBox->i_getSystemProperties();

    /* We need a proper source format description */
    /* Which format to use? */
    Utf8Str strSrcFormat = i_typeOfVirtualDiskFormatFromURI(di.strFormat);

    /*
     * fallback, if we can't determine virtual disk format using URI from the attribute ovf:format
     * in the corresponding section <Disk> in the OVF file.
     */
    if (strSrcFormat.isEmpty())
    {
        strSrcFormat = di.strHref;

        /* check either file gzipped or not
         * if "yes" then remove last extension,
         * i.e. "image.vmdk.gz"->"image.vmdk"
         */
        if (di.strCompression == "gzip")
        {
            if (RTPathHasSuffix(strSrcFormat.c_str()))
            {
                strSrcFormat.stripSuffix();
            }
            else
            {
                mf.setNull();
                hrc = setError(E_FAIL, tr("Internal inconsistency looking up medium format for the disk image '%s'"),
                               di.strHref.c_str());
                return hrc;
            }
        }
        /* Figure out from extension which format the image of disk has. */
        if (RTPathHasSuffix(strSrcFormat.c_str()))
        {
            const char *pszExt = RTPathSuffix(strSrcFormat.c_str());
            if (pszExt)
                pszExt++;
            mf = pSysProps->i_mediumFormatFromExtension(pszExt);
        }
        else
            mf.setNull();
    }
    else
        mf = pSysProps->i_mediumFormat(strSrcFormat);

    if (mf.isNull())
        hrc = setError(E_FAIL, tr("Internal inconsistency looking up medium format for the disk image '%s'"), di.strHref.c_str());

    return hrc;
}

/**
 * Setup automatic I/O stream digest calculation, adding it to hOurManifest.
 *
 * @returns Passthru I/O stream, of @a hVfsIos if no digest calc needed.
 * @param   hVfsIos             The stream to wrap. Always consumed.
 * @param   pszManifestEntry    The manifest entry.
 * @param   fRead               Set if read stream, clear if write.
 * @throws  Nothing.
 */
RTVFSIOSTREAM Appliance::i_manifestSetupDigestCalculationForGivenIoStream(RTVFSIOSTREAM hVfsIos, const char *pszManifestEntry,
                                                                          bool fRead /*= true */)
{
    int vrc;
    Assert(!RTManifestPtIosIsInstanceOf(hVfsIos));

    if (m->fDigestTypes == 0)
        return hVfsIos;

    /* Create the manifest if necessary. */
    if (m->hOurManifest == NIL_RTMANIFEST)
    {
        vrc = RTManifestCreate(0 /*fFlags*/, &m->hOurManifest);
        AssertRCReturnStmt(vrc, RTVfsIoStrmRelease(hVfsIos), NIL_RTVFSIOSTREAM);
    }

    /* Setup the stream. */
    RTVFSIOSTREAM hVfsIosPt;
    vrc = RTManifestEntryAddPassthruIoStream(m->hOurManifest, hVfsIos, pszManifestEntry, m->fDigestTypes, fRead, &hVfsIosPt);

    RTVfsIoStrmRelease(hVfsIos);        /* always consumed! */
    if (RT_SUCCESS(vrc))
        return hVfsIosPt;

    setErrorVrc(vrc, tr("RTManifestEntryAddPassthruIoStream failed with vrc=%Rrc"), vrc);
    return NIL_RTVFSIOSTREAM;
}

/**
 * Returns true if the appliance is in "idle" state. This should always be the
 * case unless an import or export is currently in progress. Similar to machine
 * states, this permits the Appliance implementation code to let go of the
 * Appliance object lock while a time-consuming disk conversion is in progress
 * without exposing the appliance to conflicting calls.
 *
 * This sets an error on "this" (the appliance) and returns false if the appliance
 * is busy. The caller should then return E_ACCESSDENIED.
 *
 * Must be called from under the object lock!
 */
bool Appliance::i_isApplianceIdle()
{
    if (m->state == ApplianceImporting)
        setError(VBOX_E_INVALID_OBJECT_STATE, tr("The appliance is busy importing files"));
    else if (m->state == ApplianceExporting)
        setError(VBOX_E_INVALID_OBJECT_STATE, tr("The appliance is busy exporting files"));
    else
        return true;

    return false;
}

HRESULT Appliance::i_searchUniqueVMName(Utf8Str &aName) const
{
    ComPtr<IMachine> ptrMachine;
    char *tmpName = RTStrDup(aName.c_str());
    int i = 1;
    while (mVirtualBox->FindMachine(Bstr(tmpName).raw(), ptrMachine.asOutParam()) != VBOX_E_OBJECT_NOT_FOUND)
    {
        RTStrFree(tmpName);
        RTStrAPrintf(&tmpName, "%s %d", aName.c_str(), i);
        ++i;
    }
    aName = tmpName;
    RTStrFree(tmpName);

    return S_OK;
}

HRESULT Appliance::i_ensureUniqueImageFilePath(const Utf8Str &aMachineFolder, DeviceType_T aDeviceType, Utf8Str &aName) const
{
    /*
     * Check if the file exists or if a medium with this path is registered already
     */
    Utf8Str strAbsName;
    size_t  offDashNum = ~(size_t)0;
    size_t  cchDashNum = 0;
    for (unsigned i = 1;; i++)
    {
        /* Complete the path (could be relative to machine folder). */
        int vrc = RTPathAbsExCxx(strAbsName, aMachineFolder, aName);
        AssertRCReturn(vrc, Global::vboxStatusCodeToCOM(vrc));  /** @todo stupid caller ignores this */

        /* Check that the file does not exist and that there is no media somehow matching the name. */
        if (!RTPathExists(strAbsName.c_str()))
        {
            ComPtr<IMedium> ptrMedium;
            HRESULT hrc = mVirtualBox->OpenMedium(Bstr(strAbsName).raw(), aDeviceType, AccessMode_ReadWrite,
                                                  FALSE /* fForceNewUuid */, ptrMedium.asOutParam());
            if (hrc == VBOX_E_OBJECT_NOT_FOUND)
                return S_OK;
        }

        /* Insert '_%i' before the suffix and try again. */
        if (offDashNum == ~(size_t)0)
        {
            const char *pszSuffix = RTPathSuffix(aName.c_str());
            offDashNum = pszSuffix ? (size_t)(pszSuffix - aName.c_str()) : aName.length();
        }
        char   szTmp[32];
        size_t cchTmp = RTStrPrintf(szTmp, sizeof(szTmp),  "_%u", i);
        aName.replace(offDashNum, cchDashNum, szTmp, cchTmp);
        cchDashNum = cchTmp;
    }
}

/**
 * Called from Appliance::importImpl() and Appliance::writeImpl() to set up a
 * progress object with the proper weights and maximum progress values.
 */
HRESULT Appliance::i_setUpProgress(ComObjPtr<Progress> &pProgress,
                                   const Utf8Str &strDescription,
                                   SetUpProgressMode mode)
{
    HRESULT hrc;

    /* Create the progress object */
    try
    {
        hrc = pProgress.createObject();
        if (FAILED(hrc))
            return hrc;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    // compute the disks weight (this sets ulTotalDisksMB and cDisks in the instance data)
    i_disksWeight();

    m->ulWeightForManifestOperation = 0;

    ULONG cOperations = 1               // one for XML setup
                      + m->cDisks;      // plus one per disk
    ULONG ulTotalOperationsWeight;
    if (m->ulTotalDisksMB)
    {
        m->ulWeightForXmlOperation = (ULONG)((double)m->ulTotalDisksMB * 1 / 100);    // use 1% of the progress for the XML
        ulTotalOperationsWeight = m->ulTotalDisksMB + m->ulWeightForXmlOperation;
    }
    else
    {
        // no disks to export:
        m->ulWeightForXmlOperation = 1;
        ulTotalOperationsWeight = 1;
    }

    switch (mode)
    {
        case ImportFile:
        {
            break;
        }
        case WriteFile:
        {
            // assume that creating the manifest will take .1% of the time it takes to export the disks
            if (m->fManifest)
            {
                ++cOperations;          // another one for creating the manifest

                m->ulWeightForManifestOperation = (ULONG)((double)m->ulTotalDisksMB * .1 / 100);    // use .5% of the
                                                                                                    // progress for the manifest
                ulTotalOperationsWeight += m->ulWeightForManifestOperation;
            }
            break;
        }
        case ImportS3:
        {
            cOperations += 1 + 1;       // another one for the manifest file & another one for the import
            ulTotalOperationsWeight = m->ulTotalDisksMB;
            if (!m->ulTotalDisksMB)
                // no disks to export:
                ulTotalOperationsWeight = 1;

            ULONG ulImportWeight = (ULONG)((double)ulTotalOperationsWeight * 50  / 100);  // use 50% for import
            ulTotalOperationsWeight += ulImportWeight;

            m->ulWeightForXmlOperation = ulImportWeight; /* save for using later */

            ULONG ulInitWeight = (ULONG)((double)ulTotalOperationsWeight * 0.1  / 100);  // use 0.1% for init
            ulTotalOperationsWeight += ulInitWeight;
            break;
        }
        case WriteS3:
        {
            cOperations += 1 + 1;       // another one for the mf & another one for temporary creation

            if (m->ulTotalDisksMB)
            {
                m->ulWeightForXmlOperation = (ULONG)((double)m->ulTotalDisksMB * 1  / 100);    // use 1% of the progress
                                                                                               // for OVF file upload
                                                                                               // (we didn't know the
                                                                                               // size at this point)
                ulTotalOperationsWeight = m->ulTotalDisksMB + m->ulWeightForXmlOperation;
            }
            else
            {
                // no disks to export:
                ulTotalOperationsWeight = 1;
                m->ulWeightForXmlOperation = 1;
            }
            ULONG ulOVFCreationWeight = (ULONG)((double)ulTotalOperationsWeight * 50.0 / 100.0); /* Use 50% for the
                                                                                                    creation of the OVF
                                                                                                    & the disks */
            ulTotalOperationsWeight += ulOVFCreationWeight;
            break;
        }
        case ExportCloud:
        case ImportCloud:
            break;
    }
    Log(("Setting up progress object: ulTotalMB = %d, cDisks = %d, => cOperations = %d, ulTotalOperationsWeight = %d, m->ulWeightForXmlOperation = %d\n",
         m->ulTotalDisksMB, m->cDisks, cOperations, ulTotalOperationsWeight, m->ulWeightForXmlOperation));

    return pProgress->init(mVirtualBox, static_cast<IAppliance*>(this),
                           strDescription,
                           TRUE /* aCancelable */,
                           cOperations, // ULONG cOperations,
                           ulTotalOperationsWeight, // ULONG ulTotalOperationsWeight,
                           strDescription, // CBSTR bstrFirstOperationDescription,
                           m->ulWeightForXmlOperation); // ULONG ulFirstOperationWeight,
}

void Appliance::i_addWarning(const char* aWarning, ...)
{
    try
    {
        va_list args;
        va_start(args, aWarning);
        Utf8Str str(aWarning, args);
        va_end(args);
        m->llWarnings.push_back(str);
    }
    catch (...)
    {
        AssertFailed();
    }
}

/**
 * Refreshes the cDisks and ulTotalDisksMB members in the instance data.
 * Requires that virtual system descriptions are present.
 */
void Appliance::i_disksWeight()
{
    m->ulTotalDisksMB = 0;
    m->cDisks = 0;
    // weigh the disk images according to their sizes
    list< ComObjPtr<VirtualSystemDescription> >::const_iterator it;
    for (it = m->virtualSystemDescriptions.begin();
         it != m->virtualSystemDescriptions.end();
         ++it)
    {
        ComObjPtr<VirtualSystemDescription> vsdescThis = (*it);
        /* One for every medium of the Virtual System */
        std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskImage);
        std::list<VirtualSystemDescriptionEntry*>::const_iterator itH;
        for (itH = avsdeHDs.begin();
             itH != avsdeHDs.end();
             ++itH)
        {
            const VirtualSystemDescriptionEntry *pHD = *itH;
            m->ulTotalDisksMB += pHD->ulSizeMB;
            ++m->cDisks;
        }

        avsdeHDs = vsdescThis->i_findByType(VirtualSystemDescriptionType_CDROM);
        for (itH = avsdeHDs.begin();
             itH != avsdeHDs.end();
             ++itH)
        {
            const VirtualSystemDescriptionEntry *pHD = *itH;
            m->ulTotalDisksMB += pHD->ulSizeMB;
            ++m->cDisks;
        }
    }

}

void Appliance::i_parseBucket(Utf8Str &aPath, Utf8Str &aBucket)
{
    /* Buckets are S3 specific. So parse the bucket out of the file path */
    if (!aPath.startsWith("/"))
        throw setError(E_INVALIDARG,
                       tr("The path '%s' must start with /"), aPath.c_str());
    size_t bpos = aPath.find("/", 1);
    if (bpos != Utf8Str::npos)
    {
        aBucket = aPath.substr(1, bpos - 1); /* The bucket without any slashes */
        aPath = aPath.substr(bpos); /* The rest of the file path */
    }
    /* If there is no bucket name provided reject it */
    if (aBucket.isEmpty())
        throw setError(E_INVALIDARG,
                       tr("You doesn't provide a bucket name in the URI '%s'"), aPath.c_str());
}

/**
 * Worker for TaskOVF::handler.
 *
 * The TaskOVF is started in Appliance::readImpl() and Appliance::importImpl()
 * and Appliance::writeImpl().
 *
 * This will in turn call Appliance::readFS() or Appliance::importFS() or
 * Appliance::writeFS().
 *
 * @thread  pTask       The task.
 */
/* static */ void Appliance::i_importOrExportThreadTask(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    AssertReturnVoid(pTask);

    Appliance *pAppliance = pTask->pAppliance;
    LogFlowFunc(("Appliance %p taskType=%d\n", pAppliance, pTask->taskType));

    switch (pTask->taskType)
    {
        case TaskOVF::Read:
            pAppliance->m->resetReadData();
            if (pTask->locInfo.storageType == VFSType_File)
                pTask->hrc = pAppliance->i_readFS(pTask);
            else
                pTask->hrc = E_NOTIMPL;
            break;

        case TaskOVF::Import:
            /** @todo allow overriding these? */
            if (!pAppliance->m->fSignatureValid && pAppliance->m->pbSignedDigest)
                pTask->hrc = pAppliance->setError(E_FAIL, tr("The manifest signature for '%s' is not valid"),
                                                  pTask->locInfo.strPath.c_str());
            else if (!pAppliance->m->fCertificateValid && pAppliance->m->pbSignedDigest)
            {
                if (pAppliance->m->strCertError.isNotEmpty())
                    pTask->hrc = pAppliance->setError(E_FAIL, tr("The certificate used to signed '%s' is not valid: %s"),
                                                      pTask->locInfo.strPath.c_str(), pAppliance->m->strCertError.c_str());
                else
                    pTask->hrc = pAppliance->setError(E_FAIL, tr("The certificate used to signed '%s' is not valid"),
                                                      pTask->locInfo.strPath.c_str());
            }
            // fusion does not consider this a show stopper (we've filed a warning during read).
            //else if (pAppliance->m->fCertificateMissingPath && pAppliance->m->pbSignedDigest)
            //    pTask->hrc = pAppliance->setError(E_FAIL, tr("The certificate used to signed '%s' is does not have a valid CA path"),
            //                                      pTask->locInfo.strPath.c_str());
            else
            {
                if (pTask->locInfo.storageType == VFSType_File)
                    pTask->hrc = pAppliance->i_importFS(pTask);
                else
                    pTask->hrc = E_NOTIMPL;
            }
            break;

        case TaskOVF::Write:
            if (pTask->locInfo.storageType == VFSType_File)
                pTask->hrc = pAppliance->i_writeFS(pTask);
            else
                pTask->hrc = E_NOTIMPL;
            break;

        default:
            AssertFailed();
            pTask->hrc = E_FAIL;
            break;
    }

    if (!pTask->pProgress.isNull())
        pTask->pProgress->i_notifyComplete(pTask->hrc);

    LogFlowFuncLeave();
}

/* static */ DECLCALLBACK(int) Appliance::TaskOVF::updateProgress(unsigned uPercent, void *pvUser)
{
    Appliance::TaskOVF* pTask = *(Appliance::TaskOVF**)pvUser;

    if (    pTask
         && !pTask->pProgress.isNull())
    {
        BOOL fCanceled;
        pTask->pProgress->COMGETTER(Canceled)(&fCanceled);
        if (fCanceled)
            return -1;
        pTask->pProgress->SetCurrentOperationProgress(uPercent);
    }
    return VINF_SUCCESS;
}

/**
 * Worker for TaskOPC::handler.
 * @thread  pTask       The task.
 */
/* static */
void Appliance::i_exportOPCThreadTask(TaskOPC *pTask)
{
    LogFlowFuncEnter();
    AssertReturnVoid(pTask);

    Appliance *pAppliance = pTask->pAppliance;
    LogFlowFunc(("Appliance %p taskType=%d\n", pAppliance, pTask->taskType));

    switch (pTask->taskType)
    {
        case TaskOPC::Export:
            pTask->hrc = pAppliance->i_writeFSOPC(pTask);
            break;

        default:
            AssertFailed();
            pTask->hrc = E_FAIL;
            break;
    }

    if (!pTask->pProgress.isNull())
        pTask->pProgress->i_notifyComplete(pTask->hrc);

    LogFlowFuncLeave();
}

/* static */
DECLCALLBACK(int) Appliance::TaskOPC::updateProgress(unsigned uPercent, void *pvUser)
{
    Appliance::TaskOPC* pTask = *(Appliance::TaskOPC**)pvUser;

    if (    pTask
         && !pTask->pProgress.isNull())
    {
        BOOL fCanceled;
        pTask->pProgress->COMGETTER(Canceled)(&fCanceled);
        if (fCanceled)
            return -1;
        pTask->pProgress->SetCurrentOperationProgress(uPercent);
    }
    return VINF_SUCCESS;
}

/**
 * Worker for TaskCloud::handler.
 * @thread  pTask       The task.
 */
/* static */
void Appliance::i_importOrExportCloudThreadTask(TaskCloud *pTask)
{
    LogFlowFuncEnter();
    AssertReturnVoid(pTask);

    Appliance *pAppliance = pTask->pAppliance;
    LogFlowFunc(("Appliance %p taskType=%d\n", pAppliance, pTask->taskType));

    switch (pTask->taskType)
    {
        case TaskCloud::Export:
            pAppliance->i_setApplianceState(ApplianceExporting);
            pTask->hrc = pAppliance->i_exportCloudImpl(pTask);
            break;
        case TaskCloud::Import:
            pAppliance->i_setApplianceState(ApplianceImporting);
            pTask->hrc = pAppliance->i_importCloudImpl(pTask);
            break;
        case TaskCloud::ReadData:
            pAppliance->i_setApplianceState(ApplianceImporting);
            pTask->hrc = pAppliance->i_gettingCloudData(pTask);
            break;
        default:
            AssertFailed();
            pTask->hrc = E_FAIL;
            break;
    }

    pAppliance->i_setApplianceState(ApplianceIdle);

    if (!pTask->pProgress.isNull())
        pTask->pProgress->i_notifyComplete(pTask->hrc);

    LogFlowFuncLeave();
}

/* static */
DECLCALLBACK(int) Appliance::TaskCloud::updateProgress(unsigned uPercent, void *pvUser)
{
    Appliance::TaskCloud* pTask = *(Appliance::TaskCloud**)pvUser;

    if (    pTask
         && !pTask->pProgress.isNull())
    {
        BOOL fCanceled;
        pTask->pProgress->COMGETTER(Canceled)(&fCanceled);
        if (fCanceled)
            return -1;
        pTask->pProgress->SetCurrentOperationProgress(uPercent);
    }
    return VINF_SUCCESS;
}

void i_parseURI(Utf8Str strUri, LocationInfo &locInfo)
{
    /* Check the URI for the protocol */
    if (strUri.startsWith("file://", Utf8Str::CaseInsensitive)) /* File based */
    {
        locInfo.storageType = VFSType_File;
        strUri = strUri.substr(sizeof("file://") - 1);
    }
    else if (strUri.startsWith("SunCloud://", Utf8Str::CaseInsensitive)) /* Sun Cloud service */
    {
        locInfo.storageType = VFSType_S3;
        strUri = strUri.substr(sizeof("SunCloud://") - 1);
    }
    else if (strUri.startsWith("S3://", Utf8Str::CaseInsensitive)) /* S3 service */
    {
        locInfo.storageType = VFSType_S3;
        strUri = strUri.substr(sizeof("S3://") - 1);
    }
    else if (strUri.startsWith("OCI://", Utf8Str::CaseInsensitive)) /* OCI service (storage or compute) */
    {
        locInfo.storageType = VFSType_Cloud;
        locInfo.strProvider = "OCI";
        strUri = strUri.substr(sizeof("OCI://") - 1);
    }
    else if (strUri.startsWith("webdav://", Utf8Str::CaseInsensitive)) /* webdav service */
        throw E_NOTIMPL;

    /* Not necessary on a file based URI */
//  if (locInfo.storageType != VFSType_File)
//  {
//      size_t uppos = strUri.find("@"); /* username:password combo */
//      if (uppos != Utf8Str::npos)
//      {
//          locInfo.strUsername = strUri.substr(0, uppos);
//          strUri = strUri.substr(uppos + 1);
//          size_t upos = locInfo.strUsername.find(":");
//          if (upos != Utf8Str::npos)
//          {
//              locInfo.strPassword = locInfo.strUsername.substr(upos + 1);
//              locInfo.strUsername = locInfo.strUsername.substr(0, upos);
//          }
//      }
//      size_t hpos = strUri.find("/"); /* hostname part */
//      if (hpos != Utf8Str::npos)
//      {
//          locInfo.strHostname = strUri.substr(0, hpos);
//          strUri = strUri.substr(hpos);
//      }
//  }

    locInfo.strPath = strUri;
}


////////////////////////////////////////////////////////////////////////////////
//
// IVirtualSystemDescription constructor / destructor
//
////////////////////////////////////////////////////////////////////////////////

/**
 * COM initializer.
 * @return
 */
HRESULT VirtualSystemDescription::init()
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Initialize data */
    m = new Data();
    m->pConfig = NULL;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();
    return S_OK;
}

/**
* COM uninitializer.
*/

void VirtualSystemDescription::uninit()
{
    if (m->pConfig)
        delete m->pConfig;
    delete m;
    m = NULL;
}


////////////////////////////////////////////////////////////////////////////////
//
// IVirtualSystemDescription public methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Public method implementation.
 */
HRESULT VirtualSystemDescription::getCount(ULONG *aCount)
{
    if (!aCount)
        return E_POINTER;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aCount = (ULONG)m->maDescriptions.size();
    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT VirtualSystemDescription::getDescription(std::vector<VirtualSystemDescriptionType_T> &aTypes,
                                                 std::vector<com::Utf8Str> &aRefs,
                                                 std::vector<com::Utf8Str> &aOVFValues,
                                                 std::vector<com::Utf8Str> &aVBoxValues,
                                                 std::vector<com::Utf8Str> &aExtraConfigValues)

{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    size_t c = m->maDescriptions.size();
    aTypes.resize(c);
    aRefs.resize(c);
    aOVFValues.resize(c);
    aVBoxValues.resize(c);
    aExtraConfigValues.resize(c);

    for (size_t i = 0; i < c; i++)
    {
        const VirtualSystemDescriptionEntry &vsde = m->maDescriptions[i];
        aTypes[i] = vsde.type;
        aRefs[i] = vsde.strRef;
        aOVFValues[i] = vsde.strOvf;
        aVBoxValues[i] = vsde.strVBoxCurrent;
        aExtraConfigValues[i] = vsde.strExtraConfigCurrent;
    }
    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT VirtualSystemDescription::getDescriptionByType(VirtualSystemDescriptionType_T aType,
                                                       std::vector<VirtualSystemDescriptionType_T> &aTypes,
                                                       std::vector<com::Utf8Str> &aRefs,
                                                       std::vector<com::Utf8Str> &aOVFValues,
                                                       std::vector<com::Utf8Str> &aVBoxValues,
                                                       std::vector<com::Utf8Str> &aExtraConfigValues)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    std::list<VirtualSystemDescriptionEntry*> vsd = i_findByType(aType);

    size_t c = vsd.size();
    aTypes.resize(c);
    aRefs.resize(c);
    aOVFValues.resize(c);
    aVBoxValues.resize(c);
    aExtraConfigValues.resize(c);

    size_t i = 0;
    for (list<VirtualSystemDescriptionEntry*>::const_iterator it = vsd.begin(); it != vsd.end(); ++it, ++i)
    {
        const VirtualSystemDescriptionEntry *vsde = (*it);
        aTypes[i] = vsde->type;
        aRefs[i] = vsde->strRef;
        aOVFValues[i] = vsde->strOvf;
        aVBoxValues[i] = vsde->strVBoxCurrent;
        aExtraConfigValues[i] = vsde->strExtraConfigCurrent;
    }

    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT VirtualSystemDescription::getValuesByType(VirtualSystemDescriptionType_T aType,
                                                  VirtualSystemDescriptionValueType_T aWhich,
                                                  std::vector<com::Utf8Str> &aValues)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    std::list<VirtualSystemDescriptionEntry*> vsd = i_findByType (aType);
    aValues.resize((ULONG)vsd.size());

    list<VirtualSystemDescriptionEntry*>::const_iterator it;
    size_t i = 0;
    for (it = vsd.begin();
         it != vsd.end();
         ++it, ++i)
    {
        const VirtualSystemDescriptionEntry *vsde = (*it);

        Bstr bstr;
        switch (aWhich)
        {
            case VirtualSystemDescriptionValueType_Reference: aValues[i]  = vsde->strRef; break;
            case VirtualSystemDescriptionValueType_Original: aValues[i]  = vsde->strOvf; break;
            case VirtualSystemDescriptionValueType_Auto: aValues[i]  = vsde->strVBoxCurrent; break;
            case VirtualSystemDescriptionValueType_ExtraConfig: aValues[i] = vsde->strExtraConfigCurrent; break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
            case VirtualSystemDescriptionValueType_32BitHack: AssertFailedBreak(); /* (compiler warnings) */
#endif
        }
    }

    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT VirtualSystemDescription::setFinalValues(const std::vector<BOOL> &aEnabled,
                                                 const std::vector<com::Utf8Str> &aVBoxValues,
                                                 const std::vector<com::Utf8Str> &aExtraConfigValues)
{
#ifndef RT_OS_WINDOWS
    // NOREF(aEnabledSize);
#endif /* RT_OS_WINDOWS */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (    (aEnabled.size() != m->maDescriptions.size())
         || (aVBoxValues.size() != m->maDescriptions.size())
         || (aExtraConfigValues.size() != m->maDescriptions.size())
       )
        return E_INVALIDARG;

    size_t i = 0;
    for (vector<VirtualSystemDescriptionEntry>::iterator it = m->maDescriptions.begin();
         it != m->maDescriptions.end();
         ++it, ++i)
    {
        VirtualSystemDescriptionEntry& vsde = *it;

        if (aEnabled[i])
        {
            vsde.strVBoxCurrent = aVBoxValues[i];
            vsde.strExtraConfigCurrent = aExtraConfigValues[i];
        }
        else
            vsde.type = VirtualSystemDescriptionType_Ignore;
    }

    return S_OK;
}

/**
 * Public method implementation.
 */
HRESULT VirtualSystemDescription::addDescription(VirtualSystemDescriptionType_T aType,
                                                 const com::Utf8Str &aVBoxValue,
                                                 const com::Utf8Str &aExtraConfigValue)

{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    i_addEntry(aType, "", aVBoxValue, aVBoxValue, 0, aExtraConfigValue);
    return S_OK;
}

/**
 * Internal method; adds a new description item to the member list.
 * @param aType Type of description for the new item.
 * @param strRef Reference item; only used with storage controllers.
 * @param aOvfValue Corresponding original value from OVF.
 * @param aVBoxValue Initial configuration value (can be overridden by caller with setFinalValues).
 * @param ulSizeMB Weight for IProgress
 * @param strExtraConfig Extra configuration; meaning dependent on type.
 */
void VirtualSystemDescription::i_addEntry(VirtualSystemDescriptionType_T aType,
                                          const Utf8Str &strRef,
                                          const Utf8Str &aOvfValue,
                                          const Utf8Str &aVBoxValue,
                                          uint32_t ulSizeMB,
                                          const Utf8Str &strExtraConfig /*= ""*/)
{
    VirtualSystemDescriptionEntry vsde;
    vsde.ulIndex = (uint32_t)m->maDescriptions.size();      // each entry gets an index so the client side can reference them
    vsde.type = aType;
    vsde.strRef = strRef;
    vsde.strOvf = aOvfValue;
    vsde.strVBoxSuggested           /* remember original value */
        = vsde.strVBoxCurrent       /* and set current value which can be overridden by setFinalValues() */
        = aVBoxValue;
    vsde.strExtraConfigSuggested
        = vsde.strExtraConfigCurrent
        = strExtraConfig;
    vsde.ulSizeMB = ulSizeMB;

    vsde.skipIt = false;

    m->maDescriptions.push_back(vsde);
}

/**
 * Private method; returns a list of description items containing all the items from the member
 * description items of this virtual system that match the given type.
 */
std::list<VirtualSystemDescriptionEntry*> VirtualSystemDescription::i_findByType(VirtualSystemDescriptionType_T aType)
{
    std::list<VirtualSystemDescriptionEntry*> vsd;
    for (vector<VirtualSystemDescriptionEntry>::iterator it = m->maDescriptions.begin();
         it != m->maDescriptions.end();
         ++it)
    {
        if (it->type == aType)
            vsd.push_back(&(*it));
    }

    return vsd;
}

HRESULT VirtualSystemDescription::removeDescriptionByType(VirtualSystemDescriptionType_T aType)
{
    std::vector<VirtualSystemDescriptionEntry>::iterator it = m->maDescriptions.begin();
    while (it != m->maDescriptions.end())
    {
        if (it->type == aType)
            it = m->maDescriptions.erase(it);
        else
            ++it;
    }

    return S_OK;
}

/* Private method; delete all records from the list
 * m->llDescriptions that match the given type.
 */
void VirtualSystemDescription::i_removeByType(VirtualSystemDescriptionType_T aType)
{
    std::vector<VirtualSystemDescriptionEntry>::iterator it = m->maDescriptions.begin();
    while (it != m->maDescriptions.end())
    {
        if (it->type == aType)
            it = m->maDescriptions.erase(it);
        else
            ++it;
    }
}

/**
 * Private method; looks thru the member hardware items for the IDE, SATA, or SCSI controller with
 * the given reference ID. Useful when needing the controller for a particular
 * virtual disk.
 */
const VirtualSystemDescriptionEntry* VirtualSystemDescription::i_findControllerFromID(const Utf8Str &id)
{
    vector<VirtualSystemDescriptionEntry>::const_iterator it;
    for (it = m->maDescriptions.begin();
         it != m->maDescriptions.end();
         ++it)
    {
        const VirtualSystemDescriptionEntry &d = *it;
        switch (d.type)
        {
            case VirtualSystemDescriptionType_HardDiskControllerIDE:
            case VirtualSystemDescriptionType_HardDiskControllerSATA:
            case VirtualSystemDescriptionType_HardDiskControllerSCSI:
            case VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI:
            case VirtualSystemDescriptionType_HardDiskControllerNVMe:
            case VirtualSystemDescriptionType_HardDiskControllerSAS:
                if (d.strRef == id)
                    return &d;
                break;
            default: break; /* Shut up MSC. */
        }
    }

    return NULL;
}

/**
 * Method called from Appliance::Interpret() if the source OVF for a virtual system
 * contains a <vbox:Machine> element. This method then attempts to parse that and
 * create a MachineConfigFile instance from it which is stored in this instance data
 * and can then be used to create a machine.
 *
 * This must only be called once per instance.
 *
 * This rethrows all XML and logic errors from MachineConfigFile.
 *
 * @param elmMachine <vbox:Machine> element with attributes and subelements from some
 *                  DOM tree.
 */
void VirtualSystemDescription::i_importVBoxMachineXML(const xml::ElementNode &elmMachine)
{
    settings::MachineConfigFile *pConfig = NULL;

    Assert(m->pConfig == NULL);

    try
    {
        pConfig = new settings::MachineConfigFile(NULL);
        pConfig->importMachineXML(elmMachine);

        m->pConfig = pConfig;
    }
    catch (...)
    {
        if (pConfig)
            delete pConfig;
        throw;
    }
}

/**
 * Returns the machine config created by importVBoxMachineXML() or NULL if there's none.
 */
const settings::MachineConfigFile* VirtualSystemDescription::i_getMachineConfig() const
{
    return m->pConfig;
}

/**
 * Private method; walks through the array of VirtualSystemDescriptionEntry entries
 * and returns the one matching the given index.
 */
const VirtualSystemDescriptionEntry* VirtualSystemDescription::i_findByIndex(const uint32_t aIndex)
{
    vector<VirtualSystemDescriptionEntry>::const_iterator it;
    for (it = m->maDescriptions.begin();
         it != m->maDescriptions.end();
         ++it)
    {
        const VirtualSystemDescriptionEntry &d = *it;
        if (d.ulIndex == aIndex)
            return &d;
    }

    return NULL;
}

