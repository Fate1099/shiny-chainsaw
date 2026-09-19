#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const char* name;
    const char* input;
    const char* expected;
} Case;

static const Case CASES[] = {
    {"empty", "", ""},
    {"plain text", "hello world", "<p>hello world</p>\n"},
    {"escaping", "a < b & c > d", "<p>a &lt; b &amp; c &gt; d</p>\n"},
    {"two paragraphs", "one\n\ntwo", "<p>one</p>\n<p>two</p>\n"},
    {"paragraph line continuation", "one\ntwo", "<p>one two</p>\n"},
    {"atx h1", "# Title", "<h1>Title</h1>\n"},
    {"atx h2", "## Sub", "<h2>Sub</h2>\n"},
    {"atx h6", "###### Tiny", "<h6>Tiny</h6>\n"},
    {"atx not heading (7 hashes)", "####### nope", "<p>####### nope</p>\n"},
    {"thematic break", "---", "<hr />\n"},
    {"fenced code", "```\ncode <here>\n```", "<pre><code>code &lt;here&gt;</code></pre>\n"},
    {"indented code", "    indented", "<pre><code>indented</code></pre>\n"},
    {"blockquote", "> quote", "<blockquote>\n<p>quote</p>\n</blockquote>\n"},
    {"unordered list", "- one\n- two", "<ul>\n<li>one</li>\n<li>two</li>\n</ul>\n"},
    {"ordered list", "1. first\n2. second", "<ol>\n<li>first</li>\n<li>second</li>\n</ol>\n"},
    {"emphasis", "*em*", "<p><em>em</em></p>\n"},
    {"strong", "**strong**", "<p><strong>strong</strong></p>\n"},
    {"code span", "`x < y`", "<p><code>x &lt; y</code></p>\n"},
    {"link", "[text](http://x.y)", "<p><a href=\"http://x.y\">text</a></p>\n"},
    {"inline in heading", "# **Big** *title*", "<h1><strong>Big</strong> <em>title</em></h1>\n"},
    {"mixed list with nested text", "- item *one*\n- item two",
     "<ul>\n<li>item <em>one</em></li>\n<li>item two</li>\n</ul>\n"},
    {"tight list not wrapped in p", "- one\n- two\n\nnext",
     "<ul>\n<li>one</li>\n<li>two</li>\n</ul>\n<p>next</p>\n"},
    {"tight ordered list", "1. a\n2. b", "<ol>\n<li>a</li>\n<li>b</li>\n</ol>\n"},
    {"loose list wraps paragraphs", "- a\n\n- b",
     "<ul>\n<li><p>a</p>\n</li>\n<li><p>b</p>\n</li>\n</ul>\n"},
    {"triple emphasis", "***x***", "<p><strong><em>x</em></strong></p>\n"},
    {"indented code no trailing blank", "    a\n    b", "<pre><code>a\nb</code></pre>\n"},
    {"image", "![alt](http://x/y)", "<p><img src=\"http://x/y\" alt=\"alt\" /></p>\n"},
    {"image with title", "![alt](http://x/y \"title\")",
     "<p><img src=\"http://x/y\" alt=\"alt\" /></p>\n"},
    {"code span strips one space each side", "` x `", "<p><code>x</code></p>\n"},
    {"code span of all spaces", "`   `", "<p><code> </code></p>\n"},
    {"code span keeps single leading space", "` x`", "<p><code> x</code></p>\n"},
    {"link followed by paren is kept", "[a](b) (c)", "<p><a href=\"b\">a</a> (c)</p>\n"},
    {"nested unordered list", "- a\n  - b\n  - c",
     "<ul>\n<li>a<ul>\n<li>b</li>\n<li>c</li>\n</ul>\n</li>\n</ul>\n"},
    {"nested ordered list", "1. a\n   1. b", "<ol>\n<li>a<ol>\n<li>b</li>\n</ol>\n</li>\n</ol>\n"},
    {"list with continuation line", "- a\n  continued", "<ul>\n<li>a continued</li>\n</ul>\n"},
    {"ordered list custom start", "3. c\n4. d",
     "<ol start=\"3\">\n<li>c</li>\n<li>d</li>\n</ol>\n"},
    {"indented fence is a code block", "    ```\n    x\n    ```",
     "<pre><code>```\nx\n```</code></pre>\n"},
    {"crlf blank line splits paragraphs", "one\r\n\r\ntwo\r\n", "<p>one</p>\n<p>two</p>\n"},
    {"crlf heading", "# Title\r\n", "<h1>Title</h1>\n"},
    {"crlf list", "- a\r\n- b\r\n", "<ul>\n<li>a</li>\n<li>b</li>\n</ul>\n"},
    {"unclosed emphasis is literal", "*foo", "<p>*foo</p>\n"},
    {"unclosed code span is literal", "`foo", "<p>`foo</p>\n"},
    {"backslash escapes", "\\*not em\\*", "<p>*not em*</p>\n"},
    {"nested blockquote", "> > nested",
     "<blockquote>\n<blockquote>\n<p>nested</p>\n</blockquote>\n</blockquote>\n"},
    {"link with title", "[text](url \"title\")", "<p><a href=\"url\">text</a></p>\n"},
    {"heading with closing hashes", "## foo ##", "<h2>foo ##</h2>\n"},
    {"html comment dropped", "<!-- note -->\n\npara", "<p>para</p>\n"},
    {"multiline html comment", "<!-- a\nb -->\n\npara", "<p>para</p>\n"},
    {"inline html comment", "a <!-- x --> b", "<p>a  b</p>\n"},
};

