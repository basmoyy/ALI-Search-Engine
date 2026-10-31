// query_parser.cpp — Parses a raw query string into a Query struct.
// v0.1: AND-by-default multi-term query.

#include "ali/query/query.h"
#include "ali/query/tokeniser.h"

namespace ali {

Query parse_query(std::string_view raw, const Tokeniser& tokeniser, uint32_t top_k) {
    Query q;
    q.raw_text = std::string(raw);
    q.tokens   = tokeniser.tokenise_query(raw);
    q.op       = Query::Operator::AND;
    q.top_k    = (top_k > 0 && top_k <= 100) ? top_k : 10;
    return q;
}

} // namespace ali
