#ifndef PAYMENTPACKET_H
#define PAYMENTPACKET_H

#include <iostream>
#include "ns3/header.h"
#include "ns3/enum.h"
#include "ns3/ipv4-address.h"
#include <map>
#include "ns3/nstime.h"
#include "balanceProof.h"
#include <bitset>

namespace std{
	typedef bitset<32> BYTE;
}


namespace ns3 {
	namespace offchain {


		enum MessageType
		{
			OFFCHAIN_ROUTING_RREQ  = 1,
			OFFCHAIN_ROUTING_RREP  = 2,
			OFFCHAIN_ROUTING_HELLO = 3,
			OFFCHAIN_ROUTING_ACK = 4,
			// PAYMENT
					SEND_LOCKED_UNSIGNED_BP = 5,
			SEND_SECRET_REQ= 6,
			SEND_SECRET_ACK = 7,
			SEND_SECRET_REVEAL= 8,
			SEND_BP = 9

		};

		class TypeHeader : public Header
		{
		public:
			/// c-tor
			TypeHeader (MessageType t = OFFCHAIN_ROUTING_RREP);

			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			/// Return type
			MessageType Get () const { return m_type; }
			/// Check that type if valid
			bool IsValid () const { return m_valid; }
			bool operator== (TypeHeader const & o) const;
		private:
			MessageType m_type;
			bool m_valid;
		};

		std::ostream & operator<< (std::ostream & os, TypeHeader const & h);

		class RreqHeader : public Header
		{
		public:

			/// c-tor
			RreqHeader (uint8_t flags = 0, uint8_t reserved = 0, uint8_t hopCount = 0,
						uint32_t requestID = 0, Ipv4Address dst = Ipv4Address (),
						uint32_t dstSeqNo = 0, Ipv4Address origin = Ipv4Address (),
						uint32_t originSeqNo = 0, uint32_t transactionAmount = 0);

			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			///\name Fields
			//\{
			void SetHopCount (uint8_t count) { m_hopCount = count; }
			uint8_t GetHopCount () const { return m_hopCount; }
			void SetId (uint32_t id) { m_requestID = id; }
			uint32_t GetId () const { return m_requestID; }
			void SetDst (Ipv4Address a) { m_dst = a; }
			Ipv4Address GetDst () const { return m_dst; }
			void SetDstSeqno (uint32_t s) { m_dstSeqNo = s; }
			uint32_t GetDstSeqno () const { return m_dstSeqNo; }
			void SetOrigin (Ipv4Address a) { m_origin = a; }
			Ipv4Address GetOrigin () const { return m_origin; }
			void SetOriginSeqno (uint32_t s) { m_originSeqNo = s; }
			uint32_t GetOriginSeqno () const { return m_originSeqNo; }
			void SetTransAmount (uint32_t t) { m_transactionAmount = t; }
			uint32_t GetTransAmount () const { return m_transactionAmount; }
			//\}

			///\name Flags
			//\{
			void SetGratiousRrep (bool f);
			bool GetGratiousRrep () const;
			void SetDestinationOnly (bool f);
			bool GetDestinationOnly () const;
			void SetUnknownSeqno (bool f);
			bool GetUnknownSeqno () const;
			//\}

			bool operator== (RreqHeader const & o) const;
		private:
			uint8_t        m_flags;          ///< |J|R|G|D|U| bit flags, see RFC
			uint8_t        m_reserved;       ///< Not used
			uint8_t        m_hopCount;       ///< Hop Count
			uint32_t       m_requestID;      ///< RREQ ID
			Ipv4Address    m_dst;            ///< Destination IP Address
			uint32_t       m_dstSeqNo;       ///< Destination Sequence Number, 중복 검사
			Ipv4Address    m_origin;         ///< Originator IP Address
			uint32_t       m_originSeqNo;    ///< Source Sequence Number
			uint32_t       m_transactionAmount;    ///< payment amount
		};

		std::ostream & operator<< (std::ostream & os, RreqHeader const &);

