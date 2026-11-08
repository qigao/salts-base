/* #181: real C11 RichDoc block-container semantics and exact HTML output. */
#include <salts/markdown.h>
#include <salts/markdown_html.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr,"line %d: %s\n",__LINE__,#x); return false; \
} } while(0)

typedef struct fixture {
    const char *name,*markdown,*expected;
    int loose; /* -1: don't inspect; otherwise outer list loose flag */
    unsigned list_count;
} fixture;

static const fixture cases[]={
    {"cm-108","  - foo\n\n    bar\n",
     "<ul>\n<li>\n<p>foo</p>\n<p>bar</p>\n</li>\n</ul>\n",1,1u},
    {"cm-109","1.  foo\n\n    - bar\n",
     "<ol>\n<li>\n<p>foo</p>\n<ul>\n<li>bar</li>\n</ul>\n</li>\n</ol>\n",1,2u},
    {"cm-254","1.  A paragraph\n    with two lines.\n\n        indented code\n\n    > A block quote.\n",
     "<ol>\n<li>\n<p>A paragraph\nwith two lines.</p>\n<pre><code>indented code\n</code></pre>\n<blockquote>\n<p>A block quote.</p>\n</blockquote>\n</li>\n</ol>\n",1,1u},
    {"cm-255","- one\n\n two\n",
     "<ul>\n<li>one</li>\n</ul>\n<p>two</p>\n",0,1u},
    {"cm-256","- one\n\n  two\n",
     "<ul>\n<li>\n<p>one</p>\n<p>two</p>\n</li>\n</ul>\n",1,1u},
    {"cm-301","- foo\n- bar\n+ baz\n",
     "<ul>\n<li>foo</li>\n<li>bar</li>\n</ul>\n<ul>\n<li>baz</li>\n</ul>\n",0,2u},
    {"cm-302","1. foo\n2. bar\n3) baz\n",
     "<ol>\n<li>foo</li>\n<li>bar</li>\n</ol>\n<ol start=\"3\">\n<li>baz</li>\n</ol>\n",0,2u},
    {"cm-304","The number of windows in my house is\n14.  The number of doors is 6.\n",
     "<p>The number of windows in my house is\n14.  The number of doors is 6.</p>\n",-1,0u},
    {"cm-305","The number of windows in my house is\n1.  The number of doors is 6.\n",
     "<p>The number of windows in my house is</p>\n<ol>\n<li>The number of doors is 6.</li>\n</ol>\n",0,1u},
    {"ordered-zero","0. zero\n",
     "<ol start=\"0\">\n<li>zero</li>\n</ol>\n",0,1u},
    {"nested-tight","- outer\n  - inner\n- next\n",
     "<ul>\n<li>outer\n<ul>\n<li>inner</li>\n</ul>\n</li>\n<li>next</li>\n</ul>\n",0,2u},
    {"tab-item","- alpha\n\tbeta\n",
     "<ul>\n<li>alpha\nbeta</li>\n</ul>\n",0,1u},
    {"cm-005","- foo\n\n\t\tbar\n",
     "<ul>\n<li>\n<p>foo</p>\n<pre><code>  bar\n</code></pre>\n</li>\n</ul>\n",1,1u},
    {"cm-006",">\t\tfoo\n",
     "<blockquote>\n<pre><code>  foo\n</code></pre>\n</blockquote>\n",-1,0u},
    {"cm-007","-\t\tfoo\n",
     "<ul>\n<li>\n<pre><code>  foo\n</code></pre>\n</li>\n</ul>\n",0,1u},
    {"quote-partial-tab-not-code","> \tfoo\n",
     "<blockquote>\n<p>foo</p>\n</blockquote>\n",-1,0u},
    {"marker-single-tab-padding","-\tfoo\n",
     "<ul>\n<li>foo</li>\n</ul>\n",0,1u},

    {"unicode-loose","- \xe4\xb8\xad\xf0\x9f\x91\xa9\n\n  \xe6\x97\xa5\n",
     "<ul>\n<li>\n<p>\xe4\xb8\xad\xf0\x9f\x91\xa9</p>\n<p>\xe6\x97\xa5</p>\n</li>\n</ul>\n",1,1u},
    /* Preserve already-successful CommonMark examples, not only new gates. */
    {"cm-060","* Foo\n* * *\n* Bar\n",
     "<ul>\n<li>Foo</li>\n</ul>\n<hr />\n<ul>\n<li>Bar</li>\n</ul>\n",0,2u},
    {"cm-280","-\n\n  foo\n",
     "<ul>\n<li></li>\n</ul>\n<p>foo</p>\n",0,1u},
    {"cm-295","- foo\n - bar\n  - baz\n   - boo\n",
     "<ul>\n<li>foo</li>\n<li>bar</li>\n<li>baz</li>\n<li>boo</li>\n</ul>\n",0,1u},
    {"cm-310","- a\n - b\n  - c\n   - d\n  - e\n - f\n- g\n",
     "<ul>\n<li>a</li>\n<li>b</li>\n<li>c</li>\n<li>d</li>\n<li>e</li>\n<li>f</li>\n<li>g</li>\n</ul>\n",0,1u},
    {"cm-312","- a\n - b\n  - c\n   - d\n    - e\n",
     "<ul>\n<li>a</li>\n<li>b</li>\n<li>c</li>\n<li>d\n- e</li>\n</ul>\n",0,1u},
    {"cm-325","* foo\n  * bar\n\n  baz\n",
     "<ul>\n<li>\n<p>foo</p>\n<ul>\n<li>bar</li>\n</ul>\n<p>baz</p>\n</li>\n</ul>\n",1,2u},
    /* #181: quote paragraphs inside a list item are owned and continued
     * within one quote, without admitting synthetic blank paragraphs. */
    {"cm-320","* a\n  > b\n  >\n* c\n",
     "<ul>\n<li>a\n<blockquote>\n<p>b</p>\n</blockquote>\n</li>\n<li>c</li>\n</ul>\n",0,1u},
    {"list-quote-marked-continuation","* a\n  > b\n  > c\n* d\n",
     "<ul>\n<li>a\n<blockquote>\n<p>b\nc</p>\n</blockquote>\n</li>\n<li>d</li>\n</ul>\n",0,1u},
    {"list-quote-explicit-blank","* a\n  > b\n  >\n  > c\n* d\n",
     "<ul>\n<li>a\n<blockquote>\n<p>b</p>\n<p>c</p>\n</blockquote>\n</li>\n<li>d</li>\n</ul>\n",0,1u},
    {"cm-264","- Foo\n\n      bar\n\n\n      baz\n",
     "<ul>\n<li>\n<p>Foo</p>\n<pre><code>bar\n\n\nbaz\n</code></pre>\n</li>\n</ul>\n",1,1u},
    {"quote-list-code-interior-blanks","> - Foo\n>\n>       bar\n>\n>\n>       baz\n",
     "<blockquote>\n<ul>\n<li>\n<p>Foo</p>\n<pre><code>bar\n\n\nbaz\n</code></pre>\n</li>\n</ul>\n</blockquote>\n",1,1u},
    {"list-code-trailing-blanks","- Foo\n\n      bar\n\n\n",
     "<ul>\n<li>\n<p>Foo</p>\n<pre><code>bar\n</code></pre>\n</li>\n</ul>\n",1,1u},
    {"cm-285","foo\n*\n\nfoo\n1.\n",
     "<p>foo\n*</p>\n<p>foo\n1.</p>\n",-1,0u},
    {"cm-315","* a\n*\n\n* c\n",
     "<ul>\n<li>\n<p>a</p>\n</li>\n<li></li>\n<li>\n<p>c</p>\n</li>\n</ul>\n",1,1u},
    {"root-empty-list-item","*\n",
     "<ul>\n<li></li>\n</ul>\n",0,1u},
    {"quote-marked-empty-not-interrupt","> foo\n> *\n",
     "<blockquote>\n<p>foo\n*</p>\n</blockquote>\n",-1,0u},
    {"quote-lazy-empty-not-interrupt","> foo\n*\n",
     "<blockquote>\n<p>foo\n*</p>\n</blockquote>\n",-1,0u},
    /* Marker-only indentation counts exactly one space after the marker,
     * not all trailing whitespace on that otherwise empty source line. */
    {"cm-279","-   \n  foo\n",
     "<ul>\n<li>foo</li>\n</ul>\n",0,1u},
    {"quoted-empty-marker-indent","> -   \n>   foo\n",
     "<blockquote>\n<ul>\n<li>foo</li>\n</ul>\n</blockquote>\n",0,1u},
    {"ordered-empty-marker-indent","1.    \n   foo\n",
     "<ol>\n<li>foo</li>\n</ol>\n",0,1u},
    {"empty-marker-blank-closes","-   \n\n  foo\n",
     "<ul>\n<li></li>\n</ul>\n<p>foo</p>\n",0,1u},
    {"empty-marker-later-loose","-   \n  foo\n\n  bar\n",
     "<ul>\n<li>\n<p>foo</p>\n<p>bar</p>\n</li>\n</ul>\n",1,1u},
    {"empty-marker-insufficient-indent","-   \n foo\n",
     "<ul>\n<li></li>\n</ul>\n<p>foo</p>\n",0,1u},
    {"cm-298","- - foo\n",
     "<ul>\n<li>\n<ul>\n<li>foo</li>\n</ul>\n</li>\n</ul>\n",0,2u},
    {"cm-299","1. - 2. foo\n",
     "<ol>\n<li>\n<ul>\n<li>\n<ol start=\"2\">\n<li>foo</li>\n</ol>\n</li>\n</ul>\n</li>\n</ol>\n",0,3u},
    {"quoted-inline-nested-list","> - - foo\n",
     "<blockquote>\n<ul>\n<li>\n<ul>\n<li>foo</li>\n</ul>\n</li>\n</ul>\n</blockquote>\n",0,2u},
    {"inline-nested-siblings","- - foo\n- - bar\n",
     "<ul>\n<li>\n<ul>\n<li>foo</li>\n</ul>\n</li>\n<li>\n<ul>\n<li>bar</li>\n</ul>\n</li>\n</ul>\n",0,3u},
    {"inline-nested-continuation","- - foo\n    bar\n",
     "<ul>\n<li>\n<ul>\n<li>foo\nbar</li>\n</ul>\n</li>\n</ul>\n",0,2u},
    {"inline-nested-empty","- -\n",
     "<ul>\n<li>\n<ul>\n<li></li>\n</ul>\n</li>\n</ul>\n",0,2u},
    /* #319: the child consumes the separating blank; the outer list stays
     * tight. Do not propagate nested looseness to ancestor list metadata. */
    {"cm-319","- a\n  - b\n\n    c\n- d\n",
     "<ul>\n<li>a\n<ul>\n<li>\n<p>b</p>\n<p>c</p>\n</li>\n</ul>\n</li>\n<li>d</li>\n</ul>\n",0,2u},
    {"nested-blank-sibling","- a\n  - b\n\n  - c\n- d\n",
     "<ul>\n<li>a\n<ul>\n<li>\n<p>b</p>\n</li>\n<li>\n<p>c</p>\n</li>\n</ul>\n</li>\n<li>d</li>\n</ul>\n",0,2u},
    {"quoted-nested-blank","> - a\n>   - b\n>\n>     c\n> - d\n",
     "<blockquote>\n<ul>\n<li>a\n<ul>\n<li>\n<p>b</p>\n<p>c</p>\n</li>\n</ul>\n</li>\n<li>d</li>\n</ul>\n</blockquote>\n",0,2u},
    /* #326 is deliberately loose at the OUTER level: a blank separates
     * the outer -a and -d items, rather than a consumed child continuation. */
    {"cm-326","- a\n  - b\n  - c\n\n- d\n  - e\n  - f\n",
     "<ul>\n<li>\n<p>a</p>\n<ul>\n<li>b</li>\n<li>c</li>\n</ul>\n</li>\n<li>\n<p>d</p>\n<ul>\n<li>e</li>\n<li>f</li>\n</ul>\n</li>\n</ul>\n",1,3u},
    /* Five-plus ASCII spaces after a marker: exactly one separates the
     * marker; the other four-plus begin a code block within that item. */
    {"cm-273","1.     indented code\n\n   paragraph\n\n       more code\n",
     "<ol>\n<li>\n<pre><code>indented code\n</code></pre>\n<p>paragraph</p>\n<pre><code>more code\n</code></pre>\n</li>\n</ol>\n",1,1u},
    {"cm-274","1.      indented code\n\n   paragraph\n\n       more code\n",
     "<ol>\n<li>\n<pre><code> indented code\n</code></pre>\n<p>paragraph</p>\n<pre><code>more code\n</code></pre>\n</li>\n</ol>\n",1,1u},
    {"unordered-initial-code","-     code\n",
     "<ul>\n<li>\n<pre><code>code\n</code></pre>\n</li>\n</ul>\n",0,1u},
    {"four-space-marker-paragraph","1.    paragraph\n",
     "<ol>\n<li>paragraph</li>\n</ol>\n",0,1u},
    {"quoted-initial-code","> 1.     indented code\n>\n>    paragraph\n>\n>        more code\n",
     "<blockquote>\n<ol>\n<li>\n<pre><code>indented code\n</code></pre>\n<p>paragraph</p>\n<pre><code>more code\n</code></pre>\n</li>\n</ol>\n</blockquote>\n",1,1u},
    {"initial-code-trailing-blank","1.     code\n\n",
     "<ol>\n<li>\n<pre><code>code\n</code></pre>\n</li>\n</ol>\n",0,1u},
    {"cm-300","- # Foo\n- Bar\n  ---\n  baz\n",
     "<ul>\n<li>\n<h1>Foo</h1>\n</li>\n<li>\n<h2>Bar</h2>\nbaz</li>\n</ul>\n",0,1u},
    {"list-atx-heading","- # H\n- text\n",
     "<ul>\n<li>\n<h1>H</h1>\n</li>\n<li>text</li>\n</ul>\n",0,1u},
    {"ordered-atx-heading","1. # T\n",
     "<ol>\n<li>\n<h1>T</h1>\n</li>\n</ol>\n",0,1u},
    {"cm-061","- Foo\n- * * *\n",
     "<ul>\n<li>Foo</li>\n<li>\n<hr />\n</li>\n</ul>\n",0,1u},
    {"item-rule-loose-after-blank","- Foo\n\n  * * *\n",
     "<ul>\n<li>\n<p>Foo</p>\n<hr />\n</li>\n</ul>\n",1,1u},
    {"nested-outer-item-rule","- outer\n  - inner\n  * * *\n",
     "<ul>\n<li>outer\n<ul>\n<li>inner</li>\n</ul>\n<hr />\n</li>\n</ul>\n",0,2u},
    {"rule-then-list-sibling","- Foo\n  * * *\n- Bar\n",
     "<ul>\n<li>Foo\n<hr />\n</li>\n<li>Bar</li>\n</ul>\n",0,1u},
    {"quoted-list-rule-continuation","> - Foo\n>   * * *\n",
     "<blockquote>\n<ul>\n<li>Foo\n<hr />\n</li>\n</ul>\n</blockquote>\n",-1,1u},

    {"root-item-rule","- * * *\n",
     "<ul>\n<li>\n<hr />\n</li>\n</ul>\n",0,1u},
    {"ordered-item-rule","1. * * *\n2. text\n",
     "<ol>\n<li>\n<hr />\n</li>\n<li>text</li>\n</ol>\n",0,1u},
    {"quoted-item-rule","> - * * *\n",
     "<blockquote>\n<ul>\n<li>\n<hr />\n</li>\n</ul>\n</blockquote>\n",-1,1u},
    {"item-continuation-rule","- Foo\n  * * *\n",
     "<ul>\n<li>Foo\n<hr />\n</li>\n</ul>\n",0,1u},
    {"ordered-continuation-rule","10. Foo\n    * * *\n",
     "<ol start=\"10\">\n<li>Foo\n<hr />\n</li>\n</ol>\n",0,1u},
    {"item-setext-not-rule","- Foo\n  ---\n",
     "<ul>\n<li>\n<h2>Foo</h2>\n</li>\n</ul>\n",0,1u},
    {"dedented-root-rule","- Foo\n* * *\n",
     "<ul>\n<li>Foo</li>\n</ul>\n<hr />\n",0,1u},
    {"thematic-before-list","- ---\n","<hr />\n",-1,0u},
    {"inline-emphasis-not-rule","- *a*\n",
     "<ul>\n<li><em>a</em></li>\n</ul>\n",0,1u},
    {"root-thematic-control","- Foo\n---\n",
     "<ul>\n<li>Foo</li>\n</ul>\n<hr />\n",0,1u},
    {"nested-inline-atx-heading","- - # Foo\n",
     "<ul>\n<li>\n<ul>\n<li>\n<h1>Foo</h1>\n</li>\n</ul>\n</li>\n</ul>\n",0,2u},
    {"quoted-list-heading","> - # Foo\n> - Bar\n>   ---\n>   baz\n",
     "<blockquote>\n<ul>\n<li>\n<h1>Foo</h1>\n</li>\n<li>\n<h2>Bar</h2>\nbaz</li>\n</ul>\n</blockquote>\n",0,1u},
    {"list-setext-equals","- Foo\n  ===\n",
     "<ul>\n<li>\n<h1>Foo</h1>\n</li>\n</ul>\n",0,1u}
};
_Static_assert(sizeof(cases)/sizeof(cases[0])==78u,
               "Scoped list container fixture denominator changed");

