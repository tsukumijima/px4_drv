
# カレントディレクトリに移動
if ($MyInvocation.MyCommand.Path -ne $null) {
    $CurrentPath = (Split-Path $MyInvocation.MyCommand.Path -Parent)
} else {
    $CurrentPath = $PSScriptRoot
}
Set-Location $CurrentPath

# コンソールウインドウのサイズを変更
$ErrorActionPreference = 'Stop'  # エラーを確実に catch する
try { (Get-Host).UI.RawUI.BufferSize = New-Object System.Management.Automation.Host.Size(120,25) } catch {}
try { (Get-Host).UI.RawUI.WindowSize = New-Object System.Management.Automation.Host.Size(120,25) } catch {}

# MSBuild のパスを環境変数 PATH に追加
$msbuild_path = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin'
$env:PATH = "$env:PATH;$msbuild_path"

# MSBuild を使用してソリューションをビルド
msbuild px4_winusb.sln /t:"Rebuild" /p:"Configuration=Release-static;Platform=x86;PlatformToolset=v142"
msbuild px4_winusb.sln /t:"Rebuild" /p:"Configuration=Release-static;Platform=x64;PlatformToolset=v142"

# dist/ フォルダにビルドされたファイルをコピー
# フォルダの作成
Remove-Item -Recurse -Force dist/ -ErrorAction SilentlyContinue
New-Item -ItemType Directory dist/
New-Item -ItemType Directory dist/BonDriver_PX4_32bit
New-Item -ItemType Directory dist/BonDriver_PX4_64bit
New-Item -ItemType Directory dist/BonDriver_PX-MLT_32bit
New-Item -ItemType Directory dist/BonDriver_PX-MLT_64bit
New-Item -ItemType Directory dist/BonDriver_ISDB2056_32bit
New-Item -ItemType Directory dist/BonDriver_ISDB2056_64bit
New-Item -ItemType Directory dist/BonDriver_ISDB2056N_32bit
New-Item -ItemType Directory dist/BonDriver_ISDB2056N_64bit
New-Item -ItemType Directory dist/BonDriver_ISDBT2071_32bit
New-Item -ItemType Directory dist/BonDriver_ISDBT2071_64bit
New-Item -ItemType Directory dist/BonDriver_PX-M1UR_32bit
New-Item -ItemType Directory dist/BonDriver_PX-M1UR_64bit
New-Item -ItemType Directory dist/BonDriver_PX-S1UR_32bit
New-Item -ItemType Directory dist/BonDriver_PX-S1UR_64bit

# BonDriver_PX4_32bit のファイルをコピー
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_PX4_32bit/BonDriver_PX4-T.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ini dist/BonDriver_PX4_32bit/BonDriver_PX4-T.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX4_32bit/BonDriver_PX4-T.ChSet.txt
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_PX4_32bit/BonDriver_PX4-S.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ini dist/BonDriver_PX4_32bit/BonDriver_PX4-S.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_PX4_32bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x86/Release-static/DriverHost_PX4.exe dist/BonDriver_PX4_32bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX4_32bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX4_32bit/it930x-firmware.bin

# BonDriver_PX4_64bit のファイルをコピー
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_PX4_64bit/BonDriver_PX4-T.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ini dist/BonDriver_PX4_64bit/BonDriver_PX4-T.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX4_64bit/BonDriver_PX4-T.ChSet.txt
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_PX4_64bit/BonDriver_PX4-S.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ini dist/BonDriver_PX4_64bit/BonDriver_PX4-S.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_PX4_64bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x64/Release-static/DriverHost_PX4.exe dist/BonDriver_PX4_64bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX4_64bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX4_64bit/it930x-firmware.bin

# BonDriver_PX-MLT_32bit のファイルをコピー
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_PX-MLT_32bit/BonDriver_PX-MLT.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX-MLT.ini dist/BonDriver_PX-MLT_32bit/BonDriver_PX-MLT.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX-MLT_32bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_PX-MLT_32bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x86/Release-static/DriverHost_PX4.exe dist/BonDriver_PX-MLT_32bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX-MLT_32bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX-MLT_32bit/it930x-firmware.bin

# BonDriver_PX-MLT_64bit のファイルをコピー
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_PX-MLT_64bit/BonDriver_PX-MLT.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX-MLT.ini dist/BonDriver_PX-MLT_64bit/BonDriver_PX-MLT.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX-MLT_64bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_PX-MLT_64bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x64/Release-static/DriverHost_PX4.exe dist/BonDriver_PX-MLT_64bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX-MLT_64bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX-MLT_64bit/it930x-firmware.bin

# BonDriver_ISDB2056_32bit のファイルをコピー
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_ISDB2056_32bit/BonDriver_ISDB2056.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_ISDB2056.ini dist/BonDriver_ISDB2056_32bit/BonDriver_ISDB2056.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_ISDB2056_32bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_ISDB2056_32bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x86/Release-static/DriverHost_PX4.exe dist/BonDriver_ISDB2056_32bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_ISDB2056_32bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_ISDB2056_32bit/it930x-firmware.bin

# BonDriver_ISDB2056_64bit のファイルをコピー
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_ISDB2056_64bit/BonDriver_ISDB2056.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_ISDB2056.ini dist/BonDriver_ISDB2056_64bit/BonDriver_ISDB2056.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_ISDB2056_64bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_ISDB2056_64bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x64/Release-static/DriverHost_PX4.exe dist/BonDriver_ISDB2056_64bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_ISDB2056_64bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_ISDB2056_64bit/it930x-firmware.bin

