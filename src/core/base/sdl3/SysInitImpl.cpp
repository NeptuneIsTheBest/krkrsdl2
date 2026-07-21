//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Initialization and Uninitialization
//---------------------------------------------------------------------------
#include "tjsCommHead.h"


#include "FilePathUtil.h"

#include "SysInitImpl.h"
#include "StorageIntf.h"
#include "StorageImpl.h"
#include "MsgIntf.h"
#include "GraphicsLoaderIntf.h"
#include "SystemControl.h"
#include "DebugIntf.h"
#include "tjsLex.h"
#include "LayerIntf.h"
#include "Random.h"
#include "DetectCPU.h"
#include "XP3Archive.h"
#include "ScriptMgnIntf.h"
#include "XP3Archive.h"

#include "BinaryStream.h"
#include "Application.h"
#include "Exception.h"
#include "ApplicationSpecialPath.h"
#include "TickCount.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/sysctl.h>

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
tjs_string TVPNativeProjectDir;
tjs_string TVPNativeDataPath;
bool TVPProjectDirSelected = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Platform specific method to increase heap
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// System security options
//---------------------------------------------------------------------------
// system security options are held inside the executable, where
// signature checker will refer. This enables the signature checker
// (or other security modules like XP3 encryption module) to check
// the changes which is not intended by the contents author.
const static char TVPSystemSecurityOptions[] =
"-- TVPSystemSecurityOptions disablemsgmap(0):forcedataxp3(0):acceptfilenameargument(0) --";
//---------------------------------------------------------------------------
int GetSystemSecurityOption(const char *name)
{
	size_t namelen = TJS_nstrlen(name);
	const char *p = TJS_nstrstr(TVPSystemSecurityOptions, name);
	if(!p) return 0;
	if(p[namelen] == '(' && p[namelen + 2] == ')')
		return p[namelen+1] - '0';
	return 0;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// delayed DLL load procedure hook
//---------------------------------------------------------------------------
// for supporting of "_inmm.dll" (C) irori
// http://www.geocities.co.jp/Playtown-Domino/8282/
//---------------------------------------------------------------------------
/*
note:
	_inmm.dll is a replacement of winmm.dll ( windows multimedia system dll ).
	_inmm.dll enables "MCI CD-DA supporting applications" to play musics using
	various way, including midi, mp3, wave or digital CD-DA, by applying a
	patch on those applications.

	TVP(kirikiri) system has a special structure of executable file --
	delayed loading of winmm.dll, in addition to compressed code/data area
	by the UPX executable packer.
	_inmm.dll's patcher can not recognize TVP's import area.

	So we must implement supporting of _inmm.dll alternatively.

	This function only works when -_inmm=yes or -inmm=yes option is specified at
	command line or embeded options area.
*/
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
static void TVPInitRandomGenerator()
{
	// initialize random generator
	tjs_uint32 tick = TVPGetRoughTickCount32();
	TVPPushEnvironNoise(&tick, sizeof(tick));
	time_t curtime = time(NULL);
	TVPPushEnvironNoise(&curtime, sizeof(curtime));
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPInitializeBaseSystems
//---------------------------------------------------------------------------
void TVPInitializeBaseSystems()
{
	// set system archive delimiter
	tTJSVariant v;
	if(TVPGetCommandLine(TJS_W("-arcdelim"), &v))
		TVPArchiveDelimiter = ttstr(v)[0];

	// set default current directory
	{
		TVPSetCurrentDirectory(IncludeTrailingSlash(ExtractFileDir(ExePath())));
	}

	// load message map file
	bool load_msgmap = GetSystemSecurityOption("disablemsgmap") == 0;

	if(load_msgmap)
	{
		const tjs_char name_msgmap [] = TJS_W("msgmap.tjs");
		if(TVPIsExistentStorage(name_msgmap))
			TVPExecuteStorage(name_msgmap, NULL, false, TJS_W(""));
	}
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// system initializer / uninitializer
//---------------------------------------------------------------------------
static tjs_uint64 TVPTotalPhysMemory = 0;
static void TVPInitProgramArgumentsAndDataPath(bool stop_after_datapath_got);
void TVPBeforeSystemInit()
{
	//RegisterDllLoadHook();
		// register DLL delayed import hook to support _inmm.dll

	TVPInitProgramArgumentsAndDataPath(false); // ensure command line

	// randomize
	TVPInitRandomGenerator();

	// memory usage
	{

		int darwin_sysctl_args[2] = {CTL_HW, HW_MEMSIZE};
		int64_t darwin_physical_memory;
		size_t darwin_physical_memory_arg_length = sizeof(int64_t);

		sysctl(darwin_sysctl_args, 2, &darwin_physical_memory, &darwin_physical_memory_arg_length, NULL, 0);
		TVPTotalPhysMemory = darwin_physical_memory;

		ttstr memstr( to_tjs_string(TVPTotalPhysMemory).c_str() );
		TVPAddImportantLog( TVPFormatMessage(TVPInfoTotalPhysicalMemory, memstr) );

		tTJSVariant opt;
		if(TVPGetCommandLine(TJS_W("-memusage"), &opt))
		{
			ttstr str(opt);
			if(str == TJS_W("low"))
				TVPTotalPhysMemory = 0; // assumes zero
		}

		if(TVPTotalPhysMemory <= 36*1024*1024)
		{
			// very very low memory, forcing to assume zero memory
			TVPTotalPhysMemory = 0;
		}

		if(TVPTotalPhysMemory < 48*1024*1024ULL)
		{
			// extra low memory
			if(TJSObjectHashBitsLimit > 0)
				TJSObjectHashBitsLimit = 0;
			TVPSegmentCacheLimit = 0;
			TVPFreeUnusedLayerCache = true; // in LayerIntf.cpp
		}
		else if(TVPTotalPhysMemory < 64*1024*1024)
		{
			// low memory
			if(TJSObjectHashBitsLimit > 4)
				TJSObjectHashBitsLimit = 4;
		}
	}
	// First, try SDL_GetBasePath
	const char *base_path = SDL_GetBasePath();
	std::string base_path_utf8;
	if (base_path)
	{
		base_path_utf8 = base_path;
	}
	tjs_string base_path_utf16;
	TVPUtf8ToUtf16(base_path_utf16, base_path_utf8);
	if (base_path_utf16.length() != 0 && !TVPGetCommandLine(TJS_W("-nosel")))
	{
		tjs_string found_dir;
		if (found_dir.length() == 0)
		{
			tjs_string tmp_search_dir = base_path_utf16 + TJS_W("content-data");
			if (TVPCheckExistentLocalFolder(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W("/");
			}
			else if (TVPCheckExistentLocalFile(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W(">");
			}
		}
		if (found_dir.length() == 0)
		{
			tjs_string tmp_search_dir = base_path_utf16 + TJS_W("data.xp3");
			if (TVPCheckExistentLocalFolder(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W("/");
			}
			else if (TVPCheckExistentLocalFile(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W(">");
			}
		}
		if (found_dir.length() == 0)
		{
			tjs_string tmp_search_dir = base_path_utf16 + TJS_W("data.exe");
			if (TVPCheckExistentLocalFolder(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W("/");
			}
			else if (TVPCheckExistentLocalFile(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W(">");
			}
		}
		if (found_dir.length() == 0)
		{
			tjs_string tmp_search_dir = base_path_utf16 + TJS_W("data");
			if (TVPCheckExistentLocalFolder(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W("/");
			}
			else if (TVPCheckExistentLocalFile(tmp_search_dir))
			{
				found_dir = tmp_search_dir + TJS_W(">");
			}
		}
		if (found_dir.length() != 0)
		{
			TVPProjectDir = TVPNormalizeStorageName(found_dir);
			TVPSetCurrentDirectory(TVPProjectDir);
			TVPNativeProjectDir = found_dir;
			TVPProjectDirSelected = true;
		}
	}

	if (!TVPProjectDirSelected)
	{
		tjs_string dir_utf16;
		size_t size = 512;
		char *buf = (char *)SDL_malloc(size);
		char *dir = getcwd(buf, size);
		while (dir == nullptr && buf != nullptr && errno == ERANGE)
		{
			size *= 2;
			buf = (char *)SDL_realloc(buf, size);
			dir = getcwd(buf, size);
		}
		std::string dir_utf8;
		if (dir)
		{
			dir_utf8 = dir;
		}
		TVPUtf8ToUtf16( dir_utf16, dir_utf8 );
		if (buf)
		{
			SDL_free(buf);
		}
		if (dir_utf16.length() != 0)
		{
			if (!TVPGetCommandLine(TJS_W("-nosel")))
			{
				TVPProjectDirSelected = true;
			}
			dir_utf16 += TJS_W("/");
			TVPProjectDir = TVPNormalizeStorageName(dir_utf16);
			TVPSetCurrentDirectory(TVPProjectDir);
			TVPNativeProjectDir = dir_utf16;
		}
		for (tjs_int i = 1; i < _argc; i += 1)
		{
			if (_wargv[i][0] == TJS_W('-') && _wargv[i][1] == TJS_W('-') && _wargv[i][2] == 0)
			{
				break;
			}

			if (_wargv[i][0] != TJS_W('-'))
			{
				ttstr dirbuf = _wargv[i];
				if (TVPCheckExistentLocalFolder(dirbuf))
				{
					dirbuf += TJS_W("/");
					TVPProjectDirSelected = true;
				}
				else if (TVPCheckExistentLocalFile(dirbuf))
				{
					dirbuf += TJS_W(">");
					TVPProjectDirSelected = true;
				}
				if (TVPProjectDirSelected)
				{
					TVPProjectDir = TVPNormalizeStorageName(dirbuf);
					TVPSetCurrentDirectory(TVPProjectDir);
					TVPNativeProjectDir = dirbuf.AsStdString();
				}
			}
		}
	}

	if (TVPProjectDirSelected)
	{
		TVPAddImportantLog( TVPFormatMessage(TVPInfoSelectedProjectDirectory, TVPProjectDir) );
	}
}
//---------------------------------------------------------------------------
static void TVPDumpOptions();
//---------------------------------------------------------------------------
extern void TVPGL_SSE2_Init();
extern void TVPAddGlobalHeapCompactCallback();
static bool TVPHighTimerPeriod = false;
static uint32_t TVPTimeBeginPeriodRes = 0;
//---------------------------------------------------------------------------
void TVPAfterSystemInit()
{
	// check CPU type
	TVPDetectCPU();

	TVPAllocGraphicCacheOnHeap = false; // always false since beta 20

	// determine maximum graphic cache limit
	tTJSVariant opt;
	tjs_int64 limitmb = -1;
	if(TVPGetCommandLine(TJS_W("-gclim"), &opt))
	{
		ttstr str(opt);
		if(str == TJS_W("auto"))
			limitmb = -1;
		else
			limitmb = opt.AsInteger();
	}

	// 物理メモリより仮想メモリの方が小さい(32bitでメモリ搭載量が多い)場合、仮想メモリの方でキャッシュ計算する
	tjs_uint64 totalMemory = TVPTotalPhysMemory;
	if(limitmb == -1)
	{
		if(totalMemory <= 32*1024*1024)
			TVPGraphicCacheSystemLimit = 0;
		else if(totalMemory <= 48*1024*1024)
			TVPGraphicCacheSystemLimit = 0;
		else if(totalMemory <= 64*1024*1024)
			TVPGraphicCacheSystemLimit = 0;
		else if(totalMemory <= 96*1024*1024)
			TVPGraphicCacheSystemLimit = 4;
		else if(totalMemory <= 128*1024*1024)
			TVPGraphicCacheSystemLimit = 8;
		else if(totalMemory <= 192*1024*1024)
			TVPGraphicCacheSystemLimit = 12;
		else if(totalMemory <= 256*1024*1024)
			TVPGraphicCacheSystemLimit = 20;
		else if(totalMemory <= 512*1024*1024)
			TVPGraphicCacheSystemLimit = 40;
		else
			TVPGraphicCacheSystemLimit = tjs_uint64(totalMemory / (1024*1024*10));	// cachemem = physmem / 10
		TVPGraphicCacheSystemLimit *= 1024*1024;
	}
	else
	{
		TVPGraphicCacheSystemLimit = limitmb * 1024*1024;
	}
	// キャッシュは 512MB までに制限
	if( TVPGraphicCacheSystemLimit >= 512*1024*1024 )
		TVPGraphicCacheSystemLimit = 512*1024*1024;

	if(totalMemory <= 64*1024*1024)
		TVPSetFontCacheForLowMem();

//	TVPGraphicCacheSystemLimit = 1*1024*1024; // DEBUG


	// check TVPGraphicSplitOperation option
	if(TVPGetCommandLine(TJS_W("-gsplit"), &opt))
	{
		ttstr str(opt);
		if(str == TJS_W("no"))
			TVPGraphicSplitOperationType = gsotNone;
		else if(str == TJS_W("int"))
			TVPGraphicSplitOperationType = gsotInterlace;
		else if(str == TJS_W("yes") || str == TJS_W("simple"))
			TVPGraphicSplitOperationType = gsotSimple;
		else if(str == TJS_W("bidi"))
			TVPGraphicSplitOperationType = gsotBiDirection;

	}

	// check TVPDefaultHoldAlpha option
	if(TVPGetCommandLine(TJS_W("-holdalpha"), &opt))
	{
		ttstr str(opt);
		if(str == TJS_W("yes") || str == TJS_W("true"))
			TVPDefaultHoldAlpha = true;
		else
			TVPDefaultHoldAlpha = false;
	}

	// check TVPJPEGFastLoad option
	if(TVPGetCommandLine(TJS_W("-jpegdec"), &opt)) // this specifies precision for JPEG decoding
	{
		ttstr str(opt);
		if(str == TJS_W("normal"))
			TVPJPEGLoadPrecision = jlpMedium;
		else if(str == TJS_W("low"))
			TVPJPEGLoadPrecision = jlpLow;
		else if(str == TJS_W("high"))
			TVPJPEGLoadPrecision = jlpHigh;

	}

	// dump option
	TVPDumpOptions();

	// Initialize the SSE2-compatible graphics routines provided through SIMDe.
	TVPGL_SSE2_Init();

	// timer precision
	uint32_t prectick = 1;
	if(TVPGetCommandLine(TJS_W("-timerprec"), &opt))
	{
		ttstr str(opt);
		if(str == TJS_W("high")) prectick = 1;
		if(str == TJS_W("higher")) prectick = 5;
		if(str == TJS_W("normal")) prectick = 10;
	}

        // draw thread num
        tjs_int drawThreadNum = 1;
        if (TVPGetCommandLine(TJS_W("-drawthread"), &opt)) {
          ttstr str(opt);
          if (str == TJS_W("auto"))
            drawThreadNum = 0;
          else
            drawThreadNum = (tjs_int)opt;
        }
        TVPDrawThreadNum = drawThreadNum;

}
//---------------------------------------------------------------------------
void TVPBeforeSystemUninit()
{
	// TVPDumpHWException(); // dump cached hw exceptoin
}
//---------------------------------------------------------------------------
void TVPAfterSystemUninit()
{
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
bool TVPTerminated = false;
bool TVPTerminateOnWindowClose = true;
bool TVPTerminateOnNoWindowStartup = true;
int TVPTerminateCode = 0;
//---------------------------------------------------------------------------
void TVPTerminateAsync(int code)
{
	// do "A"synchronous temination of application
	TVPTerminated = true;
	TVPTerminateCode = code;

	if(TVPSystemControl) TVPSystemControl->CallDeliverAllEventsOnIdle();

	Application->Terminate();

	if(TVPSystemControl) TVPSystemControl->CallDeliverAllEventsOnIdle();
}
//---------------------------------------------------------------------------
void TVPTerminateSync(int code)
{
	// do synchronous temination of application (never return)
	TVPSystemUninit();
	exit(code);
}
//---------------------------------------------------------------------------
void TVPMainWindowClosed()
{
	// called from WindowIntf.cpp, caused by closing all window.
	if( TVPTerminateOnWindowClose) TVPTerminateAsync();
}
//---------------------------------------------------------------------------







//---------------------------------------------------------------------------
// GetCommandLine
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
static std::vector<std::string> * TVPGetConfigFileOptions(const tjs_string& filename)
{
	return NULL;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------

static ttstr TVPParseCommandLineOne(const ttstr &i)
{
	// value is specified
	const tjs_char *p, *o;
	p = o = i.c_str();
	p = TJS_strchr(p, '=');

	if(p == NULL) { return i + TJS_W("=yes"); }

	p++;

	ttstr optname(o, (int)(p - o));

	if(*p == TJS_W('\'') || *p == TJS_W('\"'))
	{
		// as an escaped string
		tTJSVariant v;
		TJSParseString(v, &p);

		return optname + ttstr(v);
	}
	else
	{
		// as a string
		return optname + p;
	}
}
//---------------------------------------------------------------------------
std::vector <ttstr> TVPProgramArguments;
static bool TVPProgramArgumentsInit = false;
static tjs_int TVPCommandLineArgumentGeneration = 0;
static bool TVPDataPathDirectoryEnsured = false;
//---------------------------------------------------------------------------
tjs_int TVPGetCommandLineArgumentGeneration() { return TVPCommandLineArgumentGeneration; }
//---------------------------------------------------------------------------
void TVPEnsureDataPathDirectory()
{
	if(!TVPDataPathDirectoryEnsured)
	{
		TVPDataPathDirectoryEnsured = true;
		// ensure data path existence
		if(!TVPCheckExistentLocalFolder(TVPNativeDataPath.c_str()))
		{
			if(TVPCreateFolders(TVPNativeDataPath.c_str()))
				TVPAddImportantLog( TVPFormatMessage( TVPInfoDataPathDoesNotExistTryingToMakeIt, (const tjs_char*)TVPOk ) );
			else
				TVPAddImportantLog( TVPFormatMessage( TVPInfoDataPathDoesNotExistTryingToMakeIt, (const tjs_char*)TVPFaild ) );
		}
	}
}
//---------------------------------------------------------------------------
static void PushAllCommandlineArguments()
{
	// store arguments given by commandline to "TVPProgramArguments"
	bool acceptfilenameargument = GetSystemSecurityOption("acceptfilenameargument") != 0;

	bool argument_stopped = false;
	if(acceptfilenameargument) argument_stopped = true;
	int file_argument_count = 0;
	for(tjs_int i = 1; i<_argc; i++)
	{
		if(argument_stopped)
		{
			ttstr arg_name_and_value = TJS_W("-arg") + ttstr(file_argument_count) + TJS_W("=")
				+ ttstr(_wargv[i]);
			file_argument_count++;
			TVPProgramArguments.push_back(arg_name_and_value);
		}
		else
		{
			if(_wargv[i][0] == TJS_W('-'))
			{
				if(_wargv[i][1] == TJS_W('-') && _wargv[i][2] == 0)
				{
					// argument stopper
					argument_stopped = true;
				}
				else
				{
					ttstr value(_wargv[i]);
					if(!TJS_strchr(value.c_str(), TJS_W('=')))
						value += TJS_W("=yes");
					TVPProgramArguments.push_back(TVPParseCommandLineOne(value));
				}
			}
		}
	}
}
//---------------------------------------------------------------------------
static void PushConfigFileOptions(const std::vector<std::string> * options)
{
	if(!options) return;
	for(unsigned int j = 0; j < options->size(); j++)
	{
		if( (*options)[j].c_str()[0] != ';') // unless comment
			TVPProgramArguments.push_back(
			TVPParseCommandLineOne(TJS_W("-") + ttstr((*options)[j].c_str())));
	}
}
//---------------------------------------------------------------------------
static void TVPInitProgramArgumentsAndDataPath(bool stop_after_datapath_got)
{
	if(!TVPProgramArgumentsInit)
	{
		TVPProgramArgumentsInit = true;


		// find options from self executable image
		const int num_option_layers = 3;
		std::vector<std::string> * options[num_option_layers];
		for(int i = 0; i < num_option_layers; i++) options[i] = NULL;
		try
		{
			// read embedded options and default configuration file
			options[1] = TVPGetConfigFileOptions(ApplicationSpecialPath::GetConfigFileName(ExePath()));

			// at this point, we need to push all exsting known options
			// to be able to see datapath
			PushAllCommandlineArguments();
			PushConfigFileOptions(options[1]); // has more priority

			// read datapath
			tTJSVariant val;
			tjs_string config_datapath;
			if(TVPGetCommandLine(TJS_W("-datapath"), &val))
				config_datapath = ((ttstr)val).AsStdString();
			TVPNativeDataPath = ApplicationSpecialPath::GetDataPathDirectory(config_datapath, ExePath());

			if(stop_after_datapath_got) return;

			// read per-user configuration file
			options[2] = TVPGetConfigFileOptions(ApplicationSpecialPath::GetUserConfigFileName(config_datapath, ExePath()));

			// push each options into option stock
			// we need to clear TVPProgramArguments first because of the
			// option priority order.
			TVPProgramArguments.clear();
			PushAllCommandlineArguments();
			PushConfigFileOptions(options[2]); // has more priority
			PushConfigFileOptions(options[1]); // has more priority
		} catch(...) {
			for(int i = 0; i < num_option_layers; i++)
				if(options[i]) delete options[i];
			throw;
		}
		for(int i = 0; i < num_option_layers; i++)
			if(options[i]) delete options[i];


		// set data path
		TVPDataPath = TVPNormalizeStorageName(TVPNativeDataPath);
		TVPAddImportantLog( TVPFormatMessage( TVPInfoDataPath, TVPDataPath) );

		// set log output directory
		TVPSetLogLocation(TVPNativeDataPath);

		// increment TVPCommandLineArgumentGeneration
		TVPCommandLineArgumentGeneration++;
	}
}
//---------------------------------------------------------------------------
static void TVPDumpOptions()
{
	std::vector<ttstr>::const_iterator i;
 	ttstr options( TVPInfoSpecifiedOptionEarlierItemHasMorePriority );
	if(TVPProgramArguments.size())
	{
		for(i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
		{
			options += TJS_W(" ");
			options += *i;
		}
	}
	else
	{
		options += (const tjs_char*)TVPNone;
	}
	TVPAddImportantLog(options);
}
//---------------------------------------------------------------------------
bool TVPGetCommandLine(const tjs_char * name, tTJSVariant *value)
{
	TVPInitProgramArgumentsAndDataPath(false);

	tjs_int namelen = (tjs_int)TJS_strlen(name);
	std::vector<ttstr>::const_iterator i;
	for(i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
	{
		if(!TJS_strncmp(i->c_str(), name, namelen))
		{
			if(i->c_str()[namelen] == TJS_W('='))
			{
				// value is specified
				const tjs_char *p = i->c_str() + namelen + 1;
				if(value) *value = p;
				return true;
			}
			else if(i->c_str()[namelen] == 0)
			{
				// value is not specified
				if(value) *value = TJS_W("yes");
				return true;
			}
		}
	}
	return false;
}
//---------------------------------------------------------------------------
void TVPSetCommandLine(const tjs_char * name, const ttstr & value)
{
	TVPInitProgramArgumentsAndDataPath(false);

	tjs_int namelen = (tjs_int)TJS_strlen(name);
	std::vector<ttstr>::iterator i;
	for(i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
	{
		if(!TJS_strncmp(i->c_str(), name, namelen))
		{
			if(i->c_str()[namelen] == TJS_W('=') || i->c_str()[namelen] == 0)
			{
				// value found
				*i = ttstr(i->c_str(), namelen) + TJS_W("=") + value;
				TVPCommandLineArgumentGeneration ++;
				if(TVPCommandLineArgumentGeneration == 0) TVPCommandLineArgumentGeneration = 1;
				return;
			}
		}
	}

	// value not found; insert argument into front
	TVPProgramArguments.insert(TVPProgramArguments.begin(), ttstr(name) + TJS_W("=") + value);
	TVPCommandLineArgumentGeneration ++;
	if(TVPCommandLineArgumentGeneration == 0) TVPCommandLineArgumentGeneration = 1;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPCheckPrintDataPath
//---------------------------------------------------------------------------
bool TVPCheckPrintDataPath()
{
	return false;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPCheckAbout
//---------------------------------------------------------------------------
bool TVPCheckAbout(void)
{
	if(TVPGetCommandLine(TJS_W("-about")))
	{

		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Kirikiri SDL3", "The -about argument is partially implemented.\nSee: https://krkrsdl2.github.io/krkrsdl2/", NULL);
		return true;
	}

	return false;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPExecuteAsync
//---------------------------------------------------------------------------
static void TVPExecuteAsync( const tjs_string& progname)
{
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// TVPWaitWritePermit
//---------------------------------------------------------------------------
static bool TVPWaitWritePermit(const tjs_string& fn)
{
	return false;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPExecuteUserConfig
//---------------------------------------------------------------------------
bool TVPExecuteUserConfig()
{
	// check command line argument

	tjs_int i;
	bool process = false;
	for(i=1; i<_argc; i++)
	{
		if(!TJS_strcmp(_wargv[i], TJS_W("-userconf"))) // this does not refer TVPGetCommandLine
			process = true;
	}

	if(!process) return false;

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Kirikiri SDL3", "The -userconf argument for showing the config window is not yet implemented.", NULL);

	// exit
	return true;
}
//---------------------------------------------------------------------------


