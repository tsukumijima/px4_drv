# Windows WinUSB の J.83B 5.6 Msps 実験対応

PX-MLT 系の CXD2856ER 復調器・CXD2858ER チューナーに、J.83 Annex B / 256QAM / 5.6 Msps の実験用受信経路を追加しています。Windows WinUSB / PX-MLT8PE / フレッツ・テレビの同軸入力では、一部の搬送波で短時間の TS 受信を確認しました。搬送波によって TS 破損や未同期が残り、全サービス、全機種、長時間録画の動作は未確認です。

既存の ISDB-C（Annex C）経路とは異なる初期化を使います。J.83B 専用の公開システム種別や BonDriver ABI は追加しておらず、Windows ホストが既存の ISDB-C 要求を置き換える暫定実装です。共有 C ソースに J.83B 処理がありますが、Linux デバイス側から有効化する経路や Linux の実機受信は今回の対応範囲に含まれていません。

## 検証環境と録画の保護

実験は、別 PC など本番の DriverHost_PX4・録画環境から分離を確認できる専用環境で、使用していない受信機を明示的に確保して行ってください。稼働中の録画を保護するため、本番のドライバー、BonDriver、EDCB 設定を置き換えたり、既存ホスト・録画プロセスを停止したりすることは試験手順に含めません。空き受信機を安全に確保できない場合は、模擬試験までが検証可能な範囲です。

DriverHost_PX4 は固定のグローバル mutex と名前付きパイプを使用します。別フォルダーへコピーしたことや `DriverHostPath` を変更したことだけでは、本番ホストとの分離を保証できません。新しいホストの起動やデバイス再読み込みの影響も、実施前に確認が必要です。

PX-MLT8PE は同一筐体内に 3 チューナー側と 5 チューナー側の USB デバイスを持ちます。以下の設定は `DeviceDefinitionN` 単位であり、その USB デバイスに属するすべての受信機へ適用されます。同じデバイス内で J.83C と J.83B を受信機ごとに使い分ける設定ではありません。

## ホスト設定

専用検証環境の `DriverHost_PX4.ini` で、対象となる PXMLT の既存 `[DeviceDefinitionN.Config]` セクションへ次の値を追加します。`N` は実際の定義番号に置き換え、同じセクションを重複して作らないでください。

```ini
[DeviceDefinitionN.Config]
ExperimentalJ83B=true
ExperimentalJ83BMonitor=false
```

`ExperimentalJ83B` と `ExperimentalJ83BMonitor` の既定値は `false` です。配布用 INI も J.83B を自動で有効にしません。

J.83B が有効な対象デバイスでは、ISDB-C 選局要求の変調方式・シンボルレート・帯域幅を **256QAM / 5600 ksym/s / 6 MHz** に固定します。要求側の QAM 指定、`ForceISDBCModulation`、既存の 64→256QAM 自動再試行は、この固定条件の選択に使われません。TSMF mode、Relative stream、slot offset、TLV 出力の有効な要求は、ハードウェア操作前にエラーとして拒否します。

ISDB-T / ISDB-S 要求は J.83B への置換対象外です。J.83B 終了・切り替え時の復帰処理は模擬 I2C 試験で確認していますが、共有電源・USB・制御バスの長時間同時利用が安全だと証明したものではありません。

## BonDriver の選局例

既存の `BonDriver_PX-MLT.ini` が読み込む ISDB-C ChSet を、検証環境専用のファイルに変更します。ISDB-C を含む既存の受信機定義も必要です。

```ini
[BonDriver.ISDB-C]
ChSetPath="BonDriver_PX4-J83B.ChSet.txt"
```

次は中心周波数を明示する最小例です。各項目は**タブ区切り**です。この周波数がすべての地域・回線で受信できることを保証する例ではありません。

```text
$J.83B(sample)	0
272.750MHz	0	0	3	0	272750	0	256
567.750MHz	0	1	3	0	567750	0	256
```

列は、名前 / 空間番号 / チャンネル番号 / PX-MLT 物理チャンネル番号 / TSID / RF 中心周波数 kHz / 周波数補正 kHz / 変調方式の順です。非ゼロの第 6 列があると中心周波数を直接使うため、この例の物理チャンネル番号 `3` から周波数を計算する経路には入りません。

第 7 列の `0` は、BonDriver INI の全体補正より優先される明示的な補正値です。既存 J.83C の ChSet にある負の補正をそのまま流用すると、目的の中心周波数からずれるため、最初の比較では補正を `0` に固定してください。

第 8 列の `256` は通常 TS の QAM 指定です。J.83B 側では 256QAM に固定されますが、ChSet の解析時に TSMF と区別するため、この形式を使っています。第 8 列の `0` は「TSMF 無効」ではなく Relative stream 0 を意味し、J.83B では拒否されます。TSMF 用の後続列は不要です。

