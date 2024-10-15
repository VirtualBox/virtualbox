# -*- coding: utf-8 -*-
# $Id: wuiadmintestbox.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - TestBox.
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
import socket;

# Validation Kit imports.
from common                             import utils, webutils;
from testmanager.webui.wuicontentbase   import WuiContentBase, WuiListContentWithActionBase, WuiFormContentBase, WuiLinkBase, \
                                               WuiSvnLink, WuiTmLink, WuiSpanText, WuiRawHtml;
from testmanager.core.db                import TMDatabaseConnection;
from testmanager.core.schedgroup        import SchedGroupLogic, SchedGroupData;
from testmanager.core.testbox           import TestBoxData, TestBoxDataEx, TestBoxLogic;
from testmanager.core.testset           import TestSetData;
from testmanager.core.db                import isDbTimestampInfinity;



class WuiTestBoxDetailsLinkById(WuiTmLink):
    """  Test box details link by ID. """

    def __init__(self, idTestBox, sName = WuiContentBase.ksShortDetailsLink, fBracketed = False, tsNow = None, sTitle = None):
        from testmanager.webui.wuiadmin import WuiAdmin;
        dParams = {
            WuiAdmin.ksParamAction:             WuiAdmin.ksActionTestBoxDetails,
            TestBoxData.ksParam_idTestBox:      idTestBox,
        };
        if tsNow is not None:
            dParams[WuiAdmin.ksParamEffectiveDate] = tsNow; ## ??
        WuiTmLink.__init__(self, sName, WuiAdmin.ksScriptName, dParams, fBracketed = fBracketed, sTitle = sTitle);
        self.idTestBox = idTestBox;


class WuiTestBoxDetailsLink(WuiTestBoxDetailsLinkById):
    """  Test box details link by TestBoxData instance. """

    def __init__(self, oTestBox, sName = None, fBracketed = False, tsNow = None): # (TestBoxData, str, bool, Any) -> None
        WuiTestBoxDetailsLinkById.__init__(self, oTestBox.idTestBox,
                                           sName if sName else oTestBox.sName,
                                           fBracketed = fBracketed,
                                           tsNow = tsNow,
                                           sTitle = self.formatTitleText(oTestBox));
        self.oTestBox = oTestBox;

    @staticmethod
    def formatTitleText(oTestBox): # (TestBoxData) -> str
        """
        Formats the title text for a TestBoxData object.
        """

        # Note! Somewhat similar code is found in testresults.py

        #
        # Collect field/value tuples.
        #
        aasTestBoxTitle = [
            (u'Identifier:',  '#%u' % (oTestBox.idTestBox,),),
            (u'Name:',        oTestBox.sName,),
        ];
        if oTestBox.sCpuVendor:
            aasTestBoxTitle.append((u'CPU\u00a0vendor:',    oTestBox.sCpuVendor, ));
        if oTestBox.sCpuName:
            aasTestBoxTitle.append((u'CPU\u00a0name:',      u'\u00a0'.join(oTestBox.sCpuName.split()),));
        if oTestBox.cCpus:
            aasTestBoxTitle.append((u'CPU\u00a0threads:',   u'%s' % ( oTestBox.cCpus, ),));

        asFeatures = [];
        if oTestBox.fCpuHwVirt       is True:
            if oTestBox.sCpuVendor is None:
                asFeatures.append(u'HW\u2011Virt');
            elif oTestBox.sCpuVendor in ['AuthenticAMD',]:
                asFeatures.append(u'HW\u2011Virt(AMD\u2011V)');
            else:
                asFeatures.append(u'HW\u2011Virt(VT\u2011x)');
        if oTestBox.fCpuNestedPaging is True: asFeatures.append(u'Nested\u2011Paging');
        if oTestBox.fNativeApi       is True: asFeatures.append(u'Native\u2011API');
        if oTestBox.fCpu64BitGuest   is True: asFeatures.append(u'64\u2011bit\u2011Guest');
        if oTestBox.fChipsetIoMmu    is True: asFeatures.append(u'I/O\u2011MMU');
        aasTestBoxTitle.append((u'CPU\u00a0features:',      u',\u00a0'.join(asFeatures),));

        if oTestBox.cMbMemory:
            aasTestBoxTitle.append((u'System\u00a0RAM:',    u'%s MiB' % ( oTestBox.cMbMemory, ),));
        if oTestBox.sOs:
            aasTestBoxTitle.append((u'OS:',                 oTestBox.sOs, ));
        if oTestBox.sCpuArch:
            aasTestBoxTitle.append((u'OS\u00a0arch:',       oTestBox.sCpuArch,));
        if oTestBox.sOsVersion:
            aasTestBoxTitle.append((u'OS\u00a0version:',    u'\u00a0'.join(oTestBox.sOsVersion.split()),));
        if oTestBox.ip:
            aasTestBoxTitle.append((u'IP\u00a0address:',    u'%s' % ( oTestBox.ip, ),));

        #
        # Do a guestimation of the max field name width and pad short
        # names when constructing the title text lines.
        #
        cchMaxWidth = 0;
        for sEntry, _ in aasTestBoxTitle:
            cchMaxWidth = max(WuiTestBoxDetailsLink.estimateStringWidth(sEntry), cchMaxWidth);
        asTestBoxTitle = [];
        for sEntry, sValue in aasTestBoxTitle:
            asTestBoxTitle.append(u'%s%s\t\t%s'
                                  % (sEntry, WuiTestBoxDetailsLink.getStringWidthPadding(sEntry, cchMaxWidth), sValue));

        return u'\n'.join(asTestBoxTitle);


