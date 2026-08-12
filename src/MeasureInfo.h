#ifndef MEASURE_INFO_H
#define MEASURE_INFO_H

#include <string>
#include <vector>

#include "NoteData.h"
#include "TimingData.h"

struct MeasureInfo {
  int measureCount;
  float peakNps;
  std::vector<float> npsPerMeasure;
  std::vector<int> notesPerMeasure;

  MeasureInfo() { Zero(); }

  void Zero() {
    measureCount = 0;
    peakNps = 0;
    npsPerMeasure.clear();
    notesPerMeasure.clear();
  }

  std::string ToString() const;
  void FromString(const std::string& sValues);
  static void CalculateMeasureInfo(
      const NoteData& in, TimingData* timing, MeasureInfo& out);
};

/* A beginner chart with a peak NPS at or below this is slow enough that
 * Merciful Beginner behavior (reduced score/life penalties) should kick in
 * automatically, even if the player hasn't enabled the preference.
 * If the peak NPS is higher, even if it's in the beginner slot, we won't
 * enforce Merciful Beginner lifebar behavior. */
const float MERCIFUL_BEGINNER_MAX_PEAK_NPS = 2.5f;

#endif
