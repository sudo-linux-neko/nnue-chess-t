// test_perft.cpp — validation harness: perft + make/unmake roundtrip fuzz.
// Author: Vaibhav Agarwal <contectforbusiness@proton.me> — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "position.h"
#include <cstdio>
#include <chrono>
#include <random>

using namespace chess;

struct Case { const char* fen; int depth; long expect; };

static const Case cases[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603}, // kiwipete
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},            // pos3
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333}, // pos4
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},       // pos5
    // ep + castling heavy regression (talkchess perft 6 pipe classic)
    {"8/5bk1/8/2Pp4/8/1K6/8/8 w - d6 0 1", 6, 824064},
    {"8/8/1k6/8/2pP4/8/5BK1/8 b - d3 0 1", 6, 824064},
};

int main() {
    std::setbuf(stdout, nullptr);
    init_tables(0x9E3779B97F4A7C15ULL);
    int fails = 0;
    for (const Case& cs : cases) {
        Position p;
        if (!p.set_fen(cs.fen)) { std::printf("BAD FEN: %s\n", cs.fen); ++fails; continue; }
        auto t0 = std::chrono::steady_clock::now();
        long got = perft(p, cs.depth);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        bool ok = got == cs.expect;
        if (!ok) ++fails;
        std::printf("[%s] d%-3d nps=%9.0Lf ms=%8.1f got=%-10ld %s\n", ok ? "OK" : "FAIL",
                    cs.depth, ms > 0 ? (long double)(got * 1000.0 / ms) : 0L, ms, got,
                    ok ? "" : cs.fen);
    }

    // make/unmake roundtrip fuzz: random legal games, verify state restoration
    std::mt19937_64 rng(12345);
    const int GAMES = 400, MAXPLY = 240;
    for (int g = 0; g < GAMES; ++g) {
        Position p; p.set_fen(Position::START_FEN);
        std::vector<std::uint64_t> keys;
        std::vector<std::string> fens;
        fens.push_back(p.fen());
        for (int ply = 0; ply < MAXPLY; ++ply) {
            MoveList ml;
            generate_legal(p, ml);
            if (ml.n == 0) break;
            Move m = ml.m[rng() % ml.n];
            keys.push_back(p.key);
            fens.push_back(""); // filled after make
            p.make(m);
            fens.back() = p.fen();
        }
        // unwind fully and compare
        bool ok = true;
        for (size_t i = keys.size(); i-- > 0;) {
            p.unmake();
            if (p.key != keys[i] || p.fen() != fens[i]) { ok = false; break; }
        }
        if (!ok) {
            ++fails;
            std::printf("ROUNDTRIP FAIL game %d: fen=%s\n", g, p.fen().c_str());
            break;
        }
    }
    std::printf("%s\n", fails ? "SOME FAILURES" : "ALL PASS");
    return fails ? 1 : 0;
}
