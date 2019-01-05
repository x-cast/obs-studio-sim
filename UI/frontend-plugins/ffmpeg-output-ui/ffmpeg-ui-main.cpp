#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QAction>
#include <util/util.hpp>
#include <util/platform.h>
#include "FFmpegOutputUI.h"
#include "ffmpeg-ui-main.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("ffmpeg-output-ui", "en-US")

FFmpegOutputUI *doUI;
obs_output_t *output;
static ffmpeg_cfg *config = nullptr;
bool main_output_running = false;

static void ensure_directory_exists(std::string path)
{
	replace(path.begin(), path.end(), '\\', '/');

	size_t last = path.rfind('/');
	if (last == std::string::npos)
		return;

	std::string directory = path.substr(0, last);
	os_mkdirs(directory.c_str());
}

static void FindBestFilename(std::string strPath, bool noSpace)
{
	int num = 2;

	if (!os_file_exists(strPath.c_str()))
		return;

	const char *ext = strrchr(strPath.c_str(), '.');
	if (!ext)
		return;

	int extStart = int(ext - strPath.c_str());
	for (;;) {
		std::string testPath = strPath;
		std::string numStr;

		numStr = noSpace ? "_" : " (";
		numStr += std::to_string(num++);
		if (!noSpace)
			numStr += ")";

		testPath.insert(extStart, numStr);

		if (!os_file_exists(testPath.c_str())) {
			strPath = testPath;
			break;
		}
	}
}
 
