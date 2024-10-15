# -*- coding: utf-8 -*-
# $Id: wuiadmin.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Core - WUI - Admin Main page.
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
import cgitb;   # pylint: disable=deprecated-module ## @todo these will be retired in python 3.13!
import sys;

# Validation Kit imports.
from common                                    import utils, webutils;
from testmanager                               import config;
from testmanager.webui.wuibase                 import WuiDispatcherBase, WuiException


class WuiAdmin(WuiDispatcherBase):
    """
    WUI Admin main page.
    """

    ## The name of the script.
    ksScriptName = 'admin.py'

    ## Number of days back.
    ksParamDaysBack = 'cDaysBack';

    ## @name Actions
    ## @{
    ksActionSystemLogList           = 'SystemLogList'
    ksActionSystemChangelogList     = 'SystemChangelogList'
    ksActionSystemDbDump            = 'SystemDbDump'
    ksActionSystemDbDumpDownload    = 'SystemDbDumpDownload'

    ksActionUserList                = 'UserList'
    ksActionUserAdd                 = 'UserAdd'
    ksActionUserAddPost             = 'UserAddPost'
    ksActionUserEdit                = 'UserEdit'
    ksActionUserEditPost            = 'UserEditPost'
    ksActionUserDelPost             = 'UserDelPost'
    ksActionUserDetails             = 'UserDetails'

    ksActionTestBoxList             = 'TestBoxList'
    ksActionTestBoxListPost         = 'TestBoxListPost'
    ksActionTestBoxAdd              = 'TestBoxAdd'
    ksActionTestBoxAddPost          = 'TestBoxAddPost'
    ksActionTestBoxEdit             = 'TestBoxEdit'
    ksActionTestBoxEditPost         = 'TestBoxEditPost'
    ksActionTestBoxDetails          = 'TestBoxDetails'
    ksActionTestBoxRemovePost       = 'TestBoxRemove'
    ksActionTestBoxesRegenQueues    = 'TestBoxesRegenQueues';

    ksActionTestCaseList            = 'TestCaseList'
    ksActionTestCaseAdd             = 'TestCaseAdd'
    ksActionTestCaseAddPost         = 'TestCaseAddPost'
    ksActionTestCaseClone           = 'TestCaseClone'
    ksActionTestCaseDetails         = 'TestCaseDetails'
    ksActionTestCaseEdit            = 'TestCaseEdit'
    ksActionTestCaseEditPost        = 'TestCaseEditPost'
    ksActionTestCaseDoRemove        = 'TestCaseDoRemove'

    ksActionGlobalRsrcShowAll       = 'GlobalRsrcShowAll'
    ksActionGlobalRsrcShowAdd       = 'GlobalRsrcShowAdd'
    ksActionGlobalRsrcShowEdit      = 'GlobalRsrcShowEdit'
    ksActionGlobalRsrcAdd           = 'GlobalRsrcAddPost'
    ksActionGlobalRsrcEdit          = 'GlobalRsrcEditPost'
    ksActionGlobalRsrcDel           = 'GlobalRsrcDelPost'

    ksActionBuildList               = 'BuildList'
    ksActionBuildAdd                = 'BuildAdd'
    ksActionBuildAddPost            = 'BuildAddPost'
    ksActionBuildClone              = 'BuildClone'
    ksActionBuildDetails            = 'BuildDetails'
    ksActionBuildDoRemove           = 'BuildDoRemove'
    ksActionBuildEdit               = 'BuildEdit'
    ksActionBuildEditPost           = 'BuildEditPost'

    ksActionBuildBlacklist          = 'BuildBlacklist';
    ksActionBuildBlacklistAdd       = 'BuildBlacklistAdd';
    ksActionBuildBlacklistAddPost   = 'BuildBlacklistAddPost';
    ksActionBuildBlacklistClone     = 'BuildBlacklistClone';
    ksActionBuildBlacklistDetails   = 'BuildBlacklistDetails';
    ksActionBuildBlacklistDoRemove  = 'BuildBlacklistDoRemove';
    ksActionBuildBlacklistEdit      = 'BuildBlacklistEdit';
    ksActionBuildBlacklistEditPost  = 'BuildBlacklistEditPost';

    ksActionFailureCategoryList     = 'FailureCategoryList';
    ksActionFailureCategoryAdd      = 'FailureCategoryAdd';
    ksActionFailureCategoryAddPost  = 'FailureCategoryAddPost';
    ksActionFailureCategoryDetails  = 'FailureCategoryDetails';
    ksActionFailureCategoryDoRemove = 'FailureCategoryDoRemove';
    ksActionFailureCategoryEdit     = 'FailureCategoryEdit';
    ksActionFailureCategoryEditPost = 'FailureCategoryEditPost';

    ksActionFailureReasonList       = 'FailureReasonList'
    ksActionFailureReasonAdd        = 'FailureReasonAdd'
    ksActionFailureReasonAddPost    = 'FailureReasonAddPost'
    ksActionFailureReasonDetails    = 'FailureReasonDetails'
    ksActionFailureReasonDoRemove   = 'FailureReasonDoRemove'
    ksActionFailureReasonEdit       = 'FailureReasonEdit'
    ksActionFailureReasonEditPost   = 'FailureReasonEditPost'

    ksActionBuildSrcList            = 'BuildSrcList'
    ksActionBuildSrcAdd             = 'BuildSrcAdd'
    ksActionBuildSrcAddPost         = 'BuildSrcAddPost'
    ksActionBuildSrcClone           = 'BuildSrcClone'
    ksActionBuildSrcDetails         = 'BuildSrcDetails'
    ksActionBuildSrcEdit            = 'BuildSrcEdit'
    ksActionBuildSrcEditPost        = 'BuildSrcEditPost'
    ksActionBuildSrcDoRemove        = 'BuildSrcDoRemove'

    ksActionBuildCategoryList       = 'BuildCategoryList'
    ksActionBuildCategoryAdd        = 'BuildCategoryAdd'
    ksActionBuildCategoryAddPost    = 'BuildCategoryAddPost'
    ksActionBuildCategoryClone      = 'BuildCategoryClone';
    ksActionBuildCategoryDetails    = 'BuildCategoryDetails';
    ksActionBuildCategoryDoRemove   = 'BuildCategoryDoRemove';

    ksActionTestGroupList           = 'TestGroupList'
    ksActionTestGroupAdd            = 'TestGroupAdd'
    ksActionTestGroupAddPost        = 'TestGroupAddPost'
    ksActionTestGroupClone          = 'TestGroupClone'
    ksActionTestGroupDetails        = 'TestGroupDetails'
    ksActionTestGroupDoRemove       = 'TestGroupDoRemove'
    ksActionTestGroupEdit           = 'TestGroupEdit'
    ksActionTestGroupEditPost       = 'TestGroupEditPost'
    ksActionTestCfgRegenQueues      = 'TestCfgRegenQueues'

    ksActionSchedGroupList          = 'SchedGroupList'
    ksActionSchedGroupAdd           = 'SchedGroupAdd';
    ksActionSchedGroupAddPost       = 'SchedGroupAddPost';
    ksActionSchedGroupClone         = 'SchedGroupClone';
    ksActionSchedGroupDetails       = 'SchedGroupDetails';
    ksActionSchedGroupDoRemove      = 'SchedGroupDel';
    ksActionSchedGroupEdit          = 'SchedGroupEdit';
    ksActionSchedGroupEditPost      = 'SchedGroupEditPost';
    ksActionSchedQueueList          = 'SchedQueueList';
    ## @}

    def __init__(self, oSrvGlue): # pylint: disable=too-many-locals,too-many-statements
        WuiDispatcherBase.__init__(self, oSrvGlue, self.ksScriptName);
        self._sTemplate     = 'template.html';


        #
        # System actions.
        #
        self._dDispatch[self.ksActionSystemChangelogList]       = self._actionSystemChangelogList;
        self._dDispatch[self.ksActionSystemLogList]             = self._actionSystemLogList;
        self._dDispatch[self.ksActionSystemDbDump]              = self._actionSystemDbDump;
        self._dDispatch[self.ksActionSystemDbDumpDownload]      = self._actionSystemDbDumpDownload;

        #
        # User Account actions.
        #
        self._dDispatch[self.ksActionUserList]                  = self._actionUserList;
        self._dDispatch[self.ksActionUserAdd]                   = self._actionUserAdd;
        self._dDispatch[self.ksActionUserEdit]                  = self._actionUserEdit;
        self._dDispatch[self.ksActionUserAddPost]               = self._actionUserAddPost;
        self._dDispatch[self.ksActionUserEditPost]              = self._actionUserEditPost;
        self._dDispatch[self.ksActionUserDetails]               = self._actionUserDetails;
        self._dDispatch[self.ksActionUserDelPost]               = self._actionUserDelPost;

        #
        # TestBox actions.
        #
        self._dDispatch[self.ksActionTestBoxList]               = self._actionTestBoxList;
        self._dDispatch[self.ksActionTestBoxListPost]           = self._actionTestBoxListPost;
        self._dDispatch[self.ksActionTestBoxAdd]                = self._actionTestBoxAdd;
        self._dDispatch[self.ksActionTestBoxAddPost]            = self._actionTestBoxAddPost;
        self._dDispatch[self.ksActionTestBoxDetails]            = self._actionTestBoxDetails;
        self._dDispatch[self.ksActionTestBoxEdit]               = self._actionTestBoxEdit;
        self._dDispatch[self.ksActionTestBoxEditPost]           = self._actionTestBoxEditPost;
        self._dDispatch[self.ksActionTestBoxRemovePost]         = self._actionTestBoxRemovePost;
        self._dDispatch[self.ksActionTestBoxesRegenQueues]      = self._actionRegenQueuesCommon;

        #
        # Test Case actions.
        #
        self._dDispatch[self.ksActionTestCaseList]              = self._actionTestCaseList;
        self._dDispatch[self.ksActionTestCaseAdd]               = self._actionTestCaseAdd;
        self._dDispatch[self.ksActionTestCaseAddPost]           = self._actionTestCaseAddPost;
        self._dDispatch[self.ksActionTestCaseClone]             = self._actionTestCaseClone;
        self._dDispatch[self.ksActionTestCaseDetails]           = self._actionTestCaseDetails;
        self._dDispatch[self.ksActionTestCaseEdit]              = self._actionTestCaseEdit;
        self._dDispatch[self.ksActionTestCaseEditPost]          = self._actionTestCaseEditPost;
        self._dDispatch[self.ksActionTestCaseDoRemove]          = self._actionTestCaseDoRemove;

        #
        # Global Resource actions
        #
        self._dDispatch[self.ksActionGlobalRsrcShowAll]         = self._actionGlobalRsrcShowAll;
        self._dDispatch[self.ksActionGlobalRsrcShowAdd]         = self._actionGlobalRsrcShowAdd;
        self._dDispatch[self.ksActionGlobalRsrcShowEdit]        = self._actionGlobalRsrcShowEdit;
        self._dDispatch[self.ksActionGlobalRsrcAdd]             = self._actionGlobalRsrcAdd;
        self._dDispatch[self.ksActionGlobalRsrcEdit]            = self._actionGlobalRsrcEdit;
        self._dDispatch[self.ksActionGlobalRsrcDel]             = self._actionGlobalRsrcDel;

        #
        # Build Source actions
        #
        self._dDispatch[self.ksActionBuildSrcList]              = self._actionBuildSrcList;
        self._dDispatch[self.ksActionBuildSrcAdd]               = self._actionBuildSrcAdd;
        self._dDispatch[self.ksActionBuildSrcAddPost]           = self._actionBuildSrcAddPost;
        self._dDispatch[self.ksActionBuildSrcClone]             = self._actionBuildSrcClone;
        self._dDispatch[self.ksActionBuildSrcDetails]           = self._actionBuildSrcDetails;
        self._dDispatch[self.ksActionBuildSrcDoRemove]          = self._actionBuildSrcDoRemove;
        self._dDispatch[self.ksActionBuildSrcEdit]              = self._actionBuildSrcEdit;
        self._dDispatch[self.ksActionBuildSrcEditPost]          = self._actionBuildSrcEditPost;

        #
        # Build actions
        #
        self._dDispatch[self.ksActionBuildList]                 = self._actionBuildList;
        self._dDispatch[self.ksActionBuildAdd]                  = self._actionBuildAdd;
        self._dDispatch[self.ksActionBuildAddPost]              = self._actionBuildAddPost;
        self._dDispatch[self.ksActionBuildClone]                = self._actionBuildClone;
        self._dDispatch[self.ksActionBuildDetails]              = self._actionBuildDetails;
        self._dDispatch[self.ksActionBuildDoRemove]             = self._actionBuildDoRemove;
        self._dDispatch[self.ksActionBuildEdit]                 = self._actionBuildEdit;
        self._dDispatch[self.ksActionBuildEditPost]             = self._actionBuildEditPost;

        #
        # Build Black List actions
        #
        self._dDispatch[self.ksActionBuildBlacklist]            = self._actionBuildBlacklist;
        self._dDispatch[self.ksActionBuildBlacklistAdd]         = self._actionBuildBlacklistAdd;
        self._dDispatch[self.ksActionBuildBlacklistAddPost]     = self._actionBuildBlacklistAddPost;
        self._dDispatch[self.ksActionBuildBlacklistClone]       = self._actionBuildBlacklistClone;
        self._dDispatch[self.ksActionBuildBlacklistDetails]     = self._actionBuildBlacklistDetails;
        self._dDispatch[self.ksActionBuildBlacklistDoRemove]    = self._actionBuildBlacklistDoRemove;
        self._dDispatch[self.ksActionBuildBlacklistEdit]        = self._actionBuildBlacklistEdit;
        self._dDispatch[self.ksActionBuildBlacklistEditPost]    = self._actionBuildBlacklistEditPost;

        #
        # Failure Category actions
        #
        self._dDispatch[self.ksActionFailureCategoryList]       = self._actionFailureCategoryList;
        self._dDispatch[self.ksActionFailureCategoryAdd]        = self._actionFailureCategoryAdd;
        self._dDispatch[self.ksActionFailureCategoryAddPost]    = self._actionFailureCategoryAddPost;
        self._dDispatch[self.ksActionFailureCategoryDetails]    = self._actionFailureCategoryDetails;
        self._dDispatch[self.ksActionFailureCategoryDoRemove]   = self._actionFailureCategoryDoRemove;
        self._dDispatch[self.ksActionFailureCategoryEdit]       = self._actionFailureCategoryEdit;
        self._dDispatch[self.ksActionFailureCategoryEditPost]   = self._actionFailureCategoryEditPost;

        #
        # Failure Reason actions
        #
        self._dDispatch[self.ksActionFailureReasonList]         = self._actionFailureReasonList;
        self._dDispatch[self.ksActionFailureReasonAdd]          = self._actionFailureReasonAdd;
        self._dDispatch[self.ksActionFailureReasonAddPost]      = self._actionFailureReasonAddPost;
        self._dDispatch[self.ksActionFailureReasonDetails]      = self._actionFailureReasonDetails;
        self._dDispatch[self.ksActionFailureReasonDoRemove]     = self._actionFailureReasonDoRemove;
        self._dDispatch[self.ksActionFailureReasonEdit]         = self._actionFailureReasonEdit;
        self._dDispatch[self.ksActionFailureReasonEditPost]     = self._actionFailureReasonEditPost;

        #
        # Build Category actions
        #
        self._dDispatch[self.ksActionBuildCategoryList]         = self._actionBuildCategoryList;
        self._dDispatch[self.ksActionBuildCategoryAdd]          = self._actionBuildCategoryAdd;
        self._dDispatch[self.ksActionBuildCategoryAddPost]      = self._actionBuildCategoryAddPost;
        self._dDispatch[self.ksActionBuildCategoryClone]        = self._actionBuildCategoryClone;
        self._dDispatch[self.ksActionBuildCategoryDetails]      = self._actionBuildCategoryDetails;
        self._dDispatch[self.ksActionBuildCategoryDoRemove]     = self._actionBuildCategoryDoRemove;

        #
        # Test Group actions
        #
        self._dDispatch[self.ksActionTestGroupList]             = self._actionTestGroupList;
        self._dDispatch[self.ksActionTestGroupAdd]              = self._actionTestGroupAdd;
        self._dDispatch[self.ksActionTestGroupAddPost]          = self._actionTestGroupAddPost;
        self._dDispatch[self.ksActionTestGroupClone]            = self._actionTestGroupClone;
        self._dDispatch[self.ksActionTestGroupDetails]          = self._actionTestGroupDetails;
        self._dDispatch[self.ksActionTestGroupEdit]             = self._actionTestGroupEdit;
        self._dDispatch[self.ksActionTestGroupEditPost]         = self._actionTestGroupEditPost;
        self._dDispatch[self.ksActionTestGroupDoRemove]         = self._actionTestGroupDoRemove;
        self._dDispatch[self.ksActionTestCfgRegenQueues]        = self._actionRegenQueuesCommon;

        #
        # Scheduling Group and Queue actions
        #
        self._dDispatch[self.ksActionSchedGroupList]            = self._actionSchedGroupList;
        self._dDispatch[self.ksActionSchedGroupAdd]             = self._actionSchedGroupAdd;
        self._dDispatch[self.ksActionSchedGroupClone]           = self._actionSchedGroupClone;
        self._dDispatch[self.ksActionSchedGroupDetails]         = self._actionSchedGroupDetails;
        self._dDispatch[self.ksActionSchedGroupEdit]            = self._actionSchedGroupEdit;
        self._dDispatch[self.ksActionSchedGroupAddPost]         = self._actionSchedGroupAddPost;
        self._dDispatch[self.ksActionSchedGroupEditPost]        = self._actionSchedGroupEditPost;
        self._dDispatch[self.ksActionSchedGroupDoRemove]        = self._actionSchedGroupDoRemove;
        self._dDispatch[self.ksActionSchedQueueList]            = self._actionSchedQueueList;


        #
        # Menus
        #
        self._aaoMenus = \
        [
            [
                'Builds',       self._sActionUrlBase + self.ksActionBuildList,
                [
                    [ 'Builds',                 self._sActionUrlBase + self.ksActionBuildList,              False ],
                    [ 'Blacklist',              self._sActionUrlBase + self.ksActionBuildBlacklist,         False ],
                    [ 'Build sources',          self._sActionUrlBase + self.ksActionBuildSrcList,           False ],
                    [ 'Build categories',       self._sActionUrlBase + self.ksActionBuildCategoryList,      False ],
                    [ 'New build',              self._sActionUrlBase + self.ksActionBuildAdd,               True ],
                    [ 'New blacklisting',       self._sActionUrlBase + self.ksActionBuildBlacklistAdd,      True  ],
                    [ 'New build source',       self._sActionUrlBase + self.ksActionBuildSrcAdd,            True ],
                    [ 'New build category',     self._sActionUrlBase + self.ksActionBuildCategoryAdd,       True  ],
                ]
            ],
            [
                'Failure Reasons',       self._sActionUrlBase + self.ksActionFailureReasonList,
                [
                    [ 'Failure categories',     self._sActionUrlBase + self.ksActionFailureCategoryList,    False ],
                    [ 'Failure reasons',        self._sActionUrlBase + self.ksActionFailureReasonList,      False ],
                    [ 'New failure category',   self._sActionUrlBase + self.ksActionFailureCategoryAdd,     True  ],
                    [ 'New failure reason',     self._sActionUrlBase + self.ksActionFailureReasonAdd,       True  ],
                ]
            ],
            [
                'System',      self._sActionUrlBase + self.ksActionSystemChangelogList,
                [
                    [ 'Changelog',              self._sActionUrlBase + self.ksActionSystemChangelogList,    False ],
                    [ 'System log',             self._sActionUrlBase + self.ksActionSystemLogList,          False ],
                    [ 'Partial DB Dump',        self._sActionUrlBase + self.ksActionSystemDbDump,           False ],
                    [ 'User accounts',          self._sActionUrlBase + self.ksActionUserList,               False ],
                    [ 'New user',               self._sActionUrlBase + self.ksActionUserAdd,                True  ],
                ]
            ],
            [
                'Testboxes',   self._sActionUrlBase + self.ksActionTestBoxList,
                [
                    [ 'Testboxes',              self._sActionUrlBase + self.ksActionTestBoxList,            False ],
                    [ 'Scheduling groups',      self._sActionUrlBase + self.ksActionSchedGroupList,         False ],
                    [ 'New testbox',            self._sActionUrlBase + self.ksActionTestBoxAdd,             True  ],
                    [ 'New scheduling group',   self._sActionUrlBase + self.ksActionSchedGroupAdd,          True  ],
                    [ 'View scheduling queues', self._sActionUrlBase + self.ksActionSchedQueueList,         False ],
                    [ 'Regenerate all scheduling queues', self._sActionUrlBase + self.ksActionTestBoxesRegenQueues, True  ],
                ]
            ],
            [
                'Test Config', self._sActionUrlBase + self.ksActionTestGroupList,
                [
                    [ 'Test cases',             self._sActionUrlBase + self.ksActionTestCaseList,           False ],
                    [ 'Test groups',            self._sActionUrlBase + self.ksActionTestGroupList,          False ],
                    [ 'Global resources',       self._sActionUrlBase + self.ksActionGlobalRsrcShowAll,      False  ],
                    [ 'New test case',          self._sActionUrlBase + self.ksActionTestCaseAdd,            True  ],
                    [ 'New test group',         self._sActionUrlBase + self.ksActionTestGroupAdd,           True  ],
                    [ 'New global resource',    self._sActionUrlBase + self.ksActionGlobalRsrcShowAdd,      True  ],
                    [ 'Regenerate all scheduling queues', self._sActionUrlBase + self.ksActionTestCfgRegenQueues, True  ],
                ]
            ],
            [
                '> Test Results', 'index.py?' + webutils.encodeUrlParams(self._dDbgParams), []
            ],
        ];


    def _actionDefault(self):
        """Show the default admin page."""
        self._sAction = self.ksActionTestBoxList;
        from testmanager.core.testbox                  import TestBoxLogic;
        from testmanager.webui.wuiadmintestbox         import WuiTestBoxList;
        return self._actionGenericListing(TestBoxLogic, WuiTestBoxList);

    def _actionGenericDoDelOld(self, oCoreObjectLogic, sCoreObjectIdFieldName, sRedirectAction):
        """
        Delete entry (using oLogicType.remove).

        @param oCoreObjectLogic         A *Logic class

        @param sCoreObjectIdFieldName   Name of HTTP POST variable that
                                        contains object ID information

        @param sRedirectAction          An action for redirect user to
                                        in case of operation success
        """
        iCoreDataObjectId = self.getStringParam(sCoreObjectIdFieldName) # STRING?!?!
        self._checkForUnknownParameters()

        try:
            self._sPageTitle  = None
            self._sPageBody   = None
            self._sRedirectTo = self._sActionUrlBase + sRedirectAction
            return oCoreObjectLogic(self._oDb).remove(self._oCurUser.uid, iCoreDataObjectId)
        except Exception as oXcpt:
            self._oDb.rollback();
            self._sPageTitle  = 'Unable to delete record'
            self._sPageBody   = str(oXcpt);
            if config.g_kfDebugDbXcpt:
                self._sPageBody += cgitb.html(sys.exc_info());
            self._sRedirectTo = None

        return False


    #
    # System Category.
    #

    # System wide changelog actions.

    def _actionSystemChangelogList(self):
        """ Action handler. """
        from testmanager.core.systemchangelog          import SystemChangelogLogic;
        from testmanager.webui.wuiadminsystemchangelog import WuiAdminSystemChangelogList;

        tsEffective     = self.getEffectiveDateParam();
        cItemsPerPage   = self.getIntParam(self.ksParamItemsPerPage, iMin = 2, iMax =   9999, iDefault = 384);
        iPage           = self.getIntParam(self.ksParamPageNo,       iMin = 0, iMax = 999999, iDefault = 0);
        cDaysBack       = self.getIntParam(self.ksParamDaysBack,     iMin = 1, iMax = 366,    iDefault = 14);
        self._checkForUnknownParameters();

        aoEntries  = SystemChangelogLogic(self._oDb).fetchForListingEx(iPage * cItemsPerPage, cItemsPerPage + 1,
                                                                       tsEffective, cDaysBack);
        oContent   = WuiAdminSystemChangelogList(aoEntries, iPage, cItemsPerPage, tsEffective,
                                                 cDaysBack = cDaysBack, fnDPrint = self._oSrvGlue.dprint, oDisp = self);
        (self._sPageTitle, self._sPageBody) = oContent.show();
        return True;

    # System Log actions.

    def _actionSystemLogList(self):
        """ Action wrapper. """
        from testmanager.core.systemlog                import SystemLogLogic;
        from testmanager.webui.wuiadminsystemlog       import WuiAdminSystemLogList;
        return self._actionGenericListing(SystemLogLogic, WuiAdminSystemLogList)

    def _actionSystemDbDump(self):
        """ Action handler. """
        from testmanager.webui.wuiadminsystemdbdump    import WuiAdminSystemDbDumpForm;

        cDaysBack = self.getIntParam(self.ksParamDaysBack, iMin = config.g_kcTmDbDumpMinDays,
                                     iMax = config.g_kcTmDbDumpMaxDays, iDefault = config.g_kcTmDbDumpDefaultDays);
        self._checkForUnknownParameters();

        oContent = WuiAdminSystemDbDumpForm(cDaysBack, oDisp = self);
        (self._sPageTitle, self._sPageBody) = oContent.showForm();
        return True;

    def _actionSystemDbDumpDownload(self):
        """ Action handler. """
        import datetime;
        import os;

        cDaysBack = self.getIntParam(self.ksParamDaysBack, iMin = config.g_kcTmDbDumpMinDays,
                                     iMax = config.g_kcTmDbDumpMaxDays, iDefault = config.g_kcTmDbDumpDefaultDays);
        self._checkForUnknownParameters();

        #
        # Generate the dump.
        #
        # We generate a file name that's unique to a user is smart enough to only
        # issue one of these requests at the time.  This also makes sure we  won't
        #  waste too much space should this code get interrupted and rerun.
        #
        oFile    = None;
        oNow     = datetime.datetime.utcnow();
        sOutFile = config.g_ksTmDbDumpOutFileTmpl % (self._oCurUser.uid,);
        sTmpFile = config.g_ksTmDbDumpTmpFileTmpl % (self._oCurUser.uid,);
        sScript  = os.path.join(config.g_ksTestManagerDir, 'db', 'partial-db-dump.py');
        try:
            (iExitCode, sStdOut, sStdErr) = utils.processOutputUnchecked([ sScript,
                                                                           '--days-to-dump', str(cDaysBack),
                                                                           '-f', sOutFile,
                                                                           '-t', sTmpFile,
                                                                           ]);
            if iExitCode != 0:
                raise Exception('iExitCode=%s\n--- stderr ---\n%s\n--- stdout ---\n%s' % (iExitCode, sStdOut, sStdErr,));

            #
            # Open and send the dump.
            #
            oFile = open(sOutFile, 'rb');                       # pylint: disable=consider-using-with
            cbFile = os.fstat(oFile.fileno()).st_size;

            self._oSrvGlue.setHeaderField('Content-Type', 'application/zip');
            self._oSrvGlue.setHeaderField('Content-Disposition',
                                          oNow.strftime('attachment; filename="partial-db-dump-%Y-%m-%dT%H-%M-%S.zip"'));
            self._oSrvGlue.setHeaderField('Content-Length', str(cbFile));

            while True:
                abChunk = oFile.read(262144);
                if not abChunk:
                    break;
                self._oSrvGlue.writeRaw(abChunk);

        finally:
            # Delete the file to save space.
            if oFile:
                try:    oFile.close();
                except: pass;
            utils.noxcptDeleteFile(sOutFile);
            utils.noxcptDeleteFile(sTmpFile);
        return self.ksDispatchRcAllDone;


    # User Account actions.

    def _actionUserList(self):
        """ Action wrapper. """
        from testmanager.core.useraccount              import UserAccountLogic;
        from testmanager.webui.wuiadminuseraccount     import WuiUserAccountList;
        return self._actionGenericListing(UserAccountLogic, WuiUserAccountList)

    def _actionUserAdd(self):
        """ Action wrapper. """
        from testmanager.core.useraccount              import UserAccountData;
        from testmanager.webui.wuiadminuseraccount     import WuiUserAccount;
        return self._actionGenericFormAdd(UserAccountData, WuiUserAccount)

    def _actionUserDetails(self):
        """ Action wrapper. """
        from testmanager.core.useraccount              import UserAccountData, UserAccountLogic;
        from testmanager.webui.wuiadminuseraccount     import WuiUserAccount;
        return self._actionGenericFormDetails(UserAccountData, UserAccountLogic, WuiUserAccount, 'uid');

    def _actionUserEdit(self):
        """ Action wrapper. """
        from testmanager.core.useraccount              import UserAccountData;
        from testmanager.webui.wuiadminuseraccount     import WuiUserAccount;
        return self._actionGenericFormEdit(UserAccountData, WuiUserAccount, UserAccountData.ksParam_uid);

    def _actionUserAddPost(self):
        """ Action wrapper. """
        from testmanager.core.useraccount              import UserAccountData, UserAccountLogic;
        from testmanager.webui.wuiadminuseraccount     import WuiUserAccount;
        return self._actionGenericFormAddPost(UserAccountData, UserAccountLogic, WuiUserAccount, self.ksActionUserList)

    def _actionUserEditPost(self):
        """ Action wrapper. """
        from testmanager.core.useraccount              import UserAccountData, UserAccountLogic;
        from testmanager.webui.wuiadminuseraccount     import WuiUserAccount;
        return self._actionGenericFormEditPost(UserAccountData, UserAccountLogic, WuiUserAccount, self.ksActionUserList)

    def _actionUserDelPost(self):
        """ Action wrapper. """
        from testmanager.core.useraccount              import UserAccountData, UserAccountLogic;
        return self._actionGenericDoRemove(UserAccountLogic, UserAccountData.ksParam_uid, self.ksActionUserList)


    #
    # TestBox & Scheduling Category.
    #

    def _actionTestBoxList(self):
        """ Action wrapper. """
        from testmanager.core.testbox                  import TestBoxLogic
        from testmanager.webui.wuiadmintestbox         import WuiTestBoxList;
        return self._actionGenericListing(TestBoxLogic, WuiTestBoxList);

    def _actionTestBoxListPost(self):
        """Actions on a list of testboxes."""
        from testmanager.core.testbox                  import TestBoxData, TestBoxLogic
        from testmanager.webui.wuiadmintestbox         import WuiTestBoxList;

        # Parameters.
        aidTestBoxes = self.getListOfIntParams(TestBoxData.ksParam_idTestBox, iMin = 1, aiDefaults = []);
        sListAction  = self.getStringParam(self.ksParamListAction);
        if sListAction in [asDesc[0] for asDesc in WuiTestBoxList.kasTestBoxActionDescs]:
            idAction = None;
        else:
            asActionPrefixes = [ 'setgroup-', ];
            i = 0;
            while i < len(asActionPrefixes) and not sListAction.startswith(asActionPrefixes[i]):
                i += 1;
            if i >= len(asActionPrefixes):
                raise WuiException('Parameter "%s" has an invalid value: "%s"' % (self.ksParamListAction, sListAction,));
            idAction = sListAction[len(asActionPrefixes[i]):];
            if not idAction.isdigit():
                raise WuiException('Parameter "%s" has an invalid value: "%s"' % (self.ksParamListAction, sListAction,));
            idAction = int(idAction);
            sListAction = sListAction[:len(asActionPrefixes[i]) - 1];
        self._checkForUnknownParameters();


        # Take action.
        if sListAction == 'none':
            pass;
        else:
            oLogic = TestBoxLogic(self._oDb);
            aoTestBoxes = []
            for idTestBox in aidTestBoxes:
                aoTestBoxes.append(TestBoxData().initFromDbWithId(self._oDb, idTestBox));

            if sListAction in [ 'enable', 'disable' ]:
                fEnable = sListAction == 'enable';
                for oTestBox in aoTestBoxes:
                    if oTestBox.fEnabled != fEnable:
                        oTestBox.fEnabled = fEnable;
                        oLogic.editEntry(oTestBox, self._oCurUser.uid, fCommit = False);
            else:
                for oTestBox in aoTestBoxes:
                    if oTestBox.enmPendingCmd != sListAction:
                        oLogic.setCommand(oTestBox.idTestBox, oTestBox.enmPendingCmd, sListAction, self._oCurUser.uid,
                                          fCommit = False);
            self._oDb.commit();

        # Re-display the list.
        self._sPageTitle  = None;
        self._sPageBody   = None;
        self._sRedirectTo = self._sActionUrlBase + self.ksActionTestBoxList;
        return True;

    def _actionTestBoxAdd(self):
        """ Action wrapper. """
        from testmanager.core.testbox                  import TestBoxDataEx;
        from testmanager.webui.wuiadmintestbox         import WuiTestBox;
        return self._actionGenericFormAdd(TestBoxDataEx, WuiTestBox);

    def _actionTestBoxAddPost(self):
        """ Action wrapper. """
        from testmanager.core.testbox                  import TestBoxDataEx, TestBoxLogic;
        from testmanager.webui.wuiadmintestbox         import WuiTestBox;
        return self._actionGenericFormAddPost(TestBoxDataEx, TestBoxLogic, WuiTestBox, self.ksActionTestBoxList);

    def _actionTestBoxDetails(self):
        """ Action wrapper. """
        from testmanager.core.testbox                  import TestBoxDataEx, TestBoxLogic;
        from testmanager.webui.wuiadmintestbox         import WuiTestBox;
        return self._actionGenericFormDetails(TestBoxDataEx, TestBoxLogic, WuiTestBox, 'idTestBox', 'idGenTestBox');

    def _actionTestBoxEdit(self):
        """ Action wrapper. """
        from testmanager.core.testbox                  import TestBoxDataEx;
        from testmanager.webui.wuiadmintestbox         import WuiTestBox;
        return self._actionGenericFormEdit(TestBoxDataEx, WuiTestBox, TestBoxDataEx.ksParam_idTestBox);

    def _actionTestBoxEditPost(self):
        """ Action wrapper. """
        from testmanager.core.testbox                  import TestBoxDataEx, TestBoxLogic;
        from testmanager.webui.wuiadmintestbox         import WuiTestBox;
        return self._actionGenericFormEditPost(TestBoxDataEx, TestBoxLogic,WuiTestBox, self.ksActionTestBoxList);

    def _actionTestBoxRemovePost(self):
        """ Action wrapper. """
        from testmanager.core.testbox                  import TestBoxData, TestBoxLogic;
        return self._actionGenericDoRemove(TestBoxLogic, TestBoxData.ksParam_idTestBox, self.ksActionTestBoxList);


    # Scheduling Group actions

    def _actionSchedGroupList(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupLogic;
        from testmanager.webui.wuiadminschedgroup       import WuiAdminSchedGroupList;
        return self._actionGenericListing(SchedGroupLogic, WuiAdminSchedGroupList);

    def _actionSchedGroupAdd(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupDataEx;
        from testmanager.webui.wuiadminschedgroup       import WuiSchedGroup;
        return self._actionGenericFormAdd(SchedGroupDataEx, WuiSchedGroup);

    def _actionSchedGroupClone(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupDataEx;
        from testmanager.webui.wuiadminschedgroup       import WuiSchedGroup;
        return self._actionGenericFormClone(  SchedGroupDataEx, WuiSchedGroup, 'idSchedGroup');

    def _actionSchedGroupDetails(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupDataEx, SchedGroupLogic;
        from testmanager.webui.wuiadminschedgroup       import WuiSchedGroup;
        return self._actionGenericFormDetails(SchedGroupDataEx, SchedGroupLogic, WuiSchedGroup, 'idSchedGroup');

    def _actionSchedGroupEdit(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupDataEx;
        from testmanager.webui.wuiadminschedgroup       import WuiSchedGroup;
        return self._actionGenericFormEdit(SchedGroupDataEx, WuiSchedGroup, SchedGroupDataEx.ksParam_idSchedGroup);

    def _actionSchedGroupAddPost(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupDataEx, SchedGroupLogic;
        from testmanager.webui.wuiadminschedgroup       import WuiSchedGroup;
        return self._actionGenericFormAddPost(SchedGroupDataEx, SchedGroupLogic, WuiSchedGroup, self.ksActionSchedGroupList);

    def _actionSchedGroupEditPost(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupDataEx, SchedGroupLogic;
        from testmanager.webui.wuiadminschedgroup       import WuiSchedGroup;
        return self._actionGenericFormEditPost(SchedGroupDataEx, SchedGroupLogic, WuiSchedGroup, self.ksActionSchedGroupList);

    def _actionSchedGroupDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.schedgroup                import SchedGroupData, SchedGroupLogic;
        return self._actionGenericDoRemove(SchedGroupLogic, SchedGroupData.ksParam_idSchedGroup, self.ksActionSchedGroupList)

    def _actionSchedQueueList(self):
        """ Action wrapper. """
        from testmanager.core.schedqueue                import SchedQueueLogic;
        from testmanager.webui.wuiadminschedqueue       import WuiAdminSchedQueueList;
        return self._actionGenericListing(SchedQueueLogic, WuiAdminSchedQueueList);

    def _actionRegenQueuesCommon(self):
        """
        Common code for ksActionTestBoxesRegenQueues and ksActionTestCfgRegenQueues.

        Too lazy to put this in some separate place right now.
        """
        from testmanager.core.schedgroup               import SchedGroupLogic;
        from testmanager.core.schedulerbase            import SchedulerBase;

        self._checkForUnknownParameters();
        ## @todo should also be changed to a POST with a confirmation dialog preceeding it.

        self._sPageTitle = 'Regenerate All Scheduling Queues';
        if not self.isReadOnlyUser():
            self._sPageBody  = '';
            aoGroups = SchedGroupLogic(self._oDb).getAll();
            for oGroup in aoGroups:
                self._sPageBody += '<h3>%s (ID %#d)</h3>' % (webutils.escapeElem(oGroup.sName), oGroup.idSchedGroup);
                try:
                    (aoErrors, asMessages) = SchedulerBase.recreateQueue(self._oDb, self._oCurUser.uid, oGroup.idSchedGroup, 2);
                except Exception as oXcpt:
                    self._oDb.rollback();
                    self._sPageBody += '<p>SchedulerBase.recreateQueue threw an exception: %s</p>' \
                                    % (webutils.escapeElem(str(oXcpt)),);
                    self._sPageBody += cgitb.html(sys.exc_info());
                else:
                    if not aoErrors:
                        self._sPageBody += '<p>Successfully regenerated.</p>';
                    else:
                        for oError in aoErrors:
                            if oError[1] is None:
                                self._sPageBody += '<p>%s.</p>' % (webutils.escapeElem(oError[0]),);
                            ## @todo links.
                            #elif isinstance(oError[1], TestGroupData):
                            #    self._sPageBody += '<p>%s.</p>' % (webutils.escapeElem(oError[0]),);
                            #elif isinstance(oError[1], TestGroupCase):
                            #    self._sPageBody += '<p>%s.</p>' % (webutils.escapeElem(oError[0]),);
                            else:
                                self._sPageBody += '<p>%s. [Cannot link to %s]</p>' \
                                                 % (webutils.escapeElem(oError[0]), webutils.escapeElem(str(oError[1])),);
                    for sMsg in asMessages:
                        self._sPageBody += '<p>%s<p>\n' % (webutils.escapeElem(sMsg),);

            # Remove leftovers from deleted scheduling groups.
            self._sPageBody += '<h3>Cleanups</h3>\n';
            cOrphans = SchedulerBase.cleanUpOrphanedQueues(self._oDb);
            self._sPageBody += '<p>Removed %s orphaned (deleted) queue%s.<p>\n' % (cOrphans, '' if cOrphans == 1 else 's', );
        else:
            self._sPageBody = webutils.escapeElem('%s is a read only user and may not regenerate the scheduling queues!'
                                                  % (self._oCurUser.sUsername,));
        return True;



    #
    # Test Config Category.
    #

    # Test Cases

    def _actionTestCaseList(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseLogic;
        from testmanager.webui.wuiadmintestcase         import WuiTestCaseList;
        return self._actionGenericListing(TestCaseLogic, WuiTestCaseList);

    def _actionTestCaseAdd(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseDataEx;
        from testmanager.webui.wuiadmintestcase         import WuiTestCase;
        return self._actionGenericFormAdd(TestCaseDataEx, WuiTestCase);

    def _actionTestCaseAddPost(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseDataEx, TestCaseLogic;
        from testmanager.webui.wuiadmintestcase         import WuiTestCase;
        return self._actionGenericFormAddPost(TestCaseDataEx, TestCaseLogic, WuiTestCase, self.ksActionTestCaseList);

    def _actionTestCaseClone(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseDataEx;
        from testmanager.webui.wuiadmintestcase         import WuiTestCase;
        return self._actionGenericFormClone(  TestCaseDataEx, WuiTestCase, 'idTestCase', 'idGenTestCase');

    def _actionTestCaseDetails(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseDataEx, TestCaseLogic;
        from testmanager.webui.wuiadmintestcase         import WuiTestCase;
        return self._actionGenericFormDetails(TestCaseDataEx, TestCaseLogic, WuiTestCase, 'idTestCase', 'idGenTestCase');

    def _actionTestCaseEdit(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseDataEx;
        from testmanager.webui.wuiadmintestcase         import WuiTestCase;
        return self._actionGenericFormEdit(TestCaseDataEx, WuiTestCase, TestCaseDataEx.ksParam_idTestCase);

    def _actionTestCaseEditPost(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseDataEx, TestCaseLogic;
        from testmanager.webui.wuiadmintestcase         import WuiTestCase;
        return self._actionGenericFormEditPost(TestCaseDataEx, TestCaseLogic, WuiTestCase, self.ksActionTestCaseList);

    def _actionTestCaseDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.testcase                  import TestCaseData, TestCaseLogic;
        return self._actionGenericDoRemove(TestCaseLogic, TestCaseData.ksParam_idTestCase, self.ksActionTestCaseList);

    # Test Group actions

    def _actionTestGroupList(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupLogic;
        from testmanager.webui.wuiadmintestgroup        import WuiTestGroupList;
        return self._actionGenericListing(TestGroupLogic, WuiTestGroupList);
    def _actionTestGroupAdd(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupDataEx;
        from testmanager.webui.wuiadmintestgroup        import WuiTestGroup;
        return self._actionGenericFormAdd(TestGroupDataEx, WuiTestGroup);
    def _actionTestGroupAddPost(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupDataEx, TestGroupLogic;
        from testmanager.webui.wuiadmintestgroup        import WuiTestGroup;
        return self._actionGenericFormAddPost(TestGroupDataEx, TestGroupLogic, WuiTestGroup, self.ksActionTestGroupList);
    def _actionTestGroupClone(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupDataEx;
        from testmanager.webui.wuiadmintestgroup        import WuiTestGroup;
        return self._actionGenericFormClone(TestGroupDataEx, WuiTestGroup, 'idTestGroup');
    def _actionTestGroupDetails(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupDataEx, TestGroupLogic;
        from testmanager.webui.wuiadmintestgroup        import WuiTestGroup;
        return self._actionGenericFormDetails(TestGroupDataEx, TestGroupLogic, WuiTestGroup, 'idTestGroup');
    def _actionTestGroupEdit(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupDataEx;
        from testmanager.webui.wuiadmintestgroup        import WuiTestGroup;
        return self._actionGenericFormEdit(TestGroupDataEx, WuiTestGroup, TestGroupDataEx.ksParam_idTestGroup);
    def _actionTestGroupEditPost(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupDataEx, TestGroupLogic;
        from testmanager.webui.wuiadmintestgroup        import WuiTestGroup;
        return self._actionGenericFormEditPost(TestGroupDataEx, TestGroupLogic, WuiTestGroup, self.ksActionTestGroupList);
    def _actionTestGroupDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.testgroup                 import TestGroupDataEx, TestGroupLogic;
        return self._actionGenericDoRemove(TestGroupLogic, TestGroupDataEx.ksParam_idTestGroup, self.ksActionTestGroupList)


    # Global Resources

    def _actionGlobalRsrcShowAll(self):
        """ Action wrapper. """
        from testmanager.core.globalresource            import GlobalResourceLogic;
        from testmanager.webui.wuiadminglobalrsrc       import WuiGlobalResourceList;
        return self._actionGenericListing(GlobalResourceLogic, WuiGlobalResourceList);

    def _actionGlobalRsrcShowAdd(self):
        """ Action wrapper. """
        return self._actionGlobalRsrcShowAddEdit(WuiAdmin.ksActionGlobalRsrcAdd);

    def _actionGlobalRsrcShowEdit(self):
        """ Action wrapper. """
        return self._actionGlobalRsrcShowAddEdit(WuiAdmin.ksActionGlobalRsrcEdit);

    def _actionGlobalRsrcAdd(self):
        """ Action wrapper. """
        return self._actionGlobalRsrcAddEdit(WuiAdmin.ksActionGlobalRsrcAdd);

    def _actionGlobalRsrcEdit(self):
        """ Action wrapper. """
        return self._actionGlobalRsrcAddEdit(WuiAdmin.ksActionGlobalRsrcEdit);

    def _actionGlobalRsrcDel(self):
        """ Action wrapper. """
        from testmanager.core.globalresource            import GlobalResourceData, GlobalResourceLogic;
        return self._actionGenericDoDelOld(GlobalResourceLogic, GlobalResourceData.ksParam_idGlobalRsrc,
                                           self.ksActionGlobalRsrcShowAll);

    def _actionGlobalRsrcShowAddEdit(self, sAction): # pylint: disable=invalid-name
        """Show Global Resource creation or edit dialog"""
        from testmanager.core.globalresource           import GlobalResourceLogic, GlobalResourceData;
        from testmanager.webui.wuiadminglobalrsrc      import WuiGlobalResource;

        oGlobalResourceLogic = GlobalResourceLogic(self._oDb)
        if sAction == WuiAdmin.ksActionGlobalRsrcEdit:
            idGlobalRsrc = self.getIntParam(GlobalResourceData.ksParam_idGlobalRsrc, iDefault = -1)
            oData = oGlobalResourceLogic.getById(idGlobalRsrc)
        else:
            oData = GlobalResourceData()
            oData.convertToParamNull()

        self._checkForUnknownParameters()

        oContent = WuiGlobalResource(oData)
        (self._sPageTitle, self._sPageBody) = oContent.showAddModifyPage(sAction)

        return True

    def _actionGlobalRsrcAddEdit(self, sAction):
        """Add or modify Global Resource record"""
        from testmanager.core.globalresource           import GlobalResourceLogic, GlobalResourceData;
        from testmanager.webui.wuiadminglobalrsrc      import WuiGlobalResource;

        oData = GlobalResourceData()
        oData.initFromParams(self, fStrict=True)

        self._checkForUnknownParameters()

        if self._oSrvGlue.getMethod() != 'POST':
            raise WuiException('Expected "POST" request, got "%s"' % (self._oSrvGlue.getMethod(),))

        oGlobalResourceLogic = GlobalResourceLogic(self._oDb)
        dErrors = oData.validateAndConvert(self._oDb);
        if not dErrors:
            if sAction == WuiAdmin.ksActionGlobalRsrcAdd:
                oGlobalResourceLogic.addGlobalResource(self._oCurUser.uid, oData)
            elif sAction == WuiAdmin.ksActionGlobalRsrcEdit:
                idGlobalRsrc = self.getStringParam(GlobalResourceData.ksParam_idGlobalRsrc)
                oGlobalResourceLogic.editGlobalResource(self._oCurUser.uid, idGlobalRsrc, oData)
            else:
                raise WuiException('Invalid parameter.')
            self._sPageTitle  = None;
            self._sPageBody   = None;
            self._sRedirectTo = self._sActionUrlBase + self.ksActionGlobalRsrcShowAll;
        else:
            oContent = WuiGlobalResource(oData)
            (self._sPageTitle, self._sPageBody) = oContent.showAddModifyPage(sAction, dErrors=dErrors)

        return True


    #
    # Build Source actions
    #

    def _actionBuildSrcList(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceLogic;
        from testmanager.webui.wuiadminbuildsource      import WuiAdminBuildSrcList;
        return self._actionGenericListing(BuildSourceLogic, WuiAdminBuildSrcList);

    def _actionBuildSrcAdd(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceData;
        from testmanager.webui.wuiadminbuildsource      import WuiAdminBuildSrc;
        return self._actionGenericFormAdd(BuildSourceData, WuiAdminBuildSrc);

    def _actionBuildSrcAddPost(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceData, BuildSourceLogic;
        from testmanager.webui.wuiadminbuildsource      import WuiAdminBuildSrc;
        return self._actionGenericFormAddPost(BuildSourceData, BuildSourceLogic, WuiAdminBuildSrc, self.ksActionBuildSrcList);

    def _actionBuildSrcClone(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceData;
        from testmanager.webui.wuiadminbuildsource      import WuiAdminBuildSrc;
        return self._actionGenericFormClone(  BuildSourceData, WuiAdminBuildSrc, 'idBuildSrc');

    def _actionBuildSrcDetails(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceData, BuildSourceLogic;
        from testmanager.webui.wuiadminbuildsource      import WuiAdminBuildSrc;
        return self._actionGenericFormDetails(BuildSourceData, BuildSourceLogic, WuiAdminBuildSrc, 'idBuildSrc');

    def _actionBuildSrcDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceData, BuildSourceLogic;
        return self._actionGenericDoRemove(BuildSourceLogic, BuildSourceData.ksParam_idBuildSrc, self.ksActionBuildSrcList);

    def _actionBuildSrcEdit(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceData;
        from testmanager.webui.wuiadminbuildsource      import WuiAdminBuildSrc;
        return self._actionGenericFormEdit(BuildSourceData, WuiAdminBuildSrc, BuildSourceData.ksParam_idBuildSrc);

    def _actionBuildSrcEditPost(self):
        """ Action wrapper. """
        from testmanager.core.buildsource               import BuildSourceData, BuildSourceLogic;
        from testmanager.webui.wuiadminbuildsource      import WuiAdminBuildSrc;
        return self._actionGenericFormEditPost(BuildSourceData, BuildSourceLogic, WuiAdminBuildSrc, self.ksActionBuildSrcList);


    #
    # Build actions
    #
    def _actionBuildList(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildLogic;
        from testmanager.webui.wuiadminbuild            import WuiAdminBuildList;
        return self._actionGenericListing(BuildLogic, WuiAdminBuildList);

    def _actionBuildAdd(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildData;
        from testmanager.webui.wuiadminbuild            import WuiAdminBuild;
        return self._actionGenericFormAdd(BuildData, WuiAdminBuild);

    def _actionBuildAddPost(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildData, BuildLogic;
        from testmanager.webui.wuiadminbuild            import WuiAdminBuild;
        return self._actionGenericFormAddPost(BuildData, BuildLogic, WuiAdminBuild, self.ksActionBuildList);

    def _actionBuildClone(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildData;
        from testmanager.webui.wuiadminbuild            import WuiAdminBuild;
        return self._actionGenericFormClone(  BuildData, WuiAdminBuild, 'idBuild');

    def _actionBuildDetails(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildData, BuildLogic;
        from testmanager.webui.wuiadminbuild            import WuiAdminBuild;
        return self._actionGenericFormDetails(BuildData, BuildLogic, WuiAdminBuild, 'idBuild');

    def _actionBuildDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildData, BuildLogic;
        return self._actionGenericDoRemove(BuildLogic, BuildData.ksParam_idBuild, self.ksActionBuildList);

    def _actionBuildEdit(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildData;
        from testmanager.webui.wuiadminbuild            import WuiAdminBuild;
        return self._actionGenericFormEdit(BuildData, WuiAdminBuild, BuildData.ksParam_idBuild);

    def _actionBuildEditPost(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildData, BuildLogic;
        from testmanager.webui.wuiadminbuild            import WuiAdminBuild;
        return self._actionGenericFormEditPost(BuildData, BuildLogic, WuiAdminBuild, self.ksActionBuildList)


    #
    # Build Category actions
    #
    def _actionBuildCategoryList(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildCategoryLogic;
        from testmanager.webui.wuiadminbuildcategory    import WuiAdminBuildCatList;
        return self._actionGenericListing(BuildCategoryLogic, WuiAdminBuildCatList);

    def _actionBuildCategoryAdd(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildCategoryData;
        from testmanager.webui.wuiadminbuildcategory    import WuiAdminBuildCat;
        return self._actionGenericFormAdd(BuildCategoryData, WuiAdminBuildCat);

    def _actionBuildCategoryAddPost(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildCategoryData, BuildCategoryLogic;
        from testmanager.webui.wuiadminbuildcategory    import WuiAdminBuildCat;
        return self._actionGenericFormAddPost(BuildCategoryData, BuildCategoryLogic, WuiAdminBuildCat,
                                              self.ksActionBuildCategoryList);

    def _actionBuildCategoryClone(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildCategoryData;
        from testmanager.webui.wuiadminbuildcategory    import WuiAdminBuildCat;
        return self._actionGenericFormClone(BuildCategoryData, WuiAdminBuildCat, 'idBuildCategory');

    def _actionBuildCategoryDetails(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildCategoryData, BuildCategoryLogic;
        from testmanager.webui.wuiadminbuildcategory    import WuiAdminBuildCat;
        return self._actionGenericFormDetails(BuildCategoryData, BuildCategoryLogic, WuiAdminBuildCat, 'idBuildCategory');

    def _actionBuildCategoryDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.build                     import BuildCategoryData, BuildCategoryLogic;
        return self._actionGenericDoRemove(BuildCategoryLogic, BuildCategoryData.ksParam_idBuildCategory,
                                           self.ksActionBuildCategoryList)


    #
    # Build Black List actions
    #
    def _actionBuildBlacklist(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistLogic;
        from testmanager.webui.wuiadminbuildblacklist   import WuiAdminListOfBlacklistItems;
        return self._actionGenericListing(BuildBlacklistLogic, WuiAdminListOfBlacklistItems);

    def _actionBuildBlacklistAdd(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistData;
        from testmanager.webui.wuiadminbuildblacklist   import WuiAdminBuildBlacklist;
        return self._actionGenericFormAdd(BuildBlacklistData, WuiAdminBuildBlacklist);

    def _actionBuildBlacklistAddPost(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistData, BuildBlacklistLogic;
        from testmanager.webui.wuiadminbuildblacklist   import WuiAdminBuildBlacklist;
        return self._actionGenericFormAddPost(BuildBlacklistData, BuildBlacklistLogic,
                                              WuiAdminBuildBlacklist, self.ksActionBuildBlacklist);

    def _actionBuildBlacklistClone(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistData;
        from testmanager.webui.wuiadminbuildblacklist   import WuiAdminBuildBlacklist;
        return self._actionGenericFormClone(BuildBlacklistData, WuiAdminBuildBlacklist, 'idBlacklisting');

    def _actionBuildBlacklistDetails(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistData, BuildBlacklistLogic;
        from testmanager.webui.wuiadminbuildblacklist   import WuiAdminBuildBlacklist;
        return self._actionGenericFormDetails(BuildBlacklistData, BuildBlacklistLogic, WuiAdminBuildBlacklist, 'idBlacklisting');

    def _actionBuildBlacklistDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistData, BuildBlacklistLogic;
        return self._actionGenericDoRemove(BuildBlacklistLogic, BuildBlacklistData.ksParam_idBlacklisting,
                                           self.ksActionBuildBlacklist);

    def _actionBuildBlacklistEdit(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistData;
        from testmanager.webui.wuiadminbuildblacklist   import WuiAdminBuildBlacklist;
        return self._actionGenericFormEdit(BuildBlacklistData, WuiAdminBuildBlacklist, BuildBlacklistData.ksParam_idBlacklisting);

    def _actionBuildBlacklistEditPost(self):
        """ Action wrapper. """
        from testmanager.core.buildblacklist            import BuildBlacklistData, BuildBlacklistLogic;
        from testmanager.webui.wuiadminbuildblacklist   import WuiAdminBuildBlacklist;
        return self._actionGenericFormEditPost(BuildBlacklistData, BuildBlacklistLogic, WuiAdminBuildBlacklist,
                                               self.ksActionBuildBlacklist)


    #
    # Failure Category actions
    #
    def _actionFailureCategoryList(self):
        """ Action wrapper. """
        from testmanager.core.failurecategory           import FailureCategoryLogic;
        from testmanager.webui.wuiadminfailurecategory  import WuiFailureCategoryList;
        return self._actionGenericListing(FailureCategoryLogic, WuiFailureCategoryList);

    def _actionFailureCategoryAdd(self):
        """ Action wrapper. """
        from testmanager.core.failurecategory           import FailureCategoryData;
        from testmanager.webui.wuiadminfailurecategory  import WuiFailureCategory;
        return self._actionGenericFormAdd(FailureCategoryData, WuiFailureCategory);

    def _actionFailureCategoryAddPost(self):
        """ Action wrapper. """
        from testmanager.core.failurecategory           import FailureCategoryData, FailureCategoryLogic;
        from testmanager.webui.wuiadminfailurecategory  import WuiFailureCategory;
        return self._actionGenericFormAddPost(FailureCategoryData, FailureCategoryLogic, WuiFailureCategory,
                                              self.ksActionFailureCategoryList)

    def _actionFailureCategoryDetails(self):
        """ Action wrapper. """
        from testmanager.core.failurecategory           import FailureCategoryData, FailureCategoryLogic;
        from testmanager.webui.wuiadminfailurecategory  import WuiFailureCategory;
        return self._actionGenericFormDetails(FailureCategoryData, FailureCategoryLogic, WuiFailureCategory);


    def _actionFailureCategoryDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.failurecategory           import FailureCategoryData, FailureCategoryLogic;
        return self._actionGenericDoRemove(FailureCategoryLogic, FailureCategoryData.ksParam_idFailureCategory,
                                           self.ksActionFailureCategoryList);

    def _actionFailureCategoryEdit(self):
        """ Action wrapper. """
        from testmanager.core.failurecategory           import FailureCategoryData;
        from testmanager.webui.wuiadminfailurecategory  import WuiFailureCategory;
        return self._actionGenericFormEdit(FailureCategoryData, WuiFailureCategory,
                                           FailureCategoryData.ksParam_idFailureCategory);

    def _actionFailureCategoryEditPost(self):
        """ Action wrapper. """
        from testmanager.core.failurecategory           import FailureCategoryData, FailureCategoryLogic;
        from testmanager.webui.wuiadminfailurecategory  import WuiFailureCategory;
        return self._actionGenericFormEditPost(FailureCategoryData, FailureCategoryLogic, WuiFailureCategory,
                                               self.ksActionFailureCategoryList);

    #
    # Failure Reason actions
    #
    def _actionFailureReasonList(self):
        """ Action wrapper. """
        from testmanager.core.failurereason             import FailureReasonLogic;
        from testmanager.webui.wuiadminfailurereason    import WuiAdminFailureReasonList;
        return self._actionGenericListing(FailureReasonLogic, WuiAdminFailureReasonList)

    def _actionFailureReasonAdd(self):
        """ Action wrapper. """
        from testmanager.core.failurereason             import FailureReasonData;
        from testmanager.webui.wuiadminfailurereason    import WuiAdminFailureReason;
        return self._actionGenericFormAdd(FailureReasonData, WuiAdminFailureReason);

    def _actionFailureReasonAddPost(self):
        """ Action wrapper. """
        from testmanager.core.failurereason             import FailureReasonData, FailureReasonLogic;
        from testmanager.webui.wuiadminfailurereason    import WuiAdminFailureReason;
        return self._actionGenericFormAddPost(FailureReasonData, FailureReasonLogic, WuiAdminFailureReason,
                                              self.ksActionFailureReasonList);

    def _actionFailureReasonDetails(self):
        """ Action wrapper. """
        from testmanager.core.failurereason             import FailureReasonData, FailureReasonLogic;
        from testmanager.webui.wuiadminfailurereason    import WuiAdminFailureReason;
        return self._actionGenericFormDetails(FailureReasonData, FailureReasonLogic, WuiAdminFailureReason);

    def _actionFailureReasonDoRemove(self):
        """ Action wrapper. """
        from testmanager.core.failurereason             import FailureReasonData, FailureReasonLogic;
        return self._actionGenericDoRemove(FailureReasonLogic, FailureReasonData.ksParam_idFailureReason,
                                           self.ksActionFailureReasonList);

    def _actionFailureReasonEdit(self):
        """ Action wrapper. """
        from testmanager.core.failurereason             import FailureReasonData;
        from testmanager.webui.wuiadminfailurereason    import WuiAdminFailureReason;
        return self._actionGenericFormEdit(FailureReasonData, WuiAdminFailureReason);


    def _actionFailureReasonEditPost(self):
        """ Action wrapper. """
        from testmanager.core.failurereason             import FailureReasonData, FailureReasonLogic;
        from testmanager.webui.wuiadminfailurereason    import WuiAdminFailureReason;
        return self._actionGenericFormEditPost(FailureReasonData, FailureReasonLogic, WuiAdminFailureReason,
                                               self.ksActionFailureReasonList)


    #
    # Overrides.
    #

    def _generatePage(self):
        """Override parent handler in order to change page titte"""
        if self._sPageTitle is not None:
            self._sPageTitle = 'Test Manager Admin - ' + self._sPageTitle

        return WuiDispatcherBase._generatePage(self)
