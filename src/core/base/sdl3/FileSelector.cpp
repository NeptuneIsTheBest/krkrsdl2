//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// File Selector dialog box
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "CharacterSet.h"
#include "DebugIntf.h"
#include "FileSelector.h"
#include "StorageImpl.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <string>
#include <vector>

namespace
{
struct tTVPOwnedDialogFilter
{
	std::string Name;
	std::string Pattern;
};

enum class tTVPDialogResult
{
	Pending,
	Selected,
	Canceled,
	Failed,
};

struct tTVPDialogCallbackState
{
	std::atomic<bool> Complete;
	tTVPDialogResult Result;
	std::string FileName;
	std::string Error;
	int FilterIndex;

	tTVPDialogCallbackState()
		: Complete(false), Result(tTVPDialogResult::Pending), FilterIndex(-1)
	{
	}
};

static bool TVPIsASCIISpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static std::string TVPTrimASCII(const std::string &value)
{
	std::string::size_type begin = 0;
	while(begin < value.size() && TVPIsASCIISpace(value[begin])) ++begin;

	std::string::size_type end = value.size();
	while(end > begin && TVPIsASCIISpace(value[end - 1])) --end;

	return value.substr(begin, end - begin);
}

static bool TVPStringToUTF8(const ttstr &value, std::string &result)
{
	if(value.IsEmpty())
	{
		result.clear();
		return true;
	}
	return TVPUtf16ToUtf8(result, value.AsStdString());
}

static bool TVPStorageNameToLocalUTF8(const ttstr &value, std::string &result)
{
	if(value.IsEmpty())
	{
		result.clear();
		return true;
	}

	ttstr local_name(TVPNormalizeStorageName(value));
	TVPGetLocalName(local_name);
	return TVPStringToUTF8(local_name, result);
}

static void TVPLogFileDialogError(const std::string &detail)
{
	ttstr message(TJS_W("File selector failed"));
	if(!detail.empty())
	{
		tjs_string detail_utf16;
		if(TVPUtf8ToUtf16(detail_utf16, detail))
		{
			message += TJS_W(": ");
			message += ttstr(detail_utf16);
		}
	}
	TVPAddLog(message);
}

static bool TVPIsValidSDLDialogExtension(const std::string &extension)
{
	if(extension.empty()) return false;

	for(std::string::const_iterator i = extension.begin(); i != extension.end(); ++i)
	{
		const char c = *i;
		if((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.')
		{
			continue;
		}
		return false;
	}
	return true;
}

static bool TVPConvertDialogFilterPattern(const std::string &value, std::string &result)
{
	// Kirikiri uses Win32-style patterns ("*.png;*.jpg"), while SDL expects
	// a semicolon-separated extension list ("png;jpg").
	result.clear();
	std::string::size_type begin = 0;

	for(;;)
	{
		const std::string::size_type end = value.find(';', begin);
		std::string extension = TVPTrimASCII(value.substr(
			begin, end == std::string::npos ? std::string::npos : end - begin));
		if(extension.empty())
		{
			if(end == std::string::npos) break;
			begin = end + 1;
			continue;
		}

		if(extension == "*" || extension == "*.*")
		{
			result = "*";
			return true;
		}
		if(extension.size() >= 2 && extension[0] == '*' && extension[1] == '.')
		{
			extension.erase(0, 2);
		}
		else if(!extension.empty() && extension[0] == '.')
		{
			extension.erase(0, 1);
		}

		if(!TVPIsValidSDLDialogExtension(extension))
		{
			result = "*";
			return false;
		}

		if(!result.empty()) result += ';';
		result += extension;

		if(end == std::string::npos) break;
		begin = end + 1;
	}

	if(result.empty())
	{
		result = "*";
		return false;
	}
	return true;
}

static bool TVPAppendDialogFilter(const tTJSVariant &value,
	std::vector<tTVPOwnedDialogFilter> &filters, bool &used_fallback)
{
	std::string specification;
	if(!TVPStringToUTF8(ttstr(value), specification)) return false;

	const std::string::size_type separator = specification.find('|');
	std::string name;
	std::string pattern;
	if(separator == std::string::npos)
	{
		name = TVPTrimASCII(specification);
		pattern = specification;
	}
	else
	{
		name = TVPTrimASCII(specification.substr(0, separator));
		pattern = specification.substr(separator + 1);
	}

	tTVPOwnedDialogFilter filter;
	if(!TVPConvertDialogFilterPattern(pattern, filter.Pattern)) used_fallback = true;
	filter.Name = name.empty() ? TVPTrimASCII(pattern) : name;
	if(filter.Name.empty()) filter.Name = "Files";
	filters.push_back(filter);
	return true;
}

static bool TVPBuildDialogFilters(iTJSDispatch2 *params,
	std::vector<tTVPOwnedDialogFilter> &filters, bool &used_fallback)
{
	tTJSVariant value;
	if(TJS_FAILED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("filter"), 0,
		&value, params)))
	{
		return true;
	}

	if(value.Type() != tvtObject)
	{
		return TVPAppendDialogFilter(value, filters, used_fallback);
	}

	iTJSDispatch2 *array = value.AsObjectNoAddRef();
	if(!array) return true;

	tjs_int count = 0;
	tTJSVariant count_value;
	if(TJS_SUCCEEDED(array->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("count"), 0,
		&count_value, array)))
	{
		count = count_value;
	}

