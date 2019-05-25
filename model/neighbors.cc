#include "neighbors.h"
#include "ns3/log.h"
#include <algorithm>

NS_LOG_COMPONENT_DEFINE ("OffchainNeighbors");

namespace ns3
{
namespace offchain
{

Neighbors::Neighbors (Time delay, uint32_t defaultDposit) : 
  m_ntimer (Timer::CANCEL_ON_DESTROY)
{
  m_ntimer.SetDelay (delay);
  m_ntimer.SetFunction (&Neighbors::Purge, this);
  m_initDeposit = defaultDposit;
  m_txErrorCallback = MakeCallback (&Neighbors::ProcessTxError, this);
  b_autoChOpen = true;
}

bool
Neighbors::IsNeighbor (Ipv4Address addr)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin ();
       i != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        return true;
    }
  return false;
}

Time
Neighbors::GetExpireTime (Ipv4Address addr)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        return (i->m_expireTime - Simulator::Now ());
    }
  return Seconds (0);
}


uint32_t 
Neighbors::GetChMyDeposit(Ipv4Address addr)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        return (i->m_totalChDeposit);
    }
    NS_LOG_LOGIC ("No available payment channel " << addr );
    return -1;
}

uint32_t 
Neighbors::GetChMyAvailDeposit(Ipv4Address addr)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        return (i->m_availChDeposit);
    }
    NS_LOG_LOGIC ("No available payment channel " << addr );
    return -1;
}

uint32_t 
Neighbors::GetChPeerDeposit(Ipv4Address addr)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        return (i->m_peerTotalChDeposit);
    }
    NS_LOG_LOGIC ("No available payment channel " << addr );
    return -1;
}

uint32_t 
Neighbors::GetChPeerAvailDeposit(Ipv4Address addr)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        return (i->m_peerAvailChDeposit);
    }
    NS_LOG_LOGIC ("No available payment channel " << addr );
    return -1;
}


void 
Neighbors::DecChDeposit(Ipv4Address addr, uint32_t pay)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        i->m_availChDeposit -= pay;
    }
    NS_LOG_LOGIC ("No available payment channel " << addr );
    return -1;
}

void 
Neighbors::IncChDeposit(Ipv4Address addr, uint32_t pay)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i)
    {
      if (i->m_neighborAddress == addr)
        i->m_availChDeposit += pay;
    }
    NS_LOG_LOGIC ("No available payment channel " << addr );
    return -1;
}

int
Neighbors::Update (Ipv4Address addr, uint32_t peerAvailAmount, Time expire, bool acked)
{
  for (std::vector<Neighbor>::iterator i = m_nb.begin (); i != m_nb.end (); ++i)
    if (i->m_neighborAddress == addr)
      {
        if(i->m_peerAvailChDeposit == peerAvailAmount) //received deposit should be same as stored one.
        {
          i->m_expireTime  = std::max (expire + Simulator::Now (), i->m_expireTime);
          return 0;
        }
        else{
          //need to redempt current balance to a main channel
          return -1;
        }

      }
  if (acked == true) //agreement for open channel from a peer
  {
    NS_LOG_LOGIC ("Open a new payment channel to " << addr);
    Neighbor neighbor (addr, m_initDeposit, peerAvailAmount, expire + Simulator::Now ());
    m_nb.push_back (neighbor);
  }
  Purge ();
}

struct CloseNeighbor
{
  bool operator() (const Neighbors::Neighbor & nb) const
  {
    return ((nb.m_expireTime < Simulator::Now ()) || nb.close);
  }
};

void
Neighbors::Purge ()
{
  if (m_nb.empty ())
    return;

  CloseNeighbor pred;
  if (!m_handleLinkFailure.IsNull ())
    {
      for (std::vector<Neighbor>::iterator j = m_nb.begin (); j != m_nb.end (); ++j)
        {
          if (pred (*j))
            {
              NS_LOG_LOGIC ("Close link to " << j->m_neighborAddress);
              m_handleLinkFailure (j->m_neighborAddress);
            }
        }
    }
  m_nb.erase (std::remove_if (m_nb.begin (), m_nb.end (), pred), m_nb.end ());
  m_ntimer.Cancel ();
  m_ntimer.Schedule ();
}

void
Neighbors::ScheduleTimer ()
{
  m_ntimer.Cancel ();
  m_ntimer.Schedule ();
}



void
Neighbors::ProcessTxError (WifiMacHeader const & hdr)
{
  Mac48Address addr = hdr.GetAddr1 ();

  for (std::vector<Neighbor>::iterator i = m_nb.begin (); i != m_nb.end (); ++i)
    {
      if (i->m_hardwareAddress == addr)
        i->close = true;
    }
  Purge ();
}
}
}
