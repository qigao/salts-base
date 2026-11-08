#include <salts/markdown.h>
#include <salts_unicode.h>
#include <vstr.h>
#include "markdown_entities.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The input's exact bytes are copied once and never rewritten. The parser
 * emits semantic text spans and metadata into two distinct owned buffers.
 * All RichDoc views borrow this document until salts_md_document_destroy().
 */
/* Parse-local root reference-definition bindings, never exposed through
 * RichDoc or persisted as runtime state. First normalized label wins.
 * Restrict this slice to bounded ASCII labels, retaining raw escape spelling.
 * Full Unicode Default Case Folding remains tracked separately; never
 * substitute locale tables or decode a key as inline content. */
enum { MD_REF_MAX=128u, MD_REF_KEY_MAX=128u,
       MD_REF_FOLD_MAX=512u, MD_REF_TITLE_LINES_MAX=64u };
typedef struct md_reference_binding {
    char key[MD_REF_FOLD_MAX+1u];
    char *href;
    size_t href_bytes;
    char *title;
    size_t title_bytes;
    bool has_title;
    size_t source_line;
    size_t source_last_line; /* inclusive, all validated ROOT continuations */
} md_reference_binding;

struct salts_md_document {
    char *raw;
    size_t raw_size;
    char *text;
    size_t text_size;
    size_t text_capacity;
    char *metadata;
    size_t metadata_size;
    size_t metadata_capacity;
    salts_md_block *nodes;
    salts_md_block_metadata *node_metadata;
    size_t node_count;
    size_t capacity;
    size_t node_limit;
    md_reference_binding *references;
    size_t reference_count;
    bool collecting_container_definitions;
    bool discovered_container_definition;
    salts_md_status error;
};

typedef struct md_line {
    size_t begin;
    size_t end;
    size_t next;
    bool newline;
} md_line;

/* Container references are admitted by the SAME canonical quote/list
 * block traversal, never by stripping '>' or guessing a second grammar. */
static bool md_reference_definition_line(const salts_md_document *d,size_t begin);
static bool md_reference_collect_line(salts_md_document *d,md_line line,
                                      size_t begin,size_t *after_definition,
                                      bool container);
static bool md_container_reference_line(salts_md_document *d,md_line line,
                                        size_t begin);

typedef struct md_fence {
    char marker;
    size_t count;
    uint32_t node;
} md_fence;

static bool md_space(char c) { return c == ' ' || c == '\t'; }
static bool md_ascii_punct(char c) {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
           (c >= '[' && c <= (char)0x60) || (c >= '{' && c <= '~');
}

static md_line md_read_line(const salts_md_document *d, size_t pos) {
    md_line line = {pos, pos, pos, false};
    while (line.end < d->raw_size && d->raw[line.end] != '\n')
        ++line.end;
    line.newline = line.end < d->raw_size;
    line.next = line.end + (line.newline ? 1u : 0u);
    if (line.end > pos && d->raw[line.end - 1] == '\r') --line.end;
    return line;
}

static size_t md_left(const char *s, size_t b, size_t e) {
    while (b < e && md_space(s[b])) ++b;
    return b;
}
static size_t md_right(const char *s, size_t b, size_t e) {
    while (e > b && md_space(s[e-1])) --e;
    return e;
}
static bool md_blank(const char *s, size_t b, size_t e) {
    return md_left(s,b,e) == e;
}

static void md_error(salts_md_document *d, salts_md_status error) {
    if (d->error == SALTS_MD_OK) d->error = error;
}

static bool md_grow(salts_md_document *d) {
    if (d->node_count < d->capacity) return true;
    if (d->node_count >= d->node_limit) {
        md_error(d, SALTS_MD_CAPACITY_EXCEEDED);
        return false;
    }
    size_t next = d->capacity ? d->capacity * 2 : 32;
    if (next < d->capacity || next > d->node_limit) next = d->node_limit;
    if (next > SIZE_MAX / sizeof(salts_md_block) ||
        next > SIZE_MAX / sizeof(salts_md_block_metadata)) {
        md_error(d, SALTS_MD_CAPACITY_EXCEEDED);
        return false;
    }
    salts_md_block *nodes = (salts_md_block *)realloc(
        d->nodes, next * sizeof(*nodes));
    if (!nodes) {
        md_error(d, SALTS_MD_OUT_OF_MEMORY);
        return false;
    }
    d->nodes = nodes;
    salts_md_block_metadata *meta = (salts_md_block_metadata *)realloc(
        d->node_metadata, next * sizeof(*meta));
    if (!meta) {
        md_error(d, SALTS_MD_OUT_OF_MEMORY);
        return false;
    }
    d->node_metadata = meta;
    d->capacity = next;
    return true;
}

static uint32_t md_add(salts_md_document *d, uint32_t parent,
                       salts_md_block_kind kind, uint32_t level) {
    if (d->error != SALTS_MD_OK || !md_grow(d)) return UINT32_MAX;
    const uint32_t i = (uint32_t)d->node_count++;
    d->nodes[i] = (salts_md_block){
        .stable_id = (uint64_t)i + 1u, .parent_index = parent,
        .kind = kind, .heading_level = level,
        .byte_begin = 0, .byte_end = 0
    };
    d->node_metadata[i] = (salts_md_block_metadata){0,0};
    return i;
}

static bool md_store(salts_md_document *d, bool is_meta,
                     const char *s, size_t n, size_t *begin, size_t *end) {
    size_t *used = is_meta ? &d->metadata_size : &d->text_size;
    char *target = is_meta ? d->metadata : d->text;
    if (d->error != SALTS_MD_OK) return false;
    size_t limit=is_meta ? d->metadata_capacity : d->text_capacity;
    if(*used>limit) {md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;}
    if(!is_meta && n>limit-*used) {
        /* Two-codepoint HTML5 aliases such as &nGt; expand five raw ASCII
         * bytes into six UTF-8 bytes. The existing semantic arena therefore
         * cannot be capped at exactly raw_size. Grow only on demand, with
         * an explicit 2 * raw_size absolute bound and no borrowed views
         * until the successful document is published. */
        if(d->raw_size>(SIZE_MAX-1u)/2u) {
            md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
        }
        const size_t maximum=d->raw_size*2u;
        if(*used>maximum || n>maximum-*used) {
            md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
        }
        char *data=(char*)realloc(d->text,maximum+1u);
        if(!data) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return false;}
        d->text=data;d->text_capacity=maximum;limit=maximum;
    }
    if(is_meta && n>limit-*used) {
        /* Bound metadata by the node budget; short Markdown sources may still
         * need eight metadata bytes for one list. */
        if(d->raw_size>(SIZE_MAX-1u)/2u ||
           d->node_limit>SIZE_MAX/sizeof(salts_md_list_semantics)) {
            md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
        }
        const size_t semantic_ceiling=d->raw_size*2u;
        if(semantic_ceiling>
           SIZE_MAX-d->node_limit*sizeof(salts_md_list_semantics)) {
            md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
        }
        const size_t maximum=semantic_ceiling+
                             d->node_limit*sizeof(salts_md_list_semantics);
        if(*used>maximum || n>maximum-*used) {
            md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
        }
        size_t cap=limit?limit:1u;
        while(cap<*used+n) {
            if(cap>(maximum-1u)/2u) {cap=maximum;break;}
            cap=cap*2u+1u;
        }
        char *data=(char*)realloc(d->metadata,cap+1u);
        if(!data) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return false;}
        d->metadata=data;d->metadata_capacity=cap;limit=cap;
    }
    if(n>limit-*used) {md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;}
    target=is_meta ? d->metadata : d->text;
    *begin = *used;
    if (n) memcpy(target + *used, s, n);
    *used += n;
    *end = *used;
    target[*used] = '\0';
    return true;
}

static void md_text(salts_md_document *d, uint32_t parent,
                    const char *s, size_t n) {
    if (!n || d->error != SALTS_MD_OK) return;
    const uint32_t node = md_add(d, parent, SALTS_MD_BLOCK_TEXT, 0);
    if (node == UINT32_MAX) return;
    size_t from = 0, to = 0;
    if (md_store(d, false, s, n, &from, &to)) {
        d->nodes[node].byte_begin = from;
        d->nodes[node].byte_end = to;
    }
}

/* One owned verbatim semantic span: only the explicit renderer policy
 * chooses whether RAW_HTML is escaped or emitted byte-for-byte. */
static void md_raw_html_inline(salts_md_document *d,uint32_t parent,
                               const char *s,size_t n) {
    if(!n || d->error!=SALTS_MD_OK)return;
    const uint32_t node=md_add(d,parent,SALTS_MD_BLOCK_RAW_HTML_INLINE,0u);
    if(node==UINT32_MAX)return;
    size_t first=0u,last=0u;
    if(md_store(d,false,s,n,&first,&last)) {
        d->nodes[node].byte_begin=first;
        d->nodes[node].byte_end=last;
    }
}

/* One owned RAW_HTML_BLOCK span per canonical root block. Preserve exact
 * payload lines but normalize CRLF to LF, as CommonMark's input model does.
 * The semantic text arena stays contiguous while this block owns input. */
static bool md_raw_html_block_line(salts_md_document *d,uint32_t node,
                                   md_line line,bool first) {
    size_t begin=0u,end=0u;
    if(!md_store(d,false,d->raw+line.begin,line.end-line.begin,
                 &begin,&end))return false;
    if(first)d->nodes[node].byte_begin=begin;
    else if(begin!=d->nodes[node].byte_end) {
        md_error(d,SALTS_MD_INVALID_ARGUMENT);return false;
    }
    if(line.newline) {
        size_t from=0u,to=0u;
        if(!md_store(d,false,"\n",1u,&from,&to))return false;
        if(from!=end) {md_error(d,SALTS_MD_INVALID_ARGUMENT);return false;}
        end=to;
    }
    d->nodes[node].byte_end=end;
    return true;
}

static void md_meta(salts_md_document *d, uint32_t node,
                    const char *s, size_t n) {
    if (node == UINT32_MAX || !n) return;
    size_t from = 0, to = 0;
    if (md_store(d, true, s, n, &from, &to))
        d->node_metadata[node] = (salts_md_block_metadata){from,to};
}


/* One bounded component decoder for fence-info strings and link destinations
 * / titles. Only raw bytes undergo Markdown ASCII punctuation unescaping and
 * HTML5 character reference decoding. Results are never reparsed as grammar.
 * Two-scalar aliases can expand UTF-8 output beyond raw length; twice source
 * bytes bounds the scratch and is checked before every write. */
static bool md_decode_component(salts_md_document *d,const char *s,size_t n,
                                 char **output,size_t *output_bytes) {
    if(d->error!=SALTS_MD_OK || !output || !output_bytes)return false;
    if(n>(SIZE_MAX-1u)/2u) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    const size_t cap=n*2u;
    char *scratch=(char*)malloc(cap+1u);
    if(!scratch) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return false;}
    size_t used=0u;
    for(size_t p=0u;p<n && d->error==SALTS_MD_OK;) {
        if(s[p]=='\\' && p+1u<n && md_ascii_punct(s[p+1u])) {
            scratch[used++]=s[p+1u];
            p+=2u;continue;
        }
        char decoded[8];
        size_t consumed=0u,written=0u;
        if(s[p]=='&' && salts_md_decode_character_reference(
                    s+p,n-p,&consumed,decoded,&written)) {
            if(written>cap-used) {
                md_error(d,SALTS_MD_CAPACITY_EXCEEDED);break;
            }
            memcpy(scratch+used,decoded,written);
            used+=written;p+=consumed;continue;
        }
        scratch[used++]=s[p++];
    }
    if(d->error!=SALTS_MD_OK) {free(scratch);return false;}
    scratch[used]='\0'; /* the length remains authoritative */
    *output=scratch;*output_bytes=used;
    return true;
}

/* Fence info strings share this exact decoder with inline link metadata.
 * The code fence BODY is strictly literal. All decoded metadata is copied
 * into the document-owned arena before the scratch is released. */
static void md_fence_info_meta(salts_md_document *d,uint32_t node,
                               const char *s,size_t n) {
    if(node==UINT32_MAX || !n || d->error!=SALTS_MD_OK)return;
    char *text=NULL;size_t bytes=0u;
    if(!md_decode_component(d,s,n,&text,&bytes))return;
    md_meta(d,node,text,bytes);
    free(text);
}

/* List metadata is owned by the parser; update before publishing the view. */
static bool md_list_meta(salts_md_document *d, uint32_t id,
                         uint32_t start, char marker) {
    const salts_md_list_semantics info={start,(uint8_t)marker,0u,{0u,0u}};
    md_meta(d,id,(const char*)&info,sizeof(info));
    return d->error==SALTS_MD_OK;
}
static void md_mark_list_loose(salts_md_document *d, uint32_t id) {
    if(id>=d->node_count) {md_error(d,SALTS_MD_INVALID_ARGUMENT);return;}
    const salts_md_block_metadata span=d->node_metadata[id];
    if(span.byte_end-span.byte_begin!=sizeof(salts_md_list_semantics)) {
        md_error(d,SALTS_MD_INVALID_ARGUMENT);return;
    }
    salts_md_list_semantics info;
    memcpy(&info,d->metadata+span.byte_begin,sizeof(info));
    info.loose=1u;
    memcpy(d->metadata+span.byte_begin,&info,sizeof(info));
}

/* CommonMark tab stops are four *columns*, not four bytes. For a root-level
 * indented code line, consume precisely four indentation columns. A tab
 * advances to the next multiple of four and is never mistaken for one space.
 * Bytes after those four columns belong to literal code (including tabs). */
static size_t md_code_unindent(const char *s, size_t begin, size_t end) {
    size_t column=0u, at=begin;
    while(at<end && column<4u && md_space(s[at])) {
        column += s[at]=='\t' ? 4u-(column%4u) : 1u;
        ++at;
    }
    return column>=4u ? at : begin;
}

/* One text child per logical code line, with LF normalized at EOF/CRLF.
 * Code is not recursively parsed as inline Markdown; decoded text and the
 * original raw input remain separately owned by the same document. */
static bool md_code_line(salts_md_document *d, uint32_t parent,
                         md_line line, bool blank) {
    const char *s=d->raw;
    size_t content=md_code_unindent(s,line.begin,line.end);
    if (content==line.begin) {
        if (!blank) {
            md_error(d,SALTS_MD_INVALID_ARGUMENT);
            return false;
        }
        content=line.end; /* blank lines indented less than four columns */
    }
    const uint32_t node=md_add(d,parent,SALTS_MD_BLOCK_TEXT,0u);
    if(node==UINT32_MAX) return false;
    size_t from=0u,to=0u,lf_begin=0u,lf_end=0u;
    if(!md_store(d,false,s+content,line.end-content,&from,&to) ||
       !md_store(d,false,"\n",1u,&lf_begin,&lf_end))
        return false;
    d->nodes[node].byte_begin=from;
    d->nodes[node].byte_end=lf_end;
    return true;
}

/* A TAB can span a list or quote boundary. Keep source byte offsets
 * immutable while distinguishing the physical stop column from the single
 * optional whitespace column that belongs to a container marker. */
static size_t md_source_visual_column_at(const char *s,size_t at) {
    size_t first=at,column=0u;
    while(first && s[first-1u]!='\n')--first;
    for(size_t p=first;p<at;++p)
        column+=s[p]=='\t'?4u-(column%4u):1u;
    return column;
}
static size_t md_logical_content_column_at(const char *s,size_t at) {
    const size_t physical=md_source_visual_column_at(s,at);
    if(at && s[at-1u]=='>')return physical;
    if(at>=2u && md_space(s[at-1u]) && s[at-2u]=='>')
        return md_source_visual_column_at(s,at-2u)+2u;
    return physical;
}

/* Consume a precise visual-column code indent from raw source. If the
 * last consumed TAB crosses the boundary, emit its residual columns as
 * ASCII spaces in the ONE owned code TEXT node. CRLF/EOF normalize to LF. */
static bool md_code_line_visual(salts_md_document *d,uint32_t parent,
                                md_line line,size_t physical_column,
                                size_t wanted_column) {
    const char *s=d->raw;
    size_t pos=line.begin,column=physical_column;
    while(pos<line.end && column<wanted_column && md_space(s[pos])) {
        column+=s[pos]=='\t'?4u-(column%4u):1u;
        ++pos;
    }
    if(column<wanted_column) {
        md_error(d,SALTS_MD_INVALID_ARGUMENT);return false;
    }
    const uint32_t node=md_add(d,parent,SALTS_MD_BLOCK_TEXT,0u);
    if(node==UINT32_MAX)return false;
    size_t from=0u,to=0u,part_begin=0u,part_end=0u;
    bool began=false;
    size_t residue=column-wanted_column;
    static const char spaces[]="    ";
    while(residue && d->error==SALTS_MD_OK) {
        const size_t n=residue>4u?4u:residue;
        if(!md_store(d,false,spaces,n,&part_begin,&part_end))
            return false;
        if(!began) {from=part_begin;began=true;}
        residue-=n;
    }
    if(!md_store(d,false,s+pos,line.end-pos,&part_begin,&part_end))
        return false;
    if(!began)from=part_begin;
    if(!md_store(d,false,"\n",1u,&part_begin,&to))
        return false;
    d->nodes[node].byte_begin=from;
    d->nodes[node].byte_end=to;
    return true;
}

/* Code spans are opaque to outer emphasis and link-label delimiter scans,
 * and close only on an EXACTLY equal-sized backtick run. */
static size_t md_code_span_close(const char *s,size_t n,size_t begin,
                                 size_t marker_size);

/* CommonMark left/right flanking uses Unicode P-or-S properties for adjacent
 * scalars. Salts Unicode 17 owns the property data; Stun has no local table. */
/* Inline flanking has to classify the *adjacent scalar*, not its last
 * UTF-8 byte. The only non-ASCII property requested here is Unicode 17
 * White_Space from the released Salts::Unicode provider: there is no copied
 * Unicode table or locale-sensitive fallback. Salts validates the borrowed
 * scalar; an invalid UTF-8 fragment has no provable White_Space property.
 *
 * The previous scalar begins at most three continuation bytes to the left.
 * The public Unicode scanner decodes/validates one scalar without allocation.
 * All source/text lifetimes remain owned by the containing RichDoc. */
static bool md_inline_whitespace_after(const char *s,size_t n,size_t at) {
    if(at>=n) return true; /* end of span */
    if((unsigned char)s[at]<0x80u)
        return md_space(s[at]) || s[at]=='\n' || s[at]=='\r';
    size_t cursor=0u;
    salts_unicode_scalar scalar={0};
    const vstr borrowed=vstr_from_buf(s+at,n-at);
    return salts_unicode_utf8_next(borrowed,&cursor,&scalar)==
           SALTS_UNICODE_OK &&
           (scalar.properties&SALTS_UNICODE_PROPERTY_WHITE_SPACE)!=0u;
}

static bool md_inline_whitespace_before(const char *s,size_t at) {
    if(!at) return true; /* beginning of span */
    const size_t last=at-1u;
    if((unsigned char)s[last]<0x80u)
        return md_space(s[last]) || s[last]=='\n' || s[last]=='\r';
    size_t first=last;
    while(first>0u && at-first<4u &&
          ((unsigned char)s[first]&0xc0u)==0x80u) --first;
    size_t cursor=0u;
    salts_unicode_scalar scalar={0};
    const vstr borrowed=vstr_from_buf(s+first,at-first);
    return salts_unicode_utf8_next(borrowed,&cursor,&scalar)==
           SALTS_UNICODE_OK && cursor==at-first &&
           (scalar.properties&SALTS_UNICODE_PROPERTY_WHITE_SPACE)!=0u;
}

/* Query only the immediately adjacent scalar. The UTF-8 scanner validates
 * the borrowed slice; malformed sequences never acquire punctuation status.
 * ASCII retains its existing punctuation semantics. */
static bool md_inline_punct_after(const char *s,size_t n,size_t at) {
    if(at>=n)return false;
    if((unsigned char)s[at]<0x80u)return md_ascii_punct(s[at]);
    salts_unicode_scalar scalar={0};
    size_t cursor=0u;
    if(salts_unicode_utf8_next(vstr_from_buf(s+at,n-at),&cursor,
                               &scalar)!=SALTS_UNICODE_OK)return false;
    int result=0;
    return salts_unicode_is_punctuation_or_symbol(scalar.value,&result)==
           SALTS_UNICODE_OK && result!=0;
}
static bool md_inline_punct_before(const char *s,size_t at) {
    if(!at)return false;
    const size_t last=at-1u;
    if((unsigned char)s[last]<0x80u)return md_ascii_punct(s[last]);
    size_t first=last;
    while(first>0u && at-first<4u &&
          ((unsigned char)s[first]&0xc0u)==0x80u)--first;
    salts_unicode_scalar scalar={0};
    size_t cursor=0u;
    if(salts_unicode_utf8_next(vstr_from_buf(s+first,at-first),
                               &cursor,&scalar)!=SALTS_UNICODE_OK ||
       cursor!=at-first)return false;
    int result=0;
    return salts_unicode_is_punctuation_or_symbol(scalar.value,&result)==
           SALTS_UNICODE_OK && result!=0;
}

static void md_emph_flanking(const char *s,size_t n,size_t at,size_t run,
                             char marker,bool *can_open,bool *can_close) {
    const bool left_space=md_inline_whitespace_before(s,at);
    const bool right_space=md_inline_whitespace_after(s,n,at+run);
    const bool left_punct=md_inline_punct_before(s,at);
    const bool right_punct=md_inline_punct_after(s,n,at+run);
    const bool left_flanking=!right_space &&
        (!right_punct || left_space || left_punct);
    const bool right_flanking=!left_space &&
        (!left_punct || right_space || right_punct);
    *can_open=left_flanking &&
              (marker!='_' || !right_flanking || left_punct);
    *can_close=right_flanking &&
               (marker!='_' || !left_flanking || right_punct);
}

/* Generic exact-size matcher for link/strike/fence syntax. Emphasis and
 * strong runs are handled EXCLUSIVELY by md_emph_pairs below: do not
 * resurrect a competing greedy * / _ matcher here. */
static size_t md_match(const char *s, size_t n, size_t start,
                       char symbol, size_t count) {
    const bool opaque_code=symbol=='~' || symbol==']';
    for(size_t p=start;p<n;) {
        if(s[p]=='\\' && p+1u<n) {p+=2u;continue;}
        if(opaque_code && s[p]==(char)0x60) {
            size_t end=p+1u;
            while(end<n && s[end]==(char)0x60) ++end;
            const size_t close=md_code_span_close(s,n,end,end-p);
            p=close==SIZE_MAX?end:close+(end-p);
            continue;
        }
        if(s[p]!=symbol) {++p;continue;}
        size_t end=p+1u;
        while(end<n && s[end]==symbol) ++end;
        if(end-p==count) return p;
        p=end;
    }
    return SIZE_MAX;
}

