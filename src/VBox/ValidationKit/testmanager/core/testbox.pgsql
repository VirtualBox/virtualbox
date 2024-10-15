-- $Id: testbox.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database Stored Procedures - TestBoxes.
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


--
-- Old type signatures.
--
DROP FUNCTION IF EXISTS TestBoxLogic_addEntry(a_uidAuthor            INTEGER,
                                                 a_ip                   inet,
                                                 a_uuidSystem           uuid,
                                                 a_sName                TEXT,
                                                 a_sDescription         TEXT,
                                                 a_idSchedGroup         INTEGER,
                                                 a_fEnabled             BOOLEAN,
                                                 a_enmLomKind           LomKind_T,
                                                 a_ipLom                inet,
                                                 a_pctScaleTimeout      INTEGER,  -- Actually smallint, but default typing fun.
                                                 a_sComment             TEXT,
                                                 a_enmPendingCmd        TestBoxCmd_T,
                                                 OUT r_idTestBox        INTEGER,
                                                 OUT r_idGenTestBox     INTEGER,
                                                 OUT r_tsEffective      TIMESTAMP WITH TIME ZONE);
DROP FUNCTION IF EXISTS TestBoxLogic_editEntry(a_uidAuthor           INTEGER,
                                                  a_idTestBox           INTEGER,
                                                  a_ip                  inet,
                                                  a_uuidSystem          uuid,
                                                  a_sName               TEXT,
                                                  a_sDescription        TEXT,
                                                  a_idSchedGroup        INTEGER,
                                                  a_fEnabled            BOOLEAN,
                                                  a_enmLomKind          LomKind_T,
                                                  a_ipLom               inet,
                                                  a_pctScaleTimeout     INTEGER, -- Actually smallint, but default typing fun.
                                                  a_sComment            TEXT,
                                                  a_enmPendingCmd       TestBoxCmd_T,
                                                  OUT r_idGenTestBox    INTEGER,
                                                  OUT r_tsEffective     TIMESTAMP WITH TIME ZONE);
DROP FUNCTION IF EXISTS TestBoxLogic_removeEntry(INTEGER, INTEGER, BOOLEAN);
DROP FUNCTION IF EXISTS TestBoxLogic_addGroupEntry(a_uidAuthor           INTEGER,
                                                   a_idTestBox           INTEGER,
                                                   a_idSchedGroup        INTEGER,
                                                   a_iSchedPriority      INTEGER,
                                                   OUT r_tsEffective     TIMESTAMP WITH TIME ZONE);
DROP FUNCTION IF EXISTS    TestBoxLogic_editGroupEntry(a_uidAuthor          INTEGER,
                                                       a_idTestBox          INTEGER,
                                                       a_idSchedGroup       INTEGER,
                                                       a_iSchedPriority     INTEGER,
                                                       OUT r_tsEffective    INTEGER);


---
-- Checks if the test box name is unique, ignoring a_idTestCaseIgnore.
-- Raises exception if duplicates are found.
--
-- @internal
--
CREATE OR REPLACE FUNCTION TestBoxLogic_checkUniqueName(a_sName TEXT, a_idTestBoxIgnore INTEGER)
    RETURNS VOID AS $$
    DECLARE
        v_cRows INTEGER;
    BEGIN
        SELECT  COUNT(*) INTO v_cRows
        FROM    TestBoxes
        WHERE   sName      =  a_sName
            AND tsExpire   =  'infinity'::TIMESTAMP
            AND idTestBox <> a_idTestBoxIgnore;
        IF v_cRows <> 0 THEN
            RAISE EXCEPTION 'Duplicate test box name "%" (% times)', a_sName, v_cRows;
        END IF;
    END;
$$ LANGUAGE plpgsql;


