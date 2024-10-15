/* $Id: hash.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*
 * Copyright (c) 2003,2004 Armin Wolfermann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef VBOX
#include <config.h>
#include "dnsproxy.h"

#define HASHSIZE 10
#define HASH(id) (id & ((1 << HASHSIZE) - 1))

static struct request *request_hash[1 << HASHSIZE];
#else /* VBOX */
# include "slirp.h"
#endif

void
hash_add_request(PNATState pData, struct request *req)
{
    struct request **p = &request_hash[HASH(req->id)];
    Log2(("NAT: hash req id %d has been added \n", req->id));

    if ((req->next = *p) != NULL) {
        (*p)->prev = &req->next;
        ++hash_collisions;
    }
    *p = req;
    req->prev = p;

    ++active_queries;
}

void
hash_remove_request(PNATState pData, struct request *req)
{
    if (!req->prev) return;
    if (req->next)
        req->next->prev = req->prev;
    *req->prev = req->next;
    req->prev = NULL;

    --active_queries;
}

struct request *
hash_find_request(PNATState pData, unsigned short id)
{
    struct request *req = request_hash[HASH(id)];
    Log2(("NAT: hash try to find req by id %d \n", id));

    for (;;) {
        if (!req) break;
        if (req->id == id) break;
        req = req->next;
    }

    return req;
}
