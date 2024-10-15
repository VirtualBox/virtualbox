/* $Id: AudioMixBuffer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Audio mixing buffer for converting reading/writing audio data.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/** @page pg_audio_mixing_buffers   Audio Mixer Buffer
 *
 * @section sec_audio_mixing_buffers_volume     Soft Volume Control
 *
 * The external code supplies an 8-bit volume (attenuation) value in the
 * 0 .. 255 range. This represents 0 to -96dB attenuation where an input
 * value of 0 corresponds to -96dB and 255 corresponds to 0dB (unchanged).
 *
 * Each step thus corresponds to 96 / 256 or 0.375dB. Every 6dB (16 steps)
 * represents doubling the sample value.
 *
 * For internal use, the volume control needs to be converted to a 16-bit
 * (sort of) exponential value between 1 and 65536. This is used with fixed
 * point arithmetic such that 65536 means 1.0 and 1 means 1/65536.
 *
 * For actual volume calculation, 33.31 fixed point is used. Maximum (or
 * unattenuated) volume is represented as 0x40000000; conveniently, this
 * value fits into a uint32_t.
 *
 * To enable fast processing, the maximum volume must be a power of two
 * and must not have a sign when converted to int32_t. While 0x80000000
 * violates these constraints, 0x40000000 does not.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_AUDIO_MIXER_BUFFER
#if defined(VBOX_AUDIO_MIX_BUFFER_TESTCASE) && !defined(RT_STRICT)
# define RT_STRICT /* Run the testcase with assertions because the main functions doesn't return on invalid input. */
#endif
#include <VBox/log.h>

#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h> /* For RT_BZERO. */

#ifdef VBOX_AUDIO_TESTCASE
# define LOG_ENABLED
# include <iprt/stream.h>
#endif
#include <iprt/errcore.h>
#include <VBox/vmm/pdmaudioinline.h>

#include "AudioMixBuffer.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef VBOX_AUDIO_TESTCASE
# ifdef DEBUG
#  define AUDMIXBUF_LOG(x) LogFlowFunc(x)
#  define AUDMIXBUF_LOG_ENABLED
# else
#  define AUDMIXBUF_LOG(x) do {} while (0)
# endif
#else /* VBOX_AUDIO_TESTCASE */
# define AUDMIXBUF_LOG(x) RTPrintf x
# define AUDMIXBUF_LOG_ENABLED
#endif


/** Bit shift for fixed point conversion.
 * @sa @ref sec_audio_mixing_buffers_volume */
#define AUDIOMIXBUF_VOL_SHIFT       30

/** Internal representation of 0dB volume (1.0 in fixed point).
 * @sa @ref sec_audio_mixing_buffers_volume */
#define AUDIOMIXBUF_VOL_0DB         (1 << AUDIOMIXBUF_VOL_SHIFT)
AssertCompile(AUDIOMIXBUF_VOL_0DB <= 0x40000000);   /* Must always hold. */
AssertCompile(AUDIOMIXBUF_VOL_0DB == 0x40000000);   /* For now -- when only attenuation is used. */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Logarithmic/exponential volume conversion table.
 * @sa @ref sec_audio_mixing_buffers_volume
 */
static uint32_t const s_aVolumeConv[256] =
{
        1,     1,     1,     1,     1,     1,     1,     1, /*   7 */
        1,     2,     2,     2,     2,     2,     2,     2, /*  15 */
        2,     2,     2,     2,     2,     3,     3,     3, /*  23 */
        3,     3,     3,     3,     4,     4,     4,     4, /*  31 */
        4,     4,     5,     5,     5,     5,     5,     6, /*  39 */
        6,     6,     6,     7,     7,     7,     8,     8, /*  47 */
        8,     9,     9,    10,    10,    10,    11,    11, /*  55 */
       12,    12,    13,    13,    14,    15,    15,    16, /*  63 */
       17,    17,    18,    19,    20,    21,    22,    23, /*  71 */
       24,    25,    26,    27,    28,    29,    31,    32, /*  79 */
       33,    35,    36,    38,    40,    41,    43,    45, /*  87 */
       47,    49,    52,    54,    56,    59,    61,    64, /*  95 */
       67,    70,    73,    76,    79,    83,    87,    91, /* 103 */
       95,    99,   103,   108,   112,   117,   123,   128, /* 111 */
      134,   140,   146,   152,   159,   166,   173,   181, /* 119 */
      189,   197,   206,   215,   225,   235,   245,   256, /* 127 */
      267,   279,   292,   304,   318,   332,   347,   362, /* 135 */
      378,   395,   412,   431,   450,   470,   490,   512, /* 143 */
      535,   558,   583,   609,   636,   664,   693,   724, /* 151 */
      756,   790,   825,   861,   899,   939,   981,  1024, /* 159 */
     1069,  1117,  1166,  1218,  1272,  1328,  1387,  1448, /* 167 */
     1512,  1579,  1649,  1722,  1798,  1878,  1961,  2048, /* 175 */
     2139,  2233,  2332,  2435,  2543,  2656,  2774,  2896, /* 183 */
     3025,  3158,  3298,  3444,  3597,  3756,  3922,  4096, /* 191 */
     4277,  4467,  4664,  4871,  5087,  5312,  5547,  5793, /* 199 */
     6049,  6317,  6597,  6889,  7194,  7512,  7845,  8192, /* 207 */
     8555,  8933,  9329,  9742, 10173, 10624, 11094, 11585, /* 215 */
    12098, 12634, 13193, 13777, 14387, 15024, 15689, 16384, /* 223 */
    17109, 17867, 18658, 19484, 20347, 21247, 22188, 23170, /* 231 */
    24196, 25268, 26386, 27554, 28774, 30048, 31379, 32768, /* 239 */
    34219, 35734, 37316, 38968, 40693, 42495, 44376, 46341, /* 247 */
    48393, 50535, 52773, 55109, 57549, 60097, 62757, 65536, /* 255 */
};



#ifdef VBOX_STRICT
# ifdef UNUSED

/**
 * Prints a single mixing buffer.
 * Internal helper function for debugging. Do not use directly.
 *
 * @returns VBox status code.
 * @param   pMixBuf                 Mixing buffer to print.
 * @param   pszFunc                 Function name to log this for.
 * @param   uIdtLvl                 Indention level to use.
 */
static void audioMixBufDbgPrintSingle(PAUDIOMIXBUF pMixBuf, const char *pszFunc, uint16_t uIdtLvl)
{
    Log(("%s: %*s %s: offRead=%RU32, offWrite=%RU32 -> %RU32/%RU32\n",
         pszFunc, uIdtLvl * 4, "",
         pMixBuf->pszName, pMixBuf->offRead, pMixBuf->offWrite, pMixBuf->cUsed, pMixBuf->cFrames));
}

static void audioMixBufDbgPrintInternal(PAUDIOMIXBUF pMixBuf, const char *pszFunc)
{
    audioMixBufDbgPrintSingle(pMixBuf, pszFunc, 0 /* iIdtLevel */);
}

/**
 * Validates a single mixing buffer.
 *
 * @return  @true if the buffer state is valid or @false if not.
 * @param   pMixBuf                 Mixing buffer to validate.
 */
static bool audioMixBufDbgValidate(PAUDIOMIXBUF pMixBuf)
{
    //const uint32_t offReadEnd  = (pMixBuf->offRead + pMixBuf->cUsed) % pMixBuf->cFrames;
    //const uint32_t offWriteEnd = (pMixBuf->offWrite + (pMixBuf->cFrames - pMixBuf->cUsed)) % pMixBuf->cFrames;

    bool fValid = true;

    AssertStmt(pMixBuf->offRead  <= pMixBuf->cFrames, fValid = false);
    AssertStmt(pMixBuf->offWrite <= pMixBuf->cFrames, fValid = false);
    AssertStmt(pMixBuf->cUsed    <= pMixBuf->cFrames, fValid = false);

    if (pMixBuf->offWrite > pMixBuf->offRead)
    {
        if (pMixBuf->offWrite - pMixBuf->offRead != pMixBuf->cUsed)
            fValid = false;
    }
    else if (pMixBuf->offWrite < pMixBuf->offRead)
    {
        if (pMixBuf->offWrite + pMixBuf->cFrames - pMixBuf->offRead != pMixBuf->cUsed)
            fValid = false;
    }

    if (!fValid)
    {
        audioMixBufDbgPrintInternal(pMixBuf, __FUNCTION__);
        AssertFailed();
    }

    return fValid;
}

# endif /* UNUSED */
#endif /* VBOX_STRICT */


/**
 * Merges @a i32Src into the value stored at @a pi32Dst.
 *
 * @param   pi32Dst     The value to merge @a i32Src into.
 * @param   i32Src      The new value to add.
 */
DECL_FORCE_INLINE(void) audioMixBufBlendSample(int32_t *pi32Dst, int32_t i32Src)
{
    if (i32Src)
    {
        int64_t const i32Dst = *pi32Dst;
        if (!i32Dst)
            *pi32Dst = i32Src;
        else
            *pi32Dst = (int32_t)(((int64_t)i32Dst + i32Src) / 2);
    }
}


/**
 * Variant of audioMixBufBlendSample that returns the result rather than storing it.
 *
 * This is used for stereo -> mono.
 */
DECL_FORCE_INLINE(int32_t) audioMixBufBlendSampleRet(int32_t i32Sample1, int32_t i32Sample2)
{
    if (!i32Sample1)
        return i32Sample2;
    if (!i32Sample2)
        return i32Sample1;
    return (int32_t)(((int64_t)i32Sample1 + i32Sample2) / 2);
}


