#include <salts/markdown_html.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { STUN_HTML_MAX_DEPTH = 64 };

typedef struct stun_html_writer {
    const salts_md_rich_document_view *doc;
    char *out;
    size_t length;
    size_t capacity;
    salts_md_status error;
    salts_md_html_policy policy;
    const uint8_t *table_alignment; /* borrowed TABLE metadata, scoped */
    size_t table_columns;
    size_t table_column;
} stun_html_writer;

static void html_bytes(stun_html_writer *w, const char *bytes, size_t len) {
    if (w->error != SALTS_MD_OK) return;
    if ((!bytes && len) || len > SIZE_MAX - w->length) {
        w->error = SALTS_MD_CAPACITY_EXCEEDED;
        return;
    }
    if (w->out) {
        if (len > w->capacity - w->length) {
            w->error = SALTS_MD_CAPACITY_EXCEEDED;
            return;
        }
        if (len) memcpy(w->out + w->length, bytes, len);
    }
    w->length += len;
}
static void html_str(stun_html_writer *w, const char *str) {
    html_bytes(w,str,strlen(str));
}

static void html_escaped(stun_html_writer *w,
                         const char *s, size_t len, bool attribute) {
    (void)attribute; /* identical quote escaping for text and attributes */
    if (!s && len) { w->error = SALTS_MD_INVALID_ARGUMENT; return; }
    for (size_t i=0;i<len && w->error==SALTS_MD_OK;++i) {
        switch(s[i]) {
        case '&': html_str(w,"&amp;"); break;
        case '<': html_str(w,"&lt;"); break;
        case '>': html_str(w,"&gt;"); break;
        case '"':
            /* cmark's HTML text contract escapes literal double quotes in
             * text and code as well as in attributes (CommonMark #352).
             * The same bounded writer/measurement path owns all output. */
            html_str(w,"&quot;");
            break;
        default: html_bytes(w,s+i,1u); break;
        }
    }
}

static bool html_ascii_equal(const char *bytes, size_t len, const char *key) {
    const size_t n = strlen(key);
    if (n != len) return false;
    for (size_t i=0;i<n;++i) {
        const unsigned char c=(unsigned char)bytes[i];
        const unsigned char k=(unsigned char)key[i];
        const unsigned char low=(c>='A' && c<='Z') ? c-'A'+'a' : c;
        if(low!=k) return false;
    }
    return true;
}

/* A fully DECODED destination must be validated from FIRST through LAST
 * byte. Do not return early after "http:", '/' '?' or '#': HTML5 entities
 * can create ASCII control characters late in a URL, even after a seemingly
 * safe scheme or path separator. Early acceptance would permit those bytes
 * inside a quoted href despite the documented fail-closed policy.
 *
 * A colon prior to a path/query/fragment separator declares an explicit
 * scheme and must match the known allowlist. Other colons are relative URL
 * payload. UTF-8 non-ASCII octets are percent encoded only AFTER admission.
 */
bool salts_md_html_safe_href(const char *s, size_t n) {
    if(!s && n)return false;
    bool seen_separator=false;
    size_t scheme_colon=SIZE_MAX;
    for(size_t i=0u;i<n;++i) {
        const unsigned char c=(unsigned char)s[i];
        /* Literal ASCII space is safe only after %20 normalization.
         * All other C0 controls, including TAB/CR/LF, remain rejected. */
        if(c<0x20u || c==0x7Fu)return false;
        if(c=='/' || c=='?' || c=='#')seen_separator=true;
        if(c==':' && !seen_separator && scheme_colon==SIZE_MAX)
            scheme_colon=i;
    }
    if(scheme_colon==SIZE_MAX)return true;
    /* A colon only declares a URL scheme when the FIRST path component
     * matches RFC3986 scheme syntax: ALPHA *( ALPHA / DIGIT / "+" /
     * "-" / "." ). In "foo):" or a literal decoded closing parenthesis,
     * the colon is relative-path punctuation and is not a scheme bypass.
     * Keep the FULL byte scan above, and reject genuine unknown schemes
     * (javascript:, data:, vbscript:, etc.) without an allowlist fallback. */
    bool scheme=scheme_colon!=0u;
    for(size_t i=0u;i<scheme_colon && scheme;++i) {
        const unsigned char c=(unsigned char)s[i];
        const bool alpha=(c>='A'&&c<='Z')||(c>='a'&&c<='z');
        const bool digit=c>='0'&&c<='9';
        if(!alpha && (i==0u || (!digit && c!='+' && c!='-' &&
                                   c!='.')))
            scheme=false;
    }
    if(!scheme)return true;
    return html_ascii_equal(s,scheme_colon,"https") ||
           html_ascii_equal(s,scheme_colon,"http") ||
           html_ascii_equal(s,scheme_colon,"mailto");
}

