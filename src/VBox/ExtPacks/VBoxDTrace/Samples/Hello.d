/* $Id: Hello.d 97465 2015-01-02 12:40:55Z knut.osmundsen@oracle.com $ */
/** @file
 * VBoxDTrace - Hello world sample.
 */


/* This works by matching the dtrace:::BEGIN probe, printing the greeting and
   then quitting immediately. */
BEGIN {
    trace("Hello VBox World!");
    exit(0);
}

