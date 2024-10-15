/* $Id: VM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VM - Virtual Machine
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

/** @page pg_vm     VM API
 *
 * This is the encapsulating bit.  It provides the APIs that Main and VBoxBFE
 * use to create a VMM instance for running a guest in.  It also provides
 * facilities for queuing request for execution in EMT (serialization purposes
 * mostly) and for reporting error back to the VMM user (Main/VBoxBFE).
 *
 *
 * @section sec_vm_design   Design Critique / Things To Do
 *
 * In hindsight this component is a big design mistake, all this stuff really
 * belongs in the VMM component.  It just seemed like a kind of ok idea at a
 * time when the VMM bit was a kind of vague.  'VM' also happened to be the name
 * of the per-VM instance structure (see vm.h), so it kind of made sense.
 * However as it turned out, VMM(.cpp) is almost empty all it provides in ring-3
 * is some minor functionally and some "routing" services.
 *
 * Fixing this is just a matter of some more or less straight forward
 * refactoring, the question is just when someone will get to it. Moving the EMT
 * would be a good start.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VM
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/gcm.h>
#include "VMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/sup.h>
#if defined(VBOX_WITH_DTRACE_R3) && !defined(VBOX_WITH_NATIVE_DTRACE)
# include <VBox/VBoxTpG.h>
#endif
#include <VBox/dbg.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#ifdef RT_OS_DARWIN
# include <iprt/system.h>
#endif
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int                  vmR3CreateUVM(uint32_t cCpus, PCVMM2USERMETHODS pVmm2UserMethods, PUVM *ppUVM);
static DECLCALLBACK(int)    vmR3CreateU(PUVM pUVM, uint32_t cCpus, PFNCFGMCONSTRUCTOR pfnCFGMConstructor, void *pvUserCFGM);
static int                  vmR3ReadBaseConfig(PVM pVM, PUVM pUVM, uint32_t cCpus);
static int                  vmR3InitRing3(PVM pVM, PUVM pUVM);
static int                  vmR3InitRing0(PVM pVM);
static int                  vmR3InitDoCompleted(PVM pVM, VMINITCOMPLETED enmWhat);
static void                 vmR3DestroyUVM(PUVM pUVM, uint32_t cMilliesEMTWait);
static bool                 vmR3ValidateStateTransition(VMSTATE enmStateOld, VMSTATE enmStateNew);
static void                 vmR3DoAtState(PVM pVM, PUVM pUVM, VMSTATE enmStateNew, VMSTATE enmStateOld);
static int                  vmR3TrySetState(PVM pVM, const char *pszWho, unsigned cTransitions, ...);
static void                 vmR3SetStateLocked(PVM pVM, PUVM pUVM, VMSTATE enmStateNew, VMSTATE enmStateOld, bool fSetRatherThanClearFF);
static void                 vmR3SetState(PVM pVM, VMSTATE enmStateNew, VMSTATE enmStateOld);
static int                  vmR3SetErrorU(PUVM pUVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(6, 7);


/**
 * Creates a virtual machine by calling the supplied configuration constructor.
 *
 * On successful returned the VM is powered, i.e. VMR3PowerOn() should be
 * called to start the execution.
 *
 * @returns 0 on success.
 * @returns VBox error code on failure.
 * @param   cCpus               Number of virtual CPUs for the new VM.
 * @param   pVmm2UserMethods    An optional method table that the VMM can use
 *                              to make the user perform various action, like
 *                              for instance state saving.
 * @param   fFlags              VMCREATE_F_XXX
 * @param   pfnVMAtError        Pointer to callback function for setting VM
 *                              errors.  This was added as an implicit call to
 *                              VMR3AtErrorRegister() since there is no way the
 *                              caller can get to the VM handle early enough to
 *                              do this on its own.
 *                              This is called in the context of an EMT.
 * @param   pvUserVM            The user argument passed to pfnVMAtError.
 * @param   pfnCFGMConstructor  Pointer to callback function for constructing the VM configuration tree.
 *                              This is called in the context of an EMT0.
 * @param   pvUserCFGM          The user argument passed to pfnCFGMConstructor.
 * @param   ppVM                Where to optionally store the 'handle' of the
 *                              created VM.
 * @param   ppUVM               Where to optionally store the user 'handle' of
 *                              the created VM, this includes one reference as
 *                              if VMR3RetainUVM() was called.  The caller
 *                              *MUST* remember to pass the returned value to
 *                              VMR3ReleaseUVM() once done with the handle.
 */
VMMR3DECL(int)   VMR3Create(uint32_t cCpus, PCVMM2USERMETHODS pVmm2UserMethods, uint64_t fFlags,
                            PFNVMATERROR pfnVMAtError, void *pvUserVM,
                            PFNCFGMCONSTRUCTOR pfnCFGMConstructor, void *pvUserCFGM,
                            PVM *ppVM, PUVM *ppUVM)
{
    LogFlow(("VMR3Create: cCpus=%RU32 pVmm2UserMethods=%p fFlags=%#RX64 pfnVMAtError=%p pvUserVM=%p  pfnCFGMConstructor=%p pvUserCFGM=%p ppVM=%p ppUVM=%p\n",
             cCpus, pVmm2UserMethods, fFlags, pfnVMAtError, pvUserVM, pfnCFGMConstructor, pvUserCFGM, ppVM, ppUVM));

    if (pVmm2UserMethods)
    {
        AssertPtrReturn(pVmm2UserMethods, VERR_INVALID_POINTER);
        AssertReturn(pVmm2UserMethods->u32Magic    == VMM2USERMETHODS_MAGIC,   VERR_INVALID_PARAMETER);
        AssertReturn(pVmm2UserMethods->u32Version  == VMM2USERMETHODS_VERSION, VERR_INVALID_PARAMETER);
        AssertPtrNullReturn(pVmm2UserMethods->pfnSaveState, VERR_INVALID_POINTER);
        AssertPtrNullReturn(pVmm2UserMethods->pfnNotifyEmtInit, VERR_INVALID_POINTER);
        AssertPtrNullReturn(pVmm2UserMethods->pfnNotifyEmtTerm, VERR_INVALID_POINTER);
        AssertPtrNullReturn(pVmm2UserMethods->pfnNotifyPdmtInit, VERR_INVALID_POINTER);
        AssertPtrNullReturn(pVmm2UserMethods->pfnNotifyPdmtTerm, VERR_INVALID_POINTER);
        AssertPtrNullReturn(pVmm2UserMethods->pfnNotifyResetTurnedIntoPowerOff, VERR_INVALID_POINTER);
        AssertReturn(pVmm2UserMethods->u32EndMagic == VMM2USERMETHODS_MAGIC,   VERR_INVALID_PARAMETER);
    }
    AssertPtrNullReturn(pfnVMAtError, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnCFGMConstructor, VERR_INVALID_POINTER);
    AssertPtrNullReturn(ppVM, VERR_INVALID_POINTER);
    AssertPtrNullReturn(ppUVM, VERR_INVALID_POINTER);
    AssertReturn(ppVM || ppUVM, VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fFlags & ~VMCREATE_F_DRIVERLESS), ("%#RX64\n", fFlags), VERR_INVALID_FLAGS);

    /*
     * Validate input.
     */
    AssertLogRelMsgReturn(cCpus > 0 && cCpus <= VMM_MAX_CPU_COUNT, ("%RU32\n", cCpus), VERR_TOO_MANY_CPUS);

    /*
     * Create the UVM so we can register the at-error callback
     * and consolidate a bit of cleanup code.
     */
    PUVM pUVM = NULL;                   /* shuts up gcc */
    int rc = vmR3CreateUVM(cCpus, pVmm2UserMethods, &pUVM);
    if (RT_FAILURE(rc))
        return rc;
    if (pfnVMAtError)
        rc = VMR3AtErrorRegister(pUVM, pfnVMAtError, pvUserVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the support library creating the session for this VM.
         */
        if (fFlags & VMCREATE_F_DRIVERLESS)
            rc = SUPR3InitEx(SUPR3INIT_F_DRIVERLESS | SUPR3INIT_F_DRIVERLESS_IEM_ALLOWED, &pUVM->vm.s.pSession);
        else
            rc = SUPR3Init(&pUVM->vm.s.pSession);
        if (RT_SUCCESS(rc))
        {
#if defined(VBOX_WITH_DTRACE_R3) && !defined(VBOX_WITH_NATIVE_DTRACE)
            /* Now that we've opened the device, we can register trace probes. */
            static bool s_fRegisteredProbes = false;
            if (!SUPR3IsDriverless() && ASMAtomicCmpXchgBool(&s_fRegisteredProbes, true, false))
                SUPR3TracerRegisterModule(~(uintptr_t)0, "VBoxVMM", &g_VTGObjHeader, (uintptr_t)&g_VTGObjHeader,
                                          SUP_TRACER_UMOD_FLAGS_SHARED);
#endif

            /*
             * Call vmR3CreateU in the EMT thread and wait for it to finish.
             *
             * Note! VMCPUID_ANY is used here because VMR3ReqQueueU would have trouble
             *       submitting a request to a specific VCPU without a pVM. So, to make
             *       sure init is running on EMT(0), vmR3EmulationThreadWithId makes sure
             *       that only EMT(0) is servicing VMCPUID_ANY requests when pVM is NULL.
             */
            PVMREQ pReq;
            rc = VMR3ReqCallU(pUVM, VMCPUID_ANY, &pReq, RT_INDEFINITE_WAIT, VMREQFLAGS_VBOX_STATUS,
                              (PFNRT)vmR3CreateU, 4, pUVM, cCpus, pfnCFGMConstructor, pvUserCFGM);
            if (RT_SUCCESS(rc))
            {
                rc = pReq->iStatus;
                VMR3ReqFree(pReq);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Success!
                     */
                    if (ppVM)
                        *ppVM = pUVM->pVM;
                    if (ppUVM)
                    {
                        VMR3RetainUVM(pUVM);
                        *ppUVM = pUVM;
                    }
                    LogFlow(("VMR3Create: returns VINF_SUCCESS (pVM=%p, pUVM=%p\n", pUVM->pVM, pUVM));
                    return VINF_SUCCESS;
                }
            }
            else
                AssertMsgFailed(("VMR3ReqCallU failed rc=%Rrc\n", rc));

            /*
             * An error occurred during VM creation.  Set the error message directly
             * using the initial callback, as the callback list might not exist yet.
             */
            const char *pszError;
            switch (rc)
            {
                case VERR_VMX_IN_VMX_ROOT_MODE:
#ifdef RT_OS_LINUX
                    pszError = N_("VirtualBox can't operate in VMX root mode. "
                                  "Please disable the KVM kernel extension, recompile your kernel and reboot");
#else
                    pszError = N_("VirtualBox can't operate in VMX root mode. Please close all other virtualization programs.");
#endif
                    break;

#ifndef RT_OS_DARWIN
                case VERR_HM_CONFIG_MISMATCH:
                    pszError = N_("VT-x/AMD-V is either not available on your host or disabled. "
                                  "This hardware extension is required by the VM configuration");
                    break;
#endif

                case VERR_SVM_IN_USE:
#ifdef RT_OS_LINUX
                    pszError = N_("VirtualBox can't enable the AMD-V extension. "
                                  "Please disable the KVM kernel extension, recompile your kernel and reboot");
#else
                    pszError = N_("VirtualBox can't enable the AMD-V extension. Please close all other virtualization programs.");
#endif
                    break;

#ifdef RT_OS_LINUX
                case VERR_SUPDRV_COMPONENT_NOT_FOUND:
                    pszError = N_("One of the kernel modules was not successfully loaded. Make sure "
                                  "that VirtualBox is correctly installed, and if you are using EFI "
                                  "Secure Boot that the modules are signed if necessary in the right "
                                  "way for your host system.  Then try to recompile and reload the "
                                  "kernel modules by executing "
                                  "'/sbin/vboxconfig' as root");
                    break;
#endif

                case VERR_RAW_MODE_INVALID_SMP:
                    pszError = N_("VT-x/AMD-V is either not available on your host or disabled. "
                                  "VirtualBox requires this hardware extension to emulate more than one "
                                  "guest CPU");
                    break;

                case VERR_SUPDRV_KERNEL_TOO_OLD_FOR_VTX:
#ifdef RT_OS_LINUX
                    pszError = N_("Because the host kernel is too old, VirtualBox cannot enable the VT-x "
                                  "extension. Either upgrade your kernel to Linux 2.6.13 or later or disable "
                                  "the VT-x extension in the VM settings. Note that without VT-x you have "
                                  "to reduce the number of guest CPUs to one");
#else
                    pszError = N_("Because the host kernel is too old, VirtualBox cannot enable the VT-x "
                                  "extension. Either upgrade your kernel or disable the VT-x extension in the "
                                  "VM settings. Note that without VT-x you have to reduce the number of guest "
                                  "CPUs to one");
#endif
                    break;

                case VERR_PDM_DEVICE_NOT_FOUND:
                    pszError = N_("A virtual device is configured in the VM settings but the device "
                                  "implementation is missing.\n"
                                  "A possible reason for this error is a missing extension pack. Note "
                                  "that as of VirtualBox 4.0, certain features (for example USB 2.0 "
                                  "support and remote desktop) are only available from an 'extension "
                                  "pack' which must be downloaded and installed separately");
                    break;

                case VERR_PCI_PASSTHROUGH_NO_HM:
                    pszError = N_("PCI passthrough requires VT-x/AMD-V");
                    break;

                case VERR_PCI_PASSTHROUGH_NO_NESTED_PAGING:
                    pszError = N_("PCI passthrough requires nested paging");
                    break;

                default:
                    if (VMR3GetErrorCount(pUVM) == 0)
                    {
                        pszError = (char *)alloca(1024);
                        RTErrQueryMsgFull(rc, (char *)pszError, 1024, false /*fFailIfUnknown*/);
                    }
                    else
                        pszError = NULL; /* already set. */
                    break;
            }
            if (pszError)
                vmR3SetErrorU(pUVM, rc, RT_SRC_POS, pszError, rc);
        }
        else
        {
            /*
             * An error occurred at support library initialization time (before the
             * VM could be created). Set the error message directly using the
             * initial callback, as the callback list doesn't exist yet.
             */
            const char *pszError;
            switch (rc)
            {
                case VERR_VM_DRIVER_LOAD_ERROR:
#ifdef RT_OS_LINUX
                    pszError = N_("VirtualBox kernel driver not loaded. The vboxdrv kernel module "
                                  "was either not loaded, /dev/vboxdrv is not set up properly, "
                                  "or you are using EFI Secure Boot and the module is not signed "
                                  "in the right way for your system.  If necessary, try setting up "
                                  "the kernel module again by executing "
                                  "'/sbin/vboxconfig' as root");
#else
                    pszError = N_("VirtualBox kernel driver not loaded");
#endif
                    break;
                case VERR_VM_DRIVER_OPEN_ERROR:
                    pszError = N_("VirtualBox kernel driver cannot be opened");
                    break;
                case VERR_VM_DRIVER_NOT_ACCESSIBLE:
#ifdef VBOX_WITH_HARDENING
                    /* This should only happen if the executable wasn't hardened - bad code/build. */
                    pszError = N_("VirtualBox kernel driver not accessible, permission problem. "
                                  "Re-install VirtualBox. If you are building it yourself, you "
                                  "should make sure it installed correctly and that the setuid "
                                  "bit is set on the executables calling VMR3Create.");
#else
                    /* This should only happen when mixing builds or with the usual /dev/vboxdrv access issues. */
# if defined(RT_OS_DARWIN)
                    pszError = N_("VirtualBox KEXT is not accessible, permission problem. "
                                  "If you have built VirtualBox yourself, make sure that you do not "
                                  "have the vboxdrv KEXT from a different build or installation loaded.");
# elif defined(RT_OS_LINUX)
                    pszError = N_("VirtualBox kernel driver is not accessible, permission problem. "
                                  "If you have built VirtualBox yourself, make sure that you do "
                                  "not have the vboxdrv kernel module from a different build or "
                                  "installation loaded. Also, make sure the vboxdrv udev rule gives "
                                  "you the permission you need to access the device.");
# elif defined(RT_OS_WINDOWS)
                    pszError = N_("VirtualBox kernel driver is not accessible, permission problem.");
# else /* solaris, freebsd, ++. */
                    pszError = N_("VirtualBox kernel module is not accessible, permission problem. "
                                  "If you have built VirtualBox yourself, make sure that you do "
                                  "not have the vboxdrv kernel module from a different install loaded.");
# endif
#endif
                    break;
                case VERR_INVALID_HANDLE: /** @todo track down and fix this error. */
                case VERR_VM_DRIVER_NOT_INSTALLED:
#ifdef RT_OS_LINUX
                    pszError = N_("VirtualBox kernel driver not Installed. The vboxdrv kernel module "
                                  "was either not loaded, /dev/vboxdrv is not set up properly, "
                                  "or you are using EFI Secure Boot and the module is not signed "
                                  "in the right way for your system.  If necessary, try setting up "
                                  "the kernel module again by executing "
                                  "'/sbin/vboxconfig' as root");
#else
                    pszError = N_("VirtualBox kernel driver not installed");
#endif
                    break;
                case VERR_NO_MEMORY:
                    pszError = N_("VirtualBox support library out of memory");
                    break;
                case VERR_VERSION_MISMATCH:
                case VERR_VM_DRIVER_VERSION_MISMATCH:
                    pszError = N_("The VirtualBox support driver which is running is from a different "
                                  "version of VirtualBox.  You can correct this by stopping all "
                                  "running instances of VirtualBox and reinstalling the software.");
                    break;
                default:
                    pszError = N_("Unknown error initializing kernel driver");
                    AssertMsgFailed(("Add error message for rc=%d (%Rrc)\n", rc, rc));
            }
            vmR3SetErrorU(pUVM, rc, RT_SRC_POS, pszError, rc);
        }
    }

    /* cleanup */
    vmR3DestroyUVM(pUVM, 2000);
    LogFlow(("VMR3Create: returns %Rrc\n", rc));
    return rc;
}


/**
 * Creates the UVM.
 *
 * This will not initialize the support library even if vmR3DestroyUVM
 * will terminate that.
 *
 * @returns VBox status code.
 * @param   cCpus               Number of virtual CPUs
 * @param   pVmm2UserMethods    Pointer to the optional VMM -> User method
 *                              table.
 * @param   ppUVM               Where to store the UVM pointer.
 */
