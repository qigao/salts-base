/* Test-only C11 RichDoc dump for independent Python Markdown AST comparison.
 * The parser remains in Stun::UICore; this tool is not a rendering backend.
 * Each line: index TAB kind TAB parent TAB heading-level TAB text-hex
 *            TAB metadata-hex. No JSON/text escaping or third-party runtime.
 */
#include <salts/markdown.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

enum {
    SALTS_MD_ORACLE_MAX_SOURCE = 1 << 20,
    SALTS_MD_ORACLE_MAX_NODES = 16384
};

static const char *node_kind(salts_md_block_kind kind) {
    switch (kind) {
    case SALTS_MD_BLOCK_DOCUMENT: return "document";
    case SALTS_MD_BLOCK_PARAGRAPH: return "paragraph";
    case SALTS_MD_BLOCK_HEADING: return "heading";
    case SALTS_MD_BLOCK_TEXT: return "text";
    case SALTS_MD_BLOCK_CODE_FENCE: return "code_fence";
    case SALTS_MD_BLOCK_CODE_INDENTED: return "code_indented";
    case SALTS_MD_BLOCK_CODE_SPAN: return "code_span";
    case SALTS_MD_BLOCK_LIST: return "list";
    case SALTS_MD_BLOCK_LIST_ITEM: return "list_item";
    case SALTS_MD_BLOCK_QUOTE: return "quote";
    case SALTS_MD_BLOCK_TABLE: return "table";
    case SALTS_MD_BLOCK_TABLE_ROW: return "table_row";
    case SALTS_MD_BLOCK_TABLE_CELL: return "table_cell";
    case SALTS_MD_BLOCK_ORDERED_LIST: return "ordered_list";
    case SALTS_MD_BLOCK_EMPHASIS: return "emphasis";
    case SALTS_MD_BLOCK_STRONG: return "strong";
    case SALTS_MD_BLOCK_LINK: return "link";
    case SALTS_MD_BLOCK_IMAGE: return "image";
    case SALTS_MD_BLOCK_STRIKETHROUGH: return "strikethrough";
    case SALTS_MD_BLOCK_UNDERLINE: return "underline";
    case SALTS_MD_BLOCK_RULE: return "rule";
    case SALTS_MD_BLOCK_ENTITY: return "entity";
    case SALTS_MD_BLOCK_LINE_BREAK: return "line_break";
    case SALTS_MD_BLOCK_TASK_ITEM: return "task_item";
    case SALTS_MD_BLOCK_CHECKBOX: return "checkbox";
    case SALTS_MD_BLOCK_MATH: return "math";
    case SALTS_MD_BLOCK_MATH_BLOCK: return "math_block";
    case SALTS_MD_BLOCK_HIGHLIGHT: return "highlight";
    case SALTS_MD_BLOCK_SUPERSCRIPT: return "superscript";
    case SALTS_MD_BLOCK_SUBSCRIPT: return "subscript";
    case SALTS_MD_BLOCK_FOOTNOTE: return "footnote";
    case SALTS_MD_BLOCK_FOOTNOTE_DEF: return "footnote_def";
    case SALTS_MD_BLOCK_DEFINITION_LIST: return "definition_list";
    case SALTS_MD_BLOCK_DEFINITION_TERM: return "definition_term";
    case SALTS_MD_BLOCK_DEFINITION_DESC: return "definition_desc";
    case SALTS_MD_BLOCK_DIAGRAM: return "diagram";
    default: return NULL;
    }
}

static void write_hex(const char *bytes, size_t length) {
    static const char digits[] = "0123456789abcdef";
    if (!bytes) return;
    for (size_t i=0; i<length; ++i) {
        const unsigned char value=(unsigned char)bytes[i];
        putchar(digits[value >> 4]);
        putchar(digits[value & 15u]);
    }
}

int main(void) {
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
    char *bytes=(char *)malloc(SALTS_MD_ORACLE_MAX_SOURCE+1u);
    if (!bytes) {
        fputs("AST dump: source allocation failed\n",stderr);
        return 2;
    }
    size_t length=0;
    while (length<SALTS_MD_ORACLE_MAX_SOURCE) {
        const size_t n=fread(bytes+length,1u,
            SALTS_MD_ORACLE_MAX_SOURCE-length,stdin);
        length+=n;
        if (!n) break;
    }
    unsigned char extra=0;
    if (ferror(stdin) ||
        (length==SALTS_MD_ORACLE_MAX_SOURCE &&
         fread(&extra,1u,1u,stdin)!=0u)) {
        fputs("AST dump: source exceeds 1 MiB or stdin failed\n",stderr);
        free(bytes);
        return 2;
    }
    salts_md_document *doc=NULL;
    const salts_md_status status=salts_md_parse(
        bytes,length,SALTS_MD_ORACLE_MAX_NODES,
        SALTS_MD_ORACLE_MAX_SOURCE,&doc);
    free(bytes);
    if (status!=SALTS_MD_OK || !doc) {
        fprintf(stderr,"AST dump: parse failed (status=%d)\n",(int)status);
        return 2;
    }

    const salts_md_rich_document_view view=salts_md_get_document_view(doc);
    if (salts_md_rich_document_validate(
            &view,SALTS_MD_ORACLE_MAX_NODES)!=SALTS_MD_OK) {
        fputs("AST dump: invalid RichDoc\n",stderr);
        salts_md_document_destroy(doc);
        return 2;
    }
    for (size_t i=0;i<view.document.block_count;++i) {
        const salts_md_block *node=&view.document.blocks[i];
        const char *kind=node_kind(node->kind);
        if (!kind) {
            fputs("AST dump: unrecognized block kind\n",stderr);
            salts_md_document_destroy(doc);
            return 2;
        }
        printf("%zu\t%s\t%u\t%u\t",i,kind,
               node->parent_index,node->heading_level);
        if (node->kind==SALTS_MD_BLOCK_TEXT) {
            write_hex(view.document.source+node->byte_begin,
                      node->byte_end-node->byte_begin);
        }
        putchar('\t');
        const char *metadata=NULL;
        size_t metadata_size=0;
        if (salts_md_rich_document_metadata(
                &view,i,&metadata,&metadata_size)!=SALTS_MD_OK) {
            fputs("AST dump: invalid metadata range\n",stderr);
            salts_md_document_destroy(doc);
            return 2;
        }
        write_hex(metadata,metadata_size);
        putchar('\n');
    }
    const int result=ferror(stdout) ? 2 : 0;
    salts_md_document_destroy(doc);
    return result;
}
