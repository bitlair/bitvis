/*
 * bitvis
 * Copyright (C) Bob 2012
 * 
 * bitvis is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * bitvis is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BITX11_H
#define BITX11_H

#include "util/tcpsocket.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

class CBitX11
{
  public:
    CBitX11(int argc, char *argv[]);
    ~CBitX11();

    void Setup();
    void Process();
    void Cleanup();

  private:

    int                m_port;
    const char*        m_address;
    float              m_fps;

    CTcpClientSocket   m_socket;

    Display*           m_dpy;
    Window             m_rootwin;
    XWindowAttributes  m_rootattr;

    int                m_destwidth;
    int                m_destheight;

    Pixmap             m_pixmap;
    XRenderPictFormat* m_srcformat;
    XRenderPictFormat* m_dstformat;
    Picture            m_srcpicture;
    Picture            m_dstpicture;
    XRenderPictureAttributes m_pictattr;
    XTransform         m_transform;
    XShmSegmentInfo    m_shmseginfo;
    XImage*            m_xim;

    bool               m_debug;
    int                m_debugscale;
    Window             m_debugwindow;
    GC                 m_debuggc;
    Pixmap             m_debugpixmap;
    XRenderPictFormat* m_debugsrcformat;
    XRenderPictFormat* m_debugdstformat;
    Picture            m_debugsrcpicture;
    Picture            m_debugdstpicture;
    XTransform         m_debugtransform;
};

#endif //BITX11_H
