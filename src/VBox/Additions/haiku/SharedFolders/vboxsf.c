/* $Id: vboxsf.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared folders - Haiku Guest Additions, implementation.
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

#define MODULE_NAME     "file_systems/vboxsf"
#define FS_NAME         "vboxsf"
#define FS_PRETTY_NAME  "VirtualBox Shared Folders"

VBGLSFCLIENT g_clientHandle;
static fs_volume_ops vboxsf_volume_ops;
static fs_vnode_ops vboxsf_vnode_ops;

status_t init_module(void)
{
#if 0
    /** @todo enable this soon */
    int rc = get_module(VBOXGUEST_MODULE_NAME, (module_info **)&g_VBoxGuest);
    if (RT_LIKELY(rc == B_OK)
    {
        rc = VbglR0SfInit();
        if (RT_SUCCESS(rc))
        {
            rc = VbglR0SfConnect(&g_clientHandle);
            if (RT_SUCCESS(rc))
            {
                rc = VbglR0SfSetUtf8(&g_clientHandle);
                if (RT_SUCCESS(rc))
                {
                    rc = VbglR0SfSetSymlinks(&g_clientHandle);
                    if (RT_FAILURE(rc))
                        LogRel((FS_NAME ": Warning! VbglR0SfSetSymlinks failed (rc=%d) - symlink will appear as copies.\n", rc));

                    mutex_init(&g_vnodeCacheLock, "vboxsf vnode cache lock");
                    Log((FS_NAME ": init_module succeeded.\n");
                    return B_OK;
                }
                else
                    LogRel((FS_NAME ": VbglR0SfSetUtf8 failed. rc=%d\n", rc));
            }
            else
                LogRel((FS_NAME ": VbglR0SfConnect failed. rc=%d\n", rc));
        }
        else
            LogRel((FS_NAME ": VbglR0SfInit failed. rc=%d\n", rc));
    }
    else
        LogRel((FS_NAME ": get_module failed for '%s'. rc=%d\n", VBOXGUEST_MODULE_NAME, rc));

    return B_ERROR;
#endif

    if (get_module(VBOXGUEST_MODULE_NAME, (module_info **)&g_VBoxGuest) != B_OK)
    {
        dprintf("get_module(%s) failed\n", VBOXGUEST_MODULE_NAME);
        return B_ERROR;
    }

    if (RT_FAILURE(VbglR0SfInit()))
    {
        dprintf("VbglR0SfInit failed\n");
        return B_ERROR;
    }

    if (RT_FAILURE(VbglR0SfConnect(&g_clientHandle)))
    {
        dprintf("VbglR0SfConnect failed\n");
        return B_ERROR;
    }

    if (RT_FAILURE(VbglR0SfSetUtf8(&g_clientHandle)))
    {
        dprintf("VbglR0SfSetUtf8 failed\n");
        return B_ERROR;
    }

    if (RT_FAILURE(VbglR0SfSetSymlinks(&g_clientHandle)))
    {
        dprintf("warning: VbglR0SfSetSymlinks failed (old vbox?) - symlinks will appear as copies\n");
    }

    mutex_init(&g_vnodeCacheLock, "vboxsf vnode cache lock");

    dprintf(FS_NAME ": inited successfully\n");
    return B_OK;
}

void uninit_module(void)
{
    mutex_destroy(&g_vnodeCacheLock);
    put_module(VBOXGUEST_MODULE_NAME);
}


PSHFLSTRING make_shflstring(const char* const s)
{
    int len = strlen(s);
    if (len > 0xFFFE)
    {
        dprintf(FS_NAME ": make_shflstring: string too long\n");
        return NULL;
    }

    PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + len);
    if (!rv)
        return NULL;

    rv->u16Length = len;
    rv->u16Size = len + 1;
    strcpy(rv->String.utf8, s);
    return rv;
}


PSHFLSTRING clone_shflstring(PSHFLSTRING s)
{
    PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + s->u16Length);
    if (rv)
        memcpy(rv, s, sizeof(SHFLSTRING) + s->u16Length);
    return rv;
}

