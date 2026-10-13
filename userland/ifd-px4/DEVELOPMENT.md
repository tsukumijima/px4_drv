# 内蔵カードリーダー用 IFD ハンドラー (Linux) の開発者向け資料

この文書は、Linux 版の内蔵カードリーダー対応の構成、カーネルとユーザー空間の取り決め、試験の方法をまとめたものです。  
導入と設定は [利用者向けの README](README.md) を参照してください。

## 構成

```text
アプリケーション → libpcsclite → pcscd → libifd-px4.so (IFD ハンドラー)
                                              ↓ ioctl
                                        /dev/px4cardN
                                              ↓
                       px4_card.c (機種共通層) → it930x_bcas_*() → IT930x の UART → カード
```

カーネル側は、カードの検出・電源・リセット・UART の送受信と、デバイスの寿命・排他・チューナーと共有する基板電源だけを担当します。  
ATR の解析と T=1 (再送・WTX・APDU の分割・APDU 全体の期限) はユーザー空間の IFD ハンドラーが担当します。  
T=1 の待機をカーネルへ置くと、TS 受信と共有するロックを長く握ることになり、試験も実機に頼ることになるためです。

T=1 の実装は、Windows 版と同じ [`smart_card.cpp`](../../winusb/src/DriverHost_PX4/smart_card.cpp) をそのままビルドして使います。  
状態遷移の試験 ([`smart_card_state_test.cpp`](../../winusb/tests/smart_card_state_test.cpp)) も共有しているので、T=1 の修正は Windows 版と Linux 版の両方へ同時に反映されます。  
Linux 固有の部分は、ioctl を発行する薄いアダプター ([`linux_card_device.cpp`](linux_card_device.cpp)) と、pcsc-lite の IFD ハンドラーの関数 ([`ifdhandler.cpp`](ifdhandler.cpp)) だけです。

### カーネルの機種共通層と機種側の分担

ioctl のコマンドの振り分けと `it930x_bcas_*()` の呼び出しは、機種共通層の [`px4_card.c`](../../driver/px4_card.c) にまとめています。  
各機種は `struct px4_card_backend` で次の4つだけを渡します。

| 項目 | 内容 |
|---|---|
| `it930x` | カードの UART とカード検出の GPIO を持つ IT930x のブリッジ |
| `lock` | チューナーの制御と共有する機種のロック (カードの操作中は保持し続ける) |
| `available` | USB 切断で 0 になる機種の利用可能状態 |
| `set_power` | チューナーと共有する基板電源の、カード側の参照を取得・解放する関数 |

カードリーダーを登録する機種と条件は、Windows 版の `HasCardReader()` に合わせています。  
PX4 系のうち、2つの USB デバイスで1つの筐体を構成する機種 (PX-Q3U4・PX-Q3PE4・PX-Q3PE5) は、`dev_id` が 1 の側だけに登録します。  
PXMLT 系は PX-MLT8PE の3チューナー側 (`PXMLT8PE3`) を除くすべて、ISDB2056 系と ISDBT2071 系はすべての機種に登録します。

### 基板電源の取り決め

カードはチューナーと同じ基板電源の利用者として数え、最後の利用者が閉じたときだけ電源を切ります。

- カードを開いたままチューナーを閉じても電源は切らず、チューナーが受信中にカードを閉じても切らない
- チューナーを開くときにカードが電源を使っていれば、電源は入れ直さずに受信回路の初期化だけを行う
- 停止の要求は、ハードウェア操作の成否にかかわらず停止済みとして記録し、投入は成功したときだけ記録する
- PX4 系の2基連動モデルでは、`px4_mldev` が Windows 版の `MultiDevice::SetCardPower()` と同じく、カードの参照も含めて両方の基板の電源を決める

カードの検出 (`PX4_CARD_DETECT`) は電源を入れずに行えます。  
pcscd はカードの有無を定期的に問い合わせ続けるため、検出だけで電源の参照を取ると、何も受信していなくても基板の電源が切れなくなるためです。

### USB 切断とデバイスの寿命

USB が切断されると、機種側は機種のロックの下で利用可能状態を 0 にしてから、`px4_card_unregister()` でカードリーダーの登録を外します。  
登録の解除は、カードが使っていた電源の参照を戻し (切断後なのでハードウェアには触れません)、以後の操作がすべて `-ENODEV` になるようにします。  
開いたままのファイル記述子は機種共通層の小さな管理オブジェクトだけを参照し続けるため、機種のオブジェクトは pcscd が記述子を閉じるのを待たずに解放できます。

