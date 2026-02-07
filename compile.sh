#!/bin/zsh

if [ ! -d build ]; then
  mkdir build
fi

cd build

cmake .. -D DCMAKE_INSTALL_PREFIX=. -D CMAKE_BUILD_TYPE=Debug
make
make install

cd src
touch test.md