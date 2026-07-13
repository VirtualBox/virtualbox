/*
 * sftpserver.c - server based function for the sftp protocol
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2005 Aris Adamantiadis
 * Copyright (c) 2022 Zeyu Sheng <shengzeyu19_98@163.com>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"


#ifndef _WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/statvfs.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */
#ifdef HAVE_SYS_UTIME_H
#include <sys/utime.h>
#endif /* HAVE_SYS_UTIME_H */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "libssh/libssh.h"
#include "libssh/sftp.h"
#include "libssh/sftp_priv.h"
#include "libssh/sftpserver.h"
#include "libssh/ssh2.h"
#include "libssh/priv.h"
#include "libssh/buffer.h"
#include "libssh/misc.h"

#define SFTP_HANDLES 256

#define MAX_ENTRIES_NUM_IN_PACKET 50
#define MAX_LONG_NAME_LEN 350

static sftp_client_message
sftp_make_client_message(sftp_session sftp, sftp_packet packet)
{
    ssh_session session = sftp->session;
    sftp_client_message msg = NULL;
    ssh_buffer payload = NULL;
    int rc;
    int version;

    msg = calloc(1, sizeof(struct sftp_client_message_struct));
    if (msg == NULL) {
        ssh_set_error_oom(session);
        return NULL;
    }

    payload = packet->payload;
    msg->type = packet->type;
    msg->sftp = sftp;

    /* take a copy of the whole packet */
    msg->complete_message = ssh_buffer_new();
    if (msg->complete_message == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    rc = ssh_buffer_add_data(msg->complete_message,
                             ssh_buffer_get(payload),
                             ssh_buffer_get_len(payload));
    if (rc < 0) {
        goto error;
    }

    if (msg->type != SSH_FXP_INIT) {
        rc = ssh_buffer_get_u32(payload, &msg->id);
        if (rc != sizeof(uint32_t)) {
            goto error;
        }
    }

    switch (msg->type) {
        case SSH_FXP_INIT:
            rc = ssh_buffer_unpack(payload,
                                   "d",
                                   &version);
            if (rc != SSH_OK) {
                printf("unpack init failed!\n");
                goto error;
            }
            version = ntohl(version);
            sftp->client_version = version;
            break;
        case SSH_FXP_CLOSE:
        case SSH_FXP_READDIR:
            msg->handle = ssh_buffer_get_ssh_string(payload);
            if (msg->handle == NULL) {
                goto error;
            }
            break;
        case SSH_FXP_READ:
            rc = ssh_buffer_unpack(payload,
                                   "Sqd",
                                   &msg->handle,
                                   &msg->offset,
                                   &msg->len);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_FXP_WRITE:
            rc = ssh_buffer_unpack(payload,
                                   "SqS",
                                   &msg->handle,
                                   &msg->offset,
                                   &msg->data);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_FXP_REMOVE:
        case SSH_FXP_RMDIR:
        case SSH_FXP_OPENDIR:
        case SSH_FXP_READLINK:
        case SSH_FXP_REALPATH:
            rc = ssh_buffer_unpack(payload,
                                   "s",
                                   &msg->filename);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_FXP_RENAME:
        case SSH_FXP_SYMLINK:
            rc = ssh_buffer_unpack(payload,
                                   "sS",
                                   &msg->filename,
                                   &msg->data);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_FXP_MKDIR:
        case SSH_FXP_SETSTAT:
            rc = ssh_buffer_unpack(payload,
                                   "s",
                                   &msg->filename);
            if (rc != SSH_OK) {
                goto error;
            }
            msg->attr = sftp_parse_attr(sftp, payload, 0);
            if (msg->attr == NULL) {
                goto error;
            }
            break;
        case SSH_FXP_FSETSTAT:
            msg->handle = ssh_buffer_get_ssh_string(payload);
            if (msg->handle == NULL) {
                goto error;
            }
            msg->attr = sftp_parse_attr(sftp, payload, 0);
            if (msg->attr == NULL) {
                goto error;
            }
            break;
        case SSH_FXP_LSTAT:
        case SSH_FXP_STAT:
            rc = ssh_buffer_unpack(payload,
                                   "s",
                                   &msg->filename);
            if (rc != SSH_OK) {
                goto error;
            }
            if (sftp->version > 3) {
                ssh_buffer_unpack(payload, "d", &msg->flags);
            }
            break;
        case SSH_FXP_OPEN:
            rc = ssh_buffer_unpack(payload,
                                   "sd",
                                   &msg->filename,
                                   &msg->flags);
            if (rc != SSH_OK) {
                goto error;
            }
            msg->attr = sftp_parse_attr(sftp, payload, 0);
            if (msg->attr == NULL) {
                goto error;
            }
            break;
        case SSH_FXP_FSTAT:
            rc = ssh_buffer_unpack(payload,
                                   "S",
                                   &msg->handle);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_FXP_EXTENDED:
            rc = ssh_buffer_unpack(payload,
                                   "s",
                                   &msg->submessage);
            if (rc != SSH_OK) {
                goto error;
            }

            if (strcmp(msg->submessage, "hardlink@openssh.com") == 0 ||
                strcmp(msg->submessage, "posix-rename@openssh.com") == 0) {
                rc = ssh_buffer_unpack(payload,
                                       "sS",
                                       &msg->filename,
                                       &msg->data);
                if (rc != SSH_OK) {
                    goto error;
                }
            } else if (strcmp(msg->submessage, "statvfs@openssh.com") == 0 ){
                rc = ssh_buffer_unpack(payload,
                                       "s",
                                       &msg->filename);
                if (rc != SSH_OK) {
                    goto error;
                }
            }
            break;
        default:
            ssh_set_error(sftp->session, SSH_FATAL,
                          "Received unhandled sftp message %d", msg->type);
            goto error;
    }

    return msg;

error:
    sftp_client_message_free(msg);
    return NULL;
}

sftp_client_message sftp_get_client_message(sftp_session sftp)
{
    sftp_packet packet = NULL;

    packet = sftp_packet_read(sftp);
    if (packet == NULL) {
        return NULL;
    }
    return sftp_make_client_message(sftp, packet);
}

/**
 * @brief Get the client message from a sftp packet.
 *
 * @param  sftp         The sftp session handle.
 *
 * @return              The pointer to the generated sftp client message.
 */
static sftp_client_message
sftp_get_client_message_from_packet(sftp_session sftp)
{
    sftp_packet packet = NULL;

    packet = sftp->read_packet;
    if (packet == NULL) {
        return NULL;
    }
    return sftp_make_client_message(sftp, packet);
}

/* Send an sftp client message. Can be used in case of proxying */
int sftp_send_client_message(sftp_session sftp, sftp_client_message msg)
{
    return sftp_packet_write(sftp, msg->type, msg->complete_message);
}

uint8_t sftp_client_message_get_type(sftp_client_message msg)
{
    return msg->type;
}

const char *sftp_client_message_get_filename(sftp_client_message msg)
{
    return msg->filename;
}

void
sftp_client_message_set_filename(sftp_client_message msg, const char *newname)
{
    free(msg->filename);
    msg->filename = strdup(newname);
}

const char *sftp_client_message_get_data(sftp_client_message msg)
{
    if (msg->str_data == NULL)
        msg->str_data = ssh_string_to_char(msg->data);
    return msg->str_data;
}

uint32_t sftp_client_message_get_flags(sftp_client_message msg)
{
    return msg->flags;
}

const char *sftp_client_message_get_submessage(sftp_client_message msg)
{
    return msg->submessage;
}

void sftp_client_message_free(sftp_client_message msg)
{
    if (msg == NULL) {
        return;
    }

    SAFE_FREE(msg->filename);
    SAFE_FREE(msg->submessage);
    SSH_STRING_FREE(msg->data);
    SSH_STRING_FREE(msg->handle);
    sftp_attributes_free(msg->attr);
    SSH_BUFFER_FREE(msg->complete_message);
    SAFE_FREE(msg->str_data);
    ZERO_STRUCTP(msg);
    SAFE_FREE(msg);
}

int
sftp_reply_name(sftp_client_message msg, const char *name, sftp_attributes attr)
{
    ssh_buffer out = NULL;
    ssh_string file = NULL;

    out = ssh_buffer_new();
    if (out == NULL) {
        return -1;
    }

    file = ssh_string_from_char(name);
    if (file == NULL) {
        SSH_BUFFER_FREE(out);
        return -1;
    }

    SSH_LOG(SSH_LOG_PROTOCOL, "Sending name %s", ssh_string_get_char(file));

    if (ssh_buffer_add_u32(out, msg->id) < 0 ||
        ssh_buffer_add_u32(out, htonl(1)) < 0 ||
        ssh_buffer_add_ssh_string(out, file) < 0 ||
        ssh_buffer_add_ssh_string(out, file) < 0 || /* The protocol is broken here between 3 & 4 */
        buffer_add_attributes(out, attr) < 0 ||
        sftp_packet_write(msg->sftp, SSH_FXP_NAME, out) < 0) {
        SSH_BUFFER_FREE(out);
        SSH_STRING_FREE(file);
        return -1;
    }
    SSH_BUFFER_FREE(out);
    SSH_STRING_FREE(file);

    return 0;
}

int sftp_reply_handle(sftp_client_message msg, ssh_string handle)
{
    ssh_buffer out;

    out = ssh_buffer_new();
    if (out == NULL) {
        return -1;
    }

    ssh_log_hexdump("Sending handle:",
                    (const unsigned char *)ssh_string_get_char(handle),
                    ssh_string_len(handle));

    if (ssh_buffer_add_u32(out, msg->id) < 0 ||
        ssh_buffer_add_ssh_string(out, handle) < 0 ||
        sftp_packet_write(msg->sftp, SSH_FXP_HANDLE, out) < 0) {
        SSH_BUFFER_FREE(out);
        return -1;
    }
    SSH_BUFFER_FREE(out);

    return 0;
}

int sftp_reply_attr(sftp_client_message msg, sftp_attributes attr)
{
    ssh_buffer out;

    out = ssh_buffer_new();
    if (out == NULL) {
        return -1;
    }

    SSH_LOG(SSH_LOG_PROTOCOL, "Sending attr");

    if (ssh_buffer_add_u32(out, msg->id) < 0 ||
        buffer_add_attributes(out, attr) < 0 ||
        sftp_packet_write(msg->sftp, SSH_FXP_ATTRS, out) < 0) {
        SSH_BUFFER_FREE(out);
        return -1;
    }
    SSH_BUFFER_FREE(out);

    return 0;
}

int
sftp_reply_names_add(sftp_client_message msg, const char *file,
                     const char *longname, sftp_attributes attr)
{
    ssh_string name = NULL;

    name = ssh_string_from_char(file);
    if (name == NULL) {
        return -1;
    }

    if (msg->attrbuf == NULL) {
        msg->attrbuf = ssh_buffer_new();
        if (msg->attrbuf == NULL) {
            SSH_STRING_FREE(name);
            return -1;
        }
    }

    if (ssh_buffer_add_ssh_string(msg->attrbuf, name) < 0) {
        SSH_STRING_FREE(name);
        return -1;
    }

    SSH_STRING_FREE(name);
    name = ssh_string_from_char(longname);
    if (name == NULL) {
        return -1;
    }
    if (ssh_buffer_add_ssh_string(msg->attrbuf, name) < 0 ||
        buffer_add_attributes(msg->attrbuf, attr) < 0) {
        SSH_STRING_FREE(name);
        return -1;
    }
    SSH_STRING_FREE(name);
    msg->attr_num++;

    return 0;
}

int sftp_reply_names(sftp_client_message msg)
{
    ssh_buffer out;

    out = ssh_buffer_new();
    if (out == NULL) {
        SSH_BUFFER_FREE(msg->attrbuf);
        return -1;
    }

    SSH_LOG(SSH_LOG_PROTOCOL, "Sending %d names", msg->attr_num);

    if (ssh_buffer_add_u32(out, msg->id) < 0 ||
        ssh_buffer_add_u32(out, htonl(msg->attr_num)) < 0 ||
        ssh_buffer_add_data(out, ssh_buffer_get(msg->attrbuf),
                            ssh_buffer_get_len(msg->attrbuf)) < 0 ||
        sftp_packet_write(msg->sftp, SSH_FXP_NAME, out) < 0) {
        SSH_BUFFER_FREE(out);
        SSH_BUFFER_FREE(msg->attrbuf);
        return -1;
    }

    SSH_BUFFER_FREE(out);
    SSH_BUFFER_FREE(msg->attrbuf);

    msg->attr_num = 0;
    msg->attrbuf = NULL;

    return 0;
}

int
sftp_reply_status(sftp_client_message msg, uint32_t status, const char *message)
{
    ssh_buffer out = NULL;
    ssh_string s = NULL;

    out = ssh_buffer_new();
    if (out == NULL) {
        return -1;
    }

    s = ssh_string_from_char(message ? message : "");
    if (s == NULL) {
        SSH_BUFFER_FREE(out);
        return -1;
    }

    SSH_LOG(SSH_LOG_PROTOCOL, "Sending status %d, message: %s", status,
            ssh_string_get_char(s));

    if (ssh_buffer_add_u32(out, msg->id) < 0 ||
        ssh_buffer_add_u32(out, htonl(status)) < 0 ||
        ssh_buffer_add_ssh_string(out, s) < 0 ||
        ssh_buffer_add_u32(out, 0) < 0 || /* language string */
        sftp_packet_write(msg->sftp, SSH_FXP_STATUS, out) < 0) {
        SSH_BUFFER_FREE(out);
        SSH_STRING_FREE(s);
        return -1;
    }

    SSH_BUFFER_FREE(out);
    SSH_STRING_FREE(s);

    return 0;
}

int sftp_reply_data(sftp_client_message msg, const void *data, int len)
{
    ssh_buffer out;

    out = ssh_buffer_new();
    if (out == NULL) {
        return -1;
    }

    SSH_LOG(SSH_LOG_PROTOCOL, "Sending data, length: %d", len);

    if (ssh_buffer_add_u32(out, msg->id) < 0 ||
        ssh_buffer_add_u32(out, ntohl(len)) < 0 ||
        ssh_buffer_add_data(out, data, len) < 0 ||
        sftp_packet_write(msg->sftp, SSH_FXP_DATA, out) < 0) {
        SSH_BUFFER_FREE(out);
        return -1;
    }
    SSH_BUFFER_FREE(out);

    return 0;
}

/**
 * @brief Handle the statvfs request, return information the mounted file system.
 *
 * @param  msg          The sftp client message.
 *
 * @param  st           The statvfs state of target file.
 *
 * @return              0 on success, < 0 on error with ssh and sftp error set.
 *
 * @see sftp_get_error()
 */
static int
sftp_reply_statvfs(sftp_client_message msg, sftp_statvfs_t st)
{
    int ret = 0;
    ssh_buffer out;
    out = ssh_buffer_new();
    if (out == NULL) {
        return -1;
    }

    SSH_LOG(SSH_LOG_PROTOCOL, "Sending statvfs reply");

    if (ssh_buffer_add_u32(out, msg->id) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_bsize)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_frsize)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_blocks)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_bfree)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_bavail)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_files)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_ffree)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_favail)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_fsid)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_flag)) < 0 ||
        ssh_buffer_add_u64(out, ntohll(st->f_namemax)) < 0 ||
        sftp_packet_write(msg->sftp, SSH_FXP_EXTENDED_REPLY, out) < 0) {
        ret = -1;
    }
    SSH_BUFFER_FREE(out);

    return ret;
}

