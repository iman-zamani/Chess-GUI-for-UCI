/* Chess-GUI-for-UCI — engine matches, tournaments and SPRT runs. GPL-2.0 */
#include "tournament.hpp"
#include "power.hpp"
#include "syzygy.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

using clk = std::chrono::steady_clock;
using json = nlohmann::json;
namespace fs = std::filesystem;

static long long nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()).count();
}

static std::string num(double v){                  // shortest form: 10, 0.1, 2.5
    std::ostringstream o; o << v; return o.str();
}

std::string TimeControl::label() const{
    std::ostringstream o;
    if (baseMs % 60000 == 0) o << baseMs / 60000;
    else o << baseMs / 1000.0 << "s";
    o << "+" << incMs / 1000.0;
    return o.str();
}
std::string TimeControl::pgnTag() const{
    return incMs ? num(baseMs / 1000.0) + "+" + num(incMs / 1000.0) : num(baseMs / 1000.0);
}

const char* tournamentModeName(TournamentMode m){
    switch (m){
    case TournamentMode::RoundRobin: return "round-robin";
    case TournamentMode::Gauntlet: return "gauntlet";
    default: return "match";
    }
}

std::string MatchSettings::fingerprint() const{
    std::ostringstream o;
    o << tournamentModeName(mode) << "|" << gauntletSeeds << "|" << tc.baseMs << "+" << tc.incMs << "|"
      << ((gamesPerPairing + 1) / 2 * 2) << "|" << bookPath << "|" << (int)bookFormat << "|" << bookPlies << "|"
      << bookRandom << "|" << seed << "|" << startFens.size() << (chess960 ? "|960" : "");
    for (auto& e : engines){
        o << "|" << e.name << "=" << e.path;
        for (auto& a : e.args) o << " " << a;
        for (auto& op : e.options) o << ";" << op.first << "=" << op.second;
    }
    return o.str();
}

std::vector<std::pair<int, int>> MatchRunner::pairingsFor(TournamentMode m, int n, int seeds){
    std::vector<std::pair<int, int>> p;
    if (m == TournamentMode::Match){ if (n >= 2) p.push_back({0, 1}); }
    else if (m == TournamentMode::RoundRobin){ for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) p.push_back({i, j}); }
    else {
        seeds = std::max(1, std::min(seeds, n - 1));
        for (int s = 0; s < seeds; s++) for (int o = seeds; o < n; o++) p.push_back({s, o});
    }
    return p;
}

// Scores are White's point of view, one per engine move, newest last.
bool MatchRunner::adjudicateSyzygy(const AdjudicationConfig& a, const Position& p, GameResult& res){
    if (a.syzygyPath.empty() || (a.syzygyPieces > 0 && syzygy::pieceCount(p) > a.syzygyPieces)) return false;
    syzygy::Wdl w = syzygy::probeWdl(p);
    GameResult win = p.whiteToMove ? GameResult::WHITE_WINS : GameResult::BLACK_WINS;
    GameResult loss = p.whiteToMove ? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
    switch (w){
    case syzygy::Wdl::Win: res = win; return true;
    case syzygy::Wdl::Loss: res = loss; return true;
    case syzygy::Wdl::Draw: case syzygy::Wdl::CursedWin: case syzygy::Wdl::BlessedLoss: res = GameResult::DRAW; return true;
    default: return false;
    }
}

