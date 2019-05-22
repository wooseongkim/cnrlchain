/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "payment-network-helper.h"
#include "ns3/uinteger.h"
#include "ns3/names.h"
#include "ns3/string.h"

using namespace std;

namespace ns3 {

PaymentNetworkHelper::PaymentNetworkHelper (uint16_t port)
{
    m_factory.SetTypeId (PaymentNetwork::GetTypeId ());
    SetAttribute ("RemotePort", UintegerValue (port));
}

PaymentNetworkHelper::PaymentNetworkHelper (uint16_t port, Ipv4Address requestedContent)
{
    m_factory.SetTypeId (PaymentNetwork::GetTypeId ());
    SetAttribute ("RemotePort", UintegerValue (port));
    SetAttribute ("RequestedContent",  Ipv4AddressValue (requestedContent));
}

void 
PaymentNetworkHelper::SetAttribute (
    std::string name, 
    const AttributeValue &value)
{
    m_factory.Set (name, value);
}

void
PaymentNetworkHelper::SetFill (Ptr<Application> app, std::string fill)
{
    app->GetObject<PaymentNetwork>()->SetFill (fill);
}

void
PaymentNetworkHelper::SetFill (Ptr<Application> app, uint8_t fill, uint32_t dataLength)
{
    app->GetObject<PaymentNetwork>()->SetFill (fill, dataLength);
}

void
PaymentNetworkHelper::SetFill (Ptr<Application> app, uint8_t *fill, uint32_t fillLength, uint32_t dataLength)
{
    app->GetObject<PaymentNetwork>()->SetFill (fill, fillLength, dataLength);
}

ApplicationContainer
PaymentNetworkHelper::Install (Ptr<Node> node) const
{
    return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
PaymentNetworkHelper::Install (std::string nodeName) const
{
    Ptr<Node> node = Names::Find<Node> (nodeName);
    return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
PaymentNetworkHelper::Install (NodeContainer c) const
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
        apps.Add (InstallPriv (*i));
    }

    return apps;
}

Ptr<Application>
PaymentNetworkHelper::InstallPriv (Ptr<Node> node) const
{
    Ptr<Application> app = m_factory.Create<PaymentNetwork> ();
    node->AddApplication (app);

    return app;
}

}

