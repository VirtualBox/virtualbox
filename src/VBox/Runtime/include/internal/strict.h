/* $Id: strict.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal Header Defining Strictness Indicators.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_INTERNAL_strict_h
#define IPRT_INCLUDED_INTERNAL_strict_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @name Strictness Indicators
 * @{ */

/** @def RTCRITSECT_STRICT
 * Enables strictness checks and lock accounting of the RTCritSect API.
 */
#if (!defined(RTCRITSECT_STRICT)      && defined(IN_RING3) && defined(RT_LOCK_STRICT)) || defined(DOXYGEN_RUNNING)
# define RTCRITSECT_STRICT
#endif

/** @def RTCRITSECTRW_STRICT
 * Enables strictness checks and lock accounting of the RTCritSectRw API.
 */
#if (!defined(RTCRITSECTRW_STRICT)    && defined(IN_RING3) && defined(RT_LOCK_STRICT)) || defined(DOXYGEN_RUNNING)
# define RTCRITSECTRW_STRICT
#endif

/** @def RTSEMEVENT_STRICT
 * Enables strictness checks and lock accounting of the RTSemEvent API.
 */
#if (!defined(RTSEMEVENT_STRICT)      && defined(IN_RING3) && defined(RT_LOCK_STRICT)) || defined(DOXYGEN_RUNNING)
# define RTSEMEVENT_STRICT
#endif

/** @def RTSEMEVENTMULTI_STRICT
 * Enables strictness checks and lock accounting of the RTSemEventMulti API.
 */
#if (!defined(RTSEMEVENTMULTI_STRICT) && defined(IN_RING3) && defined(RT_LOCK_STRICT)) || defined(DOXYGEN_RUNNING)
# define RTSEMEVENTMULTI_STRICT
#endif

/** @def RTSEMMUTEX_STRICT
 * Enables strictness checks and lock accounting of the RTSemMutex API.
 */
#if (!defined(RTSEMMUTEX_STRICT)      && defined(IN_RING3) && defined(RT_LOCK_STRICT)) || defined(DOXYGEN_RUNNING)
# define RTSEMMUTEX_STRICT
#endif


/** @def RTSEMRW_STRICT
 * Enables strictness checks and lock accounting of the RTSemRW API.
 */
#if (!defined(RTSEMRW_STRICT)         && defined(IN_RING3) && defined(RT_LOCK_STRICT)) || defined(DOXYGEN_RUNNING)
# define RTSEMRW_STRICT
#endif


/** @} */

#endif /* !IPRT_INCLUDED_INTERNAL_strict_h */
