/* $Id: HMVMXAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HM VMX (VT-x) - All contexts.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_HM
#define VMCPU_INCL_CPUM_GST_CTX
#include "HMInternal.h"
#include <VBox/vmm/hmvmxinline.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/pdmapi.h>
#include <iprt/errcore.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h> /* ASMCpuId_EAX */
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#define VMXV_DIAG_DESC(a_Def, a_Desc)      #a_Def " - " #a_Desc
/** VMX virtual-instructions and VM-exit diagnostics. */
static const char * const g_apszVmxVDiagDesc[] =
{
    /* Internal processing errors. */
    VMXV_DIAG_DESC(kVmxVDiag_None                             , "None"                      ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_1                            , "Ipe_1"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_2                            , "Ipe_2"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_3                            , "Ipe_3"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_4                            , "Ipe_4"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_5                            , "Ipe_5"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_6                            , "Ipe_6"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_7                            , "Ipe_7"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_8                            , "Ipe_8"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_9                            , "Ipe_9"                     ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_10                           , "Ipe_10"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_11                           , "Ipe_11"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_12                           , "Ipe_12"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_13                           , "Ipe_13"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_14                           , "Ipe_14"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_15                           , "Ipe_15"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Ipe_16                           , "Ipe_16"                    ),
    /* VMXON. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_A20M                       , "A20M"                      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_Cpl                        , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_Cr0Fixed0                  , "Cr0Fixed0"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_Cr0Fixed1                  , "Cr0Fixed1"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_Cr4Fixed0                  , "Cr4Fixed0"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_Cr4Fixed1                  , "Cr4Fixed1"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_Intercept                  , "Intercept"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_LongModeCS                 , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_MsrFeatCtl                 , "MsrFeatCtl"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_PtrAbnormal                , "PtrAbnormal"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_PtrAlign                   , "PtrAlign"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_PtrMap                     , "PtrMap"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_PtrReadPhys                , "PtrReadPhys"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_PtrWidth                   , "PtrWidth"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_RealOrV86Mode              , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_ShadowVmcs                 , "ShadowVmcs"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_VmxAlreadyRoot             , "VmxAlreadyRoot"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_Vmxe                       , "Vmxe"                      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_VmcsRevId                  , "VmcsRevId"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxon_VmxRootCpl                 , "VmxRootCpl"                ),
    /* VMXOFF. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmxoff_Cpl                       , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxoff_Intercept                 , "Intercept"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxoff_LongModeCS                , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxoff_RealOrV86Mode             , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxoff_Vmxe                      , "Vmxe"                      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmxoff_VmxRoot                   , "VmxRoot"                   ),
    /* VMPTRLD. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_Cpl                      , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_LongModeCS               , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_PtrAbnormal              , "PtrAbnormal"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_PtrAlign                 , "PtrAlign"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_PtrMap                   , "PtrMap"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_PtrReadPhys              , "PtrReadPhys"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_PtrVmxon                 , "PtrVmxon"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_PtrWidth                 , "PtrWidth"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_RealOrV86Mode            , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_RevPtrReadPhys           , "RevPtrReadPhys"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_ShadowVmcs               , "ShadowVmcs"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_VmcsRevId                , "VmcsRevId"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrld_VmxRoot                  , "VmxRoot"                   ),
    /* VMPTRST. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrst_Cpl                      , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrst_LongModeCS               , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrst_PtrMap                   , "PtrMap"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrst_RealOrV86Mode            , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmptrst_VmxRoot                  , "VmxRoot"                   ),
    /* VMCLEAR. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_Cpl                      , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_LongModeCS               , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_PtrAbnormal              , "PtrAbnormal"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_PtrAlign                 , "PtrAlign"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_PtrMap                   , "PtrMap"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_PtrReadPhys              , "PtrReadPhys"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_PtrVmxon                 , "PtrVmxon"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_PtrWidth                 , "PtrWidth"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_RealOrV86Mode            , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmclear_VmxRoot                  , "VmxRoot"                   ),
    /* VMWRITE. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_Cpl                      , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_FieldInvalid             , "FieldInvalid"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_FieldRo                  , "FieldRo"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_LinkPtrInvalid           , "LinkPtrInvalid"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_LongModeCS               , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_PtrInvalid               , "PtrInvalid"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_PtrMap                   , "PtrMap"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_RealOrV86Mode            , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmwrite_VmxRoot                  , "VmxRoot"                   ),
    /* VMREAD. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_Cpl                       , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_FieldInvalid              , "FieldInvalid"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_LinkPtrInvalid            , "LinkPtrInvalid"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_LongModeCS                , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_PtrInvalid                , "PtrInvalid"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_PtrMap                    , "PtrMap"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_RealOrV86Mode             , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmread_VmxRoot                   , "VmxRoot"                   ),
    /* INVVPID. */
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_Cpl                      , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_DescRsvd                 , "DescRsvd"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_LongModeCS               , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_RealOrV86Mode            , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_TypeInvalid              , "TypeInvalid"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_Type0InvalidAddr         , "Type0InvalidAddr"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_Type0InvalidVpid         , "Type0InvalidVpid"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_Type1InvalidVpid         , "Type1InvalidVpid"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_Type3InvalidVpid         , "Type3InvalidVpid"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Invvpid_VmxRoot                  , "VmxRoot"                   ),
    /* INVEPT. */
    VMXV_DIAG_DESC(kVmxVDiag_Invept_Cpl                       , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Invept_DescRsvd                  , "DescRsvd"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Invept_EptpInvalid               , "EptpInvalid"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Invept_LongModeCS                , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Invept_RealOrV86Mode             , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Invept_TypeInvalid               , "TypeInvalid"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Invept_VmxRoot                   , "VmxRoot"                   ),
    /* VMLAUNCH/VMRESUME. */
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrApicAccess           , "AddrApicAccess"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrApicAccessEqVirtApic , "AddrApicAccessEqVirtApic"  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrApicAccessHandlerReg , "AddrApicAccessHandlerReg"  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrEntryMsrLoad         , "AddrEntryMsrLoad"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrExitMsrLoad          , "AddrExitMsrLoad"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrExitMsrStore         , "AddrExitMsrStore"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrIoBitmapA            , "AddrIoBitmapA"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrIoBitmapB            , "AddrIoBitmapB"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrMsrBitmap            , "AddrMsrBitmap"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrVirtApicPage         , "AddrVirtApicPage"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrVmcsLinkPtr          , "AddrVmcsLinkPtr"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrVmreadBitmap         , "AddrVmreadBitmap"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_AddrVmwriteBitmap        , "AddrVmwriteBitmap"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_ApicRegVirt              , "ApicRegVirt"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_BlocKMovSS               , "BlockMovSS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_Cpl                      , "Cpl"                       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_Cr3TargetCount           , "Cr3TargetCount"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryCtlsAllowed1        , "EntryCtlsAllowed1"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryCtlsDisallowed0     , "EntryCtlsDisallowed0"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryInstrLen            , "EntryInstrLen"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryInstrLenZero        , "EntryInstrLenZero"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryIntInfoErrCodePe    , "EntryIntInfoErrCodePe"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryIntInfoErrCodeVec   , "EntryIntInfoErrCodeVec"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryIntInfoTypeVecRsvd  , "EntryIntInfoTypeVecRsvd"   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EntryXcptErrCodeRsvd     , "EntryXcptErrCodeRsvd"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EptpAccessDirty          , "EptpAccessDirty"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EptpPageWalkLength       , "EptpPageWalkLength"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EptpMemType              , "EptpMemType"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_EptpRsvd                 , "EptpRsvd"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_ExitCtlsAllowed1         , "ExitCtlsAllowed1"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_ExitCtlsDisallowed0      , "ExitCtlsDisallowed0"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestActStateHlt         , "GuestActStateHlt"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestActStateRsvd        , "GuestActStateRsvd"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestActStateShutdown    , "GuestActStateShutdown"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestActStateSsDpl       , "GuestActStateSsDpl"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestActStateStiMovSs    , "GuestActStateStiMovSs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestCr0Fixed0           , "GuestCr0Fixed0"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestCr0Fixed1           , "GuestCr0Fixed1"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestCr0PgPe             , "GuestCr0PgPe"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestCr3                 , "GuestCr3"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestCr4Fixed0           , "GuestCr4Fixed0"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestCr4Fixed1           , "GuestCr4Fixed1"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestDebugCtl            , "GuestDebugCtl"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestDr7                 , "GuestDr7"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestEferMsr             , "GuestEferMsr"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestEferMsrRsvd         , "GuestEferMsrRsvd"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestGdtrBase            , "GuestGdtrBase"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestGdtrLimit           , "GuestGdtrLimit"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIdtrBase            , "GuestIdtrBase"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIdtrLimit           , "GuestIdtrLimit"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateEnclave     , "GuestIntStateEnclave"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateExtInt      , "GuestIntStateExtInt"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateNmi         , "GuestIntStateNmi"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateRFlagsSti   , "GuestIntStateRFlagsSti"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateRsvd        , "GuestIntStateRsvd"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateSmi         , "GuestIntStateSmi"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateStiMovSs    , "GuestIntStateStiMovSs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestIntStateVirtNmi     , "GuestIntStateVirtNmi"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPae                 , "GuestPae"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPatMsr              , "GuestPatMsr"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPcide               , "GuestPcide"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPdpte               , "GuestPdpteRsvd"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPndDbgXcptBsNoTf    , "GuestPndDbgXcptBsNoTf"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPndDbgXcptBsTf      , "GuestPndDbgXcptBsTf"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPndDbgXcptRsvd      , "GuestPndDbgXcptRsvd"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestPndDbgXcptRtm       , "GuestPndDbgXcptRtm"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestRip                 , "GuestRip"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestRipRsvd             , "GuestRipRsvd"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestRFlagsIf            , "GuestRFlagsIf"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestRFlagsRsvd          , "GuestRFlagsRsvd"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestRFlagsVm            , "GuestRFlagsVm"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrCsDefBig     , "GuestSegAttrCsDefBig"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrCsDplEqSs    , "GuestSegAttrCsDplEqSs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrCsDplLtSs    , "GuestSegAttrCsDplLtSs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrCsDplZero    , "GuestSegAttrCsDplZero"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrCsType       , "GuestSegAttrCsType"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrCsTypeRead   , "GuestSegAttrCsTypeRead"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDescTypeCs   , "GuestSegAttrDescTypeCs"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDescTypeDs   , "GuestSegAttrDescTypeDs"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDescTypeEs   , "GuestSegAttrDescTypeEs"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDescTypeFs   , "GuestSegAttrDescTypeFs"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDescTypeGs   , "GuestSegAttrDescTypeGs"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDescTypeSs   , "GuestSegAttrDescTypeSs"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDplRplCs     , "GuestSegAttrDplRplCs"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDplRplDs     , "GuestSegAttrDplRplDs"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDplRplEs     , "GuestSegAttrDplRplEs"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDplRplFs     , "GuestSegAttrDplRplFs"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDplRplGs     , "GuestSegAttrDplRplGs"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrDplRplSs     , "GuestSegAttrDplRplSs"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrGranCs       , "GuestSegAttrGranCs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrGranDs       , "GuestSegAttrGranDs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrGranEs       , "GuestSegAttrGranEs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrGranFs       , "GuestSegAttrGranFs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrGranGs       , "GuestSegAttrGranGs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrGranSs       , "GuestSegAttrGranSs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrLdtrDescType , "GuestSegAttrLdtrDescType"  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrLdtrGran     , "GuestSegAttrLdtrGran"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrLdtrPresent  , "GuestSegAttrLdtrPresent"   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrLdtrRsvd     , "GuestSegAttrLdtrRsvd"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrLdtrType     , "GuestSegAttrLdtrType"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrPresentCs    , "GuestSegAttrPresentCs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrPresentDs    , "GuestSegAttrPresentDs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrPresentEs    , "GuestSegAttrPresentEs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrPresentFs    , "GuestSegAttrPresentFs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrPresentGs    , "GuestSegAttrPresentGs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrPresentSs    , "GuestSegAttrPresentSs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrRsvdCs       , "GuestSegAttrRsvdCs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrRsvdDs       , "GuestSegAttrRsvdDs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrRsvdEs       , "GuestSegAttrRsvdEs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrRsvdFs       , "GuestSegAttrRsvdFs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrRsvdGs       , "GuestSegAttrRsvdGs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrRsvdSs       , "GuestSegAttrRsvdSs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrSsDplEqRpl   , "GuestSegAttrSsDplEqRpl"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrSsDplZero    , "GuestSegAttrSsDplZero "    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrSsType       , "GuestSegAttrSsType"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTrDescType   , "GuestSegAttrTrDescType"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTrGran       , "GuestSegAttrTrGran"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTrPresent    , "GuestSegAttrTrPresent"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTrRsvd       , "GuestSegAttrTrRsvd"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTrType       , "GuestSegAttrTrType"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTrUnusable   , "GuestSegAttrTrUnusable"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTypeAccCs    , "GuestSegAttrTypeAccCs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTypeAccDs    , "GuestSegAttrTypeAccDs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTypeAccEs    , "GuestSegAttrTypeAccEs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTypeAccFs    , "GuestSegAttrTypeAccFs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTypeAccGs    , "GuestSegAttrTypeAccGs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrTypeAccSs    , "GuestSegAttrTypeAccSs"     ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrV86Cs        , "GuestSegAttrV86Cs"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrV86Ds        , "GuestSegAttrV86Ds"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrV86Es        , "GuestSegAttrV86Es"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrV86Fs        , "GuestSegAttrV86Fs"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrV86Gs        , "GuestSegAttrV86Gs"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegAttrV86Ss        , "GuestSegAttrV86Ss"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseCs           , "GuestSegBaseCs"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseDs           , "GuestSegBaseDs"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseEs           , "GuestSegBaseEs"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseFs           , "GuestSegBaseFs"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseGs           , "GuestSegBaseGs"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseLdtr         , "GuestSegBaseLdtr"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseSs           , "GuestSegBaseSs"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseTr           , "GuestSegBaseTr"            ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseV86Cs        , "GuestSegBaseV86Cs"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseV86Ds        , "GuestSegBaseV86Ds"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseV86Es        , "GuestSegBaseV86Es"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseV86Fs        , "GuestSegBaseV86Fs"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseV86Gs        , "GuestSegBaseV86Gs"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegBaseV86Ss        , "GuestSegBaseV86Ss"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegLimitV86Cs       , "GuestSegLimitV86Cs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegLimitV86Ds       , "GuestSegLimitV86Ds"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegLimitV86Es       , "GuestSegLimitV86Es"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegLimitV86Fs       , "GuestSegLimitV86Fs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegLimitV86Gs       , "GuestSegLimitV86Gs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegLimitV86Ss       , "GuestSegLimitV86Ss"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegSelCsSsRpl       , "GuestSegSelCsSsRpl"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegSelLdtr          , "GuestSegSelLdtr"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSegSelTr            , "GuestSegSelTr"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_GuestSysenterEspEip      , "GuestSysenterEspEip"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmcsLinkPtrCurVmcs       , "VmcsLinkPtrCurVmcs"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmcsLinkPtrReadPhys      , "VmcsLinkPtrReadPhys"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmcsLinkPtrRevId         , "VmcsLinkPtrRevId"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmcsLinkPtrShadow        , "VmcsLinkPtrShadow"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCr0Fixed0            , "HostCr0Fixed0"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCr0Fixed1            , "HostCr0Fixed1"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCr3                  , "HostCr3"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCr4Fixed0            , "HostCr4Fixed0"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCr4Fixed1            , "HostCr4Fixed1"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCr4Pae               , "HostCr4Pae"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCr4Pcide             , "HostCr4Pcide"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostCsTr                 , "HostCsTr"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostEferMsr              , "HostEferMsr"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostEferMsrRsvd          , "HostEferMsrRsvd"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostGuestLongMode        , "HostGuestLongMode"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostGuestLongModeNoCpu   , "HostGuestLongModeNoCpu"    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostLongMode             , "HostLongMode"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostPatMsr               , "HostPatMsr"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostRip                  , "HostRip"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostRipRsvd              , "HostRipRsvd"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostSel                  , "HostSel"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostSegBase              , "HostSegBase"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostSs                   , "HostSs"                    ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_HostSysenterEspEip       , "HostSysenterEspEip"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_IoBitmapAPtrReadPhys     , "IoBitmapAPtrReadPhys"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_IoBitmapBPtrReadPhys     , "IoBitmapBPtrReadPhys"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_LongModeCS               , "LongModeCS"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_MsrBitmapPtrReadPhys     , "MsrBitmapPtrReadPhys"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_MsrLoad                  , "MsrLoad"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_MsrLoadCount             , "MsrLoadCount"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_MsrLoadPtrReadPhys       , "MsrLoadPtrReadPhys"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_MsrLoadRing3             , "MsrLoadRing3"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_MsrLoadRsvd              , "MsrLoadRsvd"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_NmiWindowExit            , "NmiWindowExit"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_PinCtlsAllowed1          , "PinCtlsAllowed1"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_PinCtlsDisallowed0       , "PinCtlsDisallowed0"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_ProcCtlsAllowed1         , "ProcCtlsAllowed1"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_ProcCtlsDisallowed0      , "ProcCtlsDisallowed0"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_ProcCtls2Allowed1        , "ProcCtls2Allowed1"         ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_ProcCtls2Disallowed0     , "ProcCtls2Disallowed0"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_PtrInvalid               , "PtrInvalid"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_PtrShadowVmcs            , "PtrShadowVmcs"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_RealOrV86Mode            , "RealOrV86Mode"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_SavePreemptTimer         , "SavePreemptTimer"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_TprThresholdRsvd         , "TprThresholdRsvd"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_TprThresholdVTpr         , "TprThresholdVTpr"          ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VirtApicPagePtrReadPhys  , "VirtApicPageReadPhys"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VirtIntDelivery          , "VirtIntDelivery"           ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VirtNmi                  , "VirtNmi"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VirtX2ApicTprShadow      , "VirtX2ApicTprShadow"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VirtX2ApicVirtApic       , "VirtX2ApicVirtApic"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmcsClear                , "VmcsClear"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmcsLaunch               , "VmcsLaunch"                ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmreadBitmapPtrReadPhys  , "VmreadBitmapPtrReadPhys"   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmwriteBitmapPtrReadPhys , "VmwriteBitmapPtrReadPhys"  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_VmxRoot                  , "VmxRoot"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmentry_Vpid                     , "Vpid"                      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_HostPdpte                 , "HostPdpte"                 ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrLoad                   , "MsrLoad"                   ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrLoadCount              , "MsrLoadCount"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrLoadPtrReadPhys        , "MsrLoadPtrReadPhys"        ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrLoadRing3              , "MsrLoadRing3"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrLoadRsvd               , "MsrLoadRsvd"               ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrStore                  , "MsrStore"                  ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrStoreCount             , "MsrStoreCount"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrStorePtrReadPhys       , "MsrStorePtrReadPhys"       ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrStorePtrWritePhys      , "MsrStorePtrWritePhys"      ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrStoreRing3             , "MsrStoreRing3"             ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_MsrStoreRsvd              , "MsrStoreRsvd"              ),
    VMXV_DIAG_DESC(kVmxVDiag_Vmexit_VirtApicPagePtrWritePhys  , "VirtApicPagePtrWritePhys"  )
    /* kVmxVDiag_End */
};
AssertCompile(RT_ELEMENTS(g_apszVmxVDiagDesc) == kVmxVDiag_End);
#undef VMXV_DIAG_DESC


