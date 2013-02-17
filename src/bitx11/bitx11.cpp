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

#include "bitx11.h"
#include "util/misc.h"
#include "util/log.h"
#include "util/timeutils.h"
#include "util/inclstdint.h"

#include <unistd.h>
#include <stdlib.h>

using namespace std;

CBitX11::CBitX11(int argc, char *argv[])
{
  m_port = 1337;
  m_address = NULL;
  m_fps = 30.0f;
  m_destwidth = 120;
  m_destheight = 48;
  m_debug = false;
  m_debugscale = 2;

  const char* flags = "p:a:f:d:";
  int c;
  while ((c = getopt(argc, argv, flags)) != -1)
  {
    if (c == 'f') //fps
    {
      float fps;
      if (!StrToFloat(string(optarg), fps) || fps <= 0)
      {
        LogError("Wrong argument \"%s\" for fps", optarg);
        exit(1);
      }

      m_fps = fps;
    }
    else if (c == 'p') //port
    {
      int port;
      if (!StrToInt(string(optarg), port) || port < 0 || port > 65535)
      {
        LogError("Wrong argument \"%s\" for port", optarg);
        exit(1);
      }

      m_port = port;
    }
    else if (c == 'a') //address
    {
      m_address = optarg;
    }
    else if (c == 'd') //debug
    {
      m_debug = true;

      int scale;
      if (!StrToInt(string(optarg), scale) || scale <= 0)
      {
        LogError("Wrong argument \"%s\" for debug scale", optarg);
        exit(1);
      }

      m_debugscale = scale;
    }
  }

  //if no address is specified, turn on the debug window instead
  if (!m_address)
    m_debug = true;

  m_dpy = NULL;
  m_srcformat = NULL;
  m_dstformat = NULL;
  m_pixmap = None;
  m_srcpicture = None;
  m_dstpicture = None;

  memset(&m_pictattr, 0, sizeof(m_pictattr));
  m_pictattr.repeat = RepeatNone;

  memset(&m_transform, 0, sizeof(m_transform));
  memset(&m_debugtransform, 0, sizeof(m_debugtransform));
}

CBitX11::~CBitX11()
{
}

void CBitX11::Setup()
{
  m_dpy = XOpenDisplay(NULL);
  if (m_dpy == NULL)
  {
    LogError("Unable to open display");
    exit(1);
  }

  m_rootwin = RootWindow(m_dpy, DefaultScreen(m_dpy));
  XGetWindowAttributes(m_dpy, m_rootwin, &m_rootattr);

  m_pixmap = XCreatePixmap(m_dpy, m_rootwin, m_destwidth, m_destheight, m_rootattr.depth);

  m_srcformat = XRenderFindVisualFormat(m_dpy, m_rootattr.visual);
  m_dstformat = XRenderFindVisualFormat(m_dpy, m_rootattr.visual);
  m_srcpicture = XRenderCreatePicture(m_dpy, m_rootwin, m_srcformat, CPRepeat, &m_pictattr);
  m_dstpicture = XRenderCreatePicture(m_dpy, m_pixmap,  m_dstformat, CPRepeat, &m_pictattr);

  XRenderSetPictureFilter(m_dpy, m_srcpicture, "bilinear", NULL, 0);

  m_xim = XShmCreateImage(m_dpy, m_rootattr.visual, m_rootattr.depth, ZPixmap, NULL, &m_shmseginfo, m_destwidth, m_destheight);
  m_shmseginfo.shmid = shmget(IPC_PRIVATE, m_xim->bytes_per_line * m_xim->height, IPC_CREAT | 0777);
  m_shmseginfo.shmaddr = reinterpret_cast<char*>(shmat(m_shmseginfo.shmid, NULL, 0));
  m_xim->data = m_shmseginfo.shmaddr;
  m_shmseginfo.readOnly = False;
  XShmAttach(m_dpy, &m_shmseginfo);

  if (m_debug)
  {
    m_debugwindow = XCreateSimpleWindow(m_dpy, RootWindow(m_dpy, DefaultScreen(m_dpy)),
                                        0, 0, m_destwidth * m_debugscale, m_destheight * m_debugscale, 0, 0, 0);
    XMapWindow(m_dpy, m_debugwindow);
    XFlush(m_dpy);

    m_debuggc = XCreateGC(m_dpy, m_debugwindow, 0, NULL);
    m_debugpixmap = XCreatePixmap(m_dpy, m_rootwin, m_destwidth, m_destheight, m_rootattr.depth);
    m_debugsrcformat = XRenderFindVisualFormat(m_dpy, m_rootattr.visual);
    m_debugdstformat = XRenderFindVisualFormat(m_dpy, m_rootattr.visual);
    m_debugsrcpicture = XRenderCreatePicture(m_dpy, m_debugpixmap, m_debugsrcformat, CPRepeat, &m_pictattr);
    m_debugdstpicture = XRenderCreatePicture(m_dpy, m_debugwindow, m_debugdstformat, CPRepeat, &m_pictattr);
    XRenderSetPictureFilter(m_dpy, m_debugsrcpicture, "nearest", NULL, 0);
  }
}

