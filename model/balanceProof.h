//
// Created by ryu on 19. 5. 31.
//

#ifndef BALANCEPROOF_H
#define BALANCEPROOF_H

#include <iostream>
#include <bitset>


#endif //C___BALANCEPROOF_H

namespace std{
    typedef bitset<32> BYTE;
}
namespace ns3 {
    namespace offchain {
        class UnSignedBalanceProof {
        public:
            UnSignedBalanceProof(int32_t tokenAmout);
            int32_t getTokenAmout() const {return m_tokenAmout;}
            void setTokenAmout(int32_t tokenAmout) {UnSignedBalanceProof::m_tokenAmout = tokenAmout;}
            const std::BYTE &getBalanceHash() const {return m_balanceHash;}
            void setBalanceHash(const std::BYTE &balanceHash) {UnSignedBalanceProof::m_balanceHash = balanceHash;}
            int32_t getChannelId() const {return m_channelID;}
            void setChannelId(int32_t channelId) {m_channelID = channelId;}

        private:
            int32_t m_tokenAmout;
            std::BYTE m_balanceHash;
            //BYTE additionalHash;
            int32_t m_channelID;
        };

        class SignedBalanceProof : public UnSignedBalanceProof{
        public:
            SignedBalanceProof();
            SignedBalanceProof(int32_t tokenAmout, const std::BYTE &balanceHash, int32_t channelId,
                               const std::BYTE &signature, const std::BYTE &sender)
                    : UnSignedBalanceProof(tokenAmout,balanceHash,channelId),Signature(signature),Sender(sender) {}
            const std::BYTE &getSignature() const {return Signature;}
            void setSignature(const std::BYTE &signature) {Signature = signature;}
            const std::BYTE &getSender() const {return Sender;}
            void setSender(const std::BYTE &sender) {Sender = sender;}

        private :
            std::BYTE Signature;
            std::BYTE Sender;
        };

        class HashTimeLock {
        public:
            HashTimeLock(uint32_t amout);
            uint32_t getAmout() const {return m_amout;}
            void setAmout(uint32_t amout) {HashTimeLock::m_amout = amout;}
            const std::BYTE &getSecretHash() const {return m_secretHash;}
            void setSecretHash(const std::BYTE &secretHash) {HashTimeLock::m_secretHash = secretHash;}
            const std::BYTE &getLockHash() const {return m_lockHash;}
            void setLockHash(const std::BYTE &lockHash) {HashTimeLock::m_lockHash = lockHash;}

        private:
            uint32_t m_amout;
            std::BYTE m_secretHash;
            std::BYTE m_lockHash;
            uint32_t secret = 0;
        };


    }
}