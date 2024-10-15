/* $Id: tstRTCrPkix-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT testcase - Crypto - Public-Key Infrastructure \#1.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/crypto/pkix.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/crypto/key.h>
#include <iprt/crypto/digest.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;

/**
 * Key pairs to use when testing.
 */
static const struct { unsigned cBits; const char *pszPrivateKey, *pszPublicKey, *pszPassword; } g_aKeyPairs[] =
{
    {
        4096,
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "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\n"
        "-----END RSA PRIVATE KEY-----\n",
        "-----BEGIN PUBLIC KEY-----\n"
        "MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEA1SOurMTVz033GGi+5VrM\n"
        "b0SnU7Dj49ZQCKSuxaSFK4tvbZXQBRSgwC1PcQVyt3GdoC71i3O4f9TxaA870icC\n"
        "IY7cqf4LKL9uB5Vga2SNMfx3+KqcJVt9LFsghXfLocdfV1k+xeDVGcSP7uUvnXoI\n"
        "ZyeS8puqoRYNiua1UT+ddXwihTId+6O9Q8IxcCPWkqW89LYBQVFqqMYoWzNcbEct\n"
        "Y6WpPzZk3er+AvMekBD409LbtT7jTrzIGd6eQ0aF2MyVA6lOwe3u99Ubo/FTpule\n"
        "/FQ5LXaEmlHPfDbIw+LRArdYgjoQU9l4SFajm0VbIKd2LFn5SRXHTbtAoKX2zpao\n"
        "i8GF3u8VR/EmmTPYFHr2gUoLuyeTaY56OG/5ns7N/NRzOX1d1lNRFcQYNCXPEtqa\n"
        "UfUfMJU4Jqp1LOEcd1xMkOUh8lc7DyvUfhry+SAcxB5SxcyjdWEXpj4G12/N3f6v\n"
        "sRoZNTFt5j0hsbiOAOFykgN0a2OF77bsd975e1mxkqXJ9A0sbB8EXsD2PSrUZ7Pt\n"
        "+T9CiQGOjqVUg2Vr1jevcQRHe5ed/R+B2jp6MjYjbr7cKqcXaRxEprGl+U5kIygq\n"
        "l93DTgQaXwX/ZjXmwjXvQ0W4OxxexqyW6YvDBYeNKxstuM5qfgzYf7FD/8lZYkyM\n"
        "AXELgpCqC92xlTbWpRVNpXcCAwEAAQ==\n"
        "-----END PUBLIC KEY-----\n",
        NULL
    },
    {
        2048,
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIIEogIBAAKCAQEA06LAmfLBnRldEQF6E9CcMisCiaaDco0fYJvu60jkSBiA29k2\n"
        "Ru7LzTF6ctNXkC25P4RC25RjOYJbC0iS5YIR7VYFP6R505zDWs8vONeFchdQpfau\n"
        "TVjpgipIFovNGEUOGgXKD60n8txceuSygA3fg80movXmI7O+QLyrUkvFx2onDdVM\n"
        "Vlt8uhBwv8h62mJArienFDbNyQcmj47Y5pxkBRrcA8qnti+I3I3yA3kslq2O0QtN\n"
        "LHA7ttFYjieCcVv7pm/5g4kI2XyPv56RSem/RNsEv/qoK+g/h+b2C0sVO7eUyM6n\n"
        "x9VT8w+ODunnYWs1HiAGAhzj7NhsnJp0gm88KwIDAQABAoIBAEvePnlx4yK0Yv6j\n"
        "ruXHlRcPABvki57XJHZ3sBC80sldr2Qg3CpVlM38fM8JIIzZN12jxmv9KA0HxCep\n"
        "Xq/UDyUr/zmvdtT7j7TQLTeNW5No9EpqwlWMGDnHeoxKlb2rk8CUbrlr87RGdwi/\n"
        "T5ZEYupW8xDcYiJOX1fJywj3jPFNX70Iofirz+twKJuq/pT/It1b3VKVBZb5qSW/\n"
        "kfMMnJ1kELEAk7ue1sXm5QzF0/CizHNalEGJjuKauH21iCy1BGuJ00F31iploB4f\n"
        "lqzXpNbDGyFWfQo6bZwduyrdgBe2dFt4mg5htknJPo4oSl+oLi4HewhwO3jpt06z\n"
        "KRoT8XECgYEA7vVX6QwGbfnK/+CePiTBrD3FOgzfDagn5jSrvH0Km/YDVIa/6T7k\n"
        "9M2qw5MP7D9gWPDkS7L8hL/YxCSP0mYf4ABp89/n++V6ON7tEjyA3SixXpCqLYUd\n"
        "nSYl/ygJblEujFvhVtZaKyGpTMQXyJpCbV3ZdAar8Mg2p36MusitsscCgYEA4rqU\n"
        "oTurBhXwGYzFT92OA44aFpJgh/fo532NOpayPA/eeY0cea+N2TLZYtUmUWDAaslu\n"
        "3GG+VCHzYZCwRW5QTDJjZUB7VM0tONQDXPa4TLdI0GSDxnX7QXwyE6tk7JMTJ6fH\n"
        "ZuC/Kt84ngFerZCgr5/JSy2jVfBs2sv0fdjoh30CgYBKvwvkphJMzFoneAeHwM+k\n"
        "JR5Qbj5Hc1YnuEoQB70N1AJuqkfVmgrcWIkV7CaK67gjmhaPZ0l97NTNZfJnCfLm\n"
        "irqZwmw6aym0KGdX0P0uMNBqmC3jV0RQJ+Ky0b9BdrtsxEDUfPBvlXPzw1L9OOOW\n"
        "ekjO9ldKVhZihj9XHfbXeQKBgCh/XzD1cXTi0kIeDNhZIJat+Sby+l8O/wDqQiGm\n"
        "7SshQoG/nMh3fQTAumeW3wNGHth0JmMi6lYowko5B+M+8wTJM0vQmrbo9xzhccBX\n"
        "KVA6pLzkV01JoZluz5sH0D0ZgCBjLZDIsBy+RmSipgCmhq0YA2J0QmqFSUxDheY8\n"
        "qjwZAoGANbzLzEI9wjg7ZgRPqaIfoYjTimJMAeyesXKZMJG5BxoZRyPLa3ytbzRD\n"
        "B3Gf0oOYYI0QEEa1kLv7h1OUCjVRJnKcwsSIU9D1PDZI5WSP4dyoTUqZ/x7KbOZ5\n"
        "9Ze5jxhl4B1Kr+WvZ3VBWbBBCuX8bJzOvh+C8216TWhESaz85+0=\n"
        "-----END RSA PRIVATE KEY-----\n",
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA06LAmfLBnRldEQF6E9Cc\n"
        "MisCiaaDco0fYJvu60jkSBiA29k2Ru7LzTF6ctNXkC25P4RC25RjOYJbC0iS5YIR\n"
        "7VYFP6R505zDWs8vONeFchdQpfauTVjpgipIFovNGEUOGgXKD60n8txceuSygA3f\n"
        "g80movXmI7O+QLyrUkvFx2onDdVMVlt8uhBwv8h62mJArienFDbNyQcmj47Y5pxk\n"
        "BRrcA8qnti+I3I3yA3kslq2O0QtNLHA7ttFYjieCcVv7pm/5g4kI2XyPv56RSem/\n"
        "RNsEv/qoK+g/h+b2C0sVO7eUyM6nx9VT8w+ODunnYWs1HiAGAhzj7NhsnJp0gm88\n"
        "KwIDAQAB\n"
        "-----END PUBLIC KEY-----\n",
        NULL
    },
    {
        1024,
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIICXAIBAAKBgQC2wFEkDX17SxuhH4jrSl/+lSEEXI2YGzXbDnsroXMjAa6pGj9f\n"
        "7+VOGvnBTJnT2FubDSvpaXMIEO0PTjMpS2fKKdn1jljAj3vfF9HpyyKOBgLwY1Pl\n"
        "fwj3bNPUomGZ+sgigNYWJ4+lXlSxJ7UlTQuQd7PiRsgCEIRny+5thH/rSwIDAQAB\n"
        "AoGAEzUTUh642YSDWuPdmB0xCajS14qCt0Hk3ykeeO93Em7S1KMVlhe4mgTryw0p\n"
        "/cH3nsw7mUSj+m0M/VbSubxbJA7VMVoaM3gnnHAttQVrGHxKMfA2Yupp0gLB9SFa\n"
        "W0oLO2NNz9IElQfPYWsir2VSqMbgil9srHxNMRMjcTv0O4ECQQDe8vstmZ3b2q5u\n"
        "L+Fd5pGF+rK919Bh59Nuvv3xPsJVoVjcfRJKGLKVMe+AK9YicM2jqqgV9UQ7gSZK\n"
        "z5jxS1YDAkEA0dfOsmFFGrAu4vAJf/YxJm/G7DyinM4Ffq1fVxCIZGOJxU5+EtH3\n"
        "YTRA0U6kM77O9i4Ms2LM9agSz76hdPjXGQJARVxowo4JK44EOGmS/qit23XcR+2t\n"
        "edgq0kh/Lp+szAEvaSFMIFtAq+PmNATvULWxdFqygmpUuQJ8DEg7t84NSwJAfMS7\n"
        "UpbBVvAAwNCGZX5FlRwLA/W9nkxlOf/t2z+qST5h8V4NWjVbyIEgNRN0UIwYVInm\n"
        "5VZOlZX8sWcgawN2KQJBAMvkCsY6sVjlK2FXA9f3FVHs6DT4g2TRLvCkwZAjbibY\n"
        "qy2W1RrPdtPOKXfr251hAlimxwcGXwTsRm07qirlQjE=\n"
        "-----END RSA PRIVATE KEY-----\n",
        "-----BEGIN PUBLIC KEY-----\n"
        "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQC2wFEkDX17SxuhH4jrSl/+lSEE\n"
        "XI2YGzXbDnsroXMjAa6pGj9f7+VOGvnBTJnT2FubDSvpaXMIEO0PTjMpS2fKKdn1\n"
        "jljAj3vfF9HpyyKOBgLwY1Plfwj3bNPUomGZ+sgigNYWJ4+lXlSxJ7UlTQuQd7Pi\n"
        "RsgCEIRny+5thH/rSwIDAQAB\n"
        "-----END PUBLIC KEY-----\n",
        NULL
    },
    {
        512,
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIIBOwIBAAJBAMgbhgcN8LxMNpEZgOC3hgI61pAwSxn4X8rSBHyTt7pfqbU0g2Tk\n"
        "PsNT7J6YS2xN+MwKiYNDeCTjRRbt67o1ZscCAwEAAQJBAKyXOKEq/+CYZ1P8yDCJ\n"
        "eZbAwsD4Nj4+//gB7ga4rXWbeDbkEFtLsN7wHIl1RQobfddStC5edTTbVJMk/NmX\n"
        "ESkCIQDpouOkB/cJvxfqeHqXuk4IS2s/hESEjX8dxFPsa3iNVQIhANtDCGPHhSvf\n"
        "za9hH/Wqxzbf2IrAPn/aJVNmphSi6wOrAiBj77IR2vpXp+7R86D0v9NbBu+kJq6s\n"
        "SF4kXHNNgJb7VQIhAKfuFTTmkRZjWNNj3eh4Hg/nLaBHURb26vOPgM/5X2n1AiAo\n"
        "b9m3zOpoO/0MAGCQ6qDHeebjvd65LSKgsmuDOSiOLw==\n"
        "-----END RSA PRIVATE KEY-----\n",
        "-----BEGIN PUBLIC KEY-----\n"
        "MFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAMgbhgcN8LxMNpEZgOC3hgI61pAwSxn4\n"
        "X8rSBHyTt7pfqbU0g2TkPsNT7J6YS2xN+MwKiYNDeCTjRRbt67o1ZscCAwEAAQ==\n"
        "-----END PUBLIC KEY-----\n",
        NULL
    },

    /*
     * Keys with passwords.
     */
    {
        2048,
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "Proc-Type: 4,ENCRYPTED\n"
        "DEK-Info: AES-128-CBC,86B32E02F476832DE26291AEF884BFB2\n"
        "\n"
        "3vqVAOubNaajTSUj/t0ueXRG11kVOCbQkj6AoB4bO+xYUabtcisM4I34It6GN1ZJ\n"
        "yXv2DcCE3At31LvvqS8bYGvRhY+oPpCUkC4DX+RX9Tkw5ivl1F9pv/rL3nv2F3LX\n"
        "KxMUcygwJOG2ItPu+vLI0HDYGn9reR+6boriwQfU6S8An4C6LrIZK0hUN0Bpr6W+\n"
        "JyTX9B3Tgy/BldW6yziRzYUZHnnKEKKacvHP5l0n/6nn6iFSJSFmnzvsedwOvUI0\n"
        "eHQ1LvbfQnd5yIalQ5S8UkgpKb5S4s2U0AthAC67m+Nc0E8NcbCMY1JT4FlsWVLD\n"
        "GqWmjKhwEBgoPRROEiq39KgPnoxnCEIOiQ6l8kZ0uvqlCHhWM4b1UVqb6hyrmY32\n"
        "SEBiwRqFewVYzPFI1+vT3CH/BJcXCBISNj2c4OZDqhmgncGWpLwqU1GIlLp82o3l\n"
        "t58WfNuqUM7bc/T6cIKAI2JoR2R96Zo0cgL+419msVUdZXhM/10K3W+wbHUVuSqh\n"
        "iDOCJhXWIhu47kjbCOh7OvpOtOPayWBLQiGh1Q4+WQU6t6Vdr/i71dKP0/P/QHwk\n"
        "ELNaWv/RLbE6PqKuXcjtoIqzynTvS/6C7PLEKEX3PB6kZNV+m7C0Dxu4BFj04vtx\n"
        "5CL71sGaB1ETYUdMRSvCa+f/1zwUXngmozUL+D4PkCz/vT5FYKElWt7RBMt8N+rC\n"
        "Iga+YqqvnuSPrxGXLCGZBuI2V+0BwG1pUHwk/C3uo/ggacj9+E/Oiei725cEI7H5\n"
        "FnJdFrubYsoGtyII4H1MJzp768s+bD5Bs9m/6a1m+HtzwjxNt329MyAW4DixNGEp\n"
        "T1e1e6DMnYU8XlxHkRu3IkgWjY3GPw+mfnxT5ThM16w3XC5bvRPMbIukJxFE3yDL\n"
        "jsUeVhA9NHBZbrFIjLwBWoxqlmgZjJrMFE8pcdFbNl2nKvOK0DHw6Tc93Qz0pg4q\n"
        "tvt51k9FR4WNmUY8uElmkhepAAAyzcGAHqxvrzkBmXOh76i5+j32swmmaTdx35I2\n"
        "GdRPAl75JEKZVKgHZOW6f/eCWdY7z0GAOnn+fkEzxAufU+DQAOuNkgVKySTyov5J\n"
        "v3aaMBuyrxyhgqt+k7PahlRE00S84+QvEgeiTmP/Beyd2GHwKiQ0G/9mwkVjSB1Y\n"
        "rFw0pzzud1JcYy3uFKZB+YHrV4YbfUHmJR0CKCqHUD2R95rNBIcS5ZpMm1Ak0d5E\n"
        "jAQsYlGIbWGx6aNmmf7NWacRpwVPnViU30cumeQxbCLQ2Mfb9N2zuwgplOSNp/2m\n"
        "KRU7jRs3ZLD21iplVBbmmvpC8HyJ7605bDWBw+eVaS92sEmA5lnD3uRil+7/tM8C\n"
        "rXrnU8h7vFBSWxcVM1kEiocE8eetSMczI7uA36KWbAWcMlG6hCyQSLuGkxGSZpaM\n"
        "Ro+IJx/vHNvnVj2ObqHCmSIE0+VkeyV3SlF2MqrdHNss/iOUBYFsE9zVN/oQcibt\n"
        "dXMXRN81KyHg8keNiwdd18ZWVW2+lix1mbPPgwd5iptnT4Qyder5HJroV52LdRZc\n"
        "nf3XjVzVp7tTGjGi9T/FvkpQR4tkU+Sl17qDrw9H/Y7k1j90zWFn8kykpwSRt0bV\n"
        "-----END RSA PRIVATE KEY-----\n",
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvvqfSDO1HN3Els04TSGE\n"
        "sJ0Himl934+ryfNXYIRWkq91i5+rENyZ475XBMjg8fblhvHy7vy4GfUo0PKVXxWS\n"
        "nPqOPSLEP3r2vsCX5l+KRBnGi4TeGWDTB8R6oA6HKY5ybtzUr1MHKwa7K7YJu7M9\n"
        "DW7n2JPLRajUMioO9wbYK70qlbxjeOu0V62D68fWoa3alSWMlMBv9KZW9g2oJHQy\n"
        "mUO2OdJFdyaah3z6vTKtzxmZ+NB4iwIjD6Go1CMj+FOjjjJb3EgUOIZAsRz/+9MF\n"
        "S3cRfh/8u9cZQ20Woh5vmw1anXxbwk6Z8uIFYrdgcY5G7ak0/3VukbP7VzvG+voY\n"
        "AwIDAQAB\n"
        "-----END PUBLIC KEY-----\n",
        "password"
    },

    /*
     * PKCS8 Test Keys with and without passwords.
     */
    {
        1024,
        "-----BEGIN PRIVATE KEY-----\n"
        "MIICdwIBADANBgkqhkiG9w0BAQEFAASCAmEwggJdAgEAAoGBAK4uHX+XRbLQ7dGm\n"
        "sE1IqNDi4Obf7WS2TwfklmterJvCMrN3DxDAFq9et5j8kFRtI0Lgbc6sVAxlSkaw\n"
        "+0LltbkC8JX0cjPSIlozzcZn+9dQ+m5rVLDl3AaV3kBLrYpnNggdTRiHuVbNPqZq\n"
        "0CNDMxCqHpqRjtIOuoKukcOZasD5AgMBAAECgYA4IlKNaTIkM+NBGshcz9rgHw4+\n"
        "OdKnD34e3BOCHOvh8s8mOWuYiV+GOy9OVa8qFlYz2mJpJe6cZBRw/d6sK53Jrzc1\n"
        "ULULW9YNqgkhdhTm0z8QolYjBU+qp9pAXhh29tCdMxgCWAsiVR9jsnFtPQX4QEmM\n"
        "9t+65ghTFQWtQXMqpQJBANly600i4GYoxvzvp67RvUkmnG47LvwuVRMwUAmAX6QP\n"
        "Ww5q6aJd9HnHttLsNHxgX49aVxgpFu2uJI2SwSV3qwMCQQDND2kty83UXW5RahIt\n"
        "BXAY8W60Itw6+bPLg3P4IixDCoHphnLqkz5ZT2NxxPsAPGeaFZDVyNs3Hgasnd8V\n"
        "V8VTAkEAi4KWgrvQmtqoqFkeDSRVvBwAmxxvja4wOQpzH1V0hy6u7fYcBWcgVg2T\n"
        "N4oCNpYiWTfNzxt1sXJb01UHhIFdfwJAO8ZiQpdGSMFzhwgEhFsxchPu0VPYHtjr\n"
        "MEgBZjOP83r8o7YtiXOimSYrNt7UzBzPlnry3V7PiCGYkHj0rqQHQQJBANi5N5X4\n"
        "g7dNDsE5i1B0JsQ4ru8qE60ZtoOOCwNjwiI/IIsMVW2KqhTBynEYLnWolkRRogEF\n"
        "ACoRRxUBhj9EefI=\n"
        "-----END PRIVATE KEY-----\n",
        "-----BEGIN PUBLIC KEY-----\n"
        "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCuLh1/l0Wy0O3RprBNSKjQ4uDm\n"
        "3+1ktk8H5JZrXqybwjKzdw8QwBavXreY/JBUbSNC4G3OrFQMZUpGsPtC5bW5AvCV\n"
        "9HIz0iJaM83GZ/vXUPpua1Sw5dwGld5AS62KZzYIHU0Yh7lWzT6matAjQzMQqh6a\n"
        "kY7SDrqCrpHDmWrA+QIDAQAB\n"
        "-----END PUBLIC KEY-----\n",
        NULL
    },
    {
        2048,
        "-----BEGIN ENCRYPTED PRIVATE KEY-----\n"
        "MIIFLTBXBgkqhkiG9w0BBQ0wSjApBgkqhkiG9w0BBQwwHAQIRFetWFFFIb4CAggA\n"
        "MAwGCCqGSIb3DQIJBQAwHQYJYIZIAWUDBAEqBBAZMr0Lq1s+olU2jUY8MuQJBIIE\n"
        "0ICOZE6GhPCQbUSudbBYTG4zBRGhJOTeGF43c3pqi6UNS4qWK9IQ3B5hm618Iof0\n"
        "YUnCDKy9G7TPMwP+8pcybFXuvWo1yeJcVGNalBq/LmUG2RBJ3hh/IikDnzj2jq1u\n"
        "QKFTgl5yZ41bC75d81fdg0CpYqIGOjLdQcUJmVk+lKggWcN7KuqPj+9FhCoRyjIp\n"
        "UyLYQQ8E0sb7tk0gJoi6VHddTYpLEDiFzGqXP/XWykCFHx977sbRuOymrTF3C3OZ\n"
        "X5PSkszydSBzomPl1MnmiMjAmgc3j6EABUpzjaUZ2l2xxeM9r/c076zSpHdcBFus\n"
        "Y3pA9Hm9HvV2q+1FHHNk90vZlXWtyTh8tSJvT3WF63kYMyIXXztovldjxX76fxB0\n"
        "c5K0E9FH5sjv0R4AfMf4CMsP5InGfy2zICRwi+xvp97lq6nEXjIqiePyNTUA3QAy\n"
        "brZtzM67KxFL/TuV6Y20DILAPlWZe3C8KFpFeHEi5yddi0VikzQVl1X/hieCt4SP\n"
        "aTdd+MCn3XIu+58RK6UYCVCxbH9j9iZCznOfWLRMpthvoa9SO8M8DTFlx/bptClt\n"
        "IKUnsQgBpvT3+xzpJk4sQyD4aZDcDMQeNfDr/1KyYMEjaqvGMqKfLed2HLDHdD9f\n"
        "rsg41wTCqp/draUh2qxa7pXkK0KcNbH4hLH//pduaLubHmOPofLvprVIISyOtspN\n"
        "tsPtXs43Ta4dOQWLg2Q/lwlo0psi1im/fHKyr7rpMdUa+dRGX8H4tYsFJufHzVjr\n"
        "rQrKDHPsNfhy+JuCfQu/8SdZCXwcBxxeSlam5EgtlfsTDC+zIP8dDHaOWsDRm+k3\n"
        "ryKTSn84LBQLWzc3RhZteAlzDHcmrS/MmF4yfpgSkFI+aUF5+XPLqoYVsoVKQ5bL\n"
        "NnA6xJBkXVtzNZUYH3cHoiAOATlhHRFtoWrKoEQXlCNvvTCiBGoMPfjpnTy3u/kS\n"
        "8JaUsJLvDFQBFPSxdYA+w/zb3zy0Nh5s3R9D6IkrH0X2mk8JhABYNzDIDYlS2Ioz\n"
        "ARpmwuZwPUG1iSzamYZCt2OVd1acPexiwTATihfPVT2RFbHET9+e7NX/5TFnGP++\n"
        "4o6mckiD5c9QmwE29FLTeiqwKvLweLrrF6/1/S45/okibqXHgh7O567y+PSMmjk5\n"
        "L0azEmv6UIs5z4FNvDxS5++b3oqUMu+oazQP1aDk0H/8xJaDFrnOKWL9h8waeBn7\n"
        "JBuuIFKqRb6S9H0ZPb1R7Z9BVuUil76nc4zr0kLNdJ8dq2l/kcqIIFrtVJX/INaf\n"
        "gYvlsIYXpb/IhBZit1GJxwi8kk29b2QSyDW6CNNi3dC8Y1p9jiLejqFM4LQL/HNr\n"
        "atc1pUBPePK1ZHJ0OLyVthJYXmn8v+M9eHfptQzBZpILTZZK719uOtHloPrI64LY\n"
        "iO00glzBju2W1yDF6cTgmWQEigWno65Is5pjN5ByMf3ouHM8qJFIhTEqCpAY7cQQ\n"
        "2k6o7dqAcQm7Q+BvhfsWcPWq/GH/OOkuUDqQaK1YDA+lUj9uyrxm9AlrDtUjezLE\n"
        "k3IT6ZiBVrPlKWCMbT6ajm9ti0RuCRnZfrrLn2gu16weRtaNeVyza6D5wn+eKXmE\n"
        "5dnugDd6T+QBX/3+WLaXTL3l/tj7i9WwNJU4uqW7y6+P\n"
        "-----END ENCRYPTED PRIVATE KEY-----\n",
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp6CMrt0Z/k5+c/7C3oWz\n"
        "bLBmE4fttE84EZsrwB/ZDhMBQDsVF/GMePj1e5zrxnVq6GZhcNbcJTqHp0mWb+K5\n"
        "HMlAihPKYlswJQtkVgp/czbdXwt3MI+D4ifUiq4v8AMrJHW+AYd0GYKzYma6LGVj\n"
        "75Bue450bsLocMKwB03iyFE8SBwzGSj9jwJ9UYBvVUnNsutq6nCPTj1bM6naFIHO\n"
        "Y+cozHIrKyvHGHoWBVUqKARlNT3TtbTyGxaT4QyZj8Pm9jB5Np6CrF7nmV936Q3A\n"
        "3CHji8BbhfcdZ/9s53wkSwztfpe8NYh1/RiLZtZdky9E6Q67dt3h4bKHsKRFi0xW\n"
        "jQIDAQAB\n"
        "-----END PUBLIC KEY-----\n",
        "password"
    },
};




