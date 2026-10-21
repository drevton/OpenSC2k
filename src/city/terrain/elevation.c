#include "internal.h"

// Relief features ported from OpenSC2K new_city/elevation.rs.

// Flood outward from below-sea cells to compute bounded distance-to-water values. Work must own full scratch and queue
// arrays; both are overwritten for this generation pass and no City state is used.
void TerrainWaterDistances(TerrainWork *work) {
  int count = 0;
  for (int at = 0; at < work->cells; at++) {
    work->scratch[at] = 256;
    if (work->heights[at] < work->sea) {
      work->scratch[at] = 0;
      work->queue[count++] = at;
    }
  }
  static const int kDx[4] = {-1, 1, 0, 0}, kDy[4] = {0, 0, -1, 1};
  for (int cursor = 0; cursor < count; cursor++) {
    int at = work->queue[cursor], x = at / work->height, y = at % work->height;
    for (int d = 0; d < 4; d++) {
      int nx = x + kDx[d], ny = y + kDy[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if (work->scratch[next] > work->scratch[at] + 1) {
        work->scratch[next] = work->scratch[at] + 1;
        work->queue[count++] = next;
      }
    }
  }
}

// Select a dry perimeter anchor near the supplied fixed-point angle. Requires initialized generation heights; returns
// the fallback normalized point when no suitable edge is found.
static TerrainVector TerrainPlateauAnchor(const TerrainWork *work, Fixed angle) {
  int start = (int)FixedRound(FixedDiv(angle, (kTerrainPi * 2)) * (4 * kTerrainLast)) % (4 * kTerrainLast);
  if (start < 0) start += (4 * kTerrainLast);
  for (int step = 0; step < (4 * kTerrainLast); step++) {
    int along = (start + step) % (4 * kTerrainLast), side = along / kTerrainLast, offset = along % kTerrainLast;
    int x = side == 0 ? offset : side == 1 ? kTerrainLast : side == 2 ? kTerrainLast - offset : 0;
    int y = side == 0 ? 0 : side == 1 ? offset : side == 2 ? kTerrainLast : kTerrainLast - offset;
    x = x * (work->width - 1) / kTerrainLast;
    y = y * (work->height - 1) / kTerrainLast;
    if (work->heights[x * work->height + y] >= work->sea)
      return TerrainMapPoint(work, x, y, 0);
  }
  return TerrainV(0, -32768);
}

// Apply enabled plateau, ridge, valley and related relief features to generated heights. Requires initialized work and
// noise; reuses water-distance scratch, clamps elevations to supported levels and consumes only generation state.
void ApplyTerrainElevation(TerrainWork *work, Fixed angle, Fixed phase, const TerrainNoise *noise) {
  u32 f = work->features;
  if (!(f & (kPlateau | kRidge | kValley | kRolling | kBasin | kCanyon | kCliffs))) return;
  TerrainWaterDistances(work);
  Fixed sea = FixedFromInt(work->sea), relief = 458752 + FixedFromInt(work->hills) / 5;
  Fixed inland = sea + (f & kCliffs ? relief : 0);
  static const Fixed kReliefDetail[] = {16384, FIXED_ONE, 131072, 196608};
  Fixed detail = kReliefDetail[work->smoothness];
  TerrainVector anchor = TerrainRotate(TerrainPlateauAnchor(work, angle), -angle);
  for (int x = 0; x < work->width; x++) {
    for (int y = 0; y < work->height; y++) {
      int at = x * work->height + y;
      if (work->heights[at] < work->sea) continue;
      TerrainVector point = TerrainMapPoint(work, x, y, angle);
      TerrainVector detail_point = TerrainDetailPoint(work, x, y, angle);
      Fixed px = point.x, py = point.y;
      Fixed rough = FixedMul(SampleTerrainNoise(noise, detail_point.x, detail_point.y), detail);
      Fixed value = FixedFromInt(work->heights[at]);
      if (f & kCliffs) value = sea + FixedFromInt(FixedRound(relief));
      if (f & kPlateau) {
        Fixed radius =
            TerrainLength(TerrainDivide(TerrainSub(point, anchor), TerrainV(27525, 23593))) + FixedMul(rough, 14418);
        value = TerrainLerp(value, sea + relief + 131072, FIXED_ONE - TerrainSmoothstep(47186, 73400, radius));
      }
      if (f & kRidge) {
        Fixed axis = FixedMul(3932, FixedSin(py * 8 + phase)), across = FixedDiv((px - axis), 8520);
        Fixed peaks = 52429 + FixedMul(13107, FixedSin(py * 17 + phase));
        value += FixedMul(FixedMul(relief, FixedExp(FixedMul(-across, across))), peaks);
      }
      if (f & kRolling) {
        TerrainVector warp = TerrainScale(TerrainV(SampleTerrainNoise(noise, detail_point.x + 851968, detail_point.y),
                                              SampleTerrainNoise(noise, detail_point.x, detail_point.y - 1114112)),
            10486);
        Fixed broad = SampleTerrainNoise(noise, FixedMul((detail_point.x + warp.x), 29491) + 1395917,
            FixedMul((detail_point.y + warp.y), 29491) - 635699);
        Fixed waves = FixedMax(0, FixedMin(FIXED_ONE, 32768 + FixedMul(broad, 85197) + FixedMul(rough, 5243)));
        value = inland + 131072 + FixedMul(FixedMul(waves, relief), 55706);
      }
      if (f & kBasin) {
        TerrainVector local =
            TerrainSub(point, TerrainV(FixedMul(3932, FixedSin(phase)), FixedMul(3277, FixedCos(phase))));
        local =
            TerrainAdd(local, TerrainScale(TerrainV(rough, SampleTerrainNoise(noise, px + 458752, py - 720896)), 9175));
        Fixed local_angle = TerrainAngle(local);
        Fixed edge = FIXED_ONE + FixedMul(10486, FixedSin(local_angle * 3 + phase)) +
            FixedMul(6554, FixedSin(local_angle * 5 - phase));
        Fixed radius =
            FixedDiv(TerrainLength(TerrainDivide(local, TerrainV(FIXED_ONE, 57016))), edge) + FixedMul(rough, 2621);
        value = inland + FIXED_ONE + FixedMul(relief, TerrainSmoothstep(5243, 31457, radius));
      }
      if (f & kValley) {
        Fixed rise = TerrainSmoothstep(4260, 19333, FixedFromInt(work->scratch[at]) / kTerrainLast);
        value = TerrainLerp(sea, FixedMax(value, sea + relief), rise);
      }
      if (f & kCanyon) {
        Fixed axis = FixedMul(9175, FixedSin(py * 6 + phase)) + FixedMul(2294, FixedSin(py * 11 - phase));
        Fixed floor_width = 5898 + FixedMul(983, FixedSin(py * 4 + phase));
        Fixed rise = TerrainSmoothstep(floor_width, floor_width + 5243, FixedAbs(px - axis));
        value = TerrainLerp(sea + FIXED_ONE, FixedMax(value, sea + relief), rise);
      }
      work->heights[at] = IntClamp((int)FixedRound(value), work->sea, 31);
    }
  }
}
