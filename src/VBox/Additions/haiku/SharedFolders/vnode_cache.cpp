/* $Id: vnode_cache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared folders - Haiku Guest Additions, vnode cache header.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vboxsf.h"
#include "OpenHashTable.h"

struct HashTableDefinition
{
    typedef uint32       KeyType;
    typedef vboxsf_vnode ValueType;

    size_t HashKey(uint32 key) const
    {
        return key;
    }

    size_t Hash(vboxsf_vnode* value) const
    {
        return HashKey(value->vnode);
    }

    bool Compare(uint32 key, vboxsf_vnode* value) const
    {
        return value->vnode == key;
    }

    vboxsf_vnode*& GetLink(vboxsf_vnode* value) const
    {
        return value->next;
    }
};

static BOpenHashTable<HashTableDefinition> g_cache;
static ino_t g_nextVnid = 1;
mutex g_vnodeCacheLock;


extern "C" status_t vboxsf_new_vnode(PVBSFMAP map, PSHFLSTRING path, PSHFLSTRING name, vboxsf_vnode** p)
{
    vboxsf_vnode* vn = (vboxsf_vnode*)malloc(sizeof(vboxsf_vnode));
    if (vn == NULL)
        return B_NO_MEMORY;

    dprintf("creating new vnode at %p with path=%p (%s)\n", vn, path->String.utf8, path->String.utf8);
    vn->map = map;
    vn->path = path;
    if (name)
        vn->name = name;
    else
    {
        const char* cname = strrchr((char*)path->String.utf8, '/');
        if (!cname)
            vn->name = path; // no slash, assume this *is* the filename
        else
            vn->name = make_shflstring(cname);
    }

    if (mutex_lock(&g_vnodeCacheLock) < B_OK)
    {
        free(vn);
        return B_ERROR;
    }

    vn->vnode = g_nextVnid++;
    *p = vn;
    dprintf("vboxsf: allocated %p (path=%p name=%p)\n", vn, vn->path, vn->name);
    status_t rv = g_cache.Insert(vn);

    mutex_unlock(&g_vnodeCacheLock);

    return rv;
}


extern "C" status_t vboxsf_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode,
                                     int* _type, uint32* _flags, bool reenter)
{
    vboxsf_vnode* vn = g_cache.Lookup(id);
    if (vn)
    {
        vnode->private_node = vn;
        return B_OK;
    }
    return B_ERROR;
}


extern "C" status_t vboxsf_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter)
{
    g_cache.Remove((vboxsf_vnode*)vnode->private_node);
}

