
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
# GitHub Actions では microsoft/setup-msbuild が PATH を設定するため、その設定を優先する
# ローカル環境では Visual Studio 2022 のエディション差を吸収するため、vswhere で MSBuild を探索する
if ((Get-Command msbuild -ErrorAction SilentlyContinue) -eq $null) {
    $vswhere_path = Join-Path -Path ${env:ProgramFiles(x86)} -ChildPath 'Microsoft Visual Studio\Installer\vswhere.exe'

    if ((Test-Path $vswhere_path) -eq $True) {
        $msbuild_file_path = & $vswhere_path -latest -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1

        if ($msbuild_file_path -ne $null) {
            $msbuild_dir_path = Split-Path $msbuild_file_path -Parent
            $env:PATH = "$env:PATH;$msbuild_dir_path"
        }
    }
}

if ((Get-Command msbuild -ErrorAction SilentlyContinue) -eq $null) {
    throw 'MSBuild was not found. Install Visual Studio 2022 with MSBuild.'
}

# MSBuild を使用してソリューションをビルド
$build_platforms = @('x86', 'x64')
foreach ($build_platform in $build_platforms) {
    msbuild px4_winusb.sln /t:"Rebuild" /p:"Configuration=Release-static;Platform=$build_platform;PlatformToolset=v143"

    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed. platform: $build_platform"
    }

    # Linux 版と WinUSB 版が共有する TS 同期判定を実際の入力値で検証
    msbuild tests/ts_sync_condition_test.vcxproj /t:"Rebuild" /p:"Configuration=Release-static;Platform=$build_platform;PlatformToolset=v143"
    if ($LASTEXITCODE -ne 0) {
        throw "TS sync condition test build failed. platform: $build_platform"
    }
    & 'tests/ts_sync_condition_test.ps1' -Platform $build_platform

    # 実機に依存しないカード抜去・再挿入・接触不良の状態遷移を毎回検証
    & "build/$build_platform/Release-static/smart_card_state_test.exe"
    if ($LASTEXITCODE -ne 0) {
        throw "Smart card state test failed. platform: $build_platform"
    }
}

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

# 各 BonDriver と同じビット数の WinSCard.dll を配置
# WinUSB 版の全対応機種で内蔵カードリーダーを利用できる
Get-ChildItem dist/ -Directory -Filter 'BonDriver_*' | ForEach-Object {
    $win_scard_platform = if ($_.Name.EndsWith('_32bit')) { 'x86' } else { 'x64' }
    Copy-Item "build/$win_scard_platform/Release-static/WinSCard.dll" $_.FullName
}

# inf ファイルをコピー
Copy-Item -Recurse pkg/inf/ dist/Driver

Write-Host '    '
Write-Host '    '
Write-Host '    BonDriver のビルドとパッケージングを完了しました。'
Write-Host '    ビルドしたファイルは dist/ フォルダに配置されています。'
Write-Host '    '
Start-Sleep -Seconds 5
