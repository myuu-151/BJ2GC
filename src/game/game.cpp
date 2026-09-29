#include "game/game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bj2 {

namespace {

constexpr int kClearTime = 18;     // 0x5aab8a: a dying gem's 6 steps of 3 updates
constexpr int kZapWait = 35;       // 0x5a888f: the used hypercube waits, then fades
constexpr double kPi = 3.14159265358979;
constexpr double kGravity = 0.24;  // 0x62a6e8
constexpr int kColors = 7;
constexpr int kHypercube = 9;

// The cascade bonus by the lines made so far this move (0x5a7ec1).
int CascadeBonus(int cascade) {
    static const int bonus[] = {0, 0, 10, 20, 30, 50, 70, 100, 150};
    return cascade < 9 ? bonus[cascade] : 200;
}

}  // namespace

Game::Game() {
    global.SRand(4357);
    fx_rand_.SRand(5489);
}

void Game::NewGame() {
    score_ = 0;
    level_ = 0;
    level_points_ = 0;
    cascade_ = 0;
    clearing_.clear();
    blown_up_.clear();
    message_.clear();
    board_.NewGame(global);
    state_ = State::Falling;
    sounds_.push_back({Sound::Go});
}

double Game::Multiplier() const { return tuning.norm_pm_base + tuning.norm_pm_mult * level_; }

float Game::LevelProgress() const {
    double need = tuning.norm_bonus + tuning.norm_bonus_mult * level_;
    return float(std::min(1.0, level_points_ / need));
}

int Game::ClearStep(const Gem* g) const {
    if (state_ != State::Clearing || std::find(clearing_.begin(), clearing_.end(), g) == clearing_.end()) return -1;
    return std::min(5, timer_ / 3);
}

bool Game::IsBlownUp(const Gem* g) const {
    return state_ == State::Clearing && std::find(blown_up_.begin(), blown_up_.end(), g) != blown_up_.end();
}

float Game::ZapCubeTime() const {
    return state_ == State::Zapping ? float(std::min(1.0, std::max(0, timer_ - kZapWait) * 0.0175)) : 0.0f;
}

float Game::Charge(const Gem* g) const {
    if (state_ != State::Zapping) return 0;
    for (size_t i = 0; i < zap_charged_.size(); i++)
        if (zap_charged_[i] == g) return zap_charge_[i];
    return 0;
}

void Game::AddPoints(int raw, float x, float y, int color) {
    int points = int(raw * Multiplier());
    score_ += points;
    level_points_ += raw;
    move_points_ += points;
    // Shown at the gems' middle, a little low (0x5a6236: +42, +34).
    FxEvent e{Fx::Points, x + 42, y + 34};
    e.color = color;
    e.value = points;
    e.count = raw;  // the popup's size goes by the raw points
    fx_.push_back(e);
}

void Game::Praise() {
    // The move's points against 60, 125 and 275 times the multiplier; each
    // said once a move, the next only when it's higher.
    double mult = Multiplier();
    int level = move_points_ >= 275 * mult ? 3 : move_points_ >= 125 * mult ? 2 : move_points_ >= 60 * mult ? 1 : 0;
    if (level <= praised_) return;
    praised_ = level;
    static const Sound kSay[] = {Sound::Good, Sound::Excellent, Sound::Incredible};
    sounds_.push_back({kSay[level - 1]});
    if (level >= 2) {
        FxEvent e{Fx::Praise};
        e.value = level - 1;
        fx_.push_back(e);
    }
}

bool Game::TrySwap(int col, int row, int dx, int dy) {
    if (state_ != State::Idle) return false;
    if ((dx != 0) == (dy != 0) || std::abs(dx) + std::abs(dy) != 1) return false;
    int tc = col + dx, tr = row + dy;
    if (unsigned(col) >= Board::kSize || unsigned(row) >= Board::kSize) return false;
    if (unsigned(tc) >= Board::kSize || unsigned(tr) >= Board::kSize) return false;
    swap_a_ = board_.At(col, row);
    swap_b_ = board_.At(tc, tr);
    if (!swap_a_ || !swap_b_) return false;
    state_ = State::Swapping;
    timer_ = 0;
    swap_step_ = swap_a_->color == kHypercube || swap_b_->color == kHypercube ? 4 : 5;
    move_points_ = 0;
    praised_ = 0;
    return true;
}

