/* Chess-GUI-for-UCI — Elo from games against a strength-limited reference engine. GPL-2.0
 *
 * The engine plays a gauntlet against one reference engine set to several
 * UCI_Elo levels (UCI_LimitStrength), games in parallel, colour-swapped pairs
 * from an opening book. Its rating is the maximum-likelihood fit of the
 * logistic Elo model to the scores at every level together.
 *
 * The default reference is the bundled Stockfish 11. Its UCI_Elo (1350-2850)
 * is anchored "to CCRL Elo (goldfish 1.13 = 2000)" by a fit to match results
 * at 60+0.6, as its source says (src/search.cpp at tag sf_11:
 * https://github.com/official-stockfish/Stockfish/blob/sf_11/src/search.cpp).
 * Its weakened play picks its move from the search at a fixed depth for each
 * level, so a level's strength depends little on the time control and the
 * computer as long as the search gets that deep; the run counts the moves where
 * it didn't (too fast a time control for that level) and warns. The tested
 * engine's strength does depend on them, so the result is its rating at that
 * time control on this computer. The interval is statistical only: how far the
 * levels are from their nominal Elo is not known and not included.
 *
 * Levels: given, or automatic. Automatic plays a first stage at levels spread
 * over the reference's whole range, then spends the rest of the games on
 * levels around the first estimate, where a game says the most.
 */
#pragma once
#include "engine_config.hpp"
#include "openings.hpp"
#include "stats.hpp"
#include "tournament.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

// The games against one level, from the tested engine's point of view.
struct LadderLevel {
    int elo = 0;                     // the reference engine's UCI_Elo
    WdlCounts wdl;
    PentaCounts penta;               // finished colour-swapped pairs
    int timeLosses = 0, crashes = 0; // games either engine lost on time / by a disconnect
    // Stockfish 11 only: its moves at this level, and those whose search stopped short of the
    // depth its UCI_Elo was calibrated with (the clock ran out first: it played weaker)
    int refMoves = 0, refShallowMoves = 0;
    double score() const { return wdl.wins + 0.5 * wdl.draws; }
};

enum class LadderStatus { NoGames, Ok, Above, Below };

struct LadderFit {
    LadderStatus status = LadderStatus::NoGames;
    double elo = 0;                  // Ok: the maximum-likelihood rating
    double ci95 = 0;                 // Ok: half-width of its 95% interval (statistical only)
    double low95 = 0, high95 = 0;    // Ok: the 95% likelihood-ratio interval (shown when extrapolated)
    double bound = 0;                // Above: a lower bound; Below: an upper bound (95%: the exact one for a
                                     // perfect or zero score, else the near end of the 95% interval)
    bool extrapolated = false;       // Ok, but over 100 Elo above the strongest (below the weakest) level played
    double dispersion = 1;           // observed variance of the pair scores / the model's (draws make it < 1)
    double fitP = 1;                 // do the levels agree on one rating? (chi-square p-value; 1 with one level)
    int games = 0;
};

// The expected score of an engine rated `elo` against a level (logistic Elo).
double ladderExpectedScore(double elo, double levelElo);
// The fit of the scores at every level. Levels without games are ignored.
LadderFit fitLadder(const std::vector<LadderLevel>& levels);
// The fit as text: "1834 +/- 41", "3012 (2911 to 3170)" (extrapolated), "above 2911",
// "below 1290", "no games".
std::string ladderHeadline(const LadderFit& f);
// The engine's performance against one level: the fit of its games alone, as text
// ("" with fewer than kLadderMinPerformanceGames games).
constexpr int kLadderMinPerformanceGames = 10;
std::string ladderPerformance(const LadderLevel& l);

