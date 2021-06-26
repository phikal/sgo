/* Simple Go client
 *
 * Copyright 2020-2021 Philip Kaludercic
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

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "gtp.h"
#include "state.h"
#include "ui.h"



#define LENGTH(a) (sizeof(a)/sizeof(*a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MARGIN 16



static enum Stone self;
static struct Board *active_board;
static enum State state = QUERY_BLACK;
static bool manual;
bool verbose;
bool debug;



__attribute__ ((noreturn))
static void
usage(char *argv0)
{
     fprintf(stderr, "usage: %s -m -s [WxH]\n", argv0);
     exit(EXIT_SUCCESS);
}

bool
place_bot_stone(struct Obj *o, bool error)
{
     if (error) {
          if (!strcmp(o->val.v_str, "invalid move\n")) {
               undo_move(active_board);
          }
          switch (state) {
          case QUERY_WHITE:
               S(QUERY_WHITE);
               break;
          case QUERY_BLACK:
               S(QUERY_BLACK);
               break;
          default:
               ;
          }
          return false;
     }

     assert(o->form == VERTEX);
     assert(state == QUERY_WHITE || state == QUERY_BLACK);

     switch (o->val.v_vertex.type) {
     case RESIGN:
          switch (state) {
          case QUERY_WHITE:
               S(RESIGN_BLACK);
               return true;
          case QUERY_BLACK:
               S(RESIGN_WHITE);
               return true;
          default:
               return false;
          }
     case PASS:
          switch (state) {
          case QUERY_WHITE:
               pass(active_board, BLACK);
               S(QUERY_WHITE);
               return true;
          case QUERY_BLACK:
               pass(active_board, WHITE);
               S(QUERY_BLACK);
               return true;
          default:
               return false;
          }
     default:
          ;
     }

     if (verbose) {
          fprintf(stderr, "%s bot placing stone at (%d, %d)\n",
                  state == QUERY_WHITE ? "white" : "black",
                  o->val.v_vertex.coord.x,
                  o->val.v_vertex.coord.y);
     }

     switch (state) {
     case QUERY_WHITE:
          place_stone(active_board, WHITE, o->val.v_vertex.coord);
          S(QUERY_BLACK);
          return true;
     case QUERY_BLACK:
          place_stone(active_board, BLACK, o->val.v_vertex.coord);
          S(QUERY_WHITE);
          return true;
     default:
          return false;
     }
}

static void
cleanup(void)
{
     /* terminate engine */
     board_free(active_board);
     ui_cleanup();
}



int
main(int argc, char *argv[])
{
     uint8_t height = 9, width = 9;

     for (;;) {
          switch (getopt(argc, argv, "vmDs:i:o:c:")) {
          case 's':             /* size */
               if (!sscanf(optarg, "%hhux%hhu", &height, &width)) {
                    fputs("cannot parse size\n", stderr);
                    return EXIT_FAILURE;
               }
               break;
          case 'm':             /* manual game (not bot) */
               manual = true;
               break;
          case 'c':             /* stone coolr */
               switch (optarg[0]) {
               case 'b': case 'B':
                    self = BLACK;
                    break;
               case 'w': case 'W':
                    self = WHITE;
                    break;
               default:
                    fprintf(stderr, "unknown color\n");
                    exit(EXIT_FAILURE);
               }
               break;
          case 'v':
               verbose = true;
               break;
          case 'D':
               debug = true;
               break;
          case -1:
               goto init;
          default: /* '?' */
               usage(argv[0]);
          }
     }

init:
     ui_init(height, width);
     active_board = make_board(height, width);
     if (self == WHITE) {
          state = QUERY_WHITE;
     } else {
          state = QUERY_BLACK;
     }
     if (!manual) {
          gtp_init(active_board);

          /* If the user is white, we have to ask the engine to
           * generate the first move. */
          if (self == WHITE) {
               gtp_run_command(active_board, GENMOVE, "w", place_bot_stone);
          }
     }
     ui_loop(active_board, &state, self, manual);
     cleanup();

     return EXIT_SUCCESS;
}
