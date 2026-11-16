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

#include "SkipAudioFingerprint.h"
#include <atomic>
#include <string>

// Decodes audio with the FFmpeg libraries of the internal LAV Filters (avformat, avcodec, avresample,
// avutil), loaded at run time from the LAV Filters folder of the player. Only the audio stream is
// decoded, the other streams are skipped by the demuxer.

// Loads the libraries from the folder, once per process. False if they are missing or don't match the
// FFmpeg headers this file was built with (the file names carry the major versions).
bool LoadSkipAudioDecoder(const std::wstring& folder);

enum SkipAudioResult {
    SKIP_AUDIO_OK,
    SKIP_AUDIO_NO_AUDIO,     // no audio stream or no decoder for it: worth remembering
    SKIP_AUDIO_OUT_OF_RANGE, // the duration is outside the requested range: only fp.duration is set
    SKIP_AUDIO_FAILED,       // the file can't be read (or the libraries aren't loaded)
    SKIP_AUDIO_CANCELLED,
};

// Decodes the region of the file (see GetSkipAudioRegion) and computes its fingerprint.
// minDuration / maxDuration (seconds, 0: no limit): files of other durations are not decoded.
SkipAudioResult ComputeSkipAudioFingerprint(const std::wstring& path, SkipAudioRegion region,
                                            double minDuration, double maxDuration,
                                            const std::atomic<bool>* pCancel, SkipAudioFingerprint& fp);
