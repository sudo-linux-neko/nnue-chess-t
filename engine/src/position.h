// position.h — Board state, make/unmake, Zobrist, legality helpers (FEN).
// Author: Vaibhav Agarwal (contectforbusiness@proton.me) — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later
// © 2026 Vaibhav Agarwal.

#pragma once

#include "types.h"
#include "bitboard.h"

namespace chess {

// PieceId 0..5 white P..K, 6..11 black. 12 = empty.
constexpr int EMPTY_ID = 12;
constexpr inline int color_of(int pid) { return pid >= 6 ? 1 : 0; }
constexpr inline PieceType type_of(int pid) { return static_cast<PieceType>(pid % 6); }
constexpr inline int make_piece(int color, PieceType pt) { return color * 6 + static_cast<int>(pt); }

enum GenMode : int { GEN_ALL = 0, GEN_QUIET = 1, GEN_CAPTURES = 2 };

struct MoveList {
    Move m[256] = {};
    int n = 0;
    void push(Move mv) { m[n++] = mv; }
    auto begin() { return m; }
    auto end() { return m + n; }
};

// undo payload
struct Undo {
    std::uint32_t move;
    std::uint8_t captured;   // PieceId or EMPTY_ID
    std::uint8_t cap_ep;     // 1 = captured pawn en passant
    std::uint8_t castling;
    std::uint8_t moved;      // PieceId of the mover (pre-promotion)
    std::uint16_t ep_square;
    std::uint16_t halfmove;
    std::uint16_t fullmove;
    U64 key;
};

class Position {
  public:
    U64 pieces[2][6] = {};
    U64 occ[2] = {};
    U64 occ_all = 0;
    int board[64] = {};      // piece id per square (EMPTY_ID when empty)
    int kings[2] = {};
    int stm = WHITE;
    std::uint8_t castling = 0; // bits: 1=WK, 2=WQ, 4=BK, 8=BQ
    int ep = -1;
    int halfmove = 0, fullmove = 1;
    U64 key = 0;

    Undo* hist = nullptr;    // undo stack (owned)
    int hist_cap = 0, hist_n = 0;
    std::vector<U64> key_hist; // full key trail for repetition detection

    Position() { grow_stack(256); }
    Position(const Position&) = delete;
    Position& operator=(const Position&) = delete;
    ~Position() { std::free(hist); } // keep C-free; malloc/aligned not needed here
    void grow_stack(int n);

    bool set_fen(const std::string& fen);
    std::string fen() const;
    static const char* START_FEN;

    inline int side() const { return stm; }
    inline int king_sq(int c) const { return kings[c]; }
    inline int piece_at(int s) const { return board[s]; }
    inline bool attacked_by(int sq, int by_color, U64 occ_use = 0) const {
        return attacks_to(sq, by_color, true, occ_use) != 0;
    }
    inline bool in_check() const { return attacks_to(kings[stm], other(stm), true) != 0; }
    U64 attacks_to(int sq, int by_color, bool include_king = true, U64 occ_use = 0) const;

    bool see_ge(Move mv, int threshold) const;
    bool insufficient_material() const;
    bool has_repetition() const;
    inline bool fifty_rule() const { return halfmove >= 100; }

    void make(Move mv, int* moved_id_out = nullptr);
    void unmake();
    inline int ply() const { return hist_n; }

    U64 pinned_for(int c) const;      // c's pieces pinned against c's king
    U64 checker_set(int c) const;     // enemy pieces giving check to c

    void clone_from(const Position& o); // deep copy incl. history (for SMP threads)

  private:
    bool set_fen_impl(const std::string& fen);
};

// movegen — all output fully legal
void generate(Position& pos, int mode, MoveList& out);
void generate_legal(Position& pos, MoveList& out);
int perft(Position& pos, int depth);

} // namespace chess
