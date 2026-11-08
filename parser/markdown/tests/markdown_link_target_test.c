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

typedef struct link_fixture {
    const char *markdown,*href,*title,*label,*expected_html;
} link_fixture;

static size_t kind_count(salts_md_rich_document_view v,
                         salts_md_block_kind kind) {
    size_t total=0u;
    for(size_t i=0u;i<v.document.block_count;++i)
        total+=v.document.blocks[i].kind==kind;
    return total;
}

static int verify_link(const link_fixture *f) {
    const size_t raw_size=strlen(f->markdown);
    salts_md_document *owner=NULL;
    CHECK(salts_md_parse(f->markdown,raw_size,128u,raw_size+1u,
                        &owner)==SALTS_MD_OK);
    CHECK(owner!=NULL);
    const salts_md_rich_document_view view=salts_md_get_document_view(owner);
    CHECK(salts_md_rich_document_validate(&view,128u)==SALTS_MD_OK);
    CHECK(view.raw_markdown_bytes==raw_size);
    CHECK(memcmp(view.raw_markdown,f->markdown,raw_size)==0);
    CHECK(kind_count(view,SALTS_MD_BLOCK_LINK)==1u);
    CHECK(kind_count(view,SALTS_MD_BLOCK_LINK_TITLE)==
          (f->title?1u:0u));

    bool found_link=false,found_title=!f->title,found_label=false;
    for(size_t i=1u;i<view.document.block_count;++i) {
        const salts_md_block *node=&view.document.blocks[i];
        CHECK(node->parent_index<i);
        if(node->kind==SALTS_MD_BLOCK_LINK) {
            const char *url=NULL;size_t bytes=SIZE_MAX;
            CHECK(salts_md_rich_document_metadata(&view,i,&url,&bytes)==
                  SALTS_MD_OK);
            CHECK(bytes==strlen(f->href));
            CHECK(!bytes || (url &&
                  !memcmp(url,f->href,bytes)));
            found_link=true;
        }
        if(node->kind==SALTS_MD_BLOCK_LINK_TITLE) {
            CHECK(node->parent_index+1u==i);
            CHECK(view.document.blocks[node->parent_index].kind==
                  SALTS_MD_BLOCK_LINK);
            const char *title=NULL;size_t bytes=SIZE_MAX;
            CHECK(salts_md_rich_document_metadata(&view,i,&title,&bytes)==
                  SALTS_MD_OK);
            CHECK(bytes==strlen(f->title));
            CHECK(!bytes || (title &&
                  !memcmp(title,f->title,bytes)));
            found_title=true;
        }
        if(node->kind==SALTS_MD_BLOCK_TEXT &&
           view.document.blocks[node->parent_index].kind==
               SALTS_MD_BLOCK_LINK) {
            const size_t size=node->byte_end-node->byte_begin;
            CHECK(node->byte_end<=view.document.source_bytes);
            if(size==strlen(f->label) &&
               !memcmp(view.document.source+node->byte_begin,
                       f->label,size))
                found_label=true;
        }
    }
    CHECK(found_link && found_title && found_label);

    size_t required=SIZE_MAX;
    CHECK(salts_md_render_html(&view,128u,NULL,0u,&required)==SALTS_MD_OK);
    CHECK(required==strlen(f->expected_html));
    char buffer[256];
    CHECK(required+2u<sizeof(buffer));
    memset(buffer,0x58,sizeof(buffer));
    size_t counted=SIZE_MAX;
    CHECK(salts_md_render_html(&view,128u,buffer,required,&counted)==
          SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(counted==required);
    for(size_t i=0u;i<sizeof(buffer);++i)
        CHECK((unsigned char)buffer[i]==0x58u);
    CHECK(salts_md_render_html(&view,128u,buffer,required+1u,&counted)==
          SALTS_MD_OK);
    CHECK(counted==required);
    CHECK(!memcmp(buffer,f->expected_html,required+1u));
    CHECK((unsigned char)buffer[required+1u]==0x58u);
    salts_md_document_destroy(owner);
    return 0;
}

static int verify_safe_rejection(const char *source) {
    const size_t n=strlen(source);
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(source,n,128u,n+1u,&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    const salts_md_rich_document_view view=salts_md_get_document_view(doc);
    CHECK(salts_md_rich_document_validate(&view,128u)==SALTS_MD_OK);
    CHECK(kind_count(view,SALTS_MD_BLOCK_LINK)==1u);
    char buffer[256];
    memset(buffer,0x35,sizeof(buffer));
    size_t required=SIZE_MAX;
    CHECK(salts_md_render_html(&view,128u,buffer,sizeof(buffer),
                              &required)==SALTS_MD_INVALID_ARGUMENT);
    for(size_t i=0u;i<sizeof(buffer);++i)
        CHECK((unsigned char)buffer[i]==0x35u);
    salts_md_document_destroy(doc);
    return 0;
}

int main(void) {
    static const link_fixture samples[]={
        {"[foo](/bar\\* \"ti\\*tle\")\n",
         "/bar*","ti*tle","foo",
         "<p><a href=\"/bar*\" title=\"ti*tle\">foo</a></p>\n"}, /* #22 */
        {"[foo](/f&ouml;&ouml; \"f&ouml;&ouml;\")\n",
         "/föö","föö","foo",
         "<p><a href=\"/f%C3%B6%C3%B6\" title=\"föö\">foo</a></p>\n"}, /* #32 */
        {"[&copy;](dest \"Title\")\n",
         "dest","Title","©",
         "<p><a href=\"dest\" title=\"Title\">©</a></p>\n"},
        {"[x](/f\\&copy; \"a\")\n",
         "/f&copy;","a","x",
         "<p><a href=\"/f&amp;copy;\" title=\"a\">x</a></p>\n"},
        {"[x](dest \"say \\\"hi\\\"\")\n",
         "dest","say \"hi\"","x",
         "<p><a href=\"dest\" title=\"say &quot;hi&quot;\">x</a></p>\n"},
        {"[x](dest \"\")\n",
         "dest","","x",
         "<p><a href=\"dest\" title=\"\">x</a></p>\n"},
        {"[x](/f%C3%B6)\n",
         "/f%C3%B6",NULL,"x",
         "<p><a href=\"/f%C3%B6\">x</a></p>\n"}
    };
    _Static_assert(sizeof(samples)/sizeof(samples[0])==7u,
                   "link title/URI semantic fixture count");
    for(size_t i=0u;i<sizeof(samples)/sizeof(samples[0]);++i)
        if(verify_link(&samples[i]))return 1;

    /* #185 target grammar: balanced unescaped parentheses, angle mode,
     * escaped closers and no re-interpretation of Unicode NBSP. */
    static const link_fixture balanced[] = {
        {"[link](</foo`bar>)\n","/foo`bar",NULL,"link",
         "<p><a href=\"/foo%60bar\">link</a></p>\n"},
        {"[link](<>)\n","",NULL,"link","<p><a href=\"\">link</a></p>\n"},
        {"[link](</my uri>)\n","/my uri",NULL,"link","<p><a href=\"/my%20uri\">link</a></p>\n"},
        {"[a](<b)c>)\n","b)c",NULL,"a","<p><a href=\"b)c\">a</a></p>\n"},
        {"[link](foo(and(bar)))\n","foo(and(bar))",NULL,"link","<p><a href=\"foo(and(bar))\">link</a></p>\n"},
        {"[link](<foo(and(bar)>)\n","foo(and(bar)",NULL,"link","<p><a href=\"foo(and(bar)\">link</a></p>\n"},
        {"[link](foo\\)\\:)\n","foo):",NULL,"link","<p><a href=\"foo):\">link</a></p>\n"},
        {"[link](foo\\bar)\n","foo\\bar",NULL,"link","<p><a href=\"foo%5Cbar\">link</a></p>\n"},
        {"[link](\"title\")\n","\"title\"",NULL,"link","<p><a href=\"%22title%22\">link</a></p>\n"},
        {"[link](/url \"title\")\n","/url \"title\"",NULL,"link","<p><a href=\"/url%C2%A0%22title%22\">link</a></p>\n"},
        {"[x](f(a(b)) \"info\")\n","f(a(b))","info","x","<p><a href=\"f(a(b))\" title=\"info\">x</a></p>\n"},
        {"[x](dest (hello world))\n","dest","hello world","x","<p><a href=\"dest\" title=\"hello world\">x</a></p>\n"},
    };
    _Static_assert(sizeof(balanced)/sizeof(balanced[0])==12u,
                   "balanced/angle target test denominator");
    for(size_t i=0u;i<sizeof(balanced)/sizeof(balanced[0]);++i)
        if(verify_link(&balanced[i]))return 1;

    /* A safe angle URI is a typed LINK, with a literal TEXT label and a
     * document-owned href. Its percent/HTML escaping is renderer-owned. */
    static const link_fixture autolinks[]={
        {"<https://example.com?find=\\*>\n",
         "https://example.com?find=\\*",NULL,
         "https://example.com?find=\\*",
         "<p><a href=\"https://example.com?find=%5C*\">https://example.com?find=\\*</a></p>\n"},
        {"<https://foo.bar.`baz>`\n",
         "https://foo.bar.`baz",NULL,"https://foo.bar.`baz",
         "<p><a href=\"https://foo.bar.%60baz\">https://foo.bar.`baz</a>`</p>\n"}, /* CommonMark #346 */
        {"<http://foo.bar.baz>\n",
         "http://foo.bar.baz",NULL,"http://foo.bar.baz",
         "<p><a href=\"http://foo.bar.baz\">http://foo.bar.baz</a></p>\n"},
        {"<https://foo.bar.baz/test?q=hello&id=22&boolean>\n",
         "https://foo.bar.baz/test?q=hello&id=22&boolean",NULL,
         "https://foo.bar.baz/test?q=hello&id=22&boolean",
         "<p><a href=\"https://foo.bar.baz/test?q=hello&amp;id=22&amp;boolean\">https://foo.bar.baz/test?q=hello&amp;id=22&amp;boolean</a></p>\n"},
        {"**a<https://foo.bar/?q=**>\n",
         "https://foo.bar/?q=**",NULL,"https://foo.bar/?q=**",
         "<p>**a<a href=\"https://foo.bar/?q=**\">https://foo.bar/?q=**</a></p>\n"},
        {"__a<https://foo.bar/?q=__>\n",
         "https://foo.bar/?q=__",NULL,"https://foo.bar/?q=__",
         "<p>__a<a href=\"https://foo.bar/?q=__\">https://foo.bar/?q=__</a></p>\n"},
        {"<MAILTO:FOO@BAR.BAZ>\n",
         "MAILTO:FOO@BAR.BAZ",NULL,"MAILTO:FOO@BAR.BAZ",
         "<p><a href=\"MAILTO:FOO@BAR.BAZ\">MAILTO:FOO@BAR.BAZ</a></p>\n"},
        {"<https://../>\n",
         "https://../",NULL,"https://../",
         "<p><a href=\"https://../\">https://../</a></p>\n"},
        {"<https://example.com/\\[\\>\n",
         "https://example.com/\\[\\",NULL,"https://example.com/\\[\\",
         "<p><a href=\"https://example.com/%5C%5B%5C\">https://example.com/\\[\\</a></p>\n"}
    };
    _Static_assert(sizeof(autolinks)/sizeof(autolinks[0])==9u,
                   "safe URI autolink owner/HTML denominator");
    for(size_t i=0u;i<sizeof(autolinks)/sizeof(autolinks[0]);++i)
        if(verify_link(&autolinks[i]))return 1;

    /* Same C11 owner, destination/title grammar and safe renderer across
     * exactly one root paragraph LF; no separate multiline LINK parser. */
    static const link_fixture split_titles[]={
        {"[link](   /uri\n  \"title\"  )\n",
         "/uri","title","link",
         "<p><a href=\"/uri\" title=\"title\">link</a></p>\n"}, /* #510 */
        {"[link]( /uri\r\n 'title' )\r\n",
         "/uri","title","link",
         "<p><a href=\"/uri\" title=\"title\">link</a></p>\n"},
        {"[x]( /uri\n (note) )\n",
         "/uri","note","x",
         "<p><a href=\"/uri\" title=\"note\">x</a></p>\n"},
        {"[x]( /uri\n \"T\" ) tail\n",
         "/uri","T","x",
         "<p><a href=\"/uri\" title=\"T\">x</a> tail</p>\n"}
    };
    _Static_assert(sizeof(split_titles)/sizeof(split_titles[0])==4u,
                   "root two-line inline-title fixture count");
    for(size_t i=0u;i<sizeof(split_titles)/sizeof(split_titles[0]);++i)
        if(verify_link(&split_titles[i]))return 1;
    static const char *const blocked_inline[]={
        "[link](foo\nbar)\n",          /* #490: split destination */
        "[link](<foo\nbar>)\n",        /* #491: split angle destination */
        "[link](/uri\n\n\"title\")\n", /* blank root boundary */
        "[link](/uri\n# heading)\n",  /* competing root ATX */
        "[link](/uri\n- item)\n",     /* list interrupts root */
        "[link](/uri\n \"title\" junk)\n", /* malformed target */
        "[link](/uri\n \"unterminated)\n", /* no valid title closer */
        "> [link](/uri\n> \"title\")\n" /* quote-owned; no root borrowing */
    };
    _Static_assert(sizeof(blocked_inline)/sizeof(blocked_inline[0])==8u,
                   "root inline-title negative admission count");
    for(size_t i=0u;i<sizeof(blocked_inline)/sizeof(blocked_inline[0]);++i) {
        const size_t len=strlen(blocked_inline[i]);
        salts_md_document *doc=NULL;
        CHECK(salts_md_parse(blocked_inline[i],len,256u,len+1u,&doc)==
              SALTS_MD_OK);
        CHECK(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        CHECK(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
        CHECK(v.raw_markdown_bytes==len &&
              !memcmp(v.raw_markdown,blocked_inline[i],len));
        salts_md_document_destroy(doc);
    }
    const char budget_source[]="[link](   /uri\n  \"title\"  )\n";
    salts_md_document *budget=NULL,*budget_rejected=NULL;
    CHECK(salts_md_parse(budget_source,sizeof(budget_source)-1u,
                        256u,sizeof(budget_source),&budget)==SALTS_MD_OK);
    CHECK(budget!=NULL);
    const size_t exact_nodes=salts_md_get_document_view(budget).document.block_count;
    CHECK(exact_nodes>1u);
    salts_md_document_destroy(budget);
    CHECK(salts_md_parse(budget_source,sizeof(budget_source)-1u,
                        exact_nodes-1u,sizeof(budget_source),&budget_rejected)==
          SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(budget_rejected==NULL);

    /* A syntactically malformed or unapproved scheme never manufactures
     * an unsafe LINK and must retain escaped text under SafeApplicationHtml. */
    static const struct {const char *source,*html;} opaque[]={
        {"<https://foo.bar/baz bim>\n",
         "<p>&lt;https://foo.bar/baz bim&gt;</p>\n"},
        {"<irc://foo.bar:2233/baz>\n",
         "<p>&lt;irc://foo.bar:2233/baz&gt;</p>\n"},
        {"<javascript:alert>\n",
         "<p>&lt;javascript:alert&gt;</p>\n"},
        {"<script>alert(1)</script>\n",
         "&lt;script&gt;alert(1)&lt;/script&gt;\n"} /* type-1 raw BLOCK, escaped */
    };
    _Static_assert(sizeof(opaque)/sizeof(opaque[0])==4u,
                   "URI/RawHTML safety negative denominator");
    for(size_t k=0u;k<sizeof(opaque)/sizeof(opaque[0]);++k) {
        const size_t n=strlen(opaque[k].source);
        salts_md_document *doc=NULL;
        CHECK(salts_md_parse(opaque[k].source,n,128u,n+1u,&doc)==SALTS_MD_OK);
        CHECK(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        CHECK(v.raw_markdown_bytes==n);
        CHECK(!memcmp(v.raw_markdown,opaque[k].source,n));
        CHECK(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&v,128u,NULL,0u,&required)==SALTS_MD_OK);
        CHECK(required==strlen(opaque[k].html));
        char html[256]={0};size_t output=SIZE_MAX;
        CHECK(required+1u<sizeof(html));
        CHECK(salts_md_render_html(
            &v,128u,html,required+1u,&output)==SALTS_MD_OK);
        CHECK(output==required);
        CHECK(!memcmp(html,opaque[k].html,required+1u));
        salts_md_document_destroy(doc);
    }
    /* The LINK label TEXT is the last node in a plain URI autolink.
     * A one-node-short budget must reject the whole document atomically. */
    const char limited_autolink[]="<https://example.org/a&b>\n";
    salts_md_document *atomic=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse(limited_autolink,sizeof(limited_autolink)-1u,
                        3u,sizeof(limited_autolink),&atomic)==
          SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(atomic==NULL);

    /* An entity-expanded disallowed scheme must fail AFTER decoding, never
     * be normalized into an apparently safe ASCII URL. */
    CHECK(verify_safe_rejection("[x](java&#x73;cript:alert \"t\")\n")==0);
    CHECK(verify_safe_rejection("[x](data:text/html \"t\")\n")==0);
    /* Titles are quoted HTML5 text, not URLs. LF/TAB are safe payload;
     * CR and other C0/DEL controls must still fail without output. */
    const link_fixture title_whitespace={
        "[x](safe \"a&#10;b&#9;c\")\n","safe","a\nb\tc","x",
        "<p><a href=\"safe\" title=\"a\nb\tc\">x</a></p>\n"};
    CHECK(verify_link(&title_whitespace)==0);
    CHECK(verify_safe_rejection("[x](safe \"a&#1;b\")\n")==0);
    CHECK(verify_safe_rejection("[x](safe \"a&#13;b\")\n")==0);
    CHECK(verify_safe_rejection("[x](safe \"a&#127;b\")\n")==0);
    /* The whole decoded href must be admitted, not only its first scheme,
     * separator or fragment. These inputs used to bypass early returns at
     * '/', '?', '#', 'https:' and 'mailto:' with a late C0 scalar. */
    CHECK(verify_safe_rejection("[x](/safe&#10;bad \"t\")\n")==0);
    CHECK(verify_safe_rejection(
        "[x](https://example.org/&#x09;bad \"t\")\n")==0);
    CHECK(verify_safe_rejection("[x](foo?bad&#10;value)\n")==0);
    CHECK(verify_safe_rejection("[x](#frag&#13;bad)\n")==0);
    CHECK(verify_safe_rejection("[x](mailto:foo&#x0A;bar \"t\")\n")==0);
    CHECK(verify_safe_rejection("[x](</path&#10;bad>)\n")==0);
    CHECK(verify_safe_rejection("[x](custom+scheme:payload)\n")==0);



    /* The entity-produced quote cannot fabricate a syntactic link TITLE. */
    const char invalid[]="[a](url &quot;tit&quot;)\n";
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(invalid,sizeof(invalid)-1u,128u,
                        sizeof(invalid),&doc)==SALTS_MD_OK);
    CHECK(doc!=NULL);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
    salts_md_document_destroy(doc);

    /* Malformed or over-depth targets remain source text; no partial
     * LINK may be published or borrowed from this bounded parser. */
    static const char *invalid_targets[]={
        "[link](foo(and(bar))\n",
        "[link](<foo\\>)\n",
        "[link](<a<b>)\n",
        "[deep]((((((((((((((((((((((((((((((((((x))))))))))))))))))))))))))))))))))\n"
    };
    _Static_assert(sizeof(invalid_targets)/sizeof(invalid_targets[0])==4u,
                   "target rejection sample denominator");
    for(size_t i=0u;i<4u;++i) {
        const size_t len=strlen(invalid_targets[i]);
        salts_md_document *negative=NULL;
        CHECK(salts_md_parse(invalid_targets[i],len,256u,len+1u,
                            &negative)==SALTS_MD_OK);
        CHECK(negative!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(negative);
        CHECK(salts_md_rich_document_validate(&v,256u)==SALTS_MD_OK);
        CHECK(kind_count(v,SALTS_MD_BLOCK_LINK)==0u);
        CHECK(v.raw_markdown_bytes==len);
        CHECK(!memcmp(v.raw_markdown,invalid_targets[i],len));
        salts_md_document_destroy(negative);
    }

    /* CommonMark #480/#481: delimiter runs OUTSIDE a recognized URI
     * autolink must never close inside its literal LABEL. Failure to shield
     * them previously made the C11 parse reject these documents entirely. */
    static const char *const emph_shield[]={
        "**a<https://foo.bar/?q=**>\n",
        "__a<https://foo.bar/?q=__>\n"
    };
    _Static_assert(sizeof(emph_shield)/sizeof(emph_shield[0])==2u,
                   "opaque URI delimiter negative/rollback denominator");
    for(size_t k=0u;k<sizeof(emph_shield)/sizeof(emph_shield[0]);++k) {
        salts_md_document *doc=NULL;
        const size_t n=strlen(emph_shield[k]);
        CHECK(salts_md_parse(emph_shield[k],n,128u,n+1u,&doc)==SALTS_MD_OK);
        CHECK(doc!=NULL);
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        CHECK(v.raw_markdown_bytes==n);
        CHECK(!memcmp(v.raw_markdown,emph_shield[k],n));
        CHECK(kind_count(v,SALTS_MD_BLOCK_LINK)==1u);
        CHECK(kind_count(v,SALTS_MD_BLOCK_STRONG)==0u);
        CHECK(kind_count(v,SALTS_MD_BLOCK_EMPHASIS)==0u);
        const size_t nodes=v.document.block_count;
        CHECK(nodes>3u);
        CHECK(v.document.blocks[nodes-1u].kind==SALTS_MD_BLOCK_TEXT);
        salts_md_document_destroy(doc);
        doc=NULL;
        salts_md_document *limited=(salts_md_document*)(uintptr_t)1u;
        CHECK(salts_md_parse(emph_shield[k],n,nodes-1u,n+1u,&limited)==
              SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(limited==NULL);
    }

    /* #183: email autolinks use the same document-owned LINK metadata and
     * failure-atomic safe HTML emitter as URI autolinks. The implicit
     * mailto: prefix is metadata only, not visible label text. */
    static const link_fixture emails[]={
        {"<foo@bar.example.com>\n",
         "mailto:foo@bar.example.com",NULL,"foo@bar.example.com",
         "<p><a href=\"mailto:foo@bar.example.com\">foo@bar.example.com</a></p>\n"},
        {"<foo+special@Bar.baz-bar0.com>\n",
         "mailto:foo+special@Bar.baz-bar0.com",NULL,
         "foo+special@Bar.baz-bar0.com",
         "<p><a href=\"mailto:foo+special@Bar.baz-bar0.com\">foo+special@Bar.baz-bar0.com</a></p>\n"},
        {"<a*b@Bar.example.com>\n",
         "mailto:a*b@Bar.example.com",NULL,"a*b@Bar.example.com",
         "<p><a href=\"mailto:a*b@Bar.example.com\">a*b@Bar.example.com</a></p>\n"},
        {"**<x_y@bar.example.com>**\n",
         "mailto:x_y@bar.example.com",NULL,"x_y@bar.example.com",
         "<p><strong><a href=\"mailto:x_y@bar.example.com\">x_y@bar.example.com</a></strong></p>\n"},    };
    _Static_assert(sizeof(emails)/sizeof(emails[0])==4u,
                   "owned email autolink fixture denominator");
    for(size_t i=0u;i<sizeof(emails)/sizeof(emails[0]);++i)
        if(verify_link(&emails[i]))return 1;

    /* Do not synthesize email links from escaped punctuation, multiple @,
     * malformed domain labels or URL-like text lacking a bracket pair. */
    static const char *const email_rejections[]={
        "<foo\\+@bar.example.com>\n",
        "<foo@@bar.example.com>\n",
        "<foo@-bar.example.com>\n",
        "<foo@bar..example.com>\n",
        "<foo@bar-.example.com>\n",
        "<foo@.example.com>\n",
        "foo@bar.example.com\n",
        "<javascript:alert>\n"
    };
    _Static_assert(sizeof(email_rejections)/sizeof(email_rejections[0])==8u,
                   "email autolink negative cases");
    for(size_t k=0u;k<sizeof(email_rejections)/sizeof(email_rejections[0]);++k) {
        salts_md_document *negative=NULL;
        const size_t bytes=strlen(email_rejections[k]);
        CHECK(salts_md_parse(email_rejections[k],bytes,128u,bytes+1u,
                            &negative)==SALTS_MD_OK);
        CHECK(negative!=NULL);
        const salts_md_rich_document_view view=salts_md_get_document_view(negative);
        CHECK(salts_md_rich_document_validate(&view,128u)==SALTS_MD_OK);
        CHECK(kind_count(view,SALTS_MD_BLOCK_LINK)==0u);
        CHECK(view.raw_markdown_bytes==bytes);
        CHECK(!memcmp(view.raw_markdown,email_rejections[k],bytes));
        salts_md_document_destroy(negative);
    }
    const char bounded_email[]="<foo@bar.example.com>\n";
    salts_md_document *email_limited=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse(bounded_email,sizeof(bounded_email)-1u,3u,
                        sizeof(bounded_email),&email_limited)==
          SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(email_limited==NULL);

    /* Node budget exhaustion with a title must discard every partial node. */
    const char limited[]="[x](/f&ouml;&ouml; \"f&ouml;&ouml;\")\n";
    salts_md_document *rejected=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse(limited,sizeof(limited)-1u,3u,
                        sizeof(limited),&rejected)==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(rejected==NULL);

    puts("C11 link metadata: 30/30 targets, 10 safety rejections, "
         "invalid syntax literal and atomic node-budget rollback");
    return 0;
}
