/* Simple Go client
 *
 * Copyright 2020 Philip K.
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
#include <unistd.h>

#include <sys/select.h>

#include <xcb/xcb.h>

#include "board.h"
#include "gtp.h"



#define LENGTH(a) (sizeof(a)/sizeof(*a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MARGIN 16



static xcb_connection_t *conn;
static xcb_screen_t     *screen;
static xcb_window_t      win;
static xcb_drawable_t    draw;

static xcb_gcontext_t    gc_bg;
static xcb_gcontext_t    gc_grid;
static xcb_gcontext_t    gc_white;
static xcb_gcontext_t    gc_black;

static Stone             self;
static Board            *active_board;
static xcb_point_t       hover_pos;

static bool              manual;
bool                     verbose;
bool                     debug;




static enum State {
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
} state = QUERY_BLACK;

#define OK(state) [state] = true

/* To ensure that all state transitions are valid, this table is used
 * to check if the current state may transition into a given next one
 * (see S macro below) */
static bool valid_transition[11][11] = {
     [CONFIRM_BLACK] = {
          OK(QUERY_WHITE),      /* continue */
          OK(QUERY_BLACK),      /* invalid move */
     }, [CONFIRM_WHITE] = {
          OK(QUERY_BLACK),      /* continue */
          OK(QUERY_WHITE),      /* invalid move */
     },
     [QUERY_BLACK] = {
          OK(QUERY_BLACK),      /* noop */
          OK(QUERY_WHITE),      /* undo */
          OK(CONFIRM_BLACK),    /* button 1 */
          OK(PASS_BLACK),       /* button 2 */
          OK(RESIGN_BLACK),     /* 2x button 2 */
          OK(GAMEOVER)          /* mark game as over */
     },
     [QUERY_WHITE] = {
          OK(QUERY_WHITE),      /* noop */
          OK(QUERY_BLACK),      /* undo */
          OK(CONFIRM_WHITE),    /* button 1 */
          OK(PASS_WHITE),       /* button 2 */
          OK(RESIGN_WHITE),     /* 2x button 2 */
          OK(GAMEOVER)          /* mark game as over */
     },
     [PASS_BLACK] = {
          OK(QUERY_WHITE),      /* continue */
     },
     [PASS_WHITE] = {
          OK(QUERY_BLACK),      /* continue  */
     },
     [RESIGN_BLACK] = {
          OK(GAMEOVER)          /* mark game as over */
     },
     [RESIGN_WHITE] = {
          OK(GAMEOVER)          /* mark game as over */
     },
     [GAMEOVER] = {
          OK(GAMEOVER),
     },
};

/* state transition macro */
#define S(nstate)                                       \
     do {                                               \
          assert(valid_transition[state][nstate]);      \
          state = ((nstate));                           \
     } while (0)



__attribute__ ((noreturn))
static void
usage(char *argv0)
{
     fprintf(stderr, "usage: %s -m -s [WxH]\n", argv0);
     exit(EXIT_SUCCESS);
}

