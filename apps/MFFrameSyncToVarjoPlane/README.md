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
                                  XRPlane left/right material textures
                                                        |
                                                        v
                                       future per-eye remap processing
                                                        |
                                                        v
                                                Varjo compositor
```

## 設計上の要点

- OpenCV には依存しません。
- `MFFrameSource`、表示用コピー、`VarjoXR` は同じ `D3D12Core` と direct queue を共有します。
- 同期フレームを毎回 `wrapResource()` して Plane に直接保持させず、固定の左右表示テクスチャへ GPU copy します。
- この固定テクスチャ方式により、`MFFrameSource` の frame pool が入力リソースを再利用しても、Varjo の非同期描画と競合しません。
- 現在は加工せず表示します。
- 将来の視差キャリブレーションは `StereoPlaneSurface::setProcessing()` から左右 eye 別の Plane processing として追加します。キャプチャ、同期、コピーの経路には混ぜません。

## 実行例

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

`MFFrameSource` のカメラ形式選択は exact match です。指定した解像度、fps、subtype の組み合わせがカメラの native media type に存在しない場合、カメラの open は失敗します。

通常はビルド時に `D3D12Processing` shader が実行ファイル横へコピーされます。コピーされない構成では、次のように明示できます。

```bat
MFFrameSyncToVarjoPlane.exe ... --shader-dir C:\path\to\D3D12Helper\shaders\D3D12Processing
```

終了は `Esc` または `Ctrl+C` です。
