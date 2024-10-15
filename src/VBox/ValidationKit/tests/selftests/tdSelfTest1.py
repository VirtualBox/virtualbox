#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdSelfTest1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Self Test - Dummy Test Driver.
"""

from __future__ import print_function;

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
__version__ = "$Revision: 106320 $"


import sys;
import os;

print('dummydriver.py: hello world!');
print('dummydriver.py: args: %s' % (sys.argv,));

print('dummydriver.py: environment:');
for sVar in sorted(os.environ.keys()): # pylint: disable=consider-iterating-dictionary
    print('%s=%s' % (sVar, os.environ[sVar]));

if sys.argv[-1] in [ 'all', 'execute' ]:

    import time;

    for i in range(10, 1, -1):
        print('dummydriver.py: %u...', i);
        sys.stdout.flush();
        time.sleep(1);
    print('dummydriver.py: ...0! done');

sys.exit(0);