/**
 * Blends (merges) the source buffer into the destination buffer.
 *
 * We're taking a very simple approach here, working sample by sample:
 *  - if one is silent, use the other one.
 *  - otherwise sum and divide by two.
 *
 * @param   pi32Dst     The destination stream buffer (input and output).
 * @param   pi32Src     The source stream buffer.
 * @param   cFrames     Number of frames to process.
 * @param   cChannels   Number of channels.
 */
static void audioMixBufBlendBuffer(int32_t *pi32Dst, int32_t const *pi32Src, uint32_t cFrames, uint8_t cChannels)
{
    switch (cChannels)
    {
        case 2:
            while (cFrames-- > 0)
            {
                audioMixBufBlendSample(&pi32Dst[0], pi32Src[0]);
                audioMixBufBlendSample(&pi32Dst[1], pi32Src[1]);
                pi32Dst += 2;
                pi32Src += 2;
            }
            break;

        default:
            cFrames *= cChannels;
            RT_FALL_THROUGH();
        case 1:
            while (cFrames-- > 0)
            {
                audioMixBufBlendSample(pi32Dst, pi32Src[0]);
                pi32Dst++;
                pi32Src++;
            }
            break;
    }
}


#ifdef AUDIOMIXBUF_DEBUG_MACROS
# define AUDMIXBUF_MACRO_LOG(x) AUDMIXBUF_LOG(x)
#elif defined(VBOX_AUDIO_TESTCASE_VERBOSE) /* Warning: VBOX_AUDIO_TESTCASE_VERBOSE will generate huge logs! */
# define AUDMIXBUF_MACRO_LOG(x) RTPrintf x
#else
# define AUDMIXBUF_MACRO_LOG(x) do {} while (0)
#endif

/*
 * Instantiate format conversion (in and out of the mixer buffer.)
 */
/** @todo Currently does not handle any endianness conversion yet! */

/* audioMixBufConvXXXS8: 8-bit, signed. */
#define a_Name      S8
#define a_Type      int8_t
#define a_Min       INT8_MIN
#define a_Max       INT8_MAX
#define a_fSigned   1
#define a_cShift    8
#include "AudioMixBuffer-Convert.cpp.h"

/* audioMixBufConvXXXU8: 8-bit, unsigned. */
#define a_Name      U8
#define a_Type      uint8_t
#define a_Min       0
#define a_Max       UINT8_MAX
#define a_fSigned   0
#define a_cShift    8
#include "AudioMixBuffer-Convert.cpp.h"

/* audioMixBufConvXXXS16: 16-bit, signed. */
#define a_Name      S16
#define a_Type      int16_t
#define a_Min       INT16_MIN
#define a_Max       INT16_MAX
#define a_fSigned   1
#define a_cShift    16
#include "AudioMixBuffer-Convert.cpp.h"

/* audioMixBufConvXXXU16: 16-bit, unsigned. */
#define a_Name      U16
#define a_Type      uint16_t
#define a_Min       0
#define a_Max       UINT16_MAX
#define a_fSigned   0
#define a_cShift    16
#include "AudioMixBuffer-Convert.cpp.h"

/* audioMixBufConvXXXS32: 32-bit, signed. */
#define a_Name      S32
#define a_Type      int32_t
#define a_Min       INT32_MIN
#define a_Max       INT32_MAX
#define a_fSigned   1
#define a_cShift    32
#include "AudioMixBuffer-Convert.cpp.h"

/* audioMixBufConvXXXU32: 32-bit, unsigned. */
#define a_Name      U32
#define a_Type      uint32_t
#define a_Min       0
#define a_Max       UINT32_MAX
#define a_fSigned   0
#define a_cShift    32
#include "AudioMixBuffer-Convert.cpp.h"

/* audioMixBufConvXXXRaw: 32-bit stored as 64-bit, signed. */
#define a_Name      Raw
#define a_Type      int64_t
#define a_Min       INT64_MIN
#define a_Max       INT64_MAX
#define a_fSigned   1
#define a_cShift    32 /* Yes, 32! */
#include "AudioMixBuffer-Convert.cpp.h"

#undef AUDMIXBUF_CONVERT
#undef AUDMIXBUF_MACRO_LOG


/*
 * Resampling core.
 */
/** @todo Separate down- and up-sampling, borrow filter code from RDP. */
#define COPY_LAST_FRAME_1CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
    } while (0)
#define COPY_LAST_FRAME_2CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
    } while (0)
#define COPY_LAST_FRAME_3CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
    } while (0)
#define COPY_LAST_FRAME_4CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
    } while (0)
#define COPY_LAST_FRAME_5CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
    } while (0)
#define COPY_LAST_FRAME_6CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
        (a_pi32Dst)[5] = (a_pi32Src)[5]; \
    } while (0)
#define COPY_LAST_FRAME_7CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
        (a_pi32Dst)[5] = (a_pi32Src)[5]; \
        (a_pi32Dst)[6] = (a_pi32Src)[6]; \
    } while (0)
#define COPY_LAST_FRAME_8CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
        (a_pi32Dst)[5] = (a_pi32Src)[5]; \
        (a_pi32Dst)[6] = (a_pi32Src)[6]; \
        (a_pi32Dst)[7] = (a_pi32Src)[7]; \
    } while (0)
#define COPY_LAST_FRAME_9CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
        (a_pi32Dst)[5] = (a_pi32Src)[5]; \
        (a_pi32Dst)[6] = (a_pi32Src)[6]; \
        (a_pi32Dst)[7] = (a_pi32Src)[7]; \
        (a_pi32Dst)[8] = (a_pi32Src)[8]; \
    } while (0)
#define COPY_LAST_FRAME_10CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
        (a_pi32Dst)[5] = (a_pi32Src)[5]; \
        (a_pi32Dst)[6] = (a_pi32Src)[6]; \
        (a_pi32Dst)[7] = (a_pi32Src)[7]; \
        (a_pi32Dst)[8] = (a_pi32Src)[8]; \
        (a_pi32Dst)[9] = (a_pi32Src)[9]; \
    } while (0)
#define COPY_LAST_FRAME_11CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
        (a_pi32Dst)[5] = (a_pi32Src)[5]; \
        (a_pi32Dst)[6] = (a_pi32Src)[6]; \
        (a_pi32Dst)[7] = (a_pi32Src)[7]; \
        (a_pi32Dst)[8] = (a_pi32Src)[8]; \
        (a_pi32Dst)[9] = (a_pi32Src)[9]; \
        (a_pi32Dst)[10] = (a_pi32Src)[10]; \
    } while (0)
#define COPY_LAST_FRAME_12CH(a_pi32Dst, a_pi32Src, a_cChannels) do { \
        (a_pi32Dst)[0] = (a_pi32Src)[0]; \
        (a_pi32Dst)[1] = (a_pi32Src)[1]; \
        (a_pi32Dst)[2] = (a_pi32Src)[2]; \
        (a_pi32Dst)[3] = (a_pi32Src)[3]; \
        (a_pi32Dst)[4] = (a_pi32Src)[4]; \
        (a_pi32Dst)[5] = (a_pi32Src)[5]; \
        (a_pi32Dst)[6] = (a_pi32Src)[6]; \
        (a_pi32Dst)[7] = (a_pi32Src)[7]; \
        (a_pi32Dst)[8] = (a_pi32Src)[8]; \
        (a_pi32Dst)[9] = (a_pi32Src)[9]; \
        (a_pi32Dst)[10] = (a_pi32Src)[10]; \
        (a_pi32Dst)[11] = (a_pi32Src)[11]; \
    } while (0)

#define INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_iCh) \
        (a_pi32Dst)[a_iCh] = ((a_pi32Last)[a_iCh] * a_i64FactorLast + (a_pi32Src)[a_iCh] * a_i64FactorCur) >> 32
#define INTERPOLATE_1CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
    } while (0)
#define INTERPOLATE_2CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
    } while (0)
#define INTERPOLATE_3CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
    } while (0)
#define INTERPOLATE_4CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
    } while (0)
#define INTERPOLATE_5CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
    } while (0)
#define INTERPOLATE_6CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 5); \
    } while (0)
#define INTERPOLATE_7CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 5); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 6); \
    } while (0)
#define INTERPOLATE_8CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 5); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 6); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 7); \
    } while (0)
#define INTERPOLATE_9CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 5); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 6); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 7); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 8); \
    } while (0)
#define INTERPOLATE_10CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 5); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 6); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 7); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 8); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 9); \
    } while (0)
#define INTERPOLATE_11CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 5); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 6); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 7); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 8); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 9); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 10); \
    } while (0)
#define INTERPOLATE_12CH(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, a_cChannels) do { \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 0); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 1); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 2); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 3); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 4); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 5); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 6); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 7); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 8); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 9); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 10); \
        INTERPOLATE_ONE(a_pi32Dst, a_pi32Src, a_pi32Last, a_i64FactorCur, a_i64FactorLast, 11); \
    } while (0)

