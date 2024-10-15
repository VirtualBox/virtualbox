# -*- coding: utf-8 -*-
# $Id: webservergluecgi.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Core - Web Server Abstraction Base Class.
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


# Standard python imports.
import cgi;     # pylint: disable=deprecated-module ## @todo these will be retired in python 3.13!
import cgitb;   # pylint: disable=deprecated-module ## @todo these will be retired in python 3.13!
import os;
import sys;

# Validation Kit imports.
from testmanager.core.webservergluebase import WebServerGlueBase;
from testmanager                        import config;


class WebServerGlueCgi(WebServerGlueBase):
    """
    CGI glue.
    """
    def __init__(self, sValidationKitDir, fHtmlOutput=True):
        WebServerGlueBase.__init__(self, sValidationKitDir, fHtmlOutput);

        if config.g_kfSrvGlueCgiTb is True:
            cgitb.enable(format=('html' if fHtmlOutput else 'text'));

    def getParameters(self):
        return cgi.parse(keep_blank_values=True);

    def getClientAddr(self):
        return os.environ.get('REMOTE_ADDR');

    def getMethod(self):
        return os.environ.get('REQUEST_METHOD', 'POST');

    def getLoginName(self):
        return os.environ.get('REMOTE_USER', WebServerGlueBase.ksUnknownUser);

    def getUrlScheme(self):
        return 'https' if 'HTTPS' in os.environ else 'http';

    def getUrlNetLoc(self):
        return os.environ['HTTP_HOST'];

    def getUrlPath(self):
        return os.environ['REQUEST_URI'];

    def getUserAgent(self):
        return os.getenv('HTTP_USER_AGENT', '');

    def getContentType(self):
        return cgi.parse_header(os.environ.get('CONTENT_TYPE', 'text/html'));

    def getContentLength(self):
        return int(os.environ.get('CONTENT_LENGTH', 0));

    def getBodyIoStream(self):
        return sys.stdin;

    def getBodyIoStreamBinary(self):
        # Python 3: sys.stdin.read() returns a string. To get untranslated
        #           binary data we use the sys.stdin.buffer object instead.
        return getattr(sys.stdin, 'buffer', sys.stdin);

