# X68000 BlueSCSI V2 関連ツール

## 概要

本リポジトリは X68000 で BlueSCSI V2 を使うためのツール類です。

BlueSCSI is hardware that reproduces SCSI devices (hard disks, optical, etc) with an Pico 2040 dev board.
[BlueSCSI V2](https://github.com/BlueSCSI/BlueSCSI-v2) は、Raspberry Pi Pico/Pico2を使って、
ハードディスクなどのSCSIデバイスを再現するハードウェアです。

## 関連ツール

* [dyptether - DaynaPORT LAN アダプタドライバ](dyptether/README.md)

## ビルド方法

サンプルコードのビルドには [elf2x68k](https://github.com/yunkya2/elf2x68k) が必要です。
リポジトリのトップディレクトリ内で `make` を実行するとビルドできます。

## ライセンス

本リポジトリに含まれるソースコードはすべて MIT ライセンスとします。
