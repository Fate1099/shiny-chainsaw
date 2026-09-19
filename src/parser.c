#include "parser.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

MdOptions md_options_default(void)
{
    MdOptions options = {.flags = 0};
    return options;
}

void md_options_set(MdOptions* options, MdFeature feature, gboolean enabled)
{
    if (enabled)
        options->flags |= feature;
    else
        options->flags &= ~feature;
}

gboolean md_options_has(const MdOptions* options, MdFeature feature)
{
    return (options->flags & feature) != 0;
}

/* ------------------------------------------------------------------ */
/* Node helpers                                                        */
/* ------------------------------------------------------------------ */

static MdNode* node_new(MdNodeKind kind)
{
    MdNode* node = g_new0(MdNode, 1);
    node->kind = kind;
    return node;
}

void md_node_free(MdNode* node)
{
    if (node == NULL)
        return;
    if (node->children != NULL)
    {
        for (guint i = 0; i < node->children->len; i++)
            md_node_free(g_ptr_array_index(node->children, i));
        g_ptr_array_free(node->children, TRUE);
    }
    g_free(node->literal);
    g_free(node->title);
    g_free(node);
}

static void add_child(MdNode* parent, MdNode* child)
{
    if (parent->children == NULL)
        parent->children = g_ptr_array_new();
    child->parent = parent;
    g_ptr_array_add(parent->children, child);
}

/* ------------------------------------------------------------------ */
/* Line classification helpers                                         */
/* ------------------------------------------------------------------ */

static size_t leading_spaces(const char* s)
{
    size_t n = 0;
    while (*s == ' ')
    {
        n++;
        s++;
    }
    return n;
}

static gboolean is_blank_line(const char* s)
{
    while (*s == ' ')
        s++;
    return *s == '\0';
}

static void trim_trailing_newlines(gchar** str)
{
    gsize len = strlen(*str);
    while (len > 0 && (*str)[len - 1] == '\n')
        (*str)[--len] = '\0';
}

/* ATX heading: 1-6 '#' followed by space or EOL. */
static gboolean parse_atx_heading(const char* s, int* level, const char** text)
{
    int n = 0;
    while (s[n] == '#')
        n++;
    if (n < 1 || n > 6)
        return FALSE;
    if (s[n] != ' ' && s[n] != '\0')
        return FALSE;
    *level = n;
    *text = s + n;
    if (**text == ' ')
        (*text)++;
    return TRUE;
}

static gboolean is_thematic_break(const char* s)
{
    int count = 0;
    char marker = 0;
    while (*s == ' ')
        s++;
    while (*s != '\0')
    {
        if (*s == ' ' || *s == '\t')
        {
            s++;
            continue;
        }
        if (marker == 0)
            marker = *s;
        if (*s != marker || (marker != '*' && marker != '-' && marker != '_'))
            return FALSE;
        count++;
        s++;
    }
    return count >= 3;
}

/* Fenced code: ``` or ~~~ with optional info string. */
static gboolean parse_fence(const char* s, char* fence_char, size_t* fence_len, const char** info)
{
    size_t n = leading_spaces(s);
    s += n;
    char c = *s;
    if (c != '`' && c != '~')
        return FALSE;
    size_t len = 0;
    while (s[len] == c)
        len++;
    if (len < 3)
        return FALSE;
    if (c == '`' && s[len] == '`')
        return FALSE; /* info string cannot contain backtick */
    *fence_char = c;
    *fence_len = len;
    *info = s + len;
    return TRUE;
}

static gboolean is_fence_close(const char* s, char fence_char, size_t fence_len)
{
    size_t n = leading_spaces(s);
    s += n;
    size_t len = 0;
    while (s[len] == fence_char)
        len++;
    if (len < fence_len)
        return FALSE;
    while (s[len] == ' ')
        len++;
    return s[len] == '\0';
}