PSHFLSTRING concat_shflstring_cstr(PSHFLSTRING s1, const char* const s2)
{
    size_t s2len = strlen(s2);
    PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + s1->u16Length + s2len);
    if (rv)
    {
        memcpy(rv, s1, sizeof(SHFLSTRING) + s1->u16Length);
        strcat(rv->String.utf8, s2);
        rv->u16Length += s2len;
        rv->u16Size += s2len;
    }
    return rv;
}


PSHFLSTRING concat_cstr_shflstring(const char* const s1, PSHFLSTRING s2)
{
    size_t s1len = strlen(s1);
    PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + s1len + s2->u16Length);
    if (rv)
    {
        strcpy(rv->String.utf8, s1);
        strcat(rv->String.utf8, s2->String.utf8);
        rv->u16Length = s1len + s2->u16Length;
        rv->u16Size = rv->u16Length + 1;
    }
    return rv;
}


PSHFLSTRING build_path(vboxsf_vnode* dir, const char* const name)
{
    dprintf("*** build_path(%p, %p)\n", dir, name);
    if (!dir || !name)
        return NULL;

    size_t len = dir->path->u16Length + strlen(name) + 1;
    PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + len);
    if (rv)
    {
        strcpy(rv->String.utf8, dir->path->String.utf8);
        strcat(rv->String.utf8, "/");
        strcat(rv->String.utf8, name);
        rv->u16Length = len;
        rv->u16Size = rv->u16Length + 1;
    }
    return rv;
}


status_t mount(fs_volume *volume, const char *device, uint32 flags, const char *args, ino_t *_rootVnodeID)
{
    if (device)
    {
        dprintf(FS_NAME ": trying to mount a real device as a vbox share is silly\n");
        return B_BAD_TYPE;
    }

    dprintf(FS_NAME ": mount(%s)\n", args);

    PSHFLSTRING sharename = make_shflstring(args);

    vboxsf_volume* vbsfvolume = malloc(sizeof(vboxsf_volume));
    volume->private_volume = vbsfvolume;
    int rv = VbglR0SfMapFolder(&g_clientHandle, sharename, &(vbsfvolume->map));
    free(sharename);

    if (rv == 0)
    {
        vboxsf_vnode* root_vnode;

        PSHFLSTRING name = make_shflstring("");
        if (!name)
        {
            dprintf(FS_NAME ": make_shflstring() failed\n");
            return B_NO_MEMORY;
        }

        status_t rs = vboxsf_new_vnode(&vbsfvolume->map, name, name, &root_vnode);
        dprintf(FS_NAME ": allocated %p (path=%p name=%p)\n", root_vnode, root_vnode->path, root_vnode->name);

        if (rs != B_OK)
        {
            dprintf(FS_NAME ": vboxsf_new_vnode() failed (%d)\n", (int)rs);
            return rs;
        }

        rs = publish_vnode(volume, root_vnode->vnode, root_vnode, &vboxsf_vnode_ops, S_IFDIR, 0);
        dprintf(FS_NAME ": publish_vnode(): %d\n", (int)rs);
        *_rootVnodeID = root_vnode->vnode;
        volume->ops = &vboxsf_volume_ops;
        return B_OK;
    }
    else
    {
        dprintf(FS_NAME ": VbglR0SfMapFolder failed (%d)\n", rv);
        free(volume->private_volume);
        return vbox_err_to_haiku_err(rv);
    }
}


status_t unmount(fs_volume *volume)
{
    dprintf(FS_NAME ": unmount\n");
    VbglR0SfUnmapFolder(&g_clientHandle, volume->private_volume);
    return B_OK;
}


