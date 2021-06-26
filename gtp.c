/* Go text protocol implementation for sgo
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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "gtp.h"

#define gtp_ensure(cond, msg) do if (!(cond)) gtp_error(msg); while (0)

extern bool verbose;
extern bool debug;

static struct Query {
     uint32_t id;
     Command cmd;
     Callback cb;
     Board *b;
     struct Query *next;
} *queries = NULL;

static struct Response {
     uint32_t id;
     bool error;
     char *resp;
     ssize_t len;
     struct Response *next;
} *responses = NULL;

__attribute__ ((noreturn))
static void
gtp_error(char *fmt, ...)
{
     va_list ap;

     va_start(ap, fmt);
     fprintf(stderr, fmt, ap);
     va_end(ap);
     fputs("\n", stderr);
#ifdef NDEBUG
     exit(EXIT_FAILURE);
#else
     abort();
#endif
}

static void
gtp_log(char *fmt, ...)
{
     va_list ap;

     if (!debug) {
          return;
     }

     va_start(ap, fmt);
     fprintf(stderr, fmt, ap);
     va_end(ap);
     fputs("\n", stderr);
}



static bool
gtp_ensure_version(Obj *o, bool error)
{
     assert(!error);
     assert(o->type == INT);
     gtp_ensure(o->val.v_int == 2, "invalid protocol version");

     return false;
}

static bool
gtp_check_name(Obj *o, bool error)
{
     char *c;

     assert(!error);
     assert(o->type == STRING);

     c = strchr(o->val.v_str, '\n');
     if (c) {
          *c = '\0';
     }

     fprintf(stderr, "connected to \"%s\"\n", o->val.v_str);

     return false;
}

/* prepare everything required for GTP communication. */
void
gtp_init(Board *b)
{
     assert(b->width >= 2 && b->width <= 25);
     assert(b->height >= 2 && b->height <= 25);

     /* enable asyncrhonous I/O on stdin */
     int status;
     if ((status = fcntl(STDIN_FILENO, F_GETFL)) < 0) {
          perror("fcntl");
          exit(EXIT_FAILURE);
     }
     if (fcntl(STDIN_FILENO , F_SETFL, status | O_NONBLOCK) < 0) {
          perror("fcntl");
          exit(EXIT_FAILURE);
     }

     /* ensure square board */
     if (b->width != b->height) {
          gtp_error("playing against a bot requiers a square board");
     }

     /* ensure correct protocl version */
     gtp_run_command(b, PROTOCOL_VERSION, NULL,
                     gtp_ensure_version);

     /* adjust board size */
     char param[4];
     assert(b->width < 25);
     sprintf(param, "%d", b->width);
     gtp_run_command(b, BOARDSIZE, param, NULL);

     if (verbose) {
          gtp_run_command(b, NAME, NULL, gtp_check_name);
     }
}



void
gtp_pass(Board *b, Stone s)
{
     assert(b != NULL);
     assert(s == BLACK || s == WHITE);

     pass(b, s);
     gtp_run_command(b, PLAY,
                     s == BLACK
                     ? "b pass"
                     : "w pass",
                     NULL);

}

/* place a STONE at COORD on BOARD and tell the engine */
bool
gtp_place_stone(Board *b, Stone s, Coord c)
{
     char param[1 + 1 + 1 + 4 + 1]; /* eg. "b a15" */

     assert(b != NULL);
     assert(s == BLACK || s == WHITE);
     assert(c.x < b->width);
     assert(c.y < b->height);

     snprintf(param, sizeof(param), "%c %c%d",
	      s == BLACK ? 'b' : 'w',
	      'a' + (c.x) + !('a' + (c.x) < 'i'),
	      b->height - c.y);

     if (place_stone(b, s, c) >= 0) {
	  gtp_run_command(b, PLAY, param, NULL);
	  return true;
     }
     
     return false;
}