static int vmR3CreateUVM(uint32_t cCpus, PCVMM2USERMETHODS pVmm2UserMethods, PUVM *ppUVM)
{
    uint32_t i;

    /*
     * Create and initialize the UVM.
     */
    PUVM pUVM = (PUVM)RTMemPageAllocZ(RT_UOFFSETOF_DYN(UVM, aCpus[cCpus]));
    AssertReturn(pUVM, VERR_NO_MEMORY);
    pUVM->u32Magic          = UVM_MAGIC;
    pUVM->cCpus             = cCpus;
    pUVM->pVmm2UserMethods  = pVmm2UserMethods;

    AssertCompile(sizeof(pUVM->vm.s) <= sizeof(pUVM->vm.padding));

    pUVM->vm.s.cUvmRefs      = 1;
    pUVM->vm.s.ppAtStateNext = &pUVM->vm.s.pAtState;
    pUVM->vm.s.ppAtErrorNext = &pUVM->vm.s.pAtError;
    pUVM->vm.s.ppAtRuntimeErrorNext = &pUVM->vm.s.pAtRuntimeError;

    pUVM->vm.s.enmHaltMethod = VMHALTMETHOD_BOOTSTRAP;
    RTUuidClear(&pUVM->vm.s.Uuid);

    /* Initialize the VMCPU array in the UVM. */
    for (i = 0; i < cCpus; i++)
    {
        pUVM->aCpus[i].pUVM   = pUVM;
        pUVM->aCpus[i].idCpu  = i;
    }

    /* Allocate a TLS entry to store the VMINTUSERPERVMCPU pointer. */
    int rc = RTTlsAllocEx(&pUVM->vm.s.idxTLS, NULL);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        /* Allocate a halt method event semaphore for each VCPU. */
        for (i = 0; i < cCpus; i++)
            pUVM->aCpus[i].vm.s.EventSemWait = NIL_RTSEMEVENT;
        for (i = 0; i < cCpus; i++)
        {
            rc = RTSemEventCreate(&pUVM->aCpus[i].vm.s.EventSemWait);
            if (RT_FAILURE(rc))
                break;
        }
        if (RT_SUCCESS(rc))
        {
            rc = RTCritSectInitNamed(&pUVM->vm.s.AtStateCritSect, "VM-AtStateCritSect");
            if (RT_SUCCESS(rc))
            {
                rc = RTCritSectInitNamed(&pUVM->vm.s.AtErrorCritSect, "VM-AtErrorCritSect");
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Init fundamental (sub-)components - STAM, MMR3Heap and PDMLdr.
                     */
                    rc = PDMR3InitUVM(pUVM);
                    if (RT_SUCCESS(rc))
                    {
                        rc = STAMR3InitUVM(pUVM);
                        if (RT_SUCCESS(rc))
                        {
                            rc = MMR3InitUVM(pUVM);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Start the emulation threads for all VMCPUs.
                                 */
                                for (i = 0; i < cCpus; i++)
                                {
                                    rc = RTThreadCreateF(&pUVM->aCpus[i].vm.s.ThreadEMT, vmR3EmulationThread, &pUVM->aCpus[i],
                                                         _1M, RTTHREADTYPE_EMULATION,
                                                         RTTHREADFLAGS_WAITABLE | RTTHREADFLAGS_COM_MTA | RTTHREADFLAGS_NO_SIGNALS,
                                                         cCpus > 1 ? "EMT-%u" : "EMT", i);
                                    if (RT_FAILURE(rc))
                                        break;

                                    pUVM->aCpus[i].vm.s.NativeThreadEMT = RTThreadGetNative(pUVM->aCpus[i].vm.s.ThreadEMT);
                                }

                                if (RT_SUCCESS(rc))
                                {
                                    *ppUVM = pUVM;
                                    return VINF_SUCCESS;
                                }

                                /* bail out. */
                                while (i-- > 0)
                                {
                                    /** @todo rainy day: terminate the EMTs. */
                                }
                                MMR3TermUVM(pUVM);
                            }
                            STAMR3TermUVM(pUVM);
                        }
                        PDMR3TermUVM(pUVM);
                    }
                    RTCritSectDelete(&pUVM->vm.s.AtErrorCritSect);
                }
                RTCritSectDelete(&pUVM->vm.s.AtStateCritSect);
            }
        }
        for (i = 0; i < cCpus; i++)
        {
            RTSemEventDestroy(pUVM->aCpus[i].vm.s.EventSemWait);
            pUVM->aCpus[i].vm.s.EventSemWait = NIL_RTSEMEVENT;
        }
        RTTlsFree(pUVM->vm.s.idxTLS);
    }
    RTMemPageFree(pUVM, RT_UOFFSETOF_DYN(UVM, aCpus[pUVM->cCpus]));
    return rc;
}


/**
 * Creates and initializes the VM.
 *
 * @thread EMT
 */
static DECLCALLBACK(int) vmR3CreateU(PUVM pUVM, uint32_t cCpus, PFNCFGMCONSTRUCTOR pfnCFGMConstructor, void *pvUserCFGM)
{
#if (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)) && !defined(VBOX_WITH_OLD_CPU_SUPPORT)
    /*
     * Require SSE2 to be present (already checked for in supdrv, so we
     * shouldn't ever really get here).
     */
    if (!(ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_SSE2))
    {
        LogRel(("vboxdrv: Requires SSE2 (cpuid(0).EDX=%#x)\n", ASMCpuId_EDX(1)));
        return VERR_UNSUPPORTED_CPU;
    }
#endif


    /*
     * Load the VMMR0.r0 module so that we can call GVMMR0CreateVM.
     */
    if (!SUPR3IsDriverless())
    {
        int rc = PDMR3LdrLoadVMMR0U(pUVM);
        if (RT_FAILURE(rc))
        {
            /** @todo we need a cleaner solution for this (VERR_VMX_IN_VMX_ROOT_MODE).
              * bird: what about moving the message down here? Main picks the first message, right? */
            if (rc == VERR_VMX_IN_VMX_ROOT_MODE)
                return rc;  /* proper error message set later on */
            return vmR3SetErrorU(pUVM, rc, RT_SRC_POS, N_("Failed to load VMMR0.r0"));
        }
    }

    /*
     * Request GVMM to create a new VM for us.
     */
    RTR0PTR pVMR0;
    int rc = GVMMR3CreateVM(pUVM, cCpus, pUVM->vm.s.pSession, &pUVM->pVM, &pVMR0);
    if (RT_SUCCESS(rc))
    {
        PVM pVM = pUVM->pVM;
        AssertReleaseMsg(RT_VALID_PTR(pVM), ("pVM=%p pVMR0=%p\n", pVM, pVMR0));
        AssertRelease(pVM->pVMR0ForCall == pVMR0);
        AssertRelease(pVM->pSession == pUVM->vm.s.pSession);
        AssertRelease(pVM->cCpus == cCpus);
        AssertRelease(pVM->uCpuExecutionCap == 100);
        AssertCompileMemberAlignment(VM, cpum, 64);
        AssertCompileMemberAlignment(VM, tm, 64);

        Log(("VMR3Create: Created pUVM=%p pVM=%p pVMR0=%p hSelf=%#x cCpus=%RU32\n", pUVM, pVM, pVMR0, pVM->hSelf, pVM->cCpus));

        /*
         * Initialize the VM structure and our internal data (VMINT).
         */
        pVM->pUVM = pUVM;

        for (VMCPUID i = 0; i < pVM->cCpus; i++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[i];
            pVCpu->pUVCpu            = &pUVM->aCpus[i];
            pVCpu->idCpu             = i;
            pVCpu->hNativeThread     = pUVM->aCpus[i].vm.s.NativeThreadEMT;
            pVCpu->hThread           = pUVM->aCpus[i].vm.s.ThreadEMT;
            Assert(pVCpu->hNativeThread != NIL_RTNATIVETHREAD);
            /* hNativeThreadR0 is initialized on EMT registration. */
            pUVM->aCpus[i].pVCpu     = pVCpu;
            pUVM->aCpus[i].pVM       = pVM;
        }

        /*
         * Init the configuration.
         */
        rc = CFGMR3Init(pVM, pfnCFGMConstructor, pvUserCFGM);
        if (RT_SUCCESS(rc))
        {
            rc = vmR3ReadBaseConfig(pVM, pUVM, cCpus);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Init the ring-3 components and ring-3 per cpu data, finishing it off
                 * by a relocation round (intermediate context finalization will do this).
                 */
                rc = vmR3InitRing3(pVM, pUVM);
                if (RT_SUCCESS(rc))
                {
                    LogFlow(("Ring-3 init succeeded\n"));

                    /*
                     * Init the Ring-0 components.
                     */
                    rc = vmR3InitRing0(pVM);
                    if (RT_SUCCESS(rc))
                    {
                        /* Relocate again, because some switcher fixups depends on R0 init results. */
                        VMR3Relocate(pVM, 0 /* offDelta */);

#ifdef VBOX_WITH_DEBUGGER
                        /*
                         * Init the tcp debugger console if we're building
                         * with debugger support.
                         */
                        void *pvUser = NULL;
                        rc = DBGCIoCreate(pUVM, &pvUser);
                        if (    RT_SUCCESS(rc)
                            ||  rc == VERR_NET_ADDRESS_IN_USE)
                        {
                            pUVM->vm.s.pvDBGC = pvUser;
#endif
                            /*
                             * Now we can safely set the VM halt method to default.
                             */
                            rc = vmR3SetHaltMethodU(pUVM, VMHALTMETHOD_DEFAULT);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Set the state and we're done.
                                 */
                                vmR3SetState(pVM, VMSTATE_CREATED, VMSTATE_CREATING);
                                return VINF_SUCCESS;
                            }
#ifdef VBOX_WITH_DEBUGGER
                            DBGCIoTerminate(pUVM, pUVM->vm.s.pvDBGC);
                            pUVM->vm.s.pvDBGC = NULL;
                        }
#endif
                        //..
                    }
                    vmR3Destroy(pVM);
                }
            }
            //..

            /* Clean CFGM. */
            int rc2 = CFGMR3Term(pVM);
            AssertRC(rc2);
        }

        /*
         * Do automatic cleanups while the VM structure is still alive and all
         * references to it are still working.
         */
        PDMR3CritSectBothTerm(pVM);

        /*
         * Drop all references to VM and the VMCPU structures, then
         * tell GVMM to destroy the VM.
         */
        pUVM->pVM = NULL;
        for (VMCPUID i = 0; i < pUVM->cCpus; i++)
        {
            pUVM->aCpus[i].pVM = NULL;
            pUVM->aCpus[i].pVCpu = NULL;
        }
        Assert(pUVM->vm.s.enmHaltMethod == VMHALTMETHOD_BOOTSTRAP);

        if (pUVM->cCpus > 1)
        {
            /* Poke the other EMTs since they may have stale pVM and pVCpu references
               on the stack (see VMR3WaitU for instance) if they've been awakened after
               VM creation. */
            for (VMCPUID i = 1; i < pUVM->cCpus; i++)
                VMR3NotifyCpuFFU(&pUVM->aCpus[i], 0);
            RTThreadSleep(RT_MIN(100 + 25 *(pUVM->cCpus - 1), 500)); /* very sophisticated */
        }

        int rc2 = GVMMR3DestroyVM(pUVM, pVM);
        AssertRC(rc2);
    }
    else
        vmR3SetErrorU(pUVM, rc, RT_SRC_POS, N_("VM creation failed (GVMM)"));

    LogFlow(("vmR3CreateU: returns %Rrc\n", rc));
    return rc;
}


/**
 * Reads the base configuation from CFGM.
 *
 * @returns VBox status code.
 * @param   pVM                The cross context VM structure.
 * @param   pUVM                The user mode VM structure.
 * @param   cCpus               The CPU count given to VMR3Create.
 */
static int vmR3ReadBaseConfig(PVM pVM, PUVM pUVM, uint32_t cCpus)
{
    PCFGMNODE const pRoot = CFGMR3GetRoot(pVM);

    /*
     * Base EM and HM config properties.
     */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    pVM->fHMEnabled = true;
#else /* Other architectures must fall back on IEM for the time being: */
    pVM->fHMEnabled = false;
#endif

    /*
     * Make sure the CPU count in the config data matches.
     */
    uint32_t cCPUsCfg;
    int rc = CFGMR3QueryU32Def(pRoot, "NumCPUs", &cCPUsCfg, 1);
    AssertLogRelMsgRCReturn(rc, ("Configuration error: Querying \"NumCPUs\" as integer failed, rc=%Rrc\n", rc), rc);
    AssertLogRelMsgReturn(cCPUsCfg == cCpus,
                          ("Configuration error: \"NumCPUs\"=%RU32 and VMR3Create::cCpus=%RU32 does not match!\n",
                           cCPUsCfg, cCpus),
                          VERR_INVALID_PARAMETER);

    /*
     * Get the CPU execution cap.
     */
    rc = CFGMR3QueryU32Def(pRoot, "CpuExecutionCap", &pVM->uCpuExecutionCap, 100);
    AssertLogRelMsgRCReturn(rc, ("Configuration error: Querying \"CpuExecutionCap\" as integer failed, rc=%Rrc\n", rc), rc);

    /*
     * Get the VM name and UUID.
     */
    rc = CFGMR3QueryStringAllocDef(pRoot, "Name", &pUVM->vm.s.pszName, "<unknown>");
    AssertLogRelMsgRCReturn(rc, ("Configuration error: Querying \"Name\" failed, rc=%Rrc\n", rc), rc);

    rc = CFGMR3QueryBytes(pRoot, "UUID", &pUVM->vm.s.Uuid, sizeof(pUVM->vm.s.Uuid));
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        rc = VINF_SUCCESS;
    AssertLogRelMsgRCReturn(rc, ("Configuration error: Querying \"UUID\" failed, rc=%Rrc\n", rc), rc);

    rc = CFGMR3QueryBoolDef(pRoot, "PowerOffInsteadOfReset", &pVM->vm.s.fPowerOffInsteadOfReset, false);
    AssertLogRelMsgRCReturn(rc, ("Configuration error: Querying \"PowerOffInsteadOfReset\" failed, rc=%Rrc\n", rc), rc);

    return VINF_SUCCESS;
}


/**
 * Initializes all R3 components of the VM
 */
static int vmR3InitRing3(PVM pVM, PUVM pUVM)
{
    int rc;

    /*
     * Register the other EMTs with GVM.
     */
    for (VMCPUID idCpu = 1; idCpu < pVM->cCpus; idCpu++)
    {
        rc = VMR3ReqCallWait(pVM, idCpu, (PFNRT)GVMMR3RegisterVCpu, 2, pVM, idCpu);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Register statistics.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        rc = STAMR3RegisterF(pVM, &pUVM->aCpus[idCpu].vm.s.StatHaltYield,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "Profiling halted state yielding.",  "/PROF/CPU%d/VM/Halt/Yield", idCpu);
        AssertRC(rc);
        rc = STAMR3RegisterF(pVM, &pUVM->aCpus[idCpu].vm.s.StatHaltBlock,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "Profiling halted state blocking.",  "/PROF/CPU%d/VM/Halt/Block", idCpu);
        AssertRC(rc);
        rc = STAMR3RegisterF(pVM, &pUVM->aCpus[idCpu].vm.s.StatHaltBlockOverslept,  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "Time wasted by blocking too long.", "/PROF/CPU%d/VM/Halt/BlockOverslept", idCpu);
        AssertRC(rc);
        rc = STAMR3RegisterF(pVM, &pUVM->aCpus[idCpu].vm.s.StatHaltBlockInsomnia,   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "Time slept when returning to early.","/PROF/CPU%d/VM/Halt/BlockInsomnia", idCpu);
        AssertRC(rc);
        rc = STAMR3RegisterF(pVM, &pUVM->aCpus[idCpu].vm.s.StatHaltBlockOnTime,     STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "Time slept on time.",                "/PROF/CPU%d/VM/Halt/BlockOnTime", idCpu);
        AssertRC(rc);
        rc = STAMR3RegisterF(pVM, &pUVM->aCpus[idCpu].vm.s.StatHaltTimers,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL, "Profiling halted state timer tasks.", "/PROF/CPU%d/VM/Halt/Timers", idCpu);
        AssertRC(rc);
    }

    STAM_REG(pVM, &pUVM->vm.s.StatReqAllocNew,   STAMTYPE_COUNTER,     "/VM/Req/AllocNew",       STAMUNIT_OCCURENCES,        "Number of VMR3ReqAlloc returning a new packet.");
    STAM_REG(pVM, &pUVM->vm.s.StatReqAllocRaces, STAMTYPE_COUNTER,     "/VM/Req/AllocRaces",     STAMUNIT_OCCURENCES,        "Number of VMR3ReqAlloc causing races.");
    STAM_REG(pVM, &pUVM->vm.s.StatReqAllocRecycled, STAMTYPE_COUNTER,  "/VM/Req/AllocRecycled",  STAMUNIT_OCCURENCES,        "Number of VMR3ReqAlloc returning a recycled packet.");
    STAM_REG(pVM, &pUVM->vm.s.StatReqFree,       STAMTYPE_COUNTER,     "/VM/Req/Free",           STAMUNIT_OCCURENCES,        "Number of VMR3ReqFree calls.");
    STAM_REG(pVM, &pUVM->vm.s.StatReqFreeOverflow, STAMTYPE_COUNTER,   "/VM/Req/FreeOverflow",   STAMUNIT_OCCURENCES,        "Number of times the request was actually freed.");
    STAM_REG(pVM, &pUVM->vm.s.StatReqProcessed,  STAMTYPE_COUNTER,     "/VM/Req/Processed",      STAMUNIT_OCCURENCES,        "Number of processed requests (any queue).");
    STAM_REG(pVM, &pUVM->vm.s.StatReqMoreThan1,  STAMTYPE_COUNTER,     "/VM/Req/MoreThan1",      STAMUNIT_OCCURENCES,        "Number of times there are more than one request on the queue when processing it.");
    STAM_REG(pVM, &pUVM->vm.s.StatReqPushBackRaces, STAMTYPE_COUNTER,  "/VM/Req/PushBackRaces",  STAMUNIT_OCCURENCES,        "Number of push back races.");

    /* Statistics for ring-0 components: */
    STAM_REL_REG(pVM, &pVM->R0Stats.gmm.cChunkTlbHits,   STAMTYPE_COUNTER, "/GMM/ChunkTlbHits",   STAMUNIT_OCCURENCES, "GMMR0PageIdToVirt chunk TBL hits");
    STAM_REL_REG(pVM, &pVM->R0Stats.gmm.cChunkTlbMisses, STAMTYPE_COUNTER, "/GMM/ChunkTlbMisses", STAMUNIT_OCCURENCES, "GMMR0PageIdToVirt chunk TBL misses");

    /*
     * Init all R3 components, the order here might be important.
     * NEM and HM shall be initialized first!
     */
    Assert(pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NOT_SET);
    rc = NEMR3InitConfig(pVM);
    if (RT_SUCCESS(rc))
        rc = HMR3Init(pVM);
    if (RT_SUCCESS(rc))
    {
        ASMCompilerBarrier(); /* HMR3Init will have modified const member bMainExecutionEngine. */
        Assert(   pVM->bMainExecutionEngine == VM_EXEC_ENGINE_HW_VIRT
               || pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API
               || pVM->bMainExecutionEngine == VM_EXEC_ENGINE_IEM);
        rc = MMR3Init(pVM);
        if (RT_SUCCESS(rc))
        {
            rc = CPUMR3Init(pVM);
            if (RT_SUCCESS(rc))
            {
                rc = NEMR3InitAfterCPUM(pVM);
                if (RT_SUCCESS(rc))
                    rc = PGMR3Init(pVM);
                if (RT_SUCCESS(rc))
                {
                    rc = MMR3InitPaging(pVM);
                    if (RT_SUCCESS(rc))
                        rc = TMR3Init(pVM);
                    if (RT_SUCCESS(rc))
                    {
                        rc = VMMR3Init(pVM);
                        if (RT_SUCCESS(rc))
                        {
#if !defined(VBOX_VMM_TARGET_ARMV8)
                            rc = SELMR3Init(pVM);
#endif
                            if (RT_SUCCESS(rc))
                            {
                                rc = TRPMR3Init(pVM);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = SSMR3RegisterStub(pVM, "CSAM", 0);
                                    if (RT_SUCCESS(rc))
                                    {
                                        rc = SSMR3RegisterStub(pVM, "PATM", 0);
                                        if (RT_SUCCESS(rc))
                                        {
                                            rc = IOMR3Init(pVM);
                                            if (RT_SUCCESS(rc))
                                            {
                                                rc = EMR3Init(pVM);
                                                if (RT_SUCCESS(rc))
                                                {
                                                    rc = IEMR3Init(pVM);
                                                    if (RT_SUCCESS(rc))
                                                    {
                                                        rc = DBGFR3Init(pVM);
                                                        if (RT_SUCCESS(rc))
                                                        {
                                                            /* GIM must be init'd before PDM, gimdevR3Construct()
                                                               requires GIM provider to be setup. */
                                                            rc = GIMR3Init(pVM);
                                                            if (RT_SUCCESS(rc))
                                                            {
#if !defined(VBOX_VMM_TARGET_ARMV8)
                                                                rc = GCMR3Init(pVM);
#endif
                                                                if (RT_SUCCESS(rc))
                                                                {
                                                                    rc = PDMR3Init(pVM);
                                                                    if (RT_SUCCESS(rc))
                                                                    {
                                                                        rc = PGMR3InitFinalize(pVM);
                                                                        if (RT_SUCCESS(rc))
                                                                            rc = TMR3InitFinalize(pVM);
                                                                        if (RT_SUCCESS(rc))
                                                                        {
                                                                            PGMR3MemSetup(pVM, false /*fAtReset*/);
                                                                            PDMR3MemSetup(pVM, false /*fAtReset*/);
                                                                        }
                                                                        if (RT_SUCCESS(rc))
                                                                            rc = vmR3InitDoCompleted(pVM, VMINITCOMPLETED_RING3);
                                                                        if (RT_SUCCESS(rc))
                                                                        {
                                                                            LogFlow(("vmR3InitRing3: returns %Rrc\n", VINF_SUCCESS));
                                                                            return VINF_SUCCESS;
                                                                        }

                                                                        int rc2 = PDMR3Term(pVM);
                                                                        AssertRC(rc2);
                                                                    }
#if !defined(VBOX_VMM_TARGET_ARMV8)
                                                                    int rc2 = GCMR3Term(pVM);
                                                                    AssertRC(rc2);
#endif
                                                                }
                                                                int rc2 = GIMR3Term(pVM);
                                                                AssertRC(rc2);
                                                            }
                                                            int rc2 = DBGFR3Term(pVM);
                                                            AssertRC(rc2);
                                                        }
                                                        int rc2 = IEMR3Term(pVM);
                                                        AssertRC(rc2);
                                                    }
                                                    int rc2 = EMR3Term(pVM);
                                                    AssertRC(rc2);
                                                }
                                                int rc2 = IOMR3Term(pVM);
                                                AssertRC(rc2);
                                            }
                                        }
                                    }
                                    int rc2 = TRPMR3Term(pVM);
                                    AssertRC(rc2);
                                }
#if !defined(VBOX_VMM_TARGET_ARMV8)
                                int rc2 = SELMR3Term(pVM);
                                AssertRC(rc2);
#endif
                            }
                            int rc2 = VMMR3Term(pVM);
                            AssertRC(rc2);
                        }
                        int rc2 = TMR3Term(pVM);
                        AssertRC(rc2);
                    }
                    int rc2 = PGMR3Term(pVM);
                    AssertRC(rc2);
                }
                //int rc2 = CPUMR3Term(pVM);
                //AssertRC(rc2);
            }
            /* MMR3Term is not called here because it'll kill the heap. */
        }
        int rc2 = HMR3Term(pVM);
        AssertRC(rc2);
    }
    NEMR3Term(pVM);

    LogFlow(("vmR3InitRing3: returns %Rrc\n", rc));
    return rc;
}


