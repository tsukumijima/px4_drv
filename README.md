# px4_drv - Unofficial Linux / Windows (WinUSB) driver for PLEX PX4/PX5/PX-MLT series ISDB-T/S receivers

PLEX や e-Better から発売された各種 ISDB-T/S チューナー向けの chardev 版非公式 Linux ドライバ / Windows (WinUSB) ドライバです。  
PLEX 社の [Webサイト](http://plex-net.co.jp) にて配布されている公式ドライバとは**別物**です。

## このフォークについて

本家 [nns779/px4_drv](https://github.com/nns779/px4_drv) は 2021 年以降メンテナンスされておらず、残念ながら [nns779](https://github.com/nns779) 氏自身もネット上から失踪されています。  
このフォークは、各々のフォークに散逸していた有用な変更を一つにまとめ、継続的にメンテナンスしていくことを目的としています。

### 変更点 (WinUSB 版)

- エラー発生時の MessageBox を表示しない設定を追加 
  - BonDriver の ini 内の `DisplayErrorMessage` を 1 に設定すると今まで通り MessageBox が表示される
- BS/CS の ChSet に2024年10月～2025年1月に行われた BS トランスポンダ再編後の物理チャンネル情報を反映
- [hendecarows 氏のフォーク](https://github.com/hendecarows/px4_drv) での更新を取り込み、DTV02A-1T1S-U / DTV03A-1T-U / PX-M1UR / PX-S1UR に対応
- PX-Q3PE5 の inf ファイルを追加
- inf ファイルをより分かりやすい名前に変更
- inf ファイルを ARM 版 Windows でもインストールできるようにする
  - 実機がないので試せていないけど、おそらくインストールできるはず
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1625673548/762
- inf ファイルを改善し、チューナーをタスクトレイの「ハードウェアの安全な取り外し」に表示しないようにする
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1666581918/207
- inf ファイルに自己署名を行い、自己署名証明書のインストールだけで正式なドライバーと認識されるように
  - 以前は署名がないためインストール時にドライバー署名の強制の無効化が必要だったが、事前に自己署名証明書を適切な証明書ストアにインストールしておけばこの作業が不要になる
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1577466040/104-108
- 自己署名証明書のインストール・アンインストールスクリプトを追加
  - 拡張子が .jse となっているが、これは PowerShell スクリプトにダブルクリックで実行させるための JScript コードを先頭の行に加えたもの
  - 実際に表示されないかは今のところ未確認
- 地上波の ChSet に物理 53ch ～ 62ch の定義を追加
  - 物理 53ch ～ 62ch は地上波の割り当て周波数から削除されているが、現在も ”イッツコムch10” など、一部ケーブルテレビの自主放送の割り当て周波数として使われている
- BS/CS の ChSet に2022年3月開局の BS 新チャンネル（BS松竹東急・BSJapanext・BSよしもと）の定義を追加
- バージョン情報が DLL のプロパティに表示されないのを修正
- ビルドとパッケージングを全自動で行うスクリプトを追加
  - Visual Studio 2019 が入っていれば、build.ps1 を実行するだけで全自動でビルドからパッケージングまで行える
- README（このページ）に WinUSB 版のインストール方法などを追記

### 変更点 (Linux 版)

動作確認は Ubuntu 20.04 LTS (x64) で行っています。

- チップ構成が一部変更された、ロット番号 2309 (2023年9月) 以降の DTV02A-1T1S-U に対応
- [otya 氏のフォーク](https://github.com/otya128/px4_drv) での更新を取り込み、安定性と互換性を改善
- [techmadot 氏のフォーク](https://github.com/techmadot/px4_drv) の内容を取り込み、PX-M1UR / PX-S1UR に対応
- [kznrluk 氏のフォーク](https://github.com/kznrluk/px4_drv) の内容を取り込み、Linux カーネル 6.4 系以降の API 変更に対応
- https://github.com/tsukumijima/px4_drv/pull/6 をマージし、Linux カーネル 6.8 系以降の API 変更に対応
- https://github.com/tsukumijima/px4_drv/pull/3 をマージし、`ctrl_timeout` をモジュールパラメーターに追加
- Debian パッケージ (.deb) の作成とインストールに対応
- DKMS でのインストール時にファームウェアを自動でインストールするように変更
- README (このページ) に Debian パッケージからのインストール方法などを追記

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
	- DTV03A-1T-U (実験的・Windows 版ドライバのみ)

> [!NOTE]
> 2021 年以降メンテナンスされていない [nns779/px4_drv](https://github.com/nns779/px4_drv) と異なり、新規に下記チューナーのサポートを追加しています。
> 
> - PLEX PX-M1UR
> - PLEX PX-S1UR
> - e-Better DTV02A-1T1S-U / Digibest ISDB2056 (Windows 版ドライバを新規追加)
> - e-Better DTV02A-1T1S-U (ロット番号 2309 以降) / Digibest ISDB2056N
> - e-Better DTV03A-1T-U / Digibest ISDBT2071 (Windows 版ドライバのみ)

## インストール (Windows)

Windows (WinUSB) 版のドライバは、OS にチューナーを認識させるための inf ファイルと、px4_drv 専用の BonDriver、ドライバの実体でチューナー操作を司る DriverHost_PX4 から構成されています。

ビルド済みのアーカイブは [こちら](https://github.com/tsukumijima/DTV-Builds) からダウンロードできます。  
または、winusb フォルダにある build.ps1 を実行して、ご自分でビルドしたものを使うこともできます (Visual Studio 2019 が必要です) 。

### 1. 自己署名証明書のインストール

Driver フォルダには、各機種ごとのドライバのインストールファイル (.inf) が配置されています。  
ドライバをインストールする前に cert-install.jse を実行し、自己署名証明書をインストールしてください。

自己署名証明書をインストールして信頼することで、自己署名証明書を使用して署名されたドライバも、（自己署名証明書をアンインストールするまでは）通常の署名付きドライバと同様に信頼されるようになります。

### 2. ドライバのインストール

自己署名証明書をインストールしたあとは、公式ドライバと同様の手順でインストールできると思います（公式ドライバは事前にアンインストールしてください）。

具体的には、デバイスマネージャーからチューナーデバイスを選択し、右クリックメニューの [ドライバーの更新] → [コンピューターを参照してドライバーを検索] から、inf ファイルが入っている Driver フォルダを指定してください。[ドライバーが正常に更新されました] と表示されていれば OK です。

### 3. BonDriver の配置

チューナーの機種に応じた BonDriver を配置します。

- PX4/PX5 シリーズの機種: `BonDriver_PX4`
- PX-MLT シリーズの機種・DTV02A-4TS-P: `BonDriver_PX-MLT`
- DTV02A-1T1S-U: `BonDriver_ISDB2056`
- DTV02A-1T1S-U (ロット番号 2309 以降): `BonDriver_ISDB2056N`
- DTV03A-1T-U: `BonDriver_ISDBT2071`
- PX-M1UR: `BonDriver_PX-M1UR`
- PX-S1UR: `BonDriver_PX-S1UR`

お使いの PC に合うビット数のフォルダの中のファイルを**すべて**選択し、TVTest や EDCB などのソフトウェアが指定する BonDriver の配置フォルダにコピーします。  
BonDriver と同じフォルダに DriverHost_PX4.exe / DriverHost_PX4.ini / it930x-firmware.bin があることを確認してください。

使用にあたり、特段 ini ファイルの設定変更などは必要ありません。ソフトウェアごとにチャンネルスキャンを行えばそのまま視聴できます。  
なお、BonDriver の ini ファイル内の `DisplayErrorMessage` を 1 に設定すると、オリジナル同様にエラー発生時にメッセージボックスを表示します。

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

一部の Linux ディストリビューションでは、別途 udev のインストールが必要になる場合があります。
	
#### Debian パッケージを使用してインストール (強く推奨)

Debian パッケージを使用してインストールすると依存パッケージも自動インストールされるほか、DKMS のソースコード管理も透過的に行われます。  
Ubuntu / Debian 環境では Debian パッケージを使用してインストールすることを強く推奨します。

	$ wget https://github.com/tsukumijima/px4_drv/releases/download/v0.5.2/px4-drv-dkms_0.5.2_all.deb
	$ sudo apt install -y ./px4-drv-dkms_0.5.2_all.deb

上記コマンドで、px4_drv の Debian パッケージをインストールできます。

> [!TIP]
手動で Debian パッケージを生成することもできます。  
> `./build_deb.sh` を実行すると、`./build_deb.sh` の一つ上層のディレクトリに `px4-drv-dkms_0.5.2_all.deb` という名前の Debian パッケージが生成されます。  
> ```
> $ ./build_deb.sh
> $ sudo apt install -y ../px4-drv-dkms_0.5.2_all.deb
> ```
> 上記コマンドで、生成した px4_drv の Debian パッケージをインストールできます。

#### DKMS を使用してインストールする

gcc, make, カーネルソース/ヘッダ, dkms がインストールされている必要があります。

	$ sudo cp -a ./ /usr/src/px4_drv-0.5.2
	$ sudo dkms add px4_drv/0.5.2
	$ sudo dkms install px4_drv/0.5.2

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

##### PLEX PX-MLT5PE を接続した場合

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

##### e-Better DTV02-1T1S-U/DTV02A-1T1S-U を接続した場合

	$ ls /dev/isdb2056video*
	/dev/isdb2056video0

すべてのチューナーにおいて、ISDB-T と ISDB-S のどちらも受信可能です。

> [!NOTE]  
> ロット番号 2309 以降の DTV02A-1T1S-U を接続した場合も、デバイスファイル名は `/dev/isdb2056video*` となります。  
> `/dev/isdb2056nvideo*` ではないので注意してください。

##### e-Better DTV02A-4TS-P を接続した場合

	$ ls /dev/isdb6014video*
	/dev/isdb6014video0  /dev/isdb6014video2
	/dev/isdb6014video1  /dev/isdb6014video3

すべてのチューナーにおいて、ISDB-T と ISDB-S のどちらも受信可能です。

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

	$ sudo dkms remove px4_drv/0.5.2 --all
	$ sudo rm -rf /usr/src/px4_drv-0.5.2

#### DKMS を使用せずにインストールした場合

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

## LNB電源の出力

### PLEX PX-W3U4/Q3U4/W3PE4/Q3PE4

出力なしと 15V の出力のみに対応しています。デフォルトでは LNB 電源の出力を行いません。  
LNB 電源の出力を行うには、recpt1 を実行する際のパラメータに `--lnb 15` を追加してください。

Windows では、BonDriver_PX4-S.ini に記載の `LNBPower=0` を `LNBPower=1` に変更してください。

### PLEX PX-W3PE5/Q3PE5

出力なしと 15V の出力のみに対応しているものと思われます。

### PLEX PX-MLT5PE/MLT8PE

対応していないものとされていましたが、5ch の有志により、正しく LNB 電源を出力できることが確認されています ([参考](https://mevius.5ch.net/test/read.cgi/avi/1648542476/267-288))。

### e-Better DTV02-1T1S-U/DTV02A-1T1S-U

対応しておりません。

### e-Better DTV02A-4TS-P

対応していると思われます。

## 備考

### 内蔵カードリーダーやリモコンについて

このドライバは、各種対応デバイスに内蔵されているカードリーダーやリモコンの操作には対応していません。  
また、今後対応を行う予定もありません。ご了承ください。

### e-Better DTV02-1T1S-U について

e-Better DTV02-1T1S-U は、個体によりデバイスからの応答が無くなることのある不具合が各所にて多数報告されています。そのため、このドライバでは「実験的な対応」とさせていただいております。  
上記の不具合はこの非公式ドライバでも完全には解消できないと思われますので、その点は予めご了承ください。

### e-Better DTV02A-1T1S-U について

e-better DTV02A-1T1S-U は、DTV02-1T1S-U に存在した上記の不具合がハードウェアレベルで修正されています。そのため、このドライバでは「正式な対応」とさせていただいております。

## 技術情報

### デバイスの構成

PX-W3PE4/Q3PE4/MLT5PE/MLT8PE, e-Better DTV02A-4TS-P は、電源の供給を PCIe スロットから受け、データのやり取りを USB を介して行います。  
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

PX-MLT8PE は、同一基板上に PX-MLT5PE 相当のデバイスと、3チャンネル分のチューナーを持つデバイスが実装されている構造となっています。

- PX-MLT5PE/MLT8PE5

	- USB Bridge: ITE IT9305E
	- ISDB-T/S Demodulator: Sony CXD2856ER (x5)
	- Terrestrial/Satellite Tuner: Sony CXD2858ER (x5)

- PX-MLT8PE3

	- USB Bridge: ITE IT9305E
	- ISDB-T/S Demodulator: Sony CXD2856ER (x3)
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
	- ISDB-T/S Demodulator: Sony CXD2856ER (x4)
	- Terrestrial/Satellite Tuner: Sony CXD2858ER (x4)

### TS Aggregation の設定

sync_byte をデバイス側で書き換え、ホスト側でその値を元にそれぞれのチューナーの TS データを振り分けるモードを使用しています。
