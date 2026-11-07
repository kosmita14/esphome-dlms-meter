#pragma once
#include <openssl/evp.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define MBEDTLS_CIPHER_ID_AES 1
struct mbedtls_gcm_context { uint8_t key[16]; };
inline void mbedtls_gcm_init(mbedtls_gcm_context *ctx) { std::memset(ctx, 0, sizeof(*ctx)); }
inline void mbedtls_gcm_free(mbedtls_gcm_context *) {}
inline int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx, int cipher, const uint8_t *key, unsigned bits) {
  if (cipher != MBEDTLS_CIPHER_ID_AES || bits != 128) return -1;
  std::memcpy(ctx->key, key, 16);
  return 0;
}
inline int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx, size_t length, const uint8_t *iv, size_t iv_len,
                                    const uint8_t *aad, size_t aad_len, const uint8_t *tag, size_t tag_len,
                                    const uint8_t *input, uint8_t *output) {
  EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
  int size = 0;
  bool ok = cipher && EVP_DecryptInit_ex(cipher, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) &&
            EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv_len), nullptr) &&
            EVP_DecryptInit_ex(cipher, nullptr, nullptr, ctx->key, iv) &&
            EVP_DecryptUpdate(cipher, nullptr, &size, aad, static_cast<int>(aad_len)) &&
            EVP_DecryptUpdate(cipher, output, &size, input, static_cast<int>(length)) &&
            EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag_len), const_cast<uint8_t *>(tag)) &&
            EVP_DecryptFinal_ex(cipher, output + size, &size);
  EVP_CIPHER_CTX_free(cipher);
  return ok ? 0 : -1;
}
