// html_parser.cpp — Safe HTML parser for crawled content.
//
// Extracts canonical URL, title, plain-text body and outbound links from
// raw HTML bytes. Handles malformed and adversarial HTML without panicking.
//
// Design:
//   - Single-pass byte scanner; no full DOM tree built.
//   - Tags are detected by < > delimiters; nesting is not tracked for plain
//     text extraction (adequate for search indexing, not rendering).
//   - <script>, <style>, <nav>, <footer>, <header> blocks are skipped to
//     reduce boilerplate in the extracted text.
//   - All string bounds are checked; truncation is applied at config limits.
//   - Outbound links are resolved to absolute URLs using the base URL.

#include "ali/crawler/html_parser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace ali {

HtmlParser::HtmlParser(ParseConfig cfg) : cfg_(std::move(cfg)) {}

const ParseConfig& HtmlParser::config() const noexcept { return cfg_; }

// ── Internal helpers ──────────────────────────────────────────────────────────

static std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Extract the value of an attribute from a tag string, e.g. href="..." or href='...'
static std::string extract_attr(const std::string& tag, const std::string& attr) {
    std::string key = attr + "=";
    std::size_t pos = lowercase(tag).find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    if (pos >= tag.size()) return {};
    char quote = tag[pos];
    if (quote != '"' && quote != '\'') return {};
    std::size_t end = tag.find(quote, pos + 1);
    if (end == std::string::npos) return {};
    return tag.substr(pos + 1, end - pos - 1);
}

// Collapse whitespace runs to single spaces.
static std::string normalise_whitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_ws = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_ws && !out.empty()) { out += ' '; in_ws = true; }
        } else {
            out += c;
            in_ws = false;
        }
    }
    return out;
}

// ── URL resolution ────────────────────────────────────────────────────────────

std::string HtmlParser::resolve_url(const std::string& base,
                                     const std::string& href) {
    if (href.empty()) return {};
    // Already absolute
    if (href.find("http://")  == 0) return href;
    if (href.find("https://") == 0) return href;
    // Skip javascript:, mailto:, data:, #fragment
    if (href.find(':') != std::string::npos) return {};
    if (href[0] == '#') return {};

    // Find base origin
    std::size_t scheme_end = base.find("://");
    if (scheme_end == std::string::npos) return {};
    std::size_t path_start = base.find('/', scheme_end + 3);
    std::string origin = (path_start == std::string::npos)
        ? base : base.substr(0, path_start);

    if (href[0] == '/') {
        return origin + href;
    }

    // Relative path — append to base directory
    std::size_t last_slash = base.rfind('/');
    std::string base_dir   = (last_slash == std::string::npos)
        ? base + "/" : base.substr(0, last_slash + 1);
    return base_dir + href;
}

// ── Tag stripper ──────────────────────────────────────────────────────────────

std::string HtmlParser::strip_tags(const char* begin, const char* end) {
    std::string out;
    out.reserve(static_cast<std::size_t>(end - begin));
    bool in_tag = false;
    for (const char* p = begin; p < end; ++p) {
        if (*p == '<') { in_tag = true; continue; }
        if (*p == '>') { in_tag = false; out += ' '; continue; }
        if (!in_tag) {
            out += *p;
        }
    }
    return out;
}

// ── Main parse ────────────────────────────────────────────────────────────────

ParseResult HtmlParser::parse(const std::vector<uint8_t>& html,
                               const std::string&           base_url) const {
    ParseResult result;

    if (html.empty()) {
        result.parse_error  = true;
        result.error_detail = "empty response body";
        return result;
    }

    const char* data = reinterpret_cast<const char*>(html.data());
    const std::size_t len = html.size();
    const char* end = data + len;

    // ── Single-pass scanner ───────────────────────────────────────────────────

    std::string title_text;
    std::string body_text;
    std::vector<std::string> links;
    std::string canonical_url;

    bool in_title         = false;
    bool in_skip_block    = false; // <script>, <style>, <nav>, <footer>, <header>
    std::size_t tag_start = 0;
    bool in_tag           = false;
    std::string current_tag;

    for (std::size_t i = 0; i < len; ++i) {
        char c = data[i];

        if (c == '<') {
            in_tag    = true;
            tag_start = i + 1;
            current_tag.clear();
            continue;
        }

        if (in_tag) {
            if (c == '>') {
                in_tag = false;
                std::string tag_lower = lowercase(current_tag);
                const bool is_close = (!tag_lower.empty() && tag_lower[0] == '/');
                std::string tag_name = is_close ? tag_lower.substr(1) : tag_lower;
                // Trim to first word
                std::size_t sp = tag_name.find_first_of(" \t\r\n");
                if (sp != std::string::npos) tag_name = tag_name.substr(0, sp);

                // Track skip blocks
                if (!is_close && (tag_name == "script" || tag_name == "style"
                               || tag_name == "nav"    || tag_name == "footer"
                               || tag_name == "header")) {
                    in_skip_block = true;
                }
                if (is_close && (tag_name == "script" || tag_name == "style"
                              || tag_name == "nav"    || tag_name == "footer"
                              || tag_name == "header")) {
                    in_skip_block = false;
                }

                // Title
                if (!is_close && tag_name == "title") in_title = true;
                if (is_close  && tag_name == "title") in_title = false;

                // Canonical link
                if (tag_name == "link") {
                    std::string rel  = lowercase(extract_attr(current_tag, "rel"));
                    std::string href = extract_attr(current_tag, "href");
                    if (rel == "canonical" && !href.empty()) {
                        canonical_url = href;
                    }
                }

                // Anchor links
                if (cfg_.extract_links && tag_name == "a") {
                    std::string href = extract_attr(current_tag, "href");
                    std::string resolved = resolve_url(base_url, href);
                    if (!resolved.empty()
                     && links.size() < cfg_.max_links) {
                        links.push_back(std::move(resolved));
                    }
                }
            } else {
                current_tag += c;
            }
            continue;
        }

        // Text content
        if (in_skip_block) continue;

        if (in_title) {
            if (title_text.size() < cfg_.max_title_bytes) title_text += c;
            continue;
        }

        if (body_text.size() < cfg_.max_body_bytes) {
            body_text += c;
        }
    }

    // ── Post-process ─────────────────────────────────────────────────────────

    result.title        = normalise_whitespace(title_text);
    result.body_text    = normalise_whitespace(body_text);
    result.links        = std::move(links);
    result.canonical_url = canonical_url.empty() ? base_url : canonical_url;

    // Truncate to limits
    if (result.title.size() > cfg_.max_title_bytes) {
        result.title.resize(cfg_.max_title_bytes);
    }
    if (result.body_text.size() > cfg_.max_body_bytes) {
        result.body_text.resize(cfg_.max_body_bytes);
    }

    return result;
}

} // namespace ali
