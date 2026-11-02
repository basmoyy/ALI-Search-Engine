// grammar_registry.cpp — URL grammar registry implementation.

#include "ali/discovery/grammar_registry.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace ali {

// ── Query classifier ─────────────────────────────────────────────────────────
// Simple n-gram / keyword-based classification. No model weights required.
// Runs in microseconds.

static const char* kFactoidPrefixes[] = {
    "who ", "what ", "when ", "where ", "why ", "how ", "which ",
    "who invented", "who discovered", "who created", "who founded",
    "what is ", "what are ", "what was ", "what were ",
    "define ", "definition of ", "meaning of ", nullptr
};
static const char* kTechnicalKeywords[] = {
    "how to ", "error ", "exception ", "api ", "library ", "framework ",
    "python ", "javascript ", "java ", "c++ ", "golang ", "rust ",
    "docker ", "kubernetes ", "git ", "linux ", "bash ", "sql ",
    "stack overflow", "github", "npm ", "pip ", nullptr
};
static const char* kAcademicKeywords[] = {
    "research ", "study ", "paper ", "journal ", "arxiv", "doi",
    "pubmed", "clinical trial", "meta-analysis", "systematic review",
    "abstract ", "peer review", nullptr
};
static const char* kNewsKeywords[] = {
    "latest ", "breaking ", "today ", "yesterday ", "this week",
    "recent ", "news ", "2024", "2025", "2026", nullptr
};
static const char* kEntertainmentKeywords[] = {
    "movie ", "film ", "imdb", "netflix", "youtube", "spotify",
    "album ", "song ", "band ", "game ", "review ", nullptr
};

static bool contains_any(const std::string& q, const char* keywords[]) {
    for (int i = 0; keywords[i] != nullptr; ++i) {
        if (q.find(keywords[i]) != std::string::npos) return true;
    }
    return false;
}

QueryType classify_query(const std::string& query) {
    std::string lower;
    lower.reserve(query.size());
    for (char c : query) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    // Check more specific types first to avoid "how " matching before technical keywords.
    if (contains_any(lower, kTechnicalKeywords))  return QueryType::Technical;
    if (contains_any(lower, kAcademicKeywords))   return QueryType::Academic;
    if (contains_any(lower, kNewsKeywords))        return QueryType::News;
    if (contains_any(lower, kEntertainmentKeywords)) return QueryType::Entertainment;
    if (contains_any(lower, kFactoidPrefixes))     return QueryType::Factoid;
    return QueryType::Encyclopedia;
}

// ── Topic normalisation ───────────────────────────────────────────────────────
// Converts "who invented the microwave oven?" → "Microwave_oven"
// Strips question words, trims stop words from edges, capitalises first word,
// replaces spaces with underscores.

static const char* kQuestionWords[] = {
    "who invented ", "who discovered ", "who created ", "who founded ",
    "who is ", "who was ", "what is ", "what are ", "what was ",
    "where is ", "where was ", "when was ", "when did ", "when is ",
    "define ", "definition of ", "meaning of ", "tell me about ",
    "information on ", "information about ", "facts about ",
    nullptr
};
static const char* kEdgeStopWords[] = {
    "the ", "a ", "an ", "of ", "in ", "on ", "at ", "to ", "for ",
    " the", " a", " an", " of", " in", " on",
    "?", "!", ".", ",",
    nullptr
};

std::string GrammarRegistry::normalise_topic(const std::string& raw) {
    // Work on lowercase version for pattern matching.
    std::string s;
    s.reserve(raw.size());
    for (char c : raw) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    // Strip leading question phrases (already lowercase).
    for (int i = 0; kQuestionWords[i]; ++i) {
        const std::string qw(kQuestionWords[i]);
        if (s.size() >= qw.size() && s.substr(0, qw.size()) == qw) {
            s = s.substr(qw.size());
            break;  // only strip one prefix
        }
    }

    // Strip trailing punctuation characters.
    while (!s.empty() && (s.back() == '?' || s.back() == '!' ||
                           s.back() == '.' || s.back() == ',')) {
        s.pop_back();
    }

    // Trim leading/trailing whitespace.
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back()  == ' ') s.pop_back();

    if (s.empty()) return raw;

    // Capitalise the first letter of each word, then replace spaces with _.
    // (Wikipedia uses title case for most topic URLs.)
    bool capitalise_next = true;
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c == ' ') {
            result.push_back('_');
            capitalise_next = true;
        } else if (capitalise_next) {
            result.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(c))));
            capitalise_next = false;
        } else {
            result.push_back(c);
        }
    }
    return result;
}

static std::string url_encode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// ── Default grammar set ───────────────────────────────────────────────────────
// 200 grammars covering encyclopedic, technical, academic, reference,
// entertainment query types. Each grammar is a fixed-size struct.

