# -*- coding: utf-8 -*-
# $Id: buildsource.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Build Sources.
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
from common                             import utils;
from testmanager.core.base              import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMRowAlreadyExists, \
                                               TMRowInUse, TMInvalidData, TMRowNotFound;
from testmanager.core                   import coreconsts;


class BuildSourceData(ModelDataBase):
    """
    A build source.
    """

    ksIdAttr = 'idBuildSrc';

    ksParam_idBuildSrc          = 'BuildSource_idBuildSrc';
    ksParam_tsEffective         = 'BuildSource_tsEffective';
    ksParam_tsExpire            = 'BuildSource_tsExpire';
    ksParam_uidAuthor           = 'BuildSource_uidAuthor';
    ksParam_sName               = 'BuildSource_sName';
    ksParam_sDescription        = 'BuildSource_sDescription';
    ksParam_sProduct            = 'BuildSource_sProduct';
    ksParam_sBranch             = 'BuildSource_sBranch';
    ksParam_asTypes             = 'BuildSource_asTypes';
    ksParam_asOsArches          = 'BuildSource_asOsArches';
    ksParam_iFirstRevision      = 'BuildSource_iFirstRevision';
    ksParam_iLastRevision       = 'BuildSource_iLastRevision';
    ksParam_cSecMaxAge          = 'BuildSource_cSecMaxAge';

    kasAllowNullAttributes      = [ 'idBuildSrc', 'tsEffective', 'tsExpire', 'uidAuthor', 'sDescription', 'asTypes',
                                    'asOsArches', 'iFirstRevision', 'iLastRevision', 'cSecMaxAge' ];

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idBuildSrc         = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.sName              = None;
        self.sDescription       = None;
        self.sProduct           = None;
        self.sBranch            = None;
        self.asTypes            = None;
        self.asOsArches         = None;
        self.iFirstRevision     = None;
        self.iLastRevision      = None;
        self.cSecMaxAge         = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the object from a SELECT * FROM BuildSources row.
        Returns self.  Raises exception if aoRow is None.
        """
        if aoRow is None:
            raise TMRowNotFound('Build source not found.');

        self.idBuildSrc         = aoRow[0];
        self.tsEffective        = aoRow[1];
        self.tsExpire           = aoRow[2];
        self.uidAuthor          = aoRow[3];
        self.sName              = aoRow[4];
        self.sDescription       = aoRow[5];
        self.sProduct           = aoRow[6];
        self.sBranch            = aoRow[7];
        self.asTypes            = aoRow[8];
        self.asOsArches         = aoRow[9];
        self.iFirstRevision     = aoRow[10];
        self.iLastRevision      = aoRow[11];
        self.cSecMaxAge         = aoRow[12];
        return self;

    def initFromDbWithId(self, oDb, idBuildSrc, tsNow = None, sPeriodBack = None):
        """
        Initialize from the database, given the ID of a row.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   BuildSources\n'
                                                       'WHERE  idBuildSrc   = %s\n'
                                                       , ( idBuildSrc,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idBuildSrc=%s not found (tsNow=%s sPeriodBack=%s)' % (idBuildSrc, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        # Handle asType and asOsArches specially.
        if sAttr == 'sType':
            (oNewValue, sError) = ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue,
                                                                             aoNilValues, fAllowNull, oDb);
            if sError is None:
                if not self.asTypes:
                    oNewValue = None;
                else:
                    for sType in oNewValue:
                        if len(sType) < 2  or  sType.lower() != sType:
                            if sError is None:  sError  = '';
                            else:               sError += ', ';
                            sError += 'invalid value "%s"' % (sType,);

        elif sAttr == 'asOsArches':
            (oNewValue, sError) = self.validateListOfStr(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                         asValidValues = coreconsts.g_kasOsDotCpusAll);
            if sError is not None  and  oNewValue is not None:
                oNewValue = sorted(oNewValue); # Must be sorted!

        elif sAttr == 'cSecMaxAge' and oValue not in aoNilValues: # Allow human readable interval formats.
            (oNewValue, sError) = utils.parseIntervalSeconds(oValue);
        else:
            return ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);

        return (oNewValue, sError);

class BuildSourceLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    Build source database logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    #
    # Standard methods.
    #

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches build sources.

        Returns an array (list) of BuildSourceData items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     BuildSources\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY idBuildSrc DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     BuildSources\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY idBuildSrc DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,));

        aoRows = []
        for aoRow in self._oDb.fetchAll():
            aoRows.append(BuildSourceData().initFromDbRow(aoRow))
        return aoRows

    def fetchForCombo(self):
        """Fetch data which is aimed to be passed to HTML form"""
        self._oDb.execute('SELECT   idBuildSrc, sName, sProduct\n'
                          'FROM     BuildSources\n'
                          'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                          'ORDER BY idBuildSrc DESC\n')
        asRet = self._oDb.fetchAll();
        asRet.insert(0, (-1, 'None', 'None'));
        return asRet;


    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Add a new build source to the database.
        """

        #
        # Validate the input.
        #
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dErrors:
            raise TMInvalidData('addEntry invalid input: %s' % (dErrors,));
        self._assertUnique(oData, None);

        #
        # Add it.
        #
        self._oDb.execute('INSERT INTO BuildSources (\n'
                          '         uidAuthor,\n'
                          '         sName,\n'
                          '         sDescription,\n'
                          '         sProduct,\n'
                          '         sBranch,\n'
                          '         asTypes,\n'
                          '         asOsArches,\n'
                          '         iFirstRevision,\n'
                          '         iLastRevision,\n'
                          '         cSecMaxAge)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)\n'
                          , ( uidAuthor,
                              oData.sName,
                              oData.sDescription,
                              oData.sProduct,
                              oData.sBranch,
                              oData.asTypes,
                              oData.asOsArches,
                              oData.iFirstRevision,
                              oData.iLastRevision,
                              oData.cSecMaxAge, ));

        self._oDb.maybeCommit(fCommit);
        return True;

    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Modifies a build source.
        """

        #
        # Validate the input and read the old entry.
        #
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('addEntry invalid input: %s' % (dErrors,));
        self._assertUnique(oData, oData.idBuildSrc);
        oOldData = BuildSourceData().initFromDbWithId(self._oDb, oData.idBuildSrc);

        #
        # Make the changes (if something actually changed).
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor', ]):
            self._historizeBuildSource(oData.idBuildSrc);
            self._oDb.execute('INSERT INTO BuildSources (\n'
                              '         uidAuthor,\n'
                              '         idBuildSrc,\n'
                              '         sName,\n'
                              '         sDescription,\n'
                              '         sProduct,\n'
                              '         sBranch,\n'
                              '         asTypes,\n'
                              '         asOsArches,\n'
                              '         iFirstRevision,\n'
                              '         iLastRevision,\n'
                              '         cSecMaxAge)\n'
                              'VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)\n'
                              , ( uidAuthor,
                                  oData.idBuildSrc,
                                  oData.sName,
                                  oData.sDescription,
                                  oData.sProduct,
                                  oData.sBranch,
                                  oData.asTypes,
                                  oData.asOsArches,
                                  oData.iFirstRevision,
                                  oData.iLastRevision,
                                  oData.cSecMaxAge, ));
        self._oDb.maybeCommit(fCommit);
        return True;

    def removeEntry(self, uidAuthor, idBuildSrc, fCascade = False, fCommit = False):
        """
        Deletes a build sources.
        """

        #
        # Check cascading.
        #
        if fCascade is not True:
            self._oDb.execute('SELECT   idSchedGroup, sName\n'
                              'FROM     SchedGroups\n'
                              'WHERE    idBuildSrc          = %s\n'
                              '     OR  idBuildSrcTestSuite = %s\n'
                              , (idBuildSrc, idBuildSrc,));
            if self._oDb.getRowCount() > 0:
                asGroups = [];
                for aoRow in self._oDb.fetchAll():
                    asGroups.append('%s (#%d)' % (aoRow[1], aoRow[0]));
                raise TMRowInUse('Build source #%d is used by one or more scheduling groups: %s'
                                 % (idBuildSrc, ', '.join(asGroups),));
        else:
            self._oDb.execute('UPDATE   SchedGroups\n'
                              'SET      idBuildSrc = NULL\n'
                              'WHERE    idBuildSrc = %s'
                              , ( idBuildSrc,));
            self._oDb.execute('UPDATE   SchedGroups\n'
                              'SET      idBuildSrcTestSuite = NULL\n'
                              'WHERE    idBuildSrcTestSuite = %s'
                              , ( idBuildSrc,));

        #
        # Do the job.
        #
        self._historizeBuildSource(idBuildSrc, None);
        _ = uidAuthor; ## @todo record deleter.

        self._oDb.maybeCommit(fCommit);
        return True;

    def cachedLookup(self, idBuildSrc):
        """
        Looks up the most recent BuildSourceData object for idBuildSrc
        via an object cache.

        Returns a shared BuildSourceData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('BuildSourceData');
        oEntry = self.dCache.get(idBuildSrc, None);
        if oEntry is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     BuildSources\n'
                              'WHERE    idBuildSrc = %s\n'
                              '     AND tsExpire   = \'infinity\'::TIMESTAMP\n'
                              , (idBuildSrc, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     BuildSources\n'
                                  'WHERE    idBuildSrc = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idBuildSrc, ));
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idBuildSrc));

            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                oEntry = BuildSourceData();
                oEntry.initFromDbRow(aaoRow);
                self.dCache[idBuildSrc] = oEntry;
        return oEntry;

    #
    # Other methods.
    #

    def openBuildCursor(self, oBuildSource, sOs, sCpuArch, tsNow):
        """
        Opens a cursor (SELECT) using the criteria found in the build source
        and the given OS.CPUARCH.

        Returns database cursor. May raise exception on bad input or logic error.

        Used by SchedulerBase.
        """

        oCursor = self._oDb.openCursor();

        #
        # Construct the extra conditionals.
        #
        sExtraConditions = '';

        # Types
        if oBuildSource.asTypes is not None  and  oBuildSource.asTypes:
            if len(oBuildSource.asTypes) == 1:
                sExtraConditions += oCursor.formatBindArgs('   AND BuildCategories.sType = %s', (oBuildSource.asTypes[0],));
            else:
                sExtraConditions += oCursor.formatBindArgs('   AND BuildCategories.sType IN (%s', (oBuildSource.asTypes[0],))
                for i in range(1, len(oBuildSource.asTypes) - 1):
                    sExtraConditions += oCursor.formatBindArgs(', %s', (oBuildSource.asTypes[i],));
                sExtraConditions += oCursor.formatBindArgs(', %s)\n', (oBuildSource.asTypes[-1],));

        # BuildSource OSes.ARCHes. (Paranoia: use a dictionary to avoid duplicate values.)
        if oBuildSource.asOsArches is not None  and  oBuildSource.asOsArches:
            sExtraConditions += oCursor.formatBindArgs('  AND BuildCategories.asOsArches && %s', (oBuildSource.asOsArches,));

        # TestBox OSes.ARCHes. (Paranoia: use a dictionary to avoid duplicate values.)
        dOsDotArches = {};
        dOsDotArches[sOs + '.' + sCpuArch] = 1;
        dOsDotArches[sOs + '.' + coreconsts.g_ksCpuArchAgnostic] = 1;
        dOsDotArches[coreconsts.g_ksOsAgnostic + '.' + sCpuArch] = 1;
        dOsDotArches[coreconsts.g_ksOsDotArchAgnostic] = 1;
        sExtraConditions += oCursor.formatBindArgs('   AND BuildCategories.asOsArches && %s', (list(dOsDotArches.keys()),));

        # Revision range.
        if oBuildSource.iFirstRevision is not None:
            sExtraConditions += oCursor.formatBindArgs('   AND Builds.iRevision >= %s\n', (oBuildSource.iFirstRevision,));
        if oBuildSource.iLastRevision is not None:
            sExtraConditions += oCursor.formatBindArgs('   AND Builds.iRevision <= %s\n', (oBuildSource.iLastRevision,));

        # Max age.
        if oBuildSource.cSecMaxAge is not None:
            sExtraConditions += oCursor.formatBindArgs('   AND Builds.tsCreated >= (%s - \'%s seconds\'::INTERVAL)\n',
                                                       (tsNow, oBuildSource.cSecMaxAge,));

        #
        # Execute the query.
        #
        oCursor.execute('SELECT Builds.*, BuildCategories.*,\n'
                        '       EXISTS( SELECT  tsExpire\n'
                        '               FROM    BuildBlacklist\n'
                        '               WHERE   BuildBlacklist.tsExpire = \'infinity\'::TIMESTAMP\n'
                        '                   AND BuildBlacklist.sProduct = %s\n'
                        '                   AND BuildBlacklist.sBranch  = %s\n'
                        '                   AND BuildBlacklist.iFirstRevision <= Builds.iRevision\n'
                        '                   AND BuildBlacklist.iLastRevision  >= Builds.iRevision ) AS fMaybeBlacklisted\n'
                        'FROM   Builds, BuildCategories\n'
                        'WHERE  Builds.idBuildCategory   = BuildCategories.idBuildCategory\n'
                        '   AND Builds.tsExpire          = \'infinity\'::TIMESTAMP\n'
                        '   AND Builds.tsEffective      <= %s\n'
                        '   AND Builds.fBinariesDeleted is FALSE\n'
                        '   AND BuildCategories.sProduct = %s\n'
                        '   AND BuildCategories.sBranch  = %s\n'
                        + sExtraConditions +
                        'ORDER BY Builds.idBuild DESC\n'
                        'LIMIT 256\n'
                        , ( oBuildSource.sProduct, oBuildSource.sBranch,
                            tsNow, oBuildSource.sProduct, oBuildSource.sBranch,));

        return oCursor;


    def getById(self, idBuildSrc):
        """Get Build Source data by idBuildSrc"""

        self._oDb.execute('SELECT   *\n'
                          'FROM     BuildSources\n'
                          'WHERE    tsExpire   = \'infinity\'::timestamp\n'
                          '  AND    idBuildSrc = %s;', (idBuildSrc,))
        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise self._oDb.integrityException(
                'Found more than one build sources with the same credentials. Database structure is corrupted.')
        try:
            return BuildSourceData().initFromDbRow(aRows[0])
        except IndexError:
            return None

    #
    # Internal helpers.
    #

    def _assertUnique(self, oData, idBuildSrcIgnore):
        """ Checks that the build source name is unique, raises exception if it isn't. """
        self._oDb.execute('SELECT idBuildSrc\n'
                          'FROM   BuildSources\n'
                          'WHERE  sName    = %s\n'
                          '   AND tsExpire = \'infinity\'::TIMESTAMP\n'
                          + ('' if idBuildSrcIgnore is None else  '   AND idBuildSrc <> %d\n' % (idBuildSrcIgnore,))
                          , ( oData.sName, ))
        if self._oDb.getRowCount() > 0:
            raise TMRowAlreadyExists('A build source with name "%s" already exist.' % (oData.sName,));
        return True;


    def _historizeBuildSource(self, idBuildSrc, tsExpire = None):
        """ Historizes the current build source entry. """
        if tsExpire is None:
            self._oDb.execute('UPDATE BuildSources\n'
                              'SET    tsExpire      = CURRENT_TIMESTAMP\n'
                              'WHERE  idBuildSrc    = %s\n'
                              '   AND tsExpire      = \'infinity\'::TIMESTAMP\n'
                              , ( idBuildSrc, ));
        else:
            self._oDb.execute('UPDATE BuildSources\n'
                              'SET    tsExpire      = %s\n'
                              'WHERE  idBuildSrc    = %s\n'
                              '   AND tsExpire      = \'infinity\'::TIMESTAMP\n'
                              , ( tsExpire, idBuildSrc, ));
        return True;





#
# Unit testing.
#

# pylint: disable=missing-docstring
class BuildSourceDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [BuildSourceData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

