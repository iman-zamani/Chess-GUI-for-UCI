/* Chess-GUI-for-UCI — Syzygy tablebase probing (via Fathom). GPL-2.0 */
#pragma once
#include "chess.hpp"
#include <string>

// One set of tables per process. Probing is thread-safe; init() waits for
// running probes to finish.
namespace syzygy {

// Loads the tables in `paths` (directories separated by ';' on Windows and
// ':' elsewhere). "" unloads them. Returns false with *err if no table is found.
// Calling it again with the same paths does nothing.
bool init(const std::string& paths, std::string* err = nullptr);
int largest();                     // most pieces any loaded table covers (0 = none loaded)

// Win/draw/loss for the side to move, under the 50-move rule (cursed wins and
// blessed losses are draws). Unknown when no table covers the position, it has
// castling rights or its halfmove clock is not 0 (WDL tables only answer right
// after a capture or pawn move, which is when a game enters a table).
enum class Wdl { Unknown, Loss, BlessedLoss, Draw, CursedWin, Win };
Wdl probeWdl(const Position& p);
const char* wdlName(Wdl w);        // "win", "cursed win", ...

int pieceCount(const Position& p);

}
