#!/usr/bin/env  bash

set -ex

dir=build-swift-macos
mkdir -p $dir
cd $dir

cmake \
  -DSHERPA_ONNX_ENABLE_BINARY=OFF \
  -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_INSTALL_PREFIX=./install \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DSHERPA_ONNX_ENABLE_PYTHON=OFF \
  -DSHERPA_ONNX_ENABLE_TESTS=OFF \
  -DSHERPA_ONNX_ENABLE_CHECK=OFF \
  -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
  -DSHERPA_ONNX_ENABLE_JNI=OFF \
  -DSHERPA_ONNX_ENABLE_C_API=ON \
  -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF \
  ../

make VERBOSE=1 -j4
make install
rm -fv ./install/include/cargs.h

libtool -static -o ./install/lib/libsherpa-onnx.a \
  ./install/lib/libsherpa-onnx-c-api.a \
  ./install/lib/libsherpa-onnx-core.a \
  ./install/lib/libkaldi-native-fbank-core.a \
  ./install/lib/libkissfft-float.a \
  ./install/lib/libsherpa-onnx-fstfar.a \
  ./install/lib/libsherpa-onnx-fst.a \
  ./install/lib/libsherpa-onnx-kaldifst-core.a \
  ./install/lib/libkaldi-decoder-core.a \
  ./install/lib/libucd.a \
  ./install/lib/libpiper_phonemize.a \
  ./install/lib/libespeak-ng.a \
  ./install/lib/libssentencepiece_core.a

xcodebuild -create-xcframework \
  -library install/lib/libsherpa-onnx.a \
  -headers install/include \
  -output sherpa-onnx.xcframework
