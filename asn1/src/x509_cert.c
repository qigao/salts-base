/** @file x509_cert.c @brief X.509 DER certificate parser. */
#include <asn1/x509_cert.h>
#include <asn1_value.h>
#include <core/platform.h>
#include <stdlib.h>
#include <string.h>

static int tag_is(const asn1_value *value, uint8_t tag_class,
                  uint8_t constructed, uint32_t tag_number) {
    uint8_t actual_class = 0u, actual_constructed = 0u;
    uint32_t actual_number = 0u;
    return value &&
           asn1_value_get_tag(value, &actual_class, &actual_constructed,
                                    &actual_number) == SALTS_ASN1_OK &&
           actual_class == tag_class && actual_constructed == constructed &&
           actual_number == tag_number;
}

static const asn1_value *child(const asn1_value *value,
                                     size_t index) {
    return asn1_value_child_at(value, index);
}

static int copy_content(const asn1_value *value, uint8_t **out,
                        size_t *out_size) {
    const uint8_t *data = NULL;
    size_t size = 0u;
    if (!out || !out_size ||
        asn1_value_get_content(value, &data, &size) != SALTS_ASN1_OK)
        return -1;
    *out = NULL;
    *out_size = size;
    if (size != 0u) {
        *out = (uint8_t *)malloc(size);
        if (!*out) return -1;
        memcpy(*out, data, size);
    }
    return 0;
}

static bool oid_equals(const uint32_t *oid, size_t count, const char *text) {
    uint32_t expected[16];
    size_t expected_count = 0u;
    while (*text && expected_count < 16u) {
        char *end = NULL;
        unsigned long value = strtoul(text, &end, 10);
        if (text == end || value > UINT32_MAX) return false;
        expected[expected_count++] = (uint32_t)value;
        text = *end == '.' ? end + 1 : end;
    }
    return !*text && count == expected_count &&
           memcmp(oid, expected, count * sizeof(*oid)) == 0;
}

static char *string_from_asn1(const asn1_value *value) {
    const uint8_t *data = NULL;
    size_t size = 0u;
    char *string;
    if (!value || asn1_value_get_content(value, &data, &size) !=
                      SALTS_ASN1_OK ||
        size == 0u)
        return NULL;
    string = (char *)malloc(size + 1u);
    if (!string) return NULL;
    memcpy(string, data, size);
    string[size] = '\0';
    return string;
}

static time_t parse_asn1_time(const asn1_value *value) {
    const uint8_t *data = NULL;
    size_t size = 0u;
    uint8_t tag_class = 0u, constructed = 0u;
    uint32_t tag_number = 0u;
    struct tm parsed = {0};
    const char *text;
    if (asn1_value_get_tag(value, &tag_class, &constructed, &tag_number) !=
            SALTS_ASN1_OK ||
        tag_class != 0u || constructed ||
        asn1_value_get_content(value, &data, &size) != SALTS_ASN1_OK)
        return 0;
    text = (const char *)data;
    if (tag_number == 23u) {
        int year;
        if (size != 13u || text[12] != 'Z') return 0;
        year = (text[0] - '0') * 10 + text[1] - '0';
        parsed.tm_year = (year < 50 ? 2000 + year : 1900 + year) - 1900;
        parsed.tm_mon = (text[2] - '0') * 10 + text[3] - '0' - 1;
        parsed.tm_mday = (text[4] - '0') * 10 + text[5] - '0';
        parsed.tm_hour = (text[6] - '0') * 10 + text[7] - '0';
        parsed.tm_min = (text[8] - '0') * 10 + text[9] - '0';
        parsed.tm_sec = (text[10] - '0') * 10 + text[11] - '0';
    } else if (tag_number == 24u) {
        if (size != 15u || text[14] != 'Z') return 0;
        parsed.tm_year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                         (text[2] - '0') * 10 + text[3] - '0' - 1900;
        parsed.tm_mon = (text[4] - '0') * 10 + text[5] - '0' - 1;
        parsed.tm_mday = (text[6] - '0') * 10 + text[7] - '0';
        parsed.tm_hour = (text[8] - '0') * 10 + text[9] - '0';
        parsed.tm_min = (text[10] - '0') * 10 + text[11] - '0';
        parsed.tm_sec = (text[12] - '0') * 10 + text[13] - '0';
    } else {
        return 0;
    }
    return time_from_utc(&parsed);
}

