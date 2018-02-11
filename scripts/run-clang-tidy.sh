#!/usr/bin/env bash

LAST_ARG=${@:$#}
EXEC_ROOT=`bazel info execution_root`
BAZEL_EXTERNAL=${EXEC_ROOT}/external

clang-tidy $@ -header-filter=$(realpath .) -system-headers=0 -p ./ \
           -- \
           -std=c++14 \
           -I./ \
           -x c++ \
           -I bazel-genfiles \
	   `PKG_CONFIG_PATH=/usr/local/opt/ibex@2.6.5/share/pkgconfig/ pkg-config ibex --cflags` \
           -isystem ${BAZEL_EXTERNAL}/spdlog/include \
           -isystem ${BAZEL_EXTERNAL}/fmt \
           -isystem ${BAZEL_EXTERNAL}/drake_symbolic \
           -isystem ${BAZEL_EXTERNAL}/ezoptionparser \
           -isystem ${BAZEL_EXTERNAL}/gtest/googletest/include \
           -isystem ${BAZEL_EXTERNAL}/picosat \
           -isystem /usr/local/opt/llvm/include/c++/v1 \
           -isystem /usr/local/include \
           -isystem /usr/local/opt/flex/include \
           -isystem /usr/local/opt/flex/include \

if [ ${LAST_ARG} == "--fix" ];
then
    git-clang-format -f
fi
