
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
        NS_OBJECT_ENSURE_REGISTERED (PaymentRoutingProtocol);

        const uint32_t PaymentRoutingProtocol::OFFCHAIN_ROUTING_PORT = 1200;
        const uint32_t PaymentRoutingProtocol::OFFCHAIN_HELLO_PORT = 1400;



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
        PaymentRoutingProtocol::PaymentRoutingProtocol () :
                RreqRetries (2),
                RreqRateLimit (10),
                RerrRateLimit (10),
                ActiveRouteTimeout (Seconds (3)),
                NetDiameter (35),
                NodeTraversalTime (MilliSeconds (40)),
                NetTraversalTime (Time ((2 * NetDiameter) * NodeTraversalTime)),
                PathDiscoveryTime ( Time (2 * NetTraversalTime)),
                MyRouteTimeout (Time (2 * std::max (PathDiscoveryTime, ActiveRouteTimeout))),
                HelloInterval (Seconds (60)),
                AllowedHelloLoss (2),
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
                m_rreqCount (0),
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
                m_nb.SetCallback (MakeCallback (&PaymentRoutingProtocol::ClosePaymentChannelToNextHop, this));
            }
        }

        TypeId
        PaymentRoutingProtocol::GetTypeId (void)
        {
            static TypeId tid = TypeId ("ns3::offchain::PaymentRoutingProtocol")
                    .AddConstructor<PaymentRoutingProtocol> ()
                    .AddAttribute ("HelloInterval", "HELLO messages emission interval.",
                                   TimeValue (Seconds (1)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::HelloInterval),
                                   MakeTimeChecker ())
                    .AddAttribute ("RreqRetries", "Maximum number of retransmissions of RREQ to discover a route",
                                   UintegerValue (2),
                                   MakeUintegerAccessor (&PaymentRoutingProtocol::RreqRetries),
                                   MakeUintegerChecker<uint32_t> ())
                    .AddAttribute ("RreqRateLimit", "Maximum number of RREQ per second.",
                                   UintegerValue (10),
                                   MakeUintegerAccessor (&PaymentRoutingProtocol::RreqRateLimit),
                                   MakeUintegerChecker<uint32_t> ())
                    .AddAttribute ("RerrRateLimit", "Maximum number of RERR per second.",
                                   UintegerValue (10),
                                   MakeUintegerAccessor (&PaymentRoutingProtocol::RerrRateLimit),
                                   MakeUintegerChecker<uint32_t> ())
                    .AddAttribute ("NodeTraversalTime", "Conservative estimate of the average one hop traversal time for packets and should include "
                                                        "queuing delays, interrupt processing times and transfer times.",
                                   TimeValue (MilliSeconds (40)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::NodeTraversalTime),
                                   MakeTimeChecker ())
                    .AddAttribute ("NextHopWait", "Period of our waiting for the neighbour's RREP_ACK = 10 ms + NodeTraversalTime",
                                   TimeValue (MilliSeconds (50)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::NextHopWait),
                                   MakeTimeChecker ())
                    .AddAttribute ("ActiveRouteTimeout", "Period of time during which the route is considered to be valid",
                                   TimeValue (Seconds (3)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::ActiveRouteTimeout),
                                   MakeTimeChecker ())
                    .AddAttribute ("MyRouteTimeout", "Value of lifetime field in RREP generating by this node = 2 * max(ActiveRouteTimeout, PathDiscoveryTime)",
                                   TimeValue (Seconds (11.2)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::MyRouteTimeout),
                                   MakeTimeChecker ())
                    .AddAttribute ("BlackListTimeout", "Time for which the node is put into the blacklist = RreqRetries * NetTraversalTime",
                                   TimeValue (Seconds (5.6)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::BlackListTimeout),
                                   MakeTimeChecker ())
                    .AddAttribute ("DeletePeriod", "DeletePeriod is intended to provide an upper bound on the time for which an upstream node A "
                                                   "can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D."
                                                   " = 5 * max (HelloInterval, ActiveRouteTimeout)",
                                   TimeValue (Seconds (15)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::DeletePeriod),
                                   MakeTimeChecker ())
                    .AddAttribute ("TimeoutBuffer", "Its purpose is to provide a buffer for the timeout so that if the RREP is delayed"
                                                    " due to congestion, a timeout is less likely to occur while the RREP is still en route back to the source.",
                                   UintegerValue (2),
                                   MakeUintegerAccessor (&PaymentRoutingProtocol::TimeoutBuffer),
                                   MakeUintegerChecker<uint16_t> ())
                    .AddAttribute ("NetDiameter", "Net diameter measures the maximum possible number of hops between two nodes in the network",
                                   UintegerValue (35),
                                   MakeUintegerAccessor (&PaymentRoutingProtocol::NetDiameter),
                                   MakeUintegerChecker<uint32_t> ())
                    .AddAttribute ("NetTraversalTime", "Estimate of the average net traversal time = 2 * NodeTraversalTime * NetDiameter",
                                   TimeValue (Seconds (2.8)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::NetTraversalTime),
                                   MakeTimeChecker ())
                    .AddAttribute ("PathDiscoveryTime", "Estimate of maximum time needed to find route in network = 2 * NetTraversalTime",
                                   TimeValue (Seconds (5.6)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::PathDiscoveryTime),
                                   MakeTimeChecker ())
                    .AddAttribute ("MaxQueueLen", "Maximum number of packets that we allow a routing protocol to buffer.",
                                   UintegerValue (64),
                                   MakeUintegerAccessor (&PaymentRoutingProtocol::SetMaxQueueLen,
                                                         &PaymentRoutingProtocol::GetMaxQueueLen),
                                   MakeUintegerChecker<uint32_t> ())
                    .AddAttribute ("MaxQueueTime", "Maximum time packets can be queued (in seconds)",
                                   TimeValue (Seconds (30)),
                                   MakeTimeAccessor (&PaymentRoutingProtocol::SetMaxQueueTime,
                                                     &PaymentRoutingProtocol::GetMaxQueueTime),
                                   MakeTimeChecker ())
                    .AddAttribute ("AllowedHelloLoss", "Number of hello messages which may be loss for valid link.",
                                   UintegerValue (2),
                                   MakeUintegerAccessor (&PaymentRoutingProtocol::AllowedHelloLoss),
                                   MakeUintegerChecker<uint16_t> ())
                    .AddAttribute ("GratuitousReply", "Indicates whether a gratuitous RREP should be unicast to the node originated route discovery.",
                                   BooleanValue (true),
                                   MakeBooleanAccessor (&PaymentRoutingProtocol::SetGratuitousReplyFlag,
                                                        &PaymentRoutingProtocol::GetGratuitousReplyFlag),
                                   MakeBooleanChecker ())
                    .AddAttribute ("DestinationOnly", "Indicates only the destination may respond to this RREQ.",
                                   BooleanValue (false),
                                   MakeBooleanAccessor (&PaymentRoutingProtocol::SetDesinationOnlyFlag,
                                                        &PaymentRoutingProtocol::GetDesinationOnlyFlag),
                                   MakeBooleanChecker ())
                    .AddAttribute ("EnableHello", "Indicates whether a hello messages enable.",
                                   BooleanValue (true),
                                   MakeBooleanAccessor (&PaymentRoutingProtocol::SetHelloEnable,
                                                        &PaymentRoutingProtocol::GetHelloEnable),
                                   MakeBooleanChecker ())
                    .AddAttribute ("EnableBroadcast", "Indicates whether a broadcast data packets forwarding enable.",
                                   BooleanValue (true),
                                   MakeBooleanAccessor (&PaymentRoutingProtocol::SetBroadcastEnable,
                                                        &PaymentRoutingProtocol::GetBroadcastEnable),
                                   MakeBooleanChecker ())
                    .AddAttribute ("UniformRv",
                                   "Access to the underlying UniformRandomVariable",
                                   StringValue ("ns3::UniformRandomVariable"),
                                   MakePointerAccessor (&PaymentRoutingProtocol::m_uniformRandomVariable),
                                   MakePointerChecker<UniformRandomVariable> ())
            ;
            return tid;
        }


        void
        PaymentRoutingProtocol::ClosePaymentChannelToNextHop (Ipv4Address nextHop)
        {
            NS_LOG_FUNCTION (this << nextHop);
            // record balance proof to the main chain
        }

        Ipv4Address
        PaymentRoutingProtocol::GetNodeAddress()
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
        PaymentRoutingProtocol::SendHello ()
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
        PaymentRoutingProtocol::SendHello (Ipv4Address dst, bool acked)
        {
            NS_LOG_FUNCTION (this);
            Ptr<Node> node = GetNode();
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            Ipv4Address thisIpv4Address = ipv4->GetAddress(1,0).GetLocal(); //the first argument is the interface index
            //index = 0 returns the loopback address 127.0.0.7
            uint32_t  curDeposit = m_nb.GetChAvailDeposit();
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
        PaymentRoutingProtocol::RecvHello (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address sender)
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
        PaymentRoutingProtocol::SendRReq (Ipv4Address dst, uint32_t transAmount)
        {
            NS_LOG_FUNCTION ( this << dst);
            // A node SHOULD NOT originate more than RREQ_RATELIMIT RREQ messages per 100 second.
            if (m_rreqCount == RreqRateLimit)
            {
                Simulator::Schedule (m_rreqRateLimitTimer.GetDelayLeft () + Seconds (100),
                                     &PaymentRoutingProtocol::SendRequest, this, dst);
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


        int
        PaymentRoutingProtocol::RecvRReq (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)
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
                    return -1;
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
                return -1;
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
                SendRRep (rreqHeader, toOrigin, 0);
                return 1;
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
                    return -1;
                }
                /*
                 * The Destination Sequence number for the requested destination is set to the maximum of the corresponding value
                 * received in the RREQ message, and the destination sequence value currently maintained by the node for the requested destination.
                 * However, the forwarding node MUST NOT modify its maintained value for the destination sequence number, even if the value
                 * received in the incoming RREQ is larger than the value currently maintained by the forwarding node.
                 */
                //wk: reply by intermediate nodes is consider later when the absolute deposit amount is guaranteed
#if 0
                if ((rreqHeader.GetUnknownSeqno () || (int32_t (toDst.GetSeqNo ()) - int32_t (rreqHeader.GetDstSeqno ()) >= 0))
          && toDst.GetValidSeqNo () )
        {
          if (!rreqHeader.GetDestinationOnly () && toDst.GetFlag () == VALID)
            {
              m_routingTable.LookupRoute (origin, toOrigin);
              SendReplyByIntermediateNode (toDst, toOrigin, rreqHeader.GetGratiousRrep ());
              return -1;
            }
          rreqHeader.SetDstSeqno (toDst.GetSeqNo ());
          rreqHeader.SetUnknownSeqno (false);
        }
#endif
            }

            for (std::vector<Ipv4Address>::const_iterator n = ngbAvailNodes.begin (); n
                                                                                      != ngbAvailNodes.end (); ++n)
            {
                Ptr<Packet> packet = Create<Packet> ();
                packet->AddHeader (rreqHeader);
                TypeHeader tHeader (OFFCHAIN_ROUTING_RREQ);
                packet->AddHeader (tHeader);
                m_routingSocket->SendTo (packet, 0, InetSocketAddress (n, OFFCHAIN_ROUTING_PORT));
            }
            return 0;

        }