static int copy_oid(const asn1_value *value, uint32_t **out,
                    size_t *out_count) {
    size_t count = 0u;
    uint32_t *components;
    if (!out || !out_count ||
        asn1_value_get_oid(value, NULL, 0u, &count) != SALTS_ASN1_OK ||
        count > SIZE_MAX / sizeof(*components))
        return -1;
    components = (uint32_t *)malloc(count * sizeof(*components));
    if (!components) return -1;
    if (asn1_value_get_oid(value, components, count, &count) !=
        SALTS_ASN1_OK) {
        free(components);
        return -1;
    }
    *out = components;
    *out_count = count;
    return 0;
}

static int parse_name(const asn1_value *sequence, x509_name_t *name) {
    if (!tag_is(sequence, 0u, 1u, 16u)) return -1;
    memset(name, 0, sizeof(*name));
    for (size_t i = 0u; i < asn1_value_child_count(sequence); ++i) {
        const asn1_value *rdn = child(sequence, i);
        if (!tag_is(rdn, 0u, 1u, 17u)) continue;
        for (size_t j = 0u; j < asn1_value_child_count(rdn); ++j) {
            const asn1_value *attribute = child(rdn, j);
            const asn1_value *oid_value, *text_value;
            uint32_t *oid = NULL;
            size_t oid_count = 0u;
            char *text;
            char **destination = NULL;
            if (!tag_is(attribute, 0u, 1u, 16u) ||
                asn1_value_child_count(attribute) < 2u)
                continue;
            oid_value = child(attribute, 0u);
            text_value = child(attribute, 1u);
            if (copy_oid(oid_value, &oid, &oid_count) != 0) continue;
            text = string_from_asn1(text_value);
            if (oid_equals(oid, oid_count, X509_OID_AT_CN))
                destination = &name->common_name;
            else if (oid_equals(oid, oid_count, X509_OID_AT_ORG))
                destination = &name->organization;
            else if (oid_equals(oid, oid_count, X509_OID_AT_OU))
                destination = &name->organizational_unit;
            else if (oid_equals(oid, oid_count, X509_OID_AT_COUNTRY))
                destination = &name->country;
            else if (oid_equals(oid, oid_count, X509_OID_AT_STATE))
                destination = &name->state;
            else if (oid_equals(oid, oid_count, X509_OID_AT_LOCALITY))
                destination = &name->locality;
            else if (oid_equals(oid, oid_count, X509_OID_AT_EMAIL))
                destination = &name->email;
            free(oid);
            if (destination) {
                free(*destination);
                *destination = text;
            } else {
                free(text);
            }
        }
    }
    return 0;
}

static int parse_algorithm(const asn1_value *sequence,
                           x509_algorithm_t *algorithm) {
    if (!tag_is(sequence, 0u, 1u, 16u) ||
        asn1_value_child_count(sequence) < 1u)
        return -1;
    memset(algorithm, 0, sizeof(*algorithm));
    return copy_oid(child(sequence, 0u), &algorithm->oid,
                    &algorithm->oid_count);
}

static int parse_rsa_pubkey(const uint8_t *der, size_t der_size,
                            x509_rsa_pubkey_t *rsa) {
    asn1_value *sequence = NULL;
    const asn1_value *modulus, *exponent;
    if (asn1_value_decode(der, der_size, SALTS_ASN1_DER, &sequence) !=
            SALTS_ASN1_OK ||
        !tag_is(sequence, 0u, 1u, 16u) ||
        asn1_value_child_count(sequence) != 2u) {
        asn1_value_free(sequence);
        return -1;
    }
    modulus = child(sequence, 0u);
    exponent = child(sequence, 1u);
    if (!tag_is(modulus, 0u, 0u, 2u) || !tag_is(exponent, 0u, 0u, 2u) ||
        copy_content(modulus, &rsa->modulus, &rsa->modulus_len) != 0 ||
        copy_content(exponent, &rsa->exponent, &rsa->exponent_len) != 0) {
        asn1_value_free(sequence);
        return -1;
    }
    asn1_value_free(sequence);
    return 0;
}

