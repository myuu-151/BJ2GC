// Sexy::Board: the 8x8 grid and what it does with gems.
// Coordinates are the game's own 1024x768 ones; it draws them scaled to the
// screen (0.625 at 640x480).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "game/mtrand.h"

namespace bj2 {

// A gem (0xe0 bytes in the game, made by 0x5947a7).
struct Gem {
    int color = 0;   // +0x00: 0-6 the seven colours; 7 a rock, 8 a bomb, 9 a hypercube
    float x = 0;     // +0x04
    float y = 0;     // +0x08
    int col = 0;     // +0x10
    int row = 0;     // +0x14
    float vy = 0;    // +0x1c: falling speed, pixels an update
    bool power = false;  // +0x70: a power gem (explodes when cleared)
    int hyper_from = -1; // +0x64: a hypercube's colour before it became one
};

struct Move {
    int col, row;               // the gem moved
    int target_col, target_row; // where it goes
};

class Board {
public:
    static constexpr int kSize = 8;
    static constexpr int kCellSize = 84;  // pixels between gems (0x54)

    // Gems fall upwards (Board+0x1721; Twilight's other half).
    bool gravity_up = false;
    // Board+0x171b and +0x171d: a mode that places the board 11 pixels over
    // (the puzzles' layout, not yet told apart).
    bool flag_171b = false, flag_171d = false;

    MTRand rand;  // Board+0xc20: the gems' colours

    Gem* At(int col, int row) const { return grid_[size_t(row * kSize + col)].get(); }

    // A new game's board (0x5ad581): its generator seeded with one number of
    // the global one, then filled.
    void NewGame(MTRand& global);

    // 0x597efd: a whole new board, bottom row first, each row from the left.
    // A gem's colour is the board's Next() % 7, drawn again while the board
    // has three in a line; its start above the screen (to fall in) is
    // 100-339 pixels above the gem below it, from the global generator.
    void Fill(MTRand& global);

    // 0x595346: three of a colour (0-6) in a row or a column.
    bool HasMatch() const;

    // 0x59543c: the n-th move (from 0) that would make a line, if any:
    // every cell, top to bottom and left to right, swapped with its
    // neighbour right, left, below and above. Lines counted across if
    // `across`, down if `down`; a hypercube swapped with any gem counts.
    bool FindMove(Move* out, int nth, bool across, bool down) const;

    int ColumnX(int col) const;  // 0x594ba4
    int RowY(int row) const;     // 0x594bd5

    // The gems of two cells swapped (their col and row too).
    void SwapCells(int c1, int r1, int c2, int r2);
    // A gem taken off the board.
    void Remove(int col, int row) { grid_[size_t(row * kSize + col)].reset(); }

    // 0x5a71f0: the gems fall into the holes and new ones fill them from
    // the top. Pass after pass until no hole is left, each moves a
    // column's gems above its lowest hole down a row and adds one gem at
    // the top, above the highest (at most -84); after the passes the new
    // gems get their colours in the order they were made, Next() % 7 each,
    // all drawn again (up to 500 times) until the board has a move if
    // `ensure_move`. The new gems, in that order.
    std::vector<Gem*> Refill(bool ensure_move);

private:
    std::array<std::unique_ptr<Gem>, kSize * kSize> grid_;  // Board+0x15ec, row by row
};

}  // namespace bj2
