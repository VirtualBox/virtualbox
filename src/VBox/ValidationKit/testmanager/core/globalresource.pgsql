-- $Id: globalresource.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database Stored Procedures.
--

--
-- Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

-- Args: uidAuthor, sName, sDescription, fEnabled
CREATE OR REPLACE function add_globalresource(integer, text, text, bool) RETURNS integer AS $$
    DECLARE
        _idGlobalRsrc integer;
        _uidAuthor ALIAS FOR $1;
        _sName ALIAS FOR $2;
        _sDescription ALIAS FOR $3;
        _fEnabled ALIAS FOR $4;
    BEGIN
        -- Check if Global Resource name is unique
        IF EXISTS(SELECT * FROM GlobalResources
                    WHERE sName=_sName AND
                          tsExpire='infinity'::timestamp) THEN
            RAISE EXCEPTION 'Duplicate Global Resource name';
        END IF;
        INSERT INTO GlobalResources (uidAuthor, sName, sDescription, fEnabled)
            VALUES (_uidAuthor, _sName, _sDescription, _fEnabled) RETURNING idGlobalRsrc INTO _idGlobalRsrc;
        RETURN _idGlobalRsrc;
    END;
$$ LANGUAGE plpgsql;

-- Args: uidAuthor, idGlobalRsrc
CREATE OR REPLACE function del_globalresource(integer, integer) RETURNS VOID AS $$
    DECLARE
        _uidAuthor ALIAS FOR $1;
        _idGlobalRsrc ALIAS FOR $2;
    BEGIN

        -- Check if record exist
        IF NOT EXISTS(SELECT * FROM GlobalResources WHERE idGlobalRsrc=_idGlobalRsrc AND tsExpire='infinity'::timestamp) THEN
            RAISE EXCEPTION 'Global resource (%) does not exist', _idGlobalRsrc;
        END IF;

        -- Historize record: GlobalResources
        UPDATE GlobalResources
          SET   tsExpire=CURRENT_TIMESTAMP,
                uidAuthor=_uidAuthor
          WHERE idGlobalRsrc=_idGlobalRsrc AND
                tsExpire='infinity'::timestamp;


        -- Delete record: GlobalResourceStatuses
        DELETE FROM GlobalResourceStatuses WHERE idGlobalRsrc=_idGlobalRsrc;

        -- Historize record: TestCaseGlobalRsrcDeps
        UPDATE TestCaseGlobalRsrcDeps
          SET   tsExpire=CURRENT_TIMESTAMP,
                uidAuthor=_uidAuthor
          WHERE idGlobalRsrc=_idGlobalRsrc AND
                tsExpire='infinity'::timestamp;

    END;
$$ LANGUAGE plpgsql;

-- Args: uidAuthor, idGlobalRsrc, sName, sDescription, fEnabled
CREATE OR REPLACE function update_globalresource(integer, integer, text, text, bool) RETURNS VOID AS $$
    DECLARE
        _uidAuthor ALIAS FOR $1;
        _idGlobalRsrc ALIAS FOR $2;
        _sName ALIAS FOR $3;
        _sDescription ALIAS FOR $4;
        _fEnabled ALIAS FOR $5;
    BEGIN
        -- Hostorize record
        UPDATE GlobalResources
          SET   tsExpire=CURRENT_TIMESTAMP
          WHERE idGlobalRsrc=_idGlobalRsrc AND
                tsExpire='infinity'::timestamp;
        -- Check if Global Resource name is unique
        IF EXISTS(SELECT * FROM GlobalResources
                    WHERE sName=_sName AND
                          tsExpire='infinity'::timestamp) THEN
            RAISE EXCEPTION 'Duplicate Global Resource name';
        END IF;
        -- Add new record
        INSERT INTO GlobalResources(uidAuthor, idGlobalRsrc, sName, sDescription, fEnabled)
            VALUES (_uidAuthor, _idGlobalRsrc, _sName, _sDescription, _fEnabled);
    END;
$$ LANGUAGE plpgsql;
