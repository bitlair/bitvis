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

#include "bitvlc.h"
#include "util/log.h"
#include "util/misc.h"
#include "util/lock.h"
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>

using namespace std;

CBitVlc::CBitVlc(int argc, char *argv[])
{
  m_debug = false;
  m_debugscale = 2;
  m_instance = NULL;
  m_player = NULL;
  m_media = NULL;
  m_port = 1337;
  m_address = NULL;
  m_width = 120;
  m_height = 48;
  m_plane = NULL;
  m_volume = 0;
  m_dither = false;

  const char* flags = "p:a:m:d:v:f";
  int c;
  while ((c = getopt(argc, argv, flags)) != -1)
  {
    if (c == 'p') //port
    {
      int port;
      if (!StrToInt(string(optarg), port) || port < 0 || port > 65535)
      {
        LogError("Wrong argument \"%s\" for port", optarg);
        exit(1);
      }

      m_port = port;
    }
    if (c == 'd') //debug
    {
      int scale;
      if (!StrToInt(string(optarg), scale) || scale < 0 || scale > 65535)
      {
        LogError("Wrong argument \"%s\" for debug scale", optarg);
        exit(1);
      }

      m_debugscale = scale;
      m_debug = true;
    }
    else if (c == 'a') //address
    {
      m_address = optarg;
    }
    else if (c == 'm') //media
    {
      m_media = optarg;
    }
    if (c == 'v') //volume
    {
      int volume;
      if (!StrToInt(string(optarg), volume) || volume < 0 || volume > volume)
      {
        LogError("Wrong argument \"%s\" for volume", optarg);
        exit(1);
      }

      m_volume = volume;
    }
    else if (c == 'f') //dither
    {
      m_dither = true;
    }
  }

  if (m_media == NULL)
  {
    LogError("No media given (use -m path)");
    exit(1);
  }

  if (m_address == NULL)
    m_debug = true;
}

CBitVlc::~CBitVlc()
{
}

void CBitVlc::Setup()
{
  if (m_debug)
    m_debugwindow.Enable(m_width, m_height, m_debugscale);

  m_instance = libvlc_new(0, NULL);
  if (m_instance == NULL)
  {
    LogError("Unable to init VLC");
    exit(1);
  }

  libvlc_media_t* media = libvlc_media_new_path(m_instance, m_media);
  m_player = libvlc_media_player_new_from_media(media);
  libvlc_media_release(media);

  libvlc_video_set_callbacks(m_player, SVLCLock, SVLCUnlock, SVLCDisplay, this);

  InitPlane(m_width, m_height);

  m_process = false;
  m_display = false;
}

void CBitVlc::Cleanup()
{
  if (m_player)
    libvlc_media_player_release(m_player);

  if (m_instance);
    libvlc_release(m_instance);

  delete[] m_plane;
}