/* Shared URI/email predicates are defined below and used by the one inline
 * emitter and both link-delimiter admission paths. Do not duplicate their
 * scheme, bounds, or security semantics here. */
static bool md_uri_autolink(const char *s,size_t n,size_t at,
                            size_t *out_close);
static bool md_email_autolink(const char *s,size_t n,size_t at,
                              size_t *out_close);

/* Bounded same-line HTML open-tag lexing for delimiter opacity only.
 * This is NOT RawHTML rendering, a second Markdown frontend, or a security
 * sanitizer. SafeApplicationHtml still escapes every byte of this source.
 * Accept only syntactically complete ASCII tags with quoted/unquoted
 * attribute grammar; malformed/incomplete tags stay ordinary Markdown.
 * Official #524/#536 require ']' within a quoted attribute to be opaque. */
static bool md_tag_letter(unsigned char c) {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z');
}
static bool md_tag_digit(unsigned char c) {
    return c>='0'&&c<='9';
}
static bool md_tag_attr_start(unsigned char c) {
    return md_tag_letter(c)||c=='_'||c==':';
}
static bool md_tag_attr_continue(unsigned char c) {
    return md_tag_attr_start(c)||md_tag_digit(c)||c=='-'||c=='.';
}
/* The scanner is shared by one-line and bounded joined-line candidates.
 * This is only the HTML lexical whitespace rule, not Markdown indentation. */
static bool md_tag_space(char c) {
    return md_space(c)||c=='\n'||c=='\r';
}
static size_t md_opaque_html_tag_close(const char *s,size_t n,size_t at) {
    if(!s||at>=n||s[at]!='<'||at+1u>=n||
       !md_tag_letter((unsigned char)s[at+1u]))return SIZE_MAX;
    size_t p=at+2u;
    while(p<n && (md_tag_letter((unsigned char)s[p])||
                   md_tag_digit((unsigned char)s[p])||s[p]=='-'))++p;
    for(;;) {
        if(p>=n)return SIZE_MAX;
        if(s[p]=='>')return p;
        if(s[p]=='/'&&p+1u<n&&s[p+1u]=='>')return p+1u;
        if(!md_tag_space(s[p]))return SIZE_MAX;
        while(p<n && md_tag_space(s[p]))++p;
        if(p>=n)return SIZE_MAX;
        if(s[p]=='>')return p;
        if(s[p]=='/'&&p+1u<n&&s[p+1u]=='>')return p+1u;
        if(!md_tag_attr_start((unsigned char)s[p]))return SIZE_MAX;
        ++p;
        while(p<n&&md_tag_attr_continue((unsigned char)s[p]))++p;
        /* A boolean attribute owns only its NAME. In particular,
         * "<a _boolean zoop:33=zoop:33 />" has two attributes.
         * Probe whitespace before '=' without swallowing the separator
         * needed to admit the next attribute if '=' is absent. */
        const size_t name_end=p;
        size_t equals=p;
        while(equals<n&&md_tag_space(s[equals]))++equals;
        if(equals<n&&s[equals]=='=') {
            p=equals+1u;
            while(p<n&&md_tag_space(s[p]))++p;
            if(p>=n)return SIZE_MAX;
            const char quote=s[p];
            if(quote=='"'||quote=='\'') {
                ++p;
                while(p<n&&s[p]!=quote) {
                    if(s[p]=='\0')return SIZE_MAX;
                    ++p;
                }
                if(p>=n)return SIZE_MAX;
                ++p;
            } else {
                const size_t start=p;
                while(p<n&&!md_tag_space(s[p])&&s[p]!='>') {
                    const char c=s[p];
                    if(c=='<'||c=='"'||c=='\''||c=='='||
                       c==(char)0x60||c=='\0')
                        return SIZE_MAX;
                    ++p;
                }
                if(p==start)return SIZE_MAX;
            }
        } else {
            p=name_end; /* preserve separating whitespace for next name */
        }
    }
}

/* Closing tags share this conservative ASCII lexeme scan with the existing
 * quoted-attribute opening-tag detector. Invalid/unclosed markup remains
 * normal TEXT; no renderer-side parsing or unsafe fallback. */
static size_t md_inline_html_tag_close(const char *s,size_t n,size_t at) {
    size_t p=md_opaque_html_tag_close(s,n,at);
    if(p!=SIZE_MAX)return p;
    if(!s || at>=n || s[at]!='<' || n-at<4u)return SIZE_MAX;
    /* CDATA / comment / processing-instruction / declaration are
     * opaque literal HTML only when a complete SAME-LINE token exists.
     * Malformed openers remain Markdown TEXT with production escaping. */
    if(n-at>=9u && !memcmp(s+at,"<![CDATA[",9u)) {
        for(p=at+9u;p+2u<n;++p) {
            if(s[p]=='\n'||s[p]=='\r')return SIZE_MAX;
            if(s[p]==']'&&s[p+1u]==']'&&s[p+2u]=='>')return p+2u;
        }
        return SIZE_MAX;
    }
    if(s[at+1u]=='!' && s[at+2u]=='-' && s[at+3u]=='-') {
        /* CommonMark #626: the short HTML-comment closers are complete
         * RAW_HTML_INLINE tokens. Do not annex an unrelated later '-->'
         * or change the default SAFE renderer's escaping policy. */
        if(at+4u<n && s[at+4u]=='>')return at+4u;
        if(at+5u<n && s[at+4u]=='-' && s[at+5u]=='>')
            return at+5u;
        for(p=at+4u;p+2u<n;++p) {
            /* CommonMark 0.31.2 allows internal '--' and physical LF;
             * only the first complete '-->' terminates the token. */
            if(s[p]=='-'&&s[p+1u]=='-'&&s[p+2u]=='>')return p+2u;
        }
        return SIZE_MAX;
    }
    if(s[at+1u]=='?') {
        for(p=at+2u;p+1u<n;++p) {
            if(s[p]=='\n'||s[p]=='\r')return SIZE_MAX;
            if(s[p]=='?'&&s[p+1u]=='>')return p+1u;
        }
        return SIZE_MAX;
    }
    if(s[at+1u]=='!' &&
       s[at+2u]>='A'&&s[at+2u]<='Z') {
        for(p=at+3u;p<n;++p) {
            if(s[p]=='\n'||s[p]=='\r'||s[p]=='<')return SIZE_MAX;
            if(s[p]=='>')return p;
        }
        return SIZE_MAX;
    }
    if(at>=n || s[at]!='<' || n-at<4u ||
       s[at+1]!='/' ||
       !md_tag_letter((unsigned char)s[at+2u]))return SIZE_MAX;
    p=at+3u;
    while(p<n && (md_tag_letter((unsigned char)s[p]) ||
                  md_tag_digit((unsigned char)s[p]) ||
                  s[p]=='-'))++p;
    while(p<n && md_space(s[p]))++p;
    return p<n && s[p]=='>'?p:SIZE_MAX;
}

/* CommonMark type-6 HTML root blocks: the tag vocabulary is fixed by the
 * specification. This is only block admission in the SAME C11 frontend;
 * the existing inline tag scanner owns attributes and token semantics.
 * A type-6 block may interrupt a root paragraph and ends at the next blank.
 * No second markdown parser, DOM, or unsafe production renderer. */
static bool md_raw_html_root_type6(const char *s,size_t begin,size_t end) {
    if(begin>=end || s[begin]!='<')return false;
    size_t p=begin+1u;
    if(p<end && s[p]=='/')++p;
    const size_t start=p;
    while(p<end && (md_tag_letter((unsigned char)s[p]) ||
                    md_tag_digit((unsigned char)s[p])))++p;
    if(p==start ||
       (p<end && s[p]!='>' && !md_space(s[p]) &&
        !(s[p]=='/' && p+1u<end && s[p+1u]=='>')))
        return false;
    static const char *const names[]={
        "address","article","aside","base","basefont","blockquote",
        "body","caption","center","col","colgroup","dd","details",
        "dialog","dir","div","dl","dt","fieldset","figcaption",
        "figure","footer","form","frame","frameset","h1","h2","h3",
        "h4","h5","h6","head","header","hr","html","iframe","legend",
        "li","link","main","menu","nav","noframes","ol",
        "optgroup","option","p","param","search","section",
        "summary","table","tbody","td","tfoot","th",
        "thead","title","tr","track","ul"
    };
    const size_t bytes=p-start;
    for(size_t i=0u;i<sizeof(names)/sizeof(names[0]);++i) {
        const char *name=names[i];
        if(strlen(name)!=bytes)continue;
        bool match=true;
        for(size_t j=0u;j<bytes;++j) {
            const unsigned char c=(unsigned char)s[start+j];
            const char low=(char)(c>='A'&&c<='Z'?c+('a'-'A'):c);
            if(low!=name[j]) {match=false;break;}
        }
        if(match)return true;
    }
    return false;
}

/* The canonical block tokenizer recognizes the CommonMark 0.31.2 seven
 * raw-HTML start/end conditions, sharing the type-6 vocabulary above
 * and the existing single-line inline tag scanner for type 7.
 * 'type 7 cannot interrupt paragraphs' applies identically in the ROOT
 * reference preflight and the owned block parser. */
static bool md_html_ascii_equal(const char *s,size_t n,const char *word) {
    const size_t count=strlen(word);
    if(n<count)return false;
    for(size_t i=0u;i<count;++i) {
        const unsigned char b=(unsigned char)s[i];
        const char lower=(char)(b>='A'&&b<='Z'?b+('a'-'A'):b);
        if(lower!=word[i])return false;
    }
    return true;
}
static bool md_html_line_contains(const char *s,size_t begin,size_t end,
                                  const char *pattern,bool folded) {
    const size_t n=strlen(pattern);
    if(end<begin || n>end-begin)return false;
    for(size_t at=begin;at<=end-n;++at) {
        if(folded) {
            if(md_html_ascii_equal(s+at,end-at,pattern))return true;
        } else if(!memcmp(s+at,pattern,n))return true;
    }
    return false;
}
static unsigned md_raw_html_root_kind(const char *s,size_t b,size_t e,
                                     bool paragraph_open) {
    if(b>=e || s[b]!='<' || e-b<2u)return 0u;
    static const char *const first[]={
        "pre","script","style","textarea"
    };
    for(size_t k=0u;k<sizeof(first)/sizeof(first[0]);++k) {
        const size_t n=strlen(first[k]);
        if(e-b>=n+1u && md_html_ascii_equal(s+b+1u,e-b-1u,first[k])) {
            const size_t end=b+1u+n;
            if(end==e || s[end]=='>' || md_space(s[end]))return 1u;
        }
    }
    if(e-b>=4u && !memcmp(s+b,"<!--",4u))return 2u;
    if(s[b+1u]=='?')return 3u;
    if(e-b>=3u && s[b+1u]=='!' &&
       ((s[b+2u]>='a'&&s[b+2u]<='z') ||
        (s[b+2u]>='A'&&s[b+2u]<='Z')))return 4u;
    if(e-b>=9u && !memcmp(s+b,"<![CDATA[",9u))return 5u;
    if(md_raw_html_root_type6(s,b,e))return 6u;
    if(paragraph_open)return 0u; /* type 7 may NOT interrupt a paragraph */
    size_t at=b+1u;
    if(at<e && s[at]=='/')++at;
    if(at==e || !md_tag_letter((unsigned char)s[at]))return 0u;
    const size_t name=at;
    ++at;
    while(at<e && (md_tag_letter((unsigned char)s[at]) ||
                   md_tag_digit((unsigned char)s[at]) ||
                   s[at]=='-'))++at;
    for(size_t i=0u;i<sizeof(first)/sizeof(first[0]);++i)
        if(at-name==strlen(first[i]) &&
           md_html_ascii_equal(s+name,at-name,first[i]))
            return 0u;
    const size_t close=md_inline_html_tag_close(s,e,b);
    if(close==SIZE_MAX || close+1u>e ||
       !md_blank(s,close+1u,e))return 0u;
    return 7u;
}
/* The fixed-string end condition is shared by ROOT reference scanning and
 * actual block emission. For types 1-5 a same-line closer ends the block;
 * for types 6-7 the next blank line ends it BEFORE being emitted. */
static bool md_raw_html_line_ends(unsigned kind,const char *s,
                                   size_t b,size_t e) {
    switch(kind) {
    case 1u: {
        static const char *const ends[]={
            "</pre>","</script>","</style>","</textarea>"
        };
        for(size_t i=0u;i<sizeof(ends)/sizeof(ends[0]);++i)
            if(md_html_line_contains(s,b,e,ends[i],true))return true;
        return false;
    }
    case 2u:return md_html_line_contains(s,b,e,"-->",false);
    case 3u:return md_html_line_contains(s,b,e,"?>",false);
    case 4u:return md_html_line_contains(s,b,e,">",false);
    case 5u:return md_html_line_contains(s,b,e,"]]>",false);
    default:return false;
    }
}

/* One bounded LINK-text bracket scanner for both inline targets and
 * reference links. Literal nested brackets are legal visible link text
 * (CommonMark #528), including adjacent closing "]]" from #559.
 * Code spans, recognized angle autolinks and backslash-escaped punctuation
 * are opaque. A fixed nesting ceiling rejects pathological input without
 * allocating parser state (#538). */
static size_t md_link_text_close(const char *s,size_t n,size_t begin) {
    unsigned depth=1u;
    for(size_t p=begin;p<n;) {
        if(s[p]=='\\' && p+1u<n && md_ascii_punct(s[p+1u])) {
            p+=2u;continue;
        }
        if(s[p]==(char)0x60) {
            size_t end=p+1u;
            while(end<n && s[end]==(char)0x60)++end;
            const size_t closer=md_code_span_close(s,n,end,end-p);
            p=closer==SIZE_MAX?end:closer+(end-p);
            continue;
        }
        if(s[p]=='<') {
            size_t close=SIZE_MAX;
            if(md_uri_autolink(s,n,p,&close) ||
               md_email_autolink(s,n,p,&close)) {
                p=close+1u;continue;
            }
            close=md_inline_html_tag_close(s,n,p);
            if(close!=SIZE_MAX) {p=close+1u;continue;}
        }
        if(s[p]=='[') {
            if(depth>=32u)return SIZE_MAX;
            ++depth;
        } else if(s[p]==']') {
            if(--depth==0u)return p;
        }
        ++p;
    }
    return SIZE_MAX;
}

/* One raw-source link destination/title parser shared by delimiter
 * discovery and RichDoc emission. Do not use the first ')' as a closer:
 * CommonMark admits balanced destination parentheses and <...> targets
 * containing spaces or ')' bytes. Angle delimiters are stripped BEFORE
 * decoding the semantic destination, and none of the decoded characters
 * are reused as Markdown grammar.
 *
 * All scans remain within n owned source bytes. Unbracketed parentheses
 * nesting is bounded to 32 levels; malformed/over-depth syntax remains
 * literal input rather than inventing partial LINK/URI ownership.
 * Root reference definitions reuse these same destination/title scanners. */
typedef struct md_link_parts {
    size_t destination_begin,destination_end;
    size_t title_begin,title_end;
    bool has_title;
} md_link_parts;

/* Destination and title scanners are shared by inline targets and root
 * definitions. The definition collector supplies physical-line ownership;
 * neither scanner reparses decoded metadata or publishes partial nodes. */
static bool md_link_destination_scan(const char *s,size_t n,size_t *at,
                                     md_link_parts *parts) {
    size_t p=*at,begin=p,end=p;
    if(p<n && s[p]=='<') {
        begin=++p;
        while(p<n && s[p]!='>') {
            if(s[p]=='\n' || s[p]=='\r' || s[p]=='<')return false;
            if(s[p]=='\\' && p+1u<n && md_ascii_punct(s[p+1u]))p+=2u;
            else ++p;
        }
        if(p>=n)return false;
        end=p++;
    } else {
        unsigned depth=0u;
        while(p<n) {
            const char ch=s[p];
            if(ch=='\\' && p+1u<n && md_ascii_punct(s[p+1u])) {
                p+=2u;continue;
            }
            if(ch=='\n' || ch=='\r' || ch=='<' || ch=='>' ||
               md_space(ch))break;
            if(ch=='(') {
                if(depth>=32u)return false;
                ++depth;
            } else if(ch==')') {
                if(!depth)break;
                --depth;
            }
            ++p;
        }
        if(depth)return false;
        end=p;
    }
    parts->destination_begin=begin;parts->destination_end=end;
    *at=p;
    return true;
}

/* Only an unclosed title reports incomplete. Invalid nesting, blank lines
 * and trailing garbage are not requests to consume more physical lines.
 * Inline admission remains single-line; root definitions explicitly opt in
 * after their collector has checked the canonical root block boundary. */
static bool md_link_title_scan(const char *s,size_t n,size_t *at,
                               md_link_parts *parts,bool multiline,
                               bool *incomplete) {
    *incomplete=false;
    size_t p=*at;
    if(p>=n)return false;
    const char quote=s[p++];
    if(quote!='"' && quote!='\'' && quote!='(')return false;
    const char closer=quote=='('?')':quote;
    const size_t begin=p;
    while(p<n && s[p]!=closer) {
        if(s[p]=='\\' && p+1u<n && md_ascii_punct(s[p+1u])) {
            p+=2u;continue;
        }
        if(quote=='(' && s[p]=='(')return false;
        if(s[p]=='\n' || s[p]=='\r') {
            if(!multiline)return false;
            size_t next=p+1u;
            if(s[p]=='\r' && next<n && s[next]=='\n')++next;
            while(next<n && md_space(s[next]))++next;
            if(next<n && (s[next]=='\n' || s[next]=='\r'))return false;
        }
        ++p;
    }
    if(p>=n) {*incomplete=true;return false;}
    parts->title_begin=begin;parts->title_end=p;
    parts->has_title=true;*at=p+1u;
    return true;
}

static bool md_link_target_parse(const char *s,size_t n,size_t round_open,
                                 md_link_parts *out,size_t *round_close) {
    if(!s || !out || !round_close || round_open>=n ||
       s[round_open]!='(')return false;
    size_t p=round_open+1u;
    while(p<n && md_space(s[p]))++p;
    md_link_parts parts={0};
    if(!md_link_destination_scan(s,n,&p,&parts))return false;
    const size_t separator_start=p;
    size_t separator_lfs=0u;
    /* CommonMark #510: after a complete destination, one physical LF
     * may separate an inline title from its URI. This is shared grammar,
     * but only a scoped ROOT paragraph lookahead can supply that LF. Do
     * not admit split destinations (#490/#491) or a missing title. */
    while(p<n && (md_space(s[p]) || s[p]=='\n')) {
        if(s[p]=='\n' && ++separator_lfs>1u)return false;
        ++p;
    }
    if(p<n && s[p]==')') {
        if(separator_lfs)return false;
        *out=parts;*round_close=p;return true;
    }
    bool incomplete=false;
    if(p==separator_start ||
       !md_link_title_scan(s,n,&p,&parts,false,&incomplete))return false;
    while(p<n && md_space(s[p]))++p;
    if(p>=n || s[p]!=')')return false;
    *out=parts;*round_close=p;
    return true;
}

/* Reference labels are raw syntax, not link TEXT: backticks do not hide a
 * bracket, and an escaped bracket is never a delimiter. Keys retain every
 * escape byte, so [foo\\!] does NOT match [foo!]. */
static size_t md_reference_label_close(const char *s,size_t n,size_t begin) {
    for(size_t p=begin;p<n && p-begin<=MD_REF_KEY_MAX;) {
        if(s[p]=='\\' && p+1u<n && md_ascii_punct(s[p+1u])) {
            p+=2u;continue;
        }
        if(s[p]==']')return p;
        if(s[p]=='[' || s[p]=='\n' || s[p]=='\r')return SIZE_MAX;
        ++p;
    }
    return SIZE_MAX;
}

/* Preserve source label bytes, collapse ASCII horizontal whitespace, then
 * apply the single provider-owned Unicode 17 Default Full Case Folding.
 * A raw label stays <=128 bytes; its folded expansion has a separate bounded
 * 512-byte owner slot. Never use locale transforms or a private Unicode table.
 * Both measure and write are fully validated and failure-atomic. */
static bool md_reference_key(const char *s,size_t b,size_t e,
                             char key[MD_REF_FOLD_MAX+1u]) {
    if(b>=e || e-b>MD_REF_KEY_MAX)return false;
    char canonical[MD_REF_KEY_MAX+1u];
    size_t used=0u;
    bool gap=false;
    for(size_t i=b;i<e;++i) {
        const unsigned char ch=(unsigned char)s[i];
        if(ch==' ' || ch=='\t') {
            if(used)gap=true;
            continue;
        }
        /* UTF-8 bytes are admitted only after the full Salts folding scan.
         * Keep the prior raw Markdown ASCII-escape spelling contract. */
        if(ch<0x80u &&
           !( (ch>='A'&&ch<='Z') || (ch>='a'&&ch<='z') ||
              (ch>='0'&&ch<='9') || md_ascii_punct((char)ch)))
            return false;
        if(ch=='[' || ch==']')return false;
        if(gap) {
            if(used>=MD_REF_KEY_MAX)return false;
            canonical[used++]=' ';
            gap=false;
        }
        if(used>=MD_REF_KEY_MAX)return false;
        canonical[used++]=(char)ch;
        if(ch=='\\' && i+1u<e && md_ascii_punct(s[i+1u])) {
            if(used>=MD_REF_KEY_MAX)return false;
            canonical[used++]=s[++i];
        }
    }
    if(!used)return false;
    const vstr borrowed=vstr_from_buf(canonical,used);
    size_t folded=SIZE_MAX;
    if(salts_unicode_case_fold(borrowed,NULL,0u,&folded)!=SALTS_UNICODE_OK ||
       !folded || folded>MD_REF_FOLD_MAX)return false;
    size_t written=SIZE_MAX;
    if(salts_unicode_case_fold(borrowed,key,MD_REF_FOLD_MAX,&written)!=
       SALTS_UNICODE_OK || written!=folded)return false;
    key[written]='\0';
    return true;
}

