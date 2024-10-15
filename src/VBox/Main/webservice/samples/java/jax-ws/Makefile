# $Id: Makefile 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Makefile for java samples.
#


#
# Copyright (C) 2008-2024 Oracle and/or its affiliates.
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use,
# copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following
# conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
# OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#

JAVA16=java
JAVA15=/usr/lib/jvm/java-1.5.0-sun/bin/java
JAVAC = javac
JAVAC15 = javac -target 1.5
JAVAC16 = javac -target 1.6
MKDIR = mkdir
RM = rm

DEST16 = ./gen16
DEST15 = ./gen15

VBOXWS15 = ../lib/vboxws_java15.jar
VBOXWS16 = ../lib/vboxws_java16.jar

JAXWS=./jaxws-ri
JAXWSLIBS=$(JAXWS)/lib/jaxws-api.jar:$(JAXWS)/lib/jaxb-api.jar:$(JAXWS)/lib/jsr181-api.jar:$(JAXWS)/lib/jaxws-rt.jar

all: run16

metric: metric16

$(DEST16)/clienttest.class: clienttest.java
	$(MKDIR) -p $(DEST16)
	$(JAVAC16) -d $(DEST16) -cp $(VBOXWS16) $<

$(DEST15)/clienttest.class: clienttest.java
	$(MKDIR) -p $(DEST15)
	$(JAVAC15) -d $(DEST15) -cp $(VBOXWS15):$(JAXWSLIBS) $<

run16: $(DEST16)/clienttest.class
	$(JAVA16) -cp $(VBOXWS16):$(DEST16) clienttest show vms

run15: $(DEST15)/clienttest.class
	$(JAVA15) -cp $(VBOXWS15):$(JAXWSLIBS):$(DEST15) clienttest show vms

$(DEST16)/metrictest.class: metrictest.java
	$(MKDIR) -p $(DEST16)
	$(JAVAC16) -d $(DEST16) -cp $(VBOXWS16) $<

$(DEST15)/metrictest.class: metrictest.java
	$(MKDIR) -p $(DEST15)
	$(JAVAC15) -d $(DEST15) -cp $(VBOXWS15):$(JAXWSLIBS) $<

metric16: $(DEST16)/metrictest.class
	-$(JAVA16) -cp $(VBOXWS16):$(DEST16) metrictest

metric15: $(DEST15)/metrictest.class
	-$(JAVA15) -cp $(VBOXWS15):$(JAXWSLIBS):$(DEST15) metrictest

clean:
	$(RM) -rf $(DEST15)  $(DEST16)

