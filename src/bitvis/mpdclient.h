#ifndef MPDCLIENT_H
#define MPDCLIENT_H

#include <deque>
#include <string>

#include "util/thread.h"
#include "util/condition.h"
#include "util/tcpsocket.h"

class CMpdClient : public CThread
{
  public:
    CMpdClient(std::string address, int port);
    ~CMpdClient();

    virtual void Process();
    bool CurrentSong(std::string& song);
    bool IsPlaying(bool& playingchanged);
    bool GetVolume(int& volume);
    double GetElapsedState();

  private:
    bool         OpenSocket();
    bool         GetCurrentSong();
    bool         GetPlayStatus();
    void         SetCurrentSong(const std::string& song);
    void         SetSockError();
    std::string  StripFilename(const std::string& filename);

    int              m_port;
    std::string      m_address;
    CTcpClientSocket m_socket;
    CCondition       m_condition;
    std::string      m_currentsong;
    bool             m_songchanged;
    bool             m_isplaying;
    bool             m_playingchanged;
    int              m_volume;
    bool             m_volumechanged;
    double           m_elapsedstate;
};


#endif //MPDCLIENT_H