status_t vboxsf_read_stat(fs_volume* _volume, fs_vnode* _vnode, struct stat* st)
{
    vboxsf_vnode* vnode = _vnode->private_node;
    vboxsf_volume* volume = _volume->private_volume;
    SHFLCREATEPARMS params;
    int rc;

    dprintf("vboxsf_read_stat (_vnode=%p, vnode=%p, path=%p (%s))\n", _vnode, vnode, vnode->path->String.utf8, vnode->path->String.utf8);

    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;
    dprintf("sf_stat: calling VbglR0SfCreate, file %s, flags %x\n", vnode->path->String.utf8, params.CreateFlags);
    rc = VbglR0SfCreate(&g_clientHandle, &volume->map, vnode->path, &params);
    if (rc == VERR_INVALID_NAME)
    {
        /* this can happen for names like 'foo*' on a Windows host */
        return B_ENTRY_NOT_FOUND;
    }
    if (RT_FAILURE(rc))
    {
        dprintf("VbglR0SfCreate: %d\n", params.Result);
        return vbox_err_to_haiku_err(params.Result);
    }
    if (params.Result != SHFL_FILE_EXISTS)
    {
        dprintf("VbglR0SfCreate: %d\n", params.Result);
        return B_ENTRY_NOT_FOUND;
    }

    st->st_dev     = 0;
    st->st_ino     = vnode->vnode;
    st->st_mode    = mode_from_fmode(params.Info.Attr.fMode);
    st->st_nlink   = 1;
    st->st_uid     = 0;
    st->st_gid     = 0;
    st->st_rdev    = 0;
    st->st_size    = params.Info.cbObject;
    st->st_blksize = 1;
    st->st_blocks  = params.Info.cbAllocated;
    st->st_atime   = RTTimeSpecGetSeconds(&params.Info.AccessTime);
    st->st_mtime   = RTTimeSpecGetSeconds(&params.Info.ModificationTime);
    st->st_ctime   = RTTimeSpecGetSeconds(&params.Info.BirthTime);
    return B_OK;
}


status_t vboxsf_open_dir(fs_volume* _volume, fs_vnode* _vnode, void** _cookie)
{
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_vnode* vnode = _vnode->private_node;
    SHFLCREATEPARMS params;

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;

    int rc = VbglR0SfCreate(&g_clientHandle, &volume->map, vnode->path, &params);
    if (RT_SUCCESS(rc))
    {
        if (params.Result == SHFL_FILE_EXISTS && params.Handle != SHFL_HANDLE_NIL)
        {
            vboxsf_dir_cookie* cookie = malloc(sizeof(vboxsf_dir_cookie));
            *_cookie = cookie;
            cookie->index = 0;
            cookie->path = build_path(vnode, "*");
            cookie->handle = params.Handle;
            cookie->has_more_files = true;
            cookie->buffer_start = cookie->buffer = NULL;
            cookie->buffer_length = cookie->num_files = 0;
            return B_OK;
        }
        else
            return B_ENTRY_NOT_FOUND;
    }
    else
    {
        dprintf(FS_NAME ": VbglR0SfCreate: %d\n", rc);
        return vbox_err_to_haiku_err(rc);
    }
}


/** read a single entry from a dir */
status_t vboxsf_read_dir_1(vboxsf_volume* volume, vboxsf_vnode* vnode, vboxsf_dir_cookie* cookie,
    struct dirent* buffer, size_t bufferSize)
{
    dprintf("%p, %d, %p\n", cookie, cookie->has_more_files, cookie->buffer);
    if (!cookie->has_more_files)
        return B_ENTRY_NOT_FOUND;

    if (!cookie->buffer)
    {
        cookie->buffer_length = 16384;
        cookie->buffer_start = cookie->buffer = malloc(cookie->buffer_length);

        int rc = VbglR0SfDirInfo(&g_clientHandle, &volume->map, cookie->handle, cookie->path, 0, cookie->index,
                                 &cookie->buffer_length, cookie->buffer, &cookie->num_files);

        if (rc != 0 && rc != VERR_NO_MORE_FILES)
        {
            dprintf(FS_NAME ": VbglR0SfDirInfo failed: %d\n", rc);
            free(cookie->buffer_start);
            cookie->buffer_start = NULL;
            return vbox_err_to_haiku_err(rc);
        }

        if (rc == VERR_NO_MORE_FILES)
        {
            free(cookie->buffer_start);
            cookie->buffer_start = NULL;
            cookie->has_more_files = false;
            return B_ENTRY_NOT_FOUND;
        }
    }

    if (bufferSize <= sizeof(struct dirent) + cookie->buffer->name.u16Length)
    {
        dprintf("hit end of buffer\n");
        return B_BUFFER_OVERFLOW;
    }

    PSHFLSTRING name1 = clone_shflstring(&cookie->buffer->name);
    if (!name1)
    {
        dprintf(FS_NAME ": make_shflstring() failed\n");
        return B_NO_MEMORY;
    }

    vboxsf_vnode* new_vnode;
    int rv = vboxsf_new_vnode(&volume->map, build_path(vnode, name1->String.utf8), name1, &new_vnode);
    if (rv != B_OK)
    {
        dprintf(FS_NAME ": vboxsf_new_vnode() failed\n");
        return rv;
    }