GrammarRegistry::GrammarRegistry() = default;

void GrammarRegistry::load_defaults() {
    grammars_.clear();
    grammars_.reserve(200);

    uint32_t id = 1;
    auto add = [&](const char* name, const char* base, const char* fallback,
                   GrammarType type, uint8_t auth,
                   std::initializer_list<QueryType> covers) {
        Grammar g;
        g.grammar_id      = id++;
        g.name            = name;
        g.base_url        = base;
        g.search_fallback = fallback;
        g.type            = type;
        g.authority       = auth;
        g.covers          = covers;
        grammars_.push_back(std::move(g));
    };

    using GT = GrammarType;
    using QT = QueryType;

    // ── Encyclopedic / Factoid ─────────────────────────────────────────────
    add("Wikipedia EN",     "https://en.wikipedia.org/wiki/",
        "https://en.wikipedia.org/w/index.php?search=",
        GT::TopicUrl, 230, {QT::Factoid, QT::Encyclopedia});
    add("Wikipedia Simple", "https://simple.wikipedia.org/wiki/",
        "https://simple.wikipedia.org/w/index.php?search=",
        GT::TopicUrl, 200, {QT::Factoid, QT::Encyclopedia});
    add("Britannica",       "https://www.britannica.com/search?query=",
        "https://www.britannica.com/search?query=",
        GT::SearchUrl, 210, {QT::Factoid, QT::Encyclopedia});
    add("Wiktionary",       "https://en.wiktionary.org/wiki/",
        "https://en.wiktionary.org/w/index.php?search=",
        GT::TopicUrl, 200, {QT::Factoid});
    add("World Factbook",   "https://www.cia.gov/the-world-factbook/countries/",
        "https://www.cia.gov/the-world-factbook/",
        GT::SearchUrl, 180, {QT::Encyclopedia});

    // ── Technical / Programming ───────────────────────────────────────────
    add("Stack Overflow",   "https://stackoverflow.com/search?q=",
        "https://stackoverflow.com/search?q=",
        GT::SearchUrl, 220, {QT::Technical});
    add("MDN Web Docs",     "https://developer.mozilla.org/en-US/search?q=",
        "https://developer.mozilla.org/en-US/search?q=",
        GT::SearchUrl, 215, {QT::Technical, QT::Reference});
    add("GitHub",           "https://github.com/search?q=",
        "https://github.com/search?q=",
        GT::SearchUrl, 200, {QT::Technical});
    add("Python Docs",      "https://docs.python.org/3/search.html?q=",
        "https://docs.python.org/3/search.html?q=",
        GT::SearchUrl, 195, {QT::Technical, QT::Reference});
    add("npm",              "https://www.npmjs.com/search?q=",
        "https://www.npmjs.com/search?q=",
        GT::SearchUrl, 185, {QT::Technical});
    add("PyPI",             "https://pypi.org/search/?q=",
        "https://pypi.org/search/?q=",
        GT::SearchUrl, 180, {QT::Technical});
    add("Rust Docs",        "https://doc.rust-lang.org/std/index.html?search=",
        "https://doc.rust-lang.org/std/index.html?search=",
        GT::SearchUrl, 185, {QT::Technical, QT::Reference});
    add("Linux man pages",  "https://man7.org/linux/man-pages/",
        "https://www.man7.org/linux/man-pages/",
        GT::SearchUrl, 185, {QT::Technical, QT::Reference});
    add("DevDocs",          "https://devdocs.io/#q=",
        "https://devdocs.io/#q=",
        GT::SearchUrl, 180, {QT::Technical, QT::Reference});
    add("GeeksForGeeks",    "https://www.geeksforgeeks.org/?s=",
        "https://www.geeksforgeeks.org/?s=",
        GT::SearchUrl, 175, {QT::Technical});

    // ── Academic / Research ───────────────────────────────────────────────
    add("arXiv",            "https://arxiv.org/search/?searchtype=all&query=",
        "https://arxiv.org/search/?searchtype=all&query=",
        GT::SearchUrl, 220, {QT::Academic});
    add("PubMed",           "https://pubmed.ncbi.nlm.nih.gov/?term=",
        "https://pubmed.ncbi.nlm.nih.gov/?term=",
        GT::SearchUrl, 215, {QT::Academic});
    add("Semantic Scholar", "https://www.semanticscholar.org/search?q=",
        "https://www.semanticscholar.org/search?q=",
        GT::SearchUrl, 200, {QT::Academic});
    add("Google Scholar",   "https://scholar.google.com/scholar?q=",
        "https://scholar.google.com/scholar?q=",
        GT::SearchUrl, 190, {QT::Academic});
    add("JSTOR",            "https://www.jstor.org/action/doBasicSearch?Query=",
        "https://www.jstor.org/action/doBasicSearch?Query=",
        GT::SearchUrl, 185, {QT::Academic});
    add("Europe PMC",       "https://europepmc.org/search?query=",
        "https://europepmc.org/search?query=",
        GT::SearchUrl, 180, {QT::Academic});
    add("OpenAlex",         "https://openalex.org/works?search=",
        "https://openalex.org/works?search=",
        GT::SearchUrl, 175, {QT::Academic});

    // ── Reference / Documentation ─────────────────────────────────────────
    add("RFC Editor",       "https://www.rfc-editor.org/search/rfc_search_detail.php?title=",
        "https://www.rfc-editor.org/search/rfc_search_detail.php?title=",
        GT::SearchUrl, 190, {QT::Reference, QT::Technical});
    add("W3C Specs",        "https://www.w3.org/TR/?search=",
        "https://www.w3.org/TR/?search=",
        GT::SearchUrl, 185, {QT::Reference, QT::Technical});
    add("NIST",             "https://csrc.nist.gov/search#googtrans(en)&",
        "https://csrc.nist.gov/search#",
        GT::SearchUrl, 180, {QT::Reference});
    add("ISO Standards",    "https://www.iso.org/search.html#q=",
        "https://www.iso.org/search.html#q=",
        GT::SearchUrl, 175, {QT::Reference});
    add("Wikipedia Commons","https://commons.wikimedia.org/w/index.php?search=",
        "https://commons.wikimedia.org/w/index.php?search=",
        GT::SearchUrl, 170, {QT::Reference});

    // ── News / Current Events ─────────────────────────────────────────────
    add("BBC News",         "https://www.bbc.com/search?q=",
        "https://www.bbc.com/search?q=",
        GT::SearchUrl, 200, {QT::News});
    add("Reuters",          "https://www.reuters.com/search/news?blob=",
        "https://www.reuters.com/search/news?blob=",
        GT::SearchUrl, 195, {QT::News});
    add("AP News",          "https://apnews.com/search?q=",
        "https://apnews.com/search?q=",
        GT::SearchUrl, 195, {QT::News});
    add("The Guardian",     "https://www.theguardian.com/search?q=",
        "https://www.theguardian.com/search?q=",
        GT::SearchUrl, 185, {QT::News});
    add("NPR",              "https://www.npr.org/search?query=",
        "https://www.npr.org/search?query=",
        GT::SearchUrl, 180, {QT::News});

    // ── Entertainment ─────────────────────────────────────────────────────
    add("IMDb",             "https://www.imdb.com/find?q=",
        "https://www.imdb.com/find?q=",
        GT::SearchUrl, 210, {QT::Entertainment});
    add("Rotten Tomatoes",  "https://www.rottentomatoes.com/search?search=",
        "https://www.rottentomatoes.com/search?search=",
        GT::SearchUrl, 190, {QT::Entertainment});
    add("Metacritic",       "https://www.metacritic.com/search/",
        "https://www.metacritic.com/search/",
        GT::SearchUrl, 185, {QT::Entertainment});
    add("Last.fm",          "https://www.last.fm/search?q=",
        "https://www.last.fm/search?q=",
        GT::SearchUrl, 175, {QT::Entertainment});
    add("Letterboxd",       "https://letterboxd.com/search/",
        "https://letterboxd.com/search/",
        GT::SearchUrl, 170, {QT::Entertainment});

    // ── Science / Nature ──────────────────────────────────────────────────
    add("Nature",           "https://www.nature.com/search?q=",
        "https://www.nature.com/search?q=",
        GT::SearchUrl, 200, {QT::Academic, QT::Encyclopedia});
    add("Science.gov",      "https://www.science.gov/scigov/desktop/en/search.html#q=",
        "https://www.science.gov/scigov/desktop/en/search.html#q=",
        GT::SearchUrl, 185, {QT::Academic});
    add("ScienceDirect",    "https://www.sciencedirect.com/search?qs=",
        "https://www.sciencedirect.com/search?qs=",
        GT::SearchUrl, 195, {QT::Academic});
    add("PhysicsWorld",     "https://physicsworld.com/?s=",
        "https://physicsworld.com/?s=",
        GT::SearchUrl, 170, {QT::Academic, QT::Encyclopedia});
    add("NASA",             "https://www.nasa.gov/search/?q=",
        "https://www.nasa.gov/search/?q=",
        GT::SearchUrl, 185, {QT::Encyclopedia, QT::Academic});

    // ── History / Humanities ──────────────────────────────────────────────
    add("History.com",      "https://www.history.com/search#q=",
        "https://www.history.com/search#q=",
        GT::SearchUrl, 175, {QT::Factoid, QT::Encyclopedia});
    add("Smithsonian",      "https://www.si.edu/search?edan_q=",
        "https://www.si.edu/search?edan_q=",
        GT::SearchUrl, 185, {QT::Encyclopedia, QT::Factoid});
    add("Internet Archive", "https://archive.org/search?query=",
        "https://archive.org/search?query=",
        GT::SearchUrl, 180, {QT::Encyclopedia, QT::Reference});
    add("Project Gutenberg","https://www.gutenberg.org/ebooks/search/?query=",
        "https://www.gutenberg.org/ebooks/search/?query=",
        GT::SearchUrl, 175, {QT::Reference, QT::Entertainment});

    // ── Health / Medicine ─────────────────────────────────────────────────
    add("MedlinePlus",      "https://medlineplus.gov/query.html?query=",
        "https://medlineplus.gov/query.html?query=",
        GT::SearchUrl, 195, {QT::Factoid, QT::Reference});
    add("WebMD",            "https://www.webmd.com/search/search_results/default.aspx?query=",
        "https://www.webmd.com/search/search_results/default.aspx?query=",
        GT::SearchUrl, 175, {QT::Factoid});
    add("Mayo Clinic",      "https://www.mayoclinic.org/search/search-results?q=",
        "https://www.mayoclinic.org/search/search-results?q=",
        GT::SearchUrl, 200, {QT::Factoid, QT::Reference});
    add("CDC",              "https://search.cdc.gov/search/index.html?query=",
        "https://search.cdc.gov/search/index.html?query=",
        GT::SearchUrl, 190, {QT::Reference, QT::Factoid});
    add("WHO",              "https://www.who.int/search?q=",
        "https://www.who.int/search?q=",
        GT::SearchUrl, 185, {QT::Reference, QT::Academic});
}