/* List marker: "- ", "* ", "+ ", "1. ", "1) ". Returns marker width. */
static gboolean parse_list_marker(const char* s, size_t* width, MdListType* type, int* start)
{
    const char* p = s;
    if (*p == '-' || *p == '+' || *p == '*')
    {
        p++;
        if (*p != ' ')
            return FALSE;
        *type = MD_LIST_UNORDERED;
        *width = (size_t)(p - s) + 1;
        *start = 0;
        return TRUE;
    }
    if (*p >= '0' && *p <= '9')
    {
        while (*p >= '0' && *p <= '9')
            p++;
        if ((*p != '.' && *p != ')') || p[1] != ' ')
            return FALSE;
        *type = MD_LIST_ORDERED;
        *start = (int)strtol(s, NULL, 10);
        *width = (size_t)(p - s) + 2;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Inline parser                                                       */
/* ------------------------------------------------------------------ */

static void parse_inline(GPtrArray* out, const char* s);

static void add_text_node(GPtrArray* out, const char* start, size_t len)
{
    if (len == 0)
        return;
    MdNode* node = node_new(MD_NODE_TEXT);
    node->literal = g_strndup(start, len);
    g_ptr_array_add(out, node);
}

/* Link destination + optional title after ']'. Returns node kind consumed.
 * On success, *end points just past the closing ')' of the target. */
static gboolean parse_link_target(const char* s, char** url, char** title, const char** end)
{
    if (*s != '(')
        return FALSE;
    s++;
    while (*s == ' ' || *s == '\n')
        s++;
    const char* url_start = s;
    while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '\n' && *s != ')')
        s++;
    if (url_start == s)
        return FALSE;
    char* url_str = g_strndup(url_start, (size_t)(s - url_start));

    *title = NULL;
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if (*s == '"')
    {
        s++;
        const char* t = s;
        while (*s != '\0' && *s != '"')
            s++;
        if (*s != '"')
        {
            g_free(url_str);
            return FALSE;
        }
        *title = g_strndup(t, (size_t)(s - t));
        s++;
        while (*s == ' ' || *s == '\t' || *s == '\n')
            s++;
    }
    if (*s != ')')
    {
        g_free(url_str);
        g_free(*title);
        *title = NULL;
        return FALSE;
    }
    *url = url_str;
    if (end != NULL)
        *end = s + 1;
    return TRUE;
}

static void parse_inline(GPtrArray* out, const char* s)
{
    const char* p = s;
    const char* text_start = s;

    while (*p != '\0')
    {
        char c = *p;

        /* HTML comment: skip to the closing --> */
        if (c == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-')
        {
            add_text_node(out, text_start, (size_t)(p - text_start));
            const char* end = strstr(p + 4, "-->");
            p = (end != NULL) ? end + 3 : p + 4;
            text_start = p;
            continue;
        }

        /* backslash escape */
        if (c == '\\' && p[1] != '\0')
        {
            add_text_node(out, text_start, (size_t)(p - text_start));
            MdNode* node = node_new(MD_NODE_TEXT);
            node->literal = g_strndup(p + 1, 1);
            g_ptr_array_add(out, node);
            p += 2;
            text_start = p;
            continue;
        }

        /* code span */
        if (c == '`')
        {
            const char* run = p;
            while (*run == '`')
                run++;
            size_t run_len = (size_t)(run - p);
            const char* q = run;
            const char* match = NULL;
            while (*q != '\0')
            {
                if (*q == '`')
                {
                    const char* r = q;
                    while (*r == '`')
                        r++;
                    if ((size_t)(r - q) == run_len)
                    {
                        match = q;
                        break;
                    }
                    q = r;
                }
                else
                {
                    q++;
                }
            }
            if (match != NULL)
            {
                add_text_node(out, text_start, (size_t)(p - text_start));
                gsize content_len = (size_t)(match - run);
                gchar* literal = NULL;

                gboolean all_space = TRUE;
                for (gsize k = 0; k < content_len; k++)
                {
                    if (run[k] != ' ')
                    {
                        all_space = FALSE;
                        break;
                    }
                }

                if (all_space)
                {
                    literal = g_strdup(" ");
                }
                else
                {
                    gsize start_off = (run[0] == ' ' && match[-1] == ' ') ? 1 : 0;
                    gsize end_off = (start_off && match[-1] == ' ') ? 1 : 0;
                    if (start_off + end_off > content_len)
                        end_off = content_len - start_off;
                    literal = g_strndup(run + start_off, content_len - start_off - end_off);
                }

                MdNode* node = node_new(MD_NODE_CODE_SPAN);
                node->literal = literal;
                g_ptr_array_add(out, node);
                p = match + run_len;
                text_start = p;
                continue;
            }
            /* no closing backtick: treat as literal */
        }

        /* strong ** or __ */
        if (c == '*' || c == '_')
        {
            char delim = c;
            if (p[1] == delim && p[2] == delim)
            {
                /* ***bold and em*** */
                const char* close = strstr(p + 3, delim == '*' ? "***" : "___");
                if (close != NULL)
                {
                    add_text_node(out, text_start, (size_t)(p - text_start));
                    MdNode* strong = node_new(MD_NODE_STRONG);
                    strong->children = g_ptr_array_new();
                    MdNode* em = node_new(MD_NODE_EMPHASIS);
                    em->children = g_ptr_array_new();
                    gchar* inner = g_strndup(p + 3, (size_t)(close - (p + 3)));
                    parse_inline(em->children, inner);
                    g_free(inner);
                    g_ptr_array_add(strong->children, em);
                    g_ptr_array_add(out, strong);
                    p = close + 3;
                    text_start = p;
                    continue;
                }
            }
            if (p[1] == delim)
            {
                /* strong */
                const char* close = strstr(p + 2, delim == '*' ? "**" : "__");
                if (close != NULL)
                {
                    add_text_node(out, text_start, (size_t)(p - text_start));
                    MdNode* node = node_new(MD_NODE_STRONG);
                    node->children = g_ptr_array_new();
                    gchar* inner = g_strndup(p + 2, (size_t)(close - (p + 2)));
                    parse_inline(node->children, inner);
                    g_free(inner);
                    g_ptr_array_add(out, node);
                    p = close + 2;
                    text_start = p;
                    continue;
                }
            }
            else
            {
                /* em */
                const char* close = strchr(p + 1, delim);
                if (close != NULL)
                {
                    add_text_node(out, text_start, (size_t)(p - text_start));
                    MdNode* node = node_new(MD_NODE_EMPHASIS);
                    node->children = g_ptr_array_new();
                    gchar* inner = g_strndup(p + 1, (size_t)(close - (p + 1)));
                    parse_inline(node->children, inner);
                    g_free(inner);
                    g_ptr_array_add(out, node);
                    p = close + 1;
                    text_start = p;
                    continue;
                }
            }
        }

        /* link or image */
        if (c == '[' || (c == '!' && p[1] == '['))
        {
            gboolean is_image = (c == '!');
            const char* label_start = p + (is_image ? 2 : 1);
            const char* close_bracket = strchr(label_start, ']');
            if (close_bracket != NULL)
            {
                char* url = NULL;
                char* title = NULL;
                const char* target_end = NULL;
                if (parse_link_target(close_bracket + 1, &url, &title, &target_end))
                {
                    add_text_node(out, text_start, (size_t)(p - text_start));
                    MdNode* node = node_new(is_image ? MD_NODE_IMAGE : MD_NODE_LINK);
                    node->literal = url;
                    node->title = title;
                    node->children = g_ptr_array_new();
                    gchar* label = g_strndup(label_start, (size_t)(close_bracket - label_start));
                    parse_inline(node->children, label);
                    g_free(label);
                    g_ptr_array_add(out, node);
                    p = target_end;
                    text_start = p;
                    continue;
                }
            }
        }

        /* raw text: also handle '!' as plain char otherwise */
        if (c == '\n')
        {
            /* treat newline as space for inline purposes */
            add_text_node(out, text_start, (size_t)(p - text_start));
            add_text_node(out, " ", 1);
            p++;
            text_start = p;
            continue;
        }

        p++;
    }

    add_text_node(out, text_start, (size_t)(p - text_start));
}

/* ------------------------------------------------------------------ */
/* Block parser                                                        */
/* ------------------------------------------------------------------ */

static void parse_blocks(MdNode* container, char** lines, size_t n, size_t indent,
                         const MdOptions* options);

static void parse_blockquote(MdNode* container, char** lines, size_t start, size_t end,
                             const MdOptions* options)
{
    GPtrArray* stripped = g_ptr_array_new_with_free_func(g_free);
    for (size_t i = start; i < end; i++)
    {
        const char* s = lines[i];
        size_t sp = leading_spaces(s);
        s += sp;
        if (*s == '>')
        {
            s++;
            if (*s == ' ')
                s++;
        }
        g_ptr_array_add(stripped, g_strdup(s));
    }

    MdNode* node = node_new(MD_NODE_BLOCKQUOTE);
    node->children = g_ptr_array_new();
    parse_blocks(node, (char**)stripped->pdata, stripped->len, 0, options);
    g_ptr_array_free(stripped, TRUE);
    add_child(container, node);
}

static void parse_list(MdNode* container, char** lines, size_t start, size_t end,
                       MdListType list_type, int start_num, const MdOptions* options)
{
    MdNode* list = node_new(MD_NODE_LIST);
    list->list_type = list_type;
    list->start = start_num;
    list->tight = TRUE;
    list->children = g_ptr_array_new();

    size_t base_indent = leading_spaces(lines[start]);

    MdNode* item = NULL;
    GPtrArray* item_lines = NULL;
    size_t item_indent = 0;

    for (size_t i = start; i < end; i++)
    {
        const char* s = lines[i];
        size_t lsp = leading_spaces(s);
        size_t width = 0;
        MdListType t;
        int n;

        if (lsp == base_indent && parse_list_marker(s + lsp, &width, &t, &n))
        {
            if (item != NULL)
            {
                parse_blocks(item, (char**)item_lines->pdata, item_lines->len, 0, options);
                g_ptr_array_free(item_lines, TRUE);
            }
            item = node_new(MD_NODE_ITEM);
            item->children = g_ptr_array_new();
            item_lines = g_ptr_array_new_with_free_func(g_free);
            item_indent = lsp + width;
            g_ptr_array_add(item_lines, g_strdup(s + item_indent));
            add_child(list, item);
        }
        else if (item != NULL)
        {
            if (is_blank_line(s))
                list->tight = FALSE;
            /* Normalize continuation lines: strip the item's content indent so
             * nested blocks (deeper markers, indented code) parse correctly. */
            g_ptr_array_add(item_lines, g_strdup(s + MIN(lsp, item_indent)));
        }
    }

    if (item != NULL)
    {
        parse_blocks(item, (char**)item_lines->pdata, item_lines->len, 0, options);
        g_ptr_array_free(item_lines, TRUE);
    }

    add_child(container, list);
}

/* ------------------------------------------------------------------ */
/* GFM tables                                                          */
/* ------------------------------------------------------------------ */

/* Split a table row into trimmed cells. Returns FALSE when the line has
 * no cells after stripping optional outer pipes. */
static gboolean parse_table_cells(const char* s, GPtrArray* cells)
{
    gchar* line = g_strdup(s);
    g_strstrip(line);
    gsize len = strlen(line);
    if (len == 0)
    {
        g_free(line);
        return FALSE;
    }
    if (line[0] == '|')
    {
        memmove(line, line + 1, len);
        len--;
    }
    if (len > 0 && line[len - 1] == '|')
        line[--len] = '\0';
    g_strstrip(line);
    if (strlen(line) == 0)
    {
        g_free(line);
        return FALSE;
    }

    gchar** parts = g_strsplit(line, "|", -1);
    for (guint i = 0; parts[i] != NULL; i++)
    {
        gchar* cell = parts[i];
        g_strstrip(cell);
        g_ptr_array_add(cells, g_strdup(cell));
    }
    g_strfreev(parts);
    g_free(line);
    return cells->len > 0;
}

/* Parse a GFM delimiter row (e.g. "| :--- | :---: |") into `aligns` (at
 * most max_cols entries). Returns the column count, or 0 when the line is
 * not a valid delimiter row. */
static gsize parse_delimiter_row(const char* s, MdAlign* aligns, gsize max_cols)
{
    if (strchr(s, '|') == NULL)
        return 0; /* a delimiter row must be pipe-delimited, not "---" */

    GPtrArray* cells = g_ptr_array_new_with_free_func(g_free);
    gsize n = parse_table_cells(s, cells) ? cells->len : 0;
    if (n == 0 || n > max_cols)
    {
        g_ptr_array_free(cells, TRUE);
        return 0;
    }
    for (gsize i = 0; i < n; i++)
    {
        const char* c = g_ptr_array_index(cells, i);
        gsize clen = strlen(c);
        if (clen == 0)
        {
            g_ptr_array_free(cells, TRUE);
            return 0;
        }
        gboolean left = (c[0] == ':');
        gboolean right = (c[clen - 1] == ':');
        for (gsize k = (left ? 1 : 0); k < clen - (right ? 1 : 0); k++)
        {
            if (c[k] != '-')
            {
                g_ptr_array_free(cells, TRUE);
                return 0;
            }
        }
        if (clen - (left ? 1 : 0) - (right ? 1 : 0) == 0)
        {
            g_ptr_array_free(cells, TRUE);
            return 0;
        }
        aligns[i] = (left && right)
                        ? MD_ALIGN_CENTER
                        : (left ? MD_ALIGN_LEFT : (right ? MD_ALIGN_RIGHT : MD_ALIGN_NONE));
    }
    g_ptr_array_free(cells, TRUE);
    return n;
}

static void add_table_cell(MdNode* row, const char* text, MdAlign align)
{
    MdNode* cell = node_new(MD_NODE_TABLE_CELL);
    cell->align = align;
    cell->children = g_ptr_array_new();
    parse_inline(cell->children, text);
    add_child(row, cell);
}

static void parse_blocks(MdNode* container, char** lines, size_t n, size_t indent,
                         const MdOptions* options)
{
    size_t i = 0;
    while (i < n)
    {
        const char* line = lines[i];
        size_t sp = leading_spaces(line);
        if (sp < indent)
            break;
        const char* content = line + indent;
        sp -= indent;

        if (is_blank_line(content))
        {
            i++;
            continue;
        }

        /* indented code: 4+ spaces, must be detected before stripping */
        if (sp >= 4)
        {
            GString* buf = g_string_new(NULL);
            while (i < n)
            {
                const char* l = lines[i];
                size_t lsp = leading_spaces(l);
                if (is_blank_line(l))
                {
                    g_string_append_c(buf, '\n');
                    i++;
                }
                else if (lsp >= 4)
                {
                    g_string_append(buf, l + 4);
                    g_string_append_c(buf, '\n');
                    i++;
                }
                else
                {
                    break;
                }
            }
            MdNode* node = node_new(MD_NODE_CODE_BLOCK);
            node->literal = g_strdup(buf->str);
            g_string_free(buf, TRUE);
            trim_trailing_newlines(&node->literal);
            add_child(container, node);
            continue;
        }

        /* fenced code (may be indented up to 3 spaces) */
        char fence_char;
        size_t fence_len;
        const char* info;
        if (parse_fence(content, &fence_char, &fence_len, &info))
        {
            GString* buf = g_string_new(NULL);
            i++;
            while (i < n && !is_fence_close(lines[i], fence_char, fence_len))
            {
                g_string_append(buf, lines[i]);
                g_string_append_c(buf, '\n');
                i++;
            }
            if (i < n)
                i++; /* skip closing fence */
            MdNode* node = node_new(MD_NODE_CODE_BLOCK);
            node->literal = g_strdup(buf->str);
            g_string_free(buf, TRUE);
            trim_trailing_newlines(&node->literal);
            add_child(container, node);
            continue;
        }

        /* strip up to 3 leading spaces for the remaining block classifiers */
        content += sp;

        /* HTML comment block: drop it entirely (may span lines) */
        if (strncmp(content, "<!--", 4) == 0)
        {
            while (i < n && strstr(lines[i], "-->") == NULL)
                i++;
            if (i < n)
                i++;
            continue;
        }

        /* ATX heading */
        int level;
        const char* text;
        if (parse_atx_heading(content, &level, &text))
        {
            MdNode* node = node_new(MD_NODE_HEADING);
            node->level = level;
            node->children = g_ptr_array_new();
            parse_inline(node->children, text);
            add_child(container, node);
            i++;
            continue;
        }

        /* thematic break */
        if (is_thematic_break(content))
        {
            add_child(container, node_new(MD_NODE_THEMATIC_BREAK));
            i++;
            continue;
        }

        /* blockquote */
        if (*content == '>')
        {
            size_t bq_start = i;
            while (i < n)
            {
                const char* l = lines[i];
                size_t lsp = leading_spaces(l);
                if (l[lsp] == '>')
                {
                    i++;
                }
                else if (is_blank_line(l) && i + 1 < n)
                {
                    size_t nsp = leading_spaces(lines[i + 1]);
                    if (lines[i + 1][nsp] == '>')
                        i++;
                    else
                        break;
                }
                else
                {
                    break;
                }
            }
            parse_blockquote(container, lines, bq_start, i, options);
            continue;
        }

        /* GFM table (feature-gated): header row followed by a delimiter row */
        if (options != NULL && md_options_has(options, MD_FEATURE_GFM_TABLES) && i + 1 < n &&
            strchr(content, '|') != NULL)
        {
            GPtrArray* header_cells = g_ptr_array_new_with_free_func(g_free);
            MdAlign aligns[16];
            gsize header_cols = parse_table_cells(content, header_cells) ? header_cells->len : 0;
            gsize delim_cols = 0;
            if (header_cols > 0 && header_cols <= G_N_ELEMENTS(aligns))
            {
                const char* nl = lines[i + 1] + leading_spaces(lines[i + 1]);
                delim_cols = parse_delimiter_row(nl, aligns, header_cols);
            }
            if (header_cols == delim_cols)
            {
                MdNode* table = node_new(MD_NODE_TABLE);
                table->children = g_ptr_array_new();
                i += 2;

                MdNode* header_row = node_new(MD_NODE_TABLE_ROW);
                header_row->header = TRUE;
                header_row->children = g_ptr_array_new();
                for (gsize c = 0; c < header_cols; c++)
                    add_table_cell(header_row, g_ptr_array_index(header_cells, c), aligns[c]);
                add_child(table, header_row);

                while (i < n)
                {
                    const char* bl = lines[i] + leading_spaces(lines[i]);
                    GPtrArray* cells = g_ptr_array_new_with_free_func(g_free);
                    if (strchr(bl, '|') == NULL || !parse_table_cells(bl, cells))
                    {
                        g_ptr_array_free(cells, TRUE);
                        break;
                    }
                    MdNode* row = node_new(MD_NODE_TABLE_ROW);
                    row->children = g_ptr_array_new();
                    for (gsize c = 0; c < header_cols; c++)
                    {
                        const gchar* cell_text =
                            (c < cells->len) ? g_ptr_array_index(cells, c) : "";
                        add_table_cell(row, cell_text, aligns[c]);
                    }
                    g_ptr_array_free(cells, TRUE);
                    add_child(table, row);
                    i++;
                }

                add_child(container, table);
                g_ptr_array_free(header_cells, TRUE);
                continue;
            }
            g_ptr_array_free(header_cells, TRUE);
        }

        /* list */
        size_t mw;
        MdListType ltype;
        int lnum;
        if (parse_list_marker(content, &mw, &ltype, &lnum))
        {
            size_t base = sp;
            size_t list_start = i;
            i++;
            while (i < n)
            {
                const char* l = lines[i];
                size_t lsp = leading_spaces(l);
                size_t w2;
                MdListType t2;
                int n2;

                if (parse_list_marker(l + lsp, &w2, &t2, &n2) || (lsp > base && !is_blank_line(l)))
                {
                    /* sibling item, indented content, or nested marker */
                    i++;
                }
                else if (is_blank_line(l))
                {
                    size_t j = i + 1;
                    while (j < n && is_blank_line(lines[j]))
                        j++;
                    if (j < n)
                    {
                        size_t jsp = leading_spaces(lines[j]);
                        if (jsp > base || parse_list_marker(lines[j] + jsp, &w2, &t2, &n2))
                        {
                            i = j;
                            continue;
                        }
                    }
                    break;
                }
                else
                {
                    break;
                }
            }
            parse_list(container, lines, list_start, i, ltype, lnum, options);
            continue;
        }

        /* paragraph */
        MdNode* para = node_new(MD_NODE_PARAGRAPH);
        para->children = g_ptr_array_new();
        GString* text_buf = g_string_new(content);
        i++;
        while (i < n)
        {
            const char* l = lines[i];
            size_t lsp = leading_spaces(l);
            const char* lc = l + lsp;
            if (is_blank_line(lc) || parse_atx_heading(lc, &level, &text) ||
                is_thematic_break(lc) || *lc == '>' || parse_list_marker(lc, &mw, &ltype, &lnum) ||
                parse_fence(lc, &fence_char, &fence_len, &info))
                break;
            g_string_append_c(text_buf, '\n');
            g_string_append(text_buf, lc);
            i++;
        }
        parse_inline(para->children, text_buf->str);
        g_string_free(text_buf, TRUE);
        add_child(container, para);
    }
}

/* ------------------------------------------------------------------ */
/* HTML renderer                                                       */
/* ------------------------------------------------------------------ */

static void render_inline_children(const MdNode* node, GString* out);
static void render_html(const MdNode* node, GString* out);

static void render_html(const MdNode* node, GString* out)
{
    switch (node->kind)
    {
    case MD_NODE_DOCUMENT:
        for (guint i = 0; i < node->children->len; i++)
            render_html(g_ptr_array_index(node->children, i), out);
        break;

    case MD_NODE_HEADING:
        g_string_append_printf(out, "<h%d>", node->level);
        render_inline_children(node, out);
        g_string_append_printf(out, "</h%d>\n", node->level);
        break;

    case MD_NODE_PARAGRAPH:
        if (node->parent != NULL && node->parent->parent != NULL &&
            node->parent->kind == MD_NODE_ITEM && node->parent->parent->kind == MD_NODE_LIST &&
            node->parent->parent->tight)
        {
            render_inline_children(node, out);
        }
        else
        {
            g_string_append(out, "<p>");
            render_inline_children(node, out);
            g_string_append(out, "</p>\n");
        }
        break;

    case MD_NODE_BLOCKQUOTE:
        g_string_append(out, "<blockquote>\n");
        for (guint i = 0; i < node->children->len; i++)
            render_html(g_ptr_array_index(node->children, i), out);
        g_string_append(out, "</blockquote>\n");
        break;

    case MD_NODE_LIST:
    {
        const char* tag = (node->list_type == MD_LIST_ORDERED) ? "ol" : "ul";
        if (node->list_type == MD_LIST_ORDERED && node->start != 1)
            g_string_append_printf(out, "<ol start=\"%d\">\n", node->start);
        else
            g_string_append_printf(out, "<%s>\n", tag);
        for (guint i = 0; i < node->children->len; i++)
            render_html(g_ptr_array_index(node->children, i), out);
        g_string_append_printf(out, "</%s>\n", tag);
        break;
    }

    case MD_NODE_ITEM:
        g_string_append(out, "<li>");
        for (guint i = 0; i < node->children->len; i++)
            render_html(g_ptr_array_index(node->children, i), out);
        g_string_append(out, "</li>\n");
        break;

    case MD_NODE_CODE_BLOCK:
    {
        gchar* escaped = g_markup_escape_text(node->literal, -1);
        g_string_append(out, "<pre><code>");
        g_string_append(out, escaped);
        g_string_append(out, "</code></pre>\n");
        g_free(escaped);
        break;
    }

    case MD_NODE_THEMATIC_BREAK:
        g_string_append(out, "<hr />\n");
        break;

    case MD_NODE_TABLE:
    {
        gboolean first_body = TRUE;
        g_string_append(out, "<table>\n");
        g_string_append(out, "<thead>\n");
        for (guint i = 0; i < node->children->len; i++)
        {
            const MdNode* row = g_ptr_array_index(node->children, i);
            if (!row->header && first_body)
            {
                g_string_append(out, "</thead>\n<tbody>\n");
                first_body = FALSE;
            }
            render_html(row, out);
        }
        g_string_append(out, first_body ? "</thead>\n" : "</tbody>\n");
        g_string_append(out, "</table>\n");
        break;
    }

    case MD_NODE_TABLE_ROW:
        g_string_append(out, "<tr>\n");
        for (guint i = 0; i < node->children->len; i++)
        {
            const MdNode* cell = g_ptr_array_index(node->children, i);
            const char* tag = node->header ? "th" : "td";
            if (cell->align != MD_ALIGN_NONE)
            {
                const char* a = cell->align == MD_ALIGN_LEFT
                                    ? "left"
                                    : (cell->align == MD_ALIGN_CENTER ? "center" : "right");
                g_string_append_printf(out, "<%s align=\"%s\">", tag, a);
            }
            else
            {
                g_string_append_printf(out, "<%s>", tag);
            }
            render_inline_children(cell, out);
            g_string_append_printf(out, "</%s>\n", tag);
        }
        g_string_append(out, "</tr>\n");
        break;

    default:
        break;
    }
}

static void render_inline_children(const MdNode* node, GString* out)
{
    for (guint i = 0; i < node->children->len; i++)
    {
        const MdNode* child = g_ptr_array_index(node->children, i);
        switch (child->kind)
        {
        case MD_NODE_TEXT:
        {
            gchar* escaped = g_markup_escape_text(child->literal, -1);
            g_string_append(out, escaped);
            g_free(escaped);
            break;
        }
        case MD_NODE_CODE_SPAN:
        {
            gchar* escaped = g_markup_escape_text(child->literal, -1);
            g_string_append(out, "<code>");
            g_string_append(out, escaped);
            g_string_append(out, "</code>");
            g_free(escaped);
            break;
        }
        case MD_NODE_EMPHASIS:
            g_string_append(out, "<em>");
            render_inline_children(child, out);
            g_string_append(out, "</em>");
            break;
        case MD_NODE_STRONG:
            g_string_append(out, "<strong>");
            render_inline_children(child, out);
            g_string_append(out, "</strong>");
            break;
        case MD_NODE_LINK:
        {
            gchar* url = g_markup_escape_text(child->literal, -1);
            g_string_append_printf(out, "<a href=\"%s\">", url);
            g_free(url);
            render_inline_children(child, out);
            g_string_append(out, "</a>");
            break;
        }
        case MD_NODE_IMAGE:
        {
            gchar* url = g_markup_escape_text(child->literal, -1);
            g_string_append_printf(out, "<img src=\"%s\" alt=\"", url);
            g_free(url);
            for (guint j = 0; j < child->children->len; j++)
            {
                const MdNode* c2 = g_ptr_array_index(child->children, j);
                if (c2->kind == MD_NODE_TEXT)
                {
                    gchar* esc = g_markup_escape_text(c2->literal, -1);
                    g_string_append(out, esc);
                    g_free(esc);
                }
            }
            g_string_append(out, "\" />");
            break;
        }
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

MdNode* md_parse(const gchar* markdown, const MdOptions* options, GError** error)
{
    (void)error;
    g_return_val_if_fail(markdown != NULL, NULL);

    MdNode* doc = node_new(MD_NODE_DOCUMENT);
    doc->children = g_ptr_array_new();

    gchar** lines = g_strsplit(markdown, "\n", -1);
    gsize n = g_strv_length(lines);
    for (gsize i = 0; i < n; i++)
    {
        gsize l = strlen(lines[i]);
        if (l > 0 && lines[i][l - 1] == '\r')
            lines[i][l - 1] = '\0'; /* strip CR from CRLF line endings */
    }
    parse_blocks(doc, lines, n, 0, options);
    g_strfreev(lines);

    return doc;
}

gchar* md_render_html(const MdNode* node)
{
    GString* out = g_string_new(NULL);
    render_html(node, out);
    return g_string_free(out, FALSE);
}

gchar* md_parser_parse(const gchar* markdown, const MdOptions* options, GError** error)
{
    MdNode* doc = md_parse(markdown, options, error);
    if (doc == NULL)
        return NULL;
    gchar* html = md_render_html(doc);
    md_node_free(doc);
    return html;
}