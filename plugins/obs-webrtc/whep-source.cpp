#include "whep-source.h"
#include "webrtc-utils.h"

#define do_log(level, format, ...)                              \
	blog(level, "[obs-webrtc] [whep_source: '%s'] " format, \
	     obs_source_get_name(source), ##__VA_ARGS__)

const auto rtp_buff_size = 1500;
const auto pli_interval = 500;
const auto video_session_description = "v=0\r\n"
				       "o=- 0 0 IN IP4 127.0.0.1\r\n"
				       "c=IN IP4 127.0.0.1\r\n"
				       "m=video 5000 RTP/AVP 96\r\n"
				       "a=rtpmap:96 H264/90000\r\n"
				       "a=fmtp:96";
const auto audio_session_description = "v=0\r\n"
				       "o=- 0 0 IN IP4 127.0.0.1\r\n"
				       "c=IN IP4 127.0.0.1\r\n"
				       "m=audio 5000 RTP/AVP 111\r\n"
				       "a=rtpmap:111 opus/48000/2\r\n"
				       "a=fmtp:111";
const auto ffmpeg_options = "sdp_flags=custom_io reorder_queue_size=0";

WHEPSource::WHEPSource(obs_data_t *settings, obs_source_t *source)
	: source(source),
	  endpoint_url(),
	  resource_url(),
	  bearer_token(),
	  peer_connection(nullptr),
	  audio_track(nullptr),
	  video_track(nullptr),
	  running(false),
	  start_stop_mutex(),
	  start_stop_thread(),
	  have_read_video_session_description(false),
	  have_read_audio_session_description(false),
	  media_video(nullptr),
	  media_audio(nullptr),
	  last_frame(std::chrono::system_clock::now()),
	  video_queue(),
	  audio_queue()
{
	Update(settings);
}

WHEPSource::~WHEPSource()
{
	running = false;

	if (media_audio) {
		media_playback_destroy(media_audio);
		media_audio = nullptr;
	}
	if (media_video) {
		media_playback_destroy(media_video);
		media_video = nullptr;
	}

	Stop();

	std::lock_guard<std::mutex> l(start_stop_mutex);
	if (start_stop_thread.joinable())
		start_stop_thread.join();
}

void WHEPSource::Stop()
{
	std::lock_guard<std::mutex> l(start_stop_mutex);
	if (start_stop_thread.joinable())
		start_stop_thread.join();

	start_stop_thread = std::thread(&WHEPSource::StopThread, this);
}

void WHEPSource::StopThread()
{
	if (peer_connection != nullptr) {
		peer_connection->close();
		peer_connection = nullptr;
		audio_track = nullptr;
		video_track = nullptr;
	}

	SendDelete();
}

void WHEPSource::SendDelete()
{
	if (resource_url.empty()) {
		do_log(LOG_DEBUG,
		       "No resource URL available, not sending DELETE");
		return;
	}

	auto status = send_delete(bearer_token, resource_url);
	if (status == webrtc_network_status::Success) {
		do_log(LOG_DEBUG,
		       "Successfully performed DELETE request for resource URL");
		resource_url.clear();
	} else if (status == webrtc_network_status::DeleteFailed) {
		do_log(LOG_WARNING, "DELETE request for resource URL failed");
	} else if (status == webrtc_network_status::InvalidHTTPStatusCode) {
		do_log(LOG_WARNING,
		       "DELETE request for resource URL returned non-200 Status Code");
	}
}

obs_properties_t *WHEPSource::GetProperties()
{
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_set_flags(ppts, OBS_PROPERTIES_DEFER_UPDATE);
	obs_properties_add_text(ppts, "endpoint_url", "URL", OBS_TEXT_DEFAULT);
	obs_properties_add_text(ppts, "bearer_token",
				obs_module_text("Service.BearerToken"),
				OBS_TEXT_PASSWORD);

	return ppts;
}

void WHEPSource::Update(obs_data_t *settings)
{
	endpoint_url =
		std::string(obs_data_get_string(settings, "endpoint_url"));
	bearer_token =
		std::string(obs_data_get_string(settings, "bearer_token"));

	if (endpoint_url.empty() || bearer_token.empty()) {
		return;
	}

	std::lock_guard<std::mutex> l(start_stop_mutex);

	if (start_stop_thread.joinable())
		start_stop_thread.join();

	start_stop_thread = std::thread(&WHEPSource::StartThread, this);
}

AVIOContext *WHEPSource ::CreateVideoAVIOContextSDP()
{
	auto avio_context = avio_alloc_context(
		reinterpret_cast<unsigned char *>(
			av_strdup(video_session_description)),
		static_cast<int>(strlen(video_session_description)), 0, this,
		[](void *opaque, uint8_t *buf, int buf_size) -> int {
			auto whep_source = static_cast<WHEPSource *>(opaque);

			if (whep_source->have_read_video_session_description) {
				return AVERROR_EOF;
			}

			strncpy(reinterpret_cast<char *>(buf),
				video_session_description,
				static_cast<size_t>(buf_size));
			whep_source->have_read_video_session_description = true;
			return static_cast<int>(
				strlen(video_session_description));
		},
		NULL, NULL);

	if (avio_context == nullptr) {
		throw std::runtime_error("Failed to create avio_context");
	}

	avio_context->direct = 1;

	return avio_context;
}

AVIOContext *WHEPSource::CreateVideoAVIOContextRTP()
{
	auto avio_context = avio_alloc_context(
		static_cast<unsigned char *>(av_malloc(rtp_buff_size)),
		rtp_buff_size, 1, this,
		[](void *opaque, uint8_t *buff, int) -> int {
			auto whep_source = static_cast<WHEPSource *>(opaque);

			auto video_buff = whep_source->video_queue.pop();
			memcpy(buff, video_buff.data(), video_buff.size());

			return static_cast<int>(video_buff.size());
		},
		// Ignore RTCP Packets. Must be set
		[](void *, uint8_t *, int buf_size) -> int { return buf_size; },
		NULL);

	if (avio_context == nullptr) {
		throw std::runtime_error("Failed to create avio_context");
	}

	avio_context->direct = 1;

	return avio_context;
}

AVIOContext *WHEPSource ::CreateAudioAVIOContextSDP()
{
	auto avio_context = avio_alloc_context(
		reinterpret_cast<unsigned char *>(
			av_strdup(audio_session_description)),
		static_cast<int>(strlen(audio_session_description)), 0, this,
		[](void *opaque, uint8_t *buf, int buf_size) -> int {
			auto whep_source = static_cast<WHEPSource *>(opaque);

			if (whep_source->have_read_audio_session_description) {
				return AVERROR_EOF;
			}

			strncpy(reinterpret_cast<char *>(buf),
				audio_session_description,
				static_cast<size_t>(buf_size));
			whep_source->have_read_audio_session_description = true;
			return static_cast<int>(
				strlen(audio_session_description));
		},
		NULL, NULL);

	if (avio_context == nullptr) {
		throw std::runtime_error("Failed to create avio_context");
	}

	avio_context->direct = 1;

	return avio_context;
}

AVIOContext *WHEPSource::CreateAudioAVIOContextRTP()
{
	auto avio_context = avio_alloc_context(
		static_cast<unsigned char *>(av_malloc(rtp_buff_size)),
		rtp_buff_size, 1, this,
		[](void *opaque, uint8_t *buff, int) -> int {
			auto whep_source = static_cast<WHEPSource *>(opaque);

			auto audio_buff = whep_source->audio_queue.pop();
			memcpy(buff, audio_buff.data(), audio_buff.size());

			return static_cast<int>(audio_buff.size());
		},
		// Ignore RTCP Packets. Must be set
		[](void *, uint8_t *, int buf_size) -> int { return buf_size; },
		NULL);

	if (avio_context == nullptr) {
		throw std::runtime_error("Failed to create avio_context");
	}

	avio_context->direct = 1;

	return avio_context;
}

void WHEPSource::OnMessageHandler(rtc::binary msg)
{
	this->video_queue.push(std::vector<std::byte>{msg});
}

void WHEPSource::SetupPeerConnection()
{
	peer_connection = std::make_shared<rtc::PeerConnection>();
	peer_connection->onStateChange([this](rtc::PeerConnection::State state) {
		switch (state) {
		case rtc::PeerConnection::State::New:
			do_log(LOG_INFO, "PeerConnection state is now: New");
			break;
		case rtc::PeerConnection::State::Connecting:
			do_log(LOG_INFO,
			       "PeerConnection state is now: Connecting");
			break;
		case rtc::PeerConnection::State::Connected:
			do_log(LOG_INFO,
			       "PeerConnection state is now: Connected");
			break;
		case rtc::PeerConnection::State::Disconnected:
			do_log(LOG_INFO,
			       "PeerConnection state is now: Disconnected");
			break;
		case rtc::PeerConnection::State::Failed:
			do_log(LOG_INFO, "PeerConnection state is now: Failed");
			break;
		case rtc::PeerConnection::State::Closed:
			do_log(LOG_INFO, "PeerConnection state is now: Closed");
			break;
		}
	});

	rtc::Description::Audio audioMedia(
		"0", rtc::Description::Direction::RecvOnly);
	audioMedia.addOpusCodec(111);
	audio_track = peer_connection->addTrack(audioMedia);

	auto audio_session = std::make_shared<rtc::RtcpReceivingSession>();
	audio_track->setMediaHandler(audio_session);
	audio_track->onMessage([&](rtc::binary) {}, nullptr);

	rtc::Description::Video videoMedia(
		"1", rtc::Description::Direction::RecvOnly);
	videoMedia.addH264Codec(96);
	video_track = peer_connection->addTrack(videoMedia);

	auto video_session = std::make_shared<rtc::RtcpReceivingSession>();
	video_track->setMediaHandler(video_session);
	video_track->onMessage(
		[&](rtc::binary msg) { this->OnMessageHandler(msg); }, nullptr);

	peer_connection->setLocalDescription();
}

void WHEPSource::StartThread()
{
	running = true;
	SetupPeerConnection();

	auto status = send_offer(bearer_token, endpoint_url, peer_connection,
				 resource_url);
	if (status != webrtc_network_status::Success) {
		if (status == webrtc_network_status::ConnectFailed) {
			do_log(LOG_ERROR,
			       "Connect failed: CURL returned result not CURLE_OK");
		} else if (status ==
			   webrtc_network_status::InvalidHTTPStatusCode) {
			do_log(LOG_ERROR,
			       "Connect failed: HTTP endpoint returned non-201 response code");
		} else if (status == webrtc_network_status::NoHTTPData) {
			do_log(LOG_ERROR,
			       "Connect failed: No data returned from HTTP endpoint request");
		} else if (status == webrtc_network_status::NoLocationHeader) {
			do_log(LOG_ERROR,
			       "WHEP server did not provide a resource URL via the Location header");
		} else if (status ==
			   webrtc_network_status::FailedToBuildResourceURL) {
			do_log(LOG_ERROR, "Failed to build Resource URL");
		} else if (status ==
			   webrtc_network_status::InvalidLocationHeader) {
			do_log(LOG_ERROR,
			       "WHEP server provided a invalid resource URL via the Location header");
		}

		peer_connection->close();
		return;
	}

	do_log(LOG_DEBUG, "WHEP Resource URL is: %s", resource_url.c_str());

	SetupDecoding();
}

void WHEPSource::SetupDecoding()
{
	struct mp_media_info info = {};

	info.opaque = this;
	info.v_cb = [](void *opaque, struct obs_source_frame *f) {
		auto whep_source = static_cast<WHEPSource *>(opaque);
		whep_source->last_frame = std::chrono::system_clock::now();

		obs_source_output_video(whep_source->source, f);
	};
	info.a_cb = [](void *opaque, struct obs_source_audio *f) {
		obs_source_output_audio(
			static_cast<WHEPSource *>(opaque)->source, f);
	};

	info.path = "";
	info.format = nullptr;
	info.buffering = 0;
	info.speed = 100;
	info.hardware_decoding = false;
	info.ffmpeg_options = (char *)ffmpeg_options;
	info.is_local_file = false;
	info.full_decode = false;

	info.av_io_context_open = CreateVideoAVIOContextSDP();
	info.av_io_context_playback = CreateVideoAVIOContextRTP();

	this->media_video = media_playback_create(&info);
	media_playback_play(this->media_video, true, true);

	info.av_io_context_open = CreateAudioAVIOContextSDP();
	info.av_io_context_playback = CreateAudioAVIOContextRTP();

	this->media_audio = media_playback_create(&info);
	media_playback_play(this->media_audio, true, true);
}

void WHEPSource::MaybeSendPLI()
{
	auto time_since_frame =
		std::chrono::system_clock::now() - last_frame.load();

	if (std::chrono::duration_cast<std::chrono::milliseconds>(
		    time_since_frame)
		    .count() < pli_interval) {
		return;
	}

	auto time_since_pli = std::chrono::system_clock::now() - last_pli;
	if (std::chrono::duration_cast<std::chrono::milliseconds>(
		    time_since_pli)
		    .count() < pli_interval) {
		return;
	}

	if (video_track != nullptr) {
		video_track->requestKeyframe();
	}

	last_pli = std::chrono::system_clock::now();
}

void register_whep_source()
{
	struct obs_source_info info = {};

	info.id = "whep_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO |
			    OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = [](void *) -> const char * {
		return obs_module_text("Source.Name");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		return new WHEPSource(settings, source);
	};
	info.destroy = [](void *priv_data) {
		delete static_cast<WHEPSource *>(priv_data);
	};
	info.get_properties = [](void *priv_data) -> obs_properties_t * {
		return static_cast<WHEPSource *>(priv_data)->GetProperties();
	};
	info.update = [](void *priv_data, obs_data_t *settings) {
		static_cast<WHEPSource *>(priv_data)->Update(settings);
	};
	info.video_tick = [](void *priv_data, float) {
		static_cast<WHEPSource *>(priv_data)->MaybeSendPLI();
	};

	obs_register_source(&info);
}