bool save_ffmpeg_data()
{
	obs_data_t *obj = obs_data_create();

	config->output_type = doUI->ui->advOutFFType->currentIndex();
	std::string urlstr = ((QString)doUI->ui->advOutFFURL->text()).toStdString();
	config->url = urlstr.c_str();
	std::string  path = ((QString)doUI->ui->advOutFFRecPath->text()).toStdString();
	config->path = path.c_str();
	std::string filename_format = doUI->ui->filenameFormatting->text().toStdString();
	config->filename_formatting = filename_format.c_str();
	config->name_without_space = doUI->ui->advOutFFNoSpace->isChecked();
	config->overwrite_file = doUI->ui->overwriteIfExists->isChecked();
	doUI->SaveFormat(doUI->ui->advOutFFFormat);
	config->is_device = doUI->FormatIsDevice(doUI->ui->advOutFFFormat);
	doUI->SaveDevice(doUI->ui->advOutFFDeviceList);

	/* build filepath */
	os_dir_t *dir = config->path && config->path[0] ? os_opendir(config->path) : nullptr;
	QMainWindow *main = (QMainWindow *)obs_frontend_get_main_window();
	std::string strPath;

	if (!dir) {
		if (main->isVisible())
			QMessageBox::information(main,
				QTStr(obs_module_text("Output.BadPath.Title")),
				QTStr(obs_module_text("Output.BadPath.Text")));
		return false;
	}  else {
		os_closedir(dir);


		strPath += path;

		char lastChar = strPath.back();
		if (lastChar != '/' && lastChar != '\\')
			strPath += "/";

		BPtr<char> filename = os_generate_formatted_filename(config->format_extension,
			!config->name_without_space, config->filename_formatting);
		strPath += filename;

		ensure_directory_exists(strPath);
		if (!config->overwrite_file) 
			FindBestFilename(strPath, config->name_without_space);
	}
	/*-----------------------------------------------------------*/

	std::string muxer_settings = ((QString)doUI->ui->advOutFFMCfg->text()).toStdString();
	config->muxer_settings = muxer_settings.c_str();
	config->video_bitrate = doUI->ui->advOutFFVBitrate->value();
	config->audio_bitrate = doUI->ui->advOutFFABitrate->value();
	config->gop_size = doUI->ui->advOutFFVGOPSize->value();
	std::string optionsV = ((QString)doUI->ui->advOutFFVCfg->text()).toStdString();
	config->video_settings = optionsV.c_str();
	std::string optionsA = ((QString)doUI->ui->advOutFFACfg->text()).toStdString();
	config->audio_settings = optionsA.c_str();
	config->rescale = doUI->ui->advOutFFUseRescale->isChecked();
	config->rescaleRes = QT_TO_UTF8(doUI->ui->advOutFFRescale->currentText());
	if (config->rescale && config->rescaleRes)
		doUI->RecalcOutputResPixels(config->rescaleRes);
	if (!config->rescale) {
		obs_video_info ovi;
		obs_get_video_info(&ovi);
		config->scale_width = ovi.output_width;
		config->scale_height = ovi.output_height;
	}

	doUI->SaveEncoder(doUI->ui->advOutFFAEncoder);
	doUI->SaveEncoder(doUI->ui->advOutFFVEncoder);
	config->audio_mixes = (doUI->ui->advOutFFTrack1->isChecked() ? (1 << 0) : 0) |
		(doUI->ui->advOutFFTrack2->isChecked() ? (1 << 1) : 0) |
		(doUI->ui->advOutFFTrack3->isChecked() ? (1 << 2) : 0) |
		(doUI->ui->advOutFFTrack4->isChecked() ? (1 << 3) : 0) |
		(doUI->ui->advOutFFTrack5->isChecked() ? (1 << 4) : 0) |
		(doUI->ui->advOutFFTrack6->isChecked() ? (1 << 5) : 0);

	obs_data_set_int(obj, "output_type", config->output_type);
	obs_data_set_string(obj, "url", config->output_type ? config->url : strPath.c_str());
	obs_data_set_string(obj, "path", config->path);
	obs_data_set_string(obj, "FFurl", config->url);
	obs_data_set_bool(obj, "is_device", config->is_device);
	obs_data_set_string(obj, "device_name", config->device_name);
	obs_data_set_string(obj, "device_id", config->device_id);
	obs_data_set_string(obj, "filename_formatting", config->filename_formatting);
	obs_data_set_bool(obj, "overwrite_file", config->overwrite_file);
	obs_data_set_bool(obj, "name_without_space", config->name_without_space);
	obs_data_set_string(obj, "format_name", config->format_name);
	obs_data_set_string(obj, "format_mime_type", config->format_mime_type);
	obs_data_set_string(obj, "format_extension", config->format_extension);
	obs_data_set_string(obj, "muxer_settings", config->muxer_settings);
	obs_data_set_int(obj, "video_bitrate", config->video_bitrate);
	obs_data_set_int(obj, "audio_bitrate", config->audio_bitrate);
	obs_data_set_int(obj, "gop_size", config->gop_size);
	obs_data_set_bool(obj, "rescale", config->rescale);
	obs_data_set_string(obj, "rescaleRes", config->rescaleRes);
	obs_data_set_bool(obj, "ignore_codec_compat", config->ignore_codec_compat);
	obs_data_set_string(obj, "video_encoder", config->video_encoder);
	obs_data_set_int(obj, "video_encoder_id", config->video_encoder_id);
	obs_data_set_string(obj, "audio_encoder", config->audio_encoder);
	obs_data_set_int(obj, "audio_encoder_id", config->audio_encoder_id);
	obs_data_set_string(obj, "video_settings", config->video_settings);
	obs_data_set_string(obj, "audio_settings", config->audio_settings);
	obs_data_set_int(obj, "audio_mixes", config->audio_mixes);
	obs_data_set_int(obj, "scale_width", config->scale_width);
	obs_data_set_int(obj, "scale_height", config->scale_height);
	obs_data_set_int(obj, "width", config->width);
	obs_data_set_int(obj, "height", config->height);

	char *modulePath = obs_module_get_config_path(obs_current_module(), "");
	os_mkdirs(modulePath);
	char *jsonpath = obs_module_get_config_path(obs_current_module(),
		"ffmpegOutputProps.json");
	if (obj)
		obs_data_save_json_safe(obj, jsonpath, "tmp", "bak");
	obs_data_release(obj);
	bfree(jsonpath);
	bfree(modulePath);
	return true;
}

