/* $Id: VBoxDTraceR0A-arm64.S 106964 2024-11-12 02:42:33Z knut.osmundsen@oracle.com $ */
/** @file
 * VBoxDTraceR0.
 *
 * Contributed by: bird
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from http://www.virtualbox.org.
 *
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License Version 1.0 (CDDL) only, as it
 * comes in the "COPYING.CDDL" file of the VirtualBox distribution.
 *
 * SPDX-License-Identifier: CDDL-1.0
 */

#include "iprt/asmdefs-arm.h"

BEGINCODE

BEGINPROC dtrace_probe6
        b       NAME(dtrace_probe)
ENDPROC   dtrace_probe6