int sftp_reply_version(sftp_client_message client_msg)
{
    sftp_session sftp = client_msg->sftp;
    ssh_session session = sftp->session;
    int version;
    ssh_buffer reply;
    int rc;

    SSH_LOG(SSH_LOG_PROTOCOL, "Sending version packet");

    version = sftp->client_version;
    reply = ssh_buffer_new();
    if (reply == NULL) {
        ssh_set_error_oom(session);
        return -1;
    }

    rc = ssh_buffer_pack(reply, "dssssss",
                         LIBSFTP_VERSION,
                         "posix-rename@openssh.com",
                         "1",
                         "hardlink@openssh.com",
                         "1",
                         "statvfs@openssh.com",
                         "2");
    if (rc != SSH_OK) {
        ssh_set_error_oom(session);
        SSH_BUFFER_FREE(reply);
        return -1;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_VERSION, reply);
    if (rc < 0) {
        SSH_BUFFER_FREE(reply);
        return -1;
    }
    SSH_BUFFER_FREE(reply);

    SSH_LOG(SSH_LOG_PROTOCOL, "Server version sent");

    if (version > LIBSFTP_VERSION) {
        sftp->version = LIBSFTP_VERSION;
    } else {
        sftp->version = version;
    }

    return SSH_OK;
}


