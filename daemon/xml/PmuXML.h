/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#ifndef PMUXML_H
#define PMUXML_H

#include "lib/midr.h"
#include "linux/smmu_identifier.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class GatorCpu {
public:
    GatorCpu(std::string coreName,
             std::string id,
             std::string counterSet,
             const char * dtName,
             const char * speName,
             const char * speVersion,
             std::set<cpu_utils::cpuid_t> const & cpuIds,
             int pmncCounters,
             bool isV8);

    GatorCpu(std::string coreName,
             std::string id,
             std::string counterSet,
             std::string dtName,
             std::string speName,
             std::string speVersion,
             std::set<cpu_utils::cpuid_t> const & cpuIds,
             int pmncCounters,
             bool isV8);

    GatorCpu(const GatorCpu & that, const char * speName, const char * speVersion);

    GatorCpu(const GatorCpu &) = default;
    GatorCpu & operator=(const GatorCpu &) = default;
    GatorCpu(GatorCpu &&) = default;
    GatorCpu & operator=(GatorCpu &&) = default;

    [[nodiscard]] GatorCpu withUpdatedPmncCount(int pmncCount) const
    {
        return {
            mCoreName,
            mId,
            mCounterSet,
            mDtName,
            mSpeName,
            mSpeVersion,
            mCpuIds,
            pmncCount,
            mIsV8,
        };
    }

    [[nodiscard]] const char * getCoreName() const { return mCoreName.c_str(); }

    [[nodiscard]] const char * getId() const { return mId.c_str(); }

    [[nodiscard]] const char * getCounterSet() const { return mCounterSet.c_str(); }

    [[nodiscard]] const char * getDtName() const { return (!mDtName.empty() ? mDtName.c_str() : nullptr); }

    [[nodiscard]] const char * getSpeName() const { return (!mSpeName.empty() ? mSpeName.c_str() : nullptr); }

    [[nodiscard]] const char * getSpeVersion() const { return (!mSpeVersion.empty() ? mSpeVersion.c_str() : nullptr); }

    [[nodiscard]] bool getIsV8() const { return mIsV8; }

    [[nodiscard]] const std::vector<cpu_utils::cpuid_t> & getCpuIds() const { return mCpuIds; }

    [[nodiscard]] cpu_utils::cpuid_t getMinCpuId() const { return mCpuIds.front(); }

    [[nodiscard]] cpu_utils::cpuid_t getMaxCpuId() const { return mCpuIds.back(); }

    [[nodiscard]] int getPmncCounters() const { return mPmncCounters; }

    [[nodiscard]] bool hasCpuId(cpu_utils::cpuid_t cpuId) const;

private:
    std::string mCoreName;
    std::string mId;
    std::string mCounterSet;
    std::string mDtName;
    std::string mSpeName;
    std::string mSpeVersion;
    std::vector<cpu_utils::cpuid_t> mCpuIds;
    int mPmncCounters;
    bool mIsV8;

    GatorCpu(std::string coreName,
             std::string id,
             std::string counterSet,
             std::string dtName,
             std::string speName,
             std::string speVersion,
             std::vector<cpu_utils::cpuid_t> cpuIds,
             int pmncCounters,
             bool isV8);
};

bool operator==(const GatorCpu & a, const GatorCpu & b);
bool operator<(const GatorCpu & a, const GatorCpu & b);

inline bool operator!=(const GatorCpu & a, const GatorCpu & b)
{
    return !(a == b);
}

class UncorePmu {
public:
    UncorePmu(std::string coreName,
              std::string id,
              std::string counterSet,
              std::string deviceInstance,
              int pmncCounters,
              bool hasCyclesCounter);

    UncorePmu(const UncorePmu &) = default;
    UncorePmu & operator=(const UncorePmu &) = default;
    UncorePmu(UncorePmu &&) = default;
    UncorePmu & operator=(UncorePmu &&) = default;

    [[nodiscard]] const char * getCoreName() const { return mCoreName.c_str(); }

    [[nodiscard]] const char * getId() const { return mId.c_str(); }

    [[nodiscard]] const char * getCounterSet() const { return mCounterSet.c_str(); }

    /** Returns null if the uncore is not instanced */
    [[nodiscard]] const char * getDeviceInstance() const
    {
        return (mDeviceInstance.empty() ? nullptr : mDeviceInstance.c_str());
    }

    [[nodiscard]] int getPmncCounters() const { return mPmncCounters; }

    [[nodiscard]] bool getHasCyclesCounter() const { return mHasCyclesCounter; }

private:
    std::string mCoreName;
    std::string mId;
    std::string mCounterSet;
    std::string mDeviceInstance;
    int mPmncCounters;
    bool mHasCyclesCounter;
};

class smmu_v3_pmu_t {
public:
    smmu_v3_pmu_t(std::string core_name,
                  std::string id,
                  std::string counter_set,
                  int pmnc_counters,
                  std::optional<gator::smmuv3::iidr_t> iidr)
        : core_name(std::move(core_name)),
          id(std::move(id)),
          counter_set(std::move(counter_set)),
          pmnc_counters(pmnc_counters),
          iidr(std::move(iidr))
    {
    }

    [[nodiscard]] const char * get_core_name() const { return core_name.c_str(); }

    [[nodiscard]] const std::string & get_id() const { return id; }

    [[nodiscard]] const char * get_counter_set() const { return counter_set.c_str(); }

    [[nodiscard]] int get_pmnc_counters() const { return pmnc_counters; }

    [[nodiscard]] const std::optional<gator::smmuv3::iidr_t> & get_iidr() const { return iidr; }

private:
    std::string core_name;
    std::string id;
    std::string counter_set;
    int pmnc_counters;
    std::optional<gator::smmuv3::iidr_t> iidr;
};

struct PmuXML {
    static const std::string_view DEFAULT_XML;

    [[nodiscard]] const GatorCpu * findCpuByName(const char * name) const;
    [[nodiscard]] const GatorCpu * findCpuById(cpu_utils::cpuid_t cpuid) const;
    [[nodiscard]] const UncorePmu * findUncoreByName(const char * name) const;

    std::vector<GatorCpu> cpus;
    std::vector<UncorePmu> uncores;
    std::vector<smmu_v3_pmu_t> smmu_pmus;
};

#endif // PMUXML_H
