# -*- coding: utf-8 -*-
# $Id: webutils.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Common Web Utility Functions.
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
import os;
import sys;
import unittest;

# Python 3 hacks:
if sys.version_info[0] < 3:
    from urllib2        import quote        as urllib_quote;        # pylint: disable=import-error,no-name-in-module
    from urllib         import urlencode    as urllib_urlencode;    # pylint: disable=import-error,no-name-in-module
    from urllib2        import ProxyHandler as urllib_ProxyHandler; # pylint: disable=import-error,no-name-in-module
    from urllib2        import build_opener as urllib_build_opener; # pylint: disable=import-error,no-name-in-module
else:
    from urllib.parse   import quote        as urllib_quote;        # pylint: disable=import-error,no-name-in-module
    from urllib.parse   import urlencode    as urllib_urlencode;    # pylint: disable=import-error,no-name-in-module
    from urllib.request import ProxyHandler as urllib_ProxyHandler; # pylint: disable=import-error,no-name-in-module
    from urllib.request import build_opener as urllib_build_opener; # pylint: disable=import-error,no-name-in-module

# Validation Kit imports.
from common import utils;


def escapeElem(sText):
    """
    Escapes special character to HTML-safe sequences.
    """
    sText = sText.replace('&', '&amp;')
    sText = sText.replace('<', '&lt;')
    return sText.replace('>', '&gt;')

def escapeAttr(sText):
    """
    Escapes special character to HTML-safe sequences.
    """
    sText = sText.replace('&', '&amp;')
    sText = sText.replace('<', '&lt;')
    sText = sText.replace('>', '&gt;')
    return sText.replace('"', '&quot;')

def escapeElemToStr(oObject):
    """
    Stringifies the object and hands it to escapeElem.
    """
    if utils.isString(oObject):
        return escapeElem(oObject);
    return escapeElem(str(oObject));

def escapeAttrToStr(oObject):
    """
    Stringifies the object and hands it to escapeAttr.  May return unicode string.
    """
    if utils.isString(oObject):
        return escapeAttr(oObject);
    return escapeAttr(str(oObject));

def escapeAttrJavaScriptStringDQ(sText):
    """ Escapes a javascript string that is to be emitted between double quotes. """
    if '"' not in sText:
        chMin = min(sText);
        if ord(chMin) >= 0x20:
            return sText;

    sRet = '';
    for ch in sText:
        if ch == '"':
            sRet += '\\"';
        elif ord(ch) >= 0x20:
            sRet += ch;
        elif ch == '\n':
            sRet += '\\n';
        elif ch == '\r':
            sRet += '\\r';
        elif ch == '\t':
            sRet += '\\t';
        else:
            sRet += '\\x%02x' % (ch,);
    return sRet;

def quoteUrl(sText):
    """
    See urllib.quote().
    """
    return urllib_quote(sText);

def encodeUrlParams(dParams):
    """
    See urllib.urlencode().
    """
    return urllib_urlencode(dParams, doseq=True)

def hasSchema(sUrl):
    """
    Checks if the URL has a schema (e.g. http://) or is file/server relative.
    Returns True if schema is present, False if not.
    """
    iColon = sUrl.find(':');
    if iColon > 0:
        sSchema = sUrl[0:iColon];
        if len(sSchema) >= 2 and len(sSchema) < 16 and sSchema.islower() and sSchema.isalpha():
            return True;
    return False;

def getFilename(sUrl):
    """
    Extracts the filename from the URL.
    """
    ## @TODO This isn't entirely correct. Use the urlparser instead!
    sFilename = os.path.basename(sUrl.replace('/', os.path.sep));
    return sFilename;


def downloadFile(sUrlFile, sDstFile, sLocalPrefix, fnLog, fnError = None, fNoProxies=True):
    """
    Downloads the given file if an URL is given, otherwise assume it's
    something on the build share and copy it from there.

    Raises no exceptions, returns log + success indicator instead.

    Note! This method may use proxies configured on the system and the
          http_proxy, ftp_proxy, no_proxy environment variables.

    """
    if fnError is None:
        fnError = fnLog;

    if  sUrlFile.startswith('http://') \
     or sUrlFile.startswith('https://') \
     or sUrlFile.startswith('ftp://'):
        # Download the file.
        fnLog('Downloading "%s" to "%s"...' % (sUrlFile, sDstFile));
        try:
            ## @todo We get 404.html content instead of exceptions here, which is confusing and should be addressed.
            if not fNoProxies:
                oOpener = urllib_build_opener();
            else:
                oOpener = urllib_build_opener(urllib_ProxyHandler(proxies = {} ));
            oSrc = oOpener.open(sUrlFile);
            oDst = utils.openNoInherit(sDstFile, 'wb');
            oDst.write(oSrc.read());
            oDst.close();
            oSrc.close();
        except Exception as oXcpt:
            fnError('Error downloading "%s" to "%s": %s' % (sUrlFile, sDstFile, oXcpt));
            return False;
    else:
        # Assumes file from the build share.
        if sUrlFile.startswith('file:///'):
            sSrcPath = sUrlFile[7:];
        elif sUrlFile.startswith('file://'):
            sSrcPath = sUrlFile[6:];
        elif os.path.isabs(sUrlFile):
            sSrcPath = sUrlFile;
        else:
            sSrcPath = os.path.join(sLocalPrefix, sUrlFile);
        fnLog('Copying "%s" to "%s"...' % (sSrcPath, sDstFile));
        try:
            utils.copyFileSimple(sSrcPath, sDstFile);
        except Exception as oXcpt:
            fnError('Error copying "%s" to "%s": %s' % (sSrcPath, sDstFile, oXcpt));
            return False;

    return True;



#
# Unit testing.
#

# pylint: disable=missing-docstring
class CommonUtilsTestCase(unittest.TestCase):
    def testHasSchema(self):
        self.assertTrue(hasSchema('http://www.oracle.com/'));
        self.assertTrue(hasSchema('https://virtualbox.com/'));
        self.assertFalse(hasSchema('://virtualbox.com/'));
        self.assertFalse(hasSchema('/usr/bin'));
        self.assertFalse(hasSchema('usr/bin'));
        self.assertFalse(hasSchema('bin'));
        self.assertFalse(hasSchema('C:\\WINNT'));

if __name__ == '__main__':
    unittest.main();
    # not reached.

