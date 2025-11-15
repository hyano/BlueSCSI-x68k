# X68000 Z USB LAN アダプタドライバ zusbether.x

## 概要

X68000 Z に接続した USB LAN アダプタを利用するためのデバイスドライバです。

無償公開されている (株)計測技研製 Human68k 用 TCP/IP ドライバ [TCPPACKA](http://retropc.net/x68000/software/internet/kg/tcppacka/) を用いることで、X68000 Z をネットワークに接続できるようになります。


## 対応 USB LAN アダプタ

このドライバは ASIX AX88772 チップを用いた USB LAN アダプタに対応しています。
動作確認は以下のデバイスでのみ行っていますが、同じコントローラを用いた他の USB LAN アダプタでも使えるかも知れません。

* [LUA3-U2-ATX (BUFFALO)](https://www.buffalo.jp/product/detail/lua3-u2-atx.html) 


## 使用方法

コマンドラインから以下のように実行します。

```
zusbether.x <オプション>...
```

または CONFIG.SYS に以下のように指定することで、起動時に組み込むこともできます。

```
DEVICE = zusbether.x <オプション>...
```

以下の `<オプション>` を指定できます。

* `/t<trap no>`\
  ドライバが使用する trap 番号を 0 から 7 の値で指定します。デフォルトでは trap #0 から順番に未使用の trap 番号を検索し、空いているものを使用します。
* `/i<VID>:<PID>`\
  通常は LUA3-U2-ATX (VID=0b95, PID=7720) に対してのみ接続しようとしますが、このオプションによって指定した VID, PID の USB デバイスへの接続を試みます。
  コントローラチップに AX88772 を用いた他の USB LAN アダプタが使えるかも知れません。
* `/r`\
  常駐している zusbether.x を常駐解除します。CONFIG.SYS で登録されたドライバに対しては使用できません。

正常に組み込まれると、以下のようなメッセージが表示されて TCP/IP ドライバから LAN アダプタが利用可能になります (xx:xx:xx:xx:xx:xx は認識した USB LAN アダプタの MAC アドレスです)。

```
X68000 Z USB Ethernet driver version xxxxxxxx
USB LANアダプタ(AX88772  xx:xx:xx:xx:xx:xx)が利用可能です
```


## TCP/IP ドライバの使用方法

TCP/IP ドライバ [TCPPACKA](http://retropc.net/x68000/software/internet/kg/tcppacka/) の使用方法はアーカイブに含まれているドキュメントに記述されていますが、TCP/IP が使えるようになるまでの手順を簡単に説明します。

1. CONFIG.SYS の設定

    起動ドライブの CONFIG.SYS に以下の記述を追加してバックグラウンド処理を有効にします(既にPROCESS=行がある場合には追加は不要です)。

    ```
    PROCESS = 3 10 10
    ```

2. ドライバの組み込み

    zusbether.x と、TCPPACKA に含まれる inetd.x を実行して組み込みます。

    ```
    A> zusbether
    X68000 Z USB Ethernet driver version xxxxxxxx
    USB LANアダプタ(AX88772  xx:xx:xx:xx:xx:xx)が利用可能です
    A> inetd
    TCP/IP Driver version 1.20 Copyright (C) 1994,1995 First Class Technology.
    ```

3. ネットワーク設定

    X68000 Z に IP アドレス等のネットワーク設定を行います。例として、

    * IP アドレス : 192.168.1.8
    * ネットマスク : 255.255.255.0

    この場合の設定は以下のようになります。

    ```
    A> ifconfig lp0 up
    A> ifconfig en0 192.168.1.8 netmask 255.255.255.0 up
    ```

    (zusbether.x のネットワークインターフェース名は **en0** になります。TCPPACKA のドキュメントとは異なりますので注意してください)

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


## 謝辞

ネットワークドライバの実装方法に関しては、[Neptune-X 用ドライバ ether_ne.sys](http://retropc.net/x68000/software/hardware/neptune_x/ndrv/) (Shi-MAD 氏作) のソースコードを参考にさせていただきました。感謝します。
