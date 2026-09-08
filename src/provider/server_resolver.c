/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/server_resolver.h"
#include "visual_iptv/provider.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define RESOLVER_MAX_RESPONSE (4u * 1024u * 1024u)
#define RESOLVER_MAX_BASES 64u

static const char *const resolver_apis[] = {
    "https://api.s-api.org",
    "https://api2.s-api.org",
    "https://api.asplayer.xyz",
    "https://api2.asplayer.xyz",
    "https://api.spk-web.cc",
    "https://api2.spk-web.cc",
};

static const char resolver_code[] = "11";
static const char resolver_client[] = "firestream-tv";
static const char resolver_public_key[] =
    "QSxII7iGMtxErZEn35GRlstMCVXLn50IAmu1b1WOCDIpRrJRTwjrTmgXjREHFcMA";
static const char resolver_platform[] = "android";
static const char resolver_origin[] = "https://appassets.androidplatform.net";
static const char resolver_referer[] = "https://appassets.androidplatform.net/assets/webapp/index.html";
static const char chrome_ua[] =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/131.0.0.0 Safari/537.36";
static const char android_ua[] =
    "Mozilla/5.0 (Linux; Android 12; Android TV) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Version/4.0 Chrome/131.0.0.0 Safari/537.36";

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool overflow;
} resolver_buf_t;

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} base_list_t;

static size_t resolver_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    resolver_buf_t *buf = userdata;
    if (size != 0u && nmemb > SIZE_MAX / size) return 0u;
    size_t bytes = size * nmemb;
    if (bytes > RESOLVER_MAX_RESPONSE || buf->len > RESOLVER_MAX_RESPONSE - bytes) {
        buf->overflow = true;
        return 0u;
    }
    size_t need = buf->len + bytes + 1u;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 4096u;
        while (cap < need && cap <= RESOLVER_MAX_RESPONSE / 2u) cap *= 2u;
        if (cap < need) cap = need;
        char *grown = realloc(buf->data, cap);
        if (!grown) return 0u;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

