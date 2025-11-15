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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>

#include <x68k/iocs.h>
#include <x68k/dos.h>

#include <zusb.h>

#include "zusbether.h"

//****************************************************************************
// Definition
//****************************************************************************

// zusbbuf usage (0x000 - 0xf80)
#define ZUSBBUF_TEMP        0x000   // 0x000 - 0x007
#define ZUSBBUF_INTR        0x008   // 0x008 - 0x00f
#define ZUSBBUF_SEND        0x010   // 0x010 - 0x77f
#define ZUSBBUF_SENDDATA    0x014
#define ZUSBBUF_RECV        0x780   // 0x780 - 0xf7f
#define ZUSBBUF_RECVDATA    0x784

// endpoint
#define EP_INTR             0
#define EP_RECV             1
#define EP_SEND             2

typedef struct ax_packet_header {
  uint16_t len;
  uint16_t clen;
} ax_packet_header_t;

typedef void (*rcvhandler_t)(int len, uint8_t *buff, uint32_t flag);

//****************************************************************************
// Global variables
//****************************************************************************

struct dos_req_header *reqheader;         // Human68kからのリクエストヘッダ

struct regdata {
  void *oldtrap;    // trap ベクタ変更前のアドレス
  void *oldivaddr;  // 割り込みベクタ変更前のアドレス
  char ifname[4];   // ネットワークインターフェース名

  int removable;    // 0:CONFIG.SYSで登録された 1:Human68k起動後に登録された
  int ivect;        // 割り込みベクタ番号
  int trapno;       // 使用するtrap番号 (0-7)
  int ch;           // ZUSB チャネル番号
  int nproto;       // このインターフェースを使用するプロトコル数

  uint16_t vid;     // USB Vendor ID
  uint16_t pid;     // USB Product ID
  uint8_t iProduct; // USB Product string index
} regdata = {
  .ifname = "en0",
  .trapno = 0,
  .nproto = 0,
  .vid = 0x0b95,
  .pid = 0x7720,
};

struct regdata *regp = &regdata;

extern struct dos_dev_header devheader;
extern void trap_entry(void);
extern void inthandler_asm(void);

//****************************************************************************
// Static variables
//****************************************************************************

static jmp_buf jenv;                      // ZUSB通信エラー時のジャンプ先
static int inrecovery = false;            // 通信エラー回復中
static int hotplug = false;               // USB接続状態が変化した
static int sentpacket = false;            // 送信済みパケットがある
static int flag_r = false;                // 常駐解除フラグ

#define N_PROTO_HANDLER   8
static struct {
  int proto;
  rcvhandler_t func;
} proto_handler[N_PROTO_HANDLER];

static zusb_endpoint_config_t epcfg[] = {
    { ZUSB_DIR_IN,  ZUSB_XFER_INTERRUPT, 0 },
    { ZUSB_DIR_IN,  ZUSB_XFER_BULK, 0 },
    { ZUSB_DIR_OUT, ZUSB_XFER_BULK, 0 },
    { 0, 0, -1 },
};

//****************************************************************************
// for debugging
//****************************************************************************

//#define DEBUG
//#define DEBUG_UART
//#define DEBUG_SEND_PACKET_DUMP
//#define DEBUG_RECV_PACKET_DUMP
//#define DEBUG_LINK_STATUS

#ifdef DEBUG
#include <x68k/iocs.h>
char heap[1024];                // temporary heap for debug print
void *_HSTA = heap;
void *_HEND = heap + 1024;
void *_PSP;

void DPRINTF(char *fmt, ...)
{
  char buf[256];
  va_list ap;

  va_start(ap, fmt);
  vsiprintf(buf, fmt, ap);
  va_end(ap);
#ifndef DEBUG_UART
  _iocs_b_print(buf);
#else
  char *p = buf;
  while (*p) {
    while (_iocs_osns232c() == 0)
      ;
    _iocs_out232c(*p++);
  }
#endif
}
#else
#define DPRINTF(...)
#endif

