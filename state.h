/* Copyright 2020-2021 Philip Kaludercic
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef STATE_H
#define STATE_H

enum State {
     /* initial state, before everything has been configured */
     INIT,
     /* X (human) has just confirmed a move, i.e. clicked at some
      * position */
     CONFIRM_BLACK,
     CONFIRM_WHITE,
     /* we are waiting for X (human) to make a move  */
     QUERY_BLACK,
     QUERY_WHITE,
     /* X (human) has passed the last move */
     PASS_BLACK,
     PASS_WHITE,
     /* X (human or bot) has resigned the game */
     RESIGN_BLACK,
     RESIGN_WHITE,
     /* game is over, we are in the final state */
     GAMEOVER,
     /* stop sgo completly */
     TERM,
};

/* To ensure that all state transitions are valid, this table is used
 * to check if the current state may transition into a given next one
 * (see S macro below) */
static bool valid_transition[11][11] = {
     [CONFIRM_BLACK] = {
          [QUERY_WHITE] = true,      /* continue */
          [QUERY_BLACK] = true,      /* invalid move */
     }, [CONFIRM_WHITE] = {
          [QUERY_BLACK] = true,      /* continue */
          [QUERY_WHITE] = true,      /* invalid move */
     },
     [QUERY_BLACK] = {
          [QUERY_BLACK] = true,      /* noop */
          [QUERY_WHITE] = true,      /* undo */
          [CONFIRM_BLACK] = true,    /* button 1 */
          [PASS_BLACK] = true,       /* button 2 */
          [RESIGN_BLACK] = true,     /* 2x button 2 */
          [GAMEOVER] = true          /* mark game as over */
     },
     [QUERY_WHITE] = {
          [QUERY_WHITE] = true,      /* noop */
          [QUERY_BLACK] = true,      /* undo */
          [CONFIRM_WHITE] = true,    /* button 1 */
          [PASS_WHITE] = true,       /* button 2 */
          [RESIGN_WHITE] = true,     /* 2x button 2 */
          [GAMEOVER] = true          /* mark game as over */
     },
     [PASS_BLACK] = {
          [QUERY_WHITE] = true,      /* continue */
     },
     [PASS_WHITE] = {
          [QUERY_BLACK] = true,      /* continue  */
     },
     [RESIGN_BLACK] = {
          [GAMEOVER] = true          /* mark game as over */
     },
     [RESIGN_WHITE] = {
          [GAMEOVER] = true          /* mark game as over */
     },
     [GAMEOVER] = {
          [GAMEOVER] = true,
     },
};

/* state transition macro */
#define S0(state, nstate)                                \
     do {                                                \
          assert(valid_transition[state][nstate]);       \
          (state) = ((nstate));                          \
     } while (0)

#define S(nstate) S0(state, nstate)
#define S1(nstate) S0(*state, nstate)

#endif
