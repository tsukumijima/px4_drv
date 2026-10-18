# ISDB-C (トランスモジュレーション) 対応 (実験的)

J.83 Annex B / 256QAM / 5.6 Msps の実験経路は、[J.83B 実験対応](j83b_winusb_experimental.md) を参照してください。以下は Annex C の受信条件です。

PX-MLT 系チューナーに、CATV のトランスモジュレーション方式（ISDB-C / ITU-T J.83 Annex C、以下「トラモジ」）の受信機能を追加した実験版です。

ベースは [tsukumijima/px4_drv](https://github.com/tsukumijima/px4_drv) です。本リポジトリは PLEX / e-Better の公式ドライバではなく、公式ドライバと併用するものでもありません。

> [!WARNING]
> この版の ISDB-C 復調初期化は、公開されているCXD2856ER向けLinuxドライバを参考にした実験的な実装です。CATV環境で受信を確認していますが、J.83Cへの規格適合性を検証したものではなく、すべてのCATV局・周波数・機種での動作を保証しません。
Linux版のISDB-C機能は実機未検証です。通常のISDB-C TS受信経路のみを実験的に含めており、WinUSB版のTSMF Relative stream抽出およびTLV生出力は未対応です。

## 対応範囲

| 機種 | チップ構成 | ISDB-C の状況 |
| --- | --- | --- |
| PX-MLT5PE | CXD2856ER / CXD2858ER ×5 | 実機で受信確認済み |
| DTV02A-5TS-P | PX-MLT5PE互換 ×5 | ソース上で対応、実機未確認 |
| PX-MLT8PE | 同一筐体内に3チューナー側と5チューナー側 | ソース上で対応、実機未確認 |
| DTV02A-4TS-P | CXD2856ER / CXD2858ER ×4 | 実機で受信確認済み |

上記以外のチューナーは、このトラモジ機能の対象外です。

## 実装している受信条件

- 変調方式: 64QAM / 256QAM（WinUSB版・特定環境で受信確認
- シンボルレート: 5274 ksym/s
- 帯域幅: 6 MHz
- CATV C13～C63 と、UHF帯のトラモジ再送信
- TSMF: Relative stream 番号を指定したTSMFストリームの抽出

シンボルレートと帯域幅は現時点では固定です。WinUSB版は既定で64QAMを先に試し、ロックしなかった場合に256QAM / 5274 ksym/sで再試行します。ただし64QAMとして誤ロックする256QAMチャンネルがあるため、ChSetでチャンネルごとに64または256QAMを明示指定できます。256QAMの動作は公開ドライバとの比較に基づく実験的なものです。

WinUSB版の `[DeviceDefinitionN.Config]` にある `ISDBCReferenceProfile` は、Sony/TBS の公開 DVB-C 6 MHz 参照値と比較する診断用設定です。既定値は `false` で、従来の5274 ksym/s要求に対するプロファイルを維持します。`true` では対象デバイスのISDB-C選局に参照のチューナー・復調器設定を使い、64QAMと256QAMの双方へ適用します。

J.83Bはこの設定の選択によらず専用の初期化列を使います。参照値への一致だけでRF品質や機種への適合は判断できず、全機種・搬送波・長時間利用の妥当性は未確認です。比較には専用の検証環境で同一受信機・同一条件による変更前後の記録が必要です。

## Windows (WinUSB)

### RFチューナーの停止・切替

CXD2858ERのISDB-Cは地上波側RF経路を使用します。終了時の
`cxd2858er_stop_t()`はISDB-TとISDB-Cの双方を受け付け、ISDB-Cから
ISDB-Sへ切り替える場合も既存の地上波側停止列を実行します。
`cxd2858er_stop()`は停止列のI2Cエラーを呼び出し元へ返します。
レジスタ値や校正待機時間の追加・変更はありません。

模擬I2CテストではT/Cの停止、Cの終了、T/CからSへの切替と停止列の
各I/O失敗を確認します。この検証はRF感度、復調品質、USB転送品質を
保証するものではなく、Linux版の実機検証も対象外です。

`BonDriver_PX-MLT.dll` ひとつで地デジ・BS/CS・トラモジを切り替えられます。同じDLLを複数のTVTestやEDCBプロセスから開いた場合、空いているチューナーが割り当てられます。

例: PX-MLT5PEでは「地デジ3番組 + トラモジ2番組」、PX-MLT8PEでは合計8番組の同時利用が可能です。実際に同時受信できる番組数は、アンテナ配線、USB帯域、PC性能、信号品質にも依存します。

### 配置

ビルド後に生成される `BonDriver_PX-MLT_64bit` または `BonDriver_PX-MLT_32bit` フォルダの内容を、TVTest / EDCB が使用するBonDriverフォルダへ**すべて**コピーしてください。

最低限、次のファイルは同じフォルダに必要です。

```text
BonDriver_PX-MLT.dll
BonDriver_PX-MLT.ini
BonDriver_PX4-C.ChSet.txt
BonDriver_PX4-S.ChSet.txt
BonDriver_PX4-T.ChSet.txt
DriverHost_PX4.exe
DriverHost_PX4.ini
it930x-firmware.bin
```

スクランブル解除に内蔵カードリーダーを使う場合は、同梱の `WinSCard.dll` をTVTestやEDCBの実行ファイルと同じフォルダへ配置してください。DLLのビット数はアプリケーションに合わせます。

WinUSBドライバのインストール方法、自己署名証明書、B-CASカードリーダーの詳細は、[README](../README.md) を参照してください。

### トラモジChSet

トラモジのChSetは `BonDriver_PX4-C.ChSet.txt` です。

各行の項目は次のとおりです。

```text
名前 / 空間番号 / チャンネル番号 / PX-MLT物理チャンネル番号 / TSID /
任意のRF中心周波数[kHz] / 任意の周波数補正[kHz] /
任意のTSMF Relative stream番号（0～14）または変調方式（64 / 256）または通常TS用変調方式（64 / 256） /
任意のTSMF slot offset / 任意のTSMF用変調方式（64 / 256） / 任意のTSMF出力形式（1=生TLV）
```

- CATV Cチャンネルでは、6番目を `0` にすると物理チャンネル番号から中心周波数を計算します。
- UHF・VHF帯はCチャンネルと番号体系が異なるため、6番目に中心周波数を指定します。
- 7番目は受信周波数の補正値です。放送側の設定ではありません。
- 8番目に`0`～`14`を指定した行はTSMFとして扱います。`0` はRelative stream 0を意味します。
- 8番目に`64`または`256`を指定すると、通常チャンネルの変調方式を固定します。例: `15Ch    1    2    65    0    485000    -625    256`。
- TSMFチャンネルにも変調方式を固定する場合は、9番目にTSMF slot offset、10番目に`64`または`256`を指定します。
- 11番目に`1`を指定すると、TSMF内のTLV型ストリーム（主にBS4Kなど）を生TLVで出力します。通常のチャンネルでは使用しないでください。これは `BonDriver_dantto4k.dll` の内側BonDriver用の実験機能であり、 `BonDriver_PX-MLT.dll` をTVTest / EDCBから直接開く通常用途では指定しません。

ISDB-C の第8列は 0〜14・64・256、TSMF 用の第9列は -51〜51、第10列は 0・64・256 が有効です。  
それ以外の値がある行は読み飛ばされます。

周波数補正は、映る範囲の中央を目安に25 kHz刻みで設定してください。たとえば `-550` が中心付近なら、ロックしないときは `-525`、`-575` のように調整します。補正値は受信環境（CATV局）ごとに異なる可能性があります。


### nullパケット除去

PID 0x1FFF のnullパケットを除去します。TSMFにも対応しています。


```ini
DiscardNullPackets=true
```

`false` にすると、TSMF・通常チャンネルのどちらでもnullパケットをそのまま出力します。

### TSMFヘッダ PID `0x002F` の除去

`0x002F` はTSMFの多重フレームヘッダです。`DiscardTsmfHeaderPackets=true` にすると、ISDB-C出力だけからこのPIDを除去します。

```ini
DiscardTsmfHeaderPackets=true
```

TSMFチャンネルではヘッダを解析して52スロットの対象TSを抽出した後、非TSMFチャンネルでは出力直前に除去します。地デジ・BS/CSには適用されません。入力側では常に保持されるため、TSMFの処理時は問題ありません。

### ログ

`DriverHost_PX4.ini` の以下でログを制御できます。

```ini
[DriverHost_PX4]
DebugLog=true
DebugLogPath="DriverHost_PX4.log"
```

`DebugLog=false` にするとログを作成しません。ログには選局した方式・周波数・復調ロック状態が出力されます。

BonDriver の選局ログは、読み込む DLL と同名の INI ファイルで有効にできます。

```ini
[BonDriver]
DebugLog=true
DebugLogPath="BonDriver_PX-MLT.log"
```

`DebugLog` の既定値は `false` です。  
`DebugLogPath` を省略した場合は DLL と同じフォルダの `<DLL名>.log` に出力され、相対パスを指定した場合も DLL のフォルダが基準になります。  
ログファイルは開くたびに作り直されます。

## Linux

> [!WARNING]

> Linux版のISDB-C機能は実機未検証です。通常のISDB-C TS受信経路のみを実験的に含めており、WinUSB版のTSMF Relative stream抽出およびTLV生出力は未対応です。現時点では利用手順を案内する対象ではありません。

## ビルド

### Windows

Visual Studio 2022 / 2026で、次のコンポーネントをインストールしてから実行します。

- **C++によるデスクトップ開発** ワークロード
- MSBuild
- Windows 10 または Windows 11 SDK
- MSVC v143 C++ ビルドツール

> [!IMPORTANT]
> `build.ps1` は右クリックの「PowerShellで実行」でも起動できますが、失敗時にウィンドウが閉じて原因を読めません。下記のPowerShellから実行する方法を推奨します。

リポジトリの一番上のフォルダでPowerShellを開き、次の**2行を順番に**実行します。

```powershell
Set-Location .\winusb
.\build.ps1
```

1行で実行する場合は、コマンドを `;` で区切ります。

```powershell
Set-Location .\winusb; .\build.ps1
```

実行ポリシーで止まる場合は、リポジトリの一番上のフォルダから次を実行します。

```powershell
powershell -ExecutionPolicy Bypass -File .\winusb\build.ps1
```

ダブルクリックで実行したい場合は、`winusb\build.cmd` を使ってください。成功・失敗にかかわらず最後に画面を停止するため、エラーメッセージを確認できます。

`Set-Location .\winusb .\build.ps1` のように、2つのコマンドを空白だけで続けて書くことはできません。`Set-Location` が `build.ps1` を余分な引数として扱うため失敗します。

`winusb/dist/BonDriver_PX-MLT_64bit` と `winusb/dist/BonDriver_PX-MLT_32bit` に配布用ファイルが生成されます。


## 謝辞・ライセンス

ベース実装は [nns779/px4_drv](https://github.com/nns779/px4_drv) および [tsukumijima/px4_drv](https://github.com/tsukumijima/px4_drv) に基づきます。CXD2856ERのISDB-C初期化は、LinuxメインラインのSony復調器ドライバを比較資料として利用しています。

ライセンスは [GPL-2.0-only](../LICENSE) です。