/*
 * This function will return you a new handle to give the client.
 * the function accepts an info that can be retrieved later with
 * the handle. Care is given that a corrupted handle won't give a
 * valid info (or worse).
 */
ssh_string sftp_handle_alloc(sftp_session sftp, void *info)
{
    ssh_string ret = NULL;
    uint32_t val;
    uint32_t i;

    if (sftp->handles == NULL) {
        sftp->handles = calloc(SFTP_HANDLES, sizeof(void *));
        if (sftp->handles == NULL) {
            return NULL;
        }
    }

    for (i = 0; i < SFTP_HANDLES; i++) {
        if (sftp->handles[i] == NULL) {
            break;
        }
    }

    if (i == SFTP_HANDLES) {
        return NULL; /* no handle available */
    }

    val = i;
    ret = ssh_string_new(4);
    if (ret == NULL) {
        return NULL;
    }

    memcpy(ssh_string_data(ret), &val, sizeof(uint32_t));
    sftp->handles[i] = info;

    return ret;
}

void *sftp_handle(sftp_session sftp, ssh_string handle)
{
    uint32_t val;

    if (sftp->handles == NULL) {
        return NULL;
    }

    if (ssh_string_len(handle) != sizeof(uint32_t)) {
        return NULL;
    }

    memcpy(&val, ssh_string_data(handle), sizeof(uint32_t));

    if (val >= SFTP_HANDLES) {
        return NULL;
    }

    return sftp->handles[val];
}

void sftp_handle_remove(sftp_session sftp, void *handle)
{
    int i;

    for (i = 0; i < SFTP_HANDLES; i++) {
        if (sftp->handles[i] == handle) {
            sftp->handles[i] = NULL;
            break;
        }
    }
}

/* Default SFTP handlers */

static const char *
ssh_str_error(int u_errno)
{
    switch (u_errno) {
    case SSH_FX_NO_SUCH_FILE:
        return "No such file";
    case SSH_FX_PERMISSION_DENIED:
        return "Permission denied";
    case SSH_FX_BAD_MESSAGE:
        return "Bad message";
    case SSH_FX_OP_UNSUPPORTED:
        return "Operation not supported";
    default:
        return "Operation failed";
    }
}

static int
unix_errno_to_ssh_stat(int u_errno)
{
    int ret = SSH_OK;
    switch (u_errno) {
    case 0:
        break;
    case ENOENT:
    case ENOTDIR:
    case EBADF:
    case ELOOP:
        ret = SSH_FX_NO_SUCH_FILE;
        break;
    case EPERM:
    case EACCES:
    case EFAULT:
        ret = SSH_FX_PERMISSION_DENIED;
        break;
    case ENAMETOOLONG:
    case EINVAL:
        ret = SSH_FX_BAD_MESSAGE;
        break;
    case ENOSYS:
        ret = SSH_FX_OP_UNSUPPORTED;
        break;
    default:
        ret = SSH_FX_FAILURE;
        break;
    }

    return ret;
}

static void
stat_to_filexfer_attrib(const struct stat *z_st, struct sftp_attributes_struct *z_attr)
{
    z_attr->flags = 0 | (uint32_t)SSH_FILEXFER_ATTR_SIZE;
    z_attr->size = z_st->st_size;

    z_attr->flags |= (uint32_t)SSH_FILEXFER_ATTR_UIDGID;
    z_attr->uid = z_st->st_uid;
    z_attr->gid = z_st->st_gid;

    z_attr->flags |= (uint32_t)SSH_FILEXFER_ATTR_PERMISSIONS;
    z_attr->permissions = z_st->st_mode;

    z_attr->flags |= (uint32_t)SSH_FILEXFER_ATTR_ACMODTIME;
    z_attr->atime = z_st->st_atime;
    z_attr->mtime = z_st->st_mtime;
}

static void
clear_filexfer_attrib(struct sftp_attributes_struct *z_attr)
{
    z_attr->flags = 0;
    z_attr->size = 0;
    z_attr->uid = 0;
    z_attr->gid = 0;
    z_attr->permissions = 0;
    z_attr->atime = 0;
    z_attr->mtime = 0;
}

#ifndef _WIN32
/* internal */
enum sftp_handle_type
{
    SFTP_NULL_HANDLE,
    SFTP_DIR_HANDLE,
    SFTP_FILE_HANDLE
};

