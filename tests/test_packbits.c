/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "td_packbits.h"

static void check_known_vectors(void)
{
    uint8_t source[64];
    uint8_t encoded[128];
    uint8_t decoded[128];
    size_t length;

    /* Brother's reference example: 20 x 00h -> ED 00h. */
    memset(source, 0, 20);
    length = td_packbits_encode(source, 20, encoded, sizeof(encoded));
    assert(length == 2);
    assert(encoded[0] == 0xED);
    assert(encoded[1] == 0x00);

    /* 2 x 22h -> FF 22h. */
    source[0] = 0x22;
    source[1] = 0x22;
    length = td_packbits_encode(source, 2, encoded, sizeof(encoded));
    assert(length == 2);
    assert(encoded[0] == 0xFF);
    assert(encoded[1] == 0x22);

    /* Six different bytes -> 05 followed by the bytes. */
    {
        static const uint8_t literal[] = {0x23, 0xBA, 0xBF, 0xA2, 0x22, 0x2B};

        length = td_packbits_encode(literal, sizeof(literal), encoded, sizeof(encoded));
        assert(length == 7);
        assert(encoded[0] == 0x05);
        assert(memcmp(encoded + 1, literal, sizeof(literal)) == 0);
    }

    /* The complete example from Brother's manual. */
    {
        uint8_t example[28];
        static const uint8_t expected[] = {
            0xED, 0x00, 0xFF, 0x22, 0x05, 0x23, 0xBA, 0xBF, 0xA2, 0x22, 0x2B
        };

        memset(example, 0x00, 20);
        example[20] = 0x22;
        example[21] = 0x22;
        example[22] = 0x23;
        example[23] = 0xBA;
        example[24] = 0xBF;
        example[25] = 0xA2;
        example[26] = 0x22;
        example[27] = 0x2B;

        length = td_packbits_encode(example, sizeof(example), encoded, sizeof(encoded));
        assert(length == sizeof(expected));
        assert(memcmp(encoded, expected, sizeof(expected)) == 0);
    }

    (void)decoded;
}

static void check_round_trip(void)
{
    uint8_t source[512];
    uint8_t encoded[1024];
    uint8_t decoded[512];
    unsigned pattern;

    for (pattern = 0; pattern < 16; pattern++) {
        size_t length = 1 + (pattern * 37) % sizeof(source);
        size_t i;
        size_t encoded_length;
        size_t decoded_length;

        for (i = 0; i < length; i++) {
            switch (pattern % 4) {
            case 0:
                source[i] = (uint8_t)(i * 17);
                break;
            case 1:
                source[i] = 0x00;
                break;
            case 2:
                source[i] = (uint8_t)((i / 7) % 2 ? 0xFF : 0x00);
                break;
            default:
                source[i] = (uint8_t)(i % 5);
                break;
            }
        }

        encoded_length = td_packbits_encode(source, length, encoded, sizeof(encoded));
        assert(encoded_length > 0);
        decoded_length = td_packbits_decode(encoded, encoded_length, decoded, sizeof(decoded));
        assert(decoded_length == length);
        assert(memcmp(source, decoded, length) == 0);
    }
}

static void check_limits(void)
{
    uint8_t source[160];
    uint8_t encoded[4];
    size_t i;

    for (i = 0; i < sizeof(source); i++)
        source[i] = (uint8_t)(i * 3 + 1);

    /* Destination cannot hold even the literal header + first bytes. */
    assert(td_packbits_encode(source, sizeof(source), encoded, 3) == 0);

    /* A long run uses the 128-byte repeat maximum. */
    memset(source, 0xAA, sizeof(source));
    {
        uint8_t larger[16];
        size_t length = td_packbits_encode(source, sizeof(source), larger, sizeof(larger));
        assert(length == 4); /* 128 x AA, then 32 x AA */
        assert(larger[0] == 0x81);
        assert(larger[1] == 0xAA);
        assert(larger[2] == 0xE1);
        assert(larger[3] == 0xAA);
    }
}

int main(void)
{
    check_known_vectors();
    check_round_trip();
    check_limits();
    puts("test_packbits: OK");
    return 0;
}
