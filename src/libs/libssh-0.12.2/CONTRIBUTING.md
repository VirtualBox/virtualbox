# How to contribute a patch to libssh

Please checkout the libssh source code using git.

For contributions we prefer Merge Requests on Gitlab:

https://gitlab.com/libssh/libssh-mirror/

This way you get continuous integration which runs the complete libssh
testsuite for you.

For larger code changes, breaking the changes up into a set of simple
patches, each of which does a single thing, are much easier to review.
Patch sets like that will most likely have an easier time being merged
into the libssh code than large single patches that make lots of
changes in one large diff.

Also bugfixes and new features should be covered by tests. We use the cmocka
and cwrap framework for our testing and you can simply run it locally by
calling `make test`.

## Ownership of the contributed code

libssh is a project with distributed copyright ownership, which means
we prefer the copyright on parts of libssh to be held by individuals
rather than corporations if possible. There are historical legal
reasons for this, but one of the best ways to explain it is that it's
much easier to work with individuals who have ownership than corporate
legal departments if we ever need to make reasonable compromises with
people using and working with libssh.

We track the ownership of every part of libssh via https://git.libssh.org,
our source code control system, so we know the provenance of every piece
of code that is committed to libssh.

So if possible, if you're doing libssh changes on behalf of a company
who normally owns all the work you do please get them to assign
personal copyright ownership of your changes to you as an individual,
that makes things very easy for us to work with and avoids bringing
corporate legal departments into the picture.

If you can't do this we can still accept patches from you owned by
your employer under a standard employment contract with corporate
copyright ownership. It just requires a simple set-up process first.

We use a process very similar to the way things are done in the Linux
Kernel community, so it should be very easy to get a sign off from
your corporate legal department. The only changes we've made are to
accommodate the license we use, which is LGPLv2 (or later) whereas the
Linux kernel uses GPLv2.

The process is called signing.

## How to sign your work

Once you have permission to contribute to libssh from your employer, simply
email a copy of the following text from your corporate email address to:

contributing@libssh.org


```
libssh Developer's Certificate of Origin. Version 1.0


By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the appropriate
    version of the GNU General Public License; or

(b) The contribution is based upon previous work that, to the best of
    my knowledge, is covered under an appropriate open source license
    and I have the right under that license to submit that work with
    modifications, whether created in whole or in part by me, under
    the GNU General Public License, in the appropriate version; or

(c) The contribution was provided directly to me by some other
    person who certified (a) or (b) and I have not modified it.

(d) I understand and agree that this project and the contribution are
    public and that a record of the contribution (including all
    metadata and personal information I submit with it, including my
    sign-off) is maintained indefinitely and may be redistributed
    consistent with the libssh Team's policies and the requirements of
    the GNU GPL where they are relevant.

(e) I am granting this work to this project under the terms of the
    GNU Lesser General Public License as published by the
    Free Software Foundation; either version 2.1 of
    the License, or (at the option of the project) any later version.

    https://www.gnu.org/licenses/lgpl-2.1.html
```

We will maintain a copy of that email as a record that you have the
rights to contribute code to libssh under the required licenses whilst
working for the company where the email came from.

Then when sending in a patch via the normal mechanisms described
above, add a line that states:

    Signed-off-by: Random J Developer <random@developer.example.org>

using your real name and the email address you sent the original email
you used to send the libssh Developer's Certificate of Origin to us
(sorry, no pseudonyms or anonymous contributions.)

That's it! Such code can then quite happily contain changes that have
copyright messages such as:

    (c) Example Corporation.

and can be merged into the libssh codebase in the same way as patches
from any other individual. You don't need to send in a copy of the
libssh Developer's Certificate of Origin for each patch, or inside each
patch. Just the sign-off message is all that is required once we've
received the initial email.

## Continuous Integration

Contributing patches through Merge Request workflow on Gitlab allows us to run
various checks on various configuration as part of Gitlab CI. Unfortunately,
some pipelines are slower (as they involve building dependencies) so the default
timeout of 1 hour needs to be extended at least to 2 hours. This can be done in
project settings of your libssh fork:

https://docs.gitlab.com/ee/ci/pipelines/settings.html#set-a-limit-for-how-long-jobs-can-run

Otherwise you will encounter errors like these, usually on visualstudio builds:

```
ERROR: Job failed: execution took longer than 1h0m0s seconds
The script exceeded the maximum execution time set for the job
```

Note, that the built dependencies are cached so after successful build in your
namespace, the rebuilds should be much faster.

## Running GitLab CI locally (optional helper)

For contributors working on CI, build system changes, or adding new CI jobs, it can be useful to run GitLab CI pipelines locally before pushing.

libssh provides a small helper script based on `gitlab-ci-local` that can:

- List all jobs defined in `.gitlab-ci.yml`
- Run a specific job or the full pipeline locally
- Automatically pick up new jobs when they are added to the CI configuration
- Optionally clean up CI Docker images after execution

### Requirements

- Docker (daemon running)
- git
- gitlab-ci-local  
  https://github.com/firecow/gitlab-ci-local

### Usage

```bash
./.gitlab-ci/local-ci.sh --list
./.gitlab-ci/local-ci.sh --run fedora/libressl/x86_64
./.gitlab-ci/local-ci.sh --all
./.gitlab-ci/local-ci.sh --run fedora/libressl/x86_64 --clean
```

# Coding conventions in the libssh tree

## Quick Start

Coding style guidelines are about reducing the number of unnecessary
reformatting patches and making things easier for developers to work together.

You don't have to like them or even agree with them, but once put in place we
all have to abide by them (or vote to change them).  However, coding style
should never outweigh coding itself and so the guidelines described here are
hopefully easy enough to follow as they are very common and supported by tools
and editors.

The basic style for C code, is the Linux kernel coding style (See
Documentation/CodingStyle in the kernel source tree). This closely matches what
libssh developers use already anyways, with a few exceptions as mentioned
below.

But to save you the trouble of reading the Linux kernel style guide, here
are the highlights.

* Maximum Line Width is 80 Characters
  The reason is not about people with low-res screens but rather sticking
  to 80 columns prevents you from easily nesting more than one level of
  if statements or other code blocks.

* Use 4 Spaces to Indent

* No Trailing Whitespace
  Clean up your files before committing.

* Follow the K&R guidelines.  We won't go through all of them here. Do you
  have a copy of "The C Programming Language" anyways right?


## Editor Hints

### Emacs

Add the follow to your $HOME/.emacs file:

    (add-hook 'c-mode-hook
      (lambda ()
          (c-set-style "linux")
          (c-toggle-auto-state)))


## Neovim/VIM

For the basic vi editor included with all variants of \*nix, add the
following to ~/.config/nvim/init.rc or ~/.vimrc:

    set ts=4 sw=4 et cindent

You can use the Vim gitmodline plugin to store this in the git config:

https://git.cryptomilk.org/projects/vim-gitmodeline.git/

For Vim, the following settings in $HOME/.vimrc will also deal with
displaying trailing whitespace:

    if has("syntax") && (&t_Co > 2 || has("gui_running"))
        syntax on
        function! ActivateInvisibleCharIndicator()
            syntax match TrailingSpace "[ \t]\+$" display containedin=ALL
            highlight TrailingSpace ctermbg=Red
        endf
        autocmd BufNewFile,BufRead * call ActivateInvisibleCharIndicator()
    endif
    " Show tabs, trailing whitespace, and continued lines visually
    set list listchars=tab:»·,trail:·,extends:…

    " highlight overly long lines same as TODOs.
    set textwidth=80
    autocmd BufNewFile,BufRead *.c,*.h exec 'match Todo /\%>' . &textwidth . 'v.\+/'


## FAQ & Statement Reference

### Comments

Comments should always use the standard C syntax.  C++ style comments are not
currently allowed.

The lines before a comment should be empty. If the comment directly belongs to
the following code, there should be no empty line after the comment, except if
the comment contains a summary of multiple following code blocks.