    buffer->d_dev = 0;
    buffer->d_pdev = 0;
    buffer->d_ino = new_vnode->vnode;
    buffer->d_pino = vnode->vnode;
    buffer->d_reclen = sizeof(struct dirent) + cookie->buffer->name.u16Length;
    strncpy(buffer->d_name, cookie->buffer->name.String.utf8, NAME_MAX);

    size_t size = offsetof(SHFLDIRINFO, name.String) + cookie->buffer->name.u16Size;
    cookie->buffer = ((void*)cookie->buffer + size);
    cookie->index++;

    if (cookie->index >= cookie->num_files)
    {
        // hit end of this buffer, next call will reallocate a new one
        free(cookie->buffer_start);
        cookie->buffer_start = cookie->buffer = NULL;
    }
    return B_OK;
}


status_t vboxsf_read_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
    struct dirent* buffer, size_t bufferSize, uint32* _num)
{
    vboxsf_dir_cookie* cookie = _cookie;
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_vnode* vnode = _vnode->private_node;
    uint32 num_read = 0;
    status_t rv = B_OK;

    for (num_read = 0; num_read < *_num && cookie->has_more_files; num_read++)
    {
        rv = vboxsf_read_dir_1(volume, vnode, cookie, buffer, bufferSize);
        if (rv == B_BUFFER_OVERFLOW || rv == B_ENTRY_NOT_FOUND)
        {
            // hit end of at least one of the buffers - not really an error
            rv = B_OK;
            break;
        }
        bufferSize -= buffer->d_reclen;
        buffer = ((void*)(buffer)) + buffer->d_reclen;
    }

    *_num = num_read;
    return rv;
}


status_t vboxsf_free_dir_cookie(fs_volume* _volume, fs_vnode* vnode, void* _cookie)
{
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_dir_cookie* cookie = _cookie;

    VbglR0SfClose(&g_clientHandle, &volume->map, cookie->handle);
    free(cookie->path);
    free(cookie);

    return B_OK;
}


status_t vboxsf_read_fs_info(fs_volume* _volume, struct fs_info* info)
{
    vboxsf_volume* volume = _volume->private_volume;

    SHFLVOLINFO volume_info;
    uint32_t bytes = sizeof(SHFLVOLINFO);

    int rc = VbglR0SfFsInfo(&g_clientHandle, &volume->map, 0, SHFL_INFO_GET | SHFL_INFO_VOLUME,
                            &bytes, (PSHFLDIRINFO)&volume_info);
    if (RT_FAILURE(rc))
    {
        dprintf(FS_NAME ": VbglR0SfFsInfo failed (%d)\n", rc);
        return vbox_err_to_haiku_err(rc);
    }

    info->flags = B_FS_IS_PERSISTENT;
    if (volume_info.fsProperties.fReadOnly)
        info->flags |= B_FS_IS_READONLY;

    info->dev          = 0;
    info->root         = 1;
    info->block_size   = volume_info.ulBytesPerAllocationUnit;
    info->io_size      = volume_info.ulBytesPerAllocationUnit;
    info->total_blocks = volume_info.ullTotalAllocationBytes / info->block_size;
    info->free_blocks  = volume_info.ullAvailableAllocationBytes / info->block_size;
    info->total_nodes  = LONGLONG_MAX;
    info->free_nodes   = LONGLONG_MAX;
    strcpy(info->volume_name, "VBox share");
    return B_OK;
}


status_t vboxsf_lookup(fs_volume* _volume, fs_vnode* dir, const char* name, ino_t* _id)
{
    dprintf(FS_NAME ": lookup %s\n", name);
    vboxsf_volume* volume = _volume->private_volume;
    SHFLCREATEPARMS params;

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;

    PSHFLSTRING path = build_path(dir->private_node, name);
    if (!path)
    {
        dprintf(FS_NAME ": make_shflstring() failed\n");
        return B_NO_MEMORY;
    }

    int rc = VbglR0SfCreate(&g_clientHandle, &volume->map, path, &params);
    if (RT_SUCCESS(rc))
    {
        if (params.Result == SHFL_FILE_EXISTS)
        {
            vboxsf_vnode* vn;
            status_t rv = vboxsf_new_vnode(&volume->map, path, path, &vn);
            if (rv == B_OK)
            {
                *_id = vn->vnode;
                rv = publish_vnode(_volume, vn->vnode, vn, &vboxsf_vnode_ops, mode_from_fmode(params.Info.Attr.fMode), 0);
            }
            return rv;
        }
        else
        {
            free(path);
            return B_ENTRY_NOT_FOUND;
        }
    }
    else
    {
        free(path);
        dprintf(FS_NAME ": VbglR0SfCreate: %d\n", rc);
        return vbox_err_to_haiku_err(rc);
    }
}


