#ifndef COSC3500_PROJ_M1_SEARCH_HPP
#define COSC3500_PROJ_M1_SEARCH_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace m1 {

struct Graph {
    std::vector<std::size_t> offsets;
    std::vector<std::uint32_t> destinations;
    std::vector<std::uint32_t> costs;
};

struct Path {
    std::vector<std::uint32_t> vertices;
    std::uint64_t cost = 0U;
    std::size_t expanded = 0U;
};

using Heuristic = std::uint64_t (*)(std::uint32_t, std::uint32_t,
                                    void *) noexcept;

[[nodiscard]] std::optional<Path> breadth_first_path(
    const Graph &graph, std::uint32_t start, std::uint32_t goal,
    std::size_t max_expansions = std::numeric_limits<std::size_t>::max(),
    std::size_t *expanded = nullptr);
[[nodiscard]] std::optional<Path> dijkstra_path(
    const Graph &graph, std::uint32_t start, std::uint32_t goal,
    std::size_t max_expansions = std::numeric_limits<std::size_t>::max(),
    std::size_t *expanded = nullptr);
[[nodiscard]] std::optional<Path> a_star_path(
    const Graph &graph, std::uint32_t start, std::uint32_t goal,
    Heuristic heuristic, void *context = nullptr,
    std::size_t max_expansions = std::numeric_limits<std::size_t>::max(),
    std::size_t *expanded = nullptr);

constexpr std::size_t search_max_moves = 128U;

struct TurnRules {
    void *context = nullptr;
    [[nodiscard]] std::uint64_t (*hash)(void *) noexcept = nullptr;
    [[nodiscard]] bool (*terminal)(void *, int &) noexcept = nullptr;
    [[nodiscard]] int (*player)(void *) noexcept = nullptr;
    [[nodiscard]] std::size_t (*moves)(void *, std::uint32_t *,
                                       std::size_t) noexcept = nullptr;
    [[nodiscard]] bool (*apply)(void *, std::uint32_t) noexcept = nullptr;
    void (*undo)(void *, std::uint32_t) noexcept = nullptr;
    [[nodiscard]] int (*evaluate)(void *) noexcept = nullptr;
};

struct SearchBudget {
    std::size_t depth = 1U;
    std::size_t nodes = 1U;
    std::size_t table_entries = 0U;
    std::uint64_t seed = 0U;
};

struct MoveChoice {
    std::uint32_t move = 0U;
    int score = 0;
    std::size_t nodes = 0U;
};

[[nodiscard]] std::optional<MoveChoice>
alpha_beta_move(const TurnRules &rules, const SearchBudget &budget);
[[nodiscard]] std::optional<MoveChoice> mcts_move(const TurnRules &rules,
                                                  const SearchBudget &budget);

[[nodiscard]] int search_self_test();

} // namespace m1

#endif
