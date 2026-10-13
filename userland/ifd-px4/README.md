# Linux 内蔵カードリーダー用 IFD ハンドラー

DTV02A-5TS-P / PX-MLT5PE の内蔵カードリーダーを、PC/SC 対応アプリケーションから利用するためのドライバーです。
外付け CCID リーダーは従来の `libccid` と併用でき、使用するリーダーはアプリケーション側で選択します。

## Debian パッケージでの導入

内蔵カードリーダーには、次のパッケージが必要です。

| パッケージ | 内容 | 配置先 |
|---|---|---|
| `px4-drv-dkms` | 内蔵カード対応カーネルモジュール、ファームウェア、udev 規則 | ホスト |
| `libifd-px4` | 内蔵カードリーダー用の PC/SC ドライバー | pcscd が動く環境 |
| `pcscd` | PC/SC の接続とカードの共有管理 | ホストまたはコンテナ |

内蔵カード対応版の `px4-drv-dkms` と、同じ版の `libifd-px4` を用意してください。
`libifd-px4` は CPU ごとのパッケージなので、利用先の `dpkg --print-architecture` と一致するファイルを選びます。
配布版 `v0.6.1` には内蔵カード対応が含まれません。必要な配布物がない場合は、[ソースからの deb 生成手順](DEVELOPMENT.md#deb-パッケージの生成と配布)を参照してください。

ホストで pcscd を動かす場合は、利用する版・CPU の2つの deb だけを置いたディレクトリで実行します。

```sh
sudo apt install ./px4-drv-dkms_*_all.deb ./libifd-px4_*.deb pcscd
```

カーネルモジュールの導入・入れ替えは [Linux の導入手順](../../README.md#インストール-linux)を参照してください。
IFD の導入・更新後は pcscd の再起動が必要です。カード利用中の再起動は復号を中断するため、録画・視聴を停止できる保守時間に実行してください。

```sh
sudo systemctl restart pcscd
```

## Docker で pcscd を動かしている場合

`px4-drv-dkms` はホストへ、`libifd-px4` と `pcscd` はコンテナへ配置します。
Debian / Ubuntu ベースのコンテナでは、利用する CPU 向けの IFD deb を1つだけビルドコンテキストへ置き、Dockerfile へ次を追加できます。

```dockerfile
COPY libifd-px4_*.deb /tmp/ifd-package/
RUN apt-get update \
    && apt-get install -y --no-install-recommends pcscd /tmp/ifd-package/*.deb \
    && rm -rf /tmp/ifd-package /var/lib/apt/lists/*
```

pcscd の起動はコンテナの既存の起動処理で行ってください。
手動配置した IFD を同じパスへ bind mount している場合は、そのマウントがイメージ内の deb の内容を隠すため、パッケージ導入へ移行する際に外す必要があります。
コンテナには既存のチューナーデバイスに加えて、カードデバイスへのアクセスを渡します。

```yaml
devices:
  - /dev/px4card0:/dev/px4card0
```

これは追加する設定の抜粋です。番号は対象機器の sysfs 親情報と照合してください。
USB の再接続で minor 番号やカードデバイス名が変わる場合は、Docker のデバイス割り当ても見直す必要があります。
コンテナ内の USB 自動検出には、親 USB 機器と `/sys/class/misc/px4cardN/device` が同じ機器として参照できる sysfs の可視性が必要です。
設定の反映には、録画・視聴を停止できる保守時間にイメージを再ビルドし、コンテナを再作成してください。

## リーダーの認識確認

カードを挿入し、pcscd が動く環境で `pcsc-tools` の `pcsc_scan` を実行すると、リーダーとカードの認識状態を確認できます。
リーダー名には `e-Better DTV02A-5TS-P` または `PLEX PX-MLT5PE` が表示されます。確認後は Ctrl+C で終了します。

```sh
sudo apt install pcsc-tools
pcsc_scan
```

リーダーが表示されない場合は、次を確認してください。

- ホストに `/dev/px4cardN` があること。存在しない場合は、機種とロード中のカーネルモジュールが内蔵カード対応かを確認します。
- pcscd が動く環境に IFD が導入され、導入後に pcscd を再起動していること。
- Docker では、コンテナからカードデバイスと USB・sysfs の情報を参照できること。
- pcscd を非 root で起動する場合は、そのサービスの利用者がカードデバイスを開けること。デバイスの権限は `root:video`、`0660` です。

## 固定設定が必要な場合

通常は libudev を有効にした pcsc-lite による USB 自動検出を利用できます。
固定設定が必要な環境では、`reader.conf.d` へ次のような設定を配置できます。
`LIBPATH` は実際の配置先へ合わせてください。USB 自動検出と固定設定を同じリーダーへ重複適用すると、排他的 open が衝突します。

```text
FRIENDLYNAME "DTV02A-5TS-P internal reader"
DEVICENAME /dev/px4card0
LIBPATH /usr/lib/pcsc/drivers/ifd-px4.bundle/Contents/Linux/libifd-px4.so
CHANNELID 0
```

udev 規則は、`ID_PATH` が得られる場合に `/dev/px4card/by-path/` に接続位置からのリンクを作成します。
固定設定のリーダーを USB 切断後に再登録する方法は pcsc-lite の設定再読み込み方式に依存するため、ホットプラグには USB 自動検出を優先してください。

## 確認範囲と制限

DTV02A-5TS-P の内蔵カードを使い、Mirakurun で地上波・BS を同時に30秒間受信・復号した試験では、同期エラー・TEI・連続性エラー・スクランブル残りはすべて0でした（2026-10-13）。
長時間録画、カード挿抜、USB の抜き差し、複数台や PX-MLT5PE での動作は未確認です。条件と結果は [実機確認記録](DEVELOPMENT.md#mirakurun-での同時復号確認-2026-10-13)を参照してください。

実装の構成、ソースからのビルド、ABI、自動試験については [開発者向け文書](DEVELOPMENT.md)にまとめています。
