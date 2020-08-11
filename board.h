/* Copyright 2020 Philip K.
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

typedef enum {
     NONE,
     BLACK,
     WHITE,
} Stone;

typedef struct Board	Board;
typedef struct Move	Move;
typedef struct Coord	Coord;
typedef struct Group	Group;

struct Board {
     uint8_t	width;
     uint8_t	height;
     uint16_t	black_captured;
     uint16_t	white_captured;
     Move	*history;
     bool       changed;
     Stone      next;
     Stone	board[];
};

struct Coord {
     uint8_t x, y;
};

struct Move {
     Stone	player;
     Coord	placed;
     bool       pass;
     bool       setup;          /* setup move cannot be undone */

     Move	*before;
     Move	**after;
     uint16_t	children;
     
     uint16_t	removed_n;
     Coord	removed[];
};

#define C(X, Y) ((struct Coord) { .x = (X), .y = (Y)})	    /* coord shorthand */
#define P(b, N) (C(((N) % (b)->width), ((N) / (b)->width))) /* index -> coord */
#define I(b, C) ((C).y * (b)->width + (C).x)		    /* coord -> index */
#define stone_at(b, c) (b->board[I(b, c)])
#define opposite(s) ((s) == BLACK ? WHITE : (s) == WHITE ? BLACK : (abort(), s))

Board		*make_board(uint8_t, uint8_t);
bool	        valid_move(Board *, Stone, Coord);
void            pass(Board*, Stone);
int16_t		place_stone(Board *, Stone, Coord);
uint16_t	player_points(Board *, Stone);
bool		undo_move(Board *);
void		board_free(Board *);

#endif
