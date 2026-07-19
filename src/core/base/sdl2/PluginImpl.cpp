//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class implementation / Service for plug-ins
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include <functional>
#include "ScriptMgnIntf.h"
#include "PluginImpl.h"
#include "StorageImpl.h"
#include "MsgImpl.h"
#include "SysInitIntf.h"

#include "tjsHashSearch.h"
#include "EventIntf.h"
#include "TransIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "DebugIntf.h"
#ifdef KRKRSDL2_ENABLE_PLUGINS
#include "FuncStubs.h"
#endif
#include "tjs.h"

#include "FilePathUtil.h"
#include "Application.h"
#include "SysInitImpl.h"
#include <set>


//---------------------------------------------------------------------------
// export table
//---------------------------------------------------------------------------
#ifdef KRKRSDL2_ENABLE_PLUGINS
static tTJSHashTable<ttstr, void *> TVPExportFuncs;
static bool TVPExportFuncsInit = false;
#endif
void TVPAddExportFunction(const char *name, void *ptr)
{
#ifdef KRKRSDL2_ENABLE_PLUGINS
	TVPExportFuncs.Add(name, ptr);
#endif
}
void TVPAddExportFunction(const tjs_char *name, void *ptr)
{
#ifdef KRKRSDL2_ENABLE_PLUGINS
	TVPExportFuncs.Add(name, ptr);
#endif
}
#ifdef KRKRSDL2_ENABLE_PLUGINS
static void TVPInitExportFuncs()
{
	if(TVPExportFuncsInit) return;
	TVPExportFuncsInit = true;


	// Export functions
	TVPExportFunctions();
}
//---------------------------------------------------------------------------
struct tTVPFunctionExporter : iTVPFunctionExporter
{
	bool TJS_INTF_METHOD QueryFunctions(const tjs_char **name, void **function,
		tjs_uint count);
	bool TJS_INTF_METHOD QueryFunctionsByNarrowString(const char **name,
		void **function, tjs_uint count);
} static TVPFunctionExporter;
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPFunctionExporter::QueryFunctions(const tjs_char **name, void **function,
		tjs_uint count)
{
	// retrieve function table by given name table.
	// return false if any function is missing.
	bool ret = true;
	ttstr tname;
	for(tjs_uint i = 0; i<count; i++)
	{
		tname = name[i];
		void ** ptr = TVPExportFuncs.Find(tname);
		if(ptr)
			function[i] = *ptr;
		else
			function[i] = NULL, ret= false;
	}
	return ret;
}
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPFunctionExporter::QueryFunctionsByNarrowString(
	const char **name, void **function, tjs_uint count)
{
	// retrieve function table by given name table.
	// return false if any function is missing.
	bool ret = true;
	ttstr tname;
	for(tjs_uint i = 0; i<count; i++)
	{
		tname = name[i];
		void ** ptr = TVPExportFuncs.Find(tname);
		if(ptr)
			function[i] = *ptr;
		else
			function[i] = NULL, ret= false;
	}
	return ret;
}
//---------------------------------------------------------------------------
extern "C" iTVPFunctionExporter * TVPGetFunctionExporter()
{
	// for external applications
	TVPInitExportFuncs();
    return &TVPFunctionExporter;
}
//---------------------------------------------------------------------------
#endif


