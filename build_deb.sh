#!/bin/bash

set -CEuo pipefail

# Prep
sudo apt-get install -y debhelper devscripts dh-exec dkms dpkg

# Build dkms deb
dkms mkdeb --source-only