//****************************************************************************
// Private functions
//****************************************************************************

//----------------------------------------------------------------------------
// Utiility function
//----------------------------------------------------------------------------

// zusbetherが常駐しているかどうかを調べる
static int find_zusbether(struct dos_dev_header **res)
{
  // Human68kからNULデバイスドライバを探す
  char *p = (char *)0x006800;
  while (memcmp(p, "NUL     ", 8) != 0) {
    p += 2;
  }

  struct dos_dev_header *devh = (struct dos_dev_header *)(p - 14);
  while (devh->next != (struct dos_dev_header *)-1) {
    char *p = devh->next->name;
    if (memcmp(p, "/dev/", 5) == 0 &&
        memcmp(p + 5, regp->ifname, 3) == 0 &&
        memcmp(p + 8, "EthDZeth", 8) == 0) {
      *res = devh;
      return 1; // 常駐していた場合は一つ前のデバイスヘッダへのポインタを返す
    }
    devh = devh->next;
  }
  *res = devh;
  return 0;     // 常駐していなかった場合は最後のデバイスヘッダへのポインタを返す
}

// trap #0～#7のうち使用可能なものがあるかをチェック
static int find_unused_trap(int defno)
{
  if (defno >= 0) {
    if ((uint32_t)_dos_intvcg(0x20 + defno) & 0xff000000) {
      return defno;
    }
  }
  for (int i = 0; i < 8; i++) {
    if ((uint32_t)_dos_intvcg(0x20 + i) & 0xff000000) {
      return i;
    }
  }
  return -1;
}

void msleep(int time)
{
  struct iocs_time tm1, tm2;
  tm1 = _iocs_ontime();
  while (1) {
    tm2 = _iocs_ontime();
    int t = tm2.sec - tm1.sec;
    if (t < 0) {
      tm1 = _iocs_ontime();
    } else if (t >= time) {
      return;
    }
  }
}

unsigned long hextoul(const char *p, char **endp)
{
  unsigned long val = 0;
  while (1) {
    char c = tolower(*p++);
    if (c >= '0' && c <= '9') {
      val = val * 16 + c - '0';
    } else if (c >= 'a' && c <= 'f') {
      val = val * 16 + c - 'a' + 10;
    } else {
      break;
    }
  }
  if (endp) {
    *endp = (char *)p - 1;
  }
  return val;
}

//----------------------------------------------------------------------------
// Protocol handler
//----------------------------------------------------------------------------

static rcvhandler_t find_proto_handler(int proto)
{
  for (int i = 0; i < N_PROTO_HANDLER; i++) {
    if (proto_handler[i].proto == proto) {
      return proto_handler[i].func;
    }
  }
  return NULL;
}

static int add_proto_handler(int proto, rcvhandler_t func)
{
  for (int i = 0; i < N_PROTO_HANDLER; i++) {
    if (proto_handler[i].proto == proto) {
      return -1;    // already registered
    }
  }
  for (int i = 0; i < N_PROTO_HANDLER; i++) {
    if (proto_handler[i].proto == 0) {
      proto_handler[i].proto = proto;
      proto_handler[i].func = func;
      regp->nproto++;
      return (regp->nproto == 1) ? 1 : 0;
    }
  }
  return -1;    // no space
}

static int delete_proto_handler(int proto)
{
  for (int i = 0; i < N_PROTO_HANDLER; i++) {
    if (proto_handler[i].proto == proto) {
      proto_handler[i].proto = 0;
      proto_handler[i].func = NULL;
      regp->nproto--;
      return (regp->nproto == 0) ? 1 : 0;
    }
  }
  return -1;    // not found
}

//----------------------------------------------------------------------------
// AX88772 register access
//----------------------------------------------------------------------------

static void ax_cmd(int req, int cmd, int value, int index, int size, void *data)
{
  zusb_send_control(req, cmd, value, index, size, data);
  if (zusb->stat & ZUSB_STAT_ERROR) {
    DPRINTF("ax_cmd error\r\n");
    longjmp(jenv, -1);
  }
}

