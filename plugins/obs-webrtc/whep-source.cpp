#include "whep-source.h"
#include "webrtc-utils.h"

#define do_log(level, format, ...)                              \
	blog(level, "[obs-webrtc] [whep_source: '%s'] " format, \
	     obs_source_get_name(source), ##__VA_ARGS__)

const auto buff_size_video = 1024 * 32;
const auto pli_interval = 500;
const auto ffmpeg_options = "";

const unsigned long stapaHeaderSize = 1;
const auto fuaHeaderSize = 2;
const auto rtpHeaderSize = 12;

const auto naluTypeBitmask = std::byte(0x1F);
const auto naluTypeSTAPA = std::byte(24);
const auto naluTypeFUA = std::byte(28);
const auto fuaEndBitmask = std::byte(0x40);
const auto naluRefIdcBitmask = std::byte(0x60);

std::vector<std::byte> h264_nalu_header()
{
	return std::vector<std::byte>{std::byte(0), std::byte(0), std::byte(0),
				      std::byte(1)};
}

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

AVIOContext *WHEPSource::CreateAVIOContextVideo()
{
	auto avio_context = avio_alloc_context(
		static_cast<unsigned char *>(av_malloc(buff_size_video)),
		buff_size_video, 0, this,
		[](void *opaque, uint8_t *buff, int buff_size) -> int {
			auto whep_source = static_cast<WHEPSource *>(opaque);
			std::vector<std::byte> current_buff;

			if (whep_source->partial_read.size() != 0) {
				current_buff = whep_source->partial_read;
			} else {
				current_buff = whep_source->video_queue.pop();
			}

			auto n = std::min(buff_size, int(current_buff.size()));
			memcpy(buff, current_buff.data(), n);

			if (n < int(current_buff.size())) {
				current_buff.erase(current_buff.begin(),
						   current_buff.begin() + n);
				whep_source->partial_read = current_buff;
			} else {
				whep_source->partial_read =
					std::vector<std::byte>();
			}

			return n;
		},
		nullptr, nullptr);

	avio_context->direct = 1;

	if (avio_context == nullptr) {
		throw std::runtime_error("Failed to create avio_context");
	}

	return avio_context;
}

void WHEPSource::DepacketizeH264()
{
	auto pkt = this->rtp_pkts_video[0];
	auto pktParsed = reinterpret_cast<const rtc::RtpHeader *>(pkt.data());
	auto headerSize = rtpHeaderSize + pktParsed->csrcCount() +
			  pktParsed->getExtensionHeaderSize();
	auto naluType = pkt.at(headerSize) & naluTypeBitmask;

	if (naluType > std::byte(0) && naluType < std::byte(24)) {
		auto h264_nalu = h264_nalu_header();
		std::copy(pkt.begin() + headerSize, pkt.end(),
			  std::back_inserter(h264_nalu));
		this->video_queue.push(h264_nalu);
	} else if (naluType == naluTypeSTAPA) {
		auto currOffset = stapaHeaderSize + headerSize;
		while (currOffset < pkt.size()) {
			auto h264_nalu = h264_nalu_header();

			auto naluSize = uint16_t(pkt.at(currOffset)) << 8 |
					uint8_t(pkt.at(currOffset + 1));

			currOffset += 2;

			if (pkt.size() < currOffset + naluSize) {
				throw std::runtime_error(
					"STAP-A declared size is larger then buffer");
			}

			std::copy(pkt.begin() + currOffset,
				  pkt.begin() + currOffset + naluSize,
				  std::back_inserter(h264_nalu));
			currOffset += naluSize;

			this->video_queue.push(h264_nalu);
		}
	} else if (naluType == naluTypeFUA) {
		if (fua_buffer.size() == 0) {
			fua_buffer = h264_nalu_header();
			fua_buffer.push_back(std::byte(0));
		}

		std::copy(pkt.begin() + headerSize + fuaHeaderSize, pkt.end(),
			  std::back_inserter(fua_buffer));

		if ((pkt.at(headerSize + 1) & fuaEndBitmask) != std::byte(0)) {
			auto naluRefIdc = pkt.at(headerSize) &
					  naluRefIdcBitmask;
			auto fragmentedNaluType = pkt.at(headerSize + 1) &
						  std::byte(naluTypeBitmask);

			fua_buffer[4] = naluRefIdc | fragmentedNaluType;

			this->video_queue.push(fua_buffer);
			fua_buffer = std::vector<std::byte>{};
		}
	} else {
		throw std::runtime_error("Unknown H264 RTP Packetization");
	}
}

void WHEPSource::OnMessageHandler(rtc::binary msg)
{
	this->rtp_pkts_video.push_back(msg);
	std::sort(this->rtp_pkts_video.begin(), this->rtp_pkts_video.end(),
		  [](auto const &pr1, auto const &pr2) {
			  auto pkt1 = reinterpret_cast<const rtc::RtpHeader *>(
				  pr1.data());
			  auto pkt2 = reinterpret_cast<const rtc::RtpHeader *>(
				  pr2.data());
			  return pkt2->seqNumber() > pkt1->seqNumber();
		  });

	while (true) {
		uint32_t currentTimestamp = 0;
		size_t i = 0;

		for (const auto &pkt : this->rtp_pkts_video) {
			auto p = reinterpret_cast<const rtc::RtpHeader *>(
				pkt.data());

			if (currentTimestamp == 0) {
				currentTimestamp = p->timestamp();
			} else if (currentTimestamp != p->timestamp()) {
				break;
			}

			i++;
		}

		if (i == this->rtp_pkts_video.size()) {
			break;
		}

		for (size_t j = 0; j < i; j++) {
			this->DepacketizeH264();

			this->rtp_pkts_video.erase(
				this->rtp_pkts_video.begin());
		}
	}
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
	info.hardware_decoding = true;
	info.ffmpeg_options = (char *)ffmpeg_options;
	info.is_local_file = false;
	info.full_decode = false;

	info.av_io_context_open = CreateAVIOContextVideo();

	this->media_video = media_playback_create(&info);
	media_playback_play(this->media_video, true, true);

	// TODO
	// info.av_io_context_open = CreateAudioAVIOContextSDP();
	// info.av_io_context_playback = CreateAudioAVIOContextRTP();

	// this->media_audio = media_playback_create(&info);
	// media_playback_play(this->media_audio, true, true);
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