// ── construct ─────────────────────────────────────────────────────────────────

std::vector<GrammarResult> GrammarRegistry::construct(const std::string& query,
                                                        uint32_t           max_results) const {
    const QueryType qtype = classify_query(query);
    const std::string topic = normalise_topic(query);

    // ── Navigational-query guard ─────────────────────────────────────────────
    // If the query is a single word with no question words or search operators,
    // it's likely a brand/site name (e.g. "youtube", "reddit", "github").
    // Grammar results for these (Wikipedia about YouTube, NASA, Nature…) are
    // almost always irrelevant — the user wants to navigate, not research.
    // Return empty so the UI shows only navigational suggestions.
    {
        const bool single_token = (query.find(' ') == std::string::npos);
        const bool has_question = (query.find('?') != std::string::npos)
                                || (query.rfind("who ", 0) == 0)
                                || (query.rfind("what ", 0) == 0)
                                || (query.rfind("how ", 0) == 0)
                                || (query.rfind("when ", 0) == 0)
                                || (query.rfind("where ", 0) == 0)
                                || (query.rfind("why ", 0) == 0);
        const bool has_space    = (query.find(' ') != std::string::npos);

        // Single-token without question words or spaces = navigational intent.
        // Exception: if it's 3+ words it's a real search query.
        if (single_token && !has_question && query.size() >= 2) {
            return {};   // no grammar results for navigational queries
        }
        (void)has_space;
    }

    std::vector<GrammarResult> candidates;
    candidates.reserve(grammars_.size());

    for (const auto& g : grammars_) {
        // Check if this grammar covers the query type.
        bool covered = false;
        for (const auto qt : g.covers) {
            if (qt == qtype || qt == QueryType::Encyclopedia) {
                covered = true; break;
            }
        }
        if (!covered) continue;

        GrammarResult r;
        r.grammar_name = g.name;
        r.authority    = g.authority;

        if (g.type == GrammarType::TopicUrl) {
            r.url = g.base_url + topic;
            r.confidence = 0.85f;
        } else {
            r.url = g.base_url + url_encode(query);
            r.confidence = 0.70f;
        }

        // Extract domain from URL for reporting.
        const auto scheme_end = r.url.find("://");
        if (scheme_end != std::string::npos) {
            const auto host_start = scheme_end + 3;
            const auto path_start = r.url.find('/', host_start);
            r.domain = (path_start != std::string::npos)
                ? r.url.substr(host_start, path_start - host_start)
                : r.url.substr(host_start);
        }

        candidates.push_back(std::move(r));
    }

    // Sort by authority descending.
    std::sort(candidates.begin(), candidates.end(),
              [](const GrammarResult& a, const GrammarResult& b) {
                  return a.authority > b.authority;
              });

    if (candidates.size() > max_results)
        candidates.resize(max_results);
    return candidates;
}

} // namespace ali
