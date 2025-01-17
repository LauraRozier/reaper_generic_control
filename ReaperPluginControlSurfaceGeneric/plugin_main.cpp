/*
** Based on reaper_csurf
** Copyright (C) 2006-2008 Cockos Incorporated
** License: LGPL.
**
** MCU support - Modified for generic controller surfaces such as Korg NanoKontrol 2 support by : Pierre Rousseau (May 2017)
** https://github.com/Pierousseau/reaper_generic_control
**
** Code in this file is basically identical to "ThirdParties\ReaperExtensionsSdk\jmde\csurf\csurf_main.cpp" in the reaper SDK,
** except for the plugin registration part at the end of REAPER_PLUGIN_ENTRYPOINT function.
*/

#include "control_surface_interface.h"

extern reaper_csurf_reg_t generic_surface_control_reg;

REAPER_PLUGIN_HINSTANCE g_hInst; // used for dialogs, if any
HWND g_hwnd;

double(*DB2SLIDER)(double x);
double(*SLIDER2DB)(double y);

int (*GetNumMIDIInputs)();
int (*GetNumMIDIOutputs)();
midi_Input* (*CreateMIDIInput)(int dev);
midi_Output* (*CreateMIDIOutput)(int dev, bool streamMode, int* msoffset100);
bool (*GetMIDIOutputName)(int dev, char* nameout, int nameoutlen);
bool (*GetMIDIInputName)(int dev, char* nameout, int nameoutlen);

MediaTrack* (*CSurf_TrackFromID)(int idx, bool mcpView);

// these are called by our surfaces, and actually update the project
double (*CSurf_OnVolumeChange)(MediaTrack* trackid, double volume, bool relative);
double (*CSurf_OnPanChange)(MediaTrack* trackid, double pan, bool relative);
bool (*CSurf_OnMuteChange)(MediaTrack* trackid, int mute);
bool (*CSurf_OnSoloChange)(MediaTrack* trackid, int solo);
bool (*CSurf_OnRecArmChange)(MediaTrack* trackid, int recarm);
void (*CSurf_OnPlay)();
void (*CSurf_OnStop)();
void (*CSurf_OnRecord)();
void (*CSurf_GoStart)();
void (*CSurf_GoEnd)();

int (*GetPlayState)();
int (*GetSetRepeat)(int val);
double (*GetMediaTrackInfo_Value)(MediaTrack* tr, const char* parmname);

int* g_config_csurf_rate, * g_config_zoommode;

int __g_projectconfig_timemode2, __g_projectconfig_timemode;
int __g_projectconfig_measoffs;
int __g_projectconfig_timeoffs; // double

extern "C"
{
	REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec)
	{
#define IMPAPI(x) if (!((*((void **)&(x)) = (void *)rec->GetFunc(#x)))) errcnt++;
#define IMPVAR(x,nm) if (!((*(void **)&(x)) = get_config_var(nm,&sztmp)) || sztmp != sizeof(*x)) errcnt++;
#define IMPVARP(x,nm,type) if (!((x) = projectconfig_var_getoffs(nm,&sztmp)) || sztmp != sizeof(type)) errcnt++;

		g_hInst = hInstance;

		if (!rec || rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
			return 0;

		g_hwnd = rec->hwnd_main;
		int errcnt = 0;

		IMPAPI(DB2SLIDER);
		IMPAPI(SLIDER2DB);

		IMPAPI(GetNumMIDIInputs);
		IMPAPI(GetNumMIDIOutputs);
		IMPAPI(CreateMIDIInput);
		IMPAPI(CreateMIDIOutput);
		IMPAPI(GetMIDIOutputName);
		IMPAPI(GetMIDIInputName);

		IMPAPI(CSurf_TrackFromID);

		IMPAPI(CSurf_OnVolumeChange);
		IMPAPI(CSurf_OnPanChange);
		IMPAPI(CSurf_OnMuteChange);
		IMPAPI(CSurf_OnSoloChange);
		IMPAPI(CSurf_OnRecArmChange);
		IMPAPI(CSurf_OnPlay);
		IMPAPI(CSurf_OnStop);
		IMPAPI(CSurf_OnRecord);
		IMPAPI(CSurf_GoStart);
		IMPAPI(CSurf_GoEnd);

		IMPAPI(GetPlayState);
		IMPAPI(GetSetRepeat);
		IMPAPI(GetMediaTrackInfo_Value);

		void* (*get_config_var)(const char* name, int* szout) = nullptr;
		int (*projectconfig_var_getoffs)(const char* name, int* szout) = nullptr;

		IMPAPI(get_config_var);
		IMPAPI(projectconfig_var_getoffs);

		if (errcnt)
			return 0;

		int sztmp;
		IMPVAR(g_config_csurf_rate, "csurfrate");
		IMPVAR(g_config_zoommode, "zoommode");

		IMPVARP(__g_projectconfig_timemode, "projtimemode", int);
		IMPVARP(__g_projectconfig_timemode2, "projtimemode2", int);
		IMPVARP(__g_projectconfig_timeoffs, "projtimeoffs", double);
		IMPVARP(__g_projectconfig_measoffs, "projmeasoffs", int);

		if (errcnt)
			return 0;

		rec->Register("csurf", &generic_surface_control_reg);

		return 1;
	}
};

