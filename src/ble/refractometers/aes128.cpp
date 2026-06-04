// AES-128 ECB single-block primitives.
//
// Uses platform-vetted crypto rather than vendoring an implementation:
//   - Desktop (Windows/macOS/Linux) and Android: OpenSSL EVP (already linked).
//   - iOS: Apple Security.framework's CommonCrypto (OpenSSL isn't linked there).
//
// Validated against the issue #1307 R1 test vector:
//   key  = B8 D6 1A B4 DA 18 AA AA AA AA AA AA AA AA AA AA
//   ct   = 5A 06 A0 05 22 D2 33 4D 0F 0B 28 F6 78 D9 DE CA
//   pt   = 00 00 09 B7 FF FF FF B9 00 02 08 46 FF FF FF B9

#include "aes128.h"

#include <QtGlobal>

#if defined(Q_OS_IOS)
#include <CommonCrypto/CommonCryptor.h>
#else
#include <openssl/evp.h>
#endif

namespace decenza::aes128 {

namespace {

#if defined(Q_OS_IOS)

bool oneBlock(const Key& key, const uint8_t in[16], uint8_t out[16], bool encrypt) {
    size_t outLen = 0;
    const CCOperation op = encrypt ? kCCEncrypt : kCCDecrypt;
    const CCCryptorStatus rc = CCCrypt(op,
                                       kCCAlgorithmAES128,
                                       kCCOptionECBMode,  // no padding
                                       key.data(), kCCKeySizeAES128,
                                       nullptr,
                                       in, 16,
                                       out, 16,
                                       &outLen);
    return rc == kCCSuccess && outLen == 16;
}

#else

bool oneBlock(const Key& key, const uint8_t in[16], uint8_t out[16], bool encrypt) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool ok = false;
    do {
        if (EVP_CipherInit_ex(ctx, EVP_aes_128_ecb(), nullptr,
                              key.data(), nullptr, encrypt ? 1 : 0) != 1) break;
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        int outLen = 0;
        if (EVP_CipherUpdate(ctx, out, &outLen, in, 16) != 1) break;
        int finLen = 0;
        if (EVP_CipherFinal_ex(ctx, out + outLen, &finLen) != 1) break;
        ok = (outLen + finLen) == 16;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

#endif

} // namespace

void encryptBlock(const Key& key, const uint8_t in[16], uint8_t out[16]) {
    oneBlock(key, in, out, /*encrypt=*/true);
}

void decryptBlock(const Key& key, const uint8_t in[16], uint8_t out[16]) {
    oneBlock(key, in, out, /*encrypt=*/false);
}

} // namespace decenza::aes128