static void test1()
{
    RTTestSub(g_hTest, "Basics");
    int rc;
    RTCRKEY hPublicKey  = NIL_RTCRKEY;
    RTCRKEY hPrivateKey = NIL_RTCRKEY;

    for (unsigned i = 0; i < RT_ELEMENTS(g_aKeyPairs); i++)
    {
        RTCrKeyRelease(hPublicKey);
        hPublicKey = NIL_RTCRKEY;
        RTCrKeyRelease(hPrivateKey);
        hPrivateKey = NIL_RTCRKEY;

        /*
         * Load the key pair.
         */
        rc = RTCrKeyCreateFromBuffer(&hPublicKey, 0, g_aKeyPairs[i].pszPublicKey, strlen(g_aKeyPairs[i].pszPublicKey),
                                     NULL /*pszPassword*/, NULL /*pErrInfo*/, NULL /*pszErrorTag*/);
        if (RT_FAILURE(rc))
            RTTestIFailed("Error %Rrc decoding public key #%u (%u bits)", rc, i, g_aKeyPairs[i].cBits);

        rc = RTCrKeyCreateFromBuffer(&hPrivateKey, 0, g_aKeyPairs[i].pszPrivateKey, strlen(g_aKeyPairs[i].pszPrivateKey),
                                     g_aKeyPairs[i].pszPassword, NULL /*pErrInfo*/, NULL /*pszErrorTag*/);
        if (RT_FAILURE(rc))
            RTTestIFailed("Error %Rrc decoding private key #%u (%u bits)", rc, i, g_aKeyPairs[i].cBits);

        if (hPrivateKey == NIL_RTCRKEY || hPublicKey == NIL_RTCRKEY)
            continue;

        /*
         * If we've got a password encrypted key, try some incorrect password.
         */
        if (g_aKeyPairs[i].pszPassword)
        {
            static const char * const s_apszBadPassword[] =
            {
                "bad-password", "", "<>", "really really long long long bad bad bad bad bad password password password password",
                "a", "ab", "abc", "abcd", "abcde", "fdcba"
            };
            for (unsigned iPasswd = 0; iPasswd < RT_ELEMENTS(s_apszBadPassword); iPasswd++)
            {
                RTCRKEY hKey = NIL_RTCRKEY;
                rc = RTCrKeyCreateFromBuffer(&hKey, 0, g_aKeyPairs[i].pszPrivateKey, strlen(g_aKeyPairs[i].pszPrivateKey),
                                             s_apszBadPassword[iPasswd], NULL /*pErrInfo*/, NULL /*pszErrorTag*/);
                if (rc != VERR_CR_KEY_DECRYPTION_FAILED)
                    RTTestIFailed("Unexpected bad password response %Rrc decoding private key #%u (%u bits) using '%s' as password",
                                  rc, i, g_aKeyPairs[i].cBits, s_apszBadPassword[iPasswd]);
            }
        }

        /*
         * Create corresponding signing and verifying decoder instances.
         */
        static struct { uint32_t cBits; const char *pszObjId; } const s_aSignatures[] =
        {
            { 128, RTCR_PKCS1_MD2_WITH_RSA_OID },
            //{ 128, RTCR_PKCS1_MD4_WITH_RSA_OID },
            { 128, RTCR_PKCS1_MD5_WITH_RSA_OID },
            { 160, RTCR_PKCS1_SHA1_WITH_RSA_OID },
            { 256, RTCR_PKCS1_SHA256_WITH_RSA_OID },
            { 224, RTCR_PKCS1_SHA224_WITH_RSA_OID },
            { 384, RTCR_PKCS1_SHA384_WITH_RSA_OID },
            { 512, RTCR_PKCS1_SHA512_WITH_RSA_OID },
        };
        RTCRPKIXSIGNATURE hSign   = NIL_RTCRPKIXSIGNATURE;
        RTCRPKIXSIGNATURE hVerify = NIL_RTCRPKIXSIGNATURE;
        for (unsigned iSig = 0; iSig < RT_ELEMENTS(s_aSignatures); iSig++)
        {
            RTCrPkixSignatureRelease(hSign);
            hSign = NIL_RTCRPKIXSIGNATURE;
            RTCrPkixSignatureRelease(hVerify);
            hVerify = NIL_RTCRPKIXSIGNATURE;

            rc = RTCrPkixSignatureCreateByObjIdString(&hSign, s_aSignatures[iSig].pszObjId, hPrivateKey, NULL, true /*fSigning*/);
            if (RT_FAILURE(rc))
                RTTestIFailed("RTCrPkixSignatureCreateByObjIdString failed with %Rrc on %u bits private key and %u bits MD (%s)",
                              rc, g_aKeyPairs[i].cBits, s_aSignatures[iSig].cBits, s_aSignatures[iSig].pszObjId);

            rc = RTCrPkixSignatureCreateByObjIdString(&hVerify, s_aSignatures[iSig].pszObjId, hPublicKey, NULL, false /*fSigning*/);
            if (RT_FAILURE(rc))
                RTTestIFailed("RTCrPkixSignatureCreateByObjIdString failed with %Rrc on %u bits public key and %u bits MD (%s)",
                              rc, g_aKeyPairs[i].cBits, s_aSignatures[iSig].cBits, s_aSignatures[iSig].pszObjId);

            if (RT_FAILURE(rc) || hSign == NIL_RTCRPKIXSIGNATURE || hVerify == NIL_RTCRPKIXSIGNATURE)
                continue;

            /*
             * Try a few different boilplate things.
             */
            static struct { void const *pv; size_t cb; } const s_aTexts[] =
            {
                {  RT_STR_TUPLE("") },
                {  RT_STR_TUPLE("IPRT") },
                {  RT_STR_TUPLE("abcdef") },
            };

            for (unsigned iText = 0; iText < RT_ELEMENTS(s_aTexts); iText++)
            {
                uint8_t abSignature[4096];
                size_t  cbSignature = sizeof(abSignature);

                RTCRDIGEST hDigest = NIL_RTCRDIGEST;
                rc = RTCrDigestCreateByObjIdString(&hDigest, s_aSignatures[iSig].pszObjId);
                if (RT_SUCCESS(rc))
                {
                    RTTESTI_CHECK_RC(RTCrDigestUpdate(hDigest, s_aTexts[iText].pv, s_aTexts[iText].cb), VINF_SUCCESS);

                    rc = RTCrPkixSignatureSign(hSign, hDigest, abSignature, &cbSignature);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTCrPkixSignatureVerify(hVerify, hDigest, abSignature, cbSignature);
                        if (RT_FAILURE(rc))
                            RTTestIFailed("RTCrPkixSignatureVerify failed with %Rrc for %u bits MD with %u bits key (%s); signature length %u",
                                                          rc, s_aSignatures[iSig].cBits, g_aKeyPairs[i].cBits, s_aSignatures[iSig].pszObjId, cbSignature);

                    }
                    else if (rc != VERR_CR_PKIX_HASH_TOO_LONG_FOR_KEY)
                        RTTestIFailed("RTCrPkixSignatureSign failed with %Rrc for %u bits MD with %u bits key (%s)",
                                      rc, s_aSignatures[iSig].cBits, g_aKeyPairs[i].cBits, s_aSignatures[iSig].pszObjId);
                    RTCrDigestRelease(hDigest);
                }
                else
                    RTTestIFailed("RTCrDigestCreateByObjIdString failed with %Rrc for %s (%u bits)",
                                  rc, s_aSignatures[iSig].pszObjId, s_aSignatures[iSig].cBits);
            }
        }

        RTCrPkixSignatureRelease(hSign);
        hSign = NIL_RTCRPKIXSIGNATURE;
        RTCrPkixSignatureRelease(hVerify);
        hVerify = NIL_RTCRPKIXSIGNATURE;
    }

    RTCrKeyRelease(hPublicKey);
    hPublicKey = NIL_RTCRKEY;
    RTCrKeyRelease(hPrivateKey);
    hPrivateKey = NIL_RTCRKEY;
}




int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTCrPkix-1", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    test1();

    return RTTestSummaryAndDestroy(g_hTest);
}

