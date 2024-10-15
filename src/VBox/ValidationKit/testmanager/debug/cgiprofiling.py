#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: cgiprofiling.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Debug - CGI Profiling.
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


def profileIt(fnMain, sAppendToElement = 'main', sSort = 'time'):
    """
    Profiles a main() type function call (no parameters, returns int) and
    outputs a hacky HTML section.
    """

    #
    # Execute it.
    #
    import cProfile;
    oProfiler = cProfile.Profile();
    rc = oProfiler.runcall(fnMain);

    #
    # Output HTML to stdout (CGI assumption).
    #
    print('<div id="debug2"><br>\n' # Lazy BR-layouting!!
          '  <h2>Profiler Output</h2>\n'
          '  <pre>');
    try:
        oProfiler.print_stats(sort = sSort);
    except Exception as oXcpt:
        print('<p><pre>%s</pre></p>\n' % (oXcpt,));
    else:
        print('</pre>\n');
    oProfiler = None;
    print('</div>\n');

    #
    # Trick to move the section in under the SQL trace.
    #
    print('<script lang="script/javascript">\n'
          'var oMain = document.getElementById(\'%s\');\n'
          'if (oMain) {\n'
          '    oMain.appendChild(document.getElementById(\'debug2\'));\n'
          '}\n'
          '</script>\n'
          % (sAppendToElement, ) );

    return rc;

