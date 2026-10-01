## Command line options

Every test app supports the following options:

```
-o <filename>   Redirect the output to the specified file instead of the standard output.
-r <number>     Repeat each test <number> times and report the last result. By default,
                tests are repeated 5 times.
```

## Note on switching between cmake configurations

When switching between X86/Hexagon standalone/Hexagon device/etc it is often necessary to 
delete CMakeCache.txt because cached options that are not overridden in the new configuration
are not deleted automatically.

## Example build configurations

Note that below configurations are only examples, users should adjust settings according to their environment.

### X86_64 (host, using clang)

```console
$ mkdir build && cd build
$ cmake -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-rtlib=compiler-rt" \
  -DCMAKE_CXX_STANDARD_LIBRARIES="-lunwind" \
  ..
$ cmake --build .
$ cmake --build . -t check
```

Using clang builtins (`-rtlib=compiler-rt` and  `-lunwind`) is needed for bfloat16 tests.

### AArch64 (cross-compiling, using clang)

Setting `CMAKE_SYSTEM_NAME` is necessary for cmake to detect cross-compilation.

```console
$ mkdir build && cd build
$ cmake -GNinja \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-march=armv9.2-a" \
  -DCMAKE_CROSSCOMPILING_EMULATOR=qemu \
  ..
$ cmake --build .
$ cmake --build . -t check
```

### Hexagon with Community Hexagon SDK (bare-metal, emulation)

Hexagon Community SDK is required. A version of the SDK can be downloaded from
https://softwarecenter.qualcomm.com/api/download/software/sdks/Hexagon_SDK/Linux/Debian/6.4.0.2/Hexagon_SDK_lnx.zip.

Please install the open-source Ripple compiler in PATH.

```console
$ mkdir build && cd build
$ cmake -GNinja \
  -DCMAKE_SYSTEM_NAME=HexagonStandalone \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-mv81 -mhvx -mhvx-length=128B -std=c++11 -stdlib=libstdc++" \
  -DCMAKE_CROSSCOMPILING_EMULATOR="hexagon-sim;--simulated_returnval;--" \
  ..
$ cmake --build .
$ cmake --build . -t check
```

In the above build script, we use hexagon libstdc++ to build a Hexagon standalone binary (without QuRT).

### Hexagon with Community Hexagon SDK (on device)

When CMAKE_SYSTEM_NAME is not set to "HexagonStandalone" the test apps are
built for the device, as shared libraries. They can be run with `run_main_on_hexagon`
tool from Hexagon SDK.

To build for the device, Hexagon SDK is needed, its path is substituted as `HEXAGON_SDK_ROOT`
below. The SDK include and library paths should be specified in cmake settings.

An example of invoking cmake to build for the Hexagon device:

```console
$ mkdir build && cd build
V=81
HEXAGON_SDK_ROOT=<...>
QURT="$HEXAGON_SDK_ROOT/rtos/qurt/computev$V/"
I="\
-I$QURT/include/qurt \
-I$QURT/include/posix \
-I$HEXAGON_SDK_ROOT/incs"
$ cmake -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_COMPILER_FORCED=ON \
  -DCMAKE_CXX_FLAGS="-mv$V -mhvx -mhvx-length=128B $I" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$QURT/lib" \
  ..
$ cmake --build .
```

QURT include and library paths are added in the C++ compiler settings.

CMAKE_CXX_COMPILER_FORCED is needed because cmake tries to verify the compiler and this does not
work because it tries to build an executable. For QuRT, it has to be a shared library and building
an executable fails.

Each test program is compiled as a shared library. The shared libraries along with the `run_main_on_hexagon`
tool from the Hexagon SDK should be copied to the device. Later, the tests can be run using `run_main_on_hexagon`.

First, upload the `run_main_on_hexagon` files:

```console
$ adb push $HEXAGON_SDK_ROOT/libs/run_main_on_hexagon/ship/android/run_main_on_hexagon /data/local/tmp
$ adb push $HEXAGON_SDK_ROOT/libs/run_main_on_hexagon/ship/hexagon_toolv88_v79/librun_main_on_hexagon_skel.so /data/local/tmp
```

The step above can be done once.
Note that file names are version-dependent and may need to be adjusted depending on the device and SDK versions.

To run tests e.g. in `regressions`, we can do the following:

```console
$ adb push libregressions.so /data/local/tmp
$ adb shell
kalama:/ # cd /data/local/tmp
kalama:/ # DSP_LIBRARY_PATH=/data/local/tmp ./run_main_on_hexagon 3 libregressions.so unsigned_pd=1 -o result.txt
kalama:/ # cat result.txt
```

## LICENSE

The source code is (c) Qualcomm Technologies Inc. and is distributed under the terms of The Clear BSD License (LICENSE.TXT).

Code examples taken from Intel ISPC project are (c) Intel Corporation and are subject to the license in LICENSE-ISPC.TXT.

Code derived from the KleidiAI project is (c) Arm Limited and/or its affiliates and is subject to the license in LICENSE-KLEIDIAI.TXT.
