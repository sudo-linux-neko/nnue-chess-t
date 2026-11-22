// position.cpp — FEN parse, Zobrist init, make/unmake, repetition, material.
// Author: Vaibhav Agarwal <contectforbusiness@proton.me> — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later
// © 2026 Vaibhav Agarwal.

#include "position.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace chess {

const char* Position::START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// castling bits: 1=WK(h1/e1), 2=WQ(a1/e1), 4=BK(h8/e8), 8=BQ(a8/e8)
static std::uint8_t CASTLE_CLEAR[64];
static U64 Z_PIECES[12][64];
static U64 Z_CASTLING[16];
static U64 Z_EP_FILE[8];
static U64 Z_SIDE;
static bool g_zobrist_ready = false;

static void zobrist_init() {
    if (g_zobrist_ready) return;
    U64 s = 0x9E3779B97F4A7C15ULL;
    auto next = [&]() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return s * 0xD1B54A32D192ED03ULL;
    };
    for (int p = 0; p < 12; ++p)
        for (int q = 0; q < 64; ++q) Z_PIECES[p][q] = next();
    for (int c = 0; c < 16; ++c) Z_CASTLING[c] = next();
    for (int f = 0; f < 8; ++f) Z_EP_FILE[f] = next();
    Z_SIDE = next();
    for (int i = 0; i < 64; ++i) CASTLE_CLEAR[i] = 0;
    CASTLE_CLEAR[4] = 3;   // e1: white king
    CASTLE_CLEAR[0] = 2;   // a1: white queen rook
    CASTLE_CLEAR[7] = 1;   // h1: white king rook
    CASTLE_CLEAR[60] = 12; // e8: black king
    CASTLE_CLEAR[56] = 8;  // a8: black queen rook
    CASTLE_CLEAR[63] = 4;  // h8: black king rook
    g_zobrist_ready = true;
}

void Position::grow_stack(int needed) {
    if (needed <= hist_cap) return;
    int nc = hist_cap ? hist_cap * 2 : 1024;
    while (nc < needed) nc *= 2;
    Undo* p = (Undo*)std::realloc(hist, sizeof(Undo) * (size_t)nc);
    if (!p) {
        std::fprintf(stderr, "ENGINE FATAL: out of memory for move history\n");
        std::abort();
    }
    hist = p;
    hist_cap = nc;
}

bool Position::set_fen(const std::string& fen) {
    zobrist_init();
    hist_n = 0;
    key_hist.clear();
    if (!set_fen_impl(fen)) {
        std::fprintf(stderr, "ENGINE WARNING: bad FEN '%s' - using start position\n", fen.c_str());
        set_fen_impl(START_FEN);
        return false;
    }
    key_hist.push_back(key);
    return true;
}