mode_t mode_from_fmode(RTFMODE fMode)
{
    mode_t m = 0;

    if (RTFS_IS_DIRECTORY(fMode))
        m |= S_IFDIR;
    else if (RTFS_IS_FILE(fMode))
        m |= S_IFREG;
    else if (RTFS_IS_FIFO(fMode))
        m |= S_IFIFO;
    else if (RTFS_IS_DEV_CHAR(fMode))
        m |= S_IFCHR;
    else if (RTFS_IS_DEV_BLOCK(fMode))
        m |= S_IFBLK;
    else if (RTFS_IS_SYMLINK(fMode))
        m |= S_IFLNK;
    else if (RTFS_IS_SOCKET(fMode))
        m |= S_IFSOCK;

    if (fMode & RTFS_UNIX_IRUSR)
        m |= S_IRUSR;
    if (fMode & RTFS_UNIX_IWUSR)
        m |= S_IWUSR;
    if (fMode & RTFS_UNIX_IXUSR)
        m |= S_IXUSR;
    if (fMode & RTFS_UNIX_IRGRP)
        m |= S_IRGRP;
    if (fMode & RTFS_UNIX_IWGRP)
        m |= S_IWGRP;
    if (fMode & RTFS_UNIX_IXGRP)
        m |= S_IXGRP;
    if (fMode & RTFS_UNIX_IROTH)
        m |= S_IROTH;
    if (fMode & RTFS_UNIX_IWOTH)
        m |= S_IWOTH;
    if (fMode & RTFS_UNIX_IXOTH)
        m |= S_IXOTH;
    if (fMode & RTFS_UNIX_ISUID)
        m |= S_ISUID;
    if (fMode & RTFS_UNIX_ISGID)
        m |= S_ISGID;
    if (fMode & RTFS_UNIX_ISTXT)
        m |= S_ISVTX;

    return m;
}


status_t vboxsf_open(fs_volume* _volume, fs_vnode* _vnode, int openMode, void** _cookie)
{
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_vnode* vnode = _vnode->private_node;

    dprintf(FS_NAME ": open %s (mode=%x)\n", vnode->path->String.utf8, openMode);

    SHFLCREATEPARMS params;

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;

    if (openMode & O_RDWR)
        params.CreateFlags |= SHFL_CF_ACCESS_READWRITE;
    else if (openMode & O_RDONLY)
        params.CreateFlags |= SHFL_CF_ACCESS_READ;
    else if (openMode & O_WRONLY)
        params.CreateFlags |= SHFL_CF_ACCESS_WRITE;

    if (openMode & O_APPEND)
        params.CreateFlags |= SHFL_CF_ACCESS_APPEND;

    if (openMode & O_CREAT)
    {
        params.CreateFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
        if (openMode & O_EXCL)
            params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS;
        else if (openMode & O_TRUNC)
            params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        else
            params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
    }
    else
    {
        params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
        if (openMode & O_TRUNC)
            params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        else
            params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
    }

    int rc = VbglR0SfCreate(&g_clientHandle, &volume->map, vnode->path, &params);
    if (!RT_SUCCESS(rc))
    {
        dprintf("VbglR0SfCreate returned %d\n", rc);
        return vbox_err_to_haiku_err(rc);
    }

    vboxsf_file_cookie* cookie = malloc(sizeof(vboxsf_file_cookie));
    if (!cookie)
    {
        dprintf("couldn't allocate file cookie\n");
        return B_NO_MEMORY;
    }

    cookie->handle = params.Handle;
    cookie->path = vnode->path;

    *_cookie = cookie;

    return B_OK;
}


