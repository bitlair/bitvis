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

#include "util/inclstdint.h"
#include "bitvis.h"
#include "util/log.h"
#include "util/misc.h"
#include "util/timeutils.h"

#include <signal.h>
#include <sys/signalfd.h>
#include <math.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

using namespace std;

#define CONNECTINTERVAL 1000000

CBitVis::CBitVis(int argc, char *argv[])
{
  g_printdebuglevel = true;
  m_stop = false;
  m_buf = NULL;
  m_bufsize = 0;
  m_fftbuf = NULL;
  m_displaybuf = NULL;
  m_samplecounter = 0;
  m_nrffts = 0;
  m_peakholds = NULL;
  m_nrbins = 1024;
  m_nrcolumns = 120;
  m_decay = 0.5;

  m_fontheight = 0;
  for (int i = 0; i < 128; i++)
  {
    const unsigned int* dchar = GetChar(i);
    if (dchar)
    {
      int charheight = CharHeight(dchar);
      if (charheight > m_fontheight)
        m_fontheight = charheight;
    }
  }

  m_nrlines = 48 - m_fontheight - 1;
}

CBitVis::~CBitVis()
{
}

void CBitVis::Setup()
{
  //init the logfile
  SetLogFile("bitvis.log");

  SetupSignals();

  jack_set_error_function(JackError);
  jack_set_info_function(JackInfo);

  m_fft.Allocate(m_nrbins * 2);

  m_fftbuf = new float[m_nrbins];
  memset(m_fftbuf, 0, m_nrbins * sizeof(float));

  m_displaybuf = new float[m_nrcolumns];
  memset(m_displaybuf, 0, m_nrcolumns * sizeof(float));

  m_peakholds = new peak[m_nrcolumns];
  memset(m_peakholds, 0, m_nrcolumns * sizeof(peak));
}

void CBitVis::SetupSignals()
{
  m_signalfd = -1;

  sigset_t sigset;
  if (sigemptyset(&sigset) == -1)
  {
    LogError("sigemptyset: %s", GetErrno().c_str());
    return;
  }

  if (sigaddset(&sigset, SIGTERM) == -1)
  {
    LogError("adding SIGTERM: %s", GetErrno().c_str());
    return;
  }

  if (sigaddset(&sigset, SIGINT) == -1)
  {
    LogError("adding SIGINT: %s", GetErrno().c_str());
    return;
  }

  //create a file descriptor that will catch SIGTERM and SIGINT
  m_signalfd = signalfd(-1, &sigset, SFD_NONBLOCK);
  if (m_signalfd == -1)
  {
    LogError("signalfd: %s", GetErrno().c_str());
  }
  else
  {
    //block SIGTERM and SIGINT
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) == -1)
      LogError("sigpocmask: %s", GetErrno().c_str());
  }

  if (sigemptyset(&sigset) == -1)
  {
    LogError("sigemptyset: %s", GetErrno().c_str());
    return;
  }

  if (sigaddset(&sigset, SIGPIPE) == -1)
  {
    LogError("adding SIGPIPE: %s", GetErrno().c_str());
    return;
  }

  //libjack throws SIGPIPE a lot, block it
  if (sigprocmask(SIG_BLOCK, &sigset, NULL) == -1)
    LogError("sigpocmask: %s", GetErrno().c_str());
}

void CBitVis::Process()
{
  int64_t lastconnect = GetTimeUs() - CONNECTINTERVAL - 1;

  while (!m_stop)
  {
    bool didconnect = false;

    if (m_jackclient.ExitStatus())
    {
      LogError("Jack client exited with code %i reason: \"%s\"",
               (int)m_jackclient.ExitStatus(), m_jackclient.ExitReason().c_str());
      m_jackclient.Disconnect();
    }

    if (!m_jackclient.IsConnected() && GetTimeUs() - lastconnect > CONNECTINTERVAL)
    {
      m_jackclient.Connect();
      didconnect = true;
    }

    uint8_t msg;
    while ((msg = m_jackclient.GetMessage()) != MsgNone)
      LogDebug("got message %s from jack client", MsgToString(msg));

    if (!m_socket.IsOpen() && GetTimeUs() - lastconnect > CONNECTINTERVAL)
    {
      if (m_socket.Open("192.168.88.117", 1337, 60000000) == FAIL)
      {
        LogError("Failed to connect: %s", m_socket.GetError().c_str());
        m_socket.Close();
      }
      else
      {
        Log("Connected");
      }
      didconnect = true;
    }

    if (didconnect)
      lastconnect = GetTimeUs();

    if (m_jackclient.IsConnected())
      ProcessAudio();
    else
      sleep(1);

    ProcessSignalfd();
  }

  m_jackclient.Disconnect();
}

