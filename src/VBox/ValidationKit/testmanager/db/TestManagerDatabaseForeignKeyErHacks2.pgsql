-- $Id: TestManagerDatabaseForeignKeyErHacks2.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database Addendum that adds non-unique foreign keys to Users.
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

ALTER TABLE GlobalResources
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE BuildSources
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE RequirementSets
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsCreated) REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestCases
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestCaseArgs
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestcaseDeps
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestCaseGlobalRsrcDeps
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestGroups
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestGroupMembers
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE SchedGroups
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH SIMPLE;
ALTER TABLE SchedGroupMembers
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestBoxes
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE FailureCategories
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE FailureReasons
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE TestResultFailures
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE BuildBlacklist
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsExpire)  REFERENCES Users(uid, tsExpire) MATCH FULL;
ALTER TABLE Builds
   ADD CONSTRAINT non_unique_fk9 FOREIGN KEY (uidAuthor, tsCreated) REFERENCES Users(uid, tsExpire) MATCH FULL;

