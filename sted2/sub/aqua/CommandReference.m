/* Command cheat sheet. Screen changes come from the editor thread. */
#import <Cocoa/Cocoa.h>
#include <pthread.h>
#include "CommandReference.h"
#include "sted.h"

static const char *kNameJa[STED_SCR_COUNT] = {
	"画面を待機中",
	"コマンドメニュー",
	"トラックエディタ",
	"リズムトラックエディタ",
	"ビジュアルエディタ",
	"トラック設定",
	"トラックセレクタ",
	"TIME & KEY",
	"メモ",
	"オプション",
	"パートアサイン",
	"リズムアサイン",
	"レコードフィルター",
	"ユーザーエクスクルーシブ",
	"トラックエクスクルーシブ",
	"ドットエディタ",
	"CM64 コンソール",
	"SC55 コンソール",
	"ST/GT 設定",
	"MIDI 入力モニター",
	"ファイル選択",
	"音色セレクタ",
	"タグジャンプ",
	"スペシャルコントローラ",
	"置換セレクタ",
	"コードセレクタ",
	"スケールセレクタ",
};

static const char *kNameEn[STED_SCR_COUNT] = {
	"Waiting for a screen",
	"Command menu",
	"Track editor",
	"Rhythm track editor",
	"Visual editor",
	"Track settings",
	"Track selector",
	"Time & Key",
	"Memo",
	"Option",
	"Part assign",
	"Rhythm assign",
	"Record filter",
	"User exclusive",
	"Track exclusive",
	"Dot editor",
	"CM64 console",
	"SC55 console",
	"ST/GT setup",
	"MIDI input monitor",
	"File selection",
	"Tone selector",
	"Tag jump",
	"Special controller",
	"Replace selector",
	"Chord selector",
	"Scale selector",
};

/* group 0: always. 1: F1-F10. 2: Shift+F1-F10. 3: one-touch heading.
 * keys NULL is a heading. keys "" is a full-width note. */
typedef struct {
	int screen;
	int group;
	const char *keys;
	const char *ja;
	const char *en;
} Row;

