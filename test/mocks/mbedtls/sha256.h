#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

struct mbedtls_sha256_context {
  std::vector<uint8_t> data;
};

inline void mbedtls_sha256_init(mbedtls_sha256_context* ctx) {
  ctx->data.clear();
}

inline void mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int is224) {
  (void)is224;
  ctx->data.clear();
}

inline void mbedtls_sha256_update(mbedtls_sha256_context* ctx, const uint8_t* input, size_t ilen) {
  ctx->data.insert(ctx->data.end(), input, input + ilen);
}

inline void mbedtls_sha256_finish(mbedtls_sha256_context* ctx, uint8_t output[32]) {
  // Simple non-cryptographic hash for testing — just XOR-fold the data into 32 bytes
  memset(output, 0, 32);
  for (size_t i = 0; i < ctx->data.size(); i++) {
    output[i % 32] ^= ctx->data[i];
  }
}

inline void mbedtls_sha256_free(mbedtls_sha256_context* ctx) {
  ctx->data.clear();
}
