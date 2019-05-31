
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

// hello은 조금 되어있다.
// rreq, rres 해야된다.

NS_LOG_COMPONENT_DEFINE ("OffchainRoutingProtocol");

namespace ns3
{
namespace offchain
{
NS_OBJECT_ENSURE_REGISTERED (RoutingProtocol);

const uint32_t RoutingProtocol::OFFCHAIN_ROUTING_PORT = 1200;
const uint32_t RoutingProtocol::OFFCHAIN_HELLO_PORT = 1400;



class DeferredRouteOutputTag : public Tag
{

public:
  DeferredRouteOutputTag (int32_t o = -1) : Tag (), m_oif (o) {}

  static TypeId GetTypeId ()
  {
    static TypeId tid = TypeId ("ns3::offchain::DeferredRouteOutputTag").SetParent<Tag> ()
      .SetParent<Tag> ()
      .AddConstructor<DeferredRouteOutputTag> ()
    ;
    return tid;
  }

  TypeId  GetInstanceTypeId () const 
  {
    return GetTypeId ();
  }

  int32_t GetInterface() const
  {
    return m_oif;
  }

  void SetInterface(int32_t oif)
  {
    m_oif = oif;
  }

  uint32_t GetSerializedSize () const
  {
    return sizeof(int32_t);
  }

  void  Serialize (TagBuffer i) const
  {
    i.WriteU32 (m_oif);
  }

  void  Deserialize (TagBuffer i)
  {
    m_oif = i.ReadU32 ();
  }

  void  Print (std::ostream &os) const
  {
    os << "DeferredRouteOutputTag: output interface = " << m_oif;
  }

private:
  /// Positive if output device is fixed in RouteOutput
  int32_t m_oif;
};

NS_OBJECT_ENSURE_REGISTERED (DeferredRouteOutputTag);


//-----------------------------------------------------------------------------
RoutingProtocol::RoutingProtocol () :
  RreqRetries (2),                  // 재전송 2
  RreqRateLimit (10),
  RerrRateLimit (10),
  ActiveRouteTimeout (Seconds (3)), // 경로가 유효한 것으로 간주되는 시간
  NetDiameter (35),                 // 두 노드사이에서 최대 가능한 홉 수
  NodeTraversalTime (MilliSeconds (40)),                                        // 평균 1홉 통과 시간
  NetTraversalTime (Time ((2 * NetDiameter) * NodeTraversalTime)),              // 평균 NodeTraversalTime
  PathDiscoveryTime ( Time (2 * NetTraversalTime)),                             // max
  MyRouteTimeout (Time (2 * std::max (PathDiscoveryTime, ActiveRouteTimeout))), // 노드에서 생성한 RREP의 수명 필드 값
  HelloInterval (Seconds (60)),                                                 // hellointerval마다 노드는 마지막 hellointerval 내에서 브로드캐스트를 전송했는지 여부 측정, 안왔으면 hello 메세지를 보냄
  AllowedHelloLoss (2),                                                         // 손실될 수 있는 hello 수
  DeletePeriod (Time (5 * std::max (ActiveRouteTimeout, HelloInterval))),
  NextHopWait (NodeTraversalTime + MilliSeconds (10)),
  TimeoutBuffer (2),
  BlackListTimeout (Time (RreqRetries * NetTraversalTime)),
  MaxQueueLen (64),
  MaxQueueTime (Seconds (30)),
  DestinationOnly (false),
  GratuitousReply (true),
  EnableHello (true),
  m_routingTable (DeletePeriod),
  m_queue (MaxQueueLen, MaxQueueTime),
  m_requestId (0),
  m_seqNo (0),
  m_rreqIdCache (PathDiscoveryTime),
  m_dpd (PathDiscoveryTime),
  m_nb (HelloInterval),
  m_rreqCount (0),                                  // RREQ 속도 제어에 사용된 RREQ 수
  m_rerrCount (0),
  m_htimer (Timer::CANCEL_ON_DESTROY),
  m_rreqRateLimitTimer (Timer::CANCEL_ON_DESTROY),
  m_rerrRateLimitTimer (Timer::CANCEL_ON_DESTROY)
{
    //listen broadcast packets
    TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
    m_routingSocket = Socket::CreateSocket (GetNode (), tid);
    m_routingSocket->SetAllowBroadcast(true);
    InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), OFFCHAIN_ROUTING_PORT);
    m_routingSocket->Bind (local);
    m_routingSocket->Connect (InetSocketAddress (broadcastAddr, OFFCHAIN_ROUTING_PORT)); //Enable broadcast

