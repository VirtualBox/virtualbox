' $Id: envSub.vbs 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
''  @file
' VBScript worker for env.cmd
'

'
' Copyright (C) 2006-2024 Oracle and/or its affiliates.
'
' This file is part of VirtualBox base platform packages, as
' available from https://www.virtualbox.org.
'
' This program is free software; you can redistribute it and/or
' modify it under the terms of the GNU General Public License
' as published by the Free Software Foundation, in version 3 of the
' License.
'
' This program is distributed in the hope that it will be useful, but
' WITHOUT ANY WARRANTY; without even the implied warranty of
' MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
' General Public License for more details.
'
' You should have received a copy of the GNU General Public License
' along with this program; if not, see <https://www.gnu.org/licenses>.
'
' SPDX-License-Identifier: GPL-3.0-only
'


''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
'  Header Files
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
''
' Includes a vbscript file relative to the script.
sub IncludeFile(strFilename)
   dim objFile, objFileSys
   set objFileSys = WScript.CreateObject("Scripting.FileSystemObject")
   dim strPath : strPath = objFileSys.BuildPath(objFileSys.GetParentFolderName(Wscript.ScriptFullName), strFilename)
   set objFile = objFileSys.openTextFile(strPath)
   executeglobal objFile.readAll()
   objFile.close
   set objFileSys = nothing
end sub

IncludeFile "win\vbscript\helpers.vbs"


''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
'  Global Variables                                                                                                              '
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
dim g_cntVerbose
g_cntVerbose = 1


sub LogPrint(str)
   if g_cntVerbose > 1 then
      WScript.StdErr.WriteLine "debug: " & str
   end if
end sub

sub DbgPrint(str)
   if g_cntVerbose > 2 then
      WScript.StdErr.WriteLine "debug: " & str
   end if
end sub


