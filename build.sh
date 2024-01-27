#!/bin/bash

set -ex

ocf_dir=$(cd $(dirname $0); pwd)

cd ${ocf_dir}/lava_adaptor
make distclean
make -j16