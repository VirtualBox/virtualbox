#ifndef __WINE_WINE_UNIXLIB_H
#define __WINE_WINE_UNIXLIB_H

typedef int NTSTATUS;
#define NTSTATUS_SUCCESS 0

typedef NTSTATUS (*THUNKCALLBACK)(void *obj);

extern const THUNKCALLBACK __wine_unix_call_funcs[];

#define WINE_UNIX_CALL(code, args) __wine_unix_call_funcs[code]((args))

#endif /* __WINE_WINE_UNIXLIB_H */