struct sftp_handle
{
    enum sftp_handle_type type;
    int fd;
    DIR *dirp;
    char *name;
};

SSH_SFTP_CALLBACK(process_unsupposed);
SSH_SFTP_CALLBACK(process_open);
SSH_SFTP_CALLBACK(process_read);
SSH_SFTP_CALLBACK(process_write);
SSH_SFTP_CALLBACK(process_close);
SSH_SFTP_CALLBACK(process_opendir);
SSH_SFTP_CALLBACK(process_readdir);
SSH_SFTP_CALLBACK(process_rmdir);
SSH_SFTP_CALLBACK(process_realpath);
SSH_SFTP_CALLBACK(process_mkdir);
SSH_SFTP_CALLBACK(process_lstat);
SSH_SFTP_CALLBACK(process_stat);
SSH_SFTP_CALLBACK(process_readlink);
SSH_SFTP_CALLBACK(process_symlink);
SSH_SFTP_CALLBACK(process_remove);
SSH_SFTP_CALLBACK(process_extended_statvfs);
SSH_SFTP_CALLBACK(process_setstat);

const struct sftp_message_handler message_handlers[] = {
    {"open", NULL, SSH_FXP_OPEN, process_open},
    {"close", NULL, SSH_FXP_CLOSE, process_close},
    {"read", NULL, SSH_FXP_READ, process_read},
    {"write", NULL, SSH_FXP_WRITE, process_write},
    {"lstat", NULL, SSH_FXP_LSTAT, process_lstat},
    {"fstat", NULL, SSH_FXP_FSTAT, process_unsupposed},
    {"setstat", NULL, SSH_FXP_SETSTAT, process_setstat},
    {"fsetstat", NULL, SSH_FXP_FSETSTAT, process_unsupposed},
    {"opendir", NULL, SSH_FXP_OPENDIR, process_opendir},
    {"readdir", NULL, SSH_FXP_READDIR, process_readdir},
    {"remove", NULL, SSH_FXP_REMOVE, process_remove},
    {"mkdir", NULL, SSH_FXP_MKDIR, process_mkdir},
    {"rmdir", NULL, SSH_FXP_RMDIR, process_rmdir},
    {"realpath", NULL, SSH_FXP_REALPATH, process_realpath},
    {"stat", NULL, SSH_FXP_STAT, process_stat},
    {"rename", NULL, SSH_FXP_RENAME, process_unsupposed},
    {"readlink", NULL, SSH_FXP_READLINK, process_readlink},
    {"symlink", NULL, SSH_FXP_SYMLINK, process_symlink},
    {"init", NULL, SSH_FXP_INIT, sftp_reply_version},
    {NULL, NULL, 0, NULL},
};

const struct sftp_message_handler extended_handlers[] = {
    /* here are some extended type handlers */
    {"statvfs", "statvfs@openssh.com", 0, process_extended_statvfs},
    {NULL, NULL, 0, NULL},
};

static int
process_open(sftp_client_message client_msg)
{
    const char *filename = sftp_client_message_get_filename(client_msg);
    uint32_t msg_flag = sftp_client_message_get_flags(client_msg);
    uint32_t mode = client_msg->attr->permissions;
    ssh_string handle_s = NULL;
    struct sftp_handle *h = NULL;
    int file_flag;
    int fd = -1;
    int status;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing open: filename %s, mode=0%o" PRIu32,
            filename, mode);

    if (((msg_flag & (uint32_t)SSH_FXF_READ) == SSH_FXF_READ) &&
        ((msg_flag & (uint32_t)SSH_FXF_WRITE) == SSH_FXF_WRITE)) {
        file_flag = O_RDWR; // file must exist
        if ((msg_flag & (uint32_t)SSH_FXF_CREAT) == SSH_FXF_CREAT)
            file_flag |= O_CREAT;
    } else if ((msg_flag & (uint32_t)SSH_FXF_WRITE) == SSH_FXF_WRITE) {
        file_flag = O_WRONLY;
        if ((msg_flag & (uint32_t)SSH_FXF_APPEND) == SSH_FXF_APPEND)
            file_flag |= O_APPEND;
        if ((msg_flag & (uint32_t)SSH_FXF_CREAT) == SSH_FXF_CREAT)
            file_flag |= O_CREAT;
    } else if ((msg_flag & (uint32_t)SSH_FXF_READ) == SSH_FXF_READ) {
        file_flag = O_RDONLY;
    } else {
        SSH_LOG(SSH_LOG_PROTOCOL, "undefined message flag: %" PRIu32, msg_flag);
        sftp_reply_status(client_msg, SSH_FX_FAILURE, "Flag error");
        return SSH_ERROR;
    }

    fd = open(filename, file_flag, mode);
    if (fd == -1) {
        int saved_errno = errno;
        SSH_LOG(SSH_LOG_PROTOCOL, "error open file with error: %s",
                strerror(saved_errno));
        status = unix_errno_to_ssh_stat(saved_errno);
        sftp_reply_status(client_msg, status, "Write error");
        return SSH_ERROR;
    }

    h = calloc(1, sizeof (struct sftp_handle));
    if (h == NULL) {
        close(fd);
        SSH_LOG(SSH_LOG_PROTOCOL, "failed to allocate a new handle");
        sftp_reply_status(client_msg, SSH_FX_FAILURE,
                          "Failed to allocate new handle");
        return SSH_ERROR;
    }
    h->fd = fd;
    h->type = SFTP_FILE_HANDLE;
    handle_s = sftp_handle_alloc(client_msg->sftp, h);
    if (handle_s != NULL) {
        sftp_reply_handle(client_msg, handle_s);
        ssh_string_free(handle_s);
    } else {
        free(h);
        close(fd);
        SSH_LOG(SSH_LOG_PROTOCOL, "Failed to allocate handle");
        sftp_reply_status(client_msg, SSH_FX_FAILURE,
                          "Failed to allocate handle");
    }

    return SSH_OK;
}

static int
process_read(sftp_client_message client_msg)
{
    sftp_session sftp = client_msg->sftp;
    ssh_string handle = client_msg->handle;
    struct sftp_handle *h = NULL;
    ssize_t readn = 0;
    int fd = -1;
    char *buffer = NULL;
    off_t off;

    ssh_log_hexdump("Processing read: handle:",
                    (const unsigned char *)ssh_string_get_char(handle),
                    ssh_string_len(handle));

    h = sftp_handle(sftp, handle);
    if (h != NULL && h->type == SFTP_FILE_HANDLE) {
        fd = h->fd;
    }

    if (fd < 0) {
        sftp_reply_status(client_msg, SSH_FX_INVALID_HANDLE, NULL);
        SSH_LOG(SSH_LOG_PROTOCOL, "invalid fd (%d) received from handle", fd);
        return SSH_ERROR;
    }
    off = lseek(fd, client_msg->offset, SEEK_SET);
    if (off == -1) {
        sftp_reply_status(client_msg, SSH_FX_FAILURE, NULL);
        SSH_LOG(SSH_LOG_PROTOCOL,
                "error seeking file fd: %d at offset: %" PRIu64,
                fd, client_msg->offset);
        return SSH_ERROR;
    }

    buffer = malloc(client_msg->len);
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_reply_status(client_msg, SSH_FX_FAILURE, NULL);
        SSH_LOG(SSH_LOG_PROTOCOL, "Failed to allocate memory for read data");
        return SSH_ERROR;
    }
    readn = ssh_readn(fd, buffer, client_msg->len);
    if (readn < 0) {
        sftp_reply_status(client_msg, SSH_FX_FAILURE, NULL);
        SSH_LOG(SSH_LOG_PROTOCOL, "read file error!");
        free(buffer);
        return SSH_ERROR;
    } else if (readn > 0) {
        sftp_reply_data(client_msg, buffer, readn);
    } else {
        sftp_reply_status(client_msg, SSH_FX_EOF, NULL);
    }

    free(buffer);
    return SSH_OK;
}

