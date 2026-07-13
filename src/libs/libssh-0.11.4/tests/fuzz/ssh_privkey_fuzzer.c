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
#include "libssh/priv.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ssh_key pkey = NULL;
    uint8_t *input = NULL;
    int rc;

    input = bin_to_base64(data, size);
    if (input == NULL) {
        return 1;
    }

    ssh_init();

    rc = ssh_pki_import_privkey_base64((char *)input, NULL, NULL, NULL, &pkey);
    free(input);
    if (rc != SSH_OK) {
        return 1;
    }
    ssh_key_free(pkey);

    ssh_finalize();

    return 0;
}

