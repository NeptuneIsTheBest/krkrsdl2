/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"

#include "DebugIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "WaveIntf.h"

#include <AudioToolbox/AudioToolbox.h>

extern "C" {
#include "opusfile.h"
}

#include <algorithm>
#include <alloca.h>
#include <assert.h>
#include <cmath>
#include <limits>
#include <math.h>
#include <memory>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>

/*
 * FAudio already vendors stb_vorbis 1.20. Compile that pinned copy here as a
 * built-in decoder. The FAudio copy expects these two allocation hooks because
 * its normal translation unit supplies them from FAudio_internal.h.
 */
#define STB_VORBIS_NO_PUSHDATA_API 1
#ifndef FAUDIOAPI
#define KRKRSDL3_DEFINED_FAUDIOAPI 1
#define FAUDIOAPI
#endif
#define FAudio_alloca(size) alloca(size)
#define FAudio_dealloca(ptr) ((void)(ptr))
#include "FAudio/src/stb_vorbis.h"
#undef FAudio_dealloca
#undef FAudio_alloca
#ifdef KRKRSDL3_DEFINED_FAUDIOAPI
#undef FAUDIOAPI
#undef KRKRSDL3_DEFINED_FAUDIOAPI
#endif
#undef STB_VORBIS_NO_PUSHDATA_API

