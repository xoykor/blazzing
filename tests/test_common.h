/* SPDX-License-Identifier: MIT */
#ifndef VISUAL_IPTV_TEST_COMMON_H
#define VISUAL_IPTV_TEST_COMMON_H

#include <stdio.h>

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

#define TEST_STATUS(expression, expected, error_ptr) \
    do { \
        vip_status_t test_status__ = (expression); \
        if (test_status__ != (expected)) { \
            const vip_error_t *test_error__ = (error_ptr); \
            fprintf(stderr, \
                    "FAIL %s:%d: %s => status=%d, esperado=%d%s%s\n", \
                    __FILE__, __LINE__, #expression, (int)test_status__, (int)(expected), \
                    (test_error__ && test_error__->message[0]) ? ": " : "", \
                    (test_error__ && test_error__->message[0]) ? test_error__->message : ""); \
            return 1; \
        } \
    } while (0)

#endif
