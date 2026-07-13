/*
 * sftp_common.c - Secure FTP functions which are private and are used
 *                 internally by other sftp api functions spread across
 *                 various source files.
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2005-2008 by Aris Adamantiadis
 * Copyright (c) 2008-2018 by Andreas Schneider <asn@cryptomilk.org>
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

#include <ctype.h>

#include "libssh/sftp.h"
#include "libssh/sftp_priv.h"
#include "libssh/buffer.h"
#include "libssh/session.h"
#include "libssh/bytearray.h"

#ifdef WITH_SFTP

/* Buffer size maximum is 256M */
#define SFTP_PACKET_SIZE_MAX 0x10000000

sftp_packet sftp_packet_read(sftp_session sftp)
{
    uint8_t tmpbuf[4];
    uint8_t *buffer = NULL;
    sftp_packet packet = sftp->read_packet;
    uint32_t size;
    int nread;
    bool is_eof;
    int rc;

    packet->sftp = sftp;

    /*
     * If the packet has a payload, then just reinit the buffer, otherwise
     * allocate a new one.
     */
    if (packet->payload != NULL) {
        rc = ssh_buffer_reinit(packet->payload);
        if (rc != 0) {
            ssh_set_error_oom(sftp->session);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }
    } else {
        packet->payload = ssh_buffer_new();
        if (packet->payload == NULL) {
            ssh_set_error_oom(sftp->session);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }
    }

    nread = 0;
    do {
        int s;

        /* read from channel until 4 bytes have been read or an error occurs */
        s = ssh_channel_read(sftp->channel, tmpbuf + nread, 4 - nread, 0);
        if (s < 0) {
            goto error;
        } else if (s == 0) {
            is_eof = ssh_channel_is_eof(sftp->channel);
            if (is_eof) {
                ssh_set_error(sftp->session,
                              SSH_FATAL,
                              "Received EOF while reading sftp packet size");
                sftp_set_error(sftp, SSH_FX_EOF);
                goto error;
            } else {
                ssh_set_error(sftp->session,
                              SSH_FATAL,
                              "Timeout while reading sftp packet size");
                sftp_set_error(sftp, SSH_FX_FAILURE);
                goto error;
            }
        } else {
            nread += s;
        }
    } while (nread < 4);

    size = PULL_BE_U32(tmpbuf, 0);
    if (size == 0 || size > SFTP_PACKET_SIZE_MAX) {
        ssh_set_error(sftp->session, SSH_FATAL, "Invalid sftp packet size!");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        goto error;
    }

    do {
        nread = ssh_channel_read(sftp->channel, tmpbuf, 1, 0);
        if (nread < 0) {
            goto error;
        } else if (nread == 0) {
            is_eof = ssh_channel_is_eof(sftp->channel);
            if (is_eof) {
                ssh_set_error(sftp->session,
                              SSH_FATAL,
                              "Received EOF while reading sftp packet type");
                sftp_set_error(sftp, SSH_FX_EOF);
                goto error;
            } else {
                ssh_set_error(sftp->session,
                              SSH_FATAL,
                              "Timeout while reading sftp packet type");
                sftp_set_error(sftp, SSH_FX_FAILURE);
                goto error;
            }
        }
    } while (nread < 1);

    packet->type = tmpbuf[0];

    /* Remove the packet type size */
    size -= sizeof(uint8_t);

    /* Allocate the receive buffer from payload */
    buffer = ssh_buffer_allocate(packet->payload, size);
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        goto error;
    }
    while (size > 0 && size < SFTP_PACKET_SIZE_MAX) {
        nread = ssh_channel_read(sftp->channel, buffer, size, 0);
        if (nread < 0) {
            /* TODO: check if there are cases where an error needs to be set here */
            goto error;
        }

        if (nread > 0) {
            buffer += nread;
            size -= nread;
        } else { /* nread == 0 */
            /* Retry the reading unless the remote was closed */
            is_eof = ssh_channel_is_eof(sftp->channel);
            if (is_eof) {
                ssh_set_error(sftp->session,
                              SSH_REQUEST_DENIED,
                              "Received EOF while reading sftp packet");
                sftp_set_error(sftp, SSH_FX_EOF);
                goto error;
            } else {
                ssh_set_error(sftp->session,
                              SSH_FATAL,
                              "Timeout while reading sftp packet");
                sftp_set_error(sftp, SSH_FX_FAILURE);
                goto error;
            }
        }
    }

    return packet;
