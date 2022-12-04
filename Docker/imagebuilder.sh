#!/usr/bin/env bash
# SPDX-FileCopyrightText: © 2022 Siemens AG
# Author: Gaurav Mishra <mishra.gaurav@siemens.com>
#
# SPDX-License-Identifier: GPL-2.0-only
#

set -e

cur_dir="$(dirname $(readlink -f "${BASH_SOURCE[0]}"))"

build_targets="scheduler web ununpack adj2nest wgetagent nomos ojo copyright"

DOCKER_CLI="/usr/bin/docker"

DOCKER_BUILDKIT=1 $DOCKER_CLI build -t fossology -f "${cur_dir}/Dockerfile.k8s" "${cur_dir}/.."

for target in $build_targets; do
  echo "Building target ${target}"
  DOCKER_BUILDKIT=1 $DOCKER_CLI build --target "$target" -f "${cur_dir}/Dockerfile.k8s" \
    -t "fossology/${target}:latest" "${cur_dir}/.."
done