static void ax_cmd_read(int cmd, int value, int index, int size, void *data)
{
  ax_cmd(REQ_VD_IN, cmd, value, index, size, data);
}

static void ax_cmd_write(int cmd, int value, int index, int size, void *data)
{
  ax_cmd(REQ_VD_OUT, cmd, value, index, size, data);
}

static void ax_phy_write(int phyid, int reg, int val)
{
  *(uint16_t *)&zusbbuf[ZUSBBUF_TEMP] = val;
  ax_cmd_write(AX_CMD_SET_SW_PHY, 0, 0, 0, NULL);
  ax_cmd_write(AX_CMD_WRITE_PHY_REG, phyid, reg, 2, &zusbbuf[ZUSBBUF_TEMP]);
  ax_cmd_write(AX_CMD_SET_HW_PHY, 0, 0, 0, NULL);
}

//----------------------------------------------------------------------------
// AX88772 initialization
//----------------------------------------------------------------------------

static int ax_init(void)
{
  sentpacket = false;

  // RSE|GPO_2|GPO2EN -- Reload EEPROM, GPIO2 OUT=1
  ax_cmd_write(AX_CMD_WRITE_GPIOS, 0xb0, 0, 0, NULL);
  msleep(10);

  // PSEL=1 -- Select embedded Phy manually
  ax_cmd_write(AX_CMD_SW_PHY_SELECT, 1, 0, 0, NULL);

  // IPPD|PRL -- Internal Phy power down, External Phy reset=high
  ax_cmd_write(AX_CMD_SW_RESET, 0x48, 0, 0, NULL);
  msleep(15);
  // 0 -- Internal Phy operation mode, External Phy reset=low
  ax_cmd_write(AX_CMD_SW_RESET, 0x00, 0, 0, NULL);
  msleep(15);
  // IPRL -- Internal Phy operating state
  ax_cmd_write(AX_CMD_SW_RESET, 0x20, 0, 0, NULL);
  msleep(15);

  // Rx Control clear
  ax_cmd_write(AX_CMD_WRITE_RX_CTL, 0, 0, 0, NULL);

  // PRL -- Internal Phy reset state, External Phy reset=high
  ax_cmd_write(AX_CMD_SW_RESET, 0x08, 0, 0, NULL);
  msleep(15);
  // IPRL|PRL -- Internal Phy operating state, External Phy reset=high
  ax_cmd_write(AX_CMD_SW_RESET, 0x28, 0, 0, NULL);
  msleep(15);

  // MII_BMCR = BMCR_RESET
  ax_phy_write(0, 0x00, 0x8000);
  // MII_ANAR = ALL|CSMA (default)
  ax_phy_write(0, 0x04, 0x01e1);

  return 0;
}

static int ax_fini(void)
{
  // IPPD|PRL -- Internal Phy power down, External Phy reset=high
  ax_cmd_write(AX_CMD_SW_RESET, 0x48, 0, 0, NULL);
  return 0;
}

static int ax_rx_init(int enable)
{
  zusb->inten = 0;

  if (enable) {
    // FD|RFC|TFC|PS|RE -- full duplex, RX/TX flow control, 100Mbps, Rx enable
    ax_cmd_write(AX_CMD_WRITE_MEDIUM_MODE, 0x0336, 0, 0, NULL);
    // SO|AB -- start operation, accept broadcast
    ax_cmd_write(AX_CMD_WRITE_RX_CTL, 0x0088, 0, 0, NULL);

    zusb_set_ep_region(1, &zusbbuf[ZUSBBUF_RECV], 2048);
    zusb->stat = ZUSB_STAT_PCOMPLETE(EP_RECV) | ZUSB_STAT_HOTPLUG;
    zusb->inten = ZUSB_STAT_PCOMPLETE(EP_RECV) | ZUSB_STAT_HOTPLUG;
    zusb_send_cmd(ZUSB_CMD_SUBMITXFER(EP_RECV));
#ifdef DEBUG_LINK_STATUS
    zusb_set_ep_region(0, &zusbbuf[ZUSBBUF_INTR], 8);
    zusb->stat = ZUSB_STAT_PCOMPLETE(EP_INTR);
    zusb->inten |= ZUSB_STAT_PCOMPLETE(EP_INTR);
    zusb_send_cmd(ZUSB_CMD_SUBMITXFER(EP_INTR));
#endif
  } else {
    // FD|RFC|TFC|PS -- full duplex, RX/TX flow control, 100Mbps (Rx disable)
    ax_cmd_write(AX_CMD_WRITE_MEDIUM_MODE, 0x0236, 0, 0, NULL);
    // Rx Control clear
    ax_cmd_write(AX_CMD_WRITE_RX_CTL, 0x0000, 0, 0, NULL);
  }
  return 0;
}

