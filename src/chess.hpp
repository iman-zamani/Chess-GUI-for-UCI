/*
 * Chess-GUI-for-UCI — core chess logic (rules, FEN, SAN, perft)
 * Copyright (C) 2025 Iman Zamani — GPL-2.0
 */
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "values.hpp"

// Squares are indexed 0..63, sq = y*8 + x, where y=0 is rank 8 (top, black side)
// and x=0 is file 'a'. This matches FEN reading order.

struct Move {
    int8_t from = -1, to = -1;
    int8_t promo = 0;          // 0 or abs piece type (WHITE_QUEEN etc.)
    int8_t captured = 0;       // piece value captured (0 if none)
    bool isEnPassant = false;
    bool isCastle = false;
    bool isDoublePush = false;
    bool operator==(const Move& o) const { return from==o.from && to==o.to && promo==o.promo; }
};

enum class GameResult { ONGOING, WHITE_WINS, BLACK_WINS, DRAW };
enum class ResultReason { NONE, CHECKMATE, STALEMATE, FIFTY_MOVE, REPETITION,
                          INSUFFICIENT, RESIGNATION, TIMEOUT, ILLEGAL_MOVE, AGREEMENT };

struct Position {
    int board[64] = {0};       // piece values from values.hpp, 0 = empty
    bool whiteToMove = true;
    bool castleWK = false, castleWQ = false, castleBK = false, castleBQ = false;
    int epSquare = -1;         // target square for en passant, or -1
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    static Position startpos();
    static Position fromFEN(const std::string& fen, bool* ok = nullptr);
    std::string toFEN() const;
    std::string repetitionKey() const;   // FEN without move counters

    int  kingSquare(bool white) const;
    bool squareAttacked(int sq, bool byWhite) const;
    bool inCheck(bool white) const { return squareAttacked(kingSquare(white), !white); }

    void generatePseudo(std::vector<Move>& out) const;
    void generateLegal(std::vector<Move>& out) const;
    void makeMove(const Move& m);        // no legality check

    uint64_t perft(int depth) const;

    std::string moveToUCI(const Move& m) const;
    bool uciToMove(const std::string& uci, Move& out) const;   // must be legal
    std::string moveToSAN(const Move& m) const;                // call BEFORE makeMove
    bool sanToMove(const std::string& san, Move& out) const;
};

// Full game = position + history (for repetition, PGN, navigation)
struct Game {
    Position start;
    Position pos;
    std::vector<Move> moves;
    std::vector<std::string> sans;
    std::vector<std::string> repKeys;
    GameResult result = GameResult::ONGOING;
    ResultReason reason = ResultReason::NONE;
    std::string whiteName = "White", blackName = "Black";

    void reset(const Position& p);
    bool tryMove(const Move& m);         // legal check + apply + status update
    void undo();                          // takeback one ply
    void updateStatus();                  // mate/stalemate/draw rules
    std::string toPGN() const;
    std::string resultString() const;
};

// Returns "" if UCI engines can handle this position, else a human-readable
// reason. Engines like Stockfish assume real-game material limits (max 16
// pieces and 8 pawns per side, exactly one king each); beyond that their
// behavior is undefined - they crash or silently return no move.
std::string enginePositionProblem(const Position& p);

// Geometric plausibility of a premove (ignores occupancy and legality - the
// board will look different once the opponent has moved). Allows recaptures
// onto squares currently occupied by one's own pieces.
bool premovePlausible(const Position& p, int from, int to);

// ---------------- PGN import ----------------
struct PgnGame {
    std::string white="?", black="?", result="*", event, fenTag;
    std::string movetext;
};
// Split a PGN file/text into games (headers parsed, movetext kept raw).
std::vector<PgnGame> parsePGN(const std::string& text);
// Build a playable Game from one PGN game. Returns false on first illegal
// move (out contains the moves parsed up to that point; *err explains).
bool gameFromPGN(const PgnGame& pg, Game& out, std::string* err);

std::string squareName(int sq);
int nameToSquare(const std::string& n);   // -1 if invalid
