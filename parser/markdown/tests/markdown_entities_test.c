#include <salts/markdown.h>
#include <salts/markdown_html.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TICK "\x60"
#define CHECK(test_) do { \
    if (!(test_)) { \
        fprintf(stderr,"%s:%d: failed: %s\n",__FILE__,__LINE__,#test_); \
        return 1; \
    } \
} while (0)

typedef struct md_entity_fixture {
    const char *source,*html;
    const char *decoded_text;
    size_t emphasis, code_spans;
} md_entity_fixture;

static size_t count_kind(salts_md_rich_document_view v,
                         salts_md_block_kind kind) {
    size_t count=0u;
    for(size_t i=0u;i<v.document.block_count;++i)
        count+=(v.document.blocks[i].kind==kind);
    return count;
}

static bool has_text(salts_md_rich_document_view v,const char *needle,
                     salts_md_block_kind parent) {
    const size_t length=strlen(needle);
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        if(node->kind!=SALTS_MD_BLOCK_TEXT || node->parent_index>=i ||
           v.document.blocks[node->parent_index].kind!=parent ||
           node->byte_end<node->byte_begin ||
           node->byte_end>v.document.source_bytes ||
           node->byte_end-node->byte_begin!=length)
            continue;
        if(!memcmp(v.document.source+node->byte_begin,needle,length))
            return true;
    }
    return false;
}

static int check_entity(const md_entity_fixture *fixture) {
    const size_t raw_bytes=strlen(fixture->source);
    salts_md_document *owner=NULL;
    CHECK(salts_md_parse(fixture->source,raw_bytes,256u,raw_bytes+1u,
                        &owner)==SALTS_MD_OK);
    CHECK(owner!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(owner);
    CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==raw_bytes);
    CHECK(memcmp(v.raw_markdown,fixture->source,raw_bytes)==0);
    CHECK(count_kind(v,SALTS_MD_BLOCK_EMPHASIS)==fixture->emphasis);
    CHECK(count_kind(v,SALTS_MD_BLOCK_CODE_SPAN)==fixture->code_spans);
    if(fixture->decoded_text)
        CHECK(has_text(v,fixture->decoded_text,
              fixture->code_spans?SALTS_MD_BLOCK_CODE_SPAN:
              fixture->emphasis?SALTS_MD_BLOCK_EMPHASIS:
              SALTS_MD_BLOCK_PARAGRAPH));

    /* Decoded semantic TEXT byte ranges index the owned UTF-8 arena, never
     * the raw Markdown source that is held separately for diagnostics. */
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        CHECK(node->parent_index<i);
        if(node->kind!=SALTS_MD_BLOCK_TEXT) continue;
        CHECK(node->byte_end>=node->byte_begin);
        CHECK(node->byte_end<=v.document.source_bytes);
    }
    size_t required=SIZE_MAX;
    CHECK(salts_md_render_html(&v,256u,NULL,0u,&required)==SALTS_MD_OK);
    const size_t expected=strlen(fixture->html);
    CHECK(required==expected);
    char output[1024];
    CHECK(expected+2u<=sizeof(output));
    memset(output,0xA5,sizeof(output));
    size_t reported=SIZE_MAX;
    if(expected) {
        CHECK(salts_md_render_html(&v,256u,output,expected,&reported)==
              SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(reported==expected);
        for(size_t i=0u;i<sizeof(output);++i)
            CHECK((unsigned char)output[i]==0xA5u);
    }
    CHECK(salts_md_render_html(&v,256u,output,expected+1u,&reported)==
          SALTS_MD_OK);
    CHECK(reported==expected);
    CHECK(memcmp(output,fixture->html,expected+1u)==0);
    CHECK((unsigned char)output[expected+1u]==0xA5u);
    salts_md_document_destroy(owner);
    return 0;
}

/* #185: fence INFO is decoded metadata; fenced payload stays literal code.
 * Raw Markdown byte offsets are never reused as decoded metadata offsets. */