static const md_reference_binding *md_reference_lookup(
    const salts_md_document *d,const char *s,size_t begin,size_t end) {
    if(!d->reference_count)return NULL;
    char key[MD_REF_FOLD_MAX+1u];
    if(!md_reference_key(s,begin,end,key))return NULL;
    for(size_t i=0u;i<d->reference_count;++i)
        if(!strcmp(d->references[i].key,key))
            return &d->references[i]; /* first matching definition wins */
    return NULL;
}

/* A link containing another active LINK is forbidden, but nested literal
 * square brackets are permitted. Check candidates strictly inside the
 * visible label after finding its balanced closer. Only real inline targets
 * or resolved references are active; images do not count as nested links.
 * All lookups borrow the parse-local reference table. */
static bool md_link_visible_has_active_link(const salts_md_document *d,
                                          const char *s,size_t begin,
                                          size_t end) {
    for(size_t p=begin;p<end;) {
        if(s[p]=='\\' && p+1u<end &&
           md_ascii_punct(s[p+1u])) {p+=2u;continue;}
        if(s[p]==(char)0x60) {
            size_t next=p+1u;
            while(next<end && s[next]==(char)0x60)++next;
            const size_t closer=md_code_span_close(s,end,next,next-p);
            p=closer==SIZE_MAX?next:closer+(next-p);
            continue;
        }
        if(s[p]=='<') {
            size_t close=SIZE_MAX;
            if(md_uri_autolink(s,end,p,&close) ||
               md_email_autolink(s,end,p,&close))
                return true; /* active LINK, even inside visible brackets */
            close=md_opaque_html_tag_close(s,end,p);
            if(close!=SIZE_MAX) {p=close+1u;continue;}
        }
        if(s[p]=='[' && (p==0u || s[p-1u]!='!' ||
                         (p>=2u && s[p-2u]=='\\'))) {
            const size_t inner=md_link_text_close(s,end,p+1u);
            if(inner!=SIZE_MAX && inner<end) {
                if(inner+1u<end && s[inner+1u]=='(') {
                    md_link_parts parts={0};size_t round=SIZE_MAX;
                    if(md_link_target_parse(s,end,inner+1u,&parts,&round)
                       && round<end)return true;
                }
                if(inner+1u<end && s[inner+1u]=='[') {
                    const size_t second=md_reference_label_close(
                        s,end,inner+2u);
                    if(second!=SIZE_MAX && second<end) {
                        const bool collapsed=second==inner+2u;
                        if(md_reference_lookup(
                            d,s,collapsed?p+1u:inner+2u,
                            collapsed?inner:second))return true;
                    }
                } else if(md_reference_lookup(d,s,p+1u,inner))
                    return true;
            }
        }
        ++p;
    }
    return false;
}

/* Full, collapsed and shortcut reference matching is shared by delimiter
 * opacity and RichDoc emission. Never bind an outer LINK around an already
 * active inner LINK (CommonMark #532/#533). */
static bool md_reference_inline_match(
    const salts_md_document *d,const char *s,size_t n,size_t at,
    bool image,const md_reference_binding **binding,size_t *label_end,
    size_t *token_end) {
    if(!d->reference_count || !binding || !label_end || !token_end ||
       at>=n || s[at]!='[')return false;
    const size_t first=md_link_text_close(s,n,at+1u);
    if(first==SIZE_MAX)return false;
    if(first+1u<n && s[first+1u]=='[') {
        const size_t second=md_reference_label_close(s,n,first+2u);
        if(second!=SIZE_MAX) {
            const bool collapsed=second==first+2u;
            const md_reference_binding *found=md_reference_lookup(
                d,s,collapsed?at+1u:first+2u,collapsed?first:second);
            if(found) {
                if(!image &&
                   md_link_visible_has_active_link(d,s,at+1u,first))
                    return false;
                *binding=found;*label_end=first;*token_end=second+1u;
                return true;
            }
            return false;
        }
    }
    const md_reference_binding *found=
        md_reference_lookup(d,s,at+1u,first);
    if(!found || (!image && md_link_visible_has_active_link(
                      d,s,at+1u,first)))
        return false;
    *binding=found;*label_end=first;*token_end=first+1u;
    return true;
}

/* URI autolink SCHEME SYNTAX is independent of the production scheme
 * allowlist. Approved schemes remain owned LINK nodes. Other syntactically
 * valid schemes use an explicit URI_AUTOLINK leaf: SAFE escapes its original
 * angle source; only test-only SPEC emits an anchor. Never normalize or
 * decode the literal visible URI as Markdown syntax. */
static bool md_autolink_scheme(const char *s,size_t b,size_t e) {
    static const char *const names[]={"http","https","mailto"};
    for(size_t k=0u;k<sizeof(names)/sizeof(names[0]);++k) {
        const char *name=names[k];
        if(strlen(name)!=e-b)continue;
        bool same=true;
        for(size_t i=b;i<e;++i) {
            const unsigned char c=(unsigned char)s[i];
            const unsigned char lowered=c>='A'&&c<='Z'?
                (unsigned char)(c-'A'+'a'):c;
            if(lowered!=(unsigned char)name[i-b]) {same=false;break;}
        }
        if(same)return true;
    }
    return false;
}
static bool md_uri_autolink(const char *s,size_t n,size_t at,
                            size_t *out_close) {
    if(!s||!out_close||at>=n||s[at]!='<')return false;
    const size_t b=at+1u;
    if(b>=n)return false;
    const unsigned char first=(unsigned char)s[b];
    if(!((first>='A'&&first<='Z')||(first>='a'&&first<='z')))
        return false;
    size_t colon=b;
    while(colon<n && colon-b<=32u) {
        const unsigned char c=(unsigned char)s[colon];
        const bool alpha=(c>='A'&&c<='Z')||(c>='a'&&c<='z');
        const bool digit=c>='0'&&c<='9';
        if(!alpha&&!digit&&c!='+'&&c!='-'&&c!='.')break;
        ++colon;
    }
    if(colon>=n||s[colon]!=':'||colon-b<2u||
       colon-b>32u)return false;
    size_t close=colon+1u;
    if(close>=n)return false;
    const size_t start=close;
    while(close<n && s[close]!='>') {
        const unsigned char c=(unsigned char)s[close];
        if(c<=0x20u||c==0x7fu||c=='<')return false;
        ++close;
    }
    if(close>=n||close==start)return false;
    *out_close=close;
    return true;
}


/* Email autolinks follow HTML5 local-atom and bounded ASCII domain labels.
 * URI and email are opaque to the single emphasis-delimiter scanner. */
static bool md_email_local_atom(unsigned char c) {
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9'))
        return true;
    switch(c) {
    case '.':case '!':case '#':case '$':case '%':case '&':case '\'':
    case '*':case '+':case '/':case '=':case '?':case '^':case '_':
    case 0x60:case '{':case '|':case '}':case '~':case '-':return true;
    default:return false;
    }
}
static bool md_email_domain_alnum(unsigned char c) {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||
           (c>='0'&&c<='9');
}
static bool md_email_autolink(const char *s,size_t n,size_t at,
                              size_t *out_close) {
    if(!s||!out_close||at>=n||s[at]!='<')return false;
    size_t p=at+1u,first=p;
    while(p<n&&md_email_local_atom((unsigned char)s[p]))++p;
    if(p==first||p>=n||s[p]!='@')return false;
    ++p;
    /* 1..63 ASCII bytes per DNS label; '-' is interior only. */
    for(;;) {
        if(p>=n||!md_email_domain_alnum((unsigned char)s[p]))
            return false;
        const size_t label=p++;
        while(p<n&&(md_email_domain_alnum((unsigned char)s[p])||s[p]=='-')) {
            if(p-label>=63u)return false;
            ++p;
        }
        if(p-label>63u||s[p-1u]=='-')return false;
        if(p<n&&s[p]=='.'){++p;continue;}
        break;
    }
    if(p>=n||s[p]!='>')return false;
    *out_close=p;
    return true;
}

/* CommonMark backtick delimiters close only on an EXACTLY equal-sized run.
 * Unlike emphasis/link matching, backslashes are ordinary literal payload
 * inside a code span, including when directly preceding a backtick. Skip
 * entire mismatching runs so their suffix cannot impersonate a delimiter. */
static size_t md_code_span_close(const char *s,size_t n,size_t begin,
                                 size_t marker_size) {
    for(size_t p=begin;p<n;) {
        if(s[p]!=(char)0x60) { ++p;continue; }
        size_t end=p+1u;
        while(end<n && s[end]==(char)0x60) ++end;
        if(end-p==marker_size) return p;
        p=end;
    }
    return SIZE_MAX;
}

/* Normalize code-span payload into owned semantic UTF-8. Each LF/CRLF
 * line ending becomes ONE ASCII space. Trim exactly one edge space when
 * both edges are spaces and the payload is not all spaces. Backslashes,
 * other interior spaces and Unicode bytes are literal. The scratch buffer
 * cannot exceed the already admitted raw-source budget; publication still
 * goes through the single RichDoc text arena and failure-atomic md_text. */
static void md_code_span_text(salts_md_document *d,uint32_t parent,
                              const char *s,size_t start,size_t end) {
    const size_t n=end-start;
    const bool multiline=memchr(s+start,'\n',n)!=NULL ||
                         memchr(s+start,'\r',n)!=NULL;
    char *normalized=NULL;
    const char *text=s+start;
    size_t begin=0u,bytes=n;
    if(multiline) {
        normalized=(char*)malloc(n+1u);
        if(!normalized) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return;}
        bytes=0u;
        for(size_t i=start;i<end;++i) {
            const char c=s[i];
            if(c=='\r') {
                if(i+1u<end && s[i+1u]=='\n') ++i;
                normalized[bytes++]=' ';
            } else if(c=='\n') normalized[bytes++]=' ';
            else normalized[bytes++]=c;
        }
        text=normalized;
    }
    if(bytes>1u && text[0]==' ' && text[bytes-1u]==' ') {
        bool nonspace=false;
        for(size_t i=0u;i<bytes;++i)
            if(text[i]!=' ') {nonspace=true;break;}
        if(nonspace) {++begin;--bytes;}
    }
    md_text(d,parent,text+begin,bytes-begin);
    free(normalized);
}

/* Runs retain their original source offsets, with closers consuming from the
 * LEFT and openers from the RIGHT. Each consumed marker receives exactly one
 * event; the source buffer itself is never changed. This is one C11 grammar,
 * not a fallback renderer or a second Markdown parser. */
typedef struct md_emph_run {
    size_t start, remaining, consumed_left;
    char marker;
    bool can_open, can_close, inactive;
} md_emph_run;

typedef struct md_emph_event {
    uint8_t open, close; /* 1: emphasis, 2: strong, 0: literal */
} md_emph_event;

/* Build all * / _ pairs before emitting RichDoc nodes. This is essential
 * when one delimiter run is both opener and closer, when runs have lengths
 * 1/2/3+, or when an inner opener owns the nearer closer. Code/link/strike
 * payloads are scanned by their existing canonical inline semantics.
 *
 * A pair is forbidden by CommonMark's rule of three if either endpoint can
 * both open and close, the combined remaining lengths are divisible by 3,
 * and at least one remaining length is NOT divisible by 3.
 *
 * Comparison work is capped relative to this inline source's byte count:
 * malicious alternating runs fail atomically instead of searching forever. */
static bool md_emph_pairs(salts_md_document *d,const char *s,size_t n,
                          md_emph_event *events) {
    if(n>SIZE_MAX/sizeof(md_emph_run)) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    md_emph_run *runs=(md_emph_run*)malloc((n?n:1u)*sizeof(*runs));
    if(!runs) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return false;}
    size_t count=0u;
    for(size_t p=0u;p<n;) {
        /* URI autolinks bind tighter than emphasis delimiters. The URI
         * label's ** / __ / escaped punctuation is opaque to the same
         * run collector that builds emphasis events; otherwise unmatched
         * external runs could close inside LINK text and invalidate the
         * owned RichDoc (official CommonMark #480/#481). */
        if(s[p]=='<') {
            size_t close=SIZE_MAX;
            if(md_uri_autolink(s,n,p,&close) ||
               md_email_autolink(s,n,p,&close)) {
                p=close+1u;
                continue;
            }
            close=md_inline_html_tag_close(s,n,p);
            if(close!=SIZE_MAX) {p=close+1u;continue;}
        }
        if(s[p]=='\\' && p+1u<n && md_ascii_punct(s[p+1u])) {
            p+=2u;continue;
        }
        if(s[p]==(char)0x60) {
            size_t end=p+1u;
            while(end<n && s[end]==(char)0x60) ++end;
            const size_t closer=md_code_span_close(s,n,end,end-p);
            p=closer==SIZE_MAX?end:closer+end-p;
            continue;
        }
        if(s[p]=='[' || (s[p]=='!' && p+1u<n && s[p+1u]=='[')) {
            const size_t b=p+(s[p]=='!'?2u:1u);
            const size_t square=md_link_text_close(s,n,b);
            if(square!=SIZE_MAX && square+1u<n && s[square+1u]=='(') {
                size_t round=SIZE_MAX;
                md_link_parts target={0};
                if(md_link_target_parse(
                       s,n,square+1u,&target,&round) &&
                   (s[p]=='!' || !md_link_visible_has_active_link(
                       d,s,b,square))) {
                    p=round+1u;continue;
                }
            }
            {
                const bool image=s[p]=='!';
                const md_reference_binding *ref=NULL;
                size_t label_end=0u,token_end=0u;
                if(md_reference_inline_match(
                       d,s,n,p+(image?1u:0u),image,&ref,
                       &label_end,&token_end)) {
                    p=token_end;continue;
                }
            }
        }
        if(s[p]=='~' && p+1u<n && s[p+1u]=='~') {
            const size_t closer=md_match(s,n,p+2u,'~',2u);
            if(closer!=SIZE_MAX) {p=closer+2u;continue;}
        }
        if(s[p]=='*' || s[p]=='_') {
            size_t end=p+1u;
            while(end<n && s[end]==s[p]) ++end;
            bool can_open=false,can_close=false;
            md_emph_flanking(s,n,p,end-p,s[p],&can_open,&can_close);
            if(can_open || can_close)
                runs[count++]=(md_emph_run){
                    .start=p,.remaining=end-p,.consumed_left=0u,
                    .marker=s[p],.can_open=can_open,.can_close=can_close,
                    .inactive=false
                };
            p=end;continue;
        }
        ++p;
    }
    const size_t limit=n>(SIZE_MAX-64u)/64u?
                        SIZE_MAX:n*64u+64u;
    size_t examined=0u;
    for(size_t c=0u;c<count && d->error==SALTS_MD_OK;++c) {
        md_emph_run *closer=&runs[c];
        if(!closer->can_close || closer->inactive) continue;
        while(closer->remaining) {
            size_t candidate=SIZE_MAX;
            for(size_t k=c;k>0u;) {
                --k;
                if(++examined>limit) {
                    md_error(d,SALTS_MD_CAPACITY_EXCEEDED);break;
                }
                const md_emph_run *opener=&runs[k];
                if(opener->inactive || !opener->remaining ||
                   !opener->can_open || opener->marker!=closer->marker)
                    continue;
                const size_t sum_mod3=(opener->remaining%3u+
                                       closer->remaining%3u)%3u;
                const bool odd_match=
                    (opener->can_close || closer->can_open) &&
                    sum_mod3==0u &&
                    (opener->remaining%3u!=0u ||
                     closer->remaining%3u!=0u);
                if(!odd_match) {candidate=k;break;}
            }
            if(d->error!=SALTS_MD_OK || candidate==SIZE_MAX) break;
            md_emph_run *opener=&runs[candidate];
            const uint8_t width=opener->remaining>=2u &&
                                closer->remaining>=2u?2u:1u;
            const size_t opening=opener->start+opener->consumed_left+
                                 opener->remaining-width;
            const size_t closing=closer->start+closer->consumed_left;
            if(opening>=n || closing>=n ||
               events[opening].open || events[closing].close ||
               opening+width>closing) {
                md_error(d,SALTS_MD_INVALID_ARGUMENT);break;
            }
            events[opening].open=width;
            events[closing].close=width;
            opener->remaining-=width;
            closer->remaining-=width;
            closer->consumed_left+=width;
            /* Delimiters enclosed by a completed match cannot subsequently
             * participate in a crossing pair. They remain literal text if
             * not already consumed by a nested pair. */
            for(size_t j=candidate+1u;j<c;++j)
                runs[j].inactive=true;
        }
    }
    free(runs);
    return d->error==SALTS_MD_OK;
}

/* Emit the paired spans as typed nodes in source order. Node ownership is
 * explicit, with a bounded ancestor stack; escapes and code/link payloads
 * still follow their original single C11 producer. */
typedef enum md_root_break {
    MD_ROOT_BREAK_SOFT = 0,
    MD_ROOT_BREAK_SPACES,
    MD_ROOT_BREAK_BACKSLASH
} md_root_break;

static md_root_break md_root_line_content(const char *s,size_t begin,
                                           size_t end,bool continues,
                                           size_t *content_end) {
    size_t trimmed=md_right(s,begin,end);
    *content_end=trimmed; /* one trailing space/tab is not semantic TEXT */
    if(!continues || trimmed==begin)return MD_ROOT_BREAK_SOFT;
    /* CommonMark #633–#635: two or more literal ASCII spaces before LF.
     * Tab-column hard-break interpretation remains a separate #185 slice. */
    if(end-trimmed>=2u) {
        bool only_spaces=true;
        for(size_t i=trimmed;i<end;++i)
            if(s[i]!=' ') {only_spaces=false;break;}
        if(only_spaces)return MD_ROOT_BREAK_SPACES;
    }
    /* CommonMark #634/#637: ONLY an unescaped final backslash marks a hard
     * break. Even-length trailing backslash runs retain literal backslashes. */
    if(trimmed==end && s[end-1u]=='\\') {
        size_t at=end;
        while(at>begin && s[at-1u]=='\\')--at;
        if((end-at)%2u!=0u) {
            *content_end=end-1u;
            return MD_ROOT_BREAK_BACKSLASH;
        }
    }
    return MD_ROOT_BREAK_SOFT;
}

static bool md_root_emit_break(salts_md_document *d,uint32_t paragraph,
                               md_root_break kind) {
    const bool hard=kind!=MD_ROOT_BREAK_SOFT;
    const uint32_t node=md_add(d,paragraph,hard?
        SALTS_MD_BLOCK_HARD_LINE_BREAK:SALTS_MD_BLOCK_LINE_BREAK,0u);
    if(node==UINT32_MAX)return false;
    if(hard) {
        const salts_md_line_break_semantics info={
            (uint8_t)(kind==MD_ROOT_BREAK_SPACES ?
             SALTS_MD_LINE_BREAK_TRAILING_SPACES :
             SALTS_MD_LINE_BREAK_BACKSLASH),{0u,0u,0u}
        };
        md_meta(d,node,(const char*)&info,sizeof(info));
    }
    return d->error==SALTS_MD_OK;
}

