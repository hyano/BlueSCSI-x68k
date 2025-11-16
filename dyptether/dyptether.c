/*
 * Copyright (c) 2025 Hirokuni Yano (@hyano)
 *
 * Based on zusbether.x
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

#include "daynaport.h"
#include "dyptether.h"

//****************************************************************************
// Definition
//****************************************************************************

// dyptbuf usage (0x000 - 0xf80)
#define DYPTBUF_TEMP        0x000   // 0x000 - 0x007
#define DYPTBUF_SEND        0x010   // 0x010 - 0x77f
#define DYPTBUF_SENDDATA    0x010
#define DYPTBUF_RECV        0x780   // 0x780 - 0xf7f
#define DYPTBUF_RECVDATA    0x786

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
  int target;       // SCSIターゲットID
  int nproto;       // このインターフェースを使用するプロトコル数
} regdata = {
  .ifname = "en0",
  .trapno = 0,
  .nproto = 0,

  .target = -1,
};

struct regdata *regp = &regdata;
extern struct dos_dev_header devheader;
extern void trap_entry(void);
extern void inthandler_asm(void);

//****************************************************************************
// Static variables
//****************************************************************************

static jmp_buf jenv;                      // DaynaPORT通信エラー時のジャンプ先
static int inrecovery = false;            // 通信エラー回復中
static int hotplug = false;               // USB接続状態が変化した
static int sentpacket = false;            // 送信済みパケットがある
static int flag_r = false;                // 常駐解除フラグ

#define N_PROTO_HANDLER   8
static struct {
  int proto;
  rcvhandler_t func;
} proto_handler[N_PROTO_HANDLER];

static uint8_t dyptbuf[0x1000];

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

// dyptetherが常駐しているかどうかを調べる
static int find_dyptether(struct dos_dev_header **res)
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
        memcmp(p + 8, "EthDDyPT", 8) == 0) {
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

//****************************************************************************
// Ether driver command handler
//****************************************************************************

int etherfunc(int cmd, void *args)
{
  int retry = false;
  DPRINTF("etherfunc:%d %p\r\n", cmd, args);

  if (setjmp(jenv) != 0) {
    DPRINTF("etherfunc error\r\n");
    retry = true;
    inrecovery = true;
    hotplug = false;
  }

  if (inrecovery) {
    if (!retry && !hotplug) {
      return -1;
    }

    DPRINTF("error recovery\r\n");
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
    dp_stat(6, regp->target, &dyptbuf[DYPTBUF_TEMP]);
    memcpy(args, &dyptbuf[DYPTBUF_TEMP], 6);
    return (int)args;

  // command 2: Get PROM addr
  case 2:
    dp_stat(6, regp->target, &dyptbuf[DYPTBUF_TEMP]);
    memcpy(args, &dyptbuf[DYPTBUF_TEMP], 6);
    return (int)args;

  // command 3: Set MAC addr
  case 3:
    return 0;

  // command 4: Send ether packet
  case 4:
  {
    struct {
      int size;
      uint8_t *buf;
    } *sendpkt = args;
    int len = sendpkt->size;
    memcpy(&dyptbuf[DYPTBUF_SENDDATA], sendpkt->buf, sendpkt->size);
    if (dp_send(len, regp->target, &dyptbuf[DYPTBUF_SENDDATA]) != 0)
    {
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
  uint16_t sr;
  if (dp_is_in_iocs()) return;

  sr = dp_irq_disable();
  if (dp_is_free())
  {
    dp_recv(0x600, regp->target, &dyptbuf[DYPTBUF_RECV]);
    int len = (dyptbuf[DYPTBUF_RECV+0] << 8) | dyptbuf[DYPTBUF_RECV+1];

    if (len >= 14 + 4)
    {
      int proto = *(uint16_t *)&dyptbuf[DYPTBUF_RECVDATA + 12];
      rcvhandler_t func = find_proto_handler(proto);
      if (func) {
        func(len - 4, &dyptbuf[DYPTBUF_RECVDATA], *(uint32_t *)regp->ifname);
      }
    }
  }
  dp_irq_enable(sr);
}

//****************************************************************************
// Device driver initialization
//****************************************************************************

static int etherinit(void)
{
  static struct dp_inquiry_data inquiry;

  // 空いているtrap番号を探す
  regp->trapno = find_unused_trap(regp->trapno);
  if (regp->trapno < 0) {
    _dos_print("ネットワークインターフェースに使用するtrap番号が空いていません\r\n");
    return -1;
  }

  // インターフェース名を設定する
  memcpy(&devheader.name[5], regp->ifname, 3);

  for (int target = 7; target >= 0; target--)
  {
    if (dp_inquiry(target, &inquiry) == 0)
    {
      if (dp_is_daynaport(&inquiry))
      {
        /* DaynaPORTデバイスを見つけた */
        regp->target = target;
        break;
      }
    }
  }
  if (regp->target < 0)
  {
    _dos_print("DaynaPORT デバイスが見つかりません\r\n");
    return -1;
  }

  if (dp_enable(regp->target, true) != 0)
  {
    _dos_print("DaynaPORT デバイスを初期化できませんでした\r\n");
    return -1;
  }

  if (setjmp(jenv) != 0) {
    dp_enable(regp->target, false);
    _dos_print("デバイスエラーが発生しました\r\n");
    return -1;
  }

  // 割り込みベクタを設定する
  regp->oldtrap = _dos_intvcs(0x20 + regp->trapno, trap_entry);
  //regp->oldivaddr = _dos_intvcs(regp->ivect, inthandler_asm);
  _iocs_vdispst(inthandler_asm, 0, 4);

  if (dp_inquiry(regp->target, &inquiry) == 0)
  {
    _dos_print("DaynaPORT が利用可能です\r\n");
    _dos_print("  SCSI ID  : ");
    _dos_putchar("01234567"[regp->target & 0x07]);
    _dos_print("\r\n");

    {
      char vendor[sizeof(inquiry.vendor) + 1];
      memcpy(vendor, inquiry.vendor, sizeof(inquiry.vendor));
      vendor[sizeof(inquiry.vendor)] = '\0';
      _dos_print("  VENDOR   : ");
      _dos_print(vendor);
      _dos_print("\r\n");
    }
    {
      char product[sizeof(inquiry.product) + 1];
      memcpy(product, inquiry.product, sizeof(inquiry.product));
      product[sizeof(inquiry.product)] = '\0';
      _dos_print("  PRODUCT  : ");
      _dos_print(product);
      _dos_print("\r\n");
    }
    {
      uint8_t mac[6];
      etherfunc(1, mac);
      _dos_print("  MAC ADDR : ");
      for (int i = 0; i < 6; i++)
      {
        _dos_putchar("0123456789abcdef"[mac[i] >> 4]);
        _dos_putchar("0123456789abcdef"[mac[i] & 0xf]);
        if (i < 5)
        {
          _dos_putchar(':');
        }
      }
      _dos_print("\r\n");
    }
  }

  return 0;
}

