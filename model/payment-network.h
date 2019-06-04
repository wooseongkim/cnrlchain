/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PAYMENT_NETWORK_H
#define PAYMENT_NETWORK_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "ns3/traced-callback.h"

#include "offchain-routing.h"
#include "payroute-packet.h"
#include "neighbors.h"
#include "balanceProof.h"
#include <map>

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

	struct PendingPaymentEntry
	{
		Ipv4Address previousNode;
		Ptr<offchain::HashTimeLock> hashTimeLock;
		Ptr<offchain::UnSignedBalanceProof> unSignedBalanceProof;
		uint32_t secret;
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

		uint32_t HandleTransRreq (Ipv4Address orig, uint32_t amount);
		uint32_t HandleTransRrep (Ipv4Address orig, uint32_t amount);

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

		/**
		*       payment
		*/
		int SendLockBP (uint32_t paymentID, Ipv4Address dst, uint32_t finalAmout);
		int RecvLockBP (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
		void ScheduleLockBPRetry (Ipv4Address dst);

		int SendSecretREQ(uint32_t paymentID, Ipv4Address target, Ipv4Address initiator, Ptr<offchain::HashTimeLock> hashTimeLock);
		int RecvSecretREQ(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
		void ScheduleSecretREQ (Ipv4Address dst);

		int RecvSecretACK(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
		void ScheduleSecretACK (Ipv4Address dst);

		int SendSecretReveal(uint32_t paymentID, Ipv4Address dst, Ipv4Address origin, std::BYTE secret, std::BYTE secretHash);
		int RecvSecretReveal(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
		void ScheduleSecretReveal(Ipv4Address dst);

		int SendBP(int32_t paymentID, Ipv4Address dst, std::BYTE secret, std::BYTE secretHash, uint32_t finalAmout);
		int RecvBP(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
		void ScheduleSendBP (Ipv4Address dst);
		/**
		*       payment parameter
		*/
		uint32_t m_paymentID;
		map<uint32_t, PendingPaymentEntry> m_pending_payment_entry;
		Ptr<offchain::HashTimeLock> m_hashTimeLock;
		Ptr<offchain::UnSignedBalanceProof> m_unSignedBalanceProof;
		uint32_t m_secret;

		static const uint32_t SEND_LOCKEDBP_PORT;
		static const uint32_t SEND_SECRET_REQ_PORT;
		static const uint32_t SEND_SECRET_REVEAL_PORT;
		static const uint32_t SEND_BP_PORT;

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
		Ptr<offchain::PaymentRoutingProtocol> m_routingProtocol;
		/// Handle neighbors payment channel
		Ptr<offchain::Neighbors> m_ngbChTable;

	};

}

#endif /* PAYMENT_NETWORK_H */

