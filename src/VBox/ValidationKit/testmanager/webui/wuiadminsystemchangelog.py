# -*- coding: utf-8 -*-
# $Id: wuiadminsystemchangelog.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Admin - System changelog.
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


from common import webutils;

# Validation Kit imports.
from testmanager.webui.wuicontentbase   import WuiListContentBase, WuiHtmlKeeper, WuiAdminLink, \
                                               WuiMainLink, WuiElementText, WuiHtmlBase;

from testmanager.core.base              import AttributeChangeEntryPre;
from testmanager.core.buildblacklist    import BuildBlacklistLogic, BuildBlacklistData;
from testmanager.core.build             import BuildLogic, BuildData;
from testmanager.core.buildsource       import BuildSourceLogic, BuildSourceData;
from testmanager.core.globalresource    import GlobalResourceLogic, GlobalResourceData;
from testmanager.core.failurecategory   import FailureCategoryLogic, FailureCategoryData;
from testmanager.core.failurereason     import FailureReasonLogic, FailureReasonData;
from testmanager.core.systemlog         import SystemLogData;
from testmanager.core.systemchangelog   import SystemChangelogLogic;
from testmanager.core.schedgroup        import SchedGroupLogic, SchedGroupData;
from testmanager.core.testbox           import TestBoxLogic, TestBoxData;
from testmanager.core.testcase          import TestCaseLogic, TestCaseData;
from testmanager.core.testgroup         import TestGroupLogic, TestGroupData;
from testmanager.core.testset           import TestSetData;
from testmanager.core.useraccount       import UserAccountLogic, UserAccountData;


