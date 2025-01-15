#!/bin/bash

################################################################################################################
# このスクリプトの使い方
# 1. `gpg --full-gen-key` で鍵を作成
#    - 鍵の種類は RSA 4096bit 推奨（良く分かってない）
#    - 鍵の有効期限は 0（無期限）推奨
#    - Real NameやEmailは適当（gmailな+の後ろを変えて別アドレスにしておいた方がいいかも）
#    - パスフレーズは設定しない
# 2. `gpg --export-secret-keys "example@example.com" | base64` でbase64形式にしてエクスポート
# 3. 2でエクスポートしたキーをGitHubのActions SecretにGPG_PRIVATE_KEYとして登録
# 4. `gpg --armor --export "example@example.com" > KEY.gpg` で公開鍵をエクスポートしてリポジトリルートに配置
# 5. 設定したメールアドレスをこのスクリプトのGPG_EMAILに設定
# 6. GitHubリポジトリの設定でPagesを有効にして、Actionsでのデプロイを選択
################################################################################################################

set -xe

cd $(dirname $0)

OUTDIR="./out"
GPG_EMAIL="test@example.com"

function get_debreleases() {
    local repo=$1
    local releases_json=$(gh release list -R ${repo} --limit 100 --json name,isDraft,isPrerelease)
    local releases=$(echo "${releases_json}" | jq -r '.[] | select(.isDraft == false and .isPrerelease == false) | .name')
    for rel in ${releases}; do
        local assets_json=$(gh release view -R ${repo} "${rel}" --json assets)
        local assets_url=$(echo "${assets_json}" | jq -r '.assets[] | select(.name | endswith(".deb")) | .url')
        for url in ${assets_url}; do
            if [ -z "${url}" ]; then
                continue
            fi
            curl -sSL -o "${OUTDIR}/$(basename ${url})" "${url}"
        done
    done
}

function build_apt_repository() {
    cd ${OUTDIR}
    dpkg-scanpackages --multiversion . > Packages
    gzip -k -f Packages

    apt-ftparchive release . > Release
    gpg --default-key "${GPG_EMAIL}" -abs -o - Release > Release.gpg
    gpg --default-key "${GPG_EMAIL}" --clearsign -o - Release > InRelease
}

# main logic

# mkdir
[ ! -d "${OUTDIR}" ] && mkdir -p "${OUTDIR}"

# public key

cp -f KEY.gpg "${OUTDIR}/KEY.gpg"

# tsukumijima/px4_drv
get_debreleases tsukumijima/px4_drv

# build apt repository
build_apt_repository
