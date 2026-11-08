#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK "\x60"
#define VERIFY(expr) do { \
    if(!(expr)) { \
        fprintf(stderr,"%s:%d: failed: %s\n",__FILE__,__LINE__,#expr); \
        failure=1;goto done; \
    } \
} while(0)

typedef struct reference_fixture {
    const char *markdown,*href,*title,*visible,*html;
} reference_fixture;

static size_t kind_count(salts_md_rich_document_view v,
                         salts_md_block_kind kind) {
    size_t sum=0u;
    for(size_t i=0u;i<v.document.block_count;++i)
        sum+=(v.document.blocks[i].kind==kind);
    return sum;
}

static int test_reference(const reference_fixture *f) {
    int failure=0;
    salts_md_document *doc=NULL;
    const size_t n=strlen(f->markdown);
    VERIFY(salts_md_parse(f->markdown,n,128u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
    VERIFY(v.raw_markdown_bytes==n);
    VERIFY(!memcmp(v.raw_markdown,f->markdown,n));
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK_TITLE)==(f->title?1u:0u));
    VERIFY(kind_count(v,SALTS_MD_BLOCK_PARAGRAPH)<=1u);
    bool href_found=false,title_found=!f->title,visible_found=false;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        VERIFY(node->parent_index<i);
        if(node->kind==SALTS_MD_BLOCK_LINK ||
           node->kind==SALTS_MD_BLOCK_LINK_TITLE) {
            const char *meta=NULL;size_t size=SIZE_MAX;
            VERIFY(salts_md_rich_document_metadata(
                &v,i,&meta,&size)==SALTS_MD_OK);
            const char *expect=node->kind==SALTS_MD_BLOCK_LINK?
                               f->href:f->title;
            VERIFY(expect!=NULL);
            VERIFY(size==strlen(expect));
            VERIFY(!size || (meta&&!memcmp(meta,expect,size)));
            if(node->kind==SALTS_MD_BLOCK_LINK)href_found=true;
            else {
                VERIFY(node->parent_index+1u==i);
                VERIFY(v.document.blocks[node->parent_index].kind==
                       SALTS_MD_BLOCK_LINK);
                title_found=true;
            }
        }
        if(node->kind==SALTS_MD_BLOCK_TEXT &&
           v.document.blocks[node->parent_index].kind==SALTS_MD_BLOCK_LINK) {
            const size_t bytes=node->byte_end-node->byte_begin;
            VERIFY(node->byte_end<=v.document.source_bytes);
            if(bytes==strlen(f->visible) &&
               !memcmp(v.document.source+node->byte_begin,
                       f->visible,bytes))visible_found=true;
        }
    }
    VERIFY(href_found && title_found && visible_found);
    const size_t expected=strlen(f->html);
    size_t need=SIZE_MAX;
    VERIFY(salts_md_render_html(&v,128u,NULL,0u,&need)==SALTS_MD_OK);
    VERIFY(need==expected);
    char output[256];
    VERIFY(expected+2u<sizeof(output));
    memset(output,0x31,sizeof(output));
    size_t actual=SIZE_MAX;
    VERIFY(salts_md_render_html(&v,128u,output,expected,&actual)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(actual==expected);
    for(size_t i=0u;i<sizeof(output);++i)
        VERIFY((unsigned char)output[i]==0x31u);
    VERIFY(salts_md_render_html(&v,128u,output,expected+1u,&actual)==
           SALTS_MD_OK);
    VERIFY(actual==expected);
    VERIFY(!memcmp(output,f->html,expected+1u));
    VERIFY((unsigned char)output[expected+1u]==0x31u);
done:
    salts_md_document_destroy(doc);
    return failure;
}

