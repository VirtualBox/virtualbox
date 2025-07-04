# -*- coding: utf-8 -*-
# $Id: rtexitcode.py 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $

"""
RTEXITCODE from iprt/types.h.
"""

__copyright__ = \
"""
Copyright (C) 2012-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106061 $"


## Success.
RTEXITCODE_SUCCESS = 0;
SUCCESS = RTEXITCODE_SUCCESS;
## General failure.
RTEXITCODE_FAILURE = 1;
FAILURE = RTEXITCODE_FAILURE;
## Invalid arguments.
RTEXITCODE_SYNTAX = 2;
SYNTAX  = RTEXITCODE_SYNTAX;
##  Initialization failure.
RTEXITCODE_INIT = 3;
INIT    = RTEXITCODE_INIT;
## Test skipped.
RTEXITCODE_SKIPPED = 4;
SKIPPED = RTEXITCODE_SKIPPED;
## Bad-testbox.
RTEXITCODE_BAD_TESTBOX = 32;
## Bad-testbox.
BAD_TESTBOX = RTEXITCODE_BAD_TESTBOX;

