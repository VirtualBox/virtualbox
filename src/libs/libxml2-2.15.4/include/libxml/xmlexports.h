/**
 * @file
 * 
 * @brief macros for marking symbols as exportable/importable.
 * 
 * macros for marking symbols as exportable/importable.
 *
 * @copyright See Copyright for the status of this software.
 */

#ifndef __XML_EXPORTS_H__
#define __XML_EXPORTS_H__

/* VirtualBox: start - prevent warnings about undefined __GNUC__ */
#ifndef __GNUC__
# ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable: 4668)
# endif
#endif
/* VirtualBox: end */

/*
 * Symbol visibility
 */

#if (defined(_WIN32) || defined(__CYGWIN__)) && !defined(LIBXML_STATIC)
  #if defined(IN_LIBXML)
    #define XMLPUBFUN __declspec(dllexport)
    #define XMLPUBVAR __declspec(dllexport) extern
  #else
    #define XMLPUBFUN __declspec(dllimport)
    #define XMLPUBVAR __declspec(dllimport) extern
  #endif
#else /* not Windows */
  #define XMLPUBFUN
  #define XMLPUBVAR extern
#endif /* platform switch */

/* bird/VirtualBox: Visibility attributes - start  */
/* frank: changed to "hidden" */
#if defined(VBOX_HAVE_VISIBILITY_HIDDEN) && !defined(LIBXML_STATIC) && defined(IN_LIBXML)
  #undef XMLPUBFUN
  #undef XMLPUBVAR
  #define XMLPUBFUN __attribute__((visibility("hidden")))
  #define XMLPUBVAR __attribute__((visibility("hidden"))) extern
#endif
/* bird/VirtualBox: Visibility attributes - end  */


/* Compatibility */
#define XMLCALL
#define XMLCDECL
#ifndef LIBXML_DLL_IMPORT
  #define LIBXML_DLL_IMPORT XMLPUBVAR
#endif

/*
 * Attributes
 */

#if !defined(__clang__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 403)
  #define LIBXML_ATTR_ALLOC_SIZE(x) __attribute__((alloc_size(x)))
#else
  #define LIBXML_ATTR_ALLOC_SIZE(x)
#endif

#if __GNUC__ * 100 + __GNUC_MINOR__ >= 303
  #define LIBXML_ATTR_FORMAT(fmt,args) \
    __attribute__((__format__(__printf__,fmt,args)))
#else
  #define LIBXML_ATTR_FORMAT(fmt,args)
#endif

#ifndef XML_DEPRECATED
  #if defined(IN_LIBXML)
    #define XML_DEPRECATED
  #elif __GNUC__ * 100 + __GNUC_MINOR__ >= 405
    /* GCC 4.5+ supports deprecated with message */
    #define XML_DEPRECATED __attribute__((deprecated("See https://gnome.pages.gitlab.gnome.org/libxml2/html/deprecated.html")))
  #elif __GNUC__ * 100 + __GNUC_MINOR__ >= 301
    /* GCC 3.1+ supports deprecated without message */
    #define XML_DEPRECATED __attribute__((deprecated))
  #elif defined(_MSC_VER) && _MSC_VER >= 1400
    /* Available since Visual Studio 2005 */
    #define XML_DEPRECATED __declspec(deprecated("See https://gnome.pages.gitlab.gnome.org/libxml2/html/deprecated.html"))
  #else
    #define XML_DEPRECATED
  #endif
#endif

#ifndef XML_DEPRECATED_MEMBER
  #if defined(IN_LIBXML)
    #define XML_DEPRECATED_MEMBER
  #elif __GNUC__ * 100 + __GNUC_MINOR__ >= 301
    #define XML_DEPRECATED_MEMBER __attribute__((deprecated))
  #else
    #define XML_DEPRECATED_MEMBER
  #endif
#endif

/*
 * Originally declared in xmlversion.h which is generated
 */

#ifdef __cplusplus
extern "C" {
#endif

XMLPUBFUN void xmlCheckVersion(int version);

#ifdef __cplusplus
}
#endif

/* VirtualBox: start - restore warning state */
#ifndef __GNUC__
# ifdef _MSC_VER
#  pragma warning(pop)
# endif
#endif
/* VirtualBox: end */

#endif /* __XML_EXPORTS_H__ */


