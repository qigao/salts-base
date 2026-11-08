/* #181: quote/list ownership and byte-exact CommonMark HTML. */
#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {const char *id,*input,*html;unsigned quotes,lists;} fixture;
static const fixture cases[]={
  {"cm235","> - foo\n- bar\n","<blockquote>\n<ul>\n<li>foo</li>\n</ul>\n</blockquote>\n<ul>\n<li>bar</li>\n</ul>\n",1,2},
  {"quoted-siblings","> - foo\n> - bar\n","<blockquote>\n<ul>\n<li>foo</li>\n<li>bar</li>\n</ul>\n</blockquote>\n",1,1},
  {"quoted-ordered","> 1. one\n> 2. two\n","<blockquote>\n<ol>\n<li>one</li>\n<li>two</li>\n</ol>\n</blockquote>\n",1,1},
  {"quoted-continuation","> - foo\n>   bar\n","<blockquote>\n<ul>\n<li>foo\nbar</li>\n</ul>\n</blockquote>\n",1,1},
  {"quoted-interruption","> foo\n> - bar\n","<blockquote>\n<p>foo</p>\n<ul>\n<li>bar</li>\n</ul>\n</blockquote>\n",1,1},
  {"ordered-noninterrupt","> foo\n> 2. bar\n","<blockquote>\n<p>foo\n2. bar</p>\n</blockquote>\n",1,0},
  /* CommonMark lazy continuation: an unmarked ordinary text line can
   * still continue the second quoted LIST_ITEM's open paragraph. A blank
   * barrier ends that ownership and restores a genuine ROOT paragraph. */
  {"quoted-second-item-lazy","> - foo\n> - bar\nplain\n","<blockquote>\n<ul>\n<li>foo</li>\n<li>bar\nplain</li>\n</ul>\n</blockquote>\n",1,1},
  {"quote-to-root-after-blank","> - foo\n> - bar\n\nplain\n","<blockquote>\n<ul>\n<li>foo</li>\n<li>bar</li>\n</ul>\n</blockquote>\n<p>plain</p>\n",1,1},
  {"quoted-direct-fence","> - ~~~c\n>   alpha & beta\n>   ~~~\n",
   "<blockquote>\n<ul>\n<li>\n<pre><code class=\"language-c\">alpha &amp; beta\n</code></pre>\n</li>\n</ul>\n</blockquote>\n",1,1},
  {"quoted-direct-fence-blank","> - ~~~\n>   one\n>\n>   two\n>   ~~~\n",
   "<blockquote>\n<ul>\n<li>\n<pre><code>one\n\ntwo\n</code></pre>\n</li>\n</ul>\n</blockquote>\n",1,1},
  {"quoted-direct-fence-sibling","> - ~~~\n>   ```\n>   abc\n>   ~~~\n> - bar\n",
   "<blockquote>\n<ul>\n<li>\n<pre><code>```\nabc\n</code></pre>\n</li>\n<li>bar</li>\n</ul>\n</blockquote>\n",1,1}
};
_Static_assert(sizeof(cases)/sizeof(cases[0])==11u,"fixture denominator");
static int check(const fixture *f){
 salts_md_document *d=NULL;size_t n=strlen(f->input);
 if(salts_md_parse(f->input,n,256u,n+1u,&d)!=SALTS_MD_OK||!d)return 1;
 const salts_md_rich_document_view v=salts_md_get_document_view(d);
 int failed=salts_md_rich_document_validate(&v,256u)!=SALTS_MD_OK ||
            v.raw_markdown_bytes!=n || memcmp(v.raw_markdown,f->input,n)!=0;
 unsigned quotes=0,lists=0,fences=0;
 const bool direct_fence=strncmp(f->id,"quoted-direct-fence",19u)==0;
 for(size_t i=1;i<v.document.block_count && !failed;++i){
  const salts_md_block *node=&v.document.blocks[i];
  if(node->kind==SALTS_MD_BLOCK_QUOTE){++quotes; if(node->parent_index!=0)failed=1;}
  if(node->kind==SALTS_MD_BLOCK_LIST||node->kind==SALTS_MD_BLOCK_ORDERED_LIST){
   ++lists;
   if(node->parent_index>=i)failed=1;
   else if(strcmp(f->id,"cm235")==0){
    const salts_md_block_kind expected=lists==1?
       SALTS_MD_BLOCK_QUOTE:SALTS_MD_BLOCK_DOCUMENT;
    if(v.document.blocks[node->parent_index].kind!=expected)failed=1;
   }else if(v.document.blocks[node->parent_index].kind!=SALTS_MD_BLOCK_QUOTE)
     failed=1;
  }
  if(node->kind==SALTS_MD_BLOCK_CODE_FENCE){
   ++fences;
   const uint32_t item=node->parent_index;
   if(item>=i || v.document.blocks[item].kind!=SALTS_MD_BLOCK_LIST_ITEM)
    failed=1;
   else{
    const uint32_t list=v.document.blocks[item].parent_index;
    if(list>=item || v.document.blocks[list].kind!=SALTS_MD_BLOCK_LIST)
     failed=1;
    else{
     const uint32_t quote=v.document.blocks[list].parent_index;
     if(quote>=list || v.document.blocks[quote].kind!=SALTS_MD_BLOCK_QUOTE)
      failed=1;
    }
   }
  }
 }
 if(quotes!=f->quotes||lists!=f->lists||
    fences!=(direct_fence?1u:0u))failed=1;
 size_t needed=0,actual=0;
 if(!failed && salts_md_render_html(&v,256u,NULL,0u,&needed)!=SALTS_MD_OK)failed=1;
 if(!failed && needed!=strlen(f->html))failed=1;
 if(!failed){
  char *out=(char*)malloc(needed+2u);
  if(!out)failed=1;
  else{
   memset(out,0xa5,needed+2u);
   if(salts_md_render_html(&v,256u,out,needed,&actual)!=SALTS_MD_CAPACITY_EXCEEDED
      ||actual!=needed)failed=1;
   for(size_t i=0;i<needed+2u;++i)if((unsigned char)out[i]!=0xa5u)failed=1;
   if(!failed && (salts_md_render_html(&v,256u,out,needed+1u,&actual)!=SALTS_MD_OK
        ||actual!=needed||memcmp(out,f->html,needed+1u)!=0
        ||(unsigned char)out[needed+1u]!=0xa5u))failed=1;
   free(out);
  }
 }
 salts_md_document_destroy(d);
 if(failed)fprintf(stderr,"Quote/list fixture failed: %s\n",f->id);
 return failed;
}
int main(void){
 for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
   if(check(&cases[i]))return 1;
 printf("Quoted list ownership %zu/%zu PASS\n",
        sizeof(cases)/sizeof(cases[0]),sizeof(cases)/sizeof(cases[0]));
 return 0;
}
