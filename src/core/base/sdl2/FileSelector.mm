//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// File Selector dialog box (macOS)
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "StorageImpl.h"

#import <AppKit/AppKit.h>

//---------------------------------------------------------------------------
static NSArray<NSString *> *TVPParseFilterExtensions(const tjs_string &wild)
{
	// Parse "*.ext1;*.ext2" into ["ext1", "ext2"]
	NSMutableArray *exts = [NSMutableArray array];
	std::string s = ttstr(wild).AsNarrowStdString();
	NSString *ns = [NSString stringWithUTF8String:s.c_str()];
	NSArray *parts = [ns componentsSeparatedByString:@";"];
	for (NSString *part in parts)
	{
		NSString *trimmed = [part stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
		if ([trimmed hasPrefix:@"*."])
		{
			NSString *ext = [trimmed substringFromIndex:2];
			if (ext.length > 0 && ![ext isEqualToString:@"*"])
				[exts addObject:ext];
		}
	}
	return exts;
}
//---------------------------------------------------------------------------
struct TVPFilterEntry
{
	tjs_string name;
	tjs_string wild;
	NSArray<NSString *> *extensions;
};
//---------------------------------------------------------------------------
static void TVPPushFilterPair(std::vector<TVPFilterEntry> &filters, const tjs_string &filter)
{
	TVPFilterEntry entry;
	tjs_string::size_type vpos = filter.find_first_of(TJS_W("|"));
	if (vpos != tjs_string::npos)
	{
		entry.name = filter.substr(0, vpos);
		entry.wild = filter.substr(vpos + 1);
	}
	else
	{
		entry.name = filter;
		entry.wild = filter;
	}
	entry.extensions = TVPParseFilterExtensions(entry.wild);
	filters.push_back(entry);
}
//---------------------------------------------------------------------------
bool TVPSelectFile(iTJSDispatch2 *params)
{
	tTJSVariant val;
	std::vector<TVPFilterEntry> filterlist;
	tjs_int filterIndex = 0;
	bool issave = false;

	// get filter
	if (TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("filter"), 0, &val, params)))
	{
		if (val.Type() != tvtObject)
		{
			TVPPushFilterPair(filterlist, ttstr(val).AsStdString());
		}
		else
		{
			iTJSDispatch2 *array = val.AsObjectNoAddRef();
			tjs_int count = 0;
			tTJSVariant tmp;
			if (TJS_SUCCEEDED(array->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("count"), 0, &tmp, array)))
				count = tmp;
			for (tjs_int i = 0; i < count; i++)
			{
				if (TJS_SUCCEEDED(array->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &tmp, array)))
					TVPPushFilterPair(filterlist, ttstr(tmp).AsStdString());
			}
		}
	}

	// get filterIndex (1-based)
	if (TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("filterIndex"), 0, &val, params)))
		filterIndex = (tjs_int)val;

	// get save flag
	if (TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("save"), 0, &val, params)))
		issave = val.operator bool();

	// get initial filename
	NSString *initialName = nil;
	if (TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("name"), 0, &val, params)))
	{
		ttstr lname(val);
		if (!lname.IsEmpty())
		{
			lname = TVPNormalizeStorageName(lname);
			TVPGetLocalName(lname);
			initialName = [NSString stringWithUTF8String:lname.AsNarrowStdString().c_str()];
		}
	}

	// get initial directory
	NSString *initialDir = nil;
	if (TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("initialDir"), 0, &val, params)))
	{
		ttstr lname(val);
		if (!lname.IsEmpty())
		{
			lname = TVPNormalizeStorageName(lname);
			TVPGetLocalName(lname);
			initialDir = [NSString stringWithUTF8String:lname.AsNarrowStdString().c_str()];
		}
	}

	// get title
	NSString *title = nil;
	if (TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("title"), 0, &val, params)))
	{
		title = [NSString stringWithUTF8String:ttstr(val).AsNarrowStdString().c_str()];
	}

	// get default extension
	NSString *defaultExt = nil;
	if (TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("defaultExt"), 0, &val, params)))
	{
		defaultExt = [NSString stringWithUTF8String:ttstr(val).AsNarrowStdString().c_str()];
	}

	// run panel on main thread
	__block BOOL result = NO;
	__block NSString *selectedPath = nil;
	__block tjs_int selectedFilterIndex = filterIndex;

	void (^showPanel)(void) = ^{
		NSSavePanel *panel;
		if (!issave)
		{
			NSOpenPanel *openPanel = [NSOpenPanel openPanel];
			[openPanel setAllowsMultipleSelection:NO];
			panel = openPanel;
		}
		else
		{
			panel = [NSSavePanel savePanel];
		}

		if (title)
			[panel setTitle:title];
		if (initialDir)
			[panel setDirectoryURL:[NSURL fileURLWithPath:initialDir]];
		if (initialName)
			[panel setNameFieldStringValue:[initialName lastPathComponent]];

		// set up filter popup if we have filters
		NSPopUpButton *filterPopup = nil;
		if (filterlist.size() > 0)
		{
			filterPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 300, 28) pullsDown:NO];
			for (size_t i = 0; i < filterlist.size(); i++)
			{
				NSString *label = [NSString stringWithUTF8String:ttstr(filterlist[i].name).AsNarrowStdString().c_str()];
				[filterPopup addItemWithTitle:label];
			}

			// set initial selection (filterIndex is 1-based)
			if (filterIndex >= 1 && filterIndex <= (tjs_int)filterlist.size())
				[filterPopup selectItemAtIndex:filterIndex - 1];

			// apply initial allowed types
			NSInteger idx = [filterPopup indexOfSelectedItem];
			if (idx >= 0 && idx < (NSInteger)filterlist.size())
			{
				NSArray *exts = filterlist[idx].extensions;
				if (exts.count > 0)
					[panel setAllowedFileTypes:exts];
				else
					[panel setAllowedFileTypes:nil];
			}

			// update allowed types on popup change
			[filterPopup setTarget:panel];
			[filterPopup setAction:@selector(validateVisibleColumns)];

			// use a wrapper view with label
			NSView *accessoryView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 36)];
			[filterPopup setFrame:NSMakeRect(80, 4, 310, 28)];
			[accessoryView addSubview:filterPopup];
			[panel setAccessoryView:accessoryView];

			// observe popup changes via KVO-like approach using block
			// We use a simple approach: override validateVisibleColumns won't work,
			// so we use NSPopUpButton action with a helper
		}

		NSModalResponse response = [panel runModal];

		if (filterPopup)
		{
			// read final filter selection
			NSInteger idx = [filterPopup indexOfSelectedItem];
			selectedFilterIndex = (tjs_int)(idx + 1); // convert to 1-based

			// apply the selected filter's extensions before reading result
			if (idx >= 0 && idx < (NSInteger)filterlist.size())
			{
				NSArray *exts = filterlist[idx].extensions;
				if (exts.count > 0)
					[panel setAllowedFileTypes:exts];
			}
		}

		if (response == NSModalResponseOK)
		{
			result = YES;
			selectedPath = [[panel URL] path];
		}
	};

	if ([NSThread isMainThread])
	{
		showPanel();
	}
	else
	{
		dispatch_sync(dispatch_get_main_queue(), showPanel);
	}

	if (result && selectedPath)
	{
		// write back filter index
		val = (tjs_int)selectedFilterIndex;
		params->PropSet(TJS_MEMBERENSURE, TJS_W("filterIndex"), 0, &val, params);

		// write back file name - convert NSString to tjs_char (UTF-16) directly
		NSUInteger len = [selectedPath length];
		tjs_char *buf = new tjs_char[len + 1];
		[selectedPath getCharacters:(unichar *)buf range:NSMakeRange(0, len)];
		buf[len] = 0;
		ttstr path(buf);
		delete[] buf;
		val = TVPNormalizeStorageName(path);
		params->PropSet(TJS_MEMBERENSURE, TJS_W("name"), 0, &val, params);
	}

	return result != NO;
}
//---------------------------------------------------------------------------
