#pragma once
// ali/query/tokeniser.h — Text normalisation and tokenisation.
//
// v0.1 targets English with a simple whitespace + punctuation splitter
// and lower-case folding. The interface is designed for extension to
// additional languages without changing the query pipeline.

#include "ali/query/query.h"

#include <string>
#include <string_view>
#include <vector>

namespace ali {

struct TokeniserConfig {
    uint32_t max_token_length   = 64;
    uint32_t max_tokens         = 512;   ///< hard cap on tokens per document
    uint32_t max_query_tokens   = 32;    ///< separate cap for query strings
    bool     lowercase          = true;
    bool     remove_punctuation = true;
    bool     remove_stopwords   = true;
};

class Tokeniser {
public:
    explicit Tokeniser(TokeniserConfig cfg = {});
    ~Tokeniser();

    /// Tokenise a query string. Returns at most cfg.max_tokens tokens.
    [[nodiscard]] std::vector<Token> tokenise_query(std::string_view text) const;

    /// Tokenise document body. Returns a flat term list; duplicates are kept
    /// so that the caller can compute term frequencies.
    [[nodiscard]] std::vector<Token> tokenise_document(std::string_view text) const;

    /// Compute term-frequency map from a flat token list.
    [[nodiscard]] static std::vector<std::pair<std::string, uint32_t>>
        compute_term_freqs(const std::vector<Token>& tokens);

private:
    struct Impl;
    struct Impl* impl_;   // raw pointer; trivially destructed
};

} // namespace ali
