// OSF CBPC — performance harness.
//
// Two benchmarks matching the two cost regimes in docs/PERF.md:
//   * solver   — real JiggleSolver::StepBone over the real CLSF Ni* operators
//                (regime B: per-tracked-bone work). Sensitive to LTO / AVX2 /
//                fp-contract / the SIMD rewrite.
//   * registry — the per-skeleton hook tax (regime A): locked unordered_map vs
//                lock-free published snapshot, single- and multi-threaded.
//
// Run before a change, run after, diff the numbers. `--json` emits a record you
// can save under bench/results/ and compare over time.

#include "bench/registry_bench.h"
#include "bench/solver_bench.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
	struct Args
	{
		std::size_t bones{ 5 };          // player profile
		std::size_t crowdBones{ 400 };   // ~80 actors x 5 bones (M2)
		bool        scatter{ true };
		std::size_t batches{ 200 };
		std::size_t frames{ 2000 };      // solver inner frames per batch
		std::size_t skeletons{ 128 };    // registry M
		std::size_t tracked{ 1 };        // registry T (player only by default)
		std::size_t threads{ 0 };        // 0 => hardware_concurrency
		std::size_t regFrames{ 3000 };   // registry sweeps per thread
		double      fps{ 60.0 };
		bool        json{ false };
		std::string label{ "run" };
	};

	std::size_t ParseSize(const char* v, std::size_t def)
	{
		if (!v) {
			return def;
		}
		char* end = nullptr;
		const unsigned long long r = std::strtoull(v, &end, 10);
		return (end && *end == '\0') ? static_cast<std::size_t>(r) : def;
	}

	Args ParseArgs(int argc, char** argv)
	{
		Args a;
		for (int i = 1; i < argc; ++i) {
			const std::string k = argv[i];
			const char* next = (i + 1 < argc) ? argv[i + 1] : nullptr;
			auto take = [&](std::size_t def) { ++i; return ParseSize(next, def); };
			if (k == "--bones") {
				a.bones = take(a.bones);
			} else if (k == "--crowd-bones") {
				a.crowdBones = take(a.crowdBones);
			} else if (k == "--batches") {
				a.batches = take(a.batches);
			} else if (k == "--frames") {
				a.frames = take(a.frames);
			} else if (k == "--skeletons") {
				a.skeletons = take(a.skeletons);
			} else if (k == "--tracked") {
				a.tracked = take(a.tracked);
			} else if (k == "--threads") {
				a.threads = take(a.threads);
			} else if (k == "--reg-frames") {
				a.regFrames = take(a.regFrames);
			} else if (k == "--fps") {
				a.fps = next ? std::strtod(next, nullptr) : a.fps, ++i;
			} else if (k == "--no-scatter") {
				a.scatter = false;
			} else if (k == "--json") {
				a.json = true;
			} else if (k == "--label") {
				a.label = next ? next : a.label, ++i;
			} else if (k == "--help" || k == "-h") {
				std::printf(
					"usage: bench [--bones N] [--crowd-bones N] [--no-scatter]\n"
					"             [--batches N] [--frames N]\n"
					"             [--skeletons N] [--tracked N] [--threads N] [--reg-frames N]\n"
					"             [--fps F] [--json] [--label STR]\n");
				std::exit(0);
			}
		}
		if (a.threads == 0) {
			a.threads = std::thread::hardware_concurrency();
			if (a.threads == 0) {
				a.threads = 4;
			}
		}
		return a;
	}
}

