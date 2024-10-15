-- $Id: tmdb-r15-index-sorting.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Index tuning effort.
--

--
-- Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
-- Reordered, modified and new indexes.
--
\d UsersLoginNameIdx;
DROP   INDEX UsersLoginNameIdx;
CREATE INDEX UsersLoginNameIdx ON Users (sLoginName, tsExpire DESC);
\d UsersLoginNameIdx;
ANALYZE VERBOSE Users;


\d TestCaseArgsLookupIdx;
DROP   INDEX TestCaseArgsLookupIdx;
CREATE INDEX TestCaseArgsLookupIdx ON TestCaseArgs (idTestCase, tsExpire DESC, tsEffective ASC);
\d TestCaseArgsLookupIdx;
ANALYZE VERBOSE TestCaseArgs;


\d TestGroups_id_index;
DROP   INDEX TestGroups_id_index;
CREATE INDEX TestGroups_id_index ON TestGroups (idTestGroup, tsExpire DESC, tsEffective ASC);
\d TestGroups_id_index;
ANALYZE VERBOSE TestGroups;


\d TestBoxesUuidIdx;
DROP          INDEX TestBoxesUuidIdx;
CREATE UNIQUE INDEX TestBoxesUuidIdx ON TestBoxes (uuidSystem, tsExpire DESC);
\d TestBoxesUuidIdx;
DROP INDEX IF EXISTS TestBoxesExpireEffectiveIdx;
CREATE INDEX TestBoxesExpireEffectiveIdx ON TestBoxes (tsExpire DESC, tsEffective ASC);
\d TestBoxesExpireEffectiveIdx;
ANALYZE VERBOSE TestBoxes;


DROP INDEX IF EXISTS BuildBlacklistIdx;
CREATE INDEX BuildBlacklistIdx ON BuildBlacklist (iLastRevision DESC, iFirstRevision ASC, sProduct, sBranch,
                                                  tsExpire DESC, tsEffective ASC);
\d BuildBlacklist;
ANALYZE VERBOSE BuildBlacklist;


\d TestResultsNameIdx;
DROP INDEX TestResultsNameIdx;
CREATE INDEX TestResultsNameIdx ON TestResults (idStrName, tsCreated DESC);
\d TestResultsNameIdx;
DROP INDEX IF EXISTS TestResultsNameIdx2;
CREATE INDEX TestResultsNameIdx2 ON TestResults (idTestResult, idStrName);
\d TestResultsNameIdx2;
ANALYZE VERBOSE TestResults;


\d TestSetsCreatedDoneIdx;
DROP   INDEX TestSetsCreatedDoneIdx;
DROP INDEX IF EXISTS TestSetsDoneCreatedBuildCatIdx;
CREATE INDEX TestSetsDoneCreatedBuildCatIdx ON TestSets (tsDone DESC NULLS FIRST, tsCreated ASC, idBuildCategory);
\d TestSetsDoneCreatedBuildCatIdx;
\d TestSetsGraphBoxIdx;
DROP   INDEX TestSetsGraphBoxIdx;
CREATE INDEX TestSetsGraphBoxIdx    ON TestSets (idTestBox, tsCreated DESC, tsDone ASC NULLS LAST, idBuildCategory, idTestCase);
\d TestSetsGraphBoxIdx;
ANALYZE VERBOSE TestSets;


DROP INDEX IF EXISTS SchedQueuesItemIdx;
CREATE INDEX SchedQueuesItemIdx          ON SchedQueues(idItem);
\d SchedQueuesItemIdx;
DROP INDEX IF EXISTS SchedQueuesSchedGroupIdx;
CREATE INDEX SchedQueuesSchedGroupIdx    ON SchedQueues(idSchedGroup);
\d SchedQueuesSchedGroupIdx;
ANALYZE VERBOSE SchedQueues;

