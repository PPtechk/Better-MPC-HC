/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

// Audio fingerprints to find the intro and the outro of an episode: the part of the audio that the
// other episodes of the series also have (the same opening or ending song).
//
// The audio is analysed as mono at 11025 Hz in frames of 4096 samples (0.37 s), one frame every 1024
// samples (0.093 s). Every frame gives a 32-bit hash: the signs of the energy differences of 33
// neighbouring frequency bands between 300 and 3000 Hz, compared with the previous frame (the robust
// hash of Haitsma and Kalker). The same music gives nearly the same bits, other audio about half of them.

const int SKIP_AUDIO_SAMPLE_RATE = 11025;
const int SKIP_AUDIO_FRAME = 4096;
const int SKIP_AUDIO_HOP = 1024;

enum SkipAudioRegion {
    SKIP_AUDIO_INTRO, // the first 40% of the file, at most 10 minutes
    SKIP_AUDIO_OUTRO, // the last 40% of the file, at most 6 minutes
};

// The part of a file of the given duration (seconds) that is analysed for the region
void GetSkipAudioRegion(double duration, SkipAudioRegion region, double& start, double& end);

struct SkipAudioFingerprint {
    double duration;   // of the whole file, seconds
    double startTime;  // time of the first analysed sample, seconds
    std::vector<uint32_t> hashes; // hashes[k]: the frame starting at sample k * SKIP_AUDIO_HOP, 0: silence

    SkipAudioFingerprint() : duration(0.0), startTime(0.0) {}

    double FrameCenter(size_t k) const {
        return startTime + (double(k) * SKIP_AUDIO_HOP + SKIP_AUDIO_FRAME / 2) / SKIP_AUDIO_SAMPLE_RATE;
    }
};

// Turns mono audio at SKIP_AUDIO_SAMPLE_RATE into frame hashes
class CSkipAudioFingerprinter
{
public:
    CSkipAudioFingerprinter();

    void AddSamples(const float* pSamples, size_t count);
    void AddSilence(size_t count);

    size_t GetSampleCount() const { return m_samples; }
    std::vector<uint32_t>& GetHashes() { return m_hashes; }

private:
    std::vector<float> m_window;
    std::vector<int> m_bitReverse;
    std::vector<double> m_cos, m_sin;
    std::vector<int> m_bandStart; // first FFT bin of every band, plus the end of the last band

    std::vector<float> m_pending; // samples not yet used by a frame
    size_t m_pendingPos;
    size_t m_samples;
    std::vector<double> m_re, m_im;
    std::vector<double> m_energy, m_prevEnergy;
    bool m_bHavePrev, m_bPrevSilent;
    std::vector<uint32_t> m_hashes;

    void ProcessFrame(const float* pFrame);
};

struct SkipAudioSegment {
    bool bFound;
    double start, end; // seconds in the current file
    int support;       // the number of other files that have the same part
    int compared;      // the number of other files that could be compared

    SkipAudioSegment() : bFound(false), start(0.0), end(0.0), support(0), compared(0) {}
};

// Finds the intro or the outro of the current file: the longest part of its region (15 s to 4 min) that
// the same region of another episode also has. The part must be found in at least 2 other files, or in
// the only one there is (bNoMoreFiles: the caller has no other files to add).
// Files of very different length (other than 60%-160% of the current one) are not compared.
SkipAudioSegment FindSkipAudioSegment(const SkipAudioFingerprint& current,
                                      const std::vector<const SkipAudioFingerprint*>& others,
                                      bool bNoMoreFiles);

// Is a file of this duration comparable with the current file (seconds)?
bool IsSkipAudioComparable(double currentDuration, double otherDuration);

// The other files in the order they are compared: the nearest ones in the name order first,
// the next file before the previous one (count: all files of the folder, current: the index of the current one)
std::vector<size_t> GetSkipAudioNeighbourOrder(size_t count, size_t current);

// Gets the fingerprint of a region of the i-th file of the neighbour order (the file should not be
// decoded if its duration is outside [minDuration, maxDuration]). False if there is none.
typedef std::function<bool(size_t i, SkipAudioRegion region, double minDuration, double maxDuration, SkipAudioFingerprint& fp)> SkipAudioSource;

// Compares the current file with its neighbours until the intro and the outro are found by 2 of
// them, at most 4 comparable files and 8 files in all are read. pCurrentIntro / pCurrentOutro: the
// fingerprints of the regions of the current file to look at, nullptr if that one isn't needed.
void FindSkipAudioSegments(const SkipAudioFingerprint* pCurrentIntro, const SkipAudioFingerprint* pCurrentOutro,
                           size_t neighbourCount, const SkipAudioSource& source, const std::atomic<bool>* pCancel,
                           SkipAudioSegment& intro, SkipAudioSegment& outro);
