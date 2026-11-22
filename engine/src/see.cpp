// see.cpp — Static Exchange Evaluation (iterative occupant-swap list, handles
// promotions, en passant, and king-safety-in-the-exchange). Standalone-testable.
// Author: Vaibhav Agarwal <contectforbusiness@proton.me> — final project authority.
// SPDX-License-Identifier: GPL-3.0-or-later
// © 2026 Vaibhav Agarwal.

#include "position.h"

namespace chess {

static constexpr int SEE_VAL[6] = { 100, 300, 300, 500, 900, 10000 };

// Full exchange-resolved SEE from the perspective of the side making `mv`.
// Semantics (derived & unit-checked):
//   swapList[0] = value of the captured unit (initial occupant / ep pawn / 0)
//   swapList[k] = value of the piece that WOULD BE captured at step k
//   V(i) = max(0, swapList[i] - V(i+1))     (each later side may decline)
//   SEE     = swapList[0] - V(1)            (step 0 is mandatory: the move itself)
int see_eval(const Position& pos, Move mv) {
    const int from = mv.from_sq(), to = mv.to_sq();
    const int mover_id = pos.piece_at(from);
    if (mover_id == EMPTY_ID) return 0;
    const int c0 = color_of(mover_id);
    const int pt_mover = type_of(mover_id);

    // occupant of `to` right after `mv` lands
    int occupant = mv.is_promo() ? (int)mv.promo_type()
                 : (mv.is_ep() ? (int)PAWN : pt_mover);

    // step 0 value: what was there before
    int swap[33];
    swap[0] = mv.is_ep() ? SEE_VAL[PAWN]
              : (pos.piece_at(to) != EMPTY_ID ? SEE_VAL[type_of(pos.piece_at(to))] : 0);

    U64 occ = pos.occ_all & ~BIT(from);
    if (mv.is_ep()) occ &= ~BIT(to + (c0 == WHITE ? -8 : 8)); // remove the passed pawn from the exchange model
    // note: the square `to` stays occupied in `occ`; rays FROM `to` never see its own occupant.

    int n = 1;
    int side = other(c0);
    for (;;) {
        U64 att = pos.attacks_to(to, side, false, occ) & pos.occ[side];
        int at = -1;
        for (int t = PAWN; t <= QUEEN; ++t)
            if (att & pos.pieces[side][t]) { at = t; break; }
        if (at < 0) {
            if (att & pos.pieces[side][KING]) {
                const U64 them = pos.attacks_to(to, other(side), false, occ) & pos.occ[other(side)];
                if (!them) at = KING; // king capture only when opponent has no attacker at all
            }
        }
        if (at < 0) break;

        swap[n++] = SEE_VAL[occupant];
        U64 att_sq_set = att & pos.pieces[side][at];
        const int s = lsb_int(att_sq_set);
        occ &= ~BIT(s);
        // occupant that will face the NEXT capture: the arriving piece
        occupant = (at == PAWN && (side == WHITE ? RANK_OF(s) : 7 - RANK_OF(s)) == 7) ? (int)QUEEN : at;
        if (n >= 32) break;
        side = other(side);
    }

    // backward minimax (decline allowed from step 1 on)
    int v = 0;
    for (int i = n - 1; i >= 1; --i) v = swap[i] - v > 0 ? swap[i] - v : 0;
    return swap[0] - v;
}

bool Position::see_ge(Move mv, int threshold) const {
    // cheap pre-filters keep hot paths fast
    const int from = mv.from_sq();
    if (piece_at(from) == EMPTY_ID) return false;
    return see_eval(*this, mv) >= threshold;
}

} // namespace chess
