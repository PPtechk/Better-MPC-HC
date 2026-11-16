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

// No precompiled header: the FFmpeg headers are kept away from MFC
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "SkipAudioDecoder.h"
#include <algorithm>
#include <mutex>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
extern "C" {
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavresample/avresample.h"
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace
{
#define SKIP_AV_FUNCTIONS(X)                     \
    X(avutil, av_frame_alloc)                    \
    X(avutil, av_frame_free)                     \
    X(avutil, av_opt_set_int)                    \
    X(avutil, av_get_default_channel_layout)     \
    X(avutil, av_get_channel_layout_nb_channels) \
    X(avresample, avresample_alloc_context)      \
    X(avresample, avresample_open)               \
    X(avresample, avresample_convert)            \
    X(avresample, avresample_available)          \
    X(avresample, avresample_read)               \
    X(avresample, avresample_free)               \
    X(avcodec, avcodec_find_decoder)             \
    X(avcodec, avcodec_alloc_context3)           \
    X(avcodec, avcodec_parameters_to_context)    \
    X(avcodec, avcodec_open2)                    \
    X(avcodec, avcodec_send_packet)              \
    X(avcodec, avcodec_receive_frame)            \
    X(avcodec, avcodec_free_context)             \
    X(avcodec, av_init_packet)                   \
    X(avcodec, av_packet_unref)                  \
    X(avformat, av_register_all)                 \
    X(avformat, avformat_open_input)             \
    X(avformat, avformat_find_stream_info)       \
    X(avformat, av_find_best_stream)             \
    X(avformat, av_read_frame)                   \
    X(avformat, av_seek_frame)                   \
    X(avformat, avformat_close_input)

    struct FFmpegApi {
#define SKIP_AV_DECLARE(lib, name) decltype(&::name) name;
        SKIP_AV_FUNCTIONS(SKIP_AV_DECLARE)
#undef SKIP_AV_DECLARE
    };

    FFmpegApi g_api;
    std::atomic<int> g_state(0); // 0: not loaded yet, 1: loaded, -1: failed

    const double GAP_TOLERANCE = 0.25; // seconds of missing or overlapping audio that are ignored
    const int64_t NO_TIMESTAMP = int64_t(AV_NOPTS_VALUE);

    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty()) {
            return std::string();
        }
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size_t(std::max(length, 0)), '\0');
        if (length > 0) {
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), &result[0], length, nullptr, nullptr);
        }
        return result;
    }

    HMODULE LoadLibraryFrom(const std::wstring& folder, const wchar_t* name, int major)
    {
        const std::wstring path = folder + name + std::to_wstring(major) + L".dll";
        return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }

    struct DecoderResources {
        AVFormatContext* pFormat;
        AVCodecContext* pCodec;
        AVFrame* pFrame;
        AVAudioResampleContext* pResample;

        DecoderResources() : pFormat(nullptr), pCodec(nullptr), pFrame(nullptr), pResample(nullptr) {}
        ~DecoderResources() {
            if (pResample) {
                g_api.avresample_free(&pResample);
            }
            if (pFrame) {
                g_api.av_frame_free(&pFrame);
            }
            if (pCodec) {
                g_api.avcodec_free_context(&pCodec);
            }
            if (pFormat) {
                g_api.avformat_close_input(&pFormat);
            }
        }
        DecoderResources(const DecoderResources&) = delete;
        DecoderResources& operator=(const DecoderResources&) = delete;
    };
}