static bool check_case(const fixture *f) {
    const size_t source_length=strlen(f->markdown);
    salts_md_document *owner=NULL;
    REQUIRE(salts_md_parse(f->markdown,source_length,512u,
                          source_length+1u,&owner)==SALTS_MD_OK);
    REQUIRE(owner!=NULL);
    const salts_md_rich_document_view view=salts_md_get_document_view(owner);
    REQUIRE(salts_md_rich_document_validate(&view,512u)==SALTS_MD_OK);
    REQUIRE(view.raw_markdown_bytes==source_length);
    REQUIRE(memcmp(view.raw_markdown,f->markdown,source_length)==0);

    unsigned lists=0;
    int loose=-1;
    for(size_t i=0;i<view.document.block_count;++i) {
        const salts_md_block *node=&view.document.blocks[i];
        REQUIRE(node->stable_id==i+1u);
        if(node->kind==SALTS_MD_BLOCK_LIST||
           node->kind==SALTS_MD_BLOCK_ORDERED_LIST) {
            const char *meta=NULL;size_t bytes=0;
            REQUIRE(salts_md_rich_document_metadata(&view,i,&meta,&bytes)==SALTS_MD_OK);
            REQUIRE(bytes==sizeof(salts_md_list_semantics));
            salts_md_list_semantics info;
            memcpy(&info,meta,sizeof(info));
            REQUIRE(info.loose<=1u);
            REQUIRE(info.reserved[0]==0u&&info.reserved[1]==0u);
            if(lists==0) loose=info.loose;
            if(node->kind==SALTS_MD_BLOCK_ORDERED_LIST)
                REQUIRE(info.start<=999999999u);
            else REQUIRE(info.start==0u);
            ++lists;
        }
    }
    REQUIRE(lists==f->list_count);
    if(f->loose!=-1) REQUIRE(loose==f->loose);
    size_t required=0;
    REQUIRE(salts_md_render_html(&view,512u,NULL,0,&required)==SALTS_MD_OK);
    REQUIRE(required==strlen(f->expected));
    char *out=(char*)malloc(required+2u);
    REQUIRE(out!=NULL);
    memset(out,0x7d,required+2u);
    size_t checked=0;
    REQUIRE(salts_md_render_html(&view,512u,out,required,&checked)==
            SALTS_MD_CAPACITY_EXCEEDED);
    REQUIRE(checked==required);
    for(size_t i=0;i<required+2u;++i) REQUIRE(out[i]==0x7d);
    REQUIRE(salts_md_render_html(&view,512u,out,required+1u,&checked)==SALTS_MD_OK);
    REQUIRE(checked==required);
    if(strcmp(out,f->expected)!=0)
        fprintf(stderr,"fixture %s\nactual: %s\nexpected: %s\n",
                f->name,out,f->expected);
    REQUIRE(strcmp(out,f->expected)==0);
    REQUIRE((unsigned char)out[required+1u]==0x7d);
    free(out);
    salts_md_document_destroy(owner);
    return true;
}

