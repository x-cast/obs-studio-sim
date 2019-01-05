#include <obs-module.h>
#include <util/platform.h>
#include "FFmpegOutputUI.h"
#include "ffmpeg-ui-main.h"

using namespace std;

// Used for QVariant in codec comboboxes
namespace {
	static bool StringEquals(QString left, QString right)
	{
		return left == right;
	}
	struct FormatDesc {
		const char *name = nullptr;
		const char *mimeType = nullptr;
		const bool isDevice = false;
		const ff_format_desc *desc = nullptr;

		inline FormatDesc() = default;
		inline FormatDesc(const char *name, const char *mimeType,
			const bool isDevice, const ff_format_desc *desc = nullptr)
			: name(name), mimeType(mimeType), isDevice(isDevice), desc(desc) {}
		inline FormatDesc(const char *name, const char *mimeType,
			const ff_format_desc *desc = nullptr)
			: name(name), mimeType(mimeType), desc(desc) {}
		bool operator==(const FormatDesc &f) const
		{
			if (!StringEquals(name, f.name) || f.isDevice != isDevice)
				return false;
			return StringEquals(mimeType, f.mimeType);
		}
	};
	struct CodecDesc {
		const char *name = nullptr;
		int id = 0;

		inline CodecDesc() = default;
		inline CodecDesc(const char *name, int id) : name(name), id(id) {}

		bool operator==(const CodecDesc &codecDesc) const
		{
			if (id != codecDesc.id)
				return false;
			return StringEquals(name, codecDesc.name);
		}
	};
	struct DeviceDesc {
		const char *name = nullptr;
		const char *long_name = nullptr;
		const ff_device_desc *desc = nullptr;

		inline DeviceDesc() = default;
		inline DeviceDesc(const char *name, const char *long_name,
			const ff_device_desc *desc = nullptr)
			: name(name), long_name(long_name), desc(desc) {}

		bool operator==(const DeviceDesc &f) const
		{
			if (!StringEquals(name, f.name))
				return false;
			return StringEquals(long_name, f.long_name);
		}
	};
}
Q_DECLARE_METATYPE(FormatDesc)
Q_DECLARE_METATYPE(CodecDesc)
Q_DECLARE_METATYPE(DeviceDesc)

static void AddCodec(QComboBox *combo, const ff_codec_desc *codec_desc)
{
	QString itemText(ff_codec_desc_name(codec_desc));
	if (ff_codec_desc_is_alias(codec_desc))
		itemText += QString(" (%1)").arg(
			ff_codec_desc_base_name(codec_desc));

	CodecDesc cd(ff_codec_desc_name(codec_desc),
		ff_codec_desc_id(codec_desc));

	combo->addItem(itemText, qVariantFromValue(cd));
}

static int FindEncoder(QComboBox *combo, const char *name, int id)
{
	CodecDesc codecDesc(name, id);
	for (int i = 0; i < combo->count(); i++) {
		QVariant v = combo->itemData(i);
		if (!v.isNull()) {
			if (codecDesc == v.value<CodecDesc>()) {
				return i;
				break;
			}
		}
	}
	return -1;
}

static CodecDesc GetDefaultCodecDesc(const ff_format_desc *formatDesc,
	ff_codec_type codecType)
{
	int id = 0;
	switch (codecType) {
	case FF_CODEC_AUDIO:
		id = ff_format_desc_audio(formatDesc);
		break;
	case FF_CODEC_VIDEO:
		id = ff_format_desc_video(formatDesc);
		break;
	default:
		return CodecDesc();
	}

	return CodecDesc(ff_format_desc_get_default_name(formatDesc, codecType),
		id);
}

#define AV_ENCODER_DEFAULT_STR \
	QTStr(obs_module_text("FFmpeg.AVEncoderDefault"))

