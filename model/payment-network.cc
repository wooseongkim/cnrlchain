
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/payment-network.h"
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/nstime.h"
#include "ns3/address-utils.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"

#include "ns3/string.h"
#include "ns3/pkt-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"

using namespace std;

namespace ns3 {


    NS_LOG_COMPONENT_DEFINE ("PaymentNetworkApplication");
    NS_OBJECT_ENSURE_REGISTERED (PaymentNetwork);


    PaymentNetwork::PaymentNetwork ()
    {
        NS_LOG_FUNCTION (this);
        m_sent = 0;
        m_socket = 0;
        m_sendEvent = EventId ();
        m_data = 0;
        m_dataSize = 0;
        m_contentManager = new ContentManager();
        m_interestManager = new InterestManager();
        m_interestBroadcastId = 0;
        m_pending_data = new vector<PendingDataEntry>;
        m_pending_interest_known_content_provider = new vector<PendingInterestEntryKnownContentProvider>;
        m_pending_interest_unknown_content_provider = new vector<PendingInterestEntryUnknownContentProvider>;
        m_initialRequestedContent = Ipv4Address("0.0.0.0");
        m_firstSuccess = false;
        m_ngbChTable = new Neighbors (1000, 100); //default deposit 100
        m_routingProtocol = new PaymentRoutingProtocol();
        m_routingProtocol.SetRecvRreqCB (MakeCallback (&PaymentRoutingProtocol::HandleTransRreq, this));
        m_routingProtocol.SetRecvRrepCB (MakeCallback (&PaymentRoutingProtocol::HandleTransRrep, this));
    }



    PaymentNetwork::~PaymentNetwork()
    {
        NS_LOG_FUNCTION (this);
        m_socket = 0;

        delete [] m_data;
        delete m_contentManager;
        delete m_interestManager;
        delete m_relationship;
        delete m_pending_data;
        delete m_pending_interest_known_content_provider;
        delete m_pending_interest_unknown_content_provider;

        m_data = 0;
        m_dataSize = 0;
    }


    void
    PaymentNetwork::DoDispose (void)
    {
        NS_LOG_FUNCTION (this);
        Application::DoDispose ();
    }


    void
    PaymentNetwork::Setup (uint16_t port)
    {
        m_peerPort = port;
    }


    void
    PaymentNetwork::RequestContent (Ipv4Address content)
    {
        m_initialRequestedContent = content;
    }


    void
    PaymentNetwork::StopApplication ()
    {

        NS_LOG_FUNCTION (this);

        if (m_socket != 0)
        {
            m_socket->Close ();
            m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
            m_socket = 0;
        }

        Simulator::Cancel (m_sendEvent);
    }


    void
    PaymentNetwork::StartApplication (void)
    {
        NS_LOG_FUNCTION (this);
        m_routingProtocol = offchain::RoutingProtocol ();
        m_routingProtocol.SetNeighborTable(m_ngbChTable); //set neighbor table

        Ipv4Address broadcastAddr = Ipv4Address("255.255.255.255");
        Ipv4Address thisNodeAddress = GetNodeAddress();

        //cannot obtain Ipv4Address in the constructor before application starts.
        //Attempt to do so resulting in a runtime error.
        m_relationship = new Relationship(thisNodeAddress);

        //insert the initial content into my content list
        m_initialOwnedContent = thisNodeAddress;
        NS_LOG_INFO(""<<thisNodeAddress<<" content: "<<m_initialOwnedContent);
        m_contentManager->Insert(m_initialOwnedContent);

        //insert the initial requested content into m_pending_interest_response
        if ( !( m_initialRequestedContent.IsEqual(Ipv4Address("0.0.0.0"))) )
        {
            NS_LOG_INFO(""<<thisNodeAddress<<" requests content: "<<m_initialRequestedContent);
            PendingInterestEntryUnknownContentProvider entry;
            entry.requester = thisNodeAddress;
            entry.broadcastId = m_interestBroadcastId;
            entry.requestedContent = m_initialRequestedContent;
            m_pending_interest_unknown_content_provider->push_back(entry);

            m_interestBroadcastId++;
            //When this node encounters a node with higher social level, it will send
            //the interest packet to that node.
        }

        if (m_socket == 0)
        {
            TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
            m_socket = Socket::CreateSocket (GetNode (), tid);
            m_socket->SetAllowBroadcast(true);
            InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_peerPort);
            m_socket->Bind (local);
            m_socket->Connect (InetSocketAddress (broadcastAddr, m_peerPort)); //Enable broadcast
        }

