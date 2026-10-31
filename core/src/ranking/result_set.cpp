// result_set.cpp — Bounded top-K min-heap.

#include "ali/ranking/result_set.h"

#include <algorithm>
#include <cassert>
#include <limits>

namespace ali {

ResultSet::ResultSet(uint32_t k) : k_(k) {
    heap_.reserve(k);
}

bool ResultSet::try_insert(DocId doc_id, float score) noexcept {
    if (k_ == 0) return false;

    if (heap_.size() < k_) {
        heap_.push_back({doc_id, score});
        std::push_heap(heap_.begin(), heap_.end(),
                       [](const Candidate& a, const Candidate& b) {
                           return a.score > b.score; // min-heap comparator
                       });
        return true;
    }

    // heap_.front() is the minimum element.
    if (score <= heap_.front().score) return false;

    std::pop_heap(heap_.begin(), heap_.end(),
                  [](const Candidate& a, const Candidate& b) {
                      return a.score > b.score;
                  });
    heap_.back() = {doc_id, score};
    std::push_heap(heap_.begin(), heap_.end(),
                   [](const Candidate& a, const Candidate& b) {
                       return a.score > b.score;
                   });
    return true;
}

float ResultSet::threshold() const noexcept {
    if (heap_.size() < k_) return -std::numeric_limits<float>::infinity();
    return heap_.front().score;
}

bool ResultSet::full() const noexcept {
    return heap_.size() >= k_;
}

uint32_t ResultSet::size() const noexcept {
    return static_cast<uint32_t>(heap_.size());
}

uint32_t ResultSet::capacity() const noexcept {
    return k_;
}

std::vector<Candidate> ResultSet::top_k() const {
    std::vector<Candidate> sorted = heap_;
    std::sort(sorted.begin(), sorted.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.score > b.score;
              });
    return sorted;
}

void ResultSet::clear() noexcept {
    heap_.clear();
}

} // namespace ali
