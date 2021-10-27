/*
** Based on reaper_csurf
** Copyright (C) 2006-2008 Cockos Incorporated
** License: LGPL.
**
** MCU support - Modified for generic controller surfaces such as Korg NanoKontrol 2 support by : Pierre Rousseau (May 2017)
** https://github.com/Pierousseau/reaper_generic_control
*/


#include "control_surface_interface.h"
#include "helpers.h"
#pragma warning (push, 0)
#include <WDL/ptrlist.h>
#pragma warning (pop)

#include <filesystem>
#include <functional>
#include <fstream>
#include <list>
#include <map>
#include <string>

#ifdef max
#undef min
#undef max
#endif

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#pragma warning (disable:4996)


/*
A Surface Preset does the mapping from MIDI keycodes to actions in Reaper.
Several named presets may be available in C:\Program Files\REAPER (x64)\Plugins\reaper_plugin_control_surface_generic_presets\
*/
class SurfacePreset
{
private:
	struct ControlMapping
	{
		int SoloId = -1;
		int MuteId = -1;
		int RecId = -1;
		int PanId = -1;
		int VolumeId = -1;
	};

	std::string _name;
	std::string _filename;
	int _file_name_hash;
	int _bank;
	ControlMapping _master_mapping;
	std::map<int, ControlMapping> _control_mappings;

	typedef std::map<int, std::function<void(int)>> ControlMap;

	ControlMap _controls;

private:
	void TrackBankDown(midi_Output* midi_out)
	{
		if (_bank <= 0)
			return;

		_bank--;
		ProcessBindings(midi_out);
	}

	void TrackBankUp(midi_Output* midi_out)
	{
		if (_bank >= 3)
			return;

		_bank++;
		ProcessBindings(midi_out);
	}