    m_helloSocket = Socket::CreateSocket (GetNode (), tid);
    m_helloSocket->SetAllowBroadcast(true);
    InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), OFFCHAIN_HELLO_PORT);
    m_helloSocket->Bind (local);
    m_helloSocket->Connect (InetSocketAddress (broadcastAddr, OFFCHAIN_HELLO_PORT)); //Enable broadcast

  if (EnableHello)
    {
      m_nb.SetCallback (MakeCallback (&RoutingProtocol::ClosePaymentChannelToNextHop, this));
    }
}

TypeId
RoutingProtocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::aodv::RoutingProtocol")
    .SetParent<Ipv4RoutingProtocol> ()
    .AddConstructor<RoutingProtocol> ()
    .AddAttribute ("HelloInterval", "HELLO messages emission interval.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&RoutingProtocol::HelloInterval),
                   MakeTimeChecker ())
    .AddAttribute ("RreqRetries", "Maximum number of retransmissions of RREQ to discover a route",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::RreqRetries),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RreqRateLimit", "Maximum number of RREQ per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::RreqRateLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RerrRateLimit", "Maximum number of RERR per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::RerrRateLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NodeTraversalTime", "Conservative estimate of the average one hop traversal time for packets and should include "
                   "queuing delays, interrupt processing times and transfer times.",
                   TimeValue (MilliSeconds (40)),
                   MakeTimeAccessor (&RoutingProtocol::NodeTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("NextHopWait", "Period of our waiting for the neighbour's RREP_ACK = 10 ms + NodeTraversalTime",
                   TimeValue (MilliSeconds (50)),
                   MakeTimeAccessor (&RoutingProtocol::NextHopWait),
                   MakeTimeChecker ())
    .AddAttribute ("ActiveRouteTimeout", "Period of time during which the route is considered to be valid",
                   TimeValue (Seconds (3)),
                   MakeTimeAccessor (&RoutingProtocol::ActiveRouteTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("MyRouteTimeout", "Value of lifetime field in RREP generating by this node = 2 * max(ActiveRouteTimeout, PathDiscoveryTime)",
                   TimeValue (Seconds (11.2)),
                   MakeTimeAccessor (&RoutingProtocol::MyRouteTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("BlackListTimeout", "Time for which the node is put into the blacklist = RreqRetries * NetTraversalTime",
                   TimeValue (Seconds (5.6)),
                   MakeTimeAccessor (&RoutingProtocol::BlackListTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("DeletePeriod", "DeletePeriod is intended to provide an upper bound on the time for which an upstream node A "
                   "can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D."
                   " = 5 * max (HelloInterval, ActiveRouteTimeout)",
                   TimeValue (Seconds (15)),
                   MakeTimeAccessor (&RoutingProtocol::DeletePeriod),
                   MakeTimeChecker ())
    .AddAttribute ("TimeoutBuffer", "Its purpose is to provide a buffer for the timeout so that if the RREP is delayed"
                   " due to congestion, a timeout is less likely to occur while the RREP is still en route back to the source.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::TimeoutBuffer),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("NetDiameter", "Net diameter measures the maximum possible number of hops between two nodes in the network",
                   UintegerValue (35),
                   MakeUintegerAccessor (&RoutingProtocol::NetDiameter),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NetTraversalTime", "Estimate of the average net traversal time = 2 * NodeTraversalTime * NetDiameter",
                   TimeValue (Seconds (2.8)),
                   MakeTimeAccessor (&RoutingProtocol::NetTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("PathDiscoveryTime", "Estimate of maximum time needed to find route in network = 2 * NetTraversalTime",
                   TimeValue (Seconds (5.6)),
                   MakeTimeAccessor (&RoutingProtocol::PathDiscoveryTime),
                   MakeTimeChecker ())
    .AddAttribute ("MaxQueueLen", "Maximum number of packets that we allow a routing protocol to buffer.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&RoutingProtocol::SetMaxQueueLen,
                                         &RoutingProtocol::GetMaxQueueLen),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxQueueTime", "Maximum time packets can be queued (in seconds)",
                   TimeValue (Seconds (30)),
                   MakeTimeAccessor (&RoutingProtocol::SetMaxQueueTime,
                                     &RoutingProtocol::GetMaxQueueTime),
                   MakeTimeChecker ())
    .AddAttribute ("AllowedHelloLoss", "Number of hello messages which may be loss for valid link.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::AllowedHelloLoss),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("GratuitousReply", "Indicates whether a gratuitous RREP should be unicast to the node originated route discovery.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::SetGratuitousReplyFlag,
                                        &RoutingProtocol::GetGratuitousReplyFlag),
                   MakeBooleanChecker ())
    .AddAttribute ("DestinationOnly", "Indicates only the destination may respond to this RREQ.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RoutingProtocol::SetDesinationOnlyFlag,
                                        &RoutingProtocol::GetDesinationOnlyFlag),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableHello", "Indicates whether a hello messages enable.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::SetHelloEnable,
                                        &RoutingProtocol::GetHelloEnable),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableBroadcast", "Indicates whether a broadcast data packets forwarding enable.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::SetBroadcastEnable,
                                        &RoutingProtocol::GetBroadcastEnable),
                   MakeBooleanChecker ())
    .AddAttribute ("UniformRv",
                   "Access to the underlying UniformRandomVariable",
                   StringValue ("ns3::UniformRandomVariable"),
                   MakePointerAccessor (&RoutingProtocol::m_uniformRandomVariable),
                   MakePointerChecker<UniformRandomVariable> ())
  ;
  return tid;
}


void
RoutingProtocol::ClosePaymentChannelToNextHop (Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this << nextHop);
  // record balance proof to the main chain
}

Ipv4Address
RoutingProtocol::GetNodeAddress()
{
    //Figure out the IP address of this current node
    Ptr<Node> node = GetNode();
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    Ipv4Address thisIpv4Address = ipv4->GetAddress(1,0).GetLocal(); //the first argument is the interface index
                                       //index = 0 returns the loopback address 127.0.0.1
    return thisIpv4Address;
}

//broadcast periodic hello
void
RoutingProtocol::SendHello ()
{
  NS_LOG_FUNCTION (this);

  Ptr<Node> node = GetNode();
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  Ipv4Address thisIpv4Address = ipv4->GetAddress(1,0).GetLocal(); //the first argument is the interface index
                                       //index = 0 returns the loopback address 127.0.0.7
  HelloHeader HelloHeader(/*dst=*/ dst, /*dst seqno=*/ m_seqNo, /*origin=*/ thisIpv4Address, 
                                        /*lifetime=*/ Time (AllowedHelloLoss * HelloInterval), 0);                                       
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
  m_helloSocket->SendTo (packet, 0, InetSocketAddress (destination, OFFCHAIN_HELLO_PORT));
    
}

// unicast hello for channel open 
void
RoutingProtocol::SendHello (Ipv4Address dst, bool acked)
{
  NS_LOG_FUNCTION (this);
  Ptr<Node> node = GetNode();
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  Ipv4Address thisIpv4Address = ipv4->GetAddress(1,0).GetLocal(); //the first argument is the interface index
                                       //index = 0 returns the loopback address 127.0.0.7
  uint32_t  curDeposit = m_nb.GetChAvailDeposit(); // m_nb : neighbors definication
  if(curDeposit < 0)
    curDeposit = m_nb.GetDefaultDeposit();

  HelloHeader HelloHeader(/*dst=*/ dst, /*dst seqno=*/ m_seqNo, /*origin=*/ thisIpv4Address, 
                                        /*lifetime=*/ Time (AllowedHelloLoss * HelloInterval), curDeposit);                                      
  if(acked)
    HelloHeader.SetAckRequired(true); //set ack for requesting channel open
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (helloHeader);
  TypeHeader tHeader (OFFCHAIN_ROUTING_HELLO);
  packet->AddHeader (tHeader);

  m_helloSocket->SendTo (packet, 0, InetSocketAddress (dst, OFFCHAIN_HELLO_PORT));
    
}

// 3 hellos; 1) broadcast or others (awareness) 2) channel open req (unicast), 3) acked hello (answer for the case 2)
void
RoutingProtocol::RecvHello (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address sender) 
{
  NS_LOG_FUNCTION (this);
  HelloHeader helloHeader;
  p->RemoveHeader (helloHeader);
  Ptr<Node> node = GetNode();
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  Ipv4Address thisIpv4Address = ipv4->GetAddress(1,0).GetLocal(); //the first argument is the interface index
                                       //index = 0 returns the loopback address 127.0.0.7

  if(receiver != thisIpv4Address && !m_nb.IsNeighbor (sender)) //case 1. send req to open a channel
  {
    SendHello(sender, true);
  }
  else if (receiver == thisIpv4Address && helloHeader.GetAckRequired()) // case 3. add it to neighbor table
  {
    m_nb.update(sender, helloHeader.GetAvailableDeposit(), Time (AllowedHelloLoss * HelloInterval), true);
    SendHello(sender, true);
  }
  else if (receiver == thisIpv4Address && m_nb.IsNeighbor (sender)) //case 2
  {
    m_nb.update(sender, helloHeader.GetAvailableDeposit(), Time (AllowedHelloLoss * HelloInterval), false); 
  }
  
}


void
RoutingProtocol::SendRReq (Ipv4Address dst, uint32_t transAmount)
{
  NS_LOG_FUNCTION ( this << dst);
  // A node SHOULD NOT originate more than RREQ_RATELIMIT RREQ messages per 100 second.
  if (m_rreqCount == RreqRateLimit)
    {
      Simulator::Schedule (m_rreqRateLimitTimer.GetDelayLeft () + Seconds (100),
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
      RoutingTableEntry newEntry (/*dst=*/ dst, /*validSeqNo=*/ false, /*seqno=*/ 0,
                     GetNodeAddress (), /*hop=*/ 0, /*transAmount=*/ transAmount,
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

  //send rreq
  rreqHeader.SetOrigin (GetNodeAddress());
  m_rreqIdCache.IsDuplicate (GetNodeAddress(), m_requestId);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rreqHeader);
  TypeHeader tHeader (OFFCHAIN_ROUTING_RREP);
  packet->AddHeader (tHeader);
  // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
  Ipv4Address destination = Ipv4Address ("255.255.255.255");;
  NS_LOG_DEBUG ("Send RREQ with id " << rreqHeader.GetId () << " to socket");
  socket->SendTo (packet, 0, InetSocketAddress (destination, OFFCHAIN_ROUTING_PORT));
  //if no rrep, then request again
  ScheduleRreqRetry (dst);

}


void
RoutingProtocol::RecvRReq (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)
{
  NS_LOG_FUNCTION (this);
  std::vector<Ipv4Address> ngbAvailNodes; //neighbor addresses having enough deposit
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

  // if requested amount of a transaction is over than available deposit, then discards rreq
  //unicast to all possible neighbors
    // transaction amount
  uint32_t amount = rreqHeader.GetTransAmount ();

  for(int i=0; i < m_nb.size(); i++)
    {
      Ipv4Address nbgAddr = GetNgbIPaddrByIndex(i);
      if (amount <= m_nb.GetChMyAvailDeposit(nbgAddr))
      {
        ngbAvailNodes.push_back(nbgAddr);
      }
    }
  

  // Increment RREQ hop count
  uint8_t hop = rreqHeader.GetHopCount () + 1;
  rreqHeader.SetHopCount (hop);


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
      RoutingTableEntry newEntry (/*dst=*/ origin, /*validSeqNo=*/ true, /*seqNo=*/ rreqHeader.GetOriginSeqno (),
                GetNodeAddress (), /*hop=*/ hop, /*transAmount=*/ amount,
                /*nextHop=*/ src, /*lifeTime=*/ Time ((2 * NetTraversalTime - 2 * hop * NodeTraversalTime)) );                                           
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
                         << " Transaction amount " << amount );



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

void
RoutingProtocol::SendRRep (RreqHeader const & rreqHeader, RoutingTableEntry const & toOrigin)
{
  NS_LOG_FUNCTION (this << toOrigin.GetDestination ());
  /*
   * Destination node MUST increment its own sequence number by one if the sequence number in the RREQ packet is equal to that
   * incremented value. Otherwise, the destination does not change its sequence number before generating the  RREP message.
   */
  if (!rreqHeader.GetUnknownSeqno () && (rreqHeader.GetDstSeqno () == m_seqNo + 1))
    m_seqNo++;
  RrepHeader rrepHeader ( /*prefixSize=*/ 0, /*hops=*/ 0, /*dst=*/ rreqHeader.GetDst (),
                                          /*dstSeqNo=*/ m_seqNo, /*origin=*/ toOrigin.GetDestination (), /*lifeTime=*/ MyRouteTimeout);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rrepHeader);
  TypeHeader tHeader (AODVTYPE_RREP);
  packet->AddHeader (tHeader);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigin.GetInterface ());
  NS_ASSERT (socket);
  socket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), AODV_PORT));
}





} /*offchain*/
} /*ns3*/