#ifndef _WIN32 // MAC resources & let OS X use this threading step
#include "../../../WDL/swell/swell-dlggen.h"
#include "res.rc_mac_dlg"
#undef BEGIN
#undef END
#include "../../../WDL/swell/swell-menugen.h"
#include "res.rc_mac_menu"
#include "../../../WDL/mutex.h"
#include "../../../WDL/ptrlist.h"

class threadedMIDIOutput : public midi_Output
{
public:
	threadedMIDIOutput(midi_Output* out)
	{
		m_output = out;
		m_quit = false;
		DWORD id;
		m_hThread = CreateThread(NULL, 0, threadProc, this, 0, &id);
	}
	virtual ~threadedMIDIOutput()
	{
		if (m_hThread)
		{
			m_quit = true;
			WaitForSingleObject(m_hThread, INFINITE);
			CloseHandle(m_hThread);
			m_hThread = 0;
			Sleep(30);
		}

		delete m_output;
		m_empty.Empty(true);
		m_full.Empty(true);
	}

	virtual void SendMsg(MIDI_event_t* msg, int frame_offset) // frame_offset can be <0 for "instant" if supported
	{
		if (!msg)
			return;

		WDL_HeapBuf* b = NULL;

		if (m_empty.GetSize())
		{
			m_mutex.Enter();
			b = m_empty.Get(m_empty.GetSize() - 1);
			m_empty.Delete(m_empty.GetSize() - 1);
			m_mutex.Leave();
		}

		if (!b && m_empty.GetSize() + m_full.GetSize() < 500)
			b = new WDL_HeapBuf(256);

		if (b)
		{
			int sz = msg->size;

			if (sz < 3)
				sz = 3;

			int len = msg->midi_message + sz - (unsigned char*)msg;
			memcpy(b->Resize(len, false), msg, len);
			m_mutex.Enter();
			m_full.Add(b);
			m_mutex.Leave();
		}
	}

	virtual void Send(unsigned char status, unsigned char d1, unsigned char d2, int frame_offset) // frame_offset can be <0 for "instant" if supported
	{
		MIDI_event_t evt = { 0,3,status,d1,d2 };
		SendMsg(&evt, frame_offset);
	}

	///////////

	static DWORD WINAPI threadProc(LPVOID p)
	{
		WDL_HeapBuf* lastbuf = NULL;
		threadedMIDIOutput* _this = (threadedMIDIOutput*)p;
		unsigned int scnt = 0;

		for (;;)
		{
			if (_this->m_full.GetSize() || lastbuf)
			{
				_this->m_mutex.Enter();

				if (lastbuf)
					_this->m_empty.Add(lastbuf);

				lastbuf = _this->m_full.Get(0);
				_this->m_full.Delete(0);
				_this->m_mutex.Leave();

				if (lastbuf)
					_this->m_output->SendMsg((MIDI_event_t*)lastbuf->Get(), -1);

				scnt = 0;
			}
			else
			{
				Sleep(1);

				if (_this->m_quit && scnt++ > 3)
					break; //only quit once all messages have been sent
			}
		}

		delete lastbuf;
		return 0;
	}

	WDL_Mutex m_mutex;
	WDL_PtrList<WDL_HeapBuf> m_full, m_empty;

	HANDLE m_hThread;
	bool m_quit;
	midi_Output* m_output;
};

midi_Output* CreateThreadedMIDIOutput(midi_Output* output)
{
	if (!output)
		return output;

	return new threadedMIDIOutput(output);
}

#else

// windows doesnt need it since we have threaded midi outputs now
midi_Output* CreateThreadedMIDIOutput(midi_Output* output)
{
	return output;
}

#endif
