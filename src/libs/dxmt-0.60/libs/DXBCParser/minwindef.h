// so clunky
#pragma once

#ifndef WINDEF

#ifndef CONST
#define CONST const
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef _DEF_WINBOOL_
#define _DEF_WINBOOL_
typedef int WINBOOL;
#endif

#ifndef BOOL
#define BOOL WINBOOL
#endif

#ifndef __LP64__ /* 32 bit target, 64 bit Mingw target */
#define __LONG32 long
#else /* 64 bit Cygwin target */
#define __LONG32 int
#endif

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned __LONG32 DWORD;
typedef char CHAR;
typedef short SHORT;
typedef __LONG32 LONG;
typedef float FLOAT, *PFLOAT;
typedef BYTE *PBYTE;
typedef BYTE *LPBYTE;
typedef int *PINT;
typedef int *LPINT;
typedef WORD *PWORD;
typedef WORD *LPWORD;
//   typedef __LONG32 *LPLONG;
//   typedef DWORD *PDWORD;
//   typedef DWORD *LPDWORD;
typedef void *LPVOID;
// #ifndef _LPCVOID_DEFINED
// #define _LPCVOID_DEFINED
//   typedef CONST void *LPCVOID;
// #endif
typedef int INT;
typedef unsigned int UINT, *PUINT;

#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef LONG HRESULT;
#endif

typedef CHAR *PCHAR, *LPCH, *PCH;
typedef CONST CHAR *LPCCH, *PCCH;
typedef CHAR *NPSTR, *LPSTR, *PSTR;
typedef PSTR *PZPSTR;
typedef CONST PSTR *PCZPSTR;
typedef CONST CHAR *LPCSTR, *PCSTR;
typedef PCSTR *PZPCSTR;
typedef CHAR *PZZSTR;
typedef CONST CHAR *PCZZSTR;
typedef CHAR *PNZCH;
typedef CONST CHAR *PCNZCH;

#endif

#ifndef _BASETSD_H_

#ifdef __aarch64__ /* plain char is unsigned by default on arm. */
typedef signed char INT8, *PINT8;
#else
typedef char INT8, *PINT8;
#endif
typedef short INT16, *PINT16;
typedef int INT32, *PINT32;
typedef long long INT64;
typedef unsigned char UINT8, *PUINT8;
typedef unsigned short UINT16, *PUINT16;
typedef unsigned int UINT32, *PUINT32;
typedef unsigned long long UINT64;

#endif