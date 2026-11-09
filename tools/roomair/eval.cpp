// Runs the real firmware RoomAir engine over many SIMULATED rooms.
// Input (binary, little endian), repeated until end of file:
//   int32 n, float32 volumeM3, float32 genLsPerPerson, float32 leakDefault, then n float32 CO2 values (one per minute).
// Output (binary): for each room, n rows of 13 float32:
//   valid steady occupied leakLearned outdoor excess rebreathedPct freshAir low mid high risk leak
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "roomair.h"

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: roomair_eval in.bin out.bin\n"); return 2; }
  FILE* in = fopen(argv[1], "rb");
  FILE* out = fopen(argv[2], "wb");
  if (!in || !out) { fprintf(stderr, "cannot open files\n"); return 2; }
  int32_t n;
  while (fread(&n, 4, 1, in) == 1) {
    float p[3];
    if (fread(p, 4, 3, in) != 3) return 3;
    std::vector<float> co2(n);
    if (fread(co2.data(), 4, n, in) != (size_t)n) return 3;
    RoomAir::Config c;
    c.volumeM3 = p[0];
    c.genLsPerPerson = p[1];
    c.leakPerHourDefault = p[2];
    RoomAir a(c);
    for (int i = 0; i < n; i++) {
      a.addMinute(co2[i]);
      const RoomAirReport r = a.report();
      const float row[13] = {(float)r.valid, (float)r.steady, (float)r.occupied, (float)r.leakLearned, r.outdoorPpm,
                             r.excessPpm, r.rebreathedPct, r.freshAirLsPerPerson, r.peopleLow, r.peopleMid,
                             r.peopleHigh, (float)r.risk, r.leakPerHour};
      fwrite(row, 4, 13, out);
    }
  }
  fclose(in);
  fclose(out);
  return 0;
}
