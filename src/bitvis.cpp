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
    {
      ProcessAudio();
    }
    else
    {
      sleep(1);
    }

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
  const int bins = 1024;
  const int lines = 120;
  const float decay = 0.5;
  int samplerate;
  int samples;
  int64_t audiotime;
  if ((samples = m_jackclient.GetAudio(m_buf, m_bufsize, samplerate, audiotime)) > 0)
  {
    if (!m_socket.IsOpen())
      return;

    m_fft.Allocate(bins * 2);
    if (!m_fftbuf)
    {
      m_fftbuf = new float[bins];
      memset(m_fftbuf, 0, bins * sizeof(float));
    }

    if (!m_displaybuf)
    {
      m_displaybuf = new float[lines];
      memset(m_displaybuf, 0, lines * sizeof(float));
    }

    int additions = 0;
    for (int i = 1; i < lines; i++)
      additions += i;

    const int maxbin = Round32(15000.0f / samplerate * bins * 2.0f);

    float increase = (float)(maxbin - lines - 1) / additions;

    for (int i = 0; i < samples; i++)
    {
      m_fft.AddSample(m_buf[i]);
      m_samplecounter++;

      if (m_samplecounter % 32 == 0)
      {
        m_fft.ApplyWindow();
        fftwf_execute(m_fft.m_plan);

        m_nrffts++;
        for (int j = 0; j < bins; j++)
          m_fftbuf[j] += cabsf(m_fft.m_outbuf[j]) / m_fft.m_bufsize;
      }

      if (m_samplecounter % (samplerate / 30) == 0)
      {
        m_fft.ApplyWindow();
        fftwf_execute(m_fft.m_plan);

        //string out;
        float start = 0.0f;
        float add = 1.0f;
        for (int j = 0; j < lines; j++)
        {
          float next = start + add;

          int bin    = Round32(start) + 1;
          int nrbins = Round32(next - start);
          float outval = 0.0f;
          for (int k = bin; k < bin + nrbins; k++)
            outval += m_fftbuf[k] / m_nrffts;

          m_displaybuf[j] = m_displaybuf[j] * decay + outval * (1.0f - decay);
          //out += string(Clamp(Round32((log10(m_displaybuf[j]) * 20.0f) + 30.0f) * 1.5f, 1, 48), '|');
          //out += '\n';

          start = next;
          add += increase;
        }

        //int64_t sleeptime = audiotime + Round64(1000000.0 / (double)samplerate * (double)i) - GetTimeUs();
        //SendData(sleeptime);
        SendData(audiotime + Round64(1000000.0 / (double)samplerate * (double)i));
        //USleep(sleeptime);
        /*static int64_t prev;
        int64_t now = GetTimeUs();
        int64_t interval = now - prev;
        prev = now;*/
        //printf("samples:%i\nsleeptime:%" PRIi64"\ninterval:%" PRIi64 "\nstart\n%send\n", samples, sleeptime, interval, out.c_str());
        //fflush(stdout);

        memset(m_fftbuf, 0, bins * sizeof(float));
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

  const int lines = 48;
  const int columns = 120;

  if (!m_peakholds)
  {
    m_peakholds = new peak[columns];
    memset(m_peakholds, 0, columns * sizeof(peak));
  }

  for (int y = lines - 1; y >= 0; y--)
  {
    uint8_t line[columns / 4];
    for (int x = 0; x < columns / 4; x++)
    {
      uint8_t pixel = 0;
      for (int i = 0; i < 4; i++)
      {
        pixel <<= 2;
        int value = Round32((log10(m_displaybuf[x * 4 + i]) * 20.0f) + 55.0f) * 1.0f;
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
          if (currpeak.value >= lines)
            currpeak.value = 0.0f;
        }
      }
      line[x] = pixel;
    }
    data.SetData(line, sizeof(line), true);
  }

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

void CBitVis::JackError(const char* jackerror)
{
  LogDebug("%s", jackerror);
}

void CBitVis::JackInfo(const char* jackinfo)
{
  LogDebug("%s", jackinfo);
}

