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
        CLock lock(m_condition);
        m_commands.clear();
        continue;
      }
    }

    CLock lock(m_condition);
    if (m_commands.empty())
    {
      if (!m_condition.Wait(10000000))
      {
        lock.Leave();
        if (!Ping())
        {
          m_socket.Close();
          lock.Enter();
          m_commands.clear();
          continue;
        }
      }
    }
    lock.Leave();

    if (!ProcessCommands())
    {
      m_socket.Close();
      lock.Enter();
      m_commands.clear();
    }
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

bool CMpdClient::ProcessCommands()
{
  CLock lock(m_condition);
  while (!m_commands.empty())
  {
    ECMD cmd = m_commands.front();
    m_commands.pop_front();
    lock.Leave();

    int volume;
    if (!GetVolume(volume))
      return false;

    int newvolume = volume;
    if (cmd == CMD_VOLUP)
      newvolume += 5;
    else if (cmd == CMD_VOLDOWN)
      newvolume -= 5;

    newvolume = Clamp(newvolume, 0, 100);
    printf("Setting volume from %i to %i\n", volume, newvolume);

    if (!SetVolume(newvolume))
      return false;

    lock.Enter();
  }

  return true;
}

bool CMpdClient::Ping()
{
  CTcpData data;
  data.SetData("ping\n");

  if (m_socket.Write(data) != SUCCESS)
  {
    printf("Error writing socket: %s\n", m_socket.GetError().c_str());
    return false;
  }

  m_socket.SetTimeout(0);
  int returnv;
  while((returnv = m_socket.Read(data)) == SUCCESS);

  m_socket.SetTimeout(10000000);

  if (returnv == FAIL)
  {
    printf("Error reading socket: %s\n", m_socket.GetError().c_str());
    return false;
  }

  return true;
}

bool CMpdClient::GetVolume(int& volume)
{
  CTcpData data;
  data.SetData("status\n");
  if (m_socket.Write(data) != SUCCESS)
  {
    printf("Error writing socket: %s\n", m_socket.GetError().c_str());
    return false;
  }

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
      if (GetWord(line, word) && word == "volume:")
      {
        if (GetWord(line, word) && StrToInt(word, volume) && volume >= 0 && volume <= 100)
          return true;
      }
    }
  }

  return false;
}

bool CMpdClient::SetVolume(int volume)
{
  CTcpData data;
  data.SetData(string("setvol ") + ToString(volume) + "\n");

  if (m_socket.Write(data) != SUCCESS)
  {
    printf("Error writing socket: %s\n", m_socket.GetError().c_str());
    return false;
  }

  return true;
}

void CMpdClient::VolumeUp()
{
  CLock lock(m_condition);
  m_commands.push_back(CMD_VOLUP);
  m_condition.Signal();
}

void CMpdClient::VolumeDown()
{
  CLock lock(m_condition);
  m_commands.push_back(CMD_VOLDOWN);
  m_condition.Signal();
}

