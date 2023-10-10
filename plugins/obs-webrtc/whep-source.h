#pragma once

#include <obs-module.h>
#include <util/curl/curl-helper.h>
#include <util/platform.h>
#include <util/base.h>
#include <util/dstr.h>

#include <string>
#include <atomic>
#include <mutex>
#include <thread>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/mem.h"
#include "libavutil/mathematics.h"
#include <media-playback/media-playback.h>
}

#include "../obs-ffmpeg/obs-ffmpeg-compat.h"

#include <rtc/rtc.hpp>

class RTPQueue {
private:
	std::queue<std::vector<std::byte>> pkts;
	std::mutex lock;
	std::condition_variable notify;

public:
	void push(std::vector<std::byte> item)
	{

		std::unique_lock<std::mutex> l(lock);

		pkts.push(item);
		notify.notify_one();
	}

	std::vector<std::byte> pop()
	{

		std::unique_lock<std::mutex> l(lock);

		notify.wait(l, [this]() { return !pkts.empty(); });

		auto item = pkts.front();
		pkts.pop();

		return item;
	}
};

class WHEPSource {
public:
	WHEPSource(obs_data_t *settings, obs_source_t *source);
	~WHEPSource();

	obs_properties_t *GetProperties();
	void Update(obs_data_t *settings);
	void Activate();

private:
	bool Init();
	void SetupPeerConnection();
	void SetupDecoding();
	bool Connect();
	void StartThread();
	void SendDelete();
	void StopThread(bool signal);

	AVIOContext *CreateAVIOContextSDP();
	AVIOContext *CreateAVIOContextRTP();
	AVIOContext *CreateAudioAVIOContextSDP();
	AVIOContext *CreateAudioAVIOContextRTP();

	void OnMessageHandler(rtc::binary msg);

	obs_source_t *source;

	std::string endpoint_url;
	std::string resource_url;
	std::string bearer_token;

	std::shared_ptr<rtc::PeerConnection> peer_connection;
	std::shared_ptr<rtc::Track> audio_track;
	std::shared_ptr<rtc::Track> video_track;

	std::mutex start_stop_mutex;
	std::thread start_stop_thread;

	// FFMpeg state
	bool have_read_video_session_description;
	bool have_read_audio_session_description;

	// media-playback
	media_playback_t *media_video;
	media_playback_t *media_audio;

	RTPQueue video_queue;
	RTPQueue audio_queue;

	// TODO - How do you know when a URL + Bearer Token has changed?
	// `Update` is fired for every character change. We only want to know when the dialog is closed
	bool activated;
};

void register_whep_source();
