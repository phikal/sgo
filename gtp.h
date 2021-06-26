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

enum Command {
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
};

enum Form {
     INVAL,
     NIHIL,
     INT,
     FLOAT,
     STRING,
     VERTEX,
     COLOR,
     MOVE,
     BOOL,
};

struct Vertex {
     enum {
	  VALID,
	  PASS,
	  RESIGN
     } type;
     struct Coord coord;
};

struct Obj {
     enum Form form;
     union {
	  uint32_t	 v_int;
	  float		 v_float;
	  char		*v_str;
	  struct Vertex	 v_vertex;
	  enum Stone	 v_color;
	  struct Obj	*v_list;
     } val;
     uint64_t		len;	/* used by LIST */
};

/* A callback processes and object with an error state.
 *
 * If the board was changed, it returns true. */
typedef bool (*callback)(struct Obj*, bool);

void gtp_run_command(struct Board *, enum Command, char *, callback);
void gtp_init(struct Board *);
void gtp_check_responses(void);
bool gtp_place_stone(struct Board *, enum Stone, struct Coord);
void gtp_pass(struct Board *, enum Stone);
     
#endif