static int
process_write(sftp_client_message client_msg)
{
    sftp_session sftp = client_msg->sftp;
    ssh_string handle = client_msg->handle;
    struct sftp_handle *h = NULL;
    ssize_t written = 0;
    int fd = -1;
    const char *msg_data = NULL;
    uint32_t len;
    off_t off;

    ssh_log_hexdump("Processing write: handle",
                    (const unsigned char *)ssh_string_get_char(handle),
                    ssh_string_len(handle));

    h = sftp_handle(sftp, handle);
    if (h != NULL && h->type == SFTP_FILE_HANDLE) {
        fd = h->fd;
    }
    if (fd < 0) {
        sftp_reply_status(client_msg, SSH_FX_INVALID_HANDLE, NULL);
        SSH_LOG(SSH_LOG_PROTOCOL, "write file fd error!");
        return SSH_ERROR;
    }

    msg_data = ssh_string_get_char(client_msg->data);
    len = ssh_string_len(client_msg->data);

    off = lseek(fd, client_msg->offset, SEEK_SET);
    if (off == -1) {
        sftp_reply_status(client_msg, SSH_FX_FAILURE, NULL);
        SSH_LOG(SSH_LOG_PROTOCOL,
                "error seeking file at offset: %" PRIu64,
                client_msg->offset);
        return SSH_ERROR;
    }
    written = ssh_writen(fd, msg_data, len);
    if (written != (ssize_t)len) {
        sftp_reply_status(client_msg, SSH_FX_FAILURE, "Write error");
        SSH_LOG(SSH_LOG_PROTOCOL, "file write error!");
        return SSH_ERROR;
    }

    sftp_reply_status(client_msg, SSH_FX_OK, NULL);

    return SSH_OK;
}

static int
process_close(sftp_client_message client_msg)
{
    sftp_session sftp = client_msg->sftp;
    ssh_string handle = client_msg->handle;
    struct sftp_handle *h = NULL;
    int ret;

    ssh_log_hexdump("Processing close: handle:",
                    (const unsigned char *)ssh_string_get_char(handle),
                    ssh_string_len(handle));

    h = sftp_handle(sftp, handle);
    if (h == NULL) {
        SSH_LOG(SSH_LOG_PROTOCOL, "invalid handle");
        sftp_reply_status(client_msg, SSH_FX_INVALID_HANDLE, "Invalid handle");
        return SSH_OK;
    } else if (h->type == SFTP_FILE_HANDLE) {
        int fd = h->fd;
        close(fd);
        ret = SSH_OK;
    } else if (h->type == SFTP_DIR_HANDLE) {
        DIR *dir = h->dirp;
        closedir(dir);
        ret = SSH_OK;
    } else {
        ret = SSH_ERROR;
    }
    SAFE_FREE(h->name);
    sftp_handle_remove(sftp, h);
    SAFE_FREE(h);

    if (ret == SSH_OK) {
        sftp_reply_status(client_msg, SSH_FX_OK, NULL);
    } else {
        SSH_LOG(SSH_LOG_PROTOCOL, "closing file failed");
        sftp_reply_status(client_msg, SSH_FX_BAD_MESSAGE, "Invalid handle");
    }

    return SSH_OK;
}

static int
process_opendir(sftp_client_message client_msg)
{
    DIR *dir = NULL;
    const char *dir_name = sftp_client_message_get_filename(client_msg);
    ssh_string handle_s = NULL;
    struct sftp_handle *h = NULL;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing opendir %s", dir_name);

    dir = opendir(dir_name);
    if (dir == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "No such directory");
        return SSH_ERROR;
    }

    h = calloc(1, sizeof (struct sftp_handle));
    if (h == NULL) {
        closedir(dir);
        SSH_LOG(SSH_LOG_PROTOCOL, "failed to allocate a new handle");
        sftp_reply_status(client_msg, SSH_FX_FAILURE,
                          "Failed to allocate new handle");
        return SSH_ERROR;
    }
    h->dirp = dir;
    h->name = strdup(dir_name);
    h->type = SFTP_DIR_HANDLE;
    handle_s = sftp_handle_alloc(client_msg->sftp, h);

    if (handle_s != NULL) {
        sftp_reply_handle(client_msg, handle_s);
        ssh_string_free(handle_s);
    } else {
        free(h);
        closedir(dir);
        sftp_reply_status(client_msg, SSH_FX_FAILURE, "No handle available");
    }

    return SSH_OK;
}

static int
readdir_long_name(char *z_file_name, struct stat *z_st, char *z_long_name)
{
    char tmpbuf[MAX_LONG_NAME_LEN];
    char time[50];
    char *ptr = z_long_name;
    int mode = z_st->st_mode;

    *ptr = '\0';

    switch (mode & S_IFMT) {
    case S_IFDIR:
        *ptr++ = 'd';
        break;
    default:
        *ptr++ = '-';
        break;
    }

    /* user */
    if (mode & 0400)
        *ptr++ = 'r';
    else
        *ptr++ = '-';

    if (mode & 0200)
        *ptr++ = 'w';
    else
        *ptr++ = '-';

    if (mode & 0100) {
        if (mode & S_ISUID)
            *ptr++ = 's';
        else
            *ptr++ = 'x';
    } else
        *ptr++ = '-';

    /* group */
    if (mode & 040)
        *ptr++ = 'r';
    else
        *ptr++ = '-';
    if (mode & 020)
        *ptr++ = 'w';
    else
        *ptr++ = '-';
    if (mode & 010)
        *ptr++ = 'x';
    else
        *ptr++ = '-';

    /* other */
    if (mode & 04)
        *ptr++ = 'r';
    else
        *ptr++ = '-';
    if (mode & 02)
        *ptr++ = 'w';
    else
        *ptr++ = '-';
    if (mode & 01)
        *ptr++ = 'x';
    else
        *ptr++ = '-';

    *ptr++ = ' ';
    *ptr = '\0';

    snprintf(tmpbuf, sizeof(tmpbuf), "%3d %d %d %d", (int)z_st->st_nlink,
             (int)z_st->st_uid, (int)z_st->st_gid, (int)z_st->st_size);
    strcat(z_long_name, tmpbuf);

    ctime_r(&z_st->st_mtime, time);
    if ((ptr = strchr(time, '\n'))) {
        *ptr = '\0';
    }
    snprintf(tmpbuf, sizeof(tmpbuf), " %s %s", time + 4, z_file_name);
    strcat(z_long_name, tmpbuf);

    return SSH_OK;
}