static int parse_pubkey_info(const asn1_value *sequence,
                             x509_pubkey_info_t *public_key) {
    const asn1_value *bits;
    const uint8_t *content = NULL;
    size_t size = 0u;
    if (!tag_is(sequence, 0u, 1u, 16u) ||
        asn1_value_child_count(sequence) != 2u)
        return -1;
    memset(public_key, 0, sizeof(*public_key));
    if (parse_algorithm(child(sequence, 0u), &public_key->algorithm) != 0)
        return -1;
    bits = child(sequence, 1u);
    if (!tag_is(bits, 0u, 0u, 3u) ||
        asn1_value_get_content(bits, &content, &size) != SALTS_ASN1_OK ||
        size == 0u || content[0] > 7u)
        return -1;
    ++content;
    --size;
    if (oid_equals(public_key->algorithm.oid, public_key->algorithm.oid_count,
                   X509_OID_RSA_ENCRYPTION)) {
        public_key->key_type = X509_PUBKEY_RSA;
        return parse_rsa_pubkey(content, size, &public_key->key.rsa);
    }
    if (oid_equals(public_key->algorithm.oid, public_key->algorithm.oid_count,
                   X509_OID_EC_PUBKEY)) {
        public_key->key_type = X509_PUBKEY_EC;
        public_key->key.ec.point = size ? (uint8_t *)malloc(size) : NULL;
        if (size && !public_key->key.ec.point) return -1;
        if (size) memcpy(public_key->key.ec.point, content, size);
        public_key->key.ec.point_len = size;
        return 0;
    }
    public_key->key_type =
        oid_equals(public_key->algorithm.oid, public_key->algorithm.oid_count,
                   X509_OID_ED25519)
            ? X509_PUBKEY_ED25519
            : X509_PUBKEY_UNKNOWN;
    public_key->key.raw.data = size ? (uint8_t *)malloc(size) : NULL;
    if (size && !public_key->key.raw.data) return -1;
    if (size) memcpy(public_key->key.raw.data, content, size);
    public_key->key.raw.len = size;
    return 0;
}

static int parse_tbs_certificate(const asn1_value *tbs,
                                 x509_tbs_cert_t *certificate) {
    size_t index = 0u, count;
    const asn1_value *field;
    uint8_t tag_class = 0u, constructed = 0u;
    uint32_t tag_number = 0u;
    int64_t version = 0;
    if (!tag_is(tbs, 0u, 1u, 16u)) return -1;
    memset(certificate, 0, sizeof(*certificate));
    count = asn1_value_child_count(tbs);
    if (count < 6u) return -1;
    field = child(tbs, index);
    if (asn1_value_get_tag(field, &tag_class, &constructed, &tag_number) ==
            SALTS_ASN1_OK &&
        tag_class == 2u && constructed && tag_number == 0u) {
        if (asn1_value_child_count(field) != 1u ||
            asn1_value_get_integer_i64(child(field, 0u), &version) !=
                SALTS_ASN1_OK ||
            version < 0 || version > 2)
            return -1;
        certificate->version = (int)version;
        ++index;
    }
    if (index >= count || !tag_is(child(tbs, index), 0u, 0u, 2u) ||
        copy_content(child(tbs, index++), &certificate->serial_number,
                     &certificate->serial_number_len) != 0 ||
        index >= count ||
        parse_algorithm(child(tbs, index++), &certificate->signature) != 0 ||
        index >= count ||
        parse_name(child(tbs, index++), &certificate->issuer) != 0 ||
        index >= count)
        return -1;
    field = child(tbs, index++);
    if (!tag_is(field, 0u, 1u, 16u) ||
        asn1_value_child_count(field) != 2u)
        return -1;
    certificate->validity.not_before = parse_asn1_time(child(field, 0u));
    certificate->validity.not_after = parse_asn1_time(child(field, 1u));
    if (index >= count ||
        parse_name(child(tbs, index++), &certificate->subject) != 0 ||
        index >= count ||
        parse_pubkey_info(child(tbs, index++), &certificate->public_key) != 0)
        return -1;
    return 0;
}

