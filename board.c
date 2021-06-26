/* Board logic
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"

#define LENGTH(a) ((unsigned) (sizeof(a)/sizeof(*a)))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define neighbours(c)                                   \
     {                                                  \
          C((c).x - 1,  (c).y),                         \
          C((c).x + 1,  (c).y),                         \
          C((c).x,      (c).y - 1),                     \
          C((c).x,      (c).y + 1)                      \
     }
#define invalid_coord(b, c)                             \
     ((c).x < 0 ||                                      \
      (c).y < 0 ||                                      \
      (c).x >= (b)->width ||                            \
      (c).y >= (b)->height)

/* Create and initialize board.
 *
 * Return non-NULL if successful, or NULL if an error occurs. Errno
 * will be set accordingly. */
Board *
make_board(uint8_t width, uint8_t height)
{
     /* uint16_t i; */
     Board *b;

     assert(0 == NONE);

     if (width < 2 || width > 25 || height < 2 || height > 25) {
          errno = EINVAL;
          return NULL;
     }

     b = calloc(1, sizeof(Board) + (sizeof(Stone) * width * height));
     if (b == NULL) {
          return NULL;
     }

     b->width = width;
     b->height = height;

     return b;
}

/* Count (pesido-)liberties of group containing Coord..
 *
 * For black or white stones, the number of liberties are
 * returned. For empty verteces, the size of the group is
 * calculated.
 *
 * If the third argument is non-nil, it should point to a array of
 * HEIGHT x WIDTH size. After count_liberties exits, it will mark all
 * indeces I (representing the coordinate P(b, I)) with true, if they
 * are part of the traversed group. If NULL, it will be ignored. */
static uint16_t
count_liberties(Board *b, Coord c, bool *group)
{
     bool visited[b->width * b->height];
     uint16_t stack[b->width * b->height];
     uint16_t ssize, start, i;
     uint16_t liberties = 0;

     /* mark all vertices as NOT visited */
     memset(visited, false, sizeof(visited));

     /* initialize "stack"  */
     ssize = 1;
     stack[0] = I(b, c);

     /* prepare group set */
     if (group) {
          memset(group, false, sizeof(bool) * (b->width * b->height));
          group[I(b, c)] = true;
     }

     visited[I(b, c)] = true;

     /* start tree search */
     while (ssize > 0) {
          start = ssize - 1;
          Coord nextto[4] = neighbours(P(b, stack[start]));

          /* consider all neighbours */
          for (i = 0; i < LENGTH(nextto); i++) {
               if (invalid_coord(b, nextto[i]) ||
                   visited[I(b, nextto[i])]) {
                    continue;
               }

               /* if a neighbouring vertex is empty, we have found
                * another liberty */
               if (stone_at(b, nextto[i]) == NONE) {
                    liberties++;
               }

               /* if the neighbouring vertex is of the stame type as
                * the current vertex, add it to the search stack. */
               if (stone_at(b, nextto[i]) == stone_at(b, c)) {
                    if (group) {
                         /* ... and if necessary mark it as part of
                          * the group. */
                         group[I(b, nextto[i])] = true;
                    }

                    assert(ssize < b->width * b->height);
                    stack[ssize++] = I(b, nextto[i]);
               }

               /* finally remember that this vertex has been visited */
               visited[I(b, nextto[i])] = true;
          }

          /* pop the the vertex that has just been searched of the
           * "stack" (this is done by exchanging the last element that
           * has been addeed with the element that has just been
           * search, and then reducing the stack size by one). */
          stack[start] = stack[--ssize];
     }

     return liberties;
}

