/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 Praneeth Sarode <praneethsarode@gmail.com>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 2.1 of the License.
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

#ifndef SK_USBHID_H
#define SK_USBHID_H

/**
 * @brief Get the USB-HID security key callbacks.
 *
 * This function returns a pointer to the implementation of
 * security key callbacks for FIDO2/U2F devices using the USB-HID
 * protocol.
 *
 * @return Pointer to the ssh_sk_callbacks_struct
 *
 * @see ssh_sk_callbacks_struct
 */
const struct ssh_sk_callbacks_struct *ssh_sk_get_usbhid_callbacks(void);

#endif /* SK_USBHID_H */
