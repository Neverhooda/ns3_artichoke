#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

using namespace ns3;

static const uint16_t kDataPort = 9000;

static bool EnablePatchedAttribute(const std::string &name) {
  TypeId tid = TypeId::LookupByName("ns3::aodv::RoutingProtocol");
  TypeId::AttributeInformation info;
  if (!tid.LookupAttributeByName(name, &info)) {
    std::cerr << "AODV attribute " << name
              << " is missing: use the artichoke-ns3:3.26-fast-repair image "
                 "(patches/01-aodv-fast-repair.patch)\n";
    return false;
  }
  Config::SetDefault("ns3::aodv::RoutingProtocol::" + name,
                     BooleanValue(true));
  return true;
}

class PlcRecovery {
public:
  PlcRecovery();
  bool Configure(int argc, char **argv);
  void Run();
  void Report(std::ostream &os);

private:
  uint32_t size;
  double step;
  double interval;
  uint32_t packetSize;
  double warmup;
  double totalTime;
  uint32_t failNode;
  double failAt;
  bool hello;
  bool fastRepair;
  uint32_t run;
  std::string label;
  std::string csv;
  std::string meterCsv;
  std::string timelineCsv;
  std::string traceCsv;
  std::ofstream trace;

  NodeContainer nodes;
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  std::vector<Ptr<Socket> > meterSockets;
  std::vector<uint32_t> nextSeq;
  std::map<Ipv4Address, uint32_t> meterByAddress;
  std::vector<std::vector<double> > arrivals;
  uint64_t ctrlPackets;

  void Build();
  void SendReading(uint32_t meter);
  void Receive(Ptr<Socket> socket);
  void OnIpTx(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t iface);
};

PlcRecovery::PlcRecovery()
    : size(24), step(100), interval(1.0), packetSize(64), warmup(15),
      totalTime(90), failNode(3), failAt(30), hello(true), fastRepair(false),
      run(1), label(""), csv(""), meterCsv(""), timelineCsv(""), traceCsv(""),
      ctrlPackets(0) {}

bool PlcRecovery::Configure(int argc, char **argv) {
  CommandLine cmd;
  cmd.AddValue("size", "Nodes on the ring feeder, concentrator included.",
               size);
  cmd.AddValue("step", "Cable distance between neighbouring meters, m.",
               step);
  cmd.AddValue("interval", "Meter reporting interval, s.", interval);
  cmd.AddValue("packetSize", "Meter reading size, bytes.", packetSize);
  cmd.AddValue("warmup", "Time before meters start reporting, s.", warmup);
  cmd.AddValue("time", "Simulation time, s.", totalTime);
  cmd.AddValue("failNode", "Meter that drops off the feeder.", failNode);
  cmd.AddValue("failAt", "Failure time, s.", failAt);
  cmd.AddValue("hello", "Enable AODV HELLO messages.", hello);
  cmd.AddValue("fastRepair",
               "Rediscover routes as soon as a neighbour is lost "
               "(needs patches/01-aodv-fast-repair.patch).",
               fastRepair);
  cmd.AddValue("run", "RNG run number.", run);
  cmd.AddValue("label", "Configuration name written to the CSV files.",
               label);
  cmd.AddValue("csv", "Append a summary row to this CSV file.", csv);
  cmd.AddValue("meterCsv", "Write per-meter recovery times to this CSV.",
               meterCsv);
  cmd.AddValue("timelineCsv", "Write meters-heard-per-second to this CSV.",
               timelineCsv);
  cmd.AddValue("traceCsv", "Log every AODV control packet except HELLO.",
               traceCsv);
  cmd.Parse(argc, argv);

  SeedManager::SetSeed(12345);
  SeedManager::SetRun(run);

  Config::SetDefault("ns3::aodv::RoutingProtocol::EnableHello",
                     BooleanValue(hello));
  if (fastRepair && !EnablePatchedAttribute("FastRepair"))
    return false;
  if (label.empty())
    label = fastRepair ? "fast-repair" : "baseline";

  return size >= 4 && failNode > 0 && failNode < size && failAt > warmup &&
         failAt < totalTime;
}