/* check if STONE can be placed on COORD within BOARD. */
bool
valid_move(Board *b, Stone s, Coord c)
{
     bool group[b->height * b->width];
     uint16_t i, l;

     /* don't place a stone on a stone */
     if (stone_at(b, c) != NONE) {
          return false;
     }

     /* consider all neighbours */
     Coord nextto[4] = neighbours(c);
     for (i = 0; i < LENGTH(nextto); i++) {
          if (invalid_coord(b, nextto[i])) {
               continue;
          }

          /* if any neighbour is an empty vertex, the queries
           * coordinate is legal. */
          if (stone_at(b, nextto[i]) == NONE) {
               return true;
          }

          l = count_liberties(b, nextto[i], group);
          if (stone_at(b, nextto[i]) == s) {
               /* if a neighbour has the same color as the
                * to-be-placed stone, and it has more than one
                * liberty, the stone may be placed. */
               if (l > 1) {
                    return true;
               }
          } else {
               /* if a neighbouring group has only one liberty, they
                * depend on the current vertex (as it's necessarily
                * empty). Placing a stone here, will kill the group,
                * giving the current stone at least one liberty. */
               if (l == 1) {
                    /* check the ko rule */
                    if (b->history && b->history->removed_n == 1) {
                         /* if the last move only changed one stone,
                          * and this stone was placed in the last
                          * move, the current move is not legal. */
                         if (group[I(b, b->history->placed)]) {
                              return false;
                         }
                    }

                    return true;
               }
          }
     }

     return false;
}

/* Update BOARD after LAST_CHANGE */
static int16_t
update_board(Board *b, Coord last_change)
{
     bool group[b->width * b->height];
     bool visited[b->width * b->height];
     bool removed[b->width * b->height];
     uint16_t changed = 1, gsize = 0, i, j;
     Coord c;

     /* initially mark all vertices as NOT visited/changed */
     memset(visited, false, sizeof(visited));
     memset(removed, false, sizeof(removed));

     /* start search for empty fields from every vertex */
     for (i = 0; i < LENGTH(group); i++) {
          c = P(b, i);

          /* skip over visited empty and visited verteces */
          if (stone_at(b, c) == NONE || visited[i]) {
               continue;
          }

          /* check if group has no verteces */
          if (0 == count_liberties(b, c, group)) {

               /* ignore group if it just changed the board */
               if (group[I(b, last_change)]) {
                    goto mark;
               }

               /* count number of changed stones */
               gsize = 0;
               for (j = 0; j < LENGTH(group); j++) {
                    if (group[j]) {
                         gsize++;
                    }
               }

               /* write down changes in points */
               switch (stone_at(b, last_change)) {
               case WHITE:
                    b->black_captured += gsize;
                    break;
               case BLACK:
                    b->white_captured += gsize;
                    break;
               default:
                    ;
               }
               changed += gsize;

               for (j = 0; j < LENGTH(group); j++) {
                    if (group[j]) {
                         stone_at(b, P(b, j)) = NONE;
                         removed[j] = true;
                    }
               }
          }

     mark:
          /* mark verteces as visited */
          for (j = 0; j < LENGTH(group); j++) {
               visited[j] |= group[j];
          }
     }

     /* create new history object */
     Move *move = malloc(sizeof(Move) + sizeof(Coord) * changed);
     if (!move) {
          perror("calloc");
          abort();
     }

     *move = (struct Move) {
          .player = stone_at(b, last_change),
          .placed = last_change,
          .before = b->history,
          .removed_n = changed - 1,
     };

     /* mark changed stones */
     for (j = i = 0; i < LENGTH(removed); i++) {
          if (removed[i]) {
               assert(j < move->removed_n);
               move->removed[j++] = P(b, i);
          }
     }
     assert(j == move->removed_n);  /* changed = removed + (1) added */

     if (b->history)  {
          /* insert backlink */
          b->history->after = realloc(
               b->history->after,
               sizeof(Move) * (b->history->children + 1)
               );
          if (!b->history->after) {
               perror("realloc");
               abort();
          }
          b->history->after[b->history->children] = move;
          b->history->children += 1;
     }

     /* save last move */
     b->history = move;

     assert(changed < (1 << 15)); /* prevent overflow */

     return (int16_t) changed;
}

