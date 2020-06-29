#pragma once

#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <vector>
#include <stdio.h>
#include <QMainWindow>
#include <QApplication>
#include <QDesktopWidget>
#include <QCursor>
#include <JuceHeader.h>
//#include <juce_audio_devices/midi_io/juce_MidiDevices.h>
//#include <juce_audio_processors/juce_audio_processors.h>

int get_max_obs_channels()
{
	static int channels = 0;
	if (channels > 0) {
		return channels;
	}
	else {
		for (int i = 0; i < 1024; i++) {
			int c = get_audio_channels((speaker_layout)i);
			if (c > channels)
				channels = c;
		}
		return channels;
	}
}

const int          obs_output_frames = AUDIO_OUTPUT_FRAMES;
const volatile int obs_max_channels = get_max_obs_channels();

#if JUCE_PLUGINHOST_LADSPA && JUCE_LINUX
StringArray get_paths(LADSPAPluginFormat &f);
FileSearchPath get_search_paths(LADSPAPluginFormat &f);
void set_paths(LADSPAPluginFormat &f, StringArray p);
void set_search_paths(LADSPAPluginFormat &f, FileSearchPath p);
#endif

#if JUCE_PLUGINHOST_VST && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX || JUCE_IOS)
StringArray    get_paths(VSTPluginFormat &f);
FileSearchPath get_search_paths(VSTPluginFormat &f);
void           set_paths(VSTPluginFormat &f, StringArray p);
void           set_search_paths(VSTPluginFormat &f, FileSearchPath p);
#endif

#if JUCE_PLUGINHOST_VST3 && (JUCE_MAC || JUCE_WINDOWS)
StringArray    get_paths(VST3PluginFormat &f);
FileSearchPath get_search_paths(VST3PluginFormat &f);
void           set_paths(VST3PluginFormat &f, StringArray p);
void           set_search_paths(VST3PluginFormat &f, FileSearchPath p);
#endif

#if JUCE_PLUGINHOST_AU && (JUCE_MAC || JUCE_IOS)
StringArray    get_paths(AudioUnitPluginFormat &f);
FileSearchPath get_search_paths(AudioUnitPluginFormat &f);
void           set_paths(AudioUnitPluginFormat &f, StringArray p);
void           set_search_paths(AudioUnitPluginFormat &f, FileSearchPath p);
#endif

template<typename ValueType>
Point<ValueType> physicalToLogical(
	Point<ValueType> point, const juce::Displays::Display *useScaleFactorOfDisplay = nullptr)
{
	auto &display = useScaleFactorOfDisplay != nullptr
		? *useScaleFactorOfDisplay
		: Desktop::getInstance().getDisplays().findDisplayForPoint(point.roundToInt(), true);

	auto globalScale = Desktop::getInstance().getGlobalScaleFactor();

	Point<ValueType> logicalTopLeft(display.totalArea.getX(), display.totalArea.getY());
	Point<ValueType> physicalTopLeft(display.topLeftPhysical.getX(), display.topLeftPhysical.getY());

	return ((point - physicalTopLeft) / (display.scale / globalScale)) + (logicalTopLeft * globalScale);
}

class PluginWindow : public DialogWindow {
public:
	PluginWindow(const String &name, Colour backgroundColour, bool escapeKeyTriggersCloseButton,
		bool addToDesktop = true)
		: DialogWindow(name, backgroundColour, escapeKeyTriggersCloseButton, addToDesktop)
	{
		setUsingNativeTitleBar(true);
	}
	~PluginWindow()
	{
	}
	void closeButtonPressed()
	{
		setVisible(false);
	}
};