int x509_cert_parse(const uint8_t *der, size_t der_size,
                    x509_cert_t **certificate_out) {
    asn1_value *root = NULL;
    x509_cert_t *certificate;
    const asn1_value *signature;
    const uint8_t *signature_content = NULL;
    size_t signature_size = 0u;
    if (!certificate_out) return -1;
    *certificate_out = NULL;
    if (!der || der_size == 0u) return -1;
    if (asn1_value_decode(der, der_size, SALTS_ASN1_DER, &root) !=
            SALTS_ASN1_OK ||
        !tag_is(root, 0u, 1u, 16u) ||
        asn1_value_child_count(root) != 3u) {
        asn1_value_free(root);
        return -1;
    }
    certificate = (x509_cert_t *)calloc(1u, sizeof(*certificate));
    if (!certificate) {
        asn1_value_free(root);
        return -1;
    }
    signature = child(root, 2u);
    if (parse_tbs_certificate(child(root, 0u), &certificate->tbs) != 0 ||
        parse_algorithm(child(root, 1u), &certificate->sig_algorithm) != 0 ||
        !tag_is(signature, 0u, 0u, 3u) ||
        asn1_value_get_content(signature, &signature_content,
                                     &signature_size) != SALTS_ASN1_OK ||
        signature_size == 0u || signature_content[0] > 7u) {
        x509_cert_free(certificate);
        asn1_value_free(root);
        return -1;
    }
    certificate->signature_unused_bits = signature_content[0];
    certificate->signature_len = signature_size - 1u;
    if (certificate->signature_len) {
        certificate->signature = (uint8_t *)malloc(certificate->signature_len);
        if (!certificate->signature) {
            x509_cert_free(certificate);
            asn1_value_free(root);
            return -1;
        }
        memcpy(certificate->signature, signature_content + 1u,
               certificate->signature_len);
    }
    certificate->der = (uint8_t *)malloc(der_size);
    if (!certificate->der) {
        x509_cert_free(certificate);
        asn1_value_free(root);
        return -1;
    }
    memcpy(certificate->der, der, der_size);
    certificate->der_len = der_size;
    asn1_value_free(root);
    *certificate_out = certificate;
    return 0;
}

void x509_name_free(x509_name_t *name) {
    if (!name) return;
    free(name->common_name);
    free(name->organization);
    free(name->organizational_unit);
    free(name->country);
    free(name->state);
    free(name->locality);
    free(name->email);
    free(name->der);
}

void x509_cert_free(x509_cert_t *certificate) {
    if (!certificate) return;
    free(certificate->tbs.serial_number);
    free(certificate->tbs.signature.oid);
    free(certificate->tbs.signature.parameters);
    x509_name_free(&certificate->tbs.issuer);
    x509_name_free(&certificate->tbs.subject);
    free(certificate->tbs.public_key.algorithm.oid);
    free(certificate->tbs.public_key.algorithm.parameters);
    if (certificate->tbs.public_key.key_type == X509_PUBKEY_RSA) {
        free(certificate->tbs.public_key.key.rsa.modulus);
        free(certificate->tbs.public_key.key.rsa.exponent);
    } else if (certificate->tbs.public_key.key_type == X509_PUBKEY_EC) {
        free(certificate->tbs.public_key.key.ec.curve_oid);
        free(certificate->tbs.public_key.key.ec.point);
    } else {
        free(certificate->tbs.public_key.key.raw.data);
    }
    for (size_t i = 0u; i < certificate->tbs.extensions_count; ++i) {
        free(certificate->tbs.extensions[i].oid);
        free(certificate->tbs.extensions[i].value);
    }
    free(certificate->tbs.extensions);
    free(certificate->tbs.der);
    free(certificate->sig_algorithm.oid);
    free(certificate->sig_algorithm.parameters);
    free(certificate->signature);
    free(certificate->der);
    free(certificate);
}

bool x509_cert_is_valid_at(const x509_cert_t *certificate, time_t time_value) {
    return certificate && time_value >= certificate->tbs.validity.not_before &&
           time_value <= certificate->tbs.validity.not_after;
}

bool x509_cert_is_valid_now(const x509_cert_t *certificate) {
    return x509_cert_is_valid_at(certificate, time(NULL));
}

const char *x509_cert_get_subject_cn(const x509_cert_t *certificate) {
    return certificate ? certificate->tbs.subject.common_name : NULL;
}

const char *x509_cert_get_issuer_cn(const x509_cert_t *certificate) {
    return certificate ? certificate->tbs.issuer.common_name : NULL;
}

bool x509_cert_is_self_signed(const x509_cert_t *certificate) {
    const x509_name_t *issuer, *subject;
    if (!certificate) return false;
    issuer = &certificate->tbs.issuer;
    subject = &certificate->tbs.subject;
#define STR_EQ(a, b) (((a) == (b)) || ((a) && (b) && strcmp((a), (b)) == 0))
    return STR_EQ(issuer->common_name, subject->common_name) &&
           STR_EQ(issuer->organization, subject->organization) &&
           STR_EQ(issuer->country, subject->country);
#undef STR_EQ
}

x509_pubkey_type_t x509_cert_get_pubkey_type(const x509_cert_t *certificate) {
    return certificate ? certificate->tbs.public_key.key_type
                       : X509_PUBKEY_UNKNOWN;
}
