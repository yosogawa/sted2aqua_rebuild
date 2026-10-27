/* About panel. Layout lives in en.lproj/AboutPanel.xib. */
/* Copyright 2026 Yoshinori Ogawa */

#import <Cocoa/Cocoa.h>

@interface AboutPanelController : NSWindowController

@property (assign) IBOutlet NSImageView *iconView;
@property (assign) IBOutlet NSTextField *versionField;
@property (assign) IBOutlet NSTextView *bodyText;
@property (assign) IBOutlet NSButton *authorsButton;
@property (assign) IBOutlet NSButton *licenseButton;
@property (assign) IBOutlet NSButton *thanksButton;
@property (assign) IBOutlet NSButton *memorialButton;

+ (void)showPanel;
- (IBAction)showAuthors:(id)sender;
- (IBAction)showLicense:(id)sender;
- (IBAction)showThanks:(id)sender;
- (IBAction)showMemorial:(id)sender;

@end
