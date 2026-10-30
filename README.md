# px4_drv - Unofficial Linux / Windows (WinUSB) driver for PLEX PX4/PX5/PX-MLT series / e-Better DTV series ISDB-T/S/C receivers

PLEX や e-Better から発売された各種 ISDB-T/S/C チューナー向けの chardev 版非公式 Linux ドライバ / Windows (WinUSB) ドライバです。  
PLEX 社の [Webサイト](http://plex-net.co.jp) にて配布されている公式ドライバとは**別物**です。

## このフォークについて

本家 [nns779/px4_drv](https://github.com/nns779/px4_drv) は 2021 年以降メンテナンスされておらず、残念ながら [nns779](https://github.com/nns779) 氏自身もネット上から失踪されています。  
このフォークは、各々のフォークに散逸していた有用な変更を一つにまとめ、継続的にメンテナンスしていくことを目的としています。

### 変更点 (WinUSB 版)

動作確認は Windows 10 / Windows 11 (x64) で行っています。

#### 新デバイス対応・機能追加

- PX-Q3PE5 に対応し、inf ファイルを追加
- [hendecarows 氏のフォーク](https://github.com/hendecarows/px4_drv) での更新を取り込み、DTV02A-1T1S-U (ロット番号 2309 以降を含む) / DTV03A-1TU / PX-M1UR / PX-S1UR に対応 (v0.5.0 以降 / DTV03A-1TU のみ v0.5.2 以降)
- PX-MLT5PE のリブランド品である DTV02A-5TS-P (USB Product ID: 0x924e) に対応し、inf ファイルを追加 (v0.6.1 以降)
- 各機種の内蔵 B-CAS カードリーダーを利用するためのドライバを追加 (v0.6.0 以降)
  - 同梱の `WinSCard.dll` を利用するアプリケーションの実行ファイルと同じフォルダに配置すると、内蔵カードリーダーを優先して利用できるようになる
  - Windows が認識している外付けカードリーダーも引き続き利用できる
- PX-MLT 系チューナー (PX-MLT5PE / PX-MLT8PE / DTV02A-4TS-P / DTV02A-5TS-P) において、ケーブルテレビ (トランスモジュレーション方式: ISDB-C) とスカパー！プレミアムサービス光 (J.83 Annex B) の受信に対応 (v0.7.0 以降)
  - [n0atl 氏のフォーク](https://github.com/n0atl/px4_drv) と [aoiro-labo 氏のフォーク](https://github.com/aoiro-labo/px4_drv) で進められていた対応を基にしている
  - 受信方法は [ケーブルテレビ (ISDB-C) / スカパー！プレミアムサービス光 (J.83B) の受信](#ケーブルテレビ-isdb-c--スカパープレミアムサービス光-j83b-の受信) を参照のこと

#### 設定ファイル・ドライバ構成ファイルの改善

- inf ファイルをより分かりやすい名前に変更
- inf ファイルに自己署名を行い、自己署名証明書のインストールだけで正式なドライバーと認識されるように
  - 以前は署名がないためインストール時にドライバー署名の強制の無効化が必要だったが、事前に自己署名証明書を適切な証明書ストアにインストールしておけばこの作業が不要になる
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1577466040/104-108
- 自己署名証明書のインストール・アンインストールスクリプトを追加
  - 拡張子が .jse となっているが、これは PowerShell スクリプトにダブルクリックで実行させるための JScript コードを先頭の行に加えたもの
- inf ファイルを ARM 版 Windows でもインストールできるようにする
  - 実機がないので試せていないけど、おそらくインストールできるはず
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1625673548/762
- 地上波の ChSet に物理 53ch ～ 62ch の定義を追加
  - 物理 53ch ～ 62ch は地上波の割り当て周波数から削除されているが、現在も ”イッツコムch10” など、一部ケーブルテレビの自主放送の割り当て周波数として使われている
- BS/CS の ChSet に2022年3月開局の BS 新チャンネル（BS松竹東急・BSJapanext・BSよしもと）の定義を追加
- BS/CS の ChSet で、BS23/TS1 と BS23/TS3 の TSID が入れ替わっていたのを修正
- `BonDriver_PX-MLT.ini` に、LNB 給電を有効にするための `LNBPower` の設定を追記
- inf ファイルを改善し、チューナーをタスクトレイの「ハードウェアの安全な取り外し」に表示しないようにする (v0.4.3 以降)
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1666581918/207
- BS/CS の ChSet に2024年10月～2025年1月に行われた BS トランスポンダ再編後の物理チャンネル情報を反映 (v0.5.2 以降)

#### ドライバの品質改善

- エラー発生時の MessageBox を表示しない設定を追加
  - BonDriver の ini 内の `DisplayErrorMessage` を 1 に設定すると今まで通り MessageBox が表示される
- バージョン情報が DLL のプロパティに表示されないのを修正
- https://github.com/tsukumijima/px4_drv/pull/2 をマージし、PX4 系チューナーで地上波の受信機を開く順番によってドロップが発生することがある問題を修正
- PX4 系チューナーを複数台接続したとき、使っていない方のチューナーの基板にも電源が入ったままになる問題を修正 (v0.6.0 以降)
  - ref: https://github.com/tsukumijima/px4_drv/issues/36
- 受信機の割り当て処理の不具合により、空いている受信機があっても `no receivers available` のエラーで選局できなくなることがある問題を修正 (v0.6.0 以降)
  - ref: https://github.com/tsukumijima/px4_drv/issues/38
- チャンネル切り替え、USB の切断・再接続、内蔵カードリーダーとの同時利用に関する安定性を改善 (v0.6.0 以降)
- BonDriver の ini の節に `=` を含まない行があると、設定の読み込みが無限ループに陥る問題を修正 (v0.7.0 以降)
- コピー型の `GetTsStream()` で、呼び出し元のバッファを越えて TS を書き込むことがある問題を修正 (v0.7.0 以降)
- DriverHost_PX4 のログファイルの末尾に、前回起動時のログが残る問題を修正 (v0.7.0 以降)

#### ビルド・ドキュメント

- ビルドとパッケージングを全自動で行うスクリプトを追加
- README（このページ）に WinUSB 版のインストール方法などを追記
  - Visual Studio 2022 が入っていれば、build.ps1 を実行するだけで全自動でビルドからパッケージングまで行える
- fwtool: PX-W3PE5 / PX-Q3PE4 / PX-Q3PE5 / PX-W3U4 / PX-Q3U4 / PX-MLT5PE / PX-MLT8PE の2021年版公式 Windows ドライバからのファームウェア抽出に対応 (v0.6.0 以降)
  - 既定の抽出済みファームウェアで問題なく動作することが知られているため、特段実用性はない
### 変更点 (Linux 版)

動作確認は Ubuntu 22.04 LTS (x64) で行っています。

#### 新デバイス対応・機能追加

- [techmadot 氏のフォーク](https://github.com/techmadot/px4_drv) の更新を取り込み、PX-M1UR / PX-S1UR に対応
- [otya 氏のフォーク](https://github.com/otya128/px4_drv) での更新を取り込み、BS/CS の選局で相対 TS 番号に加えて TSID も指定できるように
- [hendecarows 氏のフォーク](https://github.com/hendecarows/px4_drv) での更新を取り込み、DTV03A-1TU に対応 (v0.5.4 以降)
- チップ構成が一部変更された、ロット番号 2309 (2023年9月) 以降の DTV02A-1T1S-U に対応 (v0.4.5 以降)
- PX-MLT5PE のリブランド品である DTV02A-5TS-P (USB Product ID: 0x924e) に対応 (v0.6.0 以降)
- 各機種の内蔵 B-CAS カードリーダーを利用するためのドライバを追加 (v0.7.0 以降)
  - 別途 `px4-drv-pcsc` ドライバをインストールすることで、pcscd から内蔵カードリーダーを認識・利用できるようになる
  - pcscd が既定で認識している外付けカードリーダーも引き続き利用できる
-  PX-MLT 系チューナー (PX-MLT5PE / PX-MLT8PE / DTV02A-4TS-P / DTV02A-5TS-P) において、WinUSB 版と同じくケーブルテレビ (トランスモジュレーション方式: ISDB-C) とスカパー！プレミアムサービス光 (J.83 Annex B) の受信に対応 (v0.7.0 以降)
  - 受信方法は [ケーブルテレビ (ISDB-C) / スカパー！プレミアムサービス光 (J.83B) の受信](#ケーブルテレビ-isdb-c--スカパープレミアムサービス光-j83b-の受信) を参照のこと

#### ドライバの品質改善

- [otya 氏のフォーク](https://github.com/otya128/px4_drv) での更新を取り込み、安定性と互換性を改善
- [kznrluk 氏のフォーク](https://github.com/kznrluk/px4_drv) の更新を取り込み、Linux カーネル 6.4 系以降の API 変更に対応
- https://github.com/tsukumijima/px4_drv/pull/3 をマージし、`ctrl_timeout` をモジュールパラメーターに追加 (v0.4.1 以降)
- https://github.com/tsukumijima/px4_drv/pull/6 をマージし、Linux カーネル 6.8 系以降の API 変更に対応 (v0.4.4 以降)
- DTV02A-4TS-P / PX-M1UR / PX-S1UR のデバイスファイルのパーミッションが 644 になっていたのを、他の機種と同じ 664 に修正 (v0.4.5 以降)
- ロット番号 2309 以降の DTV02A-1T1S-U で、復調器 (TC90522) の初期化と終了の処理が抜けていたのを修正 (v0.5.0 以降)
- Digibest 製チューナーに常に USB 電源を供給するようにし、USB の省電力機能によってチューナーの動作が不安定になる問題を改善 (v0.5.1 以降)
- https://github.com/tsukumijima/px4_drv/pull/20 をマージし、UBSAN を有効にしたカーネルで、dmesg に誤検知の警告が出る問題を修正 (v0.5.3 以降)
- 新しい GCC を使う Oracle Linux 9 などでビルドできない問題を修正 (v0.5.4 以降)
- https://github.com/tsukumijima/px4_drv/pull/33 をマージし、IBT (Indirect Branch Tracking) 対応に伴う `objtool` の検査強化により、Linux 6.15 系などでモジュールをビルド・ロードできない問題に対応 (v0.5.5 以降)
  - 当初は保守的に Linux 6.15.4 以降でのみ `module_init()` と `module_exit()` を使っていたが、これらは古いカーネルでも利用できるため、v0.6.0 以降ではカーネル版による分岐を削除して統一した
- https://github.com/tsukumijima/px4_drv/pull/37 をマージし、Linux カーネル 6.19 系以降でビルドできない問題に対応 (v0.5.6 以降)
- https://github.com/tsukumijima/px4_drv/pull/40 をマージし、モジュールパラメーター `multi_device_power_control_mode` の値の範囲の検査が誤っていたのを修正 (v0.6.0 以降)
- PX4 系チューナーを複数台接続したとき、使っていない方のチューナーの基板にも電源が入ったままになる問題を修正 (v0.6.0 以降)
  - ref: https://github.com/tsukumijima/px4_drv/issues/36
- チャンネル切り替え、USB の切断・再接続に関する安定性を改善 (v0.6.0 以降)
- IT930x の GPIO の設定の書き込みに一度失敗すると、USB を抜き差しするまで同じ設定をやり直せなくなる問題を修正 (v0.7.0 以降)
- PX-S1UR / DTV03A-1TU で、チューナーを開く処理が途中で失敗したときに、基板の電源が入ったまま残る問題を修正 (v0.7.0 以降)
- Linux 4.15 (Ubuntu 18.04 の初期カーネル) 以降でビルドできるように修正 (v0.7.0 以降)
- WinUSB 版と同じく、PX-S1UR を ISDB2056 系の共通処理に統合し、設計の異なる DTV03A-1TU (ISDBT2071) を単独の実装に分離 (v0.7.0 以降)

#### ビルド・ドキュメント

- Debian パッケージ (.deb) の作成とインストールに対応
- DKMS でのインストール時にファームウェアを自動でインストールするように変更
- README (このページ) に Debian パッケージからのインストール方法などを追記
- https://github.com/tsukumijima/px4_drv/pull/27 をマージし、新しいカーネルをインストールしたときに、DKMS の後処理が使用中のドライバをアンロードしてしまう問題を修正 (v0.5.3 以降)
- https://github.com/tsukumijima/px4_drv/pull/29 をマージし、Ubuntu 24.04 で DKMS の後処理が `99-px4video.rules` と `it930x-firmware.bin` を削除してしまう問題を修正 (v0.5.4 以降)

## 対応デバイス

- PLEX

	- PX-W3U4
	- PX-Q3U4
	- PX-W3PE4
	- PX-Q3PE4
	- PX-W3PE5
	- PX-Q3PE5
	- PX-MLT5PE
	- PX-MLT8PE
    - PX-M1UR
    - PX-S1UR

- e-Better

	- DTV02-1T1S-U (実験的)
	- DTV02A-1T1S-U
	  - チップ構成が一部変更された、ロット番号 2309 (2023年9月) 以降の DTV02A-1T1S-U にも対応しています。  
	  手元の実機では問題なく動作していますが、長期間の動作テストは行えていないため、未知の不具合があるかもしれません。
	- DTV02A-4TS-P
	- DTV02A-5TS-P (PX-MLT5PE のリブランド品)
	- DTV03A-1TU (実験的)
	  - チップ構成が大幅に変更された、ロット番号 2021-11 以降の個体のみ対応しています。

> [!NOTE]
> 2021 年以降メンテナンスされていない [nns779/px4_drv](https://github.com/nns779/px4_drv) と異なり、新規に下記チューナーのサポートを追加しています。
> 
> - PLEX PX-M1UR
> - PLEX PX-S1UR
> - e-Better DTV02A-5TS-P (PX-MLT5PE のリブランド品)
> - e-Better DTV02A-1T1S-U / Digibest ISDB2056 (Windows 版ドライバを新規追加)
> - e-Better DTV02A-1T1S-U (ロット番号 2309 以降) / Digibest ISDB2056N
> - e-Better DTV03A-1TU / Digibest ISDBT2071 (ロット番号 2021-11 以降)

> [!WARNING]
> PX-M1UR または DTV02(A)-1T1S-U で CATV（周波数変換パススルー）の ISDB-T C13ch ~ C24ch を受信するには、[専用の recpt1 フォーク (hendecarows/recpt1)](https://github.com/hendecarows/recpt1) が必要となります。  
> [stz2012/recpt1](https://github.com/stz2012/recpt1) や [recisdb](https://github.com/kazuki0824/recisdb-rs) では、当該機種にて C13ch ~ C62ch を正常に選局できないことが報告されています（[詳細はこちら](https://github.com/tsukumijima/px4_drv/issues/16)）。  
> ただし、C13ch ~ C24ch 以外のチャンネルであれば、stz2012/recpt1 や recisdb でも問題なく選局・受信が可能です。

## インストール (Windows)

Windows (WinUSB) 版のドライバは、OS にチューナーを認識させるための inf ファイルと、px4_drv 専用の BonDriver、ドライバの実体でチューナー操作を司る DriverHost_PX4 から構成されています。

**ビルド済みのアーカイブは [tsukumijima/DTV-Builds](https://github.com/tsukumijima/DTV-Builds) からダウンロードできます。**  
または、winusb フォルダにある build.ps1 を実行して、ご自分でビルドしたものを使うこともできます (Visual Studio 2022 が必要です) 。

### 1. 自己署名証明書のインストール

Driver フォルダには、各機種ごとのドライバのインストールファイル (.inf) が配置されています。  
ドライバをインストールする前に cert-install.jse を実行し、自己署名証明書をインストールしてください。

自己署名証明書をインストールして信頼することで、自己署名証明書を使用して署名されたドライバも、（自己署名証明書をアンインストールするまでは）通常の署名付きドライバと同様に信頼されるようになります。

cert-install.jse が Smart App Control や関連付けの変更などにより実行できない場合は、管理者権限の Windows Terminal または PowerShell を開き、ビルド済みアーカイブ内の Driver フォルダに移動した上で、下記コマンドを実行してください。

```powershell
certutil.exe -addstore Root ".\px4_drv_winusb.cer"
certutil.exe -addstore TrustedPublisher ".\px4_drv_winusb.cer"
```

### 2. ドライバのインストール

自己署名証明書をインストールしたあとは、公式ドライバと同様の手順でインストールできると思います（公式ドライバは事前にアンインストールしてください）。

具体的には、デバイスマネージャーからチューナーデバイスを選択し、右クリックメニューの [ドライバーの更新] → [コンピューターを参照してドライバーを検索] から、inf ファイルが入っている Driver フォルダを指定してください。[ドライバーが正常に更新されました] と表示されていれば OK です。

### 3. BonDriver の配置

チューナーの機種に応じた BonDriver を配置します。

- PX4/PX5 シリーズの機種: `BonDriver_PX4`
- PX-MLT シリーズの機種・DTV02A-4TS-P・DTV02A-5TS-P: `BonDriver_PX-MLT`
- DTV02A-1T1S-U: `BonDriver_ISDB2056`
- DTV02A-1T1S-U (ロット番号 2309 以降): `BonDriver_ISDB2056N`
- DTV03A-1TU: `BonDriver_ISDBT2071`
- PX-M1UR: `BonDriver_PX-M1UR`
- PX-S1UR: `BonDriver_PX-S1UR`

お使いの PC に合うビット数のフォルダの中のファイルを**すべて**選択し、TVTest や EDCB などのソフトウェアが指定する BonDriver の配置フォルダにコピーします。  
BonDriver と同じフォルダに DriverHost_PX4.exe / DriverHost_PX4.ini / it930x-firmware.bin があることを確認してください。

使用にあたり、特段 ini ファイルの設定変更などは必要ありません。ソフトウェアごとにチャンネルスキャンを行えばそのまま視聴できます。  
なお、BonDriver の ini ファイル内の `DisplayErrorMessage` を 1 に設定すると、オリジナル同様にエラー発生時にメッセージボックスを表示します。

### 4. 内蔵 B-CAS カードリーダーの利用

**v0.6.0 以降、winUSB 版 px4_drv 専用の WinSCard.dll を TVTest.exe や EpgDataCap_Bon.exe と同じフォルダに配置することで、内蔵カードリーダーが利用できるようになりました！**

機種別の BonDriver フォルダに同梱されている `WinSCard.dll` を、TVTest や EDCB など、B-CAS カードを利用するアプリケーションの実行ファイルと同じフォルダにコピーしてください。

> [!NOTE]
> **Windows が認識している外付けカードリーダーと内蔵カードリーダーは併用できます。**  
> カードリーダーの一覧には内蔵カードリーダー、外付けカードリーダーの順で表示されます。  

> [!NOTE]
> **リモートデスクトップ接続中に、チューナーを接続した PC 側の外付けカードリーダーが見つからなくなる場合があります。**  
> RDP のスマートカードリダイレクトが有効なセッションでは、Windows 標準の WinSCard API が接続元 PC 側のカードリーダーを参照するためです。  
> [Windows 標準の対処方法](https://learn.microsoft.com/ja-jp/azure/virtual-desktop/redirection-configure-smart-cards)として、リモート デスクトップ接続の「オプションの表示」→「ローカル リソース」→「詳細」からスマートカードのチェックを外すか、接続先 PC の `gpedit.msc` で「コンピューターの構成」→「管理用テンプレート」→「Windows コンポーネント」→「リモート デスクトップ サービス」→「リモート デスクトップ セッション ホスト」→「デバイスとリソースのリダイレクト」→「スマート カード デバイスのリダイレクトを許可しない」を有効にしてください。  
> 内蔵カードリーダーへの通信は、`DriverHost_PX4.exe` が接続先 PC の WinUSB デバイスと直接行うため、RDP のスマートカードリダイレクトを経由しません。  
> ただし、`WinSCard.dll` を配置していても、外付けカードリーダーを選んだ処理は Windows 標準の WinSCard API へ引き渡されます。この場合は RDP の設定に従い、接続元 PC 側のカードリーダーだけが外付けカードリーダーとして表示されることがあります。

> [!WARNING]  
> **アプリケーション側のビット数 (32bit/64bit) と `WinSCard.dll` のビット数を合わせる必要があります。**  
> また、すでに別実装の `WinSCard.dll` を配置している場合は、元の DLL を退避してから入れ替えてください。  
> 同じフォルダで2つの `WinSCard.dll` を併用することはできません。

> [!TIP]
> `WinSCard.dll` の追加・退避後、TVTest ではカードリーダーエラーが発生することがありますが、これは過去使っていたカードリーダーにアクセスできなくなったことによるエラーですので、異常ではありません。  
> **「設定」→「TSプロセッサー」→「デフォルトフィルター」のセレクトボックスで利用するカードリーダー名を選択し、TVTest を再起動すると、そのカードリーダーに挿入された B-CAS カードを使ってスクランブル解除を行えます。**  

> [!TIP]
> **PC に複数のチューナーを接続している場合、TVTest では複数の内蔵カードリーダーを認識します。** B-CAS カードを挿入したチューナーの内蔵カードリーダーを選択してください。  
> **内蔵カードリーダーを使うために、PC に接続されているすべてのチューナーに B-CAS カードを挿入する必要はありません。** 内蔵カードリーダーは、受信中チューナーと異なるデバイスでも利用できます。

> [!NOTE]  
> `WinSCard.dll` は必要に応じて同じフォルダの `DriverHost_PX4.exe` を起動し、内蔵カードリーダーとの通信を仲介します。  
> 複数の TVTest や EDCB プロセスが同じカードへ接続した場合も、APDU と PC/SC トランザクションはカード単位で直列に処理されます。

## インストール (Linux)

### 1. ファームウェアの抽出とインストール (手動インストール時のみ)

> [!IMPORTANT]  
> **px4_drv を Debian パッケージや DKMS を使用してインストールする際、ファームウェアは自動的にインストールされます。**  
> **Debian パッケージや DKMS を使用してインストールを行う場合は、この手順をスキップしてください。**

事前に unzip, gcc, make がインストールされている必要があります。

	$ cd fwtool
	$ make
	$ wget http://plex-net.co.jp/plex/pxw3u4/pxw3u4_BDA_ver1x64.zip -O pxw3u4_BDA_ver1x64.zip
	$ unzip -oj pxw3u4_BDA_ver1x64.zip pxw3u4_BDA_ver1x64/PXW3U4.sys && rm pxw3u4_BDA_ver1x64.zip
	$ ./fwtool PXW3U4.sys it930x-firmware.bin && rm PXW3U4.sys
	$ sudo mkdir -p /lib/firmware && sudo cp it930x-firmware.bin /lib/firmware/
	$ cd ../

または、抽出済みのファームウェアを利用することもできます。

	$ sudo mkdir -p /lib/firmware && sudo cp ./etc/it930x-firmware.bin /lib/firmware/

### 2. ドライバのインストール

px4_drv は、Linux 4.15 (Ubuntu 18.04 の初期カーネル) 以降のカーネルでビルドできます。  
一部の Linux ディストリビューションでは、別途 udev のインストールが必要になる場合があります。
	
#### Debian パッケージを使用してインストール (強く推奨)

Debian パッケージを使用してインストールすると依存パッケージも自動インストールされるほか、DKMS のソースコード管理も透過的に行われます。  
Ubuntu / Debian 環境では Debian パッケージを使用してインストールすることを強く推奨します。

	$ wget https://github.com/tsukumijima/px4_drv/releases/download/v0.6.1/px4-drv-dkms_0.6.1_all.deb
	$ sudo apt install -y ./px4-drv-dkms_0.6.1_all.deb

上記コマンドで、px4_drv の Debian パッケージをインストールできます。

> [!TIP]
手動で Debian パッケージを生成することもできます。  
> `./build_deb.sh` を実行すると、`./build_deb.sh` の一つ上層のディレクトリに `px4-drv-dkms_0.6.1_all.deb` という名前の Debian パッケージが生成されます。  
> ```
> $ ./build_deb.sh
> $ sudo apt install -y ../px4-drv-dkms_0.6.1_all.deb
> ```
> 上記コマンドで、生成した px4_drv の Debian パッケージをインストールできます。

#### DKMS を使用してインストールする

gcc, make, カーネルソース/ヘッダ, dkms がインストールされている必要があります。

	$ sudo cp -a ./ /usr/src/px4_drv-0.6.1
	$ sudo dkms add px4_drv/0.6.1
	$ sudo dkms install px4_drv/0.6.1

#### DKMS を使用せずにインストールする

事前に gcc, make, カーネルソース/ヘッダがインストールされている必要があります。

> [!WARNING]  
> DKMS を使用せずにインストールした場合、**カーネルのアップデート時にドライバが自動で再ビルドされないため、アップデート後に再度インストールを行う必要があります。**  
> **可能な限り DKMS を使用してインストールすることをおすすめします。**

	$ cd driver
	$ make
	$ sudo make install
	$ cd ../

### 3. 確認

#### 3.1 カーネルモジュールのロードの確認

**下記のコマンドを実行し、`px4_drv` から始まる行が表示されれば、カーネルモジュールが正常にロードされています。**

	$ lsmod | grep -e ^px4_drv
	px4_drv                81920  0

**何も表示されない場合は、下記のコマンドでカーネルモジュールを明示的にロードしてから、再度上記のコマンドを実行して確認を行ってください。**

	$ sudo modprobe px4_drv

それでもカーネルモジュールが正常にロードされない場合は、インストールから再度やり直してください。

> [!IMPORTANT]  
> **px4_drv や Linux カーネルの更新後、px4_drv は自動再ロードされない場合があります。**  
> **px4_drv や Linux カーネルを更新した際は、可能な限りすぐに PC を再起動することを強く推奨します。**   
> すぐに再起動ができない状況では、必ず `sudo modprobe px4_drv` を実行してください。  
> (当然ですが、録画予約のある時間帯に px4_drv や Linux カーネルを更新するべきではありません。)

#### 3.2 デバイスファイルの確認

インストールに成功し、カーネルモジュールがロードされた状態でデバイスが接続されると、`/dev/` 以下にデバイスファイルが作成されます。  
下記のようなコマンドで確認できます。

##### PLEX PX-W3U4/W3PE4/W3PE5 を接続した場合

	$ ls /dev/px4video*
	/dev/px4video0  /dev/px4video1  /dev/px4video2  /dev/px4video3

チューナーは、`px4video0` から ISDB-S, ISDB-S, ISDB-T, ISDB-T というように、SとTが2つずつ交互に割り当てられます。

##### PLEX PX-Q3U4/Q3PE4/Q3PE5 を接続した場合

	$ ls /dev/px4video*
	/dev/px4video0  /dev/px4video2  /dev/px4video4  /dev/px4video6
	/dev/px4video1  /dev/px4video3  /dev/px4video5  /dev/px4video7

チューナーは、`px4video0` から ISDB-S, ISDB-S, ISDB-T, ISDB-T, ISDB-S, ISDB-S, ISDB-T, ISDB-T というように、S と T が2つずつ交互に割り当てられます。

##### PLEX PX-MLT5PE / e-Better DTV02A-5TS-P を接続した場合

	$ ls /dev/pxmlt5video*
	/dev/pxmlt5video0  /dev/pxmlt5video2  /dev/pxmlt5video4
	/dev/pxmlt5video1  /dev/pxmlt5video3

すべてのチューナーにおいて、ISDB-T と ISDB-S のどちらも受信可能です。

##### PLEX PX-MLT8PE を接続した場合

	$ ls /dev/pxmlt8video*
	/dev/pxmlt8video0  /dev/pxmlt8video3  /dev/pxmlt8video6
	/dev/pxmlt8video1  /dev/pxmlt8video4  /dev/pxmlt8video7
	/dev/pxmlt8video2  /dev/pxmlt8video5

すべてのチューナーにおいて、ISDB-T と ISDB-S のどちらも受信可能です。

##### PLEX PX-M1UR を接続した場合

	$ ls /dev/pxm1urvideo*
	/dev/pxm1urvideo0

すべてのチューナーにおいて、ISDB-T と ISDB-S のどちらも受信可能です。

##### PLEX PX-S1UR を接続した場合

	$ ls /dev/pxs1urvideo*
	/dev/pxs1urvideo0

すべてのチューナーにおいて、ISDB-T のみ受信可能です。

##### e-Better DTV03A-1TU を接続した場合

	$ ls /dev/isdbt2071video*
	/dev/isdbt2071video0

すべてのチューナーにおいて、ISDB-T のみ受信可能です。

##### e-Better DTV02-1T1S-U/DTV02A-1T1S-U を接続した場合

	$ ls /dev/isdb2056video*
	/dev/isdb2056video0

すべてのチューナーにおいて、ISDB-T と ISDB-S のどちらも受信可能です。

> [!NOTE]  
> ロット番号 2309 以降の DTV02A-1T1S-U を接続した場合でも、デバイスファイル名は `/dev/isdb2056video*` となります。  
> `/dev/isdb2056nvideo*` ではないので注意してください。

##### e-Better DTV02A-4TS-P を接続した場合

	$ ls /dev/isdb6014video*
	/dev/isdb6014video0  /dev/isdb6014video2
	/dev/isdb6014video1  /dev/isdb6014video3

すべてのチューナーにおいて、ISDB-T と ISDB-S のどちらも受信可能です。

### 4. 内蔵 B-CAS カードリーダーの利用

**v0.7.0 以降、pcscd 用ドライバ (`px4-drv-pcsc`) を別途インストールすることで、内蔵カードリーダーを外付けカードリーダー同様に利用できるようになりました！**  
内蔵 B-CAS カードリーダーを使わない場合は、この手順は不要です。

> [!NOTE]
> Mirakurun などを Docker 環境で稼働させている場合は、pcscd をホストとコンテナのどちらで動かすかによって、ドライバのインストール先が異なります。  
> 先に [Docker 環境からカードリーダーにアクセスする場合](#docker-環境からカードリーダーにアクセスする場合) をお読みください。 

#### pcscd 用ドライバ (px4-drv-pcsc) のインストール

Releases から、CPU アーキテクチャに合った Debian パッケージ (amd64 か arm64) をダウンロードし、pcscd と一緒にインストールします。  
Ubuntu 20.04 以降と、同じ世代以降の Debian 系ディストリビューションで動作します。

	$ wget https://github.com/tsukumijima/px4_drv/releases/download/v0.6.1/px4-drv-pcsc_0.6.1_amd64.deb
	$ sudo apt install -y ./px4-drv-pcsc_0.6.1_amd64.deb pcscd
	$ sudo udevadm trigger --subsystem-match=px4card
	$ sudo udevadm settle
	$ sudo systemctl restart pcscd

`udevadm` の2行は、すでに接続しているチューナーのカードリーダーを、新しくインストールした pcscd から開けるようにするためのものです。

> [!IMPORTANT]
> 基本的に、異なるバージョンの px4_drv と px4-drv-pcsc を混在させて利用することはできません。  
> px4_drv と px4-drv-pcsc の更新後は、必ず pcscd を再起動してください。

> [!TIP]  
> ソースからビルドしてインストールすることもできます。  
> C++17 に対応したコンパイラー、make 、pkg-config 、pcsc-lite の開発用ヘッダー (Debian / Ubuntu では `libpcsclite-dev`) が必要です。  
> ```
> $ make -C pcsc
> $ sudo make -C pcsc install
> ```

#### 内蔵カードリーダーの認識を確認

B-CAS カードを挿した状態で、`pcsc-tools` の `pcsc_scan` を実行します。

	$ sudo apt install -y pcsc-tools
	$ pcsc_scan

次のように、内蔵カードリーダーの名前の下に `Card inserted` と、末尾に `B-CAS Card` の判定が表示されれば利用可能です。  
確認が完了したら、Ctrl+C で `pcsc_scan` を終了します。

```
 Reader 0: PLEX PX-W3U4 Smart Card Reader [ID: CD65353E4231AA77] (000001711009061) 00 00
  Event number: 0
  Card state: Card inserted, 
  ATR: 3B F0 12 00 FF 91 81 B1 7C 45 1F 03 99
(中略)
Possibly identified card (using /usr/share/pcsc/smartcard_list.txt):
3B F0 12 00 FF 91 81 B1 7C 45 1F 03 99
	Japanese Chijou Digital B-CAS Card (pay TV)
```

> [!TIP]  
> リーダー名の `[ID: ...]` は、Windows 版のリーダー名の `(ID: ...)` と同じ値になるよう設計されています。  
> Windows と Linux の両方で、同じチューナー機器を共通の ID で識別できます。

#### 内蔵カードリーダーが認識されない場合

- **カードリーダーが表示されない:** `ls /dev/px4card/` で、カードリーダーのデバイスファイルが存在するかを確認してください。  
  デバイスファイルが存在しない場合は、インストールした px4_drv が内蔵カードリーダーに対応したバージョンかを確認してください。  
  デバイスファイルが存在する場合は、以下のコマンドを再度実行してください。  
  ```
  $ sudo udevadm trigger --subsystem-match=px4card
  $ sudo udevadm settle
  $ sudo systemctl restart pcscd
  ```
- **`Card state` が `Card inserted` にならない:** B-CAS カードの向き (裏表) と、奥まで挿し込まれているかを確認してください。
- **リーダー名に `[ID: ...]` が入っていない:** カーネルモジュールを読み込み直したときなどに発生します。pcscd を再起動することで解消されます。

それでもカードリーダーが表示されない場合は、`journalctl -u pcscd` で pcscd のログを確認してください。  
デバイスファイルのオープンに失敗した際には `open(...) failed` と、その理由が出力されます。

#### root 以外のサービスから使う場合 (Ubuntu 24.04 / Debian 13 以降)

Ubuntu 24.04 / Debian 13 以降の pcscd は、接続してきたプログラムを polkit で検証し、ログインしていないユーザーからの接続を拒否します。  
このため、Mirakurun / EDCB を一般ユーザー権限 (root 以外) の systemd サービスとして稼働させている場合、カードリーダーにアクセスできなくなります。

> [!NOTE]
> この問題は内蔵カードリーダー固有の問題ではなく、外付けカードリーダーでも同様に発生します。  
> カードリーダーへのアクセスが拒否された場合、pcscd のログに `is NOT authorized for action: access_pcsc` が出力されます。
> なお、Ubuntu 22.04 / Debian 12 以前の pcscd は当該チェックを行わないため、この設定を記述する必要はありません。

カードリーダーへのアクセスが拒否される場合には、次の polkit ルールを `/etc/polkit-1/rules.d/50-pcscd-recorder.rules` に配置し、  
systemd サービスの起動ユーザー (この例では `mirakurun`) がカードリーダーにアクセスできるように設定してください。

```js
polkit.addRule(function(action, subject) {
    if ((action.id == "org.debian.pcsc-lite.access_pcsc" ||
         action.id == "org.debian.pcsc-lite.access_card") &&
        subject.user == "mirakurun") {
        return polkit.Result.YES;
    }
});
```

> [!NOTE]
> recisdb などの受信ソフトウェアは、カードリーダーのデバイスファイルには直接アクセスせず、pcscd 経由で間接的にアクセスします。  
> チューナーデバイスの `video` グループと異なり、recisdb を実行するユーザーを `pcscd` グループに追加する必要はありません。

#### Docker 環境からカードリーダーにアクセスする場合

Docker 環境からカードリーダーにアクセスするには、ホストの pcscd を使うか、コンテナ内で pcscd を立ち上げるかの2つの方法があります。

- **コンテナ内で pcscd を動かす (Mirakurun の既定の構成):** コンテナのイメージにドライバを追加してビルドし直します
- **ホストの pcscd を使う:** イメージはそのままで済みますが、ホストとコンテナの pcsc-lite のバージョンが異なる場合は接続できないことがあります

> [!WARNING]
> 事前に px4_drv のカーネルモジュールがホスト側にインストールされている必要があります。  
> ホストとコンテナの両方で pcscd を起動するとカードリーダーへのアクセスが競合し、どちらか一方が正常に動作しなくなってしまいます。

##### コンテナ内で pcscd を動かす

Mirakurun の公式イメージには pcscd 用ドライバが入っていないため、事前に px4-drv-pcsc の Debian パッケージを追加したイメージを作成します。  
起動用の `docker-compose.yml` は Mirakurun 公式のものをベースに、`image` だけをこのイメージに変更してください。  

```dockerfile
# arm64 環境では "amd64" を "arm64" に変更してください
FROM chinachu/mirakurun:latest
ADD https://github.com/tsukumijima/px4_drv/releases/download/v0.6.1/px4-drv-pcsc_0.6.1_amd64.deb /tmp/
RUN dpkg -i /tmp/px4-drv-pcsc_0.6.1_amd64.deb && rm /tmp/px4-drv-pcsc_0.6.1_amd64.deb
```

完了したら、ホストの pcscd を停止します。

	$ sudo systemctl disable --now pcscd.socket pcscd.service

> [!NOTE]
> 全てのチューナー機器が取り外されると、`/dev/px4card` はディレクトリごと消滅します。  
> `/dev/px4card` だけをコンテナへマウントしてしまうと、USB を抜き差しした後にデバイスファイルがコンテナから見えなくなってしまう可能性があります。  
> 公式の Docker Compose に従い、`/dev` 全体をマウントしてください。

##### ホストの pcscd を使う

事前に pcscd 用ドライバをホストマシンに [インストール](#pcscd-用ドライバ-px4-drv-pcsc-のインストール) しておいてください。

その後、コンテナ内では pcscd を起動させず、代わりにホスト上の pcscd のソケットディレクトリをコンテナ内にマウントします。  
Mirakurun の場合は、環境変数 `DISABLE_PCSCD=1` を設定した上で、`/var/run/pcscd:/var/run/pcscd` をコンテナ内にマウントしてください。

> [!WARNING]
> Mirakurun 公式の `docker-compose.yml` のようにソケットファイル (`pcscd.comm`) だけをマウントすると、ホストの pcscd が再起動した際にコンテナから接続できなくなります。  
> 必ず `/var/run/pcscd` ディレクトリごとコンテナへマウントしてください。  
> また、ホストとコンテナの pcsc-lite のバージョンが大きく異なると接続できないことがあります。その場合は、コンテナ内で pcscd を動かす構成を検討してください。

## アンインストール (Windows)

### 1. ドライバのアンインストール

デバイスマネージャーからチューナーデバイス（ PLEX PX-W3PE5 ISDB-T/S Receiver Device (WinUSB) のような名前）を選択します。  
その後、右クリックメニュー → [デバイスのアンインストール] から、[このデバイスのドライバー ソフトウェアを削除します] にチェックを入れて、ドライバをアンインストールしてください。

### 2. 自己署名証明書のアンインストール

ドライバのインストール時にインストールした自己署名証明書をアンインストールするには、cert-uninstall.jse を実行します。アンインストールするとドライバが信頼されなくなってしまうので、十分注意してください。

## アンインストール (Linux)

### 1. ドライバのアンインストール

#### Debian パッケージを使用してインストールした場合

	$ sudo apt purge px4-drv-dkms

#### DKMS を使用してインストールした場合

	$ sudo dkms remove px4_drv/0.6.1 --all
	$ sudo rm -rf /usr/src/px4_drv-0.6.1

#### DKMS を使用せずにインストールした場合

内蔵カードリーダーを pcscd で使っている場合は、pcscd がデバイスファイルを開いたままでモジュールを外せないため、先に pcscd を止めます。

	$ sudo systemctl stop pcscd.socket pcscd.service
	$ sudo modprobe -r px4_drv
	$ cd driver
	$ sudo make uninstall
	$ cd ../

### 2. ファームウェアのアンインストール

> [!NOTE]  
> **px4_drv を Debian パッケージや DKMS を使用してインストールした場合、ファームウェアは自動的にアンインストールされます。**  
> Debian パッケージや DKMS を使用してインストールを行った場合は、この手順はスキップしてください。

	$ sudo rm /lib/firmware/it930x-firmware.bin

## 受信方法

### Windows

チューナーの機種に応じた BonDriver とその設定ファイルを配置し、TVTest や EDCB などの BonDriver に対応したソフトウェアで使用することで、TS データを受信することが可能です。

BonDriver は専用のものが必要になるため、公式 (Jacky版) BonDriver や radi-sh 氏版 BonDriver_BDA と併用することはできません。

### Linux

recpt1 や [BonDriverProxy_Linux](https://github.com/u-n-k-n-o-w-n/BonDriverProxy_Linux) 等の PT シリーズ用 chardev ドライバに対応したソフトウェアを使用することで、TS データを受信することが可能です。  
recpt1 は、PLEX 社より配布されているものを使用する必要はありません。

BonDriverProxy_Linux と、PLEX PX-MLT5PE や e-Better DTV02A-1T1S-U などのデバイスファイル1つで ISDB-T と ISDB-S のどちらも受信可能なチューナーを組み合わせて使用する場合は、BonDriver として BonDriverProxy_Linux に同梱されている BonDriver_LinuxPT の代わりに、[BonDriver_LinuxPTX](https://github.com/nns779/BonDriver_LinuxPTX) を使用してください。

## ケーブルテレビ (ISDB-C) / スカパー！プレミアムサービス光 (J.83B) の受信

**v0.7.0 以降、PX-MLT 系チューナー (PX-MLT5PE / PX-MLT8PE / DTV02A-4TS-P / DTV02A-5TS-P) なら、地上波や BS/CS に加え、ケーブルテレビ (ISDB-C) とスカパー！プレミアムサービス光 (J.83 Annex B) を受信できるようになりました！**

- ISDB-C (J.83 Annex C): ケーブルテレビ局が地上波・地上波 (区域外再送信)・BS・専門チャンネルをトランスモジュレーション方式で再送信する放送
  - 実装にあたり、実際に PX-MLT5PE とケーブルテレビ（イッツコム）の環境で動作確認を行っています。
- J.83 Annex B: フレッツ・テレビなどの光回線経由で送られる「スカパー！プレミアムサービス光」の放送
  - スカパー！プレミアムサービス光は、ユーザーの環境でチャンネルスキャン・受信が正常に行えたと報告を受けています。

### Windows

`BonDriver_PX-MLT.dll` は、既定では従来通り、地上波・BS/CS のみが受信対象として設定されています。  
ケーブルテレビを受信するには、`BonDriver_PX-MLT.ini` → `[BonDriver]` → `System` に `ISDB-C` を、  
スカパー！プレミアムサービス光を受信するには `J.83B` を明示的に追加してください (例: `System="ISDB-T,ISDB-S,ISDB-C"`)。

```ini
[BonDriver]
Name="PX-MLT"
; ケーブルテレビ (ISDB-C) を受信する際は 2つ目、スカパー！プレミアムサービス光 (J.83B) を受信する際は
; 3つ目の System の行の先頭の ; を外し、1つ目の行の先頭に ; を付ける
System="ISDB-T,ISDB-S"
;System="ISDB-T,ISDB-S,ISDB-C"
;System="ISDB-T,ISDB-S,J.83B"
...
```

> [!TIP]  
> EDCB (EpgDataCap_Bon) などの「全チューニング空間の全チャンネルを順に選局して一括でチャンネルスキャンを行う」受信ソフトウェアでは、実際には全て受信できないチューニング空間であっても、チャンネルスキャン時に毎回選局されます。  
> ケーブルテレビのチャンネル定義は約450行、スカパー！プレミアムサービス光は約110行あり、受信できない環境では1チャンネルごとに受信を待ってから次へ進む形となるため、既定でこれらのチューニング空間を有効にすると、チャンネルスキャンに非常に時間が掛かります。  
> こうした理由により、アンテナで地上波と BS/CS だけを受信している大半のユーザーのチャンネルスキャンまで遅くなってしまうことから、ISDB-C / J.83B は既定では無効化されています。

> [!NOTE]  
> ケーブルテレビでは、1つの物理チャンネルで複数の放送をまとめて送出していることがあります (TSMF 方式)。  
> チャンネル定義にある「C52Ch/TS1」のような行は、こうした TSMF 方式で送出されているチャンネルの、2本目以降の TS を受信するためのものです。  
> 当該物理チャンネルに該当する TS がなければ、チャンネルスキャン時に自動的に除外されます。

> [!NOTE]
> BS4K が再送信されているケーブルテレビにおいて 4K 放送を受信するには、別途 [BonDriver_dantto4k](https://github.com/nekohkr/dantto4k) と組み合わせてご利用ください。

### Linux

[recisdb](https://github.com/kazuki0824/recisdb-rs) を使い、次のようにチャンネルを指定して ISDB-C / J.83B を受信できます。  

- `ISDBC-C52` (ケーブルテレビ: C52)、`ISDBC-20` (ケーブルテレビ: UHF 20ch)
- `ISDBC-C52_1` (ケーブルテレビ: C52 の2本目の TS (TSMF))
- `J83B-H001` 〜 `J83B-H105`、`J83B-374950` (スカパー！プレミアムサービス光: 番号か中心周波数 (kHz) で指定)

受信ソフトウェアを開発する方向けの ioctl 仕様は、[ISDB-C と J.83 Annex B の選局の仕様](docs/isdbc_tuning.md) に記載されています。

> [!WARNING]  
> 2026/10/29 現在、recisdb の ISDB-C / J.83B サポートは [kazuki0824/recisdb-rs#202](https://github.com/kazuki0824/recisdb-rs/pull/202) にて開発中です。  
> recisdb の新バージョンがリリースされるまでは、こちらのブランチを自ビルドした上でご利用ください。

## LNB電源の出力

### PLEX PX-W3U4/Q3U4/W3PE4/Q3PE4

出力なしと 15V の出力のみに対応しています。デフォルトでは LNB 電源の出力を行いません。  
LNB 電源の出力を行うには、recpt1 を実行する際のパラメータに `--lnb 15` を追加してください。

Windows では、BonDriver_PX4-S.ini に記載の `LNBPower=0` を `LNBPower=1` に変更してください。

### PLEX PX-W3PE5/Q3PE5

出力なしと 15V の出力のみに対応しているものと思われます。

### PLEX PX-MLT5PE/MLT8PE

対応していないものとされていましたが、5ch の有志により、正しく LNB 電源を出力できることが確認されています ([参考](https://mevius.5ch.net/test/read.cgi/avi/1648542476/267-288))。

### PLEX PX-M1UR / e-Better DTV02-1T1S-U/DTV02A-1T1S-U

対応しておりません。

### e-Better DTV02A-4TS-P

対応していると思われます。

### e-Better DTV02A-5TS-P

ハードウェア上 PX-MLT5PE と同一機種であることから、LNB 電源の動作も PX-MLT5PE と共通です。

## 備考

### 内蔵カードリーダーやリモコンについて

WinUSB 版と Linux 版のどちらでも、各種対応デバイスに内蔵されているカードリーダーを利用できます (Linux 版は pcsc-lite 用の [IFD ハンドラー](pcsc/README.md) の導入が必要です)。リモコンの操作には対応していません。

### e-Better DTV02-1T1S-U について

e-Better DTV02-1T1S-U は、個体によりデバイスからの応答が無くなることのある不具合が各所にて多数報告されています。そのため、このドライバでは「実験的な対応」とさせていただいております。  
上記の不具合はこの非公式ドライバでも完全には解消できないと思われますので、その点は予めご了承ください。

### e-Better DTV02A-1T1S-U について

e-Better DTV02A-1T1S-U は、DTV02-1T1S-U に存在した上記の不具合がハードウェアレベルで修正されています。そのため、このドライバでは「正式な対応」とさせていただいております。

## 技術情報

### デバイスの構成

PX-W3PE4/Q3PE4/MLT5PE/MLT8PE, e-Better DTV02A-4TS-P/DTV02A-5TS-P は、電源の供給を PCIe スロットから受け、データのやり取りを USB を介して行います。  
PX-W3PE5/Q3PE5 は、PX-W3PE4/Q3PE4 相当の基板に PCIe→USB ブリッジチップを追加し、USB ケーブルを不要とした構造となっています。  
PX-Q3U4/Q3PE4 は、PX-W3U4/W3PE4 相当のデバイスが USB ハブを介して2つぶら下がる構造となっています。

- PX-W3U4/W3PE4

	- USB Bridge: ITE IT9305E
	- ISDB-T/S Demodulator: Toshiba TC90522XBG
	- Terrestrial Tuner: RafaelMicro R850 (x2)
	- Satellite Tuner: RafaelMicro RT710 (x2)

- PX-Q3U4/Q3PE4

	- USB Bridge: ITE IT9305E (x2)
	- ISDB-T/S Demodulator: Toshiba TC90522XBG (x2)
	- Terrestrial Tuner: RafaelMicro R850 (x4)
	- Satellite Tuner: RafaelMicro RT710 (x4)

- PX-W3PE5

	- PCIe to USB Bridge: ASIX MCS9990CV-AA
	- USB Bridge: ITE IT9305E
	- ISDB-T/S Demodulator: Toshiba TC90522XBG
	- Terrestrial Tuner: RafaelMicro R850 (x2)
	- Satellite Tuner: RafaelMicro RT710 (x2)

- PX-Q3PE5

	- PCIe to USB Bridge: ASIX MCS9990CV-AA
	- USB Bridge: ITE IT9305E (x2)
	- ISDB-T/S Demodulator: Toshiba TC90522XBG (x2)
	- Terrestrial Tuner: RafaelMicro R850 (x4)
	- Satellite Tuner: RafaelMicro RT710 (x4)

Demodulator として CXD2856ER が搭載されたデバイスは、ISDB-T/S だけでなく、ISDB-C / J.83 Annex B の受信にも対応しています。  
PX-MLT8PE は、同一基板上に PX-MLT5PE 相当のデバイスと、3チャンネル分のチューナーを持つデバイスが実装されている構造となっています。

- PX-MLT5PE/MLT8PE5 / DTV02A-5TS-P

	- USB Bridge: ITE IT9305E
	- ISDB-T/S/C Demodulator: Sony CXD2856ER (x5)
	- Terrestrial/Satellite Tuner: Sony CXD2858ER (x5)

- PX-MLT8PE3

	- USB Bridge: ITE IT9305E
	- ISDB-T/S/C Demodulator: Sony CXD2856ER (x3)
	- Terrestrial/Satellite Tuner: Sony CXD2858ER (x3)

DTV02-1T1S-U/DTV02A-1T1S-U は、ISDB-T 側の TS シリアル出力を ISDB-S 側と共有しています。そのため、同時に受信できるチャンネル数は1チャンネルのみです。

- DTV02-1T1S-U/DTV02A-1T1S-U (ロット番号 2309 以前: Digibest ISDB2056)

	- USB Bridge: ITE IT9303FN
	- ISDB-T/S Demodulator: Toshiba TC90532XBG
	- Terrestrial Tuner: RafaelMicro R850
	- Satellite Tuner: RafaelMicro RT710

ロット番号 2309 (2023年9月) 以降の DTV02A-1T1S-U では、ISDB-T/S Demodulator IC が TC90532XBG (ISDB-T×1TS + ISDB-S×1TS) から TC90522XBG (ISDB-T×2TS + ISDB-S×2TS) に変更されています。（[詳細](https://web.archive.org/web/20130513083035/http://www.semicon.toshiba.co.jp/product/new_products/assp/1275558_37644.html)）  
この変更に伴い、内部名称が ISDB2056 から ISDB2056N に変更されています。

- DTV02A-1T1S-U (ロット番号 2309 以降: Digibest ISDB2056N)

	- USB Bridge: ITE IT9303FN
	- ISDB-T/S Demodulator: Toshiba TC90522XBG (TC90532XBG -> TC90522XBG)
	- Terrestrial Tuner: RafaelMicro R850
	- Satellite Tuner: RafaelMicro RT710

DTV02A-4TS-P は、PX-MLT5PE から1チャンネル分のチューナーを削減した構造となっています。

- DTV02A-4TS-P (Digibest ISDB6014)

	- USB Bridge: ITE IT9305E
	- ISDB-T/S/C Demodulator: Sony CXD2856ER (x4)
	- Terrestrial/Satellite Tuner: Sony CXD2858ER (x4)

### TS Aggregation の設定

sync_byte をデバイス側で書き換え、ホスト側でその値を元にそれぞれのチューナーの TS データを振り分けるモードを使用しています。
