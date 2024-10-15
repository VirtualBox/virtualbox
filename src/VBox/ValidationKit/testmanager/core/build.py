# -*- coding: utf-8 -*-
# $Id: build.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Builds.
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
import os;
import unittest;

# Validation Kit imports.
from testmanager                        import config;
from testmanager.core                   import coreconsts;
from testmanager.core.base              import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMExceptionBase, \
                                               TMTooManyRows, TMInvalidData, TMRowNotFound, TMRowInUse;


class BuildCategoryData(ModelDataBase):
    """
    A build category.
    """

    ksIdAttr = 'idBuildCategory';

    ksParam_idBuildCategory     = 'BuildCategory_idBuildCategory';
    ksParam_sProduct            = 'BuildCategory_sProduct';
    ksParam_sRepository         = 'BuildCategory_sRepository';
    ksParam_sBranch             = 'BuildCategory_sBranch';
    ksParam_sType               = 'BuildCategory_sType';
    ksParam_asOsArches          = 'BuildCategory_asOsArches';

    kasAllowNullAttributes      = ['idBuildCategory', ];

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idBuildCategory    = None;
        self.sProduct           = None;
        self.sRepository        = None;
        self.sBranch            = None;
        self.sType              = None;
        self.asOsArches         = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the object from a SELECT * FROM BuildCategories row.
        Returns self.  Raises exception if aoRow is None.
        """
        if aoRow is None:
            raise TMRowNotFound('BuildCategory not found.');

        self.idBuildCategory     = aoRow[0];
        self.sProduct            = aoRow[1];
        self.sRepository         = aoRow[2];
        self.sBranch             = aoRow[3];
        self.sType               = aoRow[4];
        self.asOsArches          = sorted(aoRow[5]);
        return self;

    def initFromDbWithId(self, oDb, idBuildCategory, tsNow = None, sPeriodBack = None):
        """
        Initialize from the database, given the ID of a row.
        """
        _ = tsNow; _ = sPeriodBack; # No history in this table.
        oDb.execute('SELECT * FROM BuildCategories WHERE idBuildCategory = %s', (idBuildCategory,));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idBuildCategory=%s not found' % (idBuildCategory, ));
        return self.initFromDbRow(aoRow);

    def initFromValues(self, sProduct, sRepository, sBranch, sType, asOsArches, idBuildCategory = None):
        """
        Reinitializes form a set of values.
        return self.
        """
        self.idBuildCategory    = idBuildCategory;
        self.sProduct           = sProduct;
        self.sRepository        = sRepository;
        self.sBranch            = sBranch;
        self.sType              = sType;
        self.asOsArches         = asOsArches;
        return self;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        # Handle sType and asOsArches specially.
        if sAttr == 'sType':
            (oNewValue, sError) = ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue,
                                                                             aoNilValues, fAllowNull, oDb);
            if sError is None  and  self.sType.lower() != self.sType:
                sError = 'Invalid build type value';

        elif sAttr == 'asOsArches':
            (oNewValue, sError) = self.validateListOfStr(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                         asValidValues = coreconsts.g_kasOsDotCpusAll);
            if sError is not None  and  oNewValue is not None:
                oNewValue = sorted(oNewValue); # Must be sorted!

        else:
            return ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);

        return (oNewValue, sError);

    def matchesOsArch(self, sOs, sArch):
        """ Checks if the build matches the given OS and architecture. """
        if sOs + '.' + sArch in self.asOsArches:
            return True;
        if sOs + '.noarch' in self.asOsArches:
            return True;
        if 'os-agnostic.' + sArch in self.asOsArches:
            return True;
        if 'os-agnostic.noarch' in self.asOsArches:
            return True;
        return False;


class BuildCategoryLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    Build categories database logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches testboxes for listing.

        Returns an array (list) of UserAccountData items, empty list if none.
        Raises exception on error.
        """
        _ = tsNow; _ = aiSortColumns;
        self._oDb.execute('SELECT   *\n'
                          'FROM     BuildCategories\n'
                          'ORDER BY sProduct, sRepository, sBranch, sType, idBuildCategory\n'
                          'LIMIT %s OFFSET %s\n'
                          , (cMaxRows, iStart,));

        aoRows = [];
        for _ in range(self._oDb.getRowCount()):
            aoRows.append(BuildCategoryData().initFromDbRow(self._oDb.fetchOne()));
        return aoRows;

    def fetchForCombo(self):
        """
        Gets the list of Build Categories for a combo box.
        Returns an array of (value [idBuildCategory], drop-down-name [info],
        hover-text [info]) tuples.
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     BuildCategories\n'
                          'ORDER BY sProduct, sBranch, sType, asOsArches')

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            oData = BuildCategoryData().initFromDbRow(aoRow)

            sInfo = '%s / %s / %s / %s' % \
                    (oData.sProduct,
                     oData.sBranch,
                     oData.sType,
                     ', '.join(oData.asOsArches))

            # Make short info string if necessary
            sInfo = sInfo if len(sInfo) < 70 else (sInfo[:70] + '...')

            oInfoItem = (oData.idBuildCategory, sInfo, sInfo)
            aoRet.append(oInfoItem)

        return aoRet

    def addEntry(self, oData, uidAuthor = None, fCommit = False):
        """
        Standard method for adding a build category.
        """

        # Lazy bird warning! Reuse the soft addBuildCategory method.
        self.addBuildCategory(oData, fCommit);
        _ = uidAuthor;
        return True;

    def removeEntry(self, uidAuthor, idBuildCategory, fCascade = False, fCommit = False):
        """
        Tries to delete the build category.
        Note! Does not implement cascading. This is intentional!
        """

        #
        # Check that the build category isn't used by anyone.
        #
        self._oDb.execute('SELECT   COUNT(idBuild)\n'
                          'FROM     Builds\n'
                          'WHERE    idBuildCategory = %s\n'
                          , (idBuildCategory,));
        cBuilds = self._oDb.fetchOne()[0];
        if cBuilds > 0:
            raise TMRowInUse('Build category #%d is used by %d builds and can therefore not be deleted.'
                             % (idBuildCategory, cBuilds,));

        #
        # Ok, it's not used, so just delete it.
        # (No history on this table. This code is for typos.)
        #
        self._oDb.execute('DELETE FROM Builds\n'
                          'WHERE    idBuildCategory = %s\n'
                          , (idBuildCategory,));

        self._oDb.maybeCommit(fCommit);
        _ = uidAuthor; _ = fCascade;
        return True;

    def cachedLookup(self, idBuildCategory):
        """
        Looks up the most recent BuildCategoryData object for idBuildCategory
        via an object cache.

        Returns a shared BuildCategoryData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('BuildCategoryData');
        oEntry = self.dCache.get(idBuildCategory, None);
        if oEntry is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     BuildCategories\n'
                              'WHERE    idBuildCategory = %s\n'
                              , (idBuildCategory, ));
            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                oEntry = BuildCategoryData();
                oEntry.initFromDbRow(aaoRow);
                self.dCache[idBuildCategory] = oEntry;
        return oEntry;

    #
    # Other methods.
    #

    def tryFetch(self, idBuildCategory):
        """
        Try fetch the build category with the given ID.
        Returns BuildCategoryData instance if found, None if not found.
        May raise exception on database error.
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     BuildCategories\n'
                          'WHERE    idBuildCategory = %s\n'
                          , (idBuildCategory,))
        aaoRows = self._oDb.fetchAll()
        if not aaoRows:
            return None;
        if len(aaoRows) != 1:
            raise self._oDb.integrityException('Duplicates in BuildCategories: %s' % (aaoRows,));
        return BuildCategoryData().initFromDbRow(aaoRows[0])

    def tryFindByData(self, oData):
        """
        Tries to find the matching build category from the sProduct, sBranch,
        sType and asOsArches members of oData.

        Returns a valid build category ID and an updated oData object if found.
        Returns None and unmodified oData object if not found.
        May raise exception on database error.
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     BuildCategories\n'
                          'WHERE    sProduct    = %s\n'
                          '  AND    sRepository = %s\n'
                          '  AND    sBranch     = %s\n'
                          '  AND    sType       = %s\n'
                          '  AND    asOsArches  = %s\n'
                          , ( oData.sProduct,
                              oData.sRepository,
                              oData.sBranch,
                              oData.sType,
                              sorted(oData.asOsArches),
                          ));
        aaoRows = self._oDb.fetchAll();
        if not aaoRows:
            return None;
        if len(aaoRows) > 1:
            raise self._oDb.integrityException('Duplicates in BuildCategories: %s' % (aaoRows,));

        oData.initFromDbRow(aaoRows[0]);
        return oData.idBuildCategory;

    def addBuildCategory(self, oData, fCommit = False):
        """
        Add Build Category record into the database if needed, returning updated oData.
        Raises exception on input and database errors.
        """

        # Check BuildCategoryData before do anything
        dDataErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dDataErrors:
            raise TMInvalidData('Invalid data passed to addBuildCategory(): %s' % (dDataErrors,));

        # Does it already exist?
        if self.tryFindByData(oData) is None:
            # No, We'll have to add it.
            self._oDb.execute('INSERT INTO BuildCategories (sProduct, sRepository, sBranch, sType, asOsArches)\n'
                              'VALUES (%s, %s, %s, %s, %s)\n'
                              'RETURNING idBuildCategory'
                              , ( oData.sProduct,
                                  oData.sRepository,
                                  oData.sBranch,
                                  oData.sType,
                                  sorted(oData.asOsArches),
                              ));
            oData.idBuildCategory = self._oDb.fetchOne()[0];

        self._oDb.maybeCommit(fCommit);
        return oData;


