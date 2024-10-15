/* $Id: tstDBGCStubs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC Testcase - Command Parser, VMM Stub Functions.
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

#include <VBox/err.h>
#include <VBox/vmm/vmapi.h>
#include <iprt/string.h>



#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgfflowtrace.h>
VMMR3DECL(PDBGFADDRESS) DBGFR3AddrFromFlat(PUVM pUVM, PDBGFADDRESS pAddress, RTGCUINTPTR FlatPtr)
{
    return NULL;
}

VMMR3DECL(int) DBGFR3AddrFromSelOff(PUVM pUVM, VMCPUID idCpu, PDBGFADDRESS pAddress, RTSEL Sel, RTUINTPTR off)
{
    /* bad:bad -> provke error during parsing. */
    if (Sel == 0xbad && off == 0xbad)
        return VERR_OUT_OF_SELECTOR_BOUNDS;

    /* real mode conversion. */
    pAddress->FlatPtr = (uint32_t)(Sel << 4) | off;
    pAddress->fFlags |= DBGFADDRESS_FLAGS_FLAT;
    pAddress->Sel     = DBGF_SEL_FLAT;
    pAddress->off     = pAddress->FlatPtr;
    return VINF_SUCCESS;
}

VMMR3DECL(int)  DBGFR3AddrToPhys(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, PRTGCPHYS pGCPhys)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(int) DBGFR3Attach(PUVM pUVM)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(int) DBGFR3BpClear(PUVM pUVM, RTUINT iBp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3BpDisable(PUVM pUVM, RTUINT iBp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3BpEnable(PUVM pUVM, RTUINT iBp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3BpEnum(PUVM pUVM, PFNDBGFBPENUM pfnCallback, void *pvUser)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3BpSetInt3(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, uint64_t iHitTrigger, uint64_t iHitDisable, PRTUINT piBp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3BpSetReg(PUVM pUVM, PCDBGFADDRESS pAddress, uint64_t iHitTrigger, uint64_t iHitDisable,
                              uint8_t fType, uint8_t cb, PRTUINT piBp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3BpSetREM(PUVM pUVM, PCDBGFADDRESS pAddress, uint64_t iHitTrigger, uint64_t iHitDisable, PRTUINT piBp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3QueryWaitable(PUVM pUVM)
{
    return VINF_SUCCESS;
}
VMMR3DECL(int) DBGFR3Detach(PUVM pUVM)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3DisasInstrEx(PUVM pUVM, VMCPUID idCpu, RTSEL Sel, RTGCPTR GCPtr, uint32_t fFlags,
                                  char *pszOutput, uint32_t cchOutput, uint32_t *pcbInstr)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3EventWait(PUVM pUVM, RTMSINTERVAL cMillies, PDBGFEVENT pEvent)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3EventConfigEx(PUVM pUVM, PCDBGFEVENTCONFIG paConfigs, size_t cConfigs)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3InterruptConfigEx(PUVM pUVM, PCDBGFINTERRUPTCONFIG paConfigs, size_t cConfigs)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(int) DBGFR3Halt(PUVM pUVM, VMCPUID idCpu)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3Info(PUVM pUVM, const char *pszName, const char *pszArgs, PCDBGFINFOHLP pHlp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3InfoEx(PUVM pUVM, VMCPUID idCpu, const char *pszName, const char *pszArgs, PCDBGFINFOHLP pHlp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(void) DBGFR3InfoGenericGetOptError(PCDBGFINFOHLP pHlp, int rc, union RTGETOPTUNION *pValueUnion, struct RTGETOPTSTATE *pState)
{
}
VMMR3DECL(bool) DBGFR3IsHalted(PUVM pUVM, VMCPUID idCpu)
{
    return true;
}
VMMR3DECL(int) DBGFR3LogModifyDestinations(PUVM pUVM, const char *pszDestSettings)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3LogModifyFlags(PUVM pUVM, const char *pszFlagSettings)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3LogModifyGroups(PUVM pUVM, const char *pszGroupSettings)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(RTDBGCFG) DBGFR3AsGetConfig(PUVM pUVM)
{
    return NIL_RTDBGCFG;
}
VMMR3DECL(int) DBGFR3AsLoadImage(PUVM pUVM, RTDBGAS hAS, const char *pszFilename, const char *pszModName, RTLDRARCH enmArch,
                                 PCDBGFADDRESS pModAddress, RTDBGSEGIDX iModSeg, uint32_t fFlags)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3AsLoadMap(PUVM pUVM, RTDBGAS hAS, const char *pszFilename, const char *pszModName, PCDBGFADDRESS pModAddress, RTDBGSEGIDX iModSeg, RTGCUINTPTR uSubtrahend, uint32_t fFlags)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3AsUnlinkModuleByName(PUVM pUVM, RTDBGAS hDbgAs, const char *pszModName)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(RTDBGAS) DBGFR3AsResolveAndRetain(PUVM pUVM, RTDBGAS hAlias)
{
    return NIL_RTDBGAS;
}
VMMR3DECL(int) DBGFR3AsLineByAddr(PUVM pUVM, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress,
                                  PRTGCINTPTR poffDisp, PRTDBGLINE pLine, PRTDBGMOD phMod)
{
    return VERR_DBG_LINE_NOT_FOUND;
}
VMMR3DECL(int) DBGFR3Resume(PUVM pUVM, VMCPUID idCpu)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3StackWalkBegin(PUVM pUVM, VMCPUID idCpu, DBGFCODETYPE enmCodeType, PCDBGFSTACKFRAME *ppFirstFrame)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(PCDBGFSTACKFRAME) DBGFR3StackWalkNext(PCDBGFSTACKFRAME pCurrent)
{
    return NULL;
}
VMMR3DECL(void) DBGFR3StackWalkEnd(PCDBGFSTACKFRAME pFirstFrame)
{
}
VMMR3DECL(int) DBGFR3StepEx(PUVM pUVM, VMCPUID idCpu, uint32_t fFlags, PCDBGFADDRESS pStopPcAddr,
                            PCDBGFADDRESS pStopPopAddr, RTGCUINTPTR cbStopPop, uint32_t cMaxSteps)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3AsSymbolByAddr(PUVM pUVM, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress, uint32_t fFlags, PRTGCINTPTR poffDisplacement, PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(PRTDBGSYMBOL) DBGFR3AsSymbolByAddrA(PUVM pUVM, RTDBGAS hDbgAs, PCDBGFADDRESS pAddress, uint32_t fFlags,
                                              PRTGCINTPTR poffDisp, PRTDBGMOD phMod)
{
    return NULL;
}
VMMR3DECL(int) DBGFR3AsSymbolByName(PUVM pUVM, RTDBGAS hDbgAs, const char *pszSymbol, PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3AsLinkModule(PUVM pUVM, RTDBGAS hDbgAs, RTDBGMOD hMod, PCDBGFADDRESS pModAddress, RTDBGSEGIDX iModSeg, uint32_t fFlags)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3ModInMem(PUVM pUVM, PCDBGFADDRESS pImageAddr, uint32_t fFlags, const char *pszName, const char *pszFilename,
                              RTLDRARCH enmArch, uint32_t cbImage, PRTDBGMOD phDbgMod, PRTERRINFO pErrInfo)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3MemScan(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, RTGCUINTPTR cbRange, RTGCUINTPTR uAlign, const void *pabNeedle, size_t cbNeedle, PDBGFADDRESS pHitAddress)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3MemRead(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void *pvBuf, size_t cbRead)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3MemReadString(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, char *pszBuf, size_t cchBuf)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3MemWrite(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, const void *pvBuf, size_t cbRead)
{
    return VERR_INTERNAL_ERROR;
}
VMMDECL(int) DBGFR3PagingDumpEx(PUVM pUVM, VMCPUID idCpu, uint32_t fFlags, uint64_t cr3, uint64_t u64FirstAddr,
                                uint64_t u64LastAddr, uint32_t cMaxDepth, PCDBGFINFOHLP pHlp)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegNmValidate(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg)
{
    if (   !strcmp(pszReg, "ah")
        || !strcmp(pszReg, "ax")
        || !strcmp(pszReg, "eax")
        || !strcmp(pszReg, "rax"))
        return VINF_SUCCESS;
    return VERR_DBGF_REGISTER_NOT_FOUND;
}
VMMR3DECL(const char *) DBGFR3RegCpuName(PUVM pUVM, DBGFREG enmReg, DBGFREGVALTYPE enmType)
{
    return NULL;
}
VMMR3DECL(int) DBGFR3RegCpuQueryU8(  PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint8_t     *pu8)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegCpuQueryU16( PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint16_t    *pu16)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegCpuQueryU32( PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint32_t    *pu32)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegCpuQueryU64( PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint64_t    *pu64)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegCpuQueryXdtr(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint64_t *pu64Base, uint16_t *pu16Limit)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegNmQuery(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, PDBGFREGVAL pValue, PDBGFREGVALTYPE penmType)
{
    if (idDefCpu == 0 || idDefCpu == DBGFREG_HYPER_VMCPUID)
    {
        if (!strcmp(pszReg, "ah"))
        {
            pValue->u16 = 0xf0;
            *penmType   = DBGFREGVALTYPE_U8;
            return VINF_SUCCESS;
        }
        if (!strcmp(pszReg, "ax"))
        {
            pValue->u16 = 0xbabe;
            *penmType   = DBGFREGVALTYPE_U16;
            return VINF_SUCCESS;
        }
        if (!strcmp(pszReg, "eax"))
        {
            pValue->u32 = 0xcafebabe;
            *penmType   = DBGFREGVALTYPE_U32;
            return VINF_SUCCESS;
        }
        if (!strcmp(pszReg, "rax"))
        {
            pValue->u64 = UINT64_C(0x00beef00feedface);
            *penmType   = DBGFREGVALTYPE_U32;
            return VINF_SUCCESS;
        }
    }
    return VERR_DBGF_REGISTER_NOT_FOUND;
}
VMMR3DECL(int) DBGFR3RegNmQueryEx(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, uint32_t fFlags,
                                  PDBGFREGENTRYNM paRegs, size_t *pcRegs)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegNmQueryAll(PUVM pUVM, PDBGFREGENTRYNM paRegs, size_t cRegs)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegNmQueryAllCount(PUVM pUVM, size_t *pcRegs)
{
    *pcRegs = 99;
    return VINF_SUCCESS;
}
VMMR3DECL(int) DBGFR3RegPrintf(PUVM pUVM, VMCPUID idCpu, char *pszBuf, size_t cbBuf, const char *pszFormat, ...)
{
    return VERR_INTERNAL_ERROR;
}
VMMDECL(ssize_t) DBGFR3RegFormatValue(char *pszBuf, size_t cbBuf, PCDBGFREGVAL pValue, DBGFREGVALTYPE enmType, bool fSpecial)
{
    return VERR_INTERNAL_ERROR;
}
VMMDECL(ssize_t) DBGFR3RegFormatValueEx(char *pszBuf, size_t cbBuf, PCDBGFREGVAL pValue, DBGFREGVALTYPE enmType,
                                        unsigned uBase, signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    RT_NOREF(pszBuf, cbBuf, pValue, enmType, uBase, cchWidth, cchPrecision, fFlags);
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3RegNmSet(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, PCDBGFREGVAL pValue, DBGFREGVALTYPE enmType)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(PDBGFADDRESS) DBGFR3AddrFromPhys(PUVM pUVM, PDBGFADDRESS pAddress, RTGCPHYS PhysAddr)
{
    return NULL;
}
VMMR3DECL(int)  DBGFR3AddrToHostPhys(PUVM pUVM, VMCPUID idCpu, PDBGFADDRESS pAddress, PRTHCPHYS pHCPhys)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int)  DBGFR3AddrToVolatileR3Ptr(PUVM pUVM, VMCPUID idCpu, PDBGFADDRESS pAddress, bool fReadOnly, void **ppvR3Ptr)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(int) DBGFR3OSRegister(PUVM pUVM, PCDBGFOSREG pReg)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3OSDetect(PUVM pUVM, char *pszName, size_t cchName)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3OSQueryNameAndVersion(PUVM pUVM, char *pszName, size_t cchName, char *pszVersion, size_t cchVersion)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(void *) DBGFR3OSQueryInterface(PUVM pUVM, DBGFOSINTERFACE enmIf)
{
    return NULL;
}

VMMR3DECL(int) DBGFR3SelQueryInfo(PUVM pUVM, VMCPUID idCpu, RTSEL Sel, uint32_t fFlags, PDBGFSELINFO pSelInfo)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(CPUMMODE) DBGFR3CpuGetMode(PUVM pUVM, VMCPUID idCpu)
{
    return CPUMMODE_INVALID;
}
VMMR3DECL(VMCPUID) DBGFR3CpuGetCount(PUVM pUVM)
{
    return 1;
}
VMMR3DECL(bool) DBGFR3CpuIsIn64BitCode(PUVM pUVM, VMCPUID idCpu)
{
    return false;
}
VMMR3DECL(bool) DBGFR3CpuIsInV86Code(PUVM pUVM, VMCPUID idCpu)
{
    return false;
}

VMMR3DECL(int) DBGFR3CoreWrite(PUVM pUVM, const char *pszFilename, bool fReplaceFile)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(int)  DBGFR3PlugInLoad(PUVM pUVM, const char *pszPlugIn, char *pszActual, size_t cbActual, PRTERRINFO pErrInfo)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int)  DBGFR3PlugInUnload(PUVM pUVM, const char *pszName)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(void) DBGFR3PlugInLoadAll(PUVM pUVM)
{
}
VMMR3DECL(int) DBGFR3TypeRegister(  PUVM pUVM, uint32_t cTypes, PCDBGFTYPEREG paTypes)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3TypeDeregister(PUVM pUVM, const char *pszType)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3TypeQueryReg(  PUVM pUVM, const char *pszType, PCDBGFTYPEREG *ppTypeReg)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3TypeQuerySize( PUVM pUVM, const char *pszType, size_t *pcbType)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3TypeSetSize(   PUVM pUVM, const char *pszType, size_t cbType)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3TypeDumpEx(    PUVM pUVM, const char *pszType, uint32_t fFlags,
                                    uint32_t cLvlMax, PFNDBGFR3TYPEDUMP pfnDump, void *pvUser)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3TypeQueryValByType(PUVM pUVM, PCDBGFADDRESS pAddress, const char *pszType,
                                        PDBGFTYPEVAL *ppVal)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(void) DBGFR3TypeValFree(PDBGFTYPEVAL pVal)
{
}
VMMR3DECL(int)  DBGFR3TypeValDumpEx(PUVM pUVM, PCDBGFADDRESS pAddress, const char *pszType, uint32_t fFlags,
                                    uint32_t cLvlMax, FNDBGFR3TYPEVALDUMP pfnDump, void *pvUser)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(int) DBGFR3FlowCreate(PUVM pUVM, VMCPUID idCpu, PDBGFADDRESS pAddressStart, uint32_t cbDisasmMax,
                                uint32_t fFlagsFlow, uint32_t fFlagsDisasm, PDBGFFLOW phFlow)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowRetain(DBGFFLOW hFlow)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowRelease(DBGFFLOW hFlow)
{
    return 0;
}
VMMR3DECL(int) DBGFR3FlowQueryStartBb(DBGFFLOW hFlow, PDBGFFLOWBB phFlowBb)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowQueryBbByAddress(DBGFFLOW hFlow, PDBGFADDRESS pAddr, PDBGFFLOWBB phFlowBb)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowQueryBranchTblByAddress(DBGFFLOW hFlow, PDBGFADDRESS pAddr, PDBGFFLOWBRANCHTBL phFlowBranchTbl)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowGetBbCount(DBGFFLOW hFlow)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowGetBranchTblCount(DBGFFLOW hFlow)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowBbRetain(DBGFFLOWBB hFlowBb)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowBbRelease(DBGFFLOWBB hFlowBb)
{
    return 0;
}
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetStartAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrStart)
{
    return NULL;
}
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetEndAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrEnd)
{
    return NULL;
}
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetBranchAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrTarget)
{
    return NULL;
}
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetFollowingAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrFollow)
{
    return NULL;
}
VMMR3DECL(DBGFFLOWBBENDTYPE) DBGFR3FlowBbGetType(DBGFFLOWBB hFlowBb)
{
    return DBGFFLOWBBENDTYPE_INVALID;
}
VMMR3DECL(uint32_t) DBGFR3FlowBbGetInstrCount(DBGFFLOWBB hFlowBb)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowBbGetFlags(DBGFFLOWBB hFlowBb)
{
    return 0;
}
VMMR3DECL(int) DBGFR3FlowBbQueryBranchTbl(DBGFFLOWBB hFlowBb, PDBGFFLOWBRANCHTBL phBranchTbl)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowBbQueryError(DBGFFLOWBB hFlowBb, const char **ppszErr)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowBbQueryInstr(DBGFFLOWBB hFlowBb, uint32_t idxInstr, PDBGFADDRESS pAddrInstr,
                                      uint32_t *pcbInstr, const char **ppszInstr)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowBbQuerySuccessors(DBGFFLOWBB hFlowBb, PDBGFFLOWBB phFlowBbFollow,
                                           PDBGFFLOWBB phFlowBbTarget)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowBbGetRefBbCount(DBGFFLOWBB hFlowBb)
{
    return 0;
}
VMMR3DECL(int) DBGFR3FlowBbGetRefBb(DBGFFLOWBB hFlowBb, PDBGFFLOWBB pahFlowBbRef, uint32_t cRef)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowBranchTblRetain(DBGFFLOWBRANCHTBL hFlowBranchTbl)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowBranchTblRelease(DBGFFLOWBRANCHTBL hFlowBranchTbl)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowBranchTblGetSlots(DBGFFLOWBRANCHTBL hFlowBranchTbl)
{
    return 0;
}
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBranchTblGetStartAddress(DBGFFLOWBRANCHTBL hFlowBranchTbl, PDBGFADDRESS pAddrStart)
{
    return NULL;
}
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBranchTblGetAddrAtSlot(DBGFFLOWBRANCHTBL hFlowBranchTbl, uint32_t idxSlot, PDBGFADDRESS pAddrSlot)
{
    return NULL;
}
VMMR3DECL(int) DBGFR3FlowBranchTblQueryAddresses(DBGFFLOWBRANCHTBL hFlowBranchTbl, PDBGFADDRESS paAddrs, uint32_t cAddrs)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowItCreate(DBGFFLOW hFlow, DBGFFLOWITORDER enmOrder, PDBGFFLOWIT phFlowIt)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(void) DBGFR3FlowItDestroy(DBGFFLOWIT hFlowIt)
{
}
VMMR3DECL(DBGFFLOWBB) DBGFR3FlowItNext(DBGFFLOWIT hFlowIt)
{
    return NULL;
}
VMMR3DECL(int) DBGFR3FlowItReset(DBGFFLOWIT hFlowIt)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowBranchTblItCreate(DBGFFLOW hFlow, DBGFFLOWITORDER enmOrder, PDBGFFLOWBRANCHTBLIT phFlowBranchTblIt)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(void) DBGFR3FlowBranchTblItDestroy(DBGFFLOWBRANCHTBLIT hFlowBranchTblIt)
{
}
VMMR3DECL(DBGFFLOWBRANCHTBL) DBGFR3FlowBranchTblItNext(DBGFFLOWBRANCHTBLIT hFlowBranchTblIt)
{
    return NULL;
}
VMMR3DECL(int) DBGFR3FlowBranchTblItReset(DBGFFLOWBRANCHTBLIT hFlowBranchTblIt)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceModCreateFromFlowGraph(PUVM pUVM, VMCPUID idCpu, DBGFFLOW hFlow,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeCommon,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeEntry,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeRegular,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeExit,
                                                     PDBGFFLOWTRACEMOD phFlowTraceMod)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceModRetain(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceModRelease(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    return 0;
}
VMMR3DECL(int) DBGFR3FlowTraceModEnable(DBGFFLOWTRACEMOD hFlowTraceMod, uint32_t cHits, uint32_t cRecordsMax)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceModDisable(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceModQueryReport(DBGFFLOWTRACEMOD hFlowTraceMod,
                                             PDBGFFLOWTRACEREPORT phFlowTraceReport)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceModClear(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceModAddProbe(DBGFFLOWTRACEMOD hFlowTraceMod, PCDBGFADDRESS pAddrProbe,
                                          DBGFFLOWTRACEPROBE hFlowTraceProbe, uint32_t fFlags)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceProbeCreate(PUVM pUVM, const char *pszDescr, PDBGFFLOWTRACEPROBE phFlowTraceProbe)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceProbeRetain(DBGFFLOWTRACEPROBE hFlowTraceProbe)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceProbeRelease(DBGFFLOWTRACEPROBE hFlowTraceProbe)
{
    return 0;
}
VMMR3DECL(int) DBGFR3FlowTraceProbeEntriesAdd(DBGFFLOWTRACEPROBE hFlowTraceProbe,
                                              PCDBGFFLOWTRACEPROBEENTRY paEntries, uint32_t cEntries)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceReportRetain(DBGFFLOWTRACEREPORT hFlowTraceReport)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceReportRelease(DBGFFLOWTRACEREPORT hFlowTraceReport)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceReportGetRecordCount(DBGFFLOWTRACEREPORT hFlowTraceReport)
{
    return 0;
}
VMMR3DECL(int) DBGFR3FlowTraceReportQueryRecord(DBGFFLOWTRACEREPORT hFlowTraceReport, uint32_t idxRec, PDBGFFLOWTRACERECORD phFlowTraceRec)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceReportQueryFiltered(DBGFFLOWTRACEREPORT hFlowTraceReport, uint32_t fFlags,
                                                  PDBGFFLOWTRACEREPORTFILTER paFilters, uint32_t cFilters,
                                                  DBGFFLOWTRACEREPORTFILTEROP enmOp,
                                                  PDBGFFLOWTRACEREPORT phFlowTraceReportFiltered)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) DBGFR3FlowTraceReportEnumRecords(DBGFFLOWTRACEREPORT hFlowTraceReport,
                                                PFNDBGFFLOWTRACEREPORTENUMCLBK pfnEnum,
                                                void *pvUser)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceRecordRetain(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return 0;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceRecordRelease(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return 0;
}
VMMR3DECL(uint64_t) DBGFR3FlowTraceRecordGetSeqNo(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return 0;
}
VMMR3DECL(uint64_t) DBGFR3FlowTraceRecordGetTimestamp(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return 0;
}
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowTraceRecordGetAddr(DBGFFLOWTRACERECORD hFlowTraceRecord, PDBGFADDRESS pAddr)
{
    return NULL;
}
VMMR3DECL(DBGFFLOWTRACEPROBE) DBGFR3FlowTraceRecordGetProbe(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return NULL;
}
VMMR3DECL(uint32_t) DBGFR3FlowTraceRecordGetValCount(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return 0;
}
VMMR3DECL(PCDBGFFLOWTRACEPROBEVAL) DBGFR3FlowTraceRecordGetVals(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return NULL;
}
VMMR3DECL(PCDBGFFLOWTRACEPROBEVAL) DBGFR3FlowTraceRecordGetValsCommon(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return NULL;
}
VMMR3DECL(VMCPUID) DBGFR3FlowTraceRecordGetCpuId(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    return 0;
}

VMMR3DECL(int) DBGFR3FormatBugCheck(PUVM pUVM, char *pszDetails, size_t cbDetails,
                                    uint64_t uP0, uint64_t uP1, uint64_t uP2, uint64_t uP3, uint64_t uP4)
{
    pszDetails[0] = '\0';
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(PDBGFADDRESS) DBGFR3AddrAdd(PDBGFADDRESS pAddress, RTGCUINTPTR uAddend)
{
    RT_NOREF(uAddend);
    return pAddress;
}

#include <VBox/vmm/cfgm.h>
VMMR3DECL(int) CFGMR3ValidateConfig(PCFGMNODE pNode, const char *pszNode,
                                    const char *pszValidValues, const char *pszValidNodes,
                                    const char *pszWho, uint32_t uInstance)
{
    return VINF_SUCCESS;
}

VMMR3DECL(PCFGMNODE) CFGMR3GetRootU(PUVM pUVM)
{
    return NULL;
}

VMMR3DECL(PCFGMNODE) CFGMR3GetChild(PCFGMNODE pNode, const char *pszPath)
{
    return NULL;
}

VMMR3DECL(int) CFGMR3QueryString(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString)
{
    *pszString = '\0';
    return VINF_SUCCESS;
}

VMMR3DECL(int) CFGMR3QueryStringDef(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString, const char *pszDef)
{
    *pszString = '\0';
    return VINF_SUCCESS;
}



//////////////////////////////////////////////////////////////////////////
// The rest should eventually be replaced by DBGF calls and eliminated. //
/////////////////////////////////////////////////////////////////////////


#include <VBox/vmm/cpum.h>

VMMDECL(uint64_t) CPUMGetGuestCR3(PCVMCPU pVCpu)
{
    return 0;
}

VMMDECL(uint64_t) CPUMGetGuestCR4(PCVMCPU pVCpu)
{
    return 0;
}

VMMDECL(RTSEL) CPUMGetGuestCS(PCVMCPU pVCpu)
{
    return 0;
}

VMMDECL(uint32_t) CPUMGetGuestEIP(PCVMCPU pVCpu)
{
    return 0;
}

VMMDECL(uint64_t) CPUMGetGuestRIP(PCVMCPU pVCpu)
{
    return 0;
}

VMMDECL(RTGCPTR) CPUMGetGuestIDTR(PCVMCPU pVCpu, uint16_t *pcbLimit)
{
    return 0;
}

VMMDECL(CPUMMODE) CPUMGetGuestMode(PVMCPU pVCpu)
{
    return CPUMMODE_INVALID;
}

VMMDECL(PCPUMCTX) CPUMQueryGuestCtxPtr(PVMCPU pVCpu)
{
    return NULL;
}

VMMDECL(bool) CPUMIsGuestIn64BitCode(PCVMCPU pVCpu)
{
    return false;
}

VMMDECL(uint32_t) CPUMGetGuestEFlags(PCVMCPU pVCpu)
{
    return 2;
}

#include <VBox/vmm/hm.h>
VMMR3DECL(bool) HMR3IsEnabled(PUVM pUVM)
{
    return true;
}


#include <VBox/vmm/nem.h>
VMMR3DECL(bool) NEMR3IsEnabled(PUVM pUVM)
{
    return true;
}


#include <VBox/vmm/pgm.h>

VMMDECL(RTHCPHYS) PGMGetHyperCR3(PVMCPU pVCpu)
{
    return 0;
}

VMMDECL(PGMMODE) PGMGetShadowMode(PVMCPU pVCpu)
{
    return PGMMODE_INVALID;
}

VMMR3DECL(int) PGMR3DbgR3Ptr2GCPhys(PUVM pUVM, RTR3PTR R3Ptr, PRTGCPHYS pGCPhys)
{
    return VERR_INTERNAL_ERROR;
}

VMMR3DECL(int) PGMR3DbgR3Ptr2HCPhys(PUVM pUVM, RTR3PTR R3Ptr, PRTHCPHYS pHCPhys)
{
    return VERR_INTERNAL_ERROR;
}
VMMR3DECL(int) PGMR3DbgHCPhys2GCPhys(PUVM pUVM, RTHCPHYS HCPhys, PRTGCPHYS pGCPhys)
{
    return VERR_INTERNAL_ERROR;
}


#include <VBox/vmm/vmm.h>

VMMR3DECL(PVMCPU) VMMR3GetCpuByIdU(PUVM pUVM, RTCPUID idCpu)
{
    return NULL;
}

VMMR3DECL(PCVMMR3VTABLE) VMMR3GetVTable(void)
{
    return NULL;
}

VMMR3DECL(PVM) VMR3GetVM(PUVM pUVM)
{
    return NULL;
}

VMMR3DECL(VMSTATE) VMR3GetStateU(PUVM pUVM)
{
    return VMSTATE_DESTROYING;
}