status_t vboxsf_create(fs_volume* _volume, fs_vnode* _dir, const char *name, int openMode, int perms, void **_cookie, ino_t *_newVnodeID)
{
    vboxsf_volume* volume = _volume->private_volume;

    SHFLCREATEPARMS params;

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;

    if (openMode & O_RDWR)
        params.CreateFlags |= SHFL_CF_ACCESS_READWRITE;
    else if (openMode & O_RDONLY)
        params.CreateFlags |= SHFL_CF_ACCESS_READ;
    else if (openMode & O_WRONLY)
        params.CreateFlags |= SHFL_CF_ACCESS_WRITE;

    if (openMode & O_APPEND)
        params.CreateFlags |= SHFL_CF_ACCESS_APPEND;

    if (openMode & O_CREAT)
    {
        params.CreateFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
        if (openMode & O_EXCL)
            params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS;
        else if (openMode & O_TRUNC)
            params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        else
            params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
    }
    else
    {
        params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
        if (openMode & O_TRUNC)
            params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        else
            params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
    }

    PSHFLSTRING path = build_path(_dir->private_node, name);
    int rc = VbglR0SfCreate(&g_clientHandle, &volume->map, path, &params);

    if (!RT_SUCCESS(rc))
    {
        dprintf("VbglR0SfCreate returned %d\n", rc);
        free(path);
        return vbox_err_to_haiku_err(rc);
    }

    vboxsf_file_cookie* cookie = malloc(sizeof(vboxsf_file_cookie));
    if (!cookie)
    {
        dprintf("couldn't allocate file cookie\n");
        free(path);
        return B_NO_MEMORY;
    }

    cookie->handle = params.Handle;
    cookie->path = path;

    *_cookie = cookie;
    return vboxsf_lookup(_volume, _dir, name, _newVnodeID);
}


status_t vboxsf_close(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_file_cookie* cookie = _cookie;

    int rc = VbglR0SfClose(&g_clientHandle, &volume->map, cookie->handle);
    dprintf("VbglR0SfClose returned %d\n", rc);
    return vbox_err_to_haiku_err(rc);
}


status_t vboxsf_rewind_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
    vboxsf_dir_cookie* cookie = _cookie;
    cookie->index = 0;
    return B_OK;
}


status_t vboxsf_close_dir(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
    return B_OK;
}


status_t vboxsf_free_cookie(fs_volume *volume, fs_vnode *vnode, void *_cookie)
{
    vboxsf_dir_cookie* cookie = _cookie;
    free(cookie);
    return B_OK;
}

status_t vboxsf_read(fs_volume* _volume, fs_vnode* _vnode, void* _cookie, off_t pos, void *buffer, size_t *length)
{
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_vnode* vnode = _vnode->private_node;
    vboxsf_file_cookie* cookie = _cookie;

    if (*length > 0xFFFFFFFF)
        *length = 0xFFFFFFFF;

    uint32_t l = *length;
    void* other_buffer = malloc(l);  /** @todo map the user memory into kernel space here for efficiency */
    int rc = VbglR0SfRead(&g_clientHandle, &volume->map, cookie->handle, pos, &l, other_buffer, false /*fLocked*/);
    memcpy(buffer, other_buffer, l);
    free(other_buffer);

    dprintf("VbglR0SfRead returned %d\n", rc);
    *length = l;
    return vbox_err_to_haiku_err(rc);
}


status_t vboxsf_write(fs_volume* _volume, fs_vnode* _vnode, void* _cookie, off_t pos, const void *buffer, size_t *length)
{
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_vnode* vnode = _vnode->private_node;
    vboxsf_file_cookie* cookie = _cookie;

    if (*length > 0xFFFFFFFF)
        *length = 0xFFFFFFFF;

    uint32_t l = *length;
    void* other_buffer = malloc(l);  /** @todo map the user memory into kernel space here for efficiency */
    memcpy(other_buffer, buffer, l);
    int rc = VbglR0SfWrite(&g_clientHandle, &volume->map, cookie->handle, pos, &l, other_buffer, false /*fLocked*/);
    free(other_buffer);

    *length = l;
    return vbox_err_to_haiku_err(rc);
}


status_t vboxsf_write_stat(fs_volume *volume, fs_vnode *vnode, const struct stat *stat, uint32 statMask)
{
    /* The host handles updating the stat info - in the guest, this is a no-op */
    return B_OK;
}


