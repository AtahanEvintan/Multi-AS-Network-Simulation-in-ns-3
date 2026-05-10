/*
 * Multi-AS Network Simulation in ns-3
 * =====================================
 * Topology:  3 Autonomous Systems in a FULL-MESH inter-AS layout
 *            (AS1<->AS2, AS2<->AS3, AS1<->AS3 all permanently active)
 *
 * Each AS has:
 *   - 3 Border Routers (BR0, BR1, BR2)
 *   - N_i internal routers arranged in a ring+mesh hybrid
 *
 * Intra-AS routing : Ipv4GlobalRouting (OSPF-like Dijkstra shortest-path)
 * Inter-AS routing : Ipv4GlobalRouting across all ASes (BGP-like reachability)
 *
 * Failure scenario : One internal router inside AS2 goes DOWN at t=failureTime,
 *                    forcing OSPF-like intra-AS rerouting via surviving ring paths.
 *                    Router recovers at t = failureTime + recoveryOffset.
 *
 * Build:
 *   cp multi_as_sim.cc ~/ns-allinone-3.42/ns-3.42/scratch/
 *   cd ~/ns-allinone-3.42/ns-3.42
 *   ./ns3 run "multi_as_sim --nodes=20 --dist=balanced --runId=1 --outDir=/tmp/results"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"
#include "ns3/ipv4-global-routing-helper.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiAsSim");

// ============================================================
//  Global state for failure/recovery
// ============================================================
static double g_convergenceTime   = 0.0;
static double g_failureEventTime  = 0.0;

// ============================================================
//  Scenario config
// ============================================================
struct ScenarioConfig {
    uint32_t    totalNodes    = 20;
    std::string dist          = "balanced";
    std::string scenarioId    = "";
    uint32_t    runId         = 1;
    double      simTime       = 60.0;
    double      failureTime   = 20.0;
    double      recoveryOffset= 25.0;
    std::string outDir        = "results";
    uint32_t    seed          = 1;
    bool        noFailure     = false;
    uint32_t    n1, n2, n3;

    void Compute() {
        if (dist == "balanced") {
            n1 = (uint32_t)std::ceil(totalNodes / 3.0);
            n2 = (uint32_t)std::ceil(totalNodes / 3.0);
            n3 = totalNodes - n1 - n2;
        } else {
            n1 = (uint32_t)std::round(totalNodes * 0.20);
            n2 = (uint32_t)std::round(totalNodes * 0.35);
            n3 = totalNodes - n1 - n2;
        }
        if (n1 < 4) n1 = 4;
        if (n2 < 4) n2 = 4;
        if (n3 < 4) n3 = 4;
        if (scenarioId.empty())
            scenarioId = std::to_string(totalNodes) + "_" + dist;
    }
};

// ============================================================
//  Make a point-to-point link and assign IPs
// ============================================================
Ipv4InterfaceContainer
MakeLink(Ptr<Node> a, Ptr<Node> b,
         const std::string &bw, const std::string &delay,
         Ipv4AddressHelper &addr)
{
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue(bw));
    p2p.SetChannelAttribute("Delay",   StringValue(delay));
    NetDeviceContainer devs = p2p.Install(a, b);
    Ipv4InterfaceContainer ifc = addr.Assign(devs);
    addr.NewNetwork();
    return ifc;
}

// ============================================================
//  Build intra-AS topology: BR triangle + internal ring + spokes
// ============================================================
void BuildIntraAS(NodeContainer &nodes, Ipv4AddressHelper &addr)
{
    uint32_t n    = nodes.GetN();
    uint32_t nInt = n - 3;

    // BR triangle
    MakeLink(nodes.Get(0), nodes.Get(1), "100Mbps", "2ms", addr);
    MakeLink(nodes.Get(1), nodes.Get(2), "100Mbps", "2ms", addr);
    MakeLink(nodes.Get(2), nodes.Get(0), "100Mbps", "2ms", addr);

    if (nInt == 0) return;

    // Ring among internal nodes
    for (uint32_t i = 0; i < nInt; i++) {
        uint32_t cur  = 3 + i;
        uint32_t next = 3 + (i + 1) % nInt;
        if (cur != next)
            MakeLink(nodes.Get(cur), nodes.Get(next), "100Mbps", "2ms", addr);
    }

    // Spokes: each BR connects to one internal node
    MakeLink(nodes.Get(0), nodes.Get(3),            "100Mbps", "2ms", addr);
    MakeLink(nodes.Get(1), nodes.Get(3+1%nInt),     "100Mbps", "2ms", addr);
    MakeLink(nodes.Get(2), nodes.Get(3+2%nInt),     "100Mbps", "2ms", addr);

    // Extra cross-links for richer paths
    if (nInt >= 4) {
        for (uint32_t i = 0; i + 2 < nInt; i += 2)
            MakeLink(nodes.Get(3+i), nodes.Get(3+i+2), "100Mbps", "2ms", addr);
    }
    if (nInt >= 2)
        MakeLink(nodes.Get(n-1), nodes.Get(2), "100Mbps", "2ms", addr);
}

// ============================================================
//  Place nodes at fixed positions using MobilityModel
//  Must be called BEFORE AnimationInterface is created
// ============================================================
void PlaceNodes(NodeContainer &nodes, double cx, double cy)
{
    MobilityHelper mob;
    mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mob.Install(nodes);

    double bx[3] = {cx-35, cx+35, cx};
    double by[3] = {cy-25, cy-25, cy+25};
    for (uint32_t i = 0; i < 3 && i < nodes.GetN(); i++)
        nodes.Get(i)->GetObject<MobilityModel>()
            ->SetPosition(Vector(bx[i], by[i], 0));

    uint32_t nInt = nodes.GetN() > 3 ? nodes.GetN() - 3 : 0;
    for (uint32_t i = 0; i < nInt; i++) {
        double angle = 2.0 * M_PI * i / std::max(1u, nInt);
        double r     = 65.0 + (i % 3) * 18.0;
        nodes.Get(3+i)->GetObject<MobilityModel>()
            ->SetPosition(Vector(cx + r*std::cos(angle),
                                 cy + r*std::sin(angle), 0));
    }
}

// ============================================================
//  Failure / Recovery
// ============================================================
void TriggerNodeFailure(Ptr<Node> node)
{
    g_failureEventTime = Simulator::Now().GetSeconds();
    NS_LOG_INFO("[t=" << g_failureEventTime << "s] Node "
                << node->GetId() << " (AS2 internal) going DOWN");
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    for (uint32_t i = 1; i < ipv4->GetNInterfaces(); i++)
        ipv4->SetDown(i);

    // OSPF SPF reconvergence delay: 0.5–2.0 s
    double delay = 0.5 + (rand() % 150) / 100.0;
    Simulator::Schedule(Seconds(delay), [](){
        g_convergenceTime = Simulator::Now().GetSeconds() - g_failureEventTime;
        NS_LOG_INFO("[t=" << Simulator::Now().GetSeconds()
                    << "s] OSPF reconverged in " << g_convergenceTime << "s");
        Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
    });
}

void TriggerNodeRecovery(Ptr<Node> node)
{
    NS_LOG_INFO("[t=" << Simulator::Now().GetSeconds()
                << "s] Node " << node->GetId() << " recovering");
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    for (uint32_t i = 1; i < ipv4->GetNInterfaces(); i++)
        ipv4->SetUp(i);
    Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
}

// ============================================================
//  Main
// ============================================================
int main(int argc, char *argv[])
{
    ScenarioConfig cfg;
    CommandLine cmd(__FILE__);
    cmd.AddValue("nodes",          "Total node count",                 cfg.totalNodes);
    cmd.AddValue("dist",           "balanced | unbalanced",            cfg.dist);
    cmd.AddValue("scenarioId",     "Scenario label",                   cfg.scenarioId);
    cmd.AddValue("runId",          "Run index",                        cfg.runId);
    cmd.AddValue("simTime",        "Simulation duration (s)",          cfg.simTime);
    cmd.AddValue("failureTime",    "When AS2 internal node fails (s)", cfg.failureTime);
    cmd.AddValue("recoveryOffset", "Seconds after failure to recover", cfg.recoveryOffset);
    cmd.AddValue("outDir",         "Output directory",                 cfg.outDir);
    cmd.AddValue("seed",           "RNG seed",                        cfg.seed);
    cmd.AddValue("noFailure",      "Skip failure injection",           cfg.noFailure);
    cmd.Parse(argc, argv);

    cfg.Compute();
    RngSeedManager::SetSeed(cfg.seed);
    RngSeedManager::SetRun(cfg.runId);
    srand(cfg.seed * 1000 + cfg.runId);

    LogComponentEnable("MultiAsSim", LOG_LEVEL_INFO);
    NS_LOG_INFO("Scenario=" << cfg.scenarioId
                << " run=" << cfg.runId
                << " N=" << cfg.totalNodes
                << " (" << cfg.n1 << "/" << cfg.n2 << "/" << cfg.n3 << ")"
                << " dist=" << cfg.dist);

    // -------------------------------------------------------
    //  Create nodes
    // -------------------------------------------------------
    NodeContainer as1nodes, as2nodes, as3nodes;
    as1nodes.Create(cfg.n1);
    as2nodes.Create(cfg.n2);
    as3nodes.Create(cfg.n3);

    // -------------------------------------------------------
    //  Internet stack
    // -------------------------------------------------------
    InternetStackHelper internet;
    internet.Install(as1nodes);
    internet.Install(as2nodes);
    internet.Install(as3nodes);

    // -------------------------------------------------------
    //  Address helpers
    // -------------------------------------------------------
    Ipv4AddressHelper a1, a2, a3, ai;
    a1.SetBase("10.1.0.0", "255.255.255.252");
    a2.SetBase("10.2.0.0", "255.255.255.252");
    a3.SetBase("10.3.0.0", "255.255.255.252");
    ai.SetBase("172.16.0.0", "255.255.255.252");

    // -------------------------------------------------------
    //  Build topologies
    // -------------------------------------------------------
    BuildIntraAS(as1nodes, a1);
    BuildIntraAS(as2nodes, a2);
    BuildIntraAS(as3nodes, a3);

    // Inter-AS full mesh (primary + backup per pair)
    MakeLink(as1nodes.Get(0), as2nodes.Get(0), "1Gbps",   "5ms",  ai);
    MakeLink(as1nodes.Get(1), as2nodes.Get(1), "100Mbps", "15ms", ai);
    MakeLink(as2nodes.Get(0), as3nodes.Get(0), "1Gbps",   "5ms",  ai);
    MakeLink(as2nodes.Get(2), as3nodes.Get(2), "100Mbps", "15ms", ai);
    MakeLink(as1nodes.Get(2), as3nodes.Get(0), "1Gbps",   "5ms",  ai);
    MakeLink(as1nodes.Get(0), as3nodes.Get(2), "100Mbps", "15ms", ai);

    // -------------------------------------------------------
    //  Routing: GlobalRouting runs Dijkstra over the full graph.
    //  This handles OSPF-like intra-AS shortest-path AND
    //  BGP-like inter-AS reachability in one step.
    // -------------------------------------------------------
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // -------------------------------------------------------
    //  Position nodes BEFORE AnimationInterface
    // -------------------------------------------------------
    PlaceNodes(as1nodes, 100.0, 300.0);
    PlaceNodes(as2nodes, 300.0, 100.0);
    PlaceNodes(as3nodes, 500.0, 300.0);

    // -------------------------------------------------------
    //  Traffic: UDP OnOff flows across AS boundaries
    // -------------------------------------------------------
    uint16_t basePort = 9000;
    uint32_t numFlows = 4 + (cfg.totalNodes / 10);

    struct Dir { NodeContainer *src; NodeContainer *dst; };
    std::vector<Dir> dirs = {
        {&as1nodes,&as2nodes},{&as2nodes,&as3nodes},{&as1nodes,&as3nodes},
        {&as3nodes,&as1nodes},{&as3nodes,&as2nodes},{&as2nodes,&as1nodes}
    };

    auto pickNode = [](NodeContainer *nc, uint32_t off) -> Ptr<Node> {
        uint32_t nInt = nc->GetN() > 3 ? nc->GetN()-3 : 0;
        if (nInt == 0) return nc->Get(off % nc->GetN());
        return nc->Get(3 + off % nInt);
    };

    // Get a stable intra-AS IP for a node: use the FIRST interface address
    // that belongs to the node's own AS subnet (10.x.x.x), not an inter-AS link
    auto getIntraIP = [](Ptr<Node> node, uint8_t asNum) -> Ipv4Address {
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        uint8_t targetFirst = 10;
        for (uint32_t i = 1; i < ipv4->GetNInterfaces(); i++) {
            Ipv4Address addr = ipv4->GetAddress(i, 0).GetLocal();
            // Check it's a 10.asNum.x.x address
            uint32_t raw = addr.Get();
            uint8_t oct1 = (raw >> 24) & 0xFF;
            uint8_t oct2 = (raw >> 16) & 0xFF;
            if (oct1 == targetFirst && oct2 == asNum) {
                return addr;
            }
        }
        // Fallback: return first non-loopback address
        if (ipv4->GetNInterfaces() > 1)
            return ipv4->GetAddress(1, 0).GetLocal();
        return Ipv4Address("127.0.0.1");
    };

    for (uint32_t f = 0; f < numFlows; f++) {
        Dir &d = dirs[f % dirs.size()];
        // Use different offsets per flow to avoid duplicate src/dst pairs
        Ptr<Node> srcNode = pickNode(d.src, f * 3);
        Ptr<Node> dstNode = pickNode(d.dst, f * 3 + 1);

        // Figure out which AS number the dst belongs to
        uint8_t dstAsNum = (d.dst == &as1nodes) ? 1 : (d.dst == &as2nodes) ? 2 : 3;
        Ipv4Address dstAddr = getIntraIP(dstNode, dstAsNum);
        uint16_t port = basePort + f;

        PacketSinkHelper sink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApp = sink.Install(dstNode);
        sinkApp.Start(Seconds(1.0));
        sinkApp.Stop (Seconds(cfg.simTime - 1.0));

        OnOffHelper src("ns3::UdpSocketFactory",
                        InetSocketAddress(dstAddr, port));
        src.SetAttribute("DataRate",   StringValue("5Mbps"));
        src.SetAttribute("PacketSize", UintegerValue(1024));
        src.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        src.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        ApplicationContainer srcApp = src.Install(srcNode);
        srcApp.Start(Seconds(2.0));
        srcApp.Stop (Seconds(cfg.simTime - 1.0));
    }

    // -------------------------------------------------------
    //  Schedule failure / recovery
    // -------------------------------------------------------
    Ptr<Node> failNode = nullptr;
    if (!cfg.noFailure && as2nodes.GetN() > 3) {
        failNode = as2nodes.Get(3);
        Simulator::Schedule(Seconds(cfg.failureTime),
                            &TriggerNodeFailure, failNode);
        Simulator::Schedule(Seconds(cfg.failureTime + cfg.recoveryOffset),
                            &TriggerNodeRecovery, failNode);
    }

    // -------------------------------------------------------
    //  FlowMonitor
    // -------------------------------------------------------
    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor> flowMon = fmHelper.InstallAll();

    // -------------------------------------------------------
    //  NetAnim — after PlaceNodes, no mobility warnings
    // -------------------------------------------------------
    std::string animFile = cfg.outDir + "/anim_" + cfg.scenarioId
                         + "_run" + std::to_string(cfg.runId) + ".xml";
    AnimationInterface anim(animFile);
    anim.SetMaxPktsPerTraceFile(1000000);

    auto styleAS = [&](NodeContainer &nc, const std::string &prefix) {
        for (uint32_t i = 0; i < nc.GetN(); i++) {
            if (i < 3) anim.UpdateNodeColor(nc.Get(i), 220, 50,  50);
            else        anim.UpdateNodeColor(nc.Get(i),  50, 100, 220);
            std::string lbl = (i < 3)
                ? prefix+"-BR"+std::to_string(i)
                : prefix+"-N" +std::to_string(i-2);
            anim.UpdateNodeDescription(nc.Get(i), lbl);
        }
    };
    styleAS(as1nodes,"AS1");
    styleAS(as2nodes,"AS2");
    styleAS(as3nodes,"AS3");

    // Set node sizes — BRs larger than internal nodes
    for (uint32_t i = 0; i < as1nodes.GetN(); i++)
        anim.UpdateNodeSize(as1nodes.Get(i)->GetId(), i<3 ? 5.0 : 3.0, i<3 ? 5.0 : 3.0);
    for (uint32_t i = 0; i < as2nodes.GetN(); i++)
        anim.UpdateNodeSize(as2nodes.Get(i)->GetId(), i<3 ? 5.0 : 3.0, i<3 ? 5.0 : 3.0);
    for (uint32_t i = 0; i < as3nodes.GetN(); i++)
        anim.UpdateNodeSize(as3nodes.Get(i)->GetId(), i<3 ? 5.0 : 3.0, i<3 ? 5.0 : 3.0);

    // -------------------------------------------------------
    //  Run
    // -------------------------------------------------------
    Simulator::Stop(Seconds(cfg.simTime));
    Simulator::Run();

    // -------------------------------------------------------
    //  Collect metrics
    // -------------------------------------------------------
    flowMon->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMon->GetFlowStats();

    double sumDelay=0, sumTx=0, sumRx=0, sumLost=0;
    uint32_t nFlows=0;
    std::ostringstream perFlowRows;

    for (auto &kv : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        auto &s = kv.second;
        double delay_ms  = s.rxPackets > 0
                           ? s.delaySum.GetSeconds()/s.rxPackets*1000.0 : 0.0;
        double dur       = (s.timeLastRxPacket-s.timeFirstTxPacket).GetSeconds();
        double tput_mbps = dur > 0 ? s.rxBytes*8.0/(dur*1e6) : 0.0;
        double loss_pct  = s.txPackets > 0
                           ? 100.0*(s.txPackets-s.rxPackets)/s.txPackets : 0.0;
        sumDelay += delay_ms; sumTx += s.txPackets;
        sumRx    += s.rxPackets; sumLost += (s.txPackets-s.rxPackets);
        nFlows++;
        perFlowRows << cfg.scenarioId<<","<<cfg.runId<<","
                    <<cfg.totalNodes<<","<<cfg.dist<<","
                    <<kv.first<<","
                    <<t.sourceAddress<<","<<t.destinationAddress<<","
                    <<s.txPackets<<","<<s.rxPackets<<","
                    <<std::fixed<<std::setprecision(4)
                    <<delay_ms<<","<<tput_mbps<<","<<loss_pct<<"\n";
    }

    double meanDelay = nFlows>0 ? sumDelay/nFlows : 0;
    double meanTput  = (sumTx>0 && cfg.simTime>0)
                       ? sumRx*1024.0*8.0/(cfg.simTime*1e6*nFlows) : 0;
    double lossRate  = sumTx>0 ? 100.0*sumLost/sumTx : 0;
    double convTime  = (cfg.noFailure||!failNode) ? 0.0 : g_convergenceTime;

    NS_LOG_INFO("=== Results ===");
    NS_LOG_INFO("  Flows           : " << nFlows);
    NS_LOG_INFO("  Mean Delay      : " << meanDelay  << " ms");
    NS_LOG_INFO("  Mean Throughput : " << meanTput   << " Mbps");
    NS_LOG_INFO("  Packet Loss     : " << lossRate   << " %");
    NS_LOG_INFO("  Conv. Time      : " << convTime   << " s");

    // Per-flow CSV
    std::string flowCsvPath = cfg.outDir+"/flows_"+cfg.scenarioId
                             +"_run"+std::to_string(cfg.runId)+".csv";
    {
        std::ofstream f(flowCsvPath);
        if (f) {
            f<<"scenarioId,runId,totalNodes,dist,flowId,"
               "srcIP,dstIP,txPkts,rxPkts,delay_ms,throughput_mbps,loss_pct\n";
            f<<perFlowRows.str();
        }
    }
    NS_LOG_INFO("Per-flow CSV: " << flowCsvPath);

    // Summary CSV (append)
    std::string sumCsvPath = cfg.outDir+"/summary.csv";
    bool needHeader=false;
    { std::ifstream chk(sumCsvPath); needHeader=!chk.good(); }
    {
        std::ofstream f(sumCsvPath, std::ios::app);
        if (f) {
            if (needHeader)
                f<<"scenarioId,runId,totalNodes,dist,distribution,"
                   "delay_ms,throughput_mbps,loss_pct,convergence_s\n";
            f<<cfg.scenarioId<<","<<cfg.runId<<","
             <<cfg.totalNodes<<","<<cfg.dist<<","
             <<cfg.n1<<"/"<<cfg.n2<<"/"<<cfg.n3<<","
             <<std::fixed<<std::setprecision(4)
             <<meanDelay<<","<<meanTput<<","<<lossRate<<","<<convTime<<"\n";
        }
    }
    NS_LOG_INFO("Summary CSV: " << sumCsvPath);

    Simulator::Destroy();
    return 0;
}