static void etherfini(void)
{
  if (regp->target > 0)
  {
    dp_enable(regp->target, false);
  }
}

// コマンドラインパラメータを解析する
static int parse_cmdline(char *p, int issys)
{
  _dos_print("X68000 DynaPORT Ethernet driver version " GIT_REPO_VERSION "\r\n");

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
      case 'd':
        char d = *p++;
        if (d >= '0' && d <= '7') {
          regp->target = d - '0';
        } else {
          return -1;
        }
        break;
      case 'r':
        flag_r = true;
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
      "Usage: dyptether [Options]\r\n"
      "Options:\r\n"
      "  -t<trapno>\tネットワークインターフェースに使用するtrap番号を指定する(0~7)\r\n"
      "  -d<scsiid>\tDaynaPORTのSCSI IDを指定する(0~7)(デフォルトは7~0の順で検索)\r\n"
      "  -r\t\t常駐しているdyptetherドライバがあれば常駐解除する\r\n"
    );
    _dos_exit2(1);
  }

  _iocs_b_super(0);

  if (flag_r) {
    /*
     * 常駐解除処理
     */
    struct dos_dev_header *devh;
    if (!find_dyptether(&devh)) {
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
    //_iocs_b_intvcs(regp->ivect, regp->oldivaddr);
    _iocs_vdispst(0, 0, 0);
    _iocs_b_intvcs(0x20 + regp->trapno, regp->oldtrap);
    _dos_mfree((void *)olddev - 0xf0);

    _dos_print("ドライバの常駐を解除しました\r\n");
    _dos_exit();
  }

  /*
   * 常駐処理
   */
  struct dos_dev_header *devh;
  if (find_dyptether(&devh)) {
    _dos_print("ドライバが既に常駐しています\r\n");
    _dos_exit2(1);
  }

  if (etherinit() < 0) {
    _dos_exit2(1);
  }
  _dos_print("常駐します\r\n");

  // デバイスドライバのリンクを作成する
  devh->next = &devheader;
  regp->removable = 1;

  // 常駐終了する
  extern char _end;
  int size = (int)&_end - (int)&devheader;
  _dos_keeppr(size, 0);
}
