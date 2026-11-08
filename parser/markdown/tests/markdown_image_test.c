#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(expr) do { \
    if(!(expr)) { \
        fprintf(stderr,"%s:%d: failed: %s\n",__FILE__,__LINE__,#expr); \
        return 1; \
    } \
} while(0)

typedef struct image_fixture {
    unsigned official_id;
    const char *markdown;
    const char *expected;
} image_fixture;

static size_t count_kind(salts_md_rich_document_view v,
                         salts_md_block_kind kind) {
    size_t count=0u;
    for(size_t i=0;i<v.document.block_count;++i)
        count+=(v.document.blocks[i].kind==kind);
    return count;
}

static int verify_image(const image_fixture *f) {
    const size_t n=strlen(f->markdown),expected=strlen(f->expected);
    char *owned=(char*)malloc(n+1u);
    REQUIRE(owned!=NULL);
    memcpy(owned,f->markdown,n+1u);
    salts_md_document *doc=NULL,*rejected=NULL;
    REQUIRE(salts_md_parse(owned,n,512u,n+1u,&doc)==SALTS_MD_OK);
    REQUIRE(doc!=NULL);
    memset(owned,'x',n);free(owned); /* raw source is document-owned */
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    REQUIRE(salts_md_rich_document_validate(&v,512u)==SALTS_MD_OK);
    REQUIRE(v.raw_markdown_bytes==n &&
            !memcmp(v.raw_markdown,f->markdown,n));
    REQUIRE(count_kind(v,SALTS_MD_BLOCK_IMAGE)>0u);
    REQUIRE(v.document.block_count>2u);
    for(size_t i=1u;i<v.document.block_count;++i) {
        if(v.document.blocks[i].kind!=SALTS_MD_BLOCK_IMAGE_TITLE)continue;
        REQUIRE(v.document.blocks[i].parent_index==i-1u);
        REQUIRE(v.document.blocks[i-1u].kind==SALTS_MD_BLOCK_IMAGE);
        const char *title=NULL;size_t bytes=SIZE_MAX;
        REQUIRE(salts_md_rich_document_metadata(&v,i,&title,&bytes)==
                SALTS_MD_OK);
        REQUIRE(title!=NULL || bytes==0u);
    }
    size_t need=SIZE_MAX;
    REQUIRE(salts_md_render_html(&v,512u,NULL,0u,&need)==SALTS_MD_OK);
    REQUIRE(need==expected);
    char *buffer=(char*)malloc(expected+3u);
    REQUIRE(buffer!=NULL);
    memset(buffer,0x5Au,expected+3u);
    size_t counted=SIZE_MAX;
    REQUIRE(salts_md_render_html(&v,512u,buffer,need,&counted)==
            SALTS_MD_CAPACITY_EXCEEDED);
    REQUIRE(counted==need);
    for(size_t i=0u;i<expected+3u;++i)
        REQUIRE((unsigned char)buffer[i]==0x5Au);
    REQUIRE(salts_md_render_html(&v,512u,buffer,need+1u,&counted)==SALTS_MD_OK);
    REQUIRE(counted==expected &&
            !memcmp(buffer,f->expected,expected+1u));
    REQUIRE((unsigned char)buffer[expected+1u]==0x5Au);
    free(buffer);
    const size_t nodes=v.document.block_count;
    salts_md_document_destroy(doc);
    REQUIRE(salts_md_parse(f->markdown,n,nodes-1u,n+1u,
                          &rejected)==SALTS_MD_CAPACITY_EXCEEDED);
    REQUIRE(rejected==NULL);
    return 0;
}