void PlcRecovery::Build() {
  nodes.Create(size);
  double radius = step / (2 * std::sin(M_PI / size));
  Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
  for (uint32_t i = 0; i < size; ++i) {
    double a = 2 * M_PI * i / size;
    positions->Add(Vector(radius * std::cos(a), radius * std::sin(a), 0));
  }
  MobilityHelper mobility;
  mobility.SetPositionAllocator(positions);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);

  WifiHelper wifi;
  wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                               StringValue("DsssRate1Mbps"), "ControlMode",
                               StringValue("DsssRate1Mbps"));
  WifiMacHelper mac;
  mac.SetType("ns3::AdhocWifiMac");
  YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
  YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
  phy.SetChannel(channel.Create());
  devices = wifi.Install(phy, mac, nodes);

  AodvHelper aodv;
  InternetStackHelper stack;
  stack.SetRoutingHelper(aodv);
  stack.Install(nodes);
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.0.0.0");
  interfaces = address.Assign(devices);

  Ptr<Socket> sink =
      Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
  sink->Bind(InetSocketAddress(Ipv4Address::GetAny(), kDataPort));
  sink->SetRecvCallback(MakeCallback(&PlcRecovery::Receive, this));

  arrivals.resize(size);
  meterSockets.resize(size);
  nextSeq.assign(size, 0);
  Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
  for (uint32_t i = 1; i < size; ++i) {
    meterByAddress[interfaces.GetAddress(i)] = i;
    meterSockets[i] =
        Socket::CreateSocket(nodes.Get(i), UdpSocketFactory::GetTypeId());
    meterSockets[i]->Connect(
        InetSocketAddress(interfaces.GetAddress(0), kDataPort));
    Simulator::Schedule(Seconds(warmup + jitter->GetValue(0, interval)),
                        &PlcRecovery::SendReading, this, i);
  }

  Ptr<MobilityModel> failing = nodes.Get(failNode)->GetObject<MobilityModel>();
  Simulator::Schedule(Seconds(failAt), &MobilityModel::SetPosition, failing,
                      Vector(1e5, 1e5, 0));

  Config::ConnectWithoutContext("/NodeList/*/$ns3::Ipv4L3Protocol/Tx",
                                MakeCallback(&PlcRecovery::OnIpTx, this));
}

void PlcRecovery::SendReading(uint32_t meter) {
  SeqTsHeader header;
  header.SetSeq(nextSeq[meter]++);
  Ptr<Packet> p = Create<Packet>(packetSize - header.GetSerializedSize());
  p->AddHeader(header);
  meterSockets[meter]->Send(p);
  if (Simulator::Now() + Seconds(interval) < Seconds(totalTime - 1))
    Simulator::Schedule(Seconds(interval), &PlcRecovery::SendReading, this,
                        meter);
}

void PlcRecovery::Receive(Ptr<Socket> socket) {
  Address from;
  Ptr<Packet> p;
  while ((p = socket->RecvFrom(from))) {
    Ipv4Address src = InetSocketAddress::ConvertFrom(from).GetIpv4();
    std::map<Ipv4Address, uint32_t>::const_iterator it =
        meterByAddress.find(src);
    if (it != meterByAddress.end())
      arrivals[it->second].push_back(Simulator::Now().GetSeconds());
  }
}

void PlcRecovery::OnIpTx(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t) {
  Ptr<Packet> copy = packet->Copy();
  Ipv4Header ip;
  copy->RemoveHeader(ip);
  if (ip.GetProtocol() != 17)
    return;
  UdpHeader udp;
  copy->PeekHeader(udp);
  if (udp.GetDestinationPort() != 654)
    return;
  ++ctrlPackets;
  if (!trace.is_open())
    return;
  copy->RemoveHeader(udp);
  aodv::TypeHeader type;
  copy->RemoveHeader(type);
  std::string kind = "RREP_ACK";
  Ipv4Address target, origin;
  uint32_t hops = 0;
  if (type.Get() == aodv::AODVTYPE_RREQ) {
    aodv::RreqHeader h;
    copy->RemoveHeader(h);
    kind = "RREQ";
    target = h.GetDst();
    origin = h.GetOrigin();
    hops = h.GetHopCount();
  } else if (type.Get() == aodv::AODVTYPE_RREP) {
    aodv::RrepHeader h;
    copy->RemoveHeader(h);
    kind = h.GetDst() == h.GetOrigin() ? "HELLO" : "RREP";
    target = h.GetDst();
    origin = h.GetOrigin();
    hops = h.GetHopCount();
  } else if (type.Get() == aodv::AODVTYPE_RERR) {
    kind = "RERR";
  }
  if (kind == "HELLO")
    return;
  trace << Simulator::Now().GetSeconds() << ","
        << ipv4->GetObject<Node>()->GetId() << "," << kind << ","
        << (uint32_t)ip.GetTtl() << "," << ip.GetDestination() << ","
        << target << "," << origin << "," << hops << "\n";
}

