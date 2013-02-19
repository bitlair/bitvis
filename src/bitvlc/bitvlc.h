/*
 * bitvlc
 * Copyright (C) Bob 2013
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

#ifndef BITVLC_H
#define BITVLC_H

class CBitVlc
{
  public:
    CBitVlc(int argc, char *argv[]);
    ~CBitVlc();

    void Setup();
    void Process();
    void Cleanup();
};

#endif //BITVLC_H
