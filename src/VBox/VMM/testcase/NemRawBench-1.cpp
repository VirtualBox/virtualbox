/* $Id: NemRawBench-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NEM Benchmark.
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
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <WinHvPlatform.h>
# if !defined(_INTPTR) && defined(_M_AMD64) /* void pedantic stdint.h warnings  */
#  define _INTPTR 2
# endif

#elif defined(RT_OS_LINUX)
# include <linux/kvm.h>
# include <errno.h>
# include <sys/fcntl.h>
# include <sys/ioctl.h>
# include <sys/mman.h>
# include <unistd.h>
# include <time.h>

#elif defined(RT_OS_DARWIN)
# include <Availability.h>
# if 1 /* header mix hack */
#  undef __OSX_AVAILABLE_STARTING
#  define __OSX_AVAILABLE_STARTING(_osx, _ios)
# endif
# include <Hypervisor/hv.h>
# include <Hypervisor/hv_arch_x86.h>
# include <Hypervisor/hv_arch_vmx.h>
# include <Hypervisor/hv_vmx.h>
# include <mach/mach_time.h>
# include <mach/kern_return.h>
# include <sys/time.h>
# include <time.h>
# include <sys/mman.h>
# include <errno.h>

#else
# error "port me"
#endif

#include <iprt/stream.h>
#include <iprt/stdarg.h>
#include <iprt/types.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The base mapping address of the g_pbMem. */
#define MY_MEM_BASE             0x1000
/** No-op MMIO access address.   */
#define MY_NOP_MMIO             0x0808
/** The RIP which the testcode starts. */
#define MY_TEST_RIP             0x2000

/** The test termination port number. */
#define MY_TERM_PORT            0x01
/** The no-op test port number. */
#define MY_NOP_PORT             0x7f

#define MY_TEST_F_NOP_IO        (1U<<0)
#define MY_TEST_F_CPUID         (1U<<1)
#define MY_TEST_F_NOP_MMIO      (1U<<2)



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Chunk of memory mapped at address 0x1000 (MY_MEM_BASE). */
static unsigned char           *g_pbMem;
/** Amount of RAM at address 0x1000 (MY_MEM_BASE). */
static size_t                   g_cbMem;
#ifdef RT_OS_WINDOWS
static WHV_PARTITION_HANDLE     g_hPartition = NULL;

/** @name APIs imported from WinHvPlatform.dll
 * @{ */
static decltype(WHvCreatePartition)                *g_pfnWHvCreatePartition;
static decltype(WHvSetupPartition)                 *g_pfnWHvSetupPartition;
static decltype(WHvGetPartitionProperty)           *g_pfnWHvGetPartitionProperty;
static decltype(WHvSetPartitionProperty)           *g_pfnWHvSetPartitionProperty;
static decltype(WHvMapGpaRange)                    *g_pfnWHvMapGpaRange;
static decltype(WHvCreateVirtualProcessor)         *g_pfnWHvCreateVirtualProcessor;
static decltype(WHvRunVirtualProcessor)            *g_pfnWHvRunVirtualProcessor;
static decltype(WHvGetVirtualProcessorRegisters)   *g_pfnWHvGetVirtualProcessorRegisters;
static decltype(WHvSetVirtualProcessorRegisters)   *g_pfnWHvSetVirtualProcessorRegisters;
/** @} */
static uint64_t (WINAPI                            *g_pfnRtlGetSystemTimePrecise)(void);

#elif defined(RT_OS_LINUX)
/** The VM handle.   */
static int                      g_fdVm;
/** The VCPU handle.   */
static int                      g_fdVCpu;
/** The kvm_run structure for the VCpu. */
static struct kvm_run          *g_pVCpuRun;
/** The size of the g_pVCpuRun mapping. */
static ssize_t                  g_cbVCpuRun;

#elif defined(RT_OS_DARWIN)
/** The VCpu ID. */
static hv_vcpuid_t              g_idVCpu;
#endif


static int error(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "error: ");
    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);
    return 1;
}


static uint64_t getNanoTS(void)
{
#ifdef RT_OS_WINDOWS
    return g_pfnRtlGetSystemTimePrecise() * 100;

#elif defined(RT_OS_LINUX)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;

#elif defined(RT_OS_DARWIN)
    static struct mach_timebase_info    s_Info = { 0, 0 };
    static double                       s_rdFactor = 0.0;
    /* Lazy init. */
    if (s_Info.denom != 0)
    { /* likely */ }
    else if (mach_timebase_info(&s_Info) == KERN_SUCCESS)
        s_rdFactor = (double)s_Info.numer / (double)s_Info.denom;
    else
    {
        error("mach_timebase_info(&Info) failed\n");
        exit(1);
    }
    if (s_Info.denom == 1 && s_Info.numer == 1) /* special case: absolute time is in nanoseconds */
        return mach_absolute_time();
    return mach_absolute_time() * s_rdFactor;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * UINT64_C(1000000000)
         + (tv.tv_usec * UINT32_C(1000));
#endif
}


char *formatNum(uint64_t uNum, unsigned cchWidth, char *pszDst, size_t cbDst)
{
    char szTmp[64 + 22];
    size_t cchTmp = RTStrPrintf(szTmp, sizeof(szTmp) - 22, "%llu", (unsigned long long)uNum);
    size_t cSeps  = (cchTmp - 1) / 3;
    size_t const cchTotal = cchTmp + cSeps;
    if (cSeps)
    {
        szTmp[cchTotal] = '\0';
        for (size_t iSrc = cchTmp, iDst = cchTotal; cSeps > 0; cSeps--)
        {
            szTmp[--iDst] = szTmp[--iSrc];
            szTmp[--iDst] = szTmp[--iSrc];
            szTmp[--iDst] = szTmp[--iSrc];
            szTmp[--iDst] = ' ';
        }
    }

    size_t offDst = 0;
    while (cchWidth-- > cchTotal && offDst < cbDst)
        pszDst[offDst++] = ' ';
    size_t offSrc = 0;
    while (offSrc < cchTotal && offDst < cbDst)
        pszDst[offDst++] = szTmp[offSrc++];
    pszDst[offDst] = '\0';
    return pszDst;
}


int reportResult(const char *pszInstruction, uint32_t cInstructions, uint64_t nsElapsed, uint32_t cExits)
{
    uint64_t const cInstrPerSec = nsElapsed ? (uint64_t)cInstructions * 1000000000 / nsElapsed : 0;
    char szTmp1[64], szTmp2[64], szTmp3[64];
    RTPrintf("%s %7s instructions per second (%s exits in %s ns)\n",
             formatNum(cInstrPerSec, 10, szTmp1, sizeof(szTmp1)), pszInstruction,
             formatNum(cExits, 0, szTmp2, sizeof(szTmp2)),
             formatNum(nsElapsed, 0, szTmp3, sizeof(szTmp3)));
    return 0;
}



#ifdef RT_OS_WINDOWS

/*
 * Windows - Hyper-V Platform API.
 */

