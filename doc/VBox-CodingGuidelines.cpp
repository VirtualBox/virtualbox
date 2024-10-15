/* $Id: VBox-CodingGuidelines.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Coding Guidelines.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/** @page pg_vbox_guideline                 VBox Coding Guidelines
 *
 * The compulsory sections of these guidelines are to be followed in all of the
 * VBox sources.  Please note that local guidelines in parts of the VBox source
 * tree may promote the optional ones to compulsory status.  The VBox tree also
 * contains some 3rd party sources where it is good to follow the local coding
 * style while keeping these guidelines in mind.
 *
 * Contents:
 *      - @ref sec_vbox_guideline_compulsory
 *          - @ref sec_vbox_guideline_compulsory_sub64
 *          - @ref sec_vbox_guideline_compulsory_cppmain
 *          - @ref sec_vbox_guideline_compulsory_cppqtgui
 *          - @ref sec_vbox_guideline_compulsory_xslt
 *          - @ref sec_vbox_guideline_compulsory_doxygen
 *          - @ref sec_vbox_guideline_compulsory_guest
 *      - @ref sec_vbox_guideline_optional
 *          - @ref sec_vbox_guideline_optional_layout
 *          - @ref sec_vbox_guideline_optional_prefix
 *          - @ref sec_vbox_guideline_optional_misc
 *      - @ref sec_vbox_guideline_warnings
 *          - @ref sec_vbox_guideline_warnings_signed_unsigned_compare
 *      - @ref sec_vbox_guideline_svn
 *
 * Local guidelines overrides:
 *      - src/VBox/VMM/:            @ref pg_vmm_guideline (src/VBox/VMM/Docs-CodingGuidelines.cpp)
 *      - src/VBox/ValidationKit/:  @ref pg_validationkit_guideline  (src/VBox/ValidationKit/ValidationKitCodingGuidelines.cpp)
 *      - src/VBox/Runtime/:        All of @ref sec_vbox_guideline_optional is mandatory.
 *      - src/VBox/Main/:           @ref sec_vbox_guideline_compulsory_cppmain
 *      - src/VBox/Frontends/VirtualBox/: @ref sec_vbox_guideline_compulsory_cppqtgui
 *
 *
 * @section sec_vbox_guideline_compulsory       Compulsory
 *
 * <ul>
 *
 *   <li> The indentation size is 4 chars.
 *
 *   <li> Tabs are only ever used in makefiles.
 *
 *   <li> Use RT and VBOX types.
 *
 *   <li> Use Runtime functions.
 *
 *   <li> Use the standard bool, uintptr_t, intptr_t and [u]int[1-9+]_t types.
 *
 *   <li> Avoid using plain unsigned and int.
 *
 *   <li> Use static wherever possible. This makes the namespace less polluted
 *        and avoids nasty name clash problems which can occur, especially on
 *        Unix-like systems. (1)  It also simplifies locating callers when
 *        changing it (single source file vs entire VBox tree).
 *
 *   <li> Public names are of the form Domain[Subdomain[]]Method, using mixed
 *        casing to mark the words. The main domain is all uppercase.
 *        (Think like java, mapping domain and subdomain to packages/classes.)
 *
 *   <li> Public names are always declared using the appropriate DECL macro. (2)
 *
 *   <li> Internal names starts with a lowercased main domain.
 *
 *   <li> Defines are all uppercase and separate words with underscore.
 *        This applies to enum values too.
 *
 *   <li> Typedefs are all uppercase and contain no underscores to distinguish
 *        them from defines.  Alternatively, all uppercase, separate words with
 *        underscores and ending with '_T'.  The latter is not allowed in IPRT.
 *
 *   <li> Pointer typedefs start with 'P'. If pointer to const then 'PC'.
 *
 *   <li> Function typedefs start with 'FN'. If pointer to FN then 'PFN'.
 *
 *   <li> All files are case sensitive.
 *
 *   <li> Slashes are unix slashes ('/') runtime converts when necessary.
 *
 *   <li> char strings are UTF-8.
 *
 *   <li> Strings from any external source must be treated with utmost care as
 *        they do not have to be valid UTF-8.  Only trust internal strings.
 *
 *   <li> All functions return VBox status codes. There are three general
 *        exceptions from this:
 *
 *       <ol>
 *             <li>Predicate functions. These are function which are boolean in
 *                 nature and usage. They return bool. The function name will
 *                 include 'Has', 'Is' or similar.
 *             <li>Functions which by nature cannot possibly fail.
 *                 These return void.
 *             <li>"Get"-functions which return what they ask for.
 *                 A get function becomes a "Query" function if there is any
 *                 doubt about getting what is ask for.
 *       </ol>
 *
 *   <li> VBox status codes have three subdivisions:
 *         <ol>
 *           <li> Errors, which are VERR_ prefixed and negative.
 *           <li> Warnings, which are VWRN_ prefixed and positive.
 *           <li> Informational, which are VINF_ prefixed and positive.
 *        </ol>
 *
 *   <li> Platform/OS operation are generalized and put in the IPRT.
 *
 *   <li> Other useful constructs are also put in the IPRT.
 *
 *   <li> The code shall not cause compiler warnings. Check this on ALL
 *        the platforms.
 *
 *   <li> The use of symbols leading with single or double underscores is
 *        forbidden as that intrudes on reserved compiler/system namespace. (3)
 *
 *   <li> All files have file headers with $Id and a file tag which describes
 *        the file in a sentence or two.
 *        Note: Use the svn-ps.cmd/svn-ps.sh utility with the -a option to add
 *              new sources with keyword expansion and exporting correctly
 *              configured.
 *
 *   <li> All public functions are fully documented in Doxygen style using the
 *        javadoc dialect (using the 'at' instead of the 'slash' as
 *        commandprefix.)
 *
 *   <li> All structures in header files are described, including all their
 *        members. (Doxygen style, of course.)
 *
 *   <li> All modules have a documentation '\@page' in the main source file
 *        which describes the intent and actual implementation.
 *
 *   <li> Code which is doing things that are not immediately comprehensible
 *        shall include explanatory comments.
 *
 *   <li> Documentation and comments are kept up to date.
 *
 *   <li> Headers in /include/VBox shall not contain any slash-slash C++
 *        comments, only ANSI C comments!
 *
 *   <li> Comments on \#else indicates what begins while the comment on a
 *        \#endif indicates what ended.  Only add these when there are more than
 *        a few lines (6-10) of \#ifdef'ed code, otherwise they're just clutter.
 *
 *   <li> \#ifdefs around a single function shall be tight, i.e. no empty
 *        lines between it and the function documentation and body.
 *
 *   <li> \#ifdefs around more than one function shall be relaxed, i.e. leave at
 *        least one line before the first function's documentation comment and
 *        one line after the end of the last function.
 *
 *   <li> No 'else' after if block ending with 'return', 'break', or 'continue'.
 *
 *   <li> The term 'last' is inclusive, whereas the term 'end' is exclusive.
 *
 *   <li> Go through all of this: https://www.slideshare.net/olvemaudal/deep-c/
 *
 *   <li> Avoid throwing exceptions, always prefer returning statuses.
 *        Crappy exception handling is rewared by a glass of water in the face.
 *
 *   <li> Always cast bitfields members to the desired type before using them in
 *        calculations as Visual C++ and g++/clang++ may use different types.
 *
 *        It seems like Visual C++ will use the basetype of the field, while the
 *        other two will narrow the type down to the number of bits specified
 *        and then subject it to standard type promotion which typically ends up
 *        with signed int.
 *
 * </ul>
 *
 * (1) It is common practice on Unix to have a single symbol namespace for an
 *     entire process. If one is careless symbols might be resolved in a
 *     different way that one expects, leading to weird problems.
 *
 * (2) This is common practice among most projects dealing with modules in
 *     shared libraries. The Windows / PE __declspect(import) and
 *     __declspect(export) constructs are the main reason for this.
 *     OTOH, we do perhaps have a bit too detailed graining of this in VMM...
 *
 * (3) There are guys out there grepping public sources for symbols leading with
 *     single and double underscores as well as gotos and other things
 *     considered bad practice.  They'll post statistics on how bad our sources
 *     are on some mailing list, forum or similar.
 *
 *
 * @subsection sec_vbox_guideline_compulsory_sub64  64-bit and 32-bit
 *
 * Here are some amendments which address 64-bit vs. 32-bit portability issues.
 *
 * Some facts first:
 *
 * <ul>
 *
 *   <li> On 64-bit Windows the type long remains 32-bit. On nearly all other
 *        64-bit platforms long is 64-bit.
 *
 *   <li> On all 64-bit platforms we care about, int is 32-bit, short is 16 bit
 *        and char is 8-bit.
 *        (I don't know about any platforms yet where this isn't true.)
 *
 *   <li> size_t, ssize_t, uintptr_t, ptrdiff_t and similar are all 64-bit on
 *        64-bit platforms. (These are 32-bit on 32-bit platforms.)
 *
 *   <li> There is no inline assembly support in the 64-bit Microsoft compilers.
 *
 * </ul>
 *
 * Now for the guidelines:
 *
 * <ul>
 *
 *   <li> Never, ever, use int, long, ULONG, LONG, DWORD or similar to cast a
 *        pointer to integer.  Use uintptr_t or intptr_t. If you have to use
 *        NT/Windows types, there is the choice of ULONG_PTR and DWORD_PTR.
 *
 *   <li> Avoid where ever possible the use of the types 'long' and 'unsigned
 *        long' as these differs in size between windows and the other hosts
 *        (see above).
 *
 *   <li> RT_OS_WINDOWS is defined to indicate Windows. Do not use __WIN32__,
 *        __WIN64__ and __WIN__ because they are all deprecated and scheduled
 *        for removal (if not removed already). Do not use the compiler
 *        defined _WIN32, _WIN64, or similar either. The bitness can be
 *        determined by testing ARCH_BITS.
 *        Example:
 *        @code
 *              #ifdef RT_OS_WINDOWS
 *              // call win32/64 api.
 *              #endif
 *              #ifdef RT_OS_WINDOWS
 *              # if ARCH_BITS == 64
 *              // call win64 api.
 *              # else  // ARCH_BITS == 32
 *              // call win32 api.
 *              # endif // ARCH_BITS == 32
 *              #else  // !RT_OS_WINDOWS
 *              // call posix api
 *              #endif // !RT_OS_WINDOWS
 *        @endcode
 *
 *   <li> There are RT_OS_xxx defines for each OS, just like RT_OS_WINDOWS
 *        mentioned above. Use these defines instead of any predefined
 *        compiler stuff or defines from system headers.
 *
 *   <li> RT_ARCH_X86 is defined when compiling for the x86 the architecture.
 *        Do not use __x86__, __X86__, __[Ii]386__, __[Ii]586__, or similar
 *        for this purpose.
 *
 *   <li> RT_ARCH_AMD64 is defined when compiling for the AMD64 architecture.
 *        Do not use __AMD64__, __amd64__ or __x64_86__.
 *
 *   <li> Take care and use size_t when you have to, esp. when passing a pointer
 *        to a size_t as a parameter.
 *
 *   <li> Be wary of type promotion to (signed) integer. For example the
 *        following will cause u8 to be promoted to int in the shift, and then
 *        sign extended in the assignment 64-bit:
 *        @code
 *              uint8_t u8 = 0xfe;
 *              uint64_t u64 = u8 << 24;
 *              // u64 == 0xfffffffffe000000
 *        @endcode
 *
 * </ul>
 *
 * @subsubsection sec_vbox_guideline_compulsory_sub64_comp  Comparing the GCC and MSC calling conventions
 *
 * GCC expects the following (cut & past from page 20 in the ABI draft 0.96):
 *
 * @verbatim
    %rax     temporary register; with variable arguments passes information about the
             number of SSE registers used; 1st return register.
             [Not preserved]
    %rbx     callee-saved register; optionally used as base pointer.
             [Preserved]
    %rcx     used to pass 4th integer argument to functions.
             [Not preserved]
    %rdx     used to pass 3rd argument to functions; 2nd return register
             [Not preserved]
    %rsp     stack pointer
             [Preserved]
    %rbp     callee-saved register; optionally used as frame pointer
             [Preserved]
    %rsi     used to pass 2nd argument to functions
             [Not preserved]
    %rdi     used to pass 1st argument to functions
             [Not preserved]
    %r8      used to pass 5th argument to functions
             [Not preserved]
    %r9      used to pass 6th argument to functions
             [Not preserved]
    %r10     temporary register, used for passing a function's static chain
             pointer [Not preserved]
    %r11     temporary register
             [Not preserved]
    %r12-r15 callee-saved registers
             [Preserved]
    %xmm0-%xmm1  used to pass and return floating point arguments
             [Not preserved]
    %xmm2-%xmm7  used to pass floating point arguments
             [Not preserved]
    %xmm8-%xmm15 temporary registers
             [Not preserved]
    %mmx0-%mmx7  temporary registers
             [Not preserved]
    %st0     temporary register; used to return long double arguments
             [Not preserved]
    %st1     temporary registers; used to return long double arguments
             [Not preserved]
    %st2-%st7 temporary registers
             [Not preserved]
    %fs      Reserved for system use (as thread specific data register)
             [Not preserved]
   @endverbatim
 *
 * Direction flag is preserved as cleared.
 * The stack must be aligned on a 16-byte boundary before the 'call/jmp' instruction.
 *
 * MSC expects the following:
 * @verbatim
    rax      return value, not preserved.
    rbx      preserved.
    rcx      1st argument, integer, not preserved.
    rdx      2nd argument, integer, not preserved.
    rbp      preserved.
    rsp      preserved.
    rsi      preserved.
    rdi      preserved.
    r8       3rd argument, integer, not preserved.
    r9       4th argument, integer, not preserved.
    r10      scratch register, not preserved.
    r11      scratch register, not preserved.
    r12-r15  preserved.
    xmm0     1st argument, fp, return value, not preserved.
    xmm1     2st argument, fp, not preserved.
    xmm2     3st argument, fp, not preserved.
    xmm3     4st argument, fp, not preserved.
    xmm4-xmm5    scratch, not preserved.
    xmm6-xmm15   preserved.
   @endverbatim
 *
 * Dunno what the direction flag is...
 * The stack must be aligned on a 16-byte boundary before the 'call/jmp' instruction.
 *
 *
 * @subsection sec_vbox_guideline_compulsory_cppmain   C++ guidelines for Main
 *
 * Since the Main API code is a large amount of C++ code, it is allowed but
 * not required to use C++ style comments (as permanent comments, beyond the
 * temporary use allowed by the general coding guideline).  This is a weak
 * preference, i.e. large scale comment style changes are not encouraged.
 *
 * Main is currently (2009) full of hard-to-maintain code that uses complicated
 * templates. The new mid-term goal for Main is to have less custom templates
 * instead of more for the following reasons:
 *
 * <ul>
 *
 *   <li>  Template code is harder to read and understand. Custom templates create
 *         territories which only the code writer understands.
 *
 *   <li>  Errors in using templates create terrible C++ compiler messages.
 *
 *   <li>  Template code is really hard to look at in a debugger.
 *
 *   <li>  Templates slow down the compiler a lot.
 *
 * </ul>
 *
 * In particular, the following bits should be considered deprecated and should
 * NOT be used in new code:
 *
 * <ul>
 *
 *   <li>  everything in include/iprt/cpputils.h (auto_ref_ptr, exception_trap_base,
 *         char_auto_ptr and friends)
 *
 * </ul>
 *
 * Generally, in many cases, a simple class with a proper destructor can achieve
 * the same effect as a 1,000-line template include file, and the code is
 * much more accessible that way.
 *
 * Using standard STL templates like std::list, std::vector and std::map is OK.
 * Exceptions are:
 *
 * <ul>
 *
 *   <li>  Guest Additions because we don't want to link against libstdc++ there.
 *
 *   <li>  std::string should not be used because we have iprt::MiniString and
 *     com::Utf8Str which can convert efficiently with COM's UTF-16 strings.
 *
 *   <li>  std::auto_ptr<> in general; that part of the C++ standard is just broken.
 *     Write a destructor that calls delete.
 *
 * </ul>
 *
 * @subsection sec_vbox_guideline_compulsory_cppqtgui   C++ guidelines for the Qt GUI
 *
 * The Qt GUI is currently (2010) on its way to become more compatible to the
 * rest of VirtualBox coding style wise. From now on, all the coding style
 * rules described in this file are also mandatory for the Qt GUI. Additionally
 * the following rules should be respected:
 *
 * <ul>
 *
 *   <li> GUI classes which correspond to GUI tasks should be prefixed by UI (no VBox anymore)
 *
 *   <li> Classes which extents some of the Qt classes should be prefix by QI
 *
 *   <li> General task classes should be prefixed by C
 *
 *   <li> Slots are prefixed by slt -> sltName
 *
 *   <li> Signals are prefixed by sig -> sigName
 *
 *   <li> Use Qt classes for lists, strings and so on, the use of STL classes should
 *   be avoided
 *
 *   <li> All files like .cpp, .h, .ui, which belong together are located in the
 *   same directory and named the same
 *
 * </ul>
 *
 *
 * @subsection sec_vbox_guideline_compulsory_xslt  XSLT
 *
 * XSLT (eXtensible Stylesheet Language Transformations) is used quite a bit in
 * the Main API area of VirtualBox to generate sources and bindings to that API.
 * There are a couple of common pitfalls worth mentioning:
 *
 * <ul>
 *
 *   <li> Never do repeated //interface[\@name=...] and //enum[\@name=...] lookups
 *        because they are expensive. Instead delcare xsl:key elements for these
 *        searches and do the lookup using the key() function. xsltproc uses
 *        (per current document) hash tables for each xsl:key, i.e. very fast.
 *
 *   <li> When output type is 'text' make sure to call xsltprocNewlineOutputHack
 *        from typemap-shared.inc.xsl every few KB of output, or xsltproc will
 *        end up wasting all the time reallocating the output buffer.
 *
 * </ul>
 *
 *
 * @subsection sec_vbox_guideline_compulsory_doxygen    Doxygen Comments
 *
 * As mentioned above, we shall use doxygen/javadoc style commenting of public
 * functions, typedefs, classes and such.  It is mandatory to use this style
 * everywhere!
 *
 * A couple of hints on how to best write doxygen comments:
 *
 * <ul>
 *
 *   <li> A good class, method, function, structure or enum doxygen comment
 *        starts with a one line sentence giving a brief description of the
 *        item.  Details comes in a new paragraph (after blank line).
 *
 *   <li> Except for list generators like \@todo, \@cfgm, \@gcfgm and others,
 *        all doxygen comments are related to things in the code.  So, for
 *        instance you DO NOT add a doxygen \@note comment in the middle of a
 *        because you've got something important to note, you add a normal
 *        comment like 'Note! blah, very importan blah!'
 *
 *   <li> We do NOT use TODO/XXX/BUGBUG or similar markers in the code to flag
 *        things needing fixing later, we always use \@todo doxygen comments.
 *
 *   <li> There is no colon after the \@todo.  And it is ALWAYS in a doxygen
 *        comment.
 *
 *   <li> The \@retval tag is used to explain status codes a method/function may
 *        returns.  It is not used to describe output parameters, that is done
 *        using the \@param or \@param[out] tag.
 *
 * </ul>
 *
 * See https://www.stack.nl/~dimitri/doxygen/manual/index.html for the official
 * doxygen documention.
 *
 *
 *
 * @subsection sec_vbox_guideline_compulsory_guest  Handling of guest input
 *
 * First, guest input should ALWAYS be consider to be TOXIC and constructed with
 * MALICIOUS intent!  Max paranoia level!
 *
 * Second, when getting inputs from memory shared with the guest, be EXTREMELY
 * careful to not re-read input from shared memory after validating it, because
 * that will create TOCTOU problems. So, after reading input from shared memory
 * always use the RT_UNTRUSTED_NONVOLATILE_COPY_FENCE() macro.  For more details
 * on TOCTOU: https://en.wikipedia.org/wiki/Time_of_check_to_time_of_use
 *
 * Thirdly, considering the recent speculation side channel issues, spectre v1
 * in particular, we would like to be ready for future screwups.   This means
 * having input validation in a separate block of code that ends with one (or
 * more) RT_UNTRUSTED_VALIDATED_FENCE().
 *
 * So the rules:
 *
 * <ul>
 *
 *   <li> Mark all pointers to shared memory with RT_UNTRUSTED_VOLATILE_GUEST.
 *
 *   <li> Copy volatile data into local variables or heap before validating
 *        them (see RT_COPY_VOLATILE() and RT_BCOPY_VOLATILE().
 *
 *   <li> Place RT_UNTRUSTED_NONVOLATILE_COPY_FENCE() after a block copying
 *        volatile data.
 *
 *   <li> Always validate untrusted inputs in a block ending with a
 *        RT_UNTRUSTED_VALIDATED_FENCE().
 *
 *   <li> Use the ASSERT_GUEST_XXXX macros from VBox/AssertGuest.h to validate
 *        guest input.  (Do NOT use iprt/assert.h macros.)
 *
 *   <li> Validation of an input B may require using another input A to look up
 *        some data, in which case its necessary to insert an
 *        RT_UNTRUSTED_VALIDATED_FENCE() after validating A and before A is used
 *        for the lookup.
 *
 *        For example A is a view identifier, idView, and B is an offset into
 *        the view's framebuffer area, offView.  To validate offView (B) it is
 *        necessary to get the size of the views framebuffer region:
 *        @code
 *              uint32_t const idView  = pReq->idView;   // A
 *              uint32_t const offView = pReq->offView;  // B
 *              RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
 *
 *              ASSERT_GUEST_RETURN(idView < pThis->cView,
 *                                  VERR_INVALID_PARAMETER);
 *              RT_UNTRUSTED_VALIDATED_FENCE();
 *              const MYVIEW *pView = &pThis->aViews[idView];
 *              ASSERT_GUEST_RETURN(offView < pView->cbFramebufferArea,
 *                                  VERR_OUT_OF_RANGE);
 *              RT_UNTRUSTED_VALIDATED_FENCE();
 *        @endcode
 *
 *   <li> Take care to make sure input check are not subject to integer overflow problems.
 *
 *        For instance when validating an area, you must not just add cbDst + offDst
 *        and check against pThis->offEnd or something like that.  Rather do:
 *        @code
 *              uint32_t const offDst = pReq->offDst;
 *              uint32_t const cbDst  = pReq->cbDst;
 *              RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
 *
 *              ASSERT_GUEST_RETURN(   cbDst  <= pThis->cbSrc
 *                                  && offDst < pThis->cbSrc - cbDst,
 *                                  VERR_OUT_OF_RANGE);
 *              RT_UNTRUSTED_VALIDATED_FENCE();
 *        @endcode
 *
 *   <li> Input validation does not only apply to shared data cases, but also to
 *        I/O port and MMIO handlers.
 *
 *   <li> Ditto for kernel drivers working with usermode inputs.
 *
 * </ul>
 *
 *
 * Problem patterns:
 *   - https://en.wikipedia.org/wiki/Time_of_check_to_time_of_use
 *   - https://googleprojectzero.blogspot.de/2018/01/reading-privileged-memory-with-side.html
 *     (Variant 1 only).
 *   - https://en.wikipedia.org/wiki/Integer_overflow
 *
 *
 *
 * @section sec_vbox_guideline_optional         Optional
 *
 * First part is the actual coding style and all the prefixes.  The second part
 * is a bunch of good advice.
 *
 *
 * @subsection sec_vbox_guideline_optional_layout   The code layout
 *
 * <ul>
 *
 *   <li> Max line length is 130 chars.  Exceptions are table-like
 *        code/initializers and Log*() statements (don't waste unnecessary
 *        vertical space on debug logging).
 *
 *   <li> Comments should try stay within the usual 80 columns as these are
 *        denser and too long lines may be harder to read.
 *
 *   <li> Curly brackets are not indented.  Example:
 *        @code
 *              if (true)
 *              {
 *                  Something1();
 *                  Something2();
 *              }
 *              else
 *              {
 *                  SomethingElse1().
 *                  SomethingElse2().
 *              }
 *        @endcode
 *
 *   <li> Space before the parentheses when it comes after a C keyword.
 *
 *   <li> No space between argument and parentheses. Exception for complex
 *        expression.  Example:
 *        @code
 *              if (PATMR3IsPatchGCAddr(pVM, GCPtr))
 *        @endcode
 *
 *   <li> The else of an if is always the first statement on a line. (No curly
 *        stuff before it!)
 *
 *   <li> else and if go on the same line if no { compound statement }
 *        follows the if.  Example:
 *        @code
 *              if (fFlags & MYFLAGS_1)
 *                  fFlags &= ~MYFLAGS_10;
 *              else if (fFlags & MYFLAGS_2)
 *              {
 *                  fFlags &= ~MYFLAGS_MASK;
 *                  fFlags |= MYFLAGS_5;
 *              }
 *              else if (fFlags & MYFLAGS_3)
 *        @endcode
 *
 *   <li> Slightly complex boolean expressions are split into multiple lines,
 *        putting the operators first on the line and indenting it all according
 *        to the nesting of the expression. The purpose is to make it as easy as
 *        possible to read.  Example:
 *        @code
 *              if (    RT_SUCCESS(rc)
 *                  ||  (fFlags & SOME_FLAG))
 *        @endcode
 *
 *   <li> When 'if' or  'while' statements gets long, the closing parentheses
 *        goes right below the opening parentheses.  This may be applied to
 *        sub-expression.  Example:
 *        @code
 *              if (    RT_SUCCESS(rc)
 *                  ||  (   fSomeStuff
 *                       && fSomeOtherStuff
 *                       && fEvenMoreStuff
 *                      )
 *                  ||  SomePredicateFunction()
 *                 )
 *              {
 *                  ...
 *              }
 *        @endcode
 *
 *   <li> The case is indented from the switch (to avoid having the braces for
 *        the 'case' at the same level as the 'switch' statement).
 *
 *   <li> If a case needs curly brackets they contain the entire case, are not
 *        indented from the case, and the break or return is placed inside them.
 *        Example:
 *        @code
 *              switch (pCur->eType)
 *              {
 *                  case PGMMAPPINGTYPE_PAGETABLES:
 *                  {
 *                      unsigned iPDE = pCur->GCPtr >> PGDIR_SHIFT;
 *                      unsigned iPT = (pCur->GCPtrEnd - pCur->GCPtr) >> PGDIR_SHIFT;
 *                      while (iPT-- > 0)
 *                          if (pPD->a[iPDE + iPT].n.u1Present)
 *                              return VERR_HYPERVISOR_CONFLICT;
 *                      break;
 *                  }
 *              }
 *        @endcode
 *
 *   <li> In a do while construction, the while is on the same line as the
 *        closing "}" if any are used.
 *        Example:
 *        @code
 *              do
 *              {
 *                  stuff;
 *                  i--;
 *              } while (i > 0);
 *        @endcode
 *
 *   <li> Comments are in C style.  C++ style comments are used for temporary
 *        disabling a few lines of code.
 *
 *   <li> No unnecessary parentheses in expressions (just don't over do this
 *        so that gcc / msc starts bitching). Find a correct C/C++ operator
 *        precedence table if needed.
 *
 *   <li> 'for (;;)' is preferred over 'while (true)' and 'while (1)'.
 *
 *   <li> Parameters are indented to the start parentheses when breaking up
 *        function calls, declarations or prototypes.  (This is in line with
 *        how 'if', 'for' and 'while' statements are done as well.) Example:
 *        @code
 *              RTPROCESS hProcess;
 *              int rc = RTProcCreateEx(papszArgs[0],
 *                                      papszArgs,
 *                                      RTENV_DEFAULT,
 *                                      fFlags,
 *                                      NULL,           // phStdIn
 *                                      NULL,           // phStdOut
 *                                      NULL,           // phStdErr
 *                                      NULL,           // pszAsUser
 *                                      NULL,           // pszPassword
 *                                      NULL,           // pExtraData
 *                                      &hProcess);
 *        @endcode
 *
 *   <li> That Dijkstra is dead is no excuse for using gotos.
 *
 *   <li> Using do-while-false loops to avoid gotos is considered very bad form.
 *        They create hard to read code.  They tend to be either too short (i.e.
 *        pointless) or way to long (split up the function already), making
 *        tracking the state is difficult and prone to bugs.  Also, they cause
 *        the compiler to generate suboptimal code, because the break branches
 *        are by preferred over the main code flow (MSC has no branch hinting!).
 *        Instead, do make use the 130 columns (i.e. nested ifs) and split
 *        the code up into more functions!
 *
 *   <li> Avoid code like
 *        @code
 *              int foo;
 *              int rc;
 *              ...
 *              rc = FooBar();
 *              if (RT_SUCCESS(rc))
 *              {
 *                  foo = getFoo();
 *                  ...
 *                  pvBar = RTMemAlloc(sizeof(*pvBar));
 *                  if (!pvBar)
 *                     rc = VERR_NO_MEMORY;
 *              }
 *              if (RT_SUCCESS(rc))
 *              {
 *                  buzz = foo;
 *                  ...
 *              }
 *        @endcode
 *        The intention of such code is probably to save some horizontal space
 *        but unfortunately it's hard to read and the scope of certain varables
 *        (e.g. foo in this example) is not optimal. Better use the following
 *        style:
 *        @code
 *              int rc;
 *              ...
 *              rc = FooBar();
 *              if (RT_SUCCESS(rc))
 *              {
 *                  int foo = getFoo();
 *                  ...
 *                  pvBar = RTMemAlloc(sizeof(*pvBar));
 *                  if (pvBar)
 *                  {
 *                      buzz = foo;
 *                      ...
 *                  }
 *                  else
 *                      rc = VERR_NO_MEMORY;
 *              }
 *        @endcode
 *
 * </ul>
 *
 * @subsection sec_vbox_guideline_optional_prefix   Variable / Member Prefixes
 *
 * Prefixes are meant to provide extra context clues to a variable/member, we
 * therefore avoid using prefixes that just indicating the type if a better
 * choice is available.
 *
 *
 * The prefixes:
 *
 * <ul>
 *
 *   <li> The 'g_' (or 'g') prefix means a global variable, either on file or module level.
 *
 *   <li> The 's_' (or 's') prefix means a static variable inside a function or
 *        class.  This is not used for static variables on file level, use 'g_'
 *        for those (logical, right).
 *
 *   <li> The 'm_' (or 'm') prefix means a class data member.
 *
 *        In new code in Main, use "m_" (and common sense).  As an exception,
 *        in Main, if a class encapsulates its member variables in an anonymous
 *        structure which is declared in the class, but defined only in the
 *        implementation (like this: 'class X { struct Data; Data *m; }'), then
 *        the pointer to that struct is called 'm' itself and its members then
 *        need no prefix, because the members are accessed with 'm->member'
 *        already which is clear enough.
 *
 *   <li> The 'a_' prefix means a parameter (argument) variable.  This is
 *        sometimes written 'a' in parts of the source code that does not use
 *        the array prefix.
 *
 *   <li> The 'p' prefix means pointer.  For instance 'pVM' is pointer to VM,
 *        'pidx' means pointer to an index and 'pv' is a generic void pointer.
 *
 *   <li> The 'r' prefix means that something is passed by reference.
 *
 *   <li> The 'k' prefix means that something is a constant.  For instance
 *        'enum { kStuff };'.  This is usually not used in combination with
 *        'p', 'r' or any such thing, it's main main use is to make enums
 *        easily identifiable.
 *
 *   <li> The 'a' prefix means array.  For instance 'aPages' could be read as
 *        array of pages.
 *
 *   <li> The 'c' prefix means count.  For instance 'cbBlock' could be read,
 *        count of bytes in block. (1)
 *
 *   <li> The 'cx' prefix means width (count of 'x' units).
 *
 *   <li> The 'cy' prefix means height (count of 'y' units).
 *
 *   <li> The 'x', 'y' and 'z' prefix refers to the x-, y- , and z-axis
 *        respectively.
 *
 *   <li> The 'off' prefix means offset.
 *
 *   <li> The 'i' or 'idx' prefixes usually means index.  Although the 'i' one
 *        can sometimes just mean signed integer.
 *
 *   <li> The 'i[1-9]+' prefix means a fixed bit size variable.  Frequently
 *        used with the int[1-9]+_t types where the width is really important.
 *        In most cases 'i' is more appropriate.  [type]
 *
 *   <li> The 'e' (or 'enm') prefix means enum.
 *
 *   <li> The 'u' prefix usually means unsigned integer.  Exceptions follows.
 *
 *   <li> The 'u[1-9]+' prefix means a fixed bit size variable.  Frequently
 *        used with the uint[1-9]+_t types and with bitfields where the width is
 *        really important.  In most cases 'u' or 'b' (byte) would be more
 *        appropriate.  [type]
 *
 *   <li> The 'b' prefix means byte or bytes.  [type]
 *
 *   <li> The 'f' prefix means flags.  Flags are unsigned integers of some kind
 *        or booleans.
 *
 *   <li> TODO: need prefix for real float.  [type]
 *
 *   <li> The 'rd' prefix means real double and is used for 'double' variables.
 *        [type]
 *
 *   <li> The 'lrd' prefix means long real double and is used for 'long double'
 *        variables. [type]
 *
 *   <li> The 'ch' prefix means a char, the (signed) char type.  [type]
 *
 *   <li> The 'wc' prefix means a wide/windows char, the RTUTF16 type.  [type]
 *
 *   <li> The 'uc' prefix means a Unicode Code point, the RTUNICP type.  [type]
 *
 *   <li> The 'uch' prefix means unsigned char.  It's rarely used.  [type]
 *
 *   <li> The 'sz' prefix means zero terminated character string (array of
 *        chars). (UTF-8)
 *
 *   <li> The 'wsz' prefix means zero terminated wide/windows character string
 *        (array of RTUTF16).
 *
 *   <li> The 'usz' prefix means zero terminated Unicode string (array of
 *        RTUNICP).
 *
 *   <li> The 'str' prefix means C++ string; either a std::string or, in Main,
 *        a Utf8Str or, in Qt, a QString.  When used with 'p', 'r', 'a' or 'c'
 *        the first letter should be capitalized.
 *
 *   <li> The 'bstr' prefix, in Main, means a UTF-16 Bstr. When used with 'p',
 *        'r', 'a' or 'c' the first letter should be capitalized.
 *
 *   <li> The 'pfn' prefix means pointer to function. Common usage is 'pfnCallback'
 *        and such like.
 *
 *   <li> The 'psz' prefix is a combination of 'p' and 'sz' and thus means
 *        pointer to a zero terminated character string. (UTF-8)
 *
 *   <li> The 'pcsz' prefix is used to indicate constant string pointers in
 *        parts of the code.  Most code uses 'psz' for const and non-const
 *        string pointers, so please ignore this one.
 *
 *   <li> The 'l' prefix means (signed) long.  We try avoid using this,
 *        expecially with the 'LONG' types in Main as these are not 'long' on
 *        64-bit non-Windows platforms and can cause confusion. Alternatives:
 *        'i' or 'i32'.  [type]
 *
 *   <li> The 'ul' prefix means unsigned long.  We try avoid using this,
 *        expecially with the 'ULONG' types in Main as these are not 'unsigned
 *        long' on 64-bit non-Windows platforms and can cause confusion.
 *        Alternatives: 'u' or 'u32'.  [type]
 *
 * </ul>
 *
 * (1)  Except in the occasional 'pcsz' prefix, the 'c' prefix is never ever
 *      used in the meaning 'const'.
 *
 *
 * @subsection sec_vbox_guideline_optional_misc     Misc / Advice / Stuff
 *
 * <ul>
 *
 *   <li> When writing code think as the reader.
 *
 *   <li> When writing code think as the compiler. (2)
 *
 *   <li> When reading code think as if it's full of bugs - find them and fix them.
 *
 *   <li> Pointer within range tests like:
 *        @code
 *          if ((uintptr_t)pv >= (uintptr_t)pvBase && (uintptr_t)pv < (uintptr_t)pvBase + cbRange)
 *        @endcode
 *        Can also be written as (assuming cbRange unsigned):
 *        @code
 *          if ((uintptr_t)pv - (uintptr_t)pvBase < cbRange)
 *        @endcode
 *        Which is shorter and potentially faster. (1)
 *
 *   <li> Avoid unnecessary casting. All pointers automatically cast down to
 *        void *, at least for non class instance pointers.
 *
 *   <li> It's very very bad practise to write a function larger than a
 *        screen full (1024x768) without any comprehensibility and explaining
 *        comments.
 *
 *   <li> More to come....
 *
 * </ul>
 *
 * (1)  Important, be very careful with the casting. In particular, note that
 *      a compiler might treat pointers as signed (IIRC).
 *
 * (2)  "A really advanced hacker comes to understand the true inner workings of
 *      the machine - he sees through the language he's working in and glimpses
 *      the secret functioning of the binary code - becomes a Ba'al Shem of
 *      sorts."   (Neal Stephenson "Snow Crash")
 *
 *
 *
 * @section sec_vbox_guideline_warnings     Compiler Warnings
 *
 * The code should when possible compile on all platforms and compilers without any
 * warnings. That's a nice idea, however, if it means making the code harder to read,
 * less portable, unreliable or similar, the warning should not be fixed.
 *
 * Some of the warnings can seem kind of innocent at first glance. So, let's take the
 * most common ones and explain them.
 *
 *
 * @subsection sec_vbox_guideline_warnings_signed_unsigned_compare      Signed / Unsigned Compare
 *
 * GCC says: "warning: comparison between signed and unsigned integer expressions"
 * MSC says: "warning C4018: '<|<=|==|>=|>' : signed/unsigned mismatch"
 *
 * The following example will not output what you expect:
@code
#include <stdio.h>
int main()
{
    signed long a = -1;
    unsigned long b = 2294967295;
    if (a < b)
        printf("%ld < %lu: true\n", a, b);
    else
        printf("%ld < %lu: false\n", a, b);
    return 0;
}
@endcode
 * If I understood it correctly, the compiler will convert a to an
 * unsigned long before doing the compare.
 *
 *
 *
 * @section sec_vbox_guideline_svn          Subversion Commit Rules
 *
 *
 * Before checking in:
 *
 * <ul>
 *
 *   <li> Check Tinderbox and make sure the tree is green across all platforms. If it's
 *        red on a platform, don't check in. If you want, warn in the \#vbox channel and
 *        help make the responsible person fix it.
 *        NEVER CHECK IN TO A BROKEN BUILD.
 *
 *   <li> When checking in keep in mind that a commit is atomic and that the Tinderbox and
 *        developers are constantly checking out the tree. Therefore do not split up the
 *        commit unless it's into 100% independent parts. If you need to split it up in order
 *        to have sensible commit comments, make the sub-commits as rapid as possible.
 *
 *   <li> If you make a user visible change, such as fixing a reported bug,
 *        make sure you add an entry to doc/manual/user_ChangeLogImpl.xml.
 *
 *   <li> If you are adding files make sure set the right attributes.
 *        svn-ps.sh/cmd was created for this purpose, please make use of it.
 *
 * </ul>
 *
 * After checking in:
 *
 * <ul>
 *
 *   <li> After checking-in, you watch Tinderbox until your check-ins clear. You do not
 *        go home. You do not sleep. You do not log out or experiment with drugs. You do
 *        not become unavailable. If you break the tree, add a comment saying that you're
 *        fixing it. If you can't fix it and need help, ask in the \#innotek channel or back
 *        out the change.
 *
 * </ul>
 *
 * (Inspired by mozilla tree rules.)
 *
 *
 */

