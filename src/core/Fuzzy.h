#pragma once
// Fuzzy matcher shared by all providers. Allocation-free hot path.
// Ranking: exact > prefix > word-start (spaces, -_./\, CamelCase) > acronym > substring > subsequence.
// Also matches the query typed in the wrong keyboard layout (EN<->RU), slightly penalised.
#include <string>
#include <string_view>

namespace cs::fuzzy {

struct Query {
  std::wstring lower;    // lowercased, trimmed
  std::wstring swapped;  // lower typed in the other layout (empty if identical)
};

Query Prepare(std::wstring_view raw);

// candidate: original text (case preserved — used for CamelCase word starts).
float Score(const Query& q, std::wstring_view candidate);

// Fast path when the caller caches a lowercased copy of the candidate (e.g. file index).
// `lower` must equal str::ToLower(original) and have the same length.
float ScoreLowered(const Query& q, std::wstring_view original, std::wstring_view lower);

}  // namespace cs::fuzzy
