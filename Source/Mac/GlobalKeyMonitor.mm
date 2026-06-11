// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#include "GlobalKeyMonitor.h"

#import <Cocoa/Cocoa.h>
#include <JuceHeader.h>

class GlobalKeyMonitor::Pimpl
{
public:
    explicit Pimpl(Callback cb) : callback(std::move(cb))
    {
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
            handler:^NSEvent*(NSEvent* event)
            {
                // テキスト入力中（AppKit のテキストフィールド/ビュー or JUCE TextEditor）はスルーする
                NSResponder* fr = NSApp.keyWindow.firstResponder;
                if ([fr isKindOfClass:[NSText class]]
                    || [fr isKindOfClass:[NSTextView class]]
                    || [fr isKindOfClass:[NSTextField class]])
                    return event;
                if (auto* focused = juce::Component::getCurrentlyFocusedComponent();
                    focused != nullptr && dynamic_cast<juce::TextEditor*>(focused) != nullptr)
                    return event;

                NSString* chars = event.charactersIgnoringModifiers;
                int unicode = (chars.length > 0) ? [chars characterAtIndex:0] : 0;
                int mods = 0;
                const auto flags = event.modifierFlags;
                if (flags & NSEventModifierFlagCommand) mods |= juce::ModifierKeys::commandModifier;
                if (flags & NSEventModifierFlagShift)   mods |= juce::ModifierKeys::shiftModifier;
                if (flags & NSEventModifierFlagOption)  mods |= juce::ModifierKeys::altModifier;
                if (flags & NSEventModifierFlagControl) mods |= juce::ModifierKeys::ctrlModifier;

                if (callback && callback(unicode, mods))
                    return nil;     // 消費
                return event;       // 通常処理へ
            }];
    }

    ~Pimpl()
    {
        if (monitor != nil) [NSEvent removeMonitor:monitor];
        monitor = nil;
    }

private:
    id monitor { nil };
    Callback callback;
};

GlobalKeyMonitor::GlobalKeyMonitor(Callback cb)
    : pimpl(std::make_unique<Pimpl>(std::move(cb))) {}

GlobalKeyMonitor::~GlobalKeyMonitor() = default;