template<class PluginFormat> class PluginHost : private AudioProcessorListener, public ReferenceCountedObject {
private:
	juce::AudioBuffer<float> buffer;
	juce::MidiBuffer         midi;

	std::unique_ptr<AudioPluginInstance> vst_instance;
	std::unique_ptr<AudioPluginInstance> new_vst_instance;

	AudioProcessorEditor *editor = nullptr;
	obs_source_t *        context = nullptr;
	juce::MemoryBlock     vst_state;
	obs_data_t *          vst_settings = nullptr;
	juce::String          current_file = "";
	juce::String          current_name = "";

	PluginWindow *                 dialog = nullptr;
	juce::AudioProcessorParameter *param = nullptr;

	CriticalSection menu_update;

	MidiMessageCollector       midi_collector;
	std::unique_ptr<MidiInput> midi_input;
	juce::String               current_midi = "";
	double                     current_sample_rate = 0.0;
	bool                       dpi_aware = true;

	bool was_open = false;
	bool enabled = true;
	bool swap = false;

	void save_state(AudioProcessor *processor)
	{
		if (!vst_settings)
			vst_settings = obs_data_create();

		String state = "";
		if (processor) {
			processor->getStateInformation(vst_state);
			state = vst_state.toBase64Encoding();
		}
		obs_data_set_string(vst_settings, "state", state.toStdString().c_str());
	}

	void audioProcessorParameterChanged(AudioProcessor *processor, int parameterIndex, float newValue)
	{
		save_state(processor);

		std::string idx = std::to_string(parameterIndex);
		obs_data_set_double(vst_settings, idx.c_str(), newValue);
	}

	void audioProcessorChanged(AudioProcessor *processor)
	{
		save_state(processor);
	}

	void close_vst(std::unique_ptr<AudioPluginInstance> &inst)
	{
		if (inst) {
			inst->removeListener(this);
			AudioProcessorEditor *e = inst->getActiveEditor();
			if (e)
				delete e;
			inst->releaseResources();
			inst.reset();
		}
	}

	void change_vst(std::unique_ptr<AudioPluginInstance> &inst, juce::String err, obs_audio_info aoi,
		juce::String file, juce::String state)
	{
		menu_update.enter();
		close_vst(new_vst_instance);
		new_vst_instance.swap(inst);

		if (err.toStdString().length() > 0)
			blog(LOG_WARNING, "Couldn't create plugin! %s", err.toStdString().c_str());
		if (new_vst_instance) {
			host_close();
			new_vst_instance->setNonRealtime(false);
			new_vst_instance->prepareToPlay((double)aoi.samples_per_sec, 2 * obs_output_frames);

			if (!vst_settings) {
				juce::MemoryBlock m;
				m.fromBase64Encoding(state);
				new_vst_instance->setStateInformation(m.getData(), m.getSize());
				vst_settings = obs_data_create();
			}
			else {
				obs_data_clear(vst_settings);
			}

			save_state(new_vst_instance.get());
			new_vst_instance->addListener(this);
			current_name = new_vst_instance->getName();
			if (was_open)
				host_clicked(new_vst_instance.get());
		}
		else {
			AlertWindow::showMessageBoxAsync(
				AlertWindow::WarningIcon, TRANS("Couldn't create plugin"), err);
			current_name = "";
		}
		current_file = file;
		swap = true;
		menu_update.exit();
	}

	void update(obs_data_t *settings)
	{
		static PluginFormat plugin_format;

		obs_audio_info aoi = { 0 };
		bool           got_audio = obs_get_audio_info(&aoi);
		juce::String   file = obs_data_get_string(settings, "effect");
		juce::String   plugin = obs_data_get_string(settings, "desc");
		juce::String   mididevice = obs_data_get_string(settings, "midi");
		bool           dpi_awareness = obs_data_get_bool(settings, "dpi_aware");
		bool           was_showing = host_showing();
		bool           was_open = host_open();
		if (dpi_awareness != dpi_aware) {
			host_close();
			if (editor)
				delete editor;
			editor = nullptr;
		}
		dpi_aware = dpi_awareness;

		auto midi_stop = [this]() {
			if (midi_input) {
				midi_input->stop();
				midi_input.reset();
			}
		};

		double sps = (double)aoi.samples_per_sec;
		if (got_audio && current_sample_rate != sps) {
			midi_collector.reset(sps);
			current_sample_rate = sps;
		}

		if (mididevice.compare("") == 0) {
			midi_stop();
		}
		else if (mididevice.compare(current_midi) != 0) {
			midi_stop();

			juce::StringArray devices = MidiInput::getDevices();
			int               deviceindex = 0;
			for (; deviceindex < devices.size(); deviceindex++) {
				if (devices[deviceindex].compare(mididevice) == 0)
					break;
			}
			std::unique_ptr<MidiInput> nextdevice = MidiInput::openDevice(deviceindex, &midi_collector);
			// if we haven't reset, make absolute certain we have
			if (current_sample_rate == 0.0) {
				midi_collector.reset(48000.0);
				current_sample_rate = 48000.0;
			}
			midi_input.swap(nextdevice);
			if (midi_input)
				midi_input->start();
		}

		save(settings);

		juce::String err;
		bool         found = false;

		auto clear_vst = [this]() {
			close_vst(new_vst_instance);
			new_vst_instance = nullptr;
			current_name = "";
			swap = true;
		};

		if (file.compare(current_file) != 0 || plugin.compare(current_name) != 0) {
			if (file.compare("") == 0 || plugin.compare("") == 0) {
				clear_vst();
				return;
			}

			was_open = host_showing();

			juce::OwnedArray<juce::PluginDescription> descs;
			plugin_format.findAllTypesForFile(descs, file);
			if (descs.size() > 0) {
				if (got_audio) {
					String state = obs_data_get_string(settings, "state");
					auto   callback = [state, this, &aoi, file](
						std::unique_ptr<AudioPluginInstance> inst,
						const juce::String &                 err) {
						change_vst(inst, err, aoi, file, state);
						decReferenceCount();
					};

					int i = 0;
					for (; i < descs.size(); i++) {
						if (plugin.compare(descs[i]->name) == 0) {
							found = true;
							break;
						}
					}
					if (found) {
						// ensure the lifetime of this until after callback completes
						incReferenceCount();
						plugin_format.createPluginInstanceAsync(*descs[i],
							(double)aoi.samples_per_sec, 2 * obs_output_frames,
							std::move(callback));
					}
					else {
						clear_vst();
					}
					descs.clear(false);
					return;
				}
				else {
					clear_vst();
				}
			}
			else {
				clear_vst();
			}
			descs.clear(false);
		}

		if (was_open)
			host_clicked();
	}

	void save(obs_data_t *settings)
	{
		if (vst_settings)
			obs_data_set_string(settings, "state", obs_data_get_string(vst_settings, "state"));
		else
			obs_data_set_string(settings, "state", "");
	}

	void filter_audio(struct obs_audio_data *audio)
	{
		if (menu_update.tryEnter()) {
			if (swap) {
				vst_instance.swap(new_vst_instance);
				if (new_vst_instance)
					new_vst_instance->removeListener(this);
				swap = false;
			}
			menu_update.exit();
		}

		/*Process w/ VST*/
		if (vst_instance) {
			int chs = 0;
			for (; chs < obs_max_channels && audio->data[chs]; chs++)
				;

			struct obs_audio_info aoi;
			bool                  audio_info = obs_get_audio_info(&aoi);
			double                sps = (double)aoi.samples_per_sec;

			if (audio_info) {
				vst_instance->prepareToPlay(sps, audio->frames);
				if (current_sample_rate != sps)
					midi_collector.reset(sps);
				current_sample_rate = sps;
			}

			midi_collector.removeNextBlockOfMessages(midi, audio->frames);
			buffer.setDataToReferTo((float **)audio->data, chs, audio->frames);
			param = vst_instance->getBypassParameter();

			if (param && param->getValue() != 0.0f)
				vst_instance->processBlockBypassed(buffer, midi);
			else
				vst_instance->processBlock(buffer, midi);

			midi.clear();
		}
	}

public:
	PluginFormat getFormat()
	{
		static PluginFormat plugin_format;
		return plugin_format;
	}

	PluginHost(obs_data_t *settings, obs_source_t *source) : context(source)
	{
	}

	~PluginHost() override
	{
		if (vst_settings)
			obs_data_release(vst_settings);
		host_close();
		if (editor)
			delete editor;
		close_vst(vst_instance);
		close_vst(new_vst_instance);
	}

	void host_clicked(AudioPluginInstance *inst = nullptr)
	{
		QPoint mouse = QCursor::pos();
		if (!inst)
			inst = vst_instance.get();
		if (has_gui(inst)) {
			if (!dialog)
				dialog = new PluginWindow("", Colour(255, 255, 255), false, false);
			dialog->setName(inst->getName());

#if JUCE_WINDOWS && JUCE_WIN_PER_MONITOR_DPI_AWARE
			delete editor;
			std::unique_ptr<ScopedDPIAwarenessDisabler> disableDPIAwareness;
			if (!dpi_aware && (!current_name.contains("Kontakt") || !current_name.contains("BIAS"))) {
				disableDPIAwareness.reset(new ScopedDPIAwarenessDisabler());
				editor = inst->createEditorIfNeeded();
			}
			else {
				editor = inst->createEditorIfNeeded();
			}
#else
			editor = inst->createEditorIfNeeded();
#endif

			if (dialog) {
				juce::Point<double> mouse_point(mouse.x(), mouse.y());
				if (editor) {
					editor->setOpaque(true);
				}
				dialog->setContentNonOwned(editor, true);
				if (!dialog->isOnDesktop()) {
					dialog->setOpaque(true);
					dialog->addToDesktop(ComponentPeer::StyleFlags::windowHasCloseButton |
						ComponentPeer::StyleFlags::windowHasTitleBar |
						ComponentPeer::StyleFlags::
						windowHasMinimiseButton,
						nullptr);
				}
				dialog->setVisible(editor);
				if (editor) {
#if JUCE_DEBUG
					juce::Point<double> mouse_double(20, 20);
#else
					juce::Point<double> mouse_double = physicalToLogical(mouse_point);
#endif
					juce::Point<int> logical_mouse = mouse_double.roundToInt();

					logical_mouse.x -= (dialog->getWidth() / 2);
					logical_mouse.y += 20;

					editor->setVisible(true);

					dialog->setTopLeftPosition(logical_mouse);

					editor->grabKeyboardFocus();
				}
			}
		}
	}

	void host_close()
	{
		if (dialog) {
			delete dialog;
			dialog = nullptr;
		}
	}

	bool has_gui(AudioPluginInstance *inst = nullptr)
	{
		if (!inst)
			inst = vst_instance.get();
		return inst && inst->hasEditor();
	}

	bool host_open()
	{
		return dialog;
	}

	bool host_showing()
	{
		return dialog && dialog->isOnDesktop() && dialog->isVisible();
	}

	static bool vst_host_clicked(obs_properties_t *props, obs_property_t *property, void *vptr)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		plugin->host_clicked();
		return true;
	}

	static bool vst_selected_modified(
		void *vptr, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
	{
		static PluginFormat plugin_format;

		obs_property_t *desc_list = obs_properties_get(props, "desc");
		juce::String    file = obs_data_get_string(settings, "effect");

		obs_property_list_clear(desc_list);

		juce::OwnedArray<juce::PluginDescription> descs;
		plugin_format.findAllTypesForFile(descs, file);
		bool has_options = descs.size() > 1;
		if (has_options)
			obs_property_list_add_string(desc_list, "", "");

		for (int i = 0; i < descs.size(); i++) {
			std::string n = descs[i]->name.toStdString();
			obs_property_list_add_string(desc_list, n.c_str(), n.c_str());
		}

		obs_property_set_enabled(desc_list, has_options);
		descs.clear(false);
		return true;
	}

	static bool midi_selected_modified(
		void *vptr, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
	{
		obs_property_list_clear(property);
		juce::StringArray devices = MidiInput::getDevices();
		obs_property_list_add_string(property, "", "");
		for (int i = 0; i < devices.size(); i++)
			obs_property_list_add_string(property, devices[i].toRawUTF8(), devices[i].toRawUTF8());

		return true;
	}

	static obs_properties_t *Properties(void *vptr)
	{
		static PluginFormat plugin_format;

		PluginHost *plugin = static_cast<PluginHost *>(vptr);

		obs_properties_t *props;
		props = obs_properties_create();

		obs_property_t *vst_list;
		obs_property_t *desc_list;
		obs_property_t *midi_list;

		obs_property_t *vst_host_button;
		obs_property_t *dpi_aware;

		vst_list = obs_properties_add_list(
			props, "effect", obs_module_text("Plugin"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(vst_list, vst_selected_modified, plugin);

		desc_list = obs_properties_add_list(
			props, "desc", obs_module_text("Plugin Description"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

		midi_list = obs_properties_add_list(
			props, "midi", obs_module_text("Midi"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(midi_list, midi_selected_modified, nullptr);

		vst_host_button = obs_properties_add_button2(props, "vst_button", "Show", vst_host_clicked, plugin);

		dpi_aware = obs_properties_add_bool(props, "dpi_aware", obs_module_text("DPI Aware"));

		/*Add VSTs to list*/
		bool scannable = plugin_format.canScanForPlugins();
		if (scannable) {
			juce::StringArray paths = get_paths(plugin_format);
			if (paths.size() < 1) {
				juce::FileSearchPath s = get_search_paths(plugin_format);
				paths = plugin_format.searchPathsForPlugins(s, true, true);
				set_paths(plugin_format, paths);
			}

			obs_property_list_add_string(vst_list, "", "");
			for (int i = 0; i < paths.size(); i++) {
				juce::String name = plugin_format.getNameOfPluginFromIdentifier(paths[i]);
				obs_property_list_add_string(
					vst_list, paths[i].toStdString().c_str(), name.toStdString().c_str());
			}
		}

		return props;
	}

	static void Update(void *vptr, obs_data_t *settings)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		if (plugin)
			plugin->update(settings);
	}

	static void Defaults(obs_data_t *settings)
	{
		/*Setup Defaults*/
		obs_data_set_default_string(settings, "effect", "None");
		obs_data_set_default_double(settings, "enable", true);
		obs_data_set_default_bool(settings, "dpi_aware", true);
	}

	static const char *Name(void *unused)
	{
		UNUSED_PARAMETER(unused);
		static PluginFormat f;
		static std::string  type_name = std::string("VSTPlugin.") + f.getName().toStdString();
		return obs_module_text(type_name.c_str());
	}

	static void *Create(obs_data_t *settings, obs_source_t *source)
	{
		PluginHost *plugin = new PluginHost(settings, source);
		if (plugin) {
			plugin->incReferenceCount();
			plugin->update(settings);
		}
		return plugin;
	}

	static void Save(void *vptr, obs_data_t *settings)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		if (plugin)
			plugin->save(settings);
	}

	static void Destroy(void *vptr)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		if (plugin)
			plugin->decReferenceCount();
	}

	static struct obs_audio_data *Filter_Audio(void *vptr, struct obs_audio_data *audio)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		if (plugin)
			plugin->filter_audio(audio);
		return audio;
	}
};
