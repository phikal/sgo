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

#include <stdio.h>
#include <stdbool.h>

#include "board.h"

#ifndef GTP_H
#define GTP_H

typedef enum {
     PROTOCOL_VERSION,
     NAME,
     KNOWN_COMMAND,
     LIST_COMMANDS,
     QUIT,
     BOARDSIZE,
     CLEAR_BOARD,
     KOMI,
     PLAY,
     GENMOVE,
     UNDO,
     REG_GENMOVE,
} Command;

typedef enum {
     INVAL,
     NIHIL,
     INT,
     FLOAT,
     STRING,
     VERTEX,
     COLOR,
     MOVE,
     BOOL,
} Type;

typedef struct Obj Obj;
typedef struct Vertex Vertex;

struct Vertex {
     enum {
	  VALID,
	  PASS,
	  RESIGN
     } type;
     Coord coord;
};


struct Obj {
     Type type;
     union {
	  uint32_t	 v_int;
	  float		 v_float;
	  char		*v_str;
	  Vertex	 v_vertex;
	  Stone		 v_color;
	  Obj		*v_list;
     } val;
     uint64_t		len;	/* used by LIST */
};

/* A callback processes and object with an error state.
 *
 * If the board was changed, it returns true. */
typedef bool (*Callback)(Obj*, bool);

void gtp_run_command(Board *, Command, char *, Callback);
void gtp_init(Board *);
void gtp_check_responses(void);
bool gtp_place_stone(Board *, Stone, Coord);
void gtp_pass(Board *, Stone);
     
#endif