This is good:

    ...
    int i;

    /*
     * This is a multi line comment,
     * which explains the logical steps we have to do:
     *
     * 1. We need to set i=5, because...
     * 2. We need to call complex_fn1
     */

    /* This is a one line comment about i = 5. */
    i = 5;

    /*
     * This is a multi line comment,
     * explaining the call to complex_fn1()
     */
    ret = complex_fn1();
    if (ret != 0) {
    ...

    /**
     * @brief This is a doxygen comment.
     *
     * This is a more detailed explanation of
     * this simple function.
     *
     * @param[in]   param1     The parameter value of the function.
     *
     * @param[out]  result1    The result value of the function.
     *
     * @return              0 on success and -1 on error.
     */
    int example(int param1, int *result1);

This is bad:

    ...
    int i;
    /*
     * This is a multi line comment,
     * which explains the logical steps we have to do:
     *
     * 1. We need to set i=5, because...
     * 2. We need to call complex_fn1
     */
    /* This is a one line comment about i = 5. */
    i = 5;
    /*
     * This is a multi line comment,
     * explaining the call to complex_fn1()
     */
    ret = complex_fn1();
    if (ret != 0) {
    ...

    /*This is a one line comment.*/

    /* This is a multi line comment,
       with some more words...*/

    /*
     * This is a multi line comment,
     * with some more words...*/

### Indentation & Whitespace & 80 columns

To avoid confusion, indentations have to be 4 spaces. Do not use tabs!.  When
wrapping parameters for function calls, align the parameter list with the first
parameter on the previous line.  For example,

    var1 = foo(arg1,
               arg2,
               arg3);

The previous example is intended to illustrate alignment of function
parameters across lines and not as encourage for gratuitous line
splitting.  Never split a line before columns 70 - 79 unless you
have a really good reason.  Be smart about formatting.


### If, switch, & Code blocks

Always follow an 'if' keyword with a space but don't include additional
spaces following or preceding the parentheses in the conditional.
This is good:

    if (x == 1)

This is bad:

    if ( x == 1 )

or

    if (x==1)

Yes we have a lot of code that uses the second and third form and we are trying
to clean it up without being overly intrusive.

Note that this is a rule about parentheses following keywords and not
functions.  Don't insert a space between the name and left parentheses when
invoking functions.

Braces for code blocks used by for, if, switch, while, do..while, etc.  should
begin on the same line as the statement keyword and end on a line of their own.
You should always include braces, even if the block only contains one
statement.  **NOTE**: Functions are different and the beginning left brace should
be located in the first column on the next line.

If the beginning statement has to be broken across lines due to length, the
beginning brace should be on a line of its own.

The exception to the ending rule is when the closing brace is followed by
another language keyword such as else or the closing while in a do..while loop.

Good examples:

    if (x == 1) {
        printf("good\n");
    }

    for (x = 1; x < 10; x++) {
        print("%d\n", x);
    }

    for (really_really_really_really_long_var_name = 0;
         really_really_really_really_long_var_name < 10;
         really_really_really_really_long_var_name++)
    {
        print("%d\n", really_really_really_really_long_var_name);
    }

    do {
        printf("also good\n");
    } while (1);

Bad examples:

    while (1)
    {
        print("I'm in a loop!\n"); }

    for (x=1;
         x<10;
         x++)
    {
        print("no good\n");
    }

    if (i < 10)
        print("I should be in braces.\n");


### Goto

While many people have been academically taught that "goto"s are fundamentally
evil, they can greatly enhance readability and reduce memory leaks when used as
the single exit point from a function. But in no libssh world what so ever is a
goto outside of a function or block of code a good idea.

Good Examples:

    int function foo(int y)
    {
        int *z = NULL;
        int rc = 0;

        if (y < 10) {
            z = malloc(sizeof(int)*y);
            if (z == NULL) {
                rc = 1;
                goto done;
            }
        }

        print("Allocated %d elements.\n", y);

    done:
        if (z != NULL) {
            free(z);
        }

        return rc;
    }

### Initialize pointers

All pointer variables **MUST** be initialized to `NULL`. History has
demonstrated that uninitialized pointer variables have lead to various
bugs and security issues.

Pointers **MUST** be initialized even if the assignment directly follows
the declaration, like pointer2 in the example below, because the
instructions sequence may change over time.

Good Example:

    char *pointer1 = NULL;
    char *pointer2 = NULL;

    pointer2 = some_func2();

    ...

    pointer1 = some_func1();

### Typedefs

libssh tries to avoid `typedef struct { .. } x_t;` so we do always try to use
`struct x { .. };`. We know there are still such typedefs in the code, but for
new code, please don't do that anymore.

### Make use of helper variables

Please try to avoid passing function calls as function parameters in new code.
This makes the code much easier to read and it's also easier to use the "step"
command within gdb.

Good Example:

    char *name;

    name = get_some_name();
    if (name == NULL) {
        ...
    }

    rc = some_function_my_name(name);
    ...


Bad Example:

    rc = some_function_my_name(get_some_name());
    ...

Please try to avoid passing function return values to if- or while-conditions.
The reason for this is better handling of code under a debugger.

Good example:

    x = malloc(sizeof(short) * 10);
    if (x == NULL) {
        fprintf(stderr, "Unable to alloc memory!\n");
    }

Bad example:

    if ((x = malloc(sizeof(short)*10)) == NULL ) {
        fprintf(stderr, "Unable to alloc memory!\n");
    }

There are exceptions to this rule. One example is walking a data structure in
an iterator style:

    while ((opt = poptGetNextOpt(pc)) != -1) {
        ... do something with opt ...
    }

But in general, please try to avoid this pattern.


### Control-Flow changing macros

Macros like `STATUS_NOT_OK_RETURN` that change control flow (return/goto/etc)
from within the macro are considered bad, because they look like function calls
that never change control flow. Please do not introduce them.

### Switch/case indentation

The `case` should not be indented to avoid wasting too much horizontal space.
When the case block contains local variables that need to be wrapped in braces,
they should not be indented again either.

Good example:

    switch (x) {
    case 0:
        do_stuff();
        break;
    case 1: {
        int y;
        do_stuff();
        break;
    }
    default:
        do_other_stuff();
        break;
    }

Bad example:

    switch (x) {
        case 0:
            do_stuff();
            break;
        case 1:
            {
                int y;
                do_stuff();
                break;
            }
        default:
            do_other_stuff();
            break;
    }

## ABI Versioning and Symbol Management

To maintain [ABI](https://en.wikipedia.org/wiki/Application_binary_interface) stability
and ensure backward compatibility, libssh uses **symbol versioning** to track and manage
exported functions and variables. This allows libssh to introduce new symbols or modify
existing functions in an ABI-compatible way.

When introducing a new symbol:

1. Use the `LIBSSH_API` macro to mark the symbol as part of the public API.
2. If you have [abimap](https://github.com/ansasaki/abimap) installed, the new symbols are
automatically generated in the `src/libssh_dev.map` file in the **build** directory and used automatically for building the updated library. But, depending on the version of `abimap` under use, you may face linker errors like: `unable to find version dependency LIBSSH_4_9_0`. In this case, you need to manually replace the existing `src/libssh.map` file with the generated `libssh_dev.map` file to update the symbol versioning.
3. If you do not have abimap installed, the modified/added symbols must manually be added to the
`src/libssh.map` file. The symbols must be added in the following format (assuming that 4_10_0 is the latest released version):

```
LIBSSH_AFTER_4_10_0
{
    global:
        new_function;
        new_variable;
} LIBSSH_4_10_0;
```
4. After following either of the above steps, the library can be successfully built and
tested without any linker errors.

5. When submitting the patch, make sure that any new symbols have been added to `libssh.map` as described in step 3, so that the new additions may not be excluded from the next release due to human error.

Also, to maintain ABI compatibility, existing symbols must not be removed. Instead, they can
be marked as deprecated using the `LIBSSH_DEPRECATED` macro. This allows the symbol to be
removed in a future release without breaking the ABI.

Have fun and happy libssh hacking!

The libssh Team
