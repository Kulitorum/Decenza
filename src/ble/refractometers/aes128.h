#pragma once

#include <array>
#include <cstdint>

/**
 * Minimal AES-128 ECB single-block primitives.
 *
 * Vendored to avoid pulling OpenSSL into the build matrix just so the
 * DiFluid R1 driver can decrypt 16-byte measurement frames. This module
 * deliberately implements only what that driver needs: one-shot encrypt
 * and decrypt of a single 16-byte block under a 16-byte key.
 *
 * No padding, no mode of operation, no IV — callers handle framing.
 */
namespace decenza::aes128 {

using Block = std::array<uint8_t, 16>;
using Key   = std::array<uint8_t, 16>;

void encryptBlock(const Key& key, const uint8_t in[16], uint8_t out[16]);
void decryptBlock(const Key& key, const uint8_t in[16], uint8_t out[16]);

} // namespace decenza::aes128