static int
process_readdir(sftp_client_message client_msg)
{
    sftp_session sftp = client_msg->sftp;
    ssh_string handle = client_msg->handle;
    struct sftp_handle *h = NULL;
    int ret = SSH_OK;
    int entries = 0;
    struct dirent *dentry = NULL;
    DIR *dir = NULL;
    char long_path[PATH_MAX];
    int srclen;
    const char *handle_name = NULL;

    ssh_log_hexdump("Processing readdir: handle",
                    (const unsigned char *)ssh_string_get_char(handle),
                    ssh_string_len(handle));

    h = sftp_handle(sftp, client_msg->handle);
    if (h != NULL && h->type == SFTP_DIR_HANDLE) {
        dir = h->dirp;
        handle_name = h->name;
    }
    if (dir == NULL) {
        SSH_LOG(SSH_LOG_PROTOCOL, "got wrong handle from msg");
        sftp_reply_status(client_msg, SSH_FX_INVALID_HANDLE, NULL);
        return SSH_ERROR;
    }

    if (handle_name == NULL) {
        sftp_reply_status(client_msg, SSH_FX_INVALID_HANDLE, NULL);
        return SSH_ERROR;
    }

    srclen = strlen(handle_name);
    if (srclen + 2 >= PATH_MAX) {
        SSH_LOG(SSH_LOG_PROTOCOL, "handle string length exceed max length!");
        sftp_reply_status(client_msg, SSH_FX_INVALID_HANDLE, NULL);
        return SSH_ERROR;
    }

    for (int i = 0; i < MAX_ENTRIES_NUM_IN_PACKET; i++) {
        dentry = readdir(dir);

        if (dentry != NULL) {
            struct sftp_attributes_struct attr;
            struct stat st;
            char long_name[MAX_LONG_NAME_LEN];

            if (strlen(dentry->d_name) + srclen + 1 >= PATH_MAX) {
                SSH_LOG(SSH_LOG_PROTOCOL,
                        "handle string length exceed max length!");
                sftp_reply_status(client_msg, SSH_FX_INVALID_HANDLE, NULL);
                return SSH_ERROR;
            }
            snprintf(long_path, PATH_MAX, "%s/%s", handle_name, dentry->d_name);

            if (lstat(long_path, &st) == 0) {
                stat_to_filexfer_attrib(&st, &attr);
            } else {
                clear_filexfer_attrib(&attr);
            }

            if (readdir_long_name(dentry->d_name, &st, long_name) == 0) {
                sftp_reply_names_add(client_msg, dentry->d_name, long_name, &attr);
            } else {
                printf("readdir long name error\n");
            }

            entries++;
        } else {
            break;
        }
    }

    if (entries > 0) {
        ret = sftp_reply_names(client_msg);
    } else {
        sftp_reply_status(client_msg, SSH_FX_EOF, NULL);
    }

    return ret;
}

static int
process_mkdir(sftp_client_message client_msg)
{
    int ret = SSH_OK;
    const char *filename = sftp_client_message_get_filename(client_msg);
    uint32_t msg_flags = client_msg->attr->flags;
    uint32_t permission = client_msg->attr->permissions;
    uint32_t mode = (msg_flags & (uint32_t)SSH_FILEXFER_ATTR_PERMISSIONS)
                    ? permission & (uint32_t)07777 : 0777;
    int status = SSH_FX_OK;
    int rv;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing mkdir %s, mode=0%o" PRIu32,
            filename, mode);

    if (filename == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "File name error");
        return SSH_ERROR;
    }

    rv = mkdir(filename, mode);
    if (rv < 0) {
        int saved_errno = errno;
        SSH_LOG(SSH_LOG_PROTOCOL, "failed to mkdir: %s", strerror(saved_errno));
        status = unix_errno_to_ssh_stat(saved_errno);
        ret = SSH_ERROR;
    }

    sftp_reply_status(client_msg, status, NULL);

    return ret;
}

static int
process_rmdir(sftp_client_message client_msg)
{
    int ret = SSH_OK;
    const char *filename = sftp_client_message_get_filename(client_msg);
    int status = SSH_FX_OK;
    int rv;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing rmdir %s", filename);

    if (filename == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "File name error");
        return SSH_ERROR;
    }

    rv = rmdir(filename);
    if (rv < 0) {
        status = unix_errno_to_ssh_stat(errno);
        ret = SSH_ERROR;
    }

    sftp_reply_status(client_msg, status, NULL);

    return ret;
}

static int
process_realpath(sftp_client_message client_msg)
{
    const char *filename = sftp_client_message_get_filename(client_msg);
    char *path = NULL;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing realpath %s", filename);

    if (filename[0] == '\0') {
        path = realpath(".", NULL);
    } else {
        path = realpath(filename, NULL);
    }
    if (path == NULL) {
        int saved_errno = errno;
        int status = unix_errno_to_ssh_stat(saved_errno);
        const char *err_msg = ssh_str_error(status);

        SSH_LOG(SSH_LOG_PROTOCOL, "realpath failed: %s", strerror(saved_errno));
        sftp_reply_status(client_msg, status, err_msg);
        return SSH_ERROR;
    }
    sftp_reply_name(client_msg, path, NULL);
    free(path);
    return SSH_OK;
}

static int
process_lstat(sftp_client_message client_msg)
{
    int ret = SSH_OK;
    const char *filename = sftp_client_message_get_filename(client_msg);
    struct sftp_attributes_struct attr;
    struct stat st;
    int status = SSH_FX_OK;
    int rv;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing lstat %s", filename);

    if (filename == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "File name error");
        return SSH_ERROR;
    }

    rv = lstat(filename, &st);
    if (rv < 0) {
        int saved_errno = errno;
        SSH_LOG(SSH_LOG_PROTOCOL, "lstat failed: %s", strerror(saved_errno));
        status = unix_errno_to_ssh_stat(saved_errno);
        sftp_reply_status(client_msg, status, NULL);
        ret = SSH_ERROR;
    } else {
        stat_to_filexfer_attrib(&st, &attr);
        sftp_reply_attr(client_msg, &attr);
    }

    return ret;
}

static int
process_stat(sftp_client_message client_msg)
{
    int ret = SSH_OK;
    const char *filename = sftp_client_message_get_filename(client_msg);
    struct sftp_attributes_struct attr;
    struct stat st;
    int status = SSH_FX_OK;
    int rv;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing stat %s", filename);

    if (filename == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "File name error");
        return SSH_ERROR;
    }

    rv = stat(filename, &st);
    if (rv < 0) {
        int saved_errno = errno;
        SSH_LOG(SSH_LOG_PROTOCOL, "lstat failed: %s", strerror(saved_errno));
        status = unix_errno_to_ssh_stat(saved_errno);
        sftp_reply_status(client_msg, status, NULL);
        ret = SSH_ERROR;
    } else {
        stat_to_filexfer_attrib(&st, &attr);
        sftp_reply_attr(client_msg, &attr);
    }

    return ret;
}