	for(tjs_int i = 0; i < count; ++i)
	{
		tTJSVariant item;
		if(TJS_SUCCEEDED(array->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &item, array)) &&
			!TVPAppendDialogFilter(item, filters, used_fallback))
		{
			return false;
		}
	}
	return true;
}

static bool TVPIsRelativeDialogName(const ttstr &value)
{
	if(value.IsEmpty()) return false;
	const tjs_string name(value.AsStdString());
	if(name.empty() || name[0] == TJS_W('/')) return false;
	return name.find(TJS_W("://")) == tjs_string::npos;
}

static void TVPEnsureTrailingPathDelimiter(std::string &path)
{
	if(!path.empty() && path[path.size() - 1] != '/') path += '/';
}

static bool TVPBuildDialogLocation(const ttstr &name, const ttstr &initial_dir,
	std::string &location)
{
	location.clear();
	std::string initial_dir_utf8;
	if(!initial_dir.IsEmpty() &&
		!TVPStorageNameToLocalUTF8(initial_dir, initial_dir_utf8))
	{
		return false;
	}

	if(!name.IsEmpty())
	{
		if(!initial_dir_utf8.empty() && TVPIsRelativeDialogName(name))
		{
			std::string relative_name;
			if(!TVPStringToUTF8(name, relative_name)) return false;
			for(std::string::iterator i = relative_name.begin(); i != relative_name.end(); ++i)
			{
				if(*i == '\\') *i = '/';
			}
			TVPEnsureTrailingPathDelimiter(initial_dir_utf8);
			location = initial_dir_utf8 + relative_name;
			return true;
		}
		return TVPStorageNameToLocalUTF8(name, location);
	}

	location = initial_dir_utf8;
	TVPEnsureTrailingPathDelimiter(location);
	return true;
}

static std::string TVPNormalizeDefaultExtension(const std::string &value)
{
	std::string extension = TVPTrimASCII(value);
	const std::string::size_type separator = extension.find(';');
	if(separator != std::string::npos) extension.erase(separator);
	extension = TVPTrimASCII(extension);
	if(extension == "*" || extension == "*.*") return std::string();
	if(extension.size() >= 2 && extension[0] == '*' && extension[1] == '.')
	{
		extension.erase(0, 2);
	}
	while(!extension.empty() && extension[0] == '.') extension.erase(0, 1);
	if(extension.find('/') != std::string::npos ||
		extension.find('\\') != std::string::npos ||
		extension.find('*') != std::string::npos ||
		extension.find('?') != std::string::npos)
	{
		extension.clear();
	}
	return extension;
}

