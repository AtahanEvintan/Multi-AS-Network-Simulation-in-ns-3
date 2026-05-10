# Multi-AS Network Simulation in ns-3

**Student:** Atahan Evintan  
**Student No:** 820230334  
**Course:** Computer Networks  
**Date:** May 2026

---

## 📖 Overview

This project implements a parametric **Multi-Autonomous System (Multi-AS) network simulation** using the **ns-3 network simulator (v3.42)**. The simulation models three interconnected Autonomous Systems in a **full-mesh topology**, evaluates network performance across 6 different scenarios, and includes a **node failure and recovery** scenario to test OSPF-like intra-AS reconvergence.

---

## 🗺️ Topology Design

### Inter-AS: Full Mesh
Unlike a typical chain topology (AS1→AS2→AS3), all three AS pairs are **directly connected**:

```
        AS2
       /    \
     AS1 -- AS3
```

Each pair has:
- **Primary link:** 1 Gbps / 5 ms
- **Backup link:** 100 Mbps / 15 ms

### Intra-AS: Ring + Mesh Hybrid
Each AS contains:
- **3 Border Routers (BR0, BR1, BR2)** arranged in a triangle
- **Internal routers** connected in a ring with cross-links and BR spokes
- Multiple redundant paths between every BR pair

### Inter-AS Link Table

| Endpoints | Bandwidth | Delay | Role |
|-----------|-----------|-------|------|
| AS1-BR0 ↔ AS2-BR0 | 1 Gbps | 5 ms | AS1–AS2 Primary |
| AS1-BR1 ↔ AS2-BR1 | 100 Mbps | 15 ms | AS1–AS2 Backup |
| AS2-BR0 ↔ AS3-BR0 | 1 Gbps | 5 ms | AS2–AS3 Primary |
| AS2-BR2 ↔ AS3-BR2 | 100 Mbps | 15 ms | AS2–AS3 Backup |
| AS1-BR2 ↔ AS3-BR0 | 1 Gbps | 5 ms | AS1–AS3 Primary (Full Mesh) |
| AS1-BR0 ↔ AS3-BR2 | 100 Mbps | 15 ms | AS1–AS3 Backup (Full Mesh) |

### IP Addressing

| Block | Assignment |
|-------|-----------|
| 10.1.0.0/16 | AS1 intra-AS links (/30 subnets) |
| 10.2.0.0/16 | AS2 intra-AS links (/30 subnets) |
| 10.3.0.0/16 | AS3 intra-AS links (/30 subnets) |
| 172.16.0.0/24 | Inter-AS links (sequential /30 subnets) |

---

## 🔀 Routing Structure

### Intra-AS: OSPF-like Shortest-Path
- Uses `Ipv4GlobalRouting` (Dijkstra-based SPF)
- Installed via `Ipv4GlobalRoutingHelper::PopulateRoutingTables()`
- Analogous to OSPF domain-wide LSA synchronisation and SPF computation
- Ring+mesh topology guarantees multiple vertex-disjoint paths between every BR pair

### Inter-AS: BGP-like Reachability
- Same `Ipv4GlobalRouting` engine extended across all three ASes
- Dijkstra discovers shortest paths across AS boundaries automatically
- Full-mesh topology provides direct AS1↔AS3 path without mandatory AS2 transit
- Analogous to BGP providing inter-domain reachability

### Failure Scenario: Internal Node Failure
- **t = 20s:** AS2 internal router (node index 3) goes DOWN via `Ipv4::SetDown()`
- **t = 20s + Uniform[0.5, 2.0]s:** OSPF reconvergence delay, then `RecomputeRoutingTables()`
- **t = 45s:** Node recovers via `Ipv4::SetUp()`, routing tables recomputed
- Ring topology provides bypass paths that activate within the convergence window

---

## 📊 Experimental Scenarios

6 scenarios × 3 RNG seeds = **18 total simulation runs**

| ID | Nodes | Distribution | AS1 | AS2 | AS3 | Flows |
|----|-------|-------------|-----|-----|-----|-------|
| S1 | 20 | Balanced | 7 | 7 | 6 | 6 |
| S2 | 20 | Unbalanced | 4 | 7 | 9 | 6 |
| S3 | 50 | Balanced | 17 | 17 | 16 | 9 |
| S4 | 50 | Unbalanced | 10 | 18 | 22 | 9 |
| S5 | 100 | Balanced | 34 | 34 | 32 | 14 |
| S6 | 100 | Unbalanced | 20 | 35 | 45 | 14 |

**Distribution strategies:**
- **Balanced:** ~equal split (ceil(N/3) per AS)
- **Unbalanced:** 20% / 35% / 45% split across AS1 / AS2 / AS3

