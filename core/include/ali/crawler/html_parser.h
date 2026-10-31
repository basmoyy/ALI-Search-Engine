#pragma once
// ali/crawler/html_parser.h — Safe HTML parser for crawled content.
//
// Extracts: canonical URL, title, plain-text body, outbound links.
// Removes boilerplate (nav, footer, scripts, styles) where feasible.
// Handles malformed and adversarial HTML safely — never panics.

#include <cstdint>
#include <string>
#include <vector>

namespace ali {

struct ParseConfig {
    uint32_t max_title_bytes     = 512;
    uint32_t max_body_bytes      = 256 * 1024;  ///< extracted plain-text cap
    uint32_t max_links           = 500;
    bool     extract_links       = true;
    bool     strip_boilerplate   = true;
};

struct ParseResult {
    std::string              canonical_url;
    std::string              title;
    std::string              body_text;    ///< extracted, boilerplate-removed
    std::vector<std::string> links;        ///< absolute URLs found on the page
    bool                     parse_error  = false;
    std::string              error_detail;
};

class HtmlParser {
public:
    explicit HtmlParser(ParseConfig cfg = {});

    /// Parse raw HTML bytes for a given base URL.
    /// Never throws — returns parse_error = true on malformed input.
    [[nodiscard]] ParseResult parse(const std::vector<uint8_t>& html,
                                     const std::string&           base_url) const;

    [[nodiscard]] const ParseConfig& config() const noexcept;

private:
    ParseConfig cfg_;

    /// Resolve a possibly-relative href against base_url.
    static std::string resolve_url(const std::string& base,
                                    const std::string& href);

    /// Strip HTML tags from a byte range, returning plain text.
    static std::string strip_tags(const char* begin, const char* end);
};

} // namespace ali
