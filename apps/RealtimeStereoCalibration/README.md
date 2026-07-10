# RealtimeStereoCalibration

チェッカーボードを左右カメラへ見せながら、視差補正を継続的に更新し、その時点の最新結果を VarjoXR の Plane 表示へ反映するアプリケーションです。

## Pipeline

```text
Left/Right MFD3D12CameraCaptureThread
    ├─ display queues     -> display MFD3D12CameraSyncThread -> VarjoStereoView
    └─ calibration queues -> calibration MFD3D12CameraSyncThread
                                      |
                                      v
                           RealtimeStereoCalibrator
                                      |
                                      v
                           latest CalibrationSnapshot
                              |                   |
                              v                   v
                        Varjo remap         JSON object/file
```

`CameraCaptureThread` はキューごとに独立した GPU texture copy を配信します。表示用と計算用の `FrameSyncThread` は別インスタンスであり、互いのフレームを取り合いません。

## Latest-only analysis

解析コンポーネントは未処理フレームを1件だけ保持します。計算中に複数フレームが届いた場合、古い未処理フレームは新しいフレームで置換されます。画像キューによる遅延は蓄積しません。

チェッカーボード検出に成功したフレームからはコーナー座標だけを保持します。複数位置・複数姿勢のコーナー履歴を使って、次の3方式を再推定します。

- `uncalibrated`
- `affine_vertical`
- `affine_full`

同じ姿勢を過剰に重複登録しないよう、直前の採用観測との平均コーナー移動量を確認します。

## Stable defaults

実機確認で安定した次の設定を既定値としています。

- JSON未指定時のprofile: `affine_vertical`
- 最大観測数: 30
- 表示へ反映を開始する最小観測数: 8
- 新しい観測として採用する最小平均コーナー移動量: 15px
- チェッカーボード検出: 高速な通常検出
- D3D12 debug layer: 無効

`--initial-json` を指定し、`--profile` を省略した場合は、既存JSONの `default_profile` を優先します。SB検出を使う場合は `--sb`、D3D12 debug layerを有効にする場合は `--d3d-debug` を指定します。

## Live display

起動時は次のいずれかを初期値として表示します。

1. `--initial-json` で指定した既存キャリブレーション
2. JSON未指定時の恒等変換

新しい有効な推定結果が公開されると、表示スレッドがrevisionの変化を検出し、左右eyeのremap定数を同時に更新します。計算中やチェッカーボード未検出時も、直前の有効なキャリブレーションを使い続けます。

## JSON output

最新結果は `CalibrationDocument::toJson()` により `nlohmann::json` として取得できます。実行アプリでは `--output-json` を指定すると、同じ内容をUTF-8 JSONとして任意の場所へ保存します。一時ファイルへ書いてから置換するため、読み手が途中状態のJSONを開くことを避けます。

既存JSONと出力先を同じパスにすることもできます。起動時に先に読み込み、その後のrevisionで更新します。

## OpenCV boundary

OpenCVは `VdcaStereoCalibrationOpenCV` 内部の次の処理だけに使用します。

- D3D12 readback後のグレースケール変換
- チェッカーボード検出
- 対応点処理
- 3方式の行列推定

公開API、JSONモデル、MFFrameSource接続、D3D12フレーム型、Varjo表示にはOpenCV型を公開しません。表示だけを他アプリへ組み込む場合、OpenCV依存ターゲットをリンクする必要はありません。

## Example

```bat
RealtimeStereoCalibration.exe ^
  --left 0 ^
  --right 1 ^
  --width 1920 ^
  --height 1080 ^
  --fps-num 60 ^
  --fps-den 1 ^
  --subtype NV12 ^
  --board-cols 12 ^
  --board-rows 9 ^
  --output-json C:\calibration\current.json ^
  --plane-width 1.0 ^
  --plane-distance 1.0 ^
  --placement head
```

初期JSONを使う場合は `--initial-json` を追加します。profile、観測数、通常チェッカーボード検出、D3Dデバッグ設定は、上記の安定した既定値が使われます。

カメラnativeサイズと解析サイズを分ける場合:

```bat
RealtimeStereoCalibration.exe ... ^
  --width 1920 --height 1080 ^
  --processing-width 1280 --processing-height 720 ^
  --output-width 1280 --output-height 720
```

初期JSONを指定した場合、明示しない解析・出力サイズはJSONの `calibration_input_size` と `rectified_output_size` を使用します。

## Reusable components

- `Vdca::StereoCalibrationCore`
  - JSONモデル
  - 初期JSON読込
  - `nlohmann::json`出力
  - atomic file save
  - revision snapshot共有
- `Vdca::StereoCalibrationOpenCV`
  - `StereoD3D12Frame` latest-only解析
  - D3D12 GPU readback
  - OpenCV checkerboard検出と3方式の推定
- `Vdca::VarjoStereoView`
  - 外部D3D12左右textureの表示
  - 最新snapshotの左右remap適用

他アプリケーションはMFFrameSourceを使わず、独自のD3D12 resource、resource state、ready fence、lifetime tokenを `StereoD3D12Frame` として提供できます。
