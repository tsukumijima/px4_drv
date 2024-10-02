# ドライバ自己署名用スクリプト

# スクリプトのディレクトリを取得
$ScriptDir = $PSScriptRoot
$InfPath = Join-Path -Path $ScriptDir -ChildPath "..\inf"

# Inf2Cat の実行
$Inf2CatPath = Join-Path -Path $ScriptDir -ChildPath "Inf2Cat"
& $Inf2CatPath /driver:$InfPath /uselocaltime /os:7_X86,7_X64,Server2008R2_X64,8_X86,8_X64,Server8_X64,6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64,Server10_ARM64,10_RS5_X86,10_RS5_X64,10_RS5_ARM64,ServerRS5_X64,ServerRS5_ARM64

# signtool の実行
$SignToolPath = Join-Path -Path $ScriptDir -ChildPath "signtool"
& $SignToolPath sign /f trustedpub.pfx /p 123 /t http://timestamp.digicert.com "$InfPath/px4_drv_winusb.cat"

# 証明書のコピー
$rootCerPath = Join-Path -Path $ScriptDir -ChildPath "root.cer"
cp $rootCerPath "$InfPath/px4_drv_winusb.cer"