bool Position::set_fen_impl(const std::string& fen) {
    for (int i = 0; i < 64; ++i) board[i] = EMPTY_ID;
    for (int c = 0; c < 2; ++c)
        for (int t = 0; t < 6; ++t) pieces[c][t] = 0;
    occ[0] = occ[1] = occ_all = 0;
    kings[0] = kings[1] = -1;
    stm = WHITE; castling = 0; ep = -1; halfmove = 0; fullmove = 1;

    std::istringstream ss(fen);
    std::string board_s, stm_s, castle_s, ep_s, hm_s, fm_s;
    if (!(ss >> board_s)) return false;
    if (!(ss >> stm_s)) stm_s = "w";
    if (!(ss >> castle_s)) castle_s = "-";
    if (!(ss >> ep_s)) ep_s = "-";
    if (!(ss >> hm_s)) hm_s = "0";
    if (!(ss >> fm_s)) fm_s = "1";

    int r = 7, f = 0;
    for (char ch : board_s) {
        switch (ch) {
            case 'P': case 'N': case 'B': case 'R': case 'Q': case 'K':
            case 'p': case 'n': case 'b': case 'r': case 'q': case 'k': {
                if (f > 7 || r < 0) return false;
                const int col = (ch >= 'a') ? 1 : 0;
                PieceType t;
                switch (ch | 0x20) {
                    case 'p': t = PAWN; break; case 'n': t = KNIGHT; break;
                    case 'b': t = BISHOP; break; case 'r': t = ROOK; break;
                    case 'q': t = QUEEN; break; default: t = KING; break;
                }
                if (t == KING) {
                    if (kings[col] >= 0) return false; // duplicate king
                    kings[col] = r * 8 + f;
                }
                const int pid = make_piece(col, t), s = r * 8 + f;
                board[s] = pid;
                pieces[col][t] |= BIT(s);
                occ[col] |= BIT(s);
                occ_all |= BIT(s);
                ++f;
                break;
            }
            case '/':
                if (f != 8) return false;
                --r; f = 0;
                break;
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8':
                f += ch - '0';
                break;
            default:
                return false;
        }
    }
    if (f != 8 || r != 0) return false;
    if (kings[0] < 0 || kings[1] < 0) return false;
    if (popcount_lsb(pieces[0][KING]) != 1 || popcount_lsb(pieces[1][KING]) != 1) return false;

    stm = (stm_s == "b") ? 1 : 0;
    if (castle_s.find('K') != std::string::npos) castling |= 1;
    if (castle_s.find('Q') != std::string::npos) castling |= 2;
    if (castle_s.find('k') != std::string::npos) castling |= 4;
    if (castle_s.find('q') != std::string::npos) castling |= 8;
    if (ep_s != "-") {
        if (ep_s.size() == 2) {
            const int fi = ep_s[0] - 'a', ra = ep_s[1] - '1';
            if (fi >= 0 && fi <= 7 && ra >= 0 && ra <= 7) ep = ra * 8 + fi;
        }
    }
    halfmove = std::atoi(hm_s.c_str());
    fullmove = std::atoi(fm_s.c_str());
    if (halfmove < 0) halfmove = 0;
    if (fullmove < 0) fullmove = 0;

    // castling-rights sanity (drop stale rights)
    if (kings[0] != 4) castling &= std::uint8_t(~(1 | 2));
    if (kings[1] != 60) castling &= std::uint8_t(~(4 | 8));
    if (!(pieces[0][ROOK] & BIT(7))) castling &= std::uint8_t(~1);
    if (!(pieces[0][ROOK] & BIT(0))) castling &= std::uint8_t(~2);
    if (!(pieces[1][ROOK] & BIT(63))) castling &= std::uint8_t(~4);
    if (!(pieces[1][ROOK] & BIT(56))) castling &= std::uint8_t(~8);

    // keep the same lazy-ep convention as make(): retain ep only when an enemy
    // pawn exists positioned to make the capture (legality checks live in gen)
    if (ep >= 0) {
        const bool ok_rank = (stm == WHITE && RANK_OF(ep) == 5) || (stm == BLACK && RANK_OF(ep) == 2);
        const bool pawn_there = BIT(ep + (stm == WHITE ? -8 : 8)) & pieces[other(stm)][PAWN];
        const bool capturer_there = pawn_there && (pawn_att(other(stm), ep) & pieces[stm][PAWN]);
        if (!ok_rank || !pawn_there || !capturer_there) ep = -1;
    }

    key = 0;
    for (int s = 0; s < 64; ++s)
        if (board[s] != EMPTY_ID) key ^= Z_PIECES[board[s]][s];
    key ^= Z_CASTLING[castling];
    if (ep >= 0) key ^= Z_EP_FILE[FILE_OF(ep)];
    if (stm) key ^= Z_SIDE;
    return true;
}

std::string Position::fen() const {
    std::ostringstream o;
    for (int r = 7; r >= 0; --r) {
        int run = 0;
        for (int f = 0; f < 8; ++f) {
            const int pid = board[r * 8 + f];
            if (pid == EMPTY_ID) { ++run; continue; }
            if (run) { o << run; run = 0; }
            const char c = "pnbrqk"[type_of(pid)];
            o << (color_of(pid) == WHITE ? char(c - 0x20) : c);
        }
        if (run) o << run;
        if (r) o << '/';
    }
    o << (stm ? " b " : " w ");
    if (castling) {
        if (castling & 1) o << 'K';
        if (castling & 2) o << 'Q';
        if (castling & 4) o << 'k';
        if (castling & 8) o << 'q';
    } else o << '-';
    o << ' ';
    if (ep >= 0) o << char('a' + FILE_OF(ep)) << char('1' + RANK_OF(ep));
    else o << '-';
    o << ' ' << halfmove << ' ' << fullmove;
    return o.str();
}

U64 Position::attacks_to(int sq, int by_color, bool include_king, U64 occ_use) const {
    if (occ_use == 0) occ_use = occ_all;
    U64 att;
    if (by_color == WHITE)
        att = (shift_bb(-7, BIT(sq)) | shift_bb(-9, BIT(sq))) & pieces[WHITE][PAWN];
    else
        att = (shift_bb(7, BIT(sq)) | shift_bb(9, BIT(sq))) & pieces[BLACK][PAWN];
    att |= T.knight_bb[sq] & pieces[by_color][KNIGHT];
    att |= attacks_bishop(sq, occ_use) & (pieces[by_color][BISHOP] | pieces[by_color][QUEEN]);
    att |= attacks_rook(sq, occ_use) & (pieces[by_color][ROOK] | pieces[by_color][QUEEN]);
    if (include_king) att |= T.king_bb[sq] & pieces[by_color][KING];
    return att;
}