void PlcRecovery::Run() {
  if (!traceCsv.empty()) {
    trace.open(traceCsv.c_str());
    trace << "time,node,type,ip_ttl,ip_dst,target,origin,hops\n";
  }
  Build();
  Simulator::Stop(Seconds(totalTime));
  Simulator::Run();
}

void PlcRecovery::Report(std::ostream &os) {
  double threshold = 1.5 * interval + 0.2;
  std::vector<double> recovery;
  std::vector<int32_t> neverRecovered;
  std::ofstream meters;
  if (!meterCsv.empty()) {
    meters.open(meterCsv.c_str());
    meters << "label,run,interval,meter,hops_before,sent,received,worst_gap_s,"
              "affected,recovery_s\n";
  }

  for (uint32_t i = 1; i < size; ++i) {
    if (i == failNode)
      continue;
    const std::vector<double> &a = arrivals[i];
    double last = failAt;
    double worstGap = 0, worstEnd = -1;
    for (size_t k = 0; k < a.size(); ++k) {
      if (a[k] < failAt)
        continue;
      if (a[k] - last > worstGap) {
        worstGap = a[k] - last;
        worstEnd = a[k];
      }
      last = a[k];
    }
    bool silent = last <= failAt || totalTime - 1 - last > threshold;
    bool affected = silent || worstGap > threshold;
    double rec = -1;
    if (silent)
      neverRecovered.push_back(i);
    else if (affected) {
      rec = worstEnd - failAt;
      recovery.push_back(rec);
    }
    if (meters.is_open())
      meters << label << "," << run << "," << interval << "," << i << ","
             << std::min(i, size - i) << "," << nextSeq[i] << "," << a.size()
             << "," << worstGap << "," << affected << "," << rec << "\n";
  }

  std::sort(recovery.begin(), recovery.end());
  double median = recovery.empty() ? 0 : recovery[recovery.size() / 2];
  double worst = recovery.empty() ? 0 : recovery.back();

  os << "config=" << label << " ring=" << size << " step=" << step
     << " run=" << run << " failNode=" << failNode << "\n"
     << "meters cut off:           " << recovery.size() + neverRecovered.size()
     << "\n"
     << "never recovered:          " << neverRecovered.size() << "\n"
     << "recovery median / max:    " << median << " / " << worst << " s\n"
     << "AODV control packets:     " << ctrlPackets << "\n";

  if (!csv.empty()) {
    std::ifstream probe(csv.c_str());
    bool needHeader = !probe.good() || probe.peek() == EOF;
    probe.close();
    std::ofstream out(csv.c_str(), std::ios::app);
    if (needHeader)
      out << "label,run,size,step,interval,affected,never,median_s,max_s,"
             "ctrlPackets\n";
    out << label << "," << run << "," << size << "," << step << ","
        << interval << "," << recovery.size() + neverRecovered.size() << ","
        << neverRecovered.size() << "," << median << "," << worst << ","
        << ctrlPackets << "\n";
  }

  if (!timelineCsv.empty()) {
    std::ofstream out(timelineCsv.c_str());
    out << "label,run,second,meters_heard,meters_alive\n";
    for (uint32_t s = (uint32_t)warmup + 1; s + 1 < totalTime; ++s) {
      uint32_t heard = 0;
      for (uint32_t i = 1; i < size; ++i) {
        for (size_t k = 0; k < arrivals[i].size(); ++k) {
          if (arrivals[i][k] >= s && arrivals[i][k] < s + 1) {
            ++heard;
            break;
          }
        }
      }
      uint32_t alive = s >= failAt ? size - 2 : size - 1;
      out << label << "," << run << "," << s << "," << heard << "," << alive
          << "\n";
    }
  }
}

int main(int argc, char **argv) {
  PlcRecovery sim;
  if (!sim.Configure(argc, argv))
    NS_FATAL_ERROR("Configuration failed. Aborted.");
  sim.Run();
  sim.Report(std::cout);
  Simulator::Destroy();
  return 0;
}
