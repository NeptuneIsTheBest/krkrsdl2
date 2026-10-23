# 吉里吉里SDL2 / Kirikiri SDL2

吉里吉里SDL2は、macOS 専用の [SDL2](https://www.libsdl.org/) ベースの[吉里吉里Z](https://krkrz.github.io/)の移植版です。

詳細については、次の Web ページを参照してください: https://krkrsdl2.github.io/krkrsdl2/

Kirikiri SDL2 is a port of [Kirikiri Z](https://krkrz.github.io/) for macOS, using [SDL2](https://www.libsdl.org/). Apple Silicon (arm64), Intel (x86_64), and Universal binaries are supported.

Please visit the following webpage for more information: https://krkrsdl2.github.io/krkrsdl2/en/

## ビルド / Building

CMake のみを使用します。macOS 以外のターゲットは構成時に拒否されます。
CMake is the only supported build system. Non-macOS targets are rejected during configuration.
Install Xcode Command Line Tools and CMake (3.12 or newer), then initialize the dependencies:

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The default output is `build/krkrsdl2.app`, with the icon and the contents of `data` embedded in its resources. Set `EMBED_DATA_PATH` to a project directory or XP3 archive to embed different data.

For a Universal application containing both architectures:

```sh
cmake -S . -B build-universal -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-universal --parallel
```

For a command line executable:

```sh
cmake -S . -B build-cli -DCMAKE_BUILD_TYPE=Release -DOPTION_BUILD_MACOS_BUNDLE=OFF
cmake --build build-cli --parallel
./build-cli/krkrsdl2 -nosel ./data
```

The existing options for async image loading, case-insensitive storage, SDL threads, external plugins, and Canvas remain available. Canvas requires OpenGL ES or ANGLE at runtime. `CMAKE_OSX_DEPLOYMENT_TARGET` retains its default of `10.14`; the SDK applies the appropriate minimum for Apple Silicon.

### Plugin SDK

The macOS SDK is generated with Python 3 and the macOS `unifdef` tool. This also regenerates the engine's checked-in function export stubs:

```sh
mkdir -p build/tp_stub
python3 src/core/base/sdl2/makestub.py build/tp_stub
cp src/config/tp_stub_template.cmake build/tp_stub/CMakeLists.txt
cmake -S build/tp_stub -B build/tp_stub-build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build/tp_stub-build
```

Third-party submodules retain their upstream platform implementations. Their contents do not expand this project's macOS-only support policy.

## 商用ゲームの実行に関する注意 / A note on running commercial games

このプロジェクトを使用して変更されていない商用ゲームを実行することはサポートされていません。
代わりに[Wine](https://www.winehq.org/)または[GameNative](https://gamenative.app/)を使用してください。

Running unmodified commercial games using this project is not supported.
Please use [Wine](https://www.winehq.org/) or [GameNative](https://gamenative.app/) instead.

## ライセンス / License

吉里吉里SDL2ソース（`src`ディレクトリ内）のコードは、MITライセンスの下でライセンスされています。 詳細については、`LICENSE`をお読みください。
このプロジェクトには、サードパーティのコンポーネントが含まれています (GPL に基づいてライセンスされていません)。詳細については、各コンポーネントのライセンスファイルを参照してください。

The code of the Kirikiri SDL2 source (inside the `src` directory) is licensed under the MIT license. Please read `LICENSE` for more information.
This project contains third-party components (not licensed under the GPL). Please view the license file in each component for more information.