void CBitVlc::Process()
{
  libvlc_media_player_play(m_player);

  for(;;)
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

    CLock lock(m_condition);
    while (!m_process)
      m_condition.Wait();
    m_process = false;

    libvlc_audio_set_volume(m_player, m_volume);

    unsigned int videowidth = 0;
    unsigned int videoheight = 0;
    libvlc_video_get_size(m_player, 0, &videowidth, &videoheight);
    int neededwidth  = Round32((float)videowidth * m_height / videoheight);
    int neededheight = Round32((float)videoheight * m_width / videowidth);
    if (neededwidth > m_width)
      InitPlane(neededwidth, m_height);
    else
      InitPlane(m_width, neededheight);

    int xplanestart = (m_planewidth - m_width) / 2;
    int yplanestart = (m_planeheight - m_height) / 2;
    int xplaneend = xplanestart + m_width;
    int yplaneend = yplanestart + m_height;

    int avg = 0;
    for (int y = yplanestart; y < yplaneend; y++)
    {
      uint8_t* planeptr = m_plane + y * m_planewidth * 3 + xplanestart * 3;
      uint8_t* end = planeptr + m_width * 3;
      while (planeptr != end)
      {
        avg += *(planeptr++);
        avg += *(planeptr++);
        planeptr++;
      }
    }
    avg /= m_width * m_height * 2;

    CTcpData data;
    data.SetData(":00");
    uint8_t last;

    if (m_dither)
    {
      for (int y = yplanestart; y < yplaneend; y++)
      {
        uint8_t* planeptr = m_plane + y * m_planewidth * 3 + xplanestart * 3;
        int value;
        int quanterror;
        for (int x = xplanestart; x < xplaneend; x++)
        {
          for (int i = 0; i < 2; i++)
          {
            if (*planeptr > avg)
            {
              value = 255;
            }
            else
            {
              value = 0;
            }

            quanterror = *planeptr - value;
            *planeptr = value;

            if (x < xplaneend - 1)
            {
              *(planeptr + 3) = Clamp(*(planeptr + 3) + quanterror * 7 / 16, 0, 255);
              if (y < xplaneend - 1)
                *(planeptr + 3 + m_planewidth * 3) = Clamp(*(planeptr + 3 + m_planewidth * 3) + quanterror / 16, 0, 255);
            }
            if (y < xplaneend - 1)
            {
              *(planeptr + m_planewidth * 3) = Clamp(*(planeptr + m_planewidth * 3) + quanterror * 5 / 16, 0, 255);
              if (x > xplanestart)
                *(planeptr - 3 + m_planewidth * 3) = Clamp(*(planeptr - 3 + m_planewidth * 3) + quanterror * 3 / 16, 0, 255);
            }

            planeptr++;
          }
          planeptr++;
        }
      }

      avg = 128;
    }

    {
      for (int y = yplanestart; y < yplaneend; y++)
      {
        uint8_t* planeptr = m_plane + y * m_planewidth * 3 + xplanestart * 3;
        uint8_t* end = planeptr + m_width * 3;
        uint8_t  line[m_width / 4];
        uint8_t* lineptr = line;
        int      pixelcount = 3;
        memset(line, 0, sizeof(line));

        while (planeptr != end)
        {
          planeptr++;
          if (*(planeptr++) > avg)
            *lineptr |= 1 << (pixelcount * 2); //green
          if (*(planeptr++) > avg)
            *lineptr |= 1 << (pixelcount * 2 + 1); //red
          pixelcount--;
          if (pixelcount == -1)
          {
            pixelcount = 3;
            lineptr++;
          }
        }

        if (y == yplaneend - 1)
        {
          //dont add the last byte here, it will be sent later
          data.SetData(line, sizeof(line) - 1, true);
          last = line[sizeof(line) - 1];
        }
        else
        {
          data.SetData(line, sizeof(line), true);
        }
      }
    }

    //send everything but the last byte, since the bitpanel is double buffered
    //the timing is improved by sending only the last byte when the frame needs to be displayed
    lock.Leave();
    if (m_socket.IsOpen())
    {
      if (m_socket.Write(data) != SUCCESS)
      {
        LogError("%s", m_socket.GetError().c_str());
        m_socket.Close();
      }
    }
    m_debugwindow.DisplayFrame(data);
    lock.Enter();

    while (!m_display)
      m_condition.Wait();
    m_display = false;
    lock.Leave();

    uint8_t end[10] = {};
    data.SetData(&last, 1);
    data.SetData(end, sizeof(end), true);

    if (m_socket.IsOpen())
    {
      if (m_socket.Write(data) != SUCCESS)
      {
        LogError("%s", m_socket.GetError().c_str());
        m_socket.Close();
      }
    }
    m_debugwindow.DisplayFrame(data);
  }

  libvlc_media_player_stop(m_player);
}

void CBitVlc::InitPlane(int width, int height)
{
  if (m_planewidth != width || m_planeheight != height)
  {
    Log("Setting plane to %ix%i", width, height);
    m_planewidth = width;
    m_planeheight = height;
    delete[] m_plane;
    m_plane = new uint8_t[m_planewidth * m_planeheight * 3];
    memset(m_plane, 0, m_planewidth * m_planeheight * 3);
    //reset the player to update to the new format
    libvlc_media_player_stop(m_player);
    libvlc_video_set_format(m_player, "RV24", m_planewidth, m_planeheight, m_planewidth * 3);
    libvlc_media_player_play(m_player);
  }
}

void* CBitVlc::SVLCLock (void *opaque, void **planes)
{
  return ((CBitVlc*)opaque)->VLCLock(planes);
}

void CBitVlc::SVLCUnlock (void *opaque, void *picture, void *const *planes)
{
  ((CBitVlc*)opaque)->VLCUnlock(picture, planes);
}

void CBitVlc::SVLCDisplay(void *opaque, void *picture)
{
  ((CBitVlc*)opaque)->VLCDisplay(picture);
}

void* CBitVlc::VLCLock (void **planes)
{
  m_condition.Lock();
  planes[0] = m_plane;
  return NULL;
}

void CBitVlc::VLCUnlock (void *picture, void *const *planes)
{
  m_process = true;
  m_condition.Signal();
  m_condition.Unlock();
}

void CBitVlc::VLCDisplay(void *picture)
{
  m_condition.Lock();
  m_display = true;
  m_condition.Signal();
  m_condition.Unlock();
}

