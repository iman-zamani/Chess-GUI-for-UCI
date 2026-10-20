/* Chess-GUI-for-UCI — opening books for engine matches. GPL-2.0 */
#pragma once
#include "chess.hpp"
#include <cstdint>
#include <string>
#include <vector>

// One opening: a start position plus moves played from it (PGN books).
struct Opening {
    std::string fen;                       // full FEN of the start position
    std::vector<std::string> moves;        // UCI moves from `fen` (may be empty)
    std::string label;                     // EPD id / PGN game number, for reports
    Position finalPosition() const;        // after the moves
};

enum class BookFormat { Auto, Epd, Pgn };  // Auto: by extension (.pgn = PGN, else EPD / FEN list)

struct BookLoadResult {
    std::vector<Opening> openings;
    int skipped = 0;                       // invalid or unusable positions
    std::string error;                     // file-level error (cannot open, nothing found)
};

// EPD: "<4-field FEN> [op arg; ...]" (hmvc/fmvn opcodes honoured, id kept as
// the label); plain 6-field FEN lines work too. PGN: the first `plies` moves of
// every game (0 = all moves). Positions engines can't play are skipped.
BookLoadResult loadOpeningBook(const std::string& path, BookFormat fmt = BookFormat::Auto, int plies = 8);
BookLoadResult parseEpdBook(const std::string& text);
BookLoadResult parsePgnBook(const std::string& text, int plies);

// Which opening each game pair uses: sequential (cycling) or a seeded
// shuffle (reshuffled with a new seed-derived order each time the book runs
// out), so the sequence is reproducible.
class OpeningSchedule {
public:
    OpeningSchedule(size_t bookSize, bool random, uint64_t seed);
    size_t forPair(size_t pairIndex) const;  // index into the book
private:
    size_t n_;
    bool random_;
    uint64_t seed_;
    mutable size_t cachedPass_ = (size_t)-1;   // permutation of the last pass asked for
    mutable std::vector<size_t> perm_;
};
