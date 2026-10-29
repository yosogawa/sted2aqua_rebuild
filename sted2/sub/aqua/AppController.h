/* AppController.h */
/* Header file for AppController, the application controller object for sted2_aqua. */
/* Jun.01.2003 Toshi Nagata */
/* Copyright 2003 Toshi Nagata. All rights reserved. */

#import <Cocoa/Cocoa.h>

@class X68kWindow;
@class X68kView;

@interface AppController : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
{
    IBOutlet NSWindow *myWindow;
    NSFont *fontPanelPendingFont;
    NSPanel *fontPickerPanel;
    NSTableView *fontPickerTable;
    NSTextField *fontPickerPreview;
    NSArray *fontPickerEntries; /* @{ @"psName", @"title" } */
}
- (void)startX68kThread:(id)argument;
- (IBAction)midiSettingsDialog:(id)sender;
- (IBAction)showAboutPanel:(id)sender;
- (IBAction)dlsSynthDialog:(id)sender;
- (IBAction)allSoundOff:(id)sender;
- (IBAction)toggleThruMode:(id)sender;
- (IBAction)setSyncMode:(id)sender;
- (IBAction)setDisplayScale:(id)sender;
- (IBAction)showFontPanel:(id)sender;
- (IBAction)applyFontPanelSelection:(id)sender;
@end
