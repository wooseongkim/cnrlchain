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
            UnSignedBalanceProof();
            UnSignedBalanceProof(int32_t tokenAmout, const std::BYTE &balanceHash, int32_t channelId) : tokenAmout(
                    tokenAmout), balanceHash(balanceHash), channelID(channelId) {}

            int32_t getTokenAmout() const {return tokenAmout;}
            void setTokenAmout(int32_t tokenAmout) {UnSignedBalanceProof::tokenAmout = tokenAmout;}
            const std::BYTE &getBalanceHash() const {return balanceHash;}
            void setBalanceHash(const std::BYTE &balanceHash) {UnSignedBalanceProof::balanceHash = balanceHash;}
            int32_t getChannelId() const {return channelID;}
            void setChannelId(int32_t channelId) {channelID = channelId;}

        private:
            int32_t tokenAmout;
            std::BYTE balanceHash;
            //BYTE additionalHash;
            int32_t channelID;
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
            HashTimeLock();
            HashTimeLock(uint32_t amout, const std::BYTE &secretHash, const std::BYTE &lockHash)
                    : amout(amout),secretHash(secretHash),lockHash(lockHash) {}

            uint32_t getAmout() const {return amout;}
            void setAmout(uint32_t amout) {HashTimeLock::amout = amout;}
            const std::BYTE &getSecretHash() const {return secretHash;}
            void setSecretHash(const std::BYTE &secretHash) {HashTimeLock::secretHash = secretHash;}
            const std::BYTE &getLockHash() const {return lockHash;}
            void setLockHash(const std::BYTE &lockHash) {HashTimeLock::lockHash = lockHash;}

        private:
            uint32_t amout;
            std::BYTE secretHash;
            std::BYTE lockHash;
        };


    }
}