# -*- coding: utf-8 -*-
# $Id: vcsrevisions.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - VcsRevisions
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
import unittest;

# Validation Kit imports.
from testmanager.core.base              import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMExceptionBase;


class VcsRevisionData(ModelDataBase):
    """
    A version control system (VCS) revision.
    """

    #kasIdAttr = ['sRepository',iRevision];

    ksParam_sRepository         = 'VcsRevision_sRepository';
    ksParam_iRevision           = 'VcsRevision_iRevision';
    ksParam_tsCreated           = 'VcsRevision_tsCreated';
    ksParam_sAuthor             = 'VcsRevision_sAuthor';
    ksParam_sMessage            = 'VcsRevision_sMessage';

    kasAllowNullAttributes      = [ ];
    kfAllowUnicode_sMessage     = True;
    kcchMax_sMessage            = 8192;

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.sRepository        = None;
        self.iRevision          = None;
        self.tsCreated          = None;
        self.sAuthor            = None;
        self.sMessage           = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the object from a SELECT * FROM VcsRevisions row.
        Returns self.  Raises exception if aoRow is None.
        """
        if aoRow is None:
            raise TMExceptionBase('VcsRevision not found.');

        self.sRepository         = aoRow[0];
        self.iRevision           = aoRow[1];
        self.tsCreated           = aoRow[2];
        self.sAuthor             = aoRow[3];
        self.sMessage            = aoRow[4];
        return self;

    def initFromDbWithRepoAndRev(self, oDb, sRepository, iRevision):
        """
        Initialize from the database, given the tree and revision of a row.
        """
        oDb.execute('SELECT * FROM VcsRevisions WHERE sRepository = %s AND iRevision = %u', (sRepository, iRevision,));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMExceptionBase('sRepository = %s iRevision = %u not found' % (sRepository, iRevision, ));
        return self.initFromDbRow(aoRow);

    def initFromValues(self, sRepository, iRevision, tsCreated, sAuthor, sMessage):
        """
        Reinitializes form a set of values.
        return self.
        """
        self.sRepository        = sRepository;
        self.iRevision          = iRevision;
        self.tsCreated          = tsCreated;
        self.sAuthor            = sAuthor;
        self.sMessage           = sMessage;
        return self;


class VcsRevisionLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    VCS revisions database logic.
    """

    #
    # Standard methods.
    #

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches VCS revisions for listing.

        Returns an array (list) of VcsRevisionData items, empty list if none.
        Raises exception on error.
        """
        _ = tsNow; _ = aiSortColumns;
        self._oDb.execute('SELECT   *\n'
                          'FROM     VcsRevisions\n'
                          'ORDER BY tsCreated, sRepository, iRevision\n'
                          'LIMIT %s OFFSET %s\n'
                          , (cMaxRows, iStart,));

        aoRows = [];
        for _ in range(self._oDb.getRowCount()):
            aoRows.append(VcsRevisionData().initFromDbRow(self._oDb.fetchOne()));
        return aoRows;

    def tryFetch(self, sRepository, iRevision):
        """
        Tries to fetch the specified tree revision record.
        Returns VcsRevisionData instance if found, None if not found.
        Raises exception on input and database errors.
        """
        self._oDb.execute('SELECT * FROM VcsRevisions WHERE sRepository = %s AND iRevision = %s',
                          ( sRepository, iRevision, ));
        aaoRows = self._oDb.fetchAll();
        if len(aaoRows) == 1:
            return VcsRevisionData().initFromDbRow(aaoRows[0]);
        if aaoRows:
            raise TMExceptionBase('VcsRevisions has a primary key problem: %u duplicates' % (len(aaoRows),));
        return None


    #
    # Other methods.
    #

    def addVcsRevision(self, oData, fCommit = False):
        """
        Adds (or updates) a tree revision record.
        Raises exception on input and database errors.
        """

        # Check VcsRevisionData before do anything
        dDataErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dDataErrors:
            raise TMExceptionBase('Invalid data passed to addVcsRevision(): %s' % (dDataErrors,));

        # Does it already exist?
        oOldData = self.tryFetch(oData.sRepository, oData.iRevision);
        if oOldData is None:
            # New row.
            self._oDb.execute('INSERT INTO VcsRevisions (sRepository, iRevision, tsCreated, sAuthor, sMessage)\n'
                              'VALUES (%s, %s, %s, %s, %s)\n'
                              , ( oData.sRepository,
                                  oData.iRevision,
                                  oData.tsCreated,
                                  oData.sAuthor,
                                  oData.sMessage,
                              ));
        elif not oOldData.isEqual(oData):
            # Update old row.
            self._oDb.execute('UPDATE VcsRevisions\n'
                              '   SET tsCreated   = %s,\n'
                              '       sAuthor     = %s,\n'
                              '       sMessage    = %s\n'
                              'WHERE  sRepository = %s\n'
                              '   AND iRevision   = %s'
                              , ( oData.tsCreated,
                                  oData.sAuthor,
                                  oData.sMessage,
                                  oData.sRepository,
                                  oData.iRevision,
                              ));

        self._oDb.maybeCommit(fCommit);
        return oData;

    def getLastRevision(self, sRepository):
        """
        Get the last known revision number for the given repository, returns 0
        if the repository is not known to us:
        """
        self._oDb.execute('SELECT iRevision\n'
                          'FROM   VcsRevisions\n'
                          'WHERE  sRepository = %s\n'
                          'ORDER BY iRevision DESC\n'
                          'LIMIT 1\n'
                          , ( sRepository, ));
        if self._oDb.getRowCount() == 0:
            return 0;
        return self._oDb.fetchOne()[0];

    def fetchTimeline(self, sRepository, iRevision, cEntriesBack):
        """
        Fetches a VCS timeline portion for a repository.

        Returns an array (list) of VcsRevisionData items, empty list if none.
        Raises exception on error.
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     VcsRevisions\n'
                          'WHERE    sRepository = %s\n'
                          '   AND   iRevision  >  %s\n'
                          '   AND   iRevision  <= %s\n'
                          'ORDER BY iRevision DESC\n'
                          'LIMIT    %s\n'
                          , ( sRepository, iRevision - cEntriesBack*2 + 1, iRevision, cEntriesBack));
        aoRows = [];
        for _ in range(self._oDb.getRowCount()):
            aoRows.append(VcsRevisionData().initFromDbRow(self._oDb.fetchOne()));
        return aoRows;


#
# Unit testing.
#

# pylint: disable=missing-docstring
class VcsRevisionDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [VcsRevisionData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