static bool
gtp_handle_respose(struct Query *q, struct Response *r)
{
     static const Type types[] = {
          [PROTOCOL_VERSION]	= INT,
          [NAME]		= STRING,
          [QUIT]		= NIHIL,
          [BOARDSIZE]		= NIHIL,
          [CLEAR_BOARD]		= NIHIL,
          [KOMI]		= NIHIL,
          [PLAY]		= NIHIL,
          [GENMOVE]		= VERTEX,
          [UNDO]		= NIHIL,
          [REG_GENMOVE]		= VERTEX,
     };

     Obj obj = { .type = types[q->cmd] };
     ssize_t i;

     if (r->error) {
          obj.type = INVAL;
          obj.val.v_str = r->resp;
          return q->cb && q->cb(&obj, true);
     }

     switch (obj.type) {
     case INT:
          if (sscanf(r->resp, "%u", &obj.val.v_int) < 1) {
               gtp_log("invalid int (%s)", r->resp);
               return false;
          }
          break;
     case FLOAT:
          if (sscanf(r->resp, "%f", &obj.val.v_float) < 1) {
               gtp_log("invalid float (%s)", r->resp);
               return false;
          }
          break;
     case STRING:
          obj.val.v_str = r->resp;
          break;
     case VERTEX: {
          char token[r->len];
          memset(token, 0, sizeof token);
          sscanf(r->resp, "%s", token); /* chomp whitespaces */

          for (i = 0; i < r->len; i++) {
               token[i] = (char) tolower(token[i]);
          }

          if (strcmp("pass", token) == 0) {
               obj.val.v_vertex.type = PASS;
          } else if (strcmp("resign", token) == 0) {
               obj.val.v_vertex.type = RESIGN;
          } else {
               int j;
               uint8_t x, y;
               if ((j = sscanf(token, "%c%hhu", &x, &y)) < 2) {
                    gtp_log("invalid vertex (%s)", token);
                    return false;
               }

               obj.val.v_vertex.type = VALID;
               obj.val.v_vertex.coord = C(
                    /* X axis starts with 'a', goes until 'I', skips
                     * 'J', and continues until 'Z'. */
                    (x - 'a') + (x <= 'i' ? 0 : 1),
                    /* Y axis starts with 19, and goes down to 1. */
                    q->b->height - y
                    );

               if ((obj.val.v_vertex.coord.x >= q->b->width) ||
                   (obj.val.v_vertex.coord.y >= q->b->height)) {
                    gtp_log("vertex out of bounds (<%s>: %d, %d)",
                            token,
                            obj.val.v_vertex.coord.x,
                            obj.val.v_vertex.coord.y);
                    return false;
               }
          }
     }
          break;
     case NIHIL:
          return false;
     default:
          gtp_error("type handling not implemented");
     }

     return q->cb && q->cb(&obj, false);
}

void
gtp_check_responses(void)
{
     static int64_t id = -1;
     static bool error;
     static enum {
          NORMAL,               /* between commands. expect a "=" or "?" */
          ERROR,                /* command could not be parsed,  */
          PRE_ID,
          IN_ID,
          PRE_RESPONSE,
          IN_RESPONSE,
          NEWLINE,
          IN_COMMENT,
     } state = NORMAL;

     static size_t cap = 0, len = 0;
     static char *resp = NULL, last = '\0';

     struct Query *q, *q1;
     struct Response *r, *r1;

     char buf[BUFSIZ + 1];
     size_t j;
     int i;
     buf[0] = last;

     do {
          /* attempt to read data from standard input */
          i = read(STDIN_FILENO, buf + 1, sizeof(buf) - 1);

          if (i <= 0) {
               if (i == 0) {         /* end of file */
                    fputs("unexpected end of file\n", stderr);
                    exit(EXIT_FAILURE);
               }

               switch (errno) {
               case EAGAIN:          /* no input */
                    goto cross_ref;
               default:              /* unknown error */
                    exit(EXIT_FAILURE);
               }
          }
 
          /* process parsed data */
          for (j = 1; j < (size_t) i + 1; j++) {
               switch (buf[j]) { /* preprocessing */
                    
                    /* "Convert all occurences of HT to SPACE." */
               case '\t':       
                    buf[j] = ' ';
                    break;
                    /* "For each line with a hash sign (#), remove all
                     * text following and including this
                     * character."  */
                    
               case '#':
                    state = IN_COMMENT;
                    break;

                    /* "Remove all occurences of CR and other control
                     * characters except for HT and LF." */
               case 0:  case 1:  case 2:  case 3:
               case 4:  case 5:  case 6:  case 7:
               case 8:  case 11: case 12: case 13:
               case 14: case 15: case 16: case 17:
               case 18: case 19: case 20: case 21:
               case 22: case 23: case 24: case 25:
               case 26: case 27: case 28: case 29:
               case 30: case 31:
                    buf[j] = buf[j - 1];
                    continue;
               }

               switch (state) {
               case NORMAL:
                    switch (buf[j]) {
                    case '=':
                         error = false;
                         state = PRE_ID;
                         break;
                    case '?':
                         error = true;
                         state = PRE_ID;
                         break;
                    case ' ':
                         break;
                    case '\n':
                         state = NEWLINE;
                         break;
                    default:
                         state = ERROR;
                         break;
                    }
                    break;
               case PRE_ID:
                    if (buf[j] == '\n') {
                          /* command without ID */
                         state = NORMAL;
                    } else if (isspace(buf[j])) {
                         /* continue */
                    } else if (isdigit(buf[j])) {
                         state = IN_ID;
                         j--;
                    } else {
                         state = ERROR;
                    }
                    break;
               case IN_ID:
                    if (isdigit(buf[j])) {
                         if (id < 0) {
                              id = 0;
                         } else {
                              id *= 10;
                         }
                         id += buf[j] - '0';
                    } else if (buf[j] == '\n') {
                         state = NEWLINE;
                    } else if (isspace(buf[j])) {
                         state = PRE_RESPONSE;
                    } else {
                         state = ERROR;
                    }
                    break;
               case PRE_RESPONSE:
                    if (buf[j] == '\n') {
                         if (buf[j - 1] == '\n') {
                              state = NORMAL;
                         } else {
                              state = NEWLINE;
                         }
                    } else if (!isspace(buf[j])) {
                         state = IN_RESPONSE;
                         j--;
                    }
                    break;
               case IN_RESPONSE:
                    if (buf[j] == '\n') {
                         state = NEWLINE;
                    }

                    assert(len <= cap);
                    if (len == cap) {
                         resp = realloc(resp, cap + 64);
                         if (!resp) {
                              perror("realloc");
                              exit(EXIT_FAILURE);
                         }
                         memset(resp + cap, 0, 64);
                         cap += 64;
                    }
                    resp[len] = buf[j];
                    len++;
                    break;
               case NEWLINE:
                    if (buf[j] == '\n') {
                         struct Response *r;

                         state = NORMAL;

                         if (id < 0) {
                              break;
                         }

                         r = malloc(sizeof(struct Response));
                         if (!r) {
                              perror("malloc");
                              exit(EXIT_FAILURE);
                         }

                         if (len == cap) {
                              resp = realloc(resp, cap + 1);
                              if (!resp) {
                                   perror("realloc");
                                   exit(EXIT_FAILURE);
                              }
                         }
                         resp[len++] = '\0';

                         *r = (struct Response) {
                              .id = id,
                              .error = error,
                              .resp = resp,
                              .len = len,
                              .next = responses,
                         };
                         responses = r;

                         id = -1;
                         len = cap = 0;
                         resp = NULL;
                    } else {
                         state = IN_RESPONSE;
                    }
                    break;
               case IN_COMMENT:
                    if (buf[j] == '\n') {
                         state = NEWLINE;
                    }
                    break;
               case ERROR:
                    /* continue until two consecutive newlines are
                     * reached */
                    if (buf[j] == '\n' && buf[j - 1] == '\n') {
                         id = -1;
                         len = cap = 0;
                         free(resp);
                         resp = NULL;

                         state = NORMAL;
                         break;
                    }
               }

               last = buf[j];
          }
     } while (i == sizeof(buf));

cross_ref:
     /* after checking the new responses, attempt to cross-reference
      * them with the queries. */

     /* XXX: sometimes this loop doesn't terminate, becasue the list
      * has a back-reference or segfaults becasuse q is assigned an
      * invalid (usually large) pointer. */
     for (q1 = NULL, q = queries; q; q1 = q, q = q->next) {
          for (r1 = NULL, r = responses; r; r1 = r, r = r->next) {
               if (q->id == r->id) {
                    /* when the query ID matches the response ID,
                     * parse the response and call the callback
                     * function. */
                    q->b->changed |= gtp_handle_respose(q, r);

                    /* remove response object */
                    if (r1) {
                         r1->next = r->next;
                    } else {
                         responses = r->next;
                    }
                    free(r->resp);
                    free(r);

                    /* remove query object */
                    if (q1) {
                         q1->next = q->next;
                    } else {
                         queries = q->next;
                    }
                    free(q);
               }
          }
     }

     return;
}

