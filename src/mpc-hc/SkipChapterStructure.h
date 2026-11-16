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

#include <vector>

// The chapter layout of a media file: the start of every chapter and the duration of the file
// (100 ns units). The chapter names are not needed.
struct SkipChapterTable {
    REFERENCE_TIME rtDuration;
    std::vector<REFERENCE_TIME> starts; // ascending

    SkipChapterTable() : rtDuration(0) {}
};

struct SkipStructuralSegment {
    REFERENCE_TIME rtStart;
    REFERENCE_TIME rtStop;
    bool bIntro; // false: outro
};

// Finds the intro and the outro of a file when the chapters have no useful names ("Chapter 2").
//
// The opening and the ending of a series are chapters of nearly the same length (about 1.5 minutes)
// at the same place of the chapter list in most of its episodes. The chapter layout of the other
// episodes in the same folder tells which chapter that is:
//  - a chapter is a candidate if it lasts 60-130 s and lies in the first 40% (intro) or in the last
//    50% (outro) of the file;
//  - every candidate is counted by its position in the chapter list, from the start and from the end;
//  - the position offered by at least 60% of the episodes wins, if the chapter of the current file
//    on that position is about as long as the median of the others.
// Nothing is returned if there are less than 3 comparable files or no position is clear enough.
std::vector<SkipStructuralSegment> FindStructuralSkipSegments(const SkipChapterTable& current,
                                                              const std::vector<SkipChapterTable>& siblings);
