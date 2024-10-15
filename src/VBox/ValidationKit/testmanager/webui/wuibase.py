# -*- coding: utf-8 -*-
# $Id: wuibase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Web-UI - Base Classes.
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
import os;
import sys;
import string;

# Validation Kit imports.
from common                       import webutils, utils;
from testmanager                  import config;
from testmanager.core.base        import ModelDataBase, ModelLogicBase, TMExceptionBase;
from testmanager.core.db          import TMDatabaseConnection;
from testmanager.core.systemlog   import SystemLogLogic, SystemLogData;
from testmanager.core.useraccount import UserAccountLogic

# Python 3 hacks:
if sys.version_info[0] >= 3:
    unicode = str;  # pylint: disable=redefined-builtin,invalid-name
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class WuiException(TMExceptionBase):
    """
    For exceptions raised by Web UI code.
    """
    pass;                               # pylint: disable=unnecessary-pass


class WuiDispatcherBase(object):
    """
    Base class for the Web User Interface (WUI) dispatchers.

    The dispatcher class defines the basics of the page (like base template,
    menu items, action).  It is also responsible for parsing requests and
    dispatching them to action (POST) or/and content generators (GET+POST).
    The content returned by the generator is merged into the template and sent
    back to the webserver glue.
    """

    ## @todo possible that this should all go into presentation.

    ## The action parameter.
    ksParamAction       = 'Action';
    ## The name of the default action.
    ksActionDefault     = 'default';

    ## The name of the current page number parameter used when displaying lists.
    ksParamPageNo       = 'PageNo';
    ## The name of the page length (list items) parameter when displaying lists.
    ksParamItemsPerPage = 'ItemsPerPage';

    ## The name of the effective date (timestamp) parameter.
    ksParamEffectiveDate = 'EffectiveDate';

    ## The name of the redirect-to (test manager relative url) parameter.
    ksParamRedirectTo    = 'RedirectTo';

    ## The name of the list-action parameter (WuiListContentWithActionBase).
    ksParamListAction    = 'ListAction';

    ## One or more columns to sort by.
    ksParamSortColumns   = 'SortBy';

    ## The name of the change log enabled/disabled parameter.
    ksParamChangeLogEnabled         = 'ChangeLogEnabled';
    ## The name of the parmaeter indicating the change log page number.
    ksParamChangeLogPageNo          = 'ChangeLogPageNo';
    ## The name of the parameter indicate number of change log entries per page.
    ksParamChangeLogEntriesPerPage  = 'ChangeLogEntriesPerPage';
    ## The change log related parameters.
    kasChangeLogParams = (ksParamChangeLogEnabled, ksParamChangeLogPageNo, ksParamChangeLogEntriesPerPage,);

    ## @name Dispatcher debugging parameters.
    ## {@
    ksParamDbgSqlTrace      = 'DbgSqlTrace';
    ksParamDbgSqlExplain    = 'DbgSqlExplain';
    ## List of all debugging parameters.
    kasDbgParams = (ksParamDbgSqlTrace, ksParamDbgSqlExplain,);
    ## @}

    ## Special action return code for skipping _generatePage. Useful for
    # download pages and the like that messes with the HTTP header and more.
    ksDispatchRcAllDone = 'Done - Page has been rendered already';


    def __init__(self, oSrvGlue, sScriptName):
        self._oSrvGlue          = oSrvGlue;
        self._oDb               = TMDatabaseConnection(self.dprint if config.g_kfWebUiSqlDebug else None, oSrvGlue = oSrvGlue);
        self._tsNow             = None;  # Set by getEffectiveDateParam.
        self._asCheckedParams   = [];
        self._dParams           = None;  # Set by dispatchRequest.
        self._sAction           = None;  # Set by dispatchRequest.
        self._dDispatch         = { self.ksActionDefault: self._actionDefault, };

        # Template bits.
        self._sTemplate         = 'template-default.html';
        self._sPageTitle        = '$$TODO$$';   # The page title.
        self._aaoMenus          = [];           # List of [sName, sLink, [ [sSideName, sLink], .. ] tuples.
        self._sPageFilter       = '';           # The filter controls (optional).
        self._sPageBody         = '$$TODO$$';   # The body text.
        self._dSideMenuFormAttrs = {};          # key/value with attributes for the side menu <form> tag.
        self._sRedirectTo       = None;
        self._sDebug            = '';

        # Debugger bits.
        self._fDbgSqlTrace      = False;
        self._fDbgSqlExplain    = False;
        self._dDbgParams        = {};
        for sKey, sValue in oSrvGlue.getParameters().items():
            if sKey in self.kasDbgParams:
                self._dDbgParams[sKey] = sValue;
        if self._dDbgParams:
            from testmanager.webui.wuicontentbase import WuiTmLink;
            WuiTmLink.kdDbgParams = self._dDbgParams;

        # Determine currently logged in user credentials
        self._oCurUser          = UserAccountLogic(self._oDb).tryFetchAccountByLoginName(oSrvGlue.getLoginName());

        # Calc a couple of URL base strings for this dispatcher.
        self._sUrlBase          = sScriptName + '?';
        if self._dDbgParams:
            self._sUrlBase     += webutils.encodeUrlParams(self._dDbgParams) + '&';
        self._sActionUrlBase    = self._sUrlBase + self.ksParamAction + '=';


    def _redirectPage(self):
        """
        Redirects the page to the URL given in self._sRedirectTo.
        """
        assert self._sRedirectTo;
        assert self._sPageBody is None;
        assert self._sPageTitle is None;

        self._oSrvGlue.setRedirect(self._sRedirectTo);
        return True;

    def _isMenuMatch(self, sMenuUrl, sActionParam):
        """ Overridable menu matcher. """
        return sMenuUrl is not None and sMenuUrl.find(sActionParam) > 0;

    def _isSideMenuMatch(self, sSideMenuUrl, sActionParam):
        """ Overridable side menu matcher. """
        return sSideMenuUrl is not None and sSideMenuUrl.find(sActionParam) > 0;

    def _generateMenus(self):
        """
        Generates the two menus, returning them as (sTopMenuItems, sSideMenuItems).
        """
        fReadOnly = self.isReadOnlyUser();

        #
        # We use the action to locate the side menu.
        #
        aasSideMenu = None;
        for cchAction in range(len(self._sAction), 1, -1):
            sActionParam = '%s=%s' % (self.ksParamAction, self._sAction[:cchAction]);
            for aoItem in self._aaoMenus:
                if self._isMenuMatch(aoItem[1], sActionParam):
                    aasSideMenu = aoItem[2];
                    break;
                for asSubItem in aoItem[2]:
                    if self._isMenuMatch(asSubItem[1], sActionParam):
                        aasSideMenu = aoItem[2];
                        break;
                if aasSideMenu is not None:
                    break;

        #
        # Top menu first.
        #
        sTopMenuItems = '';
        for aoItem in self._aaoMenus:
            if aasSideMenu is aoItem[2]:
                sTopMenuItems += '<li class="current_page_item">';
            else:
                sTopMenuItems += '<li>';
            sTopMenuItems += '<a href="' + webutils.escapeAttr(aoItem[1]) + '">' \
                           + webutils.escapeElem(aoItem[0]) + '</a></li>\n';

        #
        # Side menu (if found).
        #
        sActionParam = '%s=%s' % (self.ksParamAction, self._sAction);
        sSideMenuItems = '';
        if aasSideMenu is not None:
            for asSubItem in aasSideMenu:
                if asSubItem[1] is not None:
                    if not asSubItem[2] or not fReadOnly:
                        if self._isSideMenuMatch(asSubItem[1], sActionParam):
                            sSideMenuItems += '<li class="current_page_item">';
                        else:
                            sSideMenuItems += '<li>';
                        sSideMenuItems += '<a href="' + webutils.escapeAttr(asSubItem[1]) + '">' \
                                        + webutils.escapeElem(asSubItem[0]) + '</a></li>\n';
                else:
                    sSideMenuItems += '<li class="subheader_item">' + webutils.escapeElem(asSubItem[0]) + '</li>';
        return (sTopMenuItems, sSideMenuItems);

    def _generatePage(self):
        """
        Generates the page using _sTemplate, _sPageTitle, _aaoMenus, and _sPageBody.
        """
        assert self._sRedirectTo is None;

        #
        # Build the replacement string dictionary.
        #

        # Provide basic auth log out for browsers that supports it.
        sUserAgent = self._oSrvGlue.getUserAgent();
        if sUserAgent.startswith('Mozilla/') and sUserAgent.find('Firefox') > 0:
            # Log in as the logout user in the same realm, the browser forgets
            # the old login and the job is done. (see apache sample conf)
            sLogOut = ' (<a href="%s://logout:logout@%s%slogout.py">logout</a>)' \
                % (self._oSrvGlue.getUrlScheme(), self._oSrvGlue.getUrlNetLoc(), self._oSrvGlue.getUrlBasePath());
        elif sUserAgent.startswith('Mozilla/') and sUserAgent.find('Safari') > 0:
            # For a 401, causing the browser to forget the old login. Works
            # with safari as well as the two above. Since safari consider the
            # above method a phishing attempt and displays a warning to that
            # effect, which when taken seriously aborts the logout, this method
            # is preferable, even if it throws logon boxes in the user's face
            # till he/she/it hits escape, because it always works.
            sLogOut = ' (<a href="logout2.py">logout</a>)'
        elif (sUserAgent.startswith('Mozilla/') and sUserAgent.find('MSIE') > 0) \
          or (sUserAgent.startswith('Mozilla/') and sUserAgent.find('Chrome') > 0):
            ## There doesn't seem to be any way to make IE really log out
            # without using a cookie and systematically 401 accesses based on
            # some logout state associated with it.  Not sure how secure that
            # can be made and we really want to avoid cookies.  So, perhaps,
            # just avoid IE for now. :-)
            ## Chrome/21.0 doesn't want to log out either.
            sLogOut = ''
        else:
            sLogOut = ''

        # Prep Menus.
        (sTopMenuItems, sSideMenuItems) = self._generateMenus();

        # The dictionary (max variable length is 28 chars (see further down)).
        dReplacements = {
            '@@PAGE_TITLE@@':           self._sPageTitle,
            '@@LOG_OUT@@':              sLogOut,
            '@@TESTMANAGER_VERSION@@':  config.g_ksVersion,
            '@@TESTMANAGER_REVISION@@': config.g_ksRevision,
            '@@BASE_URL@@':             self._oSrvGlue.getBaseUrl(),
            '@@TOP_MENU_ITEMS@@':       sTopMenuItems,
            '@@SIDE_MENU_ITEMS@@':      sSideMenuItems,
            '@@SIDE_FILTER_CONTROL@@':  self._sPageFilter,
            '@@SIDE_MENU_FORM_ATTRS@@': '',
            '@@PAGE_BODY@@':            self._sPageBody,
            '@@DEBUG@@':                '',
        };

        # Side menu form attributes.
        if self._dSideMenuFormAttrs:
            dReplacements['@@SIDE_MENU_FORM_ATTRS@@'] = ' '.join(['%s="%s"' % (sKey, webutils.escapeAttr(sValue))
                                                                  for sKey, sValue in self._dSideMenuFormAttrs.items()]);

        # Special current user handling.
        if self._oCurUser is not None:
            dReplacements['@@USER_NAME@@'] = self._oCurUser.sUsername;
        else:
            dReplacements['@@USER_NAME@@'] = 'unauthorized user "' + self._oSrvGlue.getLoginName() + '"';

        # Prep debug section.
        if self._sDebug == '':
            if config.g_kfWebUiSqlTrace or self._fDbgSqlTrace or self._fDbgSqlExplain:
                self._sDebug  = '<h3>Processed in %s ns.</h3>\n%s\n' \
                              % ( utils.formatNumber(utils.timestampNano() - self._oSrvGlue.tsStart,),
                                  self._oDb.debugHtmlReport(self._oSrvGlue.tsStart));
            elif config.g_kfWebUiProcessedIn:
                self._sDebug  = '<h3>Processed in %s ns.</h3>\n' \
                              % ( utils.formatNumber(utils.timestampNano() - self._oSrvGlue.tsStart,), );
            if config.g_kfWebUiDebugPanel:
                self._sDebug += self._debugRenderPanel();
        if self._sDebug != '':
            dReplacements['@@DEBUG@@'] = u'<div id="debug"><br><br><hr/>' \
                                       + (utils.toUnicode(self._sDebug, errors='ignore') if isinstance(self._sDebug, str)
                                          else self._sDebug) \
                                       + u'</div>\n';

        #
        # Load the template.
        #
        with open(os.path.join(self._oSrvGlue.pathTmWebUI(), self._sTemplate)) as oFile: # pylint: disable=unspecified-encoding
            sTmpl = oFile.read();

        #
        # Process the template, outputting each part we process.
        #
        offStart = 0;
        offCur   = 0;
        while offCur < len(sTmpl):
            # Look for a replacement variable.
            offAtAt = sTmpl.find('@@', offCur);
            if offAtAt < 0:
                break;
            offCur = offAtAt + 2;
            if sTmpl[offCur] not in string.ascii_uppercase:
                continue;
            offEnd = sTmpl.find('@@', offCur, offCur+28);
            if offEnd <= 0:
                continue;
            offCur = offEnd;
            sReplacement = sTmpl[offAtAt:offEnd+2];
            if sReplacement in dReplacements:
                # Got a match! Write out the previous chunk followed by the replacement text.
                if offStart < offAtAt:
                    self._oSrvGlue.write(sTmpl[offStart:offAtAt]);
                self._oSrvGlue.write(dReplacements[sReplacement]);
                # Advance past the replacement point in the template.
                offCur += 2;
                offStart = offCur;
            else:
                assert False, 'Unknown replacement "%s" at offset %s in %s' % (sReplacement, offAtAt, self._sTemplate );

        # The final chunk.
        if offStart < len(sTmpl):
            self._oSrvGlue.write(sTmpl[offStart:]);

        return True;

    #
    # Interface for WuiContentBase classes.
    #

    def getUrlNoParams(self):
        """
        Returns the base URL without any parameters (no trailing '?' or &).
        """
        return self._sUrlBase[:self._sUrlBase.rindex('?')];

    def getUrlBase(self):
        """
        Returns the base URL, ending with '?' or '&'.
        This may already include some debug parameters.
        """
        return self._sUrlBase;

    def getParameters(self):
        """
        Returns a (shallow) copy of the request parameter dictionary.
        """
        return self._dParams.copy();

    def getDb(self):
        """
        Returns the database connection.
        """
        return self._oDb;

    def getNow(self):
        """
        Returns the effective date.
        """
        return self._tsNow;


    #
    # Parameter handling.
    #

    def getStringParam(self, sName, asValidValues = None, sDefault = None, fAllowNull = False):
        """
        Gets a string parameter.
        Raises exception if not found and sDefault is None.
        """
        if sName in self._dParams:
            if sName not in self._asCheckedParams:
                self._asCheckedParams.append(sName);
            sValue = self._dParams[sName];
            if isinstance(sValue, list):
                raise WuiException('%s parameter "%s" is given multiple times: "%s"' % (self._sAction, sName, sValue));
            sValue = sValue.strip();
        elif sDefault is None and fAllowNull is not True:
            raise WuiException('%s is missing parameters: "%s"' % (self._sAction, sName,));
        else:
            sValue = sDefault;

        if asValidValues is not None and sValue not in asValidValues:
            raise WuiException('%s parameter %s value "%s" not in %s '
                               % (self._sAction, sName, sValue, asValidValues));
        return sValue;

    def getBoolParam(self, sName, fDefault = None):
        """
        Gets a boolean parameter.
        Raises exception if not found and fDefault is None, or if not a valid boolean.
        """
        sValue = self.getStringParam(sName, [ 'True', 'true', '1', 'False', 'false', '0'],
                                     '0' if fDefault is None else str(fDefault));
        # HACK: Checkboxes doesn't return a value when unchecked, so we always
        #       provide a default when dealing with boolean parameters.
        return sValue in ('True', 'true', '1',);

    def getIntParam(self, sName, iMin = None, iMax = None, iDefault = None):
        """
        Gets a integer parameter.
        Raises exception if not found and iDefault is None, if not a valid int,
        or if outside the range defined by iMin and iMax.
        """
        if iDefault is not None and sName not in self._dParams:
            return iDefault;

        sValue = self.getStringParam(sName, None, None if iDefault is None else str(iDefault));
        try:
            iValue = int(sValue);
        except:
            raise WuiException('%s parameter %s value "%s" cannot be convert to an integer'
                               % (self._sAction, sName, sValue));

        if   (iMin is not None and iValue < iMin) \
          or (iMax is not None and iValue > iMax):
            raise WuiException('%s parameter %s value %d is out of range [%s..%s]'
                               % (self._sAction, sName, iValue, iMin, iMax));
        return iValue;

    def getLongParam(self, sName, lMin = None, lMax = None, lDefault = None):
        """
        Gets a long integer parameter.
        Raises exception if not found and lDefault is None, if not a valid long,
        or if outside the range defined by lMin and lMax.
        """
        if lDefault is not None and sName not in self._dParams:
            return lDefault;

        sValue = self.getStringParam(sName, None, None if lDefault is None else str(lDefault));
        try:
            lValue = long(sValue);
        except:
            raise WuiException('%s parameter %s value "%s" cannot be convert to an integer'
                               % (self._sAction, sName, sValue));

        if   (lMin is not None and lValue < lMin) \
          or (lMax is not None and lValue > lMax):
            raise WuiException('%s parameter %s value %d is out of range [%s..%s]'
                               % (self._sAction, sName, lValue, lMin, lMax));
        return lValue;

    def getTsParam(self, sName, tsDefault = None, fRequired = True):
        """
        Gets a timestamp parameter.
        Raises exception if not found and fRequired is True.
        """
        if fRequired is False and sName not in self._dParams:
            return tsDefault;

        sValue = self.getStringParam(sName, None, None if tsDefault is None else str(tsDefault));
        (sValue, sError) = ModelDataBase.validateTs(sValue);
        if sError is not None:
            raise WuiException('%s parameter %s value "%s": %s'
                               % (self._sAction, sName, sValue, sError));
        return sValue;

    def getListOfIntParams(self, sName, iMin = None, iMax = None, aiDefaults = None):
        """
        Gets parameter list.
        Raises exception if not found and aiDefaults is None, or if any of the
        values are not valid integers or outside the range defined by iMin and iMax.
        """
        if sName in self._dParams:
            if sName not in self._asCheckedParams:
                self._asCheckedParams.append(sName);

            if isinstance(self._dParams[sName], list):
                asValues = self._dParams[sName];
            else:
                asValues = [self._dParams[sName],];
            aiValues = [];
            for sValue in asValues:
                try:
                    iValue = int(sValue);
                except:
                    raise WuiException('%s parameter %s value "%s" cannot be convert to an integer'
                                       % (self._sAction, sName, sValue));

                if   (iMin is not None and iValue < iMin) \
                  or (iMax is not None and iValue > iMax):
                    raise WuiException('%s parameter %s value %d is out of range [%s..%s]'
                                       % (self._sAction, sName, iValue, iMin, iMax));
                aiValues.append(iValue);
        else:
            aiValues = aiDefaults;

        return aiValues;

    def getListOfStrParams(self, sName, asDefaults = None):
        """
        Gets parameter list.
        Raises exception if not found and asDefaults is None.
        """
        if sName in self._dParams:
            if sName not in self._asCheckedParams:
                self._asCheckedParams.append(sName);

            if isinstance(self._dParams[sName], list):
                asValues = [str(s).strip() for s in self._dParams[sName]];
            else:
                asValues = [str(self._dParams[sName]).strip(), ];
        elif asDefaults is None:
            raise WuiException('%s is missing parameters: "%s"' % (self._sAction, sName,));
        else:
            asValues = asDefaults;

        return asValues;

    def getListOfTestCasesParam(self, sName, asDefaults = None):  # too many local vars - pylint: disable=too-many-locals
        """Get list of test cases and their parameters"""
        if sName in self._dParams:
            if sName not in self._asCheckedParams:
                self._asCheckedParams.append(sName)

        aoListOfTestCases = []

        aiSelectedTestCaseIds = self.getListOfIntParams('%s[asCheckedTestCases]' % sName, aiDefaults=[])
        aiAllTestCases        = self.getListOfIntParams('%s[asAllTestCases]'     % sName, aiDefaults=[])

        for idTestCase in aiAllTestCases:
            aiCheckedTestCaseArgs = \
                self.getListOfIntParams(
                    '%s[%d][asCheckedTestCaseArgs]' % (sName, idTestCase),
                    aiDefaults=[])

            aiAllTestCaseArgs = \
                self.getListOfIntParams(
                    '%s[%d][asAllTestCaseArgs]' % (sName, idTestCase),
                    aiDefaults=[])

            oListEntryTestCaseArgs = []
            for idTestCaseArgs in aiAllTestCaseArgs:
                fArgsChecked   = idTestCaseArgs in aiCheckedTestCaseArgs;

                # Dry run
                sPrefix = '%s[%d][%d]' % (sName, idTestCase, idTestCaseArgs,);
                self.getIntParam(sPrefix + '[idTestCaseArgs]', iDefault = -1,)

                sArgs        = self.getStringParam(sPrefix + '[sArgs]',        sDefault = '')
                cSecTimeout  = self.getStringParam(sPrefix + '[cSecTimeout]',  sDefault = '')
                cGangMembers = self.getStringParam(sPrefix + '[cGangMembers]', sDefault = '')
                cGangMembers = self.getStringParam(sPrefix + '[cGangMembers]', sDefault = '')

                oListEntryTestCaseArgs.append((fArgsChecked, idTestCaseArgs, sArgs, cSecTimeout, cGangMembers))

            sTestCaseName = self.getStringParam('%s[%d][sName]' % (sName, idTestCase), sDefault='')

            oListEntryTestCase = (
                idTestCase,
                idTestCase in aiSelectedTestCaseIds,
                sTestCaseName,
                oListEntryTestCaseArgs
            );

            aoListOfTestCases.append(oListEntryTestCase)

        if not aoListOfTestCases:
            if asDefaults is None:
                raise WuiException('%s is missing parameters: "%s"' % (self._sAction, sName))
            aoListOfTestCases = asDefaults

        return aoListOfTestCases

    def getEffectiveDateParam(self, sParamName = None):
        """
        Gets the effective date parameter.

        Returns a timestamp suitable for database and url parameters.
        Returns None if not found or empty.

        The first call with sParamName set to None will set the internal _tsNow
        value upon successfull return.
        """

        sName = sParamName if sParamName is not None else WuiDispatcherBase.ksParamEffectiveDate

        if sName not in self._dParams:
            return None;

        if sName not in self._asCheckedParams:
            self._asCheckedParams.append(sName);

        sValue = self._dParams[sName];
        if isinstance(sValue, list):
            raise WuiException('%s parameter "%s" is given multiple times: %s' % (self._sAction, sName, sValue));
        sValue = sValue.strip();
        if sValue == '':
            return None;

        #
        # Timestamp, just validate it and return.
        #
        if sValue[0] not in ['-', '+']:
            (sValue, sError) = ModelDataBase.validateTs(sValue);
            if sError is not None:
                raise WuiException('%s parameter "%s" ("%s") is invalid: %s' % (self._sAction, sName, sValue, sError));
            if sParamName is None and self._tsNow is None:
                self._tsNow = sValue;
            return sValue;

        #
        # Relative timestamp. Validate and convert it to a fixed timestamp.
        #
        chSign = sValue[0];
        (sValue, sError) = ModelDataBase.validateTs(sValue[1:], fRelative = True);
        if sError is not None:
            raise WuiException('%s parameter "%s" ("%s") is invalid: %s' % (self._sAction, sName, sValue, sError));
        if sValue[-6] in ['-', '+']:
            raise WuiException('%s parameter "%s" ("%s") is a relative timestamp but incorrectly includes a time zone.'
                               % (self._sAction, sName, sValue));
        offTime = 11;
        if sValue[offTime - 1] != ' ':
            raise WuiException('%s parameter "%s" ("%s") incorrect format.' % (self._sAction, sName, sValue));
        sInterval = 'P' + sValue[:(offTime - 1)] + 'T' + sValue[offTime:];

        self._oDb.execute('SELECT CURRENT_TIMESTAMP ' + chSign + ' \'' + sInterval + '\'::INTERVAL');
        oDate = self._oDb.fetchOne()[0];

        sValue = str(oDate);
        if sParamName is None and self._tsNow is None:
            self._tsNow = sValue;
        return sValue;

    def getRedirectToParameter(self, sDefault = None):
        """
        Gets the special redirect to parameter if it exists, will Return default
        if not, with None being a valid default.

        Makes sure the it doesn't got offsite.
        Raises exception if invalid.
        """
        if sDefault is not None or self.ksParamRedirectTo in self._dParams:
            sValue = self.getStringParam(self.ksParamRedirectTo, sDefault = sDefault);
            cch = sValue.find("?");
            if cch < 0:
                cch = sValue.find("#");
                if cch < 0:
                    cch = len(sValue);
            for ch in (':', '/', '\\', '..'):
                if sValue.find(ch, 0, cch) >= 0:
                    raise WuiException('Invalid character (%c) in redirect-to url: %s' % (ch, sValue,));
        else:
            sValue = None;
        return sValue;


    def _checkForUnknownParameters(self):
        """
        Check if we've handled all parameters, raises exception if anything
        unknown was found.
        """

        if len(self._asCheckedParams) != len(self._dParams):
            sUnknownParams = '';
            for sKey in self._dParams:
                if sKey not in self._asCheckedParams:
                    sUnknownParams += ' ' + sKey + '=' + str(self._dParams[sKey]);
            raise WuiException('Unknown parameters: ' + sUnknownParams);

        return True;

    def _assertPostRequest(self):
        """
        Makes sure that the request we're dispatching is a POST request.
        Raises an exception of not.
        """
        if self._oSrvGlue.getMethod() != 'POST':
            raise WuiException('Expected "POST" request, got "%s"' % (self._oSrvGlue.getMethod(),))
        return True;

    #
    # Client browser type.
    #

    ## @name Browser types.
    ## @{
    ksBrowserFamily_Unknown     = 0;
    ksBrowserFamily_Gecko       = 1;
    ksBrowserFamily_Webkit      = 2;
    ksBrowserFamily_Trident     = 3;
    ## @}

    ## @name Browser types.
    ## @{
    ksBrowserType_FamilyMask    = 0xff;
    ksBrowserType_Unknown       = 0;
    ksBrowserType_Firefox       = (1 << 8) | ksBrowserFamily_Gecko;
    ksBrowserType_Chrome        = (2 << 8) | ksBrowserFamily_Webkit;
    ksBrowserType_Safari        = (3 << 8) | ksBrowserFamily_Webkit;
    ksBrowserType_IE            = (4 << 8) | ksBrowserFamily_Trident;
    ## @}

    def getBrowserType(self):
        """
        Gets the browser type.
        The browser family can be extracted from this using ksBrowserType_FamilyMask.
        """
        sAgent = self._oSrvGlue.getUserAgent();
        if sAgent.find('AppleWebKit/') > 0:
            if sAgent.find('Chrome/') > 0:
                return self.ksBrowserType_Chrome;
            if sAgent.find('Safari/') > 0:
                return self.ksBrowserType_Safari;
            return self.ksBrowserType_Unknown | self.ksBrowserFamily_Webkit;
        if sAgent.find('Gecko/') > 0:
            if sAgent.find('Firefox/') > 0:
                return self.ksBrowserType_Firefox;
            return self.ksBrowserType_Unknown | self.ksBrowserFamily_Gecko;
        return self.ksBrowserType_Unknown | self.ksBrowserFamily_Unknown;

    def isBrowserGecko(self, sMinVersion = None):
        """ Returns true if it's a gecko based browser. """
        if (self.getBrowserType() & self.ksBrowserType_FamilyMask) != self.ksBrowserFamily_Gecko:
            return False;
        if sMinVersion is not None:
            sAgent = self._oSrvGlue.getUserAgent();
            sVersion = sAgent[sAgent.find('Gecko/')+6:].split()[0];
            if sVersion < sMinVersion:
                return False;
        return True;

    #
    # User related stuff.
    #

    def isReadOnlyUser(self):
        """ Returns true if the logged in user is read-only or if no user is logged in. """
        return self._oCurUser is None or self._oCurUser.fReadOnly;


    #
    # Debugging
    #

    def _debugProcessDispatch(self):
        """
        Processes any debugging parameters in the request and adds them to
        _asCheckedParams so they won't cause trouble in the action handler.
        """

        self._fDbgSqlTrace   = self.getBoolParam(self.ksParamDbgSqlTrace, False);
        self._fDbgSqlExplain = self.getBoolParam(self.ksParamDbgSqlExplain, False);

        if self._fDbgSqlExplain:
            self._oDb.debugEnableExplain();

        return True;

    def _debugRenderPanel(self):
        """
        Renders a simple form for controlling WUI debugging.

        Returns the HTML for it.
        """

        sHtml  = '<div id="debug-panel">\n' \
                 ' <form id="debug-panel-form" method="get" action="#">\n';

        for sKey, oValue in self._dParams.items():
            if sKey not in self.kasDbgParams:
                if hasattr(oValue, 'startswith'):
                    sHtml += '  <input type="hidden" name="%s" value="%s"/>\n' \
                           % (webutils.escapeAttr(sKey), webutils.escapeAttrToStr(oValue),);
                else:
                    for oSubValue in oValue:
                        sHtml += '  <input type="hidden" name="%s" value="%s"/>\n' \
                               % (webutils.escapeAttr(sKey), webutils.escapeAttrToStr(oSubValue),);

        for aoCheckBox in (
                [self.ksParamDbgSqlTrace, self._fDbgSqlTrace, 'SQL trace'],
                [self.ksParamDbgSqlExplain, self._fDbgSqlExplain, 'SQL explain'], ):
            sHtml += ' <input type="checkbox" name="%s" value="1"%s />%s\n' \
                % (aoCheckBox[0], ' checked' if aoCheckBox[1] else '', aoCheckBox[2]);

        sHtml += '  <button type="submit">Apply</button>\n';
        sHtml += ' </form>\n' \
                 '</div>\n';
        return sHtml;


    def _debugGetParameters(self):
        """
        Gets a dictionary with the debug parameters.

        For use when links are constructed from scratch instead of self._dParams.
        """
        return self._dDbgParams;

    #
    # Dispatching
    #

    def _actionDefault(self):
        """The default action handler, always overridden. """
        raise WuiException('The child class shall override WuiBase.actionDefault().')

    def _actionGenericListing(self, oLogicType, oListContentType):
        """
        Generic listing action.

        oLogicType implements fetchForListing.
        oListContentType is a child of WuiListContentBase.
        """
        tsEffective     = self.getEffectiveDateParam();
        cItemsPerPage   = self.getIntParam(self.ksParamItemsPerPage, iMin = 2, iMax =   9999, iDefault = 384);
        iPage           = self.getIntParam(self.ksParamPageNo,       iMin = 0, iMax = 999999, iDefault = 0);
        aiSortColumnsDup = self.getListOfIntParams(self.ksParamSortColumns,
                                                   iMin = -getattr(oLogicType, 'kcMaxSortColumns', 0) + 1,
                                                   iMax = getattr(oLogicType, 'kcMaxSortColumns', 0), aiDefaults = []);
        aiSortColumns   = [];
        for iSortColumn in aiSortColumnsDup:
            if iSortColumn not in aiSortColumns:
                aiSortColumns.append(iSortColumn);
        self._checkForUnknownParameters();

        ## @todo fetchForListing could be made more useful if it returned a tuple
        # that includes the total number of entries, thus making paging more user
        # friendly (known number of pages).  So, the return should be:
        #       (aoEntries, cAvailableEntries)
        #
        # In addition, we could add a new parameter to include deleted entries,
        # making it easier to find old deleted testboxes/testcases/whatever and
        # clone them back to life.  The temporal navigation is pretty usless here.
        #
        aoEntries  = oLogicType(self._oDb).fetchForListing(iPage * cItemsPerPage, cItemsPerPage + 1, tsEffective, aiSortColumns);
        oContent   = oListContentType(aoEntries, iPage, cItemsPerPage, tsEffective,
                                      fnDPrint = self._oSrvGlue.dprint, oDisp = self, aiSelectedSortColumns = aiSortColumns);
        (self._sPageTitle, self._sPageBody) = oContent.show();
        return True;

    def _actionGenericFormAdd(self, oDataType, oFormType, sRedirectTo = None):
        """
        Generic add something form display request handler.

        oDataType is a ModelDataBase child class.
        oFormType is a WuiFormContentBase child class.
        """
        assert issubclass(oDataType, ModelDataBase);
        from testmanager.webui.wuicontentbase import WuiFormContentBase;
        assert issubclass(oFormType, WuiFormContentBase);

        oData = oDataType().initFromParams(oDisp = self, fStrict = False);
        sRedirectTo = self.getRedirectToParameter(sRedirectTo);
        self._checkForUnknownParameters();

        oForm = oFormType(oData, oFormType.ksMode_Add, oDisp = self);
        oForm.setRedirectTo(sRedirectTo);
        (self._sPageTitle, self._sPageBody) = oForm.showForm();
        return True

    def _actionGenericFormDetails(self, oDataType, oLogicType, oFormType, sIdAttr = None, sGenIdAttr = None): # pylint: disable=too-many-locals
        """
        Generic handler for showing a details form/page.

        oDataType is a ModelDataBase child class.
        oLogicType may implement fetchForChangeLog.
        oFormType is a WuiFormContentBase child class.
        sIdParamName is the name of the ID parameter (not idGen!).
        """
        # Input.
        assert issubclass(oDataType, ModelDataBase);
        assert issubclass(oLogicType, ModelLogicBase);
        from testmanager.webui.wuicontentbase import WuiFormContentBase;
        assert issubclass(oFormType, WuiFormContentBase);

        if sIdAttr is None:
            sIdAttr = oDataType.ksIdAttr;
        if sGenIdAttr is None:
            sGenIdAttr = getattr(oDataType, 'ksGenIdAttr', None);

        # Parameters.
        idGenObject = -1;
        if sGenIdAttr is not None:
            idGenObject = self.getIntParam(getattr(oDataType, 'ksParam_' + sGenIdAttr), 0, 0x7ffffffe, -1);
        if idGenObject != -1:
            idObject = tsNow = None;
        else:
            idObject = self.getIntParam(getattr(oDataType, 'ksParam_' + sIdAttr), 0, 0x7ffffffe, -1);
            tsNow    = self.getEffectiveDateParam();
        fChangeLog               = self.getBoolParam(WuiDispatcherBase.ksParamChangeLogEnabled, True);
        iChangeLogPageNo         = self.getIntParam(WuiDispatcherBase.ksParamChangeLogPageNo, 0, 9999, 0);
        cChangeLogEntriesPerPage = self.getIntParam(WuiDispatcherBase.ksParamChangeLogEntriesPerPage, 2, 9999, 4);
        self._checkForUnknownParameters();

        # Fetch item and display it.
        if idGenObject == -1:
            oData = oDataType().initFromDbWithId(self._oDb, idObject, tsNow);
        else:
            oData = oDataType().initFromDbWithGenId(self._oDb, idGenObject);

        oContent  = oFormType(oData, oFormType.ksMode_Show, oDisp = self);
        (self._sPageTitle, self._sPageBody) = oContent.showForm();

        # Add change log if supported.
        if fChangeLog and hasattr(oLogicType, 'fetchForChangeLog'):
            (aoEntries, fMore) = oLogicType(self._oDb).fetchForChangeLog(getattr(oData, sIdAttr),
                                                                         iChangeLogPageNo * cChangeLogEntriesPerPage,
                                                                         cChangeLogEntriesPerPage ,
                                                                         tsNow);
            self._sPageBody += oContent.showChangeLog(aoEntries, fMore, iChangeLogPageNo, cChangeLogEntriesPerPage, tsNow);
        return True

    def _actionGenericDoRemove(self, oLogicType, sParamId, sRedirAction):
        """
        Delete entry (using oLogicType.removeEntry).

        oLogicType is a class that implements addEntry.

        sParamId is the name (ksParam_...) of the HTTP variable hold the ID of
        the database entry to delete.

        sRedirAction is what action to redirect to on success.
        """
        import cgitb;   # pylint: disable=deprecated-module ## @todo these will be retired in python 3.13!

        idEntry = self.getIntParam(sParamId, iMin = 1, iMax = 0x7ffffffe)
        fCascade = self.getBoolParam('fCascadeDelete', False);
        sRedirectTo = self.getRedirectToParameter(self._sActionUrlBase + sRedirAction);
        self._checkForUnknownParameters()

        try:
            if self.isReadOnlyUser():
                raise Exception('"%s" is a read only user!' % (self._oCurUser.sUsername,));
            self._sPageTitle  = None
            self._sPageBody   = None
            self._sRedirectTo = sRedirectTo;
            return oLogicType(self._oDb).removeEntry(self._oCurUser.uid, idEntry, fCascade = fCascade, fCommit = True);
        except Exception as oXcpt:
            self._oDb.rollback();
            self._sPageTitle  = 'Unable to delete entry';
            self._sPageBody   = str(oXcpt);
            if config.g_kfDebugDbXcpt:
                self._sPageBody += cgitb.html(sys.exc_info());
            self._sRedirectTo = None;
        return False;

    def _actionGenericFormEdit(self, oDataType, oFormType, sIdParamName = None, sRedirectTo = None):
        """
        Generic edit something form display request handler.

        oDataType is a ModelDataBase child class.
        oFormType is a WuiFormContentBase child class.
        sIdParamName is the name of the ID parameter (not idGen!).
        """
        assert issubclass(oDataType, ModelDataBase);
        from testmanager.webui.wuicontentbase import WuiFormContentBase;
        assert issubclass(oFormType, WuiFormContentBase);

        if sIdParamName is None:
            sIdParamName = getattr(oDataType, 'ksParam_' + oDataType.ksIdAttr);
        assert len(sIdParamName) > 1;

        tsNow    = self.getEffectiveDateParam();
        idObject = self.getIntParam(sIdParamName, 0, 0x7ffffffe);
        sRedirectTo = self.getRedirectToParameter(sRedirectTo);
        self._checkForUnknownParameters();
        oData = oDataType().initFromDbWithId(self._oDb, idObject, tsNow = tsNow);

        oContent = oFormType(oData, oFormType.ksMode_Edit, oDisp = self);
        oContent.setRedirectTo(sRedirectTo);
        (self._sPageTitle, self._sPageBody) = oContent.showForm();
        return True

    def _actionGenericFormEditL(self, oCoreObjectLogic, sCoreObjectIdFieldName, oWuiObjectLogic):
        """
        Generic modify something form display request handler.

        @param oCoreObjectLogic         A *Logic class

        @param sCoreObjectIdFieldName   Name of HTTP POST variable that
                                        contains object ID information

        @param oWuiObjectLogic          Web interface renderer class
        """

        iCoreDataObjectId = self.getIntParam(sCoreObjectIdFieldName,  0, 0x7ffffffe, -1)
        self._checkForUnknownParameters();

        ## @todo r=bird: This will return a None object if the object wasn't found... Crash bang in the content generator
        #                code (that's not logic code btw.).
        oData = oCoreObjectLogic(self._oDb).getById(iCoreDataObjectId)

        # Instantiate and render the MODIFY dialog form
        oContent = oWuiObjectLogic(oData, oWuiObjectLogic.ksMode_Edit, oDisp=self)

        (self._sPageTitle, self._sPageBody) = oContent.showForm()

        return True

    def _actionGenericFormClone(self, oDataType, oFormType, sIdAttr, sGenIdAttr = None):
        """
        Generic clone something form display request handler.

        oDataType is a ModelDataBase child class.
        oFormType is a WuiFormContentBase child class.
        sIdParamName is the name of the ID parameter.
        sGenIdParamName is the name of the generation ID parameter, None if not applicable.
        """
        # Input.
        assert issubclass(oDataType, ModelDataBase);
        from testmanager.webui.wuicontentbase import WuiFormContentBase;
        assert issubclass(oFormType, WuiFormContentBase);

        # Parameters.
        idGenObject = -1;
        if sGenIdAttr is not None:
            idGenObject = self.getIntParam(getattr(oDataType, 'ksParam_' + sGenIdAttr), 0, 0x7ffffffe, -1);
        if idGenObject != -1:
            idObject = tsNow = None;
        else:
            idObject = self.getIntParam(getattr(oDataType, 'ksParam_' + sIdAttr), 0, 0x7ffffffe, -1);
            tsNow    = self.getEffectiveDateParam();
        self._checkForUnknownParameters();

        # Fetch data and clear identifying attributes not relevant to the clone.
        if idGenObject != -1:
            oData = oDataType().initFromDbWithGenId(self._oDb, idGenObject);
        else:
            oData = oDataType().initFromDbWithId(self._oDb, idObject, tsNow);

        setattr(oData, sIdAttr, None);
        if sGenIdAttr is not None:
            setattr(oData, sGenIdAttr, None);
        oData.tsEffective = None;
        oData.tsExpire    = None;

        # Display form.
        oContent = oFormType(oData, oFormType.ksMode_Add, oDisp = self);
        (self._sPageTitle, self._sPageBody) = oContent.showForm()
        return True


    def _actionGenericFormPost(self, sMode, fnLogicAction, oDataType, oFormType, sRedirectTo, fStrict = True):
        """
        Generic POST request handling from a WuiFormContentBase child.

        oDataType is a ModelDataBase child class.
        oFormType is a WuiFormContentBase child class.
        fnLogicAction is a method taking a oDataType instance and uidAuthor as arguments.
        """
        assert issubclass(oDataType, ModelDataBase);
        from testmanager.webui.wuicontentbase import WuiFormContentBase;
        assert issubclass(oFormType, WuiFormContentBase);

        #
        # Read and validate parameters.
        #
        oData = oDataType().initFromParams(oDisp = self, fStrict = fStrict);
        sRedirectTo = self.getRedirectToParameter(sRedirectTo);
        self._checkForUnknownParameters();
        self._assertPostRequest();
        if sMode == WuiFormContentBase.ksMode_Add and  getattr(oData, 'kfIdAttrIsForForeign', False):
            enmValidateFor = oData.ksValidateFor_AddForeignId;
        elif sMode == WuiFormContentBase.ksMode_Add:
            enmValidateFor = oData.ksValidateFor_Add;
        else:
            enmValidateFor = oData.ksValidateFor_Edit;
        dErrors = oData.validateAndConvert(self._oDb, enmValidateFor);

        # Check that the user can do this.
        sErrorMsg = None;
        assert self._oCurUser is not None;
        if self.isReadOnlyUser():
            sErrorMsg = 'User %s is not allowed to modify anything!' % (self._oCurUser.sUsername,)

        if not dErrors and not sErrorMsg:
            oData.convertFromParamNull();

            #
            # Try do the job.
            #
            try:
                fnLogicAction(oData, self._oCurUser.uid, fCommit = True);
            except Exception as oXcpt:
                self._oDb.rollback();
                oForm = oFormType(oData, sMode, oDisp = self);
                oForm.setRedirectTo(sRedirectTo);
                sErrorMsg = str(oXcpt) if not config.g_kfDebugDbXcpt else '\n'.join(utils.getXcptInfo(4));
                (self._sPageTitle, self._sPageBody) = oForm.showForm(sErrorMsg = sErrorMsg);
            else:
                #
                # Worked, redirect to the specified page.
                #
                self._sPageTitle  = None;
                self._sPageBody   = None;
                self._sRedirectTo = sRedirectTo;
        else:
            oForm = oFormType(oData, sMode, oDisp = self);
            oForm.setRedirectTo(sRedirectTo);
            (self._sPageTitle, self._sPageBody) = oForm.showForm(dErrors = dErrors, sErrorMsg = sErrorMsg);
        return True;

    def _actionGenericFormAddPost(self, oDataType, oLogicType, oFormType, sRedirAction, fStrict=True):
        """
        Generic add entry POST request handling from a WuiFormContentBase child.

        oDataType is a ModelDataBase child class.
        oLogicType is a class that implements addEntry.
        oFormType is a WuiFormContentBase child class.
        sRedirAction is what action to redirect to on success.
        """
        assert issubclass(oDataType, ModelDataBase);
        assert issubclass(oLogicType, ModelLogicBase);
        from testmanager.webui.wuicontentbase import WuiFormContentBase;
        assert issubclass(oFormType, WuiFormContentBase);

        oLogic = oLogicType(self._oDb);
        return self._actionGenericFormPost(WuiFormContentBase.ksMode_Add, oLogic.addEntry, oDataType, oFormType,
                                           '?' + webutils.encodeUrlParams({self.ksParamAction: sRedirAction}), fStrict=fStrict)

    def _actionGenericFormEditPost(self, oDataType, oLogicType, oFormType, sRedirAction, fStrict = True):
        """
        Generic edit POST request handling from a WuiFormContentBase child.

        oDataType is a ModelDataBase child class.
        oLogicType is a class that implements addEntry.
        oFormType is a WuiFormContentBase child class.
        sRedirAction is what action to redirect to on success.
        """
        assert issubclass(oDataType, ModelDataBase);
        assert issubclass(oLogicType, ModelLogicBase);
        from testmanager.webui.wuicontentbase import WuiFormContentBase;
        assert issubclass(oFormType, WuiFormContentBase);

        oLogic = oLogicType(self._oDb);
        return self._actionGenericFormPost(WuiFormContentBase.ksMode_Edit, oLogic.editEntry, oDataType, oFormType,
                                           '?' + webutils.encodeUrlParams({self.ksParamAction: sRedirAction}),
                                           fStrict = fStrict);

    def _unauthorizedUser(self):
        """
        Displays the unauthorized user message (corresponding record is not
        present in DB).
        """

        sLoginName = self._oSrvGlue.getLoginName();

        # Report to system log
        oSystemLogLogic = SystemLogLogic(self._oDb);
        oSystemLogLogic.addEntry(SystemLogData.ksEvent_UserAccountUnknown,
                                 'Unknown user (%s) attempts to access from %s'
                                 % (sLoginName, self._oSrvGlue.getClientAddr()),
                                 24, fCommit = True)

        # Display message.
        self._sPageTitle = 'User not authorized'
        self._sPageBody = """
            <p>Access denied for user <b>%s</b>.
            Please contact an admin user to set up your access.</p>
            """ % (sLoginName,)
        return True;

    def dispatchRequest(self):
        """
        Dispatches a request.
        """

        #
        # Get the parameters and checks for duplicates.
        #
        try:
            dParams = self._oSrvGlue.getParameters();
        except Exception as oXcpt:
            raise WuiException('Error retriving parameters: %s' % (oXcpt,));

        for sKey in dParams.keys():

            # Take care about strings which may contain unicode characters: convert percent-encoded symbols back to unicode.
            for idxItem, _ in enumerate(dParams[sKey]):
                dParams[sKey][idxItem] = utils.toUnicode(dParams[sKey][idxItem], 'utf-8');

            if not len(dParams[sKey]) > 1:
                dParams[sKey] = dParams[sKey][0];
        self._dParams = dParams;

        #
        # Figure out the requested action and validate it.
        #
        if self.ksParamAction in self._dParams:
            self._sAction = self._dParams[self.ksParamAction];
            self._asCheckedParams.append(self.ksParamAction);
        else:
            self._sAction = self.ksActionDefault;

        if isinstance(self._sAction, list) or  self._sAction not in self._dDispatch:
            raise WuiException('Unknown action "%s" requested' % (self._sAction,));

        #
        # Call action handler and generate the page (if necessary).
        #
        if self._oCurUser is not None:
            self._debugProcessDispatch();
            if self._dDispatch[self._sAction]() is self.ksDispatchRcAllDone:
                return True;
        else:
            self._unauthorizedUser();

        if self._sRedirectTo is None:
            self._generatePage();
        else:
            self._redirectPage();
        return True;


    def dprint(self, sText):
        """ Debug printing. """
        if config.g_kfWebUiDebug:
            self._oSrvGlue.dprint(sText);
