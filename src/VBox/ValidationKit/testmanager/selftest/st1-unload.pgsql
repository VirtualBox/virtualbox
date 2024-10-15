-- $Id: st1-unload.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager - Self Test #1 Database Unload File.
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



\set ON_ERROR_STOP 1
\connect testmanager;

BEGIN WORK;

DELETE FROM TestBoxStatuses;
DELETE FROM SchedQueues;

DELETE FROM SchedGroupMembers   WHERE uidAuthor = 1112223331;
UPDATE TestBoxes SET idSchedGroup = 1 WHERE idSchedGroup IN ( SELECT idSchedGroup FROM SchedGroups WHERE uidAuthor = 1112223331 );
DELETE FROM SchedGroups         WHERE uidAuthor = 1112223331 OR sName = 'st1-group';

UPDATE TestSets SET idTestResult = NULL
    WHERE idTestCase IN ( SELECT idTestCase FROM TestCases WHERE uidAuthor = 1112223331 );

DELETE FROM TestResultValues
    WHERE idTestResult IN ( SELECT idTestResult FROM TestResults
                            WHERE idTestSet IN (  SELECT idTestSet  FROM TestSets
                                                  WHERE idTestCase IN ( SELECT idTestCase FROM TestCases
                                                                        WHERE uidAuthor = 1112223331 ) ) );
DELETE FROM TestResultFiles
    WHERE idTestResult IN ( SELECT idTestResult FROM TestResults
                            WHERE idTestSet IN (  SELECT idTestSet  FROM TestSets
                                                  WHERE idTestCase IN ( SELECT idTestCase FROM TestCases
                                                                        WHERE uidAuthor = 1112223331 ) ) );
DELETE FROM TestResultMsgs
    WHERE idTestResult IN ( SELECT idTestResult FROM TestResults
                            WHERE idTestSet IN (  SELECT idTestSet  FROM TestSets
                                                  WHERE idTestCase IN ( SELECT idTestCase FROM TestCases
                                                                        WHERE uidAuthor = 1112223331 ) ) );
DELETE FROM TestResults
    WHERE idTestSet IN (  SELECT idTestSet  FROM TestSets
                          WHERE idTestCase IN ( SELECT idTestCase FROM TestCases WHERE uidAuthor = 1112223331 ) );
DELETE FROM TestSets
    WHERE idTestCase IN ( SELECT idTestCase FROM TestCases WHERE uidAuthor = 1112223331 );

DELETE FROM TestCases           WHERE uidAuthor = 1112223331;
DELETE FROM TestCaseArgs        WHERE uidAuthor = 1112223331;
DELETE FROM TestGroups          WHERE uidAuthor = 1112223331 OR sName = 'st1-testgroup';
DELETE FROM TestGroupMembers    WHERE uidAuthor = 1112223331;

DELETE FROM BuildSources        WHERE uidAuthor = 1112223331;
DELETE FROM Builds              WHERE uidAuthor = 1112223331;
DELETE FROM BuildCategories     WHERE sProduct  = 'st1';

DELETE FROM Users               WHERE uid       = 1112223331;

COMMIT WORK;

