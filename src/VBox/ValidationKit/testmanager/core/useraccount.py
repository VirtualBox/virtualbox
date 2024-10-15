# -*- coding: utf-8 -*-
# $Id: useraccount.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - User DB records management.
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
from testmanager            import config;
from testmanager.core.base  import ModelDataBase, ModelLogicBase, ModelDataBaseTestCase, TMTooManyRows, TMRowNotFound;


class UserAccountData(ModelDataBase):
    """
    User account data
    """

    ksIdAttr    = 'uid';

    ksParam_uid                 = 'UserAccount_uid'
    ksParam_tsExpire            = 'UserAccount_tsExpire'
    ksParam_tsEffective         = 'UserAccount_tsEffective'
    ksParam_uidAuthor           = 'UserAccount_uidAuthor'
    ksParam_sLoginName          = 'UserAccount_sLoginName'
    ksParam_sUsername           = 'UserAccount_sUsername'
    ksParam_sEmail              = 'UserAccount_sEmail'
    ksParam_sFullName           = 'UserAccount_sFullName'
    ksParam_fReadOnly           = 'UserAccount_fReadOnly'

    kasAllowNullAttributes      = ['uid', 'tsEffective', 'tsExpire', 'uidAuthor'];


    def __init__(self):
        """Init parameters"""
        ModelDataBase.__init__(self);
        self.uid            = None;
        self.tsEffective    = None;
        self.tsExpire       = None;
        self.uidAuthor      = None;
        self.sUsername      = None;
        self.sEmail         = None;
        self.sFullName      = None;
        self.sLoginName     = None;
        self.fReadOnly      = None;

    def initFromDbRow(self, aoRow):
        """
        Init from database table row
        Returns self. Raises exception of the row is None.
        """
        if aoRow is None:
            raise TMRowNotFound('User not found.');

        self.uid            = aoRow[0];
        self.tsEffective    = aoRow[1];
        self.tsExpire       = aoRow[2];
        self.uidAuthor      = aoRow[3];
        self.sUsername      = aoRow[4];
        self.sEmail         = aoRow[5];
        self.sFullName      = aoRow[6];
        self.sLoginName     = aoRow[7];
        self.fReadOnly      = aoRow[8];
        return self;

    def initFromDbWithId(self, oDb, uid, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   Users\n'
                                                       'WHERE  uid = %s\n'
                                                       , ( uid, ), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('uid=%s not found (tsNow=%s sPeriodBack=%s)' % (uid, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        # Custom handling of the email field.
        if sAttr == 'sEmail':
            return ModelDataBase.validateEmail(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);

        # Automatically lowercase the login name if we're supposed to do case
        # insensitive matching.  (The feature assumes lower case in DB.)
        if sAttr == 'sLoginName' and oValue is not None and config.g_kfLoginNameCaseInsensitive:
            oValue = oValue.lower();

        return ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);


class UserAccountLogic(ModelLogicBase):
    """
    User account logic (for the Users table).
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches user accounts.

        Returns an array (list) of UserAccountData items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;
        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     Users\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sUsername DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     Users\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY sUsername DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,));

        aoRows = [];
        for _ in range(self._oDb.getRowCount()):
            aoRows.append(UserAccountData().initFromDbRow(self._oDb.fetchOne()));
        return aoRows;

    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Add user account entry to the DB.
        """
        self._oDb.callProc('UserAccountLogic_addEntry',
                           (uidAuthor, oData.sUsername, oData.sEmail, oData.sFullName, oData.sLoginName, oData.fReadOnly));
        self._oDb.maybeCommit(fCommit);
        return True;

    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Modify user account.
        """
        self._oDb.callProc('UserAccountLogic_editEntry',
                           ( uidAuthor, oData.uid, oData.sUsername, oData.sEmail,
                             oData.sFullName, oData.sLoginName, oData.fReadOnly));
        self._oDb.maybeCommit(fCommit);
        return True;

    def removeEntry(self, uidAuthor, uid, fCascade = False, fCommit = False):
        """
        Delete user account
        """
        self._oDb.callProc('UserAccountLogic_delEntry', (uidAuthor, uid));
        self._oDb.maybeCommit(fCommit);
        _ = fCascade;
        return True;

    def _getByField(self, sField, sValue):
        """
        Get user account record by its field value
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     Users\n'
                          'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                          '  AND    ' + sField + ' = %s'
                          , (sValue,))

        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise TMTooManyRows('Found more than one user account with the same credentials. Database structure is corrupted.')

        try:
            return aRows[0]
        except IndexError:
            return []

    def getById(self, idUserId):
        """
        Get user account information by ID.
        """
        return self._getByField('uid', idUserId)

    def tryFetchAccountByLoginName(self, sLoginName):
        """
        Try get user account information by login name.

        Returns UserAccountData if found, None if not.
        Raises exception on DB error.
        """
        if config.g_kfLoginNameCaseInsensitive:
            sLoginName = sLoginName.lower();

        self._oDb.execute('SELECT   *\n'
                          'FROM     Users\n'
                          'WHERE    sLoginName = %s\n'
                          '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                          , (sLoginName, ));
        if self._oDb.getRowCount() != 1:
            if self._oDb.getRowCount() != 0:
                raise self._oDb.integrityException('%u rows in Users with sLoginName="%s"'
                                                   % (self._oDb.getRowCount(), sLoginName));
            return None;
        return UserAccountData().initFromDbRow(self._oDb.fetchOne());

    def cachedLookup(self, uid):
        """
        Looks up the current UserAccountData object for uid via an object cache.

        Returns a shared UserAccountData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('UserAccount');

        oUser = self.dCache.get(uid, None);
        if oUser is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     Users\n'
                              'WHERE    uid = %s\n'
                              '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , (uid, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     Users\n'
                                  'WHERE    uid = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (uid, ));
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), uid));

            if self._oDb.getRowCount() == 1:
                oUser = UserAccountData().initFromDbRow(self._oDb.fetchOne());
                self.dCache[uid] = oUser;
        return oUser;

    def resolveChangeLogAuthors(self, aoEntries):
        """
        Given an array of ChangeLogEntry instances, set sAuthor to whatever
        uidAuthor resolves to.

        Returns aoEntries.
        Raises exception on DB error.
        """
        for oEntry in aoEntries:
            oUser = self.cachedLookup(oEntry.uidAuthor)
            if oUser is not None:
                oEntry.sAuthor = oUser.sUsername;
        return aoEntries;


#
# Unit testing.
#

# pylint: disable=missing-docstring
class UserAccountDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [UserAccountData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