	void ProcessBindings(midi_Output* midi_out)
	{
		std::map<MediaTrack*, int> solo_map;
		std::map<MediaTrack*, int> mute_map;
		std::map<MediaTrack*, int> rec_map;

		MediaTrack* media_track = CSurf_TrackFromID(0, false);

		if (_master_mapping.SoloId > -1)
		{
			_controls[_master_mapping.SoloId] = [media_track](int value) { CSurf_OnSoloChange(media_track, value); };
			solo_map[media_track] = _master_mapping.SoloId;
		}

		if (_master_mapping.MuteId > -1)
		{
			_controls[_master_mapping.MuteId] = [media_track](int value) { CSurf_OnMuteChange(media_track, value); };
			mute_map[media_track] = _master_mapping.MuteId;
		}

		if (_master_mapping.RecId > -1)
		{
			_controls[_master_mapping.RecId] = [media_track](int value) { CSurf_OnRecArmChange(media_track, value); };
			rec_map[media_track] = _master_mapping.RecId;
		}

		if (_master_mapping.VolumeId > -1)
			_controls[_master_mapping.VolumeId] = [media_track](int value) { CSurf_OnVolumeChange(media_track, charToVol(value), false); };

		if (_master_mapping.PanId > -1)
			_controls[_master_mapping.PanId] = [media_track](int value) { CSurf_OnPanChange(media_track, charToPan(value), false); };

		int definedMappings = _control_mappings.size();
		int track_id = 1 + (definedMappings * _bank); // 8 faders, 1 is always master, rest shifts

		for (int i = 0; i < definedMappings; i++)
		{
			ControlMapping control = _control_mappings[i];
			MediaTrack* media_track = CSurf_TrackFromID(track_id, false);

			if (control.SoloId > -1)
			{
				_controls[control.SoloId] = [media_track](int value) { CSurf_OnSoloChange(media_track, value); };
				solo_map[media_track] = control.SoloId;
			}

			if (control.MuteId > -1)
			{
				_controls[control.MuteId] = [media_track](int value) { CSurf_OnMuteChange(media_track, value); };
				mute_map[media_track] = control.MuteId;
			}

			if (control.RecId > -1)
			{
				_controls[control.RecId] = [media_track](int value) { CSurf_OnRecArmChange(media_track, value); };
				rec_map[media_track] = control.RecId;
			}

			if (control.VolumeId > -1)
				_controls[control.VolumeId] = [media_track](int value) { CSurf_OnVolumeChange(media_track, charToVol(value), false); };

			if (control.PanId > -1)
				_controls[control.PanId] = [media_track](int value) { CSurf_OnPanChange(media_track, charToPan(value), false); };

			track_id++;
		}

		if (midi_out)
		{
			if (!solo_map.empty())
				setTrackSolo = [midi_out, solo_map](MediaTrack* track, bool val) {
					std::map<MediaTrack*, int>::const_iterator finder = solo_map.find(track);

					if (finder != solo_map.end())
						midi_out->Send(0xB0, finder->second, val ? 0x7f : 0, -1);
				};

			if (!mute_map.empty())
				setTrackMute = [midi_out, mute_map](MediaTrack* track, bool val) {
					std::map<MediaTrack*, int>::const_iterator finder = mute_map.find(track);

					if (finder != mute_map.end())
						midi_out->Send(0xB0, finder->second, val ? 0x7f : 0, -1);
				};

			if (!rec_map.empty())
				setTrackRecord = [midi_out, rec_map](MediaTrack* track, bool val) {
					std::map<MediaTrack*, int>::const_iterator finder = rec_map.find(track);

					if (finder != rec_map.end())
						midi_out->Send(0xB0, finder->second, val ? 0x7f : 0, -1);
				};

			resetTracks = [midi_out, solo_map, mute_map, rec_map]() {
				double val = 0;

				for (const auto& control : solo_map)
				{
					val = GetMediaTrackInfo_Value(control.first, "I_SOLO");
					midi_out->Send(0xB0, control.second, val > 0 ? 0x7f : 0, -1);
				}

				for (const auto& control : mute_map)
				{
					val = GetMediaTrackInfo_Value(control.first, "B_MUTE");
					midi_out->Send(0xB0, control.second, val > 0 ? 0x7f : 0, -1);
				}

				for (const auto& control : rec_map)
				{
					val = GetMediaTrackInfo_Value(control.first, "I_RECARM");
					midi_out->Send(0xB0, control.second, val > 0 ? 0x7f : 0, -1);
				}
			};
		}

		setTrackBankBack(_bank == 1 || _bank == 3);
		setTrackBankForward(_bank == 2 || _bank == 3);
		resetTracks();
	}

public:
	std::function<void(bool)> setPlay;
	std::function<void(bool)> setStop;
	std::function<void(bool)> setRec;
	std::function<void(bool)> setCycle;
	std::function<void(bool)> setBack;
	std::function<void(bool)> setForward;

	std::function<void(bool)> setTrackBankBack;
	std::function<void(bool)> setTrackBankForward;

	std::function<void(MediaTrack*, bool)> setTrackSolo;
	std::function<void(MediaTrack*, bool)> setTrackMute;
	std::function<void(MediaTrack*, bool)> setTrackRecord;
	std::function<void(MediaTrack*, double)> setTrackVolume;
	std::function<void(MediaTrack*, double)> setTrackPan;
	std::function<void()> resetTracks;

public:
	SurfacePreset(const std::string& filename)
		: _filename(filename)
		, _file_name_hash((int)std::hash<std::string>()(filename))
	{
		_bank = 0;
		parseName();
		initFunctions();
	}

	int fileHash() const
	{
		return _file_name_hash;
	}

	std::string name() const
	{
		return _name;
	}

	void parseName()
	{
		std::ifstream f(_filename);
		rapidjson::IStreamWrapper isw(f);
		rapidjson::Document doc;
		doc.ParseStream<rapidjson::kParseCommentsFlag | rapidjson::kParseNanAndInfFlag>(isw);

		if (doc.HasMember("SurfaceName"))
			_name = doc["SurfaceName"].GetString();
	}