void Game::Update() {
    message_age_++;

    switch (state_) {
    case State::Idle:
    case State::GameOver:
        break;
    case State::Swapping:
    case State::SwappingBack: {
        // 0x5a888f: each gem swings through the middle, its offset from it
        // times cos of an angle going 0 to 180 degrees, 5 a update.
        timer_ += swap_step_;
        float k = float(std::cos(kPi * std::min(timer_, 180) / 180));
        float ax = float(board_.ColumnX(swap_a_->col)), ay = float(board_.RowY(swap_a_->row));
        float bx = float(board_.ColumnX(swap_b_->col)), by = float(board_.RowY(swap_b_->row));
        float mx = (ax + bx) / 2, my = (ay + by) / 2;
        swap_a_->x = float(int(mx + (ax - mx) * k));
        swap_a_->y = float(int(my + (ay - my) * k));
        swap_b_->x = float(int(mx + (bx - mx) * k));
        swap_b_->y = float(int(my + (by - my) * k));
        if (timer_ < 180) break;
        board_.SwapCells(swap_a_->col, swap_a_->row, swap_b_->col, swap_b_->row);
        for (Gem* g : {swap_a_, swap_b_}) {
            g->x = float(board_.ColumnX(g->col));
            g->y = float(board_.RowY(g->row));
        }
        if (state_ == State::SwappingBack) {
            state_ = State::Idle;
            timer_ = 0;
            break;
        }
        cascade_ = 0;
        batches_ = 0;
        hyper_color_ = -1;
        if (swap_a_->color == kHypercube && swap_b_->color < kColors) hyper_color_ = swap_b_->color;
        if (swap_b_->color == kHypercube && swap_a_->color < kColors) hyper_color_ = swap_a_->color;
        if (hyper_color_ >= 0) {
            StartZapping();
        } else if (board_.HasMatch()) {
            StartClearing();
        } else {
            state_ = State::SwappingBack;  // no line: back again
            timer_ = 0;
            sounds_.push_back({Sound::Bad});
        }
        break;
    }
    case State::Clearing:
        if (++timer_ >= kClearTime) FinishClearing();
        break;
    case State::Zapping:
        UpdateZapping();
        break;
    case State::Falling:
        if (!UpdateFall()) Settled();
        break;
    case State::LevelUp:
        if (++timer_ >= kWarpTime) {
            // The next level (FUN_005aac20): its board stacked above, to fall
            // in once it has flown in.
            level_++;
            level_points_ = 0;
            board_.Fill(global);
            sounds_.push_back({Sound::GetReady});
            state_ = State::FlyIn;
            timer_ = 0;
        }
        break;
    case State::FlyIn:
        if (++timer_ >= kFlyInTime) {
            state_ = State::Ready;
            timer_ = 0;
            message_ = "LEVEL " + std::to_string(level_ + 1);
            message_age_ = 0;
        }
        break;
    case State::Ready:
        if (++timer_ >= kReadyTime) {
            state_ = State::Falling;
            timer_ = 0;
        }
        break;
    }
}

void Game::StartClearing() {
    clearing_.clear();
    blown_up_.clear();
    if (MarkLines()) {
        state_ = State::Clearing;
        timer_ = 0;
    } else {
        state_ = State::Idle;
    }
}

void Game::StartZapping() {
    // The hypercube's colour, every gem of it, row by row; its partner
    // electrified at once, the rest by bolts (0x5a85bc, 0x5a8c66).
    zap_cube_ = swap_a_->color == kHypercube ? swap_a_ : swap_b_;
    Gem* partner = zap_cube_ == swap_a_ ? swap_b_ : swap_a_;
    zap_targets_.clear();
    zap_charged_.clear();
    zap_charge_.clear();
    zap_gone_.clear();
    for (int row = 0; row < Board::kSize; row++)
        for (int col = 0; col < Board::kSize; col++) {
            Gem* g = board_.At(col, row);
            if (g && g != partner && g->color == hyper_color_) zap_targets_.push_back(g);
        }
    zap_charged_.push_back(partner);
    zap_charge_.push_back(0);
    FxEvent glow{Fx::PowerGlow, partner->x, partner->y};
    glow.color = hyper_color_;
    fx_.push_back(glow);
    zap_bolt_ = 0;
    sounds_.push_back({Sound::Zap});
    state_ = State::Zapping;
    timer_ = 0;
}

