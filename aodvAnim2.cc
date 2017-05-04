/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is an example script for AODV manet routing protocol.
 */

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dsdv-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/olsr-module.h"
#include "ns3/v4ping-helper.h"
#include "ns3/wifi-module.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>

using namespace ns3;

/**
 * \brief Smart-meter mesh test script.
 *
 * Nodes form a static grid (gridWidth columns, step meters apart). Node 0 is
 * the data concentrator: in "meters" mode every other node periodically sends
 * a reading to it over UDP; in "ping" mode node 0 pings the last node.
 * Optionally one node is moved out of range mid-run to force route repair.
 */
class AodvExample {
public:
  AodvExample();
  /// Configure script parameters, \return true on successful configuration
  bool Configure(int argc, char **argv);
  /// Run simulation
  void Run();
  /// Report results
  void Report(std::ostream &os);

private:
  // parameters
  /// Number of nodes
  uint32_t size;
  /// Distance between nodes, meters
  double step;
  uint32_t gridWidth;
  /// Simulation time, seconds
  double totalTime;
  /// Write per-device PCAP traces if true
  bool pcap;
  /// Print routes if true
  bool printRoutes;
  std::string protocol;
  std::string traffic;
  double interval;
  uint32_t packetSize;
  double warmup;
  int32_t failNode;
  double failAt;
  uint32_t run;
  bool anim;
  std::string animFile;
  std::string icon;
  std::string csv;
  std::string nodeCsv;

  // network
  NodeContainer nodes;
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;

  Ptr<FlowMonitor> monitor;
  FlowMonitorHelper flowHelper;
  std::vector<Ptr<Socket> > meterSockets;
  std::vector<uint64_t> readingsSent;
  uint64_t ctrlPackets;
  uint64_t ctrlBytes;

private:
  void SendReading(uint32_t meter);
  void OnIpTx(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t iface);
  void CreateNodes();
  void CreateDevices();
  void InstallInternetStack();
  void InstallApplications();
};

int main(int argc, char **argv) {
  AodvExample test;
  if (!test.Configure(argc, argv))
    NS_FATAL_ERROR("Configuration failed. Aborted.");

  test.Run();
  test.Report(std::cout);
  Simulator::Destroy();
  return 0;
}

//-----------------------------------------------------------------------------
static const uint16_t kDataPort = 9000;

AodvExample::AodvExample()
    : size(50), step(100), gridWidth(5), totalTime(60), pcap(false),
      printRoutes(true), protocol("aodv"), traffic("meters"), interval(1.0),
      packetSize(128), warmup(10), failNode(-2), failAt(-1), run(1),
      anim(true), animFile("scratch/artichoke/aodv.xml"),
      icon("scratch/artichoke/page1_pic1.png"), csv(""), nodeCsv(""),
      ctrlPackets(0), ctrlBytes(0) {}

void AodvExample::SendReading(uint32_t meter) {
  meterSockets[meter]->Send(Create<Packet>(packetSize));
  ++readingsSent[meter];
  if (Simulator::Now() + Seconds(interval) < Seconds(totalTime - 1))
    Simulator::Schedule(Seconds(interval), &AodvExample::SendReading, this,
                        meter);
}

void AodvExample::OnIpTx(Ptr<const Packet> packet, Ptr<Ipv4>, uint32_t) {
  Ptr<Packet> copy = packet->Copy();
  Ipv4Header ip;
  copy->RemoveHeader(ip);
  if (ip.GetProtocol() != 17)
    return;
  UdpHeader udp;
  copy->PeekHeader(udp);
  uint16_t port = udp.GetDestinationPort();
  if (port == 654 || port == 698 || port == 269) {
    ++ctrlPackets;
    ctrlBytes += packet->GetSize();
  }
}