//****************************************************************************
// Ether driver command handler
//****************************************************************************

int etherfunc(int cmd, void *args)
{
  int retry = false;
  DPRINTF("etherfunc:%d %p\r\n", cmd, args);
  zusb_set_channel(regp->ch);

  if (setjmp(jenv) != 0) {
    zusb_disconnect_device();
    DPRINTF("etherfunc error 0x%04x\r\n", zusb->err);
    retry = true;
    inrecovery = true;
    hotplug = false;
  }

  if (inrecovery) {
    if (!retry && !hotplug) {
      return -1;
    }

    DPRINTF("error recovery\r\n");
    int devid;
    if ((devid = zusb_find_device_with_vid_pid(regp->vid, regp->pid, 0)) <= 0) {
      return -1;
    }
    if (zusb_connect_device(devid, 1, 255, 255, 0, epcfg) <= 0) {
      return -1;
    }
    ax_init();
    if (regp->nproto > 0) {
      ax_rx_init(true);
    }
    inrecovery = false;
  }

  switch (cmd) {
  // command -1: Get trap number
  case -1:
    return regp->trapno;

  // command 0: Get driver version
  case 0:
    return 0x100;

  // command 1: Get MAC addr
  case 1:
    ax_cmd_read(AX_CMD_READ_NODE_ID, 0, 0, 6, &zusbbuf[ZUSBBUF_TEMP]);
    memcpy(args, &zusbbuf[ZUSBBUF_TEMP], 6);
    return (int)args;

  // command 2: Get PROM addr
  case 2:
    for (int i = 0; i < 3; i++) {
      ax_cmd_read(AX_CMD_READ_SROM, 4 + i, 0, 2, &zusbbuf[ZUSBBUF_TEMP]);
      ((uint16_t *)args)[i] = *(uint16_t *)&zusbbuf[ZUSBBUF_TEMP];
    }
    return (int)args;

  // command 3: Set MAC addr
  case 3:
    memcpy(&zusbbuf[ZUSBBUF_TEMP], args, 6);
    ax_cmd_write(AX_CMD_WRITE_NODE_ID, 0, 0, 6, &zusbbuf[ZUSBBUF_TEMP]);
    return 0;

  // command 4: Send ether packet
  case 4:
  {
    struct {
      int size;
      uint8_t *buf;
    } *sendpkt = args;

    // 送信済みパケットがある場合は送信完了を待つ
    while (sentpacket && !(zusb->stat & ZUSB_STAT_PCOMPLETE(EP_SEND))) {
      if (zusb->stat & ZUSB_STAT_ERROR) {
        DPRINTF("send error\r\n");
        longjmp(jenv, -1);
      }
    }

    int len = sendpkt->size;
    memcpy(&zusbbuf[ZUSBBUF_SENDDATA], sendpkt->buf, sendpkt->size);
    ax_packet_header_t *hdr = (ax_packet_header_t *)&zusbbuf[ZUSBBUF_SEND];
    hdr->len = zusb_bswap16(len);
    hdr->clen = hdr->len ^ 0xffff;
    len += 4;
    zusb_set_ep_region(2, &zusbbuf[ZUSBBUF_SEND], len);
#ifdef DEBUG_SEND_PACKET_DUMP
    DPRINTF("send len=%d\r\n", len);
    for (int i = 0; i < len; i++) {
      char buf[20];
      if (i % 16 == 0) {
        DPRINTF("%04x:", i);
      }
      uint8_t byte = zusbbuf[ZUSBBUF_SEND + i];
        DPRINTF(" %02x", byte);
        buf[i % 16] = isprint(byte) ? byte : '.';
      if (i % 16 == 15) {
        buf[16] = '\0';
        DPRINTF("  %s\r\n", buf);
      }
    }
    if (len % 16 != 0) {
      DPRINTF("\r\n");
    }
#endif
    zusb->stat = ZUSB_STAT_PCOMPLETE(EP_SEND) | ZUSB_STAT_ERROR;
    zusb_send_cmd(ZUSB_CMD_SUBMITXFER(EP_SEND));
    if (zusb->stat & ZUSB_STAT_ERROR) {
      DPRINTF("send error\r\n");
      longjmp(jenv, -1);
    }
    sentpacket = true;
    return 0;
  }

  // command 5: Set int addr
  case 5:
  {
    struct {
      int proto;
      void (*handler)(int, uint8_t *, uint32_t);
    } *setint = args;

    int res = add_proto_handler(setint->proto, setint->handler);
    DPRINTF("proto=0x%x handler=%p res=%d\r\n", setint->proto, setint->handler, res);
    if (res > 0) {
      DPRINTF("enable receiver\r\n");
      ax_rx_init(true);
    }
    return 0;
  }

  // command 6: Get int addr
  case 6:
  {
    int proto = (int)args;
    return (int)find_proto_handler(proto);
  }

  // command 7: Delete int addr
  case 7:
  {
    int proto = (int)args;
    int res = delete_proto_handler(proto);
    DPRINTF("proto=0x%x res=%d\r\n", proto, res);
    if (res > 0) {
      DPRINTF("disable receiver\r\n");
      zusb_send_cmd(ZUSB_CMD_CANCELXFER(EP_RECV));
      ax_rx_init(false);
    }
    return 0;   // not supported yet
  }

  // command 8: Set multicast addr
  case 8:
    return 0;   // not supported yet

  // command 9: Get statistics
  case 9:
    return 0;   // not supported yet

  default:
    return -1;
  }
}