static int
process_setstat(sftp_client_message client_msg)
{
    int rv;
    int ret = SSH_OK;
    int status = SSH_FX_OK;
    uint32_t msg_flags = client_msg->attr->flags;
    const char *filename = sftp_client_message_get_filename(client_msg);

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing setstat %s", filename);

    if (filename == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "File name error");
        return SSH_ERROR;
    }

    if (msg_flags & SSH_FILEXFER_ATTR_SIZE) {
        rv = truncate(filename, client_msg->attr->size);
        if (rv < 0) {
            int saved_errno = errno;
            SSH_LOG(SSH_LOG_PROTOCOL,
                    "changing size failed: %s",
                    strerror(saved_errno));
            status = unix_errno_to_ssh_stat(saved_errno);
            sftp_reply_status(client_msg, status, NULL);
            return rv;
        }
    }

    if (msg_flags & SSH_FILEXFER_ATTR_PERMISSIONS) {
        rv = chmod(filename, client_msg->attr->permissions);
        if (rv < 0) {
            int saved_errno = errno;
            SSH_LOG(SSH_LOG_PROTOCOL,
                    "chmod failed: %s",
                    strerror(saved_errno));
            status = unix_errno_to_ssh_stat(saved_errno);
            sftp_reply_status(client_msg, status, NULL);
            return rv;
        }
    }

    if (msg_flags & SSH_FILEXFER_ATTR_UIDGID) {
        rv = chown(filename, client_msg->attr->uid, client_msg->attr->gid);
        if (rv < 0) {
            int saved_errno = errno;
            SSH_LOG(SSH_LOG_PROTOCOL,
                    "chwon failed: %s",
                    strerror(saved_errno));
            status = unix_errno_to_ssh_stat(saved_errno);
            sftp_reply_status(client_msg, status, NULL);
            return rv;
        }
    }

    if (msg_flags & SSH_FILEXFER_ATTR_ACMODTIME) {
#ifdef HAVE_SYS_TIME_H
        struct timeval tv[2];

        tv[0].tv_sec = client_msg->attr->atime;
        tv[0].tv_usec = 0;
        tv[1].tv_sec = client_msg->attr->mtime;
        tv[1].tv_usec = 0;

        rv = utimes(filename, tv);
        if (rv < 0) {
            int saved_errno = errno;
            SSH_LOG(SSH_LOG_PROTOCOL,
                    "utimes failed: %s",
                    strerror(saved_errno));
            status = unix_errno_to_ssh_stat(saved_errno);
            sftp_reply_status(client_msg, status, NULL);
            return rv;
        }
#else
        struct _utimbuf tf;

        tf.actime = client_msg->attr->atime;
        tf.modtime = client_msg->attr->mtime;

        rv = _utime(filename, &tf);
        if (rv < 0) {
            int saved_errno = errno;
            SSH_LOG(SSH_LOG_PROTOCOL,
                    "utimes failed: %s",
                    strerror(saved_errno));
            status = unix_errno_to_ssh_stat(saved_errno);
            sftp_reply_status(client_msg, status, NULL);
            return rv;
        }
#endif
    }

    sftp_reply_status(client_msg, status, NULL);
    return ret;
}

static int
process_readlink(sftp_client_message client_msg)
{
    int ret = SSH_OK;
    const char *filename = sftp_client_message_get_filename(client_msg);
    char buf[PATH_MAX];
    int len = -1;
    const char *err_msg = NULL;
    int status = SSH_FX_OK;

    SSH_LOG(SSH_LOG_PROTOCOL, "Processing readlink %s", filename);

    if (filename == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "File name error");
        return SSH_ERROR;
    }

    len = readlink(filename, buf, sizeof(buf) - 1);
    if (len < 0) {
        int saved_errno = errno;
        SSH_LOG(SSH_LOG_PROTOCOL, "readlink failed: %s", strerror(saved_errno));
        status = unix_errno_to_ssh_stat(saved_errno);
        err_msg = ssh_str_error(status);
        sftp_reply_status(client_msg, status, err_msg);
        ret = SSH_ERROR;
    } else {
        buf[len] = '\0';
        sftp_reply_name(client_msg, buf, NULL);
    }

    return ret;
}

/* Note, that this function is using reversed order of the arguments than the
 * OpenSSH sftp server as they have the arguments switched. See
 * section "4.1 sftp: Reversal of arguments to SSH_FXP_SYMLINK' in
 * https://github.com/openssh/openssh-portable/blob/master/PROTOCOL
 * for more information */
static int
process_symlink(sftp_client_message client_msg)
{
    int ret = SSH_OK;
    const char *destpath = sftp_client_message_get_filename(client_msg);
    const char *srcpath = ssh_string_get_char(client_msg->data);
    int status = SSH_FX_OK;
    int rv;

    SSH_LOG(SSH_LOG_PROTOCOL, "processing symlink: src=%s dest=%s",
            srcpath, destpath);

    if (srcpath == NULL || destpath == NULL) {
        sftp_reply_status(client_msg, SSH_FX_NO_SUCH_FILE, "File name error");
        return SSH_ERROR;
    }

    rv = symlink(srcpath, destpath);
    if (rv < 0) {
        int saved_errno = errno;
        status = unix_errno_to_ssh_stat(saved_errno);
        SSH_LOG(SSH_LOG_PROTOCOL, "symlink failed: %s", strerror(saved_errno));
        sftp_reply_status(client_msg, status, "Write error");
        ret = SSH_ERROR;
    } else {
        sftp_reply_status(client_msg, SSH_FX_OK, "write success");
    }

    return ret;
}

static int
process_remove(sftp_client_message client_msg)
{
    int ret = SSH_OK;
    const char *filename = sftp_client_message_get_filename(client_msg);
    int rv;
    int status = SSH_FX_OK;

    SSH_LOG(SSH_LOG_PROTOCOL, "processing remove: %s", filename);

    rv = unlink(filename);
    if (rv < 0) {
        int saved_errno = errno;
        SSH_LOG(SSH_LOG_PROTOCOL, "unlink failed: %s", strerror(saved_errno));
        status = unix_errno_to_ssh_stat(saved_errno);
        ret = SSH_ERROR;
    }

    sftp_reply_status(client_msg, status, NULL);

    return ret;
}

static int
process_unsupposed(sftp_client_message client_msg)
{
    sftp_reply_status(client_msg, SSH_FX_OP_UNSUPPORTED,
                      "Operation not supported");
    SSH_LOG(SSH_LOG_PROTOCOL, "Message type %d not implemented",
            sftp_client_message_get_type(client_msg));
    return SSH_OK;
}