//wk: leave below function for future work
        void
        PaymentRoutingProtocol::SendReplyByIntermediateNode (RoutingTableEntry & toDst, RoutingTableEntry & toOrigin, bool gratRep)
        {
            NS_LOG_FUNCTION (this);
            RrepHeader rrepHeader (/*prefix size=*/ 0, /*hops=*/ toDst.GetHop (), /*dst=*/ toDst.GetDestination (), /*dst seqno=*/ toDst.GetSeqNo (),
                    /*origin=*/ toOrigin.GetDestination (), /*lifetime=*/ toDst.GetLifeTime ());
            /* If the node we received a RREQ for is a neighbor we are
             * probably facing a unidirectional link... Better request a RREP-ack
             */
            if (toDst.GetHop () == 1)
            {
                rrepHeader.SetAckRequired (true);
                RoutingTableEntry toNextHop;
                m_routingTable.LookupRoute (toOrigin.GetNextHop (), toNextHop);
                toNextHop.m_ackTimer.SetFunction (&PaymentRoutingProtocol::AckTimerExpire, this);
                toNextHop.m_ackTimer.SetArguments (toNextHop.GetDestination (), BlackListTimeout);
                toNextHop.m_ackTimer.SetDelay (NextHopWait);
            }
            toDst.InsertPrecursor (toOrigin.GetNextHop ());
            toOrigin.InsertPrecursor (toDst.GetNextHop ());
            m_routingTable.Update (toDst);
            m_routingTable.Update (toOrigin);

            Ptr<Packet> packet = Create<Packet> ();
            packet->AddHeader (rrepHeader);
            TypeHeader tHeader (OFFCHAIN_ROUTING_RREP);
            packet->AddHeader (tHeader);
            m_routingSocket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), OFFCHAIN_ROUTING_PORT));

            // Generating gratuitous RREPs
            if (gratRep)
            {
                RrepHeader gratRepHeader (/*prefix size=*/ 0, /*hops=*/ toOrigin.GetHop (), /*dst=*/ toOrigin.GetDestination (),
                        /*dst seqno=*/ toOrigin.GetSeqNo (), /*origin=*/ toDst.GetDestination (),
                        /*lifetime=*/ toOrigin.GetLifeTime ());
                Ptr<Packet> packetToDst = Create<Packet> ();
                packetToDst->AddHeader (gratRepHeader);
                TypeHeader type (OFFCHAIN_ROUTING_RREP);
                packetToDst->AddHeader (type);
                NS_LOG_LOGIC ("Send gratuitous RREP " << packet->GetUid ());
                m_routingSocket->SendTo (packetToDst, 0, InetSocketAddress (toDst.GetNextHop (), OFFCHAIN_ROUTING_PORT));
            }
        }


        void
        PaymentRoutingProtocol::SendRRep (RreqHeader const & rreqHeader, RoutingTableEntry const & toOrigin, uint32_t reward)
        {
            NS_LOG_FUNCTION (this << toOrigin.GetDestination ());
            /*
             * Destination node MUST increment its own sequence number by one if the sequence number in the RREQ packet is equal to that
             * incremented value. Otherwise, the destination does not change its sequence number before generating the  RREP message.
             */
            if (!rreqHeader.GetUnknownSeqno () && (rreqHeader.GetDstSeqno () == m_seqNo + 1))
                m_seqNo++;
            RrepHeader rrepHeader ( /*hops=*/ 0, /*dst=*/ rreqHeader.GetDst (),
                    /*dstSeqNo=*/ m_seqNo, /*origin=*/ toOrigin.GetDestination (), /*lifeTime=*/ MyRouteTimeout, reward);

            Ptr<Packet> packet = Create<Packet> ();
            packet->AddHeader (rrepHeader);
            TypeHeader tHeader (OFFCHAIN_ROUTING_RREP);
            packet->AddHeader (tHeader);
            m_routingSocket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), OFFCHAIN_ROUTING_PORT));
        }

        int
        PaymentRoutingProtocol::RecvRRep (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address sender)
        {
            NS_LOG_FUNCTION (this << " src " << sender);
            RrepHeader rrepHeader;
            p->RemoveHeader (rrepHeader);
            Ipv4Address dst = rrepHeader.GetDst ();
            NS_LOG_LOGIC ("RREP destination " << dst << " RREP origin " << rrepHeader.GetOrigin ());

            uint8_t hop = rrepHeader.GetHopCount () + 1;
            rrepHeader.SetHopCount (hop);

            /*
             * If the route table entry to the destination is created or updated, then the following actions occur:
             * -  the route is marked as active,
             * -  the destination sequence number is marked as valid,
             * -  the next hop in the route entry is assigned to be the node from which the RREP is received,
             *    which is indicated by the source IP address field in the IP header,
             * -  the hop count is set to the value of the hop count from RREP message + 1
             * -  the expiry time is set to the current time plus the value of the Lifetime in the RREP message,
             * -  and the destination sequence number is the Destination Sequence Number in the RREP message.
             */

            RoutingTableEntry newEntry (/*dst=*/ dst, /*validSeqNo=*/ true, /*seqNo=*/ rrepHeader.GetDstSeqno (),
                                                 GetNodeAddress (), /*hop=*/ hop, /*transAmount=*/ amount,
                    /*nextHop=*/ sender, /*lifeTime=*/ rrepHeader.GetLifeTime () );

            RoutingTableEntry toDst;
            if (m_routingTable.LookupRoute (dst, toDst))
            {
                /*
                 * The existing entry is updated only in the following circumstances:
                 * (i) the sequence number in the routing table is marked as invalid in route table entry.
                 */
                if (!toDst.GetValidSeqNo ())
                {
                    m_routingTable.Update (newEntry);
                }
                    // (ii)the Destination Sequence Number in the RREP is greater than the node's copy of the destination sequence number and the known value is valid,
                else if ((int32_t (rrepHeader.GetDstSeqno ()) - int32_t (toDst.GetSeqNo ())) > 0)
                {
                    m_routingTable.Update (newEntry);
                }
                else
                {
                    // (iii) the sequence numbers are the same, but the route is marked as inactive.
                    if ((rrepHeader.GetDstSeqno () == toDst.GetSeqNo ()) && (toDst.GetFlag () != VALID))
                    {
                        m_routingTable.Update (newEntry);
                    }
                        // (iv)  the sequence numbers are the same, and the New Hop Count is smaller than the hop count in route table entry.
                    else if ((rrepHeader.GetDstSeqno () == toDst.GetSeqNo ()) && (hop < toDst.GetHop ()))
                    {
                        m_routingTable.Update (newEntry);
                    }
                }
            }
            else
            {
                // The forward route for this destination is created if it does not already exist.
                NS_LOG_LOGIC ("add new route");
                m_routingTable.AddRoute (newEntry);
            }
            // Acknowledge receipt of the RREP by sending a RREP-ACK message back
            // tbd
            if (rrepHeader.GetAckRequired ())
            {
                SendReplyAck (sender);
                rrepHeader.SetAckRequired (false);
            }
            NS_LOG_LOGIC ("receiver " << receiver << " origin " << rrepHeader.GetOrigin ());
            if (IsMyOwnAddress (rrepHeader.GetOrigin ()))
            {
                if (toDst.GetFlag () == IN_SEARCH)
                {
                    m_routingTable.Update (newEntry);
                    m_addressReqTimer[dst].Remove ();
                    m_addressReqTimer.erase (dst);
                }
                return 1;
            }

            RoutingTableEntry toOrigin;
            if (!m_routingTable.LookupRoute (rrepHeader.GetOrigin (), toOrigin) || toOrigin.GetFlag () == IN_SEARCH)
            {
                return -1; // Impossible! drop.
            }
            toOrigin.SetLifeTime (std::max (ActiveRouteTimeout, toOrigin.GetLifeTime ()));
            m_routingTable.Update (toOrigin);

            // Update information about precursors
            if (m_routingTable.LookupValidRoute (rrepHeader.GetDst (), toDst))
            {
                toDst.InsertPrecursor (toOrigin.GetNextHop ());
                m_routingTable.Update (toDst);

                RoutingTableEntry toNextHopToDst;
                m_routingTable.LookupRoute (toDst.GetNextHop (), toNextHopToDst);
                toNextHopToDst.InsertPrecursor (toOrigin.GetNextHop ());
                m_routingTable.Update (toNextHopToDst);

                toOrigin.InsertPrecursor (toDst.GetNextHop ());
                m_routingTable.Update (toOrigin);

                RoutingTableEntry toNextHopToOrigin;
                m_routingTable.LookupRoute (toOrigin.GetNextHop (), toNextHopToOrigin);
                toNextHopToOrigin.InsertPrecursor (toDst.GetNextHop ());
                m_routingTable.Update (toNextHopToOrigin);
            }

            int32_t myReward =  m_handleRecvRREP (origin, rrepHeader.GetTransAmount());
            int32_t accReward =   rrepHeader.GetAccRewards ();

            rrepHeader.SetAccRewards(myReward+accReward);
            Ptr<Packet> packet = Create<Packet> ();
            packet->AddHeader (rrepHeader);
            TypeHeader tHeader (OFFCHAIN_ROUTING_RREP);
            packet->AddHeader (tHeader);
            m_routingSocket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), OFFCHAIN_ROUTING_PORT));
            return 0;
        }



    } /*offchain*/
} /*ns3*/