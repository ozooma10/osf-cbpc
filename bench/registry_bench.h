#pragma once

// Hot-path / registry microbenchmark (cost regime A: the per-skeleton tax).
//
// BGSModelNode::Update fires for EVERY skeleton in the scene, every frame, on
// multiple threads. Once the player is tracked, every one of those calls pays:
//   (1) a wall-clock read   (MaybeRefresh -> steady_clock::now + duration_cast)
//   (2) a shared_mutex read lock + unordered_map find + shared_ptr copy
// for what is, 99% of the time, a negative lookup (the skeleton isn't tracked).
//
// This bench models that exact tax with self-contained data structures (no
// engine headers needed — BGSModelNode* is just an opaque key) and compares the
// CURRENT design against the proposed lock-free published-snapshot design, both
// single-threaded (clean per-lookup latency) and multi-threaded (the SRWLock
// cache-line contention that grows with crowd size x core count).

#include "bench/bench_util.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bench
{
	struct Actor
	{
		std::uint64_t touched{ 0 };  // bumped on a hit so the lookup result is "used"
		float         pad[6]{};
	};

	// ---- CURRENT design: shared_mutex + node-based map + shared_ptr values ----
	class LockedRegistry
	{
	public:
		void Track(const void* a_key)
		{
			std::unique_lock l{ mtx };
			map.emplace(a_key, std::make_shared<Actor>());
		}

		// One hook call: the throttle clock read + the locked lookup, exactly as
		// Hook_ModelNodeUpdate / MaybeRefresh do today.
		std::uint64_t HookLookup(const void* a_key)
		{
			// (1) MaybeRefresh's unconditional clock read (every skeleton).
			using namespace std::chrono;
			const volatile std::int64_t nowMs =
				duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
			(void)nowMs;

			// (2) the locked map lookup + shared_ptr copy.
			std::shared_ptr<Actor> actor;
			{
				std::shared_lock l{ mtx };
				if (auto it = map.find(a_key); it != map.end()) {
					actor = it->second;
				}
			}
			if (actor) {
				return actor->touched + 1;  // read-only "use" (multi-thread safe)
			}
			return 0;
		}

	private:
		std::shared_mutex                                        mtx;
		std::unordered_map<const void*, std::shared_ptr<Actor>>  map;
	};

	// ---- PROPOSED design: lock-free published snapshot + atomic throttle gate ----
	class SnapshotRegistry
	{
	public:
		struct Entry
		{
			const void* key;
			Actor*      actor;
		};
		struct Snapshot
		{
			std::vector<Entry> entries;
		};

		~SnapshotRegistry()
		{
			delete snap.load();
			for (const Snapshot* r : retired) {
				delete r;
			}
			for (auto* a : owned) {
				delete a;
			}
		}

		// Rebuild + atomically publish (game thread, ~2 Hz — off the hot path).
		void Track(const void* a_key)
		{
			auto* actor = new Actor();
			owned.push_back(actor);
			auto* next = new Snapshot(*Current());
			next->entries.push_back(Entry{ a_key, actor });
			const Snapshot* old = snap.exchange(next, std::memory_order_acq_rel);
			retired.push_back(old);  // bench: defer reclamation; real impl uses RCU/epoch
		}

		// One hook call: a cheap atomic throttle gate + a lock-free snapshot scan.
		std::uint64_t HookLookup(const void* a_key)
		{
			// (1) throttle gate: a single relaxed load + unlikely branch, instead
			//     of a clock read. (Set true ~2 Hz by an off-thread timer.)
			if (refreshDue.load(std::memory_order_relaxed)) [[unlikely]] {
				// would dispatch RefreshActors here
			}

			// (2) lock-free read: acquire-load the snapshot, linear scan the tiny
			//     flat array. No lock, no hash, no shared_ptr refcount churn.
			const Snapshot* s = snap.load(std::memory_order_acquire);
			for (const Entry& e : s->entries) {
				if (e.key == a_key) {
					return e.actor->touched + 1;  // read-only "use" (multi-thread safe)
				}
			}
			return 0;
		}

		void SetRefreshDue(bool v) { refreshDue.store(v, std::memory_order_relaxed); }

	private:
		const Snapshot* Current()
		{
			const Snapshot* s = snap.load(std::memory_order_acquire);
			return s ? s : (snap.store(new Snapshot(), std::memory_order_release), snap.load());
		}

		std::atomic<const Snapshot*> snap{ new Snapshot() };
		std::atomic<bool>            refreshDue{ false };
		std::vector<Actor*>          owned;
		std::vector<const Snapshot*> retired;
	};

	// Build M opaque skeleton keys, mark T of them tracked (spread across the set
	// so most lookups are the negative lookups that dominate in a real scene).
	inline std::vector<const void*> MakeKeys(std::size_t a_skeletons)
	{
		std::vector<const void*> keys;
		keys.reserve(a_skeletons);
		for (std::size_t i = 0; i < a_skeletons; ++i) {
			keys.push_back(reinterpret_cast<const void*>(0x10000ull + i * 0x40ull));
		}
		return keys;
	}

	template <class Reg>
	void TrackSpread(Reg& a_reg, const std::vector<const void*>& a_keys, std::size_t a_tracked)
	{
		const std::size_t stride = a_tracked ? (a_keys.size() / a_tracked) : a_keys.size();
		for (std::size_t i = 0, n = 0; n < a_tracked && i < a_keys.size(); i += (stride ? stride : 1), ++n) {
			a_reg.Track(a_keys[i]);
		}
	}

	// Multi-threaded sweep: `threads` workers each sweep all M keys `frames`
	// times, mirroring every skeleton calling the hook each frame across the
	// engine's render threads. Returns wall-clock ns per individual lookup.
	template <class Reg>
	double MultiThreadLookupNs(Reg& a_reg, const std::vector<const void*>& a_keys,
		std::size_t a_threads, std::size_t a_frames)
	{
		std::atomic<int>      ready{ 0 };
		std::atomic<bool>     go{ false };
		std::atomic<uint64_t> checksum{ 0 };
		std::vector<std::thread> pool;
		pool.reserve(a_threads);

		for (std::size_t t = 0; t < a_threads; ++t) {
			pool.emplace_back([&]() {
				ready.fetch_add(1, std::memory_order_acq_rel);
				while (!go.load(std::memory_order_acquire)) {
					std::this_thread::yield();
				}
				std::uint64_t local = 0;
				for (std::size_t f = 0; f < a_frames; ++f) {
					for (const void* k : a_keys) {
						local += a_reg.HookLookup(k);
					}
				}
				checksum.fetch_add(local, std::memory_order_relaxed);
			});
		}

		while (ready.load(std::memory_order_acquire) < static_cast<int>(a_threads)) {
			std::this_thread::yield();
		}
		const auto t0 = Clock::now();
		go.store(true, std::memory_order_release);
		for (auto& th : pool) {
			th.join();
		}
		const auto t1 = Clock::now();
		Sink(checksum);

		const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
		const double lookups = static_cast<double>(a_threads * a_frames * a_keys.size());
		return ns / lookups;
	}
}
