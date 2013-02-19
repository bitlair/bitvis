/*
 * bitvis
 * Copyright (C) Bob 2013
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

#ifndef DEBUGWINDOW_H
#define DEBUGWINDOW_H

#include "thread.h"
#include "condition.h"
#include "tcpsocket.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>

class CDebugWindow : public CThread
{
  public:
    CDebugWindow();
    ~CDebugWindow();

    void Enable(int width, int height, int scale);
    void Disable();
    void DisplayFrame(CTcpData& data);

    void Process();

  private:
    bool Setup();
    void ProcessInternal();
    void Cleanup();

    int                      m_scale;
    CCondition               m_condition;
    CTcpData                 m_data;

    Display*                 m_dpy;
    int                      m_width;
    int                      m_height;
    Window                   m_rootwin;
    XWindowAttributes        m_rootattr;
    Pixmap                   m_pixmap;
    XRenderPictFormat*       m_srcformat;
    XRenderPictFormat*       m_dstformat;
    Picture                  m_srcpicture;
    Picture                  m_dstpicture;
    XRenderPictureAttributes m_pictattr;
    XTransform               m_transform;
    XImage*                  m_xim;
    Window                   m_window;
    GC                       m_gc;
};

#endif //DEBUGWINDOW_H
