#ifndef CRYPTO_ESP_H
#define CRYPTO_ESP_H

#include <Arduino.h>
#include <uECC.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <ArduinoJson.h>

class CryptoESP
{
private:
    // Các biến lưu trữ Key
    uint8_t _privateKey[32];
    uint8_t _publicKey[64];
    uint8_t _peerPublicKey[64]; // Laptop Public Key
    uint8_t _aesKey[32];        // Key sau khi đã qua KDF (SHA-256)

    bool _hasPeerKey = false;
    bool _hasSharedSecret = false;

    // Hàm hỗ trợ RNG cho uECC
    static int rng_wrapper(uint8_t *dest, unsigned int size);

    // Hàm hỗ trợ Base64 nội bộ
    String base64Encode(const uint8_t *data, size_t length);

public:
    CryptoESP();

    // 1. Khởi tạo và tạo Key pair mới
    bool begin();
    void generateNewKeys();

    // 2. Các hàm Getter để lấy Key (phục vụ trao đổi HTTP)
    String getPublicKeyHex();
    const uint8_t *getPublicKeyRaw();

    // 3. Cập nhật Key của Laptop (Peer)
    bool setPeerPublicKeyHex(const char *hexString);
    bool setPeerPublicKeyRaw(const uint8_t *rawData);

    // 4. Tính toán Shared Secret & Derive AES Key (KDF)
    bool computeSessionKey();

    // 5. Mã hóa & Đóng gói JSON (Tương đương việc "Sign & Encrypt")
    // Trả về chuỗi JSON đầy đủ (ciphertext, iv, tag) để gửi đi
    String createEncryptedPacket(const char *plaintext, const char *deviceName = "esp32");

    // Kiểm tra trạng thái
    bool isReadyToSend();
};

#endif