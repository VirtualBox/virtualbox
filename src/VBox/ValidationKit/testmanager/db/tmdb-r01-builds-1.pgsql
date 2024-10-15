-- $Id: tmdb-r01-builds-1.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Changed Builds to be historized.
--

--
-- Copyright (C) 2012-2024 Oracle and/or its affiliates.
--
-- This file is part of VirtualBox base platform packages, as
-- available from https://www.virtualbox.org.
--
-- This program is free software; you can redistribute it and/or
-- modify it under the terms of the GNU General Public License
-- as published by the Free Software Foundation, in version 3 of the
-- License.
--
-- This program is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
-- General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program; if not, see <https://www.gnu.org/licenses>.
--
-- The contents of this file may alternatively be used under the terms
-- of the Common Development and Distribution License Version 1.0
-- (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
-- in the VirtualBox distribution, in which case the provisions of the
-- CDDL are applicable instead of those of the GPL.
--
-- You may elect to license modified versions of this file under the
-- terms and conditions of either the GPL or the CDDL or both.
--
-- SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
--


DROP TABLE OldBuilds;
DROP TABLE NewBuilds;
DROP INDEX BuildsLookupIdx;

\set ON_ERROR_STOP 1

--
-- idBuild won't be unique, so it cannot be used directly as a foreign key
-- by TestSets.
--
ALTER TABLE TestSets
    DROP CONSTRAINT TestSets_idBuild_fkey;
ALTER TABLE TestSets
    DROP CONSTRAINT TestSets_idBuildTestSuite_fkey;


--
-- Create the table, filling it with the current Builds content.
--
CREATE TABLE NewBuilds (
    idBuild             INTEGER     DEFAULT NEXTVAL('BuildIdSeq')  NOT NULL,
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    uidAuthor           INTEGER     DEFAULT NULL,
    idBuildCategory     INTEGER     REFERENCES BuildCategories(idBuildCategory)  NOT NULL,
    iRevision           INTEGER     NOT NULL,
    sVersion            TEXT        NOT NULL,
    sLogUrl             TEXT,
    sBinaries           TEXT        NOT NULL,
    fBinariesDeleted    BOOLEAN     DEFAULT FALSE  NOT NULL,
    UNIQUE (idBuild, tsExpire)
);

INSERT INTO NewBuilds (idBuild, tsCreated, tsEffective, uidAuthor, idBuildCategory, iRevision, sVersion, sLogUrl, sBinaries)
    SELECT idBuild, tsCreated, tsCreated, uidAuthor, idBuildCategory, iRevision, sVersion, sLogUrl, sBinaries
    FROM Builds;
COMMIT;

-- Switch the tables.
ALTER TABLE Builds    RENAME TO OldBuilds;
ALTER TABLE NewBuilds RENAME TO Builds;
COMMIT;

-- Finally index the table.
CREATE INDEX BuildsLookupIdx ON Builds (idBuildCategory, iRevision);
COMMIT;

DROP TABLE OldBuilds;
COMMIT;

-- Fix implicit index name.
ALTER INDEX newbuilds_idbuild_tsexpire_key RENAME TO builds_idbuild_tsexpire_key;