#define AUDIOMIXBUF_RESAMPLE(a_cChannels, a_Suffix) \
    /** @returns Number of destination frames written. */ \
    static DECLCALLBACK(uint32_t) \
    audioMixBufResample##a_cChannels##Ch##a_Suffix(int32_t *pi32Dst, uint32_t cDstFrames, \
                                                   int32_t const *pi32Src, uint32_t cSrcFrames, uint32_t *pcSrcFramesRead, \
                                                   PAUDIOSTREAMRATE pRate) \
    { \
        Log5(("Src: %RU32 L %RU32;  Dst: %RU32 L%RU32; uDstInc=%#RX64\n", \
              pRate->offSrc, cSrcFrames, RT_HI_U32(pRate->offDst), cDstFrames, pRate->uDstInc)); \
        int32_t * const       pi32DstStart = pi32Dst; \
        int32_t const * const pi32SrcStart = pi32Src; \
        \
        int32_t ai32LastFrame[a_cChannels]; \
        COPY_LAST_FRAME_##a_cChannels##CH(ai32LastFrame, pRate->SrcLast.ai32Samples, a_cChannels); \
        \
        while (cDstFrames > 0 && cSrcFrames > 0) \
        { \
            int32_t const cSrcNeeded = RT_HI_U32(pRate->offDst) - pRate->offSrc + 1; \
            if (cSrcNeeded > 0) \
            { \
                if ((uint32_t)cSrcNeeded + 1 < cSrcFrames) \
                { \
                    pRate->offSrc += (uint32_t)cSrcNeeded; \
                    cSrcFrames    -= (uint32_t)cSrcNeeded; \
                    pi32Src       += (uint32_t)cSrcNeeded * a_cChannels; \
                    COPY_LAST_FRAME_##a_cChannels##CH(ai32LastFrame, &pi32Src[-a_cChannels], a_cChannels); \
                } \
                else \
                { \
                    pi32Src       += cSrcFrames * a_cChannels; \
                    pRate->offSrc += cSrcFrames; \
                    COPY_LAST_FRAME_##a_cChannels##CH(pRate->SrcLast.ai32Samples, &pi32Src[-a_cChannels], a_cChannels); \
                    *pcSrcFramesRead = (pi32Src - pi32SrcStart) / a_cChannels; \
                    return (pi32Dst - pi32DstStart) / a_cChannels; \
                } \
            } \
            \
            /* Interpolate. */ \
            int64_t const offFactorCur  = pRate->offDst & UINT32_MAX; \
            int64_t const offFactorLast = (int64_t)_4G - offFactorCur; \
            INTERPOLATE_##a_cChannels##CH(pi32Dst, pi32Src, ai32LastFrame, offFactorCur, offFactorLast, a_cChannels); \
            \
            /* Advance. */ \
            pRate->offDst += pRate->uDstInc; \
            pi32Dst       += a_cChannels; \
            cDstFrames    -= 1; \
        } \
        \
        COPY_LAST_FRAME_##a_cChannels##CH(pRate->SrcLast.ai32Samples, ai32LastFrame, a_cChannels); \
        *pcSrcFramesRead = (pi32Src - pi32SrcStart) / a_cChannels; \
        return (pi32Dst - pi32DstStart) / a_cChannels; \
    }

AUDIOMIXBUF_RESAMPLE(1,Generic)
AUDIOMIXBUF_RESAMPLE(2,Generic)
AUDIOMIXBUF_RESAMPLE(3,Generic)
AUDIOMIXBUF_RESAMPLE(4,Generic)
AUDIOMIXBUF_RESAMPLE(5,Generic)
AUDIOMIXBUF_RESAMPLE(6,Generic)
AUDIOMIXBUF_RESAMPLE(7,Generic)
AUDIOMIXBUF_RESAMPLE(8,Generic)
AUDIOMIXBUF_RESAMPLE(9,Generic)
AUDIOMIXBUF_RESAMPLE(10,Generic)
AUDIOMIXBUF_RESAMPLE(11,Generic)
AUDIOMIXBUF_RESAMPLE(12,Generic)


/**
 * Resets the resampling state unconditionally.
 *
 * @param   pRate   The state to reset.
 */
static void audioMixBufRateResetAlways(PAUDIOSTREAMRATE pRate)
{
    pRate->offDst = 0;
    pRate->offSrc = 0;
    for (uintptr_t i = 0; i < RT_ELEMENTS(pRate->SrcLast.ai32Samples); i++)
        pRate->SrcLast.ai32Samples[0] = 0;
}


/**
 * Resets the resampling state.
 *
 * @param   pRate   The state to reset.
 */
DECLINLINE(void) audioMixBufRateReset(PAUDIOSTREAMRATE pRate)
{
    if (pRate->offDst == 0)
    { /* likely */ }
    else
    {
        Assert(!pRate->fNoConversionNeeded);
        audioMixBufRateResetAlways(pRate);
    }
}


/**
 * Initializes the frame rate converter state.
 *
 * @returns VBox status code.
 * @param   pRate       The state to initialize.
 * @param   uSrcHz      The source frame rate.
 * @param   uDstHz      The destination frame rate.
 * @param   cChannels   The number of channels in a frame.
 */
