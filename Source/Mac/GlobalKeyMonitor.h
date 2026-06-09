// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <functional>
#include <memory>

/**
    macOS の NSEvent ローカルモニターを使い、アプリ全体のキー入力を横取りする。
    プラグインのネイティブ Cocoa UI（NSView）にフォーカスがある状態でも
    Space 等のトランスポートキーを取り損ねないようにするための仕組み。

    @returns コールバックが true を返したキーはアプリ内で消費される（プラグインへは渡らない）。
              false の場合はそのまま通常処理へ流れる。
*/
class GlobalKeyMonitor
{
public:
    using Callback = std::function<bool(int unicodeChar, int modifiersAsJuceFlags)>;

    explicit GlobalKeyMonitor(Callback callback);
    ~GlobalKeyMonitor();

private:
    class Pimpl;
    std::unique_ptr<Pimpl> pimpl;
};
