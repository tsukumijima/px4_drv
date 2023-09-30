# px4_drv - Unofficial Linux / Windows (WinUSB) driver for PLEX PX4/PX5/PX-MLT series ISDB-T/S receivers

PLEX や e-Better から発売された各種 ISDB-T/S チューナー向けの chardev 版非公式 Linux ドライバ / Windows (WinUSB) ドライバです。  
PLEX 社の [Webサイト](http://plex-net.co.jp) にて配布されている公式ドライバとは**別物**です。

## このフォークについて

### 変更点 (WinUSB 版)

- エラー発生時の MessageBox を表示しない設定を追加 
  - BonDriver の ini 内の `DisplayErrorMessage` を 1 に設定すると今まで通り MessageBox が表示される
- PX-Q3PE5 の inf ファイルを追加
- inf ファイルをより分かりやすい名前に変更
- inf ファイルを ARM 版 Windows でもインストールできるようにする
  - 実機がないので試せていないけど、おそらくインストールできるはず
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1625673548/762
- inf ファイルに自己署名を行い、自己署名証明書のインストールだけで正式なドライバーと認識されるように
  - 以前は署名がないためインストール時にドライバー署名の強制の無効化が必要だったが、事前に自己署名証明書を適切な証明書ストアにインストールしておけばこの作業が不要になる
  - ref: https://mevius.5ch.net/test/read.cgi/avi/1577466040/104-108
- 自己署名証明書のインストール・アンインストールスクリプトを追加
  - 拡張子が .jse となっているが、これは PowerShell スクリプトにダブルクリックで実行させるための JScript コードを先頭の行に加えたもの
- 地上波の ChSet に物理 53ch ～ 62ch の定義を追加
  - 物理 53ch ～ 62ch は地上波の割り当て周波数から削除されているが、現在も ”イッツコムch10” など、一部ケーブルテレビの自主放送の割り当て周波数として使われている
- BS/CS の ChSet に2022年3月開局の BS 新チャンネル（BS松竹東急・BSJapanext・BSよしもと）の定義を追加
- バージョン情報が DLL のプロパティに表示されないのを修正
- ビルドとパッケージングを全自動で行うスクリプトを追加
  - Visual Studio 2019 が入っていれば、build.ps1 を実行するだけで全自動でビルドからパッケージングまで行える
- README（このページ）に WinUSB 版のインストール方法などを追記

### 変更点 (Linux 版)

動作確認は Ubuntu 20.04 LTS (x64) で行っています。

- [otya 氏のフォーク](https://github.com/otya128/px4_drv) での更新を取り込み
- [techmadot 氏のフォーク](https://github.com/techmadot/px4_drv) の内容を取り込み PX-M1UR / PX-S1UR に対応 (実験的)
- [kznrluk 氏のフォーク](https://github.com/kznrluk/px4_drv) の内容を取り込み Linux カーネル 6.4 系以降の API 変更に対応
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
    - PX-M1UR (実験的・Windows 非対応)
    - PX-S1UR (実験的・Windows 非対応)

- e-Better

	- DTV02-1T1S-U (実験的・Windows 非対応)
	- DTV02A-1T1S-U (Windows 非対応)
	- DTV02A-4TS-P

## インストール (Windows)

Windows (WinUSB) 版のドライバは、OS にチューナーを認識させるための inf ファイルと、専用の BonDriver (BonDriver_PX4 / BonDriver_PX-MLT)、ドライバの実体でチューナー操作を司る DriverHost_PX4 から構成されています。

ビルド済みのアーカイブは [こちら](https://github.com/tsukumijima/DTV-Built) からダウンロードできます。または、winusb フォルダにある build.jse を実行して、ご自分でビルドしたものを使うこともできます（ Visual Studio 2019 が必要です）。

### 1. 自己署名証明書のインストール

Driver フォルダには、各機種ごとのドライバのインストールファイル (.inf) が配置されています。  
ドライバをインストールする前に cert-install.jse を実行し、自己署名証明書をインストールしてください。

自己署名証明書をインストールして信頼することで、自己署名証明書を使用して署名されたドライバも、（自己署名証明書をアンインストールするまでは）通常の署名付きドライバと同様に信頼されるようになります。

### 2. ドライバのインストール

自己署名証明書をインストールしたあとは、公式ドライバと同様の手順でインストールできると思います（公式ドライバは事前にアンインストールしてください）。

具体的には、デバイスマネージャーからチューナーデバイスを選択し、右クリックメニューの [ドライバーの更新] → [コンピューターを参照してドライバーを検索] から、inf ファイルが入っている Driver フォルダを指定してください。[ドライバーが正常に更新されました] と表示されていれば OK です。

### 3. BonDriver の配置

PX4/PX5 シリーズの機種では BonDriver_PX4 、PX-MLT シリーズの機種では BonDriver_PX-MLT を使用します。

お使いの PC に合うビット数のフォルダの中のファイルを**すべて**選択し、TVTest や EDCB などのソフトウェアが指定する BonDriver の配置フォルダにコピーします。  
BonDriver と同じフォルダに DriverHost_PX4.exe / DriverHost_PX4.ini / it930x-firmware.bin があることを確認してください。

使用にあたり、特段 ini ファイルの設定変更などは必要ありません。ソフトウェアごとにチャンネルスキャンを行えばそのまま視聴できます。  
なお、BonDriver の ini ファイル内の `DisplayErrorMessage` を 1 に設定すると、オリジナル同様にエラー発生時にメッセージボックスを表示します。

## インストール (Linux)

このドライバを使用する前に、ファームウェアを公式ドライバより抽出しインストールを行う必要があります。

### 1. ファームウェアの抽出とインストール

> **Note**  
> **px4_drv を Debian パッケージや DKMS を使用してインストールする際、ファームウェアは自動的にインストールされます。**  
> Debian パッケージや DKMS を使用してインストールを行う場合は、この手順はスキップしてください。

unzip, gcc, make がインストールされている必要があります。

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

一部の Linux ディストリビューションでは、udev のインストールが別途必要になる場合があります。
	
#### Debian パッケージを使用してインストール (推奨)

	$ wget https://github.com/tsukumijima/px4_drv/releases/download/v0.4.1/px4-drv-dkms_0.4.1_all.deb
	$ sudo apt install -y ./px4-drv-dkms_0.4.1_all.deb

上記コマンドで、px4_drv の Debian パッケージをインストールできます。

手動で Debian パッケージを生成することもできます。  
`./build_deb.sh` を実行すると、./build_deb.sh の一つ上層のディレクトリに `px4-drv-dkms_0.4.1_all.deb` という名前の Debian パッケージが生成されます。  

	$ ./build_deb.sh
	$ sudo apt install -y ../px4-drv-dkms_0.4.1_all.deb

上記コマンドで、生成した px4_drv の Debian パッケージをインストールできます。

#### DKMS を使用してインストールする

gcc, make, カーネルソース/ヘッダ, dkms がインストールされている必要があります。

	$ sudo cp -a ./ /usr/src/px4_drv-0.4.1
	$ sudo dkms add px4_drv/0.4.1
	$ sudo dkms install px4_drv/0.4.1

#### DKMS を使用せずにインストールする

gcc, make, カーネルソース/ヘッダがインストールされている必要があります。

> **Warning**  
> DKMS を使用せずにインストールした場合、カーネルのアップデート時にドライバが自動で再ビルドされないため、アップデート後に再度インストールを行う必要があります。  
> できるだけ DKMS を使用してインストールすることをおすすめします。

	$ cd driver
	$ make
	$ sudo make install
	$ cd ../

### 3. 確認

#### 3.1 カーネルモジュールのロードの確認

下記のコマンドを実行し、`px4_drv` から始まる行が表示されれば、カーネルモジュールが正常にロードされています。

	$ lsmod | grep -e ^px4_drv
	px4_drv                81920  0

何も表示されない場合は、下記のコマンドを実行してから、再度上記のコマンドを実行して確認を行ってください。

	$ sudo modprobe px4_drv

それでもカーネルモジュールが正常にロードされない場合は、インストールから再度やり直してください。

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

	$ sudo dkms remove px4_drv/0.4.1 --all
	$ sudo rm -rf /usr/src/px4_drv-0.4.1

#### DKMS を使用せずにインストールした場合

	$ sudo modprobe -r px4_drv
	$ cd driver
	$ sudo make uninstall
	$ cd ../

### 2. ファームウェアのアンインストール

> **Note**
> **px4_drv を Debian パッケージや DKMS を使用してインストールした場合、ファームウェアは自動的にアンインストールされます。**  
> Debian パッケージや DKMS を使用してインストールを行った場合は、この手順はスキップしてください。

	$ sudo rm /lib/firmware/it930x-firmware.bin

## 受信方法

### Windows

PX4/PX5 シリーズの機種では BonDriver_PX4 、PX-MLT シリーズの機種では BonDriver_PX-MLT を、TVTest や EDCB などの BonDriver に対応したソフトウェアで使用することで、TS データを受信することが可能です。

BonDriver は専用のものが必要になるため、公式 (Jacky版) BonDriver や radi-sh 氏版 BonDriver_BDA と併用することはできません。

### Linux

recpt1 や [BonDriverProxy_Linux](https://github.com/u-n-k-n-o-w-n/BonDriverProxy_Linux) 等の PT シリーズ用 chardev ドライバに対応したソフトウェアを使用することで、TS データを受信することが可能です。  
recpt1 は、PLEX 社より配布されているものを使用する必要はありません。

BonDriverProxy_Linux と、PLEX PX-MLT5PEやe-Better DTV02A-1T1S-U などのデバイスファイル1つで ISDB-T と ISDB-S のどちらも受信可能なチューナーを組み合わせて使用する場合は、BonDriver として BonDriverProxy_Linux に同梱されている BonDriver_LinuxPT の代わりに、[BonDriver_LinuxPTX](https://github.com/nns779/BonDriver_LinuxPTX) を使用してください。

## LNB電源の出力

### PLEX PX-W3U4/Q3U4/W3PE4/Q3PE4

出力なしと 15V の出力のみに対応しています。デフォルトでは LNB 電源の出力を行いません。  
LNB 電源の出力を行うには、recpt1 を実行する際のパラメータに `--lnb 15` を追加してください。  
Windows では、BonDriver_PX4-S.ini の `LNBPower=0` の項目を `LNBPower=1` に変更してください。

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

- DTV02-1T1S-U/DTV02A-1T1S-U

	- USB Bridge: ITE IT9303FN
	- ISDB-T/S Demodulator: Toshiba TC90532XBG
	- Terrestrial Tuner: RafaelMicro R850
	- Satellite Tuner: RafaelMicro RT710

DTV02A-4TS-P は、PX-MLT5PE から1チャンネル分のチューナーを削減した構造となっています。

- DTV02A-4TS-P

	- USB Bridge: ITE IT9305E
	- ISDB-T/S Demodulator: Sony CXD2856ER (x4)
	- Terrestrial/Satellite Tuner: Sony CXD2858ER (x4)

### TS Aggregation の設定

sync_byte をデバイス側で書き換え、ホスト側でその値を元にそれぞれのチューナーの TS データを振り分けるモードを使用しています。