DECLINLINE(int) audioMixBufRateInit(PAUDIOSTREAMRATE pRate, uint32_t uSrcHz, uint32_t uDstHz, uint8_t cChannels)
{
    /*
     * Do we need to set up frequency conversion?
     *
     * Some examples to get an idea of what uDstInc holds:
     *   44100 to 44100 -> (44100<<32) / 44100 = 0x01'00000000 (4294967296)
     *   22050 to 44100 -> (22050<<32) / 44100 = 0x00'80000000 (2147483648)
     *   44100 to 22050 -> (44100<<32) / 22050 = 0x02'00000000 (8589934592)
     *   44100 to 48000 -> (44100<<32) / 48000 = 0x00'EB333333 (3946001203.2)
     *   48000 to 44100 -> (48000<<32) / 44100 = 0x01'16A3B35F (4674794335.7823129251700680272109)
     */
    audioMixBufRateResetAlways(pRate);
    if (uSrcHz == uDstHz)
    {
        pRate->fNoConversionNeeded = true;
        pRate->uDstInc             = RT_BIT_64(32);
        pRate->pfnResample         = NULL;
    }
    else
    {
        pRate->fNoConversionNeeded = false;
        pRate->uDstInc             = ((uint64_t)uSrcHz << 32) / uDstHz;
        AssertReturn(uSrcHz != 0, VERR_INVALID_PARAMETER);
        switch (cChannels)
        {
            case  1: pRate->pfnResample = audioMixBufResample1ChGeneric; break;
            case  2: pRate->pfnResample = audioMixBufResample2ChGeneric; break;
            case  3: pRate->pfnResample = audioMixBufResample3ChGeneric; break;
            case  4: pRate->pfnResample = audioMixBufResample4ChGeneric; break;
            case  5: pRate->pfnResample = audioMixBufResample5ChGeneric; break;
            case  6: pRate->pfnResample = audioMixBufResample6ChGeneric; break;
            case  7: pRate->pfnResample = audioMixBufResample7ChGeneric; break;
            case  8: pRate->pfnResample = audioMixBufResample8ChGeneric; break;
            case  9: pRate->pfnResample = audioMixBufResample9ChGeneric; break;
            case 10: pRate->pfnResample = audioMixBufResample10ChGeneric; break;
            case 11: pRate->pfnResample = audioMixBufResample11ChGeneric; break;
            case 12: pRate->pfnResample = audioMixBufResample12ChGeneric; break;
            default:
                AssertMsgFailedReturn(("resampling %u changes is not implemented yet\n", cChannels), VERR_OUT_OF_RANGE);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Initializes a mixing buffer.
 *
 * @returns VBox status code.
 * @param   pMixBuf                 Mixing buffer to initialize.
 * @param   pszName                 Name of mixing buffer for easier identification. Optional.
 * @param   pProps                  PCM audio properties to use for the mixing buffer.
 * @param   cFrames                 Maximum number of audio frames the mixing buffer can hold.
 */
int AudioMixBufInit(PAUDIOMIXBUF pMixBuf, const char *pszName, PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pMixBuf, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pProps,  VERR_INVALID_POINTER);
    Assert(PDMAudioPropsAreValid(pProps));

    /*
     * Initialize all members, setting the volume to max (0dB).
     */
    pMixBuf->cFrames        = 0;
    pMixBuf->pi32Samples    = NULL;
    pMixBuf->cChannels      = 0;
    pMixBuf->cbFrame        = 0;
    pMixBuf->offRead        = 0;
    pMixBuf->offWrite       = 0;
    pMixBuf->cUsed          = 0;
    pMixBuf->Props          = *pProps;
    pMixBuf->Volume.fMuted  = false;
    pMixBuf->Volume.fAllMax = true;
    for (uintptr_t i = 0; i < RT_ELEMENTS(pMixBuf->Volume.auChannels); i++)
        pMixBuf->Volume.auChannels[i] = AUDIOMIXBUF_VOL_0DB;

    int rc;
    uint8_t const cChannels = PDMAudioPropsChannels(pProps);
    if (cChannels >= 1 && cChannels <= PDMAUDIO_MAX_CHANNELS)
    {
        pMixBuf->pszName = RTStrDup(pszName);
        if (pMixBuf->pszName)
        {
            pMixBuf->pi32Samples = (int32_t *)RTMemAllocZ(cFrames * cChannels * sizeof(pMixBuf->pi32Samples[0]));
            if (pMixBuf->pi32Samples)
            {
                pMixBuf->cFrames    = cFrames;
                pMixBuf->cChannels  = cChannels;
                pMixBuf->cbFrame    = cChannels * sizeof(pMixBuf->pi32Samples[0]);
                pMixBuf->uMagic     = AUDIOMIXBUF_MAGIC;
#ifdef AUDMIXBUF_LOG_ENABLED
                char szTmp[PDMAUDIOPROPSTOSTRING_MAX];
                AUDMIXBUF_LOG(("%s: %s - cFrames=%#x (%d)\n",
                               pMixBuf->pszName, PDMAudioPropsToString(pProps, szTmp, sizeof(szTmp)), cFrames, cFrames));
#endif
                return VINF_SUCCESS;
            }
            RTStrFree(pMixBuf->pszName);
            pMixBuf->pszName = NULL;
            rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    else
    {
        LogRelMaxFunc(64, ("cChannels=%d pszName=%s\n", cChannels, pszName));
        rc = VERR_OUT_OF_RANGE;
    }
    pMixBuf->uMagic = AUDIOMIXBUF_MAGIC_DEAD;
    return rc;
}

/**
 * Terminates (uninitializes) a mixing buffer.
 *
 * @param   pMixBuf     The mixing buffer.  Uninitialized mixer buffers will be
 *                      quietly ignored.  As will NULL.
 */
void AudioMixBufTerm(PAUDIOMIXBUF pMixBuf)
{
    if (!pMixBuf)
        return;

    /* Ignore calls for an uninitialized (zeroed) or already destroyed instance.  Happens a lot. */
    if (   pMixBuf->uMagic == 0
        || pMixBuf->uMagic == AUDIOMIXBUF_MAGIC_DEAD)
    {
        Assert(!pMixBuf->pszName);
        Assert(!pMixBuf->pi32Samples);
        Assert(!pMixBuf->cFrames);
        return;
    }

    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    pMixBuf->uMagic = ~AUDIOMIXBUF_MAGIC;

    if (pMixBuf->pszName)
    {
        AUDMIXBUF_LOG(("%s\n", pMixBuf->pszName));

        RTStrFree(pMixBuf->pszName);
        pMixBuf->pszName = NULL;
    }

    if (pMixBuf->pi32Samples)
    {
        Assert(pMixBuf->cFrames);
        RTMemFree(pMixBuf->pi32Samples);
        pMixBuf->pi32Samples = NULL;
    }

    pMixBuf->cFrames   = 0;
    pMixBuf->cChannels = 0;
}


/**
 * Drops all the frames in the given mixing buffer
 *
 * This will reset the read and write offsets to zero.
 *
 * @param   pMixBuf     The mixing buffer.  Uninitialized mixer buffers will be
 *                      quietly ignored.
 */
void AudioMixBufDrop(PAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturnVoid(pMixBuf);

    /* Ignore uninitialized (zeroed) mixer sink buffers (happens with AC'97 during VM construction). */
    if (   pMixBuf->uMagic == 0
        || pMixBuf->uMagic == AUDIOMIXBUF_MAGIC_DEAD)
        return;

    AUDMIXBUF_LOG(("%s\n", pMixBuf->pszName));

    pMixBuf->offRead  = 0;
    pMixBuf->offWrite = 0;
    pMixBuf->cUsed    = 0;
}


/**
 * Gets the maximum number of audio frames this buffer can hold.
 *
 * @returns Number of frames.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufSize(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return pMixBuf->cFrames;
}


/**
 * Gets the maximum number of bytes this buffer can hold.
 *
 * @returns Number of bytes.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufSizeBytes(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    AssertReturn(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC, 0);
    return AUDIOMIXBUF_F2B(pMixBuf, pMixBuf->cFrames);
}


/**
 * Worker for AudioMixBufUsed and AudioMixBufUsedBytes.
 */
DECLINLINE(uint32_t) audioMixBufUsedInternal(PCAUDIOMIXBUF pMixBuf)
{
    uint32_t const cFrames = pMixBuf->cFrames;
    uint32_t       cUsed   = pMixBuf->cUsed;
    AssertStmt(cUsed <= cFrames, cUsed = cFrames);
    return cUsed;
}


/**
 * Get the number of used (readable) frames in the buffer.
 *
 * @returns Number of frames.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufUsed(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return audioMixBufUsedInternal(pMixBuf);
}


/**
 * Get the number of (readable) bytes in the buffer.
 *
 * @returns Number of bytes.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufUsedBytes(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return AUDIOMIXBUF_F2B(pMixBuf, audioMixBufUsedInternal(pMixBuf));
}


/**
 * Worker for AudioMixBufFree and AudioMixBufFreeBytes.
 */
DECLINLINE(uint32_t) audioMixBufFreeInternal(PCAUDIOMIXBUF pMixBuf)
{
    uint32_t const cFrames = pMixBuf->cFrames;
    uint32_t       cUsed   = pMixBuf->cUsed;
    AssertStmt(cUsed <= cFrames, cUsed = cFrames);
    uint32_t const cFramesFree = cFrames - cUsed;

    AUDMIXBUF_LOG(("%s: %RU32 of %RU32\n", pMixBuf->pszName, cFramesFree, cFrames));
    return cFramesFree;
}


/**
 * Gets the free buffer space in frames.
 *
 * @return  Number of frames.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufFree(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return audioMixBufFreeInternal(pMixBuf);
}


/**
 * Gets the free buffer space in bytes.
 *
 * @return  Number of bytes.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufFreeBytes(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return AUDIOMIXBUF_F2B(pMixBuf, audioMixBufFreeInternal(pMixBuf));
}


/**
 * Checks if the buffer is empty.
 *
 * @retval  true if empty buffer.
 * @retval  false if not empty and there are frames to be processed.
 * @param   pMixBuf     The mixing buffer.
 */
bool AudioMixBufIsEmpty(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, true);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return pMixBuf->cUsed == 0;
}


/**
 * Get the current read position.
 *
 * This is for the testcase.
 *
 * @returns Frame number.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufReadPos(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return pMixBuf->offRead;
}


/**
 * Gets the current write position.
 *
 * This is for the testcase.
 *
 * @returns Frame number.
 * @param   pMixBuf     The mixing buffer.
 */
uint32_t AudioMixBufWritePos(PCAUDIOMIXBUF pMixBuf)
{
    AssertPtrReturn(pMixBuf, 0);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    return pMixBuf->offWrite;
}


/**
 * Creates a mapping between desination channels and source source channels.
 *
 * @param   paidxChannelMap     Where to store the mapping.  Indexed by
 *                              destination channel.  Entry is either source
 *                              channel index or -1 for zero and -2 for silence.
 * @param   pSrcProps           The source properties.
 * @param   pDstProps           The desination properties.
 */
static void audioMixBufInitChannelMap(int8_t paidxChannelMap[PDMAUDIO_MAX_CHANNELS],
                                      PCPDMAUDIOPCMPROPS pSrcProps, PCPDMAUDIOPCMPROPS pDstProps)
{
    uintptr_t const cDstChannels = PDMAudioPropsChannels(pDstProps);
    uintptr_t const cSrcChannels = PDMAudioPropsChannels(pSrcProps);
    uintptr_t       idxDst;
    for (idxDst = 0; idxDst < cDstChannels; idxDst++)
    {
        uint8_t const idDstCh = pDstProps->aidChannels[idxDst];
        if (idDstCh >= PDMAUDIOCHANNELID_FRONT_LEFT && idDstCh < PDMAUDIOCHANNELID_END)
        {
            uintptr_t idxSrc;
            for (idxSrc = 0; idxSrc < cSrcChannels; idxSrc++)
                if (idDstCh == pSrcProps->aidChannels[idxSrc])
                {
                    paidxChannelMap[idxDst] = idxSrc;
                    break;
                }
            if (idxSrc >= cSrcChannels)
            {
                /** @todo deal with mono. */
                paidxChannelMap[idxDst] = -2;
            }
        }
        else if (idDstCh == PDMAUDIOCHANNELID_UNKNOWN)
        {
            /** @todo What to do here?  Pick unused source channels in order? */
            paidxChannelMap[idxDst] = -2;
        }
        else
        {
            AssertMsg(idDstCh == PDMAUDIOCHANNELID_UNUSED_SILENCE || idDstCh == PDMAUDIOCHANNELID_UNUSED_ZERO,
                      ("idxDst=%u idDstCh=%u\n", idxDst, idDstCh));
            paidxChannelMap[idxDst] = idDstCh == PDMAUDIOCHANNELID_UNUSED_SILENCE ? -2 : -1;
        }
    }

    /* Set the remainder to -1 just to be sure their are safe. */
    for (; idxDst < PDMAUDIO_MAX_CHANNELS; idxDst++)
        paidxChannelMap[idxDst] = -1;
}


/**
 * Initializes the peek state, setting up encoder and (if necessary) resampling.
 *
 * @returns VBox status code.
 */
int AudioMixBufInitPeekState(PCAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFPEEKSTATE pState, PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtr(pMixBuf);
    AssertPtr(pState);
    AssertPtr(pProps);

    /*
     * Pick the encoding function first.
     */
    uint8_t const cbSample = PDMAudioPropsSampleSize(pProps);
    uint8_t const cSrcCh   = PDMAudioPropsChannels(&pMixBuf->Props);
    uint8_t const cDstCh   = PDMAudioPropsChannels(pProps);
    pState->cSrcChannels   = cSrcCh;
    pState->cDstChannels   = cDstCh;
    pState->cbDstFrame     = PDMAudioPropsFrameSize(pProps);
    audioMixBufInitChannelMap(pState->aidxChannelMap, &pMixBuf->Props, pProps);
    AssertReturn(cDstCh > 0 && cDstCh <= PDMAUDIO_MAX_CHANNELS, VERR_OUT_OF_RANGE);
    AssertReturn(cSrcCh > 0 && cSrcCh <= PDMAUDIO_MAX_CHANNELS, VERR_OUT_OF_RANGE);

    if (PDMAudioPropsIsSigned(pProps))
    {
        /* Assign generic encoder first. */
        switch (cbSample)
        {
            case 1: pState->pfnEncode = audioMixBufEncodeGenericS8; break;
            case 2: pState->pfnEncode = audioMixBufEncodeGenericS16; break;
            case 4: pState->pfnEncode = audioMixBufEncodeGenericS32; break;
            case 8:
                AssertReturn(pProps->fRaw, VERR_DISK_INVALID_FORMAT);
                pState->pfnEncode = audioMixBufEncodeGenericRaw;
                break;
            default:
                AssertMsgFailedReturn(("%u bytes\n", cbSample), VERR_OUT_OF_RANGE);
        }

        /* Any specializations available? */
        switch (cDstCh)
        {
            case 1:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode1ChTo1ChS8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode1ChTo1ChS16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode1ChTo1ChS32; break;
                        case 8: pState->pfnEncode = audioMixBufEncode1ChTo1ChRaw; break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode2ChTo1ChS8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode2ChTo1ChS16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode2ChTo1ChS32; break;
                        case 8: pState->pfnEncode = audioMixBufEncode2ChTo1ChRaw; break;
                    }
                break;

            case 2:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode1ChTo2ChS8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode1ChTo2ChS16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode1ChTo2ChS32; break;
                        case 8: pState->pfnEncode = audioMixBufEncode1ChTo2ChRaw; break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode2ChTo2ChS8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode2ChTo2ChS16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode2ChTo2ChS32; break;
                        case 8: pState->pfnEncode = audioMixBufEncode2ChTo2ChRaw; break;
                    }
                break;
        }
    }
    else
    {
        /* Assign generic encoder first. */
        switch (cbSample)
        {
            case 1: pState->pfnEncode = audioMixBufEncodeGenericU8; break;
            case 2: pState->pfnEncode = audioMixBufEncodeGenericU16; break;
            case 4: pState->pfnEncode = audioMixBufEncodeGenericU32; break;
            default:
                AssertMsgFailedReturn(("%u bytes\n", cbSample), VERR_OUT_OF_RANGE);
        }

        /* Any specializations available? */
        switch (cDstCh)
        {
            case 1:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode1ChTo1ChU8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode1ChTo1ChU16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode1ChTo1ChU32; break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode2ChTo1ChU8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode2ChTo1ChU16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode2ChTo1ChU32; break;
                    }
                break;

            case 2:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode1ChTo2ChU8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode1ChTo2ChU16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode1ChTo2ChU32; break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1: pState->pfnEncode = audioMixBufEncode2ChTo2ChU8; break;
                        case 2: pState->pfnEncode = audioMixBufEncode2ChTo2ChU16; break;
                        case 4: pState->pfnEncode = audioMixBufEncode2ChTo2ChU32; break;
                    }
                break;
        }
    }

    int rc = audioMixBufRateInit(&pState->Rate, PDMAudioPropsHz(&pMixBuf->Props), PDMAudioPropsHz(pProps), cSrcCh);
    AUDMIXBUF_LOG(("%s: %RU32 Hz to %RU32 Hz => uDstInc=0x%'RX64\n", pMixBuf->pszName, PDMAudioPropsHz(&pMixBuf->Props),
                   PDMAudioPropsHz(pProps), pState->Rate.uDstInc));
    return rc;
}


