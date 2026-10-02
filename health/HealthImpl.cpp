/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Cityman splits the battery across two power_supply nodes:
 *
 *   qpnp-fg.c        chip->bms_psy.name = "bms"
 *                    chip->bms_psy.type = POWER_SUPPLY_TYPE_BMS
 *   qpnp-smbcharger  chip->batt_psy.name = "battery"
 *                    chip->batt_psy.type = POWER_SUPPLY_TYPE_BATTERY
 *
 * BatteryMonitor::readPowerSupplyType() has no "BMS" entry, so "bms" maps to
 * ANDROID_POWER_SUPPLY_TYPE_UNKNOWN and is skipped. Every path is autodetected
 * under "battery" instead. smbchg_battery_properties[] does not contain
 * POWER_SUPPLY_PROP_CHARGE_FULL, CHARGE_FULL_DESIGN or CYCLE_COUNT -- only
 * fg_power_props[] does -- so batteryFullChargePath, batteryCycleCountPath and
 * batteryFullChargeDesignCapacityUahPath stay empty and healthd logs
 * "<name> not found". The HIDL fields then report 0.
 *
 * BatteryMonitor::init() only autodetects a path that is still empty, so
 * pinning the three fuel-gauge paths here is enough; capacity, status, health,
 * voltage, current, temp and charge_counter are left to autodetect on
 * "battery", which is where smbcharger proxies them from the gauge.
 *
 * The three pinned fuel-gauge values are then gated in UpdateHealthInfo()
 * because what qpnp-fg exports under those names is not always a capacity:
 *
 *   charge_full        learning_data.learned_cc_uah. Seeded from NOM_CAP_REG
 *                      only when the SRAM copy reads 0; otherwise whatever the
 *                      SRAM holds is published verbatim. This RM-1104 reads
 *                      -819000 uAh on kernel #21.
 *   cycle_count        fg_get_cycle_count(), a per-SOC-bucket SRAM counter,
 *                      not a pack cycle count. Reads 14921 here.
 *   charge_full_design nom_cap_uah from the loaded profile. Reads 3043000,
 *                      matching the 3000 mAh BV-T5E.
 *
 * Framework consumers take these at face value: BatteryStatsImpl feeds
 * batteryFullCharge into min/max learned capacity, and any Settings health page
 * would print them as-is. So a value that cannot be a Li-ion capacity is left
 * at 0 (the same thing BatteryMonitor reports when the path is missing) rather
 * than handed on. Nothing is substituted: an unlearned gauge reports no
 * learned capacity, not a guessed one.
 */

#include <errno.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <health/utils.h>
#include <health2impl/Health.h>
#include <utils/String8.h>

using ::android::hardware::health::InitHealthdConfig;
using ::android::hardware::health::V2_1::HealthInfo;
using ::android::hardware::health::V2_1::IHealth;
using ::android::hardware::health::V2_1::implementation::Health;

using namespace std::literals;

namespace {

constexpr char kFgChargeFull[] = "/sys/class/power_supply/bms/charge_full";
constexpr char kFgChargeFullDesign[] = "/sys/class/power_supply/bms/charge_full_design";
constexpr char kFgCycleCount[] = "/sys/class/power_supply/bms/cycle_count";
constexpr char kFgChargeNow[] = "/sys/class/power_supply/bms/charge_now";
constexpr char kFgVoltageNow[] = "/sys/class/power_supply/bms/voltage_now";

/*
 * Microsoft BV-T5E, the only pack this device tree carries a profile for:
 * 3000 mAh nominal (Cityman_batterydata microsoft_bvt5e_3000mah). Used as the
 * yardstick when the gauge's own charge_full_design is not believable.
 */
constexpr int32_t kNominalCapacityUah = 3000000;

/*
 * A learned or design figure more than this far from the reference is not a
 * reading of this pack. qpnp-fg's fg_cap_learning_post_process() moves
 * learned_cc_uah by at most +0.5% / -10% per completed learning cycle
 * (cl-max-increment-deciperc / cl-max-decrement-deciperc defaults), so a real
 * learned value walks away from nominal slowly; the SRAM garbage seen here
 * (-819000) is nowhere near. The trade-off is that a pack genuinely worn below
 * 80% of design reports no learned capacity rather than a low one. This pack's
 * charge_counter reads ~2999 mAh at 100%, so it is not in that regime.
 */
constexpr int32_t kCapacityTolerancePercent = 20;

/*
 * Upper bound on a believable pack cycle count. Li-ion phone packs are rated
 * for 500-1000 cycles; 4000 leaves a wide margin above that and is still an
 * order of magnitude short of the 14921 the SRAM bucket counter reports.
 */
constexpr int32_t kMaxCycleCount = 4000;

bool WithinTolerance(int32_t value, int32_t reference) {
    if (reference <= 0) {
        return false;
    }
    int64_t diff = std::abs(static_cast<int64_t>(value) - reference);
    return diff * 100 <= static_cast<int64_t>(reference) * kCapacityTolerancePercent;
}

bool DesignCapacityIsSane(int32_t design_uah) {
    return design_uah > 0 && WithinTolerance(design_uah, kNominalCapacityUah);
}

bool FullChargeIsSane(int32_t full_uah, int32_t reference_uah) {
    return full_uah > 0 && WithinTolerance(full_uah, reference_uah);
}

bool CycleCountIsSane(int32_t cycles) {
    return cycles >= 0 && cycles <= kMaxCycleCount;
}

class CitymanHealth : public Health {
  public:
    using Health::Health;

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override;