        m_socket->SetRecvCallback (MakeCallback (&PaymentNetwork::HandleRead, this));

        ScheduleTransmitPayChannelPackets(1000);
    }

    bool
    PaymentNetwork::LookupRoute(Ipv4Address dst)
    {
        RoutingTableEntry toNeighbor;
        return m_routingTable->LookupRoute (dst, toNeighbor);

    }

    Ipv4Address
    PaymentNetwork::LookupNextNode (Ipv4Address dst)
    {
        return m_routingTable->LookupNeigbhorInRoute (dst);
    }

    bool
    PaymentNetwork::addNewRoute()
    {
        RoutingTableEntry toNeighbor;
        if (!m_routingTable.LookupRoute (rrepHeader.GetDst (), toNeighbor))
        {
            Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
            RoutingTableEntry newEntry (/*device=*/ dev, /*dst=*/ rrepHeader.GetDst (), /*validSeqNo=*/ true, /*seqno=*/ rrepHeader.GetDstSeqno (),
                    /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
                    /*hop=*/ 1, /*nextHop=*/ rrepHeader.GetDst (), /*lifeTime=*/ rrepHeader.GetLifeTime ());
            m_routingTable.AddRoute (newEntry);
        }

    }

    uint32_t
    PaymentNetwork::HandleTransRreq (Ipv4Address orig, uint32_t amount)
    {
        //may have to do something related to determine serving this transaction or not
        return 0;
    }

    uint32_t
    PaymentNetwork::HandleTransRrep (Ipv4Address orig, uint32_t amount)
    {
        //must return reward here
        return 0;
    }

    void
    PaymentNetwork::HandleOffchainMsg (Ptr<Socket> socket)
    {
        NS_LOG_FUNCTION (this << socket);
        Address sourceAddress;
        Ptr<Packet> packet = socket->RecvFrom (sourceAddress);
        InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom (sourceAddress);
        Ipv4Address sender = inetSourceAddr.GetIpv4 ();
        Ipv4Address receiver = m_socketAddresses[socket].GetLocal ();


        //UpdateRouteToNeighbor (sender, receiver);
        TypeHeader tHeader (OFFCHAIN_ROUTING_RREP);
        packet->RemoveHeader (tHeader);
        int resEv;
        switch (tHeader.Get ())
        {
            case OFFCHAIN_ROUTING_RREQ:
            {
                resEv = m_routingProtocol->RecvRReq (packet, receiver, sender);
                if (resEv < 0)
                {
                    NS_LOG_INFO("RREQ IS DUPLICATED OR EXISTING ROUTE");
                }
                else if (resEv == 1)
                {
                    NS_LOG_INFO("RREQ IS FOR ME");
                    //wk: do we have to do something when a receiver receives rreq?
                }
                break;
            }
            case OFFCHAIN_ROUTING_RREP:
            {
                resEv = m_routingProtocol->RecvRRep (packet, receiver, sender);
                if (resEv < 0)
                {
                    NS_LOG_INFO("RREQ IS DUPLICATED OR EXISTING ROUTE");
                }
                else if (resEv == 1)
                {
                    NS_LOG_INFO("RREP IS FOR ME");

                    //wk: here we have to put a routine to start payment

                }
                break;
            }
            case OFFCHAIN_ROUTING_HELLO:
            {
                m_routingProtocol->RecvHello (packet, receiver, sender);
                break;
            }

        }
    }

