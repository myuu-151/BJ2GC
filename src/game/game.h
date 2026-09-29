// A game of Bejeweled 2's Classic mode: the board, a move at a time, run in
// the original's updates (100 a second). Platform-free: a front end feeds it
// the player's actions and draws what it holds.
//
// From the original: the fill and refill order and their
// random numbers, the line check, the move finder, the fall (gravity 0.24 a
// update), the points of a line (10, the cascade bonus, 10 a gem past 3 and
// past 5, 25 a power gem, times NormPMBase + NormPMMult x level), the level
// (NormBonus + NormBonusMult x level points, before the multiplier), power
// gems at 4 in a line and hypercubes at 5. How long the swap and the
// clearing take, and the explosions' and electrocutions' details, are ours
// until checked against the original.
#pragma once

#include <string>
#include <vector>

#include "game/board.h"

namespace bj2 {

// config.xml's values the Classic mode uses.
struct Tuning {
    double norm_bonus = 500, norm_bonus_mult = 150;
    double norm_pm_base = 1.0, norm_pm_mult = 0.5;
    int min_power_combo = 4, min_wild_combo = 5;
    int power_bonus = 25;
    int explosion_chain_bonus = 40, explosion_chain_bonus_add = 10;
    int elect_normal_bonus = 20, elect_power_bonus = 60;
};

// What the game would play, for a front end to play (the original's
// effects, by what happens).
enum class Sound { Go, Bad, Match, MatchBig, Combo, Land, Explode, PowerMade, HyperMade, Zap, LevelUp, NoMoves,
                   Excellent, Incredible, GetReady, Good, ZapGem, Bolt, Whirlpool };
struct SoundEvent {
    Sound sound;
    int level = 0;  // Combo: the batch of lines this move, 2 to 7
};

// What the front end would show (docs/board-effects.md), as the sounds.
// Positions are a gem's top left in the board's units.
enum class Fx {
    Points,     // `value` points scored at x, y (the popup's centre), in `color`
    Explosion,  // a power gem going off at x, y; `count` centres at once
    Shatter,    // a gem blown up at x, y: its shards, in `color`
    Shake,      // the board shaking for `count` gems blown up
    Bolt,       // lightning from x, y to x2, y2, in `color`
    Zapped,     // a gem electrified to nothing at x, y
    PowerGlow,  // a hypercube's partner lit, at x, y
    Praise,     // 1 EXCELLENT, 2 INCREDIBLE (`value`)
};
struct FxEvent {
    Fx fx;
    float x = 0, y = 0;
    int color = 0;
    int value = 0;
    int count = 0;
    float x2 = 0, y2 = 0;
};

class Game {
public:
    // A level's end, as the original's board states 8-10 and 0xd: LevelUp,
    // the warp (the whirlpool, then hyperspace), kWarpTime updates; FlyIn,
    // the next level's board flying in; Ready, a moment before it falls.
    // Zapping: a hypercube used, its colour electrified a gem at a time.
    enum class State { Idle, Swapping, SwappingBack, Clearing, Falling, LevelUp, GameOver, FlyIn, Ready, Zapping };

    static constexpr int kWarpTime = 623;  // state 10 to FUN_005aac20
    static constexpr int kFlyInTime = 100;
    static constexpr int kReadyTime = 25;  // Board+0x1730

    static constexpr int kUpdatesPerSecond = 100;

    Game();

    // A new game, its board seeded from the global generator as the
    // original's is.
    void NewGame();
    void Update();  // one update (1/100 s)

    // The player: swap the gem at (col, row) with its neighbour in
    // direction (dx, dy). False if not now, or not a neighbour.
    bool TrySwap(int col, int row, int dx, int dy);

    const Board& GetBoard() const { return board_; }
    State GetState() const { return state_; }
    int Score() const { return score_; }
    int Level() const { return level_ + 1; }  // shown from 1
    // The bar to the next level, 0 to 1.
    float LevelProgress() const;
    int Cascade() const { return cascade_; }
    // A gem being cleared: its step of dying, 0 to 5 (0x5aab8a: a step
    // every 3 updates), or -1 if it isn't; blown up ones aren't drawn.
    int ClearStep(const Gem* g) const;
    bool IsBlownUp(const Gem* g) const;
    // Zapping: the hypercube (fading and shrinking as its t goes 0 to 1),
    // and a gem's charge (0 none, to 1, when it goes).
    const Gem* ZapCube() const { return state_ == State::Zapping ? zap_cube_ : nullptr; }
    float ZapCubeTime() const;
    float Charge(const Gem* g) const;
    // A message ("LEVEL 2", "NO MOVES!") and how long it has shown.
    const std::string& Message() const { return message_; }
    int MessageAge() const { return message_age_; }
    // Updates in the current state (the warp's clock).
    int StateTime() const { return timer_; }
    // For testing: the level's points, as if scored (only while the board is still).
    void CompleteLevel();
    // For testing: the board out of moves, the game over (only while the board is still).
    void EndNoMoves();
    // For testing: the gem at col, row a power gem, then a hypercube, then itself again.
    void CycleGem(int col, int row);
    // The sounds since last asked.
    std::vector<SoundEvent> TakeSounds() {
        std::vector<SoundEvent> out;
        out.swap(sounds_);
        return out;
    }
    std::vector<FxEvent> TakeFx() {
        std::vector<FxEvent> out;
        out.swap(fx_);
        return out;
    }
    // A move the player could make (the hint): false if none.
    bool Hint(Move* out) const { return board_.FindMove(out, 0, true, true); }

    Tuning tuning;
    MTRand global;  // Sexy::Rand

private:
    void StartClearing();
    bool MarkLines();  // the lines on the board, marked and scored
    void Explode(int col, int row, std::vector<bool>& marked, int& explosions);
    void FinishClearing();
    bool UpdateFall();
    void Settled();
    void GameOver();  // no moves left
    double Multiplier() const;
    void AddPoints(int raw, float x, float y, int color);
    void StartZapping();
    void UpdateZapping();
    void Praise();  // the move's points: Good, EXCELLENT, INCREDIBLE

    Board board_;
    State state_ = State::Idle;
    int timer_ = 0;
    int score_ = 0;
    int level_ = 0;           // Board+0x15e4, from 0
    double level_points_ = 0; // raw points towards the next level
    int cascade_ = 0;         // Board+0x15e4c: lines made by this move so far
    int batches_ = 0;         // times lines were cleared this move
    int last_land_ = 0;       // updates since a landing was heard
    std::vector<SoundEvent> sounds_;
    Gem* swap_a_ = nullptr;
    Gem* swap_b_ = nullptr;
    int hyper_color_ = -1;    // a hypercube's swap: the colour it takes
    std::vector<Gem*> clearing_;
    std::vector<Gem*> blown_up_;  // of clearing_, those an explosion took
    std::vector<FxEvent> fx_;
    int swap_step_ = 5;           // 0x5a888f: the swap's angle a update (4 with a hypercube)
    double move_points_ = 0;      // Board+0x15e94: points this move
    int praised_ = 0;             // the praise said this move
    // Zapping (0x5a85bc, 0x5a8c66).
    Gem* zap_cube_ = nullptr;
    std::vector<Gem*> zap_targets_;     // not yet electrified
    std::vector<Gem*> zap_charged_;     // electrified, charging
    std::vector<float> zap_charge_;
    std::vector<Gem*> zap_gone_;        // charged to nothing
    int zap_bolt_ = 0;                  // updates the last bolt has left
    MTRand fx_rand_;                    // the effects' own numbers
    std::string message_;
    int message_age_ = 0;
};

}  // namespace bj2