---
-- Checks that the given scheduling group exists.
-- Raises exception if it doesn't.
--
-- @internal
--
CREATE OR REPLACE FUNCTION TestBoxLogic_checkSchedGroupExists(a_idSchedGroup INTEGER)
    RETURNS VOID AS $$
    DECLARE
        v_cRows INTEGER;
    BEGIN
        SELECT  COUNT(*) INTO v_cRows
        FROM    SchedGroups
        WHERE   idSchedGroup = a_idSchedGroup
            AND tsExpire     =  'infinity'::TIMESTAMP;
        IF v_cRows <> 1 THEN
            IF v_cRows = 0 THEN
                RAISE EXCEPTION 'Scheduling group with ID % was not found', a_idSchedGroup;
            END IF;
            RAISE EXCEPTION 'Integrity error in SchedGroups: % current rows with idSchedGroup=%', v_cRows, a_idSchedGroup;
        END IF;
    END;
$$ LANGUAGE plpgsql;


---
-- Checks that the given testbxo + scheduling group pair does not currently exists.
-- Raises exception if it does.
--
-- @internal
--
CREATE OR REPLACE FUNCTION TestBoxLogic_checkTestBoxNotInSchedGroup(a_idTestBox INTEGER, a_idSchedGroup INTEGER)
    RETURNS VOID AS $$
    DECLARE
        v_cRows INTEGER;
    BEGIN
        SELECT  COUNT(*) INTO v_cRows
        FROM    TestBoxesInSchedGroups
        WHERE   idTestBox    = a_idTestBox
            AND idSchedGroup = a_idSchedGroup
            AND tsExpire     =  'infinity'::TIMESTAMP;
        IF v_cRows <> 0 THEN
            RAISE EXCEPTION 'TestBox % is already a member of scheduling group %', a_idTestBox, a_idSchedGroup;
        END IF;
    END;
$$ LANGUAGE plpgsql;


---
-- Historize a row.
-- @internal
--
CREATE OR REPLACE FUNCTION TestBoxLogic_historizeEntry(a_idGenTestBox INTEGER, a_tsExpire TIMESTAMP WITH TIME ZONE)
    RETURNS VOID AS $$
    DECLARE
        v_cUpdatedRows INTEGER;
    BEGIN
        UPDATE  TestBoxes
          SET   tsExpire        = a_tsExpire
          WHERE idGenTestBox    = a_idGenTestBox
            AND tsExpire        = 'infinity'::TIMESTAMP;
        GET DIAGNOSTICS v_cUpdatedRows = ROW_COUNT;
        IF v_cUpdatedRows <> 1 THEN
            IF v_cUpdatedRows = 0 THEN
                RAISE EXCEPTION 'Test box generation ID % is no longer valid', a_idGenTestBox;
            END IF;
            RAISE EXCEPTION 'Integrity error in TestBoxes: % current rows with idGenTestBox=%', v_cUpdatedRows, a_idGenTestBox;
        END IF;
    END;
$$ LANGUAGE plpgsql;


---
-- Historize a in-scheduling-group row.
-- @internal
--
CREATE OR REPLACE FUNCTION TestBoxLogic_historizeGroupEntry(a_idTestBox INTEGER,
                                                            a_idSchedGroup INTEGER,
                                                            a_tsExpire TIMESTAMP WITH TIME ZONE)
    RETURNS VOID AS $$
    DECLARE
        v_cUpdatedRows INTEGER;
    BEGIN
        UPDATE  TestBoxesInSchedGroups
          SET   tsExpire        = a_tsExpire
          WHERE idTestBox       = a_idTestBox
            AND idSchedGroup    = a_idSchedGroup
            AND tsExpire        = 'infinity'::TIMESTAMP;
        GET DIAGNOSTICS v_cUpdatedRows = ROW_COUNT;
        IF v_cUpdatedRows <> 1 THEN
            IF v_cUpdatedRows = 0 THEN
                RAISE EXCEPTION 'TestBox ID % / SchedGroup ID % is no longer a valid combination', a_idTestBox, a_idSchedGroup;
            END IF;
            RAISE EXCEPTION 'Integrity error in TestBoxesInSchedGroups: % current rows for % / %',
                v_cUpdatedRows, a_idTestBox, a_idSchedGroup;
        END IF;
    END;
