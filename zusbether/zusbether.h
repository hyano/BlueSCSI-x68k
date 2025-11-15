/*
 * Copyright (c) 2025 Yuichi Nakamura (@yunkya2)
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

#ifndef _ZUSBETHER_H_
#define _ZUSBETHER_H_

#include <stdint.h>

//****************************************************************************
// Human68k structure definitions
//****************************************************************************

struct dos_req_header {
  uint8_t magic;       // +0x00.b  Constant (26)
  uint8_t unit;        // +0x01.b  Unit number
  uint8_t command;     // +0x02.b  Command code
  uint8_t errl;        // +0x03.b  Error code low
  uint8_t errh;        // +0x04.b  Error code high
  uint8_t reserved[8]; // +0x05 .. +0x0c  not used
  uint8_t attr;        // +0x0d.b  Attribute / Seek mode
  void *addr;          // +0x0e.l  Buffer address
  uint32_t status;     // +0x12.l  Bytes / Buffer / Result status
  void *fcb;           // +0x16.l  FCB
} __attribute__((packed, aligned(2)));

struct dos_dev_header {
  struct dos_dev_header *next;  // +0x00.l  Link pointer
  uint16_t type;       // +0x04.w  Device type
  void *strategy;      // +0x06.l  Strategy routine entry point
  void *interrupt;     // +0x0a.l  Interrupt routine entry point
  char name[8];        // +0x0e .. +0x15  Device name
} __attribute__((packed, aligned(2)));

//****************************************************************************
// Private structure definitions
//****************************************************************************

#define AX_CMD_SET_SW_PHY               0x06
#define AX_CMD_READ_PHY_REG             0x07
#define AX_CMD_WRITE_PHY_REG            0x08
#define AX_CMD_SET_HW_PHY               0x0a
#define AX_CMD_READ_SROM                0x0b
#define AX_CMD_WRITE_SROM               0x0c
#define AX_CMD_SROM_WRITE_ENABLE        0x0d
#define AX_CMD_SROM_WRITE_DISABLE       0x0e
#define AX_CMD_READ_RX_CTL              0x0f
#define AX_CMD_WRITE_RX_CTL             0x10
#define AX_CMD_READ_IPG                 0x11
#define AX_CMD_WRITE_IPG                0x12
#define AX_CMD_READ_NODE_ID             0x13
#define AX_CMD_WRITE_NODE_ID            0x14
#define AX_CMD_READ_MULTI_FILTER        0x15
#define AX_CMD_WRITE_MULTI_FILTER       0x16
#define AX_CMD_READ_PHY_ID              0x19
#define AX_CMD_READ_MEDIUM_STATUS       0x1a
#define AX_CMD_WRITE_MEDIUM_MODE        0x1b
#define AX_CMD_READ_MONITOR_MODE        0x1c
#define AX_CMD_WRITE_MONITOR_MODE       0x1d
#define AX_CMD_READ_GPIOS               0x1e
#define AX_CMD_WRITE_GPIOS              0x1f
#define AX_CMD_SW_RESET                 0x20
#define AX_CMD_SW_PHY_STATUS            0x21
#define AX_CMD_SW_PHY_SELECT            0x22

#define REQ_VD_IN       (ZUSB_REQ_DIR_IN|ZUSB_REQ_TYPE_VENDOR|ZUSB_REQ_RCPT_DEVICE)
#define REQ_VD_OUT      (ZUSB_REQ_DIR_OUT|ZUSB_REQ_TYPE_VENDOR|ZUSB_REQ_RCPT_DEVICE)

#endif /* _ZUSBETHER_H_ */