## ioctl の仕様

定義は [`include/px4_card_ioctl.h`](../../include/px4_card_ioctl.h) にあります。  
32bit のユーザー空間からも同じ配置で使えるよう、構造体は固定幅の型だけで構成しています。  
IFD ハンドラーは、開いた直後に `PX4_CARD_GET_INFO` で ABI の版と1回に送受信できる長さを確かめます。

`/dev/px4cardN` は1つのプロセスしか開けず、2つ目の open は `EBUSY` になります。  
T=1 の連番と再送の状態は1つのセッションだけが持てるため、複数のアプリケーションからの共有は pcscd が受け持ちます。  
open しただけでは電源を入れず、最後の記述子を閉じたときにカードの電源の参照も解放します。

| コマンド | 内容 |
|---|---|
| `PX4_CARD_GET_INFO` | ABI の版 (1) と、1回に送受信できる長さ (255 バイト) を返す |
| `PX4_CARD_POWER` | `value` が 1 なら電源の参照を取って UART を初期化し、0 なら参照を解放する |
| `PX4_CARD_DETECT` | カードの有無を `value` に返す (電源を入れていなくても使える) |
| `PX4_CARD_RESET` | カードをリセットし、UART を ATR の受信速度 (9600bps) へ戻す |
| `PX4_CARD_SET_BAUDRATE` | `value` に 9600、19200、38400 のいずれかを渡す |
| `PX4_CARD_RX_READY` | カードの応答を受信し終えたかを `value` に返す |
| `PX4_CARD_READ` | `length` に受信できる最大長を渡し、実際の受信長とデータを返す |
| `PX4_CARD_WRITE` | `data` の先頭から `length` バイトを1つの UART フレームとして送信する |

使わない `value`・`length` と `reserved` には 0 を入れます。  
電源を入れていない状態で UART を操作するコマンドを送ると `EHOSTDOWN`、USB 切断後は `ENODEV` を返します。

APDU 全体の期限は、Windows 版と同じく `smart_card.cpp` が操作の合間に検査するので、ioctl の引数は期限を持っていません。  
1回の ioctl は、既存の USB 制御転送のタイムアウト (モジュールのパラメーター `ctrl_timeout` で、初期値は 3000ms) で打ち切られます。

## ビルドと自動試験

カーネルモジュールと IFD ハンドラーは、リポジトリのルートから次のようにビルドします。

```bash
make -C driver
make -C userland/ifd-px4
make -C userland/ifd-px4 check
```

`make -C userland/ifd-px4 check` は、次の2つの試験を実行します。

- Windows 版と共有する T=1 の状態遷移の試験
- IFD ハンドラーの関数から `LinuxCardDevice` までを実物で通し、open・close・ioctl のシステムコールだけを模擬する試験 (未挿入、電源、T=1、抜去、複数のカードリーダーの区別、ABI の不一致など)

実際の pcscd と libpcsclite を使う統合試験は、ホストの pcscd へ接続しないよう、試験専用のコンテナの中で実行します。  
USB デバイスとホストの PC/SC のソケットを渡さず、pcscd を起動していないコンテナの root で、次を実行します。

```bash
cd userland/ifd-px4
make integration-build
PX4_ISOLATED_TEST=1 sh tests/run-pcsc-integration.sh
```

`libifd-px4-test.so` は模擬のカードを使う統合試験専用で、実際の環境へ配置するのは `libifd-px4.so` です。

## 実機での確認

自動試験はカードとの実際の通信、TS 受信への影響、電源の遷移を確かめられないため、変更したときは実機で次を確認します。

- カードだけを使ったときのカードリーダーの列挙、ATR、APDU
- チューナーとカードの同時使用 (どちらを先に閉じても他方が使い続けられること)
- 最後の利用者が閉じたときだけ基板の電源が切れること (`px4_backend_set_power` などの dynamic debug で遷移を見られる)
- 通信中の USB 切断と再接続、通信中のカードの抜去と再挿入
- 外付けのカードリーダーとの共存

これまでの確認結果と、確認できていない機種・条件は [実機確認の記録](../../docs/20261014_linux_card_reader_validation.md) にまとめています。
