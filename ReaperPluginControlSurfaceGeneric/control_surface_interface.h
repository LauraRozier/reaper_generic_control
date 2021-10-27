/*
** Based on reaper_csurf
** Copyright (C) 2006-2008 Cockos Incorporated
** License: LGPL.
**
** MCU support - Modified for generic controller surfaces such as Korg NanoKontrol 2 support by : Pierre Rousseau (May 2017)
** https://github.com/Pierousseau/reaper_generic_control

** Code in this file was reformatted from "ThirdParties\ReaperExtensionsSdk\jmde\csurf\csurf.h" in the reaper SDK,
** but has not been otherwise modified.
*/

#pragma once

#pragma warning (push, 0)
#include <jmde/reaper_plugin.h>
#include <WDL/db2val.h>
#include <WDL/wdlstring.h>
#pragma warning (pop)

#include <stdio.h>
#include "resource.h"

extern REAPER_PLUGIN_HINSTANCE g_hInst; // used for dialogs
extern HWND g_hwnd;
/*
** Calls back to REAPER (all validated on load)
*/
extern double (*DB2SLIDER)(double x);
extern double (*SLIDER2DB)(double y);

extern int (*GetNumMIDIInputs)();
extern int (*GetNumMIDIOutputs)();
extern midi_Input* (*CreateMIDIInput)(int dev);
extern midi_Output* (*CreateMIDIOutput)(int dev, bool streamMode, int* msoffset100);
extern bool (*GetMIDIOutputName)(int dev, char* nameout, int nameoutlen);
extern bool (*GetMIDIInputName)(int dev, char* nameout, int nameoutlen);

extern MediaTrack* (*CSurf_TrackFromID)(int idx, bool mcpView);

// these are called by our surfaces, and actually update the project
extern double (*CSurf_OnVolumeChange)(MediaTrack* trackid, double volume, bool relative);
extern double (*CSurf_OnPanChange)(MediaTrack* trackid, double pan, bool relative);
extern bool (*CSurf_OnMuteChange)(MediaTrack* trackid, int mute);
extern bool (*CSurf_OnSoloChange)(MediaTrack* trackid, int solo);
extern bool (*CSurf_OnRecArmChange)(MediaTrack* trackid, int recarm);
extern void (*CSurf_OnPlay)();
extern void (*CSurf_OnStop)();
extern void (*CSurf_OnRecord)();
extern void (*CSurf_GoStart)();
extern void (*CSurf_GoEnd)();


extern int (*GetPlayState)();
extern int (*GetSetRepeat)(int val);
extern double (*GetMediaTrackInfo_Value)(MediaTrack* tr, const char* parmname);

extern int* g_config_csurf_rate, * g_config_zoommode;

extern int __g_projectconfig_timemode2, __g_projectconfig_timemode;
extern int __g_projectconfig_timeoffs;
extern int __g_projectconfig_measoffs;

/*
** REAPER command message defines
*/

#define IDC_REPEAT                      1068
#define ID_FILE_SAVEAS                  40022
#define ID_FILE_NEWPROJECT              40023
#define ID_FILE_OPENPROJECT             40025
#define ID_FILE_SAVEPROJECT             40026
#define IDC_EDIT_UNDO                   40029
#define IDC_EDIT_REDO                   40030
#define ID_MARKER_PREV                  40172
#define ID_MARKER_NEXT                  40173
#define ID_INSERT_MARKERRGN             40174
#define ID_INSERT_MARKER                40157
#define ID_LOOP_SETSTART                40222
#define ID_LOOP_SETEND                  40223
#define ID_METRONOME                    40364
#define ID_GOTO_MARKER1                 40161
#define ID_SET_MARKER1                  40657

// Reaper track automation modes
enum AutoMode {
	AUTO_MODE_TRIM,
	AUTO_MODE_READ,
	AUTO_MODE_TOUCH,
	AUTO_MODE_WRITE,
	AUTO_MODE_LATCH,
};

midi_Output* CreateThreadedMIDIOutput(midi_Output* output); // returns null on null
