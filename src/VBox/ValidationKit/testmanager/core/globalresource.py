# -*- coding: utf-8 -*-
# $Id: globalresource.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Global Resources.
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
from testmanager.core.base import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMRowNotFound;


class GlobalResourceData(ModelDataBase):
    """
    Global resource data
    """

    ksIdAttr = 'idGlobalRsrc';

    ksParam_idGlobalRsrc        = 'GlobalResource_idGlobalRsrc'
    ksParam_tsEffective         = 'GlobalResource_tsEffective'
    ksParam_tsExpire            = 'GlobalResource_tsExpire'
    ksParam_uidAuthor           = 'GlobalResource_uidAuthor'
    ksParam_sName               = 'GlobalResource_sName'
    ksParam_sDescription        = 'GlobalResource_sDescription'
    ksParam_fEnabled            = 'GlobalResource_fEnabled'

    kasAllowNullAttributes      = ['idGlobalRsrc', 'tsEffective', 'tsExpire', 'uidAuthor', 'sDescription' ];
    kcchMin_sName               = 2;
    kcchMax_sName               = 64;

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idGlobalRsrc       = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.sName              = None;
        self.sDescription       = None;
        self.fEnabled           = False

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT * FROM GlobalResources row.
        Returns self. Raises exception if no row.
        """
        if aoRow is None:
            raise TMRowNotFound('Global resource not found.')

        self.idGlobalRsrc       = aoRow[0]
        self.tsEffective        = aoRow[1]
        self.tsExpire           = aoRow[2]
        self.uidAuthor          = aoRow[3]
        self.sName              = aoRow[4]
        self.sDescription       = aoRow[5]
        self.fEnabled           = aoRow[6]
        return self

    def initFromDbWithId(self, oDb, idGlobalRsrc, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   GlobalResources\n'
                                                       'WHERE  idGlobalRsrc = %s\n'
                                                       , ( idGlobalRsrc,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idGlobalRsrc=%s not found (tsNow=%s sPeriodBack=%s)' % (idGlobalRsrc, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def isEqual(self, oOther):
        """
        Compares two instances.
        """
        return       self.idGlobalRsrc == oOther.idGlobalRsrc \
            and str(self.tsEffective)  == str(oOther.tsEffective) \
            and str(self.tsExpire)     == str(oOther.tsExpire) \
            and      self.uidAuthor    == oOther.uidAuthor \
            and      self.sName        == oOther.sName \
            and      self.sDescription == oOther.sDescription \
            and      self.fEnabled     == oOther.fEnabled


class GlobalResourceLogic(ModelLogicBase):
    """
    Global resource logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Returns an array (list) of FailureReasonData items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     GlobalResources\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY idGlobalRsrc DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     GlobalResources\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY idGlobalRsrc DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,))

        aoRows = []
        for aoRow in self._oDb.fetchAll():
            aoRows.append(GlobalResourceData().initFromDbRow(aoRow))
        return aoRows


    def cachedLookup(self, idGlobalRsrc):
        """
        Looks up the most recent GlobalResourceData object for idGlobalRsrc
        via an object cache.

        Returns a shared GlobalResourceData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('GlobalResourceData');
        oEntry = self.dCache.get(idGlobalRsrc, None);
        if oEntry is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     GlobalResources\n'
                              'WHERE    idGlobalRsrc = %s\n'
                              '     AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                              , (idGlobalRsrc, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     GlobalResources\n'
                                  'WHERE    idGlobalRsrc = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idGlobalRsrc, ));
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idGlobalRsrc));

            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                oEntry = GlobalResourceData();
                oEntry.initFromDbRow(aaoRow);
                self.dCache[idGlobalRsrc] = oEntry;
        return oEntry;


    def getAll(self, tsEffective = None):
        """
        Gets all global resources.

        Returns an array of GlobalResourceData instances on success (can be
        empty).  Raises exception on database error.
        """
        if tsEffective is not None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     GlobalResources\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              , (tsEffective, tsEffective));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     GlobalResources\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n');
        aaoRows = self._oDb.fetchAll();
        aoRet = [];
        for aoRow in aaoRows:
            aoRet.append(GlobalResourceData().initFromDbRow(aoRow));

        return aoRet;

    def addGlobalResource(self, uidAuthor, oData):
        """Add Global Resource DB record"""
        self._oDb.execute('SELECT * FROM add_globalresource(%s, %s, %s, %s);',
                          (uidAuthor,
                           oData.sName,
                           oData.sDescription,
                           oData.fEnabled))
        self._oDb.commit()
        return True

    def editGlobalResource(self, uidAuthor, idGlobalRsrc, oData):
        """Modify Global Resource DB record"""
        # Check if anything has been changed
        oGlobalResourcesDataOld = self.getById(idGlobalRsrc)
        if oGlobalResourcesDataOld.isEqual(oData):
            # Nothing has been changed, do nothing
            return True

        self._oDb.execute('SELECT * FROM update_globalresource(%s, %s, %s, %s, %s);',
                          (uidAuthor,
                           idGlobalRsrc,
                           oData.sName,
                           oData.sDescription,
                           oData.fEnabled))
        self._oDb.commit()
        return True

    def remove(self, uidAuthor, idGlobalRsrc):
        """Delete Global Resource DB record"""
        self._oDb.execute('SELECT * FROM del_globalresource(%s, %s);',
                          (uidAuthor, idGlobalRsrc))
        self._oDb.commit()
        return True

    def getById(self, idGlobalRsrc):
        """
        Get global resource record by its id
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     GlobalResources\n'
                          'WHERE    tsExpire = \'infinity\'::timestamp\n'
                          '  AND    idGlobalRsrc=%s;', (idGlobalRsrc,))

        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise self._oDb.integrityException('Duplicate global resource entry with ID %u (current)' % (idGlobalRsrc,));
        try:
            return GlobalResourceData().initFromDbRow(aRows[0])
        except IndexError:
            raise TMRowNotFound('Global resource not found.')

    def allocateResources(self, idTestBox, aoGlobalRsrcs, fCommit = False):
        """
        Allocates the given global resource.

        Returns True of successfully allocated the resources, False if not.
        May raise exception on DB error.
        """
        # Quit quickly if there is nothing to alloocate.
        if not aoGlobalRsrcs:
            return True;

        #
        # Note! Someone else might have allocated the resources since the
        #       scheduler check that they were available. In such case we
        #       need too quietly rollback and return FALSE.
        #
        self._oDb.execute('SAVEPOINT allocateResources');

        for oGlobalRsrc in aoGlobalRsrcs:
            try:
                self._oDb.execute('INSERT INTO GlobalResourceStatuses (idGlobalRsrc, idTestBox)\n'
                                  'VALUES (%s, %s)', (oGlobalRsrc.idGlobalRsrc, idTestBox, ) );
            except self._oDb.oXcptError:
                self._oDb.execute('ROLLBACK TO SAVEPOINT allocateResources');
                return False;

        self._oDb.execute('RELEASE SAVEPOINT allocateResources');
        self._oDb.maybeCommit(fCommit);
        return True;

    def freeGlobalResourcesByTestBox(self, idTestBox, fCommit = False):
        """
        Frees all global resources own by the given testbox.
        Returns True. May raise exception on DB error.
        """
        self._oDb.execute('DELETE FROM GlobalResourceStatuses\n'
                          'WHERE    idTestBox = %s\n', (idTestBox, ) );
        self._oDb.maybeCommit(fCommit);
        return True;

#
# Unit testing.
#

# pylint: disable=missing-docstring
class GlobalResourceDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [GlobalResourceData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

