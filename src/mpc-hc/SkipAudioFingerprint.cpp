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

#include "stdafx.h"
#include "SkipAudioFingerprint.h"
#include <algorithm>
#include <cmath>

namespace
{
    const double PI = 3.14159265358979323846;

    const int BAND_COUNT = 33;
    const double BAND_LOW = 300.0;
    const double BAND_HIGH = 3000.0;
    const double SILENCE_RMS = 0.001; // -60 dBFS

    const double INTRO_PART = 0.4;
    const double INTRO_MAX = 600.0;
    const double OUTRO_PART = 0.4;
    const double OUTRO_MAX = 360.0;

    const double MIN_SEGMENT = 15.0;
    const double MAX_SEGMENT = 240.0;
    const double MAX_GAP = 2.0;       // seconds without a matching frame inside a common part
    const int MAX_BIT_ERRORS = 10;    // of 32: the frame matches
    const double MIN_DENSITY = 0.5;   // matching frames in a common part
    const double END_TOLERANCE = 6.0; // two common parts agree if their ends are this close (seconds)

    const double MIN_DURATION_RATIO = 0.6;
    const double MAX_DURATION_RATIO = 1.6;

    const size_t MAX_COMPARED_FILES = 4;
    const size_t MAX_READ_FILES = 8;

    int SecondsToFrames(double seconds)
    {
        return int(seconds * SKIP_AUDIO_SAMPLE_RATE / SKIP_AUDIO_HOP + 0.5);
    }

