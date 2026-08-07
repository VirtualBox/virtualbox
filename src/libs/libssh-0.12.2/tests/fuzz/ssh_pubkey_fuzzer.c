/*
 * Copyright 2023 Jakub Jelen <jjelen@redhat.com>
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
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBSSH_STATIC 1
#include "libssh/libssh.h"
#include "libssh/misc.h"

#include "nallocinc.c"

static void _fuzz_finalize(void)
{
    ssh_finalize();
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;

    nalloc_init(*argv[0]);

    ssh_init();

    atexit(_fuzz_finalize);

    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ssh_key pkey = NULL;
    char *filename = NULL;
    int fd;
    int rc;
    ssize_t sz;

    filename = strdup("/tmp/libssh_pubkey_XXXXXX");
    if (filename == NULL) {
        return -1;
    }
    fd = mkstemp(filename);
    if (fd  == -1) {
        free(filename);
        close(fd);
        return -1;
    }
    sz = ssh_writen(fd, data, size);
    close(fd);
    if (sz == SSH_ERROR) {
        unlink(filename);
        free(filename);
        return -1;
    }

    assert(nalloc_start(data, size) > 0);

    rc = ssh_pki_import_pubkey_file(filename, &pkey);
    if (rc != SSH_OK) {
        goto out;
    }
    ssh_key_free(pkey);

out:
    unlink(filename);
    free(filename);
    nalloc_end();
    return 0;
}

