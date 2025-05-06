///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_cpucores.h
/// \brief      Get the number of CPU cores online
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_CPUCORES_H
#define TUKLIB_CPUCORES_H

#include "sysdefs.h"
RT_C_DECLS_BEGIN

DECL_FORCE_INLINE(uint32_t) tuklib_cpucores(void)
{
	return RTMpGetOnlineCount();
}

RT_C_DECLS_END
#endif
