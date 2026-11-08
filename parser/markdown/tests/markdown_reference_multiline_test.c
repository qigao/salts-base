#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Official CommonMark 0.31.2 examples retain their exact Markdown/HTML.
 * https://spec.commonmark.org/0.31.2/#link-reference-definitions
 * Spec fixtures are CC BY-SA 4.0 (John MacFarlane and contributors).
 * No runtime expected output is obtained from the implementation under test. */
typedef struct reference_case {
    const char *name,*source,*html,*href,*title,*visible;
} reference_case;

#define VERIFY(expr) do { if(!(expr)) { \
    fprintf(stderr,"%s:%d: %s: %s\n",__FILE__,__LINE__,name,#expr); \
    failure=1;goto done; } } while(0)

static size_t count_kind(salts_md_rich_document_view v,salts_md_block_kind kind) {
    size_t count=0u;
    for(size_t i=0u;i<v.document.block_count;++i)
        count+=(v.document.blocks[i].kind==kind);
    return count;
}

static int check_case(const reference_case *f) {
    const char *name=f->name;
    int failure=0;
    salts_md_document *doc=NULL,*rejected=NULL;
    char *input=NULL;
    const size_t n=strlen(f->source);
    input=(char*)malloc(n+1u);VERIFY(input!=NULL);
    memcpy(input,f->source,n+1u);
    VERIFY(salts_md_parse(input,n,256u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    /* All syntax, text and metadata must survive mutation/free of input. */
    memset(input,0x6f,n);free(input);input=NULL;
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    VERIFY(v.raw_markdown_bytes==n && !memcmp(v.raw_markdown,f->source,n));
    VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK)==(f->href?1u:0u));
    VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK_TITLE)==(f->title?1u:0u));
    char visible[256];size_t used=0u;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        VERIFY(node->parent_index<i);
        const salts_md_block *parent=&v.document.blocks[node->parent_index];
        if(node->kind==SALTS_MD_BLOCK_LINK ||
           node->kind==SALTS_MD_BLOCK_LINK_TITLE) {
            const char *meta=NULL;size_t bytes=SIZE_MAX;
            const char *expected=node->kind==SALTS_MD_BLOCK_LINK?
                                 f->href:f->title;
            VERIFY(expected!=NULL);
            VERIFY(salts_md_rich_document_metadata(&v,i,&meta,&bytes)==SALTS_MD_OK);
            VERIFY(bytes==strlen(expected));
            VERIFY(!bytes || (meta && !memcmp(meta,expected,bytes)));
            if(node->kind==SALTS_MD_BLOCK_LINK_TITLE)
                VERIFY(parent->kind==SALTS_MD_BLOCK_LINK && node->parent_index+1u==i);
            else VERIFY(parent->kind==SALTS_MD_BLOCK_PARAGRAPH ||
                        parent->kind==SALTS_MD_BLOCK_STRONG);
        }
        if(node->kind==SALTS_MD_BLOCK_TEXT) {
            /* Visible link text includes owned emphasis descendants. It is
             * not necessarily a direct TEXT child of LINK (#554/#558). */
            uint32_t owner=node->parent_index;
            while(owner && v.document.blocks[owner].kind!=SALTS_MD_BLOCK_LINK)
                owner=v.document.blocks[owner].parent_index;
            if(owner) {
                const size_t bytes=node->byte_end-node->byte_begin;
                VERIFY(node->byte_end<=v.document.source_bytes);
                VERIFY(bytes<=sizeof(visible)-used);
                memcpy(visible+used,v.document.source+node->byte_begin,bytes);
                used+=bytes;
            }
        }
    }
    if(f->visible) {
        VERIFY(used==strlen(f->visible));
        VERIFY(!memcmp(visible,f->visible,used));
    }
    size_t required=SIZE_MAX,actual=SIZE_MAX;
    VERIFY(salts_md_render_html(&v,256u,NULL,0u,&required)==SALTS_MD_OK);
    VERIFY(required==strlen(f->html));
    char output[1024];VERIFY(required+2u<sizeof(output));
    memset(output,0x53,sizeof(output));
    VERIFY(salts_md_render_html(&v,256u,output,required,&actual)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(actual==required);
    for(size_t i=0u;i<sizeof(output);++i)VERIFY((unsigned char)output[i]==0x53u);
    VERIFY(salts_md_render_html(&v,256u,output,required+1u,&actual)==SALTS_MD_OK);
    VERIFY(actual==required && !memcmp(output,f->html,required+1u));
    VERIFY((unsigned char)output[required+1u]==0x53u);
    /* Exact node limit succeeds; one node short never publishes a document. */
    const size_t nodes=v.document.block_count;
    VERIFY(nodes>1u);
    salts_md_document_destroy(doc);doc=NULL;
    VERIFY(salts_md_parse(f->source,n,nodes,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    VERIFY(salts_md_parse(f->source,n,nodes-1u,n+1u,&rejected)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(rejected==NULL);
done:
    free(input);salts_md_document_destroy(rejected);salts_md_document_destroy(doc);
    return failure;
}

static int check_safety(void) {
    const char *name="title-vs-href-security";
    int failure=0;salts_md_document *doc=NULL;
    const char *const rejected[]={
        "[x]: java&#x73;cript:alert 'a\nb'\n\n[x]\n",
        "[x]: /safe&#10;bad 'a\nb'\n\n[x]\n",
        "[x]: /safe 'a\n&#1;b'\n\n[x]\n",
        "[x]: /safe 'a\n&#13;b'\n\n[x]\n",
        "[x]: /safe 'a\n&#127;b'\n\n[x]\n"
    };
    for(size_t k=0u;k<sizeof(rejected)/sizeof(rejected[0]);++k) {
        const size_t n=strlen(rejected[k]);
        VERIFY(salts_md_parse(rejected[k],n,128u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK)==1u);
        char output[256];memset(output,0x68,sizeof(output));size_t need=SIZE_MAX;
        VERIFY(salts_md_render_html(&v,128u,output,sizeof(output),&need)==
               SALTS_MD_INVALID_ARGUMENT);
        for(size_t i=0u;i<sizeof(output);++i)
            VERIFY((unsigned char)output[i]==0x68u);
        salts_md_document_destroy(doc);doc=NULL;
    }
done:
    salts_md_document_destroy(doc);return failure;
}

static int check_budgets(void) {
    const char *name="bounded-title-and-reference-capacity";
    int failure=0;salts_md_document *doc=NULL;
    char source[8192];size_t n=0u;
    /* 64 title physical lines, including opener and closer, are accepted. */
    const char start[]="[foo]: /url 'start\n";
    memcpy(source,start,sizeof(start)-1u);n=sizeof(start)-1u;
    for(size_t i=0u;i<62u;++i) {memcpy(source+n,"x\n",2u);n+=2u;}
    memcpy(source+n,"'\n\n[foo]\n",9u);n+=9u;
    VERIFY(salts_md_parse(source,n,256u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    VERIFY(count_kind(salts_md_get_document_view(doc),SALTS_MD_BLOCK_LINK_TITLE)==1u);
    salts_md_document_destroy(doc);doc=NULL;
    /* One additional title line is a resource error, not a literal fallback. */
    n=sizeof(start)-1u;
    for(size_t i=0u;i<63u;++i) {memcpy(source+n,"x\n",2u);n+=2u;}
    memcpy(source+n,"'\n\n[foo]\n",9u);n+=9u;
    VERIFY(salts_md_parse(source,n,256u,n+1u,&doc)==SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(doc==NULL);
    /* The last of 129 multiline definitions must atomically exhaust the
     * existing 128-entry map; no separate continuation registry exists. */
    n=0u;
    for(size_t i=0u;i<129u;++i) {
        const int wrote=snprintf(source+n,sizeof(source)-n,
                                 "[r%zu]: /url 'a\nb'\n",i);
        VERIFY(wrote>0 && (size_t)wrote<sizeof(source)-n);n+=(size_t)wrote;
    }
    VERIFY(salts_md_parse(source,n,1024u,n+1u,&doc)==SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(doc==NULL);
done:
    salts_md_document_destroy(doc);return failure;
}

static int check_label_budget(void) {
    const char *name="escaped-label-exact-byte-budget";
    int failure=0;salts_md_document *doc=NULL;
    char label[130],visible[130],source[512],html[512];
    memset(label,'a',126u);label[126]='\\';label[127]=']';label[128]='\0';
    memset(visible,'a',126u);visible[126]=']';visible[127]='\0';
    int n=snprintf(source,sizeof(source),"[%s]: /uri\n\n[%s]\n",label,label);
    VERIFY(n>0 && (size_t)n<sizeof(source));
    int h=snprintf(html,sizeof(html),"<p><a href=\"/uri\">%s</a></p>\n",visible);
    VERIFY(h>0 && (size_t)h<sizeof(html));
    const reference_case exact={name,source,html,"/uri",NULL,visible};
    VERIFY(check_case(&exact)==0);
    /* The fixed raw-key ceiling includes both bytes of an escape. A
     * 129-byte label remains unsupported literal syntax, never truncated
     * to impersonate an admitted 128-byte key. */
    memset(label,'a',127u);label[127]='\\';label[128]=']';label[129]='\0';
    n=snprintf(source,sizeof(source),"[%s]: /uri\n\n[%s]\n",label,label);
    VERIFY(n>0 && (size_t)n<sizeof(source));
    VERIFY(salts_md_parse(source,(size_t)n,256u,(size_t)n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK)==0u);
    VERIFY(v.raw_markdown_bytes==(size_t)n && !memcmp(v.raw_markdown,source,(size_t)n));
done:
    salts_md_document_destroy(doc);return failure;
}

static int check_adjacent_owners(void) {
    const char *name="multiline-title-adjacent-definition-owner";
    int failure=0;salts_md_document *doc=NULL;
    const char source[]="[foo]: /first 'a\n[shadow]: /bad\nb'\n"
        "[bar]: /second\n    'second'\n\n[foo] [bar] [shadow]\n";
    const char html[]="<p><a href=\"/first\" title=\"a\n[shadow]: /bad\nb\">foo</a> "
        "<a href=\"/second\" title=\"second\">bar</a> [shadow]</p>\n";
    const char *const urls[]={"/first","/second"};
    const char *const titles[]={"a\n[shadow]: /bad\nb","second"};
    const size_t n=sizeof(source)-1u;
    VERIFY(salts_md_parse(source,n,256u,n+1u,&doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    VERIFY(v.raw_markdown_bytes==n && !memcmp(v.raw_markdown,source,n));
    VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK)==2u);
    VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK_TITLE)==2u);
    VERIFY(count_kind(v,SALTS_MD_BLOCK_CODE_INDENTED)==0u);
    size_t link=0u,title=0u;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        const char *meta=NULL;size_t bytes=0u;
        if(node->kind!=SALTS_MD_BLOCK_LINK && node->kind!=SALTS_MD_BLOCK_LINK_TITLE)
            continue;
        VERIFY(node->parent_index<i);
        VERIFY(salts_md_rich_document_metadata(&v,i,&meta,&bytes)==SALTS_MD_OK);
        const char *expected=NULL;
        if(node->kind==SALTS_MD_BLOCK_LINK) {
            VERIFY(link<2u);expected=urls[link++];
            VERIFY(v.document.blocks[node->parent_index].kind==SALTS_MD_BLOCK_PARAGRAPH);
        } else {
            VERIFY(title<2u);expected=titles[title++];
            VERIFY(v.document.blocks[node->parent_index].kind==SALTS_MD_BLOCK_LINK);
        }
        VERIFY(bytes==strlen(expected) && meta && !memcmp(meta,expected,bytes));
    }
    size_t required=SIZE_MAX;char output[512];memset(output,0x49,sizeof(output));
    VERIFY(salts_md_render_html(&v,256u,output,sizeof(html)-1u,&required)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(required==sizeof(html)-1u);
    for(size_t i=0u;i<sizeof(output);++i)VERIFY((unsigned char)output[i]==0x49u);
    VERIFY(salts_md_render_html(&v,256u,output,sizeof(output),&required)==SALTS_MD_OK);
    VERIFY(required==sizeof(html)-1u && !memcmp(output,html,sizeof(html)));
done:
    salts_md_document_destroy(doc);return failure;
}


/* All nine newly matching IDs are pinned by the permanent official gate.
 * These three additionally prove typed delimiter ownership, independently
 * of rendered HTML: emphasis stays INSIDE LINK and an outside '*' cannot
 * pair with a literal trailing '*' that belongs to a reference label. */
static const reference_case qualified_inline_cases[]={
    {"official-554", "[*foo* bar][]\n\n[*foo* bar]: /url \"title\"\n",
     "<p><a href=\"/url\" title=\"title\"><em>foo</em> bar</a></p>\n",
     "/url", "title", "foo bar"},
    {"official-558", "[*foo* bar]\n\n[*foo* bar]: /url \"title\"\n",
     "<p><a href=\"/url\" title=\"title\"><em>foo</em> bar</a></p>\n",
     "/url", "title", "foo bar"},
    {"official-564", "[foo*]: /url\n\n*[foo*]\n",
     "<p>*<a href=\"/url\">foo*</a></p>\n",
     "/url", NULL, "foo*"}
};

static int check_inline_label_owners(void) {
    const char *name="qualified-reference-inline-owners";
    int failure=0;salts_md_document *doc=NULL;
    _Static_assert(sizeof(qualified_inline_cases)/sizeof(qualified_inline_cases[0])==3u,
                   "qualified inline reference denominator");
    for(size_t k=0u;k<3u;++k) {
        const reference_case *f=&qualified_inline_cases[k];
        name=f->name;
        VERIFY(check_case(f)==0);
        const size_t n=strlen(f->source);
        VERIFY(salts_md_parse(f->source,n,256u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        VERIFY(count_kind(v,SALTS_MD_BLOCK_EMPHASIS)==(k<2u?1u:0u));
        VERIFY(count_kind(v,SALTS_MD_BLOCK_STRONG)==0u);
        size_t emphasized=0u,inside=0u,outside=0u;
        for(size_t i=1u;i<v.document.block_count;++i) {
            const salts_md_block *node=&v.document.blocks[i];
            VERIFY(node->parent_index<i);
            const salts_md_block *parent=&v.document.blocks[node->parent_index];
            if(node->kind==SALTS_MD_BLOCK_EMPHASIS)
                VERIFY(parent->kind==SALTS_MD_BLOCK_LINK);
            if(node->kind!=SALTS_MD_BLOCK_TEXT)continue;
            const size_t bytes=node->byte_end-node->byte_begin;
            const char *text=v.document.source+node->byte_begin;
            if(parent->kind==SALTS_MD_BLOCK_EMPHASIS) {
                VERIFY(k<2u && bytes==3u && !memcmp(text,"foo",3u));
                ++emphasized;
            } else if(parent->kind==SALTS_MD_BLOCK_LINK) {
                const char *expected=k<2u?" bar":"foo*";
                VERIFY(bytes==4u && !memcmp(text,expected,4u));
                ++inside;
            } else {
                VERIFY(k==2u && parent->kind==SALTS_MD_BLOCK_PARAGRAPH);
                VERIFY(bytes==1u && text[0]=='*');
                ++outside;
            }
        }
        VERIFY(emphasized==(k<2u?1u:0u) && inside==1u);
        VERIFY(outside==(k==2u?1u:0u));
        salts_md_document_destroy(doc);doc=NULL;
    }
done:
    salts_md_document_destroy(doc);return failure;
}

/* Concurrently live snapshots with the SAME normalized raw key must not
 * borrow one another's table or scratch. A failed intervening parse and a
 * later undefined reference also must not donate a binding to either one. */
static int check_document_binding_isolation(void) {
    const char *name="reference-document-binding-isolation";
    int failure=0;
    salts_md_document *first=NULL,*second=NULL,*rejected=NULL,*undefined=NULL;
    const char one[]="[A\\!]: /one 'first\nline'\n\n[A\\!]\n";
    const char two[]="[a\\!]: /two 'second'\n\n[a\\!]\n";
    const char missing[]="[A\\!]\n";
    const char expected_one[]="<p><a href=\"/one\" title=\"first\nline\">A!</a></p>\n";
    const char expected_two[]="<p><a href=\"/two\" title=\"second\">a!</a></p>\n";
    VERIFY(salts_md_parse(one,sizeof(one)-1u,128u,sizeof(one),&first)==SALTS_MD_OK);
    VERIFY(first!=NULL);
    VERIFY(salts_md_parse(two,sizeof(two)-1u,128u,sizeof(two),&second)==SALTS_MD_OK);
    VERIFY(second!=NULL);
    VERIFY(salts_md_parse(two,sizeof(two)-1u,2u,sizeof(two),&rejected)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(rejected==NULL);
    const salts_md_rich_document_view a=salts_md_get_document_view(first);
    const salts_md_rich_document_view b=salts_md_get_document_view(second);
    VERIFY(salts_md_rich_document_validate(&a,128u)==SALTS_MD_OK);
    VERIFY(salts_md_rich_document_validate(&b,128u)==SALTS_MD_OK);
    char output[256];size_t required=SIZE_MAX;
    VERIFY(salts_md_render_html(&b,128u,output,sizeof(output),&required)==SALTS_MD_OK);
    VERIFY(required==sizeof(expected_two)-1u &&
           !memcmp(output,expected_two,sizeof(expected_two)));
    salts_md_document_destroy(second);second=NULL;
    VERIFY(salts_md_parse(missing,sizeof(missing)-1u,128u,sizeof(missing),
                        &undefined)==SALTS_MD_OK);
    VERIFY(undefined!=NULL);
    const salts_md_rich_document_view u=salts_md_get_document_view(undefined);
    VERIFY(salts_md_rich_document_validate(&u,128u)==SALTS_MD_OK);
    VERIFY(count_kind(u,SALTS_MD_BLOCK_LINK)==0u);
    VERIFY(salts_md_render_html(&u,128u,output,sizeof(output),&required)==SALTS_MD_OK);
    VERIFY(required==sizeof("<p>[A!]</p>\n")-1u &&
           !memcmp(output,"<p>[A!]</p>\n",required+1u));
    salts_md_document_destroy(undefined);undefined=NULL;
    /* Original borrowed view remains valid after all other owners drain. */
    memset(output,0x67,sizeof(output));
    VERIFY(salts_md_render_html(&a,128u,output,sizeof(expected_one)-1u,&required)==
           SALTS_MD_CAPACITY_EXCEEDED);
    VERIFY(required==sizeof(expected_one)-1u);
    for(size_t i=0u;i<sizeof(output);++i)VERIFY((unsigned char)output[i]==0x67u);
    VERIFY(salts_md_render_html(&a,128u,output,sizeof(output),&required)==SALTS_MD_OK);
    VERIFY(required==sizeof(expected_one)-1u &&
           !memcmp(output,expected_one,sizeof(expected_one)));
    VERIFY(a.raw_markdown_bytes==sizeof(one)-1u &&
           !memcmp(a.raw_markdown,one,sizeof(one)-1u));
done:
    salts_md_document_destroy(undefined);salts_md_document_destroy(rejected);
    salts_md_document_destroy(second);salts_md_document_destroy(first);
    return failure;
}

/* Candidate two-line labels may never borrow blank lines, competing root
 * containers, indented code or an over-budget key. A rejected definition
 * must leave later ordinary text and unrelated bindings untouched. */
static int check_root_label_boundary_rejections(void) {
    const char *name="root-multiline-label-negative-admission";
    static const char *const inputs[]={
        "[Foo\n\nbar]: /url\n\n[Baz][Foo bar]\n",
        "[Foo\n> bar]: /url\n\n[Baz][Foo bar]\n",
        "[Foo\n    bar]: /url\n\n[Baz][Foo bar]\n",
        "[Foo\n[bar]: /url\n\n[Baz][Foo bar]\n",
        "[\nfoo\n\n]: /uri\n\n[x][foo]\n",
        "[\n> foo\n]: /uri\n\n[x][foo]\n",
        "[\nfoo\n    ]: /uri\n\n[x][foo]\n",
        "[\nfoo\nbar\n]: /uri\n\n[x][foo bar]\n"
    };
    int failure=0;salts_md_document *doc=NULL;
    for(size_t k=0u;k<sizeof(inputs)/sizeof(inputs[0]);++k) {
        const size_t n=strlen(inputs[k]);
        VERIFY(salts_md_parse(inputs[k],n,512u,n+1u,&doc)==SALTS_MD_OK);
        VERIFY(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        VERIFY(salts_md_rich_document_validate(&v,512u)==SALTS_MD_OK);
        VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK)==0u);
        VERIFY(v.raw_markdown_bytes==n &&
               !memcmp(v.raw_markdown,inputs[k],n));
        salts_md_document_destroy(doc);doc=NULL;
    }
    char source[512],first[128];
    /* The original raw bytes BETWEEN '[' and ']' count toward the 128
     * byte ceiling: 121 + LF(1) + indent(2) + "last"(4) = exactly 128.
     * The inline normalized key is also admissible, independently. */
    memset(first,'a',121u);first[121]='\0';
    int n=snprintf(source,sizeof(source),
                   "[%s\n  last]: /uri\n\n[x][%s last]\n",first,first);
    VERIFY(n>0 && (size_t)n<sizeof(source));
    const reference_case exact={name,source,
        "<p><a href=\"/uri\">x</a></p>\n","/uri",NULL,"x"};
    VERIFY(check_case(&exact)==0);
    /* 122 + LF(1) + indent(2) + "last"(4) = 129 RAW bytes and is
     * unsupported even though the normalized inline key is <=128. */
    memset(first,'a',122u);first[122]='\0';
    n=snprintf(source,sizeof(source),
               "[%s\n  last]: /uri\n\n[x][%s last]\n",first,first);
    VERIFY(n>0 && (size_t)n<sizeof(source));
    VERIFY(salts_md_parse(source,(size_t)n,512u,(size_t)n+1u,
                         &doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&v,512u)==SALTS_MD_OK);
    VERIFY(count_kind(v,SALTS_MD_BLOCK_LINK)==0u);
    VERIFY(v.raw_markdown_bytes==(size_t)n &&
           !memcmp(v.raw_markdown,source,(size_t)n));
    salts_md_document_destroy(doc);doc=NULL;
    /* 123 ASCII + LF + foo + LF = 128 original label bytes, while 124
     * makes 129; the normalized inline key is <=128 in both cases. */
    memset(first,'a',123u);first[123]='\0';
    n=snprintf(source,sizeof(source),
               "[%s\nfoo\n]: /uri\n\n[x][%s foo]\n",first,first);
    VERIFY(n>0 && (size_t)n<sizeof(source));
    const reference_case two_lf_exact={name,source,
        "<p><a href=\"/uri\">x</a></p>\n","/uri",NULL,"x"};
    VERIFY(check_case(&two_lf_exact)==0);
    memset(first,'a',124u);first[124]='\0';
    n=snprintf(source,sizeof(source),
               "[%s\nfoo\n]: /uri\n\n[x][%s foo]\n",first,first);
    VERIFY(n>0 && (size_t)n<sizeof(source));
    VERIFY(salts_md_parse(source,(size_t)n,512u,(size_t)n+1u,
                         &doc)==SALTS_MD_OK);
    VERIFY(doc!=NULL);
    const salts_md_rich_document_view two_lf=salts_md_get_document_view(doc);
    VERIFY(salts_md_rich_document_validate(&two_lf,512u)==SALTS_MD_OK);
    VERIFY(count_kind(two_lf,SALTS_MD_BLOCK_LINK)==0u);
done:
    salts_md_document_destroy(doc);
    return failure;
}

int main(void) {
    static const reference_case cases[]={
    { "official-208", "[\nfoo\n]: /url\nbar\n",
      "<p>bar</p>\n", NULL, NULL, NULL },
    { "two-LF-forward-reference", "[\nfoo\n]: /url\n\n[x][foo]\n",
      "<p><a href=\"/url\">x</a></p>\n", "/url", NULL, "x" },
    { "two-LF-CRLF-title", "[\r\n FoO\r\n ]: /uri 'T'\r\n\r\n[x][foo]\r\n",
      "<p><a href=\"/uri\" title=\"T\">x</a></p>\n", "/uri", "T", "x" },
    { "two-LF-split-URL", "[\nfoo\n]:\n /url\n\n[x][foo]\n",
      "<p><a href=\"/url\">x</a></p>\n", "/url", NULL, "x" },
    { "two-LF-duplicate-first-wins", "[\nfoo\n]: /first\n[foo]: /second\n\n[x][foo]\n",
      "<p><a href=\"/first\">x</a></p>\n", "/first", NULL, "x" },
    { "official-541", "[Foo\n  bar]: /url\n\n[Baz][Foo bar]\n",
      "<p><a href=\"/url\">Baz</a></p>\n", "/url", NULL, "Baz" },
    { "root-label-CRLF-title", "  [FoO\r\n   Bar]: /uri 'ok'\r\n\r\n[Baz][foo bar]\r\n",
      "<p><a href=\"/uri\" title=\"ok\">Baz</a></p>\n",
      "/uri", "ok", "Baz" },
    { "root-label-first-wins", "[Foo\n bar]: /first\n[foo bar]: /second\n\n[x][Foo bar]\n",
      "<p><a href=\"/first\">x</a></p>\n", "/first", NULL, "x" },
    { "root-label-escaped-case", "[foo\\!\n BAR]: /x\n\n[x][Foo\\! Bar]\n",
      "<p><a href=\"/x\">x</a></p>\n", "/x", NULL, "x" },
    { "official-193", "   [foo]: \n      /url  \n           'the title'  \n\n[foo]\n", "<p><a href=\"/url\" title=\"the title\">foo</a></p>\n", "/url", "the title", "foo" },
    { "official-194", "[Foo*bar\\]]:my_(url) 'title (with parens)'\n\n[Foo*bar\\]]\n", "<p><a href=\"my_(url)\" title=\"title (with parens)\">Foo*bar]</a></p>\n", "my_(url)", "title (with parens)", "Foo*bar]" },
    { "official-196", "[foo]: /url '\ntitle\nline1\nline2\n'\n\n[foo]\n", "<p><a href=\"/url\" title=\"\ntitle\nline1\nline2\n\">foo</a></p>\n", "/url", "\ntitle\nline1\nline2\n", "foo" },
    { "deep-CRLF-tab-owner", "   [foo]:\r\n\t /url \t\r\n\t\t'title' \t\r\n\r\n[foo]\r\n", "<p><a href=\"/url\" title=\"title\">foo</a></p>\n", "/url", "title", "foo" },
    { "multiline-CRLF-payload", "[foo]: /uri 'A &amp; B  \r\n    x\ty\r\n  z'\r\n\r\n[foo]\r\n", "<p><a href=\"/uri\" title=\"A &amp; B  \nx\ty\nz\">foo</a></p>\n", "/uri", "A & B  \nx\ty\nz", "foo" },
    { "optional-deep-multiline-title", "[foo]: /uri\n    'first\n     second'\n\n[foo]\n", "<p><a href=\"/uri\" title=\"first\nsecond\">foo</a></p>\n", "/uri", "first\nsecond", "foo" },
    { "parenthesized-multiline-title", "[foo]: /uri (a\\(\n b\\))\n\n[foo]\n", "<p><a href=\"/uri\" title=\"a(\nb)\">foo</a></p>\n", "/uri", "a(\nb)", "foo" },
    { "escaped-explicit-casefold", "[visible][ FOO*BAR\\] ]\n\n[foo*bar\\]]: /uri 'ok'\n", "<p><a href=\"/uri\" title=\"ok\">visible</a></p>\n", "/uri", "ok", "visible" },
    { "escaped-collapsed", "[Foo*bar\\]][]\n\n[foo*bar\\]]: /uri\n", "<p><a href=\"/uri\">Foo*bar]</a></p>\n", "/uri", NULL, "Foo*bar]" },
    { "escaped-delimiter-opacity", "**[Foo*bar\\]]**\n\n[Foo*bar\\]]: /uri\n", "<p><strong><a href=\"/uri\">Foo*bar]</a></strong></p>\n", "/uri", NULL, "Foo*bar]" },
    { "official-545", "[bar][foo\\!]\n\n[foo!]: /url\n", "<p>[bar][foo!]</p>\n", NULL, NULL, NULL },
    { "official-549", "[foo][ref\\[]\n\n[ref\\[]: /uri\n", "<p><a href=\"/uri\">foo</a></p>\n", "/uri", NULL, "foo" },
    { "official-550", "[bar\\\\]: /uri\n\n[bar\\\\]\n", "<p><a href=\"/uri\">bar\\</a></p>\n", "/uri", NULL, "bar\\" },
    { "official-559", "[[*foo* bar]]\n\n[*foo* bar]: /url \"title\"\n",
      "<p>[<a href=\"/url\" title=\"title\"><em>foo</em> bar</a>]</p>\n",
      "/url", "title", "foo bar" },
    { "adjacent-close-shortcut", "[[ref]]\n\n[ref]: /url\n",
      "<p>[<a href=\"/url\">ref</a>]</p>\n", "/url", NULL, "ref" },
    { "adjacent-close-escaped-label", "[[a\\]]]\n\n[a\\]]: /url\n",
      "<p>[<a href=\"/url\">a]</a>]</p>\n", "/url", NULL, "a]" },
    { "adjacent-close-unbound", "[[missing]]\n\n[other]: /url\n",
      "<p>[[missing]]</p>\n", NULL, NULL, NULL },
    { "adjacent-close-code-span", "`[[ref]]`\n\n[ref]: /url\n",
      "<p><code>[[ref]]</code></p>\n", NULL, NULL, NULL },
    { "raw-entity-key-not-decoded", "[visible][a&b]\n\n[a&amp;b]: /wrong\n", "<p>[visible][a&amp;b]</p>\n", NULL, NULL, NULL },
    { "escaped-first-definition-wins", "[a\\!]: /first 'one'\n[A\\!]: /second 'two'\n\n[a\\!]\n", "<p><a href=\"/first\" title=\"one\">a!</a></p>\n", "/first", "one", "a!" },
    { "official-197", "[foo]: /url 'title\n\nwith blank line'\n\n[foo]\n", "<p>[foo]: /url 'title</p>\n<p>with blank line'</p>\n<p>[foo]</p>\n", NULL, NULL, NULL },
    { "official-209", "[foo]: /url \"title\" ok\n", "<p>[foo]: /url &quot;title&quot; ok</p>\n", NULL, NULL, NULL },
    { "same-line-unclosed", "[foo]: /url 'never\nends\n\n[foo]\n", "<p>[foo]: /url 'never\nends</p>\n<p>[foo]</p>\n", NULL, NULL, NULL },
    { "optional-title-garbage-keeps-url", "[foo]: /url\n\"title\" junk\n\n[foo]\n", "<p>&quot;title&quot; junk</p>\n<p><a href=\"/url\">foo</a></p>\n", "/url", NULL, "foo" },
    { "optional-unclosed-keeps-url", "[foo]: /url\n'never\n\n[foo]\n", "<p>'never</p>\n<p><a href=\"/url\">foo</a></p>\n", "/url", NULL, "foo" },
    { "title-root-heading-barrier", "[foo]: /url 'never\n# boundary\nclose'\n\n[foo]\n", "<p>[foo]: /url 'never</p>\n<h1>boundary</h1>\n<p>close'</p>\n<p>[foo]</p>\n", NULL, NULL, NULL },
    { "title-root-fence-barrier", "[foo]: /url 'never\n```\nclose'\n```\n\n[foo]\n", "<p>[foo]: /url 'never</p>\n<pre><code>close'\n</code></pre>\n<p>[foo]</p>\n", NULL, NULL, NULL },
    { "opening-indented-code-not-reference", "    [foo]: /url 'title'\n\n[foo]\n", "<pre><code>[foo]: /url 'title'\n</code></pre>\n<p>[foo]</p>\n", NULL, NULL, NULL },
    { "blank-destination-barrier", "[foo]:\n\n    /url\n\n[foo]\n", "<p>[foo]:</p>\n<pre><code>/url\n</code></pre>\n<p>[foo]</p>\n", NULL, NULL, NULL },
    { "unescaped-nested-label-rejected", "[a[b]]: /wrong\n\n[a[b]]\n", "<p>[a[b]]: /wrong</p>\n<p>[a[b]]</p>\n", NULL, NULL, NULL },
    { "title-escaped-attribute-injection", "[foo]: /uri 'a\n&quot; onmouseover=&quot;alert(1)'\n\n[foo]\n", "<p><a href=\"/uri\" title=\"a\n&quot; onmouseover=&quot;alert(1)\">foo</a></p>\n", "/uri", "a\n\" onmouseover=\"alert(1)", "foo" },
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==40u,
                   "multiline/escaped reference fixture denominator");
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(check_case(&cases[i]))return 1;
    if(check_root_label_boundary_rejections() ||
       check_safety() || check_budgets() || check_label_budget() ||
       check_adjacent_owners() || check_inline_label_owners() ||
       check_document_binding_isolation())return 1;
    puts("C11 multiline/escaped references: 43/43 HTML/AST, raw ownership, "
         "inline owners, document isolation, title/href security, line/map budgets and atomic rollback");
    return 0;
}
