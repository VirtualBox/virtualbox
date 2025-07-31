# MinGW DirectX Headers

These headers are taken directly from MinGW-w64 found at:</br>
https://www.mingw-w64.org/</br>
https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/mingw-w64-headers/

## License

The license for these headers is LGPL v2.1.</br>
However that does not mean that your project is bound by LGPL v2.1 for using these headers:</br>
```
DirectX and DDK headers are under GNU LGPLv2.1+ (see the file
COPYING.LGPLv2.1) and copyrighted by various people. Using these
headers doesn't make LGPLv2.1 apply to your code, because these
headers files contain only data structure definitions, short
macros, and short inline functions. Here is the relevant part
from LGPLv2.1 section 5 paragraph 4:

    If such an object file uses only numerical parameters, data
    structure layouts and accessors, and small macros and small
    inline functions (ten lines or less in length), then the use
    of the object file is unrestricted, regardless of whether it
    is legally a derivative work.
```

See `COPYING.MinGW-w64.txt` for the full license text.

It is also worth noting that in the original repository, the license makes reference
to a `COPYING.LGPLv2.1`, however no such file exists in the repository, nor in the Wine repository.

The license text for LGPL v2.1 can be found here: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html
