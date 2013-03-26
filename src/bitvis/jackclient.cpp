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

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE //for pipe2
#endif //_GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <complex.h>
#include <fftw3.h>

#include "util/inclstdint.h"
#include "util/misc.h"
#include "util/timeutils.h"
#include "util/log.h"
#include "util/lock.h"

#include "jackclient.h"
#include "fft.h"

using namespace std;

CJackClient::CJackClient()
{
  m_name          = "bitvis";
  m_client        = NULL;
  m_jackport      = NULL;
  m_connected     = false;
  m_wasconnected  = true;
  m_exitstatus    = (jack_status_t)0;
  m_samplerate    = 0;
  m_outsamplerate = 40000;
  m_srcstate      = NULL;

  for (int i = 0; i < 2; i++)
  {
    m_buf[i]        = NULL;
    m_bufsize[i]    = 0;
    m_outsamples[i] = 0;
    m_audiotime[i]  = 0;
  }

  if (pipe2(m_pipe, O_NONBLOCK) == -1)
  {
    LogError("creating msg pipe for client \"%s\": %s", m_name.c_str(), GetErrno().c_str());
    m_pipe[0] = m_pipe[1] = -1;
  }
}

CJackClient::~CJackClient()
{
  Disconnect();

  if (m_pipe[0] != -1)
    close(m_pipe[0]);
  if (m_pipe[1] != -1)
    close(m_pipe[1]);
}

bool CJackClient::Connect()
{
  m_connected = ConnectInternal();
  if (!m_connected)
    Disconnect();
  else
    m_wasconnected = true;

  return m_connected;
}

