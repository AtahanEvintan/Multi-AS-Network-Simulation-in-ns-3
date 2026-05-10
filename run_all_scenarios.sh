#!/usr/bin/env bash
# ============================================================
#  run_all_scenarios.sh
#  Runs all 6 scenarios x 3 runs = 18 simulations
#
#  Usage:
#    bash run_all_scenarios.sh [--ns3 /path/to/ns-3.42] [--parallel N]
#
#  Defaults:
#    NS3_HOME = ~/ns-allinone-3.42/ns-3.42
#    Parallel = 1 (sequential)
# ============================================================

set -e

NS3_HOME="${NS3_HOME:-$HOME/ns-allinone-3.42/ns-3.42}"
PARALLEL=1
RUNS=3
BASE_SEED=1
RESULTS_DIR="$(pwd)/results/$(date +%Y%m%d_%H%M%S)"

# Parse flags
while [[ $# -gt 0 ]]; do
    case $1 in
        --ns3)      NS3_HOME="$2"; shift 2;;
        --parallel) PARALLEL="$2"; shift 2;;
        --runs)     RUNS="$2";     shift 2;;
        *) echo "Unknown flag: $1"; exit 1;;
    esac
done

echo "=============================================="
echo "  Multi-AS Simulation Runner"
echo "  NS3_HOME   : $NS3_HOME"
echo "  Results dir: $RESULTS_DIR"
echo "  Scenarios  : 6 x ${RUNS} runs = $((6*RUNS)) total"
echo "=============================================="

# ---- 1. Copy simulation file ----
SCRATCH="$NS3_HOME/scratch/multi_as_sim.cc"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cp "$SCRIPT_DIR/multi_as_sim.cc" "$SCRATCH"
echo "[1/3] Copied multi_as_sim.cc -> $SCRATCH"

# ---- 2. Build ----
echo "[2/3] Building..."
cd "$NS3_HOME"
./ns3 build 2>&1 | tail -5
echo "Build complete."

# ---- 3. Run scenarios ----
mkdir -p "$RESULTS_DIR"
echo "[3/3] Running simulations..."

declare -a NODES_LIST=(20 50 100)
declare -a DIST_LIST=(balanced unbalanced)

run_one() {
    local nodes=$1
    local dist=$2
    local run=$3
    local seed=$((BASE_SEED + run))
    local scenario="${nodes}_${dist}"
    local tag="${scenario}_run${run}"

    echo "  -> $tag (seed=$seed)"
    cd "$NS3_HOME"
    ./ns3 run "multi_as_sim \
        --nodes=$nodes \
        --dist=$dist \
        --scenarioId=$scenario \
        --runId=$run \
        --seed=$seed \
        --simTime=60 \
        --failureTime=20 \
        --recoveryOffset=25 \
        --outDir=$RESULTS_DIR" \
        2>&1 | grep -E "Results|Delay|Throughput|Loss|Conv|ERROR|error" || true
}

export -f run_one
export NS3_HOME RESULTS_DIR BASE_SEED

if [[ $PARALLEL -gt 1 ]]; then
    echo "  Running with $PARALLEL parallel jobs..."
    # Generate job list
    JOBS=()
    for nodes in "${NODES_LIST[@]}"; do
        for dist in "${DIST_LIST[@]}"; do
            for run in $(seq 1 $RUNS); do
                JOBS+=("$nodes $dist $run")
            done
        done
    done
    printf '%s\n' "${JOBS[@]}" | xargs -P "$PARALLEL" -I{} bash -c 'run_one $@' _ {}
else
    for nodes in "${NODES_LIST[@]}"; do
        for dist in "${DIST_LIST[@]}"; do
            for run in $(seq 1 $RUNS); do
                run_one "$nodes" "$dist" "$run"
            done
        done
    done
fi

echo ""
echo "=============================================="
echo "  All runs complete!"
echo "  Results in: $RESULTS_DIR"
echo "  summary.csv: $RESULTS_DIR/summary.csv"
echo "=============================================="