bool AodvExample::Configure(int argc, char **argv) {
  // Enable AODV logs by default. Comment this if too noisy
  // LogComponentEnable("AodvRoutingProtocol", LOG_LEVEL_ALL);

  CommandLine cmd;

  cmd.AddValue("pcap", "Write PCAP traces.", pcap);
  cmd.AddValue("printRoutes", "Print routing table dumps.", printRoutes);
  cmd.AddValue("size", "Number of nodes.", size);
  cmd.AddValue("time", "Simulation time, s.", totalTime);
  cmd.AddValue("step", "Grid step, m", step);
  cmd.AddValue("gridWidth", "Nodes per grid column.", gridWidth);
  cmd.AddValue("protocol", "Routing protocol: aodv|olsr|dsdv.", protocol);
  cmd.AddValue("traffic", "Traffic pattern: meters|ping.", traffic);
  cmd.AddValue("interval", "Meter reporting interval, s.", interval);
  cmd.AddValue("packetSize", "Meter reading size, bytes.", packetSize);
  cmd.AddValue("warmup", "Time before meters start reporting, s.", warmup);
  cmd.AddValue("failNode", "Node moved out of range (-1 none, -2 middle).",
               failNode);
  cmd.AddValue("failAt", "Failure time, s (negative: time/3).", failAt);
  cmd.AddValue("run", "RNG run number.", run);
  cmd.AddValue("anim", "Write NetAnim XML.", anim);
  cmd.AddValue("animFile", "NetAnim XML output path.", animFile);
  cmd.AddValue("icon", "Node icon path for NetAnim.", icon);
  cmd.AddValue("csv", "Append a summary row to this CSV file.", csv);
  cmd.AddValue("nodeCsv", "Write per-meter delivery stats to this CSV.",
               nodeCsv);

  cmd.Parse(argc, argv);

  SeedManager::SetSeed(12345);
  SeedManager::SetRun(run);

  if (failNode == -2)
    failNode = size / 2;
  if (failAt < 0)
    failAt = totalTime / 3;
  if (failNode == 0 || failNode >= (int32_t)size)
    failNode = -1;

  return (protocol == "aodv" || protocol == "olsr" || protocol == "dsdv") &&
         (traffic == "meters" || traffic == "ping") && size >= 2;
}

void AodvExample::Run() {
  //  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold",
  //  UintegerValue (1)); // enable rts cts all the time.
  CreateNodes();
  CreateDevices();
  InstallInternetStack();
  InstallApplications();

  std::cout << "Starting simulation for " << totalTime << " s ...\n";

  Simulator::Stop(Seconds(totalTime));

  monitor = flowHelper.InstallAll();

  AnimationInterface *animIface = 0;
  if (anim) {
    animIface = new AnimationInterface(animFile);
    animIface->SetMaxPktsPerTraceFile(5000000);
    uint32_t urlImage = animIface->AddResource(icon);
    for (uint32_t i = 0; i < size; ++i) {
      Ptr<MobilityModel> mob = nodes.Get(i)->GetObject<MobilityModel>();
      Vector pos = mob->GetPosition();
      animIface->SetConstantPosition(nodes.Get(i), pos.x, pos.y);
      uint32_t nodeId = nodes.Get(i)->GetId();
      animIface->UpdateNodeImage(nodeId, urlImage);
      animIface->UpdateNodeSize(nodeId, 80.0, 80.0);
    }
  }

  Simulator::Run();
  monitor->CheckForLostPackets();
  delete animIface;
}

void AodvExample::Report(std::ostream &os) {
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  uint64_t tx = 0, rx = 0, rxBytes = 0;
  double delaySum = 0;
  std::map<uint32_t, std::pair<uint64_t, uint64_t> > perNode;
  std::map<uint32_t, double> perNodeDelay;

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator it =
           stats.begin();
       it != stats.end(); ++it) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it->first);
    bool isData = traffic == "ping"
                      ? t.protocol == 1 &&
                            t.sourceAddress == interfaces.GetAddress(0)
                      : t.protocol == 17 && t.destinationPort == kDataPort;
    if (!isData)
      continue;
    rx += it->second.rxPackets;
    rxBytes += it->second.rxBytes;
    delaySum += it->second.delaySum.GetSeconds();
    for (uint32_t i = 0; i < size; ++i) {
      if (interfaces.GetAddress(i) == t.sourceAddress) {
        perNode[i].first += it->second.txPackets;
        perNode[i].second += it->second.rxPackets;
        perNodeDelay[i] += it->second.delaySum.GetSeconds();
      }
    }
  }
  if (traffic == "meters") {
    for (uint32_t i = 0; i < size; ++i)
      perNode[i].first = readingsSent[i];
  }
  for (uint32_t i = 0; i < size; ++i)
    tx += perNode[i].first;
  uint64_t ctrlTx = ctrlPackets;

  double pdr = tx ? 100.0 * rx / tx : 0;
  double meanDelayMs = rx ? 1000.0 * delaySum / rx : 0;
  double activeTime = totalTime - warmup;
  double throughputKbps = activeTime > 0 ? rxBytes * 8.0 / activeTime / 1000 : 0;

  os << "protocol=" << protocol << " traffic=" << traffic << " nodes=" << size
     << " step=" << step << " run=" << run << " failNode=" << failNode << "\n";
  if (traffic == "ping") {
    os << "routing ctrl packets:  " << ctrlTx << " (" << ctrlBytes / 1024
       << " KiB)\n";
    return;
  }
  os << "data packets sent:     " << tx << "\n"
     << "data packets received: " << rx << "\n"
     << "PDR:                   " << pdr << " %\n"
     << "mean delay:            " << meanDelayMs << " ms\n"
     << "goodput:               " << throughputKbps << " kbit/s\n"
     << "routing ctrl packets:  " << ctrlTx << " (" << ctrlBytes / 1024
     << " KiB)\n";

  if (!csv.empty()) {
    std::ifstream probe(csv.c_str());
    bool needHeader = !probe.good() || probe.peek() == EOF;
    probe.close();
    std::ofstream out(csv.c_str(), std::ios::app);
    if (needHeader)
      out << "protocol,traffic,nodes,step,interval,run,failNode,sent,received,"
             "pdr,meanDelayMs,goodputKbps,ctrlPackets,ctrlBytes\n";
    out << protocol << "," << traffic << "," << size << "," << step << ","
        << interval << "," << run << "," << failNode << "," << tx << "," << rx
        << "," << pdr << "," << meanDelayMs << "," << throughputKbps << ","
        << ctrlTx << "," << ctrlBytes << "\n";
  }

  if (!nodeCsv.empty()) {
    std::ofstream out(nodeCsv.c_str());
    out << "node,x,y,sent,received,pdr,meanDelayMs\n";
    for (uint32_t i = 0; i < size; ++i) {
      Vector pos = nodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
      uint64_t s = perNode[i].first, r = perNode[i].second;
      out << i << "," << pos.x << "," << pos.y << "," << s << "," << r << ","
          << (s ? 100.0 * r / s : 0) << ","
          << (r ? 1000.0 * perNodeDelay[i] / r : 0) << "\n";
    }
  }
}

