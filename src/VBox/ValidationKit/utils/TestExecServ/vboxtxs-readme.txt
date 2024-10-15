$Id: vboxtxs-readme.txt 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $


VirtualBox Test eXecution Service
=================================

This readme briefly describes how to install the Test eXecution Service (TXS)
on the various systems.

There are currently two transport options for the TXS:

  - The default is to use it in TCP server mode, i.e. the test script needs
    to know the guest's IP and therefore requires guest additions to be
    installed as well.  (Please use the latest stable additions compatible with
    the VBox host versions you intend to test.)

  - The alternative is for NATted setups where TXS will act like a TCP client
    and try connect to the test script on the host.  Since this require that
    TXS knows which IP to connect to, it's only really possible in a NATted
    setup where we know the host IP is 10.0.2.2.

Since r85596 TXS operates in both modes by default so the nat version of
the init scripts is not required anymore. Instead the other type can be installed
for both cases.

Linux Installation
------------------

1.   mkdir -p /opt/validationkit
2.   scp/download VBoxValidationKit*.zip there.
3.   unzip VBoxValidationKit*.zip
4.   chmod -R u+w,a+x /opt/validationkit/ && chown -R root.root /opt/
5.   cd /etc/init.d/

6 a) For init.rc distros:
     Link up the right init script (see connection type above):
       nat)   ln -s ../../opt/validationkit/linux/vboxtxs-nat ./vboxtxs
       other) ln -s ../../opt/validationkit/linux/vboxtxs     ./vboxtxs
6 b) Add vboxtxs to runlevels 2, 3, 5 and any other that makes sense
     on the distro. There is usually some command for doing this, e.g.
       ```update-rc.d vboxtxs defaults && update-rc.d vboxtxs enable``` (Debian-based)
    or
       ```chkconfig --add vboxtxs``` (OL/RHEL)

  ... or ...

7 a) For systemd distros: Link/copy up the vboxtxs.system to [/usr]/lib/systemd/, e.g.
       cp /opt/validationkit/linux/vboxtxs.service /etc/systemd/system
  b) Enable the vboxtxs service via:
       systemctl enable vboxtxs

  For all distros again:

8a.  Check the CD-ROM location (--cdrom <path>) in vboxtxs and fix it so it's correct, make sure
     to update in svn as well.
8b.  Optional: If no suitable CD-ROM location is available on the guest yet, do a:
    mkdir -p /media/cdrom; vi /etc/fstab
     and enter this in /etc/fstab:
    /dev/sr0<tab>/media/cdrom<tab>udf,iso9660<tab>user,noauto,exec,utf8<tab>0<tab>0
8c.  Optional: If SELinux denies execution of TXS, make sure to allow this, based on
     how the distribution handles SELinux exceptions. Often there even is a GUI for that
     (e.g. Oracle Linux 8+).
9.   Make sure that the package sources are still valid and up to date (apt / yum / ++)
10.  reboot / done.
11.  Do test.


OS/2 Installation
--------------------

1. Start an "OS/2 Window" ("OS/2 System" -> "Command Prompts")
2. md C:\Apps
3. cd C:\Apps
4. Mount the validationkit iso.
5. copy D:\os2\x86\* C:\Apps
5. copy D:\os2\x86\libc*.dll C:\OS2\DLL\
6. Open C:\startup.cmd in an editor (tedit.exe for instance or e.exe).
7. Add the line "start /C C:\Apps\TestExecService.exe --foreground" at the top of the file.
8. reboot / done
9. Do test.


Solaris Installation
--------------------

1. Start the guest and open a root console.
2. mkdir -p /opt/VBoxTest
3. cd /opt/VBoxTest
4. scp/download VBoxValidationKit*.zip there.
5. unzip VBoxValidationKit*.zip
6. chmod -R u+w,a+x /opt/VBoxTest/
7. Import the right service setup depending on the Solaris version:
      <= 10u9) /usr/sbin/svccfg import /opt/VBoxTest/validationkit/solaris/vboxtxs-sol10.xml
      >= 11.0) /usr/sbin/svccfg import /opt/VBoxTest/validationkit/solaris/vboxtxs.xml
8. /usr/sbin/svcadm enable svc:/system/virtualbox/vboxtxs
9. reboot / done.

To remove the service before repeating steps 7 & 8:
1. /usr/sbin/svcadm disable -s svc:/system/virtualbox/vboxtxs:default
2. /usr/sbin/svccfg delete svc:/system/virtualbox/vboxtxs:default

Note. To configure dhcp for more a new interface the files
/etc/hostname.<if#X> and /etc/dhcp.<ifnm#> have to exist.  If you want the VM
to work with any network card you throw at it, create /etc/*.pcn[01] and
/etc/*.e1000g[012] as Solaris will remember it has seen the other variants
before and use a different instance number (or something to that effect).


Windows Installation
--------------------

1. Log on as Administrator.
2. Make sure you have set a secure password, which you'll need in step 9.
3. Start CMD.EXE or equivalent.
4. md C:\Apps
5. cd C:\Apps
6. Mount the validationkit iso.
7. copy D:\win\* C:\Apps
8. copy D:\win\<x86 or amd64>\* C:\Apps
9. Put the password from step 2 into the right service setup (see connection
   type above) and import it into the registry:
     nat)   start C:\Apps\vboxtxs-nat.reg
     other) start C:\Apps\vboxtxs.reg
10. Add vboxtxs in task scheduler and create an exception for windows defender
    by running the script (use connection type from step 9) as Administrator:
      nat)   C:\Apps\vboxtxs-add-task-nat.cmd
      other) C:\Apps\vboxtxs-add-task.cmd
11. Make sure that the CD-ROM location is assigned to D: (via "Disk Management").
12. reboot / done
13. Do test.

NT 3.1 and 3.x tricks:
- Make sure the file system is NTFS.  Observed issues converting 2GB partitions,
  more success with smaller.
- For NT3.1 PCNET drivers can be found on the net.  No DHCP, so NAT only with
  IP 10.0.2.15, 10.0.2.2 as gateway, and 10.0.2.3 as DNS with --natdnsproxy1 on.
- On NT3.1 you need to add SystemDrive=C: to the environment.
- Need to perform registry edits manually.
- Use startup folder instead of non-exising Windows/Run key.


Testing the setup
-----------------

1. Make sure the validationkit.iso is inserted.
2. Boot / reboot the guest.
3. Depending on the TXS transport options:
      nat)   python testdriver/tst-txsclient.py --reversed-setup
      other) python testdriver/tst-txsclient.py --hostname <guest-ip>