/**
 * Initializes the write/blend state, setting up decoders and (if necessary)
 * resampling.
 *
 * @returns VBox status code.
 */
int AudioMixBufInitWriteState(PCAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtr(pMixBuf);
    AssertPtr(pState);
    AssertPtr(pProps);

    /*
     * Pick the encoding function first.
     */
    uint8_t const cbSample = PDMAudioPropsSampleSize(pProps);
    uint8_t const cSrcCh   = PDMAudioPropsChannels(pProps);
    uint8_t const cDstCh   = PDMAudioPropsChannels(&pMixBuf->Props);
    pState->cSrcChannels   = cSrcCh;
    pState->cDstChannels   = cDstCh;
    pState->cbSrcFrame     = PDMAudioPropsFrameSize(pProps);
    audioMixBufInitChannelMap(pState->aidxChannelMap, pProps, &pMixBuf->Props);

    if (PDMAudioPropsIsSigned(pProps))
    {
        /* Assign generic decoders first. */
        switch (cbSample)
        {
            case 1:
                pState->pfnDecode      = audioMixBufDecodeGenericS8;
                pState->pfnDecodeBlend = audioMixBufDecodeGenericS8Blend;
                break;
            case 2:
                pState->pfnDecode      = audioMixBufDecodeGenericS16;
                pState->pfnDecodeBlend = audioMixBufDecodeGenericS16Blend;
                break;
            case 4:
                pState->pfnDecode      = audioMixBufDecodeGenericS32;
                pState->pfnDecodeBlend = audioMixBufDecodeGenericS32Blend;
                break;
            case 8:
                AssertReturn(pProps->fRaw, VERR_DISK_INVALID_FORMAT);
                pState->pfnDecode      = audioMixBufDecodeGenericRaw;
                pState->pfnDecodeBlend = audioMixBufDecodeGenericRawBlend;
                break;
            default:
                AssertMsgFailedReturn(("%u bytes\n", cbSample), VERR_OUT_OF_RANGE);
        }

        /* Any specializations available? */
        switch (cDstCh)
        {
            case 1:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode1ChTo1ChS8;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo1ChS8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode1ChTo1ChS16;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo1ChS16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode1ChTo1ChS32;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo1ChS32Blend;
                            break;
                        case 8:
                            pState->pfnDecode      = audioMixBufDecode1ChTo1ChRaw;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo1ChRawBlend;
                            break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode2ChTo1ChS8;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo1ChS8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode2ChTo1ChS16;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo1ChS16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode2ChTo1ChS32;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo1ChS32Blend;
                            break;
                        case 8:
                            pState->pfnDecode      = audioMixBufDecode2ChTo1ChRaw;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo1ChRawBlend;
                            break;
                    }
                break;

            case 2:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode1ChTo2ChS8;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo2ChS8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode1ChTo2ChS16;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo2ChS16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode1ChTo2ChS32;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo2ChS32Blend;
                            break;
                        case 8:
                            pState->pfnDecode      = audioMixBufDecode1ChTo2ChRaw;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo2ChRawBlend;
                            break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode2ChTo2ChS8;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo2ChS8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode2ChTo2ChS16;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo2ChS16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode2ChTo2ChS32;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo2ChS32Blend;
                            break;
                        case 8:
                            pState->pfnDecode      = audioMixBufDecode2ChTo2ChRaw;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo2ChRawBlend;
                            break;
                    }
                break;
        }
    }
    else
    {
        /* Assign generic decoders first. */
        switch (cbSample)
        {
            case 1:
                pState->pfnDecode      = audioMixBufDecodeGenericU8;
                pState->pfnDecodeBlend = audioMixBufDecodeGenericU8Blend;
                break;
            case 2:
                pState->pfnDecode      = audioMixBufDecodeGenericU16;
                pState->pfnDecodeBlend = audioMixBufDecodeGenericU16Blend;
                break;
            case 4:
                pState->pfnDecode      = audioMixBufDecodeGenericU32;
                pState->pfnDecodeBlend = audioMixBufDecodeGenericU32Blend;
                break;
            default:
                AssertMsgFailedReturn(("%u bytes\n", cbSample), VERR_OUT_OF_RANGE);
        }

        /* Any specializations available? */
        switch (cDstCh)
        {
            case 1:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode1ChTo1ChU8;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo1ChU8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode1ChTo1ChU16;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo1ChU16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode1ChTo1ChU32;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo1ChU32Blend;
                            break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode2ChTo1ChU8;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo1ChU8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode2ChTo1ChU16;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo1ChU16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode2ChTo1ChU32;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo1ChU32Blend;
                            break;
                    }
                break;

            case 2:
                if (cSrcCh == 1)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode1ChTo2ChU8;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo2ChU8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode1ChTo2ChU16;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo2ChU16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode1ChTo2ChU32;
                            pState->pfnDecodeBlend = audioMixBufDecode1ChTo2ChU32Blend;
                            break;
                    }
                else if (cSrcCh == 2)
                    switch (cbSample)
                    {
                        case 1:
                            pState->pfnDecode      = audioMixBufDecode2ChTo2ChU8;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo2ChU8Blend;
                            break;
                        case 2:
                            pState->pfnDecode      = audioMixBufDecode2ChTo2ChU16;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo2ChU16Blend;
                            break;
                        case 4:
                            pState->pfnDecode      = audioMixBufDecode2ChTo2ChU32;
                            pState->pfnDecodeBlend = audioMixBufDecode2ChTo2ChU32Blend;
                            break;
                    }
                break;
        }
    }

    int rc = audioMixBufRateInit(&pState->Rate, PDMAudioPropsHz(pProps), PDMAudioPropsHz(&pMixBuf->Props), cDstCh);
    AUDMIXBUF_LOG(("%s: %RU32 Hz to %RU32 Hz => uDstInc=0x%'RX64\n", pMixBuf->pszName, PDMAudioPropsHz(pProps),
                   PDMAudioPropsHz(&pMixBuf->Props), pState->Rate.uDstInc));
    return rc;
}


/**
 * Worker for AudioMixBufPeek that handles the rate conversion case.
 */