static bool
place_bot_stone(Obj *o, bool error)
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

     assert(o->type == VERTEX);
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
draw_board(Board *b, bool redraw)
{
     char status[256];
     uint32_t height, width, i, n, step, pad_x, pad_y, dist;
     xcb_segment_t grid[b->width + b->height];
     xcb_arc_t stones[b->width * b->height];
     xcb_get_geometry_cookie_t cookie;
     xcb_get_geometry_reply_t *geom;
     xcb_generic_error_t *xcb_error;

     cookie     = xcb_get_geometry(conn, win);
     geom       = xcb_get_geometry_reply(conn, cookie, &xcb_error);
     if (!geom) {
          /* the window has been closed or can otherwise not be
           * found. */
          return;
     }

     height     = geom->height - MARGIN;
     width      = geom->width;

     free(geom);

     /* calculate padding and grid distance */
     if (width < height) {      /* narrow */
          step  = (width + 2) / (b->width + 1);
          pad_x = step;
          pad_y = step + (height - width) / 2;
     } else {                   /* wide */
          step  = (height + 2) / (b->height + 1);
          pad_x = step + (width - height) / 2;
          pad_y = step;
     }

     /* clear area if a redraw was requested */
     if (redraw) {
          xcb_clear_area(conn, 1, win, 0, 0, width, height);
     }

     /* calculate horizontal lines */
     for (i = 0; i < b->width; i++) {
          uint16_t x = pad_x + i * step;
          grid[i] = (xcb_segment_t) {
               .x1 = x, .y1 = pad_y,
               .x2 = x, .y2 = pad_y + (b->height-1) * step,
          };
     }
     /* calculate vertical lines */
     for (n = i, i = 0 ; i < b->height; i++) {
          uint16_t y = pad_y + i * step;
          grid[n + i] = (xcb_segment_t) {
               .x1 = pad_x, .y1 = y,
               .x2 = pad_x + (b->width-1) * step, .y2 = y,
          };
     }

     /* draw all lines */
     xcb_poly_segment(conn, win, gc_grid, LENGTH(grid), grid);

     /* draw hint and place stone */
     if (hover_pos.x != 0 && hover_pos.x != 0) {
          for (dist = ~0, n = i = 0; i < b->height * b->width; i++) {
               Coord c = P(b, i);
               uint32_t x, y;
               x  = pad_x + c.x * step + 1;
               x -= hover_pos.x;
               y  = pad_y + c.y * step + 1;
               y -= hover_pos.y;

               if (dist > x * x + y * y) {
                    n = i;
                    dist = x * x + y * y;
               }
          }

          switch (state) {
          case PASS_BLACK:
               if (manual) {
                    pass(b, BLACK);
               } else {
                    gtp_pass(b, BLACK);
               }
               S(QUERY_WHITE);
               break;
          case PASS_WHITE:
               if (manual) {
                    pass(b, WHITE);
               } else {
                    gtp_pass(b, WHITE);
               }
               S(QUERY_BLACK);
               break;
          case CONFIRM_BLACK:
               if (manual) {
                    if (place_stone(b, BLACK, P(b, n)) >= 0) {
                         S(QUERY_WHITE);
                    } else {
                         S(QUERY_BLACK);
                    }
               } else {
                    if (gtp_place_stone(b, BLACK, P(b, n))) {
                         gtp_run_command(b, GENMOVE, "w", place_bot_stone);
                         S(QUERY_WHITE);
                    }
               }
               break;
          case CONFIRM_WHITE:
               if (manual) {
                    if (place_stone(b, WHITE, P(b, n)) >= 0) {
                         S(QUERY_BLACK);
                    } else {
                         S(QUERY_WHITE);
                    }
               } else {
                    if (gtp_place_stone(b, WHITE, P(b, n))) {
                         gtp_run_command(b, GENMOVE, "b", place_bot_stone);
                         S(QUERY_BLACK);
                    }
               }
               break;
          default:
               ;
          }
     }

     /* draw black stones */
     for (n = i = 0; i < b->height * b->width; i++) {
          Coord c = P(b, i);
          if (stone_at(b, c) == BLACK) {
               stones[n++] = (xcb_arc_t) {
                    .x = pad_x - step / 2 + c.x * step + 1,
                    .y = pad_y - step / 2 + c.y * step + 1,
                    .width = step - 2,
                    .height = step - 2,
                    .angle1 = 0,
                    .angle2 = (360 << 6),
               };
          }
     }
     xcb_poly_fill_arc(conn, win, gc_black, n, stones);

     /* draw white stones */
     for (n = i = 0; i < b->height * b->width; i++) {
          Coord c = P(b, i);
          if (stone_at(b, c) == WHITE) {
               stones[n++] = (xcb_arc_t) {
                    .x = pad_x - step / 2 + c.x * step + 1,
                    .y = pad_y - step / 2 + c.y * step + 1,
                    .width = step - 2,
                    .height = step - 2,
                    .angle1 = 0,
                    .angle2 = (360 << 6),
               };
          }
     }
     xcb_poly_fill_arc(conn, win, gc_white, n, stones);
     xcb_poly_arc(conn, win, gc_black, n, stones);

     switch (state) {
     case CONFIRM_BLACK:
          snprintf(status, sizeof(status), "black has played.");
          break;
     case CONFIRM_WHITE:
          snprintf(status, sizeof(status), "white has played.");
          break;
     case QUERY_BLACK:
          if (!manual && self != BLACK) {
               snprintf(status, sizeof(status), "waiting for black");
          } else {
               snprintf(status, sizeof(status), "black to play");
          }
          if (b->history) {
               char update[256] = {0};
               /* assert(b->history->player == WHITE); */
               if (b->history->pass) {
                    strncat(update, " (white passed)",
                            sizeof(update) - 1);
               } else {
                    uint8_t x, y;
                    x = b->history->placed.x;
                    y = b->history->placed.y;
                    snprintf(update, sizeof(update),
                             " (last move %c%u, removed %u)",
                             'a' + (x) + ('a' + (x) < 'i' ? 0 : 1),
                             b->height - y,
                             b->history->removed_n);
               }
               strncat(status, update, sizeof(status) - 1);
          }
          break;
     case QUERY_WHITE:
          if (!manual && self != WHITE) {
               snprintf(status, sizeof(status), "waiting for white");
          } else {
               snprintf(status, sizeof(status), "white to play");
          }
          if (b->history) {
               char update[256] = {0};
               /* assert(b->history->player == BLACK); */
               if (b->history->pass) {
                    strncat(update, " (black passed)",
                            sizeof(update) - 1);
               } else {
                    uint8_t x, y;
                    x = b->history->placed.x;
                    y = b->history->placed.y;
                    snprintf(update, sizeof(update),
                             " (last move %c%u, removed %u)",
                             'a' + x + ('a' + (x) < 'i' ? 0 : 1),
                             b->height - y,
                             b->history->removed_n);
               }
               strncat(status, update, sizeof(status) - 1);
          }
          break;
     case RESIGN_BLACK:
          snprintf(status, sizeof(status), "black resigned.");
          break;
     case RESIGN_WHITE:
          snprintf(status, sizeof(status), "white resigned.");
          break;
     case GAMEOVER: {
          uint16_t black = player_points(b, BLACK);
          uint16_t white = player_points(b, WHITE);

          if (black > white) {
               snprintf(status, sizeof(status), "black wins! (B+%d)",
                        black - white);
          } else if (black < white) {
               snprintf(status, sizeof(status), "white wins! (W+%d)",
                        white - black);
          } else {
               snprintf(status, sizeof(status), "it's a tie.");
          }
     }
          break;
     default:
          abort();
          break;
     }

     xcb_rectangle_t bar = {
          .x = 0,
          .y = height,
          .width = width,
          .height = MARGIN,
     };
     xcb_poly_fill_rectangle(conn, win, gc_black, 1, &bar);
     xcb_image_text_8(conn, strnlen(status, sizeof(status)),
                      win, gc_white,
                      MARGIN / 4,
                      height + MARGIN - MARGIN / 4,
                      status);

     xcb_flush(conn);
}



