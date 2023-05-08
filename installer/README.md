# 機能
`mkdeb.bash`を実行すると、
- px4-drv.deb
- px4-drv_dkms-***.deb
の２つのファイルが生成されます。

# 生成されたパッケージのインストール
```
sudo apt install px4-drv.deb px4-drv_dkms-***.deb -y
```
とする事でインストールができます。
