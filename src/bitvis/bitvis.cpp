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
#include "util/lock.h"

#include <signal.h>
#include <sys/signalfd.h>
#include <math.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <complex>

using namespace std;

#define CONNECTINTERVAL 1000000

CBitVis::CBitVis(int argc, char *argv[])
{
  m_debug = false;
  m_debugscale = 2;
  m_address = NULL;
  m_port = 1337;
  m_mpdaddress = NULL;
  m_mpdport = 6600;
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
  m_fps = 30;
  m_mpdclient = NULL;
  m_scopebuf = NULL;
  m_scopebufsize = 0;
  m_scopecorrbuf = NULL;
  m_scopedisplaybuf = NULL;
  m_scopebufpos = 0;
  m_hystime = 0;
  m_hasaudio = false;
  m_peakup = false;
  m_hysstate = 0;
  m_scopemul = 1.0f;
  m_srcstate = NULL;

  m_fontheight = 0;
  InitChars();

  m_nrlines = 48;
  m_scrolloffset = 0;
  m_fontdisplay = 0;
  m_songupdatetime = GetTimeUs();
  m_volumetime = GetTimeUs();
  m_displayvolume = 0;

  const char* flags = "f:d:p:a:m:o:u";
  int c;
  while ((c = getopt(argc, argv, flags)) != -1)
  {
    if (c == 'f') //fps
    {
      int fps;
      if (!StrToInt(string(optarg), fps) || fps <= 0)
      {
        LogError("Wrong argument \"%s\" for fps", optarg);
        exit(1);
      }

      m_fps = fps;
    }
    else if (c == 'd') //debug
    {
      int scale;
      if (!StrToInt(string(optarg), scale) || scale <= 0)
      {
        LogError("Wrong argument \"%s\" for debug", optarg);
        exit(1);
      }

      m_debugscale = scale;
      m_debug = true;
    }
    else if (c == 'p') //port
    {
      int port;
      if (!StrToInt(string(optarg), port) || port <= 0 || port > 65535)
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
    else if (c == 'm') //mpd address
    {
      m_mpdaddress = optarg;
    }
    else if (c == 'o') //mpd port
    {
      int port;
      if (!StrToInt(string(optarg), port) || port <= 0 || port > 65535)
      {
        LogError("Wrong argument \"%s\" for mpd port", optarg);
        exit(1);
      }

      m_mpdport = port;
    }
    else if (c == 'u') //peak hold goes up
    {
      m_peakup = true;
    }
  }

  if (!m_address)
    m_debug = true;
}

CBitVis::~CBitVis()
{
}

void CBitVis::Setup()
{
  //init the logfile
  SetLogFile(".bitvis", "bitvis.log");

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

  m_scopebufsize = m_nrcolumns * 2;
  m_scopebuf = new float[m_scopebufsize];
  memset(m_scopebuf, 0, m_scopebufsize * sizeof(float));

  m_scopedisplaybuf = new float[m_nrcolumns];
  memset(m_scopedisplaybuf, 0, m_nrcolumns * sizeof(float));

  m_scopecorrbuf = new float[m_nrcolumns];
  memset(m_scopecorrbuf, 0, m_nrcolumns * sizeof(float));

  int error;
  m_srcstate = src_new(SRC_SINC_FASTEST, 1, &error);

  if (m_debug)
    m_debugwindow.Enable(m_nrcolumns, m_nrlines, m_debugscale);

  if (m_mpdaddress)
  {
    m_mpdclient = new CMpdClient(m_mpdaddress, m_mpdport);
    m_mpdclient->StartThread();
  }

  StartThread();
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

void CBitVis::Run()
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

    if (!m_socket.IsOpen() && m_address && GetTimeUs() - lastconnect > CONNECTINTERVAL)
    {
      CLock lock(m_socketlock);
      if (m_socket.Open(m_address, m_port, 10000000) != SUCCESS)
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

  if (m_mpdclient)
  {
    m_mpdclient->StopThread();
    delete m_mpdclient;
    m_mpdclient = NULL;
  }
}

void CBitVis::Process()
{
  //set priority to SCHED_FIFO to improve timing
  int policy;
  sched_param param;
  int returnv = pthread_getschedparam(m_thread, &policy, &param);
  if (returnv == 0)
  {
    policy = SCHED_FIFO;
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    returnv = pthread_setschedparam(m_thread, policy, &param);
    if (returnv == 0)
      Log("successfully set thread priority of transmit thread to SCHED_FIFO");
    else
      LogError("pthread_setschedparam: %s", strerror(returnv));
  }
  else
  {
    LogError("pthread_getschedparam: %s", strerror(returnv));
  }

  int64_t smoothtime = 0;

  while (!CThread::m_stop)
  {
    CLock lock(m_condition);
    while (!CThread::m_stop && m_data.empty())
      m_condition.Wait();

    if (m_data.empty())
      continue;

    int64_t  time = m_data.front().first;
    CTcpData data = m_data.front().second;
    m_data.pop_front();
    lock.Leave();

    if (smoothtime == 0)
    {
      //init smoothtime first with a timestamp
      smoothtime = time;
    }
    else
    {
      //filter out timing jitter
      smoothtime += 1000000 / m_fps;
      smoothtime += (time - smoothtime) / 100;
    }

    USleep(smoothtime - GetTimeUs());

    CLock socketlock(m_socketlock);
    if (m_socket.IsOpen() && m_socket.Write(data) != SUCCESS)
    {
      LogError("%s", m_socket.GetError().c_str());
      m_socket.Close();
    }
    socketlock.Leave();

    m_debugwindow.DisplayFrame(data);
  }
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
    int additions = 0;
    for (int i = 1; i < m_nrcolumns; i++)
      additions += i;

    const int maxbin = Round32(15000.0f / samplerate * m_nrbins * 2.0f);

    float increase = (float)(maxbin - m_nrcolumns - 1) / additions;

    for (int i = 0; i < samples; i++)
    {
      m_fft.AddSample(m_buf[i]);
      m_samplecounter++;

      const float hys = 0.01;
      if (m_hysstate == -1)
      {
        if (m_buf[i] < -hys)
          m_hysstate = 1;
      }
      else if (m_hysstate == 1)
      {
        if (m_buf[i] > hys)
        {
          m_hysstate = 0;
          m_hystime = audiotime;
          m_hasaudio = true;
        }
      }

      SRC_DATA srcdata = {};
      srcdata.data_in = m_buf + i;
      srcdata.data_out = m_scopebuf + m_scopebufpos;
      srcdata.input_frames = 1;
      srcdata.output_frames = 1;
      srcdata.src_ratio = (double)m_nrcolumns * 30.0 / samplerate;

      src_process(m_srcstate, &srcdata);

      if (srcdata.output_frames_gen)
      {
        m_scopebufpos++;

        if (m_scopebufpos == m_scopebufsize)
          m_scopebufpos = 0;
      }

      if (m_samplecounter % (samplerate / 4 / m_fps) == 0)
      {
        m_fft.ApplyWindow();
        fftwf_execute(m_fft.m_plan);

        m_nrffts++;
        for (int j = 0; j < m_nrbins; j++)
        {
          std::complex<float> bin;
          memcpy(&bin, m_fft.m_outbuf[j], sizeof(bin));
          m_fftbuf[j] += std::abs(bin) / m_fft.m_bufsize;
        }
      }

      if (m_samplecounter % (samplerate / m_fps) == 0)
      {
        m_hysstate = -1;

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

        int offset = 0;
        float maxweight = 0.0;
        for (int j = 0; j < m_scopebufsize - m_nrcolumns; j++)
        {
          float weight = 0.0f;
          for (int k = 0; k < m_nrcolumns; k++)
          {
            int pos = m_scopebufpos + j + k;
            if (pos >= m_scopebufsize)
              pos -= m_scopebufsize;

            weight += m_scopedisplaybuf[k] * m_scopebuf[pos];
          }

          if (weight > maxweight)
          {
            maxweight = weight;
            offset = j;
          }
        }

        int pos = m_scopebufpos + offset;
        if (pos >= m_scopebufsize)
          pos -= m_scopebufsize;

        int size;
        if (m_scopebufsize - pos < m_nrcolumns)
          size = m_scopebufsize - pos;
        else
          size = m_nrcolumns;

        memcpy(m_scopecorrbuf, m_scopebuf + pos, size * sizeof(float));
        if (size < m_nrcolumns)
          memcpy(m_scopecorrbuf + size, m_scopebuf, (m_nrcolumns - size) * sizeof(float));

        const float interpolant = 0.5;
        for (int j = 0; j < m_nrcolumns; j++)
          m_scopedisplaybuf[j] = m_scopedisplaybuf[j] * (1.0 - interpolant) + m_scopecorrbuf[j] * interpolant;

        if (m_hasaudio && audiotime - m_hystime > 5000000)
        {
          memset(m_scopedisplaybuf, 0, m_nrcolumns * sizeof(float));
          m_hasaudio = false;
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
  m_condition.Lock();
  AsyncStopThread();
  m_condition.Signal();
  m_condition.Unlock();

  m_jackclient.Disconnect();
  m_debugwindow.Disable();
  StopThread();
}

void CBitVis::SendData(int64_t time)
{
  CTcpData data;
  data.SetData(":00");

  int nrlines;
  bool playingchanged;
  bool isplaying = false;
  int  volume = 0;
  int elapsed = 0;
  if (m_mpdclient)
  {
    isplaying = m_mpdclient->IsPlaying(playingchanged);
    if (m_mpdclient->GetVolume(volume))
      m_volumetime = GetTimeUs();

    if (isplaying)
      elapsed = Round32(m_mpdclient->GetElapsedState() * m_nrcolumns);
  }

  if (isplaying)
  {
    if (m_fontdisplay < m_fontheight)
      m_fontdisplay++;

    nrlines = m_nrlines - m_fontdisplay;
  }
  else
  {
    if (m_fontdisplay > 0)
      m_fontdisplay--;

    nrlines = m_nrlines - m_fontdisplay;
  }

  if (GetTimeUs() - m_volumetime < 1000000)
  {
    for (int y = 0; y < nrlines; y++)
    {
      uint8_t line[m_nrcolumns / 4];
      memset(line, 0, sizeof(line));
      int pixelcounter = 3;
      for (int x = 0; x < m_nrcolumns; x++)
      {
        if (y == nrlines - 1)
        {
          line[x / 4] |= 1 << (pixelcounter * 2 + 1);
          if (x < elapsed)
            line[x / 4] |= 1 << (pixelcounter * 2);
        }
        else if (x < m_displayvolume * m_nrcolumns / 100)
        {
          if (m_displayvolume > 75)
            line[x / 4] |= 2 << (pixelcounter * 2);
          else if (m_displayvolume > 50)
            line[x / 4] |= 3 << (pixelcounter * 2);
          else
            line[x / 4] |= 1 << (pixelcounter * 2);
        }
        pixelcounter--;
        if (pixelcounter == -1)
          pixelcounter = 3;
      }
      data.SetData(line, sizeof(line), true);
    }
  }
  else
  {
    float scopemax = 0.0f;
    for (int i = 0; i < m_nrcolumns; i++)
    {
      if (fabs(m_scopedisplaybuf[i]) > scopemax)
        scopemax = fabs(m_scopedisplaybuf[i]);
    }

    float scopemul;
    if (scopemax > 0.0f)
      scopemul = Min(1.0f / scopemax, 10.0f);
    else
      scopemul = 1.0f;

    if (scopemul < m_scopemul)
      m_scopemul = scopemul;
    else
      m_scopemul = m_scopemul * 0.9 + scopemul * 0.1;

    for (int y = nrlines - 1; y >= 0; y--)
    {
      uint8_t line[m_nrcolumns / 4];
      for (int x = 0; x < m_nrcolumns / 4; x++)
      {
        uint8_t pixel = 0;
        for (int i = 0; i < 4; i++)
        {
          int pixelpos = x * 4 + i;
          int value = Round32(((log10(m_displaybuf[pixelpos]) * 20.0f) + 55.0f) / 48.0f * nrlines);

          peak& currpeak = m_peakholds[pixelpos];
          if (value >= Round32(currpeak.value))
          {
            currpeak.value = value;
            currpeak.time = time;
          }

          pixel <<= 2;

          float prev;
          float curr;
          float next;

          const float mul = 0.25f;
          const float add = 0.75f;

          curr = (m_scopedisplaybuf[pixelpos] * m_scopemul * mul + add) * nrlines;

          if (pixelpos == 0)
            prev = curr;
          else
            prev = (m_scopedisplaybuf[pixelpos - 1] * m_scopemul * mul + add) * nrlines;

          if (pixelpos == m_nrcolumns - 1)
            next = curr;
          else
            next = (m_scopedisplaybuf[pixelpos + 1] * m_scopemul * mul + add) * nrlines;

          int bounds[2] = {Round32((prev + curr) * 0.5f) - 1, Round32((next + curr) * 0.5f) - 1};

          if (y == 0)
          {
            if (m_hasaudio || isplaying)
            {
              pixel |= 2;
              if (x * 4 + i < elapsed)
                pixel |= 1;
            }
          }
          else if (Round32(currpeak.value) == y)
          {
            pixel |= 2;
          }
          else if (value > y)
          {
            pixel |= 1;
          }
          else if (m_hasaudio && ((y <= bounds[0] && y >= bounds[1]) || (y >= bounds[0] && y <= bounds[1])))
          {
            pixel |= 3;
          }
        }
        line[x] = pixel;
      }
      data.SetData(line, sizeof(line), true);
    }

    for (int i = 0; i < m_nrcolumns; i++)
    {
      peak& currpeak = m_peakholds[i];
      if (time - currpeak.time > 500000 && Round32(currpeak.value) > 0)
      {
        if (m_peakup)
        {
          currpeak.value += 0.5f;
          if (currpeak.value >= nrlines)
            currpeak.value = 0.0f;
        }
        else
        {
          currpeak.value -= 0.5f;
          if (currpeak.value <= 0.0f)
            currpeak.value = 0.0f;
        }
      }
    }
  }

  uint8_t text[m_nrcolumns / 4 * m_fontheight];
  memset(text, 0, sizeof(text));

  string currentsong;
  if ((m_mpdclient && m_mpdclient->CurrentSong(currentsong)) || playingchanged)
  {
    m_scrolloffset = 0;
    m_songupdatetime = GetTimeUs();
  }

  SetText(text, currentsong.c_str());
  if (m_fontdisplay > 0)
    data.SetData(text, m_nrcolumns / 4 * m_fontdisplay, true);

  uint8_t end[10];
  memset(end, 0, sizeof(end));
  data.SetData(end, sizeof(end), true);

  //add 10 milliseconds to the timestamp, since the current timestamp
  //might already have passed because of processing, this decreases
  //jitter in the display output
  CLock lock(m_condition);
  m_data.push_back(make_pair(time + 10000, data));
  lock.Leave();
  m_condition.Signal();

  if (volume != m_displayvolume)
  {
    m_volumetime = GetTimeUs();

    if (volume > m_displayvolume)
      m_displayvolume++;
    else if (volume < m_displayvolume)
      m_displayvolume--;
  }
}

void CBitVis::SetText(uint8_t* buff, const char* str, int offset /*= 0*/)
{
  int length = strlen(str);

  int charpos = offset + m_scrolloffset;
  int pixlength = 0;
  for (int i = 0; i < length; i++)
  {
    map<char, vector<unsigned int> >::iterator it = m_glyphs.find(str[i]);
    vector<unsigned int>& dchar = it->second;
    if (it != m_glyphs.end())
    {
      for (size_t j = 0; j < dchar.size(); j++)
      {
        if (charpos >= 0 && charpos < m_nrcolumns)
        {
          for (int k = 0; k < m_fontheight; k++)
          {
            int bit = (dchar[j] >> k) & 1;
            buff[charpos / 4 + m_nrcolumns / 4 * (m_fontheight - k - 1)] |= bit << (6 - ((charpos % 4) * 2));
          }
        }
        charpos++;
        pixlength++;
      }

      if (i < length - 1)
      {
        charpos++;
        pixlength++;
      }
    }
  }

  if (pixlength > m_nrcolumns && GetTimeUs() - m_songupdatetime > 2000000)
  {
    m_scrolloffset--;
    if (m_scrolloffset < -pixlength)
      m_scrolloffset = m_nrcolumns;
  }
}

void CBitVis::InitChars()
{
#define GLYPH(index, pixels)\
  {\
    const unsigned int* pixarr = pixels;\
    size_t nrcolumns = sizeof(pixels) / sizeof(pixels[0]);\
    std::vector<unsigned int> pixelvec;\
    int charheight = CharHeight(pixarr, nrcolumns);\
    if (m_fontheight < charheight)\
      m_fontheight = charheight;\
    for (size_t i = 0; i < nrcolumns; i++)\
      pixelvec.push_back(pixarr[i]);\
    \
    m_glyphs[index].swap(pixelvec);\
  }\

  GLYPH('a',  ((const unsigned int[]){0x18, 0xbc, 0xa4, 0xa4, 0xf8, 0x7c, 0x4}))
  GLYPH('b',  ((const unsigned int[]){0x400, 0x7fc, 0x7fc, 0x84, 0xc4, 0x7c, 0x38}))
  GLYPH('c',  ((const unsigned int[]){0x78, 0xfc, 0x84, 0x84, 0x84, 0xcc, 0x48}))
  GLYPH('d',  ((const unsigned int[]){0x38, 0x7c, 0xc4, 0x484, 0x7f8, 0x7fc, 0x4}))
  GLYPH('e',  ((const unsigned int[]){0x78, 0xfc, 0xa4, 0xa4, 0xa4, 0xec, 0x68}))
  GLYPH('f',  ((const unsigned int[]){0x44, 0x3fc, 0x7fc, 0x444, 0x600, 0x300}))
  GLYPH('g',  ((const unsigned int[]){0x72, 0xfb, 0x89, 0x89, 0x7f, 0xfe, 0x80}))
  GLYPH('h',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x40, 0x80, 0xfc, 0x7c}))
  GLYPH('i',  ((const unsigned int[]){0x84, 0x6fc, 0x6fc, 0x4}))
  GLYPH('j',  ((const unsigned int[]){0x6, 0x7, 0x1, 0x81, 0x6ff, 0x6fe}))
  GLYPH('k',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x20, 0x70, 0xdc, 0x8c}))
  GLYPH('l',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x4}))
  GLYPH('m',  ((const unsigned int[]){0xfc, 0xfc, 0xc0, 0x78, 0xc0, 0xfc, 0x7c}))
  GLYPH('n',  ((const unsigned int[]){0x80, 0xfc, 0x7c, 0x80, 0x80, 0xfc, 0x7c}))
  GLYPH('o',  ((const unsigned int[]){0x78, 0xfc, 0x84, 0x84, 0x84, 0xfc, 0x78}))
  GLYPH('p',  ((const unsigned int[]){0x81, 0xff, 0x7f, 0x89, 0x88, 0xf8, 0x70}))
  GLYPH('q',  ((const unsigned int[]){0x70, 0xf8, 0x88, 0x89, 0x7f, 0xff, 0x81}))
  GLYPH('r',  ((const unsigned int[]){0x84, 0xfc, 0x7c, 0xc4, 0x80, 0xe0, 0x60}))
  GLYPH('s',  ((const unsigned int[]){0x48, 0xec, 0xa4, 0xb4, 0x94, 0xdc, 0x48}))
  GLYPH('t',  ((const unsigned int[]){0x80, 0x80, 0x3f8, 0x7fc, 0x84, 0x8c, 0x8}))
  GLYPH('u',  ((const unsigned int[]){0xf8, 0xfc, 0x4, 0x4, 0xf8, 0xfc, 0x4}))
  GLYPH('v',  ((const unsigned int[]){0xf0, 0xf8, 0xc, 0xc, 0xf8, 0xf0}))
  GLYPH('w',  ((const unsigned int[]){0xf8, 0xfc, 0xc, 0x38, 0xc, 0xfc, 0xf8}))
  GLYPH('x',  ((const unsigned int[]){0x84, 0xcc, 0x78, 0x30, 0x78, 0xcc, 0x84}))
  GLYPH('y',  ((const unsigned int[]){0xf1, 0xf9, 0x9, 0x9, 0xb, 0xfe, 0xfc}))
  GLYPH('z',  ((const unsigned int[]){0xc4, 0xcc, 0x9c, 0xb4, 0xe4, 0xcc, 0x8c}))
  GLYPH('A',  ((const unsigned int[]){0xfc, 0x1fc, 0x320, 0x620, 0x320, 0x1fc, 0xfc}))
  GLYPH('B',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x444, 0x444, 0x7fc, 0x3b8}))
  GLYPH('C',  ((const unsigned int[]){0x1f0, 0x3f8, 0x60c, 0x404, 0x404, 0x60c, 0x318}))
  GLYPH('D',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x404, 0x60c, 0x3f8, 0x1f0}))
  GLYPH('E',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x444, 0x4e4, 0x60c, 0x71c}))
  GLYPH('F',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x444, 0x4e0, 0x600, 0x700}))
  GLYPH('G',  ((const unsigned int[]){0x1f0, 0x3f8, 0x60c, 0x424, 0x424, 0x638, 0x33c}))
  GLYPH('H',  ((const unsigned int[]){0x7fc, 0x7fc, 0x40, 0x40, 0x40, 0x7fc, 0x7fc}))
  GLYPH('I',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x404}))
  GLYPH('J',  ((const unsigned int[]){0x18, 0x1c, 0x4, 0x404, 0x7fc, 0x7f8, 0x400}))
  GLYPH('K',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x40, 0x1f0, 0x7bc, 0x60c}))
  GLYPH('L',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x404, 0x4, 0xc, 0x1c}))
  GLYPH('M',  ((const unsigned int[]){0x7fc, 0x7fc, 0x380, 0x1c0, 0x380, 0x7fc, 0x7fc}))
  GLYPH('N',  ((const unsigned int[]){0x7fc, 0x7fc, 0x380, 0x1c0, 0xe0, 0x7fc, 0x7fc}))
  GLYPH('O',  ((const unsigned int[]){0x1f0, 0x3f8, 0x60c, 0x404, 0x60c, 0x3f8, 0x1f0}))
  GLYPH('P',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x444, 0x440, 0x7c0, 0x380}))
  GLYPH('Q',  ((const unsigned int[]){0x3f0, 0x7f8, 0x408, 0x438, 0x41e, 0x7fe, 0x3f2}))
  GLYPH('R',  ((const unsigned int[]){0x404, 0x7fc, 0x7fc, 0x440, 0x460, 0x7fc, 0x39c}))
  GLYPH('S',  ((const unsigned int[]){0x318, 0x79c, 0x4c4, 0x444, 0x464, 0x73c, 0x318}))
  GLYPH('T',  ((const unsigned int[]){0x700, 0x604, 0x7fc, 0x7fc, 0x604, 0x700}))
  GLYPH('U',  ((const unsigned int[]){0x7f8, 0x7fc, 0x4, 0x4, 0x4, 0x7fc, 0x7f8}))
  GLYPH('V',  ((const unsigned int[]){0x7e0, 0x7f0, 0x18, 0xc, 0x18, 0x7f0, 0x7e0}))
  GLYPH('W',  ((const unsigned int[]){0x7f0, 0x7fc, 0x1c, 0x78, 0x1c, 0x7fc, 0x7f0}))
  GLYPH('X',  ((const unsigned int[]){0x60c, 0x71c, 0x1f0, 0xe0, 0x1f0, 0x71c, 0x60c}))
  GLYPH('Y',  ((const unsigned int[]){0x780, 0x7c4, 0x7c, 0x7c, 0x7c4, 0x780}))
  GLYPH('Z',  ((const unsigned int[]){0x71c, 0x63c, 0x464, 0x4c4, 0x584, 0x70c, 0x61c}))
  GLYPH('0',  ((const unsigned int[]){0x3f8, 0x7fc, 0x464, 0x4c4, 0x584, 0x7fc, 0x3f8}))
  GLYPH('1',  ((const unsigned int[]){0x104, 0x304, 0x7fc, 0x7fc, 0x4, 0x4}))
  GLYPH('2',  ((const unsigned int[]){0x20c, 0x61c, 0x434, 0x464, 0x4c4, 0x78c, 0x30c}))
  GLYPH('3',  ((const unsigned int[]){0x208, 0x60c, 0x444, 0x444, 0x444, 0x7fc, 0x3b8}))
  GLYPH('4',  ((const unsigned int[]){0x60, 0xe0, 0x1a0, 0x324, 0x7fc, 0x7fc, 0x24}))
  GLYPH('5',  ((const unsigned int[]){0x7c8, 0x7cc, 0x444, 0x444, 0x444, 0x47c, 0x438}))
  GLYPH('6',  ((const unsigned int[]){0x1f8, 0x3fc, 0x644, 0x444, 0x444, 0x7c, 0x38}))
  GLYPH('7',  ((const unsigned int[]){0x600, 0x600, 0x43c, 0x47c, 0x4c0, 0x780, 0x700}))
  GLYPH('8',  ((const unsigned int[]){0x3b8, 0x7fc, 0x444, 0x444, 0x444, 0x7fc, 0x3b8}))
  GLYPH('9',  ((const unsigned int[]){0x380, 0x7c4, 0x444, 0x444, 0x44c, 0x7f8, 0x3f0}))
  GLYPH(',',  ((const unsigned int[]){0x4, 0x3c, 0x38}))
  GLYPH('.',  ((const unsigned int[]){0x18, 0x18}))
  GLYPH('/',  ((const unsigned int[]){0x30, 0x60, 0xc0, 0x180, 0x300, 0x600, 0xc00}))
  GLYPH('<',  ((const unsigned int[]){0x80, 0x1c0, 0x360, 0x630, 0xc18, 0x808}))
  GLYPH('>',  ((const unsigned int[]){0x808, 0xc18, 0x630, 0x360, 0x1c0, 0x80}))
  GLYPH('?',  ((const unsigned int[]){0x600, 0xe00, 0x800, 0x8d8, 0x9d8, 0xf00, 0x600}))
  GLYPH(';',  ((const unsigned int[]){0x8, 0x638, 0x630}))
  GLYPH('\'', ((const unsigned int[]){0x200, 0x1e00, 0x1c00}))
  GLYPH(':',  ((const unsigned int[]){0x630, 0x630}))
  GLYPH('"',  ((const unsigned int[]){0x1c00, 0x1e00, 0x0, 0x0, 0x1e00, 0x1c00}))
  GLYPH('[',  ((const unsigned int[]){0xff8, 0xff8, 0x808, 0x808}))
  GLYPH(']',  ((const unsigned int[]){0x808, 0x808, 0xff8, 0xff8}))
  GLYPH('\\', ((const unsigned int[]){0xe00, 0x700, 0x380, 0x1c0, 0xe0, 0x70, 0x38}))
  GLYPH('{',  ((const unsigned int[]){0x80, 0x80, 0x7f0, 0xf78, 0x808, 0x808}))
  GLYPH('}',  ((const unsigned int[]){0x808, 0x808, 0xf78, 0x7f0, 0x80, 0x80}))
  GLYPH('|',  ((const unsigned int[]){0xf78, 0xf78}))
  GLYPH('`',  ((const unsigned int[]){0x3000, 0x3800, 0x800}))
  GLYPH('-',  ((const unsigned int[]){0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}))
  GLYPH('=',  ((const unsigned int[]){0x120, 0x120, 0x120, 0x120, 0x120, 0x120}))
  GLYPH('~',  ((const unsigned int[]){0x400, 0xc00, 0x800, 0xc00, 0x400, 0xc00, 0x800}))
  GLYPH('!',  ((const unsigned int[]){0x700, 0xfd8, 0xfd8, 0x700}))
  GLYPH('@',  ((const unsigned int[]){0x7f0, 0xff8, 0x808, 0x9e8, 0x9e8, 0xfe8, 0x7c0}))
  GLYPH('#',  ((const unsigned int[]){0x220, 0xff8, 0xff8, 0x220, 0xff8, 0xff8, 0x220}))
  GLYPH('$',  ((const unsigned int[]){0x730, 0xf98, 0x888, 0x388e, 0x388e, 0xcf8, 0x670}))
  GLYPH('%',  ((const unsigned int[]){0x308, 0x318, 0x30, 0x60, 0xc0, 0x198, 0x318}))
  GLYPH('^',  ((const unsigned int[]){0x400, 0xc00, 0x1800, 0x3000, 0x1800, 0xc00, 0x400}))
  GLYPH('&',  ((const unsigned int[]){0x70, 0x6f8, 0xf88, 0x9c8, 0xf70, 0x6f8, 0x88}))
  GLYPH('*',  ((const unsigned int[]){0x80, 0x2a0, 0x3e0, 0x1c0, 0x1c0, 0x3e0, 0x2a0, 0x80}))
  GLYPH('(',  ((const unsigned int[]){0x3e0, 0x7f0, 0xc18, 0x808}))
  GLYPH(')',  ((const unsigned int[]){0x808, 0xc18, 0x7f0, 0x3e0}))
  GLYPH('_',  ((const unsigned int[]){0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2}))
  GLYPH('+',  ((const unsigned int[]){0x80, 0x80, 0x3e0, 0x3e0, 0x80, 0x80}))
  GLYPH(' ',  ((const unsigned int[]){0x0, 0x0, 0x0}))
}

int CBitVis::CharHeight(const unsigned int* in, size_t size)
{
  int highest = 0;
  for (size_t i = 0; i < size; i++)
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

