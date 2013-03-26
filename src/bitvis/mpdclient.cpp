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
  m_isplaying = false;
  m_playingchanged = false;
  m_volume = 0;
  m_volumechanged = false;
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
        continue;
    }

    if (!GetCurrentSong() || !GetPlayStatus())
    {
      m_socket.Close();
      USleep(10000000, &m_stop);
    }
    else
    {
      USleep(30000);
    }
  }

  m_socket.Close();
}

bool CMpdClient::OpenSocket()
{
  m_socket.Close();
  int returnv = m_socket.Open(m_address, m_port, 10000000);

  if (returnv != SUCCESS)
  {
    SetSockError();
    LogError("Connecting to %s:%i, %s", m_address.c_str(), m_port, m_socket.GetError().c_str());
    m_socket.Close();

    if (returnv != TIMEOUT)
      USleep(10000000, &m_stop);

    return false;
  }
  else
  {
    Log("Connected to %s:%i", m_address.c_str(), m_port);
    SetCurrentSong("Connected to " + m_address + " " + ToString(m_port));
    return true;
  }
}

bool CMpdClient::GetCurrentSong()
{
  CTcpData data;
  data.SetData("currentsong\n");
  if (m_socket.Write(data) != SUCCESS)
  {
    SetSockError();
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
      SetSockError();
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

      string tmpline = line;
      string word;
      if (GetWord(tmpline, word))
      {
        if (word == "Artist:")
          artist = tmpline.substr(1);
        else if (word == "Title:")
          title = tmpline.substr(1);
      }

      if (line == "OK")
      {
        string songtext;
        if (artist.empty() && title.empty())
        {
          songtext = "Wat? No title?";
        }
        else
        {
          if (artist.empty())
            artist = "Unknown artist";
          if (title.empty())
            title = "Unknown title";

          songtext = artist + " - " + title;
        }

        SetCurrentSong(songtext);
        return true;
      }
    }
  }

  SetCurrentSong("Unable to get song info");
  return false;
}

bool CMpdClient::GetPlayStatus()
{
  CTcpData data;
  data.SetData("status\n");
  if (m_socket.Write(data) != SUCCESS)
  {
    SetSockError();
    LogError("Writing socket: %s", m_socket.GetError().c_str());
    return false;
  }

  data.Clear();
  bool isplaying = false;
  int  volume = -1;
  while(1)
  {
    if (m_socket.Read(data) != SUCCESS)
    {
      SetSockError();
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

      string tmpline = line;
      string word;
      if (GetWord(tmpline, word))
      {
        if (word == "state:")
        {
          if (GetWord(tmpline, word))
            if (word == "play")
              isplaying = true;
        }
        else if (word == "volume:")
        {
          int parsevolume;
          if (GetWord(tmpline, word) && StrToInt(word, parsevolume))
            volume = parsevolume;
        }
      }

      if (line == "OK")
      {
        if (volume == 0)
          isplaying = false;

        CLock lock(m_condition);
        if (m_isplaying == false && isplaying == true)
          m_playingchanged = true;

        m_isplaying = isplaying;

        if (volume != -1 && m_volume != volume)
        {
          m_volume = volume;
          m_volumechanged = true;
        }

        return true;
      }
    }
  }

  SetCurrentSong("Unable to get play status");
  m_isplaying = true; //to make the error message show on the led display

  return false;
}

void CMpdClient::SetCurrentSong(const std::string& song)
{
  CLock lock(m_condition);
  if (song != m_currentsong)
  {
    m_currentsong = song;
    m_songchanged = true;

    lock.Leave();
    Log("Song changed to \"%s\"", song.c_str());
  }
}

void CMpdClient::SetSockError()
{
  SetCurrentSong(m_socket.GetError());
  m_isplaying = true; //to make the error message show on the led display
}

bool CMpdClient::CurrentSong(std::string& song)
{
  CLock lock(m_condition);
  song = m_currentsong;
  bool songchanged = m_songchanged;
  m_songchanged = false;
  return songchanged;
}

bool CMpdClient::IsPlaying(bool& playingchanged)
{
  CLock lock(m_condition);
  playingchanged = m_playingchanged;
  m_playingchanged = false;
  return m_isplaying;
}

bool CMpdClient::GetVolume(int& volume)
{
  CLock lock(m_condition);
  volume = m_volume;
  bool changed = m_volumechanged;
  m_volumechanged = false;
  return changed;
}