static void
setup(uint8_t width, uint8_t height)
{
     const xcb_setup_t *setup;

     /* initialize connection and screen  */
     conn = xcb_connect(NULL, NULL);
     setup = xcb_get_setup(conn);
     screen = xcb_setup_roots_iterator(setup).data;

     /* create pixmap */
     draw = xcb_generate_id(conn);
     xcb_create_pixmap(conn, screen->root_depth,
                       draw, screen->root,
                       MARGIN * width,
                       MARGIN * height + MARGIN);

     /* graphic context for background */
     gc_bg = xcb_generate_id(conn);
     xcb_create_gc(conn, gc_bg, draw,
                   XCB_GC_BACKGROUND,
                   ((uint32_t[]) {
                        screen->white_pixel,
                   }));

     /* graphic context for grid */
     gc_grid = xcb_generate_id(conn);
     xcb_create_gc(conn, gc_grid, draw,
                   XCB_GC_LINE_WIDTH | XCB_GC_FOREGROUND,
                   ((uint32_t[]) {
                        screen->black_pixel,
                        1,
                   }));

     /* graphic context for black stones */
     gc_grid = xcb_generate_id(conn);
     xcb_create_gc(conn, gc_grid, draw,
                   XCB_GC_BACKGROUND,
                   ((uint32_t[]) {
                        screen->black_pixel,
                   }));

     gc_black = xcb_generate_id(conn);
     xcb_create_gc(conn, gc_black, draw,
                   XCB_GC_LINE_WIDTH | XCB_GC_FOREGROUND,
                   ((uint32_t[]) {
                        screen->black_pixel,
                        1,
                   }));

     /* graphic context for white stones */
     gc_white = xcb_generate_id(conn);
     xcb_create_gc(conn, gc_white, draw,
                   XCB_GC_BACKGROUND,
                   ((uint32_t[]) {
                        screen->black_pixel,
                   }));

     gc_white = xcb_generate_id(conn);
     xcb_create_gc(conn, gc_white, draw,
                   XCB_GC_LINE_WIDTH | XCB_GC_FOREGROUND,
                   ((uint32_t[]) {
                        screen->white_pixel,
                        1,
                   }));

     /* create window */
     win = xcb_generate_id(conn);
     xcb_create_window(
          conn,
          screen->root_depth,
          win,
          screen->root,
          0, 0,
          256, 256,
          16,
          XCB_WINDOW_CLASS_INPUT_OUTPUT,
          screen->root_visual,
          XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
          ((uint32_t[]) {
               screen->white_pixel,
               XCB_EVENT_MASK_BUTTON_RELEASE |
               XCB_EVENT_MASK_BUTTON_PRESS |
               XCB_EVENT_MASK_KEY_PRESS |
               XCB_EVENT_MASK_EXPOSURE,
          }));
     xcb_map_window(conn, win);

     active_board = make_board(height, width);
     if (active_board == NULL) {
          perror("make_board");
          exit(EXIT_FAILURE);
     }

    xcb_change_property(conn,
        XCB_PROP_MODE_REPLACE,
        win,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        strlen("sgo"),
        "sgo");
}

