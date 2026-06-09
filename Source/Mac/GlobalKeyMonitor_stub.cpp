// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// 非 macOS 向けの no-op 実装。
// Windows ではプラグインのネイティブ UI 越しのキー受付には SetWindowsHookEx 等の
// Win32 API を使った別実装が必要（将来対応）。
#include "GlobalKeyMonitor.h"

class GlobalKeyMonitor::Pimpl {};

GlobalKeyMonitor::GlobalKeyMonitor(Callback) : pimpl(std::make_unique<Pimpl>()) {}
GlobalKeyMonitor::~GlobalKeyMonitor() = default;