status_t vboxsf_create_dir(fs_volume *_volume, fs_vnode *parent, const char *name, int perms)
{
    vboxsf_volume* volume = _volume->private_volume;

    SHFLCREATEPARMS params;
    params.Handle = 0;
    params.Info.cbObject = 0;
    params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_CREATE_IF_NEW |
        SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACCESS_READ;

    PSHFLSTRING path = build_path(parent->private_node, name);
    int rc = VbglR0SfCreate(&g_clientHandle, &volume->map, path, &params);
    free(path);
    /** @todo r=ramshankar: we should perhaps also check rc here and change
     *        Handle initialization from 0 to SHFL_HANDLE_NIL. */
    if (params.Handle == SHFL_HANDLE_NIL)
        return vbox_err_to_haiku_err(rc);
    VbglR0SfClose(&g_clientHandle, &volume->map, params.Handle);
    return B_OK;
}


status_t vboxsf_remove_dir(fs_volume *_volume, fs_vnode *parent, const char *name)
{
    vboxsf_volume* volume = _volume->private_volume;

    PSHFLSTRING path = build_path(parent->private_node, name);
    int rc = VbglR0SfRemove(&g_clientHandle, &volume->map, path, SHFL_REMOVE_DIR);
    free(path);

    return vbox_err_to_haiku_err(rc);
}


status_t vboxsf_unlink(fs_volume *_volume, fs_vnode *parent, const char *name)
{
    vboxsf_volume* volume = _volume->private_volume;

    PSHFLSTRING path = build_path(parent->private_node, name);
    int rc = VbglR0SfRemove(&g_clientHandle, &volume->map, path, SHFL_REMOVE_FILE);
    free(path);

    return vbox_err_to_haiku_err(rc);
}

status_t vboxsf_link(fs_volume *volume, fs_vnode *dir, const char *name, fs_vnode *vnode)
{
    return B_UNSUPPORTED;
}


status_t vboxsf_rename(fs_volume* _volume, fs_vnode* fromDir, const char* fromName, fs_vnode* toDir, const char* toName)
{
    vboxsf_volume* volume = _volume->private_volume;

    PSHFLSTRING oldpath = build_path(fromDir->private_node, fromName);
    PSHFLSTRING newpath = build_path(toDir->private_node, toName);
    int rc = VbglR0SfRename(&g_clientHandle, &volume->map, oldpath, newpath, SHFL_RENAME_FILE | SHFL_RENAME_REPLACE_IF_EXISTS);
    free(oldpath);
    free(newpath);

    return vbox_err_to_haiku_err(rc);
}


status_t vboxsf_create_symlink(fs_volume* _volume, fs_vnode* dir, const char* name, const char* path, int mode)
{
    vboxsf_volume* volume = _volume->private_volume;

    PSHFLSTRING target = make_shflstring(path);
    PSHFLSTRING linkpath = build_path(dir->private_node, name);
    SHFLFSOBJINFO stuff;
    RT_ZERO(stuff);

    int rc = VbglR0SfSymlink(&g_clientHandle, &volume->map, linkpath, target, &stuff);

    free(target);
    free(linkpath);

    return vbox_err_to_haiku_err(rc);
}


status_t vboxsf_read_symlink(fs_volume* _volume, fs_vnode* link, char* buffer, size_t* _bufferSize)
{
    vboxsf_volume* volume = _volume->private_volume;
    vboxsf_vnode* vnode = link->private_node;

    int rc = VbglR0SfReadLink(&g_clientHandle, &volume->map, vnode->path, *_bufferSize, buffer);
    *_bufferSize = strlen(buffer);

    return vbox_err_to_haiku_err(rc);
}


/** @todo move this into the runtime */
status_t vbox_err_to_haiku_err(int rc)
{
    switch (rc)
    {
        case VINF_SUCCESS:              return B_OK;
        case VERR_INVALID_POINTER:      return B_BAD_ADDRESS;
        case VERR_INVALID_PARAMETER:    return B_BAD_VALUE;
        case VERR_PERMISSION_DENIED:    return B_PERMISSION_DENIED;
        case VERR_NOT_IMPLEMENTED:      return B_UNSUPPORTED;
        case VERR_FILE_NOT_FOUND:       return B_ENTRY_NOT_FOUND;

        case SHFL_PATH_NOT_FOUND:
        case SHFL_FILE_NOT_FOUND:       return B_ENTRY_NOT_FOUND;
        case SHFL_FILE_EXISTS:          return B_FILE_EXISTS;

        default:
            return B_ERROR;
    }
}


