#include "balanceProof.h"
#include "ns3/log.h"
#include "ns3/hash.h"
//
// Created by ryu on 19. 6. 3.
//
NS_LOG_COMPONENT_DEFINE ("BalanceProof");

namespace ns3 {
	namespace offchain {
		UnSignedBalanceProof::UnSignedBalanceProof(uint32_t Token, Neighbor nb){
		 m_tokenAmout = Token;
		 m_balanceHash = GetHash32 (Token, 32);
		 m_channelID  = nb;
		}

		HashTimeLock::HashTimeLock(uint32_t amout){
		 uint32_t secret = getSecret();

		 m_amout = amout;
		 m_secretHash = GetHash32 (secret, 32);

		 setSecret(secret+1);
		}
	}
}

