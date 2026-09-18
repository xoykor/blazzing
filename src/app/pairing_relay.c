/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/pairing_relay.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RELAY_KEY_SIZE 32u
#define RELAY_SESSION_RAW_SIZE 16u
#define RELAY_SESSION_ID_SIZE 33u
#define RELAY_KEY_FRAGMENT_SIZE 44u
#define RELAY_BASE_URL_MAX 512u
#define RELAY_PAGE_URL_MAX 1024u
#define RELAY_RESPONSE_MAX 16384u
#define RELAY_PAYLOAD_MAX 8192u
#define RELAY_SESSION_TTL_SECONDS 300
#define RELAY_POLL_MS 2000L

struct vip_pairing_relay {
    char base_url[RELAY_BASE_URL_MAX];
    char session_id[RELAY_SESSION_ID_SIZE];
    char key_fragment[RELAY_KEY_FRAGMENT_SIZE];
    char page_url[RELAY_PAGE_URL_MAX];
    uint8_t key[RELAY_KEY_SIZE];

    pthread_t thread;
    bool thread_started;
    atomic_bool stop;
    atomic_bool finished;

    vip_pairing_submit_fn on_submit;
    void *userdata;
};

typedef struct {
    char data[RELAY_RESPONSE_MAX];
    size_t len;
} relay_response_t;

static bool starts_with(const char *text, const char *prefix) {
    return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool relay_base_url_valid(const char *url) {
    if (!url || !starts_with(url, "https://"))
        return false;
    size_t len = strlen(url);
    return len > strlen("https://") && len < RELAY_BASE_URL_MAX &&
           strchr(url, '#') == NULL && strchr(url, '?') == NULL;
}

static bool copy_base_url(char out[RELAY_BASE_URL_MAX], const char *url) {
    if (!relay_base_url_valid(url))
        return false;
    int n = snprintf(out, RELAY_BASE_URL_MAX, "%s", url);
    if (n <= 0 || (size_t)n >= RELAY_BASE_URL_MAX)
        return false;
    size_t len = strlen(out);
    while (len > strlen("https://") && out[len - 1u] == '/')
        out[--len] = '\0';
    return true;
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_size) {
    static const char hex[] = "0123456789abcdef";
    if (!data || !out || out_size < len * 2u + 1u)
        return;
    for (size_t i = 0u; i < len; ++i) {
        out[i * 2u] = hex[(data[i] >> 4u) & 0x0fu];
        out[i * 2u + 1u] = hex[data[i] & 0x0fu];
    }
    out[len * 2u] = '\0';
}

static bool base64url_encode(const uint8_t *data,
                             size_t len,
                             char *out,
                             size_t out_size) {
    if (!data || !out || len > (size_t)INT_MAX)
        return false;

    size_t encoded_size = 4u * ((len + 2u) / 3u) + 1u;
    unsigned char *encoded = malloc(encoded_size);
    if (!encoded)
        return false;

    int n = EVP_EncodeBlock(encoded, data, (int)len);
    if (n <= 0) {
        free(encoded);
        return false;
    }

    size_t written = 0u;
    for (int i = 0; i < n && encoded[i] != '='; ++i) {
        if (written + 1u >= out_size) {
            free(encoded);
            return false;
        }
        char ch = (char)encoded[i];
        if (ch == '+')
            ch = '-';
        else if (ch == '/')
            ch = '_';
        out[written++] = ch;
    }
    out[written] = '\0';
    free(encoded);
    return true;
}

static bool base64url_decode(const char *text,
                             uint8_t *out,
                             size_t out_size,
                             size_t *out_len) {
    if (!text || !out || !out_len)
        return false;

    size_t len = strlen(text);
    if (len == 0u || len > RELAY_RESPONSE_MAX)
        return false;

    size_t padded_len = ((len + 3u) / 4u) * 4u;
    if (padded_len > (size_t)INT_MAX)
        return false;

    char *padded = malloc(padded_len + 1u);
    if (!padded)
        return false;

    for (size_t i = 0u; i < len; ++i) {
        char ch = text[i];
        if (ch == '-')
            ch = '+';
        else if (ch == '_')
            ch = '/';
        else if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                   (ch >= '0' && ch <= '9'))) {
            free(padded);
            return false;
        }
        padded[i] = ch;
    }
    for (size_t i = len; i < padded_len; ++i)
        padded[i] = '=';
    padded[padded_len] = '\0';

    size_t decoded_capacity = 3u * (padded_len / 4u);
    unsigned char *decoded = malloc(decoded_capacity + 1u);
    if (!decoded) {
        free(padded);
        return false;
    }

    int n = EVP_DecodeBlock(decoded, (const unsigned char *)padded, (int)padded_len);
    if (n < 0) {
        OPENSSL_cleanse(decoded, decoded_capacity + 1u);
        free(decoded);
        free(padded);
        return false;
    }

    size_t padding = padded_len - len;
    size_t actual = (size_t)n;
    if (padding > actual)
        actual = 0u;
    else
        actual -= padding;

    bool ok = actual <= out_size;
    if (ok) {
        memcpy(out, decoded, actual);
        *out_len = actual;
    }

    OPENSSL_cleanse(decoded, decoded_capacity + 1u);
    free(decoded);
    free(padded);
    return ok;
}