bool CJackClient::ConnectInternal()
{
  if (m_connected)
    return true; //already connected

  LogDebug("Connecting client \"%s\" to jackd", m_name.c_str());

  //this is set in PJackInfoShutdownCallback(), init to 0 here so we know when the jack thread has exited
  m_exitstatus = (jack_status_t)0; 
  m_exitreason.clear();

  //try to connect to jackd
  m_client = jack_client_open(m_name.substr(0, jack_client_name_size() - 1).c_str(), JackNoStartServer, NULL);
  if (m_client == NULL)
  {
    if (m_wasconnected || g_printdebuglevel)
    {
      LogError("Client \"%s\" error connecting to jackd: \"%s\"", m_name.c_str(), GetErrno().c_str());
      m_wasconnected = false; //only print this to the log once
    }
    return false;
  }

  //we want to know when the jack thread shuts down, so we can restart it
  jack_on_info_shutdown(m_client, SJackInfoShutdownCallback, this);

  m_samplerate = jack_get_sample_rate(m_client);

  Log("Client \"%s\" connected to jackd, got name \"%s\", samplerate %" PRIi32,
      m_name.c_str(), jack_get_client_name(m_client), m_samplerate);

  int returnv;

  //SJackProcessCallback gets called when jack has new audio data to process
  returnv = jack_set_process_callback(m_client, SJackProcessCallback, this);
  if (returnv != 0)
  {
    LogError("Client \"%s\" error %i setting process callback: \"%s\"",
             m_name.c_str(), returnv, GetErrno().c_str());
    return false;
  }

  m_jackport = jack_port_register(m_client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  if (m_jackport == NULL)
  {
    Log("Error registering jack port: %s", GetErrno().c_str());
    return false;
  }

  //initialize the resampler
  int error;
  m_srcstate = src_new(SRC_SINC_FASTEST, 1, &error);

  //alloc one second buffers
  for (int i = 0; i < 2; i++)
  {
    m_bufsize[i] = m_outsamplerate;
    m_buf[i]     = (float*)malloc(m_bufsize[i] * sizeof(float));
  }

  //everything set up, activate
  returnv = jack_activate(m_client);
  if (returnv != 0)
  {
    LogError("Client \"%s\" error %i activating client: \"%s\"",
             m_name.c_str(), returnv, GetErrno().c_str());
    return false;
  }

  return true;
}

void CJackClient::Disconnect()
{
  if (m_client)
  {
    //deactivate the client before everything else
    int returnv = jack_deactivate(m_client);
    if (returnv != 0)
      LogError("Client \"%s\" error %i deactivating client: \"%s\"",
               m_name.c_str(), returnv, GetErrno().c_str());

    //close the jack client
    returnv = jack_client_close(m_client);
    if (returnv != 0)
      LogError("Client \"%s\" error %i closing client: \"%s\"",
               m_name.c_str(), returnv, GetErrno().c_str());

    m_client = NULL;
  }

  m_connected  = false;
  m_exitstatus = (jack_status_t)0;
  m_samplerate = 0;

  for (int i = 0; i < 2; i++)
  {
    free(m_buf[i]);
    m_buf[i] = NULL;
    m_bufsize[i] = 0;
  }

  if (m_srcstate)
  {
    src_delete(m_srcstate);
    m_srcstate = NULL;
  }
}

//returns true when the message has been sent or pipe is broken
//returns false when the message write needs to be retried
bool CJackClient::WriteMessage(uint8_t msg)
{
  if (m_pipe[1] == -1)
    return true; //can't write

  int returnv = write(m_pipe[1], &msg, 1);
  if (returnv == 1)
    return true; //write successful

  if (returnv == -1 && errno != EAGAIN)
  {
    LogError("Client \"%s\" error writing msg %s to pipe: \"%s\"", m_name.c_str(), MsgToString(msg), GetErrno().c_str());
    if (errno != EINTR)
    {
      close(m_pipe[1]);
      m_pipe[1] = -1;
      return true; //pipe broken
    }
  }

  return false; //need to try again
}

ClientMessage CJackClient::GetMessage()
{
  if (m_pipe[0] == -1)
    return MsgNone;

  uint8_t msg;
  int returnv = read(m_pipe[0], &msg, 1);
  if (returnv == 1)
  {
    return (ClientMessage)msg;
  }
  else if (returnv == -1 && errno != EAGAIN)
  {
    LogError("Client \"%s\" error reading msg from pipe: \"%s\"", m_name.c_str(), GetErrno().c_str());
    if (errno != EINTR)
    {
      close(m_pipe[0]);
      m_pipe[0] = -1;
    }
  }

  return MsgNone;
}

int CJackClient::SJackProcessCallback(jack_nframes_t nframes, void *arg)
{
  ((CJackClient*)arg)->PJackProcessCallback(nframes);
  return 0;
}

void CJackClient::PJackProcessCallback(jack_nframes_t nframes)
{
  int64_t now = GetTimeUs();
  int outsamples = Round32((double)nframes / m_samplerate * m_outsamplerate + 2.0);

  //use a trylock, so the realtime jack thread doesn't block
  CLock lock(m_condition, true);

  int index;
  if (lock.HasLock())
  {
    //the mutex is locked, copy samples from the temp buffer into the main buffer
    if (m_outsamples[1] > 0)
    {
      //if the main buffer is empty, use the time from the temp buffer
      if (m_outsamples[0] == 0)
        m_audiotime[0] = m_audiotime[1];

      //copy as many samples as possible from the temp buffer to the main buffer
      int size = Min(m_outsamples[1], m_bufsize[0] - m_outsamples[0]);
      memcpy(m_buf[0] + m_outsamples[0], m_buf[1], size * sizeof(float));
      m_outsamples[1] -= size;
      m_outsamples[0] += size;
    }

    //if the main buffer has enough room, put the audio samples there
    //otherwise put them into the temp buffer
    if (m_bufsize[0] - m_outsamples[0] >= outsamples)
      index = 0;
    else
      index = 1;
  }
  else
  {
    //the mutex couldn't be locked, copy samples into the temp buffer
    index = 1;
  }

  if (m_bufsize[index] - m_outsamples[index] < outsamples)
    return; //no room, drop the samples

  if (m_outsamples[index] == 0)
    m_audiotime[index] = now;

  float* jackptr = (float*)jack_port_get_buffer(m_jackport, nframes);

  if (m_outsamplerate != m_samplerate)
  {
    SRC_DATA srcdata = {};
    srcdata.data_in = jackptr;
    srcdata.data_out = m_buf[index] + m_outsamples[index];
    srcdata.input_frames = nframes;
    srcdata.output_frames = outsamples;
    srcdata.src_ratio = (double)m_outsamplerate / m_samplerate;

    src_process(m_srcstate, &srcdata);
    m_outsamples[index] += srcdata.output_frames_gen;

    if (srcdata.input_frames_used < (int)nframes)
      Log("WARNING: %i out of %i frames used", (int)srcdata.input_frames_used, (int)nframes);
  }
  else
  {
    memcpy(m_buf[index] + m_outsamples[index], jackptr, nframes * sizeof(float));
    m_outsamples[index] += nframes;
  }

  lock.Leave();
  m_condition.Signal();
}

int CJackClient::GetAudio(float*& buf, int& bufsize, int& samplerate, int64_t& audiotime)
{
  CLock lock(m_condition);
  m_condition.Wait(1000000, m_outsamples[0], 0);

  if (m_outsamples[0] == 0)
    return 0;

  samplerate = m_outsamplerate;

  if (bufsize < m_outsamples[0])
  {
    bufsize = m_outsamples[0];
    buf = (float*)realloc(buf, bufsize * sizeof(float));
  }

  memcpy(buf, m_buf[0], m_outsamples[0] * sizeof(float));

  int outsamples = m_outsamples[0];
  m_outsamples[0] = 0;

  audiotime = m_audiotime[0];

  return outsamples;
}

void CJackClient::SJackInfoShutdownCallback(jack_status_t code, const char *reason, void *arg)
{
  ((CJackClient*)arg)->PJackInfoShutdownCallback(code, reason);
}

void CJackClient::PJackInfoShutdownCallback(jack_status_t code, const char *reason)
{
  //save the exit code, this will be read from the loop in main()
  //make sure reason is saved before code, to make it thread safe
  //since main() will read m_exitstatus first, then m_exitreason if necessary
  m_exitreason = reason;
  m_exitstatus = code;

  //send message to the main loop
  //try for one second to make sure it gets there
  int64_t start = GetTimeUs();
  do
  {
    if (WriteMessage(MsgExited))
      return;

    USleep(100); //don't busy spin
  }
  while (GetTimeUs() - start < 1000000);

  LogError("Client \"%s\" unable to write exit msg to pipe", m_name.c_str());
}