/* GFM tables (run with MD_FEATURE_GFM_TABLES enabled). */
static const Case TABLE_CASES[] = {
    {"table basic", "| Term | Match |\n|------|-------|\n| a | b |",
     "<table>\n<thead>\n<tr>\n<th>Term</th>\n<th>Match</th>\n</tr>\n</"
     "thead>\n<tbody>\n<tr>\n<td>a</td>\n<td>b</td>\n</tr>\n</tbody>\n</table>\n"},
    {"table alignment", "| a | b | c | d |\n|:---|:---:|---:|---|\n| 1 | 2 | 3 | 4 |",
     "<table>\n<thead>\n<tr>\n<th align=\"left\">a</th>\n<th align=\"center\">b</th>\n<th "
     "align=\"right\">c</th>\n<th>d</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td "
     "align=\"left\">1</td>\n<td align=\"center\">2</td>\n<td "
     "align=\"right\">3</td>\n<td>4</td>\n</tr>\n</tbody>\n</table>\n"},
    {"table no outer pipes", "a | b\n- | -\n1 | 2",
     "<table>\n<thead>\n<tr>\n<th>a</th>\n<th>b</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td>1</"
     "td>\n<td>2</td>\n</tr>\n</tbody>\n</table>\n"},
    {"table bold cell", "| h |\n|---|\n| **b** |",
     "<table>\n<thead>\n<tr>\n<th>h</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td><strong>b</strong></"
     "td>\n</tr>\n</tbody>\n</table>\n"},
    {"table header only", "| a | b |\n|---|---|",
     "<table>\n<thead>\n<tr>\n<th>a</th>\n<th>b</th>\n</tr>\n</thead>\n</table>\n"},
    {"table empty body cell", "| a | b |\n|---|---|\n| 1 |  |",
     "<table>\n<thead>\n<tr>\n<th>a</th>\n<th>b</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td>1</"
     "td>\n<td></td>\n</tr>\n</tbody>\n</table>\n"},
    {"delimiter mismatch is not a table", "| a | b |\n|---|\nx", "<p>| a | b | |---| x</p>\n"},
};

static void run_case(const Case* c, const MdOptions* options)
{
    GError* error = NULL;
    gchar* html = md_parser_parse(c->input, options, &error);
    assert(error == NULL);
    assert(html != NULL);
    if (strcmp(html, c->expected) != 0)
    {
        fprintf(stderr, "FAIL: %s\n  input:    %s\n  expected: %s\n  got:      %s\n", c->name,
                c->input, c->expected, html);
        assert(!"case mismatch");
    }
    g_free(html);
}

int main(void)
{
    for (guint i = 0; i < G_N_ELEMENTS(CASES); i++)
        run_case(&CASES[i], NULL);

    MdOptions opts = md_options_default();
    md_options_set(&opts, MD_FEATURE_GFM_TABLES, TRUE);
    for (guint i = 0; i < G_N_ELEMENTS(TABLE_CASES); i++)
        run_case(&TABLE_CASES[i], &opts);

    printf("test_parser: %u cases passed\n",
           (guint)(G_N_ELEMENTS(CASES) + G_N_ELEMENTS(TABLE_CASES)));
    return 0;
}