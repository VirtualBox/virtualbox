-- $Id: tmdb-r18-testresultfiles-1-testresultmsgs-1.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds an idTestSet to TestResultFiles and TestResultMsgs.
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
DROP TABLE IF EXISTS NewTestResultFiles;
DROP TABLE IF EXISTS OldTestResultFiles;
DROP TABLE IF EXISTS NewTestResultMsgs;
DROP TABLE IF EXISTS OldTestResultMsgs;

-- Die on error from now on.
\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0


--
-- Rename the original table, drop constrains and foreign key references so we
-- get the right name automatic when creating the new one.
--
\d+ TestResultFiles;
ALTER TABLE TestResultFiles RENAME TO OldTestResultFiles;

DROP INDEX IF EXISTS TestResultFilesIdx;
DROP INDEX IF EXISTS TestResultFilesIdx2;

--
-- Create the new version of the table and filling with the content of the old.
--
CREATE TABLE TestResultFiles (
    --- The ID of this file.
    idTestResultFile    INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestResultFileId'),
    --- The test result it was reported within.
    idTestResult        INTEGER     NOT NULL,
    --- The test set this file is a part of (for avoiding joining thru TestResults).
    idTestSet           INTEGER     NOT NULL,
    --- Creation time stamp.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The filename relative to TestSets(sBaseFilename) + '-'.
    -- The set of valid filename characters should be very limited so that no
    -- file system issues can occure either on the TM side or the user when
    -- loading the files.  Tests trying to use other characters will fail.
    -- Valid character regular expession: '^[a-zA-Z0-9_-(){}#@+,.=]*$'
    idStrFile           INTEGER     NOT NULL,
    --- The description.
    idStrDescription    INTEGER     NOT NULL,
    --- The kind of file.
    -- For instance: 'log/release/vm',
    --               'screenshot/failure',
    --               'screencapture/failure',
    --               'xmllog/somestuff'
    idStrKind           INTEGER     NOT NULL,
    --- The mime type for the file.
    -- For instance: 'text/plain',
    --               'image/png',
    --               'video/webm',
    --               'text/xml'
    idStrMime           INTEGER     NOT NULL
);

INSERT INTO TestResultFiles ( idTestResultFile, idTestResult, idTestSet, tsCreated, idStrFile, idStrDescription,
                              idStrKind, idStrMime)
    SELECT o.idTestResultFile, o.idTestResult, tr.idTestSet, o.tsCreated, o.idStrFile, o.idStrDescription,
           o.idStrKind, o.idStrMime
    FROM   OldTestResultFiles o,
           TestResults tr
    WHERE  o.idTestResult = tr.idTestResult;

-- Add new indexes.
CREATE INDEX TestResultFilesIdx  ON TestResultFiles(idTestResult);
CREATE INDEX TestResultFilesIdx2 ON TestResultFiles(idTestSet, tsCreated DESC);

-- Restore foreign keys.
ALTER TABLE TestResultFiles ADD CONSTRAINT TestResultFiles_idTestResult_fkey     FOREIGN KEY(idTestResult)     REFERENCES TestResults(idTestResult);
ALTER TABLE TestResultFiles ADD CONSTRAINT TestResultFiles_idTestSet_fkey        FOREIGN KEY(idTestSet)        REFERENCES TestSets(idTestSet);
ALTER TABLE TestResultFiles ADD CONSTRAINT TestResultFiles_idStrFile_fkey        FOREIGN KEY(idStrFile)        REFERENCES TestResultStrTab(idStr);
ALTER TABLE TestResultFiles ADD CONSTRAINT TestResultFiles_idStrDescription_fkey FOREIGN KEY(idStrDescription) REFERENCES TestResultStrTab(idStr);
ALTER TABLE TestResultFiles ADD CONSTRAINT TestResultFiles_idStrKind_fkey        FOREIGN KEY(idStrKind)        REFERENCES TestResultStrTab(idStr);
ALTER TABLE TestResultFiles ADD CONSTRAINT TestResultFiles_idStrMime_fkey        FOREIGN KEY(idStrMime)        REFERENCES TestResultStrTab(idStr);

\d TestResultFiles;


--
-- Rename the original table, drop constrains and foreign key references so we
-- get the right name automatic when creating the new one.
--
\d+ TestResultMsgs;
ALTER TABLE TestResultMsgs RENAME TO OldTestResultMsgs;

DROP INDEX IF EXISTS TestResultMsgsIdx;
DROP INDEX IF EXISTS TestResultMsgsIdx2;

--
-- Create the new version of the table and filling with the content of the old.
--
CREATE TABLE TestResultMsgs (
    --- The ID of this file.
    idTestResultMsg     INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestResultMsgIdSeq'),
    --- The test result it was reported within.
    idTestResult        INTEGER     NOT NULL,
    --- The test set this file is a part of (for avoiding joining thru TestResults).
    idTestSet           INTEGER     NOT NULL,
    --- Creation time stamp.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The message string.
    idStrMsg            INTEGER     NOT NULL,
    --- The message level.
    enmLevel            TestResultMsgLevel_T  NOT NULL
);

INSERT INTO TestResultMsgs ( idTestResultMsg, idTestResult, idTestSet, tsCreated, idStrMsg, enmLevel)
    SELECT o.idTestResultMsg, o.idTestResult, tr.idTestSet, o.tsCreated, o.idStrMsg, o.enmLevel
    FROM   OldTestResultMsgs o,
           TestResults tr
    WHERE  o.idTestResult = tr.idTestResult;

-- Add new indexes.
CREATE INDEX TestResultMsgsIdx  ON TestResultMsgs(idTestResult);
CREATE INDEX TestResultMsgsIdx2 ON TestResultMsgs(idTestSet, tsCreated DESC);

-- Restore foreign keys.
ALTER TABLE TestResultMsgs ADD CONSTRAINT TestResultMsgs_idTestResult_fkey FOREIGN KEY(idTestResult)        REFERENCES TestResults(idTestResult);
ALTER TABLE TestResultMsgs ADD CONSTRAINT TestResultMsgs_idTestSet_fkey    FOREIGN KEY(idTestSet)           REFERENCES TestSets(idTestSet);
ALTER TABLE TestResultMsgs ADD CONSTRAINT TestResultMsgs_idStrMsg_fkey     FOREIGN KEY(idStrMsg)            REFERENCES TestResultStrTab(idStr);


\d TestResultMsgs;


--
-- Drop the old tables and commit.
--
DROP TABLE OldTestResultFiles;
DROP TABLE OldTestResultMsgs;

COMMIT;

