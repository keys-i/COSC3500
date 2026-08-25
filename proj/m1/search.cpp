#include "search.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <random>
#include <utility>

namespace m1 {
namespace {

constexpr int score_floor = std::numeric_limits<int>::min() / 4;
constexpr int score_ceiling = std::numeric_limits<int>::max() / 4;

struct QueueNode {
    std::uint64_t priority = 0U;
    std::uint64_t distance = 0U;
    std::uint32_t vertex = 0U;

    [[nodiscard]] bool operator>(const QueueNode &other) const noexcept {
        return std::tie(priority, vertex) >
               std::tie(other.priority, other.vertex);
    }
};

[[nodiscard]] bool valid_graph(const Graph &graph) noexcept {
    return !graph.offsets.empty() && graph.offsets.front() == 0U &&
           std::is_sorted(graph.offsets.begin(), graph.offsets.end()) &&
           graph.offsets.back() == graph.destinations.size() &&
           graph.destinations.size() == graph.costs.size();
}

[[nodiscard]] std::size_t vertex_count(const Graph &graph) noexcept {
    return graph.offsets.empty() ? 0U : graph.offsets.size() - 1U;
}

[[nodiscard]] std::optional<Path>
build_path(const std::vector<std::uint32_t> &previous,
           const std::vector<std::uint64_t> &distance,
           const std::uint32_t start, const std::uint32_t goal,
           const std::size_t expanded) {
    if (distance[goal] == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    Path path;
    path.cost = distance[goal];
    path.expanded = expanded;
    for (std::uint32_t vertex = goal;; vertex = previous[vertex]) {
        path.vertices.push_back(vertex);
        if (vertex == start) {
            break;
        }
    }
    std::reverse(path.vertices.begin(), path.vertices.end());
    return path;
}

[[nodiscard]] bool usable_rules(const TurnRules &rules) noexcept {
    return rules.hash != nullptr && rules.terminal != nullptr &&
           rules.player != nullptr && rules.moves != nullptr &&
           rules.apply != nullptr && rules.undo != nullptr &&
           rules.evaluate != nullptr;
}

void record_expanded(std::size_t *const output,
                     const std::size_t expanded) noexcept {
    if (output != nullptr) {
        *output = expanded;
    }
}

enum class Bound : std::uint8_t { exact, lower, upper };

struct TableEntry {
    std::uint64_t key = 0U;
    int score = 0;
    std::size_t depth = 0U;
    Bound bound = Bound::exact;
    bool used = false;
};

struct AlphaBeta {
    const TurnRules &rules;
    const SearchBudget &budget;
    std::vector<TableEntry> table;
    std::size_t nodes = 0U;
    int root_player = 0;

    [[nodiscard]] int evaluate(const std::size_t depth, int alpha, int beta) {
        int score = 0;
        if (rules.terminal(rules.context, score)) {
            return score;
        }
        if (depth == 0U || nodes >= budget.nodes) {
            return rules.evaluate(rules.context);
        }
        ++nodes;
        const std::uint64_t key = rules.hash(rules.context);
        TableEntry *entry = nullptr;
        if (!table.empty()) {
            entry = &table[key % table.size()];
            if (entry->used && entry->key == key && entry->depth >= depth) {
                if (entry->bound == Bound::exact) {
                    return entry->score;
                }
                if (entry->bound == Bound::lower) {
                    alpha = std::max(alpha, entry->score);
                } else {
                    beta = std::min(beta, entry->score);
                }
                if (alpha >= beta) {
                    return entry->score;
                }
            }
        }
        std::array<std::uint32_t, search_max_moves> moves{};
        const std::size_t count =
            rules.moves(rules.context, moves.data(), moves.size());
        if (count == 0U || count > moves.size()) {
            return rules.evaluate(rules.context);
        }
        const bool maximise = rules.player(rules.context) == root_player;
        const int original_alpha = alpha;
        const int original_beta = beta;
        int best = maximise ? score_floor : score_ceiling;
        for (std::size_t index = 0U; index < count; ++index) {
            const std::uint32_t move = moves[index];
            if (!rules.apply(rules.context, move)) {
                continue;
            }
            const int candidate = evaluate(depth - 1U, alpha, beta);
            rules.undo(rules.context, move);
            if ((maximise && candidate > best) ||
                (!maximise && candidate < best)) {
                best = candidate;
            }
            if (maximise) {
                alpha = std::max(alpha, best);
            } else {
                beta = std::min(beta, best);
            }
            if (alpha >= beta || nodes >= budget.nodes) {
                break;
            }
        }
        if (entry != nullptr) {
            entry->used = true;
            entry->key = key;
            entry->score = best;
            entry->depth = depth;
            entry->bound = best <= original_alpha  ? Bound::upper
                           : best >= original_beta ? Bound::lower
                                                   : Bound::exact;
        }
        return best;
    }
};

struct MctsNode {
    std::size_t parent = std::numeric_limits<std::size_t>::max();
    std::size_t first_child = std::numeric_limits<std::size_t>::max();
    std::size_t next_sibling = std::numeric_limits<std::size_t>::max();
    std::uint32_t move = 0U;
    std::size_t visits = 0U;
    double score = 0.0;
    int player = 0;
};

[[nodiscard]] std::size_t find_child(const std::vector<MctsNode> &nodes,
                                     const std::size_t parent,
                                     const std::uint32_t move) noexcept {
    for (std::size_t child = nodes[parent].first_child;
         child != std::numeric_limits<std::size_t>::max();
         child = nodes[child].next_sibling) {
        if (nodes[child].move == move) {
            return child;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

[[nodiscard]] double uct(const MctsNode &parent, const MctsNode &child,
                         const bool maximise) noexcept {
    if (child.visits == 0U) {
        return maximise ? std::numeric_limits<double>::infinity()
                        : -std::numeric_limits<double>::infinity();
    }
    const double mean = child.score / static_cast<double>(child.visits);
    const double explore =
        std::sqrt(2.0 * std::log(static_cast<double>(parent.visits + 1U)) /
                  static_cast<double>(child.visits));
    return maximise ? mean + explore : mean - explore;
}

struct TakeAway {
    int stones = 0;
    int side = 1;
};

[[nodiscard]] std::uint64_t take_hash(void *const context) noexcept {
    const auto &state = *static_cast<TakeAway *>(context);
    return (static_cast<std::uint64_t>(state.stones) << 1U) |
           static_cast<std::uint64_t>(state.side < 0);
}

[[nodiscard]] bool take_terminal(void *const context, int &score) noexcept {
    const auto &state = *static_cast<TakeAway *>(context);
    if (state.stones != 0) {
        return false;
    }
    score = state.side == 1 ? -100 : 100;
    return true;
}

[[nodiscard]] int take_player(void *const context) noexcept {
    return static_cast<TakeAway *>(context)->side;
}

[[nodiscard]] std::size_t take_moves(void *const context,
                                     std::uint32_t *const output,
                                     const std::size_t capacity) noexcept {
    const int stones = static_cast<TakeAway *>(context)->stones;
    const std::size_t count = static_cast<std::size_t>(std::min(stones, 2));
    if (count > capacity) {
        return count;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        output[index] = static_cast<std::uint32_t>(index + 1U);
    }
    return count;
}

[[nodiscard]] bool take_apply(void *const context,
                              const std::uint32_t move) noexcept {
    auto &state = *static_cast<TakeAway *>(context);
    if (move == 0U || move > static_cast<std::uint32_t>(state.stones)) {
        return false;
    }
    state.stones -= static_cast<int>(move);
    state.side = -state.side;
    return true;
}

void take_undo(void *const context, const std::uint32_t move) noexcept {
    auto &state = *static_cast<TakeAway *>(context);
    state.stones += static_cast<int>(move);
    state.side = -state.side;
}

[[nodiscard]] int take_evaluate(void *const) noexcept { return 0; }

[[nodiscard]] std::uint64_t count_heuristic(const std::uint32_t,
                                            const std::uint32_t,
                                            void *const context) noexcept {
    ++*static_cast<std::size_t *>(context);
    return 0U;
}

} // namespace

std::optional<Path> breadth_first_path(const Graph &graph,
                                       const std::uint32_t start,
                                       const std::uint32_t goal,
                                       const std::size_t max_expansions,
                                       std::size_t *const expanded_output) {
    record_expanded(expanded_output, 0U);
    if (!valid_graph(graph) || start >= vertex_count(graph) ||
        goal >= vertex_count(graph)) {
        return std::nullopt;
    }
    const std::size_t count = vertex_count(graph);
    const std::uint32_t unknown = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> previous(count, unknown);
    std::vector<std::uint64_t> distance(
        count, std::numeric_limits<std::uint64_t>::max());
    std::queue<std::uint32_t> queue;
    previous[start] = start;
    distance[start] = 0U;
    queue.push(start);
    std::size_t expanded = 0U;
    while (!queue.empty()) {
        if (expanded == max_expansions) {
            record_expanded(expanded_output, expanded);
            return std::nullopt;
        }
        const std::uint32_t current = queue.front();
        queue.pop();
        ++expanded;
        if (current == goal) {
            record_expanded(expanded_output, expanded);
            return build_path(previous, distance, start, goal, expanded);
        }
        for (std::size_t edge = graph.offsets[current];
             edge < graph.offsets[current + 1U]; ++edge) {
            const std::uint32_t next = graph.destinations[edge];
            if (next >= count || previous[next] != unknown) {
                continue;
            }
            previous[next] = current;
            distance[next] = distance[current] + 1U;
            queue.push(next);
        }
    }
    record_expanded(expanded_output, expanded);
    return std::nullopt;
}

std::optional<Path> dijkstra_path(const Graph &graph, const std::uint32_t start,
                                  const std::uint32_t goal,
                                  const std::size_t max_expansions,
                                  std::size_t *const expanded_output) {
    return a_star_path(graph, start, goal, nullptr, nullptr, max_expansions,
                       expanded_output);
}

std::optional<Path> a_star_path(const Graph &graph, const std::uint32_t start,
                                const std::uint32_t goal,
                                const Heuristic heuristic, void *const context,
                                const std::size_t max_expansions,
                                std::size_t *const expanded_output) {
    record_expanded(expanded_output, 0U);
    if (!valid_graph(graph) || start >= vertex_count(graph) ||
        goal >= vertex_count(graph)) {
        return std::nullopt;
    }
    const std::size_t count = vertex_count(graph);
    const std::uint32_t unknown = std::numeric_limits<std::uint32_t>::max();
    const std::uint64_t infinity = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint32_t> previous(count, unknown);
    std::vector<std::uint64_t> distance(count, infinity);
    std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<>> open;
    previous[start] = start;
    distance[start] = 0U;
    const std::uint64_t initial =
        heuristic == nullptr ? 0U : heuristic(start, goal, context);
    open.push(QueueNode{initial, 0U, start});
    std::size_t expanded = 0U;
    while (!open.empty()) {
        const QueueNode queued = open.top();
        open.pop();
        if (queued.distance != distance[queued.vertex]) {
            continue;
        }
        if (expanded == max_expansions) {
            record_expanded(expanded_output, expanded);
            return std::nullopt;
        }
        ++expanded;
        if (queued.vertex == goal) {
            record_expanded(expanded_output, expanded);
            return build_path(previous, distance, start, goal, expanded);
        }
        for (std::size_t edge = graph.offsets[queued.vertex];
             edge < graph.offsets[queued.vertex + 1U]; ++edge) {
            const std::uint32_t next = graph.destinations[edge];
            const std::uint32_t cost = graph.costs[edge];
            if (next >= count || distance[queued.vertex] > infinity - cost) {
                continue;
            }
            const std::uint64_t candidate = distance[queued.vertex] + cost;
            if (candidate >= distance[next]) {
                continue;
            }
            distance[next] = candidate;
            previous[next] = queued.vertex;
            const std::uint64_t estimate =
                heuristic == nullptr ? 0U : heuristic(next, goal, context);
            if (candidate > infinity - estimate) {
                return std::nullopt;
            }
            open.push(QueueNode{candidate + estimate, candidate, next});
        }
    }
    record_expanded(expanded_output, expanded);
    return std::nullopt;
}

std::optional<MoveChoice> alpha_beta_move(const TurnRules &rules,
                                          const SearchBudget &budget) {
    if (!usable_rules(rules) || budget.depth == 0U || budget.nodes == 0U) {
        return std::nullopt;
    }
    std::array<std::uint32_t, search_max_moves> moves{};
    const std::size_t count =
        rules.moves(rules.context, moves.data(), moves.size());
    if (count == 0U || count > moves.size()) {
        return std::nullopt;
    }
    AlphaBeta search{rules, budget,
                     std::vector<TableEntry>(budget.table_entries), 0U,
                     rules.player(rules.context)};
    MoveChoice choice{moves.front(), score_floor, 0U};
    for (std::size_t index = 0U; index < count && search.nodes < budget.nodes;
         ++index) {
        const std::uint32_t move = moves[index];
        if (!rules.apply(rules.context, move)) {
            continue;
        }
        const int score =
            search.evaluate(budget.depth - 1U, score_floor, score_ceiling);
        rules.undo(rules.context, move);
        if (score > choice.score) {
            choice = MoveChoice{move, score, search.nodes};
        }
    }
    choice.nodes = search.nodes;
    return choice;
}

std::optional<MoveChoice> mcts_move(const TurnRules &rules,
                                    const SearchBudget &budget) {
    if (!usable_rules(rules) || budget.nodes == 0U ||
        budget.nodes == std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    std::array<std::uint32_t, search_max_moves> root_moves{};
    const std::size_t root_count =
        rules.moves(rules.context, root_moves.data(), root_moves.size());
    if (root_count == 0U || root_count > root_moves.size()) {
        return std::nullopt;
    }
    std::vector<MctsNode> nodes;
    nodes.reserve(budget.nodes + 1U);
    const int root_player = rules.player(rules.context);
    nodes.push_back(MctsNode{std::numeric_limits<std::size_t>::max(),
                             std::numeric_limits<std::size_t>::max(),
                             std::numeric_limits<std::size_t>::max(), 0U, 0U,
                             0.0, root_player});
    std::mt19937_64 random{budget.seed};
    for (std::size_t iteration = 0U; iteration < budget.nodes; ++iteration) {
        std::array<std::uint32_t, search_max_moves * 2U> applied{};
        std::size_t applied_count = 0U;
        std::size_t current = 0U;
        int terminal_score = 0;
        bool terminal = rules.terminal(rules.context, terminal_score);
        while (!terminal) {
            std::array<std::uint32_t, search_max_moves> moves{};
            const std::size_t count =
                rules.moves(rules.context, moves.data(), moves.size());
            if (count == 0U || count > moves.size()) {
                terminal_score = rules.evaluate(rules.context);
                break;
            }
            std::size_t selected = std::numeric_limits<std::size_t>::max();
            for (std::size_t index = 0U; index < count; ++index) {
                if (find_child(nodes, current, moves[index]) ==
                    std::numeric_limits<std::size_t>::max()) {
                    selected = index;
                    break;
                }
            }
            if (selected != std::numeric_limits<std::size_t>::max()) {
                const std::uint32_t move = moves[selected];
                if (applied_count == applied.size() ||
                    !rules.apply(rules.context, move)) {
                    terminal_score = rules.evaluate(rules.context);
                    break;
                }
                applied[applied_count++] = move;
                if (nodes.size() < budget.nodes + 1U) {
                    const std::size_t child = nodes.size();
                    nodes.push_back(
                        MctsNode{current, nodes[current].first_child,
                                 std::numeric_limits<std::size_t>::max(), move,
                                 0U, 0.0, rules.player(rules.context)});
                    nodes[current].first_child = child;
                    current = child;
                }
                terminal = rules.terminal(rules.context, terminal_score);
                break;
            }
            const bool maximise = nodes[current].player == root_player;
            double best = maximise ? -std::numeric_limits<double>::infinity()
                                   : std::numeric_limits<double>::infinity();
            for (std::size_t child = nodes[current].first_child;
                 child != std::numeric_limits<std::size_t>::max();
                 child = nodes[child].next_sibling) {
                const double candidate =
                    uct(nodes[current], nodes[child], maximise);
                if ((maximise && candidate > best) ||
                    (!maximise && candidate < best)) {
                    best = candidate;
                    selected = child;
                }
            }
            if (selected == std::numeric_limits<std::size_t>::max() ||
                applied_count == applied.size() ||
                !rules.apply(rules.context, nodes[selected].move)) {
                terminal_score = rules.evaluate(rules.context);
                break;
            }
            applied[applied_count++] = nodes[selected].move;
            current = selected;
            terminal = rules.terminal(rules.context, terminal_score);
        }
        if (!terminal) {
            for (std::size_t depth = 0U;
                 depth < budget.depth && applied_count < applied.size();
                 ++depth) {
                std::array<std::uint32_t, search_max_moves> moves{};
                const std::size_t count =
                    rules.moves(rules.context, moves.data(), moves.size());
                if (count == 0U || count > moves.size()) {
                    break;
                }
                const std::size_t index = static_cast<std::size_t>(
                    random() % static_cast<std::uint64_t>(count));
                const std::uint32_t move = moves[index];
                if (!rules.apply(rules.context, move)) {
                    break;
                }
                applied[applied_count++] = move;
                if (rules.terminal(rules.context, terminal_score)) {
                    terminal = true;
                    break;
                }
            }
            if (!terminal) {
                terminal_score = rules.evaluate(rules.context);
            }
        }
        while (applied_count != 0U) {
            rules.undo(rules.context, applied[--applied_count]);
        }
        for (std::size_t node = current;
             node != std::numeric_limits<std::size_t>::max();
             node = nodes[node].parent) {
            ++nodes[node].visits;
            nodes[node].score += static_cast<double>(terminal_score);
        }
    }
    std::size_t best = nodes.front().first_child;
    if (best == std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    for (std::size_t child = nodes[best].next_sibling;
         child != std::numeric_limits<std::size_t>::max();
         child = nodes[child].next_sibling) {
        if (nodes[child].visits > nodes[best].visits) {
            best = child;
        }
    }
    const MctsNode &node = nodes[best];
    return MoveChoice{
        node.move,
        node.visits == 0U
            ? 0
            : static_cast<int>(node.score / static_cast<double>(node.visits)),
        nodes.size() - 1U};
}

int search_self_test() {
    const Graph graph{{0U, 2U, 3U, 4U, 4U}, {1U, 2U, 3U, 3U}, {1U, 4U, 2U, 1U}};
    const Graph malformed{{0U, 2U, 1U}, {1U}, {1U}};
    const std::optional<Path> breadth = breadth_first_path(graph, 0U, 3U);
    const std::optional<Path> dijkstra = dijkstra_path(graph, 0U, 3U);
    std::size_t heuristic_calls = 0U;
    const std::optional<Path> astar =
        a_star_path(graph, 0U, 3U, count_heuristic, &heuristic_calls);
    std::size_t capped_expanded = 0U;
    const std::optional<Path> capped =
        breadth_first_path(graph, 0U, 3U, 2U, &capped_expanded);
    if (!breadth || !dijkstra || !astar || heuristic_calls != 4U ||
        breadth_first_path(malformed, 0U, 1U) ||
        breadth->vertices != std::vector<std::uint32_t>{0U, 1U, 3U} ||
        dijkstra->cost != 3U || astar->vertices != dijkstra->vertices ||
        dijkstra->vertices != std::vector<std::uint32_t>{0U, 1U, 3U} ||
        breadth->expanded != 4U || capped || capped_expanded != 2U) {
        return 1;
    }
    TakeAway state{4, 1};
    const TurnRules rules{&state,     take_hash,  take_terminal, take_player,
                          take_moves, take_apply, take_undo,     take_evaluate};
    const SearchBudget budget{8U, 128U, 32U, 7U};
    const std::optional<MoveChoice> alpha = alpha_beta_move(rules, budget);
    const std::optional<MoveChoice> mcts = mcts_move(rules, budget);
    SearchBudget impossible = budget;
    impossible.nodes = std::numeric_limits<std::size_t>::max();
    if (!alpha || alpha->move != 1U || !mcts || state.stones != 4 ||
        state.side != 1 || mcts_move(rules, impossible)) {
        return 1;
    }
    return 0;
}

} // namespace m1
