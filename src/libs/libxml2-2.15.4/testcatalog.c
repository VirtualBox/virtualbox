/*
 * testcatalog.c: C program to run libxml2 catalog.c unit tests
 *
 * To compile on Unixes:
 * cc -o testcatalog `xml2-config --cflags` testcatalog.c `xml2-config --libs` -lpthread
 *
 * See Copyright for the status of this software.
 *
 * Author: Daniel Garcia <dani@danigm.net>
 */


#include "libxml.h"
#include <stdio.h>

#ifdef LIBXML_CATALOG_ENABLED
#include <libxml/catalog.h>

/* Test catalog resolve uri with recursive catalog */
static int
testRecursiveDelegateUri(void) {
    int ret = 0;
    const char *cat = "test/catalogs/catalog-recursive.xml";
    const char *entity = "/foo.ent";
    xmlChar *resolved = NULL;

    xmlInitParser();
    xmlLoadCatalog(cat);

    /* This should trigger recursive error */
    resolved = xmlCatalogResolveURI(BAD_CAST entity);
    if (resolved != NULL) {
        fprintf(stderr, "CATALOG-FAILURE: Catalog %s entity should fail to resolve\n", entity);
        ret = 1;
    }
    xmlCatalogCleanup();

    return ret;
}

/* Test parsing repeated NextCatalog */
static int
testRepeatedNextCatalog(void) {
    int ret = 0;
    int i = 0;
    const char *cat = "test/catalogs/repeated-next-catalog.xml";
    const char *entity = "/foo.ent";
    xmlDocPtr doc = NULL;
    xmlNodePtr node = NULL;

    xmlInitParser();

    xmlLoadCatalog(cat);
    /* To force the complete recursive load */
    xmlCatalogResolveURI(BAD_CAST entity);
    /**
     * Ensure that the doc doesn't contain the same nextCatalog
     */
    doc = xmlCatalogDumpDoc();
    xmlCatalogCleanup();

    if (doc == NULL) {
        fprintf(stderr, "CATALOG-FAILURE: Failed to dump the catalog\n");
        return 1;
    }

    /* Just the root "catalog" node with a series of nextCatalog */
    node = xmlDocGetRootElement(doc);
    node = node->children;
    for (i=0; node != NULL; node=node->next, i++) {}
    if (i > 1) {
        fprintf(stderr, "CATALOG-FAILURE: Found %d nextCatalog entries and should be 1\n", i);
        ret = 1;
    }

    xmlFreeDoc(doc);

    return ret;
}

int
main(void) {
    int err = 0;

    err |= testRecursiveDelegateUri();
    err |= testRepeatedNextCatalog();

    return err;
}
#else
/* No catalog, so everything okay */
int
main(void) {
    return 0;
}
#endif
