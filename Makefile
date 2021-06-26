# Copyright 2020-2021 Philip Kaludercic
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see
# <https://www.gnu.org/licenses/>.

CC	= clang
CFLAGS	= `pkg-config --cflags xcb`						\
	  -D_POSIX_C_SOURCE=200809L -std=c99 -Wall -Wextra -Werror -pedantic	\
	  -pipe -O0 -ggdb3 -fno-omit-frame-pointer 
LDFLAGS	= `pkg-config --libs xcb`
PREFIX  = /usr/local

OBJ	= sgo.o gtp.o board.o	

all: sgo

sgo: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

board.o: board.h
gtp.o:   gtp.c board.h
sgo.o:   sgo.c gtp.h board.h

TAGS: board.c gtp.c sgo.c board.h gtp.h
	ctags -Re $^

clean:
	rm -f *.o sgo TAGS

install: all
	install -Dpm 755 sgo $(PREFIX)/games
	install -Dpm 755 contrib/sgo.gnugo $(PREFIX)/games
	install -Dpm 644 sgo.1 $(PREFIX)/share/man/man6

uninstall:
	rm -f $(PREFIX)/games/sgo
	rm -f $(PREFIX)/games/sgo.gnugo
	rm -f $(PREFIX)/share/man/man6/sgo.1

check-syntax: 			# flymake support
	$(CC) -fsyntax-only -fanalyzer $(CFLAGS) $(CHK_SOURCES)

.PHONY: all clean install uninstall check-syntax
.POSIX:
