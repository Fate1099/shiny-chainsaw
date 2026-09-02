#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
test_empty_input(void)
{
    GError *error = NULL;
    gchar *html = md_parser_parse("", &error);
    assert(error == NULL);
    assert(html != NULL);
    assert(strcmp(html, "") == 0);
    g_free(html);
}

static void
test_plain_text_is_escaped_and_wrapped(void)
{
    GError *error = NULL;
    gchar *html = md_parser_parse("hello & <world>", &error);
    assert(error == NULL);
    assert(html != NULL);
    assert(strcmp(html, "<p>hello &amp; &lt;world&gt;</p>") == 0);
    g_free(html);
}

static void
test_null_input(void)
{
    GError *error = NULL;
    gchar *html = md_parser_parse(NULL, &error);
    assert(html == NULL);
}

int
main(void)
{
    test_empty_input();
    test_plain_text_is_escaped_and_wrapped();
    test_null_input();
    puts("test_parser: all assertions passed");
    return 0;
}