	void parseControls(midi_Output* midi_out)
	{
		std::ifstream f(_filename);
		rapidjson::IStreamWrapper isw(f);
		rapidjson::Document doc;
		doc.ParseStream<rapidjson::kParseCommentsFlag | rapidjson::kParseNanAndInfFlag>(isw);

		if (doc.HasMember("Control"))
		{
			auto& control = doc["Control"];

			if (control.HasMember("Track Back"))
			{
				int code = control["Track Back"].GetInt();
				_controls[code] = [this, midi_out](int) { this->TrackBankDown(midi_out); };

				if (midi_out)
					setTrackBankBack = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
			}

			if (control.HasMember("Track Forward"))
			{
				int code = control["Track Forward"].GetInt();
				_controls[code] = [this, midi_out](int) { this->TrackBankUp(midi_out); };

				if (midi_out)
					setTrackBankForward = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
			}
		}

		if (doc.HasMember("Transport"))
		{
			auto& transport = doc["Transport"];

			if (transport.HasMember("Rewind"))
			{
				int code = transport["Rewind"].GetInt();
				_controls[code] = [this](int val) { this->setBack(val > 0); CSurf_GoStart(); };

				if (midi_out)
					setBack = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
			}

			if (transport.HasMember("Forward"))
			{
				int code = transport["Forward"].GetInt();
				_controls[code] = [this](int val) { this->setForward(val > 0); CSurf_GoEnd(); };

				if (midi_out)
					setForward = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
			}

			/*
			 * Either bitwise: &1=playing,&2=pause,&4=recording, or
			 * 0, stop
			 * 1, play
			 * 2, paused play
			 * 5, recording
			 * 6, paused recording
			 */
			int playState = GetPlayState();

			if (transport.HasMember("Stop"))
			{
				int code = transport["Stop"].GetInt();
				_controls[code] = [](int) { CSurf_OnStop(); };

				if (midi_out)
				{
					setStop = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
					setStop(playState == 0 || playState == 2 || playState == 6);
				}
			}

			if (transport.HasMember("Play"))
			{
				int code = transport["Play"].GetInt();
				_controls[code] = [](int value) { CSurf_OnPlay(); };

				if (midi_out)
				{
					setPlay = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
					setPlay(playState == 1 || playState == 2 || playState == 5 || playState == 6);
				}
			}

			if (transport.HasMember("Record"))
			{
				int code = transport["Record"].GetInt();
				_controls[code] = [](int value) { CSurf_OnRecord(); };

				if (midi_out)
				{
					setRec = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
					setRec(playState == 5 || playState == 6);
				}
			}

			if (transport.HasMember("Cycle"))
			{
				int code = transport["Cycle"].GetInt();
				_controls[code] = [](int value) { GetSetRepeat(value); };

				if (midi_out)
				{
					setCycle = [midi_out, code](bool val) { midi_out->Send(0xB0, code, val ? 0x7f : 0, -1); };
					setCycle(GetSetRepeat(-1) > 0);
				}
			}

			if (transport.HasMember("Marker Set"))
				_controls[transport["Marker Set"].GetInt()] = [](int value) { if (value == 127) SendMessage(g_hwnd, WM_COMMAND, 40157, 0); };

			if (transport.HasMember("Marker Previous"))
				_controls[transport["Marker Previous"].GetInt()] = [](int value) { if (value == 127) SendMessage(g_hwnd, WM_COMMAND, 40172, 0); };

			if (transport.HasMember("Marker Next"))
				_controls[transport["Marker Next"].GetInt()] = [](int value) { if (value == 127) SendMessage(g_hwnd, WM_COMMAND, 40173, 0); };
		}

		if (doc.HasMember("Master"))
		{
			auto& master = doc["Master"];
			MediaTrack* media_track = CSurf_TrackFromID(0, false);

			if (master.HasMember("Solo"))
				_master_mapping.SoloId = master["Solo"].GetInt();

			if (master.HasMember("Mute"))
				_master_mapping.MuteId = master["Mute"].GetInt();

			if (master.HasMember("Record"))
				_master_mapping.RecId = master["Record"].GetInt();

			if (master.HasMember("Volume"))
				_master_mapping.VolumeId = master["Volume"].GetInt();

			if (master.HasMember("Pan"))
				_master_mapping.PanId = master["Pan"].GetInt();
		}

		if (doc.HasMember("Tracks"))
		{
			int map_id = 0;

			for (const auto& track : doc["Tracks"].GetArray())
				_control_mappings[map_id++] = {
					track.HasMember("Solo")   ? track["Solo"].GetInt()   : -1, /* SoloId   */
					track.HasMember("Mute")   ? track["Mute"].GetInt()   : -1, /* MuteId   */
					track.HasMember("Record") ? track["Record"].GetInt() : -1, /* RecId    */
					track.HasMember("Pan")    ? track["Pan"].GetInt()    : -1, /* PanId    */
					track.HasMember("Volume") ? track["Volume"].GetInt() : -1  /* VolumeId */
				};
		}

		ProcessBindings(midi_out);
	}