static bool TVPPathHasExtension(const std::string &path)
{
	const std::string::size_type name_begin = path.find_last_of("/\\");
	const std::string::size_type first_name_character =
		name_begin == std::string::npos ? 0 : name_begin + 1;
	const std::string::size_type dot = path.find_last_of('.');
	return dot != std::string::npos && dot > first_name_character && dot + 1 < path.size();
}

static void TVPAppendDefaultExtension(std::string &path, const std::string &extension)
{
	if(path.empty() || extension.empty() || path[path.size() - 1] == '/' ||
		TVPPathHasExtension(path))
	{
		return;
	}
	if(path[path.size() - 1] == '.') path.erase(path.size() - 1);
	path += '.';
	path += extension;
}

static SDL_Window *TVPGetFileDialogParentWindow()
{
	if(SDL_WasInit(SDL_INIT_VIDEO) == 0) return nullptr;

	SDL_Window *window = SDL_GetKeyboardFocus();
	if(window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN)) return window;

	int count = 0;
	SDL_Window **windows = SDL_GetWindows(&count);
	window = nullptr;
	if(windows)
	{
		for(int i = 0; i < count; ++i)
		{
			if(!(SDL_GetWindowFlags(windows[i]) & SDL_WINDOW_HIDDEN))
			{
				window = windows[i];
				break;
			}
		}
		SDL_free(windows);
	}
	return window;
}

static void SDLCALL TVPFileDialogCallback(void *userdata,
	const char * const *file_list, int filter_index)
{
	tTVPDialogCallbackState *state = static_cast<tTVPDialogCallbackState *>(userdata);
	try
	{
		if(!file_list)
		{
			state->Result = tTVPDialogResult::Failed;
			const char *error = SDL_GetError();
			if(error) state->Error = error;
		}
		else if(!file_list[0])
		{
			state->Result = tTVPDialogResult::Canceled;
		}
		else
		{
			state->Result = tTVPDialogResult::Selected;
			state->FileName = file_list[0];
			state->FilterIndex = filter_index;
		}
	}
	catch(...)
	{
		// Never allow a C++ exception to cross SDL's C callback boundary.
		state->Result = tTVPDialogResult::Failed;
		state->Error.clear();
	}
	state->Complete.store(true, std::memory_order_release);
}
}

