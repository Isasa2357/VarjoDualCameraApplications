# VarjoDualCameraApplications

Varjo HMD と2台のカメラを組み合わせる複数の Windows / C++ アプリケーションを収録するリポジトリです。

## Applications

### MFFrameSyncToVarjoPlane

`MFFrameSource` の `MFD3D12CameraSyncThread` から得た後処理同期済み左右フレームを、`VarjoXR` の Plane に左右 eye 別で表示します。

- D3D12 backend
- GPU 内コピーのみ
- OpenCV 非依存
- HeadRelative / World 配置
- 将来の左右別視差キャリブレーション処理を Plane processing に追加可能

詳細は `apps/MFFrameSyncToVarjoPlane/README.md` を参照してください。

## Dependencies

- Windows 10 / 11
- Visual Studio 2026
- CMake 4.2 以降
- Varjo Native SDK / Varjo Runtime
- [MFFrameSource](https://github.com/Isasa2357/MFFrameSource.git)
- [VarjoXR](https://github.com/Isasa2357/VarjoXR.git)
- [D3D12Helper](https://github.com/Isasa2357/D3D12Helper.git)
- `VarjoXR` / `MFFrameSource` が使用する `VarjoToolkit`, `ThreadKit`, `glm`

依存リポジトリは既定で `FetchContent` により取得します。ローカル checkout を使う場合は、`D3D12HELPER_ROOT`、`VARJOXR_ROOT`、`MFFRAMESOURCE_ROOT`、`THREADKIT_ROOT` を指定できます。

## Configure and build

CMD プロンプトで、リポジトリのルートに移動済みの状態から実行します。

```bat
set "VARJO_SDK_ROOT=C:\path\to\VarjoSDK"

cmake -S . -B out/build/default -G "Visual Studio 18 2026" -A x64 ^
  -DVARJOXR_VARJO_SDK_ROOT="%VARJO_SDK_ROOT%" ^
  -DVARJOXR_VARJO_RUNTIME_DIR="%VARJO_SDK_ROOT%\bin"

cmake --build out/build/default --config Debug --target MFFrameSyncToVarjoPlane
```

ローカルの各ライブラリを使う例:

```bat
set "VARJO_SDK_ROOT=C:\path\to\VarjoSDK"
set "D3D12HELPER_ROOT=C:\path\to\D3D12Helper"
set "VARJOXR_ROOT=C:\path\to\VarjoXR"
set "MFFRAMESOURCE_ROOT=C:\path\to\MFFrameSource"
set "THREADKIT_ROOT=C:\path\to\ThreadKit"

cmake -S . -B out/build/default -G "Visual Studio 18 2026" -A x64 ^
  -DVARJOXR_VARJO_SDK_ROOT="%VARJO_SDK_ROOT%" ^
  -DVARJOXR_VARJO_RUNTIME_DIR="%VARJO_SDK_ROOT%\bin" ^
  -DD3D12HELPER_ROOT="%D3D12HELPER_ROOT%" ^
  -DVARJOXR_ROOT="%VARJOXR_ROOT%" ^
  -DMFFRAMESOURCE_ROOT="%MFFRAMESOURCE_ROOT%" ^
  -DTHREADKIT_ROOT="%THREADKIT_ROOT%"

cmake --build out/build/default --config Debug --target MFFrameSyncToVarjoPlane
```

## Repository layout

```text
VarjoDualCameraApplications/
├─ apps/
│  └─ MFFrameSyncToVarjoPlane/
├─ cmake/
├─ CMakeLists.txt
└─ README.md
```
