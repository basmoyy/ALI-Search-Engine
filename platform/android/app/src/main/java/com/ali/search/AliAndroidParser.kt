package com.ali.search

// AliAndroidParser.kt — Lightweight HTML content extractor.
//
// Extracts title, main body text and outbound links from raw HTML bytes.
// No external library — uses a single-pass string scan.
//
// Design:
//   - Skips <script>, <style>, <nav>, <footer>, <header>, <aside> blocks.
//   - Extracts <title> tag content.
//   - For Wikipedia specifically, targets #mw-content-text to reduce boilerplate.
//   - Collapses whitespace and trims to a configurable body limit.
//   - Resolves relative URLs against the base URL.
//   - Never throws — returns a best-effort result on malformed input.

data class ParsedPage(
    val url:         String,
    val title:       String,
    val bodyText:    String,
    val links:       List<String>,
    val parseError:  String? = null,
)

object AliAndroidParser {

    private const val MAX_BODY_CHARS   = 256_000
    private const val MAX_TITLE_CHARS  = 512
    private const val MAX_LINKS        = 200

    fun parse(html: ByteArray, baseUrl: String): ParsedPage {
        if (html.isEmpty()) {
            return ParsedPage(baseUrl, "", "", emptyList(), "Empty response body")
        }

        val text = try {
            // Try UTF-8 first, fall back to ISO-8859-1.
            String(html, Charsets.UTF_8)
        } catch (e: Exception) {
            String(html, Charsets.ISO_8859_1)
        }

        val title   = extractTitle(text)
        val body    = extractBody(text)
        val links   = extractLinks(text, baseUrl)

        return ParsedPage(
            url      = extractCanonical(text) ?: baseUrl,
            title    = title.take(MAX_TITLE_CHARS),
            bodyText = body.take(MAX_BODY_CHARS),
            links    = links,
        )
    }

    // ── Title ─────────────────────────────────────────────────────────────────

    private fun extractTitle(html: String): String {
        val m = Regex("<title[^>]*>(.*?)</title>",
                      setOf(RegexOption.IGNORE_CASE, RegexOption.DOT_MATCHES_ALL))
            .find(html) ?: return ""
        return stripTags(m.groupValues[1]).trim()
    }

    // ── Canonical URL ─────────────────────────────────────────────────────────

    private fun extractCanonical(html: String): String? {
        val m = Regex("""<link[^>]+rel=["']canonical["'][^>]+href=["']([^"']+)["']""",
                      RegexOption.IGNORE_CASE).find(html)
            ?: Regex("""<link[^>]+href=["']([^"']+)["'][^>]+rel=["']canonical["']""",
                     RegexOption.IGNORE_CASE).find(html)
        return m?.groupValues?.get(1)?.trim()
    }

    // ── Body text ─────────────────────────────────────────────────────────────

    private val SKIP_TAGS = setOf("script", "style", "nav", "footer",
                                   "header", "aside", "noscript", "svg")

    private fun extractBody(html: String): String {
        // For Wikipedia: prefer the article content div.
        val wikiContent = extractWikiContent(html)
        if (wikiContent.isNotBlank()) return wikiContent

        // General extraction: strip skip blocks then strip all tags.
        var working = html
        for (tag in SKIP_TAGS) {
            working = Regex("<$tag[\\s>][\\s\\S]*?</$tag>",
                            RegexOption.IGNORE_CASE).replace(working, " ")
        }
        return normaliseWhitespace(stripTags(working))
    }

    private fun extractWikiContent(html: String): String {
        // Wikipedia wraps article body in id="mw-content-text"
        val startTag = html.indexOf("id=\"mw-content-text\"")
            .takeIf { it >= 0 } ?: return ""
        val divStart = html.lastIndexOf("<div", startTag)
            .takeIf { it >= 0 } ?: return ""
        // Find the matching closing </div> by counting depth.
        var depth  = 0
        var i      = divStart
        while (i < html.length) {
            when {
                html.startsWith("<div", i, ignoreCase = true)  -> { depth++; i += 4 }
                html.startsWith("</div", i, ignoreCase = true) -> {
                    depth--
                    if (depth == 0) {
                        val content = html.substring(divStart, i + 6)
                        // Strip skip blocks within the article.
                        var cleaned = content
                        for (tag in SKIP_TAGS) {
                            cleaned = Regex("<$tag[\\s>][\\s\\S]*?</$tag>",
                                            RegexOption.IGNORE_CASE).replace(cleaned, " ")
                        }
                        return normaliseWhitespace(stripTags(cleaned))
                    }
                    i += 5
                }
                else -> i++
            }
        }
        return ""
    }

    // ── Links ─────────────────────────────────────────────────────────────────

    private fun extractLinks(html: String, baseUrl: String): List<String> {
        val links = mutableListOf<String>()
        val pattern = Regex("""<a\s[^>]*href=["']([^"'#][^"']*)["']""", RegexOption.IGNORE_CASE)
        for (match in pattern.findAll(html)) {
            if (links.size >= MAX_LINKS) break
            val href = match.groupValues[1].trim()
            val resolved = resolveUrl(baseUrl, href) ?: continue
            if (resolved.startsWith("http")) links.add(resolved)
        }
        return links
    }

    private fun resolveUrl(base: String, href: String): String? {
        if (href.isBlank()) return null
        if (href.startsWith("http://") || href.startsWith("https://")) return href
        if (href.startsWith("//")) {
            val scheme = if (base.startsWith("https")) "https" else "http"
            return "$scheme:$href"
        }
        if (href.startsWith("mailto:") || href.startsWith("javascript:") ||
            href.startsWith("data:") || href.startsWith("tel:")) return null

        val schemeEnd = base.indexOf("://")
        if (schemeEnd < 0) return null
        val hostEnd = base.indexOf("/", schemeEnd + 3).takeIf { it >= 0 } ?: base.length
        val origin  = base.substring(0, hostEnd)

        return if (href.startsWith("/")) "$origin$href"
        else {
            val lastSlash = base.lastIndexOf("/").takeIf { it > hostEnd } ?: hostEnd
            "${base.substring(0, lastSlash + 1)}$href"
        }
    }

    // ── Utilities ─────────────────────────────────────────────────────────────

    private fun stripTags(html: String): String {
        return Regex("<[^>]+>").replace(html, " ")
    }

    private fun normaliseWhitespace(text: String): String {
        return text.replace(Regex("&[a-zA-Z]+;"), " ")  // HTML entities
                   .replace(Regex("&#\\d+;"), " ")
                   .replace(Regex("\\s+"), " ")
                   .trim()
    }
}
