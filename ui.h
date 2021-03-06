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

#include "board.h"
#include "state.h"

#ifndef UI_H
#define UI_H

void ui_init(uint8_t, uint8_t);
void ui_cleanup();
void ui_loop(struct Board *, enum State*, enum Stone, bool);

/* from sgo.c */
bool place_bot_stone(struct Obj *o, bool error);

#endif
