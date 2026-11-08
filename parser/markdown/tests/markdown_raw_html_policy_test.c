#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do {if(!(expr)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr);return 1; \
} } while(0)

typedef struct raw_fixture {
    unsigned official;
    const char *source,*spec,*safe;
    size_t raw_nodes;
} raw_fixture;
static size_t raw_nodes(salts_md_rich_document_view v) {
    size_t found=0u;
    for(size_t i=1u;i<v.document.block_count;++i)
        found+=v.document.blocks[i].kind==SALTS_MD_BLOCK_RAW_HTML_INLINE;
    return found;
}
static int verify(const raw_fixture *f) {
    const size_t n=strlen(f->source);
    char *input=(char*)malloc(n+1u);CHECK(input!=NULL);
    memcpy(input,f->source,n+1u);
    salts_md_document *doc=NULL,*rejected=NULL;
    CHECK(salts_md_parse(input,n,256u,n+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);memset(input,'!',n);free(input);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
    CHECK(v.raw_markdown_bytes==n &&
          !memcmp(v.raw_markdown,f->source,n));
    CHECK(raw_nodes(v)==f->raw_nodes);
    for(unsigned policy=0u;policy<2u;++policy) {
        const salts_md_html_policy mode=policy?
            SALTS_MD_HTML_SPEC_CONFORMANCE:SALTS_MD_HTML_SAFE_APPLICATION;
        const char *expected=policy?f->spec:f->safe;
        const size_t bytes=strlen(expected);
        size_t required=SIZE_MAX,reported=SIZE_MAX;
        CHECK(salts_md_render_html_with_policy(
            &v,256u,mode,NULL,0u,&required)==SALTS_MD_OK);
        CHECK(required==bytes);
        char *out=(char*)malloc(bytes+3u);CHECK(out!=NULL);
        memset(out,0x55,bytes+3u);
        CHECK(salts_md_render_html_with_policy(
            &v,256u,mode,out,bytes,&reported)==SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(reported==bytes);
        for(size_t i=0u;i<bytes+3u;++i)
            CHECK((unsigned char)out[i]==0x55u);
        CHECK(salts_md_render_html_with_policy(
            &v,256u,mode,out,bytes+1u,&reported)==SALTS_MD_OK);
        CHECK(reported==bytes && !memcmp(out,expected,bytes+1u));
        CHECK((unsigned char)out[bytes+1u]==0x55u);
        if(!policy) {
            size_t measured=SIZE_MAX;
            CHECK(salts_md_render_html(&v,256u,out,bytes+1u,&measured)
                  ==SALTS_MD_OK);
            CHECK(measured==bytes && !memcmp(out,expected,bytes+1u));
        }
        free(out);
    }
    char unchanged[64];memset(unchanged,0x47,sizeof(unchanged));
    size_t needed=SIZE_MAX;
    CHECK(salts_md_render_html_with_policy(&v,256u,
          (salts_md_html_policy)42,unchanged,sizeof(unchanged),&needed)
          ==SALTS_MD_INVALID_ARGUMENT);
    for(size_t i=0u;i<sizeof(unchanged);++i)
        CHECK((unsigned char)unchanged[i]==0x47u);
    const size_t count=v.document.block_count;
    salts_md_document_destroy(doc);
    CHECK(salts_md_parse(f->source,n,count-1u,n+1u,&rejected)
          ==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(rejected==NULL);
    return 0;
}
static const raw_fixture fixtures[] = {
    {613u,"<a><bab><c2c>\n",
     "<p><a><bab><c2c></p>\n",
     "<p>&lt;a&gt;&lt;bab&gt;&lt;c2c&gt;</p>\n",
     3u},
    {614u,"<a/><b2/>\n",
     "<p><a/><b2/></p>\n",
     "<p>&lt;a/&gt;&lt;b2/&gt;</p>\n",
     2u},
    {617u,"Foo <responsive-image src=\"foo.jpg\" />\n",
     "<p>Foo <responsive-image src=\"foo.jpg\" /></p>\n",
     "<p>Foo &lt;responsive-image src=&quot;foo.jpg&quot; /&gt;</p>\n",
     1u},
    {623u,"</a></foo >\n",
     "<p></a></foo ></p>\n",
     "<p>&lt;/a&gt;&lt;/foo &gt;</p>\n",
     2u},
    {630u,"foo <a href=\"&ouml;\">\n",
     "<p>foo <a href=\"&ouml;\"></p>\n",
     "<p>foo &lt;a href=&quot;&amp;ouml;&quot;&gt;</p>\n",
     1u},
    {631u,"foo <a href=\"\\*\">\n",
     "<p>foo <a href=\"\\*\"></p>\n",
     "<p>foo &lt;a href=&quot;\\*&quot;&gt;</p>\n",
     1u},
    {627u,"foo <?php echo $a; ?>\n",
     "<p>foo <?php echo $a; ?></p>\n",
     "<p>foo &lt;?php echo $a; ?&gt;</p>\n",1u},
    {628u,"foo <!ELEMENT br EMPTY>\n",
     "<p>foo <!ELEMENT br EMPTY></p>\n",
     "<p>foo &lt;!ELEMENT br EMPTY&gt;</p>\n",1u},
    {629u,"foo <![CDATA[>&<]]>\n",
     "<p>foo <![CDATA[>&<]]></p>\n",
     "<p>foo &lt;![CDATA[&gt;&amp;&lt;]]&gt;</p>\n",1u},
    {0u,"foo <!-- simple -->\n",
     "<p>foo <!-- simple --></p>\n",
     "<p>foo &lt;!-- simple --&gt;</p>\n",1u},
    {0u,"before <script>alert(1)</script> after\n",
     "<p>before <script>alert(1)</script> after</p>\n",
     "<p>before &lt;script&gt;alert(1)&lt;/script&gt; after</p>\n",2u},
    {0u,"x <a foo=\"unclosed\n",
     "<p>x &lt;a foo=&quot;unclosed</p>\n",
     "<p>x &lt;a foo=&quot;unclosed</p>\n",0u},
    {626u,"foo <!--> foo -->\n\nfoo <!---> foo -->\n",
     "<p>foo <!--> foo --&gt;</p>\n<p>foo <!---> foo --&gt;</p>\n",
     "<p>foo &lt;!--&gt; foo --&gt;</p>\n<p>foo &lt;!---&gt; foo --&gt;</p>\n",2u},
    {615u,"<a  /><b2\ndata=\"foo\" >\n",
     "<p><a  /><b2\ndata=\"foo\" ></p>\n",
     "<p>&lt;a  /&gt;&lt;b2\ndata=&quot;foo&quot; &gt;</p>\n",2u},
    {616u,"<a foo=\"bar\" bam = 'baz <em>\"</em>'\n_boolean zoop:33=zoop:33 />\n",
     "<p><a foo=\"bar\" bam = 'baz <em>\"</em>'\n_boolean zoop:33=zoop:33 /></p>\n",
     "<p>&lt;a foo=&quot;bar&quot; bam = 'baz &lt;em&gt;&quot;&lt;/em&gt;'\n_boolean zoop:33=zoop:33 /&gt;</p>\n",1u},
    {0u,"x <a disabled title=\"ok\"> y\n",
     "<p>x <a disabled title=\"ok\"> y</p>\n",
     "<p>x &lt;a disabled title=&quot;ok&quot;&gt; y</p>\n",1u},
    {0u,"x <a disabled= > y\n",
     "<p>x &lt;a disabled= &gt; y</p>\n",
     "<p>x &lt;a disabled= &gt; y</p>\n",0u},
    {625u,"foo <!-- this is a --\ncomment - with hyphens -->\n",
     "<p>foo <!-- this is a --\ncomment - with hyphens --></p>\n",
     "<p>foo &lt;!-- this is a --\ncomment - with hyphens --&gt;</p>\n",1u},
    {642u,"<a href=\"foo  \nbar\">\n",
     "<p><a href=\"foo  \nbar\"></p>\n",
     "<p>&lt;a href=&quot;foo  \nbar&quot;&gt;</p>\n",1u},
    {643u,"<a href=\"foo\\\nbar\">\n",
     "<p><a href=\"foo\\\nbar\"></p>\n",
     "<p>&lt;a href=&quot;foo\\\nbar&quot;&gt;</p>\n",1u},
    {491u,"[link](<foo\nbar>)\n",
     "<p>[link](<foo\nbar>)</p>\n",
     "<p>[link](&lt;foo\nbar&gt;)</p>\n",1u},
    {0u,"<a href=\"foo\r\nbar\">\r\n",
     "<p><a href=\"foo\nbar\"></p>\n",
     "<p>&lt;a href=&quot;foo\nbar&quot;&gt;</p>\n",1u},
    {0u,"foo <!--x> bar\n",
     "<p>foo &lt;!--x&gt; bar</p>\n",
     "<p>foo &lt;!--x&gt; bar</p>\n",0u}
};
/* Real ROOT HTML type-6 blocks use the same canonical block parser.
 * The SAFE profile still escapes all raw HTML; SPEC emits only admitted
 * literal source tokens. HTML code/fence/definition interactions remain
 * bounded and may not create Reference bindings inside raw block content. */
static const raw_fixture block_fixtures[]={
    {162u,"<a href=\"foo\">\n*bar*\n</a>\n",
     "<a href=\"foo\">\n*bar*\n</a>\n",
     "&lt;a href=&quot;foo&quot;&gt;\n*bar*\n&lt;/a&gt;\n",0u}, /* CommonMark #162 */
    {163u,"<Warning>\n*bar*\n</Warning>\n",
     "<Warning>\n*bar*\n</Warning>\n",
     "&lt;Warning&gt;\n*bar*\n&lt;/Warning&gt;\n",0u}, /* CommonMark #163 */
    {164u,"<i class=\"foo\">\n*bar*\n</i>\n",
     "<i class=\"foo\">\n*bar*\n</i>\n",
     "&lt;i class=&quot;foo&quot;&gt;\n*bar*\n&lt;/i&gt;\n",0u}, /* CommonMark #164 */
    {165u,"</ins>\n*bar*\n",
     "</ins>\n*bar*\n",
     "&lt;/ins&gt;\n*bar*\n",0u}, /* CommonMark #165 */
    {166u,"<del>\n*foo*\n</del>\n",
     "<del>\n*foo*\n</del>\n",
     "&lt;del&gt;\n*foo*\n&lt;/del&gt;\n",0u}, /* CommonMark #166 */
    {167u,"<del>\n\n*foo*\n\n</del>\n",
     "<del>\n<p><em>foo</em></p>\n</del>\n",
     "&lt;del&gt;\n<p><em>foo</em></p>\n&lt;/del&gt;\n",0u}, /* CommonMark #167 */
    {169u,"<pre language=\"haskell\"><code>\nimport Text.HTML.TagSoup\n\nmain :: IO ()\nmain = print $ parseTags tags\n</code></pre>\nokay\n",
     "<pre language=\"haskell\"><code>\nimport Text.HTML.TagSoup\n\nmain :: IO ()\nmain = print $ parseTags tags\n</code></pre>\n<p>okay</p>\n",
     "&lt;pre language=&quot;haskell&quot;&gt;&lt;code&gt;\nimport Text.HTML.TagSoup\n\nmain :: IO ()\nmain = print $ parseTags tags\n&lt;/code&gt;&lt;/pre&gt;\n<p>okay</p>\n",0u}, /* CommonMark #169 */
    {170u,"<script type=\"text/javascript\">\n// JavaScript example\n\ndocument.getElementById(\"demo\").innerHTML = \"Hello JavaScript!\";\n</script>\nokay\n",
     "<script type=\"text/javascript\">\n// JavaScript example\n\ndocument.getElementById(\"demo\").innerHTML = \"Hello JavaScript!\";\n</script>\n<p>okay</p>\n",
     "&lt;script type=&quot;text/javascript&quot;&gt;\n// JavaScript example\n\ndocument.getElementById(&quot;demo&quot;).innerHTML = &quot;Hello JavaScript!&quot;;\n&lt;/script&gt;\n<p>okay</p>\n",0u}, /* CommonMark #170 */
    {171u,"<textarea>\n\n*foo*\n\n_bar_\n\n</textarea>\n",
     "<textarea>\n\n*foo*\n\n_bar_\n\n</textarea>\n",
     "&lt;textarea&gt;\n\n*foo*\n\n_bar_\n\n&lt;/textarea&gt;\n",0u}, /* CommonMark #171 */
    {172u,"<style\n  type=\"text/css\">\nh1 {color:red;}\n\np {color:blue;}\n</style>\nokay\n",
     "<style\n  type=\"text/css\">\nh1 {color:red;}\n\np {color:blue;}\n</style>\n<p>okay</p>\n",
     "&lt;style\n  type=&quot;text/css&quot;&gt;\nh1 {color:red;}\n\np {color:blue;}\n&lt;/style&gt;\n<p>okay</p>\n",0u}, /* CommonMark #172 */
    {173u,"<style\n  type=\"text/css\">\n\nfoo\n",
     "<style\n  type=\"text/css\">\n\nfoo\n",
     "&lt;style\n  type=&quot;text/css&quot;&gt;\n\nfoo\n",0u}, /* CommonMark #173 */
    {176u,"<style>p{color:red;}</style>\n*foo*\n",
     "<style>p{color:red;}</style>\n<p><em>foo</em></p>\n",
     "&lt;style&gt;p{color:red;}&lt;/style&gt;\n<p><em>foo</em></p>\n",0u}, /* CommonMark #176 */
    {177u,"<!-- foo -->*bar*\n*baz*\n",
     "<!-- foo -->*bar*\n<p><em>baz</em></p>\n",
     "&lt;!-- foo --&gt;*bar*\n<p><em>baz</em></p>\n",0u}, /* CommonMark #177 */
    {178u,"<script>\nfoo\n</script>1. *bar*\n",
     "<script>\nfoo\n</script>1. *bar*\n",
     "&lt;script&gt;\nfoo\n&lt;/script&gt;1. *bar*\n",0u}, /* CommonMark #178 */
    {179u,"<!-- Foo\n\nbar\n   baz -->\nokay\n",
     "<!-- Foo\n\nbar\n   baz -->\n<p>okay</p>\n",
     "&lt;!-- Foo\n\nbar\n   baz --&gt;\n<p>okay</p>\n",0u}, /* CommonMark #179 */
    {180u,"<?php\n\n  echo '>';\n\n?>\nokay\n",
     "<?php\n\n  echo '>';\n\n?>\n<p>okay</p>\n",
     "&lt;?php\n\n  echo '&gt;';\n\n?&gt;\n<p>okay</p>\n",0u}, /* CommonMark #180 */
    {181u,"<!DOCTYPE html>\n",
     "<!DOCTYPE html>\n",
     "&lt;!DOCTYPE html&gt;\n",0u}, /* CommonMark #181 */
    {182u,"<![CDATA[\nfunction matchwo(a,b)\n{\n  if (a < b && a < 0) then {\n    return 1;\n\n  } else {\n\n    return 0;\n  }\n}\n]]>\nokay\n",
     "<![CDATA[\nfunction matchwo(a,b)\n{\n  if (a < b && a < 0) then {\n    return 1;\n\n  } else {\n\n    return 0;\n  }\n}\n]]>\n<p>okay</p>\n",
     "&lt;![CDATA[\nfunction matchwo(a,b)\n{\n  if (a &lt; b &amp;&amp; a &lt; 0) then {\n    return 1;\n\n  } else {\n\n    return 0;\n  }\n}\n]]&gt;\n<p>okay</p>\n",0u}, /* CommonMark #182 */
    {149u,"<table>\n  <tr>\n    <td>\n           hi\n    </td>\n  </tr>\n</table>\n\nokay.\n",
     "<table>\n  <tr>\n    <td>\n           hi\n    </td>\n  </tr>\n</table>\n<p>okay.</p>\n",
     "&lt;table&gt;\n  &lt;tr&gt;\n    &lt;td&gt;\n           hi\n    &lt;/td&gt;\n  &lt;/tr&gt;\n&lt;/table&gt;\n<p>okay.</p>\n",0u},
    {150u," <div>\n  *hello*\n         <foo><a>\n",
     " <div>\n  *hello*\n         <foo><a>\n",
     " &lt;div&gt;\n  *hello*\n         &lt;foo&gt;&lt;a&gt;\n",0u},
    {151u,"</div>\n*foo*\n",
     "</div>\n*foo*\n",
     "&lt;/div&gt;\n*foo*\n",0u},
    {160u,"<table><tr><td>\nfoo\n</td></tr></table>\n",
     "<table><tr><td>\nfoo\n</td></tr></table>\n",
     "&lt;table&gt;&lt;tr&gt;&lt;td&gt;\nfoo\n&lt;/td&gt;&lt;/tr&gt;&lt;/table&gt;\n",0u},
    {185u,"Foo\n<div>\nbar\n</div>\n",
     "<p>Foo</p>\n<div>\nbar\n</div>\n",
     "<p>Foo</p>\n&lt;div&gt;\nbar\n&lt;/div&gt;\n",0u},
    {190u,"<table>\n\n<tr>\n\n<td>\nHi\n</td>\n\n</tr>\n\n</table>\n",
     "<table>\n<tr>\n<td>\nHi\n</td>\n</tr>\n</table>\n",
     "&lt;table&gt;\n&lt;tr&gt;\n&lt;td&gt;\nHi\n&lt;/td&gt;\n&lt;/tr&gt;\n&lt;/table&gt;\n",0u},
    {174u,"> <div>\n> foo\n\nbar\n",
     "<blockquote>\n<div>\nfoo\n</blockquote>\n<p>bar</p>\n",
     "<blockquote>\n&lt;div&gt;\nfoo\n</blockquote>\n<p>bar</p>\n",0u},
    {175u,"- <div>\n- foo\n",
     "<ul>\n<li>\n<div>\n</li>\n<li>foo</li>\n</ul>\n",
     "<ul>\n<li>\n&lt;div&gt;\n</li>\n<li>foo</li>\n</ul>\n",0u},
    {0u,"> <script>\n> console.log(1)\n> </script>\n",
     "<blockquote>\n<script>\nconsole.log(1)\n</script>\n</blockquote>\n",
     "<blockquote>\n&lt;script&gt;\nconsole.log(1)\n&lt;/script&gt;\n</blockquote>\n",0u},
    {0u,"- <script>\n  hi\n  </script>\n",
     "<ul>\n<li>\n<script>\nhi\n</script>\n</li>\n</ul>\n",
     "<ul>\n<li>\n&lt;script&gt;\nhi\n&lt;/script&gt;\n</li>\n</ul>\n",0u},
    {0u,"<div>\n[ref]: /evil\n\n[ref]\n",
     "<div>\n[ref]: /evil\n<p>[ref]</p>\n",
     "&lt;div&gt;\n[ref]: /evil\n<p>[ref]</p>\n",0u}
};

static int verify_block(const raw_fixture *f) {
    if(verify(f))return 1;
    const size_t n=strlen(f->source);
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(f->source,n,256u,n+1u,&doc)==SALTS_MD_OK);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    size_t blocks=0u;
    for(size_t i=0u;i<v.document.block_count;++i)
        blocks+=v.document.blocks[i].kind==SALTS_MD_BLOCK_RAW_HTML_BLOCK;
    CHECK(blocks>0u);
    salts_md_document_destroy(doc);
    return 0;
}

/* HTML block type-6 has an exact CommonMark 0.31.2 tag vocabulary
 * and delimiter rule. Names outside that vocabulary, plus a raw slash
 * that is not '/>', must remain inline/paragraph grammar and not swallow
 * the next Markdown emphasis line as an HTML block. */
static int test_type6_exact_admission(void) {
    static const char *const nonblocks[]={
        "<source>x\n*foo*\n",
        "<output>x\n*foo*\n",
        "<menuitem>x\n*foo*\n",
        "<table/foo>x\n*foo*\n"
    };
    salts_md_document *doc=NULL;
    for(size_t k=0u;k<sizeof(nonblocks)/sizeof(nonblocks[0]);++k) {
        const char *input=nonblocks[k];size_t n=strlen(input);
        CHECK(salts_md_parse(input,n,128u,n+1u,&doc)==SALTS_MD_OK);
        CHECK(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        size_t raw_blocks=0u,emphasis=0u;
        for(size_t i=0u;i<v.document.block_count;++i) {
            raw_blocks+=v.document.blocks[i].kind==
                        SALTS_MD_BLOCK_RAW_HTML_BLOCK;
            emphasis+=v.document.blocks[i].kind==
                      SALTS_MD_BLOCK_EMPHASIS;
        }
        CHECK(raw_blocks==0u && emphasis==1u);
        salts_md_document_destroy(doc);doc=NULL;
    }
    return 0;
}

/* An escaped opener, a blank, or an interrupting list must not donate
 * its next source line to the bounded inline HTML ownership path. */
static int test_multiline_html_nonadmission(void) {
    static const char *const rejected[]={
        "\\<a href=\"foo\nbar\">\n",
        "<a href=\"foo\n\nbar\">\n",
        "<a href=\"foo\n- bar\">\n",
        "foo <!-- unfinished\nstill not closed\n"
    };
    for(size_t k=0u;k<sizeof(rejected)/sizeof(rejected[0]);++k) {
        const size_t bytes=strlen(rejected[k]);
        salts_md_document *doc=NULL;
        CHECK(salts_md_parse(rejected[k],bytes,256u,bytes+1u,&doc)
              ==SALTS_MD_OK);
        CHECK(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        CHECK(raw_nodes(v)==0u);
        salts_md_document_destroy(doc);
    }
    return 0;
}

int main(void) {
    if(test_type6_exact_admission())return 1;
    if(test_multiline_html_nonadmission())return 1;
    _Static_assert(sizeof(fixtures)/sizeof(fixtures[0])==23u,
                   "sixteen official inline, one CRLF, six grammar/security controls");
    for(size_t i=0u;i<sizeof(fixtures)/sizeof(fixtures[0]);++i)
        if(verify(&fixtures[i])) {
            fprintf(stderr,"RAW HTML fixture %u failed\n",
                    fixtures[i].official);
            return 1;
        }
    _Static_assert(sizeof(block_fixtures)/sizeof(block_fixtures[0])==29u,
                   "root cases plus quote/list owned HTML blocks and opacity");
    for(size_t i=0u;i<sizeof(block_fixtures)/sizeof(block_fixtures[0]);++i)
        if(verify_block(&block_fixtures[i])) {
            fprintf(stderr,"OWNED RAW_HTML_BLOCK official #%u failed\n",
                    block_fixtures[i].official);
            return 1;
        }
    /* Type 7 complete tags cannot interrupt an already-open paragraph. */
    const raw_fixture type7_noninterrupt={
        187u,"Foo\n<a href=\"bar\">\nbaz\n","<p>Foo\n<a href=\"bar\">\nbaz</p>\n","<p>Foo\n&lt;a href=&quot;bar&quot;&gt;\nbaz</p>\n",1u
    };
    if(verify(&type7_noninterrupt))return 1;
    puts("C11 typed RAW_HTML: sixteen official inline, twenty-eight official block "
         "spec outputs; unchanged SAFE rendering and atomic limits");
    return 0;
}
