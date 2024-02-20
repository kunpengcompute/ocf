#!/bin/bash
set -e

if [ $# != 1 ];then
    echo "Usage: bash coverage_cal.sh [BASE_OCF_BRANCH_VERSION]"
    exit 1
fi

BASE_OCF_BRANCH_VERSION=$1

# the same path of this shell file
ROOT_PATH=$(pwd)

lcov --version

# create base compare branch
if [[ -z $(git branch -a | grep base_branch) ]];then
    git branch base_branch ${BASE_OCF_BRANCH_VERSION}
fi

# clean directory first
if [ -f result.info ];then
    rm -rf result.info
fi
if [ -f total.info ];then
    rm -rf total.info
fi
if [ -f result.xml ];then
    rm -rf result.xml
fi
if [ -f inc.html ];then
    rm -rf inc.html
fi

for file in ${ROOT_PATH}/*
do
    if [ -d $file ];then
        if [ -d "$file"/build ];then
            rm -rf "$file"/build
        fi
        if [ -d "$file"/inc ];then
            rm -rf "$file"/inc
        fi
    fi
done


for file in ${ROOT_PATH}/*
do
    if [ -d $file ];then
        cd $file
        binary_file=$(grep get_target_property CMakeLists.txt | head -n 1 | awk '{print $2}')
        mkdir build && cd build
        cmake ..
        make -j
        ./${binary_file}
        lcov -c -d . -o total.info
        cd ${ROOT_PATH}
    fi
done

# link all tests coverage result
tmp=$(for file in ${ROOT_PATH}/*;do if [ -d $file ];then echo "-a ${file}/build/total.info";fi done | xargs)
lcov ${tmp} -o total.info

# remove coverage in ucache_ut directory
lcov --remove total.info '*/ucache_ut/*' -o result.info

lcov_cobertura result.info -o result.xml
diff-cover result.xml --compare=base_branch --html-report inc.html