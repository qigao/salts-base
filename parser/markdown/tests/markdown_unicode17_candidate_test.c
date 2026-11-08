/* Canonical installed-SDK Unicode 17 regression: official CommonMark 0.31.2
 * #206/#354/#540, plus reverse-casefold negative controls. Both default SAFE
 * and explicit test-only SPEC HTML policies must match exact official bytes.
 * No local Unicode table, old parser fallback or Tango migration. */
#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <salts_unicode.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if(!(expr)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr); \
    return 1; \
} } while(0)

typedef struct unicode_case {
    unsigned official_id;
    const char *markdown,*html;
    size_t links;
} unicode_case;

/* Hex UTF-8 inputs preserve the official corpus bytes across host codepages. */
static const unicode_case cases[] = {
    {206u,
     "[\xCE\x91\xCE\x93\xCE\xA9]: /\xCF\x86\xCE\xBF\xCF\x85\n\n"
     "[\xCE\xB1\xCE\xB3\xCF\x89]\n",
     "<p><a href=\"/%CF%86%CE%BF%CF%85\">\xCE\xB1\xCE\xB3\xCF\x89</a></p>\n",
     1u},
    {354u,
     "*$*alpha.\n\n*\xC2\xA3*bravo.\n\n*\xE2\x82\xAC*charlie.\n",
     "<p>*$*alpha.</p>\n<p>*\xC2\xA3*bravo.</p>\n"
     "<p>*\xE2\x82\xAC*charlie.</p>\n",
     0u},
    {540u,
     "[\xE1\xBA\x9E]\n\n[SS]: /url\n",
     "<p><a href=\"/url\">\xE1\xBA\x9E</a></p>\n",
     1u},
    {0u,
     "[SS]\n\n[\xE1\xBA\x9E]: /reverse\n",
     "<p><a href=\"/reverse\">SS</a></p>\n",
     1u},
    {0u,
     "[\xCE\xB1\xCE\xB3\xCF\x89]\n\n[\xCE\x91\xCE\x93\xCE\xA9]: /reverse\n",
     "<p><a href=\"/reverse\">\xCE\xB1\xCE\xB3\xCF\x89</a></p>\n",
     1u}
};

static int test_abi(void) {
    int ps=7;
    CHECK(salts_unicode_is_punctuation_or_symbol(0x00a3u,&ps)==
          SALTS_UNICODE_OK && ps==1);
    CHECK(salts_unicode_is_punctuation_or_symbol(0x20acu,&ps)==
          SALTS_UNICODE_OK && ps==1);
    CHECK(salts_unicode_is_punctuation_or_symbol(0x03b1u,&ps)==
          SALTS_UNICODE_OK && ps==0);
    ps=7;
    CHECK(salts_unicode_is_punctuation_or_symbol(0xd800u,&ps)==
          SALTS_UNICODE_ERR_INVALID_ARGUMENT && ps==7);
    char buffer[16];memset(buffer,0x55,sizeof(buffer));
    size_t required=99u;
    const char sharp[]="\xE1\xBA\x9E";
    const vstr source=vstr_from_buf(sharp,sizeof(sharp)-1u);
    CHECK(salts_unicode_case_fold(source,NULL,0u,&required)==
          SALTS_UNICODE_OK && required==2u);
    CHECK(salts_unicode_case_fold(source,buffer,1u,&required)==
          SALTS_UNICODE_ERR_CAPACITY && required==2u);
    for(size_t j=0u;j<sizeof(buffer);++j)
        CHECK((unsigned char)buffer[j]==0x55u);
    CHECK(salts_unicode_case_fold(source,buffer,sizeof(buffer),&required)==
          SALTS_UNICODE_OK && required==2u &&
          !memcmp(buffer,"ss",2u));
    const char invalid[]="\xC0\xAF";
    size_t len=99u;
    memset(buffer,0x66,sizeof(buffer));
    CHECK(salts_unicode_case_fold(vstr_from_buf(invalid,2u),
          buffer,sizeof(buffer),&len)==SALTS_UNICODE_ERR_INVALID_UTF8 &&
          len==99u);
    for(size_t j=0u;j<sizeof(buffer);++j)
        CHECK((unsigned char)buffer[j]==0x66u);
    return 0;
}

static int test_fixture(const unicode_case *f) {
    const size_t n=strlen(f->markdown),expected=strlen(f->html);
    char *owned=malloc(n+1u);CHECK(owned!=NULL);
    memcpy(owned,f->markdown,n+1u);
    salts_md_document *d=NULL;
    const salts_md_status rc=salts_md_parse(owned,n,256u,n+1u,&d);
    memset(owned,0x39,n);
    free(owned);
    CHECK(rc==SALTS_MD_OK && d!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(d);
    CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==n);
    CHECK(!memcmp(v.raw_markdown,f->markdown,n));
    size_t links=0u;
    for(size_t i=1u;i<v.document.block_count;++i)
        links+=v.document.blocks[i].kind==SALTS_MD_BLOCK_LINK;
    CHECK(links==f->links);
    for(unsigned p=0u;p<2u;++p) {
        const salts_md_html_policy policy=p?SALTS_MD_HTML_SPEC_CONFORMANCE:
            SALTS_MD_HTML_SAFE_APPLICATION;
        size_t measured=SIZE_MAX,reported=SIZE_MAX;
        CHECK(salts_md_render_html_with_policy(&v,256u,policy,
              NULL,0u,&measured)==SALTS_MD_OK && measured==expected);
        char output[1024];CHECK(expected+2u<sizeof(output));
        memset(output,0x47,sizeof(output));
        CHECK(salts_md_render_html_with_policy(&v,256u,policy,
              output,expected,&reported)==SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(reported==expected);
        for(size_t i=0u;i<sizeof(output);++i)
            CHECK((unsigned char)output[i]==0x47u);
        CHECK(salts_md_render_html_with_policy(&v,256u,policy,
              output,expected+1u,&reported)==SALTS_MD_OK &&
              reported==expected);
        CHECK(!memcmp(output,f->html,expected+1u));
        CHECK((unsigned char)output[expected+1u]==0x47u);
    }
    salts_md_document_destroy(d);
    printf("CommonMark #%-3u Unicode 17 candidate: PASS\n",f->official_id);
    return 0;
}

int main(void) {
    if(test_abi())return 1;
    _Static_assert(sizeof(cases)/sizeof(cases[0])==5u,
                   "official three + reverse casefold controls");
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(test_fixture(&cases[i]))return 1;
    return 0;
}
