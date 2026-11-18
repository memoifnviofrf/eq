# Vocal EQ v0.3 (JUCE / VST3)

ボーカル向けの 8バンド・パラメトリックEQ(ダイナミックEQ付き)。**レイテンシーゼロ**、リニアフェーズなし。

## かんたんビルド(Windows)

1. このフォルダの `build.bat` を **右クリック → 管理者として実行**
2. 足りないツール(CMake / Git / Visual Studio)があれば、画面の指示で winget から導入できます
3. 初回は JUCE のダウンロードと全体のコンパイルで 5〜15分ほどかかります
4. 終わると `C:\Program Files\Common Files\VST3\Vocal EQ.vst3` に自動で入ります

管理者権限なしで実行した場合は、ビルドだけ行われ、VST3フォルダが開きます(手動でコピーしてください)。

出力場所:
```
build\VocalEQ_artefacts\Release\VST3\Vocal EQ.vst3
build\VocalEQ_artefacts\Release\Standalone\Vocal EQ.exe   ← DAWなしで動作確認できます
```

### 手動でビルドする場合
```
cmake -B build -A x64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --config Release --parallel
```

## DAWで使う(REAPER / Cubase 共通)
プラグインの再スキャン(REAPER: Options → Preferences → Plug-ins → VST → Re-scan、
Cubase: VSTプラグインマネージャー → 更新)をして、`Vocal EQ` を読み込みます。

## 機能
- **8バンド**。タイプ: Bell / Low Shelf / High Shelf / High Pass / Low Pass / Notch / Band Pass
- **HP/LP**: 12 / 24 / 48 dB/oct(バターワース)
- **帯域ごとの処理対象**: Stereo / Left / Right / Mid / Side
- **ダイナミックEQ**(Bell・シェルフ): Threshold / Ratio / Range / Attack / Release、ソフトニー付き
  - Cut when loud: 大きい時にカット(ディエッサー的な使い方)
  - Boost when loud: 大きい時にブースト
- **高域で崩れないフィルター設計**(下記)。オーバーサンプリング不要=遅延ゼロ
- **スペクトラム表示**(白線: 入力 / 青: 出力)
- **カーブ上の丸をドラッグ**で周波数・ゲイン、**ホイール**でQ、**ダブルクリック**でバンド追加/削除
- パラメーターのスムージング、内部は倍精度(double)
- プリセット5種(出発点です。声に合わせて調整してください)

### 高域の精度について
通常のデジタルEQ(双一次変換)は、高域でカーブが理想(アナログ)から崩れます。
このEQは、アナログ原型の特性に合わせた設計に置き換えて、オーバーサンプリングなし(=遅延なし)で崩れを減らしています。
アナログ原型との最大誤差(20Hz〜20kHz、44.1/48/96kHz、多数の組み合わせ)の比較:

| 種類 | 従来方式 | このEQ |
|---|---|---|
| Bell | 10.8 dB | 2.5 dB |
| Low/High Shelf(Q≤2) | 7.4 dB | 1.2 dB |
| High Pass | 16.8 dB | 3.2 dB |
| Low Pass | 25.8 dB | 1.2 dB |
| Notch | 16.1 dB | 7.4 dB |
| Band Pass | 15.3 dB | 15.3 dB(改善なし) |

※ 誤差が大きい値は、高Q・大ゲイン・ナイキスト付近の極端な設定での最悪値です。通常の使用範囲ではもっと小さくなります。

## 使い方の例: 歯擦音を抑える
1. バンド6 を選択 → タイプ Bell、Freq 6〜8kHz、Q 3 前後
2. Gain は 0 のまま、Dynamic をオン
3. Cut when loud、Threshold を -35dB 前後、Range 6〜8dB、Attack 1ms、Release 80ms
4. Threshold を動かしながら、歯擦音だけが抑えられる位置を探す

(プリセット `De-esser (dynamic)` が同じ設定です)

## 制限・今後
- リニアフェーズ、オーバーサンプリングは非搭載(遅延を増やさないため)
- ダイナミックEQの検出は「そのバンド周辺の信号」のみ。外部サイドチェインは未対応
- Band Pass は従来方式のままです
- 実機(Windows + DAW)での音質確認は未実施です。聴感で気になる点があれば報告してください

## ライセンスについて
このプロジェクトは JUCE を使っています。JUCE 8 は AGPLv3 または商用ライセンスです。
個人利用は問題ありませんが、プラグインを**配布・販売**する場合は JUCE のライセンス条件を確認してください
(https://juce.com/legal/juce-8-licence/)。