	void midiIn(int code, int value)
	{
		ControlMap::const_iterator finder = _controls.find(code);

		if (finder != _controls.end())
			finder->second(value);
	}

	void initFunctions()
	{
		setPlay = [](bool) {};
		setStop = [](bool) {};
		setRec = [](bool) {};
		setCycle = [](bool) {};
		setBack = [](bool) {};
		setForward = [](bool) {};
		setTrackBankBack = [](bool) {};
		setTrackBankForward = [](bool) {};
		setTrackSolo = [](MediaTrack*, bool) {};
		setTrackMute = [](MediaTrack*, bool) {};
		setTrackRecord = [](MediaTrack*, bool) {};
		setTrackVolume = [](MediaTrack*, double) {};
		setTrackPan = [](MediaTrack*, double) {};
		resetTracks = []() {};
	}
};


/*
The List of SurfacePreset found in the preset folder.
*/
class SurfacePresets : public std::map<int, std::shared_ptr<SurfacePreset>>
{
protected:
	void insert(std::shared_ptr<SurfacePreset> preset)
	{
		if (preset)
			std::map<int, std::shared_ptr<SurfacePreset>>::insert(std::make_pair(preset->fileHash(), preset));
	}

	std::string _getExecutableDirPath()
	{
		char buffer[_MAX_PATH];
		DWORD length = GetModuleFileNameA(0, buffer, sizeof(buffer));
		bool status = ((length > 0) && (length < sizeof(buffer)));

		if (status == false)
			return std::string();

		std::string path = buffer;
		std::string::size_type pos = path.find_last_of("\\/");
		return path.substr(0, pos) + "\\";
	}

public:
	SurfacePresets()
	{
		init();
	}

	void init()
	{
		clear();
		std::string presets_dir_path = _getExecutableDirPath() + "plugins\\reaper_plugin_control_surface_generic_presets";

		for (auto& p : std::filesystem::directory_iterator(presets_dir_path))
			insert(std::make_shared<SurfacePreset>(p.path().string()));
	}
};


/*
The static preset list will be populated on DLL load from all files in the preset folder.
*/
static SurfacePresets surface_presets;



/*
A control surface plugin needs to override IReaperControlSurface.
This override is a lot simpler than usual, as it just forwards MIDI orders to the active preset,
without trying to understand them.
All code below this point is mostly a heavily simplified version of the usual MCU plugin examples.
*/
class ControlSurfaceGeneric : public IReaperControlSurface
{
private:
	int _midi_in_dev;
	int _midi_out_dev;
	int _preset;

	midi_Output* _midi_out;
	midi_Input* _midi_in;
	std::shared_ptr<SurfacePreset> _active_preset;

	WDL_String _desc_string;
	char _cfg_string[1024];

private:
	void _surfaceInit()
	{
		if (_midi_out && _active_preset)
		{
			_active_preset->setPlay(false);
			_active_preset->setStop(false);
			_active_preset->setRec(false);
			_active_preset->setCycle(false);
			_active_preset->setBack(false);
			_active_preset->setForward(false);

			_active_preset->resetTracks();
		}
	}

