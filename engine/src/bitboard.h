// bitboard.h — Bitboard utilities, sliding-piece attacks via Magic Bitboards or
// PEXT (BMI2), with runtime dispatch so one binary runs on every x86-64 CPU.
// Author: Vaibhav Agarwal (contectforbusiness@proton.me) — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later
// © 2026 Vaibhav Agarwal.

#pragma once

#include "types.h"
#include <array>
#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>
#endif

namespace chess {

constexpr U64 BIT(int s) { return 1ULL << s; }
constexpr U64 ALL_SQUARES_BB = 0xffffffffffffffffULL;

constexpr U64 RANK_BB[8] = {
    0x00000000000000FFULL, 0x000000000000FF00ULL, 0x0000000000FF0000ULL, 0x00000000FF000000ULL,
    0x000000FF00000000ULL, 0x0000FF0000000000ULL, 0x00FF000000000000ULL, 0xFF00000000000000ULL };
constexpr U64 FILE_BB[8] = {
    0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
    0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL };

template <typename U>
inline int popcount_lsb(U b) { return __builtin_popcountll(b); }
inline int lsb_int(U64 b) { return __builtin_ctzll(b); }
inline int msb_int(U64 b) { return 63 - __builtin_clzll(b); }
inline U64 lsb_bb(U64 b) { return b & -b; }

// pext/pdep emission restricted to these helpers via pragma target so the whole
// binary stays runnable on pre-BMI2 CPUs (runtime dispatch survives).
#if defined(__x86_64__) && defined(__GNUC__)
#pragma GCC push_options
#pragma GCC target("bmi2")
inline U64 pext64(U64 v, U64 m) { return _pext_u64(v, m); }
inline U64 pdep64(U64 p, U64 m) { return _pdep_u64(p, m); }
#pragma GCC pop_options
#else
inline U64 pext64(U64, U64) { return 0; }
inline U64 pdep64(U64, U64) { return 0; }
#endif

struct AttackTables {
    std::array<int, 64> rook_bits{}, bishop_bits{};
    std::array<U64, 64> rook_mask{}, bishop_mask{}, rook_magic{}, bishop_magic{};
    U64* rook_att = nullptr;    // magic-indexed
    U64* bishop_att = nullptr;  // magic-indexed
    U64* rook_pext = nullptr;   // 64 * 4096
    U64* bishop_pext = nullptr; // 64 * 512
    U64 knight_bb[64] = {}, king_bb[64] = {}, pawn_att[2][64] = {};
    U64 between_tbl[4096] = {};  // between[a][b], index (a<<6)|b
    U64 line_tbl[4096] = {};     // aligned-increasing ray (a,b inclusive)
    bool use_pext = false;
};

extern AttackTables T;

void init_tables(U64 seed = 97); // builds all aux tables (~0.3-2 s)

U64 attacks_rook(int sq, U64 occ);
U64 attacks_bishop(int sq, U64 occ);
inline U64 attacks_queen(int sq, U64 occ) { return attacks_rook(sq, occ) | attacks_bishop(sq, occ); }
inline U64 attacks_knight(int sq) { return T.knight_bb[sq]; }
inline U64 attacks_king(int sq) { return T.king_bb[sq]; }
inline U64 pawn_att(int color, int sq) { return T.pawn_att[color][sq]; }
inline U64 between(int a, int b) { return T.between_tbl[(a << 6) | b]; }
inline U64 full_line(int a, int b) { return T.line_tbl[(a << 6) | b]; }
inline bool squares_aligned(int a, int b) { return (a != b) && (T.line_tbl[(a << 6) | b] != 0); }

constexpr U64 shift_bb(int delta, U64 b) {
    switch (delta) {
        case 8:  return b << 8;
        case -8: return b >> 8;
        case 9:  return (b & ~FILE_BB[7]) << 9;
        case 7:  return (b & ~FILE_BB[0]) << 7;
        case -9: return (b & ~FILE_BB[0]) >> 9;
        case -7: return (b & ~FILE_BB[7]) >> 7;
        default: return 0;
    }
}

} // namespace chess