static int
process_extended_statvfs(sftp_client_message client_msg)
{
    const char *path = sftp_client_message_get_filename(client_msg);
    sftp_statvfs_t sftp_statvfs;
    struct statvfs st;
    uint64_t flag;
    int status;
    int rv;

    SSH_LOG(SSH_LOG_PROTOCOL, "processing extended statvfs: %s", path);

    rv = statvfs(path, &st);
    if (rv != 0) {
        int saved_errno = errno;
        SSH_LOG(SSH_LOG_PROTOCOL, "statvfs failed: %s", strerror(saved_errno));
        status = unix_errno_to_ssh_stat(saved_errno);
        sftp_reply_status(client_msg, status, NULL);
        return SSH_ERROR;
    }

    sftp_statvfs = calloc(1, sizeof(struct sftp_statvfs_struct));
    if (sftp_statvfs == NULL) {
        SSH_LOG(SSH_LOG_PROTOCOL, "Failed to allocate statvfs structure");
        sftp_reply_status(client_msg, SSH_FX_FAILURE, NULL);
        return SSH_ERROR;
    }
    flag = (st.f_flag & ST_RDONLY) ? SSH_FXE_STATVFS_ST_RDONLY : 0;
    flag |= (st.f_flag & ST_NOSUID) ? SSH_FXE_STATVFS_ST_NOSUID : 0;

    sftp_statvfs->f_bsize = st.f_bsize;
    sftp_statvfs->f_frsize = st.f_frsize;
    sftp_statvfs->f_blocks = st.f_blocks;
    sftp_statvfs->f_bfree = st.f_bfree;
    sftp_statvfs->f_bavail = st.f_bavail;
    sftp_statvfs->f_files = st.f_files;
    sftp_statvfs->f_ffree = st.f_ffree;
    sftp_statvfs->f_favail = st.f_favail;
    sftp_statvfs->f_fsid = st.f_fsid;
    sftp_statvfs->f_flag = flag;
    sftp_statvfs->f_namemax = st.f_namemax;

    rv = sftp_reply_statvfs(client_msg, sftp_statvfs);
    free(sftp_statvfs);
    if (rv == 0) {
        return SSH_OK;
    }
    return SSH_ERROR;
}

static int
process_extended(sftp_client_message sftp_msg)
{
    int status = SSH_ERROR;
    const char *subtype = sftp_msg->submessage;
    sftp_server_message_callback handler = NULL;

    SSH_LOG(SSH_LOG_PROTOCOL, "processing extended message: %s", subtype);

    for (int i = 0; extended_handlers[i].cb != NULL; i++) {
        if (strcmp(subtype, extended_handlers[i].extended_name) == 0) {
            handler = extended_handlers[i].cb;
            break;
        }
    }
    if (handler != NULL) {
        status = handler(sftp_msg);
        return status;
    }

    sftp_reply_status(sftp_msg, SSH_FX_OP_UNSUPPORTED,
                      "Extended Operation not supported");
    SSH_LOG(SSH_LOG_PROTOCOL, "Extended Message type %s not implemented",
            subtype);
    return SSH_OK;
}

static int
dispatch_sftp_request(sftp_client_message sftp_msg)
{
    int status = SSH_ERROR;
    sftp_server_message_callback handler = NULL;
    uint8_t type = sftp_client_message_get_type(sftp_msg);

    SSH_LOG(SSH_LOG_PROTOCOL, "processing request type: %u", type);

    for (int i = 0; message_handlers[i].cb != NULL; i++) {
        if (type == message_handlers[i].type) {
            handler = message_handlers[i].cb;
            break;
        }
    }

    if (handler != NULL) {
        status = handler(sftp_msg);
    } else {
        sftp_reply_status(sftp_msg, SSH_FX_OP_UNSUPPORTED,
                          "Operation not supported");
        SSH_LOG(SSH_LOG_PROTOCOL, "Message type %u not implemented", type);
        return SSH_OK;
    }

    return status;
}

static int
process_client_message(sftp_client_message client_msg)
{
    int status = SSH_OK;
    if (client_msg == NULL) {
        return SSH_ERROR;
    }

    switch (client_msg->type) {
    case SSH_FXP_EXTENDED:
        status = process_extended(client_msg);
        break;
    default:
        status = dispatch_sftp_request(client_msg);
    }

    if (status != SSH_OK)
        SSH_LOG(SSH_LOG_PROTOCOL,
                "error occurred during processing client message!");

    return status;
}

/**
 * @brief Default subsystem request handler for SFTP subsystem
 *
 * @param[in]  session   The ssh session
 * @param[in]  channel   The existing ssh channel
 * @param[in]  subsystem The subsystem name. Only "sftp" is handled
 * @param[out] userdata  The pointer to sftp_session which will get the
 *                       resulting SFTP session
 *
 * @return SSH_OK when the SFTP server was successfully initialized, SSH_ERROR
 *         otherwise.
 */
int
sftp_channel_default_subsystem_request(ssh_session session,
                                       ssh_channel channel,
                                       const char *subsystem,
                                       void *userdata)
{
    if (strcmp(subsystem, "sftp") == 0) {
        sftp_session *sftp = (sftp_session *)userdata;

        /* initialize sftp session and file handler */
        *sftp = sftp_server_new(session, channel);
        if (*sftp == NULL) {
            return SSH_ERROR;
        }

        return SSH_OK;
    }
    return SSH_ERROR;
}

/**
 * @brief Default data callback for sftp server
 *
 * @param[in] session   The ssh session
 * @param[in] channel   The ssh channel with SFTP session opened
 * @param[in] data      The data to be processed.
 * @param[in] len       The length of input data to be processed
 * @param[in] is_stderr Unused channel flag for stderr flagging
 * @param[in] userdata  The pointer to sftp_session
 *
 * @return number of bytes processed, -1 when error occurs.
 */
int
sftp_channel_default_data_callback(UNUSED_PARAM(ssh_session session),
                                   UNUSED_PARAM(ssh_channel channel),
                                   void *data,
                                   uint32_t len,
                                   UNUSED_PARAM(int is_stderr),
                                   void *userdata)
{
    sftp_session *sftpp = (sftp_session *)userdata;
    sftp_session sftp = NULL;
    sftp_client_message msg;
    int decode_len;
    int rc;

    if (sftpp == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "NULL userdata passed to callback");
        return SSH_ERROR;
    }
    sftp = *sftpp;

    decode_len = sftp_decode_channel_data_to_packet(sftp, data, len);
    if (decode_len == SSH_ERROR)
        return SSH_ERROR;

    msg = sftp_get_client_message_from_packet(sftp);
    rc = process_client_message(msg);
    sftp_client_message_free(msg);
    if (rc != SSH_OK)
        SSH_LOG(SSH_LOG_PROTOCOL, "process sftp failed!");

    return decode_len;
}
#else
/* Not available on Windows for now */
int
sftp_channel_default_data_callback(UNUSED_PARAM(ssh_session session),
                                   UNUSED_PARAM(ssh_channel channel),
                                   UNUSED_PARAM(void *data),
                                   UNUSED_PARAM(uint32_t len),
                                   UNUSED_PARAM(int is_stderr),
                                   UNUSED_PARAM(void *userdata))
{
    return -1;
}

int
sftp_channel_default_subsystem_request(UNUSED_PARAM(ssh_session session),
                                       UNUSED_PARAM(ssh_channel channel),
                                       UNUSED_PARAM(const char *subsystem),
                                       UNUSED_PARAM(void *userdata))
{
    return SSH_ERROR;
}
#endif
