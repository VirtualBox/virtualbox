/* $Id: VBoxSFInit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - OS/2 Shared Folders, Initialization.
 */

/*
 * Copyright (c) 2007-2018 knut st. osmundsen <bird-src-spam@anduin.net>
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
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT
#include "VBoxSFInternal.h"

#include <VBox/VBoxGuestLib.h>
#include <VBox/VBoxGuest.h>
#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
/* from VBoxSFA.asm */
extern RTFAR16 g_fpfnDevHlp;
extern VBGLOS2ATTACHDD g_VBoxGuestIDC;
extern uint32_t g_u32Info;
/* from sys0.asm and the linker/end.lib. */
extern char _text, _etext, _data, _end;
RT_C_DECLS_END



/**
 * 32-bit Ring-0 init routine.
 *
 * This is called the first time somebody tries to use the IFS.
 * It will initialize IPRT, Vbgl and whatever else is required.
 *
 * The caller will do the necessary AttachDD and calling of the 16 bit
 * IDC to initialize the g_VBoxGuestIDC global. Perhaps we should move
 * this bit to VbglR0InitClient? It's just that it's so much simpler to do it
 * while we're on the way here...
 *
 */
DECLASM(void) VBoxSFR0Init(void)
{
    RTLogBackdoorPrintf("VBoxSFR0Init: version " VBOX_VERSION_STRING " r" RT_XSTR(VBOX_SVN_REV));
    RTLogBackdoorPrintf("VBoxSFR0Init: g_fpfnDevHlp=%lx u32Version=%RX32 u32Session=%RX32 pfnServiceEP=%p g_u32Info=%u (%#x)\n",
                        g_fpfnDevHlp, g_VBoxGuestIDC.u32Version, g_VBoxGuestIDC.u32Session, g_VBoxGuestIDC.pfnServiceEP, g_u32Info, g_u32Info);
    //RTLogBackdoorPrintf("&KernSISData=%p\n",        &KernSISData);
    //RTLogBackdoorPrintf("&KernLISData=%p\n",        &KernLISData);
    //RTLogBackdoorPrintf("KernInterruptLevel=%#x\n", KernInterruptLevel);
    //RTLogBackdoorPrintf("KernTKSSBase=%p\n",        KernTKSSBase);

    KernAllocMutexLock(&g_MtxFolders);
    RTListInit(&g_FolderHead);

    /*
     * Start by initializing IPRT.
     */
    if (    g_VBoxGuestIDC.u32Version == VBGL_IOC_VERSION
        &&  RT_VALID_PTR(g_VBoxGuestIDC.u32Session)
        &&  RT_VALID_PTR(g_VBoxGuestIDC.pfnServiceEP))
    {
        int rc = RTR0Init(0);
        if (RT_SUCCESS(rc))
        {
            rc = VbglR0InitClient();
            if (RT_SUCCESS(rc))
            {
                /*
                 * Complain if the embedded buffers feature is missing.
                 */
                uint32_t fFeatures = 0;
                rc = VbglR0QueryHostFeatures(&fFeatures);
                if (RT_FAILURE(rc))
                    RTLogBackdoorPrintf("VBoxSFR0Init: Missing VBoxGuest.sys IDC connection!  Check order in Config.kmk!\n");
                else
                {
                    g_fHostFeatures = fFeatures;
                    if (!(fFeatures & VMMDEV_HVF_HGCM_EMBEDDED_BUFFERS))
                        RTLogBackdoorPrintf("VBoxSFR0Init: WARNING! Embedded buffers feature is missing.  Upgrade to latest VirtualBox!\n");
                    if (!(fFeatures & VMMDEV_HVF_HGCM_CONTIGUOUS_PAGE_LIST))
                        RTLogBackdoorPrintf("VBoxSFR0Init: WARNING! Contiguous page list buffers feature is missing.  Upgrade to latest VirtualBox!\n");
                }

                /*
                 * Allocate some big buffers for reading and writing.
                 */
                vboxSfOs2InitFileBuffers();

#ifndef DONT_LOCK_SEGMENTS
                /*
                 * Lock the 32-bit segments in memory.
                 */
                static KernVMLock_t s_Text32, s_Data32;
                rc = KernVMLock(VMDHL_LONG,
                                &_text, (uintptr_t)&_etext - (uintptr_t)&_text,
                                &s_Text32, (KernPageList_t *)-1, NULL);
                AssertMsg(rc == NO_ERROR, ("locking text32 failed, rc=%d\n"));
                rc = KernVMLock(VMDHL_LONG | VMDHL_WRITE,
                                &_data, (uintptr_t)&_end - (uintptr_t)&_data,
                                &s_Data32, (KernPageList_t *)-1, NULL);
                AssertMsg(rc == NO_ERROR, ("locking text32 failed, rc=%d\n"));
#endif

                RTLogBackdoorPrintf("VBoxSFR0Init: completed successfully\n");
                return;
            }
        }

        RTLogBackdoorPrintf("VBoxSF: RTR0Init failed, rc=%Rrc\n", rc);
    }
    else
        RTLogBackdoorPrintf("VBoxSF: Failed to connect to VBoxGuest.sys.\n");
}