''
' The main() like function.
'
function Main()
   Main = 1

   '
   ' check that we're not using wscript.
   '
   if UCase(Right(Wscript.FullName, 11)) = "WSCRIPT.EXE" then
      Wscript.Echo "This script must be run under CScript."
      exit function
   end if
   SelfTest

   '
   ' Get our bearings.
   '
   dim strScriptDir
   strScriptDir = g_objFileSys.GetParentFolderName(Wscript.ScriptFullName)

   dim strRootDir
   strRootDir = g_objFileSys.GetParentFolderName(strScriptDir)

   dim strRealArch
   strRealArch = Trim(EnvGet("PROCESSOR_ARCHITEW6432"))
   if strRealArch = "" then strRealArch = Trim(EnvGet("PROCESSOR_ARCHITECTURE"))
   if strRealArch = "" then strRealArch = "amd64"
   strRealArch = LCase(strRealArch)
   if strRealArch <> "amd64" and strRealArch <> "x86" and strRealArch <> "arm64" then
      MsgError "Unsupported host architecture: " & strRealArch ' quits
   end if

   '
   ' Guest the default configuration.
   '
   dim arrTypes          : arrTypes          = Array("debug", "release", "profile", "strict", "dbgopt")
   dim arrTargetAndHosts : arrTargetAndHosts = Array("win", "linux", "solaris", "darwin", "os2", "freebsd")
   dim arrArchitectures  : arrArchitectures  = Array("x86", "amd64", "arm32", "arm64", "sparc32", "sparc64")

   dim strType
   strType        = EnvGetDefValid("KBUILD_TYPE",        "debug", arrTypes)

   dim strPathDevTools
   strPathDevTools= EnvGetDef("KBUILD_DEVTOOLS",    g_objFileSys.BuildPath(strRootDir, "tools"))

   dim strPathkBuild
   strPathkBuild  = EnvGetDef("KBUILD_PATH",        g_objFileSys.BuildPath(strRootDir, "kBuild"))

   dim strTarget, strTargetArch
   strTarget      = EnvGetDefValid("KBUILD_TARGET",      "win", arrTargetAndHosts)
   strTargetArch  = EnvGetDefValid("KBUILD_TARGET_ARCH", strRealArch, arrArchitectures)

   dim strHost, strHostArch
   strHost        = EnvGetDefValid("KBUILD_HOST",        "win", arrTargetAndHosts)
   strHostArch    = EnvGetDefValid("KBUILD_HOST_ARCH",   strRealArch, arrArchitectures)

   '
   ' Parse arguments.
   '
   dim arrValueOpts   : arrValueOpts   = Array("--type", "--arch", "--tmp-script")
   dim arrCmdToExec   : arrCmdToExec   = Array()
   dim blnDashDash    : blnDashDash    = false
   dim strChdirTo     : strChdirTo     = strRootDir
   dim strTmpScript   : strTmpScript   = g_objFileSys.BuildPath(strScriptDir, "envtmp.cmd")
   dim i              : i              = 0
   do while i < Wscript.Arguments.Count
      dim strArg, strValue, off
      strArg = Wscript.Arguments.item(i)
      i = i + 1
      if blnDashDash <> true then
         ' Does it have an embedded value?  Does it take a value?
         off = InStr(2, strArg, "=")
         if off <= 0 then off = InStr(2, strArg, ":")
         if off > 0 then
            strValue = Mid(strArg, off + 1)
            strArg   = Left(strArg, off - 1)
            if not ArrayContainsString(arrValueOpts, strArg) then
               MsgSyntaxError "'" & strArg & "' does not take a value"     ' quits
            end if
         elseif ArrayContainsString(arrValueOpts, strArg) then
            if i >= Wscript.Arguments.Count then
               MsgSyntaxError "'" & strArg & "' takes a value"             ' quits
            end if
            strValue = Wscript.Arguments.item(i)
            i = i + 1
         end if

         ' Process it.
         select case strArg
            ' Build types:
            case "--type"
               if not ArrayContainsString(arrTypes, strValue) then
                  MsgSyntaxError "Invalid build type '" & strValue & "'.  Valid types: " & ArrayJoinString(arrTypes, ", ") ' quits
               else
                  strType = strValue
               end if
            case "--release"
               strType = "release"
            case "--debug"
               strType = "debug"
            case "--strict"
               strType = "strict"
            case "--dbgopt"
               strType = "dbgopt"

            ' Target architecture:
            case "--arch"
               if not ArrayContainsString(arrArchitectures, strValue) then
                  MsgSyntaxError "Invalid target architecture'" & strValue & "'.  Valid ones: " _
                                 & ArrayJoinString(arrArchitectures, ", ") ' quits
               else
                  strTargetArch = strValue
               end if
            case "--amd64"
               strTargetArch = "amd64"
            case "--x86"
               strTargetArch = "amd64"
            case "--arm32"
               strTargetArch = "arm32"
            case "--arm64"
               strTargetArch = "amd64"

            ' Verbosity, env.sh compatibility and stuff
            case "--quiet"
               g_cntVerbose = 0
            case "--verbose"
               g_cntVerbose = g_cntVerbose + 1
            case "--chdir"
               strChdirTo = strValue
            case "--no-chdir"
               strChdirTo = ""

            ' Internal.
            case "--tmp-script"
               strTmpScript = strValue

            ' Standard options
            case "-h", "-?", "--help"
               Print "Sets the VBox development shell environment on Windows."
               Print "usage: env.cmd [--type <type> | --release | --debug | --strict | --dbgopt]"
               Print "               [--arch <arch> | --amd64 | --x86 | --arm32 | --arm64]"
               Print "               [--no-chdir | --chdir <dir>] [--quiet | --verbose]"
               Print "               [--] [prog] [args...]"
               Print "usage: env.cmd [--help | -h | -?]"
               Print "usage: env.cmd [--version | -V]"
               Main = 0
               exit function
            case "-V", "--version"
               Print "x.y"
               Main = 0
               exit function

            case "--"
               blnDashDash = True

            case else
               MsgSyntaxError "Unknown option: " & strArg
         end select
      else
         ' cscript may eat quoting... So we should consider using some windows API to get the raw command line
         ' and look for the dash-dash instead.  Maybe.
         arrCmdToExec = ArrayAppend(arrCmdToExec, strArg)
      end if
   loop

   '
   ' Set up the environment.
   '
   dim str1

   EnvSet "KBUILD_PATH",        UnixSlashes(strPathkBuild)
   EnvSet "KBUILD_DEVTOOLS",    UnixSlashes(strPathDevTools)
   EnvSet "KBUILD_TYPE",        strType
   EnvSet "KBUILD_TARGET",      strTarget
   EnvSet "KBUILD_TARGET_ARCH", strTargetArch
   EnvSet "KBUILD_HOST",        strHost
   EnvSet "KBUILD_HOST_ARCH",   strHostArch

   ' Remove legacy variables.
   dim arrObsolete
   arrObsolete = Array("BUILD_TYPE", "BUILD_TARGET", "BUILD_TARGET_ARCH", "BUILD_PLATFORM", "BUILD_PLATFORM_ARCH", _
                       "PATH_DEVTOOLS", "KBUILD_TARGET_CPU", "KBUILD_PLATFORM_CPU")
   for each str1 in arrObsolete
      EnvUnset str1
   next

   ' cleanup path before we start adding to it
   for each str1 in arrArchitectures
      EnvRemovePathItem "Path", DosSlashes(strPathkBuild & "\bin\win." & str1), ";"
      EnvRemovePathItem "Path", DosSlashes(strPathkBuild & "\bin\win." & str1 & "\wrappers"), ";"
      EnvRemovePathItem "Path", DosSlashes(strPathDevTools & "\win." & str1) & "\bin", ";"
   next

   '
   ' We skip the extra stuff like gnuwin32, windbg, cl.exe and mingw64 if
   ' there is a command to execute.
   '
   if ArraySize(arrCmdToExec) = 0 then
      ' Add the kbuild wrapper directory to the end of the path, these take
      ' precedence over the dated gnuwin32 stuff.
      EnvAppendPathItem "Path", DosSlashes(strPathkBuild & "\bin\win." & strHostArch & "\wrappers"), ";"

      ' Add some gnuwin32 tools to the end of the path.
      EnvAppendPathItem "Path", DosSlashes(strPathDevTools & "\win.x86\gnuwin32\r1\bin"), ";"

      ' Add the newest debugger we can find to the front of the path.
      dim strDir, blnStop
      bldExitLoop = false
      for each str1 in arrArchitectures
         strDir = strPathDevTools & "\win." & str1 & "\sdk"
         for each strSubDir in GetSubdirsStartingWithRVerSorted(strDir, "v")
            if FileExists(strDir & "\" & strSubDir & "\Debuggers\" & XlateArchitectureToWin(strHostArch) & "\windbg.exe") then
               EnvPrependPathItem "Path", DosSlashes(strDir & "\" & strSubDir & "\Debuggers\" & XlateArchitectureToWin(strHostArch)), ";"
               bldExitLoop = true
               exit for
            end if
         next
         if bldExitLoop then exit for
      next

      ' Add VCC to the end of the path.
      dim str2, strDir2, arrVccOldBinDirs
      arrVccOldBinDirs = Array("\bin\" & strHostArch & "_" & strTargetArch, "\bin\" & strTargetArch, "\bin")
      bldExitLoop = false
      for each str1 in Array("amd64", "x86")
         for each strDir in GetSubdirsStartingWithRVerSorted(strPathDevTools & "\win." & str1 & "\vcc", "v")
            strDir = strPathDevTools & "\win." & str1 & "\vcc\" & strDir
            if DirExists(strDir & "\Tools\MSVC") then
               for each strDir2 in GetSubdirsStartingWithRVerSorted(strDir & "\Tools\MSVC", "1")
                  strDir2 = strDir & "\Tools\MSVC\" & strDir2 & "\bin\Host" & XlateArchitectureToWin(strHostArch) _
                          & "\" & XlateArchitectureToWin(strTargetArch)
                  if FileExists(strDir2 & "\cl.exe") then
                     EnvAppendPathItem "Path", DosSlashes(strDir2), ";"
                     if strTargetArch <> strHostArch then
                        EnvAppendPathItem "Path", DosSlashes(PathStripFilename(strDir2) & "\" & XlateArchitectureToWin(strHostArch)), ";"
                     end if
                     bldExitLoop = true
                     exit for
                  end if
               next
            elseif DirExists(strDir & "\bin") then
               for each str2 in arrVccOldBinDirs
                  if FileExists(strDir & str2 & "\cl.exe") then
                     EnvAppendPathItem "Path", DosSlashes(strDir & str2), ";"
                     if str2 <> "\bin" then EnvAppendPathItem "Path", DosSlashes(strDir & "bin"), ";"
                     bldExitLoop = true
                     exit for
                  end if
               next
            end if
            if bldExitLoop then exit for
         next
         if bldExitLoop then exit for
      next

      ' Add mingw64 if it's still there.
      if strHostArch = "amd64" or strTargetArch = "amd64" then
         str1 = strPathDev & "win.amd64\mingw-64\r1\bin"
         if DirExists(str1) then EnvAppendPathItem "Path", DosSlashes(str1), ";"
      end if
   end if

   ' Add the output tools and bin directories to the fron of the path, taking PATH_OUT_BASE into account.
   dim strOutDir
   strOutDir = EnvGetDef("PATH_OUT_BASE", strRootDir & "\out")
   strOutDir = strOutDir & "\" & strTarget & "." & strTargetArch & "\" & strType
   EnvPrependPathItem "Path", DosSlashes(strOutDir & "\bin\tools"), ";"
   EnvPrependPathItem "Path", DosSlashes(strOutDir & "\bin"), ";"

   ' Add kbuild binary directory to the front the the path.
   EnvPrependPathItem "Path", DosSlashes(strPathkBuild & "\bin\win." & strHostArch), ";"

   ' Finally, add the relevant tools/**/bin directories to the front of the path.
   EnvPrependPathItem "Path", DosSlashes(strPathDevTools & "\bin"), ";"
   if strHostArch = "amd64" then EnvPrependPathItem "Path", DosSlashes(strPathDevTools & "\win.x86\bin"), ";"
   EnvPrependPathItem "Path", DosSlashes(strPathDevTools & "\win." & strHostArch) & "\bin", ";"

   '
   ' Export if we are not executing a program.
   '
   Main = g_rcScript
   if ArraySize(arrCmdToExec) = 0 then
      dim objTmpScript
      set objTmpScript = g_objFileSys.CreateTextFile(strTmpScript, true, false)
      objTmpScript.WriteLine

      for each str1 in Array("Path", "KBUILD_PATH", "KBUILD_DEVTOOLS", "KBUILD_TYPE", _
                             "KBUILD_TARGET", "KBUILD_TARGET_ARCH", "KBUILD_HOST", "KBUILD_HOST_ARCH")
         objTmpScript.WriteLine "SET " & str1 & "=" & EnvGet(str1)
      next
      for each str1 in arrObsolete
         if EnvExists(str1) then objTmpScript.WriteLine "SET " & str1 & "="
      next

      if strChdirTo <> "" then
         objTmpScript.WriteLine "CD """ & strChdirTo & """"
         if Mid(strChdirTo, 2, 1) = ":" then
            objTmpScript.WriteLine Left(strChdirTo, 2)
         end if
      end if

      objTmpScript.Close()
   '
   ' Run the specified program.
   '
   ' We must redirect stderr to stdout here, because vbscript doesn't seem to
   ' have any way to reuse the same console/stdout/stererr as we use (Exec
   ' creates two pipes, Run a new console), nor can vbscript service two
   ' TextStream/pipe objects at the same time without the risk of deadlocking
   ' with the child process (we read stdout, child waits for stderr space).
   '
   ' So, to make it work we use kmk_redirect.exe to stuff everything into stderr
   ' and ignore stdout.
   '
   else
      if strChdirTo <> "" then
         g_objShell.CurrentDirectory = strChdirTo
      end if

      ' Prepate the command line.
      dim strCmdLine, str
      strCmdLine = """" & DosSlashes(strPathkBuild) & "\bin\win." & strHostArch & "\kmk_redirect.exe"" -d1=2 -c0 -- " _
                 & """" & arrCmdToExec(0) & """"
      for i = 1 to UBound(arrCmdToExec)
         str = arrCmdToExec(i)
         if InStr(1, str, " ") > 0 then '' @todo There is more stuff that needs escaping
            strCmdLine = strCmdLine & " """ & str & """"
         else
            strCmdLine = strCmdLine & " " & str
         end if
      next

      ' Start it.
      if g_cntVerbose > 0 then MsgInfo "Executing command: " & strCmdLine
      dim objChild
      set objChild = g_objShell.Exec(strCmdLine)

      ' The fun output / wait.  As mention above, we only need to bother with stderr here.
      dim cMsSleepMin : cMsSleepMin = 8
      dim cMsSleepMax : cMsSleepMax = 92
      dim cMsSleep    : cMsSleep    = cMsSleepMin
      do while objChild.Status = 0
         if not objChild.StdErr.AtEndOfStream then       ' Seems this bugger might do a 0x80
            WScript.StdErr.WriteLine objChild.StdErr.ReadLine()
            cMsSleep = cMsSleepMin
         elseif objChild.Status = 0 then
            Wscript.Sleep cMsSleep
            ' We probably only end up here once stderr is closed/disconnected (i.e. never).
            ' This was originally written with the idea that AtEndOfStream would use
            ' PeekNamedPipe to see if there were anything to read, rather than block.
            ' Let's keep it for now.
            if cMsSleep < cMsSleepMax then cMsSleep = cMsSleep + 8
         end if
      loop

      ' Flush any remaining output on the offchance that we could get out of the above loop with pending output.
      WScript.StdErr.Write strStdErr & objChild.StdErr.ReadAll()
      WScript.StdOut.Write strStdOut & objChild.StdOut.ReadAll()

      ' Return the exit code to our parent.
      if g_cntVerbose > 0 then MsgInfo "Exit code = " & objChild.ExitCode
      Main = objChild.ExitCode
   end if
end function

'
' What crt0.o typically does:
'
WScript.Quit(Main())

