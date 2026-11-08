#include <salts/markdown.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#condition); \
        return 1; \
    } \
} while (0)

static size_t kind_count(salts_md_rich_document_view v,salts_md_block_kind kind) {
    size_t result=0u;
    for(size_t i=0u;i<v.document.block_count;++i)
        result+=v.document.blocks[i].kind==kind;
    return result;
}

static bool has_text(salts_md_rich_document_view v,const char *expected) {
    const size_t n=strlen(expected);
    for(size_t i=0u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        if(node->kind==SALTS_MD_BLOCK_TEXT &&
           node->byte_end>=node->byte_begin &&
           node->byte_end<=v.document.source_bytes &&
           node->byte_end-node->byte_begin==n &&
           memcmp(v.document.source+node->byte_begin,expected,n)==0)
            return true;
    }
    return false;
}

/* The normalized soft break belongs to the actual nested emphasis owner
 * while raw Markdown (including CRLF) remains exactly the original bytes. */
static int check_multiline(const char *source,const char *before,
                           const char *after,size_t emphasis,size_t strong,
                           salts_md_block_kind break_owner) {
    const size_t n=strlen(source);
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(source,n,128u,n+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==n);
    CHECK(memcmp(v.raw_markdown,source,n)==0);
    CHECK(kind_count(v,SALTS_MD_BLOCK_EMPHASIS)==emphasis);
    CHECK(kind_count(v,SALTS_MD_BLOCK_STRONG)==strong);
    CHECK(kind_count(v,SALTS_MD_BLOCK_LINE_BREAK)==1u);
    CHECK(has_text(v,before));
    CHECK(has_text(v,after));
    bool owned_break=false;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        CHECK(node->parent_index<i);
        if(node->kind==SALTS_MD_BLOCK_LINE_BREAK &&
           v.document.blocks[node->parent_index].kind==break_owner)
            owned_break=true;
    }
    CHECK(owned_break);
    salts_md_document_destroy(doc);
    return 0;
}

/* #185: hard breaks stay inside the Emphasis/Strong owner after pairing
 * across source LF/CRLF. Source-origin metadata is typed, bounded and owned,
 * never reconstructed from semantic TEXT offsets. */
static int check_crossline_hard(const char *source,uint8_t origin,
                                 salts_md_block_kind inline_owner) {
    const size_t n=strlen(source);
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(source,n,128u,n+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==n);
    CHECK(memcmp(v.raw_markdown,source,n)==0);
    CHECK(kind_count(v,SALTS_MD_BLOCK_HARD_LINE_BREAK)==1u);
    CHECK(kind_count(v,SALTS_MD_BLOCK_LINE_BREAK)==0u);
    CHECK(has_text(v,"foo"));
    CHECK(has_text(v,"bar"));
    bool owned=false;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        CHECK(node->parent_index<i);
        if(node->kind!=SALTS_MD_BLOCK_HARD_LINE_BREAK)continue;
        CHECK(v.document.blocks[node->parent_index].kind==inline_owner);
        const char *bytes=NULL;size_t size=SIZE_MAX;
        CHECK(salts_md_rich_document_metadata(&v,i,&bytes,&size)==SALTS_MD_OK);
        CHECK(size==sizeof(salts_md_line_break_semantics));
        salts_md_line_break_semantics meta;
        memcpy(&meta,bytes,sizeof(meta));
        CHECK(meta.origin==origin);
        CHECK(!meta.reserved[0]&&!meta.reserved[1]&&!meta.reserved[2]);
        owned=true;
    }
    CHECK(owned);
    salts_md_document_destroy(doc);
    return 0;
}

/* #185: when an inner strong/em/link closes before two trailing spaces,
 * the HARD break still belongs to the OUTER inline owner. Its provenance
 * comes from the whole physical line rather than a residual text fragment. */
static int check_hard_after_nested_inline(
    const char *source,salts_md_block_kind outer,
    salts_md_block_kind inner,uint8_t origin) {
    const size_t n=strlen(source);
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(source,n,128u,n+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==n);
    CHECK(memcmp(v.raw_markdown,source,n)==0);
    CHECK(kind_count(v,inner)==1u);
    CHECK(kind_count(v,SALTS_MD_BLOCK_HARD_LINE_BREAK)==1u);
    CHECK(has_text(v,"baz"));
    bool inner_owned=false,hard_owned=false;
    for(size_t i=1u;i<v.document.block_count;++i) {
        const salts_md_block *node=&v.document.blocks[i];
        CHECK(node->parent_index<i);
        const salts_md_block_kind parent_kind=
            v.document.blocks[node->parent_index].kind;
        if(node->kind==inner && parent_kind==outer)inner_owned=true;
        if(node->kind!=SALTS_MD_BLOCK_HARD_LINE_BREAK)continue;
        CHECK(parent_kind==outer);
        const char *meta=NULL;size_t len=SIZE_MAX;
        CHECK(salts_md_rich_document_metadata(&v,i,&meta,&len)==SALTS_MD_OK);
        CHECK(len==sizeof(salts_md_line_break_semantics));
        salts_md_line_break_semantics sem;
        memcpy(&sem,meta,sizeof(sem));
        CHECK(sem.origin==origin);
        CHECK(!sem.reserved[0]&&!sem.reserved[1]&&!sem.reserved[2]);
        hard_owned=true;
    }
    CHECK(inner_owned && hard_owned);
    salts_md_document_destroy(doc);
    return 0;
}

