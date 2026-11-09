#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <asn1.h>
#include <tinytest.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

/* Independent base-128 state oracle: a leading 0x80 is nonminimal; an arc
 * ending in a continuation octet is incomplete. No fixed arc-width limit is
 * imposed by the reader (the typed uint32_t accessor has its own limit). */
static asn1_status oid_status(const uint8_t *content, size_t length) {
    int continuation = 0;
    if (length == 0u) return SALTS_ASN1_MALFORMED;
    for (size_t i = 0u; i < length; ++i) {
        if (!continuation && content[i] == 0x80u) return SALTS_ASN1_MALFORMED;
        continuation = (content[i] & 0x80u) != 0u;
    }
    return continuation ? SALTS_ASN1_UNEXPECTED_END : SALTS_ASN1_OK;
}

static void check_oid(uint8_t *wire, size_t length) {
    const asn1_status expected = oid_status(wire + 2u, length);
    wire[1] = (uint8_t)length;
    for (unsigned tag = 6u; tag <= 13u; tag += 7u) {
        wire[0] = (uint8_t)tag;
        for (unsigned mode = SALTS_ASN1_BER; mode <= SALTS_ASN1_DER; ++mode) {
            check_equal(asn1_validate(wire, length + 2u, (asn1_mode)mode), expected);
            asn1_reader reader;
            asn1_tlv value = {0};
            value.content_size = 123u;
            check_equal(asn1_reader_init(&reader, wire, length + 2u, (asn1_mode)mode), SALTS_ASN1_OK);
            check_equal(asn1_reader_next(&reader, &value), expected);
            if (expected == SALTS_ASN1_OK) {
                check_equal(reader.offset, length + 2u);
                check_equal(value.tag_number, tag);
                check(value.content == wire + 2u);
                check_equal(value.content_size, length);
            } else {
                check_equal(reader.offset, 0u);
                check_equal(value.content_size, 123u);
                check(value.content == NULL);
            }
        }
    }
}

suite("ASN.1 OID counted vector boundaries") {
    it("preserves each octet class at every lane, alignment and partial tail") {
        uint8_t storage[112];
        static const uint8_t octets[] = {0u, 0x7fu, 0x80u, 0x81u, 0xffu};
        for (size_t alignment = 0u; alignment < 16u; ++alignment) {
            uint8_t *wire = storage + alignment;
            for (size_t length = 0u; length <= 80u; ++length) {
                memset(wire + 2u, 1, length);
                check_oid(wire, length);
                for (size_t at = 0u; at < length; ++at) {
                    for (size_t b = 0u; b < sizeof(octets); ++b) {
                        wire[at + 2u] = octets[b];
                        check_oid(wire, length);
                    }
                    wire[at + 2u] = 1u;
                }
            }
        }
    }

    it("accepts zero payload continuation octets but rejects them at arc starts") {
        uint8_t wire[96];
        for (size_t prefix = 0u; prefix <= 64u; ++prefix) {
            memset(wire + 2u, 1, prefix);
            wire[prefix + 2u] = 0x81;
            wire[prefix + 3u] = 0x80;
            check_oid(wire, prefix + 2u);
            wire[prefix + 4u] = 0u;
            check_oid(wire, prefix + 3u);
            wire[prefix + 5u] = 0x80;
            check_oid(wire, prefix + 4u);
        }
    }

    it("preserves malformed-before-truncated error precedence after full vectors") {
        uint8_t wire[96];
        for (size_t prefix = 0u; prefix <= 64u; ++prefix) {
            memset(wire + 2u, 1, prefix);
            wire[prefix + 2u] = 0x80;
            wire[prefix + 3u] = 0x81;
            check_oid(wire, prefix + 2u);
        }
    }
}

suite("ASN.1 OID protected input") {
    static uint8_t *mapping;
    static size_t page_size;
    before_all() {
#if defined(_WIN32)
        SYSTEM_INFO info;
        DWORD old_protection;
        GetSystemInfo(&info);
        page_size = (size_t)info.dwPageSize;
        mapping = (uint8_t *)VirtualAlloc(NULL, page_size * 2u,
                                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        check_not_null(mapping);
        check(VirtualProtect(mapping + page_size, page_size, PAGE_NOACCESS, &old_protection) != 0);
#else
        const long native_page_size = sysconf(_SC_PAGESIZE);
        check(native_page_size >= 82);
        page_size = (size_t)native_page_size;
        void *pages = mmap(NULL, page_size * 2u, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        check(pages != MAP_FAILED);
        mapping = (uint8_t *)pages;
        check_equal(mprotect(mapping + page_size, page_size, PROT_NONE), 0);
#endif
    }
    after_all() {
        if (mapping != NULL) {
#if defined(_WIN32)
            check(VirtualFree(mapping, 0, MEM_RELEASE) != 0);
#else
            check_equal(munmap(mapping, page_size * 2u), 0);
#endif
            mapping = NULL;
        }
    }
    it("never reads beyond a counted OID, including truncated last arcs") {
        check(page_size >= 82u);
        for (size_t length = 0u; length <= 80u; ++length) {
            uint8_t *wire = mapping + page_size - length - 2u;
            memset(wire + 2u, 1, length);
            check_oid(wire, length);
            if (length != 0u) {
                wire[length + 1u] = 0x81;
                check_oid(wire, length);
                wire[length + 1u] = 0x80;
                check_oid(wire, length);
            }
        }
    }
}