static bool md_inline(salts_md_document *d, uint32_t parent,
                      const char *s, size_t n, unsigned depth) {
    if(depth>32u || n>SIZE_MAX/sizeof(md_emph_event)) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    md_emph_event *events=(md_emph_event*)calloc(
        n?n:1u,sizeof(*events));
    if(!events) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return false;}
    if(!md_emph_pairs(d,s,n,events)) {
        free(events);return false;
    }
    uint32_t owners[33]={0u};
    owners[0]=parent;
    unsigned nested=0u;
    size_t p=0u,text_start=0u,physical_line_start=0u;
    while(p<n && d->error==SALTS_MD_OK) {
        if(events[p].close) {
            if(!nested) {md_error(d,SALTS_MD_INVALID_ARGUMENT);break;}
            md_text(d,owners[nested],s+text_start,p-text_start);
            --nested;
            p+=events[p].close;
            text_start=p;continue;
        }
        if(events[p].open) {
            if(depth+nested>=32u) {
                md_error(d,SALTS_MD_CAPACITY_EXCEEDED);break;
            }
            md_text(d,owners[nested],s+text_start,p-text_start);
            const salts_md_block_kind kind=events[p].open==2u?
                SALTS_MD_BLOCK_STRONG:SALTS_MD_BLOCK_EMPHASIS;
            const uint32_t node=md_add(d,owners[nested],kind,0u);
            if(node==UINT32_MAX) break;
            owners[++nested]=node;
            p+=events[p].open;
            text_start=p;continue;
        }
        /* A single paragraph-scoped inline source can span physical LF
         * boundaries. The same #185 source-origin classifier trims semantic
         * whitespace/backslash BEFORE publishing TEXT, and publishes a
         * hard or soft break under the actual nested Emphasis/Strong owner.
         * Code spans are opaque and remain literal under this path. */
        if(s[p]=='\n') {
            /* Classify the physical line, NOT only the remaining TEXT
             * suffix: inner strong/link owners may have closed immediately
             * before the hard-break whitespace. Keep output TEXT trimmed
             * from its own admitted span without reviving source markers. */
            size_t content_end=p;
            const md_root_break kind=md_root_line_content(
                s,physical_line_start,p,true,&content_end);
            if(content_end<text_start)content_end=text_start;
            md_text(d,owners[nested],s+text_start,content_end-text_start);
            if(!md_root_emit_break(d,owners[nested],kind))break;
            ++p;
            text_start=physical_line_start=p;
            continue;
        }
        /* URI and email angle tokens are opaque, owned LINK nodes.
         * The implicit mailto: prefix is href metadata only. */
        if(s[p]=='<') {
            size_t close=SIZE_MAX;
            const bool uri=md_uri_autolink(s,n,p,&close);
            const bool email=!uri&&md_email_autolink(s,n,p,&close);
            if(uri||email) {
                md_text(d,owners[nested],s+text_start,p-text_start);
                const size_t bytes=close-p-1u;
                /* A non-approved but valid RFC-style scheme is still
                 * opaque Markdown syntax. Do not manufacture a navigable
                 * LINK for a production consumer: preserve owned literal
                 * bytes in a dedicated leaf, letting SAFE escape it. */
                size_t colon=p+1u;
                if(uri)while(colon<close && s[colon]!=':')++colon;
                const bool approved=email ||
                    (uri && md_autolink_scheme(s,p+1u,colon));
                if(!approved) {
                    const uint32_t node=md_add(
                        d,owners[nested],SALTS_MD_BLOCK_URI_AUTOLINK,0u);
                    if(node==UINT32_MAX)break;
                    md_meta(d,node,s+p+1u,bytes);
                    size_t first=0u,last=0u;
                    if(md_store(d,false,s+p+1u,bytes,&first,&last)) {
                        d->nodes[node].byte_begin=first;
                        d->nodes[node].byte_end=last;
                    }
                    if(d->error!=SALTS_MD_OK)break;
                    p=close+1u;text_start=p;continue;
                }
                const uint32_t link=md_add(
                    d,owners[nested],SALTS_MD_BLOCK_LINK,0u);
                if(link==UINT32_MAX)break;
                if(email) {
                    /* Bounded contiguous metadata writes: no transient
                     * global registry, heap string, or alternative parser.
                     * Any failure rolls back this unpublished RichDoc. */
                    size_t first=0u,middle=0u,tail=0u,end=0u;
                    if(!md_store(d,true,"mailto:",7u,&first,&middle) ||
                       !md_store(d,true,s+p+1u,bytes,&tail,&end))
                        break;
                    if(tail!=middle) {
                        md_error(d,SALTS_MD_INVALID_ARGUMENT);break;
                    }
                    d->node_metadata[link]=
                        (salts_md_block_metadata){first,end};
                } else md_meta(d,link,s+p+1u,bytes);
                md_text(d,link,s+p+1u,bytes);
                if(d->error!=SALTS_MD_OK)break;
                p=close+1u;
                text_start=p;
                continue;
            }
            close=md_inline_html_tag_close(s,n,p);
            if(close!=SIZE_MAX) {
                md_text(d,owners[nested],s+text_start,p-text_start);
                md_raw_html_inline(d,owners[nested],s+p,close+1u-p);
                if(d->error!=SALTS_MD_OK)break;
                p=close+1u;text_start=p;continue;
            }
        }
        if(s[p]=='\\' && p+1u<n && md_ascii_punct(s[p+1u])) {
            md_text(d,owners[nested],s+text_start,p-text_start);
            md_text(d,owners[nested],s+p+1u,1u);
            p+=2u;text_start=p;continue;
        }
        /* #185: decode a recognized HTML5 entity as SEMANTIC UTF-8 only
         * after raw-source punctuation/escape/code admission. The decoded
         * value is never rescanned as a Markdown delimiter, so &#42; cannot
         * manufacture emphasis or a list marker. Code spans remain opaque;
         * malformed or semicolon-less references remain literal source. */
        if(s[p]=='&') {
            char decoded[8];
            size_t consumed=0u,written=0u;
            if(salts_md_decode_character_reference(
                    s+p,n-p,&consumed,decoded,&written)) {
                md_text(d,owners[nested],s+text_start,p-text_start);
                md_text(d,owners[nested],decoded,written);
                p+=consumed;
                text_start=p;
                continue;
            }
        }
        if(s[p]==(char)0x60) {
            size_t end=p+1u;
            while(end<n && s[end]==(char)0x60) ++end;
            const size_t close=md_code_span_close(s,n,end,end-p);
            if(close!=SIZE_MAX) {
                md_text(d,owners[nested],s+text_start,p-text_start);
                const uint32_t node=md_add(d,owners[nested],
                    SALTS_MD_BLOCK_CODE_SPAN,0u);
                if(node==UINT32_MAX) break;
                md_code_span_text(d,node,s,end,close);
                p=close+end-p;text_start=p;
            } else p=end;
            continue;
        }
        if(s[p]=='~' && p+1u<n && s[p+1u]=='~') {
            const size_t close=md_match(s,n,p+2u,'~',2u);
            if(close!=SIZE_MAX) {
                md_text(d,owners[nested],s+text_start,p-text_start);
                const uint32_t node=md_add(d,owners[nested],
                    SALTS_MD_BLOCK_STRIKETHROUGH,0u);
                if(node==UINT32_MAX) break;
                if(!md_inline(d,node,s+p+2u,close-(p+2u),
                              depth+nested+1u)) break;
                p=close+2u;text_start=p;continue;
            }
        }
        if(s[p]=='[' || (s[p]=='!' && p+1u<n && s[p+1u]=='[')) {
            const bool image=s[p]=='!';
            const size_t b=p+(image?2u:1u);
            const size_t square=md_link_text_close(s,n,b);
            if(square!=SIZE_MAX && square+1u<n && s[square+1u]=='(') {
                size_t round=SIZE_MAX;
                md_link_parts target={0};
                if(md_link_target_parse(
                       s,n,square+1u,&target,&round) &&
                   (image || !md_link_visible_has_active_link(
                       d,s,b,square))) {
                    /* Decode metadata before publishing nodes. An invalid
                     * or unsafe decoded href still fails closed in the HTML
                     * renderer; no URL filtering is delegated to the text
                     * grammar or weakened by title syntax. */
                    char *dest=NULL,*title=NULL;
                    size_t dest_len=0u,title_len=0u;
                    if(!md_decode_component(d,
                            s+target.destination_begin,
                            target.destination_end-
                            target.destination_begin,&dest,&dest_len))
                        break;
                    if(target.has_title &&
                       !md_decode_component(d,
                            s+target.title_begin,
                            target.title_end-target.title_begin,
                            &title,&title_len)) {
                        free(dest);break;
                    }
                    md_text(d,owners[nested],s+text_start,p-text_start);
                    const uint32_t node=md_add(d,owners[nested],
                        image?SALTS_MD_BLOCK_IMAGE:SALTS_MD_BLOCK_LINK,0u);
                    if(node!=UINT32_MAX) {
                        md_meta(d,node,dest,dest_len);
                        if(target.has_title) {
                            const uint32_t title_node=md_add(
                                d,node,image?SALTS_MD_BLOCK_IMAGE_TITLE:
                                       SALTS_MD_BLOCK_LINK_TITLE,0u);
                            if(title_node!=UINT32_MAX)
                                md_meta(d,title_node,title,title_len);
                        }
                    }
                    free(title);free(dest);
                    if(node==UINT32_MAX || d->error!=SALTS_MD_OK)break;
                    if(!md_inline(d,node,s+b,square-b,
                                  depth+nested+1u))break;
                    p=round+1u;text_start=p;continue;
                }
            }
            {
                const md_reference_binding *ref=NULL;
                size_t first_end=0u,token_end=0u;
                const size_t opener=p+(image?1u:0u);
                if(md_reference_inline_match(
                       d,s,n,opener,image,&ref,&first_end,&token_end)) {
                    md_text(d,owners[nested],s+text_start,p-text_start);
                    const uint32_t node=md_add(
                        d,owners[nested],
                        image?SALTS_MD_BLOCK_IMAGE:SALTS_MD_BLOCK_LINK,0u);
                    if(node==UINT32_MAX)break;
                    md_meta(d,node,ref->href,ref->href_bytes);
                    if(ref->has_title) {
                        const uint32_t title=md_add(
                            d,node,image?SALTS_MD_BLOCK_IMAGE_TITLE:
                                         SALTS_MD_BLOCK_LINK_TITLE,0u);
                        if(title==UINT32_MAX)break;
                        md_meta(d,title,ref->title,ref->title_bytes);
                    }
                    if(d->error!=SALTS_MD_OK)break;
                    if(!md_inline(d,node,s+opener+1u,
                                  first_end-opener-1u,
                                  depth+nested+1u))break;
                    p=token_end;
                    text_start=p;
                    continue;
                }
            }
        }
        ++p;
    }
    if(d->error==SALTS_MD_OK && nested)
        md_error(d,SALTS_MD_INVALID_ARGUMENT);
    if(d->error==SALTS_MD_OK)
        md_text(d,owners[0],s+text_start,n-text_start);
    free(events);
    return d->error==SALTS_MD_OK;
}

static bool md_rule(const char *s, size_t b, size_t e) {
    char mark = 0;
    size_t count = 0;
    for (size_t p=b; p<e; ++p) {
        if (md_space(s[p])) continue;
        if (s[p] != '-' && s[p] != '*' && s[p] != '_') return false;
        if (mark && mark != s[p]) return false;
        mark = s[p]; ++count;
    }
    return count >= 3;
}

static unsigned md_setext(const char *s, size_t b, size_t e) {
    b = md_left(s,b,e); e = md_right(s,b,e);
    if (b == e || (s[b] != '=' && s[b] != '-')) return 0;
    const char marker = s[b];
    for (size_t p=b; p<e; ++p) if (s[p] != marker) return 0;
    return marker == '=' ? 1u : 2u;
}

static unsigned md_atx(const char *s, size_t *begin, size_t *end) {
    size_t p = *begin, n = 0;
    while (p < *end && s[p] == '#' && n < 7) { ++p; ++n; }
    if (n < 1 || n > 6 || (p < *end && !md_space(s[p]))) return 0;
    p = md_left(s,p,*end);
    size_t e = md_right(s,p,*end);
    /* Closing ATX markers require whitespace before the trailing # run. */
    size_t x = e;
    while (x > p && s[x-1] == '#') --x;
    if (x < e && (x == p || md_space(s[x-1])))
        e = md_right(s,p,x);
    *begin = p; *end = e;
    return (unsigned)n;
}

static bool md_fence_start(const char *s, size_t b, size_t e,
                           char *marker, size_t *count,
                           size_t *info_begin, size_t *info_end) {
    if (b >= e || (s[b] != (char)0x60 && s[b] != '~')) return false;
    const char ch = s[b];
    size_t p=b;
    while (p<e && s[p]==ch) ++p;
    if (p-b<3) return false;
    if (ch == (char)0x60)
        for (size_t q=p; q<e; ++q)
            if (s[q] == (char)0x60) return false;
    *marker=ch; *count=p-b;
    *info_begin=md_left(s,p,e);
    *info_end=md_right(s,*info_begin,e);
    return true;
}
static bool md_fence_close(const char *s, size_t b, size_t e,
                           md_fence fence) {
    size_t p=b;
    while (p<e && s[p]==fence.marker) ++p;
    if (p-b < fence.count) return false;
    return md_blank(s,p,e);
}

static bool md_list_marker(const char *s, size_t b, size_t e,
                           bool *ordered, size_t *content) {
    if (b>=e) return false;
    if (s[b]=='-' || s[b]=='+' || s[b]=='*') {
        if (b+1<e && !md_space(s[b+1])) return false;
        *ordered=false;
        *content=md_left(s,b+1,e);
        return true;
    }
    size_t p=b;
    while (p<e && p-b<10 && s[p]>='0' && s[p]<='9') ++p;
    if (p==b || p-b>9 || p>=e || (s[p]!='.' && s[p]!=')') ||
        (p+1<e && !md_space(s[p+1]))) return false;
    *ordered=true;
    *content=md_left(s,p+1,e);
    return true;
}

/* GFM task marker: optional leading spaces after the list marker were already
 * consumed by md_list_marker. A marker belongs only to the first paragraph
 * of a list item, not to arbitrary text or link syntax. */
static bool md_task_marker(const char *s, size_t start, size_t end,
                           char *state, size_t *content) {
    if(end-start<3 || s[start]!='[' || s[start+2]!=']') return false;
    const char mark=s[start+1];
    if(mark!=' ' && mark!='\t' && mark!='x' && mark!='X') return false;
    const size_t after=start+3u;
    /* A bare '[x]' or '[ ]' without whitespace AFTER the closing ] is
     * ordinary list text, not a GFM task marker. */
    if(after==end || !md_space(s[after])) return false;
    *state=(mark=='x'||mark=='X') ? 'x' : ' ';
    *content=md_left(s,after,end);
    return true;
}


enum { MD_LIST_MAX_DEPTH = 32 };
typedef struct md_list_marker_info {
    size_t column;
    size_t content_column;
    size_t content_offset;
    uint32_t start;
    char marker;
    bool ordered;
    bool expanded_tab_padding;
} md_list_marker_info;
typedef struct md_list_frame {
    uint32_t list, item, paragraph, code, quote, quote_paragraph;
    md_fence fence;
    unsigned html_kind; /* per-item canonical raw HTML block lifetime */
    uint32_t html_node;
    bool fence_open;
    size_t code_blank_lines;
    size_t marker_column, content_column, list_base_column;
    char marker;
    bool ordered, pending_blank, empty_item;
} md_list_frame;
typedef struct md_list_stack {
    md_list_frame frames[MD_LIST_MAX_DEPTH];
    size_t depth;
} md_list_stack;

/* CommonMark indentation is measured in source columns. Relative container
 * indentation is derived after matching the outer container; a tab occupies
 * the remainder of its four-column stop. */
static size_t md_columns(const char *s,size_t begin,size_t end) {
    size_t col=0u;
    for(size_t p=begin;p<end;++p) {
        if(s[p]==' ') ++col;
        else if(s[p]=='\t') col+=4u-(col%4u);
        else break;
    }
    return col;
}
static size_t md_skip_columns(const char *s,size_t begin,size_t end,
                              size_t wanted) {
    size_t col=0u, p=begin;
    while(p<end && col<wanted && md_space(s[p])) {
        col+=s[p]=='\t'?4u-(col%4u):1u;
        ++p;
    }
    return col>=wanted?p:begin;
}

/* List content columns are relative to the physical line's final owner
 * (root or marked quote). Count TAB stops from original source positions. */
static bool md_list_code_line(salts_md_document *d,md_list_frame *f,
                              md_line line) {
    const char *s=d->raw;
    const size_t base=md_logical_content_column_at(s,line.begin);
    if(f->content_column>SIZE_MAX-base ||
       base+f->content_column>SIZE_MAX-4u) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    return md_code_line_visual(
        d,f->code,line,md_source_visual_column_at(s,line.begin),
        base+f->content_column+4u);
}

/* Admit a list marker, recording the marker's visual column, exact ordered
 * start, and the item content column. Marker type is part of list identity. */
static bool md_list_token(const char *s,md_line line,
                          md_list_marker_info *out) {
    const size_t b=md_left(s,line.begin,line.end);
    bool ordered=false;
    size_t content=0u;
    if(!md_list_marker(s,b,line.end,&ordered,&content)) return false;
    size_t marker_end=b;
    uint32_t start=0u;
    char marker=s[b];
    if(ordered) {
        while(marker_end<line.end && s[marker_end]>='0' &&
              s[marker_end]<='9') {
            start=start*10u+(uint32_t)(s[marker_end]-'0');
            ++marker_end;
        }
        marker=s[marker_end];
        ++marker_end;
    } else ++marker_end;
    const size_t marker_col=md_columns(s,line.begin,b);
    size_t total=marker_col+(marker_end-b);
    for(size_t p=marker_end;p<content;++p)
        total+=s[p]=='\t' ? 4u-(total%4u) : 1u;
    /* Marker-only padding is one column. When 5+ DISPLAY columns
     * separate a nonempty marker from its content, only one belongs to
     * list syntax; the rest is code indentation. Tabs may cross that
     * column and must retain their residual code-space ownership. */
    const size_t marker_end_column=marker_col+(marker_end-b);
    bool expanded_tab_padding=false;
    if(content==line.end) {
        total=marker_end_column+1u;
    } else if(total-marker_end_column>4u) {
        bool ascii_spaces=true;
        for(size_t p=marker_end;p<content;++p)
            if(s[p]!=' ') {ascii_spaces=false;break;}
        total=marker_end_column+1u;
        if(ascii_spaces)content=marker_end+1u;
        else {
            content=marker_end;
            expanded_tab_padding=true;
        }
    }
    *out=(md_list_marker_info){
        marker_col,total,content,start,marker,ordered,expanded_tab_padding
    };
    return true;
}

/* A marker-only item cannot interrupt an existing paragraph, whether it is
 * root-owned or within a Blockquote. Ordered interrupters also start at 1.
 * This is an admission rule, not a restriction on empty sibling list items. */
static bool md_list_interrupts_paragraph(const md_list_marker_info *m,
                                         md_line line) {
    return m->content_offset<line.end &&
           (!m->ordered || m->start==1u);
}

/* Both marker-line and continuation-line fences use the same owned item
 * frame.  A recognized opener is consumed even when allocation fails; the
 * caller will propagate d->error and discard the incomplete document. */
static bool md_list_begin_fence(salts_md_document *d,md_list_frame *f,
                                const char *s,size_t begin,size_t end) {
    char marker=0;
    size_t run=0,info_b=0,info_e=0;
    if(!md_fence_start(s,begin,end,&marker,&run,&info_b,&info_e))
        return false;
    const uint32_t node=md_add(d,f->item,SALTS_MD_BLOCK_CODE_FENCE,0u);
    if(node==UINT32_MAX) return true;
    md_fence_info_meta(d,node,s+info_b,info_e-info_b);
    if(d->error!=SALTS_MD_OK) return true;
    f->fence=(md_fence){marker,run,node};
    f->fence_open=true;
    f->code_blank_lines=0u;
    f->paragraph=f->code=f->quote=f->quote_paragraph=UINT32_MAX;
    return true;
}

/* Recursive admission only follows list markers on the same physical
 * line and is bounded by MD_LIST_MAX_DEPTH. The frame stack, not the C call
 * stack, continues to own all subsequent source-line admission. */
/* One typed GFM TABLE admission shared by root/quote/list owners.
 * Item content columns are relative to their owning root or quoted line;
 * 0 means no list owner. The cursor is advanced only on actual admission. */
static bool md_table_admit(salts_md_document *d,uint32_t parent,
                           md_line header,size_t header_begin,
                           size_t *next_pos,size_t quote_depth,
                           size_t item_column);
static bool md_list_open(salts_md_document *d,md_list_stack *stack,
                         uint32_t parent,const md_list_marker_info *m,
                         const char *s,size_t end,size_t *next_pos,
                         size_t quote_depth);

/* Use the same ATX grammar and owned IR for headings admitted directly
 * within a list item; no competing inline parser is introduced. */
static bool md_list_begin_atx(salts_md_document *d,md_list_frame *f,
                              const char *s,size_t begin,size_t end) {
    size_t text_begin=begin,text_end=end;
    const unsigned level=md_atx(s,&text_begin,&text_end);
    if(!level) return false;
    const uint32_t heading=md_add(d,f->item,SALTS_MD_BLOCK_HEADING,level);
    if(heading!=UINT32_MAX)
        (void)md_inline(d,heading,s+text_begin,text_end-text_begin,0u);
    f->paragraph=f->code=f->quote=f->quote_paragraph=UINT32_MAX;
    return true;
}

/* An inline '>' on a list marker line and subsequent explicitly marked
 * quote lines share the same list-owned quote paragraph. The owned quote
 * node stays a child of the list item; no root quote grammar is duplicated. */
static bool md_list_append_quote(salts_md_document *d,md_list_frame *f,
                                 const char *s,size_t begin,size_t end) {
    if(begin>=end || s[begin]!='>') return false;
    if(f->quote==UINT32_MAX) {
        f->quote=md_add(d,f->item,SALTS_MD_BLOCK_QUOTE,0u);
        if(f->quote==UINT32_MAX) return true;
        f->quote_paragraph=UINT32_MAX;
    }
    const size_t body=md_left(s,begin+1u,end);
    if(body==end) {
        f->quote_paragraph=UINT32_MAX;
    } else {
        if(f->quote_paragraph==UINT32_MAX) {
            f->quote_paragraph=md_add(d,f->quote,SALTS_MD_BLOCK_PARAGRAPH,0u);
            if(f->quote_paragraph==UINT32_MAX) return true;
        } else if(md_add(d,f->quote_paragraph,SALTS_MD_BLOCK_LINE_BREAK,0u)
                  ==UINT32_MAX) return true;
        (void)md_inline(d,f->quote_paragraph,s+body,end-body,0u);
    }
    f->paragraph=UINT32_MAX;
    return true;
}

static bool md_list_append_item(salts_md_document *d,md_list_stack *stack,
                                md_list_frame *f,
                                const md_list_marker_info *m,
                                const char *s,size_t end,
                                size_t *next_pos,size_t quote_depth) {
    char state=0;
    size_t content=m->content_offset;
    const bool task=md_task_marker(s,content,end,&state,&content);
    f->item=md_add(d,f->list,task ? SALTS_MD_BLOCK_TASK_ITEM :
                                    SALTS_MD_BLOCK_LIST_ITEM,0u);
    if(f->item==UINT32_MAX) return false;
    if(task) {
        const uint32_t checkbox=md_add(d,f->item,SALTS_MD_BLOCK_CHECKBOX,0u);
        if(checkbox==UINT32_MAX) return false;
        md_meta(d,checkbox,&state,1u);
        if(d->error!=SALTS_MD_OK) return false;
    }
    /* A marker-only item is a genuinely empty item. Do not manufacture
     * an empty paragraph or permit an intervening blank to attach a
     * following top-level paragraph to it. */
    f->empty_item=content==end && !task;
    f->paragraph=f->code=f->quote=f->quote_paragraph=UINT32_MAX;
    f->html_kind=0u;f->html_node=UINT32_MAX;
    f->content_column=m->content_column;
    f->code_blank_lines=0u;
    f->fence_open=f->pending_blank=false;
    if(!f->empty_item) {
        /* The excess marker separation belongs to an indented code block,
         * not a manufactured paragraph. Admit the first code line through
         * the same owned frame as later indented-code continuation lines. */
        if(!task &&
           (m->expanded_tab_padding || md_columns(s,content,end)>=4u)) {
            f->code=md_add(d,f->item,SALTS_MD_BLOCK_CODE_INDENTED,0u);
            if(f->code==UINT32_MAX) return false;
            const md_line initial={content,end,end,false};
            if(m->expanded_tab_padding) {
                const size_t physical=md_source_visual_column_at(s,content);
                if(physical>SIZE_MAX-5u) {
                    md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
                }
                return md_code_line_visual(
                    d,f->code,initial,physical,physical+5u);
            }
            return md_code_line(d,f->code,initial,false);
        }
        /* A list marker's CONTENT can be a thematic rule. Admit the typed
         * RULE after literal-code precedence but before nested lists or
         * paragraph text. A whole "- ---" source line is a root HR earlier
         * in md_parse_blocks, and Setext precedence is unchanged. */
        if(!task && md_rule(s,content,end))
            return md_add(d,f->item,SALTS_MD_BLOCK_RULE,0u)!=UINT32_MAX;
        /* CommonMark permits list containers to nest on one source line:
         * "- - foo" and "1. - 2. foo". Reuse the same bounded list-frame
         * admission, with nested marker columns relative to this item. */
        md_list_marker_info nested={0};
        const md_line tail={content,end,end,false};
        if(!task && md_list_token(s,tail,&nested) &&
           !md_rule(s,content,end)) {
            if(nested.column>SIZE_MAX-m->content_column ||
               nested.content_column>SIZE_MAX-m->content_column) {
                md_error(d,SALTS_MD_CAPACITY_EXCEEDED);
                return false;
            }
            nested.column+=m->content_column;
            nested.content_column+=m->content_column;
            return md_list_open(d,stack,f->item,&nested,s,end,
                                next_pos,quote_depth);
        }
        /* A list marker may introduce a heading or fenced block directly.
         * Both retain their list-item parent instead of creating a paragraph. */
        if(!task && md_list_begin_atx(d,f,s,content,end))
            return d->error==SALTS_MD_OK;
        if(!task && md_list_begin_fence(d,f,s,content,end))
            return d->error==SALTS_MD_OK;
        if(!task && md_list_append_quote(d,f,s,content,end))
            return d->error==SALTS_MD_OK;
        /* An item opener shares the canonical seven-type HTML classifier.
         * The enclosing item owns the RAW_HTML_BLOCK; neither renderer
         * policy is changed by this block-admission decision. */
        const unsigned html_kind=task?0u:
            md_raw_html_root_kind(s,content,end,false);
        if(html_kind) {
            const uint32_t html=md_add(
                d,f->item,SALTS_MD_BLOCK_RAW_HTML_BLOCK,0u);
            const bool newline=end<d->raw_size &&
                (s[end]=='\n' || (s[end]=='\r' &&
                 end+1u<d->raw_size && s[end+1u]=='\n'));
            const md_line owned={content,end,end,newline};
            if(html==UINT32_MAX ||
               !md_raw_html_block_line(d,html,owned,true))return false;
            f->html_node=html;
            f->html_kind=html_kind<=5u &&
                md_raw_html_line_ends(html_kind,s,content,end)?
                0u:html_kind;
            return d->error==SALTS_MD_OK;
        }
        /* An item-opener can itself be a GFM table header. Its delimiter
         * belongs to this ITEM only if the next physical line matches this
         * item's content column and (when quoted) the exact quote depth. */
        if(!task && next_pos && content<end) {
            const md_line header={content,end,end,false};
            if(md_table_admit(d,f->item,header,content,next_pos,
                              quote_depth,m->content_column))
                return d->error==SALTS_MD_OK;
        }
        f->paragraph=md_add(d,f->item,SALTS_MD_BLOCK_PARAGRAPH,0u);
        if(f->paragraph==UINT32_MAX) return false;
        if(!md_inline(d,f->paragraph,s+content,end-content,0u)) return false;
    }
    return d->error==SALTS_MD_OK;
}