static const Row kRows[] = {
	{ STED_SCR_COMMAND, 0, "up | down", "項目を移動する", "Move through the menu" },
	{ STED_SCR_COMMAND, 0, "ret | space", "選んでいる項目を実行する", "Run the selected item" },
	{ STED_SCR_COMMAND, 0, "L", "データをロードする", "Load a song" },
	{ STED_SCR_COMMAND, 0, "S", "データをセーブする", "Save the song" },
	{ STED_SCR_COMMAND, 0, "E", "トラック設定を開く", "Open track settings" },
	{ STED_SCR_COMMAND, 0, "P | ctrl,P", "演奏する", "Play" },
	{ STED_SCR_COMMAND, 0, "R", "録音画面を開く。Shift では入力モニター", "Record. Shift opens the input monitor" },
	{ STED_SCR_COMMAND, 0, "F", "レコードフィルター", "Record filter" },
	{ STED_SCR_COMMAND, 0, "K", "TIME & KEY", "Time & Key" },
	{ STED_SCR_COMMAND, 0, "T", "タイトルを編集する", "Edit the title" },
	{ STED_SCR_COMMAND, 0, "M", "メモを編集する", "Edit the memo" },
	{ STED_SCR_COMMAND, 0, "G", "パートアサイン", "Part assign" },
	{ STED_SCR_COMMAND, 0, "A", "リズムアサイン", "Rhythm assign" },
	{ STED_SCR_COMMAND, 0, "U", "ユーザーエクスクルーシブ", "User exclusive" },
	{ STED_SCR_COMMAND, 0, "C", "CM64 コンソール。Shift では SC55", "CM64 console. Shift opens SC55" },
	{ STED_SCR_COMMAND, 0, "O", "オプション", "Option" },
	{ STED_SCR_COMMAND, 0, "D", "音源定義ファイルを読み込む", "Load a module define file" },
	{ STED_SCR_COMMAND, 0, "I", "全データを初期化する", "Initialize all data" },
	{ STED_SCR_COMMAND, 0, "X", "終了する。Shift では確認なし", "Quit. Shift skips the confirmation" },
	{ STED_SCR_COMMAND, 0, "Z", "音源へコントロールデータを送る", "Send control data to the module" },
	{ STED_SCR_COMMAND, 0, "xf1", "メジャープレイ。Shift ではトラック単位のポイントプレイ", "Measure play. Shift plays from the cursor on this track" },
	{ STED_SCR_COMMAND, 0, "xf2", "全トラックのポイントプレイ。Shift では小節指定", "Point play, all tracks. Shift asks for a measure" },
	{ STED_SCR_COMMAND, 0, "esc", "演奏を止める", "Stop playback" },

	{ STED_SCR_TRACK_EDIT, 0, NULL, "移動", "Move" },
	{ STED_SCR_TRACK_EDIT, 0, "left", "カーソルを左へ", "Cursor left" },
	{ STED_SCR_TRACK_EDIT, 0, "right | space", "カーソルを右へ", "Cursor right" },
	{ STED_SCR_TRACK_EDIT, 0, "up", "カーソルを上へ", "Cursor up" },
	{ STED_SCR_TRACK_EDIT, 0, "down", "カーソルを下へ", "Cursor down" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,N | opt,rup", "1小節先へ", "Next measure" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,B | opt,rdown", "1小節前へ", "Previous measure" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,C | rup", "1ページ先へ", "Page forward" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,R | rdown", "1ページ前へ", "Page back" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,Z", "1行ロールアップ", "Scroll up one line" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,W", "1行ロールダウン", "Scroll down one line" },
	{ STED_SCR_TRACK_EDIT, 0, "shift,rup | opt,down", "高速で上へスクロール", "Fast scroll up" },
	{ STED_SCR_TRACK_EDIT, 0, "shift,rdown | opt,up", "高速で下へスクロール", "Fast scroll down" },
	{ STED_SCR_TRACK_EDIT, 0, "home", "カーソルを先頭へ", "Cursor home" },
	{ STED_SCR_TRACK_EDIT, 0, "shift,home", "演奏位置へジャンプ", "Jump to the play position" },
	{ STED_SCR_TRACK_EDIT, 0, "kana", "演奏位置を追う。もう一度か Esc で解除", "Follow the play position. Press again or Esc to stop" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,T", "トラック番号を指定して移動", "Go to a track number" },
	{ STED_SCR_TRACK_EDIT, 0, "opt,right | ctrl,right", "次のトラックへ", "Next track" },
	{ STED_SCR_TRACK_EDIT, 0, "opt,left | ctrl,left", "前のトラックへ", "Previous track" },
	{ STED_SCR_TRACK_EDIT, 0, "tab", "小節番号を指定して移動", "Go to a measure number" },
	{ STED_SCR_TRACK_EDIT, 0, "shift,tab", "タグジャンプ", "Tag jump" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,tab", "タグをセット", "Set a tag" },

	{ STED_SCR_TRACK_EDIT, 1, NULL, "F1–F10", "F1–F10" },
	{ STED_SCR_TRACK_EDIT, 1, "f1", "先頭行へ", "First line" },
	{ STED_SCR_TRACK_EDIT, 1, "f2", "最終行へ", "Last line" },
	{ STED_SCR_TRACK_EDIT, 1, "f3", "連続置換", "Replace" },
	{ STED_SCR_TRACK_EDIT, 1, "f4", "検索内容を設定", "Set the find text" },
	{ STED_SCR_TRACK_EDIT, 1, "f5", "前方検索", "Find next" },
	{ STED_SCR_TRACK_EDIT, 1, "f6", "範囲の開始行を設定", "Set the range start" },
	{ STED_SCR_TRACK_EDIT, 1, "f7", "削除（コピーバッファへ移動）", "Cut to the copy buffer" },
	{ STED_SCR_TRACK_EDIT, 1, "f8", "複写", "Copy" },
	{ STED_SCR_TRACK_EDIT, 1, "f9", "貼付", "Paste" },
	{ STED_SCR_TRACK_EDIT, 1, "f10", "行を二重化する", "Duplicate the line" },

	{ STED_SCR_TRACK_EDIT, 2, NULL, "Shift+F1–F10", "Shift+F1–F10" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f1", "ベロシティ連続設定", "Set velocity across a range" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f2", "最適化", "Optimize" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f3", "連続削除", "Delete across a range" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f4", "NOTE 並べ替え・STEP シフト", "Sort notes and shift steps" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f5", "後方検索", "Find previous" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f6", "UNDO を登録", "Store undo" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f7", "UNDO を実行", "Undo" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f8", "MIX 貼付", "Mix paste" },
	{ STED_SCR_TRACK_EDIT, 2, "shift,f9", "反転貼付", "Reverse paste" },

	{ STED_SCR_TRACK_EDIT, 0, NULL, "編集", "Edit" },
	{ STED_SCR_TRACK_EDIT, 0, "S", "パートデータをセーブ", "Save part data" },
	{ STED_SCR_TRACK_EDIT, 0, "L", "パートデータをロード", "Load part data" },
	{ STED_SCR_TRACK_EDIT, 0, "O", "パートデータをテキストで書き出す", "Export the part as text" },
	{ STED_SCR_TRACK_EDIT, 0, "R", "コピー。Shift では全トラック", "Replicate. Shift does every track" },
	{ STED_SCR_TRACK_EDIT, 0, "Q", "クォンタイズ", "Quantize" },
	{ STED_SCR_TRACK_EDIT, 0, "backslash", "ピッチベンド / コントロールチェンジを分割。Shift ではノート分割", "Split pitch bend or control change. Shift splits notes" },
	{ STED_SCR_TRACK_EDIT, 0, "slash", "スペシャルコントローラを書き込む。Shift ではユーザーエクスクルーシブ", "Insert a special controller. Shift opens user exclusive" },
	{ STED_SCR_TRACK_EDIT, 0, "under | shift,under", "トラックエクスクルーシブ", "Track exclusive" },
	{ STED_SCR_TRACK_EDIT, 0, "shift,lbracket", "パートアサイン", "Part assign" },
	{ STED_SCR_TRACK_EDIT, 0, "shift,rbracket", "ワンタッチ ST/GT の値を変更", "Edit one-touch ST/GT values" },
	{ STED_SCR_TRACK_EDIT, 0, "opt,A-Z", "オプションを直接実行", "Run an option command directly" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,rbracket", "対になるリピートを検索", "Find the matching repeat" },
	{ STED_SCR_TRACK_EDIT, 0, "lbracket", "リピート開始を挿入", "Insert repeat start" },
	{ STED_SCR_TRACK_EDIT, 0, "rbracket", "リピート終了を挿入", "Insert repeat end" },
	{ STED_SCR_TRACK_EDIT, 0, "star", "小節線を引く", "Insert a bar line" },
	{ STED_SCR_TRACK_EDIT, 0, "hat", "小節複写を設定。Shift では自動変換、Control では展開", "Same-measure copy. Shift converts, Control expands" },
	{ STED_SCR_TRACK_EDIT, 0, "plus | greater", "数値を +1。Shift+> は +10", "Add 1. Shift+> adds 10" },
	{ STED_SCR_TRACK_EDIT, 0, "minus | less", "数値を -1。Shift+< は -10", "Subtract 1. Shift+< subtracts 10" },
	{ STED_SCR_TRACK_EDIT, 0, "eq", "数値の符号を反転", "Flip the sign" },
	{ STED_SCR_TRACK_EDIT, 0, "0-9", "数値を編集", "Type a number" },
	{ STED_SCR_TRACK_EDIT, 0, "A-G", "ノートを音名で編集", "Type a note name" },
	{ STED_SCR_TRACK_EDIT, 0, "ret", "ノートを挿入", "Insert a note" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,Y | del", "行を削除バッファへ", "Delete the line into the line buffer" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,L | undo", "削除した行を挿入", "Insert the deleted line" },
	{ STED_SCR_TRACK_EDIT, 0, "kigou", "リズムパターンを編集", "Edit the rhythm pattern" },
	{ STED_SCR_TRACK_EDIT, 0, "touroku", "新しい小節でリズムパターンを作る。Shift では ST を変更", "New rhythm pattern. Shift also sets the step" },
	{ STED_SCR_TRACK_EDIT, 0, "at | shift,at", "コメントを書き込む", "Write a comment" },
	{ STED_SCR_TRACK_EDIT, 0, "K", "小節のキースケール", "Music key for the measure" },
	{ STED_SCR_TRACK_EDIT, 0, "X", "ワンタッチコード入力", "One-touch chord" },
	{ STED_SCR_TRACK_EDIT, 0, "V", "ビジュアルエディット。Shift はワイド、Control はセームメジャー", "Visual edit. Shift is wide, Control is same-measure" },
	{ STED_SCR_TRACK_EDIT, 0, "W", "ダブルトラック。Shift は同じ位置で開く", "Double-track edit. Shift opens at the same position" },
	{ STED_SCR_TRACK_EDIT, 0, "colon", "エディットモードを切り替える", "Switch the edit mode" },
	{ STED_SCR_TRACK_EDIT, 0, "I", "ポイントインフォメーション", "Point information" },
	{ STED_SCR_TRACK_EDIT, 0, "ctrl,G", "この小節からグラフィック表示", "Graphic from this measure" },
	{ STED_SCR_TRACK_EDIT, 0, "clr", "他トラックのグラフィックを消す", "Clear graphics on other tracks" },
	{ STED_SCR_TRACK_EDIT, 0, "help", "コマンドヘルプ", "Command help" },
	{ STED_SCR_TRACK_EDIT, 0, "esc", "トラックセレクタへ戻る。範囲設定中はその解除", "Back to the track selector, or clear the range" },
	{ STED_SCR_TRACK_EDIT, 0, "xf1 | M", "メジャープレイ。Shift はトラック、Control は小節指定", "Measure play. Shift is this track, Control asks for a measure" },
	{ STED_SCR_TRACK_EDIT, 0, "N", "ノートプレイ", "Note play" },
	{ STED_SCR_TRACK_EDIT, 0, "xf2 | P", "全トラックのポイントプレイ。Shift は小節指定", "Point play, all tracks. Shift asks for a measure" },
	{ STED_SCR_TRACK_EDIT, 0, "xf3", "演奏再開", "Continue" },
	{ STED_SCR_TRACK_EDIT, 0, "xf4", "演奏一時停止", "Pause" },
	{ STED_SCR_TRACK_EDIT, 0, "xf5", "演奏停止", "Stop" },

	{ STED_SCR_TRACK_EDIT, 3, NULL, "ワンタッチ（Shift+Option）", "One-touch (Shift+Option)" },

	{ STED_SCR_RHYTHM_EDIT, 0, NULL, "移動", "Move" },
	{ STED_SCR_RHYTHM_EDIT, 0, "left", "カーソルを左へ", "Cursor left" },
	{ STED_SCR_RHYTHM_EDIT, 0, "right", "カーソルを右へ", "Cursor right" },
	{ STED_SCR_RHYTHM_EDIT, 0, "up", "カーソルを上へ", "Cursor up" },
	{ STED_SCR_RHYTHM_EDIT, 0, "down", "カーソルを下へ", "Cursor down" },
	{ STED_SCR_RHYTHM_EDIT, 0, "home", "カーソルを先頭へ", "Cursor home" },
	{ STED_SCR_RHYTHM_EDIT, 0, "shift,home", "演奏位置へジャンプ", "Jump to the play position" },
	{ STED_SCR_RHYTHM_EDIT, 0, "kana", "演奏位置を追う", "Follow the play position" },
	{ STED_SCR_RHYTHM_EDIT, 0, "tab", "小節番号を指定して移動", "Go to a measure number" },
	{ STED_SCR_RHYTHM_EDIT, 0, "ctrl,C | rup", "次の小節のパターン", "Next measure's pattern" },
	{ STED_SCR_RHYTHM_EDIT, 0, "ctrl,R | rdown", "前の小節のパターン", "Previous measure's pattern" },
	{ STED_SCR_RHYTHM_EDIT, 0, "shift,rup | shift,rdown", "パターンの残り半分を表示", "Show the other half of the pattern" },
	{ STED_SCR_RHYTHM_EDIT, 0, "touroku", "新しい小節でパターンを作る。Shift では ST を変更", "New pattern. Shift also sets the step" },
	{ STED_SCR_RHYTHM_EDIT, 0, "esc", "トラックエディタへ戻る", "Back to the track editor" },
	{ STED_SCR_RHYTHM_EDIT, 0, "plus | greater", "数値を +1。Shift+> は +10", "Add 1. Shift+> adds 10" },
	{ STED_SCR_RHYTHM_EDIT, 0, "minus | less", "数値を -1。Shift+< は -10", "Subtract 1. Shift+< subtracts 10" },
	{ STED_SCR_RHYTHM_EDIT, 0, "ctrl,Y | del", "行を削除バッファへ", "Delete the line into the line buffer" },
	{ STED_SCR_RHYTHM_EDIT, 0, "ctrl,L | undo", "削除した行を挿入", "Insert the deleted line" },
	{ STED_SCR_RHYTHM_EDIT, 0, "f1", "先頭小節のパターン", "First measure" },
	{ STED_SCR_RHYTHM_EDIT, 0, "f2", "最終小節のパターン", "Last measure" },
	{ STED_SCR_RHYTHM_EDIT, 0, "f7", "パターンを削除", "Cut the pattern" },
	{ STED_SCR_RHYTHM_EDIT, 0, "f8", "複写", "Copy" },
	{ STED_SCR_RHYTHM_EDIT, 0, "f9", "貼付", "Paste" },
	{ STED_SCR_RHYTHM_EDIT, 0, "f10", "行を二重化する", "Duplicate the line" },
	{ STED_SCR_RHYTHM_EDIT, 0, "shift,slash", "リズムアサイン", "Rhythm assign" },
	{ STED_SCR_RHYTHM_EDIT, 0, "space", "音の強さを順に変える", "Cycle the velocity level" },
	{ STED_SCR_RHYTHM_EDIT, 0, "0-9", "音の強さを指定", "Set the velocity level" },
	{ STED_SCR_RHYTHM_EDIT, 0, "ret", "ベロシティまたはステップを直接指定", "Type velocity or step directly" },
	{ STED_SCR_RHYTHM_EDIT, 0, "xf1 | M", "メジャープレイ。Shift はトラック、Control は小節指定", "Measure play. Shift is this track, Control asks for a measure" },
	{ STED_SCR_RHYTHM_EDIT, 0, "xf2 | P", "全トラックのポイントプレイ。Shift は小節指定", "Point play, all tracks. Shift asks for a measure" },
	{ STED_SCR_RHYTHM_EDIT, 0, "xf3", "演奏再開", "Continue" },
	{ STED_SCR_RHYTHM_EDIT, 0, "xf4", "演奏一時停止", "Pause" },
	{ STED_SCR_RHYTHM_EDIT, 0, "xf5", "演奏停止", "Stop" },

	{ STED_SCR_RHYTHM_AS, 0, "up | down | left | right", "カーソルを移動する", "Move the cursor" },
	{ STED_SCR_RHYTHM_AS, 0, "home", "左上へ戻る", "Jump to the first cell" },
	{ STED_SCR_RHYTHM_AS, 0, "tab", "番号を指定して移動する", "Go to a number" },
	{ STED_SCR_RHYTHM_AS, 0, "ret", "名前、キー番号、GT を変更する。キー番号の入力中に ↓ で音色セレクタ", "Edit the name, key, or GT. Down in the key field opens the tone list" },
	{ STED_SCR_RHYTHM_AS, 0, "0-9", "キー番号か GT を直接入力する", "Type a key number or GT" },
	{ STED_SCR_RHYTHM_AS, 0, "plus | greater", "+1。Shift では +12", "Add 1. Shift adds 12" },
	{ STED_SCR_RHYTHM_AS, 0, "minus | less", "-1。Shift では -12", "Subtract 1. Shift subtracts 12" },
	{ STED_SCR_RHYTHM_AS, 0, "space", "その1音を初期化する", "Reset this one assignment" },
	{ STED_SCR_RHYTHM_AS, 0, "clr", "32音すべてを初期化する", "Reset all 32 assignments" },
	{ STED_SCR_RHYTHM_AS, 0, "A", "音色名を一括更新する", "Refresh every rhythm name" },
	{ STED_SCR_RHYTHM_AS, 0, "X", "2つのアサインを入れ換える", "Exchange two assignments" },
	{ STED_SCR_RHYTHM_AS, 0, "C", "サウンドテストのチャンネル", "Sound-test channel" },
	{ STED_SCR_RHYTHM_AS, 0, "B", "サウンドテストの Bank。-1 で送らない", "Sound-test bank. -1 sends none" },
	{ STED_SCR_RHYTHM_AS, 0, "G", "プログラム番号。入力中に ↓ で音色セレクタ", "Program number. Down opens the tone list" },
	{ STED_SCR_RHYTHM_AS, 0, "V", "サウンドテストのベロシティ", "Sound-test velocity" },
	{ STED_SCR_RHYTHM_AS, 0, "undo", "今のキーを鳴らす", "Play the current key" },
	{ STED_SCR_RHYTHM_AS, 0, "R", "ROCK DRUMS で鳴らす", "Play with Rock Drums" },
	{ STED_SCR_RHYTHM_AS, 0, "E", "ELEC DRUMS で鳴らす", "Play with Elec Drums" },
	{ STED_SCR_RHYTHM_AS, 0, "L", "アサインファイルをロードする", "Load a .RAS file" },
	{ STED_SCR_RHYTHM_AS, 0, "S", "アサインファイルをセーブする", "Save a .RAS file" },
	{ STED_SCR_RHYTHM_AS, 0, "esc", "抜ける。キー番号の重複は確認が出る", "Leave. A duplicate key asks for confirmation" },

	{ STED_SCR_PART, 0, "up | down | left | right", "チャンネルを移動する", "Move between channels" },
	{ STED_SCR_PART, 0, "tab", "番号を指定して移動する", "Go to a channel number" },
	{ STED_SCR_PART, 0, "opt,0-9", "その番号のチャンネルへ移動する", "Jump to that channel" },
	{ STED_SCR_PART, 0, "ret", "パート一覧を開く", "Open the part list" },
	{ STED_SCR_PART, 0, "0-9 | A-F", "アサイン番号を直接入れる", "Type an assign number" },
	{ STED_SCR_PART, 0, "clr", "アサインを初期化する", "Reset the assigns" },
	{ STED_SCR_PART, 0, "esc", "抜ける", "Leave" },
	{ STED_SCR_PART, 0, NULL, "パート選択", "Part list" },
	{ STED_SCR_PART, 0, "up | down", "一覧を移動する", "Move in the list" },
	{ STED_SCR_PART, 0, "tab", "番号を指定する", "Go to a number" },
	{ STED_SCR_PART, 0, "ret", "決定する", "Choose" },
	{ STED_SCR_PART, 0, "esc", "キャンセルする", "Cancel" },

	{ STED_SCR_FILTER, 0, "up | down | left | right", "項目を移動する", "Move between items" },
	{ STED_SCR_FILTER, 0, "ret", "フィルターの ON/OFF、または変換先を入力する", "Toggle a filter, or edit a conversion" },
	{ STED_SCR_FILTER, 0, "clr", "初期化する", "Reset the filters" },
	{ STED_SCR_FILTER, 0, "esc", "抜ける", "Leave" },

	{ STED_SCR_STGT, 0, "up | down | left | right", "F1〜F10 の ST / GT / VEL を移動する", "Move through the F1–F10 values" },
	{ STED_SCR_STGT, 0, "ret | 0-9", "数値を入力する", "Type a value" },
	{ STED_SCR_STGT, 0, "plus | greater", "+1。Shift では +12", "Add 1. Shift adds 12" },
	{ STED_SCR_STGT, 0, "minus | less", "-1。Shift では -12", "Subtract 1. Shift subtracts 12" },
	{ STED_SCR_STGT, 0, "space", "その1つを初期化する", "Reset this entry" },
	{ STED_SCR_STGT, 0, "clr", "すべて初期化する", "Reset all entries" },
	{ STED_SCR_STGT, 0, "home | esc", "抜ける", "Leave" },

	{ STED_SCR_MEMO, 0, "up | down", "行を移動する", "Move between lines" },
	{ STED_SCR_MEMO, 0, "ret", "その行を編集する", "Edit the line" },
	{ STED_SCR_MEMO, 0, "del", "その行を消して下を詰める", "Delete the line" },
	{ STED_SCR_MEMO, 0, "undo", "消した行を戻す", "Put the deleted line back" },
	{ STED_SCR_MEMO, 0, "clr", "メモをすべて消す", "Clear the memo" },
	{ STED_SCR_MEMO, 0, "esc", "抜ける", "Leave" },

	{ STED_SCR_TIMEKEY, 0, "up | down | left | right", "テンポ、バイアス、キー、拍子、タイムベースを移動する", "Move among tempo, bias, key, beat, and timebase" },
	{ STED_SCR_TIMEKEY, 0, "home", "各列の先頭へ戻る", "Back to the first item in the column" },
	{ STED_SCR_TIMEKEY, 0, "ret | 0-9", "数値を入力する。キーでは Return でスケール選択が開く", "Type a value. On Key, Return opens the scale list" },
	{ STED_SCR_TIMEKEY, 0, "plus | greater", "+1。テンポの Shift は +10、バイアスは +12。タイムベースは次の段階", "Add 1. Shift adds 10 to tempo or 12 to bias. Timebase steps up" },
	{ STED_SCR_TIMEKEY, 0, "minus | less", "-1。テンポの Shift は -10、バイアスは -12。タイムベースは前の段階", "Subtract 1. Shift subtracts 10 from tempo or 12 from bias" },
	{ STED_SCR_TIMEKEY, 0, "space", "その項目を初期値に戻す", "Reset the current item" },
	{ STED_SCR_TIMEKEY, 0, "esc", "抜ける", "Leave" },

	{ STED_SCR_TRACK_SET, 0, "up | down", "トラックを移動する", "Move between tracks" },
	{ STED_SCR_TRACK_SET, 0, "left | right", "モード、CH、ST、KEY、RHY、メモの列を移動する", "Move across mode, channel, ST, key, rhythm, and memo" },
	{ STED_SCR_TRACK_SET, 0, "home", "表示中の先頭トラックへ", "First track on this page" },
	{ STED_SCR_TRACK_SET, 0, "rup | rdown", "9トラックずつページを送る。Shift では1つ", "Page by 9 tracks. Shift moves by one" },
	{ STED_SCR_TRACK_SET, 0, "tab", "トラック番号を指定する", "Go to a track number" },
	{ STED_SCR_TRACK_SET, 0, "opt,0-9", "その番号のトラックへ移動する", "Jump to that track" },
	{ STED_SCR_TRACK_SET, 0, "ret", "トラック番号、または Shift ではエディタを開く。他の列では値を編集する", "On the track number, or with Shift, open the editor. Other columns edit the value" },
	{ STED_SCR_TRACK_SET, 0, "plus | minus", "その列の値を変える。Shift では ST と KEY が 12 ずつ", "Change the column. Shift steps ST and key by 12" },
	{ STED_SCR_TRACK_SET, 0, "space", "その列を初期化する。Shift では全トラック", "Reset the column. Shift resets every track" },
	{ STED_SCR_TRACK_SET, 0, "eq", "符号や A/B、リズムの ON を反転する", "Flip the sign, the port, or rhythm on/off" },
	{ STED_SCR_TRACK_SET, 0, "P | M | X | R", "モード列では PLAY / MIDI / EXCL / REC にする", "On the mode column: Play, MIDI, Exclusive, or Record" },
	{ STED_SCR_TRACK_SET, 0, "X", "モード列以外ではトラックを入れ換える", "Off the mode column, exchange tracks" },
	{ STED_SCR_TRACK_SET, 0, "C", "トラックをコピーする", "Copy a track" },
	{ STED_SCR_TRACK_SET, 0, "I", "トラックを挿入する", "Insert a track" },
	{ STED_SCR_TRACK_SET, 0, "T", "トラックを並べ替える", "Sort tracks" },
	{ STED_SCR_TRACK_SET, 0, "M", "モード列以外ではトラックをミックスする", "Off the mode column, mix tracks" },
	{ STED_SCR_TRACK_SET, 0, "D | clr", "トラックのデータを消す", "Erase the track data" },
	{ STED_SCR_TRACK_SET, 0, "O", "最適化する。Shift では全トラック", "Optimize. Shift does every track" },
	{ STED_SCR_TRACK_SET, 0, "A", "小節複写を圧縮する。Shift では全トラック", "Compress same-measure copies. Shift does every track" },
	{ STED_SCR_TRACK_SET, 0, "R", "モード列以外ではリピートを圧縮する。Shift では全トラック", "Off the mode column, compress repeats. Shift does every track" },
	{ STED_SCR_TRACK_SET, 0, "E", "小節複写とリピートを展開する。Shift では全トラック", "Expand copies and repeats. Shift does every track" },
	{ STED_SCR_TRACK_SET, 0, "B", "小節線を引き直す。Shift では全トラック", "Rewrite bar lines. Shift does every track" },
	{ STED_SCR_TRACK_SET, 0, "L", "トラックをロードする。Shift では曲全体", "Load a track. Shift loads the song" },
	{ STED_SCR_TRACK_SET, 0, "S", "トラックをセーブする。Shift では曲全体", "Save a track. Shift saves the song" },
	{ STED_SCR_TRACK_SET, 0, "xf1", "メジャープレイ。Shift ではトラック単位のポイントプレイ", "Measure play. Shift plays from the cursor on this track" },
	{ STED_SCR_TRACK_SET, 0, "xf2", "全トラックのポイントプレイ。Shift では小節指定", "Point play, all tracks. Shift asks for a measure" },
	{ STED_SCR_TRACK_SET, 0, "opt,A-Z", "オプションを直接実行する", "Run an option command" },
	{ STED_SCR_TRACK_SET, 0, "esc", "コマンドメニューへ戻る", "Back to the command menu" },

	{ STED_SCR_TRACK_SEL, 0, "up | down", "トラックを移動する", "Move between tracks" },
	{ STED_SCR_TRACK_SEL, 0, "home", "表示中の先頭へ", "First track on this page" },
	{ STED_SCR_TRACK_SEL, 0, "rup | rdown", "9トラックずつページを送る。Shift では1つ", "Page by 9 tracks. Shift moves by one" },
	{ STED_SCR_TRACK_SEL, 0, "tab", "トラック番号を指定する", "Go to a track number" },
	{ STED_SCR_TRACK_SEL, 0, "opt,0-9", "その番号へ移動する", "Jump to that track" },
	{ STED_SCR_TRACK_SEL, 0, "ret", "決定する", "Choose" },
	{ STED_SCR_TRACK_SEL, 0, "esc", "キャンセルする", "Cancel" },

	{ STED_SCR_OPTION, 0, "up | down", "項目を移動する", "Move through the list" },
	{ STED_SCR_OPTION, 0, "A-Z", "その文字の項目を実行する", "Run the item for that letter" },
	{ STED_SCR_OPTION, 0, "ret | space", "選んでいる項目を実行する", "Run the selected item" },
	{ STED_SCR_OPTION, 0, "esc", "抜ける", "Leave" },
	{ STED_SCR_OPTION, 0, "", "項目は sted2.cnf の後半です。空の項目は実行されません。", "These are the later commands in sted2.cnf. Empty ones do nothing." },

	{ STED_SCR_USER_EXC, 0, "up | down | left | right", "メモとデータの間、バイト間を移動する", "Move between the memo and the bytes" },
	{ STED_SCR_USER_EXC, 0, "opt", "テンキーと一緒に押すと、1〜7 番へ移動する", "With the numeric keypad, jump to entries 1–7" },
	{ STED_SCR_USER_EXC, 0, "ret", "メモ、またはバイトを入力する。Shift では 00 を挿入する", "Edit the memo or a byte. Shift inserts 00" },
	{ STED_SCR_USER_EXC, 0, "0-9 | A-F", "16進数で入力する", "Type a hex byte" },
	{ STED_SCR_USER_EXC, 0, "plus | greater", "+1。> は +16", "Add 1. > adds 16" },
	{ STED_SCR_USER_EXC, 0, "minus | less", "-1。< は -16", "Subtract 1. < subtracts 16" },
	{ STED_SCR_USER_EXC, 0, "del", "そのバイトを消す", "Delete the byte" },
	{ STED_SCR_USER_EXC, 0, "bs", "左のバイトを消す", "Delete the byte to the left" },
	{ STED_SCR_USER_EXC, 0, "tab", "そのバイトを二重化する", "Duplicate the byte" },
	{ STED_SCR_USER_EXC, 0, "f1", "GT を送る印を入れる", "Insert send-gate" },
	{ STED_SCR_USER_EXC, 0, "f2", "VEL を送る印を入れる", "Insert send-velocity" },
	{ STED_SCR_USER_EXC, 0, "f3", "チャンネルを送る印を入れる", "Insert send-channel" },
	{ STED_SCR_USER_EXC, 0, "f4", "チェックサムをクリアする印を入れる", "Insert clear-checksum" },
	{ STED_SCR_USER_EXC, 0, "f5", "チェックサムを送る印を入れる", "Insert send-checksum" },
	{ STED_SCR_USER_EXC, 0, "f6", "F7（終端）を入れる", "Insert the end marker" },
	{ STED_SCR_USER_EXC, 0, "f8", "チェックサムを計算して書き込む", "Write a Roland checksum" },
	{ STED_SCR_USER_EXC, 0, "f9", "読み込む。Shift ではその1つだけ", "Load. Shift loads only this one" },
	{ STED_SCR_USER_EXC, 0, "f10", "保存する。Shift ではその1つだけ", "Save. Shift saves only this one" },
	{ STED_SCR_USER_EXC, 0, "U", "別の番号からコピーする", "Copy another user exclusive" },
	{ STED_SCR_USER_EXC, 0, "space", "その1つを初期化する", "Reset this one" },
	{ STED_SCR_USER_EXC, 0, "clr", "8つすべてを初期化する", "Reset all eight" },
	{ STED_SCR_USER_EXC, 0, "esc", "抜ける", "Leave" },

	{ STED_SCR_TRK_EXC, 0, "up | down | left | right", "バイトを移動する", "Move between bytes" },
	{ STED_SCR_TRK_EXC, 0, "home", "先頭へ戻る", "Jump to the first byte" },
	{ STED_SCR_TRK_EXC, 0, "ret", "バイトを挿入する。Shift では 00 を挿入する", "Insert a byte. Shift inserts 00" },
	{ STED_SCR_TRK_EXC, 0, "0-9 | A-F", "16進数で入力する", "Type a hex byte" },
	{ STED_SCR_TRK_EXC, 0, "plus | greater", "+1。> は +10", "Add 1. > adds 10" },
	{ STED_SCR_TRK_EXC, 0, "minus | less", "-1。< は -10", "Subtract 1. < subtracts 10" },
	{ STED_SCR_TRK_EXC, 0, "del", "そのバイトを消す", "Delete the byte" },
	{ STED_SCR_TRK_EXC, 0, "bs", "左のバイトを消す", "Delete the byte to the left" },
	{ STED_SCR_TRK_EXC, 0, "tab", "そのバイトを二重化する", "Duplicate the byte" },
	{ STED_SCR_TRK_EXC, 0, "f1 | f2 | f3 | f4 | f5", "GT、VEL、チャンネル、チェックサム消去、チェックサム送信を入れる", "Insert gate, velocity, channel, checksum-clear, or checksum-send" },
	{ STED_SCR_TRK_EXC, 0, "f6", "F7（終端）を入れる", "Insert the end marker" },
	{ STED_SCR_TRK_EXC, 0, "f8", "チェックサムを計算して書き込む", "Write a Roland checksum" },
	{ STED_SCR_TRK_EXC, 0, "f9", "テキストのエクスクルーシブを読み込む", "Load an exclusive text file" },
	{ STED_SCR_TRK_EXC, 0, "f10", "テキストで保存する", "Save as text" },
	{ STED_SCR_TRK_EXC, 0, "dollar", "文字列をバイト列として挿入する", "Insert a text string as bytes" },
	{ STED_SCR_TRK_EXC, 0, "U", "ユーザーエクスクルーシブをコピーする", "Copy a user exclusive" },
	{ STED_SCR_TRK_EXC, 0, "hat", "ドットエディタを開く", "Open the dot editor" },
	{ STED_SCR_TRK_EXC, 0, "at", "GS の音色データならパネルを表示する", "Show the panel when the data is a GS tone" },
	{ STED_SCR_TRK_EXC, 0, "undo", "データがあるとき、編集を終える", "Finish editing when there is data" },
	{ STED_SCR_TRK_EXC, 0, "esc", "中止して戻る", "Cancel" },

	{ STED_SCR_DOT, 0, "up | down | left | right", "ドットを移動する", "Move between dots" },
	{ STED_SCR_DOT, 0, "space", "ドットを反転する", "Toggle the dot" },
	{ STED_SCR_DOT, 0, "opt", "カーソルのドットを塗る", "Paint the dot under the cursor" },
	{ STED_SCR_DOT, 0, "caps", "カーソルのドットを消す", "Erase the dot under the cursor" },
	{ STED_SCR_DOT, 0, "f1", "上下を反転する", "Flip vertically" },
	{ STED_SCR_DOT, 0, "f2", "左右を反転する", "Flip horizontally" },
	{ STED_SCR_DOT, 0, "f3", "回転する", "Rotate" },
	{ STED_SCR_DOT, 0, "G", "データからパターンを読み込む", "Load the pattern from the data" },
	{ STED_SCR_DOT, 0, "R", "白黒を反転する", "Invert the pattern" },
	{ STED_SCR_DOT, 0, "clr", "すべて消す", "Clear the pattern" },
	{ STED_SCR_DOT, 0, "ctrl,left | ctrl,right | ctrl,up | ctrl,down", "パターンをずらす", "Shift the pattern" },
	{ STED_SCR_DOT, 0, "undo", "エクスクルーシブとして送る", "Send it as an exclusive" },
	{ STED_SCR_DOT, 0, "esc | hat", "パターンを書き戻して閉じる", "Write the pattern back and close" },

	{ STED_SCR_CM64, 0, "f1", "システムモード", "System mode" },
	{ STED_SCR_CM64, 0, "f2", "LA パッチ", "LA patch" },
	{ STED_SCR_CM64, 0, "f3", "PCM パッチ", "PCM patch" },
	{ STED_SCR_CM64, 0, "f4", "リズムパッチ", "Rhythm patch" },
	{ STED_SCR_CM64, 0, "f5", "ティンバー編集を起動する。Shift ではユーザートーンリスト。TE.x が無いと失敗する", "Start the timbre editor. Shift opens the user tone list. This fails if TE.x is missing" },
	{ STED_SCR_CM64, 0, "f6", "コントロールファイルをロードする", "Load a control file" },
	{ STED_SCR_CM64, 0, "f7", "コントロールファイルをセーブする", "Save a control file" },
	{ STED_SCR_CM64, 0, "f8", "演奏する", "Play" },
	{ STED_SCR_CM64, 0, "f9", "コントロールデータを初期化する", "Initialize the control data" },
	{ STED_SCR_CM64, 0, "f10 | esc", "システム画面ではコマンドメニューへ戻る。パッチ画面の Esc はシステムへ戻る", "On the system page, back to the command menu. Esc on a patch page returns to the system page" },
	{ STED_SCR_CM64, 0, NULL, "パッチ画面", "Patch page" },
	{ STED_SCR_CM64, 0, "ret", "音色を選ぶ。リズムでは数値も編集する", "Pick a tone. On rhythm, this also edits the numbers" },
	{ STED_SCR_CM64, 0, "tab", "LA と PCM ではパラメータを編集する", "On LA and PCM, edit the parameters" },
	{ STED_SCR_CM64, 0, "space", "パッチを初期化する", "Reset the patch" },
	{ STED_SCR_CM64, 0, "C", "LA と PCM ではカードの音色リストを開く", "On LA and PCM, open the card tone list" },
	{ STED_SCR_CM64, 0, "K | V | undo", "試聴のキー、ベロシティ、発音", "Audition key, velocity, and playback" },
	{ STED_SCR_CM64, 0, NULL, "ユーザートーンリスト", "User tone list" },
	{ STED_SCR_CM64, 0, "up | down | left | right", "音色を移動する", "Move through the tones" },
	{ STED_SCR_CM64, 0, "space", "入れ換えの印を付ける", "Mark a tone to exchange" },
	{ STED_SCR_CM64, 0, "X", "印を付けた音色と入れ換える", "Exchange with the marked tone" },
	{ STED_SCR_CM64, 0, "ret", "名前を変える", "Rename" },
	{ STED_SCR_CM64, 0, "clr", "その音色を消す", "Clear that tone" },
	{ STED_SCR_CM64, 0, "esc", "システム画面へ戻る", "Back to the system page" },

	{ STED_SCR_GS, 0, "", "外部の GS エディタ（GE.x）を起動します。この画面自体のキーはありません。", "This starts the external GS editor (GE.x). The screen has no keys of its own." },

	{ STED_SCR_VISUAL, 0, "left | right", "点を移動する。Shift か Control では大きく跳ぶ", "Move a point. Shift or Control jumps" },
	{ STED_SCR_VISUAL, 0, "up | down", "値を上下する。Shift か Control では大きく動く", "Raise or lower the value. Shift or Control moves faster" },
	{ STED_SCR_VISUAL, 0, "space", "範囲の端を付ける、または外す", "Set or clear the range end" },
	{ STED_SCR_VISUAL, 0, "0-9", "値を段階で入れる（ピッチ以外）", "Set a level, except for pitch bend" },
	{ STED_SCR_VISUAL, 0, "Y", "縦の倍率を変える", "Change the vertical zoom" },
	{ STED_SCR_VISUAL, 0, "f1 | f2", "先頭、または末尾へ移動する", "Go to the first or last point" },
	{ STED_SCR_VISUAL, 0, "F3-F10", "直線や波形を描く。Shift では反転や乱数", "Draw a line or a wave. Shift uses the inverted and random shapes" },
	{ STED_SCR_VISUAL, 0, "X", "ステップを分割する", "Split steps" },
	{ STED_SCR_VISUAL, 0, "Z", "ステップの長さを変える", "Change the step size" },
	{ STED_SCR_VISUAL, 0, "I", "点を挿入する。Shift ではその点の長さを変える", "Insert a point. Shift changes its length" },
	{ STED_SCR_VISUAL, 0, "D", "点を消す。Shift では指定した長さを削る", "Delete a point. Shift deletes a length" },
	{ STED_SCR_VISUAL, 0, "C", "同じ値が続く点をまとめる", "Join points that share a value" },
	{ STED_SCR_VISUAL, 0, "tab", "点の番号へ移動する", "Jump to a point number" },
	{ STED_SCR_VISUAL, 0, "M | xf1 | undo", "今の形を鳴らす", "Play the current shape" },
	{ STED_SCR_VISUAL, 0, "clr", "その点を 0 にする。Control ではすべての点を初期値に戻す", "Zero this point. Control resets every point" },
	{ STED_SCR_VISUAL, 0, "ret", "トラックへ書き戻す", "Write it back to the track" },
	{ STED_SCR_VISUAL, 0, "esc", "書き戻さず閉じる", "Close without writing" },

	{ STED_SCR_TAG, 0, "up | down", "タグを移動する", "Move through the tags" },
	{ STED_SCR_TAG, 0, "A-V", "その文字のタグへ移動する", "Move to the tag for that letter" },
	{ STED_SCR_TAG, 0, "home", "先頭を選んで決定する", "Choose the first tag" },
	{ STED_SCR_TAG, 0, "ret", "ジャンプする。登録中はそのトラックも記録する", "Jump. While setting a tag, this also stores the track" },
	{ STED_SCR_TAG, 0, "space", "ジャンプする。登録中はトラックを記録しない", "Jump. While setting a tag, the track is left unset" },
	{ STED_SCR_TAG, 0, "esc", "キャンセルする", "Cancel" },

	{ STED_SCR_TONESEL, 0, "up | down", "音色を移動する。端では1行分スクロールする", "Move through tones. The ends scroll by one line" },
	{ STED_SCR_TONESEL, 0, "rup | rdown", "1ページ送る", "Page forward or back" },
	{ STED_SCR_TONESEL, 0, "home", "先頭へ戻る", "Back to the first tone" },
	{ STED_SCR_TONESEL, 0, "tab", "番号を指定して移動する", "Go to a tone number" },
	{ STED_SCR_TONESEL, 0, "left | right", "バンクを送る", "Step the bank" },
	{ STED_SCR_TONESEL, 0, "opt,left | opt,right", "バンクグループを送る", "Step the bank group" },
	{ STED_SCR_TONESEL, 0, "ctrl,left | ctrl,right", "チャンネルを送る", "Step the channel" },
	{ STED_SCR_TONESEL, 0, "clr", "開いたときのリストに戻す", "Restore the list from when it opened" },
	{ STED_SCR_TONESEL, 0, "slash", "バンクを一列にした一覧に入る", "List every bank in one column" },
	{ STED_SCR_TONESEL, 0, "undo", "その音色を鳴らす", "Play the tone" },
	{ STED_SCR_TONESEL, 0, "K | V", "試聴のキー、ベロシティを変える", "Change the audition key or velocity" },
	{ STED_SCR_TONESEL, 0, "Z", "音源へコントロールデータを送る", "Send control data to the module" },
	{ STED_SCR_TONESEL, 0, "ret", "決定する", "Choose" },
	{ STED_SCR_TONESEL, 0, "esc", "キャンセルする", "Cancel" },

	{ STED_SCR_CMDSEL, 0, NULL, "記号", "Symbols" },
	{ STED_SCR_CMDSEL, 0, "G | Gnn", "プログラムチェンジ。数字で番号", "Program change. A number sets the program" },
	{ STED_SCR_CMDSEL, 0, "G,at | GM", "バンクとプログラムチェンジ", "Bank and program change" },
	{ STED_SCR_CMDSEL, 0, "M", "MIDIチャンネル", "MIDI channel" },
	{ STED_SCR_CMDSEL, 0, "T", "テンポ", "Tempo" },
	{ STED_SCR_CMDSEL, 0, "P", "ピッチベンド", "Pitch bend" },
	{ STED_SCR_CMDSEL, 0, "C", "チャンネルアフタータッチ", "Channel aftertouch" },
	{ STED_SCR_CMDSEL, 0, "K", "ポリフォニックアフタータッチ", "Polyphonic aftertouch" },
	{ STED_SCR_CMDSEL, 0, "L | Lnn", "コントロールチェンジ。数字でコントロール番号", "Control change. A number sets the controller" },
	{ STED_SCR_CMDSEL, 0, "0-127", "コントロール番号を直接指定する", "A control number by itself" },
	{ STED_SCR_CMDSEL, 0, "D", "モジュレーション", "Modulation" },
	{ STED_SCR_CMDSEL, 0, "V", "ボリューム", "Volume" },
	{ STED_SCR_CMDSEL, 0, "N", "パンポット", "Pan" },
	{ STED_SCR_CMDSEL, 0, "E", "エクスプレッション", "Expression" },
	{ STED_SCR_CMDSEL, 0, "H", "ホールド1（ダンパー）", "Hold 1 (damper)" },
	{ STED_SCR_CMDSEL, 0, "A", "リセットオールコントローラ", "Reset all controllers" },
	{ STED_SCR_CMDSEL, 0, "at | at,M", "バンクセレクト MSB", "Bank select MSB" },
	{ STED_SCR_CMDSEL, 0, "at,at | at,L", "バンクセレクト LSB", "Bank select LSB" },
	{ STED_SCR_CMDSEL, 0, "F1", "リバーブ（エフェクト1）", "Reverb (effect 1)" },
	{ STED_SCR_CMDSEL, 0, "F2", "エフェクト2", "Effect 2" },
	{ STED_SCR_CMDSEL, 0, "F3", "コーラス（エフェクト3）", "Chorus (effect 3)" },
	{ STED_SCR_CMDSEL, 0, "F4", "ディレイ（エフェクト4）", "Delay (effect 4)" },
	{ STED_SCR_CMDSEL, 0, "F5", "エフェクト5", "Effect 5" },
	{ STED_SCR_CMDSEL, 0, "PT", "ポルタメントタイム", "Portamento time" },
	{ STED_SCR_CMDSEL, 0, "PO", "ポルタメント", "Portamento" },
	{ STED_SCR_CMDSEL, 0, "PC", "ポルタメントコントロール", "Portamento control" },
	{ STED_SCR_CMDSEL, 0, "SS", "ソステヌート", "Sostenuto" },
	{ STED_SCR_CMDSEL, 0, "ST", "ソフトペダル", "Soft pedal" },
	{ STED_SCR_CMDSEL, 0, "BR", "ブレスコントロール", "Breath" },
	{ STED_SCR_CMDSEL, 0, "RS", "レゾナンス", "Resonance" },
	{ STED_SCR_CMDSEL, 0, "RT", "リリースタイム", "Release time" },
	{ STED_SCR_CMDSEL, 0, "AT", "アタックタイム", "Attack time" },
	{ STED_SCR_CMDSEL, 0, "CO", "カットオフ", "Cutoff" },
	{ STED_SCR_CMDSEL, 0, "EM", "データエントリ MSB", "Data entry MSB" },
	{ STED_SCR_CMDSEL, 0, "EL", "データエントリ LSB", "Data entry LSB" },
	{ STED_SCR_CMDSEL, 0, "NM", "NRPN MSB", "NRPN MSB" },
	{ STED_SCR_CMDSEL, 0, "NL", "NRPN LSB", "NRPN LSB" },
	{ STED_SCR_CMDSEL, 0, "RM", "RPN MSB", "RPN MSB" },
	{ STED_SCR_CMDSEL, 0, "RL", "RPN LSB", "RPN LSB" },
	{ STED_SCR_CMDSEL, 0, "U0-U7", "ユーザーエクスクルーシブ 0〜7", "User exclusive 0–7" },
	{ STED_SCR_CMDSEL, 0, "X", "トラックエクスクルーシブ", "Track exclusive" },
	{ STED_SCR_CMDSEL, 0, "B", "ローランド・ベースアドレス", "Roland base address" },
	{ STED_SCR_CMDSEL, 0, "R", "ローランド・アドレスとパラメータ", "Roland address and parameter" },
	{ STED_SCR_CMDSEL, 0, "I", "ローランド・デバイス番号とモデルID", "Roland device number and model ID" },
	{ STED_SCR_CMDSEL, 0, "S4", "Roland MKS-7", "Roland MKS-7" },
	{ STED_SCR_CMDSEL, 0, "S5", "YAMAHA FB-01 パラメータ", "YAMAHA FB-01 parameter" },
	{ STED_SCR_CMDSEL, 0, "S6", "YAMAHA FB-01 システム", "YAMAHA FB-01 system" },
	{ STED_SCR_CMDSEL, 0, NULL, "一覧ウィンドウ", "List window" },
	{ STED_SCR_CMDSEL, 0, "", "入力中に ↓ で開きます。#SPC_SEL=ON のときは / ですぐ開きます。", "Press Down while typing to open it. With #SPC_SEL=ON, / opens it immediately." },
	{ STED_SCR_CMDSEL, 0, "up | down", "項目を移動する", "Move through the list" },
	{ STED_SCR_CMDSEL, 0, "A-Z", "その記号の行へ移動する", "Move to the controller with that letter" },
	{ STED_SCR_CMDSEL, 0, "ret", "決定する", "Choose" },
	{ STED_SCR_CMDSEL, 0, "esc", "キャンセルする", "Cancel" },

	{ STED_SCR_REPLSEL, 0, "up | down", "項目を移動する", "Move through the list" },
	{ STED_SCR_REPLSEL, 0, "left | right | rup | rdown", "次の26件、前の26件へ", "The next or previous 26 entries" },
	{ STED_SCR_REPLSEL, 0, "A-Z", "その文字を選んで実行する。Shift では文字列を入れるだけ", "Pick that letter and run it. Shift only fills in the text" },
	{ STED_SCR_REPLSEL, 0, "ret", "選んで実行する。Shift では文字列を入れるだけ", "Choose and run it. Shift only fills in the text" },
	{ STED_SCR_REPLSEL, 0, "undo", "キャンセルする", "Cancel" },
	{ STED_SCR_REPLSEL, 0, "esc", "閉じる", "Close" },

	{ STED_SCR_CHORD, 0, "left | right", "コードの種類を送る", "Step through chord types" },
	{ STED_SCR_CHORD, 0, "F1-F10", "メジャー系。Shift ではマイナー系", "Major chords. Shift selects the minor set" },
	{ STED_SCR_CHORD, 0, "up | down", "構成音を回転する", "Rotate the chord tones" },
	{ STED_SCR_CHORD, 0, "rup | rdown", "ルート音を上下する", "Raise or lower the root" },
	{ STED_SCR_CHORD, 0, "M | xf1 | undo", "コードを鳴らす", "Play the chord" },
	{ STED_SCR_CHORD, 0, "del", "グラフィック表示を切り替える", "Toggle the graphic" },
	{ STED_SCR_CHORD, 0, "ret", "トラックへ書き込む", "Write the chord" },
	{ STED_SCR_CHORD, 0, "esc", "キャンセルする", "Cancel" },

	{ STED_SCR_SCALE, 0, "space", "メジャーとマイナーを切り替える", "Switch major and minor" },
	{ STED_SCR_SCALE, 0, "plus | hash", "シャープ側にする", "Use sharps" },
	{ STED_SCR_SCALE, 0, "minus | b", "フラット側にする", "Use flats" },
	{ STED_SCR_SCALE, 0, "0-7", "シャープまたはフラットの数", "The number of sharps or flats" },
	{ STED_SCR_SCALE, 0, "rup | rdown", "スケールを1つ送る", "Step to the next or previous scale" },
	{ STED_SCR_SCALE, 0, "ret", "決定する", "Choose" },
	{ STED_SCR_SCALE, 0, "esc", "キャンセルする", "Cancel" },

	{ STED_SCR_INPMON, 0, "esc", "抜ける", "Leave" },

	{ STED_SCR_FILESEL, 0, "", "macOS のファイルダイアログが開いています。", "The macOS file dialog is open." },
};

static int row_count(void)
{
	return (int)(sizeof kRows / sizeof kRows[0]);
}

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static int sId[32];
static int sLayer[32];
static int sDepth;
static int sOverflow;

static int gId;
static int gLayer;
static NSPanel *sPanel;
static NSView *sRoot;
static NSView *sList;

static int
ui_ja(void)
{
	NSArray *locs = [[NSBundle mainBundle] preferredLocalizations];
	NSString *first = [locs count] > 0 ? [locs objectAtIndex:0] : @"";
	return [first hasPrefix:@"ja"];
}

static void apply_ui(int ident, int layer);

static void
publish(void)
{
	int ident, layer;

	pthread_mutex_lock(&sLock);
	if (sDepth > 0) {
		ident = sId[sDepth - 1];
		layer = sLayer[sDepth - 1];
	} else {
		ident = STED_SCR_NONE;
		layer = 0;
	}
	pthread_mutex_unlock(&sLock);
	dispatch_async(dispatch_get_main_queue(), ^{
		apply_ui(ident, layer);
	});
}

void
STedScreenPush(int ident)
{
	pthread_mutex_lock(&sLock);
	if (sDepth >= 32)
		sOverflow++;
	else {
		sId[sDepth] = ident;
		sLayer[sDepth] = 0;
		sDepth++;
	}
	pthread_mutex_unlock(&sLock);
	publish();
}

void
STedScreenPop(void)
{
	pthread_mutex_lock(&sLock);
	if (sOverflow > 0)
		sOverflow--;
	else if (sDepth > 0)
		sDepth--;
	pthread_mutex_unlock(&sLock);
	publish();
}

int
STedScreenCurrent(void)
{
	int ident;

	pthread_mutex_lock(&sLock);
	ident = (sDepth > 0) ? sId[sDepth - 1] : STED_SCR_NONE;
	pthread_mutex_unlock(&sLock);
	return ident;
}

void
STedScreenLayer(int layer)
{
	int changed = 0;

	pthread_mutex_lock(&sLock);
	if (sDepth > 0 && sId[sDepth - 1] == STED_SCR_TRACK_EDIT &&
	    sLayer[sDepth - 1] != layer) {
		sLayer[sDepth - 1] = layer;
		changed = 1;
	}
	pthread_mutex_unlock(&sLock);
	if (changed)
		publish();
}

@interface CommandRefList : NSView
- (void)reloadScrollTop:(BOOL)top;
@end

@interface CommandRefRoot : NSView
@end

static NSString *
piece_text(NSString *piece)
{
	if ([piece isEqualToString:@"shift"]) return @"⇧";
	if ([piece isEqualToString:@"ctrl"]) return @"⌃";
	if ([piece isEqualToString:@"opt"]) return @"⌥";
	if ([piece isEqualToString:@"left"]) return @"←";
	if ([piece isEqualToString:@"right"]) return @"→";
	if ([piece isEqualToString:@"up"]) return @"↑";
	if ([piece isEqualToString:@"down"]) return @"↓";
	if ([piece isEqualToString:@"space"]) return @"space";
	if ([piece isEqualToString:@"esc"]) return @"esc";
	if ([piece isEqualToString:@"ret"]) return @"↩";
	if ([piece isEqualToString:@"tab"]) return @"tab";
	if ([piece isEqualToString:@"home"]) return @"home";
	if ([piece isEqualToString:@"del"]) return @"del";
	if ([piece isEqualToString:@"undo"]) return @"undo";
	if ([piece isEqualToString:@"clr"]) return @"clr";
	if ([piece isEqualToString:@"help"]) return @"help";
	if ([piece isEqualToString:@"kana"]) return @"かな";
	if ([piece isEqualToString:@"kigou"]) return NSLocalizedString(@"KIGOU", @"X68000 key");
	if ([piece isEqualToString:@"touroku"]) return NSLocalizedString(@"TOUROKU", @"X68000 key");
	if ([piece isEqualToString:@"rup"]) return @"RUP";
	if ([piece isEqualToString:@"rdown"]) return @"RDOWN";
	if ([piece isEqualToString:@"plus"]) return @"+";
	if ([piece isEqualToString:@"minus"]) return @"−";
	if ([piece isEqualToString:@"greater"]) return @">";
	if ([piece isEqualToString:@"less"]) return @"<";
	if ([piece isEqualToString:@"eq"]) return @"=";
	if ([piece isEqualToString:@"star"]) return @"*";
	if ([piece isEqualToString:@"hat"]) return @"^";
	if ([piece isEqualToString:@"slash"]) return @"/";
	if ([piece isEqualToString:@"under"]) return @"_";
	if ([piece isEqualToString:@"at"]) return @"@";
	if ([piece isEqualToString:@"colon"]) return @":";
	if ([piece isEqualToString:@"lbracket"]) return @"[";
	if ([piece isEqualToString:@"rbracket"]) return @"]";
	if ([piece isEqualToString:@"backslash"]) return @"\\";
	if ([piece isEqualToString:@"bs"]) return @"BS";
	if ([piece isEqualToString:@"dollar"]) return @"$";
	if ([piece isEqualToString:@"hash"]) return @"#";
	if ([piece hasPrefix:@"xf"])
		return [NSString stringWithFormat:@"XF%@", [piece substringFromIndex:2]];
	if ([piece hasPrefix:@"f"] && [piece length] > 1)
		return [NSString stringWithFormat:@"F%@", [piece substringFromIndex:1]];
	return piece;
}

static CGFloat
draw_chips(NSString *spec, NSPoint origin, NSColor *fill, NSColor *stroke, NSColor *text, BOOL draw)
{
	NSFont *font = [NSFont systemFontOfSize:12];
	NSArray *alts = [spec componentsSeparatedByString:@" | "];
	CGFloat x = origin.x;
	NSUInteger a;

	for (a = 0; a < [alts count]; a++) {
		NSArray *parts = [[alts objectAtIndex:a] componentsSeparatedByString:@","];
		NSMutableString *label = [NSMutableString string];
		NSUInteger i;
		NSDictionary *attr;
		NSSize sz;
		NSRect r;

		for (i = 0; i < [parts count]; i++) {
			NSString *one = piece_text([[parts objectAtIndex:i] stringByTrimmingCharactersInSet:
				[NSCharacterSet whitespaceCharacterSet]]);
			if ([one length] == 0)
				continue;
			if ([label length] > 0 && [one length] > 1)
				[label appendString:@" "];
			[label appendString:one];
		}
		if ([label length] == 0)
			continue;
		attr = [NSDictionary dictionaryWithObjectsAndKeys:
			font, NSFontAttributeName, text, NSForegroundColorAttributeName, nil];
		sz = [label sizeWithAttributes:attr];
		r = NSMakeRect(x, origin.y, sz.width + 14, 20);
		if (draw) {
			NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:r xRadius:5 yRadius:5];
			[fill setFill];
			[stroke setStroke];
			[path setLineWidth:1];
			[path fill];
			[path stroke];
			[label drawWithRect:NSMakeRect(r.origin.x + 7, r.origin.y + 1, sz.width, 18)
				options:NSStringDrawingUsesLineFragmentOrigin
				attributes:attr];
		}
		x += r.size.width + 6;
		if (a + 1 < [alts count]) {
			if (draw) {
				NSDictionary *orAttr = [NSDictionary dictionaryWithObjectsAndKeys:
					font, NSFontAttributeName, text, NSForegroundColorAttributeName, nil];
				[@"/" drawWithRect:NSMakeRect(x, origin.y + 1, 12, 18)
					options:NSStringDrawingUsesLineFragmentOrigin
					attributes:orAttr];
			}
			x += 14;
		}
	}
	return x - origin.x;
}

@implementation CommandRefList

- (BOOL)isFlipped
{
	return YES;
}

- (CGFloat)layoutWidth:(CGFloat)width draw:(BOOL)draw
{
	BOOL ja = ui_ja();
	BOOL dark = NO;
	NSColor *textColor, *subColor, *chipFill, *chipStroke, *headColor, *dimColor;
	NSFont *body = [NSFont systemFontOfSize:13];
	NSFont *head = [NSFont boldSystemFontOfSize:13];
	CGFloat y = 8;
	int active = 0;
	int i;
	int screen = gId;

	if (@available(macOS 10.14, *)) {
		NSAppearanceName match = [self.effectiveAppearance
			bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
		dark = [match isEqualToString:NSAppearanceNameDarkAqua];
	}
	if (dark) {
		if (draw)
			[[NSColor colorWithCalibratedWhite:0.12 alpha:1] setFill];
		textColor = [NSColor whiteColor];
		subColor = [NSColor colorWithCalibratedWhite:0.62 alpha:1];
		chipFill = [NSColor colorWithCalibratedWhite:0.24 alpha:1];
		chipStroke = [NSColor colorWithCalibratedWhite:0.45 alpha:1];
		headColor = [NSColor colorWithCalibratedRed:0.55 green:0.78 blue:1 alpha:1];
	} else {
		if (draw)
			[[NSColor colorWithCalibratedWhite:0.96 alpha:1] setFill];
		textColor = [NSColor blackColor];
		subColor = [NSColor colorWithCalibratedWhite:0.35 alpha:1];
		chipFill = [NSColor whiteColor];
		chipStroke = [NSColor colorWithCalibratedWhite:0.55 alpha:1];
		headColor = [NSColor colorWithCalibratedRed:0.10 green:0.35 blue:0.72 alpha:1];
	}
	dimColor = subColor;
	if (draw)
		NSRectFill([self bounds]);

	if (screen == STED_SCR_TRACK_EDIT) {
		if (gLayer == STED_LAYER_SHIFT)
			active = 2;
		else if (gLayer >= STED_LAYER_ST)
			active = 3;
		else
			active = 1;
	}

	for (i = 0; i < row_count(); i++) {
		const Row *row = &kRows[i];
		NSString *desc;
		BOOL dim;
		NSColor *color;
		NSDictionary *attr;
		NSRect textRect;
		CGFloat textX;

		if (row->screen != screen)
			continue;
		desc = [NSString stringWithUTF8String:(ja ? row->ja : row->en)];
		if (desc == nil)
			desc = @"";
		dim = (row->group >= 1 && row->group <= 3 && active != 0 && row->group != active);
		color = dim ? dimColor : textColor;

		if (row->keys == NULL) {
			attr = [NSDictionary dictionaryWithObjectsAndKeys:
				head, NSFontAttributeName,
				(row->group == active ? headColor : color), NSForegroundColorAttributeName, nil];
			if (draw)
				[desc drawWithRect:NSMakeRect(12, y, width - 24, 20)
					options:NSStringDrawingUsesLineFragmentOrigin
					attributes:attr];
			y += 26;
			if (row->group == 3) {
				int n;
				for (n = 0; n < 10; n++) {
					char spec[8];
					NSString *nums;
					NSColor *fc = dim ? dimColor : textColor;
					snprintf(spec, sizeof spec, "f%d", n + 1);
					nums = [NSString stringWithFormat:@"ST %d    GT %d    VEL %d",
						(int)stgt_tbl[n], (int)stgt_tbl[n + 10], (int)stgt_tbl[n + 20]];
					if (draw)
						draw_chips([NSString stringWithUTF8String:spec],
							NSMakePoint(16, y + 4), chipFill, chipStroke, fc, YES);
					textRect = NSMakeRect(150, y + 2, width - 166, 22);
					attr = [NSDictionary dictionaryWithObjectsAndKeys:
						body, NSFontAttributeName, fc, NSForegroundColorAttributeName, nil];
					if (draw)
						[nums drawWithRect:textRect
							options:NSStringDrawingTruncatesLastVisibleLine | NSStringDrawingUsesLineFragmentOrigin
							attributes:attr];
					y += 28;
				}
			}
			continue;
		}
		if (row->keys[0] == 0) {
			textRect = NSMakeRect(12, y, width - 24, 36);
			attr = [NSDictionary dictionaryWithObjectsAndKeys:
				body, NSFontAttributeName, textColor, NSForegroundColorAttributeName, nil];
			if (draw)
				[desc drawWithRect:textRect
					options:NSStringDrawingUsesLineFragmentOrigin
					attributes:attr];
			y += 40;
			continue;
		}
		if (draw)
			draw_chips([NSString stringWithUTF8String:row->keys],
				NSMakePoint(16, y + 6), chipFill, chipStroke, color, YES);
		textX = 220;
		textRect = NSMakeRect(textX, y + 2, width - textX - 16, 32);
		attr = [NSDictionary dictionaryWithObjectsAndKeys:
			body, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
		if (draw)
			[desc drawWithRect:textRect
				options:NSStringDrawingTruncatesLastVisibleLine | NSStringDrawingUsesLineFragmentOrigin
				attributes:attr];
		y += 36;
	}
	if (y == 8) {
		NSString *miss = NSLocalizedString(@"No command list for this screen yet.", @"Command reference");
		NSDictionary *attr = [NSDictionary dictionaryWithObjectsAndKeys:
			body, NSFontAttributeName, textColor, NSForegroundColorAttributeName, nil];
		if (draw)
			[miss drawWithRect:NSMakeRect(12, y, width - 24, 24)
				options:NSStringDrawingUsesLineFragmentOrigin
				attributes:attr];
		y += 32;
	}
	return y + 12;
}

- (void)drawRect:(NSRect)dirty
{
	(void)dirty;
	[self layoutWidth:NSWidth([self bounds]) draw:YES];
}

- (void)viewDidMoveToWindow
{
	NSClipView *clip = [[self enclosingScrollView] contentView];

	[super viewDidMoveToWindow];
	if (clip == nil)
		return;
	[clip setPostsBoundsChangedNotifications:YES];
	[[NSNotificationCenter defaultCenter] removeObserver:self
		name:NSViewBoundsDidChangeNotification object:clip];
	[[NSNotificationCenter defaultCenter] addObserver:self
		selector:@selector(clipBoundsChanged:)
		name:NSViewBoundsDidChangeNotification object:clip];
}

- (void)clipBoundsChanged:(NSNotification *)note
{
	CGFloat w = NSWidth([[[self enclosingScrollView] contentView] bounds]);
	CGFloat dw = w - NSWidth([self frame]);

	(void)note;
	if (dw < 0)
		dw = -dw;
	if (dw > 1.0)
		[self reloadScrollTop:NO];
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[super dealloc];
}

- (void)reloadScrollTop:(BOOL)top
{
	NSScrollView *sv = [self enclosingScrollView];
	CGFloat w = sv != nil ? NSWidth([[sv contentView] bounds]) : 720;
	CGFloat h;

	if (w < 200)
		w = 720;
	h = [self layoutWidth:w draw:NO];
	if (h < 20)
		h = 20;
	[self setFrameSize:NSMakeSize(w, h)];
	[self setNeedsDisplay:YES];
	if (top && sv != nil) {
		[[sv contentView] scrollToPoint:NSZeroPoint];
		[sv reflectScrolledClipView:[sv contentView]];
	}
}

@end

@implementation CommandRefRoot

- (void)drawRect:(NSRect)dirty
{
	NSRect b = [self bounds];
	BOOL dark = NO;
	BOOL ja = ui_ja();
	NSString *title;
	NSString *layer = nil;
	NSDictionary *titleAttr, *layerAttr;
	NSColor *textColor, *subColor;
	const char *name;

	(void)dirty;
	if (@available(macOS 10.14, *)) {
		NSAppearanceName match = [self.effectiveAppearance
			bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
		dark = [match isEqualToString:NSAppearanceNameDarkAqua];
	}
	if (dark) {
		[[NSColor colorWithCalibratedWhite:0.12 alpha:1] setFill];
		textColor = [NSColor whiteColor];
		subColor = [NSColor colorWithCalibratedWhite:0.75 alpha:1];
	} else {
		[[NSColor colorWithCalibratedWhite:0.94 alpha:1] setFill];
		textColor = [NSColor blackColor];
		subColor = [NSColor colorWithCalibratedWhite:0.25 alpha:1];
	}
	NSRectFill(b);
	name = (gId >= 0 && gId < STED_SCR_COUNT) ? (ja ? kNameJa[gId] : kNameEn[gId]) : "";
	title = [NSString stringWithUTF8String:name];
	if (title == nil)
		title = @"";
	if (gId == STED_SCR_TRACK_EDIT) {
		if (gLayer == STED_LAYER_SHIFT)
			layer = NSLocalizedString(@"Active: Shift", @"Command reference");
		else if (gLayer == STED_LAYER_GT)
			layer = NSLocalizedString(@"Active: Shift+Option (GT)", @"Command reference");
		else if (gLayer == STED_LAYER_VEL)
			layer = NSLocalizedString(@"Active: Shift+Option (VEL)", @"Command reference");
		else if (gLayer == STED_LAYER_ST)
			layer = NSLocalizedString(@"Active: Shift+Option (ST)", @"Command reference");
		else
			layer = NSLocalizedString(@"Active: Normal", @"Command reference");
	}
	titleAttr = [NSDictionary dictionaryWithObjectsAndKeys:
		[NSFont boldSystemFontOfSize:15], NSFontAttributeName,
		textColor, NSForegroundColorAttributeName, nil];
	[title drawAtPoint:NSMakePoint(16, b.size.height - 26) withAttributes:titleAttr];
	if (layer != nil) {
		layerAttr = [NSDictionary dictionaryWithObjectsAndKeys:
			[NSFont systemFontOfSize:12], NSFontAttributeName,
			subColor, NSForegroundColorAttributeName, nil];
		[layer drawAtPoint:NSMakePoint(16, b.size.height - 46) withAttributes:layerAttr];
	}
}

@end

static void
apply_ui(int ident, int layer)
{
	BOOL screenChanged = (ident != gId);

	gId = ident;
	gLayer = layer;
	if (sRoot == nil)
		return;
	[sRoot setNeedsDisplay:YES];
	[(CommandRefList *)sList reloadScrollTop:screenChanged];
}

/* Read-only palette. A click must not take the key window away from STed2,
   or the emulator stays in its key wait. */
@interface CommandRefPanel : NSPanel
@end

@implementation CommandRefPanel

- (BOOL)canBecomeKeyWindow
{
	return NO;
}

@end

static void
ensure_panel(void)
{
	CommandRefRoot *root;
	NSScrollView *scroll;
	CommandRefList *list;
	NSRect frame;

	if (sPanel != nil)
		return;
	frame = NSMakeRect(0, 0, 760, 520);
	sPanel = [[CommandRefPanel alloc] initWithContentRect:frame
					    styleMask:(NSWindowStyleMaskTitled |
						       NSWindowStyleMaskClosable |
						       NSWindowStyleMaskResizable |
						       NSWindowStyleMaskUtilityWindow |
						       NSWindowStyleMaskNonactivatingPanel)
					      backing:NSBackingStoreBuffered
						defer:NO];
	[sPanel setFloatingPanel:YES];
	[sPanel setBecomesKeyOnlyIfNeeded:YES];
	[sPanel setHidesOnDeactivate:NO];
	[sPanel setLevel:NSFloatingWindowLevel];
	[sPanel setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
		NSWindowCollectionBehaviorFullScreenAuxiliary];
	[sPanel setReleasedWhenClosed:NO];
	[sPanel setMinSize:NSMakeSize(480, 280)];
	root = [[CommandRefRoot alloc] initWithFrame:frame];
	scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height - 56)];
	[scroll setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[scroll setHasVerticalScroller:YES];
	[scroll setAutohidesScrollers:YES];
	[scroll setDrawsBackground:NO];
	[scroll setBorderType:NSNoBorder];
	list = [[CommandRefList alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, 10)];
	[list setAutoresizingMask:NSViewWidthSizable];
	[scroll setDocumentView:list];
	[root addSubview:scroll];
	[sPanel setContentView:root];
	sRoot = root;
	sList = list;
	[list release];
	[scroll release];
	[root release];
}

void
CommandReferenceShow(void)
{
	int ident, layer;

	ensure_panel();
	[sPanel setTitle:NSLocalizedString(@"Command Cheat Sheet", @"Help menu")];
	pthread_mutex_lock(&sLock);
	if (sDepth > 0) {
		ident = sId[sDepth - 1];
		layer = sLayer[sDepth - 1];
	} else {
		ident = STED_SCR_NONE;
		layer = 0;
	}
	pthread_mutex_unlock(&sLock);
	apply_ui(ident, layer);
	if (![sPanel isVisible])
		[sPanel center];
	[sPanel orderFront:nil];
}
