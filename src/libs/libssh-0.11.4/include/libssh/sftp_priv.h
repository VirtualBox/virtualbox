/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2003-2008 by Aris Adamantiadis
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef SFTP_PRIV_H
#define SFTP_PRIV_H

#ifdef __cplusplus
extern "C" {
#endif

sftp_packet sftp_packet_read(sftp_session sftp);
int sftp_packet_write(sftp_session sftp, uint8_t type, ssh_buffer payload);
void sftp_packet_free(sftp_packet packet);
int buffer_add_attributes(ssh_buffer buffer, sftp_attributes attr);
sftp_attributes sftp_parse_attr(sftp_session session,
                                ssh_buffer buf,
                                int expectname);
/**
 * @brief Reply to the SSH_FXP_INIT message with the SSH_FXP_VERSION message
 *
 * @param  client_msg         The pointer to client message.
 *
 * @return                    0 on success, < 0 on error with ssh and sftp error set.
 *
 * @see sftp_get_error()
 */
int sftp_reply_version(sftp_client_message client_msg);
/**
 * @brief Decode the data from channel buffer into sftp read_packet.
 *
 * @param  sftp         The sftp session handle.
 *
 * @param  data         The pointer to the data buffer of channel.
 * @param  len          The data buffer length
 *
 * @return              Length of data decoded.
 */
int sftp_decode_channel_data_to_packet(sftp_session sftp, void *data, uint32_t len);

void sftp_set_error(sftp_session sftp, int errnum);

void sftp_message_free(sftp_message msg);

int sftp_read_and_dispatch(sftp_session sftp);

sftp_message sftp_dequeue(sftp_session sftp, uint32_t id);

/*
 * Assigns a new SFTP ID for new requests and assures there is no collision
 * between them.
 * Returns a new ID ready to use in a request
 */
static inline uint32_t sftp_get_new_id(sftp_session session)
{
    return ++session->id_counter;
}

sftp_status_message parse_status_msg(sftp_message msg);

void status_msg_free(sftp_status_message status);

#ifdef __cplusplus
}
#endif

#endif /* SFTP_PRIV_H */