/**
 * Gets the descriptive name of a VMX instruction/VM-exit diagnostic code.
 *
 * @returns The descriptive string.
 * @param   enmDiag    The VMX diagnostic.
 */
VMM_INT_DECL(const char *) HMGetVmxDiagDesc(VMXVDIAG enmDiag)
{
    if (RT_LIKELY((unsigned)enmDiag < RT_ELEMENTS(g_apszVmxVDiagDesc)))
        return g_apszVmxVDiagDesc[enmDiag];
    return "Unknown/invalid";
}


/**
 * Checks if a code selector (CS) is suitable for execution using hardware-assisted
 * VMX when unrestricted execution isn't available.
 *
 * @returns true if selector is suitable for VMX, otherwise
 *        false.
 * @param   pSel        Pointer to the selector to check (CS).
 * @param   uStackDpl   The CPL, aka the DPL of the stack segment.
 */
static bool hmVmxIsCodeSelectorOk(PCCPUMSELREG pSel, unsigned uStackDpl)
{
    /*
     * Segment must be an accessed code segment, it must be present and it must
     * be usable.
     * Note! These are all standard requirements and if CS holds anything else
     *       we've got buggy code somewhere!
     */
    AssertCompile(X86DESCATTR_TYPE == 0xf);
    AssertMsgReturn(   (pSel->Attr.u & (X86_SEL_TYPE_ACCESSED | X86_SEL_TYPE_CODE | X86DESCATTR_DT | X86DESCATTR_P | X86DESCATTR_UNUSABLE))
                    ==                 (X86_SEL_TYPE_ACCESSED | X86_SEL_TYPE_CODE | X86DESCATTR_DT | X86DESCATTR_P),
                    ("%#x\n", pSel->Attr.u),
                    false);

    /*
     * For conforming segments, CS.DPL must be <= SS.DPL, while CS.DPL must equal
     * SS.DPL for non-confroming segments.
     * Note! This is also a hard requirement like above.
     */
    AssertMsgReturn(  pSel->Attr.n.u4Type & X86_SEL_TYPE_CONF
                    ? pSel->Attr.n.u2Dpl <= uStackDpl
                    : pSel->Attr.n.u2Dpl == uStackDpl,
                    ("u4Type=%#x u2Dpl=%u uStackDpl=%u\n", pSel->Attr.n.u4Type, pSel->Attr.n.u2Dpl, uStackDpl),
                    false);

    /*
     * The following two requirements are VT-x specific:
     *  - G bit must be set if any high limit bits are set.
     *  - G bit must be clear if any low limit bits are clear.
     */
    if (   ((pSel->u32Limit & 0xfff00000) == 0x00000000 ||  pSel->Attr.n.u1Granularity)
        && ((pSel->u32Limit & 0x00000fff) == 0x00000fff || !pSel->Attr.n.u1Granularity))
        return true;
    return false;
}