		class RrepHeader : public Header
		{
		public:
			/// c-tor
			RrepHeader (uint8_t hopCount = 0, Ipv4Address dst =
			Ipv4Address (), uint32_t dstSeqNo = 0, Ipv4Address origin =
			Ipv4Address (), Time lifetime = MilliSeconds (0), uint32_t reward = 0);
			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			///\name Fields
			//\{
			void SetHopCount (uint8_t count) { m_hopCount = count; }
			uint8_t GetHopCount () const { return m_hopCount; }
			void SetDst (Ipv4Address a) { m_dst = a; }
			Ipv4Address GetDst () const { return m_dst; }
			void SetDstSeqno (uint32_t s) { m_dstSeqNo = s; }
			uint32_t GetDstSeqno () const { return m_dstSeqNo; }
			void SetOrigin (Ipv4Address a) { m_origin = a; }
			Ipv4Address GetOrigin () const { return m_origin; }
			void SetLifeTime (Time t);
			Time GetLifeTime () const;
			void SetAccRewards (uint32_t reward) { m_accRewards = reward; }
			uint32_t GetAccRewards () const {return m_accRewards; }

			//\}

			///\name Flags
			//\{
			void SetAckRequired (bool f);
			bool GetAckRequired () const;

			//\}

			bool operator== (RrepHeader const & o) const;
		private:
			uint8_t       m_flags;                  ///< A - acknowledgment required flag
			uint8_t       m_prefixSize;         ///< Prefix Size
			uint8_t             m_hopCount;         ///< Hop Count
			Ipv4Address   m_dst;              ///< Destination IP Address
			uint32_t      m_dstSeqNo;         ///< Destination Sequence Number
			Ipv4Address     m_origin;           ///< Source IP Address
			uint32_t      m_lifeTime;         ///< Lifetime (in milliseconds)
			uint32_t      m_accRewards;         ///< cumulative rewards
		};

		std::ostream & operator<< (std::ostream & os, RrepHeader const &);


		class HelloHeader : public Header
		{
		public:
			/// c-tor
			HelloHeader (Ipv4Address dst = Ipv4Address(), uint32_t dstSeqNo = 0,
						 Ipv4Address origin = Ipv4Address(), Time lifetime = MilliSeconds(0),
						 uint32_t curDeposit=0);
			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			///\name Fields
			//\{

			void SetDst (Ipv4Address a) { m_dst = a; }
			Ipv4Address GetDst () const { return m_dst; }
			void SetDstSeqno (uint32_t s) { m_dstSeqNo = s; }
			uint32_t GetDstSeqno () const { return m_dstSeqNo; }
			void SetOrigin (Ipv4Address a) { m_origin = a; }
			Ipv4Address GetOrigin () const { return m_origin; }
			void SetLifeTime (Time t);
			Time GetLifeTime () const;
			void SetAvailableDeposit (uint32_t depo) { m_chAvailDeposit = depo; }
			uint32_t GetAvailableDeposit () const {return m_chAvailDeposit; }

			//\}
			void SetAckRequired (bool f);
			bool GetAckRequired () const;

			bool operator== (HelloHeader const & o) const;
		private:
			uint8_t       m_flags;                  ///< A - acknowledgment required flag
			Ipv4Address   m_dst;              ///< Destination IP Address
			uint32_t      m_dstSeqNo;         ///< Destination Sequence Number
			Ipv4Address     m_origin;           ///< Source IP Address
			uint32_t      m_lifeTime;         ///< Lifetime (in milliseconds)
			uint32_t      m_chAvailDeposit;         ///< available deposit for payment
		};

		std::ostream & operator<< (std::ostream & os, HelloHeader const &);

