-- $Id: tmdb-r22-testboxes-3-teststatus-4-testboxinschedgroups-1.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Turns idSchedGroup column in TestBoxes
-- into an N:M relationship with a priority via the new table
-- TestBoxesInSchedGroups.  Adds an internal scheduling table index to
-- TestBoxStatuses to implement testboxes switching between groups.
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
DROP TABLE IF EXISTS OldTestBoxes;

--
-- Die on error from now on.
--
\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0


-- Total grid lock.
LOCK TABLE TestBoxStatuses      IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestSets             IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestBoxes            IN ACCESS EXCLUSIVE MODE;
LOCK TABLE SchedGroups          IN ACCESS EXCLUSIVE MODE;
LOCK TABLE SchedGroupMembers    IN ACCESS EXCLUSIVE MODE;

\d+ TestBoxes;

--
-- We'll only be doing simple alterations so, no need to drop constraints
-- and stuff like we usually do first.
--

--
-- Create the new table and populate it.
--

CREATE TABLE TestBoxesInSchedGroups (
    --- TestBox ID.
    -- Non-unique foreign key: TestBoxes(idTestBox).
    idTestBox           INTEGER     NOT NULL,
    --- Scheduling ID.
    -- Non-unique foreign key: SchedGroups(idSchedGroup).
    idSchedGroup        INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- The scheduling priority of the scheduling group for the test box.
    -- Higher number causes the scheduling group to be serviced more frequently.
    -- @sa TestGroupMembers.iSchedPriority, SchedGroups.iSchedPriority
    iSchedPriority      INTEGER     DEFAULT 16 CHECK (iSchedPriority >= 0 AND iSchedPriority < 32)  NOT NULL,

    PRIMARY KEY (idTestBox, idSchedGroup, tsExpire)
);

GRANT ALL PRIVILEGES ON TABLE TestBoxesInSchedGroups TO testmanager;

CREATE OR REPLACE FUNCTION TestBoxesInSchedGroups_ConvertedOneBox(a_idTestBox INTEGER)
    RETURNS VOID AS $$
    DECLARE
        v_Row           RECORD;
        v_idSchedGroup  INTEGER;
        v_uidAuthor     INTEGER;
        v_tsEffective   TIMESTAMP WITH TIME ZONE;
        v_tsExpire      TIMESTAMP WITH TIME ZONE;
    BEGIN
        FOR v_Row IN
            SELECT  idTestBox,
                    idSchedGroup,
                    tsEffective,
                    tsExpire,
                    uidAuthor
            FROM    TestBoxes
            WHERE   idTestBox = a_idTestBox
            ORDER BY tsEffective, tsExpire
        LOOP
            IF v_idSchedGroup IS NOT NULL THEN
                IF (v_idSchedGroup != v_Row.idSchedGroup) OR (v_Row.tsEffective <> v_tsExpire) THEN
                    INSERT INTO TestBoxesInSchedGroups (idTestBox, idSchedGroup, tsEffective, tsExpire, uidAuthor)
                        VALUES (a_idTestBox, v_idSchedGroup, v_tsEffective, v_tsExpire, v_uidAuthor);
                    v_idSchedGroup := NULL;
                END IF;
            END IF;

            IF v_idSchedGroup IS NULL THEN
                v_idSchedGroup := v_Row.idSchedGroup;
                v_tsEffective  := v_Row.tsEffective;
            END IF;
            IF v_Row.uidAuthor IS NOT NULL THEN
                v_uidAuthor := v_Row.uidAuthor;
            END IF;
            v_tsExpire := v_Row.tsExpire;
        END LOOP;

        IF v_idSchedGroup != -1 THEN
            INSERT INTO TestBoxesInSchedGroups (idTestBox, idSchedGroup, tsEffective, tsExpire, uidAuthor)
                VALUES (a_idTestBox, v_idSchedGroup, v_tsEffective, v_tsExpire, v_uidAuthor);
        END IF;
    END;
$$ LANGUAGE plpgsql;

SELECT TestBoxesInSchedGroups_ConvertedOneBox(TestBoxIDs.idTestBox)
FROM ( SELECT DISTINCT idTestBox FROM TestBoxes ) AS TestBoxIDs;

DROP FUNCTION TestBoxesInSchedGroups_ConvertedOneBox(INTEGER);

--
-- Do the other two modifications.
--
ALTER TABLE TestBoxStatuses ADD COLUMN iWorkItem  INTEGER  DEFAULT 0  NOT NULL;

DROP VIEW TestBoxesWithStrings;
ALTER TABLE TestBoxes       DROP COLUMN idSchedGroup;
CREATE VIEW TestBoxesWithStrings AS
    SELECT  TestBoxes.*,
            Str1.sValue AS sDescription,
            Str2.sValue AS sComment,
            Str3.sValue AS sOs,
            Str4.sValue AS sOsVersion,
            Str5.sValue AS sCpuVendor,
            Str6.sValue AS sCpuArch,
            Str7.sValue AS sCpuName,
            Str8.sValue AS sReport
    FROM    TestBoxes
            LEFT OUTER JOIN TestBoxStrTab Str1 ON idStrDescription = Str1.idStr
            LEFT OUTER JOIN TestBoxStrTab Str2 ON idStrComment     = Str2.idStr
            LEFT OUTER JOIN TestBoxStrTab Str3 ON idStrOs          = Str3.idStr
            LEFT OUTER JOIN TestBoxStrTab Str4 ON idStrOsVersion   = Str4.idStr
            LEFT OUTER JOIN TestBoxStrTab Str5 ON idStrCpuVendor   = Str5.idStr
            LEFT OUTER JOIN TestBoxStrTab Str6 ON idStrCpuArch     = Str6.idStr
            LEFT OUTER JOIN TestBoxStrTab Str7 ON idStrCpuName     = Str7.idStr
            LEFT OUTER JOIN TestBoxStrTab Str8 ON idStrReport      = Str8.idStr;

GRANT ALL PRIVILEGES ON TABLE TestBoxesWithStrings TO testmanager;

\prompt "Update python files while everything is locked. Hurry!"  dummy

COMMIT;

\d TestBoxesInSchedGroups;
\d TestBoxStatuses;
\d TestBoxes;
ANALYZE VERBOSE TestBoxesInSchedGroups;

