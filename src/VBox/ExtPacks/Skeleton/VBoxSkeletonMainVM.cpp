/* $Id: VBoxSkeletonMainVM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Skeleton main VM module.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/ExtPack/ExtPack.h>

#include <iprt/errcore.h>
#include <VBox/version.h>
#include <VBox/vmm/cfgm.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/path.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the extension pack helpers. */
static PCVBOXEXTPACKHLP g_pHlp;


// /**
//  * @interface_method_impl{VBOXEXTPACKVMREG,pfnConsoleReady}
//  */
// static DECLCALLBACK(void)  vboxSkeletonExtPackVM_ConsoleReady(PCVBOXEXTPACKVMREG pThis, VBOXEXTPACK_IF_CS(IConsole) *pConsole);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKVMREG,pfnUnload}
//  */
// static DECLCALLBACK(void) vboxSkeletonExtPackVM_Unload(PCVBOXEXTPACKVMREG pThis);
//
//  * @interface_method_impl{VBOXEXTPACKVMREG,pfnVMConfigureVMM}
//  */
// static DECLCALLBACK(int)  vboxSkeletonExtPackVM_VMConfigureVMM(PCVBOXEXTPACKVMREG pThis, VBOXEXTPACK_IF_CS(IConsole) *pConsole, PVM pVM);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKVMREG,pfnVMPowerOn}
//  */
// static DECLCALLBACK(int)  vboxSkeletonExtPackVM_VMPowerOn(PCVBOXEXTPACKVMREG pThis, VBOXEXTPACK_IF_CS(IConsole) *pConsole, PVM pVM);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKVMREG,pfnVMPowerOff}
//  */
// static DECLCALLBACK(void) vboxSkeletonExtPackVM_VMPowerOff(PCVBOXEXTPACKVMREG pThis, VBOXEXTPACK_IF_CS(IConsole) *pConsole, PVM pVM);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKVMREG,pfnQueryObject}
//  */
// static DECLCALLBACK(void) vboxSkeletonExtPackVM_QueryObject(PCVBOXEXTPACKVMREG pThis, PCRTUUID pObjectId);


static const VBOXEXTPACKVMREG g_vboxSkeletonExtPackVMReg =
{
    VBOXEXTPACKVMREG_VERSION,
    /* .uVBoxFullVersion =  */  VBOX_FULL_VERSION,
    /* .pszNlsBaseName =    */  NULL,
    /* .pfnConsoleReady =   */  NULL,
    /* .pfnUnload =         */  NULL,
    /* .pfnVMConfigureVMM = */  NULL,
    /* .pfnVMPowerOn =      */  NULL,
    /* .pfnVMPowerOff =     */  NULL,
    /* .pfnQueryObject =    */  NULL,
    /* .pfnReserved1 =      */  NULL,
    /* .pfnReserved2 =      */  NULL,
    /* .pfnReserved3 =      */  NULL,
    /* .pfnReserved4 =      */  NULL,
    /* .pfnReserved5 =      */  NULL,
    /* .pfnReserved6 =      */  NULL,
    /* .uReserved7 =        */  0,
    VBOXEXTPACKVMREG_VERSION
};


/** @callback_method_impl{FNVBOXEXTPACKVMREGISTER}  */
extern "C" DECLEXPORT(int) VBoxExtPackVMRegister(PCVBOXEXTPACKHLP pHlp, PCVBOXEXTPACKVMREG *ppReg, PRTERRINFO pErrInfo)
{
    /*
     * Check the VirtualBox version.
     */
    if (!VBOXEXTPACK_IS_VER_COMPAT(pHlp->u32Version, VBOXEXTPACKHLP_VERSION))
        return RTErrInfoSetF(pErrInfo, VERR_VERSION_MISMATCH,
                             "Helper version mismatch - expected %#x got %#x",
                             VBOXEXTPACKHLP_VERSION, pHlp->u32Version);
    if (   VBOX_FULL_VERSION_GET_MAJOR(pHlp->uVBoxFullVersion) != VBOX_VERSION_MAJOR
        || VBOX_FULL_VERSION_GET_MINOR(pHlp->uVBoxFullVersion) != VBOX_VERSION_MINOR)
        return RTErrInfoSetF(pErrInfo, VERR_VERSION_MISMATCH,
                             "VirtualBox version mismatch - expected %u.%u got %u.%u",
                             VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR,
                             VBOX_FULL_VERSION_GET_MAJOR(pHlp->uVBoxFullVersion),
                             VBOX_FULL_VERSION_GET_MINOR(pHlp->uVBoxFullVersion));

    /*
     * We're good, save input and return the registration structure.
     */
    g_pHlp = pHlp;
    *ppReg = &g_vboxSkeletonExtPackVMReg;

    return VINF_SUCCESS;
}

