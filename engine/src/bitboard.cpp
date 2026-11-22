// bitboard.cpp — Magic Bitboard + PEXT (BMI2) attack-table construction.
// Runtime dispatch decided once at init: PEXT when BMI2 is present.
// Author: Vaibhav Agarwal (contectforbusiness@proton.me) — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later
// © 2026 Vaibhav Agarwal.

#include "bitboard.h"
#include <cstdlib>
#include <cstdio>

namespace chess {

AttackTables T;

namespace {

U64 rnd_state;
inline U64 rnd64() {
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 7;
    rnd_state ^= rnd_state << 17;
    return rnd_state;
}
inline U64 rnd64_sparse() { return rnd64() & rnd64() & rnd64(); }

inline U64 rook_occ_attacks(int sq, U64 occ) {
    const int f0 = FILE_OF(sq), r0 = RANK_OF(sq);
    U64 att = 0;
    static const int df[4] = {1, -1, 0, 0}, dr[4] = {0, 0, 1, -1};
    for (int d = 0; d < 4; ++d) {
        int f = f0 + df[d], r = r0 + dr[d];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            att |= BIT(r * 8 + f);
            if (occ & BIT(r * 8 + f)) break;
            f += df[d]; r += dr[d];
        }
    }
    return att;
}

inline U64 bishop_occ_attacks(int sq, U64 occ) {
    const int f0 = FILE_OF(sq), r0 = RANK_OF(sq);
    U64 att = 0;
    static const int df[4] = {1, 1, -1, -1}, dr[4] = {1, -1, 1, -1};
    for (int d = 0; d < 4; ++d) {
        int f = f0 + df[d], r = r0 + dr[d];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            att |= BIT(r * 8 + f);
            if (occ & BIT(r * 8 + f)) break;
            f += df[d]; r += dr[d];
        }
    }
    return att;
}

int rook_off[64], bishop_off[64];

template <int ROOKS>
void build_magics() {
    int offset = 0;
    for (int sq = 0; sq < 64; ++sq) {
        U64 mask = ROOKS ? T.rook_mask[sq] : T.bishop_mask[sq];
        int bits_ = popcount_lsb(mask);
        if (ROOKS) rook_off[sq] = offset; else bishop_off[sq] = offset;
        int n = 1 << bits_;
        std::vector<U64> occs(n), atts(n);
        U64 subset = 0;
        for (int i = 0; i < n; ++i) {
            occs[i] = subset;
            atts[i] = ROOKS ? rook_occ_attacks(sq, subset) : bishop_occ_attacks(sq, subset);
            subset = (subset - mask) & mask;
        }
        U64 magic = 0;
        for (;;) {
            magic = rnd64_sparse();
            if (popcount_lsb((mask * magic) & 0xFF00000000000000ULL) < 7) continue;
            std::vector<U64> used(n, 0);
            bool ok = true;
            for (int i = 0; i < n; ++i) {
                U64 idx = ((occs[i] & mask) * magic) >> (64 - bits_);
                if (used[idx] && used[idx] != atts[i]) { ok = false; break; }
                used[idx] = atts[i];
                if (ROOKS) T.rook_att[rook_off[sq] + idx] = atts[i];
                else T.bishop_att[bishop_off[sq] + idx] = atts[i];
            }
            if (ok) break;
        }
        if (ROOKS) T.rook_magic[sq] = magic; else T.bishop_magic[sq] = magic;
        offset += n;
    }
}

} // anon

/* pext runtime dispatch below uses the pragma-targeted helpers from bitboard.h */

