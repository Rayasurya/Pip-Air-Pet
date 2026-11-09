// Parity harness: runs the real LeftBehind (v5) over whole SIMULATED fleet histories.
// Input CSV: fleet,t_s,ignition,co2_ppm,temp_c,nominal_volume_m3 (rows grouped by fleet, then by parking in
// time order; each parking starts with ignition=1 rows and its clock restarts at 0).
// Output CSV, one line per parking: fleet,parking,first_check_s,nBase,nClosed,nCracked,saved_state_hex
// After every parking the learned state is saved to bytes and loaded back, as main.cpp does with Preferences.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "leftbehind.h"

static LeftBehind lb;
static long curFleet = -1;
static int parking = -1;
static long firstCheck = -1;
static bool inPark = false;

static void finishParking() {
  if (!inPark) return;
  lb.learnFromParking();
  uint8_t buf[kLbLearnedBytes];
  const size_t n = lb.saveLearned(buf, sizeof buf);
  if (n != kLbLearnedBytes || !lb.loadLearned(buf, n)) {
    fprintf(stderr, "save/load roundtrip failed\n");
    exit(3);
  }
  printf("%ld,%d,%ld,%d,%d,%d,", curFleet, parking, firstCheck, lb.learnedBaseCount(), lb.learnedClosedCount(),
         lb.learnedCrackedCount());
  for (size_t i = 0; i < n; i++) printf("%02x", buf[i]);
  printf("\n");
  inPark = false;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  FILE* f = fopen(argv[1], "r");
  if (!f) return 2;
  char line[256];
  if (!fgets(line, sizeof line, f)) return 2;
  printf("fleet,parking,first_check_s,n_base,n_closed,n_cracked,state_hex\n");
  int prevIgn = -1;
  while (fgets(line, sizeof line, f)) {
    long id, t, ign;
    double co2, temp, vol = 3.0;
    if (sscanf(line, "%ld,%ld,%ld,%lf,%lf,%lf", &id, &t, &ign, &co2, &temp, &vol) < 5) continue;
    if (id != curFleet) {
      finishParking();
      curFleet = id;
      parking = -1;
      prevIgn = -1;
      LeftBehind::Config c;
      c.cabinVolumeM3 = (float)vol;
      lb = LeftBehind(c);
    }
    if (ign != prevIgn) {
      if (ign) {
        finishParking();  // learn from the parking that just ended, then reset
        parking++;
        firstCheck = -1;
      } else {
        inPark = true;
      }
      lb.setIgnition(ign != 0, (uint32_t)t);
      prevIgn = (int)ign;
    }
    if (!ign) {
      const LBResult r = lb.update((uint32_t)t, (float)co2, (float)temp);
      if (r.level != LBLevel::None && firstCheck < 0) firstCheck = t;
    }
  }
  finishParking();
  fclose(f);
  return 0;
}
