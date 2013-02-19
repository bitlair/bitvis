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

#include <vlc/vlc.h>
#include "util/debugwindow.h"
#include "util/condition.h"
#include "util/tcpsocket.h"

class CBitVlc
{
  public:
    CBitVlc(int argc, char *argv[]);
    ~CBitVlc();

    void Setup();
    void Process();
    void Cleanup();

  private:
    bool                   m_debug;
    int                    m_debugscale;
    CDebugWindow           m_debugwindow;

    int                    m_port;
    const char*            m_address;
    CTcpClientSocket       m_socket;
    const char*            m_media;
    libvlc_instance_t*     m_instance;
    libvlc_media_player_t* m_player;
    int                    m_volume;
    int                    m_width;
    int                    m_height;
    bool                   m_dither;
    CCondition             m_condition;
    uint8_t*               m_plane;
    int                    m_planewidth;
    int                    m_planeheight;

    bool                   m_process;
    bool                   m_display;

    void InitPlane(int width, int height);

    static void* SVLCLock (void *opaque, void **planes);
    static void  SVLCUnlock (void *opaque, void *picture, void *const *planes);
    static void  SVLCDisplay(void *opaque, void *picture);

    void* VLCLock (void **planes);
    void  VLCUnlock (void *picture, void *const *planes);
    void  VLCDisplay(void *picture);
};

#endif //BITVLC_H