static uint32_t crc32_identity(const unsigned char *data, size_t len) {
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static char *base64url_decode(const char *encoded, size_t *len_out, vip_error_t *error) {
    size_t len = strlen(encoded);
    size_t padded = ((len + 3u) / 4u) * 4u;
    char *standard = malloc(padded + 1u);
    if (!standard) return NULL;
    for (size_t i = 0; i < len; ++i) {
        char c = encoded[i];
        standard[i] = c == '-' ? '+' : (c == '_' ? '/' : c);
    }
    for (size_t i = len; i < padded; ++i) standard[i] = '=';
    standard[padded] = '\0';

    size_t alloc = (padded / 4u) * 3u + 1u;
    unsigned char *raw = malloc(alloc);
    if (!raw) { free(standard); return NULL; }
    int decoded = EVP_DecodeBlock(raw, (const unsigned char *)standard, (int)padded);
    if (decoded < 0) {
        free(standard); free(raw);
        vip_error_set(error, VIP_ERR_MALFORMED, "payload do resolvedor não é base64url válido");
        return NULL;
    }
    size_t pad = padded - len;
    size_t actual = (size_t)decoded;
    if (pad <= actual) actual -= pad;
    raw[actual] = '\0';
    free(standard);
    *len_out = actual;
    return (char *)raw;
}

vip_status_t vip_streamfire_decode_payload(const char *payload,
                                           const char *identity,
                                           char **json_out,
                                           vip_error_t *error) {
    if (!payload || !identity || !json_out) return VIP_ERR_INVALID_ARGUMENT;
    *json_out = NULL;
    size_t plen = strlen(payload);
    if (plen < 3u) {
        vip_error_set(error, VIP_ERR_MALFORMED, "payload do resolvedor inválido");
        return VIP_ERR_MALFORMED;
    }
    char *rev = malloc(plen + 1u);
    if (!rev) return VIP_ERR_NOMEM;
    for (size_t i = 0; i < plen; ++i) rev[i] = payload[plen - i - 1u];
    rev[plen] = '\0';
    char *dot = strchr(rev, '.');
    if (!dot || dot == rev) {
        free(rev);
        vip_error_set(error, VIP_ERR_MALFORMED, "formato do payload do resolvedor inválido");
        return VIP_ERR_MALFORMED;
    }
    *dot = '\0';
    const char *odd = rev;
    const char *even = dot + 1;
    size_t odd_len = strlen(odd), even_len = strlen(even);
    size_t total = odd_len + even_len;
    char *b64u = malloc(total + 1u);
    if (!b64u) { free(rev); return VIP_ERR_NOMEM; }
    size_t oi = 0u, ei = 0u, outi = 0u;
    for (size_t pos = 0; pos < total; ++pos) {
        if ((pos & 1u) == 0u) {
            if (ei < even_len) b64u[outi++] = even[ei++];
        } else if (oi < odd_len) {
            b64u[outi++] = odd[oi++];
        }
    }
    b64u[outi] = '\0';
    size_t raw_len = 0u;
    char *raw = base64url_decode(b64u, &raw_len, error);
    free(b64u); free(rev);
    if (!raw) return error && error->code ? error->code : VIP_ERR_MALFORMED;

    uint32_t state = crc32_identity((const unsigned char *)identity, strlen(identity));
    if (state == 0u) state = UINT32_C(2784059165);
    for (size_t i = 0; i < raw_len; ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        raw[i] = (char)((unsigned char)raw[i] ^ (unsigned char)(state & 0xffu));
    }
    raw[raw_len] = '\0';
    json_object *check = json_tokener_parse(raw);
    if (!check) {
        free(raw);
        vip_error_set(error, VIP_ERR_MALFORMED, "resposta decodificada do resolvedor não é JSON");
        return VIP_ERR_MALFORMED;
    }
    json_object_put(check);
    *json_out = raw;
    vip_error_clear(error);
    return VIP_OK;
}

static bool key_is_base(const char *key) {
    static const char *const keys[] = {
        "baseurl", "base_url", "dns", "dns_list", "dnslist", "server", "server_url", "url"
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (strcasecmp(key, keys[i]) == 0) return true;
    return false;
}

static char *normalize_base(const char *input) {
    if (!input) return NULL;
    while (isspace((unsigned char)*input)) ++input;
    size_t len = strlen(input);
    while (len && isspace((unsigned char)input[len - 1u])) --len;
    if (!len) return NULL;
    bool has_scheme = (len >= 7u && strncmp(input, "http://", 7u) == 0) ||
                      (len >= 8u && strncmp(input, "https://", 8u) == 0);
    size_t prefix = has_scheme ? 0u : 7u;
    while (len > 0u && input[len - 1u] == '/') --len;
    char *out = malloc(prefix + len + 1u);
    if (!out) return NULL;
    if (prefix) memcpy(out, "http://", prefix);
    memcpy(out + prefix, input, len);
    out[prefix + len] = '\0';
    return out;
}

static vip_status_t base_list_add(base_list_t *list, const char *value, vip_error_t *error) {
    if (!value || !value[0] || list->len >= RESOLVER_MAX_BASES) return VIP_OK;
    char *normalized = normalize_base(value);
    if (!normalized) return VIP_OK;
    for (size_t i = 0; i < list->len; ++i) {
        if (strcmp(list->items[i], normalized) == 0) { free(normalized); return VIP_OK; }
    }
    if (list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2u : 8u;
        char **grown = realloc(list->items, cap * sizeof(*grown));
        if (!grown) { free(normalized); vip_error_set(error, VIP_ERR_NOMEM, "sem memória para servidores resolvidos"); return VIP_ERR_NOMEM; }
        list->items = grown; list->cap = cap;
    }
    list->items[list->len++] = normalized;
    return VIP_OK;
}

static vip_status_t collect_value(base_list_t *list, json_object *value, vip_error_t *error) {
    if (!value) return VIP_OK;
    enum json_type type = json_object_get_type(value);
    if (type == json_type_string) return base_list_add(list, json_object_get_string(value), error);
    if (type == json_type_array) {
        size_t n = json_object_array_length(value);
        for (size_t i = 0; i < n; ++i) {
            vip_status_t st = collect_value(list, json_object_array_get_idx(value, i), error);
            if (st != VIP_OK) return st;
        }
    }
    return VIP_OK;
}

static vip_status_t walk_json(base_list_t *list, json_object *obj, vip_error_t *error) {
    if (!obj) return VIP_OK;
    if (json_object_get_type(obj) == json_type_object) {
        json_object_object_foreach(obj, key, value) {
            if (key_is_base(key)) {
                vip_status_t st = collect_value(list, value, error);
                if (st != VIP_OK) return st;
            }
            vip_status_t st = walk_json(list, value, error);
            if (st != VIP_OK) return st;
        }
    } else if (json_object_get_type(obj) == json_type_array) {
        size_t n = json_object_array_length(obj);
        for (size_t i = 0; i < n; ++i) {
            vip_status_t st = walk_json(list, json_object_array_get_idx(obj, i), error);
            if (st != VIP_OK) return st;
        }
    }
    return VIP_OK;
}

vip_status_t vip_streamfire_collect_bases(const char *json,
                                          char ***bases_out,
                                          size_t *count_out,
                                          vip_error_t *error) {
    if (!json || !bases_out || !count_out) return VIP_ERR_INVALID_ARGUMENT;
    *bases_out = NULL; *count_out = 0u;
    json_object *root = json_tokener_parse(json);
    if (!root) { vip_error_set(error, VIP_ERR_MALFORMED, "JSON do resolvedor inválido"); return VIP_ERR_MALFORMED; }
    base_list_t list = {0};
    vip_status_t st = walk_json(&list, root, error);
    json_object_put(root);
    if (st != VIP_OK) { vip_streamfire_free_bases(list.items, list.len); return st; }
    *bases_out = list.items; *count_out = list.len;
    vip_error_clear(error);
    return VIP_OK;
}

void vip_streamfire_free_bases(char **bases, size_t count) {
    if (!bases) return;
    for (size_t i = 0; i < count; ++i) free(bases[i]);
    free(bases);
}

static int mkdir_if_needed(const char *path) {
    if (mkdir(path, 0700) == 0 || errno == EEXIST) return 0;
    return -1;
}

static char *identity_file_path(void) {
    const char *config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char *base = NULL;
    if (config && config[0]) base = vip_strdup(config);
    else if (home && home[0]) {
        size_t n = strlen(home) + 9u;
        base = malloc(n);
        if (base) snprintf(base, n, "%s/.config", home);
    }
    if (!base) return NULL;
    (void)mkdir_if_needed(base);
    size_t dn = strlen(base) + 21u;
    char *dir = malloc(dn);
    if (!dir) { free(base); return NULL; }
    snprintf(dir, dn, "%s/streamfire-resolver", base);
    free(base);
    if (mkdir_if_needed(dir) != 0) { free(dir); return NULL; }
    size_t pn = strlen(dir) + 14u;
    char *path = malloc(pn);
    if (path) snprintf(path, pn, "%s/identity.txt", dir);
    free(dir);
    return path;
}

static bool valid_identity_char(unsigned char c) {
    return isalnum(c) || c == '-' || c == '_';
}

static vip_status_t load_identity(char out[192], vip_error_t *error) {
    char *path = identity_file_path();
    if (!path) { vip_error_set(error, VIP_ERR_IO, "não foi possível preparar identidade do resolvedor"); return VIP_ERR_IO; }
    FILE *fp = fopen(path, "rb");
    if (fp) {
        size_t n = fread(out, 1u, 191u, fp); fclose(fp);
        while (n && (out[n - 1u] == '\n' || out[n - 1u] == '\r' || isspace((unsigned char)out[n - 1u]))) --n;
        out[n] = '\0';
        bool valid = n > 0u;
        for (size_t i = 0; valid && i < n; ++i) valid = valid_identity_char((unsigned char)out[i]);
        if (valid) { free(path); return VIP_OK; }
    }
    unsigned char r[16];
    if (RAND_bytes(r, sizeof(r)) != 1) { free(path); vip_error_set(error, VIP_ERR_IO, "falha ao gerar identidade do resolvedor"); return VIP_ERR_IO; }
    r[6] = (unsigned char)((r[6] & 0x0fu) | 0x40u);
    r[8] = (unsigned char)((r[8] & 0x3fu) | 0x80u);
    snprintf(out, 192u,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],r[8],r[9],r[10],r[11],r[12],r[13],r[14],r[15]);
    fp = fopen(path, "wb");
    if (fp) { fputs(out, fp); fputc('\n', fp); fclose(fp); }
    free(path);
    return VIP_OK;
}

static struct curl_slist *resolver_headers(int profile) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Accept: application/json, text/plain, */*");
    char client[128]; snprintf(client, sizeof(client), "X-Client: %s", resolver_client);
    headers = curl_slist_append(headers, client);
    char key[256]; snprintf(key, sizeof(key), "X-Public-Key: %s", resolver_public_key);
    headers = curl_slist_append(headers, key);
    if (profile >= 1) {
        char origin[160]; snprintf(origin, sizeof(origin), "Origin: %s", resolver_origin);
        char referer[200]; snprintf(referer, sizeof(referer), "Referer: %s", resolver_referer);
        headers = curl_slist_append(headers, origin);
        headers = curl_slist_append(headers, referer);
        headers = curl_slist_append(headers, "Accept-Language: pt-BR,pt;q=0.9,en;q=0.8");
    }
    return headers;
}

static vip_status_t resolver_post(const char *api, int profile,
                                  const char *username, const char *password, const char *identity,
                                  char **body_out, long *http_out, vip_error_t *error) {
    *body_out = NULL; *http_out = 0;
    CURL *curl = curl_easy_init();
    if (!curl) return VIP_ERR_NETWORK;
    char *eu = curl_easy_escape(curl, username, 0), *ep = curl_easy_escape(curl, password, 0),
         *ei = curl_easy_escape(curl, identity, 0);
    if (!eu || !ep || !ei) { if(eu)curl_free(eu);if(ep)curl_free(ep);if(ei)curl_free(ei);curl_easy_cleanup(curl);return VIP_ERR_NOMEM; }
    size_t form_n = strlen(eu)+strlen(ep)+strlen(ei)+128u;
    char *form = malloc(form_n);
    if (!form) { curl_free(eu);curl_free(ep);curl_free(ei);curl_easy_cleanup(curl);return VIP_ERR_NOMEM; }
    snprintf(form, form_n, "code=%s&username=%s&password=%s&identity=%s&platform=%s",
             resolver_code, eu, ep, ei, resolver_platform);
    curl_free(eu); curl_free(ep); curl_free(ei);
    size_t url_n = strlen(api)+16u; char *url = malloc(url_n);
    if (!url) { free(form);curl_easy_cleanup(curl);return VIP_ERR_NOMEM; }
    snprintf(url,url_n,"%s/validate-login",api);
    resolver_buf_t buf={0};
    struct curl_slist *headers=resolver_headers(profile);
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_POST,1L);
    curl_easy_setopt(curl,CURLOPT_POSTFIELDS,form);
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,profile==2?android_ua:chrome_ua);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,resolver_write);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&buf);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,15L);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
#ifdef CURL_HTTP_VERSION_2TLS
    curl_easy_setopt(curl,CURLOPT_HTTP_VERSION,CURL_HTTP_VERSION_2TLS);
#endif
    CURLcode rc=curl_easy_perform(curl);
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,http_out);
    curl_slist_free_all(headers); free(url); free(form); curl_easy_cleanup(curl);
    if(rc!=CURLE_OK || buf.overflow){free(buf.data);vip_error_set(error,VIP_ERR_NETWORK,"falha ao consultar resolvedor");return VIP_ERR_NETWORK;}
    if(!buf.data) buf.data=vip_strdup("");
    if(!buf.data) return VIP_ERR_NOMEM;
    *body_out=buf.data; return VIP_OK;
}

static bool response_payload(const char *body, char **payload_out) {
    *payload_out=NULL;
    json_object *root=json_tokener_parse(body);
    if(!root || json_object_get_type(root)!=json_type_object){if(root)json_object_put(root);return false;}
    json_object *result=NULL;
    if(json_object_object_get_ex(root,"result",&result) && result && json_object_get_type(result)==json_type_boolean && !json_object_get_boolean(result)) {json_object_put(root);return false;}
    json_object *response=NULL;
    bool ok=json_object_object_get_ex(root,"response",&response) && response && json_object_get_type(response)==json_type_string;
    if(ok) *payload_out=vip_strdup(json_object_get_string(response));
    json_object_put(root);
    return ok && *payload_out;
}

static bool verify_xtream_base(const char *base, const char *username, const char *password) {
    CURL *curl=curl_easy_init(); if(!curl) return false;
    char *eu=curl_easy_escape(curl,username,0),*ep=curl_easy_escape(curl,password,0);
    if(!eu||!ep){if(eu)curl_free(eu);if(ep)curl_free(ep);curl_easy_cleanup(curl);return false;}
    size_t n=strlen(base)+strlen(eu)+strlen(ep)+64u; char *url=malloc(n);
    if(!url){curl_free(eu);curl_free(ep);curl_easy_cleanup(curl);return false;}
    snprintf(url,n,"%s/player_api.php?username=%s&password=%s",base,eu,ep);
    curl_free(eu);curl_free(ep);
    resolver_buf_t buf={0};
    curl_easy_setopt(curl,CURLOPT_URL,url);curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,resolver_write);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&buf);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,12L);curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,chrome_ua);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    CURLcode rc=curl_easy_perform(curl); long http=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);
    curl_easy_cleanup(curl);free(url);
    bool ok=false;
    if(rc==CURLE_OK && http>=200 && http<300 && buf.data){vip_error_t ignored={0};ok=vip_xtream_parse_auth_json(buf.data,&ignored)==VIP_OK;}
    free(buf.data);return ok;
}

void vip_server_resolution_clear(vip_server_resolution_t *resolution) {
    if (!resolution) return;
    free(resolution->primary);
    free(resolution->alternate);
    memset(resolution, 0, sizeof(*resolution));
}

vip_status_t vip_streamfire_resolve_servers(const char *username,
                                            const char *password,
                                            vip_server_resolution_t *out,
                                            vip_error_t *error) {
    if(!username||!password||!out)return VIP_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    char identity[192]={0}; vip_status_t st=load_identity(identity,error); if(st!=VIP_OK)return st;
    char **candidates=NULL;size_t candidate_count=0u;
    for(size_t ai=0;ai<sizeof(resolver_apis)/sizeof(resolver_apis[0]) && candidate_count==0u;++ai){
        for(int profile=0;profile<3 && candidate_count==0u;++profile){
            char *body=NULL;long http=0;vip_error_t net={0};
            if(resolver_post(resolver_apis[ai],profile,username,password,identity,&body,&http,&net)!=VIP_OK){free(body);continue;}
            if(http>=500){free(body);continue;}
            char *payload=NULL;if(!response_payload(body,&payload)){free(body);continue;}free(body);
            char *decoded=NULL;vip_error_t decode={0};
            if(vip_streamfire_decode_payload(payload,identity,&decoded,&decode)==VIP_OK)
                (void)vip_streamfire_collect_bases(decoded,&candidates,&candidate_count,&decode);
            free(payload);free(decoded);
        }
    }
    if(candidate_count==0u){vip_error_set(error,VIP_ERR_NETWORK,"resolvedor não retornou servidores utilizáveis");return VIP_ERR_NETWORK;}
    for(size_t i=0;i<candidate_count;++i){
        if(!verify_xtream_base(candidates[i],username,password))continue;
        if(!out->primary) out->primary=vip_strdup(candidates[i]);
        else if(!out->alternate && strcmp(out->primary,candidates[i])!=0) {out->alternate=vip_strdup(candidates[i]);break;}
    }
    vip_streamfire_free_bases(candidates,candidate_count);
    if(!out->primary){vip_server_resolution_clear(out);vip_error_set(error,VIP_ERR_NETWORK,"resolvedor respondeu, mas nenhum servidor Xtream foi confirmado");return VIP_ERR_NETWORK;}
    vip_error_clear(error);return VIP_OK;
}