//---------------------------------------------------------------------------
void TVPThrowPluginUnboundFunctionError(const char *funcname)
{
	TVPThrowExceptionMessage(TVPPluginUnboundFunctionError, funcname);
}
//---------------------------------------------------------------------------
void TVPThrowPluginUnboundFunctionError(const tjs_char *funcname)
{
	TVPThrowExceptionMessage(TVPPluginUnboundFunctionError, funcname);
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// implementation of IStorageProvider
//---------------------------------------------------------------------------
class tTVPStorageProvider : public ITSSStorageProvider
{
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **ppvObjOut)
	{
		if(!ppvObjOut) return E_INVALIDARG;

		*ppvObjOut = NULL;
		if(!memcmp(&iid, &IID_IUnknown, 16))
			*ppvObjOut = (IUnknown*)this;
		else if(!memcmp(&iid, &IID_ITSSStorageProvider, 16))
			*ppvObjOut = (ITSSStorageProvider*)this;

		if(*ppvObjOut)
		{
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef(void) { return 1; }
	ULONG STDMETHODCALLTYPE Release(void) { return 1; }

	HRESULT STDMETHODCALLTYPE GetStreamForRead(
		TSS_LPWSTR url,
		IUnknown * *stream);

	HRESULT STDMETHODCALLTYPE GetStreamForWrite(
		TSS_LPWSTR url,
		IUnknown * *stream) { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE GetStreamForUpdate(
		TSS_LPWSTR url,
		IUnknown * *stream) { return E_NOTIMPL; }
};
//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPStorageProvider::GetStreamForRead(
		TSS_LPWSTR url,
		IUnknown * *stream)
{
	tTJSBinaryStream *stream0;
	try
	{
		stream0 = TVPCreateStream(url);
	}
	catch(...)
	{
		return E_FAIL;
	}

	IUnknown *istream = (IUnknown*)(IStream*)new tTVPIStreamAdapter(stream0);
	*stream = istream;

	return S_OK;
}
//---------------------------------------------------------------------------



#ifdef KRKRSDL2_ENABLE_PLUGINS
//---------------------------------------------------------------------------
// Plug-ins management
//---------------------------------------------------------------------------
struct tTVPPlugin
{
	ttstr Name;
	void *Instance = nullptr;

	tTVPPluginHolder *Holder = nullptr;

	ITSSModule *TSSModule = nullptr;

	tTVPV2LinkProc V2Link = nullptr;
	tTVPV2UnlinkProc V2Unlink = nullptr;


	tTVPGetModuleInstanceProc GetModuleInstance = nullptr;

	std::vector<ttstr> SupportedExts;

	tTVPPlugin(const ttstr & name, ITSSStorageProvider *storageprovider);
	~tTVPPlugin();

	bool Uninit();
};
//---------------------------------------------------------------------------
tTVPPlugin::tTVPPlugin(const ttstr & name, ITSSStorageProvider *storageprovider)
{
	Name = name;

	Instance = NULL;
	Holder = new tTVPPluginHolder(name);
	std::string filename;
	if (TVPUtf16ToUtf8(filename, Holder->GetLocalName().AsStdString()))
	{
		if (TVPCheckExistentLocalFile(Holder->GetLocalName()))
		{
			Instance = SDL_LoadObject(filename.c_str());
		}
	}

	if(!Instance)
	{
		if (Holder != NULL)
		{
			delete Holder;
		}
		TVPThrowExceptionMessage(TVPCannotLoadPlugin, name);
	}

	try
	{
		// retrieve each functions
		V2Link = (tTVPV2LinkProc)
			SDL_LoadFunction(Instance, "V2Link");
		V2Unlink = (tTVPV2UnlinkProc)
			SDL_LoadFunction(Instance, "V2Unlink");

		GetModuleInstance = (tTVPGetModuleInstanceProc)
			SDL_LoadFunction(Instance, "GetModuleInstance");

		// link
		if(V2Link)
		{
			V2Link(TVPGetFunctionExporter());
		}

		if(GetModuleInstance)
		{
			TSS_HWND mainwin = NULL;
			HRESULT hr = GetModuleInstance(&TSSModule, storageprovider,
				 NULL, mainwin);
			if(FAILED(hr) || TSSModule == NULL)
				TVPThrowExceptionMessage(TVPCannotLoadPlugin, name);

			// get supported extensions
			TSS_ULONG index = 0;
			while(true)
			{
				tjs_char mediashortname[33];
				tjs_char buf[256];
				HRESULT hr = TSSModule->GetSupportExts(index,
					mediashortname, buf, 255);
				if(hr == S_OK)
					SupportedExts.push_back(ttstr(buf).AsLowerCase());
				else
					break;
				index ++;
			}
		}


	}
	catch(...)
	{
		if (Instance != NULL)
		{
			SDL_UnloadObject(Instance);
			Instance = NULL;
		}
		if (Holder != NULL)
		{
			delete Holder;
			Holder = NULL;
		}
		throw;
	}
}
//---------------------------------------------------------------------------
tTVPPlugin::~tTVPPlugin()
{
}
//---------------------------------------------------------------------------
bool tTVPPlugin::Uninit()
{
	tTJS *tjs = TVPGetScriptEngine();
	if(tjs) tjs->DoGarbageCollection(); // to release unused objects

	if(V2Unlink)
	{
 		if(TJS_FAILED(V2Unlink())) return false;
	}

	if (Instance != NULL)
	{
		SDL_UnloadObject(Instance);
		Instance = NULL;
	}
	if (Holder != NULL)
	{
		delete Holder;
		Holder = NULL;
	}
	return true;
}
#endif
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
#ifdef KRKRSDL2_ENABLE_PLUGINS
bool TVPPluginUnloadedAtSystemExit = false;
typedef std::vector<tTVPPlugin*> tTVPPluginVectorType;
#endif
struct tTVPPluginVectorStruc
{
#ifdef KRKRSDL2_ENABLE_PLUGINS
	tTVPPluginVectorType Vector;
#endif
	tTVPStorageProvider StorageProvider;
} static TVPPluginVector;
#ifdef KRKRSDL2_ENABLE_PLUGINS
static void TVPDestroyPluginVector(void)
{
	// state all plugins are to be released
	TVPPluginUnloadedAtSystemExit = true;

	// delete all objects
	tTVPPluginVectorType::iterator i;
	while(TVPPluginVector.Vector.size())
	{
		i = TVPPluginVector.Vector.end() - 1;
		try
		{
			(*i)->Uninit();
			delete *i;
		}
		catch(...)
		{
		}
		TVPPluginVector.Vector.pop_back();
	}
}
tTVPAtExit TVPDestroyPluginVectorAtExit
	(TVP_ATEXIT_PRI_RELEASE, TVPDestroyPluginVector);
#endif
//---------------------------------------------------------------------------
static bool TVPPluginLoading = false;
void TVPLoadPlugin(const ttstr & name)
{
#ifdef KRKRSDL2_ENABLE_PLUGINS
	// load plugin
	if(TVPPluginLoading)
		TVPThrowExceptionMessage(TVPCannnotLinkPluginWhilePluginLinking);
			// linking plugin while other plugin is linking, is prohibited
			// by data security reason.

	// check whether the same plugin was already loaded
	tTVPPluginVectorType::iterator i;
	for(i = TVPPluginVector.Vector.begin();
		i != TVPPluginVector.Vector.end(); i++)
	{
		if((*i)->Name == name) return;
	}

	tTVPPlugin * p;

	try
	{
		TVPPluginLoading = true;
		p = new tTVPPlugin(name, &TVPPluginVector.StorageProvider);
		TVPPluginLoading = false;
	}
	catch(...)
	{
		TVPPluginLoading = false;
		throw;
	}

	TVPPluginVector.Vector.push_back(p);
#endif
}
//---------------------------------------------------------------------------
bool TVPUnloadPlugin(const ttstr & name)
{
	// unload plugin

#ifdef KRKRSDL2_ENABLE_PLUGINS
	tTVPPluginVectorType::iterator i;
	for(i = TVPPluginVector.Vector.begin();
		i != TVPPluginVector.Vector.end(); i++)
	{
		if((*i)->Name == name)
		{
			if(!(*i)->Uninit()) return false;
			delete *i;
			TVPPluginVector.Vector.erase(i);
			return true;
		}
	}
	TVPThrowExceptionMessage(TVPNotLoadedPlugin, name);
	return false;
#else
	return true;
#endif
}
//---------------------------------------------------------------------------





void TVPLoadPluigins(void)
{
}
//---------------------------------------------------------------------------
tjs_int TVPGetAutoLoadPluginCount() { return 0; }
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// interface for built-in Wave decode plugins
//---------------------------------------------------------------------------
struct tTVPTSSModuleWrapper
{
	ITSSModule *TSSModule = nullptr;

	tTVPGetModuleInstanceProc GetModuleInstance = nullptr;

	std::vector<ttstr> SupportedExts;

	tTVPTSSModuleWrapper(tTVPGetModuleInstanceProc GetModuleInstanceProc, ITSSStorageProvider *storageprovider);
	~tTVPTSSModuleWrapper();
};
//---------------------------------------------------------------------------
tTVPTSSModuleWrapper::tTVPTSSModuleWrapper(tTVPGetModuleInstanceProc GetModuleInstanceProc, ITSSStorageProvider *storageprovider)
{
	GetModuleInstance = GetModuleInstanceProc;
	if(GetModuleInstance)
	{
		TSS_HWND mainwin = NULL;
		HRESULT hr = GetModuleInstance(&TSSModule, storageprovider,
			 NULL, mainwin);
		if(FAILED(hr) || TSSModule == NULL)
			TVPThrowExceptionMessage(TJS_W("TSSModule retrieval failure"));

		// get supported extensions
		TSS_ULONG index = 0;
		while(true)
		{
			tjs_char mediashortname[33];
			tjs_char buf[256];
			HRESULT hr = TSSModule->GetSupportExts(index,
				mediashortname, buf, 255);
			if(hr == S_OK)
				SupportedExts.push_back(ttstr(buf).AsLowerCase());
			else
				break;
			index ++;
		}
	}
}
//---------------------------------------------------------------------------
tTVPTSSModuleWrapper::~tTVPTSSModuleWrapper()
{
	if(TSSModule) TSSModule->Release();
}
typedef std::vector<tTVPTSSModuleWrapper*> tTVPTSSModuleWrapperType;
struct tTVPTSSModuleWrapperVectorStruc
{
	tTVPTSSModuleWrapperType Vector;
} static TVPTSSModuleWrapperVector;

void TVPRegisterTSSWaveDecoder(tTVPGetModuleInstanceProc GetModuleInstance)
{
	tTVPTSSModuleWrapper * p;

	try
	{
		p = new tTVPTSSModuleWrapper(GetModuleInstance, &TVPPluginVector.StorageProvider);
	}
	catch(...)
	{
		throw;
	}

	TVPTSSModuleWrapperVector.Vector.push_back(p);
}
//---------------------------------------------------------------------------
// interface to Wave decode plugins
//---------------------------------------------------------------------------
ITSSWaveDecoder * TVPSearchAvailTSSWaveDecoder(const ttstr & storage, const ttstr & extension)
{
	{
		tTVPTSSModuleWrapperType::iterator i;
		for(i = TVPTSSModuleWrapperVector.Vector.begin();
			i != TVPTSSModuleWrapperVector.Vector.end(); i++)
		{
			if((*i)->TSSModule)
			{
				// check whether the plugin supports extension
				bool supported = false;
				std::vector<ttstr>::iterator ei;
				for(ei = (*i)->SupportedExts.begin(); ei != (*i)->SupportedExts.end(); ei++)
				{
					if(ei->GetLen() == 0) { supported = true; break; }
					if(extension == *ei) { supported = true; break; }
				}

				if(!supported) continue;

				// retrieve instance from (*i)->TSSModule
				IUnknown *intf = NULL;
				HRESULT hr = (*i)->TSSModule->GetMediaInstance(
					(tjs_char*)storage.c_str(), &intf);
				if(SUCCEEDED(hr))
				{
					try
					{
						// check  whether the instance has IID_ITSSWaveDecoder
						// interface.
						ITSSWaveDecoder * decoder;
						if(SUCCEEDED(intf->QueryInterface(IID_ITSSWaveDecoder,
							(void**) &decoder)))
						{
							intf->Release();
							return decoder; // OK
						}
					}
					catch(...)
					{
						intf->Release();
						throw;
					}
					intf->Release();
				}

			}
		}
	}
#ifdef KRKRSDL2_ENABLE_PLUGINS
	tTVPPluginVectorType::iterator i;
	for(i = TVPPluginVector.Vector.begin();
		i != TVPPluginVector.Vector.end(); i++)
	{
		if((*i)->TSSModule)
		{
			// check whether the plugin supports extension
			bool supported = false;
			std::vector<ttstr>::iterator ei;
			for(ei = (*i)->SupportedExts.begin(); ei != (*i)->SupportedExts.end(); ei++)
			{
				if(ei->GetLen() == 0) { supported = true; break; }
				if(extension == *ei) { supported = true; break; }
			}

			if(!supported) continue;

			// retrieve instance from (*i)->TSSModule
			IUnknown *intf = NULL;
			HRESULT hr = (*i)->TSSModule->GetMediaInstance(
				(tjs_char*)storage.c_str(), &intf);
			if(SUCCEEDED(hr))
			{
				try
				{
					// check  whether the instance has IID_ITSSWaveDecoder
					// interface.
					ITSSWaveDecoder * decoder;
					if(SUCCEEDED(intf->QueryInterface(IID_ITSSWaveDecoder,
						(void**) &decoder)))
					{
						intf->Release();
						return decoder; // OK
					}
				}
				catch(...)
				{
					intf->Release();
					throw;
				}
				intf->Release();
			}

		}
	}
#endif
	return NULL; // not found
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// some service functions for plugin
//---------------------------------------------------------------------------
#include "zlib/zlib.h"
int ZLIB_uncompress(unsigned char *dest, unsigned long *destlen,
	const unsigned char *source, unsigned long sourcelen)
{
	return uncompress(dest, destlen, source, sourcelen);
}
//---------------------------------------------------------------------------
int ZLIB_compress(unsigned char *dest, unsigned long *destlen,
	const unsigned char *source, unsigned long sourcelen)
{
	return compress(dest, destlen, source, sourcelen);
}
//---------------------------------------------------------------------------
int ZLIB_compress2(unsigned char *dest, unsigned long *destlen,
	const unsigned char *source, unsigned long sourcelen, int level)
{
	return compress2(dest, destlen, source, sourcelen, level);
}
//---------------------------------------------------------------------------
#include "md5.h"
static char TVP_assert_md5_state_t_size[
	 (sizeof(TVP_md5_state_t) >= sizeof(md5_state_t))];
	// if this errors, sizeof(TVP_md5_state_t) is not equal to sizeof(md5_state_t).
	// sizeof(TVP_md5_state_t) must be equal to sizeof(md5_state_t).
//---------------------------------------------------------------------------
void TVP_md5_init(TVP_md5_state_t *pms)
{
	md5_init((md5_state_t*)pms);
}
//---------------------------------------------------------------------------
void TVP_md5_append(TVP_md5_state_t *pms, const tjs_uint8 *data, int nbytes)
{
	md5_append((md5_state_t*)pms, (const md5_byte_t*)data, nbytes);
}
//---------------------------------------------------------------------------
void TVP_md5_finish(TVP_md5_state_t *pms, tjs_uint8 *digest)
{
	md5_finish((md5_state_t*)pms, digest);
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void TVPProcessApplicationMessages()
{
}
//---------------------------------------------------------------------------
void TVPHandleApplicationMessage()
{
}
//---------------------------------------------------------------------------
bool TVPRegisterGlobalObject(const tjs_char *name, iTJSDispatch2 * dsp)
{
	// register given object to global object
	tTJSVariant val(dsp);
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	tjs_error er;
	try
	{
		er = global->PropSet(TJS_MEMBERENSURE, name, NULL, &val, global);
	}
	catch(...)
	{
		global->Release();
		return false;
	}
	global->Release();
	return TJS_SUCCEEDED(er);
}
//---------------------------------------------------------------------------
bool TVPRemoveGlobalObject(const tjs_char *name)
{
	// remove registration of global object
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if(!global) return false;
	tjs_error er;
	try
	{
		er = global->DeleteMember(0, name, NULL, global);
	}
	catch(...)
	{
		global->Release();
		return false;
	}
	global->Release();
	return TJS_SUCCEEDED(er);
}
//---------------------------------------------------------------------------
void TVPDoTryBlock(
	tTVPTryBlockFunction tryblock,
	tTVPCatchBlockFunction catchblock,
	tTVPFinallyBlockFunction finallyblock,
	void *data)
{
	try
	{
		tryblock(data);
	}
	catch(const eTJS & e)
	{
		if(finallyblock) finallyblock(data);
		tTVPExceptionDesc desc;
		desc.type = TJS_W("eTJS");
		desc.message = e.GetMessage();
		if(catchblock(data, desc)) throw;
		return;
	}
	catch(...)
	{
		if(finallyblock) finallyblock(data);
		tTVPExceptionDesc desc;
		desc.type = TJS_W("unknown");
		if(catchblock(data, desc)) throw;
		return;
	}
	if(finallyblock) finallyblock(data);
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPGetFileVersionOf
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPCreateNativeClass_Plugins
//---------------------------------------------------------------------------
tTJSNativeClass * TVPCreateNativeClass_Plugins()
{
	tTJSNC_Plugins *cls = new tTJSNC_Plugins();


	// setup some platform-specific members
//---------------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/link)
{
	if(numparams < 1) return TJS_E_BADPARAMCOUNT;

	ttstr name = *param[0];

	TVPLoadPlugin(name);

	return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
	/*func. name*/link)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/unlink)
{
	if(numparams < 1) return TJS_E_BADPARAMCOUNT;

	ttstr name = *param[0];

	bool res = TVPUnloadPlugin(name);

	if(result) *result = (tjs_int)res;

	return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
	/*func. name*/unlink)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(getList)
{
	iTJSDispatch2 * array = TJSCreateArrayObject();
	try
	{
#ifdef KRKRSDL2_ENABLE_PLUGINS
		tTVPPluginVectorType::iterator i;
		tjs_int idx = 0;
		for(i = TVPPluginVector.Vector.begin(); i != TVPPluginVector.Vector.end(); i++)
		{
			tTJSVariant val = (*i)->Name.c_str();
			array->PropSetByNum(TJS_MEMBERENSURE, idx++, &val, array);
		}
#endif

		if (result) *result = tTJSVariant(array, array);
	}
	catch(...)
	{
		array->Release();
		throw;
	}
	array->Release();
	return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(cls, getList)
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
	return cls;
}
//---------------------------------------------------------------------------



