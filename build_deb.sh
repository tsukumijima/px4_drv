#!/bin/bash

set -euo pipefail
umask 022

repo_dir=$(cd -- "$(dirname -- "$0")" && pwd)
template_dir="$repo_dir/px4_drv-dkms-mkdeb/debian"
module_version=$(sed -nE 's/^PACKAGE_VERSION="([^"]*)"$/\1/p' "$repo_dir/dkms.conf")
package_version=$module_version
output_dir="$repo_dir/.."

usage() {
    echo "Usage: $0 [--version DEBIAN_VERSION] [--output-dir DIRECTORY]"
    echo "Build a source-only DKMS deb from the current Git working tree."
}

while (($#)); do
    case "$1" in
        --version)
            if (($# < 2)); then
                usage >&2
                exit 2
            fi
            package_version=$2
            shift 2
            ;;
        --output-dir)
            if (($# < 2)); then
                usage >&2
                exit 2
            fi
            output_dir=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

for required_command in git dpkg dpkg-deb tar; do
    if ! command -v "$required_command" >/dev/null; then
        echo "Missing command: $required_command" >&2
        exit 1
    fi
done

# Debian リビジョンまで DKMS の版に含めると、dkms.conf の版と登録先が一致しない。
if [[ ! $module_version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
   [[ $package_version != "$module_version" && $package_version != "$module_version"-* ]]; then
    echo "Debian version must be $module_version or $module_version-<revision>." >&2
    exit 2
fi
dpkg --validate-version "$package_version"

mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
output_file="$output_dir/px4-drv-dkms_${package_version}_all.deb"
if [[ -e $output_file ]]; then
    echo "Already exists: $output_file (choose a new version or output directory)." >&2
    exit 1
fi

build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT
package_dir="$build_dir/package"
control_dir="$package_dir/DEBIAN"
source_dir="$package_dir/usr/src/px4_drv-$module_version"
doc_dir="$package_dir/usr/share/doc/px4-drv-dkms"
source_list="$build_dir/source-files"
mkdir -p "$control_dir" "$source_dir" "$doc_dir"

# HEAD のアーカイブでは未コミットの修正や追加が欠けるため、作業ツリーから収録する。
git -C "$repo_dir" ls-files --cached --others --exclude-standard -z -- \
    driver include etc dkms dkms.conf LICENSE | sort -zu > "$source_list"
tar -C "$repo_dir" --null --verbatim-files-from -T "$source_list" -cf - |
    tar -C "$source_dir" --no-same-owner -xf -

for script in postinst prerm; do
    sed -e 's/MODULE_NAME/px4_drv/g' \
        -e "s/MODULE_VERSION/$module_version/g" \
        "$template_dir/$script" > "$control_dir/$script"
    chmod 755 "$control_dir/$script"
done
install -m 644 "$repo_dir/LICENSE" "$doc_dir/copyright"
chmod -R u=rwX,go=rX "$package_dir"

# 圧縮後の割当量で計算すると、導入先の必要容量を過小評価する。
installed_size=$(du -sk --apparent-size "$package_dir/usr" | cut -f1)
# debhelper 用の control には Source 節があるため、直接生成用の定義を使う。
sed -e "s/@PACKAGE_VERSION@/$package_version/g" \
    -e "s/@MODULE_VERSION@/$module_version/g" \
    -e "s/@INSTALLED_SIZE@/$installed_size/g" \
    "$template_dir/control.binary.in" > "$control_dir/control"

(
    cd "$package_dir"
    find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
# 作成者の UID/GID を収録すると、導入先でもその番号の利用者がソースを所有する。
dpkg-deb --root-owner-group --build "$package_dir" "$build_dir/result.deb"
install -m 644 "$build_dir/result.deb" "$output_file"
echo "Built: $output_file"