static int check_fence_metadata(const char *source,const char *expected) {
    const size_t n=strlen(source);
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(source,n,256u,n+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    const salts_md_rich_document_view view=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&view,256u)==SALTS_MD_OK);
    CHECK(view.raw_markdown_bytes==n);
    CHECK(memcmp(view.raw_markdown,source,n)==0);
    size_t found=0u;
    for(size_t i=1u;i<view.document.block_count;++i) {
        const salts_md_block *node=&view.document.blocks[i];
        if(node->kind!=SALTS_MD_BLOCK_CODE_FENCE)continue;
        CHECK(node->parent_index<i);
        const char *metadata=NULL;
        size_t length=SIZE_MAX;
        CHECK(salts_md_rich_document_metadata(&view,i,&metadata,&length)==
              SALTS_MD_OK);
        const size_t want=strlen(expected);
        CHECK(length==want);
        CHECK(want==0u || (metadata &&
              !memcmp(metadata,expected,want)));
        ++found;
    }
    CHECK(found==1u);
    salts_md_document_destroy(doc);
    return 0;
}

static int test_fence_entities_and_expanding_utf8(void) {
    CHECK(check_fence_metadata(
        TICK TICK TICK " f&ouml;&ouml;\nfoo\n" TICK TICK TICK "\n",
        "föö")==0); /* CommonMark #34 */
    CHECK(check_fence_metadata(
        TICK TICK TICK " foo\\+bar\nfoo\n" TICK TICK TICK "\n",
        "foo+bar")==0); /* CommonMark #24 */
    CHECK(check_fence_metadata(
        "> " TICK TICK TICK " f&ouml;\n> body\n> " TICK TICK TICK "\n",
        "fö")==0);
    CHECK(check_fence_metadata(
        "- " TICK TICK TICK " f&nGt;\n  body\n  " TICK TICK TICK "\n",
        "f≫⃒")==0);
    CHECK(check_fence_metadata(
        TICK TICK TICK " \\&copy;\nbody\n" TICK TICK TICK "\n",
        "&copy;")==0);

    /* HTML5 alias expansion from five RAW bytes to six decoded UTF-8 bytes
     * must not invalidate owned semantic text spans or metadata. */
    const char expanded[]="&nGt;&nGt;&nLt;&nLt;\n";
    const size_t size=sizeof(expanded)-1u;
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(expanded,size,256u,size+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    CHECK(v.document.source_bytes>v.raw_markdown_bytes);
    CHECK(v.raw_markdown_bytes==size);
    CHECK(memcmp(v.raw_markdown,expanded,size)==0);
    CHECK(has_text(v,"≫⃒",SALTS_MD_BLOCK_PARAGRAPH));
    CHECK(has_text(v,"≪⃒",SALTS_MD_BLOCK_PARAGRAPH));
    salts_md_document_destroy(doc);

    char source[512],semantic[512];
    size_t raw=0u,decoded=0u;
    const char *prefix=TICK TICK TICK " ";
    memcpy(source+raw,prefix,4u);raw+=4u;
    for(size_t i=0u;i<40u;++i) {
        const char raw_entity[]="&nGt;";
        const char decoded_entity[]="≫⃒";
        CHECK(raw+sizeof(raw_entity)-1u<sizeof(source));
        CHECK(decoded+sizeof(decoded_entity)-1u<sizeof(semantic));
        memcpy(source+raw,raw_entity,sizeof(raw_entity)-1u);
        raw+=sizeof(raw_entity)-1u;
        memcpy(semantic+decoded,decoded_entity,sizeof(decoded_entity)-1u);
        decoded+=sizeof(decoded_entity)-1u;
    }
    const char suffix[]="\nbody\n" TICK TICK TICK "\n";
    CHECK(raw+sizeof(suffix)<=sizeof(source));
    memcpy(source+raw,suffix,sizeof(suffix));
    raw+=sizeof(suffix)-1u;
    semantic[decoded]='\0';
    CHECK(raw<decoded);
    CHECK(check_fence_metadata(source,semantic)==0);
    return 0;
}

int main(void) {
    static const md_entity_fixture cases[]={
        /* Official CommonMark 0.31.2 normative character refs. */
        {"&nbsp; &amp; &copy; &AElig; &Dcaron;\n"
         "&frac34; &HilbertSpace; &DifferentialD;\n"
         "&ClockwiseContourIntegral; &ngE;\n",
         "<p>  &amp; © Æ Ď\n¾ ℋ ⅆ\n∲ ≧̸</p>\n",
         NULL,0u,0u},                                      /* #25 */
        {"&#35; &#1234; &#992; &#0;\n",
         "<p># Ӓ Ϡ �</p>\n",NULL,0u,0u},                 /* #26 */
        {"&#X22; &#XD06; &#xcab;\n",
         "<p>&quot; ആ ಫ</p>\n",NULL,0u,0u},          /* #27 */
        {"&nbsp &x; &#; &#x;\n&#87654321;\n&#abcdef0;\n"
         "&ThisIsNotDefined; &hi?;\n",
         "<p>&amp;nbsp &amp;x; &amp;#; &amp;#x;\n"
         "&amp;#87654321;\n&amp;#abcdef0;\n"
         "&amp;ThisIsNotDefined; &amp;hi?;</p>\n",
         NULL,0u,0u},                                      /* #28 */
        {"&copy\n","<p>&amp;copy</p>\n",NULL,0u,0u},      /* #29 */
        {"&MadeUpEntity;\n","<p>&amp;MadeUpEntity;</p>\n",
         NULL,0u,0u},                                      /* #30 */
        {TICK "f&ouml;&ouml;" TICK "\n",
         "<p><code>f&amp;ouml;&amp;ouml;</code></p>\n",
         "f&ouml;&ouml;",0u,1u},                       /* #35 */
        {"&#42;foo&#42;\n*foo*\n",
         "<p>*foo*\n<em>foo</em></p>\n",NULL,1u,0u},     /* #37 */
        {"foo&#10;&#10;bar\n","<p>foo\n\nbar</p>\n",
         "\n",0u,0u},                                       /* #39 */
        {"&#9;foo\n","<p>\tfoo</p>\n",
         "\t",0u,0u},                                       /* #40 */
        {"&ngE;\n","<p>≧̸</p>\n","≧̸",0u,0u},
        {"&NotEqualTilde;\n","<p>≂̸</p>\n","≂̸",0u,0u},
        {"&#x80; &#x9f;\n","<p>€ Ÿ</p>\n",
         "€",0u,0u},                                       /* C1 remap */
        {"&#xD800; &#x110000; &#9999999;\n",
         "<p>� � �</p>\n","�",0u,0u},
        {"\\&copy; \\&#35;\n","<p>&amp;copy; &amp;#35;</p>\n",
         NULL,0u,0u},
        {"*x &copy; y*\n","<p><em>x © y</em></p>\n",
         "©",1u,0u},
        {"[&copy;](url)\n","<p><a href=\"url\">©</a></p>\n",
         NULL,0u,0u},
        {"&#x26;lt;\n","<p>&amp;lt;</p>\n","&",0u,0u},
        {"&amp;copy;\n","<p>&amp;copy;</p>\n","&",0u,0u},
        {TICK "&#42;" TICK "\n",
         "<p><code>&amp;#42;</code></p>\n",
         "&#42;",0u,1u}
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==20u,
                   "C11 entity reference AST/HTML fixture denominator");
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i) {
        if(check_entity(&cases[i])) {
            fprintf(stderr,"Entity reference case %zu failed\n",i);
            return 1;
        }
    }
    if(test_fence_entities_and_expanding_utf8())return 1;
    const char limited[]="*A &ngE; B*\n";
    salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse(limited,sizeof(limited)-1u,3u,
                        sizeof(limited),&rejected)==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(rejected==NULL);

    /* Unknown names remain bounded ordinary text, with no recursive
     * interpretation or permissive fallback name grammar. */
    char repeated[1024];
    size_t used=0u;
    for(size_t i=0u;i<40u;++i) {
        const char item[]="&Unknown;";
        memcpy(repeated+used,item,sizeof(item)-1u);
        used+=sizeof(item)-1u;
    }
    repeated[used++]='\n';
    CHECK(used<sizeof(repeated));
    salts_md_document *document=NULL;
    CHECK(salts_md_parse(repeated,used,128u,used+1u,&document)==SALTS_MD_OK);
    CHECK(document!=NULL);
    const salts_md_rich_document_view view=salts_md_get_document_view(document);
    CHECK(salts_md_rich_document_validate(&view,128u)==SALTS_MD_OK);
    CHECK(view.raw_markdown_bytes==used);
    CHECK(memcmp(view.raw_markdown,repeated,used)==0);
    salts_md_document_destroy(document);
    puts("C11 HTML5 entities: 20/20 AST/HTML, fence info, bounded UTF-8 growth and rollback");
    return 0;
}
