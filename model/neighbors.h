#ifndef OFFCHAINNEIGHBOR_H
#define OFFCHAINNEIGHBOR_H

#include "ns3/simulator.h"
#include "ns3/timer.h"
#include "ns3/ipv4-address.h"
#include "ns3/callback.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/arp-cache.h"
#include <vector>

namespace ns3
{
namespace offchain
{
class RoutingProtocol;
/**
 * \brief maintain list of payment active neighbors
 */
class Neighbors
{
public:
  /// c-tor
  Neighbors (Time delay);
  /// Neighbor description
  struct Neighbor
  {
    Ipv4Address m_neighborAddress;
    Time m_expireTime;    
    uint32_t m_totalChDeposit;  //my total channel deposit
    uint32_t m_availChDeposit;  // my available balance including received amount
    uint32_t m_peerTotalChDeposit;  //peer total channel deposit
    uint32_t m_peerAvailChDeposit;  // peer available balance including received amount 
    bool close;

    Neighbor (Ipv4Address ip, Time t) :
      m_neighborAddress (ip), m_expireTime (t), close (false)
    {
    }
  };
  
  /// Return expire time for neighbor node with address addr, if exists, else return 0.
  Time GetExpireTime (Ipv4Address addr);
  /// Check that node with address addr  is neighbor
  bool IsNeighbor (Ipv4Address addr);
  /// Update expire time for entry with address addr, if it exists, else add new entry
  void Update (Ipv4Address addr, Time expire);
  /// Remove all expired entries
  void Purge ();
  /// Schedule m_ntimer.
  void ScheduleTimer ();
  /// Remove all entries
  void Clear () { m_nb.clear (); }
  /// open channel by deposit
  void SetChDeposit(Ipv4Address addr);  
  // get amount of total channel deposit
  uint32_t GetChDeposit(Ipv4Address addr);
  // get current available channel deposit
  uint32_t GetChAvailDeposit(Ipv4Address addr);
  // decrease channel deposit
  void DecChDeposit(Ipv4Address addr, uint32_t pay);
  //increase channel deposit
  void IncChDeposit(Ipv4Address addr, uint32_t pay);

  /// Get callback to ProcessTxError
  Callback<void, WifiMacHeader const &> GetTxErrorCallback () const { return m_txErrorCallback; }
 
  ///\name Handle link failure callback
  //\{
  void SetCallback (Callback<void, Ipv4Address> cb) { m_handleLinkFailure = cb; }
  Callback<void, Ipv4Address> GetCallback () const { return m_handleLinkFailure; }
  //\}
private:
  /// link failure callback
  Callback<void, Ipv4Address> m_handleLinkFailure;
  /// TX error callback
  Callback<void, WifiMacHeader const &> m_txErrorCallback;
  /// Timer for neighbor's list. Schedule Purge().
  Timer m_ntimer;
  /// vector of entries
  std::vector<Neighbor> m_nb;
  //

  /// Process layer 2 TX error notification
  void ProcessTxError (WifiMacHeader const &);
};

}
}

#endif /* OFFCHAINNEIGHBOR_H */