static bool md_list_open(salts_md_document *d,md_list_stack *stack,
                         uint32_t parent,const md_list_marker_info *m,
                         const char *s,size_t end,size_t *next_pos,
                         size_t quote_depth) {
    if(stack->depth>=MD_LIST_MAX_DEPTH) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    const uint32_t list=md_add(d,parent,m->ordered ?
        SALTS_MD_BLOCK_ORDERED_LIST : SALTS_MD_BLOCK_LIST,0u);
    if(list==UINT32_MAX || !md_list_meta(d,list,m->start,m->marker))
        return false;
    md_list_frame *f=&stack->frames[stack->depth++];
    *f=(md_list_frame){0};
    f->list=list;
    f->marker=m->marker;
    f->ordered=m->ordered;
    f->marker_column=m->column;
    f->list_base_column=m->column;
    return md_list_append_item(d,stack,f,m,s,end,next_pos,quote_depth);
}

/* A blank line first belongs to every still-open candidate container. If a
 * deeper item consumes the next nonblank line, that line proves the blank
 * was internal to the child, not a separator of outer list items. Only the
 * consuming list is loosened; outer list tightness remains unchanged. */
static void md_list_claim_nested_blank(md_list_stack *stack) {
    for(size_t depth=0u;depth+1u<stack->depth;++depth)
        stack->frames[depth].pending_blank=false;
}

/* Defined with the quote-container admission policy below. */
static bool md_quote_lazy_interrupts(const char *s,md_line line,
                                     size_t begin);

static bool md_list_consume(salts_md_document *d,md_list_stack *stack,
                            md_line line,size_t *next_pos,
                            size_t quote_depth) {
    if(!stack->depth) return false;
    const char *s=d->raw;
    if(stack->frames[stack->depth-1u].fence_open &&
       md_blank(s,line.begin,line.end)) {
        md_list_frame *f=&stack->frames[stack->depth-1u];
        md_text(d,f->fence.node,"\n",1u);
        return true;
    }
    if(md_blank(s,line.begin,line.end)) {
        /* A separator can loosen an enclosing list item even when the
         * deepest nested list closes before the next nonblank line. */
        for(size_t depth=0u;depth<stack->depth;++depth)
            stack->frames[depth].pending_blank=true;
        /* Defer interior code blanks until a following indented line proves
         * continuation. EOF or another block discards trailing blanks. */
        md_list_frame *top=&stack->frames[stack->depth-1u];
        if(top->code!=UINT32_MAX) ++top->code_blank_lines;
        if(top->html_kind) {
            if(top->html_kind>5u) {
                top->html_kind=0u;top->html_node=UINT32_MAX;
            } else {
                md_line blank=line;blank.begin=line.end;
                (void)md_raw_html_block_line(d,top->html_node,blank,false);
            }
        }
        return true;
    }
    const size_t indent=md_columns(s,line.begin,md_left(s,line.begin,line.end));
    md_list_marker_info marker={0};
    const bool marker_found=md_list_token(s,line,&marker);
    const size_t line_begin=md_left(s,line.begin,line.end);
    /* An underline indented within an existing item converts the active
     * paragraph to a setext heading BEFORE the thematic-break precedence.
     * An unindented "---" cannot be owned by the item and remains root HR. */
    md_list_frame *active=&stack->frames[stack->depth-1u];
    if(!active->fence_open && !active->pending_blank &&
       active->paragraph!=UINT32_MAX &&
       indent>=active->content_column &&
       indent-active->content_column<=3u) {
        const unsigned level=md_setext(s,line_begin,line.end);
        if(level) {
            d->nodes[active->paragraph].kind=SALTS_MD_BLOCK_HEADING;
            d->nodes[active->paragraph].heading_level=level;
            active->paragraph=UINT32_MAX;
            return true;
        }
    }
    /* A thematic break after an item paragraph belongs to the nearest
     * still-open item whose CONTENT columns match. Only a DEDENTED rule
     * interrupts the entire list and becomes a root-level HR. Check
     * Setext above first, so "---" in an item paragraph stays a heading.
     * Existing md_rule is the sole grammar: no parallel container parser. */
    if(md_rule(s,line_begin,line.end) &&
       !stack->frames[stack->depth-1u].fence_open) {
        for(size_t depth=stack->depth;depth>0u;--depth) {
            md_list_frame *owner=&stack->frames[depth-1u];
            if(owner->fence_open ||
               (owner->empty_item && owner->pending_blank) ||
               indent<owner->content_column ||
               indent-owner->content_column>3u)
                continue;
            stack->depth=depth;
            if(owner->pending_blank) {
                md_mark_list_loose(d,owner->list);
                md_list_claim_nested_blank(stack);
                owner->pending_blank=false;
            }
            const uint32_t rule=md_add(
                d,owner->item,SALTS_MD_BLOCK_RULE,0u);
            if(rule==UINT32_MAX)return true;
            owner->paragraph=owner->code=owner->quote=
                owner->quote_paragraph=UINT32_MAX;
            owner->empty_item=false;
            owner->code_blank_lines=0u;
            return true;
        }
        if(indent<=3u) {
            stack->depth=0u;
            return false;
        }
    }
    while(stack->depth) {
        md_list_frame *f=&stack->frames[stack->depth-1u];
        /* A sibling's marker can shift between 0..3 root columns, while
         * its content indent still determines the nesting boundary. For
         * instance '- a /  - b /   - c' denotes three siblings when the
         * preceding marker is at the previous column. */
        const bool sibling=!f->fence_open && marker_found &&
            marker.column>=f->list_base_column &&
            (marker.column==f->marker_column ||
             (marker.column<f->content_column &&
              marker.column-f->list_base_column<=3u));
        if(sibling) {
            if(f->ordered==marker.ordered && f->marker==marker.marker) {
                if(f->pending_blank) {
                    md_mark_list_loose(d,f->list);
                    md_list_claim_nested_blank(stack);
                }
                f->marker_column=marker.column;
                (void)md_list_append_item(d,stack,f,&marker,s,line.end,
                                          next_pos,quote_depth);
                return true;
            }
            --stack->depth; /* differing marker families never coalesce */
            continue;
        }
        if(f->html_kind && indent>=f->content_column) {
            md_line owned=line;
            owned.begin=md_skip_columns(
                s,line.begin,line.end,f->content_column);
            if(owned.begin==line.begin && f->content_column) {
                md_error(d,SALTS_MD_INVALID_ARGUMENT);return true;
            }
            if(!md_raw_html_block_line(d,f->html_node,owned,false))
                return true;
            const size_t content=md_left(s,owned.begin,line.end);
            if(f->html_kind<=5u &&
               md_raw_html_line_ends(f->html_kind,s,content,line.end)) {
                f->html_kind=0u;f->html_node=UINT32_MAX;
            }
            return true;
        }
        if(f->fence_open) {
            if(indent<f->content_column) {
                --stack->depth;
                continue;
            }
            const size_t body=md_skip_columns(s,line.begin,line.end,
                                               f->content_column);
            const size_t content=md_left(s,body,line.end);
            if(md_fence_close(s,content,line.end,f->fence)) {
                f->fence_open=false;
                return true;
            }
            md_text(d,f->fence.node,s+body,line.end-body);
            if(line.newline) md_text(d,f->fence.node,"\n",1u);
            return true;
        }
        if(f->empty_item && f->pending_blank) {
            --stack->depth;
            continue;
        }
        if(indent>=f->content_column) {
            const size_t body=md_skip_columns(s,line.begin,line.end,
                                              f->content_column);
            if(body==line.begin && f->content_column) {
                md_error(d,SALTS_MD_INVALID_ARGUMENT);return true;
            }
            /* A nonblank continuation makes the previously marker-only item
             * nonempty. Only truly empty items are barred from attaching
             * content after a following blank line. */
            f->empty_item=false;
            /* A nested list is admitted after matching this item, never as
             * a new root list. It must ALSO obey CommonMark paragraph
             * interruption: an ordered marker beginning at 2 (or later)
             * cannot interrupt this item's active paragraph. A pending
             * blank has already ended that paragraph, so it can admit any
             * valid list start. The existing md_list_interrupts_paragraph
             * is the only grammar, not a table-specific workaround.
             * Indentation four columns deeper remains literal code. */
            if(marker_found && marker.column>=f->content_column &&
               marker.column<=f->content_column+3u &&
               (f->paragraph==UINT32_MAX || f->pending_blank ||
                md_list_interrupts_paragraph(&marker,line))) {
                if(f->pending_blank) {
                    md_mark_list_loose(d,f->list);
                    md_list_claim_nested_blank(stack);
                }
                (void)md_list_open(d,stack,f->item,&marker,s,line.end,
                                   next_pos,quote_depth);
                return true;
            }
            const size_t inner_indent=indent-f->content_column;
            if(f->pending_blank && f->code!=UINT32_MAX &&
               inner_indent>=4u) {
                /* Interior blank lines belong to the *same* indented code
                 * node; they are not paragraph/list separators and must not
                 * split one <pre><code> into two blocks. This also works for
                 * quote-owned list frames because no raw prefix is replayed. */
                while(f->code_blank_lines) {
                    md_text(d,f->code,"\n",1u);
                    if(d->error!=SALTS_MD_OK) return true;
                    --f->code_blank_lines;
                }
                f->pending_blank=false;
                md_list_claim_nested_blank(stack);
                (void)md_list_code_line(d,f,line);
                return true;
            }
            if(f->pending_blank) {
                md_mark_list_loose(d,f->list);
                md_list_claim_nested_blank(stack);
                f->paragraph=f->code=f->quote=f->quote_paragraph=UINT32_MAX;
                f->pending_blank=false;
                f->code_blank_lines=0u;
            }
            const size_t inner_begin=md_left(s,body,line.end);
            if(inner_indent<=3u &&
               md_list_begin_atx(d,f,s,inner_begin,line.end))
                return true;
            if(inner_indent<=3u &&
               md_list_begin_fence(d,f,s,inner_begin,line.end))
                return true;
            if(inner_indent>=4u && f->paragraph==UINT32_MAX &&
               f->quote==UINT32_MAX) {
                if(f->code==UINT32_MAX) {
                    f->code=md_add(d,f->item,SALTS_MD_BLOCK_CODE_INDENTED,0u);
                    if(f->code==UINT32_MAX) return true;
                }
                (void)md_list_code_line(d,f,line);
                return true;
            }
            f->code=UINT32_MAX;
            if(md_list_append_quote(d,f,s,body,line.end)) return true;
            f->quote=f->quote_paragraph=UINT32_MAX;
            const size_t text_begin=md_left(s,body,line.end);
            /* A GFM delimiter may turn the PREVIOUS paragraph's final
             * physical line into a TABLE header. A header after an open
             * list paragraph is likewise a separate TABLE block, while
             * already-emitted paragraph text remains with that paragraph.
             * Only the canonical bounded two-line md_table_admit can prove
             * this; a failed lookahead falls back to the SAME paragraph
             * and the existing soft-break semantics without side effects. */
            if(inner_indent<=3u && next_pos &&
               md_table_admit(d,f->item,line,text_begin,
                              next_pos,quote_depth,f->content_column)) {
                f->paragraph=f->code=f->quote=
                    f->quote_paragraph=UINT32_MAX;
                f->code_blank_lines=0u;
                return true;
            }
            if(f->paragraph==UINT32_MAX &&
               md_container_reference_line(d,line,text_begin))return true;
            if(f->paragraph==UINT32_MAX) {
                f->paragraph=md_add(d,f->item,SALTS_MD_BLOCK_PARAGRAPH,0u);
                if(f->paragraph==UINT32_MAX) return true;
            } else (void)md_add(d,f->paragraph,SALTS_MD_BLOCK_LINE_BREAK,0u);
            (void)md_inline(d,f->paragraph,s+text_begin,
                            line.end-text_begin,0u);
            return true;
        }
        /* Lazy paragraph continuation may also belong to the list-owned
         * inner quote (CommonMark #292/#293). Only its already-open text
         * may continue: new block openers, blanks and competing quote markers
         * must not be silently admitted into the borrowed paragraph. */
        const uint32_t continuation=f->quote_paragraph!=UINT32_MAX ?
                                    f->quote_paragraph:f->paragraph;
        if(!f->pending_blank &&
           (!marker_found || marker.column>3u) &&
           continuation!=UINT32_MAX && !md_rule(s,line.begin,line.end) &&
           s[line_begin]!='>' &&
           /* Lazy continuation can never consume a new ATX/fence/rule/list
            * opener, whether its paragraph is a LIST_ITEM or an inner
            * QUOTE. Reuse the existing bounded interruption classifier:
            * an unindented '# Root' after nested list/table closes the
            * list owner and releases true ROOT reference definitions. */
           !md_quote_lazy_interrupts(s,line,line_begin)) {
            if(md_add(d,continuation,SALTS_MD_BLOCK_LINE_BREAK,0u)
               ==UINT32_MAX) return true;
            (void)md_inline(d,continuation,s+line_begin,
                            line.end-line_begin,0u);
            return true;
        }
        --stack->depth;
    }
    return false;
}

/* GFM pipe-table subset. A raw unescaped pipe separates cells even inside
 * backtick spans: block-level table cells precede inline code-span parsing.
 * A backslash-escaped pipe remains cell content and the table preprocessor
 * consumes its escaping backslash BEFORE inline semantics (GFM Example 200).
 * Header/delimiter counts must match; two-line admission stays bounded. */
static bool md_table_unescaped(const char *s, size_t pos, size_t begin) {
    /* GFM table pipes use a raw preceding backslash as the shield even when
     * another backslash precedes it. Do not apply CommonMark inline escape
     * parity before TABLE boundary classification: that happens only after
     * the bounded per-cell raw '\\|' prepass. */
    return pos==begin || s[pos-1u]!='\\';
}

static bool md_table_delimiter_cell(const char *s, size_t begin, size_t end,
                                    uint8_t *out_alignment) {
    begin=md_left(s,begin,end);
    end=md_right(s,begin,end);
    const bool left=begin<end && s[begin]==':';
    if(left)++begin;
    const bool right=end>begin && s[end-1]==':';
    if(right)--end;
    if(begin==end)return false;
    for(size_t i=begin;i<end;++i)
        if(s[i]!='-')return false;
    if(out_alignment) *out_alignment=(uint8_t)(
        left ? (right ? SALTS_MD_TABLE_ALIGN_CENTER : SALTS_MD_TABLE_ALIGN_LEFT)
             : (right ? SALTS_MD_TABLE_ALIGN_RIGHT : SALTS_MD_TABLE_ALIGN_NONE));
    return true;
}

static bool md_table_scan(const char *s, size_t begin, size_t end,
                          bool delimiter, bool require_pipe,
                          size_t *out_columns, uint8_t *alignments,
                          size_t alignment_capacity) {
    if (begin>=end || !out_columns) return false;
    begin=md_left(s,begin,end);
    end=md_right(s,begin,end);
    if (begin>=end) return false;
    bool pipe=false;
    if (s[begin]=='|') { ++begin; pipe=true; }
    if (end>begin && s[end-1]=='|' &&
        md_table_unescaped(s,end-1,begin)) { --end; pipe=true; }
    if (begin>=end) {
        if (pipe && !delimiter) { *out_columns=1u; return true; }
        return false;
    }
    size_t cell_begin=begin, cells=0;
    for (size_t p=begin;p<=end;++p) {
        const bool boundary = p==end ||
            (s[p]=='|' && md_table_unescaped(s,p,begin));
        if (!boundary) continue;
        pipe |= p<end;
        if(delimiter) {
            uint8_t align=SALTS_MD_TABLE_ALIGN_NONE;
            if(!md_table_delimiter_cell(s,cell_begin,p,&align) ||
               (alignments && cells>=alignment_capacity))return false;
            if(alignments)alignments[cells]=align;
        }
        ++cells;
        cell_begin=p+1;
    }
    *out_columns=cells;
    return cells>0 && (!require_pipe || pipe);
}

/* A validated table cell owns only normalized inline bytes. Remove the
 * escaping backslash of GFM '\\|' while COPYING the cell into a bounded
 * temporary buffer: inside a code span the regular CommonMark inline scanner
 * otherwise treats that backslash as literal and would print an extra '\\'.
 * The original document input stays immutable and no scratch is published. */
static bool md_table_cell_inline(salts_md_document *d,uint32_t cell,
                                 const char *s,size_t begin,size_t end) {
    if(end<begin || end-begin==SIZE_MAX) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    const size_t bytes=end-begin;
    char *scratch=(char*)malloc(bytes+1u);
    if(!scratch) {
        md_error(d,SALTS_MD_OUT_OF_MEMORY);return false;
    }
    size_t written=0u;
    for(size_t p=begin;p<end;++p) {
        if(s[p]=='\\' && p+1u<end && s[p+1u]=='|') ++p;
        scratch[written++]=s[p];
    }
    scratch[written]='\0';
    const bool ok=md_inline(d,cell,scratch,written,0u);
    free(scratch);
    return ok;
}

static bool md_table_row(salts_md_document *d, uint32_t table,
                         const char *s, size_t begin, size_t end,
                         size_t expected_columns) {
    const uint32_t row=md_add(d,table,SALTS_MD_BLOCK_TABLE_ROW,0u);
    if (row==UINT32_MAX) return false;
    begin=md_left(s,begin,end);
    end=md_right(s,begin,end);
    if (begin<end && s[begin]=='|') ++begin;
    if (end>begin && s[end-1]=='|' &&
        md_table_unescaped(s,end-1,begin)) --end;
    size_t cell_begin=begin, cells=0;
    for(size_t p=begin;p<=end && d->error==SALTS_MD_OK;++p) {
        const bool boundary=p==end ||
            (s[p]=='|' && md_table_unescaped(s,p,begin));
        if(!boundary) continue;
        if(cells<expected_columns) {
            const uint32_t cell=md_add(d,row,SALTS_MD_BLOCK_TABLE_CELL,0u);
            if(cell==UINT32_MAX) return false;
            const size_t cell_start=md_left(s,cell_begin,p);
            const size_t cell_end=md_right(s,cell_start,p);
            if(!md_table_cell_inline(d,cell,s,cell_start,cell_end))
                return false;
        }
        ++cells;
        cell_begin=p+1;
    }
    while(cells<expected_columns && d->error==SALTS_MD_OK) {
        if(md_add(d,row,SALTS_MD_BLOCK_TABLE_CELL,0u)==UINT32_MAX)
            return false;
        ++cells;
    }
    return d->error==SALTS_MD_OK;
}

/* A table ends before a new block-level structure. Ordinary text without
 * a pipe still supplies a one-column data row (padded to header width). */
static bool md_table_starts_new_block(const char *s, size_t b, size_t e) {
    if (b>=e || s[b]=='>') return true;
    /* #186: an actual block-level HTML opener terminates a GFM table;
     * otherwise it would incorrectly become a DATA cell. Reuse the SAME
     * CommonMark type-1..7 admission classifier as the surrounding block
     * parser, never a private HTML vocabulary or a renderer-side guess. */
    if (md_raw_html_root_kind(s,b,e,false)) return true;
    if (md_rule(s,b,e)) return true;
    size_t hb=b,he=e;
    if (md_atx(s,&hb,&he)) return true;
    char marker=0; size_t count=0,info_begin=0,info_end=0;
    if (md_fence_start(s,b,e,&marker,&count,&info_begin,&info_end)) return true;
    bool ordered=false;size_t content=0;
    if (md_list_marker(s,b,e,&ordered,&content)) return true;
    return false;
}

/* Lazy quote continuation is admitted only into an already open paragraph.
 * A new block opener ends that admission; indented candidate markers are
 * ordinary continuation text, not a new root list. */
static bool md_quote_lazy_interrupts(const char *s,md_line line,
                                     size_t begin) {
    /* Four-space indentation after an open quote paragraph is literal
     * lazy text, not a root heading/rule/fence/list admission. */
    if(md_columns(s,line.begin,begin)>3u) return false;
    if(md_rule(s,begin,line.end)) return true;
    size_t hbegin=begin,hend=line.end;
    if(md_atx(s,&hbegin,&hend)) return true;
    char mark=0;size_t count=0,info_begin=0,info_end=0;
    if(md_fence_start(s,begin,line.end,
                      &mark,&count,&info_begin,&info_end)) return true;
    md_list_marker_info marker={0};
    if(md_list_token(s,line,&marker) && marker.column<=3u &&
       md_list_interrupts_paragraph(&marker,line)) return true;
    return false;
}


