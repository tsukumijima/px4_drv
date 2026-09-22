# 指定ディレクトリの INF を、同梱のカタログファイルが署名したバイト列と突き合わせる
# Windows のカタログファイルは改行を含む INF のバイト列へ署名する
# 同じ作業ツリーからコピーした原本と配布物のハッシュが一致しても、チェックアウト時の改行変換は検出できないため、各 INF をカタログファイルの収録内容と直接照合する
# 検査対象は INF と px4_drv_winusb.cat を置いたディレクトリ
# リポジトリの pkg/inf と、配布物へコピーした dist/Driver を同じ手順で扱う
param(
    [Parameter(Mandatory = $true)]
    [string] $DriverPath
)

# 証明書ストアへカタログファイルを登録せず、指定したカタログファイル自体から INF の収録ハッシュを照合する
# CryptCATOpen の CRYPTCAT_OPEN_VERIFYSIGHASH は、証明書の信頼チェーンに依存せずカタログファイルの署名ハッシュを検証する
if ($null -eq ('DriverCatalog' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

public static class DriverCatalog
{
    private const uint CRYPTCAT_OPEN_VERIFYSIGHASH = 0x10000000;
    private const uint CRYPTCAT_VERSION_2 = 0x200;
    private static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

    [DllImport("wintrust.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CryptCATOpen(
        string fileName,
        uint openFlags,
        IntPtr provider,
        uint publicVersion,
        uint encodingType);

    [DllImport("wintrust.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptCATClose(IntPtr catalog);

    [DllImport("wintrust.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptCATAdminCalcHashFromFileHandle(
        IntPtr file,
        ref uint hashSize,
        byte[] hash,
        uint flags);

    [DllImport("wintrust.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CryptCATGetMemberInfo(IntPtr catalog, string referenceTag);

    public static bool ContainsFile(string catalogPath, string filePath)
    {
        IntPtr catalog = CryptCATOpen(
            catalogPath,
            CRYPTCAT_OPEN_VERIFYSIGHASH,
            IntPtr.Zero,
            CRYPTCAT_VERSION_2,
            0);
        if (catalog == INVALID_HANDLE_VALUE)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to open or verify the driver catalog signature hash.");
        }

        try
        {
            using (FileStream file = File.Open(filePath, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                uint hashSize = 0;
                if (!CryptCATAdminCalcHashFromFileHandle(
                        file.SafeFileHandle.DangerousGetHandle(),
                        ref hashSize,
                        null,
                        0))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to determine the driver INF hash size.");
                }

                byte[] hash = new byte[hashSize];
                if (!CryptCATAdminCalcHashFromFileHandle(
                        file.SafeFileHandle.DangerousGetHandle(),
                        ref hashSize,
                        hash,
                        0))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to calculate the driver INF hash.");
                }

                string referenceTag = BitConverter.ToString(hash).Replace("-", String.Empty);
                return CryptCATGetMemberInfo(catalog, referenceTag) != IntPtr.Zero;
            }
        }
        finally
        {
            CryptCATClose(catalog);
        }
    }
}
'@
}

# 1件でも検査に失敗したら、そこで実行を終える
$ErrorActionPreference = 'Stop'

# 相対パスでも検査対象を固定するため、実在するディレクトリへ解決する
$driver_directory = (Resolve-Path -LiteralPath $DriverPath).Path
$catalog_path = Join-Path $driver_directory 'px4_drv_winusb.cat'
$signer_certificate_path = Join-Path $PSScriptRoot 'trustedpub.cer'

# 署名対象のカタログファイルが揃っていることを、INF の検査に入る前に確定する
if ((Test-Path -LiteralPath $catalog_path -PathType Leaf) -eq $false) {
    throw "Driver catalog was not found: $catalog_path"
}

# 署名者の照合に使う公開証明書が揃っていることを、検証に入る前に確定する
if ((Test-Path -LiteralPath $signer_certificate_path -PathType Leaf) -eq $false) {
    throw "Driver catalog signer certificate was not found: $signer_certificate_path"
}

# 自己署名証明書を信頼していない環境では NotTrusted になるが、署名者と署名ハッシュは検証できる
$expected_signer_certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($signer_certificate_path)
$catalog_signature = Get-AuthenticodeSignature -LiteralPath $catalog_path
if ($catalog_signature.Status -in 'NotSigned', 'HashMismatch') {
    throw "Driver catalog signature is invalid: $($catalog_signature.Status), path: $catalog_path"
}

if ($null -eq $catalog_signature.SignerCertificate) {
    throw "Driver catalog signer certificate was not found in the signature: $catalog_path"
}

if ($catalog_signature.SignerCertificate.Thumbprint -ne $expected_signer_certificate.Thumbprint) {
    throw "Driver catalog has an unexpected signer certificate: $catalog_path"
}

if ($catalog_signature.Status -notin 'Valid', 'NotTrusted', 'UnknownError') {
    throw "Driver catalog has an unexpected signature status: $($catalog_signature.Status), path: $catalog_path"
}

Write-Host "Driver catalog signature verified: status $($catalog_signature.Status), signer $($catalog_signature.SignerCertificate.Thumbprint)"

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

    # 指定したカタログファイルを直接開き、Windows のカタログ API で計算した INF の参照タグが収録されているかを確認する
    # 証明書ストアの信頼状態に依存しないため、開発環境とクリーンな CI 環境で同じ内容一致を判定できる
    if ([DriverCatalog]::ContainsFile($catalog_path, $inf_file.FullName) -eq $false) {
        throw "Driver INF is not included in the catalog with the same byte content: $($inf_file.FullName)"
    }

    # 通過した INF のファイル名を残し、どの機種まで確認できたかを実行ログから追えるようにする
    Write-Host "Driver INF verified: $($inf_file.Name)"
}
