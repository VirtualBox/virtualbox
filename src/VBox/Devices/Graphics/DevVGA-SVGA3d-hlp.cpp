/* $Id: DevVGA-SVGA3d-hlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVMWare - VMWare SVGA device helpers
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_DEV_VMSVGA
#include <VBox/AssertGuest.h>

#ifdef SHADER_VERIFY_STANDALONE
# include <stdio.h>
//# define Log3(a) printf a        - /** @todo r=bird: This is strictly forbidden. Noone redefines Log macros ever! */
//# define LogRel(a) printf a      - /** @todo r=bird: This is strictly forbidden. Noone redefines Log macros ever! */
#else
# include <VBox/log.h>
#endif

#include <iprt/cdefs.h>
#include <iprt/errcore.h>
#include <iprt/types.h>
#include <iprt/string.h>

#include "DevVGA-SVGA.h"

/** Per shader data is stored in this structure. */
typedef struct VMSVGA3DSHADERPARSECONTEXT
{
    /** Version token. */
    SVGA3dShaderVersion version;

    SVGA3dShaderOpCodeType currentOpcode;
    union
    {
        SVGA3DOpDclArgs *pDclArgs;
    } u;
} VMSVGA3DSHADERPARSECONTEXT;

/** Callback which parses a parameter token.
 *
 * @param pCtx     The shader data.
 * @param Op       Instruction opcode which the token is used with.
 * @param Token    The parameter token which must be parsed.
 * @param idxToken Index of the parameter token in the instruction. 0 for the first parameter.
 *
 * @return VBox error code.
 */
typedef int FNSHADERPARSETOKEN(VMSVGA3DSHADERPARSECONTEXT* pCtx, uint32_t Op, uint32_t Token, uint32_t idxToken);
typedef FNSHADERPARSETOKEN* PFNSHADERPARSETOKEN;

/** Information about a shader opcode. */
typedef struct VMSVGA3DSHADERPARSEOP
{
    /** Opcode. */
    SVGA3dShaderOpCodeType Op;
    /** Maximum number of parameters. */
    uint32_t Length;
    /** Pointer to callback, which parse each parameter.
     * The size is the number of maximum possible parameters: dest + 3 * src
     */
    PFNSHADERPARSETOKEN apfnParse[4];
} VMSVGA3DSHADERPARSEOP;

static int vmsvga3dShaderParseRegOffset(VMSVGA3DSHADERPARSECONTEXT *pCtx,
                                        bool fIsSrc,
                                        SVGA3dShaderRegType regType,
                                        uint32_t off)
{
    RT_NOREF(pCtx, fIsSrc);

    switch (regType)
    {
        case SVGA3DREG_TEMP:
            break;
        case SVGA3DREG_INPUT:
            break;
        case SVGA3DREG_CONST:
            break;
        case SVGA3DREG_ADDR /* also SVGA3DREG_TEXTURE */:
            break;
        case SVGA3DREG_RASTOUT:
            break;
        case SVGA3DREG_ATTROUT:
            break;
        case SVGA3DREG_TEXCRDOUT /* also SVGA3DREG_OUTPUT */:
            break;
        case SVGA3DREG_CONSTINT:
            break;
        case SVGA3DREG_COLOROUT:
            break;
        case SVGA3DREG_DEPTHOUT:
            break;
        case SVGA3DREG_SAMPLER:
            break;
        case SVGA3DREG_CONST2:
            break;
        case SVGA3DREG_CONST3:
            break;
        case SVGA3DREG_CONST4:
            break;
        case SVGA3DREG_CONSTBOOL:
            break;
        case SVGA3DREG_LOOP:
            break;
        case SVGA3DREG_TEMPFLOAT16:
            break;
        case SVGA3DREG_MISCTYPE:
            ASSERT_GUEST_RETURN(   off == SVGA3DMISCREG_POSITION
                                || off == SVGA3DMISCREG_FACE, VERR_PARSE_ERROR);
            break;
        case SVGA3DREG_LABEL:
            break;
        case SVGA3DREG_PREDICATE:
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN(VERR_PARSE_ERROR);
    }

    return VINF_SUCCESS;
}

/* Parse a declaration parameter token:
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/display/dcl-instruction
 *
 * See FNSHADERPARSETOKEN.
 */
