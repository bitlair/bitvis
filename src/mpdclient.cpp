#include <cstdio>
#include <cstring>
#include <sstream>

#include "mpdclient.h"
#include "util/timeutils.h"
#include "util/lock.h"
#include "util/misc.h"
#include "util/log.h"

using namespace std;

CMpdClient::CMpdClient(std::string address, int port)
{
  m_port = port;
  m_address = address;
}

CMpdClient::~CMpdClient()
{
}

void CMpdClient::Process()
{
  while (!m_stop)
  {
    if (!m_socket.IsOpen())
    {
      if (!OpenSocket())
      {
        ClearCurrentSong();
        continue;
      }
    }

    if (!GetCurrentSong())
    {
      ClearCurrentSong();
      m_socket.Close();
    }

    USleep(1000000);
  }
}

bool CMpdClient::OpenSocket()
{
  m_socket.Close();
  int returnv = m_socket.Open(m_address, m_port, 10000000);

  if (returnv != SUCCESS)
  {
    LogError("Connecting to %s:%i, %s", m_address.c_str(), m_port, m_socket.GetError().c_str());
    m_socket.Close();

    if (returnv != TIMEOUT)
      USleep(10000000, &m_stop);

    return false;
  }
  else
  {
    Log("Connected to %s:%i", m_address.c_str(), m_port);
    return true;
  }
}

bool CMpdClient::GetCurrentSong()
{
  CTcpData data;
  data.SetData("currentsong");
  if (m_socket.Write(data) != SUCCESS)
  {
    LogError("Writing socket: %s", m_socket.GetError().c_str());
    return false;
  }

  string artist;
  string title;

  data.Clear();
  while(1)
  {
    if (m_socket.Read(data) != SUCCESS)
    {
      LogError("Reading socket: %s", m_socket.GetError().c_str());
      return false;
    }

    stringstream datastream(data.GetData());
    string line;
    while (1)
    {
      getline(datastream, line);
      if (datastream.fail())
        break;

      string word;
      if (GetWord(line, word))
      {
        if (word == "Artist:")
          artist = line.substr(1);
        else if (word == "Title:")
          title = line.substr(1);
      }

      if (!artist.empty() && !title.empty())
      {
        string song = artist + " - " + title;
        CLock lock(m_condition);
        if (song != m_currentsong)
        {
          m_currentsong = song;
          m_songchanged = true;
          lock.Leave();
          Log("Song changed to \"%s\"", m_currentsong.c_str());
        }
        return true;
      }
    }
  }

  return false;
}

void CMpdClient::ClearCurrentSong()
{
  CLock lock(m_condition);
  if (!m_currentsong.empty())
  {
    m_currentsong.clear();
    m_songchanged = true;
  }
}

bool CMpdClient::CurrentSong(std::string& song)
{
  CLock lock(m_condition);
  song = m_currentsong;
  bool songchanged = m_songchanged;
  m_songchanged = false;
  return songchanged;
}