U64 attacks_rook(int sq, U64 occ) {
    if (T.use_pext) [[likely]] return T.rook_pext[sq * 4096 + pext64(occ, T.rook_mask[sq])];
    return T.rook_att[rook_off[sq] + (((occ & T.rook_mask[sq]) * T.rook_magic[sq]) >> (64 - T.rook_bits[sq]))];
}
U64 attacks_bishop(int sq, U64 occ) {
    if (T.use_pext) [[likely]] return T.bishop_pext[sq * 512 + pext64(occ, T.bishop_mask[sq])];
    return T.bishop_att[bishop_off[sq] + (((occ & T.bishop_mask[sq]) * T.bishop_magic[sq]) >> (64 - T.bishop_bits[sq]))];
}
void init_tables(U64 seed) {
    rnd_state = seed * 2685821657736338714ULL + 2862933555777941757ULL;

    {
        static const int rdf[4] = {1, -1, 0, 0}, rdr[4] = {0, 0, 1, -1};
        static const int bdf[4] = {1, -1, 1, -1}, bdr[4] = {1, 1, -1, -1};
        int rs = 0, bs = 0;
        for (int sq = 0; sq < 64; ++sq) {
            {
                U64 mask = 0;
                for (int d = 0; d < 4; ++d) {
                    int f = FILE_OF(sq) + rdf[d], r = RANK_OF(sq) + rdr[d];
                    while (f >= 0 && f < 8 && r >= 0 && r < 8) {
                        int nf = f + rdf[d], nr = r + rdr[d];
                        if (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) mask |= BIT(r * 8 + f);
                        f = nf; r = nr;
                    }
                }
                T.rook_mask[sq] = mask;
                T.rook_bits[sq] = popcount_lsb(mask);
                rs += 1 << T.rook_bits[sq];
            }
            {
                U64 mask = 0;
                for (int d = 0; d < 4; ++d) {
                    int f = FILE_OF(sq) + bdf[d], r = RANK_OF(sq) + bdr[d];
                    while (f >= 0 && f < 8 && r >= 0 && r < 8) {
                        int nf = f + bdf[d], nr = r + bdr[d];
                        if (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) mask |= BIT(r * 8 + f);
                        f = nf; r = nr;
                    }
                }
                T.bishop_mask[sq] = mask;
                T.bishop_bits[sq] = popcount_lsb(mask);
                bs += 1 << T.bishop_bits[sq];
            }
        }
        if (!T.rook_att)   T.rook_att   = (U64*)std::aligned_alloc(64, size_t(rs) * 8);
        if (!T.bishop_att) T.bishop_att = (U64*)std::aligned_alloc(64, size_t(bs) * 8);
        std::memset(T.rook_att, 0, size_t(rs) * 8);
        std::memset(T.bishop_att, 0, size_t(bs) * 8);
    }

    build_magics<1>();
    build_magics<0>();

    if (!T.rook_pext)   T.rook_pext   = (U64*)std::aligned_alloc(64, size_t(64) * 4096 * 8);
    if (!T.bishop_pext) T.bishop_pext = (U64*)std::aligned_alloc(64, size_t(64) * 512 * 8);

#if defined(__x86_64__) && defined(__GNUC__)
    T.use_pext = __builtin_cpu_supports("bmi2");
#else
    T.use_pext = false;
#endif
    if (T.use_pext) {
        for (int sq = 0; sq < 64; ++sq) {
            U64 mask = T.rook_mask[sq];
            int bits_ = popcount_lsb(mask);
            for (int p = 0; p < (1 << bits_); ++p) {
                U64 occ = pdep64(p, mask);
                T.rook_pext[sq * 4096 + p] = rook_occ_attacks(sq, occ);
            }
            mask = T.bishop_mask[sq];
            bits_ = popcount_lsb(mask);
            for (int p = 0; p < (1 << bits_); ++p) {
                U64 occ = pdep64(p, mask);
                T.bishop_pext[sq * 512 + p] = bishop_occ_attacks(sq, occ);
            }
        }
    }

    static const int kn_df[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int kn_dr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    for (int sq = 0; sq < 64; ++sq) {
        U64 kbb = 0, kk = 0;
        for (int d = 0; d < 8; ++d) {
            int f = FILE_OF(sq) + kn_df[d], r = RANK_OF(sq) + kn_dr[d];
            if (f >= 0 && f < 8 && r >= 0 && r < 8) kbb |= BIT(r * 8 + f);
        }
        T.knight_bb[sq] = kbb;
        for (int dfx = -1; dfx <= 1; ++dfx)
            for (int drx = -1; drx <= 1; ++drx) {
                if (!dfx && !drx) continue;
                int f = FILE_OF(sq) + dfx, r = RANK_OF(sq) + drx;
                if (f >= 0 && f < 8 && r >= 0 && r < 8) kk |= BIT(r * 8 + f);
            }
        T.king_bb[sq] = kk;
        U64 pw = 0, pb = 0;
        for (int dfx = -1; dfx <= 1; dfx += 2) {
            int f = FILE_OF(sq) + dfx;
            if (f >= 0 && f < 8) {
                if (RANK_OF(sq) + 1 < 8) pw |= BIT((RANK_OF(sq) + 1) * 8 + f);
                if (RANK_OF(sq) - 1 >= 0) pb |= BIT((RANK_OF(sq) - 1) * 8 + f);
            }
        }
        T.pawn_att[WHITE][sq] = pw;
        T.pawn_att[BLACK][sq] = pb;
    }

    for (int a = 0; a < 64; ++a)
        for (int b = 0; b < 64; ++b) {
            T.between_tbl[(a << 6) | b] = 0;
            T.line_tbl[(a << 6) | b] = 0;
            if (a == b) continue;
            int adf = FILE_OF(b) - FILE_OF(a), adr = RANK_OF(b) - RANK_OF(a);
            bool aligned = (adf == 0) || (adr == 0) || (adf == adr) || (adf == -adr);
            if (!aligned) continue;
            int df = (adf == 0) ? 0 : (adf > 0 ? 1 : -1);
            int dr = (adr == 0) ? 0 : (adr > 0 ? 1 : -1);
            U64 btw = 0, lin = BIT(a) | BIT(b);
            int f = FILE_OF(a) + df, r = RANK_OF(a) + dr;
            while (r * 8 + f != b) { btw |= BIT(r * 8 + f); lin |= BIT(r * 8 + f); f += df; r += dr; }
            T.between_tbl[(a << 6) | b] = btw;
            T.line_tbl[(a << 6) | b] = lin;
        }
}

} // namespace chess
