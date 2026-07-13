/*
 * Copyright 2022 Jakub Jelen <jjelen@redhat.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBSSH_STATIC 1
#include "libssh/libssh.h"
#include "knownhosts.c"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *hostname = NULL;
    const uint8_t *hostname_end = NULL;
    size_t hostname_len = 0;
    char filename[256];
    struct ssh_list *entries = NULL;
    struct ssh_iterator *it = NULL;
    FILE *fp = NULL;

    /* Interpret the first part of the string (until the first NULL byte)
     * as a hostname we are searching for in the file */
    hostname_end = memchr(data, '\0', size);
    if (hostname_end == NULL) {
        return 1;
    }
    hostname_len = hostname_end - data + 1;
    if (hostname_len > 253) {
        /* This is the maximum valid length of a hostname */
        return 1;
    }
    hostname = malloc(hostname_len);
    if (hostname == NULL) {
        return 1;
    }
    memcpy(hostname, data, hostname_len);

    snprintf(filename, sizeof(filename), "/tmp/libfuzzer.%d", getpid());
    fp = fopen(filename, "wb");
    if (!fp) {
        free(hostname);
        return 1;
    }
    fwrite(data + hostname_len, size - hostname_len, 1, fp);
    fclose(fp);

    ssh_init();

    ssh_known_hosts_read_entries(hostname, filename, &entries);
    for (it = ssh_list_get_iterator(entries);
         it != NULL;
         it = ssh_list_get_iterator(entries)) {
        struct ssh_knownhosts_entry *entry = NULL;

        entry = ssh_iterator_value(struct ssh_knownhosts_entry *, it);
        ssh_knownhosts_entry_free(entry);
        ssh_list_remove(entries, it);
    }
    ssh_list_free(entries);

    ssh_finalize();

    free(hostname);
    unlink(filename);

    return 0;
}
