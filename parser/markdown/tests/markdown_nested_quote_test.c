/* #181: bounded nested-quote RichDoc ownership and actual HTML. */
#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct sample {
    const char *name,*source,*html;
    unsigned quote_nodes,depth;
} sample;
static const sample cases[]={
    {"cm228","> # Foo\n> bar\n> baz\n",
     "<blockquote>\n<h1>Foo</h1>\n<p>bar\nbaz</p>\n</blockquote>\n",1,1},
    {"cm230","   > # Foo\n   > bar\n > baz\n",
     "<blockquote>\n<h1>Foo</h1>\n<p>bar\nbaz</p>\n</blockquote>\n",1,1},
    {"cm236",">     foo\n    bar\n",
     "<blockquote>\n<pre><code>foo\n</code></pre>\n</blockquote>\n<pre><code>bar\n</code></pre>\n",1,1},
    {"cm250","> > > foo\nbar\n",
     "<blockquote>\n<blockquote>\n<blockquote>\n<p>foo\nbar</p>\n</blockquote>\n</blockquote>\n</blockquote>\n",3,3},
    {"fenced","> ~~~c\n> hi\n> ~~~\n",
     "<blockquote>\n<pre><code class=\"language-c\">hi\n</code></pre>\n</blockquote>\n",1,1},
    {"changing-depth","> outer\n> > inner\n> after\n",
     "<blockquote>\n<p>outer</p>\n<blockquote>\n<p>inner\nafter</p>\n</blockquote>\n</blockquote>\n",2,2},
    {"cm251",">>> foo\n> bar\n>>baz\n",
     "<blockquote>\n<blockquote>\n<blockquote>\n<p>foo\nbar\nbaz</p>\n</blockquote>\n</blockquote>\n</blockquote>\n",3,3},
    {"partial-marker-interrupt",">>> foo\n> # Heading\n",
     "<blockquote>\n<blockquote>\n<blockquote>\n<p>foo</p>\n</blockquote>\n</blockquote>\n<h1>Heading</h1>\n</blockquote>\n",3,3},
    {"partial-marker-list",">>> foo\n> - bar\n",
     "<blockquote>\n<blockquote>\n<blockquote>\n<p>foo</p>\n</blockquote>\n</blockquote>\n<ul>\n<li>bar</li>\n</ul>\n</blockquote>\n",3,3},
    {"cm292","> 1. > Blockquote\ncontinued here.\n",
     "<blockquote>\n<ol>\n<li>\n<blockquote>\n<p>Blockquote\ncontinued here.</p>\n</blockquote>\n</li>\n</ol>\n</blockquote>\n",2,2},
    {"cm293","> 1. > Blockquote\n> continued here.\n",
     "<blockquote>\n<ol>\n<li>\n<blockquote>\n<p>Blockquote\ncontinued here.</p>\n</blockquote>\n</li>\n</ol>\n</blockquote>\n",2,2},
    {"inner-quote-marked-heading-interrupt","> 1. > b\n> # Heading\n",
     "<blockquote>\n<ol>\n<li>\n<blockquote>\n<p>b</p>\n</blockquote>\n</li>\n</ol>\n<h1>Heading</h1>\n</blockquote>\n",2,2},
    {"inner-quote-unmarked-heading-interrupt","> 1. > b\n# Heading\n",
     "<blockquote>\n<ol>\n<li>\n<blockquote>\n<p>b</p>\n</blockquote>\n</li>\n</ol>\n</blockquote>\n<h1>Heading</h1>\n",2,2},
    {"inner-quote-blank-stops-lazy","> 1. > b\n\noutside\n",
     "<blockquote>\n<ol>\n<li>\n<blockquote>\n<p>b</p>\n</blockquote>\n</li>\n</ol>\n</blockquote>\n<p>outside</p>\n",2,2}
};
_Static_assert(sizeof(cases)/sizeof(cases[0])==14u,"fixture count");
static int verify(const sample *f) {
    salts_md_document *owner=NULL;
    const size_t input_size=strlen(f->source);
    if(salts_md_parse(f->source,input_size,256u,input_size+1u,&owner)!=
       SALTS_MD_OK || !owner) return 1;
    const salts_md_rich_document_view view=salts_md_get_document_view(owner);
    int fail=salts_md_rich_document_validate(&view,256u)!=SALTS_MD_OK ||
       view.raw_markdown_bytes!=input_size ||
       memcmp(view.raw_markdown,f->source,input_size)!=0;
    unsigned count=0u,maxdepth=0u;
    for(size_t i=1u;i<view.document.block_count&&!fail;++i) {
        if(view.document.blocks[i].kind!=SALTS_MD_BLOCK_QUOTE) continue;
        ++count;
        size_t at=i;unsigned depth=0u;
        while(at) {
            const salts_md_block *b=&view.document.blocks[at];
            if(b->kind==SALTS_MD_BLOCK_QUOTE) ++depth;
            if(b->parent_index>=at) {fail=1;break;}
            at=b->parent_index;
        }
        if(depth>maxdepth)maxdepth=depth;
    }
    if(count!=f->quote_nodes||maxdepth!=f->depth)fail=1;
    size_t needed=0u;
    if(!fail&&salts_md_render_html(&view,256u,NULL,0u,&needed)!=SALTS_MD_OK)fail=1;
    if(!fail&&needed!=strlen(f->html))fail=1;
    if(!fail) {
        char *out=(char*)malloc(needed+2u);
        if(!out)fail=1;
        else {
            memset(out,0xa5,needed+2u);
            size_t measured=0u;
            if(salts_md_render_html(&view,256u,out,needed,&measured)!=
               SALTS_MD_CAPACITY_EXCEEDED || measured!=needed)fail=1;
            for(size_t i=0;i<needed+2u;++i)
                if((unsigned char)out[i]!=0xa5u)fail=1;
            if(!fail&&(salts_md_render_html(&view,256u,out,needed+1u,&measured)
                !=SALTS_MD_OK || measured!=needed ||
                memcmp(out,f->html,needed+1u)!=0 ||
                (unsigned char)out[needed+1u]!=0xa5u))fail=1;
            free(out);
        }
    }
    salts_md_document_destroy(owner);
    if(fail)fprintf(stderr,"Nested quote mismatch: %s\n",f->name);
    return fail;
}
/* A fenced code block belongs to its list item, not the document root. */
static int verify_list_owned_fence(void) {
    static const char source[]="- item\n  ~~~c\n  abc\n  ~~~\n";
    salts_md_document *owner=NULL;
    const size_t bytes=sizeof(source)-1u;
    if(salts_md_parse(source,bytes,256u,bytes+1u,&owner)!=SALTS_MD_OK ||
       !owner) return 1;
    const salts_md_rich_document_view view=salts_md_get_document_view(owner);
    int fail=salts_md_rich_document_validate(&view,256u)!=SALTS_MD_OK;
    size_t fences=0u;
    for(size_t i=1u;i<view.document.block_count&&!fail;++i) {
        if(view.document.blocks[i].kind!=SALTS_MD_BLOCK_CODE_FENCE)continue;
        ++fences;
        const uint32_t parent=view.document.blocks[i].parent_index;
        if(parent>=i ||
           view.document.blocks[parent].kind!=SALTS_MD_BLOCK_LIST_ITEM)
            fail=1;
        size_t payloads=0u;
        for(size_t j=i+1u;j<view.document.block_count;++j) {
            if(view.document.blocks[j].parent_index!=(uint32_t)i)continue;
            const salts_md_block *text=&view.document.blocks[j];
            if(text->kind!=SALTS_MD_BLOCK_TEXT)continue;
            if(text->byte_end<text->byte_begin ||
               text->byte_end>view.document.source_bytes) {
                fail=1;break;
            }
            const size_t n=text->byte_end-text->byte_begin;
            if((n==3u && memcmp(view.document.source+text->byte_begin,"abc",3u)==0) ||
               (n==1u && view.document.source[text->byte_begin]=='\n'))
                ++payloads;
        }
        if(payloads!=2u)fail=1; /* 'abc' and LF are separate owned nodes */
    }
    if(fences!=1u)fail=1;
    salts_md_document_destroy(owner);
    if(fail)fprintf(stderr,"List-owned fence ancestry/payload mismatch\n");
    return fail;
}
/* Verify that the existing list fence frame also works under a quote owner. */
static int verify_quoted_list_fence(void) {
    static const char source[]="> - item\n>   ~~~c\n>   abc\n>   ~~~\n";
    salts_md_document *doc=NULL;
    const size_t n=sizeof(source)-1u;
    if(salts_md_parse(source,n,256u,n+1u,&doc)!=SALTS_MD_OK || !doc)
        return 1;
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    int failed=salts_md_rich_document_validate(&v,256u)!=SALTS_MD_OK ||
               v.raw_markdown_bytes!=n ||
               memcmp(v.raw_markdown,source,n)!=0;
    unsigned fenced=0u,quote_lists=0u;
    for(size_t i=1u;i<v.document.block_count&&!failed;++i) {
        const salts_md_block *b=&v.document.blocks[i];
        if(b->kind==SALTS_MD_BLOCK_LIST) {
            const uint32_t parent=b->parent_index;
            if(parent<i &&
               v.document.blocks[parent].kind==SALTS_MD_BLOCK_QUOTE)
                ++quote_lists;
        }
        if(b->kind==SALTS_MD_BLOCK_CODE_FENCE) {
            ++fenced;
            const uint32_t parent=b->parent_index;
            if(parent>=i ||
               v.document.blocks[parent].kind!=SALTS_MD_BLOCK_LIST_ITEM)
                failed=1;
            else {
                const uint32_t list=v.document.blocks[parent].parent_index;
                if(list>=parent ||
                   v.document.blocks[list].kind!=SALTS_MD_BLOCK_LIST)
                    failed=1;
                else {
                    const uint32_t quote=v.document.blocks[list].parent_index;
                    if(quote>=list ||
                       v.document.blocks[quote].kind!=SALTS_MD_BLOCK_QUOTE)
                        failed=1;
                }
            }
        }
    }
    if(quote_lists!=1u || fenced!=1u) failed=1;
    salts_md_document_destroy(doc);
    if(failed)fprintf(stderr,"Quoted list fence ancestry mismatch\n");
    return failed;
}
int main(void) {
    if(verify_quoted_list_fence())return 1;
    if(verify_list_owned_fence())return 1;
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(verify(&cases[i]))return 1;
    /* The nested quote owns the lazy continuation. With exactly 7 nodes
     * admitted for the first line, publishing its soft-break would exhaust
     * the caller's node budget. No partially owned RichDoc may escape. */
    static const char lazy_limited[]=
        "> 1. > Blockquote\n> continued here.\n";
    salts_md_document *limited_quote=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(lazy_limited,sizeof(lazy_limited)-1u,7u,
                     sizeof(lazy_limited),&limited_quote)!=
       SALTS_MD_CAPACITY_EXCEEDED || limited_quote!=NULL) {
        fprintf(stderr,"nested quoted list lazy budget rollback failed\n");
        return 1;
    }
    /* Every quoted fixture must reject exhausted node/source budgets without
     * publishing a partially constructed document to the caller. */
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i) {
        const size_t bytes=strlen(cases[i].source);
        salts_md_document *limited=(salts_md_document*)(uintptr_t)1u;
        if(salts_md_parse(cases[i].source,bytes,1u,bytes+1u,&limited)!=
           SALTS_MD_CAPACITY_EXCEEDED || limited!=NULL) {
            fprintf(stderr,"Node budget rollback: %s\n",cases[i].name);
            return 1;
        }
        limited=(salts_md_document*)(uintptr_t)1u;
        if(salts_md_parse(cases[i].source,bytes,256u,bytes-1u,&limited)!=
           SALTS_MD_CAPACITY_EXCEEDED || limited!=NULL) {
            fprintf(stderr,"Source budget rollback: %s\n",cases[i].name);
            return 1;
        }
    }
    /* The maximum permitted depth must succeed, not merely depth+1 fail. */
    char boundary[160];size_t boundary_bytes=0u;
    for(size_t i=0u;i<32u;++i) {
        boundary[boundary_bytes++]='>';
        boundary[boundary_bytes++]=' ';
    }
    boundary[boundary_bytes++]='x';
    boundary[boundary_bytes++]='\n';
    salts_md_document *accepted=NULL;
    if(salts_md_parse(boundary,boundary_bytes,256u,boundary_bytes+1u,
                     &accepted)!=SALTS_MD_OK || !accepted) {
        fprintf(stderr,"Depth 32 unexpectedly rejected\n");
        return 1;
    }
    const salts_md_rich_document_view accepted_view=
        salts_md_get_document_view(accepted);
    unsigned accepted_quotes=0u;
    for(size_t i=1u;i<accepted_view.document.block_count;++i)
        if(accepted_view.document.blocks[i].kind==SALTS_MD_BLOCK_QUOTE)
            ++accepted_quotes;
    const int invalid_boundary=
        salts_md_rich_document_validate(&accepted_view,256u)!=SALTS_MD_OK ||
        accepted_view.raw_markdown_bytes!=boundary_bytes ||
        memcmp(accepted_view.raw_markdown,boundary,boundary_bytes)!=0 ||
        accepted_quotes!=32u;
    salts_md_document_destroy(accepted);
    if(invalid_boundary) {
        fprintf(stderr,"Depth 32 ownership/count invalid\n");
        return 1;
    }
    char deep[160];size_t n=0u;
    for(size_t i=0;i<33u;++i){deep[n++]='>';deep[n++]=' ';}
    deep[n++]='x';deep[n++]='\n';
    salts_md_document *owner=(salts_md_document*)(uintptr_t)1u;
    if(salts_md_parse(deep,n,256u,n+1u,&owner)!=
       SALTS_MD_CAPACITY_EXCEEDED || owner!=NULL)return 1;
    printf("Native nested-quote fixtures %zu/%zu, node/source budgets and depth 32/33 boundary PASS\n",
           sizeof(cases)/sizeof(cases[0]),sizeof(cases)/sizeof(cases[0]));
    return 0;
}
