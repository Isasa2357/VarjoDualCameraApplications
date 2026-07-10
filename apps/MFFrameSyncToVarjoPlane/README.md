# MFFrameSyncToVarjoPlane

`MFFrameSource` の `MFD3D12CameraSyncThread` が出力した同期済み左右フレームを、`VarjoXR` の単一 `XRPlane` に左右 eye 別テクスチャとして表示するアプリケーションです。

## データ経路

```text
Left camera  -> MFD3D12CameraCaptureThread --\
                                             -> MFD3D12CameraSyncThread
Right camera -> MFD3D12CameraCaptureThread --/          |
                                                        v
                                           synchronized D3D12 frame pair
                                                        |
                                                        v
                                  application-owned stable eye textures
                                                        |
                                                        v
                               per-eye homography remap when JSON is supplied
                                                        |
                                                        v
                                  XRPlane left/right material textures
                                                        |
                                                        v
                                                Varjo compositor
```

## 設計上の要点

- OpenCV には依存しません。
- `MFFrameSource`、表示用コピー、`VarjoXR` は同じ `D3D12Core` と direct queue を共有します。
- 同期フレームを毎回 `wrapResource()` して Plane に直接保持させず、固定の左右表示テクスチャへ GPU copy します。
- JSON未指定時は左右フレームを加工せず表示します。
- JSON指定時は左右別 `inverse_pixel_homography` をCompute Shaderへ渡し、出力画素から入力座標を逆算します。
- 線形補間はCompute Shader内で4近傍画素を読み出して行います。2次元remap mapやOpenCVは使用しません。
- `uncalibrated`、`affine_vertical`、`affine_full` の3プロファイルを選択できます。
- 新しい同期フレームごとにXRTextureラッパを交互に切り替え、各eyeにつきremapを1回だけdispatchします。Varjoの複数viewによる重複dispatchを避けます。

## 視差キャリブレーションJSON

対応形式は次の識別子とバージョンです。

```json
{
  "format": "vdca.stereo_rectification",
  "version": 1
}
```

実行時には次を検証します。

- `format` と `version`
- `coordinate_system` の原点、軸方向、ピクセル中心規約、forward mapping
- `sampling.filter == "linear"`
- `sampling.border_mode == "constant"`
- 3x3 forward/inverse homography
- forwardとinverseの積が単位行列に比例すること
- カメラnativeサイズと `image_geometry.source_size` の一致
- MFFrameSource GPU出力サイズと `image_geometry.calibration_input_size` の一致

JSON指定時、MFFrameSourceはnativeカメラ画像を `calibration_input_size` へGPU変換・リサイズしてからremapへ渡します。Planeの物理的な縦横比は `rectified_output_size` に合わせます。

## 実行例

無補正表示:

```bat
MFFrameSyncToVarjoPlane.exe ^
  --left 0 ^
  --right 1 ^
  --width 1920 ^
  --height 1080 ^
  --fps-num 60 ^
  --fps-den 1 ^
  --subtype NV12 ^
  --plane-width 1.0 ^
  --plane-distance 1.0 ^
  --placement head
```

JSONの `default_profile` を使う場合:

```bat
MFFrameSyncToVarjoPlane.exe ^
  --left 0 ^
  --right 1 ^
  --width 1920 ^
  --height 1080 ^
  --fps-num 60 ^
  --fps-den 1 ^
  --subtype NV12 ^
  --rectification C:\path\to\stereo_rectification.json ^
  --plane-width 1.0 ^
  --plane-distance 1.0 ^
  --placement head
```

プロファイルを明示する場合:

```bat
MFFrameSyncToVarjoPlane.exe ... ^
  --rectification C:\path\to\stereo_rectification.json ^
  --rectification-profile affine_vertical
```

指定可能なプロファイル名:

```text
uncalibrated
affine_vertical
affine_full
```

`MFFrameSource` のカメラ形式選択は exact match です。`--width`、`--height`、fps、subtype の組み合わせがカメラのnative media typeに存在しない場合、カメラのopenは失敗します。

通常はビルド時に `D3D12Processing` shader が実行ファイル横へコピーされます。コピーされない構成では、次のように明示できます。

```bat
MFFrameSyncToVarjoPlane.exe ... --shader-dir C:\path\to\D3D12Helper\shaders\D3D12Processing
```

終了は `Esc` または `Ctrl+C` です。
