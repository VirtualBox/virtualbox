@echo "$Id: autoexec-testbox.cmd 96152 2014-09-17 14:51:43Z noreply@oracle.com $"
@echo on
%SystemDrive%\Python27\python.exe %SystemDrive%\testboxscript\testboxscript\testboxscript.py --testrsrc-server-type=cifs --builds-server-type=cifs
pause

