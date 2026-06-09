/* config.h.in.  Generated from configure.in by autoheader.  */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_LINUX)
# undef HAVE_LIBM
#else
# define HAVE_LIBM 1
#endif

/* Define if IPV6 support is there */
#undef SUPPORT_IP6

/* Define if getaddrinfo is there */
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD)
# define HAVE_GETADDRINFO 1
#else
# undef HAVE_GETADDRINFO
#endif 

/* Define to 1 if you have the <ansidecl.h> header file. */
#undef HAVE_ANSIDECL_H

/* Define to 1 if you have the <arpa/inet.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_ARPA_INET_H
#else
# define HAVE_ARPA_INET_H 1
#endif 

/* Define to 1 if you have the <arpa/nameser.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_ARPA_NAMESER_H
#else
# define HAVE_ARPA_NAMESER_H 1
#endif 

/* Whether struct sockaddr::__ss_family exists */
#undef HAVE_BROKEN_SS_FAMILY

/* Define to 1 if you have the `class' function. */
#undef HAVE_CLASS

/* Define to 1 if you have the <ctype.h> header file. */
#define HAVE_CTYPE_H 1

/* Define to 1 if you have the <dirent.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_DIRENT_H
#else
# define HAVE_DIRENT_H 1
#endif

/* Define to 1 if you have the <dlfcn.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_DLFCN_H
#else
# define HAVE_DLFCN_H 1
#endif 

/* Have dlopen based dso */
#if defined(RT_OS_WINDOWS)
# undef HAVE_DLOPEN
#else
# define HAVE_DLOPEN 1
#endif

/* Define to 1 if you have the <dl.h> header file. */
#undef HAVE_DL_H

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `finite' function. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_FINITE
#else
# define HAVE_FINITE 1
#endif 

/* Define to 1 if you have the <float.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_FLOAT_H
#else
# define HAVE_FLOAT_H 1
#endif 

/* Define to 1 if you have the `fpclass' function. */
#if 0
# define HAVE_FPCLASS 1
#else
# undef HAVE_FPCLASS
#endif

/* Define to 1 if you have the `fprintf' function. */
#define HAVE_FPRINTF 1

/* Define to 1 if you have the `fp_class' function. */
#undef HAVE_FP_CLASS

/* Define to 1 if you have the <fp_class.h> header file. */
#undef HAVE_FP_CLASS_H

/* Define to 1 if you have the `ftime' function. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_FTIME
#else
# define HAVE_FTIME 1
#endif

/* Define to 1 if you have the `gettimeofday' function. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_GETTIMEOFDAY
#else
# define HAVE_GETTIMEOFDAY 1
#endif

/* Define to 1 if you have the <ieeefp.h> header file. */
#undef HAVE_IEEEFP_H

/* Define to 1 if you have the <inttypes.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_INTTYPES_H
#else
# define HAVE_INTTYPES_H 1
#endif

/* Define if isinf is there */
#define HAVE_ISINF 1

/* Define if isnan is there */
#define HAVE_ISNAN 1

/* Define to 1 if you have the `isnand' function. */
#if 0
# define HAVE_ISNAND 1
#else
# undef HAVE_ISNAND
#endif 

/* Define if history library is there (-lhistory) */
#undef HAVE_LIBHISTORY

/* Define if pthread library is there (-lpthread) */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# undef HAVE_LIBPTHREAD
#else
# define HAVE_LIBPTHREAD 1
#endif

/* Define if readline library is there (-lreadline) */
#undef HAVE_LIBREADLINE

/* Have compression library */
#define HAVE_LIBZ 1

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the `localtime' function. */
#define HAVE_LOCALTIME 1

/* Define to 1 if you have the <malloc.h> header file. */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define HAVE_MALLOC_H 1
#else
# undef HAVE_MALLOC_H
#endif

/* Define to 1 if you have the <math.h> header file. */
#define HAVE_MATH_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <nan.h> header file. */
#undef HAVE_NAN_H

/* Define to 1 if you have the <ndir.h> header file, and it defines `DIR'. */
#undef HAVE_NDIR_H

/* Define to 1 if you have the <netdb.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_NETDB_H
#else
# define HAVE_NETDB_H 1
#endif 

/* Define to 1 if you have the <netinet/in.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_NETINET_IN_H
#else
# define HAVE_NETINET_IN_H 1
#endif

/* Define to 1 if you have the `printf' function. */
#define HAVE_PRINTF 1

/* Define if <pthread.h> is there */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# undef HAVE_PTHREAD_H
#else
# define HAVE_PTHREAD_H 1
#endif

/* Define to 1 if you have the <resolv.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_RESOLV_H
#else
# define HAVE_RESOLV_H 1
#endif 

/* Have shl_load based dso */
#undef HAVE_SHLLOAD