void CBitX11::Process()
{
  int64_t looptime = GetTimeUs();
  for (;;)
  {
    if (!m_socket.IsOpen() && m_address)
    {
      if (m_socket.Open(m_address, m_port, 1000000) != SUCCESS)
      {
        LogError("Failed to connect: %s", m_socket.GetError().c_str());
        m_socket.Close();
      }
      else
      {
        Log("Connected");
      }
    }

    m_transform.matrix[0][0] = m_rootattr.width;
    m_transform.matrix[1][1] = m_rootattr.width;
    m_transform.matrix[2][2] = m_destwidth;

    //the aspect ratio of the root window is probably different from the target aspect ratio
    //so clip parts from the top and bottom of the root window
    int offset = (m_rootattr.height * m_destwidth / m_rootattr.width - m_destheight) / 2;

    //render the root window to m_pixmap using xrender
    XRenderSetPictureTransform (m_dpy, m_srcpicture, &m_transform);
    XRenderComposite(m_dpy, PictOpSrc, m_srcpicture, None, m_dstpicture, 0, offset, 0, 0, 0, 0, m_destwidth, m_destheight);
    XShmGetImage(m_dpy, m_pixmap, m_xim, 0, 0, AllPlanes);

    //allocate 2 extra pixels on each line, and allocate one extra line
    //since the Floy-Steinberg dithering writes to one line below the current pixel
    //and writes to one pixel to the left and right of the current pixel
    int planewidth = m_destwidth + 2;
    int planeheight = m_destheight + 1;
    int planes[2][planewidth * planeheight];
    int avg = 0;
    //copy the red and green pixels to the planes, and calculate the average pixel value
    for (int y = 0; y < m_destheight; y++)
    {
      uint8_t* ximline = (uint8_t*)m_xim->data + y * m_xim->bytes_per_line;
      uint8_t* ximend  = ximline + m_xim->bytes_per_line;
      int* planeliner = planes[0] + y * planewidth + 1;
      int* planelineg = planes[1] + y * planewidth + 1;

      while (ximline != ximend)
      {
        ximline++;
        avg += *(planelineg++) = *(ximline++);
        avg += *(planeliner++) = *(ximline++);
        ximline++;
      }
    }
    avg /= m_destwidth * m_destheight * 2;

    //quantize the planes, apply Floy-Steinberg dithering, and write into the buffer for the socket
    CTcpData data;
    data.SetData(":00");
    for (int y = 0; y < m_destheight; y++)
    {
      uint8_t ledline[m_destwidth / 4];
      memset(ledline, 0, sizeof(ledline));

      for (int i = 0; i < 2; i++)
      {
        int* line = planes[i] + y * planewidth + 1;
        int* lineend = line + m_destwidth;
        int  quantval;
        int  quanterror;

        uint8_t* ledpos = ledline;
        int      ledcounter = 0;

        while (line != lineend)
        {
          //if a pixel is higher than the average, set it to the max value
          //this will turn on the led
          if (*line > avg)
          {
            quantval = 255;
            *ledpos |= 1 << ((ledcounter * 2) + (1 - i));
          }
          else
          {
            quantval = 0;
          }

          ledcounter++;
          if (ledcounter == 4)
          {
            ledcounter = 0;
            ledpos++;
          }

          //apply Floyd-Steinberg dither
          quanterror = *line - quantval;
          *line = quantval;

          line[1] += quanterror * 7 / 16;
          line[m_destwidth - 1] += quanterror * 3 / 16;
          line[m_destwidth] += quanterror * 5 / 16;
          line[m_destwidth + 1] += quanterror / 16;

          line++;
        }
      }

      //append the line to the buffer
      data.SetData(ledline, sizeof(ledline), true);
    }

    //add 10 zeros to the buffer in case the receiver is out of sync
    uint8_t end[10] = {};
    data.SetData(end, sizeof(end), true);

    if (m_socket.IsOpen())
    {
      if (m_socket.Write(data) != SUCCESS)
      {
        LogError("%s", m_socket.GetError().c_str());
        m_socket.Close();
      }
    }

    if (m_debug)
    {
      //write the quantized and dithered planes back into the Ximage
      for (int y = 0; y < m_destheight; y++)
      {
        uint8_t* ximline = (uint8_t*)m_xim->data + y * m_xim->bytes_per_line;
        uint8_t* ximend  = ximline + m_xim->bytes_per_line;
        int* planeliner = planes[0] + y * planewidth + 1;
        int* planelineg = planes[1] + y * planewidth + 1;

        while (ximline != ximend)
        {
          *(ximline++) = 0;
          *(ximline++) = Clamp(*(planelineg++), 0, 255);
          *(ximline++) = Clamp(*(planeliner++), 0, 255);
          ximline++;
        }
      }

      //write the Ximage back into the pixmap
      XShmPutImage(m_dpy, m_debugpixmap, m_debuggc, m_xim, 0, 0, 0, 0, m_destwidth, m_destheight, False);

      m_debugtransform.matrix[0][0] = m_destwidth;
      m_debugtransform.matrix[1][1] = m_destwidth;
      m_debugtransform.matrix[2][2] = m_destwidth * m_debugscale;

      //render the pixmap on the debug window, scaled by m_debugscale
      XRenderSetPictureTransform (m_dpy, m_debugsrcpicture, &m_debugtransform);
      XRenderComposite(m_dpy, PictOpSrc, m_debugsrcpicture, None, m_debugdstpicture,
                       0, 0, 0, 0, 0, 0, m_destwidth * m_debugscale, m_destheight * m_debugscale);

      XFlush(m_dpy);
    }

    looptime += Round64(1000000.0f / m_fps);
    USleep(looptime - GetTimeUs());
  }
}

void CBitX11::Cleanup()
{
}