static int createVM(void)
{
    /*
     * Resolve APIs.
     */
    HMODULE hmod = LoadLibraryW(L"WinHvPlatform.dll");
    if (hmod == NULL)
        return error("Error loading WinHvPlatform.dll: %u\n", GetLastError());
    static struct { const char *pszFunction; FARPROC *ppfn; } const s_aImports[] =
    {
# define IMPORT_ENTRY(a_Name) { #a_Name, (FARPROC *)&g_pfn##a_Name }
        IMPORT_ENTRY(WHvCreatePartition),
        IMPORT_ENTRY(WHvSetupPartition),
        IMPORT_ENTRY(WHvGetPartitionProperty),
        IMPORT_ENTRY(WHvSetPartitionProperty),
        IMPORT_ENTRY(WHvMapGpaRange),
        IMPORT_ENTRY(WHvCreateVirtualProcessor),
        IMPORT_ENTRY(WHvRunVirtualProcessor),
        IMPORT_ENTRY(WHvGetVirtualProcessorRegisters),
        IMPORT_ENTRY(WHvSetVirtualProcessorRegisters),
# undef IMPORT_ENTRY
    };
    FARPROC pfn;
    for (size_t i = 0; i < sizeof(s_aImports) / sizeof(s_aImports[0]); i++)
    {
        *s_aImports[i].ppfn = pfn = GetProcAddress(hmod, s_aImports[i].pszFunction);
        if (!pfn)
            return error("Error resolving WinHvPlatform.dll!%s: %u\n", s_aImports[i].pszFunction, GetLastError());
    }
# ifndef IN_SLICKEDIT
#  define WHvCreatePartition                         g_pfnWHvCreatePartition
#  define WHvSetupPartition                          g_pfnWHvSetupPartition
#  define WHvGetPartitionProperty                    g_pfnWHvGetPartitionProperty
#  define WHvSetPartitionProperty                    g_pfnWHvSetPartitionProperty
#  define WHvMapGpaRange                             g_pfnWHvMapGpaRange
#  define WHvCreateVirtualProcessor                  g_pfnWHvCreateVirtualProcessor
#  define WHvRunVirtualProcessor                     g_pfnWHvRunVirtualProcessor
#  define WHvGetVirtualProcessorRegisters            g_pfnWHvGetVirtualProcessorRegisters
#  define WHvSetVirtualProcessorRegisters            g_pfnWHvSetVirtualProcessorRegisters
# endif
    /* Need a precise time function. */
    *(FARPROC *)&g_pfnRtlGetSystemTimePrecise = pfn = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetSystemTimePrecise");
    if (pfn == NULL)
        return error("Error resolving ntdll.dll!RtlGetSystemTimePrecise: %u\n", GetLastError());

    /*
     * Create the partition with 1 CPU and the specfied amount of memory.
     */
    WHV_PARTITION_HANDLE hPartition;
    HRESULT hrc = WHvCreatePartition(&hPartition);
    if (!SUCCEEDED(hrc))
        return error("WHvCreatePartition failed: %#x\n", hrc);
    g_hPartition = hPartition;

    WHV_PARTITION_PROPERTY Property;
    memset(&Property, 0, sizeof(Property));
    Property.ProcessorCount = 1;
    hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeProcessorCount, &Property, sizeof(Property));
    if (!SUCCEEDED(hrc))
        return error("WHvSetPartitionProperty/WHvPartitionPropertyCodeProcessorCount failed: %#x\n", hrc);

    memset(&Property, 0, sizeof(Property));
    Property.ExtendedVmExits.X64CpuidExit  = 1;
    Property.ExtendedVmExits.X64MsrExit    = 1;
    hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeExtendedVmExits, &Property, sizeof(Property));
    if (!SUCCEEDED(hrc))
        return error("WHvSetPartitionProperty/WHvPartitionPropertyCodeExtendedVmExits failed: %#x\n", hrc);

    hrc = WHvSetupPartition(hPartition);
    if (!SUCCEEDED(hrc))
        return error("WHvSetupPartition failed: %#x\n", hrc);

    hrc = WHvCreateVirtualProcessor(hPartition, 0 /*idVCpu*/, 0 /*fFlags*/);
    if (!SUCCEEDED(hrc))
        return error("WHvCreateVirtualProcessor failed: %#x\n", hrc);

    g_pbMem = (unsigned char *)VirtualAlloc(NULL, g_cbMem, MEM_COMMIT, PAGE_READWRITE);
    if (!g_pbMem)
        return error("VirtualAlloc failed: %u\n", GetLastError());
    memset(g_pbMem, 0xcc, g_cbMem);

    hrc = WHvMapGpaRange(hPartition, g_pbMem, MY_MEM_BASE /*GCPhys*/, g_cbMem,
                         WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
    if (!SUCCEEDED(hrc))
        return error("WHvMapGpaRange failed: %#x\n", hrc);

    WHV_RUN_VP_EXIT_CONTEXT ExitInfo;
    memset(&ExitInfo, 0, sizeof(ExitInfo));
    WHvRunVirtualProcessor(g_hPartition, 0 /*idCpu*/, &ExitInfo, sizeof(ExitInfo));

    return 0;
}


static int runtimeError(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "runtime error: ");
    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    static struct { const char *pszName; WHV_REGISTER_NAME enmName; unsigned uType; } const s_aRegs[] =
    {
        { "rip",    WHvX64RegisterRip, 64 },
        { "cs",     WHvX64RegisterCs, 1 },
        { "rflags", WHvX64RegisterRflags, 32 },
        { "rax",    WHvX64RegisterRax, 64 },
        { "rcx",    WHvX64RegisterRcx, 64 },
        { "rdx",    WHvX64RegisterRdx, 64 },
        { "rbx",    WHvX64RegisterRbx, 64 },
        { "rsp",    WHvX64RegisterRsp, 64 },
        { "ss",     WHvX64RegisterSs, 1 },
        { "rbp",    WHvX64RegisterRbp, 64 },
        { "rsi",    WHvX64RegisterRsi, 64 },
        { "rdi",    WHvX64RegisterRdi, 64 },
        { "ds",     WHvX64RegisterDs, 1 },
        { "es",     WHvX64RegisterEs, 1 },
        { "fs",     WHvX64RegisterFs, 1 },
        { "gs",     WHvX64RegisterGs, 1 },
        { "cr0",    WHvX64RegisterCr0, 64 },
        { "cr2",    WHvX64RegisterCr2, 64 },
        { "cr3",    WHvX64RegisterCr3, 64 },
        { "cr4",    WHvX64RegisterCr4, 64 },
    };
    for (unsigned i = 0; i < sizeof(s_aRegs) / sizeof(s_aRegs[0]); i++)
    {
        WHV_REGISTER_VALUE Value;
        WHV_REGISTER_NAME  enmName = s_aRegs[i].enmName;
        HRESULT hrc = WHvGetVirtualProcessorRegisters(g_hPartition, 0 /*idCpu*/, &enmName, 1, &Value);
        if (SUCCEEDED(hrc))
        {
            if (s_aRegs[i].uType == 32)
                RTStrmPrintf(g_pStdErr, "%8s=%08x\n", s_aRegs[i].pszName, Value.Reg32);
            else if (s_aRegs[i].uType == 64)
                RTStrmPrintf(g_pStdErr, "%8s=%08x'%08x\n", s_aRegs[i].pszName, (unsigned)(Value.Reg64 >> 32), Value.Reg32);
            else if (s_aRegs[i].uType == 1)
                RTStrmPrintf(g_pStdErr, "%8s=%04x  base=%08x'%08x  limit=%08x attr=%04x\n", s_aRegs[i].pszName,
                             Value.Segment.Selector, (unsigned)(Value.Segment.Base >> 32), (unsigned)Value.Segment.Base,
                             Value.Segment.Limit, Value.Segment.Attributes);
        }
        else
            RTStrmPrintf(g_pStdErr, "%8s=<WHvGetVirtualProcessorRegisters failed %#x>\n", s_aRegs[i].pszName, hrc);
    }

    return 1;
}


static int runRealModeTest(unsigned cInstructions, const char *pszInstruction, unsigned fTest,
                           unsigned uEax, unsigned uEcx, unsigned uEdx, unsigned uEbx,
                           unsigned uEsp, unsigned uEbp, unsigned uEsi, unsigned uEdi)
{
    (void)fTest;

    /*
     * Initialize the real mode context.
     */
# define ADD_REG64(a_enmName, a_uValue) do { \
            aenmNames[iReg]      = (a_enmName); \
            aValues[iReg].Reg128.High64 = 0; \
            aValues[iReg].Reg64  = (a_uValue); \
            iReg++; \
        } while (0)
# define ADD_SEG(a_enmName, a_Base, a_Limit, a_Sel, a_fCode) \
        do { \
            aenmNames[iReg]                  = a_enmName; \
            aValues[iReg].Segment.Base       = (a_Base); \
            aValues[iReg].Segment.Limit      = (a_Limit); \
            aValues[iReg].Segment.Selector   = (a_Sel); \
            aValues[iReg].Segment.Attributes = a_fCode ? 0x9b : 0x93; \
            iReg++; \
        } while (0)
    WHV_REGISTER_NAME  aenmNames[80];
    WHV_REGISTER_VALUE aValues[80];
    unsigned iReg = 0;
    ADD_REG64(WHvX64RegisterRax, uEax);
    ADD_REG64(WHvX64RegisterRcx, uEcx);
    ADD_REG64(WHvX64RegisterRdx, uEdx);
    ADD_REG64(WHvX64RegisterRbx, uEbx);
    ADD_REG64(WHvX64RegisterRsp, uEsp);
    ADD_REG64(WHvX64RegisterRbp, uEbp);
    ADD_REG64(WHvX64RegisterRsi, uEsi);
    ADD_REG64(WHvX64RegisterRdi, uEdi);
    ADD_REG64(WHvX64RegisterRip, MY_TEST_RIP);
    ADD_REG64(WHvX64RegisterRflags, 2);
    ADD_SEG(WHvX64RegisterEs, 0x00000, 0xffff, 0x0000, 0);
    ADD_SEG(WHvX64RegisterCs, 0x00000, 0xffff, 0x0000, 1);
    ADD_SEG(WHvX64RegisterSs, 0x00000, 0xffff, 0x0000, 0);
    ADD_SEG(WHvX64RegisterDs, 0x00000, 0xffff, 0x0000, 0);
    ADD_SEG(WHvX64RegisterFs, 0x00000, 0xffff, 0x0000, 0);
    ADD_SEG(WHvX64RegisterGs, 0x00000, 0xffff, 0x0000, 0);
    ADD_REG64(WHvX64RegisterCr0, 0x10010 /*WP+ET*/);
    ADD_REG64(WHvX64RegisterCr2, 0);
    ADD_REG64(WHvX64RegisterCr3, 0);
    ADD_REG64(WHvX64RegisterCr4, 0);
    HRESULT hrc = WHvSetVirtualProcessorRegisters(g_hPartition, 0 /*idCpu*/, aenmNames, iReg, aValues);
    if (!SUCCEEDED(hrc))
        return error("WHvSetVirtualProcessorRegisters failed (for %s): %#x\n", pszInstruction, hrc);