    int BitErrors(uint32_t v)
    {
        v = v - ((v >> 1) & 0x55555555u);
        v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
        return int((((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
    }

    struct Run {
        int start, end; // frames of the first file, inclusive
        int matched;

        Run() : start(0), end(-1), matched(0) {}
        int Length() const { return end - start + 1; }
    };

    // The longest part of a that b also has, at any shift between them
    bool FindLongestCommonRun(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, Run& best)
    {
        const int na = int(a.size()), nb = int(b.size());
        const int minFrames = SecondsToFrames(MIN_SEGMENT);
        const int maxGap = SecondsToFrames(MAX_GAP);

        best = Run();
        int bestLength = minFrames - 1;

        for (int shift = -(na - 1); shift <= nb - 1; shift++) {
            const int first = std::max(0, -shift);
            const int last = std::min(na, nb - shift); // exclusive
            if (last - first <= bestLength) {
                continue;
            }

            int runStart = -1, runEnd = -1, matched = 0;
            auto closeRun = [&]() {
                const int length = runEnd - runStart + 1;
                if (length > bestLength && matched >= MIN_DENSITY * length) {
                    best.start = runStart;
                    best.end = runEnd;
                    best.matched = matched;
                    bestLength = length;
                }
                runStart = -1;
            };

            const uint32_t* pA = a.data();
            const uint32_t* pB = b.data() + shift;
            for (int i = first; i < last; i++) {
                const uint32_t x = pA[i], y = pB[i];
                if (!x || !y || BitErrors(x ^ y) > MAX_BIT_ERRORS) {
                    if (runStart >= 0 && i - runEnd > maxGap) {
                        closeRun();
                    }
                    continue;
                }
                if (runStart < 0) {
                    runStart = i;
                    matched = 0;
                }
                runEnd = i;
                matched++;
            }
            if (runStart >= 0) {
                closeRun();
            }
        }

        return best.end >= best.start;
    }

    double Median(std::vector<double> values)
    {
        std::sort(values.begin(), values.end());
        const size_t n = values.size();
        return (n % 2) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
    }
}

void GetSkipAudioRegion(double duration, SkipAudioRegion region, double& start, double& end)
{
    if (region == SKIP_AUDIO_INTRO) {
        start = 0.0;
        end = std::min(duration * INTRO_PART, INTRO_MAX);
    } else {
        start = std::max(duration * (1.0 - OUTRO_PART), duration - OUTRO_MAX);
        end = duration;
    }
}

bool IsSkipAudioComparable(double currentDuration, double otherDuration)
{
    if (currentDuration <= 0.0 || otherDuration <= 0.0) {
        return false;
    }
    const double ratio = otherDuration / currentDuration;
    return ratio >= MIN_DURATION_RATIO && ratio <= MAX_DURATION_RATIO;
}

CSkipAudioFingerprinter::CSkipAudioFingerprinter()
    : m_window(SKIP_AUDIO_FRAME)
    , m_bitReverse(SKIP_AUDIO_FRAME)
    , m_cos(SKIP_AUDIO_FRAME / 2)
    , m_sin(SKIP_AUDIO_FRAME / 2)
    , m_pendingPos(0)
    , m_samples(0)
    , m_re(SKIP_AUDIO_FRAME)
    , m_im(SKIP_AUDIO_FRAME)
    , m_energy(BAND_COUNT)
    , m_prevEnergy(BAND_COUNT)
    , m_bHavePrev(false)
    , m_bPrevSilent(true)
{
    const int n = SKIP_AUDIO_FRAME;
    for (int i = 0; i < n; i++) {
        m_window[i] = float(0.5 - 0.5 * cos(2.0 * PI * i / (n - 1)));
    }

    int bits = 0;
    while ((1 << bits) < n) {
        bits++;
    }
    for (int i = 0; i < n; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) {
            r |= ((i >> b) & 1) << (bits - 1 - b);
        }
        m_bitReverse[i] = r;
    }
    for (int i = 0; i < n / 2; i++) {
        m_cos[i] = cos(2.0 * PI * i / n);
        m_sin[i] = -sin(2.0 * PI * i / n);
    }

    m_bandStart.resize(BAND_COUNT + 1);
    int previous = 0;
    for (int band = 0; band <= BAND_COUNT; band++) {
        const double frequency = BAND_LOW * pow(BAND_HIGH / BAND_LOW, double(band) / BAND_COUNT);
        int bin = int(ceil(frequency * n / SKIP_AUDIO_SAMPLE_RATE));
        if (band > 0 && bin <= previous) {
            bin = previous + 1; // every band has at least one bin
        }
        m_bandStart[band] = bin;
        previous = bin;
    }
}

void CSkipAudioFingerprinter::AddSamples(const float* pSamples, size_t count)
{
    m_pending.insert(m_pending.end(), pSamples, pSamples + count);
    m_samples += count;

    while (m_pending.size() - m_pendingPos >= size_t(SKIP_AUDIO_FRAME)) {
        ProcessFrame(m_pending.data() + m_pendingPos);
        m_pendingPos += SKIP_AUDIO_HOP;
    }

    if (m_pendingPos >= 64 * SKIP_AUDIO_FRAME) {
        m_pending.erase(m_pending.begin(), m_pending.begin() + m_pendingPos);
        m_pendingPos = 0;
    }
}

void CSkipAudioFingerprinter::AddSilence(size_t count)
{
    const std::vector<float> silence(std::min<size_t>(count, 65536));
    while (count > 0) {
        const size_t n = std::min(count, silence.size());
        AddSamples(silence.data(), n);
        count -= n;
    }
}

void CSkipAudioFingerprinter::ProcessFrame(const float* pFrame)
{
    const int n = SKIP_AUDIO_FRAME;

    double sumSquares = 0.0;
    for (int i = 0; i < n; i++) {
        sumSquares += double(pFrame[i]) * pFrame[i];
        const int j = m_bitReverse[i];
        m_re[j] = double(pFrame[i]) * m_window[i];
        m_im[j] = 0.0;
    }
    const bool bSilent = sqrt(sumSquares / n) < SILENCE_RMS;

    // iterative radix-2 FFT
    for (int size = 2; size <= n; size <<= 1) {
        const int half = size >> 1;
        const int step = n / size;
        for (int start = 0; start < n; start += size) {
            for (int k = 0; k < half; k++) {
                const double wr = m_cos[k * step], wi = m_sin[k * step];
                const int even = start + k, odd = even + half;
                const double tr = m_re[odd] * wr - m_im[odd] * wi;
                const double ti = m_re[odd] * wi + m_im[odd] * wr;
                m_re[odd] = m_re[even] - tr;
                m_im[odd] = m_im[even] - ti;
                m_re[even] += tr;
                m_im[even] += ti;
            }
        }
    }

    for (int band = 0; band < BAND_COUNT; band++) {
        double energy = 0.0;
        for (int bin = m_bandStart[band]; bin < m_bandStart[band + 1]; bin++) {
            energy += m_re[bin] * m_re[bin] + m_im[bin] * m_im[bin];
        }
        m_energy[band] = energy;
    }

    uint32_t hash = 0;
    if (m_bHavePrev && !bSilent && !m_bPrevSilent) {
        for (int bit = 0; bit < BAND_COUNT - 1; bit++) {
            const double difference = (m_energy[bit] - m_energy[bit + 1]) - (m_prevEnergy[bit] - m_prevEnergy[bit + 1]);
            if (difference > 0.0) {
                hash |= 1u << bit;
            }
        }
        if (!hash) {
            hash = 1; // 0 means silence
        }
    }
    m_hashes.push_back(hash);

    m_prevEnergy.swap(m_energy);
    m_bHavePrev = true;
    m_bPrevSilent = bSilent;
}

SkipAudioSegment FindSkipAudioSegment(const SkipAudioFingerprint& current,
                                      const std::vector<const SkipAudioFingerprint*>& others,
                                      bool bNoMoreFiles)
{
    SkipAudioSegment result;

    const double hopTime = double(SKIP_AUDIO_HOP) / SKIP_AUDIO_SAMPLE_RATE;

    struct Candidate {
        double start, end;
    };
    std::vector<Candidate> candidates;

    for (const SkipAudioFingerprint* pOther : others) {
        if (!pOther || pOther->hashes.empty() || !IsSkipAudioComparable(current.duration, pOther->duration)) {
            continue;
        }
        result.compared++;

        Run run;
        if (!FindLongestCommonRun(current.hashes, pOther->hashes, run)) {
            continue;
        }

        const double start = current.FrameCenter(run.start) - hopTime / 2;
        const double end = current.FrameCenter(run.end) + hopTime / 2;
        if (end - start >= MIN_SEGMENT && end - start <= MAX_SEGMENT) {
            candidates.push_back({ std::max(0.0, start), std::min(current.duration, end) });
        }
    }

    // The end is what matters, it is where the skip goes. The files that agree on the end (within
    // END_TOLERANCE) win, the most of them, the later end if equally many. Some episodes share a few
    // more seconds after the song (the same title card), so the earliest end of the group is used.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate & a, const Candidate & b) {
        return a.end < b.end;
    });
    size_t bestFirst = 0, bestCount = 0;
    for (size_t first = 0; first < candidates.size(); first++) {
        size_t count = 0;
        while (first + count < candidates.size() && candidates[first + count].end - candidates[first].end <= END_TOLERANCE) {
            count++;
        }
        if (count >= bestCount) {
            bestFirst = first;
            bestCount = count;
        }
    }

    result.support = int(bestCount);
    if (bestCount > 0) {
        std::vector<double> starts;
        for (size_t i = bestFirst; i < bestFirst + bestCount; i++) {
            starts.push_back(candidates[i].start);
        }
        result.start = Median(starts);
        result.end = candidates[bestFirst].end;
        result.bFound = bestCount >= 2 || (bNoMoreFiles && result.compared == 1);
    }
    return result;
}

std::vector<size_t> GetSkipAudioNeighbourOrder(size_t count, size_t current)
{
    std::vector<size_t> order;
    for (size_t distance = 1; distance < count; distance++) {
        if (current + distance < count) {
            order.push_back(current + distance);
        }
        if (current >= distance) {
            order.push_back(current - distance);
        }
    }
    return order;
}

void FindSkipAudioSegments(const SkipAudioFingerprint* pCurrentIntro, const SkipAudioFingerprint* pCurrentOutro,
                           size_t neighbourCount, const SkipAudioSource& source, const std::atomic<bool>* pCancel,
                           SkipAudioSegment& intro, SkipAudioSegment& outro)
{
    intro = SkipAudioSegment();
    outro = SkipAudioSegment();

    const SkipAudioFingerprint* pCurrent[2] = { pCurrentIntro, pCurrentOutro };
    const SkipAudioRegion regions[2] = { SKIP_AUDIO_INTRO, SKIP_AUDIO_OUTRO };
    SkipAudioSegment* pResult[2] = { &intro, &outro };
    std::vector<SkipAudioFingerprint> others[2];
    bool bDecided[2];
    for (int r = 0; r < 2; r++) {
        bDecided[r] = !pCurrent[r] || pCurrent[r]->hashes.empty();
    }

    auto pointers = [](const std::vector<SkipAudioFingerprint>& fps) {
        std::vector<const SkipAudioFingerprint*> result;
        for (const SkipAudioFingerprint& fp : fps) {
            result.push_back(&fp);
        }
        return result;
    };

    for (size_t i = 0; i < neighbourCount && i < MAX_READ_FILES && !(bDecided[0] && bDecided[1]); i++) {
        for (int r = 0; r < 2; r++) {
            if (bDecided[r] || (pCancel && *pCancel)) {
                continue;
            }

            const double duration = pCurrent[r]->duration;
            SkipAudioFingerprint fp;
            if (source(i, regions[r], duration * MIN_DURATION_RATIO, duration * MAX_DURATION_RATIO, fp)
                    && !fp.hashes.empty() && IsSkipAudioComparable(duration, fp.duration)) {
                others[r].push_back(std::move(fp));
            }

            if (others[r].size() >= 2) {
                const SkipAudioSegment segment = FindSkipAudioSegment(*pCurrent[r], pointers(others[r]), false);
                if (segment.bFound || others[r].size() >= MAX_COMPARED_FILES) {
                    *pResult[r] = segment;
                    bDecided[r] = true;
                }
            }
        }
        if (pCancel && *pCancel) {
            return;
        }
    }

    for (int r = 0; r < 2; r++) {
        if (!bDecided[r]) {
            *pResult[r] = FindSkipAudioSegment(*pCurrent[r], pointers(others[r]), true);
        }
    }
}