## 診断と限界

`ExperimentalJ83BMonitor=true` にすると、CNR 要求時に内部状態の生値をログへ出力します。診断には `DriverHost_PX4` のログ出力設定も必要です。

J.83B の CNR 公開 API は未対応を返します。表示上の 0 を測定済みの SNR や受信失敗の確定値として扱うことはできません。IF AGC と C/N 推定器の生値は校正された dBm / MER / SNR ではなく、無効な計数もエラー率 0 とは区別する必要があります。

モニター読み取りは共有制御バスを使い、同期時 19 回、未同期時 16 回の I/O を伴います。低頻度で取得し、正常搬送波と異常搬送波を同一受信機・同一条件で比較することが診断の前提です。monitor ON/OFF の同条件比較、長時間録画への影響、外部減衰・同軸経路の比較は未確認です。

オフラインの `winusb/tests/j83b_driver_test.vcxproj` は、J.83B 設定・ロック判定・復帰、C チューナー停止、各 I/O 失敗境界、モニターのマスク・符号・cleanup を模擬 I2C で検証します。`Release-static` の `Win32` / `x64` を個別にビルドし、生成されたテストを実行してください。模擬試験は RF 校正、実機受信品質、共有負荷の検証を代替しません。

Annex C の説明は [ISDB-C 実験対応](isdbc_winusb_experimental.md) を参照してください。

## 公開前のオフライン検証（2026-10-12）

Visual Studio 2022 / MSVC v143 / `Release-static` / x64・x86 で、公開対象のソースからソリューションと以下の6試験をビルドし、両アーキテクチャで6試験が成功しました。

| 対象 | x64 | x86 |
| --- | --- | --- |
| `px4_winusb.sln` | ビルド成功 | ビルド成功 |
| `ts_sync_condition_test` | 成功 | 成功 |
| `smart_card_state_test` | 成功 | 成功 |
| `ringbuffer_purge_test` | 成功 | 成功 |
| `j83b_driver_test` | 成功 | 成功 |
| `bon_ts_copy_test` | 成功 | 成功 |
| `stream_transfer_test` | 成功 | 成功 |

新しい `j83b_driver_test` / `bon_ts_copy_test` / `stream_transfer_test` は、現行のソリューションと `build.ps1` の実行対象外です。次の手順は、それらと個別プロジェクトのTS同期試験を明示的にビルドし、6試験を実行します。MSBuildを使える Visual Studio 2022 の Developer PowerShell で、リポジトリのルートから実行してください。ソリューションのプラットフォーム名は `x86` / `x64`、個別試験プロジェクトは `Win32` / `x64`、生成先は `winusb/build/x86` / `x64` です。

```powershell
$taskExtraTests = @('ts_sync_condition_test', 'j83b_driver_test',
    'bon_ts_copy_test', 'stream_transfer_test')
$taskAllTests = @('ts_sync_condition_test', 'smart_card_state_test',
    'ringbuffer_purge_test', 'j83b_driver_test',
    'bon_ts_copy_test', 'stream_transfer_test')
foreach ($taskBuildPlatform in @('x64', 'x86')) {
    msbuild .\winusb\px4_winusb.sln /t:Rebuild "/p:Configuration=Release-static;Platform=$taskBuildPlatform;PlatformToolset=v143"
    if ($LASTEXITCODE -ne 0) { throw 'Solution build failed' }
    $taskProjectPlatform = if ($taskBuildPlatform -eq 'x86') { 'Win32' } else { 'x64' }
    foreach ($taskTest in $taskExtraTests) {
        msbuild ".\winusb\tests\$taskTest.vcxproj" /t:Rebuild "/p:Configuration=Release-static;Platform=$taskProjectPlatform;PlatformToolset=v143"
        if ($LASTEXITCODE -ne 0) { throw "Test build failed: $taskTest" }
    }
    foreach ($taskTest in $taskAllTests) {
        & ".\winusb\build\$taskBuildPlatform\Release-static\$taskTest.exe"
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $taskTest" }
    }
}
```

検証対象は模擬I2C、模擬カード、メモリー内のTS処理とプロセス固有のfixtureパイプです。ビルドした DriverHost_PX4 の起動、本番ドライバー・EDCB設定の変更、録画プロセスの停止、使用中受信機の選局など、本番環境に対する実機操作は行っていません。実機や本番ホストのパイプへ接続する `channel_purge_test`、カード・受信のreliabilityツールは、この手順の対象外です。

Linuxのビルド・実機受信、RF品質・校正、全搬送波の受信、モニターON/OFFの同条件比較、長時間の実機共有負荷は未検証です。オフライン試験の成功は、これらの受信条件を確認した結果とは区別する必要があります。