/**
 * Checks if a data selector (DS/ES/FS/GS) is suitable for execution using
 * hardware-assisted VMX when unrestricted execution isn't available.
 *
 * @returns true if selector is suitable for VMX, otherwise
 *        false.
 * @param   pSel        Pointer to the selector to check
 *                      (DS/ES/FS/GS).
 */
static bool hmVmxIsDataSelectorOk(PCCPUMSELREG pSel)
{
    /*
     * Unusable segments are OK.  These days they should be marked as such, as
     * but as an alternative we for old saved states and AMD<->VT-x migration
     * we also treat segments with all the attributes cleared as unusable.
     */
    if (pSel->Attr.n.u1Unusable || !pSel->Attr.u)
        return true;

    /** @todo tighten these checks. Will require CPUM load adjusting. */

    /* Segment must be accessed. */
    if (pSel->Attr.u & X86_SEL_TYPE_ACCESSED)
    {
        /* Code segments must also be readable. */
        if (  !(pSel->Attr.u & X86_SEL_TYPE_CODE)
            || (pSel->Attr.u & X86_SEL_TYPE_READ))
        {
            /* The S bit must be set. */
            if (pSel->Attr.n.u1DescType)
            {
                /* Except for conforming segments, DPL >= RPL. */
                if (   pSel->Attr.n.u2Dpl  >= (pSel->Sel & X86_SEL_RPL)
                    || pSel->Attr.n.u4Type >= X86_SEL_TYPE_ER_ACC)
                {
                    /* Segment must be present. */
                    if (pSel->Attr.n.u1Present)
                    {
                        /*
                         * The following two requirements are VT-x specific:
                         *   - G bit must be set if any high limit bits are set.
                         *   - G bit must be clear if any low limit bits are clear.
                         */
                        if (   ((pSel->u32Limit & 0xfff00000) == 0x00000000 ||  pSel->Attr.n.u1Granularity)
                            && ((pSel->u32Limit & 0x00000fff) == 0x00000fff || !pSel->Attr.n.u1Granularity))
                            return true;
                    }
                }
            }
        }
    }

    return false;
}


/**
 * Checks if the stack selector (SS) is suitable for execution using
 * hardware-assisted VMX when unrestricted execution isn't available.
 *
 * @returns true if selector is suitable for VMX, otherwise
 *        false.
 * @param   pSel        Pointer to the selector to check (SS).
 */
static bool hmVmxIsStackSelectorOk(PCCPUMSELREG pSel)
{
    /*
     * Unusable segments are OK.  These days they should be marked as such, as
     * but as an alternative we for old saved states and AMD<->VT-x migration
     * we also treat segments with all the attributes cleared as unusable.
     */
    /** @todo r=bird: actually all zeroes isn't gonna cut it... SS.DPL == CPL. */
    if (pSel->Attr.n.u1Unusable || !pSel->Attr.u)
        return true;

    /*
     * Segment must be an accessed writable segment, it must be present.
     * Note! These are all standard requirements and if SS holds anything else
     *       we've got buggy code somewhere!
     */
    AssertCompile(X86DESCATTR_TYPE == 0xf);
    AssertMsgReturn(   (pSel->Attr.u & (X86_SEL_TYPE_ACCESSED | X86_SEL_TYPE_WRITE | X86DESCATTR_DT | X86DESCATTR_P | X86_SEL_TYPE_CODE))
                    ==                 (X86_SEL_TYPE_ACCESSED | X86_SEL_TYPE_WRITE | X86DESCATTR_DT | X86DESCATTR_P),
                    ("%#x\n", pSel->Attr.u), false);

    /*
     * DPL must equal RPL. But in real mode or soon after enabling protected
     * mode, it might not be.
     */
    if (pSel->Attr.n.u2Dpl == (pSel->Sel & X86_SEL_RPL))
    {
        /*
         * The following two requirements are VT-x specific:
         *   - G bit must be set if any high limit bits are set.
         *   - G bit must be clear if any low limit bits are clear.
         */
        if (   ((pSel->u32Limit & 0xfff00000) == 0x00000000 ||  pSel->Attr.n.u1Granularity)
            && ((pSel->u32Limit & 0x00000fff) == 0x00000fff || !pSel->Attr.n.u1Granularity))
            return true;
    }
    return false;
}


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
/**
 * Checks if the CPU is subject to the "VMX-Preemption Timer Does Not Count Down at
 * the Rate Specified" erratum.
 *
 * Errata names and related steppings:
 *      - BA86   - D0.
 *      - AAX65  - C2.
 *      - AAU65  - C2, K0.
 *      - AAO95  - B1.
 *      - AAT59  - C2.
 *      - AAK139 - D0.
 *      - AAM126 - C0, C1, D0.
 *      - AAN92  - B1.
 *      - AAJ124 - C0, D0.
 *      - AAP86  - B1.
 *
 * Steppings: B1, C0, C1, C2, D0, K0.
 *
 * @returns @c true if subject to it, @c false if not.
 */
