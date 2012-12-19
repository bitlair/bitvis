#include <cstdio>
#include <cstring>
#include <sstream>

#include "mpdclient.h"
#include "util/timeutils.h"
#include "util/lock.h"
#include "util/misc.h"

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
        m_currentsong.clear();
        continue;
      }
    }

    if (!GetCurrentSong())
    {
      m_currentsong.clear();
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
    printf("Error connecting to %s:%i, %s\n", m_address.c_str(), m_port, m_socket.GetError().c_str());
    m_socket.Close();

    if (returnv != TIMEOUT)
      USleep(10000000, &m_stop);

    return false;
  }
  else
  {
    printf("Connected to %s:%i\n", m_address.c_str(), m_port);
    return true;
  }
}

bool CMpdClient::GetCurrentSong()
{
  CTcpData data;
  data.SetData("currentsong\n");
  if (m_socket.Write(data) != SUCCESS)
  {
    printf("Error writing socket: %s\n", m_socket.GetError().c_str());
    return false;
  }

  string artist;
  string title;

  data.Clear();
  while(1)
  {
    if (m_socket.Read(data) != SUCCESS)
    {
      printf("Error reading socket: %s\n", m_socket.GetError().c_str());
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
        m_currentsong = artist + " - " + title;
        return true;
      }
    }
  }

  return false;
}

std::string CMpdClient::CurrentSong()
{
  CLock lock(m_condition);
  return m_currentsong;
}

