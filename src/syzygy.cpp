/* Chess-GUI-for-UCI — Syzygy tablebase probing (via Fathom). GPL-2.0 */
#include "syzygy.hpp"
#include "tbprobe.h"
#include <cstdlib>
#include <mutex>
#include <shared_mutex>

namespace syzygy {

static std::shared_mutex g_mx;      // probes share it; init takes it exclusively
static std::string g_paths;

bool init(const std::string& paths, std::string* err){
    std::unique_lock<std::shared_mutex> lk(g_mx);
    if (paths == g_paths && (paths.empty() || TB_LARGEST > 0)) return true;
    g_paths.clear();
    if (paths.empty()){ tb_free(); return true; }
    if (!tb_init(paths.c_str()) || TB_LARGEST == 0){
        tb_free();
        if (err) *err = "no Syzygy tables found in " + paths;
        return false;
    }
    g_paths = paths;
    return true;
}

int largest(){
    std::shared_lock<std::shared_mutex> lk(g_mx);
    return g_paths.empty() ? 0 : (int)TB_LARGEST;
}

int pieceCount(const Position& p){
    int n = 0;
    for (int v : p.board) if (v) n++;
    return n;
}

Wdl probeWdl(const Position& p){
    std::shared_lock<std::shared_mutex> lk(g_mx);
    if (g_paths.empty() || pieceCount(p) > (int)TB_LARGEST) return Wdl::Unknown;
    if (p.castleWK || p.castleWQ || p.castleBK || p.castleBQ || p.halfmoveClock != 0) return Wdl::Unknown;
    uint64_t white = 0, black = 0, kings = 0, queens = 0, rooks = 0, bishops = 0, knights = 0, pawns = 0;
    for (int sq = 0; sq < 64; sq++){
        int v = p.board[sq];
        if (!v) continue;
        // ours: sq = y*8 + x with y = 0 on rank 8; Fathom: a1 = 0, h8 = 63
        uint64_t bit = 1ULL << ((7 - sq / 8) * 8 + sq % 8);
        (v > 0 ? white : black) |= bit;
        switch (std::abs(v)){
        case WHITE_KING:   kings |= bit; break;
        case WHITE_QUEEN:  queens |= bit; break;
        case WHITE_ROOK:   rooks |= bit; break;
        case WHITE_BISHOP: bishops |= bit; break;
        case WHITE_KNIGHT: knights |= bit; break;
        case WHITE_PAWN:   pawns |= bit; break;
        }
    }
    unsigned ep = p.epSquare >= 0 ? (unsigned)((7 - p.epSquare / 8) * 8 + p.epSquare % 8) : 0;
    unsigned r = tb_probe_wdl(white, black, kings, queens, rooks, bishops, knights, pawns, 0, 0, ep, p.whiteToMove);
    switch (r){
    case TB_LOSS: return Wdl::Loss;
    case TB_BLESSED_LOSS: return Wdl::BlessedLoss;
    case TB_DRAW: return Wdl::Draw;
    case TB_CURSED_WIN: return Wdl::CursedWin;
    case TB_WIN: return Wdl::Win;
    default: return Wdl::Unknown;
    }
}

const char* wdlName(Wdl w){
    switch (w){
    case Wdl::Loss: return "loss";
    case Wdl::BlessedLoss: return "blessed loss";
    case Wdl::Draw: return "draw";
    case Wdl::CursedWin: return "cursed win";
    case Wdl::Win: return "win";
    default: return "unknown";
    }
}

}
