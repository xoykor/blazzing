/* SPDX-License-Identifier: MIT */
#include "visual_iptv/pairing_relay.h"
#include "test_common.h"

#include <openssl/evp.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool base64url_encode(const uint8_t *data, size_t len, char *out, size_t out_size) {
    unsigned char encoded[4096];
    if (len > 3000u)
        return false;

    int n = EVP_EncodeBlock(encoded, data, (int)len);
    if (n <= 0)
        return false;

    size_t w = 0u;
    for (int i = 0; i < n && encoded[i] != '='; ++i) {
        if (w + 1u >= out_size)
            return false;
        char ch = (char)encoded[i];
        if (ch == '+')
            ch = '-';
        else if (ch == '/')
            ch = '_';
        out[w++] = ch;
    }
    out[w] = '\0';
    return true;
}

static bool make_payload(const uint8_t key[32],
                         const char *clear_json,
                         char *out,
                         size_t out_size) {
    uint8_t iv[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    uint8_t cipher[2048];
    uint8_t tag[16];
    int out_len = 0;
    int final_len = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    bool ok =
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)sizeof(iv), NULL) == 1 &&
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) == 1 &&
        EVP_EncryptUpdate(ctx, cipher, &out_len,
                          (const unsigned char *)clear_json, (int)strlen(clear_json)) == 1 &&
        EVP_EncryptFinal_ex(ctx, cipher + out_len, &final_len) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)sizeof(tag), tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return false;

    size_t cipher_len = (size_t)(out_len + final_len);
    memcpy(cipher + cipher_len, tag, sizeof(tag));
    cipher_len += sizeof(tag);

    char iv_b64[64];
    char cipher_b64[4096];
    if (!base64url_encode(iv, sizeof(iv), iv_b64, sizeof(iv_b64)) ||
        !base64url_encode(cipher, cipher_len, cipher_b64, sizeof(cipher_b64)))
        return false;

    int n = snprintf(out, out_size,
                     "{\"iv\":\"%s\",\"ciphertext\":\"%s\"}",
                     iv_b64, cipher_b64);
    return n > 0 && (size_t)n < out_size;
}

int main(void) {
    uint8_t key[32];
    for (size_t i = 0u; i < sizeof(key); ++i)
        key[i] = (uint8_t)i;

    char encrypted[4096];
    TEST_CHECK(make_payload(
        key,
        "{\"name\":\"Minha Lista\",\"url\":\"https://example.com/lista.m3u8\"}",
        encrypted, sizeof(encrypted)));

    char name[128];
    char url[512];
    vip_error_t error = {0};

    TEST_STATUS(vip_pairing_relay_decrypt_payload(
                    key, encrypted,
                    name, sizeof(name),
                    url, sizeof(url),
                    &error),
                VIP_OK, &error);
    TEST_CHECK(strcmp(name, "Minha Lista") == 0);
    TEST_CHECK(strcmp(url, "https://example.com/lista.m3u8") == 0);

    size_t len = strlen(encrypted);
    TEST_CHECK(len > 3u);
    encrypted[len - 2u] = encrypted[len - 2u] == 'A' ? 'B' : 'A';

    TEST_CHECK(vip_pairing_relay_decrypt_payload(
                   key, encrypted,
                   name, sizeof(name),
                   url, sizeof(url),
                   &error) != VIP_OK);

    TEST_CHECK(make_payload(
        key,
        "{\"name\":\"X\",\"url\":\"ftp://example.com/lista.m3u8\"}",
        encrypted, sizeof(encrypted)));
    TEST_CHECK(vip_pairing_relay_decrypt_payload(
                   key, encrypted,
                   name, sizeof(name),
                   url, sizeof(url),
                   &error) == VIP_ERR_INVALID_ARGUMENT);

    return 0;
}
