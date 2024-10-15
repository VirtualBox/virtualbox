# -*- coding: utf-8 -*-
# $Id: failurecategory.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Failure Categories.
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
import sys;

# Validation Kit imports.
from testmanager.core.base          import ModelDataBase, ModelLogicBase, TMRowInUse, TMInvalidData, TMRowNotFound, \
                                           ChangeLogEntry, AttributeChangeEntry;
from testmanager.core.useraccount   import UserAccountLogic;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


class FailureCategoryData(ModelDataBase):
    """
    Failure Category Data.
    """

    ksIdAttr = 'idFailureCategory';

    ksParam_idFailureCategory = 'FailureCategory_idFailureCategory'
    ksParam_tsEffective       = 'FailureCategory_tsEffective'
    ksParam_tsExpire          = 'FailureCategory_tsExpire'
    ksParam_uidAuthor         = 'FailureCategory_uidAuthor'
    ksParam_sShort            = 'FailureCategory_sShort'
    ksParam_sFull             = 'FailureCategory_sFull'

    kasAllowNullAttributes    = [ 'idFailureCategory', 'tsEffective', 'tsExpire', 'uidAuthor' ]

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #

        self.idFailureCategory = None
        self.tsEffective       = None
        self.tsExpire          = None
        self.uidAuthor         = None
        self.sShort            = None
        self.sFull             = None

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the data with a row from a SELECT * FROM FailureCategoryes.

        Returns self. Raises exception if the row is None or otherwise invalid.
        """

        if aoRow is None:
            raise TMRowNotFound('Failure Category not found.');

        self.idFailureCategory = aoRow[0]
        self.tsEffective       = aoRow[1]
        self.tsExpire          = aoRow[2]
        self.uidAuthor         = aoRow[3]
        self.sShort            = aoRow[4]
        self.sFull             = aoRow[5]

        return self

    def initFromDbWithId(self, oDb, idFailureCategory, tsNow = None, sPeriodBack = None):
        """
        Initialize from the database, given the ID of a row.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   FailureCategories\n'
                                                       'WHERE  idFailureCategory = %s\n'
                                                       , ( idFailureCategory,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idFailureCategory=%s not found (tsNow=%s sPeriodBack=%s)'
                                % (idFailureCategory, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);


class FailureCategoryLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    Failure Category logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches Failure Category records.

        Returns an array (list) of FailureCategoryData items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     FailureCategories\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY idFailureCategory ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     FailureCategories\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY idFailureCategory ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,));

        aoRows = []
        for aoRow in self._oDb.fetchAll():
            aoRows.append(FailureCategoryData().initFromDbRow(aoRow))
        return aoRows


    def fetchForChangeLog(self, idFailureCategory, iStart, cMaxRows, tsNow): # pylint: disable=too-many-locals
        """
        Fetches change log entries for a failure reason.

        Returns an array of ChangeLogEntry instance and an indicator whether
        there are more entries.
        Raises exception on error.
        """
        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();

        # 1. Get a list of the relevant changes.
        self._oDb.execute('SELECT * FROM FailureCategories WHERE idFailureCategory = %s AND tsEffective <= %s\n'
                          'ORDER BY tsEffective DESC\n'
                          'LIMIT %s OFFSET %s\n'
                          , ( idFailureCategory, tsNow, cMaxRows + 1, iStart, ));
        aoRows = [];
        for aoChange in self._oDb.fetchAll():
            aoRows.append(FailureCategoryData().initFromDbRow(aoChange));

        # 2. Calculate the changes.
        aoEntries = [];
        for i in xrange(0, len(aoRows) - 1):
            oNew = aoRows[i];
            oOld = aoRows[i + 1];

            aoChanges = [];
            for sAttr in oNew.getDataAttributes():
                if sAttr not in [ 'tsEffective', 'tsExpire', 'uidAuthor', ]:
                    oOldAttr = getattr(oOld, sAttr);
                    oNewAttr = getattr(oNew, sAttr);
                    if oOldAttr != oNewAttr:
                        aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));

            aoEntries.append(ChangeLogEntry(oNew.uidAuthor, None, oNew.tsEffective, oNew.tsExpire, oNew, oOld, aoChanges));

        # If we're at the end of the log, add the initial entry.
        if len(aoRows) <= cMaxRows and aoRows:
            oNew = aoRows[-1];
            aoEntries.append(ChangeLogEntry(oNew.uidAuthor, None, oNew.tsEffective, oNew.tsExpire, oNew, None, []));

        return (UserAccountLogic(self._oDb).resolveChangeLogAuthors(aoEntries), len(aoRows) > cMaxRows);


    def getFailureCategoriesForCombo(self, tsEffective = None):
        """
        Gets the list of Failure Categories for a combo box.
        Returns an array of (value [idFailureCategory], drop-down-name [sShort],
        hover-text [sFull]) tuples.
        """
        if tsEffective is None:
            self._oDb.execute('SELECT   idFailureCategory, sShort, sFull\n'
                              'FROM     FailureCategories\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sShort')
        else:
            self._oDb.execute('SELECT   idFailureCategory, sShort, sFull\n'
                              'FROM     FailureCategories\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY sShort'
                              , (tsEffective, tsEffective))
        return self._oDb.fetchAll()


    def getById(self, idFailureCategory):
        """Get Failure Category data by idFailureCategory"""

        self._oDb.execute('SELECT   *\n'
                          'FROM     FailureCategories\n'
                          'WHERE    tsExpire   = \'infinity\'::timestamp\n'
                          '  AND    idFailureCategory = %s;', (idFailureCategory,))
        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise self._oDb.integrityException(
                'Found more than one failure categories with the same credentials. Database structure is corrupted.')
        try:
            return FailureCategoryData().initFromDbRow(aRows[0])
        except IndexError:
            return None


    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Add a failure reason category.
        """
        #
        # Validate inputs and read in the old(/current) data.
        #
        assert isinstance(oData, FailureCategoryData);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dErrors:
            raise TMInvalidData('editEntry invalid input: %s' % (dErrors,));

        #
        # Add the record.
        #
        self._readdEntry(uidAuthor, oData);
        self._oDb.maybeCommit(fCommit);
        return True;


    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Modifies a failure reason category.
        """

        #
        # Validate inputs and read in the old(/current) data.
        #
        assert isinstance(oData, FailureCategoryData);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('editEntry invalid input: %s' % (dErrors,));

        oOldData = FailureCategoryData().initFromDbWithId(self._oDb, oData.idFailureCategory);

        #
        # Update the data that needs updating.
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor', ]):
            self._historizeEntry(oData.idFailureCategory);
            self._readdEntry(uidAuthor, oData);
        self._oDb.maybeCommit(fCommit);
        return True;


    def removeEntry(self, uidAuthor, idFailureCategory, fCascade = False, fCommit = False):
        """
        Deletes a failure reason category.
        """
        _ = fCascade; # too complicated for now.

        #
        # Check whether it's being used by other tables and bitch if it is .
        # We currently do not implement cascading.
        #
        self._oDb.execute('SELECT   CONCAT(idFailureReason, \' - \', sShort)\n'
                          'FROM     FailureReasons\n'
                          'WHERE    idFailureCategory = %s\n'
                          '    AND  tsExpire = \'infinity\'::TIMESTAMP\n'
                          , (idFailureCategory,));
        aaoRows = self._oDb.fetchAll();
        if aaoRows:
            raise TMRowInUse('Cannot remove failure reason category %u because its being used by: %s'
                             % (idFailureCategory, ', '.join(aoRow[0] for aoRow in aaoRows),));

        #
        # Do the job.
        #
        oData = FailureCategoryData().initFromDbWithId(self._oDb, idFailureCategory);
        (tsCur, tsCurMinusOne) = self._oDb.getCurrentTimestamps();
        if oData.tsEffective not in (tsCur, tsCurMinusOne):
            self._historizeEntry(idFailureCategory, tsCurMinusOne);
            self._readdEntry(uidAuthor, oData, tsCurMinusOne);
        self._historizeEntry(idFailureCategory);
        self._oDb.maybeCommit(fCommit);
        return True;


    def cachedLookup(self, idFailureCategory):
        """
        Looks up the most recent FailureCategoryData object for idFailureCategory
        via an object cache.

        Returns a shared FailureCategoryData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('FailureCategory');

        oEntry = self.dCache.get(idFailureCategory, None);
        if oEntry is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     FailureCategories\n'
                              'WHERE    idFailureCategory = %s\n'
                              '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , (idFailureCategory, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     FailureCategories\n'
                                  'WHERE    idFailureCategory = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idFailureCategory, ));
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idFailureCategory));

            if self._oDb.getRowCount() == 1:
                oEntry = FailureCategoryData().initFromDbRow(self._oDb.fetchOne());
                self.dCache[idFailureCategory] = oEntry;
        return oEntry;


    #
    # Helpers.
    #

    def _readdEntry(self, uidAuthor, oData, tsEffective = None):
        """
        Re-adds the FailureCategories entry. Used by addEntry, editEntry and removeEntry.
        """
        if tsEffective is None:
            tsEffective = self._oDb.getCurrentTimestamp();
        self._oDb.execute('INSERT INTO FailureCategories (\n'
                          '         uidAuthor,\n'
                          '         tsEffective,\n'
                          '         idFailureCategory,\n'
                          '         sShort,\n'
                          '         sFull)\n'
                          'VALUES (%s, %s, '
                          + ('DEFAULT' if oData.idFailureCategory is None else str(oData.idFailureCategory))
                          + ', %s, %s)\n'
                          , ( uidAuthor,
                              tsEffective,
                              oData.sShort,
                              oData.sFull,) );
        return True;


    def _historizeEntry(self, idFailureCategory, tsExpire = None):
        """ Historizes the current entry. """
        if tsExpire is None:
            tsExpire = self._oDb.getCurrentTimestamp();
        self._oDb.execute('UPDATE FailureCategories\n'
                          'SET    tsExpire   = %s\n'
                          'WHERE  idFailureCategory = %s\n'
                          '   AND tsExpire          = \'infinity\'::TIMESTAMP\n'
                          , (tsExpire, idFailureCategory,));
        return True;

