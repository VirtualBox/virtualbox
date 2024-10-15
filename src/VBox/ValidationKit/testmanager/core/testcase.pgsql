-- $Id: testcase.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database Stored Procedures - TestCases.
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

DROP FUNCTION IF EXISTS add_testcase(INTEGER, TEXT, TEXT, BOOLEAN, INTEGER, TEXT, TEXT);
DROP FUNCTION IF EXISTS edit_testcase(INTEGER, INTEGER, TEXT, TEXT, BOOLEAN, INTEGER, TEXT, TEXT);
DROP FUNCTION IF EXISTS del_testcase(INTEGER);
DROP FUNCTION IF EXISTS TestCaseLogic_delEntry(INTEGER, INTEGER);
DROP FUNCTION IF EXISTS TestCaseLogic_addEntry(a_uidAuthor INTEGER, a_sName TEXT, a_sDescription TEXT,
                                               a_fEnabled BOOL, a_cSecTimeout INTEGER,  a_sTestBoxReqExpr TEXT,
                                               a_sBuildReqExpr TEXT, a_sBaseCmd TEXT, a_sTestSuiteZips TEXT);
DROP FUNCTION IF EXISTS TestCaseLogic_editEntry(a_uidAuthor INTEGER, a_idTestCase INTEGER, a_sName TEXT, a_sDescription TEXT,
                                                a_fEnabled BOOL, a_cSecTimeout INTEGER,  a_sTestBoxReqExpr TEXT,
                                                a_sBuildReqExpr TEXT, a_sBaseCmd TEXT, a_sTestSuiteZips TEXT);

---
-- Checks if the test case name is unique, ignoring a_idTestCaseIgnore.
-- Raises exception if duplicates are found.
--
-- @internal
--
CREATE OR REPLACE FUNCTION TestCaseLogic_checkUniqueName(a_sName TEXT, a_idTestCaseIgnore INTEGER)
    RETURNS VOID AS $$
    DECLARE
        v_cRows INTEGER;
    BEGIN
        SELECT  COUNT(*) INTO v_cRows
        FROM    TestCases
        WHERE   sName      =  a_sName
            AND tsExpire   =  'infinity'::TIMESTAMP
            AND idTestCase <> a_idTestCaseIgnore;
        IF v_cRows <> 0 THEN
            RAISE EXCEPTION 'Duplicate test case name "%" (% times)', a_sName, v_cRows;
        END IF;
    END;
$$ LANGUAGE plpgsql;

---
-- Check that the test case exists.
-- Raises exception if it doesn't.
--
-- @internal
--
CREATE OR REPLACE FUNCTION TestCaseLogic_checkExists(a_idTestCase INTEGER) RETURNS VOID AS $$
    BEGIN
        IF NOT EXISTS(  SELECT  *
                        FROM    TestCases
                        WHERE   idTestCase = a_idTestCase
                            AND tsExpire   = 'infinity'::TIMESTAMP ) THEN
            RAISE EXCEPTION 'Test case with ID % does not currently exist', a_idTestCase;
        END IF;
    END;
$$ LANGUAGE plpgsql;


