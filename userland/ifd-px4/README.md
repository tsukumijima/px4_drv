# 内蔵カードリーダー用 IFD ハンドラー (Linux)

チューナーに内蔵された B-CAS カードリーダーを、pcsc-lite (pcscd) を通して PC/SC 対応のアプリケーションから使うためのドライバーです。  
pcscd が USB の接続を検出するとこのドライバーを読み込み、内蔵カードリーダーが外付けのカードリーダーと同じように一覧へ並びます。  
外付けのカードリーダーは従来どおり `libccid` で使えるため、どちらのカードリーダーを使うかはアプリケーション側で選びます。

## 対応機種

内蔵カードリーダーを持つ次の機種に対応しています。

| 系統 | 機種 |
|---|---|
| PX4 系 | PLEX PX-W3U4、PX-Q3U4、PX-W3PE4、PX-Q3PE4、PX-W3PE5、PX-Q3PE5 |
| PXMLT 系 | PLEX PX-MLT5U、PX-MLT5PE、PX-MLT8PE、e-Better DTV02A-5TS-P、DTV02A-4TS-P |
| ISDB2056 系 | e-Better DTV02A-1T1S-U、PLEX PX-M1UR |
| ISDBT2071 系 | PLEX PX-S1UR、e-Better DTV03A-1TU |

PX-Q3U4・PX-Q3PE4・PX-Q3PE5 と PX-MLT8PE は、1つの筐体が2つの USB デバイスとして見える機種で、カードスロットがあるのは片方だけです。  
カードスロットのない側の USB デバイスでも pcscd がこのドライバーを読み込みますが、カードリーダーは見つからないまま終わります (接続時に3秒ほどかかります)。

実機で動作を確かめた機種と条件は、[実機確認の記録](../../docs/20261014_linux_card_reader_validation.md)にまとめています。

## 導入

内蔵カードリーダーを使うには、カードリーダーに対応した版のカーネルモジュールと、このドライバー、pcscd が必要です。  
カーネルモジュールと udev 規則は、[Linux の導入手順](../../README.md#インストール-linux)で導入されます。

このドライバーのビルドには、C++17 に対応したコンパイラー、make、pkg-config、pcsc-lite の開発用ファイルが必要です。  
Debian / Ubuntu では次のパッケージで揃います。

```bash
sudo apt install build-essential pkg-config libpcsclite-dev pcscd
```

リポジトリのルートでビルドし、pcsc-lite のドライバーの配置先へインストールします。

```bash
make -C userland/ifd-px4
sudo make -C userland/ifd-px4 install
```

配置先は `pkg-config --variable=usbdropdir libpcsclite` で決まり、次の2つのファイルが置かれます。

```text
<配置先>/ifd-px4.bundle/Contents/Info.plist
<配置先>/ifd-px4.bundle/Contents/Linux/libifd-px4.so
```

導入や更新の後は、pcscd を再起動すると読み込まれます。  
カードを使っている最中に再起動すると復号が途切れるため、録画や視聴をしていないときに実行してください。

```bash
sudo systemctl restart pcscd
```

## Docker で pcscd を動かす場合

カーネルモジュールはホストへ、このドライバーと pcscd はコンテナへ導入します。  
コンテナの中でも上と同じ手順でビルドとインストールができます。

コンテナには、チューナーのデバイスファイルに加えて、カードリーダーのデバイスファイル (`/dev/px4cardN`) を渡します。

```yaml
devices:
  - /dev/px4card0:/dev/px4card0
```

pcscd は、USB デバイスと `/sys/class/misc/px4cardN` の対応を sysfs でたどってカードリーダーを見つけるため、コンテナからホストの sysfs と USB デバイスの情報が見える必要があります。  
USB を抜き差しするとカードリーダーの番号が変わることがあるので、そのときはコンテナへ渡すデバイスファイルも見直してください。

## 認識の確認

`pcsc-tools` の `pcsc_scan` で、カードリーダーとカードを認識しているかを確かめられます。

```bash
sudo apt install pcsc-tools
pcsc_scan
```

`PLEX PX-W3U4 (シリアル番号) 00 00` のように、機種名の付いたカードリーダーが表示されます。  
表示されない場合は、次の順に確かめてください。

- ホストに `/dev/px4cardN` があるか (ない場合は、カーネルモジュールがカードリーダーに対応した版かを確かめます)
- pcscd を動かす環境にこのドライバーを導入し、その後で pcscd を再起動したか
- Docker で動かす場合は、コンテナへカードリーダーのデバイスファイルを渡し、sysfs が見えるようになっているか
- pcscd を root 以外で動かす場合は、そのユーザーが `/dev/px4cardN` を開けるか (権限は `root:video` の `0660`)

## 設定ファイルで固定する場合

通常は pcscd が USB の接続を検出して自動で読み込むため、設定ファイルは不要です。  
USB の検出を使えない環境では、`/etc/reader.conf.d/` に次のような設定を置いて固定できます。

```text
FRIENDLYNAME "PX4 internal card reader"
DEVICENAME /dev/px4card0
LIBPATH /usr/lib/pcsc/drivers/ifd-px4.bundle/Contents/Linux/libifd-px4.so
CHANNELID 0
```

同じカードリーダーを USB の検出と設定ファイルの両方で登録すると、片方がデバイスファイルを開けずに失敗します。  
udev 規則は、接続位置から作ったリンク (`/dev/px4card/by-path/`) も作るので、固定する場合はそちらを指定すると USB の差し直しで番号が変わっても追従できます。

開発者向けの構成・ioctl の仕様・試験は、[開発者向けの文書](DEVELOPMENT.md)を参照してください。
