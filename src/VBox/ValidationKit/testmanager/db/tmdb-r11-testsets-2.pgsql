-- $Id: tmdb-r11-testsets-2.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds an idBuildCategories to TestSets.
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
-- Drop all indexes (might already be dropped).
--
DROP INDEX TestSetsGangIdx;
DROP INDEX TestSetsBoxIdx;
DROP INDEX TestSetsBuildIdx;
DROP INDEX TestSetsTestCaseIdx;
DROP INDEX TestSetsTestVarIdx;
DROP INDEX TestSetsCreated;
DROP INDEX TestSetsDone;

--
-- Drop foreign keys on this table.
--
ALTER TABLE SchedQueues DROP CONSTRAINT SchedQueues_idTestSetGangLeader_fkey;
ALTER TABLE TestBoxStatuses DROP CONSTRAINT TestBoxStatuses_idTestSet_fkey;
ALTER TABLE TestResults DROP CONSTRAINT idTestSetFk; -- old name
ALTER TABLE TestResults DROP CONSTRAINT TestResults_idTestSet_fkey;
ALTER TABLE TestResultValues DROP CONSTRAINT TestResultValues_idTestSet_fkey;

--
-- Cleanup after failed runs.
--
DROP TABLE NewTestSets;
DROP TABLE OldTestSets;

-- Die on error from now on.
\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0

\d+ TestSets;

--
-- Create the new version of the table and filling with the content of the old.
--
CREATE TABLE NewTestSets (
    --- The ID of this test set.
    idTestSet           INTEGER      DEFAULT NEXTVAL('TestSetIdSeq')  NOT NULL, -- PRIMARY KEY

    --- The test config timestamp, used when reading test config.
    tsConfig            TIMESTAMP WITH TIME ZONE  DEFAULT CURRENT_TIMESTAMP  NOT NULL,
    --- When this test set was scheduled.
    -- idGenTestBox is valid at this point.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT CURRENT_TIMESTAMP  NOT NULL,
    --- When this test completed, i.e. testing stopped.  This should only be set once.
    tsDone              TIMESTAMP WITH TIME ZONE  DEFAULT NULL,
    --- The current status.
    enmStatus           TestStatus_T  DEFAULT 'running'::TestStatus_T  NOT NULL,

    --- The build we're testing.
    -- Non-unique foreign key: Builds(idBuild)
    idBuild             INTEGER     NOT NULL,
    --- The build category of idBuild when the test started.
    -- This is for speeding up graph data collection, i.e. avoid idBuild
    -- the WHERE part of the selection.
    idBuildCategory     INTEGER     , -- NOT NULL REFERENCES BuildCategories(idBuildCategory)
    --- The test suite build we're using to do the testing.
    -- This is NULL if the test suite zip wasn't referred or if a test suite
    -- build source wasn't configured.
    -- Non-unique foreign key: Builds(idBuild)
    idBuildTestSuite    INTEGER     DEFAULT NULL,

    --- The exact testbox configuration.
    idGenTestBox        INTEGER     NOT NULL, -- REFERENCES TestBoxes(idGenTestBox)
    --- The testbox ID for joining with (valid: tsStarted).
    -- Non-unique foreign key: TestBoxes(idTestBox)
    idTestBox           INTEGER     NOT NULL,

    --- The testgroup (valid: tsConfig).
    -- Non-unique foreign key: TestBoxes(idTestGroup)
    -- Note! This also gives the member ship entry, since a testcase can only
    --       have one membership per test group.
    idTestGroup         INTEGER     NOT NULL,

    --- The exact test case config we executed in this test run.
    idGenTestCase       INTEGER     NOT NULL, -- REFERENCES TestCases(idGenTestCase)
    --- The test case ID for joining with (valid: tsConfig).
    -- Non-unique foreign key: TestBoxes(idTestCase)
    idTestCase          INTEGER     NOT NULL,

    --- The arguments (and requirements++) we executed this test case with.
    idGenTestCaseArgs   INTEGER     NOT NULL, -- REFERENCES TestCaseArgs(idGenTestCaseArgs)
    --- The argument variation ID (valid: tsConfig).
    -- Non-unique foreign key: TestCaseArgs(idTestCaseArgs)
    idTestCaseArgs      INTEGER     NOT NULL,

    --- The root of the test result tree.
    -- @note This will only be NULL early in the transaction setting up the testset.
    -- @note If the test reports more than one top level test result, we'll
    --       fail the whole test run and let the test developer fix it.
    idTestResult        INTEGER     DEFAULT NULL, -- REFERENCES TestResults(idTestResult)

    --- The base filename used for storing files related to this test set.
    -- This is a path relative to wherever TM is dumping log files.  In order
    -- to not become a file system test case, we will try not to put too many
    -- hundred thousand files in a directory.  A simple first approach would
    -- be to just use the current date (tsCreated) like this:
    --    TM_FILE_DIR/year/month/day/TestSets.idTestSet
    --
    -- The primary log file for the test is this name suffixed by '.log'.
    --
    -- The files in the testresultfile table gets their full names like this:
    --    TM_FILE_DIR/sBaseFilename-testresultfile.id-TestResultStrTab(testresultfile.idStrFilename)
    --
    -- @remarks We store this explicitly in case we change the directly layout
    --          at some later point.
    sBaseFilename       text        UNIQUE  NOT NULL,

    --- The gang member number number, 0 is the leader.
    iGangMemberNo       SMALLINT    DEFAULT 0  NOT NULL, --CHECK (iGangMemberNo >= 0 AND iGangMemberNo < 1024),
    --- The test set of the gang leader, NULL if no gang involved.
    -- @note This is set by the gang leader as well, so that we can find all
    --       gang members by WHERE idTestSetGangLeader = :id.
    idTestSetGangLeader INTEGER     DEFAULT NULL -- REFERENCES TestSets(idTestSet)

);
COMMIT;
\d+ NewTestSets