namespace
{

bool TVPAddStreamOffset(tjs_uint64 base, tjs_int64 offset, tjs_uint64 &result)
{
	if(offset >= 0)
	{
		const tjs_uint64 positive = static_cast<tjs_uint64>(offset);
		if(base > std::numeric_limits<tjs_uint64>::max() - positive) return false;
		result = base + positive;
		return true;
	}

	const tjs_uint64 magnitude = static_cast<tjs_uint64>(-(offset + 1)) + 1;
	if(base < magnitude) return false;
	result = base - magnitude;
	return true;
}

bool TVPSeekStream(tTJSBinaryStream *stream, tjs_int64 offset, int whence,
	tjs_uint64 *newposition = nullptr)
{
	if(!stream) return false;

	try
	{
		tjs_uint64 base;
		switch(whence)
		{
		case SEEK_SET:
			base = 0;
			break;
		case SEEK_CUR:
			base = stream->GetPosition();
			break;
		case SEEK_END:
			base = stream->GetSize();
			break;
		default:
			return false;
		}

		tjs_uint64 target;
		if(!TVPAddStreamOffset(base, offset, target)) return false;
		if(target > static_cast<tjs_uint64>(std::numeric_limits<tjs_int64>::max()))
			return false;
		const tjs_uint64 actual = stream->Seek(static_cast<tjs_int64>(target), TJS_BS_SEEK_SET);
		if(actual != target) return false;
		if(newposition) *newposition = actual;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

tjs_uint64 TVPCalculateTotalTime(tjs_uint64 samples, tjs_uint samplerate)
{
	if(samples == 0 || samplerate == 0) return 0;
	return static_cast<tjs_uint64>((static_cast<long double>(samples) * 1000.0L) /
		static_cast<long double>(samplerate));
}

void TVPLogCodecError(const tjs_char *codec, const tjs_char *operation,
	const ttstr &storagename, tjs_int error)
{
	TVPAddLog(ttstr(codec) + TJS_W(": ") + operation + TJS_W(" failed for '") +
		storagename + TJS_W("' (error ") + ttstr(error) + TJS_W(")."));
}

// -------------------------------------------------------------------------
// AudioToolbox decoder
// -------------------------------------------------------------------------

class tTVPWD_AudioToolbox final : public tTVPWaveDecoder
{
	std::unique_ptr<tTJSBinaryStream> Stream;
	AudioFileID AudioFile;
	ExtAudioFileRef ExtendedFile;
	tTVPWaveFormat Format;
	ttstr StorageName;
	tjs_uint FrameBytes;

public:
	tTVPWD_AudioToolbox(std::unique_ptr<tTJSBinaryStream> stream,
		const ttstr &storagename)
		: Stream(std::move(stream)), AudioFile(nullptr), ExtendedFile(nullptr),
		StorageName(storagename), FrameBytes(0)
	{
		memset(&Format, 0, sizeof(Format));
	}

	~tTVPWD_AudioToolbox() override
	{
		if(ExtendedFile) ExtAudioFileDispose(ExtendedFile);
		if(AudioFile) AudioFileClose(AudioFile);
	}

	bool Open(AudioFileTypeID typehint)
	{
		OSStatus status = AudioFileOpenWithCallbacks(this, ReadCallback, nullptr,
			GetSizeCallback, nullptr, typehint, &AudioFile);
		if(status != noErr)
		{
			TVPLogCodecError(TJS_W("AudioToolbox"), TJS_W("open"), StorageName,
				static_cast<tjs_int>(status));
			return false;
		}

		status = ExtAudioFileWrapAudioFileID(AudioFile, false, &ExtendedFile);
		if(status != noErr)
		{
			TVPLogCodecError(TJS_W("AudioToolbox"), TJS_W("wrap"), StorageName,
				static_cast<tjs_int>(status));
			return false;
		}

		AudioStreamBasicDescription fileformat;
		memset(&fileformat, 0, sizeof(fileformat));
		UInt32 size = sizeof(fileformat);
		status = ExtAudioFileGetProperty(ExtendedFile,
			kExtAudioFileProperty_FileDataFormat, &size, &fileformat);
		if(status != noErr || !std::isfinite(fileformat.mSampleRate) ||
			fileformat.mSampleRate <= 0.0 ||
			fileformat.mSampleRate > static_cast<double>(std::numeric_limits<tjs_uint>::max()) ||
			fileformat.mChannelsPerFrame == 0 ||
			fileformat.mChannelsPerFrame >
				std::numeric_limits<UInt32>::max() / sizeof(tjs_int16))
		{
			TVPLogCodecError(TJS_W("AudioToolbox"), TJS_W("read format"), StorageName,
				static_cast<tjs_int>(status));
			return false;
		}

		AudioStreamBasicDescription clientformat;
		memset(&clientformat, 0, sizeof(clientformat));
		clientformat.mSampleRate = fileformat.mSampleRate;
		clientformat.mFormatID = kAudioFormatLinearPCM;
		clientformat.mFormatFlags = kAudioFormatFlagIsSignedInteger |
			kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
		clientformat.mFramesPerPacket = 1;
		clientformat.mChannelsPerFrame = fileformat.mChannelsPerFrame;
		clientformat.mBitsPerChannel = 16;
		clientformat.mBytesPerFrame = fileformat.mChannelsPerFrame * sizeof(tjs_int16);
		clientformat.mBytesPerPacket = clientformat.mBytesPerFrame;

		status = ExtAudioFileSetProperty(ExtendedFile,
			kExtAudioFileProperty_ClientDataFormat, sizeof(clientformat), &clientformat);
		if(status != noErr)
		{
			TVPLogCodecError(TJS_W("AudioToolbox"), TJS_W("set PCM format"), StorageName,
				static_cast<tjs_int>(status));
			return false;
		}

		SInt64 totalframes = 0;
		size = sizeof(totalframes);
		status = ExtAudioFileGetProperty(ExtendedFile,
			kExtAudioFileProperty_FileLengthFrames, &size, &totalframes);
		if(status != noErr || totalframes < 0) totalframes = 0;

		Format.SamplesPerSec = static_cast<tjs_uint>(fileformat.mSampleRate);
		Format.Channels = fileformat.mChannelsPerFrame;
		Format.BitsPerSample = 16;
		Format.BytesPerSample = sizeof(tjs_int16);
		Format.TotalSamples = static_cast<tjs_uint64>(totalframes);
		Format.TotalTime = TVPCalculateTotalTime(Format.TotalSamples, Format.SamplesPerSec);
		Format.SpeakerConfig = 0;
		Format.IsFloat = false;
		Format.Seekable = true;
		FrameBytes = clientformat.mBytesPerFrame;
		return FrameBytes != 0;
	}

	void GetFormat(tTVPWaveFormat &format) override
	{
		format = Format;
	}

	bool Render(void *buf, tjs_uint bufsamplelen, tjs_uint &rendered) override
	{
		rendered = 0;
		if(!ExtendedFile || !buf || bufsamplelen == 0 || FrameBytes == 0) return false;

		while(rendered < bufsamplelen)
		{
			const tjs_uint remaining = bufsamplelen - rendered;
			const tjs_uint maxframes = std::numeric_limits<UInt32>::max() / FrameBytes;
			UInt32 frames = std::min(remaining, maxframes);
			if(frames == 0) return false;

			AudioBufferList buffers;
			buffers.mNumberBuffers = 1;
			buffers.mBuffers[0].mNumberChannels = Format.Channels;
			buffers.mBuffers[0].mDataByteSize = frames * FrameBytes;
			buffers.mBuffers[0].mData = static_cast<tjs_uint8 *>(buf) +
				static_cast<size_t>(rendered) * FrameBytes;

			const OSStatus status = ExtAudioFileRead(ExtendedFile, &frames, &buffers);
			if(status != noErr)
			{
				TVPLogCodecError(TJS_W("AudioToolbox"), TJS_W("decode"), StorageName,
					static_cast<tjs_int>(status));
				return false;
			}
			if(frames == 0) return false;
			rendered += frames;
		}

		return true;
	}

	bool SetPosition(tjs_uint64 samplepos) override
	{
		if(!ExtendedFile || samplepos > static_cast<tjs_uint64>(std::numeric_limits<SInt64>::max()))
			return false;
		if(Format.TotalSamples != 0 && samplepos >= Format.TotalSamples) return false;

		const OSStatus status = ExtAudioFileSeek(ExtendedFile, static_cast<SInt64>(samplepos));
		if(status != noErr)
		{
			TVPLogCodecError(TJS_W("AudioToolbox"), TJS_W("seek"), StorageName,
				static_cast<tjs_int>(status));
			return false;
		}
		return true;
	}

private:
	static OSStatus ReadCallback(void *clientdata, SInt64 position, UInt32 requestcount,
		void *buffer, UInt32 *actualcount)
	{
		auto *decoder = static_cast<tTVPWD_AudioToolbox *>(clientdata);
		if(actualcount) *actualcount = 0;
		if(!decoder || !decoder->Stream || !buffer || !actualcount || position < 0)
			return kAudioFilePositionError;

		try
		{
			const tjs_uint64 target = static_cast<tjs_uint64>(position);
			if(decoder->Stream->Seek(position, TJS_BS_SEEK_SET) != target)
				return kAudioFilePositionError;
			*actualcount = decoder->Stream->Read(buffer, requestcount);
			return noErr;
		}
		catch(...)
		{
			return kAudioFilePositionError;
		}
	}

	static SInt64 GetSizeCallback(void *clientdata)
	{
		auto *decoder = static_cast<tTVPWD_AudioToolbox *>(clientdata);
		if(!decoder || !decoder->Stream) return 0;
		try
		{
			const tjs_uint64 size = decoder->Stream->GetSize();
			return size > static_cast<tjs_uint64>(std::numeric_limits<SInt64>::max()) ?
				std::numeric_limits<SInt64>::max() : static_cast<SInt64>(size);
		}
		catch(...)
		{
			return 0;
		}
	}
};

// -------------------------------------------------------------------------
// Ogg Opus decoder
// -------------------------------------------------------------------------

bool TVPOpusFloatOutput = false;
bool TVPOpusOptionsInitialized = false;

void TVPInitOpusOptions()
{
	if(TVPOpusOptionsInitialized) return;

	tTJSVariant value;
	if(TVPGetCommandLine(TJS_W("-opus_gain"), &value))
	{
		const double db = static_cast<tTVReal>(value);
		const double factor = std::pow(10.0, db / 20.0);
		TVPAddLog(TJS_W("opus: Setting global gain to ") + ttstr(value) + TJS_W("dB (") +
			ttstr(static_cast<tjs_int>(factor * 100.0)) + TJS_W("%)."));
	}

	if(TVPGetCommandLine(TJS_W("-opus_pcm_format"), &value) &&
		ttstr(value) == TJS_W("f32"))
	{
		TVPOpusFloatOutput = true;
		TVPAddLog(TJS_W("opus: IEEE 32-bit float output enabled."));
	}

	TVPOpusOptionsInitialized = true;
}

class tTVPWD_Opus final : public tTVPWaveDecoder
{
	std::unique_ptr<tTJSBinaryStream> Stream;
	std::unique_ptr<OggOpusFile, decltype(&op_free)> InputFile;
	tTVPWaveFormat Format;
	ttstr StorageName;
	int CurrentSection;

public:
	tTVPWD_Opus(std::unique_ptr<tTJSBinaryStream> stream, const ttstr &storagename)
		: Stream(std::move(stream)), InputFile(nullptr, op_free), StorageName(storagename),
		CurrentSection(-1)
	{
		memset(&Format, 0, sizeof(Format));
		TVPInitOpusOptions();
	}

	bool Open()
	{
		int error = 0;
		const OpusFileCallbacks callbacks = { ReadCallback, SeekCallback, TellCallback,
			CloseCallback };
		InputFile.reset(op_open_callbacks(this, &callbacks, nullptr, 0, &error));
		if(!InputFile || error != 0) return false;

		const int links = op_link_count(InputFile.get());
		if(links <= 0) return false;
		const OpusHead *head = op_head(InputFile.get(), 0);
		if(!head || head->channel_count <= 0) return false;
		for(int index = 1; index < links; ++index)
		{
			const OpusHead *linkhead = op_head(InputFile.get(), index);
			if(!linkhead || linkhead->channel_count != head->channel_count) return false;
		}

		const ogg_int64_t totalsamples = op_pcm_total(InputFile.get(), -1);
		Format.SamplesPerSec = 48000;
		Format.Channels = static_cast<tjs_uint>(head->channel_count);
		Format.BitsPerSample = TVPOpusFloatOutput ? 32 : 16;
		Format.BytesPerSample = TVPOpusFloatOutput ? sizeof(float) : sizeof(opus_int16);
		Format.TotalSamples = totalsamples < 0 ? 0 : static_cast<tjs_uint64>(totalsamples);
		Format.TotalTime = TVPCalculateTotalTime(Format.TotalSamples, Format.SamplesPerSec);
		Format.SpeakerConfig = 0;
		Format.IsFloat = TVPOpusFloatOutput;
		Format.Seekable = op_seekable(InputFile.get()) != 0;
		return true;
	}

	void GetFormat(tTVPWaveFormat &format) override
	{
		format = Format;
	}

	bool Render(void *buf, tjs_uint bufsamplelen, tjs_uint &rendered) override
	{
		rendered = 0;
		if(!InputFile || !buf || bufsamplelen == 0) return false;

		const int channels = static_cast<int>(Format.Channels);
		const size_t samplesize = Format.BytesPerSample;
		while(rendered < bufsamplelen)
		{
			const tjs_uint remaining = bufsamplelen - rendered;
			const int maxframes = std::numeric_limits<int>::max() / channels;
			const int frames = static_cast<int>(std::min<tjs_uint>(remaining, maxframes));
			if(frames <= 0) return false;

			void *output = static_cast<tjs_uint8 *>(buf) +
				static_cast<size_t>(rendered) * channels * samplesize;
			int result;
			if(TVPOpusFloatOutput)
				result = op_read_float(InputFile.get(), static_cast<float *>(output),
					frames * channels, &CurrentSection);
			else
				result = op_read(InputFile.get(), static_cast<opus_int16 *>(output),
					frames * channels, &CurrentSection);

			if(result == OP_HOLE) continue;
			if(result < 0)
			{
				TVPLogCodecError(TJS_W("Opus"), TJS_W("decode"), StorageName, result);
				return false;
			}
			if(result == 0) return false;
			rendered += static_cast<tjs_uint>(result);
		}
		return true;
	}

	bool SetPosition(tjs_uint64 samplepos) override
	{
		if(!InputFile || !Format.Seekable ||
			samplepos > static_cast<tjs_uint64>(std::numeric_limits<ogg_int64_t>::max()))
			return false;
		if(Format.TotalSamples != 0 && samplepos >= Format.TotalSamples) return false;
		const int result = op_pcm_seek(InputFile.get(), static_cast<ogg_int64_t>(samplepos));
		if(result != 0)
		{
			TVPLogCodecError(TJS_W("Opus"), TJS_W("seek"), StorageName, result);
			return false;
		}
		return true;
	}

private:
	static int ReadCallback(void *stream, unsigned char *buffer, int bytes)
	{
		auto *decoder = static_cast<tTVPWD_Opus *>(stream);
		if(!decoder || !decoder->Stream || bytes < 0) return -1;
		try
		{
			return static_cast<int>(decoder->Stream->Read(buffer, static_cast<tjs_uint>(bytes)));
		}
		catch(...)
		{
			return -1;
		}
	}

	static int SeekCallback(void *stream, opus_int64 offset, int whence)
	{
		auto *decoder = static_cast<tTVPWD_Opus *>(stream);
		return decoder && TVPSeekStream(decoder->Stream.get(), offset, whence) ? 0 : -1;
	}

	static int CloseCallback(void *stream)
	{
		auto *decoder = static_cast<tTVPWD_Opus *>(stream);
		if(decoder) decoder->Stream.reset();
		return 0;
	}

	static opus_int64 TellCallback(void *stream)
	{
		auto *decoder = static_cast<tTVPWD_Opus *>(stream);
		if(!decoder || !decoder->Stream) return -1;
		try
		{
			const tjs_uint64 position = decoder->Stream->GetPosition();
			return position > static_cast<tjs_uint64>(std::numeric_limits<opus_int64>::max()) ?
				-1 : static_cast<opus_int64>(position);
		}
		catch(...)
		{
			return -1;
		}
	}
};

// -------------------------------------------------------------------------
// Ogg Vorbis decoder
// -------------------------------------------------------------------------

struct tTVPVorbisStreamCookie
{
	std::unique_ptr<tTJSBinaryStream> Stream;

	explicit tTVPVorbisStreamCookie(std::unique_ptr<tTJSBinaryStream> stream)
		: Stream(std::move(stream)) {}
};

int TVPVorbisRead(void *cookie, char *buffer, int bytes)
{
	auto *holder = static_cast<tTVPVorbisStreamCookie *>(cookie);
	if(!holder || !holder->Stream || !buffer || bytes < 0) return -1;
	try
	{
		return static_cast<int>(holder->Stream->Read(buffer, static_cast<tjs_uint>(bytes)));
	}
	catch(...)
	{
		return -1;
	}
}

fpos_t TVPVorbisSeek(void *cookie, fpos_t offset, int whence)
{
	auto *holder = static_cast<tTVPVorbisStreamCookie *>(cookie);
	tjs_uint64 position;
	if(!holder || !TVPSeekStream(holder->Stream.get(), static_cast<tjs_int64>(offset),
		whence, &position))
	{
		return static_cast<fpos_t>(-1);
	}
	if(position > static_cast<tjs_uint64>(std::numeric_limits<fpos_t>::max()))
		return static_cast<fpos_t>(-1);
	return static_cast<fpos_t>(position);
}

int TVPVorbisClose(void *cookie)
{
	delete static_cast<tTVPVorbisStreamCookie *>(cookie);
	return 0;
}

FILE *TVPOpenVorbisStream(std::unique_ptr<tTJSBinaryStream> stream)
{
	std::unique_ptr<tTVPVorbisStreamCookie> cookie(
		new tTVPVorbisStreamCookie(std::move(stream)));
	FILE *file = funopen(cookie.get(), TVPVorbisRead, nullptr, TVPVorbisSeek,
		TVPVorbisClose);
	if(!file) return nullptr;
	cookie.release();
	setvbuf(file, nullptr, _IONBF, 0);
	return file;
}

class tTVPWD_Vorbis final : public tTVPWaveDecoder
{
	std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> InputFile;
	tTVPWaveFormat Format;
	ttstr StorageName;

public:
	tTVPWD_Vorbis(stb_vorbis *input, const ttstr &storagename)
		: InputFile(input, stb_vorbis_close), StorageName(storagename)
	{
		memset(&Format, 0, sizeof(Format));
	}

	bool Open()
	{
		if(!InputFile) return false;
		const stb_vorbis_info info = stb_vorbis_get_info(InputFile.get());
		if(info.sample_rate == 0 || info.channels <= 0) return false;

		Format.SamplesPerSec = info.sample_rate;
		Format.Channels = static_cast<tjs_uint>(info.channels);
		Format.BitsPerSample = 16;
		Format.BytesPerSample = sizeof(tjs_int16);
		Format.TotalSamples = stb_vorbis_stream_length_in_samples(InputFile.get());
		Format.TotalTime = TVPCalculateTotalTime(Format.TotalSamples, Format.SamplesPerSec);
		Format.SpeakerConfig = 0;
		Format.IsFloat = false;
		Format.Seekable = true;
		return true;
	}

	void GetFormat(tTVPWaveFormat &format) override
	{
		format = Format;
	}

	bool Render(void *buf, tjs_uint bufsamplelen, tjs_uint &rendered) override
	{
		rendered = 0;
		if(!InputFile || !buf || bufsamplelen == 0) return false;

		const int channels = static_cast<int>(Format.Channels);
		while(rendered < bufsamplelen)
		{
			const tjs_uint remaining = bufsamplelen - rendered;
			const int maxframes = std::numeric_limits<int>::max() / channels;
			const int frames = static_cast<int>(std::min<tjs_uint>(remaining, maxframes));
			if(frames <= 0) return false;

			tjs_int16 *output = static_cast<tjs_int16 *>(buf) +
				static_cast<size_t>(rendered) * channels;
			const int result = stb_vorbis_get_samples_short_interleaved(InputFile.get(),
				channels, output, frames * channels);
			if(result <= 0)
			{
				const int error = stb_vorbis_get_error(InputFile.get());
				if(error != VORBIS__no_error)
					TVPLogCodecError(TJS_W("Vorbis"), TJS_W("decode"), StorageName, error);
				return false;
			}
			rendered += static_cast<tjs_uint>(result);
		}
		return true;
	}

	bool SetPosition(tjs_uint64 samplepos) override
	{
		if(!InputFile || samplepos > std::numeric_limits<unsigned int>::max()) return false;
		if(Format.TotalSamples != 0 && samplepos >= Format.TotalSamples) return false;
		if(!stb_vorbis_seek(InputFile.get(), static_cast<unsigned int>(samplepos)))
		{
			TVPLogCodecError(TJS_W("Vorbis"), TJS_W("seek"), StorageName,
				stb_vorbis_get_error(InputFile.get()));
			return false;
		}
		return true;
	}
};

std::unique_ptr<tTVPWaveDecoder> TVPCreateAudioToolboxDecoder(const ttstr &storagename,
	AudioFileTypeID typehint)
{
	std::unique_ptr<tTVPWD_AudioToolbox> decoder;
	try
	{
		std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(storagename));
		if(!stream) return nullptr;
		decoder.reset(new tTVPWD_AudioToolbox(std::move(stream), storagename));
		if(!decoder->Open(typehint)) return nullptr;
	}
	catch(...)
	{
		return nullptr;
	}
	return std::unique_ptr<tTVPWaveDecoder>(decoder.release());
}

std::unique_ptr<tTVPWaveDecoder> TVPCreateOpusDecoder(const ttstr &storagename)
{
	std::unique_ptr<tTVPWD_Opus> decoder;
	try
	{
		std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(storagename));
		if(!stream) return nullptr;
		decoder.reset(new tTVPWD_Opus(std::move(stream), storagename));
		if(!decoder->Open()) return nullptr;
	}
	catch(...)
	{
		return nullptr;
	}
	return std::unique_ptr<tTVPWaveDecoder>(decoder.release());
}

std::unique_ptr<tTVPWaveDecoder> TVPCreateVorbisDecoder(const ttstr &storagename)
{
	try
	{
		std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(storagename));
		if(!stream) return nullptr;
		FILE *file = TVPOpenVorbisStream(std::move(stream));
		if(!file) return nullptr;

		int error = 0;
		std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> input(
			stb_vorbis_open_file(file, 1, &error, nullptr), stb_vorbis_close);
		if(!input) return nullptr; // stb_vorbis closes the FILE on failure.

		std::unique_ptr<tTVPWD_Vorbis> decoder(
			new tTVPWD_Vorbis(input.release(), storagename));
		if(!decoder->Open()) return nullptr;
		return std::unique_ptr<tTVPWaveDecoder>(decoder.release());
	}
	catch(...)
	{
		return nullptr;
	}
}

