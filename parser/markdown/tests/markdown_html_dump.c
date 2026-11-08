/* Test-only native RichDoc -> HTML CLI.
 * Input: stdin exact UTF-8 bytes (at most 1 MiB).
 * Output: stdout HTML bytes only; failure emits diagnostics to stderr.
 * Uses the same production-candidate C11 parse + renderer implementations.
 */
#include <salts/markdown.h>
#include <salts/markdown_html.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

enum {
    MD_TEST_INPUT_MAX = 1 << 20,
    MD_TEST_OUTPUT_MAX = 8 << 20,
    MD_TEST_NODES_MAX = 16384
};

int main(int argc,char **argv) {
#if defined(_WIN32)
    /* Test oracles compare byte-exact HTML/AST against CommonMark/GFM.
     * On Windows CRT text streams convert LF <-> CRLF, causing false
     * mismatches and erasing CRLF source evidence. No normalization is allowed.
     */
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1) {
        fputs("Markdown test oracle: cannot select binary stdio\n", stderr);
        return 2;
    }
#endif
    /* An explicit CLI flag requests unsafe literal HTML for trusted
     * conformance testing. Zero-argument invocation stays production-safe. */
    const bool spec_mode=argc==2 && !strcmp(argv[1],"--spec-html");
    if(argc!=1 && !spec_mode) {
        fputs("markdown HTML test CLI: invalid render policy\n",stderr);
        return 2;
    }
    const salts_md_html_policy policy=spec_mode?
        SALTS_MD_HTML_SPEC_CONFORMANCE:SALTS_MD_HTML_SAFE_APPLICATION;
    char *source = (char *)malloc(MD_TEST_INPUT_MAX + 1u);
    if (!source) {
        fputs("markdown HTML test CLI: input OOM\n", stderr);
        return 2;
    }
    size_t length=0u;
    while(length<MD_TEST_INPUT_MAX) {
        const size_t n = fread(source+length,1u,MD_TEST_INPUT_MAX-length,stdin);
        length+=n;
        if(!n) break;
    }
    unsigned char extra=0u;
    if(ferror(stdin) || (length==MD_TEST_INPUT_MAX &&
                        fread(&extra,1u,1u,stdin)!=0u)) {
        fputs("markdown HTML test CLI: input too large or I/O failure\n",stderr);
        free(source);
        return 2;
    }

    salts_md_document *doc=NULL;
    salts_md_status status=salts_md_parse(
        source,length,MD_TEST_NODES_MAX,MD_TEST_INPUT_MAX,&doc);
    free(source);
    if(status!=SALTS_MD_OK || !doc) {
        fprintf(stderr,"markdown HTML test CLI: parse error %d\n",(int)status);
        return 2;
    }
    salts_md_rich_document_view view=salts_md_get_document_view(doc);
    size_t size=0u;
    status=salts_md_render_html_with_policy(&view,MD_TEST_NODES_MAX,
                                             policy,NULL,0u,&size);
    if(status!=SALTS_MD_OK || size>MD_TEST_OUTPUT_MAX) {
        fprintf(stderr,"markdown HTML test CLI: render/count error %d (%zu)\n",
                (int)status,size);
        salts_md_document_destroy(doc);
        return 2;
    }

    char *html=(char*)malloc(size+1u);
    if(!html) {
        fputs("markdown HTML test CLI: output OOM\n",stderr);
        salts_md_document_destroy(doc);
        return 2;
    }
    status=salts_md_render_html_with_policy(&view,MD_TEST_NODES_MAX,
                                             policy,html,size+1u,&size);
    if(status!=SALTS_MD_OK) {
        fprintf(stderr,"markdown HTML test CLI: render error %d\n",(int)status);
        free(html);
        salts_md_document_destroy(doc);
        return 2;
    }

    const bool wrote=(size==0u || fwrite(html,1u,size,stdout)==size) &&
                     fflush(stdout)==0 && !ferror(stdout);
    free(html);
    salts_md_document_destroy(doc);
    if(!wrote) {
        fputs("markdown HTML test CLI: stdout write failed\n",stderr);
        return 2;
    }
    return 0;
}
