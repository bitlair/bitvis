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

#ifndef BITVIS_H
#define BITVIS_H

#include <map>
#include <vector>
#include <deque>
#include <utility>

#include "jackclient.h"
#include "fft.h"
#include "util/tcpsocket.h"
#include "util/debugwindow.h"
#include "util/thread.h"
#include "util/condition.h"
#include "mpdclient.h"

class CBitVis : public CThread
{
  public:
    CBitVis(int argc, char *argv[]);
    ~CBitVis();

    void Setup();
    void Run();
    void Cleanup();

    virtual void Process();

  private:
    bool         m_stop;
    char*        m_address;
    int          m_port;
    char*        m_mpdaddress;
    int          m_mpdport;
    CJackClient  m_jackclient;
    int          m_signalfd;
    Cfft         m_fft;
    float*       m_buf;
    int          m_bufsize;
    float*       m_fftbuf;
    float*       m_displaybuf;
    int          m_samplecounter;
    int          m_nrffts;
    int          m_nrbins;
    int          m_nrcolumns;
    int          m_nrlines;
    int          m_fontdisplay;
    float        m_decay;
    int          m_fps;
    int          m_fontheight;
    int          m_scrolloffset;
    int64_t      m_songupdatetime;
    CMpdClient*  m_mpdclient;
    int64_t      m_volumetime;
    int          m_displayvolume;

    CCondition   m_condition;
    std::deque< std::pair<int64_t, CTcpData> > m_data;

    bool         m_debug;
    int          m_debugscale;
    CDebugWindow m_debugwindow;

    struct peak
    {
      int64_t time;
      float   value;
    };

    peak*       m_peakholds;

    CTcpClientSocket m_socket;

    std::map<char, std::vector<unsigned int> > m_glyphs;

    void SetupSignals();
    void ProcessSignalfd();
    void ProcessAudio();
    void SendData(int64_t time);
    void SetText(uint8_t* buff, const char* str, int offset = 0);
    int CharHeight(const unsigned int* in, size_t size);
    void InitChars();
    static void JackError(const char* jackerror);
    static void JackInfo(const char* jackinfo);
};

#endif //BITVIS_H
