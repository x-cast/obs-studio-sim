#include "whep-source.h"
#include "webrtc-utils.h"

#define do_log(level, format, ...)                              \
	blog(level, "[obs-webrtc] [whep_source: '%s'] " format, \
	     obs_source_get_name(source), ##__VA_ARGS__)

const auto rtp_buff_size = 1500;
const auto session_description = "v=0\r\n"
				 "o=- 0 0 IN IP4 127.0.0.1\r\n"
				 "c=IN IP4 127.0.0.1\r\n"
				 "m=video 5000 RTP/AVP 96\r\n"
				 "a=rtpmap:96 H264/90000\r\n"
				 "a=fmtp:96";
const auto ffmpeg_options = "sdp_flags=custom_io reorder_queue_size=0";

WHEPSource::WHEPSource(obs_data_t *settings, obs_source_t *source)
	: source(source),
	  endpoint_url(),
	  bearer_token(),
	  peer_connection(nullptr),
	  start_stop_mutex(),
	  start_stop_thread(),
	  have_read_session_description(0),
	  media_video(nullptr),
	  video_queue(),
	  activated(false)

{
	Update(settings);
}

WHEPSource::~WHEPSource()
{
	std::lock_guard<std::mutex> l(start_stop_mutex);
	if (start_stop_thread.joinable())
		start_stop_thread.join();
}

obs_properties_t *WHEPSource::GetProperties()
{
	obs_properties_t *ppts = obs_properties_create();

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
}

void WHEPSource::Activate()
{
	if (activated || endpoint_url.empty() || bearer_token.empty()) {
		return;
	}
	activated = true;

	std::lock_guard<std::mutex> l(start_stop_mutex);

	if (start_stop_thread.joinable())
		start_stop_thread.join();
	start_stop_thread = std::thread(&WHEPSource::StartThread, this);
}

AVIOContext *WHEPSource ::CreateAVIOContextSDP()
{
	auto avio_context = avio_alloc_context(
		reinterpret_cast<unsigned char *>(
			av_strdup(session_description)),
		strlen(session_description), 0, this,
		[](void *opaque, uint8_t *buf, int buf_size) -> int {
			auto whep_source = static_cast<WHEPSource *>(opaque);

			if (whep_source->have_read_session_description) {
				return AVERROR_EOF;
			}

			strncpy(reinterpret_cast<char *>(buf),
				session_description, buf_size);
			whep_source->have_read_session_description = true;
			return strlen(session_description);
		},
		NULL, NULL);

	if (avio_context == nullptr) {
		throw std::runtime_error("Failed to create avio_context");
	}

	return avio_context;
}

AVIOContext *WHEPSource::CreateAVIOContextRTP()
{
	auto avio_context = avio_alloc_context(
		static_cast<unsigned char *>(av_malloc(rtp_buff_size)),
		rtp_buff_size, 1, this,
		[](void *opaque, uint8_t *buff, int) -> int {
			auto whep_source = static_cast<WHEPSource *>(opaque);

			auto video_buff = whep_source->video_queue.pop();
			std::memcpy(buff, video_buff.data(), video_buff.size());

			return video_buff.size();
		},
		// Ignore RTCP Packets. Must be set
		[](void *, uint8_t *, int buf_size) -> int { return buf_size; },
		NULL);

	if (avio_context == nullptr) {
		throw std::runtime_error("Failed to create avio_context");
	}

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
		obs_source_output_video(
			static_cast<WHEPSource *>(opaque)->source, f);
	};

	info.path = "";
	info.format = nullptr;
	info.buffering = 1;
	info.speed = 100;
	info.hardware_decoding = false;
	info.ffmpeg_options = (char *)ffmpeg_options;
	info.is_local_file = false;
	info.full_decode = false;

	info.av_io_context_open = CreateAVIOContextSDP();
	info.av_io_context_playback = CreateAVIOContextRTP();

	this->media_video = media_playback_create(&info);
	media_playback_play(this->media_video, true, true);
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
	info.activate = [](void *priv_data) {
		static_cast<WHEPSource *>(priv_data)->Activate();
	};

	obs_register_source(&info);
}
