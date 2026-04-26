//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// File Selector dialog box, macOS implementation
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#import <AppKit/AppKit.h>

#include <vector>

#include "CharacterSet.h"
#include "FileSelector.h"
#include "StorageIntf.h"

//---------------------------------------------------------------------------
struct tTVPMacFileFilter
{
	tjs_string Name;
	std::vector<tjs_string> Extensions;
	bool AllowsAll = false;
};
//---------------------------------------------------------------------------
static bool TVPGetSelectFileProp(iTJSDispatch2 *params, const tjs_char *name, tTJSVariant &value)
{
	return TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, name, 0, &value, params));
}
//---------------------------------------------------------------------------
static NSString *TVPNSStringFromTJSString(const tjs_string &string)
{
	std::string utf8;
	if(!TVPUtf16ToUtf8(utf8, string)) return nil;
	return [NSString stringWithUTF8String:utf8.c_str()];
}
//---------------------------------------------------------------------------
static bool TVPTJSStringFromNSString(NSString *string, tjs_string &result)
{
	if(!string) return false;
	const char *utf8 = [string UTF8String];
	if(!utf8) return false;
	return TVPUtf8ToUtf16(result, std::string(utf8));
}
//---------------------------------------------------------------------------
static NSString *TVPLocalPathFromVariant(const tTJSVariant &value)
{
	ttstr local(value);
	if(local.IsEmpty()) return nil;
	local = TVPNormalizeStorageName(local);
	TVPGetLocalName(local);
	return TVPNSStringFromTJSString(local.AsStdString());
}
//---------------------------------------------------------------------------
static tjs_string TVPTrimTJSString(const tjs_string &string)
{
	tjs_string::size_type first = 0;
	tjs_string::size_type last = string.length();
	while(first < last)
	{
		tjs_char ch = string[first];
		if(ch != TJS_W(' ') && ch != TJS_W('\t') && ch != TJS_W('\r') && ch != TJS_W('\n')) break;
		first++;
	}
	while(last > first)
	{
		tjs_char ch = string[last - 1];
		if(ch != TJS_W(' ') && ch != TJS_W('\t') && ch != TJS_W('\r') && ch != TJS_W('\n')) break;
		last--;
	}
	return string.substr(first, last - first);
}
//---------------------------------------------------------------------------
static bool TVPStringContainsWildcard(const tjs_string &string)
{
	return string.find(TJS_W('*')) != tjs_string::npos || string.find(TJS_W('?')) != tjs_string::npos;
}
//---------------------------------------------------------------------------
static bool TVPAddExtensionFromPattern(tTVPMacFileFilter &entry, const tjs_string &pattern)
{
	tjs_string token = TVPTrimTJSString(pattern);
	if(token.empty()) return true;

	if(token == TJS_W("*") || token == TJS_W("*.*"))
	{
		entry.AllowsAll = true;
		return true;
	}

	tjs_string extension;
	if(token.length() >= 3 && token[0] == TJS_W('*') && token[1] == TJS_W('.'))
	{
		extension = token.substr(2);
	}
	else if(token.length() >= 2 && token[0] == TJS_W('.'))
	{
		extension = token.substr(1);
	}
	else if(!TVPStringContainsWildcard(token) && token.find(TJS_W('/')) == tjs_string::npos &&
		token.find(TJS_W('\\')) == tjs_string::npos)
	{
		extension = token;
	}
	else
	{
		return false;
	}

	extension = TVPTrimTJSString(extension);
	if(extension.empty() || extension == TJS_W("*"))
	{
		entry.AllowsAll = true;
		return true;
	}
	if(TVPStringContainsWildcard(extension)) return false;

	entry.Extensions.push_back(extension);
	return true;
}
//---------------------------------------------------------------------------
static void TVPParseFilterPattern(tTVPMacFileFilter &entry, const tjs_string &pattern)
{
	bool had_complex_pattern = false;
	tjs_string::size_type start = 0;

	while(start <= pattern.length())
	{
		tjs_string::size_type end = pattern.find(TJS_W(';'), start);
		tjs_string part = end == tjs_string::npos ? pattern.substr(start) : pattern.substr(start, end - start);
		if(!TVPAddExtensionFromPattern(entry, part)) had_complex_pattern = true;
		if(end == tjs_string::npos) break;
		start = end + 1;
	}

	if(entry.Extensions.empty() && !entry.AllowsAll && had_complex_pattern)
	{
		entry.AllowsAll = true;
	}
}
//---------------------------------------------------------------------------
static void TVPAddFilterEntry(std::vector<tTVPMacFileFilter> &filters, const tjs_string &filter)
{
	tTVPMacFileFilter entry;
	tjs_string::size_type separator = filter.find(TJS_W('|'));
	tjs_string pattern;

	if(separator != tjs_string::npos)
	{
		entry.Name = TVPTrimTJSString(filter.substr(0, separator));
		pattern = filter.substr(separator + 1);
	}
	else
	{
		entry.Name = TVPTrimTJSString(filter);
		pattern = filter;
	}

	TVPParseFilterPattern(entry, pattern);
	if(entry.Name.empty()) entry.Name = pattern;
	if(entry.Extensions.empty() && !entry.AllowsAll) entry.AllowsAll = true;

	filters.push_back(entry);
}
//---------------------------------------------------------------------------
static std::vector<tTVPMacFileFilter> TVPReadFilters(iTJSDispatch2 *params)
{
	std::vector<tTVPMacFileFilter> filters;
	tTJSVariant value;

	if(!TVPGetSelectFileProp(params, TJS_W("filter"), value)) return filters;

	if(value.Type() != tvtObject)
	{
		TVPAddFilterEntry(filters, ttstr(value).AsStdString());
	}
	else
	{
		iTJSDispatch2 *array = value.AsObjectNoAddRef();
		tjs_int count = 0;
		tTJSVariant tmp;
		if(TJS_SUCCEEDED(array->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("count"), 0, &tmp, array)))
		{
			count = tmp;
		}

		for(tjs_int i = 0; i < count; i++)
		{
			if(TJS_SUCCEEDED(array->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &tmp, array)))
			{
				TVPAddFilterEntry(filters, ttstr(tmp).AsStdString());
			}
		}
	}

	return filters;
}
//---------------------------------------------------------------------------
static NSArray *TVPAllowedFileTypesForFilter(const tTVPMacFileFilter &filter)
{
	if(filter.AllowsAll || filter.Extensions.empty()) return nil;

	NSMutableArray *types = [NSMutableArray arrayWithCapacity:filter.Extensions.size()];
	for(std::vector<tjs_string>::const_iterator i = filter.Extensions.begin(); i != filter.Extensions.end(); ++i)
	{
		NSString *extension = TVPNSStringFromTJSString(*i);
		if(extension && [extension length] > 0) [types addObject:extension];
	}

	return [types count] == 0 ? nil : types;
}
//---------------------------------------------------------------------------
static void TVPApplyFilterToPanel(NSSavePanel *panel, const tTVPMacFileFilter &filter)
{
	NSArray *types = TVPAllowedFileTypesForFilter(filter);
	[panel setAllowedFileTypes:types];
	[panel setAllowsOtherFileTypes:![panel isKindOfClass:[NSOpenPanel class]] || types == nil];
}
//---------------------------------------------------------------------------
static tjs_int TVPGetInitialFilterIndex(iTJSDispatch2 *params, size_t filter_count)
{
	if(filter_count == 0) return 0;

	tTJSVariant value;
	tjs_int index = 1;
	if(TVPGetSelectFileProp(params, TJS_W("filterIndex"), value)) index = value;

	if(index < 1 || static_cast<size_t>(index) > filter_count) index = 1;
	return index;
}
//---------------------------------------------------------------------------
static NSString *TVPDefaultExtensionFromParams(iTJSDispatch2 *params)
{
	tTJSVariant value;
	if(!TVPGetSelectFileProp(params, TJS_W("defaultExt"), value)) return nil;

	tjs_string extension = TVPTrimTJSString(ttstr(value).AsStdString());
	while(!extension.empty() && extension[0] == TJS_W('.')) extension.erase(extension.begin());
	if(extension.empty()) return nil;
	return TVPNSStringFromTJSString(extension);
}
//---------------------------------------------------------------------------
static NSString *TVPPathByAppendingDefaultExtension(NSString *path, NSString *default_extension)
{
	if(!path || !default_extension || [default_extension length] == 0) return path;
	if([[path pathExtension] length] != 0) return path;
	return [path stringByAppendingPathExtension:default_extension];
}
//---------------------------------------------------------------------------
static void TVPSetPanelDirectory(NSSavePanel *panel, NSString *path)
{
	if(!path || [path length] == 0) return;
	[panel setDirectoryURL:[NSURL fileURLWithPath:path isDirectory:YES]];
}
//---------------------------------------------------------------------------
static void TVPApplyNameParameterToPanel(NSSavePanel *panel, NSString *path, bool is_save, bool has_initial_dir)
{
	if(!path || [path length] == 0) return;

	BOOL is_directory = NO;
	BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&is_directory];
	if(exists && is_directory)
	{
		if(!has_initial_dir) TVPSetPanelDirectory(panel, path);
		return;
	}

	NSString *directory = [path stringByDeletingLastPathComponent];
	NSString *filename = [path lastPathComponent];

	if(is_save && filename && [filename length] > 0)
	{
		[panel setNameFieldStringValue:filename];
	}
	if(!has_initial_dir && directory && [directory length] > 0)
	{
		TVPSetPanelDirectory(panel, directory);
	}
}
//---------------------------------------------------------------------------
@interface TVPMacFileFilterController : NSObject
{
	NSSavePanel *Panel;
	std::vector<tTVPMacFileFilter> *Filters;
}
- (id)initWithPanel:(NSSavePanel *)panel filters:(std::vector<tTVPMacFileFilter> *)filters;
- (void)filterChanged:(id)sender;
@end
//---------------------------------------------------------------------------
@implementation TVPMacFileFilterController
- (id)initWithPanel:(NSSavePanel *)panel filters:(std::vector<tTVPMacFileFilter> *)filters
{
	self = [super init];
	if(self)
	{
		Panel = panel;
		Filters = filters;
	}
	return self;
}

