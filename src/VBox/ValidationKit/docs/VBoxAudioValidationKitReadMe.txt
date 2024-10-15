Audio Testing of VirtualBox
===========================


Overview / Goal
---------------

The goal is to create a flexible testing framework to test the
VirtualBox audio stack.

It should be runnable with an easy-to-use setup so that also regular users
can perform tests on request, without having to install or set up additional
dependencies.

That framework must be runnable on all host/guest combinations together with all
audio drivers ("backends") and device emulations being offered. This makes it a
rather big testing matrix which therefore has to be processed in an automated
fashion.

Additionally it should be flexible enough to add more (custom) tests later on.


Operation
---------

The framework consists of several components which try to make use as much of
the existing audio stack code as possible. This allows the following
operation modes:

Standalone
  Playing back / recording audio data (test tones / .WAV files) in a
  standalone scenario, i.e. no VirtualBox / VMs required). This mode is using
  VirtualBox' audio (mixing) stack and available backend drivers without the
  need of VirtualBox being installed.

Manual
  Performing single / multiple tests manually on a local machine.
  Requires a running and set up test VM.

Automated
  Performs single / multiple tests via the Validation Kit audio test
  driver and can be triggered via the Validation Kit Test Manager.

(Re-)validation of previously ran tests
  This takes two test sets and runs the validation / analysis on them.

Self testing mode
  Performs standalone self tests to verify / debug the involved components.


Components and Terminology
--------------------------

The following components are in charge for performing the audio tests
(depends on the operation mode, see above):

- VBoxAudioTest (also known as VKAT, "Validation Kit Audio Test"):
  A binary which can perform the standalone audio tests mentioned above, as well
  as acting as the guest and host service(s) when performing manual or automated
  tests. It also includes the analysis / verification of audio test sets.
  VKAT also is included in host installations and Guest Additions since
  VirtualBox 7.0 to give customers and end users the opportunity to test and
  verify the audio stack.

  Additional features include:
    * Automatic probing of audio backends ("--probe-backends")
    * Manual playback of test tones ("play -t")
    * Manual playback of .WAV files ("play <WAV-File>")
    * Manual recording to .WAV files ("recording <WAV-File>")
    * Manual device enumeration (sub command "enum")
    * Manual (re-)verification of test sets (sub command "verify")
    * Self-contained self tests (sub command "selftest")

  See the syntax help ("--help") for more.

- ATS ("Audio Testing Service"): Component which is being used by 1 and the
  Validation Kit audio driver (backend) to communicate across guest and host
  boundaries. Currently using a TCP/IP transport layer. Also works with VMs
  which are configured with NAT networking ("reverse connection").

- Validation Kit audio test driver (tdAudioTest.py): Used for integrating and
  invoking VKAT for manual and automated tests via the Validation Kit framework
  (Test Manager). Optional. The test driver can be found at [1]_.

- Validation Kit audio driver (backend): A dedicated audio backend which
  communicates with VKAT running on the same host to perform the actual audio
  tests on a VirtualBox installation. This makes it possible to test the full
  audio stack on a running VM without any additional / external tools.

  On guest playback, data will be recorded, on guest recording, data will be
  injected from the host into the audio stack.

- Test sets contain
    - a test manifest with all information required (vkat_manifest.ini)
    - the generated / captured audio data (as raw PCM)

  and are either packed as .tar.gz archives or consist of a dedicated directory
  per test set.

  There always must be at least two test sets - one from the host side and one
  from the guest side - to perform a verification.

  Each test set contains a test tag so that matching test sets can be
  identified.

The above components are also included in VirtualBox release builds and can be
optionally enabled (disabled by default).

.. [1] src/VBox/ValidationKit/tests/audio/tdAudioTest.py


Setup instructions
------------------

- VM needs to be configured to have audio emulation and audio testing enabled
  (via extra-data, set "VBoxInternal2/Audio/Debug/Enabled" to "true").
- Audio input / output for the VM needs to be enabled (depending on the test).
- Start VBoxAudioTest on the guest, for example:

  VBoxAudioTest test --mode guest --tcp-connect-address 10.0.2.2

  Note: VBoxAudioTest is included with the Guest Additions starting at
        VirtualBox 7.0.
  Note: Depending on the VM's networking configuration there might be further
        steps necessary in order to be able to reach the host from the guest.
        See the VirtualBox manual for more information.


Performing a manual test
------------------------

- Follow "Setup instructions".
- Start VBoxAudioTest on the host with selected test(s), for example:

  VBoxAudioTest test --mode host

    Note: VBoxAudioTest is included with the VirtualBox 7.0 host installers and
          will be installed by default.

- By default the test verification will be done automatically after running the
  tests.


Advanced: Performing manual verification
----------------------------------------

VBoxAudioTest can manually be used with the "verify" sub command in order to
(re-)verify previously generated test sets. It then will return different exit
codes based on the verification result.


Advanced: Performing an automated test
--------------------------------------

- TxS (Test E[x]ecution Service) has to be up and running (part of the
  Validation Kit) on the guest.
- Invoke the tdAudioTest.py test driver, either manually or fully automated
  via Test Manager.


Internals: Workflow for a single test
-------------------------------------

When a single test is being executed on a running VM, the following (simplified)
workflow applies:

- VKAT on the host connects to VKAT running on the guest (via ATS, also can be a
  remote machine in theory).
- VKAT on the host connects to Validation Kit audio driver on the host
  (via ATS, also can be a remote machine in theory).
- For example, when doing playback tests, VKAT on the host ...
    * ... tells the Validation Kit audio driver to start recording
          guest playback.
    * ... tells the VKAT on the guest to start playing back audio data.
    * ... gathers all test data (generated from/by the guest and recorded from
          the host) as separate test sets.
    * ... starts verification / analysis of the test sets.


Current status / limitations
----------------------------

- The following test types are currently implemented:
    * Test tone (sine wave) playback from the guest
    * Test tone (sine wave) recording by the guest (injected from the host)
- Only the HDA device emulation has been verified so far.
- Only the ALSA audio stack on Debian 10 has been verified so far.
  Note: This is different from PulseAudio using the ALSA plugin!


Troubleshooting
---------------

- Make sure that audio device emulation is enabled and can be used within the
  guest. Also, audio input / output has to be enabled, depending on the tests.
- Make sure that the guest's VBoxAudioTest's instance can reach the host via
  the selected transport layer (TCP/IP by default).
- Increase the hosts audio logging level
  (via extra-data, set "VBoxInternal2/Audio/Debug/Level" to "5").
- Increase VBoxAudioTest's verbosity level (add "-v", can be specified
  multiple times).
- Check if the VBox release log contains any warnings / errors with the
  "ValKit:" prefix.


:Status: $Id: VBoxAudioValidationKitReadMe.txt 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
:Copyright: Copyright (C) 2021-2024 Oracle Corporation.
