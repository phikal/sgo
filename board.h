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

#include <stdint.h>
#include <stdbool.h>

#ifndef BOARD_H
#define BOARD_H

enum Stone {
     NONE,
     BLACK,
     WHITE,
};

struct Board {
     uint8_t	width;
     uint8_t	height;
     uint16_t	black_captured;
     uint16_t	white_captured;
     struct Move *history;
     bool       changed;
     enum Stone      next;
     enum Stone	board[];
};

struct Coord {
     uint8_t x, y;
};

struct Move {
     enum Stone player;
     struct Coord placed;
     bool       pass;
     bool       setup;          /* setup move cannot be undone */

     struct Move *before;
     struct Move **after;
     uint16_t	children;
     
     uint16_t	removed_n;
     struct Coord removed[];
};

#define C(X, Y) ((struct Coord) { .x = (X), .y = (Y)})	    /* coord shorthand */
#define P(b, N) (C(((N) % (b)->width), ((N) / (b)->width))) /* index -> coord */
#define I(b, C) ((C).y * (b)->width + (C).x)		    /* coord -> index */
#define stone_at(b, c) (b->board[I(b, c)])
#define opposite(s) ((s) == BLACK ? WHITE : (s) == WHITE ? BLACK : (abort(), s))

struct Board	*make_board(uint8_t, uint8_t);
bool	        valid_move(struct Board *, enum Stone, struct Coord);
void            pass(struct Board*, enum Stone);
int16_t		place_stone(struct Board *, enum Stone, struct Coord);
uint16_t	player_points(struct Board *, enum Stone);
bool		undo_move(struct Board *);
void		board_free(struct Board *);

#endif
