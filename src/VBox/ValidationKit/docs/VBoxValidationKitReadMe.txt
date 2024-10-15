
The VirtualBox Validation Kit
=============================


Introduction
------------

The VirtualBox Validation Kit is our new public tool for doing automated
testing of VirtualBox.  We are continually working on adding new features
and guest operating systems to our battery of tests.

We warmly welcome contributions, new ideas for good tests and fixes.


Directory Layout
----------------

./docs/
    The documentation for the test suite mostly lives here, the exception being
    readme.txt files that are better off living near what they concern.

    For a definition of terms used here, see the Definitions / Glossary section
    of ./docs/AutomaticTestingRevamp.txt / ./docs/AutomaticTestingRevamp.html.

./testdriver/
    Python module implementing the base test drivers and supporting stuff.
    The base test driver implementation is found in ./testdriver/base.py while
    the VBox centric specialization is in ./testdriver/vbox.py. Various VBox
    API wrappers that makes things easier to use and glosses over a lot of API
    version differences that live in ./testdriver/vboxwrappers.py.

    Test VM collections are often managed thru ./testdriver/vboxtestvms.py, but
    doesn't necessarily have to be, it's up to the individual test driver.

    For logging, reporting result, uploading useful files and such we have a
    reporter singleton sub-package, ./testdriver/reporter.py.  It implements
    both local (for local testing) and remote (for testboxes + test manager)
    reporting.

    There is also a VBoxTXS client implementation in txsclient.py and a stacked
    test driver for installing VBox (vboxinstaller.py).  Most test drivers will
    use the TXS client indirectly thru vbox.py methods.  The installer driver
    is a special trick for the testbox+testmanager setup.

./tests/
    The python scripts driving the tests.  These are organized by what they
    test and are all derived from the base classes in ./testdriver (mostly from
    vbox.py of course).  Most tests use one or more VMs from a standard set of
    preconfigured VMs defined by ./testdriver/vboxtestvms.py (mentioned above),
    though the installation tests used prepared ISOs and floppy images.

./vms/
    Text documents describing the preconfigured test VMs defined by
    ./testdrive/vboxtestvms.py.  This will also contain description of how to
    prepare installation ISOs when we get around to it (soon).

./utils/
    Test utilities and lower level test programs, compiled from C, C++ and
    Assembly mostly.  Generally available for both host and guest, i.e. in the
    zip and on the VBoxValidationKit.iso respectively.

    The Test eXecution Service (VBoxTXS) found in ./utils/TestExecServ is one
    of the more important utilities.  It implements a remote execution service
    for running programs/tests inside VMs and on other test boxes. See
    ./utils/TestExecServ/vboxtxs-readme.txt for more details.

    A simple network bandwidth and latency test program can be found in
    ./utils/network/NetPerf.cpp.

./bootsectors/
    Boot sector test environment.  This allows creating floppy images in
    assembly that tests specific CPU or device behavior.  Most tests can be
    put on a USB stick, floppy or similar and booted up on real hardware for
    comparison.  All floppy images can be used for manual testing by developers
    and most will be used by test drivers (./tests/*/td*.py) sooner or later.

    The boot sector environment is heavily bound to yasm and it's ability to
    link binary images for single assembly input units.  There is a "library"
    of standard initialization code and runtime code, which include switch to
    all (well V8086 mode is still missing, but we'll get that done eventually)
    processor modes and paging modes.  The image specific code is split into
    init/driver code and test template, the latter can be instantiated for each
    process execution+paging mode.

./common/
    Python package containing common python code.

./testboxscript/
    The testbox script.  This is installed on testboxes used for automatic
    testing with the testmanager.

./testmanager/
    The VirtualBox Test Manager (server side code).  This is written in Python
    and currently uses postgresql as database backend for no particular reason
    other than that it was already installed on the server the test manager was
    going to run on.  It's relatively generic, though there are of course
    things in there that are of more use when testing VirtualBox than other
    things.  A more detailed account (though perhaps a little dated) of the
    test manager can be found in ./docs/AutomaticTestingRevamp.txt and
    ./docs/AutomaticTestingRevamp.html.

./testanalysis/
    A start a local test result analysis, comparing network test output.  We'll
    probably be picking this up again later.

./snippets/
    Various code snippets that may be turned into real tests at some point.



:Status: $Id: VBoxValidationKitReadMe.txt 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
:Copyright: Copyright (C) 2010-2024 Oracle Corporation.
