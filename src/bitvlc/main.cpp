/*
 * bitvlc
 * Copyright (C) Bob 2012
 * 
 * bitvlc is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * bitvlc is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bitvlc.h"
#include "util/log.h"

int main (int argc, char *argv[])
{
  SetLogFile(".bitvlc", "bitvlc.log");

  CBitVlc bitvlc(argc, argv);

  bitvlc.Setup();
  bitvlc.Process();
  bitvlc.Cleanup();

  return 0;
}