static size_t response_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    relay_response_t *response = userdata;
    if (!response || !ptr || (size != 0u && nmemb > SIZE_MAX / size))
        return 0u;

    size_t bytes = size * nmemb;
    if (bytes > RELAY_RESPONSE_MAX - response->len - 1u)
        return 0u;

    memcpy(response->data + response->len, ptr, bytes);
    response->len += bytes;
    response->data[response->len] = '\0';
    return bytes;
}

static int transfer_progress(void *userdata,
                             curl_off_t dltotal,
                             curl_off_t dlnow,
                             curl_off_t ultotal,
                             curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    atomic_bool *stop = userdata;
    return stop && atomic_load(stop) ? 1 : 0;
}

static CURLcode relay_http(const char *method,
                           const char *url,
                           relay_response_t *response,
                           long *status,
                           atomic_bool *stop) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return CURLE_FAILED_INIT;

    relay_response_t local = {0};
    if (!response)
        response = &local;
    response->len = 0u;
    response->data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Blazzing-Pairing/1");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

    if (stop) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, stop);
    }

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode code = curl_easy_perform(curl);
    if (status) {
        *status = 0L;
        if (code == CURLE_OK)
            (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    }
    curl_easy_cleanup(curl);
    return code;
}

static bool build_api_url(const vip_pairing_relay_t *relay,
                          const char *suffix,
                          char *out,
                          size_t out_size) {
    if (!relay || !suffix || !out || out_size == 0u)
        return false;
    int n = snprintf(out, out_size, "%s/api/v1/sessions/%s%s",
                     relay->base_url, relay->session_id, suffix);
    return n > 0 && (size_t)n < out_size;
}

static void delete_remote_session(vip_pairing_relay_t *relay) {
    char url[1024];
    if (!build_api_url(relay, "", url, sizeof(url)))
        return;
    long status = 0L;
    (void)relay_http("DELETE", url, NULL, &status, NULL);
}

static vip_status_t create_remote_session(vip_pairing_relay_t *relay,
                                          vip_error_t *error) {
    uint8_t raw_id[RELAY_SESSION_RAW_SIZE];

    if (RAND_bytes(relay->key, (int)sizeof(relay->key)) != 1) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível gerar a chave de pareamento");
        return VIP_ERR_IO;
    }
    if (!base64url_encode(relay->key, sizeof(relay->key),
                          relay->key_fragment, sizeof(relay->key_fragment))) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível codificar a chave de pareamento");
        return VIP_ERR_IO;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (RAND_bytes(raw_id, (int)sizeof(raw_id)) != 1) {
            vip_error_set(error, VIP_ERR_IO, "não foi possível gerar a sessão de pareamento");
            return VIP_ERR_IO;
        }
        bytes_to_hex(raw_id, sizeof(raw_id), relay->session_id, sizeof(relay->session_id));

        char url[1024];
        if (!build_api_url(relay, "", url, sizeof(url))) {
            vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "URL do relay é longa demais");
            return VIP_ERR_INVALID_ARGUMENT;
        }

        long status = 0L;
        CURLcode code = relay_http("POST", url, NULL, &status, NULL);
        if (code != CURLE_OK) {
            vip_error_set(error, VIP_ERR_NETWORK, "não foi possível conectar ao relay HTTPS");
            return VIP_ERR_NETWORK;
        }
        if (status == 201L) {
            int n = snprintf(relay->page_url, sizeof(relay->page_url),
                             "%s/pair/%s#%s",
                             relay->base_url, relay->session_id, relay->key_fragment);
            if (n <= 0 || (size_t)n >= sizeof(relay->page_url)) {
                vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "URL de pareamento é longa demais");
                return VIP_ERR_INVALID_ARGUMENT;
            }
            return VIP_OK;
        }
        if (status != 409L) {
            vip_error_set(error, VIP_ERR_NETWORK, "relay recusou a criação da sessão");
            return VIP_ERR_NETWORK;
        }
    }

    vip_error_set(error, VIP_ERR_IO, "não foi possível obter uma sessão de pareamento única");
    return VIP_ERR_IO;
}

