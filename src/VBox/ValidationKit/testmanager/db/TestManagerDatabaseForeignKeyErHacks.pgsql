-- $Id: TestManagerDatabaseForeignKeyErHacks.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database Addendum that adds non-unique foreign keys.
--
-- This is for getting better visualization in reverse engeering ER tools,
-- it is not for production databases.
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
\connect testmanager

ALTER TABLE TestCaseArgs
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idTestCase, tsExpire)             REFERENCES TestCases(idTestCase, tsExpire) MATCH FULL;

ALTER TABLE TestcaseDeps
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idTestCase, tsExpire)             REFERENCES TestCases(idTestCase, tsExpire) MATCH FULL;
ALTER TABLE TestcaseDeps
   ADD CONSTRAINT non_unique_fk2 FOREIGN KEY (idTestCasePreReq,tsExpire)        REFERENCES TestCases(idTestCase, tsExpire) MATCH FULL;

ALTER TABLE TestCaseGlobalRsrcDeps
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idTestCase, tsExpire)             REFERENCES TestCases(idTestCase, tsExpire) MATCH FULL;
ALTER TABLE TestCaseGlobalRsrcDeps
   ADD CONSTRAINT non_unique_fk2 FOREIGN KEY (idGlobalRsrc, tsExpire)           REFERENCES GlobalResources(idGlobalRsrc, tsExpire) MATCH FULL;

ALTER TABLE TestGroupMembers
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idTestGroup, tsExpire)            REFERENCES TestGroups(idTestGroup, tsExpire) MATCH FULL;
ALTER TABLE TestGroupMembers
   ADD CONSTRAINT non_unique_fk2 FOREIGN KEY (idTestCase, tsExpire)             REFERENCES TestCases(idTestCase, tsExpire) MATCH FULL;

ALTER TABLE SchedGroups
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idBuildSrc, tsExpire)             REFERENCES BuildSources(idBuildSrc, tsExpire) MATCH SIMPLE;
ALTER TABLE SchedGroups
   ADD CONSTRAINT non_unique_fk2 FOREIGN KEY (idBuildSrcTestSuite, tsExpire)    REFERENCES BuildSources(idBuildSrc, tsExpire) MATCH SIMPLE;

ALTER TABLE SchedGroupMembers
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idSchedGroup, tsExpire)           REFERENCES SchedGroups(idSchedGroup, tsExpire) MATCH FULL;
ALTER TABLE SchedGroupMembers
   ADD CONSTRAINT non_unique_fk2 FOREIGN KEY (idTestGroup, tsExpire)            REFERENCES TestGroups(idTestGroup, tsExpire) MATCH FULL;
ALTER TABLE SchedGroupMembers
   ADD CONSTRAINT non_unique_fk3 FOREIGN KEY (idTestGroupPreReq, tsExpire)      REFERENCES TestGroups(idTestGroup, tsExpire) MATCH FULL;

ALTER TABLE TestBoxes
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idSchedGroup, tsExpire)           REFERENCES SchedGroups(idSchedGroup, tsExpire) MATCH FULL;

ALTER TABLE FailureReasons
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idFailureCategory, tsExpire)      REFERENCES FailureCategories(idFailureCategory, tsExpire) MATCH FULL;

ALTER TABLE TestResultFailures
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idFailureReason, tsExpire)        REFERENCES FailureReasons(idFailureReason, tsExpire) MATCH FULL;

ALTER TABLE BuildBlacklist
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idFailureReason, tsExpire)        REFERENCES FailureReasons(idFailureReason, tsExpire) MATCH FULL;

ALTER TABLE GlobalResourceStatuses
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idGlobalRsrc, tsAllocated)        REFERENCES GlobalResources(idGlobalRsrc, tsExpire) MATCH FULL;

ALTER TABLE SchedQueues
   ADD CONSTRAINT non_unique_fk1 FOREIGN KEY (idSchedGroup, tsLastScheduled)    REFERENCES SchedGroups(idSchedGroup, tsExpire) MATCH FULL;