  private:
    // Last values that were refused, so the log carries one line per change of
    // sysfs value rather than one per poll.
    int32_t last_bad_design_uah_ = 0;
    int32_t last_bad_full_uah_ = 0;
    int32_t last_bad_cycles_ = 0;
};

void CitymanHealth::UpdateHealthInfo(HealthInfo* health_info) {
    auto& legacy = health_info->legacy.legacy;

    int32_t design_uah = health_info->batteryFullChargeDesignCapacityUah;
    int32_t reference_uah = kNominalCapacityUah;
    if (DesignCapacityIsSane(design_uah)) {
        reference_uah = design_uah;
    } else if (design_uah != 0) {
        if (design_uah != last_bad_design_uah_) {
            LOG(WARNING) << "bms/charge_full_design=" << design_uah
                         << " uAh is not a BV-T5E design capacity; reporting none";
            last_bad_design_uah_ = design_uah;
        }
        health_info->batteryFullChargeDesignCapacityUah = 0;
    }

    int32_t full_uah = legacy.batteryFullCharge;
    if (full_uah != 0 && !FullChargeIsSane(full_uah, reference_uah)) {
        if (full_uah != last_bad_full_uah_) {
            LOG(WARNING) << "bms/charge_full=" << full_uah << " uAh is not within "
                         << kCapacityTolerancePercent << "% of " << reference_uah
                         << " uAh; reporting no learned capacity";
            last_bad_full_uah_ = full_uah;
        }
        legacy.batteryFullCharge = 0;
    }

    int32_t cycles = legacy.batteryCycleCount;
    if (!CycleCountIsSane(cycles)) {
        if (cycles != last_bad_cycles_) {
            LOG(WARNING) << "bms/cycle_count=" << cycles
                         << " is not a pack cycle count (limit " << kMaxCycleCount
                         << "); reporting none";
            last_bad_cycles_ = cycles;
        }
        legacy.batteryCycleCount = 0;
    }
}

std::optional<int64_t> ReadInt64(const char* path) {
    std::string buf;
    if (!::android::base::ReadFileToString(path, &buf)) {
        return std::nullopt;
    }
    int64_t value = 0;
    if (!::android::base::ParseInt(::android::base::Trim(buf), &value)) {
        return std::nullopt;
    }
    return value;
}

/*
 * healthd_config::energyCounter reports remaining energy in nWh.
 *
 * qpnp-fg returns charge_now as learning_data.cc_uah (uAh) and voltage_now from
 * FG_DATA_VOLTAGE (uV). uAh * uV is 1e-12 Wh, and nWh is 1e-9 Wh, so the nWh
 * value is that product divided by 1000. 3000 mAh at 4.4 V is ~1.3e10 nWh,
 * well inside int64_t.
 *
 * cc_uah is coulomb-counter learning state: it reads 0 until the gauge has
 * completed a charge cycle against the microsoft_bvt5e_3000mah profile. Report
 * an error in that case instead of 0 nWh so callers treat it as unavailable
 * rather than as an empty battery.
 */
int CitymanEnergyCounter(int64_t* energy) {
    std::optional<int64_t> charge_uah = ReadInt64(kFgChargeNow);
    std::optional<int64_t> voltage_uv = ReadInt64(kFgVoltageNow);

    if (!charge_uah.has_value() || !voltage_uv.has_value()) {
        return -ENOENT;
    }
    if (*charge_uah <= 0 || *voltage_uv <= 0) {
        return -EAGAIN;
    }

    *energy = (*charge_uah * *voltage_uv) / 1000;
    return 0;
}

}  // namespace

/*
 * batteryCapacityLevel and batteryChargeTimeToFullNowSeconds are deliberately
 * left at BatteryMonitor's UNSUPPORTED default.
 *
 * CAF 3.10 qpnp-fg exports neither POWER_SUPPLY_PROP_CAPACITY_LEVEL nor a
 * time-to-full property, so there is no sysfs source for either. Synthesising a
 * capacity level from the percentage would change shutdown behaviour:
 * BatteryService::shouldShutdownLocked() shuts down as soon as the HAL reports
 * CRITICAL, whereas its UNSUPPORTED fallback only shuts down at level 0.
 * config_criticalBatteryWarningLevel (5) drives the warning UI, not shutdown,
 * so reusing it here would power the phone off at 5%.
 */
extern "C" IHealth* HIDL_FETCH_IHealth(const char* instance) {
    if (instance != "default"sv) {
        return nullptr;
    }

    auto config = std::make_unique<healthd_config>();
    InitHealthdConfig(config.get());

    config->batteryFullChargePath = ::android::String8(kFgChargeFull);
    config->batteryFullChargeDesignCapacityUahPath = ::android::String8(kFgChargeFullDesign);
    config->batteryCycleCountPath = ::android::String8(kFgCycleCount);
    config->energyCounter = CitymanEnergyCounter;

    return new CitymanHealth(std::move(config));
}
