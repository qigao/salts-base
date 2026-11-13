#include <asn1.h>
#include <asn1_value.h>
#include <asn1/crypto_random.h>
#include <asn1/x509_cert.h>
#include <mib_compiler.h>
#include <tinytest.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<asn1_tlv>::value, "TLV ABI");
static_assert(std::is_standard_layout<asn1_reader>::value, "reader ABI");
static_assert(SALTS_ASN1_ABI_VERSION == 1u, "ABI version");

spec("Standalone Salts ASN.1 C++17 header") {
    it("links the migrated C interfaces from C++17") {
        std::uint8_t random[16]{};
        check_equal(crypto_random_bytes(random, sizeof(random)), 0);
        x509_cert_t *certificate = nullptr;
        check_equal(x509_cert_parse(nullptr, 0u, &certificate), -1);
        check_null(certificate);
        check_null(x509_cert_get_subject_cn(certificate));
        x509_cert_free(certificate);
        mib_compile_context_t *context = mib_compile_create_context();
        check_not_null(context);
        mib_compile_destroy_context(context);
    }
    it("validates strict DER through the exported C ABI") {
        const std::uint8_t der[]{0x02u,0x01u,0x2au};
        check_equal(asn1_validate(der,sizeof(der),SALTS_ASN1_DER),SALTS_ASN1_OK);
    }
    it("allocates and transfers a single owned value") {
        asn1_value *root=nullptr, *v=nullptr;
        check_equal(asn1_value_create_sequence(&root),SALTS_ASN1_OK);
        check_equal(asn1_value_create_integer(42,&v),SALTS_ASN1_OK);
        check_equal(asn1_value_add_child(root,v),SALTS_ASN1_OK);
        check_equal(asn1_value_child_count(root),static_cast<std::size_t>(1u));
        asn1_value_free(root);
    }
}
