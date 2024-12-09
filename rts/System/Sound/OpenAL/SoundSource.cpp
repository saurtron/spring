/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "SoundSource.h"

#include <climits>
#include <alc.h>

#include "ALShared.h"
#include "EFX.h"
#include "System/Sound/IAudioChannel.h"
#include "MusicStream.h"
#include "System/Sound/SoundLog.h"
#include "SoundBuffer.h"
#include "SoundItem.h"

#include "Sound.h" //remove when unified ElmoInMeters

#include "Sim/Misc/GlobalConstants.h"
#include "System/float3.h"
#include "System/StringUtil.h"
#include "System/SpringMath.h"


static constexpr float ROLLOFF_FACTOR = 5.0f;
static constexpr float REFERENCE_DIST = 200.0f;


// used to adjust the pitch to the GameSpeed (optional)
float CSoundSource::globalPitch = 1.0f;

// reduce the rolloff when the camera is height above the ground (so we still hear something in tab mode or far zoom)
float CSoundSource::heightRolloffModifier = 1.0f;

void CSoundSource::swap(CSoundSource& r)
{
	std::swap(id, r.id);
	std::swap(curChannel, r.curChannel);
	std::swap(curStream, r.curStream);
	std::swap(curVolume, r.curVolume);
	std::swap(loopStop, r.loopStop);
	std::swap(in3D, r.in3D);
	std::swap(efxEnabled, r.efxEnabled);
	std::swap(efxUpdates, r.efxUpdates);
	std::swap(curHeightRolloffModifier, r.curHeightRolloffModifier);

	std::swap(curPlayingItem, r.curPlayingItem);
	std::swap(asyncPlayItem, r.asyncPlayItem);
}

CSoundSource::CSoundSource()
{
	alGenSources(1, &id);

	if (!CheckError("CSoundSource::CSoundSource")) {
		id = 0;
	} else {
		alSourcef(id, AL_REFERENCE_DISTANCE, REFERENCE_DIST * ELMOS_TO_METERS);
		CheckError("CSoundSource::CSoundSource");
	}
}


CSoundSource::CSoundSource(CSoundSource&& src)
{
	// can't use naive/default move because `id` member has to be unique
	this->swap(src);
}

CSoundSource& CSoundSource::operator = (CSoundSource&& src) {
	this->swap(src);
	return *this;
}

CSoundSource::~CSoundSource()
{
	Delete();
}