int main(void) {
    CHECK(check_multiline(
        "**Gomphocarpus (*Gomphocarpus physocarpus*, syn.\n"
        "*Asclepias physocarpa*)**\n",
        "Gomphocarpus (","Asclepias physocarpa",2u,1u,
        SALTS_MD_BLOCK_STRONG)==0); /* #394 */
    CHECK(check_multiline("*foo\nbar*\n","foo","bar",1u,0u,
                          SALTS_MD_BLOCK_EMPHASIS)==0); /* #405 */
    CHECK(check_multiline("**foo\nbar**\n","foo","bar",0u,1u,
                          SALTS_MD_BLOCK_STRONG)==0); /* #423 */
    CHECK(check_multiline("**foo *bar **baz**\nbim* bop**\n",
                          "baz","bim",1u,2u,
                          SALTS_MD_BLOCK_EMPHASIS)==0); /* #432 */
    CHECK(check_multiline("*foo\r\nbar*\r\n","foo","bar",1u,0u,
                          SALTS_MD_BLOCK_EMPHASIS)==0);
    CHECK(check_multiline("Foo *bar\nbaz*\n====\n","Foo ","baz",
                          1u,0u,SALTS_MD_BLOCK_EMPHASIS)==0); /* #81 */
    CHECK(check_multiline("  Foo *bar\nbaz*\t\n====\n","Foo ","baz",
                          1u,0u,SALTS_MD_BLOCK_EMPHASIS)==0); /* #82 */
    CHECK(check_crossline_hard("*foo  \nbar*\n",
                               SALTS_MD_LINE_BREAK_TRAILING_SPACES,
                               SALTS_MD_BLOCK_EMPHASIS)==0); /* #638 */
    CHECK(check_crossline_hard("*foo\\\nbar*\n",
                               SALTS_MD_LINE_BREAK_BACKSLASH,
                               SALTS_MD_BLOCK_EMPHASIS)==0); /* #639 */
    CHECK(check_crossline_hard("**foo  \nbar**\n",
                               SALTS_MD_LINE_BREAK_TRAILING_SPACES,
                               SALTS_MD_BLOCK_STRONG)==0);
    CHECK(check_crossline_hard("__foo\\\nbar__\n",
                               SALTS_MD_LINE_BREAK_BACKSLASH,
                               SALTS_MD_BLOCK_STRONG)==0);
    CHECK(check_crossline_hard("*foo\\\r\nbar*\r\n",
                               SALTS_MD_LINE_BREAK_BACKSLASH,
                               SALTS_MD_BLOCK_EMPHASIS)==0);
    CHECK(check_hard_after_nested_inline("*foo **bar**  \nbaz*\n",
           SALTS_MD_BLOCK_EMPHASIS,SALTS_MD_BLOCK_STRONG,
           SALTS_MD_LINE_BREAK_TRAILING_SPACES)==0);
    CHECK(check_hard_after_nested_inline("*foo [bar](url)  \nbaz*\n",
           SALTS_MD_BLOCK_EMPHASIS,SALTS_MD_BLOCK_LINK,
           SALTS_MD_LINE_BREAK_TRAILING_SPACES)==0);
    CHECK(check_hard_after_nested_inline("**foo _bar_  \nbaz**\n",
           SALTS_MD_BLOCK_STRONG,SALTS_MD_BLOCK_EMPHASIS,
           SALTS_MD_LINE_BREAK_TRAILING_SPACES)==0);
    CHECK(check_hard_after_nested_inline("*foo **bar**\\\nbaz*\n",
           SALTS_MD_BLOCK_EMPHASIS,SALTS_MD_BLOCK_STRONG,
           SALTS_MD_LINE_BREAK_BACKSLASH)==0);


    /* These are now VALID cross-line soft emphasis pairs rather than
     * negative boundary fixtures after #185 normalizes their whitespace. */
    CHECK(check_multiline("*foo \nbar*\n","foo","bar",
                          1u,0u,SALTS_MD_BLOCK_EMPHASIS)==0);
    CHECK(check_multiline("*foo\nbar*\t\n","foo","bar",
                          1u,0u,SALTS_MD_BLOCK_EMPHASIS)==0);
    /* Only genuine root paragraph interruptions are negative: a blank
     * line, an ATX heading, and a quote marker. */
    static const char *barriers[]={
        "*foo\n\nbar*\n",
        "*foo\n# Heading\nbar*\n",
        "*foo\n> quote*\n"
    };
    _Static_assert(sizeof(barriers)/sizeof(barriers[0])==3u,
                   "cross-block barrier fixture count");
    for(size_t i=0u;i<sizeof(barriers)/sizeof(barriers[0]);++i) {
        const size_t n=strlen(barriers[i]);
        salts_md_document *doc=NULL;
        CHECK(salts_md_parse(barriers[i],n,128u,n+1u,&doc)==SALTS_MD_OK);
        CHECK(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        CHECK(kind_count(v,SALTS_MD_BLOCK_EMPHASIS)==0u);
        CHECK(v.raw_markdown_bytes==n);
        CHECK(memcmp(v.raw_markdown,barriers[i],n)==0);
        salts_md_document_destroy(doc);
    }

    const char limited[]="**foo\nbar**\n";
    salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse(limited,sizeof(limited)-1u,5u,
                        sizeof(limited),&rejected)==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(rejected==NULL);
    const char hard_limited[]="*foo  \nbar*\n";
    rejected=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse(hard_limited,sizeof(hard_limited)-1u,5u,
                        sizeof(hard_limited),&rejected)==
          SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(rejected==NULL);
    puts("C11 Markdown root paragraph inline ownership: 9/9 positive, "
         "3/3 real block barriers, 4/4 nested hard origins and atomic soft/hard node-budget rejection");
    return 0;
}
