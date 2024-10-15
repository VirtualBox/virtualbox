-- $Id: tmdb-r20-testcases-1-testgroups-1-schedgroups-1.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds sComment to TestCases, TestGroups
--                              and SchedGroups.
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



\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0

LOCK TABLE TestBoxes          IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestBoxStatuses    IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestCases          IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestGroups         IN ACCESS EXCLUSIVE MODE;
LOCK TABLE SchedGroups        IN ACCESS EXCLUSIVE MODE;

--
-- All the changes are rather simple and we'll just add the sComment column last.
--
\d TestCases;
\d TestGroups;
\d SchedGroups;

ALTER TABLE TestCases   ADD COLUMN sComment TEXT DEFAULT NULL;
ALTER TABLE TestGroups  ADD COLUMN sComment TEXT DEFAULT NULL;
ALTER TABLE SchedGroups ADD COLUMN sComment TEXT DEFAULT NULL;

\d TestCases;
\d TestGroups;
\d SchedGroups;

\prompt "Update python files while everything is locked. Hurry!"  dummy

COMMIT;

