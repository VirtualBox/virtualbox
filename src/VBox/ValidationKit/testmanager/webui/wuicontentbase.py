# -*- coding: utf-8 -*-
# $Id: wuicontentbase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Web-UI - Content Base Classes.
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
import copy;
import sys;

# Validation Kit imports.
from common                         import utils, webutils;
from testmanager                    import config;
from testmanager.webui.wuibase      import WuiDispatcherBase, WuiException;
from testmanager.webui.wuihlpform   import WuiHlpForm;
from testmanager.core               import db;
from testmanager.core.base          import AttributeChangeEntryPre;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    unicode = str;  # pylint: disable=redefined-builtin,invalid-name


class WuiHtmlBase(object): # pylint: disable=too-few-public-methods
    """
    Base class for HTML objects.
    """

    def __init__(self):
        """Dummy init to shut up pylint."""
        pass;                               # pylint: disable=unnecessary-pass

    def toHtml(self):

        """
        Must be overridden by sub-classes.
        """
        assert False;
        return '';

    def __str__(self):
        """ String representation is HTML, simplifying formatting and such. """
        return self.toHtml();


class WuiLinkBase(WuiHtmlBase): # pylint: disable=too-few-public-methods
    """
    For passing links from WuiListContentBase._formatListEntry.
    """

    def __init__(self, sName, sUrlBase, dParams = None, sConfirm = None, sTitle = None,
                 sFragmentId = None, fBracketed = True, sExtraAttrs = ''):
        WuiHtmlBase.__init__(self);
        self.sName          = sName
        self.sUrl           = sUrlBase
        self.sConfirm       = sConfirm;
        self.sTitle         = sTitle;
        self.fBracketed     = fBracketed;
        self.sExtraAttrs    = sExtraAttrs;

        if dParams:
            # Do some massaging of None arguments.
            dParams = dict(dParams);
            for sKey in dParams:
                if dParams[sKey] is None:
                    dParams[sKey] = '';
            self.sUrl += '?' + webutils.encodeUrlParams(dParams);

        if sFragmentId is not None:
            self.sUrl += '#' + sFragmentId;

    def setBracketed(self, fBracketed):
        """Changes the bracketing style."""
        self.fBracketed = fBracketed;
        return True;

    def toHtml(self):
        """
        Returns a simple HTML anchor element.
        """
        sExtraAttrs = self.sExtraAttrs;
        if self.sConfirm is not None:
            sExtraAttrs += 'onclick=\'return confirm("%s");\' ' % (webutils.escapeAttr(self.sConfirm),);
        if self.sTitle is not None:
            sExtraAttrs += 'title="%s" ' % (webutils.escapeAttr(self.sTitle),);
        if sExtraAttrs and sExtraAttrs[-1] != ' ':
            sExtraAttrs += ' ';

        sFmt = '[<a %shref="%s">%s</a>]';
        if not self.fBracketed:
            sFmt = '<a %shref="%s">%s</a>';
        return sFmt % (sExtraAttrs, webutils.escapeAttr(self.sUrl), webutils.escapeElem(self.sName));

    @staticmethod
    def estimateStringWidth(sString):
        """
        Takes a string and estimate it's width so the caller can pad with
        U+2002 before tab in a title text.  This is very very rough.
        """
        cchWidth = 0;
        for ch in sString:
            if ch.isupper() or ch in u'wm\u2007\u2003\u2001\u3000':
                cchWidth += 2;
            else:
                cchWidth += 1;
        return cchWidth;

    @staticmethod
    def getStringWidthPaddingEx(cchWidth, cchMaxWidth):
        """ Works with estiamteStringWidth(). """
        if cchWidth + 2 <= cchMaxWidth:
            return u'\u2002' * ((cchMaxWidth - cchWidth) * 2 // 3)
        return u'';

    @staticmethod
    def getStringWidthPadding(sString, cchMaxWidth):
        """ Works with estiamteStringWidth(). """
        return WuiLinkBase.getStringWidthPaddingEx(WuiLinkBase.estimateStringWidth(sString), cchMaxWidth);

    @staticmethod
    def padStringToWidth(sString, cchMaxWidth):
        """ Works with estimateStringWidth. """
        cchWidth = WuiLinkBase.estimateStringWidth(sString);
        if cchWidth < cchMaxWidth:
            return sString + WuiLinkBase.getStringWidthPaddingEx(cchWidth, cchMaxWidth);
        return sString;


class WuiTmLink(WuiLinkBase): # pylint: disable=too-few-public-methods
    """ Local link to the test manager. """

    kdDbgParams = [];

    def __init__(self, sName, sUrlBase, dParams = None, sConfirm = None, sTitle = None,
                 sFragmentId = None, fBracketed = True):

        # Add debug parameters if necessary.
        if self.kdDbgParams:
            if not dParams:
                dParams = dict(self.kdDbgParams);
            else:
                dParams = dict(dParams);
                for sKey in self.kdDbgParams:
                    if sKey not in dParams:
                        dParams[sKey] = self.kdDbgParams[sKey];

        WuiLinkBase.__init__(self, sName, sUrlBase, dParams, sConfirm, sTitle, sFragmentId, fBracketed);


class WuiAdminLink(WuiTmLink): # pylint: disable=too-few-public-methods
    """ Local link to the test manager's admin portion. """

    def __init__(self, sName, sAction, tsEffectiveDate = None, dParams = None, sConfirm = None, sTitle = None,
                 sFragmentId = None, fBracketed = True):
        from testmanager.webui.wuiadmin import WuiAdmin;
        if not dParams:
            dParams = {};
        else:
            dParams = dict(dParams);
        if sAction is not None:
            dParams[WuiAdmin.ksParamAction] = sAction;
        if tsEffectiveDate is not None:
            dParams[WuiAdmin.ksParamEffectiveDate] = tsEffectiveDate;
        WuiTmLink.__init__(self, sName, WuiAdmin.ksScriptName, dParams = dParams, sConfirm = sConfirm, sTitle = sTitle,
                           sFragmentId = sFragmentId, fBracketed = fBracketed);

class WuiMainLink(WuiTmLink): # pylint: disable=too-few-public-methods
    """ Local link to the test manager's main portion. """

    def __init__(self, sName, sAction, dParams = None, sConfirm = None, sTitle = None, sFragmentId = None, fBracketed = True):
        if not dParams:
            dParams = {};
        else:
            dParams = dict(dParams);
        from testmanager.webui.wuimain import WuiMain;
        if sAction is not None:
            dParams[WuiMain.ksParamAction] = sAction;
        WuiTmLink.__init__(self, sName, WuiMain.ksScriptName, dParams = dParams, sConfirm = sConfirm, sTitle = sTitle,
                           sFragmentId = sFragmentId, fBracketed = fBracketed);

class WuiSvnLink(WuiLinkBase): # pylint: disable=too-few-public-methods
    """
    For linking to a SVN revision.
    """
    def __init__(self, iRevision, sName = None, fBracketed = True, sExtraAttrs = ''):
        if sName is None:
            sName = 'r%s' % (iRevision,);
        WuiLinkBase.__init__(self, sName, config.g_ksTracLogUrlPrefix, { 'rev': iRevision,},
                             fBracketed = fBracketed, sExtraAttrs = sExtraAttrs);

class WuiSvnLinkWithTooltip(WuiSvnLink): # pylint: disable=too-few-public-methods
    """
    For linking to a SVN revision with changelog tooltip.
    """
    def __init__(self, iRevision, sRepository, sName = None, fBracketed = True):
        sExtraAttrs = ' onmouseover="return svnHistoryTooltipShow(event,\'%s\',%s);" onmouseout="return tooltipHide();"' \
                    % ( sRepository, iRevision, );
        WuiSvnLink.__init__(self, iRevision, sName = sName, fBracketed = fBracketed, sExtraAttrs = sExtraAttrs);

class WuiBuildLogLink(WuiLinkBase):
    """
    For linking to a build log.
    """
    def __init__(self, sUrl, sName = None, fBracketed = True):
        assert sUrl;
        if sName is None:
            sName = 'Build log';
        if not webutils.hasSchema(sUrl):
            WuiLinkBase.__init__(self, sName, config.g_ksBuildLogUrlPrefix + sUrl, fBracketed = fBracketed);
        else:
            WuiLinkBase.__init__(self, sName, sUrl, fBracketed = fBracketed);

class WuiRawHtml(WuiHtmlBase): # pylint: disable=too-few-public-methods
    """
    For passing raw html from WuiListContentBase._formatListEntry.
    """
    def __init__(self, sHtml):
        self.sHtml = sHtml;
        WuiHtmlBase.__init__(self);

    def toHtml(self):
        return self.sHtml;

class WuiHtmlKeeper(WuiHtmlBase): # pylint: disable=too-few-public-methods
    """
    For keeping a list of elements, concatenating their toHtml output together.
    """
    def __init__(self, aoInitial = None, sSep = ' '):
        WuiHtmlBase.__init__(self);
        self.sSep   = sSep;
        self.aoKept = [];
        if aoInitial is not None:
            if isinstance(aoInitial, WuiHtmlBase):
                self.aoKept.append(aoInitial);
            else:
                self.aoKept.extend(aoInitial);

    def append(self, oObject):
        """ Appends one objects. """
        self.aoKept.append(oObject);

    def extend(self, aoObjects):
        """ Appends a list of objects. """
        self.aoKept.extend(aoObjects);

    def toHtml(self):
        return self.sSep.join(oObj.toHtml() for oObj in self.aoKept);

class WuiSpanText(WuiRawHtml): # pylint: disable=too-few-public-methods
    """
    Outputs the given text within a span of the given CSS class.
    """
    def __init__(self, sSpanClass, sText, sTitle = None):
        if sTitle is None:
            WuiRawHtml.__init__(self,
                                u'<span class="%s">%s</span>'
                                % ( webutils.escapeAttr(sSpanClass),  webutils.escapeElem(sText),));
        else:
            WuiRawHtml.__init__(self,
                                u'<span class="%s" title="%s">%s</span>'
                                % ( webutils.escapeAttr(sSpanClass), webutils.escapeAttr(sTitle), webutils.escapeElem(sText),));

class WuiElementText(WuiRawHtml): # pylint: disable=too-few-public-methods
    """
    Outputs the given element text.
    """
    def __init__(self, sText):
        WuiRawHtml.__init__(self, webutils.escapeElem(sText));


class WuiContentBase(object): # pylint: disable=too-few-public-methods
    """
    Base for the content classes.
    """

    ## The text/symbol for a very short add link.
    ksShortAddLink         = u'\u2795'
    ## HTML hex entity string for ksShortAddLink.
    ksShortAddLinkHtml     = '&#x2795;;'
    ## The text/symbol for a very short edit link.
    ksShortEditLink        = u'\u270D'
    ## HTML hex entity string for ksShortDetailsLink.
    ksShortEditLinkHtml    = '&#x270d;'
    ## The text/symbol for a very short details link.
    ksShortDetailsLink     = u'\U0001f6c8\ufe0e'
    ## HTML hex entity string for ksShortDetailsLink.
    ksShortDetailsLinkHtml =    '&#x1f6c8;;&#xfe0e;'
    ## The text/symbol for a very short change log / details / previous page link.
    ksShortChangeLogLink   = u'\u2397'
    ## HTML hex entity string for ksShortDetailsLink.
    ksShortChangeLogLinkHtml = '&#x2397;'
    ## The text/symbol for a very short reports link.
    ksShortReportLink      = u'\U0001f4ca\ufe0e'
    ## HTML hex entity string for ksShortReportLink.
    ksShortReportLinkHtml  =    '&#x1f4ca;&#xfe0e;'
    ## The text/symbol for a very short test results link.
    ksShortTestResultsLink = u'\U0001f5d0\ufe0e'


    def __init__(self, fnDPrint = None, oDisp = None):
        self._oDisp    = oDisp;         # WuiDispatcherBase.
        self._fnDPrint = fnDPrint;
        if fnDPrint is None  and  oDisp is not None:
            self._fnDPrint = oDisp.dprint;

    def dprint(self, sText):
        """ Debug printing. """
        if self._fnDPrint:
            self._fnDPrint(sText);

    @staticmethod
    def formatTsShort(oTs):
        """
        Formats a timestamp (db rep) into a short form.
        """
        oTsZulu = db.dbTimestampToZuluDatetime(oTs);
        sTs = oTsZulu.strftime('%Y-%m-%d %H:%M:%SZ');
        return unicode(sTs).replace('-', u'\u2011').replace(' ', u'\u00a0');

    def getNowTs(self):
        """ Gets a database compatible current timestamp from python. See db.dbTimestampPythonNow(). """
        return db.dbTimestampPythonNow();

    def formatIntervalShort(self, oInterval):
        """
        Formats an interval (db rep) into a short form.
        """
        # default formatting for negative intervals.
        if oInterval.days < 0:
            return str(oInterval);

        # Figure the hour, min and sec counts.
        cHours   = oInterval.seconds // 3600;
        cMinutes = (oInterval.seconds % 3600) // 60;
        cSeconds = oInterval.seconds - cHours * 3600 - cMinutes * 60;

        # Tailor formatting to the interval length.
        if oInterval.days > 0:
            if oInterval.days > 1:
                return '%d days, %d:%02d:%02d' % (oInterval.days, cHours, cMinutes, cSeconds);
            return '1 day, %d:%02d:%02d' % (cHours, cMinutes, cSeconds);
        if cMinutes > 0 or cSeconds >= 30 or cHours > 0:
            return '%d:%02d:%02d' % (cHours, cMinutes, cSeconds);
        if cSeconds >= 10:
            return '%d.%ds'   % (cSeconds, oInterval.microseconds // 100000);
        if cSeconds > 0:
            return '%d.%02ds' % (cSeconds, oInterval.microseconds // 10000);
        return '%d ms' % (oInterval.microseconds // 1000,);

    @staticmethod
    def genericPageWalker(iCurItem, cItems, sHrefFmt, cWidth = 11, iBase = 1, sItemName = 'page'):
        """
        Generic page walker generator.

        sHrefFmt has three %s sequences:
            1. The first is the page number link parameter (0-based).
            2. The title text, iBase-based number or text.
            3. The link text, iBase-based number or text.
        """

        # Calc display range.
        iStart = 0 if iCurItem - cWidth // 2 <= cWidth // 4 else iCurItem - cWidth // 2;
        iEnd   = iStart + cWidth;
        if iEnd > cItems:
            iEnd = cItems;
            if cItems > cWidth:
                iStart = cItems - cWidth;

        sHtml = u'';

        # Previous page (using << >> because &laquo; and &raquo are too tiny).
        if iCurItem > 0:
            sHtml += '%s&nbsp;&nbsp;' % sHrefFmt % (iCurItem - 1, 'previous ' + sItemName, '&lt;&lt;');
        else:
            sHtml += '&lt;&lt;&nbsp;&nbsp;';

        # 1 2 3 4...
        if iStart > 0:
            sHtml += '%s&nbsp; ... &nbsp;\n' % (sHrefFmt % (0, 'first %s' % (sItemName,), 0 + iBase),);

        sHtml += '&nbsp;\n'.join(sHrefFmt % (i, '%s %d' % (sItemName, i + iBase), i + iBase) if i != iCurItem
                                 else unicode(i + iBase)
                                 for i in range(iStart, iEnd));
        if iEnd < cItems:
            sHtml += '&nbsp; ... &nbsp;%s\n' % (sHrefFmt % (cItems - 1, 'last %s' % (sItemName,), cItems - 1 + iBase));

        # Next page.
        if iCurItem + 1 < cItems:
            sHtml += '&nbsp;&nbsp;%s' % sHrefFmt % (iCurItem + 1, 'next ' + sItemName, '&gt;&gt;');
        else:
            sHtml += '&nbsp;&nbsp;&gt;&gt;';

        return sHtml;

class WuiSingleContentBase(WuiContentBase): # pylint: disable=too-few-public-methods
    """
    Base for the content classes working on a single data object (oData).
    """
    def __init__(self, oData, oDisp = None, fnDPrint = None):
        WuiContentBase.__init__(self, oDisp = oDisp, fnDPrint = fnDPrint);
        self._oData = oData;            # Usually ModelDataBase.


class WuiFormContentBase(WuiSingleContentBase): # pylint: disable=too-few-public-methods
    """
    Base class for simple input form content classes (single data object).
    """

    ## @name Form mode.
    ## @{
    ksMode_Add  = 'add';
    ksMode_Edit = 'edit';
    ksMode_Show = 'show';
    ## @}

    ## Default action mappings.
    kdSubmitActionMappings = {
        ksMode_Add:  'AddPost',
        ksMode_Edit: 'EditPost',
    };

    def __init__(self, oData, sMode, sCoreName, oDisp, sTitle, sId = None, fEditable = True, sSubmitAction = None):
        WuiSingleContentBase.__init__(self, copy.copy(oData), oDisp);
        assert sMode in [self.ksMode_Add, self.ksMode_Edit, self.ksMode_Show];
        assert len(sTitle) > 1;
        assert sId is None or sId;

        self._sMode         = sMode;
        self._sCoreName     = sCoreName;
        self._sActionBase   = 'ksAction' + sCoreName;
        self._sTitle        = sTitle;
        self._sId           = sId if sId is not None else (type(oData).__name__.lower() + 'form');
        self._fEditable     = fEditable and (oDisp is None or not oDisp.isReadOnlyUser())
        self._sSubmitAction = sSubmitAction;
        if sSubmitAction is None and sMode != self.ksMode_Show:
            self._sSubmitAction = getattr(oDisp, self._sActionBase + self.kdSubmitActionMappings[sMode]);
        self._sRedirectTo   = None;


    def _populateForm(self, oForm, oData):
        """
        Populates the form.  oData has parameter NULL values.
        This must be reimplemented by the child.
        """
        _ = oForm; _ = oData;
        raise Exception('Reimplement me!');

    def _generatePostFormContent(self, oData):
        """
        Generate optional content that comes below the form.
        Returns a list of tuples, where the first tuple element is the title
        and the second the content.  I.e. similar to show() output.
        """
        _ = oData;
        return [];

    @staticmethod
    def _calcChangeLogEntryLinks(aoEntries, iEntry):
        """
        Returns an array of links to go with the change log entry.
        """
        _ = aoEntries; _ = iEntry;
        ## @todo detect deletion and recreation.
        ## @todo view details link.
        ## @todo restore link (need new action)
        ## @todo clone link.
        return [];

    @staticmethod
    def _guessChangeLogEntryDescription(aoEntries, iEntry):
        """
        Guesses the action + author that caused the change log entry.
        Returns descriptive string.
        """
        oEntry = aoEntries[iEntry];

        # Figure the author of the change.
        if oEntry.sAuthor is not None:
            sAuthor = '%s (#%s)' % (oEntry.sAuthor, oEntry.uidAuthor,);
        elif oEntry.uidAuthor is not None:
            sAuthor = '#%d (??)' % (oEntry.uidAuthor,);
        else:
            sAuthor = None;

        # Figure the action.
        if oEntry.oOldRaw is None:
            if sAuthor is None:
                return 'Created by batch job.';
            return 'Created by %s.' % (sAuthor,);

        if sAuthor is None:
            return 'Automatically updated.'
        return 'Modified by %s.' % (sAuthor,);

    @staticmethod
    def formatChangeLogEntry(aoEntries, iEntry, sUrl, dParams):
        """
        Formats one change log entry into one or more HTML table rows.

        The sUrl and dParams arguments are used to construct links to historical
        data using the tsEffective value.  If no links wanted, they'll both be None.

        Note! The parameters are given as array + index in case someone wishes
              to access adjacent entries later in order to generate better
              change descriptions.
        """
        oEntry = aoEntries[iEntry];

        # Turn the effective date into a URL if we can:
        if sUrl:
            dParams[WuiDispatcherBase.ksParamEffectiveDate] = oEntry.tsEffective;
            sEffective = WuiLinkBase(WuiFormContentBase.formatTsShort(oEntry.tsEffective), sUrl,
                                     dParams, fBracketed = False).toHtml();
        else:
            sEffective = webutils.escapeElem(WuiFormContentBase.formatTsShort(oEntry.tsEffective))

        # The primary row.
        sRowClass = 'tmodd' if (iEntry + 1) & 1 else 'tmeven';
        sContent = '    <tr class="%s">\n' \
                   '      <td rowspan="%d">%s</td>\n' \
                   '      <td rowspan="%d">%s</td>\n' \
                   '      <td colspan="3">%s%s</td>\n' \
                   '    </tr>\n' \
                 % ( sRowClass,
                     len(oEntry.aoChanges) + 1, sEffective,
                     len(oEntry.aoChanges) + 1, webutils.escapeElem(WuiFormContentBase.formatTsShort(oEntry.tsExpire)),
                     WuiFormContentBase._guessChangeLogEntryDescription(aoEntries, iEntry),
                     ' '.join(oLink.toHtml() for oLink in WuiFormContentBase._calcChangeLogEntryLinks(aoEntries, iEntry)),);

        # Additional rows for each changed attribute.
        j = 0;
        for oChange in oEntry.aoChanges:
            if isinstance(oChange, AttributeChangeEntryPre):
                sContent += '        <tr class="%s%s"><td>%s</td>'\
                            '<td><div class="tdpre">%s%s%s</div></td>' \
                            '<td><div class="tdpre">%s%s%s</div></td></tr>\n' \
                          % ( sRowClass, 'odd' if j & 1 else 'even',
                              webutils.escapeElem(oChange.sAttr),
                              '<pre>' if oChange.sOldText else '',
                               webutils.escapeElem(oChange.sOldText),
                              '</pre>' if oChange.sOldText else '',
                              '<pre>' if oChange.sNewText else '',
                              webutils.escapeElem(oChange.sNewText),
                              '</pre>' if oChange.sNewText else '', );
            else:
                sContent += '        <tr class="%s%s"><td>%s</td><td>%s</td><td>%s</td></tr>\n' \
                          % ( sRowClass, 'odd' if j & 1 else 'even',
                              webutils.escapeElem(oChange.sAttr),
                              webutils.escapeElem(oChange.sOldText),
                              webutils.escapeElem(oChange.sNewText), );
            j += 1;

        return sContent;

    def _showChangeLogNavi(self, fMoreEntries, iPageNo, cEntriesPerPage, tsNow, sWhere):
        """
        Returns the HTML for the change log navigator.
        Note! See also _generateNavigation.
        """
        sNavigation  = '<div class="tmlistnav-%s">\n' % sWhere;
        sNavigation += '  <table class="tmlistnavtab">\n' \
                       '    <tr>\n';
        dParams = self._oDisp.getParameters();
        dParams[WuiDispatcherBase.ksParamChangeLogEntriesPerPage] = cEntriesPerPage;
        dParams[WuiDispatcherBase.ksParamChangeLogPageNo]         = iPageNo;
        if tsNow is not None:
            dParams[WuiDispatcherBase.ksParamEffectiveDate]       = tsNow;

        # Prev and combo box in one cell. Both inside the form for formatting reasons.
        sNavigation += '    <td>\n' \
                       '    <form name="ChangeLogEntriesPerPageForm" method="GET">\n'

        # Prev
        if iPageNo > 0:
            dParams[WuiDispatcherBase.ksParamChangeLogPageNo] = iPageNo - 1;
            sNavigation += '<a href="?%s#tmchangelog">Previous</a>\n' \
                         % (webutils.encodeUrlParams(dParams),);
            dParams[WuiDispatcherBase.ksParamChangeLogPageNo] = iPageNo;
        else:
            sNavigation += 'Previous\n';

        # Entries per page selector.
        del dParams[WuiDispatcherBase.ksParamChangeLogEntriesPerPage];
        sNavigation += '&nbsp; &nbsp;\n' \
                       '  <select name="%s" onchange="window.location=\'?%s&%s=\' + ' \
                       'this.options[this.selectedIndex].value + \'#tmchangelog\';" ' \
                       'title="Max change log entries per page">\n' \
                     % (WuiDispatcherBase.ksParamChangeLogEntriesPerPage,
                        webutils.encodeUrlParams(dParams),
                        WuiDispatcherBase.ksParamChangeLogEntriesPerPage);
        dParams[WuiDispatcherBase.ksParamChangeLogEntriesPerPage] = cEntriesPerPage;

        for iEntriesPerPage in [2, 4, 8, 16, 32, 64, 128, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 8192]:
            sNavigation += '        <option value="%d" %s>%d entries per page</option>\n' \
                         % ( iEntriesPerPage,
                             'selected="selected"' if iEntriesPerPage == cEntriesPerPage else '',
                             iEntriesPerPage );
        sNavigation += '      </select>\n';

        # End of cell (and form).
        sNavigation += '    </form>\n' \
                       '   </td>\n';

        # Next
        if fMoreEntries:
            dParams[WuiDispatcherBase.ksParamChangeLogPageNo] = iPageNo + 1;
            sNavigation += '      <td><a href="?%s#tmchangelog">Next</a></td>\n' \
                         % (webutils.encodeUrlParams(dParams),);
        else:
            sNavigation += '      <td>Next</td>\n';

        sNavigation += '    </tr>\n' \
                       '  </table>\n' \
                       '</div>\n';
        return sNavigation;

    def setRedirectTo(self, sRedirectTo):
        """
        For setting the hidden redirect-to field.
        """
        self._sRedirectTo = sRedirectTo;
        return True;

    def showChangeLog(self, aoEntries, fMoreEntries, iPageNo, cEntriesPerPage, tsNow, fShowNavigation = True):
        """
        Render the change log, returning raw HTML.
        aoEntries is an array of ChangeLogEntry.
        """
        sContent  = '\n' \
                    '<hr>\n' \
                    '<div id="tmchangelog">\n' \
                    '  <h3>Change Log </h3>\n';
        if fShowNavigation:
            sContent += self._showChangeLogNavi(fMoreEntries, iPageNo, cEntriesPerPage, tsNow, 'top');
        sContent += '  <table class="tmtable tmchangelog">\n' \
                    '    <thead class="tmheader">' \
                    '      <tr>' \
                    '        <th rowspan="2">When</th>\n' \
                    '        <th rowspan="2">Expire (excl)</th>\n' \
                    '        <th colspan="3">Changes</th>\n' \
                    '      </tr>\n' \
                    '      <tr>\n' \
                    '        <th>Attribute</th>\n' \
                    '        <th>Old value</th>\n' \
                    '        <th>New value</th>\n' \
                    '      </tr>\n' \
                    '    </thead>\n' \
                    '    <tbody>\n';

        if self._sMode == self.ksMode_Show:
            sUrl    = self._oDisp.getUrlNoParams();
            dParams = self._oDisp.getParameters();
        else:
            sUrl    = None;
            dParams = None;

        for iEntry, _ in enumerate(aoEntries):
            sContent += self.formatChangeLogEntry(aoEntries, iEntry, sUrl, dParams);

        sContent += '    </tbody>\n' \
                    '  </table>\n';
        if fShowNavigation and len(aoEntries) >= 8:
            sContent += self._showChangeLogNavi(fMoreEntries, iPageNo, cEntriesPerPage, tsNow, 'bottom');
        sContent += '</div>\n\n';
        return sContent;

    def _generateTopRowFormActions(self, oData):
        """
        Returns a list of WuiTmLinks.
        """
        aoActions = [];
        if self._sMode == self.ksMode_Show and self._fEditable:
            # Remove _idGen and effective date since we're always editing the current data,
            # and make sure the primary ID is present.  Also remove change log stuff.
            dParams = self._oDisp.getParameters();
            if hasattr(oData, 'ksIdGenAttr'):
                sIdGenParam = getattr(oData, 'ksParam_' + oData.ksIdGenAttr);
                if sIdGenParam in dParams:
                    del dParams[sIdGenParam];
            for sParam in [ WuiDispatcherBase.ksParamEffectiveDate, ] + list(WuiDispatcherBase.kasChangeLogParams):
                if sParam in dParams:
                    del dParams[sParam];
            dParams[getattr(oData, 'ksParam_' + oData.ksIdAttr)] = getattr(oData, oData.ksIdAttr);

            dParams[WuiDispatcherBase.ksParamAction] = getattr(self._oDisp, self._sActionBase + 'Edit');
            aoActions.append(WuiTmLink('Edit', '', dParams));

            # Add clone operation if available. This uses the same data selection as for showing details.  No change log.
            if hasattr(self._oDisp, self._sActionBase + 'Clone'):
                dParams = self._oDisp.getParameters();
                for sParam in WuiDispatcherBase.kasChangeLogParams:
                    if sParam in dParams:
                        del dParams[sParam];
                dParams[WuiDispatcherBase.ksParamAction] = getattr(self._oDisp, self._sActionBase + 'Clone');
                aoActions.append(WuiTmLink('Clone', '', dParams));

        elif self._sMode == self.ksMode_Edit:
            # Details views the details at a given time, so we need either idGen or an effecive date + regular id.
            dParams = {};
            if hasattr(oData, 'ksIdGenAttr'):
                sIdGenParam = getattr(oData, 'ksParam_' + oData.ksIdGenAttr);
                dParams[sIdGenParam] = getattr(oData, oData.ksIdGenAttr);
            elif hasattr(oData, 'tsEffective'):
                dParams[WuiDispatcherBase.ksParamEffectiveDate] = oData.tsEffective;
                dParams[getattr(oData, 'ksParam_' + oData.ksIdAttr)] = getattr(oData, oData.ksIdAttr);
            dParams[WuiDispatcherBase.ksParamAction] = getattr(self._oDisp, self._sActionBase + 'Details');
            aoActions.append(WuiTmLink('Details', '', dParams));

            # Add delete operation if available.
            if hasattr(self._oDisp, self._sActionBase + 'DoRemove'):
                dParams = self._oDisp.getParameters();
                dParams[WuiDispatcherBase.ksParamAction] = getattr(self._oDisp, self._sActionBase + 'DoRemove');
                dParams[getattr(oData, 'ksParam_' + oData.ksIdAttr)] = getattr(oData, oData.ksIdAttr);
                aoActions.append(WuiTmLink('Delete', '', dParams, sConfirm = "Are you absolutely sure?"));

        return aoActions;

    def showForm(self, dErrors = None, sErrorMsg = None):
        """
        Render the form.
        """
        oForm = WuiHlpForm(self._sId,
                           '?' + webutils.encodeUrlParams({WuiDispatcherBase.ksParamAction: self._sSubmitAction}),
                           dErrors if dErrors is not None else {},
                           fReadOnly = self._sMode == self.ksMode_Show);

        self._oData.convertToParamNull();

        # If form cannot be constructed due to some reason we
        # need to show this reason
        try:
            self._populateForm(oForm, self._oData);
            if self._sRedirectTo is not None:
                oForm.addTextHidden(self._oDisp.ksParamRedirectTo, self._sRedirectTo);
        except WuiException as oXcpt:
            sContent = unicode(oXcpt)
        else:
            sContent = oForm.finalize();

        # Add any post form content.
        atPostFormContent = self._generatePostFormContent(self._oData);
        if atPostFormContent:
            for iSection, tSection in enumerate(atPostFormContent):
                (sSectionTitle, sSectionContent) = tSection;
                sContent += u'<div id="postform-%d"  class="tmformpostsection">\n' % (iSection,);
                if sSectionTitle:
                    sContent += '<h3 class="tmformpostheader">%s</h3>\n' % (webutils.escapeElem(sSectionTitle),);
                sContent += u' <div id="postform-%d-content" class="tmformpostcontent">\n' % (iSection,);
                sContent += sSectionContent;
                sContent += u' </div>\n' \
                            u'</div>\n';

        # Add action to the top.
        aoActions = self._generateTopRowFormActions(self._oData);
        if aoActions:
            sActionLinks = '<p>%s</p>' % (' '.join(unicode(oLink) for oLink in aoActions));
            sContent = sActionLinks + sContent;

        # Add error info to the top.
        if sErrorMsg is not None:
            sContent = '<p class="tmerrormsg">' + webutils.escapeElem(sErrorMsg) + '</p>\n' + sContent;

        return (self._sTitle, sContent);

    def getListOfItems(self, asListItems = tuple(), asSelectedItems = tuple()):
        """
        Format generic list which should be used by HTML form
        """
        aoRet = []
        for sListItem in asListItems:
            fEnabled = sListItem in asSelectedItems;
            aoRet.append((sListItem, fEnabled, sListItem))
        return aoRet


class WuiListContentBase(WuiContentBase):
    """
    Base for the list content classes.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffectiveDate, sTitle, # pylint: disable=too-many-arguments
                 sId = None, fnDPrint = None, oDisp = None, aiSelectedSortColumns = None, fTimeNavigation = True):
        WuiContentBase.__init__(self, fnDPrint = fnDPrint, oDisp = oDisp);
        self._aoEntries         = aoEntries; ## @todo should replace this with a Logic object and define methods for querying.
        self._iPage             = iPage;
        self._cItemsPerPage     = cItemsPerPage;
        self._tsEffectiveDate   = tsEffectiveDate;
        self._fTimeNavigation   = fTimeNavigation;
        self._sTitle            = sTitle;       assert len(sTitle) > 1;
        if sId is None:
            sId                 = sTitle.strip().replace(' ', '').lower();
        assert sId.strip();
        self._sId               = sId;
        self._asColumnHeaders   = [];
        self._asColumnAttribs   = [];
        self._aaiColumnSorting  = [];   ##< list of list of integers
        self._aiSelectedSortColumns = aiSelectedSortColumns; ##< list of integers

    def _formatCommentCell(self, sComment, cMaxLines = 3, cchMaxLine = 63):
        """
        Helper functions for formatting comment cell.
        Returns None or WuiRawHtml instance.
        """
        # Nothing to do for empty comments.
        if sComment is None:
            return None;
        sComment = sComment.strip();
        if not sComment:
            return None;

        # Restrict the text if necessary, making the whole text available thru mouse-over.
        ## @todo this would be better done by java script or smth, so it could automatically adjust to the table size.
        if len(sComment) > cchMaxLine or sComment.count('\n') >= cMaxLines:
            sShortHtml = '';
            for iLine, sLine in enumerate(sComment.split('\n')):
                if iLine >= cMaxLines:
                    break;
                if iLine > 0:
                    sShortHtml += '<br>\n';
                if len(sLine) > cchMaxLine:
                    sShortHtml += webutils.escapeElem(sLine[:(cchMaxLine - 3)]);
                    sShortHtml += '...';
                else:
                    sShortHtml += webutils.escapeElem(sLine);
            return WuiRawHtml('<span class="tmcomment" title="%s">%s</span>' % (webutils.escapeAttr(sComment), sShortHtml,));

        return WuiRawHtml('<span class="tmcomment">%s</span>' % (webutils.escapeElem(sComment).replace('\n', '<br>'),));

    def _formatListEntry(self, iEntry):
        """
        Formats the specified list entry as a list of column values.
        Returns HTML for a table row.

        The child class really need to override this!
        """
        # ASSUMES ModelDataBase children.
        asRet = [];
        for sAttr in self._aoEntries[0].getDataAttributes():
            asRet.append(getattr(self._aoEntries[iEntry], sAttr));
        return asRet;

    def _formatListEntryHtml(self, iEntry):
        """
        Formats the specified list entry as HTML.
        Returns HTML for a table row.

        The child class can override this to
        """
        if (iEntry + 1) & 1:
            sRow = u'  <tr class="tmodd">\n';
        else:
            sRow = u'  <tr class="tmeven">\n';

        aoValues = self._formatListEntry(iEntry);
        assert len(aoValues) == len(self._asColumnHeaders), '%s vs %s' % (len(aoValues), len(self._asColumnHeaders));

        for i, oValue in enumerate(aoValues):
            if i < len(self._asColumnAttribs) and self._asColumnAttribs[i]:
                sRow += u'    <td ' + self._asColumnAttribs[i] + '>';
            else:
                sRow += u'    <td>';

            if isinstance(oValue, WuiHtmlBase):
                sRow += oValue.toHtml();
            elif isinstance(oValue, list):
                if oValue:
                    for oElement in oValue:
                        if isinstance(oElement, WuiHtmlBase):
                            sRow += oElement.toHtml();
                        elif db.isDbTimestamp(oElement):
                            sRow += webutils.escapeElem(self.formatTsShort(oElement));
                        else:
                            sRow += webutils.escapeElem(unicode(oElement));
                        sRow += ' ';
            elif db.isDbTimestamp(oValue):
                sRow += webutils.escapeElem(self.formatTsShort(oValue));
            elif db.isDbInterval(oValue):
                sRow += webutils.escapeElem(self.formatIntervalShort(oValue));
            elif oValue is not None:
                sRow += webutils.escapeElem(unicode(oValue));

            sRow += u'</td>\n';

        return sRow + u'  </tr>\n';

    @staticmethod
    def generateTimeNavigationComboBox(sWhere, dParams, tsEffective):
        """
        Generates the HTML for the xxxx ago combo box form.
        """
        sNavigation  = '<form name="TmTimeNavCombo-%s" method="GET">\n' % (sWhere,);
        sNavigation += '  <select name="%s" onchange="window.location=' % (WuiDispatcherBase.ksParamEffectiveDate);
        sNavigation += '\'?%s&%s=\' + ' % (webutils.encodeUrlParams(dParams), WuiDispatcherBase.ksParamEffectiveDate)
        sNavigation += 'this.options[this.selectedIndex].value;" title="Effective date">\n';

        aoWayBackPoints = [
            ('+0000-00-00 00:00:00.00', 'Now', ' title="Present Day. Present Time."'), # lain :)

            ('-0000-00-00 01:00:00.00', '1 hour ago', ''),
            ('-0000-00-00 02:00:00.00', '2 hours ago', ''),
            ('-0000-00-00 03:00:00.00', '3 hours ago', ''),

            ('-0000-00-01 00:00:00.00', '1 day ago', ''),
            ('-0000-00-02 00:00:00.00', '2 days ago', ''),
            ('-0000-00-03 00:00:00.00', '3 days ago', ''),

            ('-0000-00-07 00:00:00.00', '1 week ago', ''),
            ('-0000-00-14 00:00:00.00', '2 weeks ago', ''),
            ('-0000-00-21 00:00:00.00', '3 weeks ago', ''),

            ('-0000-01-00 00:00:00.00', '1 month ago', ''),
            ('-0000-02-00 00:00:00.00', '2 months ago', ''),
            ('-0000-03-00 00:00:00.00', '3 months ago', ''),
            ('-0000-04-00 00:00:00.00', '4 months ago', ''),
            ('-0000-05-00 00:00:00.00', '5 months ago', ''),
            ('-0000-06-00 00:00:00.00', 'Half a year ago', ''),

            ('-0001-00-00 00:00:00.00', '1 year ago', ''),
        ]
        fSelected = False;
        for sTimestamp, sWayBackPointCaption, sExtraAttrs in aoWayBackPoints:
            if sTimestamp == tsEffective:
                fSelected = True;
            sNavigation += '    <option value="%s"%s%s>%s</option>\n' \
                         % (webutils.quoteUrl(sTimestamp),
                            ' selected="selected"' if sTimestamp == tsEffective else '',
                            sExtraAttrs, sWayBackPointCaption, );
        if not fSelected and tsEffective != '':
            sNavigation += '    <option value="%s" selected>%s</option>\n' \
                         % (webutils.quoteUrl(tsEffective), WuiContentBase.formatTsShort(tsEffective))

        sNavigation += '  </select>\n' \
                       '</form>\n';
        return sNavigation;

    @staticmethod
    def generateTimeNavigationDateTime(sWhere, dParams, sNow):
        """
        Generates HTML for a form with date + time input fields.

        Note! Modifies dParams!
        """

        #
        # Date + time input fields.  We use a java script helper to combine the two
        # into a hidden field as there is no portable datetime input field type.
        #
        sNavigation = '<form method="get" action="?" onchange="timeNavigationUpdateHiddenEffDate(this,\'%s\')">' % (sWhere,);
        if sNow is None:
            sNow = utils.getIsoTimestamp();
        else:
            sNow = utils.normalizeIsoTimestampToZulu(sNow);
        asSplit = sNow.split('T');
        sNavigation += '  <input type="date" value="%s" id="EffDate%s"/> ' % (asSplit[0], sWhere, );
        sNavigation += '  <input type="time" value="%s" id="EffTime%s"/> ' % (asSplit[1][:8], sWhere,);
        sNavigation += '  <input type="hidden" name="%s" value="%s" id="EffDateTime%s"/>' \
                     % (WuiDispatcherBase.ksParamEffectiveDate, webutils.escapeAttr(sNow), sWhere);
        for sKey in dParams:
            sNavigation += '  <input type="hidden" name="%s" value="%s"/>' \
                % (webutils.escapeAttr(sKey), webutils.escapeAttrToStr(dParams[sKey]));
        sNavigation += '  <input type="submit" value="Set"/>\n' \
                       '</form>\n';
        return sNavigation;

    ## @todo move to better place! WuiMain uses it.
    @staticmethod
    def generateTimeNavigation(sWhere, dParams, tsEffectiveAbs, sPreamble = '', sPostamble = '', fKeepPageNo = False):
        """
        Returns HTML for time navigation.

        Note! Modifies dParams!
        Note! Views without a need for a timescale just stubs this method.
        """
        sNavigation = '<div class="tmtimenav-%s tmtimenav">%s' % (sWhere, sPreamble,);

        #
        # Prepare the URL parameters.
        #
        if WuiDispatcherBase.ksParamPageNo in dParams: # Forget about page No when changing a period
            del dParams[WuiDispatcherBase.ksParamPageNo]
        if not fKeepPageNo and WuiDispatcherBase.ksParamEffectiveDate in dParams:
            tsEffectiveParam = dParams[WuiDispatcherBase.ksParamEffectiveDate];
            del dParams[WuiDispatcherBase.ksParamEffectiveDate];
        else:
            tsEffectiveParam = ''

        #
        # Generate the individual parts.
        #
        sNavigation += WuiListContentBase.generateTimeNavigationDateTime(sWhere, dParams, tsEffectiveAbs);
        sNavigation += WuiListContentBase.generateTimeNavigationComboBox(sWhere, dParams, tsEffectiveParam);

        sNavigation += '%s</div>' % (sPostamble,);
        return sNavigation;

    def _generateTimeNavigation(self, sWhere, sPreamble = '', sPostamble = ''):
        """
        Returns HTML for time navigation.

        Note! Views without a need for a timescale just stubs this method.
        """
        return self.generateTimeNavigation(sWhere, self._oDisp.getParameters(), self._oDisp.getEffectiveDateParam(),
                                           sPreamble, sPostamble)

    @staticmethod
    def generateItemPerPageSelector(sWhere, dParams, cCurItemsPerPage):
        """
        Generate HTML code for items per page selector.
        Note! Modifies dParams!
        """

        # Drop the current page count parameter.
        if WuiDispatcherBase.ksParamItemsPerPage in dParams:
            del dParams[WuiDispatcherBase.ksParamItemsPerPage];

        # Remove the current page number.
        if WuiDispatcherBase.ksParamPageNo in dParams:
            del dParams[WuiDispatcherBase.ksParamPageNo];

        sHtmlItemsPerPageSelector = '<form name="TmItemsPerPageForm-%s" method="GET" class="tmitemsperpage-%s tmitemsperpage">\n'\
                                    '  <select name="%s" onchange="window.location=\'?%s&%s=\' + ' \
                                    'this.options[this.selectedIndex].value;" title="Max items per page">\n' \
                                  % (sWhere, WuiDispatcherBase.ksParamItemsPerPage, sWhere,
                                     webutils.encodeUrlParams(dParams),
                                     WuiDispatcherBase.ksParamItemsPerPage)

        acItemsPerPage = [16, 32, 64, 128, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096];
        for cItemsPerPage in acItemsPerPage:
            sHtmlItemsPerPageSelector += '    <option value="%d" %s>%d per page</option>\n' \
                                       % (cItemsPerPage,
                                          'selected="selected"' if cItemsPerPage == cCurItemsPerPage else '',
                                          cItemsPerPage)
        sHtmlItemsPerPageSelector += '  </select>\n' \
                                     '</form>\n';

        return sHtmlItemsPerPageSelector


    def _generateNavigation(self, sWhere):
        """
        Return HTML for navigation.
        """

        #
        # ASSUMES the dispatcher/controller code fetches one entry more than
        # needed to fill the page to indicate further records.
        #
        sNavigation  = '<div class="tmlistnav-%s">\n' % sWhere;
        sNavigation += '  <table class="tmlistnavtab">\n' \
                       '    <tr>\n';
        dParams = self._oDisp.getParameters();
        dParams[WuiDispatcherBase.ksParamItemsPerPage] = self._cItemsPerPage;
        dParams[WuiDispatcherBase.ksParamPageNo]       = self._iPage;
        if self._tsEffectiveDate is not None:
            dParams[WuiDispatcherBase.ksParamEffectiveDate] = self._tsEffectiveDate;

        # Prev
        if self._iPage > 0:
            dParams[WuiDispatcherBase.ksParamPageNo] = self._iPage - 1;
            sNavigation += '    <td align="left"><a href="?%s">Previous</a></td>\n' % (webutils.encodeUrlParams(dParams),);
        else:
            sNavigation += '      <td></td>\n';

        # Time scale.
        if self._fTimeNavigation:
            sNavigation += '<td align="center" class="tmtimenav">';
            sNavigation += self._generateTimeNavigation(sWhere);
            sNavigation += '</td>';

        # page count and next.
        sNavigation += '<td align="right" class="tmnextanditemsperpage">\n';

        if len(self._aoEntries) > self._cItemsPerPage:
            dParams[WuiDispatcherBase.ksParamPageNo] = self._iPage + 1;
            sNavigation += '    <a href="?%s">Next</a>\n' % (webutils.encodeUrlParams(dParams),);
        sNavigation += self.generateItemPerPageSelector(sWhere, dParams, self._cItemsPerPage);
        sNavigation += '</td>\n';
        sNavigation += '    </tr>\n' \
                       '  </table>\n' \
                       '</div>\n';
        return sNavigation;

    def _checkSortingByColumnAscending(self, aiColumns):
        """
        Checks if we're sorting by this column.

        Returns 0 if not sorting by this, negative if descending, positive if ascending.  The
        value indicates the priority (nearer to 0 is higher).
        """
        if len(aiColumns) <= len(self._aiSelectedSortColumns):
            aiColumns    = list(aiColumns);
            aiNegColumns = list([-i for i in aiColumns]);   # pylint: disable=consider-using-generator
            i = 0;
            while i + len(aiColumns) <= len(self._aiSelectedSortColumns):
                aiSub = list(self._aiSelectedSortColumns[i : i + len(aiColumns)]);
                if aiSub == aiColumns:
                    return 1 + i;
                if aiSub == aiNegColumns:
                    return -1 - i;
                i += 1;
        return 0;

    def _generateTableHeaders(self):
        """
        Generate table headers.
        Returns raw html string.
        Overridable.
        """

        sHtml  = '  <thead class="tmheader"><tr>';
        for iHeader, oHeader in enumerate(self._asColumnHeaders):
            if isinstance(oHeader, WuiHtmlBase):
                sHtml += '<th>' + oHeader.toHtml() + '</th>';
            elif iHeader < len(self._aaiColumnSorting) and self._aaiColumnSorting[iHeader] is not None:
                sHtml += '<th>'
                iSorting = self._checkSortingByColumnAscending(self._aaiColumnSorting[iHeader]);
                if iSorting > 0:
                    sDirection  = '&nbsp;&#x25b4;' if iSorting == 1  else '<small>&nbsp;&#x25b5;</small>';
                    sSortParams = ','.join([str(-i) for i in self._aaiColumnSorting[iHeader]]);
                else:
                    sDirection = '';
                    if iSorting < 0:
                        sDirection  = '&nbsp;&#x25be;' if iSorting == -1 else '<small>&nbsp;&#x25bf;</small>'
                    sSortParams = ','.join([str(i) for i in self._aaiColumnSorting[iHeader]]);
                sHtml += '<a href="javascript:ahrefActionSortByColumns(\'%s\',[%s]);">' \
                       % (WuiDispatcherBase.ksParamSortColumns, sSortParams);
                sHtml += webutils.escapeElem(oHeader) + '</a>' + sDirection +  '</th>';
            else:
                sHtml += '<th>' + webutils.escapeElem(oHeader) + '</th>';
        sHtml += '</tr><thead>\n';
        return sHtml

    def _generateTable(self):
        """
        show worker that just generates the table.
        """

        #
        # Create a table.
        # If no colum headers are provided, fall back on database field
        # names, ASSUMING that the entries are ModelDataBase children.
        # Note! the cellspacing is for IE8.
        #
        sPageBody = '<table class="tmtable" id="' + self._sId + '" cellspacing="0">\n';

        if not self._asColumnHeaders:
            self._asColumnHeaders = self._aoEntries[0].getDataAttributes();

        sPageBody += self._generateTableHeaders();

        #
        # Format the body and close the table.
        #
        sPageBody += '  <tbody>\n';
        for iEntry in range(min(len(self._aoEntries), self._cItemsPerPage)):
            sPageBody += self._formatListEntryHtml(iEntry);
        sPageBody += '  </tbody>\n' \
                     '</table>\n';
        return sPageBody;

    def _composeTitle(self):
        """Composes the title string (return value)."""
        sTitle = self._sTitle;
        if self._iPage != 0:
            sTitle += ' (page ' + unicode(self._iPage + 1) + ')'
        if self._tsEffectiveDate is not None:
            sTitle += ' as per ' + unicode(self.formatTsShort(self._tsEffectiveDate));
        return sTitle;


    def show(self, fShowNavigation = True):
        """
        Displays the list.
        Returns (Title, HTML) on success, raises exception on error.
        """

        sPageBody = ''
        if fShowNavigation:
            sPageBody += self._generateNavigation('top');

        if self._aoEntries:
            sPageBody += self._generateTable();
            if fShowNavigation:
                sPageBody += self._generateNavigation('bottom');
        else:
            sPageBody += '<p>No entries.</p>'

        return (self._composeTitle(), sPageBody);


class WuiListContentWithActionBase(WuiListContentBase):
    """
    Base for the list content with action classes.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffectiveDate, sTitle, # pylint: disable=too-many-arguments
                 sId = None, fnDPrint = None, oDisp = None, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffectiveDate, sTitle, sId = sId,
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);
        self._aoActions     = None; # List of [ oValue, sText, sHover ] provided by the child class.
        self._sAction       = None; # Set by the child class.
        self._sCheckboxName = None; # Set by the child class.
        self._asColumnHeaders = [ WuiRawHtml('<input type="checkbox" onClick="toggle%s(this)">'
                                             % ('' if sId is None else sId)), ];
        self._asColumnAttribs = [ 'align="center"', ];
        self._aaiColumnSorting = [ None, ];

    def _getCheckBoxColumn(self, iEntry, sValue):
        """
        Used by _formatListEntry implementations, returns a WuiRawHtmlBase object.
        """
        _ = iEntry;
        return WuiRawHtml('<input type="checkbox" name="%s" value="%s">'
                          % (webutils.escapeAttr(self._sCheckboxName), webutils.escapeAttr(unicode(sValue))));

    def show(self, fShowNavigation=True):
        """
        Displays the list.
        Returns (Title, HTML) on success, raises exception on error.
        """
        assert self._aoActions is not None;
        assert self._sAction   is not None;

        sPageBody = '<script language="JavaScript">\n' \
                    'function toggle%s(oSource) {\n' \
                    '    aoCheckboxes = document.getElementsByName(\'%s\');\n' \
                    '    for(var i in aoCheckboxes)\n' \
                    '        aoCheckboxes[i].checked = oSource.checked;\n' \
                    '}\n' \
                    '</script>\n' \
                    % ('' if self._sId is None else self._sId, self._sCheckboxName,);
        if fShowNavigation:
            sPageBody += self._generateNavigation('top');
        if self._aoEntries:

            sPageBody += '<form action="?%s" method="post" class="tmlistactionform">\n' \
                       % (webutils.encodeUrlParams({WuiDispatcherBase.ksParamAction: self._sAction,}),);
            sPageBody += self._generateTable();

            sPageBody += '  <label>Actions</label>\n' \
                         '  <select name="%s" id="%s-action-combo" class="tmlistactionform-combo">\n' \
                       % (webutils.escapeAttr(WuiDispatcherBase.ksParamListAction), webutils.escapeAttr(self._sId),);
            for oValue, sText, _ in self._aoActions:
                sPageBody += '    <option value="%s">%s</option>\n' \
                           % (webutils.escapeAttr(unicode(oValue)), webutils.escapeElem(sText), );
            sPageBody += '  </select>\n';
            sPageBody += '  <input type="submit"></input>\n';
            sPageBody += '</form>\n';
            if fShowNavigation:
                sPageBody += self._generateNavigation('bottom');
        else:
            sPageBody += '<p>No entries.</p>'

        return (self._composeTitle(), sPageBody);

