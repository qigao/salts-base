#include <salts/markdown.h>
#include <salts/markdown_html.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr); \
        return 1; \
    } \
} while (0)

typedef struct break_case {
    const char *markdown;
    const char *html;
    const char *first_text;
    const char *second_text;
    uint8_t origin;
} break_case;

static int verify(const break_case *c) {
    const size_t n=strlen(c->markdown);
    salts_md_document *owned=NULL;
    CHECK(salts_md_parse(c->markdown,n,128u,n+1u,&owned)==SALTS_MD_OK);
    CHECK(owned!=NULL);
    salts_md_rich_document_view v=salts_md_get_document_view(owned);
    CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==n);
    CHECK(memcmp(v.raw_markdown,c->markdown,n)==0);
    size_t hard_count=0u,soft_count=0u,first=0u,second=0u;
    size_t break_index=SIZE_MAX;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        CHECK(node->parent_index<i);
        if(node->kind==SALTS_MD_BLOCK_HARD_LINE_BREAK ||
           node->kind==SALTS_MD_BLOCK_LINE_BREAK) {
            if(node->kind==SALTS_MD_BLOCK_HARD_LINE_BREAK)
                ++hard_count;
            else ++soft_count;
            break_index=i;
            CHECK(v.document.blocks[node->parent_index].kind==
                  SALTS_MD_BLOCK_PARAGRAPH);
            const char *meta=NULL;
            size_t meta_size=SIZE_MAX;
            CHECK(salts_md_rich_document_metadata(&v,i,&meta,&meta_size)==
                  SALTS_MD_OK);
            if(c->origin) {
                CHECK(meta_size==sizeof(salts_md_line_break_semantics));
                salts_md_line_break_semantics info={0};
                memcpy(&info,meta,sizeof(info));
                CHECK(info.origin==c->origin);
                CHECK(info.reserved[0]==0u && info.reserved[1]==0u &&
                      info.reserved[2]==0u);
            } else CHECK(meta_size==0u);
        }
        if(node->kind==SALTS_MD_BLOCK_TEXT) {
            CHECK(node->byte_end>=node->byte_begin);
            CHECK(node->byte_end<=v.document.source_bytes);
            const char *data=v.document.source+node->byte_begin;
            const size_t size=node->byte_end-node->byte_begin;
            if(c->first_text && size==strlen(c->first_text) &&
               !memcmp(data,c->first_text,size)) ++first;
            if(c->second_text && size==strlen(c->second_text) &&
               !memcmp(data,c->second_text,size)) ++second;
        }
    }
    CHECK(hard_count==(c->origin!=0u?1u:0u));
    CHECK(soft_count==(c->origin==0u && c->second_text!=NULL?1u:0u));
    if(c->first_text) CHECK(first>=1u);
    if(c->second_text) CHECK(second>=1u);
    size_t needed=SIZE_MAX;
    CHECK(salts_md_render_html(&v,128u,NULL,0u,&needed)==SALTS_MD_OK);
    CHECK(needed==strlen(c->html));
    char output[256];
    CHECK(needed+2u<=sizeof(output));
    memset(output,0xa7,sizeof(output));
    size_t reported=SIZE_MAX;
    if(needed) {
        CHECK(salts_md_render_html(&v,128u,output,needed,&reported)==
              SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(reported==needed);
        for(size_t i=0u;i<sizeof(output);++i)
            CHECK((unsigned char)output[i]==0xa7);
    }
    CHECK(salts_md_render_html(&v,128u,output,needed+1u,&reported)==
          SALTS_MD_OK);
    CHECK(reported==needed);
    CHECK(memcmp(output,c->html,needed+1u)==0);
    CHECK((unsigned char)output[needed+1u]==0xa7);

    if(c->origin) {
        CHECK(break_index!=SIZE_MAX);
        /* The HTML emitter must reject forged metadata rather than inferring
         * or blindly trusting a hard-break origin; failure is output-atomic. */
        CHECK(v.metadata_bytes>0u);
        char *metadata=(char*)malloc(v.metadata_bytes);
        CHECK(metadata!=NULL);
        memcpy(metadata,v.metadata,v.metadata_bytes);
        const salts_md_block_metadata span=v.block_metadata[break_index];
        CHECK(span.byte_end-span.byte_begin==
              sizeof(salts_md_line_break_semantics));
        const salts_md_rich_document_view bad={
            .document=v.document,
            .metadata=metadata,
            .metadata_bytes=v.metadata_bytes,
            .block_metadata=v.block_metadata,
            .block_metadata_count=v.block_metadata_count,
            .raw_markdown=v.raw_markdown,
            .raw_markdown_bytes=v.raw_markdown_bytes
        };
        metadata[span.byte_begin]=(char)0xff;
        memset(output,0xa7,sizeof(output));
        CHECK(salts_md_render_html(&bad,128u,output,sizeof(output),
                                  &reported)==SALTS_MD_INVALID_ARGUMENT);
        for(size_t i=0u;i<sizeof(output);++i)
            CHECK((unsigned char)output[i]==0xa7);
        memcpy(metadata,v.metadata,v.metadata_bytes);
        metadata[span.byte_begin+1u]=1;
        CHECK(salts_md_render_html(&bad,128u,NULL,0u,&reported)==
              SALTS_MD_INVALID_ARGUMENT);
        free(metadata);
    }
    salts_md_document_destroy(owned);
    return 0;
}

int main(void) {
    static const break_case cases[]={
        {"foo  \nbaz\n","<p>foo<br />\nbaz</p>\n","foo","baz",
         SALTS_MD_LINE_BREAK_TRAILING_SPACES},                /* #633 */
        {"foo\\\nbaz\n","<p>foo<br />\nbaz</p>\n","foo","baz",
         SALTS_MD_LINE_BREAK_BACKSLASH},                      /* #634 */
        {"foo       \nbaz\n","<p>foo<br />\nbaz</p>\n","foo","baz",
         SALTS_MD_LINE_BREAK_TRAILING_SPACES},                /* #635 */
        {"foo  \n     bar\n","<p>foo<br />\nbar</p>\n","foo","bar",
         SALTS_MD_LINE_BREAK_TRAILING_SPACES},                /* #636 */
        {"foo\\\n     bar\n","<p>foo<br />\nbar</p>\n","foo","bar",
         SALTS_MD_LINE_BREAK_BACKSLASH},                      /* #637 */
        {"foo \n baz\n","<p>foo\nbaz</p>\n","foo","baz",0u},/* #649 */
        {"foo\\\\\nbaz\n","<p>foo\\\nbaz</p>\n","\\","baz",0u},
        {"foo  \n","<p>foo</p>\n","foo",NULL,0u},         /* #645 */
        {"foo\\\n","<p>foo\\</p>\n","foo\\",NULL,0u}
    };
    _Static_assert(sizeof(cases)/sizeof(cases[0])==9u,
                   "hard/soft break and EOF fixture denominator");
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(verify(&cases[i])) {
            fprintf(stderr,"break fixture %zu failed\n",i);
            return 1;
        }
    const char limited[]="foo  \nbaz\n";
    salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse(limited,sizeof(limited)-1u,4u,
                        sizeof(limited),&rejected)==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(rejected==NULL);
    puts("C11 Markdown typed break semantics: 9/9 fixtures, metadata "
         "validation, atomic HTML and node-budget rollback");
    return 0;
}
