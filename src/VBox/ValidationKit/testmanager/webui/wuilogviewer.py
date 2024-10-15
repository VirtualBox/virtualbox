# -*- coding: utf-8 -*-
# $Id: wuilogviewer.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Log viewer
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

# Validation Kit imports.
from common                             import webutils;
from testmanager.core.testset           import TestSetData;
from testmanager.webui.wuicontentbase   import WuiContentBase, WuiTmLink;
from testmanager.webui.wuimain          import WuiMain;


class WuiLogViewer(WuiContentBase):
    """Log viewer."""

    def __init__(self, oTestSet, oLogFile, cbChunk, iChunk, aoTimestamps, oDisp = None, fnDPrint = None):
        WuiContentBase.__init__(self, oDisp = oDisp, fnDPrint = fnDPrint);
        self._oTestSet      = oTestSet;
        self._oLogFile      = oLogFile;
        self._cbChunk       = cbChunk;
        self._iChunk        = iChunk;
        self._aoTimestamps  = aoTimestamps;

    def _generateNavigation(self, cbFile):
        """Generate the HTML for the log navigation."""

        dParams = {
            WuiMain.ksParamAction:          WuiMain.ksActionViewLog,
            WuiMain.ksParamLogSetId:        self._oTestSet.idTestSet,
            WuiMain.ksParamLogFileId:       self._oLogFile.idTestResultFile,
            WuiMain.ksParamLogChunkSize:    self._cbChunk,
            WuiMain.ksParamLogChunkNo:      self._iChunk,
        };

        #
        # The page walker.
        #
        dParams2 = dict(dParams);
        del dParams2[WuiMain.ksParamLogChunkNo];
        sHrefFmt        = '<a href="?%s&%s=%%s" title="%%s">%%s</a>' \
                        % (webutils.encodeUrlParams(dParams2).replace('%', '%%'), WuiMain.ksParamLogChunkNo,);
        sHtmlWalker = self.genericPageWalker(self._iChunk, (cbFile + self._cbChunk - 1) // self._cbChunk,
                                             sHrefFmt, 11, 0, 'chunk');

        #
        # The chunk size selector.
        #

        dParams2 = dict(dParams);
        del dParams2[WuiMain.ksParamLogChunkSize];
        sHtmlSize  = '<form name="ChunkSizeForm" method="GET">\n' \
                     '  Max <select name="%s" onchange="window.location=\'?%s&%s=\' + ' \
                     'this.options[this.selectedIndex].value;" title="Max items per page">\n' \
                   % ( WuiMain.ksParamLogChunkSize, webutils.encodeUrlParams(dParams2), WuiMain.ksParamLogChunkSize,);

        for cbChunk in [ 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152,
                         4194304, 8388608, 16777216 ]:
            sHtmlSize += '    <option value="%d" %s>%d bytes</option>\n' \
                       % (cbChunk, 'selected="selected"' if cbChunk == self._cbChunk else '', cbChunk);
        sHtmlSize += '  </select> per page\n' \
                     '</form>\n'

        #
        # Download links.
        #
        oRawLink      = WuiTmLink('View Raw', '',
                                  { WuiMain.ksParamAction:            WuiMain.ksActionGetFile,
                                    WuiMain.ksParamGetFileSetId:      self._oTestSet.idTestSet,
                                    WuiMain.ksParamGetFileId:         self._oLogFile.idTestResultFile,
                                    WuiMain.ksParamGetFileDownloadIt: False,
                                  },
                                  sTitle = '%u MiB' % ((cbFile + 1048576 - 1) // 1048576,) );
        oDownloadLink = WuiTmLink('Download Log', '',
                                  { WuiMain.ksParamAction:            WuiMain.ksActionGetFile,
                                    WuiMain.ksParamGetFileSetId:      self._oTestSet.idTestSet,
                                    WuiMain.ksParamGetFileId:         self._oLogFile.idTestResultFile,
                                    WuiMain.ksParamGetFileDownloadIt: True,
                                  },
                                  sTitle = '%u MiB' % ((cbFile + 1048576 - 1) // 1048576,) );
        oTestSetLink  = WuiTmLink('Test Set', '',
                                  { WuiMain.ksParamAction:            WuiMain.ksActionTestResultDetails,
                                    TestSetData.ksParam_idTestSet:    self._oTestSet.idTestSet,
                                  });


        #
        # Combine the elements and return.
        #
        return '<div class="tmlogviewernavi">\n' \
               ' <table width=100%>\n' \
               '  <tr>\n' \
               '   <td width=20%>\n' \
               '    ' + oTestSetLink.toHtml() + '\n' \
               '    ' + oRawLink.toHtml() + '\n' \
               '    ' + oDownloadLink.toHtml() + '\n' \
               '   </td>\n' \
               '   <td width=60% align=center>' + sHtmlWalker + '</td>' \
               '   <td width=20% align=right>' + sHtmlSize + '</td>\n' \
               '  </tr>\n' \
               ' </table>\n' \
               '</div>\n';

    def _displayLog(self, oFile, offFile, cbFile, aoTimestamps):
        """Displays the current section of the log file."""
        from testmanager.core import db;

        def prepCurTs():
            """ Formats the current timestamp. """
            if iCurTs < len(aoTimestamps):
                oTsZulu = db.dbTimestampToZuluDatetime(aoTimestamps[iCurTs]);
                return (oTsZulu.strftime('%H:%M:%S.%f'), oTsZulu.strftime('%H_%M_%S_%f'));
            return ('~~|~~|~~|~~~~~~', '~~|~~|~~|~~~~~~'); # ASCII chars with high values. Limit hits.

        def isCurLineAtOrAfterCurTs():
            """ Checks if the current line starts with a timestamp that is after the current one. """
            if    len(sLine) >= 15 \
              and sLine[2]  == ':' \
              and sLine[5]  == ':' \
              and sLine[8]  == '.' \
              and sLine[14] in '0123456789':
                if sLine[:15] >=  sCurTs and iCurTs < len(aoTimestamps):
                    return True;
            return False;

        # Figure the end offset.
        offEnd = offFile + self._cbChunk;
        offEnd = min(offEnd, cbFile);

        #
        # Here is an annoying thing, we cannot seek in zip file members. So,
        # since we have to read from the start, we can just as well count line
        # numbers while we're at it.
        #
        iCurTs           = 0;
        (sCurTs, sCurId) = prepCurTs();
        offCur           = 0;
        iLine            = 0;
        while True:
            sLine   = oFile.readline().decode('utf-8', 'replace');
            offLine = offCur;
            iLine  += 1;
            offCur += len(sLine);
            if offCur >= offFile or not sLine:
                break;
            while isCurLineAtOrAfterCurTs():
                iCurTs += 1;
                (sCurTs, sCurId) = prepCurTs();

        #
        # Got to where we wanted, format the chunk.
        #
        asLines = ['\n<div class="tmlog">\n<pre>\n', ];
        while True:
            # The timestamp IDs.
            sPrevTs = '';
            while isCurLineAtOrAfterCurTs():
                if sPrevTs != sCurTs:
                    asLines.append('<a id="%s"></a>' % (sCurId,));
                iCurTs += 1;
                (sCurTs, sCurId) = prepCurTs();

            # The line.
            asLines.append('<a id="L%d" href="#L%d">%05d</a><a id="O%d"></a>%s\n' \
                           % (iLine, iLine, iLine, offLine, webutils.escapeElem(sLine.rstrip())));

            # next
            if offCur >= offEnd:
                break;
            sLine   = oFile.readline().decode('utf-8', 'replace');
            offLine = offCur;
            iLine  += 1;
            offCur += len(sLine);
            if not sLine:
                break;
        asLines.append('<pre/></div>\n');
        return ''.join(asLines);


    def show(self):
        """Shows the log."""

        if self._oLogFile.sDescription not in [ '', None ]:
            sTitle = '%s - %s' % (self._oLogFile.sFile, self._oLogFile.sDescription);
        else:
            sTitle = '%s' % (self._oLogFile.sFile,);

        #
        # Open the log file. No universal line endings here.
        #
        (oFile, oSizeOrError, _) = self._oTestSet.openFile(self._oLogFile.sFile, 'rb');
        if oFile is None:
            return (sTitle, '<p>%s</p>\n' % (webutils.escapeElem(oSizeOrError),),);
        cbFile = oSizeOrError;

        #
        # Generate the page.
        #

        # Start with a focus hack.
        sHtml = '<div id="tmlogoutdiv" tabindex="0">\n' \
                '<script lang="text/javascript">\n' \
                'document.getElementById(\'tmlogoutdiv\').focus();\n' \
                '</script>\n';

        sNaviHtml = self._generateNavigation(cbFile);
        sHtml += sNaviHtml;

        offFile   = self._iChunk * self._cbChunk;
        if offFile < cbFile:
            sHtml += self._displayLog(oFile, offFile, cbFile, self._aoTimestamps);
            sHtml += sNaviHtml;
        else:
            sHtml += '<p>End Of File</p>';

        return (sTitle, sHtml);

