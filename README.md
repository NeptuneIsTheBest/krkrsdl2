# 吉里吉里SDL3 / Kirikiri SDL3

吉里吉里SDL3は、[吉里吉里Z](https://krkrz.github.io/)をSDL3上へ移植したプロジェクトです。現在サポートする実行環境は、Apple Siliconを搭載したMac上のmacOS 13 Ventura以降だけです。

Kirikiri SDL3 is a port of [Kirikiri Z](https://krkrz.github.io/) to SDL3. The only supported target is macOS 13 Ventura or later on an Apple Silicon Mac.

詳細 / More information: https://krkrsdl2.github.io/krkrsdl2/

## サポート対象 / Supported target

- macOS 13.0 or later
- Apple Silicon (`arm64`) only
- Command-line executable and macOS application bundle
- External plugins built with the generated `tp_stub`

Intel Macs, Universal binaries, iOS, and non-macOS operating systems are not supported. CMake rejects unsupported hosts, SDKs, architectures, and deployment targets during configuration.

## ビルド / Build

Xcode Command Line Tools and CMake are required. Initialize all submodules before configuring:

```sh
git submodule update --init --recursive
```

Build the command-line executable:

```sh
cmake -S . -B build-cli \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPTION_BUILD_MACOS_BUNDLE=OFF
cmake --build build-cli --parallel
```

Build the application bundle, optionally embedding a game data file or directory:

```sh
cmake -S . -B build-app \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPTION_BUILD_MACOS_BUNDLE=ON \
  -DEMBED_DATA_PATH=/absolute/path/to/game-data
cmake --build build-app --parallel
```

The build defaults to `arm64` and a macOS 13.0 deployment target.

## プラグインスタブ / Plugin stub

Python 3 is required to regenerate the external-plugin stub:

```sh
mkdir -p build-tp-stub
python3 src/core/base/sdl3/makestub.py build-tp-stub
cp src/config/tp_stub_template.cmake build-tp-stub/CMakeLists.txt
cmake -S build-tp-stub -B build-tp-stub/build
cmake --build build-tp-stub/build --parallel
```

The resulting `libtp_stub.a` targets macOS 13 or later on `arm64`.

## 商用ゲームの実行に関する注意 / A note on running commercial games

このプロジェクトを使用して変更されていない商用ゲームを実行することはサポートされていません。

Running unmodified commercial games with this project is not supported.

## ライセンス / License

吉里吉里SDL3ソース（`src`ディレクトリ内）のコードはMITライセンスの下でライセンスされています。詳細については`LICENSE`をお読みください。このプロジェクトには、各コンポーネントのライセンスに従うサードパーティ製コードが含まれています。

The Kirikiri SDL3 source in the `src` directory is licensed under the MIT License. See `LICENSE` for details. This project also contains third-party code covered by each component's own license.
