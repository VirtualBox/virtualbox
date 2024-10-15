/* $Id: DataStreamImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - DataStream
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_DATASTREAM
#include "DataStreamImpl.h"

#include "AutoCaller.h"
#include "LoggingNew.h"
#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Boilerplate constructor & destructor                                                                                         *
*********************************************************************************************************************************/

DEFINE_EMPTY_CTOR_DTOR(DataStream)

HRESULT DataStream::FinalConstruct()
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void DataStream::FinalRelease()
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}


/*********************************************************************************************************************************
*   Initializer & uninitializer                                                                                                  *
*********************************************************************************************************************************/

/**
 * Initializes the DataStream object.
 *
 * @param   aBufferSize     Size of the intermediate buffer.
 *
 */
HRESULT DataStream::init(unsigned long aBufferSize)
{
    LogFlowThisFunc(("cbBuffer=%zu\n", aBufferSize));

    /*
     * Enclose the state transition NotReady->InInit->Ready
     */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /*
     * Allocate data instance.
     */
    HRESULT hrc = S_OK;

    m_hSemEvtDataAvail   = NIL_RTSEMEVENT;
    m_hSemEvtBufSpcAvail = NIL_RTSEMEVENT;
    m_pBuffer            = NULL;
    m_fEos               = false;
    int vrc = RTSemEventCreate(&m_hSemEvtDataAvail);
    if (RT_SUCCESS(vrc))
        vrc = RTSemEventCreate(&m_hSemEvtBufSpcAvail);
    if (RT_SUCCESS(vrc))
        vrc = RTCircBufCreate(&m_pBuffer, aBufferSize);

    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to initialize data stream object (%Rrc)"), vrc);

    /*
     * Done. Just update object readiness state.
     */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed(hrc);

    LogFlowThisFunc(("returns %Rhrc\n", hrc));
    return hrc;
}

/**
 * Uninitializes the instance (called from FinalRelease()).
 */
void DataStream::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (!autoUninitSpan.uninitDone())
    {
        if (m_hSemEvtDataAvail != NIL_RTSEMEVENT)
            RTSemEventDestroy(m_hSemEvtDataAvail);
        if (m_hSemEvtBufSpcAvail != NIL_RTSEMEVENT)
            RTSemEventDestroy(m_hSemEvtBufSpcAvail);
        if (m_pBuffer != NULL)
            RTCircBufDestroy(m_pBuffer);
        m_hSemEvtDataAvail = NIL_RTSEMEVENT;
        m_hSemEvtBufSpcAvail = NIL_RTSEMEVENT;
    }

    LogFlowThisFuncLeave();
}


/*********************************************************************************************************************************
*   IDataStream attributes                                                                                                       *
*********************************************************************************************************************************/

HRESULT DataStream::getReadSize(ULONG *aReadSize)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aReadSize = (ULONG)RTCircBufUsed(m_pBuffer);
    return S_OK;
}


/*********************************************************************************************************************************
*   IDataStream methods                                                                                                          *
*********************************************************************************************************************************/

HRESULT DataStream::read(ULONG aSize, ULONG aTimeoutMS, std::vector<BYTE> &aData)
{
    /*
     * Allocate return buffer.
     */
    try
    {
        aData.resize(aSize);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Do the reading.  To play safe we exclusivly lock the object while doing this.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;
    while (   !RTCircBufUsed(m_pBuffer)
           && !m_fEos
           && RT_SUCCESS(vrc))
    {
        /* Wait for something to become available. */
        alock.release();
        vrc = RTSemEventWait(m_hSemEvtDataAvail, aTimeoutMS == 0 ? RT_INDEFINITE_WAIT : aTimeoutMS);
        alock.acquire();
    }

    /*
     * Manage the result.
     */
    HRESULT hrc = S_OK;
    if (   RT_SUCCESS(vrc)
        && RTCircBufUsed(m_pBuffer))
    {

        size_t off = 0;
        size_t cbCopy = RT_MIN(aSize, RTCircBufUsed(m_pBuffer));
        if (cbCopy != aSize)
        {
            Assert(cbCopy < aSize);
            aData.resize(cbCopy);
        }

        while (cbCopy)
        {
            void *pvSrc = NULL;
            size_t cbThisCopy = 0;

            RTCircBufAcquireReadBlock(m_pBuffer, cbCopy, &pvSrc, &cbThisCopy);
            memcpy(&aData.front() + off, pvSrc, cbThisCopy);
            RTCircBufReleaseReadBlock(m_pBuffer, cbThisCopy);

            cbCopy -= cbThisCopy;
            off    += cbThisCopy;
        }
        vrc = RTSemEventSignal(m_hSemEvtBufSpcAvail);
        AssertRC(vrc);
    }
    else
    {
        Assert(   RT_FAILURE(vrc)
               || (   m_fEos
                   && !RTCircBufUsed(m_pBuffer)));

        aData.resize(0);
        if (vrc == VERR_TIMEOUT)
            hrc = VBOX_E_TIMEOUT;
        else if (RT_FAILURE(vrc))
            hrc = setErrorBoth(E_FAIL, vrc, tr("Error reading %u bytes: %Rrc", "", aSize), aSize, vrc);
    }

    return hrc;
}


/*********************************************************************************************************************************
*   DataStream internal methods                                                                                                  *
*********************************************************************************************************************************/

/**
 * Writes the given data into the temporary buffer blocking if it is full.
 *
 * @returns IPRT status code.
 * @param   pvBuf           The data to write.
 * @param   cbWrite         How much to write.
 * @param   pcbWritten      Where to store the amount of data written.
 */
int DataStream::i_write(const void *pvBuf, size_t cbWrite, size_t *pcbWritten)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(!m_fEos, VERR_INVALID_STATE);

    *pcbWritten = 0;

    int vrc = VINF_SUCCESS;
    while (   !RTCircBufFree(m_pBuffer)
           && RT_SUCCESS(vrc))
    {
        /* Wait for space to become available. */
        alock.release();
        vrc = RTSemEventWait(m_hSemEvtBufSpcAvail, RT_INDEFINITE_WAIT);
        alock.acquire();
    }

    if (RT_SUCCESS(vrc))
    {
        const uint8_t *pbBuf = (const uint8_t *)pvBuf;
        size_t cbCopy = RT_MIN(cbWrite, RTCircBufFree(m_pBuffer));

        *pcbWritten = cbCopy;

        while (cbCopy)
        {
            void *pvDst = NULL;
            size_t cbThisCopy = 0;

            RTCircBufAcquireWriteBlock(m_pBuffer, cbCopy, &pvDst, &cbThisCopy);
            memcpy(pvDst, pbBuf, cbThisCopy);
            RTCircBufReleaseWriteBlock(m_pBuffer, cbThisCopy);

            cbCopy -= cbThisCopy;
            pbBuf  += cbThisCopy;
        }

        RTSemEventSignal(m_hSemEvtDataAvail);
    }

    return vrc;
}

/**
 * Marks the end of the stream.
 *
 * @returns IPRT status code.
 */
int DataStream::i_close()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m_fEos = true;
    RTSemEventSignal(m_hSemEvtDataAvail);
    return VINF_SUCCESS;
}