enum { MD_QUOTE_MAX_DEPTH=32 };
typedef struct md_quote_stack {
    uint32_t nodes[MD_QUOTE_MAX_DEPTH];
    size_t depth;
    uint32_t paragraph, code;
    md_fence fence;
    md_list_stack lists; /* bounded list frames owned by the active quote */
    unsigned html_kind;
    uint32_t html_node;
    bool lazy, fence_open;
} md_quote_stack;

/* Match all explicit quote markers using CommonMark's <=3-column leading
 * indent per layer and consume at most one optional whitespace character
 * after each '>'. No unbounded recursion or separate Markdown frontend. */
static size_t md_quote_prefix(const char *s,md_line line,size_t *body) {
    size_t pos=line.begin,depth=0u;
    while(pos<line.end) {
        size_t next=pos,column=0u;
        while(next<line.end && md_space(s[next]) && column<=3u) {
            const size_t step=s[next]=='\t' ? 4u-column%4u : 1u;
            column+=step;
            if(column>3u) break;
            ++next;
        }
        if(column>3u || next>=line.end || s[next]!='>') break;
        ++depth;
        pos=next+1u;
        if(pos<line.end && md_space(s[pos])) ++pos;
        if(depth>MD_QUOTE_MAX_DEPTH) break;
    }
    *body=pos;
    return depth;
}

/* Reuse the SAME bounded table scanner, cell normalization and typed owner
 * for root tables and tables in explicitly marked quote containers. A quoted
 * delimiter/row must carry the exact active quote depth; never borrow a
 * lazy/unmarked line, a nested list, or a competing container as table data.
 * This routine advances next_pos only after a complete header+delimiter
 * admission and never persists a second parser or an unowned scratch view. */
static bool md_table_admit(salts_md_document *d,uint32_t parent,
                           md_line header,size_t header_begin,
                           size_t *next_pos,size_t quote_depth,
                           size_t item_column) {
    if(!next_pos || *next_pos>=d->raw_size)return false;
    const char *s=d->raw;
    const md_line delimiter=md_read_line(d,*next_pos);
    size_t delimiter_body=delimiter.begin;
    if(quote_depth &&
       md_quote_prefix(s,delimiter,&delimiter_body)!=quote_depth)
        return false;
    const size_t sep_begin=md_left(s,delimiter_body,delimiter.end);
    const size_t sep_indent=md_columns(s,delimiter_body,sep_begin);
    if(item_column) {
        if(sep_indent<item_column || sep_indent-item_column>3u)
            return false;
    } else if(sep_indent>3u) {
        /* Root and direct-QUOTE tables obey CommonMark's 0..3 visual
         * indentation limit for a delimiter. Four spaces (or a TAB at
         * column zero) are code indentation, not TABLE admission.
         * LIST_ITEM tables intentionally use their own content column. */
        return false;
    }
    size_t columns=0u,delimiter_columns=0u;
    if(!md_table_scan(s,header_begin,header.end,false,true,
                      &columns,NULL,0u) ||
       !md_table_scan(s,sep_begin,delimiter.end,true,true,
                      &delimiter_columns,NULL,0u) ||
       columns!=delimiter_columns)
        return false;
    if(columns>d->node_limit) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return true;
    }
    uint8_t *alignments=(uint8_t*)malloc(columns);
    if(!alignments) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return true;}
    size_t verified=0u;
    if(!md_table_scan(s,sep_begin,delimiter.end,true,true,
                      &verified,alignments,columns) ||
       verified!=columns) {
        free(alignments);
        md_error(d,SALTS_MD_INVALID_ARGUMENT);return true;
    }
    const uint32_t table=md_add(d,parent,SALTS_MD_BLOCK_TABLE,0u);
    if(table==UINT32_MAX) {free(alignments);return true;}
    md_meta(d,table,(const char*)alignments,columns);
    free(alignments);
    if(d->error!=SALTS_MD_OK ||
       !md_table_row(d,table,s,header_begin,header.end,columns))
        return true;
    *next_pos=delimiter.next;
    while(*next_pos<d->raw_size && d->error==SALTS_MD_OK) {
        const md_line row=md_read_line(d,*next_pos);
        size_t row_body=row.begin;
        if(quote_depth &&
           md_quote_prefix(s,row,&row_body)!=quote_depth)break;
        const size_t row_begin=md_left(s,row_body,row.end);
        const size_t row_indent=md_columns(s,row_body,row_begin);
        size_t row_columns=0u;
        if((item_column ?
            (row_indent<item_column ||
             row_indent-item_column>3u) :
            (row_indent>3u)) ||
           md_blank(s,row_body,row.end) ||
           md_table_starts_new_block(s,row_begin,row.end) ||
           !md_table_scan(s,row_begin,row.end,false,false,
                          &row_columns,NULL,0u))
            break;
        if(!md_table_row(d,table,s,row_begin,row.end,columns))
            return true;
        *next_pos=row.next;
    }
    return true;
}

/* The same raw header may look like a root TABLE and a LIST opener.
 * Give it to the LIST only when the delimiter is indented into that
 * marker's actual content columns AND the sliced item header matches the
 * same delimiter column count. An unindented/dedented delimiter remains
 * root TABLE-owned. This single check is reused during ROOT reference
 * preflight; it is not a second parser or an HTML-only inference. */
static bool md_table_header_owned_by_list(salts_md_document *d,
                                          md_line header,
                                          size_t next_pos) {
    if(next_pos>=d->raw_size)return false;
    const char *s=d->raw;
    md_list_marker_info marker={0};
    if(!md_list_token(s,header,&marker) ||
       marker.content_offset>=header.end)
        return false;
    const md_line separator=md_read_line(d,next_pos);
    const size_t delimiter_begin=md_left(s,separator.begin,separator.end);
    const size_t indent=md_columns(s,separator.begin,delimiter_begin);
    if(indent<marker.content_column ||
       indent-marker.content_column>3u)
        return false;
    size_t header_columns=0u,delimiter_columns=0u;
    return md_table_scan(s,marker.content_offset,header.end,false,true,
                         &header_columns,NULL,0u) &&
           md_table_scan(s,delimiter_begin,separator.end,true,true,
                         &delimiter_columns,NULL,0u) &&
           header_columns==delimiter_columns;
}

/* After a quote marker consumes its optional ONE visual whitespace
 * column, retain the remaining TAB columns as owned code indentation. */
static bool md_quote_code_line(salts_md_document *d,uint32_t code,
                               md_line line,size_t body) {
    const char *s=d->raw;
    const size_t logical=md_logical_content_column_at(s,body);
    if(logical>SIZE_MAX-4u) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    const size_t start=body>line.begin && md_space(s[body-1u]) ?
                       body-1u : body;
    md_line inner=line;
    inner.begin=start;
    return md_code_line_visual(
        d,code,inner,md_source_visual_column_at(s,start),logical+4u);
}

/* One explicitly owned stack of quote containers. All admitted blocks keep
 * their true parent in the pre-order RichDoc, with one document lifetime. */
static bool md_quote_line(salts_md_document *d,md_quote_stack *q,
                          md_line line,size_t level,size_t body,
                          size_t *next_pos) {
    if(level>MD_QUOTE_MAX_DEPTH) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);return false;
    }
    if(level!=q->depth) {
        if(level<q->depth) q->depth=level;
        else {
            while(q->depth<level) {
                const uint32_t parent=q->depth ? q->nodes[q->depth-1u] : 0u;
                const uint32_t node=md_add(d,parent,SALTS_MD_BLOCK_QUOTE,0u);
                if(node==UINT32_MAX) return false;
                q->nodes[q->depth++]=node;
            }
        }
        q->paragraph=q->code=UINT32_MAX;
        q->lists.depth=0u; /* no list state across quote generation/depth */
        q->fence_open=q->lazy=false;
        q->html_kind=0u;q->html_node=UINT32_MAX;
    }
    const uint32_t parent=q->nodes[level-1u];
    const char *s=d->raw;
    const size_t begin=md_left(s,body,line.end);
    const size_t logical_quote=md_logical_content_column_at(s,body);
    const size_t physical_content=md_source_visual_column_at(s,begin);
    const size_t indent=physical_content>=logical_quote ?
                        physical_content-logical_quote : 0u;
    if(q->html_kind) {
        if(q->html_kind>5u && md_blank(s,body,line.end)) {
            q->html_kind=0u;q->html_node=UINT32_MAX;
            q->paragraph=q->code=UINT32_MAX;q->lazy=false;
            return true; /* type-6/7 terminates before the blank */
        }
        md_line owned=line;owned.begin=body;
        if(!md_raw_html_block_line(d,q->html_node,owned,false))
            return false;
        if(q->html_kind<=5u &&
           md_raw_html_line_ends(q->html_kind,s,begin,line.end)) {
            q->html_kind=0u;q->html_node=UINT32_MAX;
        }
        return d->error==SALTS_MD_OK;
    }
    if(q->fence_open) {
        if(indent<=3u && md_fence_close(s,begin,line.end,q->fence)) {
            q->fence_open=false;
            return true;
        }
        /* Quoted fence payload excludes quote markers, never the raw UTF-8
         * bytes of the owned source. */
        md_text(d,q->fence.node,s+body,line.end-body);
        if(line.newline) md_text(d,q->fence.node,"\n",1u);
        return d->error==SALTS_MD_OK;
    }
    md_line inner=line;
    inner.begin=body; /* all list columns relative to the quote content */
    if(md_blank(s,body,line.end)) {
        if(q->lists.depth) (void)md_list_consume(d,&q->lists,inner,
                                                   next_pos,level);
        q->paragraph=q->code=UINT32_MAX;
        q->lazy=false;
        return d->error==SALTS_MD_OK;
    }
    if(q->lists.depth && md_list_consume(d,&q->lists,inner,
                                            next_pos,level))
        return d->error==SALTS_MD_OK;
    if(d->error!=SALTS_MD_OK)return false;
    if(q->code!=UINT32_MAX) {
        if(indent>=4u)
            return md_quote_code_line(d,q->code,line,body);
        q->code=UINT32_MAX;
    }
    if(q->paragraph!=UINT32_MAX && indent<=3u) {
        const unsigned setext=md_setext(s,begin,line.end);
        if(setext) {
            d->nodes[q->paragraph].kind=SALTS_MD_BLOCK_HEADING;
            d->nodes[q->paragraph].heading_level=setext;
            q->paragraph=UINT32_MAX;q->lazy=false;
            return true;
        }
    }
    if(indent<=3u && md_rule(s,begin,line.end)) {
        if(md_add(d,parent,SALTS_MD_BLOCK_RULE,0u)==UINT32_MAX)return false;
        q->paragraph=UINT32_MAX;q->lazy=false;return true;
    }
    char marker=0u;size_t run=0u,info_begin=0u,info_end=0u;
    if(indent<=3u && md_fence_start(s,begin,line.end,
                                    &marker,&run,&info_begin,&info_end)) {
        const uint32_t node=md_add(d,parent,SALTS_MD_BLOCK_CODE_FENCE,0u);
        if(node==UINT32_MAX)return false;
        md_fence_info_meta(d,node,s+info_begin,info_end-info_begin);
        if(d->error!=SALTS_MD_OK)return false;
        q->fence=(md_fence){marker,run,node};
        q->fence_open=true;
        q->paragraph=UINT32_MAX;q->lazy=false;
        return true;
    }
    if(indent<=3u) {
        size_t hbegin=begin,hend=line.end;
        const unsigned heading=md_atx(s,&hbegin,&hend);
        if(heading) {
            const uint32_t node=md_add(d,parent,SALTS_MD_BLOCK_HEADING,heading);
            if(node==UINT32_MAX)return false;
            q->paragraph=UINT32_MAX;q->lazy=false;
            return md_inline(d,node,s+hbegin,hend-hbegin,0u);
        }
    }
    /* Tables belong to the innermost explicit quote owner. List-owned
     * table admission is a separate slice and is deliberately not inferred
     * from a lazy paragraph continuation or an unmatched quote delimiter. */
    if(indent<=3u && q->paragraph==UINT32_MAX &&
       q->lists.depth==0u &&
       md_table_admit(d,parent,line,begin,next_pos,level,0u)) {
        q->paragraph=q->code=UINT32_MAX;
        q->lazy=false;
        return d->error==SALTS_MD_OK;
    }
    /* An admitted quote list belongs to this quote node, not the document.
     * A numbered marker >1 never interrupts an existing paragraph. */
    md_list_marker_info item={0};
    if(indent<=3u && md_list_token(s,inner,&item) &&
       (q->paragraph==UINT32_MAX ||
        md_list_interrupts_paragraph(&item,inner)) &&
       !md_rule(s,begin,line.end)) {
        if(!md_list_open(d,&q->lists,parent,&item,s,line.end,
                         next_pos,level))
            return false;
        q->paragraph=q->code=UINT32_MAX;
        q->lazy=false;
        return true;
    }
    if(indent>=4u && q->paragraph==UINT32_MAX) {
        q->code=md_add(d,parent,SALTS_MD_BLOCK_CODE_INDENTED,0u);
        if(q->code==UINT32_MAX)return false;
        q->lazy=false;
        return md_quote_code_line(d,q->code,line,body);
    }
    if(indent<=3u && q->lists.depth==0u) {
        const unsigned kind=md_raw_html_root_kind(
            s,begin,line.end,q->paragraph!=UINT32_MAX);
        if(kind) {
            const uint32_t node=md_add(
                d,parent,SALTS_MD_BLOCK_RAW_HTML_BLOCK,0u);
            md_line owned=line;owned.begin=body;
            if(node==UINT32_MAX ||
               !md_raw_html_block_line(d,node,owned,true))return false;
            q->html_node=node;
            q->html_kind=kind<=5u &&
                md_raw_html_line_ends(kind,s,begin,line.end)?0u:kind;
            q->paragraph=q->code=UINT32_MAX;
            q->lazy=false;
            return true;
        }
    }
    if(q->paragraph==UINT32_MAX && indent<=3u &&
       md_container_reference_line(d,line,begin)) {
        q->lazy=false;
        return d->error==SALTS_MD_OK;
    }
    if(q->paragraph==UINT32_MAX) {
        q->paragraph=md_add(d,parent,SALTS_MD_BLOCK_PARAGRAPH,0u);
        if(q->paragraph==UINT32_MAX)return false;
    } else if(md_add(d,q->paragraph,SALTS_MD_BLOCK_LINE_BREAK,0u)
              ==UINT32_MAX) return false;
    q->lazy=true;
    return md_inline(d,q->paragraph,s+begin,line.end-begin,0u);
}

/* Bounded multiline code-span admission within ROOT paragraphs only.
 * The legacy C11 per-line inline scan remains canonical for all other
 * constructs. Read ahead for the SAME exact-length closing backtick run,
 * never crossing a blank line or a block that interrupts this paragraph.
 * At most 64 continuation lines are examined per opener to keep repeated
 * unmatched candidates from causing unbounded quadratic rescanning.
 * Nested quote/list paragraph admission is a separate #182 slice. */
static bool md_root_code_span_interrupts(salts_md_document *d,md_line line) {
    const char *s=d->raw;
    const size_t begin=md_left(s,line.begin,line.end);
    if(begin==line.end) return true;
    const size_t indent=md_columns(s,line.begin,begin);
    if(indent>3u) return false; /* still part of an open paragraph */
    if(md_rule(s,begin,line.end) || md_setext(s,begin,line.end)) return true;
    size_t hb=begin,he=line.end;
    if(md_atx(s,&hb,&he)) return true;
    char fence=0;size_t count=0,info_begin=0,info_end=0;
    if(md_fence_start(s,begin,line.end,&fence,&count,
                      &info_begin,&info_end)) return true;
    size_t quote_body=line.begin;
    if(md_quote_prefix(s,line,&quote_body)) return true;
    md_list_marker_info item={0};
    return md_list_token(s,line,&item) &&
           md_list_interrupts_paragraph(&item,line);
}

static bool md_root_multiline_code_span(salts_md_document *d,
                                        uint32_t *paragraph,md_line first,
                                        size_t begin,size_t *next_pos) {
    const char *s=d->raw;
    if(begin>=first.end || s[begin]!=(char)0x60) return false;
    size_t run=1u;
    while(begin+run<first.end && s[begin+run]==(char)0x60) ++run;
    /* A same-line match already belongs to the normal inline scanner. */
    if(md_code_span_close(s,first.end,begin+run,run)!=SIZE_MAX)
        return false;
    size_t pos=first.next;
    for(size_t lines=0u;pos<d->raw_size && lines<64u;++lines) {
        const md_line line=md_read_line(d,pos);
        if(md_root_code_span_interrupts(d,line)) break;
        const size_t close=md_code_span_close(s,line.end,line.begin,run);
        if(close!=SIZE_MAX) {
            if(*paragraph==UINT32_MAX) {
                *paragraph=md_add(d,0u,SALTS_MD_BLOCK_PARAGRAPH,0u);
                if(*paragraph==UINT32_MAX) return true;
            } else if(md_add(d,*paragraph,SALTS_MD_BLOCK_LINE_BREAK,0u)
                      ==UINT32_MAX) return true;
            const uint32_t code=md_add(d,*paragraph,SALTS_MD_BLOCK_CODE_SPAN,0u);
            if(code==UINT32_MAX) return true;
            md_code_span_text(d,code,s,begin+run,close);
            if(d->error!=SALTS_MD_OK) return true;
            if(close+run<line.end)
                (void)md_inline(d,*paragraph,s+close+run,
                                line.end-close-run,0u);
            *next_pos=line.next;
            return true;
        }
        pos=line.next;
    }
    return false;
}

/* Root-paragraph inline ownership spans physical lines when * / _ runs
 * pair across a SOFT line break. Collect only an unindented, uninterrupted,
 * bounded root paragraph prefix; no quote/list/container, Setext, code,
 * hard-break admission, or competing block may be crossed here.
 *
 * The original raw bytes remain owned unchanged by d->raw. This scratch
 * UTF-8 buffer normalizes LF/CRLF solely for semantic inline processing.
 * The same md_inline/md_emph_pairs code produces the typed RichDoc; there
 * is no second Markdown grammar and no delayed pointer into scratch.
 *
 * Delimiters may be parsed twice in this test-and-admit path. Both passes
 * remain subject to the identical work cap and the 32-deep owner ceiling;
 * this is bounded by the full original raw-byte input budget. */
static bool md_root_paragraph_emphasis(salts_md_document *d,
                                        uint32_t *paragraph,
                                        md_line first,size_t begin,
                                        size_t *next_pos) {
    enum { MD_PARAGRAPH_LOOKAHEAD_LINES=32 };
    const char *s=d->raw;
    if(*paragraph!=UINT32_MAX || begin>=first.end ||
       memchr(s+begin,(char)0x60,first.end-begin)!=NULL)
        return false;
    /* #185 supplies explicit hard/soft origin even when the marker pair
     * crosses spaces/backslash; keep the complete physical line in scratch. */
    md_line lines[MD_PARAGRAPH_LOOKAHEAD_LINES]={0};
    size_t begins[MD_PARAGRAPH_LOOKAHEAD_LINES]={0};
    lines[0]=first;begins[0]=begin;
    size_t count=1u,bytes=first.end-begin;
    for(size_t cursor=first.next;
        cursor<d->raw_size && count<MD_PARAGRAPH_LOOKAHEAD_LINES;) {
        const md_line next=md_read_line(d,cursor);
        const size_t at=md_left(s,next.begin,next.end);
        if(at!=next.begin || at==next.end ||
           md_root_code_span_interrupts(d,next) ||
           memchr(s+at,(char)0x60,next.end-at)!=NULL)
            break;
        if(bytes>=d->raw_size || next.end-at>d->raw_size-bytes-1u)
            break;
        bytes+=1u+next.end-at;
        lines[count]=next;begins[count]=at;
        ++count;cursor=next.next;
    }
    if(count<2u)return false;
    /* No trailing source whitespace is semantic text on the final physical
     * line, including before a Setext underline (#82). Intermediate lines
     * retain their suffix so LF admission can classify HARD vs SOFT. */
    const size_t last_end=md_right(s,begins[count-1u],lines[count-1u].end);
    const size_t removed=lines[count-1u].end-last_end;
    if(removed>bytes)return false;
    bytes-=removed;
    if(!bytes || bytes>=SIZE_MAX/sizeof(md_emph_event))return false;
    char *joined=(char*)malloc(bytes+1u);
    md_emph_event *events=(md_emph_event*)calloc(bytes,sizeof(*events));
    if(!joined || !events) {
        free(joined);free(events);
        md_error(d,SALTS_MD_OUT_OF_MEMORY);
        return false;
    }
    size_t used=0u;
    for(size_t i=0u;i<count;++i) {
        if(i) joined[used++]='\n';
        const size_t end=(i+1u==count)?last_end:lines[i].end;
        const size_t len=end-begins[i];
        if(len) memcpy(joined+used,s+begins[i],len);
        used+=len;
    }
    joined[used]='\0';
    if(used!=bytes || !md_emph_pairs(d,joined,bytes,events)) {
        if(d->error==SALTS_MD_OK)md_error(d,SALTS_MD_INVALID_ARGUMENT);
        free(events);free(joined);return false;
    }
    /* Check actual matched pair ancestry across at least one LF. Merely
     * seeing markers on both lines is not sufficient admission. */
    size_t openings[33]={0u},last_lf=SIZE_MAX;
    unsigned nested=0u;
    bool across=false;
    for(size_t p=0u;p<bytes;++p) {
        if(joined[p]=='\n')last_lf=p;
        if(events[p].close) {
            if(nested && last_lf!=SIZE_MAX &&
               openings[nested-1u]<last_lf)
                across=true;
            if(nested)--nested;
        }
        if(events[p].open) {
            if(nested>=32u)break; /* normal per-line bounded parser */
            openings[nested++]=p;
        }
    }
    free(events);
    if(!across) {free(joined);return false;}
    const uint32_t node=md_add(d,0u,SALTS_MD_BLOCK_PARAGRAPH,0u);
    if(node==UINT32_MAX) {free(joined);return false;}
    *paragraph=node;
    const bool ok=md_inline(d,node,joined,bytes,0u);
    free(joined);
    if(!ok)return true; /* parse error is already recorded; no publication */
    *next_pos=lines[count-1u].next;
    return true;
}

/* #185: root paragraph line boundaries must decide whitespace and break
 * origin BEFORE publishing decoded RichDoc text. A backward trim of the
 * shared text arena would invalidate borrowed byte spans and node owners.
 * Do not cross competing blocks, list/quote generations or blank lines. */