static void AddDefaultCodec(QComboBox *combo, const ff_format_desc *formatDesc,
	ff_codec_type codecType)
{
	CodecDesc cd = GetDefaultCodecDesc(formatDesc, codecType);

	int existingIdx = FindEncoder(combo, cd.name, cd.id);
	if (existingIdx >= 0)
		combo->removeItem(existingIdx);

	combo->addItem(QString("%1 (%2)").arg(cd.name, AV_ENCODER_DEFAULT_STR),
		qVariantFromValue(cd));
}

void FFmpegOutputUI::SelectEncoder(QComboBox *combo, const char *name, int id)
{
	int idx = FindEncoder(combo, name, id);
	if (idx >= 0)
		combo->setCurrentIndex(idx);
}

void FFmpegOutputUI::SaveEncoder(QComboBox *combo)
{
	QVariant v = combo->currentData();
	CodecDesc cd;
	if (!v.isNull())
		cd = v.value<CodecDesc>();
	if (combo == ui->advOutFFAEncoder) {
		config->audio_encoder_id = cd.id;
		if (cd.id != 0)
			config->audio_encoder = cd.name;
		else
			config->audio_encoder = nullptr;
	}
	if (combo == ui->advOutFFVEncoder) {
		config->video_encoder_id = cd.id;
		if (cd.id != 0)
			config->video_encoder = cd.name;
		else
			config->video_encoder = nullptr;
	}
}


static string ResString(uint32_t cx, uint32_t cy)
{
	std::stringstream res;
	res << cx << "x" << cy;
	return res.str();
}

/* some nice default output resolution vals */
static const double vals[] =
{
	1.0,
	1.25,
	(1.0 / 0.75),
	1.5,
	(1.0 / 0.6),
	1.75,
	2.0,
	2.25,
	2.5,
	2.75,
	3.0
};

static const size_t numVals = sizeof(vals) / sizeof(double);

