-- $Id: tmdb-r13-buildcategories-1-vcsrevisions-1.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds an sRepository to Builds and creates a new VcsRepositories table.
--

--
-- Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

--
-- Cleanup after failed runs.
--
DROP TABLE NewBuildCategories;
DROP TABLE OldBuildCategories;

--
-- Drop foreign keys on this table.
--
ALTER TABLE Builds DROP CONSTRAINT NewBuilds_idBuildCategory_fkey;
ALTER TABLE Builds DROP CONSTRAINT Builds_idBuildCategory_fkey;
ALTER TABLE TestSets DROP CONSTRAINT TestSets_idBuildCategory_fkey;

-- Die on error from now on.
\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0

\d+ BuildCategories;

--
-- Create the new version of the table and filling with the content of the old.
--
CREATE TABLE NewBuildCategories (
    --- The build type identifier.
    idBuildCategory     INTEGER     PRIMARY KEY DEFAULT NEXTVAL('BuildCategoryIdSeq')  NOT NULL,
    --- Product.
    -- The product name.  For instance 'VBox' or 'VBoxTestSuite'.
    sProduct            TEXT        NOT NULL,
    --- The version control repository name.
    sRepository         TEXT        NOT NULL,
    --- The branch name (in the version control system).
    sBranch             TEXT        NOT NULL,
    --- The build type.
    -- See KBUILD_BLD_TYPES in kBuild for a list of standard build types.
    sType               TEXT        NOT NULL,
    --- Array of the 'sOs.sCpuArch' supported by the build.
    -- See KBUILD_OSES in kBuild for a list of standard target OSes, and
    -- KBUILD_ARCHES for a list of standard architectures.
    --
    -- @remarks 'os-agnostic' is used if the build doesn't really target any
    --          specific OS or if it targets all applicable OSes.
    --          'noarch' is used if the build is architecture independent or if
    --          all applicable architectures are handled.
    --          Thus, 'os-agnostic.noarch' will run on all build boxes.
    --
    -- @note    The array shall be sorted ascendingly to prevent unnecessary duplicates!
    --
    asOsArches          TEXT ARRAY  NOT NULL,

    UNIQUE (sProduct, sRepository, sBranch, sType, asOsArches)
);
COMMIT;
\d+ NewBuildCategories

INSERT INTO NewBuildCategories (idBuildCategory, sProduct, sRepository, sBranch, sType, asOsArches)
    SELECT idBuildCategory, sProduct, 'vbox', sBranch, sType, asOsArches
    FROM   BuildCategories
COMMIT;

-- Switch the tables.
ALTER TABLE BuildCategories RENAME TO OldBuildCategories;
ALTER TABLE NewBuildCategories RENAME TO BuildCategories;
COMMIT;

-- Drop the old table.
DROP TABLE OldBuildCategories;
COMMIT;

-- Restore foreign keys.
LOCK TABLE Builds, TestSets;
ALTER TABLE Builds   ADD FOREIGN KEY (idBuildCategory)      REFERENCES BuildCategories(idBuildCategory);
ALTER TABLE TestSets ADD FOREIGN KEY (idBuildCategory)      REFERENCES BuildCategories(idBuildCategory);
COMMIT;

\d+ BuildCategories;


--
-- Create the new VcsRevisions table.
--
CREATE TABLE VcsRevisions (
    --- The version control tree name.
    sRepository         TEXT        NOT NULL,
    --- The version control tree revision number.
    iRevision           INTEGER     NOT NULL,
    --- When the revision was created (committed).
    tsCreated           TIMESTAMP WITH TIME ZONE  NOT NULL,
    --- The name of the committer.
    -- @note Not to be confused with uidAuthor and test manager users.
    sAuthor             TEXT,
    --- The commit message.
    sMessage            TEXT,

    UNIQUE (sRepository, iRevision)
);
COMMIT;
\d+ VcsRevisions;