/* Undo last move on BOARD.
 *
 * If no undo was possible, return false. Otherwise return true. */
bool
undo_move(Board *b)
{
     Move *move = b->history;
     uint16_t i;

     /* if there is no predecesor, the history cannot be changed */
     if (!move || move->setup) {
          return false;
     }

     /* remove last placed stone */
     stone_at(b, move->placed) = NONE;

     /* add removed stones again */
     for (i = 0; i < move->removed_n; i++) {
          stone_at(b, move->removed[i]) = opposite(move->player);
     }

     /* save changed */
     b->history = move->before;

     /* update points */
     switch (move->player) {
     case WHITE:
          b->black_captured -= move->removed_n;
          break;
     case BLACK:
          b->white_captured -= move->removed_n;
          break;
     default:
          ;
     }

     b->changed = true;
     return true;
}

void
pass(Board *b, Stone s)
{
     Move *move = malloc(sizeof(Move));
     if (!move) {
          perror("malloc");
          abort();
     }

     *move = (struct Move) {
          .pass = true,
          .player = s,
          .before = b->history,
     };

     b->history = move;
}


/* Place STONE at COORD on BOARD.
 *
 * Return number of changed stones if the move was valid, otherwise
 * return a negative value. */
int16_t
place_stone(Board *b, Stone s, Coord c)
{
     if (!valid_move(b, s, c)) {
          return -1;
     }

     stone_at(b, c) = s;
     b->changed = true;

     return update_board(b, c);
}

/* Calculate points for player STONE on BOARD. */
uint16_t
player_points(Board *b, Stone s)
{
     bool visited[b->width * b->height];
     bool group[b->width * b->height];
     uint16_t p = 0, i, j, k, c;
     bool surrounded;

     assert(s == BLACK || s == WHITE);

     memset(visited, false, sizeof(visited));
     for (i = 0; i < b->width * b->height; i++) {
          /* don't double count areas */
          if (visited[i]) {
               continue;
          }

          /* only consider empty verteces */
          if (NONE != stone_at(b, P(b, i))) {
               continue;
          }

          /* count adjacent liberties to calculate the empty group */
          count_liberties(b, P(b, i), group);

          /* determine size of area and check if if any adjacant stone
           * is of the opposite color. */
          c = 0;
          surrounded = true;
          for (j = 0; j < LENGTH(group); j++) {
               if (group[j] && surrounded)  {
                    Coord nextto[4] = neighbours(P(b, j));

                    for (k = 0; k < LENGTH(nextto); k++) {
                         if (invalid_coord(b, nextto[k])) {
                              continue;
                         }

                         if (NONE == stone_at(b, nextto[k])) {
                              continue;
                         }

                         if (opposite(s) == stone_at(b, nextto[k])) {
                              surrounded = false;
                         }
                    }
               }

               if (group[j]) {
                    c++;
               }

               visited[j] |= group[j];
          }

          /* if the area is cleanly surrounded by STONE, add the size
           * of the area to the players points. */
          if (surrounded) {
               p += c;
          }
     }

     if (p == b->width * b->height) {
          return 0;
     }

     switch (s) {
          /* TODO: consider other scoring systems */
     case BLACK:
          return p + b->white_captured;
     case WHITE:
          return p + b->black_captured;
     default:
          return 0;             /* should not happen, see assert above */
     }
}

static void
move_free(Move *m)
{
     uint16_t i;

     /* the move should always exist */
     assert(m);

     /* free all children */
     for (i = 0; i < m->children; i++) {
          move_free(m->after[i]);
     }
     free(m->after);

     /* then free the move object */
     free(m);
}

void
board_free(Board *b)
{
     if (!b) {
          return;
     }

     /* find root history node */
     Move *m = b->history;
     if (m) {
          while (m->before) {
               m = m->before;
          }

          /* recursivly free moves */
          move_free(m);
     }

     /* free board itself */
     free(b);
}
