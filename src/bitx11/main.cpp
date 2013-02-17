/*
 * bitx11
 * Copyright (C) Bob 2012
 * 
 * bitx11 is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * bitx11 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bitx11.h"

int main (int argc, char *argv[])
{
  CBitX11 bitx11(argc, argv);

  bitx11.Setup();
  bitx11.Process();
  bitx11.Cleanup();

  return 0;
}

