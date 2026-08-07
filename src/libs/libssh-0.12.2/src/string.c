/*
 * string.c - ssh string functions
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2003-2008 by Aris Adamantiadis
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

#include <errno.h>
#include <limits.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "libssh/priv.h"
#include "libssh/string.h"

/* String maximum size is 256M */
#define STRING_SIZE_MAX 0x10000000

/**
 * @defgroup libssh_string The SSH string functions
 * @ingroup libssh
 *
 * @brief String manipulations used in libssh.
 *
 * @{
 */

/**
 * @brief Create a new SSH String object.
 *
 * @param[in] size       The size of the string.
 *
 * @return               The newly allocated string, NULL on error.
 */
struct ssh_string_struct *ssh_string_new(size_t size)
{
    struct ssh_string_struct *str = NULL;

    if (size > STRING_SIZE_MAX) {
        errno = EINVAL;
        return NULL;
    }

    str = calloc(1, sizeof(struct ssh_string_struct) + size);
    if (str == NULL) {
        return NULL;
    }

    str->size = htonl((uint32_t)size);

    return str;
}

/**
 * @brief Fill a string with given data. The string should be big enough.
 *
 * @param s        An allocated string to fill with data.
 *
 * @param data     The data to fill the string with.
 *
 * @param len      Size of data.
 *
 * @return         0 on success, < 0 on error.
 */
int ssh_string_fill(struct ssh_string_struct *s, const void *data, size_t len)
{
    if ((s == NULL) || (data == NULL) || (len == 0) ||
        (len > ssh_string_len(s))) {
        return -1;
    }

    memcpy(s->data, data, len);

    return 0;
}

/**
 * @brief Create a ssh string using a C string
 *
 * @param[in] what      The source 0-terminated C string.
 *
 * @return              The newly allocated string, NULL on error with errno
 *                      set.
 *
 * @note The null byte is not copied nor counted in the output string.
 */
struct ssh_string_struct *ssh_string_from_char(const char *what)
{
    struct ssh_string_struct *ptr = NULL;
    size_t len;

    if (what == NULL) {
        errno = EINVAL;
        return NULL;
    }

    len = strlen(what);

    ptr = ssh_string_new(len);
    if (ptr == NULL) {
        return NULL;
    }

    memcpy(ptr->data, what, len);

    return ptr;
}

/**
 * @brief Create a ssh string from an arbitrary data buffer.
 *
 * Allocates a new SSH string of length `len` and copies the provided data
 * into it. If len is 0, returns an empty SSH string. When len > 0, data
 * must not be NULL.
 *
 * @param[in] data     Pointer to the data buffer to copy from. May be NULL
 *                     only when len == 0.
 * @param[in] len      Length of the data buffer to copy.
 *
 * @return             The newly allocated string, NULL on error.
 */
struct ssh_string_struct *ssh_string_from_data(const void *data, size_t len)
{
    struct ssh_string_struct *s = NULL;
    int rc;

    if (len > 0 && data == NULL) {
        errno = EINVAL;
        return NULL;
    }

    s = ssh_string_new(len);
    if (s == NULL) {
        return NULL;
    }

    if (len > 0) {
        rc = ssh_string_fill(s, data, len);
        if (rc != 0) {
            ssh_string_free(s);
            return NULL;
        }
    }

    return s;
}

/**
 * @brief Return the size of a SSH string.
 *
 * @param[in] s         The input SSH string.
 *
 * @return The size of the content of the string, 0 on error.
 */
size_t ssh_string_len(struct ssh_string_struct *s)
{
    size_t size;

    if (s == NULL) {
        return 0;
    }

    size = ntohl(s->size);
    if (size > 0 && size <= STRING_SIZE_MAX) {
        return size;
    }

    return 0;
}

/**
 * @brief Get the string as a C null-terminated string.
 *
 * This is only available as long as the SSH string exists.
 *
 * @param[in] s         The SSH string to get the C string from.
 *
 * @return              The char pointer, NULL on error.
 */