VMM_INT_DECL(bool) HMIsSubjectToVmxPreemptTimerErratum(void)
{
    uint32_t u = ASMCpuId_EAX(1);
    u &= ~(RT_BIT_32(14) | RT_BIT_32(15) | RT_BIT_32(28) | RT_BIT_32(29) | RT_BIT_32(30) | RT_BIT_32(31));
    if (   u == 0x000206E6 /* 323344.pdf - BA86   - D0 - Xeon Processor 7500 Series */
        || u == 0x00020652 /* 323056.pdf - AAX65  - C2 - Xeon Processor L3406 */
                           /* 322814.pdf - AAT59  - C2 - CoreTM i7-600, i5-500, i5-400 and i3-300 Mobile Processor Series */
                           /* 322911.pdf - AAU65  - C2 - CoreTM i5-600, i3-500 Desktop Processor Series and Intel Pentium Processor G6950 */
        || u == 0x00020655 /* 322911.pdf - AAU65  - K0 - CoreTM i5-600, i3-500 Desktop Processor Series and Intel Pentium Processor G6950 */
        || u == 0x000106E5 /* 322373.pdf - AAO95  - B1 - Xeon Processor 3400 Series */
                           /* 322166.pdf - AAN92  - B1 - CoreTM i7-800 and i5-700 Desktop Processor Series */
                           /* 320767.pdf - AAP86  - B1 - Core i7-900 Mobile Processor Extreme Edition Series, Intel Core i7-800 and i7-700 Mobile Processor Series */
        || u == 0x000106A0 /* 321333.pdf - AAM126 - C0 - Xeon Processor 3500 Series Specification */
        || u == 0x000106A1 /* 321333.pdf - AAM126 - C1 - Xeon Processor 3500 Series Specification */
        || u == 0x000106A4 /* 320836.pdf - AAJ124 - C0 - Core i7-900 Desktop Processor Extreme Edition Series and Intel Core i7-900 Desktop Processor Series */
        || u == 0x000106A5 /* 321333.pdf - AAM126 - D0 - Xeon Processor 3500 Series Specification */
                           /* 321324.pdf - AAK139 - D0 - Xeon Processor 5500 Series Specification */
                           /* 320836.pdf - AAJ124 - D0 - Core i7-900 Desktop Processor Extreme Edition Series and Intel Core i7-900 Desktop Processor Series */
        || u == 0x000306A8 /* ?????????? - ?????? - ?? - Xeon E3-1220 v2 */
        )
        return true;
    return false;
}
#endif


/**
 * Checks if the guest is in a suitable state for hardware-assisted VMX execution.
 *
 * @returns @c true if it is suitable, @c false otherwise.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pCtx    Pointer to the guest CPU context.
 *
 * @remarks @a pCtx can be a partial context and thus may not be necessarily the
 *          same as pVCpu->cpum.GstCtx! Thus don't eliminate the @a pCtx parameter.
 *          Secondly, if additional checks are added that require more of the CPU
 *          state, make sure REM (which supplies a partial state) is updated.
 */
VMM_INT_DECL(bool) HMCanExecuteVmxGuest(PVMCC pVM, PVMCPUCC pVCpu, PCCPUMCTX pCtx)
{
    Assert(HMIsEnabled(pVM));
    bool const fUnrestrictedGuest = CTX_EXPR(pVM->hm.s.vmx.fUnrestrictedGuestCfg, pVM->hmr0.s.vmx.fUnrestrictedGuest, RT_NOTHING);
    Assert(   ( fUnrestrictedGuest && !pVM->hm.s.vmx.pRealModeTSS)
           || (!fUnrestrictedGuest && pVM->hm.s.vmx.pRealModeTSS));

    pVCpu->hm.s.fActive = false;

    bool const fSupportsRealMode = fUnrestrictedGuest || PDMVmmDevHeapIsEnabled(pVM);
    if (!fUnrestrictedGuest)
    {
        /*
         * The VMM device heap is a requirement for emulating real mode or protected mode without paging with the unrestricted
         * guest execution feature is missing (VT-x only).
         */
        if (fSupportsRealMode)
        {
            if (CPUMIsGuestInRealModeEx(pCtx))
            {
                /*
                 * In V86 mode (VT-x or not), the CPU enforces real-mode compatible selector
                 * bases, limits, and attributes, i.e. limit must be 64K, base must be selector * 16,
                 * and attributes must be 0x9b for code and 0x93 for code segments.
                 * If this is not true, we cannot execute real mode as V86 and have to fall
                 * back to emulation.
                 */
                if (   pCtx->cs.Sel != (pCtx->cs.u64Base >> 4)
                    || pCtx->ds.Sel != (pCtx->ds.u64Base >> 4)
                    || pCtx->es.Sel != (pCtx->es.u64Base >> 4)
                    || pCtx->ss.Sel != (pCtx->ss.u64Base >> 4)
                    || pCtx->fs.Sel != (pCtx->fs.u64Base >> 4)
                    || pCtx->gs.Sel != (pCtx->gs.u64Base >> 4))
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadRmSelBase);
                    return false;
                }
                if (   (pCtx->cs.u32Limit != 0xffff)
                    || (pCtx->ds.u32Limit != 0xffff)
                    || (pCtx->es.u32Limit != 0xffff)
                    || (pCtx->ss.u32Limit != 0xffff)
                    || (pCtx->fs.u32Limit != 0xffff)
                    || (pCtx->gs.u32Limit != 0xffff))
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadRmSelLimit);
                    return false;
                }
                if (   (pCtx->cs.Attr.u != 0x9b)
                    || (pCtx->ds.Attr.u != 0x93)
                    || (pCtx->es.Attr.u != 0x93)
                    || (pCtx->ss.Attr.u != 0x93)
                    || (pCtx->fs.Attr.u != 0x93)
                    || (pCtx->gs.Attr.u != 0x93))
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadRmSelAttr);
                    return false;
                }
                STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckRmOk);
            }
            else
            {
                /*
                 * Verify the requirements for executing code in protected mode. VT-x can't
                 * handle the CPU state right after a switch from real to protected mode
                 * (all sorts of RPL & DPL assumptions).
                 */
                PCVMXVMCSINFOSHARED pVmcsInfo = hmGetVmxActiveVmcsInfoShared(pVCpu);
                if (pVmcsInfo->fWasInRealMode)
                {
                    if (!CPUMIsGuestInV86ModeEx(pCtx))
                    {
                        /* The guest switched to protected mode, check if the state is suitable for VT-x. */
                        if ((pCtx->cs.Sel & X86_SEL_RPL) != (pCtx->ss.Sel & X86_SEL_RPL))
                        {
                            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadRpl);
                            return false;
                        }
                        if (   !hmVmxIsCodeSelectorOk(&pCtx->cs, pCtx->ss.Attr.n.u2Dpl)
                            || !hmVmxIsDataSelectorOk(&pCtx->ds)
                            || !hmVmxIsDataSelectorOk(&pCtx->es)
                            || !hmVmxIsDataSelectorOk(&pCtx->fs)
                            || !hmVmxIsDataSelectorOk(&pCtx->gs)
                            || !hmVmxIsStackSelectorOk(&pCtx->ss))
                        {
                            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadSel);
                            return false;
                        }
                    }
                    else
                    {
                        /* The guest switched to V86 mode, check if the state is suitable for VT-x. */
                        if (   pCtx->cs.Sel != (pCtx->cs.u64Base >> 4)
                            || pCtx->ds.Sel != (pCtx->ds.u64Base >> 4)
                            || pCtx->es.Sel != (pCtx->es.u64Base >> 4)
                            || pCtx->ss.Sel != (pCtx->ss.u64Base >> 4)
                            || pCtx->fs.Sel != (pCtx->fs.u64Base >> 4)
                            || pCtx->gs.Sel != (pCtx->gs.u64Base >> 4))
                        {
                            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadV86SelBase);
                            return false;
                        }
                        if (   pCtx->cs.u32Limit != 0xffff
                            || pCtx->ds.u32Limit != 0xffff
                            || pCtx->es.u32Limit != 0xffff
                            || pCtx->ss.u32Limit != 0xffff
                            || pCtx->fs.u32Limit != 0xffff
                            || pCtx->gs.u32Limit != 0xffff)
                        {
                            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadV86SelLimit);
                            return false;
                        }
                        if (   pCtx->cs.Attr.u != 0xf3
                            || pCtx->ds.Attr.u != 0xf3
                            || pCtx->es.Attr.u != 0xf3
                            || pCtx->ss.Attr.u != 0xf3
                            || pCtx->fs.Attr.u != 0xf3
                            || pCtx->gs.Attr.u != 0xf3)
                        {
                            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckBadV86SelAttr);
                            return false;
                        }
                    }
                }
                STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckPmOk);
            }
        }
        else
        {
            if (!CPUMIsGuestInLongModeEx(pCtx))
            {
                if (/* Requires a fake PD for real *and* protected mode without paging - stored in the VMM device heap: */
                       !CTX_EXPR(pVM->hm.s.fNestedPagingCfg, pVM->hmr0.s.fNestedPaging, RT_NOTHING)
                    /* Requires a fake TSS for real mode - stored in the VMM device heap: */
                    || CPUMIsGuestInRealModeEx(pCtx))
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck1);
                    return false;
                }

                /* Too early for VT-x; Solaris guests will fail with a guru meditation otherwise; same for XP. */
                if (pCtx->idtr.pIdt == 0 || pCtx->idtr.cbIdt == 0 || pCtx->tr.Sel == 0)
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck1);
                    return false;
                }

                /*
                 * The guest is about to complete the switch to protected mode. Wait a bit longer.
                 * Windows XP; switch to protected mode; all selectors are marked not present
                 * in the hidden registers (possible recompiler bug; see load_seg_vm).
                 */
                /** @todo Is this supposed recompiler bug still relevant with IEM? */
                if (pCtx->cs.Attr.n.u1Present == 0)
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck1);
                    return false;
                }
                if (pCtx->ss.Attr.n.u1Present == 0)
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck1);
                    return false;
                }

                /*
                 * Windows XP: possible same as above, but new recompiler requires new
                 * heuristics? VT-x doesn't seem to like something about the guest state and
                 * this stuff avoids it.
                 */
                /** @todo This check is actually wrong, it doesn't take the direction of the
                 *        stack segment into account. But, it does the job for now. */
                if (pCtx->rsp >= pCtx->ss.u32Limit)
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck1);
                    return false;
                }
            }
        }
    }

    if (pVM->hm.s.vmx.fEnabled)
    {
        /* If bit N is set in cr0_fixed0, then it must be set in the guest's cr0. */
        uint32_t uCr0Mask = (uint32_t)CTX_EXPR(pVM->hm.s.ForR3.vmx.Msrs.u64Cr0Fixed0, g_HmMsrs.u.vmx.u64Cr0Fixed0, RT_NOTHING);

        /* We ignore the NE bit here on purpose; see HMR0.cpp for details. */
        uCr0Mask &= ~X86_CR0_NE;

        if (fSupportsRealMode)
        {
            /* We ignore the PE & PG bits here on purpose; we emulate real and protected mode without paging. */
            uCr0Mask &= ~(X86_CR0_PG | X86_CR0_PE);
        }
        else
        {
            /* We support protected mode without paging using identity mapping. */
            uCr0Mask &= ~X86_CR0_PG;
        }
        if ((pCtx->cr0 & uCr0Mask) != uCr0Mask)
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck2);
            return false;
        }

        /* If bit N is cleared in cr0_fixed1, then it must be zero in the guest's cr0. */
        uCr0Mask = (uint32_t)~CTX_EXPR(pVM->hm.s.ForR3.vmx.Msrs.u64Cr0Fixed1, g_HmMsrs.u.vmx.u64Cr0Fixed1, RT_NOTHING);
        if ((pCtx->cr0 & uCr0Mask) != 0)
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck2);
            return false;
        }

        /* If bit N is set in cr4_fixed0, then it must be set in the guest's cr4. */
        uCr0Mask  = (uint32_t)CTX_EXPR(pVM->hm.s.ForR3.vmx.Msrs.u64Cr4Fixed0, g_HmMsrs.u.vmx.u64Cr4Fixed0, RT_NOTHING);
        uCr0Mask &= ~X86_CR4_VMXE;
        if ((pCtx->cr4 & uCr0Mask) != uCr0Mask)
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck2);
            return false;
        }

        /* If bit N is cleared in cr4_fixed1, then it must be zero in the guest's cr4. */
        uCr0Mask = (uint32_t)~CTX_EXPR(pVM->hm.s.ForR3.vmx.Msrs.u64Cr4Fixed1, g_HmMsrs.u.vmx.u64Cr4Fixed1, RT_NOTHING);
        if ((pCtx->cr4 & uCr0Mask) != 0)
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheck2);
            return false;
        }

        pVCpu->hm.s.fActive = true;
        STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckOk);
        return true;
    }

    STAM_COUNTER_INC(&pVCpu->hm.s.StatVmxCheckDisabled);
    return false;
}


