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

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#include <poll.h>

#include <xcb/xcb.h>

#include "board.h"
#include "state.h"
#include "gtp.h"
#include "ui.h"

#define LENGTH(a) (sizeof(a)/sizeof(*a))

#define MARGIN 16



static xcb_connection_t *conn;
static xcb_screen_t     *screen;
static xcb_window_t      win;
static xcb_drawable_t    draw;

static xcb_gcontext_t    gc_bg;
static xcb_gcontext_t    gc_grid;
static xcb_gcontext_t    gc_white;
static xcb_gcontext_t    gc_black;

static xcb_point_t       hover_pos;



void
ui_init(uint8_t height, uint8_t width)
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
               XCB_EVENT_MASK_KEY_PRESS,
          }));
     xcb_map_window(conn, win);

     xcb_change_property(conn,
                         XCB_PROP_MODE_REPLACE,
                         win,
                         XCB_ATOM_WM_NAME,
                         XCB_ATOM_STRING,
                         8,
                         strlen("sgo"),
                         "sgo");
}

static enum State
ui_draw(struct Board *b, enum State state, enum Stone self, bool manual)
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
          return state;
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
     xcb_clear_area(conn, 1, win, 0, 0, width, height);

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
               struct Coord c = P(b, i);
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
          struct Coord c = P(b, i);
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
          struct Coord c = P(b, i);
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
               assert(b->history->player == WHITE);
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
               assert(b->history->player == BLACK);
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

     b->changed = false;
     xcb_flush(conn);
     return state;
}

void
ui_loop(struct Board *b, enum State *state, enum Stone self, bool manual)
{
     struct pollfd fds[2] = {
          {
               .fd = STDIN_FILENO,
               .events = manual ? 0 : POLLIN | POLLERR,
          }, {
               .fd = xcb_get_file_descriptor(conn),
               .events = POLLIN | POLLERR,
          },
     };

     xcb_generic_event_t *event;
     xcb_timestamp_t last_pass = {0};
     int c;

     b->changed = true;
     for (;;) {
          fprintf(stderr, "changed: %d\n", b->changed);
          if (b->changed) {
               *state = ui_draw(b, *state, self, manual);
          }

          c = poll(fds, LENGTH(fds), 1000);
          fprintf(stderr, "poll() -> %d (%d)\n", c, errno);
          if (c == 0) {
               continue;
          } if (c == -1) {
               if (errno == EINTR || errno == EAGAIN) {
                    continue;
               }
               perror("poll");
               exit(EXIT_FAILURE);
          }

          /* check for input on stdin */
          if (fds[0].revents & POLLERR) {
               perror("poll");
               exit(EXIT_FAILURE);
          }
          if (fds[0].revents & POLLIN) {
               gtp_check_responses();
          }

          /* check for UI input */
          if (fds[1].revents & POLLERR) {
               perror("poll");
               exit(EXIT_FAILURE);
          }
          event = xcb_poll_for_event(conn);
          if (!(fds[1].revents & POLLIN)) {
               continue;
          }
          if (!event) {
               return;
          }

          switch (event->response_type & ~0x80) {
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
                    switch (*state) {
                    case QUERY_WHITE:
                         if (!manual && self != WHITE) {
                              break;
                         }
                         S1(CONFIRM_WHITE);
                         b->changed = true;
                         break;
                    case QUERY_BLACK:
                         if (!manual && self != BLACK) {
                              break;
                         }
                         S1(CONFIRM_BLACK);
                         b->changed = true;
                         break;
                    default:
                         ;
                    }
                    break;
               case XCB_BUTTON_MASK_2: /* pass/resign */
                    b->changed = true;
                    /* double click to resign, single click to pass */
                    if (press->time - last_pass < 200) {
                         switch (*state) {
                         case QUERY_BLACK:
                         case CONFIRM_BLACK:
                              if (manual) {
                                   S1(RESIGN_BLACK);
                              } else {
                                   S1(RESIGN_WHITE);
                              }
                              break;
                         case QUERY_WHITE:
                         case CONFIRM_WHITE:
                              if (manual) {
                                   S1(RESIGN_WHITE);
                              } else {
                                   S1(RESIGN_BLACK);
                              }
                              break;
                         default:
                              b->changed = false;
                         }
                    } else {
                         if (b && b->history && b->history->pass) {
                              S1(GAMEOVER);
                              break;
                         }

                         switch (*state) {
                         case QUERY_WHITE:
                              S1(PASS_WHITE);
                              break;
                         case QUERY_BLACK:
                              S1(PASS_BLACK);
                              break;
                         default:
                              ;
                         }
                    }
                    last_pass = press->time;
                    break;
               case XCB_BUTTON_MASK_3: /* undo */
                    if (undo_move(b)) {
                         switch (*state) {
                         case QUERY_WHITE:
                              if (!manual) {
                                   gtp_run_command(b, GENMOVE, "b", place_bot_stone);
                              }
                              S1(QUERY_BLACK);
                              break;
                         case QUERY_BLACK:
                              if (!manual) {
                                   gtp_run_command(b, GENMOVE, "w", place_bot_stone);
                              }
                              S1(QUERY_WHITE);
                              break;
                         default:
                              ;
                         }
                    }
                    break;
               }
          }
          }

          free(event);
     }
}

void
ui_cleanup()
{
     xcb_free_pixmap(conn, draw);
     xcb_disconnect(conn);
}
