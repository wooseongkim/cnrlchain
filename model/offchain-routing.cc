
#include "offchain-routing.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-net-device.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include <algorithm>
#include <limits>


NS_LOG_COMPONENT_DEFINE ("OffchainRoutingProtocol");

namespace ns3
{
namespace offchain
{
NS_OBJECT_ENSURE_REGISTERED (RoutingProtocol);

const uint32_t RoutingProtocol::OFFCHAIN_PORT = 1200;


void
RoutingProtocol::SendHello ()
{
  NS_LOG_FUNCTION (this);

  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;
      uint32_t  curDeposit = m_nb.GetChAvailDeposit();
      if(curDeposit < 0)
      {
          
      }
      HelloHeader HelloHeader(/*dst=*/ iface.GetLocal (), /*dst seqno=*/ m_seqNo, /*origin=*/ iface.GetLocal (), 
                                            /*lifetime=*/ Time (AllowedHelloLoss * HelloInterval), curDeposit);                                      
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (helloHeader);
      TypeHeader tHeader (OFFCHAIN_TYPE_HELLO);
      packet->AddHeader (tHeader);
      // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
      Ipv4Address destination;
      if (iface.GetMask () == Ipv4Mask::GetOnes ())
        {
          destination = Ipv4Address ("255.255.255.255");
        }
      else
        { 
          destination = iface.GetBroadcast ();
        }
      socket->SendTo (packet, 0, InetSocketAddress (destination, OFFCHAIN_PORT));
    }
}

void
RoutingProtocol::ProcessHello (RrepHeader const & rrepHeader, Ipv4Address receiver )
{
  NS_LOG_FUNCTION (this << "from " << rrepHeader.GetDst ());
  /*
   *  Whenever a node receives a Hello message from a neighbor, the node
   * SHOULD make sure that it has an active route to the neighbor, and
   * create one if necessary.
   */
  RoutingTableEntry toNeighbor;
  if (!m_routingTable.LookupRoute (rrepHeader.GetDst (), toNeighbor))
    {
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
      RoutingTableEntry newEntry (/*device=*/ dev, /*dst=*/ rrepHeader.GetDst (), /*validSeqNo=*/ true, /*seqno=*/ rrepHeader.GetDstSeqno (),
                                              /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
                                              /*hop=*/ 1, /*nextHop=*/ rrepHeader.GetDst (), /*lifeTime=*/ rrepHeader.GetLifeTime ());
      m_routingTable.AddRoute (newEntry);
    }
  else
    {
      toNeighbor.SetLifeTime (std::max (Time (AllowedHelloLoss * HelloInterval), toNeighbor.GetLifeTime ()));
      toNeighbor.SetSeqNo (rrepHeader.GetDstSeqno ());
      toNeighbor.SetValidSeqNo (true);
      toNeighbor.SetFlag (VALID);
      toNeighbor.SetOutputDevice (m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver)));
      toNeighbor.SetInterface (m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
      m_routingTable.Update (toNeighbor);
    }
  if (EnableHello)
    {
      m_nb.Update (rrepHeader.GetDst (), Time (AllowedHelloLoss * HelloInterval));
    }
}


