# -*- coding: utf-8 -*-
# $Id: wuimain.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Core - WUI - The Main page.
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

# Validation Kit imports.
from testmanager                            import config;
from testmanager.core.base                  import TMExceptionBase, TMTooManyRows;
from testmanager.webui.wuibase              import WuiDispatcherBase, WuiException;
from testmanager.webui.wuicontentbase       import WuiTmLink;
from common                                 import webutils, utils;



class WuiMain(WuiDispatcherBase):
    """
    WUI Main page.

    Note! All cylic dependency avoiance stuff goes here in the dispatcher code,
          not in the action specific code.  This keeps the uglyness in one place
          and reduces load time dependencies in the more critical code path.
    """

    ## The name of the script.
    ksScriptName = 'index.py'

    ## @name Actions
    ## @{
    ksActionResultsUnGrouped            = 'ResultsUnGrouped'
    ksActionResultsGroupedBySchedGroup  = 'ResultsGroupedBySchedGroup'
    ksActionResultsGroupedByTestGroup   = 'ResultsGroupedByTestGroup'
    ksActionResultsGroupedByBuildRev    = 'ResultsGroupedByBuildRev'
    ksActionResultsGroupedByBuildCat    = 'ResultsGroupedByBuildCat'
    ksActionResultsGroupedByTestBox     = 'ResultsGroupedByTestBox'
    ksActionResultsGroupedByTestCase    = 'ResultsGroupedByTestCase'
    ksActionResultsGroupedByOS          = 'ResultsGroupedByOS'
    ksActionResultsGroupedByArch        = 'ResultsGroupedByArch'
    ksActionTestSetDetails              = 'TestSetDetails';
    ksActionTestResultDetails           = ksActionTestSetDetails;
    ksActionTestSetDetailsFromResult    = 'TestSetDetailsFromResult'
    ksActionTestResultFailureDetails    = 'TestResultFailureDetails'
    ksActionTestResultFailureAdd        = 'TestResultFailureAdd'
    ksActionTestResultFailureAddPost    = 'TestResultFailureAddPost'
    ksActionTestResultFailureEdit       = 'TestResultFailureEdit'
    ksActionTestResultFailureEditPost   = 'TestResultFailureEditPost'
    ksActionTestResultFailureDoRemove   = 'TestResultFailureDoRemove'
    ksActionViewLog                     = 'ViewLog'
    ksActionGetFile                     = 'GetFile'
    ksActionReportSummary               = 'ReportSummary';
    ksActionReportRate                  = 'ReportRate';
    ksActionReportTestCaseFailures      = 'ReportTestCaseFailures';
    ksActionReportTestBoxFailures       = 'ReportTestBoxFailures';
    ksActionReportFailureReasons        = 'ReportFailureReasons';
    ksActionGraphWiz                    = 'GraphWiz';
    ksActionVcsHistoryTooltip           = 'VcsHistoryTooltip';  ##< Hardcoded in common.js.
    ## @}

    ## @name Standard report parameters
    ## @{
    ksParamReportPeriods        = 'cPeriods';
    ksParamReportPeriodInHours  = 'cHoursPerPeriod';
    ksParamReportSubject        = 'sSubject';
    ksParamReportSubjectIds     = 'SubjectIds';
    ## @}

    ## @name Graph Wizard parameters
    ## Common parameters: ksParamReportPeriods, ksParamReportPeriodInHours, ksParamReportSubjectIds,
    ##                    ksParamReportSubject, ksParamEffectivePeriod, and ksParamEffectiveDate.
    ## @{
    ksParamGraphWizTestBoxIds   = 'aidTestBoxes';
    ksParamGraphWizBuildCatIds  = 'aidBuildCats';
    ksParamGraphWizTestCaseIds  = 'aidTestCases';
    ksParamGraphWizSepTestVars  = 'fSepTestVars';
    ksParamGraphWizImpl         = 'enmImpl';
    ksParamGraphWizWidth        = 'cx';
    ksParamGraphWizHeight       = 'cy';
    ksParamGraphWizDpi          = 'dpi';
    ksParamGraphWizFontSize     = 'cPtFont';
    ksParamGraphWizErrorBarY    = 'fErrorBarY';
    ksParamGraphWizMaxErrorBarY = 'cMaxErrorBarY';
    ksParamGraphWizMaxPerGraph  = 'cMaxPerGraph';
    ksParamGraphWizXkcdStyle    = 'fXkcdStyle';
    ksParamGraphWizTabular      = 'fTabular';
    ksParamGraphWizSrcTestSetId = 'idSrcTestSet';
    ## @}

    ## @name Graph implementations values for ksParamGraphWizImpl.
    ## @{
    ksGraphWizImpl_Default      = 'default';
    ksGraphWizImpl_Matplotlib   = 'matplotlib';
    ksGraphWizImpl_Charts       = 'charts';
    kasGraphWizImplValid        = [ ksGraphWizImpl_Default, ksGraphWizImpl_Matplotlib, ksGraphWizImpl_Charts];
    kaasGraphWizImplCombo       = [
        ( ksGraphWizImpl_Default,       'Default' ),
        ( ksGraphWizImpl_Matplotlib,    'Matplotlib (server)' ),
        ( ksGraphWizImpl_Charts,        'Google Charts (client)'),
    ];
    ## @}

    ## @name Log Viewer parameters.
    ## @{
    ksParamLogSetId             = 'LogViewer_idTestSet';
    ksParamLogFileId            = 'LogViewer_idFile';
    ksParamLogChunkSize         = 'LogViewer_cbChunk';
    ksParamLogChunkNo           = 'LogViewer_iChunk';
    ## @}

    ## @name File getter parameters.
    ## @{
    ksParamGetFileSetId         = 'GetFile_idTestSet';
    ksParamGetFileId            = 'GetFile_idFile';
    ksParamGetFileDownloadIt    = 'GetFile_fDownloadIt';
    ## @}

    ## @name VCS history parameters.
    ## @{
    ksParamVcsHistoryRepository = 'repo';
    ksParamVcsHistoryRevision   = 'rev';
    ksParamVcsHistoryEntries    = 'cEntries';
    ## @}

    ## @name Test result listing parameters.
    ## @{
    ## If this param is specified, then show only results for this member when results grouped by some parameter.
    ksParamGroupMemberId        = 'GroupMemberId'
    ## Optional parameter for indicating whether to restrict the listing to failures only.
    ksParamOnlyFailures         = 'OnlyFailures';
    ## The sheriff parameter for getting failures needing a reason or two assigned to them.
    ksParamOnlyNeedingReason    = 'OnlyNeedingReason';
    ## Result listing sorting.
    ksParamTestResultsSortBy    = 'enmSortBy'
    ## @}

    ## Effective time period. one of the first column values in kaoResultPeriods.
    ksParamEffectivePeriod      = 'sEffectivePeriod'

    ## Test result period values.
    kaoResultPeriods = [
        ( '1 hour',   '1 hour',      1 ),
        ( '2 hours',  '2 hours',     2 ),
        ( '3 hours',  '3 hours',     3 ),
        ( '6 hours',  '6 hours',     6 ),
        ( '12 hours', '12 hours',    12 ),

        ( '1 day',    '1 day',       24 ),
        ( '2 days',   '2 days',      48 ),
        ( '3 days',   '3 days',      72 ),

        ( '1 week',   '1 week',      168 ),
        ( '2 weeks',  '2 weeks',     336 ),
        ( '3 weeks',  '3 weeks',     504 ),

        ( '1 month',  '1 month',     31 * 24 ),                             # The approx hour count varies with the start date.
        ( '2 months', '2 months',    (31 + 31) * 24 ),                      # Using maximum values.
        ( '3 months', '3 months',    (31 + 30 + 31) * 24 ),

        ( '6 months', '6 months',    (31 + 31 + 30 + 31 + 30 + 31) * 24 ),

        ( '1 year',   '1 year',      365 * 24 ),
    ];
    ## The default test result period.
    ksResultPeriodDefault = '6 hours';



    def __init__(self, oSrvGlue):
        WuiDispatcherBase.__init__(self, oSrvGlue, self.ksScriptName);
        self._sTemplate     = 'template.html'

        #
        # Populate the action dispatcher dictionary.
        # Lambda is forbidden because of readability, speed and reducing number of imports.
        #
        self._dDispatch[self.ksActionResultsUnGrouped]              = self._actionResultsUnGrouped;
        self._dDispatch[self.ksActionResultsGroupedByTestGroup]     = self._actionResultsGroupedByTestGroup;
        self._dDispatch[self.ksActionResultsGroupedByBuildRev]      = self._actionResultsGroupedByBuildRev;
        self._dDispatch[self.ksActionResultsGroupedByBuildCat]      = self._actionResultsGroupedByBuildCat;
        self._dDispatch[self.ksActionResultsGroupedByTestBox]       = self._actionResultsGroupedByTestBox;
        self._dDispatch[self.ksActionResultsGroupedByTestCase]      = self._actionResultsGroupedByTestCase;
        self._dDispatch[self.ksActionResultsGroupedByOS]            = self._actionResultsGroupedByOS;
        self._dDispatch[self.ksActionResultsGroupedByArch]          = self._actionResultsGroupedByArch;
        self._dDispatch[self.ksActionResultsGroupedBySchedGroup]    = self._actionResultsGroupedBySchedGroup;

        self._dDispatch[self.ksActionTestSetDetails]                = self._actionTestSetDetails;
        self._dDispatch[self.ksActionTestSetDetailsFromResult]      = self._actionTestSetDetailsFromResult;

        self._dDispatch[self.ksActionTestResultFailureAdd]          = self._actionTestResultFailureAdd;
        self._dDispatch[self.ksActionTestResultFailureAddPost]      = self._actionTestResultFailureAddPost;
        self._dDispatch[self.ksActionTestResultFailureDetails]      = self._actionTestResultFailureDetails;
        self._dDispatch[self.ksActionTestResultFailureDoRemove]     = self._actionTestResultFailureDoRemove;
        self._dDispatch[self.ksActionTestResultFailureEdit]         = self._actionTestResultFailureEdit;
        self._dDispatch[self.ksActionTestResultFailureEditPost]     = self._actionTestResultFailureEditPost;

        self._dDispatch[self.ksActionViewLog]                       = self._actionViewLog;
        self._dDispatch[self.ksActionGetFile]                       = self._actionGetFile;

        self._dDispatch[self.ksActionReportSummary]                 = self._actionReportSummary;
        self._dDispatch[self.ksActionReportRate]                    = self._actionReportRate;
        self._dDispatch[self.ksActionReportTestCaseFailures]        = self._actionReportTestCaseFailures;
        self._dDispatch[self.ksActionReportFailureReasons]          = self._actionReportFailureReasons;
        self._dDispatch[self.ksActionGraphWiz]                      = self._actionGraphWiz;

        self._dDispatch[self.ksActionVcsHistoryTooltip]             = self._actionVcsHistoryTooltip;

        # Legacy.
        self._dDispatch['TestResultDetails']                        = self._dDispatch[self.ksActionTestSetDetails];


        #
        # Popupate the menus.
        #

        # Additional URL parameters keeping for time navigation.
        sExtraTimeNav = ''
        dCurParams = oSrvGlue.getParameters()
        if dCurParams is not None:
            for sExtraParam in [ self.ksParamItemsPerPage, self.ksParamEffectiveDate, self.ksParamEffectivePeriod, ]:
                if sExtraParam in dCurParams:
                    sExtraTimeNav += '&%s' % (webutils.encodeUrlParams({sExtraParam: dCurParams[sExtraParam]}),)

        # Additional URL parameters for reports
        sExtraReports = '';
        if dCurParams is not None:
            for sExtraParam in [ self.ksParamReportPeriods, self.ksParamReportPeriodInHours, self.ksParamEffectiveDate, ]:
                if sExtraParam in dCurParams:
                    sExtraReports += '&%s' % (webutils.encodeUrlParams({sExtraParam: dCurParams[sExtraParam]}),)

        # Shorthand to keep within margins.
        sActUrlBase   = self._sActionUrlBase;
        sOnlyFailures = '&%s%s' % ( webutils.encodeUrlParams({self.ksParamOnlyFailures: True}), sExtraTimeNav, );
        sSheriff      = '&%s%s' % ( webutils.encodeUrlParams({self.ksParamOnlyNeedingReason: True}), sExtraTimeNav, );

        self._aaoMenus = \
        [
            [
                'Sheriff',     sActUrlBase + self.ksActionResultsUnGrouped + sSheriff,
                [
                    [ 'Grouped by',        None ],
                    [ 'Ungrouped',          sActUrlBase + self.ksActionResultsUnGrouped           + sSheriff, False ],
                    [ 'Sched group',        sActUrlBase + self.ksActionResultsGroupedBySchedGroup + sSheriff, False ],
                    [ 'Test group',         sActUrlBase + self.ksActionResultsGroupedByTestGroup  + sSheriff, False ],
                    [ 'Test case',          sActUrlBase + self.ksActionResultsGroupedByTestCase   + sSheriff, False ],
                    [ 'Testbox',            sActUrlBase + self.ksActionResultsGroupedByTestBox    + sSheriff, False ],
                    [ 'OS',                 sActUrlBase + self.ksActionResultsGroupedByOS         + sSheriff, False ],
                    [ 'Architecture',       sActUrlBase + self.ksActionResultsGroupedByArch       + sSheriff, False ],
                    [ 'Revision',           sActUrlBase + self.ksActionResultsGroupedByBuildRev   + sSheriff, False ],
                    [ 'Build category',     sActUrlBase + self.ksActionResultsGroupedByBuildCat   + sSheriff, False ],
                ]
            ],
            [
                'Reports',          sActUrlBase + self.ksActionReportSummary,
                [
                    [ 'Summary',                  sActUrlBase + self.ksActionReportSummary          + sExtraReports, False ],
                    [ 'Success rate',             sActUrlBase + self.ksActionReportRate             + sExtraReports, False ],
                    [ 'Test case failures',       sActUrlBase + self.ksActionReportTestCaseFailures + sExtraReports, False ],
                    [ 'Testbox failures',         sActUrlBase + self.ksActionReportTestBoxFailures  + sExtraReports, False ],
                    [ 'Failure reasons',          sActUrlBase + self.ksActionReportFailureReasons   + sExtraReports, False ],
                ]
            ],
            [
                'Test Results',     sActUrlBase + self.ksActionResultsUnGrouped + sExtraTimeNav,
                [
                    [ 'Grouped by',        None ],
                    [ 'Ungrouped',          sActUrlBase + self.ksActionResultsUnGrouped           + sExtraTimeNav, False ],
                    [ 'Sched group',        sActUrlBase + self.ksActionResultsGroupedBySchedGroup + sExtraTimeNav, False ],
                    [ 'Test group',         sActUrlBase + self.ksActionResultsGroupedByTestGroup  + sExtraTimeNav, False ],
                    [ 'Test case',          sActUrlBase + self.ksActionResultsGroupedByTestCase   + sExtraTimeNav, False ],
                    [ 'Testbox',            sActUrlBase + self.ksActionResultsGroupedByTestBox    + sExtraTimeNav, False ],
                    [ 'OS',                 sActUrlBase + self.ksActionResultsGroupedByOS         + sExtraTimeNav, False ],
                    [ 'Architecture',       sActUrlBase + self.ksActionResultsGroupedByArch       + sExtraTimeNav, False ],
                    [ 'Revision',           sActUrlBase + self.ksActionResultsGroupedByBuildRev   + sExtraTimeNav, False ],
                    [ 'Build category',     sActUrlBase + self.ksActionResultsGroupedByBuildCat   + sExtraTimeNav, False ],
                ]
            ],
            [
                'Test Failures',     sActUrlBase + self.ksActionResultsUnGrouped + sOnlyFailures,
                [
                    [ 'Grouped by',        None ],
                    [ 'Ungrouped',          sActUrlBase + self.ksActionResultsUnGrouped           + sOnlyFailures, False ],
                    [ 'Sched group',        sActUrlBase + self.ksActionResultsGroupedBySchedGroup + sOnlyFailures, False ],
                    [ 'Test group',         sActUrlBase + self.ksActionResultsGroupedByTestGroup  + sOnlyFailures, False ],
                    [ 'Test case',          sActUrlBase + self.ksActionResultsGroupedByTestCase   + sOnlyFailures, False ],
                    [ 'Testbox',            sActUrlBase + self.ksActionResultsGroupedByTestBox    + sOnlyFailures, False ],
                    [ 'OS',                 sActUrlBase + self.ksActionResultsGroupedByOS         + sOnlyFailures, False ],
                    [ 'Architecture',       sActUrlBase + self.ksActionResultsGroupedByArch       + sOnlyFailures, False ],
                    [ 'Revision',           sActUrlBase + self.ksActionResultsGroupedByBuildRev   + sOnlyFailures, False ],
                    [ 'Build category',     sActUrlBase + self.ksActionResultsGroupedByBuildCat   + sOnlyFailures, False ],
                ]
            ],
            [
                '> Admin', 'admin.py?' + webutils.encodeUrlParams(self._dDbgParams), []
            ],
        ];


    #
    # Overriding parent methods.
    #

    def _generatePage(self):
        """Override parent handler in order to change page title."""
        if self._sPageTitle is not None:
            self._sPageTitle = 'Test Results - ' + self._sPageTitle

        return WuiDispatcherBase._generatePage(self)

    def _actionDefault(self):
        """Show the default admin page."""
        from testmanager.webui.wuitestresult import WuiGroupedResultList;
        from testmanager.core.testresults    import TestResultLogic, TestResultFilter;
        self._sAction = self.ksActionResultsUnGrouped
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeNone,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _isMenuMatch(self, sMenuUrl, sActionParam):
        if super(WuiMain, self)._isMenuMatch(sMenuUrl, sActionParam):
            fOnlyNeedingReason = self.getBoolParam(self.ksParamOnlyNeedingReason, fDefault = False);
            if fOnlyNeedingReason:
                return (sMenuUrl.find(self.ksParamOnlyNeedingReason) > 0);
            fOnlyFailures = self.getBoolParam(self.ksParamOnlyFailures, fDefault = False);
            return (sMenuUrl.find(self.ksParamOnlyFailures) > 0) == fOnlyFailures \
                and sMenuUrl.find(self.ksParamOnlyNeedingReason) < 0;
        return False;


    #
    # Navigation bar stuff
    #

    def _generateSortBySelector(self, dParams, sPreamble, sPostamble):
        """
        Generate HTML code for the sort by selector.
        """
        from testmanager.core.testresults    import TestResultLogic;

        if self.ksParamTestResultsSortBy in dParams:
            enmResultSortBy = dParams[self.ksParamTestResultsSortBy];
            del dParams[self.ksParamTestResultsSortBy];
        else:
            enmResultSortBy = TestResultLogic.ksResultsSortByRunningAndStart;

        sHtmlSortBy  = '<form name="TimeForm" method="GET"> Sort by\n';
        sHtmlSortBy += sPreamble;
        sHtmlSortBy += '\n  <select name="%s" onchange="window.location=' % (self.ksParamTestResultsSortBy,);
        sHtmlSortBy += '\'?%s&%s=\' + ' % (webutils.encodeUrlParams(dParams), self.ksParamTestResultsSortBy)
        sHtmlSortBy += 'this.options[this.selectedIndex].value;" title="Sorting by">\n'

        fSelected = False;
        for enmCode, sTitle in TestResultLogic.kaasResultsSortByTitles:
            if enmCode == enmResultSortBy:
                fSelected = True;
            sHtmlSortBy += '    <option value="%s"%s>%s</option>\n' \
                         % (enmCode, ' selected="selected"' if enmCode == enmResultSortBy else '', sTitle,);
        assert fSelected;
        sHtmlSortBy += '  </select>\n';
        sHtmlSortBy += sPostamble;
        sHtmlSortBy += '\n</form>\n'
        return sHtmlSortBy;

    def _generateStatusSelector(self, dParams, fOnlyFailures):
        """
        Generate HTML code for the status code selector.  Currently very simple.
        """
        dParams[self.ksParamOnlyFailures] = not fOnlyFailures;
        return WuiTmLink('Show all results' if fOnlyFailures else 'Only show failed tests', '', dParams,
                         fBracketed = False).toHtml();

    def _generateTimeWalker(self, dParams, tsEffective, sCurPeriod):
        """
        Generates HTML code for walking back and forth in time.
        """
        # Have to do some math here. :-/
        if tsEffective is None:
            self._oDb.execute('SELECT CURRENT_TIMESTAMP - \'' + sCurPeriod + '\'::interval');
            tsNext = None;
            tsPrev = self._oDb.fetchOne()[0];
        else:
            self._oDb.execute('SELECT %s::TIMESTAMP - \'' + sCurPeriod + '\'::interval,\n'
                              '       %s::TIMESTAMP + \'' + sCurPeriod + '\'::interval',
                              (tsEffective, tsEffective,));
            tsPrev, tsNext = self._oDb.fetchOne();

        # Forget about page No when changing a period
        if WuiDispatcherBase.ksParamPageNo in dParams:
            del dParams[WuiDispatcherBase.ksParamPageNo]

        # Format.
        dParams[WuiDispatcherBase.ksParamEffectiveDate] = str(tsPrev);
        sPrev = '<a href="?%s" title="One period earlier">&lt;&lt;</a>&nbsp;&nbsp;' \
              % (webutils.encodeUrlParams(dParams),);

        if tsNext is not None:
            dParams[WuiDispatcherBase.ksParamEffectiveDate] = str(tsNext);
            sNext = '&nbsp;&nbsp;<a href="?%s" title="One period later">&gt;&gt;</a>' \
                   % (webutils.encodeUrlParams(dParams),);
        else:
            sNext = '&nbsp;&nbsp;&gt;&gt;';

        from testmanager.webui.wuicontentbase import WuiListContentBase; ## @todo move to better place.
        return WuiListContentBase.generateTimeNavigation('top', self.getParameters(), self.getEffectiveDateParam(),
                                                         sPrev, sNext, False);

    def _generateResultPeriodSelector(self, dParams, sCurPeriod):
        """
        Generate HTML code for result period selector.
        """

        if self.ksParamEffectivePeriod in dParams:
            del dParams[self.ksParamEffectivePeriod];

        # Forget about page No when changing a period
        if WuiDispatcherBase.ksParamPageNo in dParams:
            del dParams[WuiDispatcherBase.ksParamPageNo]

        sHtmlPeriodSelector  = '<form name="PeriodForm" method="GET">\n'
        sHtmlPeriodSelector += '  Period is\n'
        sHtmlPeriodSelector += '  <select name="%s" onchange="window.location=' % self.ksParamEffectivePeriod
        sHtmlPeriodSelector += '\'?%s&%s=\' + ' % (webutils.encodeUrlParams(dParams), self.ksParamEffectivePeriod)
        sHtmlPeriodSelector += 'this.options[this.selectedIndex].value;">\n'

        for sPeriodValue, sPeriodCaption, _ in self.kaoResultPeriods:
            sHtmlPeriodSelector += '    <option value="%s"%s>%s</option>\n' \
                                % (webutils.quoteUrl(sPeriodValue),
                                   ' selected="selected"' if sPeriodValue == sCurPeriod else '',
                                   sPeriodCaption)

        sHtmlPeriodSelector += '  </select>\n' \
                               '</form>\n'

        return sHtmlPeriodSelector

    def _generateGroupContentSelector(self, aoGroupMembers, iCurrentMember, sAltAction):
        """
        Generate HTML code for group content selector.
        """

        dParams = self.getParameters()

        if self.ksParamGroupMemberId in dParams:
            del dParams[self.ksParamGroupMemberId]

        if sAltAction is not None:
            if self.ksParamAction in dParams:
                del dParams[self.ksParamAction];
            dParams[self.ksParamAction] = sAltAction;

        sHtmlSelector  = '<form name="GroupContentForm" method="GET">\n'
        sHtmlSelector += '  <select name="%s" onchange="window.location=' % self.ksParamGroupMemberId
        sHtmlSelector += '\'?%s&%s=\' + ' % (webutils.encodeUrlParams(dParams), self.ksParamGroupMemberId)
        sHtmlSelector += 'this.options[this.selectedIndex].value;">\n'

        sHtmlSelector += '<option value="-1">All</option>\n'

        for iGroupMemberId, sGroupMemberName in aoGroupMembers:
            if iGroupMemberId is not None:
                sHtmlSelector += '    <option value="%s"%s>%s</option>\n' \
                                    % (iGroupMemberId,
                                       ' selected="selected"' if iGroupMemberId == iCurrentMember else '',
                                       sGroupMemberName)

        sHtmlSelector += '  </select>\n' \
                         '</form>\n'

        return sHtmlSelector

    def _generatePagesSelector(self, dParams, cItems, cItemsPerPage, iPage):
        """
        Generate HTML code for pages (1, 2, 3 ... N) selector
        """

        if WuiDispatcherBase.ksParamPageNo in dParams:
            del dParams[WuiDispatcherBase.ksParamPageNo]

        sHrefPtr    = '<a href="?%s&%s=' % (webutils.encodeUrlParams(dParams).replace('%', '%%'),
                                            WuiDispatcherBase.ksParamPageNo)
        sHrefPtr   += '%d">%s</a>'

        cNumOfPages      = (cItems + cItemsPerPage - 1) // cItemsPerPage;
        cPagesToDisplay  = 10
        cPagesRangeStart = iPage - cPagesToDisplay // 2 \
                           if not iPage - cPagesToDisplay / 2 < 0 else 0
        cPagesRangeEnd   = cPagesRangeStart + cPagesToDisplay \
                           if not cPagesRangeStart + cPagesToDisplay > cNumOfPages else cNumOfPages
        # Adjust pages range
        if cNumOfPages < cPagesToDisplay:
            cPagesRangeStart = 0
            cPagesRangeEnd   = cNumOfPages

        # 1 2 3 4...
        sHtmlPager  = '&nbsp;\n'.join(sHrefPtr % (x, str(x + 1)) if x != iPage else str(x + 1)
                                      for x in range(cPagesRangeStart, cPagesRangeEnd))
        if cPagesRangeStart > 0:
            sHtmlPager = '%s&nbsp; ... &nbsp;\n' % (sHrefPtr % (0, str(1))) + sHtmlPager
        if cPagesRangeEnd < cNumOfPages:
            sHtmlPager += ' ... %s\n' % (sHrefPtr % (cNumOfPages, str(cNumOfPages + 1)))

        # Prev/Next (using << >> because &laquo; and &raquo are too tiny).
        if iPage > 0:
            dParams[WuiDispatcherBase.ksParamPageNo] = iPage - 1
            sHtmlPager = ('<a title="Previous page" href="?%s">&lt;&lt;</a>&nbsp;&nbsp;\n'
                          % (webutils.encodeUrlParams(dParams), )) \
                          + sHtmlPager;
        else:
            sHtmlPager = '&lt;&lt;&nbsp;&nbsp;\n' + sHtmlPager

        if iPage + 1 < cNumOfPages:
            dParams[WuiDispatcherBase.ksParamPageNo] = iPage + 1
            sHtmlPager += '\n&nbsp; <a title="Next page" href="?%s">&gt;&gt;</a>\n' % (webutils.encodeUrlParams(dParams),)
        else:
            sHtmlPager += '\n&nbsp; &gt;&gt;\n'

        return sHtmlPager

    def _generateItemPerPageSelector(self, dParams, cItemsPerPage):
        """
        Generate HTML code for items per page selector
        Note! Modifies dParams!
        """

        from testmanager.webui.wuicontentbase import WuiListContentBase; ## @todo move to better place.
        return WuiListContentBase.generateItemPerPageSelector('top', dParams, cItemsPerPage);

    def _generateResultNavigation(self, cItems, cItemsPerPage, iPage, tsEffective, sCurPeriod, fOnlyFailures,
                                  sHtmlMemberSelector):
        """ Make custom time navigation bar for the results. """

        # Generate the elements.
        sHtmlStatusSelector = self._generateStatusSelector(self.getParameters(), fOnlyFailures);
        sHtmlSortBySelector = self._generateSortBySelector(self.getParameters(), '', sHtmlStatusSelector);
        sHtmlPeriodSelector = self._generateResultPeriodSelector(self.getParameters(), sCurPeriod)
        sHtmlTimeWalker     = self._generateTimeWalker(self.getParameters(), tsEffective, sCurPeriod);

        if cItems > 0:
            sHtmlPager = self._generatePagesSelector(self.getParameters(), cItems, cItemsPerPage, iPage)
            sHtmlItemsPerPageSelector = self._generateItemPerPageSelector(self.getParameters(), cItemsPerPage)
        else:
            sHtmlPager = ''
            sHtmlItemsPerPageSelector = ''

        # Generate navigation bar
        sHtml = '<table width=100%>\n' \
                '<tr>\n' \
                ' <td width=30%>' + sHtmlMemberSelector + '</td>\n' \
                ' <td width=40% align=center>' + sHtmlTimeWalker + '</td>' \
                ' <td width=30% align=right>\n' + sHtmlPeriodSelector + '</td>\n' \
                '</tr>\n' \
                '<tr>\n' \
                ' <td width=30%>' + sHtmlSortBySelector + '</td>\n' \
                ' <td width=40% align=center>\n' + sHtmlPager + '</td>\n' \
                ' <td width=30% align=right>\n' + sHtmlItemsPerPageSelector + '</td>\n'\
                '</tr>\n' \
                '</table>\n'

        return sHtml

    def _generateReportNavigation(self, tsEffective, cHoursPerPeriod, cPeriods):
        """ Make time navigation bar for the reports. """

        # The period length selector.
        dParams = self.getParameters();
        if WuiMain.ksParamReportPeriodInHours in dParams:
            del dParams[WuiMain.ksParamReportPeriodInHours];
        sHtmlPeriodLength  = '';
        sHtmlPeriodLength += '<form name="ReportPeriodInHoursForm" method="GET">\n' \
                             '  Period length <select name="%s" onchange="window.location=\'?%s&%s=\' + ' \
                             'this.options[this.selectedIndex].value;" title="Statistics period length in hours.">\n' \
                           % (WuiMain.ksParamReportPeriodInHours,
                              webutils.encodeUrlParams(dParams),
                              WuiMain.ksParamReportPeriodInHours)
        for cHours in [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 18, 24, 48, 72, 96, 120, 144, 168 ]:
            sHtmlPeriodLength += '    <option value="%d"%s>%d hour%s</option>\n' \
                               % (cHours, 'selected="selected"' if cHours == cHoursPerPeriod else '', cHours,
                                  's' if cHours > 1 else '');
        sHtmlPeriodLength += '  </select>\n' \
                             '</form>\n'

        # The period count selector.
        dParams = self.getParameters();
        if WuiMain.ksParamReportPeriods in dParams:
            del dParams[WuiMain.ksParamReportPeriods];
        sHtmlCountOfPeriods  = '';
        sHtmlCountOfPeriods += '<form name="ReportPeriodsForm" method="GET">\n' \
                               '  Periods <select name="%s" onchange="window.location=\'?%s&%s=\' + ' \
                               'this.options[this.selectedIndex].value;" title="Statistics periods to report.">\n' \
                             % (WuiMain.ksParamReportPeriods,
                                webutils.encodeUrlParams(dParams),
                                WuiMain.ksParamReportPeriods)
        for cCurPeriods in range(2, 43):
            sHtmlCountOfPeriods += '    <option value="%d"%s>%d</option>\n' \
                                 % (cCurPeriods, 'selected="selected"' if cCurPeriods == cPeriods else '', cCurPeriods);
        sHtmlCountOfPeriods += '  </select>\n' \
                               '</form>\n'

        # The time walker.
        sHtmlTimeWalker = self._generateTimeWalker(self.getParameters(), tsEffective, '%d hours' % (cHoursPerPeriod));

        # Combine them all.
        sHtml = '<table width=100%>\n' \
                ' <tr>\n' \
                '  <td width=30% align="center">\n' + sHtmlPeriodLength + '</td>\n' \
                '  <td width=40% align="center">\n' + sHtmlTimeWalker + '</td>' \
                '  <td width=30% align="center">\n' + sHtmlCountOfPeriods + '</td>\n' \
                ' </tr>\n' \
                '</table>\n';
        return sHtml;


    #
    # The rest of stuff
    #

    def _actionGroupedResultsListing( #pylint: disable=too-many-locals
            self,
            enmResultsGroupingType,
            oResultsLogicType,
            oResultFilterType,
            oResultsListContentType):
        """
        Override generic listing action.

        oResultsLogicType implements getEntriesCount, fetchResultsForListing and more.
        oResultFilterType is a child of ModelFilterBase.
        oResultsListContentType is a child of WuiListContentBase.
        """
        from testmanager.core.testresults    import TestResultLogic;

        cItemsPerPage       = self.getIntParam(self.ksParamItemsPerPage,  iMin =  2, iMax =   9999, iDefault = 128);
        iPage               = self.getIntParam(self.ksParamPageNo,        iMin =  0, iMax = 999999, iDefault = 0);
        tsEffective         = self.getEffectiveDateParam();
        iGroupMemberId      = self.getIntParam(self.ksParamGroupMemberId, iMin = -1, iMax = 999999, iDefault = -1);
        fOnlyFailures       = self.getBoolParam(self.ksParamOnlyFailures, fDefault = False);
        fOnlyNeedingReason  = self.getBoolParam(self.ksParamOnlyNeedingReason, fDefault = False);
        enmResultSortBy     = self.getStringParam(self.ksParamTestResultsSortBy,
                                                  asValidValues = TestResultLogic.kasResultsSortBy,
                                                  sDefault = TestResultLogic.ksResultsSortByRunningAndStart);
        oFilter = oResultFilterType().initFromParams(self);

        # Get testing results period and validate it
        asValidValues       = [x for (x, _, _) in self.kaoResultPeriods]
        sCurPeriod          = self.getStringParam(self.ksParamEffectivePeriod, asValidValues = asValidValues,
                                                  sDefault = self.ksResultPeriodDefault)
        assert sCurPeriod != ''; # Impossible!

        self._checkForUnknownParameters()

        #
        # Fetch the group members.
        #
        # If no grouping is selected, we'll fill the grouping combo with
        # testboxes just to avoid having completely useless combo box.
        #
        oTrLogic = TestResultLogic(self._oDb);
        sAltSelectorAction = None;
        if enmResultsGroupingType in (TestResultLogic.ksResultsGroupingTypeNone, TestResultLogic.ksResultsGroupingTypeTestBox,):
            aoTmp = oTrLogic.getTestBoxes(tsNow = tsEffective, sPeriod = sCurPeriod)
            aoGroupMembers = sorted(list({(x.idTestBox, '%s (%s)' % (x.sName, str(x.ip))) for x in aoTmp }),
                                    reverse = False, key = lambda asData: asData[1])

            if enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeTestBox:
                self._sPageTitle = 'Grouped by Test Box';
            else:
                self._sPageTitle = 'Ungrouped results';
                sAltSelectorAction = self.ksActionResultsGroupedByTestBox;
                aoGroupMembers.insert(0, [None, None]); # The "All" member.

        elif enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeTestGroup:
            aoTmp = oTrLogic.getTestGroups(tsNow = tsEffective, sPeriod = sCurPeriod);
            aoGroupMembers = sorted(list({ (x.idTestGroup, x.sName ) for x in aoTmp }),
                                    reverse = False, key = lambda asData: asData[1])
            self._sPageTitle = 'Grouped by Test Group'

        elif enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeBuildRev:
            aoTmp = oTrLogic.getBuilds(tsNow = tsEffective, sPeriod = sCurPeriod)
            aoGroupMembers = sorted(list({ (x.iRevision, '%s.%d' % (x.oCat.sBranch, x.iRevision)) for x in aoTmp }),
                                    reverse = True, key = lambda asData: asData[0])
            self._sPageTitle = 'Grouped by Build'

        elif enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeBuildCat:
            aoTmp = oTrLogic.getBuildCategories(tsNow = tsEffective, sPeriod = sCurPeriod)
            aoGroupMembers = sorted(list({ (x.idBuildCategory,
                                            '%s / %s / %s / %s' % ( x.sProduct, x.sBranch, ', '.join(x.asOsArches), x.sType) )
                                           for x in aoTmp }),
                                    reverse = True, key = lambda asData: asData[1]);
            self._sPageTitle = 'Grouped by Build Category'

        elif enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeTestCase:
            aoTmp = oTrLogic.getTestCases(tsNow = tsEffective, sPeriod = sCurPeriod)
            aoGroupMembers = sorted(list({ (x.idTestCase, '%s' % x.sName) for x in aoTmp }),
                                    reverse = False, key = lambda asData: asData[1])
            self._sPageTitle = 'Grouped by Test Case'

        elif enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeOS:
            aoTmp = oTrLogic.getOSes(tsNow = tsEffective, sPeriod = sCurPeriod)
            aoGroupMembers = sorted(list(set(aoTmp)), reverse = False, key = lambda asData: asData[1]);
            self._sPageTitle = 'Grouped by OS'

        elif enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeArch:
            aoTmp = oTrLogic.getArchitectures(tsNow = tsEffective, sPeriod = sCurPeriod)
            aoGroupMembers = sorted(list(set(aoTmp)), reverse = False, key = lambda asData: asData[1]);
            self._sPageTitle = 'Grouped by Architecture'

        elif enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeSchedGroup:
            aoTmp = oTrLogic.getSchedGroups(tsNow = tsEffective, sPeriod = sCurPeriod)
            aoGroupMembers = sorted(list({ (x.idSchedGroup, '%s' % x.sName) for x in aoTmp }),
                                    reverse = False, key = lambda asData: asData[1])
            self._sPageTitle = 'Grouped by Scheduling Group'

        else:
            raise TMExceptionBase('Unknown grouping type')

        _sPageBody   = ''
        oContent     = None
        cEntriesMax  = 0
        _dParams     = self.getParameters()
        oResultLogic = oResultsLogicType(self._oDb);
        for idMember, sMemberName in aoGroupMembers:
            #
            # Count and fetch entries to be displayed.
            #

            # Skip group members that were not specified.
            if    idMember != iGroupMemberId \
              and (   (idMember is not None and enmResultsGroupingType == TestResultLogic.ksResultsGroupingTypeNone)
                   or (iGroupMemberId > 0   and enmResultsGroupingType != TestResultLogic.ksResultsGroupingTypeNone) ):
                continue

            cEntries = oResultLogic.getEntriesCount(tsNow = tsEffective,
                                                    sInterval = sCurPeriod,
                                                    oFilter = oFilter,
                                                    enmResultsGroupingType = enmResultsGroupingType,
                                                    iResultsGroupingValue = idMember,
                                                    fOnlyFailures = fOnlyFailures,
                                                    fOnlyNeedingReason = fOnlyNeedingReason);
            if cEntries == 0: # Do not display empty groups
                continue
            aoEntries = oResultLogic.fetchResultsForListing(iPage * cItemsPerPage,
                                                            cItemsPerPage,
                                                            tsNow = tsEffective,
                                                            sInterval = sCurPeriod,
                                                            oFilter = oFilter,
                                                            enmResultSortBy = enmResultSortBy,
                                                            enmResultsGroupingType = enmResultsGroupingType,
                                                            iResultsGroupingValue = idMember,
                                                            fOnlyFailures = fOnlyFailures,
                                                            fOnlyNeedingReason = fOnlyNeedingReason);
            cEntriesMax = max(cEntriesMax, cEntries)

            #
            # Format them.
            #
            oContent = oResultsListContentType(aoEntries,
                                               cEntries,
                                               iPage,
                                               cItemsPerPage,
                                               tsEffective,
                                               fnDPrint = self._oSrvGlue.dprint,
                                               oDisp = self)

            (_, sHtml) = oContent.show(fShowNavigation = False)
            if sMemberName is not None:
                _sPageBody += '<table width=100%><tr><td>'

                _dParams[self.ksParamGroupMemberId] = idMember
                sLink = WuiTmLink(sMemberName, '', _dParams, fBracketed = False).toHtml()

                _sPageBody += '<h2>%s (%d)</h2></td>' % (sLink, cEntries)
                _sPageBody += '<td><br></td>'
                _sPageBody += '</tr></table>'
            _sPageBody += sHtml
            _sPageBody += '<br>'

        #
        # Complete the page by slapping navigation controls at the top and
        # bottom of it.
        #
        sHtmlNavigation = self._generateResultNavigation(cEntriesMax, cItemsPerPage, iPage,
                                                         tsEffective, sCurPeriod, fOnlyFailures,
                                                         self._generateGroupContentSelector(aoGroupMembers, iGroupMemberId,
                                                                                            sAltSelectorAction));
        if cEntriesMax > 0:
            self._sPageBody = sHtmlNavigation + _sPageBody + sHtmlNavigation;
        else:
            self._sPageBody = sHtmlNavigation + '<p align="center"><i>No data to display</i></p>\n';

        #
        # Now, generate a filter control panel for the side bar.
        #
        if hasattr(oFilter, 'kiBranches'):
            oFilter.aCriteria[oFilter.kiBranches].fExpanded = True;
        if hasattr(oFilter, 'kiTestStatus'):
            oFilter.aCriteria[oFilter.kiTestStatus].fExpanded = True;
        self._sPageFilter = self._generateResultFilter(oFilter, oResultLogic, tsEffective, sCurPeriod,
                                                       enmResultsGroupingType = enmResultsGroupingType,
                                                       aoGroupMembers = aoGroupMembers,
                                                       fOnlyFailures = fOnlyFailures,
                                                       fOnlyNeedingReason = fOnlyNeedingReason);
        return True;

    def _generateResultFilter(self, oFilter, oResultLogic, tsNow, sPeriod, enmResultsGroupingType = None, aoGroupMembers = None,
                              fOnlyFailures = False, fOnlyNeedingReason = False):
        """
        Generates the result filter for the left hand side.
        """
        _ = enmResultsGroupingType; _ = aoGroupMembers; _ = fOnlyFailures; _ = fOnlyNeedingReason;
        oResultLogic.fetchPossibleFilterOptions(oFilter, tsNow, sPeriod)

        # Add non-filter parameters as hidden fields so we can use 'GET' and have URLs to bookmark.
        self._dSideMenuFormAttrs['method'] = 'GET';
        sHtml = u'';
        for sKey, oValue in self._oSrvGlue.getParameters().items():
            if len(sKey) > 3:
                if hasattr(oValue, 'startswith'):
                    sHtml += u'<input type="hidden" name="%s" value="%s"/>\n' \
                           % (webutils.escapeAttr(sKey), webutils.escapeAttr(oValue),);
                else:
                    for oSubValue in oValue:
                        sHtml += u'<input type="hidden" name="%s" value="%s"/>\n' \
                               % (webutils.escapeAttr(sKey), webutils.escapeAttr(oSubValue),);

        # Generate the filter panel.
        sHtml += u'<div id="side-filters">\n' \
                 u' <p>Filters' \
                 u' <span class="tm-side-filter-title-buttons"><input type="submit" value="Apply" />\n' \
                 u' <a href="javascript:toggleSidebarSize();" class="tm-sidebar-size-link">&#x00bb;&#x00bb;</a></span></p>\n';
        sHtml += u' <dl>\n';
        for oCrit in oFilter.aCriteria:
            if oCrit.aoPossible or oCrit.sType == oCrit.ksType_Ranges:
                if   (    oCrit.oSub is None \
                      and (   oCrit.sState == oCrit.ksState_Selected \
                           or (len(oCrit.aoPossible) <= 2 and oCrit.sType != oCrit.ksType_Ranges))) \
                  or (    oCrit.oSub is not None \
                      and (   oCrit.sState == oCrit.ksState_Selected \
                           or oCrit.oSub.sState == oCrit.ksState_Selected \
                           or (len(oCrit.aoPossible) <= 2 and len(oCrit.oSub.aoPossible) <= 2))) \
                  or oCrit.fExpanded is True:
                    sClass = 'sf-collapsible';
                    sChar  = '&#9660;';
                else:
                    sClass = 'sf-expandable';
                    sChar  = '&#9654;';

                sHtml += u'  <dt class="%s"><a href="javascript:void(0)" onclick="toggleCollapsibleDtDd(this);">%s %s</a> ' \
                       % (sClass, sChar, webutils.escapeElem(oCrit.sName),);
                sHtml += u'<span class="tm-side-filter-dt-buttons">';
                if oCrit.sInvVarNm is not None:
                    sHtml += u'<input  id="sf-union-%s" class="tm-side-filter-union-input" ' \
                             u'name="%s" value="1" type="checkbox"%s />' \
                             u'<label for="sf-union-%s" class="tm-side-filter-union-input"></label>' \
                           % ( oCrit.sInvVarNm, oCrit.sInvVarNm, ' checked' if oCrit.fInverted else '', oCrit.sInvVarNm,);
                sHtml += u' <input type="submit" value="Apply" />';
                sHtml += u'</span>';
                sHtml += u'</dt>\n' \
                         u'  <dd class="%s">\n' \
                         u'   <ul>\n' \
                         % (sClass);

                if oCrit.sType == oCrit.ksType_Ranges:
                    assert not oCrit.oSub;
                    assert not oCrit.aoPossible;
                    asValues = [];
                    for tRange in oCrit.aoSelected:
                        if tRange[0] == tRange[1]:
                            asValues.append('%s' % (tRange[0],));
                        else:
                            asValues.append('%s-%s' % (tRange[0] if tRange[0] is not None else 'inf',
                                                       tRange[1] if tRange[1] is not None else 'inf'));
                    sHtml += u'    <li title="%s"><input type="text" name="%s" value="%s"/></li>\n' \
                           % ( webutils.escapeAttr('comma separate list of numerical ranges'), oCrit.sVarNm,
                               ', '.join(asValues), );
                else:
                    for oDesc in oCrit.aoPossible:
                        fChecked = oDesc.oValue in oCrit.aoSelected;
                        sHtml += u'    <li%s%s><label><input type="checkbox" name="%s" value="%s"%s%s/>%s%s</label>\n' \
                               % ( ' class="side-filter-irrelevant"' if oDesc.fIrrelevant else '',
                                   (' title="%s"' % (webutils.escapeAttr(oDesc.sHover,)) if oDesc.sHover is not None else ''),
                                   oCrit.sVarNm,
                                   oDesc.oValue,
                                   ' checked' if fChecked else '',
                                   ' onclick="toggleCollapsibleCheckbox(this);"' if oDesc.aoSubs is not None else '',
                                   webutils.escapeElem(oDesc.sDesc),
                                   '<span class="side-filter-count"> [%u]</span>' % (oDesc.cTimes) if oDesc.cTimes is not None
                                   else '', );
                        if oDesc.aoSubs is not None:
                            sHtml += u'     <ul class="sf-checkbox-%s">\n' % ('collapsible' if fChecked else 'expandable', );
                            for oSubDesc in oDesc.aoSubs:
                                fSubChecked = oSubDesc.oValue in oCrit.oSub.aoSelected;
                                sHtml += u'     <li%s%s><label><input type="checkbox" name="%s" value="%s"%s/>%s%s</label>\n' \
                                       % ( ' class="side-filter-irrelevant"' if oSubDesc.fIrrelevant else '',
                                           ' title="%s"' % ( webutils.escapeAttr(oSubDesc.sHover,) if oSubDesc.sHover is not None
                                                             else ''),
                                           oCrit.oSub.sVarNm, oSubDesc.oValue, ' checked' if fSubChecked else '',
                                           webutils.escapeElem(oSubDesc.sDesc),
                                           '<span class="side-filter-count"> [%u]</span>' % (oSubDesc.cTimes)
                                           if oSubDesc.cTimes is not None else '', );

                            sHtml += u'     </ul>\n';
                        sHtml += u'    </li>';

                sHtml += u'   </ul>\n' \
                         u'  </dd>\n';

        sHtml += u' </dl>\n';
        sHtml += u' <input type="submit" value="Apply"/>\n';
        sHtml += u' <input type="reset" value="Reset"/>\n';
        sHtml += u' <button type="button" onclick="clearForm(\'side-menu-form\');">Clear</button>\n';
        sHtml += u'</div>\n';
        return sHtml;

    def _actionResultsUnGrouped(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        #return self._actionResultsListing(TestResultLogic, WuiGroupedResultList)?
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeNone,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedByTestGroup(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeTestGroup,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedByBuildRev(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeBuildRev,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedByBuildCat(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeBuildCat,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedByTestBox(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeTestBox,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedByTestCase(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeTestCase,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedByOS(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeOS,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedByArch(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeArch,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);

    def _actionResultsGroupedBySchedGroup(self):
        """ Action wrapper. """
        from testmanager.webui.wuitestresult        import WuiGroupedResultList;
        from testmanager.core.testresults           import TestResultLogic, TestResultFilter;
        return self._actionGroupedResultsListing(TestResultLogic.ksResultsGroupingTypeSchedGroup,
                                                 TestResultLogic, TestResultFilter, WuiGroupedResultList);


    def _actionTestSetDetailsCommon(self, idTestSet):
        """Show test case execution result details."""
        from testmanager.core.build          import BuildDataEx;
        from testmanager.core.testbox        import TestBoxData;
        from testmanager.core.testcase       import TestCaseDataEx;
        from testmanager.core.testcaseargs   import TestCaseArgsDataEx;
        from testmanager.core.testgroup      import TestGroupData;
        from testmanager.core.testresults    import TestResultLogic;
        from testmanager.core.testset        import TestSetData;
        from testmanager.webui.wuitestresult import WuiTestResult;

        self._sTemplate = 'template-details.html';
        self._checkForUnknownParameters()

        oTestSetData          = TestSetData().initFromDbWithId(self._oDb, idTestSet);
        try:
            (oTestResultTree, _) = TestResultLogic(self._oDb).fetchResultTree(idTestSet);
        except TMTooManyRows:
            (oTestResultTree, _) = TestResultLogic(self._oDb).fetchResultTree(idTestSet, 2);
        oBuildDataEx          = BuildDataEx().initFromDbWithId(self._oDb, oTestSetData.idBuild, oTestSetData.tsCreated);
        try:    oBuildValidationKitDataEx = BuildDataEx().initFromDbWithId(self._oDb, oTestSetData.idBuildTestSuite,
                                                                           oTestSetData.tsCreated);
        except: oBuildValidationKitDataEx = None;
        oTestBoxData          = TestBoxData().initFromDbWithGenId(self._oDb, oTestSetData.idGenTestBox);
        oTestGroupData        = TestGroupData().initFromDbWithId(self._oDb,  ## @todo This bogus time wise. Bad DB design?
                                                                 oTestSetData.idTestGroup, oTestSetData.tsCreated);
        oTestCaseDataEx       = TestCaseDataEx().initFromDbWithGenId(self._oDb, oTestSetData.idGenTestCase,
                                                                     oTestSetData.tsConfig);
        oTestCaseArgsDataEx   = TestCaseArgsDataEx().initFromDbWithGenIdEx(self._oDb, oTestSetData.idGenTestCaseArgs,
                                                                           oTestSetData.tsConfig);

        oContent = WuiTestResult(oDisp = self, fnDPrint = self._oSrvGlue.dprint);
        (self._sPageTitle, self._sPageBody) = oContent.showTestCaseResultDetails(oTestResultTree,
                                                                                 oTestSetData,
                                                                                 oBuildDataEx,
                                                                                 oBuildValidationKitDataEx,
                                                                                 oTestBoxData,
                                                                                 oTestGroupData,
                                                                                 oTestCaseDataEx,
                                                                                 oTestCaseArgsDataEx);
        return True

    def _actionTestSetDetails(self):
        """Show test case execution result details."""
        from testmanager.core.testset        import TestSetData;

        idTestSet = self.getIntParam(TestSetData.ksParam_idTestSet);
        return self._actionTestSetDetailsCommon(idTestSet);

    def _actionTestSetDetailsFromResult(self):
        """Show test case execution result details."""
        from testmanager.core.testresults    import TestResultData;
        from testmanager.core.testset        import TestSetData;
        idTestResult = self.getIntParam(TestSetData.ksParam_idTestResult);
        oTestResultData = TestResultData().initFromDbWithId(self._oDb, idTestResult);
        return self._actionTestSetDetailsCommon(oTestResultData.idTestSet);


    def _actionTestResultFailureAdd(self):
        """ Pro forma. """
        from testmanager.core.testresultfailures import TestResultFailureData;
        from testmanager.webui.wuitestresultfailure import WuiTestResultFailure;
        return self._actionGenericFormAdd(TestResultFailureData, WuiTestResultFailure);

    def _actionTestResultFailureAddPost(self):
        """Add test result failure result"""
        from testmanager.core.testresultfailures import TestResultFailureLogic, TestResultFailureData;
        from testmanager.webui.wuitestresultfailure import WuiTestResultFailure;
        if self.ksParamRedirectTo not in self._dParams:
            raise WuiException('Missing parameter ' + self.ksParamRedirectTo);

        return self._actionGenericFormAddPost(TestResultFailureData, TestResultFailureLogic,
                                              WuiTestResultFailure, self.ksActionResultsUnGrouped);

    def _actionTestResultFailureDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.testresultfailures import TestResultFailureData, TestResultFailureLogic;
        return self._actionGenericDoRemove(TestResultFailureLogic, TestResultFailureData.ksParam_idTestResult,
                                           self.ksActionResultsUnGrouped);

    def _actionTestResultFailureDetails(self):
        """ Pro forma. """
        from testmanager.core.testresultfailures import TestResultFailureLogic, TestResultFailureData;
        from testmanager.webui.wuitestresultfailure import WuiTestResultFailure;
        return self._actionGenericFormDetails(TestResultFailureData, TestResultFailureLogic,
                                              WuiTestResultFailure, 'idTestResult');

    def _actionTestResultFailureEdit(self):
        """ Pro forma. """
        from testmanager.core.testresultfailures import TestResultFailureData;
        from testmanager.webui.wuitestresultfailure import WuiTestResultFailure;
        return self._actionGenericFormEdit(TestResultFailureData, WuiTestResultFailure,
                                           TestResultFailureData.ksParam_idTestResult);

    def _actionTestResultFailureEditPost(self):
        """Edit test result failure result"""
        from testmanager.core.testresultfailures import TestResultFailureLogic, TestResultFailureData;
        from testmanager.webui.wuitestresultfailure import WuiTestResultFailure;
        return self._actionGenericFormEditPost(TestResultFailureData, TestResultFailureLogic,
                                               WuiTestResultFailure, self.ksActionResultsUnGrouped);

    def _actionViewLog(self):
        """
        Log viewer action.
        """
        from testmanager.core.testresults   import TestResultLogic, TestResultFileDataEx;
        from testmanager.core.testset       import TestSetData, TestSetLogic;
        from testmanager.webui.wuilogviewer import WuiLogViewer;

        self._sTemplate = 'template-details.html'; ## @todo create new template (background color, etc)
        idTestSet       = self.getIntParam(self.ksParamLogSetId,     iMin = 1);
        idLogFile       = self.getIntParam(self.ksParamLogFileId,    iMin = 0,                    iDefault = 0);
        cbChunk         = self.getIntParam(self.ksParamLogChunkSize, iMin = 256, iMax = 16777216, iDefault = 1024*1024);
        iChunk          = self.getIntParam(self.ksParamLogChunkNo,   iMin = 0,
                                           iMax = config.g_kcMbMaxMainLog * 1048576 / cbChunk,    iDefault = 0);
        self._checkForUnknownParameters();

        oTestSet = TestSetData().initFromDbWithId(self._oDb, idTestSet);
        if idLogFile == 0:
            oTestFile    = TestResultFileDataEx().initFakeMainLog(oTestSet);
            aoTimestamps = TestResultLogic(self._oDb).fetchTimestampsForLogViewer(idTestSet);
        else:
            oTestFile    = TestSetLogic(self._oDb).getFile(idTestSet, idLogFile);
            aoTimestamps = [];
        if oTestFile.sMime not in [ 'text/plain',]:
            raise WuiException('The log view does not display files of type: %s' % (oTestFile.sMime,));

        oContent = WuiLogViewer(oTestSet, oTestFile, cbChunk, iChunk, aoTimestamps,
                                oDisp = self, fnDPrint = self._oSrvGlue.dprint);
        (self._sPageTitle, self._sPageBody) = oContent.show();
        return True;

    def _actionGetFile(self):
        """
        Get file action.
        """
        from testmanager.core.testset           import TestSetData, TestSetLogic;
        from testmanager.core.testresults       import TestResultFileDataEx;

        idTestSet       = self.getIntParam(self.ksParamGetFileSetId,        iMin = 1);
        idFile          = self.getIntParam(self.ksParamGetFileId,           iMin = 0, iDefault = 0);
        fDownloadIt     = self.getBoolParam(self.ksParamGetFileDownloadIt,  fDefault = True);
        self._checkForUnknownParameters();

        #
        # Get the file info and open it.
        #
        oTestSet = TestSetData().initFromDbWithId(self._oDb, idTestSet);
        if idFile == 0:
            oTestFile = TestResultFileDataEx().initFakeMainLog(oTestSet);
        else:
            oTestFile = TestSetLogic(self._oDb).getFile(idTestSet, idFile);

        (oFile, oSizeOrError, _) = oTestSet.openFile(oTestFile.sFile, 'rb');
        if oFile is None:
            raise Exception(oSizeOrError);

        #
        # Send the file.
        #
        self._oSrvGlue.setHeaderField('Content-Type', oTestFile.getMimeWithEncoding());
        if fDownloadIt:
            self._oSrvGlue.setHeaderField('Content-Disposition', 'attachment; filename="TestSet-%d-%s"'
                                          % (idTestSet, oTestFile.sFile,));
        while True:
            abChunk = oFile.read(262144);
            if not abChunk:
                break;
            self._oSrvGlue.writeRaw(abChunk);
        return self.ksDispatchRcAllDone;

    def _actionGenericReport(self, oModelType, oFilterType, oReportType):
        """
        Generic report action.
        oReportType is a child of WuiReportContentBase.
        oFilterType is a child of ModelFilterBase.
        oModelType is a child of ReportModelBase.
        """
        from testmanager.core.report                import ReportModelBase;

        tsEffective     = self.getEffectiveDateParam();
        cPeriods        = self.getIntParam(self.ksParamReportPeriods,       iMin = 2, iMax =   99,  iDefault = 7);
        cHoursPerPeriod = self.getIntParam(self.ksParamReportPeriodInHours, iMin = 1, iMax =   168, iDefault = 24);
        sSubject        = self.getStringParam(self.ksParamReportSubject, ReportModelBase.kasSubjects,
                                              ReportModelBase.ksSubEverything);
        if sSubject == ReportModelBase.ksSubEverything:
            aidSubjects = self.getListOfIntParams(self.ksParamReportSubjectIds, aiDefaults = []);
        else:
            aidSubjects = self.getListOfIntParams(self.ksParamReportSubjectIds, iMin = 1);
            if aidSubjects is None:
                raise WuiException('Missing parameter %s' % (self.ksParamReportSubjectIds,));

        aiSortColumnsDup = self.getListOfIntParams(self.ksParamSortColumns,
                                                   iMin = -getattr(oReportType, 'kcMaxSortColumns', cPeriods) + 1,
                                                   iMax = getattr(oReportType, 'kcMaxSortColumns', cPeriods), aiDefaults = []);
        aiSortColumns   = [];
        for iSortColumn in aiSortColumnsDup:
            if iSortColumn not in aiSortColumns:
                aiSortColumns.append(iSortColumn);

        oFilter = oFilterType().initFromParams(self);
        self._checkForUnknownParameters();

        dParams = \
        {
            self.ksParamEffectiveDate:          tsEffective,
            self.ksParamReportPeriods:          cPeriods,
            self.ksParamReportPeriodInHours:    cHoursPerPeriod,
            self.ksParamReportSubject:          sSubject,
            self.ksParamReportSubjectIds:       aidSubjects,
        };
        ## @todo oFilter.

        oModel   = oModelType(self._oDb, tsEffective, cPeriods, cHoursPerPeriod, sSubject, aidSubjects, oFilter);
        oContent = oReportType(oModel, dParams, fSubReport = False, aiSortColumns = aiSortColumns,
                               fnDPrint = self._oSrvGlue.dprint, oDisp = self);
        (self._sPageTitle, self._sPageBody) = oContent.show();
        sNavi = self._generateReportNavigation(tsEffective, cHoursPerPeriod, cPeriods);
        self._sPageBody = sNavi + self._sPageBody;

        if hasattr(oFilter, 'kiBranches'):
            oFilter.aCriteria[oFilter.kiBranches].fExpanded = True;
        self._sPageFilter = self._generateResultFilter(oFilter, oModel, tsEffective, '%s hours' % (cHoursPerPeriod * cPeriods,));
        return True;

    def _actionReportSummary(self):
        """ Action wrapper. """
        from testmanager.core.report                import ReportLazyModel, ReportFilter;
        from testmanager.webui.wuireport            import WuiReportSummary;
        return self._actionGenericReport(ReportLazyModel, ReportFilter, WuiReportSummary);

    def _actionReportRate(self):
        """ Action wrapper. """
        from testmanager.core.report                import ReportLazyModel, ReportFilter;
        from testmanager.webui.wuireport            import WuiReportSuccessRate;
        return self._actionGenericReport(ReportLazyModel, ReportFilter, WuiReportSuccessRate);

    def _actionReportTestCaseFailures(self):
        """ Action wrapper. """
        from testmanager.core.report                import ReportLazyModel, ReportFilter;
        from testmanager.webui.wuireport            import WuiReportTestCaseFailures;
        return self._actionGenericReport(ReportLazyModel, ReportFilter, WuiReportTestCaseFailures);

    def _actionReportFailureReasons(self):
        """ Action wrapper. """
        from testmanager.core.report                import ReportLazyModel, ReportFilter;
        from testmanager.webui.wuireport            import WuiReportFailureReasons;
        return self._actionGenericReport(ReportLazyModel, ReportFilter, WuiReportFailureReasons);

    def _actionGraphWiz(self):
        """
        Graph wizard action.
        """
        from testmanager.core.report                import ReportModelBase, ReportGraphModel;
        from testmanager.webui.wuigraphwiz          import WuiGraphWiz;
        self._sTemplate = 'template-graphwiz.html';

        tsEffective     = self.getEffectiveDateParam();
        cPeriods        = self.getIntParam(self.ksParamReportPeriods, iMin = 1, iMax = 1, iDefault = 1); # Not needed yet.
        sTmp            = self.getStringParam(self.ksParamReportPeriodInHours, sDefault = '3 weeks');
        (cHoursPerPeriod, sError) = utils.parseIntervalHours(sTmp);
        if sError is not None: raise WuiException(sError);
        asSubjectIds    = self.getListOfStrParams(self.ksParamReportSubjectIds);
        sSubject        = self.getStringParam(self.ksParamReportSubject, [ReportModelBase.ksSubEverything],
                                              ReportModelBase.ksSubEverything); # dummy
        aidTestBoxes    = self.getListOfIntParams(self.ksParamGraphWizTestBoxIds,  iMin = 1, aiDefaults = []);
        aidBuildCats    = self.getListOfIntParams(self.ksParamGraphWizBuildCatIds, iMin = 1, aiDefaults = []);
        aidTestCases    = self.getListOfIntParams(self.ksParamGraphWizTestCaseIds, iMin = 1, aiDefaults = []);
        fSepTestVars    = self.getBoolParam(self.ksParamGraphWizSepTestVars, fDefault = False);

        enmGraphImpl    = self.getStringParam(self.ksParamGraphWizImpl, asValidValues = self.kasGraphWizImplValid,
                                              sDefault = self.ksGraphWizImpl_Default);
        cx              = self.getIntParam(self.ksParamGraphWizWidth,  iMin = 128, iMax = 8192, iDefault = 1280);
        cy              = self.getIntParam(self.ksParamGraphWizHeight, iMin = 128, iMax = 8192, iDefault = int(cx * 5 / 16) );
        cDotsPerInch    = self.getIntParam(self.ksParamGraphWizDpi,    iMin =  64, iMax =  512, iDefault = 96);
        cPtFont         = self.getIntParam(self.ksParamGraphWizFontSize, iMin = 6, iMax =  32,  iDefault = 8);
        fErrorBarY      = self.getBoolParam(self.ksParamGraphWizErrorBarY, fDefault = False);
        cMaxErrorBarY   = self.getIntParam(self.ksParamGraphWizMaxErrorBarY, iMin = 8, iMax = 9999999, iDefault = 18);
        cMaxPerGraph    = self.getIntParam(self.ksParamGraphWizMaxPerGraph, iMin = 1, iMax = 24, iDefault = 8);
        fXkcdStyle      = self.getBoolParam(self.ksParamGraphWizXkcdStyle, fDefault = False);
        fTabular        = self.getBoolParam(self.ksParamGraphWizTabular, fDefault = False);
        idSrcTestSet    = self.getIntParam(self.ksParamGraphWizSrcTestSetId, iDefault = None);
        self._checkForUnknownParameters();

        dParams = \
        {
            self.ksParamEffectiveDate:          tsEffective,
            self.ksParamReportPeriods:          cPeriods,
            self.ksParamReportPeriodInHours:    cHoursPerPeriod,
            self.ksParamReportSubject:          sSubject,
            self.ksParamReportSubjectIds:       asSubjectIds,
            self.ksParamGraphWizTestBoxIds:     aidTestBoxes,
            self.ksParamGraphWizBuildCatIds:    aidBuildCats,
            self.ksParamGraphWizTestCaseIds:    aidTestCases,
            self.ksParamGraphWizSepTestVars:    fSepTestVars,

            self.ksParamGraphWizImpl:           enmGraphImpl,
            self.ksParamGraphWizWidth:          cx,
            self.ksParamGraphWizHeight:         cy,
            self.ksParamGraphWizDpi:            cDotsPerInch,
            self.ksParamGraphWizFontSize:       cPtFont,
            self.ksParamGraphWizErrorBarY:      fErrorBarY,
            self.ksParamGraphWizMaxErrorBarY:   cMaxErrorBarY,
            self.ksParamGraphWizMaxPerGraph:    cMaxPerGraph,
            self.ksParamGraphWizXkcdStyle:      fXkcdStyle,
            self.ksParamGraphWizTabular:        fTabular,
            self.ksParamGraphWizSrcTestSetId:   idSrcTestSet,
        };

        oModel   = ReportGraphModel(self._oDb, tsEffective, cPeriods, cHoursPerPeriod, sSubject, asSubjectIds,
                                    aidTestBoxes, aidBuildCats, aidTestCases, fSepTestVars);
        oContent = WuiGraphWiz(oModel, dParams, fSubReport = False, fnDPrint = self._oSrvGlue.dprint, oDisp = self);
        (self._sPageTitle, self._sPageBody) = oContent.show();
        return True;

    def _actionVcsHistoryTooltip(self):
        """
        Version control system history.
        """
        from testmanager.webui.wuivcshistory import WuiVcsHistoryTooltip;
        from testmanager.core.vcsrevisions   import VcsRevisionLogic;

        self._sTemplate = 'template-tooltip.html';
        iRevision   = self.getIntParam(self.ksParamVcsHistoryRevision, iMin = 0, iMax = 999999999);
        sRepository = self.getStringParam(self.ksParamVcsHistoryRepository);
        cEntries    = self.getIntParam(self.ksParamVcsHistoryEntries, iMin = 1, iMax = 1024, iDefault = 8);
        self._checkForUnknownParameters();

        aoEntries = VcsRevisionLogic(self._oDb).fetchTimeline(sRepository, iRevision, cEntries);
        oContent  = WuiVcsHistoryTooltip(aoEntries, sRepository, iRevision, cEntries,
                                         fnDPrint = self._oSrvGlue.dprint, oDisp = self);
        (self._sPageTitle, self._sPageBody) = oContent.show();
        return True;

