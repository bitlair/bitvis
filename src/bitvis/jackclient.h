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

#ifndef JACKCLIENT_H
#define JACKCLIENT_H

#include <string>
#include <vector>
#include <jack/jack.h>
#include <samplerate.h>

#include "fft.h"
#include "clientmessage.h"
#include "util/condition.h"
#include "util/inclstdint.h"

class CJackClient
{
  public:
    CJackClient();
    ~CJackClient();

    bool Connect();
    void Disconnect();
    bool IsConnected() { return m_connected;   }
    int  MsgPipe()     { return m_pipe[0];     }
    ClientMessage GetMessage();

    jack_status_t      ExitStatus() { return m_exitstatus; }
    const std::string& ExitReason() { return m_exitreason; }

    int                Samplerate() { return m_samplerate; }
    int                GetAudio(float*& buf, int& bufsize, int& samplerate, int64_t& audiotime);

  private:
    bool           m_connected;
    bool           m_wasconnected;
    jack_client_t* m_client;
    jack_port_t*   m_jackport;
    std::string    m_name;
    int            m_samplerate;
    int            m_outsamplerate;
    jack_status_t  m_exitstatus;
    std::string    m_exitreason;
    int            m_portevents;
    int            m_pipe[2];
    CCondition     m_condition;
    float*         m_buf[2];
    int            m_bufsize[2];
    int            m_outsamples[2];
    int64_t        m_audiotime[2];
    SRC_STATE*     m_srcstate;

    bool        ConnectInternal();
    void        CheckMessages();
    bool        WriteMessage(uint8_t message);

    static int  SJackProcessCallback(jack_nframes_t nframes, void *arg);
    void        PJackProcessCallback(jack_nframes_t nframes);

    static void SJackInfoShutdownCallback(jack_status_t code, const char *reason, void *arg);
    void        PJackInfoShutdownCallback(jack_status_t code, const char *reason);
};

#endif //JACKCLIENT_H