$$ LANGUAGE plpgsql;


---
-- Translate string via the string table.
--
-- @returns NULL if a_sValue is NULL, otherwise a string ID.
--
CREATE OR REPLACE FUNCTION TestBoxLogic_lookupOrFindString(a_sValue TEXT)
    RETURNS INTEGER AS $$
    DECLARE
        v_idStr        INTEGER;
        v_cRows        INTEGER;
    BEGIN
        IF a_sValue IS NULL THEN
            RETURN NULL;
        END IF;

        SELECT      idStr
            INTO    v_idStr
            FROM    TestBoxStrTab
            WHERE   sValue = a_sValue;
        GET DIAGNOSTICS v_cRows = ROW_COUNT;
        IF v_cRows = 0 THEN
            INSERT INTO TestBoxStrTab (sValue)
                VALUES (a_sValue)
                RETURNING idStr INTO v_idStr;
        END IF;
        RETURN v_idStr;
    END;
$$ LANGUAGE plpgsql;


---
-- Only adds the user settable parts of the row, i.e. not what TestBoxLogic_updateOnSignOn touches.
--
CREATE OR REPLACE function TestBoxLogic_addEntry(a_uidAuthor            INTEGER,
                                                 a_ip                   inet,
                                                 a_uuidSystem           uuid,
                                                 a_sName                TEXT,
                                                 a_sDescription         TEXT,
                                                 a_fEnabled             BOOLEAN,
                                                 a_enmLomKind           LomKind_T,
                                                 a_ipLom                inet,
                                                 a_pctScaleTimeout      INTEGER,  -- Actually smallint, but default typing fun.
                                                 a_sComment             TEXT,
                                                 a_enmPendingCmd        TestBoxCmd_T,
                                                 OUT r_idTestBox        INTEGER,
                                                 OUT r_idGenTestBox     INTEGER,
                                                 OUT r_tsEffective      TIMESTAMP WITH TIME ZONE
                                                 ) AS $$
    DECLARE
         v_idStrDescription INTEGER;
         v_idStrComment     INTEGER;
    BEGIN
        PERFORM TestBoxLogic_checkUniqueName(a_sName, -1);

        SELECT TestBoxLogic_lookupOrFindString(a_sDescription) INTO v_idStrDescription;
        SELECT TestBoxLogic_lookupOrFindString(a_sComment)     INTO v_idStrComment;

        INSERT INTO TestBoxes (
                    tsEffective,         -- 1
                    uidAuthor,           -- 2
                    ip,                  -- 3
                    uuidSystem,          -- 4
                    sName,               -- 5
                    idStrDescription,    -- 6
                    fEnabled,            -- 7
                    enmLomKind,          -- 8
                    ipLom,               -- 9
                    pctScaleTimeout,     -- 10
                    idStrComment,        -- 11
                    enmPendingCmd )      -- 12
            VALUES (CURRENT_TIMESTAMP,   -- 1
                    a_uidAuthor,         -- 2
                    a_ip,                -- 3
                    a_uuidSystem,        -- 4
                    a_sName,             -- 5
                    v_idStrDescription,  -- 6
                    a_fEnabled,          -- 7
                    a_enmLomKind,        -- 8
                    a_ipLom,             -- 9
                    a_pctScaleTimeout,   -- 10
                    v_idStrComment,      -- 11
                    a_enmPendingCmd )    -- 12
            RETURNING idTestBox, idGenTestBox, tsEffective INTO r_idTestBox, r_idGenTestBox, r_tsEffective;
    END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE function TestBoxLogic_addGroupEntry(a_uidAuthor           INTEGER,
                                                      a_idTestBox           INTEGER,
                                                      a_idSchedGroup        INTEGER,
                                                      a_iSchedPriority      INTEGER,
                                                      OUT r_tsEffective     TIMESTAMP WITH TIME ZONE
                                                      ) AS $$
    BEGIN
        PERFORM TestBoxLogic_checkSchedGroupExists(a_idSchedGroup);
        PERFORM TestBoxLogic_checkTestBoxNotInSchedGroup(a_idTestBox, a_idSchedGroup);

        INSERT INTO TestBoxesInSchedGroups (
                    idTestBox,
                    idSchedGroup,
                    tsEffective,
                    tsExpire,
                    uidAuthor,
                    iSchedPriority)
            VALUES (a_idTestBox,
                    a_idSchedGroup,
                    CURRENT_TIMESTAMP,
                    'infinity'::TIMESTAMP,
                    a_uidAuthor,
                    a_iSchedPriority)
            RETURNING tsEffective INTO r_tsEffective;
    END;
