#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QFileDialog>
#include <QDirIterator>
#include <QVariant>
#include <QMessageBox>
#include <QCompleter>
#include <libff/ff-util.h>
#include <sstream>
#include <util/lexer.h>
#include "ui_output.h"
#include "../../UI/properties-view.hpp"

#define QTStr(lookupVal) QString::fromUtf8(lookupVal)

/* The ffmpeg output uses a 'url' setting both for urls and filepaths.
 * This can be quite confusing. The next struct passes pathurl member to ffmpeg
 * output; and stores a file path in 'path' and a url in 'FFurl'.
 */
struct ffmpeg_cfg {
	int                output_type;
	const char         *url;//stored as FFurl in json
	const char         *path;//stored in json as path ==> this is actually a dir
	const char         *device_name;
	const char         *device_id;
	const char         *format_name;
	bool               is_device;
	const char         *format_mime_type;
	const char         *format_extension; // this and the next 3 members serve to build filepath stored in pathurl
	const char         *filename_formatting;
	bool               overwrite_file;
	bool               name_without_space;
	const char         *muxer_settings;
	int                gop_size;
	bool               rescale;
	const char         *rescaleRes;
	bool               ignore_codec_compat;
	int                video_bitrate;
	int                audio_bitrate;
	const char         *video_encoder;
	int                video_encoder_id;
	const char         *audio_encoder;
	int                audio_encoder_id;
	const char         *video_settings;
	const char         *audio_settings;
	int                audio_mixes;
	uint32_t           scale_width;
	uint32_t           scale_height;
	uint32_t           width;
	uint32_t           height;
};

struct BaseLexer {
	lexer lex;
public:
	inline BaseLexer() { lexer_init(&lex); }
	inline ~BaseLexer() { lexer_free(&lex); }
	operator lexer*() { return &lex; }
};

class OBSFFDeleter
{
public:
	void operator()(const ff_format_desc *format)
	{
		ff_format_desc_free(format);
	}
	void operator()(const ff_codec_desc *codec)
	{
		ff_codec_desc_free(codec);
	}
	void operator()(const ff_device_desc *device)
	{
		ff_device_desc_free(device);
	}
};

using OBSFFCodecDesc = std::unique_ptr<const ff_codec_desc,
	OBSFFDeleter>;
using OBSFFFormatDesc = std::unique_ptr<const ff_format_desc,
	OBSFFDeleter>;
using OBSFFDeviceDesc = std::unique_ptr<const ff_device_desc,
	OBSFFDeleter>;

class OBSMessageBox {
public:
	static QMessageBox::StandardButton question(
		QWidget *parent,
		const QString &title,
		const QString &text,
		QMessageBox::StandardButtons buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No),
		QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
	static void information(
		QWidget *parent,
		const QString &title,
		const QString &text);
};

class FFmpegOutputUI : public QDialog {
Q_OBJECT
private:

	OBSFFFormatDesc formats;
	void SetAdvOutputFFmpegEnablement(
		ff_codec_type encoderType, bool enabled,
		bool enableEncode = false);

public:
	struct ffmpeg_cfg *config;
	std::unique_ptr<Ui_Output> ui;
	FFmpegOutputUI(QWidget *parent);
	~FFmpegOutputUI();
	void ShowHideDialog();
	void LoadFormats();
	bool LoadDeviceList();
	bool FormatIsDevice(QComboBox *combo);
	void ReloadCodecs(const ff_format_desc *formatDesc);
	void Load();
	void Save();
	void SetupFilenameCompleter();
	void SelectFormat(QComboBox *combo, const char *name,
			const char *mimeType);
	void SaveFormat(QComboBox *combo);
	void SaveDevice(QComboBox *combo);
	void SelectDevice(QComboBox *combo, const char *name, const char *id);
	void RecalcOutputResPixels(const char *resText);
	bool ConvertResText(const char *res, uint32_t &cx, uint32_t &cy);
	void ResetDownscales();
	void SelectEncoder(QComboBox *combo, const char *name, int id);
	void SaveEncoder(QComboBox *combo);

private slots:
	void on_advOutFFPathBrowse_clicked();
	void on_advOutFFIgnoreCompat_stateChanged(int state);
	void on_advOutFFAEncoder_currentIndexChanged(int idx);
	void on_advOutFFVEncoder_currentIndexChanged(int idx);
	void on_advOutFFType_currentIndexChanged(int idx);
	void on_apply_clicked();
	void on_close_clicked();
	void on_cancel_clicked();


public slots:
	void StartOutput();
	void StopOutput();
	void on_advOutFFFormat_currentIndexChanged(int idx);

};