static int vmsvga3dShaderParseDclToken(VMSVGA3DSHADERPARSECONTEXT* pCtx, uint32_t Op, uint32_t Token, uint32_t idxToken)
{
    RT_NOREF(pCtx, Op, Token, idxToken);
    return VINF_SUCCESS;
}

/* Parse a label (D3DSPR_LABEL) parameter token.
 *
 * See FNSHADERPARSETOKEN.
 */
static int vmsvga3dShaderParseLabelToken(VMSVGA3DSHADERPARSECONTEXT* pCtx, uint32_t Op, uint32_t Token, uint32_t idxToken)
{
    RT_NOREF(pCtx, Op, Token, idxToken);
    return VINF_SUCCESS;
}

/* Parse a destination parameter token:
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/display/destination-parameter-token
 * See FNSHADERPARSETOKEN.
 */
static int vmsvga3dShaderParseDestToken(VMSVGA3DSHADERPARSECONTEXT* pCtx, uint32_t Op, uint32_t Token, uint32_t idxToken)
{
    RT_NOREF(pCtx, Op, idxToken);

    SVGA3dShaderDestToken dest;
    dest.value = Token;

    SVGA3dShaderRegType const regType = (SVGA3dShaderRegType)(dest.type_upper << 3 | dest.type_lower);
    Log3(("Dest: type %d, r0 %d, shfScale %d, dstMod %d, mask 0x%x, r1 %d, relAddr %d, num %d\n",
        regType, dest.reserved0, dest.shfScale, dest.dstMod, dest.mask, dest.reserved1, dest.relAddr, dest.num));

    if (pCtx->currentOpcode == SVGA3DOP_DCL && regType == SVGA3DREG_SAMPLER)
    {
        if (pCtx->u.pDclArgs->type == SVGA3DSAMP_UNKNOWN)
        {
            Log3(("Replacing SVGA3DSAMP_UNKNOWN with SVGA3DSAMP_2D\n"));
            pCtx->u.pDclArgs->type = SVGA3DSAMP_2D;
        }
    }

    return vmsvga3dShaderParseRegOffset(pCtx, false, regType, dest.num);
}

/* Parse a source parameter token:
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/display/source-parameter-token
 * See FNSHADERPARSETOKEN.
 */
static int vmsvga3dShaderParseSrcToken(VMSVGA3DSHADERPARSECONTEXT* pCtx, uint32_t Op, uint32_t Token, uint32_t idxToken)
{
    RT_NOREF(pCtx, Op, idxToken);

    SVGA3dShaderSrcToken src;
    src.value = Token;

    SVGA3dShaderRegType const regType = (SVGA3dShaderRegType)(src.type_upper << 3 | src.type_lower);
    Log3(("Src: type %d, r0 %d, srcMod %d, swizzle 0x%x, r1 %d, relAddr %d, num %d\n",
        regType, src.reserved0, src.srcMod, src.swizzle, src.reserved1, src.relAddr, src.num));

    return vmsvga3dShaderParseRegOffset(pCtx, true, regType, src.num);
}

/* Shortcut defines. */
#define PT_DCL  vmsvga3dShaderParseDclToken
#define PT_LBL  vmsvga3dShaderParseLabelToken
#define PT_DEST vmsvga3dShaderParseDestToken
#define PT_SRC  vmsvga3dShaderParseSrcToken

/* Information about opcodes:
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d9types/ne-d3d9types-_d3dshader_instruction_opcode_type
 */
