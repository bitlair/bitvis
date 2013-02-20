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

#include "debugwindow.h"
#include "lock.h"
#include "log.h"
#include "timeutils.h"
#include "inclstdint.h"

#include <cstring>
#include <cstdlib>

CDebugWindow::CDebugWindow()
{
  m_scale = 0;
  m_dpy = NULL;
  m_width = 0;
  m_height = 0;
  m_pixmap = None;
  m_srcformat = NULL;
  m_dstformat = NULL;
  m_srcpicture = None;
  m_dstpicture = None;
  memset(&m_pictattr, 0, sizeof(m_pictattr));
  memset(&m_transform, 0, sizeof(m_transform));
  memset(&m_rootattr, 0, sizeof(m_rootattr));
  m_xim = NULL;
  m_window = None;
  m_rootwin = None;
  m_gc = None;
}

CDebugWindow::~CDebugWindow()
{
  Disable();
}

void CDebugWindow::Enable(int width, int height, int scale)
{
  Disable();

  m_width = width;
  m_height = height;
  m_scale = scale;

  StartThread();
}

void CDebugWindow::Disable()
{
  AsyncStopThread();
  CLock lock(m_condition);
  m_process = true;
  m_condition.Signal();
  lock.Leave();
  StopThread();
}

void CDebugWindow::DisplayFrame(CTcpData& data)
{
  if (m_running)
  {
    CLock lock(m_condition);
    m_data.push_back(data);
    m_process = true;
    m_condition.Signal();
  }
}

void CDebugWindow::Process()
{
  if (!Setup())
  {
    Cleanup();
    return;
  }

  ProcessInternal();
  Cleanup();
}

bool CDebugWindow::Setup()
{
  m_dpy = XOpenDisplay(NULL);
  if (m_dpy == NULL)
  {
    LogError("Unable to open display");
    return false;
  }

  m_window = XCreateSimpleWindow(m_dpy, RootWindow(m_dpy, DefaultScreen(m_dpy)),
                                 0, 0, m_width * m_scale, m_height * m_scale, 0, 0, 0);
  XMapWindow(m_dpy, m_window);
  XFlush(m_dpy);

  m_rootwin = RootWindow(m_dpy, DefaultScreen(m_dpy));
  XGetWindowAttributes(m_dpy, m_rootwin, &m_rootattr);
  m_gc = XCreateGC(m_dpy, m_window, 0, NULL);
  m_pixmap = XCreatePixmap(m_dpy, m_rootwin, m_width, m_height, m_rootattr.depth);
  m_srcformat = XRenderFindVisualFormat(m_dpy, m_rootattr.visual);
  m_dstformat = XRenderFindVisualFormat(m_dpy, m_rootattr.visual);
  m_srcpicture = XRenderCreatePicture(m_dpy, m_pixmap, m_srcformat, CPRepeat, &m_pictattr);
  m_dstpicture = XRenderCreatePicture(m_dpy, m_window, m_dstformat, CPRepeat, &m_pictattr);
  XRenderSetPictureFilter(m_dpy, m_srcpicture, "nearest", NULL, 0);

  m_xim = XCreateImage(m_dpy, m_rootattr.visual, m_rootattr.depth, ZPixmap, 0, NULL, m_width, m_height, 8, m_width * 4);
  m_xim->data = (char*)malloc(m_width * m_height * 4);
  memset(m_xim->data, 0, m_width * m_height * 4);

  m_transform.matrix[0][0] = m_width;
  m_transform.matrix[1][1] = m_width;
  m_transform.matrix[2][2] = m_width * m_scale;

  m_process = false;

  return true;
}

void CDebugWindow::Cleanup()
{
  if (m_xim)
  {
    XDestroyImage(m_xim);
    m_xim = NULL;
  }

  if (m_dstpicture != None)
  {
    XRenderFreePicture(m_dpy, m_dstpicture);
    m_dstpicture = None;
  }

  if (m_srcpicture != None)
  {
    XRenderFreePicture(m_dpy, m_srcpicture);
    m_srcpicture = None;
  }

  if (m_pixmap != None)
  {
    XFreePixmap(m_dpy, m_pixmap);
    m_pixmap = None;
  }

  if (m_gc != None)
  {
    XFreeGC(m_dpy, m_gc);
    m_gc = None;
  }

  if (m_window != None)
  {
    XDestroyWindow(m_dpy, m_window);
    m_window = None;
  }

  if (m_dpy)
  {
    XCloseDisplay(m_dpy);
    m_dpy = NULL;
  }
}

#define BYTESPERFRAME  (120 * 48 / 4 + 3)
#define BAUDRATE       (500000)

void CDebugWindow::ProcessInternal()
{
  bool    render = true;
  int     count  = -3;
  int     xcount = 0;
  int     ycount = 0;
  int64_t lastrender = GetTimeUs();

  while (!m_stop)
  {
    if (render)
    {
      XPutImage(m_dpy, m_pixmap, m_gc, m_xim, 0, 0, 0, 0, m_width, m_height);
      
      XRenderSetPictureTransform (m_dpy, m_srcpicture, &m_transform);
      XRenderComposite(m_dpy, PictOpSrc, m_srcpicture, None, m_dstpicture,
                       0, 0, 0, 0, 0, 0, m_width * m_scale, m_height * m_scale);

      //add a delay, to simulate the bytes being transferred over rs232 to the bitpanel
      int64_t frametime = 1000000LL * 10 * BYTESPERFRAME / BAUDRATE;
      int64_t now = GetTimeUs();
      USleep(lastrender + frametime - now);
      lastrender = GetTimeUs();

      XFlush(m_dpy);
      render = false;
    }

    CLock lock(m_condition);
    if (m_data.empty())
    {
      while (!m_process)
        m_condition.Wait();
      m_process = false;
    }

    if (m_data.empty())
      continue;

    CTcpData data = m_data.front();
    m_data.pop_front();
    lock.Leave();

    int size = data.GetSize();
    uint8_t* dataptr = (uint8_t*)data.GetData();
    for (int i = 0; i < size; i++)
    {
      if (count == -3)
      {
        if (dataptr[i] == ':')
          count++;
      }
      else if (count == -2 || count == -1)
      {
        if (dataptr[i] == '0')
          count++;
        else
          count = -3;
      }
      else
      {
        uint8_t* pixelptr = (uint8_t*)m_xim->data + ycount * m_xim->bytes_per_line + xcount * 4;
        for (int j = 0; j < 4; j++)
        {
          pixelptr++;
          *(pixelptr++) = ((dataptr[i] << (j * 2 + 1)) & 128) ? 0xFF : 0;
          *(pixelptr++) = ((dataptr[i] << (j * 2)) & 128) ? 0xFF : 0;
          pixelptr++;
          xcount++;
          if (xcount == m_width)
          {
            xcount = 0;
            ycount++;
            if (ycount == m_height)
            {
              render = true;
              count = -3;
              ycount = 0;
            }
          }
        }
      }
    }
  }
}
