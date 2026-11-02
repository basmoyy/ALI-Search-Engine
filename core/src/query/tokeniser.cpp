// tokeniser.cpp — English tokeniser with lower-case folding and stop-word removal.

#include "ali/query/tokeniser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace ali {

// Basic English stop-word list. Extend as needed.
static const std::unordered_set<std::string> kEnglishStopwords = {
    "a","an","and","are","as","at","be","by","for","from","has","he",
    "in","is","it","its","of","on","or","that","the","to","was","were",
    "will","with","this","their","there","they","we","you","your","about",
    "also","but","do","if","into","just","more","my","no","not","our","so",
    "than","then","these","up","use","used","what","which","who","would",
};

struct Tokeniser::Impl {
    TokeniserConfig cfg;
    std::unordered_set<std::string> stopwords;
};

Tokeniser::Tokeniser(TokeniserConfig cfg) : impl_(new Impl{cfg, kEnglishStopwords}) {}
Tokeniser::~Tokeniser() { delete impl_; }

static bool is_separator(char c) {
    return !std::isalnum(static_cast<unsigned char>(c)) && c != '\'';
}

std::vector<Token> Tokeniser::tokenise_query(std::string_view text) const {
    // Use min(max_tokens, max_query_tokens) as the cap for query tokenisation
    const uint32_t saved_max = impl_->cfg.max_tokens;
    impl_->cfg.max_tokens = std::min(saved_max, impl_->cfg.max_query_tokens);
    auto result = tokenise_document(text);
    impl_->cfg.max_tokens = saved_max;
    return result;
}

std::vector<Token> Tokeniser::tokenise_document(std::string_view text) const {
    std::vector<Token> result;
    result.reserve(64);

    std::string tok;
    tok.reserve(32);
    uint32_t pos = 0;

    auto emit = [&]() {
        if (tok.empty()) return;
        if (tok.size() > impl_->cfg.max_token_length) { tok.clear(); return; }
        if (impl_->cfg.remove_stopwords && impl_->stopwords.count(tok)) {
            tok.clear(); return;
        }
        if (result.size() >= impl_->cfg.max_tokens) { tok.clear(); return; }
        result.push_back({tok, pos});
        tok.clear();
        ++pos;
    };

    for (char c : text) {
        if (is_separator(c)) {
            emit();
        } else {
            tok.push_back(impl_->cfg.lowercase
                          ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                          : c);
        }
    }
    emit();
    return result;
}

std::vector<std::pair<std::string, uint32_t>>
Tokeniser::compute_term_freqs(const std::vector<Token>& tokens) {
    std::unordered_map<std::string, uint32_t> freq;
    freq.reserve(tokens.size());
    for (const auto& t : tokens) ++freq[t.text];

    std::vector<std::pair<std::string, uint32_t>> result(freq.begin(), freq.end());
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace ali
