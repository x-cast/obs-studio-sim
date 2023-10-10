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
#include <chrono>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include <rtc/rtc.hpp>

class WHEPSource {
public:
	WHEPSource(obs_data_t *settings, obs_source_t *source);
	~WHEPSource();

	obs_properties_t *GetProperties();
	void Update(obs_data_t *settings);
	void MaybeSendPLI();

	std::atomic<std::chrono::system_clock::time_point> last_frame;
	std::chrono::system_clock::time_point last_pli;

private:
	bool Init();
	void SetupPeerConnection();
	bool Connect();
	void StartThread();
	void SendDelete();
	void Stop();
	void StopThread();
	void DepacketizeH264();
	void DecodeH264(std::vector<std::byte> &pkt, uint64_t timestamp);
	void DepacketizeOpus();
	void DecodeOpus(std::vector<std::byte> &pkt, uint64_t timestamp);

	AVCodecContext *CreateVideoAVCodecDecoder();
	AVCodecContext *CreateAudioAVCodecDecoder();

	void OnMessageHandler(rtc::binary msg);
	void OnAudioMessageHandler(rtc::binary msg);

	obs_source_t *source;

	std::string endpoint_url;
	std::string resource_url;
	std::string bearer_token;

	std::shared_ptr<rtc::PeerConnection> peer_connection;
	std::shared_ptr<rtc::Track> audio_track;
	std::shared_ptr<rtc::Track> video_track;

	std::shared_ptr<AVCodecContext> video_av_codec_context;
	std::shared_ptr<AVCodecContext> audio_av_codec_context;
	std::shared_ptr<AVPacket> av_packet;
	std::shared_ptr<AVFrame> av_frame;

	std::atomic<bool> running;
	std::mutex start_stop_mutex;
	std::thread start_stop_thread;

	std::vector<std::vector<std::byte>> rtp_pkts_video;
	std::vector<std::vector<std::byte>> rtp_pkts_audio;
	std::vector<std::byte> fua_buffer;
	std::vector<std::byte> sps_and_pps;

	uint64_t last_video_rtp_timestamp;
	uint64_t last_video_pts;
};

void register_whep_source();
