#include "markdown_entities.h"

#include <stdint.h>
#include <string.h>

typedef struct md_named_character_reference {
    const char *name;
    uint32_t first;
    uint32_t second;
} md_named_character_reference;

#include "markdown_entities_table.inc"

/* The WHATWG HTML5 numeric-reference C1 remapping is syntax normalization,
 * NOT Unicode general-category/punctuation classification. Unassigned C1
 * values remain as their original codepoints (zero marks no remapping). */
static const uint32_t md_c1_replacements[32] = {
    0x20ACu, 0u,      0x201Au, 0x0192u,
    0x201Eu, 0x2026u, 0x2020u, 0x2021u,
    0x02C6u, 0x2030u, 0x0160u, 0x2039u,
    0x0152u, 0u,      0x017Du, 0u,
    0u,      0x2018u, 0x2019u, 0x201Cu,
    0x201Du, 0x2022u, 0x2013u, 0x2014u,
    0x02DCu, 0x2122u, 0x0161u, 0x203Au,
    0x0153u, 0u,      0x017Eu, 0x0178u
};

static bool md_entity_alpha(unsigned char c) {
    return (c>='A' && c<='Z') || (c>='a' && c<='z');
}
static bool md_entity_digit(unsigned char c) {
    return c>='0' && c<='9';
}
static bool md_entity_alnum(unsigned char c) {
    return md_entity_alpha(c) || md_entity_digit(c);
}
static int md_entity_hex_value(unsigned char c) {
    if(md_entity_digit(c))return (int)(c-'0');
    if(c>='A' && c<='F')return (int)(c-'A')+10;
    if(c>='a' && c<='f')return (int)(c-'a')+10;
    return -1;
}
static uint32_t md_entity_scalar(uint32_t cp) {
    if(cp==0u || cp>0x10FFFFu ||
       (cp>=0xD800u && cp<=0xDFFFu))
        return 0xFFFDu;
    if(cp>=0x80u && cp<=0x9Fu) {
        const uint32_t replacement=md_c1_replacements[cp-0x80u];
        if(replacement)cp=replacement;
    }
    return cp;
}
static size_t md_entity_utf8(uint32_t cp,char *out) {
    if(cp<0x80u) {
        out[0]=(char)cp;
        return 1u;
    }
    if(cp<0x800u) {
        out[0]=(char)(0xC0u|(cp>>6));
        out[1]=(char)(0x80u|(cp&0x3Fu));
        return 2u;
    }
    if(cp<0x10000u) {
        out[0]=(char)(0xE0u|(cp>>12));
        out[1]=(char)(0x80u|((cp>>6)&0x3Fu));
        out[2]=(char)(0x80u|(cp&0x3Fu));
        return 3u;
    }
    out[0]=(char)(0xF0u|(cp>>18));
    out[1]=(char)(0x80u|((cp>>12)&0x3Fu));
    out[2]=(char)(0x80u|((cp>>6)&0x3Fu));
    out[3]=(char)(0x80u|(cp&0x3Fu));
    return 4u;
}

static bool md_decode_numeric(const char *s,size_t n,size_t *consumed,
                              char utf8[8],size_t *bytes) {
    size_t p=2u;
    unsigned base=10u,max_digits=7u;
    if(p<n && (s[p]=='x'||s[p]=='X')) {
        base=16u;max_digits=6u;++p;
    }
    uint32_t cp=0u;
    size_t count=0u;
    for(;p<n && count<max_digits;++p) {
        const int digit=md_entity_hex_value((unsigned char)s[p]);
        if(digit<0 || (base==10u && digit>=10))break;
        cp=cp*base+(uint32_t)digit; /* max seven decimals or six hex digits */
        ++count;
    }
    if(!count || p>=n || s[p]!=';')return false;
    char scratch[8];
    const size_t size=md_entity_utf8(md_entity_scalar(cp),scratch);
    memcpy(utf8,scratch,size);
    *bytes=size;
    *consumed=p+1u;
    return true;
}

static bool md_decode_named(const char *s,size_t n,size_t *consumed,
                            char utf8[8],size_t *bytes) {
    /* Maximal ASCII [A-Za-z][A-Za-z0-9]* followed by REQUIRED ';'.
     * A bounded 32-byte buffer prevents unbounded scans on malformed input. */
    if(n<4u || !md_entity_alpha((unsigned char)s[1]))return false;
    size_t p=1u;
    while(p<n && p<MD_MAX_ENTITY_NAME_BYTES &&
          md_entity_alnum((unsigned char)s[p]))++p;
    if(p>=n || s[p]!=';' || p>MD_MAX_ENTITY_NAME_BYTES)return false;
    char key[MD_MAX_ENTITY_NAME_BYTES+1u];
    memcpy(key,s+1u,p); /* from after '&' THROUGH terminating ';' */
    key[p]='\0';
    size_t low=0u,high=sizeof(md_named_references)/
                      sizeof(md_named_references[0]);
    while(low<high) {
        const size_t middle=low+(high-low)/2u;
        const md_named_character_reference *candidate=
            &md_named_references[middle];
        const int cmp=strcmp(key,candidate->name);
        if(cmp<0)high=middle;
        else if(cmp>0)low=middle+1u;
        else {
            char scratch[8];
            size_t written=md_entity_utf8(candidate->first,scratch);
            if(candidate->second)
                written+=md_entity_utf8(candidate->second,scratch+written);
            memcpy(utf8,scratch,written);
            *bytes=written;
            *consumed=p+1u;
            return true;
        }
    }
    return false;
}

bool salts_md_decode_character_reference(const char *source,size_t size,
                                        size_t *consumed,char utf8[8],
                                        size_t *output_bytes) {
    if(!source || !consumed || !utf8 || !output_bytes ||
       size<4u || source[0]!='&')
        return false;
    if(source[1]=='#')
        return md_decode_numeric(source,size,consumed,utf8,output_bytes);
    return md_decode_named(source,size,consumed,utf8,output_bytes);
}