void load_ffmpeg_data()
{
	char *path = obs_module_get_config_path(obs_current_module(),
		"ffmpegOutputProps.json");
	BPtr<char> jsonData = os_quick_read_utf8_file(path);
	OBSData dataRet = nullptr;
	if (!!jsonData) {
		obs_data_t *data = obs_data_create_from_json(jsonData);
		dataRet = data;
		obs_data_release(data);
	}
	bfree(path);
	obs_data_t *obj = dataRet;
	if (!obj)
		obj = obs_data_create();

	obs_data_set_default_string(obj, "filename_formatting",
		"%CCYY-%MM-%DD %hh-%mm-%ss");
	obs_data_set_default_int(obj, "video_bitrate", 2500);
	obs_data_set_default_int(obj, "audio_bitrate", 160);
	obs_data_set_default_int(obj, "gop_size", 250);
	obs_data_set_default_bool(obj, "rescale", false);
	obs_data_set_default_bool(obj, "ignore_codec_compat", false);
	obs_data_set_default_int(obj, "audio_mixes", 1);
	obs_data_set_default_bool(obj, "name_without_space", true);
	obs_data_set_default_bool(obj, "overwrite_file", true);
	obs_data_set_default_bool(obj, "is_device", false);

	config->output_type = obs_data_get_int(obj, "output_type");
	config->url = obs_data_get_string(obj, "FFurl");
	config->path = obs_data_get_string(obj, "path");
	config->is_device = obs_data_get_bool(obj, "is_device");
	config->device_name = obs_data_get_string(obj, "device_name");
	config->device_id = obs_data_get_string(obj, "device_id");
	config->filename_formatting = obs_data_get_string(obj, "filename_formatting");
	config->overwrite_file = obs_data_get_bool(obj, "overwrite_file");
	config->name_without_space = obs_data_get_bool(obj, "name_without_space");
	config->format_name = obs_data_get_string(obj, "format_name");
	config->format_mime_type = obs_data_get_string(obj, "format_mime_type");
	config->format_extension = obs_data_get_string(obj, "format_extension");
	config->muxer_settings = obs_data_get_string(obj, "muxer_settings");
	config->video_bitrate = obs_data_get_int(obj, "video_bitrate");
	config->audio_bitrate = obs_data_get_int(obj, "audio_bitrate");
	config->gop_size = obs_data_get_int(obj, "gop_size");
	config->rescale = obs_data_get_bool(obj, "rescale");
	config->rescaleRes = obs_data_get_string(obj, "rescaleRes");
	config->ignore_codec_compat = obs_data_get_bool(obj, "ignore_codec_compat");
	config->video_encoder = obs_data_get_string(obj, "video_encoder");
	config->video_encoder_id = obs_data_get_int(obj, "video_encoder_id");
	config->audio_encoder = obs_data_get_string(obj, "audio_encoder");
	config->audio_encoder_id = obs_data_get_int(obj, "audio_encoder_id");
	config->video_settings = obs_data_get_string(obj, "video_settings");
	config->audio_settings = obs_data_get_string(obj, "audio_settings");
	config->audio_mixes = obs_data_get_int(obj, "audio_mixes");
	if (config->rescale && config->rescaleRes) {
		config->scale_width = obs_data_get_int(obj, "scale_width");
		config->scale_height = obs_data_get_int(obj, "scale_height");
	}

	if (!doUI->ui->advOutFFFormat->count())
		doUI->LoadFormats();
	doUI->ui->advOutFFType->setCurrentIndex(config->output_type);
	doUI->ui->advOutFFRecPath->setText(QT_UTF8(config->path));
	doUI->ui->advOutFFURL->setText(QT_UTF8(config->url));

	doUI->SetupFilenameCompleter();
	doUI->ui->filenameFormatting->setToolTip(QT_UTF8(obs_module_text("FilenameFormatting.TT")));
	doUI->ui->filenameFormatting->setText(QT_UTF8(config->filename_formatting));
	doUI->SelectFormat(doUI->ui->advOutFFFormat, config->format_name, config->format_mime_type);
	doUI->SelectDevice(doUI->ui->advOutFFDeviceList, config->device_name, config->device_id);
	doUI->ui->advOutFFMCfg->setText(config->muxer_settings);
	doUI->ui->advOutFFVBitrate->setValue(config->video_bitrate);
	doUI->ui->advOutFFVGOPSize->setValue(config->gop_size);
	doUI->ui->advOutFFUseRescale->setChecked(config->rescale);
	doUI->ui->advOutFFIgnoreCompat->setChecked(config->ignore_codec_compat);
	doUI->ui->advOutFFRescale->setEnabled(config->rescale);
	doUI->ui->advOutFFRescale->setCurrentText(config->rescaleRes);
	if (config->rescale && config->rescaleRes)
		doUI->RecalcOutputResPixels(config->rescaleRes);
	doUI->ResetDownscales();
	if (!doUI->ui->advOutFFVEncoder->count() || !doUI->ui->advOutFFVEncoder->count())
		doUI->on_advOutFFFormat_currentIndexChanged(
			doUI->ui->advOutFFFormat->currentIndex());
	doUI->SelectEncoder(doUI->ui->advOutFFVEncoder, config->video_encoder, config->video_encoder_id);
	doUI->ui->advOutFFVCfg->setText(config->video_settings);
	doUI->ui->advOutFFABitrate->setValue(config->audio_bitrate);
	doUI->SelectEncoder(doUI->ui->advOutFFAEncoder, config->audio_encoder, config->audio_encoder_id);
	doUI->ui->advOutFFACfg->setText(config->audio_settings);

	doUI->ui->advOutFFTrack1->setChecked(config->audio_mixes & (1 << 0));
	doUI->ui->advOutFFTrack2->setChecked(config->audio_mixes & (1 << 1));
	doUI->ui->advOutFFTrack3->setChecked(config->audio_mixes & (1 << 2));
	doUI->ui->advOutFFTrack4->setChecked(config->audio_mixes & (1 << 3));
	doUI->ui->advOutFFTrack5->setChecked(config->audio_mixes & (1 << 4));
	doUI->ui->advOutFFTrack6->setChecked(config->audio_mixes & (1 << 5));

	obs_data_release(obj);
}

