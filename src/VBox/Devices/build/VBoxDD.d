/* $Id: VBoxDD.d 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDD - Static dtrace probes
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

provider vboxdd
{
    probe hgcmcall__enter(void *pvCmd, uint32_t idFunction, uint32_t idClient, uint32_t cbCmd);
    probe hgcmcall__completed__req(void *pvCmd, int rc);
    probe hgcmcall__completed__emt(void *pvCmd, int rc);
    probe hgcmcall__completed__done(void *pvCmd, uint32_t idFunction, uint32_t idClient, int rc);

    probe ahci__req__submit(void *pvReq, int iTxDir, uint64_t offStart, uint64_t cbXfer);
    probe ahci__req__completed(void *pvReq, int rcReq, uint64_t offStart, uint64_t cbXfer);

    probe hda__stream__setup(uint32_t idxStream, int32_t rc, uint32_t uHz, uint64_t cTicksPeriod, uint32_t cbPeriod);
    probe hda__stream__reset(uint32_t idxStream);
    probe hda__stream__dma__out(uint32_t idxStream, uint32_t cb, uint64_t off);
    probe hda__stream__dma__in(uint32_t idxStream, uint32_t cb, uint64_t off);
    probe hda__stream__dma__flowerror(uint32_t idxStream, uint32_t cbFree, uint32_t cbPeriod, int32_t fOverflow);

    probe audio__mixer__sink__aio__out(uint32_t idxStream, uint32_t cb, uint64_t off);
    probe audio__mixer__sink__aio__in(uint32_t idxStream, uint32_t cb, uint64_t off);
};

#pragma D attributes Evolving/Evolving/Common provider vboxdd provider
#pragma D attributes Private/Private/Unknown  provider vboxdd module
#pragma D attributes Private/Private/Unknown  provider vboxdd function
#pragma D attributes Evolving/Evolving/Common provider vboxdd name
#pragma D attributes Evolving/Evolving/Common provider vboxdd args