DECL_NO_INLINE(static, void)
audioMixBufPeekResampling(PCAUDIOMIXBUF pMixBuf, uint32_t offSrcFrame, uint32_t cMaxSrcFrames, uint32_t *pcSrcFramesPeeked,
                          PAUDIOMIXBUFPEEKSTATE pState, void *pvDst, uint32_t cbDst, uint32_t *pcbDstPeeked)
{
    *pcSrcFramesPeeked = 0;
    *pcbDstPeeked      = 0;
    while (cMaxSrcFrames > 0 && cbDst >= pState->cbDstFrame)
    {
        /* Rate conversion into temporary buffer. */
        int32_t  ai32DstRate[1024];
        uint32_t cSrcFrames    = RT_MIN(pMixBuf->cFrames - offSrcFrame, cMaxSrcFrames);
        uint32_t cDstMaxFrames = RT_MIN(RT_ELEMENTS(ai32DstRate) / pState->cSrcChannels, cbDst / pState->cbDstFrame);
        uint32_t const cDstFrames = pState->Rate.pfnResample(ai32DstRate, cDstMaxFrames,
                                                             &pMixBuf->pi32Samples[offSrcFrame * pMixBuf->cChannels],
                                                             cSrcFrames, &cSrcFrames, &pState->Rate);
        *pcSrcFramesPeeked += cSrcFrames;
        cMaxSrcFrames      -= cSrcFrames;
        offSrcFrame         = (offSrcFrame + cSrcFrames) % pMixBuf->cFrames;

        /* Encode the converted frames. */
        uint32_t const cbDstEncoded = cDstFrames * pState->cbDstFrame;
        pState->pfnEncode(pvDst, ai32DstRate, cDstFrames, pState);
        *pcbDstPeeked      += cbDstEncoded;
        cbDst              -= cbDstEncoded;
        pvDst               = (uint8_t *)pvDst + cbDstEncoded;
    }
}


/**
 * Copies data out of the mixing buffer, converting it if needed, but leaves the
 * read offset untouched.
 *
 * @param   pMixBuf             The mixing buffer.
 * @param   offSrcFrame         The offset to start reading at relative to
 *                              current read position (offRead).  The caller has
 *                              made sure there is at least this number of
 *                              frames available in the buffer before calling.
 * @param   cMaxSrcFrames       Maximum number of frames to read.
 * @param   pcSrcFramesPeeked   Where to return the actual number of frames read
 *                              from the mixing buffer.
 * @param   pState              Output configuration & conversion state.
 * @param   pvDst               The destination buffer.
 * @param   cbDst               The size of the destination buffer in bytes.
 * @param   pcbDstPeeked        Where to put the actual number of bytes
 *                              returned.
 */
void AudioMixBufPeek(PCAUDIOMIXBUF pMixBuf, uint32_t offSrcFrame, uint32_t cMaxSrcFrames, uint32_t *pcSrcFramesPeeked,
                     PAUDIOMIXBUFPEEKSTATE pState, void *pvDst, uint32_t cbDst, uint32_t *pcbDstPeeked)
{
    /*
     * Check inputs.
     */
    AssertPtr(pMixBuf);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    AssertPtr(pState);
    AssertPtr(pState->pfnEncode);
    Assert(pState->cSrcChannels == PDMAudioPropsChannels(&pMixBuf->Props));
    Assert(cMaxSrcFrames > 0);
    Assert(cMaxSrcFrames <= pMixBuf->cFrames);
    Assert(offSrcFrame <= pMixBuf->cFrames);
    Assert(offSrcFrame + cMaxSrcFrames <= pMixBuf->cUsed);
    AssertPtr(pcSrcFramesPeeked);
    AssertPtr(pvDst);
    Assert(cbDst >= pState->cbDstFrame);
    AssertPtr(pcbDstPeeked);

    /*
     * Make start frame absolute.
     */
    offSrcFrame = (pMixBuf->offRead + offSrcFrame) % pMixBuf->cFrames;

    /*
     * Hopefully no sample rate conversion is necessary...
     */
    if (pState->Rate.fNoConversionNeeded)
    {
        /* Figure out how much we should convert. */
        cMaxSrcFrames      = RT_MIN(cMaxSrcFrames, cbDst / pState->cbDstFrame);
        *pcSrcFramesPeeked = cMaxSrcFrames;
        *pcbDstPeeked      = cMaxSrcFrames * pState->cbDstFrame;

        /* First chunk. */
        uint32_t const cSrcFrames1 = RT_MIN(pMixBuf->cFrames - offSrcFrame, cMaxSrcFrames);
        pState->pfnEncode(pvDst, &pMixBuf->pi32Samples[offSrcFrame * pMixBuf->cChannels], cSrcFrames1, pState);

        /* Another chunk from the start of the mixing buffer? */
        if (cMaxSrcFrames > cSrcFrames1)
            pState->pfnEncode((uint8_t *)pvDst + cSrcFrames1 * pState->cbDstFrame,
                              &pMixBuf->pi32Samples[0], cMaxSrcFrames - cSrcFrames1, pState);

        //Log9Func(("*pcbDstPeeked=%#x\n%32.*Rhxd\n", *pcbDstPeeked, *pcbDstPeeked, pvDst));
    }
    else
        audioMixBufPeekResampling(pMixBuf, offSrcFrame, cMaxSrcFrames, pcSrcFramesPeeked, pState, pvDst, cbDst, pcbDstPeeked);
}


/**
 * Worker for AudioMixBufWrite that handles the rate conversion case.
 */
DECL_NO_INLINE(static, void)
audioMixBufWriteResampling(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, const void *pvSrcBuf, uint32_t cbSrcBuf,
                           uint32_t offDstFrame, uint32_t cDstMaxFrames, uint32_t *pcDstFramesWritten)
{
    *pcDstFramesWritten = 0;
    while (cDstMaxFrames > 0 && cbSrcBuf >= pState->cbSrcFrame)
    {
        /* Decode into temporary buffer. */
        int32_t  ai32Decoded[1024];
        uint32_t cFramesDecoded = RT_MIN(RT_ELEMENTS(ai32Decoded) / pState->cDstChannels, cbSrcBuf / pState->cbSrcFrame);
        pState->pfnDecode(ai32Decoded, pvSrcBuf, cFramesDecoded, pState);
        cbSrcBuf -= cFramesDecoded * pState->cbSrcFrame;
        pvSrcBuf  = (uint8_t const *)pvSrcBuf + cFramesDecoded * pState->cbSrcFrame;

        /* Rate convert that into the mixer. */
        uint32_t iFrameDecoded = 0;
        while (iFrameDecoded < cFramesDecoded)
        {
            uint32_t cDstMaxFramesNow = RT_MIN(pMixBuf->cFrames - offDstFrame, cDstMaxFrames);
            uint32_t cSrcFrames       = cFramesDecoded - iFrameDecoded;
            uint32_t const cDstFrames = pState->Rate.pfnResample(&pMixBuf->pi32Samples[offDstFrame * pMixBuf->cChannels],
                                                                 cDstMaxFramesNow,
                                                                 &ai32Decoded[iFrameDecoded * pState->cDstChannels],
                                                                 cSrcFrames, &cSrcFrames, &pState->Rate);

            iFrameDecoded       += cSrcFrames;
            *pcDstFramesWritten += cDstFrames;
            offDstFrame          = (offDstFrame + cDstFrames) % pMixBuf->cFrames;
        }
    }

    /** @todo How to squeeze odd frames out of 22050 => 44100 conversion?   */
}


/**
 * Writes @a cbSrcBuf bytes to the mixer buffer starting at @a offDstFrame,
 * converting it as needed, leaving the write offset untouched.
 *
 * @param   pMixBuf             The mixing buffer.
 * @param   pState              Source configuration & conversion state.
 * @param   pvSrcBuf            The source frames.
 * @param   cbSrcBuf            Number of bytes of source frames.  This will be
 *                              convered in full.
 * @param   offDstFrame         Mixing buffer offset relative to the write
 *                              position.
 * @param   cDstMaxFrames       Max number of frames to write.
 * @param   pcDstFramesWritten  Where to return the number of frames actually
 *                              written.
 *
 * @note    Does not advance the write position, please call AudioMixBufCommit()
 *          to do that.
 */
void AudioMixBufWrite(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, const void *pvSrcBuf, uint32_t cbSrcBuf,
                      uint32_t offDstFrame, uint32_t cDstMaxFrames, uint32_t *pcDstFramesWritten)
{
    /*
     * Check inputs.
     */
    AssertPtr(pMixBuf);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    AssertPtr(pState);
    AssertPtr(pState->pfnDecode);
    AssertPtr(pState->pfnDecodeBlend);
    Assert(pState->cDstChannels == PDMAudioPropsChannels(&pMixBuf->Props));
    Assert(cDstMaxFrames > 0);
    Assert(cDstMaxFrames <= pMixBuf->cFrames - pMixBuf->cUsed);
    Assert(offDstFrame <= pMixBuf->cFrames);
    AssertPtr(pvSrcBuf);
    Assert(!(cbSrcBuf % pState->cbSrcFrame));
    AssertPtr(pcDstFramesWritten);

    /*
     * Make start frame absolute.
     */
    offDstFrame = (pMixBuf->offWrite + offDstFrame) % pMixBuf->cFrames;

    /*
     * Hopefully no sample rate conversion is necessary...
     */
    if (pState->Rate.fNoConversionNeeded)
    {
        /* Figure out how much we should convert. */
        Assert(cDstMaxFrames >= cbSrcBuf / pState->cbSrcFrame);
        cDstMaxFrames       = RT_MIN(cDstMaxFrames, cbSrcBuf / pState->cbSrcFrame);
        *pcDstFramesWritten = cDstMaxFrames;

        //Log10Func(("cbSrc=%#x\n%32.*Rhxd\n", pState->cbSrcFrame * cDstMaxFrames, pState->cbSrcFrame * cDstMaxFrames, pvSrcBuf));

        /* First chunk. */
        uint32_t const cDstFrames1 = RT_MIN(pMixBuf->cFrames - offDstFrame, cDstMaxFrames);
        pState->pfnDecode(&pMixBuf->pi32Samples[offDstFrame * pMixBuf->cChannels], pvSrcBuf, cDstFrames1, pState);
        //Log8Func(("offDstFrame=%#x cDstFrames1=%#x\n%32.*Rhxd\n", offDstFrame, cDstFrames1,
        //          cDstFrames1 * pMixBuf->cbFrame, &pMixBuf->pi32Samples[offDstFrame * pMixBuf->cChannels]));

        /* Another chunk from the start of the mixing buffer? */
        if (cDstMaxFrames > cDstFrames1)
        {
            pState->pfnDecode(&pMixBuf->pi32Samples[0], (uint8_t *)pvSrcBuf + cDstFrames1 * pState->cbSrcFrame,
                              cDstMaxFrames - cDstFrames1, pState);
            //Log8Func(("cDstFrames2=%#x\n%32.*Rhxd\n", cDstMaxFrames - cDstFrames1,
            //          (cDstMaxFrames - cDstFrames1) * pMixBuf->cbFrame, &pMixBuf->pi32Samples[0]));
        }
    }
    else
        audioMixBufWriteResampling(pMixBuf, pState, pvSrcBuf, cbSrcBuf, offDstFrame, cDstMaxFrames, pcDstFramesWritten);
}


