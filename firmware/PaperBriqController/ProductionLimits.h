#pragma once
// Commissioning ceilings, not per-batch times. Pump flow/dose is not measured
// yet, so automatic water dosing remains unavailable (0).
// Do not infer a time limit from a pump's current rating or heater wattage.
#ifndef PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS
#define PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS 0UL
#endif
#ifndef PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS
#define PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS 600UL
#endif
#ifndef PAPERBRIQ_PREHEAT_MAX_SECONDS
#define PAPERBRIQ_PREHEAT_MAX_SECONDS 600UL
#endif
#ifndef PAPERBRIQ_CHAMBER_TARGET_C
#define PAPERBRIQ_CHAMBER_TARGET_C 45
#endif
// Set only after confirming the heater's own fan is continuously powered.
// Heating and forming remain mutually exclusive; supply capacity is unverified.
#ifndef PAPERBRIQ_HEATER_FAN_CONFIRMED
#define PAPERBRIQ_HEATER_FAN_CONFIRMED 1
#endif
// Initial drying ceiling: 10 minutes per explicitly started cycle. This does
// not certify dryness. Physical thermostat, independent cutoff and fan remain
// required. Operator confirmed cutoff and pump suppression on 2026-10-31.
