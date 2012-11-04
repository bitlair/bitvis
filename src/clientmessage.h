/*
 * bobdsp
 * Copyright (C) Bob 2012
 * 
 * bobdsp is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * bobdsp is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CLIENTMESSAGE_H
#define CLIENTMESSAGE_H

#include "util/inclstdint.h"

enum ClientMessage
{
  MsgNone,
  MsgExited,
};

inline const char* MsgToString(ClientMessage msg)
{
  static const char* msgstrings[] = 
  {
    "MsgNone",
    "MsgExited",
  };

  if (msg >= 0 && (size_t)msg < (sizeof(msgstrings) / sizeof(msgstrings[0])))
    return msgstrings[msg];
  else
    return "ERROR: INVALID MESSAGE";
}

inline const char* MsgToString(uint8_t msg)
{
  return MsgToString((ClientMessage)msg);
}

#endif //CLIENTMESSAGE_H
