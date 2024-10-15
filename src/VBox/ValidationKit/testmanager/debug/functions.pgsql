-- $Id: functions.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- ?????????????????????????
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

\connect testmanager;

DROP FUNCTION authenticate_testbox(inet, uuid);
DROP FUNCTION testbox_status_set(integer, TestBoxState_T);

-- Authenticate Test Box record by IP and UUID and set its state to IDLE
-- Args: IP, UUID
CREATE OR REPLACE FUNCTION authenticate_testbox(inet, uuid) RETURNS testboxes AS $$
    DECLARE
        _ip ALIAS FOR $1;
        _uuidSystem ALIAS FOR $2;
        _box TestBoxes;
    BEGIN
        -- Find Test Box record
        SELECT *
            FROM testboxes
            WHERE ip=_ip AND uuidSystem=_uuidSystem INTO _box;
        IF FOUND THEN
            -- Update Test Box status if exists
            UPDATE TestBoxStatuses SET enmState='idle' WHERE idTestBox=_box.idTestBox;
            IF NOT FOUND THEN
                -- Otherwise, add new record to TestBoxStatuses table
                INSERT
                    INTO TestBoxStatuses(idTestBox, idGenTestBox, enmState)
                    VALUES (_box.idTestBox, _box.idGenTestBox, 'idle');
            END IF;
        END IF;
        return _box;
    END;
$$ LANGUAGE plpgsql;

-- Set Test Box status and make sure if it has been set
-- Args: Test Box ID, new status
CREATE OR REPLACE FUNCTION testbox_status_set(integer, TestBoxState_T) RETURNS VOID AS $$
    DECLARE
        _box ALIAS FOR $1;
        _status ALIAS FOR $2;
    BEGIN
        -- Update Test Box status if exists
        UPDATE TestBoxStatuses SET enmState=_status WHERE idTestBox=_box;
        IF NOT FOUND THEN
            RAISE EXCEPTION 'Test Box (#%) was not found in database', _box;
        END IF;
    END;
$$ LANGUAGE plpgsql;

