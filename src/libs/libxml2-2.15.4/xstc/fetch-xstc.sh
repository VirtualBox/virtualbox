#!/bin/sh

set -e

# Additional tests for runsuite

URL=http://www.w3.org/XML/2004/xml-schema-test-suite

mkdir -p Tests

TARBALL_2=xsts-2004-01-14.tar.gz
NISTTESTDEF_2=NISTXMLSchemaDatatypes.testSet
curl -LJO $URL/xmlschema2004-01-14/$TARBALL_2
tar -xzf $TARBALL_2 Tests/Datatypes Tests/Metadata/$NISTTESTDEF_2
rm $TARBALL_2

TARBALL=xsts-2002-01-16.tar.gz
MSTESTDEF=MSXMLSchema1-0-20020116.testSet
SUNTESTDEF=SunXMLSchema1-0-20020116.testSet
NISTTESTDEF=NISTXMLSchema1-0-20020116.testSet
curl -LJO $URL/xmlschema2002-01-16/$TARBALL
tar -C Tests -xzf $TARBALL \
    xmlschema2002-01-16/suntest \
    xmlschema2002-01-16/msxsdtest \
    xmlschema2002-01-16/$MSTESTDEF \
    xmlschema2002-01-16/$SUNTESTDEF
if [ -d Tests/suntest ] ; then rm -r Tests/suntest ; fi
if [ -d Tests/msxsdtest ] ; then rm -r Tests/msxsdtest ; fi
mv Tests/xmlschema2002-01-16/* Tests
mv Tests/*.testSet Tests/Metadata
rm -r Tests/xmlschema2002-01-16
rm $TARBALL
