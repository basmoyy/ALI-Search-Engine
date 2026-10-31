#!/usr/bin/env python3
# gen_corpus.py — Generate a synthetic benchmark corpus for ALI.
#
# Produces a TSV file (url TAB title TAB body_text) with realistic
# term distributions: short pages, long pages, rare terms, common terms,
# duplicate content (for dedup testing) and varied vocabulary.
#
# Usage:
#   python3 gen_corpus.py <num_docs> <output.tsv>
#
# Example:
#   python3 gen_corpus.py 10000 corpus_10k.tsv
#   python3 gen_corpus.py 100000 corpus_100k.tsv

import random
import sys
import os

# ── Vocabulary pools ──────────────────────────────────────────────────────────

TOPICS = [
    ("search engine", ["index", "query", "ranking", "retrieval", "document", "term",
                        "posting", "inverted", "bm25", "scoring", "relevance", "result"]),
    ("mobile device", ["android", "phone", "battery", "memory", "storage", "app",
                        "network", "wifi", "bluetooth", "screen", "sensor", "cpu"]),
    ("web crawler", ["crawler", "robots", "sitemap", "fetch", "parse", "html",
                      "link", "url", "domain", "redirect", "header", "http"]),
    ("compression", ["encode", "decode", "delta", "vbyte", "integer", "sequence",
                      "codec", "ratio", "throughput", "block", "chunk", "stream"]),
    ("database", ["sqlite", "transaction", "query", "index", "table", "row",
                   "column", "schema", "join", "primary", "foreign", "constraint"]),
    ("networking", ["tcp", "socket", "connection", "timeout", "packet", "protocol",
                     "port", "address", "server", "client", "request", "response"]),
    ("operating system", ["process", "thread", "memory", "scheduler", "kernel",
                           "filesystem", "cache", "interrupt", "signal", "pipe"]),
    ("machine learning", ["model", "training", "inference", "embedding", "vector",
                           "neural", "layer", "activation", "gradient", "loss"]),
    ("security", ["encryption", "certificate", "hash", "signature", "token",
                   "authentication", "authorization", "vulnerability", "exploit"]),
    ("performance", ["latency", "throughput", "benchmark", "profiling", "cache",
                      "memory", "cpu", "instruction", "pipeline", "branch"]),
]

COMMON_WORDS = [
    "the", "and", "for", "with", "from", "this", "that", "which", "when",
    "using", "based", "approach", "method", "system", "data", "local",
    "efficient", "fast", "small", "large", "new", "old", "high", "low",
]

def random_sentence(rng, topic_words, length=8):
    words = []
    for _ in range(length):
        if rng.random() < 0.3:
            words.append(rng.choice(COMMON_WORDS))
        else:
            words.append(rng.choice(topic_words))
    return " ".join(words)

def make_doc(rng, doc_id):
    topic_name, topic_words = rng.choice(TOPICS)
    num_sentences = rng.randint(3, 20)  # varied length pages

    title = f"{topic_name.title()} {doc_id}"
    sentences = [random_sentence(rng, topic_words, rng.randint(5, 15))
                 for _ in range(num_sentences)]
    body = " ".join(sentences)

    url = f"https://example.com/{topic_name.replace(' ', '-')}/{doc_id}"
    return url, title, body

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <num_docs> <output.tsv>", file=sys.stderr)
        sys.exit(1)

    num_docs  = int(sys.argv[1])
    out_path  = sys.argv[2]
    rng       = random.Random(42)   # fixed seed for reproducibility

    dup_rate  = 0.05  # 5% duplicates for dedup testing
    dup_pool  = []

    written = 0
    with open(out_path, "w", encoding="utf-8") as f:
        for i in range(num_docs):
            # Occasionally emit a duplicate
            if dup_pool and rng.random() < dup_rate:
                url, title, body = rng.choice(dup_pool)
                url = url + f"?dup={i}"
            else:
                url, title, body = make_doc(rng, i)
                if len(dup_pool) < 200:
                    dup_pool.append((url, title, body))

            # Escape tabs in body (shouldn't occur with our vocab but be safe)
            body = body.replace("\t", " ")
            f.write(f"{url}\t{title}\t{body}\n")
            written += 1

            if written % 10000 == 0:
                print(f"  {written}/{num_docs}", flush=True)

    print(f"Done. Wrote {written} documents to {out_path}")
    print(f"File size: {os.path.getsize(out_path) // 1024} KB")

if __name__ == "__main__":
    main()
