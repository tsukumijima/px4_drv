# 指定ディレクトリの INF を、同梱のカタログファイルが署名したバイト列と突き合わせる
# Windows のカタログファイルは改行を含む INF のバイト列へ署名する
# 同じ作業ツリーからコピーした原本と配布物のハッシュが一致しても、チェックアウト時の改行変換は検出できないため、各 INF をカタログファイルの収録内容と直接照合する
# 検査対象は INF と px4_drv_winusb.cat を置いたディレクトリ
# リポジトリの pkg/inf と、配布物へコピーした dist/Driver を同じ手順で扱う
param(
    [Parameter(Mandatory = $true)]
    [string] $DriverPath
)

# 1件でも検査に失敗したら、そこで実行を終える
$ErrorActionPreference = 'Stop'

# 相対パスでも検査対象を固定するため、実在するディレクトリへ解決する
$driver_directory = (Resolve-Path -LiteralPath $DriverPath).Path
$catalog_path = Join-Path $driver_directory 'px4_drv_winusb.cat'
# 署名時と同じ signtool.exe をこのスクリプトのディレクトリから参照する
# 検証結果を、実行環境の PATH にある別バージョンではなく、同梱の実装で固定する
$sign_tool_path = Join-Path $PSScriptRoot 'signtool.exe'

# 署名対象のカタログファイルが揃っていることを、INF の検査に入る前に確定する
if ((Test-Path -LiteralPath $catalog_path -PathType Leaf) -eq $false) {
    throw "Driver catalog was not found: $catalog_path"
}

# 同梱の signtool.exe が実在することを、検証コマンドの実行前に確定する
if ((Test-Path -LiteralPath $sign_tool_path -PathType Leaf) -eq $false) {
    throw "SignTool was not found: $sign_tool_path"
}

# 列挙結果が1件でも配列として扱い、件数を Count で読む
# 報告順をディレクトリ列挙順から切り離すため、ファイル名順に並べる
$inf_files = @(Get-ChildItem -LiteralPath $driver_directory -Filter '*.inf' -File | Sort-Object Name)
# 検証対象の INF が1件以上あることを、照合ループに入る前に確定する
if ($inf_files.Count -eq 0) {
    throw "Driver INF files were not found: $driver_directory"
}

# 各 INF をテキストとして解釈せず、Inf2Cat が署名したのと同じバイト列として検査する
foreach ($inf_file in $inf_files) {
    $bytes = [System.IO.File]::ReadAllBytes($inf_file.FullName)

    # Inf2Cat はファイル先頭の BOM も署名対象へ含める
    # sign.ps1 が揃える BOM なしのバイト列と、検査中の INF が同じであることを先に確定する
    if (($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf) -or
        ($bytes.Length -ge 2 -and (($bytes[0] -eq 0xff -and $bytes[1] -eq 0xfe) -or ($bytes[0] -eq 0xfe -and $bytes[1] -eq 0xff)))) {
        throw "Driver INF must not have a byte order mark: $($inf_file.FullName)"
    }

    # 改行は CRLF (0x0d 0x0a) だけを正規形とする
    # Git の core.autocrlf やパッチツールで混入した単独 LF / 単独 CR をここで検出し、カタログファイルの収録時と同じ改行の INF だけを後段へ渡す
    for ($index = 0; $index -lt $bytes.Length; $index++) {
        # LF の直前が CR である組だけを改行として受理する
        if ($bytes[$index] -eq 0x0a -and ($index -eq 0 -or $bytes[$index - 1] -ne 0x0d)) {
            throw "Driver INF contains an LF line ending: $($inf_file.FullName)"
        }

        # CR の直後が LF である組だけを改行として受理する
        if ($bytes[$index] -eq 0x0d -and ($index + 1 -ge $bytes.Length -or $bytes[$index + 1] -ne 0x0a)) {
            throw "Driver INF contains a CR line ending: $($inf_file.FullName)"
        }
    }

    # /pa は Authenticode の既定ポリシーで検証する
    # 省略時は Microsoft のクロス証明書を求めるドライバー検証ポリシーになる
    # このリポジトリの自己署名のカタログファイルを /c で指定し、INF のバイト列が収録されているかを確認する
    & $sign_tool_path verify /pa /c $catalog_path $inf_file.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Driver INF is not included in the catalog with the same byte content: $($inf_file.FullName)"
    }

    # 通過した INF のファイル名を残し、どの機種まで確認できたかを実行ログから追えるようにする
    Write-Host "Driver INF verified: $($inf_file.Name)"
}
