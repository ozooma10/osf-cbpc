#pragma once

// Tiny dependency-free microbenchmark harness: a steady-clock timer loop with
// warmup, batches for a latency distribution (min / median / p99), an
// optimizer-defeating sink, and a small results table + JSON emitter so runs
// can be recorded and diffed over time. No GoogleBenchmark dependency on
// purpose — this stays as portable as the solver it measures.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bench
{
	using Clock = std::chrono::steady_clock;

	// Prevent the compiler from optimizing `v` (and the writes that produced it)
	// away. Taking its address + a memory clobber is type-agnostic.
#if defined(_MSC_VER)
	template <class T>
	inline void Sink(const T& v)
	{
		volatile const char* p = reinterpret_cast<volatile const char*>(&v);
		(void)*p;
		_ReadWriteBarrier();
	}
#else
	template <class T>
	inline void Sink(const T& v)
	{
		asm volatile("" : : "g"(&v) : "memory");
	}
#endif

	struct Stats
	{
		std::string label;
		std::string unit{ "ns/op" };
		double      min_ns{ 0.0 };
		double      median_ns{ 0.0 };
		double      p99_ns{ 0.0 };
		double      mean_ns{ 0.0 };
		double      ops_per_sec{ 0.0 };
		std::size_t ops{ 0 };  // logical ops per timed call (e.g. bones/frame)
	};

	inline double Percentile(std::vector<double>& sorted, double p)
	{
		if (sorted.empty()) {
			return 0.0;
		}
		const std::size_t idx = static_cast<std::size_t>(p * (sorted.size() - 1) + 0.5);
		return sorted[idx];
	}

	// Run `body` `inner` times per batch, `batches` batches, reporting per-batch
	// nanoseconds divided by (inner * ops_per_call). `body()` should return a
	// checksum (double) that the harness sinks so nothing is dead-code eliminated.
	template <class F>
	Stats TimeOp(std::string a_label, std::size_t a_batches, std::size_t a_inner,
		std::size_t a_opsPerCall, F&& a_body)
	{
		std::vector<double> per_op;
		per_op.reserve(a_batches);
		double acc = 0.0;

		// Warmup: page in, prime branch predictors / caches, settle clocks.
		const std::size_t warm = a_batches < 8 ? a_batches : 8;
		for (std::size_t w = 0; w < warm; ++w) {
			for (std::size_t i = 0; i < a_inner; ++i) {
				acc += a_body();
			}
		}

		for (std::size_t b = 0; b < a_batches; ++b) {
			const auto t0 = Clock::now();
			double c = 0.0;
			for (std::size_t i = 0; i < a_inner; ++i) {
				c += a_body();
			}
			const auto t1 = Clock::now();
			acc += c;
			const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
			per_op.push_back(ns / static_cast<double>(a_inner * a_opsPerCall));
		}
		Sink(acc);

		std::vector<double> sorted = per_op;
		std::sort(sorted.begin(), sorted.end());
		double sum = 0.0;
		for (double v : per_op) {
			sum += v;
		}

		Stats s;
		s.label = std::move(a_label);
		s.min_ns = sorted.front();
		s.median_ns = Percentile(sorted, 0.50);
		s.p99_ns = Percentile(sorted, 0.99);
		s.mean_ns = sum / static_cast<double>(per_op.size());
		s.ops_per_sec = s.median_ns > 0.0 ? 1e9 / s.median_ns : 0.0;
		s.ops = a_opsPerCall;
		return s;
	}

	inline void PrintHeader()
	{
		std::printf("%-34s %12s %12s %12s %14s\n",
			"benchmark", "min", "median", "p99", "throughput");
		std::printf("%-34s %12s %12s %12s %14s\n",
			"---------", "---", "------", "---", "----------");
	}

	inline void PrintRow(const Stats& s)
	{
		std::printf("%-34s %9.2f ns %9.2f ns %9.2f ns %10.2f M/s\n",
			s.label.c_str(), s.min_ns, s.median_ns, s.p99_ns, s.ops_per_sec / 1e6);
	}

	inline void PrintJson(const std::vector<Stats>& rows, const char* a_label)
	{
		std::printf("{\n  \"label\": \"%s\",\n  \"results\": [\n", a_label ? a_label : "");
		for (std::size_t i = 0; i < rows.size(); ++i) {
			const Stats& s = rows[i];
			std::printf("    { \"name\": \"%s\", \"unit\": \"%s\", \"min_ns\": %.4f, \"median_ns\": %.4f, "
						"\"p99_ns\": %.4f, \"mean_ns\": %.4f, \"ops_per_sec\": %.1f }%s\n",
				s.label.c_str(), s.unit.c_str(), s.min_ns, s.median_ns, s.p99_ns, s.mean_ns, s.ops_per_sec,
				i + 1 < rows.size() ? "," : "");
		}
		std::printf("  ]\n}\n");
	}
}