/* A TAB crossing the item's/quote's content boundary is only partly
 * consumed by that container. The remaining display columns must be emitted
 * as literal spaces in the owned CODE_INDENTED child, never dropped or
 * spliced back into the immutable raw Markdown input. */
static bool test_partial_tab_code_ownership(void) {
    static const struct {
        const char *markdown,*code;
        salts_md_block_kind expected_parent;
    } samples[]={
        {"- foo\n\n\t\tbar\n","  bar\n",SALTS_MD_BLOCK_LIST_ITEM},
        {">\t\tfoo\n","  foo\n",SALTS_MD_BLOCK_QUOTE},
        {"-\t\tfoo\n","  foo\n",SALTS_MD_BLOCK_LIST_ITEM}
    };
    _Static_assert(sizeof(samples)/sizeof(samples[0])==3u,
                   "official tab overhang native owner cases");
    for(size_t k=0u;k<sizeof(samples)/sizeof(samples[0]);++k) {
        salts_md_document *doc=NULL;
        const size_t bytes=strlen(samples[k].markdown);
        if(salts_md_parse(samples[k].markdown,bytes,128u,bytes+1u,&doc)!=
           SALTS_MD_OK || !doc) return false;
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        bool success=salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK &&
            v.raw_markdown_bytes==bytes &&
            !memcmp(v.raw_markdown,samples[k].markdown,bytes);
        size_t code=SIZE_MAX;
        for(size_t i=1u;success && i<v.document.block_count;++i)
            if(v.document.blocks[i].kind==SALTS_MD_BLOCK_CODE_INDENTED) {
                if(code!=SIZE_MAX) success=false;
                code=i;
            }
        success=success && code!=SIZE_MAX;
        if(success) {
            const size_t parent=v.document.blocks[code].parent_index;
            success=parent<v.document.block_count &&
                v.document.blocks[parent].kind==samples[k].expected_parent;
        }
        char payload[64]={0};size_t used=0u;
        for(size_t i=code+1u;success && i<v.document.block_count;++i) {
            const salts_md_block *node=&v.document.blocks[i];
            if(node->parent_index!=code)continue;
            if(node->kind!=SALTS_MD_BLOCK_TEXT ||
               node->byte_end<node->byte_begin ||
               node->byte_end>v.document.source_bytes) {success=false;break;}
            const size_t n=node->byte_end-node->byte_begin;
            if(n>=sizeof(payload)-used) {success=false;break;}
            memcpy(payload+used,v.document.source+node->byte_begin,n);
            used+=n;
        }
        success=success && used==strlen(samples[k].code) &&
                !memcmp(payload,samples[k].code,used) &&
                v.document.block_count>1u &&
                v.document.blocks[v.document.block_count-1u].kind==
                    SALTS_MD_BLOCK_TEXT &&
                v.document.blocks[v.document.block_count-1u].parent_index==
                    code;
        /* The final CODE TEXT allocation is the exact failing node when
         * this bound is one below the successful owned document size. */
        const size_t code_text_limited=v.document.block_count-1u;
        if(!success)
            fprintf(stderr,"native tab ownership failed: #%zu code '%s'\n",
                    k+5u,payload);
        salts_md_document_destroy(doc);
        if(!success)return false;
        salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
        if(salts_md_parse(samples[k].markdown,bytes,code_text_limited,
                         bytes+1u,&rejected)!=SALTS_MD_CAPACITY_EXCEEDED ||
           rejected!=NULL) {
            fprintf(stderr,"tab code TEXT budget rollback failed #%zu\n",k+5u);
            return false;
        }
    }
    return true;
}