bool Position::insufficient_material() const {
    if (pieces[0][PAWN] | pieces[1][PAWN]) return false;
    if (pieces[0][ROOK] | pieces[1][ROOK] | pieces[0][QUEEN] | pieces[1][QUEEN]) return false;
    const int wN = popcount_lsb(pieces[0][KNIGHT]), wB = popcount_lsb(pieces[0][BISHOP]);
    const int bN = popcount_lsb(pieces[1][KNIGHT]), bB = popcount_lsb(pieces[1][BISHOP]);
    if (wN + wB + bN + bB <= 1) return true;
    if (!(wN | bN)) {
        U64 allb = pieces[0][BISHOP] | pieces[1][BISHOP];
        const bool dark = (RANK_OF(lsb_int(allb)) + FILE_OF(lsb_int(allb))) & 1;
        U64 rest = allb & (allb - 1);
        while (rest) {
            if (((RANK_OF(lsb_int(rest)) + FILE_OF(lsb_int(rest))) & 1) != dark) return false;
            rest &= rest - 1;
        }
        return true;
    }
    return false;
}

bool Position::has_repetition() const {
    if ((int)key_hist.size() < 3) return false;
    int matches = 0;
    const int lim = (int)key_hist.size();
    const int window = lim - 1 - halfmove;
    for (int i = lim - 2; i >= window && i >= 0; --i) {
        // Z_SIDE is part of the key, so parity filtering is unnecessary
        if (key_hist[i] == key && ++matches >= 2) return true;
    }
    return false;
}

U64 Position::pinned_for(int c) const {
    const int k = kings[c];
    if (k < 0) return 0;
    U64 pinned = 0;
    const U64 occ_k = occ_all ^ BIT(k);
    U64 snipers = attacks_bishop(k, occ_k) & (pieces[other(c)][BISHOP] | pieces[other(c)][QUEEN]);
    snipers |= attacks_rook(k, occ_k) & (pieces[other(c)][ROOK] | pieces[other(c)][QUEEN]);
    while (snipers) {
        const int sq = lsb_int(snipers); snipers &= snipers - 1;
        const U64 ray = between(sq, k) & occ[c];
        if (ray && !(ray & (ray - 1))) pinned |= ray;
    }
    return pinned;
}

U64 Position::checker_set(int c) const {
    return attacks_to(kings[c], other(c), false);
}

void Position::make(Move mv, int* moved_id_out) {
    grow_stack(hist_n + 1);
    Undo& u = hist[hist_n];
    const int from = mv.from_sq(), to = mv.to_sq();
    const int moved = board[from];
    const int c = color_of(moved), pt = type_of(moved);
    const bool ep_cap = mv.is_ep();
    int captured = EMPTY_ID, cap_sq = to;
    if (ep_cap) { cap_sq = to + (c == WHITE ? -8 : 8); captured = board[cap_sq]; }
    else captured = board[to];

    u.move = mv.m;
    u.moved = (std::uint8_t)moved;
    u.captured = (std::uint8_t)captured;
    u.cap_ep = (std::uint8_t)ep_cap;
    u.castling = castling;
    u.ep_square = (std::uint16_t)(ep < 0 ? 0xFFFF : ep);
    u.halfmove = (std::uint16_t)halfmove;
    u.fullmove = (std::uint16_t)fullmove;
    u.key = key;

    key ^= Z_CASTLING[castling];
    if (ep >= 0) key ^= Z_EP_FILE[FILE_OF(ep)];
    key ^= Z_PIECES[moved][from];

    board[from] = EMPTY_ID;
    pieces[c][pt] &= ~BIT(from);
    occ[c] &= ~BIT(from);
    occ_all &= ~BIT(from);

    if (captured != EMPTY_ID) {
        const int cc = color_of(captured), cpt = type_of(captured);
        pieces[cc][cpt] &= ~BIT(cap_sq);
        occ[cc] &= ~BIT(cap_sq);
        occ_all &= ~BIT(cap_sq);
        board[cap_sq] = EMPTY_ID;
        key ^= Z_PIECES[captured][cap_sq];
    }

    const PieceType placed_t = mv.is_promo() ? mv.promo_type() : (PieceType)pt;
    const int placed_id = make_piece(c, placed_t);
    board[to] = placed_id;
    key ^= Z_PIECES[placed_id][to];
    if (placed_id == moved)
        pieces[c][pt] |= BIT(to);
    else
        pieces[c][placed_t] |= BIT(to);
    occ[c] |= BIT(to);
    occ_all |= BIT(to);
    if (pt == KING) kings[c] = to;

    if (mv.is_castle()) {
        int rf, rt;
        if (to > from) { rf = (c == WHITE ? 7 : 63); rt = (c == WHITE ? 5 : 61); }  // kingside
        else { rf = (c == WHITE ? 0 : 56); rt = (c == WHITE ? 3 : 59); }            // queenside
        const int rook = board[rf];
        board[rf] = EMPTY_ID;
        board[rt] = rook;
        pieces[c][ROOK] = (pieces[c][ROOK] & ~BIT(rf)) | BIT(rt);
        occ[c] = (occ[c] & ~BIT(rf)) | BIT(rt);
        occ_all = (occ_all & ~BIT(rf)) | BIT(rt);
        key ^= Z_PIECES[rook][rf] ^ Z_PIECES[rook][rt];
    }

    ep = -1;
    if (mv.is_dpp()) {
        const int target = to + (c == WHITE ? -8 : 8);
        if (pawn_att(c, target) & pieces[other(c)][PAWN]) { ep = target; key ^= Z_EP_FILE[FILE_OF(ep)]; }
    }

    castling = (std::uint8_t)(castling & ~(CASTLE_CLEAR[from] | CASTLE_CLEAR[to]));
    key ^= Z_CASTLING[castling];

    key ^= Z_SIDE;
    stm = other(stm);
    halfmove = (pt == PAWN || captured != EMPTY_ID) ? 0 : halfmove + 1;
    if (c == BLACK) ++fullmove;

    hist_n++;
    key_hist.push_back(key);
    if (moved_id_out) *moved_id_out = moved;
}

