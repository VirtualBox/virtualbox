/* $Id: NetworkServiceRunner.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - interface for VBox DHCP server.
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

#ifndef MAIN_INCLUDED_NetworkServiceRunner_h
#define MAIN_INCLUDED_NetworkServiceRunner_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/string.h>


/** @name Internal networking trunk type option values (NetworkServiceRunner::kpszKeyTrunkType)
 *  @{ */
#define TRUNKTYPE_WHATEVER "whatever"
#define TRUNKTYPE_NETFLT   "netflt"
#define TRUNKTYPE_NETADP   "netadp"
#define TRUNKTYPE_SRVNAT   "srvnat"
/** @} */

/**
 * Network service runner.
 *
 * Build arguments, starts and stops network service processes.
 */
class NetworkServiceRunner
{
public:
    NetworkServiceRunner(const char *aProcName);
    virtual ~NetworkServiceRunner();

    /** @name Argument management
     * @{ */
    int  addArgument(const char *pszArgument);
    int  addArgPair(const char *pszOption, const char *pszValue);
    void resetArguments();
    /** @} */

    int  start(bool aKillProcessOnStop);
    int  stop();
    void detachFromServer();
    bool isRunning();

    RTPROCESS getPid() const;

    /** @name Common options
     * @{ */
    static const char * const kpszKeyNetwork;
    static const char * const kpszKeyTrunkType;
    static const char * const kpszTrunkName;
    static const char * const kpszMacAddress;
    static const char * const kpszIpAddress;
    static const char * const kpszIpNetmask;
    static const char * const kpszKeyNeedMain;
    /** @} */

private:
    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_NetworkServiceRunner_h */

