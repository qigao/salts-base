/* Golden RichDoc structural contract.
 * This is a deliberately bounded subset of CommonMark 0.31.2 semantics.
 * It verifies every node and source/metadata byte, not just parse success.
 * It is NOT a full CommonMark/GFM HTML conformance runner.
 */
#include <salts/markdown.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    salts_md_block_kind kind;
    uint32_t parent;
    uint32_t level;
    const char *text;  /* required only for TEXT nodes */
    const char *metadata; /* NULL means zero metadata bytes */
} expected_node;

typedef struct {
    const char *name;
    const char *markdown;
    const expected_node *nodes;
    size_t count;
} fixture;

#define E(kind_,parent_,level_,text_,meta_) \
    { SALTS_MD_BLOCK_##kind_, parent_, level_, text_, meta_ }
#define LENGTH(a_) (sizeof(a_) / sizeof((a_)[0]))
#define ROOT E(DOCUMENT,SALTS_MD_PARENT_NONE,0,NULL,NULL)
#define PARA(parent_) E(PARAGRAPH,parent_,0,NULL,NULL)
#define TEXT(parent_,value_) E(TEXT,parent_,0,value_,NULL)

static const expected_node heading[] = {
    ROOT, E(HEADING,0,2,NULL,NULL), TEXT(1,"Hello")
};
static const expected_node strong[] = {
    ROOT, PARA(0), TEXT(1,"Normal "), E(STRONG,1,0,NULL,NULL),
    TEXT(3,"Bold"), TEXT(1," text")
};
static const expected_node emphasis[] = {
    ROOT, PARA(0), TEXT(1,"A "), E(EMPHASIS,1,0,NULL,NULL),
    TEXT(3,"bright"), TEXT(1," day.")
};
static const expected_node quote[] = {
    ROOT, E(QUOTE,0,0,NULL,NULL), PARA(1), TEXT(2,"quote")
};
static const expected_node ordered[] = {
    ROOT, E(ORDERED_LIST,0,0,NULL,NULL),
    E(LIST_ITEM,1,0,NULL,NULL), PARA(2), TEXT(3,"alpha"),
    E(LIST_ITEM,1,0,NULL,NULL), PARA(5), TEXT(6,"beta")
};
static const expected_node bullet[] = {
    ROOT, E(LIST,0,0,NULL,NULL),
    E(LIST_ITEM,1,0,NULL,NULL), PARA(2), TEXT(3,"one"),
    E(LIST_ITEM,1,0,NULL,NULL), PARA(5), TEXT(6,"two")
};
static const expected_node fenced[] = {
    ROOT, E(CODE_FENCE,0,0,NULL,"c"), TEXT(1,"int v;\n")
};
static const expected_node link[] = {
    ROOT, PARA(0), TEXT(1,"A "),
    E(LINK,1,0,NULL,"dest"), TEXT(3,"B")
};
static const expected_node code[] = {
    ROOT, PARA(0), TEXT(1,"a "), E(CODE_SPAN,1,0,NULL,NULL),
    TEXT(3,"code"), TEXT(1," b")
};
static const expected_node hard_rule[] = {
    ROOT, E(RULE,0,0,NULL,NULL)
};
static const expected_node setext[] = {
    ROOT, E(HEADING,0,1,NULL,NULL), TEXT(1,"first")
};
static const expected_node soft_break[] = {
    ROOT, PARA(0), TEXT(1,"first"), E(LINE_BREAK,1,0,NULL,NULL),
    TEXT(1,"second")
};
static const expected_node escaped[] = {
    ROOT, PARA(0), TEXT(1,"a "), TEXT(1,"*"), TEXT(1,"b")
};
static const expected_node emoji[] = {
    ROOT, PARA(0), TEXT(1,"中😀 👩‍💻")
};
static const expected_node empty[] = { ROOT };

static const fixture fixtures[] = {
    {"atx-heading", "## Hello\n", heading, LENGTH(heading)},
    {"strong", "Normal **Bold** text\n", strong, LENGTH(strong)},
    {"emphasis", "A *bright* day.\n", emphasis, LENGTH(emphasis)},
    {"blockquote", "> quote\n", quote, LENGTH(quote)},
    {"ordered-list", "1. alpha\n2. beta\n", ordered, LENGTH(ordered)},
    {"bullet-list", "- one\n- two\n", bullet, LENGTH(bullet)},
    {"fenced-code", "\x60\x60\x60" "c\nint v;\n" "\x60\x60\x60\n", fenced, LENGTH(fenced)},
    {"inline-link", "A [B](dest)\n", link, LENGTH(link)},
    {"inline-code", "a " "\x60" "code" "\x60" " b\n", code, LENGTH(code)},
    {"thematic-break", "---\n", hard_rule, LENGTH(hard_rule)},
    {"setext-heading", "first\n===\n", setext, LENGTH(setext)},
    {"paragraph-soft-line", "first\nsecond\n", soft_break, LENGTH(soft_break)},
    {"escaped-punctuation", "a \\*b\n", escaped, LENGTH(escaped)},
    {"unicode-byte-preservation", "中😀 👩‍💻\n", emoji, LENGTH(emoji)},
    {"empty-document", "", empty, LENGTH(empty)}
};

_Static_assert(LENGTH(fixtures) == 15, "Explicit golden fixture denominator changed");

static int same_bytes(const char *actual, size_t actual_len,
                      const char *expected) {
    if (!expected) return actual_len == 0;
    const size_t len = strlen(expected);
    return actual && actual_len == len && memcmp(actual, expected, len) == 0;
}

static int check_fixture(const fixture *f) {
    salts_md_document *doc = NULL;
    const size_t size = strlen(f->markdown);
    const salts_md_status rc = salts_md_parse(
        f->markdown, size, 256u, size + 1u, &doc);
    if (rc != SALTS_MD_OK || doc == NULL) {
        fprintf(stderr, "%s: parse rc=%d, doc=%p\n",f->name,(int)rc,(void *)doc);
        return 1;
    }
    const salts_md_rich_document_view view = salts_md_get_document_view(doc);
    int failed = 0;
    if (salts_md_rich_document_validate(&view,256u) != SALTS_MD_OK ||
        view.raw_markdown_bytes != size ||
        memcmp(view.raw_markdown, f->markdown, size) != 0 ||
        view.document.block_count != f->count) {
        fprintf(stderr,"%s: structural count/raw bytes expected=%zu actual=%zu\n",
                f->name,f->count,view.document.block_count);
        failed = 1;
        goto finish;
    }
    for (size_t i = 0; i < f->count; ++i) {
        const salts_md_block *got = &view.document.blocks[i];
        const expected_node *exp = &f->nodes[i];
        if (got->kind != exp->kind ||
            got->parent_index != exp->parent ||
            got->heading_level != exp->level ||
            got->stable_id != (uint64_t)(i+1u)) {
            fprintf(stderr,
                    "%s: node %zu kind=%d/%d parent=%u/%u level=%u/%u\n",
                    f->name,i,(int)got->kind,(int)exp->kind,
                    got->parent_index,exp->parent,
                    got->heading_level,exp->level);
            failed = 1;
            break;
        }
        if (got->kind == SALTS_MD_BLOCK_TEXT) {
            const char *start = view.document.source + got->byte_begin;
            const size_t length = got->byte_end - got->byte_begin;
            if (!exp->text || !same_bytes(start,length,exp->text)) {
                fprintf(stderr,"%s: node %zu semantic text mismatch\n",f->name,i);
                failed = 1;
                break;
            }
        } else if (exp->text) {
            fprintf(stderr,"%s: node %zu unexpected text expectation\n",f->name,i);
            failed = 1;
            break;
        }
        const char *metadata = NULL;
        size_t length = SIZE_MAX;
        if(salts_md_rich_document_metadata(&view,i,&metadata,&length)
                != SALTS_MD_OK) {
            fprintf(stderr,"%s: node %zu metadata lookup failed\n",f->name,i);
            failed=1;
            break;
        }
        bool match=same_bytes(metadata,length,exp->metadata);
        if((got->kind==SALTS_MD_BLOCK_LIST ||
            got->kind==SALTS_MD_BLOCK_ORDERED_LIST) &&
           exp->metadata==NULL) {
            salts_md_list_semantics info={0};
            if(metadata && length==sizeof(info)) {
                memcpy(&info,metadata,sizeof(info));
                const bool ordered=got->kind==SALTS_MD_BLOCK_ORDERED_LIST;
                match=info.start==(ordered?1u:0u) &&
                      info.marker==(ordered?'.':'-') &&
                      info.loose==0u &&
                      info.reserved[0]==0u && info.reserved[1]==0u;
            } else match=false;
        }
        if (!match) {
            fprintf(stderr,"%s: node %zu metadata mismatch\n",f->name,i);
            failed = 1;
            break;
        }
    }
finish:
    salts_md_document_destroy(doc);
    return failed;
}

int main(void) {
    for (size_t i=0; i<LENGTH(fixtures); ++i)
        if (check_fixture(&fixtures[i])) return 1;
    printf("C11 RichDoc golden fixtures: %zu/%zu passed (explicit subset; not full CommonMark)\n",
           LENGTH(fixtures), LENGTH(fixtures));
    return 0;
}