		class LockBPHeader : public Header
		{
		public:
			/// c-tor
			LockBPHeader (uint32_t paymentID = 0, Ipv4Address target = Ipv4Address(),
						  Ipv4Address initiator = Ipv4Address(), HashTimeLock hashTimeLock = HashTimeLock(0),
						  UnSignedBalanceProof unSignedBalanceProof = UnSignedBalanceProof(0));

			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			///\name Fields
			//\{
			void SetId (uint32_t id) { m_paymentID = id; }
			uint32_t GetId () const { return m_paymentID; }
			void SetTarget (Ipv4Address a) { m_target = a; }
			Ipv4Address GetTarget () const { return m_target; }
			void SetInitiator (Ipv4Address a) { m_initiator = a; }
			Ipv4Address GetInitiator () const { return m_initiator; }
			void SetHashTimeLock(HashTimeLock Lock) {m_hashTimeLock = Lock;}
			HashTimeLock GetHashTimeLock() const {return m_hashTimeLock;}
			void SetUnsignedBalanceProof (UnSignedBalanceProof BP) { m_unSignedBalanceProof= BP; }
			UnSignedBalanceProof GetUnsignedBalanceProof () const { return m_unSignedBalanceProof; }
			//\}

			///\name Flags
			//\{
//        void SetGratiousRrep (bool f);
//        bool GetGratiousRrep () const;
//        void SetDestinationOnly (bool f);
//        bool GetDestinationOnly () const;
//        void SetUnknownSeqno (bool f);
//        bool GetUnknownSeqno () const;
			//\}

			bool operator== (LockBPHeader const & o) const;
		private:
			uint32_t       m_paymentID;      ///< payment ID
			Ipv4Address    m_target;         ///< Destination IP Address
			Ipv4Address    m_initiator;      ///< Originator IP Address
			Ptr<offchain::HashTimeLock> m_hashTimeLock;
			Ptr<offchain::UnSignedBalanceProof> m_unSignedBalanceProof;
			//        HashTimeLock   m_hashTimeLock;
//        UnSignedBalanceProof
//                m_unSignedBalanceProof;    ///<  UnsignedBalanceProof
		};

		std::ostream & operator<< (std::ostream & os, LockBPHeader const &);

		class SecretReqHeader : public Header
		{
		public:
			/// c-tor
			SecretReqHeader (uint32_t paymentID = 0, Ipv4Address target = Ipv4Address (),
							 Ipv4Address initiator = Ipv4Address (),
							 HashTimeLock hashTimeLock = HashTimeLock(0));
			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			///\name Fields
			//\{
			void SetId (uint32_t id) { m_paymentID = id; }
			uint32_t GetId () const { return m_paymentID; }
			void SetTarget (Ipv4Address a) { m_target = a; }
			Ipv4Address GetTarget () const { return m_target; }
			void SetInitiator (Ipv4Address a) { m_initiator = a; }
			Ipv4Address GetInitiator () const { return m_initiator; }
//        void SetPaymentAmout(uint32_t paymentAmout) {m_paymentAmout = paymentAmout;}
//        uint32_t GetPaymentAmout() const {return m_paymentAmout;}
			void SetHashTimeLock(HashTimeLock Lock) {m_hashTimeLock = Lock;}
			HashTimeLock GetHashTimeLock() const {return m_hashTimeLock;}

			bool operator== (SecretReqHeader const & o) const;
		private:
			uint32_t       m_paymentID;      ///< payment ID
			Ipv4Address    m_target;         ///< Destination IP Address
			Ipv4Address    m_initiator;      ///< Originator IP Address
//        uint32_t       m_paymentAmout;
			Ptr<offchain::HashTimeLock> m_hashTimeLock;

		};

		std::ostream & operator<< (std::ostream & os, SecretReqHeader const &);

		class SecretAckHeader : public Header
		{
		public:
			/// c-tor
			SecretAckHeader (uint32_t paymentID = 0, Ipv4Address dst = Ipv4Address (),
							 Ipv4Address origin = Ipv4Address (), std::BYTE secret = 0,
							 std::BYTE secretHash = 0);
			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			///\name Fields
			//\{
			void SetId (uint32_t id) { m_paymentID = id; }
			uint32_t GetId () const { return m_paymentID; }
			void SetDst (Ipv4Address a) { m_dst = a; }
			Ipv4Address GetDst () const { return m_dst; }
			void SetOrigin (Ipv4Address a) { m_origin = a; }
			Ipv4Address GetOrigin () const { return m_origin; }
			void SetSecret(std::BYTE secret) {m_secret = secret;}
			std::BYTE GetSecret() const {return m_secret;}
			void SetSecretHash (std::BYTE secretHash) { m_secretHash= secretHash; }
			std::BYTE GetSecretHash() const { return m_secretHash; }

