// Fast fleet evaluation with the real LeftBehind engine.
// Input (binary, little-endian, from stdin or a file): repeated fleets:
//   int32 fleetId, float32 nominalVolumeM3, int32 nParkings, then for each parking:
//   int32 n, then n x int32 t_s, n x uint8 ignition, n x int16 co2_ppm, n x int16 temp_c_x100.
// The LAST parking of a fleet is the test parking; the others are history, oldest first.
// argv[1] = variants, e.g. "off:0,on:0,on:3,on:10": off = learning disabled (v4), on = v5,
// and the number is how many of the most recent history parkings are fed first.
// Config overrides for tuning: environment variables LB_<field>, for example LB_learnBaseMargin=50.
// Output CSV: fleet,variant,first_check_s_test_parking,history_alarm_bitmask,nBase,nClosed,nCracked
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "leftbehind.h"

struct Park {
  std::vector<int32_t> t;
  std::vector<uint8_t> ign;
  std::vector<int16_t> co2, temp;
};

static void envf(const char* name, float& v) {
  std::string k = std::string("LB_") + name;
  if (const char* e = getenv(k.c_str())) v = (float)atof(e);
}

static LeftBehind::Config baseConfig(float vol, bool learn) {
  LeftBehind::Config c;
  c.cabinVolumeM3 = vol;
  envf("learnBaseMargin", c.learnBaseMargin);
  envf("learnLeakLo", c.learnLeakLo);
  envf("learnLeakHi", c.learnLeakHi);
  envf("learnModelThreshold", c.learnModelThreshold);
  envf("learnMaxResidualSd", c.learnMaxResidualSd);
  envf("learnMaxBaseSe", c.learnMaxBaseSe);
  envf("learnDetectZ", c.learnDetectZ);
  envf("learnTailZ", c.learnTailZ);
  envf("learnBaseMin", c.learnBaseMin);
  envf("learnBaseMax", c.learnBaseMax);
  envf("modelThreshold", c.modelThreshold);
  float f = c.learnMinSamples;
  envf("learnMinSamples", f);
  c.learnMinSamples = (uint16_t)f;
  f = c.learnMinBase;
  envf("learnMinBase", f);
  c.learnMinBase = (uint8_t)f;
  f = c.learnMinLeak;
  envf("learnMinLeak", f);
  c.learnMinLeak = (uint8_t)f;
  c.learnOn = learn;
  return c;
}

// Runs one parking; returns the first Check second (-1 none). Learns from the previous parking first.
static long runPark(LeftBehind& lb, const Park& p) {
  long first = -1;
  lb.learnFromParking();
  lb.setIgnition(true, (uint32_t)p.t[0]);
  int prev = 1;
  for (size_t i = 0; i < p.t.size(); i++) {
    const int g = p.ign[i] ? 1 : 0;
    if (g != prev) {
      lb.setIgnition(g != 0, (uint32_t)p.t[i]);
      prev = g;
    }
    if (!g) {
      const LBResult r = lb.update((uint32_t)p.t[i], (float)p.co2[i], (float)(p.temp[i] / 100.0));
      if (r.level != LBLevel::None && first < 0) first = p.t[i];
    }
  }
  return first;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  FILE* in = argc > 2 ? fopen(argv[2], "rb") : stdin;
  if (!in) return 2;
  struct V {
    bool learn;
    int n;
    std::string name;
  };
  std::vector<V> vars;
  {
    std::string s = argv[1];
    size_t pos = 0;
    while (pos < s.size()) {
      size_t e = s.find(',', pos);
      if (e == std::string::npos) e = s.size();
      std::string tok = s.substr(pos, e - pos);
      const size_t c = tok.find(':');
      vars.push_back({tok.substr(0, c) == "on", atoi(tok.c_str() + c + 1), tok});
      pos = e + 1;
    }
  }
  static LeftBehind lb;
  for (;;) {
    int32_t id, np;
    float vol;
    if (fread(&id, 4, 1, in) != 1) break;
    if (fread(&vol, 4, 1, in) != 1 || fread(&np, 4, 1, in) != 1) return 3;
    std::vector<Park> parks(np);
    for (int k = 0; k < np; k++) {
      int32_t n;
      if (fread(&n, 4, 1, in) != 1) return 3;
      Park& p = parks[k];
      p.t.resize(n);
      p.ign.resize(n);
      p.co2.resize(n);
      p.temp.resize(n);
      if (fread(p.t.data(), 4, n, in) != (size_t)n || fread(p.ign.data(), 1, n, in) != (size_t)n ||
          fread(p.co2.data(), 2, n, in) != (size_t)n || fread(p.temp.data(), 2, n, in) != (size_t)n)
        return 3;
    }
    for (const V& v : vars) {
      lb = LeftBehind(baseConfig(vol, v.learn));
      long histMask = 0;
      const int firstHist = np - 1 - v.n;
      for (int k = firstHist < 0 ? 0 : firstHist; k < np - 1; k++)
        if (runPark(lb, parks[k]) >= 0) histMask |= 1L << k;
      const long first = runPark(lb, parks[np - 1]);
      printf("%d,%s,%ld,%ld,%d,%d,%d\n", id, v.name.c_str(), first, histMask, lb.learnedBaseCount(),
             lb.learnedClosedCount(), lb.learnedCrackedCount());
    }
  }
  return 0;
}
