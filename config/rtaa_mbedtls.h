/* SPDX-License-Identifier: GPL-3.0-only
 * Portable TLS 1.2 client profile. No OS, filesystem, sockets or built-in RNG.
 * Randomness and certificate/key bytes are supplied by the application.
 * Platform allocation is mbedTLS's standard calloc/free unless overridden by
 * the product when linking; enforce a product-level allocator budget. */
#ifndef RTAA_MBEDTLS_CONFIG_H
#define RTAA_MBEDTLS_CONFIG_H
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_GCM_C
#define MBEDTLS_HMAC_DRBG_C
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_MAX_CONTENT_LEN 16384
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
#include <mbedtls/check_config.h>
#endif
