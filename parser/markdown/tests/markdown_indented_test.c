/* #177 native C11 indented code semantics; no alternate Markdown grammar. */
#include <salts/markdown.h>
#include <salts/markdown_html.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(cond) do { \
    if(!(cond)) { \
        fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#cond); \
        return 1; \
    } \
} while(0)

typedef struct indented_fixture {
    const char *id;
    const char *source;
    const char *expected_html;
    const char *expected_code; /* NULL only when there must be no code node */
    size_t expected_code_nodes;
} indented_fixture;

/* Independent fixture descriptions; official numbered cases are checked in
 * Python directly from tools/md/test/spec.txt, without rewriting expected. */
static const indented_fixture cases[] = {
    { "cm-0107", "    a simple\n      indented code block\n",
      "<pre><code>a simple\n  indented code block\n</code></pre>\n",
      "a simple\n  indented code block\n", 1u },
    { "cm-0110", "    <a/>\n    *hi*\n\n    - one\n",
      "<pre><code>&lt;a/&gt;\n*hi*\n\n- one\n</code></pre>\n",
      "<a/>\n*hi*\n\n- one\n", 1u },
    { "cm-0111", "    chunk1\n\n    chunk2\n  \n \n \n    chunk3\n",
      "<pre><code>chunk1\n\nchunk2\n\n\n\nchunk3\n</code></pre>\n",
      "chunk1\n\nchunk2\n\n\n\nchunk3\n", 1u },
    { "cm-0112", "    chunk1\n      \n      chunk2\n",
      "<pre><code>chunk1\n  \n  chunk2\n</code></pre>\n",
      "chunk1\n  \n  chunk2\n", 1u },
    { "cm-0113", "Foo\n    bar\n\n",
      "<p>Foo\nbar</p>\n", NULL, 0u },
    { "cm-0114", "    foo\nbar\n",
      "<pre><code>foo\n</code></pre>\n<p>bar</p>\n", "foo\n", 1u },
    { "cm-0116", "        foo\n    bar\n",
      "<pre><code>    foo\nbar\n</code></pre>\n", "    foo\nbar\n", 1u },
    { "cm-0117", "\n    \n    foo\n    \n\n",
      "<pre><code>foo\n</code></pre>\n", "foo\n", 1u },
    { "cm-0118", "    foo  \n",
      "<pre><code>foo  \n</code></pre>\n", "foo  \n", 1u },
    { "cm-0048", "    ***\n",
      "<pre><code>***\n</code></pre>\n", "***\n", 1u },
    { "cm-0069", "    # foo\n",
      "<pre><code># foo\n</code></pre>\n", "# foo\n", 1u },
    { "cm-0225", "    aaa\nbbb\n",
      "<pre><code>aaa\n</code></pre>\n<p>bbb</p>\n", "aaa\n", 1u },
    { "tab-root", "\tfoo\n",
      "<pre><code>foo\n</code></pre>\n", "foo\n", 1u },
    { "tab-after-column", "   \tbar\n",
      "<pre><code>bar\n</code></pre>\n", "bar\n", 1u },
    { "tab-after-dedent", "\t\tfoo\n",
      "<pre><code>\tfoo\n</code></pre>\n", "\tfoo\n", 1u },
    { "missing-final-newline", "    foo",
      "<pre><code>foo\n</code></pre>\n", "foo\n", 1u },
    { "crlf-normalization", "    <foo>\r\n    bar\r\n",
      "<pre><code>&lt;foo&gt;\nbar\n</code></pre>\n", "<foo>\nbar\n", 1u },
    { "paragraph-not-interrupted", "Foo\n    bar\n",
      "<p>Foo\nbar</p>\n", NULL, 0u },
    { "list-continuation-precedence", "- foo\n    bar\n",
      "<ul>\n<li>foo\nbar</li>\n</ul>\n", NULL, 0u },
    { "tabbed-list-continuation", "- foo\n\tbar\n",
      "<ul>\n<li>foo\nbar</li>\n</ul>\n", NULL, 0u },
    { "tab-cannot-open-atx-inside-paragraph", "Foo\n\t# heading\n",
      "<p>Foo\n# heading</p>\n", NULL, 0u },
    { "tab-cannot-open-thematic-rule-inside-paragraph", "Foo\n\t---\n",
      "<p>Foo\n---</p>\n", NULL, 0u }
};

