#include "parser.h"

#include <stdio.h>
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
    g_free(node->info);
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
    if (s[n] != ' ' && s[n] != '\t' && s[n] != '\0')
        return FALSE;
    *level = n;
    *text = s + n;
    while (**text == ' ' || **text == '\t')
        (*text)++;
    return TRUE;
}

/* ATX headings may end with a run of '#' preceded by whitespace. */
static void strip_atx_closing(gchar* s)
{
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == '#')
    {
        size_t start = len;
        while (start > 0 && s[start - 1] == '#')
            start--;
        if (start == 0 || s[start - 1] == ' ' || s[start - 1] == '\t')
            len = start;
        else
            break;
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
        len--;
    s[len] = '\0';
}

/* Setext underline: a line of only '-' or '=' with at most 3 leading spaces
 * and optional trailing whitespace. Returns the heading level, or 0. */
static int setext_underline_level(const char* s)
{
    size_t n = leading_spaces(s);
    if (n > 3)
        return 0;
    s += n;
    char marker = *s;
    if (marker != '-' && marker != '=')
        return 0;
    while (*s == marker)
        s++;
    while (*s == ' ' || *s == '\t')
        s++;
    return (*s == '\0') ? (marker == '=' ? 1 : 2) : 0;
}

static void strip_surrounding_whitespace(gchar* text)
{
    gsize start = 0;
    while (text[start] == ' ' || text[start] == '\t')
        start++;
    gsize end = strlen(text);
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        end--;
    if (start > 0)
        memmove(text, text + start, end - start);
    text[end - start] = '\0';
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
static gboolean parse_fence(const char* s, char* fence_char, size_t* fence_len,
                            size_t* fence_indent, const char** info)
{
    size_t n = leading_spaces(s);
    if (n > 3)
        return FALSE;
    s += n;
    char c = *s;
    if (c != '`' && c != '~')
        return FALSE;
    size_t len = 0;
    while (s[len] == c)
        len++;
    if (len < 3)
        return FALSE;
    const char* rest = s + len;
    if (c == '`' && strchr(rest, '`') != NULL)
        return FALSE;
    *fence_char = c;
    *fence_len = len;
    *fence_indent = n;
    *info = rest;
    return TRUE;
}

static gboolean is_fence_close(const char* s, char fence_char, size_t fence_len)
{
    size_t n = leading_spaces(s);
    if (n > 3)
        return FALSE;
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

/* GFM task marker: [ ] or [x]/[X] followed by whitespace or end of line. */
static gboolean parse_task_marker(const char* s, gboolean* checked)
{
    if (s[0] != '[' || s[1] == '\0' || s[2] != ']')
        return FALSE;
    if (s[1] == ' ')
        *checked = FALSE;
    else if (s[1] == 'x' || s[1] == 'X')
        *checked = TRUE;
    else
        return FALSE;
    return s[3] == ' ' || s[3] == '\t' || s[3] == '\0';
}

/* List marker: "- ", "* ", "+ ", "1. ", "1) ". Returns marker width. */
static gboolean parse_list_marker(const char* s, size_t* width, MdListType* type, int* start,
                                  char* marker, gboolean require_content)
{
    const char* p = s;
    if (*p == '-' || *p == '+' || *p == '*')
    {
        p++;
        if (*p != ' ' && *p != '\t' && *p != '\0')
            return FALSE;
        if (require_content)
        {
            const char* q = p;
            while (*q == ' ' || *q == '\t')
                q++;
            if (*q == '\0')
                return FALSE;
        }
        *type = MD_LIST_UNORDERED;
        *width = (size_t)(p - s);
        *start = 0;
        *marker = *s;
        return TRUE;
    }
    if (*p >= '0' && *p <= '9')
    {
        while (*p >= '0' && *p <= '9')
            p++;
        if ((*p != '.' && *p != ')') || (p[1] != ' ' && p[1] != '\t' && p[1] != '\0'))
            return FALSE;
        if (require_content)
        {
            const char* q = p + 1;
            while (*q == ' ' || *q == '\t')
                q++;
            if (*q == '\0')
                return FALSE;
        }
        long long num = strtoll(s, NULL, 10);
        if (num > 999999999LL)
            return FALSE;
        *type = MD_LIST_ORDERED;
        *start = (int)num;
        *width = (size_t)(p - s) + 1;
        *marker = *p;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Character classes and character references                          */
/* ------------------------------------------------------------------ */

typedef struct
{
    const char* name;
    const char* replacement;
} MdEntity;

#include "entities.inc"

static const char* entity_lookup(const char* name, size_t len)
{
    size_t lo = 0;
    size_t hi = G_N_ELEMENTS(ENTITY_TABLE);
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        const MdEntity* entry = &ENTITY_TABLE[mid];
        size_t entry_len = strlen(entry->name);
        size_t common = (len < entry_len) ? len : entry_len;
        int cmp = memcmp(name, entry->name, common);
        if (cmp == 0)
            cmp = (len < entry_len) ? -1 : (len > entry_len ? 1 : 0);

        if (cmp == 0)
            return entry->replacement;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

/* Returns the number of source characters consumed when s is a valid HTML5
 * character reference, writing the UTF-8 replacement into out (8 bytes). */
static size_t try_character_reference(const char* s, char* out)
{
    if (s[0] != '&')
        return 0;

    if (s[1] == '#')
    {
        gboolean hex = (s[2] == 'x' || s[2] == 'X');
        size_t i = hex ? 3 : 2;
        gunichar value = 0;
        size_t digits = 0;
        while (TRUE)
        {
            char c = s[i];
            guint digit;
            if (hex)
            {
                if (!g_ascii_isxdigit(c))
                    break;
                digit = (guint)((c >= '0' && c <= '9') ? c - '0' : g_ascii_tolower(c) - 'a' + 10);
            }
            else
            {
                if (!g_ascii_isdigit(c))
                    break;
                digit = (guint)(c - '0');
            }
            if (value > 0x10FFFF)
            {
                value = 0x110000;
            }
            else
            {
                value = value * (hex ? 16 : 10) + digit;
            }
            i++;
            digits++;
        }
        if (digits == 0 || s[i] != ';' || value > 0x10FFFF)
            return 0;
        if (value == 0 || (value >= 0xD800 && value <= 0xDFFF))
            value = 0xFFFD;
        int len = g_unichar_to_utf8(value, out);
        out[len] = '\0';
        return i + 1;
    }

    size_t i = 1;
    while (g_ascii_isalnum(s[i]))
        i++;
    if (s[i] != ';')
        return 0;
    const char* replacement = entity_lookup(s + 1, i - 1);
    if (replacement == NULL)
        return 0;
    memcpy(out, replacement, strlen(replacement) + 1);
    return i + 1;
}

static gunichar utf8_char_at(const char* s)
{
    gunichar c = g_utf8_get_char_validated(s, -1);
    return (c == (gunichar)-1 || c == (gunichar)-2) ? 0 : c;
}

/* The character immediately before s[pos], or 0 for the start of the line. */
static gunichar char_before(const char* s, size_t pos)
{
    if (pos == 0)
        return 0;
    size_t start = pos - 1;
    while (start > 0 && ((guchar)s[start] & 0xC0) == 0x80)
        start--;
    return utf8_char_at(s + start);
}

static gboolean is_whitespace(gunichar c)
{
    return c == 0 || g_unichar_isspace(c);
}

/* CommonMark treats anything that is neither alphanumeric nor whitespace
 * (and not the start/end of the line) as punctuation. */
static gboolean is_punctuation(gunichar c)
{
    return c != 0 && !g_unichar_isalnum(c) && !g_unichar_isspace(c);
}

/* CommonMark ASCII punctuation, as ranges so a NUL terminator is never a match. */
static gboolean is_ascii_punctuation(char c)
{
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') ||
           (c >= '{' && c <= '~');
}

/* ------------------------------------------------------------------ */
/* Inline parser                                                       */
/* ------------------------------------------------------------------ */

static void parse_inline(GPtrArray* out, const char* s, const MdOptions* options, GHashTable* refs);
static gboolean is_html_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Spaces, tabs and at most one line ending between tag parts. */
static size_t skip_tag_whitespace(const char* s)
{
    size_t i = 0;
    gboolean newline = FALSE;
    while (s[i] == ' ' || s[i] == '\t' || (s[i] == '\n' && !newline))
    {
        if (s[i] == '\n')
            newline = TRUE;
        i++;
    }
    return i;
}

static size_t scan_tag(const char* s, const MdOptions* options);
static gboolean is_disallowed_raw_tag(const char* s, size_t len);

typedef struct
{
    gsize token_index;  /* index of the opening bracket token */
    size_t label_start; /* first character of the label in the source */
    gboolean is_image;
    gboolean bracket_after;
} Bracket;

typedef struct
{
    MdNode* node; /* non-NULL for a finished inline node */
    char delim;   /* '*' or '_' for a delimiter run */
    int len;
    gboolean can_open;
    gboolean can_close;
} InlineToken;

static void token_add(GArray* tokens, MdNode* node)
{
    InlineToken token = {node, 0, 0, FALSE, FALSE};
    g_array_append_val(tokens, token);
}

static void flush_text(GArray* tokens, const char* text_start, const char* end)
{
    if (end > text_start)
    {
        MdNode* node = node_new(MD_NODE_TEXT);
        node->literal = g_strndup(text_start, (size_t)(end - text_start));
        token_add(tokens, node);
    }
}

/* Link destination + optional title after ']'. Returns node kind consumed.
 * On success, *end points just past the closing ')' of the target. */
/* RFC 3986 style escaping of a link destination, as cmark does it. */
static gchar* uri_escape(const char* s, size_t len)
{
    GString* out = g_string_new(NULL);
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)s[i];
        gboolean unsafe = (c <= 0x20 || c > 0x7E || c == '"' || c == '<' || c == '>' || c == '[' ||
                           c == ']' || c == '\\' || c == '{' || c == '}' || c == '|' || c == '^');
        if (unsafe)
            g_string_append_printf(out, "%%%02X", c);
        else
            g_string_append_c(out, (char)c);
    }
    return g_string_free(out, FALSE);
}

static void append_dest_text(GString* out, const char* s, size_t* i)
{
    if (s[*i] == '\\' && is_ascii_punctuation(s[*i + 1]))
    {
        g_string_append_c(out, s[*i + 1]);
        *i += 2;
        return;
    }
    char buf[8];
    size_t consumed = try_character_reference(s + *i, buf);
    if (consumed)
    {
        g_string_append(out, buf);
        *i += consumed;
        return;
    }
    g_string_append_c(out, s[*i]);
    (*i)++;
}

static gboolean parse_link_target(const char* s, char** url, char** title, const char** end)
{
    if (*s != '(')
        return FALSE;
    size_t i = 1;
    GString* dest = g_string_new(NULL);

    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')
        i++;

    if (s[i] == '<')
    {
        i++;
        while (s[i] != '\0' && s[i] != '>')
        {
            if (s[i] == '\n' || (unsigned char)s[i] < 0x20)
            {
                g_string_free(dest, TRUE);
                return FALSE;
            }
            append_dest_text(dest, s, &i);
        }
        if (s[i] != '>')
        {
            g_string_free(dest, TRUE);
            return FALSE;
        }
        i++;
    }
    else
    {
        int depth = 0;
        while (s[i] != '\0')
        {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || (unsigned char)c < 0x20)
                break;
            if (c == ')')
            {
                if (depth == 0)
                    break;
                depth--;
                g_string_append_c(dest, c);
                i++;
                continue;
            }
            if (c == '(')
            {
                depth++;
                g_string_append_c(dest, c);
                i++;
                continue;
            }
            append_dest_text(dest, s, &i);
        }
        if (depth != 0)
        {
            g_string_free(dest, TRUE);
            return FALSE;
        }
    }

    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')
        i++;

    char* title_str = NULL;
    if (s[i] == '"' || s[i] == '\'')
    {
        char quote = s[i];
        i++;
        GString* t = g_string_new(NULL);
        while (s[i] != '\0' && s[i] != quote)
        {
            if (s[i] == '\n')
            {
                g_string_free(t, TRUE);
                g_string_free(dest, TRUE);
                return FALSE;
            }
            if (s[i] == '\\' && is_ascii_punctuation(s[i + 1]))
            {
                g_string_append_c(t, s[i + 1]);
                i += 2;
                continue;
            }
            char buf[8];
            size_t consumed = try_character_reference(s + i, buf);
            if (consumed)
            {
                g_string_append(t, buf);
                i += consumed;
                continue;
            }
            g_string_append_c(t, s[i]);
            i++;
        }
        if (s[i] != quote)
        {
            g_string_free(t, TRUE);
            g_string_free(dest, TRUE);
            return FALSE;
        }
        i++;
        title_str = g_string_free(t, FALSE);
    }
    else if (s[i] == '(')
    {
        i++;
        int depth = 1;
        GString* t = g_string_new(NULL);
        while (s[i] != '\0')
        {
            if (s[i] == '(')
            {
                depth++;
            }
            else if (s[i] == ')')
            {
                if (depth == 1)
                    break;
                depth--;
            }
            else if (s[i] == '\n')
            {
                g_string_free(t, TRUE);
                g_string_free(dest, TRUE);
                return FALSE;
            }
            else if (s[i] == '\\' && is_ascii_punctuation(s[i + 1]))
            {
                g_string_append_c(t, s[i + 1]);
                i += 2;
                continue;
            }
            g_string_append_c(t, s[i]);
            i++;
        }
        if (s[i] != ')')
        {
            g_string_free(t, TRUE);
            g_string_free(dest, TRUE);
            return FALSE;
        }
        i++;
        title_str = g_string_free(t, FALSE);
    }

    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')
        i++;
    if (s[i] != ')')
    {
        g_string_free(dest, TRUE);
        g_free(title_str);
        return FALSE;
    }
    i++;

    *url = uri_escape(dest->str, dest->len);
    g_string_free(dest, TRUE);
    *title = title_str;
    *end = s + i;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* GFM autolinks                                                       */
/* ------------------------------------------------------------------ */

static gboolean autolink_domain_char(guchar c)
{
    return g_ascii_isalnum(c) || c == '_' || c == '-';
}

static gboolean autolink_email_domain_char(guchar c)
{
    return g_ascii_isalnum(c) || c == '-' || c == '_';
}

/* Match a domain: segments of domain chars separated by '.', at least one
 * period, and no underscore in the last two segments. On success sets *end
 * one past the domain. */
static gboolean match_autolink_domain(const char* s, const char** end)
{
    const char* p = s;
    while (autolink_domain_char((guchar)*p))
        p++;
    if (p == s)
        return FALSE;
    const char* domain_end = p;

    const char* segs[3] = {s, NULL, NULL};
    int nseg = 1;
    int dots = 0;
    while (*p == '.')
    {
        const char* ns = p + 1;
        const char* seg = ns;
        while (autolink_domain_char((guchar)*ns))
            ns++;
        if (ns == seg)
            break; /* '.' not followed by a segment: trailing dot */
        dots++;
        segs[nseg % 3] = seg;
        nseg++;
        domain_end = ns;
        p = ns;
    }
    if (dots == 0)
        return FALSE;

    /* No underscore in the last two segments. */
    const char* last = segs[(nseg - 1) % 3];
    const char* prev = segs[(nseg - 2) % 3];
    for (const char* q = last; q < domain_end; q++)
    {
        if (*q == '_')
            return FALSE;
    }
    for (const char* q = prev; q < last - 1; q++)
    {
        if (*q == '_')
            return FALSE;
    }

    *end = domain_end;
    return TRUE;
}

/* Consume the path (non-whitespace, non-<) and apply GFM's extended path
 * validation: strip trailing punctuation, balance parens, drop a trailing
 * entity-looking ";". On success sets *end and returns TRUE. */
static gboolean match_autolink_path(const char* s, const char** end)
{
    const char* p = s;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '<')
        p++;

    while (p > s && strchr("?!.,:*_~", p[-1]) != NULL)
        p--;

    if (p > s && p[-1] == ')')
    {
        int open = 0, close = 0;
        for (const char* q = s; q < p; q++)
        {
            if (*q == '(')
                open++;
            else if (*q == ')')
                close++;
        }
        while (close > open && p > s && p[-1] == ')')
        {
            p--;
            close--;
        }
    }

    if (p > s && p[-1] == ';')
    {
        const char* q = p - 1;
        while (q > s && g_ascii_isalnum(q[-1]))
            q--;
        if (q > s && q[-1] == '&')
            p = q - 1; /* exclude an entity-like &name; */
    }

    /* The path may be empty (e.g. "www.x.com." with the trailing period
     * stripped), in which case the autolink is just the domain. */
    *end = p;
    return TRUE;
}

/* Email domain: segments of email-domain chars separated by '.', at least
 * one period, last character not '-' or '_'; a trailing '.' is not part of
 * the address. */
static gboolean match_email_domain(const char* s, const char** end)
{
    const char* p = s;
    while (autolink_email_domain_char((guchar)*p))
        p++;
    if (p == s)
        return FALSE;

    int dots = 0;
    while (*p == '.')
    {
        const char* ns = p + 1;
        const char* seg = ns;
        while (autolink_email_domain_char((guchar)*ns))
            ns++;
        if (ns == seg)
            break;
        dots++;
        p = ns;
    }
    if (dots == 0)
        return FALSE;

    if (p > s && p[-1] == '.')
        p--; /* trailing period is not part of the address */
    if (p > s && (p[-1] == '-' || p[-1] == '_'))
        return FALSE;
    if (p <= s)
        return FALSE;

    *end = p;
    return TRUE;
}

/* Attempt a GFM autolink at s. On success returns one past the matched text
 * and sets *href to the link destination (owned). */
static const char* try_autolink(const char* s, gchar** href)
{
    *href = NULL;

    /* extended www autolink */
    if (strncmp(s, "www.", 4) == 0)
    {
        const char* dom_end = NULL;
        if (match_autolink_domain(s + 4, &dom_end) && match_autolink_path(dom_end, &dom_end))
        {
            *href = g_strdup_printf("http://%.*s", (int)(dom_end - s), s);
            return dom_end;
        }
        return NULL;
    }

    /* extended url autolink: http:// or https:// + domain */
    if (strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0)
    {
        const char* dom_start = s + (s[4] == ':' ? 7 : 8);
        const char* dom_end = NULL;
        if (match_autolink_domain(dom_start, &dom_end) && match_autolink_path(dom_end, &dom_end))
        {
            *href = g_strndup(s, (size_t)(dom_end - s));
            return dom_end;
        }
        return NULL;
    }

    /* extended protocol autolink: mailto: or xmpp: + email */
    if (strncmp(s, "mailto:", 7) == 0 || strncmp(s, "xmpp:", 5) == 0)
    {
        gboolean xmpp = s[0] == 'x';
        const char* t = s + (xmpp ? 5 : 7);
        const char* at = strchr(t, '@');
        if (at != NULL)
        {
            const char* dom_end = NULL;
            if (match_email_domain(at + 1, &dom_end))
            {
                /* xmpp allows exactly one path segment after the address. */
                if (xmpp && dom_end[0] == '/')
                {
                    const char* p = dom_end + 1;
                    while (p[0] != '\0' && (g_ascii_isalnum((guchar)p[0]) || p[0] == '-' ||
                                            p[0] == '_' || p[0] == '.' || p[0] == '@'))
                        p++;
                    dom_end = p;
                }
                *href = g_strndup(s, (size_t)(dom_end - s));
                return dom_end;
            }
        }
        return NULL;
    }

    /* extended email autolink */
    {
        const char* at = strchr(s, '@');
        if (at != NULL && at > s)
        {
            gboolean local_ok = TRUE;
            for (const char* q = s; q < at; q++)
            {
                if (!(g_ascii_isalnum((guchar)*q) || *q == '.' || *q == '-' || *q == '_' ||
                      *q == '+'))
                {
                    local_ok = FALSE;
                    break;
                }
            }
            if (local_ok)
            {
                const char* dom_end = NULL;
                if (match_email_domain(at + 1, &dom_end))
                {
                    *href = g_strdup_printf("mailto:%.*s", (int)(dom_end - s), s);
                    return dom_end;
                }
            }
        }
    }

    return NULL;
}

static gchar* delimiter_literal(const InlineToken* token)
{
    GString* s = g_string_new(NULL);
    for (int i = 0; i < token->len; i++)
        g_string_append_c(s, token->delim);
    return g_string_free(s, FALSE);
}

static void process_emphasis(GArray* tokens)
{
    while (TRUE)
    {
        gboolean matched = FALSE;

        for (gsize i = 0; i < tokens->len && !matched; i++)
        {
            InlineToken* closer = &g_array_index(tokens, InlineToken, i);
            if (closer->node != NULL || closer->len == 0 || !closer->can_close)
                continue;

            for (gsize j = i; j-- > 0;)
            {
                InlineToken* opener = &g_array_index(tokens, InlineToken, j);
                if (opener->node != NULL || opener->len == 0 || !opener->can_open)
                    continue;
                if (opener->delim != closer->delim)
                    continue;

                /* Rule 9/10: if either run can both open and close, the sum of
                 * the run lengths may not be a multiple of 3 unless both are.
                 * Strikethrough tildes are exempt from this rule. */
                if (opener->delim != '~' && (opener->can_close || closer->can_open) &&
                    ((opener->len + closer->len) % 3 == 0) &&
                    !((opener->len % 3 == 0) && (closer->len % 3 == 0)))
                    continue;

                gboolean strong = opener->len >= 2 && closer->len >= 2;
                int used = strong ? 2 : 1;
                MdNodeKind kind = strong ? MD_NODE_STRONG : MD_NODE_EMPHASIS;
                if (opener->delim == '~')
                {
                    kind = MD_NODE_STRIKETHROUGH;
                }
                MdNode* node = node_new(kind);
                node->children = g_ptr_array_new();
                for (gsize k = j + 1; k < i; k++)
                {
                    InlineToken* t = &g_array_index(tokens, InlineToken, k);
                    if (t->node != NULL)
                    {
                        t->node->parent = node;
                        g_ptr_array_add(node->children, t->node);
                    }
                    else if (t->len > 0)
                    {
                        MdNode* text = node_new(MD_NODE_TEXT);
                        text->literal = delimiter_literal(t);
                        add_child(node, text);
                    }
                }

                opener->len -= used;
                closer->len -= used;
                InlineToken node_token = {node, 0, 0, FALSE, FALSE};
                g_array_remove_range(tokens, (guint)(j + 1), (guint)(i - j - 1));
                g_array_insert_val(tokens, (guint)(j + 1), node_token);
                matched = TRUE;
                break;
            }
        }

        if (!matched)
            break;
    }
}

static const char* find_close_bracket(const char* s)
{
    int depth = 0;
    for (const char* p = s; *p != '\0'; p++)
    {
        if (*p == '\\' && p[1] != '\0')
        {
            p++;
            continue;
        }
        if (*p == '[')
            depth++;
        else if (*p == ']')
        {
            if (depth == 0)
                return p;
            depth--;
        }
    }
    return NULL;
}

static gboolean is_email_address(const char* s)
{
    const char* at = strchr(s, '@');
    if (at == NULL || at == s || at[1] == '\0')
        return FALSE;
    for (const char* p = s; p < at; p++)
    {
        if (!g_ascii_isalnum(*p) && strchr("!#$%&'*+/=?^_`{|}~-", *p) == NULL)
            return FALSE;
    }
    const char* p = at + 1;
    if (!g_ascii_isalnum(*p))
        return FALSE;
    while (*p != '\0')
    {
        if (*p == '.')
        {
            p++;
            if (!g_ascii_isalnum(*p))
                return FALSE;
            while (*p == '-' || g_ascii_isalnum(*p))
                p++;
        }
        else
        {
            p++; /* '-' and any other character are part of the domain */
        }
    }
    return TRUE;
}

static const char* try_autolink_angle(const char* s, char** href)
{
    if (s[1] == '>')
        return NULL;

    GString* url = g_string_new(NULL);
    size_t i = 1;
    while (s[i] != '\0' && s[i] != '>')
    {
        unsigned char c = (unsigned char)s[i];
        if (c <= 0x20 || c == '<')
        {
            g_string_free(url, TRUE);
            return NULL;
        }
        g_string_append_c(url, s[i]);
        i++;
    }
    if (s[i] != '>')
    {
        g_string_free(url, TRUE);
        return NULL;
    }

    /* scheme form: at least two scheme characters, then ':' */
    size_t scheme = 0;
    while (scheme < url->len && (g_ascii_isalnum(url->str[scheme]) || url->str[scheme] == '-' ||
                                 url->str[scheme] == '+' || url->str[scheme] == '.'))
        scheme++;
    if (scheme >= 2 && url->str[scheme] == ':')
    {
        *href = g_string_free(url, FALSE);
        return s + i + 1;
    }

    if (strchr(url->str, '\\') == NULL && is_email_address(url->str))
    {
        *href = g_strdup_printf("mailto:%s", url->str);
        g_string_free(url, TRUE);
        return s + i + 1;
    }

    g_string_free(url, TRUE);
    return NULL;
}

typedef struct
{
    gchar* url;
    gchar* title;
} MdLinkRef;

static gchar* normalize_ref_key(const char* label, size_t len)
{
    GString* out = g_string_new(NULL);
    gboolean in_ws = FALSE;
    for (size_t i = 0; i < len; i++)
    {
        char c = label[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            in_ws = TRUE;
            continue;
        }
        if (in_ws && out->len > 0)
            g_string_append_c(out, ' ');
        in_ws = FALSE;
        g_string_append_c(out, c);
    }
    if (out->len == 0)
    {
        g_string_free(out, TRUE);
        return NULL;
    }
    gchar* key = g_utf8_casefold(out->str, (gssize)out->len);
    g_string_free(out, TRUE);
    return key;
}

static void parse_inline(GPtrArray* out, const char* s, const MdOptions* options, GHashTable* refs)
{
    GArray* tokens = g_array_new(FALSE, FALSE, sizeof(InlineToken));
    GArray* brackets = g_array_new(FALSE, FALSE, sizeof(Bracket));
    gboolean no_link_openers = TRUE;
    const char* p = s;
    const char* text_start = s;

    while (*p != '\0')
    {
        char c = *p;

        /* backslash escapes */
        if (c == '\\' && p[1] != '\0')
        {
            if (p[1] == '\n')
            {
                flush_text(tokens, text_start, p);
                token_add(tokens, node_new(MD_NODE_HARD_BREAK));
                p += 2;
                text_start = p;
                continue;
            }
            if (is_ascii_punctuation(p[1]))
            {
                flush_text(tokens, text_start, p);
                MdNode* node = node_new(MD_NODE_TEXT);
                node->literal = g_strndup(p + 1, 1);
                token_add(tokens, node);
                p += 2;
                text_start = p;
                continue;
            }
            p++;
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
                flush_text(tokens, text_start, p);
                gsize content_len = (size_t)(match - run);
                gchar* literal;

                gboolean all_space = TRUE;
                for (gsize k = 0; k < content_len; k++)
                {
                    if (run[k] != ' ')
                    {
                        all_space = FALSE;
                        break;
                    }
                }

                literal = g_strndup(run, content_len);

                /* Line ends inside a code span become spaces. */
                for (gsize k = 0; k < content_len; k++)
                {
                    if (literal[k] == '\n')
                        literal[k] = ' ';
                }

                if (!all_space && content_len >= 2 && literal[0] == ' ' &&
                    literal[content_len - 1] == ' ')
                {
                    literal[content_len - 1] = '\0';
                    memmove(literal, literal + 1, content_len - 1);
                    literal[content_len - 2] = '\0';
                }

                MdNode* node = node_new(MD_NODE_CODE_SPAN);
                node->literal = literal;
                token_add(tokens, node);
                p = match + run_len;
                text_start = p;
                continue;
            }
            /* no closing run of the same length: the run is literal text */
            p = run;
            continue;
        }

        /* character references */
        if (c == '&')
        {
            char buf[8];
            size_t consumed = try_character_reference(p, buf);
            if (consumed)
            {
                flush_text(tokens, text_start, p);
                MdNode* node = node_new(MD_NODE_TEXT);
                node->literal = g_strdup(buf);
                token_add(tokens, node);
                p += consumed;
                text_start = p;
                continue;
            }
        }

        /* emphasis delimiter runs */
        gboolean tilde =
            (c == '~' && options != NULL && md_options_has(options, MD_FEATURE_GFM_STRIKETHROUGH));
        if (c == '*' || c == '_' || tilde)
        {
            size_t start = (size_t)(p - s);
            size_t run = 0;
            while (p[run] == c)
                run++;
            flush_text(tokens, text_start, p);

            gunichar prev = char_before(s, start);
            gunichar next = utf8_char_at(p + run);
            gboolean left_flanking =
                !is_whitespace(next) &&
                (!is_punctuation(next) || (is_whitespace(prev) || is_punctuation(prev)));
            gboolean right_flanking =
                !is_whitespace(prev) &&
                (!is_punctuation(prev) || (is_whitespace(next) || is_punctuation(next)));

            InlineToken token = {NULL, c, (int)run, FALSE, FALSE};
            if (c == '*')
            {
                token.can_open = left_flanking;
                token.can_close = right_flanking;
            }
            else if (c == '~')
            {
                /* "Three or more tildes do not create a strikethrough." */
                token.can_open = left_flanking && run < 3;
                token.can_close = right_flanking && run < 3;
            }
            else
            {
                token.can_open = left_flanking && (!right_flanking || is_punctuation(prev));
                token.can_close = right_flanking && (!left_flanking || is_punctuation(next));
            }
            g_array_append_val(tokens, token);
            p += run;
            text_start = p;
            continue;
        }

        /* link or image: brackets are tracked on a stack so that code spans,
         * autolinks and raw tags take precedence over link grouping, and a
         * link label never contains another link. */
        if (c == '[')
        {
            flush_text(tokens, text_start, p);
            MdNode* open = node_new(MD_NODE_TEXT);
            open->literal = g_strdup("[");
            token_add(tokens, open);
            Bracket b = {tokens->len - 1, (size_t)(p - s) + 1, FALSE, FALSE};
            if (brackets->len > 0)
                g_array_index(brackets, Bracket, brackets->len - 1).bracket_after = TRUE;
            g_array_append_val(brackets, b);
            no_link_openers = FALSE;
            p++;
            text_start = p;
            continue;
        }
        if (c == '!' && p[1] == '[')
        {
            flush_text(tokens, text_start, p);
            MdNode* open = node_new(MD_NODE_TEXT);
            open->literal = g_strdup("![");
            token_add(tokens, open);
            Bracket b = {tokens->len - 1, (size_t)(p - s) + 2, TRUE, FALSE};
            if (brackets->len > 0)
                g_array_index(brackets, Bracket, brackets->len - 1).bracket_after = TRUE;
            g_array_append_val(brackets, b);
            p += 2;
            text_start = p;
            continue;
        }
        if (c == ']' && brackets->len > 0)
        {
            Bracket br = g_array_index(brackets, Bracket, brackets->len - 1);
            if (!br.is_image && no_link_openers)
            {
                flush_text(tokens, text_start, p);
                MdNode* lit = node_new(MD_NODE_TEXT);
                lit->literal = g_strdup("]");
                token_add(tokens, lit);
                g_array_remove_index(brackets, brackets->len - 1);
                p++;
                text_start = p;
                continue;
            }

            flush_text(tokens, text_start, p);

            char* url = NULL;
            char* title = NULL;
            const char* target_end = NULL;
            gboolean matched = FALSE;
            gboolean found_label = FALSE;
            size_t ref_start = 0;
            size_t ref_len = 0;

            if (p[1] == '(')
            {
                if (parse_link_target(p + 1, &url, &title, &target_end))
                    matched = TRUE;
            }

            if (!matched)
            {
                const char* q = p + 1;
                if (q[0] == '[')
                {
                    const char* ref_close = find_close_bracket(q + 1);
                    if (ref_close != NULL)
                    {
                        found_label = TRUE;
                        ref_start = (size_t)(q + 1 - s);
                        ref_len = (size_t)(ref_close - q - 1);
                        q = ref_close + 1;
                    }
                }
                if ((!found_label || ref_len == 0) && !br.bracket_after)
                {
                    found_label = TRUE;
                    ref_start = br.label_start;
                    ref_len = (size_t)(p - s) - br.label_start;
                }
                if (found_label && refs != NULL)
                {
                    gchar* key = normalize_ref_key(s + ref_start, ref_len);
                    MdLinkRef* ref = (key != NULL) ? g_hash_table_lookup(refs, key) : NULL;
                    g_free(key);
                    if (ref != NULL)
                    {
                        url = g_strdup(ref->url);
                        title = g_strdup(ref->title);
                        target_end = q;
                        matched = TRUE;
                    }
                }
            }

            if (matched)
            {
                GArray* label = g_array_new(FALSE, FALSE, sizeof(InlineToken));
                for (gsize k = br.token_index + 1; k < tokens->len; k++)
                    g_array_append_val(label, g_array_index(tokens, InlineToken, k));
                process_emphasis(label);

                MdNode* node = node_new(br.is_image ? MD_NODE_IMAGE : MD_NODE_LINK);
                node->literal = url;
                node->title = title;
                node->children = g_ptr_array_new();
                for (gsize k = 0; k < label->len; k++)
                {
                    InlineToken* t = &g_array_index(label, InlineToken, k);
                    if (t->node != NULL)
                    {
                        t->node->parent = node;
                        g_ptr_array_add(node->children, t->node);
                    }
                    else if (t->len > 0)
                    {
                        MdNode* text = node_new(MD_NODE_TEXT);
                        text->literal = delimiter_literal(t);
                        add_child(node, text);
                    }
                }
                if (node->children->len == 0 && found_label)
                {
                    gchar* shortcut = g_strndup(s + ref_start, ref_len);
                    parse_inline(node->children, shortcut, options, refs);
                    g_free(shortcut);
                }
                g_array_free(label, TRUE);

                /* The literal "[" / "![" token is consumed by the link. */
                md_node_free(g_array_index(tokens, InlineToken, br.token_index).node);
                g_array_set_size(tokens, (guint)br.token_index);
                InlineToken link_token = {node, 0, 0, FALSE, FALSE};
                g_array_append_val(tokens, link_token);
                g_array_remove_index(brackets, brackets->len - 1);
                if (!br.is_image)
                    no_link_openers = TRUE;
                p = target_end;
                text_start = p;
                continue;
            }

            g_array_remove_index(brackets, brackets->len - 1);
            MdNode* lit = node_new(MD_NODE_TEXT);
            lit->literal = g_strdup("]");
            token_add(tokens, lit);
            p++;
            text_start = p;
            continue;
        }

        /* CommonMark autolink */
        if (c == '<')
        {
            char* href = NULL;
            const char* end = try_autolink_angle(p, &href);
            if (end != NULL)
            {
                flush_text(tokens, text_start, p);
                MdNode* node = node_new(MD_NODE_LINK);
                node->literal = href;
                node->children = g_ptr_array_new();
                MdNode* text = node_new(MD_NODE_TEXT);
                text->literal = g_strndup(p + 1, (size_t)(end - p - 2));
                add_child(node, text);
                token_add(tokens, node);
                p = end;
                text_start = p;
                continue;
            }

            size_t tag_len = scan_tag(p, options);
            if (tag_len > 0)
            {
                flush_text(tokens, text_start, p);
                MdNode* node = node_new(MD_NODE_RAW_HTML);
                node->literal = g_strndup(p, tag_len);
                node->raw_html_filtered = options != NULL &&
                                          md_options_has(options, MD_FEATURE_GFM_RAW_HTML) &&
                                          is_disallowed_raw_tag(p, tag_len);
                token_add(tokens, node);
                p += tag_len;
                text_start = p;
                continue;
            }
        }

        /* GFM autolinks */
        if (options != NULL && md_options_has(options, MD_FEATURE_GFM_AUTOLINKS))
        {
            gboolean ctx_ok = (p == s) || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' ||
                              p[-1] == '*' || p[-1] == '_' || p[-1] == '~' || p[-1] == '(';
            if (ctx_ok)
            {
                gchar* href = NULL;
                const char* end = try_autolink(p, &href);
                if (end != NULL)
                {
                    flush_text(tokens, text_start, p);
                    MdNode* node = node_new(MD_NODE_LINK);
                    node->literal = href;
                    node->children = g_ptr_array_new();
                    MdNode* text = node_new(MD_NODE_TEXT);
                    text->literal = g_strndup(p, (size_t)(end - p));
                    add_child(node, text);
                    token_add(tokens, node);
                    p = end;
                    text_start = p;
                    continue;
                }
            }
        }

        /* line ends: two or more trailing spaces make a hard break */
        if (c == '\n')
        {
            const char* run_start = p;
            while (run_start > text_start && run_start[-1] == ' ')
                run_start--;
            flush_text(tokens, text_start, run_start);
            if ((size_t)(p - run_start) >= 2)
            {
                token_add(tokens, node_new(MD_NODE_HARD_BREAK));
            }
            else
            {
                MdNode* node = node_new(MD_NODE_TEXT);
                node->literal = g_strdup("\n");
                token_add(tokens, node);
            }
            p++;
            text_start = p;
            continue;
        }

        p++;
    }

    flush_text(tokens, text_start, p);

    process_emphasis(tokens);

    for (gsize i = 0; i < tokens->len; i++)
    {
        InlineToken* token = &g_array_index(tokens, InlineToken, i);
        if (token->node != NULL)
        {
            g_ptr_array_add(out, token->node);
        }
        else if (token->len > 0)
        {
            MdNode* text = node_new(MD_NODE_TEXT);
            text->literal = delimiter_literal(token);
            g_ptr_array_add(out, text);
        }
    }
    g_array_free(tokens, TRUE);
    g_array_free(brackets, TRUE);
}

static void resolve_inlines(MdNode* node, const MdOptions* options, GHashTable* refs)
{
    if (node->children == NULL)
        return;
    if ((node->kind == MD_NODE_PARAGRAPH || node->kind == MD_NODE_HEADING ||
         node->kind == MD_NODE_TABLE_CELL) &&
        node->literal != NULL)
    {
        parse_inline(node->children, node->literal, options, refs);
        g_free(node->literal);
        node->literal = NULL;
    }
    for (gsize i = 0; i < node->children->len; i++)
        resolve_inlines((MdNode*)g_ptr_array_index(node->children, i), options, refs);
}
/* ------------------------------------------------------------------ */
/* Block parser                                                        */
/* ------------------------------------------------------------------ */

static void parse_blocks(MdNode* container, char** lines, const gboolean* lazy, size_t n,
                         size_t indent, const MdOptions* options, GHashTable* refs,
                         gboolean* blank_sep);

static void parse_blockquote(MdNode* container, char** lines, size_t start, size_t end,
                             const MdOptions* options, GHashTable* refs)
{
    GPtrArray* stripped = g_ptr_array_new_with_free_func(g_free);
    GArray* inner_lazy = g_array_new(FALSE, FALSE, sizeof(gboolean));
    for (size_t i = start; i < end; i++)
    {
        const char* s = lines[i];
        size_t sp = leading_spaces(s);
        s += sp;
        gboolean is_lazy = FALSE;
        if (*s == '>')
        {
            s++;
            if (*s == ' ')
                s++;
        }
        else
        {
            /* A line inside a blockquote that carries no ">" marker continues
             * the inner paragraph (CommonMark lazy continuation). */
            is_lazy = TRUE;
        }
        g_ptr_array_add(stripped, g_strdup(s));
        g_array_append_val(inner_lazy, is_lazy);
    }

    MdNode* node = node_new(MD_NODE_BLOCKQUOTE);
    node->children = g_ptr_array_new();
    parse_blocks(node, (char**)stripped->pdata, (const gboolean*)inner_lazy->data, stripped->len, 0,
                 options, refs, NULL);
    g_ptr_array_free(stripped, TRUE);
    g_array_free(inner_lazy, TRUE);
    add_child(container, node);
}

static int html_block_type(const char* c);

/* Does this (already dedented) line start a block, which would end a
 * lazy paragraph continuation inside a list item? */
static gboolean starts_new_block(const char* c)
{
    if (c[0] == '\0')
        return FALSE;
    if (c[0] == '#')
        return TRUE;
    if (c[0] == '>')
        return TRUE;
    size_t mw;
    MdListType t;
    int num;
    char mk;
    if (leading_spaces(c) <= 3 && parse_list_marker(c, &mw, &t, &num, &mk, FALSE))
        return TRUE;
    if (is_thematic_break(c))
        return TRUE;
    char fence;
    size_t flen;
    size_t findent;
    const char* info;
    if (parse_fence(c, &fence, &flen, &findent, &info))
        return TRUE;
    int type = html_block_type(c);
    return type != 0 && type != 7;
}

/* Does the innermost content of a block quote marker line open a paragraph? */
static gboolean innermost_is_paragraph(const char* s)
{
    while (TRUE)
    {
        size_t sp = leading_spaces(s);
        if (sp > 3)
            return FALSE;
        size_t mw;
        MdListType t;
        int num;
        char mk;
        if (parse_list_marker(s, &mw, &t, &num, &mk, FALSE))
        {
            s += sp + mw;
            while (*s == ' ' || *s == '\t')
                s++;
            continue;
        }
        if (*s == '>')
        {
            s++;
            if (*s == ' ')
                s++;
            continue;
        }
        break;
    }
    if (is_blank_line(s))
        return FALSE;
    return !starts_new_block(s);
}

static size_t parse_list(MdNode* container, char** lines, const gboolean* lazy, size_t n,
                         size_t start, size_t indent, const MdOptions* options, GHashTable* refs)
{
    MdNode* list = node_new(MD_NODE_LIST);
    list->children = g_ptr_array_new();
    list->tight = TRUE;

    size_t base = indent + leading_spaces(lines[start]);
    size_t i = start;
    MdNode* item = NULL;
    GPtrArray* item_lines = NULL;
    GArray* item_lazy = NULL;
    size_t item_indent = 0;
    gboolean last_item_line_blank = FALSE;
    gboolean item_has_content = FALSE;

    while (i < n)
    {
        const char* line = lines[i];
        size_t lsp = leading_spaces(line);
        if (is_blank_line(line))
        {
            /* A list item may not begin with a blank line: a blank line right
             * after an empty marker line closes that item. */
            if (item != NULL && !item_has_content)
            {
                size_t k = i + 1;
                while (k < n && is_blank_line(lines[k]))
                    k++;
                if (k >= n)
                    break;
                size_t ksp = leading_spaces(lines[k]);
                size_t mw2;
                MdListType t2;
                int num2;
                char mk2;
                if (ksp >= base && ksp - base <= 3 &&
                    parse_list_marker(lines[k] + ksp, &mw2, &t2, &num2, &mk2, FALSE))
                {
                    list->tight = FALSE;
                    i++;
                    continue;
                }
                break;
            }

            size_t j = i + 1;
            while (j < n && is_blank_line(lines[j]))
                j++;
            if (j < n)
            {
                size_t jsp = leading_spaces(lines[j]);
                size_t mw2;
                MdListType t2;
                int num2;
                char mk2;
                gboolean continues =
                    (jsp >= base && jsp - base <= 3 &&
                     parse_list_marker(lines[j] + jsp, &mw2, &t2, &num2, &mk2, FALSE)) ||
                    (item != NULL && jsp >= item_indent);

                if (continues)
                {
                    if (item == NULL || jsp < item_indent)
                        list->tight = FALSE;
                    if (item != NULL)
                    {
                        g_ptr_array_add(item_lines, g_strdup(""));
                        gboolean bl = FALSE;
                        g_array_append_val(item_lazy, bl);
                    }
                    last_item_line_blank = TRUE;
                    i++;
                    continue;
                }
            }
            break;
        }

        if (lsp < base)
        {
            if (item != NULL && !last_item_line_blank && !starts_new_block(line))
            {
                g_ptr_array_add(item_lines, g_strdup(line + lsp));
                gboolean lzy = TRUE;
                g_array_append_val(item_lazy, lzy);
                last_item_line_blank = FALSE;
                item_has_content = TRUE;
                i++;
                continue;
            }
            break;
        }

        if (item != NULL && lsp >= item_indent)
        {
            g_ptr_array_add(item_lines, g_strdup(line + item_indent));
            gboolean lzy = (lazy != NULL && lazy[i]);
            g_array_append_val(item_lazy, lzy);
            last_item_line_blank = FALSE;
            if (!is_blank_line(line + item_indent))
                item_has_content = TRUE;
            i++;
            continue;
        }

        size_t mw;
        MdListType t;
        int num;
        char mk;
        if (is_thematic_break(line + lsp))
            break;
        if (lsp >= base && lsp - base <= 3 &&
            parse_list_marker(line + lsp, &mw, &t, &num, &mk, FALSE))
        {
            if (list->children->len == 0)
            {
                list->list_type = t;
                list->start = num;
                list->marker = mk;
            }
            else if (t != list->list_type || mk != list->marker)
                break;

            if (item != NULL)
            {
                gboolean item_blank_sep = FALSE;
                parse_blocks(item, (char**)item_lines->pdata, (const gboolean*)item_lazy->data,
                             item_lines->len, 0, options, refs, &item_blank_sep);
                if (item_blank_sep)
                    list->tight = FALSE;
                g_ptr_array_free(item_lines, TRUE);
                g_array_free(item_lazy, TRUE);
            }
            item = node_new(MD_NODE_ITEM);
            item->children = g_ptr_array_new();
            item_lines = g_ptr_array_new_with_free_func(g_free);
            item_lazy = g_array_new(FALSE, FALSE, sizeof(gboolean));

            /* Padding is the marker plus the spaces after it, capped at four;
             * five or more spaces (or none) fall back to a single space. */
            size_t spaces = 0;
            while (line[lsp + mw + spaces] == ' ' || line[lsp + mw + spaces] == '\t')
                spaces++;
            size_t padding = (spaces >= 1 && spaces <= 4) ? mw + spaces : mw + 1;
            item_indent = lsp + padding;

            const char* content = line + MIN(item_indent, strlen(line));
            gboolean checked = FALSE;
            if (options != NULL && md_options_has(options, MD_FEATURE_GFM_TASKLISTS) &&
                parse_task_marker(content, &checked))
            {
                item->task = TRUE;
                item->task_checked = checked;
                content += 3;
                while (*content == ' ' || *content == '\t')
                    content++;
            }
            gboolean marker_line_empty = is_blank_line(content);
            item_has_content = !marker_line_empty;
            g_ptr_array_add(item_lines, g_strdup(content));
            gboolean marker_lazy = FALSE;
            g_array_append_val(item_lazy, marker_lazy);
            last_item_line_blank = marker_line_empty;
            add_child(list, item);
            i++;
            continue;
        }

        if (item == NULL)
            break;

        if (item != NULL && lsp < item_indent && (!last_item_line_blank || !item_has_content) &&
            !starts_new_block(line))
        {
            g_ptr_array_add(item_lines, g_strdup(line + lsp));
            gboolean lzy = TRUE;
            g_array_append_val(item_lazy, lzy);
            last_item_line_blank = FALSE;
            item_has_content = TRUE;
            i++;
            continue;
        }

        break;
    }

    if (item != NULL)
    {
        gboolean item_blank_sep = FALSE;
        parse_blocks(item, (char**)item_lines->pdata, (const gboolean*)item_lazy->data,
                     item_lines->len, 0, options, refs, &item_blank_sep);
        if (item_blank_sep)
            list->tight = FALSE;
        g_ptr_array_free(item_lines, TRUE);
        g_array_free(item_lazy, TRUE);
    }

    add_child(container, list);
    return i - start;
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

    /* Split on unescaped pipes; GFM lets a cell pipe be escaped with a
     * backslash, and the escape is consumed before inline parsing. */
    GString* cell = g_string_new(NULL);
    for (size_t i = 0; i < len; i++)
    {
        if (line[i] == '\\' && line[i + 1] == '|')
        {
            g_string_append_c(cell, '|');
            i++;
            continue;
        }
        if (line[i] == '|')
        {
            gchar* trimmed = g_strdup(cell->str);
            g_strstrip(trimmed);
            g_ptr_array_add(cells, trimmed);
            g_string_set_size(cell, 0);
            continue;
        }
        g_string_append_c(cell, line[i]);
    }
    {
        gchar* trimmed = g_strdup(cell->str);
        g_strstrip(trimmed);
        g_ptr_array_add(cells, trimmed);
    }
    g_string_free(cell, TRUE);
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
        gboolean right = (clen > 1 && c[clen - 1] == ':');
        gsize start = left ? 1 : 0;
        gsize end = right ? clen - 1 : clen;
        if (end <= start)
        {
            g_ptr_array_free(cells, TRUE);
            return 0;
        }
        for (gsize k = start; k < end; k++)
        {
            if (c[k] != '-')
            {
                g_ptr_array_free(cells, TRUE);
                return 0;
            }
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
    cell->literal = g_strdup(text);
    add_child(row, cell);
}

static size_t scan_tag(const char* s, const MdOptions* options)
{
    if (s[0] != '<')
        return 0;
    size_t i = 1;

    if (s[i] == '!' || s[i] == '?')
    {
        if (s[i] == '!' && strncmp(s + i, "![CDATA[", 8) == 0)
        {
            i += 8;
            while (s[i] != '\0')
            {
                if (s[i] == ']' && s[i + 1] == ']' && s[i + 2] == '>')
                    return i + 3;
                i++;
            }
            return 0;
        }
        /* GFM follows the HTML5 comment grammar: the text must not start with
         * '>' or '->', must not end with '-', and must not contain '--'. */
        if (s[i] == '!' && s[i + 1] == '-' && s[i + 2] == '-' && options != NULL &&
            md_options_has(options, MD_FEATURE_GFM_RAW_HTML))
        {
            size_t text_start = i + 3;
            const char* close = strstr(s + text_start, "-->");
            if (close == NULL)
                return 0;
            size_t text_len = (size_t)(close - (s + text_start));
            if (text_len == 0)
                return 0;
            if (s[text_start] == '>')
                return 0;
            if (text_len >= 2 && s[text_start] == '-' && s[text_start + 1] == '>')
                return 0;
            if (s[(size_t)(close - s) - 1] == '-')
                return 0;
            for (size_t k = text_start; k + 1 < (size_t)(close - s); k++)
            {
                if (s[k] == '-' && s[k + 1] == '-')
                    return 0;
            }
            return (size_t)(close - s) + 3;
        }
        while (s[i] != '\0' && s[i] != '>')
            i++;
        return (s[i] == '>') ? i + 1 : 0;
    }

    gboolean closing = (s[i] == '/');
    if (closing)
        i++;
    if (!g_ascii_isalpha(s[i]))
        return 0;
    i++;
    while (g_ascii_isalnum(s[i]) || s[i] == '-')
        i++;

    if (closing)
    {
        i += skip_tag_whitespace(s + i);
        return (s[i] == '>') ? i + 1 : 0;
    }

    while (TRUE)
    {
        if (s[i] == '>')
            return i + 1;
        if (s[i] == '/')
            return (s[i + 1] == '>') ? i + 2 : 0;
        if (!is_html_space(s[i]))
            return 0;

        i += skip_tag_whitespace(s + i);
        if (s[i] == '>')
            return i + 1;
        if (s[i] == '/')
            return (s[i + 1] == '>') ? i + 2 : 0;

        /* attribute name */
        if (!g_ascii_isalpha(s[i]) && s[i] != '_' && s[i] != ':')
            return 0;
        i++;
        while (g_ascii_isalnum(s[i]) || s[i] == '_' || s[i] == '.' || s[i] == ':' || s[i] == '-')
            i++;

        size_t adv = skip_tag_whitespace(s + i);
        if (s[i + adv] != '=')
            continue; /* bare attribute; the whitespace is re-read at the top */
        i = i + adv + 1;
        i += skip_tag_whitespace(s + i);

        if (s[i] == '"' || s[i] == '\'')
        {
            char quote = s[i++];
            while (s[i] != '\0' && s[i] != quote)
                i++;
            if (s[i] != quote)
                return 0;
            i++;
        }
        else
        {
            if (s[i] == '\0')
                return 0;
            while (s[i] != '\0' && !is_html_space(s[i]) && s[i] != '"' && s[i] != '\'' &&
                   s[i] != '=' && s[i] != '<' && s[i] != '>' && s[i] != '`')
                i++;
        }
    }
}

/* GFM "tagfilter": these tags are escaped instead of passed through raw. */
static const char* DISALLOWED_RAW_TAGS[] = {"title",   "textarea", "style",  "xmp",      "iframe",
                                            "noembed", "noframes", "script", "plaintext"};

static gboolean is_disallowed_raw_tag(const char* s, size_t len)
{
    size_t name_start = 1;
    size_t name_end = name_start;
    while (name_end < len && (g_ascii_isalnum(s[name_end]) || s[name_end] == '-'))
        name_end++;
    if (name_end == name_start)
        return FALSE;
    for (size_t i = 0; i < G_N_ELEMENTS(DISALLOWED_RAW_TAGS); i++)
    {
        size_t n = strlen(DISALLOWED_RAW_TAGS[i]);
        if (n == name_end - name_start &&
            g_ascii_strncasecmp(s + name_start, DISALLOWED_RAW_TAGS[i], n) == 0)
            return TRUE;
    }
    return FALSE;
}

static const char* HTML_BLOCK_TAGS[] = {
    "address",  "article",  "aside",    "base",       "basefont", "blockquote", "body",   "caption",
    "center",   "col",      "colgroup", "dd",         "details",  "dialog",     "dir",    "div",
    "dl",       "dt",       "fieldset", "figcaption", "figure",   "footer",     "form",   "frame",
    "frameset", "h1",       "h2",       "h3",         "h4",       "h5",         "h6",     "head",
    "header",   "hr",       "html",     "iframe",     "legend",   "li",         "link",   "main",
    "menu",     "menuitem", "nav",      "noframes",   "ol",       "optgroup",   "option", "p",
    "param",    "search",   "section",  "summary",    "table",    "tbody",      "td",     "tfoot",
    "th",       "thead",    "title",    "tr",         "track",    "ul"};

static gboolean is_html_block_tag(const char* name, size_t len)
{
    for (size_t i = 0; i < G_N_ELEMENTS(HTML_BLOCK_TAGS); i++)
    {
        if (strlen(HTML_BLOCK_TAGS[i]) == len &&
            g_ascii_strncasecmp(name, HTML_BLOCK_TAGS[i], len) == 0)
            return TRUE;
    }
    return FALSE;
}

static int html_block_type(const char* c)
{
    if (c[0] != '<')
        return 0;

    if (c[1] == '!' && c[2] == '-')
        return 2;
    if (c[1] == '?')
        return 3;
    if (c[1] == '!' && strncmp(c, "<![CDATA[", 9) == 0)
        return 5;
    if (c[1] == '!' && g_ascii_isalpha(c[2]))
        return 4;

    static const char* RAW_PREFIXES[] = {"pre", "script", "style", "textarea"};
    for (size_t k = 0; k < G_N_ELEMENTS(RAW_PREFIXES); k++)
    {
        size_t len = strlen(RAW_PREFIXES[k]);
        if (g_ascii_strncasecmp(c + 1, RAW_PREFIXES[k], len) != 0)
            continue;
        char next = c[1 + len];
        if (next == ' ' || next == '\t' || next == '>' || next == '\0' ||
            (next == '/' && c[2 + len] == '>'))
            return 1;
    }

    size_t i = 1;
    if (c[i] == '/')
        i++;
    if (!g_ascii_isalpha(c[i]))
        return 0;
    size_t name_start = i;
    while (g_ascii_isalnum(c[i]) || c[i] == '-')
        i++;

    gboolean self_closing = (c[i] == '/' && c[i + 1] == '>');
    if (self_closing || c[i] == ' ' || c[i] == '\t' || c[i] == '>' || c[i] == '\0')
    {
        if (is_html_block_tag(c + name_start, i - name_start))
            return 6;
    }

    size_t tag_len = scan_tag(c, NULL);
    if (tag_len > 0)
    {
        const char* rest = c + tag_len;
        while (*rest == ' ' || *rest == '\t')
            rest++;
        if (*rest == '\0')
            return 7;
    }

    return 0;
}

static gboolean html_block_ends(int type, const char* line)
{
    switch (type)
    {
    case 1:
        return strstr(line, "</pre") != NULL || strstr(line, "</script") != NULL ||
               strstr(line, "</style") != NULL || strstr(line, "</textarea") != NULL;
    case 2:
        return strstr(line, "-->") != NULL;
    case 3:
        return strstr(line, "?>") != NULL;
    case 5:
        return strstr(line, "]]>") != NULL;
    case 6:
    case 7:
        return is_blank_line(line);
    default:
        return strchr(line, '>') != NULL;
    }
}

static void link_ref_free(gpointer p)
{
    MdLinkRef* ref = (MdLinkRef*)p;
    g_free(ref->url);
    g_free(ref->title);
    g_free(ref);
}

/* Returns the number of lines consumed by a link reference definition at line i,
 * or 0 when the line does not start one. */
static void skip_spaces_at(const char* s, size_t* pos)
{
    while (s[*pos] == ' ' || s[*pos] == '\t')
        (*pos)++;
}

/* cmark's "spnl": spaces, at most one line break, then spaces. */
static void skip_spaces_and_one_line(const char* s, size_t* pos)
{
    skip_spaces_at(s, pos);
    if (s[*pos] == '\n')
    {
        (*pos)++;
        skip_spaces_at(s, pos);
    }
}

static gboolean at_line_end(const char* s, size_t pos)
{
    return s[pos] == '\0' || s[pos] == '\n';
}

/* Line content with the container indent removed. Lines carrying fewer leading
 * spaces than the indent (blank lines, lazy continuations) are returned
 * unchanged instead of being read past their terminator. */
static const char* strip_indent(const char* line, size_t indent)
{
    return line + MIN(indent, leading_spaces(line));
}

static size_t try_link_ref(char** lines, size_t n, size_t i, size_t indent, GHashTable* refs,
                           gboolean register_ref)
{
    if (i >= n)
        return 0;

    GString* buf = g_string_new(lines[i] + indent);
    for (size_t k = i + 1; k < n && !is_blank_line(strip_indent(lines[k], indent)); k++)
    {
        g_string_append_c(buf, '\n');
        g_string_append(buf, strip_indent(lines[k], indent));
    }
    const char* s = buf->str;

    size_t pos = 0;
    if (s[pos] != '[')
    {
        g_string_free(buf, TRUE);
        return 0;
    }
    pos++;
    size_t label_start = pos;
    size_t label_len = 0;
    while (s[pos] != '\0' && s[pos] != '[' && s[pos] != ']')
    {
        if (s[pos] == '\\')
        {
            pos++;
            label_len++;
            if (is_ascii_punctuation(s[pos]))
            {
                pos++;
                label_len++;
            }
        }
        else
        {
            pos++;
            label_len++;
        }
    }
    if (s[pos] != ']' || label_len == 0 || s[pos + 1] != ':')
    {
        g_string_free(buf, TRUE);
        return 0;
    }
    gchar* label = normalize_ref_key(s + label_start, label_len);
    if (label == NULL)
    {
        g_string_free(buf, TRUE);
        return 0;
    }
    pos += 2;

    skip_spaces_and_one_line(s, &pos);

    /* link destination */
    GString* dest = g_string_new(NULL);
    if (s[pos] == '<')
    {
        pos++;
        while (s[pos] != '\0' && s[pos] != '>' && s[pos] != '\n')
            append_dest_text(dest, s, &pos);
        if (s[pos] == '>')
        {
            pos++;
        }
        else
        {
            g_string_free(dest, TRUE);
            g_free(label);
            g_string_free(buf, TRUE);
            return 0;
        }
    }
    else
    {
        int depth = 0;
        while (s[pos] != '\0')
        {
            char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\n')
                break;
            if (c == ')')
            {
                if (depth == 0)
                    break;
                depth--;
                g_string_append_c(dest, c);
                pos++;
                continue;
            }
            if (c == '(')
            {
                if (depth >= 32)
                    break;
                depth++;
                g_string_append_c(dest, c);
                pos++;
                continue;
            }
            append_dest_text(dest, s, &pos);
        }
        if (dest->len == 0 || depth != 0)
        {
            g_string_free(dest, TRUE);
            g_free(label);
            g_string_free(buf, TRUE);
            return 0;
        }
    }
    gchar* raw_url = g_string_free(dest, FALSE);
    gchar* url = uri_escape(raw_url, strlen(raw_url));
    g_free(raw_url);

    if (url == NULL)
    {
        g_free(label);
        g_string_free(buf, TRUE);
        return 0;
    }

    /* optional title */
    size_t before_title = pos;
    skip_spaces_and_one_line(s, &pos);
    gchar* title = NULL;
    gboolean has_title = FALSE;
    char open = s[pos];
    if (pos != before_title && (open == '"' || open == '\'' || open == '('))
    {
        char close = (open == '(') ? ')' : open;
        GString* t = g_string_new(NULL);
        size_t r = pos + 1;
        int depth = (open == '(') ? 1 : 0;
        while (s[r] != '\0')
        {
            if (open == '(')
            {
                if (s[r] == '(')
                {
                    depth++;
                }
                else if (s[r] == ')')
                {
                    depth--;
                    if (depth == 0)
                        break;
                }
            }
            else if (s[r] == close)
            {
                break;
            }
            append_dest_text(t, s, &r);
        }
        if (s[r] == close)
        {
            title = g_string_free(t, FALSE);
            pos = r + 1;
            has_title = TRUE;
        }
        else
        {
            g_string_free(t, TRUE);
        }
    }
    if (!has_title)
        pos = before_title;

    skip_spaces_at(s, &pos);
    if (!at_line_end(s, pos))
    {
        if (has_title)
        {
            pos = before_title;
            skip_spaces_at(s, &pos);
            if (!at_line_end(s, pos))
            {
                g_free(label);
                g_free(url);
                g_free(title);
                g_string_free(buf, TRUE);
                return 0;
            }
            g_free(title);
            title = NULL;
        }
        else
        {
            g_free(label);
            g_free(url);
            g_free(title);
            g_string_free(buf, TRUE);
            return 0;
        }
    }

    size_t used_lines = 1;
    for (size_t r = 0; r < pos; r++)
    {
        if (s[r] == '\n')
            used_lines++;
    }

    if (register_ref)
    {
        if (!g_hash_table_contains(refs, label))
        {
            MdLinkRef* ref = g_new0(MdLinkRef, 1);
            ref->url = url;
            ref->title = title;
            g_hash_table_insert(refs, label, ref);
        }
        else
        {
            g_free(label);
            g_free(url);
            g_free(title);
        }
    }
    else
    {
        g_free(label);
        g_free(url);
        g_free(title);
    }
    g_string_free(buf, TRUE);
    return used_lines;
}

static void parse_blocks(MdNode* container, char** lines, const gboolean* lazy, size_t n,
                         size_t indent, const MdOptions* options, GHashTable* refs,
                         gboolean* blank_sep)
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
            if (blank_sep != NULL && container->children != NULL && container->children->len > 0)
                *blank_sep = TRUE;
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
                if (lsp >= 4)
                {
                    g_string_append(buf, l + 4);
                    g_string_append_c(buf, '\n');
                    i++;
                }
                else if (is_blank_line(l))
                {
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
        size_t fence_indent;
        const char* info;
        if (parse_fence(content, &fence_char, &fence_len, &fence_indent, &info))
        {
            GPtrArray* fence_lines = g_ptr_array_new_with_free_func(g_free);
            i++;
            while (i < n && !is_fence_close(lines[i], fence_char, fence_len))
            {
                g_ptr_array_add(fence_lines, g_strdup(lines[i]));
                i++;
            }
            if (i < n)
                i++; /* skip closing fence */

            GString* buf = g_string_new(NULL);
            for (guint k = 0; k < fence_lines->len; k++)
            {
                const char* l = (const char*)g_ptr_array_index(fence_lines, k);
                g_string_append(buf, l + MIN(leading_spaces(l), fence_indent));
                g_string_append_c(buf, '\n');
            }
            g_ptr_array_free(fence_lines, TRUE);

            MdNode* node = node_new(MD_NODE_CODE_BLOCK);
            node->literal = g_strdup(buf->str);
            g_string_free(buf, TRUE);
            /* The renderer supplies the final newline, so drop exactly one. */
            size_t lit_len = strlen(node->literal);
            if (lit_len > 0 && node->literal[lit_len - 1] == '\n')
                node->literal[lit_len - 1] = '\0';

            GString* info_buf = g_string_new(NULL);
            size_t k = 0;
            while (info[k] != '\0')
                append_dest_text(info_buf, info, &k);
            gchar* info_trim = g_string_free(info_buf, FALSE);
            g_strstrip(info_trim);
            /* cmark uses only the first word of the info string as the class */
            for (gsize word = 0; info_trim[word] != '\0'; word++)
            {
                if (info_trim[word] == ' ' || info_trim[word] == '\t')
                {
                    info_trim[word] = '\0';
                    break;
                }
            }
            if (info_trim[0] != '\0')
                node->info = info_trim;
            else
                g_free(info_trim);

            add_child(container, node);
            continue;
        }

        /* link reference definitions are consumed, not rendered */
        size_t ref_lines = try_link_ref(lines, n, i, indent + sp, refs, TRUE);
        if (ref_lines > 0)
        {
            i += ref_lines;
            continue;
        }

        /* strip up to 3 leading spaces for the remaining block classifiers */
        content += sp;

        /* HTML block (CommonMark types 1-7) */
        int html_type = html_block_type(content);
        if (html_type != 0)
        {
            GString* buf = g_string_new(NULL);
            while (i < n)
            {
                const char* l = strip_indent(lines[i], indent);
                if (html_type == 6 || html_type == 7)
                {
                    if (is_blank_line(l))
                        break;
                }
                g_string_append(buf, l);
                g_string_append_c(buf, '\n');
                i++;
                if (html_type != 6 && html_type != 7 && html_block_ends(html_type, l))
                    break;
            }
            MdNode* node = node_new(MD_NODE_HTML_BLOCK);
            node->literal = g_strdup(buf->str);
            node->raw_html_filtered = options != NULL &&
                                      md_options_has(options, MD_FEATURE_GFM_RAW_HTML) &&
                                      (html_type == 2 || html_type == 6);
            g_string_free(buf, TRUE);
            add_child(container, node);
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
            node->literal = g_strdup(text);
            strip_surrounding_whitespace(node->literal);
            strip_atx_closing(node->literal);
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
            gboolean open_para = FALSE;
            while (i < n)
            {
                const char* l = lines[i];
                size_t lsp = leading_spaces(l);
                if (l[lsp] == '>')
                {
                    const char* rest = l + lsp + 1;
                    if (*rest == ' ')
                        rest++;
                    open_para = innermost_is_paragraph(rest);
                    i++;
                }
                else if (open_para && !is_blank_line(l) && !starts_new_block(l))
                    i++; /* lazy continuation line */
                else
                    break;
            }
            parse_blockquote(container, lines, bq_start, i, options, refs);
            continue;
        }

        /* GFM table (feature-gated): header row followed by a delimiter row */
        if (options != NULL && md_options_has(options, MD_FEATURE_GFM_TABLES) && i + 1 < n &&
            strchr(content, '|') != NULL)
        {
            GPtrArray* header_cells = g_ptr_array_new_with_free_func(g_free);
            MdAlign* aligns = NULL;
            gsize header_cols = parse_table_cells(content, header_cells) ? header_cells->len : 0;
            gsize delim_cols = 0;
            if (header_cols > 0)
            {
                aligns = g_new0(MdAlign, header_cols);
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
                    if (is_blank_line(bl) || starts_new_block(bl))
                        break;
                    GPtrArray* cells = g_ptr_array_new_with_free_func(g_free);
                    if (!parse_table_cells(bl, cells))
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
                g_free(aligns);
                continue;
            }
            g_ptr_array_free(header_cells, TRUE);
            g_free(aligns);
        }

        /* list */
        size_t mw;
        MdListType ltype;
        int lnum;
        char mk;
        if (parse_list_marker(content, &mw, &ltype, &lnum, &mk, FALSE))
        {
            size_t used = parse_list(container, lines, lazy, n, i, indent, options, refs);
            i += used;
            continue;
        }

        /* paragraph, or a setext heading when the next line is an underline */
        GString* text_buf = g_string_new(content);
        MdNode* block = NULL;
        i++;
        while (i < n)
        {
            const char* l = lines[i];
            size_t lsp = leading_spaces(l);
            const char* lc = l + lsp;

            if (is_blank_line(lc))
                break;

            gboolean is_lazy = (lazy != NULL && lazy[i]);

            int setext_level = (!is_lazy && lsp <= 3) ? setext_underline_level(lc) : 0;
            if (setext_level > 0)
            {
                block = node_new(MD_NODE_HEADING);
                block->level = setext_level;
                block->children = g_ptr_array_new();
                block->literal = g_strdup(text_buf->str);
                strip_surrounding_whitespace(block->literal);
                i++;
                break;
            }

            /* HTML blocks of type 7 cannot interrupt a paragraph; everything
             * else that opens a block does. A line indented by 4+ spaces is
             * a lazy continuation. */
            int cont_html = html_block_type(lc);
            gboolean interrupts_list = parse_list_marker(lc, &mw, &ltype, &lnum, &mk, TRUE) &&
                                       (ltype == MD_LIST_UNORDERED || lnum == 1);
            if (!is_lazy && lsp < 4 &&
                (parse_atx_heading(lc, &level, &text) || is_thematic_break(lc) || *lc == '>' ||
                 interrupts_list ||
                 parse_fence(lc, &fence_char, &fence_len, &fence_indent, &info) ||
                 (cont_html != 0 && cont_html != 7)))
                break;

            g_string_append_c(text_buf, '\n');
            g_string_append(text_buf, lc);
            i++;
        }

        if (block == NULL)
        {
            block = node_new(MD_NODE_PARAGRAPH);
            block->children = g_ptr_array_new();
            block->literal = g_strdup(text_buf->str);
            gsize len = strlen(block->literal);
            while (len > 0 && (block->literal[len - 1] == ' ' || block->literal[len - 1] == '\t'))
                block->literal[--len] = '\0';
        }
        g_string_free(text_buf, TRUE);
        add_child(container, block);
    }
}

/* ------------------------------------------------------------------ */
/* HTML renderer                                                       */
/* ------------------------------------------------------------------ */

static void render_inline_children(const MdNode* node, GString* out);
/* cmark escapes only &, <, > and "; apostrophes stay literal. */
static gchar* escape_href(const char* s)
{
    static const char safe[] = "!#$%()*+,-./:;=?@_~";
    GString* out = g_string_new(NULL);
    for (const char* p = s; *p != '\0'; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c == '&')
            g_string_append(out, "&amp;");
        else if (c == '\'')
            g_string_append(out, "&#x27;");
        else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 strchr(safe, c) != NULL)
            g_string_append_c(out, (gchar)c);
        else
            g_string_append_printf(out, "%%%02X", c);
    }
    return g_string_free(out, FALSE);
}