void Game::UpdateZapping() {
    timer_++;
    // Each electrified gem charges for 67 updates, then goes.
    for (size_t i = 0; i < zap_charged_.size();) {
        zap_charge_[i] += 0.015f;
        if (zap_charge_[i] < 1) {
            i++;
            continue;
        }
        Gem* g = zap_charged_[i];
        AddPoints(g->power ? tuning.elect_power_bonus : tuning.elect_normal_bonus, g->x, g->y, hyper_color_);
        FxEvent e{Fx::Zapped, g->x, g->y};
        e.color = hyper_color_;
        fx_.push_back(e);
        sounds_.push_back({Sound::ZapGem});
        zap_gone_.push_back(g);
        zap_charged_.erase(zap_charged_.begin() + long(i));
        zap_charge_.erase(zap_charge_.begin() + long(i));
    }
    // A bolt to the next gem: when there is none, or by chance, the more
    // often the more are charging.
    if (zap_bolt_ > 0) zap_bolt_--;
    if (timer_ >= 15 && !zap_targets_.empty() &&
        (zap_bolt_ == 0 || fx_rand_.Next() % (20 / (zap_charged_.size() + 1) + 5) == 0)) {
        const Gem* from = zap_charged_.empty() ? zap_cube_ : zap_charged_[fx_rand_.Next() % zap_charged_.size()];
        Gem* to = zap_targets_.back();
        zap_targets_.pop_back();
        zap_charged_.push_back(to);
        zap_charge_.push_back(0);
        FxEvent e{Fx::Bolt, from->x, from->y};
        e.x2 = to->x;
        e.y2 = to->y;
        e.color = hyper_color_;
        fx_.push_back(e);
        sounds_.push_back({Sound::Bolt});
        zap_bolt_ = 83;
    }
    if (!zap_targets_.empty() || !zap_charged_.empty() || timer_ < kZapWait + 58) return;
    // All gone, and the cube: the gaps fill.
    Praise();
    clearing_ = zap_gone_;
    clearing_.push_back(zap_cube_);
    zap_cube_ = nullptr;
    hyper_color_ = -1;
    FinishClearing();
}

bool Game::MarkLines() {
    std::vector<bool> marked(Board::kSize * Board::kSize, false);
    std::vector<Gem*> made_special;
    bool any = false, made_power = false, made_hyper = false;
    int lines = 0;
    // Across, then down: runs of three or more of a colour.
    for (int pass = 0; pass < 2; pass++) {
        for (int line = 0; line < Board::kSize; line++) {
            int start = 0;
            while (start < Board::kSize) {
                auto cell = [&](int i) { return pass == 0 ? board_.At(i, line) : board_.At(line, i); };
                Gem* first = cell(start);
                int end = start + 1;
                if (first && first->color < kColors)
                    while (end < Board::kSize && cell(end) && cell(end)->color == first->color) end++;
                int length = end - start;
                if (!first || first->color >= kColors || length < 3) {
                    start = end;
                    continue;
                }
                any = true;
                lines++;
                cascade_++;
                int raw = 10 + CascadeBonus(cascade_);
                if (length > 3) raw += (length - 3) * 10;
                if (length > 5) raw += (length - 5) * 10;
                float x = 0, y = 0;
                // The gem that becomes special: the one the player moved if
                // it is in the line, else the middle one.
                Gem* keep = cell(start + length / 2);
                for (int i = start; i < end; i++) {
                    Gem* g = cell(i);
                    if (g == swap_a_ || g == swap_b_) keep = g;
                    if (g->power) raw += tuning.power_bonus;
                    x += g->x;
                    y += g->y;
                    marked[size_t(g->row * Board::kSize + g->col)] = true;
                }
                AddPoints(raw, x / float(length), y / float(length), first->color);
                if (length >= tuning.min_wild_combo) {
                    keep->hyper_from = keep->color;
                    keep->color = kHypercube;
                    keep->power = false;
                    made_special.push_back(keep);
                    made_hyper = true;
                } else if (length >= tuning.min_power_combo && !keep->power) {
                    keep->power = true;
                    made_special.push_back(keep);
                    made_power = true;
                }
                start = end;
            }
        }
    }
    if (!any) return false;
    for (Gem* g : made_special) marked[size_t(g->row * Board::kSize + g->col)] = false;
    // Power gems cleared explode: the eight around them go too.
    std::vector<bool> in_line = marked;
    int explosions = 0;
    const size_t first_fx = fx_.size();
    for (int row = 0; row < Board::kSize; row++)
        for (int col = 0; col < Board::kSize; col++) {
            Gem* g = board_.At(col, row);
            if (g && g->power && marked[size_t(row * Board::kSize + col)]) Explode(col, row, marked, explosions);
        }
    int blown = 0;
    for (int i = 0; i < Board::kSize * Board::kSize; i++) {
        if (!marked[size_t(i)]) continue;
        Gem* g = board_.At(i % Board::kSize, i / Board::kSize);
        clearing_.push_back(g);
        if (in_line[size_t(i)]) continue;
        // Taken by an explosion: shattered, not dying in steps.
        blown_up_.push_back(g);
        FxEvent e{Fx::Shatter, g->x, g->y};
        e.color = g->color;
        fx_.push_back(e);
        blown++;
    }
    if (explosions) {
        for (size_t i = first_fx; i < fx_.size(); i++)
            if (fx_[i].fx == Fx::Explosion) fx_[i].count = explosions;
        FxEvent shake{Fx::Shake};
        shake.count = blown + explosions;
        fx_.push_back(shake);
    }
    batches_++;
    // 0x5a7ec1: multishot when a power gem is made, else gotset (gotsetbig
    // for two lines or more); hypergem_creation too for a hypercube; and a
    // combo note, higher each time this move (combo22 to combo72).
    sounds_.push_back({made_power ? Sound::PowerMade : lines > 1 ? Sound::MatchBig : Sound::Match});
    if (made_hyper) sounds_.push_back({Sound::HyperMade});
    sounds_.push_back({Sound::Combo, std::min(batches_ + 1, 7)});
    Praise();
    return true;
}

