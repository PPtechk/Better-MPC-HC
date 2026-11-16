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
#include "SkipChapterStructure.h"
#include <algorithm>
#include <cstdlib>
#include <map>

namespace
{
    const REFERENCE_TIME UNITS_PER_SECOND = 10000000;
    const REFERENCE_TIME MIN_CHAPTER_LENGTH = 60 * UNITS_PER_SECOND;
    const REFERENCE_TIME MAX_CHAPTER_LENGTH = 130 * UNITS_PER_SECOND;
    const REFERENCE_TIME MAX_LENGTH_DEVIATION = 20 * UNITS_PER_SECOND;
    const double INTRO_START_LIMIT = 0.4;   // an intro starts before this part of the file
    const double OUTRO_START_LIMIT = 0.5;   // an outro starts after this part of the file
    const double MIN_DURATION_RATIO = 0.6;  // files of very different length are not episodes of the same series
    const double MAX_DURATION_RATIO = 1.6;
    const size_t MIN_FILES = 3;
    const double MIN_SUPPORT = 0.6;

    REFERENCE_TIME ChapterStop(const SkipChapterTable& table, size_t i)
    {
        return (i + 1 < table.starts.size()) ? table.starts[i + 1] : table.rtDuration;
    }

    // The candidates of a file by chapter key: +n is the n-th chapter from the start,
    // -n is the n-th chapter from the end. The value is the length of the chapter.
    void GetCandidates(const SkipChapterTable& table, bool bIntro, std::map<int, REFERENCE_TIME>& candidates)
    {
        candidates.clear();

        const size_t n = table.starts.size();
        for (size_t i = 0; i < n; i++) {
            const REFERENCE_TIME length = ChapterStop(table, i) - table.starts[i];
            if (length < MIN_CHAPTER_LENGTH || length > MAX_CHAPTER_LENGTH) {
                continue;
            }

            const double position = double(table.starts[i]) / double(table.rtDuration);
            if (bIntro ? (position >= INTRO_START_LIMIT) : (position < OUTRO_START_LIMIT)) {
                continue;
            }

            candidates[int(i) + 1] = length;
            candidates[-int(n - i)] = length;
        }
    }

    bool IsComparable(const SkipChapterTable& current, const SkipChapterTable& other)
    {
        if (other.rtDuration <= 0 || other.starts.size() < 2) {
            return false;
        }

        const double ratio = double(other.rtDuration) / double(current.rtDuration);
        return ratio >= MIN_DURATION_RATIO && ratio <= MAX_DURATION_RATIO;
    }

    // Equal support: the key nearer to the file edge wins, then the key counted from the start for
    // an intro and the one counted from the end for an outro
    bool IsPreferred(int key, int other, bool bIntro)
    {
        if (!other) {
            return true;
        }
        if (std::abs(key) != std::abs(other)) {
            return std::abs(key) < std::abs(other);
        }
        return bIntro ? (key > 0) : (key < 0);
    }
}

std::vector<SkipStructuralSegment> FindStructuralSkipSegments(const SkipChapterTable& current,
                                                              const std::vector<SkipChapterTable>& siblings)
{
    std::vector<SkipStructuralSegment> result;

    if (current.rtDuration <= 0 || current.starts.size() < 2) {
        return result;
    }

    std::vector<const SkipChapterTable*> files;
    files.push_back(&current);
    for (const SkipChapterTable& sibling : siblings) {
        if (IsComparable(current, sibling)) {
            files.push_back(&sibling);
        }
    }
    if (files.size() < MIN_FILES) {
        return result;
    }

    for (int pass = 0; pass < 2; pass++) {
        const bool bIntro = (pass == 0);

        std::map<int, std::vector<REFERENCE_TIME>> votes;
        std::map<int, REFERENCE_TIME> candidates;
        for (const SkipChapterTable* pFile : files) {
            GetCandidates(*pFile, bIntro, candidates);
            for (const auto& candidate : candidates) {
                votes[candidate.first].push_back(candidate.second);
            }
        }

        int bestKey = 0;
        size_t bestCount = 0;
        for (const auto& vote : votes) {
            const size_t count = vote.second.size();
            if (count > bestCount || (count == bestCount && IsPreferred(vote.first, bestKey, bIntro))) {
                bestKey = vote.first;
                bestCount = count;
            }
        }
        if (!bestKey || double(bestCount) < MIN_SUPPORT * double(files.size())) {
            continue;
        }

        GetCandidates(current, bIntro, candidates);
        const auto it = candidates.find(bestKey);
        if (it == candidates.end()) {
            continue;
        }

        std::vector<REFERENCE_TIME>& lengths = votes[bestKey];
        std::sort(lengths.begin(), lengths.end());
        const REFERENCE_TIME median = lengths[lengths.size() / 2];
        if (it->second < median - MAX_LENGTH_DEVIATION || it->second > median + MAX_LENGTH_DEVIATION) {
            continue;
        }

        const size_t index = (bestKey > 0) ? size_t(bestKey - 1) : size_t(int(current.starts.size()) + bestKey);

        SkipStructuralSegment segment;
        segment.rtStart = current.starts[index];
        segment.rtStop = ChapterStop(current, index);
        segment.bIntro = bIntro;
        result.push_back(segment);
    }

    return result;
}