/**
 * Dumps the virtual VMCS state to the release log.
 *
 * This is a purely a convenience function to output to the release log because
 * cpumR3InfoVmxVmcs dumps only to the debug console and isn't always easy to use in
 * case of a crash.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMM_INT_DECL(void) HMDumpHwvirtVmxState(PVMCPU pVCpu)
{
    /* The string width of -4 used in the macros below to cover 'LDTR', 'GDTR', 'IDTR. */
#define HMVMX_DUMP_HOST_XDTR(a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        LogRel(("  %s%-4s                       = {base=%016RX64}\n", \
            (a_pszPrefix), (a_SegName), (a_pVmcs)->u64Host##a_Seg##Base.u)); \
    } while (0)
#define HMVMX_DUMP_HOST_FS_GS_TR(a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        LogRel(("  %s%-4s                       = {%04x base=%016RX64}\n", \
                (a_pszPrefix), (a_SegName), (a_pVmcs)->Host##a_Seg, (a_pVmcs)->u64Host##a_Seg##Base.u)); \
    } while (0)
#define HMVMX_DUMP_GUEST_SEGREG(a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        LogRel(("  %s%-4s                       = {%04x base=%016RX64 limit=%08x flags=%04x}\n", \
                (a_pszPrefix), (a_SegName), (a_pVmcs)->Guest##a_Seg, (a_pVmcs)->u64Guest##a_Seg##Base.u, \
                (a_pVmcs)->u32Guest##a_Seg##Limit, (a_pVmcs)->u32Guest##a_Seg##Attr)); \
    } while (0)
