/* #181 bounded root blockquote lazy-continuation conformance.
 * Official example IDs remain independently gated against spec.txt. */
#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct quote_fixture {
    const char *name,*markdown,*html;
    size_t quote_count;
} quote_fixture;

static const quote_fixture cases[]={
    {"cm-233","> bar\nbaz\n> foo\n",
     "<blockquote>\n<p>bar\nbaz\nfoo</p>\n</blockquote>\n",1},
    {"cm-238","> foo\n    - bar\n",
     "<blockquote>\n<p>foo\n- bar</p>\n</blockquote>\n",1},
    {"cm-239",">\n","<blockquote>\n</blockquote>\n",1},
    {"cm-240",">\n>  \n> \n","<blockquote>\n</blockquote>\n",1},
    {"cm-241",">\n> foo\n>  \n",
     "<blockquote>\n<p>foo</p>\n</blockquote>\n",1},
    {"cm-244","> foo\n>\n> bar\n",
     "<blockquote>\n<p>foo</p>\n<p>bar</p>\n</blockquote>\n",1},
    {"cm-247","> bar\nbaz\n",
     "<blockquote>\n<p>bar\nbaz</p>\n</blockquote>\n",1},
    {"cm-249","> bar\n>\nbaz\n",
     "<blockquote>\n<p>bar</p>\n</blockquote>\n<p>baz</p>\n",1},
    {"cm-234","> foo\n---\n",
     "<blockquote>\n<p>foo</p>\n</blockquote>\n<hr />\n",1},
    {"cm-245","foo\n> bar\n",
     "<p>foo</p>\n<blockquote>\n<p>bar</p>\n</blockquote>\n",1},
    {"cm-246","> aaa\n***\n> bbb\n",
     "<blockquote>\n<p>aaa</p>\n</blockquote>\n<hr />\n<blockquote>\n<p>bbb</p>\n</blockquote>\n",2},
    {"cm-248","> bar\n\nbaz\n",
     "<blockquote>\n<p>bar</p>\n</blockquote>\n<p>baz</p>\n",1},
    {"unordered-interrupt","> one\n- two\n",
     "<blockquote>\n<p>one</p>\n</blockquote>\n<ul>\n<li>two</li>\n</ul>\n",1},
    {"ordered-noninterrupt","> one\n2. two\n",
     "<blockquote>\n<p>one\n2. two</p>\n</blockquote>\n",1},
    {"ordered-interrupt","> one\n1. two\n",
     "<blockquote>\n<p>one</p>\n</blockquote>\n<ol>\n<li>two</li>\n</ol>\n",1},
};
_Static_assert(sizeof(cases)/sizeof(cases[0])==15u,
               "Quote continuation fixture count changed");

static int check_fixture(const quote_fixture *f) {
    salts_md_document *doc=NULL;
    const size_t len=strlen(f->markdown);
    if(salts_md_parse(f->markdown,len,256u,len+1u,&doc)!=SALTS_MD_OK ||
       !doc) {fprintf(stderr,"%s: failed parse\n",f->name);return 1;}
    const salts_md_rich_document_view view=salts_md_get_document_view(doc);
    int failed=0;size_t quoted=0,needed=0;
    if(salts_md_rich_document_validate(&view,256u)!=SALTS_MD_OK ||
       view.raw_markdown_bytes!=len ||
       memcmp(view.raw_markdown,f->markdown,len)!=0) failed=1;
    for(size_t i=0;i<view.document.block_count&&!failed;++i) {
        if(view.document.blocks[i].kind!=SALTS_MD_BLOCK_QUOTE) continue;
        ++quoted;
        if(view.document.blocks[i].parent_index!=0u) failed=1;
    }
    if(quoted!=f->quote_count) failed=1;
    if(!failed && salts_md_render_html(&view,256u,NULL,0u,&needed)
            !=SALTS_MD_OK) failed=1;
    if(!failed && needed!=strlen(f->html)) failed=1;
    if(!failed) {
        char *output=(char*)malloc(needed+2u);
        if(!output) failed=1;
        else {
            memset(output,0xa5,needed+2u);
            size_t measured=0;
            if(salts_md_render_html(&view,256u,output,needed,&measured)
                    !=SALTS_MD_CAPACITY_EXCEEDED || measured!=needed)
                failed=1;
            for(size_t k=0;k<needed+2u;++k)
                if((unsigned char)output[k]!=0xa5u) failed=1;
            if(!failed &&
               salts_md_render_html(&view,256u,output,needed+1u,&measured)
                   !=SALTS_MD_OK) failed=1;
            if(!failed && (measured!=needed ||
               memcmp(output,f->html,needed+1u)!=0 ||
               (unsigned char)output[needed+1u]!=0xa5u)) failed=1;
            /* A failed writer might not have NUL-terminated output;
             * never treat a partial/sentinel buffer as a C string. */
            if(failed)
                fprintf(stderr,"Quote fixture %s: HTML/atomic output mismatch\n",
                        f->name);
            free(output);
        }
    }
    salts_md_document_destroy(doc);
    return failed;
}
int main(void) {
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(check_fixture(&cases[i])) {
            fprintf(stderr,"FAILED %s\n",cases[i].name);
            return 1;
        }
    printf("Quote lazy-continuation fixtures: %zu/%zu PASS\n",
      sizeof(cases)/sizeof(cases[0]),sizeof(cases)/sizeof(cases[0]));
    return 0;
}