/**
 * Initializes all R0 components of the VM.
 */
static int vmR3InitRing0(PVM pVM)
{
    LogFlow(("vmR3InitRing0:\n"));

    /*
     * Check for FAKE suplib mode.
     */
    int rc = VINF_SUCCESS;
    const char *psz = RTEnvGet("VBOX_SUPLIB_FAKE");
    if (!psz || strcmp(psz, "fake"))
    {
        /*
         * Call the VMMR0 component and let it do the init.
         */
        rc = VMMR3InitR0(pVM);
    }
    else
        Log(("vmR3InitRing0: skipping because of VBOX_SUPLIB_FAKE=fake\n"));

    /*
     * Do notifications and return.
     */
    if (RT_SUCCESS(rc))
        rc = vmR3InitDoCompleted(pVM, VMINITCOMPLETED_RING0);
    if (RT_SUCCESS(rc))
        rc = vmR3InitDoCompleted(pVM, VMINITCOMPLETED_HM);

    LogFlow(("vmR3InitRing0: returns %Rrc\n", rc));
    return rc;
}


/**
 * Do init completed notifications.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmWhat     What's completed.
 */
static int vmR3InitDoCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    int rc = VMMR3InitCompleted(pVM, enmWhat);
    if (RT_SUCCESS(rc))
        rc = HMR3InitCompleted(pVM, enmWhat);
    if (RT_SUCCESS(rc))
        rc = NEMR3InitCompleted(pVM, enmWhat);
    if (RT_SUCCESS(rc))
        rc = PGMR3InitCompleted(pVM, enmWhat);
    if (RT_SUCCESS(rc))
        rc = CPUMR3InitCompleted(pVM, enmWhat);
    if (RT_SUCCESS(rc))
        rc = EMR3InitCompleted(pVM, enmWhat);
    if (enmWhat == VMINITCOMPLETED_RING3)
    {
        if (RT_SUCCESS(rc))
            rc = SSMR3RegisterStub(pVM, "rem", 1);
    }
    if (RT_SUCCESS(rc))
        rc = PDMR3InitCompleted(pVM, enmWhat);

    /* IOM *must* come after PDM, as device (DevPcArch) may register some final
       handlers in their init completion method. */
    if (RT_SUCCESS(rc))
        rc = IOMR3InitCompleted(pVM, enmWhat);
    return rc;
}


/**
 * Calls the relocation functions for all VMM components so they can update
 * any GC pointers. When this function is called all the basic VM members
 * have been updated  and the actual memory relocation have been done
 * by the PGM/MM.
 *
 * This is used both on init and on runtime relocations.
 *
 * @param   pVM         The cross context VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMMR3_INT_DECL(void) VMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    LogFlow(("VMR3Relocate: offDelta=%RGv\n", offDelta));

    /*
     * The order here is very important!
     */
    PGMR3Relocate(pVM, offDelta);
    PDMR3LdrRelocateU(pVM->pUVM, offDelta);
    PGMR3Relocate(pVM, 0);              /* Repeat after PDM relocation. */
    CPUMR3Relocate(pVM);
    HMR3Relocate(pVM);
#if !defined(VBOX_VMM_TARGET_ARMV8)
    SELMR3Relocate(pVM);
#endif
    VMMR3Relocate(pVM, offDelta);
#if !defined(VBOX_VMM_TARGET_ARMV8)
    SELMR3Relocate(pVM);                /* !hack! fix stack! */
#endif
    TRPMR3Relocate(pVM, offDelta);
    IOMR3Relocate(pVM, offDelta);
    EMR3Relocate(pVM);
    TMR3Relocate(pVM, offDelta);
    IEMR3Relocate(pVM);
    DBGFR3Relocate(pVM, offDelta);
    PDMR3Relocate(pVM, offDelta);
    GIMR3Relocate(pVM, offDelta);
}


/**
 * EMT rendezvous worker for VMR3PowerOn.
 *
 * @returns VERR_VM_INVALID_VM_STATE or VINF_SUCCESS. (This is a strict return
 *          code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          Ignored.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3PowerOn(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    LogFlow(("vmR3PowerOn: pVM=%p pVCpu=%p/#%u\n", pVM, pVCpu, pVCpu->idCpu));
    Assert(!pvUser); NOREF(pvUser);

    /*
     * The first thread thru here tries to change the state.  We shouldn't be
     * called again if this fails.
     */
    if (pVCpu->idCpu == pVM->cCpus - 1)
    {
        int rc = vmR3TrySetState(pVM, "VMR3PowerOn", 1, VMSTATE_POWERING_ON, VMSTATE_CREATED);
        if (RT_FAILURE(rc))
            return rc;
    }

    VMSTATE enmVMState = VMR3GetState(pVM);
    AssertMsgReturn(enmVMState == VMSTATE_POWERING_ON,
                    ("%s\n", VMR3GetStateName(enmVMState)),
                    VERR_VM_UNEXPECTED_UNSTABLE_STATE);

    /*
     * All EMTs changes their state to started.
     */
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED);

    /*
     * EMT(0) is last thru here and it will make the notification calls
     * and advance the state.
     */
    if (pVCpu->idCpu == 0)
    {
        PDMR3PowerOn(pVM);
        vmR3SetState(pVM, VMSTATE_RUNNING, VMSTATE_POWERING_ON);
    }

    return VINF_SUCCESS;
}


/**
 * Powers on the virtual machine.
 *
 * @returns VBox status code.
 *
 * @param   pUVM        The VM to power on.
 *
 * @thread      Any thread.
 * @vmstate     Created
 * @vmstateto   PoweringOn+Running
 */
VMMR3DECL(int) VMR3PowerOn(PUVM pUVM)
{
    LogFlow(("VMR3PowerOn: pUVM=%p\n", pUVM));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Gather all the EMTs to reduce the init TSC drift and keep
     * the state changing APIs a bit uniform.
     */
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                vmR3PowerOn, NULL);
    LogFlow(("VMR3PowerOn: returns %Rrc\n", rc));
    return rc;
}


/**
 * Does the suspend notifications.
 *
 * @param  pVM      The cross context VM structure.
 * @thread  EMT(0)
 */
static void vmR3SuspendDoWork(PVM pVM)
{
    PDMR3Suspend(pVM);
}


/**
 * EMT rendezvous worker for VMR3Suspend.
 *
 * @returns VERR_VM_INVALID_VM_STATE or VINF_EM_SUSPEND. (This is a strict
 *          return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          Ignored.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3Suspend(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    VMSUSPENDREASON enmReason = (VMSUSPENDREASON)(uintptr_t)pvUser;
    LogFlow(("vmR3Suspend: pVM=%p pVCpu=%p/#%u enmReason=%d\n", pVM, pVCpu, pVCpu->idCpu, enmReason));

    /*
     * The first EMT switches the state to suspending.  If this fails because
     * something was racing us in one way or the other, there will be no more
     * calls and thus the state assertion below is not going to annoy anyone.
     *
     * Note! Changes to the state transition here needs to be reflected in the
     *       checks in vmR3SetRuntimeErrorCommon!
     */
    if (pVCpu->idCpu == pVM->cCpus - 1)
    {
        int rc = vmR3TrySetState(pVM, "VMR3Suspend", 2,
                                 VMSTATE_SUSPENDING,        VMSTATE_RUNNING,
                                 VMSTATE_SUSPENDING_EXT_LS, VMSTATE_RUNNING_LS);
        if (RT_FAILURE(rc))
            return rc;
        pVM->pUVM->vm.s.enmSuspendReason = enmReason;
    }

    VMSTATE enmVMState = VMR3GetState(pVM);
    AssertMsgReturn(    enmVMState == VMSTATE_SUSPENDING
                    ||  enmVMState == VMSTATE_SUSPENDING_EXT_LS,
                    ("%s\n", VMR3GetStateName(enmVMState)),
                    VERR_VM_UNEXPECTED_UNSTABLE_STATE);

    /*
     * EMT(0) does the actually suspending *after* all the other CPUs have
     * been thru here.
     */
    if (pVCpu->idCpu == 0)
    {
        vmR3SuspendDoWork(pVM);

        int rc = vmR3TrySetState(pVM, "VMR3Suspend", 2,
                                 VMSTATE_SUSPENDED,        VMSTATE_SUSPENDING,
                                 VMSTATE_SUSPENDED_EXT_LS, VMSTATE_SUSPENDING_EXT_LS);
        if (RT_FAILURE(rc))
            return VERR_VM_UNEXPECTED_UNSTABLE_STATE;
    }

    return VINF_EM_SUSPEND;
}


/**
 * Suspends a running VM.
 *
 * @returns VBox status code. When called on EMT, this will be a strict status
 *          code that has to be propagated up the call stack.
 *
 * @param   pUVM        The VM to suspend.
 * @param   enmReason   The reason for suspending.
 *
 * @thread      Any thread.
 * @vmstate     Running or RunningLS
 * @vmstateto   Suspending + Suspended or SuspendingExtLS + SuspendedExtLS
 */
VMMR3DECL(int) VMR3Suspend(PUVM pUVM, VMSUSPENDREASON enmReason)
{
    LogFlow(("VMR3Suspend: pUVM=%p\n", pUVM));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(enmReason > VMSUSPENDREASON_INVALID && enmReason < VMSUSPENDREASON_END, VERR_INVALID_PARAMETER);

    /*
     * Gather all the EMTs to make sure there are no races before
     * changing the VM state.
     */
    int rc = VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                vmR3Suspend, (void *)(uintptr_t)enmReason);
    LogFlow(("VMR3Suspend: returns %Rrc\n", rc));
    return rc;
}


/**
 * Retrieves the reason for the most recent suspend.
 *
 * @returns Suspend reason. VMSUSPENDREASON_INVALID if no suspend has been done
 *          or the handle is invalid.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(VMSUSPENDREASON) VMR3GetSuspendReason(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VMSUSPENDREASON_INVALID);
    return pUVM->vm.s.enmSuspendReason;
}


/**
 * EMT rendezvous worker for VMR3Resume.
 *
 * @returns VERR_VM_INVALID_VM_STATE or VINF_EM_RESUME. (This is a strict
 *          return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          Reason.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3Resume(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    VMRESUMEREASON enmReason = (VMRESUMEREASON)(uintptr_t)pvUser;
    LogFlow(("vmR3Resume: pVM=%p pVCpu=%p/#%u enmReason=%d\n", pVM, pVCpu, pVCpu->idCpu, enmReason));

    /*
     * The first thread thru here tries to change the state.  We shouldn't be
     * called again if this fails.
     */
    if (pVCpu->idCpu == pVM->cCpus - 1)
    {
        int rc = vmR3TrySetState(pVM, "VMR3Resume", 1, VMSTATE_RESUMING, VMSTATE_SUSPENDED);
        if (RT_FAILURE(rc))
            return rc;
        pVM->pUVM->vm.s.enmResumeReason = enmReason;
    }

    VMSTATE enmVMState = VMR3GetState(pVM);
    AssertMsgReturn(enmVMState == VMSTATE_RESUMING,
                    ("%s\n", VMR3GetStateName(enmVMState)),
                    VERR_VM_UNEXPECTED_UNSTABLE_STATE);

#if 0
    /*
     * All EMTs changes their state to started.
     */
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED);
#endif

    /*
     * EMT(0) is last thru here and it will make the notification calls
     * and advance the state.
     */
    if (pVCpu->idCpu == 0)
    {
        PDMR3Resume(pVM);
        vmR3SetState(pVM, VMSTATE_RUNNING, VMSTATE_RESUMING);
        pVM->vm.s.fTeleportedAndNotFullyResumedYet = false;
    }

    return VINF_EM_RESUME;
}


/**
 * Resume VM execution.
 *
 * @returns VBox status code. When called on EMT, this will be a strict status
 *          code that has to be propagated up the call stack.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   enmReason   The reason we're resuming.
 *
 * @thread      Any thread.
 * @vmstate     Suspended
 * @vmstateto   Running
 */
VMMR3DECL(int) VMR3Resume(PUVM pUVM, VMRESUMEREASON enmReason)
{
    LogFlow(("VMR3Resume: pUVM=%p\n", pUVM));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(enmReason > VMRESUMEREASON_INVALID && enmReason < VMRESUMEREASON_END, VERR_INVALID_PARAMETER);

    /*
     * Gather all the EMTs to make sure there are no races before
     * changing the VM state.
     */
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                vmR3Resume, (void *)(uintptr_t)enmReason);
    LogFlow(("VMR3Resume: returns %Rrc\n", rc));
    return rc;
}


/**
 * Retrieves the reason for the most recent resume.
 *
 * @returns Resume reason. VMRESUMEREASON_INVALID if no suspend has been
 *          done or the handle is invalid.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(VMRESUMEREASON) VMR3GetResumeReason(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VMRESUMEREASON_INVALID);
    return pUVM->vm.s.enmResumeReason;
}


/**
 * EMT rendezvous worker for VMR3Save and VMR3Teleport that suspends the VM
 * after the live step has been completed.
 *
 * @returns VERR_VM_INVALID_VM_STATE or VINF_EM_RESUME. (This is a strict
 *          return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          The pfSuspended argument of vmR3SaveTeleport.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3LiveDoSuspend(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    LogFlow(("vmR3LiveDoSuspend: pVM=%p pVCpu=%p/#%u\n", pVM, pVCpu, pVCpu->idCpu));
    bool *pfSuspended = (bool *)pvUser;

    /*
     * The first thread thru here tries to change the state.  We shouldn't be
     * called again if this fails.
     */
    if (pVCpu->idCpu == pVM->cCpus - 1U)
    {
        PUVM     pUVM = pVM->pUVM;
        int      rc;

        RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);
        VMSTATE enmVMState = pVM->enmVMState;
        switch (enmVMState)
        {
            case VMSTATE_RUNNING_LS:
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_SUSPENDING_LS, VMSTATE_RUNNING_LS, false /*fSetRatherThanClearFF*/);
                rc = VINF_SUCCESS;
                break;

            case VMSTATE_SUSPENDED_EXT_LS:
            case VMSTATE_SUSPENDED_LS:          /* (via reset) */
                rc = VINF_SUCCESS;
                break;

            case VMSTATE_DEBUGGING_LS:
                rc = VERR_TRY_AGAIN;
                break;

            case VMSTATE_OFF_LS:
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_OFF, VMSTATE_OFF_LS, false /*fSetRatherThanClearFF*/);
                rc = VERR_SSM_LIVE_POWERED_OFF;
                break;

            case VMSTATE_FATAL_ERROR_LS:
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_FATAL_ERROR, VMSTATE_FATAL_ERROR_LS, false /*fSetRatherThanClearFF*/);
                rc = VERR_SSM_LIVE_FATAL_ERROR;
                break;

            case VMSTATE_GURU_MEDITATION_LS:
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_GURU_MEDITATION, VMSTATE_GURU_MEDITATION_LS, false /*fSetRatherThanClearFF*/);
                rc = VERR_SSM_LIVE_GURU_MEDITATION;
                break;

            case VMSTATE_POWERING_OFF_LS:
            case VMSTATE_SUSPENDING_EXT_LS:
            case VMSTATE_RESETTING_LS:
            default:
                AssertMsgFailed(("%s\n", VMR3GetStateName(enmVMState)));
                rc = VERR_VM_UNEXPECTED_VM_STATE;
                break;
        }
        RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);
        if (RT_FAILURE(rc))
        {
            LogFlow(("vmR3LiveDoSuspend: returns %Rrc (state was %s)\n", rc, VMR3GetStateName(enmVMState)));
            return rc;
        }
    }

    VMSTATE enmVMState = VMR3GetState(pVM);
    AssertMsgReturn(enmVMState == VMSTATE_SUSPENDING_LS,
                    ("%s\n", VMR3GetStateName(enmVMState)),
                    VERR_VM_UNEXPECTED_UNSTABLE_STATE);

    /*
     * Only EMT(0) have work to do since it's last thru here.
     */
    if (pVCpu->idCpu == 0)
    {
        vmR3SuspendDoWork(pVM);
        int rc = vmR3TrySetState(pVM, "VMR3Suspend", 1,
                                 VMSTATE_SUSPENDED_LS, VMSTATE_SUSPENDING_LS);
        if (RT_FAILURE(rc))
            return VERR_VM_UNEXPECTED_UNSTABLE_STATE;

        *pfSuspended = true;
    }

    return VINF_EM_SUSPEND;
}


/**
 * EMT rendezvous worker that VMR3Save and VMR3Teleport uses to clean up a
 * SSMR3LiveDoStep1 failure.
 *
 * Doing this as a rendezvous operation avoids all annoying transition
 * states.
 *
 * @returns VERR_VM_INVALID_VM_STATE, VINF_SUCCESS or some specific VERR_SSM_*
 *          status code. (This is a strict return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          The pfSuspended argument of vmR3SaveTeleport.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3LiveDoStep1Cleanup(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    LogFlow(("vmR3LiveDoStep1Cleanup: pVM=%p pVCpu=%p/#%u\n", pVM, pVCpu, pVCpu->idCpu));
    bool *pfSuspended = (bool *)pvUser;
    NOREF(pVCpu);

    int rc = vmR3TrySetState(pVM, "vmR3LiveDoStep1Cleanup", 8,
                             VMSTATE_OFF,               VMSTATE_OFF_LS,                     /* 1 */
                             VMSTATE_FATAL_ERROR,       VMSTATE_FATAL_ERROR_LS,             /* 2 */
                             VMSTATE_GURU_MEDITATION,   VMSTATE_GURU_MEDITATION_LS,         /* 3 */
                             VMSTATE_SUSPENDED,         VMSTATE_SUSPENDED_LS,               /* 4 */
                             VMSTATE_SUSPENDED,         VMSTATE_SAVING,
                             VMSTATE_SUSPENDED,         VMSTATE_SUSPENDED_EXT_LS,
                             VMSTATE_RUNNING,           VMSTATE_RUNNING_LS,
                             VMSTATE_DEBUGGING,         VMSTATE_DEBUGGING_LS);
    if (rc == 1)
        rc = VERR_SSM_LIVE_POWERED_OFF;
    else if (rc == 2)
        rc = VERR_SSM_LIVE_FATAL_ERROR;
    else if (rc == 3)
        rc = VERR_SSM_LIVE_GURU_MEDITATION;
    else if (rc == 4)
    {
        *pfSuspended = true;
        rc = VINF_SUCCESS;
    }
    else if (rc > 0)
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * EMT(0) worker for VMR3Save and VMR3Teleport that completes the live save.
 *
 * @returns VBox status code.
 * @retval  VINF_SSM_LIVE_SUSPENDED if VMR3Suspend was called.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The handle of saved state operation.
 *
 * @thread  EMT(0)
 */