/* Emit an admitted decoded URL with UTF-8 octets percent-encoded.
 * The safety gate checks the DECODED URL before this encoding step, so
 * character-reference obfuscation cannot create an unsafe scheme. Keep
 * original ASCII percent escapes and escape HTML-special attributes. */
static void html_link_href(stun_html_writer *w,const char *s,size_t n) {
    static const char digits[]="0123456789ABCDEF";
    for(size_t i=0u;i<n && w->error==SALTS_MD_OK;++i) {
        const unsigned char b=(unsigned char)s[i];
        /* CommonMark URI normalization percent-encodes decoded UTF-8,
         * raw backslash, angle/quote/backtick delimiters and ASCII space, while
         * existing %HH syntax and URL structural punctuation stay intact.
         * This runs AFTER the full decoded href safety admission. */
        if(b>=0x80u || b==' ' || b=='\\' || b=='<' ||
           b=='>' || b=='\"' || b=='[' || b==']' || b==0x60u) {
            const char pct[3]={'%',digits[b>>4u],digits[b&0x0Fu]};
            html_bytes(w,pct,sizeof(pct));
        } else html_escaped(w,s+i,1u,true);
    }
}
/* IMAGE src uses a narrower policy than navigable href: mailto is
 * never an image protocol. All decoded control bytes and unknown schemes
 * are already rejected by the canonical full-byte href safety scan. */
bool salts_md_html_safe_image_src(const char *s,size_t n) {
    if(!salts_md_html_safe_href(s,n))return false;
    if(n>=7u && s[6]==':' && html_ascii_equal(s,6u,"mailto"))
        return false;
    return true;
}

bool salts_md_html_safe_title(const char *s,size_t n) {
    if(!s && n)return false;
    for(size_t i=0u;i<n;++i) {
        const unsigned char c=(unsigned char)s[i];
        /* LF and TAB are safe payload in a quoted HTML5 title attribute.
         * The Markdown collector normalizes physical CRLF to LF; all other
         * C0/DEL bytes remain rejected. This does NOT relax href admission. */
        if((c<0x20u && c!='\n' && c!='\t') || c==0x7Fu)return false;
    }
    return true;
}

static bool html_metadata(stun_html_writer *w, size_t id,
                          const char **s, size_t *n) {
    if (salts_md_rich_document_metadata(w->doc,id,s,n)!=SALTS_MD_OK) {
        w->error = SALTS_MD_INVALID_ARGUMENT;
        return false;
    }
    return true;
}

static bool html_list_meta(stun_html_writer *w,size_t id,
                           salts_md_list_semantics *info) {
    const salts_md_block *node=&w->doc->document.blocks[id];
    const char *data=NULL;size_t size=0;
    *info=(salts_md_list_semantics){0};
    info->start=node->kind==SALTS_MD_BLOCK_ORDERED_LIST?1u:0u;
    if(!html_metadata(w,id,&data,&size)) return false;
    if(!size) return true; /* existing projected tight lists */
    if(size!=sizeof(*info)) {w->error=SALTS_MD_INVALID_ARGUMENT;return false;}
    memcpy(info,data,sizeof(*info));
    const bool ordered=node->kind==SALTS_MD_BLOCK_ORDERED_LIST;
    if(info->loose>1u||info->reserved[0]||info->reserved[1]||
       (ordered ? (info->start>999999999u||
                   (info->marker!='.'&&info->marker!=')'))
                : (info->start!=0u||(info->marker!='-'&&
                       info->marker!='+'&&info->marker!='*')))) {
        w->error=SALTS_MD_INVALID_ARGUMENT;return false;
    }
    return true;
}
static bool html_loose_item(stun_html_writer *w,size_t id) {
    const uint32_t parent=w->doc->document.blocks[id].parent_index;
    salts_md_list_semantics info;
    if(parent>=w->doc->document.block_count ||
       !html_list_meta(w,parent,&info)) {
        w->error=SALTS_MD_INVALID_ARGUMENT;return false;
    }
    return info.loose!=0u;
}
static void html_number(stun_html_writer *w,uint32_t v) {
    char digits[10];size_t n=0;
    do {digits[n++]=(char)('0'+v%10u);v/=10u;} while(v);
    while(n) html_bytes(w,&digits[--n],1u);
}