-- Note! Using left out join here to speed up things (no hashing).
SELECT COUNT(*) FROM TestSets a LEFT OUTER JOIN Builds b ON (a.idBuild = b.idBuild AND b.tsExpire = 'infinity'::TIMESTAMP);
SELECT COUNT(*) FROM TestSets;

INSERT INTO NewTestSets (idTestSet, tsConfig, tsCreated, tsDone, enmStatus, idBuild, idBuildCategory, idBuildTestSuite,
                         idGenTestBox, idTestBox, idTestGroup, idGenTestCase, idTestCase, idGenTestCaseArgs, idTestCaseArgs,
                         idTestResult, sBaseFilename, iGangMemberNo, idTestSetGangLeader )
    SELECT a.idTestSet, a.tsConfig, a.tsCreated, tsDone, a.enmStatus, a.idBuild, b.idBuildCategory, a.idBuildTestSuite,
           a.idGenTestBox, a.idTestBox, a.idTestGroup, a.idGenTestCase, a.idTestCase, a.idGenTestCaseArgs, a.idTestCaseArgs,
           a.idTestResult, a.sBaseFilename, a.iGangMemberNo, a.idTestSetGangLeader
    FROM   TestSets a LEFT OUTER JOIN Builds b ON (a.idBuild = b.idBuild AND b.tsExpire = 'infinity'::TIMESTAMP);
COMMIT;
SELECT COUNT(*) FROM NewTestSets;

-- Note! 2-3 builds are missing from the Builds table, so fudge it.
UPDATE NewTestSets
   SET idBuildCategory = 1
 WHERE idBuildCategory IS NULL;

-- Switch the tables.
ALTER TABLE TestSets RENAME TO OldTestSets;
ALTER TABLE NewTestSets RENAME TO TestSets;
COMMIT;

-- Index the table.
CREATE INDEX TestSetsGangIdx        ON TestSets (idTestSetGangLeader);
CREATE INDEX TestSetsBoxIdx         ON TestSets (idTestBox, idTestResult);
CREATE INDEX TestSetsBuildIdx       ON TestSets (idBuild, idTestResult);
CREATE INDEX TestSetsTestCaseIdx    ON TestSets (idTestCase, idTestResult);
CREATE INDEX TestSetsTestVarIdx     ON TestSets (idTestCaseArgs, idTestResult);
CREATE INDEX TestSetsCreated        ON TestSets (tsCreated);
CREATE INDEX TestSetsDone           ON TestSets (tsDone);
COMMIT;

-- Drop the old table.
DROP TABLE OldTestSets;
COMMIT;

-- Add the constraints constraint.
ALTER TABLE TestSets ADD CONSTRAINT TestSets_iGangMemberNo_Check CHECK (iGangMemberNo >= 0 AND iGangMemberNo < 1024);
ALTER TABLE TestSets ADD PRIMARY KEY (idTestSet);
ALTER TABLE TestSets ADD FOREIGN KEY (idBuildCategory)      REFERENCES BuildCategories(idBuildCategory);
ALTER TABLE TestSets ADD FOREIGN KEY (idGenTestBox)         REFERENCES TestBoxes(idGenTestBox);
ALTER TABLE TestSets ADD FOREIGN KEY (idGenTestCase)        REFERENCES TestCases(idGenTestCase);
ALTER TABLE TestSets ADD FOREIGN KEY (idGenTestCaseArgs)    REFERENCES TestCaseArgs(idGenTestCaseArgs);
ALTER TABLE TestSets ADD FOREIGN KEY (idTestResult)         REFERENCES TestResults(idTestResult);
ALTER TABLE TestSets ADD FOREIGN KEY (idTestSetGangLeader)  REFERENCES TestSets(idTestSet);
COMMIT;

-- Restore foreign keys.
LOCK TABLE SchedQueues, TestBoxStatuses, TestResults, TestResultValues IN EXCLUSIVE MODE;
ALTER TABLE SchedQueues      ADD FOREIGN KEY (idTestSetGangLeader) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestBoxStatuses  ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResults      ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultValues ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
COMMIT;

\d+ TestSets;