#define HMVMX_DUMP_GUEST_XDTR(a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        LogRel(("  %s%-4s                       = {base=%016RX64 limit=%08x}\n", \
                (a_pszPrefix), (a_SegName), (a_pVmcs)->u64Guest##a_Seg##Base.u, (a_pVmcs)->u32Guest##a_Seg##Limit)); \
    } while (0)

    PCCPUMCTX  const pCtx  = &pVCpu->cpum.GstCtx;
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    if (!pVmcs)
    {
        LogRel(("Virtual VMCS not allocated\n"));
        return;
    }
    LogRel(("GCPhysVmxon                = %#RGp\n",     pCtx->hwvirt.vmx.GCPhysVmxon));
    LogRel(("GCPhysVmcs                 = %#RGp\n",     pCtx->hwvirt.vmx.GCPhysVmcs));
    LogRel(("GCPhysShadowVmcs           = %#RGp\n",     pCtx->hwvirt.vmx.GCPhysShadowVmcs));
    LogRel(("enmDiag                    = %u (%s)\n",   pCtx->hwvirt.vmx.enmDiag, HMGetVmxDiagDesc(pCtx->hwvirt.vmx.enmDiag)));
    LogRel(("uDiagAux                   = %#RX64\n",    pCtx->hwvirt.vmx.uDiagAux));
    LogRel(("enmAbort                   = %u (%s)\n",   pCtx->hwvirt.vmx.enmAbort, VMXGetAbortDesc(pCtx->hwvirt.vmx.enmAbort)));
    LogRel(("uAbortAux                  = %u (%#x)\n",  pCtx->hwvirt.vmx.uAbortAux, pCtx->hwvirt.vmx.uAbortAux));
    LogRel(("fInVmxRootMode             = %RTbool\n",   pCtx->hwvirt.vmx.fInVmxRootMode));
    LogRel(("fInVmxNonRootMode          = %RTbool\n",   pCtx->hwvirt.vmx.fInVmxNonRootMode));
    LogRel(("fInterceptEvents           = %RTbool\n",   pCtx->hwvirt.vmx.fInterceptEvents));
    LogRel(("fNmiUnblockingIret         = %RTbool\n",   pCtx->hwvirt.vmx.fNmiUnblockingIret));
    LogRel(("uFirstPauseLoopTick        = %RX64\n",     pCtx->hwvirt.vmx.uFirstPauseLoopTick));
    LogRel(("uPrevPauseTick             = %RX64\n",     pCtx->hwvirt.vmx.uPrevPauseTick));
    LogRel(("uEntryTick                 = %RX64\n",     pCtx->hwvirt.vmx.uEntryTick));
    LogRel(("offVirtApicWrite           = %#RX16\n",    pCtx->hwvirt.vmx.offVirtApicWrite));
    LogRel(("fVirtNmiBlocking           = %RTbool\n",   pCtx->hwvirt.vmx.fVirtNmiBlocking));
    LogRel(("VMCS cache:\n"));

    const char *pszPrefix = "  ";
    /* Header. */
    {
        LogRel(("%sHeader:\n", pszPrefix));
        LogRel(("  %sVMCS revision id           = %#RX32\n",      pszPrefix, pVmcs->u32VmcsRevId));
        LogRel(("  %sVMX-abort id               = %#RX32 (%s)\n", pszPrefix, pVmcs->enmVmxAbort, VMXGetAbortDesc(pVmcs->enmVmxAbort)));
        LogRel(("  %sVMCS state                 = %#x (%s)\n",    pszPrefix, pVmcs->fVmcsState, VMXGetVmcsStateDesc(pVmcs->fVmcsState)));
    }

    /* Control fields. */
    {
        /* 16-bit. */
        LogRel(("%sControl:\n", pszPrefix));
        LogRel(("  %sVPID                       = %#RX16\n",   pszPrefix, pVmcs->u16Vpid));
        LogRel(("  %sPosted intr notify vector  = %#RX16\n",   pszPrefix, pVmcs->u16PostIntNotifyVector));
        LogRel(("  %sEPTP index                 = %#RX16\n",   pszPrefix, pVmcs->u16EptpIndex));

        /* 32-bit. */
        LogRel(("  %sPin ctls                   = %#RX32\n",   pszPrefix, pVmcs->u32PinCtls));
        LogRel(("  %sProcessor ctls             = %#RX32\n",   pszPrefix, pVmcs->u32ProcCtls));
        LogRel(("  %sSecondary processor ctls   = %#RX32\n",   pszPrefix, pVmcs->u32ProcCtls2));
        LogRel(("  %sVM-exit ctls               = %#RX32\n",   pszPrefix, pVmcs->u32ExitCtls));
        LogRel(("  %sVM-entry ctls              = %#RX32\n",   pszPrefix, pVmcs->u32EntryCtls));
        LogRel(("  %sException bitmap           = %#RX32\n",   pszPrefix, pVmcs->u32XcptBitmap));
        LogRel(("  %sPage-fault mask            = %#RX32\n",   pszPrefix, pVmcs->u32XcptPFMask));
        LogRel(("  %sPage-fault match           = %#RX32\n",   pszPrefix, pVmcs->u32XcptPFMatch));
        LogRel(("  %sCR3-target count           = %RU32\n",    pszPrefix, pVmcs->u32Cr3TargetCount));
        LogRel(("  %sVM-exit MSR store count    = %RU32\n",    pszPrefix, pVmcs->u32ExitMsrStoreCount));
        LogRel(("  %sVM-exit MSR load count     = %RU32\n",    pszPrefix, pVmcs->u32ExitMsrLoadCount));
        LogRel(("  %sVM-entry MSR load count    = %RU32\n",    pszPrefix, pVmcs->u32EntryMsrLoadCount));
        LogRel(("  %sVM-entry interruption info = %#RX32\n",   pszPrefix, pVmcs->u32EntryIntInfo));
        {
            uint32_t const fInfo = pVmcs->u32EntryIntInfo;
            uint8_t  const uType = VMX_ENTRY_INT_INFO_TYPE(fInfo);
            LogRel(("    %sValid                      = %RTbool\n",  pszPrefix, VMX_ENTRY_INT_INFO_IS_VALID(fInfo)));
            LogRel(("    %sType                       = %#x (%s)\n", pszPrefix, uType, VMXGetEntryIntInfoTypeDesc(uType)));
            LogRel(("    %sVector                     = %#x\n",      pszPrefix, VMX_ENTRY_INT_INFO_VECTOR(fInfo)));
            LogRel(("    %sNMI-unblocking-IRET        = %RTbool\n",  pszPrefix, VMX_ENTRY_INT_INFO_IS_NMI_UNBLOCK_IRET(fInfo)));
            LogRel(("    %sError-code valid           = %RTbool\n",  pszPrefix, VMX_ENTRY_INT_INFO_IS_ERROR_CODE_VALID(fInfo)));
        }
        LogRel(("  %sVM-entry xcpt error-code   = %#RX32\n",   pszPrefix, pVmcs->u32EntryXcptErrCode));
        LogRel(("  %sVM-entry instr length      = %u byte(s)\n", pszPrefix, pVmcs->u32EntryInstrLen));
        LogRel(("  %sTPR threshold              = %#RX32\n",   pszPrefix, pVmcs->u32TprThreshold));
        LogRel(("  %sPLE gap                    = %#RX32\n",   pszPrefix, pVmcs->u32PleGap));
        LogRel(("  %sPLE window                 = %#RX32\n",   pszPrefix, pVmcs->u32PleWindow));

        /* 64-bit. */
        LogRel(("  %sIO-bitmap A addr           = %#RX64\n",   pszPrefix, pVmcs->u64AddrIoBitmapA.u));
        LogRel(("  %sIO-bitmap B addr           = %#RX64\n",   pszPrefix, pVmcs->u64AddrIoBitmapB.u));
        LogRel(("  %sMSR-bitmap addr            = %#RX64\n",   pszPrefix, pVmcs->u64AddrMsrBitmap.u));
        LogRel(("  %sVM-exit MSR store addr     = %#RX64\n",   pszPrefix, pVmcs->u64AddrExitMsrStore.u));
        LogRel(("  %sVM-exit MSR load addr      = %#RX64\n",   pszPrefix, pVmcs->u64AddrExitMsrLoad.u));
        LogRel(("  %sVM-entry MSR load addr     = %#RX64\n",   pszPrefix, pVmcs->u64AddrEntryMsrLoad.u));
        LogRel(("  %sExecutive VMCS ptr         = %#RX64\n",   pszPrefix, pVmcs->u64ExecVmcsPtr.u));
        LogRel(("  %sPML addr                   = %#RX64\n",   pszPrefix, pVmcs->u64AddrPml.u));
        LogRel(("  %sTSC offset                 = %#RX64\n",   pszPrefix, pVmcs->u64TscOffset.u));
        LogRel(("  %sVirtual-APIC addr          = %#RX64\n",   pszPrefix, pVmcs->u64AddrVirtApic.u));
        LogRel(("  %sAPIC-access addr           = %#RX64\n",   pszPrefix, pVmcs->u64AddrApicAccess.u));
        LogRel(("  %sPosted-intr desc addr      = %#RX64\n",   pszPrefix, pVmcs->u64AddrPostedIntDesc.u));
        LogRel(("  %sVM-functions control       = %#RX64\n",   pszPrefix, pVmcs->u64VmFuncCtls.u));
        LogRel(("  %sEPTP ptr                   = %#RX64\n",   pszPrefix, pVmcs->u64EptPtr.u));
        LogRel(("  %sEOI-exit bitmap 0          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap0.u));
        LogRel(("  %sEOI-exit bitmap 1          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap1.u));
        LogRel(("  %sEOI-exit bitmap 2          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap2.u));
        LogRel(("  %sEOI-exit bitmap 3          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap3.u));
        LogRel(("  %sEPTP-list addr             = %#RX64\n",   pszPrefix, pVmcs->u64AddrEptpList.u));
        LogRel(("  %sVMREAD-bitmap addr         = %#RX64\n",   pszPrefix, pVmcs->u64AddrVmreadBitmap.u));
        LogRel(("  %sVMWRITE-bitmap addr        = %#RX64\n",   pszPrefix, pVmcs->u64AddrVmwriteBitmap.u));
        LogRel(("  %sVirt-Xcpt info addr        = %#RX64\n",   pszPrefix, pVmcs->u64AddrXcptVeInfo.u));
        LogRel(("  %sXSS-exiting bitmap         = %#RX64\n",   pszPrefix, pVmcs->u64XssExitBitmap.u));
        LogRel(("  %sENCLS-exiting bitmap       = %#RX64\n",   pszPrefix, pVmcs->u64EnclsExitBitmap.u));
        LogRel(("  %sSPP table pointer          = %#RX64\n",   pszPrefix, pVmcs->u64SppTablePtr.u));
        LogRel(("  %sTSC multiplier             = %#RX64\n",   pszPrefix, pVmcs->u64TscMultiplier.u));
        LogRel(("  %sENCLV-exiting bitmap       = %#RX64\n",   pszPrefix, pVmcs->u64EnclvExitBitmap.u));

        /* Natural width. */
        LogRel(("  %sCR0 guest/host mask        = %#RX64\n",   pszPrefix, pVmcs->u64Cr0Mask.u));
        LogRel(("  %sCR4 guest/host mask        = %#RX64\n",   pszPrefix, pVmcs->u64Cr4Mask.u));
        LogRel(("  %sCR0 read shadow            = %#RX64\n",   pszPrefix, pVmcs->u64Cr0ReadShadow.u));
        LogRel(("  %sCR4 read shadow            = %#RX64\n",   pszPrefix, pVmcs->u64Cr4ReadShadow.u));
        LogRel(("  %sCR3-target 0               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target0.u));
        LogRel(("  %sCR3-target 1               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target1.u));
        LogRel(("  %sCR3-target 2               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target2.u));
        LogRel(("  %sCR3-target 3               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target3.u));
    }

    /* Guest state. */
    {
        LogRel(("%sGuest state:\n", pszPrefix));

        /* 16-bit. */
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Cs,   "cs",   pszPrefix);
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Ss,   "ss",   pszPrefix);
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Es,   "es",   pszPrefix);
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Ds,   "ds",   pszPrefix);
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Fs,   "fs",   pszPrefix);
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Gs,   "gs",   pszPrefix);
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Ldtr, "ldtr", pszPrefix);
        HMVMX_DUMP_GUEST_SEGREG(pVmcs, Tr,   "tr",   pszPrefix);
        HMVMX_DUMP_GUEST_XDTR(  pVmcs, Gdtr, "gdtr", pszPrefix);
        HMVMX_DUMP_GUEST_XDTR(  pVmcs, Idtr, "idtr", pszPrefix);
        LogRel(("  %sInterrupt status           = %#RX16\n",   pszPrefix, pVmcs->u16GuestIntStatus));
        LogRel(("  %sPML index                  = %#RX16\n",   pszPrefix, pVmcs->u16PmlIndex));

        /* 32-bit. */
        LogRel(("  %sInterruptibility state     = %#RX32\n",   pszPrefix, pVmcs->u32GuestIntrState));
        LogRel(("  %sActivity state             = %#RX32\n",   pszPrefix, pVmcs->u32GuestActivityState));
        LogRel(("  %sSMBASE                     = %#RX32\n",   pszPrefix, pVmcs->u32GuestSmBase));
        LogRel(("  %sSysEnter CS                = %#RX32\n",   pszPrefix, pVmcs->u32GuestSysenterCS));
        LogRel(("  %sVMX-preemption timer value = %#RX32\n",   pszPrefix, pVmcs->u32PreemptTimer));

        /* 64-bit. */
        LogRel(("  %sVMCS link ptr              = %#RX64\n",   pszPrefix, pVmcs->u64VmcsLinkPtr.u));
        LogRel(("  %sDBGCTL                     = %#RX64\n",   pszPrefix, pVmcs->u64GuestDebugCtlMsr.u));
        LogRel(("  %sPAT                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestPatMsr.u));
        LogRel(("  %sEFER                       = %#RX64\n",   pszPrefix, pVmcs->u64GuestEferMsr.u));
        LogRel(("  %sPERFGLOBALCTRL             = %#RX64\n",   pszPrefix, pVmcs->u64GuestPerfGlobalCtlMsr.u));
        LogRel(("  %sPDPTE 0                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte0.u));
        LogRel(("  %sPDPTE 1                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte1.u));
        LogRel(("  %sPDPTE 2                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte2.u));
        LogRel(("  %sPDPTE 3                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte3.u));
        LogRel(("  %sBNDCFGS                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestBndcfgsMsr.u));
        LogRel(("  %sRTIT_CTL                   = %#RX64\n",   pszPrefix, pVmcs->u64GuestRtitCtlMsr.u));

        /* Natural width. */
        LogRel(("  %scr0                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestCr0.u));
        LogRel(("  %scr3                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestCr3.u));
        LogRel(("  %scr4                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestCr4.u));
        LogRel(("  %sdr7                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestDr7.u));
        LogRel(("  %srsp                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestRsp.u));
        LogRel(("  %srip                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestRip.u));
        LogRel(("  %srflags                     = %#RX64\n",   pszPrefix, pVmcs->u64GuestRFlags.u));
        LogRel(("  %sPending debug xcpts        = %#RX64\n",   pszPrefix, pVmcs->u64GuestPendingDbgXcpts.u));
        LogRel(("  %sSysEnter ESP               = %#RX64\n",   pszPrefix, pVmcs->u64GuestSysenterEsp.u));
        LogRel(("  %sSysEnter EIP               = %#RX64\n",   pszPrefix, pVmcs->u64GuestSysenterEip.u));
    }

    /* Host state. */
    {
        LogRel(("%sHost state:\n", pszPrefix));

        /* 16-bit. */
        LogRel(("  %scs                         = %#RX16\n",   pszPrefix, pVmcs->HostCs));
        LogRel(("  %sss                         = %#RX16\n",   pszPrefix, pVmcs->HostSs));
        LogRel(("  %sds                         = %#RX16\n",   pszPrefix, pVmcs->HostDs));
        LogRel(("  %ses                         = %#RX16\n",   pszPrefix, pVmcs->HostEs));
        HMVMX_DUMP_HOST_FS_GS_TR(pVmcs, Fs, "fs", pszPrefix);
        HMVMX_DUMP_HOST_FS_GS_TR(pVmcs, Gs, "gs", pszPrefix);
        HMVMX_DUMP_HOST_FS_GS_TR(pVmcs, Tr, "tr", pszPrefix);
        HMVMX_DUMP_HOST_XDTR(pVmcs, Gdtr, "gdtr", pszPrefix);
        HMVMX_DUMP_HOST_XDTR(pVmcs, Idtr, "idtr", pszPrefix);

        /* 32-bit. */
        LogRel(("  %sSysEnter CS                = %#RX32\n",   pszPrefix, pVmcs->u32HostSysenterCs));

        /* 64-bit. */
        LogRel(("  %sEFER                       = %#RX64\n",   pszPrefix, pVmcs->u64HostEferMsr.u));
        LogRel(("  %sPAT                        = %#RX64\n",   pszPrefix, pVmcs->u64HostPatMsr.u));
        LogRel(("  %sPERFGLOBALCTRL             = %#RX64\n",   pszPrefix, pVmcs->u64HostPerfGlobalCtlMsr.u));

        /* Natural width. */
        LogRel(("  %scr0                        = %#RX64\n",   pszPrefix, pVmcs->u64HostCr0.u));
        LogRel(("  %scr3                        = %#RX64\n",   pszPrefix, pVmcs->u64HostCr3.u));
        LogRel(("  %scr4                        = %#RX64\n",   pszPrefix, pVmcs->u64HostCr4.u));
        LogRel(("  %sSysEnter ESP               = %#RX64\n",   pszPrefix, pVmcs->u64HostSysenterEsp.u));
        LogRel(("  %sSysEnter EIP               = %#RX64\n",   pszPrefix, pVmcs->u64HostSysenterEip.u));
        LogRel(("  %srsp                        = %#RX64\n",   pszPrefix, pVmcs->u64HostRsp.u));
        LogRel(("  %srip                        = %#RX64\n",   pszPrefix, pVmcs->u64HostRip.u));
    }

    /* Read-only fields. */
    {
        LogRel(("%sRead-only data fields:\n", pszPrefix));

        /* 16-bit (none currently). */

        /* 32-bit. */
        uint32_t const uExitReason = pVmcs->u32RoExitReason;
        LogRel(("  %sExit reason                = %u (%s)\n",  pszPrefix, uExitReason, HMGetVmxExitName(uExitReason)));
        LogRel(("  %sExit qualification         = %#RX64\n",   pszPrefix, pVmcs->u64RoExitQual.u));
        LogRel(("  %sVM-instruction error       = %#RX32\n",   pszPrefix, pVmcs->u32RoVmInstrError));
        LogRel(("  %sVM-exit intr info          = %#RX32\n",   pszPrefix, pVmcs->u32RoExitIntInfo));
        {
            uint32_t const fInfo = pVmcs->u32RoExitIntInfo;
            uint8_t  const uType = VMX_EXIT_INT_INFO_TYPE(fInfo);
            LogRel(("    %sValid                      = %RTbool\n",  pszPrefix, VMX_EXIT_INT_INFO_IS_VALID(fInfo)));
            LogRel(("    %sType                       = %#x (%s)\n", pszPrefix, uType, VMXGetExitIntInfoTypeDesc(uType)));
            LogRel(("    %sVector                     = %#x\n",      pszPrefix, VMX_EXIT_INT_INFO_VECTOR(fInfo)));
            LogRel(("    %sNMI-unblocking-IRET        = %RTbool\n",  pszPrefix, VMX_EXIT_INT_INFO_IS_NMI_UNBLOCK_IRET(fInfo)));
            LogRel(("    %sError-code valid           = %RTbool\n",  pszPrefix, VMX_EXIT_INT_INFO_IS_ERROR_CODE_VALID(fInfo)));
        }
        LogRel(("  %sVM-exit intr error-code    = %#RX32\n",   pszPrefix, pVmcs->u32RoExitIntErrCode));
        LogRel(("  %sIDT-vectoring info         = %#RX32\n",   pszPrefix, pVmcs->u32RoIdtVectoringInfo));
        {
            uint32_t const fInfo = pVmcs->u32RoIdtVectoringInfo;
            uint8_t  const uType = VMX_IDT_VECTORING_INFO_TYPE(fInfo);
            LogRel(("    %sValid                      = %RTbool\n",  pszPrefix, VMX_IDT_VECTORING_INFO_IS_VALID(fInfo)));
            LogRel(("    %sType                       = %#x (%s)\n", pszPrefix, uType, VMXGetIdtVectoringInfoTypeDesc(uType)));
            LogRel(("    %sVector                     = %#x\n",      pszPrefix, VMX_IDT_VECTORING_INFO_VECTOR(fInfo)));
            LogRel(("    %sError-code valid           = %RTbool\n",  pszPrefix, VMX_IDT_VECTORING_INFO_IS_ERROR_CODE_VALID(fInfo)));
        }
        LogRel(("  %sIDT-vectoring error-code   = %#RX32\n",   pszPrefix, pVmcs->u32RoIdtVectoringErrCode));
        LogRel(("  %sVM-exit instruction length = %u bytes\n", pszPrefix, pVmcs->u32RoExitInstrLen));
        LogRel(("  %sVM-exit instruction info   = %#RX64\n",   pszPrefix, pVmcs->u32RoExitInstrInfo));

        /* 64-bit. */
        LogRel(("  %sGuest-physical addr        = %#RX64\n",   pszPrefix, pVmcs->u64RoGuestPhysAddr.u));

        /* Natural width. */
        LogRel(("  %sI/O RCX                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRcx.u));
        LogRel(("  %sI/O RSI                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRsi.u));
        LogRel(("  %sI/O RDI                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRdi.u));
        LogRel(("  %sI/O RIP                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRip.u));
        LogRel(("  %sGuest-linear addr          = %#RX64\n",   pszPrefix, pVmcs->u64RoGuestLinearAddr.u));
    }

#undef HMVMX_DUMP_HOST_XDTR
#undef HMVMX_DUMP_HOST_FS_GS_TR
#undef HMVMX_DUMP_GUEST_SEGREG
#undef HMVMX_DUMP_GUEST_XDTR
}


/**
 * Gets the active (in use) VMCS info. object for the specified VCPU.
 *
 * This is either the guest or nested-guest VMCS info. and need not necessarily
 * pertain to the "current" VMCS (in the VMX definition of the term). For instance,
 * if the VM-entry failed due to an invalid-guest state, we may have "cleared" the
 * current VMCS while returning to ring-3. However, the VMCS info. object for that
 * VMCS would still be active and returned here so that we could dump the VMCS
 * fields to ring-3 for diagnostics. This function is thus only used to
 * distinguish between the nested-guest or guest VMCS.
 *
 * @returns The active VMCS information.
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @thread  EMT.
 * @remarks This function may be called with preemption or interrupts disabled!
 */
VMM_INT_DECL(PVMXVMCSINFOSHARED) hmGetVmxActiveVmcsInfoShared(PVMCPUCC pVCpu)
{
#ifdef IN_RING0
    if (!pVCpu->hmr0.s.vmx.fSwitchedToNstGstVmcs)
#else
    if (!pVCpu->hm.s.vmx.fSwitchedToNstGstVmcsCopyForRing3)
#endif
        return &pVCpu->hm.s.vmx.VmcsInfo;
    return &pVCpu->hm.s.vmx.VmcsInfoNstGst;
}


/**
 * Converts a VMX event type into an appropriate TRPM event type.
 *
 * @returns TRPM event.
 * @param   uIntInfo    The VMX event.
 */
VMM_INT_DECL(TRPMEVENT) HMVmxEventTypeToTrpmEventType(uint32_t uIntInfo)
{
    Assert(VMX_IDT_VECTORING_INFO_IS_VALID(uIntInfo));

    TRPMEVENT enmTrapType;
    uint8_t const uType   = VMX_IDT_VECTORING_INFO_TYPE(uIntInfo);
    uint8_t const uVector = VMX_IDT_VECTORING_INFO_VECTOR(uIntInfo);

    switch (uType)
    {
        case VMX_IDT_VECTORING_INFO_TYPE_EXT_INT:
           enmTrapType = TRPM_HARDWARE_INT;
           break;

        case VMX_IDT_VECTORING_INFO_TYPE_NMI:
            Assert(uVector == X86_XCPT_NMI); NOREF(uVector);
            enmTrapType = TRPM_NMI;
            break;

        case VMX_IDT_VECTORING_INFO_TYPE_HW_XCPT:
            enmTrapType = TRPM_TRAP;
            break;

        case VMX_IDT_VECTORING_INFO_TYPE_PRIV_SW_XCPT:  /* INT1 (ICEBP). */
            Assert(uVector == X86_XCPT_DB); NOREF(uVector);
            enmTrapType = TRPM_SOFTWARE_INT;
            break;

        case VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT:       /* INT3 (#BP) and INTO (#OF) */
            Assert(uVector == X86_XCPT_BP || uVector == X86_XCPT_OF); NOREF(uVector);
            enmTrapType = TRPM_SOFTWARE_INT;
            break;

        case VMX_IDT_VECTORING_INFO_TYPE_SW_INT:
            enmTrapType = TRPM_SOFTWARE_INT;
            break;

        default:
            AssertMsgFailed(("Invalid trap type %#x\n", uType));
            enmTrapType = TRPM_32BIT_HACK;
            break;
    }

    return enmTrapType;
}


/**
 * Converts a TRPM event type into an appropriate VMX event type.
 *
 * @returns VMX event type mask.
 * @param   uVector         The event vector.
 * @param   enmTrpmEvent    The TRPM event.
 * @param   fIcebp          Whether the \#DB vector is caused by an INT1/ICEBP
 *                          instruction.
 */
VMM_INT_DECL(uint32_t) HMTrpmEventTypeToVmxEventType(uint8_t uVector, TRPMEVENT enmTrpmEvent, bool fIcebp)
{
    uint32_t uIntInfoType = 0;
    if (enmTrpmEvent == TRPM_TRAP)
    {
        Assert(!fIcebp);
        switch (uVector)
        {
            case X86_XCPT_BP:
            case X86_XCPT_OF:
                uIntInfoType |= (VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT << VMX_IDT_VECTORING_INFO_TYPE_SHIFT);
                break;

            case X86_XCPT_PF:
            case X86_XCPT_DF:
            case X86_XCPT_TS:
            case X86_XCPT_NP:
            case X86_XCPT_SS:
            case X86_XCPT_GP:
            case X86_XCPT_AC:
                uIntInfoType |= VMX_IDT_VECTORING_INFO_ERROR_CODE_VALID;
                RT_FALL_THRU();
            default:
                uIntInfoType |= (VMX_IDT_VECTORING_INFO_TYPE_HW_XCPT << VMX_IDT_VECTORING_INFO_TYPE_SHIFT);
                break;
        }
    }
    else if (enmTrpmEvent == TRPM_HARDWARE_INT)
    {
        Assert(!fIcebp);
        uIntInfoType |= (VMX_IDT_VECTORING_INFO_TYPE_EXT_INT << VMX_IDT_VECTORING_INFO_TYPE_SHIFT);
    }
    else if (enmTrpmEvent == TRPM_SOFTWARE_INT)
    {
        switch (uVector)
        {
            case X86_XCPT_BP:
            case X86_XCPT_OF:
                uIntInfoType |= (VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT << VMX_IDT_VECTORING_INFO_TYPE_SHIFT);
                break;

            case X86_XCPT_DB:
            {
                if (fIcebp)
                {
                    uIntInfoType |= (VMX_IDT_VECTORING_INFO_TYPE_PRIV_SW_XCPT << VMX_IDT_VECTORING_INFO_TYPE_SHIFT);
                    break;
                }
                RT_FALL_THRU();
            }
            default:
                uIntInfoType |= (VMX_IDT_VECTORING_INFO_TYPE_SW_INT << VMX_IDT_VECTORING_INFO_TYPE_SHIFT);
                break;
        }
    }
    else if (enmTrpmEvent == TRPM_NMI)
        uIntInfoType |= (VMX_IDT_VECTORING_INFO_TYPE_NMI << VMX_IDT_VECTORING_INFO_TYPE_SHIFT);
    else
        AssertMsgFailed(("Invalid TRPM event type %d\n", enmTrpmEvent));
    return uIntInfoType;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Notification callback for when a VM-exit happens outside VMX R0 code (e.g. in
 * IEM).
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks Can be called from ring-0 as well as ring-3.
 */
VMM_INT_DECL(void) HMNotifyVmxNstGstVmexit(PVMCPU pVCpu)
{
    LogFlowFunc(("\n"));

    /*
     * Transitions to ring-3 flag a full CPU-state change except if we transition to ring-3
     * in response to a physical CPU interrupt as no changes to the guest-CPU state are
     * expected (see VINF_EM_RAW_INTERRUPT handling in hmR0VmxExitToRing3).
     *
     * However, with nested-guests, the state -can- change on trips to ring-3 for we might
     * try to inject a nested-guest physical interrupt and cause a VMX_EXIT_EXT_INT VM-exit
     * for the nested-guest from ring-3.
     *
     * Signalling reload of just the guest-CPU state that changed with the VM-exit is -not-
     * sufficient since HM also needs to reload state related to VM-entry/VM-exit controls
     * etc. So signal reloading of the entire state. It does not seem worth making this any
     * more fine grained at the moment.
     */
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_ALL);
    ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST);

    /*
     * Make sure we need to merge the guest VMCS controls with the nested-guest
     * VMCS controls on the next nested-guest VM-entry.
     */
    pVCpu->hm.s.vmx.fMergedNstGstCtls = false;

    /*
     * Flush the TLB before entering the outer guest execution (mainly required since the
     * APIC-access guest-physical address would have changed and probably more things in
     * the future).
     */
    pVCpu->hm.s.vmx.fSwitchedNstGstFlushTlb = true;

    /** @todo Handle releasing of the page-mapping lock later. */
#if 0
    if (pVCpu->hm.s.vmx.fVirtApicPageLocked)
    {
        PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), &pVCpu->hm.s.vmx.PgMapLockVirtApic);
        pVCpu->hm.s.vmx.fVirtApicPageLocked = false;
    }
#endif
}


/**
 * Notification callback for when the nested hypervisor's current VMCS is loaded or
 * changed outside VMX R0 code (e.g. in IEM).
 *
 * This need -not- be called for modifications to the nested hypervisor's current
 * VMCS when the guest is in VMX non-root mode as VMCS shadowing is not applicable
 * there.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks Can be called from ring-0 as well as ring-3.
 */
VMM_INT_DECL(void) HMNotifyVmxNstGstCurrentVmcsChanged(PVMCPU pVCpu)
{
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_HWVIRT);
    ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, CPUMCTX_EXTRN_HWVIRT);

    /*
     * Make sure we need to copy the nested hypervisor's current VMCS into the shadow VMCS
     * on the next guest VM-entry.
     */
    pVCpu->hm.s.vmx.fCopiedNstGstToShadowVmcs = false;
}

#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */

