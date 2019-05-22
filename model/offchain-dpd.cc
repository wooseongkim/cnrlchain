#include "offchain-dpd.h"

namespace ns3
{
namespace offchain
{

bool
DuplicatePacketDetection::IsDuplicate  (Ptr<const Packet> p, const Ipv4Header & header)
{
  return m_idCache.IsDuplicate (header.GetSource (), p->GetUid () );
}
void
DuplicatePacketDetection::SetLifetime (Time lifetime)
{
  m_idCache.SetLifetime (lifetime);
}

Time
DuplicatePacketDetection::GetLifetime () const
{
  return m_idCache.GetLifeTime ();
}


}
}