void Game::Explode(int col, int row, std::vector<bool>& marked, int& explosions) {
    explosions++;
    Gem* centre = board_.At(col, row);
    AddPoints(tuning.explosion_chain_bonus + tuning.explosion_chain_bonus_add * (explosions - 1), centre->x,
              centre->y, centre->color);
    fx_.push_back(FxEvent{Fx::Explosion, centre->x, centre->y, centre->color});
    centre->power = false;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int c = col + dx, r = row + dy;
            if (unsigned(c) >= Board::kSize || unsigned(r) >= Board::kSize) continue;
            Gem* g = board_.At(c, r);
            if (!g) continue;
            bool was = marked[size_t(r * Board::kSize + c)];
            marked[size_t(r * Board::kSize + c)] = true;
            if (!was && g->power) Explode(c, r, marked, explosions);
        }
}

void Game::FinishClearing() {
    for (Gem* g : clearing_) board_.Remove(g->col, g->row);
    clearing_.clear();
    blown_up_.clear();
    swap_a_ = swap_b_ = nullptr;
    board_.Refill(false);  // Classic: no second draw for a move; no moves ends it
    state_ = State::Falling;
}

bool Game::UpdateFall() {
    // 0x5a9ced: each gem not on its row moves by its speed, and the speed
    // grows by the gravity; one that reaches its row stops there.
    bool moving = false, landed = false;
    last_land_++;
    for (int row = 0; row < Board::kSize; row++)
        for (int col = 0; col < Board::kSize; col++) {
            Gem* g = const_cast<Gem*>(board_.At(col, row));
            if (!g) continue;
            float target = float(board_.RowY(row));
            if (g->y == target) {
                g->vy = 0;
                continue;
            }
            moving = true;
            g->y += g->vy;
            if (board_.gravity_up ? g->y <= target : g->y >= target) {
                g->y = target;
                landed = true;
            }
            g->vy = float(board_.gravity_up ? g->vy - kGravity : g->vy + kGravity);
        }
    if (landed && last_land_ >= 10) {  // gemongem2 at most every 10 updates
        sounds_.push_back({Sound::Land});
        last_land_ = 0;
    }
    return moving;
}

void Game::CompleteLevel() {
    if (state_ != State::Idle) return;
    level_points_ = tuning.norm_bonus + tuning.norm_bonus_mult * level_;
    Settled();
}

void Game::CycleGem(int col, int row) {
    if (state_ != State::Idle) return;
    Gem* g = board_.At(col, row);
    if (!g) return;
    if (g->color == kHypercube) {
        g->color = g->hyper_from >= 0 ? g->hyper_from : 0;
    } else if (g->power) {
        g->power = false;
        g->hyper_from = g->color;
        g->color = kHypercube;
    } else {
        g->power = true;
    }
}

void Game::EndNoMoves() {
    if (state_ != State::Idle) return;
    GameOver();
}

void Game::GameOver() {
    state_ = State::GameOver;
    message_ = "NO MOVES!";
    message_age_ = 0;
    sounds_.push_back({Sound::NoMoves});
}

void Game::Settled() {
    if (MarkLines()) {  // a cascade
        state_ = State::Clearing;
        timer_ = 0;
        return;
    }
    cascade_ = 0;
    if (level_points_ >= tuning.norm_bonus + tuning.norm_bonus_mult * level_) {
        // No words: the voice says it (Level_Complete); the warp follows.
        state_ = State::LevelUp;
        timer_ = 0;
        message_.clear();
        sounds_.push_back({Sound::LevelUp});
        sounds_.push_back({Sound::Whirlpool});  // 0x5a2d15, as the whirlpool starts
        return;
    }
    if (!board_.FindMove(nullptr, 0, true, true)) {
        GameOver();
        return;
    }
    state_ = State::Idle;
}

}  // namespace bj2