//****************************************************************************
// USB interrupt handler
//****************************************************************************

void inthandler(void)
{
  uint16_t stat = zusb->stat;

  // USB接続状態が変化した
  if (stat & ZUSB_STAT_HOTPLUG) {
    DPRINTF("USB plug stat changed\r\n");
    hotplug = true;
    zusb->stat = ZUSB_STAT_HOTPLUG;
  }

#ifdef DEBUG_LINK_STATUS
  // ネットワークリンクの状態が変化した
  if (stat & ZUSB_STAT_PCOMPLETE(EP_INTR)) {
    static int prev;
    if (prev != zusbbuf[ZUSBBUF_INTR + 2]) {
      DPRINTF("Link stat changed: %02x\r\n", zusbbuf[ZUSBBUF_INTR + 2]);
      prev = zusbbuf[ZUSBBUF_INTR + 2];
    }
    zusb->stat = ZUSB_STAT_PCOMPLETE(EP_INTR);
    zusb_send_cmd(ZUSB_CMD_SUBMITXFER(EP_INTR));
  }
#endif

  // 受信パケットが到着した
  if (stat & ZUSB_STAT_PCOMPLETE(EP_RECV)) {
    ax_packet_header_t *hdr = (ax_packet_header_t *)&zusbbuf[ZUSBBUF_RECV];
    int len = zusb_bswap16(hdr->len);

#ifdef DEBUG_RECV_PACKET_DUMP
    DPRINTF("recv len=%d\r\n", len + 4);
    for (int i = 0; i < len + 4; i++) {
      char buf[20];
      if (i % 16 == 0) {
        DPRINTF("%04x:", i);
      }
      uint8_t byte = zusbbuf[ZUSBBUF_RECV + i];
        DPRINTF(" %02x", byte);
        buf[i % 16] = isprint(byte) ? byte : '.';
      if (i % 16 == 15) {
        buf[16] = '\0';
        DPRINTF("  %s\r\n", buf);
      }
    }
    if ((len + 4) % 16 != 0) {
      DPRINTF("\r\n");
    }
#endif

    int proto = *(uint16_t *)&zusbbuf[ZUSBBUF_RECV + 16];
    rcvhandler_t func = find_proto_handler(proto);
    if (func) {
      func(len, &zusbbuf[ZUSBBUF_RECVDATA], *(uint32_t *)regp->ifname);
    }

    zusb->stat = ZUSB_STAT_PCOMPLETE(EP_RECV);
    zusb_send_cmd(ZUSB_CMD_SUBMITXFER(EP_RECV));
  }
}