static int test_image_title_and_safety(void) {
    const char src[]="![a\" <b> & c](/i \"T &copy;\")\n";
    salts_md_document *doc=NULL;
    REQUIRE(salts_md_parse(src,sizeof(src)-1u,256u,sizeof(src),&doc)==
            SALTS_MD_OK);
    REQUIRE(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    REQUIRE(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    REQUIRE(count_kind(v,SALTS_MD_BLOCK_IMAGE)==1u);
    REQUIRE(count_kind(v,SALTS_MD_BLOCK_IMAGE_TITLE)==1u);
    size_t image=0u,title=0u;
    for(size_t i=1u;i<v.document.block_count;++i) {
        if(v.document.blocks[i].kind==SALTS_MD_BLOCK_IMAGE)image=i;
        if(v.document.blocks[i].kind==SALTS_MD_BLOCK_IMAGE_TITLE)title=i;
    }
    REQUIRE(title==image+1u);
    const char *metadata=NULL;size_t bytes=SIZE_MAX;
    REQUIRE(salts_md_rich_document_metadata(&v,title,&metadata,&bytes)==
            SALTS_MD_OK);
    const char t[]="T ©";
    REQUIRE(bytes==sizeof(t)-1u && !memcmp(metadata,t,bytes));
    const char expect[]="<p><img src=\"/i\" alt=\"a&quot; &lt;b&gt; &amp; c\" "
                        "title=\"T ©\" /></p>\n";
    size_t need=SIZE_MAX;
    REQUIRE(salts_md_render_html(&v,256u,NULL,0u,&need)==SALTS_MD_OK);
    REQUIRE(need==sizeof(expect)-1u);
    char out[256]={0};size_t actual=SIZE_MAX;
    REQUIRE(salts_md_render_html(&v,256u,out,sizeof(out),&actual)==SALTS_MD_OK);
    REQUIRE(!memcmp(out,expect,sizeof(expect)));
    salts_md_document_destroy(doc);doc=NULL;
    const char empty[]="![x](/i \"\")\n";
    REQUIRE(salts_md_parse(empty,sizeof(empty)-1u,128u,sizeof(empty),&doc)==
            SALTS_MD_OK);
    const salts_md_rich_document_view e=salts_md_get_document_view(doc);
    REQUIRE(count_kind(e,SALTS_MD_BLOCK_IMAGE_TITLE)==1u);
    REQUIRE(salts_md_render_html(&e,128u,out,sizeof(out),&actual)==SALTS_MD_OK);
    REQUIRE(!strcmp(out,"<p><img src=\"/i\" alt=\"x\" title=\"\" /></p>\n"));
    salts_md_document_destroy(doc);doc=NULL;
    const char *const rejected_src[]={
        "![bad](javascript:alert)\n",
        "![bad](data:text/html)\n",
        "![bad](mailto:you@example.org)\n",
        "![bad](/a&#10;b)\n"
    };
    for(size_t i=0u;i<sizeof(rejected_src)/sizeof(rejected_src[0]);++i) {
        const char *bad=rejected_src[i];const size_t n=strlen(bad);
        REQUIRE(salts_md_parse(bad,n,128u,n+1u,&doc)==SALTS_MD_OK);
        const salts_md_rich_document_view view=salts_md_get_document_view(doc);
        REQUIRE(count_kind(view,SALTS_MD_BLOCK_IMAGE)==1u);
        memset(out,0x37,sizeof(out));
        REQUIRE(salts_md_render_html(&view,128u,out,sizeof(out),&actual)==
                SALTS_MD_INVALID_ARGUMENT);
        for(size_t j=0u;j<sizeof(out);++j)
            REQUIRE((unsigned char)out[j]==0x37u);
        salts_md_document_destroy(doc);doc=NULL;
    }
    return 0;
}

static int test_document_owner_isolation(void) {
    const char *const markdown[]={
        "![x][id]\n\n[id]: /first \"ONE\"\n",
        "![x][id]\n\n[id]: /second \"TWO\"\n"
    };
    const char *const expected[]={
        "<p><img src=\"/first\" alt=\"x\" title=\"ONE\" /></p>\n",
        "<p><img src=\"/second\" alt=\"x\" title=\"TWO\" /></p>\n"
    };
    salts_md_document *docs[2]={NULL,NULL};
    for(size_t k=0u;k<2u;++k) {
        const size_t n=strlen(markdown[k]);
        REQUIRE(salts_md_parse(markdown[k],n,128u,n+1u,
                              &docs[k])==SALTS_MD_OK);
    }
    for(size_t k=0u;k<2u;++k) {
        const salts_md_rich_document_view v=salts_md_get_document_view(docs[k]);
        REQUIRE(count_kind(v,SALTS_MD_BLOCK_IMAGE)==1u);
        char out[256];size_t size=SIZE_MAX;
        REQUIRE(salts_md_render_html(&v,128u,out,sizeof(out),&size)==SALTS_MD_OK);
        REQUIRE(size==strlen(expected[k]) && !strcmp(out,expected[k]));
    }
    salts_md_document_destroy(docs[0]);
    salts_md_document_destroy(docs[1]);
    return 0;
}

static const image_fixture official[]={
    {517u, "[![moon](moon.jpg)](/uri)\n",
     "<p><a href=\"/uri\"><img src=\"moon.jpg\" alt=\"moon\" /></a></p>\n"},
    {520u, "![[[foo](uri1)](uri2)](uri3)\n",
     "<p><img src=\"uri3\" alt=\"[foo](uri2)\" /></p>\n"},
    {531u, "[![moon](moon.jpg)][ref]\n\n[ref]: /uri\n",
     "<p><a href=\"/uri\"><img src=\"moon.jpg\" alt=\"moon\" /></a></p>\n"},
    {572u, "![foo](/url \"title\")\n",
     "<p><img src=\"/url\" alt=\"foo\" title=\"title\" /></p>\n"},
    {573u, "![foo *bar*]\n\n[foo *bar*]: train.jpg \"train & tracks\"\n",
     "<p><img src=\"train.jpg\" alt=\"foo bar\" title=\"train &amp; tracks\" /></p>\n"},
    {574u, "![foo ![bar](/url)](/url2)\n",
     "<p><img src=\"/url2\" alt=\"foo bar\" /></p>\n"},
    {575u, "![foo [bar](/url)](/url2)\n",
     "<p><img src=\"/url2\" alt=\"foo bar\" /></p>\n"},
    {576u, "![foo *bar*][]\n\n[foo *bar*]: train.jpg \"train & tracks\"\n",
     "<p><img src=\"train.jpg\" alt=\"foo bar\" title=\"train &amp; tracks\" /></p>\n"},
    {577u, "![foo *bar*][foobar]\n\n[FOOBAR]: train.jpg \"train & tracks\"\n",
     "<p><img src=\"train.jpg\" alt=\"foo bar\" title=\"train &amp; tracks\" /></p>\n"},
    {578u, "![foo](train.jpg)\n",
     "<p><img src=\"train.jpg\" alt=\"foo\" /></p>\n"},
    {579u, "My ![foo bar](/path/to/train.jpg  \"title\"   )\n",
     "<p>My <img src=\"/path/to/train.jpg\" alt=\"foo bar\" title=\"title\" /></p>\n"},
    {580u, "![foo](<url>)\n",
     "<p><img src=\"url\" alt=\"foo\" /></p>\n"},
    {581u, "![](/url)\n",
     "<p><img src=\"/url\" alt=\"\" /></p>\n"},
    {582u, "![foo][bar]\n\n[bar]: /url\n",
     "<p><img src=\"/url\" alt=\"foo\" /></p>\n"},
    {583u, "![foo][bar]\n\n[BAR]: /url\n",
     "<p><img src=\"/url\" alt=\"foo\" /></p>\n"},
    {584u, "![foo][]\n\n[foo]: /url \"title\"\n",
     "<p><img src=\"/url\" alt=\"foo\" title=\"title\" /></p>\n"},
    {585u, "![*foo* bar][]\n\n[*foo* bar]: /url \"title\"\n",
     "<p><img src=\"/url\" alt=\"foo bar\" title=\"title\" /></p>\n"},
    {586u, "![Foo][]\n\n[foo]: /url \"title\"\n",
     "<p><img src=\"/url\" alt=\"Foo\" title=\"title\" /></p>\n"},
    {587u, "![foo] \n[]\n\n[foo]: /url \"title\"\n",
     "<p><img src=\"/url\" alt=\"foo\" title=\"title\" />\n[]</p>\n"},
    {588u, "![foo]\n\n[foo]: /url \"title\"\n",
     "<p><img src=\"/url\" alt=\"foo\" title=\"title\" /></p>\n"},
    {589u, "![*foo* bar]\n\n[*foo* bar]: /url \"title\"\n",
     "<p><img src=\"/url\" alt=\"foo bar\" title=\"title\" /></p>\n"},
    {591u, "![Foo]\n\n[foo]: /url \"title\"\n",
     "<p><img src=\"/url\" alt=\"Foo\" title=\"title\" /></p>\n"},
};

int main(void) {
    _Static_assert(sizeof(official)/sizeof(official[0])==22u,
                   "CommonMark official image-related fixture count");
    for(size_t i=0u;i<sizeof(official)/sizeof(official[0]);++i)
        if(verify_image(&official[i])) {
            fprintf(stderr,"official IMAGE #%u failed\n",
                    official[i].official_id);
            return 1;
        }
    if(test_image_title_and_safety() || test_document_owner_isolation())
        return 1;
    puts("C11 IMAGE: 22 official owned AST/HTML, src/title/alt, "
         "document isolation, safety and failure-atomic output");
    return 0;
}
