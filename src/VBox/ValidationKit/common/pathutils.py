# -*- coding: utf-8 -*-
# $Id: pathutils.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Path Utility Functions.
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
__version__ = "$Revision: 106320 $"


# Standard Python imports.
import unittest;


## @name Path data keyed by fDosStyle bool value.
## @{
g_dsPathSlash       = { False: '/', True: '\\', };
g_dasPathSlashes    = { False: ('/',), True: ('\\', '/', ), };
g_dasPathSeparators = { False: ('/',), True: ('\\', '/', ':', ), };
## @}


def joinEx(fDosStyle, sBase, *asAppend):
    """
    Mimicking os.path.join, but where target system isn't the host.
    The code is very simple at present.
    """
    # Get the first non-None element and use it as base.
    i = 0;
    sRet = sBase;
    while sRet is None and i < len(asAppend):
        sRet = asAppend[i];
        i += 1;

    while i < len(asAppend):
        sAppend = asAppend[i];

        # Skip None elements.
        if sAppend is not None:
            # Strip leading slashes from sAppend:
            offSkip = 0;
            while offSkip < len(sAppend) and sAppend[offSkip] in g_dasPathSlashes[fDosStyle]:
                offSkip += 1;

            # Add separator if needed before appending the new bit:
            if not sRet or sRet[-1] not in g_dasPathSeparators[fDosStyle]:
                sRet += g_dsPathSlash[fDosStyle] + sAppend[offSkip:];
            else:
                sRet += sAppend[offSkip:];

        i += 1;

    return sRet;


#
# Unit testing.
#

# pylint: disable=missing-docstring,undefined-variable
class JoinExTestCase(unittest.TestCase):
    def testJoinEx(self):
        self.assertEqual(joinEx(True, None), None);
        self.assertEqual(joinEx(False, None), None);
        self.assertEqual(joinEx(True, ''), '');
        self.assertEqual(joinEx(False, ''), '');
        self.assertEqual(joinEx(True, '',''), '\\');
        self.assertEqual(joinEx(False, '',''), '/');
        self.assertEqual(joinEx(True, 'C:','dos'), 'C:dos');
        self.assertEqual(joinEx(True, 'C:/','dos'), 'C:/dos');
        self.assertEqual(joinEx(True, 'C:\\','dos'), 'C:\\dos');
        self.assertEqual(joinEx(True, 'C:\\dos','edlin.com'), 'C:\\dos\\edlin.com');
        self.assertEqual(joinEx(True, 'C:\\dos\\','edlin.com'), 'C:\\dos\\edlin.com');
        self.assertEqual(joinEx(True, 'C:\\dos/','edlin.com'), 'C:\\dos/edlin.com');
        self.assertEqual(joinEx(True, 'C:\\dos//','edlin.com'), 'C:\\dos//edlin.com');
        self.assertEqual(joinEx(True, 'C:\\dos','\\/edlin.com'), 'C:\\dos\\edlin.com');
        self.assertEqual(joinEx(True, 'C:\\dos', None, 'edlin.com'), 'C:\\dos\\edlin.com');
        self.assertEqual(joinEx(True, None, 'C:\\dos', None, 'edlin.com'), 'C:\\dos\\edlin.com');
        self.assertEqual(joinEx(True, None, None, 'C:\\dos', None, 'edlin.com', None), 'C:\\dos\\edlin.com');
        self.assertEqual(joinEx(False, '/', 'bin', 'ls'), '/bin/ls');
        self.assertEqual(joinEx(False, '/', '/bin', 'ls'), '/bin/ls');
        self.assertEqual(joinEx(False, '/', '/bin/', 'ls'), '/bin/ls');
        self.assertEqual(joinEx(False, '/', '/bin//', 'ls'), '/bin//ls');
        self.assertEqual(joinEx(False, '/', None, 'bin', None, 'ls', None), '/bin/ls');
        self.assertEqual(joinEx(False, None, '/', None, 'bin', None, 'ls', None), '/bin/ls');


if __name__ == '__main__':
    unittest.main();
    # not reached.

