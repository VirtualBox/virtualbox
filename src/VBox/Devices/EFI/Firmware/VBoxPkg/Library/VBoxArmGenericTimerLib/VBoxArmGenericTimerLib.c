/* $Id$ */
/** @file
  VirtualBox timer: virtual at EL1, hypervisor physical at EL2.

  Copyright (c) 2011 - 2014, ARM Ltd. All rights reserved.<BR>
  Copyright (c) 2014, Linaro Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/ArmLib.h>

UINTN EFIAPI VBoxReadTimerCtl (VOID);
VOID EFIAPI VBoxWriteTimerCtl (IN UINTN Value);
UINT64 EFIAPI VBoxReadTimerCval (VOID);
VOID EFIAPI VBoxWriteTimerCval (IN UINT64 Value);
UINTN EFIAPI VBoxReadTimerTval (VOID);
VOID EFIAPI VBoxWriteTimerTval (IN UINTN Value);
UINT64 EFIAPI VBoxReadTimerCounter (VOID);

/** Enable the timer selected for the current exception level. */
VOID
EFIAPI
ArmGenericTimerEnableTimer (
  VOID
  )
{
  UINTN  TimerCtrlReg;

  TimerCtrlReg  = VBoxReadTimerCtl ();
  TimerCtrlReg |= ARM_ARCH_TIMER_ENABLE;
  VBoxWriteTimerCtl (TimerCtrlReg);
}

/** Reenable the timer; the selected timer requires no additional action. */
VOID
EFIAPI
ArmGenericTimerReenableTimer (
  VOID
  )
{
}

/** Disable the timer selected for the current exception level. */
VOID
EFIAPI
ArmGenericTimerDisableTimer (
  VOID
  )
{
  UINTN  TimerCtrlReg;

  TimerCtrlReg  = VBoxReadTimerCtl ();
  TimerCtrlReg &= ~ARM_ARCH_TIMER_ENABLE;
  VBoxWriteTimerCtl (TimerCtrlReg);
}

/** Set the architectural counter frequency.
  @param[in] FreqInHz  Counter frequency in Hz.
**/
VOID
EFIAPI
ArmGenericTimerSetTimerFreq (
  IN   UINTN  FreqInHz
  )
{
  ArmWriteCntFrq (FreqInHz);
}

/** Get the architectural counter frequency.
  @return Counter frequency in Hz.
**/
UINTN
EFIAPI
ArmGenericTimerGetTimerFreq (
  VOID
  )
{
  return ArmReadCntFrq ();
}

/** Read the selected timer's relative deadline.
  @return Timer value register, in counter ticks.
**/
UINTN
EFIAPI
ArmGenericTimerGetTimerVal (
  VOID
  )
{
  return VBoxReadTimerTval ();
}

/** Set the selected timer's relative deadline.
  @param[in] Value  Timer value register, in counter ticks.
**/
VOID
EFIAPI
ArmGenericTimerSetTimerVal (
  IN   UINTN  Value
  )
{
  VBoxWriteTimerTval (Value);
}

/** Read the counter corresponding to the selected timer.
  @return Physical count at EL2, virtual count at EL1.
**/
UINT64
EFIAPI
ArmGenericTimerGetSystemCount (
  VOID
  )
{
  return VBoxReadTimerCounter ();
}

/** Read the selected timer's control register.
  @return Timer control register value.
**/
UINTN
EFIAPI
ArmGenericTimerGetTimerCtrlReg (
  VOID
  )
{
  return VBoxReadTimerCtl ();
}

/** Write the selected timer's control register.
  @param[in] Value  Timer control register value.
**/
VOID
EFIAPI
ArmGenericTimerSetTimerCtrlReg (
  UINTN  Value
  )
{
  VBoxWriteTimerCtl (Value);
}

/** Read the selected timer's absolute deadline.
  @return Timer compare value, in counter ticks.
**/
UINT64
EFIAPI
ArmGenericTimerGetCompareVal (
  VOID
  )
{
  return VBoxReadTimerCval ();
}

/** Set the selected timer's absolute deadline.
  @param[in] Value  Timer compare value, in counter ticks.
**/
VOID
EFIAPI
ArmGenericTimerSetCompareVal (
  IN   UINT64  Value
  )
{
  VBoxWriteTimerCval (Value);
}