class WuiTestBoxDetailsLinkShort(WuiTestBoxDetailsLink):
    """  Test box details link by TestBoxData instance, but with ksShortDetailsLink as default name. """

    def __init__(self, oTestBox, sName = WuiContentBase.ksShortDetailsLink, fBracketed = False,
                 tsNow = None): # (TestBoxData, str, bool, Any) -> None
        WuiTestBoxDetailsLink.__init__(self, oTestBox, sName = sName, fBracketed = fBracketed, tsNow = tsNow);


class WuiTestBox(WuiFormContentBase):
    """
    WUI TestBox Form Content Generator.
    """

    def __init__(self, oData, sMode, oDisp):
        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'Create TextBox';
            if oData.uuidSystem is not None and len(oData.uuidSystem) > 10:
                sTitle += ' - ' + oData.uuidSystem;
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Edit TestBox - %s (#%s)' % (oData.sName, oData.idTestBox);
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'TestBox - %s (#%s)' % (oData.sName, oData.idTestBox);
        WuiFormContentBase.__init__(self, oData, sMode, 'TestBox', oDisp, sTitle);

        # Try enter sName as hostname (no domain) when creating the testbox.
        if    sMode == WuiFormContentBase.ksMode_Add  \
          and self._oData.sName in [None, ''] \
          and self._oData.ip not in [None, '']:
            try:
                (self._oData.sName, _, _) = socket.gethostbyaddr(self._oData.ip);
            except:
                pass;
            offDot = self._oData.sName.find('.');
            if offDot > 0:
                self._oData.sName = self._oData.sName[:offDot];


    def _populateForm(self, oForm, oData):
        oForm.addIntRO(      TestBoxData.ksParam_idTestBox,         oData.idTestBox, 'TestBox ID');
        oForm.addIntRO(      TestBoxData.ksParam_idGenTestBox,      oData.idGenTestBox, 'TestBox generation ID');
        oForm.addTimestampRO(TestBoxData.ksParam_tsEffective,       oData.tsEffective, 'Last changed');
        oForm.addTimestampRO(TestBoxData.ksParam_tsExpire,          oData.tsExpire, 'Expires (excl)');
        oForm.addIntRO(      TestBoxData.ksParam_uidAuthor,         oData.uidAuthor, 'Changed by UID');

        oForm.addText(       TestBoxData.ksParam_ip,                oData.ip, 'TestBox IP Address'); ## make read only??
        oForm.addUuid(       TestBoxData.ksParam_uuidSystem,        oData.uuidSystem, 'TestBox System/Firmware UUID');
        oForm.addText(       TestBoxData.ksParam_sName,             oData.sName, 'TestBox Name');
        oForm.addText(       TestBoxData.ksParam_sDescription,      oData.sDescription, 'TestBox Description');
        oForm.addCheckBox(   TestBoxData.ksParam_fEnabled,          oData.fEnabled, 'Enabled');
        oForm.addComboBox(   TestBoxData.ksParam_enmLomKind,        oData.enmLomKind, 'Lights-out-management',
                             TestBoxData.kaoLomKindDescs);
        oForm.addText(       TestBoxData.ksParam_ipLom,             oData.ipLom, 'Lights-out-management IP Address');
        oForm.addInt(        TestBoxData.ksParam_pctScaleTimeout,   oData.pctScaleTimeout, 'Timeout scale factor (%)');

        oForm.addListOfSchedGroupsForTestBox(TestBoxDataEx.ksParam_aoInSchedGroups,
                                             oData.aoInSchedGroups,
                                             SchedGroupLogic(TMDatabaseConnection()).fetchOrderedByName(),
                                             'Scheduling Group', oData.idTestBox);
        # Command, comment and submit button.
        if self._sMode == WuiFormContentBase.ksMode_Edit:
            oForm.addComboBox(TestBoxData.ksParam_enmPendingCmd,    oData.enmPendingCmd, 'Pending command',
                              TestBoxData.kaoTestBoxCmdDescs);
        else:
            oForm.addComboBoxRO(TestBoxData.ksParam_enmPendingCmd,  oData.enmPendingCmd, 'Pending command',
                                TestBoxData.kaoTestBoxCmdDescs);
        oForm.addMultilineText(TestBoxData.ksParam_sComment,        oData.sComment, 'Comment');
        if self._sMode != WuiFormContentBase.ksMode_Show:
            oForm.addSubmit('Create TestBox' if self._sMode == WuiFormContentBase.ksMode_Add else 'Change TestBox');

        return True;


    def _generatePostFormContent(self, oData):
        from testmanager.webui.wuihlpform import WuiHlpForm;

        oForm = WuiHlpForm('testbox-machine-settable', '', fReadOnly = True);
        oForm.addTextRO(     TestBoxData.ksParam_sOs,               oData.sOs, 'TestBox OS');
        oForm.addTextRO(     TestBoxData.ksParam_sOsVersion,        oData.sOsVersion, 'TestBox OS version');
        oForm.addTextRO(     TestBoxData.ksParam_sCpuArch,          oData.sCpuArch, 'TestBox OS kernel architecture');
        oForm.addTextRO(     TestBoxData.ksParam_sCpuVendor,        oData.sCpuVendor, 'TestBox CPU vendor');
        oForm.addTextRO(     TestBoxData.ksParam_sCpuName,          oData.sCpuName, 'TestBox CPU name');
        if oData.lCpuRevision:
            oForm.addTextRO( TestBoxData.ksParam_lCpuRevision,      '%#x' % (oData.lCpuRevision,), 'TestBox CPU revision',
                             sPostHtml = ' (family=%#x model=%#x stepping=%#x)'
                                       % (oData.getCpuFamily(), oData.getCpuModel(), oData.getCpuStepping(),),
                             sSubClass = 'long');
        else:
            oForm.addLongRO( TestBoxData.ksParam_lCpuRevision,      oData.lCpuRevision, 'TestBox CPU revision');
        oForm.addIntRO(      TestBoxData.ksParam_cCpus,             oData.cCpus, 'Number of CPUs, cores and threads');
        oForm.addCheckBoxRO( TestBoxData.ksParam_fCpuHwVirt,        oData.fCpuHwVirt, 'VT-x or AMD-V supported');
        oForm.addCheckBoxRO( TestBoxData.ksParam_fCpuNestedPaging,  oData.fCpuNestedPaging, 'Nested paging supported');
        oForm.addCheckBoxRO( TestBoxData.ksParam_fNativeApi,        oData.fNativeApi, 'Native API supported');
        oForm.addCheckBoxRO( TestBoxData.ksParam_fCpu64BitGuest,    oData.fCpu64BitGuest, '64-bit guest supported');
        oForm.addCheckBoxRO( TestBoxData.ksParam_fChipsetIoMmu,     oData.fChipsetIoMmu, 'I/O MMU supported');
        oForm.addMultilineTextRO(TestBoxData.ksParam_sReport,       oData.sReport, 'Hardware/software report');
        oForm.addLongRO(     TestBoxData.ksParam_cMbMemory,         oData.cMbMemory, 'Installed RAM size (MB)');
        oForm.addLongRO(     TestBoxData.ksParam_cMbScratch,        oData.cMbScratch, 'Available scratch space (MB)');
        oForm.addIntRO(      TestBoxData.ksParam_iTestBoxScriptRev, oData.iTestBoxScriptRev,
                             'TestBox Script SVN revision');
        sHexVer = oData.formatPythonVersion();
        oForm.addIntRO(      TestBoxData.ksParam_iPythonHexVersion, oData.iPythonHexVersion,
                             'Python version (hex)', sPostHtml = webutils.escapeElem(sHexVer));
        return [('Machine Only Settables', oForm.finalize()),];



class WuiTestBoxList(WuiListContentWithActionBase):
    """
    WUI TestBox List Content Generator.
    """

    ## Descriptors for the combo box.
    kasTestBoxActionDescs = \
    [ \
        [ 'none',    'Select an action...', '' ],
        [ 'enable',  'Enable',              '' ],
        [ 'disable', 'Disable',             '' ],
        TestBoxData.kaoTestBoxCmdDescs[1],
        TestBoxData.kaoTestBoxCmdDescs[2],
        TestBoxData.kaoTestBoxCmdDescs[3],
        TestBoxData.kaoTestBoxCmdDescs[4],
        TestBoxData.kaoTestBoxCmdDescs[5],
    ];

    ## Boxes which doesn't report in for more than 15 min are considered dead.
    kcSecMaxStatusDeltaAlive = 15*60

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        # type: (list[TestBoxDataForListing], int, int, datetime.datetime, ignore, WuiAdmin) -> None
        WuiListContentWithActionBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective,
                                              sTitle = 'TestBoxes', sId = 'users', fnDPrint = fnDPrint, oDisp = oDisp,
                                              aiSelectedSortColumns = aiSelectedSortColumns);
        self._asColumnHeaders.extend([ 'Name', 'LOM', 'Status', 'Cmd',
                                       'Note', 'Script', 'Python', 'Group',
                                       'OS', 'CPU', 'Features', 'CPUs', 'RAM', 'Scratch',
                                       'Actions' ]);
        self._asColumnAttribs.extend([ 'align="center"', 'align="center"', 'align="center"', 'align="center"',
                                       'align="center"', 'align="center"', 'align="center"', '',
                                       '', '', '', 'align="left"', 'align="right"', 'align="right"',
                                       'align="center"' ]);
        self._aaiColumnSorting.extend([
            (TestBoxLogic.kiSortColumn_sName,),
            None, # LOM
            (-TestBoxLogic.kiSortColumn_fEnabled, TestBoxLogic.kiSortColumn_enmState, -TestBoxLogic.kiSortColumn_tsUpdated,),
            (TestBoxLogic.kiSortColumn_enmPendingCmd,),
            None, # Note
            (TestBoxLogic.kiSortColumn_iTestBoxScriptRev,),
            (TestBoxLogic.kiSortColumn_iPythonHexVersion,),
            None, # Group
            (TestBoxLogic.kiSortColumn_sOs, TestBoxLogic.kiSortColumn_sOsVersion, TestBoxLogic.kiSortColumn_sCpuArch,),
            (TestBoxLogic.kiSortColumn_sCpuVendor, TestBoxLogic.kiSortColumn_lCpuRevision,),
            (TestBoxLogic.kiSortColumn_fCpuNestedPaging,),
            (TestBoxLogic.kiSortColumn_cCpus,),
            (TestBoxLogic.kiSortColumn_cMbMemory,),
            (TestBoxLogic.kiSortColumn_cMbScratch,),
            None, # Actions
        ]);
        assert len(self._aaiColumnSorting) == len(self._asColumnHeaders);
        self._aoActions     = list(self.kasTestBoxActionDescs);
        self._sAction       = oDisp.ksActionTestBoxListPost;
        self._sCheckboxName = TestBoxData.ksParam_idTestBox;

    def show(self, fShowNavigation = True):
        """ Adds some stats at the bottom of the page """
        (sTitle, sBody) = super(WuiTestBoxList, self).show(fShowNavigation);

        # Count boxes in interesting states.
        if self._aoEntries:
            cActive = 0;
            cDead   = 0;
            for oTestBox in self._aoEntries:
                if oTestBox.oStatus is not None:
                    oDelta = oTestBox.tsCurrent - oTestBox.oStatus.tsUpdated;
                    if oDelta.days <= 0 and oDelta.seconds <= self.kcSecMaxStatusDeltaAlive:
                        if oTestBox.fEnabled:
                            cActive += 1;
                    else:
                        cDead += 1;
                else:
                    cDead += 1;
            sBody += '<div id="testboxsummary"><p>\n' \
                     '%s testboxes of which %s are active and %s dead' \
                     '</p></div>\n' \
                     % (len(self._aoEntries), cActive, cDead,)
        return (sTitle, sBody);

    def _formatListEntry(self, iEntry): # pylint: disable=too-many-locals
        from testmanager.webui.wuiadmin import WuiAdmin;
        oEntry  = self._aoEntries[iEntry];

        # Lights outs managment.
        if oEntry.enmLomKind == TestBoxData.ksLomKind_ILOM:
            aoLom = [ WuiLinkBase('ILOM', 'https://%s/' % (oEntry.ipLom,), fBracketed = False), ];
        elif oEntry.enmLomKind == TestBoxData.ksLomKind_ELOM:
            aoLom = [ WuiLinkBase('ELOM', 'http://%s/'  % (oEntry.ipLom,), fBracketed = False), ];
        elif oEntry.enmLomKind == TestBoxData.ksLomKind_AppleXserveLom:
            aoLom = [ 'Apple LOM' ];
        elif oEntry.enmLomKind == TestBoxData.ksLomKind_None:
            aoLom = [ 'none' ];
        else:
            aoLom = [ 'Unexpected enmLomKind value "%s"' % (oEntry.enmLomKind,) ];
        if oEntry.ipLom is not None:
            if oEntry.enmLomKind in [ TestBoxData.ksLomKind_ILOM,  TestBoxData.ksLomKind_ELOM ]:
                aoLom += [ WuiLinkBase('(ssh)', 'ssh://%s' % (oEntry.ipLom,), fBracketed = False) ];
            aoLom += [ WuiRawHtml('<br>'), '%s' % (oEntry.ipLom,) ];

        # State and Last seen.
        if oEntry.oStatus is None:
            oSeen = WuiSpanText('tmspan-offline', 'Never');
            oState = '';
        else:
            oDelta = oEntry.tsCurrent - oEntry.oStatus.tsUpdated;
            if oDelta.days <= 0 and oDelta.seconds <= self.kcSecMaxStatusDeltaAlive:
                oSeen = WuiSpanText('tmspan-online',  u'%s\u00a0s\u00a0ago' % (oDelta.days * 24 * 3600 + oDelta.seconds,));
            else:
                oSeen = WuiSpanText('tmspan-offline', u'%s' % (self.formatTsShort(oEntry.oStatus.tsUpdated),));

            if oEntry.oStatus.idTestSet is None:
                oState = str(oEntry.oStatus.enmState);
            else:
                from testmanager.webui.wuimain import WuiMain;
                oState = WuiTmLink(oEntry.oStatus.enmState, WuiMain.ksScriptName,                       # pylint: disable=redefined-variable-type
                                   { WuiMain.ksParamAction: WuiMain.ksActionTestResultDetails,
                                     TestSetData.ksParam_idTestSet: oEntry.oStatus.idTestSet, },
                                   sTitle = '#%u' % (oEntry.oStatus.idTestSet,),
                                   fBracketed = False);
        # Comment
        oComment = self._formatCommentCell(oEntry.sComment);

        # Group links.
        aoGroups = [];
        for oInGroup in oEntry.aoInSchedGroups:
            oSchedGroup = oInGroup.oSchedGroup;
            aoGroups.append(WuiTmLink(oSchedGroup.sName, WuiAdmin.ksScriptName,
                                      { WuiAdmin.ksParamAction: WuiAdmin.ksActionSchedGroupEdit,
                                        SchedGroupData.ksParam_idSchedGroup: oSchedGroup.idSchedGroup, },
                                      sTitle = '#%u' % (oSchedGroup.idSchedGroup,),
                                      fBracketed = len(oEntry.aoInSchedGroups) > 1));

        # Reformat the OS version to take less space.
        aoOs = [ 'N/A' ];
        if oEntry.sOs is not None and oEntry.sOsVersion is not None and oEntry.sCpuArch:
            sOsVersion = oEntry.sOsVersion;
            if      sOsVersion[0] not in [ 'v', 'V', 'r', 'R'] \
                and sOsVersion[0].isdigit() \
                and sOsVersion.find('.') in range(4) \
                and oEntry.sOs in [ 'linux', 'solaris', 'darwin', ]:
                sOsVersion = 'v' + sOsVersion;

            sVer1 = sOsVersion;
            sVer2 = None;
            if oEntry.sOs in ('linux', 'darwin'):
                iSep = sOsVersion.find(' / ');
                if iSep > 0:
                    sVer1 = sOsVersion[:iSep].strip();
                    sVer2 = sOsVersion[iSep + 3:].strip();
                    sVer2 = sVer2.replace('Red Hat Enterprise Linux Server', 'RHEL');
                    sVer2 = sVer2.replace('Oracle Linux Server', 'OL');
            elif oEntry.sOs == 'solaris':
                iSep = sOsVersion.find(' (');
                if iSep > 0 and sOsVersion[-1] == ')':
                    sVer1 = sOsVersion[:iSep].strip();
                    sVer2 = sOsVersion[iSep + 2:-1].strip();
            elif oEntry.sOs == 'win':
                iSep = sOsVersion.find('build');
                if iSep > 0:
                    sVer1 = sOsVersion[:iSep].strip();
                    sVer2 = 'B' + sOsVersion[iSep + 1:].strip();
            aoOs = [
                WuiSpanText('tmspan-osarch', u'%s.%s' % (oEntry.sOs, oEntry.sCpuArch,)),
                WuiSpanText('tmspan-osver1', sVer1.replace('-', u'\u2011'),),
            ];
            if sVer2 is not None:
                aoOs += [ WuiRawHtml('<br>'), WuiSpanText('tmspan-osver2', sVer2.replace('-', u'\u2011')), ];

        # Format the CPU revision.
        oCpu = None;
        if oEntry.lCpuRevision is not None  and  oEntry.sCpuVendor is not None and oEntry.sCpuName is not None:
            oCpu = [
                u'%s (fam:%xh\u00a0m:%xh\u00a0s:%xh)'
                % (oEntry.sCpuVendor, oEntry.getCpuFamily(), oEntry.getCpuModel(), oEntry.getCpuStepping(),),
                WuiRawHtml('<br>'),
                oEntry.sCpuName,
            ];
        else:
            oCpu = [];
            if oEntry.sCpuVendor is not None:
                oCpu.append(oEntry.sCpuVendor);
            if oEntry.lCpuRevision is not None:
                oCpu.append('%#x' % (oEntry.lCpuRevision,));
            if oEntry.sCpuName is not None:
                oCpu.append(oEntry.sCpuName);

        # Stuff cpu vendor and cpu/box features into one field.
        asFeatures = []
        if oEntry.fCpuHwVirt       is True: asFeatures.append(u'HW\u2011Virt');
        if oEntry.fCpuNestedPaging is True: asFeatures.append(u'Nested\u2011Paging');
        if oEntry.fNativeApi       is True: asFeatures.append(u'Native\u2011API');
        if oEntry.fCpu64BitGuest   is True: asFeatures.append(u'64\u2011bit\u2011Guest');
        if oEntry.fChipsetIoMmu    is True: asFeatures.append(u'I/O\u2011MMU');
        sFeatures = u' '.join(asFeatures) if asFeatures else u'';

        # Collection applicable actions.
        aoActions = [
             WuiTmLink('Details', WuiAdmin.ksScriptName,
                       { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestBoxDetails,
                         TestBoxData.ksParam_idTestBox: oEntry.idTestBox,
                         WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, } ),
            ]

        if self._oDisp is None or not self._oDisp.isReadOnlyUser():
            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions += [
                    WuiTmLink('Edit', WuiAdmin.ksScriptName,
                              { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestBoxEdit,
                                TestBoxData.ksParam_idTestBox: oEntry.idTestBox, } ),
                    WuiTmLink('Remove', WuiAdmin.ksScriptName,
                              { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestBoxRemovePost,
                                TestBoxData.ksParam_idTestBox: oEntry.idTestBox },
                              sConfirm = 'Are you sure that you want to remove %s (%s)?' % (oEntry.sName, oEntry.ip) ),
                ]

            if oEntry.sOs not in [ 'win', 'os2', ] and oEntry.ip is not None:
                aoActions.append(WuiLinkBase('ssh', 'ssh://vbox@%s' % (oEntry.ip,),));

        return [ self._getCheckBoxColumn(iEntry, oEntry.idTestBox),
                 [ WuiSpanText('tmspan-name', oEntry.sName), WuiRawHtml('<br>'), '%s' % (oEntry.ip,),],
                 aoLom,
                 [
                     '' if oEntry.fEnabled else 'disabled / ',
                     oState,
                     WuiRawHtml('<br>'),
                     oSeen,
                  ],
                 oEntry.enmPendingCmd,
                 oComment,
                 WuiSvnLink(oEntry.iTestBoxScriptRev),
                 oEntry.formatPythonVersion(),
                 aoGroups,
                 aoOs,
                 oCpu,
                 sFeatures,
                 oEntry.cCpus            if oEntry.cCpus                is not None else 'N/A',
                 utils.formatNumberNbsp(oEntry.cMbMemory)  + u'\u00a0MB' if oEntry.cMbMemory  is not None else 'N/A',
                 utils.formatNumberNbsp(oEntry.cMbScratch) + u'\u00a0MB' if oEntry.cMbScratch is not None else 'N/A',
                 aoActions,
        ];