void CBitVis::ProcessSignalfd()
{
  signalfd_siginfo siginfo;
  int returnv = read(m_signalfd, &siginfo, sizeof(siginfo));
  if (returnv == -1 && errno != EAGAIN)
  {
    LogError("reading signals fd: %s", GetErrno().c_str());
    if (errno != EINTR)
    {
      close(m_signalfd);
      m_signalfd = -1;
    }
  }
  else if (returnv > 0)
  {
    if (siginfo.ssi_signo == SIGTERM || siginfo.ssi_signo == SIGINT)
    {
      Log("caught %s, exiting", siginfo.ssi_signo == SIGTERM ? "SIGTERM" : "SIGINT");
      m_stop = true;
    }
    else
    {
      LogDebug("caught signal %i", siginfo.ssi_signo);
    }
  }
}

void CBitVis::ProcessAudio()
{
  int samplerate;
  int samples;
  int64_t audiotime;
  if ((samples = m_jackclient.GetAudio(m_buf, m_bufsize, samplerate, audiotime)) > 0)
  {
    if (!m_socket.IsOpen())
      return;

    int additions = 0;
    for (int i = 1; i < m_nrcolumns; i++)
      additions += i;

    const int maxbin = Round32(15000.0f / samplerate * m_nrbins * 2.0f);

    float increase = (float)(maxbin - m_nrcolumns - 1) / additions;

    for (int i = 0; i < samples; i++)
    {
      m_fft.AddSample(m_buf[i]);
      m_samplecounter++;

      if (m_samplecounter % 32 == 0)
      {
        m_fft.ApplyWindow();
        fftwf_execute(m_fft.m_plan);

        m_nrffts++;
        for (int j = 0; j < m_nrbins; j++)
          m_fftbuf[j] += cabsf(m_fft.m_outbuf[j]) / m_fft.m_bufsize;
      }

      if (m_samplecounter % (samplerate / 30) == 0)
      {
        m_fft.ApplyWindow();
        fftwf_execute(m_fft.m_plan);

        float start = 0.0f;
        float add = 1.0f;
        for (int j = 0; j < m_nrcolumns; j++)
        {
          float next = start + add;

          int bin    = Round32(start) + 1;
          int nrbins = Round32(next - start);
          float outval = 0.0f;
          for (int k = bin; k < bin + nrbins; k++)
            outval += m_fftbuf[k] / m_nrffts;

          m_displaybuf[j] = m_displaybuf[j] * m_decay + outval * (1.0f - m_decay);

          start = next;
          add += increase;
        }

        SendData(audiotime + Round64(1000000.0 / (double)samplerate * (double)i));

        memset(m_fftbuf, 0, m_nrbins * sizeof(float));
        m_nrffts = 0;
      }
    }
  }
}

void CBitVis::Cleanup()
{
  m_jackclient.Disconnect();
}

