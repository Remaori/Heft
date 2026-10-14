#pragma once

// =============================================================================
//  Tuning.h: every number that defines Heft's character.
//
//  To change the sound: edit a value, save, run build.bat and reload the
//  project in REAPER. "f" = Fatness and "c" = Color, both in 0..1.
// =============================================================================

namespace tuning
{
    // --- 1. Compressor (stereo-linked, peak detector) -----------------------
    inline constexpr float compAttackMs          = 2.0f;
    inline constexpr float compReleaseMs         = 80.0f;
    inline constexpr float compKneeDb            = 6.0f;    // soft knee width
    inline constexpr float compThresholdAtMaxDb  = -30.0f;  // threshold = f * this (0 dB at Fatness 0 %)
    inline constexpr float compRatioAtMax        = 6.0f;    // ratio = 1 + f * (this - 1)  (1:1 at Fatness 0 %)
    inline constexpr float compMakeupAmount      = 0.5f;    // auto makeup: fraction (0..1) of the gain
                                                            // reduction at 0 dBFS that is given back

    // --- 2 and 5. Pre-emphasis / de-emphasis tilt EQ ------------------------
    inline constexpr float tiltPivotHz           = 700.0f;  // frequency that stays unchanged
    inline constexpr float tiltMaxDb             = 6.0f;    // at Color 100 %: highs +6 dB and lows -6 dB
                                                            // into the saturator (reversed at Color 0 %)

    // --- 3. Saturation (asymmetric tanh) ------------------------------------
    inline constexpr float driveAtMax            = 4.0f;    // drive = f * this; the tanh ceiling is 1/drive
                                                            // (4 -> ceiling ~ -12 dBFS at Fatness 100 %)
    inline constexpr float biasAtMinColor        = 0.0f;    // bias (even harmonics) at Color 0 %
    inline constexpr float biasAtMaxColor        = 0.35f;   // bias at Color 100 %
    inline constexpr float fatnessMakeupAtMaxDb  = 6.0f;    // extra level after saturation: f * this
                                                            // (what makes it "sound louder")

    // --- 4. Oversampling ----------------------------------------------------
    inline constexpr int   oversamplingFactorLog2 = 2;      // 2^2 = 4x

    // --- 6. DC blocker ------------------------------------------------------
    inline constexpr float dcBlockerHz           = 5.0f;

    // --- 7. Output ----------------------------------------------------------
    inline constexpr float clipperKnee           = 0.75f;   // linear below this (0.75 ~ -2.5 dBFS)
    inline constexpr float clipperCeiling        = 1.0f;    // clipper ceiling (1.0 = 0 dBFS)
    inline constexpr int   clipperOversamplingFactorLog2 = 2;   // 4x: less aliasing, catches most
                                                                // inter-sample peaks; adds latency
    inline constexpr float levelMatchWindowSec   = 0.4f;    // loudness window (0.4 s = BS.1770 momentary)
    inline constexpr float levelMatchGlideSec    = 0.05f;   // smoothing of the compensation gain
    inline constexpr float levelMatchRangeDb     = 24.0f;   // maximum correction (+/-)
    inline constexpr float levelMatchGateDb      = -60.0f;  // below this it stops re-measuring (silence)

    // --- Parameter smoothing and bypass -------------------------------------
    inline constexpr double paramSmoothingSec    = 0.05;
    inline constexpr double bypassFadeSec        = 0.03;    // crossfade when the host bypass toggles
}