/* A list-line HR is a list ITEM child, never a root rule or paragraph.
 * Verify the actual typed owner (not just HTML output) and raw bytes on
 * root, ordered, quoted, and subsequent item lines. */
static bool test_thematic_item_ownership(void) {
    static const char *const samples[]={
        "- Foo\n- * * *\n",
        "- * * *\n",
        "1. * * *\n2. text\n",
        "> - * * *\n",
        "- Foo\n  * * *\n",
        "10. Foo\n    * * *\n",
        "- Foo\n\n  * * *\n",
        "- outer\n  - inner\n  * * *\n",
        "- Foo\n  * * *\n- Bar\n",
        "> - Foo\n>   * * *\n"
    };
    _Static_assert(sizeof(samples)/sizeof(samples[0])==10u,
                   "item rule owner denominator");
    for(size_t k=0u;k<sizeof(samples)/sizeof(samples[0]);++k) {
        salts_md_document *owned=NULL;
        const size_t size=strlen(samples[k]);
        if(salts_md_parse(samples[k],size,256u,size+1u,&owned)!=SALTS_MD_OK ||
           !owned)return false;
        const salts_md_rich_document_view v=salts_md_get_document_view(owned);
        bool ok=salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK &&
            v.raw_markdown_bytes==size &&
            !memcmp(v.raw_markdown,samples[k],size);
        size_t rule_count=0u;
        for(size_t i=1u;ok && i<v.document.block_count;++i) {
            const salts_md_block *b=&v.document.blocks[i];
            if(b->kind!=SALTS_MD_BLOCK_RULE)continue;
            ++rule_count;
            if(b->parent_index>=i ||
               v.document.blocks[b->parent_index].kind!=
                   SALTS_MD_BLOCK_LIST_ITEM)
                ok=false;
        }
        ok=ok && rule_count==1u;
        salts_md_document_destroy(owned);
        if(!ok) {
            fprintf(stderr,"thematic rule owner mismatch case %zu\n",k);
            return false;
        }
    }
    return true;
}

