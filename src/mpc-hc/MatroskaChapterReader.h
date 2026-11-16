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

#include "SkipChapterStructure.h"

// True for the extensions of the Matroska family: mkv, mk3d, mka, webm
bool IsMatroskaFileName(LPCTSTR pszPath);

// Reads the duration and the start of the chapters of a Matroska file straight from its header
// (Info and Chapters elements, found through the SeekHead if they are behind the clusters),
// without opening the streams. Reads a few tens of kilobytes at most.
// Returns false if the file is not readable, not Matroska or has no chapters. The chapters of the
// first edition are used, hidden or not, only the top level ones.
bool ReadMatroskaChapterTable(LPCTSTR pszPath, SkipChapterTable& table);