void CBitVis::SendData(int64_t time)
{
  CTcpData data;
  data.SetData(":00");

  for (int y = m_nrlines - 1; y >= 0; y--)
  {
    uint8_t line[m_nrcolumns / 4];
    for (int x = 0; x < m_nrcolumns / 4; x++)
    {
      uint8_t pixel = 0;
      for (int i = 0; i < 4; i++)
      {
        pixel <<= 2;
        int value = Round32(((log10(m_displaybuf[x * 4 + i]) * 20.0f) + 55.0f) / 48.0f * m_nrlines);
        if (value > y)
          pixel |= 1;

        peak& currpeak = m_peakholds[x * 4 + i];
        if (value >= Round32(currpeak.value))
        {
          currpeak.value = value;
          currpeak.time = time;
        }

        if (Round32(currpeak.value) == y)
          pixel |= 2;

        if (time - currpeak.time > 500000 && Round32(currpeak.value) > 0)
        {
          currpeak.value += 0.01f;
          if (currpeak.value >= m_nrlines)
            currpeak.value = 0.0f;
        }
      }
      line[x] = pixel;
    }
    data.SetData(line, sizeof(line), true);
  }

  uint8_t text[m_nrcolumns / 4 * m_fontheight];
  memset(text, 0, sizeof(text));

  AddText(text, "Now Playing");

  //add an empty line
  data.SetData(text, m_nrcolumns / 4, true);

  /*const unsigned int* dchar = GetChar('b');
  for (int i = 0; i < m_fontheight; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      if (dchar[3 - j] & (1 << (m_fontheight - i - 1)))
        text[m_nrcolumns / 4 * i] |= 1 << (j * 2);

      if (dchar[7 - j] & (1 << (m_fontheight - i - 1)))
        text[m_nrcolumns / 4 * i + 1] |= 1 << (j * 2);
    }
  }*/

  data.SetData(text, sizeof(text), true);

  uint8_t end[10];
  memset(end, 0, sizeof(end));
  data.SetData(end, sizeof(end), true);

  USleep(time - GetTimeUs());

  if (m_socket.Write(data) == FAIL)
  {
    LogError("%s", m_socket.GetError().c_str());
    m_socket.Close();
  }
}

void CBitVis::AddText(uint8_t* buff, const char* str)
{
  int length = strlen(str);

  for (int currchar = 0; currchar < length; currchar++)
  {
    const unsigned int* dchar = GetChar(str[currchar]);
    if (dchar)
    {
      for (int i = 0; i < m_fontheight; i++)
      {
        for (int j = 0; j < 4; j++)
        {
          if (dchar[3 - j] & (1 << (m_fontheight - i - 1)))
            buff[m_nrcolumns / 4 * i + currchar * 2] |= 1 << (j * 2);

          if (dchar[7 - j] & (1 << (m_fontheight - i - 1)))
            buff[m_nrcolumns / 4 * i + 1 + currchar * 2] |= 1 << (j * 2);
        }
      }
    }
  }
}