# undef ADD_REG64
# undef ADD_SEG

    /*
     * Run the test.
     */
    uint32_t cExits = 0;
    uint64_t const nsStart = getNanoTS();
    for (;;)
    {
        WHV_RUN_VP_EXIT_CONTEXT ExitInfo;
        memset(&ExitInfo, 0, sizeof(ExitInfo));
        hrc = WHvRunVirtualProcessor(g_hPartition, 0 /*idCpu*/, &ExitInfo, sizeof(ExitInfo));
        if (SUCCEEDED(hrc))
        {
            cExits++;
            if (ExitInfo.ExitReason == WHvRunVpExitReasonX64IoPortAccess)
            {
                if (ExitInfo.IoPortAccess.PortNumber == MY_NOP_PORT)
                { /* likely: nop instruction */ }
                else if (ExitInfo.IoPortAccess.PortNumber == MY_TERM_PORT)
                    break;
                else
                    return runtimeError("Unexpected I/O port access (for %s): %#x\n", pszInstruction, ExitInfo.IoPortAccess.PortNumber);

                /* Advance. */
                if (ExitInfo.VpContext.InstructionLength)
                {
                    aenmNames[0] = WHvX64RegisterRip;
                    aValues[0].Reg64 = ExitInfo.VpContext.Rip + ExitInfo.VpContext.InstructionLength;
                    hrc = WHvSetVirtualProcessorRegisters(g_hPartition, 0 /*idCpu*/, aenmNames, 1, aValues);
                    if (SUCCEEDED(hrc))
                    { /* likely */ }
                    else
                        return runtimeError("Error advancing RIP (for %s): %#x\n", pszInstruction, hrc);
                }
                else
                    return runtimeError("VpContext.InstructionLength is zero (for %s)\n", pszInstruction);
            }
            else if (ExitInfo.ExitReason == WHvRunVpExitReasonX64Cpuid)
            {
                /* Advance RIP and set default results. */
                if (ExitInfo.VpContext.InstructionLength)
                {
                    aenmNames[0] = WHvX64RegisterRip;
                    aValues[0].Reg64 = ExitInfo.VpContext.Rip + ExitInfo.VpContext.InstructionLength;
                    aenmNames[1] = WHvX64RegisterRax;
                    aValues[1].Reg64 = ExitInfo.CpuidAccess.DefaultResultRax;
                    aenmNames[2] = WHvX64RegisterRcx;
                    aValues[2].Reg64 = ExitInfo.CpuidAccess.DefaultResultRcx;
                    aenmNames[3] = WHvX64RegisterRdx;
                    aValues[3].Reg64 = ExitInfo.CpuidAccess.DefaultResultRdx;
                    aenmNames[4] = WHvX64RegisterRbx;
                    aValues[4].Reg64 = ExitInfo.CpuidAccess.DefaultResultRbx;
                    hrc = WHvSetVirtualProcessorRegisters(g_hPartition, 0 /*idCpu*/, aenmNames, 5, aValues);
                    if (SUCCEEDED(hrc))
                    { /* likely */ }
                    else
                        return runtimeError("Error advancing RIP (for %s): %#x\n", pszInstruction, hrc);
                }
                else
                    return runtimeError("VpContext.InstructionLength is zero (for %s)\n", pszInstruction);
            }
            else if (ExitInfo.ExitReason == WHvRunVpExitReasonMemoryAccess)
            {
                if (ExitInfo.MemoryAccess.Gpa == MY_NOP_MMIO)
                { /* likely: nop address */ }
                else
                    return runtimeError("Unexpected memory access (for %s): %#x\n", pszInstruction, ExitInfo.MemoryAccess.Gpa);

                /* Advance and set return register (assuming RAX and two byte instruction). */
                aenmNames[0] = WHvX64RegisterRip;
                if (ExitInfo.VpContext.InstructionLength)
                    aValues[0].Reg64 = ExitInfo.VpContext.Rip + ExitInfo.VpContext.InstructionLength;
                else
                    aValues[0].Reg64 = ExitInfo.VpContext.Rip + 2;
                aenmNames[1] = WHvX64RegisterRax;
                aValues[1].Reg64 = 42;
                hrc = WHvSetVirtualProcessorRegisters(g_hPartition, 0 /*idCpu*/, aenmNames, 2, aValues);
                if (SUCCEEDED(hrc))
                { /* likely */ }
                else
                    return runtimeError("Error advancing RIP (for %s): %#x\n", pszInstruction, hrc);
            }
            else
                return runtimeError("Unexpected exit (for %s): %#x\n", pszInstruction, ExitInfo.ExitReason);
        }
        else
            return runtimeError("WHvRunVirtualProcessor failed (for %s): %#x\n", pszInstruction, hrc);
    }
    uint64_t const nsElapsed = getNanoTS() - nsStart;
    return reportResult(pszInstruction, cInstructions, nsElapsed, cExits);
}



#elif defined(RT_OS_LINUX)

/*
 * GNU/linux - KVM
 */

