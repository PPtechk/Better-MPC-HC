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

#include "SkipSegmentProvider.h"
#include <atomic>
#include <functional>
#include <memory>

// The last resort: finds the intro and the outro by the audio the episode shares with the other
// episodes in its folder (see SkipAudioFingerprint.h). It only starts when the earlier providers are
// done and missed the intro or the outro, it works on a background thread with low CPU and disk
// priority, and remembers the fingerprints on disk, so a series is analysed only once.
class CAudioSkipSegmentProvider final : public ISkipSegmentProvider
{
public:
    // getCurrentPath returns the path of the file that is playing, an empty string if there is none
    explicit CAudioSkipSegmentProvider(std::function<CString()> getCurrentPath);
    ~CAudioSkipSegmentProvider();

    bool IsDirty() const override;
    bool IsPending() const override;
    void SetEarlierSegments(const std::vector<SkipSegment>& earlier, bool bFinal) override;
    void GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments) override;
    void Reset() override;

private:
    struct Job {
        std::atomic<bool> bCancel;
        std::atomic<bool> bDone;
        bool bIntro, bOutro;               // the parts to look for
        std::vector<SkipSegment> segments; // filled by the worker thread before bDone is set

        Job() : bCancel(false), bDone(false), bIntro(false), bOutro(false) {}
    };

    std::function<CString()> m_getCurrentPath;
    bool m_bEarlierFinal;
    bool m_bNeedIntro, m_bNeedOutro;

    std::shared_ptr<Job> m_pJob;
    CString m_strJobPath; // the file the job belongs to
    bool m_bJobConsumed;  // GetSkipSegments() already used the finished job

    static CString GetCacheFolder();
    static void Run(std::shared_ptr<Job> pJob, CString strPath, CString strDecoderFolder, CString strCacheFolder);
};
