#pragma once

#include <array>
#include <cstdint>

/**
 * AES-128 ECB single-block primitives, thin wrapper over platform crypto.
 *
 * Implementation routes to OpenSSL EVP on desktop+Android (already linked)
 * and Apple CommonCrypto on iOS (in libSystem). No new build dependency.
 *
 * Surface is the bare minimum needed by the DiFluid R1 driver: one-shot
 * encrypt and decrypt of a single 16-byte block under a 16-byte key. No
 * padding, no mode of operation, no IV — callers handle framing.
 *
 * Failure mode: if the underlying platform call fails (genuinely unreachable
 * for a fixed 16-byte ECB block, but guarded for defence in depth), `out` is
 * zeroed rather than left uninitialized. A failed decrypt then parses as a
 * trivially-zero reading instead of laundering as a plausible measurement.
 */
namespace decenza::aes128 {

using Key = std::array<uint8_t, 16>;

void encryptBlock(const Key& key, const uint8_t in[16], uint8_t out[16]);
void decryptBlock(const Key& key, const uint8_t in[16], uint8_t out[16]);

} // namespace decenza::aes128
