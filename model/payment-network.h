/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PAYMENT_NETWORK_H
#define PAYMENT_NETWORK_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "ns3/traced-callback.h"

#include "ns3/offchain-routing.h"
#include "ns3/payroute-packet.h"
#include "ns3/neighbors.h"

using namespace std;

namespace ns3 {

struct PendingInterestEntryUnknownContentProvider
{
    PendingInterestEntryUnknownContentProvider()
    {
        lastRelayNode = Ipv4Address("0.0.0.0");
    }

    Ipv4Address lastRelayNode;
    Ipv4Address requester;
    uint32_t broadcastId;
    Ipv4Address requestedContent;
};

struct PendingDataEntry
{
    PendingDataEntry()
    {
        lastRelayNode = Ipv4Address("0.0.0.0");
    }
    
    Ipv4Address lastRelayNode;
    Ipv4Address requester;
    uint32_t broadcastId;
    Ipv4Address requestedContent;
};

struct PendingInterestEntryKnownContentProvider
{
    PendingInterestEntryKnownContentProvider()
    {
        lastRelayNode = Ipv4Address("0.0.0.0");
    }
    
    Ipv4Address lastRelayNode;
    Ipv4Address requester;
    uint32_t broadcastId;
    Ipv4Address requestedContent;
    Ipv4Address contentProvider;
};

class Socket;
class Packet;

/**
 * \ingroup udpecho
 * \brief A Udp Echo client
 *
 * Every packet sent should be returned by the server and received here.
 */
class PaymentNetwork : public Application 
{
public:
    PaymentNetwork ();
    virtual ~PaymentNetwork ();

    // Avoid using the helper to setup an app
    void Setup (uint16_t port);
    // Set content requestor
    void RequestContent (Ipv4Address content);

protected:
    virtual void DoDispose (void);

private:

    virtual void StartApplication (void);
    virtual void StopApplication (void);
    // negibhor payment channel maintain
    void SendChMaintain ();
    void SendPacket(PktHeader header);
    void ScheduleTransmitPayChannelPackets (int num);
    Ipv4Address GetNodeAddress(void);
    
    void HandleOffchainMsg (Ptr<Socket> socket);
    void HandleRead (Ptr<Socket> socket);
    void HandleData(PktHeader *header);
    void HandleHello(PktHeader *header);
    void HandleDigest(PktHeader *header);
    void HandleInterestUnknownContentProvider(PktHeader *header);
    void HandleInterestKnownContentProvider(PktHeader *header);
    
    PktHeader *CreateDataPacketHeader(Ipv4Address requester,
                Ipv4Address destination, uint32_t broadcastId, Ipv4Address requestedContent);
    PktHeader *CreateHelloPacketHeader();
    PktHeader *CreateInterestPacketHeaderUnknownContentProvider(Ipv4Address requester,
                Ipv4Address destination, uint32_t broadcastId, Ipv4Address requestedContent);
    PktHeader *CreateInterestPacketHeaderKnownContentProvider(Ipv4Address requester,
                Ipv4Address destination, uint32_t broadcastId, Ipv4Address requestedContent, Ipv4Address contentProvider);
    PktHeader *CreateDigestPacketHeader(Ipv4Address destinationId);
                
                
    void ProcessPendingData(PktHeader *header);
    void ProcessPendingInterestKnownContentProvider(PktHeader *header);
    void ProcessPendingInterestUnknownContentProvider(PktHeader *header);
   
    
    //Print content line by line
    void PrintAllContent(Ipv4Address *array, uint32_t size);
    void DecideWhetherToSendContentNameDigest(PktHeader *header);

    void PaymentNetwork::SendChMaintain ();
    
    
    uint32_t m_count;
    Time m_interval;
    uint32_t m_size;

    uint32_t m_dataSize;
    uint8_t *m_data;

    uint32_t m_sent;
    Ptr<Socket> m_socket;
    Address m_peerAddress;
    uint16_t m_peerPort;
    EventId m_sendEvent;
    /// Callbacks for tracing thce packet Tx events
    TracedCallback<Ptr<const Packet> > m_txTrace;
    
    
    Relationship *m_relationship;
    Ipv4Address m_initialOwnedContent;
    Ipv4Address m_initialRequestedContent;
    ContentManager *m_contentManager;
    InterestManager *m_interestManager;
    uint32_t m_interestBroadcastId;
    
    vector<PendingInterestEntryKnownContentProvider> *m_pending_interest_known_content_provider;
    vector<PendingInterestEntryUnknownContentProvider> *m_pending_interest_unknown_content_provider;
    vector<PendingDataEntry> *m_pending_data;
    
    
    bool m_firstSuccess; //for accounting purpose
    Ptr<offchain::RoutingProtocol> m_routingProtocol;
      /// Handle neighbors payment channel
    Neighbors m_ngbChTable;
    
};

}

#endif /* PAYMENT_NETWORK_H */