/* parses "[width]x[height]", string, i.e. 1024x768 */
bool FFmpegOutputUI::ConvertResText(const char *res, uint32_t &cx, uint32_t &cy)
{
	BaseLexer lex;
	base_token token;

	lexer_start(lex, res);

	/* parse width */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	cx = std::stoul(token.text.array);

	/* parse 'x' */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (strref_cmpi(&token.text, "x") != 0)
		return false;

	/* parse height */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	cy = std::stoul(token.text.array);

	/* shouldn't be any more tokens after this */
	if (lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;

	return true;
}

void FFmpegOutputUI::RecalcOutputResPixels(const char *resText)
{
	uint32_t newCX;
	uint32_t newCY;

	ConvertResText(resText, newCX, newCY);
	if (newCX && newCY) {
		config->scale_width = newCX;
		config->scale_height = newCY;
	}
}

void FFmpegOutputUI::ResetDownscales()
{
	QString advRescale;
	QString advRecRescale;
	QString advFFRescale;
	QString oldOutputRes;
	string bestScale;
	int bestPixelDiff = 0x7FFFFFFF;
	uint32_t cx = config->width;
	uint32_t cy = config->height;
	uint32_t out_cx = config->scale_width;
	uint32_t out_cy = config->scale_height;

	advFFRescale = ui->advOutFFRescale->lineEdit()->text();

	ui->advOutFFRescale->clear();

	oldOutputRes = QString::number(out_cx) + "x" +
			QString::number(out_cy);

	for (size_t idx = 0; idx < numVals; idx++) {
		uint32_t downscaleCX = uint32_t(double(cx) / vals[idx]);
		uint32_t downscaleCY = uint32_t(double(cy) / vals[idx]);
		uint32_t outDownscaleCX = uint32_t(double(out_cx) / vals[idx]);
		uint32_t outDownscaleCY = uint32_t(double(out_cy) / vals[idx]);

		downscaleCX &= 0xFFFFFFFC;
		downscaleCY &= 0xFFFFFFFE;
		outDownscaleCX &= 0xFFFFFFFE;
		outDownscaleCY &= 0xFFFFFFFE;

		string res = ResString(downscaleCX, downscaleCY);
		string outRes = ResString(outDownscaleCX, outDownscaleCY);
		ui->advOutFFRescale->addItem(outRes.c_str());

		/* always try to find the closest output resolution to the
		 * previously set output resolution */
		int newPixelCount = int(downscaleCX * downscaleCY);
		int oldPixelCount = int(out_cx * out_cy);
		int diff = abs(newPixelCount - oldPixelCount);

		if (diff < bestPixelDiff) {
			bestScale = res;
			bestPixelDiff = diff;
		}
	}

	string res = ResString(cx, cy);

	if (advFFRescale.isEmpty())
		advFFRescale = res.c_str();

	ui->advOutFFRescale->lineEdit()->setText(advFFRescale);
}
#define AV_ENCODER_DISABLE_STR \
	QTStr(obs_module_text("FFmpeg.AVEncoderDisable"))

FFmpegOutputUI::FFmpegOutputUI(QWidget *parent)
		: QDialog(parent),
		  ui(new Ui_Output)
{
	ui->setupUi(this);
	config = new ffmpeg_cfg;

	connect(ui->startOutput, SIGNAL(released()), this, SLOT(StartOutput()));
	connect(ui->stopOutput, SIGNAL(released()), this, SLOT(StopOutput()));
}

void FFmpegOutputUI::SetupFilenameCompleter()
{
	QStringList specList = QT_UTF8(obs_module_text("FilenameFormatting.completer")).split(QRegularExpression("\n"));
	QCompleter *specCompleter = new QCompleter(specList);
	specCompleter->setCaseSensitivity(Qt::CaseSensitive);
	specCompleter->setFilterMode(Qt::MatchContains);
	ui->filenameFormatting->setCompleter(specCompleter);
}

FFmpegOutputUI::~FFmpegOutputUI()
{
	delete ui->filenameFormatting->completer();
	delete config;
}

void FFmpegOutputUI::ShowHideDialog()
{
	setVisible(!isVisible());
}

void FFmpegOutputUI::Save()
{
	bool ret = save_ffmpeg_data();
	if (!ret)
		warn("Save folder is not set or does not exist.\n");
}

void FFmpegOutputUI::Load()
{
	load_ffmpeg_data();
}

void FFmpegOutputUI::SelectFormat(QComboBox *combo, const char *name,
	const char *mimeType)
{
	FormatDesc formatDesc(name, mimeType);

	for (int i = 0; i < combo->count(); i++) {
		QVariant v = combo->itemData(i);
		if (!v.isNull()) {
			if (formatDesc == v.value<FormatDesc>()) {
				combo->setCurrentIndex(i);
				return;
			}
		}
	}

	combo->setCurrentIndex(0);
}

bool FFmpegOutputUI::FormatIsDevice(QComboBox *combo)
{
	QVariant v = combo->currentData();
	if (!v.isNull()) {
		FormatDesc desc = v.value<FormatDesc>();
		return desc.isDevice;
	}
	return false;
}

void FFmpegOutputUI::StartOutput()
{
	Save();
	output_start();
}

void FFmpegOutputUI::StopOutput()
{
	output_stop();
}


#define AV_FORMAT_DEFAULT_STR \
	QTStr(obs_module_text("FFmpeg.FormatDefault"))
#define AUDIO_STR \
	QTStr(obs_module_text("FFmpeg.FormatAudio"))
#define VIDEO_STR \
	QTStr(obs_module_text("FFmpeg.FormatVideo"))
#define AV_NO_DEVICE_STR \
	QTStr(obs_module_text("FFmpeg.NoDevice"))

void FFmpegOutputUI::LoadFormats()
{
	ui->advOutFFFormat->clear();
	ui->advOutFFFormat->blockSignals(true);
	ff_init();
	formats.reset(ff_format_supported());
	const ff_format_desc *format = formats.get();

	while (format != nullptr) {
		bool audio = ff_format_desc_has_audio(format);
		bool video = ff_format_desc_has_video(format);
		FormatDesc formatDesc(ff_format_desc_name(format),
				ff_format_desc_mime_type(format),
				ff_format_desc_is_device(format), format);
		if (audio || video) {
			QString itemText(ff_format_desc_name(format));
			if (audio ^ video)
				itemText += QString(" (%1)").arg(
					audio ? AUDIO_STR : VIDEO_STR);

			ui->advOutFFFormat->addItem(itemText,
				qVariantFromValue(formatDesc));
		}

		format = ff_format_desc_next(format);
	}

	ui->advOutFFFormat->model()->sort(0);

	ui->advOutFFFormat->insertItem(0, AV_FORMAT_DEFAULT_STR);

	ui->advOutFFFormat->blockSignals(false);
}


bool FFmpegOutputUI::LoadDeviceList()
{

	if (!FormatIsDevice(ui->advOutFFFormat))
		return false;
	QVariant v = ui->advOutFFFormat->currentData();
	FormatDesc desc = v.value<FormatDesc>();
	const char *name = ff_format_desc_name(desc.desc);
	ui->advOutFFDeviceList->blockSignals(true);
	ui->advOutFFDeviceList->clear();
	OBSFFDeviceDesc deviceDescs(ff_get_device_list(name));
	const ff_device_desc *device = deviceDescs.get();

	ui->advOutFFDeviceList->insertItem(0, AV_NO_DEVICE_STR);
	while (device != nullptr) {
		DeviceDesc deviceDesc(ff_device_desc_name(device),
			ff_device_desc_long_name(device), device);
		QString itemText(ff_device_desc_long_name(device));
		ui->advOutFFDeviceList->addItem(itemText,
			qVariantFromValue(deviceDesc));

		device = ff_device_desc_next(device);
	}

	ui->advOutFFDeviceList->blockSignals(false);
	return true;
}

void FFmpegOutputUI::SaveFormat(QComboBox *combo)
{
	QVariant v = combo->currentData();
	if (!v.isNull()) {
		FormatDesc desc          = v.value<FormatDesc>();
		config->format_name      = desc.name;
		config->format_mime_type = desc.mimeType;

		const char *ext = ff_format_desc_extensions(desc.desc);
		string extStr = ext ? ext : "";

		char *comma = strchr(&extStr[0], ',');
		if (comma)
			*comma = 0;

		config->format_extension = extStr.c_str();
	} else {
		config->format_name      = nullptr;
		config->format_mime_type = nullptr;
		config->format_extension = nullptr;
	}
}

void FFmpegOutputUI::SaveDevice(QComboBox *combo)
{
	QVariant v = combo->currentData();
	if (!v.isNull()) {
		DeviceDesc desc     = v.value<DeviceDesc>();
		config->device_id   = desc.name;
		config->device_name = desc.long_name;
	} else {
		config->device_id   = nullptr;
		config->device_name = nullptr;
	}
}

void FFmpegOutputUI::SelectDevice(QComboBox *combo, const char *name, const char *id)
{
	if (name) {
		DeviceDesc deviceDesc(id, name, NULL);
		for (int i = 0; i < combo->count(); i++) {
			QVariant v = combo->itemData(i);
			if (!v.isNull()) {
				if (deviceDesc == v.value<DeviceDesc>()) {
					combo->setCurrentIndex(i);
					return;
				}
			}
		}
	}

	combo->setCurrentIndex(0);
}

void FFmpegOutputUI::SetAdvOutputFFmpegEnablement(
	ff_codec_type encoderType, bool enabled,
	bool enableEncoder)
{
	bool rescale = config->rescale;

	switch (encoderType) {
	case FF_CODEC_VIDEO:
		ui->advOutFFVBitrate->setEnabled(enabled);
		ui->advOutFFVGOPSize->setEnabled(enabled);
		ui->advOutFFUseRescale->setEnabled(enabled);
		ui->advOutFFRescale->setEnabled(enabled && rescale);
		ui->advOutFFVEncoder->setEnabled(enabled || enableEncoder);
		ui->advOutFFVCfg->setEnabled(enabled);
		break;
	case FF_CODEC_AUDIO:
		ui->advOutFFABitrate->setEnabled(enabled);
		ui->advOutFFAEncoder->setEnabled(enabled || enableEncoder);
		ui->advOutFFACfg->setEnabled(enabled);
		ui->advOutFFTrack1->setEnabled(enabled);
		ui->advOutFFTrack2->setEnabled(enabled);
		ui->advOutFFTrack3->setEnabled(enabled);
		ui->advOutFFTrack4->setEnabled(enabled);
		ui->advOutFFTrack5->setEnabled(enabled);
		ui->advOutFFTrack6->setEnabled(enabled);
	default:
		break;
	}
}

void FFmpegOutputUI::ReloadCodecs(const ff_format_desc *formatDesc)
{
	ui->advOutFFAEncoder->blockSignals(true);
	ui->advOutFFVEncoder->blockSignals(true);
	ui->advOutFFAEncoder->clear();
	ui->advOutFFVEncoder->clear();

	if (formatDesc == nullptr)
		return;

	bool ignore_compatability = ui->advOutFFIgnoreCompat->isChecked();
	OBSFFCodecDesc codecDescs(ff_codec_supported(formatDesc,
		ignore_compatability));

	const ff_codec_desc *codec = codecDescs.get();

	while (codec != nullptr) {
		switch (ff_codec_desc_type(codec)) {
		case FF_CODEC_AUDIO:
			AddCodec(ui->advOutFFAEncoder, codec);
			break;
		case FF_CODEC_VIDEO:
			AddCodec(ui->advOutFFVEncoder, codec);
			break;
		default:
			break;
		}

		codec = ff_codec_desc_next(codec);
	}

	if (ff_format_desc_has_audio(formatDesc))
		AddDefaultCodec(ui->advOutFFAEncoder, formatDesc,
			FF_CODEC_AUDIO);
	if (ff_format_desc_has_video(formatDesc))
		AddDefaultCodec(ui->advOutFFVEncoder, formatDesc,
			FF_CODEC_VIDEO);

	ui->advOutFFAEncoder->model()->sort(0);
	ui->advOutFFVEncoder->model()->sort(0);

	QVariant disable = qVariantFromValue(CodecDesc());

	ui->advOutFFAEncoder->insertItem(0, AV_ENCODER_DISABLE_STR, disable);
	ui->advOutFFVEncoder->insertItem(0, AV_ENCODER_DISABLE_STR, disable);

	ui->advOutFFAEncoder->blockSignals(false);
	ui->advOutFFVEncoder->blockSignals(false);
}

void FFmpegOutputUI::on_advOutFFType_currentIndexChanged(int idx)
{
	config->output_type = ui->advOutFFType->currentIndex();
	ui->advOutFFNoSpace->setHidden(idx != 0);
	ui->overwriteIfExists->setHidden(idx != 0);
	ui->filenameFormatting->setHidden(idx != 0);
	ui->label_3->setHidden(idx != 0);
}

void FFmpegOutputUI::on_advOutFFPathBrowse_clicked()
{
	QString dir = QFileDialog::getExistingDirectory(this, QTStr("FFmpeg.SelectDirectory"),
		ui->advOutFFRecPath->text(),
		QFileDialog::ShowDirsOnly |
		QFileDialog::DontResolveSymlinks);
	if (dir.isEmpty())
		return;

	ui->advOutFFRecPath->setText(dir);
}

void FFmpegOutputUI::on_advOutFFIgnoreCompat_stateChanged(int)
{
	/* Little hack to reload codecs when checked */
	on_advOutFFFormat_currentIndexChanged(
		ui->advOutFFFormat->currentIndex());
	config->ignore_codec_compat = ui->advOutFFIgnoreCompat->isChecked();
}

#define DEFAULT_CONTAINER_STR \
	QTStr(obs_module_text("FFmpeg.FormatDescDef"))

void FFmpegOutputUI::on_advOutFFFormat_currentIndexChanged(int idx)
{
	const QVariant itemDataVariant = ui->advOutFFFormat->itemData(idx);

	if (!itemDataVariant.isNull()) {
		FormatDesc desc = itemDataVariant.value<FormatDesc>();
		SetAdvOutputFFmpegEnablement(FF_CODEC_AUDIO,
			ff_format_desc_has_audio(desc.desc),
			false);
		SetAdvOutputFFmpegEnablement(FF_CODEC_VIDEO,
			ff_format_desc_has_video(desc.desc),
			false);
		ReloadCodecs(desc.desc);
		ui->advOutFFFormatDesc->setText(ff_format_desc_long_name(
			desc.desc));
		config->format_name = ff_format_desc_name(desc.desc);
		config->format_mime_type = ff_format_desc_mime_type(desc.desc);
		CodecDesc defaultAudioCodecDesc =
			GetDefaultCodecDesc(desc.desc, FF_CODEC_AUDIO);
		CodecDesc defaultVideoCodecDesc =
			GetDefaultCodecDesc(desc.desc, FF_CODEC_VIDEO);
		SelectEncoder(ui->advOutFFAEncoder, defaultAudioCodecDesc.name,
			defaultAudioCodecDesc.id);
		SelectEncoder(ui->advOutFFVEncoder, defaultVideoCodecDesc.name,
			defaultVideoCodecDesc.id);

		ui->label_1->setHidden(desc.isDevice);
		if (desc.isDevice)
			ui->stackedWidget->setCurrentIndex(2);
		else
			ui->stackedWidget->setCurrentIndex(ui->advOutFFType->currentIndex());
		ui->advOutFFNoSpace->setHidden(desc.isDevice);
		ui->label_3->setHidden(desc.isDevice);
		ui->filenameFormatting->setHidden(desc.isDevice);
		ui->overwriteIfExists->setHidden(desc.isDevice);
		ui->filenameFormatting->setHidden(desc.isDevice);
		ui->advOutFFDeviceList->setHidden(!desc.isDevice);
		if (desc.isDevice) {
			if (LoadDeviceList())
				debug("Device list loaded correctly.\n");
			else
				warn("Device list could not be loaded.\n");
		}
	}
	else {
		ReloadCodecs(nullptr);
		ui->advOutFFFormatDesc->setText(DEFAULT_CONTAINER_STR);
	}
}

void FFmpegOutputUI::on_advOutFFAEncoder_currentIndexChanged(int idx)
{
	const QVariant itemDataVariant = ui->advOutFFAEncoder->itemData(idx);
	if (!itemDataVariant.isNull()) {
		CodecDesc desc = itemDataVariant.value<CodecDesc>();
		SetAdvOutputFFmpegEnablement(FF_CODEC_AUDIO,
			desc.id != 0 || desc.name != nullptr, true);
	}
}

void FFmpegOutputUI::on_advOutFFVEncoder_currentIndexChanged(int idx)
{
	const QVariant itemDataVariant = ui->advOutFFVEncoder->itemData(idx);
	if (!itemDataVariant.isNull()) {
		CodecDesc desc = itemDataVariant.value<CodecDesc>();
		SetAdvOutputFFmpegEnablement(FF_CODEC_VIDEO,
			desc.id != 0 || desc.name != nullptr, true);
	}
}

void FFmpegOutputUI::on_apply_clicked()
{
	Save();
}


void FFmpegOutputUI::on_cancel_clicked()
{
	Load();
}


void FFmpegOutputUI::on_close_clicked()
{
	this->close();
}