	void _midiEvent(MIDI_event_t* evt)
	{
		if (!_active_preset)
			return;

		int code = evt->midi_message[1];
		int value = evt->midi_message[2];
		_active_preset->midiIn(code, value);
	}

public:
	ControlSurfaceGeneric(int indev, int outdev, int preset, int* errStats)
		: _midi_in_dev(indev)
		, _midi_out_dev(outdev)
		, _preset(preset)
	{
		_midi_in = _midi_in_dev >= 0 ? CreateMIDIInput(_midi_in_dev) : NULL;
		_midi_out = _midi_out_dev >= 0 ? CreateThreadedMIDIOutput(CreateMIDIOutput(_midi_out_dev, false, NULL)) : NULL;

		_active_preset = _preset >= 0 ? surface_presets[_preset] : nullptr;

		if (errStats)
		{
			if (_midi_in_dev >= 0 && !_midi_in)
				*errStats |= 1;

			if (_midi_out_dev >= 0 && !_midi_out)
				*errStats |= 2;
		}

		_surfaceInit();

		if (_midi_in)
			_midi_in->start();
	}

	~ControlSurfaceGeneric()
	{
		CloseNoReset();
	}

	const char* GetTypeString()
	{
		return "GenericController";
	}

	/*
	The plugin description string, as seen in Reaper's preferences dialog
	*/
	const char* GetDescString()
	{
		_desc_string.Set("Generic Controller");
		char tmp[512];
		sprintf(tmp, " (dev %d,%d - preset %s)", _midi_in_dev, _midi_out_dev, _active_preset ? _active_preset->name().c_str() : "None");
		_desc_string.Append(tmp);
		return _desc_string.Get();
	}

	/*
	The plugin configuration string, made of three numerals which are the ID of the input device, the ID of the output device, and the preset ID
	*/
	const char* GetConfigString()
	{
		sprintf(_cfg_string, "%d %d %d", _midi_in_dev, _midi_out_dev, _preset);
		return _cfg_string;
	}

	virtual void CloseNoReset() override
	{
		if (_midi_in)
			delete _midi_in;

		if (_midi_out)
			delete _midi_out;

		_midi_out = 0;
		_midi_in = 0;
	}

	virtual void Run() override
	{
		if (_midi_in)
		{
			_midi_in->SwapBufs(timeGetTime());
			int l = 0;
			MIDI_eventlist* list = _midi_in->GetReadBuf();
			MIDI_event_t* evts;

			while ((evts = list->EnumItems(&l)))
				_midiEvent(evts);
		}
	}

	/*
	If the track list changes, track indices probably changed so we need to make sure are controls affect the right tracks.
	*/
	virtual void SetTrackListChange() override
	{
		if (_active_preset)
			_active_preset->parseControls(_midi_out);
	}

	/*
	The following methods are actions done in Reaper UI, which we can maybe reflect on the surface (e.g. LED status)
	*/
	virtual void SetSurfaceVolume(MediaTrack* trackid, double volume) override
	{
		if (_midi_out && _active_preset)
			_active_preset->setTrackVolume(trackid, volume);
	}

	virtual void SetSurfacePan(MediaTrack* trackid, double pan) override
	{
		if (_midi_out && _active_preset)
			_active_preset->setTrackPan(trackid, pan);
	}

	virtual void SetSurfaceMute(MediaTrack* trackid, bool mute) override
	{
		if (_midi_out && _active_preset)
			_active_preset->setTrackMute(trackid, mute);
	}

	virtual void SetSurfaceSolo(MediaTrack* trackid, bool solo) override
	{
		if (_midi_out && _active_preset)
			_active_preset->setTrackSolo(trackid, solo);
	}

	virtual void SetSurfaceRecArm(MediaTrack* trackid, bool recarm) override
	{
		if (_midi_out && _active_preset)
			_active_preset->setTrackRecord(trackid, recarm);
	}

	virtual void SetPlayState(bool play, bool pause, bool rec) override
	{
		if (_midi_out && _active_preset)
		{
			_active_preset->setPlay(play || pause || rec);
			_active_preset->setRec(rec);
			_active_preset->setStop(!play);
		}
	}

	virtual void SetRepeatState(bool rep) override
	{
		if (_midi_out && _active_preset)
			_active_preset->setCycle(rep);
	}
};