static bool md_root_paragraph_continues(salts_md_document *d,md_line current) {
    if(!current.newline || current.next>=d->raw_size)return false;
    const md_line next=md_read_line(d,current.next);
    const char *s=d->raw;
    if(md_blank(s,next.begin,next.end))return false;
    /* An indented continuation is still a paragraph continuation when its
     * owner already exists. This preserves the existing paragraph precedence. */
    return !md_root_code_span_interrupts(d,next);
}

/* Admit exactly a complete inline LINK whose destination ends on this
 * ROOT paragraph's first physical line and whose quoted TITLE begins on
 * the immediately adjacent second line (CommonMark #510). The same
 * md_link_text_close/md_link_target_parse and md_inline grammar is used for
 * recognition and publication; scratch is copied into owned RichDoc spans,
 * never borrowed past this call. No list/quote/fence/blank crossing.
 *
 * A single CRLF is normalized to a semantic LF. The second line's actual
 * hard/soft continuation origin remains owned by md_root_line_content.
 * Other multiline inline grammars remain separately scoped. */
static bool md_root_multiline_inline_title(salts_md_document *d,
                                           uint32_t *paragraph,
                                           md_line first,size_t begin,
                                           size_t *next_pos,
                                           md_root_break *next_break) {
    const char *s=d->raw;
    if(*paragraph!=UINT32_MAX || begin>=first.end || s[begin]!='[' ||
       !first.newline || first.next>=d->raw_size)return false;
    const size_t square_first=md_link_text_close(s,first.end,begin+1u);
    if(square_first==SIZE_MAX || square_first+1u>=first.end ||
       s[square_first+1u]!='(')return false;
    const md_line second=md_read_line(d,first.next);
    const size_t second_at=md_left(s,second.begin,second.end);
    if(second_at>=second.end ||
       md_columns(s,second.begin,second_at)>3u ||
       md_root_code_span_interrupts(d,second))
        return false;
    const size_t first_bytes=first.end-begin;
    const size_t second_end=md_right(s,second.begin,second.end);
    const size_t second_bytes=second_end-second.begin;
    if(first_bytes>=d->raw_size ||
       second_bytes>d->raw_size-first_bytes-1u)return false;
    const size_t bytes=first_bytes+1u+second_bytes;
    char *joined=(char*)malloc(bytes+1u);
    if(!joined) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return true;}
    memcpy(joined,s+begin,first_bytes);
    joined[first_bytes]='\n';
    memcpy(joined+first_bytes+1u,s+second.begin,second_bytes);
    joined[bytes]='\0';
    const size_t square=md_link_text_close(joined,bytes,1u);
    size_t round=SIZE_MAX;
    md_link_parts parts={0};
    const bool admitted=square!=SIZE_MAX && square<first_bytes &&
        square+1u<first_bytes && joined[square+1u]=='(' &&
        md_link_target_parse(joined,bytes,square+1u,&parts,&round) &&
        round>first_bytes && parts.has_title &&
        parts.title_begin>first_bytes &&
        !md_link_visible_has_active_link(d,joined,1u,square);
    if(!admitted) {free(joined);return false;}
    const uint32_t node=md_add(d,0u,SALTS_MD_BLOCK_PARAGRAPH,0u);
    if(node==UINT32_MAX) {free(joined);return true;}
    *paragraph=node;
    const bool ok=md_inline(d,node,joined,bytes,0u);
    free(joined);
    if(!ok)return true; /* d->error prevents partial publication */
    size_t ignored=0u;
    *next_break=md_root_line_content(s,second.begin,second.end,
        md_root_paragraph_continues(d,second),&ignored);
    *next_pos=second.next;
    return true;
}

/* Admit one bounded two-line ROOT inline HTML token by the SAME
 * md_inline_html_tag_close / md_inline grammar. The scanner owns the LF
 * INSIDE the typed RAW_HTML_INLINE node, so neither soft nor hard breaks
 * are manufactured from attribute/comment source. Only a token closing
 * at the next physical line's end (or with one trailing literal ')' after
 * an invalid multiline LINK destination) is admitted. Other cases remain
 * with canonical per-line parsing; this is not a second HTML parser
 * or any change to the default safe renderer.
 *
 * Input is copied into transient LF-normalized scratch; published nodes own
 * copied semantic bytes. Never borrow scratch or cross blank/container/block
 * boundaries. The two-line limit bounds incomplete-candidate work. */
static bool md_root_multiline_raw_html(salts_md_document *d,
                                       uint32_t *paragraph,md_line first,
                                       size_t begin,size_t *next_pos,
                                       md_root_break *next_break) {
    const char *s=d->raw;
    if(*paragraph!=UINT32_MAX || begin>=first.end ||
       !first.newline || first.next>=d->raw_size)return false;
    const md_line second=md_read_line(d,first.next);
    if(md_blank(s,second.begin,second.end) ||
       md_root_code_span_interrupts(d,second))return false;
    const size_t first_bytes=first.end-begin;
    const size_t second_bytes=second.end-second.begin;
    if(first_bytes>=d->raw_size ||
       second_bytes>d->raw_size-first_bytes-1u)return false;
    const size_t bytes=first_bytes+1u+second_bytes;
    char *joined=(char*)malloc(bytes+1u);
    if(!joined) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return true;}
    memcpy(joined,s+begin,first_bytes);
    joined[first_bytes]='\n';
    memcpy(joined+first_bytes+1u,s+second.begin,second_bytes);
    joined[bytes]='\0';
    bool admitted=false;
    /* Fixed lexical probe ceiling prevents quadratic rescans for a source
     * consisting mostly of repeated malformed '<' prefixes. */
    size_t html_probes=0u;
    for(size_t p=0u;p<first_bytes;++p) {
        if(joined[p]=='\\' && p+1u<first_bytes) {++p;continue;}
        if(joined[p]=='\x60') {
            size_t end=p+1u;
            while(end<first_bytes && joined[end]=='\x60')++end;
            const size_t close=md_code_span_close(
                joined,first_bytes,end,end-p);
            if(close!=SIZE_MAX) {p=close+(end-p)-1u;continue;}
        }
        if(joined[p]!='<')continue;
        if(++html_probes>32u)break;
        size_t autolink=SIZE_MAX;
        if(md_uri_autolink(joined,bytes,p,&autolink) ||
           md_email_autolink(joined,bytes,p,&autolink)) {
            if(autolink<first_bytes)p=autolink;
            continue;
        }
        const size_t close=md_inline_html_tag_close(joined,bytes,p);
        if(close!=SIZE_MAX && close>=first_bytes &&
           (close+1u==bytes ||
            (close+2u==bytes && joined[close+1u]==')'))) {
            admitted=true;break;
        }
        if(close!=SIZE_MAX && close<first_bytes)p=close;
    }
    if(!admitted) {free(joined);return false;}
    const uint32_t node=md_add(d,0u,SALTS_MD_BLOCK_PARAGRAPH,0u);
    if(node==UINT32_MAX) {free(joined);return true;}
    *paragraph=node;
    const bool ok=md_inline(d,node,joined,bytes,0u);
    free(joined);
    if(!ok)return true; /* failure discards all unpublished RichDoc nodes */
    *next_pos=second.next;
    *next_break=MD_ROOT_BREAK_SOFT;
    return true;
}

/* Collect bounded ASCII root reference definitions. The scan is
 * BEFORE inline admission, so a later [ref]: ... can bind an earlier
 * [text][ref]. Definitions inside code fences, indented code, lists or
 * quotes are NOT recognized by this restricted root-only slice. This
 * prepass has no AST side effects and cannot reinterpret decoded entities
 * as syntax. Unicode full case folding and labels spanning more than two LFs remain separate.
 *
 * Definition entries also record exact source-line offsets to suppress
 * their block output; duplicates are consumed but lookup selects the FIRST.
 * Fixed 128-entry budget prevents unbounded hash/state growth. */
static bool md_reference_definition_line(
    const salts_md_document *d,size_t begin) {
    size_t low=0u,high=d->reference_count;
    while(low<high) {
        const size_t mid=low+(high-low)/2u;
        if(d->references[mid].source_last_line<begin)low=mid+1u;
        else high=mid;
    }
    return low<d->reference_count &&
           d->references[low].source_line<=begin &&
           begin<=d->references[low].source_last_line;
}

/* Accumulate only a candidate title, not an arbitrary paragraph or second
 * grammar. The shared scanner distinguishes unclosed from invalid syntax.
 * Normalize CRLF to LF and remove structural continuation indentation;
 * retain all title payload/trailing spaces and raw document bytes.
 * Scratch is bounded by source bytes; at most 64 physical lines are read. */
static bool md_reference_collect_title(salts_md_document *d,md_line first,
                                       size_t begin,md_line *last,
                                       char **title,size_t *title_bytes) {
    const char *s=d->raw;
    size_t used=first.end-begin,capacity=used;
    char *scratch=(char*)malloc(capacity+1u);
    if(!scratch) {md_error(d,SALTS_MD_OUT_OF_MEMORY);return false;}
    memcpy(scratch,s+begin,used);
    md_line current=first;
    for(size_t lines=1u;;++lines) {
        size_t at=0u;md_link_parts parts={0};bool incomplete=false;
        if(md_link_title_scan(scratch,used,&at,&parts,true,&incomplete)) {
            if(!md_blank(scratch,at,used))break;
            const bool ok=md_decode_component(d,scratch+parts.title_begin,
                parts.title_end-parts.title_begin,title,title_bytes);
            if(ok)*last=current;
            free(scratch);return ok;
        }
        if(!incomplete || !current.newline || current.next>=d->raw_size)
            break;
        const md_line next=md_read_line(d,current.next);
        if(md_root_code_span_interrupts(d,next))break;
        if(lines>=MD_REF_TITLE_LINES_MAX) {
            md_error(d,SALTS_MD_CAPACITY_EXCEEDED);break;
        }
        const size_t content=md_left(s,next.begin,next.end);
        const size_t extra=next.end-content;
        if(used>=d->raw_size || extra>d->raw_size-used-1u) {
            md_error(d,SALTS_MD_CAPACITY_EXCEEDED);break;
        }
        const size_t required=used+1u+extra;
        if(required>capacity) {
            size_t grown=capacity;
            if(grown<d->raw_size/2u)grown=grown*2u+1u;
            else grown=d->raw_size;
            if(grown<required)grown=required;
            char *data=(char*)realloc(scratch,grown+1u);
            if(!data) {md_error(d,SALTS_MD_OUT_OF_MEMORY);break;}
            scratch=data;capacity=grown;
        }
        scratch[used++]='\n';
        memcpy(scratch+used,s+content,extra);used+=extra;
        current=next;
    }
    free(scratch);return false;
}

/* Admit a ROOT bounded UTF-8 link-reference label across at most two physical LFs.
 * The block prepass alone consumes this bounded source interval; inline
 * references keep using md_reference_key on their literal single-line label.
 * Original bytes (including CRLF and indentation) must fit 128 raw bytes.
 * Continue to borrow only the immutable source; no secondary table, Unicode
 * fold substitute, or nested-container definition grammar (#208 / #183). */
static bool md_reference_definition_label(salts_md_document *d,
                                         md_line first,size_t begin,
                                         md_line *target_line,
                                         size_t *after_colon,
                                         char key[MD_REF_FOLD_MAX+1u]) {
    const char *s=d->raw;
    const size_t close=md_reference_label_close(s,first.end,begin+1u);
    if(close!=SIZE_MAX) {
        if(close+1u>=first.end || s[close+1u]!=':' ||
           !md_reference_key(s,begin+1u,close,key))return false;
        *target_line=first;*after_colon=close+2u;
        return true;
    }
    char scratch[MD_REF_KEY_MAX+1u];
    size_t used=0u;
    md_line current=first;
    for(size_t line_index=0u;line_index<3u;++line_index) {
        size_t from=begin+1u;
        if(line_index) {
            if(!current.newline || current.next>=d->raw_size)return false;
            current=md_read_line(d,current.next);
            from=md_left(s,current.begin,current.end);
            if(from>=current.end ||
               md_columns(s,current.begin,from)>3u ||
               md_root_code_span_interrupts(d,current))return false;
        }
        if(from>current.end)return false;
        const size_t closer=md_reference_label_close(
            s,current.end,from);
        if(closer!=SIZE_MAX &&
           (closer+1u>=current.end || s[closer+1u]!=':'))
            return false;
        const size_t until=closer==SIZE_MAX?current.end:closer;
        if(until<(begin+1u) ||
           until-(begin+1u)>MD_REF_KEY_MAX)
            return false;
        /* Invalid literal syntax cannot donate a later root continuation.
         * Escape pairs remain raw key bytes for md_reference_key; UTF-8
         * folding is separately owned by Salts::Unicode (#1192). */
        for(size_t at=from;at<until;) {
            const unsigned char c=(unsigned char)s[at];
            if(c=='\\' && at+1u<until &&
               md_ascii_punct(s[at+1u])) {at+=2u;continue;}
            if(c=='[' || c==']' ||
               (c<0x20u && c!='\t'))return false;
            ++at;
        }
        const size_t bytes=until-from;
        if(used>MD_REF_KEY_MAX || bytes>MD_REF_KEY_MAX-used)
            return false;
        if(bytes)memcpy(scratch+used,s+from,bytes);
        used+=bytes;
        if(closer!=SIZE_MAX) {
            if(!md_reference_key(scratch,0u,used,key))
                return false;
            *target_line=current;*after_colon=closer+2u;
            return true;
        }
        if(line_index==2u || used>=MD_REF_KEY_MAX ||
           !current.newline)return false;
        scratch[used++]=' '; /* canonical physical LF */
    }
    return false;
}

static bool md_reference_collect_line(salts_md_document *d,md_line line,
                                      size_t begin,size_t *after_definition,
                                      bool container) {
    const char *s=d->raw;
    if(begin>=line.end || s[begin]!='[')return false;
    char key[MD_REF_FOLD_MAX+1u];
    md_line target_line=line;
    size_t after_colon=0u;
    if(container &&
       (md_reference_label_close(s,line.end,begin+1u)==SIZE_MAX))
        return false;
    if(!md_reference_definition_label(d,line,begin,&target_line,
                                      &after_colon,key))return false;
    if(container && target_line.begin!=line.begin)return false;
    size_t target_begin=md_left(s,after_colon,target_line.end);
    md_line last=target_line;
    /* Only the OPENING definition has a <=3-column indentation limit.
     * A destination continuation may be deeply indented (#193), but a
     * blank or an actual interrupting root block cannot donate its bytes. */
    if(!container && target_begin==target_line.end &&
       target_line.next<d->raw_size) {
        const md_line next=md_read_line(d,target_line.next);
        const size_t candidate=md_left(s,next.begin,next.end);
        if(candidate<next.end && s[candidate]!='[' &&
           !md_root_code_span_interrupts(d,next)) {
            target_begin=candidate;last=next;
        }
    }
    if(target_begin>=last.end)return false;
    md_link_parts parts={0};
    size_t at=target_begin;
    if(!md_link_destination_scan(s,last.end,&at,&parts) ||
       (parts.destination_begin==parts.destination_end &&
        s[target_begin]!='<'))return false;
    const size_t destination_stop=at;
    at=md_left(s,at,last.end);
    char *title=NULL;size_t title_len=0u;
    bool has_title=false;
    if(at<last.end) {
        /* An invalid/unclosed same-line title invalidates the definition;
         * never keep only its destination (official #197/#209). */
        if(at==destination_stop)return false;
        if(container) {
            size_t cursor=at;
            md_link_parts scoped={0};bool incomplete=false;
            if(!md_link_title_scan(s,last.end,&cursor,&scoped,false,
                                   &incomplete) ||
               !md_blank(s,cursor,last.end))
                return false;
        }
        if(!md_reference_collect_title(d,last,at,&last,&title,&title_len))
            return false;
        has_title=true;
    } else if(!container && last.next<d->raw_size) {
        const md_line next=md_read_line(d,last.next);
        const size_t candidate=md_left(s,next.begin,next.end);
        if(candidate<next.end &&
           (s[candidate]=='"' || s[candidate]=='\'' || s[candidate]=='(')) {
            /* A failed OPTIONAL next-line title leaves the valid URL and
             * every unconsumed line intact (official #210). */
            has_title=md_reference_collect_title(
                d,next,candidate,&last,&title,&title_len);
            if(d->error!=SALTS_MD_OK) {free(title);return true;}
        }
    }
    if(d->reference_count>=MD_REF_MAX) {
        md_error(d,SALTS_MD_CAPACITY_EXCEEDED);free(title);return true;
    }
    char *href=NULL;size_t href_len=0u;
    if(!md_decode_component(d,s+parts.destination_begin,
            parts.destination_end-parts.destination_begin,&href,&href_len)) {
        free(title);return true;
    }
    if(!d->references) {
        d->references=(md_reference_binding*)calloc(
            MD_REF_MAX,sizeof(*d->references));
        if(!d->references) {
            md_error(d,SALTS_MD_OUT_OF_MEMORY);
            free(title);free(href);return true;
        }
    }
    /* Root definitions were collected first; newly admitted container
     * definitions must be inserted by SOURCE offset so first definition
     * wins even when it lived in a quote/list before the root binding.
     * This also keeps the definition-line binary search valid. */
    size_t index=d->reference_count;
    while(index && d->references[index-1u].source_line>line.begin)
        --index;
    if(index<d->reference_count)
        memmove(d->references+index+1u,d->references+index,
                (d->reference_count-index)*sizeof(*d->references));
    ++d->reference_count;
    md_reference_binding *item=&d->references[index];
    memcpy(item->key,key,strlen(key)+1u);
    item->href=href;item->href_bytes=href_len;
    item->title=title;item->title_bytes=title_len;
    item->has_title=has_title;
    item->source_line=line.begin;item->source_last_line=last.begin;
    if(after_definition)*after_definition=last.next;
    return true;
}

/* Called ONLY after existing quote/list owner has established this line's
 * content column and paragraph/fence precedence. In discovery, collect
 * its metadata into the SAME document table. During emission, suppress
 * only source intervals already admitted by discovery. */
static bool md_container_reference_line(salts_md_document *d,md_line line,
                                        size_t begin) {
    if(md_reference_definition_line(d,line.begin))return true;
    if(!d->collecting_container_definitions || begin>=line.end ||
       d->raw[begin]!='[')return false;
    size_t after=line.next;
    if(!md_reference_collect_line(d,line,begin,&after,true))return false;
    if(d->error==SALTS_MD_OK)d->discovered_container_definition=true;
    return true;
}

static void md_reference_collect_root(salts_md_document *d) {
    const char *s=d->raw;
    bool paragraph_started=false;
    bool fenced=false;
    unsigned root_html_kind=0u;
    bool container_pending=false;
    /* Root-only admission guard, not a second nested-list grammar. Keep
     * the outer list content column across blank lines: a blank does not
     * terminate a list item when the next line remains content-indented. */
    size_t list_content_column=0u;
    bool after_list_blank=false;
    md_fence fence={0u,0u,UINT32_MAX};
    for(size_t pos=0u;pos<d->raw_size && d->error==SALTS_MD_OK;) {
        const md_line line=md_read_line(d,pos);
        pos=line.next;
        const size_t begin=md_left(s,line.begin,line.end);
        const size_t columns=md_columns(s,line.begin,begin);
        if(fenced) {
            if(columns<=3u && md_fence_close(s,begin,line.end,fence))
                fenced=false;
            continue;
        }
        if(root_html_kind) {
            if(root_html_kind<=5u) {
                if(md_raw_html_line_ends(root_html_kind,s,begin,line.end))
                    root_html_kind=0u;
                continue; /* includes blank lines for HTML types 1-5 */
            }
            if(!md_blank(s,line.begin,line.end))continue;
            root_html_kind=0u; /* types 6/7 end BEFORE blank */
        }
        if(md_blank(s,line.begin,line.end)) {
            paragraph_started=false;
            container_pending=false;
            after_list_blank=list_content_column!=0u;
            continue;
        }
        if(after_list_blank) {
            /* The real list parser matches the existing item's content
             * column even after a blank. Never donate a continuation's
             * [label]: syntax to the ROOT reference table. Dedenting below
             * that column proves that this restricted root list has ended. */
            if(columns>=list_content_column)container_pending=true;
            else list_content_column=0u;
            after_list_blank=false;
        }
        /* One shared CommonMark HTML start/end gate suppresses reference
         * definitions inside raw blocks; no detached HTML parser. */
        const unsigned html_start=(!container_pending && columns<=3u)?
            md_raw_html_root_kind(s,begin,line.end,paragraph_started):0u;
        if(html_start) {
            root_html_kind=html_start;
            if(html_start<=5u &&
               md_raw_html_line_ends(html_start,s,begin,line.end))
                root_html_kind=0u;
            paragraph_started=false;
            continue;
        }
        /* Root thematic breaks can interrupt a preceding list/quote/code
         * owner without a blank line. Release its conservative reference
         * guard only when the production block parser can prove ROOT
         * ownership: spaced/star rules are unambiguous; a bare Setext
         * underline inside a still-indented list item is NOT.
         *
         * A root paragraph's "---" is a Setext heading, not a rule.
         * No nested definition grammar or list-owned binding is added. */
        const unsigned rule_setext=columns<=3u?
            md_setext(s,begin,line.end):0u;
        if(columns<=3u && md_rule(s,begin,line.end) &&
           !(paragraph_started && rule_setext) &&
           (!container_pending || !list_content_column ||
            columns<list_content_column || !rule_setext)) {
            paragraph_started=false;
            container_pending=false;
            list_content_column=0u;
            continue;
        }
        /* Setext underlines have precedence over list-marker admission
         * for an existing ROOT paragraph, including a single '-' line.
         * Keep this decision ahead of the list guard as in md_parse_blocks. */
        if(!container_pending && paragraph_started && rule_setext) {
            paragraph_started=false;
            continue;
        }
        char marker=0;size_t count=0,ib=0,ie=0;
        if(columns<=3u &&
           md_fence_start(s,begin,line.end,&marker,&count,&ib,&ie)) {
            /* A root fence interrupts a quote or indented-code owner.
             * The production block parser admits this same fence at the
             * root, so its close must permit the next root definition.
             * An unresolved list content column cannot be released here:
             * list-lazy fence admission is deliberately out of scope. */
            if(!list_content_column)container_pending=false;
            fenced=true;
            fence=(md_fence){marker,count,UINT32_MAX};
            paragraph_started=false;continue;
        }
        /* The root block parser admits GFM header+delimiter lookahead
         * BEFORE considering a list opener. A line like "- Name | Link"
         * may be a TABLE header rather than a LIST item; suppressing all
         * later root definitions as list continuation would drop a valid
         * forward reference from both table cells and root paragraphs.
         *
         * Mirror the SAME bounded table scan and row-stop predicates used
         * by md_parse_blocks. Only list-looking root headers require this
         * ownership disambiguation: other table headers cannot introduce
         * a phantom list guard. Literal table data rows, including
         * "[shadow]: /bad" without pipes, never donate definitions. */
        if(!paragraph_started && !container_pending &&
           !list_content_column && columns<=3u && pos<d->raw_size) {
            md_list_marker_info candidate={0};
            if(md_list_token(s,line,&candidate) &&
               !md_table_header_owned_by_list(d,line,pos)) {
                const md_line sep=md_read_line(d,pos);
                size_t header_columns=0u,delimiter_columns=0u;
                if(md_table_scan(s,begin,line.end,false,true,
                                 &header_columns,NULL,0u) &&
                   md_table_scan(s,md_left(s,sep.begin,sep.end),sep.end,
                                 true,true,&delimiter_columns,NULL,0u) &&
                   header_columns==delimiter_columns) {
                    pos=sep.next;
                    while(pos<d->raw_size) {
                        const md_line row=md_read_line(d,pos);
                        const size_t rb=md_left(s,row.begin,row.end);
                        size_t row_columns=0u;
                        if(rb-row.begin>3u ||
                           md_blank(s,row.begin,row.end) ||
                           md_table_starts_new_block(s,rb,row.end) ||
                           !md_table_scan(s,rb,row.end,false,false,
                                          &row_columns,NULL,0u))
                            break;
                        pos=row.next;
                    }
                    paragraph_started=false;
                    continue;
                }
            }
        }
        md_list_marker_info list_marker={0};
        /* An ordered marker other than '1.' or a marker without content
         * cannot interrupt an existing ROOT paragraph. The canonical
         * md_parse_blocks parser uses precisely this admission gate;
         * do not turn its literal paragraph text into a phantom list. */
        const bool list_start=columns<=3u &&
            md_list_token(s,line,&list_marker) &&
            (!paragraph_started ||
             md_list_interrupts_paragraph(&list_marker,line));
        if(columns>3u || s[begin]=='>' || list_start) {
            /* The outermost content column is sufficient for bounded
             * root-definition admission; nested list definitions remain
             * deferred. A new dedented list marker starts a new guard. */
            if(list_start &&
               (!list_content_column ||
                list_marker.column<list_content_column))
                list_content_column=list_marker.content_column;
            paragraph_started=false;
            container_pending=true;
            continue;
        }
        /* A dedented ATX heading is a proven ROOT block boundary, even
         * when a preceding LIST owner is still conservatively tracked by
         * the reference prepass. It cannot be a list continuation if the
         * marker's visual column is below the active item content column.
         * Keep indented headings within the list, never donating those
         * local [label]: lines to the document-root definition table.
         * This is the same caller-driven list-content test as production
         * md_list_consume; no independent nested-list parser is created. */
        if(container_pending && columns<=3u &&
           (!list_content_column || columns<list_content_column)) {
            size_t hb=begin,he=line.end;
            if(md_atx(s,&hb,&he)) {
                container_pending=false;
                list_content_column=0u;
                after_list_blank=false;
                paragraph_started=false;
                continue;
            }
        }
        if(container_pending)continue;
        if(md_rule(s,begin,line.end)) {
            paragraph_started=false;continue;
        }
        size_t heading_begin=begin,heading_end=line.end;
        if(md_atx(s,&heading_begin,&heading_end)) {
            paragraph_started=false;continue;
        }
        size_t after_definition=pos;
        if(!paragraph_started &&
           md_reference_collect_line(d,line,begin,&after_definition,false)) {
            pos=after_definition;
            paragraph_started=false;continue;
        }
        paragraph_started=true;
    }
}

