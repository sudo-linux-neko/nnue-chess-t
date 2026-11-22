// types.h — Core fixed-width types for the NNUE chess engine.
// Author: Vaibhav Agarwal (contectforbusiness@proton.me) — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later
// © 2026 Vaibhav Agarwal.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace chess {

using U64 = std::uint64_t;

enum Color : int { WHITE = 0, BLACK = 1, NO_COLOR = 2 };
inline Color operator~(Color c) { return static_cast<Color>(c ^ 1); }

enum PieceType : int { PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5, PT_NONE = 6 };
constexpr int N_PIECE_TYPES = 6;

// Square encoding a1=0 .. h8=63 (rank*8 + file).
constexpr int RANK_OF(int s) { return s >> 3; }
constexpr int FILE_OF(int s) { return s & 7; }
constexpr int make_square(int r, int f) { return (r << 3) | f; }
constexpr int flip_rank(int s) { return s ^ 56; }
constexpr int flip_file(int s) { return s ^ 7; }
constexpr int flip_diag(int s) { return flip_rank(flip_file(s)); }

// Move encoding: <5+6+4+4 bits> in uint32_t.
// bits 0..5 from, 6..11 to, 12..15 promo (piece code, 0 = none),
// 16 capture, 17 en passant (implies capture), 18 castle, 19 double push.
constexpr std::uint32_t F_CAP = 1u << 16;
constexpr std::uint32_t F_EP = 1u << 17;
constexpr std::uint32_t F_CASTLE = 1u << 18;
constexpr std::uint32_t F_DPP = 1u << 19;
constexpr std::uint32_t PROMO_NONE = 0u;
constexpr std::uint32_t PROMO_MASK = 15u << 12;

struct Move {
    std::uint32_t m = 0;
    Move() = default;
    explicit Move(std::uint32_t raw) : m(raw) {}
    Move(int from, int to, PieceType promo, std::uint32_t flags)
        : m(static_cast<std::uint32_t>(from | (to << 6) | (static_cast<int>(promo) << 12) | flags)) {}
    int from_sq() const { return m & 63; }
    int to_sq() const { return (m >> 6) & 63; }
    PieceType promo_type() const { return static_cast<PieceType>((m >> 12) & 15); }
    bool is_capture() const { return m & F_CAP; }
    bool is_ep() const { return m & F_EP; }
    bool is_castle() const { return m & F_CASTLE; }
    bool is_dpp() const { return m & F_DPP; }
    bool is_promo() const { return m & PROMO_MASK; }
    bool operator==(Move o) const { return m == o.m; }
    bool operator!=(Move o) const { return m != o.m; }
    explicit operator bool() const { return m != 0; }
};

constexpr Move MOVE_NONE = Move();

enum Bound : std::uint8_t { BND_NONE = 0, BND_UPPER = 1, BND_LOWER = 2 };

constexpr int MATE = 32000;
constexpr int MATE_IN_MAX = MATE - 255;
constexpr int MATED_IN_MAX = -MATE_IN_MAX;
constexpr int INF_SCORE = MATE + 1;

// the other color (0 <-> 1); never use ~c on ints — that is -1/-2
constexpr int other(int c) { return c ^ 1; }

// plane code used in packed dataset rows and NNUE plane ordering:
// white P..K = 0..5, black P..K = 6..11.
constexpr int plane_code(int color, int pt) { return color == BLACK ? pt + 6 : pt; }

} // namespace chess