/*
Parse the plugin configuration string (input device ID, output device ID, preset ID)
*/
static void parseParameters(const char* str, int params[3])
{
	params[0] = params[1] = params[2] = -1;

	const char* p = str;

	if (p)
	{
		int x = 0;

		while (x < 3)
		{
			while (*p == ' ')
				p++;

			if ((*p < '0' || *p > '9') && *p != '-')
				break;

			params[x++] = atoi(p);

			while (*p && *p != ' ')
				p++;
		}
	}
}


/*
Instantiate the plugin
*/
static IReaperControlSurface* createFunc(const char* type_string, const char* configString, int* errStats)
{
	int params[3];
	parseParameters(configString, params);

	return new ControlSurfaceGeneric(params[0], params[1], params[2], errStats);
}

static WDL_DLGRET dlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		/*
		Add comboboxes to the plugin's configuration dialog
		*/
		int params[3];
		parseParameters((const char*)lParam, params);

		int count = GetNumMIDIInputs();
		LRESULT entry_none = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)"None");
		SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETITEMDATA, entry_none, -1);
		for (int i = 0; i < count; i++)
		{
			char buf[512];
			if (GetMIDIInputName(i, buf, sizeof(buf)))
			{
				LRESULT entry_midi = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)buf);
				SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETITEMDATA, entry_midi, i);
				if (i == params[0])
					SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETCURSEL, entry_midi, 0);
			}
		}

		entry_none = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)"None");
		SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_SETITEMDATA, entry_none, -1);
		count = GetNumMIDIOutputs();
		for (int i = 0; i < count; i++)
		{
			char buf[512];
			if (GetMIDIOutputName(i, buf, sizeof(buf)))
			{
				LRESULT entry_midi = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)buf);
				SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_SETITEMDATA, entry_midi, i);
				if (i == params[1])
					SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_SETCURSEL, entry_midi, 0);
			}
		}

		entry_none = SendDlgItemMessage(hwndDlg, IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)"None");
		SendDlgItemMessage(hwndDlg, IDC_COMBO4, CB_SETITEMDATA, entry_none, -1);
		for (const auto& preset : surface_presets)
		{
			int preset_key = preset.first;
			LRESULT entry_preset = SendDlgItemMessage(hwndDlg, IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)preset.second->name().c_str());
			SendDlgItemMessage(hwndDlg, IDC_COMBO4, CB_SETITEMDATA, entry_preset, preset_key);
			if (preset_key == params[2])
				SendDlgItemMessage(hwndDlg, IDC_COMBO4, CB_SETCURSEL, entry_preset, 0);
		}
	}
	break;

	case WM_USER + 1024:
	{
		/*
		Handle user input in the plugin's configuration dialog
		*/
		if (wParam > 1 && lParam)
		{
			char tmp[512];
			int midi_in = -1, midi_out = -1, preset = -1;

			LRESULT r = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_GETCURSEL, 0, 0);
			if (r != CB_ERR)
				midi_in = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_GETITEMDATA, r, 0);

			r = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_GETCURSEL, 0, 0);
			if (r != CB_ERR)
				midi_out = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_GETITEMDATA, r, 0);

			r = SendDlgItemMessage(hwndDlg, IDC_COMBO4, CB_GETCURSEL, 0, 0);
			if (r != CB_ERR)
				preset = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO4, CB_GETITEMDATA, r, 0);

			sprintf(tmp, "%d %d %d", midi_in, midi_out, preset);
			lstrcpyn((char*)lParam, tmp, (int)wParam);
		}
	}
	break;
	}
	return 0;
}

static HWND configFunc(const char* type_string, HWND parent, const char* initConfigString)
{
	return CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_SURFACEEDIT_MCU), parent, dlgProc, (LPARAM)initConfigString);
}

/*
Global variable, of type struct reaper_csurf_reg_t, containing the plugin's name, creation and configuration functions.
Used in plugin_main.cpp : REAPER_PLUGIN_ENTRYPOINT :
	rec->Register("csurf", &generic_surface_control_reg);
*/
reaper_csurf_reg_t generic_surface_control_reg =
{
  "GenericController",
  "Generic Surface Controller",
  createFunc,
  configFunc,
};