//---------------------------------------------------------------------------
// TVPSelectFile related
// OS固有のファイル選択ダイアログを表示する
//---------------------------------------------------------------------------
bool TVPSelectFile(iTJSDispatch2 *params)
{
	if(!params) return false;
	if(!SDL_IsMainThread())
	{
		TVPLogFileDialogError("file dialogs must be opened on the main thread");
		return false;
	}

	tTJSVariant value;
	bool save = false;
	if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("save"), 0,
		&value, params)))
	{
		save = value.operator bool();
	}

	ttstr name;
	if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("name"), 0,
		&value, params)))
	{
		name = ttstr(value);
	}

	ttstr initial_dir;
	if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("initialDir"), 0,
		&value, params)))
	{
		initial_dir = ttstr(value);
	}

	std::string location;
	if(!TVPBuildDialogLocation(name, initial_dir, location))
	{
		TVPLogFileDialogError("invalid UTF-16 in the initial file location");
		return false;
	}

	std::string title;
	if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("title"), 0,
		&value, params)) && !TVPStringToUTF8(ttstr(value), title))
	{
		TVPLogFileDialogError("invalid UTF-16 in the dialog title");
		return false;
	}

	std::string default_extension;
	if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("defaultExt"), 0,
		&value, params)))
	{
		std::string raw_default_extension;
		if(!TVPStringToUTF8(ttstr(value), raw_default_extension))
		{
			TVPLogFileDialogError("invalid UTF-16 in the default extension");
			return false;
		}
		default_extension = TVPNormalizeDefaultExtension(raw_default_extension);
	}
	if(save && !name.IsEmpty()) TVPAppendDefaultExtension(location, default_extension);

	std::vector<tTVPOwnedDialogFilter> owned_filters;
	bool used_filter_fallback = false;
	if(!TVPBuildDialogFilters(params, owned_filters, used_filter_fallback))
	{
		TVPLogFileDialogError("invalid UTF-16 in a dialog filter");
		return false;
	}
	if(used_filter_fallback)
	{
		TVPAddLog(TJS_W("File selector: an unsupported wildcard filter was replaced with the all-files filter"));
	}

	std::vector<SDL_DialogFileFilter> filters;
	filters.reserve(owned_filters.size());
	for(std::vector<tTVPOwnedDialogFilter>::const_iterator i = owned_filters.begin();
		i != owned_filters.end(); ++i)
	{
		SDL_DialogFileFilter filter = { i->Name.c_str(), i->Pattern.c_str() };
		filters.push_back(filter);
	}

	SDL_ClearError();
	SDL_PropertiesID properties = SDL_CreateProperties();
	if(!properties)
	{
		TVPLogFileDialogError(SDL_GetError());
		return false;
	}

	bool properties_set = true;
	SDL_Window *parent = TVPGetFileDialogParentWindow();
	if(parent)
	{
		properties_set = SDL_SetPointerProperty(properties,
			SDL_PROP_FILE_DIALOG_WINDOW_POINTER, parent) && properties_set;
	}
	if(!filters.empty())
	{
		properties_set = SDL_SetPointerProperty(properties,
			SDL_PROP_FILE_DIALOG_FILTERS_POINTER, filters.data()) && properties_set;
		properties_set = SDL_SetNumberProperty(properties,
			SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
			static_cast<Sint64>(filters.size())) && properties_set;
	}
	if(!location.empty())
	{
		properties_set = SDL_SetStringProperty(properties,
			SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str()) && properties_set;
	}
	if(!title.empty())
	{
		properties_set = SDL_SetStringProperty(properties,
			SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str()) && properties_set;
	}
	properties_set = SDL_SetBooleanProperty(properties,
		SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false) && properties_set;

	if(!properties_set)
	{
		const std::string error(SDL_GetError());
		SDL_DestroyProperties(properties);
		TVPLogFileDialogError(error);
		return false;
	}

	SDL_ClearError();
	tTVPDialogCallbackState state;
	SDL_ShowFileDialogWithProperties(
		save ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE,
		TVPFileDialogCallback, &state, properties);

	// SDL dialogs are asynchronous, but Storages.selectFile is synchronous.
	// Pump only platform events here so the Cocoa sheet can complete without
	// re-entering Kirikiri's script/event dispatch while this call is active.
	while(!state.Complete.load(std::memory_order_acquire))
	{
		SDL_PumpEvents();
		if(!state.Complete.load(std::memory_order_acquire)) SDL_Delay(10);
	}
	SDL_DestroyProperties(properties);

	if(state.Result == tTVPDialogResult::Failed)
	{
		TVPLogFileDialogError(state.Error);
		return false;
	}
	if(state.Result != tTVPDialogResult::Selected) return false;

	if(save) TVPAppendDefaultExtension(state.FileName, default_extension);
	tjs_string selected_name_utf16;
	if(!TVPUtf8ToUtf16(selected_name_utf16, state.FileName))
	{
		TVPLogFileDialogError("the selected path is not valid UTF-8");
		return false;
	}

	if(state.FilterIndex >= 0)
	{
		value = static_cast<tjs_int>(state.FilterIndex + 1);
		params->PropSet(TJS_MEMBERENSURE, TJS_W("filterIndex"), 0, &value, params);
	}

	value = TVPNormalizeStorageName(ttstr(selected_name_utf16));
	params->PropSet(TJS_MEMBERENSURE, TJS_W("name"), 0, &value, params);
	return true;
}
//---------------------------------------------------------------------------
