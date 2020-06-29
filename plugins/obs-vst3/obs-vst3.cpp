/*
Copyright (C) 2019 andersama <anderson.john.alexander@gmail.com>
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "obs-vst3.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-vst3", "en-US")

#define blog(level, msg, ...) blog(level, "obs-vst3: " msg, ##__VA_ARGS__)

#if JUCE_PLUGINHOST_VST && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX || JUCE_IOS)
static FileSearchPath search_vst;
StringArray           paths_vst;
StringArray           get_paths(VSTPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return paths_vst;
}
FileSearchPath get_search_paths(VSTPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return search_vst;
}
void set_paths(VSTPluginFormat &f, StringArray p)
{
	paths_vst = p;
}
void set_search_paths(VSTPluginFormat &f, FileSearchPath p)
{
	search_vst = p;
}
#endif

#if JUCE_PLUGINHOST_VST3 && (JUCE_MAC || JUCE_WINDOWS)
static FileSearchPath search_vst3;
StringArray           paths_vst3;
StringArray           get_paths(VST3PluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return paths_vst3;
}
FileSearchPath get_search_paths(VST3PluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return search_vst3;
}
void set_paths(VST3PluginFormat &f, StringArray p)
{
	paths_vst3 = p;
}
void set_search_paths(VST3PluginFormat &f, FileSearchPath p)
{
	search_vst3 = p;
}
#endif

#if JUCE_PLUGINHOST_LADSPA && JUCE_LINUX
static FileSearchPath search_ladspa;
StringArray           paths_ladspa;
StringArray           get_paths(LADSPAPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return paths_ladspa;
}
FileSearchPath get_search_paths(LADSPAPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return search_ladspa;
}
void set_paths(LADSPAPluginFormat &f, StringArray p)
{
	paths_ladspa = p;
}
void set_search_paths(LADSPAPluginFormat &f, FileSearchPath p)
{
	search_ladspa = p;
}
#endif

#if JUCE_PLUGINHOST_AU && (JUCE_MAC || JUCE_IOS)
static FileSearchPath search_au;
StringArray           paths_au;
StringArray           get_paths(AudioUnitPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return paths_au;
}
FileSearchPath get_search_paths(AudioUnitPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return search_au;
}
void set_paths(AudioUnitPluginFormat &f, StringArray p)
{
	paths_au = p;
}
void set_search_paths(AudioUnitPluginFormat &f, FileSearchPath p)
{
	search_au = p;
}
#endif

template<class _T> void register_plugin(const char *id)
{
	struct obs_source_info _filter = {0};
	_filter.id                     = id;
	_filter.type                   = OBS_SOURCE_TYPE_FILTER;
	_filter.output_flags           = OBS_SOURCE_AUDIO;
	_filter.get_name               = PluginHost<_T>::Name;
	_filter.create                 = PluginHost<_T>::Create;
	_filter.destroy                = PluginHost<_T>::Destroy;
	_filter.update                 = PluginHost<_T>::Update;
	_filter.filter_audio           = PluginHost<_T>::Filter_Audio;
	_filter.get_properties         = PluginHost<_T>::Properties;
	_filter.save                   = PluginHost<_T>::Save;

	obs_register_source(&_filter);

	static _T f;

	auto rescan = [](void * = nullptr) {
		static _T _f;
		if (_f.canScanForPlugins()) {
			FileSearchPath s_path = get_search_paths(_f);
			StringArray    p      = _f.searchPathsForPlugins(s_path, true, true);
			set_paths(_f, p);
		}
	};

	std::string s = std::string("Rescan ") + f.getName().toStdString();
	obs_frontend_add_tools_menu_item(s.c_str(), rescan, nullptr);
	FileSearchPath fs = f.getDefaultLocationsToSearch();
	set_search_paths(f, fs);
	rescan();
}

bool obs_module_load(void)
{
	int version = (JUCE_MAJOR_VERSION << 16) | (JUCE_MINOR_VERSION << 8) | JUCE_BUILDNUMBER;
	blog(LOG_INFO, "JUCE Version: (%i) %i.%i.%i", version, JUCE_MAJOR_VERSION, JUCE_MINOR_VERSION,
			JUCE_BUILDNUMBER);

	MessageManager::getInstance();
#if JUCE_PLUGINHOST_VST3 && (JUCE_MAC || JUCE_WINDOWS)
	register_plugin<VST3PluginFormat>("vst_filter_juce_vst3");
#endif
#if JUCE_PLUGINHOST_VST && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX || JUCE_IOS)
	register_plugin<VSTPluginFormat>("vst_filter_juce_vst2");
#endif
#if JUCE_PLUGINHOST_LADSPA && JUCE_LINUX
	register_plugin<LADSPAPluginFormat>("vst_filter_juce_ladspa");
#endif
#if JUCE_PLUGINHOST_AU && (JUCE_MAC || JUCE_IOS)
	register_plugin<AudioUnitPluginFormat>("vst_filter_juce_au");
#endif
	return true;
}

void obs_module_unload()
{
}
