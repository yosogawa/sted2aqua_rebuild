/* About panel. The three texts are placeholders for a later edit.
   License text will come from sted2/doc/STed2_help/gpl.html (GPL 2)
   and sted2/fonts/OFL.txt, with the font copyright from the TTF name table. */
/* Copyright 2026 Yoshinori Ogawa */

#import "AboutPanelController.h"

/* Body and chrome come from Localizable.strings (en / ja).
   The xib holds the layout only. */

@implementation AboutPanelController

+ (void)showPanel
{
	static AboutPanelController *panel = nil;

	if (panel == nil)
		panel = [[AboutPanelController alloc] initWithWindowNibName:@"AboutPanel"];
	[panel showWindow:nil];
}

- (void)windowDidLoad
{
	NSBundle *bundle;
	NSString *shortVersion;
	NSString *build;
	NSImage *icon;

	[super windowDidLoad];
	[[self window] setReleasedWhenClosed:NO];
	[[self window] setContentMinSize:NSMakeSize(480, 300)];
	[[self window] setTitle:NSLocalizedString(@"About STed2", @"About window title")];
	[self.authorsButton setTitle:NSLocalizedString(@"Authors", @"About page button")];
	[self.licenseButton setTitle:NSLocalizedString(@"License", @"About page button")];
	[self.thanksButton setTitle:NSLocalizedString(@"Thanks", @"About page button")];
	[self.memorialButton setTitle:NSLocalizedString(@"RIP", @"Memorial notice button")];
	[[self window] center];

	icon = [NSApp applicationIconImage];
	if (icon != nil)
		[self.iconView setImage:icon];

	bundle = [NSBundle mainBundle];
	shortVersion = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
	build = [bundle objectForInfoDictionaryKey:@"CFBundleVersion"];
	if (shortVersion == nil)
		shortVersion = @"";
	if (build == nil)
		build = @"";
	[self.versionField setStringValue:[NSString stringWithFormat:@"%@ (%@)", shortVersion, build]];

	[self.authorsButton setButtonType:NSButtonTypePushOnPushOff];
	[self.licenseButton setButtonType:NSButtonTypePushOnPushOff];
	[self.thanksButton setButtonType:NSButtonTypePushOnPushOff];
	[self.memorialButton setButtonType:NSButtonTypePushOnPushOff];
	[[self.bodyText textContainer] setWidthTracksTextView:YES];
	[self.bodyText setEditable:NO];
	[self.bodyText setSelectable:YES];
	[self showAuthors:self.authorsButton];
}

- (NSString *)memorialString
{
	NSString *path;
	NSString *text;

	path = [[NSBundle mainBundle] pathForResource:@"tuitou" ofType:@"doc"];
	text = nil;
	if (path != nil)
		text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
	if (text == nil)
		text = @"";
	text = [text stringByReplacingOccurrencesOfString:@"\x1a" withString:@""];
	text = [text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
	text = [text stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
	return text;
}

- (void)showText:(NSString *)text button:(NSButton *)button
{
	NSFont *font;

	[self.authorsButton setState:NSControlStateValueOff];
	[self.licenseButton setState:NSControlStateValueOff];
	[self.thanksButton setState:NSControlStateValueOff];
	[self.memorialButton setState:NSControlStateValueOff];
	[button setState:NSControlStateValueOn];

	font = [self.bodyText font];
	[self.bodyText setString:text != nil ? text : @""];
	if (font != nil)
		[self.bodyText setFont:font];
	[self.bodyText scrollRangeToVisible:NSMakeRange(0, 0)];
}

- (IBAction)showAuthors:(id)sender
{
	(void)sender;
	[self showText:NSLocalizedString(@"about.authors", @"About authors page") button:self.authorsButton];
}

- (IBAction)showLicense:(id)sender
{
	(void)sender;
	[self showText:NSLocalizedString(@"about.license", @"About license page") button:self.licenseButton];
}

- (IBAction)showThanks:(id)sender
{
	(void)sender;
	[self showText:NSLocalizedString(@"about.thanks", @"About thanks page") button:self.thanksButton];
}

- (IBAction)showMemorial:(id)sender
{
	(void)sender;
	[self showText:[self memorialString] button:self.memorialButton];
}

@end
