// Runs the real LeftBehind engine over SIMULATED scenarios.
// Input CSV: scenario,t_s,ignition,co2_ppm,temp_c[,nominal_volume_m3] (rows grouped by scenario, time ascending).
// Output CSV: scenario,first_check_s,first_urgent_s,first_out_of_spec_s (absolute time, -1 = never).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>

#include "leftbehind.h"

struct Out {
  long id = -1;
  long check = -1, urgent = -1, spec = -1;
};

static void flush(const Out& o) {
  if (o.id >= 0) printf("%ld,%ld,%ld,%ld\n", o.id, o.check, o.urgent, o.spec);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: eval scenarios.csv\n");
    return 2;
  }
  FILE* f = fopen(argv[1], "r");
  if (!f) {
    perror("open");
    return 2;
  }
  char line[256];
  if (!fgets(line, sizeof line, f)) return 2;  // header
  printf("scenario,first_check_s,first_urgent_s,first_out_of_spec_s\n");
  static LeftBehind lb;
  Out cur;
  long updates = 0;
  double cpuSec = 0, maxSec = 0;
  int prevIgn = -1;
  while (fgets(line, sizeof line, f)) {
    long id, t, ign;
    double co2, temp, vol = 3.0;
    if (sscanf(line, "%ld,%ld,%ld,%lf,%lf,%lf", &id, &t, &ign, &co2, &temp, &vol) < 5) continue;
    if (id != cur.id) {
      flush(cur);
      cur = Out();
      cur.id = id;
      prevIgn = -1;
      LeftBehind::Config c;
      c.cabinVolumeM3 = (float)vol;  // nominal class: 3 car, 8 van
      if (getenv("LB_MODEL_THR")) c.modelThreshold = (float)atof(getenv("LB_MODEL_THR"));  // tuning only
      if (getenv("LB_MODEL_OFF")) c.modelRuleOn = false;
      lb = LeftBehind(c);
    }
    if (ign != prevIgn) {
      lb.setIgnition(ign != 0, (uint32_t)t);
      prevIgn = (int)ign;
    }
    if (!ign) {
      const auto c0 = std::chrono::steady_clock::now();
      const LBResult r = lb.update((uint32_t)t, (float)co2, (float)temp);
      const double dtc = std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
      cpuSec += dtc;
      if (dtc > maxSec) maxSec = dtc;
      updates++;
      if (r.level != LBLevel::None && cur.check < 0) cur.check = t;
      if (r.level == LBLevel::Urgent && cur.urgent < 0) cur.urgent = t;
      if (r.outOfSpec && cur.spec < 0) cur.spec = t;
    }
  }
  flush(cur);
  fclose(f);
  fprintf(stderr, "updates %ld, mean %.2f us, max %.1f us per update\n", updates, cpuSec / (updates ? updates : 1) * 1e6, maxSec * 1e6);
  return 0;
}