# BonDriver_ISDB2056N_32bit のファイルをコピー
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_ISDB2056N_32bit/BonDriver_ISDB2056N.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_ISDB2056N.ini dist/BonDriver_ISDB2056N_32bit/BonDriver_ISDB2056N.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_ISDB2056N_32bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_ISDB2056N_32bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x86/Release-static/DriverHost_PX4.exe dist/BonDriver_ISDB2056N_32bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_ISDB2056N_32bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_ISDB2056N_32bit/it930x-firmware.bin

# BonDriver_ISDB2056N_64bit のファイルをコピー
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_ISDB2056N_64bit/BonDriver_ISDB2056N.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_ISDB2056N.ini dist/BonDriver_ISDB2056N_64bit/BonDriver_ISDB2056N.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_ISDB2056N_64bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_ISDB2056N_64bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x64/Release-static/DriverHost_PX4.exe dist/BonDriver_ISDB2056N_64bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_ISDB2056N_64bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_ISDB2056N_64bit/it930x-firmware.bin

# BonDriver_PX-M1UR_32bit のファイルをコピー
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_PX-M1UR_32bit/BonDriver_PX-M1UR.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX-M1UR.ini dist/BonDriver_PX-M1UR_32bit/BonDriver_PX-M1UR.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX-M1UR_32bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_PX-M1UR_32bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x86/Release-static/DriverHost_PX4.exe dist/BonDriver_PX-M1UR_32bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX-M1UR_32bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX-M1UR_32bit/it930x-firmware.bin

# BonDriver_PX-M1UR_64bit のファイルをコピー
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_PX-M1UR_64bit/BonDriver_PX-M1UR.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX-M1UR.ini dist/BonDriver_PX-M1UR_64bit/BonDriver_PX-M1UR.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX-M1UR_64bit/BonDriver_PX4-T.ChSet.txt
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-S.ChSet.txt dist/BonDriver_PX-M1UR_64bit/BonDriver_PX4-S.ChSet.txt
Copy-Item build/x64/Release-static/DriverHost_PX4.exe dist/BonDriver_PX-M1UR_64bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX-M1UR_64bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX-M1UR_64bit/it930x-firmware.bin

# BonDriver_ISDBT2071_32bit のファイルをコピー
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_ISDBT2071_32bit/BonDriver_ISDBT2071.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_ISDBT2071.ini dist/BonDriver_ISDBT2071_32bit/BonDriver_ISDBT2071.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_ISDBT2071_32bit/BonDriver_PX4-T.ChSet.txt
Copy-Item build/x86/Release-static/DriverHost_PX4.exe dist/BonDriver_ISDBT2071_32bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_ISDBT2071_32bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_ISDBT2071_32bit/it930x-firmware.bin

# BonDriver_ISDBT2071_64bit のファイルをコピー
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_ISDBT2071_64bit/BonDriver_ISDBT2071.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_ISDBT2071.ini dist/BonDriver_ISDBT2071_64bit/BonDriver_ISDBT2071.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_ISDBT2071_64bit/BonDriver_PX4-T.ChSet.txt
Copy-Item build/x64/Release-static/DriverHost_PX4.exe dist/BonDriver_ISDBT2071_64bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_ISDBT2071_64bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_ISDBT2071_64bit/it930x-firmware.bin

# BonDriver_PX-S1UR_32bit のファイルをコピー
Copy-Item build/x86/Release-static/BonDriver_PX4.dll dist/BonDriver_PX-S1UR_32bit/BonDriver_PX-S1UR.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX-S1UR.ini dist/BonDriver_PX-S1UR_32bit/BonDriver_PX-S1UR.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX-S1UR_32bit/BonDriver_PX4-T.ChSet.txt
Copy-Item build/x86/Release-static/DriverHost_PX4.exe dist/BonDriver_PX-S1UR_32bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX-S1UR_32bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX-S1UR_32bit/it930x-firmware.bin

# BonDriver_PX-S1UR_64bit のファイルをコピー
Copy-Item build/x64/Release-static/BonDriver_PX4.dll dist/BonDriver_PX-S1UR_64bit/BonDriver_PX-S1UR.dll
Copy-Item pkg/BonDriver_PX4/BonDriver_PX-S1UR.ini dist/BonDriver_PX-S1UR_64bit/BonDriver_PX-S1UR.ini
Copy-Item pkg/BonDriver_PX4/BonDriver_PX4-T.ChSet.txt dist/BonDriver_PX-S1UR_64bit/BonDriver_PX4-T.ChSet.txt
Copy-Item build/x64/Release-static/DriverHost_PX4.exe dist/BonDriver_PX-S1UR_64bit/DriverHost_PX4.exe
Copy-Item pkg/DriverHost_PX4/DriverHost_PX4.ini dist/BonDriver_PX-S1UR_64bit/DriverHost_PX4.ini
Copy-Item pkg/DriverHost_PX4/it930x-firmware.bin dist/BonDriver_PX-S1UR_64bit/it930x-firmware.bin

# inf ファイルをコピー
Copy-Item -Recurse pkg/inf/ dist/Driver

Write-Host '    '
Write-Host '    '
Write-Host '    BonDriver のビルドとパッケージングを完了しました。'
Write-Host '    ビルドしたファイルは dist/ フォルダに配置されています。'
Write-Host '    '
Start-Sleep -Seconds 5
