// Touch bench. Sweeps the analog acquisition settings on real hardware so the
// multi-touch collapse can be characterised without a reflash per guess.
//
// Short-press  -> next config (re-baselines and clears the min/max tracking)
// Long-press   -> sweep COMPCAP and INTCAP, then calibrate COMPCAP per pad to
//                 park each idle reading near TOUCH_CAL_TARGET and re-baseline
//
// Prints a table per pad: raw, baseline, current delta, and the largest
// positive and negative excursions seen since the last reset. The negative
// column is the important one - the failure shows up as a pad reading *below*
// its baseline when a neighbour is also held.
#pragma once

#include <Arduino.h>

// Never returns. Call instead of the normal role dispatch.
void touchDiagRun();