void AodvExample::CreateNodes() {
  std::cout << "Creating " << (unsigned)size << " nodes " << step
            << " m apart.\n";
  nodes.Create(size);
  // Name nodes
  for (uint32_t i = 0; i < size; ++i) {
    std::ostringstream os;
    os << "node-" << i;
    Names::Add(os.str(), nodes.Get(i));
  }
  // Create static grid
  MobilityHelper mobility;
  mobility.SetPositionAllocator(
      "ns3::GridPositionAllocator", "MinX", DoubleValue(0.0), "MinY",
      DoubleValue(0.0), "DeltaX", DoubleValue(step), "DeltaY",
      DoubleValue(step), "GridWidth", UintegerValue(gridWidth), "LayoutType",
      StringValue("ColumnFirst")); // RowFirst
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);
}

void AodvExample::CreateDevices() {
  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  wifiPhy.SetChannel(wifiChannel.Create());
  WifiHelper wifi;
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                               StringValue("OfdmRate6Mbps"), "RtsCtsThreshold",
                               UintegerValue(0));
  devices = wifi.Install(wifiPhy, wifiMac, nodes);

  if (pcap) {
    wifiPhy.EnablePcapAll(std::string("aodv"));
  }
}

void AodvExample::InstallInternetStack() {
  AodvHelper aodv;
  OlsrHelper olsr;
  DsdvHelper dsdv;
  const Ipv4RoutingHelper *routing = &aodv;
  if (protocol == "olsr")
    routing = &olsr;
  else if (protocol == "dsdv")
    routing = &dsdv;

  InternetStackHelper stack;
  stack.SetRoutingHelper(*routing); // has effect on the next Install ()
  stack.Install(nodes);
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.0.0.0");
  interfaces = address.Assign(devices);

  if (printRoutes) {
    Ptr<OutputStreamWrapper> routingStream =
        Create<OutputStreamWrapper>(protocol + ".routes", std::ios::out);
    routing->PrintRoutingTableAllAt(Seconds(std::min(8.0, totalTime / 2)),
                                    routingStream);
  }
}

void AodvExample::InstallApplications() {
  if (traffic == "ping") {
    V4PingHelper ping(interfaces.GetAddress(size - 1));
    ping.SetAttribute("Verbose", BooleanValue(true));

    ApplicationContainer p = ping.Install(nodes.Get(0));
    p.Start(Seconds(0));
    p.Stop(Seconds(totalTime) - Seconds(0.001));
  } else {
    UdpServerHelper server(kDataPort);
    ApplicationContainer s = server.Install(nodes.Get(0));
    s.Start(Seconds(0));
    s.Stop(Seconds(totalTime));

    Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
    meterSockets.resize(size);
    readingsSent.assign(size, 0);
    for (uint32_t i = 1; i < size; ++i) {
      meterSockets[i] =
          Socket::CreateSocket(nodes.Get(i), UdpSocketFactory::GetTypeId());
      meterSockets[i]->Connect(
          InetSocketAddress(interfaces.GetAddress(0), kDataPort));
      Simulator::Schedule(Seconds(warmup + jitter->GetValue(0, interval)),
                          &AodvExample::SendReading, this, i);
    }
  }

  Config::ConnectWithoutContext(
      "/NodeList/*/$ns3::Ipv4L3Protocol/Tx",
      MakeCallback(&AodvExample::OnIpTx, this));

  if (failNode >= 0) {
    Ptr<MobilityModel> mob = nodes.Get(failNode)->GetObject<MobilityModel>();
    Simulator::Schedule(Seconds(failAt), &MobilityModel::SetPosition, mob,
                        Vector(1e5, 1e5, 0));
  }
}