static int test_unsafe(void) {
    int failure=0;salts_md_document *doc=NULL;
    const char source[]="[x][evil]\n\n[evil]: java&#x73;cript:alert\n";
    const size_t n=sizeof(source)-1u;
    VERIFY(salts_md_parse(source,n,128u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
    char output[128];memset(output,0x39,sizeof(output));
    size_t required=SIZE_MAX;
    VERIFY(salts_md_render_html(&v,128u,output,sizeof(output),&required)==
           SALTS_MD_INVALID_ARGUMENT);
    for(size_t i=0u;i<sizeof(output);++i)
        VERIFY((unsigned char)output[i]==0x39u);
done:
    salts_md_document_destroy(doc);
    return failure;
}

static int test_fence(void) {
    int failure=0;salts_md_document *doc=NULL;
    const char source[]=
        TICK TICK TICK "text\n[foo]: /url\n" TICK TICK TICK "\n\n[foo]\n";
    const size_t n=sizeof(source)-1u;
    VERIFY(salts_md_parse(source,n,128u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_CODE_FENCE)==1u);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
    VERIFY(v.raw_markdown_bytes==n);
    VERIFY(!memcmp(v.raw_markdown,source,n));
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Canonical quote/list owners may admit definitions at real block
 * boundaries, but lazy continuation, fenced/indented code and active
 * paragraph text remain semantically opaque. Earlier root-only exclusions
 * that were contrary to CommonMark are now positive ownership gates. */
static int test_container_definition_fences(void) {
    int failure=0;salts_md_document *doc=NULL;
    static const char *const blocked[]={
        "- item\n  [hidden]: /uri\n\n[hidden]\n",
        "> quote\n[hidden]: /uri\n\n[hidden]\n",
        "    [hidden]: /uri\n\n[hidden]\n",
        "paragraph\n[hidden]: /uri\n\n[hidden]\n",
        "paragraph\n== invalid\n[hidden]: /uri\n\n[hidden]\n",
        "- item\n  ---\n  continuation\n  [hidden]: /bad\n\n[hidden]\n",
        "    # literal\n    [hidden]: /uri\n\n[hidden]\n",
        "Root\n1. real\n===\n[hidden]: /uri\n\n[hidden]\n",
        "Root\n+ real\n===\n[hidden]: /uri\n\n[hidden]\n"
    };
    _Static_assert(sizeof(blocked)/sizeof(blocked[0])==9u,
                   "real container-opaque negative denominator");
    for(size_t i=0u;i<sizeof(blocked)/sizeof(blocked[0]);++i) {
        const size_t n=strlen(blocked[i]);
        VERIFY(salts_md_parse(blocked[i],n,128u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
        VERIFY(v.raw_markdown_bytes==n &&
               !memcmp(v.raw_markdown,blocked[i],n));
        salts_md_document_destroy(doc);doc=NULL;
    }
    static const struct {
        const char *source,*href;
    } owner_definitions[]={
        {"- item\n    * * *\n    [hidden]: /bad\n\n[hidden]\n","/bad"},
        {"> quote\n> # inner\n> [hidden]: /uri\n\n[hidden]\n","/uri"},
        {"- item\n\n  [hidden]: /uri\n\n[hidden]\n","/uri"},
        {"1. item\n\n   [hidden]: /uri\n\n[hidden]\n","/uri"},
        {"- item\n\n\t[hidden]: /uri\n\n[hidden]\n","/uri"},
        {"- outer\n\n  - inner\n\n    [hidden]: /uri\n\n[hidden]\n","/uri"},
        {"- item\n\n  [hidden]: /uri\n\n  continuation\n\n[hidden]\n","/uri"}
    };
    _Static_assert(sizeof(owner_definitions)/sizeof(owner_definitions[0])
                   ==7u,"normative quote/list binding denominator");
    for(size_t i=0u;i<sizeof(owner_definitions)/
                         sizeof(owner_definitions[0]);++i) {
        const char *source=owner_definitions[i].source;
        const size_t n=strlen(source);
        VERIFY(salts_md_parse(source,n,256u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
        bool found=false;
        for(size_t j=1u;j<v.document.block_count;++j) {
            if(v.document.blocks[j].kind!=SALTS_MD_BLOCK_LINK)continue;
            const char *href=NULL;size_t bytes=SIZE_MAX;
            VERIFY(salts_md_rich_document_metadata(&v,j,&href,&bytes)
                   ==SALTS_MD_OK);
            VERIFY(bytes==strlen(owner_definitions[i].href) &&
                   !memcmp(href,owner_definitions[i].href,bytes));
            found=true;
        }
        VERIFY(found);
        VERIFY(v.raw_markdown_bytes==n &&
               !memcmp(v.raw_markdown,source,n));
        salts_md_document_destroy(doc);doc=NULL;
    }
    /* Root dedent after a blank still releases the active container. */
    static const char *const roots[]={
        "- item\n\n[visible]: /uri\n\n[visible]\n",
        "10. item\n\n   [visible]: /uri\n\n[visible]\n"
    };
    for(size_t i=0u;i<sizeof(roots)/sizeof(roots[0]);++i) {
        const size_t n=strlen(roots[i]);
        VERIFY(salts_md_parse(roots[i],n,128u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
        salts_md_document_destroy(doc);doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* List-owned and root definitions share one document map; both become
 * visible to later root references, preserving their individual hrefs. */
static int test_list_blank_scope(void) {
    int failure=0;salts_md_document *doc=NULL;
    const char source[]=
        "- item\n\n  [hidden]: /bad\n\n[visible]: /good\n\n[hidden] [visible]\n";
    const size_t n=sizeof(source)-1u;
    VERIFY(salts_md_parse(source,n,256u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    VERIFY(v.raw_markdown_bytes==n && !memcmp(v.raw_markdown,source,n));
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==2u);
    bool saw_bad=false,saw_good=false;
    for(size_t i=1u;i<v.document.block_count;++i) {
        if(v.document.blocks[i].kind!=SALTS_MD_BLOCK_LINK)continue;
        const char *href=NULL;size_t bytes=SIZE_MAX;
        VERIFY(salts_md_rich_document_metadata(&v,i,&href,&bytes)
               ==SALTS_MD_OK);
        if(bytes==4u && !memcmp(href,"/bad",4u))saw_bad=true;
        else if(bytes==5u && !memcmp(href,"/good",5u))saw_good=true;
        else VERIFY(false);
    }
    VERIFY(saw_bad && saw_good);
    char html[512];size_t required=SIZE_MAX;
    VERIFY(salts_md_render_html(&v,256u,html,sizeof(html),&required)==
           SALTS_MD_OK);
    VERIFY(required<sizeof(html));
    VERIFY(strstr(html,"<a href=\"/bad\">hidden</a>")!=NULL);
    VERIFY(strstr(html,"<a href=\"/good\">visible</a>")!=NULL);
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Real list definitions must obey the SAME 128-entry global map budget;
 * 129 unique list definitions fail atomically rather than being silently
 * treated as inert root-only syntax. Exact 128 entries still bind. */
static int test_list_capacity_isolation(void) {
    int failure=0;salts_md_document *doc=NULL;
    char source[8192];
    const char prefix[]="- item\n\n",suffix[]="\n[k0]\n";
    for(size_t count=128u;count<=129u;++count) {
        size_t used=sizeof(prefix)-1u;
        memcpy(source,prefix,used);
        for(size_t i=0u;i<count;++i) {
            char line[32];
            const int n=snprintf(line,sizeof(line),"  [k%zu]: /uri\n",i);
            VERIFY(n>0 && (size_t)n<sizeof(line));
            VERIFY(used+(size_t)n<sizeof(source));
            memcpy(source+used,line,(size_t)n);used+=(size_t)n;
        }
        VERIFY(used+sizeof(suffix)-1u<sizeof(source));
        memcpy(source+used,suffix,sizeof(suffix)-1u);
        used+=sizeof(suffix)-1u;
        const salts_md_status status=
            salts_md_parse(source,used,1024u,used+1u,&doc);
        if(count==129u) {
            VERIFY(status==SALTS_MD_CAPACITY_EXCEEDED);
            VERIFY(doc==NULL);
        } else {
            VERIFY(status==SALTS_MD_OK && doc!=NULL);
            const salts_md_rich_document_view v=salts_md_get_document_view(doc);
            VERIFY(salts_md_rich_document_validate(&v,1024u)==SALTS_MD_OK);
            VERIFY(v.raw_markdown_bytes==used &&
                   !memcmp(v.raw_markdown,source,used));
            VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
            salts_md_document_destroy(doc);doc=NULL;
        }
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* A verified root thematic break releases an earlier container guard.
 * These cases intentionally leave the list/quote/code without a blank line;
 * document ownership and the emitted LINK's copied metadata must agree. */
static int test_root_rule_owner_release(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    static const char *const sources[]={
        "- item\n* * *\n[key]: /root\n\n[key]\n",
        "- item\n---\n[key]: /root\n\n[key]\n",
        "> quote\n* * *\n[key]: /root\n\n[key]\n",
        "    code\n* * *\n[key]: /root\n\n[key]\n"
    };
    _Static_assert(sizeof(sources)/sizeof(sources[0])==4u,
                   "root-rule release test count");
    for(size_t k=0u;k<sizeof(sources)/sizeof(sources[0]);++k) {
        const size_t n=strlen(sources[k]);
        VERIFY(salts_md_parse(sources[k],n,256u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view view=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&view,256u)==SALTS_MD_OK);
        VERIFY(view.raw_markdown_bytes==n);
        VERIFY(!memcmp(view.raw_markdown,sources[k],n));
        VERIFY(kind_count(view,SALTS_MD_BLOCK_RULE)==1u);
        VERIFY(kind_count(view,SALTS_MD_BLOCK_LINK)==1u);
        bool copied=false;
        for(size_t i=1u;i<view.document.block_count;++i) {
            if(view.document.blocks[i].kind!=SALTS_MD_BLOCK_LINK)continue;
            const char *meta=NULL;size_t bytes=SIZE_MAX;
            VERIFY(salts_md_rich_document_metadata(
                &view,i,&meta,&bytes)==SALTS_MD_OK);
            VERIFY(meta!=NULL && bytes==5u && !memcmp(meta,"/root",5u));
            copied=true;
        }
        VERIFY(copied);
        char output[512];size_t required=SIZE_MAX;
        VERIFY(salts_md_render_html(
            &view,256u,output,sizeof(output),&required)==SALTS_MD_OK);
        VERIFY(required<sizeof(output));
        VERIFY(strstr(output,"<a href=\"/root\">key</a>")!=NULL);
        salts_md_document_destroy(doc);
        doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* A root-level ATX heading terminates an already open quote or
 * indented code block. The following root ASCII definition is source-owned
 * and its metadata is copied into the RichDoc before prepass teardown. */
static int test_root_atx_owner_release(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    static const char *const cases[]={
        "> quote\n# heading\n[ref]: /uri\n\n[ref]\n",
        "    code\n# heading\n[ref]: /uri\n\n[ref]\n",
        "> quote\r\n  ## heading\r\n[ref]: /uri\r\n\r\n[ref]\r\n"
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==3u,
                   "ATX root owner evidence denominator");
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        const size_t bytes=strlen(cases[k]);
        VERIFY(salts_md_parse(cases[k],bytes,256u,bytes+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==bytes);
        VERIFY(!memcmp(v.raw_markdown,cases[k],bytes));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_HEADING)==1u);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
        bool copied=false;
        for(size_t i=1u;i<v.document.block_count;++i) {
            const salts_md_block *link=&v.document.blocks[i];
            if(link->kind!=SALTS_MD_BLOCK_LINK)continue;
            VERIFY(link->parent_index<i);
            const salts_md_block *parent=&v.document.blocks[link->parent_index];
            VERIFY(parent->kind==SALTS_MD_BLOCK_PARAGRAPH);
            VERIFY(parent->parent_index==0u);
            const char *meta=NULL;size_t len=SIZE_MAX;
            VERIFY(salts_md_rich_document_metadata(
                &v,i,&meta,&len)==SALTS_MD_OK);
            VERIFY(meta && len==4u && !memcmp(meta,"/uri",4u));
            copied=true;
        }
        VERIFY(copied);
        char html[512];memset(html,0x6b,sizeof(html));
        size_t needed=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,256u,NULL,0u,&needed)==SALTS_MD_OK);
        VERIFY(needed+1u<sizeof(html));
        size_t reported=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,256u,html,needed,&reported)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(reported==needed);
        for(size_t i=0u;i<sizeof(html);++i)
            VERIFY((unsigned char)html[i]==0x6bu);
        VERIFY(salts_md_render_html(&v,256u,html,needed+1u,&reported)==
               SALTS_MD_OK);
        VERIFY(reported==needed);
        VERIFY(strstr(html,"<a href=\"/uri\">ref</a>")!=NULL);
        VERIFY((unsigned char)html[needed+1u]==0x6bu);
        salts_md_document_destroy(doc);
        doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Root fenced-code admission must end a prior quote/indented-code
 * scope without granting reference definitions to the literal fence body.
 * The same root-only prepass and owned RichDoc emitter are under test. */
static int test_root_fence_owner_release(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    typedef struct fenced_case { const char *source,*html; } fenced_case;
    static const fenced_case cases[]={
        {"> quote\n" TICK TICK TICK "c\n[shadow]: /blocked\n"
         TICK TICK TICK "\n[ref]: /uri\n\n[ref]\n",
         "<blockquote>\n<p>quote</p>\n</blockquote>\n"
         "<pre><code class=\"language-c\">[shadow]: /blocked\n</code></pre>\n"
         "<p><a href=\"/uri\">ref</a></p>\n"},
        {"    code\n~~~\n[shadow]: /blocked\n~~~\n[ref]: /uri\n\n[ref]\n",
         "<pre><code>code\n</code></pre>\n"
         "<pre><code>[shadow]: /blocked\n</code></pre>\n"
         "<p><a href=\"/uri\">ref</a></p>\n"},
        {"> quote\n~~~~\n[shadow]: /blocked\n~~~\n[shadow]: /blocked\n"
         "~~~~\n[ref]: /uri\n\n[ref]\n",
         "<blockquote>\n<p>quote</p>\n</blockquote>\n"
         "<pre><code>[shadow]: /blocked\n~~~\n[shadow]: /blocked\n</code></pre>\n"
         "<p><a href=\"/uri\">ref</a></p>\n"}
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==3u,
                   "root-fence owner positives");
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        const size_t n=strlen(cases[k].source);
        VERIFY(salts_md_parse(cases[k].source,n,256u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==n);
        VERIFY(!memcmp(v.raw_markdown,cases[k].source,n));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_CODE_FENCE)==1u);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
        bool copied=false;
        for(size_t i=1u;i<v.document.block_count;++i) {
            const salts_md_block *node=&v.document.blocks[i];
            if(node->kind!=SALTS_MD_BLOCK_LINK)continue;
            VERIFY(node->parent_index<i);
            const salts_md_block *parent=&v.document.blocks[node->parent_index];
            VERIFY(parent->kind==SALTS_MD_BLOCK_PARAGRAPH);
            VERIFY(parent->parent_index==0u);
            const char *meta=NULL;size_t bytes=SIZE_MAX;
            VERIFY(salts_md_rich_document_metadata(
                &v,i,&meta,&bytes)==SALTS_MD_OK);
            VERIFY(meta && bytes==4u && !memcmp(meta,"/uri",4u));
            copied=true;
        }
        VERIFY(copied);
        const size_t expected=strlen(cases[k].html);
        char output[512];size_t required=SIZE_MAX;
        VERIFY(expected+2u<sizeof(output));
        VERIFY(salts_md_render_html(&v,256u,NULL,0u,&required)==SALTS_MD_OK);
        VERIFY(required==expected);
        memset(output,0x73,sizeof(output));
        size_t actual=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,256u,output,expected,&actual)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(actual==expected);
        for(size_t i=0u;i<sizeof(output);++i)
            VERIFY((unsigned char)output[i]==0x73u);
        VERIFY(salts_md_render_html(&v,256u,output,expected+1u,&actual)==
               SALTS_MD_OK);
        VERIFY(actual==expected);
        VERIFY(!memcmp(output,cases[k].html,expected+1u));
        VERIFY((unsigned char)output[expected+1u]==0x73u);
        salts_md_document_destroy(doc);
        doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Closed and unclosed fences cannot donate root definitions. Even a
 * quoted or list-owned fake definition remains inert after the fence. */
static int test_fence_definition_opacity(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    static const char *const sources[]={
        "> quote\n~~~\n[shadow]: /blocked\n~~~\n\n[shadow]\n",
        "> quote\n~~~\n[shadow]: /blocked\n\n[shadow]\n",
        "- item\n  ~~~\n  [shadow]: /blocked\n  ~~~\n\n[shadow]\n"
    };
    _Static_assert(sizeof(sources)/sizeof(sources[0])==3u,
                   "fence-negative owner cases");
    for(size_t k=0u;k<sizeof(sources)/sizeof(sources[0]);++k) {
        const size_t n=strlen(sources[k]);
        VERIFY(salts_md_parse(sources[k],n,256u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==n);
        VERIFY(!memcmp(v.raw_markdown,sources[k],n));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
        salts_md_document_destroy(doc);
        doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* GFM tables are parsed by the existing block parser. Their one-column
 * pipe-optional DATA rows cannot donate ASCII reference definitions, while
 * a definition after a real root boundary can still bind a table-cell link.
 * Require copied href ownership in the final RichDoc, not raw source state. */
static int test_table_reference_owner(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    typedef struct table_fixture {
        const char *source,*html;
        size_t links,table_links;
    } table_fixture;
    static const table_fixture cases[]={
        {"| Column |\n| --- |\n[shadow]: /blocked\n\n"
         "[ref]: /uri \"T\"\n\n[shadow] [ref]\n",
         "<table>\n<thead>\n<tr>\n<th>Column</th>\n</tr>\n</thead>\n"
         "<tbody>\n<tr>\n<td>[shadow]: /blocked</td>\n</tr>\n</tbody>\n"
         "</table>\n<p>[shadow] <a href=\"/uri\" title=\"T\">ref</a></p>\n",
         1u,0u},
        {"| Item | Link |\n| --- | --- |\n| A | [go][ref] |\n\n"
         "[ref]: /uri\n\n[go][ref]\n",
         "<table>\n<thead>\n<tr>\n<th>Item</th>\n<th>Link</th>\n"
         "</tr>\n</thead>\n<tbody>\n<tr>\n<td>A</td>\n"
         "<td><a href=\"/uri\">go</a></td>\n</tr>\n</tbody>\n"
         "</table>\n<p><a href=\"/uri\">go</a></p>\n",
         2u,1u},
        {"| Column |\n| --- |\n[shadow]: /blocked\n"
         "# Root\n[ref]: /uri\n\n[shadow] [ref]\n",
         "<table>\n<thead>\n<tr>\n<th>Column</th>\n</tr>\n</thead>\n"
         "<tbody>\n<tr>\n<td>[shadow]: /blocked</td>\n</tr>\n</tbody>\n"
         "</table>\n<h1>Root</h1>\n"
         "<p>[shadow] <a href=\"/uri\">ref</a></p>\n",
         1u,0u}
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==3u,
                   "table/root reference owner denominator");
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        const size_t bytes=strlen(cases[k].source);
        VERIFY(salts_md_parse(cases[k].source,bytes,256u,bytes+1u,&doc)==
               SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==bytes);
        VERIFY(!memcmp(v.raw_markdown,cases[k].source,bytes));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_TABLE)==1u);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==cases[k].links);
        size_t link_count=0u,cell_links=0u;
        for(size_t i=1u;i<v.document.block_count;++i) {
            const salts_md_block *node=&v.document.blocks[i];
            if(node->kind!=SALTS_MD_BLOCK_LINK)continue;
            VERIFY(node->parent_index<i);
            const salts_md_block *owner=&v.document.blocks[node->parent_index];
            VERIFY(owner->kind==SALTS_MD_BLOCK_PARAGRAPH ||
                   owner->kind==SALTS_MD_BLOCK_TABLE_CELL);
            if(owner->kind==SALTS_MD_BLOCK_TABLE_CELL) {
                VERIFY(owner->parent_index<node->parent_index);
                const salts_md_block *row=&v.document.blocks[owner->parent_index];
                VERIFY(row->kind==SALTS_MD_BLOCK_TABLE_ROW);
                ++cell_links;
            } else VERIFY(owner->parent_index==0u);
            const char *meta=NULL;size_t size=SIZE_MAX;
            VERIFY(salts_md_rich_document_metadata(
                &v,i,&meta,&size)==SALTS_MD_OK);
            VERIFY(meta && size==4u && !memcmp(meta,"/uri",4u));
            ++link_count;
        }
        VERIFY(link_count==cases[k].links);
        VERIFY(cell_links==cases[k].table_links);
        const size_t expected=strlen(cases[k].html);
        char html[1024];memset(html,0x6a,sizeof(html));
        size_t required=SIZE_MAX;
        VERIFY(expected+2u<sizeof(html));
        VERIFY(salts_md_render_html(&v,256u,NULL,0u,&required)==SALTS_MD_OK);
        VERIFY(required==expected);
        size_t reported=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,256u,html,expected,&reported)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(reported==expected);
        for(size_t i=0u;i<sizeof(html);++i)
            VERIFY((unsigned char)html[i]==0x6au);
        VERIFY(salts_md_render_html(&v,256u,html,expected+1u,&reported)==
               SALTS_MD_OK);
        VERIFY(reported==expected);
        VERIFY(!memcmp(html,cases[k].html,expected+1u));
        VERIFY((unsigned char)html[expected+1u]==0x6au);
        salts_md_document_destroy(doc);
        doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* A 129-row one-column table with fake [key]: /uri rows must not consume
 * any of the 128 parse-local ROOT reference slots. Prove both the native
 * table row ancestry and the independent output capacity failure atomicity. */
static int test_table_reference_capacity_isolation(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    char source[8192];
    const char prefix[]="| Key |\n| --- |\n";
    size_t used=sizeof(prefix)-1u;
    memcpy(source,prefix,used);
    for(size_t i=0u;i<129u;++i) {
        char line[40];
        const int n=snprintf(line,sizeof(line),"[k%zu]: /blocked\n",i);
        VERIFY(n>0 && (size_t)n<sizeof(line));
        VERIFY(used+(size_t)n<sizeof(source));
        memcpy(source+used,line,(size_t)n);
        used+=(size_t)n;
    }
    const char suffix[]="\n[real]: /uri\n\n[k0] [real]\n";
    VERIFY(used+sizeof(suffix)-1u<sizeof(source));
    memcpy(source+used,suffix,sizeof(suffix)-1u);
    used+=sizeof(suffix)-1u;
    VERIFY(salts_md_parse(source,used,1024u,used+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,1024u)==SALTS_MD_OK);
    VERIFY(v.raw_markdown_bytes==used);
    VERIFY(!memcmp(v.raw_markdown,source,used));
    VERIFY(kind_count(v,SALTS_MD_BLOCK_TABLE)==1u);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_TABLE_ROW)==130u);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_TABLE_CELL)==130u);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
    bool found=false;
    for(size_t i=1u;i<v.document.block_count;++i) {
        if(v.document.blocks[i].kind!=SALTS_MD_BLOCK_LINK)continue;
        const salts_md_block *node=&v.document.blocks[i];
        VERIFY(v.document.blocks[node->parent_index].kind==
               SALTS_MD_BLOCK_PARAGRAPH);
        const char *meta=NULL;size_t length=SIZE_MAX;
        VERIFY(salts_md_rich_document_metadata(
            &v,i,&meta,&length)==SALTS_MD_OK);
        VERIFY(meta && length==4u && !memcmp(meta,"/uri",4u));
        found=true;
    }
    VERIFY(found);
    char html[16384];memset(html,0x59,sizeof(html));
    size_t required=SIZE_MAX,reported=SIZE_MAX;
    VERIFY(salts_md_render_html(&v,1024u,NULL,0u,&required)==SALTS_MD_OK);
    VERIFY(required+2u<sizeof(html));
    VERIFY(salts_md_render_html(&v,1024u,html,required,&reported)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(reported==required);
    for(size_t i=0u;i<sizeof(html);++i)
        VERIFY((unsigned char)html[i]==0x59u);
    VERIFY(salts_md_render_html(&v,1024u,html,required+1u,&reported)==
           SALTS_MD_OK);
    VERIFY(reported==required);
    VERIFY(strstr(html,"<td>[k0]: /blocked</td>")!=NULL);
    VERIFY(strstr(html,"<td>[k128]: /blocked</td>")!=NULL);
    VERIFY(strstr(html,"<p>[k0] <a href=\"/uri\">real</a></p>")!=NULL);
    VERIFY(strstr(html,"href=\"/blocked\"")==NULL);
    VERIFY((unsigned char)html[required+1u]==0x59u);
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* A list-looking root GFM table HEADER is not an actual list; its body
 * may carry real forward references and fake [shadow]: data rows. A genuine
 * list-marker ROW, however, terminates the table into a root ordered list.
 * This is an independent AST/HTML/metadata gate against prepass drift. */
static int test_table_list_competition(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    typedef struct table_list_fixture {
        const char *source,*html;
        size_t rows,links,table_links,ordered;
    } table_list_fixture;
    static const table_list_fixture cases[]={
        {"- Name | Link\n| --- | --- |\n[shadow]: /blocked\n"
         "| A | [go][ref] |\n# Root\n[ref]: /uri\n\n[shadow] [go][ref]\n",
         "<table>\n<thead>\n<tr>\n<th>- Name</th>\n<th>Link</th>\n"
         "</tr>\n</thead>\n<tbody>\n<tr>\n<td>[shadow]: /blocked</td>\n"
         "<td></td>\n</tr>\n<tr>\n<td>A</td>\n"
         "<td><a href=\"/uri\">go</a></td>\n</tr>\n</tbody>\n</table>\n"
         "<h1>Root</h1>\n<p>[shadow] <a href=\"/uri\">go</a></p>\n",
         3u,2u,1u,0u},
        {"1. Item | Link\n| --- | --- |\n| A | [go][ref] |\n"
         "# Root\n[ref]: /uri\n\n[go][ref]\n",
         "<table>\n<thead>\n<tr>\n<th>1. Item</th>\n<th>Link</th>\n"
         "</tr>\n</thead>\n<tbody>\n<tr>\n<td>A</td>\n"
         "<td><a href=\"/uri\">go</a></td>\n</tr>\n</tbody>\n</table>\n"
         "<h1>Root</h1>\n<p><a href=\"/uri\">go</a></p>\n",
         2u,2u,1u,0u},
        {"| Key | Value |\n| --- | --- |\n| A | B |\n"
         "1. first\n2. second\n\n[ref]: /uri\n\n[ref]\n",
         "<table>\n<thead>\n<tr>\n<th>Key</th>\n<th>Value</th>\n"
         "</tr>\n</thead>\n<tbody>\n<tr>\n<td>A</td>\n<td>B</td>\n"
         "</tr>\n</tbody>\n</table>\n<ol>\n<li>first</li>\n"
         "<li>second</li>\n</ol>\n<p><a href=\"/uri\">ref</a></p>\n",
         2u,1u,0u,1u}
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==3u,
                   "table-versus-list owner denominator");
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        const size_t bytes=strlen(cases[k].source);
        VERIFY(salts_md_parse(cases[k].source,bytes,256u,bytes+1u,&doc)==
               SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==bytes);
        VERIFY(!memcmp(v.raw_markdown,cases[k].source,bytes));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_TABLE)==1u);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_TABLE_ROW)==cases[k].rows);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_ORDERED_LIST)==cases[k].ordered);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==cases[k].links);
        size_t cell_links=0u;
        for(size_t i=1u;i<v.document.block_count;++i) {
            const salts_md_block *node=&v.document.blocks[i];
            if(node->kind!=SALTS_MD_BLOCK_LINK)continue;
            VERIFY(node->parent_index<i);
            const salts_md_block *owner=&v.document.blocks[node->parent_index];
            VERIFY(owner->kind==SALTS_MD_BLOCK_TABLE_CELL ||
                   owner->kind==SALTS_MD_BLOCK_PARAGRAPH);
            if(owner->kind==SALTS_MD_BLOCK_TABLE_CELL) {
                const salts_md_block *row=&v.document.blocks[owner->parent_index];
                VERIFY(row->kind==SALTS_MD_BLOCK_TABLE_ROW);
                ++cell_links;
            } else VERIFY(owner->parent_index==0u);
            const char *meta=NULL;size_t size=SIZE_MAX;
            VERIFY(salts_md_rich_document_metadata(
                &v,i,&meta,&size)==SALTS_MD_OK);
            VERIFY(meta && size==4u && !memcmp(meta,"/uri",4u));
        }
        VERIFY(cell_links==cases[k].table_links);
        const size_t expected=strlen(cases[k].html);
        char html[1024];memset(html,0x51,sizeof(html));
        size_t required=SIZE_MAX;
        VERIFY(expected+2u<sizeof(html));
        VERIFY(salts_md_render_html(&v,256u,NULL,0u,&required)==SALTS_MD_OK);
        VERIFY(required==expected);
        size_t actual=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,256u,html,expected,&actual)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(actual==expected);
        for(size_t i=0u;i<sizeof(html);++i)
            VERIFY((unsigned char)html[i]==0x51u);
        VERIFY(salts_md_render_html(&v,256u,html,expected+1u,&actual)==
               SALTS_MD_OK);
        VERIFY(actual==expected);
        VERIFY(!memcmp(html,cases[k].html,expected+1u));
        VERIFY((unsigned char)html[expected+1u]==0x51u);
        salts_md_document_destroy(doc);
        doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Root [label]: must not consume the very next actual block marker as its
 * URL target. Assert exact source bytes, no borrowed LINK and byte-exact
 * safe HTML for fenced code, Setext and quoted blocks. */
static int test_split_reference_block_barriers(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    static const struct {
        const char *source,*html;
    } samples[]={
        {"[foo]:\n" TICK TICK TICK "\ncode\n" TICK TICK TICK
         "\n\n[foo]\n",
         "<p>[foo]:</p>\n<pre><code>code\n</code></pre>\n"
         "<p>[foo]</p>\n"},
        {"[foo]:\n===\n\n[foo]\n",
         "<h1>[foo]:</h1>\n<p>[foo]</p>\n"},
        {"[foo]:\n> /uri\n\n[foo]\n",
         "<p>[foo]:</p>\n<blockquote>\n<p>/uri</p>\n"
         "</blockquote>\n<p>[foo]</p>\n"}
    };
    _Static_assert(sizeof(samples)/sizeof(samples[0])==3u,
                   "split-definition block barrier cases");
    for(size_t k=0u;k<3u;++k) {
        const size_t n=strlen(samples[k].source);
        VERIFY(salts_md_parse(samples[k].source,n,128u,n+1u,&doc)==
               SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==n);
        VERIFY(!memcmp(v.raw_markdown,samples[k].source,n));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
        const size_t expected=strlen(samples[k].html);
        size_t need=SIZE_MAX,reported=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,128u,NULL,0u,&need)==SALTS_MD_OK);
        VERIFY(need==expected);
        char out[256];memset(out,0x56,sizeof(out));
        VERIFY(need+2u<sizeof(out));
        VERIFY(salts_md_render_html(&v,128u,out,need,&reported)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(reported==need);
        for(size_t i=0u;i<sizeof(out);++i)
            VERIFY((unsigned char)out[i]==0x56u);
        VERIFY(salts_md_render_html(&v,128u,out,need+1u,&reported)==
               SALTS_MD_OK);
        VERIFY(reported==need);
        VERIFY(!memcmp(out,samples[k].html,need+1u));
        VERIFY((unsigned char)out[need+1u]==0x56u);
        salts_md_document_destroy(doc);doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* The final LINK TEXT allocation of a split definition must roll back the
 * entire parse on node-budget exhaustion. No partially published RichDoc. */
static int test_split_reference_capacity_rollback(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    const char sample[]="[foo]:\n/url\n\n[foo]\n";
    const size_t n=sizeof(sample)-1u;
    VERIFY(salts_md_parse(sample,n,128u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
    const size_t nodes=v.document.block_count;
    VERIFY(nodes>=4u);
    VERIFY(v.document.blocks[nodes-1u].kind==SALTS_MD_BLOCK_TEXT);
    salts_md_document_destroy(doc);doc=NULL;
    salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
    VERIFY(salts_md_parse(sample,n,nodes-1u,n+1u,&rejected)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(rejected==NULL);
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* A third quoted definition must not be consumed by a preceding
 * two-line URL+title owner (#217). Verify all three hrefs and only the
 * first two title child metadata in the document-owned RichDoc. */
static int test_adjacent_split_title_ownership(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    const char sample[]="[foo]: /foo-url \"foo\"\n[bar]: /bar-url\n"
                        "  \"bar\"\n[baz]: /baz-url\n\n"
                        "[foo],\n[bar],\n[baz]\n";
    const char expected[]="<p><a href=\"/foo-url\" title=\"foo\">foo</a>,\n"
                          "<a href=\"/bar-url\" title=\"bar\">bar</a>,\n"
                          "<a href=\"/baz-url\">baz</a></p>\n";
    const size_t n=sizeof(sample)-1u;
    VERIFY(salts_md_parse(sample,n,256u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    VERIFY(v.raw_markdown_bytes==n);
    VERIFY(!memcmp(v.raw_markdown,sample,n));
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==3u);
    VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK_TITLE)==2u);
    const char *const urls[]={"/foo-url","/bar-url","/baz-url"};
    const char *const titles[]={"foo","bar"};
    size_t link=0u,title=0u;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        if(node->kind!=SALTS_MD_BLOCK_LINK &&
           node->kind!=SALTS_MD_BLOCK_LINK_TITLE)continue;
        VERIFY(node->parent_index<i);
        const char *meta=NULL;size_t bytes=SIZE_MAX;
        VERIFY(salts_md_rich_document_metadata(
            &v,i,&meta,&bytes)==SALTS_MD_OK);
        if(node->kind==SALTS_MD_BLOCK_LINK) {
            VERIFY(link<3u);
            VERIFY(bytes==strlen(urls[link]));
            VERIFY(meta && !memcmp(meta,urls[link],bytes));
            VERIFY(v.document.blocks[node->parent_index].kind==
                   SALTS_MD_BLOCK_PARAGRAPH);
            ++link;
        } else {
            VERIFY(title<2u);
            VERIFY(bytes==strlen(titles[title]));
            VERIFY(meta && !memcmp(meta,titles[title],bytes));
            VERIFY(v.document.blocks[node->parent_index].kind==
                   SALTS_MD_BLOCK_LINK);
            ++title;
        }
    }
    VERIFY(link==3u && title==2u);
    size_t need=SIZE_MAX,reported=SIZE_MAX;
    VERIFY(salts_md_render_html(&v,256u,NULL,0u,&need)==SALTS_MD_OK);
    VERIFY(need==sizeof(expected)-1u);
    char out[512];memset(out,0x75,sizeof(out));
    VERIFY(salts_md_render_html(&v,256u,out,need,&reported)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(reported==need);
    for(size_t i=0u;i<sizeof(out);++i)
        VERIFY((unsigned char)out[i]==0x75u);
    VERIFY(salts_md_render_html(&v,256u,out,need+1u,&reported)==
           SALTS_MD_OK);
    VERIFY(reported==need);
    VERIFY(!memcmp(out,expected,need+1u));
    VERIFY((unsigned char)out[need+1u]==0x75u);
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Unfinished or separated title lines remain normal root paragraphs.
 * They must not change the preceding LINK's optional TITLE metadata. */
static int test_split_title_rejection(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    static const char *const cases[]={
        "[foo]: /uri\n\"unterminated\n\n[foo]\n",
        "[foo]: /uri\n- item\n\n[foo]\n",
        "[foo]: /uri\n\n\"title\"\n\n[foo]\n"
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==3u,
                   "root title-negative control count");
    for(size_t k=0u;k<3u;++k) {
        const size_t n=strlen(cases[k]);
        VERIFY(salts_md_parse(cases[k],n,128u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==n);
        VERIFY(!memcmp(v.raw_markdown,cases[k],n));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK_TITLE)==0u);
        salts_md_document_destroy(doc);doc=NULL;
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* The final owned LINK TEXT child of a split-title definition is an
 * exact node-budget failure point, never a partially returned RichDoc.
 * Test the angle-URL and the three adjacent root-definition path. */
static int test_split_title_last_text_budget(void) {
    int failure=0;
    salts_md_document *doc=NULL;
    static const char *const sources[]={
        "[Foo bar]:\n<my url>\n'title'\n\n[Foo bar]\n",
        "[foo]: /foo-url \"foo\"\n[bar]: /bar-url\n"
        "  \"bar\"\n[baz]: /baz-url\n\n[foo],\n[bar],\n[baz]\n"
    };
    _Static_assert(sizeof(sources)/sizeof(sources[0])==2u,
                   "final split-title LINK TEXT budget cases");
    for(size_t k=0u;k<2u;++k) {
        const size_t n=strlen(sources[k]);
        VERIFY(salts_md_parse(sources[k],n,128u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==n);
        VERIFY(!memcmp(v.raw_markdown,sources[k],n));
        const size_t count=v.document.block_count;
        VERIFY(count>3u);
        VERIFY(v.document.blocks[count-1u].kind==SALTS_MD_BLOCK_TEXT);
        const size_t parent=v.document.blocks[count-1u].parent_index;
        VERIFY(parent<count-1u);
        VERIFY(v.document.blocks[parent].kind==SALTS_MD_BLOCK_LINK);
        salts_md_document_destroy(doc);doc=NULL;
        salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
        VERIFY(salts_md_parse(sources[k],n,count-1u,n+1u,&rejected)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(rejected==NULL);
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Canonical nested visible labels and nested active LINK prohibition.
 * Both official negative examples must create two sibling/paragraph-owned
 * links, never a LINK descendant of another LINK. Keep independent HTML,
 * ownership and output-buffer rollback checks. */
static int test_nested_link_precedence(void) {
    static const struct {
        const char *source,*expected;
        size_t expected_links;
    } cases[]={
        {"[foo [bar](/uri)][ref]\n\n[ref]: /uri\n",
         "<p>[foo <a href=\"/uri\">bar</a>]<a href=\"/uri\">ref</a></p>\n",2u},
        {"[foo *bar [baz][ref]*][ref]\n\n[ref]: /uri\n",
         "<p>[foo <em>bar <a href=\"/uri\">baz</a></em>]<a href=\"/uri\">ref</a></p>\n",2u},
        {"[foo [bar](/uri)](/uri)\n",
         "<p>[foo <a href=\"/uri\">bar</a>](/uri)</p>\n",1u},
        {"[foo *[bar [baz](/uri)](/uri)*](/uri)\n",
         "<p>[foo <em>[bar <a href=\"/uri\">baz</a>](/uri)</em>](/uri)</p>\n",1u},
        {"[x <https://example.com/path>](/outer)\n",
         "<p>[x <a href=\"https://example.com/path\">https://example.com/path</a>](/outer)</p>\n",1u}
    };
    int failure=0;salts_md_document *doc=NULL;
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        const char *input=cases[k].source;
        const size_t len=strlen(input);
        VERIFY(salts_md_parse(input,len,256u,len+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==cases[k].expected_links);
        for(size_t i=1u;i<v.document.block_count;++i) {
            if(v.document.blocks[i].kind!=SALTS_MD_BLOCK_LINK)continue;
            uint32_t owner=v.document.blocks[i].parent_index;
            while(owner) {
                VERIFY(v.document.blocks[owner].kind!=SALTS_MD_BLOCK_LINK);
                owner=v.document.blocks[owner].parent_index;
            }
        }
        const size_t bytes=strlen(cases[k].expected);
        size_t required=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,256u,NULL,0u,&required)==SALTS_MD_OK);
        VERIFY(required==bytes);
        char output[256];memset(output,0x57,sizeof(output));
        VERIFY(salts_md_render_html(&v,256u,output,required,&required)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(required==bytes);
        for(size_t i=0u;i<sizeof(output);++i)
            VERIFY((unsigned char)output[i]==0x57u);
        VERIFY(salts_md_render_html(&v,256u,output,bytes+1u,&required)==
               SALTS_MD_OK);
        VERIFY(required==bytes &&
               !memcmp(output,cases[k].expected,bytes+1u));
        const size_t nodes=v.document.block_count;
        salts_md_document_destroy(doc);doc=NULL;
        salts_md_document *rejected=NULL;
        VERIFY(salts_md_parse(input,len,nodes-1u,len+1u,&rejected)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(rejected==NULL);
    }
done:
    salts_md_document_destroy(doc);
    return failure;
}

/* Complete quoted HTML tags are opaque only to Markdown delimiters.
 * The production safe renderer MUST still escape raw markup, including
 * potentially unsafe attributes; no RawHTML nodes or nested LINKs. */
static int test_html_tag_delimiter_opacity(void) {
    static const struct {
        const char *source,*expected;
        size_t links;
    } cases[]={
        {"[foo <bar attr=\"](baz)\">\n",
         "<p>[foo &lt;bar attr=&quot;](baz)&quot;&gt;</p>\n",0u}, /* #524 */
        {"[foo <bar attr=\"][ref]\">\n\n[ref]: /uri\n",
         "<p>[foo &lt;bar attr=&quot;][ref]&quot;&gt;</p>\n",0u}, /* #536 */
        {"[foo <bar attr=\"[ref]\">](/outer)\n\n[ref]: /uri\n",
         "<p><a href=\"/outer\">foo &lt;bar attr=&quot;[ref]&quot;&gt;</a></p>\n",1u},
        {"[foo <bar attr=\"](baz)\n",
         "<p><a href=\"baz\">foo &lt;bar attr=&quot;</a></p>\n",1u},
        {"[foo <bar attr='](baz)'>\n",
         "<p>[foo &lt;bar attr='](baz)'&gt;</p>\n",0u}
    };
    int failure=0;salts_md_document *doc=NULL,*rejected=NULL;
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        const size_t n=strlen(cases[k].source);
        VERIFY(salts_md_parse(cases[k].source,n,256u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(v.raw_markdown_bytes==n &&
               !memcmp(v.raw_markdown,cases[k].source,n));
        VERIFY(kind_count(v,SALTS_MD_BLOCK_LINK)==cases[k].links);
        VERIFY(kind_count(v,SALTS_MD_BLOCK_IMAGE)==0u);
        const size_t expected=strlen(cases[k].expected);
        size_t required=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,256u,NULL,0u,&required)==SALTS_MD_OK);
        VERIFY(required==expected);
        char out[256];memset(out,0x43,sizeof(out));
        VERIFY(required+2u<sizeof(out));
        VERIFY(salts_md_render_html(&v,256u,out,required,&required)==
               SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(required==expected);
        for(size_t i=0u;i<sizeof(out);++i)
            VERIFY((unsigned char)out[i]==0x43u);
        VERIFY(salts_md_render_html(&v,256u,out,expected+1u,&required)==
               SALTS_MD_OK);
        VERIFY(required==expected &&
               !memcmp(out,cases[k].expected,expected+1u));
        const size_t nodes=v.document.block_count;
        VERIFY(nodes>1u);
        salts_md_document_destroy(doc);doc=NULL;
        VERIFY(salts_md_parse(cases[k].source,n,nodes-1u,n+1u,
                             &rejected)==SALTS_MD_CAPACITY_EXCEEDED);
        VERIFY(rejected==NULL);
    }
done:
    salts_md_document_destroy(rejected);
    salts_md_document_destroy(doc);
    return failure;
}

int main(void) {
    static const reference_fixture cases[]={
        {"[Foo bar]:\n<my url>\n'title'\n\n[Foo bar]\n",
         "my url","title","Foo bar",
         "<p><a href=\"my%20url\" title=\"title\">Foo bar</a></p>\n"},
        {"[foo]: /uri\r\n  \"A &amp; B\"\r\n\r\n[foo]\r\n",
         "/uri","A & B","foo",
         "<p><a href=\"/uri\" title=\"A &amp; B\">foo</a></p>\n"},
        {"[foo]: /uri\n   (title)\n\n[foo]\n",
         "/uri","title","foo",
         "<p><a href=\"/uri\" title=\"title\">foo</a></p>\n"},
        {"[foo]: /uri\n\"title\"\n\n[foo]\n",
         "/uri","title","foo",
         "<p><a href=\"/uri\" title=\"title\">foo</a></p>\n"},
        {"[foo]:\n/url\n\n[foo]\n",
         "/url",NULL,"foo",
         "<p><a href=\"/url\">foo</a></p>\n"}, /* CommonMark #198 */
        {"[foo]:  \r\n  /uri \"A &amp; B\"\r\n\r\n[foo]\r\n",
         "/uri","A & B","foo",
         "<p><a href=\"/uri\" title=\"A &amp; B\">foo</a></p>\n"},
        {"[a][ref]\n\n[ref]:\n/uri\n",
         "/uri",NULL,"a",
         "<p><a href=\"/uri\">a</a></p>\n"},

        {"[foo<https://example.com/?search=](uri)>\n",
         "https://example.com/?search=](uri)",NULL,
         "https://example.com/?search=](uri)",
         "<p>[foo<a href=\"https://example.com/?search=%5D(uri)\">https://example.com/?search=](uri)</a></p>\n"}, /* #526 */
        {"[foo<https://example.com/?search=][ref]>\n\n[ref]: /uri\n",
         "https://example.com/?search=][ref]",NULL,
         "https://example.com/?search=][ref]",
         "<p>[foo<a href=\"https://example.com/?search=%5D%5Bref%5D\">https://example.com/?search=][ref]</a></p>\n"}, /* #538 */
        {"[link [foo [bar]]][ref]\n\n[ref]: /uri\n",
         "/uri",NULL,"link [foo [bar]]",
         "<p><a href=\"/uri\">link [foo [bar]]</a></p>\n"}, /* #528 */
        {"[foo][bar]\n\n[bar]: /url \"title\"\n",
         "/url","title","foo",
         "<p><a href=\"/url\" title=\"title\">foo</a></p>\n"}, /* #527 */
        {"[foo][BaR]\n\n[bar]: /url \"title\"\n",
         "/url","title","foo",
         "<p><a href=\"/url\" title=\"title\">foo</a></p>\n"}, /* #539 */
        {"[foo]: /url1\n\n[foo]: /url2\n\n[bar][foo]\n",
         "/url1",NULL,"bar",
         "<p><a href=\"/url1\">bar</a></p>\n"}, /* #544 */
        {"[foo][]\n\n[foo]: /url \"title\"\n",
         "/url","title","foo",
         "<p><a href=\"/url\" title=\"title\">foo</a></p>\n"}, /* #553 */
        {"[Foo][]\n\n[foo]: /url \"title\"\n",
         "/url","title","Foo",
         "<p><a href=\"/url\" title=\"title\">Foo</a></p>\n"}, /* #555 */
        {"[foo]\n\n[foo]: /url \"title\"\n",
         "/url","title","foo",
         "<p><a href=\"/url\" title=\"title\">foo</a></p>\n"}, /* #557 */
        {"[Foo]\n\n[foo]: /url \"title\"\n",
         "/url","title","Foo",
         "<p><a href=\"/url\" title=\"title\">Foo</a></p>\n"}, /* #561 */
        {"[foo] bar\n\n[foo]: /url\n",
         "/url",NULL,"foo",
         "<p><a href=\"/url\">foo</a> bar</p>\n"}, /* #562 */
        {"[foo][bar]\n\n[foo]: /url1\n[bar]: /url2\n",
         "/url2",NULL,"foo",
         "<p><a href=\"/url2\">foo</a></p>\n"}, /* #565 */
        {"[foo][]\n\n[foo]: /url1\n",
         "/url1",NULL,"foo",
         "<p><a href=\"/url1\">foo</a></p>\n"}, /* #566 */
        {"[x][id]\n\n[id]: /f&ouml; \"A &copy;\"\n",
         "/fö","A ©","x",
         "<p><a href=\"/f%C3%B6\" title=\"A ©\">x</a></p>\n"},
        {"Setext\n===\n[key]: /root\n\n[key]\n",
         "/root",NULL,"key",
         "<h1>Setext</h1>\n<p><a href=\"/root\">key</a></p>\n"},
        {"Setext\n---\n[key]: /root\n\n[key]\n",
         "/root",NULL,"key",
         "<h2>Setext</h2>\n<p><a href=\"/root\">key</a></p>\n"},
        {"Setext\r\n===\r\n[key]: /root\r\n\r\n[key]\r\n",
         "/root",NULL,"key",
         "<h1>Setext</h1>\n<p><a href=\"/root\">key</a></p>\n"},
        {"* * *\n[key]: /root\n\n[key]\n",
         "/root",NULL,"key",
         "<hr />\n<p><a href=\"/root\">key</a></p>\n"},
        {"- - -\n[key]: /root\n\n[key]\n",
         "/root",NULL,"key",
         "<hr />\n<p><a href=\"/root\">key</a></p>\n"},
        {"  * * *\r\n[key]: /root\r\n\r\n[key]\r\n",
         "/root",NULL,"key",
         "<hr />\n<p><a href=\"/root\">key</a></p>\n"},
        {"Root\n2. text\n===\n[id]: /uri\n\n[id]\n",
         "/uri",NULL,"id",
         "<h1>Root\n2. text</h1>\n<p><a href=\"/uri\">id</a></p>\n"},
        {"Root\n+\n===\n[id]: /uri\n\n[id]\n",
         "/uri",NULL,"id",
         "<h1>Root\n+</h1>\n<p><a href=\"/uri\">id</a></p>\n"},
        {"Root\n-\n[id]: /uri\n\n[id]\n",
         "/uri",NULL,"id",
         "<h2>Root</h2>\n<p><a href=\"/uri\">id</a></p>\n"},
        {"Root\r\n2. text\r\n===\r\n[id]: /uri\r\n\r\n[id]\r\n",
         "/uri",NULL,"id",
         "<h1>Root\n2. text</h1>\n<p><a href=\"/uri\">id</a></p>\n"}
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==31u,
                   "reference AST/HTML fixture denominator");
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(test_reference(&cases[i]))return 1;
    if(test_html_tag_delimiter_opacity() ||
       test_nested_link_precedence() || test_unsafe() || test_fence() ||
       test_container_definition_fences() ||
       test_list_blank_scope() || test_list_capacity_isolation() ||
       test_root_rule_owner_release() ||
       test_root_atx_owner_release() ||
       test_root_fence_owner_release() ||
       test_fence_definition_opacity() ||
       test_table_reference_owner() ||
       test_table_reference_capacity_isolation() ||
       test_table_list_competition() ||
       test_split_reference_block_barriers() ||
       test_split_reference_capacity_rollback() ||
       test_adjacent_split_title_ownership() ||
       test_split_title_rejection() ||
       test_split_title_last_text_budget())return 1;
    const char limited[]="[foo][bar]\n\n[bar]: /url \"title\"\n";
    salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(limited,sizeof(limited)-1u,3u,
                     sizeof(limited),&rejected)!=SALTS_MD_CAPACITY_EXCEEDED ||
       rejected!=NULL) {
        fprintf(stderr,"node budget must reject partial reference\n");
        return 1;
    }
    char many[4096];size_t used=0u;
    for(size_t i=0u;i<129u;++i) {
        char line[32];
        const int len=snprintf(line,sizeof(line),"[k%zu]: /url\n",i);
        if(len<=0 || (size_t)len>=sizeof(line) ||
           used+(size_t)len>=sizeof(many))return 1;
        memcpy(many+used,line,(size_t)len);
        used+=(size_t)len;
    }
    rejected=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(many,used,256u,used+1u,&rejected)!=
       SALTS_MD_CAPACITY_EXCEEDED || rejected!=NULL) {
        fprintf(stderr,"reference admission must be bounded\n");
        return 1;
    }
    puts("C11 root references: 31/31 AST/HTML, forward/duplicate/case "
         "binding, safe URL, root-rule, ATX, fence and table ownership");
    return 0;
}