void
RoutingProtocol::SendRequest (Ipv4Address dst)
{
  NS_LOG_FUNCTION ( this << dst);
  // A node SHOULD NOT originate more than RREQ_RATELIMIT RREQ messages per second.
  if (m_rreqCount == RreqRateLimit)
    {
      Simulator::Schedule (m_rreqRateLimitTimer.GetDelayLeft () + MicroSeconds (100),
                           &RoutingProtocol::SendRequest, this, dst);
      return;
    }
  else
    m_rreqCount++;
  // Create RREQ header
  RreqHeader rreqHeader;
  rreqHeader.SetDst (dst);

  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (dst, rt))
    {
      rreqHeader.SetHopCount (rt.GetHop ());
      if (rt.GetValidSeqNo ())
        rreqHeader.SetDstSeqno (rt.GetSeqNo ());
      else
        rreqHeader.SetUnknownSeqno (true);
      rt.SetFlag (IN_SEARCH);
      m_routingTable.Update (rt);
    }
  else
    {
      rreqHeader.SetUnknownSeqno (true);
      Ptr<NetDevice> dev = 0;
      RoutingTableEntry newEntry (/*device=*/ dev, /*dst=*/ dst, /*validSeqNo=*/ false, /*seqno=*/ 0,
                                              /*iface=*/ Ipv4InterfaceAddress (),/*hop=*/ 0,
                                              /*nextHop=*/ Ipv4Address (), /*lifeTime=*/ Seconds (0));
      newEntry.SetFlag (IN_SEARCH);
      m_routingTable.AddRoute (newEntry);
    }

  if (GratuitousReply)
    rreqHeader.SetGratiousRrep (true);
  if (DestinationOnly)
    rreqHeader.SetDestinationOnly (true);

  m_seqNo++;
  rreqHeader.SetOriginSeqno (m_seqNo);
  m_requestId++;
  rreqHeader.SetId (m_requestId);
  rreqHeader.SetHopCount (0);

  // Send RREQ as subnet directed broadcast from each interface used by aodv
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
         m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;

      rreqHeader.SetOrigin (iface.GetLocal ());
      m_rreqIdCache.IsDuplicate (iface.GetLocal (), m_requestId);

      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (rreqHeader);
      TypeHeader tHeader (OFFCHAIN_TYPE_RREQ);
      packet->AddHeader (tHeader);
      // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
      Ipv4Address destination;
      if (iface.GetMask () == Ipv4Mask::GetOnes ())
        {
          destination = Ipv4Address ("255.255.255.255");
        }
      else
        { 
          destination = iface.GetBroadcast ();
        }
      NS_LOG_DEBUG ("Send RREQ with id " << rreqHeader.GetId () << " to socket");
      socket->SendTo (packet, 0, InetSocketAddress (destination, OFFCHAIN_PORT));
    }
  ScheduleRreqRetry (dst);
  if (EnableHello)
    {
      if (!m_htimer.IsRunning ())
        {
          m_htimer.Cancel ();
          m_htimer.Schedule (HelloInterval - Time (0.01 * MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))));
        }
    }
}


void
RoutingProtocol::RecvRequest (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)
{
  NS_LOG_FUNCTION (this);
  RreqHeader rreqHeader;
  p->RemoveHeader (rreqHeader);

  // A node ignores all RREQs received from any node in its blacklist
  RoutingTableEntry toPrev;
  if (m_routingTable.LookupRoute (src, toPrev))
    {
      if (toPrev.IsUnidirectional ())
        {
          NS_LOG_DEBUG ("Ignoring RREQ from node in blacklist");
          return;
        }
    }

  uint32_t id = rreqHeader.GetId ();
  Ipv4Address origin = rreqHeader.GetOrigin ();

  /*
   *  Node checks to determine whether it has received a RREQ with the same Originator IP Address and RREQ ID.
   *  If such a RREQ has been received, the node silently discards the newly received RREQ.
   */
  if (m_rreqIdCache.IsDuplicate (origin, id))
    {
      NS_LOG_DEBUG ("Ignoring RREQ due to duplicate");
      return;
    }

  // Increment RREQ hop count
  uint8_t hop = rreqHeader.GetHopCount () + 1;
  rreqHeader.SetHopCount (hop);

  // transaction amount
  uint32_t amount = rreqHeader.GetTransAmount ();
  /*
   *  When the reverse route is created or updated, the following actions on the route are also carried out:
   *  1. the Originator Sequence Number from the RREQ is compared to the corresponding destination sequence number
   *     in the route table entry and copied if greater than the existing value there
   *  2. the valid sequence number field is set to true;
   *  3. the next hop in the routing table becomes the node from which the  RREQ was received
   *  4. the hop count is copied from the Hop Count in the RREQ message;
   *  5. the Lifetime is set to be the maximum of (ExistingLifetime, MinimalLifetime), where
   *     MinimalLifetime = current time + 2*NetTraversalTime - 2*HopCount*NodeTraversalTime
   */
  RoutingTableEntry toOrigin;
  if (!m_routingTable.LookupRoute (origin, toOrigin))
    {
      RoutingTableEntry newEntry (/*dst=*/ origin, /*validSeno=*/ true, /*seqNo=*/ rreqHeader.GetOriginSeqno (),
                                              /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0), /*hops=*/ hop,
                                              /*transaction*/ amount, /*nextHop*/ src, /*timeLife=*/ Time ((2 * NetTraversalTime - 2 * hop * NodeTraversalTime)));
      m_routingTable.AddRoute (newEntry);
    }
  else
    {
      if (toOrigin.GetValidSeqNo ())
        {
          if (int32_t (rreqHeader.GetOriginSeqno ()) - int32_t (toOrigin.GetSeqNo ()) > 0)
            toOrigin.SetSeqNo (rreqHeader.GetOriginSeqno ());
        }
      else
        toOrigin.SetSeqNo (rreqHeader.GetOriginSeqno ());
      toOrigin.SetValidSeqNo (true);
      toOrigin.SetNextHop (src);
      toOrigin.SetOutputDevice (m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver)));
      toOrigin.SetInterface (m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
      toOrigin.SetHop (hop);
      toOrigin.SetTransAmount (amount);
      toOrigin.SetLifeTime (std::max (Time (2 * NetTraversalTime - 2 * hop * NodeTraversalTime),
                                      toOrigin.GetLifeTime ()));
      m_routingTable.Update (toOrigin);
    }
  NS_LOG_LOGIC (receiver << " receive RREQ with hop count " << static_cast<uint32_t>(rreqHeader.GetHopCount ()) 
                         << " ID " << rreqHeader.GetId ()
                         << " to destination " << rreqHeader.GetDst ()
                         << " Transaction amount " << rreqHeader.GetTransAmount ());

  //  A node generates a RREP if either:
  //  (i)  it is itself the destination,
  if (IsMyOwnAddress (rreqHeader.GetDst ()))
    {
      m_routingTable.LookupRoute (origin, toOrigin);
      NS_LOG_DEBUG ("Send reply since I am the destination");
      SendReply (rreqHeader, toOrigin);
      return;
    }
  /*
   * (ii) or it has an active route to the destination, the destination sequence number in the node's existing route table entry for the destination
   *      is valid and greater than or equal to the Destination Sequence Number of the RREQ, and the "destination only" flag is NOT set.
   */
  RoutingTableEntry toDst;
  Ipv4Address dst = rreqHeader.GetDst ();
  if (m_routingTable.LookupRoute (dst, toDst))
    {
      /*
       * Drop RREQ, This node RREP wil make a loop.
       */
      if (toDst.GetNextHop () == src)
        {
          NS_LOG_DEBUG ("Drop RREQ from " << src << ", dest next hop " << toDst.GetNextHop ());
          return;
        }
      /*
       * The Destination Sequence number for the requested destination is set to the maximum of the corresponding value
       * received in the RREQ message, and the destination sequence value currently maintained by the node for the requested destination.
       * However, the forwarding node MUST NOT modify its maintained value for the destination sequence number, even if the value
       * received in the incoming RREQ is larger than the value currently maintained by the forwarding node.
       */
      if ((rreqHeader.GetUnknownSeqno () || (int32_t (toDst.GetSeqNo ()) - int32_t (rreqHeader.GetDstSeqno ()) >= 0))
          && toDst.GetValidSeqNo () )
        {
          if (!rreqHeader.GetDestinationOnly () && toDst.GetFlag () == VALID)
            {
              m_routingTable.LookupRoute (origin, toOrigin);
              SendReplyByIntermediateNode (toDst, toOrigin, rreqHeader.GetGratiousRrep ());
              return;
            }
          rreqHeader.SetDstSeqno (toDst.GetSeqNo ());
          rreqHeader.SetUnknownSeqno (false);
        }
    }

  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
         m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (rreqHeader);
      TypeHeader tHeader (AODVTYPE_RREQ);
      packet->AddHeader (tHeader);
      // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
      Ipv4Address destination;
      if (iface.GetMask () == Ipv4Mask::GetOnes ())
        {
          destination = Ipv4Address ("255.255.255.255");
        }
      else
        { 
          destination = iface.GetBroadcast ();
        }
      socket->SendTo (packet, 0, InetSocketAddress (destination, AODV_PORT));
    }

  if (EnableHello)
    {
      if (!m_htimer.IsRunning ())
        {
          m_htimer.Cancel ();
          m_htimer.Schedule (HelloInterval - Time (0.1 * MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))));
	}
    }
}