/* The newly admitted item RULE is an owned node, and exhausting its
 * allocation must reject the complete document without a half-published
 * RichDoc. Verify exact one-node-short capacity for direct, sibling and
 * continuation admissions. */
static bool test_item_rule_last_node_budget(void) {
    static const char *const sources[]={
        "- * * *\n",
        "- Foo\n  * * *\n",
        "- Foo\n- * * *\n",
        "10. Foo\n    * * *\n"
    };
    _Static_assert(sizeof(sources)/sizeof(sources[0])==4u,
                   "item RULE rollback denominator");
    for(size_t k=0u;k<sizeof(sources)/sizeof(sources[0]);++k) {
        const size_t n=strlen(sources[k]);
        salts_md_document *doc=NULL;
        if(salts_md_parse(sources[k],n,128u,n+1u,&doc)!=SALTS_MD_OK ||
           !doc)return false;
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        const size_t count=v.document.block_count;
        const bool valid=salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK &&
            count>1u &&
            v.document.blocks[count-1u].kind==SALTS_MD_BLOCK_RULE &&
            v.document.blocks[count-1u].parent_index<count-1u &&
            v.document.blocks[v.document.blocks[count-1u].parent_index].kind==
                SALTS_MD_BLOCK_LIST_ITEM;
        salts_md_document_destroy(doc);
        if(!valid)return false;
        salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
        if(salts_md_parse(sources[k],n,count-1u,n+1u,&rejected)!=
           SALTS_MD_CAPACITY_EXCEEDED || rejected!=NULL) {
            fprintf(stderr,"item RULE node-budget rollback failed #%zu\n",k);
            return false;
        }
    }
    return true;
}