/**
 * Worker for AudioMixBufBlend that handles the rate conversion case.
 */
DECL_NO_INLINE(static, void)
audioMixBufBlendResampling(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, const void *pvSrcBuf, uint32_t cbSrcBuf,
                           uint32_t offDstFrame, uint32_t cDstMaxFrames, uint32_t *pcDstFramesBlended)
{
    *pcDstFramesBlended = 0;
    while (cDstMaxFrames > 0 && cbSrcBuf >= pState->cbSrcFrame)
    {
        /* Decode into temporary buffer.  This then has the destination channel count. */
        int32_t  ai32Decoded[1024];
        uint32_t cFramesDecoded = RT_MIN(RT_ELEMENTS(ai32Decoded) / pState->cDstChannels, cbSrcBuf / pState->cbSrcFrame);
        pState->pfnDecode(ai32Decoded, pvSrcBuf, cFramesDecoded, pState);
        cbSrcBuf -= cFramesDecoded * pState->cbSrcFrame;
        pvSrcBuf  = (uint8_t const *)pvSrcBuf + cFramesDecoded * pState->cbSrcFrame;

        /* Rate convert that into another temporary buffer and then blend that into the mixer. */
        uint32_t iFrameDecoded = 0;
        while (iFrameDecoded < cFramesDecoded)
        {
            int32_t  ai32Rate[1024];
            uint32_t cDstMaxFramesNow = RT_MIN(RT_ELEMENTS(ai32Rate) / pState->cDstChannels, cDstMaxFrames);
            uint32_t cSrcFrames       = cFramesDecoded - iFrameDecoded;
            uint32_t const cDstFrames = pState->Rate.pfnResample(&ai32Rate[0], cDstMaxFramesNow,
                                                                 &ai32Decoded[iFrameDecoded * pState->cDstChannels],
                                                                 cSrcFrames, &cSrcFrames, &pState->Rate);

            /* First chunk.*/
            uint32_t const cDstFrames1 = RT_MIN(pMixBuf->cFrames - offDstFrame, cDstFrames);
            audioMixBufBlendBuffer(&pMixBuf->pi32Samples[offDstFrame * pMixBuf->cChannels],
                                   ai32Rate, cDstFrames1, pState->cDstChannels);

            /* Another chunk from the start of the mixing buffer? */
            if (cDstFrames > cDstFrames1)
                audioMixBufBlendBuffer(&pMixBuf->pi32Samples[0], &ai32Rate[cDstFrames1 * pState->cDstChannels],
                                       cDstFrames - cDstFrames1, pState->cDstChannels);

            /* Advance */
            iFrameDecoded       += cSrcFrames;
            *pcDstFramesBlended += cDstFrames;
            offDstFrame          = (offDstFrame + cDstFrames) % pMixBuf->cFrames;
        }
    }

    /** @todo How to squeeze odd frames out of 22050 => 44100 conversion?   */
}


/**
 * @todo not sure if 'blend' is the appropriate term here, but you know what
 *       we mean.
 */
void AudioMixBufBlend(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, const void *pvSrcBuf, uint32_t cbSrcBuf,
                      uint32_t offDstFrame, uint32_t cDstMaxFrames, uint32_t *pcDstFramesBlended)
{
    /*
     * Check inputs.
     */
    AssertPtr(pMixBuf);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    AssertPtr(pState);
    AssertPtr(pState->pfnDecode);
    AssertPtr(pState->pfnDecodeBlend);
    Assert(pState->cDstChannels == PDMAudioPropsChannels(&pMixBuf->Props));
    Assert(cDstMaxFrames > 0);
    Assert(cDstMaxFrames <= pMixBuf->cFrames - pMixBuf->cUsed);
    Assert(offDstFrame <= pMixBuf->cFrames);
    AssertPtr(pvSrcBuf);
    Assert(!(cbSrcBuf % pState->cbSrcFrame));
    AssertPtr(pcDstFramesBlended);

    /*
     * Make start frame absolute.
     */
    offDstFrame = (pMixBuf->offWrite + offDstFrame) % pMixBuf->cFrames;

    /*
     * Hopefully no sample rate conversion is necessary...
     */
    if (pState->Rate.fNoConversionNeeded)
    {
        /* Figure out how much we should convert. */
        Assert(cDstMaxFrames >= cbSrcBuf / pState->cbSrcFrame);
        cDstMaxFrames       = RT_MIN(cDstMaxFrames, cbSrcBuf / pState->cbSrcFrame);
        *pcDstFramesBlended = cDstMaxFrames;

        /* First chunk. */
        uint32_t const cDstFrames1 = RT_MIN(pMixBuf->cFrames - offDstFrame, cDstMaxFrames);
        pState->pfnDecodeBlend(&pMixBuf->pi32Samples[offDstFrame * pMixBuf->cChannels], pvSrcBuf, cDstFrames1, pState);

        /* Another chunk from the start of the mixing buffer? */
        if (cDstMaxFrames > cDstFrames1)
            pState->pfnDecodeBlend(&pMixBuf->pi32Samples[0], (uint8_t *)pvSrcBuf + cDstFrames1 * pState->cbSrcFrame,
                                   cDstMaxFrames - cDstFrames1, pState);
    }
    else
        audioMixBufBlendResampling(pMixBuf, pState, pvSrcBuf, cbSrcBuf, offDstFrame, cDstMaxFrames, pcDstFramesBlended);
}


/**
 * Writes @a cFrames of silence at @a offFrame relative to current write pos.
 *
 * This will also adjust the resampling state.
 *
 * @param   pMixBuf     The mixing buffer.
 * @param   pState      The write state.
 * @param   offFrame    Where to start writing silence relative to the current
 *                      write position.
 * @param   cFrames     Number of frames of silence.
 * @sa      AudioMixBufWrite
 *
 * @note    Does not advance the write position, please call AudioMixBufCommit()
 *          to do that.
 */
void AudioMixBufSilence(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, uint32_t offFrame, uint32_t cFrames)
{
    /*
     * Check inputs.
     */
    AssertPtr(pMixBuf);
    Assert(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);
    AssertPtr(pState);
    AssertPtr(pState->pfnDecode);
    AssertPtr(pState->pfnDecodeBlend);
    Assert(pState->cDstChannels == PDMAudioPropsChannels(&pMixBuf->Props));
    Assert(cFrames > 0);
#ifdef VBOX_STRICT
    uint32_t const cMixBufFree = pMixBuf->cFrames - pMixBuf->cUsed;
#endif
    Assert(cFrames <= cMixBufFree);
    Assert(offFrame < cMixBufFree);
    Assert(offFrame + cFrames <= cMixBufFree);

    /*
     * Make start frame absolute.
     */
    offFrame = (pMixBuf->offWrite + offFrame) % pMixBuf->cFrames;

    /*
     * First chunk.
     */
    uint32_t const cFramesChunk1 = RT_MIN(pMixBuf->cFrames - offFrame, cFrames);
    RT_BZERO(&pMixBuf->pi32Samples[offFrame * pMixBuf->cChannels], cFramesChunk1 * pMixBuf->cbFrame);

    /*
     * Second chunk, if needed.
     */
    if (cFrames > cFramesChunk1)
    {
        cFrames -= cFramesChunk1;
        AssertStmt(cFrames <= pMixBuf->cFrames, cFrames = pMixBuf->cFrames);
        RT_BZERO(&pMixBuf->pi32Samples[0], cFrames * pMixBuf->cbFrame);
    }

    /*
     * Reset the resampling state.
     */
    audioMixBufRateReset(&pState->Rate);
}


/**
 * Records a blending gap (silence) of @a cFrames.
 *
 * This is used to adjust or reset the resampling state so we start from a
 * silence state the next time we need to blend or write using @a pState.
 *
 * @param   pMixBuf     The mixing buffer.
 * @param   pState      The write state.
 * @param   cFrames     Number of frames of silence.
 * @sa      AudioMixBufSilence
 */
void AudioMixBufBlendGap(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, uint32_t cFrames)
{
    /*
     * For now we'll just reset the resampling state regardless of how many
     * frames of silence there is.
     */
    audioMixBufRateReset(&pState->Rate);
    RT_NOREF(pMixBuf, cFrames);
}


/**
 * Advances the read position of the buffer.
 *
 * For use after done peeking with AudioMixBufPeek().
 *
 * @param   pMixBuf     The mixing buffer.
 * @param   cFrames     Number of frames to advance.
 * @sa      AudioMixBufCommit
 */
