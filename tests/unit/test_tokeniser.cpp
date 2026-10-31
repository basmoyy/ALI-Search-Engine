// test_tokeniser.cpp — Tokeniser correctness tests.

#include "ali/query/tokeniser.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace ali;

TEST(Tokeniser, BasicLowercaseSplit) {
    Tokeniser t;
    auto toks = t.tokenise_query("Hello World");
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[0].text, "hello");
    EXPECT_EQ(toks[1].text, "world");
}

TEST(Tokeniser, RemovesStopwords) {
    Tokeniser t;
    auto toks = t.tokenise_query("the quick brown fox");
    // "the" is a stop word; should be removed
    for (const auto& tok : toks) {
        EXPECT_NE(tok.text, "the");
    }
    EXPECT_FALSE(toks.empty());
}

TEST(Tokeniser, PunctuationSeparates) {
    Tokeniser t;
    auto toks = t.tokenise_query("foo.bar,baz");
    ASSERT_EQ(toks.size(), 3u);
}

TEST(Tokeniser, EmptyInputProducesNoTokens) {
    Tokeniser t;
    auto toks = t.tokenise_query("");
    EXPECT_TRUE(toks.empty());
}

TEST(Tokeniser, ComputeTermFreqs) {
    Tokeniser t;
    auto toks = t.tokenise_document("apple orange apple banana apple");
    auto freqs = Tokeniser::compute_term_freqs(toks);
    bool found_apple = false;
    for (const auto& [term, freq] : freqs) {
        if (term == "apple") {
            EXPECT_EQ(freq, 3u);
            found_apple = true;
        }
    }
    EXPECT_TRUE(found_apple);
}

TEST(Tokeniser, MaxTokensEnforced) {
    TokeniserConfig cfg;
    cfg.max_tokens = 4;
    Tokeniser t(cfg);
    auto toks = t.tokenise_query("one two three four five six");
    EXPECT_LE(toks.size(), 4u);
}