_Static_assert(sizeof(cases)/sizeof(cases[0])==22u,
               "Indendated-code fixture denominator changed");

static int run_case(const indented_fixture *f) {
    const size_t size=strlen(f->source);
    salts_md_document *owner=NULL;
    REQUIRE(salts_md_parse(f->source,size,256u,size+1u,&owner)==SALTS_MD_OK);
    REQUIRE(owner!=NULL);
    const salts_md_rich_document_view view=salts_md_get_document_view(owner);
    REQUIRE(salts_md_rich_document_validate(&view,256u)==SALTS_MD_OK);
    REQUIRE(view.raw_markdown_bytes==size);
    REQUIRE(!memcmp(view.raw_markdown,f->source,size));

    size_t code_count=0u;
    char combined[2048];
    size_t total=0u;
    for(size_t i=0;i<view.document.block_count;++i) {
        const salts_md_block *node=&view.document.blocks[i];
        REQUIRE(node->stable_id==(uint64_t)i+1u);
        if(node->kind==SALTS_MD_BLOCK_CODE_INDENTED) {
            ++code_count;
            REQUIRE(node->parent_index==0u);
            const char *metadata=NULL;
            size_t metadata_bytes=SIZE_MAX;
            REQUIRE(salts_md_rich_document_metadata(&view,i,&metadata,
                                                   &metadata_bytes)==SALTS_MD_OK);
            REQUIRE(metadata_bytes==0u);
        }
        if(node->kind==SALTS_MD_BLOCK_TEXT && node->parent_index>0u &&
           view.document.blocks[node->parent_index].kind==
           SALTS_MD_BLOCK_CODE_INDENTED) {
            const size_t n=node->byte_end-node->byte_begin;
            REQUIRE(n<=sizeof(combined)-total);
            memcpy(combined+total,view.document.source+node->byte_begin,n);
            total+=n;
        }
    }
    REQUIRE(code_count==f->expected_code_nodes);
    if(f->expected_code) {
        const size_t expected=strlen(f->expected_code);
        REQUIRE(total==expected);
        REQUIRE(!memcmp(combined,f->expected_code,total));
    } else REQUIRE(total==0u);

    size_t required=SIZE_MAX;
    REQUIRE(salts_md_render_html(&view,256u,NULL,0u,&required)==SALTS_MD_OK);
    REQUIRE(required==strlen(f->expected_html));
    char *output=(char*)malloc(required+2u);
    REQUIRE(output!=NULL);
    memset(output,0xa5,required+2u);
    size_t checked=SIZE_MAX;
    if(required) {
        REQUIRE(salts_md_render_html(&view,256u,output,required,&checked)==
                SALTS_MD_CAPACITY_EXCEEDED);
        REQUIRE(checked==required);
        for(size_t j=0;j<required+2u;++j)
            REQUIRE((unsigned char)output[j]==0xa5u);
    }
    REQUIRE(salts_md_render_html(&view,256u,output,required+1u,&checked)==
            SALTS_MD_OK);
    REQUIRE(checked==required);
    REQUIRE(!memcmp(output,f->expected_html,required+1u));
    REQUIRE((unsigned char)output[required+1u]==0xa5u);
    free(output);
    salts_md_document_destroy(owner);
    return 0;
}

int main(void) {
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        if(run_case(&cases[i])) {
            fprintf(stderr,"indented code fixture failed: %s\n",cases[i].id);
            return 1;
        }
    }
    printf("C11 CommonMark indented-code native subset: %zu/%zu PASS\n",
           sizeof(cases)/sizeof(cases[0]),sizeof(cases)/sizeof(cases[0]));
    return 0;
}
