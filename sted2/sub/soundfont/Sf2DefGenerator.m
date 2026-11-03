/* Sf2DefGenerator.m — thin Cocoa wrapper around sf2_def_gen. */

#import "Sf2DefGenerator.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "sf2_def_gen.h"

@implementation Sf2DefGenerator

+ (void)run
{
	NSOpenPanel *panel = [NSOpenPanel openPanel];
	char banks[1024];
	NSURL *dirURL;

	sf2_def_default_banks_dir(banks, sizeof banks);

	[panel setCanChooseFiles:YES];
	[panel setCanChooseDirectories:NO];
	[panel setAllowsMultipleSelection:NO];
	[panel setTitle:@"Generate DEF from SF2"];
	[panel setMessage:@"Choose a SoundFont under ~/Library/Audio/Sounds/Banks"];
	if (@available(macOS 11.0, *)) {
		UTType *sf2 = [UTType typeWithFilenameExtension:@"sf2"];
		if (sf2 != nil)
			[panel setAllowedContentTypes:[NSArray arrayWithObject:sf2]];
	} else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		[panel setAllowedFileTypes:[NSArray arrayWithObject:@"sf2"]];
#pragma clang diagnostic pop
	}
	if (banks[0] != 0) {
		dirURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:banks]
				    isDirectory:YES];
		if (dirURL != nil)
			[panel setDirectoryURL:dirURL];
	}

	if ([panel runModal] != NSModalResponseOK)
		return;

	{
		NSURL *url = [[panel URLs] objectAtIndex:0];
		const char *sf2Path = [[url path] fileSystemRepresentation];
		char out_dir[1024], out_path[1024], err[256];
		Sf2DefGenResult result;
		NSAlert *alert;
		NSString *outPathStr;
		int overwrite = 0;
		int rc;

		sf2_def_default_out_dir(out_dir, sizeof out_dir);
		err[0] = 0;
		out_path[0] = 0;

		if (sf2_def_planned_out_path(sf2Path, out_dir, out_path,
					     sizeof out_path) != SF2_DEF_OK) {
			alert = [[[NSAlert alloc] init] autorelease];
			[alert setMessageText:@"SF2 → DEF failed"];
			[alert setInformativeText:@"Could not build output path."];
			[alert addButtonWithTitle:@"OK"];
			[alert runModal];
			return;
		}

		outPathStr = [NSString stringWithUTF8String:out_path];
		if ([[NSFileManager defaultManager] fileExistsAtPath:outPathStr]) {
			alert = [[[NSAlert alloc] init] autorelease];
			[alert setMessageText:@"Replace existing DEF?"];
			[alert setInformativeText:
				[NSString stringWithFormat:
					@"A file with this name already exists and may have been edited.\n\n%@\n\nReplace it?",
					outPathStr]];
			[alert addButtonWithTitle:@"Replace"];
			[alert addButtonWithTitle:@"Cancel"];
			if ([alert runModal] != NSAlertFirstButtonReturn)
				return;
			overwrite = 1;
		}

		rc = sf2_def_generate(sf2Path, out_dir, out_path, sizeof out_path,
				      &result, err, sizeof err, overwrite);
		if (rc == SF2_DEF_EXISTS) {
			/* Race: appeared after our check — ask once more. */
			alert = [[[NSAlert alloc] init] autorelease];
			[alert setMessageText:@"Replace existing DEF?"];
			[alert setInformativeText:
				[NSString stringWithFormat:
					@"A file with this name already exists and may have been edited.\n\n%s\n\nReplace it?",
					out_path]];
			[alert addButtonWithTitle:@"Replace"];
			[alert addButtonWithTitle:@"Cancel"];
			if ([alert runModal] != NSAlertFirstButtonReturn)
				return;
			rc = sf2_def_generate(sf2Path, out_dir, out_path, sizeof out_path,
					     &result, err, sizeof err, 1);
		}
		if (rc != SF2_DEF_OK) {
			alert = [[[NSAlert alloc] init] autorelease];
			[alert setMessageText:@"SF2 → DEF failed"];
			[alert setInformativeText:
				[NSString stringWithUTF8String:err[0] ? err : "Unknown error"]];
			[alert addButtonWithTitle:@"OK"];
			[alert runModal];
			return;
		}

		alert = [[[NSAlert alloc] init] autorelease];
		[alert setMessageText:@"SF2 → DEF"];
		[alert setInformativeText:
			[NSString stringWithFormat:
				@"Wrote %d preset slot(s) from %d TinySoundFont preset(s).\n\n%s",
				result.preset_slots, result.tsf_preset_count, out_path]];
		[alert addButtonWithTitle:@"OK"];
		[alert runModal];
	}
}

@end