**3 runs per scenario** with seeds 2, 3, 4 for statistical reliability and variance analysis.

---

## 📈 Results Summary

| Nodes | Distribution | Delay (ms) | Tput (Mbps) | Loss (%) | Conv. (s) |
|-------|-------------|-----------|------------|---------|----------|
| 20 | Balanced | 13.87 | 4.06 | 0.31 | 1.14 |
| 20 | Unbalanced | 18.17 | 4.06 | 0.33 | 1.14 |
| 50 | Balanced | 24.63 | 4.75 | 0.00 | 1.14 |
| 50 | Unbalanced | 23.75 | 4.52 | 0.23 | 1.14 |
| 100 | Balanced | 30.37 | 4.60 | 3.13 | 1.14 |
| 100 | Unbalanced | 30.75 | 4.43 | 0.14 | 1.14 |

**Key findings:**
- Delay grows sub-linearly with node count; inter-AS fixed cost (5ms/hop) dominates at small N
- Throughput stays near 5 Mbps target — inter-AS links never saturated
- Packet loss is near zero except during the failure window at t=20s
- Convergence time is flat at ~1.14s — governed by protocol timer, not topology size
- Unbalanced distribution increases delay at small N but converges at large N

---

## 📁 Repository Structure

```
.
├── multi_as_sim.cc          ← ns-3 C++ simulation (single file, all scenarios)
├── aggregate_results.py     ← Python analysis script (plots + summary table)
├── run_all_scenarios.sh     ← Shell script to run all 18 scenarios
├── README.md                ← This file
├── results/
│   ├── summary.csv          ← Aggregated metrics (all 18 runs)
│   ├── flows_*.csv          ← Per-flow metrics for every run
│   └── analysis/
│       ├── delay.png
│       ├── throughput.png
│       ├── packet_loss.png
│       └── convergence.png
└── netanim/
    └── anim_*.xml           ← NetAnim animation files (all 18 runs)
```

---

## 🚀 Reproducing the Simulation

### Prerequisites
- macOS 13+ on Apple Silicon (arm64) or Linux
- ns-3.42 (`ns-allinone-3.42`)
- NetAnim 3.109 built with Qt6
- Python 3.x with pandas, matplotlib, numpy

### Installation
```bash
# Download ns-3.42
curl -LO https://www.nsnam.org/releases/ns-allinone-3.42.tar.bz2
tar xjf ns-allinone-3.42.tar.bz2
cd ns-allinone-3.42/ns-3.42
./ns3 configure --enable-examples
./ns3 build

# Build NetAnim
cd ../netanim-3.109
qmake NetAnim.pro
make -j4
```

### Running Simulations
```bash
cd ~/ns-allinone-3.42/ns-3.42
cp multi_as_sim.cc scratch/

# Single scenario test
./ns3 run "multi_as_sim --nodes=20 --dist=balanced --runId=1 --seed=2 --outDir=/tmp/results"

# All 18 scenarios
for nodes in 20 50 100; do
  for dist in balanced unbalanced; do
    for run in 1 2 3; do
      seed=$((run + 1))
      ./ns3 run "multi_as_sim --nodes=$nodes --dist=$dist \
        --scenarioId=${nodes}_${dist} --runId=$run \
        --seed=$seed --outDir=~/multi_as_results"
    done
  done
done
```

### Generating Analysis Plots
```bash
pip3 install pandas matplotlib numpy
python3 aggregate_results.py --resultsDir ~/multi_as_results
```

### Visualising with NetAnim
```bash
~/ns-allinone-3.42/netanim-3.109/NetAnim &
# File → Open XML → select any anim_*.xml
# Red nodes = Border Routers | Blue nodes = Internal routers
# t=20s: AS2 internal node fails
# t=45s: Node recovers
```

### Command-Line Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--nodes` | 20 | Total node count (20/50/100) |
| `--dist` | balanced | balanced or unbalanced |
| `--runId` | 1 | Run index (used in filenames) |
| `--seed` | 1 | RNG seed |
| `--simTime` | 60.0 | Simulation duration (s) |
| `--failureTime` | 20.0 | When AS2 internal node fails |
| `--recoveryOffset` | 25.0 | Seconds after failure to recover |
| `--outDir` | results | Output directory |
| `--noFailure` | false | Skip failure injection |

---

## 🛠️ Environment

| Component | Version |
|-----------|---------|
| ns-3 | 3.42 |
| NetAnim | 3.109 |
| Qt | 6 (Homebrew) |
| macOS | 14.5 (Apple Silicon M1) |
| Python | 3.12 |
| Compiler | AppleClang 15.0 |