int main(int argc, char** argv)
{
	using namespace bench;
	const Args args = ParseArgs(argc, argv);

	std::vector<Stats> rows;

	// ---------------------------------------------------------------- solver --
	rows.push_back(RunSolverBench(
		args.scatter ? "solver/player (5 bones, scatter)" : "solver/player (5 bones, seq)",
		args.bones, args.scatter, args.batches, args.frames));
	rows.push_back(RunSolverBench(
		"solver/crowd (M2-scale)", args.crowdBones, args.scatter,
		args.batches, args.frames / 4 + 1));

	// -------------------------------------------------------------- registry --
	const std::vector<const void*> keys = MakeKeys(args.skeletons);

	// Best-of-N: thread scheduling on shared/virtualized hosts is noisy, and the
	// fastest run is the one least perturbed by the OS — the cleanest signal for
	// tracking a relative before/after delta. Each run uses a fresh registry.
	auto bestLookupNs = [&](bool a_snapshot, std::size_t a_threads) {
		double best = 1e300;
		for (int rep = 0; rep < 3; ++rep) {
			if (a_snapshot) {
				SnapshotRegistry reg;
				TrackSpread(reg, keys, args.tracked);
				best = std::min(best, MultiThreadLookupNs(reg, keys, a_threads, args.regFrames));
			} else {
				LockedRegistry reg;
				TrackSpread(reg, keys, args.tracked);
				best = std::min(best, MultiThreadLookupNs(reg, keys, a_threads, args.regFrames));
			}
		}
		return best;
	};

	const double curr1 = bestLookupNs(false, 1);            // current, clean
	const double snap1 = bestLookupNs(true, 1);             // snapshot, clean
	const double currN = bestLookupNs(false, args.threads); // current, contended
	const double snapN = bestLookupNs(true, args.threads);  // snapshot, contended

	auto mkReg = [](const char* name, double ns) {
		Stats s;
		s.label = name;
		s.unit = "ns/lookup";
		s.min_ns = s.median_ns = s.p99_ns = s.mean_ns = ns;
		s.ops_per_sec = ns > 0.0 ? 1e9 / ns : 0.0;
		s.ops = 1;
		return s;
	};
	rows.push_back(mkReg("registry/current   1-thread", curr1));
	rows.push_back(mkReg("registry/snapshot  1-thread", snap1));
	rows.push_back(mkReg("registry/current   N-thread", currN));
	rows.push_back(mkReg("registry/snapshot  N-thread", snapN));

	if (args.json) {
		PrintJson(rows, args.label.c_str());
		return 0;
	}

	// ---------------------------------------------------------------- report --
	std::printf("OSF CBPC — perf harness   [label: %s]\n", args.label.c_str());
	std::printf("threads=%zu  skeletons=%zu  tracked=%zu  fps=%.0f\n\n",
		args.threads, args.skeletons, args.tracked, args.fps);

	PrintHeader();
	for (const Stats& s : rows) {
		PrintRow(s);
	}

	// Headline: per-frame hook tax for a crowd of `skeletons` at `fps`.
	// One frame = `skeletons` hook calls; cost ~= per-lookup latency x skeletons.
	const double budgetUs = 1e6 / args.fps;
	const double currFrameUs = currN * args.skeletons / 1e3;
	const double snapFrameUs = snapN * args.skeletons / 1e3;
	std::printf("\nregime-A hook tax  (%zu skeletons/frame, %.0f fps, %zu threads):\n",
		args.skeletons, args.fps, args.threads);
	std::printf("  current  : %8.2f us/frame  (%.2f%% of the %.2f ms frame budget)\n",
		currFrameUs, 100.0 * currFrameUs / budgetUs, budgetUs / 1e3);
	std::printf("  snapshot : %8.2f us/frame  (%.2f%% of budget)   ->  %.2fx cheaper\n",
		snapFrameUs, 100.0 * snapFrameUs / budgetUs,
		snapFrameUs > 0.0 ? currFrameUs / snapFrameUs : 0.0);
	std::printf("  contention scaling 1->%zu threads:  current %.2fx   snapshot %.2fx"
				"   (1.00x = perfectly flat)\n",
		args.threads, curr1 > 0.0 ? currN / curr1 : 0.0, snap1 > 0.0 ? snapN / snap1 : 0.0);

	return 0;
}