			bool operator== (SecretAckHeader const & o) const;
		private:
			uint32_t       m_paymentID;      ///< payment ID
			Ipv4Address    m_dst;            ///< Destination IP Address
			Ipv4Address    m_origin;         ///< Originator IP Address
			std::BYTE      m_secret;
			std::BYTE      m_secretHash;
		};

		std::ostream & operator<< (std::ostream & os, SecretAckHeader const &);


		class SecretRevealHeader : public Header
		{
		public:
			/// c-tor
			SecretRevealHeader (uint32_t paymentID = 0, Ipv4Address dst = Ipv4Address (),
								Ipv4Address origin = Ipv4Address (), std::BYTE secret = 0,
								std::BYTE secretHash = 0);
			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}

			///\name Fields
			//\{
			void SetId (uint32_t id) { m_paymentID = id; }
			uint32_t GetId () const { return m_paymentID; }
			void SetDst (Ipv4Address a) { m_dst = a; }
			Ipv4Address GetDst () const { return m_dst; }
			void SetOrigin (Ipv4Address a) { m_origin = a; }
			Ipv4Address GetOrigin () const { return m_origin; }
			void SetSecret(std::BYTE secret) {m_secret = secret;}
			std::BYTE GetSecret() const {return m_secret;}
			void SetSecretHash (std::BYTE secretHash) { m_secretHash= secretHash; }
			std::BYTE GetSecretHash() const { return m_secretHash; }

			bool operator== (SecretRevealHeader const & o) const;
		private:
			uint32_t       m_paymentID;      ///< payment ID
			Ipv4Address    m_dst;            ///< Destination IP Address
			Ipv4Address    m_origin;         ///< Originator IP Address
			std::BYTE      m_secret;
			std::BYTE      m_secretHash;
		};

		std::ostream & operator<< (std::ostream & os, SecretRevealHeader const &);



		class BPHeader : public Header
		{
		public:
			/// c-tor
			BPHeader (uint32_t paymentID = 0, Ipv4Address dst = Ipv4Address (),
					  Ipv4Address origin = Ipv4Address (), std::BYTE secret = 0, std::BYTE secretHash = 0,
					  UnSignedBalanceProof unSignedBalanceProof = UnSignedBalanceProof(0));
			///\name Header serialization/deserialization
			//\{
			static TypeId GetTypeId ();
			TypeId GetInstanceTypeId () const;
			uint32_t GetSerializedSize () const;
			void Serialize (Buffer::Iterator start) const;
			uint32_t Deserialize (Buffer::Iterator start);
			void Print (std::ostream &os) const;
			//\}
			///\name Fields
			//\{
			void SetId (uint32_t id) { m_paymentID = id; }
			uint32_t GetId () const { return m_paymentID; }
			void SetDst (Ipv4Address a) { m_dst = a; }
			Ipv4Address GetDst () const { return m_dst; }
			void SetOrigin (Ipv4Address a) { m_origin = a; }
			Ipv4Address GetOrigin () const { return m_origin; }
			void SetSecret(std::BYTE secret) {m_secret = secret;}
			std::BYTE GetSecret() const {return m_secret;}
			void SetSecretHash (std::BYTE secretHash) { m_secretHash= secretHash; }
			std::BYTE GetSecretHash() const { return m_secretHash; }
			void SetUnsignedBalanceProof (UnSignedBalanceProof BP) { m_unSignedBalanceProof= BP; }
			UnSignedBalanceProof GetUnsignedBalanceProof () const { return m_unSignedBalanceProof; }


			bool operator== (BPHeader const & o) const;

		private:
			uint32_t       m_paymentID;      ///< payment ID
			Ipv4Address    m_dst;            ///< Destination IP Address
			Ipv4Address    m_origin;         ///< Originator IP Address
			std::BYTE      m_secret;
			std::BYTE      m_secretHash;
			Ptr<offchain::UnSignedBalanceProof> m_unSignedBalanceProof;
		};

		std::ostream & operator<< (std::ostream & os, BPHeader const &);

	}
}
#endif /* PAYMENTPACKET_H */