struct LadderSettings {
    EngineConfig engine;             // the engine to rate
    EngineConfig reference;          // an engine with UCI_LimitStrength and UCI_Elo (see defaultLadderReference)
    TimeControl tc{60000, 600};      // 60+0.6: the time control Stockfish 11's levels were fitted at
    int timeMarginMs = 100;
    int games = 200;                 // in all; rounded to colour-swapped pairs at every level
    std::vector<int> levels;         // empty: automatic
    int concurrency = 1;
    std::string bookPath;            // "" = the bundled book (books/openings.pgn), or the start position
    BookFormat bookFormat = BookFormat::Auto;
    int bookPlies = 8;
    bool bookRandom = true;
    uint64_t seed = 1;
    std::string pgnPath;             // games appended as they finish ("" = none)
    // Resumable state ("" = none): each stage keeps its games in <statePath>.stage<N>, and
    // <statePath> the builds (size, time) of both engines. The same settings (seed included)
    // resume a stopped run, stage 2's levels following from stage 1's; a rebuilt engine or other
    // settings start again, with a warning. A run that finishes removes its state files.
    std::string statePath;
    std::string validate() const;    // "" when usable
};

// One gauntlet of the run.
struct LadderStage {
    std::vector<int> levels;
    std::vector<int> games;          // per level, even (colour-swapped pairs)
    int total() const;
};
// Automatic levels. Stage 1: kLadderSpread levels over [minElo, maxElo], about 30% of
// the games (at least one pair each). Stage 2, the rest: three levels 200 apart around the
// stage-1 fit (the odd pairs to the middle one), or all at the end level when stage 1
// gave a bound.
constexpr int kLadderSpread = 5;
constexpr int kLadderMinAutoGames = 20;
LadderStage ladderFirstStage(int games, int minElo, int maxElo);
LadderStage ladderSecondStage(const LadderFit& first, int gamesLeft, int minElo, int maxElo);
// Given levels: one stage, the games shared equally (at least one pair each; the odd
// pairs to the middle levels).
LadderStage ladderGivenStage(const std::vector<int>& levels, int games);
// Stockfish 11 picks its weakened move at this search depth for a UCI_Elo (src/search.cpp
// at sf_11; the depth for the lower of the two levels it rounds to at random).
int stockfish11PickDepth(int uciElo);
bool ladderIsStockfish11(const std::string& engineId);   // its "id name" starts "Stockfish 11"
// At a time control faster than Stockfish 11's calibration (60+0.6, compared as base + 40 x
// increment), a level is flagged when more than this share of its moves fell short of that depth.
constexpr double kLadderShallowWarn = 0.05;
constexpr int kLadderShallowMinMoves = 100;  // ...out of at least this many (one game in time trouble isn't enough)
constexpr long long kStockfish11CalibrationMs = 60000 + 40 * 600;
// "1500, 1800 2100" -> {1500, 1800, 2100} (sorted, without repeats); false on anything else.
bool parseLadderLevels(const std::string& text, std::vector<int>& out);

struct LadderProgress {
    int stage = 1, stages = 1;       // stages: 2 for automatic levels
    int gamesDone = 0, gamesPlanned = 0;
    std::vector<LadderLevel> levels; // every game so far, by level (ascending)
    LadderFit fit;
    std::vector<LiveGame> live;      // the games being played now
};

struct LadderResult {
    bool ok = false;                 // the run finished (an estimate or a bound may still be missing)
    bool aborted = false;            // stopped by the user: the results are partial
    std::string error;
    LadderSettings settings;
    std::string engineId, referenceId;   // "id name" of both engines
    int refMinElo = 0, refMaxElo = 0;    // the reference's UCI_Elo range
    std::vector<LadderStage> stages;
    std::vector<LadderLevel> levels;
    LadderFit fit;
    int gamesPlayed = 0, gamesReplayed = 0;
    std::vector<std::string> warnings;
    double seconds = 0;
};

struct LadderOptions {
    const std::atomic<bool>* abort = nullptr;
    std::function<void(const std::string&)> log;
    std::function<void(const LadderProgress&)> progress;   // about every half second
    int pollMs = 250;
};

// The bundled Stockfish 11 (Resources/reference), or "" if it is not installed.
std::string defaultLadderReference();
// The bundled opening book (Resources/books/openings.pgn), or "".
std::string defaultLadderBook();
// Plays the games and fits the rating. Blocks; call from a worker thread.
LadderResult runEloLadder(const LadderSettings& s, const LadderOptions& o = {});
std::string ladderResultJson(const LadderResult& r);