static int createVM(void)
{
    int fd = open("/dev/kvm", O_RDWR);
    if (fd < 0)
        return error("Error opening /dev/kvm: %d\n", errno);

    g_fdVm = ioctl(fd, KVM_CREATE_VM, (uintptr_t)0);
    if (g_fdVm < 0)
        return error("KVM_CREATE_VM failed: %d\n", errno);

    /* Create the VCpu. */
    g_cbVCpuRun = ioctl(fd, KVM_GET_VCPU_MMAP_SIZE, (uintptr_t)0);
    if (g_cbVCpuRun <= 0x1000 || (g_cbVCpuRun & 0xfff))
        return error("Failed to get KVM_GET_VCPU_MMAP_SIZE: %#xz errno=%d\n", g_cbVCpuRun, errno);

    g_fdVCpu = ioctl(g_fdVm, KVM_CREATE_VCPU, (uintptr_t)0);
    if (g_fdVCpu < 0)
        return error("KVM_CREATE_VCPU failed: %d\n", errno);

    g_pVCpuRun = (struct kvm_run *)mmap(NULL, g_cbVCpuRun, PROT_READ | PROT_WRITE, MAP_PRIVATE, g_fdVCpu, 0);
    if ((void *)g_pVCpuRun == MAP_FAILED)
        return error("mmap kvm_run failed: %d\n", errno);

    /* Memory. */
    g_pbMem = (unsigned char *)mmap(NULL, g_cbMem, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((void *)g_pbMem == MAP_FAILED)
        return error("mmap RAM failed: %d\n", errno);

    struct kvm_userspace_memory_region MemReg;
    MemReg.slot            = 0;
    MemReg.flags           = 0;
    MemReg.guest_phys_addr = MY_MEM_BASE;
    MemReg.memory_size     = g_cbMem;
    MemReg.userspace_addr  = (uintptr_t)g_pbMem;
    int rc = ioctl(g_fdVm, KVM_SET_USER_MEMORY_REGION, &MemReg);
    if (rc != 0)
        return error("KVM_SET_USER_MEMORY_REGION failed: %d (%d)\n", errno, rc);

    close(fd);
    return 0;
}


static void printSReg(const char *pszName, struct kvm_segment const *pSReg)
{
    RTStrmPrintf(g_pStdErr, "     %5s=%04x  base=%016llx  limit=%08x type=%#x p=%d dpl=%d db=%d s=%d l=%d g=%d avl=%d un=%d\n",
                 pszName, pSReg->selector, pSReg->base, pSReg->limit, pSReg->type, pSReg->present, pSReg->dpl,
                 pSReg->db, pSReg->s, pSReg->l, pSReg->g, pSReg->avl, pSReg->unusable);
}


static int runtimeError(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "runtime error: ");
    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    RTStrmPrintf(g_pStdErr, "                  exit_reason=%#010x\n", g_pVCpuRun->exit_reason);
    RTStrmPrintf(g_pStdErr, "ready_for_interrupt_injection=%#x\n", g_pVCpuRun->ready_for_interrupt_injection);
    RTStrmPrintf(g_pStdErr, "                      if_flag=%#x\n", g_pVCpuRun->if_flag);
    RTStrmPrintf(g_pStdErr, "                        flags=%#x\n", g_pVCpuRun->flags);
    RTStrmPrintf(g_pStdErr, "               kvm_valid_regs=%#018llx\n", g_pVCpuRun->kvm_valid_regs);
    RTStrmPrintf(g_pStdErr, "               kvm_dirty_regs=%#018llx\n", g_pVCpuRun->kvm_dirty_regs);

    struct kvm_regs Regs;
    memset(&Regs, 0, sizeof(Regs));
    struct kvm_sregs SRegs;
    memset(&SRegs, 0, sizeof(SRegs));
    if (   ioctl(g_fdVCpu, KVM_GET_REGS, &Regs) != -1
        && ioctl(g_fdVCpu, KVM_GET_SREGS, &SRegs) != -1)
    {
        RTStrmPrintf(g_pStdErr, "       rip=%016llx\n", Regs.rip);
        printSReg("cs", &SRegs.cs);
        RTStrmPrintf(g_pStdErr, "    rflags=%08llx\n", Regs.rflags);
        RTStrmPrintf(g_pStdErr, "       rax=%016llx\n", Regs.rax);
        RTStrmPrintf(g_pStdErr, "       rbx=%016llx\n", Regs.rcx);
        RTStrmPrintf(g_pStdErr, "       rdx=%016llx\n", Regs.rdx);
        RTStrmPrintf(g_pStdErr, "       rcx=%016llx\n", Regs.rbx);
        RTStrmPrintf(g_pStdErr, "       rsp=%016llx\n", Regs.rsp);
        RTStrmPrintf(g_pStdErr, "       rbp=%016llx\n", Regs.rbp);
        RTStrmPrintf(g_pStdErr, "       rsi=%016llx\n", Regs.rsi);
        RTStrmPrintf(g_pStdErr, "       rdi=%016llx\n", Regs.rdi);
        printSReg("ss", &SRegs.ss);
        printSReg("ds", &SRegs.ds);
        printSReg("es", &SRegs.es);
        printSReg("fs", &SRegs.fs);
        printSReg("gs", &SRegs.gs);
        printSReg("tr", &SRegs.tr);
        printSReg("ldtr", &SRegs.ldt);

        uint64_t const offMem = Regs.rip + SRegs.cs.base - MY_MEM_BASE;
        if (offMem < g_cbMem - 10)
            RTStrmPrintf(g_pStdErr, "  bytes at PC (%#zx): %02x %02x %02x %02x %02x %02x %02x %02x\n", (size_t)(offMem + MY_MEM_BASE),
                         g_pbMem[offMem    ], g_pbMem[offMem + 1], g_pbMem[offMem + 2], g_pbMem[offMem + 3],
                         g_pbMem[offMem + 4], g_pbMem[offMem + 5], g_pbMem[offMem + 6], g_pbMem[offMem + 7]);
    }

    return 1;
}

static int runRealModeTest(unsigned cInstructions, const char *pszInstruction, unsigned fTest,
                           unsigned uEax, unsigned uEcx, unsigned uEdx, unsigned uEbx,
                           unsigned uEsp, unsigned uEbp, unsigned uEsi, unsigned uEdi)
{
    (void)fTest;

    /*
     * Setup real mode context.
     */
#define SET_SEG(a_SReg, a_Base, a_Limit, a_Sel, a_fCode) \
        do { \
            a_SReg.base     = (a_Base); \
            a_SReg.limit    = (a_Limit); \
            a_SReg.selector = (a_Sel); \
            a_SReg.type     = (a_fCode) ? 10 : 3; \
            a_SReg.present  = 1; \
            a_SReg.dpl      = 0; \
            a_SReg.db       = 0; \
            a_SReg.s        = 1; \
            a_SReg.l        = 0; \
            a_SReg.g        = 0; \
            a_SReg.avl      = 0; \
            a_SReg.unusable = 0; \
            a_SReg.padding  = 0; \
        } while (0)
    struct kvm_regs Regs;
    memset(&Regs, 0, sizeof(Regs));
    Regs.rax = uEax;
    Regs.rcx = uEcx;
    Regs.rdx = uEdx;
    Regs.rbx = uEbx;
    Regs.rsp = uEsp;
    Regs.rbp = uEbp;
    Regs.rsi = uEsi;
    Regs.rdi = uEdi;
    Regs.rip = MY_TEST_RIP;
    Regs.rflags = 2;
    int rc = ioctl(g_fdVCpu, KVM_SET_REGS, &Regs);
    if (rc != 0)
        return error("KVM_SET_REGS failed: %d (rc=%d)\n", errno, rc);

    struct kvm_sregs SRegs;
    memset(&SRegs, 0, sizeof(SRegs));
    rc = ioctl(g_fdVCpu, KVM_GET_SREGS, &SRegs);
    if (rc != 0)
        return error("KVM_GET_SREGS failed: %d (rc=%d)\n", errno, rc);
    SET_SEG(SRegs.es, 0x00000, 0xffff, 0x0000, 0);
    SET_SEG(SRegs.cs, 0x00000, 0xffff, 0x0000, 1);
    SET_SEG(SRegs.ss, 0x00000, 0xffff, 0x0000, 0);
    SET_SEG(SRegs.ds, 0x00000, 0xffff, 0x0000, 0);
    SET_SEG(SRegs.fs, 0x00000, 0xffff, 0x0000, 0);
    SET_SEG(SRegs.gs, 0x00000, 0xffff, 0x0000, 0);
    //SRegs.cr0 = 0x10010 /*WP+ET*/;
    SRegs.cr2 = 0;
    //SRegs.cr3 = 0;
    //SRegs.cr4 = 0;
    rc = ioctl(g_fdVCpu, KVM_SET_SREGS, &SRegs);
    if (rc != 0)
        return error("KVM_SET_SREGS failed: %d (rc=%d)\n", errno, rc);

    /*
     * Run the test.
     */
    uint32_t cExits = 0;
    uint64_t const nsStart = getNanoTS();
    for (;;)
    {
        rc = ioctl(g_fdVCpu, KVM_RUN, (uintptr_t)0);
        if (rc == 0)
        {
            cExits++;
            if (g_pVCpuRun->exit_reason == KVM_EXIT_IO)
            {
                if (g_pVCpuRun->io.port == MY_NOP_PORT)
                { /* likely: nop instruction */ }
                else if (g_pVCpuRun->io.port == MY_TERM_PORT)
                    break;
                else
                    return runtimeError("Unexpected I/O port access (for %s): %#x\n", pszInstruction, g_pVCpuRun->io.port);
            }
            else if (g_pVCpuRun->exit_reason == KVM_EXIT_MMIO)
            {
                if (g_pVCpuRun->mmio.phys_addr == MY_NOP_MMIO)
                { /* likely: nop address */ }
                else
                    return runtimeError("Unexpected memory access (for %s): %#llx\n", pszInstruction, g_pVCpuRun->mmio.phys_addr);
            }
            else
                return runtimeError("Unexpected exit (for %s): %d\n", pszInstruction, g_pVCpuRun->exit_reason);
        }
        else
            return runtimeError("KVM_RUN failed (for %s): %#x (ret %d)\n", pszInstruction, errno, rc);
    }
    uint64_t const nsElapsed = getNanoTS() - nsStart;
    return reportResult(pszInstruction, cInstructions, nsElapsed, cExits);
}


#elif defined(RT_OS_DARWIN)

/*
 * Mac OS X - Hypervisor API.
 */