const char *ssh_string_get_char(struct ssh_string_struct *s)
{
    if (s == NULL) {
        return NULL;
    }
    s->data[ssh_string_len(s)] = '\0';

    return (const char *)s->data;
}

/**
 * @brief Convert a SSH string to a C null-terminated string.
 *
 * @param[in] s         The SSH input string.
 *
 * @return              An allocated string pointer, NULL on error with errno
 *                      set.
 *
 * @note If the input SSH string contains zeroes, some parts of the output
 * string may not be readable with regular libc functions.
 */
char *ssh_string_to_char(struct ssh_string_struct *s)
{
    size_t len;
    char *new = NULL;

    if (s == NULL) {
        return NULL;
    }

    len = ssh_string_len(s);
    if (len + 1 < len) {
        return NULL;
    }

    new = malloc(len + 1);
    if (new == NULL) {
        return NULL;
    }
    memcpy(new, s->data, len);
    new[len] = '\0';

    return new;
}

/**
 * @brief Deallocate a char string object.
 *
 * @param[in] s         The string to delete.
 */
void ssh_string_free_char(char *s)
{
    SAFE_FREE(s);
}

/**
 * @brief Copy a string, return a newly allocated string. The caller has to
 *        free the string.
 *
 * @param[in] s         String to copy.
 *
 * @return              Newly allocated copy of the string, NULL on error.
 */
struct ssh_string_struct *ssh_string_copy(struct ssh_string_struct *s)
{
    struct ssh_string_struct *new = NULL;
    size_t len;

    if (s == NULL) {
        return NULL;
    }

    len = ssh_string_len(s);

    new = ssh_string_new(len);
    if (new == NULL) {
        return NULL;
    }

    memcpy(new->data, s->data, len);

    return new;
}

/**
 * @brief Compare two SSH strings.
 *
 * @param[in] s1        The first SSH string to compare.
 * @param[in] s2        The second SSH string to compare.
 *
 * @return              0 if the strings are equal,
 *                      < 0 if s1 is less than s2,
 *                      > 0 if s1 is greater than s2.
 */
int ssh_string_cmp(struct ssh_string_struct *s1, struct ssh_string_struct *s2)
{
    size_t len1, len2, min_len;
    int cmp;

    /* Both are NULL */
    if (s1 == NULL && s2 == NULL) {
        return 0;
    }

    /* Only one is NULL - NULL is considered "less than" non-NULL */
    if (s1 == NULL) {
        return -1;
    } else if (s2 == NULL) {
        return 1;
    }

    /* Get lengths */
    len1 = ssh_string_len(s1);
    len2 = ssh_string_len(s2);
    min_len = MIN(len1, len2);

    /* Compare data up to the shorter length */
    if (min_len > 0) {
        cmp = memcmp(s1->data, s2->data, min_len);
        if (cmp != 0) {
            return cmp;
        }
    }

    /* If common prefix is equal, compare lengths */
    if (len1 < len2) {
        return -1;
    } else if (len1 > len2) {
        return 1;
    }

    return 0;
}

/**
 * @brief Destroy the data in a string so it couldn't appear in a core dump.
 *
 * @param[in] s         The string to burn.
 */
void ssh_string_burn(struct ssh_string_struct *s)
{
    if (s == NULL || s->size == 0) {
        return;
    }

    ssh_burn(s->data, ssh_string_len(s));
}

/**
 * @brief Get the payload of the string.
 *
 * @param s             The string to get the data from.
 *
 * @return              Return the data of the string or NULL on error.
 */
void *ssh_string_data(struct ssh_string_struct *s)
{
    if (s == NULL) {
        return NULL;
    }

    return s->data;
}

/**
 * @brief Deallocate a SSH string object.
 *
 * \param[in] s         The SSH string to delete.
 */
void ssh_string_free(struct ssh_string_struct *s)
{
    SAFE_FREE(s);
}

/** @} */
