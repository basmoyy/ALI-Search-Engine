#pragma once
// ali/discovery/grammar_registry.h — Grammar Registry
//
// A registry of URL construction rules for authoritative structured sources.
// For a query "who invented the microwave?", the registry identifies
// Wikipedia as the best grammar match and constructs:
//   https://en.wikipedia.org/wiki/Microwave_oven
// ...without having that page in the index.
//
// Grammar types:
//   TOPIC_URL    — construct a URL from a normalised topic name
//   SEARCH_URL   — construct a search URL from the raw query
//
// Query type classification is based on a lightweight n-gram lookup table.
// No neural model is required.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ali {

/// Query type categories for grammar selection.
enum class QueryType : uint8_t {
    Factoid       = 0,  ///< "who invented X", "what is Y"
    Encyclopedia  = 1,  ///< "X (topic)" — general lookup
    Technical     = 2,  ///< programming, APIs, debugging
    Academic      = 3,  ///< research papers, studies
    News          = 4,  ///< current events, recent developments
    Reference     = 5,  ///< documentation, manuals, specifications
    Entertainment = 6,  ///< films, music, games, books
    Local         = 7,  ///< location-based, maps
    Unknown       = 8,
};

enum class GrammarType : uint8_t {
    TopicUrl  = 0,  ///< base + normalize(topic) → URL
    SearchUrl = 1,  ///< base + encode(query)    → URL
};

struct Grammar {
    uint32_t    grammar_id  = 0;
    std::string name;              ///< e.g. "Wikipedia"
    std::string base_url;          ///< e.g. "https://en.wikipedia.org/wiki/"
    std::string search_fallback;   ///< fallback search URL pattern
    GrammarType type        = GrammarType::TopicUrl;
    uint8_t     authority   = 200; ///< 0–255; pre-set high for known authorities
    std::vector<QueryType> covers; ///< query types this grammar handles well
};

/// A candidate URL constructed by a grammar.
struct GrammarResult {
    std::string url;
    std::string domain;
    std::string grammar_name;
    uint8_t     authority   = 0;
    float       confidence  = 0.0f;  ///< 0.0–1.0 grammar match confidence
};

/// Classifies a query into its primary QueryType.
QueryType classify_query(const std::string& query);

/// Registry of URL construction grammars.
class GrammarRegistry {
public:
    GrammarRegistry();

    /// Load the built-in grammar set (called once at startup).
    void load_defaults();

    /// Construct candidate URLs for `query` using matching grammars.
    /// Returns up to `max_results` candidates sorted by authority.
    [[nodiscard]] std::vector<GrammarResult>
        construct(const std::string& query, uint32_t max_results = 5) const;

    /// Normalise a topic string into a URL-safe form for TopicUrl grammars.
    static std::string normalise_topic(const std::string& raw);

    [[nodiscard]] uint32_t size() const noexcept {
        return static_cast<uint32_t>(grammars_.size());
    }

private:
    std::vector<Grammar> grammars_;
};

} // namespace ali
