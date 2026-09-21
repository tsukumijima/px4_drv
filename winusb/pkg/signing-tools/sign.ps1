# ドライバ自己署名用スクリプト

# スクリプトのディレクトリを取得
$ScriptDir = $PSScriptRoot
$InfPath = Join-Path -Path $ScriptDir -ChildPath "..\inf"

# カタログファイルは INF の改行を含むバイト列へ署名するため、作成方法や Git の設定にかかわらず CRLF・BOM なしへ揃える
Get-ChildItem -LiteralPath $InfPath -Filter '*.inf' -File | ForEach-Object {
    $source_bytes = [System.IO.File]::ReadAllBytes($_.FullName)
    $source_start_index = 0
    if ($source_bytes.Length -ge 3 -and $source_bytes[0] -eq 0xef -and $source_bytes[1] -eq 0xbb -and $source_bytes[2] -eq 0xbf) {
        $source_start_index = 3
    } elseif ($source_bytes.Length -ge 2 -and (($source_bytes[0] -eq 0xff -and $source_bytes[1] -eq 0xfe) -or ($source_bytes[0] -eq 0xfe -and $source_bytes[1] -eq 0xff))) {
        throw "UTF-16 driver INF is not supported: $($_.FullName)"
    }

    $normalized_stream = [System.IO.MemoryStream]::new()
    try {
        for ($index = $source_start_index; $index -lt $source_bytes.Length; $index++) {
            $byte = $source_bytes[$index]
            if ($byte -eq 0x0d) {
                $normalized_stream.WriteByte(0x0d)
                $normalized_stream.WriteByte(0x0a)
                if ($index + 1 -lt $source_bytes.Length -and $source_bytes[$index + 1] -eq 0x0a) {
                    $index++
                }
            } elseif ($byte -eq 0x0a) {
                $normalized_stream.WriteByte(0x0d)
                $normalized_stream.WriteByte(0x0a)
            } else {
                $normalized_stream.WriteByte($byte)
            }
        }

        $normalized_bytes = $normalized_stream.ToArray()
    } finally {
        $normalized_stream.Dispose()
    }

    if (-not [System.Linq.Enumerable]::SequenceEqual[byte]($source_bytes, $normalized_bytes)) {
        [System.IO.File]::WriteAllBytes($_.FullName, $normalized_bytes)
        Write-Host "Driver INF line endings normalized: $($_.Name)"
    }
}

# Inf2Cat の実行
$Inf2CatPath = Join-Path -Path $ScriptDir -ChildPath "Inf2Cat"
& $Inf2CatPath /driver:$InfPath /uselocaltime /os:7_X86,7_X64,Server2008R2_X64,8_X86,8_X64,Server8_X64,6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64,Server10_ARM64,10_RS5_X86,10_RS5_X64,10_RS5_ARM64,ServerRS5_X64,ServerRS5_ARM64
if ($LASTEXITCODE -ne 0) {
    throw 'Inf2Cat failed.'
}

# signtool の実行
$SignToolPath = Join-Path -Path $ScriptDir -ChildPath "signtool"
$TrustedPublisherPfxPath = Join-Path -Path $ScriptDir -ChildPath "trustedpub.pfx"
& $SignToolPath sign /f $TrustedPublisherPfxPath /p 123 /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 "$InfPath/px4_drv_winusb.cat"
if ($LASTEXITCODE -ne 0) {
    throw 'Catalog signing failed.'
}

& (Join-Path $ScriptDir 'verify.ps1') -DriverPath $InfPath

# 証明書のコピー
$rootCerPath = Join-Path -Path $ScriptDir -ChildPath "root.cer"
cp $rootCerPath "$InfPath/px4_drv_winusb.cer"