void
gtp_run_command(Board *b, Command c, char *param, Callback cb)
{
     struct Query *q;
     static uint32_t counter = 0;
     char *cmd;

     switch (c) {
     case PROTOCOL_VERSION:	cmd = "protocol_version"; break;
     case NAME:			cmd = "name"; break;
     case KNOWN_COMMAND:	cmd = "known_command"; break;
     case LIST_COMMANDS:	cmd = "list_commands"; break;
     case QUIT:			cmd = "quit"; break;
     case BOARDSIZE:		cmd = "boardsize"; break;
     case CLEAR_BOARD:		cmd = "clear_board"; break;
     case KOMI:			cmd = "komi"; break;
     case PLAY:			cmd = "play"; break;
     case GENMOVE:		cmd = "genmove"; break;
     case UNDO:			cmd = "undo"; break;
     case REG_GENMOVE:		cmd = "reg_genmove"; break;
     default:
          abort();
     }

     /* initialize query object (parsed response + ) */
     q = malloc(sizeof(struct Query));
     if (!q) {
          perror("malloc");
          exit(EXIT_FAILURE);
     }

     *q = (struct Query) {
          .id      = ++counter,
          .cmd     = c,
          .cb      = cb,
          .b       = b,
          .next    = queries,
     };
     queries = q;

     /* send command */
     if (param) {
          if (debug) {
               fprintf(stderr, "run: %d %s %s\n", counter, cmd, param);
          }
          printf("%d %s %s\n", counter, cmd, param);
     } else {
          if (debug) {
               fprintf(stderr, "run: %d %s\n", counter, cmd);
          }
          printf("%d %s\n", counter, cmd);
     }
     if (debug) {
          printf("showboard\n");
     }
     fflush(stdout);

     b->changed = false;
     gtp_check_responses();
}

