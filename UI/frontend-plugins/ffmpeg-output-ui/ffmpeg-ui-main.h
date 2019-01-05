
#define do_log(type, format, ...) blog(type, "[Tools_FFmpeg] " format, \
		##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG, format, ##__VA_ARGS__)

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

void output_start();
void output_stop();
bool save_ffmpeg_data();
void load_ffmpeg_data();
