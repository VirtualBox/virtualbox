/* $Id: PDMLdr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device Manager, module loader.
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

//#define PDMLDR_FAKE_MODE


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_LDR
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/sup.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/vmm/hm.h>
#include <VBox/VBoxTpG.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include <limits.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Structure which the user argument of the RTLdrGetBits() callback points to.
 * @internal
 */
typedef struct PDMGETIMPORTARGS
{
    PVM         pVM;
    PPDMMOD     pModule;
} PDMGETIMPORTARGS, *PPDMGETIMPORTARGS;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_RAW_MODE_KEEP
static DECLCALLBACK(int) pdmR3GetImportRC(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol, unsigned uSymbol, RTUINTPTR *pValue, void *pvUser);
static char    *pdmR3FileRC(const char *pszFile, const char *pszSearchPath);
#endif
static int      pdmR3LoadR0U(PUVM pUVM, const char *pszFilename, const char *pszName, const char *pszSearchPath);
static char    *pdmR3FileR0(const char *pszFile, const char *pszSearchPath);
static char    *pdmR3File(const char *pszFile, const char *pszDefaultExt, const char *pszSearchPath, bool fShared);



/**
 * Loads the VMMR0.r0 module early in the init process.
 *
 * @returns VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 */
VMMR3_INT_DECL(int) PDMR3LdrLoadVMMR0U(PUVM pUVM)
{
    return pdmR3LoadR0U(pUVM, NULL, VMMR0_MAIN_MODULE_NAME, NULL);
}


/**
 * Init the module loader part of PDM.
 *
 * This routine will load the Host Context Ring-0 and Guest
 * Context VMM modules.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM structure.
 */
int pdmR3LdrInitU(PUVM pUVM)
{
#if !defined(PDMLDR_FAKE_MODE) && defined(VBOX_WITH_RAW_MODE_KEEP)
    /*
     * Load the mandatory RC module, the VMMR0.r0 is loaded before VM creation.
     */
    PVM pVM = pUVM->pVM; AssertPtr(pVM);
    if (VM_IS_RAW_MODE_ENABLED(pVM))
    {
        int rc = PDMR3LdrLoadRC(pVM, NULL, VMMRC_MAIN_MODULE_NAME);
        if (RT_FAILURE(rc))
            return rc;
    }
#else
    RT_NOREF(pUVM);
#endif
    return VINF_SUCCESS;
}


/**
 * Terminate the module loader part of PDM.
 *
 * This will unload and free all modules.
 *
 * @param   pUVM        The user mode VM structure.
 * @param   fFinal      This is clear when in the PDMR3Term/vmR3Destroy call
 *                      chain, and set when called from PDMR3TermUVM.
 *
 * @remarks This is normally called twice during termination.
 */