vip_status_t vip_pairing_relay_decrypt_payload(const uint8_t key[32],
                                               const char *payload_json,
                                               char *profile_name,
                                               size_t profile_name_size,
                                               char *playlist_url,
                                               size_t playlist_url_size,
                                               vip_error_t *error) {
    if (!key || !payload_json || !profile_name || profile_name_size == 0u ||
        !playlist_url || playlist_url_size == 0u) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "argumentos inválidos no payload do relay");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    struct json_object *root = json_tokener_parse(payload_json);
    if (!root || !json_object_is_type(root, json_type_object)) {
        if (root)
            json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "payload cifrado inválido");
        return VIP_ERR_MALFORMED;
    }

    struct json_object *iv_obj = NULL;
    struct json_object *cipher_obj = NULL;
    if (!json_object_object_get_ex(root, "iv", &iv_obj) ||
        !json_object_object_get_ex(root, "ciphertext", &cipher_obj) ||
        !json_object_is_type(iv_obj, json_type_string) ||
        !json_object_is_type(cipher_obj, json_type_string)) {
        json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "payload cifrado incompleto");
        return VIP_ERR_MALFORMED;
    }

    uint8_t iv[32];
    size_t iv_len = 0u;
    uint8_t ciphertext[RELAY_PAYLOAD_MAX];
    size_t ciphertext_len = 0u;
    bool decoded =
        base64url_decode(json_object_get_string(iv_obj), iv, sizeof(iv), &iv_len) &&
        base64url_decode(json_object_get_string(cipher_obj),
                         ciphertext, sizeof(ciphertext), &ciphertext_len);
    json_object_put(root);

    if (!decoded || iv_len != 12u || ciphertext_len <= 16u) {
        OPENSSL_cleanse(ciphertext, sizeof(ciphertext));
        vip_error_set(error, VIP_ERR_MALFORMED, "payload cifrado malformado");
        return VIP_ERR_MALFORMED;
    }

    size_t encrypted_len = ciphertext_len - 16u;
    if (encrypted_len > (size_t)INT_MAX) {
        OPENSSL_cleanse(ciphertext, sizeof(ciphertext));
        vip_error_set(error, VIP_ERR_MALFORMED, "payload cifrado grande demais");
        return VIP_ERR_MALFORMED;
    }

    uint8_t *tag = ciphertext + encrypted_len;
    uint8_t clear[RELAY_PAYLOAD_MAX];
    int out_len = 0;
    int final_len = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool decrypt_ok =
        ctx &&
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv_len, NULL) == 1 &&
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) == 1 &&
        EVP_DecryptUpdate(ctx, clear, &out_len, ciphertext, (int)encrypted_len) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1 &&
        EVP_DecryptFinal_ex(ctx, clear + out_len, &final_len) == 1;

    if (ctx)
        EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(ciphertext, sizeof(ciphertext));

    if (!decrypt_ok) {
        OPENSSL_cleanse(clear, sizeof(clear));
        vip_error_set(error, VIP_ERR_MALFORMED, "falha ao autenticar payload do pareamento");
        return VIP_ERR_MALFORMED;
    }

    size_t clear_len = (size_t)(out_len + final_len);
    if (clear_len >= sizeof(clear)) {
        OPENSSL_cleanse(clear, sizeof(clear));
        vip_error_set(error, VIP_ERR_MALFORMED, "payload descriptografado grande demais");
        return VIP_ERR_MALFORMED;
    }
    clear[clear_len] = '\0';

    root = json_tokener_parse((const char *)clear);
    if (!root || !json_object_is_type(root, json_type_object)) {
        if (root)
            json_object_put(root);
        OPENSSL_cleanse(clear, sizeof(clear));
        vip_error_set(error, VIP_ERR_MALFORMED, "conteúdo do pareamento inválido");
        return VIP_ERR_MALFORMED;
    }

    struct json_object *name_obj = NULL;
    struct json_object *url_obj = NULL;
    const char *name = "";
    if (json_object_object_get_ex(root, "name", &name_obj) &&
        json_object_is_type(name_obj, json_type_string))
        name = json_object_get_string(name_obj);

    if (!json_object_object_get_ex(root, "url", &url_obj) ||
        !json_object_is_type(url_obj, json_type_string)) {
        json_object_put(root);
        OPENSSL_cleanse(clear, sizeof(clear));
        vip_error_set(error, VIP_ERR_MALFORMED, "URL ausente no pareamento");
        return VIP_ERR_MALFORMED;
    }

    const char *url = json_object_get_string(url_obj);
    if (!(starts_with(url, "http://") || starts_with(url, "https://")) ||
        strlen(name) >= profile_name_size || strlen(url) >= playlist_url_size) {
        json_object_put(root);
        OPENSSL_cleanse(clear, sizeof(clear));
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "dados recebidos do celular são inválidos");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    snprintf(profile_name, profile_name_size, "%s", name);
    snprintf(playlist_url, playlist_url_size, "%s", url);
    json_object_put(root);
    OPENSSL_cleanse(clear, sizeof(clear));
    return VIP_OK;
}