const unsigned int* CBitVis::GetChar(char in)
{
    static const char letter[] =
      {
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ',', '.', '/',
        '<', '>', '?', ';', '\'', ':', '"', '[', ']', '\\', '{', '}', '|',
        '`', '-', '=', '~', '!', '@', '#', '$', '%', '^', '&', '*', '(',
        ')', '_', '+', ' '
      };

    static const unsigned int letterData[][8] =
    {
      {
        0x18, 0xbc, 0xa4, 0xa4, 0xf8, 0x7c, 0x4,
      },
      {
        0x400, 0x7fc, 0x7fc, 0x84, 0xc4, 0x7c, 0x38,
      },
      {
        0x78, 0xfc, 0x84, 0x84, 0x84, 0xcc, 0x48,
      },
      {
        0x38, 0x7c, 0xc4, 0x484, 0x7f8, 0x7fc, 0x4,
      },
      {
        0x78, 0xfc, 0xa4, 0xa4, 0xa4, 0xec, 0x68,
      },
      {
        0x44, 0x3fc, 0x7fc, 0x444, 0x600, 0x300,
      },
      {
        0x72, 0xfb, 0x89, 0x89, 0x7f, 0xfe, 0x80,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x40, 0x80, 0xfc, 0x7c,
      },
      {
        0x84, 0x6fc, 0x6fc, 0x4,
      },
      {
        0x6, 0x7, 0x1, 0x81, 0x6ff, 0x6fe,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x20, 0x70, 0xdc, 0x8c,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x4,
      },
      {
        0xfc, 0xfc, 0xc0, 0x78, 0xc0, 0xfc, 0x7c,
      },
      {
        0x80, 0xfc, 0x7c, 0x80, 0x80, 0xfc, 0x7c,
      },
      {
        0x78, 0xfc, 0x84, 0x84, 0x84, 0xfc, 0x78,
      },
      {
        0x81, 0xff, 0x7f, 0x89, 0x88, 0xf8, 0x70,
      },
      {
        0x70, 0xf8, 0x88, 0x89, 0x7f, 0xff, 0x81,
      },
      {
        0x84, 0xfc, 0x7c, 0xc4, 0x80, 0xe0, 0x60,
      },
      {
        0x48, 0xec, 0xa4, 0xb4, 0x94, 0xdc, 0x48,
      },
      {
        0x80, 0x80, 0x3f8, 0x7fc, 0x84, 0x8c, 0x8,
      },
      {
        0xf8, 0xfc, 0x4, 0x4, 0xf8, 0xfc, 0x4,
      },
      {
        0xf0, 0xf8, 0xc, 0xc, 0xf8, 0xf0,
      },
      {
        0xf8, 0xfc, 0xc, 0x38, 0xc, 0xfc, 0xf8,
      },
      {
        0x84, 0xcc, 0x78, 0x30, 0x78, 0xcc, 0x84,
      },
      {
        0xf1, 0xf9, 0x9, 0x9, 0xb, 0xfe, 0xfc,
      },
      {
        0xc4, 0xcc, 0x9c, 0xb4, 0xe4, 0xcc, 0x8c,
      },
      {
        0xfc, 0x1fc, 0x320, 0x620, 0x320, 0x1fc, 0xfc,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x444, 0x444, 0x7fc, 0x3b8,
      },
      {
        0x1f0, 0x3f8, 0x60c, 0x404, 0x404, 0x60c, 0x318,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x404, 0x60c, 0x3f8, 0x1f0,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x444, 0x4e4, 0x60c, 0x71c,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x444, 0x4e0, 0x600, 0x700,
      },
      {
        0x1f0, 0x3f8, 0x60c, 0x424, 0x424, 0x638, 0x33c,
      },
      {
        0x7fc, 0x7fc, 0x40, 0x40, 0x40, 0x7fc, 0x7fc,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x404,
      },
      {
        0x18, 0x1c, 0x4, 0x404, 0x7fc, 0x7f8, 0x400,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x40, 0x1f0, 0x7bc, 0x60c,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x404, 0x4, 0xc, 0x1c,
      },
      {
        0x7fc, 0x7fc, 0x380, 0x1c0, 0x380, 0x7fc, 0x7fc,
      },
      {
        0x7fc, 0x7fc, 0x380, 0x1c0, 0xe0, 0x7fc, 0x7fc,
      },
      {
        0x1f0, 0x3f8, 0x60c, 0x404, 0x60c, 0x3f8, 0x1f0,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x444, 0x440, 0x7c0, 0x380,
      },
      {
        0x3f0, 0x7f8, 0x408, 0x438, 0x41e, 0x7fe, 0x3f2,
      },
      {
        0x404, 0x7fc, 0x7fc, 0x440, 0x460, 0x7fc, 0x39c,
      },
      {
        0x318, 0x79c, 0x4c4, 0x444, 0x464, 0x73c, 0x318,
      },
      {
        0x700, 0x604, 0x7fc, 0x7fc, 0x604, 0x700,
      },
      {
        0x7f8, 0x7fc, 0x4, 0x4, 0x4, 0x7fc, 0x7f8,
      },
      {
        0x7e0, 0x7f0, 0x18, 0xc, 0x18, 0x7f0, 0x7e0,
      },
      {
        0x7f0, 0x7fc, 0x1c, 0x78, 0x1c, 0x7fc, 0x7f0,
      },
      {
        0x60c, 0x71c, 0x1f0, 0xe0, 0x1f0, 0x71c, 0x60c,
      },
      {
        0x780, 0x7c4, 0x7c, 0x7c, 0x7c4, 0x780,
      },
      {
        0x71c, 0x63c, 0x464, 0x4c4, 0x584, 0x70c, 0x61c,
      },
      {
        0x3f8, 0x7fc, 0x464, 0x4c4, 0x584, 0x7fc, 0x3f8,
      },
      {
        0x104, 0x304, 0x7fc, 0x7fc, 0x4, 0x4,
      },
      {
        0x20c, 0x61c, 0x434, 0x464, 0x4c4, 0x78c, 0x30c,
      },
      {
        0x208, 0x60c, 0x444, 0x444, 0x444, 0x7fc, 0x3b8,
      },
      {
        0x60, 0xe0, 0x1a0, 0x324, 0x7fc, 0x7fc, 0x24,
      },
      {
        0x7c8, 0x7cc, 0x444, 0x444, 0x444, 0x47c, 0x438,
      },
      {
        0x1f8, 0x3fc, 0x644, 0x444, 0x444, 0x7c, 0x38,
      },
      {
        0x600, 0x600, 0x43c, 0x47c, 0x4c0, 0x780, 0x700,
      },
      {
        0x3b8, 0x7fc, 0x444, 0x444, 0x444, 0x7fc, 0x3b8,
      },
      {
        0x380, 0x7c4, 0x444, 0x444, 0x44c, 0x7f8, 0x3f0,
      },
      {
        0x4, 0x3c, 0x38,
      },
      {
        0x18, 0x18,
      },
      {
        0x30, 0x60, 0xc0, 0x180, 0x300, 0x600, 0xc00,
      },
      {
        0x80, 0x1c0, 0x360, 0x630, 0xc18, 0x808,
      },
      {
        0x808, 0xc18, 0x630, 0x360, 0x1c0, 0x80,
      },
      {
        0x600, 0xe00, 0x800, 0x8d8, 0x9d8, 0xf00, 0x600,
      },
      {
        0x8, 0x638, 0x630,
      },
      {
        0x200, 0x1e00, 0x1c00,
      },
      {
        0x630, 0x630,
      },
      {
        0x1c00, 0x1e00, 0x0, 0x0, 0x1e00, 0x1c00,
      },
      {
        0xff8, 0xff8, 0x808, 0x808,
      },
      {
        0x808, 0x808, 0xff8, 0xff8,
      },
      {
        0xe00, 0x700, 0x380, 0x1c0, 0xe0, 0x70, 0x38,
      },
      {
        0x80, 0x80, 0x7f0, 0xf78, 0x808, 0x808,
      },
      {
        0x808, 0x808, 0xf78, 0x7f0, 0x80, 0x80,
      },
      {
        0xf78, 0xf78,
      },
      {
        0x3000, 0x3800, 0x800,
      },
      {
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      },
      {
        0x120, 0x120, 0x120, 0x120, 0x120, 0x120,
      },
      {
        0x400, 0xc00, 0x800, 0xc00, 0x400, 0xc00, 0x800,
      },
      {
        0x700, 0xfd8, 0xfd8, 0x700,
      },
      {
        0x7f0, 0xff8, 0x808, 0x9e8, 0x9e8, 0xfe8, 0x7c0,
      },
      {
        0x220, 0xff8, 0xff8, 0x220, 0xff8, 0xff8, 0x220,
      },
      {
        0x730, 0xf98, 0x888, 0x388e, 0x388e, 0xcf8, 0x670,
      },
      {
        0x308, 0x318, 0x30, 0x60, 0xc0, 0x198, 0x318,
      },
      {
        0x400, 0xc00, 0x1800, 0x3000, 0x1800, 0xc00, 0x400,
      },
      {
        0x70, 0x6f8, 0xf88, 0x9c8, 0xf70, 0x6f8, 0x88,
      },
      {
        0x80, 0x2a0, 0x3e0, 0x1c0, 0x1c0, 0x3e0, 0x2a0, 0x80,
      },
      {
        0x3e0, 0x7f0, 0xc18, 0x808,
      },
      {
        0x808, 0xc18, 0x7f0, 0x3e0,
      },
      {
        0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2,
      },
      {
        0x80, 0x80, 0x3e0, 0x3e0, 0x80, 0x80,
      },
      {
        0x0, 0x0, 0x0,
      },
    };

    assert((sizeof(letter) / sizeof(letter[0])) <= (sizeof(letterData) / sizeof(letterData[0])));

    for (size_t i = 0; i < sizeof(letter) / sizeof(letter[0]); i++)
    {
      if (letter[i] == in)
        return letterData[i];
    }

    return NULL;
}

int CBitVis::CharHeight(const unsigned int* in)
{
  int highest = 0;
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < 32; j++)
    {
      if (in[i] & (1 << j))
      {
        if (highest < j + 1)
          highest = j + 1;
      }
    }
  }

  return highest;
}

void CBitVis::JackError(const char* jackerror)
{
  LogDebug("%s", jackerror);
}

void CBitVis::JackInfo(const char* jackinfo)
{
  LogDebug("%s", jackinfo);
}