static void html_text(stun_html_writer *w, const salts_md_block *node) {
    if (node->byte_end < node->byte_begin ||
        node->byte_end > w->doc->document.source_bytes) {
        w->error = SALTS_MD_INVALID_ARGUMENT;
        return;
    }
    const size_t length=node->byte_end-node->byte_begin;
    if (!w->doc->document.source) {
        if (length) w->error=SALTS_MD_INVALID_ARGUMENT;
        return;
    }
    html_escaped(w,w->doc->document.source+node->byte_begin,
                 length,false);
}

/* IMAGE alt is flattened from its owned RichDoc descendants, never from
 * serialized markup or source reparsing. LINK and nested IMAGE contribute
 * their visible TEXT, never their URLs or titles. The same bounded walk
 * runs during sizing and writing; failures remain output-atomic. */
static void html_image_alt_children(stun_html_writer *w,size_t parent,
                                    size_t *after,size_t depth) {
    const salts_md_document_view *doc=&w->doc->document;
    if(parent>=doc->block_count || depth>STUN_HTML_MAX_DEPTH) {
        w->error=SALTS_MD_CAPACITY_EXCEEDED;return;
    }
    size_t at=parent+1u;
    while(at<doc->block_count && w->error==SALTS_MD_OK &&
          doc->blocks[at].parent_index==parent) {
        const salts_md_block_kind kind=doc->blocks[at].kind;
        if(kind==SALTS_MD_BLOCK_IMAGE_TITLE ||
           kind==SALTS_MD_BLOCK_LINK_TITLE) {
            const bool correct=(at==parent+1u) &&
                (kind==SALTS_MD_BLOCK_IMAGE_TITLE?
                 doc->blocks[parent].kind==SALTS_MD_BLOCK_IMAGE:
                 doc->blocks[parent].kind==SALTS_MD_BLOCK_LINK);
            if(!correct || (at+1u<doc->block_count &&
                            doc->blocks[at+1u].parent_index==at)) {
                w->error=SALTS_MD_INVALID_ARGUMENT;break;
            }
            ++at;continue;
        }
        /* A typed inline HTML token contributes its literal visible
         * characters to IMAGE alt, not live markup or its attributes.
         * Always HTML-escape the alt attribute even in SPEC mode. */
        if(kind==SALTS_MD_BLOCK_TEXT ||
           kind==SALTS_MD_BLOCK_RAW_HTML_INLINE ||
           kind==SALTS_MD_BLOCK_URI_AUTOLINK) {
            html_text(w,&doc->blocks[at]);
            if(at+1u<doc->block_count &&
               doc->blocks[at+1u].parent_index==at)
                w->error=SALTS_MD_INVALID_ARGUMENT;
            ++at;continue;
        }
        if(kind==SALTS_MD_BLOCK_LINE_BREAK ||
           kind==SALTS_MD_BLOCK_HARD_LINE_BREAK) {
            html_str(w," ");++at;continue;
        }
        if(kind==SALTS_MD_BLOCK_EMPHASIS ||
           kind==SALTS_MD_BLOCK_STRONG ||
           kind==SALTS_MD_BLOCK_STRIKETHROUGH ||
           kind==SALTS_MD_BLOCK_CODE_SPAN ||
           kind==SALTS_MD_BLOCK_LINK ||
           kind==SALTS_MD_BLOCK_IMAGE) {
            size_t next=at;
            html_image_alt_children(w,at,&next,depth+1u);
            if(next<=at) {w->error=SALTS_MD_INVALID_ARGUMENT;break;}
            at=next;continue;
        }
        w->error=SALTS_MD_INVALID_ARGUMENT;break;
    }
    *after=at;
}

static void html_node(stun_html_writer *w, size_t id,
                      size_t *next, size_t depth, bool table_header);

static void html_children(stun_html_writer *w, size_t id,
                          size_t *next, size_t depth, bool table_header) {
    size_t at=id+1u;
    const salts_md_document_view *doc=&w->doc->document;
    while(w->error==SALTS_MD_OK && at<doc->block_count &&
          doc->blocks[at].parent_index==id) {
        size_t after=at;
        html_node(w,at,&after,depth+1u,table_header);
        if(w->error!=SALTS_MD_OK || after<=at) {
            if(w->error==SALTS_MD_OK) w->error=SALTS_MD_INVALID_ARGUMENT;
            break;
        }
        at=after;
    }
    *next=at;
}

/* Check all semantic ancestors of TABLE in an untrusted RichDoc.  A
 * forged LIST under PARAGRAPH can retain valid immediate TABLE->ITEM->LIST
 * edges and generic preorder IDs. The parser's actual TABLE containers
 * are only DOCUMENT, QUOTE and LIST_ITEM/TASK_ITEM chains. */