static DECLCALLBACK(int) vmR3LiveDoStep2(PVM pVM, PSSMHANDLE pSSM)
{
    LogFlow(("vmR3LiveDoStep2: pVM=%p pSSM=%p\n", pVM, pSSM));
    VM_ASSERT_EMT0(pVM);

    /*
     * Advance the state and mark if VMR3Suspend was called.
     */
    int rc = VINF_SUCCESS;
    VMSTATE enmVMState = VMR3GetState(pVM);
    if (enmVMState == VMSTATE_SUSPENDED_LS)
        vmR3SetState(pVM, VMSTATE_SAVING, VMSTATE_SUSPENDED_LS);
    else
    {
        if (enmVMState != VMSTATE_SAVING)
            vmR3SetState(pVM, VMSTATE_SAVING, VMSTATE_SUSPENDED_EXT_LS);
        rc = VINF_SSM_LIVE_SUSPENDED;
    }

    /*
     * Finish up and release the handle. Careful with the status codes.
     */
    int rc2 = SSMR3LiveDoStep2(pSSM);
    if (rc == VINF_SUCCESS || (RT_FAILURE(rc2) && RT_SUCCESS(rc)))
        rc = rc2;

    rc2 = SSMR3LiveDone(pSSM);
    if (rc == VINF_SUCCESS || (RT_FAILURE(rc2) && RT_SUCCESS(rc)))
        rc = rc2;

    /*
     * Advance to the final state and return.
     */
    vmR3SetState(pVM, VMSTATE_SUSPENDED, VMSTATE_SAVING);
    Assert(rc > VINF_EM_LAST || rc < VINF_EM_FIRST);
    return rc;
}


/**
 * Worker for vmR3SaveTeleport that validates the state and calls SSMR3Save or
 * SSMR3LiveSave.
 *
 * @returns VBox status code.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   cMsMaxDowntime      The maximum downtime given as milliseconds.
 * @param   pszFilename         The name of the file.  NULL if pStreamOps is used.
 * @param   pStreamOps          The stream methods.  NULL if pszFilename is used.
 * @param   pvStreamOpsUser     The user argument to the stream methods.
 * @param   enmAfter            What to do afterwards.
 * @param   pfnProgress         Progress callback. Optional.
 * @param   pvProgressUser      User argument for the progress callback.
 * @param   ppSSM               Where to return the saved state handle in case of a
 *                              live snapshot scenario.
 *
 * @thread  EMT
 */
static DECLCALLBACK(int) vmR3Save(PVM pVM, uint32_t cMsMaxDowntime, const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                                  SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvProgressUser, PSSMHANDLE *ppSSM)
{
    int rc = VINF_SUCCESS;

    LogFlow(("vmR3Save: pVM=%p cMsMaxDowntime=%u pszFilename=%p:{%s} pStreamOps=%p pvStreamOpsUser=%p enmAfter=%d pfnProgress=%p pvProgressUser=%p ppSSM=%p\n",
             pVM, cMsMaxDowntime, pszFilename, pszFilename, pStreamOps, pvStreamOpsUser, enmAfter, pfnProgress, pvProgressUser, ppSSM));

    /*
     * Validate input.
     */
    AssertPtrNull(pszFilename);
    AssertPtrNull(pStreamOps);
    AssertPtr(pVM);
    Assert(   enmAfter == SSMAFTER_DESTROY
           || enmAfter == SSMAFTER_CONTINUE
           || enmAfter == SSMAFTER_TELEPORT);
    AssertPtr(ppSSM);
    *ppSSM = NULL;

    /*
     * Change the state and perform/start the saving.
     */
    rc = vmR3TrySetState(pVM, "VMR3Save", 2,
                         VMSTATE_SAVING,     VMSTATE_SUSPENDED,
                         VMSTATE_RUNNING_LS, VMSTATE_RUNNING);
    if (rc == 1 && enmAfter != SSMAFTER_TELEPORT)
    {
        rc = SSMR3Save(pVM, pszFilename, pStreamOps, pvStreamOpsUser, enmAfter, pfnProgress, pvProgressUser);
        vmR3SetState(pVM, VMSTATE_SUSPENDED, VMSTATE_SAVING);
    }
    else if (rc == 2 || enmAfter == SSMAFTER_TELEPORT)
    {
        if (enmAfter == SSMAFTER_TELEPORT)
            pVM->vm.s.fTeleportedAndNotFullyResumedYet = true;
        rc = SSMR3LiveSave(pVM, cMsMaxDowntime, pszFilename, pStreamOps, pvStreamOpsUser,
                           enmAfter, pfnProgress, pvProgressUser, ppSSM);
        /* (We're not subject to cancellation just yet.) */
    }
    else
        Assert(RT_FAILURE(rc));
    return rc;
}


/**
 * Common worker for VMR3Save and VMR3Teleport.
 *
 * @returns VBox status code.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   cMsMaxDowntime      The maximum downtime given as milliseconds.
 * @param   pszFilename         The name of the file.  NULL if pStreamOps is used.
 * @param   pStreamOps          The stream methods.  NULL if pszFilename is used.
 * @param   pvStreamOpsUser     The user argument to the stream methods.
 * @param   enmAfter            What to do afterwards.
 * @param   pfnProgress         Progress callback. Optional.
 * @param   pvProgressUser      User argument for the progress callback.
 * @param   pfSuspended         Set if we suspended the VM.
 *
 * @thread  Non-EMT
 */
static int vmR3SaveTeleport(PVM pVM, uint32_t cMsMaxDowntime,
                            const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                            SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvProgressUser, bool *pfSuspended)
{
    /*
     * Request the operation in EMT(0).
     */
    PSSMHANDLE pSSM;
    int rc = VMR3ReqCallWait(pVM, 0 /*idDstCpu*/,
                             (PFNRT)vmR3Save, 9, pVM, cMsMaxDowntime, pszFilename, pStreamOps, pvStreamOpsUser,
                             enmAfter, pfnProgress, pvProgressUser, &pSSM);
    if (   RT_SUCCESS(rc)
        && pSSM)
    {
        /*
         * Live snapshot.
         *
         * The state handling here is kind of tricky, doing it on EMT(0) helps
         * a bit. See the VMSTATE diagram for details.
         */
        rc = SSMR3LiveDoStep1(pSSM);
        if (RT_SUCCESS(rc))
        {
            if (VMR3GetState(pVM) != VMSTATE_SAVING)
                for (;;)
                {
                    /* Try suspend the VM. */
                    rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                            vmR3LiveDoSuspend, pfSuspended);
                    if (rc != VERR_TRY_AGAIN)
                        break;

                    /* Wait for the state to change. */
                    RTThreadSleep(250); /** @todo Live Migration: fix this polling wait by some smart use of multiple release event  semaphores.. */
                }
            if (RT_SUCCESS(rc))
                rc = VMR3ReqCallWait(pVM, 0 /*idDstCpu*/, (PFNRT)vmR3LiveDoStep2, 2, pVM, pSSM);
            else
            {
                int rc2 = VMR3ReqCallWait(pVM, 0 /*idDstCpu*/, (PFNRT)SSMR3LiveDone, 1, pSSM);
                AssertMsg(rc2 == rc, ("%Rrc != %Rrc\n", rc2, rc)); NOREF(rc2);
            }
        }
        else
        {
            int rc2 = VMR3ReqCallWait(pVM, 0 /*idDstCpu*/, (PFNRT)SSMR3LiveDone, 1, pSSM);
            AssertMsg(rc2 == rc, ("%Rrc != %Rrc\n", rc2, rc));

            rc2 = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE, vmR3LiveDoStep1Cleanup, pfSuspended);
            if (RT_FAILURE(rc2) && rc == VERR_SSM_CANCELLED)
                rc = rc2;
        }
    }

    return rc;
}


/**
 * Save current VM state.
 *
 * Can be used for both saving the state and creating snapshots.
 *
 * When called for a VM in the Running state, the saved state is created live
 * and the VM is only suspended when the final part of the saving is preformed.
 * The VM state will not be restored to Running in this case and it's up to the
 * caller to call VMR3Resume if this is desirable.  (The rational is that the
 * caller probably wish to reconfigure the disks before resuming the VM.)
 *
 * @returns VBox status code.
 *
 * @param   pUVM                The VM which state should be saved.
 * @param   pszFilename         The name of the save state file.
 * @param   pStreamOps          The stream methods.  NULL if pszFilename is used.
 * @param   pvStreamOpsUser     The user argument to the stream methods.
 * @param   fContinueAfterwards Whether continue execution afterwards or not.
 *                              When in doubt, set this to true.
 * @param   pfnProgress         Progress callback. Optional.
 * @param   pvUser              User argument for the progress callback.
 * @param   pfSuspended         Set if we suspended the VM.
 *
 * @thread      Non-EMT.
 * @vmstate     Suspended or Running
 * @vmstateto   Saving+Suspended or
 *              RunningLS+SuspendingLS+SuspendedLS+Saving+Suspended.
 */
VMMR3DECL(int) VMR3Save(PUVM pUVM, const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                        bool fContinueAfterwards, PFNVMPROGRESS pfnProgress, void *pvUser,
                        bool *pfSuspended)
{
    LogFlow(("VMR3Save: pUVM=%p pszFilename=%p:{%s} fContinueAfterwards=%RTbool pfnProgress=%p pvUser=%p pfSuspended=%p\n",
             pUVM, pszFilename, pszFilename, fContinueAfterwards, pfnProgress, pvUser, pfSuspended));

    /*
     * Validate input.
     */
    AssertPtr(pfSuspended);
    *pfSuspended = false;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_OTHER_THREAD(pVM);
    AssertReturn(pszFilename || pStreamOps, VERR_INVALID_POINTER);
    AssertReturn(   (!pStreamOps && *pszFilename)
                 || pStreamOps,
                 VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnProgress, VERR_INVALID_POINTER);

    /*
     * Join paths with VMR3Teleport.
     */
    SSMAFTER enmAfter = fContinueAfterwards ? SSMAFTER_CONTINUE : SSMAFTER_DESTROY;
    int rc = vmR3SaveTeleport(pVM, 250 /*cMsMaxDowntime*/,
                              pszFilename, pStreamOps, pvStreamOpsUser,
                              enmAfter, pfnProgress, pvUser, pfSuspended);
    LogFlow(("VMR3Save: returns %Rrc (*pfSuspended=%RTbool)\n", rc, *pfSuspended));
    return rc;
}


/**
 * Teleport the VM (aka live migration).
 *
 * @returns VBox status code.
 *
 * @param   pUVM                The VM which state should be saved.
 * @param   cMsMaxDowntime      The maximum downtime given as milliseconds.
 * @param   pStreamOps          The stream methods.
 * @param   pvStreamOpsUser     The user argument to the stream methods.
 * @param   pfnProgress         Progress callback. Optional.
 * @param   pvProgressUser      User argument for the progress callback.
 * @param   pfSuspended         Set if we suspended the VM.
 *
 * @thread      Non-EMT.
 * @vmstate     Suspended or Running
 * @vmstateto   Saving+Suspended or
 *              RunningLS+SuspendingLS+SuspendedLS+Saving+Suspended.
 */
VMMR3DECL(int) VMR3Teleport(PUVM pUVM, uint32_t cMsMaxDowntime, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                            PFNVMPROGRESS pfnProgress, void *pvProgressUser, bool *pfSuspended)
{
    LogFlow(("VMR3Teleport: pUVM=%p cMsMaxDowntime=%u pStreamOps=%p pvStreamOps=%p pfnProgress=%p pvProgressUser=%p\n",
             pUVM, cMsMaxDowntime, pStreamOps, pvStreamOpsUser, pfnProgress, pvProgressUser));

    /*
     * Validate input.
     */
    AssertPtr(pfSuspended);
    *pfSuspended = false;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_OTHER_THREAD(pVM);
    AssertPtrReturn(pStreamOps, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnProgress, VERR_INVALID_POINTER);

    /*
     * Join paths with VMR3Save.
     */
    int rc = vmR3SaveTeleport(pVM, cMsMaxDowntime, NULL /*pszFilename*/, pStreamOps, pvStreamOpsUser,
                              SSMAFTER_TELEPORT, pfnProgress, pvProgressUser, pfSuspended);
    LogFlow(("VMR3Teleport: returns %Rrc (*pfSuspended=%RTbool)\n", rc, *pfSuspended));
    return rc;
}



/**
 * EMT(0) worker for VMR3LoadFromFile and VMR3LoadFromStream.
 *
 * @returns VBox status code.
 *
 * @param   pUVM                Pointer to the VM.
 * @param   pszFilename         The name of the file.  NULL if pStreamOps is used.
 * @param   pStreamOps          The stream methods.  NULL if pszFilename is used.
 * @param   pvStreamOpsUser     The user argument to the stream methods.
 * @param   pfnProgress         Progress callback. Optional.
 * @param   pvProgressUser      User argument for the progress callback.
 * @param   fTeleporting        Indicates whether we're teleporting or not.
 *
 * @thread  EMT.
 */
static DECLCALLBACK(int) vmR3Load(PUVM pUVM, const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                                  PFNVMPROGRESS pfnProgress, void *pvProgressUser, bool fTeleporting)
{
    LogFlow(("vmR3Load: pUVM=%p pszFilename=%p:{%s} pStreamOps=%p pvStreamOpsUser=%p pfnProgress=%p pvProgressUser=%p fTeleporting=%RTbool\n",
             pUVM, pszFilename, pszFilename, pStreamOps, pvStreamOpsUser, pfnProgress, pvProgressUser, fTeleporting));

    /*
     * Validate input (paranoia).
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertPtrNull(pszFilename);
    AssertPtrNull(pStreamOps);
    AssertPtrNull(pfnProgress);

    /*
     * Change the state and perform the load.
     *
     * Always perform a relocation round afterwards to make sure hypervisor
     * selectors and such are correct.
     */
    int rc = vmR3TrySetState(pVM, "VMR3Load", 2,
                             VMSTATE_LOADING, VMSTATE_CREATED,
                             VMSTATE_LOADING, VMSTATE_SUSPENDED);
    if (RT_FAILURE(rc))
        return rc;

    pVM->vm.s.fTeleportedAndNotFullyResumedYet = fTeleporting;

    uint32_t cErrorsPriorToSave = VMR3GetErrorCount(pUVM);
    rc = SSMR3Load(pVM, pszFilename, pStreamOps, pvStreamOpsUser, SSMAFTER_RESUME, pfnProgress, pvProgressUser);
    if (RT_SUCCESS(rc))
    {
        VMR3Relocate(pVM, 0 /*offDelta*/);
        vmR3SetState(pVM, VMSTATE_SUSPENDED, VMSTATE_LOADING);
    }
    else
    {
        pVM->vm.s.fTeleportedAndNotFullyResumedYet = false;
        vmR3SetState(pVM, VMSTATE_LOAD_FAILURE, VMSTATE_LOADING);

        if (cErrorsPriorToSave == VMR3GetErrorCount(pUVM))
            rc = VMSetError(pVM, rc, RT_SRC_POS,
                            N_("Unable to restore the virtual machine's saved state from '%s'. "
                               "It may be damaged or from an older version of VirtualBox.  "
                               "Please discard the saved state before starting the virtual machine"),
                            pszFilename);
    }

    return rc;
}


/**
 * Loads a VM state into a newly created VM or a one that is suspended.
 *
 * To restore a saved state on VM startup, call this function and then resume
 * the VM instead of powering it on.
 *
 * @returns VBox status code.
 *
 * @param   pUVM            The user mode VM structure.
 * @param   pszFilename     The name of the save state file.
 * @param   pfnProgress     Progress callback. Optional.
 * @param   pvUser          User argument for the progress callback.
 *
 * @thread      Any thread.
 * @vmstate     Created, Suspended
 * @vmstateto   Loading+Suspended
 */
VMMR3DECL(int) VMR3LoadFromFile(PUVM pUVM, const char *pszFilename, PFNVMPROGRESS pfnProgress, void *pvUser)
{
    LogFlow(("VMR3LoadFromFile: pUVM=%p pszFilename=%p:{%s} pfnProgress=%p pvUser=%p\n",
             pUVM, pszFilename, pszFilename, pfnProgress, pvUser));

    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);

    /*
     * Forward the request to EMT(0).  No need to setup a rendezvous here
     * since there is no execution taking place when this call is allowed.
     */
    int rc = VMR3ReqCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)vmR3Load, 7,
                              pUVM, pszFilename, (uintptr_t)NULL /*pStreamOps*/, (uintptr_t)NULL /*pvStreamOpsUser*/,
                              pfnProgress, pvUser, false /*fTeleporting*/);
    LogFlow(("VMR3LoadFromFile: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMR3LoadFromFile for arbitrary file streams.
 *
 * @returns VBox status code.
 *
 * @param   pUVM            Pointer to the VM.
 * @param   pStreamOps      The stream methods.
 * @param   pvStreamOpsUser The user argument to the stream methods.
 * @param   pfnProgress     Progress callback. Optional.
 * @param   pvProgressUser  User argument for the progress callback.
 * @param   fTeleporting    Flag whether this call is part of a teleportation operation.
 *
 * @thread      Any thread.
 * @vmstate     Created, Suspended
 * @vmstateto   Loading+Suspended
 */
VMMR3DECL(int) VMR3LoadFromStream(PUVM pUVM, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                                  PFNVMPROGRESS pfnProgress, void *pvProgressUser, bool fTeleporting)
{
    LogFlow(("VMR3LoadFromStream: pUVM=%p pStreamOps=%p pvStreamOpsUser=%p pfnProgress=%p pvProgressUser=%p fTeleporting=%RTbool\n",
             pUVM, pStreamOps, pvStreamOpsUser, pfnProgress, pvProgressUser, fTeleporting));

    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pStreamOps, VERR_INVALID_POINTER);

    /*
     * Forward the request to EMT(0).  No need to setup a rendezvous here
     * since there is no execution taking place when this call is allowed.
     */
    int rc = VMR3ReqCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)vmR3Load, 7,
                              pUVM, (uintptr_t)NULL /*pszFilename*/, pStreamOps, pvStreamOpsUser, pfnProgress,
                              pvProgressUser, fTeleporting);
    LogFlow(("VMR3LoadFromStream: returns %Rrc\n", rc));
    return rc;
}


/**
 * EMT rendezvous worker for VMR3PowerOff.
 *
 * @returns VERR_VM_INVALID_VM_STATE or VINF_EM_OFF. (This is a strict
 *          return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          Ignored.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3PowerOff(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    LogFlow(("vmR3PowerOff: pVM=%p pVCpu=%p/#%u\n", pVM, pVCpu, pVCpu->idCpu));
    Assert(!pvUser); NOREF(pvUser);

    /*
     * The first EMT thru here will change the state to PoweringOff.
     */
    if (pVCpu->idCpu == pVM->cCpus - 1)
    {
        int rc = vmR3TrySetState(pVM, "VMR3PowerOff", 11,
                                 VMSTATE_POWERING_OFF,    VMSTATE_RUNNING,           /* 1 */
                                 VMSTATE_POWERING_OFF,    VMSTATE_SUSPENDED,         /* 2 */
                                 VMSTATE_POWERING_OFF,    VMSTATE_DEBUGGING,         /* 3 */
                                 VMSTATE_POWERING_OFF,    VMSTATE_LOAD_FAILURE,      /* 4 */
                                 VMSTATE_POWERING_OFF,    VMSTATE_GURU_MEDITATION,   /* 5 */
                                 VMSTATE_POWERING_OFF,    VMSTATE_FATAL_ERROR,       /* 6 */
                                 VMSTATE_POWERING_OFF,    VMSTATE_CREATED,           /* 7 */   /** @todo update the diagram! */
                                 VMSTATE_POWERING_OFF_LS, VMSTATE_RUNNING_LS,        /* 8 */
                                 VMSTATE_POWERING_OFF_LS, VMSTATE_DEBUGGING_LS,      /* 9 */
                                 VMSTATE_POWERING_OFF_LS, VMSTATE_GURU_MEDITATION_LS,/* 10 */
                                 VMSTATE_POWERING_OFF_LS, VMSTATE_FATAL_ERROR_LS);   /* 11 */
        if (RT_FAILURE(rc))
            return rc;
        if (rc >= 7)
            SSMR3Cancel(pVM->pUVM);
    }

    /*
     * Check the state.
     */
    VMSTATE enmVMState = VMR3GetState(pVM);
    AssertMsgReturn(   enmVMState == VMSTATE_POWERING_OFF
                    || enmVMState == VMSTATE_POWERING_OFF_LS,
                    ("%s\n", VMR3GetStateName(enmVMState)),
                    VERR_VM_INVALID_VM_STATE);

    /*
     * EMT(0) does the actual power off work here *after* all the other EMTs
     * have been thru and entered the STOPPED state.
     */
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STOPPED);
    if (pVCpu->idCpu == 0)
    {
        /*
         * For debugging purposes, we will log a summary of the guest state at this point.
         */
        if (enmVMState != VMSTATE_GURU_MEDITATION)
        {
            /** @todo make the state dumping at VMR3PowerOff optional. */
            bool fOldBuffered = RTLogRelSetBuffering(true /*fBuffered*/);
            RTLogRelPrintf("****************** Guest state at power off for VCpu %u ******************\n", pVCpu->idCpu);
            DBGFR3InfoEx(pVM->pUVM, pVCpu->idCpu, "cpumguest", "verbose", DBGFR3InfoLogRelHlp());
            RTLogRelPrintf("***\n");
            DBGFR3InfoEx(pVM->pUVM, pVCpu->idCpu, "cpumguesthwvirt", "verbose", DBGFR3InfoLogRelHlp());
            RTLogRelPrintf("***\n");
            DBGFR3InfoEx(pVM->pUVM, pVCpu->idCpu, "mode", NULL, DBGFR3InfoLogRelHlp());
            RTLogRelPrintf("***\n");
            DBGFR3Info(pVM->pUVM, "activetimers", NULL, DBGFR3InfoLogRelHlp());
            RTLogRelPrintf("***\n");
            DBGFR3Info(pVM->pUVM, "gdt", NULL, DBGFR3InfoLogRelHlp());
            /** @todo dump guest call stack. */
            RTLogRelSetBuffering(fOldBuffered);
            RTLogRelPrintf("************** End of Guest state at power off ***************\n");
        }

        /*
         * Perform the power off notifications and advance the state to
         * Off or OffLS.
         */
        PDMR3PowerOff(pVM);
        DBGFR3PowerOff(pVM);

        PUVM pUVM = pVM->pUVM;
        RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);
        enmVMState = pVM->enmVMState;
        if (enmVMState == VMSTATE_POWERING_OFF_LS)
            vmR3SetStateLocked(pVM, pUVM, VMSTATE_OFF_LS, VMSTATE_POWERING_OFF_LS, false /*fSetRatherThanClearFF*/);
        else
            vmR3SetStateLocked(pVM, pUVM, VMSTATE_OFF,    VMSTATE_POWERING_OFF, false /*fSetRatherThanClearFF*/);
        RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);
    }
    else if (enmVMState != VMSTATE_GURU_MEDITATION)
    {
        /** @todo make the state dumping at VMR3PowerOff optional. */
        bool fOldBuffered = RTLogRelSetBuffering(true /*fBuffered*/);
        RTLogRelPrintf("****************** Guest state at power off for VCpu %u ******************\n", pVCpu->idCpu);
        DBGFR3InfoEx(pVM->pUVM, pVCpu->idCpu, "cpumguest", "verbose", DBGFR3InfoLogRelHlp());
        RTLogRelPrintf("***\n");
        DBGFR3InfoEx(pVM->pUVM, pVCpu->idCpu, "cpumguesthwvirt", "verbose", DBGFR3InfoLogRelHlp());
        RTLogRelPrintf("***\n");
        DBGFR3InfoEx(pVM->pUVM, pVCpu->idCpu, "mode", NULL, DBGFR3InfoLogRelHlp());
        RTLogRelPrintf("***\n");
        RTLogRelSetBuffering(fOldBuffered);
        RTLogRelPrintf("************** End of Guest state at power off for VCpu %u ***************\n", pVCpu->idCpu);
    }

    return VINF_EM_OFF;
}


