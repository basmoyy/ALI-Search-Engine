// KnownSites.swift — Curated database of known websites and their official URLs.
//
// This replaces blind URL construction ("discord" → "discord.com") with a
// real lookup against verified site information. If a query matches a known
// site, we return the actual official URL with a real description.
//
// This is intentionally a curated, finite list — NOT an exhaustive web index.
// It covers the most commonly searched sites and brands. The ALI crawler
// handles discovery beyond this list.

import Foundation

struct KnownSite {
    let name:        String          // canonical display name
    let url:         String          // official URL
    let description: String          // one-line description
    let aliases:     [String]        // alternative names/spellings
    let category:    String          // e.g. "social", "video", "news"
}

// MARK: - Database

enum KnownSites {

    // Returns sites matching the query. Checks name and aliases case-insensitively.
    // Returns empty array if no match — no guessing, no construction.
    static func match(query: String) -> [KnownSite] {
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        guard !q.isEmpty, q.count >= 2 else { return [] }

        // Exact match first, then prefix match
        let exact  = all.filter { site in
            site.name.lowercased() == q ||
            site.aliases.contains(where: { $0.lowercased() == q })
        }
        if !exact.isEmpty { return exact }

        let prefix = all.filter { site in
            site.name.lowercased().hasPrefix(q) ||
            site.aliases.contains(where: { $0.lowercased().hasPrefix(q) })
        }
        return prefix
    }

    // Returns true only if query is clearly navigational (exact match to a known site).
    static func isNavigational(_ query: String) -> Bool {
        !match(query: query).isEmpty
    }

    // MARK: - Curated site list

