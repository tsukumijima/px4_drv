
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

$env:PATH = "$PATH;C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin"
MSBuild.exe px4_winusb.sln /t:"Rebuild" /p:"Configuration=Release-static;Platform=x86;PlatformToolset=v142"
MSBuild.exe px4_winusb.sln /t:"Rebuild" /p:"Configuration=Release-static;Platform=x64;PlatformToolset=v142"

# dist/ フォルダにビルドされたファイルをコピー
# フォルダの作成
Remove-Item -Recurse -Force dist/ -ErrorAction SilentlyContinue
New-Item -ItemType Directory dist/
New-Item -ItemType Directory dist/BonDriver_PX4_32bit
New-Item -ItemType Directory dist/BonDriver_PX4_64bit
New-Item -ItemType Directory dist/BonDriver_PX-MLT_32bit
New-Item -ItemType Directory dist/BonDriver_PX-MLT_64bit

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

# inf ファイルをコピー
Copy-Item -Recurse pkg/inf/ dist/Driver

Write-Host '    '
Write-Host '    '
Write-Host '    BonDriver_PX4・BonDriver_PX-MLT のビルドとパッケージングを完了しました。'
Write-Host '    ビルドしたファイルは dist/ フォルダに配置されています。'
Write-Host '    '
(Get-Host).UI.RawUI.ReadKey('NoEcho,IncludeKeyDown') | Out-Null
