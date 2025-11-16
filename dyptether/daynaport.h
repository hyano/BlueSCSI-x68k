/*
 * Copyright (c) 2025 Hirokuni Yano (@hyano)
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef DAYNAPORT_H
#define DAYNAPORT_H

#include <stdint.h>
#include <stdbool.h>

struct dp_inquiry_data
{
    uint8_t unit;
    uint8_t info;
    uint8_t ver;
    uint8_t reserve;
    uint8_t size;
    uint8_t unused[2];
    uint8_t flags;
    uint8_t vendor[8];
    uint8_t product[16];
    uint8_t revision[4];
    uint8_t extra[8];
};

static inline __attribute__((always_inline)) uint16_t dp_irq_disable(void)
{
    uint16_t sr;
    __asm__ volatile (
        "move.w %%sr,%0\n"
        "ori.w #0x0700,%%sr\n"
        : "=d"(sr)
        :
        : "memory"
    );
    return sr;
}

static inline __attribute__((always_inline)) void dp_irq_enable(uint16_t sr)
{
    __asm__ volatile(
        "move.w %0,%%sr\n"
        :
        : "d"(sr)
        : "memory"
    );
}

int32_t dp_inquiry(int32_t target, struct dp_inquiry_data *data);
int32_t dp_stat(int32_t size, int32_t target, void *buffer);
int32_t dp_enable(int32_t target, bool enable);
int32_t dp_recv(int32_t size, int32_t target, void *buffer);
int32_t dp_send(int32_t size, int32_t target, void *buffer);

bool dp_is_daynaport(struct dp_inquiry_data *data);
bool dp_is_in_iocs(void);
bool dp_is_free(void);

#endif /* DAYNAPORT_H */
