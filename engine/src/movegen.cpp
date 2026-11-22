// movegen.cpp — Pin-filtered legal move generation (exact ep handling,
// evasion generation) + perft. All output is fully legal.
// Author: Vaibhav Agarwal <contectforbusiness@proton.me> — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later
// © 2026 Vaibhav Agarwal.

#include "position.h"

namespace chess {

namespace {

inline Move mv(int from, int to, PieceType promo, std::uint32_t flags) {
    return Move(from, to, promo, flags);
}

inline bool is_pinned_now(U64 pinned, int sq) { return pinned & BIT(sq); }

// exact legality via make/attacks/unmake (used only for ep and evasions)
bool move_is_legal_exact(Position& pos, Move m) {
    pos.make(m);
    const int mover = other(pos.side());
    const bool ok = pos.attacks_to(pos.kings[mover], pos.side(), true, pos.occ_all) == 0;
    pos.unmake();
    return ok;
}

void gen_pawns(const Position& pos, MoveList& out, U64 pinned, U64 target_mask,
               bool include_pushes, bool include_ep) {
    const int c = pos.side();
    const int up = (c == WHITE) ? 8 : -8;
    const int promo_rank = (c == WHITE) ? 7 : 0;
    const int start_rank = (c == WHITE) ? 1 : 6;
    U64 pawns = pos.pieces[c][PAWN];

    while (pawns) {
        const int s = lsb_int(pawns); pawns &= pawns - 1;
        const bool pin = is_pinned_now(pinned, s);
        const U64 line = pin ? full_line(pos.kings[c], s) : ~0ULL;

        // quiet pushes (promotions always included — they are tactical with value)
        if (include_pushes) {
            const int s1 = s + up;
            if (!(pos.occ_all & BIT(s1))) {
                if ((line & BIT(s1)) && (target_mask & BIT(s1))) {
                    if (RANK_OF(s1) == promo_rank) {
                        out.push(mv(s, s1, QUEEN, 0));
                        out.push(mv(s, s1, ROOK, 0));
                        out.push(mv(s, s1, BISHOP, 0));
                        out.push(mv(s, s1, KNIGHT, 0));
                    } else {
                        out.push(mv(s, s1, PT_NONE, 0));
                    }
                }
                if (RANK_OF(s) == start_rank) {
                    const int s2 = s + 2 * up;
                    if (!(pos.occ_all & BIT(s2)) && (line & BIT(s2)) && (target_mask & BIT(s2)))
                        out.push(mv(s, s2, PT_NONE, F_DPP));
                }
            }
        }

        // captures
        U64 caps = pawn_att(c, s) & pos.occ[other(c)] & target_mask;
        while (caps) {
            const int t = lsb_int(caps); caps &= caps - 1;
            if (!(line & BIT(t))) continue;
            if (RANK_OF(t) == promo_rank) {
                out.push(mv(s, t, QUEEN, F_CAP));
                out.push(mv(s, t, ROOK, F_CAP));
                out.push(mv(s, t, BISHOP, F_CAP));
                out.push(mv(s, t, KNIGHT, F_CAP));
            } else {
                out.push(mv(s, t, PT_NONE, F_CAP));
            }
        }

        // en passant — exact-verified by caller (discovered-check safe)
        if (include_ep && pos.ep >= 0 && (pawn_att(c, s) & BIT(pos.ep))
            && (target_mask & BIT(pos.ep))
            && !(pin && !(full_line(pos.kings[c], s) & BIT(pos.ep)))) {
            out.push(mv(s, pos.ep, PT_NONE, F_EP | F_CAP));
        }
    }
}

void gen_pieces(const Position& pos, MoveList& out, int pt, U64 pinned, U64 target_mask) {
    const int c = pos.side();
    U64 pcs = pos.pieces[c][pt];
    while (pcs) {
        const int s = lsb_int(pcs); pcs &= pcs - 1;
        const bool pin = is_pinned_now(pinned, s);
        if (pin && pt == KNIGHT) continue; // a pinned knight can never move
        U64 att;
        switch (pt) {
            case KNIGHT: att = T.knight_bb[s]; break;
            case BISHOP: att = attacks_bishop(s, pos.occ_all); break;
            case ROOK:   att = attacks_rook(s, pos.occ_all); break;
            case QUEEN:  att = attacks_queen(s, pos.occ_all); break;
            default:     att = T.king_bb[s]; break;
        }
        att &= ~pos.occ[c] & target_mask;
        if (pin) att &= full_line(pos.kings[c], s);
        while (att) {
            const int t = lsb_int(att); att &= att - 1;
            out.push(mv(s, t, PT_NONE, pos.occ[other(c)] & BIT(t) ? F_CAP : 0));
        }
    }
}

void gen_king(const Position& pos, MoveList& out, bool castles, U64 target_mask) {
    const int c = pos.side();
    const int k = pos.kings[c];
    const U64 occ_nk = pos.occ_all & ~BIT(k);

    U64 att = T.king_bb[k] & ~pos.occ[c] & target_mask;
    while (att) {
        const int t = lsb_int(att); att &= att - 1;
        // destination must not be attacked with the king removed from occupancy
        if (pos.attacks_to(t, other(c), false, occ_nk) == 0)
            out.push(mv(k, t, PT_NONE, pos.occ[other(c)] & BIT(t) ? F_CAP : 0));
    }

    if (!castles) return;
    const int rights = pos.castling;
    if (c == WHITE) {
        if (!(rights & 1) && !(rights & 2)) return;
    } else {
        if (!(rights & 4) && !(rights & 8)) return;
    }
    if (pos.in_check()) return; // castling never possible out of/through check

    const U64 occ_t = occ_nk & ~BIT(c == WHITE ? 7 : 63);   // h-rook vacates
    const U64 occ_q = occ_nk & ~BIT(c == WHITE ? 0 : 56);   // a-rook vacates
    if (c == WHITE) {
        if ((rights & 1) && !(pos.occ_all & (BIT(5) | BIT(6)))
            && !pos.attacks_to(6, BLACK, false, occ_t) && !pos.attacks_to(5, BLACK, false, occ_t))
            out.push(mv(4, 6, PT_NONE, F_CASTLE));
        if ((rights & 2) && !(pos.occ_all & (BIT(1) | BIT(3)))
            && !pos.attacks_to(3, BLACK, false, occ_q) && !pos.attacks_to(2, BLACK, false, occ_q))
            out.push(mv(4, 2, PT_NONE, F_CASTLE));
    } else {
        if ((rights & 4) && !(pos.occ_all & (BIT(61) | BIT(62)))
            && !pos.attacks_to(62, WHITE, false, occ_t) && !pos.attacks_to(61, WHITE, false, occ_t))
            out.push(mv(60, 62, PT_NONE, F_CASTLE));
        if ((rights & 8) && !(pos.occ_all & (BIT(57) | BIT(59)))
            && !pos.attacks_to(59, WHITE, false, occ_q) && !pos.attacks_to(58, WHITE, false, occ_q))
            out.push(mv(60, 58, PT_NONE, F_CASTLE));
    }
}

// king moves + single-checker capture/block candidates, all exactly verified
void gen_evasions(Position& pos, MoveList& out) {
    out.n = 0;
    const int c = pos.side();
    const int k = pos.kings[c];
    gen_king(pos, out, /*castles=*/false, ~0ULL);

    const U64 checkers = pos.checker_set(c);
    if (popcount_lsb(checkers) > 1) return; // double check: king moves only
    const int csq = lsb_int(checkers);
    const U64 targets = between(csq, k) | BIT(csq);
    const U64 pinned = pos.pinned_for(c);

    // pawn candidates
    {
        const int up = (c == WHITE) ? 8 : -8;
        const int promo_rank = (c == WHITE) ? 7 : 0;
        const int start_rank = (c == WHITE) ? 1 : 6;
        U64 pawns = pos.pieces[c][PAWN];
        while (pawns) {
            const int s = lsb_int(pawns); pawns &= pawns - 1;
            const bool pin = is_pinned_now(pinned, s);
            const U64 line = pin ? full_line(k, s) : ~0ULL;
            const int s1 = s + up;
            // blocking push
            if (!(pos.occ_all & BIT(s1)) && (line & BIT(s1)) && (targets & BIT(s1)))
                out.push(mv(s, s1, PT_NONE, 0));
            // blocking double push
            const int s2 = s + 2 * up;
            if (RANK_OF(s) == start_rank && !(pos.occ_all & BIT(s1)) && !(pos.occ_all & BIT(s2))
                && (line & BIT(s2)) && (targets & BIT(s2)))
                out.push(mv(s, s2, PT_NONE, F_DPP));
            // capture of the checking pawn
            if (pawn_att(c, s) & BIT(csq) && !(pin && !(line & BIT(csq)))) {
                if (RANK_OF(csq) == promo_rank) {
                    for (PieceType promo : { QUEEN, ROOK, BISHOP, KNIGHT })
                        out.push(mv(s, csq, promo, F_CAP));
                } else {
                    out.push(mv(s, csq, PT_NONE, F_CAP));
                }
            }
            // ep capture removes the checking double-pushed pawn
            if (pos.ep == csq && (pawn_att(c, s) & BIT(csq)))
                out.push(mv(s, csq, PT_NONE, F_EP | F_CAP));
        }
    }
    gen_pieces(pos, out, KNIGHT, pinned, targets);
    gen_pieces(pos, out, BISHOP, pinned, targets);
    gen_pieces(pos, out, ROOK, pinned, targets);
    gen_pieces(pos, out, QUEEN, pinned, targets);

    // exact filter — candidate set is tiny, make/unmake cost is negligible
    const int n = out.n;
    int w = 0;
    for (int i = 0; i < n; ++i)
        if (move_is_legal_exact(pos, out.m[i])) out.m[w++] = out.m[i];
    out.n = w;
}

// fully legal, no-check state assumed
void gen_semi(Position& pos, MoveList& out, U64 target_mask, bool include_pushes, bool include_ep) {
    out.n = 0;
    const U64 pinned = pos.pinned_for(pos.side());
    gen_pawns(pos, out, pinned, target_mask, include_pushes, include_ep);
    gen_pieces(pos, out, KNIGHT, pinned, target_mask);
    gen_pieces(pos, out, BISHOP, pinned, target_mask);
    gen_pieces(pos, out, ROOK, pinned, target_mask);
    gen_pieces(pos, out, QUEEN, pinned, target_mask);
    gen_king(pos, out, /*castles=*/true, target_mask);

    // exact filter for ep only (pin logic + occ checks cover everything else)
    if (!include_ep) return;
    const int n = out.n;
    int w = 0;
    for (int i = 0; i < n; ++i) {
        if (!out.m[i].is_ep()) { out.m[w++] = out.m[i]; continue; }
        if (move_is_legal_exact(pos, out.m[i])) out.m[w++] = out.m[i];
    }
    out.n = w;
}

inline bool is_tactical(Move m) {
    return m.is_capture() || m.is_promo();
}

// tactical subset: captures, promotion pushes, ep — used by qsearch
void gen_tactical(Position& pos, MoveList& out) {
    out.n = 0;
    const int c = pos.side();
    const int up = (c == WHITE) ? 8 : -8;
    const int promo_rank = (c == WHITE) ? 7 : 0;
    const U64 pinned = pos.pinned_for(c);
    const U64 caps_mask = pos.occ[other(c)];

    U64 pawns = pos.pieces[c][PAWN];
    while (pawns) {
        const int s = lsb_int(pawns); pawns &= pawns - 1;
        const bool pin = is_pinned_now(pinned, s);
        const U64 line = pin ? full_line(pos.kings[c], s) : ~0ULL;
        // quiet promotions (they are always tactical)
        const int s1 = s + up;
        if (!(pos.occ_all & BIT(s1)) && RANK_OF(s1) == promo_rank && (line & BIT(s1))) {
            out.push(mv(s, s1, QUEEN, 0));
            out.push(mv(s, s1, ROOK, 0));
            out.push(mv(s, s1, BISHOP, 0));
            out.push(mv(s, s1, KNIGHT, 0));
        }
        // captures incl. promotion captures
        U64 caps = pawn_att(c, s) & caps_mask;
        while (caps) {
            const int t = lsb_int(caps); caps &= caps - 1;
            if (!(line & BIT(t))) continue;
            if (RANK_OF(t) == promo_rank) {
                out.push(mv(s, t, QUEEN, F_CAP));
                out.push(mv(s, t, ROOK, F_CAP));
                out.push(mv(s, t, BISHOP, F_CAP));
                out.push(mv(s, t, KNIGHT, F_CAP));
            } else {
                out.push(mv(s, t, PT_NONE, F_CAP));
            }
        }
        // ep pseudo — exact-checked below
        if (pos.ep >= 0 && (pawn_att(c, s) & BIT(pos.ep))
            && !(pin && !(full_line(pos.kings[c], s) & BIT(pos.ep)))) {
            out.push(mv(s, pos.ep, PT_NONE, F_EP | F_CAP));
        }
    }
    gen_pieces(pos, out, KNIGHT, pinned, caps_mask);
    gen_pieces(pos, out, BISHOP, pinned, caps_mask);
    gen_pieces(pos, out, ROOK, pinned, caps_mask);
    gen_pieces(pos, out, QUEEN, pinned, caps_mask);
    gen_king(pos, out, /*castles=*/false, caps_mask);

    const int n = out.n;
    int w = 0;
    for (int i = 0; i < n; ++i) {
        if (!out.m[i].is_ep()) { out.m[w++] = out.m[i]; continue; }
        if (move_is_legal_exact(pos, out.m[i])) out.m[w++] = out.m[i];
    }
    out.n = w;
}

} // anon

void generate_legal(Position& pos, MoveList& out) {
    if (pos.checker_set(pos.side())) { gen_evasions(pos, out); return; }
    gen_semi(pos, out, ~0ULL, /*pushes=*/true, /*ep=*/true);
}

void generate(Position& pos, int mode, MoveList& out) {
    switch (mode) {
        case GEN_CAPTURES:
            if (pos.checker_set(pos.side())) { generate_legal(pos, out); return; }
            gen_tactical(pos, out);
            return;
        case GEN_QUIET:
            generate_legal(pos, out);
            {
                int w = 0;
                for (int i = 0; i < out.n; ++i)
                    if (!is_tactical(out.m[i])) out.m[w++] = out.m[i];
                out.n = w;
            }
            return;
        default: // GEN_ALL
            generate_legal(pos, out);
            return;
    }
}

int perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList ml;
    generate_legal(pos, ml);
    if (depth == 1) return ml.n;
    int nodes = 0;
    for (int i = 0; i < ml.n; ++i) {
        pos.make(ml.m[i]);
        nodes += perft(pos, depth - 1);
        pos.unmake();
    }
    return nodes;
}

} // namespace chess