error:
    ssh_buffer_reinit(packet->payload);
    return NULL;
}

int sftp_packet_write(sftp_session sftp, uint8_t type, ssh_buffer payload)
{
    uint8_t header[5] = {0};
    uint32_t payload_size;
    int size;
    int rc;

    /* Add size of type */
    payload_size = ssh_buffer_get_len(payload) + sizeof(uint8_t);
    PUSH_BE_U32(header, 0, payload_size);
    PUSH_BE_U8(header, 4, type);

    rc = ssh_buffer_prepend_data(payload, header, sizeof(header));
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    size = ssh_channel_write(sftp->channel,
                             ssh_buffer_get(payload),
                             ssh_buffer_get_len(payload));
    if (size < 0) {
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    if ((uint32_t)size != ssh_buffer_get_len(payload)) {
        SSH_LOG(SSH_LOG_PACKET,
                "Had to write %" PRIu32 " bytes, wrote only %d",
                ssh_buffer_get_len(payload),
                size);
    }

    return size;
}

void sftp_packet_free(sftp_packet packet)
{
    if (packet == NULL) {
        return;
    }

    SSH_BUFFER_FREE(packet->payload);
    free(packet);
}

int buffer_add_attributes(ssh_buffer buffer, sftp_attributes attr)
{
    uint32_t flags = (attr ? attr->flags : 0);
    int rc;

    flags &= (SSH_FILEXFER_ATTR_SIZE | SSH_FILEXFER_ATTR_UIDGID |
              SSH_FILEXFER_ATTR_PERMISSIONS | SSH_FILEXFER_ATTR_ACMODTIME);

    rc = ssh_buffer_pack(buffer, "d", flags);
    if (rc != SSH_OK) {
        return -1;
    }

    if (attr != NULL) {
        if (flags & SSH_FILEXFER_ATTR_SIZE) {
            rc = ssh_buffer_pack(buffer, "q", attr->size);
            if (rc != SSH_OK) {
                return -1;
            }
        }

        if (flags & SSH_FILEXFER_ATTR_UIDGID) {
            rc = ssh_buffer_pack(buffer, "dd", attr->uid, attr->gid);
            if (rc != SSH_OK) {
                return -1;
            }
        }

        if (flags & SSH_FILEXFER_ATTR_PERMISSIONS) {
            rc = ssh_buffer_pack(buffer, "d", attr->permissions);
            if (rc != SSH_OK) {
                return -1;
            }
        }

        if (flags & SSH_FILEXFER_ATTR_ACMODTIME) {
            rc = ssh_buffer_pack(buffer, "dd", attr->atime, attr->mtime);
            if (rc != SSH_OK) {
                return -1;
            }
        }
    }

    return 0;
}

/*
 * Parse the attributes from a payload from some messages. It is coded on
 * baselines from the protocol version 4.
 * This code is more or less dead but maybe we will need it in the future.
 */
static sftp_attributes sftp_parse_attr_4(sftp_session sftp,
                                         ssh_buffer buf,
                                         int expectnames)
{
    sftp_attributes attr = NULL;
    ssh_string owner = NULL;
    ssh_string group = NULL;
    uint32_t flags = 0;
    int ok = 0;

    /* unused member variable */
    (void) expectnames;

    attr = calloc(1, sizeof(struct sftp_attributes_struct));
    if (attr == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    /* This isn't really a loop, but it is like a try..catch.. */
    do {
        if (ssh_buffer_get_u32(buf, &flags) != 4) {
            break;
        }

        flags = ntohl(flags);
        attr->flags = flags;

        if (flags & SSH_FILEXFER_ATTR_SIZE) {
            if (ssh_buffer_get_u64(buf, &attr->size) != 8) {
                break;
            }
            attr->size = ntohll(attr->size);
        }

        if (flags & SSH_FILEXFER_ATTR_OWNERGROUP) {
            owner = ssh_buffer_get_ssh_string(buf);
            if (owner == NULL) {
                break;
            }
            attr->owner = ssh_string_to_char(owner);
            SSH_STRING_FREE(owner);
            if (attr->owner == NULL) {
                break;
            }

            group = ssh_buffer_get_ssh_string(buf);
            if (group == NULL) {
                break;
            }
            attr->group = ssh_string_to_char(group);
            SSH_STRING_FREE(group);
            if (attr->group == NULL) {
                break;
            }
        }

        if (flags & SSH_FILEXFER_ATTR_PERMISSIONS) {
            if (ssh_buffer_get_u32(buf, &attr->permissions) != 4) {
                break;
            }
            attr->permissions = ntohl(attr->permissions);

            /* FIXME on windows! */
            switch (attr->permissions & SSH_S_IFMT) {
            case SSH_S_IFSOCK:
            case SSH_S_IFBLK:
            case SSH_S_IFCHR:
            case SSH_S_IFIFO:
                attr->type = SSH_FILEXFER_TYPE_SPECIAL;
                break;
            case SSH_S_IFLNK:
                attr->type = SSH_FILEXFER_TYPE_SYMLINK;
                break;
            case SSH_S_IFREG:
                attr->type = SSH_FILEXFER_TYPE_REGULAR;
                break;
            case SSH_S_IFDIR:
                attr->type = SSH_FILEXFER_TYPE_DIRECTORY;
                break;
            default:
                attr->type = SSH_FILEXFER_TYPE_UNKNOWN;
                break;
            }
        }

        if (flags & SSH_FILEXFER_ATTR_ACCESSTIME) {
            if (ssh_buffer_get_u64(buf, &attr->atime64) != 8) {
                break;
            }
            attr->atime64 = ntohll(attr->atime64);

            if (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES) {
                if (ssh_buffer_get_u32(buf, &attr->atime_nseconds) != 4) {
                    break;
                }
                attr->atime_nseconds = ntohl(attr->atime_nseconds);
            }
        }

        if (flags & SSH_FILEXFER_ATTR_CREATETIME) {
            if (ssh_buffer_get_u64(buf, &attr->createtime) != 8) {
                break;
            }
            attr->createtime = ntohll(attr->createtime);

            if (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES) {
                if (ssh_buffer_get_u32(buf, &attr->createtime_nseconds) != 4) {
                    break;
                }
                attr->createtime_nseconds = ntohl(attr->createtime_nseconds);
            }
        }

        if (flags & SSH_FILEXFER_ATTR_MODIFYTIME) {
            if (ssh_buffer_get_u64(buf, &attr->mtime64) != 8) {
                break;
            }
            attr->mtime64 = ntohll(attr->mtime64);

            if (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES) {
                if (ssh_buffer_get_u32(buf, &attr->mtime_nseconds) != 4) {
                    break;
                }
                attr->mtime_nseconds = ntohl(attr->mtime_nseconds);
            }
        }

        if (flags & SSH_FILEXFER_ATTR_ACL) {
            if ((attr->acl = ssh_buffer_get_ssh_string(buf)) == NULL) {
                break;
            }
        }

        if (flags & SSH_FILEXFER_ATTR_EXTENDED) {
            if (ssh_buffer_get_u32(buf,&attr->extended_count) != 4) {
                break;
            }
            attr->extended_count = ntohl(attr->extended_count);

            while (attr->extended_count &&
                   (attr->extended_type = ssh_buffer_get_ssh_string(buf)) &&
                   (attr->extended_data = ssh_buffer_get_ssh_string(buf))) {
                attr->extended_count--;
            }

            if (attr->extended_count) {
                break;
            }
        }
        ok = 1;
    } while (0);

    if (ok == 0) {
        /* break issued somewhere */
        SSH_STRING_FREE(attr->acl);
        SSH_STRING_FREE(attr->extended_type);
        SSH_STRING_FREE(attr->extended_data);
        SAFE_FREE(attr->owner);
        SAFE_FREE(attr->group);
        SAFE_FREE(attr);

        ssh_set_error(sftp->session, SSH_FATAL, "Invalid ATTR structure");

        return NULL;
    }

    return attr;
}

enum sftp_longname_field_e {
    SFTP_LONGNAME_PERM = 0,
    SFTP_LONGNAME_FIXME,
    SFTP_LONGNAME_OWNER,
    SFTP_LONGNAME_GROUP,
    SFTP_LONGNAME_SIZE,
    SFTP_LONGNAME_DATE,
    SFTP_LONGNAME_TIME,
    SFTP_LONGNAME_NAME,
};

static char * sftp_parse_longname(const char *longname,
                                  enum sftp_longname_field_e longname_field)
{
    const char *p = NULL, *q = NULL;
    size_t len, field = 0;

    if (longname == NULL || longname_field < SFTP_LONGNAME_PERM ||
        longname_field > SFTP_LONGNAME_NAME) {
        return NULL;
    }

    p = longname;
    /*
     * Find the beginning of the field which is specified
     * by sftp_longname_field_e.
     */
    while (*p != '\0' && field != longname_field) {
        if (isspace(*p)) {
            field++;
            p++;
            while (*p != '\0' && isspace(*p)) {
                p++;
            }
        } else {
            p++;
        }
    }

    /* If we reached NULL before we got our field fail */
    if (field != longname_field) {
        return NULL;
    }

    q = p;
    while (*q != '\0' && !isspace(*q)) {
        q++;
    }

    len = q - p;

    return strndup(p, len);
}

/* sftp version 0-3 code. It is different from the v4 */
/* maybe a paste of the draft is better than the code */
/*
        uint32   flags
        uint64   size           present only if flag SSH_FILEXFER_ATTR_SIZE
        uint32   uid            present only if flag SSH_FILEXFER_ATTR_UIDGID
        uint32   gid            present only if flag SSH_FILEXFER_ATTR_UIDGID
        uint32   permissions    present only if flag SSH_FILEXFER_ATTR_PERMISSIONS
        uint32   atime          present only if flag SSH_FILEXFER_ACMODTIME
        uint32   mtime          present only if flag SSH_FILEXFER_ACMODTIME
        uint32   extended_count present only if flag SSH_FILEXFER_ATTR_EXTENDED
        string   extended_type
        string   extended_data
        ...      more extended data (extended_type - extended_data pairs),
                   so that number of pairs equals extended_count              */
static sftp_attributes sftp_parse_attr_3(sftp_session sftp,
                                         ssh_buffer buf,
                                         int expectname)
{
    sftp_attributes attr;
    int rc;

    attr = calloc(1, sizeof(struct sftp_attributes_struct));
    if (attr == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    if (expectname) {
        rc = ssh_buffer_unpack(buf, "ss",
                               &attr->name,
                               &attr->longname);
        if (rc != SSH_OK){
            goto error;
        }
        SSH_LOG(SSH_LOG_DEBUG, "Name: %s", attr->name);

        /* Set owner and group if we talk to openssh and have the longname */
        if (ssh_get_openssh_version(sftp->session)) {
            attr->owner = sftp_parse_longname(attr->longname,
                                              SFTP_LONGNAME_OWNER);
            if (attr->owner == NULL) {
                goto error;
            }

            attr->group = sftp_parse_longname(attr->longname,
                                              SFTP_LONGNAME_GROUP);
            if (attr->group == NULL) {
                goto error;
            }
        }
    }

    rc = ssh_buffer_unpack(buf, "d", &attr->flags);
    if (rc != SSH_OK){
        goto error;
    }
    SSH_LOG(SSH_LOG_DEBUG, "Flags: %.8" PRIx32, attr->flags);

    if (attr->flags & SSH_FILEXFER_ATTR_SIZE) {
        rc = ssh_buffer_unpack(buf, "q", &attr->size);
        if(rc != SSH_OK) {
            goto error;
        }
        SSH_LOG(SSH_LOG_DEBUG, "Size: %" PRIu64, (uint64_t)attr->size);
    }

    if (attr->flags & SSH_FILEXFER_ATTR_UIDGID) {
        rc = ssh_buffer_unpack(buf, "dd",
                               &attr->uid,
                               &attr->gid);
        if (rc != SSH_OK) {
            goto error;
        }
    }

    if (attr->flags & SSH_FILEXFER_ATTR_PERMISSIONS) {
        rc = ssh_buffer_unpack(buf, "d", &attr->permissions);
        if (rc != SSH_OK) {
            goto error;
        }

        switch (attr->permissions & SSH_S_IFMT) {
        case SSH_S_IFSOCK:
        case SSH_S_IFBLK:
        case SSH_S_IFCHR:
        case SSH_S_IFIFO:
            attr->type = SSH_FILEXFER_TYPE_SPECIAL;
            break;
        case SSH_S_IFLNK:
            attr->type = SSH_FILEXFER_TYPE_SYMLINK;
            break;
        case SSH_S_IFREG:
            attr->type = SSH_FILEXFER_TYPE_REGULAR;
            break;
        case SSH_S_IFDIR:
            attr->type = SSH_FILEXFER_TYPE_DIRECTORY;
            break;
        default:
            attr->type = SSH_FILEXFER_TYPE_UNKNOWN;
            break;
        }
    }

    if (attr->flags & SSH_FILEXFER_ATTR_ACMODTIME) {
        rc = ssh_buffer_unpack(buf, "dd",
                               &attr->atime,
                               &attr->mtime);
        if (rc != SSH_OK) {
            goto error;
        }
    }

    if (attr->flags & SSH_FILEXFER_ATTR_EXTENDED) {
        rc = ssh_buffer_unpack(buf, "d", &attr->extended_count);
        if (rc != SSH_OK) {
            goto error;
        }

        if (attr->extended_count > 0) {
            rc = ssh_buffer_unpack(buf, "ss",
                                   &attr->extended_type,
                                   &attr->extended_data);
            if (rc != SSH_OK) {
                goto error;
            }
            attr->extended_count--;
        }
        /* just ignore the remaining extensions */

        while (attr->extended_count > 0) {
            ssh_string tmp1,tmp2;
            rc = ssh_buffer_unpack(buf, "SS", &tmp1, &tmp2);
            if (rc != SSH_OK){
                goto error;
            }
            SAFE_FREE(tmp1);
            SAFE_FREE(tmp2);
            attr->extended_count--;
        }
    }

    return attr;

error:
    SSH_STRING_FREE(attr->extended_type);
    SSH_STRING_FREE(attr->extended_data);
    SAFE_FREE(attr->name);
    SAFE_FREE(attr->longname);
    SAFE_FREE(attr->owner);
    SAFE_FREE(attr->group);
    SAFE_FREE(attr);
    ssh_set_error(sftp->session, SSH_FATAL, "Invalid ATTR structure");
    sftp_set_error(sftp, SSH_FX_FAILURE);

    return NULL;
}

sftp_attributes sftp_parse_attr(sftp_session session,
                                ssh_buffer buf,
                                int expectname)
{
    switch (session->version) {
    case 4:
        return sftp_parse_attr_4(session, buf, expectname);
    case 3:
    case 2:
    case 1:
    case 0:
        return sftp_parse_attr_3(session, buf, expectname);
    default:
        ssh_set_error(session->session, SSH_FATAL,
                      "Version %d unsupported by client",
                      session->server_version);
        return NULL;
    }

    return NULL;
}

void sftp_set_error(sftp_session sftp, int errnum)
{
    if (sftp != NULL) {
        sftp->errnum = errnum;
    }
}

void sftp_message_free(sftp_message msg)
{
    if (msg == NULL) {
        return;
    }

    SSH_BUFFER_FREE(msg->payload);
    SAFE_FREE(msg);
}

static sftp_request_queue request_queue_new(sftp_message msg)
{
    sftp_request_queue queue = NULL;

    queue = calloc(1, sizeof(struct sftp_request_queue_struct));
    if (queue == NULL) {
        ssh_set_error_oom(msg->sftp->session);
        sftp_set_error(msg->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    queue->message = msg;

    return queue;
}

static void request_queue_free(sftp_request_queue queue)
{
    if (queue == NULL) {
        return;
    }

    ZERO_STRUCTP(queue);
    SAFE_FREE(queue);
}

static int
sftp_enqueue(sftp_session sftp, sftp_message msg)
{
    sftp_request_queue queue = NULL;
    sftp_request_queue ptr;

    queue = request_queue_new(msg);
    if (queue == NULL) {
        return -1;
    }

    SSH_LOG(SSH_LOG_PACKET,
            "Queued msg id %" PRIu32 " type %d",
            msg->id, msg->packet_type);

    if(sftp->queue == NULL) {
        sftp->queue = queue;
    } else {
        ptr = sftp->queue;
        while(ptr->next) {
            ptr=ptr->next; /* find end of linked list */
        }
        ptr->next = queue; /* add it on bottom */
    }

    return 0;
}

/*
 * Pulls a message from the queue based on the ID.
 * Returns NULL if no message has been found.
 */
sftp_message sftp_dequeue(sftp_session sftp, uint32_t id)
{
    sftp_request_queue prev = NULL;
    sftp_request_queue queue;
    sftp_message msg;

    if(sftp->queue == NULL) {
        return NULL;
    }

    queue = sftp->queue;
    while (queue) {
        if (queue->message->id == id) {
            /* remove from queue */
            if (prev == NULL) {
                sftp->queue = queue->next;
            } else {
                prev->next = queue->next;
            }
            msg = queue->message;
            request_queue_free(queue);
            SSH_LOG(SSH_LOG_PACKET,
                    "Dequeued msg id %" PRIu32 " type %d",
                    msg->id,
                    msg->packet_type);
            return msg;
        }
        prev = queue;
        queue = queue->next;
    }

    return NULL;
}

static sftp_message sftp_get_message(sftp_packet packet)
{
    sftp_session sftp = packet->sftp;
    sftp_message msg = NULL;
    int rc;

    switch (packet->type) {
    case SSH_FXP_STATUS:
    case SSH_FXP_HANDLE:
    case SSH_FXP_DATA:
    case SSH_FXP_ATTRS:
    case SSH_FXP_NAME:
    case SSH_FXP_EXTENDED_REPLY:
        break;
    default:
        ssh_set_error(packet->sftp->session,
                      SSH_FATAL,
                      "Unknown packet type %d",
                      packet->type);
        sftp_set_error(packet->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    msg = calloc(1, sizeof(struct sftp_message_struct));
    if (msg == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(packet->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    msg->sftp = packet->sftp;
    msg->packet_type = packet->type;

    /* Move the payload from the packet to the message */
    msg->payload = packet->payload;
    packet->payload = NULL;

    rc = ssh_buffer_unpack(msg->payload, "d", &msg->id);
    if (rc != SSH_OK) {
        ssh_set_error(packet->sftp->session, SSH_FATAL,
                "Invalid packet %d: no ID", packet->type);
        sftp_message_free(msg);
        sftp_set_error(packet->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    SSH_LOG(SSH_LOG_PACKET,
            "Packet with id %" PRIu32 " type %d",
            msg->id,
            msg->packet_type);

    return msg;
}

int sftp_read_and_dispatch(sftp_session sftp)
{
    sftp_packet packet = NULL;
    sftp_message msg = NULL;

    packet = sftp_packet_read(sftp);
    if (packet == NULL) {
        /* something nasty happened reading the packet */
        return -1;
    }

    msg = sftp_get_message(packet);
    if (msg == NULL) {
        return -1;
    }

    if (sftp_enqueue(sftp, msg) < 0) {
        sftp_message_free(msg);
        return -1;
    }

    return 0;
}

sftp_status_message parse_status_msg(sftp_message msg)
{
    sftp_status_message status = NULL;
    int rc;

    if (msg->packet_type != SSH_FXP_STATUS) {
        ssh_set_error(msg->sftp->session, SSH_FATAL,
                      "Not a ssh_fxp_status message passed in!");
        sftp_set_error(msg->sftp, SSH_FX_BAD_MESSAGE);
        return NULL;
    }

    status = calloc(1, sizeof(struct sftp_status_message_struct));
    if (status == NULL) {
        ssh_set_error_oom(msg->sftp->session);
        sftp_set_error(msg->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    status->id = msg->id;
    rc = ssh_buffer_unpack(msg->payload, "d",
                           &status->status);
    if (rc != SSH_OK) {
        SAFE_FREE(status);
        ssh_set_error(msg->sftp->session, SSH_FATAL,
                      "Invalid SSH_FXP_STATUS message");
        sftp_set_error(msg->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = ssh_buffer_unpack(msg->payload, "ss",
                           &status->errormsg,
                           &status->langmsg);

    if (rc != SSH_OK && msg->sftp->version >= 3) {
        SSH_LOG(SSH_LOG_WARN,
                "Invalid SSH_FXP_STATUS message. Missing error message.");
    }

    if (status->errormsg == NULL)
        status->errormsg = strdup("No error message in packet");

    if (status->langmsg == NULL)
        status->langmsg = strdup("");

    if (status->errormsg == NULL || status->langmsg == NULL) {
        ssh_set_error_oom(msg->sftp->session);
        sftp_set_error(msg->sftp, SSH_FX_FAILURE);
        status_msg_free(status);
        return NULL;
    }

    return status;
}

void status_msg_free(sftp_status_message status)
{
    if (status == NULL) {
        return;
    }

    SAFE_FREE(status->errormsg);
    SAFE_FREE(status->langmsg);
    SAFE_FREE(status);
}

#endif /* WITH_SFTP */