void AudioMixBufAdvance(PAUDIOMIXBUF pMixBuf, uint32_t cFrames)
{
    AssertPtrReturnVoid(pMixBuf);
    AssertReturnVoid(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);

    AssertStmt(cFrames <= pMixBuf->cUsed, cFrames = pMixBuf->cUsed);
    pMixBuf->cUsed   -= cFrames;
    pMixBuf->offRead  = (pMixBuf->offRead + cFrames) % pMixBuf->cFrames;
    LogFlowFunc(("%s: Advanced %u frames: offRead=%u cUsed=%u\n", pMixBuf->pszName, cFrames, pMixBuf->offRead, pMixBuf->cUsed));
}


/**
 * Worker for audioMixAdjustVolume that adjust one contiguous chunk.
 */
static void audioMixAdjustVolumeWorker(PAUDIOMIXBUF pMixBuf, uint32_t off, uint32_t cFrames)
{
    int32_t       *pi32Samples = &pMixBuf->pi32Samples[off * pMixBuf->cChannels];
    switch (pMixBuf->cChannels)
    {
        case 1:
        {
            uint32_t const uFactorCh0 = pMixBuf->Volume.auChannels[0];
            while (cFrames-- > 0)
            {
                *pi32Samples = (int32_t)(ASMMult2xS32RetS64(*pi32Samples, uFactorCh0) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples++;
            }
            break;
        }

        case 2:
        {
            uint32_t const uFactorCh0 = pMixBuf->Volume.auChannels[0];
            uint32_t const uFactorCh1 = pMixBuf->Volume.auChannels[1];
            while (cFrames-- > 0)
            {
                pi32Samples[0] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[0], uFactorCh0) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[1] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[1], uFactorCh1) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples += 2;
            }
            break;
        }

        case 3:
        {
            uint32_t const uFactorCh0 = pMixBuf->Volume.auChannels[0];
            uint32_t const uFactorCh1 = pMixBuf->Volume.auChannels[1];
            uint32_t const uFactorCh2 = pMixBuf->Volume.auChannels[2];
            while (cFrames-- > 0)
            {
                pi32Samples[0] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[0], uFactorCh0) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[1] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[1], uFactorCh1) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[2] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[2], uFactorCh2) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples += 3;
            }
            break;
        }

        case 4:
        {
            uint32_t const uFactorCh0 = pMixBuf->Volume.auChannels[0];
            uint32_t const uFactorCh1 = pMixBuf->Volume.auChannels[1];
            uint32_t const uFactorCh2 = pMixBuf->Volume.auChannels[2];
            uint32_t const uFactorCh3 = pMixBuf->Volume.auChannels[3];
            while (cFrames-- > 0)
            {
                pi32Samples[0] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[0], uFactorCh0) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[1] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[1], uFactorCh1) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[2] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[2], uFactorCh2) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[3] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[3], uFactorCh3) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples += 4;
            }
            break;
        }

        case 5:
        {
            uint32_t const uFactorCh0 = pMixBuf->Volume.auChannels[0];
            uint32_t const uFactorCh1 = pMixBuf->Volume.auChannels[1];
            uint32_t const uFactorCh2 = pMixBuf->Volume.auChannels[2];
            uint32_t const uFactorCh3 = pMixBuf->Volume.auChannels[3];
            uint32_t const uFactorCh4 = pMixBuf->Volume.auChannels[4];
            while (cFrames-- > 0)
            {
                pi32Samples[0] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[0], uFactorCh0) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[1] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[1], uFactorCh1) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[2] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[2], uFactorCh2) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[3] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[3], uFactorCh3) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[4] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[4], uFactorCh4) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples += 5;
            }
            break;
        }

        case 6:
        {
            uint32_t const uFactorCh0 = pMixBuf->Volume.auChannels[0];
            uint32_t const uFactorCh1 = pMixBuf->Volume.auChannels[1];
            uint32_t const uFactorCh2 = pMixBuf->Volume.auChannels[2];
            uint32_t const uFactorCh3 = pMixBuf->Volume.auChannels[3];
            uint32_t const uFactorCh4 = pMixBuf->Volume.auChannels[4];
            uint32_t const uFactorCh5 = pMixBuf->Volume.auChannels[5];
            while (cFrames-- > 0)
            {
                pi32Samples[0] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[0], uFactorCh0) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[1] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[1], uFactorCh1) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[2] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[2], uFactorCh2) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[3] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[3], uFactorCh3) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[4] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[4], uFactorCh4) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples[5] = (int32_t)(ASMMult2xS32RetS64(pi32Samples[5], uFactorCh5) >> AUDIOMIXBUF_VOL_SHIFT);
                pi32Samples += 6;
            }
            break;
        }

        default:
            while (cFrames-- > 0)
                for (uint32_t iCh = 0; iCh < pMixBuf->cChannels; iCh++, pi32Samples++)
                    *pi32Samples = ASMMult2xS32RetS64(*pi32Samples, pMixBuf->Volume.auChannels[iCh]) >> AUDIOMIXBUF_VOL_SHIFT;
            break;
    }
}


/**
 * Does volume adjustments for the given stretch of the buffer.
 *
 * @param   pMixBuf     The mixing buffer.
 * @param   offFirst    Where to start (validated).
 * @param   cFrames     How many frames (validated).
 */
static void audioMixAdjustVolume(PAUDIOMIXBUF pMixBuf, uint32_t offFirst, uint32_t cFrames)
{
    /* Caller has already validated these, so we don't need to repeat that in non-strict builds. */
    Assert(offFirst < pMixBuf->cFrames);
    Assert(cFrames <= pMixBuf->cFrames);

    /*
     * Muted?
     */
    if (pMixBuf->Volume.fMuted)
    {
        /* first chunk */
        uint32_t const cFramesChunk1 = RT_MIN(pMixBuf->cFrames - offFirst, cFrames);
        RT_BZERO(&pMixBuf->pi32Samples[offFirst * pMixBuf->cChannels], pMixBuf->cbFrame * cFramesChunk1);

        /* second chunk */
        if (cFramesChunk1 < cFrames)
            RT_BZERO(&pMixBuf->pi32Samples[0], pMixBuf->cbFrame * (cFrames - cFramesChunk1));
    }
    /*
     * Less than max volume?
     */
    else if (!pMixBuf->Volume.fAllMax)
    {
        /* first chunk */
        uint32_t const cFramesChunk1 = RT_MIN(pMixBuf->cFrames - offFirst, cFrames);
        audioMixAdjustVolumeWorker(pMixBuf, offFirst, cFramesChunk1);

        /* second chunk */
        if (cFramesChunk1 < cFrames)
            audioMixAdjustVolumeWorker(pMixBuf, 0, cFrames - cFramesChunk1);
    }
}


/**
 * Adjust for volume settings and advances the write position of the buffer.
 *
 * For use after done peeking with AudioMixBufWrite(), AudioMixBufSilence(),
 * AudioMixBufBlend() and AudioMixBufBlendGap().
 *
 * @param   pMixBuf     The mixing buffer.
 * @param   cFrames     Number of frames to advance.
 * @sa      AudioMixBufAdvance, AudioMixBufSetVolume
 */
void AudioMixBufCommit(PAUDIOMIXBUF pMixBuf, uint32_t cFrames)
{
    AssertPtrReturnVoid(pMixBuf);
    AssertReturnVoid(pMixBuf->uMagic == AUDIOMIXBUF_MAGIC);

    AssertStmt(cFrames <= pMixBuf->cFrames - pMixBuf->cUsed, cFrames = pMixBuf->cFrames - pMixBuf->cUsed);

    audioMixAdjustVolume(pMixBuf, pMixBuf->offWrite, cFrames);

    pMixBuf->cUsed   += cFrames;
    pMixBuf->offWrite = (pMixBuf->offWrite + cFrames) % pMixBuf->cFrames;
    LogFlowFunc(("%s: Advanced %u frames: offWrite=%u cUsed=%u\n", pMixBuf->pszName, cFrames, pMixBuf->offWrite, pMixBuf->cUsed));
}


/**
 * Sets the volume.
 *
 * The volume adjustments are applied by AudioMixBufCommit().
 *
 * @param   pMixBuf     Mixing buffer to set volume for.
 * @param   pVol        Pointer to volume structure to set.
 */
void AudioMixBufSetVolume(PAUDIOMIXBUF pMixBuf, PCPDMAUDIOVOLUME pVol)
{
    AssertPtrReturnVoid(pMixBuf);
    AssertPtrReturnVoid(pVol);

    LogFlowFunc(("%s: fMuted=%RTbool auChannels=%.*Rhxs\n",
                 pMixBuf->pszName, pVol->fMuted, sizeof(pVol->auChannels), pVol->auChannels));

    /*
     * Convert PDM audio volume to the internal format.
     */
    if (!pVol->fMuted)
    {
        pMixBuf->Volume.fMuted  = false;

        AssertCompileSize(pVol->auChannels[0], sizeof(uint8_t));
        for (uintptr_t i = 0; i < pMixBuf->cChannels; i++)
            pMixBuf->Volume.auChannels[i] = s_aVolumeConv[pVol->auChannels[i]] * (AUDIOMIXBUF_VOL_0DB >> 16);

        pMixBuf->Volume.fAllMax = true;
        for (uintptr_t i = 0; i < pMixBuf->cChannels; i++)
            if (pMixBuf->Volume.auChannels[i] != AUDIOMIXBUF_VOL_0DB)
            {
                pMixBuf->Volume.fAllMax = false;
                break;
            }
    }
    else
    {
        pMixBuf->Volume.fMuted  = true;
        pMixBuf->Volume.fAllMax = false;
        for (uintptr_t i = 0; i < RT_ELEMENTS(pMixBuf->Volume.auChannels); i++)
            pMixBuf->Volume.auChannels[i] = 0;
    }
}

