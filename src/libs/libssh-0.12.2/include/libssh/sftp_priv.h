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

#include <stdbool.h>

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

/**
 * @brief Receive the response of an sftp request
 *
 * In blocking mode, if the response hasn't arrived at the time of call, this
 * function waits for the response to arrive.
 *
 * @param sftp           The sftp session via which the request was sent.
 *
 * @param id             The request identifier of the request whose
 *                       corresponding response is required.
 *
 * @param blocking       Flag to indicate the operating mode. true indicates
 *                       blocking mode and false indicates non-blocking mode
 *
 * @param msg_ptr        Pointer to the location to store the response message.
 *                       In case of success, the message is allocated
 *                       dynamically and must be freed (using
 *                       sftp_message_free()) by the caller after usage. In case
 *                       of failure, this is left untouched.
 *
 * @returns SSH_OK on success
 * @returns SSH_ERROR on failure with the sftp and ssh errors set
 * @returns SSH_AGAIN in case of non-blocking mode if the response hasn't
 *          arrived yet.
 *
 * @warning In blocking mode, this may block indefinitely for an invalid request
 *          identifier.
 */
int sftp_recv_response_msg(sftp_session sftp,
                           uint32_t id,
                           bool blocking,
                           sftp_message *msg_ptr);

/**
 * @brief Assigns a new SFTP ID for new requests and assures there is no
 *        collision between them.
 *
 * @param sftp           The sftp session handle.
 * @param id_out         Pointer to store the new ID.
 *
 * @returns SSH_OK on success with the new ID stored in *id
 * @returns SSH_ERROR on failure with the sftp and ssh errors set
 */
int sftp_get_new_id(sftp_session sftp, uint32_t *id_out);

sftp_status_message parse_status_msg(sftp_message msg);

void status_msg_free(sftp_status_message status);

#ifdef __cplusplus
}
#endif

#endif /* SFTP_PRIV_H */