/**
 * Power off the VM.
 *
 * @returns VBox status code. When called on EMT, this will be a strict status
 *          code that has to be propagated up the call stack.
 *
 * @param   pUVM    The handle of the VM to be powered off.
 *
 * @thread      Any thread.
 * @vmstate     Suspended, Running, Guru Meditation, Load Failure
 * @vmstateto   Off or OffLS
 */
VMMR3DECL(int)   VMR3PowerOff(PUVM pUVM)
{
    LogFlow(("VMR3PowerOff: pUVM=%p\n", pUVM));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Gather all the EMTs to make sure there are no races before
     * changing the VM state.
     */
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                vmR3PowerOff, NULL);
    LogFlow(("VMR3PowerOff: returns %Rrc\n", rc));
    return rc;
}


/**
 * Destroys the VM.
 *
 * The VM must be powered off (or never really powered on) to call this
 * function. The VM handle is destroyed and can no longer be used up successful
 * return.
 *
 * @returns VBox status code.
 *
 * @param   pUVM    The user mode VM handle.
 *
 * @thread      Any none emulation thread.
 * @vmstate     Off, Created
 * @vmstateto   N/A
 */
VMMR3DECL(int) VMR3Destroy(PUVM pUVM)
{
    LogFlow(("VMR3Destroy: pUVM=%p\n", pUVM));

    /*
     * Validate input.
     */
    if (!pUVM)
        return VERR_INVALID_VM_HANDLE;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertLogRelReturn(!VM_IS_EMT(pVM), VERR_VM_THREAD_IS_EMT);

    /*
     * Change VM state to destroying and aall vmR3Destroy on each of the EMTs
     * ending with EMT(0) doing the bulk of the cleanup.
     */
    int rc = vmR3TrySetState(pVM, "VMR3Destroy", 1, VMSTATE_DESTROYING, VMSTATE_OFF);
    if (RT_FAILURE(rc))
        return rc;

    rc = VMR3ReqCallWait(pVM, VMCPUID_ALL_REVERSE, (PFNRT)vmR3Destroy, 1, pVM);
    AssertLogRelRC(rc);

    /*
     * Wait for EMTs to quit and destroy the UVM.
     */
    vmR3DestroyUVM(pUVM, 30000);

    LogFlow(("VMR3Destroy: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Internal destruction worker.
 *
 * This is either called from VMR3Destroy via VMR3ReqCallU or from
 * vmR3EmulationThreadWithId when EMT(0) terminates after having called
 * VMR3Destroy().
 *
 * When called on EMT(0), it will performed the great bulk of the destruction.
 * When called on the other EMTs, they will do nothing and the whole purpose is
 * to return VINF_EM_TERMINATE so they break out of their run loops.
 *
 * @returns VINF_EM_TERMINATE.
 * @param   pVM     The cross context VM structure.
 */
DECLCALLBACK(int) vmR3Destroy(PVM pVM)
{
    PUVM   pUVM  = pVM->pUVM;
    PVMCPU pVCpu = VMMGetCpu(pVM);
    Assert(pVCpu);
    LogFlow(("vmR3Destroy: pVM=%p pUVM=%p pVCpu=%p idCpu=%u\n", pVM, pUVM, pVCpu, pVCpu->idCpu));

    /*
     * Only VCPU 0 does the full cleanup (last).
     */
    if (pVCpu->idCpu == 0)
    {
        /*
         * Dump statistics to the log.
         */
#if defined(VBOX_WITH_STATISTICS) || defined(LOG_ENABLED)
        RTLogFlags(NULL, "nodisabled nobuffered");
#endif
//#ifdef VBOX_WITH_STATISTICS
//        STAMR3Dump(pUVM, "*");
//#else
        LogRel(("************************* Statistics *************************\n"));
        STAMR3DumpToReleaseLog(pUVM, "*");
        LogRel(("********************* End of statistics **********************\n"));
//#endif

        /*
         * Destroy the VM components.
         */
        int rc = TMR3Term(pVM);
        AssertRC(rc);
#ifdef VBOX_WITH_DEBUGGER
        rc = DBGCIoTerminate(pUVM, pUVM->vm.s.pvDBGC);
        pUVM->vm.s.pvDBGC = NULL;
#endif
        AssertRC(rc);
        rc = PDMR3Term(pVM);
        AssertRC(rc);
        rc = GIMR3Term(pVM);
        AssertRC(rc);
        rc = DBGFR3Term(pVM);
        AssertRC(rc);
        rc = IEMR3Term(pVM);
        AssertRC(rc);
        rc = EMR3Term(pVM);
        AssertRC(rc);
        rc = IOMR3Term(pVM);
        AssertRC(rc);
        rc = TRPMR3Term(pVM);
        AssertRC(rc);
#if !defined(VBOX_VMM_TARGET_ARMV8)
        rc = SELMR3Term(pVM);
        AssertRC(rc);
#endif
        rc = HMR3Term(pVM);
        AssertRC(rc);
        rc = NEMR3Term(pVM);
        AssertRC(rc);
        rc = PGMR3Term(pVM);
        AssertRC(rc);
        rc = VMMR3Term(pVM); /* Terminates the ring-0 code! */
        AssertRC(rc);
        rc = CPUMR3Term(pVM);
        AssertRC(rc);
        SSMR3Term(pVM);
        rc = PDMR3CritSectBothTerm(pVM);
        AssertRC(rc);
        rc = MMR3Term(pVM);
        AssertRC(rc);

        /*
         * We're done, tell the other EMTs to quit.
         */
        ASMAtomicUoWriteBool(&pUVM->vm.s.fTerminateEMT, true);
        ASMAtomicWriteU32(&pVM->fGlobalForcedActions, VM_FF_CHECK_VM_STATE); /* Can't hurt... */
        LogFlow(("vmR3Destroy: returning %Rrc\n", VINF_EM_TERMINATE));
    }

    /*
     * Decrement the active EMT count here.
     */
    PUVMCPU pUVCpu = &pUVM->aCpus[pVCpu->idCpu];
    if (!pUVCpu->vm.s.fBeenThruVmDestroy)
    {
        pUVCpu->vm.s.fBeenThruVmDestroy = true;
        ASMAtomicDecU32(&pUVM->vm.s.cActiveEmts);
    }
    else
        AssertFailed();

    return VINF_EM_TERMINATE;
}


/**
 * Destroys the UVM portion.
 *
 * This is called as the final step in the VM destruction or as the cleanup
 * in case of a creation failure.
 *
 * @param   pUVM            The user mode VM structure.
 * @param   cMilliesEMTWait The number of milliseconds to wait for the emulation
 *                          threads.
 */
static void vmR3DestroyUVM(PUVM pUVM, uint32_t cMilliesEMTWait)
{
    /*
     * Signal termination of each the emulation threads and
     * wait for them to complete.
     */
    /* Signal them - in reverse order since EMT(0) waits for the others. */
    ASMAtomicUoWriteBool(&pUVM->vm.s.fTerminateEMT, true);
    if (pUVM->pVM)
        VM_FF_SET(pUVM->pVM, VM_FF_CHECK_VM_STATE); /* Can't hurt... */
    VMCPUID iCpu = pUVM->cCpus;
    while (iCpu-- > 0)
    {
        VMR3NotifyGlobalFFU(pUVM, VMNOTIFYFF_FLAGS_DONE_REM);
        RTSemEventSignal(pUVM->aCpus[iCpu].vm.s.EventSemWait);
    }

    /* Wait for EMT(0), it in turn waits for the rest. */
    ASMAtomicUoWriteBool(&pUVM->vm.s.fTerminateEMT, true);

    RTTHREAD const hSelf = RTThreadSelf();
    RTTHREAD hThread = pUVM->aCpus[0].vm.s.ThreadEMT;
    if (   hThread != NIL_RTTHREAD
        && hThread != hSelf)
    {
        int rc2 = RTThreadWait(hThread, RT_MAX(cMilliesEMTWait, 2000), NULL);
        if (rc2 == VERR_TIMEOUT) /* avoid the assertion when debugging. */
            rc2 = RTThreadWait(hThread, 1000, NULL);
        AssertLogRelMsgRC(rc2, ("iCpu=0 rc=%Rrc\n", rc2));
        if (RT_SUCCESS(rc2))
            pUVM->aCpus[0].vm.s.ThreadEMT = NIL_RTTHREAD;
    }

    /* Just in case we're in a weird failure situation w/o EMT(0) to do the
       waiting, wait the other EMTs too. */
    for (iCpu = 1; iCpu < pUVM->cCpus; iCpu++)
    {
        ASMAtomicXchgHandle(&pUVM->aCpus[iCpu].vm.s.ThreadEMT, NIL_RTTHREAD, &hThread);
        if (hThread != NIL_RTTHREAD)
        {
            if (hThread != hSelf)
            {
                int rc2 = RTThreadWait(hThread, 250 /*ms*/, NULL);
                AssertLogRelMsgRC(rc2, ("iCpu=%u rc=%Rrc\n", iCpu, rc2));
                if (RT_SUCCESS(rc2))
                    continue;
            }
            pUVM->aCpus[iCpu].vm.s.ThreadEMT = hThread;
        }
    }

    /* Cleanup the semaphores. */
    iCpu = pUVM->cCpus;
    while (iCpu-- > 0)
    {
        RTSemEventDestroy(pUVM->aCpus[iCpu].vm.s.EventSemWait);
        pUVM->aCpus[iCpu].vm.s.EventSemWait = NIL_RTSEMEVENT;
    }

    /*
     * Free the event semaphores associated with the request packets.
     */
    unsigned cReqs = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(pUVM->vm.s.apReqFree); i++)
    {
        PVMREQ pReq = pUVM->vm.s.apReqFree[i];
        pUVM->vm.s.apReqFree[i] = NULL;
        for (; pReq; pReq = pReq->pNext, cReqs++)
        {
            pReq->enmState = VMREQSTATE_INVALID;
            RTSemEventDestroy(pReq->EventSem);
        }
    }
    Assert(cReqs == pUVM->vm.s.cReqFree); NOREF(cReqs);

    /*
     * Kill all queued requests. (There really shouldn't be any!)
     */
    for (unsigned i = 0; i < 10; i++)
    {
        PVMREQ pReqHead = ASMAtomicXchgPtrT(&pUVM->vm.s.pPriorityReqs, NULL, PVMREQ);
        if (!pReqHead)
        {
            pReqHead = ASMAtomicXchgPtrT(&pUVM->vm.s.pNormalReqs, NULL, PVMREQ);
            if (!pReqHead)
                break;
        }
        AssertLogRelMsgFailed(("Requests pending! VMR3Destroy caller has to serialize this.\n"));

        for (PVMREQ pReq = pReqHead; pReq; pReq = pReq->pNext)
        {
            ASMAtomicUoWriteS32(&pReq->iStatus, VERR_VM_REQUEST_KILLED);
            ASMAtomicWriteSize(&pReq->enmState, VMREQSTATE_INVALID);
            RTSemEventSignal(pReq->EventSem);
            RTThreadSleep(2);
            RTSemEventDestroy(pReq->EventSem);
        }
        /* give them a chance to respond before we free the request memory. */
        RTThreadSleep(32);
    }

    /*
     * Now all queued VCPU requests (again, there shouldn't be any).
     */
    for (VMCPUID idCpu = 0; idCpu < pUVM->cCpus; idCpu++)
    {
        PUVMCPU pUVCpu = &pUVM->aCpus[idCpu];

        for (unsigned i = 0; i < 10; i++)
        {
            PVMREQ pReqHead = ASMAtomicXchgPtrT(&pUVCpu->vm.s.pPriorityReqs, NULL, PVMREQ);
            if (!pReqHead)
            {
                pReqHead = ASMAtomicXchgPtrT(&pUVCpu->vm.s.pNormalReqs, NULL, PVMREQ);
                if (!pReqHead)
                    break;
            }
            AssertLogRelMsgFailed(("Requests pending! VMR3Destroy caller has to serialize this.\n"));

            for (PVMREQ pReq = pReqHead; pReq; pReq = pReq->pNext)
            {
                ASMAtomicUoWriteS32(&pReq->iStatus, VERR_VM_REQUEST_KILLED);
                ASMAtomicWriteSize(&pReq->enmState, VMREQSTATE_INVALID);
                RTSemEventSignal(pReq->EventSem);
                RTThreadSleep(2);
                RTSemEventDestroy(pReq->EventSem);
            }
            /* give them a chance to respond before we free the request memory. */
            RTThreadSleep(32);
        }
    }

    /*
     * Make sure the VMMR0.r0 module and whatever else is unloaded.
     */
    PDMR3TermUVM(pUVM);

    RTCritSectDelete(&pUVM->vm.s.AtErrorCritSect);
    RTCritSectDelete(&pUVM->vm.s.AtStateCritSect);

    /*
     * Terminate the support library if initialized.
     */
    if (pUVM->vm.s.pSession)
    {
        int rc = SUPR3Term(false /*fForced*/);
        AssertRC(rc);
        pUVM->vm.s.pSession = NIL_RTR0PTR;
    }

    /*
     * Release the UVM structure reference.
     */
    VMR3ReleaseUVM(pUVM);

    /*
     * Clean up and flush logs.
     */
    RTLogFlush(NULL);
}


/**
 * Worker which checks integrity of some internal structures.
 * This is yet another attempt to track down that AVL tree crash.
 */
static void vmR3CheckIntegrity(PVM pVM)
{
#ifdef VBOX_STRICT
    int rc = PGMR3CheckIntegrity(pVM);
    AssertReleaseRC(rc);
#else
    RT_NOREF_PV(pVM);
#endif
}


/**
 * EMT rendezvous worker for VMR3ResetFF for doing soft/warm reset.
 *
 * @returns VERR_VM_INVALID_VM_STATE, VINF_EM_RESCHEDULE.
 *          (This is a strict return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          The reset flags.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3SoftReset(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    uint32_t fResetFlags = *(uint32_t *)pvUser;


    /*
     * The first EMT will try change the state to resetting.  If this fails,
     * we won't get called for the other EMTs.
     */
    if (pVCpu->idCpu == pVM->cCpus - 1)
    {
        int rc = vmR3TrySetState(pVM, "vmR3ResetSoft", 3,
                                 VMSTATE_SOFT_RESETTING,     VMSTATE_RUNNING,
                                 VMSTATE_SOFT_RESETTING,     VMSTATE_SUSPENDED,
                                 VMSTATE_SOFT_RESETTING_LS,  VMSTATE_RUNNING_LS);
        if (RT_FAILURE(rc))
            return rc;
        pVM->vm.s.cResets++;
        pVM->vm.s.cSoftResets++;
    }

    /*
     * Check the state.
     */
    VMSTATE enmVMState = VMR3GetState(pVM);
    AssertLogRelMsgReturn(   enmVMState == VMSTATE_SOFT_RESETTING
                          || enmVMState == VMSTATE_SOFT_RESETTING_LS,
                          ("%s\n", VMR3GetStateName(enmVMState)),
                          VERR_VM_UNEXPECTED_UNSTABLE_STATE);

    /*
     * EMT(0) does the full cleanup *after* all the other EMTs has been
     * thru here and been told to enter the EMSTATE_WAIT_SIPI state.
     *
     * Because there are per-cpu reset routines and order may/is important,
     * the following sequence looks a bit ugly...
     */

    /* Reset the VCpu state. */
    VMCPU_ASSERT_STATE(pVCpu, VMCPUSTATE_STARTED);

    /*
     * Soft reset the VM components.
     */
    if (pVCpu->idCpu == 0)
    {
        PDMR3SoftReset(pVM, fResetFlags);
        TRPMR3Reset(pVM);
        CPUMR3Reset(pVM);               /* This must come *after* PDM (due to APIC base MSR caching). */
        EMR3Reset(pVM);
        HMR3Reset(pVM);                 /* This must come *after* PATM, CSAM, CPUM, SELM and TRPM. */
        NEMR3Reset(pVM);

        /*
         * Since EMT(0) is the last to go thru here, it will advance the state.
         * (Unlike vmR3HardReset we won't be doing any suspending of live
         * migration VMs here since memory is unchanged.)
         */
        PUVM pUVM = pVM->pUVM;
        RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);
        enmVMState = pVM->enmVMState;
        if (enmVMState == VMSTATE_SOFT_RESETTING)
        {
            if (pUVM->vm.s.enmPrevVMState == VMSTATE_SUSPENDED)
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_SUSPENDED, VMSTATE_SOFT_RESETTING, false /*fSetRatherThanClearFF*/);
            else
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_RUNNING,   VMSTATE_SOFT_RESETTING, false /*fSetRatherThanClearFF*/);
        }
        else
            vmR3SetStateLocked(pVM, pUVM, VMSTATE_RUNNING_LS, VMSTATE_SOFT_RESETTING_LS, false /*fSetRatherThanClearFF*/);
        RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);
    }

    return VINF_EM_RESCHEDULE;
}


