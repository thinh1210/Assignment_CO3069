#include "CryptoESP.h"

// Wrapper RNG static để tương thích với uECC
int CryptoESP::rng_wrapper(uint8_t *dest, unsigned int size)
{
    esp_fill_random(dest, size);
    return 1;
}

CryptoESP::CryptoESP()
{
    // Constructor
}

bool CryptoESP::begin()
{
    // Đăng ký hàm RNG
    uECC_set_rng(&CryptoESP::rng_wrapper);
    generateNewKeys();
    return true;
}

void CryptoESP::generateNewKeys()
{
    const struct uECC_Curve_t *curve = uECC_secp256r1();
    if (!uECC_make_key(_publicKey, _privateKey, curve))
    {
        Serial.println("[Crypto] Key generation failed!");
    }
    else
    {
        Serial.println("[Crypto] New ECDH Key Pair generated.");
        _hasSharedSecret = false; // Reset lại trạng thái khi có key mới
    }
}

String CryptoESP::getPublicKeyHex()
{
    char hexStr[129] = {0};
    for (int i = 0; i < 64; i++)
    {
        sprintf(hexStr + i * 2, "%02X", _publicKey[i]);
    }
    return String(hexStr);
}

const uint8_t *CryptoESP::getPublicKeyRaw()
{
    return _publicKey;
}

bool CryptoESP::setPeerPublicKeyHex(const char *hexString)
{
    if (strlen(hexString) != 128)
        return false;

    for (int i = 0; i < 64; i++)
    {
        char byteHex[3] = {hexString[i * 2], hexString[i * 2 + 1], 0};
        _peerPublicKey[i] = (uint8_t)strtol(byteHex, NULL, 16);
    }
    _hasPeerKey = true;
    return computeSessionKey(); // Tự động tính toán Session Key ngay khi có Peer Key
}

bool CryptoESP::setPeerPublicKeyRaw(const uint8_t *rawData)
{
    memcpy(_peerPublicKey, rawData, 64);
    _hasPeerKey = true;
    return computeSessionKey();
}

bool CryptoESP::computeSessionKey()
{
    if (!_hasPeerKey)
        return false;

    const struct uECC_Curve_t *curve = uECC_secp256r1();
    uint8_t sharedSecret[32];

    if (!uECC_shared_secret(_peerPublicKey, _privateKey, sharedSecret, curve))
    {
        Serial.println("[Crypto] Shared secret calculation failed!");
        return false;
    }

    // KDF: SHA-256 hashing
    mbedtls_md_context_t sha_ctx;
    mbedtls_md_init(&sha_ctx);
    mbedtls_md_setup(&sha_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&sha_ctx);
    mbedtls_md_update(&sha_ctx, sharedSecret, 32);
    mbedtls_md_finish(&sha_ctx, _aesKey);
    mbedtls_md_free(&sha_ctx);

    _hasSharedSecret = true;
    Serial.println("[Crypto] AES Session Key ready.");
    return true;
}

bool CryptoESP::isReadyToSend()
{
    return _hasSharedSecret;
}

String CryptoESP::createEncryptedPacket(const char *plaintext, const char *deviceName)
{
    if (!_hasSharedSecret)
        return "{}";

    // 1. Tạo IV ngẫu nhiên (12 bytes)
    uint8_t iv[12];
    esp_fill_random(iv, 12);

    // 2. Chuẩn bị AES-GCM
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _aesKey, 256) != 0)
    {
        mbedtls_gcm_free(&gcm);
        return "{}";
    }

    size_t len = strlen(plaintext);
    // Cấp phát động để tiết kiệm RAM nếu chuỗi lớn, hoặc dùng buffer tĩnh nếu muốn nhanh
    uint8_t *ciphertext = (uint8_t *)malloc(len);
    uint8_t tag[16];

    int ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_ENCRYPT, len, iv, 12, NULL, 0,
                                        (const uint8_t *)plaintext, ciphertext, 16, tag);
    mbedtls_gcm_free(&gcm);

    if (ret != 0)
    {
        free(ciphertext);
        return "{}";
    }

    // 3. Base64 Encode
    String cipherB64 = base64Encode(ciphertext, len);
    String ivB64 = base64Encode(iv, 12);
    String tagB64 = base64Encode(tag, 16);

    free(ciphertext); // Giải phóng bộ nhớ

    // 4. Đóng gói JSON
    DynamicJsonDocument doc(1024);
    doc["from"] = deviceName;
    doc["ciphertext"] = cipherB64;
    doc["iv"] = ivB64;
    doc["tag"] = tagB64;

    String output;
    serializeJson(doc, output);
    return output;
}

String CryptoESP::base64Encode(const uint8_t *data, size_t length)
{
    // (Giữ nguyên hàm base64 của bạn ở đây, hoặc dùng thư viện <base64.h>)
    static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    out.reserve(((length + 2) / 3) * 4);
    for (size_t i = 0; i < length; i += 3)
    {
        uint32_t v = data[i] << 16;
        if (i + 1 < length)
            v |= data[i + 1] << 8;
        if (i + 2 < length)
            v |= data[i + 2];
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += (i + 1 < length) ? table[(v >> 6) & 63] : '=';
        out += (i + 2 < length) ? table[v & 63] : '=';
    }
    return out;
}