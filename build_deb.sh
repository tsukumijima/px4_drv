#!/bin/bash

set -CEuo pipefail

# Prep
sudo apt-get install -y dkms dpkg

# Build dkms deb
dkms mkdeb
