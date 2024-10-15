/* $Id: sbuf.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - sbuf implemenation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>

/* Done as a macro in socket.h */
/* int
 * sbspace(struct sockbuff *sb)
 * {
 *      return SB_DATALEN - sb->sb_cc;
 * }
 */

void
sbfree(struct sbuf *sb)
{
    /*
     * Catch double frees. Actually tcp_close() already filters out listening sockets
     * passing NULL.
     */
    Assert((sb->sb_data));

    /*
     * Don't call RTMemFree() for an already freed buffer, the EFence could complain
     */
    if (sb->sb_data)
    {
        RTMemFree(sb->sb_data);
        sb->sb_data = NULL;
    }
}

void
sbdrop(struct sbuf *sb, int num)
{
    /*
     * We can only drop how much we have
     * This should never succeed
     */
    if (num > sb->sb_cc)
        num = sb->sb_cc;
    sb->sb_cc -= num;
    sb->sb_rptr += num;
    if (sb->sb_rptr >= sb->sb_data + sb->sb_datalen)
        sb->sb_rptr -= sb->sb_datalen;

}

void
sbreserve(PNATState pData, struct sbuf *sb, int size)
{
    NOREF(pData);
    if (sb->sb_data)
    {
        /* Already alloced, realloc if necessary */
        if (sb->sb_datalen != (u_int)size)
        {
            sb->sb_wptr =
            sb->sb_rptr =
            sb->sb_data = (char *)RTMemReallocZ(sb->sb_data, sb->sb_datalen, size);
            sb->sb_cc = 0;
            if (sb->sb_wptr)
                sb->sb_datalen = size;
            else
                sb->sb_datalen = 0;
        }
    }
    else
    {
        sb->sb_wptr = sb->sb_rptr = sb->sb_data = (char *)RTMemAllocZ(size);
        sb->sb_cc = 0;
        if (sb->sb_wptr)
            sb->sb_datalen = size;
        else
            sb->sb_datalen = 0;
    }
}

/*
 * Try and write() to the socket, whatever doesn't get written
 * append to the buffer... for a host with a fast net connection,
 * this prevents an unnecessary copy of the data
 * (the socket is non-blocking, so we won't hang)
 */
void
sbappend(PNATState pData, struct socket *so, struct mbuf *m)
{
    int ret = 0;
    int mlen = 0;

    STAM_PROFILE_START(&pData->StatIOSBAppend_pf, a);
    LogFlow(("sbappend: so = %p, m = %p, m->m_len = %d\n", so, m, m ? m->m_len : 0));

    STAM_COUNTER_INC(&pData->StatIOSBAppend);
    /* Shouldn't happen, but...  e.g. foreign host closes connection */
    mlen = m_length(m, NULL);
    if (mlen <= 0)
    {
        STAM_COUNTER_INC(&pData->StatIOSBAppend_zm);
        goto done;
    }

    /*
     * If there is urgent data, call sosendoob
     * if not all was sent, sowrite will take care of the rest
     * (The rest of this function is just an optimisation)
     */
    if (so->so_urgc)
    {
        sbappendsb(pData, &so->so_rcv, m);
        m_freem(pData, m);
        sosendoob(so);
        return;
    }

    /*
     * We only write if there's nothing in the buffer,
     * otherwise it'll arrive out of order, and hence corrupt
     */
    if (so->so_rcv.sb_cc == 0)
    {
        caddr_t buf = NULL;

        if (m->m_next)
        {
            buf = RTMemAllocZ(mlen);
            if (buf == NULL)
            {
                ret = 0;
                goto no_sent;
            }
            m_copydata(m, 0, mlen, buf);
        }
        else
            buf = mtod(m, char *);

        ret = send(so->s, buf, mlen, 0);

        if (m->m_next)
            RTMemFree(buf);
    }
no_sent:

    if (ret <= 0)
    {
        STAM_COUNTER_INC(&pData->StatIOSBAppend_wf);
        /*
         * Nothing was written
         * It's possible that the socket has closed, but
         * we don't need to check because if it has closed,
         * it will be detected in the normal way by soread()
         */
        sbappendsb(pData, &so->so_rcv, m);
        STAM_PROFILE_STOP(&pData->StatIOSBAppend_pf_wf, a);
        goto done;
    }
    else if (ret != mlen)
    {
        STAM_COUNTER_INC(&pData->StatIOSBAppend_wp);
        /*
         * Something was written, but not everything..
         * sbappendsb the rest
         */
        m_adj(m, ret);
        sbappendsb(pData, &so->so_rcv, m);
        STAM_PROFILE_STOP(&pData->StatIOSBAppend_pf_wp, a);
        goto done;
    } /* else */
    /* Whatever happened, we free the mbuf */
    STAM_COUNTER_INC(&pData->StatIOSBAppend_wa);
    STAM_PROFILE_STOP(&pData->StatIOSBAppend_pf_wa, a);
done:
    m_freem(pData, m);
}

/*
 * Copy the data from m into sb
 * The caller is responsible to make sure there's enough room
 */
void
sbappendsb(PNATState pData, struct sbuf *sb, struct mbuf *m)
{
    int len, n,  nn;
#ifndef VBOX_WITH_STATISTICS
    NOREF(pData);
#endif

    len = m_length(m, NULL);

    STAM_COUNTER_INC(&pData->StatIOSBAppendSB);
    if (sb->sb_wptr < sb->sb_rptr)
    {
        STAM_COUNTER_INC(&pData->StatIOSBAppendSB_w_l_r);
        n = sb->sb_rptr - sb->sb_wptr;
        if (n > len)
            n = len;
        m_copydata(m, 0, n, sb->sb_wptr);
    }
    else
    {
        STAM_COUNTER_INC(&pData->StatIOSBAppendSB_w_ge_r);
        /* Do the right edge first */
        n = sb->sb_data + sb->sb_datalen - sb->sb_wptr;
        if (n > len)
            n = len;
        m_copydata(m, 0, n, sb->sb_wptr);
        len -= n;
        if (len)
        {
            /* Now the left edge */
            nn = sb->sb_rptr - sb->sb_data;
            if (nn > len)
                nn = len;
            m_copydata(m, n, nn, sb->sb_data);
            n += nn;
        }
    }

    sb->sb_cc += n;
    sb->sb_wptr += n;
    if (sb->sb_wptr >= sb->sb_data + sb->sb_datalen)
    {
        STAM_COUNTER_INC(&pData->StatIOSBAppendSB_w_alter);
        sb->sb_wptr -= sb->sb_datalen;
    }
}

/*
 * Copy data from sbuf to a normal, straight buffer
 * Don't update the sbuf rptr, this will be
 * done in sbdrop when the data is acked
 */
void
sbcopy(struct sbuf *sb, int off, int len, char *to)
{
    char *from;

    from = sb->sb_rptr + off;
    if (from >= sb->sb_data + sb->sb_datalen)
        from -= sb->sb_datalen;

    if (from < sb->sb_wptr)
    {
        if (len > sb->sb_cc)
            len = sb->sb_cc;
        memcpy(to, from, len);
    }
    else
    {
        /* re-use off */
        off = (sb->sb_data + sb->sb_datalen) - from;
        if (off > len)
            off = len;
        memcpy(to, from, off);
        len -= off;
        if (len)
            memcpy(to+off, sb->sb_data, len);
    }
}