int main(void) {
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        if(!check_case(&cases[i])) {
            fprintf(stderr,"FAILED: %s\n",cases[i].name);
            return 1;
        }
    }
    if(!test_partial_tab_code_ownership())return 1;
    if(!test_thematic_item_ownership())return 1;
    if(!test_item_rule_last_node_budget())return 1;
    const char *limited="- one\n\n  two\n";
    salts_md_document *d=(salts_md_document*)(uintptr_t)1u;
    const salts_md_status status=salts_md_parse(limited,strlen(limited),2u,
                                             strlen(limited)+1u,&d);
    if(status!=SALTS_MD_CAPACITY_EXCEEDED||d!=NULL) {
        fprintf(stderr,"node exhaustion must roll back exactly once\n");
        return 1;
    }
    /* The quote-continuation soft-break allocation must also be failure
     * atomic. Eight nodes admit the first quote text but not its next
     * LINE_BREAK, and the caller must never receive a partial RichDoc. */
    const char *quote_limit="* a\n  > b\n  > c\n";
    d=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(quote_limit,strlen(quote_limit),8u,
                     strlen(quote_limit)+1u,&d)!=SALTS_MD_CAPACITY_EXCEEDED ||
       d!=NULL) {
        fprintf(stderr,"quote continuation node exhaustion must roll back\n");
        return 1;
    }
    /* Interior blank line publication must also fail atomically. With
     * eight nodes, the second deferred newline exceeds the node budget. */
    const char *code_blanks="- Foo\n\n      bar\n\n\n      baz\n";
    d=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(code_blanks,strlen(code_blanks),8u,
                     strlen(code_blanks)+1u,&d)!=SALTS_MD_CAPACITY_EXCEEDED ||
       d!=NULL) {
        fprintf(stderr,"indented code blank-node exhaustion must roll back\n");
        return 1;
    }
    /* Inline marker chains are bounded by the same list-frame depth as
     * multiline nesting; exceeding 32 frames must fail atomically. */
    char inline_deep[128];
    size_t inline_used=0u;
    for(size_t i=0u;i<33u;++i) {
        inline_deep[inline_used++]='-';
        inline_deep[inline_used++]=' ';
    }
    inline_deep[inline_used++]='x';
    inline_deep[inline_used++]='\n';
    d=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(inline_deep,inline_used,512u,
                     inline_used+1u,&d)!=SALTS_MD_CAPACITY_EXCEEDED ||
       d!=NULL) {
        fprintf(stderr,"inline nesting overflow must roll back\n");
        return 1;
    }
    const char *inline_limited="- - foo\n";
    d=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(inline_limited,strlen(inline_limited),3u,
                     strlen(inline_limited)+1u,&d)!=SALTS_MD_CAPACITY_EXCEEDED ||
       d!=NULL) {
        fprintf(stderr,"inline nesting node exhaustion must roll back\n");
        return 1;
    }
    /* The initial indented code must not publish a partially initialized
     * RichDoc when creating its first TEXT node exhausts the node budget. */
    const char *initial_code_limited="1.     code\n";
    d=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(initial_code_limited,strlen(initial_code_limited),4u,
                     strlen(initial_code_limited)+1u,&d)!=
       SALTS_MD_CAPACITY_EXCEEDED || d!=NULL) {
        fprintf(stderr,"initial indented code must roll back on node OOM\n");
        return 1;
    }
    /* The inline text child of a directly admitted heading is still
     * resource-bounded and must not publish a partial RichDoc on failure. */
    const char *heading_limited="- # Foo\n";
    d=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(heading_limited,strlen(heading_limited),4u,
                     strlen(heading_limited)+1u,&d)!=SALTS_MD_CAPACITY_EXCEEDED ||
       d!=NULL) {
        fprintf(stderr,"list heading node-budget failure must roll back\n");
        return 1;
    }
    /* The container nesting limit is executable, not just documented:
     * forcing a 33rd frame must fail atomically without returning a view. */
    char deep[4096];
    size_t used=0u;
    for(size_t depth=0u;depth<34u;++depth) {
        for(size_t j=0u;j<depth*2u;++j) deep[used++]=' ';
        deep[used++]='-'; deep[used++]=' '; deep[used++]='x';
        deep[used++]='\n';
    }
    deep[used]='\0';
    d=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(deep,used,2048u,used+1u,&d)!=
       SALTS_MD_CAPACITY_EXCEEDED||d!=NULL) {
        fprintf(stderr,"container depth overflow must roll back\n");
        return 1;
    }
    printf("Native CommonMark list container subset: %zu/%zu passed\n",
        sizeof(cases)/sizeof(cases[0]),sizeof(cases)/sizeof(cases[0]));
    return 0;
}
