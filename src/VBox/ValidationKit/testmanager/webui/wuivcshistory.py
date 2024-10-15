# -*- coding: utf-8 -*-
# $Id: wuivcshistory.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - VCS history
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

# Python imports.
#import datetime;

# Validation Kit imports.
from testmanager                        import config;
from testmanager.core                   import db;
from testmanager.webui.wuicontentbase   import WuiContentBase;
from common                             import webutils;

class WuiVcsHistoryTooltip(WuiContentBase):
    """
    WUI VCS history tooltip generator.
    """

    def __init__(self, aoEntries, sRepository, iRevision, cEntries, fnDPrint, oDisp):
        """Override initialization"""
        WuiContentBase.__init__(self, fnDPrint = fnDPrint, oDisp = oDisp);
        self.aoEntries      = aoEntries;
        self.sRepository    = sRepository;
        self.iRevision      = iRevision;
        self.cEntries       = cEntries;


    def show(self):
        """
        Generates the tooltip.
        Returns (sTitle, HTML).
        """
        sHtml  = '<div class="tmvcstimeline tmvcstimelinetooltip">\n';

        oCurDate = None;
        for oEntry in self.aoEntries:
            oTsZulu = db.dbTimestampToZuluDatetime(oEntry.tsCreated);
            if oCurDate is None or oCurDate != oTsZulu.date():
                if oCurDate is not None:
                    sHtml += ' </dl>\n'
                oCurDate = oTsZulu.date();
                sHtml += ' <h2>%s:</h2>\n' \
                         ' <dl>\n' \
                       % (oTsZulu.strftime('%Y-%m-%d'),);

            sEntry  = '  <dt id="r%s">' % (oEntry.iRevision, );
            sEntry += '<a href="%s" target="_blank">' \
                    % ( webutils.escapeAttr(config.g_ksTracChangsetUrlFmt
                                            % { 'iRevision': oEntry.iRevision, 'sRepository': oEntry.sRepository,}), );

            sEntry += '<span class="tmvcstimeline-time">%s</span>' % ( oTsZulu.strftime('%H:%MZ'), );
            sEntry += ' Changeset <span class="tmvcstimeline-rev">[%s]</span>' % ( oEntry.iRevision, );
            sEntry += ' by <span class="tmvcstimeline-author">%s</span>' % ( webutils.escapeElem(oEntry.sAuthor), );
            sEntry += '</a>\n';
            sEntry += '</dt>\n';
            sEntry += '  <dd>%s</dd>\n' % ( webutils.escapeElem(oEntry.sMessage), );

            sHtml += sEntry;

        if oCurDate is not None:
            sHtml += ' </dl>\n';
        sHtml += '</div>\n';

        return ('VCS History Tooltip', sHtml);