    static let all: [KnownSite] = [

        // ── Video ─────────────────────────────────────────────────────────────
        KnownSite(name: "YouTube",   url: "https://www.youtube.com",
                  description: "Watch videos, music and live streams.",
                  aliases: ["yt", "you tube"], category: "video"),
        KnownSite(name: "Twitch",    url: "https://www.twitch.tv",
                  description: "Live streaming for gaming and more.",
                  aliases: [], category: "video"),
        KnownSite(name: "Vimeo",     url: "https://vimeo.com",
                  description: "High-quality video hosting.",
                  aliases: [], category: "video"),
        KnownSite(name: "Netflix",   url: "https://www.netflix.com",
                  description: "Stream movies and TV shows.",
                  aliases: [], category: "video"),

        // ── Social ────────────────────────────────────────────────────────────
        KnownSite(name: "Discord",   url: "https://discord.com",
                  description: "Messaging and voice chat for communities.",
                  aliases: ["discord app"], category: "social"),
        KnownSite(name: "Reddit",    url: "https://www.reddit.com",
                  description: "News, discussion and community forums.",
                  aliases: ["reddit.com"], category: "social"),
        KnownSite(name: "Twitter",   url: "https://twitter.com",
                  description: "Real-time public conversation.",
                  aliases: ["x", "x.com", "tweet"], category: "social"),
        KnownSite(name: "Instagram", url: "https://www.instagram.com",
                  description: "Photo and video sharing.",
                  aliases: ["insta", "ig"], category: "social"),
        KnownSite(name: "Facebook",  url: "https://www.facebook.com",
                  description: "Social network.",
                  aliases: ["fb"], category: "social"),
        KnownSite(name: "LinkedIn",  url: "https://www.linkedin.com",
                  description: "Professional networking.",
                  aliases: [], category: "social"),
        KnownSite(name: "TikTok",    url: "https://www.tiktok.com",
                  description: "Short-form video.",
                  aliases: ["tik tok"], category: "social"),
        KnownSite(name: "Mastodon",  url: "https://mastodon.social",
                  description: "Open-source decentralised social network.",
                  aliases: [], category: "social"),

        // ── Search & Reference ────────────────────────────────────────────────
        KnownSite(name: "Wikipedia", url: "https://en.wikipedia.org",
                  description: "Free encyclopaedia.",
                  aliases: ["wiki"], category: "reference"),
        KnownSite(name: "Google",    url: "https://www.google.com",
                  description: "Search engine.",
                  aliases: [], category: "search"),
        KnownSite(name: "DuckDuckGo",url: "https://duckduckgo.com",
                  description: "Privacy-focused search engine.",
                  aliases: ["ddg"], category: "search"),
        KnownSite(name: "Bing",      url: "https://www.bing.com",
                  description: "Microsoft search engine.",
                  aliases: [], category: "search"),
        KnownSite(name: "Wolfram Alpha", url: "https://www.wolframalpha.com",
                  description: "Computational knowledge engine.",
                  aliases: ["wolfram", "wolframalpha"], category: "reference"),

        // ── News ──────────────────────────────────────────────────────────────
        KnownSite(name: "BBC",       url: "https://www.bbc.com",
                  description: "British Broadcasting Corporation news.",
                  aliases: ["bbc news"], category: "news"),
        KnownSite(name: "CNN",       url: "https://www.cnn.com",
                  description: "Cable News Network.",
                  aliases: [], category: "news"),
        KnownSite(name: "Reuters",   url: "https://www.reuters.com",
                  description: "International news agency.",
                  aliases: [], category: "news"),
        KnownSite(name: "AP News",   url: "https://apnews.com",
                  description: "Associated Press — independent news.",
                  aliases: ["ap", "associated press"], category: "news"),
        KnownSite(name: "The Guardian", url: "https://www.theguardian.com",
                  description: "British newspaper and online news.",
                  aliases: ["guardian"], category: "news"),
        KnownSite(name: "NPR",       url: "https://www.npr.org",
                  description: "National Public Radio news.",
                  aliases: [], category: "news"),
        KnownSite(name: "Hacker News", url: "https://news.ycombinator.com",
                  description: "Tech and startup news aggregator.",
                  aliases: ["hn", "ycombinator"], category: "news"),

        // ── Tech & Development ────────────────────────────────────────────────
        KnownSite(name: "GitHub",    url: "https://github.com",
                  description: "Code hosting and collaboration.",
                  aliases: ["gh"], category: "tech"),
        KnownSite(name: "Stack Overflow", url: "https://stackoverflow.com",
                  description: "Q&A for programmers.",
                  aliases: ["so", "stackoverflow"], category: "tech"),
        KnownSite(name: "MDN",       url: "https://developer.mozilla.org",
                  description: "Web development documentation.",
                  aliases: ["mdn web docs", "mozilla developer"], category: "tech"),
        KnownSite(name: "npm",       url: "https://www.npmjs.com",
                  description: "Node.js package registry.",
                  aliases: ["npmjs"], category: "tech"),
        KnownSite(name: "PyPI",      url: "https://pypi.org",
                  description: "Python package index.",
                  aliases: ["python packages"], category: "tech"),
        KnownSite(name: "Rust",      url: "https://www.rust-lang.org",
                  description: "The Rust programming language.",
                  aliases: ["rust lang", "rust-lang"], category: "tech"),
        KnownSite(name: "Swift",     url: "https://www.swift.org",
                  description: "Apple's Swift programming language.",
                  aliases: ["swift lang"], category: "tech"),
        KnownSite(name: "Homebrew",  url: "https://brew.sh",
                  description: "Package manager for macOS.",
                  aliases: ["brew"], category: "tech"),

        // ── Shopping & Finance ────────────────────────────────────────────────
        KnownSite(name: "Amazon",    url: "https://www.amazon.com",
                  description: "Online retailer.",
                  aliases: ["amzn"], category: "shopping"),
        KnownSite(name: "eBay",      url: "https://www.ebay.com",
                  description: "Online marketplace.",
                  aliases: [], category: "shopping"),

        // ── Entertainment & Culture ───────────────────────────────────────────
        KnownSite(name: "IMDb",      url: "https://www.imdb.com",
                  description: "Films, TV, and entertainment database.",
                  aliases: ["internet movie database"], category: "entertainment"),
        KnownSite(name: "Spotify",   url: "https://www.spotify.com",
                  description: "Music streaming.",
                  aliases: [], category: "entertainment"),
        KnownSite(name: "Letterboxd",url: "https://letterboxd.com",
                  description: "Film discovery and social network.",
                  aliases: [], category: "entertainment"),

        // ── Tools & Productivity ──────────────────────────────────────────────
        KnownSite(name: "Notion",    url: "https://www.notion.so",
                  description: "Notes and project management.",
                  aliases: [], category: "productivity"),
        KnownSite(name: "Figma",     url: "https://www.figma.com",
                  description: "Collaborative design tool.",
                  aliases: [], category: "productivity"),

        // ── Countries & Tourism ───────────────────────────────────────────────
        KnownSite(name: "Japan",     url: "https://www.japan.travel/en/",
                  description: "Official Japan tourism information.",
                  aliases: ["japan tourism", "visit japan"], category: "travel"),
        KnownSite(name: "France",    url: "https://www.france.fr/en",
                  description: "Official France tourism information.",
                  aliases: ["visit france"], category: "travel"),
        KnownSite(name: "Italy",     url: "https://www.italia.it/en",
                  description: "Official Italy tourism information.",
                  aliases: [], category: "travel"),

        // ── Science & Education ───────────────────────────────────────────────
        KnownSite(name: "NASA",      url: "https://www.nasa.gov",
                  description: "US space agency.",
                  aliases: [], category: "science"),
        KnownSite(name: "arXiv",     url: "https://arxiv.org",
                  description: "Open-access scientific research papers.",
                  aliases: ["arxiv"], category: "science"),
        KnownSite(name: "PubMed",    url: "https://pubmed.ncbi.nlm.nih.gov",
                  description: "Biomedical research database.",
                  aliases: [], category: "science"),
        KnownSite(name: "Khan Academy", url: "https://www.khanacademy.org",
                  description: "Free online education.",
                  aliases: ["khan"], category: "education"),
        KnownSite(name: "Coursera",  url: "https://www.coursera.org",
                  description: "Online courses from universities.",
                  aliases: [], category: "education"),

        // ── Health ────────────────────────────────────────────────────────────
        KnownSite(name: "WebMD",     url: "https://www.webmd.com",
                  description: "Health information and medical news.",
                  aliases: [], category: "health"),
        KnownSite(name: "NHS",       url: "https://www.nhs.uk",
                  description: "UK National Health Service.",
                  aliases: [], category: "health"),

        // ── Misc ──────────────────────────────────────────────────────────────
        KnownSite(name: "Archive.org", url: "https://archive.org",
                  description: "Internet Archive — digital library.",
                  aliases: ["internet archive", "wayback machine"], category: "archive"),
        KnownSite(name: "Patreon",   url: "https://www.patreon.com",
                  description: "Creator subscription platform.",
                  aliases: [], category: "misc"),
        KnownSite(name: "Kickstarter", url: "https://www.kickstarter.com",
                  description: "Crowdfunding for creative projects.",
                  aliases: [], category: "misc"),
    ]
}
