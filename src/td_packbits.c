/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

#include "td_packbits.h"

#include <string.h>

static size_t count_run(const uint8_t *source, size_t offset, size_t length)
{
    size_t run = 1;

    while (offset + run < length && run < 128 &&
           source[offset + run] == source[offset])
        run++;

    return run;
}

size_t td_packbits_encode(const uint8_t *source, size_t length,
                          uint8_t *destination, size_t destination_size)
{
    size_t input = 0;
    size_t output = 0;

    while (input < length) {
        size_t run = count_run(source, input, length);

        if (run >= 2) {
            if (output + 2 > destination_size)
                return 0;
            destination[output++] = (uint8_t)(1 - (int)run);
            destination[output++] = source[input];
            input += run;
        } else {
            size_t start = input;

            input += run;
            while (input < length && (input - start) < 128) {
                size_t next_run = count_run(source, input, length);

                if (next_run >= 2)
                    break;
                if ((input - start) + next_run > 128)
                    break;
                input += next_run;
            }

            {
                size_t literal = input - start;

                if (output + 1 + literal > destination_size)
                    return 0;
                destination[output++] = (uint8_t)(literal - 1);
                memcpy(destination + output, source + start, literal);
                output += literal;
            }
        }
    }

    return output;
}

size_t td_packbits_decode(const uint8_t *source, size_t length,
                          uint8_t *destination, size_t destination_size)
{
    size_t input = 0;
    size_t output = 0;

    while (input < length) {
        int control = (int)(int8_t)source[input++];

        if (control >= 0) {
            size_t count = (size_t)control + 1;

            if (input + count > length || output + count > destination_size)
                return 0;
            memcpy(destination + output, source + input, count);
            input += count;
            output += count;
        } else if (control != -128) {
            size_t count = (size_t)(1 - control);

            if (input >= length || output + count > destination_size)
                return 0;
            memset(destination + output, source[input++], count);
            output += count;
        }
        /* -128 (0x80) is a no-op in TIFF PackBits. */
    }

    return output;
}