/* Define to 1 if you have the `signal' function. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_SIGNAL
#else
# define HAVE_SIGNAL 1
#endif

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* Define to 1 if you have the `snprintf' function. */
#if defined(RT_OS_WINDOWS) /* others? */
# undef HAVE_SNPRINTF
#else
# define HAVE_SNPRINTF 1
#endif 


/* Define to 1 if you have the `sprintf' function. */
#define HAVE_SPRINTF 1

/* Define to 1 if you have the `sscanf' function. */
#define HAVE_SSCANF 1

/* Define to 1 if you have the `stat' function. */
#define HAVE_STAT 1

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_STDINT_H
#else
# define HAVE_STDINT_H 1
#endif 

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the `strerror' function. */
#define HAVE_STRERROR 1

/* Define to 1 if you have the `strftime' function. */
#define HAVE_STRFTIME 1

/* Define to 1 if you have the <strings.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_STRINGS_H
#else
# define HAVE_STRINGS_H 1
#endif 

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strndup' function. */
#if defined(RT_OS_LINUX) || defined(RT_OS_OS2) /*??*/
# define HAVE_STRNDUP 1
#else
# undef HAVE_STRNDUP
#endif 

/* Define to 1 if you have the <sys/dir.h> header file, and it defines `DIR'.
   */
#if defined(RT_OS_WINDOWS) /* others? */
# undef HAVE_SYS_DIR_H
#else
# define HAVE_SYS_DIR_H 1
#endif 

/* Define to 1 if you have the <sys/mman.h> header file. */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# undef HAVE_SYS_MMAN_H
#else
# define HAVE_SYS_MMAN_H 1
#endif 

/* Define to 1 if you have the <sys/ndir.h> header file, and it defines `DIR'.
   */
#undef HAVE_SYS_NDIR_H

/* Define to 1 if you have the <sys/select.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_SYS_SELECT_H
#else
# define HAVE_SYS_SELECT_H 1
#endif 

/* Define to 1 if you have the <sys/socket.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_SYS_SOCKET_H
#else
# define HAVE_SYS_SOCKET_H 1
#endif 

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/timeb.h> header file. */
#define HAVE_SYS_TIMEB_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_SYS_TIME_H
#else
# define HAVE_SYS_TIME_H 1
#endif

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_UNISTD_H
#else
# define HAVE_UNISTD_H 1
#endif 

/* Whether va_copy() is available */
#ifdef RT_ARCH_AMD64
# define HAVE_VA_COPY 1
#else 
# undef HAVE_VA_COPY
#endif 

/* Define to 1 if you have the `vfprintf' function. */
#define HAVE_VFPRINTF 1

/* Define to 1 if you have the `vsnprintf' function. */
#if defined(RT_OS_WINDOWS)
# undef HAVE_VSNPRINTF
#else
# define HAVE_VSNPRINTF 1
#endif 

/* Define to 1 if you have the `vsprintf' function. */
#define HAVE_VSPRINTF 1

/* Define to 1 if you have the <zlib.h> header file. */
#define HAVE_ZLIB_H 1

/* Define to 1 if you have the `_stat' function. */
#if defined(RT_OS_WINDOWS)
# define HAVE__STAT 1
#else
# undef HAVE__STAT
#endif 

/* Whether __va_copy() is available */
#undef HAVE___VA_COPY

/* Name of package */
#define PACKAGE "libxml2"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "2.6.30"

/* Define to 1 if the C compiler supports function prototypes. */
#define PROTOTYPES 1

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Support for IPv6 */
#undef SUPPORT_IP6

/* Version number of package */
#define VERSION "2.6.30"

/* Determine what socket length (socklen_t) data type is */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define XML_SOCKLEN_T int
#else
# define XML_SOCKLEN_T socklen_t
#endif

/* Define like PROTOTYPES; this can be used by system headers. */
#define __PROTOTYPES 1

/* Win32 Std C name mangling work-around */
#undef snprintf

/* ss_family is not defined here, use __ss_family instead */
#undef ss_family

/* Win32 Std C name mangling work-around */
#undef vsnprintf

/* Using the Win32 Socket implementation */
#ifdef RT_OS_WINDOWS
# include <io.h>
# include <direct.h>
# ifdef NEED_SOCKETS
#  include <wsockcompat.h>
# endif
#else  /* !RT_OS_WINDOWS */
# undef _WINSOCKAPI_
#endif /* !RT_OS_WINDOWS */

/* make sure LIBXML_ICONV_ENABLED is killed */
#include <libxml/xmlversion.h>
#undef LIBXML_ICONV_ENABLED

#ifdef RT_OS_WINDOWS
#ifndef XML_HIDDEN
  #define XML_HIDDEN
#endif

#ifndef ATTRIBUTE_NO_SANITIZE_INTEGER
  #define ATTRIBUTE_NO_SANITIZE_INTEGER
#endif

#ifndef ATTRIBUTE_NO_SANITIZE
  #define ATTRIBUTE_NO_SANITIZE(arg)
#endif

#undef NAN
#endif