void pdmR3LdrTermU(PUVM pUVM, bool fFinal)
{
    /*
     * Free the modules.
     */
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    PPDMMOD pModule = pUVM->pdm.s.pModules;
    pUVM->pdm.s.pModules = NULL;
    PPDMMOD *ppNext = &pUVM->pdm.s.pModules;
    while (pModule)
    {
        /* free loader item. */
        if (pModule->hLdrMod != NIL_RTLDRMOD)
        {
            int rc2 = RTLdrClose(pModule->hLdrMod);
            AssertRC(rc2);
            pModule->hLdrMod = NIL_RTLDRMOD;
        }

        /* free bits. */
        switch (pModule->eType)
        {
            case PDMMOD_TYPE_R0:
            {
                if (fFinal)
                {
                    Assert(pModule->ImageBase);
                    int rc2 = SUPR3FreeModule((void *)(uintptr_t)pModule->ImageBase);
                    AssertRC(rc2);
                    pModule->ImageBase = 0;
                    break;
                }

                /* Postpone ring-0 module till the PDMR3TermUVM() phase as VMMR0.r0 is still
                   busy when we're called the first time very very early in vmR3Destroy().  */
                PPDMMOD pNextModule = pModule->pNext;

                pModule->pNext = NULL;
                *ppNext = pModule;
                ppNext = &pModule->pNext;

                pModule = pNextModule;
                continue;
            }

#ifdef VBOX_WITH_RAW_MODE_KEEP
            case PDMMOD_TYPE_RC:
#endif
            case PDMMOD_TYPE_R3:
                /* MM will free this memory for us - it's alloc only memory. :-) */
                break;

            default:
                AssertMsgFailed(("eType=%d\n", pModule->eType));
                break;
        }
        pModule->pvBits = NULL;

        void    *pvFree = pModule;
        pModule = pModule->pNext;
        RTMemFree(pvFree);
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
}


/**
 * Applies relocations to RC modules.
 *
 * This must be done very early in the relocation
 * process so that components can resolve RC symbols during relocation.
 *
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMMR3_INT_DECL(void) PDMR3LdrRelocateU(PUVM pUVM, RTGCINTPTR offDelta)
{
#ifdef VBOX_WITH_RAW_MODE_KEEP
    LogFlow(("PDMR3LdrRelocate: offDelta=%RGv\n", offDelta));
    RT_NOREF1(offDelta);

    /*
     * RC Modules.
     */
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    if (pUVM->pdm.s.pModules)
    {
        /*
         * The relocation have to be done in two passes so imports
         * can be correctly resolved. The first pass will update
         * the ImageBase saving the current value in OldImageBase.
         * The second pass will do the actual relocation.
         */
        /* pass 1 */
        PPDMMOD pCur;
        for (pCur = pUVM->pdm.s.pModules; pCur; pCur = pCur->pNext)
        {
            if (pCur->eType == PDMMOD_TYPE_RC)
            {
                pCur->OldImageBase = pCur->ImageBase;
                pCur->ImageBase = MMHyperR3ToRC(pUVM->pVM, pCur->pvBits);
            }
        }

        /* pass 2 */
        for (pCur = pUVM->pdm.s.pModules; pCur; pCur = pCur->pNext)
        {
            if (pCur->eType == PDMMOD_TYPE_RC)
            {
                PDMGETIMPORTARGS Args;
                Args.pVM = pUVM->pVM;
                Args.pModule = pCur;
                int rc = RTLdrRelocate(pCur->hLdrMod, pCur->pvBits, pCur->ImageBase, pCur->OldImageBase,
                                       pdmR3GetImportRC, &Args);
                AssertFatalMsgRC(rc, ("RTLdrRelocate failed, rc=%d\n", rc));
            }
        }
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
#else
    RT_NOREF2(pUVM, offDelta);
#endif
}


/**
 * Loads a module into the host context ring-3.
 *
 * This is used by the driver and device init functions to load modules
 * containing the drivers and devices. The function can be extended to
 * load modules which are not native to the environment we're running in,
 * but at the moment this is not required.
 *
 * No reference counting is kept, since we don't implement any facilities
 * for unloading the module. But the module will naturally be released
 * when the VM terminates.
 *
 * @returns VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   pszFilename     Filename of the module binary.
 * @param   pszName         Module name. Case sensitive and the length is limited!
 */
int pdmR3LoadR3U(PUVM pUVM, const char *pszFilename, const char *pszName)
{
    /*
     * Validate input.
     */
    AssertMsg(RTCritSectIsInitialized(&pUVM->pdm.s.ListCritSect), ("bad init order!\n"));
    Assert(pszFilename);
    size_t cchFilename = strlen(pszFilename);
    Assert(pszName);
    size_t cchName = strlen(pszName);
    PPDMMOD  pCur;
    if (cchName >= sizeof(pCur->szName))
    {
        AssertMsgFailed(("Name is too long, cchName=%d pszName='%s'\n", cchName, pszName));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Try lookup the name and see if the module exists.
     */
    int rc;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (pCur = pUVM->pdm.s.pModules; pCur; pCur = pCur->pNext)
    {
        if (!strcmp(pCur->szName, pszName))
        {
            if (pCur->eType == PDMMOD_TYPE_R3)
                rc = VINF_PDM_ALREADY_LOADED;
            else
                rc = VERR_PDM_MODULE_NAME_CLASH;
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);

            AssertMsgRC(rc, ("We've already got a module '%s' loaded!\n", pszName));
            return rc;
        }
    }

    /*
     * Allocate the module list node and initialize it.
     */
    const char *pszSuff = RTLdrGetSuff();
    size_t      cchSuff = RTPathHasSuffix(pszFilename) ? 0 : strlen(pszSuff);
    PPDMMOD     pModule = (PPDMMOD)RTMemAllocZ(RT_UOFFSETOF_DYN(PDMMOD, szFilename[cchFilename + cchSuff + 1]));
    if (pModule)
    {
        pModule->eType = PDMMOD_TYPE_R3;
        memcpy(pModule->szName, pszName, cchName); /* memory is zero'd, no need to copy terminator :-) */
        memcpy(pModule->szFilename, pszFilename, cchFilename);
        memcpy(&pModule->szFilename[cchFilename], pszSuff, cchSuff);

        /*
         * Load the loader item.
         */
        RTERRINFOSTATIC ErrInfo;
        RTErrInfoInitStatic(&ErrInfo);
        rc = SUPR3HardenedLdrLoadPlugIn(pModule->szFilename, &pModule->hLdrMod, &ErrInfo.Core);
        if (RT_SUCCESS(rc))
        {
            pModule->pNext = pUVM->pdm.s.pModules;
            pUVM->pdm.s.pModules = pModule;
        }
        else
        {
            /* Something went wrong, most likely module not found. Don't consider other unlikely errors */
            rc = VMSetError(pUVM->pVM, rc, RT_SRC_POS,
                            N_("Unable to load R3 module %s (%s): %s"), pModule->szFilename, pszName, ErrInfo.Core.pszMsg);
            RTMemFree(pModule);
        }
    }
    else
        rc = VERR_NO_MEMORY;

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return rc;
}

#ifdef VBOX_WITH_RAW_MODE_KEEP

/**
 * Resolve an external symbol during RTLdrGetBits() of a RC module.
 *
 * @returns VBox status code.
 * @param   hLdrMod         The loader module handle.
 * @param   pszModule       Module name.
 * @param   pszSymbol       Symbol name, NULL if uSymbol should be used.
 * @param   uSymbol         Symbol ordinal, ~0 if pszSymbol should be used.
 * @param   pValue          Where to store the symbol value (address).
 * @param   pvUser          User argument.
 */
static DECLCALLBACK(int) pdmR3GetImportRC(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol, unsigned uSymbol,
                                          RTUINTPTR *pValue, void *pvUser)
{
    PVM         pVM     = ((PPDMGETIMPORTARGS)pvUser)->pVM;
    PPDMMOD     pModule = ((PPDMGETIMPORTARGS)pvUser)->pModule;
    NOREF(hLdrMod); NOREF(uSymbol);

    /*
     * Adjust input.
     */
    if (pszModule && !*pszModule)
        pszModule = NULL;

    /*
     * Builtin module.
     */
    if (!pszModule || !strcmp(pszModule, "VMMRCBuiltin.rc"))
    {
        int rc = VINF_SUCCESS;
        if (!strcmp(pszSymbol, "g_VM"))
            *pValue = pVM->pVMRC;
        else if (!strcmp(pszSymbol, "g_VCpu0"))
            *pValue = pVM->pVMRC + pVM->offVMCPU;
        else if (!strcmp(pszSymbol, "g_CPUM"))
            *pValue = VM_RC_ADDR(pVM, &pVM->cpum);
        else if (   !strncmp(pszSymbol, "g_TRPM", 6)
                 || !strncmp(pszSymbol, "g_trpm", 6)
                 || !strncmp(pszSymbol, "TRPM", 4))
        {
            RTRCPTR RCPtr = 0;
            rc = TRPMR3GetImportRC(pVM, pszSymbol, &RCPtr);
            if (RT_SUCCESS(rc))
                *pValue = RCPtr;
        }
        else if (   !strncmp(pszSymbol, "VMM", 3)
                 || !strcmp(pszSymbol, "g_Logger")
                 || !strcmp(pszSymbol, "g_RelLogger"))
        {
            RTRCPTR RCPtr = 0;
            rc = VMMR3GetImportRC(pVM, pszSymbol, &RCPtr);
            if (RT_SUCCESS(rc))
                *pValue = RCPtr;
        }
        else
        {
            AssertMsg(!pszModule, ("Unknown builtin symbol '%s' for module '%s'!\n", pszSymbol, pModule->szName)); NOREF(pModule);
            rc = VERR_SYMBOL_NOT_FOUND;
        }
        if (RT_SUCCESS(rc) || pszModule)
        {
            if (RT_FAILURE(rc))
                LogRel(("PDMLdr: Couldn't find symbol '%s' in module '%s'!\n", pszSymbol, pszModule));
            return rc;
        }
    }

    /*
     * Search for module.
     */
    PUVM     pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    PPDMMOD  pCur = pUVM->pdm.s.pModules;
    while (pCur)
    {
        if (    pCur->eType == PDMMOD_TYPE_RC
            &&  (   !pszModule
                 || !strcmp(pCur->szName, pszModule))
           )
        {
            /* Search for the symbol. */
            int rc = RTLdrGetSymbolEx(pCur->hLdrMod, pCur->pvBits, pCur->ImageBase, UINT32_MAX, pszSymbol, pValue);
            if (RT_SUCCESS(rc))
            {
                AssertMsg(*pValue - pCur->ImageBase < RTLdrSize(pCur->hLdrMod),
                          ("%RRv-%RRv %s %RRv\n", (RTRCPTR)pCur->ImageBase,
                           (RTRCPTR)(pCur->ImageBase + RTLdrSize(pCur->hLdrMod) - 1),
                           pszSymbol, (RTRCPTR)*pValue));
                RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
                return rc;
            }
            if (pszModule)
            {
                RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
                AssertLogRelMsgFailed(("PDMLdr: Couldn't find symbol '%s' in module '%s'!\n", pszSymbol, pszModule));
                return VERR_SYMBOL_NOT_FOUND;
            }
        }

        /* next */
        pCur = pCur->pNext;
    }

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    AssertLogRelMsgFailed(("Couldn't find module '%s' for resolving symbol '%s'!\n", pszModule, pszSymbol));
    return VERR_SYMBOL_NOT_FOUND;
}


/**
 * Loads a module into the raw-mode context (i.e. into the Hypervisor memory
 * region).
 *
 * @returns VBox status code.
 * @retval  VINF_PDM_ALREADY_LOADED if the module is already loaded (name +
 *          filename match).
 * @retval  VERR_PDM_MODULE_NAME_CLASH if a different file has already been
 *          loaded with the name module name.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pszFilename     Filename of the module binary.
 * @param   pszName         Module name. Case sensitive and the length is limited!
 */
VMMR3DECL(int) PDMR3LdrLoadRC(PVM pVM, const char *pszFilename, const char *pszName)
{
    /*
     * Validate input.
     */
    AssertReturn(VM_IS_RAW_MODE_ENABLED(pVM), VERR_PDM_HM_IPE);

    /*
     * Find the file if not specified.
     */
    char *pszFile = NULL;
    if (!pszFilename)
        pszFilename = pszFile = pdmR3FileRC(pszName, NULL);

    /*
     * Check if a module by that name is already loaded.
     */
    int     rc;
    PUVM    pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    PPDMMOD pCur = pUVM->pdm.s.pModules;
    while (pCur)
    {
        if (!strcmp(pCur->szName, pszName))
        {
            /* Name clash. Hopefully due to it being the same file. */
            if (!strcmp(pCur->szFilename, pszFilename))
                rc = VINF_PDM_ALREADY_LOADED;
            else
            {
                rc = VERR_PDM_MODULE_NAME_CLASH;
                AssertMsgFailed(("We've already got a module '%s' loaded!\n", pszName));
            }
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
            RTMemTmpFree(pszFile);
            return rc;
        }
        /* next */
        pCur = pCur->pNext;
    }

    /*
     * Allocate the module list node.
     */
    PPDMMOD pModule = (PPDMMOD)RTMemAllocZ(sizeof(*pModule) + strlen(pszFilename));
    if (!pModule)
    {
        RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
        RTMemTmpFree(pszFile);
        return VERR_NO_MEMORY;
    }
    AssertMsg(strlen(pszName) + 1 < sizeof(pModule->szName),
              ("pazName is too long (%d chars) max is %d chars.\n", strlen(pszName), sizeof(pModule->szName) - 1));
    strcpy(pModule->szName, pszName);
    pModule->eType = PDMMOD_TYPE_RC;
    strcpy(pModule->szFilename, pszFilename);


    /*
     * Open the loader item.
     */
    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);
    rc = SUPR3HardenedVerifyPlugIn(pszFilename, &ErrInfo.Core);
    if (RT_SUCCESS(rc))
    {
        RTErrInfoClear(&ErrInfo.Core);
        rc = RTLdrOpen(pszFilename, 0, RTLDRARCH_X86_32, &pModule->hLdrMod);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate space in the hypervisor.
         */
        size_t          cb = RTLdrSize(pModule->hLdrMod);
        cb = RT_ALIGN_Z(cb, RT_MAX(GUEST_PAGE_SIZE, HOST_PAGE_SIZE));
        uint32_t        cPages = (uint32_t)(cb >> HOST_PAGE_SHIFT);
        if (((size_t)cPages << HOST_PAGE_SHIFT) == cb)
        {
            PSUPPAGE    paPages = (PSUPPAGE)RTMemTmpAlloc(cPages * sizeof(paPages[0]));
            if (paPages)
            {
                rc = SUPR3PageAllocEx(cPages, 0 /*fFlags*/, &pModule->pvBits, NULL /*pR0Ptr*/, paPages);
                if (RT_SUCCESS(rc))
                {
                    RTGCPTR GCPtr;
                    rc = VERR_NOT_IMPLEMENTED; //MMR3HyperMapPages(pVM, pModule->pvBits, NIL_RTR0PTR, cPages, paPages, pModule->szName, &GCPtr);
                    if (RT_SUCCESS(rc))
                    {
                        //MMR3HyperReserveFence(pVM);

                        /*
                         * Get relocated image bits.
                         */
                        Assert(MMHyperR3ToRC(pVM, pModule->pvBits) == GCPtr);
                        pModule->ImageBase = GCPtr;
                        PDMGETIMPORTARGS Args;
                        Args.pVM = pVM;
                        Args.pModule = pModule;
                        rc = RTLdrGetBits(pModule->hLdrMod, pModule->pvBits, pModule->ImageBase, pdmR3GetImportRC, &Args);
                        if (RT_SUCCESS(rc))
                        {
#ifdef VBOX_WITH_DTRACE_RC
                            /*
                             * Register the tracer bits if present.
                             */
                            RTLDRADDR uValue;
                            rc = RTLdrGetSymbolEx(pModule->hLdrMod, pModule->pvBits, pModule->ImageBase, UINT32_MAX,
                                                  "g_VTGObjHeader", &uValue);
                            if (RT_SUCCESS(rc))
                            {
                                PVTGOBJHDR pVtgHdr = (PVTGOBJHDR)MMHyperRCToCC(pVM, (RTRCPTR)uValue);
                                if (   pVtgHdr
                                    && !memcmp(pVtgHdr->szMagic, VTGOBJHDR_MAGIC, sizeof(pVtgHdr->szMagic)))
                                    rc = SUPR3TracerRegisterModule(~(uintptr_t)0, pModule->szName, pVtgHdr, uValue,
                                                                   SUP_TRACER_UMOD_FLAGS_SHARED);
                                else
                                    rc = pVtgHdr ? VERR_INVALID_MAGIC : VERR_INVALID_POINTER;
                                if (RT_FAILURE(rc))
                                    LogRel(("PDMLdr: Failed to register tracepoints for '%s': %Rrc\n", pModule->szName, rc));
                            }
#endif

                            /*
                             * Insert the module.
                             */
                            if (pUVM->pdm.s.pModules)
                            {
                                /* we don't expect this list to be very long, so rather save the tail pointer. */
                                pCur = pUVM->pdm.s.pModules;
                                while (pCur->pNext)
                                    pCur = pCur->pNext;
                                pCur->pNext = pModule;
                            }
                            else
                                pUVM->pdm.s.pModules = pModule; /* (pNext is zeroed by alloc) */
                            Log(("PDM: RC Module at %RRv %s (%s)\n", (RTRCPTR)pModule->ImageBase, pszName, pszFilename));

                            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
                            RTMemTmpFree(pszFile);
                            RTMemTmpFree(paPages);

                            return VINF_SUCCESS;
                        }
                    }
                    else
                    {
                        AssertRC(rc);
                        SUPR3PageFreeEx(pModule->pvBits, cPages);
                    }
                }
                else
                    AssertMsgFailed(("SUPR3PageAlloc(%d,) -> %Rrc\n", cPages, rc));
                RTMemTmpFree(paPages);
            }
            else
                rc = VERR_NO_TMP_MEMORY;
        }
        else
            rc = VERR_OUT_OF_RANGE;
        int rc2 = RTLdrClose(pModule->hLdrMod);
        AssertRC(rc2);
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);

    /* Don't consider VERR_PDM_MODULE_NAME_CLASH and VERR_NO_MEMORY above as these are very unlikely. */
    if (RT_FAILURE(rc) && RTErrInfoIsSet(&ErrInfo.Core))
        rc = VMSetError(pVM, rc, RT_SRC_POS, N_("Cannot load RC module %s: %s"), pszFilename, ErrInfo.Core.pszMsg);
    else if (RT_FAILURE(rc))
        rc = VMSetError(pVM, rc, RT_SRC_POS, N_("Cannot load RC module %s"), pszFilename);

    RTMemFree(pModule);
    RTMemTmpFree(pszFile);
    return rc;
}