static int createVM(void)
{
    /* VM and VCpu */
    hv_return_t rcHv = hv_vm_create(HV_VM_DEFAULT);
    if (rcHv != HV_SUCCESS)
        return error("hv_vm_create failed: %#x\n", rcHv);

    g_idVCpu = -1;
    rcHv = hv_vcpu_create(&g_idVCpu, HV_VCPU_DEFAULT);
    if (rcHv != HV_SUCCESS)
        return error("hv_vcpu_create failed: %#x\n", rcHv);

    /* Memory. */
    g_pbMem = (unsigned char *)mmap(NULL, g_cbMem, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((void *)g_pbMem == MAP_FAILED)
        return error("mmap RAM failed: %d\n", errno);
    memset(g_pbMem, 0xf4, g_cbMem);

    rcHv = hv_vm_map(g_pbMem, MY_MEM_BASE, g_cbMem, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    if (rcHv != HV_SUCCESS)
        return error("hv_vm_map failed: %#x\n", rcHv);

    rcHv = hv_vm_protect(0x2000, 0x1000, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    if (rcHv != HV_SUCCESS)
        return error("hv_vm_protect failed: %#x\n", rcHv);
    return 0;
}


static int runtimeError(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "runtime error: ");
    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    static struct { const char *pszName; uint32_t uField; uint32_t uFmt : 31; uint32_t fIsReg : 1; } const s_aFields[] =
    {
        { "VMCS_RO_EXIT_REASON",        VMCS_RO_EXIT_REASON,        64, 0 },
        { "VMCS_RO_EXIT_QUALIFIC",      VMCS_RO_EXIT_QUALIFIC,      64, 0 },
        { "VMCS_RO_INSTR_ERROR",        VMCS_RO_INSTR_ERROR,        64, 0 },
        { "VMCS_RO_VMEXIT_IRQ_INFO",    VMCS_RO_VMEXIT_IRQ_INFO,    64, 0 },
        { "VMCS_RO_VMEXIT_IRQ_ERROR",   VMCS_RO_VMEXIT_IRQ_ERROR,   64, 0 },
        { "VMCS_RO_VMEXIT_INSTR_LEN",   VMCS_RO_VMEXIT_INSTR_LEN,   64, 0 },
        { "VMCS_RO_VMX_INSTR_INFO",     VMCS_RO_VMX_INSTR_INFO,     64, 0 },
        { "VMCS_RO_GUEST_LIN_ADDR",     VMCS_RO_GUEST_LIN_ADDR,     64, 0 },
        { "VMCS_GUEST_PHYSICAL_ADDRESS",VMCS_GUEST_PHYSICAL_ADDRESS,64, 0 },
        { "VMCS_RO_IO_RCX",             VMCS_RO_IO_RCX,             64, 0 },
        { "VMCS_RO_IO_RSI",             VMCS_RO_IO_RSI,             64, 0 },
        { "VMCS_RO_IO_RDI",             VMCS_RO_IO_RDI,             64, 0 },
        { "VMCS_RO_IO_RIP",             VMCS_RO_IO_RIP,             64, 0 },
        { "rip",                        HV_X86_RIP,                 64, 1 },
        { "rip (vmcs)",                 VMCS_GUEST_RIP,             64, 0 },
        { "cs",                         HV_X86_CS,                  16, 1 },
        { "cs (vmcs)",                  VMCS_GUEST_CS,              16, 0 },
        { "cs.base",                    VMCS_GUEST_CS_BASE,         64, 0 },
        { "cs.limit",                   VMCS_GUEST_CS_LIMIT,        32, 0 },
        { "cs.attr",                    VMCS_GUEST_CS_AR,           32, 0 },
        { "rflags",                     HV_X86_RFLAGS,              32, 1 },
        { "rax",                        HV_X86_RAX,                 64, 1 },
        { "rcx",                        HV_X86_RCX,                 64, 1 },
        { "rdx",                        HV_X86_RDX,                 64, 1 },
        { "rbx",                        HV_X86_RBX,                 64, 1 },
        { "rsp",                        HV_X86_RSP,                 64, 1 },
        { "rsp (vmcs)",                 VMCS_GUEST_RSP,             64, 0 },
        { "ss",                         HV_X86_SS,                  16, 1 },
        { "ss (vmcs)",                  VMCS_GUEST_SS,              16, 0 },
        { "ss.base",                    VMCS_GUEST_SS_BASE,         64, 0 },
        { "ss.limit",                   VMCS_GUEST_SS_LIMIT,        32, 0 },
        { "ss.attr",                    VMCS_GUEST_SS_AR,           32, 0 },
        { "rbp",                        HV_X86_RBP,                 64, 1 },
        { "rsi",                        HV_X86_RSI,                 64, 1 },
        { "rdi",                        HV_X86_RDI,                 64, 1 },
        { "ds",                         HV_X86_DS,                  16, 1 },
        { "ds (vmcs)",                  VMCS_GUEST_DS,              16, 0 },
        { "ds.base",                    VMCS_GUEST_DS_BASE,         64, 0 },
        { "ds.limit",                   VMCS_GUEST_DS_LIMIT,        32, 0 },
        { "ds.attr",                    VMCS_GUEST_DS_AR,           32, 0 },
        { "es",                         HV_X86_ES,                  16, 1 },
        { "es (vmcs)",                  VMCS_GUEST_ES,              16, 0 },
        { "es.base",                    VMCS_GUEST_ES_BASE,         64, 0 },
        { "es.limit",                   VMCS_GUEST_ES_LIMIT,        32, 0 },
        { "es.attr",                    VMCS_GUEST_ES_AR,           32, 0 },
        { "fs",                         HV_X86_FS,                  16, 1 },
        { "fs (vmcs)",                  VMCS_GUEST_FS,              16, 0 },
        { "fs.base",                    VMCS_GUEST_FS_BASE,         64, 0 },
        { "fs.limit",                   VMCS_GUEST_FS_LIMIT,        32, 0 },
        { "fs.attr",                    VMCS_GUEST_FS_AR,           32, 0 },
        { "gs",                         HV_X86_GS,                  16, 1 },
        { "gs (vmcs)",                  VMCS_GUEST_GS,              16, 0 },
        { "gs.base",                    VMCS_GUEST_GS_BASE,         64, 0 },
        { "gs.limit",                   VMCS_GUEST_GS_LIMIT,        32, 0 },
        { "gs.attr",                    VMCS_GUEST_GS_AR,           32, 0 },
        { "cr0",                        HV_X86_CR0,                 64, 1 },
        { "cr0 (vmcs)",                 VMCS_GUEST_CR0,             64, 0 },
        { "cr2",                        HV_X86_CR2,                 64, 1 },
        { "cr3",                        HV_X86_CR3,                 64, 1 },
        { "cr3 (vmcs)",                 VMCS_GUEST_CR3,             64, 0 },
        { "cr4",                        HV_X86_CR4,                 64, 1 },
        { "cr4 (vmcs)",                 VMCS_GUEST_CR4,             64, 0 },
        { "idtr.base",                  VMCS_GUEST_IDTR_BASE,       64, 0 },
        { "idtr.limit",                 VMCS_GUEST_IDTR_LIMIT,      32, 0 },
        { "gdtr.base",                  VMCS_GUEST_GDTR_BASE,       64, 0 },
        { "gdtr.limit",                 VMCS_GUEST_GDTR_LIMIT,      32, 0 },

        { "VMCS_CTRL_PIN_BASED",        VMCS_CTRL_PIN_BASED,        64, 0 },
        { "VMCS_CTRL_CPU_BASED",        VMCS_CTRL_CPU_BASED,        64, 0 },
        { "VMCS_CTRL_CPU_BASED2",       VMCS_CTRL_CPU_BASED2,       64, 0 },
        { "VMCS_CTRL_VMENTRY_CONTROLS", VMCS_CTRL_VMENTRY_CONTROLS, 64, 0 },
        { "VMCS_CTRL_VMEXIT_CONTROLS",  VMCS_CTRL_VMEXIT_CONTROLS,  64, 0 },
        { "VMCS_CTRL_EXC_BITMAP",       VMCS_CTRL_EXC_BITMAP,       64, 0 },
        { "VMCS_CTRL_CR0_MASK",         VMCS_CTRL_CR0_MASK,         64, 0 },
        { "VMCS_CTRL_CR0_SHADOW",       VMCS_CTRL_CR0_SHADOW,       64, 0 },
        { "VMCS_CTRL_CR4_MASK",         VMCS_CTRL_CR4_MASK,         64, 0 },
        { "VMCS_CTRL_CR4_SHADOW",       VMCS_CTRL_CR4_SHADOW,       64, 0 },
    };
    for (unsigned i = 0; i < sizeof(s_aFields) / sizeof(s_aFields[0]); i++)
    {
        uint64_t uValue = UINT64_MAX;
        hv_return_t rcHv;
        if (s_aFields[i].fIsReg)
            rcHv = hv_vcpu_read_register(g_idVCpu, (hv_x86_reg_t)s_aFields[i].uField, &uValue);
        else
            rcHv = hv_vmx_vcpu_read_vmcs(g_idVCpu, s_aFields[i].uField, &uValue);
        if (rcHv == HV_SUCCESS)
        {
            if (s_aFields[i].uFmt == 16)
                RTStrmPrintf(g_pStdErr, "%28s=%04llx\n", s_aFields[i].pszName, uValue);
            else if (s_aFields[i].uFmt == 32)
                RTStrmPrintf(g_pStdErr, "%28s=%08llx\n", s_aFields[i].pszName, uValue);
            else
                RTStrmPrintf(g_pStdErr, "%28s=%08x'%08x\n", s_aFields[i].pszName, (uint32_t)(uValue >> 32), (uint32_t)uValue);
        }
        else
            RTStrmPrintf(g_pStdErr, "%28s=<%s failed %#x>\n", s_aFields[i].pszName,
                         s_aFields[i].fIsReg ? "hv_vcpu_read_register" : "hv_vmx_vcpu_read_vmcs", rcHv);
    }
    return 1;
}


static int runRealModeTest(unsigned cInstructions, const char *pszInstruction, unsigned fTest,
                           unsigned uEax, unsigned uEcx, unsigned uEdx, unsigned uEbx,
                           unsigned uEsp, unsigned uEbp, unsigned uEsi, unsigned uEdi)
{
    /*
     * Setup real mode context.
     */
#define WRITE_REG_RET(a_enmReg, a_uValue) \
        do { \
            hv_return_t rcHvX = hv_vcpu_write_register(g_idVCpu, a_enmReg, a_uValue); \
            if (rcHvX == HV_SUCCESS) { /* likely */ } \
            else return error("hv_vcpu_write_register(%#x, %s, %#llx) -> %#x\n", g_idVCpu, #a_enmReg, (uint64_t)(a_uValue), rcHvX); \
        } while (0)
#define READ_REG_RET(a_enmReg, a_puValue) \
        do { \
            hv_return_t rcHvX = hv_vcpu_read_register(g_idVCpu, a_enmReg, a_puValue); \
            if (rcHvX == HV_SUCCESS) { /* likely */ } \
            else return error("hv_vcpu_read_register(%#x, %s,) -> %#x\n", g_idVCpu, #a_enmReg, rcHvX); \
        } while (0)
#define WRITE_VMCS_RET(a_enmField, a_uValue) \
        do { \
            hv_return_t rcHvX = hv_vmx_vcpu_write_vmcs(g_idVCpu, a_enmField, a_uValue); \
            if (rcHvX == HV_SUCCESS) { /* likely */ } \
            else return error("hv_vmx_vcpu_write_vmcs(%#x, %s, %#llx) -> %#x\n", g_idVCpu, #a_enmField, (uint64_t)(a_uValue), rcHvX); \
        } while (0)
#define READ_VMCS_RET(a_enmField, a_puValue) \
        do { \
            hv_return_t rcHvX = hv_vmx_vcpu_read_vmcs(g_idVCpu, a_enmField, a_puValue); \
            if (rcHvX == HV_SUCCESS) { /* likely */ } \
            else return error("hv_vmx_vcpu_read_vmcs(%#x, %s,) -> %#x\n", g_idVCpu, #a_enmField, rcHvX); \
        } while (0)
#define READ_CAP_RET(a_enmCap, a_puValue) \
        do { \
            hv_return_t rcHvX = hv_vmx_read_capability(a_enmCap, a_puValue); \
            if (rcHvX == HV_SUCCESS) { /* likely */ } \
            else return error("hv_vmx_read_capability(%s) -> %#x\n", #a_enmCap); \
        } while (0)
#define CAP_2_CTRL(a_uCap, a_fWanted) ( ((a_fWanted) | (uint32_t)(a_uCap)) & (uint32_t)((a_uCap) >> 32) )
#if 1
    uint64_t uCap;
    READ_CAP_RET(HV_VMX_CAP_PINBASED, &uCap);
    WRITE_VMCS_RET(VMCS_CTRL_PIN_BASED, CAP_2_CTRL(uCap, PIN_BASED_INTR | PIN_BASED_NMI | PIN_BASED_VIRTUAL_NMI));
    READ_CAP_RET(HV_VMX_CAP_PROCBASED, &uCap);
    WRITE_VMCS_RET(VMCS_CTRL_CPU_BASED, CAP_2_CTRL(uCap, CPU_BASED_HLT
                                                       | CPU_BASED_INVLPG
                                                       | CPU_BASED_MWAIT
                                                       | CPU_BASED_RDPMC
                                                       | CPU_BASED_RDTSC
                                                       | CPU_BASED_CR3_LOAD
                                                       | CPU_BASED_CR3_STORE
                                                       | CPU_BASED_CR8_LOAD
                                                       | CPU_BASED_CR8_STORE
                                                       | CPU_BASED_MOV_DR
                                                       | CPU_BASED_UNCOND_IO
                                                       | CPU_BASED_MONITOR
                                                       | CPU_BASED_PAUSE
                                                       ));
    READ_CAP_RET(HV_VMX_CAP_PROCBASED2, &uCap);
    WRITE_VMCS_RET(VMCS_CTRL_CPU_BASED2, CAP_2_CTRL(uCap, 0));
    READ_CAP_RET(HV_VMX_CAP_ENTRY, &uCap);
    WRITE_VMCS_RET(VMCS_CTRL_VMENTRY_CONTROLS, CAP_2_CTRL(uCap, 0));
#endif
    WRITE_VMCS_RET(VMCS_CTRL_EXC_BITMAP, UINT32_MAX);
    WRITE_VMCS_RET(VMCS_CTRL_CR0_MASK,   0x60000000);
    WRITE_VMCS_RET(VMCS_CTRL_CR0_SHADOW, 0x00000000);
    WRITE_VMCS_RET(VMCS_CTRL_CR4_MASK,   0x00000000);
    WRITE_VMCS_RET(VMCS_CTRL_CR4_SHADOW, 0x00000000);

    WRITE_REG_RET(HV_X86_RAX, uEax);
    WRITE_REG_RET(HV_X86_RCX, uEcx);
    WRITE_REG_RET(HV_X86_RDX, uEdx);
    WRITE_REG_RET(HV_X86_RBX, uEbx);
    WRITE_REG_RET(HV_X86_RSP, uEsp);
    WRITE_REG_RET(HV_X86_RBP, uEbp);
    WRITE_REG_RET(HV_X86_RSI, uEsi);
    WRITE_REG_RET(HV_X86_RDI, uEdi);
    WRITE_REG_RET(HV_X86_RIP, MY_TEST_RIP);
    WRITE_REG_RET(HV_X86_RFLAGS, 2);
    WRITE_REG_RET(HV_X86_ES, 0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_ES_BASE, 0x0000000);
    WRITE_VMCS_RET(VMCS_GUEST_ES_LIMIT,   0xffff);
    WRITE_VMCS_RET(VMCS_GUEST_ES_AR,        0x93);
    WRITE_REG_RET(HV_X86_CS, 0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_CS_BASE, 0x0000000);
    WRITE_VMCS_RET(VMCS_GUEST_CS_LIMIT,   0xffff);
    WRITE_VMCS_RET(VMCS_GUEST_CS_AR,        0x9b);
    WRITE_REG_RET(HV_X86_SS, 0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_SS_BASE, 0x0000000);
    WRITE_VMCS_RET(VMCS_GUEST_SS_LIMIT,   0xffff);
    WRITE_VMCS_RET(VMCS_GUEST_SS_AR,        0x93);
    WRITE_REG_RET(HV_X86_DS, 0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_DS_BASE, 0x0000000);
    WRITE_VMCS_RET(VMCS_GUEST_DS_LIMIT,  0xffff);
    WRITE_VMCS_RET(VMCS_GUEST_DS_AR,        0x93);
    WRITE_REG_RET(HV_X86_FS, 0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_FS_BASE, 0x0000000);
    WRITE_VMCS_RET(VMCS_GUEST_FS_LIMIT,   0xffff);
    WRITE_VMCS_RET(VMCS_GUEST_FS_AR,        0x93);
    WRITE_REG_RET(HV_X86_GS, 0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_GS_BASE, 0x0000000);
    WRITE_VMCS_RET(VMCS_GUEST_GS_LIMIT,   0xffff);
    WRITE_VMCS_RET(VMCS_GUEST_GS_AR,        0x93);
    //WRITE_REG_RET(HV_X86_CR0, 0x10030 /*WP+NE+ET*/);
    WRITE_VMCS_RET(VMCS_GUEST_CR0,        0x10030 /*WP+NE+ET*/);
    //WRITE_REG_RET(HV_X86_CR2, 0);
    //WRITE_REG_RET(HV_X86_CR3, 0);
    WRITE_VMCS_RET(VMCS_GUEST_CR3,             0);
    //WRITE_REG_RET(HV_X86_CR4, 0x2000);
    WRITE_VMCS_RET(VMCS_GUEST_CR4,        0x2000);
    WRITE_VMCS_RET(VMCS_GUEST_LDTR,          0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_LDTR_BASE, 0x00000000);
    WRITE_VMCS_RET(VMCS_GUEST_LDTR_LIMIT,    0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_LDTR_AR,      0x10000);
    WRITE_VMCS_RET(VMCS_GUEST_TR,            0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_TR_BASE,   0x00000000);
    WRITE_VMCS_RET(VMCS_GUEST_TR_LIMIT,      0x0000);
    WRITE_VMCS_RET(VMCS_GUEST_TR_AR,        0x00083);
    hv_vcpu_flush(g_idVCpu);
    hv_vcpu_invalidate_tlb(g_idVCpu);

    /*
     * Run the test.
     */
    uint32_t cExits = 0;
    uint64_t const nsStart = getNanoTS();
    for (;;)
    {
        hv_return_t rcHv = hv_vcpu_run(g_idVCpu);
        if (rcHv == HV_SUCCESS)
        {
            cExits++;
            uint64_t uExitReason = UINT64_MAX;
            READ_VMCS_RET(VMCS_RO_EXIT_REASON, &uExitReason);
            if (!(uExitReason & UINT64_C(0x80000000)))
            {
                if (uExitReason == VMX_REASON_IO)
                {
                    uint64_t uIoQual = UINT64_MAX;
                    READ_VMCS_RET(VMCS_RO_EXIT_QUALIFIC, &uIoQual);
                    if ((uint16_t)(uIoQual >> 16) == MY_NOP_PORT && (fTest & MY_TEST_F_NOP_IO))
                    { /* likely: nop instruction */ }
                    else if ((uint16_t)(uIoQual >> 16) == MY_TERM_PORT)
                        break;
                    else
                        return runtimeError("Unexpected I/O port access (for %s): %#x\n", pszInstruction, (uint16_t)(uIoQual >> 16));

                    /* Advance RIP. */
                    uint64_t cbInstr = UINT64_MAX;
                    READ_VMCS_RET(VMCS_RO_VMEXIT_INSTR_LEN, &cbInstr);
                    if (cbInstr < 1 || cbInstr > 15)
                        return runtimeError("Bad instr len: %#llx\n", cbInstr);
                    uint64_t uRip = UINT64_MAX;
                    READ_REG_RET(HV_X86_RIP, &uRip);
                    WRITE_REG_RET(HV_X86_RIP, uRip + cbInstr);
                }
                else if (uExitReason == VMX_REASON_CPUID && (fTest & MY_TEST_F_CPUID))
                {
                    /* Set registers and advance RIP. */
                    WRITE_REG_RET(HV_X86_RAX, 0x42424242);
                    WRITE_REG_RET(HV_X86_RCX, 0x04242424);
                    WRITE_REG_RET(HV_X86_RDX, 0x00424242);
                    WRITE_REG_RET(HV_X86_RBX, 0x00024242);

                    uint64_t cbInstr = UINT64_MAX;
                    READ_VMCS_RET(VMCS_RO_VMEXIT_INSTR_LEN, &cbInstr);
                    if (cbInstr < 1 || cbInstr > 15)
                        return runtimeError("Bad instr len: %#llx\n", cbInstr);
                    uint64_t uRip = UINT64_MAX;
                    READ_REG_RET(HV_X86_RIP, &uRip);
                    WRITE_REG_RET(HV_X86_RIP, uRip + cbInstr);
                }
                else if (uExitReason == VMX_REASON_EPT_VIOLATION)
                {
                    uint64_t uEptQual = UINT64_MAX;
                    READ_VMCS_RET(VMCS_RO_EXIT_QUALIFIC, &uEptQual);
                    uint64_t GCPhys = UINT64_MAX;
                    READ_VMCS_RET(VMCS_GUEST_PHYSICAL_ADDRESS, &GCPhys);
                    if (GCPhys == MY_NOP_MMIO && (fTest & MY_TEST_F_NOP_MMIO))
                    { /* likely */ }
                    else if (GCPhys == MY_TEST_RIP)
                        continue; /* dunno why we get this, but restarting it works */
                    else
                        return runtimeError("Unexpected EPT viotaion at %#llx\n", GCPhys);

                    /* Set RAX and advance RIP. */
                    WRITE_REG_RET(HV_X86_RAX, 42);

                    uint64_t cbInstr = UINT64_MAX;
                    READ_VMCS_RET(VMCS_RO_VMEXIT_INSTR_LEN, &cbInstr);
                    if (cbInstr < 1 || cbInstr > 15)
                        return runtimeError("Bad instr len: %#llx\n", cbInstr);
                    uint64_t uRip = UINT64_MAX;
                    READ_REG_RET(HV_X86_RIP, &uRip);
                    WRITE_REG_RET(HV_X86_RIP, uRip + cbInstr);
                }
                else if (uExitReason == VMX_REASON_IRQ)
                { /* ignore */ }
                else
                    return runtimeError("Unexpected exit reason: %#x\n", uExitReason);
            }
            else
                return runtimeError("VM entry failure: %#x\n", uExitReason);
        }
        else
            return runtimeError("hv_vcpu_run failed (for %s): %#x\n", pszInstruction, rcHv);
    }
    uint64_t const nsElapsed = getNanoTS() - nsStart;
    return reportResult(pszInstruction, cInstructions, nsElapsed, cExits);
}

#else
# error "port me"
#endif

void dumpCode(uint8_t const *pb, uint8_t *pbEnd)
{
    RTPrintf("testing:");
    for (; pb != pbEnd; pb++)
        RTPrintf(" %02x", *pb);
    RTPrintf("\n");
}


int ioportTest(unsigned cFactor)
{
    /*
     * Produce realmode code
     */
    unsigned char *pb = &g_pbMem[MY_TEST_RIP - MY_MEM_BASE];
    unsigned char * const pbStart = pb;
    /* OUT DX, AL - 10 times */
    for (unsigned i = 0; i < 10; i++)
        *pb++ = 0xee;
    /* DEC ECX */
    *pb++ = 0x66;
    *pb++ = 0x48 + 1;
    /* JNZ MY_TEST_RIP */
    *pb++ = 0x75;
    *pb   = (signed char)(pbStart - pb - 1);
    pb++;
    /* OUT 1, AL -  Temination port call. */
    *pb++ = 0xe6;
    *pb++ = MY_TERM_PORT;
    /* JMP to previous instruction */
    *pb++ = 0xeb;
    *pb++ = 0xfc;
    dumpCode(pbStart, pb);

    return runRealModeTest(100000 * cFactor, "OUT", MY_TEST_F_NOP_IO,
                           42 /*eax*/, 10000 * cFactor /*ecx*/, MY_NOP_PORT /*edx*/, 0 /*ebx*/,
                           0 /*esp*/, 0 /*ebp*/, 0 /*esi*/, 0 /*uEdi*/);
}


int cpuidTest(unsigned cFactor)
{
    /*
     * Produce realmode code
     */
    unsigned char *pb = &g_pbMem[MY_TEST_RIP - MY_MEM_BASE];
    unsigned char * const pbStart = pb;
    for (unsigned i = 0; i < 10; i++)
    {
        /* XOR EAX,EAX */
        *pb++ = 0x66;
        *pb++ = 0x33;
        *pb++ = 0xc0;

        /* CPUID */
        *pb++ = 0x0f;
        *pb++ = 0xa2;
    }
    /* DEC ESI */
    *pb++ = 0x66;
    *pb++ = 0x48 + 6;
    /* JNZ MY_TEST_RIP */
    *pb++ = 0x75;
    *pb   = (signed char)(pbStart - pb - 1);
    pb++;
    /* OUT 1, AL -  Temination port call. */
    *pb++ = 0xe6;
    *pb++ = MY_TERM_PORT;
    /* JMP to previous instruction */
    *pb++ = 0xeb;
    *pb++ = 0xfc;
    dumpCode(pbStart, pb);

    return runRealModeTest(100000 * cFactor, "CPUID", MY_TEST_F_CPUID,
                           0 /*eax*/, 0 /*ecx*/, 0 /*edx*/, 0 /*ebx*/,
                           0 /*esp*/, 0 /*ebp*/, 10000 * cFactor /*esi*/, 0 /*uEdi*/);
}


int mmioTest(unsigned cFactor)
{
    /*
     * Produce realmode code accessing MY_MMIO_NOP address assuming it's low.
     */
    unsigned char *pb = &g_pbMem[MY_TEST_RIP - MY_MEM_BASE];
    unsigned char * const pbStart = pb;
    for (unsigned i = 0; i < 10; i++)
    {
        /* MOV AL,DS:[BX] */
        *pb++ = 0x8a;
        *pb++ = 0x07;
    }
    /* DEC ESI */
    *pb++ = 0x66;
    *pb++ = 0x48 + 6;
    /* JNZ MY_TEST_RIP */
    *pb++ = 0x75;
    *pb   = (signed char)(pbStart - pb - 1);
    pb++;
    /* OUT 1, AL -  Temination port call. */
    *pb++ = 0xe6;
    *pb++ = MY_TERM_PORT;
    /* JMP to previous instruction */
    *pb++ = 0xeb;
    *pb++ = 0xfc;
    dumpCode(pbStart, pb);

    return runRealModeTest(100000 * cFactor, "MMIO/r1", MY_TEST_F_NOP_MMIO,
                           0 /*eax*/, 0 /*ecx*/, 0 /*edx*/, MY_NOP_MMIO /*ebx*/,
                           0 /*esp*/, 0 /*ebp*/, 10000 * cFactor /*esi*/, 0 /*uEdi*/);
}



int main(int argc, char **argv)
{
    /*
     * Do some parameter parsing.
     */
#ifdef RT_OS_WINDOWS
    unsigned const  cFactorDefault = 4;
#elif RT_OS_DARWIN
    unsigned const  cFactorDefault = 32;
#else
    unsigned const  cFactorDefault = 24;
#endif
    unsigned        cFactor = cFactorDefault;
    for (int i = 1; i < argc; i++)
    {
        const char *pszArg = argv[i];
        if (   strcmp(pszArg, "--help") == 0
            || strcmp(pszArg, "/help") == 0
            || strcmp(pszArg, "-h") == 0
            || strcmp(pszArg, "-?") == 0
            || strcmp(pszArg, "/?") == 0)
        {
            RTPrintf("Does some benchmarking of the native NEM engine.\n"
                     "\n"
                     "Usage: NemRawBench-1 --factor <factor>\n"
                     "\n"
                     "Options\n"
                     "  --factor <factor>\n"
                     "        Iteration count factor.  Default is %u.\n"
                     "        Lower it if execution is slow, increase if quick.\n",
                     cFactorDefault);
            return 0;
        }
        if (strcmp(pszArg, "--factor") == 0)
        {
            i++;
            if (i < argc)
                cFactor = RTStrToUInt32(argv[i]);
            else
            {
                RTStrmPrintf(g_pStdErr, "syntax error: Option %s is takes a value!\n", pszArg);
                return 2;
            }
        }
        else
        {
            RTStrmPrintf(g_pStdErr, "syntax error: Unknown option: %s\n", pszArg);
            return 2;
        }
    }

    /*
     * Create the VM
     */
    g_cbMem = 128*1024 - MY_MEM_BASE;
    int rcExit = createVM();
    if (rcExit == 0)
    {
        RTPrintf("tstNemBench-1: Successfully created test VM...\n");

        /*
         * Do the benchmarking.
         */
        ioportTest(cFactor);
        cpuidTest(cFactor);
        mmioTest(cFactor);

        RTPrintf("tstNemBench-1: done\n");
    }
    return rcExit;
}

/*
 * Results:
 *
 * - Darwin/xnu 10.12.6/16.7.0; 3.1GHz Intel Core i7-7920HQ (Kaby Lake):
 *    925 845     OUT instructions per second (3 200 307 exits in 3 456 301 621 ns)
 *    949 278   CPUID instructions per second (3 200 222 exits in 3 370 980 173 ns)
 *    871 499 MMIO/r1 instructions per second (3 200 223 exits in 3 671 834 221 ns)
 *
 * - Linux 4.15.0 / ubuntu 18.04.1 Desktop LiveCD; 3.1GHz Intel Core i7-7920HQ (Kaby Lake):
 *    829 775     OUT instructions per second (3 200 001 exits in 3 856 466 567 ns)
 *  2 212 038   CPUID instructions per second (1 exits in 1 446 629 591 ns)             [1]
 *    477 962 MMIO/r1 instructions per second (3 200 001 exits in 6 695 090 600 ns)
 *
 * - Linux 4.15.0 / ubuntu 18.04.1 Desktop LiveCD; 3.4GHz Core i5-3570 (Ivy Bridge):
 *    717 216     OUT instructions per second (2 400 001 exits in 3 346 271 640 ns)
 *  1 675 983   CPUID instructions per second (1 exits in 1 431 995 135 ns)             [1]
 *    402 621 MMIO/r1 instructions per second (2 400 001 exits in 5 960 930 854 ns)
 *
 * - Linux 4.18.0-1-amd64 (debian); 3.4GHz AMD Threadripper 1950X:
 *    455 727     OUT instructions per second (2 400 001 exits in 5 266 300 471 ns)
 *  1 745 014   CPUID instructions per second (1 exits in 1 375 346 658 ns)             [1]
 *    351 767 MMIO/r1 instructions per second (2 400 001 exits in 6 822 684 544 ns)
 *
 * - Windows 1803 updated as per 2018-10-01; 3.4GHz Core i5-3570 (Ivy Bridge):
 *     67 778     OUT instructions per second (400 001 exits in 5 901 560 700 ns)
 *     66 113   CPUID instructions per second (400 001 exits in 6 050 208 000 ns)
 *     62 939 MMIO/r1 instructions per second (400 001 exits in 6 355 302 900 ns)
 *
 * - Windows 1803 updated as per 2018-09-28; 3.4GHz AMD Threadripper 1950X:
 *     34 485     OUT instructions per second (400 001 exits in 11 598 918 200 ns)
 *     34 043   CPUID instructions per second (400 001 exits in 11 749 753 200 ns)
 *     33 124 MMIO/r1 instructions per second (400 001 exits in 12 075 617 000 ns)
 *
 * - Windows build 17763; 3.4GHz AMD Threadripper 1950X:
 *     65 633     OUT instructions per second (400 001 exits in 6 094 409 100 ns)
 *     65 245   CPUID instructions per second (400 001 exits in 6 130 720 600 ns)
 *     61 642 MMIO/r1 instructions per second (400 001 exits in 6 489 013 700 ns)
 *
 *
 * [1] CPUID causes no return to ring-3 with KVM.
 *
 *
 * For reference we can compare with similar tests in bs2-test1 running VirtualBox:
 *
 * - Linux 4.18.0-1-amd64 (debian); 3.4GHz AMD Threadripper 1950X; trunk/r125404:
 *      real mode, 32-bit OUT            :        1 338 471 ins/sec
 *      real mode, 32-bit OUT-to-ring-3  :          500 337 ins/sec
 *      real mode, CPUID                 :        1 566 343 ins/sec
 *      real mode, 32-bit write          :          870 671 ins/sec
 *      real mode, 32-bit write-to-ring-3:          391 014 ins/sec
 *
 * - Darwin/xnu 10.12.6/16.7.0; 3.1GHz Intel Core i7-7920HQ (Kaby Lake); trunk/r125404:
 *      real mode, 32-bit OUT            :          790 117 ins/sec
 *      real mode, 32-bit OUT-to-ring-3  :          157 205 ins/sec
 *      real mode, CPUID                 :        1 001 087 ins/sec
 *      real mode, 32-bit write          :          651 257 ins/sec
 *      real mode, 32-bit write-to-ring-3:          157 773 ins/sec
 *
 * - Linux 4.15.0 / ubuntu 18.04.1 Desktop LiveCD; 3.1GHz Intel Core i7-7920HQ (Kaby Lake); trunk/r125450:
 *      real mode, 32-bit OUT            :        1 229 245 ins/sec
 *      real mode, 32-bit OUT-to-ring-3  :          284 848 ins/sec
 *      real mode, CPUID                 :        1 429 760 ins/sec
 *      real mode, 32-bit write          :          820 679 ins/sec
 *      real mode, 32-bit write-to-ring-3:          245 159 ins/sec
 *
 * - Windows 1803 updated as per 2018-10-01; 3.4GHz Core i5-3570 (Ivy Bridge); trunk/r15442:
 *      real mode, 32-bit OUT            :          961 939 ins/sec
 *      real mode, 32-bit OUT-to-ring-3  :          189 458 ins/sec
 *      real mode, CPUID                 :        1 060 582 ins/sec
 *      real mode, 32-bit write          :          637 967 ins/sec
 *      real mode, 32-bit write-to-ring-3:          148 573 ins/sec
 *
 */