OBSData load_settings()
{
	char *path = obs_module_get_config_path(obs_current_module(),
		"ffmpegOutputProps.json");
	BPtr<char> jsonData = os_quick_read_utf8_file(path);
	bfree(path);
	if (!!jsonData) {
		obs_data_t *data = obs_data_create_from_json(jsonData);
		OBSData dataRet(data);
		obs_data_release(data);

		return dataRet;
	}
	return nullptr;
}

void output_start()
{
	if (!main_output_running) {
		OBSData settings = load_settings();
		if (settings != nullptr) {
			output = obs_output_create("ffmpeg_output", 
					"ffmpeg_output", settings, NULL);
			obs_output_set_mixers(output, config->audio_mixes);
			obs_output_start(output);
			main_output_running = true;
		}
	}
}

void output_stop()
{
	if (main_output_running) {
		obs_output_stop(output);
		obs_data_t *settings = obs_output_get_settings(output);
		obs_data_release(settings);
		obs_output_release(output);
		main_output_running = false;
	}
}

void addOutputUI(void)
{
	QAction *action = (QAction*)obs_frontend_add_tools_menu_qaction(
			obs_module_text("FFmpeg Output"));

	obs_frontend_push_ui_translation(obs_module_get_string);

	QMainWindow *window = (QMainWindow*)obs_frontend_get_main_window();
	
	doUI = new FFmpegOutputUI(window);
	config = doUI->config;

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	config->width = ovi.base_width;
	config->height = ovi.base_height;
	config->scale_width = ovi.output_width;
	config->scale_height = ovi.output_height;

	auto cb = []() {
		obs_frontend_push_ui_translation(obs_module_get_string);
		doUI->ShowHideDialog();
		obs_frontend_pop_ui_translation();
	};

	obs_frontend_pop_ui_translation();
	load_ffmpeg_data();//initial loading
	action->connect(action, &QAction::triggered, cb);
}

bool obs_module_load(void)
{
	addOutputUI();

	return true;
}

void obs_module_unload(void)
{

}
