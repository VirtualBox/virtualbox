-- $Id: tmdb-r16-testcaseargs-1-testresultfailures-1.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds sName to TestCaseArgs, idTestSet
-- to TestResultFailures and add some indexes to the latter as well.
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


DROP TABLE OldTestCaseArgs;
DROP TABLE NewTestCaseArgs;


\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0

LOCK TABLE TestBoxStatuses    IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestSets           IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestCaseArgs       IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestResultFailures IN ACCESS EXCLUSIVE MODE;

--
-- TestCaseArgs is simple and we can use ALTER TABLE for a change.
--
\d TestCaseArgs;
ALTER TABLE TestCaseArgs ADD COLUMN sSubName text DEFAULT NULL;
\d TestCaseArgs;


--
-- Rename the original table, drop constrains and foreign key references so we
-- get the right name automatic when creating the new one.
--
\d TestResultFailures;
ALTER TABLE TestResultFailures DROP CONSTRAINT idTestResultFk;
ALTER TABLE TestResultFailures RENAME TO OldTestResultFailures;

DROP INDEX IF EXISTS TestResultFailureIdx;
DROP INDEX IF EXISTS TestResultFailureIdx2;
DROP INDEX IF EXISTS TestResultFailureIdx3;


CREATE TABLE TestResultFailures (
    --- The test result we're disucssing.
    -- @note The foreign key is declared after TestResults (further down).
    idTestResult        INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,
    --- The testsest this result is a part of.
    -- This is mainly an aid for bypassing the enormous TestResults table.
    -- Note! This is a foreign key, but we have to add it after TestSets has
    --       been created, see further down.
    idTestSet           INTEGER     NOT NULL,

    --- The suggested failure reason.
    -- Non-unique foreign key: FailureReasons(idFailureReason)
    idFailureReason     INTEGER     NOT NULL,
    --- Optional comment.
    sComment            text        DEFAULT NULL,

    PRIMARY KEY (idTestResult, tsExpire)
);

INSERT INTO TestResultFailures ( idTestResult, tsEffective, tsExpire, uidAuthor, idTestSet, idFailureReason, sComment )
    SELECT o.idTestResult, o.tsEffective, o.tsExpire, o.uidAuthor, tr.idTestSet, o.idFailureReason, sComment
    FROM   OldTestResultFailures o,
           TestResults tr
    WHERE  o.idTestResult = tr.idTestResult;

-- Add unique constraint to TestResult for our new foreign key.
ALTER TABLE TestResults ADD CONSTRAINT TestResults_idTestResult_idTestSet_key UNIQUE (idTestResult, idTestSet);

-- Restore foreign key.
ALTER TABLE TestResultFailures ADD CONSTRAINT TestResultFailures_idTestResult_idTestSet_fkey
    FOREIGN KEY (idTestResult, idTestSet) REFERENCES TestResults(idTestResult, idTestSet) MATCH FULL;

-- Add new indexes.
CREATE INDEX TestResultFailureIdx  ON TestResultFailures (idTestSet, tsExpire DESC, tsEffective ASC);
CREATE INDEX TestResultFailureIdx2 ON TestResultFailures (idTestResult, tsExpire DESC, tsEffective ASC);
CREATE INDEX TestResultFailureIdx3 ON TestResultFailures (idFailureReason, idTestResult, tsExpire DESC, tsEffective ASC);

-- Drop the old table.
DROP TABLE OldTestResultFailures;

COMMIT;

\d TestResultFailures;

