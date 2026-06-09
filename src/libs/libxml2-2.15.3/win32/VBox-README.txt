In order to reconfigure the Windows build, one needs to do:

  1) back up ../xmlversion.h
  2) run ./configure.js <options>

and then do:

  1) move the generated file ../xmlversion.h to the ../include/ directory
     and restore the original ../xmlversion.h from the backup copy
  2) delete the file ../config.h copied from ../include/win32config.h since
     the latter is included directly from ../libxml.h anyway
  3) delete ./Makefile since it is not used by kBuild anyway