$$ LANGUAGE plpgsql;


---
-- Only adds the user settable parts of the row, i.e. not what TestBoxLogic_updateOnSignOn touches.
--
CREATE OR REPLACE function TestBoxLogic_editEntry(a_uidAuthor           INTEGER,
                                                  a_idTestBox           INTEGER,
                                                  a_ip                  inet,
                                                  a_uuidSystem          uuid,
                                                  a_sName               TEXT,
                                                  a_sDescription        TEXT,
                                                  a_fEnabled            BOOLEAN,
                                                  a_enmLomKind          LomKind_T,
                                                  a_ipLom               inet,
                                                  a_pctScaleTimeout     INTEGER, -- Actually smallint, but default typing fun.
                                                  a_sComment            TEXT,
                                                  a_enmPendingCmd       TestBoxCmd_T,
                                                  OUT r_idGenTestBox    INTEGER,
                                                  OUT r_tsEffective     TIMESTAMP WITH TIME ZONE
                                                  ) AS $$
    DECLARE
        v_Row               TestBoxes%ROWTYPE;
        v_idStrDescription  INTEGER;
        v_idStrComment      INTEGER;
    BEGIN
        PERFORM TestBoxLogic_checkUniqueName(a_sName, a_idTestBox);

        SELECT TestBoxLogic_lookupOrFindString(a_sDescription) INTO v_idStrDescription;
        SELECT TestBoxLogic_lookupOrFindString(a_sComment)     INTO v_idStrComment;

        -- Fetch and historize the current row - there must be one.
        UPDATE      TestBoxes
            SET     tsExpire  = CURRENT_TIMESTAMP
            WHERE   idTestBox = a_idTestBox
                AND tsExpire  = 'infinity'::TIMESTAMP
            RETURNING * INTO STRICT v_Row;

        -- Modify the row with the new data.
        v_Row.uidAuthor         := a_uidAuthor;
        v_Row.ip                := a_ip;
        v_Row.uuidSystem        := a_uuidSystem;
        v_Row.sName             := a_sName;
        v_Row.idStrDescription  := v_idStrDescription;
        v_Row.fEnabled          := a_fEnabled;
        v_Row.enmLomKind        := a_enmLomKind;
        v_Row.ipLom             := a_ipLom;
        v_Row.pctScaleTimeout   := a_pctScaleTimeout;
        v_Row.idStrComment      := v_idStrComment;
        v_Row.enmPendingCmd     := a_enmPendingCmd;
        v_Row.tsEffective       := v_Row.tsExpire;
        r_tsEffective           := v_Row.tsExpire;
        v_Row.tsExpire          := 'infinity'::TIMESTAMP;

        -- Get a new generation ID.
        SELECT NEXTVAL('TestBoxGenIdSeq') INTO v_Row.idGenTestBox;
        r_idGenTestBox  := v_Row.idGenTestBox;

        -- Insert the modified row.
        INSERT INTO TestBoxes VALUES (v_Row.*);
    END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE function TestBoxLogic_editGroupEntry(a_uidAuthor          INTEGER,
                                                       a_idTestBox          INTEGER,
                                                       a_idSchedGroup       INTEGER,
                                                       a_iSchedPriority     INTEGER,
                                                       OUT r_tsEffective    TIMESTAMP WITH TIME ZONE
                                                       ) AS $$
    DECLARE
        v_Row               TestBoxesInSchedGroups%ROWTYPE;
        v_idStrDescription  INTEGER;
        v_idStrComment      INTEGER;
    BEGIN
        PERFORM TestBoxLogic_checkSchedGroupExists(a_idSchedGroup);

        -- Fetch and historize the current row - there must be one.
        UPDATE      TestBoxesInSchedGroups
            SET     tsExpire     = CURRENT_TIMESTAMP
            WHERE   idTestBox    = a_idTestBox
                AND idSchedGroup = a_idSchedGroup
                AND tsExpire     = 'infinity'::TIMESTAMP
            RETURNING * INTO STRICT v_Row;

        -- Modify the row with the new data.
        v_Row.uidAuthor         := a_uidAuthor;
        v_Row.iSchedPriority    := a_iSchedPriority;
        v_Row.tsEffective       := v_Row.tsExpire;
        r_tsEffective           := v_Row.tsExpire;
        v_Row.tsExpire          := 'infinity'::TIMESTAMP;

        -- Insert the modified row.
        INSERT INTO TestBoxesInSchedGroups VALUES (v_Row.*);
    END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION TestBoxLogic_removeEntry(a_uidAuthor INTEGER, a_idTestBox INTEGER, a_fCascade BOOLEAN)
    RETURNS VOID AS $$
    DECLARE
        v_Row           TestBoxes%ROWTYPE;
        v_tsEffective   TIMESTAMP WITH TIME ZONE;
        v_Rec           RECORD;
        v_sErrors       TEXT;
    BEGIN
        --
        -- Check preconditions.
        --
        IF a_fCascade <> TRUE THEN
            -- @todo implement checks which throws useful exceptions.
        ELSE
            RAISE EXCEPTION 'CASCADE test box deletion is not implemented';
        END IF;

        --
        -- Delete all current groups, skipping history since we're also deleting the testbox.
        --
        UPDATE      TestBoxesInSchedGroups
            SET     tsExpire = CURRENT_TIMESTAMP
            WHERE   idTestBox   = a_idTestBox
                AND tsExpire    = 'infinity'::TIMESTAMP;

        --
        -- To preserve the information about who deleted the record, we try to
        -- add a dummy record which expires immediately.  I say try because of
        -- the primary key, we must let the new record be valid for 1 us. :-(
        --
        SELECT  * INTO STRICT v_Row
        FROM    TestBoxes
        WHERE   idTestBox  = a_idTestBox
            AND tsExpire   = 'infinity'::TIMESTAMP;

        v_tsEffective := CURRENT_TIMESTAMP - INTERVAL '1 microsecond';
        IF v_Row.tsEffective < v_tsEffective THEN
            PERFORM TestBoxLogic_historizeEntry(v_Row.idGenTestBox, v_tsEffective);

            v_Row.tsEffective   := v_tsEffective;
            v_Row.tsExpire      := CURRENT_TIMESTAMP;
            v_Row.uidAuthor     := a_uidAuthor;
            SELECT NEXTVAL('TestBoxGenIdSeq') INTO v_Row.idGenTestBox;
            INSERT INTO TestBoxes VALUES (v_Row.*);
        ELSE
            PERFORM TestBoxLogic_historizeEntry(v_Row.idGenTestBox, CURRENT_TIMESTAMP);
        END IF;

    EXCEPTION
        WHEN NO_DATA_FOUND THEN
            RAISE EXCEPTION 'Test box with ID % does not currently exist', a_idTestBox;
        WHEN TOO_MANY_ROWS THEN
            RAISE EXCEPTION 'Integrity error in TestBoxes: Too many current rows for %', a_idTestBox;
    END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION TestBoxLogic_removeGroupEntry(a_uidAuthor INTEGER, a_idTestBox INTEGER, a_idSchedGroup INTEGER)
    RETURNS VOID AS $$
    DECLARE
        v_Row           TestBoxesInSchedGroups%ROWTYPE;
        v_tsEffective   TIMESTAMP WITH TIME ZONE;
    BEGIN
        --
        -- To preserve the information about who deleted the record, we try to
        -- add a dummy record which expires immediately.  I say try because of
        -- the primary key, we must let the new record be valid for 1 us. :-(
        --
        SELECT  * INTO STRICT v_Row
        FROM    TestBoxesInSchedGroups
        WHERE   idTestBox    = a_idTestBox
            AND idSchedGroup = a_idSchedGroup
            AND tsExpire     = 'infinity'::TIMESTAMP;

        v_tsEffective := CURRENT_TIMESTAMP - INTERVAL '1 microsecond';
        IF v_Row.tsEffective < v_tsEffective THEN
            PERFORM TestBoxLogic_historizeGroupEntry(a_idTestBox, a_idSchedGroup, v_tsEffective);

            v_Row.tsEffective   := v_tsEffective;
            v_Row.tsExpire      := CURRENT_TIMESTAMP;
            v_Row.uidAuthor     := a_uidAuthor;
            INSERT INTO TestBoxesInSchedGroups VALUES (v_Row.*);
        ELSE
            PERFORM TestBoxLogic_historizeGroupEntry(a_idTestBox, a_idSchedGroup, CURRENT_TIMESTAMP);
        END IF;

    EXCEPTION
        WHEN NO_DATA_FOUND THEN
            RAISE EXCEPTION 'TestBox #% does is not currently a member of scheduling group #%', a_idTestBox, a_idSchedGroup;
        WHEN TOO_MANY_ROWS THEN
            RAISE EXCEPTION 'Integrity error in TestBoxesInSchedGroups: Too many current rows for % / %',
                a_idTestBox, a_idSchedGroup;
    END;
$$ LANGUAGE plpgsql;


---
-- Sign on update
--
CREATE OR REPLACE function TestBoxLogic_updateOnSignOn(a_idTestBox          INTEGER,
                                                       a_ip                 inet,
                                                       a_sOs                TEXT,
                                                       a_sOsVersion         TEXT,
                                                       a_sCpuVendor         TEXT,
                                                       a_sCpuArch           TEXT,
                                                       a_sCpuName           TEXT,
                                                       a_lCpuRevision       bigint,
                                                       a_cCpus              INTEGER, -- Actually smallint, but default typing fun.
                                                       a_fCpuHwVirt         boolean,
                                                       a_fCpuNestedPaging   boolean,
                                                       a_fCpu64BitGuest     boolean,
                                                       a_fChipsetIoMmu      boolean,
                                                       a_fRawMode           boolean,
                                                       a_fNativeApi         boolean,
                                                       a_cMbMemory          bigint,
                                                       a_cMbScratch         bigint,
                                                       a_sReport            TEXT,
                                                       a_iTestBoxScriptRev  INTEGER,
                                                       a_iPythonHexVersion  INTEGER,
                                                       OUT r_idGenTestBox   INTEGER
                                                       ) AS $$
    DECLARE
        v_Row               TestBoxes%ROWTYPE;
        v_idStrOs           INTEGER;
        v_idStrOsVersion    INTEGER;
        v_idStrCpuVendor    INTEGER;
        v_idStrCpuArch      INTEGER;
        v_idStrCpuName      INTEGER;
        v_idStrReport       INTEGER;
    BEGIN
        SELECT TestBoxLogic_lookupOrFindString(a_sOs)           INTO v_idStrOs;
        SELECT TestBoxLogic_lookupOrFindString(a_sOsVersion)    INTO v_idStrOsVersion;
        SELECT TestBoxLogic_lookupOrFindString(a_sCpuVendor)    INTO v_idStrCpuVendor;
        SELECT TestBoxLogic_lookupOrFindString(a_sCpuArch)      INTO v_idStrCpuArch;
        SELECT TestBoxLogic_lookupOrFindString(a_sCpuName)      INTO v_idStrCpuName;
        SELECT TestBoxLogic_lookupOrFindString(a_sReport)       INTO v_idStrReport;

        -- Fetch and historize the current row - there must be one.
        UPDATE      TestBoxes
            SET     tsExpire     = CURRENT_TIMESTAMP
            WHERE   idTestBox    = a_idTestBox
                AND tsExpire     = 'infinity'::TIMESTAMP
            RETURNING * INTO STRICT v_Row;

        -- Modify the row with the new data.
        v_Row.uidAuthor             := NULL;
        v_Row.ip                    := a_ip;
        v_Row.idStrOs               := v_idStrOs;
        v_Row.idStrOsVersion        := v_idStrOsVersion;
        v_Row.idStrCpuVendor        := v_idStrCpuVendor;
        v_Row.idStrCpuArch          := v_idStrCpuArch;
        v_Row.idStrCpuName          := v_idStrCpuName;
        v_Row.lCpuRevision          := a_lCpuRevision;
        v_Row.cCpus                 := a_cCpus;
        v_Row.fCpuHwVirt            := a_fCpuHwVirt;
        v_Row.fCpuNestedPaging      := a_fCpuNestedPaging;
        v_Row.fCpu64BitGuest        := a_fCpu64BitGuest;
        v_Row.fChipsetIoMmu         := a_fChipsetIoMmu;
        v_Row.fRawMode              := a_fRawMode;
        v_Row.fNativeApi            := a_fNativeApi;
        v_Row.cMbMemory             := a_cMbMemory;
        v_Row.cMbScratch            := a_cMbScratch;
        v_Row.idStrReport           := v_idStrReport;
        v_Row.iTestBoxScriptRev     := a_iTestBoxScriptRev;
        v_Row.iPythonHexVersion     := a_iPythonHexVersion;
        v_Row.tsEffective           := v_Row.tsExpire;
        v_Row.tsExpire              := 'infinity'::TIMESTAMP;

        -- Get a new generation ID.
        SELECT NEXTVAL('TestBoxGenIdSeq') INTO v_Row.idGenTestBox;
        r_idGenTestBox  := v_Row.idGenTestBox;

        -- Insert the modified row.
        INSERT INTO TestBoxes VALUES (v_Row.*);
    END;
$$ LANGUAGE plpgsql;


---
-- Set new command.
--
CREATE OR REPLACE function TestBoxLogic_setCommand(a_uidAuthor          INTEGER,
                                                   a_idTestBox          INTEGER,
                                                   a_enmOldCmd          TestBoxCmd_T,
                                                   a_enmNewCmd          TestBoxCmd_T,
                                                   a_sComment           TEXT,
                                                   OUT r_idGenTestBox   INTEGER,
                                                   OUT r_tsEffective    TIMESTAMP WITH TIME ZONE
                                                   ) AS $$
    DECLARE
        v_Row               TestBoxes%ROWTYPE;
        v_idStrComment      INTEGER;
    BEGIN
        SELECT TestBoxLogic_lookupOrFindString(a_sComment) INTO v_idStrComment;

        -- Fetch and historize the current row - there must be one.
        UPDATE      TestBoxes
            SET     tsExpire      = CURRENT_TIMESTAMP
            WHERE   idTestBox     = a_idTestBox
                AND tsExpire      = 'infinity'::TIMESTAMP
                AND enmPendingCmd = a_enmOldCmd
            RETURNING * INTO STRICT v_Row;

        -- Modify the row with the new data.
        v_Row.enmPendingCmd         := a_enmNewCmd;
        IF v_idStrComment IS NOT NULL THEN
            v_Row.idStrComment      := v_idStrComment;
        END IF;
        v_Row.tsEffective           := v_Row.tsExpire;
        r_tsEffective               := v_Row.tsExpire;
        v_Row.tsExpire              := 'infinity'::TIMESTAMP;

        -- Get a new generation ID.
        SELECT NEXTVAL('TestBoxGenIdSeq') INTO v_Row.idGenTestBox;
        r_idGenTestBox  := v_Row.idGenTestBox;

        -- Insert the modified row.
        INSERT INTO TestBoxes VALUES (v_Row.*);
    END;
$$ LANGUAGE plpgsql;

