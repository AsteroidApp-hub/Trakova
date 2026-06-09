# Trakova

歌い手（ボーカルカバー投稿者）向けの**録音特化 DAW**。macOS / Windows のクロスプラットフォームで無料配布します。

Copyright (C) 2025-2026 Studio アステロイド (Studio Asteroid)
Licensed under the **GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later)**.

- ソースコード: https://github.com/AsteroidApp-hub/Trakova
- ライセンス全文: [LICENSE](LICENSE)
- 同梱ライブラリの表記: [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt)

---

## 特長

- マルチトラック録音 / 再生（複数トラック同時録音・パンチイン・遡及録音・ループ録音）
- 波形編集（移動 / リサイズ / 分割 / 結合 / フェード / クロスフェード / 無音カット）
- MIDI トラック・ピアノロール・内蔵シンセ
- ミキサー / メータリング / 簡易リバーブ / プラグイン（VST3・AU）ホスト
- WAV / AIFF / MP3 書き出し（内蔵エンコーダ・高品質ディザ）
- 日本語 / English

詳細は同梱マニュアル（[Docs/MANUAL.html](Docs/MANUAL.html)）と仕様書（[Docs/SPEC.md](Docs/SPEC.md)）を参照。

---

## ビルド

### 前提

- CMake 3.22 以上
- C++17 対応コンパイラ（macOS: Xcode / Windows: MSVC）
- JUCE 8（CMake が自動取得、またはローカル `JUCE/` に配置）

### 手順

```sh
git clone https://github.com/AsteroidApp-hub/Trakova.git
cd Trakova
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

初回の configure で **JUCE 8 (8.0.12) を自動ダウンロード**します（CMake FetchContent）。
リポジトリに JUCE は含まれません。オフラインや特定バージョンでビルドしたい場合は、JUCE 8 を
リポジトリ直下 `JUCE/` に配置すると、ダウンロードせずローカルの `JUCE/` を使います。

### Windows での ASIO 対応（任意）

ASIO SDK は再配布が制限されているためリポジトリには含まれません。利用する場合は
[Steinberg Developer](https://www.steinberg.net/developers/) から SDK を取得し、
`Source/ThirdParty/asiosdk/` に配置してください（配置されていれば CMake が自動で有効化します）。
未配置の場合は WASAPI / DirectSound のみで動作します。

---

## テスト

```sh
cmake --build build --target TrakovaTests --config Debug
./build/TrakovaTests_artefacts/Debug/TrakovaTests
```

`juce::UnitTest` ベースのコンソールアプリです（全合格で終了コード 0）。

---

## ライセンス

本アプリ本体は **AGPL-3.0-or-later** で配布されます。AGPL ではバイナリを受け取った人に対して
対応するソースコードを提供する必要があり、本リポジトリがその対応ソースです。利用しているオープンソース
ライブラリ・SDK のライセンスは [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt) を参照してください。

## 免責事項（無保証・無責任）

本ソフトウェアは**無料**で「**現状のまま（AS IS）**」提供されます。**いかなる保証もありません。**

- 動作・品質・安全性・特定目的への適合性について、明示・黙示を問わず一切保証しません。
- 本ソフトウェアの使用または使用できないことによって生じたいかなる損害（録音・プロジェクトデータの
  消失や破損、他のソフトウェア・機材への影響、逸失利益などを含み、これらに限りません）についても、
  作者は**一切の責任を負いません**。
- ご利用は**利用者ご自身の責任**でお願いします。大切なデータは事前にバックアップしてください。

これは AGPL-3.0-or-later 第15条（保証の否認）および第16条（責任の制限）に基づくものです。

### Disclaimer (No warranty / No liability)

This software is **free** and provided **"AS IS", without warranty of any kind**, either express or
implied. To the maximum extent permitted by applicable law, the author shall
**not be liable** for any damages whatsoever (including, without limitation, loss or corruption of
recordings or project data, damage to other software or equipment, or lost profits) arising from the
use of, or inability to use, this software. **Use it at your own risk** and back up important data.
This follows Sections 15 and 16 of the AGPL-3.0-or-later.

## 支援 / Support

開発の継続を応援していただけると励みになります 🙇

- **GitHub Sponsors**: https://github.com/sponsors/AsteroidApp-hub
- **Ko-fi**: （準備中 — アカウント開設後にリンクを追加します）

## ダウンロード / Download

ビルド済みアプリ（macOS / Windows）は **[Releases](https://github.com/AsteroidApp-hub/Trakova/releases)**
からダウンロードできます。リリース手順は [RELEASE.md](RELEASE.md) を参照。

## 貢献

Pull Request やバグ報告・提案を受け付けています。ただし個人開発のため、内容の確認・反映に
お時間をいただくことや、内容によっては取り込みをお約束できない場合があります。送る際は
[CONTRIBUTING.md](CONTRIBUTING.md) の条件（AGPL-3.0-or-later / DCO サインオフ）をご確認ください。

---

## English summary

**Trakova** is a free, recording-focused DAW for vocal-cover singers (macOS / Windows).

Copyright (C) 2025-2026 Studio Asteroid. Licensed under **AGPL-3.0-or-later** (see [LICENSE](LICENSE)).
Third-party components are listed in [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).

Build with CMake 3.22+ and a C++17 compiler; JUCE 8 is **not** vendored in the repo — it is
fetched automatically at configure time via CMake FetchContent (or place it in `./JUCE` for
offline builds). See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes
(contributions are AGPL-3.0-or-later, DCO sign-off required).