static void
loop()
{
     fd_set set;
     int xfd, c;
     xcb_generic_event_t *event;
     xcb_timestamp_t last_pass = {0};

     xfd = xcb_get_file_descriptor(conn);
     active_board->changed = true;
     for (;;) {
          if (active_board->changed) {
               draw_board(active_board, true);
          }

          active_board->changed = false;
          if (!manual) {
               gtp_check_responses();
          }

          FD_ZERO(&set);
          FD_SET(xfd, &set);

          c = select(xfd + 1,
                     &set, NULL, NULL,
                     &(struct timeval) { .tv_usec = 100000 });
          if (c == 0) {
               continue;
          } if (c < 0) {
               if (errno == EINTR) {
                    continue;
               }
               perror("pselect");
               abort();
          }

          event =  xcb_poll_for_event(conn);
          if (!event) {
               break;
          }

          switch (event->response_type & ~0x80) {
          case XCB_EXPOSE:
               active_board->changed = true;
               break;
          case XCB_BUTTON_RELEASE: {
               xcb_button_press_event_t *press = (xcb_button_press_event_t *) event;

               hover_pos = (xcb_point_t) {
                    .x = press->event_x,
                    .y = press->event_y,
               };

               switch (press->state & (XCB_BUTTON_MASK_1 |
                                       XCB_BUTTON_MASK_2 |
                                       XCB_BUTTON_MASK_3)) {
               case XCB_BUTTON_MASK_1: /* move */
                    switch (state) {
                    case QUERY_WHITE:
                         if (!manual && self != WHITE) {
                              break;
                         }
                         S(CONFIRM_WHITE);
                         active_board->changed = true;
                         break;
                    case QUERY_BLACK:
                         if (!manual && self != BLACK) {
                              break;
                         }
                         S(CONFIRM_BLACK);
                         active_board->changed = true;
                         break;
                    default:
                         ;
                    }
                    break;
               case XCB_BUTTON_MASK_2: /* pass/resign */
                    active_board->changed = true;
                    /* double click to resign, single click to pass */
                    if (press->time - last_pass < 200) {
                         switch (state) {
                         case QUERY_BLACK:
                         case CONFIRM_BLACK:
                              if (manual) {
                                   S(RESIGN_BLACK);
                              } else {
                                   S(RESIGN_WHITE);
                              }
                              break;
                         case QUERY_WHITE:
                         case CONFIRM_WHITE:
                              if (manual) {
                                   S(RESIGN_WHITE);
                              } else {
                                   S(RESIGN_BLACK);
                              }
                              break;
                         default:
                              active_board->changed = false;
                         }
                    } else {
                         if (active_board &&
                             active_board->history &&
                             active_board->history->pass) {
                              S(GAMEOVER);
                              break;
                         }

                         switch (state) {
                         case QUERY_WHITE:
                              S(PASS_WHITE);
                              break;
                         case QUERY_BLACK:
                              S(PASS_BLACK);
                              break;
                         default:
                              ;
                         }
                    }
                    last_pass = press->time;
                    break;
               case XCB_BUTTON_MASK_3: /* undo */
                    if (undo_move(active_board)) {
                         switch (state) {
                         case QUERY_WHITE:
                              if (!manual) {
                                   gtp_run_command(active_board, GENMOVE,
                                                   "b", place_bot_stone);
                              }
                              S(QUERY_BLACK);
                              break;
                         case QUERY_BLACK:
                              if (!manual) {
                                   gtp_run_command(active_board, GENMOVE,
                                                   "w", place_bot_stone);
                              }
                              S(QUERY_WHITE);
                              break;
                         default:
                              ;
                         }
                         if (!manual) {
                         }
                    } else {

                    }
                    break;
               }
          }
          }

          free(event);
     }
}

static void
cleanup()
{
     /* terminate engine */
     board_free(active_board);
     xcb_free_pixmap(conn, draw);
     xcb_disconnect(conn);
}



int
main(int argc, char *argv[])
{
     Stone start = BLACK;
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
               manual= true;
               break;
          case 'c':             /* stone coolr */
               switch (optarg[0]) {
               case 'b': case 'B':
                    start = BLACK;
                    break;
               case 'w': case 'W':
                    start = WHITE;
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
     setup(height, width);
     if (start == WHITE) {
          state = QUERY_WHITE;
          self = WHITE;
     } else {
          self = BLACK;
     }
     if (!manual) {
          gtp_init(active_board);
          if (start == WHITE) {
               gtp_run_command(active_board, GENMOVE, "w", place_bot_stone);
          }
     }
     loop();
     cleanup();

     return EXIT_SUCCESS;
}
