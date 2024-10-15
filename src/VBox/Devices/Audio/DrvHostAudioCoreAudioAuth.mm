/* $Id: DrvHostAudioCoreAudioAuth.mm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host audio driver - Mac OS X CoreAudio, authorization helpers for Mojave+.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>

#include <iprt/errcore.h>
#include <iprt/semaphore.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
# import <AVFoundation/AVFoundation.h>
# import <AVFoundation/AVMediaFormat.h>
#endif
#import <Foundation/NSException.h>


#if MAC_OS_X_VERSION_MIN_REQUIRED < 101400

/* HACK ALERT! It's there in the 10.13 SDK, but only for iOS 7.0+. Deploying CPP trickery to shut up warnings/errors. */
# if MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
#  define AVAuthorizationStatus                 OurAVAuthorizationStatus
#  define AVAuthorizationStatusNotDetermined    OurAVAuthorizationStatusNotDetermined
#  define AVAuthorizationStatusRestricted       OurAVAuthorizationStatusRestricted
#  define AVAuthorizationStatusDenied           OurAVAuthorizationStatusDenied
#  define AVAuthorizationStatusAuthorized       OurAVAuthorizationStatusAuthorized
# endif

/**
 * The authorization status enum.
 *
 * Starting macOS 10.14 we need to request permissions in order to use any audio input device
 * but as we build against an older SDK where this is not available we have to duplicate
 * AVAuthorizationStatus and do everything dynmically during runtime, sigh...
 */
typedef enum AVAuthorizationStatus
# if RT_CPLUSPLUS_PREREQ(201100)
    : NSInteger
# endif
{
    AVAuthorizationStatusNotDetermined = 0,
    AVAuthorizationStatusRestricted    = 1,
    AVAuthorizationStatusDenied        = 2,
    AVAuthorizationStatusAuthorized    = 3
} AVAuthorizationStatus;

#endif


#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1070 /** @todo need some better fix/whatever for AudioTest */

/**
 * Requests camera permissions for Mojave and onwards.
 *
 * @returns VBox status code.
 */
static int coreAudioInputPermissionRequest(void)
{
    __block RTSEMEVENT hEvt = NIL_RTSEMEVENT;
    __block int rc = RTSemEventCreate(&hEvt);
    if (RT_SUCCESS(rc))
    {
        /* Perform auth request. */
        [AVCaptureDevice performSelector: @selector(requestAccessForMediaType: completionHandler:)
                              withObject: (id)AVMediaTypeAudio
                              withObject: (id)^(BOOL granted)
        {
            if (!granted)
            {
                LogRel(("CoreAudio: Access denied!\n"));
                rc = VERR_ACCESS_DENIED;
            }
            RTSemEventSignal(hEvt);
        }];

        rc = RTSemEventWait(hEvt, RT_MS_10SEC);
        RTSemEventDestroy(hEvt);
    }

    return rc;
}

#endif

/**
 * Checks permission for capturing devices on Mojave and onwards.
 *
 * @returns VBox status code.
 */
DECLHIDDEN(int) coreAudioInputPermissionCheck(void)
{
    int rc = VINF_SUCCESS;

    if (NSFoundationVersionNumber >= 10.14)
    {
        /*
         * Because we build with an older SDK where the authorization APIs are not available
         * (introduced with Mojave 10.14) we have to resort to resolving the APIs dynamically.
         */
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1070 /** @todo need some better fix/whatever for AudioTest */
        LogRel(("CoreAudio: macOS 10.14+ detected, checking audio input permissions\n"));

        if ([AVCaptureDevice respondsToSelector:@selector(authorizationStatusForMediaType:)])
        {
            AVAuthorizationStatus enmAuthSts
                = (AVAuthorizationStatus)(NSInteger)[AVCaptureDevice performSelector: @selector(authorizationStatusForMediaType:)
                                                                          withObject: (id)AVMediaTypeAudio];
            if (enmAuthSts == AVAuthorizationStatusNotDetermined)
                rc = coreAudioInputPermissionRequest();
            else if (   enmAuthSts == AVAuthorizationStatusRestricted
                     || enmAuthSts == AVAuthorizationStatusDenied)
            {
                LogRel(("CoreAudio: Access denied!\n"));
                rc = VERR_ACCESS_DENIED;
            }
        }
#else
        LogRel(("CoreAudio: WARNING! macOS 10.14+ detected.  Audio input probably wont work as this app was compiled using a too old SDK.\n"));
#endif
    }

    return rc;
}
