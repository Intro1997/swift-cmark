#!/bin/zsh

if [ ! -d build ]; then
  mkdir build
fi

cd build

cmake .. -D DCMAKE_INSTALL_PREFIX=. -D CMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make
make install

if [ ! -e compile_commands.json ]; then
  ln -s compile_commands.json ../compile_commands.json
fi

cd src
touch test.md
