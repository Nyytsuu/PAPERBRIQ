#pragma once
// Commissioning values, not operator time settings. Zero leaves production
// disabled until measured operating limits and hardware protection are known.
// Do not infer a time limit from a pump's current rating or heater wattage.
#ifndef PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS
#define PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS 0UL
#endif
#ifndef PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS
#define PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS 0UL
#endif