/**
 * EMT rendezvous worker for VMR3Reset and VMR3ResetFF.
 *
 * This is called by the emulation threads as a response to the reset request
 * issued by VMR3Reset().
 *
 * @returns VERR_VM_INVALID_VM_STATE, VINF_EM_RESET or VINF_EM_SUSPEND. (This
 *          is a strict return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          Ignored.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3HardReset(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    Assert(!pvUser); NOREF(pvUser);

    /*
     * The first EMT will try change the state to resetting.  If this fails,
     * we won't get called for the other EMTs.
     */
    if (pVCpu->idCpu == pVM->cCpus - 1)
    {
        int rc = vmR3TrySetState(pVM, "vmR3HardReset", 3,
                                 VMSTATE_RESETTING,     VMSTATE_RUNNING,
                                 VMSTATE_RESETTING,     VMSTATE_SUSPENDED,
                                 VMSTATE_RESETTING_LS,  VMSTATE_RUNNING_LS);
        if (RT_FAILURE(rc))
            return rc;
        pVM->vm.s.cResets++;
        pVM->vm.s.cHardResets++;
    }

    /*
     * Check the state.
     */
    VMSTATE enmVMState = VMR3GetState(pVM);
    AssertLogRelMsgReturn(   enmVMState == VMSTATE_RESETTING
                          || enmVMState == VMSTATE_RESETTING_LS,
                          ("%s\n", VMR3GetStateName(enmVMState)),
                          VERR_VM_UNEXPECTED_UNSTABLE_STATE);

    /*
     * EMT(0) does the full cleanup *after* all the other EMTs has been
     * thru here and been told to enter the EMSTATE_WAIT_SIPI state.
     *
     * Because there are per-cpu reset routines and order may/is important,
     * the following sequence looks a bit ugly...
     */
    if (pVCpu->idCpu == 0)
        vmR3CheckIntegrity(pVM);

    /* Reset the VCpu state. */
    VMCPU_ASSERT_STATE(pVCpu, VMCPUSTATE_STARTED);

    /* Clear all pending forced actions. */
    VMCPU_FF_CLEAR_MASK(pVCpu, VMCPU_FF_ALL_MASK & ~VMCPU_FF_REQUEST);

    /*
     * Reset the VM components.
     */
    if (pVCpu->idCpu == 0)
    {
        GIMR3Reset(pVM);                /* This must come *before* PDM and TM. */
        PDMR3Reset(pVM);
        PGMR3Reset(pVM);
#if !defined(VBOX_VMM_TARGET_ARMV8)
        SELMR3Reset(pVM);
#endif
        TRPMR3Reset(pVM);
        IOMR3Reset(pVM);
        CPUMR3Reset(pVM);               /* This must come *after* PDM (due to APIC base MSR caching). */
        TMR3Reset(pVM);
        EMR3Reset(pVM);
        HMR3Reset(pVM);                 /* This must come *after* PATM, CSAM, CPUM, SELM and TRPM. */
        NEMR3Reset(pVM);

        /*
         * Do memory setup.
         */
        PGMR3MemSetup(pVM, true /*fAtReset*/);
        PDMR3MemSetup(pVM, true /*fAtReset*/);

        /*
         * Since EMT(0) is the last to go thru here, it will advance the state.
         * When a live save is active, we will move on to SuspendingLS but
         * leave it for VMR3Reset to do the actual suspending due to deadlock risks.
         */
        PUVM pUVM = pVM->pUVM;
        RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);
        enmVMState = pVM->enmVMState;
        if (enmVMState == VMSTATE_RESETTING)
        {
            if (pUVM->vm.s.enmPrevVMState == VMSTATE_SUSPENDED)
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_SUSPENDED, VMSTATE_RESETTING, false /*fSetRatherThanClearFF*/);
            else
                vmR3SetStateLocked(pVM, pUVM, VMSTATE_RUNNING,   VMSTATE_RESETTING, false /*fSetRatherThanClearFF*/);
        }
        else
            vmR3SetStateLocked(pVM, pUVM, VMSTATE_SUSPENDING_LS, VMSTATE_RESETTING_LS, false /*fSetRatherThanClearFF*/);
        RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);

        vmR3CheckIntegrity(pVM);

        /*
         * Do the suspend bit as well.
         * It only requires some EMT(0) work at present.
         */
        if (enmVMState != VMSTATE_RESETTING)
        {
            vmR3SuspendDoWork(pVM);
            vmR3SetState(pVM, VMSTATE_SUSPENDED_LS, VMSTATE_SUSPENDING_LS);
        }
    }

    return enmVMState == VMSTATE_RESETTING
         ? VINF_EM_RESET
         : VINF_EM_SUSPEND; /** @todo VINF_EM_SUSPEND has lower priority than VINF_EM_RESET, so fix races. Perhaps add a new code for this combined case. */
}


/**
 * Internal worker for VMR3Reset, VMR3ResetFF, VMR3TripleFault.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   fHardReset      Whether it's a hard reset or not.
 * @param   fResetFlags     The reset flags (PDMVMRESET_F_XXX).
 */
static VBOXSTRICTRC vmR3ResetCommon(PVM pVM, bool fHardReset, uint32_t fResetFlags)
{
    LogFlow(("vmR3ResetCommon: fHardReset=%RTbool fResetFlags=%#x\n", fHardReset, fResetFlags));
    int rc;
    if (fHardReset)
    {
        /*
         * Hard reset.
         */
        /* Check whether we're supposed to power off instead of resetting. */
        if (pVM->vm.s.fPowerOffInsteadOfReset)
        {
            PUVM pUVM = pVM->pUVM;
            if (   pUVM->pVmm2UserMethods
                && pUVM->pVmm2UserMethods->pfnNotifyResetTurnedIntoPowerOff)
                pUVM->pVmm2UserMethods->pfnNotifyResetTurnedIntoPowerOff(pUVM->pVmm2UserMethods, pUVM);
            return VMR3PowerOff(pUVM);
        }

        /* Gather all the EMTs to make sure there are no races before changing
           the VM state. */
        rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                vmR3HardReset, NULL);
    }
    else
    {
        /*
         * Soft reset. Since we only support this with a single CPU active,
         * we must be on EMT #0 here.
         */
        VM_ASSERT_EMT0(pVM);
        rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                vmR3SoftReset, &fResetFlags);
    }

    LogFlow(("vmR3ResetCommon: returns %Rrc\n", rc));
    return rc;
}



/**
 * Reset the current VM.
 *
 * @returns VBox status code.
 * @param   pUVM    The VM to reset.
 */
VMMR3DECL(int) VMR3Reset(PUVM pUVM)
{
    LogFlow(("VMR3Reset:\n"));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    return VBOXSTRICTRC_VAL(vmR3ResetCommon(pVM, true, 0));
}


/**
 * Handle the reset force flag or triple fault.
 *
 * This handles both soft and hard resets (see PDMVMRESET_F_XXX).
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @thread  EMT
 *
 * @remarks Caller is expected to clear the VM_FF_RESET force flag.
 */
VMMR3_INT_DECL(VBOXSTRICTRC) VMR3ResetFF(PVM pVM)
{
    LogFlow(("VMR3ResetFF:\n"));

    /*
     * First consult the firmware on whether this is a hard or soft reset.
     */
    uint32_t fResetFlags;
    bool fHardReset = PDMR3GetResetInfo(pVM, 0 /*fOverride*/, &fResetFlags);
    return vmR3ResetCommon(pVM, fHardReset, fResetFlags);
}


/**
 * For handling a CPU reset on triple fault.
 *
 * According to one mainboard manual, a CPU triple fault causes the 286 CPU to
 * send a SHUTDOWN signal to the chipset.  The chipset responds by sending a
 * RESET signal to the CPU.  So, it should be very similar to a soft/warm reset.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @thread  EMT
 */
VMMR3_INT_DECL(VBOXSTRICTRC) VMR3ResetTripleFault(PVM pVM)
{
    LogFlow(("VMR3ResetTripleFault:\n"));

    /*
     * First consult the firmware on whether this is a hard or soft reset.
     */
    uint32_t fResetFlags;
    bool fHardReset = PDMR3GetResetInfo(pVM, PDMVMRESET_F_TRIPLE_FAULT, &fResetFlags);
    return vmR3ResetCommon(pVM, fHardReset, fResetFlags);
}


/**
 * Gets the user mode VM structure pointer given Pointer to the VM.
 *
 * @returns Pointer to the user mode VM structure on success. NULL if @a pVM is
 *          invalid (asserted).
 * @param   pVM                 The cross context VM structure.
 * @sa      VMR3GetVM, VMR3RetainUVM
 */
VMMR3DECL(PUVM) VMR3GetUVM(PVM pVM)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, NULL);
    return pVM->pUVM;
}


/**
 * Gets the shared VM structure pointer given the pointer to the user mode VM
 * structure.
 *
 * @returns Pointer to the VM.
 *          NULL if @a pUVM is invalid (asserted) or if no shared VM structure
 *          is currently associated with it.
 * @param   pUVM                The user mode VM handle.
 * @sa      VMR3GetUVM
 */
VMMR3DECL(PVM) VMR3GetVM(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    return pUVM->pVM;
}


/**
 * Retain the user mode VM handle.
 *
 * @returns Reference count.
 *          UINT32_MAX if @a pUVM is invalid.
 *
 * @param   pUVM                The user mode VM handle.
 * @sa      VMR3ReleaseUVM
 */
VMMR3DECL(uint32_t) VMR3RetainUVM(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, UINT32_MAX);
    uint32_t cRefs = ASMAtomicIncU32(&pUVM->vm.s.cUvmRefs);
    AssertMsg(cRefs > 0 && cRefs < _64K, ("%u\n", cRefs));
    return cRefs;
}


/**
 * Does the final release of the UVM structure.
 *
 * @param   pUVM                The user mode VM handle.
 */
static void vmR3DoReleaseUVM(PUVM pUVM)
{
    /*
     * Free the UVM.
     */
    Assert(!pUVM->pVM);

    MMR3HeapFree(pUVM->vm.s.pszName);
    pUVM->vm.s.pszName = NULL;

    MMR3TermUVM(pUVM);
    STAMR3TermUVM(pUVM);

    ASMAtomicUoWriteU32(&pUVM->u32Magic, UINT32_MAX);
    RTTlsFree(pUVM->vm.s.idxTLS);
    RTMemPageFree(pUVM, RT_UOFFSETOF_DYN(UVM, aCpus[pUVM->cCpus]));
}


/**
 * Releases a refernece to the mode VM handle.
 *
 * @returns The new reference count, 0 if destroyed.
 *          UINT32_MAX if @a pUVM is invalid.
 *
 * @param   pUVM                The user mode VM handle.
 * @sa      VMR3RetainUVM
 */
VMMR3DECL(uint32_t) VMR3ReleaseUVM(PUVM pUVM)
{
    if (!pUVM)
        return 0;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, UINT32_MAX);
    uint32_t cRefs = ASMAtomicDecU32(&pUVM->vm.s.cUvmRefs);
    if (!cRefs)
        vmR3DoReleaseUVM(pUVM);
    else
        AssertMsg(cRefs < _64K, ("%u\n", cRefs));
    return cRefs;
}


/**
 * Gets the VM name.
 *
 * @returns Pointer to a read-only string containing the name. NULL if called
 *          too early.
 * @param   pUVM                The user mode VM handle.
 */
VMMR3DECL(const char *) VMR3GetName(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    return pUVM->vm.s.pszName;
}


/**
 * Gets the VM UUID.
 *
 * @returns pUuid on success, NULL on failure.
 * @param   pUVM                The user mode VM handle.
 * @param   pUuid               Where to store the UUID.
 */
VMMR3DECL(PRTUUID) VMR3GetUuid(PUVM pUVM, PRTUUID pUuid)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    AssertPtrReturn(pUuid, NULL);

    *pUuid = pUVM->vm.s.Uuid;
    return pUuid;
}


/**
 * Gets the current VM state.
 *
 * @returns The current VM state.
 * @param   pVM             The cross context VM structure.
 * @thread  Any
 */
VMMR3DECL(VMSTATE) VMR3GetState(PVM pVM)
{
    AssertMsgReturn(RT_VALID_ALIGNED_PTR(pVM, HOST_PAGE_SIZE), ("%p\n", pVM), VMSTATE_TERMINATED);
    VMSTATE enmVMState = pVM->enmVMState;
    return enmVMState >= VMSTATE_CREATING && enmVMState <= VMSTATE_TERMINATED ? enmVMState : VMSTATE_TERMINATED;
}


/**
 * Gets the current VM state.
 *
 * @returns The current VM state.
 * @param   pUVM            The user-mode VM handle.
 * @thread  Any
 */
VMMR3DECL(VMSTATE) VMR3GetStateU(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VMSTATE_TERMINATED);
    if (RT_UNLIKELY(!pUVM->pVM))
        return VMSTATE_TERMINATED;
    return pUVM->pVM->enmVMState;
}


/**
 * Gets the state name string for a VM state.
 *
 * @returns Pointer to the state name. (readonly)
 * @param   enmState        The state.
 */
VMMR3DECL(const char *) VMR3GetStateName(VMSTATE enmState)
{
    switch (enmState)
    {
        case VMSTATE_CREATING:          return "CREATING";
        case VMSTATE_CREATED:           return "CREATED";
        case VMSTATE_LOADING:           return "LOADING";
        case VMSTATE_POWERING_ON:       return "POWERING_ON";
        case VMSTATE_RESUMING:          return "RESUMING";
        case VMSTATE_RUNNING:           return "RUNNING";
        case VMSTATE_RUNNING_LS:        return "RUNNING_LS";
        case VMSTATE_RESETTING:         return "RESETTING";
        case VMSTATE_RESETTING_LS:      return "RESETTING_LS";
        case VMSTATE_SOFT_RESETTING:    return "SOFT_RESETTING";
        case VMSTATE_SOFT_RESETTING_LS: return "SOFT_RESETTING_LS";
        case VMSTATE_SUSPENDED:         return "SUSPENDED";
        case VMSTATE_SUSPENDED_LS:      return "SUSPENDED_LS";
        case VMSTATE_SUSPENDED_EXT_LS:  return "SUSPENDED_EXT_LS";
        case VMSTATE_SUSPENDING:        return "SUSPENDING";
        case VMSTATE_SUSPENDING_LS:     return "SUSPENDING_LS";
        case VMSTATE_SUSPENDING_EXT_LS: return "SUSPENDING_EXT_LS";
        case VMSTATE_SAVING:            return "SAVING";
        case VMSTATE_DEBUGGING:         return "DEBUGGING";
        case VMSTATE_DEBUGGING_LS:      return "DEBUGGING_LS";
        case VMSTATE_POWERING_OFF:      return "POWERING_OFF";
        case VMSTATE_POWERING_OFF_LS:   return "POWERING_OFF_LS";
        case VMSTATE_FATAL_ERROR:       return "FATAL_ERROR";
        case VMSTATE_FATAL_ERROR_LS:    return "FATAL_ERROR_LS";
        case VMSTATE_GURU_MEDITATION:   return "GURU_MEDITATION";
        case VMSTATE_GURU_MEDITATION_LS:return "GURU_MEDITATION_LS";
        case VMSTATE_LOAD_FAILURE:      return "LOAD_FAILURE";
        case VMSTATE_OFF:               return "OFF";
        case VMSTATE_OFF_LS:            return "OFF_LS";
        case VMSTATE_DESTROYING:        return "DESTROYING";
        case VMSTATE_TERMINATED:        return "TERMINATED";

        default:
            AssertMsgFailed(("Unknown state %d\n", enmState));
            return "Unknown!\n";
    }
}


/**
 * Validates the state transition in strict builds.
 *
 * @returns true if valid, false if not.
 *
 * @param   enmStateOld         The old (current) state.
 * @param   enmStateNew         The proposed new state.
 *
 * @remarks The reference for this is found in doc/vp/VMM.vpp, the VMSTATE
 *          diagram (under State Machine Diagram).
 */