static void sleep_poll_interval(atomic_bool *stop) {
    const long slice_ms = 50L;
    for (long waited = 0L; waited < RELAY_POLL_MS && !atomic_load(stop); waited += slice_ms) {
        struct timespec ts = {.tv_sec = 0, .tv_nsec = slice_ms * 1000000L};
        (void)nanosleep(&ts, NULL);
    }
}

static void *relay_worker(void *userdata) {
    vip_pairing_relay_t *relay = userdata;
    time_t deadline = time(NULL) + RELAY_SESSION_TTL_SECONDS;

    char payload_url[1024];
    if (!build_api_url(relay, "/payload", payload_url, sizeof(payload_url)))
        return NULL;

    while (!atomic_load(&relay->stop) && time(NULL) < deadline) {
        relay_response_t response = {0};
        long status = 0L;
        CURLcode code = relay_http("GET", payload_url, &response, &status, &relay->stop);

        if (atomic_load(&relay->stop))
            break;

        if (code == CURLE_OK && status == 200L) {
            char name[128];
            char url[512];
            vip_error_t error = {0};
            if (vip_pairing_relay_decrypt_payload(relay->key, response.data,
                                                  name, sizeof(name),
                                                  url, sizeof(url), &error) == VIP_OK) {
                if (relay->on_submit)
                    relay->on_submit(name, url, relay->userdata);
                delete_remote_session(relay);
                atomic_store(&relay->finished, true);
                return NULL;
            }
        } else if (code == CURLE_OK && status == 410L) {
            break;
        }

        sleep_poll_interval(&relay->stop);
    }

    atomic_store(&relay->finished, true);
    return NULL;
}

vip_status_t vip_pairing_relay_start(vip_pairing_relay_t **out_relay,
                                     const char *base_url,
                                     vip_pairing_submit_fn on_submit,
                                     void *userdata,
                                     vip_error_t *error) {
    if (!out_relay || !on_submit || !relay_base_url_valid(base_url)) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT,
                      "configure um relay HTTPS válido para o pareamento");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    *out_relay = NULL;

    vip_pairing_relay_t *relay = calloc(1, sizeof(*relay));
    if (!relay) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para pareamento");
        return VIP_ERR_NOMEM;
    }

    if (!copy_base_url(relay->base_url, base_url)) {
        free(relay);
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "URL do relay inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    relay->on_submit = on_submit;
    relay->userdata = userdata;
    atomic_init(&relay->stop, false);
    atomic_init(&relay->finished, false);

    vip_status_t created = create_remote_session(relay, error);
    if (created != VIP_OK) {
        OPENSSL_cleanse(relay->key, sizeof(relay->key));
        free(relay);
        return created;
    }

    if (pthread_create(&relay->thread, NULL, relay_worker, relay) != 0) {
        delete_remote_session(relay);
        OPENSSL_cleanse(relay->key, sizeof(relay->key));
        OPENSSL_cleanse(relay->key_fragment, sizeof(relay->key_fragment));
        free(relay);
        vip_error_set(error, VIP_ERR_IO, "não foi possível iniciar polling do pareamento");
        return VIP_ERR_IO;
    }

    relay->thread_started = true;
    *out_relay = relay;
    return VIP_OK;
}

void vip_pairing_relay_stop(vip_pairing_relay_t *relay) {
    if (!relay)
        return;

    atomic_store(&relay->stop, true);
    if (relay->thread_started)
        pthread_join(relay->thread, NULL);

    delete_remote_session(relay);
    OPENSSL_cleanse(relay->key, sizeof(relay->key));
    OPENSSL_cleanse(relay->key_fragment, sizeof(relay->key_fragment));
    OPENSSL_cleanse(relay->page_url, sizeof(relay->page_url));
    free(relay);
}

const char *vip_pairing_relay_page_url(const vip_pairing_relay_t *relay) {
    return relay ? relay->page_url : "";
}

const char *vip_pairing_relay_session_id(const vip_pairing_relay_t *relay) {
    return relay ? relay->session_id : "";
}

bool vip_pairing_relay_finished(const vip_pairing_relay_t *relay) {
    return relay ? atomic_load(&relay->finished) : true;
}