void CSoundSource::Update()
{
	if (asyncPlayItem.id != 0) {
		// Sound::Update() holds mutex, soundItems can not be accessed concurrently
		Play(asyncPlayItem.channel, sound->GetSoundItem(asyncPlayItem.id), asyncPlayItem.position, asyncPlayItem.velocity, asyncPlayItem.volume, asyncPlayItem.relative);
		asyncPlayItem = AsyncSoundItemData();
	}

	if (curPlayingItem.id != 0) {
		if (in3D && (efxEnabled != efx.Enabled())) {
			alSourcef(id, AL_AIR_ABSORPTION_FACTOR, (efx.Enabled()) ? efx.GetAirAbsorptionFactor() : 0);
			alSource3i(id, AL_AUXILIARY_SEND_FILTER, (efx.Enabled()) ? efx.sfxSlot : AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
			alSourcei(id, AL_DIRECT_FILTER, (efx.Enabled()) ? efx.sfxFilter : AL_FILTER_NULL);
			efxEnabled = efx.Enabled();
			efxUpdates = efx.updates;
		}

		if (heightRolloffModifier != curHeightRolloffModifier) {
			curHeightRolloffModifier = heightRolloffModifier;
			alSourcef(id, AL_ROLLOFF_FACTOR, ROLLOFF_FACTOR * curPlayingItem.rolloff * heightRolloffModifier);
		}

		if (!IsPlaying(true) || ((curPlayingItem.loopTime > 0) && (spring_gettime() > loopStop)))
			Stop();
	}

	if (curStream) {
		if (curStream->IsFinished()) {
			Stop();
		} else {
			curStream->Update();
			CheckError("CSoundSource::Update");
		}
	}

	if (efxEnabled && (efxUpdates != efx.updates)) {
		// airAbsorption & LowPass aren't auto updated by OpenAL on change, so we need to do it per source
		alSourcef(id, AL_AIR_ABSORPTION_FACTOR, efx.GetAirAbsorptionFactor());
		alSourcei(id, AL_DIRECT_FILTER, efx.sfxFilter);
		efxUpdates = efx.updates;
	}
}

void CSoundSource::Delete()
{
	if (efxEnabled) {
		alSource3i(id, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
		alSourcei(id, AL_DIRECT_FILTER, AL_FILTER_NULL);
	}

	Stop();
	alDeleteSources(1, &id);
	CheckError("CSoundSource::Delete");
}


int CSoundSource::GetCurrentPriority() const
{
	if (asyncPlayItem.id != 0)
		return asyncPlayItem.priority;

	if (curStream)
		return INT_MAX;

	if (curPlayingItem.id == 0)
		return INT_MIN;

	return (curPlayingItem.priority);
}

bool CSoundSource::IsPlaying(const bool checkOpenAl) const
{
	if (curStream)
		return true;

	if (asyncPlayItem.id != 0)
		return true;

	if (curPlayingItem.id == 0)
		return false;

	// calling OpenAL has a high chance of generating a L2 cache miss, avoid if possible
	if (!checkOpenAl)
		return true;

	CheckError("CSoundSource::IsPlaying");
	ALint state;
	alGetSourcei(id, AL_SOURCE_STATE, &state);
	CheckError("CSoundSource::IsPlaying");
	return (state == AL_PLAYING);
}


void CSoundSource::Stop()
{
	alSourceStop(id);

	{
		SoundItem* item = nullptr;

		// callers marked * are mutex-guarded
		//   ::Delete via ~CSoundSource via CSound::Kill
		//   ::Play via ::Update (*)
		//   ::PlayStream via AudioChannel::StreamPlay (*)
		//   ::StreamStop via AudioChannel::StreamStop (*)
		//   AudioChannel::FindSourceAndPlay (*)
		if (sound != nullptr)
			item = sound->GetSoundItem(curPlayingItem.id);
		if (item != nullptr)
			item->StopPlay();

		curPlayingItem = {};
	}

	curStream.reset();

	if (curChannel != nullptr) {
		IAudioChannel* oldChannel = curChannel;
		curChannel = nullptr;
		oldChannel->SoundSourceFinished(this);
	}
	CheckError("CSoundSource::Stop");
}

void CSoundSource::Play(IAudioChannel* channel, SoundItem* item, float3 pos, float3 velocity, float volume, bool relative)
{
	assert(!curStream);
	assert(channel);

	if (!item->PlayNow())
		return;

	const SoundBuffer& itemBuffer = SoundBuffer::GetById(item->GetSoundBufferID());

	Stop();

	curVolume = volume;
	curPlayingItem = {item->soundItemID,  item->loopTime, item->priority,  item->GetGain(), item->rolloff};
	curChannel = channel;

	alSourcei(id, AL_BUFFER, itemBuffer.GetId());
	alSourcef(id, AL_GAIN, volume * item->GetGain() * channel->volume);
	alSourcef(id, AL_PITCH, item->GetPitch() * globalPitch);

	velocity *= item->dopplerScale * ELMOS_TO_METERS;
	alSource3f(id, AL_VELOCITY, velocity.x, velocity.y, velocity.z);
	alSourcei(id, AL_LOOPING, (item->loopTime > 0) ? AL_TRUE : AL_FALSE);

	loopStop = spring_gettime() + spring_msecs(item->loopTime);

	if (relative || !item->in3D) {
		in3D = false;
		if (efxEnabled) {
			alSource3i(id, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
			alSourcei(id, AL_DIRECT_FILTER, AL_FILTER_NULL);
			efxEnabled = false;
		}
		alSourcei(id, AL_SOURCE_RELATIVE, AL_TRUE);
		alSourcef(id, AL_ROLLOFF_FACTOR, 0.f);
		alSource3f(id, AL_POSITION, 0.0f, 0.0f, -1.0f * ELMOS_TO_METERS);
#if defined(__APPLE__) || defined(__OpenBSD__)
		alSourcef(id, AL_REFERENCE_DISTANCE, REFERENCE_DIST * ELMOS_TO_METERS);
#endif
	} else {
		if (itemBuffer.GetChannels() > 1)
			LOG_L(L_WARNING, "Can not play non-mono \"%s\" in 3d.", itemBuffer.GetFilename().c_str());

		in3D = true;
		if (efx.Enabled()) {
			efxEnabled = true;
			alSourcef(id, AL_AIR_ABSORPTION_FACTOR, efx.GetAirAbsorptionFactor());
			alSource3i(id, AL_AUXILIARY_SEND_FILTER, efx.sfxSlot, 0, AL_FILTER_NULL);
			alSourcei(id, AL_DIRECT_FILTER, efx.sfxFilter);
			efxUpdates = efx.updates;
		}

		alSourcei(id, AL_SOURCE_RELATIVE, AL_FALSE);
		pos *= ELMOS_TO_METERS;
		alSource3f(id, AL_POSITION, pos.x, pos.y, pos.z);
		curHeightRolloffModifier = heightRolloffModifier;
		alSourcef(id, AL_ROLLOFF_FACTOR, ROLLOFF_FACTOR * item->rolloff * heightRolloffModifier);


#if defined(__APPLE__) || defined(__OpenBSD__)
		alSourcef(id, AL_MAX_DISTANCE, 1000000.0f);
		// Max distance is too small by default on my Mac...
		ALfloat gain = channel->volume * item->GetGain() * volume;
		if (gain > 1.0f) {
			// OpenAL on Mac cannot handle AL_GAIN > 1 well, so we will adjust settings to get the same output with AL_GAIN = 1.
			const ALint model = alGetInteger(AL_DISTANCE_MODEL);
			const ALfloat rolloff = ROLLOFF_FACTOR * item->rolloff * heightRolloffModifier;
			const ALfloat refDist = REFERENCE_DIST * ELMOS_TO_METERS;

			if ((model == AL_INVERSE_DISTANCE_CLAMPED) || (model == AL_INVERSE_DISTANCE)) {
				alSourcef(id, AL_REFERENCE_DISTANCE, ((gain - 1.0f) * refDist / rolloff) + refDist);
				alSourcef(id, AL_ROLLOFF_FACTOR, (gain + rolloff - 1.0f) / gain);
				alSourcef(id, AL_GAIN, 1.0f);
			}
		} else {
			alSourcef(id, AL_REFERENCE_DISTANCE, REFERENCE_DIST * ELMOS_TO_METERS);
		}
#endif

	}
	alSourcePlay(id);

	if (itemBuffer.GetId() == 0)
		LOG_L(L_WARNING, "CSoundSource::Play: Empty buffer for item %s (file %s)", item->name.c_str(), itemBuffer.GetFilename().c_str());

	CheckError("CSoundSource::Play");
}


void CSoundSource::PlayAsync(IAudioChannel* channel, size_t id, float3 pos, float3 velocity, float volume, float priority, bool relative)
{
	asyncPlayItem.channel  = channel;
	asyncPlayItem.id       = id;

	asyncPlayItem.position = pos;
	asyncPlayItem.velocity = velocity;

	asyncPlayItem.volume   = volume;
	asyncPlayItem.priority = priority;

	asyncPlayItem.relative = relative;
}


void CSoundSource::PlayStream(IAudioChannel* channel, const std::string& file, float volume)
{
	// stop any current playback
	Stop();

	if (!curStream)
		curStream = std::make_unique <MusicStream> ();

	// OpenAL params
	curChannel = channel;
	curVolume = volume;
	in3D = false;

	if (efxEnabled) {
		alSource3i(id, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
		alSourcei(id, AL_DIRECT_FILTER, AL_FILTER_NULL);
		efxEnabled = false;
	}

	alSource3f(id, AL_POSITION,       0.0f, 0.0f, 0.0f);
	alSourcef(id, AL_GAIN,            volume);
	alSourcef(id, AL_PITCH,           globalPitch);
	alSource3f(id, AL_VELOCITY,       0.0f,  0.0f,  0.0f);
	alSource3f(id, AL_DIRECTION,      0.0f,  0.0f,  0.0f);
	alSourcef(id, AL_ROLLOFF_FACTOR,  0.0f);
	alSourcei(id, AL_SOURCE_RELATIVE, AL_TRUE);

	// COggStreams only appends buffers, giving errors when a buffer of another format is still assigned
	alSourcei(id, AL_BUFFER, AL_NONE);
	curStream->Play(file, volume, id);
	curStream->Update();
	CheckError("CSoundSource::Update");
}

void CSoundSource::StreamStop()
{
	if (!curStream)
		return;

	Stop();
}

void CSoundSource::StreamPause()
{
	if (!curStream)
		return;

	if (curStream->TogglePause())
		alSourcePause(id);
	else
		alSourcePlay(id);
}

float CSoundSource::GetStreamTime()
{
	return curStream
		? curStream->GetTotalTime()
		: 0.0f
	;
}

float CSoundSource::GetStreamPlayTime()
{
	return curStream
		? curStream->GetPlayTime()
		: 0.0f
	;
}

void CSoundSource::UpdateVolume()
{
	if (curChannel == nullptr)
		return;

	if (curStream) {
		alSourcef(id, AL_GAIN, curVolume * curChannel->volume);
		return;
	}
	if (curPlayingItem.id != 0) {
		alSourcef(id, AL_GAIN, curVolume * curPlayingItem.rndGain * curChannel->volume);
		return;
	}
}

