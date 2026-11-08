#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expr) do { if(!(expr)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr);return 1; \
} } while(0)

typedef struct fixture {
    const char *markdown,*expected,*href;
    unsigned links,quotes,lists;
} fixture;
static size_t count(salts_md_rich_document_view v,salts_md_block_kind kind){
    size_t total=0u;
    for(size_t i=0;i<v.document.block_count;++i)
        total+=v.document.blocks[i].kind==kind;
    return total;
}
static int verify(const fixture *f){
    const size_t n=strlen(f->markdown),want=strlen(f->expected);
    char *original=(char*)malloc(n+1u);CHECK(original!=NULL);
    memcpy(original,f->markdown,n+1u);
    salts_md_document *doc=NULL,*exact=NULL,*rejected=NULL;
    CHECK(salts_md_parse(original,n,512u,n+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);memset(original,0x61,n);free(original);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&v,512u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==n &&
          !memcmp(v.raw_markdown,f->markdown,n));
    CHECK(count(v,SALTS_MD_BLOCK_LINK)==f->links);
    CHECK(count(v,SALTS_MD_BLOCK_QUOTE)==f->quotes);
    CHECK(count(v,SALTS_MD_BLOCK_LIST)==f->lists);
    if(f->href){
        size_t matched=0u;
        for(size_t i=1u;i<v.document.block_count;++i) {
            if(v.document.blocks[i].kind!=SALTS_MD_BLOCK_LINK)continue;
            const char *data=NULL;size_t bytes=SIZE_MAX;
            CHECK(salts_md_rich_document_metadata(&v,i,&data,&bytes)
                  ==SALTS_MD_OK);
            CHECK(bytes==strlen(f->href) &&
                  !memcmp(data,f->href,bytes));++matched;
        }
        CHECK(matched==f->links);
    }
    size_t bytes=SIZE_MAX;
    CHECK(salts_md_render_html(&v,512u,NULL,0u,&bytes)==SALTS_MD_OK);
    CHECK(bytes==want);
    char *output=(char*)malloc(want+3u);CHECK(output!=NULL);
    memset(output,0x57,want+3u);
    CHECK(salts_md_render_html(&v,512u,output,want,&bytes)
          ==SALTS_MD_CAPACITY_EXCEEDED);
    for(size_t i=0u;i<want+3u;++i)
        CHECK((unsigned char)output[i]==0x57u);
    CHECK(salts_md_render_html(&v,512u,output,want+1u,&bytes)==SALTS_MD_OK);
    CHECK(bytes==want && !memcmp(output,f->expected,want+1u));
    CHECK((unsigned char)output[want+1u]==0x57u);
    free(output);
    const size_t nodes=v.document.block_count;
    salts_md_document_destroy(doc);
    CHECK(salts_md_parse(f->markdown,n,nodes,n+1u,&exact)==SALTS_MD_OK);
    CHECK(exact!=NULL);
    const salts_md_rich_document_view e=salts_md_get_document_view(exact);
    CHECK(salts_md_rich_document_validate(&e,nodes)==SALTS_MD_OK);
    salts_md_document_destroy(exact);
    CHECK(salts_md_parse(f->markdown,n,nodes-1u,n+1u,&rejected)
          ==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(rejected==NULL);
    return 0;
}
static const fixture cases[]={
    {"[foo]\n\n> [foo]: /url\n",
     "<p><a href=\"/url\">foo</a></p>\n<blockquote>\n</blockquote>\n",
     "/url",1u,1u,0u}, /* #218 */
    {"- a\n- b\n\n  [ref]: /url\n- d\n",
     "<ul>\n<li>\n<p>a</p>\n</li>\n<li>\n<p>b</p>\n</li>\n<li>\n<p>d</p>\n</li>\n</ul>\n",
     NULL,0u,0u,1u}, /* #317 */
    {"> [x]: /first\n\n[x]: /second\n\n[x]\n",
     "<blockquote>\n</blockquote>\n<p><a href=\"/first\">x</a></p>\n",
     "/first",1u,1u,0u},
    {"[x]: /first\n\n> [x]: /second\n\n[x]\n",
     "<blockquote>\n</blockquote>\n<p><a href=\"/first\">x</a></p>\n",
     "/first",1u,1u,0u},
    {"[x]\n\n> [x]: /first\n\n[x]: /second\n",
     "<p><a href=\"/first\">x</a></p>\n<blockquote>\n</blockquote>\n",
     "/first",1u,1u,0u}
};
int main(void){
    _Static_assert(sizeof(cases)/sizeof(cases[0])==5u,
                   "quote/list owner and source-order test count");
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(verify(&cases[i]))return 1;
    puts("C11 quote/list reference owners: 5 exact AST/HTML, node "
         "and buffer budgets, source order, first wins, forward refs");
    return 0;
}
