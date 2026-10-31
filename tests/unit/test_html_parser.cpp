// test_html_parser.cpp — HtmlParser correctness tests.
//
// Covers: title extraction, body text extraction, link discovery,
// canonical URL detection, malformed HTML safety, and config limits.

#include "ali/crawler/html_parser.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace ali;

static std::vector<uint8_t> to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

TEST(HtmlParser, ExtractsTitle) {
    HtmlParser p;
    auto r = p.parse(to_bytes("<html><head><title>Hello World</title></head><body></body></html>"),
                     "https://example.com/");
    EXPECT_EQ(r.title, "Hello World");
    EXPECT_FALSE(r.parse_error);
}

TEST(HtmlParser, ExtractsBodyText) {
    HtmlParser p;
    auto r = p.parse(to_bytes("<html><body><p>Quick brown fox</p></body></html>"),
                     "https://example.com/");
    EXPECT_NE(r.body_text.find("fox"), std::string::npos);
}

TEST(HtmlParser, SkipsScriptAndStyle) {
    HtmlParser p;
    const std::string html =
        "<html><body>"
        "<script>var x = 'should not appear';</script>"
        "<style>.foo { color: red; }</style>"
        "<p>visible text</p>"
        "</body></html>";
    auto r = p.parse(to_bytes(html), "https://example.com/");
    EXPECT_EQ(r.body_text.find("should not appear"), std::string::npos);
    EXPECT_NE(r.body_text.find("visible text"),      std::string::npos);
}

TEST(HtmlParser, ExtractsLinks) {
    HtmlParser p;
    const std::string html =
        "<html><body>"
        "<a href=\"https://other.com/page\">link</a>"
        "<a href=\"/relative\">rel</a>"
        "</body></html>";
    auto r = p.parse(to_bytes(html), "https://example.com/");
    ASSERT_GE(r.links.size(), 1u);
    bool found_absolute = false;
    for (const auto& l : r.links) {
        if (l.find("other.com") != std::string::npos) found_absolute = true;
    }
    EXPECT_TRUE(found_absolute);
}

TEST(HtmlParser, DetectsCanonicalUrl) {
    HtmlParser p;
    const std::string html =
        "<html><head>"
        "<link rel=\"canonical\" href=\"https://canonical.example.com/page\"/>"
        "</head><body></body></html>";
    auto r = p.parse(to_bytes(html), "https://example.com/other");
    EXPECT_EQ(r.canonical_url, "https://canonical.example.com/page");
}

TEST(HtmlParser, FallsBackToBaseUrl) {
    HtmlParser p;
    auto r = p.parse(to_bytes("<html><body>text</body></html>"),
                     "https://example.com/page");
    EXPECT_EQ(r.canonical_url, "https://example.com/page");
}

TEST(HtmlParser, EmptyInputReturnsParseError) {
    HtmlParser p;
    auto r = p.parse({}, "https://example.com/");
    EXPECT_TRUE(r.parse_error);
}

TEST(HtmlParser, TitleTruncatedAtLimit) {
    ParseConfig cfg;
    cfg.max_title_bytes = 10;
    HtmlParser p(cfg);
    auto r = p.parse(to_bytes("<title>This title is way too long</title>"),
                     "https://example.com/");
    EXPECT_LE(r.title.size(), 10u);
}

TEST(HtmlParser, MalformedHtmlDoesNotCrash) {
    HtmlParser p;
    const std::string broken = "<html><body><p>unclosed <b>tag <i>nesting";
    EXPECT_NO_THROW(p.parse(to_bytes(broken), "https://example.com/"));
}
