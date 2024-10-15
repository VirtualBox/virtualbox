# -*- coding: utf-8 -*-
# $Id: build_id_to_file_mapping.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Scans the given files (globbed) for topic id and stores records the filename
in the output file.

This is used by add_file_to_id_only_references.py after converting man_V*.xml
refentry files to dita to correct links.
"""

__copyright__ = \
"""
Copyright (C) 2023-2024 Oracle and/or its affiliates.

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

SPDX-License-Identifier: GPL-3.0-only
"""
__version__ = "$Revision: 106320 $"


# Standard python imports.
import glob;
import os;
import re;
import sys;


g_oReDita = re.compile(r'<topic[^><]*\sid=("[^">]+"|\'[^\'>]+\')');

def scanDitaFileForIds(dIdToFile, sContent, sFile):
    """
    Scans the given content of a .dita-file for topic IDs that can be referenced.
    """
    for oMatch in g_oReDita.finditer(sContent):
        sId = oMatch.group(1)[1:-1];
        if sId:
            dIdToFile[sId] = sFile;

g_oReRefentry = re.compile(r'<(refentry\b|refsect[12345]\b|cmdsynopsis\b)[^><]*\sid=("[^">]+"|\'[^\'>]+\')');

def scanDocbookRefentryForIds(dIdToFile, sContent, sFile):
    """
    Scans the given content of a Docbook refentry file for topic IDs that can be referenced.
    """
    for oMatch in g_oReRefentry.finditer(sContent):
        sId = oMatch.group(2)[1:-1];
        if sId:
            #dIdToFile[sId] = sFile;
            dIdToFile[sId] = '%s.dita' % (sId,);

def isDocbook(sContent):
    """
    Check if the file is a Docbook one.
    """
    return sContent.find('<refentry ') >= 0 and sContent.find('<refentryinfo>');

def error(sMessage):
    """ Reports an error. """
    print('build_id_to_file_mapping.py: error: %s' % sMessage, file = sys.stderr);
    return 1;

def syntax(sMessage):
    """ Reports a syntax error. """
    print('build_id_to_file_mapping.py: syntax error: %s' % sMessage, file = sys.stderr);
    return 2;

def usage():
    """ Reports usage. """
    print('usage: build_id_to_file_mapping.py --output <map.db> file1.dita docbook2.xml wild*card.* [...]');
    return 0;

def main(asArgs):
    """
    C-like main function.
    """
    #
    # Process arguments.
    #
    dIdToFile  = {};
    sOutput    = None;
    fEndOfArgs = False;
    iArg       = 1;
    while iArg < len(asArgs):
        sArg = asArgs[iArg];
        if sArg[0] == '-' and not fEndOfArgs:
            # Options.
            if sArg == '--':
                fEndOfArgs = True;
            elif sArg in ('--help', '-h', '-?'):
                return usage();
            elif sArg in ('--version', '-V' ):
                print(__version__[__version__.find(':') + 2:-2]);
            elif sArg in ('--output', '-o'):
                iArg += 1;
                if iArg >= len(asArgs):
                    return syntax('Expected filename following "--output"!');
                sOutput = asArgs[iArg];
            else:
                return syntax('Unknown option: %s' % (sArg,));
        else:
            # Input files.
            if sArg[0] == '@':
                with open(sArg[1:], 'r', encoding = 'utf-8') as oFile:
                    asFiles = oFile.read().split();
            else:
                asFiles = glob.glob(sArg);
            if not asFiles:
                return error('File not found: %s' % (sArg,));
            for sFile in asFiles:
                try:
                    with open(sFile, 'r', encoding = 'utf-8') as oFile:
                        sContent = oFile.read();
                except Exception as oXcpt: # pylint: disable=broad-exception-caught
                    return error('Failed to open and read "%s": %s' % (sFile, oXcpt,));
                if isDocbook(sContent):
                    scanDocbookRefentryForIds(dIdToFile, sContent, os.path.splitext(os.path.basename(sFile))[0] + '.dita');
                else:
                    scanDitaFileForIds(dIdToFile, sContent, os.path.basename(sFile));
        iArg += 1;

    # Dump the dictionary.
    asDict = sorted(['%s=%s' % (sKey, sValue) for sKey, sValue in dIdToFile.items()]);
    if sOutput is not None:
        try:
            with open(sOutput, 'w', encoding = 'utf-8') as oFile:
                oFile.write('\n'.join(asDict));
        except Exception as oXcpt: # pylint: disable=broad-exception-caught
            return error('Failed to open and write "%s": %s' % (sFile, oXcpt,));
    else:
        sys.stdout.write('\n'.join(asDict));
    return 0;

if __name__ == "__main__":
    sys.exit(main(sys.argv));