static void md_reference_clear(salts_md_document *d) {
    if(!d)return;
    for(size_t i=0u;i<d->reference_count;++i) {
        free(d->references[i].href);
        free(d->references[i].title);
    }
    free(d->references);
    d->references=NULL;
    d->reference_count=0u;
}

static void md_parse_blocks(salts_md_document *d) {
    uint32_t paragraph = UINT32_MAX;
    md_root_break pending_break=MD_ROOT_BREAK_SOFT;
    uint32_t indented = UINT32_MAX;
    size_t pending_code_blank = SIZE_MAX;
    md_list_stack lists={0};
    bool code_open=false;
    unsigned html_block_kind=0u;
    uint32_t html_block_node=UINT32_MAX;
    bool reference_continuation=false;
    /* The root opening fence may consume 0..3 ASCII indentation columns.
     * Remove at most those opening columns from each subsequent code line;
     * never trim semantic trailing spaces or mutate the original raw bytes. */
    size_t root_fence_indent=0u;
    md_quote_stack quotes={0};
    quotes.paragraph=quotes.code=UINT32_MAX;
    md_fence fence = {0,0,UINT32_MAX};

    for (size_t pos=0; pos<d->raw_size && d->error==SALTS_MD_OK;) {
        const md_line line = md_read_line(d,pos);
        pos=line.next;
        const char *s=d->raw;
        const size_t indent_begin=md_left(s,line.begin,line.end);
        /* Block precedence uses visual columns, not raw prefix bytes:
         * a tab at column N advances to the next 4-column stop. Saturating
         * at four is enough for every current admission/continuation test. */
        size_t indent=0u;
        for(size_t i=line.begin;i<indent_begin && indent<4u;++i)
            indent += s[i]=='\t' ? 4u-(indent%4u) : 1u;
        size_t begin=indent_begin, end=line.end;
        if(html_block_kind) {
            if(html_block_kind>5u && md_blank(s,line.begin,line.end)) {
                html_block_kind=0u;html_block_node=UINT32_MAX;
                paragraph=UINT32_MAX;
                continue; /* type 6/7 ends BEFORE the blank line */
            }
            if(!md_raw_html_block_line(d,html_block_node,line,false))
                return;
            if(html_block_kind<=5u &&
               md_raw_html_line_ends(html_block_kind,s,begin,end)) {
                html_block_kind=0u;html_block_node=UINT32_MAX;
                paragraph=UINT32_MAX;
            }
            continue;
        }
        /* Once a ROOT definition's opening line was consumed below, its
         * recorded continuation interval takes precedence over indentation
         * and apparent block markers. No CODE/TEXT nodes may leak from an
         * already validated title or destination continuation (#193/#196). */
        if(reference_continuation &&
           md_reference_definition_line(d,line.begin))continue;
        reference_continuation=false;
        if (code_open) {
            if (indent<=3 && md_fence_close(s,begin,end,fence)) {
                code_open=false;
                continue;
            }
            /* CommonMark #131-133: opening-fence indentation removes up
             * to N initial ASCII spaces on each code line (N=0..3).
             * Keep every additional space and the original line-ending
             * bytes. Tabs have column-based semantics and are handled
             * separately from this ASCII-only deduction. */
            size_t content=line.begin,removed=0u;
            while(content<line.end && removed<root_fence_indent &&
                  s[content]==' ') {
                ++content;
                ++removed;
            }
            md_text(d,fence.node,s+content,line.next-content);
            continue;
        }
        if(md_list_consume(d,&lists,line,&pos,0u)) continue;
        if(d->error!=SALTS_MD_OK) return;
        /* A root quote container is matched before admitting root blocks.
         * Lazy lines may continue only the last quoted paragraph and never
         * create a new quote or survive a blank/competing block opener. */
        size_t quote_body=line.begin;
        const size_t quote_depth=md_quote_prefix(s,line,&quote_body);
        if(quote_depth) {
            /* A reduced number of explicit '>' markers may lazily continue
             * an already-open LIST_ITEM paragraph in the deepest quote.
             * Reuse the same bounded list frame with NO read-ahead cursor:
             * lazy source is text, never a new TABLE/list/fence owner. */
            if(quote_depth<quotes.depth && quotes.lists.depth &&
               !quotes.fence_open &&
               !md_blank(s,quote_body,line.end)) {
                md_line inner=line;
                inner.begin=quote_body;
                const size_t content=md_left(s,quote_body,line.end);
                md_list_frame *top=
                    &quotes.lists.frames[quotes.lists.depth-1u];
                if(!top->pending_blank &&
                   (top->paragraph!=UINT32_MAX ||
                    top->quote_paragraph!=UINT32_MAX) &&
                   !md_quote_lazy_interrupts(s,inner,content)) {
                    if(md_list_consume(d,&quotes.lists,inner,NULL,0u))
                        continue;
                    if(d->error!=SALTS_MD_OK)return;
                }
            }
            /* A continuing nested quote paragraph may omit any number of
             * innermost markers. Match already-open quote containers
             * before closing them; block starts and blanks are not lazy. */
            if(quote_depth<quotes.depth && quotes.lazy &&
               quotes.paragraph!=UINT32_MAX &&
               quotes.lists.depth==0u && !quotes.fence_open &&
               !md_blank(s,quote_body,line.end)) {
                md_line inner=line;
                inner.begin=quote_body;
                const size_t content=md_left(s,quote_body,line.end);
                if(!md_quote_lazy_interrupts(s,inner,content)) {
                    if(md_add(d,quotes.paragraph,SALTS_MD_BLOCK_LINE_BREAK,0u)
                       ==UINT32_MAX ||
                       !md_inline(d,quotes.paragraph,s+content,
                                  line.end-content,0u))
                        return;
                    continue;
                }
            }
            if(indented!=UINT32_MAX) {
                indented=UINT32_MAX;
                pending_code_blank=SIZE_MAX;
            }
            paragraph=UINT32_MAX;
            if(!md_quote_line(d,&quotes,line,quote_depth,quote_body,
                              &pos)) return;
            continue;
        }
        if(quotes.depth) {
            /* A missing OUTER '>' lazily continues an already-open paragraph
             * owned by a quoted LIST_ITEM or its inner QUOTE. No blank,
             * new block opener or read-ahead TABLE admission is permitted. */
            if(quotes.lists.depth) {
                md_list_frame *top=
                    &quotes.lists.frames[quotes.lists.depth-1u];
                if((top->quote_paragraph!=UINT32_MAX ||
                    top->paragraph!=UINT32_MAX) &&
                   !top->pending_blank &&
                   !md_blank(s,line.begin,line.end) &&
                   !md_quote_lazy_interrupts(s,line,begin)) {
                    if(md_list_consume(d,&quotes.lists,line,NULL,0u)) continue;
                    if(d->error!=SALTS_MD_OK) return;
                }
            }
            if(quotes.lazy && quotes.paragraph!=UINT32_MAX &&
               !md_blank(s,line.begin,line.end) &&
               !md_quote_lazy_interrupts(s,line,begin)) {
                if(md_add(d,quotes.paragraph,SALTS_MD_BLOCK_LINE_BREAK,0u)
                   ==UINT32_MAX ||
                   !md_inline(d,quotes.paragraph,s+begin,end-begin,0u))
                    return;
                continue;
            }
            quotes.depth=0u;
            quotes.paragraph=quotes.code=UINT32_MAX;
            quotes.lists.depth=0u;
            quotes.fence_open=quotes.lazy=false;
            quotes.html_kind=0u;quotes.html_node=UINT32_MAX;
        }
        /* Keep interstitial blank lines pending until another indented line
         * proves that they are interior. On termination/EOF they vanish. */
        if (indented!=UINT32_MAX) {
            if(md_blank(s,line.begin,line.end)) {
                if(pending_code_blank==SIZE_MAX) pending_code_blank=line.begin;
                continue;
            }
            if(md_code_unindent(s,line.begin,line.end)>line.begin) {
                if(pending_code_blank!=SIZE_MAX) {
                    for(size_t at=pending_code_blank;at<line.begin;) {
                        const md_line gap=md_read_line(d,at);
                        if(!md_code_line(d,indented,gap,true)) return;
                        at=gap.next;
                    }
                    pending_code_blank=SIZE_MAX;
                }
                if(!md_code_line(d,indented,line,false)) return;
                continue;
            }
            indented=UINT32_MAX;
            pending_code_blank=SIZE_MAX;
        }
        if (md_blank(s,line.begin,line.end)) {
            paragraph=UINT32_MAX;
            continue;
        }
        /* Canonical raw-HTML block admission: types 1-6 interrupt root
         * paragraphs, type 7 does not; the same classifier is used in
         * reference preflight to preserve document-source ownership. */
        const unsigned html_kind=(indent<=3u && lists.depth==0u &&
                                 quotes.depth==0u)?
            md_raw_html_root_kind(s,begin,end,paragraph!=UINT32_MAX):0u;
        if(html_kind) {
            const uint32_t node=md_add(
                d,0u,SALTS_MD_BLOCK_RAW_HTML_BLOCK,0u);
            if(node==UINT32_MAX ||
               !md_raw_html_block_line(d,node,line,true))return;
            html_block_node=node;
            html_block_kind=html_kind<=5u &&
                md_raw_html_line_ends(html_kind,s,begin,end)?0u:html_kind;
            paragraph=UINT32_MAX;pending_break=MD_ROOT_BREAK_SOFT;
            continue;
        }
        /* Definitions are recorded by the bounded root prepass. Do not
         * emit their RAW syntax as paragraph TEXT; all link metadata was
         * already copied and is consulted only during this parse. */
        if(paragraph==UINT32_MAX && lists.depth==0u && indent<=3u &&
           md_reference_definition_line(d,line.begin)) {
            pending_break=MD_ROOT_BREAK_SOFT;
            reference_continuation=true;
            continue;
        }
        /* Four indentation columns select literal code before checking ATX,
         * thematic rules, fences, or other block markers. Indented code
         * cannot interrupt a paragraph or the active list-item continuation. */
        if(paragraph==UINT32_MAX && lists.depth==0u &&
           md_code_unindent(s,line.begin,line.end)>line.begin) {
            indented=md_add(d,0u,SALTS_MD_BLOCK_CODE_INDENTED,0u);
            if(indented==UINT32_MAX || !md_code_line(d,indented,line,false))
                return;
            continue;
        }
        unsigned setext = md_setext(s,begin,end);
        if (paragraph!=UINT32_MAX && indent<=3 && setext) {
            d->nodes[paragraph].kind=SALTS_MD_BLOCK_HEADING;
            d->nodes[paragraph].heading_level=setext;
            paragraph=UINT32_MAX;
            continue;
        }
        if (indent <= 3 && md_rule(s,begin,end)) {
            (void)md_add(d,0u,SALTS_MD_BLOCK_RULE,0);
            paragraph=UINT32_MAX;
            continue;
        }
        char marker=0; size_t run=0, info_b=0,info_e=0;
        if (indent<=3 && md_fence_start(s,begin,end,
                &marker,&run,&info_b,&info_e)) {
            const uint32_t node=md_add(d,0u,SALTS_MD_BLOCK_CODE_FENCE,0);
            if (node==UINT32_MAX) return;
            md_fence_info_meta(d,node,s+info_b,info_e-info_b);
            fence=(md_fence){marker,run,node};
            root_fence_indent=indent;
            code_open=true;
            paragraph=UINT32_MAX;
            continue;
        }
        if (indent<=3) {
            size_t hstart=begin, hend=end;
            const unsigned level=md_atx(s,&hstart,&hend);
            if (level) {
                const uint32_t node=md_add(d,0u,SALTS_MD_BLOCK_HEADING,level);
                if (node==UINT32_MAX) return;
                (void)md_inline(d,node,s+hstart,hend-hstart,0);
                paragraph=UINT32_MAX;
                continue;
            }
        }
        /* The same bounded root/quote TABLE grammar; root admission
         * retains the established paragraph, list and indentation gates. */
        if(indent<=3u && paragraph==UINT32_MAX &&
           lists.depth==0u &&
           !md_table_header_owned_by_list(d,line,pos) &&
           md_table_admit(d,0u,line,begin,&pos,0u,0u)) {
            if(d->error!=SALTS_MD_OK)return;
            paragraph=UINT32_MAX;
            continue;
        }
        md_list_marker_info top_marker={0};
        if(indent<=3 && md_list_token(s,line,&top_marker) &&
           (paragraph==UINT32_MAX ||
            md_list_interrupts_paragraph(&top_marker,line)) &&
           !md_rule(s,begin,end)) {
            if(!md_list_open(d,&lists,0u,&top_marker,s,end,&pos,0u)) return;
            paragraph=UINT32_MAX;
            continue;
        }
        /* A root-paragraph code span can own multiple physical lines.
         * Consume them as ONE typed code node before emitting soft breaks. */
        if(md_root_multiline_code_span(d,&paragraph,line,begin,&pos)) {
            if(d->error!=SALTS_MD_OK) return;
            continue;
        }
        /* One two-line ROOT inline title candidate consumes both physical
         * lines into one owned LINK, without manufacturing a soft break
         * inside its href/title or competing with list/quote ownership. */
        if(paragraph==UINT32_MAX && lists.depth==0u && quotes.depth==0u &&
           md_root_multiline_inline_title(d,&paragraph,line,begin,
                                          &pos,&pending_break)) {
            if(d->error!=SALTS_MD_OK)return;
            continue;
        }
        /* One typed multi-line HTML token; SAFE always escapes. */
        if(paragraph==UINT32_MAX && lists.depth==0u && quotes.depth==0u &&
           md_root_multiline_raw_html(d,&paragraph,line,begin,
                                      &pos,&pending_break)) {
            if(d->error!=SALTS_MD_OK)return;
            continue;
        }
        /* #182: allow matched runs across SOFT physical paragraph lines
         * without permitting a match across a block/container boundary. */
        if(paragraph==UINT32_MAX && lists.depth==0u &&
           md_root_paragraph_emphasis(d,&paragraph,line,begin,&pos)) {
            if(d->error!=SALTS_MD_OK)return;
            pending_break=MD_ROOT_BREAK_SOFT;
            continue;
        }
        if(d->error!=SALTS_MD_OK)return;
        /* This slice owns root paragraph semantics only. Nested list/quote
         * breaks still use their existing parser until scoped admission. */
        const bool root=lists.depth==0u && quotes.depth==0u;
        const bool continues=root && md_root_paragraph_continues(d,line);
        size_t text_end=end;
        const md_root_break next_break=root?
            md_root_line_content(s,begin,end,continues,&text_end):
            MD_ROOT_BREAK_SOFT;
        if (paragraph==UINT32_MAX) {
            paragraph=md_add(d,0u,SALTS_MD_BLOCK_PARAGRAPH,0);
            if (paragraph==UINT32_MAX) return;
        } else if(root) {
            if(!md_root_emit_break(d,paragraph,pending_break))return;
        } else if(md_add(d,paragraph,SALTS_MD_BLOCK_LINE_BREAK,0)==
                  UINT32_MAX)return;
        if(!md_inline(d,paragraph,s+begin,text_end-begin,0u))return;
        pending_break=next_break;
    }
}

void salts_md_document_destroy(salts_md_document *d) {
    if (!d) return;
    free(d->raw);
    free(d->text);
    free(d->metadata);
    free(d->nodes);
    free(d->node_metadata);
    md_reference_clear(d);
    free(d);
}

salts_md_rich_document_view salts_md_get_document_view(const salts_md_document *d) {
    if (!d) return (salts_md_rich_document_view){0};
    return (salts_md_rich_document_view){
        .document = {
            .source = d->text,
            .source_bytes = d->text_size,
            .blocks = d->nodes,
            .block_count = d->node_count
        },
        .metadata = d->metadata,
        .metadata_bytes = d->metadata_size,
        .block_metadata = d->node_metadata,
        .block_metadata_count = d->node_count,
        .raw_markdown = d->raw,
        .raw_markdown_bytes = d->raw_size
    };
}

salts_md_status salts_md_parse(const char *input,size_t length,
                             size_t max_nodes,size_t max_source_bytes,
                             salts_md_document **out) {
    if (!out) return SALTS_MD_INVALID_ARGUMENT;
    *out=NULL;
    if ((!input && length) || !max_nodes || !max_source_bytes)
        return SALTS_MD_INVALID_ARGUMENT;
    if (length>max_source_bytes || max_nodes>UINT32_MAX ||
        length==SIZE_MAX) return SALTS_MD_CAPACITY_EXCEEDED;
    salts_md_document *d=(salts_md_document *)calloc(1,sizeof(*d));
    if (!d) return SALTS_MD_OUT_OF_MEMORY;
    d->node_limit=max_nodes;
    d->raw_size=length;
    d->raw=(char*)malloc(length+1u);
    d->text=(char*)malloc(length+1u);
    d->metadata=(char*)malloc(length+1u);
    d->metadata_capacity=length;
    d->text_capacity=length;
    if (!d->raw || !d->text || !d->metadata) {
        salts_md_document_destroy(d);
        return SALTS_MD_OUT_OF_MEMORY;
    }
    if(length) memcpy(d->raw,input,length);
    d->raw[length]='\0';
    d->text[0]=d->metadata[0]='\0';
    if (md_add(d,SALTS_MD_PARENT_NONE,SALTS_MD_BLOCK_DOCUMENT,0)==UINT32_MAX) {
        const salts_md_status error=d->error;
        salts_md_document_destroy(d);
        return error;
    }
    md_reference_collect_root(d);
    if(d->error==SALTS_MD_OK) {
        /* Discovery and final emission use the SAME canonical block parser.
         * Only a document containing a real container definition is
         * replayed. Nothing from discovery is exposed to callers: reset
         * owned IR arenas, preserve bounded refs, then publish once. */
        d->collecting_container_definitions=true;
        md_parse_blocks(d);
        d->collecting_container_definitions=false;
        if(d->error==SALTS_MD_OK && d->discovered_container_definition) {
            d->node_count=d->text_size=d->metadata_size=0u;
            d->text[0]=d->metadata[0]='\0';
            if(md_add(d,SALTS_MD_PARENT_NONE,
                      SALTS_MD_BLOCK_DOCUMENT,0u)!=UINT32_MAX)
                md_parse_blocks(d);
        }
    }
    /* Parse-local binding state is discarded before the public RichDoc
     * snapshot is validated or returned. No raw runtime binding persists. */
    md_reference_clear(d);
    if (d->error==SALTS_MD_OK) {
        d->nodes[0].byte_end=d->text_size;
        const salts_md_rich_document_view view=salts_md_get_document_view(d);
        d->error=salts_md_rich_document_validate(&view,max_nodes);
    }
    if (d->error!=SALTS_MD_OK) {
        const salts_md_status error=d->error;
        salts_md_document_destroy(d);
        return error;
    }
    *out=d;
    return SALTS_MD_OK;
}