---
-- Historize a row.
-- @internal
--
CREATE OR REPLACE FUNCTION TestCaseLogic_historizeEntry(a_idTestCase INTEGER, a_tsExpire TIMESTAMP WITH TIME ZONE)
    RETURNS VOID AS $$
    DECLARE
        v_cUpdatedRows INTEGER;
    BEGIN
        UPDATE  TestCases
          SET   tsExpire   = a_tsExpire
          WHERE idTestcase = a_idTestCase
            AND tsExpire   = 'infinity'::TIMESTAMP;
        GET DIAGNOSTICS v_cUpdatedRows = ROW_COUNT;
        IF v_cUpdatedRows <> 1 THEN
            IF v_cUpdatedRows = 0 THEN
                RAISE EXCEPTION 'Test case ID % does not currently exist', a_idTestCase;
            END IF;
            RAISE EXCEPTION 'Integrity error in TestCases: % current rows with idTestCase=%d', v_cUpdatedRows, a_idTestCase;
        END IF;
    END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE function TestCaseLogic_addEntry(a_uidAuthor INTEGER, a_sName TEXT, a_sDescription TEXT,
                                                  a_fEnabled BOOL, a_cSecTimeout INTEGER,  a_sTestBoxReqExpr TEXT,
                                                  a_sBuildReqExpr TEXT, a_sBaseCmd TEXT, a_sTestSuiteZips TEXT,
                                                  a_sComment TEXT)
    RETURNS INTEGER AS $$
    DECLARE
         v_idTestCase INTEGER;
    BEGIN
        PERFORM TestCaseLogic_checkUniqueName(a_sName, -1);

        INSERT INTO TestCases (uidAuthor, sName, sDescription, fEnabled, cSecTimeout,
                               sTestBoxReqExpr, sBuildReqExpr, sBaseCmd, sTestSuiteZips, sComment)
            VALUES (a_uidAuthor, a_sName, a_sDescription, a_fEnabled, a_cSecTimeout,
                    a_sTestBoxReqExpr, a_sBuildReqExpr, a_sBaseCmd, a_sTestSuiteZips, a_sComment)
            RETURNING idTestcase INTO v_idTestCase;
        RETURN v_idTestCase;
    END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE function TestCaseLogic_editEntry(a_uidAuthor INTEGER, a_idTestCase INTEGER, a_sName TEXT, a_sDescription TEXT,
                                                   a_fEnabled BOOL, a_cSecTimeout INTEGER,  a_sTestBoxReqExpr TEXT,
                                                   a_sBuildReqExpr TEXT, a_sBaseCmd TEXT, a_sTestSuiteZips TEXT,
                                                   a_sComment TEXT)
    RETURNS INTEGER AS $$
    DECLARE
         v_idGenTestCase INTEGER;
    BEGIN
        PERFORM TestCaseLogic_checkExists(a_idTestCase);
        PERFORM TestCaseLogic_checkUniqueName(a_sName, a_idTestCase);

        PERFORM TestCaseLogic_historizeEntry(a_idTestCase, CURRENT_TIMESTAMP);
        INSERT INTO TestCases (idTestCase, uidAuthor, sName, sDescription, fEnabled, cSecTimeout,
                               sTestBoxReqExpr, sBuildReqExpr, sBaseCmd, sTestSuiteZips, sComment)
            VALUES (a_idTestCase, a_uidAuthor, a_sName, a_sDescription, a_fEnabled, a_cSecTimeout,
                    a_sTestBoxReqExpr, a_sBuildReqExpr, a_sBaseCmd, a_sTestSuiteZips, a_sComment)
            RETURNING idGenTestCase INTO v_idGenTestCase;
       RETURN v_idGenTestCase;
    END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION TestCaseLogic_delEntry(a_uidAuthor INTEGER, a_idTestCase INTEGER, a_fCascade BOOLEAN)
    RETURNS VOID AS $$
    DECLARE
        v_Row           TestCases%ROWTYPE;
        v_tsEffective   TIMESTAMP WITH TIME ZONE;
        v_Rec           RECORD;
        v_sErrors       TEXT;
    BEGIN
        --
        -- Check preconditions.
        --
        IF a_fCascade <> TRUE THEN
            IF EXISTS(  SELECT  *
                        FROM    TestCaseDeps
                        WHERE   idTestCasePreReq = a_idTestCase
                            AND tsExpire         = 'infinity'::TIMESTAMP ) THEN
                v_sErrors := '';
                FOR v_Rec IN
                    SELECT  TestCases.idTestCase AS idTestCase,
                            TestCases.sName AS sName
                    FROM    TestCaseDeps, TestCases
                    WHERE   TestCaseDeps.idTestCasePreReq   = a_idTestCase
                        AND TestCaseDeps.tsExpire           = 'infinity'::TIMESTAMP
                        AND TestCases.idTestCase            = TestCaseDeps.idTestCase
                        AND TestCases.tsExpire              = 'infinity'::TIMESTAMP
                    LOOP
                    IF v_sErrors <> '' THEN
                        v_sErrors := v_sErrors || ', ';
                    END IF;
                    v_sErrors := v_sErrors || v_Rec.sName || ' (idTestCase=' || v_Rec.idTestCase || ')';
                END LOOP;
                RAISE EXCEPTION 'Other test cases depends on test case with ID %: % ', a_idTestCase, v_sErrors;
            END IF;

            IF EXISTS(  SELECT  *
                        FROM    TestGroupMembers
                        WHERE   idTestCase = a_idTestCase
                            AND tsExpire   = 'infinity'::TIMESTAMP ) THEN
                v_sErrors := '';
                FOR v_Rec IN
                    SELECT  TestGroups.idTestGroup AS idTestGroup,
                            TestGroups.sName AS sName
                    FROM    TestGroupMembers, TestGroups
                    WHERE   TestGroupMembers.idTestCase     = a_idTestCase
                        AND TestGroupMembers.tsExpire       = 'infinity'::TIMESTAMP
                        AND TestGroupMembers.idTestGroup    = TestGroups.idTestGroup
                        AND TestGroups.tsExpire             = 'infinity'::TIMESTAMP
                    LOOP
                    IF v_sErrors <> '' THEN
                        v_sErrors := v_sErrors || ', ';
                    END IF;
                    v_sErrors := v_sErrors || v_Rec.sName || ' (idTestGroup=' || v_Rec.idTestGroup || ')';
                END LOOP;
                RAISE EXCEPTION 'Test case with ID % is member of the following test group(s): % ', a_idTestCase, v_sErrors;
            END IF;
        END IF;

        --
        -- To preserve the information about who deleted the record, we try to
        -- add a dummy record which expires immediately.  I say try because of
        -- the primary key, we must let the new record be valid for 1 us. :-(
        --
        SELECT  * INTO STRICT v_Row
        FROM    TestCases
        WHERE   idTestCase = a_idTestCase
            AND tsExpire   = 'infinity'::TIMESTAMP;

        v_tsEffective := CURRENT_TIMESTAMP - INTERVAL '1 microsecond';
        IF v_Row.tsEffective < v_tsEffective THEN
            PERFORM TestCaseLogic_historizeEntry(a_idTestCase, v_tsEffective);
            v_Row.tsEffective   := v_tsEffective;
            v_Row.tsExpire      := CURRENT_TIMESTAMP;
            v_Row.uidAuthor     := a_uidAuthor;
            SELECT NEXTVAL('TestCaseGenIdSeq') INTO v_Row.idGenTestCase;
            INSERT INTO TestCases VALUES (v_Row.*);
        ELSE
            PERFORM TestCaseLogic_historizeEntry(a_idTestCase, CURRENT_TIMESTAMP);
        END IF;

        --
        -- Delete arguments, test case dependencies and resource dependencies.
        -- (We don't bother recording who deleted the records here since it's
        -- a lot of work and sufficiently covered in the TestCases table.)
        --
        UPDATE  TestCaseArgs
        SET     tsExpire   = CURRENT_TIMESTAMP
        WHERE   idTestCase = a_idTestCase
            AND tsExpire   = 'infinity'::TIMESTAMP;

        UPDATE  TestCaseDeps
        SET     tsExpire   = CURRENT_TIMESTAMP
        WHERE   idTestCase = a_idTestCase
            AND tsExpire   = 'infinity'::TIMESTAMP;

        UPDATE  TestCaseGlobalRsrcDeps
        SET     tsExpire   = CURRENT_TIMESTAMP
        WHERE   idTestCase = a_idTestCase
            AND tsExpire   = 'infinity'::TIMESTAMP;

        IF a_fCascade = TRUE THEN
            UPDATE  TestCaseDeps
            SET     tsExpire         = CURRENT_TIMESTAMP
            WHERE   idTestCasePreReq = a_idTestCase
                AND tsExpire         = 'infinity'::TIMESTAMP;

            UPDATE  TestGroupMembers
            SET     tsExpire   = CURRENT_TIMESTAMP
            WHERE   idTestCase = a_idTestCase
                AND tsExpire   = 'infinity'::TIMESTAMP;
        END IF;

    EXCEPTION
        WHEN NO_DATA_FOUND THEN
            RAISE EXCEPTION 'Test case with ID % does not currently exist', a_idTestCase;
        WHEN TOO_MANY_ROWS THEN
            RAISE EXCEPTION 'Integrity error in TestCases: Too many current rows for %', a_idTestCase;
    END;
$$ LANGUAGE plpgsql;