class BuildData(ModelDataBase):
    """
    A build.
    """

    ksIdAttr = 'idBuild';

    ksParam_idBuild             = 'Build_idBuild';
    ksParam_tsCreated           = 'Build_tsCreated';
    ksParam_tsEffective         = 'Build_tsEffective';
    ksParam_tsExpire            = 'Build_tsExpire';
    ksParam_uidAuthor           = 'Build_uidAuthor';
    ksParam_idBuildCategory     = 'Build_idBuildCategory';
    ksParam_iRevision           = 'Build_iRevision';
    ksParam_sVersion            = 'Build_sVersion';
    ksParam_sLogUrl             = 'Build_sLogUrl';
    ksParam_sBinaries           = 'Build_sBinaries';
    ksParam_fBinariesDeleted    = 'Build_fBinariesDeleted';

    kasAllowNullAttributes      = ['idBuild', 'tsCreated', 'tsEffective', 'tsExpire', 'uidAuthor', 'tsCreated', 'sLogUrl'];


    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idBuild            = None;
        self.tsCreated          = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.idBuildCategory    = None;
        self.iRevision          = None;
        self.sVersion           = None;
        self.sLogUrl            = None;
        self.sBinaries          = None;
        self.fBinariesDeleted   = False;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the object from a SELECT * FROM Builds row.
        Returns self.  Raises exception if aoRow is None.
        """
        if aoRow is None:
            raise TMRowNotFound('Build not found.');

        self.idBuild            = aoRow[0];
        self.tsCreated          = aoRow[1];
        self.tsEffective        = aoRow[2];
        self.tsExpire           = aoRow[3];
        self.uidAuthor          = aoRow[4];
        self.idBuildCategory    = aoRow[5];
        self.iRevision          = aoRow[6];
        self.sVersion           = aoRow[7];
        self.sLogUrl            = aoRow[8];
        self.sBinaries          = aoRow[9];
        self.fBinariesDeleted   = aoRow[10];
        return self;

    def initFromDbWithId(self, oDb, idBuild, tsNow = None, sPeriodBack = None):
        """
        Initialize from the database, given the ID of a row.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   Builds\n'
                                                       'WHERE  idBuild = %s\n'
                                                       , ( idBuild,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idBuild=%s not found (tsNow=%s sPeriodBack=%s)' % (idBuild, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def areFilesStillThere(self):
        """
        Try check if the build files are still there.

        Returns True if they are, None if we cannot tell, and False if one or
        more are missing.
        """
        if self.fBinariesDeleted:
            return False;

        for sBinary in self.sBinaries.split(','):
            sBinary = sBinary.strip();
            if not sBinary:
                continue;
            # Same URL tests as in webutils.downloadFile().
            if   sBinary.startswith('http://') \
              or sBinary.startswith('https://') \
              or sBinary.startswith('ftp://'):
                # URL - don't bother trying to verify that (we don't use it atm).
                fRc = None;
            else:
                # File.
                if config.g_ksBuildBinRootDir is not None:
                    sFullPath = os.path.join(config.g_ksBuildBinRootDir, sBinary);
                    fRc = os.path.isfile(sFullPath);
                    if    not fRc \
                      and not os.path.isfile(os.path.join(config.g_ksBuildBinRootDir, config.g_ksBuildBinRootFile)):
                        fRc = None; # Root file missing, so the share might not be mounted correctly.
                else:
                    fRc = None;
            if fRc is not True:
                return fRc;

        return True;


class BuildDataEx(BuildData):
    """
    Complete data set.
    """

    kasInternalAttributes = [ 'oCat', ];

    def __init__(self):
        BuildData.__init__(self);
        self.oCat = None;

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT Builds.*, BuildCategories.* FROM Builds, BuildCategories query.
        Returns self.  Raises exception if aoRow is None.
        """
        if aoRow is None:
            raise TMRowNotFound('Build not found.');
        BuildData.initFromDbRow(self, aoRow);
        self.oCat = BuildCategoryData().initFromDbRow(aoRow[11:]);
        return self;

    def initFromDbWithId(self, oDb, idBuild, tsNow = None, sPeriodBack = None):
        """
        Reinitialize from database given a row ID.
        Returns self.  Raises exception on database error or if the ID is invalid.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT Builds.*, BuildCategories.*\n'
                                                       'FROM   Builds, BuildCategories\n'
                                                       'WHERE  idBuild = %s\n'
                                                       '   AND Builds.idBuildCategory = BuildCategories.idBuildCategory\n'
                                                       , ( idBuild,), tsNow, sPeriodBack, 'Builds.'));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idBuild=%s not found (tsNow=%s sPeriodBack=%s)' % (idBuild, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def convertFromParamNull(self):
        raise TMExceptionBase('Not implemented');

    def isEqual(self, oOther):
        raise TMExceptionBase('Not implemented');



class BuildLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    Build database logic (covers build categories as well as builds).
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    #
    # Standard methods.
    #

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches builds for listing.

        Returns an array (list) of BuildDataEx items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     Builds, BuildCategories\n'
                              'WHERE    Builds.idBuildCategory = BuildCategories.idBuildCategory\n'
                              '     AND Builds.tsExpire        = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY tsCreated DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     Builds, BuildCategories\n'
                              'WHERE    Builds.idBuildCategory = BuildCategories.idBuildCategory\n'
                              '     AND Builds.tsExpire        > %s\n'
                              '     AND Builds.tsEffective    <= %s\n'
                              'ORDER BY tsCreated DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,));

        aoRows = [];
        for _ in range(self._oDb.getRowCount()):
            aoRows.append(BuildDataEx().initFromDbRow(self._oDb.fetchOne()));
        return aoRows;

    def addEntry(self, oBuildData, uidAuthor = None, fCommit = False):
        """
        Adds the build to the database, optionally adding the build category if
        a BuildDataEx object used and it's necessary.

        Returns updated data object. Raises exception on failure.
        """

        # Find/Add the build category if specified.
        if    isinstance(oBuildData, BuildDataEx) \
          and oBuildData.idBuildCategory is None:
            BuildCategoryLogic(self._oDb).addBuildCategory(oBuildData.oCat, fCommit = False);
            oBuildData.idBuildCategory = oBuildData.oCat.idBuildCategory;

        # Add the build.
        self._oDb.execute('INSERT INTO Builds (uidAuthor,\n'
                          '                    idBuildCategory,\n'
                          '                    iRevision,\n'
                          '                    sVersion,\n'
                          '                    sLogUrl,\n'
                          '                    sBinaries,\n'
                          '                    fBinariesDeleted)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s, %s)\n'
                          'RETURNING idBuild, tsCreated\n'
                          , ( uidAuthor,
                              oBuildData.idBuildCategory,
                              oBuildData.iRevision,
                              oBuildData.sVersion,
                              oBuildData.sLogUrl,
                              oBuildData.sBinaries,
                              oBuildData.fBinariesDeleted,
                          ));
        aoRow = self._oDb.fetchOne();
        oBuildData.idBuild   = aoRow[0];
        oBuildData.tsCreated = aoRow[1];

        self._oDb.maybeCommit(fCommit);
        return oBuildData;

    def editEntry(self, oData, uidAuthor = None, fCommit = False):
        """Modify database record"""

        #
        # Validate input and get current data.
        #
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('editEntry invalid input: %s' % (dErrors,));
        oOldData = BuildData().initFromDbWithId(self._oDb, oData.idBuild);

        #
        # Do the work.
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor' ]):
            self._historizeBuild(oData.idBuild);
            self._oDb.execute('INSERT INTO Builds (uidAuthor,\n'
                              '                    idBuild,\n'
                              '                    tsCreated,\n'
                              '                    idBuildCategory,\n'
                              '                    iRevision,\n'
                              '                    sVersion,\n'
                              '                    sLogUrl,\n'
                              '                    sBinaries,\n'
                              '                    fBinariesDeleted)\n'
                              'VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)\n'
                              'RETURNING idBuild, tsCreated\n'
                              , ( uidAuthor,
                                  oData.idBuild,
                                  oData.tsCreated,
                                  oData.idBuildCategory,
                                  oData.iRevision,
                                  oData.sVersion,
                                  oData.sLogUrl,
                                  oData.sBinaries,
                                  oData.fBinariesDeleted,
                              ));

        self._oDb.maybeCommit(fCommit);
        return True;

    def removeEntry(self, uidAuthor, idBuild, fCascade = False, fCommit = False):
        """
        Historize record
        """

        #
        # No non-historic refs here, so just go ahead and expire the build.
        #
        _ = fCascade;
        _ = uidAuthor; ## @todo record deleter.

        self._historizeBuild(idBuild, None);

        self._oDb.maybeCommit(fCommit);
        return True;

    def cachedLookup(self, idBuild):
        """
        Looks up the most recent BuildDataEx object for idBuild
        via an object cache.

        Returns a shared BuildDataEx object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('BuildDataEx');
        oEntry = self.dCache.get(idBuild, None);
        if oEntry is None:
            self._oDb.execute('SELECT   Builds.*, BuildCategories.*\n'
                              'FROM     Builds, BuildCategories\n'
                              'WHERE    Builds.idBuild         = %s\n'
                              '     AND Builds.idBuildCategory = BuildCategories.idBuildCategory\n'
                              '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , (idBuild, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   Builds.*, BuildCategories.*\n'
                                  'FROM     Builds, BuildCategories\n'
                                  'WHERE    Builds.idBuild         = %s\n'
                                  '     AND Builds.idBuildCategory = BuildCategories.idBuildCategory\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idBuild, ));
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idBuild));

            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                oEntry = BuildDataEx();
                oEntry.initFromDbRow(aaoRow);
                self.dCache[idBuild] = oEntry;
        return oEntry;


    #
    # Other methods.
    #

    def tryFindSameBuildForOsArch(self, oBuildEx, sOs, sCpuArch):
        """
        Attempts to find a matching build for the given OS.ARCH.  May return
        the input build if if matches.

        Returns BuildDataEx instance if found, None if none.  May raise
        exception on database error.
        """

        if oBuildEx.oCat.matchesOsArch(sOs, sCpuArch):
            return oBuildEx;

        self._oDb.execute('SELECT Builds.*, BuildCategories.*\n'
                          'FROM   Builds, BuildCategories\n'
                          'WHERE  BuildCategories.sProduct = %s\n'
                          '   AND BuildCategories.sBranch  = %s\n'
                          '   AND BuildCategories.sType    = %s\n'
                          '   AND (   %s = ANY(BuildCategories.asOsArches)\n'
                          '        OR %s = ANY(BuildCategories.asOsArches)\n'
                          '        OR %s = ANY(BuildCategories.asOsArches))\n'
                          '   AND Builds.idBuildCategory   = BuildCategories.idBuildCategory\n'
                          '   AND Builds.tsExpire          = \'infinity\'::TIMESTAMP\n'
                          '   AND Builds.iRevision         = %s\n'
                          '   AND Builds.sRelease          = %s\n'
                          '   AND Builds.fBinariesDeleted IS FALSE\n'
                          'ORDER BY tsCreated DESC\n'
                          'LIMIT 4096\n'        # stay sane.
                          , (oBuildEx.oCat.sProduct,
                             oBuildEx.oCat.sBranch,
                             oBuildEx.oCat.sType,
                             '%s.%s' % (sOs, sCpuArch),
                             '%s.noarch' % (sOs,),
                             'os-agnostic.%s' % (sCpuArch,),
                             'os-agnostic.noarch',
                             oBuildEx.iRevision,
                             oBuildEx.sRelease,
                          ) );
        aaoRows = self._oDb.fetchAll();

        for aoRow in aaoRows:
            oBuildExRet = BuildDataEx().initFromDbRow(aoRow);
            if not self.isBuildBlacklisted(oBuildExRet):
                return oBuildExRet;

        return None;

    def isBuildBlacklisted(self, oBuildEx):
        """
        Checks if the given build is blacklisted
        Returns True/False. May raise exception on database error.
        """

        asOsAgnosticArch = [];
        asOsNoArch       = [];
        for sOsArch in oBuildEx.oCat.asOsArches:
            asParts = sOsArch.split('.');
            if len(asParts) != 2 or not asParts[0] or not asParts[1]:
                raise self._oDb.integrityException('Bad build asOsArches value: %s (idBuild=%s idBuildCategory=%s)'
                                                   % (sOsArch, oBuildEx.idBuild, oBuildEx.idBuildCategory));
            asOsNoArch.append(asParts[0] + '.noarch');
            asOsNoArch.append('os-agnostic.' + asParts[1]);

        self._oDb.execute('SELECT COUNT(*)\n'
                          'FROM   BuildBlacklist\n'
                          'WHERE  BuildBlacklist.tsExpire     > CURRENT_TIMESTAMP\n'
                          '   AND BuildBlacklist.tsEffective <= CURRENT_TIMESTAMP\n'
                          '   AND BuildBlacklist.sProduct     = %s\n'
                          '   AND BuildBlacklist.sBranch      = %s\n'
                          '   AND (   BuildBlacklist.asTypes is NULL\n'
                          '        OR %s = ANY(BuildBlacklist.asTypes))\n'
                          '   AND (   BuildBlacklist.asOsArches is NULL\n'
                          '        OR %s && BuildBlacklist.asOsArches\n'  ## @todo check array rep! Need overload?
                          '        OR %s && BuildBlacklist.asOsArches\n'
                          '        OR %s && BuildBlacklist.asOsArches\n'
                          '        OR %s = ANY(BuildBlacklist.asOsArches))\n'
                          '   AND BuildBlacklist.iFirstRevision <= %s\n'
                          '   AND BuildBlacklist.iLastRevision  >= %s\n'
                          , (oBuildEx.oCat.sProduct,
                             oBuildEx.oCat.sBranch,
                             oBuildEx.oCat.sType,
                             oBuildEx.oCat.asOsArches,
                             asOsAgnosticArch,
                             asOsNoArch,
                             'os-agnostic.noarch',
                             oBuildEx.iRevision,
                             oBuildEx.iRevision,
                          ) );
        return self._oDb.fetchOne()[0] > 0;


    def getById(self, idBuild):
        """
        Get build record by its id
        """
        self._oDb.execute('SELECT Builds.*, BuildCategories.*\n'
                          'FROM     Builds, BuildCategories\n'
                          'WHERE    Builds.idBuild=%s\n'
                          '     AND Builds.idBuildCategory=BuildCategories.idBuildCategory\n'
                          '     AND Builds.tsExpire = \'infinity\'::TIMESTAMP\n', (idBuild,))

        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise TMTooManyRows('Found more than one build with the same credentials. Database structure is corrupted.')
        try:
            return BuildDataEx().initFromDbRow(aRows[0])
        except IndexError:
            return None


    def getAll(self, tsEffective = None):
        """
        Gets the list of all builds.
        Returns an array of BuildDataEx instances.
        """
        if tsEffective is None:
            self._oDb.execute('SELECT   Builds.*, BuildCategories.*\n'
                              'FROM     Builds, BuildCategories\n'
                              'WHERE    Builds.tsExpire = \'infinity\'::TIMESTAMP\n'
                              '     AND Builds.idBuildCategory=BuildCategories.idBuildCategory')
        else:
            self._oDb.execute('SELECT   Builds.*, BuildCategories.*\n'
                              'FROM     Builds, BuildCategories\n'
                              'WHERE    Builds.tsExpire > %s\n'
                              '     AND Builds.tsEffective <= %s'
                              '     AND Builds.idBuildCategory=BuildCategories.idBuildCategory'
                              , (tsEffective, tsEffective))
        aoRet = []
        for aoRow in self._oDb.fetchAll():
            aoRet.append(BuildDataEx().initFromDbRow(aoRow))
        return aoRet


    def markDeletedByBinaries(self, sBinaries, fCommit = False):
        """
        Marks zero or more builds deleted given the build binaries.

        Returns the number of affected builds.
        """
        # Fetch a list of affected build IDs (generally 1 build), and used the
        # editEntry method to do the rest.  This isn't 100% optimal, but it's
        # short and simple, the main effort is anyway the first query.
        self._oDb.execute('SELECT   idBuild\n'
                          'FROM     Builds\n'
                          'WHERE    sBinaries        = %s\n'
                          '     AND fBinariesDeleted = FALSE\n'
                          '     AND tsExpire         = \'infinity\'::TIMESTAMP\n'
                          , (sBinaries,));
        aaoRows = self._oDb.fetchAll();
        for aoRow in aaoRows:
            oData = BuildData().initFromDbWithId(self._oDb, aoRow[0]);
            assert not oData.fBinariesDeleted;
            oData.fBinariesDeleted = True;
            self.editEntry(oData, fCommit = False);
        self._oDb.maybeCommit(fCommit);
        return len(aaoRows);



    #
    # Internal helpers.
    #

    def _historizeBuild(self, idBuild, tsExpire = None):
        """ Historizes the current entry for the specified build. """
        if tsExpire is None:
            self._oDb.execute('UPDATE Builds\n'
                              'SET    tsExpire = CURRENT_TIMESTAMP\n'
                              'WHERE  idBuild  = %s\n'
                              '   AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , (idBuild,));
        else:
            self._oDb.execute('UPDATE Builds\n'
                              'SET    tsExpire = %s\n'
                              'WHERE  idBuild  = %s\n'
                              '   AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , (tsExpire, idBuild,));
        return True;

#
# Unit testing.
#

# pylint: disable=missing-docstring
class BuildCategoryDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [BuildCategoryData(),];

class BuildDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [BuildData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

