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
    std::string  CurrentSong();

  private:
    bool         OpenSocket();
    bool         GetCurrentSong();

    int              m_port;
    std::string      m_address;
    CTcpClientSocket m_socket;
    CCondition       m_condition;
    std::string      m_currentsong;
};


#endif //MPDCLIENT_H