static bool html_table_owner_chain(const salts_md_document_view *doc,
                                   size_t table) {
    size_t child=table;
    for(size_t hops=0u;child>0u && hops<doc->block_count;++hops) {
        const uint32_t parent=doc->blocks[child].parent_index;
        if(parent>=child)return false;
        const salts_md_block_kind kind=doc->blocks[child].kind;
        const salts_md_block_kind owner=doc->blocks[parent].kind;
        switch(kind) {
        case SALTS_MD_BLOCK_TABLE:
        case SALTS_MD_BLOCK_QUOTE:
        case SALTS_MD_BLOCK_LIST:
        case SALTS_MD_BLOCK_ORDERED_LIST:
            if(owner!=SALTS_MD_BLOCK_DOCUMENT &&
               owner!=SALTS_MD_BLOCK_QUOTE &&
               owner!=SALTS_MD_BLOCK_LIST_ITEM &&
               owner!=SALTS_MD_BLOCK_TASK_ITEM)return false;
            break;
        case SALTS_MD_BLOCK_LIST_ITEM:
        case SALTS_MD_BLOCK_TASK_ITEM:
            if(owner!=SALTS_MD_BLOCK_LIST &&
               owner!=SALTS_MD_BLOCK_ORDERED_LIST)return false;
            break;
        default:return false;
        }
        if(parent==0u)return owner==SALTS_MD_BLOCK_DOCUMENT;
        child=parent;
    }
    return false;
}

static void html_table(stun_html_writer *w, size_t id,
                       size_t *next, size_t depth) {
    const salts_md_document_view *doc=&w->doc->document;
    /* Validate the entire typed owner chain before HTML sizing/writing. */
    if(!html_table_owner_chain(doc,id)) {
        w->error=SALTS_MD_INVALID_ARGUMENT;return;
    }
    const char *metadata=NULL;size_t columns=0u;
    if(!html_metadata(w,id,&metadata,&columns))return;
    /* The canonical parser publishes exactly one byte per column. Never
     * infer GFM alignment by reparsing text or trusting a child attribute. */
    if(!columns || !metadata || columns>doc->block_count) {
        w->error=SALTS_MD_INVALID_ARGUMENT;return;
    }
    for(size_t i=0u;i<columns;++i)
        if((unsigned char)metadata[i]>SALTS_MD_TABLE_ALIGN_RIGHT) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
    const uint8_t *saved_alignment=w->table_alignment;
    const size_t saved_columns=w->table_columns;
    const size_t saved_column=w->table_column;
    w->table_alignment=(const uint8_t*)metadata;
    w->table_columns=columns;
    w->table_column=0u;
    size_t at=id+1u;
    if (at>=doc->block_count || doc->blocks[at].parent_index!=id ||
        doc->blocks[at].kind!=SALTS_MD_BLOCK_TABLE_ROW) {
        w->error=SALTS_MD_INVALID_ARGUMENT;
        w->table_alignment=saved_alignment;
        w->table_columns=saved_columns;
        w->table_column=saved_column;
        return;
    }
    html_str(w,"<table>\n<thead>\n");
    size_t after=at;
    html_node(w,at,&after,depth+1u,true);
    at=after;
    html_str(w,"</thead>\n");
    bool has_body=false;
    while(w->error==SALTS_MD_OK && at<doc->block_count &&
          doc->blocks[at].parent_index==id) {
        if(doc->blocks[at].kind!=SALTS_MD_BLOCK_TABLE_ROW) {
            w->error=SALTS_MD_INVALID_ARGUMENT;
            break;
        }
        if(!has_body) { html_str(w,"<tbody>\n"); has_body=true; }
        html_node(w,at,&after,depth+1u,false);
        if(after<=at) { w->error=SALTS_MD_INVALID_ARGUMENT; break; }
        at=after;
    }
    if(has_body) html_str(w,"</tbody>\n");
    html_str(w,"</table>\n");
    w->table_alignment=saved_alignment;
    w->table_columns=saved_columns;
    w->table_column=saved_column;
    *next=at;
}

