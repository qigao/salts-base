#include "asn1/x509_cert.h"
#include "test_cert_data.h"
#include "tinytest.h"
#include <string.h>

spec("x509_integration") {
  static x509_cert_t *certificate;

  before_each() { certificate = NULL; }
  after_each() { x509_cert_free(certificate); }

  it("parses a real DER certificate through Salts ASN.1") {
    check_equal(x509_cert_parse(test_cert_der, test_cert_der_len, &certificate),
                0);
    check_not_null(certificate);
    if (certificate) {
      check_equal(certificate->tbs.version, 2);
      check_equal(x509_cert_get_subject_cn(certificate), "test.example.com");
      check_equal(x509_cert_get_issuer_cn(certificate), "test.example.com");
      check_true(x509_cert_is_self_signed(certificate));
      check_equal(x509_cert_get_pubkey_type(certificate), X509_PUBKEY_RSA);
      check(certificate->tbs.public_key.key.rsa.modulus_len > 0u);
      check(certificate->signature_len > 0u);
      check_equal(certificate->signature_unused_bits, 0);
    }
  }

  it("rejects truncated and trailing DER") {
    uint8_t with_trailing[sizeof(test_cert_der) + 1u];
    memcpy(with_trailing, test_cert_der, test_cert_der_len);
    with_trailing[test_cert_der_len] = 0u;
    check(x509_cert_parse(test_cert_der, test_cert_der_len - 1u,
                          &certificate) != 0);
    check_null(certificate);
    check(x509_cert_parse(with_trailing, test_cert_der_len + 1u,
                          &certificate) != 0);
    check_null(certificate);
  }

  it("reads validity as UTC independently of the local timezone") {
    check_equal(x509_cert_parse(test_cert_der, test_cert_der_len, &certificate), 0);
    check_not_null(certificate);
    check_equal(certificate->tbs.validity.not_before, (time_t)1767320835);
    check_equal(certificate->tbs.validity.not_after, (time_t)1798856835);
    check_true(x509_cert_is_valid_at(certificate, (time_t)1767320835));
    check_true(x509_cert_is_valid_at(certificate, (time_t)1798856835));
    check_false(x509_cert_is_valid_at(certificate, (time_t)1767320834));
    check_false(x509_cert_is_valid_at(certificate, (time_t)1798856836));
  }

  it("clears the output for invalid input") {
    x509_cert_t sentinel = {0};
    x509_cert_t *output = &sentinel;
    check_equal(x509_cert_parse(NULL, 1u, &output), -1);
    check_null(output);
    output = &sentinel;
    check_equal(x509_cert_parse(test_cert_der, 0u, &output), -1);
    check_null(output);
    check_equal(x509_cert_parse(test_cert_der, test_cert_der_len, NULL), -1);
  }
}