//****************************************************************************
// Device driver initialization
//****************************************************************************

static int etherinit(void)
{
  // 空いているtrap番号を探す
  regp->trapno = find_unused_trap(regp->trapno);
  if (regp->trapno < 0) {
    _dos_print("ネットワークインターフェースに使用するtrap番号が空いていません\r\n");
    return -1;
  }

  // インターフェース名を設定する
  memcpy(&devheader.name[5], regp->ifname, 3);

  if ((regp->ch = zusb_open_protected()) < 0) {
    _dos_print("ZUSB デバイスが見つかりません\r\n");
    return -1;
  }

  // チャネルが使用する割り込みベクタ番号を取得する
  zusb_send_cmd(ZUSB_CMD_GETIVECT);
  regp->ivect = zusb->param;

  int devid;
  if ((devid = zusb_find_device_with_vid_pid(regp->vid, regp->pid, 0)) <= 0) {
    _dos_print("USB LANアダプタが見つかりません\r\n");
    zusb_close();
    return -1;
  }

  // 見つかったデバイスのプロダクトIDを得る
  zusb_desc_device_t *ddev = (zusb_desc_device_t *)zusbbuf;
  if (zusb_get_descriptor(zusbbuf) > 0 &&
      ddev->bDescriptorType == ZUSB_DESC_DEVICE) {
    regp->iProduct = ddev->iProduct;
  }

  if (zusb_connect_device(devid, 1, 255, 255, 0, epcfg) <= 0) {
    _dos_print("USB LANアダプタに接続できません\r\n");
    zusb_close();
    return -1;
  }

  if (setjmp(jenv) != 0) {
    zusb_disconnect_device();
    zusb_close();
    _dos_print("デバイスエラーが発生しました\r\n");
    return -1;
  }

  ax_init();

  // 割り込みベクタを設定する
  regp->oldtrap = _dos_intvcs(0x20 + regp->trapno, trap_entry);
  regp->oldivaddr = _dos_intvcs(regp->ivect, inthandler_asm);

  _dos_print("USB LANアダプタ(");
  if (regp->iProduct) {
    char product[256];
    product[0] = '\0';
    zusb_get_string_descriptor(product, sizeof(product), regp->iProduct);
    _dos_print(product);
    _dos_putchar(' ');
  }
  {
    uint8_t mac[6];
    etherfunc(1, mac);
    for (int i = 0; i < 6; i++) {
      _dos_putchar("0123456789abcdef"[mac[i] >> 4]);
      _dos_putchar("0123456789abcdef"[mac[i] & 0xf]);
      if (i < 5) {
        _dos_putchar(':');
      }
    }
  }
  _dos_print(")が利用可能です\r\n");

  return 0;
}

static void etherfini(void)
{
  zusb_set_channel(regp->ch);
  ax_fini();
  zusb_disconnect_device();
  zusb_close();
}