bool MatchRunner::adjudicate(const AdjudicationConfig& a, const std::vector<int>& ws, int fullMove, GameResult& res){
    if (a.maxMoves > 0 && fullMove > a.maxMoves){ res = GameResult::DRAW; return true; }
    if (a.resign && a.resignMoves > 0 && (int)ws.size() >= 2 * a.resignMoves){
        bool white = true, black = true;
        for (size_t i = ws.size() - 2 * a.resignMoves; i < ws.size(); i++){
            if (ws[i] < a.resignCp) white = false;
            if (ws[i] > -a.resignCp) black = false;
        }
        if (white){ res = GameResult::WHITE_WINS; return true; }
        if (black){ res = GameResult::BLACK_WINS; return true; }
    }
    if (a.draw && a.drawMoves > 0 && fullMove >= a.drawAfterMove && (int)ws.size() >= 2 * a.drawMoves){
        bool all = true;
        for (size_t i = ws.size() - 2 * a.drawMoves; i < ws.size(); i++) if (std::abs(ws[i]) > a.drawCp) all = false;
        if (all){ res = GameResult::DRAW; return true; }
    }
    return false;
}

// ---------------------------------------------------------------- PGN
static std::string clockText(long long ms){
    if (ms < 0) ms = 0;
    char b[32];
    std::snprintf(b, sizeof b, "%lld:%02lld:%02lld.%lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, (ms % 1000) / 100);
    return b;
}

static std::string terminationTag(ResultReason r){
    switch (r){
    case ResultReason::TIMEOUT: return "time forfeit";
    case ResultReason::ADJUDICATION: return "adjudication";
    case ResultReason::ILLEGAL_MOVE: return "illegal move";
    case ResultReason::DISCONNECT: return "abandoned";
    default: return "normal";
    }
}

std::string matchGamePgn(const Game& g, const MatchSettings& s, int round, int gameInRound,
                         const std::vector<std::string>& comments, const std::string& termination, int bookPlies){
    std::ostringstream o;
    char date[16]; std::time_t t = std::time(nullptr);
    std::strftime(date, sizeof date, "%Y.%m.%d", std::localtime(&t));
    auto tag = [&](const char* k, const std::string& v){
        std::string e;
        for (char c : v){ if (c == '"' || c == '\\') e += '\\'; e += c; }
        o << "[" << k << " \"" << e << "\"]\n";
    };
    tag("Event", s.event);
    tag("Site", "Chess-GUI-for-UCI");
    tag("Date", date);
    tag("Round", std::to_string(round) + "." + std::to_string(gameInRound));
    tag("White", g.whiteName);
    tag("Black", g.blackName);
    tag("Result", g.resultString());
    std::string startFen = g.start.toFEN();
    if (g.start.chess960) tag("Variant", "Chess960");
    if (startFen != Position::startpos().toFEN() || g.start.chess960){ tag("SetUp", "1"); tag("FEN", startFen); }
    tag("TimeControl", s.tc.pgnTag());
    tag("Termination", termination);
    tag("PlyCount", std::to_string(g.sans.size()));
    o << "\n";
    std::string line;
    auto put = [&](const std::string& tok){
        if (!line.empty() && line.size() + 1 + tok.size() > 79){ o << line << "\n"; line.clear(); }
        if (!line.empty()) line += " ";
        line += tok;
    };
    int num = g.start.fullmoveNumber;
    bool w = g.start.whiteToMove;
    for (size_t i = 0; i < g.sans.size(); i++){
        if (w) put(std::to_string(num) + ".");
        else if (i == 0) put(std::to_string(num) + "...");
        put(g.sans[i]);
        if ((int)i < bookPlies) put("{book}");
        else if (i < comments.size() && !comments[i].empty()) put("{" + comments[i] + "}");
        if (!w) num++;
        w = !w;
    }
    put(g.resultString());
    o << line << "\n";
    return o.str();
}

// ---------------------------------------------------------------- run control
void MatchRunner::log(const std::string& msg){
    std::lock_guard<std::mutex> lk(mx_);
    logLocked(msg);
}

void MatchRunner::logLocked(const std::string& msg){
    snap_.log.push_back(msg);
    snap_.logTotal++;
    if (snap_.log.size() > 200) snap_.log.erase(snap_.log.begin());
    snap_.status = msg;
}

void MatchRunner::startTournament(std::vector<EngineConfig> engines, TimeControl tc, int gamesPerPair,
                                  const std::vector<std::string>& startFens){
    MatchSettings s;
    s.engines = engines;
    s.mode = engines.size() > 2 ? TournamentMode::RoundRobin : TournamentMode::Match;
    s.tc = tc;
    s.gamesPerPairing = gamesPerPair;
    s.startFens = startFens;
    std::string err;
    if (!start(s, &err)) log("Cannot start: " + err);
}

bool MatchRunner::start(const MatchSettings& settings, std::string* err){
    abort();
    auto fail = [&](const std::string& e){ if (err) *err = e; return false; };
    s_ = settings;
    int n = (int)s_.engines.size();
    if (n < 2) return fail("at least two engines are needed");
    if (s_.tc.baseMs <= 0) return fail("the base time must be positive");
    if (s_.gamesPerPairing < 1) s_.gamesPerPairing = 1;
    s_.gamesPerPairing = (s_.gamesPerPairing + 1) / 2 * 2;
    s_.concurrency = std::max(1, s_.concurrency);
    if (s_.sprtEnabled){
        if (s_.mode != TournamentMode::Match) return fail("SPRT needs a two-engine match");
        std::string e = s_.sprt.validate();
        if (!e.empty()) return fail("SPRT: " + e);
    }
    if (!s_.adj.syzygyPath.empty()){
        std::string e;
        if (!syzygy::init(s_.adj.syzygyPath, &e)) return fail(e);
    }
    // openings
    retry_.clear();
    book_.clear();
    if (!s_.bookPath.empty()){
        BookLoadResult r = loadOpeningBook(s_.bookPath, s_.bookFormat, s_.bookPlies);
        if (!r.error.empty()) return fail("opening book: " + r.error);
        book_ = r.openings;
    } else if (s_.startFens.empty() && s_.chess960){
        for (int n = 0; n < 960; n++)                        // order: s_.bookRandom / seed, like a book
            book_.push_back({Position::chess960Start(n).toFEN(), {}, "Chess960 #" + std::to_string(n)});
    } else {
        for (auto& f : s_.startFens){
            bool ok = false;
            Position p = Position::fromFEN(f, &ok);
            if (ok && enginePositionProblem(p).empty()) book_.push_back({p.toFEN(), {}, f});
        }
    }
    schedule_ = std::make_unique<OpeningSchedule>(book_.size(), s_.bookRandom, s_.seed);
    pairings_ = pairingsFor(s_.mode, n, s_.gauntletSeeds);
    // jobs: round r of every pairing, then the next round; each round is a colour-swapped pair
    jobs_.clear();
    int rounds = s_.gamesPerPairing / 2, idx = 0;
    for (int r = 0; r < rounds; r++)
        for (size_t p = 0; p < pairings_.size(); p++){
            int opening = book_.empty() ? -1 : (int)schedule_->forPair((size_t)r);
            auto [a, b] = pairings_[p];
            jobs_.push_back({idx++, a, b, opening, (int)p});
            jobs_.push_back({idx++, b, a, opening, (int)p});
        }
    done_.assign(jobs_.size(), false);
    pairResults_.assign(jobs_.size() / 2, {});
    allGames_.clear(); pgns_.clear();
    engineNames_.clear();
    for (auto& e : s_.engines) engineNames_.push_back(e.name.empty() ? fs::u8path(e.path).filename().u8string() : e.name);
    {
        std::lock_guard<std::mutex> lk(mx_);
        snap_ = Snapshot{};
        snap_.gamesTotal = snap_.totalGames = (int)jobs_.size();
        for (auto& nm : engineNames_){ Standing st; st.name = nm; snap_.standings.push_back(st); }
        for (auto& pr : pairings_){ PairingStats ps; ps.a = pr.first; ps.b = pr.second; snap_.pairings.push_back(ps); }
        snap_.sprt = s_.sprtEnabled;
        snap_.llrLower = s_.sprt.lowerBound(); snap_.llrUpper = s_.sprt.upperBound();
        snap_.live.resize(s_.concurrency);
        snap_.running = true;
    }
    loadState();
    nextJob_ = 0;
    stop_ = false; running_ = true;
    int remaining = (int)std::count(done_.begin(), done_.end(), false);
    int workers = std::max(1, std::min(s_.concurrency, remaining));
    log(std::string(tournamentModeName(s_.mode)) + ": " + std::to_string(jobs_.size()) + " games, "
        + std::to_string(workers) + " in parallel, " + s_.tc.label()
        + (book_.empty() ? ", standard start" : ", " + std::to_string(book_.size()) + " openings")
        + (snap_.gamesResumed ? ", resumed " + std::to_string(snap_.gamesResumed) + " finished games" : ""));
    activeWorkers_ = workers;
    for (int w = 0; w < workers; w++) threads_.emplace_back([this, w](){ workerLoop(w); });
    return true;
}

void MatchRunner::abort(){
    stop_ = true;
    for (auto& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
    running_ = false;
    std::lock_guard<std::mutex> lk(mx_);
    snap_.running = false;
}

MatchRunner::Snapshot MatchRunner::snapshot(){
    std::lock_guard<std::mutex> lk(mx_);
    Snapshot s = snap_;
    // legacy single-board fields mirror the first active worker
    for (auto& g : s.live) if (g.active){
        s.pos = g.pos; s.whiteName = g.whiteName; s.blackName = g.blackName;
        s.wClockMs = g.wClockMs; s.bClockMs = g.bClockMs; s.postedAtMs = g.postedAtMs;
        s.lastFrom = g.lastFrom; s.lastTo = g.lastTo; s.stats = g.stats;
        s.scoreCpWhitePOV = g.scoreCpWhite; s.scoreIsMate = g.scoreIsMate; s.mateIn = g.mateWhite;
        s.gameIndex = g.index + 1;
        break;
    }
    if (!s.recent.empty()){ s.result = s.recent.back().result; s.reason = s.recent.back().reason; }
    s.totalGames = s.gamesTotal;
    return s;
}

std::vector<std::string> MatchRunner::collectedPGNs(){
    std::lock_guard<std::mutex> lk(mx_);
    return pgns_;
}

bool MatchRunner::nextJob(Job& j){
    std::lock_guard<std::mutex> lk(mx_);
    if (stop_) return false;
    if (!retry_.empty()){ j = retry_.front(); retry_.pop_front(); return true; }
    while (nextJob_ < jobs_.size() && done_[nextJob_]) nextJob_++;
    if (nextJob_ >= jobs_.size()) return false;
    j = jobs_[nextJob_++];
    return true;
}

void MatchRunner::workerLoop(int w){
    KeepAwake awake("Chess-GUI-for-UCI is playing an engine match");   // held by this worker thread
    std::vector<std::unique_ptr<UciEngine>> engines(s_.engines.size());
    Job j;
    while (!stop_ && nextJob(j)) playGame(w, j, engines);
    for (auto& e : engines) if (e) e->quit();
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (w < (int)snap_.live.size()) snap_.live[w].active = false;
    }
    if (--activeWorkers_ == 0){
        std::lock_guard<std::mutex> lk(mx_);
        snap_.running = false;
        snap_.finished = !stop_ || snap_.sprtOutcome != SprtOutcome::Continue;
        logLocked(snap_.finished ? "Finished." : "Stopped.");
        running_ = false;
    }
}

static int whitePov(const EngineInfo& i, bool whiteMoved){
    int s = i.isMate ? (i.mateIn > 0 ? 32000 - i.mateIn : -32000 - i.mateIn) : i.scoreCp;
    return whiteMoved ? s : -s;
}

void MatchRunner::playGame(int w, const Job& job, std::vector<std::unique_ptr<UciEngine>>& engines){
    // engines are started once per worker and reused; a crashed one is restarted
    auto ensure = [&](int idx) -> UciEngine* {
        auto& e = engines[idx];
        if (e && e->alive()) return e.get();
        bool restart = e != nullptr;
        e = std::make_unique<UciEngine>();
        e->setRole("Match");
        if (!e->start(s_.engines[idx])){
            log("Cannot start " + engineNames_[idx] + ": " + e->lastError());
            e.reset();
            return nullptr;
        }
        if (restart) log("Restarted " + engineNames_[idx]);
        return e.get();
    };
    const Opening* op = job.opening >= 0 ? &book_[job.opening] : nullptr;
    Game g;
    g.reset(op ? Position::fromFEN(op->fen) : Position::startpos());
    std::string startFen = g.start.toFEN();
    std::vector<std::string> ucis;
    if (op) for (auto& u : op->moves){ Move m; if (g.pos.uciToMove(u, m) && g.tryMove(m)) ucis.push_back(u); }
    int bookPlies = (int)ucis.size();
    g.whiteName = engineNames_[job.white];
    g.blackName = engineNames_[job.black];
    std::vector<std::string> comments(ucis.size());
    std::vector<int> whiteScores;
    bool timeLoss = false, crash = false;
    long long clock[2] = {s_.tc.baseMs, s_.tc.baseMs};
    auto post = [&](const std::function<void(LiveGame&)>& f){
        std::lock_guard<std::mutex> lk(mx_);
        if (w < (int)snap_.live.size()) f(snap_.live[w]);
    };
    post([&](LiveGame& L){
        L = LiveGame{}; L.active = true; L.index = job.index;
        L.whiteName = g.whiteName; L.blackName = g.blackName; L.pos = g.pos; L.sans = g.sans;
        L.wClockMs = clock[0]; L.bClockMs = clock[1]; L.postedAtMs = nowMs();
    });
    UciEngine* eng[2] = {ensure(job.white), ensure(job.black)};
    for (int side = 0; side < 2; side++)
        if (!eng[side]){                                    // can't even start: forfeit
            g.result = side == 0 ? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
            g.reason = ResultReason::DISCONNECT;
            crash = true;
        }
    if (g.result == GameResult::ONGOING){
        GameResult r;                                       // a start position already in the tables
        if (adjudicateSyzygy(s_.adj, g.pos, r)){
            g.result = r; g.reason = ResultReason::ADJUDICATION;
            log("Game " + std::to_string(job.index + 1) + ": the start position is in the Syzygy tables");
        }
        else { eng[0]->newGame(); eng[1]->newGame(); }
    }
    while (g.result == GameResult::ONGOING && !stop_){
        int side = g.pos.whiteToMove ? 0 : 1;
        UciEngine& e = *eng[side];
        e.setPosition(startFen, ucis);
        post([&](LiveGame& L){ L.wClockMs = clock[0]; L.bClockMs = clock[1]; L.postedAtMs = nowMs(); });
        auto t0 = clk::now();
        SuspendMark asleep = suspendMark();
        std::string bm = e.goClock((int)clock[0], (int)clock[1], s_.tc.incMs, s_.tc.incMs);
        long long spent = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
        if (stop_) return;                                   // aborted: the game doesn't count
        if (long long slept = suspendedMsSince(asleep); slept > 500){
            // the clocks are meaningless across a system sleep: play the game again
            log("Game " + std::to_string(job.index + 1) + " was interrupted: the computer slept for "
                + std::to_string((slept + 500) / 1000) + " s. It will be replayed.");
            std::lock_guard<std::mutex> lk(mx_);
            retry_.push_back(job);
            return;
        }
        clock[side] -= spent;
        GameResult loss = side == 0 ? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
        if (clock[side] < -s_.timeMarginMs){
            g.result = loss; g.reason = ResultReason::TIMEOUT; timeLoss = true;
            log(std::string(side == 0 ? g.whiteName : g.blackName) + " lost on time (" + std::to_string(-clock[side]) + " ms over)");
            break;
        }
        if (clock[side] < 0) clock[side] = 0;                // within the margin
        clock[side] += s_.tc.incMs;
        Move m;
        if (bm.empty() || !g.pos.uciToMove(bm, m)){
            g.result = loss;
            std::string who = side == 0 ? g.whiteName : g.blackName;
            if (!e.alive()){
                g.reason = ResultReason::DISCONNECT; crash = true;
                log(who + " crashed or stopped responding. stderr: " + e.stderrTail(2));
            } else {
                g.reason = ResultReason::ILLEGAL_MOVE;
                log(who + (bm.empty() ? " returned no move" : " played an illegal move '" + bm + "'"));
            }
            break;
        }
        EngineInfo inf = e.lastInfo();
        MoveStat ms = MoveStat::from(inf, (int)g.moves.size(), side == 0, spent);
        int ws = whitePov(inf, side == 0);
        whiteScores.push_back(ws);
        char cm[160];
        if (inf.isMate) std::snprintf(cm, sizeof cm, "[%%eval #%d] [%%clk %s] d=%d", side == 0 ? inf.mateIn : -inf.mateIn,
                                      clockText(clock[side]).c_str(), inf.depth);
        else std::snprintf(cm, sizeof cm, "[%%eval %.2f] [%%clk %s] d=%d", ws / 100.0, clockText(clock[side]).c_str(), inf.depth);
        comments.push_back(cm);
        g.tryMove(m);
        ucis.push_back(bm);
        if (g.result == GameResult::ONGOING){
            GameResult r;
            if (adjudicateSyzygy(s_.adj, g.pos, r)){
                g.result = r; g.reason = ResultReason::ADJUDICATION;
                log("Game " + std::to_string(job.index + 1) + ": Syzygy adjudication (" + std::to_string(syzygy::pieceCount(g.pos)) + " pieces)");
            }
            else if (adjudicate(s_.adj, whiteScores, g.pos.fullmoveNumber, r)){ g.result = r; g.reason = ResultReason::ADJUDICATION; }
            else if (g.moves.size() >= 1000){ g.result = GameResult::DRAW; g.reason = ResultReason::ADJUDICATION; }
        }
        post([&](LiveGame& L){
            L.pos = g.pos; L.sans = g.sans;
            L.wClockMs = clock[0]; L.bClockMs = clock[1]; L.postedAtMs = nowMs();
            L.lastFrom = g.moves.back().from; L.lastTo = g.moves.back().to;
            L.scoreCpWhite = inf.isMate ? (ws > 0 ? 10000 : -10000) : ws;
            L.scoreIsMate = inf.isMate; L.mateWhite = side == 0 ? inf.mateIn : -inf.mateIn;
            L.stats.push_back(ms);
        });
    }
    if (stop_ && g.result == GameResult::ONGOING) return;
    int pair = job.index / 2;
    std::string pgn = matchGamePgn(g, s_, pair + 1, job.index % 2 + 1, comments, terminationTag(g.reason), bookPlies);
    recordGame(job, g, pgn, timeLoss, crash);
}

void MatchRunner::recordGame(const Job& job, const Game& g, const std::string& pgn, bool timeLoss, bool crash){
    std::lock_guard<std::mutex> lk(mx_);
    if (job.index < (int)done_.size()) done_[job.index] = true;
    GameRecord rec;
    rec.index = job.index; rec.white = job.white; rec.black = job.black; rec.opening = job.opening;
    rec.result = g.result; rec.reason = g.reason; rec.plies = (int)g.moves.size();
    allGames_.push_back(rec);
    pgns_.push_back(pgn);
    if (!s_.pgnPath.empty()){
        std::ofstream f(fs::u8path(s_.pgnPath), std::ios::app);
        f << pgn << "\n";
    }
    // standings and head-to-head statistics
    double whiteScore = g.result == GameResult::WHITE_WINS ? 1 : g.result == GameResult::BLACK_WINS ? 0 : 0.5;
    auto addResult = [&](int idx, double sc){
        Standing& st = snap_.standings[idx];
        if (sc == 1) st.wins++; else if (sc == 0) st.losses++; else st.draws++;
    };
    addResult(job.white, whiteScore);
    addResult(job.black, 1 - whiteScore);
    PairingStats& ps = snap_.pairings[job.pairing];
    double aScore = job.white == ps.a ? whiteScore : 1 - whiteScore;
    if (aScore == 1) ps.wdl.wins++; else if (aScore == 0) ps.wdl.losses++; else ps.wdl.draws++;
    if (timeLoss) ps.timeLosses++;
    if (crash) ps.crashes++;
    auto& pr = pairResults_[job.index / 2];
    pr.push_back((int)std::lround(aScore * 2));          // stored as 0/1/2 (half points)
    if (pr.size() == 2) ps.penta.addPair(pr[0] / 2.0, pr[1] / 2.0);
    snap_.gamesDone++;
    snap_.recent.push_back(rec);
    if (snap_.recent.size() > 300) snap_.recent.erase(snap_.recent.begin());
    std::string line = "Game " + std::to_string(job.index + 1) + ": " + g.whiteName + " - " + g.blackName + " "
                     + g.resultString() + (g.reason != ResultReason::NONE ? " (" + resultReasonText(g.reason) + ")" : "");
    logLocked(line);
    // SPRT: decide after every finished pair (pentanomial), as fastchess does
    if (s_.sprtEnabled && job.pairing == 0){
        snap_.llr = ps.penta.pairs() > 0 ? sprtLlr(s_.sprt, ps.penta) : 0;
        snap_.sprtOutcome = sprtOutcome(s_.sprt, snap_.llr);
        if (snap_.sprtOutcome != SprtOutcome::Continue && !stop_){
            stop_ = true;
            char b[160];
            std::snprintf(b, sizeof b, "SPRT: %s accepted (LLR %.2f, bounds %.2f / %.2f) after %d games",
                          snap_.sprtOutcome == SprtOutcome::AcceptH1 ? "H1" : "H0", snap_.llr,
                          s_.sprt.lowerBound(), s_.sprt.upperBound(), snap_.gamesDone);
            logLocked(b);
        }
    }
    saveState();
}

// ---------------------------------------------------------------- state / export
static const char* resultCode(GameResult r){
    return r == GameResult::WHITE_WINS ? "1-0" : r == GameResult::BLACK_WINS ? "0-1" : "1/2-1/2";
}

void MatchRunner::saveState(){
    if (s_.statePath.empty()) return;
    json games = json::array();
    for (auto& r : allGames_)
        games.push_back({{"index", r.index}, {"white", r.white}, {"black", r.black}, {"opening", r.opening},
                         {"result", resultCode(r.result)}, {"reason", (int)r.reason}, {"plies", r.plies}});
    json j = {{"version", 1}, {"fingerprint", s_.fingerprint()}, {"games", games}};
    std::string tmp = s_.statePath + ".tmp";
    { std::ofstream f(fs::u8path(tmp)); f << j.dump(1) << "\n"; }
    std::error_code ec;
    fs::rename(fs::u8path(tmp), fs::u8path(s_.statePath), ec);
}

void MatchRunner::loadState(){
    if (s_.statePath.empty()) return;
    std::ifstream f(fs::u8path(s_.statePath));
    if (!f) return;
    try {
        json j = json::parse(f);
        if (j.value("fingerprint", "") != s_.fingerprint()){
            log("The state file belongs to a different run; starting from scratch");
            return;
        }
        int resumed = 0;
        for (auto& g : j.at("games")){
            int idx = g.at("index");
            if (idx < 0 || idx >= (int)jobs_.size() || done_[idx]) continue;
            Game fake;
            std::string res = g.at("result");
            fake.result = res == "1-0" ? GameResult::WHITE_WINS : res == "0-1" ? GameResult::BLACK_WINS : GameResult::DRAW;
            fake.reason = (ResultReason)g.value("reason", 0);
            fake.whiteName = engineNames_[jobs_[idx].white]; fake.blackName = engineNames_[jobs_[idx].black];
            // replay the bookkeeping without PGN (it is already in the PGN file)
            std::string keepPgn = s_.pgnPath; s_.pgnPath.clear();
            std::string keepState = s_.statePath; s_.statePath.clear();
            recordGame(jobs_[idx], fake, "", fake.reason == ResultReason::TIMEOUT, fake.reason == ResultReason::DISCONNECT);
            s_.pgnPath = keepPgn; s_.statePath = keepState;
            pgns_.pop_back();
            allGames_.back().plies = g.value("plies", 0);
            resumed++;
        }
        std::lock_guard<std::mutex> lk(mx_);
        snap_.gamesResumed = resumed;
    } catch (std::exception& e){
        log(std::string("Ignoring an unreadable state file: ") + e.what());
    }
}

std::string MatchRunner::resultsJson(){
    std::lock_guard<std::mutex> lk(mx_);
    json engines = json::array();
    for (size_t i = 0; i < s_.engines.size(); i++){
        const Standing& st = snap_.standings[i];
        engines.push_back({{"name", engineNames_[i]}, {"path", s_.engines[i].path}, {"wins", st.wins},
                           {"draws", st.draws}, {"losses", st.losses}, {"points", st.points()}});
    }
    json pairs = json::array();
    for (auto& ps : snap_.pairings){
        EloEstimate e = eloFromPenta(ps.penta);
        if (!e.valid) e = eloFromWdl(ps.wdl);
        pairs.push_back({{"a", engineNames_[ps.a]}, {"b", engineNames_[ps.b]},
                         {"wins", ps.wdl.wins}, {"draws", ps.wdl.draws}, {"losses", ps.wdl.losses},
                         {"penta", {ps.penta.ll, ps.penta.ld, ps.penta.wl + ps.penta.dd, ps.penta.wd, ps.penta.ww}},
                         {"elo", e.valid ? json(e.elo) : json(nullptr)}, {"eloError", e.valid ? json(e.eloError) : json(nullptr)},
                         {"nelo", e.valid ? json(e.nelo) : json(nullptr)}, {"los", e.valid ? json(e.los) : json(nullptr)},
                         {"timeLosses", ps.timeLosses}, {"crashes", ps.crashes}});
    }
    json games = json::array();
    for (auto& r : allGames_)
        games.push_back({{"game", r.index + 1}, {"white", engineNames_[r.white]}, {"black", engineNames_[r.black]},
                         {"result", resultCode(r.result)}, {"termination", terminationTag(r.reason)},
                         {"opening", r.opening}, {"plies", r.plies}});
    json j = {{"mode", tournamentModeName(s_.mode)}, {"timeControl", s_.tc.pgnTag()}, {"gamesPlanned", jobs_.size()},
              {"gamesPlayed", snap_.gamesDone}, {"engines", engines}, {"pairings", pairs}, {"games", games}};
    if (s_.sprtEnabled)
        j["sprt"] = {{"elo0", s_.sprt.elo0}, {"elo1", s_.sprt.elo1}, {"alpha", s_.sprt.alpha}, {"beta", s_.sprt.beta},
                     {"model", sprtModelName(s_.sprt.model)}, {"llr", snap_.llr}, {"lower", s_.sprt.lowerBound()},
                     {"upper", s_.sprt.upperBound()},
                     {"outcome", snap_.sprtOutcome == SprtOutcome::AcceptH1 ? "H1" : snap_.sprtOutcome == SprtOutcome::AcceptH0 ? "H0" : "running"}};
    return j.dump(2);
}
