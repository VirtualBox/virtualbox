-- $Id: tmdb-r21-testsets-4.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds an idSchedGroup to TestSets in
-- preparation for testboxes belonging to multiple scheduling queues.
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
DROP TABLE IF EXISTS OldTestSets;

--
-- Die on error from now on.
--
\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0


-- Total grid lock (don't want to deadlock below).
LOCK TABLE TestBoxStatuses      IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestSets             IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestBoxes            IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestResults          IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestResultFailures   IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestResultFiles      IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestResultMsgs       IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestResultValues     IN ACCESS EXCLUSIVE MODE;
LOCK TABLE SchedGroups          IN ACCESS EXCLUSIVE MODE;
LOCK TABLE SchedQueues          IN ACCESS EXCLUSIVE MODE;
LOCK TABLE SchedGroupMembers    IN ACCESS EXCLUSIVE MODE;

\d+ TestSets;

--
-- Rename the table, drop foreign keys refering to it, and drop constrains
-- within the table itself.  The latter is mostly for naming and we do it
-- up front in case the database we're running against has different names
-- due to previous conversions.
--
ALTER TABLE TestSets RENAME TO OldTestSets;

ALTER TABLE TestResultFailures  DROP CONSTRAINT IF EXISTS idtestsetfk;
ALTER TABLE TestResultFailures  DROP CONSTRAINT IF EXISTS TestResultFailures_idTestSet_fkey;
ALTER TABLE SchedQueues         DROP CONSTRAINT IF EXISTS SchedQueues_idTestSetGangLeader_fkey;
ALTER TABLE TestBoxStatuses     DROP CONSTRAINT IF EXISTS TestBoxStatuses_idTestSet_fkey;
ALTER TABLE TestResultFiles     DROP CONSTRAINT IF EXISTS TestResultFiles_idTestSet_fkey;
ALTER TABLE TestResultMsgs      DROP CONSTRAINT IF EXISTS TestResultMsgs_idTestSet_fkey;
ALTER TABLE TestResults         DROP CONSTRAINT IF EXISTS TestResults_idTestSet_fkey;
ALTER TABLE TestResultValues    DROP CONSTRAINT IF EXISTS TestResultValues_idTestSet_fkey;
ALTER TABLE TestResultValues    DROP CONSTRAINT IF EXISTS TestResultValues_idTestSet_fkey1;

ALTER TABLE OldTestSets     DROP CONSTRAINT testsets_igangmemberno_check;

ALTER TABLE OldTestSets     DROP CONSTRAINT TestSets_idBuildCategory_fkey;
ALTER TABLE OldTestSets     DROP CONSTRAINT TestSets_idGenTestBox_fkey;
ALTER TABLE OldTestSets     DROP CONSTRAINT TestSets_idGenTestCase_fkey;
ALTER TABLE OldTestSets     DROP CONSTRAINT TestSets_idGenTestCaseArgs_fkey;
ALTER TABLE OldTestSets     DROP CONSTRAINT TestSets_idTestResult_fkey;
ALTER TABLE OldTestSets     DROP CONSTRAINT TestSets_idTestSetGangLeader_fkey;

ALTER TABLE OldTestSets     DROP CONSTRAINT IF EXISTS TestSets_sBaseFilename_key;
ALTER TABLE OldTestSets     DROP CONSTRAINT IF EXISTS NewTestSets_sBaseFilename_key;
ALTER TABLE OldTestSets     DROP CONSTRAINT TestSets_pkey;

DROP INDEX IF EXISTS TestSetsGangIdx;
DROP INDEX IF EXISTS TestSetsBoxIdx;
DROP INDEX IF EXISTS TestSetsBuildIdx;
DROP INDEX IF EXISTS TestSetsTestCaseIdx;
DROP INDEX IF EXISTS TestSetsTestVarIdx;
DROP INDEX IF EXISTS TestSetsDoneCreatedBuildCatIdx;
DROP INDEX IF EXISTS TestSetsGraphBoxIdx;


-- This output should be free of indexes, constraints and references from other tables.
\d+ OldTestSets;

\prompt "Is the above table completely free of indexes, constraints and references? Ctrl-C if not."  dummy

--
-- Create the new table (no foreign keys).
--
CREATE TABLE TestSets (
    --- The ID of this test set.
    idTestSet           INTEGER   DEFAULT NEXTVAL('TestSetIdSeq')  NOT NULL,

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
    idBuildCategory     INTEGER      NOT NULL,
    --- The test suite build we're using to do the testing.
    -- This is NULL if the test suite zip wasn't referred or if a test suite
    -- build source wasn't configured.
    -- Non-unique foreign key: Builds(idBuild)
    idBuildTestSuite    INTEGER     DEFAULT NULL,

    --- The exact testbox configuration.
    idGenTestBox        INTEGER     NOT NULL,
    --- The testbox ID for joining with (valid: tsStarted).
    -- Non-unique foreign key: TestBoxes(idTestBox)
    idTestBox           INTEGER     NOT NULL,
    --- The scheduling group ID the test was scheduled thru (valid: tsStarted).
    -- Non-unique foreign key: SchedGroups(idSchedGroup)
    idSchedGroup        INTEGER     NOT NULL,

    --- The testgroup (valid: tsConfig).
    -- Non-unique foreign key: TestBoxes(idTestGroup)
    -- Note! This also gives the member ship entry, since a testcase can only
    --       have one membership per test group.
    idTestGroup         INTEGER     NOT NULL,

    --- The exact test case config we executed in this test run.
    idGenTestCase       INTEGER     NOT NULL,
    --- The test case ID for joining with (valid: tsConfig).
    -- Non-unique foreign key: TestBoxes(idTestCase)
    idTestCase          INTEGER     NOT NULL,

    --- The arguments (and requirements++) we executed this test case with.
    idGenTestCaseArgs   INTEGER     NOT NULL,
    --- The argument variation ID (valid: tsConfig).
    -- Non-unique foreign key: TestCaseArgs(idTestCaseArgs)
    idTestCaseArgs      INTEGER     NOT NULL,

    --- The root of the test result tree.
    -- @note This will only be NULL early in the transaction setting up the testset.
    -- @note If the test reports more than one top level test result, we'll
    --       fail the whole test run and let the test developer fix it.
    idTestResult        INTEGER     DEFAULT NULL,

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
    sBaseFilename       text        NOT NULL,

    --- The gang member number number, 0 is the leader.
    iGangMemberNo       SMALLINT    DEFAULT 0  NOT NULL, --  CHECK (iGangMemberNo >= 0 AND iGangMemberNo < 1024),
    --- The test set of the gang leader, NULL if no gang involved.
    -- @note This is set by the gang leader as well, so that we can find all
    --       gang members by WHERE idTestSetGangLeader = :id.
    idTestSetGangLeader INTEGER     DEFAULT NULL

);

-- Convert the data.
INSERT INTO TestSets (
            idTestSet,
            tsConfig,
            tsCreated,
            tsDone,
            enmStatus,
            idBuild,
            idBuildCategory,
            idBuildTestSuite,
            idGenTestBox,
            idTestBox,
            idSchedGroup,
            idTestGroup,
            idGenTestCase,
            idTestCase,
            idGenTestCaseArgs,
            idTestCaseArgs,
            idTestResult,
            sBaseFilename,
            iGangMemberNo,
            idTestSetGangLeader
            )
SELECT      OldTestSets.idTestSet,
            OldTestSets.tsConfig,
            OldTestSets.tsCreated,
            OldTestSets.tsDone,
            OldTestSets.enmStatus,
            OldTestSets.idBuild,
            OldTestSets.idBuildCategory,
            OldTestSets.idBuildTestSuite,
            OldTestSets.idGenTestBox,
            OldTestSets.idTestBox,
            TestBoxes.idSchedGroup,
            OldTestSets.idTestGroup,
            OldTestSets.idGenTestCase,
            OldTestSets.idTestCase,
            OldTestSets.idGenTestCaseArgs,
            OldTestSets.idTestCaseArgs,
            OldTestSets.idTestResult,
            OldTestSets.sBaseFilename,
            OldTestSets.iGangMemberNo,
            OldTestSets.idTestSetGangLeader
FROM        OldTestSets
            INNER JOIN TestBoxes
                    ON OldTestSets.idGenTestBox = TestBoxes.idGenTestBox;

-- Restore the primary key and unique constraints.
ALTER TABLE TestSets ADD PRIMARY KEY (idTestSet);
ALTER TABLE TestSets ADD UNIQUE (sBaseFilename);

-- Restore check constraints.
ALTER TABLE TestSets ADD CONSTRAINT TestSets_iGangMemberNo_Check CHECK (iGangMemberNo >= 0 AND iGangMemberNo < 1024);

-- Restore foreign keys in the table.
ALTER TABLE TestSets            ADD FOREIGN KEY (idBuildCategory)     REFERENCES BuildCategories(idBuildCategory);
ALTER TABLE TestSets            ADD FOREIGN KEY (idGenTestBox)        REFERENCES TestBoxes(idGenTestBox);
ALTER TABLE TestSets            ADD FOREIGN KEY (idGenTestCase)       REFERENCES TestCases(idGenTestCase);
ALTER TABLE TestSets            ADD FOREIGN KEY (idGenTestCaseArgs)   REFERENCES TestCaseArgs(idGenTestCaseArgs);
ALTER TABLE TestSets            ADD FOREIGN KEY (idTestResult)        REFERENCES TestResults(idTestResult);
ALTER TABLE TestSets            ADD FOREIGN KEY (idTestSetGangLeader) REFERENCES TestSets(idTestSet);

-- Restore indexes.
CREATE INDEX TestSetsGangIdx        ON TestSets (idTestSetGangLeader);
CREATE INDEX TestSetsBoxIdx         ON TestSets (idTestBox, idTestResult);
CREATE INDEX TestSetsBuildIdx       ON TestSets (idBuild, idTestResult);
CREATE INDEX TestSetsTestCaseIdx    ON TestSets (idTestCase, idTestResult);
CREATE INDEX TestSetsTestVarIdx     ON TestSets (idTestCaseArgs, idTestResult);
CREATE INDEX TestSetsDoneCreatedBuildCatIdx ON TestSets (tsDone DESC NULLS FIRST, tsCreated ASC, idBuildCategory);
CREATE INDEX TestSetsGraphBoxIdx    ON TestSets (idTestBox, tsCreated DESC, tsDone ASC NULLS LAST, idBuildCategory, idTestCase);

-- Restore foreign key references to the table.
ALTER TABLE TestResults         ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultValues    ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultFiles     ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultMsgs      ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultFailures  ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;

ALTER TABLE TestBoxStatuses     ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE SchedQueues         ADD FOREIGN KEY (idTestSetGangLeader) REFERENCES TestSets(idTestSet) MATCH FULL;

-- Drop the old table.
DROP TABLE OldTestSets;

\prompt "Update python files while everything is locked. Hurry!"  dummy

-- Grant access to the new table.
GRANT ALL PRIVILEGES ON TABLE TestSets TO testmanager;

COMMIT;

\d TestSets;