// コマンドラインパラメータを解析する
static int parse_cmdline(char *p, int issys)
{
  _dos_print("X68000 Z USB Ethernet driver version " GIT_REPO_VERSION "\r\n");

  if (issys) {
    while (*p++ != '\0')  // デバイスドライバ名をスキップする
      ;
  } else {
    p++;                  // 文字数をスキップする
  }

  while (*p != '\0') {
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    if (*p == '/' || *p == '-') {
      p++;
      switch (tolower(*p++)) {
      case 't':
        char c = *p++;
        if (c >= '0' && c <= '7') {
          regp->trapno = c - '0';
        } else {
          return -1;
        }
        break;
      case 'r':
        flag_r = true;
        break;
      case 'i':
        regp->vid = hextoul(p, &p);
        if (*p++ != ':') {
          return -1;
        }
        regp->pid = hextoul(p, &p);
        break;
      default:
        return -1;
      }

      if (issys) {
        p += strlen(p) + 1;
      }
      continue;
    } else {
      return -1;
    }
  }

  return 0;
}

//****************************************************************************
// Program entry
//****************************************************************************

// CONFIG.SYSでの登録時 (デバイスドライバ インタラプトルーチン)
int interrupt(void)
{
  uint16_t err = 0;
  struct dos_req_header *req = reqheader;

  // Initialize以外はエラー
  if (req->command != 0x00) {
    return 0x700d;
  }

  _dos_print("\r\n");

  // パラメータを解析する
  if (parse_cmdline((char *)req->status, 1) < 0) {
    _dos_print("パラメータが不正です\r\n");
    return 0x700d;
  }

  if (etherinit() < 0) {
    return 0x700d;
  }

  extern char _end;
  req->addr = &_end;
  return 0;
}

// Xファイル実行時
void _start(void)
{
  char *cmdl;
  __asm__ volatile ("move.l %%a2,%0" : "=r"(cmdl)); // コマンドラインへのポインタ

  if (parse_cmdline(cmdl, 0) < 0) {
    _dos_print(
      "Usage: zusbether [Options]\r\n"
      "Options:\r\n"
      "  -t<trapno>\tネットワークインターフェースに使用するtrap番号を指定する(0~7)\r\n"
      "  -i<VID>:<PID>\t使用するUSB LANアダプタのVID:PIDを指定する (デフォルトは 0b95:7720)\r\n"
      "  -r\t\t常駐しているzusbetherドライバがあれば常駐解除する\r\n"
    );
    _dos_exit2(1);
  }

  _iocs_b_super(0);

  if (flag_r) {
    /*
     * 常駐解除処理
     */
    struct dos_dev_header *devh;
    if (!find_zusbether(&devh)) {
      _dos_print("ドライバは常駐していません\r\n");
      _dos_exit2(1);
    }

    struct dos_dev_header *olddev = devh->next;
    regp = ((struct regdata **)olddev->interrupt)[-1];

    if (!regp->removable) {
      _dos_print("CONFIG.SYSで登録されているため常駐解除できません\r\n");
      _dos_exit2(1);
    }

    if (regp->nproto > 0) {
      _dos_print("ネットワークインターフェースが使用中のため常駐解除できません\r\n");
      _dos_exit2(1);
    }

    // 動作中のドライバを停止する
    etherfini();

    // デバイスドライバのリンクを解除する
    devh->next = olddev->next;

    // 割り込みベクタを元に戻す
    _iocs_b_intvcs(regp->ivect, regp->oldivaddr);
    _iocs_b_intvcs(0x20 + regp->trapno, regp->oldtrap);
    _dos_mfree((void *)olddev - 0xf0);

    _dos_print("ドライバの常駐を解除しました\r\n");
    _dos_exit();
  }

  /*
   * 常駐処理
   */
  struct dos_dev_header *devh;
  if (find_zusbether(&devh)) {
    _dos_print("ドライバが既に常駐しています\r\n");
    _dos_exit2(1);
  }

  if (etherinit() < 0) {
    _dos_exit2(1);
  }

  // デバイスドライバのリンクを作成する
  devh->next = &devheader;
  regp->removable = 1;

  // 常駐終了する
  extern char _end;
  int size = (int)&_end - (int)&devheader;
  _dos_keeppr(size, 0);
}
