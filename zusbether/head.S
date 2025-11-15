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

/* Human68k device driver header */

    .text

    .extern reqheader
    .extern interrupt
    .extern etherfunc
    .extern inthandler
    .extern regdata

    .global devheader
devheader:
    .long   -1                  // link pointer
    .word   0x8000              // device type
    .long   strategy            // strategy routine entry point
    .long   interrupt_asm       // interrupt routine entry point
    .ascii  "/dev/en0"          // device driver name
    .ascii  "EthD"              // for etherlib.a
    .ascii  "Zeth"              // driver name

/* superjsr call entry */

    .global superjsr_entry
superjsr_entry:
    movem.l %d1-%d2/%a1-%a2,%sp@-
    movem.l %d0/%a0,%sp@-
    bsr     etherfunc
    addq.l  #8,%sp
    movem.l %sp@+,%d1-%d2/%a1-%a2
    rts

/* trap #n entry */

    .global trap_entry
trap_entry:
    movem.l %d1-%d2/%a1-%a2,%sp@-
    movem.l %d0/%a0,%sp@-
    bsr     etherfunc
    addq.l  #8,%sp
    movem.l %sp@+,%d1-%d2/%a1-%a2
    rte

/* interrupt handler entry */

    .global inthandler_asm
inthandler_asm:
    movem.l %d0-%d7/%a0-%a6,%sp@-
    bsr     inthandler
    movem.l %sp@+,%d0-%d7/%a0-%a6
    rte

/* device driver entry */

strategy:
    move.l  %a5,reqheader
    rts

    .long   regdata

interrupt_asm:
    movem.l %d1-%d2/%a0-%a2/%a5,%sp@-
    bsr     interrupt
    movea.l reqheader,%a5
    ror.w   #8,%d0
    move.b  %d0,%a5@(4)             // errh エラーコードを設定
    rol.w   #8,%d0
    move.b  %d0,%a5@(3)             // errl エラーコードを設定
    movem.l %sp@+,%d1-%d2/%a0-%a2/%a5
    rts

    .end