static void collect_plain_text(const MdNode* node, GString* out)
{
    if (node->kind == MD_NODE_TEXT || node->kind == MD_NODE_CODE_SPAN)
    {
        if (node->literal != NULL)
            g_string_append(out, node->literal);
        return;
    }
    if (node->children != NULL)
        for (guint i = 0; i < node->children->len; i++)
            collect_plain_text(g_ptr_array_index(node->children, i), out);
}

static gchar* html_escape(const char* s, ssize_t len)
{
    if (len < 0)
        len = (ssize_t)strlen(s);
    GString* out = g_string_new(NULL);
    for (ssize_t i = 0; i < len; i++)
    {
        switch (s[i])
        {
        case '&':
            g_string_append(out, "&amp;");
            break;
        case '<':
            g_string_append(out, "&lt;");
            break;
        case '>':
            g_string_append(out, "&gt;");
            break;
        case '"':
            g_string_append(out, "&quot;");
            break;
        default:
            g_string_append_c(out, s[i]);
            break;
        }
    }
    return g_string_free(out, FALSE);
}

static void append_filtered_raw(GString* out, const char* s)
{
    for (size_t i = 0; s[i] != '\0'; i++)
    {
        if (s[i] == '<' && is_disallowed_raw_tag(s + i, strlen(s + i)))
        {
            g_string_append(out, "&lt;");
            continue;
        }
        g_string_append_c(out, s[i]);
    }
}

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
            /* cmark separates a tight item's paragraph from a following
             * block (e.g. a nested list) with a newline. */
            if (node->parent->children->len > 1 &&
                g_ptr_array_index(node->parent->children, node->parent->children->len - 1) != node)
                g_string_append_c(out, '\n');
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
    {
        gboolean tight =
            (node->parent != NULL && node->parent->kind == MD_NODE_LIST && node->parent->tight);
        g_string_append(out, "<li>");
        if (node->children->len > 0 &&
            (!tight ||
             ((const MdNode*)g_ptr_array_index(node->children, 0))->kind != MD_NODE_PARAGRAPH))
            g_string_append_c(out, '\n');
        if (node->task)
            g_string_append(out, node->task_checked
                                     ? "<input checked=\"\" disabled=\"\" type=\"checkbox\"> "
                                     : "<input disabled=\"\" type=\"checkbox\"> ");
        for (guint i = 0; i < node->children->len; i++)
            render_html(g_ptr_array_index(node->children, i), out);
        g_string_append(out, "</li>\n");
        break;
    }

    case MD_NODE_CODE_BLOCK:
    {
        gchar* escaped = html_escape(node->literal, -1);
        if (node->info != NULL)
        {
            gchar* esc_info = html_escape(node->info, -1);
            g_string_append_printf(out, "<pre><code class=\"language-%s\">", esc_info);
            g_free(esc_info);
        }
        else
        {
            g_string_append(out, "<pre><code>");
        }
        g_string_append(out, escaped);
        if (escaped[0] != '\0')
            g_string_append_c(out, '\n');
        g_string_append(out, "</code></pre>\n");
        g_free(escaped);
        break;
    }

    case MD_NODE_THEMATIC_BREAK:
        g_string_append(out, "<hr />\n");
        break;

    case MD_NODE_HTML_BLOCK:
        if (node->raw_html_filtered)
            append_filtered_raw(out, node->literal);
        else
            g_string_append(out, node->literal);
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
            gchar* escaped = html_escape(child->literal, -1);
            g_string_append(out, escaped);
            g_free(escaped);
            break;
        }
        case MD_NODE_CODE_SPAN:
        {
            gchar* escaped = html_escape(child->literal, -1);
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
        case MD_NODE_STRIKETHROUGH:
            g_string_append(out, "<del>");
            render_inline_children(child, out);
            g_string_append(out, "</del>");
            break;
        case MD_NODE_LINK:
        {
            gchar* url = escape_href(child->literal);
            if (child->title != NULL)
            {
                gchar* title = html_escape(child->title, -1);
                g_string_append_printf(out, "<a href=\"%s\" title=\"%s\">", url, title);
                g_free(title);
            }
            else
            {
                g_string_append_printf(out, "<a href=\"%s\">", url);
            }
            g_free(url);
            render_inline_children(child, out);
            g_string_append(out, "</a>");
            break;
        }
        case MD_NODE_HARD_BREAK:
            g_string_append(out, "<br />\n");
            break;
        case MD_NODE_RAW_HTML:
            if (child->raw_html_filtered)
                g_string_append_printf(out, "&lt;%s", child->literal + 1);
            else
                g_string_append(out, child->literal);
            break;
        case MD_NODE_IMAGE:
        {
            gchar* url = escape_href(child->literal);
            GString* alt = g_string_new(NULL);
            collect_plain_text(child, alt);
            gchar* esc_alt = html_escape(alt->str, -1);
            g_string_append_printf(out, "<img src=\"%s\" alt=\"%s\"", url, esc_alt);
            g_free(url);
            g_free(esc_alt);
            g_string_free(alt, TRUE);
            if (child->title != NULL)
            {
                gchar* title = html_escape(child->title, -1);
                g_string_append_printf(out, " title=\"%s\"", title);
                g_free(title);
            }
            g_string_append(out, " />");
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

/* CommonMark counts a leading tab as the spaces needed to reach the next
 * multiple of four; internal tabs stay literal. */
static void expand_indent_tabs(gchar** lines, gsize n)
{
    for (gsize i = 0; i < n; i++)
    {
        GString* out = g_string_new(NULL);
        size_t col = 0;
        gboolean indent_ctx = TRUE;
        for (size_t j = 0; lines[i][j] != '\0'; j++)
        {
            char c = lines[i][j];
            if (c == '\t' && indent_ctx)
            {
                size_t add = 4 - (col % 4);
                for (size_t k = 0; k < add; k++)
                    g_string_append_c(out, ' ');
                col += add;
            }
            else
            {
                g_string_append_c(out, c);
                col++;
                indent_ctx = (c == '>' || c == '-' || c == '*' || c == '+' || c == '.' ||
                              g_ascii_isdigit(c) || c == ' ');
            }
        }
        g_free(lines[i]);
        lines[i] = g_strdup(out->str);
        g_string_free(out, TRUE);
    }
}

MdNode* md_parse(const gchar* markdown, const MdOptions* options, GError** error)
{
    if (markdown == NULL)
    {
        g_set_error(error, g_quark_from_static_string(MD_PARSER_ERROR_DOMAIN),
                    MD_PARSER_ERROR_INVALID_INPUT, "Markdown input is NULL");
        return NULL;
    }

    MdNode* doc = node_new(MD_NODE_DOCUMENT);
    doc->children = g_ptr_array_new();

    gchar** lines = g_strsplit(markdown, "\n", -1);
    gsize n = g_strv_length(lines);
    if (n > 0 && lines[n - 1][0] == '\0')
        n--; /* a trailing newline is not an extra blank line */
    for (gsize i = 0; i < n; i++)
    {
        gsize l = strlen(lines[i]);
        if (l > 0 && lines[i][l - 1] == '\r')
            lines[i][l - 1] = '\0'; /* strip CR from CRLF line endings */
    }
    expand_indent_tabs(lines, n);
    GHashTable* refs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, link_ref_free);
    parse_blocks(doc, lines, NULL, n, 0, options, refs, NULL);
    resolve_inlines(doc, options, refs);
    g_hash_table_destroy(refs);
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