$Id: readme.txt 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $


Preparations:

0. Make sure the computer name (what hostname prints) is the same as the DNS 
   returns (sans domain) for the host IP.

1. Install Python 2.7.x from python.org to C:\Python27 or Python 3.y.x to 
   C:\Python3%y%,  where y >= 5.  Matching bit count as the host windows version.

2. Install the win32 extension for python.

3. Append C:\Python27 or C:\Python3%y% to the system PATH (tail).

4. Disable UAC.

   Windows 8 / 8.1 / Server 2012: Set the following key to zero:
   "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\policies\system\EnableLUA"

5. Disable Automatic updates. (No rebooting during tests, thank you!)

   Ideally we would prevent windows from even checking for updates to avoid
   influencing benchmarks and such, however the microsofties aren't keen on it.
   So, disable it as much as possible.

   W10: gpedit.msc -> "Administrative Templates" -> "Windows Components"
   -> "Windows Update":
     - "Configure Automatic Updates": Enable and select "2 - Notify for
       download and notiy for install".
     - "Allow Automatic Updates immediate installation": Disable.
     - "No auto-restart with logged on users for scheduled automatic
       updates installations": Enabled.

6. Go to the group policy editor (gpedit.msc) and change "Computer Configuration"
   -> "Windows Settings" -> "Security Settings" -> "Local Policies"
   -> "Security Options" -> "Network security: LAN Manager authentication level"
   to "Send LM & NTLM- use NTLMv2 session security if negotiated".  This fixed
   passing the password as an argument to "NET USE" (don't ask why!).

6b. While in the group policy editor, make sure that  "Computer Configuration"
   -> "Windows Settings" -> "Security Settings" [ -> "Local Policies" ]
   -> "Account Policy" -> "Password must meet complexity requirements" is
   disabled so the vbox account can be created later one.

7. Need to disable the error popups blocking testing.

   Set "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Windows\ErrorMode"
   to 2.  This immediately disables hard error popups (missing DLLs and such).

   Then there are the sending info to microsoft, debug, dump, look for solution
   questions we don't want.  Not entirely sure what's required here yet, but
   the following stuff might hopefully help (update after testing):

   On Windows XP:

   Go "Control Panel" -> "System Properties" -> "Advanced"
   -> "Error Reporting" and check "Disable error reporting"
   and uncheck "But notify me when critical erorr occurs".

   On Windows Vista and later:

   In gpedit change the following settings under "Computer Configuration"
   -> "Administrative Templates" ->  "Windows Components"
   -> "Windows Error Reporting":
        1) Enable "Prevent display of the user interface for critical errors".
   ... -> "Advanced Error Reporting Settings":
        1) Enable "Configure Report Archive" and set it to "Store All" for
           up to 500 (or less) reports.
        2) Disable "Configure Report Queue".

   Run 'serverWerOptin /disable'.

   Then set "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\DontShowUI"
   to 1. (Could do all the above from regedit if we wanted...)

7b. Configure application crash dumps on Vista SP1 and later:

   Set the following values under the key
   HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps:
        DumpFolder     [string] = C:\CrashDumps
        DumpCount       [dword] = 10
        DumpType        [dword] = 1  (minidump)
        CustomDumpFlags [dword] = 0

   mkdir C:\CrashDumps

   See also http://msdn.microsoft.com/en-us/library/windows/desktop/bb787181%28v=vs.85%29.aspx

7c. Enable verbose driver installation logging (C:\Windows\setupapi.dev.log):

   Create the following value under the key
   HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Setup\
        LogLevel        [dword] = 0xFF (255)

   If it already exists (typical on W10), just OR 0xff into the existing value.

8. Install firefox or chrome, download the latest testboxscript*.zip from
   the build box. If the testbox is very short on disk space, i.e. less than
   15GB free disk space after installing Windows Updates, install ImDisk 2.0.9
   or later from e.g. http://www.ltr-data.se/opencode.html/

9. Create a user named "vbox" with password "password".  Must be an
   Administrator user!

10. Configure user "vbox" to log in automatically via "control userpasswords2".

11. Open up the port ranges 6000-6100 (VRDP) for TCP traffic and 5000-5032
    (NetPerf) for both TCP and UDP traffic in the Windows Firewall.
    From the command line (recommended in vista):
        for /L %i in (6000,1,6100) do netsh firewall add portopening TCP %i "VRDP %i"
        for /L %i in (5000,1,5032) do netsh firewall add portopening TCP %i "NetPerf %i TCP"
        for /L %i in (5000,1,5032) do netsh firewall add portopening UDP %i "NetPerf %i UDP"
        netsh firewall set icmpsetting type=ALL

11b. Set a hostname which the test script can resolve to the host's IP address.

12. Setup time server to "wei01-time.de.oracle.com" and update date/time.

13. Activate windows. "https://linserv.de.oracle.com/vbox/wiki/MSDN Volume License Keys"

14. Windows 2012 R2: If you experience mouse pointer problems connecting with rdesktop,
    open the mouse pointer settings and disable mouse pointer shadow.

15. Enable RDP access by opening "System Properties" and selecting "Allow
    remote connections to this computer" in the "Remote" tab.  Ensure that
    "Allow connections only from computers running Remote Desktop with Network
    Level Authentication" is not checked or rdesktop can't access it.

    W10: Make old rdesktop connect:
         \HKLM\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RDP-Tcp\SecurityLayer
         Change DWORD Hex '2' -> '1'

15b. While you're in "System Properties", in the "Hardware" tab, button
    "Driver Signing" tell it to ignore logo testing requirements.

    W10: Doesn't exist any more.

The install (as user vbox):

16. Disable loading CONIME. Set "HKEY_CURRENT_USER\Console\LoadConIme" to 0.

17. Unzip (/ copy) the content of the testboxscript-*.zip to C:\testboxscript.

18. Copy C:\testboxscript\testboxscript\win\autoexec-testbox.cmd to C:\.

19. Create a shortcut to C:\autoexec-testbox.cmd and drag it into
    "Start" -> "All Programs" -> "Startup".

    W10: Find startup folder by hitting Win+R and entering "shell:startup".

20. If this is an Intel box and the CPU is capable of Nested Paging, edit C:\autoexec-testbox.cmd
    and append '--nested-paging'


That's currently it.

