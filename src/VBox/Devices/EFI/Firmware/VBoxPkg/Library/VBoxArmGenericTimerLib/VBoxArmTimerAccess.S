/* $Id$ */
/* VirtualBox ARM timer selection. SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <AsmMacroLib.h>

/* EL2 firmware must use CNTHP: HVF owns the EL1 timers for the nested guest.
 * Select the physical counter with it so CNTVOFF cannot skew deadlines. */
.macro vboxtimerread, el1reg, el2reg
  mrs x0, CurrentEL
  cmp x0, #8
  b.ne .Lel1read\@
  mrs x0, \el2reg
  ret
.Lel1read\@:
  mrs x0, \el1reg
  ret
.endm

.macro vboxtimerwrite, el1reg, el2reg
  mrs x1, CurrentEL
  cmp x1, #8
  b.ne .Lel1write\@
  msr \el2reg, x0
  ret
.Lel1write\@:
  msr \el1reg, x0
  ret
.endm

ASM_FUNC(VBoxReadTimerCtl)
  vboxtimerread cntv_ctl_el0, cnthp_ctl_el2

ASM_FUNC(VBoxWriteTimerCtl)
  vboxtimerwrite cntv_ctl_el0, cnthp_ctl_el2

ASM_FUNC(VBoxReadTimerCval)
  vboxtimerread cntv_cval_el0, cnthp_cval_el2

ASM_FUNC(VBoxWriteTimerCval)
  vboxtimerwrite cntv_cval_el0, cnthp_cval_el2

ASM_FUNC(VBoxReadTimerTval)
  vboxtimerread cntv_tval_el0, cnthp_tval_el2

ASM_FUNC(VBoxWriteTimerTval)
  vboxtimerwrite cntv_tval_el0, cnthp_tval_el2

ASM_FUNC(VBoxReadTimerCounter)
  vboxtimerread cntvct_el0, cntpct_el0

ASM_FUNCTION_REMOVE_IF_UNREFERENCED
