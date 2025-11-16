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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <x68k/iocs.h>

#include "daynaport.h"

int32_t _iocs_s_dataini(int, void *);
__asm__(
".global _iocs_s_dataini\n"
".type _iocs_s_dataini,@function\n"
"_iocs_s_dataini:\n"
"	move.l	%d3, %sp@-\n"
"	movem.l	%sp@(8),%d3/%a1\n"
"	moveq	#11, %d1\n"
"	moveq	#0xfffffff5, %d0\n"
"	trap	#15\n"
"	move.l	%sp@+, %d3\n"
"	rts\n"
);

uint32_t ontime(void)
{
    struct iocs_time t;
    t = _iocs_ontime();
    return t.day * (24*60*60*100) + t.sec;
}

void wait_ms(uint32_t wait)
{
    uint32_t start;
    uint32_t now;
    uint32_t wait_10ms = wait / 10;

    start = ontime();
    for (;;)
    {
        now = ontime();
        if ((now - start) >= wait) break;
    }
}

static int32_t cmdout(int32_t size, int32_t target, uint8_t *cmd)
{
    int32_t status;

    for (int32_t i = 0; i < 2; i++)
    {
        status = _iocs_s_select(target);
        if (status == 0) break;
    }
    if (status != 0) return -1;

    cmd[1] |= (target >> 16) << 5;
    status = _iocs_s_cmdout(size, cmd);

    return status;
}

static int32_t stsmsgin(void)
{
    int32_t status;
    uint8_t sts;
    uint8_t msg;

    status = _iocs_s_stsin(&sts);
    if (status != 0) return -1;

    status = _iocs_s_msgin(&msg);
    if (status != 0) return -1;

    return (msg << 16) | sts;
}

int32_t dp_inquiry(int32_t target, struct dp_inquiry_data *data)
{
    return _iocs_s_inquiry(sizeof(*data), target, (struct iocs_inquiry *)data);
}

int32_t dp_stat(int32_t size, int32_t target, void *buffer)
{
    int32_t status;
    uint8_t cmd[6] = {0x09, 0x00, 0x00, 0x00, 0x00, 0x00};
    cmd[4] = size;
    cmd[3] = size >> 8;

    status = cmdout(sizeof(cmd), target, cmd);
    if (status != 0) return -1;

    status = _iocs_s_dataini(size, buffer);
    if (status == -1) return -1;

    status = stsmsgin();

    return status;
}

int32_t dp_enable(int32_t target, bool enable)
{
    int32_t status;
    uint8_t cmd[6] = {0x0e, 0x00, 0x00, 0x00, 0x00, 0x00};
    cmd[5] = enable ? 0x80 : 0x00;

    status = cmdout(sizeof(cmd), target, cmd);
    if (status != 0) return -1;

    status = stsmsgin();

    wait_ms(1000);

    return status;
}

int32_t dp_recv(int32_t size, int32_t target, void *buffer)
{
    int32_t status;
    uint8_t cmd[6] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00};
    cmd[4] = size;
    cmd[3] = size >> 8;
    cmd[5] = 0xc0;

    status = cmdout(sizeof(cmd), target, cmd);
    if (status != 0) return -1;

    status = _iocs_s_datain(size, buffer);
    if (status == -1) return -1;

    status = stsmsgin();

    return status;
}

int32_t dp_send(int32_t size, int32_t target, void *buffer)
{
    int32_t status;
    uint8_t cmd[6] = {0x0a, 0x00, 0x00, 0x00, 0x00, 0x00};
    cmd[4] = size;
    cmd[3] = size >> 8;

    status = cmdout(sizeof(cmd), target, cmd);
    if (status != 0) return -1;

    status = _iocs_s_dataout(size, buffer);
    if (status == -1) return -1;

    status = stsmsgin();

    return status;
}

bool dp_is_daynaport(struct dp_inquiry_data *data)
{
    bool ret = false;
    const uint8_t vendor[8] = {
        /* "Dayna  " */
        0x44, 0x61, 0x79, 0x6e, 0x61, 0x20, 0x20, 0x20
    };
    const uint8_t product[16] = {
        /* "SCSI/Link       " */
        0x53, 0x43, 0x53, 0x49, 0x2f, 0x4c, 0x69, 0x6e,
        0x6b, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
    };

    do
    {
        if (data->unit != 0x03) break;
        if (data->info != 0x00) break;
        if (data->ver != 0x01) break;
        if (data->size < 0x1f) break;
        if (memcmp(data->vendor, vendor, sizeof(vendor)) != 0) break;
        if (memcmp(data->product, product, sizeof(product)) != 0) break;

        ret = true;
    }
    while(0);

    return ret;
}

bool dp_is_in_iocs(void)
{
    volatile int16_t *iniocs = (short*)0x0a0e;
    return (*iniocs != -1);
}

bool dp_is_free(void)
{
    int32_t phase = _iocs_s_phase();
    return (phase == 0);
}
