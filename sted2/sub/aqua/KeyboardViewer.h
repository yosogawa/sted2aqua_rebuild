/* Floating map of the current Mac keyboard and the loaded STed2 bindings. */
void KeyboardViewerShow(void);
/* flags is NSEvent modifierFlags. Safe to call when the viewer is closed. */
void KeyboardViewerNoteFlags(unsigned long flags);
