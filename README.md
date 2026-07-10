# VarjoDualCameraApplications

Varjo HMD と2台のカメラを組み合わせる複数の Windows / C++ アプリケーションを収録するリポジトリです。

## Applications

### MFFrameSyncToVarjoPlane

`MFFrameSource` の `MFD3D12CameraSyncThread` から得た後処理同期済み左右フレームを、`VarjoXR` の Plane に左右 eye 別で表示します。

- D3D12 backend
- GPU内コピーのみ
- OpenCV非依存
- HeadRelative / World配置
- JSONから3方式の左右視差remapを適用可能

詳細は `apps/MFFrameSyncToVarjoPlane/README.md` を参照してください。

### RealtimeStereoCalibration

左右カメラ映像をVarjoへ表示しながら、チェッカーボード検出結果を蓄積して視差キャリブレーションを継続更新します。表示用と計算用に独立した2本の `MFD3D12CameraSyncThread` を使い、最新の採用済みパラメータを以後の表示フレームへ反映します。

- `CameraCaptureThread` から表示系・計算系へキューごとのGPUコピーを配信
- 計算側は未処理の最新同期フレームだけを保持
- `uncalibrated` / `affine_vertical` / `affine_full` を継続更新
- 初期JSONまたは恒等変換から開始
- 最新結果を `nlohmann::json` と任意パスのJSONファイルで提供
- D3D12リソース入力の解析コンポーネントとVarjo表示コンポーネントを再利用可能なライブラリとして分離
- OpenCVはキャリブレーション解析ターゲット内部だけで使用

詳細は `apps/RealtimeStereoCalibration/README.md` を参照してください。

## Reusable targets

- `Vdca::StereoCalibrationCore`
  - JSONデータモデル、初期値読込、revision snapshot、atomic file save
- `Vdca::StereoCalibrationOpenCV`
  - D3D12左右リソースのlatest-only解析、GPU readback、チェッカーボード検出、3方式の推定
- `Vdca::VarjoStereoView`
  - 外部D3D12左右リソースのPlane表示、最新snapshotの左右remap適用

OpenCV型は公開APIへ露出しません。表示コンポーネント単体を統合する場合、OpenCV依存ターゲットをリンクする必要はありません。

## Dependencies

- Windows 10 / 11
- Visual Studio 2026
- CMake 4.2以降
- Varjo Native SDK / Varjo Runtime
- [MFFrameSource](https://github.com/Isasa2357/MFFrameSource.git)
- [VarjoXR](https://github.com/Isasa2357/VarjoXR.git)
- [D3D12Helper](https://github.com/Isasa2357/D3D12Helper.git)
- `VarjoXR` / `MFFrameSource` が使用する `VarjoToolkit`, `ThreadKit`, `glm`
- `nlohmann/json`
- OpenCV `core`, `imgproc`, `calib3d`（`RealtimeStereoCalibration` の解析部分のみ）

依存リポジトリは既定で `FetchContent` により取得します。OpenCVはインストール済みのCMake packageを `find_package(OpenCV)` で検出します。必要に応じて `OpenCV_DIR` を指定してください。

## Configure and build

CMDプロンプトで、リポジトリのルートに移動済みの状態から実行します。

```bat
set "VARJO_SDK_ROOT=C:\path\to\VarjoSDK"
set "OpenCV_DIR=C:\path\to\opencv\build\x64\vc17\lib"

cmake -S . -B out/build/default -G "Visual Studio 18 2026" -A x64 ^
  -DVARJOXR_VARJO_SDK_ROOT="%VARJO_SDK_ROOT%" ^
  -DVARJOXR_VARJO_RUNTIME_DIR="%VARJO_SDK_ROOT%\bin" ^
  -DOpenCV_DIR="%OpenCV_DIR%"

cmake --build out/build/default --config Debug --target MFFrameSyncToVarjoPlane
cmake --build out/build/default --config Debug --target RealtimeStereoCalibration
```

OpenCVを必要としない1つ目のアプリケーションだけを構成する場合:

```bat
cmake -S . -B out/build/display-only -G "Visual Studio 18 2026" -A x64 ^
  -DVARJOXR_VARJO_SDK_ROOT="%VARJO_SDK_ROOT%" ^
  -DVARJOXR_VARJO_RUNTIME_DIR="%VARJO_SDK_ROOT%\bin" ^
  -DVDCA_BUILD_REALTIME_STEREO_CALIBRATION=OFF
```

ローカルcheckoutを使う場合は、`D3D12HELPER_ROOT`、`VARJOXR_ROOT`、`MFFRAMESOURCE_ROOT`、`THREADKIT_ROOT`、`NLOHMANN_JSON_ROOT` を指定できます。

## Repository layout

```text
VarjoDualCameraApplications/
├─ apps/
│  ├─ MFFrameSyncToVarjoPlane/
│  └─ RealtimeStereoCalibration/
├─ libraries/
│  ├─ StereoCalibrationCore/
│  ├─ StereoCalibrationOpenCV/
│  └─ VarjoStereoView/
├─ cmake/
├─ CMakeLists.txt
└─ README.md
```