bool LoadSkipAudioDecoder(const std::wstring& folderPath)
{
    static std::mutex s_lock;
    std::lock_guard<std::mutex> lock(s_lock);
    if (g_state) {
        return g_state > 0;
    }
    g_state = -1;

    std::wstring folder = folderPath;
    if (!folder.empty() && folder.back() != L'\\' && folder.back() != L'/') {
        folder += L'\\';
    }

    // The dependencies (zlib, iconv, winpthread) are next to them, LOAD_WITH_ALTERED_SEARCH_PATH finds them
    HMODULE avutil = LoadLibraryFrom(folder, L"avutil-lav-", LIBAVUTIL_VERSION_MAJOR);
    HMODULE avresample = LoadLibraryFrom(folder, L"avresample-lav-", LIBAVRESAMPLE_VERSION_MAJOR);
    HMODULE avcodec = LoadLibraryFrom(folder, L"avcodec-lav-", LIBAVCODEC_VERSION_MAJOR);
    HMODULE avformat = LoadLibraryFrom(folder, L"avformat-lav-", LIBAVFORMAT_VERSION_MAJOR);
    if (!avutil || !avresample || !avcodec || !avformat) {
        return false; // the loaded libraries stay loaded, they may be used by LAV Filters too
    }

#define SKIP_AV_RESOLVE(lib, name)                                                   \
    g_api.name = reinterpret_cast<decltype(g_api.name)>(GetProcAddress(lib, #name)); \
    if (!g_api.name) {                                                               \
        return false;                                                                \
    }
    SKIP_AV_FUNCTIONS(SKIP_AV_RESOLVE)
#undef SKIP_AV_RESOLVE

    g_api.av_register_all();
    g_state = 1;
    return true;
}

SkipAudioResult ComputeSkipAudioFingerprint(const std::wstring& path, SkipAudioRegion region,
                                            double minDuration, double maxDuration,
                                            const std::atomic<bool>* pCancel, SkipAudioFingerprint& fp)
{
    fp = SkipAudioFingerprint();
    if (g_state != 1) {
        return SKIP_AUDIO_FAILED;
    }

    DecoderResources res;
    if (g_api.avformat_open_input(&res.pFormat, ToUtf8(path).c_str(), nullptr, nullptr) < 0) {
        res.pFormat = nullptr;
        return SKIP_AUDIO_FAILED;
    }
    AVFormatContext* pFormat = res.pFormat;

    // The headers of Matroska and MP4 describe the streams, the others need a look at the data
    int iStream = g_api.av_find_best_stream(pFormat, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (iStream < 0 || pFormat->streams[iStream]->codecpar->sample_rate <= 0
            || pFormat->streams[iStream]->codecpar->channels <= 0 || pFormat->duration <= 0) {
        if (g_api.avformat_find_stream_info(pFormat, nullptr) < 0) {
            return SKIP_AUDIO_FAILED;
        }
        iStream = g_api.av_find_best_stream(pFormat, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    }

    double duration = (pFormat->duration > 0) ? double(pFormat->duration) / AV_TIME_BASE : 0.0;
    if (duration <= 0.0 && iStream >= 0 && pFormat->streams[iStream]->duration > 0) {
        duration = pFormat->streams[iStream]->duration * av_q2d(pFormat->streams[iStream]->time_base);
    }
    if (duration <= 0.0) {
        return SKIP_AUDIO_FAILED;
    }
    fp.duration = duration;

    if ((minDuration > 0.0 && duration < minDuration) || (maxDuration > 0.0 && duration > maxDuration)) {
        return SKIP_AUDIO_OUT_OF_RANGE;
    }
    if (iStream < 0) {
        return SKIP_AUDIO_NO_AUDIO;
    }

    AVStream* pStream = pFormat->streams[iStream];
    for (unsigned i = 0; i < pFormat->nb_streams; i++) {
        pFormat->streams[i]->discard = (int(i) == iStream) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    AVCodec* pDecoder = g_api.avcodec_find_decoder(pStream->codecpar->codec_id);
    if (!pDecoder) {
        return SKIP_AUDIO_NO_AUDIO;
    }
    res.pCodec = g_api.avcodec_alloc_context3(pDecoder);
    res.pFrame = g_api.av_frame_alloc();
    if (!res.pCodec || !res.pFrame || g_api.avcodec_parameters_to_context(res.pCodec, pStream->codecpar) < 0) {
        return SKIP_AUDIO_FAILED;
    }
    res.pCodec->pkt_timebase = pStream->time_base;
    res.pCodec->thread_count = 1;
    if (g_api.avcodec_open2(res.pCodec, pDecoder, nullptr) < 0) {
        return SKIP_AUDIO_NO_AUDIO;
    }

    double regionStart, regionEnd;
    GetSkipAudioRegion(duration, region, regionStart, regionEnd);

    // The player counts the time from the start of the file
    const double zeroTime = (pFormat->start_time != NO_TIMESTAMP) ? double(pFormat->start_time) / AV_TIME_BASE : 0.0;
    const double timeBase = av_q2d(pStream->time_base);

    if (regionStart > 1.0) {
        // If the seek fails, the file is decoded from the start, only slower
        const int64_t target = int64_t((std::max(0.0, regionStart - 3.0) + zeroTime) * AV_TIME_BASE);
        g_api.av_seek_frame(pFormat, -1, target, AVSEEK_FLAG_BACKWARD);
    }

    CSkipAudioFingerprinter fingerprinter;
    std::vector<float> output;
    uint64_t resampleLayout = 0;
    int resampleRate = 0, resampleFormat = -1;
    bool bStarted = false, bDone = false, bFailed = false;

    auto addFrame = [&](AVFrame * pFrame) {
        if (pFrame->nb_samples <= 0 || pFrame->sample_rate <= 0 || pFrame->channels <= 0) {
            return;
        }

        int64_t timestamp = pFrame->best_effort_timestamp;
        if (timestamp == NO_TIMESTAMP) {
            timestamp = pFrame->pts;
        }
        const double expected = fp.startTime + double(fingerprinter.GetSampleCount()) / SKIP_AUDIO_SAMPLE_RATE;
        double time;
        if (timestamp != NO_TIMESTAMP) {
            time = timestamp * timeBase - zeroTime;
        } else if (bStarted) {
            time = expected;
        } else if (regionStart <= 1.0) {
            time = 0.0;
        } else {
            bFailed = true; // after a seek the position must be known
            return;
        }

        if (time >= regionEnd) {
            bDone = true;
            return;
        }
        const double frameDuration = double(pFrame->nb_samples) / pFrame->sample_rate;
        if (!bStarted) {
            if (time + frameDuration <= regionStart) {
                return;
            }
            bStarted = true;
            fp.startTime = time;
        } else if (time > expected + GAP_TOLERANCE) {
            fingerprinter.AddSilence(size_t((time - expected) * SKIP_AUDIO_SAMPLE_RATE));
        } else if (time + frameDuration < expected - GAP_TOLERANCE) {
            return; // audio that was already used
        }

        uint64_t layout = pFrame->channel_layout;
        if (!layout || g_api.av_get_channel_layout_nb_channels(layout) != pFrame->channels) {
            layout = uint64_t(g_api.av_get_default_channel_layout(pFrame->channels));
        }
        if (!res.pResample || layout != resampleLayout || pFrame->sample_rate != resampleRate || pFrame->format != resampleFormat) {
            if (res.pResample) {
                g_api.avresample_free(&res.pResample);
            }
            res.pResample = g_api.avresample_alloc_context();
            if (!res.pResample) {
                bFailed = true;
                return;
            }
            g_api.av_opt_set_int(res.pResample, "in_channel_layout", int64_t(layout), 0);
            g_api.av_opt_set_int(res.pResample, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
            g_api.av_opt_set_int(res.pResample, "in_sample_rate", pFrame->sample_rate, 0);
            g_api.av_opt_set_int(res.pResample, "out_sample_rate", SKIP_AUDIO_SAMPLE_RATE, 0);
            g_api.av_opt_set_int(res.pResample, "in_sample_fmt", pFrame->format, 0);
            g_api.av_opt_set_int(res.pResample, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
            if (g_api.avresample_open(res.pResample) < 0) {
                bFailed = true;
                return;
            }
            resampleLayout = layout;
            resampleRate = pFrame->sample_rate;
            resampleFormat = pFrame->format;
        }

        const int maxOutput = int(int64_t(pFrame->nb_samples) * SKIP_AUDIO_SAMPLE_RATE / pFrame->sample_rate) + 256;
        output.resize(size_t(maxOutput));
        uint8_t* pOutput = reinterpret_cast<uint8_t*>(output.data());
        const int converted = g_api.avresample_convert(res.pResample, &pOutput, maxOutput * int(sizeof(float)), maxOutput,
                                                       pFrame->extended_data, pFrame->linesize[0], pFrame->nb_samples);
        if (converted < 0) {
            bFailed = true;
            return;
        }
        fingerprinter.AddSamples(output.data(), size_t(converted));
        while (g_api.avresample_available(res.pResample) > 0) {
            const int read = g_api.avresample_read(res.pResample, &pOutput, maxOutput);
            if (read <= 0) {
                break;
            }
            fingerprinter.AddSamples(output.data(), size_t(read));
        }
    };

    AVPacket packet;
    g_api.av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    bool bEndOfFile = false;
    while (!bDone && !bFailed && !bEndOfFile) {
        if (pCancel && *pCancel) {
            return SKIP_AUDIO_CANCELLED;
        }

        if (g_api.av_read_frame(pFormat, &packet) < 0) {
            bEndOfFile = true;
            g_api.avcodec_send_packet(res.pCodec, nullptr); // drain the decoder
        } else if (packet.stream_index != iStream || g_api.avcodec_send_packet(res.pCodec, &packet) < 0) {
            g_api.av_packet_unref(&packet); // other stream or broken packet
            continue;
        }

        while (!bDone && !bFailed && g_api.avcodec_receive_frame(res.pCodec, res.pFrame) == 0) {
            addFrame(res.pFrame);
        }
        if (!bEndOfFile) {
            g_api.av_packet_unref(&packet);
        }
    }

    if (bFailed) {
        return SKIP_AUDIO_FAILED;
    }
    if (!bStarted) {
        return SKIP_AUDIO_NO_AUDIO;
    }

    fp.hashes.swap(fingerprinter.GetHashes());
    return SKIP_AUDIO_OK;
}