static bool vmR3ValidateStateTransition(VMSTATE enmStateOld, VMSTATE enmStateNew)
{
#ifndef VBOX_STRICT
    RT_NOREF2(enmStateOld, enmStateNew);
#else
    switch (enmStateOld)
    {
        case VMSTATE_CREATING:
            AssertMsgReturn(enmStateNew == VMSTATE_CREATED, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_CREATED:
            AssertMsgReturn(   enmStateNew == VMSTATE_LOADING
                            || enmStateNew == VMSTATE_POWERING_ON
                            || enmStateNew == VMSTATE_POWERING_OFF
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_LOADING:
            AssertMsgReturn(   enmStateNew == VMSTATE_SUSPENDED
                            || enmStateNew == VMSTATE_LOAD_FAILURE
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_POWERING_ON:
            AssertMsgReturn(   enmStateNew == VMSTATE_RUNNING
                            /*|| enmStateNew == VMSTATE_FATAL_ERROR ?*/
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_RESUMING:
            AssertMsgReturn(   enmStateNew == VMSTATE_RUNNING
                            /*|| enmStateNew == VMSTATE_FATAL_ERROR ?*/
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_RUNNING:
            AssertMsgReturn(   enmStateNew == VMSTATE_POWERING_OFF
                            || enmStateNew == VMSTATE_SUSPENDING
                            || enmStateNew == VMSTATE_RESETTING
                            || enmStateNew == VMSTATE_SOFT_RESETTING
                            || enmStateNew == VMSTATE_RUNNING_LS
                            || enmStateNew == VMSTATE_DEBUGGING
                            || enmStateNew == VMSTATE_FATAL_ERROR
                            || enmStateNew == VMSTATE_GURU_MEDITATION
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_RUNNING_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_POWERING_OFF_LS
                            || enmStateNew == VMSTATE_SUSPENDING_LS
                            || enmStateNew == VMSTATE_SUSPENDING_EXT_LS
                            || enmStateNew == VMSTATE_RESETTING_LS
                            || enmStateNew == VMSTATE_SOFT_RESETTING_LS
                            || enmStateNew == VMSTATE_RUNNING
                            || enmStateNew == VMSTATE_DEBUGGING_LS
                            || enmStateNew == VMSTATE_FATAL_ERROR_LS
                            || enmStateNew == VMSTATE_GURU_MEDITATION_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_RESETTING:
            AssertMsgReturn(enmStateNew == VMSTATE_RUNNING, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SOFT_RESETTING:
            AssertMsgReturn(enmStateNew == VMSTATE_RUNNING, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_RESETTING_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_SUSPENDING_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SOFT_RESETTING_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_RUNNING_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SUSPENDING:
            AssertMsgReturn(enmStateNew == VMSTATE_SUSPENDED, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SUSPENDING_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_SUSPENDING
                            || enmStateNew == VMSTATE_SUSPENDED_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SUSPENDING_EXT_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_SUSPENDING
                            || enmStateNew == VMSTATE_SUSPENDED_EXT_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SUSPENDED:
            AssertMsgReturn(   enmStateNew == VMSTATE_POWERING_OFF
                            || enmStateNew == VMSTATE_SAVING
                            || enmStateNew == VMSTATE_RESETTING
                            || enmStateNew == VMSTATE_SOFT_RESETTING
                            || enmStateNew == VMSTATE_RESUMING
                            || enmStateNew == VMSTATE_LOADING
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SUSPENDED_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_SUSPENDED
                            || enmStateNew == VMSTATE_SAVING
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SUSPENDED_EXT_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_SUSPENDED
                            || enmStateNew == VMSTATE_SAVING
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_SAVING:
            AssertMsgReturn(enmStateNew == VMSTATE_SUSPENDED, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_DEBUGGING:
            AssertMsgReturn(   enmStateNew == VMSTATE_RUNNING
                            || enmStateNew == VMSTATE_POWERING_OFF
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_DEBUGGING_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_DEBUGGING
                            || enmStateNew == VMSTATE_RUNNING_LS
                            || enmStateNew == VMSTATE_POWERING_OFF_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_POWERING_OFF:
            AssertMsgReturn(enmStateNew == VMSTATE_OFF, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_POWERING_OFF_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_POWERING_OFF
                            || enmStateNew == VMSTATE_OFF_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_OFF:
            AssertMsgReturn(enmStateNew == VMSTATE_DESTROYING, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_OFF_LS:
            AssertMsgReturn(enmStateNew == VMSTATE_OFF, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_FATAL_ERROR:
            AssertMsgReturn(enmStateNew == VMSTATE_POWERING_OFF, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_FATAL_ERROR_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_FATAL_ERROR
                            || enmStateNew == VMSTATE_POWERING_OFF_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_GURU_MEDITATION:
            AssertMsgReturn(   enmStateNew == VMSTATE_DEBUGGING
                            || enmStateNew == VMSTATE_POWERING_OFF
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_GURU_MEDITATION_LS:
            AssertMsgReturn(   enmStateNew == VMSTATE_GURU_MEDITATION
                            || enmStateNew == VMSTATE_DEBUGGING_LS
                            || enmStateNew == VMSTATE_POWERING_OFF_LS
                            , ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_LOAD_FAILURE:
            AssertMsgReturn(enmStateNew == VMSTATE_POWERING_OFF, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_DESTROYING:
            AssertMsgReturn(enmStateNew == VMSTATE_TERMINATED, ("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;

        case VMSTATE_TERMINATED:
        default:
            AssertMsgFailedReturn(("%s -> %s\n", VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew)), false);
            break;
    }
#endif /* VBOX_STRICT */
    return true;
}


/**
 * Does the state change callouts.
 *
 * The caller owns the AtStateCritSect.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pUVM                The UVM handle.
 * @param   enmStateNew         The New state.
 * @param   enmStateOld         The old state.
 */
static void vmR3DoAtState(PVM pVM, PUVM pUVM, VMSTATE enmStateNew, VMSTATE enmStateOld)
{
    LogRel(("Changing the VM state from '%s' to '%s'\n", VMR3GetStateName(enmStateOld),  VMR3GetStateName(enmStateNew)));

    for (PVMATSTATE pCur = pUVM->vm.s.pAtState; pCur; pCur = pCur->pNext)
    {
        pCur->pfnAtState(pUVM, VMMR3GetVTable(), enmStateNew, enmStateOld, pCur->pvUser);
        if (    enmStateNew     != VMSTATE_DESTROYING
            &&  pVM->enmVMState == VMSTATE_DESTROYING)
            break;
        AssertMsg(pVM->enmVMState == enmStateNew,
                  ("You are not allowed to change the state while in the change callback, except "
                   "from destroying the VM. There are restrictions in the way the state changes "
                   "are propagated up to the EM execution loop and it makes the program flow very "
                   "difficult to follow. (%s, expected %s, old %s)\n",
                   VMR3GetStateName(pVM->enmVMState), VMR3GetStateName(enmStateNew),
                   VMR3GetStateName(enmStateOld)));
    }
}


/**
 * Sets the current VM state, with the AtStatCritSect already entered.
 *
 * @param   pVM                     The cross context VM structure.
 * @param   pUVM                    The UVM handle.
 * @param   enmStateNew             The new state.
 * @param   enmStateOld             The old state.
 * @param   fSetRatherThanClearFF   The usual behavior is to clear the
 *                                  VM_FF_CHECK_VM_STATE force flag, but for
 *                                  some transitions (-> guru) we need to kick
 *                                  the other EMTs to stop what they're doing.
 */
static void vmR3SetStateLocked(PVM pVM, PUVM pUVM, VMSTATE enmStateNew, VMSTATE enmStateOld, bool fSetRatherThanClearFF)
{
    vmR3ValidateStateTransition(enmStateOld, enmStateNew);

    AssertMsg(pVM->enmVMState == enmStateOld,
              ("%s != %s\n", VMR3GetStateName(pVM->enmVMState), VMR3GetStateName(enmStateOld)));

    pUVM->vm.s.enmPrevVMState = enmStateOld;
    pVM->enmVMState           = enmStateNew;

    if (!fSetRatherThanClearFF)
        VM_FF_CLEAR(pVM, VM_FF_CHECK_VM_STATE);
    else if (pVM->cCpus > 0)
        VM_FF_SET(pVM, VM_FF_CHECK_VM_STATE);

    vmR3DoAtState(pVM, pUVM, enmStateNew, enmStateOld);
}


/**
 * Sets the current VM state.
 *
 * @param   pVM             The cross context VM structure.
 * @param   enmStateNew     The new state.
 * @param   enmStateOld     The old state (for asserting only).
 */
static void vmR3SetState(PVM pVM, VMSTATE enmStateNew, VMSTATE enmStateOld)
{
    PUVM pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);

    RT_NOREF_PV(enmStateOld);
    AssertMsg(pVM->enmVMState == enmStateOld,
              ("%s != %s\n", VMR3GetStateName(pVM->enmVMState), VMR3GetStateName(enmStateOld)));
    vmR3SetStateLocked(pVM, pUVM, enmStateNew, pVM->enmVMState, false /*fSetRatherThanClearFF*/);

    RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);
}


/**
 * Tries to perform a state transition.
 *
 * @returns The 1-based ordinal of the succeeding transition.
 *          VERR_VM_INVALID_VM_STATE and Assert+LogRel on failure.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pszWho              Who is trying to change it.
 * @param   cTransitions        The number of transitions in the ellipsis.
 * @param   ...                 Transition pairs; new, old.
 */
static int vmR3TrySetState(PVM pVM, const char *pszWho, unsigned cTransitions, ...)
{
    va_list va;
    VMSTATE enmStateNew = VMSTATE_CREATED;
    VMSTATE enmStateOld = VMSTATE_CREATED;

#ifdef VBOX_STRICT
    /*
     * Validate the input first.
     */
    va_start(va, cTransitions);
    for (unsigned i = 0; i < cTransitions; i++)
    {
        enmStateNew = (VMSTATE)va_arg(va, /*VMSTATE*/int);
        enmStateOld = (VMSTATE)va_arg(va, /*VMSTATE*/int);
        vmR3ValidateStateTransition(enmStateOld, enmStateNew);
    }
    va_end(va);
#endif

    /*
     * Grab the lock and see if any of the proposed transitions works out.
     */
    va_start(va, cTransitions);
    int     rc          = VERR_VM_INVALID_VM_STATE;
    PUVM    pUVM        = pVM->pUVM;
    RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);

    VMSTATE enmStateCur = pVM->enmVMState;

    for (unsigned i = 0; i < cTransitions; i++)
    {
        enmStateNew = (VMSTATE)va_arg(va, /*VMSTATE*/int);
        enmStateOld = (VMSTATE)va_arg(va, /*VMSTATE*/int);
        if (enmStateCur == enmStateOld)
        {
            vmR3SetStateLocked(pVM, pUVM, enmStateNew, enmStateOld, false /*fSetRatherThanClearFF*/);
            rc = i + 1;
            break;
        }
    }

    if (RT_FAILURE(rc))
    {
        /*
         * Complain about it.
         */
        const char * const pszStateCur = VMR3GetStateName(enmStateCur);
        if (cTransitions == 1)
        {
            LogRel(("%s: %s -> %s failed, because the VM state is actually %s!\n",
                    pszWho, VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew), pszStateCur));
            VMSetError(pVM, VERR_VM_INVALID_VM_STATE, RT_SRC_POS, N_("%s failed because the VM state is %s instead of %s"),
                       pszWho, pszStateCur, VMR3GetStateName(enmStateOld));
            AssertMsgFailed(("%s: %s -> %s failed, because the VM state is actually %s\n",
                             pszWho, VMR3GetStateName(enmStateOld), VMR3GetStateName(enmStateNew), pszStateCur));
        }
        else
        {
            char   szTransitions[4096];
            size_t cchTransitions = 0;
            szTransitions[0] = '\0';
            va_end(va);
            va_start(va, cTransitions);
            for (unsigned i = 0; i < cTransitions; i++)
            {
                enmStateNew = (VMSTATE)va_arg(va, /*VMSTATE*/int);
                enmStateOld = (VMSTATE)va_arg(va, /*VMSTATE*/int);
                const char * const pszStateNew = VMR3GetStateName(enmStateNew);
                const char * const pszStateOld = VMR3GetStateName(enmStateOld);
                LogRel(("%s%s -> %s", i ? ", " : " ", pszStateOld, pszStateNew));
                cchTransitions += RTStrPrintf(&szTransitions[cchTransitions], sizeof(szTransitions) - cchTransitions,
                                              "%s%s -> %s", i ? ", " : " ", pszStateOld, pszStateNew);
            }
            Assert(cchTransitions < sizeof(szTransitions) - 64);

            LogRel(("%s: %s failed, because the VM state is actually %s!\n", pszWho, szTransitions, pszStateCur));
            VMSetError(pVM, VERR_VM_INVALID_VM_STATE, RT_SRC_POS,
                       N_("%s failed because the current VM state, %s, was not found in the state transition table (%s)"),
                       pszWho, pszStateCur, szTransitions);
            AssertMsgFailed(("%s - state=%s, transitions: %s. Check the cTransitions passed us.\n",
                             pszWho, pszStateCur, szTransitions));
        }
    }

    RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);
    va_end(va);
    Assert(rc > 0 || rc < 0);
    return rc;
}


/**
 * Interface used by EM to signal that it's entering the guru meditation state.
 *
 * This will notifying other threads.
 *
 * @returns true if the state changed to Guru, false if no state change.
 * @param   pVM             The cross context VM structure.
 */
VMMR3_INT_DECL(bool) VMR3SetGuruMeditation(PVM pVM)
{
    PUVM pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);

    VMSTATE enmStateCur = pVM->enmVMState;
    bool fRc = true;
    if (enmStateCur == VMSTATE_RUNNING)
        vmR3SetStateLocked(pVM, pUVM, VMSTATE_GURU_MEDITATION, VMSTATE_RUNNING, true /*fSetRatherThanClearFF*/);
    else if (enmStateCur == VMSTATE_RUNNING_LS)
    {
        vmR3SetStateLocked(pVM, pUVM, VMSTATE_GURU_MEDITATION_LS, VMSTATE_RUNNING_LS, true /*fSetRatherThanClearFF*/);
        SSMR3Cancel(pUVM);
    }
    else
        fRc = false;

    RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);
    return fRc;
}


/**
 * Called by vmR3EmulationThreadWithId just before the VM structure is freed.
 *
 * @param   pVM             The cross context VM structure.
 */
void vmR3SetTerminated(PVM pVM)
{
    vmR3SetState(pVM, VMSTATE_TERMINATED, VMSTATE_DESTROYING);
}


/**
 * Checks if the VM was teleported and hasn't been fully resumed yet.
 *
 * This applies to both sides of the teleportation since we may leave a working
 * clone behind and the user is allowed to resume this...
 *
 * @returns true / false.
 * @param   pVM                 The cross context VM structure.
 * @thread  Any thread.
 */
VMMR3_INT_DECL(bool) VMR3TeleportedAndNotFullyResumedYet(PVM pVM)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->vm.s.fTeleportedAndNotFullyResumedYet;
}


/**
 * Registers a VM state change callback.
 *
 * You are not allowed to call any function which changes the VM state from a
 * state callback.
 *
 * @returns VBox status code.
 * @param   pUVM            The VM handle.
 * @param   pfnAtState      Pointer to callback.
 * @param   pvUser          User argument.
 * @thread  Any.
 */
VMMR3DECL(int) VMR3AtStateRegister(PUVM pUVM, PFNVMATSTATE pfnAtState, void *pvUser)
{
    LogFlow(("VMR3AtStateRegister: pfnAtState=%p pvUser=%p\n", pfnAtState, pvUser));

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnAtState, VERR_INVALID_PARAMETER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /*
     * Allocate a new record.
     */
    PVMATSTATE pNew = (PVMATSTATE)MMR3HeapAllocU(pUVM, MM_TAG_VM, sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;

    /* fill */
    pNew->pfnAtState = pfnAtState;
    pNew->pvUser     = pvUser;

    /* insert */
    RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);
    pNew->pNext      = *pUVM->vm.s.ppAtStateNext;
    *pUVM->vm.s.ppAtStateNext = pNew;
    pUVM->vm.s.ppAtStateNext = &pNew->pNext;
    RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);

    return VINF_SUCCESS;
}


/**
 * Deregisters a VM state change callback.
 *
 * @returns VBox status code.
 * @param   pUVM            The VM handle.
 * @param   pfnAtState      Pointer to callback.
 * @param   pvUser          User argument.
 * @thread  Any.
 */
VMMR3DECL(int) VMR3AtStateDeregister(PUVM pUVM, PFNVMATSTATE pfnAtState, void *pvUser)
{
    LogFlow(("VMR3AtStateDeregister: pfnAtState=%p pvUser=%p\n", pfnAtState, pvUser));

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnAtState, VERR_INVALID_PARAMETER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    RTCritSectEnter(&pUVM->vm.s.AtStateCritSect);

    /*
     * Search the list for the entry.
     */
    PVMATSTATE pPrev = NULL;
    PVMATSTATE pCur = pUVM->vm.s.pAtState;
    while (     pCur
           &&   (   pCur->pfnAtState != pfnAtState
                 || pCur->pvUser != pvUser))
    {
        pPrev = pCur;
        pCur = pCur->pNext;
    }
    if (!pCur)
    {
        AssertMsgFailed(("pfnAtState=%p was not found\n", pfnAtState));
        RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);
        return VERR_FILE_NOT_FOUND;
    }

    /*
     * Unlink it.
     */
    if (pPrev)
    {
        pPrev->pNext = pCur->pNext;
        if (!pCur->pNext)
            pUVM->vm.s.ppAtStateNext = &pPrev->pNext;
    }
    else
    {
        pUVM->vm.s.pAtState = pCur->pNext;
        if (!pCur->pNext)
            pUVM->vm.s.ppAtStateNext = &pUVM->vm.s.pAtState;
    }

    RTCritSectLeave(&pUVM->vm.s.AtStateCritSect);

    /*
     * Free it.
     */
    pCur->pfnAtState = NULL;
    pCur->pNext = NULL;
    MMR3HeapFree(pCur);

    return VINF_SUCCESS;
}


/**
 * Registers a VM error callback.
 *
 * @returns VBox status code.
 * @param   pUVM            The VM handle.
 * @param   pfnAtError      Pointer to callback.
 * @param   pvUser          User argument.
 * @thread  Any.
 */
VMMR3DECL(int)  VMR3AtErrorRegister(PUVM pUVM, PFNVMATERROR pfnAtError, void *pvUser)
{
    LogFlow(("VMR3AtErrorRegister: pfnAtError=%p pvUser=%p\n", pfnAtError, pvUser));

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnAtError, VERR_INVALID_PARAMETER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /*
     * Allocate a new record.
     */
    PVMATERROR pNew = (PVMATERROR)MMR3HeapAllocU(pUVM, MM_TAG_VM, sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;

    /* fill */
    pNew->pfnAtError = pfnAtError;
    pNew->pvUser     = pvUser;

    /* insert */
    RTCritSectEnter(&pUVM->vm.s.AtErrorCritSect);
    pNew->pNext      = *pUVM->vm.s.ppAtErrorNext;
    *pUVM->vm.s.ppAtErrorNext = pNew;
    pUVM->vm.s.ppAtErrorNext = &pNew->pNext;
    RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);

    return VINF_SUCCESS;
}


/**
 * Deregisters a VM error callback.
 *
 * @returns VBox status code.
 * @param   pUVM            The VM handle.
 * @param   pfnAtError      Pointer to callback.
 * @param   pvUser          User argument.
 * @thread  Any.
 */
VMMR3DECL(int) VMR3AtErrorDeregister(PUVM pUVM, PFNVMATERROR pfnAtError, void *pvUser)
{
    LogFlow(("VMR3AtErrorDeregister: pfnAtError=%p pvUser=%p\n", pfnAtError, pvUser));

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnAtError, VERR_INVALID_PARAMETER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    RTCritSectEnter(&pUVM->vm.s.AtErrorCritSect);

    /*
     * Search the list for the entry.
     */
    PVMATERROR pPrev = NULL;
    PVMATERROR pCur = pUVM->vm.s.pAtError;
    while (     pCur
           &&   (   pCur->pfnAtError != pfnAtError
                 || pCur->pvUser != pvUser))
    {
        pPrev = pCur;
        pCur = pCur->pNext;
    }
    if (!pCur)
    {
        AssertMsgFailed(("pfnAtError=%p was not found\n", pfnAtError));
        RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);
        return VERR_FILE_NOT_FOUND;
    }

    /*
     * Unlink it.
     */
    if (pPrev)
    {
        pPrev->pNext = pCur->pNext;
        if (!pCur->pNext)
            pUVM->vm.s.ppAtErrorNext = &pPrev->pNext;
    }
    else
    {
        pUVM->vm.s.pAtError = pCur->pNext;
        if (!pCur->pNext)
            pUVM->vm.s.ppAtErrorNext = &pUVM->vm.s.pAtError;
    }

    RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);

    /*
     * Free it.
     */
    pCur->pfnAtError = NULL;
    pCur->pNext = NULL;
    MMR3HeapFree(pCur);

    return VINF_SUCCESS;
}


/**
 * Ellipsis to va_list wrapper for calling pfnAtError.
 */
static void vmR3SetErrorWorkerDoCall(PVM pVM, PVMATERROR pCur, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    pCur->pfnAtError(pVM->pUVM, pCur->pvUser, rc, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
}


/**
 * This is a worker function for GC and Ring-0 calls to VMSetError and VMSetErrorV.
 * The message is found in VMINT.
 *
 * @param   pVM             The cross context VM structure.
 * @thread  EMT.
 */
VMMR3_INT_DECL(void) VMR3SetErrorWorker(PVM pVM)
{
    VM_ASSERT_EMT(pVM);
    AssertReleaseMsgFailed(("And we have a winner! You get to implement Ring-0 and GC VMSetErrorV! Congrats!\n"));

    /*
     * Unpack the error (if we managed to format one).
     */
    PVMERROR pErr = pVM->vm.s.pErrorR3;
    const char *pszFile = NULL;
    const char *pszFunction = NULL;
    uint32_t    iLine = 0;
    const char *pszMessage;
    int32_t     rc = VERR_MM_HYPER_NO_MEMORY;
    if (pErr)
    {
        AssertCompile(sizeof(const char) == sizeof(uint8_t));
        if (pErr->offFile)
            pszFile = (const char *)pErr + pErr->offFile;
        iLine = pErr->iLine;
        if (pErr->offFunction)
            pszFunction = (const char *)pErr + pErr->offFunction;
        if (pErr->offMessage)
            pszMessage = (const char *)pErr + pErr->offMessage;
        else
            pszMessage = "No message!";
    }
    else
        pszMessage = "No message! (Failed to allocate memory to put the error message in!)";

    /*
     * Call the at error callbacks.
     */
    PUVM pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->vm.s.AtErrorCritSect);
    ASMAtomicIncU32(&pUVM->vm.s.cRuntimeErrors);
    for (PVMATERROR pCur = pUVM->vm.s.pAtError; pCur; pCur = pCur->pNext)
        vmR3SetErrorWorkerDoCall(pVM, pCur, rc, RT_SRC_POS_ARGS, "%s", pszMessage);
    RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);
}


/**
 * Gets the number of errors raised via VMSetError.
 *
 * This can be used avoid double error messages.
 *
 * @returns The error count.
 * @param   pUVM            The VM handle.
 */
VMMR3_INT_DECL(uint32_t) VMR3GetErrorCount(PUVM pUVM)
{
    AssertPtrReturn(pUVM, 0);
    AssertReturn(pUVM->u32Magic == UVM_MAGIC, 0);
    return pUVM->vm.s.cErrors;
}


/**
 * Creation time wrapper for vmR3SetErrorUV.
 *
 * @returns rc.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   rc              The VBox status code.
 * @param   SRC_POS         The source position of this error.
 * @param   pszFormat       Format string.
 * @param   ...             The arguments.
 * @thread  Any thread.
 */
static int vmR3SetErrorU(PUVM pUVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    vmR3SetErrorUV(pUVM, rc, pszFile, iLine, pszFunction, pszFormat, &va);
    va_end(va);
    return rc;
}


/**
 * Worker which calls everyone listening to the VM error messages.
 *
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   rc              The VBox status code.
 * @param   SRC_POS         The source position of this error.
 * @param   pszFormat       Format string.
 * @param   pArgs           Pointer to the format arguments.
 * @thread  EMT
 */
DECLCALLBACK(void) vmR3SetErrorUV(PUVM pUVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list *pArgs)
{
    /*
     * Log the error.
     */
    va_list va3;
    va_copy(va3, *pArgs);
    RTLogRelPrintf("VMSetError: %s(%d) %s; rc=%Rrc\n"
                   "VMSetError: %N\n",
                   pszFile, iLine, pszFunction, rc,
                   pszFormat, &va3);
    va_end(va3);

#ifdef LOG_ENABLED
    va_copy(va3, *pArgs);
    RTLogPrintf("VMSetError: %s(%d) %s; rc=%Rrc\n"
                "%N\n",
                pszFile, iLine, pszFunction, rc,
                pszFormat, &va3);
    va_end(va3);
#endif

    /*
     * Make a copy of the message.
     */
    if (pUVM->pVM)
        vmSetErrorCopy(pUVM->pVM, rc, RT_SRC_POS_ARGS, pszFormat, *pArgs);

    /*
     * Call the at error callbacks.
     */
    bool fCalledSomeone = false;
    RTCritSectEnter(&pUVM->vm.s.AtErrorCritSect);
    ASMAtomicIncU32(&pUVM->vm.s.cErrors);
    for (PVMATERROR pCur = pUVM->vm.s.pAtError; pCur; pCur = pCur->pNext)
    {
        va_list va2;
        va_copy(va2, *pArgs);
        pCur->pfnAtError(pUVM, pCur->pvUser, rc, RT_SRC_POS_ARGS, pszFormat, va2);
        va_end(va2);
        fCalledSomeone = true;
    }
    RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);
}


/**
 * Sets the error message.
 *
 * @returns rc. Meaning you can do:
 *    @code
 *    return VM_SET_ERROR_U(pUVM, VERR_OF_YOUR_CHOICE, "descriptive message");
 *    @endcode
 * @param   pUVM            The user mode VM handle.
 * @param   rc              VBox status code.
 * @param   SRC_POS         Use RT_SRC_POS.
 * @param   pszFormat       Error message format string.
 * @param   ...             Error message arguments.
 * @thread  Any
 */
VMMR3DECL(int) VMR3SetError(PUVM pUVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rcRet = VMR3SetErrorV(pUVM, rc, pszFile, iLine, pszFunction, pszFormat, va);
    va_end(va);
    return rcRet;
}


/**
 * Sets the error message.
 *
 * @returns rc. Meaning you can do:
 *    @code
 *    return VM_SET_ERROR_U(pUVM, VERR_OF_YOUR_CHOICE, "descriptive message");
 *    @endcode
 * @param   pUVM            The user mode VM handle.
 * @param   rc              VBox status code.
 * @param   SRC_POS         Use RT_SRC_POS.
 * @param   pszFormat       Error message format string.
 * @param   va              Error message arguments.
 * @thread  Any
 */
VMMR3DECL(int) VMR3SetErrorV(PUVM pUVM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /* Take shortcut when called on EMT, skipping VM handle requirement + validation. */
    if (VMR3GetVMCPUThread(pUVM) != NIL_RTTHREAD)
    {
        va_list vaCopy;
        va_copy(vaCopy, va);
        vmR3SetErrorUV(pUVM, rc, RT_SRC_POS_ARGS, pszFormat, &vaCopy);
        va_end(vaCopy);
        return rc;
    }

    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    return VMSetErrorV(pUVM->pVM, rc, pszFile, iLine, pszFunction, pszFormat, va);
}



/**
 * Registers a VM runtime error callback.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM structure.
 * @param   pfnAtRuntimeError   Pointer to callback.
 * @param   pvUser              User argument.
 * @thread  Any.
 */
VMMR3DECL(int)   VMR3AtRuntimeErrorRegister(PUVM pUVM, PFNVMATRUNTIMEERROR pfnAtRuntimeError, void *pvUser)
{
    LogFlow(("VMR3AtRuntimeErrorRegister: pfnAtRuntimeError=%p pvUser=%p\n", pfnAtRuntimeError, pvUser));

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnAtRuntimeError, VERR_INVALID_PARAMETER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /*
     * Allocate a new record.
     */
    PVMATRUNTIMEERROR pNew = (PVMATRUNTIMEERROR)MMR3HeapAllocU(pUVM, MM_TAG_VM, sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;

    /* fill */
    pNew->pfnAtRuntimeError = pfnAtRuntimeError;
    pNew->pvUser            = pvUser;

    /* insert */
    RTCritSectEnter(&pUVM->vm.s.AtErrorCritSect);
    pNew->pNext             = *pUVM->vm.s.ppAtRuntimeErrorNext;
    *pUVM->vm.s.ppAtRuntimeErrorNext = pNew;
    pUVM->vm.s.ppAtRuntimeErrorNext = &pNew->pNext;
    RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);

    return VINF_SUCCESS;
}


/**
 * Deregisters a VM runtime error callback.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pfnAtRuntimeError   Pointer to callback.
 * @param   pvUser              User argument.
 * @thread  Any.
 */
VMMR3DECL(int) VMR3AtRuntimeErrorDeregister(PUVM pUVM, PFNVMATRUNTIMEERROR pfnAtRuntimeError, void *pvUser)
{
    LogFlow(("VMR3AtRuntimeErrorDeregister: pfnAtRuntimeError=%p pvUser=%p\n", pfnAtRuntimeError, pvUser));

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnAtRuntimeError, VERR_INVALID_PARAMETER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    RTCritSectEnter(&pUVM->vm.s.AtErrorCritSect);

    /*
     * Search the list for the entry.
     */
    PVMATRUNTIMEERROR pPrev = NULL;
    PVMATRUNTIMEERROR pCur = pUVM->vm.s.pAtRuntimeError;
    while (     pCur
           &&   (   pCur->pfnAtRuntimeError != pfnAtRuntimeError
                 || pCur->pvUser != pvUser))
    {
        pPrev = pCur;
        pCur = pCur->pNext;
    }
    if (!pCur)
    {
        AssertMsgFailed(("pfnAtRuntimeError=%p was not found\n", pfnAtRuntimeError));
        RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);
        return VERR_FILE_NOT_FOUND;
    }

    /*
     * Unlink it.
     */
    if (pPrev)
    {
        pPrev->pNext = pCur->pNext;
        if (!pCur->pNext)
            pUVM->vm.s.ppAtRuntimeErrorNext = &pPrev->pNext;
    }
    else
    {
        pUVM->vm.s.pAtRuntimeError = pCur->pNext;
        if (!pCur->pNext)
            pUVM->vm.s.ppAtRuntimeErrorNext = &pUVM->vm.s.pAtRuntimeError;
    }

    RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);

    /*
     * Free it.
     */
    pCur->pfnAtRuntimeError = NULL;
    pCur->pNext = NULL;
    MMR3HeapFree(pCur);

    return VINF_SUCCESS;
}


/**
 * EMT rendezvous worker that vmR3SetRuntimeErrorCommon uses to safely change
 * the state to FatalError(LS).
 *
 * @returns VERR_VM_INVALID_VM_STATE or VINF_EM_SUSPEND.  (This is a strict
 *          return code, see FNVMMEMTRENDEZVOUS.)
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser          Ignored.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3SetRuntimeErrorChangeState(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    NOREF(pVCpu);
    Assert(!pvUser); NOREF(pvUser);

    /*
     * The first EMT thru here changes the state.
     */
    if (pVCpu->idCpu == pVM->cCpus - 1)
    {
        int rc = vmR3TrySetState(pVM, "VMSetRuntimeError", 2,
                                 VMSTATE_FATAL_ERROR,    VMSTATE_RUNNING,
                                 VMSTATE_FATAL_ERROR_LS, VMSTATE_RUNNING_LS);
        if (RT_FAILURE(rc))
            return rc;
        if (rc == 2)
            SSMR3Cancel(pVM->pUVM);

        VM_FF_SET(pVM, VM_FF_CHECK_VM_STATE);
    }

    /* This'll make sure we get out of whereever we are (e.g. REM). */
    return VINF_EM_SUSPEND;
}


/**
 * Worker for VMR3SetRuntimeErrorWorker and vmR3SetRuntimeErrorV.
 *
 * This does the common parts after the error has been saved / retrieved.
 *
 * @returns VBox status code with modifications, see VMSetRuntimeErrorV.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fFlags          The error flags.
 * @param   pszErrorId      Error ID string.
 * @param   pszFormat       Format string.
 * @param   pVa             Pointer to the format arguments.
 */
static int vmR3SetRuntimeErrorCommon(PVM pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list *pVa)
{
    LogRel(("VM: Raising runtime error '%s' (fFlags=%#x)\n", pszErrorId, fFlags));
    PUVM pUVM = pVM->pUVM;

    /*
     * Take actions before the call.
     */
    int rc;
    if (fFlags & VMSETRTERR_FLAGS_FATAL)
        rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING | VMMEMTRENDEZVOUS_FLAGS_STOP_ON_ERROR,
                                vmR3SetRuntimeErrorChangeState, NULL);
    else if (fFlags & VMSETRTERR_FLAGS_SUSPEND)
    {
        /* Make sure we don't call VMR3Suspend when we shouldn't.  As seen in
           @bugref{10111} multiple runtime error may be flagged when we run out
           of disk space or similar, so don't freak out VMR3Suspend by calling
           it in an invalid VM state. */
        VMSTATE enmStateCur = pVM->enmVMState;
        if (enmStateCur == VMSTATE_RUNNING || enmStateCur == VMSTATE_RUNNING_LS)
            rc = VMR3Suspend(pUVM, VMSUSPENDREASON_RUNTIME_ERROR);
        else
            rc = VINF_SUCCESS;
    }
    else
        rc = VINF_SUCCESS;

    /*
     * Do the callback round.
     */
    RTCritSectEnter(&pUVM->vm.s.AtErrorCritSect);
    ASMAtomicIncU32(&pUVM->vm.s.cRuntimeErrors);
    for (PVMATRUNTIMEERROR pCur = pUVM->vm.s.pAtRuntimeError; pCur; pCur = pCur->pNext)
    {
        va_list va;
        va_copy(va, *pVa);
        pCur->pfnAtRuntimeError(pUVM, pCur->pvUser, fFlags, pszErrorId, pszFormat, va);
        va_end(va);
    }
    RTCritSectLeave(&pUVM->vm.s.AtErrorCritSect);

    return rc;
}


/**
 * Ellipsis to va_list wrapper for calling vmR3SetRuntimeErrorCommon.
 */
static int vmR3SetRuntimeErrorCommonF(PVM pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = vmR3SetRuntimeErrorCommon(pVM, fFlags, pszErrorId, pszFormat, &va);
    va_end(va);
    return rc;
}


/**
 * This is a worker function for RC and Ring-0 calls to VMSetError and
 * VMSetErrorV.
 *
 * The message is found in VMINT.
 *
 * @returns VBox status code, see VMSetRuntimeError.
 * @param   pVM             The cross context VM structure.
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) VMR3SetRuntimeErrorWorker(PVM pVM)
{
    VM_ASSERT_EMT(pVM);
    AssertReleaseMsgFailed(("And we have a winner! You get to implement Ring-0 and GC VMSetRuntimeErrorV! Congrats!\n"));

    /*
     * Unpack the error (if we managed to format one).
     */
    const char     *pszErrorId = "SetRuntimeError";
    const char     *pszMessage = "No message!";
    uint32_t        fFlags     = VMSETRTERR_FLAGS_FATAL;
    PVMRUNTIMEERROR pErr       = pVM->vm.s.pRuntimeErrorR3;
    if (pErr)
    {
        AssertCompile(sizeof(const char) == sizeof(uint8_t));
        if (pErr->offErrorId)
            pszErrorId = (const char *)pErr + pErr->offErrorId;
        if (pErr->offMessage)
            pszMessage = (const char *)pErr + pErr->offMessage;
        fFlags = pErr->fFlags;
    }

    /*
     * Join cause with vmR3SetRuntimeErrorV.
     */
    return vmR3SetRuntimeErrorCommonF(pVM, fFlags, pszErrorId, "%s", pszMessage);
}


/**
 * Worker for VMSetRuntimeErrorV for doing the job on EMT in ring-3.
 *
 * @returns VBox status code with modifications, see VMSetRuntimeErrorV.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fFlags          The error flags.
 * @param   pszErrorId      Error ID string.
 * @param   pszMessage      The error message residing the MM heap.
 *
 * @thread  EMT
 */
DECLCALLBACK(int) vmR3SetRuntimeError(PVM pVM, uint32_t fFlags, const char *pszErrorId, char *pszMessage)
{
#if 0 /** @todo make copy of the error msg. */
    /*
     * Make a copy of the message.
     */
    va_list va2;
    va_copy(va2, *pVa);
    vmSetRuntimeErrorCopy(pVM, fFlags, pszErrorId, pszFormat, va2);
    va_end(va2);
#endif

    /*
     * Join paths with VMR3SetRuntimeErrorWorker.
     */
    int rc = vmR3SetRuntimeErrorCommonF(pVM, fFlags, pszErrorId, "%s", pszMessage);
    MMR3HeapFree(pszMessage);
    return rc;
}


/**
 * Worker for VMSetRuntimeErrorV for doing the job on EMT in ring-3.
 *
 * @returns VBox status code with modifications, see VMSetRuntimeErrorV.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fFlags          The error flags.
 * @param   pszErrorId      Error ID string.
 * @param   pszFormat       Format string.
 * @param   pVa             Pointer to the format arguments.
 *
 * @thread  EMT
 */
DECLCALLBACK(int) vmR3SetRuntimeErrorV(PVM pVM, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list *pVa)
{
    /*
     * Make a copy of the message.
     */
    va_list va2;
    va_copy(va2, *pVa);
    vmSetRuntimeErrorCopy(pVM, fFlags, pszErrorId, pszFormat, va2);
    va_end(va2);

    /*
     * Join paths with VMR3SetRuntimeErrorWorker.
     */
    return vmR3SetRuntimeErrorCommon(pVM, fFlags, pszErrorId, pszFormat, pVa);
}


/**
 * Gets the number of runtime errors raised via VMR3SetRuntimeError.
 *
 * This can be used avoid double error messages.
 *
 * @returns The runtime error count.
 * @param   pUVM            The user mode VM handle.
 */
VMMR3_INT_DECL(uint32_t) VMR3GetRuntimeErrorCount(PUVM pUVM)
{
    return pUVM->vm.s.cRuntimeErrors;
}


/**
 * Gets the ID virtual of the virtual CPU associated with the calling thread.
 *
 * @returns The CPU ID. NIL_VMCPUID if the thread isn't an EMT.
 *
 * @param   pVM             The cross context VM structure.
 */
VMMR3_INT_DECL(RTCPUID) VMR3GetVMCPUId(PVM pVM)
{
    PUVMCPU pUVCpu = (PUVMCPU)RTTlsGet(pVM->pUVM->vm.s.idxTLS);
    return pUVCpu
         ? pUVCpu->idCpu
         : NIL_VMCPUID;
}


/**
 * Checks if the VM is long-mode (64-bit) capable or not.
 *
 * @returns true if VM can operate in long-mode, false otherwise.
 * @param   pVM             The cross context VM structure.
 */
VMMR3_INT_DECL(bool) VMR3IsLongModeAllowed(PVM pVM)
{
    switch (pVM->bMainExecutionEngine)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case VM_EXEC_ENGINE_HW_VIRT:
            return HMIsLongModeAllowed(pVM);
#endif

        case VM_EXEC_ENGINE_NATIVE_API:
            return NEMHCIsLongModeAllowed(pVM);

        case VM_EXEC_ENGINE_NOT_SET:
            AssertFailed();
            RT_FALL_THRU();
        default:
            return false;
    }
}