static const VMSVGA3DSHADERPARSEOP aOps[] =
{
    /*         Op                Length    Parameters */
    /* 00 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 01 */ { SVGA3DOP_MOV,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 02 */ { SVGA3DOP_ADD,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 03 */ { SVGA3DOP_SUB,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 04 */ { SVGA3DOP_MAD,          4, { PT_DEST, PT_SRC,  PT_SRC,  PT_SRC  } },
    /* 05 */ { SVGA3DOP_MUL,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 06 */ { SVGA3DOP_RCP,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 07 */ { SVGA3DOP_RSQ,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 08 */ { SVGA3DOP_DP3,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 09 */ { SVGA3DOP_DP4,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 10 */ { SVGA3DOP_MIN,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 11 */ { SVGA3DOP_MAX,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 12 */ { SVGA3DOP_SLT,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 13 */ { SVGA3DOP_SGE,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 14 */ { SVGA3DOP_EXP,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 15 */ { SVGA3DOP_LOG,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 16 */ { SVGA3DOP_LIT,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 17 */ { SVGA3DOP_DST,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 18 */ { SVGA3DOP_LRP,          4, { PT_DEST, PT_SRC,  PT_SRC,  PT_SRC  } },
    /* 19 */ { SVGA3DOP_FRC,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 20 */ { SVGA3DOP_M4x4,         3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 21 */ { SVGA3DOP_M4x3,         3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 22 */ { SVGA3DOP_M3x4,         3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 23 */ { SVGA3DOP_M3x3,         3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 24 */ { SVGA3DOP_M3x2,         3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 25 */ { SVGA3DOP_CALL,         1, { PT_LBL,  NULL,    NULL,    NULL    } },
    /* 26 */ { SVGA3DOP_CALLNZ,       2, { PT_LBL,  PT_SRC,  NULL,    NULL    } },
    /* 27 */ { SVGA3DOP_LOOP,         1, { PT_SRC,  NULL,    NULL,    NULL    } },
    /* 28 */ { SVGA3DOP_RET,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 29 */ { SVGA3DOP_ENDLOOP,      0, { NULL,    NULL,    NULL,    NULL    } },
    /* 30 */ { SVGA3DOP_LABEL,        1, { PT_LBL,  NULL,    NULL,    NULL    } },
    /* 31 */ { SVGA3DOP_DCL,          2, { PT_DCL,  PT_DEST, NULL,    NULL    } },
    /* 32 */ { SVGA3DOP_POW,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 33 */ { SVGA3DOP_CRS,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 34 */ { SVGA3DOP_SGN,          4, { PT_DEST, PT_SRC,  PT_SRC,  PT_SRC  } },
    /* 35 */ { SVGA3DOP_ABS,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 36 */ { SVGA3DOP_NRM,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 37 */ { SVGA3DOP_SINCOS,       4, { PT_DEST, PT_SRC,  PT_SRC,  PT_SRC  } },
    /* 38 */ { SVGA3DOP_REP,          1, { PT_SRC,  NULL,    NULL,    NULL    } },
    /* 39 */ { SVGA3DOP_ENDREP,       0, { NULL,    NULL,    NULL,    NULL    } },
    /* 40 */ { SVGA3DOP_IF,           1, { PT_SRC,  NULL,    NULL,    NULL    } },
    /* 41 */ { SVGA3DOP_IFC,          2, { PT_SRC,  PT_SRC,  NULL,    NULL    } },
    /* 42 */ { SVGA3DOP_ELSE,         0, { NULL,    NULL,    NULL,    NULL    } },
    /* 43 */ { SVGA3DOP_ENDIF,        0, { NULL,    NULL,    NULL,    NULL    } },
    /* 44 */ { SVGA3DOP_BREAK,        0, { NULL,    NULL,    NULL,    NULL    } },
    /* 45 */ { SVGA3DOP_BREAKC,       2, { PT_SRC,  PT_SRC,  NULL,    NULL    } },
    /* 46 */ { SVGA3DOP_MOVA,         2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 47 */ { SVGA3DOP_DEFB,         2, { PT_DEST, NULL,    NULL,    NULL    } },
    /* 48 */ { SVGA3DOP_DEFI,         5, { PT_DEST, NULL,    NULL,    NULL    } },
    /* 49 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 50 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 51 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 52 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 53 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 54 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 55 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 56 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 57 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 58 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 59 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 60 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 61 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 62 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 63 */ { SVGA3DOP_NOP,          0, { NULL,    NULL,    NULL,    NULL    } },
    /* 64 */ { SVGA3DOP_TEXCOORD,     2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 65 */ { SVGA3DOP_TEXKILL,      1, { PT_DEST, NULL,    NULL,    NULL    } },
    /* 66 */ { SVGA3DOP_TEX,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } }, // pre-1.4 = tex dest, post-1.4 = texld dest, src, src
    /* 67 */ { SVGA3DOP_TEXBEM,       2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 68 */ { SVGA3DOP_TEXBEML,      2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 69 */ { SVGA3DOP_TEXREG2AR,    2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 70 */ { SVGA3DOP_TEXREG2GB,    2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 71 */ { SVGA3DOP_TEXM3x2PAD,   2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 72 */ { SVGA3DOP_TEXM3x2TEX,   2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 73 */ { SVGA3DOP_TEXM3x3PAD,   2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 74 */ { SVGA3DOP_TEXM3x3TEX,   2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 75 */ { SVGA3DOP_RESERVED0,    0, { NULL,    NULL,    NULL,    NULL    } },
    /* 76 */ { SVGA3DOP_TEXM3x3SPEC,  3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 77 */ { SVGA3DOP_TEXM3x3VSPEC, 2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 78 */ { SVGA3DOP_EXPP,         2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 79 */ { SVGA3DOP_LOGP,         2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 80 */ { SVGA3DOP_CND,          4, { PT_DEST, PT_SRC,  PT_SRC,  PT_SRC  } },
    /* 81 */ { SVGA3DOP_DEF,          5, { PT_DEST, NULL,    NULL,    NULL    } },
    /* 82 */ { SVGA3DOP_TEXREG2RGB,   2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 83 */ { SVGA3DOP_TEXDP3TEX,    2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 84 */ { SVGA3DOP_TEXM3x2DEPTH, 2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 85 */ { SVGA3DOP_TEXDP3,       2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 86 */ { SVGA3DOP_TEXM3x3,      2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 87 */ { SVGA3DOP_TEXDEPTH,     1, { PT_DEST, NULL,    NULL,    NULL    } },
    /* 88 */ { SVGA3DOP_CMP,          4, { PT_DEST, PT_SRC,  PT_SRC,  PT_SRC  } },
    /* 89 */ { SVGA3DOP_BEM,          3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 90 */ { SVGA3DOP_DP2ADD,       4, { PT_DEST, PT_SRC,  PT_SRC,  PT_SRC  } },
    /* 91 */ { SVGA3DOP_DSX,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 92 */ { SVGA3DOP_DSY,          2, { PT_DEST, PT_SRC,  NULL,    NULL    } },
    /* 93 */ { SVGA3DOP_TEXLDD,       3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 94 */ { SVGA3DOP_SETP,         3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 95 */ { SVGA3DOP_TEXLDL,       3, { PT_DEST, PT_SRC,  PT_SRC,  NULL    } },
    /* 96 */ { SVGA3DOP_BREAKP,       1, { PT_SRC,  NULL,    NULL,    NULL    } },
};

#undef PT_DCL
#undef PT_LBL
#undef PT_DEST
#undef PT_SRC

/* Parse the shader code
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/display/shader-code-format
 */
int vmsvga3dShaderParse(SVGA3dShaderType type, uint32_t cbShaderData, uint32_t* pShaderData)
{
    uint32_t *paTokensStart = (uint32_t*)pShaderData;
    uint32_t const cTokens = cbShaderData / sizeof(uint32_t);

    ASSERT_GUEST_RETURN(cTokens * sizeof(uint32_t) == cbShaderData, VERR_INVALID_PARAMETER);

    /* Need at least the version token and SVGA3DOP_END instruction token. 48KB is an arbitrary limit. */
    ASSERT_GUEST_RETURN(cTokens >= 2 && cTokens < (48 * _1K) / sizeof(paTokensStart[0]), VERR_INVALID_PARAMETER);

#ifdef LOG_ENABLED
    Log3(("Shader code:\n"));
    const uint32_t cTokensPerLine = 8;
    for (uint32_t iToken = 0; iToken < cTokens; ++iToken)
    {
        if ((iToken % cTokensPerLine) == 0)
        {
            if (iToken == 0)
                Log3(("0x%08X,", paTokensStart[iToken]));
            else
                Log3(("\n0x%08X,", paTokensStart[iToken]));
        }
        else
            Log3((" 0x%08X,", paTokensStart[iToken]));
    }
    Log3(("\n"));
#endif

    VMSVGA3DSHADERPARSECONTEXT ctx;
    RT_ZERO(ctx);

    /* "The first token must be a version token." */
    ctx.version = *(SVGA3dShaderVersion*)paTokensStart;
    ASSERT_GUEST_RETURN(ctx.version.type == SVGA3D_VS_TYPE
        || ctx.version.type == SVGA3D_PS_TYPE, VERR_PARSE_ERROR);
    /* A vertex shader should not be defined with a pixel shader bytecode (and visa versa)*/
    ASSERT_GUEST_RETURN((ctx.version.type == SVGA3D_VS_TYPE && type == SVGA3D_SHADERTYPE_VS)
                     || (ctx.version.type == SVGA3D_PS_TYPE && type == SVGA3D_SHADERTYPE_PS), VERR_PARSE_ERROR);
    ASSERT_GUEST_RETURN(ctx.version.major >= 2 && ctx.version.major <= 4, VERR_PARSE_ERROR);

    /* Scan the tokens. Immediately return an error code on any unexpected data. */
    uint32_t *paTokensEnd = &paTokensStart[cTokens];
    uint32_t *pToken = &paTokensStart[1]; /* Skip the version token. */
    bool  bEndTokenFound = false;
    while (pToken < paTokensEnd)
    {
        SVGA3dShaderInstToken const token = *(SVGA3dShaderInstToken*)pToken;

        /* Figure out the instruction length, which is how many tokens follow the instruction token. */
        uint32_t const cInstLen = token.op == SVGA3DOP_COMMENT
            ? token.comment_size
            : token.size;

        Log3(("op %d, cInstLen %d\n", token.op, cInstLen));

        /* Must not be greater than the number of remaining tokens. */
        ASSERT_GUEST_RETURN(cInstLen < (uintptr_t)(paTokensEnd - pToken), VERR_PARSE_ERROR);

        /* Stop parsing if this is the SVGA3DOP_END instruction. */
        if (token.op == SVGA3DOP_END)
        {
            ASSERT_GUEST_RETURN(token.value == 0x0000FFFF, VERR_PARSE_ERROR);
            bEndTokenFound = true;
            break;
        }

        ctx.currentOpcode = (SVGA3dShaderOpCodeType)token.op;

        /* If this instrution is in the aOps table. */
        if (token.op <= SVGA3DOP_BREAKP)
        {
            VMSVGA3DSHADERPARSEOP const* pOp = &aOps[token.op];

            if (ctx.currentOpcode == SVGA3DOP_DCL)
                ctx.u.pDclArgs = (SVGA3DOpDclArgs *)&pToken[1];

            /* cInstLen can be greater than pOp->Length.
             * W10 guest sends a vertex shader MUL instruction with length 4.
             * So figure out the actual number of valid parameters.
             */
            uint32_t const cParams = RT_MIN(cInstLen, pOp->Length);

            /* Parse paramater tokens. */
            uint32_t i;
            for (i = 0; i < RT_MIN(cParams, RT_ELEMENTS(pOp->apfnParse)); ++i)
            {
                if (!pOp->apfnParse[i])
                    continue;

                int rc = pOp->apfnParse[i](&ctx, token.op, pToken[i + 1], i);
                if (RT_FAILURE(rc))
                    return rc;
            }
        }
        else if (token.op == SVGA3DOP_PHASE
            || token.op == SVGA3DOP_COMMENT)
        {
        }
        else
            ASSERT_GUEST_FAILED_RETURN(VERR_PARSE_ERROR);

        /* Next token. */
        pToken += cInstLen + 1;
    }

    if (!bEndTokenFound)
    {
        ASSERT_GUEST_FAILED_RETURN(VERR_PARSE_ERROR);
    }

    return VINF_SUCCESS;
}

void vmsvga3dShaderLogRel(char const *pszMsg, SVGA3dShaderType type, uint32_t cbShaderData, uint32_t const *pShaderData)
{
    /* Dump the shader code. */
    static int scLogged = 0;
    if (scLogged < 8)
    {
        ++scLogged;

        LogRel(("VMSVGA: %s shader: %s:\n", (type == SVGA3D_SHADERTYPE_VS) ? "VERTEX" : "PIXEL", pszMsg));
        const uint32_t cTokensPerLine = 8;
        const uint32_t *paTokens = (uint32_t *)pShaderData;
        const uint32_t cTokens = cbShaderData / sizeof(uint32_t);
        for (uint32_t iToken = 0; iToken < cTokens; ++iToken)
        {
            if ((iToken % cTokensPerLine) == 0)
            {
                if (iToken == 0)
                    LogRel(("0x%08X,", paTokens[iToken]));
                else
                    LogRel(("\n0x%08X,", paTokens[iToken]));
            }
            else
                LogRel((" 0x%08X,", paTokens[iToken]));
        }
        LogRel(("\n"));
    }
}