static status_t std_ops(int32 op, ...)
{
    switch(op)
    {
        case B_MODULE_INIT:
            dprintf(MODULE_NAME ": B_MODULE_INIT\n");
            return init_module();
        case B_MODULE_UNINIT:
            dprintf(MODULE_NAME ": B_MODULE_UNINIT\n");
            uninit_module();
            return B_OK;
        default:
            return B_ERROR;
    }
}


static fs_volume_ops vboxsf_volume_ops =
{
    unmount,
    vboxsf_read_fs_info,
    NULL,                   /* write_fs_info */
    NULL,                   /* sync */
    vboxsf_get_vnode,
    NULL,                   /* open_index_dir */
    NULL,                   /* close_index_dir */
    NULL,                   /* free_index_dir_cookie */
    NULL,                   /* read_index_dir */
    NULL,                   /* rewind_index_dir */
    NULL,                   /* create_index */
    NULL,                   /* remove_index */
    NULL,                   /* read_index_stat */
    NULL,                   /* open_query */
    NULL,                   /* close_query */
    NULL,                   /* free_query_cookie */
    NULL,                   /* read_query */
    NULL,                   /* rewind_query */
    NULL,                   /* all_layers_mounted */
    NULL,                   /* create_sub_vnode */
    NULL,                   /* delete_sub_vnode */
};

static fs_vnode_ops vboxsf_vnode_ops =
{
    vboxsf_lookup,
    NULL,                   /* get_vnode_name */
    vboxsf_put_vnode,
    NULL,                   /* remove_vnode */
    NULL,                   /* can_page */
    NULL,                   /* read_pages */
    NULL,                   /* write_pages */
    NULL,                   /* io */
    NULL,                   /* cancel_io */
    NULL,                   /* get_file_map */
    NULL,                   /* ioctl */
    NULL,                   /* set_flags */
    NULL,                   /* select */
    NULL,                   /* deselect */
    NULL,                   /* fsync */
    vboxsf_read_symlink,
    vboxsf_create_symlink,
    vboxsf_link,
    vboxsf_unlink,
    vboxsf_rename,
    NULL,                   /* access */
    vboxsf_read_stat,
    vboxsf_write_stat,
    NULL,                   /* preallocate */
    vboxsf_create,
    vboxsf_open,
    vboxsf_close,
    vboxsf_free_cookie,
    vboxsf_read,
    vboxsf_write,
    vboxsf_create_dir,
    vboxsf_remove_dir,
    vboxsf_open_dir,
    vboxsf_close_dir,
    vboxsf_free_dir_cookie,
    vboxsf_read_dir,
    vboxsf_rewind_dir,
    NULL,                   /* open_attr_dir */
    NULL,                   /* close_attr_dir */
    NULL,                   /* free_attr_dir_cookie */
    NULL,                   /* read_attr_dir */
    NULL,                   /* rewind_attr_dir */
    NULL,                   /* create_attr */
    NULL,                   /* open_attr */
    NULL,                   /* close_attr */
    NULL,                   /* free_attr_cookie */
    NULL,                   /* read_attr */
    NULL,                   /* write_attr */
    NULL,                   /* read_attr_stat */
    NULL,                   /* write_attr_stat */
    NULL,                   /* rename_attr */
    NULL,                   /* remove_attr */
    NULL,                   /* create_special_node */
    NULL,                   /* get_super_vnode */
};

static file_system_module_info sVBoxSharedFileSystem =
{
    {
        MODULE_NAME B_CURRENT_FS_API_VERSION,
        0,
        std_ops,
    },
    FS_NAME,
    FS_PRETTY_NAME,
    0,                      /* DDM flags */
    NULL,                   /* identify_partition */
    NULL,                   /* scan_partition */
    NULL,                   /* free_identify_partition_cookie */
    NULL,                   /* free_partition_content_cookie */
    mount,
};

module_info *modules[] =
{
    (module_info *)&sVBoxSharedFileSystem,
    NULL,
};

