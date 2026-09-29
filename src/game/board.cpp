#include "game/board.h"

#include <utility>

namespace bj2 {

namespace {
constexpr int kColors = 7;
constexpr int kHypercube = 9;
}  // namespace

int Board::ColumnX(int col) const {
    return (!flag_171b || flag_171d) ? col * kCellSize + 0x133 : col * kCellSize + 0x13e;
}

int Board::RowY(int row) const {
    if (flag_171b && !flag_171d) return row * kCellSize + 0x2b;
    return gravity_up ? 0x26c - row * kCellSize : row * kCellSize + 0x20;
}

void Board::NewGame(MTRand& global) {
    rand.SRand(global.Next());
    if (!flag_171b) Fill(global);
}

void Board::Fill(MTRand& global) {
    for (auto& g : grid_) g.reset();
    for (int row = kSize - 1; row >= 0; row--) {
        for (int col = 0; col < kSize; col++) {
            auto gem = std::make_unique<Gem>();
            gem->row = row;
            gem->col = col;
            gem->x = float(ColumnX(col));
            // Above the gem below (truncated, as the game's _ftol), or the
            // board's edge for the first row.
            int base = gravity_up ? 800 : 0;
            if (row < kSize - 1) base = int(At(col, row + 1)->y);
            int start = gravity_up ? int(global.Next() % 240) + 100 + base : base - int(global.Next() % 240) - 100;
            gem->y = float(start);
            Gem* placed = gem.get();
            grid_[size_t(row * kSize + col)] = std::move(gem);
            do {
                placed->color = int(rand.Next() % kColors);
            } while (HasMatch());
        }
    }
}

void Board::SwapCells(int c1, int r1, int c2, int r2) {
    auto& a = grid_[size_t(r1 * kSize + c1)];
    auto& b = grid_[size_t(r2 * kSize + c2)];
    std::swap(a, b);
    if (a) a->col = c1, a->row = r1;
    if (b) b->col = c2, b->row = r2;
}

std::vector<Gem*> Board::Refill(bool ensure_move) {
    std::vector<Gem*> made;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int col = 0; col < kSize; col++) {
            bool hole = false;
            int top = RowY(0);        // the highest gem's y in the column so far
            double last_vy = 0;       // the speed of the last gem moved
            // From the bottom up: past the lowest hole, each gem moves down a row.
            for (int row = kSize - 1; row >= 0; row--) {
                auto& here = grid_[size_t(row * kSize + col)];
                if (!here) {
                    hole = true;
                    continue;
                }
                if (gravity_up ? here->y > float(top) : here->y < float(top)) top = int(here->y);
                if (!hole) continue;
                changed = true;
                if (here->vy == 0) here->vy = gravity_up ? -1.0f : 1.0f;
                last_vy = here->vy;
                here->row++;
                grid_[size_t((row + 1) * kSize + col)] = std::move(here);
            }
            if (!hole) continue;
            if (flag_171b) continue;  // (puzzles: no new gems)
            changed = true;
            auto gem = std::make_unique<Gem>();
            gem->vy = float(gravity_up ? last_vy + 0.35 : last_vy - 0.55);
            gem->col = col;
            gem->row = 0;
            gem->x = float(ColumnX(col));
            float y = float(gravity_up ? top + 0x66 : top - 0x66);
            gem->y = gravity_up ? (y < 768.0f ? 768.0f : y) : (y > -84.0f ? -84.0f : y);
            made.push_back(gem.get());
            grid_[size_t(col)] = std::move(gem);
        }
    }
    for (int tries = 0; !made.empty(); tries++) {
        for (Gem* g : made) g->color = int(rand.Next() % 7);
        if (!ensure_move || tries >= 500 || FindMove(nullptr, 0, true, true)) break;
    }
    return made;
}

bool Board::HasMatch() const {
    for (int row = 0; row < kSize; row++) {
        int run = 0, last = -1;
        for (int col = 0; col < kSize; col++) {
            const Gem* g = At(col, row);
            if (!g) {
                last = -1;
            } else if (g->color < kColors && g->color == last) {
                if (++run == 3) return true;
            } else {
                run = 1;
                last = g->color;
            }
        }
    }
    for (int col = 0; col < kSize; col++) {
        int run = 0, last = -1;
        for (int row = 0; row < kSize; row++) {
            const Gem* g = At(col, row);
            if (!g) {
                last = -1;
            } else if (g->color < kColors && g->color == last) {
                if (++run == 3) return true;
            } else {
                run = 1;
                last = g->color;
            }
        }
    }
    return false;
}

bool Board::FindMove(Move* out, int nth, bool across, bool down) const {
    static const int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    // Swapped in a copy of the grid's pointers, as the game swaps in place.
    std::array<Gem*, kSize * kSize> cells;
    for (size_t i = 0; i < cells.size(); i++) cells[i] = grid_[i].get();
    auto cell = [&](int col, int row) -> Gem*& { return cells[size_t(row * kSize + col)]; };
    int found = 0;
    for (int row = 0; row < kSize; row++) {
        for (int col = 0; col < kSize; col++) {
            Gem* here = cell(col, row);
            for (const auto& d : kDirections) {
                int tc = col + d[0], tr = row + d[1];
                if (unsigned(tc) >= kSize || unsigned(tr) >= kSize) continue;
                bool makes_line = here && here->color == kHypercube && cell(tc, tr);
                Gem* there = cell(tc, tr);
                if (there && there->color < kColors) {
                    std::swap(cell(col, row), cell(tc, tr));
                    int color = cell(col, row)->color;
                    auto same = [&](int c, int r) { return cell(c, r) && cell(c, r)->color == color; };
                    int left = col, right = col, top = row, bottom = row;
                    while (left > 0 && same(left - 1, row)) left--;
                    while (right < kSize - 1 && same(right + 1, row)) right++;
                    while (top > 0 && same(col, top - 1)) top--;
                    while (bottom < kSize - 1 && same(col, bottom + 1)) bottom++;
                    std::swap(cell(col, row), cell(tc, tr));
                    if ((right - left > 1 && across) || (bottom - top > 1 && down)) makes_line = true;
                }
                if (!makes_line) continue;
                if (found == nth) {
                    if (out) *out = {col, row, tc, tr};
                    return true;
                }
                found++;
            }
        }
    }
    return false;
}

}  // namespace bj2
