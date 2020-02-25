// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "lib/catch.hpp"
#include "medida/histogram.h"
#include "medida/stats/exp_decay_sample.h"
#include "medida/stats/snapshot.h"
#include <random>

// These tests just check that medida's math is roughly sensible.

using uniform_dbl = std::uniform_real_distribution<double>;
using uniform_u64 = std::uniform_int_distribution<uint64_t>;

/*****************************************************************
 * Snapshot / percentile tests
 *****************************************************************/

template <typename Dist, typename... Args>
medida::stats::Snapshot
sampleFrom(Args... args)
{
    std::mt19937_64 rng;
    Dist dist(std::forward<Args>(args)...);
    std::vector<double> sample;
    for (size_t i = 0; i < 10000; ++i)
    {
        sample.emplace_back(dist(rng));
    }
    return medida::stats::Snapshot(sample);
}

void
checkPercentiles(medida::stats::Snapshot const& snap, Approx const& e50,
                 Approx const& e75, Approx const& e95, Approx const& e98,
                 Approx const& e99, Approx const& e999)
{
    CHECK(snap.getMedian() == e50);
    CHECK(snap.get75thPercentile() == e75);
    CHECK(snap.get95thPercentile() == e95);
    CHECK(snap.get98thPercentile() == e98);
    CHECK(snap.get99thPercentile() == e99);
    CHECK(snap.get999thPercentile() == e999);
}

TEST_CASE("percentile calculation - constant", "[percentile]")
{
    auto snap = sampleFrom<uniform_dbl>(1.0, 100.0);
    Approx e50 = Approx(50.0).margin(3);
    Approx e75 = Approx(75.0).margin(2);
    Approx e95 = Approx(95.0).margin(1);
    Approx e98 = Approx(98.0).margin(1);
    Approx e99 = Approx(99.0).margin(1);
    Approx e999 = Approx(99.9).margin(0.1);
    checkPercentiles(snap, e50, e75, e95, e98, e99, e999);
}

/*****************************************************************
 * ExpDecaySample tests, time-based
 *****************************************************************/

class ExpDecayTester
{
    // These are private constants in the implementation of Histogram,
    // but we want to reuse them here for testing ExpDecaySample.
    static uint32_t constexpr medidaExpDecayReservoirSize = 1028;
    static double constexpr medidaExpDecayAlpha = 0.015;
    medida::stats::ExpDecaySample mExpDecaySample;
    std::mt19937_64 mRng;
    medida::Clock::time_point mTimestamp;

  public:
    ExpDecayTester()
        : mExpDecaySample(medidaExpDecayReservoirSize, medidaExpDecayAlpha)
        , mTimestamp(medida::Clock::now())
    {
    }
    template <typename Dist, typename... Args>
    void
    addSamplesAtFrequency(size_t nSamples, std::chrono::milliseconds timeStep,
                          Args... args)
    {
        Dist dist(std::forward<Args>(args)...);
        for (size_t i = 0; i < nSamples; ++i)
        {
            mExpDecaySample.Update(dist(mRng), mTimestamp);
            mTimestamp += timeStep;
        }
    }
    // Adds 10 seconds @ 1khz of uniform samples from [low, high]
    void
    addUniformSamplesAtHighFrequency(uint64_t low, uint64_t high)
    {
        auto freq = std::chrono::milliseconds(1);
        addSamplesAtFrequency<uniform_u64>(10000, freq, low, high);
    }
    // Adds 5 minutes @ 30hz of uniform samples from [low, high]
    void
    addUniformSamplesAtMediumFrequency(uint64_t low, uint64_t high)
    {
        auto freq = std::chrono::milliseconds(33);
        addSamplesAtFrequency<uniform_u64>(10000, freq, low, high);
    }
    // Adds 13 hours @ 0.2hz of uniform samples from [low, high]
    void
    addUniformSamplesAtLowFrequency(uint64_t low, uint64_t high)
    {
        auto freq = std::chrono::milliseconds(5000);
        addSamplesAtFrequency<uniform_u64>(10000, freq, low, high);
    }
    medida::stats::Snapshot
    getSnapshot()
    {
        return mExpDecaySample.MakeSnapshot();
    }
    void
    checkPercentiles(Approx const& e50, Approx const& e75, Approx const& e95,
                     Approx const& e98, Approx const& e99, Approx const& e999)
    {
        ::checkPercentiles(getSnapshot(), e50, e75, e95, e98, e99, e999);
    }
};

TEST_CASE("exp decay percentiles - constant", "[expdecay]")
{
    ExpDecayTester et;
    et.addUniformSamplesAtHighFrequency(23, 23);
    Approx e50(23.0), e75(23.0), e95(23.0), e98(23.0), e99(23.0), e999(23.0);
    et.checkPercentiles(e50, e75, e95, e98, e99, e999);
}

TEST_CASE("exp decay percentiles - uniform at high frequency", "[expdecay]")
{
    ExpDecayTester et;
    et.addUniformSamplesAtHighFrequency(1, 100);
    Approx e50 = Approx(50.0).margin(5);
    Approx e75 = Approx(75.0).margin(4);
    Approx e95 = Approx(95.0).margin(3);
    Approx e98 = Approx(98.0).margin(2);
    Approx e99 = Approx(99.0).margin(1);
    Approx e999 = Approx(99.9).margin(0.1);
    et.checkPercentiles(e50, e75, e95, e98, e99, e999);
}

TEST_CASE("exp decay percentiles - uniform at medium frequency", "[expdecay]")
{
    ExpDecayTester et;
    et.addUniformSamplesAtMediumFrequency(1, 100);
    Approx e50 = Approx(50.0).margin(5);
    Approx e75 = Approx(75.0).margin(4);
    Approx e95 = Approx(95.0).margin(3);
    Approx e98 = Approx(98.0).margin(2);
    Approx e99 = Approx(99.0).margin(1);
    Approx e999 = Approx(99.9).margin(0.1);
    et.checkPercentiles(e50, e75, e95, e98, e99, e999);
}

TEST_CASE("exp decay percentiles - uniform at low frequency", "[expdecay]")
{
    ExpDecayTester et;
    et.addUniformSamplesAtLowFrequency(1, 100);
    Approx e50 = Approx(50.0).margin(5);
    Approx e75 = Approx(75.0).margin(4);
    Approx e95 = Approx(95.0).margin(3);
    Approx e98 = Approx(98.0).margin(2);
    Approx e99 = Approx(99.0).margin(1);
    Approx e999 = Approx(99.9).margin(0.1);
    et.checkPercentiles(e50, e75, e95, e98, e99, e999);
}
