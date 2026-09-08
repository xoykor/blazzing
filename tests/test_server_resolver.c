/* SPDX-License-Identifier: MIT */
#include "visual_iptv/server_resolver.h"
#include "test_common.h"
#include <string.h>
#include <stdlib.h>

int main(void) {
    vip_error_t error = {0};
    const char *identity = "12345678-1234-4234-9234-123456789abc";
    const char *payload =
        "FrKJsQy_4aSlogWdYFpDsfQaPFeUGOdM6RcN7lbFIeR2rqVgHpDT3qUwORcN5ctyfSDAirui."
        "WIYG9ksGasRjy5MNpawxHYudF9kWdkusmCma8sKT7Zsc9gp3N_9x_wYnV-06QKjlp24cLCXY";
    char *decoded = NULL;
    TEST_STATUS(vip_streamfire_decode_payload(payload, identity, &decoded, &error), VIP_OK, &error);
    TEST_CHECK(decoded != NULL);
    TEST_CHECK(strstr(decoded, "dns_list") != NULL);

    char **bases = NULL;
    size_t count = 0u;
    TEST_STATUS(vip_streamfire_collect_bases(decoded, &bases, &count, &error), VIP_OK, &error);
    TEST_CHECK(count == 2u);
    TEST_CHECK(strcmp(bases[0], "https://one.example") == 0);
    TEST_CHECK(strcmp(bases[1], "http://two.example") == 0);
    vip_streamfire_free_bases(bases, count);
    free(decoded);

    TEST_STATUS(vip_streamfire_decode_payload("invalido", identity, &decoded, &error), VIP_ERR_MALFORMED, &error);
    return 0;
}
