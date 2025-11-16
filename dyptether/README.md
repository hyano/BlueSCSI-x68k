# X68000 DaynaPORT LAN アダプタドライバ dyptether.x

## 概要

X68000 の SCSI ポートに接続した DaynaPORT デバイスを LAN アダプタとして利用するためのデバイスドライバです。

無償公開されている (株)計測技研製 Human68k 用 TCP/IP ドライバ [TCPPACKA](http://retropc.net/x68000/software/internet/kg/tcppacka/) を用いることで、X68000 をネットワークに接続できるようになります。


## 対応 SCSI デバイス

動作確認は以下のデバイスでのみ行っていますが、DanyaPORT のエミュレーション機能を備える他の SCSI 機器でも使えるかも知れません。

* [BlueSCSI V2](https://github.com/BlueSCSI/BlueSCSI-v2) 


## 使用方法

コマンドラインから以下のように実行します。

```
dyptether.x <オプション>...
```

または CONFIG.SYS に以下のように指定することで、起動時に組み込むこともできます。

```
DEVICE = dyptether.x <オプション>...
```

以下の `<オプション>` を指定できます。

* `/t<trap no>`\
  ドライバが使用する trap 番号を 0 から 7 の値で指定します。デフォルトでは trap #0 から順番に未使用の trap 番号を検索し、空いているものを使用します。
* `/d<scsi id>`\
  DaynaPORT の SCSI ID を 0 から 7 の値で指定します。デフォルトでは 7 から 0 順番に SCSI 機器を検索し、最初に見つけた DaynaPORT デバイスを使用します。
* `/r`\
  常駐している dyptether.x を常駐解除します。CONFIG.SYS で登録されたドライバに対しては使用できません。

正常に組み込まれると、以下のようなメッセージが表示されて TCP/IP ドライバから LAN アダプタが利用可能になります (xx:xx:xx:xx:xx:xx は認識した DaynaPORT の MAC アドレスです)。

```
X68000 DaynaPORT Ethernet driver version xxxxxxxx
DaynaPORT が利用可能です
  SCSI ID  : X
  VENDOR   : Dayna
  PRODUCT  : SCSI/Link
  MAC ADDR : xx:xx:xx:xx:xx:xx
常駐します
```


## TCP/IP ドライバの使用方法

TCP/IP ドライバ [TCPPACKA](http://retropc.net/x68000/software/internet/kg/tcppacka/) の使用方法はアーカイブに含まれているドキュメントに記述されていますが、TCP/IP が使えるようになるまでの手順を簡単に説明します。

1. CONFIG.SYS の設定

    起動ドライブの CONFIG.SYS に以下の記述を追加してバックグラウンド処理を有効にします(既にPROCESS=行がある場合には追加は不要です)。

    ```
    PROCESS = 3 10 10
    ```

2. ドライバの組み込み

    dyptether.x と、TCPPACKA に含まれる inetd.x を実行して組み込みます。

    ```
    A> dyptether
    X68000 DaynaPORT Ethernet driver version xxxxxxxx
    DaynaPORT が利用可能です
      SCSI ID  : X
      VENDOR   : Dayna
      PRODUCT  : SCSI/Link
      MAC ADDR : xx:xx:xx:xx:xx:xx
    常駐します
    A> inetd
    TCP/IP Driver version 1.20 Copyright (C) 1994,1995 First Class Technology.
    ```

3. ネットワーク設定

    X68000 に IP アドレス等のネットワーク設定を行います。例として、

    * IP アドレス : 192.168.1.8
    * ネットマスク : 255.255.255.0

    この場合の設定は以下のようになります。

    ```
    A> ifconfig lp0 up
    A> ifconfig en0 192.168.1.8 netmask 255.255.255.0 up
    ```

    (dyptether.x のネットワークインターフェース名は **en0** になります。TCPPACKA のドキュメントとは異なりますので注意してください)

    続いて、ネームサーバとデフォルトルートの設定を行います。

    * ネームサーバ : 192.168.1.1
    * デフォルトルート : 192.168.1.1

    この場合の設定は以下のようになります。

    ```
    A> inetdconf +dns 192.168.1.1 +router 192.168.1.1
    ```


## 制限事項

TCP/IP ドライバ用ネットワークドライバの機能のうち、以下のものは未実装です。
* マルチキャスト対応
* 統計情報読み出し

Ether パケットの受信には、暫定的に垂直同期割り込みを使用しています。
カウンタは4を指定しています。使用する割り込みを含め、試行錯誤する予定です。

## 謝辞

ネットワークドライバの実装は、[X68000 Z USB LAN アダプタドライバ zusbether.x](https://github.com/yunkya2/x68kz-zusb/tree/master/zusbether) (@yunkya2 氏作) をベースに改造しました。感謝します。