class WuiAdminSystemChangelogList(WuiListContentBase):
    """
    WUI System Changelog Content Generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, cDaysBack, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, 'System Changelog',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);
        self._asColumnHeaders = [ 'When', 'User', 'Event', 'Details' ];
        self._asColumnAttribs = [ 'align="center"', 'align="center"', '', '' ];
        self._oBuildBlacklistLogic  = BuildBlacklistLogic(oDisp.getDb());
        self._oBuildLogic           = BuildLogic(oDisp.getDb());
        self._oBuildSourceLogic     = BuildSourceLogic(oDisp.getDb());
        self._oFailureCategoryLogic = FailureCategoryLogic(oDisp.getDb());
        self._oFailureReasonLogic   = FailureReasonLogic(oDisp.getDb());
        self._oGlobalResourceLogic  = GlobalResourceLogic(oDisp.getDb());
        self._oSchedGroupLogic      = SchedGroupLogic(oDisp.getDb());
        self._oTestBoxLogic         = TestBoxLogic(oDisp.getDb());
        self._oTestCaseLogic        = TestCaseLogic(oDisp.getDb());
        self._oTestGroupLogic       = TestGroupLogic(oDisp.getDb());
        self._oUserAccountLogic     = UserAccountLogic(oDisp.getDb());
        self._sPrevDate             = '';
        _ = cDaysBack;

    #   oDetails = self._createBlacklistingDetailsLink(oEntry.idWhat, oEntry.tsEffective);
    def _createBlacklistingDetailsLink(self, idBlacklisting, tsEffective):
        """ Creates a link to the build source details. """
        oBlacklisting = self._oBuildBlacklistLogic.cachedLookup(idBlacklisting);
        if oBlacklisting is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink('Blacklisting #%u' % (oBlacklisting.idBlacklisting,),
                                WuiAdmin.ksActionBuildBlacklistDetails, tsEffective,
                                { BuildBlacklistData.ksParam_idBlacklisting: oBlacklisting.idBlacklisting },
                                fBracketed = False);
        return WuiElementText('[blacklisting #%u not found]' % (idBlacklisting,));

    def _createBuildDetailsLink(self, idBuild, tsEffective):
        """ Creates a link to the build details. """
        oBuild = self._oBuildLogic.cachedLookup(idBuild);
        if oBuild is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink('%s %sr%u' % ( oBuild.oCat.sProduct, oBuild.sVersion, oBuild.iRevision),
                                WuiAdmin.ksActionBuildDetails, tsEffective,
                                { BuildData.ksParam_idBuild: oBuild.idBuild },
                                fBracketed = False,
                                sTitle = 'build #%u for %s, type %s'
                                       % (oBuild.idBuild, ' & '.join(oBuild.oCat.asOsArches), oBuild.oCat.sType));
        return WuiElementText('[build #%u not found]' % (idBuild,));

    def _createBuildSourceDetailsLink(self, idBuildSrc, tsEffective):
        """ Creates a link to the build source details. """
        oBuildSource = self._oBuildSourceLogic.cachedLookup(idBuildSrc);
        if oBuildSource is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink(oBuildSource.sName, WuiAdmin.ksActionBuildSrcDetails, tsEffective,
                                { BuildSourceData.ksParam_idBuildSrc: oBuildSource.idBuildSrc },
                                fBracketed = False,
                                sTitle = 'Build source #%u' % (oBuildSource.idBuildSrc,));
        return WuiElementText('[build source #%u not found]' % (idBuildSrc,));

    def _createFailureCategoryDetailsLink(self, idFailureCategory, tsEffective):
        """ Creates a link to the failure category details. """
        oFailureCategory = self._oFailureCategoryLogic.cachedLookup(idFailureCategory);
        if oFailureCategory is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink(oFailureCategory.sShort, WuiAdmin.ksActionFailureCategoryDetails, tsEffective,
                                { FailureCategoryData.ksParam_idFailureCategory: oFailureCategory.idFailureCategory },
                                fBracketed = False,
                                sTitle = 'Failure category #%u' % (oFailureCategory.idFailureCategory,));
        return WuiElementText('[failure category #%u not found]' % (idFailureCategory,));

    def _createFailureReasonDetailsLink(self, idFailureReason, tsEffective):
        """ Creates a link to the failure reason details. """
        oFailureReason = self._oFailureReasonLogic.cachedLookup(idFailureReason);
        if oFailureReason is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink(oFailureReason.sShort, WuiAdmin.ksActionFailureReasonDetails, tsEffective,
                                { FailureReasonData.ksParam_idFailureReason: oFailureReason.idFailureReason },
                                fBracketed = False,
                                sTitle = 'Failure reason #%u, category %s'
                                       % (oFailureReason.idFailureReason, oFailureReason.oCategory.sShort));
        return WuiElementText('[failure reason #%u not found]' % (idFailureReason,));

    def _createGlobalResourceDetailsLink(self, idGlobalRsrc, tsEffective):
        """ Creates a link to the global resource details. """
        oGlobalResource = self._oGlobalResourceLogic.cachedLookup(idGlobalRsrc);
        if oGlobalResource is not None:
            return WuiAdminLink(oGlobalResource.sName, '@todo', tsEffective,
                                { GlobalResourceData.ksParam_idGlobalRsrc: oGlobalResource.idGlobalRsrc },
                                fBracketed = False,
                                sTitle = 'Global resource #%u' % (oGlobalResource.idGlobalRsrc,));
        return WuiElementText('[global resource #%u not found]' % (idGlobalRsrc,));

    def _createSchedGroupDetailsLink(self, idSchedGroup, tsEffective):
        """ Creates a link to the scheduling group details. """
        oSchedGroup = self._oSchedGroupLogic.cachedLookup(idSchedGroup);
        if oSchedGroup is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink(oSchedGroup.sName, WuiAdmin.ksActionSchedGroupDetails, tsEffective,
                                { SchedGroupData.ksParam_idSchedGroup: oSchedGroup.idSchedGroup },
                                fBracketed = False,
                                sTitle = 'Scheduling group #%u' % (oSchedGroup.idSchedGroup,));
        return WuiElementText('[scheduling group #%u not found]' % (idSchedGroup,));

    def _createTestBoxDetailsLink(self, idTestBox, tsEffective):
        """ Creates a link to the testbox details. """
        oTestBox = self._oTestBoxLogic.cachedLookup(idTestBox);
        if oTestBox is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink(oTestBox.sName, WuiAdmin.ksActionTestBoxDetails, tsEffective,
                                { TestBoxData.ksParam_idTestBox: oTestBox.idTestBox },
                                fBracketed = False, sTitle = 'Testbox #%u' % (oTestBox.idTestBox,));
        return WuiElementText('[testbox #%u not found]' % (idTestBox,));

    def _createTestCaseDetailsLink(self, idTestCase, tsEffective):
        """ Creates a link to the test case details. """
        oTestCase = self._oTestCaseLogic.cachedLookup(idTestCase);
        if oTestCase is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink(oTestCase.sName, WuiAdmin.ksActionTestCaseDetails, tsEffective,
                                { TestCaseData.ksParam_idTestCase: oTestCase.idTestCase },
                                fBracketed = False, sTitle = 'Test case #%u' % (oTestCase.idTestCase,));
        return WuiElementText('[test case #%u not found]' % (idTestCase,));

    def _createTestGroupDetailsLink(self, idTestGroup, tsEffective):
        """ Creates a link to the test group details. """
        oTestGroup = self._oTestGroupLogic.cachedLookup(idTestGroup);
        if oTestGroup is not None:
            from testmanager.webui.wuiadmin import WuiAdmin;
            return WuiAdminLink(oTestGroup.sName, WuiAdmin.ksActionTestGroupDetails, tsEffective,
                                { TestGroupData.ksParam_idTestGroup: oTestGroup.idTestGroup },
                                fBracketed = False, sTitle = 'Test group #%u' % (oTestGroup.idTestGroup,));
        return WuiElementText('[test group #%u not found]' % (idTestGroup,));

    def _createTestSetResultsDetailsLink(self, idTestSet, tsEffective):
        """ Creates a link to the test set results. """
        _ = tsEffective;
        from testmanager.webui.wuimain import WuiMain;
        return WuiMainLink('test set #%u' % idTestSet, WuiMain.ksActionTestSetDetails,
                           { TestSetData.ksParam_idTestSet: idTestSet }, fBracketed = False);

    def _createTestSetDetailsLinkByResult(self, idTestResult, tsEffective):
        """ Creates a link to the test set results. """
        _ = tsEffective;
        from testmanager.webui.wuimain import WuiMain;
        return WuiMainLink('test result #%u' % idTestResult, WuiMain.ksActionTestSetDetailsFromResult,
                           { TestSetData.ksParam_idTestResult: idTestResult }, fBracketed = False);

    def _createUserAccountDetailsLink(self, uid, tsEffective):
        """ Creates a link to the user account details. """
        oUser = self._oUserAccountLogic.cachedLookup(uid);
        if oUser is not None:
            return WuiAdminLink(oUser.sUsername, '@todo', tsEffective, { UserAccountData.ksParam_uid: oUser.uid },
                                fBracketed = False, sTitle = '%s (#%u)' % (oUser.sFullName, oUser.uid));
        return WuiElementText('[user #%u not found]' % (uid,));

    def _formatDescGeneric(self, sDesc, oEntry):
        """
        Generically format system log the description.
        """
        oRet = WuiHtmlKeeper();
        asWords = sDesc.split();
        for sWord in asWords:
            offEqual = sWord.find('=');
            if offEqual > 0:
                sKey = sWord[:offEqual];
                try:    idValue = int(sWord[offEqual+1:].rstrip('.,'));
                except: pass;
                else:
                    if sKey == 'idTestSet':
                        oRet.append(self._createTestSetResultsDetailsLink(idValue, oEntry.tsEffective));
                        continue;
                    if sKey == 'idTestBox':
                        oRet.append(self._createTestBoxDetailsLink(idValue, oEntry.tsEffective));
                        continue;
                    if sKey == 'idSchedGroup':
                        oRet.append(self._createSchedGroupDetailsLink(idValue, oEntry.tsEffective));
                        continue;

            oRet.append(WuiElementText(sWord));
        return oRet;

    def _formatListEntryHtml(self, iEntry): # pylint: disable=too-many-statements
        """
        Overridden parent method.
        """
        oEntry    = self._aoEntries[iEntry];
        sRowClass = 'tmodd' if (iEntry + 1) & 1 else 'tmeven';
        sHtml     = u'';

        #
        # Format the timestamp.
        #
        sDate = self.formatTsShort(oEntry.tsEffective);
        if sDate[:10] != self._sPrevDate:
            self._sPrevDate = sDate[:10];
            sHtml += '  <tr class="%s tmdaterow" align="left"><td colspan="7">%s</td></tr>\n' % (sRowClass, sDate[:10],);
        sDate = sDate[11:]

        #
        # System log events.
        # pylint: disable=redefined-variable-type
        #
        aoChanges = None;
        if   oEntry.sEvent == SystemLogData.ksEvent_CmdNacked:
            sEvent = 'Command not acknowleged';
            oDetails = oEntry.sDesc;

        elif oEntry.sEvent == SystemLogData.ksEvent_TestBoxUnknown:
            sEvent = 'Unknown testbox';
            oDetails = oEntry.sDesc;

        elif oEntry.sEvent == SystemLogData.ksEvent_TestSetAbandoned:
            sEvent = 'Abandoned ' if oEntry.sDesc.startswith('idTestSet') else 'Abandoned test set';
            oDetails = self._formatDescGeneric(oEntry.sDesc, oEntry);

        elif oEntry.sEvent == SystemLogData.ksEvent_UserAccountUnknown:
            sEvent = 'Unknown user account';
            oDetails = oEntry.sDesc;

        elif oEntry.sEvent == SystemLogData.ksEvent_XmlResultMalformed:
            sEvent = 'Malformed XML result';
            oDetails = oEntry.sDesc;

        elif oEntry.sEvent == SystemLogData.ksEvent_SchedQueueRecreate:
            sEvent = 'Recreating scheduling queue';
            asWords = oEntry.sDesc.split();
            if len(asWords) > 3 and asWords[0] == 'User' and asWords[1][0] == '#':
                try:    idAuthor = int(asWords[1][1:]);
                except: pass;
                else:
                    oEntry.oAuthor = self._oUserAccountLogic.cachedLookup(idAuthor);
                    if oEntry.oAuthor is not None:
                        i = 2;
                        if asWords[i] == 'recreated':   i += 1;
                        oEntry.sDesc = ' '.join(asWords[i:]);
            oDetails = self._formatDescGeneric(oEntry.sDesc.replace('sched queue #', 'for scheduling group idSchedGroup='),
                                               oEntry);
        #
        # System changelog events.
        #
        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_Blacklisting:
            sEvent = 'Modified blacklisting';
            oDetails = self._createBlacklistingDetailsLink(oEntry.idWhat, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_Build:
            sEvent = 'Modified build';
            oDetails = self._createBuildDetailsLink(oEntry.idWhat, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_BuildSource:
            sEvent = 'Modified build source';
            oDetails = self._createBuildSourceDetailsLink(oEntry.idWhat, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_GlobalRsrc:
            sEvent = 'Modified global resource';
            oDetails = self._createGlobalResourceDetailsLink(oEntry.idWhat, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_FailureCategory:
            sEvent = 'Modified failure category';
            oDetails = self._createFailureCategoryDetailsLink(oEntry.idWhat, oEntry.tsEffective);
            (aoChanges, _) = self._oFailureCategoryLogic.fetchForChangeLog(oEntry.idWhat, 0, 1, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_FailureReason:
            sEvent = 'Modified failure reason';
            oDetails = self._createFailureReasonDetailsLink(oEntry.idWhat, oEntry.tsEffective);
            (aoChanges, _) = self._oFailureReasonLogic.fetchForChangeLog(oEntry.idWhat, 0, 1, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_SchedGroup:
            sEvent = 'Modified scheduling group';
            oDetails = self._createSchedGroupDetailsLink(oEntry.idWhat, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_TestBox:
            sEvent = 'Modified testbox';
            oDetails = self._createTestBoxDetailsLink(oEntry.idWhat, oEntry.tsEffective);
            (aoChanges, _) = self._oTestBoxLogic.fetchForChangeLog(oEntry.idWhat, 0, 1, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_TestCase:
            sEvent = 'Modified test case';
            oDetails = self._createTestCaseDetailsLink(oEntry.idWhat, oEntry.tsEffective);
            (aoChanges, _) = self._oTestCaseLogic.fetchForChangeLog(oEntry.idWhat, 0, 1, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_TestGroup:
            sEvent = 'Modified test group';
            oDetails = self._createTestGroupDetailsLink(oEntry.idWhat, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_TestResult:
            sEvent = 'Modified test failure reason';
            oDetails = self._createTestSetDetailsLinkByResult(oEntry.idWhat, oEntry.tsEffective);

        elif oEntry.sEvent == SystemChangelogLogic.ksWhat_User:
            sEvent = 'Modified user account';
            oDetails = self._createUserAccountDetailsLink(oEntry.idWhat, oEntry.tsEffective);

        else:
            sEvent   = '%s(%s)' % (oEntry.sEvent, oEntry.idWhat,);
            oDetails = '!Unknown event!' + (oEntry.sDesc if oEntry.sDesc else '');

        #
        # Do the formatting.
        #

        if aoChanges:
            oChangeEntry    = aoChanges[0];
            cAttribsChanged = len(oChangeEntry.aoChanges) + 1;
            if oChangeEntry.oOldRaw is None and sEvent.startswith('Modified '):
                sEvent = 'Created ' + sEvent[9:];

        else:
            oChangeEntry    = None;
            cAttribsChanged = -1;

        sHtml += u'  <tr class="%s">\n' \
                 u'    <td rowspan="%d" align="center" >%s</td>\n' \
                 u'    <td rowspan="%d" align="center" >%s</td>\n' \
                 u'    <td colspan="5" class="%s%s">%s %s</td>\n' \
                 u'  </tr>\n' \
               % ( sRowClass,
                  1 + cAttribsChanged + 1, sDate,
                  1 + cAttribsChanged + 1, webutils.escapeElem(oEntry.oAuthor.sUsername if oEntry.oAuthor is not None else ''),
                  sRowClass, ' tmsyschlogevent' if oChangeEntry is not None else '', webutils.escapeElem(sEvent),
                  oDetails.toHtml() if isinstance(oDetails, WuiHtmlBase) else oDetails,
                  );

        if oChangeEntry is not None:
            sHtml += u'  <tr class="%s tmsyschlogspacerrowabove">\n' \
                     u'    <td xrowspan="%d" style="border-right: 0px; border-bottom: 0px;"></td>\n' \
                     u'    <td colspan="3" style="border-right: 0px;"></td>\n' \
                     u'    <td rowspan="%d" class="%s tmsyschlogspacer"></td>\n' \
                     u'  </tr>\n' \
                   % (sRowClass, cAttribsChanged + 1, cAttribsChanged + 1, sRowClass);
            for j, oChange in enumerate(oChangeEntry.aoChanges):
                fLastRow = j + 1 == len(oChangeEntry.aoChanges);
                sHtml += u'  <tr class="%s%s tmsyschlogattr%s">\n' \
                       % ( sRowClass, 'odd' if j & 1 else 'even', ' tmsyschlogattrfinal' if fLastRow else '',);
                if j == 0:
                    sHtml += u'    <td class="%s tmsyschlogspacer" rowspan="%d"></td>\n' % (sRowClass, cAttribsChanged - 1,);

                if isinstance(oChange, AttributeChangeEntryPre):
                    sHtml += u'    <td class="%s%s">%s</td>\n' \
                             u'    <td><div class="tdpre"><pre>%s</pre></div></td>\n' \
                             u'    <td class="%s%s"><div class="tdpre"><pre>%s</pre></div></td>\n' \
                           % ( ' tmtopleft' if j == 0 else '', ' tmbottomleft' if fLastRow else '',
                               webutils.escapeElem(oChange.sAttr),
                               webutils.escapeElem(oChange.sOldText),
                               ' tmtopright' if j == 0 else '', ' tmbottomright' if fLastRow else '',
                               webutils.escapeElem(oChange.sNewText), );
                else:
                    sHtml += u'    <td class="%s%s">%s</td>\n' \
                             u'    <td>%s</td>\n' \
                             u'    <td class="%s%s">%s</td>\n' \
                           % ( ' tmtopleft' if j == 0 else '', ' tmbottomleft' if fLastRow else '',
                               webutils.escapeElem(oChange.sAttr),
                               webutils.escapeElem(oChange.sOldText),
                               ' tmtopright' if j == 0 else '', ' tmbottomright' if fLastRow else '',
                               webutils.escapeElem(oChange.sNewText), );
                sHtml += u'  </tr>\n';

        if oChangeEntry is not None:
            sHtml += u'  <tr class="%s tmsyschlogspacerrowbelow "><td colspan="5"></td></tr>\n\n' % (sRowClass,);
        return sHtml;


    def _generateTableHeaders(self):
        """
        Overridden parent method.
        """

        sHtml = u'<thead class="tmheader">\n' \
                u' <tr>\n' \
                u'  <th rowspan="2">When</th>\n' \
                u'  <th rowspan="2">Who</th>\n' \
                u'  <th colspan="5">Event</th>\n' \
                u' </tr>\n' \
                u' <tr>\n' \
                u'  <th style="border-right: 0px;"></th>\n' \
                u'  <th>Attribute</th>\n' \
                u'  <th>Old</th>\n' \
                u'  <th style="border-right: 0px;">New</th>\n' \
                u'  <th></th>\n' \
                u' </tr>\n' \
                u'</thead>\n';
        return sHtml;

