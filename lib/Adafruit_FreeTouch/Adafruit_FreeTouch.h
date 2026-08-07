#ifndef ADAFRUIT_FREETOUCH_H
#define ADAFRUIT_FREETOUCH_H

#include "adafruit_ptc.h"
#include <Arduino.h>

class Adafruit_FreeTouch {
public:
  Adafruit_FreeTouch(int p = 0, oversample_t f = OVERSAMPLE_4,
                     series_resistor_t r = RESISTOR_0,
                     freq_mode_t fh = FREQ_MODE_NONE);
  bool begin(void);

  uint16_t measure(void);
  uint16_t measureRaw(void);

  // LOCAL FORK. These four were private upstream; they are the only change.
  //
  // Every badge pad idles at 400-680 out of a ~1016 full scale, so parasitic
  // capacitance eats 40-67% of the range before a finger is anywhere near the
  // slider. One touch then pegs the reading at the ceiling, and a second touch
  // pushes the conversion out of range and the result collapses. setCompCap()
  // is the register that cancels that standing offset, so it has to be
  // reachable to calibrate each pad down to a usable starting point.
  //
  // setFreqHopping() also matters: adafruit_ptc_get_config_default() never
  // initialises config.hops, which becomes SAMPLEDELAY whenever freqhop is not
  // FREQ_MODE_NONE. Calling this explicitly is the only way to use spread
  // spectrum without an uninitialised sample delay.
  void setOversampling(oversample_t lvl);
  void setSeriesResistor(series_resistor_t res);
  void setFreqHopping(freq_mode_t fh, freq_hop_t hops = FREQ_HOP_1);
  void setCompCap(uint16_t cc);   // 0..16383, default 8192
  void setIntCap(uint8_t ic);     // 0..63, default 63

private:
  void ptcInitSettings(void);
  void ptcConfigIOpin(void);
  uint16_t startPtcAcquire(void);

  int getYLine(void);
  void selectYLine(void);

  void snapshotRegsAndPrint(uint32_t base, uint8_t numregs);
  void printHex(uint8_t h, boolean newline);
  void printPTCregs(uint32_t base, uint8_t *regs, uint8_t num);

private:
  int pin; // arduino pin #
  struct adafruit_ptc_config config;
};

#endif