/**
 * Returns the native ID of the current EMT VMCPU thread.
 *
 * @returns Handle if this is an EMT thread; NIL_RTNATIVETHREAD otherwise
 * @param   pVM             The cross context VM structure.
 * @thread  EMT
 */
VMMR3DECL(RTNATIVETHREAD) VMR3GetVMCPUNativeThread(PVM pVM)
{
    PUVMCPU pUVCpu = (PUVMCPU)RTTlsGet(pVM->pUVM->vm.s.idxTLS);

    if (!pUVCpu)
        return NIL_RTNATIVETHREAD;

    return pUVCpu->vm.s.NativeThreadEMT;
}


/**
 * Returns the native ID of the current EMT VMCPU thread.
 *
 * @returns Handle if this is an EMT thread; NIL_RTNATIVETHREAD otherwise
 * @param   pUVM        The user mode VM structure.
 * @thread  EMT
 */
VMMR3DECL(RTNATIVETHREAD) VMR3GetVMCPUNativeThreadU(PUVM pUVM)
{
    PUVMCPU pUVCpu = (PUVMCPU)RTTlsGet(pUVM->vm.s.idxTLS);

    if (!pUVCpu)
        return NIL_RTNATIVETHREAD;

    return pUVCpu->vm.s.NativeThreadEMT;
}


/**
 * Returns the handle of the current EMT VMCPU thread.
 *
 * @returns Handle if this is an EMT thread; NIL_RTNATIVETHREAD otherwise
 * @param   pUVM            The user mode VM handle.
 * @thread  EMT
 */
VMMR3DECL(RTTHREAD) VMR3GetVMCPUThread(PUVM pUVM)
{
    PUVMCPU pUVCpu = (PUVMCPU)RTTlsGet(pUVM->vm.s.idxTLS);

    if (!pUVCpu)
        return NIL_RTTHREAD;

    return pUVCpu->vm.s.ThreadEMT;
}


/**
 * Returns the handle of the current EMT VMCPU thread.
 *
 * @returns The IPRT thread handle.
 * @param   pUVCpu          The user mode CPU handle.
 * @thread  EMT
 */
VMMR3_INT_DECL(RTTHREAD) VMR3GetThreadHandle(PUVMCPU pUVCpu)
{
    return pUVCpu->vm.s.ThreadEMT;
}


/**
 * Return the package and core ID of a CPU.
 *
 * @returns VBOX status code.
 * @param   pUVM             The user mode VM handle.
 * @param   idCpu            Virtual CPU to get the ID from.
 * @param   pidCpuCore       Where to store the core ID of the virtual CPU.
 * @param   pidCpuPackage    Where to store the package ID of the virtual CPU.
 *
 */
VMMR3DECL(int) VMR3GetCpuCoreAndPackageIdFromCpuId(PUVM pUVM, VMCPUID idCpu, uint32_t *pidCpuCore, uint32_t *pidCpuPackage)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pidCpuCore, VERR_INVALID_POINTER);
    AssertPtrReturn(pidCpuPackage, VERR_INVALID_POINTER);
    if (idCpu >= pVM->cCpus)
        return VERR_INVALID_CPU_ID;

    /*
     * Set return values.
     */
#ifdef VBOX_WITH_MULTI_CORE
    *pidCpuCore    = idCpu;
    *pidCpuPackage = 0;
#else
    *pidCpuCore    = 0;
    *pidCpuPackage = idCpu;
#endif

    return VINF_SUCCESS;
}


/**
 * Worker for VMR3HotUnplugCpu.
 *
 * @returns VINF_EM_WAIT_SPIP (strict status code).
 * @param   pVM                 The cross context VM structure.
 * @param   idCpu               The current CPU.
 */
static DECLCALLBACK(int) vmR3HotUnplugCpu(PVM pVM, VMCPUID idCpu)
{
    PVMCPU pVCpu = VMMGetCpuById(pVM, idCpu);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Reset per CPU resources.
     *
     * Actually only needed for VT-x because the CPU seems to be still in some
     * paged mode and startup fails after a new hot plug event. SVM works fine
     * even without this.
     */
    Log(("vmR3HotUnplugCpu for VCPU %u\n", idCpu));
    PGMR3ResetCpu(pVM, pVCpu);
    PDMR3ResetCpu(pVCpu);
    TRPMR3ResetCpu(pVCpu);
    CPUMR3ResetCpu(pVM, pVCpu);
    EMR3ResetCpu(pVCpu);
    HMR3ResetCpu(pVCpu);
    NEMR3ResetCpu(pVCpu, false /*fInitIpi*/);
    return VINF_EM_WAIT_SIPI;
}


/**
 * Hot-unplugs a CPU from the guest.
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 * @param   idCpu   Virtual CPU to perform the hot unplugging operation on.
 */
VMMR3DECL(int) VMR3HotUnplugCpu(PUVM pUVM, VMCPUID idCpu)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);

    /** @todo r=bird: Don't destroy the EMT, it'll break VMMR3EmtRendezvous and
     *        broadcast requests.  Just note down somewhere that the CPU is
     *        offline and send it to SPIP wait.  Maybe modify VMCPUSTATE and push
     *        it out of the EM loops when offline. */
    return VMR3ReqCallNoWaitU(pUVM, idCpu, (PFNRT)vmR3HotUnplugCpu, 2, pVM, idCpu);
}


/**
 * Hot-plugs a CPU on the guest.
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 * @param   idCpu   Virtual CPU to perform the hot plugging operation on.
 */
VMMR3DECL(int) VMR3HotPlugCpu(PUVM pUVM, VMCPUID idCpu)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);

    /** @todo r-bird: Just mark it online and make sure it waits on SPIP. */
    return VINF_SUCCESS;
}


/**
 * Changes the VMM execution cap.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM structure.
 * @param   uCpuExecutionCap    New CPU execution cap in precent, 1-100. Where
 *                              100 is max performance (default).
 */
VMMR3DECL(int) VMR3SetCpuExecutionCap(PUVM pUVM, uint32_t uCpuExecutionCap)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(uCpuExecutionCap > 0 && uCpuExecutionCap <= 100, VERR_INVALID_PARAMETER);

    Log(("VMR3SetCpuExecutionCap: new priority = %d\n", uCpuExecutionCap));
    /* Note: not called from EMT. */
    pVM->uCpuExecutionCap = uCpuExecutionCap;
    return VINF_SUCCESS;
}


/**
 * Control whether the VM should power off when resetting.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   fPowerOffInsteadOfReset Flag whether the VM should power off when
 *                                  resetting.
 */
VMMR3DECL(int) VMR3SetPowerOffInsteadOfReset(PUVM pUVM, bool fPowerOffInsteadOfReset)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /* Note: not called from EMT. */
    pVM->vm.s.fPowerOffInsteadOfReset = fPowerOffInsteadOfReset;
    return VINF_SUCCESS;
}