/*
 Schedule to transmit hello packets every TIME_INTERVAL=10s,
 starting at the initial value of time_elapsed.
*/
    void
    PaymentNetwork::ScheduleTransmitPayChannelPackets(int numberOfEvents)
    {
        uint16_t event_counter = 0;
        double time_elapsed = Simulator::Now ().GetSeconds () +
                              static_cast <double> (rand()) / static_cast <double> (RAND_MAX);
        /*
        There are 2 components for time_elapsed:
        - Simulator::Now ().GetSeconds () => so that a new node joins will not send a packet at time already in the past
                   For example: If it joins at time 2 second, it does not make sense to transmit at time 0 second.
        - static_cast <double> (RAND_MAX) => ensure that 2 nodes join at the same time, will not transmit at the same time
                                             which results in packet collision => No node receives the HELLO packet.
        */
        const double TIME_INTERVAL = 10; // 10 seconds

        while (event_counter < numberOfEvents)
        {
            time_elapsed += TIME_INTERVAL;
            Simulator::Schedule (Seconds(time_elapsed), &PaymentNetwork::SendChMaintain, this);
            event_counter++;
        }
    }


    void
    PaymentNetwork::SendChMaintain ()
    {
        //broadcast once
        m_routingProtocol->SendHello();
        //unicast to all neighbor
        for(int i=0; i < m_ngbChTable.size(); i++)
        {
            m_routingProtocol->SendHello(m_ngbChTable.GetNgbIPaddrByIndex(i), false);
        }

        NS_LOG_INFO("");
        NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds () << "s node "<< GetNodeAddress() <<  " broadcasts HELLO on port ");
    }


    void
    PaymentNetwork::SendPacket(PktHeader header)
    {
        Ptr<Packet> p = Create<Packet> (m_size);
        p->AddHeader(header);

        // call to the trace sinks before the packet is actually sent,
        // so that tags added to the packet can be sent as well
        m_txTrace (p);
        m_socket->Send (p);
    }

    void
    PaymentNetwork::SendLockBP(RreqHeader const & LockBPHeader, RoutingTableEntry const & toDst)
    {
        NS_LOG_FUNCTION (this << toOrigin.GetDestination ());
        /*
         * Destination node MUST increment its own sequence number by one if the sequence number in the RREQ packet is equal to that
         * incremented value. Otherwise, the destination does not change its sequence number before generating the  RREP message.
         */
        if (!rreqHeader.GetUnknownSeqno () && (rreqHeader.GetDstSeqno () == m_seqNo + 1))
            m_seqNo++;
        LockBPHeader lockBPHeader ( /*hops=*/ 0, /*dst=*/ rreqHeader.GetDst (),
                /*dstSeqNo=*/ m_seqNo, /*origin=*/ toOrigin.GetDestination (), /*lifeTime=*/ MyRouteTimeout, reward);

        Ptr<Packet> packet = Create<Packet> ();
        packet->AddHeader (rrepHeader);
        TypeHeader tHeader (OFFCHAIN_ROUTING_RREP);
        packet->AddHeader (tHeader);
        m_routingSocket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), OFFCHAIN_ROUTING_PORT));

    }


    PktHeader *
    PaymentNetwork::CreateDataPacketHeader(Ipv4Address requester,
                                           Ipv4Address destination, uint32_t broadcastId, Ipv4Address requestedContent)
    {
        PktHeader *header = new PktHeader();
        header->SetSource(GetNodeAddress());
        header->SetDestination(destination);
        header->SetRequestedContent(requestedContent);

        header->SetRequesterId(requester);
        header->SetInterestBroadcastId(broadcastId);
        header->SetPacketType(DATA);

        return header;
    }


    PktHeader *
    PaymentNetwork::CreateInterestPacketHeaderUnknownContentProvider(Ipv4Address requester,
                                                                     Ipv4Address destination, uint32_t broadcastId, Ipv4Address requestedContent)
    {
        PktHeader *header = new PktHeader();
        header->SetSource(GetNodeAddress());
        header->SetDestination(destination);
        header->SetRequestedContent(requestedContent);

        //The node that has the content, when receiving this packet, it will check
        //the requesterId and broadcastId and broadcastId of the packet, if it already exists
        //in its interestManager, it will discard the packet even though it has DATA matches
        //that interest.
        header->SetRequesterId(requester);
        header->SetInterestBroadcastId(broadcastId);
        header->SetPacketType(InterestUnknownContentProvider);

        return header;
    }


    PktHeader *
    PaymentNetwork::CreateInterestPacketHeaderKnownContentProvider(Ipv4Address requester,
                                                                   Ipv4Address destination, uint32_t broadcastId, Ipv4Address requestedContent, Ipv4Address contentProvider)
    {
        PktHeader *header = new PktHeader();
        header->SetSource(GetNodeAddress());
        header->SetDestination(destination);
        header->SetRequestedContent(requestedContent);

        //The node that has the content, when receiving this packet, it will check
        //the requesterId and broadcastId and broadcastId of the packet, if it already exists
        //in its interestManager, it will discard the packet even though it has DATA matches
        //that interest.
        header->SetRequesterId(requester);
        header->SetInterestBroadcastId(broadcastId);
        header->SetPacketType(InterestUnknownContentProvider);
        header->SetContentProviderId(contentProvider);

        return header;
    }


    PktHeader *
    PaymentNetwork::CreateDigestPacketHeader(Ipv4Address destinationId)
    {
        PktHeader *header = new PktHeader();

        //Allow other neighbor nodes to read the content array from this node
        header->SetContentArraySize(m_contentManager->GetContentArraySize());
        header->SetContentArray( m_contentManager->GetContentArray() );

        //Set packet type in packet header
        header->SetPacketType(DIGEST);

        //Set destination in packet header
        header->SetDestination(destinationId); //we unicast this Digest packet to destinationId

        //Set source in packet header
        header->SetSource(GetNodeAddress());

        return header;
    }


    Ipv4Address
    PaymentNetwork::GetNodeAddress()
    {
        //Figure out the IP address of this current node
        Ptr<Node> node = GetNode();
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        Ipv4Address thisIpv4Address = ipv4->GetAddress(1,0).GetLocal(); //the first argument is the interface index
        //index = 0 returns the loopback address 127.0.0.1
        return thisIpv4Address;
    }


    void
    PaymentNetwork::PrintAllContent(Ipv4Address *array, uint32_t size)
    {
        for (uint32_t i=0;i<size;++i)
        {
            NS_LOG_INFO( array[i] );
        }
    }





    void
    PaymentNetwork::HandleData(PktHeader *header)
    {
        /*
            Accessible information from header (DATA) include:
                - source
                - destination (which is this node address)
                - packet type
                - requester
                - broadcastId
                - requestedContent

            Please see function: CreateDataPacketHeader
        */

        /*
            Algorithm:
            If requested content from DATA packet matches my m_initialRequestedContent:
                //This DATA packet does not neccessarily destine to me. It may destine to other
                //requester and I am happening to be an intermediate node along the path
                //and I am also requesting the same DATA packet.
                SUCCESS
            Else:
                If this DATA packet is not in my m_pending_data: //DATA packet is uniquely identify by (requester, broadcastId)
                    Turn into DATA social tie routing by saving
                    (requester, broadcastId, requestedContent) into m_pending_data
        */

        NS_LOG_INFO("Inside HandleData");

        Ipv4Address currentNode = GetNodeAddress();
        Ipv4Address encounterNode = header->GetSource();

        if ( !currentNode.IsEqual(header->GetDestination()) )
        {
            //I am not the node this Data packet is destined to
            return;
        }

        Ipv4Address requester = header->GetRequesterId();
        Ipv4Address requestedContent = header->GetRequestedContent();
        uint32_t broadcastId = header->GetInterestBroadcastId();

        NS_LOG_INFO("Data packet - requester: "<<requester);
        NS_LOG_INFO("Interest packet - requestedContent: "<<requestedContent);
        NS_LOG_INFO("Interest packet - broadcastId: "<<broadcastId);

        if ( m_initialRequestedContent.IsEqual(requestedContent ) )
        {
            if (m_firstSuccess == false)
            {
                NS_LOG_INFO ("SUCCESS FIRST");
                m_firstSuccess = true;
            }
            else
            {
                NS_LOG_INFO ("SUCCESS SECOND");
            }

            NS_LOG_INFO ("Time now " << Simulator::Now ().GetSeconds ());
            NS_LOG_INFO ("Requester node "<<currentNode<<" receives requested content "<<requestedContent<< " from node "<<encounterNode);
        }
        else
        {
            for (vector<PendingDataEntry>::iterator it = m_pending_data->begin();
                 it != m_pending_data->end(); ++it)
            {
                //Each PendingDataEntry is uniquely identified by <requester, broadcastId>
                if ( requester.IsEqual(it->requester) &&  broadcastId == it->broadcastId )
                {
                    NS_LOG_INFO("Pending DATA entry already exists");
                    return;
                }
            }

            PendingDataEntry entry;
            entry.requester = requester;
            entry.broadcastId = broadcastId;
            entry.requestedContent = requestedContent;
            m_pending_data->push_back(entry);
            NS_LOG_INFO("Save pending DATA entry into m_pending_data");
        }
    }


    void
    PaymentNetwork::HandleDigest(PktHeader *header)
    {
        /*
            Accessible information from header (DIGEST) include:
                - source
                - destination (which is this node address)
                - packet type
                - content array
                - contenet array size

            Please see function: CreateDigestPacketHeader
        */

        NS_LOG_INFO("Inside HandleDigest");
        Ipv4Address currentNode = GetNodeAddress();

        if ( currentNode.IsEqual(header->GetDestination()) ) { //I am the destination of this digest packet?
            uint32_t contentArraySize = header->GetContentArraySize();
            Ipv4Address *contentArray = header->GetContentArray();

            NS_LOG_INFO("Encounter node - content array size: "<<contentArraySize);
            NS_LOG_INFO("Encounter node - content: ");
            PrintAllContent(contentArray, contentArraySize);
            NS_LOG_INFO("This node content before merge: ");
            PrintAllContent(m_contentManager->GetContentArray(), m_contentManager->GetContentArraySize());

            m_contentManager->Merge(contentArray, contentArraySize);

            NS_LOG_INFO("This node content after merge: ");
            PrintAllContent(m_contentManager->GetContentArray(), m_contentManager->GetContentArraySize());
        }
    }


    void
    PaymentNetwork::DecideWhetherToSendContentNameDigest(PktHeader *header)
    {
        /*
        Algorithm:
            If he has higher centrality than me:
                Send content name digest to him
            End if
        */

        Ipv4Address currentNode = GetNodeAddress();
        Ipv4Address encounterNode = header->GetSource();
        Ipv4Address higherCentralityNode = m_relationship->GetHigherCentralityNode(currentNode, encounterNode);

        if (higherCentralityNode.IsEqual(encounterNode))
        {
            //Unicast content name digest packet to encounter node
            NS_LOG_INFO(""<<currentNode<<" sends content name digest to node "<<encounterNode);
            PktHeader *header = CreateDigestPacketHeader(encounterNode);
            SendPacket(*header);
        }
    }


    void
    PaymentNetwork::ProcessPendingInterestKnownContentProvider(PktHeader *header)
    {
        /*
            Accessible information from header (HELLO) include:
                - source
                - destination (which is this node address)
                - packet type
                - social tie table (not used here)
                - social tie table size (not used here)

            Please see function: CreateHelloPacketHeader
        */

        /*
        Algorithm:
        For each response in m_pending_content_dest_node_response:
            If encounter is the target content provider node:
                Unicast InterestKnownContentProvider packet to encounter
            Else:
                If encounter has higher social tie toward target content provider than me:
                    If last_relay_encounter == NULL:
                        Unicast InterestKnownContentProvider packet to encounter
                        Update last_relay_encounter to encounter
                    Else:
                        If encounter has higher social tie toward content provider than last_relay_encounter:
                            Unicast InterestKnownContentProvider packet to encounter
                            Update last_relay_encounter to encounter
        */

        NS_LOG_INFO("Inside ProcessPendingInterestKnownContentProvider");

        Ipv4Address currentNode = GetNodeAddress();
        Ipv4Address encounterNode = header->GetSource();

        NS_LOG_INFO("m_pending_interest_known_content_provider size: "<<m_pending_interest_known_content_provider->size());
        for (vector<PendingInterestEntryKnownContentProvider>::iterator it = m_pending_interest_known_content_provider->begin();
             it != m_pending_interest_known_content_provider->end(); ++it)
        {
            NS_LOG_INFO("PendingInterestKnownContentProvider - lastRelayNode: "<<it->lastRelayNode);
            NS_LOG_INFO("PendingInterestKnownContentProvider - requester: "<<it->requester);
            NS_LOG_INFO("PendingInterestKnownContentProvider - requestedContent: "<<it->requestedContent);
            NS_LOG_INFO("PendingInterestKnownContentProvider - contentProvider: "<<it->contentProvider);
            NS_LOG_INFO("PendingInterestKnownContentProvider - broadcastId: "<<it->broadcastId);
            NS_ASSERT_MSG(it->requestedContent.IsEqual(it->contentProvider),"ERROR: requestedContent does not match contentProvider");

            if ( (it->contentProvider).IsEqual(encounterNode) ) //encounter node is the content provider
            {
                //Unicast InterestKnownContentProvider packet to encounter
                PktHeader *header = CreateInterestPacketHeaderKnownContentProvider(
                        it->requester, encounterNode, it->broadcastId, it->requestedContent, it->contentProvider);
                SendPacket(*header);
                NS_LOG_INFO("Send InterestKnownContentProvider (requestedContent is: "<<it->requestedContent<<") to "<<encounterNode);
            }
            else
            {
                Ipv4Address higherPaymentTieNode =
                        m_relationship->GetHigherPaymentTie(currentNode, encounterNode, it->contentProvider);
                if (higherPaymentTieNode.IsEqual(encounterNode))
                {
                    if ( (it->lastRelayNode).IsEqual(Ipv4Address("0.0.0.0")) )
                    {
                        it->lastRelayNode = encounterNode;

                        //Unicast InterestKnownContentProvider packet to encounter
                        PktHeader *header = CreateInterestPacketHeaderKnownContentProvider(
                                it->requester, encounterNode, it->broadcastId, it->requestedContent, it->contentProvider);
                        SendPacket(*header);
                        NS_LOG_INFO("Send InterestKnownContentProvider (requestedContent is: "<<it->requestedContent<<") to "<<encounterNode);
                    }
                    else
                    {
                        Ipv4Address higherPaymentTieNode =
                                m_relationship->GetHigherPaymentTie(encounterNode, it->lastRelayNode, it->contentProvider);
                        if ( higherPaymentTieNode.IsEqual(encounterNode) )
                        {
                            it->lastRelayNode = encounterNode;

                            //Unicast InterestKnownContentProvider packet to encounter
                            PktHeader *header = CreateInterestPacketHeaderKnownContentProvider(
                                    it->requester, encounterNode, it->broadcastId, it->requestedContent, it->contentProvider);
                            SendPacket(*header);
                            NS_LOG_INFO("Send InterestKnownContentProvider (requestedContent is: "<<it->requestedContent<<") to "<<encounterNode);
                        }
                    }
                }
            }
        }
    }


    void
    PaymentNetwork::ProcessPendingInterestUnknownContentProvider(PktHeader *header)
    {
        /*
            Accessible information from header (HELLO) include:
                - source
                - destination (which is this node address)
                - packet type
                - social tie table (not used here)
                - social tie table size (not used here)

            Please see function: CreateHelloPacketHeader
        */

        /*
        Algorithm:
        For each response in m_pending_interest_unknown_content_provider:
            If encounter has higher social level than me:
                If last_data_relay_encounter == NULL:
                    Unicast InterestUnknownContentProvider packet to encounter
                    Update last_relay_encounter to encounter
                Else:
                    If encounter has higher social level than last_relay_encounter:
                        Unicast InterestUnknownContentProvider packet to encounter
                        Update last_relay_encounter to encounter
        */

        NS_LOG_INFO("Inside ProcessPendingInterestUnknownContentProvider");

        Ipv4Address currentNode = GetNodeAddress();
        Ipv4Address encounterNode = header->GetSource();

        NS_LOG_INFO("m_pending_interest_unknown_content_provider size: "<<m_pending_interest_unknown_content_provider->size());
        for (vector<PendingInterestEntryUnknownContentProvider>::iterator it = m_pending_interest_unknown_content_provider->begin();
             it != m_pending_interest_unknown_content_provider->end(); ++it)
        {
            NS_LOG_INFO("PendingInterestKnownContentProvider - lastRelayNode: "<<it->lastRelayNode);
            NS_LOG_INFO("PendingInterestKnownContentProvider - requester: "<<it->requester);
            NS_LOG_INFO("PendingInterestKnownContentProvider - requestedContent: "<<it->requestedContent);
            NS_LOG_INFO("PendingInterestKnownContentProvider - broadcastId: "<<it->broadcastId);

            Ipv4Address higherPaymentLevelNode = m_relationship->GetHigherPaymentLevel(currentNode, encounterNode);
            if ( higherPaymentLevelNode.IsEqual(encounterNode) )
            {
                if ( (it->lastRelayNode).IsEqual(Ipv4Address("0.0.0.0")) )
                {
                    it->lastRelayNode = encounterNode;

                    //Unicast InterestUnknownContentProvider packet to encounter
                    PktHeader *header = CreateInterestPacketHeaderUnknownContentProvider(
                            it->requester, encounterNode, it->broadcastId, it->requestedContent);
                    SendPacket(*header);
                    NS_LOG_INFO("Send InterestUnknownContentProvider (requestedContent is: "<<it->requestedContent<<") to "<<encounterNode);
                }
                else
                {
                    Ipv4Address higherPaymentLevelNode = m_relationship->GetHigherPaymentLevel(encounterNode, it->lastRelayNode);
                    if ( higherPaymentLevelNode.IsEqual(encounterNode) )
                    {
                        it->lastRelayNode = encounterNode;

                        //Unicast InterestUnknownContentProvider packet to encounter
                        PktHeader *header = CreateInterestPacketHeaderUnknownContentProvider(
                                it->requester, encounterNode, it->broadcastId, it->requestedContent);
                        SendPacket(*header);
                        NS_LOG_INFO("Send InterestUnknownContentProvider (requestedContent is: "<<it->requestedContent<<") to "<<encounterNode);
                    }
                }
            }
        }
    }


    void
    PaymentNetwork::ProcessPendingData(PktHeader *header)
    {
        /*
            Accessible information from header (HELLO) include:
                - source
                - destination (which is this node address)
                - packet type
                - social tie table (not used here)
                - social tie table size (not used here)

            Please see function: CreateHelloPacketHeader
        */

        //Take care of m_pending_data
        /*
        Algorithm:
        For each response in m_pending_data:
            If encounter is the requester.
                Unicast DATA packet to encounter
            Else:
                If encounter has higher social tie toward requester X than me:
                    If last_relay_encounter == NULL:
                        Unicast DATA packet to encounter
                        Update last_relay_encounter to encounter
                    Else:
                        If encounter has higher social tie toward requester X than last_relay_encounter:
                            Unicast DATA packet to encounter
                            Update last_relay_encounter to encounter
        */

        NS_LOG_INFO("Inside ProcessPendingData");

        Ipv4Address currentNode = GetNodeAddress();
        Ipv4Address encounterNode = header->GetSource();

        NS_LOG_INFO("m_pending_data size: "<<m_pending_data->size());

        // We never remove pending entry even after we already sent it since later
        // we might encounter a node that has a stronger social tie toward requester
        // than the last encounter node, and thus we will send out DATA again.

        for (vector<PendingDataEntry>::iterator it = m_pending_data->begin();
             it != m_pending_data->end(); ++it)
        {
            NS_LOG_INFO("PendingData - lastRelayNode: "<<it->lastRelayNode);
            NS_LOG_INFO("PendingData - requester: "<<it->requester);
            NS_LOG_INFO("PendingData - requestedContent: "<<it->requestedContent);
            NS_LOG_INFO("PendingData - broadcastId: "<<it->broadcastId);

            if ( (it->requester).IsEqual(encounterNode) ) //encounter is the requester
            {
                // Unicast DATA packet to encounter
                PktHeader *header = CreateDataPacketHeader(it->requester, encounterNode, it->broadcastId, it->requestedContent);
                SendPacket(*header);
                NS_LOG_INFO("Send DATA packet with content ("<< it->requestedContent <<") to "<<encounterNode);
            }
            else // Use social-tie routing to propagate DATA packet
            {
                Ipv4Address higherPaymentTieNode =
                        m_relationship->GetHigherPaymentTie(currentNode, encounterNode, it->requester);
                if (higherPaymentTieNode.IsEqual(encounterNode))
                {
                    if ( (it->lastRelayNode).IsEqual(Ipv4Address("0.0.0.0")) )
                    {
                        it->lastRelayNode = encounterNode;

                        // Unicast DATA packet to encounter
                        PktHeader *header = CreateDataPacketHeader(it->requester, encounterNode, it->broadcastId, it->requestedContent);
                        SendPacket(*header);
                        NS_LOG_INFO("Send DATA packet with content ("<< it->requestedContent <<") to "<<encounterNode);
                    }
                    else
                    {
                        Ipv4Address higherPaymentTieNode =
                                m_relationship->GetHigherPaymentTie(encounterNode, it->lastRelayNode, it->requester);
                        if ( higherPaymentTieNode.IsEqual(encounterNode) )
                        {
                            it->lastRelayNode = encounterNode;

                            // Unicast DATA packet to encounter
                            PktHeader *header = CreateDataPacketHeader(it->requester, encounterNode, it->broadcastId, it->requestedContent);
                            SendPacket(*header);
                            NS_LOG_INFO("Send DATA packet with content ("<< it->requestedContent <<") to "<<encounterNode);
                        }
                    }
                }
            }
        }
    }


    void
    PaymentNetwork::HandleInterestKnownContentProvider(PktHeader *header)
    {
        /*
            Accessible information from header (InterestKnownContentProvider) include:
                - source
                - destination (which is this node address)
                - requester
                - broadcastid
                - requested content
                - content provider
                - packet type

            Please see function: CreateInterestPacketHeaderKnownContentProvider
        */

        /*
        Algorithm:
            If not see this Interest before:
                Insert the Interest into my InterestManager
                If I have the content: //either I am the content provider this packet is destined to or I have the cached data
                    If encounter node is the requester:
                        send DATA packet to encounter node
                    Else:
                        Turn into DATA social-tie routing by saving
                        (requester, broadcast_id, requestedContent) into m_pending_data
                Else: //I do not have the content, so I am a relay node to a known content provider
                    Turn into InterestKnownContentProvider social-tie routing by saving
                    (requester, broadcastId, requestedContent, contentProvider) into m_pending_interest_known_content_provider
        */

        NS_LOG_INFO("Inside HandleInterestKnownContentProvider");

        Ipv4Address currentNode = GetNodeAddress();
        Ipv4Address encounterNode = header->GetSource();

        if ( !currentNode.IsEqual(header->GetDestination()) )
        {
            //I am not the node this Interest packet is destined to
            return;
        }

        Ipv4Address requester = header->GetRequesterId();
        Ipv4Address requestedContent = header->GetRequestedContent();
        Ipv4Address contentProvider = header->GetContentProviderId();
        uint32_t broadcastId = header->GetInterestBroadcastId();
        NS_LOG_INFO("Interest packet - requester: "<<requester);
        NS_LOG_INFO("Interest packet - requestedContent: "<<requestedContent);
        NS_LOG_INFO("Interest packet - contentProvider: "<<contentProvider);
        NS_LOG_INFO("Interest packet - broadcastId: "<<broadcastId);

        NS_ASSERT_MSG(requestedContent.IsEqual(contentProvider),"ERROR: requestedContent does not match contentProvider");

        InterestEntry interestEntry(requester, broadcastId, requestedContent);

        if (! (m_interestManager->Exist(interestEntry)) )
        {
            // First time sees this Interest packet
            m_interestManager->Insert(interestEntry);

            if (currentNode.IsEqual(requestedContent)) //I am the content provider
            {
                if (encounterNode.IsEqual(requester))
                {
                    // Unicast DATA packet to encounter
                    PktHeader *header = CreateDataPacketHeader(requester, encounterNode, broadcastId, requestedContent);
                    SendPacket(*header);
                    NS_LOG_INFO("Send DATA packet with content ("<< requestedContent <<") to "<<encounterNode);
                }
                else
                {
                    PendingDataEntry entry;
                    entry.requester = requester;
                    entry.broadcastId = broadcastId;
                    entry.requestedContent = requestedContent;
                    m_pending_data->push_back(entry);
                    NS_LOG_INFO("Save pending DATA entry into m_pending_data");
                }
            }
            else // I do not have the content, so I am a relay node to a known content provider
            {
                PendingInterestEntryKnownContentProvider entry;
                entry.requester = requester;
                entry.broadcastId = broadcastId;
                entry.requestedContent = requestedContent;
                entry.contentProvider = contentProvider;
                m_pending_interest_known_content_provider->push_back(entry);
                NS_LOG_INFO("Save pending InterestKnownContentProvider entry into m_pending_interest_known_content_provider");
            }
        }
        else
        {
            // Need not to do anything here because:
            // InterestEntry exists meaning I already saw it before.
            // That means I already served DATA packet for that interest before, or
            // I already saved it to the pending interest list with known content provider.
        }
    }


}