void Position::unmake() {
    --hist_n;
    key_hist.pop_back();
    const Undo& u = hist[hist_n];
    const Move mv(u.move);
    const int from = mv.from_sq(), to = mv.to_sq();
    const int c = color_of(u.moved);
    const unsigned placed_id = (unsigned)board[to];
    if (placed_id == EMPTY_ID) {
        std::fprintf(stderr, "ENGINE FATAL: unmake() hit an empty target square (corrupt history)\n");
        std::abort();
    }
    const unsigned placed_t = placed_id % 6;

    stm = other(stm);
    ep = (u.ep_square == 0xFFFF) ? -1 : (int)u.ep_square;
    castling = u.castling;
    halfmove = u.halfmove;
    fullmove = u.fullmove;
    key = u.key;

    if (mv.is_castle()) {
        int rf, rt;
        if (to > from) { rf = (c == WHITE ? 7 : 63); rt = (c == WHITE ? 5 : 61); }
        else { rf = (c == WHITE ? 0 : 56); rt = (c == WHITE ? 3 : 59); }
        const int rook = board[rt];
        board[rf] = rook;
        board[rt] = EMPTY_ID;
        pieces[c][ROOK] = (pieces[c][ROOK] & ~BIT(rt)) | BIT(rf);
        occ[c] = (occ[c] & ~BIT(rt)) | BIT(rf);
        occ_all = (occ_all & ~BIT(rt)) | BIT(rf);
    }

    occ[c] &= ~BIT(to);
    pieces[c][placed_t] &= ~BIT(to);
    occ_all &= ~BIT(to);
    board[to] = EMPTY_ID;

    if (mv.is_promo()) {
        board[from] = u.moved;
        pieces[c][PAWN] |= BIT(from);
        occ[c] |= BIT(from);
        occ_all |= BIT(from);
        if (u.moved % 6 == KING) kings[c] = from;
    } else {
        board[from] = placed_id;
        pieces[c][placed_t] |= BIT(from);
        occ[c] |= BIT(from);
        occ_all |= BIT(from);
        if (placed_t == KING) kings[c] = from;
    }

    if (u.captured != EMPTY_ID) {
        const int cc = color_of(u.captured), cpt = type_of(u.captured);
        int cap_sq = to;
        if (u.cap_ep) cap_sq = (cc == WHITE) ? to + 8 : to - 8;
        board[cap_sq] = u.captured;
        pieces[cc][cpt] |= BIT(cap_sq);
        occ[cc] |= BIT(cap_sq);
        occ_all |= BIT(cap_sq);
    }
}

void Position::clone_from(const Position& o) {
    std::memcpy(pieces, o.pieces, sizeof(pieces));
    std::memcpy(occ, o.occ, sizeof(occ));
    occ_all = o.occ_all;
    std::memcpy(board, o.board, sizeof(board));
    kings[0] = o.kings[0]; kings[1] = o.kings[1];
    stm = o.stm;
    castling = o.castling;
    ep = o.ep;
    halfmove = o.halfmove; fullmove = o.fullmove;
    key = o.key;
    grow_stack(o.hist_cap + 8);
    if (o.hist_n > 0) std::memcpy(hist, o.hist, sizeof(Undo) * (size_t)o.hist_n);
    hist_n = o.hist_n;
    key_hist = o.key_hist;
}

} // namespace chess