bool TVPGetAudioToolboxType(const ttstr &extension, AudioFileTypeID &type)
{
	if(extension == TJS_W(".mp3")) type = kAudioFileMP3Type;
	else if(extension == TJS_W(".flac")) type = kAudioFileFLACType;
	else if(extension == TJS_W(".aac") || extension == TJS_W(".adts"))
		type = kAudioFileAAC_ADTSType;
	else if(extension == TJS_W(".m4a") || extension == TJS_W(".m4r"))
		type = kAudioFileM4AType;
	else if(extension == TJS_W(".m4b")) type = kAudioFileM4BType;
	else if(extension == TJS_W(".mp4") || extension == TJS_W(".mpg4"))
		type = kAudioFileMPEG4Type;
	else if(extension == TJS_W(".caf") || extension == TJS_W(".caff"))
		type = kAudioFileCAFType;
	else return false;
	return true;
}

class tTVPWDC_AudioCodec final : public tTVPWaveDecoderCreator
{
public:
	tTVPWaveDecoder *Create(const ttstr &storagename, const ttstr &extension) override
	{
		std::unique_ptr<tTVPWaveDecoder> decoder;
		if(extension == TJS_W(".opus"))
		{
			decoder = TVPCreateOpusDecoder(storagename);
		}
		else if(extension == TJS_W(".ogg") || extension == TJS_W(".oga"))
		{
			decoder = TVPCreateOpusDecoder(storagename);
			if(!decoder) decoder = TVPCreateVorbisDecoder(storagename);
		}
		else
		{
			AudioFileTypeID type;
			if(!TVPGetAudioToolboxType(extension, type)) return nullptr;
			decoder = TVPCreateAudioToolboxDecoder(storagename, type);
		}
		return decoder.release();
	}
};

} // namespace

/* WaveIntf.cpp already calls this legacy registration entry point. */
void TVPRegisterOpusDecoderCreator()
{
	static tTVPWDC_AudioCodec creator;
	TVPRegisterWaveDecoderCreator(&creator);
}
