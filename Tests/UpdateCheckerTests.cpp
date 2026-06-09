// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include <JuceHeader.h>
#include "../Source/Project/UpdateChecker.h"

// UpdateChecker のネットワーク非依存な純関数 (parseLatestRelease / normaliseVersion /
// isNewerVersion) を検証する。check() の通信経路はユニットテスト対象外。
class UpdateCheckerTests : public juce::UnitTest
{
public:
    UpdateCheckerTests() : juce::UnitTest("UpdateChecker") {}

    void runTest() override
    {
        beginTest("parseLatestRelease: well-formed releases/latest JSON");
        {
            const juce::String json = R"({
              "tag_name": "v0.2.0",
              "name": "Trakova 0.2.0",
              "html_url": "https://github.com/AsteroidApp-hub/Trakova/releases/tag/v0.2.0",
              "assets": []
            })";
            auto info = UpdateChecker::parseLatestRelease(json);
            expectEquals(info.tag,     juce::String("v0.2.0"));
            expectEquals(info.version, juce::String("0.2.0"));
            expectEquals(info.pageUrl,
                juce::String("https://github.com/AsteroidApp-hub/Trakova/releases/tag/v0.2.0"));
        }

        beginTest("parseLatestRelease: missing fields / not found");
        {
            // GitHub が releases 無しで返す 404 ボディ
            auto info = UpdateChecker::parseLatestRelease(R"({ "message": "Not Found" })");
            expect(info.tag.isEmpty(),     "no tag_name");
            expect(info.pageUrl.isEmpty(), "no html_url");
            expect(info.version.isEmpty(), "no version");
        }

        beginTest("parseLatestRelease: malformed / non-object");
        {
            expect(UpdateChecker::parseLatestRelease("not json").tag.isEmpty());
            expect(UpdateChecker::parseLatestRelease("").tag.isEmpty());
            expect(UpdateChecker::parseLatestRelease("[ 1, 2, 3 ]").tag.isEmpty());
        }

        beginTest("parseTags: extracts tag names in order");
        {
            const juce::String json = R"([
              { "name": "v0.3.0", "commit": { "sha": "aaa" } },
              { "name": "v0.2.0", "commit": { "sha": "bbb" } },
              { "name": "v0.1.0", "commit": { "sha": "ccc" } }
            ])";
            auto tags = UpdateChecker::parseTags(json);
            expectEquals((int) tags.size(), 3, "three tags parsed");
            expectEquals(tags[0], juce::String("v0.3.0"));
            expectEquals(tags[2], juce::String("v0.1.0"));
        }

        beginTest("parseTags: empty / malformed / non-array");
        {
            expect(UpdateChecker::parseTags("[]").empty(),            "empty array");
            expect(UpdateChecker::parseTags("not json").empty(),     "malformed");
            expect(UpdateChecker::parseTags(R"({"x":1})").empty(),   "object, not array");
        }

        beginTest("pickNewestTag: highest semver wins regardless of order");
        {
            // API 順がバラバラでも最大を選ぶ。数字を含まないタグ ("nightly") は無視。
            expectEquals(UpdateChecker::pickNewestTag(
                { "v0.1.0", "v0.10.0", "v0.2.0", "nightly" }), juce::String("v0.10.0"));
            expectEquals(UpdateChecker::pickNewestTag({ "1.0.0" }), juce::String("1.0.0"));
            expect(UpdateChecker::pickNewestTag({}).isEmpty(),            "no tags → empty");
            expect(UpdateChecker::pickNewestTag({ "nightly", "edge" }).isEmpty(),
                   "no version-like tags → empty");
        }

        beginTest("normaliseVersion: strips prefixes and suffixes");
        {
            expectEquals(UpdateChecker::normaliseVersion("v0.2.0"),          juce::String("0.2.0"));
            expectEquals(UpdateChecker::normaliseVersion("0.2.0"),           juce::String("0.2.0"));
            expectEquals(UpdateChecker::normaliseVersion("V1.0"),            juce::String("1.0"));
            expectEquals(UpdateChecker::normaliseVersion("Trakova-v1.2.3"),  juce::String("1.2.3"));
            expectEquals(UpdateChecker::normaliseVersion("0.2.0-beta"),      juce::String("0.2.0"));
            expectEquals(UpdateChecker::normaliseVersion("1.2."),            juce::String("1.2"));
            expectEquals(UpdateChecker::normaliseVersion("  v3.4.5  "),      juce::String("3.4.5"));
            expectEquals(UpdateChecker::normaliseVersion("beta"),           juce::String());
            expectEquals(UpdateChecker::normaliseVersion(""),               juce::String());
        }

        beginTest("isNewerVersion: strictly-newer semantics");
        {
            expect(  UpdateChecker::isNewerVersion("0.2.0", "0.1.0"), "patch/minor bump newer");
            expect(! UpdateChecker::isNewerVersion("0.1.0", "0.1.0"), "same is not newer");
            expect(! UpdateChecker::isNewerVersion("0.1.0", "0.2.0"), "older is not newer");
            expect(  UpdateChecker::isNewerVersion("1.0.0", "0.9.9"), "major bump newer");
            expect(  UpdateChecker::isNewerVersion("0.1.1", "0.1.0"), "patch bump newer");
        }

        beginTest("isNewerVersion: numeric (not lexical) comparison");
        {
            expect(  UpdateChecker::isNewerVersion("0.10.0", "0.9.0"), "0.10 > 0.9 numerically");
            expect(! UpdateChecker::isNewerVersion("0.9.0", "0.10.0"), "0.9 < 0.10 numerically");
        }

        beginTest("isNewerVersion: prefix tolerance and unequal lengths");
        {
            expect(  UpdateChecker::isNewerVersion("v0.2.0", "0.1.0"), "leading v normalised");
            expect(! UpdateChecker::isNewerVersion("0.1.0", "0.1"),    "0.1.0 == 0.1 (zero pad)");
            expect(  UpdateChecker::isNewerVersion("0.2", "0.1.9"),    "0.2 > 0.1.9");
        }

        beginTest("isNewerVersion: guards on unparsable input");
        {
            expect(! UpdateChecker::isNewerVersion("garbage", "0.1.0"), "unparsable latest → no notify");
            expect(! UpdateChecker::isNewerVersion("", "0.1.0"),        "empty latest → no notify");
            expect(  UpdateChecker::isNewerVersion("0.2.0", ""),        "empty current → treat as newer");
        }
    }
};

static UpdateCheckerTests updateCheckerTests;
