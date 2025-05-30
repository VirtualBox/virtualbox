#
#  Copyright (c) 2014, ARM Limited. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
#

#include <AsmMacroLib.h>
#include <Library/ArmLib.h>

// For the moment we assume this will run in SVC mode on ARMv7

//UINT32
//EFIAPI
//ArmGicGetControlSystemRegisterEnable (
//  VOID
//  );
ASM_FUNC(ArmGicV3GetControlSystemRegisterEnable)
        mrc     p15, 0, r0, c12, c12, 5 // ICC_SRE
        bx      lr

//VOID
//EFIAPI
//ArmGicSetControlSystemRegisterEnable (
//  IN UINT32         ControlSystemRegisterEnable
//  );
ASM_FUNC(ArmGicV3SetControlSystemRegisterEnable)
        mcr     p15, 0, r0, c12, c12, 5 // ICC_SRE
        isb
        bx      lr

//VOID
//ArmGicV3EnableInterruptInterface (
//  VOID
//  );
ASM_FUNC(ArmGicV3EnableInterruptInterface)
        mov     r0, #1
        mcr     p15, 0, r0, c12, c12, 7 // ICC_IGRPEN1
        bx      lr

//VOID
//ArmGicV3DisableInterruptInterface (
//  VOID
//  );
ASM_FUNC(ArmGicV3DisableInterruptInterface)
        mov     r0, #0
        mcr     p15, 0, r0, c12, c12, 7 // ICC_IGRPEN1
        bx      lr

//VOID
//ArmGicV3EndOfInterrupt (
//  IN UINTN InterruptId
//  );
ASM_FUNC(ArmGicV3EndOfInterrupt)
        mcr     p15, 0, r0, c12, c12, 1 //ICC_EOIR1
        bx      lr

//UINTN
//ArmGicV3AcknowledgeInterrupt (
//  VOID
//  );
ASM_FUNC(ArmGicV3AcknowledgeInterrupt)
        mrc     p15, 0, r0, c12, c12, 0 //ICC_IAR1
        bx      lr

//VOID
//ArmGicV3SetPriorityMask (
//  IN UINTN                  Priority
//  );
ASM_FUNC(ArmGicV3SetPriorityMask)
        mcr     p15, 0, r0, c4, c6, 0 //ICC_PMR
        bx      lr

//VOID
//ArmGicV3SetBinaryPointer (
//  IN UINTN                  BinaryPoint
//  );
ASM_FUNC(ArmGicV3SetBinaryPointer)
        mcr     p15, 0, r0, c12, c12, 3 //ICC_BPR1
        bx      lr

//UINTN
//ArmGicV3GetControlRegister(
//  VOID
//  );
ASM_FUNC(ArmGicV3GetControlRegister)
        mrc     p15, 0, r0, c12, c12, 4 //ICC_CTLR
        bx      lr

//VOID
//ArmGicV3SetControlRegister(
//  IN UINTN          Value
//  );
ASM_FUNC(ArmGicV3SetControlRegister)
        mcr     p15, 0, r0, c12, c12, 4 //ICC_CTLR
        bx      lr
