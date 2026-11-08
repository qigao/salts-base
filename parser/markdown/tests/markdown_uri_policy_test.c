#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do {if(!(expr)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr);return 1; \
} } while(0)

typedef struct uri_fixture {
    unsigned official;
    const char *markdown,*visible,*safe_html,*spec_html;
} uri_fixture;

static size_t count_kind(salts_md_rich_document_view v,salts_md_block_kind kind) {
    size_t count=0u;
    for(size_t i=0u;i<v.document.block_count;++i)
        count+=v.document.blocks[i].kind==kind;
    return count;
}

static int render_check(salts_md_rich_document_view v,
                        salts_md_html_policy policy,const char *expected) {
    const size_t bytes=strlen(expected);
    size_t required=SIZE_MAX,actual=SIZE_MAX;
    CHECK(salts_md_render_html_with_policy(&v,256u,policy,
                                         NULL,0u,&required)==SALTS_MD_OK);
    CHECK(required==bytes);
    char *out=(char*)malloc(bytes+3u);
    CHECK(out!=NULL);
    memset(out,0x53,bytes+3u);
    CHECK(salts_md_render_html_with_policy(&v,256u,policy,
                                         out,bytes,&actual)==
          SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(actual==bytes);
    for(size_t i=0u;i<bytes+3u;++i)CHECK((unsigned char)out[i]==0x53u);
    CHECK(salts_md_render_html_with_policy(&v,256u,policy,
                                         out,bytes+1u,&actual)==SALTS_MD_OK);
    CHECK(actual==bytes && !memcmp(out,expected,bytes+1u));
    CHECK((unsigned char)out[bytes+1u]==0x53u);
    if(policy==SALTS_MD_HTML_SAFE_APPLICATION) {
        size_t default_count=SIZE_MAX;
        CHECK(salts_md_render_html(&v,256u,out,bytes+1u,&default_count)
              ==SALTS_MD_OK);
        CHECK(default_count==bytes && !memcmp(out,expected,bytes+1u));
    }
    free(out);
    return 0;
}

static int verify(const uri_fixture *fixture) {
    const size_t bytes=strlen(fixture->markdown);
    char *mutable_input=(char*)malloc(bytes+1u);
    CHECK(mutable_input!=NULL);
    memcpy(mutable_input,fixture->markdown,bytes+1u);
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(mutable_input,bytes,256u,bytes+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    memset(mutable_input,'?',bytes);
    free(mutable_input);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==bytes &&
          !memcmp(v.raw_markdown,fixture->markdown,bytes));
    CHECK(count_kind(v,SALTS_MD_BLOCK_LINK)==0u);
    CHECK(count_kind(v,SALTS_MD_BLOCK_URI_AUTOLINK)==1u);
    const size_t visible_bytes=strlen(fixture->visible);
    size_t leaf=v.document.block_count;
    for(size_t i=1u;i<v.document.block_count;++i)
        if(v.document.blocks[i].kind==SALTS_MD_BLOCK_URI_AUTOLINK)leaf=i;
    CHECK(leaf<v.document.block_count);
    const salts_md_block *node=&v.document.blocks[leaf];
    CHECK(node->byte_end-node->byte_begin==visible_bytes);
    CHECK(!memcmp(v.document.source+node->byte_begin,
                  fixture->visible,visible_bytes));
    const char *meta=NULL;size_t meta_bytes=SIZE_MAX;
    CHECK(salts_md_rich_document_metadata(&v,leaf,&meta,&meta_bytes)==
          SALTS_MD_OK);
    CHECK(meta_bytes==visible_bytes);
    CHECK(!memcmp(meta,fixture->visible,meta_bytes));
    CHECK(render_check(v,SALTS_MD_HTML_SAFE_APPLICATION,fixture->safe_html)==0);
    CHECK(render_check(v,SALTS_MD_HTML_SPEC_CONFORMANCE,fixture->spec_html)==0);

    /* One missing semantic node must fail without partial publication. */
    const size_t nodes=v.document.block_count;
    CHECK(nodes>1u);
    salts_md_document_destroy(doc);doc=NULL;
    CHECK(salts_md_parse(fixture->markdown,bytes,nodes-1u,bytes+1u,
                        &doc)==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(doc==NULL);
    return 0;
}

static int test_forged_uri_leaf(void) {
    const char sample[]="<custom:trusted-only-in-spec>\n";
    const size_t bytes=sizeof(sample)-1u;
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(sample,bytes,64u,bytes+1u,&doc)==SALTS_MD_OK);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    size_t leaf=v.document.block_count;
    for(size_t i=1u;i<v.document.block_count;++i)
        if(v.document.blocks[i].kind==SALTS_MD_BLOCK_URI_AUTOLINK)leaf=i;
    CHECK(leaf<v.document.block_count);
    salts_md_block_metadata *metadata=(salts_md_block_metadata*)malloc(
        v.block_metadata_count*sizeof(*metadata));
    CHECK(metadata!=NULL);
    memcpy(metadata,v.block_metadata,
           v.block_metadata_count*sizeof(*metadata));
    CHECK(metadata[leaf].byte_end>metadata[leaf].byte_begin);
    --metadata[leaf].byte_end;
    salts_md_rich_document_view forged=v;
    forged.block_metadata=metadata;
    char untouched[256];memset(untouched,0x6A,sizeof(untouched));
    size_t required=SIZE_MAX;
    CHECK(salts_md_render_html_with_policy(
          &forged,64u,SALTS_MD_HTML_SPEC_CONFORMANCE,
          untouched,sizeof(untouched),&required)==SALTS_MD_INVALID_ARGUMENT);
    for(size_t i=0u;i<sizeof(untouched);++i)
        CHECK((unsigned char)untouched[i]==0x6Au);
    free(metadata);
    salts_md_document_destroy(doc);
    return 0;
}

int main(void) {
    static const uri_fixture official[]={
        {596u,"<irc://foo.bar:2233/baz>\n",
         "irc://foo.bar:2233/baz",
         "<p>&lt;irc://foo.bar:2233/baz&gt;</p>\n",
         "<p><a href=\"irc://foo.bar:2233/baz\">irc://foo.bar:2233/baz</a></p>\n"},
        {598u,"<a+b+c:d>\n","a+b+c:d",
         "<p>&lt;a+b+c:d&gt;</p>\n",
         "<p><a href=\"a+b+c:d\">a+b+c:d</a></p>\n"},
        {599u,"<made-up-scheme://foo,bar>\n","made-up-scheme://foo,bar",
         "<p>&lt;made-up-scheme://foo,bar&gt;</p>\n",
         "<p><a href=\"made-up-scheme://foo,bar\">made-up-scheme://foo,bar</a></p>\n"},
        {601u,"<localhost:5001/foo>\n","localhost:5001/foo",
         "<p>&lt;localhost:5001/foo&gt;</p>\n",
         "<p><a href=\"localhost:5001/foo\">localhost:5001/foo</a></p>\n"}
    };
    _Static_assert(sizeof(official)/sizeof(official[0])==4u,
                   "four missing URI policy official cases");
    for(size_t i=0u;i<sizeof(official)/sizeof(official[0]);++i)
        if(verify(&official[i]))return 1;

    /* Threat model: a legal autolink scheme may be malicious to navigate;
     * only explicitly selected test-only SPEC may emit a real anchor.
     * Even then, literal label and href bytes are independently escaped. */
    static const uri_fixture unsafe[]={
        {0u,"<javascript:alert>\n","javascript:alert",
         "<p>&lt;javascript:alert&gt;</p>\n",
         "<p><a href=\"javascript:alert\">javascript:alert</a></p>\n"},
        {0u,"<data:text/html,hello>\n","data:text/html,hello",
         "<p>&lt;data:text/html,hello&gt;</p>\n",
         "<p><a href=\"data:text/html,hello\">data:text/html,hello</a></p>\n"},
        {0u,"<VBScript:msgbox>\n","VBScript:msgbox",
         "<p>&lt;VBScript:msgbox&gt;</p>\n",
         "<p><a href=\"VBScript:msgbox\">VBScript:msgbox</a></p>\n"},
        {0u,"<abc:foo&bar>\n","abc:foo&bar",
         "<p>&lt;abc:foo&amp;bar&gt;</p>\n",
         "<p><a href=\"abc:foo&amp;bar\">abc:foo&amp;bar</a></p>\n"}
    };
    for(size_t i=0u;i<sizeof(unsafe)/sizeof(unsafe[0]);++i)
        if(verify(&unsafe[i]))return 1;

    /* Malformed token syntax cannot create even a SPEC-only URI node. */
    static const char *const invalid[]={
        "<a:d>\n","<abc:>\n","<a_b:c>\n",
        "<foo:has spaces>\n","<foo:\tbad>\n"
    };
    for(size_t i=0u;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        salts_md_document *doc=NULL;
        const size_t bytes=strlen(invalid[i]);
        CHECK(salts_md_parse(invalid[i],bytes,128u,bytes+1u,&doc)==SALTS_MD_OK);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        CHECK(count_kind(v,SALTS_MD_BLOCK_URI_AUTOLINK)==0u);
        salts_md_document_destroy(doc);
    }
    if(test_forged_uri_leaf())return 1;
    puts("C11 URI policy: 4 official + 4 unsafe schemes, no SAFE navigation");
    return 0;
}