- (void)filterChanged:(id)sender
{
	NSInteger index = [(NSPopUpButton *)sender indexOfSelectedItem];
	if(index >= 0 && static_cast<size_t>(index) < Filters->size())
	{
		TVPApplyFilterToPanel(Panel, (*Filters)[index]);
	}
}
@end
//---------------------------------------------------------------------------
static NSPopUpButton *TVPCreateFilterPopup(
	NSSavePanel *panel,
	std::vector<tTVPMacFileFilter> &filters,
	tjs_int initial_filter_index,
	TVPMacFileFilterController **controller)
{
	if(filters.size() <= 1) return nil;

	NSView *accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 280, 28)];
	NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 280, 26) pullsDown:NO];

	for(std::vector<tTVPMacFileFilter>::const_iterator i = filters.begin(); i != filters.end(); ++i)
	{
		NSString *title = TVPNSStringFromTJSString(i->Name);
		[popup addItemWithTitle:title ? title : @"File Type"];
	}

	[popup selectItemAtIndex:initial_filter_index - 1];
	*controller = [[TVPMacFileFilterController alloc] initWithPanel:panel filters:&filters];
	[popup setTarget:*controller];
	[popup setAction:@selector(filterChanged:)];

	[accessory addSubview:popup];
	[panel setAccessoryView:accessory];
	[accessory release];

	return [popup autorelease];
}
//---------------------------------------------------------------------------
bool TVPSelectFile(iTJSDispatch2 *params)
{
	if(!params) return false;

	bool result = false;

	@autoreleasepool
	{
		[NSApplication sharedApplication];

		tTJSVariant value;
		bool is_save = false;
		if(TVPGetSelectFileProp(params, TJS_W("save"), value)) is_save = value.operator bool();

		NSSavePanel *panel;
		if(is_save)
		{
			panel = [NSSavePanel savePanel];
			[panel setCanCreateDirectories:YES];
			[panel setExtensionHidden:NO];
		}
		else
		{
			NSOpenPanel *open_panel = [NSOpenPanel openPanel];
			[open_panel setCanChooseFiles:YES];
			[open_panel setCanChooseDirectories:NO];
			[open_panel setAllowsMultipleSelection:NO];
			[open_panel setResolvesAliases:YES];
			panel = open_panel;
		}

		if(TVPGetSelectFileProp(params, TJS_W("title"), value))
		{
			NSString *title = TVPNSStringFromTJSString(ttstr(value).AsStdString());
			if(title) [panel setTitle:title];
		}

		bool has_initial_dir = false;
		if(TVPGetSelectFileProp(params, TJS_W("initialDir"), value))
		{
			NSString *initial_dir = TVPLocalPathFromVariant(value);
			if(initial_dir && [initial_dir length] > 0)
			{
				TVPSetPanelDirectory(panel, initial_dir);
				has_initial_dir = true;
			}
		}

		if(TVPGetSelectFileProp(params, TJS_W("name"), value))
		{
			TVPApplyNameParameterToPanel(panel, TVPLocalPathFromVariant(value), is_save, has_initial_dir);
		}

		std::vector<tTVPMacFileFilter> filters = TVPReadFilters(params);
		tjs_int initial_filter_index = TVPGetInitialFilterIndex(params, filters.size());
		if(initial_filter_index > 0)
		{
			TVPApplyFilterToPanel(panel, filters[initial_filter_index - 1]);
		}

		TVPMacFileFilterController *filter_controller = nil;
		NSPopUpButton *filter_popup = TVPCreateFilterPopup(panel, filters, initial_filter_index, &filter_controller);

		NSInteger response = [panel runModal];
		if(response == NSModalResponseOK)
		{
			NSString *path = [[panel URL] path];
			if(is_save)
			{
				path = TVPPathByAppendingDefaultExtension(path, TVPDefaultExtensionFromParams(params));
			}

			tjs_string path_utf16;
			if(TVPTJSStringFromNSString(path, path_utf16))
			{
				tTJSVariant out_value;

				tjs_int selected_filter_index = 0;
				if(!filters.empty())
				{
					selected_filter_index = filter_popup ? static_cast<tjs_int>([filter_popup indexOfSelectedItem] + 1) : initial_filter_index;
				}
				out_value = selected_filter_index;
				params->PropSet(TJS_MEMBERENSURE, TJS_W("filterIndex"), 0, &out_value, params);

				out_value = TVPNormalizeStorageName(ttstr(path_utf16));
				params->PropSet(TJS_MEMBERENSURE, TJS_W("name"), 0, &out_value, params);

				result = true;
			}
		}

		if(filter_controller) [filter_controller release];
	}

	return result;
}
//---------------------------------------------------------------------------
