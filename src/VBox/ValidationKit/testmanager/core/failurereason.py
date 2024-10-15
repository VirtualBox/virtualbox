# -*- coding: utf-8 -*-
# $Id: failurereason.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Failure Reasons.
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
from testmanager.core.base              import ModelDataBase, ModelLogicBase, TMRowNotFound, TMInvalidData, TMRowInUse, \
                                               AttributeChangeEntry, ChangeLogEntry;
from testmanager.core.useraccount       import UserAccountLogic;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


class FailureReasonData(ModelDataBase):
    """
    Failure Reason Data.
    """

    ksIdAttr = 'idFailureReason';

    ksParam_idFailureReason   = 'FailureReasonData_idFailureReason'
    ksParam_tsEffective       = 'FailureReasonData_tsEffective'
    ksParam_tsExpire          = 'FailureReasonData_tsExpire'
    ksParam_uidAuthor         = 'FailureReasonData_uidAuthor'
    ksParam_idFailureCategory = 'FailureReasonData_idFailureCategory'
    ksParam_sShort            = 'FailureReasonData_sShort'
    ksParam_sFull             = 'FailureReasonData_sFull'
    ksParam_iTicket           = 'FailureReasonData_iTicket'
    ksParam_asUrls            = 'FailureReasonData_asUrls'

    kasAllowNullAttributes    = [ 'idFailureReason', 'tsEffective', 'tsExpire',
                                  'uidAuthor',       'iTicket',      'asUrls' ]

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #

        self.idFailureReason   = None
        self.tsEffective       = None
        self.tsExpire          = None
        self.uidAuthor         = None
        self.idFailureCategory = None
        self.sShort            = None
        self.sFull             = None
        self.iTicket           = None
        self.asUrls            = None

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the data with a row from a SELECT * FROM FailureReasons.

        Returns self. Raises exception if the row is None or otherwise invalid.
        """

        if aoRow is None:
            raise TMRowNotFound('Failure Reason not found.');

        self.idFailureReason   = aoRow[0]
        self.tsEffective       = aoRow[1]
        self.tsExpire          = aoRow[2]
        self.uidAuthor         = aoRow[3]
        self.idFailureCategory = aoRow[4]
        self.sShort            = aoRow[5]
        self.sFull             = aoRow[6]
        self.iTicket           = aoRow[7]
        self.asUrls            = aoRow[8]

        return self;

    def initFromDbWithId(self, oDb, idFailureReason, tsNow = None, sPeriodBack = None):
        """
        Initialize from the database, given the ID of a row.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   FailureReasons\n'
                                                       'WHERE  idFailureReason = %s\n'
                                                       , ( idFailureReason,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idFailureReason=%s not found (tsNow=%s sPeriodBack=%s)'
                                % (idFailureReason, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);


class FailureReasonDataEx(FailureReasonData):
    """
    Failure Reason Data, extended version that includes the category.
    """

    def __init__(self):
        FailureReasonData.__init__(self);
        self.oCategory  = None;
        self.oAuthor    = None;

    def initFromDbRowEx(self, aoRow, oCategoryLogic, oUserAccountLogic):
        """
        Re-initializes the data with a row from a SELECT * FROM FailureReasons.

        Returns self. Raises exception if the row is None or otherwise invalid.
        """

        self.initFromDbRow(aoRow);
        self.oCategory  = oCategoryLogic.cachedLookup(self.idFailureCategory);
        self.oAuthor    = oUserAccountLogic.cachedLookup(self.uidAuthor);

        return self;


class FailureReasonLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    Failure Reason logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;
        self.dCacheNameAndCat = None;
        self.oCategoryLogic = None;
        self.oUserAccountLogic = None;

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches Failure Category records.

        Returns an array (list) of FailureReasonDataEx items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;
        self._ensureCachesPresent();

        if tsNow is None:
            self._oDb.execute('SELECT   FailureReasons.*,\n'
                              '         FailureCategories.sShort AS sCategory\n'
                              'FROM     FailureReasons,\n'
                              '         FailureCategories\n'
                              'WHERE    FailureReasons.tsExpire             = \'infinity\'::TIMESTAMP\n'
                              '     AND FailureCategories.idFailureCategory = FailureReasons.idFailureCategory\n'
                              '     AND FailureCategories.tsExpire          = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sCategory ASC, sShort ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   FailureReasons.*,\n'
                              '         FailureCategories.sShort AS sCategory\n'
                              'FROM     FailureReasons,\n'
                              '         FailureCategories\n'
                              'WHERE    FailureReasons.tsExpire     > %s\n'
                              '     AND FailureReasons.tsEffective <= %s\n'
                              '     AND FailureCategories.idFailureCategory = FailureReasons.idFailureCategory\n'
                              '     AND FailureReasons.tsExpire     > %s\n'
                              '     AND FailureReasons.tsEffective <= %s\n'
                              'ORDER BY sCategory ASC, sShort ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, tsNow, tsNow, cMaxRows, iStart,));

        aoRows = []
        for aoRow in self._oDb.fetchAll():
            aoRows.append(FailureReasonDataEx().initFromDbRowEx(aoRow, self.oCategoryLogic, self.oUserAccountLogic));
        return aoRows

    def fetchForListingInCategory(self, iStart, cMaxRows, tsNow, idFailureCategory, aiSortColumns = None):
        """
        Fetches Failure Category records.

        Returns an array (list) of FailureReasonDataEx items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;
        self._ensureCachesPresent();

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     FailureReasons\n'
                              'WHERE    tsExpire          = \'infinity\'::TIMESTAMP\n'
                              '     AND idFailureCategory = %s\n'
                              'ORDER BY sShort ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , ( idFailureCategory, cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     FailureReasons\n'
                              'WHERE    idFailureCategory = %s\n'
                              '     AND tsExpire          > %s\n'
                              '     AND tsEffective      <= %s\n'
                              'ORDER BY sShort ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , ( idFailureCategory, tsNow, tsNow, cMaxRows, iStart,));

        aoRows = []
        for aoRow in self._oDb.fetchAll():
            aoRows.append(FailureReasonDataEx().initFromDbRowEx(aoRow, self.oCategoryLogic, self.oUserAccountLogic));
        return aoRows


    def fetchForSheriffByNamedCategory(self, sFailureCategory):
        """
        Fetches the short names of the reasons in the named category.

        Returns array of strings.
        Raises exception on error.
        """
        self._oDb.execute('SELECT   FailureReasons.sShort\n'
                          'FROM     FailureReasons,\n'
                          '         FailureCategories\n'
                          'WHERE    FailureReasons.tsExpire          = \'infinity\'::TIMESTAMP\n'
                          '     AND FailureReasons.idFailureCategory = FailureCategories.idFailureCategory\n'
                          '     AND FailureCategories.sShort         = %s\n'
                          'ORDER BY FailureReasons.sShort ASC\n'
                          , ( sFailureCategory,));
        return [aoRow[0] for aoRow in self._oDb.fetchAll()];


    def fetchForCombo(self, sFirstEntry = 'Select a failure reason', tsEffective = None):
        """
        Gets the list of Failure Reasons for a combo box.
        Returns an array of (value [idFailureReason], drop-down-name [sShort],
        hover-text [sFull]) tuples.
        """
        if tsEffective is None:
            self._oDb.execute('SELECT   fr.idFailureReason, CONCAT(fc.sShort, \' / \', fr.sShort) as sComboText, fr.sFull\n'
                              'FROM     FailureReasons fr,\n'
                              '         FailureCategories fc\n'
                              'WHERE    fr.idFailureCategory = fc.idFailureCategory\n'
                              '  AND    fr.tsExpire = \'infinity\'::TIMESTAMP\n'
                              '  AND    fc.tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sComboText')
        else:
            self._oDb.execute('SELECT   fr.idFailureReason, CONCAT(fc.sShort, \' / \', fr.sShort) as sComboText, fr.sFull\n'
                              'FROM     FailureReasons fr,\n'
                              '         FailureCategories fc\n'
                              'WHERE    fr.idFailureCategory = fc.idFailureCategory\n'
                              '  AND    fr.tsExpire     > %s\n'
                              '  AND    fr.tsEffective <= %s\n'
                              '  AND    fc.tsExpire     > %s\n'
                              '  AND    fc.tsEffective <= %s\n'
                              'ORDER BY sComboText'
                              , (tsEffective, tsEffective, tsEffective, tsEffective));
        aoRows = self._oDb.fetchAll();
        return [(-1, sFirstEntry, '')] + aoRows;


    def fetchForChangeLog(self, idFailureReason, iStart, cMaxRows, tsNow): # pylint: disable=too-many-locals
        """
        Fetches change log entries for a failure reason.

        Returns an array of ChangeLogEntry instance and an indicator whether
        there are more entries.
        Raises exception on error.
        """
        self._ensureCachesPresent();

        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();

        # 1. Get a list of the relevant changes.
        self._oDb.execute('SELECT * FROM FailureReasons WHERE idFailureReason = %s AND tsEffective <= %s\n'
                          'ORDER BY tsEffective DESC\n'
                          'LIMIT %s OFFSET %s\n'
                          , ( idFailureReason, tsNow, cMaxRows + 1, iStart, ));
        aoRows = [];
        for aoChange in self._oDb.fetchAll():
            aoRows.append(FailureReasonData().initFromDbRow(aoChange));

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
                        if sAttr == 'idFailureCategory':
                            oCat = self.oCategoryLogic.cachedLookup(oOldAttr);
                            if oCat is not None:
                                oOldAttr = '%s (%s)' % (oOldAttr, oCat.sShort, );
                            oCat = self.oCategoryLogic.cachedLookup(oNewAttr);
                            if oCat is not None:
                                oNewAttr = '%s (%s)' % (oNewAttr, oCat.sShort, );
                        aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));

            aoEntries.append(ChangeLogEntry(oNew.uidAuthor, None, oNew.tsEffective, oNew.tsExpire, oNew, oOld, aoChanges));

        # If we're at the end of the log, add the initial entry.
        if len(aoRows) <= cMaxRows and aoRows:
            oNew = aoRows[-1];
            aoEntries.append(ChangeLogEntry(oNew.uidAuthor, None, oNew.tsEffective, oNew.tsExpire, oNew, None, []));

        return (UserAccountLogic(self._oDb).resolveChangeLogAuthors(aoEntries), len(aoRows) > cMaxRows);


    def getById(self, idFailureReason):
        """Get Failure Reason data by idFailureReason"""

        self._oDb.execute('SELECT   *\n'
                          'FROM     FailureReasons\n'
                          'WHERE    tsExpire   = \'infinity\'::timestamp\n'
                          '  AND    idFailureReason = %s;', (idFailureReason,))
        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise self._oDb.integrityException(
                'Found more than one failure reasons with the same credentials. Database structure is corrupted.')
        try:
            return FailureReasonData().initFromDbRow(aRows[0])
        except IndexError:
            return None


    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Add a failure reason.
        """
        #
        # Validate.
        #
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dErrors:
            raise TMInvalidData('addEntry invalid input: %s' % (dErrors,));

        #
        # Add the record.
        #
        self._readdEntry(uidAuthor, oData);
        self._oDb.maybeCommit(fCommit);
        return True;


    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Modifies a failure reason.
        """

        #
        # Validate inputs and read in the old(/current) data.
        #
        assert isinstance(oData, FailureReasonData);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('editEntry invalid input: %s' % (dErrors,));

        oOldData = FailureReasonData().initFromDbWithId(self._oDb, oData.idFailureReason);

        #
        # Update the data that needs updating.
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor', ]):
            self._historizeEntry(oData.idFailureReason);
            self._readdEntry(uidAuthor, oData);
        self._oDb.maybeCommit(fCommit);
        return True;


    def removeEntry(self, uidAuthor, idFailureReason, fCascade = False, fCommit = False):
        """
        Deletes a failure reason.
        """
        _ = fCascade; # too complicated for now.

        #
        # Check whether it's being used by other tables and bitch if it is .
        # We currently do not implement cascading.
        #
        self._oDb.execute('SELECT   CONCAT(idBlacklisting, \' - blacklisting\')\n'
                          'FROM     BuildBlacklist\n'
                          'WHERE    idFailureReason = %s\n'
                          '    AND  tsExpire = \'infinity\'::TIMESTAMP\n'
                          'UNION\n'
                          'SELECT   CONCAT(idTestResult, \' - test result failure reason\')\n'
                          'FROM     TestResultFailures\n'
                          'WHERE    idFailureReason = %s\n'
                          '    AND  tsExpire = \'infinity\'::TIMESTAMP\n'
                          , (idFailureReason, idFailureReason,));
        aaoRows = self._oDb.fetchAll();
        if aaoRows:
            raise TMRowInUse('Cannot remove failure reason %u because its being used by: %s'
                             % (idFailureReason, ', '.join(aoRow[0] for aoRow in aaoRows),));

        #
        # Do the job.
        #
        oData = FailureReasonData().initFromDbWithId(self._oDb, idFailureReason);
        assert oData.idFailureReason == idFailureReason;
        (tsCur, tsCurMinusOne) = self._oDb.getCurrentTimestamps();
        if oData.tsEffective not in (tsCur, tsCurMinusOne):
            self._historizeEntry(idFailureReason, tsCurMinusOne);
            self._readdEntry(uidAuthor, oData, tsCurMinusOne);
        self._historizeEntry(idFailureReason);
        self._oDb.maybeCommit(fCommit);
        return True;


    def cachedLookup(self, idFailureReason):
        """
        Looks up the most recent FailureReasonDataEx object for idFailureReason
        via an object cache.

        Returns a shared FailureReasonData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('FailureReasonDataEx');
        oEntry = self.dCache.get(idFailureReason, None);
        if oEntry is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     FailureReasons\n'
                              'WHERE    idFailureReason = %s\n'
                              '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , (idFailureReason, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     FailureReasons\n'
                                  'WHERE    idFailureReason = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idFailureReason, ));
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idFailureReason));

            if self._oDb.getRowCount() == 1:
                self._ensureCachesPresent();
                oEntry = FailureReasonDataEx().initFromDbRowEx(self._oDb.fetchOne(), self.oCategoryLogic,
                                                               self.oUserAccountLogic);
                self.dCache[idFailureReason] = oEntry;
        return oEntry;


    def cachedLookupByNameAndCategory(self, sName, sCategory):
        """
        Looks up a failure reason by it's name and category.

        Should the request be ambigiuos, we'll return the oldest one.

        Returns a shared FailureReasonData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCacheNameAndCat is None:
            self.dCacheNameAndCat = self._oDb.getCache('FailureReasonDataEx-By-Name-And-Category');
        sKey = '%s:::%s' % (sName, sCategory,);
        oEntry = self.dCacheNameAndCat.get(sKey, None);
        if oEntry is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     FailureReasons,\n'
                              '         FailureCategories\n'
                              'WHERE    FailureReasons.sShort            = %s\n'
                              '     AND FailureReasons.tsExpire          = \'infinity\'::TIMESTAMP\n'
                              '     AND FailureReasons.idFailureCategory = FailureCategories.idFailureCategory '
                              '     AND FailureCategories.sShort         = %s\n'
                              '     AND FailureCategories.tsExpire       = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY FailureReasons.tsEffective\n'
                              , ( sName, sCategory));
            if self._oDb.getRowCount() == 0:
                sLikeSucks = self._oDb.formatBindArgs(
                                  'SELECT   *\n'
                                  'FROM     FailureReasons,\n'
                                  '         FailureCategories\n'
                                  'WHERE    (   FailureReasons.sShort    ILIKE @@@@@@@! %s !@@@@@@@\n'
                                  '          OR FailureReasons.sFull     ILIKE @@@@@@@! %s !@@@@@@@)\n'
                                  '     AND FailureCategories.tsExpire       = \'infinity\'::TIMESTAMP\n'
                                  '     AND FailureReasons.idFailureCategory = FailureCategories.idFailureCategory\n'
                                  '     AND (   FailureCategories.sShort     = %s\n'
                                  '          OR FailureCategories.sFull      = %s)\n'
                                  '     AND FailureReasons.tsExpire          = \'infinity\'::TIMESTAMP\n'
                                  'ORDER BY FailureReasons.tsEffective\n'
                                  , ( sName, sName, sCategory, sCategory ));
                sLikeSucks = sLikeSucks.replace('LIKE @@@@@@@! \'', 'LIKE \'%').replace('\' !@@@@@@@', '%\'');
                self._oDb.execute(sLikeSucks);
            if self._oDb.getRowCount() > 0:
                self._ensureCachesPresent();
                oEntry = FailureReasonDataEx().initFromDbRowEx(self._oDb.fetchOne(), self.oCategoryLogic,
                                                               self.oUserAccountLogic);
                self.dCacheNameAndCat[sKey] = oEntry;
                if sName != oEntry.sShort or sCategory != oEntry.oCategory.sShort:
                    sKey2 = '%s:::%s' % (oEntry.sShort, oEntry.oCategory.sShort,);
                    self.dCacheNameAndCat[sKey2] = oEntry;
        return oEntry;


    #
    # Helpers.
    #

    def _readdEntry(self, uidAuthor, oData, tsEffective = None):
        """
        Re-adds the FailureReasons entry. Used by addEntry, editEntry and removeEntry.
        """
        if tsEffective is None:
            tsEffective = self._oDb.getCurrentTimestamp();
        self._oDb.execute('INSERT INTO FailureReasons (\n'
                          '         uidAuthor,\n'
                          '         tsEffective,\n'
                          '         idFailureReason,\n'
                          '         idFailureCategory,\n'
                          '         sShort,\n'
                          '         sFull,\n'
                          '         iTicket,\n'
                          '         asUrls)\n'
                          'VALUES (%s, %s, '
                          + ( 'DEFAULT' if oData.idFailureReason is None else str(oData.idFailureReason) )
                          + ', %s, %s, %s, %s, %s)\n'
                          , ( uidAuthor,
                              tsEffective,
                              oData.idFailureCategory,
                              oData.sShort,
                              oData.sFull,
                              oData.iTicket,
                              oData.asUrls,) );
        return True;


    def _historizeEntry(self, idFailureReason, tsExpire = None):
        """ Historizes the current entry. """
        if tsExpire is None:
            tsExpire = self._oDb.getCurrentTimestamp();
        self._oDb.execute('UPDATE FailureReasons\n'
                          'SET    tsExpire   = %s\n'
                          'WHERE  idFailureReason = %s\n'
                          '   AND tsExpire        = \'infinity\'::TIMESTAMP\n'
                          , (tsExpire, idFailureReason,));
        return True;


    def _ensureCachesPresent(self):
        """ Ensures we've got the cache references resolved. """
        if self.oCategoryLogic is None:
            from testmanager.core.failurecategory import FailureCategoryLogic;
            self.oCategoryLogic = FailureCategoryLogic(self._oDb);
        if self.oUserAccountLogic is None:
            self.oUserAccountLogic = UserAccountLogic(self._oDb);
        return True;

