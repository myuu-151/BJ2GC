// bj2check: the game's logic on the PC, without drawing, to compare with
// the original.
//
//   bj2check board SEED    the board a new game's generator seeded with SEED fills
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/board.h"
#include "game/game.h"

namespace {

const char kColorLetters[] = "WRGBPOYrbh";  // white red green blue purple orange yellow; rock bomb hypercube

void PrintBoard(const bj2::Board& board) {
    for (int row = 0; row < bj2::Board::kSize; row++) {
        for (int col = 0; col < bj2::Board::kSize; col++) {
            const bj2::Gem* g = board.At(col, row);
            std::printf(" %c", g ? kColorLetters[g->color] : '.');
        }
        std::printf("\n");
    }
    bj2::Move m;
    int moves = 0;
    while (board.FindMove(moves == 0 ? &m : nullptr, moves, true, true)) moves++;
    if (moves) {
        board.FindMove(&m, 0, true, true);
        std::printf("%d moves; the first: (%d,%d) to (%d,%d)\n", moves, m.col, m.row, m.target_col, m.target_row);
    } else {
        std::printf("no moves\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "board") == 0) {
        bj2::MTRand global;
        bj2::Board board;
        board.rand.SRand(uint32_t(std::strtoul(argv[2], nullptr, 0)));
        board.Fill(global);
        PrintBoard(board);
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "fill-check") == 0) {
        // A board saved from the original just after it was filled: its
        // generator's words, from the start of the
        // block, replayed through our fill, must give the same colours and
        // use as many numbers as the original had.
        FILE* f = std::fopen(argv[2], "r");
        if (!f) return 2;
        int index = 0, colors[64];
        static uint32_t words[bj2::MTRand::kN];
        bool ok = std::fscanf(f, "%d", &index) == 1;
        for (auto& w : words) ok = ok && std::fscanf(f, "%u", &w) == 1;
        for (int& c : colors) ok = ok && std::fscanf(f, "%d", &c) == 1;
        std::fclose(f);
        if (!ok) return 2;
        bj2::MTRand global;
        bj2::Board board;
        board.rand.SetState(words, 0);
        board.Fill(global);
        int differ = 0;
        for (int i = 0; i < 64; i++)
            if (board.At(i % 8, i / 8)->color != colors[i]) differ++;
        PrintBoard(board);
        std::printf("colours: %d of 64 differ; numbers used: ours %d, the original's %d\n", differ,
                    board.rand.Index(), index);
        return differ == 0 && board.rand.Index() == index ? 0 : 1;
    }
    if (argc >= 3 && std::strcmp(argv[1], "play") == 0) {
        // A Classic game played by the hint, to the end or MOVES moves.
        bj2::Game game;
        game.global.SRand(uint32_t(std::strtoul(argv[2], nullptr, 0)));
        game.NewGame();
        int limit = argc >= 4 ? std::atoi(argv[3]) : 1000, moves = 0;
        long updates = 0;
        while (moves < limit && game.GetState() != bj2::Game::State::GameOver && updates < 2000000) {
            game.Update();
            updates++;
            bj2::Move m;
            if (game.GetState() == bj2::Game::State::Idle && game.Hint(&m) &&
                game.TrySwap(m.col, m.row, m.target_col - m.col, m.target_row - m.row))
                moves++;
        }
        PrintBoard(game.GetBoard());
        std::printf("%d moves, %ld updates (%.0f s): score %d, level %d%s\n", moves, updates, updates / 100.0,
                    game.Score(), game.Level(),
                    game.GetState() == bj2::Game::State::GameOver ? ", no moves left" : "");
        return 0;
    }
    std::fprintf(stderr, "bj2check board SEED | fill-check SNAPSHOT.txt | play SEED [MOVES]\n");
    return 1;
}