#endif /* VBOX_WITH_RAW_MODE_KEEP */

/**
 * Loads a module into the ring-0 context.
 *
 * @returns VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   pszFilename     Filename of the module binary.
 * @param   pszName         Module name. Case sensitive and the length is limited!
 * @param   pszSearchPath   List of directories to search if @a pszFilename is
 *                          not specified.  Can be NULL, in which case the arch
 *                          dependent install dir is searched.
 */
static int pdmR3LoadR0U(PUVM pUVM, const char *pszFilename, const char *pszName, const char *pszSearchPath)
{
    /*
     * Validate input.
     */
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    PPDMMOD pCur = pUVM->pdm.s.pModules;
    while (pCur)
    {
        if (!strcmp(pCur->szName, pszName))
        {
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
            AssertMsgFailed(("We've already got a module '%s' loaded!\n", pszName));
            return VERR_PDM_MODULE_NAME_CLASH;
        }
        /* next */
        pCur = pCur->pNext;
    }

    /*
     * Find the file if not specified.
     */
    char *pszFile = NULL;
    if (!pszFilename)
        pszFilename = pszFile = pdmR3FileR0(pszName, pszSearchPath);

    /*
     * Allocate the module list node.
     */
    PPDMMOD     pModule = (PPDMMOD)RTMemAllocZ(sizeof(*pModule) + strlen(pszFilename));
    if (!pModule)
    {
        RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
        RTMemTmpFree(pszFile);
        return VERR_NO_MEMORY;
    }
    AssertMsg(strlen(pszName) + 1 < sizeof(pModule->szName),
              ("pazName is too long (%d chars) max is %d chars.\n", strlen(pszName), sizeof(pModule->szName) - 1));
    strcpy(pModule->szName, pszName);
    pModule->eType = PDMMOD_TYPE_R0;
    strcpy(pModule->szFilename, pszFilename);

    /*
     * Ask the support library to load it.
     */
    RTERRINFOSTATIC ErrInfo;
    void           *pvImageBase = NULL;
    int rc = SUPR3LoadModule(pszFilename, pszName, &pvImageBase, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        pModule->hLdrMod = NIL_RTLDRMOD;
        pModule->ImageBase = (uintptr_t)pvImageBase;

        /*
         * Insert the module.
         */
        if (pUVM->pdm.s.pModules)
        {
            /* we don't expect this list to be very long, so rather save the tail pointer. */
            pCur = pUVM->pdm.s.pModules;
            while (pCur->pNext)
                pCur = pCur->pNext;
            pCur->pNext = pModule;
        }
        else
            pUVM->pdm.s.pModules = pModule; /* (pNext is zeroed by alloc) */
        Log(("PDM: R0 Module at %RHv %s (%s)\n", (RTR0PTR)pModule->ImageBase, pszName, pszFilename));
        RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
        RTMemTmpFree(pszFile);
        return VINF_SUCCESS;
    }

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    RTMemFree(pModule);
    LogRel(("PDMLdr: pdmR3LoadR0U: pszName=\"%s\" rc=%Rrc szErr=\"%s\"\n", pszName, rc, ErrInfo.Core.pszMsg));

    /* Don't consider VERR_PDM_MODULE_NAME_CLASH and VERR_NO_MEMORY above as these are very unlikely. */
    if (RT_FAILURE(rc))
        rc = VMR3SetError(pUVM, rc, RT_SRC_POS, N_("Failed to load R0 module %s: %s"), pszFilename, ErrInfo.Core.pszMsg);

    RTMemTmpFree(pszFile); /* might be reference thru pszFilename in the above VMSetError call. */
    return rc;
}


