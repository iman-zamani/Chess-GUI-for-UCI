/* Chess-GUI-for-UCI — a game with variations, comments and NAGs. GPL-2.0 */
#pragma once
#include "chess.hpp"
#include <map>
#include <string>
#include <vector>

// One position of the tree, reached by `move` from `parent` (node 0 is the
// start position). children[0] continues the main line; the others are
// variations, in the order they were added.
struct GameNode {
    int parent = -1;
    std::vector<int> children;
    Move move;
    std::string san;
    Position pos;                   // after the move
    std::string comment;            // PGN { } text, without the [%eval] / [%clk] commands
    std::vector<int> nags;          // $1 = !, $2 = ?, $3 = !!, $4 = ??, $5 = !?, $6 = ?!
    bool hasEval = false, evalMate = false;
    int evalCp = 0, mateIn = 0;     // White's point of view ([%eval])
    long long clockMs = -1;         // [%clk], -1 = none
    bool alive = true;              // false once removed
};

class GameTree {
public:
    GameTree(){ reset(Position::startpos()); }
    void reset(const Position& start);

    std::string white = "White", black = "Black", result = "*";
    std::map<std::string, std::string> tags;     // other PGN tags (Event, Date, ...)

    const Position& start() const { return nodes_[0].pos; }
    const GameNode& at(int id) const { return nodes_[id]; }
    GameNode& at(int id){ return nodes_[id]; }
    int size() const { return (int)nodes_.size(); }

    // The child of `parent` playing `m` (existing or appended as a new variation).
    int add(int parent, const Move& m);
    std::vector<int> path(int id) const;          // move nodes from the root to id (root not included)
    std::vector<int> mainLine() const;            // move nodes of the main line
    int mainLineEnd() const;                      // last node of the main line (0 if no moves)
    int lineEnd(int id) const;                    // follow children[0] from id
    bool onMainLine(int id) const;
    int depth(int id) const;                      // plies from the start
    void promote(int id);                         // the line through id becomes the main line
    void remove(int id);                          // delete this move and everything after it
    Game lineGame(int id) const;                  // the path to id as a Game (rules, status)

    // PGN with variations, comments, NAGs and [%eval] / [%clk].
    std::string toPGN() const;
private:
    std::vector<GameNode> nodes_;
};

// Builds a tree from a parsed PGN game (variations "( )", comments "{ }" and
// ";", NAGs "$n" and !? style suffixes, [%eval] / [%clk] in comments). On an
// illegal move it stops, keeps what was read and returns false with *err.
bool gameTreeFromPGN(const PgnGame& pg, GameTree& out, std::string* err = nullptr);