static void html_node(stun_html_writer *w, size_t id,
                      size_t *next, size_t depth, bool table_header) {
    const salts_md_document_view *doc=&w->doc->document;
    if (w->error!=SALTS_MD_OK) return;
    if (id>=doc->block_count || depth>STUN_HTML_MAX_DEPTH) {
        w->error=SALTS_MD_CAPACITY_EXCEEDED;
        return;
    }
    const salts_md_block *node=&doc->blocks[id];
    const salts_md_block_kind kind=node->kind;
    if(kind==SALTS_MD_BLOCK_TABLE) {
        html_table(w,id,next,depth);
        return;
    }

    const char *meta=NULL;
    size_t meta_length=0;
    switch(kind) {
    case SALTS_MD_BLOCK_DOCUMENT: break;
    case SALTS_MD_BLOCK_PARAGRAPH: {
        const uint32_t parent=node->parent_index;
        if(parent==0u || (doc->blocks[parent].kind!=SALTS_MD_BLOCK_LIST_ITEM &&
                          doc->blocks[parent].kind!=SALTS_MD_BLOCK_TASK_ITEM) ||
           html_loose_item(w,parent)) html_str(w,"<p>");
        break;
    }
    case SALTS_MD_BLOCK_HEADING:
        if(node->heading_level<1 || node->heading_level>6) {
            w->error=SALTS_MD_INVALID_ARGUMENT; return;
        }
        html_str(w,"<h");
        { char digit=(char)('0'+node->heading_level); html_bytes(w,&digit,1u); }
        html_str(w,">");
        break;
    case SALTS_MD_BLOCK_RAW_HTML_INLINE:
    case SALTS_MD_BLOCK_RAW_HTML_BLOCK: {
        if(node->byte_end<node->byte_begin ||
           node->byte_end>doc->source_bytes ||
           (!doc->source && node->byte_end!=node->byte_begin)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        const size_t size=node->byte_end-node->byte_begin;
        const char *raw=doc->source?doc->source+node->byte_begin:NULL;
        if(w->policy==SALTS_MD_HTML_SPEC_CONFORMANCE)
            html_bytes(w,raw,size);
        else html_escaped(w,raw,size,false);
        if(id+1u<doc->block_count &&
           doc->blocks[id+1u].parent_index==id)
            w->error=SALTS_MD_INVALID_ARGUMENT;
        *next=id+1u;return;
    }
    case SALTS_MD_BLOCK_URI_AUTOLINK: {
        /* The parser records the undecoded literal URI in BOTH owned
         * semantic text and metadata. The SAFE output remains the escaped
         * angle token, never a clickable unsafe destination. Only the
         * explicitly selected trusted SPEC profile may emit an anchor. */
        if(!html_metadata(w,id,&meta,&meta_length))return;
        if(node->byte_end<=node->byte_begin ||
           node->byte_end>doc->source_bytes || !doc->source ||
           !meta || meta_length!=node->byte_end-node->byte_begin ||
           memcmp(meta,doc->source+node->byte_begin,meta_length) ||
           (id+1u<doc->block_count &&
            doc->blocks[id+1u].parent_index==id)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        const char *raw=doc->source+node->byte_begin;
        /* Reject forged embedded controls even for explicit SPEC. The
         * canonical C11 autolink lexer already enforces this on admission. */
        for(size_t i=0u;i<meta_length;++i) {
            const unsigned char c=(unsigned char)raw[i];
            if(c<=0x20u||c==0x7fu||c=='<'||c=='>') {
                w->error=SALTS_MD_INVALID_ARGUMENT;return;
            }
        }
        if(w->policy==SALTS_MD_HTML_SPEC_CONFORMANCE) {
            html_str(w,"<a href=\"");
            html_link_href(w,meta,meta_length);
            html_str(w,"\">");
            html_escaped(w,raw,meta_length,false);
            html_str(w,"</a>");
        } else {
            html_str(w,"&lt;");
            html_escaped(w,raw,meta_length,false);
            html_str(w,"&gt;");
        }
        *next=id+1u;return;
    }
    case SALTS_MD_BLOCK_TEXT:
        html_text(w,node);
        if(id+1u<doc->block_count && doc->blocks[id+1u].parent_index==id)
            w->error=SALTS_MD_INVALID_ARGUMENT;
        *next=id+1u;
        return;
    case SALTS_MD_BLOCK_EMPHASIS: html_str(w,"<em>"); break;
    case SALTS_MD_BLOCK_STRONG: html_str(w,"<strong>"); break;
    case SALTS_MD_BLOCK_STRIKETHROUGH: html_str(w,"<del>"); break;
    case SALTS_MD_BLOCK_CODE_SPAN: html_str(w,"<code>"); break;
    case SALTS_MD_BLOCK_LINK: {
        if (!html_metadata(w,id,&meta,&meta_length)) return;
        if(!salts_md_html_safe_href(meta,meta_length)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        const char *title=NULL;
        size_t title_length=0u;
        bool has_title=false;
        if(id+1u<doc->block_count &&
           doc->blocks[id+1u].parent_index==id &&
           doc->blocks[id+1u].kind==SALTS_MD_BLOCK_LINK_TITLE) {
            has_title=true;
            if(!html_metadata(w,id+1u,&title,&title_length))return;
            if(!salts_md_html_safe_title(title,title_length)) {
                w->error=SALTS_MD_INVALID_ARGUMENT;return;
            }
        }
        html_str(w,"<a href=\"");
        html_link_href(w,meta,meta_length);
        html_str(w,"\"");
        if(has_title) {
            html_str(w," title=\"");
            html_escaped(w,title,title_length,true);
            html_str(w,"\"");
        }
        html_str(w,">");
        break;
    }
    case SALTS_MD_BLOCK_IMAGE: {
        if(!html_metadata(w,id,&meta,&meta_length))return;
        if(!salts_md_html_safe_image_src(meta,meta_length)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        const char *title=NULL;size_t title_length=0u;
        bool has_title=false;
        if(id+1u<doc->block_count &&
           doc->blocks[id+1u].parent_index==id &&
           doc->blocks[id+1u].kind==SALTS_MD_BLOCK_IMAGE_TITLE) {
            has_title=true;
            if(!html_metadata(w,id+1u,&title,&title_length))return;
            if(!salts_md_html_safe_title(title,title_length)) {
                w->error=SALTS_MD_INVALID_ARGUMENT;return;
            }
        }
        html_str(w,"<img src=\"");
        html_link_href(w,meta,meta_length);
        html_str(w,"\" alt=\"");
        size_t child_end=id;
        html_image_alt_children(w,id,&child_end,depth+1u);
        if(w->error!=SALTS_MD_OK || child_end<=id)return;
        html_str(w,"\"");
        if(has_title) {
            html_str(w," title=\"");
            html_escaped(w,title,title_length,true);
            html_str(w,"\"");
        }
        html_str(w," />");
        *next=child_end;return;
    }
    case SALTS_MD_BLOCK_IMAGE_TITLE: {
        const uint32_t parent=node->parent_index;
        if(parent>=id || id!=parent+1u ||
           doc->blocks[parent].kind!=SALTS_MD_BLOCK_IMAGE ||
           (id+1u<doc->block_count &&
            doc->blocks[id+1u].parent_index==id)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        *next=id+1u;return;
    }
    case SALTS_MD_BLOCK_LINK_TITLE: {
        /* This typed metadata child never renders as visible inline TEXT.
         * Reject arbitrary parent placement or a second title silently
         * supplied by an untrusted projected RichDoc. */
        const uint32_t parent=node->parent_index;
        if(parent>=id || id!=parent+1u ||
           doc->blocks[parent].kind!=SALTS_MD_BLOCK_LINK ||
           (id+1u<doc->block_count &&
            doc->blocks[id+1u].parent_index==id)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        *next=id+1u;return;
    }
    case SALTS_MD_BLOCK_CODE_INDENTED:
        html_str(w,"<pre><code>");
        break;
    case SALTS_MD_BLOCK_CODE_FENCE:
        if(!html_metadata(w,id,&meta,&meta_length)) return;
        html_str(w,"<pre><code");
        if(meta_length) {
            size_t lang=0;
            while(lang<meta_length && meta[lang]!=' ' && meta[lang]!='\t')
                ++lang;
            if(lang) {
                html_str(w," class=\"language-");
                html_escaped(w,meta,lang,true);
                html_str(w,"\"");
            }
        }
        html_str(w,">");
        break;
    case SALTS_MD_BLOCK_LIST: {
        salts_md_list_semantics info;
        if(!html_list_meta(w,id,&info)) return;
        html_str(w,"<ul>\n");break;
    }
    case SALTS_MD_BLOCK_ORDERED_LIST: {
        salts_md_list_semantics info;
        if(!html_list_meta(w,id,&info)) return;
        html_str(w,"<ol");
        if(info.start!=1u) {
            html_str(w," start=\"");
            html_number(w,info.start);
            html_str(w,"\"");
        }
        html_str(w,">\n");break;
    }
    case SALTS_MD_BLOCK_TASK_ITEM:
        /* Require real list ownership, not merely a printable CHECKBOX. */
        if(node->parent_index>=id ||
           (doc->blocks[node->parent_index].kind!=SALTS_MD_BLOCK_LIST &&
            doc->blocks[node->parent_index].kind!=SALTS_MD_BLOCK_ORDERED_LIST)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        /* A checked-state leaf must be the first direct TASK_ITEM child. */
        if(id+1u>=doc->block_count ||
           doc->blocks[id+1u].parent_index!=id ||
           doc->blocks[id+1u].kind!=SALTS_MD_BLOCK_CHECKBOX) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        html_str(w,"<li>");break;
    case SALTS_MD_BLOCK_CHECKBOX: {
        const uint32_t parent=node->parent_index;
        if(parent>=id || id!=parent+1u ||
           doc->blocks[parent].kind!=SALTS_MD_BLOCK_TASK_ITEM ||
           (id+1u<doc->block_count &&
            doc->blocks[id+1u].parent_index==id) ||
           !html_metadata(w,id,&meta,&meta_length)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        if(meta_length!=1u || !meta || (meta[0]!='x' && meta[0]!=' ')) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        html_str(w,"<input type=\"checkbox\"");
        if(meta[0]=='x')html_str(w," checked=\"\"");
        html_str(w," disabled=\"\" />");
        const bool sibling=id+1u<doc->block_count &&
                           doc->blocks[id+1u].parent_index==parent;
        /* Preserve the literal separator after a task marker even when
         * a loose-list paragraph starts on the following HTML line. */
        html_str(w," ");
        if(sibling && html_loose_item(w,parent))html_str(w,"\n");
        *next=id+1u;return;
    }
    case SALTS_MD_BLOCK_LIST_ITEM: {
        html_str(w,"<li>");
        /* A loose list does not give an empty item a phantom newline.
         * A direct list or code child, however, starts on a new HTML line
         * even when this outer list is tight (CommonMark #298/#299). */
        const bool has_child=id+1u<doc->block_count &&
                             doc->blocks[id+1u].parent_index==id;
        if(has_child &&
           (html_loose_item(w,id) ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_CODE_FENCE ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_CODE_INDENTED ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_HEADING ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_RULE ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_QUOTE ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_RAW_HTML_BLOCK ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_TABLE ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_LIST ||
            doc->blocks[id+1u].kind==SALTS_MD_BLOCK_ORDERED_LIST))
            html_str(w,"\n");
        break;
    }
    case SALTS_MD_BLOCK_QUOTE: html_str(w,"<blockquote>\n"); break;
    case SALTS_MD_BLOCK_TABLE_ROW:
        if(!w->table_alignment || node->parent_index>=id ||
           doc->blocks[node->parent_index].kind!=SALTS_MD_BLOCK_TABLE) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        w->table_column=0u;
        html_str(w,"<tr>\n");break;
    case SALTS_MD_BLOCK_TABLE_CELL: {
        if(!w->table_alignment || w->table_column>=w->table_columns ||
           node->parent_index>=id ||
           doc->blocks[node->parent_index].kind!=SALTS_MD_BLOCK_TABLE_ROW) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        const uint8_t align=w->table_alignment[w->table_column++];
        html_str(w,table_header?"<th":"<td");
        switch(align) {
        case SALTS_MD_TABLE_ALIGN_NONE: break;
        case SALTS_MD_TABLE_ALIGN_LEFT: html_str(w," align=\"left\"");break;
        case SALTS_MD_TABLE_ALIGN_CENTER: html_str(w," align=\"center\"");break;
        case SALTS_MD_TABLE_ALIGN_RIGHT: html_str(w," align=\"right\"");break;
        default: w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        html_str(w,">");break;
    }
    case SALTS_MD_BLOCK_RULE:
        html_str(w,"<hr />\n");
        *next=id+1u; return;
    case SALTS_MD_BLOCK_LINE_BREAK:
        html_str(w,"\n"); /* soft: semantic LF, never a hard HTML break */
        *next=id+1u; return;
    case SALTS_MD_BLOCK_HARD_LINE_BREAK: {
        if(!html_metadata(w,id,&meta,&meta_length)) return;
        if(meta_length!=sizeof(salts_md_line_break_semantics)) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        salts_md_line_break_semantics info;
        memcpy(&info,meta,sizeof(info));
        if((info.origin!=SALTS_MD_LINE_BREAK_TRAILING_SPACES &&
            info.origin!=SALTS_MD_LINE_BREAK_BACKSLASH) ||
           info.reserved[0] || info.reserved[1] || info.reserved[2]) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        html_str(w,"<br />\n");
        *next=id+1u;return;
    }
    default:
        /* No silent HTML generation for unsupported Markdown semantics.
         * In particular: tasks, math, diagrams, entities, etc. */
        w->error=SALTS_MD_INVALID_ARGUMENT;
        return;
    }

    size_t child_end=id+1u;
    html_children(w,id,&child_end,depth,table_header);
    if (w->error!=SALTS_MD_OK) return;

    switch(kind) {
    case SALTS_MD_BLOCK_DOCUMENT: break;
    case SALTS_MD_BLOCK_PARAGRAPH: {
        const uint32_t parent=node->parent_index;
        if(parent==0u || (doc->blocks[parent].kind!=SALTS_MD_BLOCK_LIST_ITEM &&
                          doc->blocks[parent].kind!=SALTS_MD_BLOCK_TASK_ITEM) ||
           html_loose_item(w,parent)) html_str(w,"</p>\n");
        else if(child_end<doc->block_count &&
                doc->blocks[child_end].parent_index==parent)
            html_str(w,"\n");
        break;
    }
    case SALTS_MD_BLOCK_HEADING:
        html_str(w,"</h");
        { char digit=(char)('0'+node->heading_level); html_bytes(w,&digit,1u); }
        html_str(w,">\n");
        break;
    case SALTS_MD_BLOCK_EMPHASIS: html_str(w,"</em>"); break;
    case SALTS_MD_BLOCK_STRONG: html_str(w,"</strong>"); break;
    case SALTS_MD_BLOCK_STRIKETHROUGH: html_str(w,"</del>"); break;
    case SALTS_MD_BLOCK_CODE_SPAN: html_str(w,"</code>"); break;
    case SALTS_MD_BLOCK_LINK: html_str(w,"</a>"); break;
    case SALTS_MD_BLOCK_CODE_FENCE:
    case SALTS_MD_BLOCK_CODE_INDENTED: html_str(w,"</code></pre>\n"); break;
    case SALTS_MD_BLOCK_LIST: html_str(w,"</ul>\n"); break;
    case SALTS_MD_BLOCK_ORDERED_LIST: html_str(w,"</ol>\n"); break;
    case SALTS_MD_BLOCK_LIST_ITEM:
    case SALTS_MD_BLOCK_TASK_ITEM: html_str(w,"</li>\n");break;
    case SALTS_MD_BLOCK_QUOTE: html_str(w,"</blockquote>\n"); break;
    case SALTS_MD_BLOCK_TABLE_ROW:
        if(w->table_column!=w->table_columns) {
            w->error=SALTS_MD_INVALID_ARGUMENT;return;
        }
        html_str(w,"</tr>\n");break;
    case SALTS_MD_BLOCK_TABLE_CELL:
        html_str(w,table_header?"</th>\n":"</td>\n");
        break;
    default: w->error=SALTS_MD_INVALID_ARGUMENT; return;
    }
    *next=child_end;
}

salts_md_status salts_md_render_html_with_policy(
    const salts_md_rich_document_view *view,size_t max_nodes,
    salts_md_html_policy policy,char *buffer,size_t capacity,
    size_t *required) {
    if(!required || !view || (!buffer && capacity!=0u) ||
       (policy!=SALTS_MD_HTML_SAFE_APPLICATION &&
        policy!=SALTS_MD_HTML_SPEC_CONFORMANCE))
        return SALTS_MD_INVALID_ARGUMENT;
    const salts_md_status valid=salts_md_rich_document_validate(view,max_nodes);
    if(valid!=SALTS_MD_OK) return valid;

    /* Dry-run first: validation and exact byte count before any write. */
    stun_html_writer measuring={view,NULL,0u,0u,SALTS_MD_OK,policy,NULL,0u,0u};
    size_t next=0u;
    html_node(&measuring,0u,&next,0u,false);
    if(measuring.error!=SALTS_MD_OK) return measuring.error;
    if(next!=view->document.block_count)
        return SALTS_MD_INVALID_ARGUMENT;

    *required=measuring.length;
    if(!buffer) return SALTS_MD_OK;
    if(measuring.length==SIZE_MAX || capacity<=measuring.length)
        return SALTS_MD_CAPACITY_EXCEEDED;

    stun_html_writer writing={view,buffer,0u,capacity-1u,SALTS_MD_OK,policy,
                              NULL,0u,0u};
    html_node(&writing,0u,&next,0u,false);
    if(writing.error!=SALTS_MD_OK || writing.length!=measuring.length ||
       next!=view->document.block_count)
        return SALTS_MD_INVALID_ARGUMENT;
    buffer[writing.length]='\0';
    return SALTS_MD_OK;
}


/* Public stable production entrypoint: always escapes untrusted markup.
 * Spec conformance is never inferred from source syntax. */
salts_md_status salts_md_render_html(const salts_md_rich_document_view *view,
                                   size_t max_nodes,
                                   char *buffer, size_t capacity,
                                   size_t *required) {
    return salts_md_render_html_with_policy(
        view,max_nodes,SALTS_MD_HTML_SAFE_APPLICATION,
        buffer,capacity,required);
}