/**
 * Makes sure a ring-0 module is loaded.
 *
 * @returns VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   pszModule       Module name (no path).
 * @param   pszSearchPath   List of directories to search for the module
 *                          (assumes @a pszModule is also a filename).
 */
VMMR3_INT_DECL(int) PDMR3LdrLoadR0(PUVM pUVM, const char *pszModule, const char *pszSearchPath)
{
    /*
     * Find the module.
     */
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (PPDMMOD pModule = pUVM->pdm.s.pModules; pModule; pModule = pModule->pNext)
    {
        if (   pModule->eType == PDMMOD_TYPE_R0
            && !strcmp(pModule->szName, pszModule))
        {
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
            return VINF_SUCCESS;
        }
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);

    /*
     * Okay, load it.
     */
    return pdmR3LoadR0U(pUVM, NULL, pszModule, pszSearchPath);
}


/**
 * Get the address of a symbol in a given HC ring 3 module.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszModule       Module name.
 * @param   pszSymbol       Symbol name. If it's value is less than 64k it's treated like a
 *                          ordinal value rather than a string pointer.
 * @param   ppvValue        Where to store the symbol value.
 */
VMMR3_INT_DECL(int) PDMR3LdrGetSymbolR3(PVM pVM, const char *pszModule, const char *pszSymbol, void **ppvValue)
{
    /*
     * Validate input.
     */
    AssertPtr(pVM);
    AssertPtr(pszModule);
    AssertPtr(ppvValue);
    PUVM pUVM = pVM->pUVM;
    AssertMsg(RTCritSectIsInitialized(&pUVM->pdm.s.ListCritSect), ("bad init order!\n"));

    /*
     * Find the module.
     */
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (PPDMMOD pModule = pUVM->pdm.s.pModules; pModule; pModule = pModule->pNext)
    {
        if (    pModule->eType == PDMMOD_TYPE_R3
            &&  !strcmp(pModule->szName, pszModule))
        {
            RTUINTPTR Value = 0;
            int rc = RTLdrGetSymbolEx(pModule->hLdrMod, pModule->pvBits, pModule->ImageBase, UINT32_MAX, pszSymbol, &Value);
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
            if (RT_SUCCESS(rc))
            {
                *ppvValue = (void *)(uintptr_t)Value;
                Assert((uintptr_t)*ppvValue == Value);
            }
            else
            {
                if ((uintptr_t)pszSymbol < 0x10000)
                    AssertMsg(rc, ("Couldn't symbol '%u' in module '%s'\n", (unsigned)(uintptr_t)pszSymbol, pszModule));
                else
                    AssertMsg(rc, ("Couldn't symbol '%s' in module '%s'\n", pszSymbol, pszModule));
            }
            return rc;
        }
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    AssertMsgFailed(("Couldn't locate module '%s'\n", pszModule));
    return VERR_SYMBOL_NOT_FOUND;
}


/**
 * Get the address of a symbol in a given HC ring 0 module.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszModule       Module name. If NULL the main R0 module (VMMR0.r0) is assumes.
 * @param   pszSymbol       Symbol name. If it's value is less than 64k it's treated like a
 *                          ordinal value rather than a string pointer.
 * @param   ppvValue        Where to store the symbol value.
 */
VMMR3DECL(int) PDMR3LdrGetSymbolR0(PVM pVM, const char *pszModule, const char *pszSymbol, PRTR0PTR ppvValue)
{
#ifdef PDMLDR_FAKE_MODE
    *ppvValue = 0xdeadbeef;
    return VINF_SUCCESS;

#else
    /*
     * Validate input.
     */
    AssertPtr(pVM);
    AssertPtrNull(pszModule);
    AssertPtr(ppvValue);
    PUVM pUVM = pVM->pUVM;
    AssertMsg(RTCritSectIsInitialized(&pUVM->pdm.s.ListCritSect), ("bad init order!\n"));

    if (!pszModule)
        pszModule = VMMR0_MAIN_MODULE_NAME;

    /*
     * Find the module.
     */
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (PPDMMOD pModule = pUVM->pdm.s.pModules; pModule; pModule = pModule->pNext)
    {
        if (    pModule->eType == PDMMOD_TYPE_R0
            &&  !strcmp(pModule->szName, pszModule))
        {
            int rc = SUPR3GetSymbolR0((void *)(uintptr_t)pModule->ImageBase, pszSymbol, (void **)ppvValue);
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
            if (RT_FAILURE(rc))
            {
                AssertMsgRC(rc, ("Couldn't find symbol '%s' in module '%s'\n", pszSymbol, pszModule));
                LogRel(("PDMLdr: PDMGetSymbol: Couldn't find symbol '%s' in module '%s'\n", pszSymbol, pszModule));
            }
            return rc;
        }
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    AssertMsgFailed(("Couldn't locate module '%s'\n", pszModule));
    return VERR_SYMBOL_NOT_FOUND;
#endif
}


/**
 * Same as PDMR3LdrGetSymbolR0 except that the module will be attempted loaded if not found.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszModule       Module name. If NULL the main R0 module (VMMR0.r0) is assumed.
 * @param   pszSearchPath   List of directories to search if @a pszFile is
 *                          not qualified with a path.  Can be NULL, in which
 *                          case the arch dependent install dir is searched.
 * @param   pszSymbol       Symbol name. If it's value is less than 64k it's treated like a
 *                          ordinal value rather than a string pointer.
 * @param   ppvValue        Where to store the symbol value.
 */
VMMR3DECL(int) PDMR3LdrGetSymbolR0Lazy(PVM pVM, const char *pszModule, const char *pszSearchPath, const char *pszSymbol,
                                       PRTR0PTR ppvValue)
{
#ifdef PDMLDR_FAKE_MODE
    *ppvValue = 0xdeadbeef;
    return VINF_SUCCESS;

#else
    AssertPtr(pVM);
    AssertPtrNull(pszModule);
    AssertPtr(ppvValue);
    PUVM pUVM = pVM->pUVM;
    AssertMsg(RTCritSectIsInitialized(&pUVM->pdm.s.ListCritSect), ("bad init order!\n"));

    if (pszModule) /* (We don't lazy load the main R0 module.) */
    {
        /*
         * Since we're lazy, we'll only check if the module is present
         * and hand it over to PDMR3LdrGetSymbolR0 when that's done.
         */
        AssertMsgReturn(!strpbrk(pszModule, "/\\:\n\r\t"), ("pszModule=%s\n", pszModule), VERR_INVALID_PARAMETER);
        PPDMMOD pModule;
        RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
        for (pModule = pUVM->pdm.s.pModules; pModule; pModule = pModule->pNext)
            if (    pModule->eType == PDMMOD_TYPE_R0
                &&  !strcmp(pModule->szName, pszModule))
                break;
        RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
        if (!pModule)
        {
            int rc = pdmR3LoadR0U(pUVM, NULL, pszModule, pszSearchPath);
            AssertMsgRCReturn(rc, ("pszModule=%s rc=%Rrc\n", pszModule, rc), VERR_MODULE_NOT_FOUND);
        }
    }

    return PDMR3LdrGetSymbolR0(pVM, pszModule, pszSymbol, ppvValue);
#endif
}


/**
 * Get the address of a symbol in a given RC module.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszModule       Module name.  If NULL the main R0 module (VMMRC.rc)
 *                          is assumes.
 * @param   pszSymbol       Symbol name.  If it's value is less than 64k it's
 *                          treated like a ordinal value rather than a string
 *                          pointer.
 * @param   pRCPtrValue     Where to store the symbol value.
 */
VMMR3DECL(int) PDMR3LdrGetSymbolRC(PVM pVM, const char *pszModule, const char *pszSymbol, PRTRCPTR pRCPtrValue)
{
#if defined(PDMLDR_FAKE_MODE) || !defined(VBOX_WITH_RAW_MODE_KEEP)
    RT_NOREF(pVM, pszModule, pszSymbol);
    *pRCPtrValue = NIL_RTRCPTR;
    return VINF_SUCCESS;

#else
    /*
     * Validate input.
     */
    AssertPtr(pVM);
    AssertPtrNull(pszModule);
    AssertPtr(pRCPtrValue);

    if (!pszModule)
        pszModule = VMMRC_MAIN_MODULE_NAME;

    /*
     * Find the module.
     */
    PUVM pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (PPDMMOD pModule = pUVM->pdm.s.pModules; pModule; pModule = pModule->pNext)
    {
        if (    pModule->eType == PDMMOD_TYPE_RC
            &&  !strcmp(pModule->szName, pszModule))
        {
            RTUINTPTR Value;
            int rc = RTLdrGetSymbolEx(pModule->hLdrMod, pModule->pvBits, pModule->ImageBase, UINT32_MAX, pszSymbol, &Value);
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
            if (RT_SUCCESS(rc))
            {
                *pRCPtrValue = (RTGCPTR)Value;
                Assert(*pRCPtrValue == Value);
            }
            else
            {
                if ((uintptr_t)pszSymbol < 0x10000)
                    AssertMsg(rc, ("Couldn't symbol '%u' in module '%s'\n", (unsigned)(uintptr_t)pszSymbol, pszModule));
                else
                    AssertMsg(rc, ("Couldn't symbol '%s' in module '%s'\n", pszSymbol, pszModule));
            }
            return rc;
        }
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    AssertMsgFailed(("Couldn't locate module '%s'\n", pszModule));
    return VERR_SYMBOL_NOT_FOUND;
#endif
}


/**
 * Same as PDMR3LdrGetSymbolRC except that the module will be attempted loaded if not found.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszModule       Module name.  If NULL the main RC module (VMMRC.rc)
 *                          is assumed.
 * @param   pszSearchPath   List of directories to search if @a pszFile is
 *                          not qualified with a path.  Can be NULL, in which
 *                          case the arch dependent install dir is searched.
 * @param   pszSymbol       Symbol name. If it's value is less than 64k it's treated like a
 *                          ordinal value rather than a string pointer.
 * @param   pRCPtrValue     Where to store the symbol value.
 */
VMMR3DECL(int) PDMR3LdrGetSymbolRCLazy(PVM pVM, const char *pszModule, const char *pszSearchPath, const char *pszSymbol,
                                       PRTRCPTR pRCPtrValue)
{
#if defined(PDMLDR_FAKE_MODE) || !defined(VBOX_WITH_RAW_MODE_KEEP)
    RT_NOREF(pVM, pszModule, pszSearchPath, pszSymbol);
    *pRCPtrValue = NIL_RTRCPTR;
    return VINF_SUCCESS;

#else
    AssertPtr(pVM);
    if (!pszModule)
        pszModule = VMMRC_MAIN_MODULE_NAME;
    AssertPtr(pszModule);
    AssertPtr(pRCPtrValue);

    /*
     * Since we're lazy, we'll only check if the module is present
     * and hand it over to PDMR3LdrGetSymbolRC when that's done.
     */
    AssertMsgReturn(!strpbrk(pszModule, "/\\:\n\r\t"), ("pszModule=%s\n", pszModule), VERR_INVALID_PARAMETER);
    PUVM    pUVM = pVM->pUVM;
    PPDMMOD pModule;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (pModule = pUVM->pdm.s.pModules; pModule; pModule = pModule->pNext)
        if (    pModule->eType == PDMMOD_TYPE_RC
            &&  !strcmp(pModule->szName, pszModule))
            break;
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    if (!pModule)
    {
        char *pszFilename = pdmR3FileRC(pszModule, pszSearchPath);
        AssertMsgReturn(pszFilename, ("pszModule=%s\n", pszModule), VERR_MODULE_NOT_FOUND);
        int rc = PDMR3LdrLoadRC(pVM, pszFilename, pszModule);
        RTMemTmpFree(pszFilename);
        AssertMsgRCReturn(rc, ("pszModule=%s rc=%Rrc\n", pszModule, rc), VERR_MODULE_NOT_FOUND);
    }

    return PDMR3LdrGetSymbolRC(pVM, pszModule, pszSymbol, pRCPtrValue);
#endif
}


/**
 * Constructs the full filename for a R3 image file.
 *
 * @returns Pointer to temporary memory containing the filename.
 *          Caller must free this using RTMemTmpFree().
 * @returns NULL on failure.
 *
 * @param   pszFile         File name (no path).
 * @param   fShared         If true, search in the shared directory (/usr/lib on Unix), else
 *                          search in the private directory (/usr/lib/virtualbox on Unix).
 *                          Ignored if VBOX_PATH_SHARED_LIBS is not defined.
 */
char *pdmR3FileR3(const char *pszFile, bool fShared)
{
    return pdmR3File(pszFile, NULL, NULL, fShared);
}


/**
 * Constructs the full filename for a R0 image file.
 *
 * @returns Pointer to temporary memory containing the filename.
 *          Caller must free this using RTMemTmpFree().
 * @returns NULL on failure.
 *
 * @param   pszFile         File name (no path).
 * @param   pszSearchPath   List of directories to search if @a pszFile is
 *                          not qualified with a path.  Can be NULL, in which
 *                          case the arch dependent install dir is searched.
 */
static char *pdmR3FileR0(const char *pszFile, const char *pszSearchPath)
{
    return pdmR3File(pszFile, NULL, pszSearchPath, /*fShared=*/false);
}


#if 0 /* unused */
/**
 * Constructs the full filename for a RC image file.
 *
 * @returns Pointer to temporary memory containing the filename.
 *          Caller must free this using RTMemTmpFree().
 * @returns NULL on failure.
 *
 * @param   pszFile         File name (no path).
 * @param   pszSearchPath   List of directories to search if @a pszFile is
 *                          not qualified with a path.  Can be NULL, in which
 *                          case the arch dependent install dir is searched.
 */
static char *pdmR3FileRC(const char *pszFile, const char *pszSearchPath)
{
    return pdmR3File(pszFile, NULL, pszSearchPath, /*fShared=*/false);
}
#endif


/**
 * Worker for pdmR3File().
 *
 * @returns Pointer to temporary memory containing the filename.
 *          Caller must free this using RTMemTmpFree().
 * @returns NULL on failure.
 *
 * @param   pszDir        Directory part
 * @param   pszFile       File name part
 * @param   pszDefaultExt Extension part
 */
static char *pdmR3FileConstruct(const char *pszDir, const char *pszFile, const char *pszDefaultExt)
{
    /*
     * Allocate temp memory for return buffer.
     */
    size_t cchDir  = strlen(pszDir);
    size_t cchFile = strlen(pszFile);
    size_t cchDefaultExt;

    /*
     * Default extention?
     */
    if (!pszDefaultExt || strchr(pszFile, '.'))
        cchDefaultExt = 0;
    else
        cchDefaultExt = strlen(pszDefaultExt);

    size_t cchPath = cchDir + 1 + cchFile + cchDefaultExt + 1;
    AssertMsgReturn(cchPath <= RTPATH_MAX, ("Path too long!\n"), NULL);

    char *pszRet = (char *)RTMemTmpAlloc(cchDir + 1 + cchFile + cchDefaultExt + 1);
    AssertMsgReturn(pszRet, ("Out of temporary memory!\n"), NULL);

    /*
     * Construct the filename.
     */
    memcpy(pszRet, pszDir, cchDir);
    pszRet[cchDir++] = '/';            /* this works everywhere */
    memcpy(pszRet + cchDir, pszFile, cchFile + 1);
    if (cchDefaultExt)
        memcpy(pszRet + cchDir + cchFile, pszDefaultExt, cchDefaultExt + 1);

    return pszRet;
}


/**
 * Worker for pdmR3FileRC(), pdmR3FileR0() and pdmR3FileR3().
 *
 * @returns Pointer to temporary memory containing the filename.
 *          Caller must free this using RTMemTmpFree().
 * @returns NULL on failure.
 * @param   pszFile         File name (no path).
 * @param   pszDefaultExt   The default extention, NULL if none.
 * @param   pszSearchPath   List of directories to search if @a pszFile is
 *                          not qualified with a path.  Can be NULL, in which
 *                          case the arch dependent install dir is searched.
 * @param   fShared         If true, search in the shared directory (/usr/lib on Unix), else
 *                          search in the private directory (/usr/lib/virtualbox on Unix).
 *                          Ignored if VBOX_PATH_SHARED_LIBS is not defined.
 * @todo    We'll have this elsewhere than in the root later!
 * @todo    Remove the fShared hack again once we don't need to link against VBoxDD anymore!
 */
static char *pdmR3File(const char *pszFile, const char *pszDefaultExt, const char *pszSearchPath, bool fShared)
{
    char szPath[RTPATH_MAX];
    int  rc;

    AssertLogRelReturn(!fShared || !pszSearchPath, NULL);
    Assert(!RTPathHavePath(pszFile));

    /*
     * If there is a path, search it.
     */
    if (   pszSearchPath
        && *pszSearchPath)
    {
        /* Check the filename length. */
        size_t const    cchFile = strlen(pszFile);
        if (cchFile >= sizeof(szPath))
            return NULL;

        /*
         * Walk the search path.
         */
        const char *psz = pszSearchPath;
        while (*psz)
        {
            /* Skip leading blanks - no directories with leading spaces, thank you. */
            while (RT_C_IS_BLANK(*psz))
                psz++;

            /* Find the end of this element. */
            const char *pszNext;
            const char *pszEnd = strchr(psz, ';');
            if (!pszEnd)
                pszEnd = pszNext = strchr(psz, '\0');
            else
                pszNext = pszEnd + 1;
            if (pszEnd != psz)
            {
                rc = RTPathJoinEx(szPath, sizeof(szPath), psz, pszEnd - psz, pszFile, cchFile, RTPATH_STR_F_STYLE_HOST);
                if (RT_SUCCESS(rc))
                {
                    if (RTFileExists(szPath))
                    {
                        size_t cchPath = strlen(szPath) + 1;
                        char *pszRet = (char *)RTMemTmpAlloc(cchPath);
                        if (pszRet)
                            memcpy(pszRet, szPath, cchPath);
                        return pszRet;
                    }
                }
            }

            /* advance */
            psz = pszNext;
        }
    }

    /*
     * Use the default location.
     */
    rc = fShared
       ? RTPathSharedLibs(    szPath, sizeof(szPath))
       : RTPathAppPrivateArch(szPath, sizeof(szPath));
    if (!RT_SUCCESS(rc))
    {
        AssertMsgFailed(("RTPath[SharedLibs|AppPrivateArch](,%d) failed rc=%d!\n", sizeof(szPath), rc));
        return NULL;
    }

    return pdmR3FileConstruct(szPath, pszFile, pszDefaultExt);
}


/** @internal */
typedef struct QMFEIPARG
{
    RTINTPTR    uPC;

    char       *pszNearSym1;
    size_t      cchNearSym1;
    RTINTPTR    offNearSym1;

    char       *pszNearSym2;
    size_t      cchNearSym2;
    RTINTPTR    offNearSym2;
} QMFEIPARG, *PQMFEIPARG;


/**
 * Enumeration callback function used by RTLdrEnumSymbols().
 *
 * @returns VBox status code. Failure will stop the enumeration.
 * @param   hLdrMod         The loader module handle.
 * @param   pszSymbol       Symbol name. NULL if ordinal only.
 * @param   uSymbol         Symbol ordinal, ~0 if not used.
 * @param   Value           Symbol value.
 * @param   pvUser          The user argument specified to RTLdrEnumSymbols().
 */
static DECLCALLBACK(int) pdmR3QueryModFromEIPEnumSymbols(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol,
                                                         RTUINTPTR Value, void *pvUser)
{
    PQMFEIPARG pArgs = (PQMFEIPARG)pvUser;
    NOREF(hLdrMod);

    RTINTPTR off = Value - pArgs->uPC;
    if (off <= 0)   /* near1 is before or at same location. */
    {
        if (off > pArgs->offNearSym1)
        {
            pArgs->offNearSym1 = off;
            if (pArgs->pszNearSym1 && pArgs->cchNearSym1)
            {
                *pArgs->pszNearSym1 = '\0';
                if (pszSymbol)
                    strncat(pArgs->pszNearSym1, pszSymbol, pArgs->cchNearSym1);
                else
                {
                    char szOrd[32];
                    RTStrPrintf(szOrd, sizeof(szOrd), "#%#x", uSymbol);
                    strncat(pArgs->pszNearSym1, szOrd, pArgs->cchNearSym1);
                }
            }
        }
    }
    else            /* near2 is after */
    {
        if (off < pArgs->offNearSym2)
        {
            pArgs->offNearSym2 = off;
            if (pArgs->pszNearSym2 && pArgs->cchNearSym2)
            {
                *pArgs->pszNearSym2 = '\0';
                if (pszSymbol)
                    strncat(pArgs->pszNearSym2, pszSymbol, pArgs->cchNearSym2);
                else
                {
                    char szOrd[32];
                    RTStrPrintf(szOrd, sizeof(szOrd), "#%#x", uSymbol);
                    strncat(pArgs->pszNearSym2, szOrd, pArgs->cchNearSym2);
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Internal worker for PDMR3LdrQueryRCModFromPC and PDMR3LdrQueryR0ModFromPC.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   uPC         The program counter (eip/rip) to locate the module for.
 * @param   enmType     The module type.
 * @param   pszModName  Where to store the module name.
 * @param   cchModName  Size of the module name buffer.
 * @param   pMod        Base address of the module.
 * @param   pszNearSym1 Name of the closes symbol from below.
 * @param   cchNearSym1 Size of the buffer pointed to by pszNearSym1.
 * @param   pNearSym1   The address of pszNearSym1.
 * @param   pszNearSym2 Name of the closes symbol from below.
 * @param   cchNearSym2 Size of the buffer pointed to by pszNearSym2.
 * @param   pNearSym2   The address of pszNearSym2.
 */
static int pdmR3LdrQueryModFromPC(PVM pVM, RTUINTPTR uPC, PDMMODTYPE enmType,
                                  char *pszModName,  size_t cchModName,  PRTUINTPTR pMod,
                                  char *pszNearSym1, size_t cchNearSym1, PRTUINTPTR pNearSym1,
                                  char *pszNearSym2, size_t cchNearSym2, PRTUINTPTR pNearSym2)
{
    PUVM    pUVM = pVM->pUVM;
    int     rc   = VERR_MODULE_NOT_FOUND;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (PPDMMOD pCur= pUVM->pdm.s.pModules; pCur; pCur = pCur->pNext)
    {
        if (pCur->eType != enmType)
            continue;

        /* The following RTLdrOpen call is a dirty hack to get ring-0 module information. */
        RTLDRMOD hLdrMod = pCur->hLdrMod;
        if (hLdrMod == NIL_RTLDRMOD && uPC >= pCur->ImageBase)
        {
            int rc2 = RTLdrOpen(pCur->szFilename, 0 /*fFlags*/, RTLDRARCH_HOST, &hLdrMod);
            if (RT_FAILURE(rc2))
                hLdrMod = NIL_RTLDRMOD;
        }

        if (   hLdrMod != NIL_RTLDRMOD
            && uPC - pCur->ImageBase < RTLdrSize(hLdrMod))
        {
            if (pMod)
                *pMod = pCur->ImageBase;
            if (pszModName && cchModName)
            {
                *pszModName = '\0';
                strncat(pszModName, pCur->szName, cchModName);
            }
            if (pNearSym1)   *pNearSym1   = 0;
            if (pNearSym2)   *pNearSym2   = 0;
            if (pszNearSym1) *pszNearSym1 = '\0';
            if (pszNearSym2) *pszNearSym2 = '\0';

            /*
             * Locate the nearest symbols.
             */
            QMFEIPARG   Args;
            Args.uPC         = uPC;
            Args.pszNearSym1 = pszNearSym1;
            Args.cchNearSym1 = cchNearSym1;
            Args.offNearSym1 = RTINTPTR_MIN;
            Args.pszNearSym2 = pszNearSym2;
            Args.cchNearSym2 = cchNearSym2;
            Args.offNearSym2 = RTINTPTR_MAX;

            rc = RTLdrEnumSymbols(hLdrMod, RTLDR_ENUM_SYMBOL_FLAGS_ALL, pCur->pvBits, pCur->ImageBase,
                                  pdmR3QueryModFromEIPEnumSymbols, &Args);
            if (pNearSym1 && Args.offNearSym1 != RTINTPTR_MIN)
                *pNearSym1 = Args.offNearSym1 + uPC;
            if (pNearSym2 && Args.offNearSym2 != RTINTPTR_MAX)
                *pNearSym2 = Args.offNearSym2 + uPC;

            rc = VINF_SUCCESS;
        }

        if (hLdrMod != pCur->hLdrMod && hLdrMod != NIL_RTLDRMOD)
            RTLdrClose(hLdrMod);

        if (RT_SUCCESS(rc))
            break;
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return rc;
}


/**
 * Queries raw-mode context module information from an PC (eip/rip).
 *
 * This is typically used to locate a crash address.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   uPC         The program counter (eip/rip) to locate the module for.
 * @param   pszModName  Where to store the module name.
 * @param   cchModName  Size of the module name buffer.
 * @param   pMod        Base address of the module.
 * @param   pszNearSym1 Name of the closes symbol from below.
 * @param   cchNearSym1 Size of the buffer pointed to by pszNearSym1.
 * @param   pNearSym1   The address of pszNearSym1.
 * @param   pszNearSym2 Name of the closes symbol from below.
 * @param   cchNearSym2 Size of the buffer pointed to by pszNearSym2.
 * @param   pNearSym2   The address of pszNearSym2.
 */
VMMR3_INT_DECL(int) PDMR3LdrQueryRCModFromPC(PVM pVM, RTRCPTR uPC,
                                             char *pszModName,  size_t cchModName,  PRTRCPTR pMod,
                                             char *pszNearSym1, size_t cchNearSym1, PRTRCPTR pNearSym1,
                                             char *pszNearSym2, size_t cchNearSym2, PRTRCPTR pNearSym2)
{
    RTUINTPTR AddrMod   = 0;
    RTUINTPTR AddrNear1 = 0;
    RTUINTPTR AddrNear2 = 0;
    int rc = pdmR3LdrQueryModFromPC(pVM, uPC, PDMMOD_TYPE_RC,
                                    pszModName,  cchModName,  &AddrMod,
                                    pszNearSym1, cchNearSym1, &AddrNear1,
                                    pszNearSym2, cchNearSym2, &AddrNear2);
    if (RT_SUCCESS(rc))
    {
        if (pMod)
            *pMod      = (RTRCPTR)AddrMod;
        if (pNearSym1)
            *pNearSym1 = (RTRCPTR)AddrNear1;
        if (pNearSym2)
            *pNearSym2 = (RTRCPTR)AddrNear2;
    }
    return rc;
}


/**
 * Queries ring-0 context module information from an PC (eip/rip).
 *
 * This is typically used to locate a crash address.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   uPC         The program counter (eip/rip) to locate the module for.
 * @param   pszModName  Where to store the module name.
 * @param   cchModName  Size of the module name buffer.
 * @param   pMod        Base address of the module.
 * @param   pszNearSym1 Name of the closes symbol from below.
 * @param   cchNearSym1 Size of the buffer pointed to by pszNearSym1.
 * @param   pNearSym1   The address of pszNearSym1.
 * @param   pszNearSym2 Name of the closes symbol from below.
 * @param   cchNearSym2 Size of the buffer pointed to by pszNearSym2. Optional.
 * @param   pNearSym2   The address of pszNearSym2. Optional.
 */
VMMR3_INT_DECL(int) PDMR3LdrQueryR0ModFromPC(PVM pVM, RTR0PTR uPC,
                                             char *pszModName,  size_t cchModName,  PRTR0PTR pMod,
                                             char *pszNearSym1, size_t cchNearSym1, PRTR0PTR pNearSym1,
                                             char *pszNearSym2, size_t cchNearSym2, PRTR0PTR pNearSym2)
{
    RTUINTPTR AddrMod   = 0;
    RTUINTPTR AddrNear1 = 0;
    RTUINTPTR AddrNear2 = 0;
    int rc = pdmR3LdrQueryModFromPC(pVM, uPC, PDMMOD_TYPE_R0,
                                    pszModName,  cchModName,  &AddrMod,
                                    pszNearSym1, cchNearSym1, &AddrNear1,
                                    pszNearSym2, cchNearSym2, &AddrNear2);
    if (RT_SUCCESS(rc))
    {
        if (pMod)
            *pMod      = (RTR0PTR)AddrMod;
        if (pNearSym1)
            *pNearSym1 = (RTR0PTR)AddrNear1;
        if (pNearSym2)
            *pNearSym2 = (RTR0PTR)AddrNear2;
    }
    return rc;
}


/**
 * Enumerate all PDM modules.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pfnCallback     Function to call back for each of the modules.
 * @param   pvArg           User argument.
 */
VMMR3DECL(int)  PDMR3LdrEnumModules(PVM pVM, PFNPDMR3ENUM pfnCallback, void *pvArg)
{
    PUVM pUVM = pVM->pUVM;
    int  rc   = VINF_SUCCESS;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (PPDMMOD pCur = pUVM->pdm.s.pModules; pCur; pCur = pCur->pNext)
    {
        rc = pfnCallback(pVM,
                         pCur->szFilename,
                         pCur->szName,
                         pCur->ImageBase,
                         pCur->eType == PDMMOD_TYPE_RC ? RTLdrSize(pCur->hLdrMod) : 0,
                           pCur->eType == PDMMOD_TYPE_RC ? PDMLDRCTX_RAW_MODE
                         : pCur->eType == PDMMOD_TYPE_R0 ? PDMLDRCTX_RING_0
                         : pCur->eType == PDMMOD_TYPE_R3 ? PDMLDRCTX_RING_3
                         :                                 PDMLDRCTX_INVALID,
                         pvArg);
        if (RT_FAILURE(rc))
            break;
    }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return rc;
}


/**
 * Locates a module.
 *
 * @returns Pointer to the module if found.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   pszModule       The module name.
 * @param   enmType         The module type.
 * @param   fLazy           Lazy loading the module if set.
 * @param   pszSearchPath   Search path for use when lazy loading.
 */
static PPDMMOD pdmR3LdrFindModule(PUVM pUVM, const char *pszModule, PDMMODTYPE enmType,
                                  bool fLazy, const char *pszSearchPath)
{
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    for (PPDMMOD pModule = pUVM->pdm.s.pModules; pModule; pModule = pModule->pNext)
        if (    pModule->eType == enmType
            &&  !strcmp(pModule->szName, pszModule))
        {
            RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
            return pModule;
        }
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    if (fLazy)
    {
        switch (enmType)
        {
#ifdef VBOX_WITH_RAW_MODE_KEEP
            case PDMMOD_TYPE_RC:
            {
                char *pszFilename = pdmR3FileRC(pszModule, pszSearchPath);
                if (pszFilename)
                {
                    int rc = PDMR3LdrLoadRC(pUVM->pVM, pszFilename, pszModule);
                    RTMemTmpFree(pszFilename);
                    if (RT_SUCCESS(rc))
                        return pdmR3LdrFindModule(pUVM, pszModule, enmType, false, NULL);
                }
                break;
            }
#endif

            case PDMMOD_TYPE_R0:
            {
                int rc = pdmR3LoadR0U(pUVM, NULL, pszModule, pszSearchPath);
                if (RT_SUCCESS(rc))
                    return pdmR3LdrFindModule(pUVM, pszModule, enmType, false, NULL);
                break;
            }

            default:
                AssertFailed();
        }
    }
    return NULL;
}


/**
 * Resolves a ring-0 or raw-mode context interface.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pvInterface     Pointer to the interface structure.  The symbol list
 *                          describes the layout.
 * @param   cbInterface     The size of the structure pvInterface is pointing
 *                          to.  For bounds checking.
 * @param   pszModule       The module name.  If NULL we assume it's the default
 *                          R0 or RC module (@a fRing0OrRC).  We'll attempt to
 *                          load the module if it isn't found in the module
 *                          list.
 * @param   pszSearchPath   The module search path.  If NULL, search the
 *                          architecture dependent install directory.
 * @param   pszSymPrefix    What to prefix the symbols in the list with.  The
 *                          idea is that you define a list that goes with an
 *                          interface (INTERFACE_SYM_LIST) and reuse it with
 *                          each implementation.
 * @param   pszSymList      The symbol list for the interface.  This is a
 *                          semi-colon separated list of symbol base names.  As
 *                          mentioned above, each is prefixed with @a
 *                          pszSymPrefix before resolving.  There are a couple
 *                          of special symbol names that will cause us to skip
 *                          ahead a little bit:
 *                              - U8:whatever,
 *                              - U16:whatever,
 *                              - U32:whatever,
 *                              - U64:whatever,
 *                              - RCPTR:whatever,
 *                              - R3PTR:whatever,
 *                              - R0PTR:whatever,
 *                              - GCPHYS:whatever,
 *                              - HCPHYS:whatever.
 * @param   fRing0          Set if it's a ring-0 context interface, clear if
 *                          it's raw-mode context interface.
 */
VMMR3_INT_DECL(int) PDMR3LdrGetInterfaceSymbols(PVM pVM, void *pvInterface, size_t cbInterface,
                                                const char *pszModule, const char *pszSearchPath,
                                                const char *pszSymPrefix, const char *pszSymList,
                                                bool fRing0)
{
    bool const fNullRun = !fRing0;

    /*
     * Find the module.
     */
    int     rc      = VINF_SUCCESS;
    PPDMMOD pModule = NULL;
    if (!fNullRun)
        pModule = pdmR3LdrFindModule(pVM->pUVM,
                                     pszModule ? pszModule : fRing0 ? "VMMR0.r0" : "VMMRC.rc",
                                     fRing0 ? PDMMOD_TYPE_R0 : PDMMOD_TYPE_RC,
                                     true /*fLazy*/, pszSearchPath);
    if (pModule || fNullRun)
    {
        /* Prep the symbol name. */
        char            szSymbol[256];
        size_t const    cchSymPrefix = strlen(pszSymPrefix);
        AssertReturn(cchSymPrefix + 5 < sizeof(szSymbol), VERR_SYMBOL_NOT_FOUND);
        memcpy(szSymbol, pszSymPrefix, cchSymPrefix);

        /*
         * Iterate the symbol list.
         */
        uint32_t        offInterface = 0;
        const char     *pszCur       = pszSymList;
        while (pszCur)
        {
            /*
             * Find the end of the current symbol name.
             */
            size_t      cchSym;
            const char *pszNext = strchr(pszCur, ';');
            if (pszNext)
            {
                cchSym = pszNext - pszCur;
                pszNext++;
            }
            else
                cchSym = strlen(pszCur);
            AssertBreakStmt(cchSym > 0, rc = VERR_INVALID_PARAMETER);

            /* Is it a skip instruction? */
            const char *pszColon = (const char *)memchr(pszCur, ':', cchSym);
            if (pszColon)
            {
                /*
                 * String switch on the instruction and execute it, checking
                 * that we didn't overshoot the interface structure.
                 */
#define IS_SKIP_INSTR(szInstr) \
                (   cchSkip == sizeof(szInstr) - 1 \
                 && !memcmp(pszCur, szInstr, sizeof(szInstr) - 1) )

                size_t const cchSkip = pszColon - pszCur;
                if (IS_SKIP_INSTR("U8"))
                    offInterface += sizeof(uint8_t);
                else if (IS_SKIP_INSTR("U16"))
                    offInterface += sizeof(uint16_t);
                else if (IS_SKIP_INSTR("U32"))
                    offInterface += sizeof(uint32_t);
                else if (IS_SKIP_INSTR("U64"))
                    offInterface += sizeof(uint64_t);
                else if (IS_SKIP_INSTR("RCPTR"))
                    offInterface += sizeof(RTRCPTR);
                else if (IS_SKIP_INSTR("R3PTR"))
                    offInterface += sizeof(RTR3PTR);
                else if (IS_SKIP_INSTR("R0PTR"))
                    offInterface += sizeof(RTR0PTR);
                else if (IS_SKIP_INSTR("HCPHYS"))
                    offInterface += sizeof(RTHCPHYS);
                else if (IS_SKIP_INSTR("GCPHYS"))
                    offInterface += sizeof(RTGCPHYS);
                else
                    AssertMsgFailedBreakStmt(("Invalid skip instruction %.*s (prefix=%s)\n", cchSym, pszCur, pszSymPrefix),
                                             rc = VERR_INVALID_PARAMETER);
                AssertMsgBreakStmt(offInterface <= cbInterface,
                                   ("off=%#x cb=%#x (sym=%.*s prefix=%s)\n", offInterface, cbInterface, cchSym, pszCur, pszSymPrefix),
                                   rc = VERR_BUFFER_OVERFLOW);
#undef IS_SKIP_INSTR
            }
            else
            {
                /*
                 * Construct the symbol name, get its value, store it and
                 * advance the interface cursor.
                 */
                AssertReturn(cchSymPrefix + cchSym < sizeof(szSymbol), VERR_SYMBOL_NOT_FOUND);
                memcpy(&szSymbol[cchSymPrefix], pszCur, cchSym);
                szSymbol[cchSymPrefix + cchSym] = '\0';

                if (fRing0)
                {
                    void *pvValue = NULL;
                    if (!fNullRun)
                    {
                        rc = SUPR3GetSymbolR0((void *)(RTR0PTR)pModule->ImageBase, szSymbol, &pvValue);
                        AssertMsgRCBreak(rc, ("Couldn't find symbol '%s' in module '%s'\n", szSymbol, pModule->szName));
                    }

                    PRTR0PTR pValue = (PRTR0PTR)((uintptr_t)pvInterface + offInterface);
                    AssertMsgBreakStmt(offInterface + sizeof(*pValue) <= cbInterface,
                                       ("off=%#x cb=%#x sym=%s\n", offInterface, cbInterface, szSymbol),
                                       rc = VERR_BUFFER_OVERFLOW);
                    *pValue = (RTR0PTR)pvValue;
                    Assert((void *)*pValue == pvValue);
                    offInterface += sizeof(*pValue);
                }
                else
                {
                    RTUINTPTR Value = 0;
                    if (!fNullRun)
                    {
                        rc = RTLdrGetSymbolEx(pModule->hLdrMod, pModule->pvBits, pModule->ImageBase, UINT32_MAX, szSymbol, &Value);
                        AssertMsgRCBreak(rc, ("Couldn't find symbol '%s' in module '%s'\n", szSymbol, pModule->szName));
                    }

                    PRTRCPTR pValue = (PRTRCPTR)((uintptr_t)pvInterface + offInterface);
                    AssertMsgBreakStmt(offInterface + sizeof(*pValue) <= cbInterface,
                                       ("off=%#x cb=%#x sym=%s\n", offInterface, cbInterface, szSymbol),
                                       rc = VERR_BUFFER_OVERFLOW);
                    *pValue = (RTRCPTR)Value;
                    Assert(*pValue == Value);
                    offInterface += sizeof(*pValue);
                }
            }

            /* advance */
            pszCur = pszNext;
        }

    }
    else
        rc = VERR_MODULE_NOT_FOUND;
    return rc;
}

